#include "NoTraceSimulation.h"
#include "Config/NoTraceCqcConfig.h"
#include "NoTraceVisibility.h"
#include "NoTraceSpatialGeometry.h"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace NoTrace {
  namespace {
    enum class CqcInputContext { Held, CoverWall, Combat };
    sf::Vector2f CqcUnit(sf::Vector2f value) { return value.lengthSquared() > 0.0001f ? value / value.length() : sf::Vector2f{1.f, 0.f}; }
    bool CqcGuardAvailable(const Guard &guard) { return guard.state != GuardState::Dead && guard.state != GuardState::Incapacitated && !guard.bodyHidden && guard.heldBy == 0 && guard.cqcState == CqcState::None; }
    bool CqcAgentAvailable(const Agent &agent) { return agent.connected && agent.alive && !agent.incapacitated && !agent.hidden && !agent.extracted && agent.cqcHeldBy == 0 && agent.cqcHoldingAgent == 0 && agent.cqcHoldingGuard == 0 && agent.stance != Stance::Prone && agent.maneuver.kind == ManeuverKind::None && agent.traversal.mode == TraversalMode::Grounded; }
    bool CharacterPathClear(const WorldState &world, const Agent &agent, Simulation::CqcTarget target, sf::Vector2f from, sf::Vector2f to) {
      if (!WorldGeometry::ClearBodySegment(world, {from.x, from.y, agent.elevation}, {to.x, to.y, agent.elevation}, GameplayConfig::AgentRadius, WorldGeometry::ActorHeight(agent), agent.supportId)) return false;
      if (!world.walkSurfaces.empty()) {
        auto position = from; auto elevation = agent.elevation; auto support = agent.supportId;
        WorldGeometry::MoveOnSupports(world, position, elevation, support, to - from, GameplayConfig::AgentRadius, WorldGeometry::ActorHeight(agent));
        if ((position - to).length() > SpatialConfig::CoverPositionTolerance || std::abs(elevation - agent.elevation) > LevelConfig::MaximumStepHeight) return false;
      }
      const auto movement = to - from;
      const auto clear = [&](sf::Vector2f position) {
        const float along = movement.lengthSquared() > 0.f ? std::clamp((position - from).dot(movement) / movement.lengthSquared(), 0.f, 1.f) : 0.f;
        return (position - from - movement * along).length() >= GameplayConfig::AgentRadius * 2.f;
      };
      for (const auto &other : world.agents) if (other.id != agent.id && other.id != target.agent && other.connected && !other.hidden && !other.extracted && WorldGeometry::HeightOverlap(agent.elevation, agent.elevation + WorldGeometry::ActorHeight(agent), other.elevation, other.elevation + WorldGeometry::ActorHeight(other)) && !clear(other.position)) return false;
      for (const auto &other : world.guards) if (other.id != target.guard && !other.bodyHidden && other.storedIn == 0 && WorldGeometry::HeightOverlap(agent.elevation, agent.elevation + WorldGeometry::ActorHeight(agent), other.elevation, other.elevation + CombatConfig::StandingHeight) && !clear(other.position)) return false;
      return true;
    }
    std::optional<int> RearApproachSide(const WorldState &world, const Agent &agent, Simulation::CqcTarget target, sf::Vector2f position, sf::Vector2f facing) {
      using namespace CqcConfig;
      const auto offset = position - agent.position;
      if ((agent.position - (position - facing * GripSeparation)).length() <= ContactTolerance) return 0;
      const auto radial = CqcUnit(agent.position - position);
      const float radius = std::max(GripSeparation, offset.length());
      const auto start = position + radial * radius;
      if (!CharacterPathClear(world, agent, target, agent.position, start)) return std::nullopt;
      const float first = std::atan2(radial.y, radial.x);
      const auto rear = -facing;
      const float shortest = std::remainder(std::atan2(rear.y, rear.x) - first, 2.f * std::numbers::pi_v<float>);
      for (const int side : {shortest < 0.f ? -1 : 1, shortest < 0.f ? 1 : -1}) {
        float angle = shortest;
        if (side > 0 && angle < 0.f) angle += 2.f * std::numbers::pi_v<float>;
        if (side < 0 && angle > 0.f) angle -= 2.f * std::numbers::pi_v<float>;
        const int steps = std::max(1, static_cast<int>(std::ceil(std::abs(angle) * radius / (GameplayConfig::AgentRadius * 0.5f))));
        auto previous = start;
        bool clear = true;
        for (int step = 1; step <= steps && clear; ++step) {
          const float at = first + angle * static_cast<float>(step) / static_cast<float>(steps);
          const auto next = position + sf::Vector2f{std::cos(at), std::sin(at)} * radius;
          clear = CharacterPathClear(world, agent, target, previous, next); previous = next;
        }
        if (clear && CharacterPathClear(world, agent, target, previous, position + rear * GripSeparation)) return side;
      }
      return std::nullopt;
    }
    std::optional<sf::Vector2f> PushDestination(const WorldState &world, const Agent &agent, Simulation::CqcTarget target, sf::Vector2f position) {
      const auto direction = CqcUnit(position - agent.position);
      const int steps = std::max(1, static_cast<int>(std::ceil(CqcConfig::PushDistance / (GameplayConfig::AgentRadius * 0.5f))));
      auto destination = position;
      for (int step = 1; step <= steps; ++step) {
        const auto next = position + direction * (CqcConfig::PushDistance * static_cast<float>(step) / static_cast<float>(steps));
        if (!CharacterPathClear(world, agent, target, destination, next)) break;
        destination = next;
      }
      return (destination - position).length() > CqcConfig::ApproachBlockedTolerance ? std::optional{destination} : std::nullopt;
    }
  }

  std::optional<Simulation::CqcTarget> Simulation::FindCqcTarget(const WorldState &world, const Agent &agent, CqcAction action) {
    if (!IsTactical(world) || !CqcAgentAvailable(agent) || agent.draggingGuard >= 0 || agent.cover.active || agent.puzzle.target != 0 ||
        agent.reloadRemaining > 0.f || agent.staggerRemaining > 0.f || agent.actionCooldown > 0.f || agent.cqcAttackTime > 0.f || agent.stamina < GameplayConfig::CqcStaminaCost ||
        (action == CqcAction::Push && (agent.cqcPressPending || agent.cqcApproachGuard != 0 || agent.cqcApproachAgent != 0))) return std::nullopt;
    std::optional<CqcTarget> result;
    float nearest = action == CqcAction::Push ? GameplayConfig::CqcDistance : CqcConfig::AcquireDistance;
    float bestFacing = -1.f;
    const auto eligible = [&](CqcTarget target, sf::Vector2f position, sf::Vector2f facing, float elevation) {
      if (std::abs(elevation - agent.elevation) > LevelConfig::MaximumStepHeight) return;
      const auto offset = position - agent.position;
      const float distance = offset.length();
      const float alignment = CqcUnit(agent.facing).dot(CqcUnit(offset));
      if (distance < CqcConfig::MinimumGripDistance || distance > nearest || (distance == nearest && alignment <= bestFacing) || alignment < CqcConfig::FacingCosine ||
          !CharacterPathClear(world, agent, target, agent.position, position)) return;
      if (action == CqcAction::Push ? !PushDestination(world, agent, target, position).has_value() : !RearApproachSide(world, agent, target, position, CqcUnit(facing)).has_value()) return;
      if (action == CqcAction::Grab && std::ranges::any_of(world.agents, [&](const Agent &owner) {
        return owner.id != agent.id && owner.connected && owner.alive && !owner.incapacitated && owner.cqcPressPending &&
            ((target.guard != 0 && owner.cqcApproachGuard == target.guard) || (target.agent != 0 && owner.cqcApproachAgent == target.agent));
      })) return;
      result = target; nearest = distance; bestFacing = alignment;
    };
    for (const auto &guard : world.guards) if (CqcGuardAvailable(guard) && (!IsTraining(world) || guard.trainingDummy)) eligible({guard.id, 0}, guard.position, guard.facing, guard.elevation);
    for (const auto &other : world.agents) if (other.id != agent.id && CqcAgentAvailable(other) && !other.cqcPressPending && other.cqcAttackTime <= 0.f) eligible({0, other.id}, other.position, other.facing, other.elevation);
    return result;
  }

  void Simulation::ReleaseCqc(Agent &agent) {
    CancelReload(agent);
    const bool interrupted = agent.cqcHoldingGuard != 0 || agent.cqcHoldingAgent != 0 || agent.cqcHeldBy != 0 || agent.cqcPressPending || agent.cqcAttackTime > 0.f || agent.cqcApproachGuard != 0 || agent.cqcApproachAgent != 0;
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
        guard.state = GuardState::Search; guard.lastKnown = agent.position; guard.lastKnownElevation = agent.elevation;
      }
    }
    agent.cqcHoldingGuard = 0; agent.cqcHoldingAgent = 0; agent.cqcHoldTime = 0.f; agent.cqcReleaseTime = 0.f;
    agent.cqcPulseTime = 0.f; agent.cqcPulseReady = false; agent.cqcProgress = 0;
    agent.cqcHeldPressTime = 0.f; agent.cqcLongPressArmed = false;
    agent.cqcPressPending = false; agent.cqcStrikePending = false; agent.cqcAttackTime = 0.f;
    agent.cqcApproachGuard = 0; agent.cqcApproachAgent = 0; agent.cqcApproachTime = 0.f; agent.cqcApproachSide = 0;
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
    const bool holding = agent.cqcHoldingGuard != 0 || agent.cqcHoldingAgent != 0;
    const bool pushBlocked = holding || agent.cqcAttackTime > 0.f || agent.cqcPressPending || agent.cqcApproachGuard != 0 || agent.cqcApproachAgent != 0;
    const bool incompatible = (command.push && !pushBlocked) || command.tool || command.interact || command.coverToggle || command.prone || command.dragBody || command.throwDecoy || command.knock || command.reloadWeapon || command.weaponChoice >= 0 || command.dive || command.roll;
    if (!IsTactical(world_) || !agent.connected || !agent.alive || agent.incapacitated || agent.hidden || agent.extracted || agent.draggingGuard >= 0 || agent.traversal.mode != TraversalMode::Grounded) {
      ReleaseCqc(agent); return false;
    }
    if (agent.stance == Stance::Prone) {
      if (agent.cqcHoldingGuard != 0 || agent.cqcHoldingAgent != 0 || agent.cqcHeldBy != 0 || agent.cqcPressPending || agent.cqcAttackTime > 0.f) ReleaseCqc(agent);
      return false;
    }
    const auto context = agent.cqcHoldingGuard != 0 || agent.cqcHoldingAgent != 0 ? CqcInputContext::Held : agent.cover.active && CoverSurfaceValid(agent.cover, agent.elevation) ? CqcInputContext::CoverWall : CqcInputContext::Combat;
    if (context == CqcInputContext::CoverWall) {
      agent.cqcPressPending = false; agent.cqcHoldTime = 0.f; agent.cqcApproachGuard = 0; agent.cqcApproachAgent = 0; agent.cqcApproachTime = 0.f; agent.cqcApproachSide = 0;
      if (newPress && agent.actionCooldown <= 0.f && !incompatible) {
        const auto contact = WorldGeometry::ClosestPoint(agent.position, agent.cover.surface);
        if ((contact - agent.position).length() <= DistractionConfig::KnockDistance) {
          CancelReload(agent);
          EmitNoise(contact, DistractionConfig::KnockHearingDistance, agent.id, 0, SoundKind::Knock);
          EmitSound(SoundKind::Knock, contact, DistractionConfig::KnockHearingDistance, agent.id);
          agent.actionPose = ActionPose::KnockWall; agent.actionPoseTime = GameplayConfig::AttackAnimationDuration;
          agent.aiming = false;
          agent.actionCooldown = GameplayConfig::WeaponCooldown;
          world_.message = "Golpe contextual de cobertura: las patrullas investigarán el punto de contacto.";
        }
      }
      if (command.cqc || command.cqcHeld) { agent.moving = false; agent.running = false; }
      return command.cqc || command.cqcHeld;
    }
    if (context == CqcInputContext::Held) {
      Guard *guard = nullptr; Agent *partner = nullptr;
      if (agent.cqcHoldingGuard != 0) {
        const auto found = std::ranges::find_if(world_.guards, [&](const Guard &entry) { return entry.id == agent.cqcHoldingGuard; });
        if (found != world_.guards.end() && found->heldBy == agent.id && found->state != GuardState::Dead && found->state != GuardState::Incapacitated) guard = &*found;
      } else {
        const auto found = std::ranges::find_if(world_.agents, [&](const Agent &entry) { return entry.id == agent.cqcHoldingAgent; });
        if (found != world_.agents.end() && found->cqcHeldBy == agent.id && found->connected && found->alive && !found->incapacitated && !found->hidden && !found->extracted) partner = &*found;
      }
      const auto targetPosition = guard ? guard->position : (partner ? partner->position : agent.position);
      const float targetElevation = guard ? guard->elevation : partner ? partner->elevation : agent.elevation;
      if ((!guard && !partner) || std::abs(targetElevation - agent.elevation) > LevelConfig::MaximumStepHeight || (targetPosition - agent.position).length() > GameplayConfig::CqcDistance * GameplayConfig::CqcMaximumStretch ||
          !WorldGeometry::ClearBodySegment(world_, WorldGeometry::Feet(agent), {targetPosition.x, targetPosition.y, targetElevation}, GameplayConfig::AgentRadius, WorldGeometry::ActorHeight(agent), agent.supportId) || incompatible) {
        ReleaseCqc(agent); return false;
      }
      agent.moving = false; agent.running = false; agent.cover = {}; agent.decoyGuide = {}; agent.aiming = false;
      if (guard) { guard->facing = agent.facing; guard->attackTime = 0.f; guard->stateTime = 0.f; guard->navigation.clear(); }
      else { partner->facing = agent.facing; partner->moving = false; partner->running = false; partner->cover = {}; partner->actionPose = ActionPose::Grab; partner->actionPoseTime = GameplayConfig::AttackAnimationDuration; }
      agent.cqcPulseTime = std::min(MaximumPressBuffer, agent.cqcPulseTime + dt);
      if (!command.cqcHeld && !command.cqc) {
        agent.cqcReleaseTime = std::min(MinimumRelease, agent.cqcReleaseTime + dt);
        agent.cqcHeldPressTime = 0.f;
        if (agent.cqcReleaseTime >= MinimumRelease) { agent.cqcPulseReady = true; agent.cqcLongPressArmed = true; }
      } else if (!newPress) agent.cqcReleaseTime = 0.f;
      if (command.cqcHeld && agent.cqcLongPressArmed) agent.cqcHeldPressTime = std::min(LongPressEscape, agent.cqcHeldPressTime + dt);
      if (newPress && agent.cqcPulseReady && agent.cqcPulseTime >= PulseInterval && agent.stamina >= GameplayConfig::CqcStaminaCost) {
        agent.cqcPulseReady = false; agent.cqcReleaseTime = 0.f; agent.cqcPulseTime = 0.f;
        agent.stamina -= GameplayConfig::CqcStaminaCost; ++agent.cqcProgress;
        agent.actionPose = ActionPose::Choke; agent.actionPoseTime = GameplayConfig::AttackAnimationDuration;
        EmitSound(SoundKind::Choke, targetPosition, GripSoundRadius, agent.id, targetElevation);
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
      if (agent.cqcHeldPressTime >= LongPressEscape) {
        ReleaseCqc(agent);
        world_.message = "La presión prolongada deshizo la sujeción; vuelve a acercarte y alterna pulsaciones breves de F.";
        return true;
      }
      return true;
    }
    if (agent.cqcAttackTime > 0.f) {
      if (incompatible || command.fireLethal || command.fireNonLethal || command.fireSelected) { ReleaseCqc(agent); return false; }
      const bool kick = !agent.cqcNextKick;
      agent.aiming = false;
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
          if (std::abs(enemy.elevation - agent.elevation) > LevelConfig::MaximumStepHeight || !CqcGuardAvailable(enemy) || (IsTraining(world_) && !enemy.trainingDummy) || distance <= 0.01f || distance >= nearest || CqcUnit(agent.facing).dot(CqcUnit(offset)) < FacingCosine ||
              !WorldGeometry::ClearBodySegment(world_, WorldGeometry::Feet(agent), WorldGeometry::Feet(enemy), GameplayConfig::AgentRadius, WorldGeometry::ActorHeight(agent), agent.supportId)) continue;
          target = &enemy; nearest = distance;
        }
        EmitNoise(agent.position, kick ? KickNoise : PunchNoise, agent.id, 0, kick ? SoundKind::Kick : SoundKind::Punch);
        if (target) {
          EmitSound(kick ? SoundKind::Kick : SoundKind::Punch, target->position, kick ? KickNoise : PunchNoise, agent.id, target->elevation);
          if (DamageTrainingDummy(*target, kick ? KickStun : PunchStun)) return true;
          target->stun = std::min(CombatConfig::MaxGuardStun, target->stun + (kick ? KickStun : PunchStun));
          target->staggerRemaining = kick ? KickStagger : PunchStagger;
          target->attackTime = 0.f; target->navigation.clear(); target->lastKnown = agent.position; target->lastKnownElevation = agent.elevation;
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
      agent.cqcPressPending = false; agent.cqcHoldTime = 0.f; agent.cqcApproachGuard = 0; agent.cqcApproachAgent = 0; agent.cqcApproachTime = 0.f; agent.cqcApproachSide = 0; return false;
    }
    if (newPress) { agent.cqcPressPending = true; agent.cqcHoldTime = 0.f; }
    if (!agent.cqcPressPending) return false;
    if (!command.cqcHeld) {
      const bool kick = agent.cqcNextKick;
      const float staminaCost = kick ? KickStamina : PunchStamina;
      const bool shortPress = agent.cqcHoldTime < AcquireHold;
      agent.cqcPressPending = false; agent.cqcHoldTime = 0.f;
      agent.cqcApproachGuard = 0; agent.cqcApproachAgent = 0; agent.cqcApproachTime = 0.f; agent.cqcApproachSide = 0;
      if (!shortPress || agent.stamina < staminaCost) return false;
      agent.stamina -= staminaCost; agent.cqcNextKick = !kick;
      agent.cqcAttackTime = kick ? KickCooldown : PunchCooldown; agent.cqcStrikePending = true;
      agent.actionPose = kick ? ActionPose::Kick : ActionPose::Punch; agent.actionPoseTime = agent.cqcAttackTime;
      agent.moving = false; agent.running = false; agent.decoyGuide = {};
      agent.aiming = false;
      return true;
    }
    agent.cqcHoldTime = std::min(MaximumPressBuffer, agent.cqcHoldTime + dt);
    if (agent.cqcHoldTime < AcquireHold || agent.stamina < GameplayConfig::CqcStaminaCost) return false;
    Guard *target = nullptr; Agent *partner = nullptr;
    if (agent.cqcApproachGuard != 0) {
      const auto found = std::ranges::find_if(world_.guards, [&](const Guard &entry) { return entry.id == agent.cqcApproachGuard; });
      if (found != world_.guards.end() && CqcGuardAvailable(*found) && (!IsTraining(world_) || found->trainingDummy)) target = &*found;
    } else if (agent.cqcApproachAgent != 0) {
      const auto found = std::ranges::find_if(world_.agents, [&](const Agent &entry) { return entry.id == agent.cqcApproachAgent; });
      if (found != world_.agents.end() && CqcAgentAvailable(*found) && !found->cqcPressPending && found->cqcAttackTime <= 0.f) partner = &*found;
    } else {
      const auto selected = FindCqcTarget(world_, agent, CqcAction::Grab);
      if (selected) {
        for (auto &enemy : world_.guards) if (enemy.id == selected->guard) target = &enemy;
        for (auto &other : world_.agents) if (other.id == selected->agent) partner = &other;
      }
      if (!target && !partner) return false;
      agent.cqcApproachGuard = target ? target->id : 0; agent.cqcApproachAgent = partner ? partner->id : 0;
      agent.cqcApproachTime = 0.f;
      agent.cqcApproachSide = *RearApproachSide(world_, agent, *selected, target ? target->position : partner->position, CqcUnit(target ? target->facing : partner->facing));
    }
    if (!target && !partner) { ReleaseCqc(agent); return false; }
    const auto targetPosition = target ? target->position : partner->position;
    const float targetElevation = target ? target->elevation : partner->elevation;
    if (std::abs(targetElevation - agent.elevation) > LevelConfig::MaximumStepHeight) { ReleaseCqc(agent); return false; }
    const auto targetFacing = CqcUnit(target ? target->facing : partner->facing);
    const CqcTarget selected{target ? target->id : 0, partner ? partner->id : 0};
    const auto offset = targetPosition - agent.position;
    agent.cqcApproachTime = std::min(ApproachTimeout, agent.cqcApproachTime + dt);
    if (offset.length() > AcquireDistance || agent.cqcApproachTime >= ApproachTimeout ||
        !CharacterPathClear(world_, agent, selected, agent.position, targetPosition) || (target && target->staggerRemaining > 0.f)) { ReleaseCqc(agent); return false; }
    const auto contact = targetPosition - targetFacing * GripSeparation;
    if ((agent.position - contact).length() > ContactTolerance) {
      const auto radial = CqcUnit(agent.position - targetPosition);
      const auto rear = -targetFacing;
      float angle = std::remainder(std::atan2(rear.y, rear.x) - std::atan2(radial.y, radial.x), 2.f * std::numbers::pi_v<float>);
      if (agent.cqcApproachSide == 0) agent.cqcApproachSide = angle < 0.f ? -1 : 1;
      if (agent.cqcApproachSide > 0 && angle < 0.f) angle += 2.f * std::numbers::pi_v<float>;
      if (agent.cqcApproachSide < 0 && angle > 0.f) angle -= 2.f * std::numbers::pi_v<float>;
      const float radialDistance = std::max(GripSeparation, offset.length());
      const float turn = std::clamp(angle, -ApproachSpeed * dt / radialDistance, ApproachSpeed * dt / radialDistance);
      const float nextAngle = std::atan2(radial.y, radial.x) + turn;
      const float nextRadius = targetFacing.dot(CqcUnit(offset)) >= RearCosine ? std::max(GripSeparation, radialDistance - ApproachSpeed * dt) : radialDistance;
      const auto destination = targetPosition + sf::Vector2f{std::cos(nextAngle), std::sin(nextAngle)} * nextRadius;
      auto movement = destination - agent.position;
      if (movement.length() > ApproachSpeed * dt) movement = CqcUnit(movement) * ApproachSpeed * dt;
      const auto previous = agent.position;
      auto probe = agent;
      if (!CharacterPathClear(world_, agent, selected, previous, previous + movement)) { ReleaseCqc(agent); return false; }
      MoveAgent(probe, movement);
      const auto reached = probe.position;
      const auto displacement = reached - previous;
      const float along = displacement.lengthSquared() > 0.f ? std::clamp((targetPosition - previous).dot(displacement) / displacement.lengthSquared(), 0.f, 1.f) : 0.f;
      if ((previous + displacement * along - targetPosition).length() < MinimumGripDistance) { ReleaseCqc(agent); return false; }
      agent.position = reached; agent.elevation = probe.elevation; agent.supportId = probe.supportId;
      if ((agent.position - previous).length() < ApproachBlockedTolerance) { ReleaseCqc(agent); return false; }
      agent.facing = CqcUnit(targetPosition - agent.position); agent.running = false; agent.moving = true; agent.decoyGuide = {}; agent.aiming = false;
      if ((agent.position - contact).length() > ContactTolerance) return true;
    }
    if ((agent.position - contact).length() > ContactTolerance || !CharacterPathClear(world_, agent, selected, agent.position, targetPosition)) { ReleaseCqc(agent); return false; }
    agent.cqcHoldingGuard = target ? target->id : 0; agent.cqcHoldingAgent = partner ? partner->id : 0;
    agent.cqcProgress = 0; agent.cqcReleaseTime = 0.f; agent.cqcPulseTime = 0.f; agent.cqcPulseReady = false;
    agent.cqcHeldPressTime = 0.f; agent.cqcLongPressArmed = false;
    agent.cqcHoldTime = 0.f; agent.cqcPressPending = false;
    agent.cqcApproachGuard = 0; agent.cqcApproachAgent = 0; agent.cqcApproachTime = 0.f; agent.cqcApproachSide = 0;
    agent.stamina = std::max(0.f, agent.stamina - GameplayConfig::CqcStaminaCost);
    agent.facing = CqcUnit(targetPosition - agent.position); agent.cover = {}; agent.decoyGuide = {}; agent.moving = false; agent.running = false; agent.aiming = false;
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
      partner->aiming = false;
      partner->actionPose = ActionPose::Grab; partner->actionPoseTime = GameplayConfig::AttackAnimationDuration;
    }
    EmitSound(SoundKind::Grab, targetPosition, GripSoundRadius, agent.id, targetElevation);
    world_.message = target ? "Guardia agarrado por detrás. Suelta brevemente y pulsa F tres veces para someter." : "Compañero sujetado por detrás; suelta F para liberarlo sin daño.";
    return true;
  }

  void Simulation::Push(Agent &agent) {
    using namespace GameplayConfig;
    const auto selected = FindCqcTarget(world_, agent, CqcAction::Push);
    if (!selected) { world_.message = "Acércate a un personaje disponible para empujarlo con V."; return; }
    Guard *enemy = nullptr; Agent *partner = nullptr;
    for (auto &guard : world_.guards) if (guard.id == selected->guard) enemy = &guard;
    for (auto &other : world_.agents) if (other.id == selected->agent) partner = &other;
    auto &position = enemy ? enemy->position : partner->position;
    const auto previous = position;
    const auto destination = PushDestination(world_, agent, *selected, position);
    if (!destination) return;
    if (enemy) WorldGeometry::MoveOnSupports(world_, enemy->position, enemy->elevation, enemy->supportId, *destination - position, AgentRadius, CombatConfig::StandingHeight);
    else MoveAgent(*partner, *destination - position);
    if ((position - previous).lengthSquared() < 0.0001f) { world_.message = "El obstáculo impide desplazar al objetivo."; return; }
    agent.stamina -= CqcStaminaCost; agent.actionCooldown = CqcConfig::PushCooldown;
    agent.actionPose = ActionPose::Push; agent.actionPoseTime = AttackAnimationDuration;
    if (enemy) {
      enemy->staggerRemaining = CqcConfig::PushStagger; enemy->state = GuardState::Search; enemy->stateTime = 0.f;
      enemy->lastKnown = agent.position; enemy->lastKnownElevation = agent.elevation; enemy->attackTime = 0.f; enemy->navigation.clear();
    } else {
      CancelPuzzle(*partner); CancelReload(*partner); partner->aiming = false;
      partner->cover = {}; partner->staggerRemaining = CqcConfig::PushStagger;
      partner->moving = false; partner->running = false;
    }
    if (!IsTraining(world_)) Record(EventKind::Cqc, agent.id, enemy ? enemy->id : partner->id);
    if (enemy) DamageTrainingDummy(*enemy, 0.f);
    EmitNoise(position, CqcConfig::PushNoise, agent.id, 0, SoundKind::Impact, {}, enemy ? enemy->elevation : partner->elevation);
    EmitSound(SoundKind::Push, position, CqcConfig::PushNoise, agent.id, enemy ? enemy->elevation : partner->elevation);
    world_.message = "Empujón: desplazamiento bloqueado por la geometría sólida.";
  }
}
