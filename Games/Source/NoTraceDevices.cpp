#include "NoTraceSimulation.h"
#include "Config/NoTraceDevicesConfig.h"
#include "NoTraceVisibility.h"
#include "NoTraceWorldGeometry.h"
#include <algorithm>
#include <cmath>

namespace NoTrace {
  namespace {
    void PrepareChallenge(PuzzleSession &session, std::uint32_t seed) {
      const std::uint32_t value = seed * 1664525u + session.target * 1013904223u + session.stage * 2246822519u;
      session.clueA = 1u + (value >> 5u) % 8u;
      session.clueB = 1u + (value >> 13u) % 8u;
    }
    bool Operational(const Agent &agent) { return agent.connected && agent.alive && !agent.incapacitated && !agent.hidden && !agent.extracted; }
  }

  void Simulation::CancelPuzzle(Agent &agent) {
    for (auto &object : world_.objects) if (object.id == agent.puzzle.target && object.user == agent.id) {
      object.user = 0;
      if (object.puzzle.state == PuzzleState::InUse) object.puzzle.state = PuzzleState::Ready;
    }
    agent.puzzle = {};
  }

  bool Simulation::BeginPuzzle(Agent &agent, Interactable &object) {
    if (!Operational(agent) || !IsTactical(world_) || !object.active || object.puzzle.kind == PuzzleKind::None) return false;
    if (object.puzzle.state == PuzzleState::Solved) { world_.message = object.puzzle.intelligence.empty() ? "Dispositivo resuelto." : object.puzzle.intelligence; return false; }
    if (object.puzzle.cooldown > 0.f) { world_.message = "El dispositivo está bloqueado temporalmente tras el último intento."; return false; }
    if (object.user != 0 && object.user != agent.id) { world_.message = "Otro agente está operando este dispositivo."; return false; }
    if (WorldGeometry::DistanceToBounds(agent.position, WorldGeometry::ObjectFootprint(object)) >= GameplayConfig::InteractionDistance ||
        !Visibility::HasLineOfSight(world_, agent.position, WorldGeometry::ClosestPoint(agent.position, WorldGeometry::ObjectFootprint(object)), Stance::Standing, object.id)) return false;
    CancelPuzzle(agent);
    ReleaseCqc(agent);
    agent.draggingGuard = -1; agent.cover = {}; agent.moving = false; agent.running = false;
    agent.puzzle.target = object.id; agent.puzzle.kind = object.puzzle.kind;
    agent.puzzle.length = std::clamp(object.puzzle.difficulty, DevicesConfig::PuzzleMinimumStages, DevicesConfig::PuzzleMaximumStages);
    PrepareChallenge(agent.puzzle, world_.seed);
    object.user = agent.id; object.puzzle.state = PuzzleState::InUse;
    Record(EventKind::PuzzleStarted, agent.id, object.id);
    EmitSound(SoundKind::Lock, object.position, GameplayConfig::InteractionDistance * 2.f, agent.id);
    world_.message = "Circuito CIO: suma las dos señales, calcula el resto entre cuatro y pulsa resto + 1. ESC cancela.";
    return true;
  }

