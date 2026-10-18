#include "NoTraceSimulation.h"
#include "Config/NoTraceWeaponConfig.h"
#include "Config/NoTraceTrainingConfig.h"
#include "NoTraceWorldGeometry.h"
#include <algorithm>
#include <cmath>

namespace NoTrace {
  void Simulation::BuildTraining() {
    world_.mode = SimulationMode::Training;
    world_.size = TrainingConfig::WorldSize;
    for (unsigned int index = 0; index < TrainingConfig::DummyCount; ++index) {
      Guard dummy;
      dummy.id = TrainingConfig::FirstDummyId + index; dummy.position = {1210.f + 90.f * index, 210.f};
      dummy.facing = {1.f, 0.f}; dummy.trainingDummy = true; dummy.health = TrainingConfig::DummyMaximumHealth;
      world_.guards.push_back(dummy);
    }
    Interactable camera{100, ObjectKind::Camera, {1690.f, 260.f}, "Cámara de prácticas"};
    camera.camera.sweeping = true; camera.camera.baseAngle = 3.14159265f; camera.camera.controlGroup = TrainingConfig::CameraControlGroup; camera.facing = {-1.f, 0.f};
    world_.objects.push_back(camera);
    Interactable panel{101, ObjectKind::CameraPanel, {1640.f, 430.f}, "Circuito de cámaras"};
    panel.footprint = {28.f, 24.f}; panel.puzzle.kind = PuzzleKind::CameraPanel; panel.puzzle.difficulty = 2;
    panel.camera.controlGroup = TrainingConfig::CameraControlGroup; world_.objects.push_back(panel);
    Interactable explosive{102, ObjectKind::Explosive, {1230.f, 650.f}, "Explosivo de entrenamiento"};
    explosive.footprint = {16.f, 12.f}; explosive.solid = SpatialOverride::Disabled; explosive.blocksVision = SpatialOverride::Disabled;
    explosive.puzzle.kind = PuzzleKind::Explosive; explosive.puzzle.difficulty = 2; explosive.trap.fuse = TrainingConfig::TrainingTrapFuse;
    world_.objects.push_back(explosive);
    Interactable door{103, ObjectKind::Door, {1500.f, 600.f}, "Cerradura de prácticas"};
    door.locked = true; door.puzzle.kind = PuzzleKind::Door; door.puzzle.difficulty = 2; world_.objects.push_back(door);
    Interactable computer{104, ObjectKind::Computer, {1680.f, 650.f}, "Computador de prácticas"};
    computer.footprint = {38.f, 28.f}; computer.puzzle.kind = PuzzleKind::Computer; computer.puzzle.difficulty = 2;
    computer.puzzle.intelligence = "PRÁCTICA COMPLETADA: los computadores revelan información tras descifrar el circuito. Rearma los ejercicios en la consola de entrenamiento.";
    world_.objects.push_back(computer);
    Interactable closet{105, ObjectKind::Hideout, {1710.f, 850.f}, "Armario de prácticas"};
    closet.hideoutKind = HideoutKind::Closet; world_.objects.push_back(closet);
    Interactable trash{106, ObjectKind::Hideout, {1250.f, 850.f}, "Contenedor de prácticas"};
    trash.hideoutKind = HideoutKind::TrashBin; world_.objects.push_back(trash);
    Interactable table{107, ObjectKind::Hideout, {1480.f, 850.f}, "Mesa de prácticas"};
    table.hideoutKind = HideoutKind::UnderTable; world_.objects.push_back(table);
    Interactable reset{108, ObjectKind::TrainingReset, TrainingConfig::ResetPosition, "REARMAR PRUEBAS"};
    reset.footprint = {32.f, 24.f}; world_.objects.push_back(reset);
    world_.obstacles.push_back({{{1150.f, 280.f}, {360.f, 22.f}}, CoverKind::High, true, "Parapeto del campo de tiro"});
    world_.obstacles.push_back({{{1488.f, 520.f}, {24.f, 48.f}}, CoverKind::High, true, "Marco de prácticas"});
    world_.obstacles.push_back({{{1488.f, 632.f}, {24.f, 68.f}}, CoverKind::High, true, "Marco de prácticas"});
    world_.obstacles.push_back({{{1170.f, 420.f}, {120.f, 28.f}}, CoverKind::Low, true, "Cobertura de prácticas"});
    BuildPuzzleNodes();
    world_.message = "CIO: estaciones de preparación al oeste; campo de prácticas al este. Sin enemigos, con protección y suministros ilimitados.";
  }

  bool Simulation::DamageTrainingDummy(Guard &guard, float damage) {
    if (!IsTraining(world_) || !guard.trainingDummy) return false;
    const float amount = std::isfinite(damage) ? std::max(0.f, damage) : 0.f;
    guard.health = std::clamp(guard.health - amount, TrainingConfig::DummyMinimumHealth, TrainingConfig::DummyMaximumHealth);
    guard.stun = 0.f; guard.incapacityRemaining = 0.f; guard.bodyHidden = false; guard.storedIn = 0;
    guard.state = GuardState::Patrol; guard.trainingFeedback = TrainingConfig::DummyFeedbackDuration;
    if (amount > 0.f) {
      guard.trainingHits = std::min(TrainingConfig::MaximumRecordedHits, guard.trainingHits + 1u);
      guard.staggerRemaining = TrainingConfig::DummyReactionDuration;
    }
    guard.attackTime = 0.f; guard.navigation.clear();
    return true;
  }

