#include "NoTraceSimulation.h"
#include "Config/NoTraceCqcConfig.h"
#include "NoTraceVisibility.h"
#include "NoTraceWorldGeometry.h"
#include <algorithm>
#include <cmath>

namespace NoTrace {
  void Simulation::ReleaseCqc(Agent &agent) {
    for (auto &guard : world_.guards) if (guard.heldBy == agent.id) {
      guard.heldBy = 0; guard.cqcState = CqcState::None; guard.stateTime = 0.f;
      if (guard.state != GuardState::Dead && guard.state != GuardState::Incapacitated) {
        guard.state = GuardState::Search; guard.lastKnown = agent.position;
      }
    }
    agent.cqcHoldingGuard = 0; agent.cqcHoldTime = 0.f; agent.cqcReleaseTime = 0.f;
    agent.cqcPulseTime = 0.f; agent.cqcPulseReady = false; agent.cqcProgress = 0;
  }

  bool Simulation::UpdateCqc(Agent &agent, const ActionCommand &command, float dt) {
    using namespace CqcConfig;
    const auto normalize = [](sf::Vector2f value) { return value.lengthSquared() > 0.0001f ? value / value.length() : sf::Vector2f{1.f, 0.f}; };
    if (world_.phase != GamePhase::Mission || !agent.alive || agent.incapacitated || agent.hidden || agent.extracted || agent.draggingGuard >= 0 || agent.stance == Stance::Prone) {
      ReleaseCqc(agent); return false;
    }
    if (agent.cqcHoldingGuard != 0) {
      const auto found = std::ranges::find_if(world_.guards, [&](const Guard &guard) { return guard.id == agent.cqcHoldingGuard; });
      if (found == world_.guards.end() || found->heldBy != agent.id || found->state == GuardState::Dead || found->state == GuardState::Incapacitated ||
          (found->position - agent.position).length() > GameplayConfig::CqcDistance * GameplayConfig::CqcMaximumStretch ||
          !WorldGeometry::ClearMovementSegment(world_, agent.position, found->position, GameplayConfig::AgentRadius)) {
        ReleaseCqc(agent); return false;
      }
      if (command.push || command.tool || command.interact || command.coverToggle || command.prone) { ReleaseCqc(agent); return false; }
      auto &guard = *found;
      agent.moving = false; agent.running = false; agent.cover = {}; agent.decoyGuide = {};
      guard.facing = agent.facing; guard.attackTime = 0.f; guard.stateTime = 0.f; guard.navigation.clear();
      agent.cqcPulseTime += dt;
      if (!command.cqcHeld && !command.cqc) {
        agent.cqcReleaseTime += dt;
        if (agent.cqcReleaseTime >= MinimumRelease) agent.cqcPulseReady = true;
      } else if (!command.cqc) agent.cqcReleaseTime = 0.f;
      if (command.cqc && agent.cqcPulseReady && agent.cqcPulseTime >= PulseInterval && agent.stamina >= GameplayConfig::CqcStaminaCost) {
        agent.cqcPulseReady = false; agent.cqcReleaseTime = 0.f; agent.cqcPulseTime = 0.f;
        agent.stamina -= GameplayConfig::CqcStaminaCost;
        ++agent.cqcProgress;
        agent.actionPose = ActionPose::Subdue; agent.actionPoseTime = GameplayConfig::AttackAnimationDuration;
        Record(EventKind::Cqc, agent.id, guard.id);
        if (agent.cqcProgress >= RequiredPulses) {
          const auto id = guard.id;
          ReleaseCqc(agent);
          const auto target = std::ranges::find_if(world_.guards, [id](const Guard &value) { return value.id == id; });
          if (target != world_.guards.end()) HitGuard(*target, HitZone::Head, false, agent.id);
          world_.message = "Sometimiento completado: guardia inconsciente.";
          return true;
        }
        guard.cqcState = agent.cqcProgress == 1 ? CqcState::KnockedDown : CqcState::Restrained;
        world_.message = "Sujeción: suelta brevemente y vuelve a pulsar F para someter.";
      } else if (command.cqc) agent.cqcPulseReady = false;
      if (agent.cqcReleaseTime > ReleaseGrace || agent.stamina <= 0.f) { ReleaseCqc(agent); return false; }
      return true;
    }
    if (!command.cqcHeld || agent.actionCooldown > 0.f || agent.cover.active || agent.stamina < GameplayConfig::CqcStaminaCost) {
      agent.cqcHoldTime = 0.f; return false;
    }
    Guard *target = nullptr;
    float nearest = GameplayConfig::CqcDistance;
    for (auto &guard : world_.guards) {
      const auto offset = guard.position - agent.position;
      const float distance = offset.length();
      if (guard.state == GuardState::Dead || guard.state == GuardState::Incapacitated || guard.bodyHidden || guard.heldBy != 0 || guard.cqcState != CqcState::None ||
          distance >= nearest || normalize(agent.facing).dot(normalize(offset)) < FacingCosine ||
          !WorldGeometry::ClearMovementSegment(world_, agent.position, guard.position, GameplayConfig::AgentRadius)) continue;
      target = &guard; nearest = distance;
    }
    if (!target) { agent.cqcHoldTime = 0.f; return false; }
    agent.moving = false; agent.running = false;
    agent.cqcHoldTime += dt;
    if (agent.cqcHoldTime < AcquireHold) return true;
    agent.cqcHoldingGuard = target->id; agent.cqcProgress = 0; agent.cqcReleaseTime = 0.f;
    agent.cqcPulseTime = 0.f; agent.cqcPulseReady = false;
    agent.stamina = std::max(0.f, agent.stamina - GameplayConfig::CqcStaminaCost);
    agent.facing = normalize(target->position - agent.position); agent.cover = {}; agent.decoyGuide = {};
    target->heldBy = agent.id; target->cqcState = CqcState::Grabbed; target->stateTime = 0.f;
    target->attackTime = 0.f; target->facing = agent.facing; target->navigation.clear();
    Record(EventKind::Cqc, agent.id, target->id);
    world_.message = "Guardia agarrado. Mantén F; suelta brevemente y pulsa tres veces para someter.";
    return true;
  }

  void Simulation::Push(Agent &agent) {
    using namespace GameplayConfig;
    if (world_.phase != GamePhase::Mission || agent.hidden || agent.incapacitated || !agent.alive || agent.stance == Stance::Prone || agent.draggingGuard >= 0 ||
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
    for (auto &guard : world_.guards) if (guard.state != GuardState::Dead && guard.state != GuardState::Incapacitated && !guard.bodyHidden && guard.heldBy == 0 && eligible(guard.position)) { enemy = &guard; partner = nullptr; }
    for (auto &other : world_.agents) if (other.id != agent.id && other.alive && other.connected && !other.incapacitated && !other.hidden && !other.extracted && other.cqcHoldingGuard == 0 && eligible(other.position)) { partner = &other; enemy = nullptr; }
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
    Record(EventKind::Cqc, agent.id, enemy ? enemy->id : partner->id);
    EmitNoise(position, CqcConfig::PushNoise, agent.id);
    world_.message = "Empujón: desplazamiento bloqueado por la geometría sólida.";
  }
}