  void Simulation::CompletePuzzle(Agent &agent, Interactable &object) {
    const auto kind = object.puzzle.kind;
    object.user = 0; object.puzzle.state = PuzzleState::Solved; object.completed = true;
    Record(EventKind::PuzzleSolved, agent.id, object.id);
    EmitSound(SoundKind::PuzzleSolved, object.position, GameplayConfig::InteractionDistance * 2.f, agent.id);
    switch (kind) {
    case PuzzleKind::Explosive:
      object.trap.state = TrapState::Disarmed; object.trap.remaining = -1.f; object.active = false;
      world_.message = "Explosivo desactivado: el temporizador y el detonador están aislados.";
      break;
    case PuzzleKind::Door:
      object.locked = false; object.open = true;
      EmitSound(SoundKind::Lock, object.position, GameplayConfig::InteractionDistance * 2.f, agent.id);
      world_.message = "Cerradura descifrada. Puerta desbloqueada y abierta.";
      break;
    case PuzzleKind::Computer:
      if (!IsTraining(world_)) { ++world_.secondary; Record(EventKind::Secondary, agent.id, object.id); }
      world_.message = object.puzzle.intelligence;
      break;
    case PuzzleKind::CameraPanel:
      for (auto &camera : world_.objects) if (camera.kind == ObjectKind::Camera && camera.camera.controlGroup == object.camera.controlGroup && camera.camera.state != CameraState::Broken) {
        camera.active = false; camera.camera.state = CameraState::Disabled;
        camera.camera.reactivationRemaining = DevicesConfig::CameraPanelDisabledDuration;
        camera.progress = 0.f; camera.observedAgent = 0;
        Record(EventKind::CameraDisabled, agent.id, camera.id);
      }
      object.puzzle.cooldown = DevicesConfig::CameraPanelDisabledDuration;
      world_.message = "Grupo de cámaras desactivado durante 45 segundos. Después recuperará la vigilancia.";
      break;
    default: break;
    }
    agent.puzzle = {};
  }

  bool Simulation::UpdatePuzzle(Agent &agent, const ActionCommand &command) {
    if (agent.puzzle.target == 0) return command.puzzleInput != -1;
    auto found = std::ranges::find_if(world_.objects, [&agent](const Interactable &object) { return object.id == agent.puzzle.target; });
    if (found == world_.objects.end() || found->user != agent.id || !Operational(agent) || !found->active ||
        found->puzzle.state != PuzzleState::InUse || !IsTactical(world_) ||
        WorldGeometry::DistanceToBounds(agent.position, WorldGeometry::ObjectFootprint(*found)) >= GameplayConfig::InteractionDistance ||
        !Visibility::HasLineOfSight(world_, agent.position, WorldGeometry::ClosestPoint(agent.position, WorldGeometry::ObjectFootprint(*found)), Stance::Standing, found->id)) {
      CancelPuzzle(agent); return true;
    }
    agent.moving = false; agent.running = false;
    if (command.puzzleInput == -1) return true;
    if (command.puzzleTarget != agent.puzzle.target) return true;
    auto &object = *found;
    if (command.puzzleInput == 0) { CancelPuzzle(agent); world_.message = "Interacción cancelada; el dispositivo conserva su estado."; return true; }
    if (command.puzzleInput < 1 || command.puzzleInput > static_cast<int>(DevicesConfig::PuzzleChoiceCount)) return true;
    const int expected = static_cast<int>((agent.puzzle.clueA + agent.puzzle.clueB) % DevicesConfig::PuzzleChoiceCount + 1u);
    if (command.puzzleInput != expected) {
      Record(EventKind::PuzzleFailed, agent.id, object.id);
      EmitSound(SoundKind::PuzzleFailed, object.position, GameplayConfig::InteractionDistance * 2.f, agent.id);
      CancelPuzzle(agent); object.puzzle.state = PuzzleState::Failed; object.puzzle.cooldown = DevicesConfig::PuzzleFailureCooldown;
      if (object.puzzle.kind == PuzzleKind::Explosive) Detonate(object);
      else { EmitNoise(object.position, GameplayConfig::NonLethalNoiseRadius, agent.id); world_.message = "Secuencia incorrecta. El dispositivo sigue protegido y se bloquea tres segundos."; }
      return true;
    }
    Record(EventKind::PuzzleStep, agent.id, object.id);
    EmitSound(SoundKind::PuzzleStep, object.position, GameplayConfig::InteractionDistance * 2.f, agent.id);
    if (++agent.puzzle.stage >= agent.puzzle.length) CompletePuzzle(agent, object);
    else PrepareChallenge(agent.puzzle, world_.seed);
    return true;
  }

