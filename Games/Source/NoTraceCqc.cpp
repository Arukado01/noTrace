#include "NoTraceSimulation.h"
#include "Config/NoTraceCqcConfig.h"
#include "NoTraceVisibility.h"
#include "NoTraceWorldGeometry.h"
#include <algorithm>
#include <cmath>

namespace NoTrace {
  namespace {
    sf::Vector2f CqcUnit(sf::Vector2f value) { return value.lengthSquared() > 0.0001f ? value / value.length() : sf::Vector2f{1.f, 0.f}; }
    bool CqcGuardAvailable(const Guard &guard) { return guard.state != GuardState::Dead && guard.state != GuardState::Incapacitated && !guard.bodyHidden && guard.heldBy == 0 && guard.cqcState == CqcState::None; }
    bool CqcAgentAvailable(const Agent &agent) { return agent.connected && agent.alive && !agent.incapacitated && !agent.hidden && !agent.extracted && agent.cqcHeldBy == 0 && agent.cqcHoldingAgent == 0 && agent.cqcHoldingGuard == 0 && agent.stance != Stance::Prone; }
  }

  void Simulation::ReleaseCqc(Agent &agent) {
    CancelReload(agent);
    const bool interrupted = agent.cqcHoldingGuard != 0 || agent.cqcHoldingAgent != 0 || agent.cqcHeldBy != 0 || agent.cqcPressPending || agent.cqcAttackTime > 0.f;
    if (agent.cqcHeldBy != 0) {
      const auto owner = std::ranges::find_if(world_.agents, [&](const Agent &entry) { return entry.id == agent.cqcHeldBy; });
      agent.cqcHeldBy = 0;
      if (owner != world_.agents.end() && owner->cqcHoldingAgent == agent.id) ReleaseCqc(*owner);
    }
    for (auto &other : world_.agents) if (other.cqcHeldBy == agent.id) {
      CancelReload(other);
      other.cqcHeldBy = 0; other.cqcInputDown = true; other.cqcPressPending = false;
      other.actionPose = ActionPose::Recover; other.actionPoseTime = CqcConfig::PunchRecovery;
    }
    for (auto &guard : world_.guards) if (guard.heldBy == agent.id) {
      guard.heldBy = 0; guard.cqcState = CqcState::None; guard.stateTime = 0.f;
      if (guard.state != GuardState::Dead && guard.state != GuardState::Incapacitated) {
        guard.state = GuardState::Search; guard.lastKnown = agent.position;
      }
    }
    agent.cqcHoldingGuard = 0; agent.cqcHoldingAgent = 0; agent.cqcHoldTime = 0.f; agent.cqcReleaseTime = 0.f;
    agent.cqcPulseTime = 0.f; agent.cqcPulseReady = false; agent.cqcProgress = 0;
    agent.cqcPressPending = false; agent.cqcStrikePending = false; agent.cqcAttackTime = 0.f;
    if (interrupted) {
      agent.cqcInputDown = true;
      agent.actionPose = ActionPose::Recover; agent.actionPoseTime = CqcConfig::PunchRecovery;
    }
  }