  void Simulation::UpdateTraining(float dt) {
    if (!IsTraining(world_)) return;
    std::erase_if(world_.guards, [](const Guard &guard) { return !guard.trainingDummy; });
    world_.securityEntries.clear(); world_.securityActive = false; world_.reinforcementWaves = 0;
    world_.alarm = false; world_.alarmCount = 0;
    securityHasClue_ = false; securityContactAge_ = 0.f; reinforcementTimer_ = 0.f; alarmRemaining_ = 0.f;
    if (world_.events.size() > TrainingConfig::MaximumRetainedEvents)
      world_.events.erase(world_.events.begin(), world_.events.end() - TrainingConfig::MaximumRetainedEvents);
    for (auto &dummy : world_.guards) {
      dummy.trainingFeedback = std::max(0.f, dummy.trainingFeedback - dt);
      dummy.staggerRemaining = std::max(0.f, dummy.staggerRemaining - dt);
      dummy.health = std::clamp(dummy.health, TrainingConfig::DummyMinimumHealth, TrainingConfig::DummyMaximumHealth);
      dummy.state = GuardState::Patrol; dummy.stun = 0.f; dummy.incapacityRemaining = 0.f;
      dummy.bodyHidden = false; dummy.storedIn = 0; dummy.navigation.clear(); dummy.patrol.clear(); dummy.suspicion = 0.f;
    }
    for (auto &agent : world_.agents) if (agent.connected) {
      agent.lifeState = LifeState::Alive; agent.alive = true; agent.incapacitated = false; agent.rescueExpired = false; agent.incapacityRemaining = 0.f;
      agent.health = std::clamp(agent.health + TrainingConfig::HealthRecoveryPerSecond * dt, WeaponConfig::TrainingMinimumHealth, GameplayConfig::MaxHealth);
      if (TrainingConfig::UnlimitedAmmo) { agent.loadout.lethalAmmo = TrainingConfig::RefilledAmmo; agent.loadout.nonLethalAmmo = TrainingConfig::RefilledAmmo; }
      agent.loadout.decoys = TrainingConfig::RefilledDecoys; agent.loadout.tools = TrainingConfig::RefilledTools;
    }
  }

  void Simulation::ResetTrainingDevice(Agent &agent) {
    if (!IsTraining(world_) || !agent.connected || !agent.alive || agent.incapacitated || agent.hidden || agent.extracted) return;
    const auto console = std::ranges::find_if(world_.objects, [](const Interactable &object) { return object.kind == ObjectKind::TrainingReset; });
    if (console == world_.objects.end() || WorldGeometry::DistanceToBounds(agent.position, WorldGeometry::ObjectFootprint(*console)) >= GameplayConfig::InteractionDistance) return;
    for (auto &user : world_.agents) { CancelPuzzle(user); ReleaseCqc(user); user.pistolSuppressor = user.nonLethalSuppressor = 100.f; user.pistolSuppressorEquipped = user.nonLethalSuppressorEquipped = true; }
    for (auto &object : world_.objects) {
      if (object.kind == ObjectKind::PuzzleNode) object.open = false;
      if (object.kind == ObjectKind::CoopSwitch) object.progress = 0.f;
      if (object.kind == ObjectKind::Camera) {
        object.active = true; object.completed = false; object.progress = 0.f; object.observedAgent = 0;
        object.camera.trackedAgent = 0; object.camera.lastKnown = {}; object.camera.perception = CameraPerception::Sweeping;
        object.camera.searchRemaining = object.camera.suspicionRemaining = object.camera.confirmedRemaining = object.camera.indicatorProgress = 0.f;
        object.camera.state = CameraState::Operating; object.camera.reactivationRemaining = 0.f; object.camera.pauseRemaining = 0.f;
      }
      if (object.puzzle.kind == PuzzleKind::None) continue;
      object.puzzle.sequenceProgress = 0; object.puzzle.coordinationProgress = 0.f;
      object.puzzle.state = PuzzleState::Ready; object.puzzle.cooldown = 0.f; object.user = 0; object.completed = false; object.active = true;
      if (object.kind == ObjectKind::Explosive) {
        object.trap.state = TrapState::Armed; object.trap.remaining = -1.f; object.trap.tickRemaining = 0.f; object.trap.effectRemaining = 0.f;
      } else if (object.kind == ObjectKind::Door) {
        const bool occupied = std::ranges::any_of(world_.agents, [&object](const Agent &other) { return other.connected && WorldGeometry::CircleOverlaps(other.position, GameplayConfig::AgentRadius, WorldGeometry::ObjectFootprint(object)); }) ||
          std::ranges::any_of(world_.guards, [&object](const Guard &dummy) { return dummy.trainingDummy && WorldGeometry::CircleOverlaps(dummy.position, GameplayConfig::AgentRadius, WorldGeometry::ObjectFootprint(object)); });
        object.locked = !occupied; object.open = occupied;
      }
    }
    for (auto &dummy : world_.guards) if (dummy.trainingDummy) { dummy.health = TrainingConfig::DummyMaximumHealth; dummy.trainingHits = 0; dummy.trainingFeedback = 0.f; }
    EmitSound(SoundKind::PuzzleSolved, console->position, GameplayConfig::InteractionDistance * 2.f, agent.id);
    world_.message = "Campo rearmado: dispositivos, cámaras y muñecos listos para repetir. Las puertas ocupadas permanecen abiertas por seguridad.";
  }
}