  void Simulation::Detonate(Interactable &object) {
    if (!IsTactical(world_) || object.trap.state == TrapState::Disarmed || object.trap.state == TrapState::Detonated) return;
    for (auto &agent : world_.agents) if (agent.puzzle.target == object.id) CancelPuzzle(agent);
    object.active = false; object.completed = true; object.user = 0;
    object.trap.state = TrapState::Detonated; object.trap.remaining = 0.f; object.trap.effectRemaining = DevicesConfig::ExplosionEffectDuration;
    Record(EventKind::Explosion, 0, object.id);
    EmitSound(SoundKind::Explosion, object.position, DevicesConfig::ExplosionNoiseRadius);
    EmitNoise(object.position, DevicesConfig::ExplosionNoiseRadius, 0);
    const auto exposed = [this, &object](sf::Vector2f target) {
      if ((target - object.position).length() > object.trap.blastRadius) return false;
      bool blocked = false;
      for (const auto &obstacle : world_.obstacles) if (WorldGeometry::BlocksMovement(obstacle)) blocked |= WorldGeometry::SegmentRect(object.position, target, obstacle.bounds);
      for (const auto &other : world_.objects) if (other.id != object.id && WorldGeometry::BlocksMovement(other)) blocked |= WorldGeometry::SegmentRect(object.position, target, WorldGeometry::ObjectFootprint(other));
      return !blocked;
    };
    const auto damage = [&object](sf::Vector2f target) {
      return object.trap.damage * std::clamp(1.f - (target - object.position).length() / object.trap.blastRadius * 0.65f, 0.35f, 1.f);
    };
    for (auto &agent : world_.agents) if (agent.connected && agent.alive && !agent.extracted && exposed(agent.position)) DamageAgent(agent, damage(agent.position), damage(agent.position), 0, object.id);
    for (auto &guard : world_.guards) if (guard.state != GuardState::Dead && exposed(guard.position)) {
      for (auto &agent : world_.agents) if (agent.cqcHoldingGuard == guard.id) ReleaseCqc(agent);
      if (DamageTrainingDummy(guard, damage(guard.position))) continue;
      guard.health = std::max(0.f, guard.health - damage(guard.position));
      if (guard.health <= 0.f) {
        guard.state = GuardState::Dead; guard.heldBy = 0; guard.cqcState = CqcState::None; guard.bodyHidden = false; guard.storedIn = 0; Record(EventKind::GuardKilled, 0, guard.id);
      }
      else if (guard.state != GuardState::Incapacitated) { guard.state = GuardState::Search; guard.lastKnown = object.position; guard.navigation.clear(); }
    }
    if (!IsTraining(world_)) {
      if (!world_.alarm) { world_.alarm = true; ++world_.alarmCount; Record(EventKind::AgentAlarm, 0, object.id); }
      RegisterSecurityContact(object.position);
      world_.message = "Explosión: daño real, alarma y respuesta de seguridad activados.";
    } else world_.message = "Explosión de prácticas: impacto y efectos reales, con protección para los agentes y los muñecos.";
  }

