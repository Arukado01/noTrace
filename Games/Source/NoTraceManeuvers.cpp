#include "NoTraceSimulation.h"
#include "Config/NoTraceManeuverConfig.h"
#include "NoTraceWorldGeometry.h"
#include <algorithm>
#include <cmath>

namespace NoTrace {
  void Simulation::CancelManeuver(Agent &agent) {
    if (agent.maneuver.kind == ManeuverKind::None) return;
    const bool dive = agent.maneuver.kind == ManeuverKind::Dive;
    if (dive && agent.maneuver.phase != ManeuverPhase::Start) agent.stance = Stance::Prone;
    const float cooldown = std::max(agent.maneuver.cooldown, dive ? ManeuverConfig::DiveCooldown : ManeuverConfig::RollCooldown);
    agent.maneuver = {}; agent.maneuver.cooldown = cooldown;
    agent.running = false; agent.moving = false;
  }

  bool Simulation::UpdateManeuver(Agent &agent, const ActionCommand &command, float dt) {
    using namespace ManeuverConfig;
    auto &motion = agent.maneuver;
    motion.cooldown = std::max(0.f, motion.cooldown - dt);
    const bool unavailable = !IsTactical(world_) || !agent.connected || !agent.alive || agent.incapacitated || agent.hidden || agent.extracted || agent.hideoutId != 0 || agent.staggerRemaining > 0.f || agent.traversal.mode != TraversalMode::Grounded;
    if (unavailable) { CancelManeuver(agent); return false; }
    if (motion.kind == ManeuverKind::None) {
      if ((!command.dive && !command.roll) || motion.cooldown > 0.f || agent.cover.active || agent.puzzle.target != 0 || agent.draggingGuard >= 0 || agent.reloadRemaining > 0.f ||
          agent.cqcHoldingGuard != 0 || agent.cqcHoldingAgent != 0 || agent.cqcHeldBy != 0 || agent.cqcApproachGuard != 0 || agent.cqcApproachAgent != 0 || agent.cqcAttackTime > 0.f || agent.cqcPressPending || agent.actionCooldown > 0.f ||
          command.cqc || command.cqcHeld || command.interact || command.tool || command.reloadWeapon || command.fireSelected || command.fireLethal || command.fireNonLethal || command.push || command.throwDecoy) return false;
      const bool dive = command.dive && agent.stance == Stance::Standing && agent.running && command.run;
      const bool roll = !command.dive && command.roll && agent.stance == Stance::Crouched;
      if (!dive && !roll) return false;
      sf::Vector2f direction{command.moveX, command.moveY};
      if (!std::isfinite(direction.x) || !std::isfinite(direction.y)) return false;
      if (direction.lengthSquared() < 0.0001f) direction = agent.facing;
      if (direction.lengthSquared() < 0.0001f || (dive && !agent.moving)) return false;
      direction /= direction.length();
      const float cost = dive ? DiveStamina : RollStamina;
      if (agent.stamina < cost) return false;
      auto probe = agent;
      MoveAgent(probe, direction * StartProbe);
      if ((probe.position - agent.position).length() < CollisionTolerance) return false;
      CancelReload(agent);
      motion = {}; motion.kind = dive ? ManeuverKind::Dive : ManeuverKind::Roll; motion.phase = ManeuverPhase::Start; motion.direction = direction;
      agent.stamina -= cost; agent.facing = direction; agent.running = false; agent.moving = false; agent.cover = {}; agent.decoyGuide = {};
      agent.aiming = false;
      agent.actionPose = ActionPose::None; agent.actionPoseTime = 0.f;
      EmitNoise(agent.position, dive ? DiveNoise : RollNoise, agent.id, 0, SoundKind::StepSoft);
      EmitSound(dive ? SoundKind::Dive : SoundKind::Roll, agent.position, dive ? DiveNoise : RollNoise, agent.id);
      world_.message = dive ? "Zambullida táctica: impulso y aterrizaje tumbado." : "Rodada táctica: desplazamiento y recuperación agachado.";
      return true;
    }
    const bool dive = motion.kind == ManeuverKind::Dive;
    const float start = dive ? DiveStart : RollStart;
    const float travelEnd = start + (dive ? DiveTravel : RollTravel);
    const float landEnd = travelEnd + (dive ? DiveLand : 0.f);
    const float previousTime = motion.time;
    motion.time = std::min(Duration(motion.kind), motion.time + dt);
    agent.running = false; agent.facing = motion.direction; agent.moving = false; agent.decoyGuide = {}; agent.aiming = false;
    const float travelTime = std::max(0.f, std::min(motion.time, travelEnd) - std::max(previousTime, start));
    if (travelTime > 0.f && !motion.blocked) {
      if (dive) agent.stance = Stance::Prone;
      const auto previous = agent.position;
      const float requested = (dive ? DiveSpeed : RollSpeed) * travelTime;
      MoveAgent(agent, motion.direction * requested);
      const float travelled = (agent.position - previous).length();
      motion.travelled += travelled; agent.moving = travelled > CollisionTolerance;
      if (travelled + CollisionTolerance < requested) { motion.blocked = true; motion.time = std::max(motion.time, travelEnd); }
    }
    if (previousTime < travelEnd && motion.time >= travelEnd) {
      agent.stance = dive ? Stance::Prone : Stance::Crouched;
      EmitNoise(agent.position, dive ? LandingNoise : RollNoise, agent.id, 0, SoundKind::Impact);
      if (motion.travelled > CollisionTolerance) EmitSound(SoundKind::Land, agent.position, dive ? LandingNoise : RollNoise, agent.id);
    }
    motion.phase = motion.time < start ? ManeuverPhase::Start : motion.time < travelEnd ? ManeuverPhase::Travel : motion.time < landEnd ? ManeuverPhase::Land : ManeuverPhase::Recover;
    if (motion.time >= Duration(motion.kind)) {
      const float cooldown = dive ? DiveCooldown : RollCooldown;
      agent.stance = dive ? Stance::Prone : Stance::Crouched; agent.moving = false;
      motion = {}; motion.cooldown = cooldown;
    }
    return true;
  }
}