  bool Simulation::UpdateCqc(Agent &agent, const ActionCommand &command, float dt) {
    using namespace CqcConfig;
    const bool wasDown = agent.cqcInputDown;
    const bool newPress = !wasDown && (command.cqc || command.cqcHeld);
    agent.cqcInputDown = command.cqcHeld;
    const bool incompatible = command.push || command.tool || command.interact || command.coverToggle || command.prone || command.dragBody || command.throwDecoy || command.knock || command.reloadWeapon || command.weaponChoice >= 0;
    if (!IsTactical(world_) || !agent.connected || !agent.alive || agent.incapacitated || agent.hidden || agent.extracted || agent.draggingGuard >= 0) {
      ReleaseCqc(agent); return false;
    }
    if (agent.stance == Stance::Prone) {
      if (agent.cqcHoldingGuard != 0 || agent.cqcHoldingAgent != 0 || agent.cqcHeldBy != 0 || agent.cqcPressPending || agent.cqcAttackTime > 0.f) ReleaseCqc(agent);
      return false;
    }
    if (agent.cqcHoldingGuard != 0 || agent.cqcHoldingAgent != 0) {
      Guard *guard = nullptr; Agent *partner = nullptr;
      if (agent.cqcHoldingGuard != 0) {
        const auto found = std::ranges::find_if(world_.guards, [&](const Guard &entry) { return entry.id == agent.cqcHoldingGuard; });
        if (found != world_.guards.end() && found->heldBy == agent.id && found->state != GuardState::Dead && found->state != GuardState::Incapacitated) guard = &*found;
      } else {
        const auto found = std::ranges::find_if(world_.agents, [&](const Agent &entry) { return entry.id == agent.cqcHoldingAgent; });
        if (found != world_.agents.end() && found->cqcHeldBy == agent.id && found->connected && found->alive && !found->incapacitated && !found->hidden && !found->extracted) partner = &*found;
      }
      const auto targetPosition = guard ? guard->position : (partner ? partner->position : agent.position);
      if ((!guard && !partner) || (targetPosition - agent.position).length() > GameplayConfig::CqcDistance * GameplayConfig::CqcMaximumStretch ||
          !WorldGeometry::ClearMovementSegment(world_, agent.position, targetPosition, GameplayConfig::AgentRadius) || incompatible) {
        ReleaseCqc(agent); return false;
      }
      agent.moving = false; agent.running = false; agent.cover = {}; agent.decoyGuide = {};
      if (guard) { guard->facing = agent.facing; guard->attackTime = 0.f; guard->stateTime = 0.f; guard->navigation.clear(); }
      else { partner->facing = agent.facing; partner->moving = false; partner->running = false; partner->cover = {}; partner->actionPose = ActionPose::Grab; partner->actionPoseTime = GameplayConfig::AttackAnimationDuration; }
      agent.cqcPulseTime = std::min(MaximumPressBuffer, agent.cqcPulseTime + dt);
      if (!command.cqcHeld && !command.cqc) {
        agent.cqcReleaseTime += dt;
        if (agent.cqcReleaseTime >= MinimumRelease) agent.cqcPulseReady = true;
      } else if (!newPress) agent.cqcReleaseTime = 0.f;
      if (newPress && agent.cqcPulseReady && agent.cqcPulseTime >= PulseInterval && agent.stamina >= GameplayConfig::CqcStaminaCost) {
        agent.cqcPulseReady = false; agent.cqcReleaseTime = 0.f; agent.cqcPulseTime = 0.f;
        agent.stamina -= GameplayConfig::CqcStaminaCost; ++agent.cqcProgress;
        agent.actionPose = ActionPose::Choke; agent.actionPoseTime = GameplayConfig::AttackAnimationDuration;
        EmitSound(SoundKind::Choke, targetPosition, GripSoundRadius, agent.id);
        if (guard && !IsTraining(world_)) Record(EventKind::Cqc, agent.id, guard->id);
        if (agent.cqcProgress >= RequiredPulses) {
          const auto guardId = guard ? guard->id : 0;
          const auto partnerId = partner ? partner->id : 0;
          ReleaseCqc(agent);
          if (guardId != 0) {
            const auto target = std::ranges::find_if(world_.guards, [guardId](const Guard &entry) { return entry.id == guardId; });
            if (target != world_.guards.end()) HitGuard(*target, HitZone::Head, false, agent.id);
          } else {
            const auto target = std::ranges::find_if(world_.agents, [partnerId](const Agent &entry) { return entry.id == partnerId; });
            if (target != world_.agents.end()) Incapacitate(*target, agent.id, partnerId);
          }
          world_.message = IsTraining(world_) ? "Sometimiento de práctica completado; el objetivo continúa operativo." : guardId != 0 ? "Sometimiento completado: guardia inconsciente." : "Compañero inconsciente, sin eliminación ni puntuación enemiga; puede ser reanimado.";
          return true;
        }
        if (guard) guard->cqcState = agent.cqcProgress == 1 ? CqcState::KnockedDown : CqcState::Restrained;
        world_.message = "Sujeción: suelta brevemente y vuelve a pulsar F para someter.";
      }
      if (agent.actionPoseTime <= 0.f) { agent.actionPose = ActionPose::Grab; agent.actionPoseTime = GameplayConfig::AttackAnimationDuration; }
      if (agent.cqcReleaseTime > ReleaseGrace || agent.stamina <= 0.f) { ReleaseCqc(agent); return false; }
      return true;
    }
    if (agent.cqcAttackTime > 0.f) {
      if (incompatible || command.fireLethal || command.fireNonLethal || command.fireSelected) { ReleaseCqc(agent); return false; }
      const bool kick = !agent.cqcNextKick;
      const float duration = kick ? KickCooldown : PunchCooldown;
      const float windup = kick ? KickWindup : PunchWindup;
      const float recovery = kick ? KickRecovery : PunchRecovery;
      agent.cqcAttackTime = std::max(0.f, agent.cqcAttackTime - dt);
      if (agent.cqcStrikePending && agent.cqcAttackTime <= duration - windup) {
        agent.cqcStrikePending = false;
        Guard *target = nullptr; float nearest = kick ? KickReach : PunchReach;
        for (auto &enemy : world_.guards) {
          const auto offset = enemy.position - agent.position;
          const float distance = offset.length();
          if (!CqcGuardAvailable(enemy) || (IsTraining(world_) && !enemy.trainingDummy) || distance <= 0.01f || distance >= nearest || CqcUnit(agent.facing).dot(CqcUnit(offset)) < FacingCosine ||
              !WorldGeometry::ClearMovementSegment(world_, agent.position, enemy.position, GameplayConfig::AgentRadius)) continue;
          target = &enemy; nearest = distance;
        }
        EmitSound(kick ? SoundKind::Kick : SoundKind::Punch, agent.position, kick ? KickNoise : PunchNoise, agent.id);
        EmitNoise(agent.position, kick ? KickNoise : PunchNoise, agent.id);
        if (target) {
          if (DamageTrainingDummy(*target, kick ? KickStun : PunchStun)) return true;
          target->stun = std::min(CombatConfig::MaxGuardStun, target->stun + (kick ? KickStun : PunchStun));
          target->staggerRemaining = kick ? KickStagger : PunchStagger;
          target->attackTime = 0.f; target->navigation.clear(); target->lastKnown = agent.position;
          Record(EventKind::Cqc, agent.id, target->id);
          if (target->stun >= CombatConfig::MaxGuardStun) {
            target->state = GuardState::Incapacitated; target->incapacityRemaining = GameplayConfig::GuardIncapacityDuration;
            target->heldBy = 0; target->cqcState = CqcState::None;
            Record(EventKind::GuardIncapacitated, agent.id, target->id);
          } else { target->state = GuardState::Search; target->stateTime = 0.f; }
        }
      }
      if (agent.cqcAttackTime <= recovery) agent.actionPose = agent.cqcAttackTime > 0.f ? ActionPose::Recover : ActionPose::None;
      agent.actionPoseTime = agent.cqcAttackTime; agent.moving = false; agent.running = false;
      return true;
    }
    if (incompatible || command.fireLethal || command.fireNonLethal || command.fireSelected || agent.actionCooldown > 0.f || agent.cover.active || agent.reloadRemaining > 0.f) {
      agent.cqcPressPending = false; agent.cqcHoldTime = 0.f; return false;
    }
    if (newPress) { agent.cqcPressPending = true; agent.cqcHoldTime = 0.f; }
    if (!agent.cqcPressPending) return false;
    if (!command.cqcHeld) {
      const bool kick = agent.cqcNextKick;
      const float staminaCost = kick ? KickStamina : PunchStamina;
      const bool shortPress = agent.cqcHoldTime < AcquireHold;
      agent.cqcPressPending = false; agent.cqcHoldTime = 0.f;
      if (!shortPress || agent.stamina < staminaCost) return false;
      agent.stamina -= staminaCost; agent.cqcNextKick = !kick;
      agent.cqcAttackTime = kick ? KickCooldown : PunchCooldown; agent.cqcStrikePending = true;
      agent.actionPose = kick ? ActionPose::Kick : ActionPose::Punch; agent.actionPoseTime = agent.cqcAttackTime;
      agent.moving = false; agent.running = false; agent.decoyGuide = {};
      return true;
    }
    agent.cqcHoldTime = std::min(MaximumPressBuffer, agent.cqcHoldTime + dt);
    if (agent.cqcHoldTime < AcquireHold || agent.stamina < GameplayConfig::CqcStaminaCost) return false;
    Guard *target = nullptr; Agent *partner = nullptr; float nearest = GameplayConfig::CqcDistance;
    const auto rearEligible = [&](sf::Vector2f position, sf::Vector2f facing) {
      const auto offset = position - agent.position; const float distance = offset.length();
      if (distance < MinimumGripDistance || distance >= nearest || CqcUnit(agent.facing).dot(CqcUnit(offset)) < FacingCosine ||
          CqcUnit(facing).dot(CqcUnit(offset)) < RearCosine || !WorldGeometry::ClearMovementSegment(world_, agent.position, position, GameplayConfig::AgentRadius)) return false;
      nearest = distance; return true;
    };
    for (auto &enemy : world_.guards) if (CqcGuardAvailable(enemy) && (!IsTraining(world_) || enemy.trainingDummy) && enemy.state != GuardState::Alert && enemy.suspicion < 1.f && rearEligible(enemy.position, enemy.facing)) { target = &enemy; partner = nullptr; }
    for (auto &other : world_.agents) if (other.id != agent.id && CqcAgentAvailable(other) && !other.cqcPressPending && other.cqcAttackTime <= 0.f && rearEligible(other.position, other.facing)) { partner = &other; target = nullptr; }
    if (!target && !partner) return false;
    const auto targetPosition = target ? target->position : partner->position;
    agent.cqcHoldingGuard = target ? target->id : 0; agent.cqcHoldingAgent = partner ? partner->id : 0;
    agent.cqcProgress = 0; agent.cqcReleaseTime = 0.f; agent.cqcPulseTime = 0.f; agent.cqcPulseReady = false;
    agent.cqcHoldTime = 0.f; agent.cqcPressPending = false;
    agent.stamina = std::max(0.f, agent.stamina - GameplayConfig::CqcStaminaCost);
    agent.facing = CqcUnit(targetPosition - agent.position); agent.cover = {}; agent.decoyGuide = {}; agent.moving = false; agent.running = false;
    agent.actionPose = ActionPose::Grab; agent.actionPoseTime = GameplayConfig::AttackAnimationDuration;
    if (target) {
      target->heldBy = agent.id; target->cqcState = CqcState::Grabbed; target->stateTime = 0.f;
      target->attackTime = 0.f; target->facing = agent.facing; target->navigation.clear();
      if (!IsTraining(world_)) Record(EventKind::Cqc, agent.id, target->id);
    } else {
      CancelPuzzle(*partner);
      CancelReload(*partner);
      partner->cqcHeldBy = agent.id; partner->facing = agent.facing; partner->cover = {}; partner->decoyGuide = {};
      partner->moving = false; partner->running = false; partner->draggingGuard = -1;
      partner->actionPose = ActionPose::Grab; partner->actionPoseTime = GameplayConfig::AttackAnimationDuration;
    }
    EmitSound(SoundKind::Grab, targetPosition, GripSoundRadius, agent.id);
    world_.message = target ? "Guardia agarrado por detrás. Suelta brevemente y pulsa F tres veces para someter." : "Compañero sujetado por detrás; suelta F para liberarlo sin daño.";
    return true;
  }