  void Simulation::UpdateDevices(float dt) {
    using namespace DevicesConfig;
    if (!IsTactical(world_)) return;
    for (auto &agent : world_.agents) if (agent.puzzle.target != 0) UpdatePuzzle(agent, {});
    for (auto &object : world_.objects) {
      if (object.puzzle.cooldown > 0.f) {
        object.puzzle.cooldown = std::max(0.f, object.puzzle.cooldown - dt);
        if (object.puzzle.cooldown <= 0.f && (object.puzzle.state == PuzzleState::Failed || object.puzzle.kind == PuzzleKind::CameraPanel)) {
          object.puzzle.state = PuzzleState::Ready;
          if (object.puzzle.kind == PuzzleKind::CameraPanel) object.completed = false;
        }
      }
      if (object.kind == ObjectKind::Camera) {
        auto &camera = object.camera;
        if (camera.state == CameraState::Disabled && camera.reactivationRemaining > 0.f) {
          camera.reactivationRemaining = std::max(0.f, camera.reactivationRemaining - dt);
          if (camera.reactivationRemaining <= 0.f) { camera.state = CameraState::Operating; object.active = true; EmitSound(SoundKind::Camera, object.position, CameraRange); }
        }
        if (!object.active || camera.state != CameraState::Operating) { object.progress = 0.f; object.observedAgent = 0; continue; }
        if (camera.sweeping) {
          if (camera.pauseRemaining > 0.f) camera.pauseRemaining = std::max(0.f, camera.pauseRemaining - dt);
          else {
            camera.sweepAngle += camera.speed * static_cast<float>(camera.direction) * dt;
            if (camera.sweepAngle >= camera.maxAngle || camera.sweepAngle <= camera.minAngle) {
              camera.sweepAngle = std::clamp(camera.sweepAngle, camera.minAngle, camera.maxAngle);
              camera.direction = -camera.direction; camera.pauseRemaining = CameraEndpointPause;
              EmitSound(SoundKind::Camera, object.position, camera.range);
            }
          }
          const float angle = camera.baseAngle + camera.sweepAngle;
          object.facing = {std::cos(angle), std::sin(angle)};
        }
        Agent *seen = nullptr;
        for (auto &agent : world_.agents) {
          if (!Operational(agent)) continue;
          const auto delta = agent.position - object.position;
          const float distance = delta.length();
          const float posture = agent.stance == Stance::Prone ? GameplayConfig::ProneVisionFactor : agent.stance == Stance::Crouched ? GameplayConfig::CrouchedVisionFactor : 1.f;
          if (distance > camera.range * posture || (distance > GameplayConfig::CloseVisionDistance && object.facing.dot(delta) < camera.cosHalfAngle * distance * object.facing.length())) continue;
          if (!Visibility::HasLineOfSight(world_, object.position, agent.position, agent.stance, object.id)) continue;
          if (!seen || distance < (seen->position - object.position).length()) seen = &agent;
        }
        if (seen) {
          if (object.observedAgent != 0 && object.observedAgent != seen->id) object.progress = 0.f;
          object.observedAgent = seen->id;
          const bool confirmed = object.progress >= camera.confirmationTime;
          object.progress = std::min(camera.confirmationTime, object.progress + dt);
          seen->detectionProgress = std::max(seen->detectionProgress, object.progress / camera.confirmationTime);
          if (object.progress >= camera.confirmationTime) {
            if (!IsTraining(world_)) {
              if (!confirmed) Detect(*seen, object.id + 1000);
              else RegisterSecurityContact(seen->position);
            } else if (!confirmed) { EmitSound(SoundKind::Camera, object.position, camera.range); world_.message = "Cámara de prácticas: detección confirmada; sin alarma operativa ni refuerzos."; }
            seen->detectionConfirmed = true;
          }
        } else { object.progress = std::max(0.f, object.progress - dt); if (object.progress <= 0.f) object.observedAgent = 0; }
      } else if (object.kind == ObjectKind::Explosive) {
        auto &trap = object.trap;
        trap.effectRemaining = std::max(0.f, trap.effectRemaining - dt);
        if (!object.active || trap.state == TrapState::Disarmed || trap.state == TrapState::Detonated) continue;
        bool audible = false;
        for (const auto &agent : world_.agents) if (Operational(agent)) {
          const float distance = (agent.position - object.position).length();
          audible |= distance <= trap.audibleRadius;
          if (distance > trap.audibleRadius || !Visibility::HasLineOfSight(world_, object.position, agent.position, Stance::Standing, object.id)) continue;
          if (trap.state == TrapState::Armed) trap.state = TrapState::Detected;
          if (distance <= trap.triggerRadius && trap.remaining < 0.f) { trap.remaining = trap.fuse; EmitNoise(object.position, trap.audibleRadius, 0); world_.message = "Temporizador activado: aléjate o resuelve el circuito del explosivo."; }
        }
        trap.tickRemaining = std::max(0.f, trap.tickRemaining - dt);
        if (audible && trap.tickRemaining <= 0.f) {
          EmitSound(SoundKind::TrapTick, object.position, trap.audibleRadius);
          Record(EventKind::TrapTick, 0, object.id);
          trap.tickRemaining = trap.remaining < 0.f ? TrapMaximumTickInterval : std::clamp(trap.remaining / trap.fuse * TrapMaximumTickInterval, TrapMinimumTickInterval, TrapMaximumTickInterval);
        }
        if (trap.remaining >= 0.f) { trap.remaining = std::max(0.f, trap.remaining - dt); if (trap.remaining <= 0.f) Detonate(object); }
      }
    }
  }
}