  void Simulation::Push(Agent &agent) {
    using namespace GameplayConfig;
    if (!IsTactical(world_) || agent.hidden || agent.incapacitated || !agent.alive || agent.stance == Stance::Prone || agent.draggingGuard >= 0 ||
        agent.actionCooldown > 0.f || agent.stamina < CqcStaminaCost) return;
    const auto facing = agent.facing.lengthSquared() > 0.0001f ? agent.facing / agent.facing.length() : sf::Vector2f{1.f, 0.f};
    Guard *enemy = nullptr; Agent *partner = nullptr;
    float nearest = CqcDistance;
    const auto eligible = [&](sf::Vector2f position) {
      const auto delta = position - agent.position;
      const float distance = delta.length();
      if (distance < 0.01f || distance >= nearest || facing.dot(delta / distance) < CqcConfig::FacingCosine ||
          !WorldGeometry::ClearMovementSegment(world_, agent.position, position, AgentRadius)) return false;
      nearest = distance; return true;
    };
    for (auto &guard : world_.guards) if (guard.state != GuardState::Dead && guard.state != GuardState::Incapacitated && !guard.bodyHidden && guard.heldBy == 0 && (!IsTraining(world_) || guard.trainingDummy) && eligible(guard.position)) { enemy = &guard; partner = nullptr; }
    for (auto &other : world_.agents) if (other.id != agent.id && CqcAgentAvailable(other) && eligible(other.position)) { partner = &other; enemy = nullptr; }
    if (!enemy && !partner) { world_.message = "Acércate y mira a un personaje para empujarlo con V."; return; }
    auto &position = enemy ? enemy->position : partner->position;
    const auto previous = position;
    const auto direction = (position - agent.position) / (position - agent.position).length();
    Move(position, direction * CqcConfig::PushDistance, AgentRadius);
    if ((position - previous).lengthSquared() < 0.0001f) { world_.message = "El obstáculo impide desplazar al objetivo."; return; }
    agent.stamina -= CqcStaminaCost; agent.actionCooldown = CqcConfig::PushCooldown;
    agent.actionPose = ActionPose::Push; agent.actionPoseTime = AttackAnimationDuration;
    if (enemy) {
      enemy->staggerRemaining = CqcConfig::PushStagger; enemy->state = GuardState::Search; enemy->stateTime = 0.f;
      enemy->lastKnown = agent.position; enemy->attackTime = 0.f; enemy->navigation.clear();
    } else {
      partner->cover = {}; partner->staggerRemaining = CqcConfig::PushStagger;
      partner->moving = false; partner->running = false;
    }
    if (!IsTraining(world_)) Record(EventKind::Cqc, agent.id, enemy ? enemy->id : partner->id);
    if (enemy) DamageTrainingDummy(*enemy, 0.f);
    EmitNoise(position, CqcConfig::PushNoise, agent.id);
    world_.message = "Empujón: desplazamiento bloqueado por la geometría sólida.";
  }
}
