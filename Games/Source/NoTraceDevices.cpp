#include "NoTraceSimulation.h"
#include "Config/NoTraceDevicesConfig.h"
#include "NoTraceVisibility.h"
#include "NoTracePerception.h"
#include "NoTraceSpatialGeometry.h"
#include "NoTraceCombatGeometry.h"
#include "NoTraceCoordination.h"
#include <algorithm>
#include <bit>
#include <cmath>

namespace NoTrace {
  namespace {
    PuzzleFamily Family(const Interactable &object) {
      if (object.puzzle.family != PuzzleFamily::Automatic) return object.puzzle.family;
      switch (object.puzzle.kind) {
      case PuzzleKind::Explosive: case PuzzleKind::CameraPanel: return PuzzleFamily::Wires;
      case PuzzleKind::Computer: return PuzzleFamily::Levers;
      case PuzzleKind::Coordination: return PuzzleFamily::Coordination;
      default: return PuzzleFamily::Arrows;
      }
    }
    std::vector<std::uint8_t> Order(std::uint32_t seed, std::uint32_t id, unsigned int length, bool permutation) {
      std::vector<std::uint8_t> result(length);
      std::uint32_t value = seed ^ (id * 2246822519u);
      for (unsigned int index = 0; index < length; ++index) {
        value = value * 1664525u + 1013904223u;
        result[index] = static_cast<std::uint8_t>(permutation ? index + 1 : 1u + (value >> 12u) % 4u);
      }
      if (permutation) for (unsigned int index = length; index > 1; --index) {
        value = value * 1664525u + 1013904223u;
        std::swap(result[index - 1], result[value % index]);
      }
      return result;
    }
    void PrepareChallenge(PuzzleSession &session, std::uint32_t seed, const Interactable &object) {
      session.family = Family(object);
      if (session.family == PuzzleFamily::Arrows) session.sequence = Order(seed, object.id, session.length, false);
      else if (session.family == PuzzleFamily::Wires) {
        session.length = DevicesConfig::WireCount;
        const auto labels = object.puzzle.wireCrossingAllowed ? Order(seed, object.id, DevicesConfig::WireCount, true) : std::vector<std::uint8_t>{1, 2, 3, 4};
        std::copy(labels.begin(), labels.end(), session.wireLabels.begin());
      } else if (session.family == PuzzleFamily::Coordination) session.length = 1;
      else {
        session.sequence = object.puzzle.leverOrder.empty() ? Order(seed, object.id, 4, true) : object.puzzle.leverOrder;
        session.length = static_cast<unsigned int>(session.sequence.size()); session.stage = object.puzzle.sequenceProgress;
      }
    }
    bool Operational(const Agent &agent) { return agent.connected && agent.alive && !agent.incapacitated && !agent.hidden && !agent.extracted; }
    bool CanOperate(const WorldState &world, const Agent &agent, const Interactable &object) {
      return Operational(agent) && agent.traversal.mode == TraversalMode::Grounded && std::abs(agent.elevation - object.elevation) <= LevelConfig::MaximumStepHeight && WorldGeometry::DistanceToBounds(agent.position, WorldGeometry::ObjectFootprint(object)) < GameplayConfig::InteractionDistance &&
        !WorldGeometry::RayOccluded(world, {agent.position.x, agent.position.y, agent.elevation + CombatConfig::StandingWeaponHeight}, {object.position.x, object.position.y, object.elevation + CombatConfig::StandingWeaponHeight}, object.id);
    }
    void ClearCamera(Interactable &object) {
      object.progress = 0.f; object.observedAgent = 0; object.camera.trackedAgent = 0; object.camera.lastKnown = {};
      object.camera.perception = CameraPerception::Sweeping; object.camera.searchRemaining = 0.f;
      object.camera.suspicionRemaining = 0.f; object.camera.confirmedRemaining = 0.f; object.camera.indicatorProgress = 0.f;
      object.camera.lostSightTime = 0.f;
    }
    void LookTowards(Interactable &object, sf::Vector2f direction, float maximumTurn) {
      if (direction.lengthSquared() < 0.0001f) return;
      const float current = std::atan2(object.facing.y, object.facing.x);
      const float delta = std::remainder(std::atan2(direction.y, direction.x) - current, 6.2831853f);
      const float angle = current + std::clamp(delta, -maximumTurn, maximumTurn);
      object.facing = {std::cos(angle), std::sin(angle)};
    }
  }

  void Simulation::CancelPuzzle(Agent &agent) {
    const auto target = agent.puzzle.target;
    agent.puzzle = {};
    for (auto &object : world_.objects) if (object.id == target) {
      const auto other = std::ranges::find_if(world_.agents, [target](const Agent &user) { return user.puzzle.target == target; });
      object.user = other == world_.agents.end() ? 0 : other->id;
      if (object.puzzle.state == PuzzleState::InUse && other == world_.agents.end()) object.puzzle.state = PuzzleState::Ready;
      if (Family(object) == PuzzleFamily::Coordination) {
        object.puzzle.coordinationProgress = 0.f;
        for (auto &user : world_.agents) if (user.puzzle.target == target) user.puzzle.coordinationProgress = 0.f;
        Coordination::Refresh(world_, object);
      }
    }
  }

  bool Simulation::BeginPuzzle(Agent &agent, Interactable &object) {
    if (!Operational(agent) || !IsTactical(world_) || !object.active || object.puzzle.kind == PuzzleKind::None) return false;
    if (!CanOperate(world_, agent, object)) return false;
    if (object.puzzle.parentDevice != 0) {
      const auto parent = std::ranges::find_if(world_.objects, [&object](const Interactable &value) { return value.id == object.puzzle.parentDevice; });
      if (parent == world_.objects.end() || !parent->active || Family(*parent) != PuzzleFamily::Levers || parent->puzzle.cooldown > 0.f || parent->puzzle.state == PuzzleState::Solved) return false;
      if (parent->puzzle.leverOrder.empty()) parent->puzzle.leverOrder = Order(world_.seed, parent->id, 4, true);
      const auto progress = parent->puzzle.sequenceProgress;
      if (progress >= parent->puzzle.leverOrder.size() || parent->puzzle.leverOrder[progress] != object.puzzle.nodeIndex) { FailPuzzle(agent, *parent); return false; }
      ++parent->puzzle.sequenceProgress; object.open = true;
      Record(EventKind::PuzzleStep, agent.id, object.id); EmitSound(SoundKind::Lever, object.position, GameplayConfig::InteractionDistance * 2.f, agent.id, object.elevation);
      for (auto &user : world_.agents) if (user.puzzle.target == parent->id) user.puzzle.stage = parent->puzzle.sequenceProgress;
      if (parent->puzzle.sequenceProgress >= parent->puzzle.leverOrder.size()) CompletePuzzle(agent, *parent);
      else world_.message = "Palanca correcta. Continúa el orden del panel de acceso.";
      return true;
    }
    if (object.puzzle.state == PuzzleState::Solved) { world_.message = object.puzzle.intelligence.empty() ? "Dispositivo resuelto." : object.puzzle.intelligence; return false; }
    if (object.puzzle.cooldown > 0.f) { world_.message = "El dispositivo está bloqueado temporalmente tras el último intento."; return false; }
    if (Family(object) != PuzzleFamily::Coordination && object.user != 0 && object.user != agent.id) { world_.message = "Otro agente está operando este dispositivo."; return false; }
    const unsigned int coordinationButton = Family(object) == PuzzleFamily::Coordination ? Coordination::AvailableButton(world_, agent, object) : 0u;
    if (Family(object) == PuzzleFamily::Coordination && !coordinationButton) { world_.message = "Todos los controles del panel están ocupados."; return false; }
    CancelPuzzle(agent);
    ReleaseCqc(agent);
    CancelManeuver(agent);
    agent.draggingGuard = -1; agent.cover = {}; agent.moving = false; agent.running = false;
    agent.puzzle.target = object.id; agent.puzzle.kind = object.puzzle.kind;
    agent.puzzle.length = std::clamp(object.puzzle.difficulty, DevicesConfig::PuzzleMinimumStages, DevicesConfig::PuzzleMaximumStages);
    PrepareChallenge(agent.puzzle, world_.seed, object);
    agent.puzzle.coordinationButton = coordinationButton;
    if (!object.user) object.user = agent.id;
    object.puzzle.state = PuzzleState::InUse;
    if (agent.puzzle.family == PuzzleFamily::Coordination) Coordination::Refresh(world_, object);
    Record(EventKind::PuzzleStarted, agent.id, object.id);
    if (agent.puzzle.family != PuzzleFamily::Coordination)
      EmitSound(object.puzzle.kind == PuzzleKind::Computer ? SoundKind::ComputerKey : SoundKind::Button, object.position, GameplayConfig::InteractionDistance * 2.f, agent.id, object.elevation);
    world_.message = "Intervención táctica: reproduce señales, conecta terminales o coordina los controles físicos. ESC cancela sin resolver.";
    return true;
  }

  void Simulation::CompletePuzzle(Agent &agent, Interactable &object) {
    const auto kind = object.puzzle.kind;
    object.user = 0; object.puzzle.state = PuzzleState::Solved; object.completed = true;
    Record(EventKind::PuzzleSolved, agent.id, object.id);
    EmitSound(SoundKind::PuzzleSolved, object.position, GameplayConfig::InteractionDistance * 2.f, agent.id, object.elevation);
    switch (kind) {
    case PuzzleKind::Explosive:
      object.trap.state = TrapState::Disarmed; object.trap.remaining = -1.f; object.active = false;
      EmitSound(SoundKind::TrapDisarm, object.position, GameplayConfig::InteractionDistance * 2.f, agent.id, object.elevation);
      world_.message = "Explosivo desactivado: el temporizador y el detonador están aislados.";
      break;
    case PuzzleKind::Door:
      object.locked = false; object.open = true;
      EmitSound(SoundKind::Lock, object.position, GameplayConfig::InteractionDistance * 2.f, agent.id, object.elevation);
      EmitSound(SoundKind::DoorOpen, object.position, GameplayConfig::InteractionDistance * 2.f, agent.id, object.elevation);
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
        ClearCamera(camera);
        Record(EventKind::CameraDisabled, agent.id, camera.id);
      }
      object.puzzle.cooldown = DevicesConfig::CameraPanelDisabledDuration;
      world_.message = "Grupo de cámaras desactivado durante 45 segundos. Después recuperará la vigilancia.";
      break;
    case PuzzleKind::Coordination:
      object.active = false; object.progress = GameplayConfig::CoopHoldDuration;
      if (!IsTraining(world_)) {
        if (world_.contract == ContractId::SilentRelay) { ++world_.objectives; Record(EventKind::Objective, agent.id, object.id); }
        else { ++world_.secondary; Record(EventKind::Secondary, agent.id, object.id); }
        Record(EventKind::Sabotage, agent.id, object.id);
        for (auto &user : world_.agents) if (user.puzzle.target == object.id && user.privateOrder == PrivateOrder::RecoverDevice && !user.privateComplete) { user.privateComplete = true; Record(EventKind::PrivateObjective, user.id, object.id); }
      }
      world_.message = "Interruptores sincronizados: repetidor saboteado y circuito asegurado.";
      break;
    default: break;
    }
    for (auto &user : world_.agents) if (user.puzzle.target == object.id) CancelPuzzle(user);
    agent.puzzle = {};
  }

  void Simulation::FailPuzzle(Agent &agent, Interactable &object) {
    CancelManeuver(agent);
    Record(EventKind::PuzzleFailed, agent.id, object.id);
    EmitSound(SoundKind::PuzzleFailed, object.position, GameplayConfig::InteractionDistance * 2.f, agent.id, object.elevation);
    for (auto &user : world_.agents) if (user.puzzle.target == object.id) CancelPuzzle(user);
    object.puzzle.state = PuzzleState::Failed; object.puzzle.cooldown = DevicesConfig::PuzzleFailureCooldown;
    object.puzzle.sequenceProgress = 0; object.puzzle.coordinationProgress = 0.f; object.user = 0;
    for (auto &node : world_.objects) if (node.puzzle.parentDevice == object.id) node.open = false;
    if (object.puzzle.kind == PuzzleKind::Explosive) Detonate(object);
    else { EmitNoise(object.position, GameplayConfig::NonLethalNoiseRadius, agent.id, 0, SoundKind::PuzzleFailed, {}, object.elevation); world_.message = "Intervención incorrecta: aviso local. El circuito se reinicia en tres segundos y conserva la protección."; }
  }

  bool Simulation::UpdatePuzzle(Agent &agent, const ActionCommand &command) {
    if (agent.puzzle.target == 0) return command.puzzleInput != -1;
    auto found = std::ranges::find_if(world_.objects, [&agent](const Interactable &object) { return object.id == agent.puzzle.target; });
    if (found == world_.objects.end() || (Family(*found) != PuzzleFamily::Coordination && found->user != agent.id) || !Operational(agent) || !found->active ||
        found->puzzle.state != PuzzleState::InUse || !IsTactical(world_) ||
        !CanOperate(world_, agent, *found)) {
      CancelPuzzle(agent); return true;
    }
    agent.moving = false; agent.running = false;
    if (agent.puzzle.family == PuzzleFamily::Coordination) {
      if (command.puzzleTarget == agent.puzzle.target && command.puzzleInput == 0) { CancelPuzzle(agent); world_.message = "Coordinación interrumpida; mantén todos los controles para reiniciar."; }
      else {
        const bool holding = command.puzzleTarget == agent.puzzle.target && command.puzzleHeld;
        if (holding && !agent.puzzle.holding) EmitSound(SoundKind::Button, Coordination::ButtonPosition(*found, agent.puzzle.coordinationButton),
          GameplayConfig::InteractionDistance * 2.f, agent.id, found->elevation);
        agent.puzzle.holding = holding;
      }
      return true;
    }
    if (command.puzzleInput == -1) return true;
    if (command.puzzleTarget != agent.puzzle.target) return true;
    auto &object = *found;
    if (command.puzzleInput == 0) { CancelPuzzle(agent); world_.message = "Interacción cancelada; el dispositivo conserva su estado."; return true; }
    if (agent.puzzle.family == PuzzleFamily::Levers) { world_.message = "Acciona las palancas físicas en el orden indicado; cierra el panel para desplazarte."; return true; }
    if (command.puzzleInput < 1 || command.puzzleInput > 16) return true;
    bool correct = false;
    if (agent.puzzle.family == PuzzleFamily::Arrows) correct = agent.puzzle.stage < agent.puzzle.sequence.size() && command.puzzleInput == agent.puzzle.sequence[agent.puzzle.stage];
    else if (agent.puzzle.family == PuzzleFamily::Wires) {
      const unsigned int source = static_cast<unsigned int>((command.puzzleInput - 1) / 4 + 1), port = static_cast<unsigned int>((command.puzzleInput - 1) % 4 + 1);
      const auto sourceBit = static_cast<std::uint8_t>(1u << (source - 1u)), portBit = static_cast<std::uint8_t>(1u << (port - 1u));
      correct = !(agent.puzzle.connectedSources & sourceBit) && !(agent.puzzle.connectedPorts & portBit) && agent.puzzle.wireLabels[port - 1] == source && (object.puzzle.wireCrossingAllowed || source == port);
      if (correct) { agent.puzzle.connectedSources |= sourceBit; agent.puzzle.connectedPorts |= portBit; }
    }
    if (!correct) {
      FailPuzzle(agent, object);
      return true;
    }
    Record(EventKind::PuzzleStep, agent.id, object.id);
    EmitSound(agent.puzzle.family == PuzzleFamily::Wires ? SoundKind::WireConnect : object.puzzle.kind == PuzzleKind::Computer ? SoundKind::ComputerKey : SoundKind::PuzzleStep, object.position, GameplayConfig::InteractionDistance * 2.f, agent.id, object.elevation);
    if (++agent.puzzle.stage >= agent.puzzle.length) CompletePuzzle(agent, object);
    return true;
  }

  void Simulation::BuildPuzzleNodes() {
    std::uint32_t next = 1;
    for (const auto &object : world_.objects) next = std::max(next, object.id + 1);
    if (IsTraining(world_) && std::ranges::none_of(world_.objects, [](const Interactable &object) { return object.kind == ObjectKind::CoopSwitch; })) {
      Interactable coordination{next++, ObjectKind::CoopSwitch, {1450.f, 430.f}, "Sincronización de prácticas"};
      coordination.footprint = {44.f, 34.f}; coordination.solid = SpatialOverride::Disabled; coordination.blocksVision = SpatialOverride::Disabled;
      coordination.puzzle.kind = PuzzleKind::Coordination; coordination.puzzle.family = PuzzleFamily::Coordination;
      world_.objects.push_back(coordination);
    }
    const std::size_t originalSize = world_.objects.size();
    for (std::size_t index = 0; index < originalSize; ++index) {
      if (world_.objects[index].kind == ObjectKind::CoopSwitch) {
        world_.objects[index].puzzle.kind = PuzzleKind::Coordination; world_.objects[index].puzzle.family = PuzzleFamily::Coordination;
      }
      if (world_.objects[index].kind != ObjectKind::Computer || world_.objects[index].puzzle.parentDevice != 0) continue;
      const auto parent = world_.objects[index].id; const auto position = world_.objects[index].position;
      const float elevation = world_.objects[index].elevation; const auto support = world_.objects[index].supportId;
      world_.objects[index].puzzle.family = PuzzleFamily::Levers;
      world_.objects[index].puzzle.leverOrder = Order(world_.seed, parent, 4, true);
      if (std::ranges::any_of(world_.objects, [parent](const Interactable &object) { return object.puzzle.parentDevice == parent; })) continue;
      const float spacing = DevicesConfig::PuzzleNodeSpacing;
      const std::array<sf::Vector2f, 12> offsets{{{spacing, 0.f}, {-spacing, 0.f}, {0.f, spacing}, {0.f, -spacing},
        {spacing, spacing}, {-spacing, spacing}, {spacing, -spacing}, {-spacing, -spacing},
        {spacing * 1.5f, 0.f}, {-spacing * 1.5f, 0.f}, {0.f, spacing * 1.5f}, {0.f, -spacing * 1.5f}}};
      std::vector<sf::Vector2f> assigned;
      for (const auto offset : offsets) {
        const auto point = position + offset;
        if (WorldGeometry::PositionBlocked(world_, point, GameplayConfig::AgentRadius + DevicesConfig::PuzzleNodeSize * 0.5f, elevation, CombatConfig::StandingHeight, support) ||
            std::ranges::any_of(assigned, [point](sf::Vector2f other) { return (point - other).length() < DevicesConfig::PuzzleNodeMinimumSeparation; })) continue;
        Interactable node{next++, ObjectKind::PuzzleNode, point, "Palanca " + std::string(1, static_cast<char>('A' + assigned.size()))};
        node.elevation = elevation; node.supportId = support;
        node.footprint = {DevicesConfig::PuzzleNodeSize, DevicesConfig::PuzzleNodeSize}; node.solid = SpatialOverride::Disabled; node.blocksVision = SpatialOverride::Disabled;
        node.puzzle.kind = PuzzleKind::Computer; node.puzzle.parentDevice = parent; node.puzzle.nodeIndex = static_cast<unsigned int>(assigned.size() + 1);
        world_.objects.push_back(node); assigned.push_back(point);
        if (assigned.size() == 4) break;
      }
    }
  }

  void Simulation::Detonate(Interactable &object) {
    if (!IsTactical(world_) || object.trap.state == TrapState::Disarmed || object.trap.state == TrapState::Detonated) return;
    for (auto &agent : world_.agents) if (agent.puzzle.target == object.id) CancelPuzzle(agent);
    object.active = false; object.completed = true; object.user = 0;
    object.trap.state = TrapState::Detonated; object.trap.remaining = 0.f; object.trap.effectRemaining = DevicesConfig::ExplosionEffectDuration;
    Record(EventKind::Explosion, 0, object.id);
    EmitSound(SoundKind::Explosion, object.position, DevicesConfig::ExplosionNoiseRadius, 0, object.elevation);
    EmitNoise(object.position, DevicesConfig::ExplosionNoiseRadius, 0, 0, SoundKind::Explosion, {}, object.elevation);
    const auto exposed = [this, &object](sf::Vector2f target, float elevation) {
      const sf::Vector3f from{object.position.x, object.position.y, object.elevation + 2.f}, to{target.x, target.y, elevation + CombatConfig::ProneHeight};
      if (WorldGeometry::SpatialDistance(from, to) > object.trap.blastRadius || WorldGeometry::RayOccluded(world_, from, to, object.id)) return false;
      for (const auto &obstacle : world_.obstacles) if (WorldGeometry::BlocksMovement(obstacle) && WorldGeometry::SegmentPrism(from, to, obstacle.bounds, obstacle.elevation, obstacle.elevation + WorldGeometry::ObstacleHeight(obstacle))) return false;
      for (const auto &other : world_.objects) if (other.id != object.id && WorldGeometry::BlocksMovement(other) && WorldGeometry::SegmentPrism(from, to, WorldGeometry::ObjectFootprint(other), other.elevation, other.elevation + WorldGeometry::ObjectHeight(other))) return false;
      return true;
    };
    const auto damage = [&object](sf::Vector2f target, float elevation) {
      const float distance = std::hypot((target - object.position).length(), elevation - object.elevation);
      return object.trap.damage * std::clamp(1.f - distance / object.trap.blastRadius * 0.65f, 0.35f, 1.f);
    };
    for (auto &agent : world_.agents) if (agent.connected && agent.alive && !agent.extracted && exposed(agent.position, agent.elevation)) DamageAgent(agent, damage(agent.position, agent.elevation), damage(agent.position, agent.elevation), 0, object.id);
    for (auto &guard : world_.guards) if (guard.state != GuardState::Dead && exposed(guard.position, guard.elevation)) {
      for (auto &agent : world_.agents) if (agent.cqcHoldingGuard == guard.id) ReleaseCqc(agent);
      if (DamageTrainingDummy(guard, damage(guard.position, guard.elevation))) continue;
      guard.health = std::max(0.f, guard.health - damage(guard.position, guard.elevation));
      if (guard.health <= 0.f) {
        guard.state = GuardState::Dead; guard.heldBy = 0; guard.cqcState = CqcState::None; guard.bodyHidden = false; guard.storedIn = 0; Record(EventKind::GuardKilled, 0, guard.id);
      }
      else if (guard.state != GuardState::Incapacitated) { guard.state = GuardState::Search; guard.lastKnown = object.position; guard.lastKnownElevation = object.elevation; guard.navigation.clear(); }
    }
    if (!IsTraining(world_)) {
      if (!world_.alarm) { world_.alarm = true; ++world_.alarmCount; Record(EventKind::AgentAlarm, 0, object.id); }
      RegisterSecurityContact(object.position, object.elevation);
      world_.message = "Explosión: daño real, alarma y respuesta de seguridad activados.";
    } else world_.message = "Explosión de prácticas: impacto y efectos reales, con protección para los agentes y los muñecos.";
  }

  void Simulation::UpdateDevices(float dt) {
    using namespace DevicesConfig;
    if (!IsTactical(world_)) return;
    for (auto &agent : world_.agents) if (agent.puzzle.target != 0) {
      ActionCommand validate; validate.puzzleTarget = agent.puzzle.target; validate.puzzleHeld = agent.puzzle.holding;
      UpdatePuzzle(agent, validate);
    }
    for (auto &object : world_.objects) {
      if (object.puzzle.cooldown > 0.f) {
        object.puzzle.cooldown = std::max(0.f, object.puzzle.cooldown - dt);
        if (object.puzzle.cooldown <= 0.f && (object.puzzle.state == PuzzleState::Failed || object.puzzle.kind == PuzzleKind::CameraPanel)) {
          object.puzzle.state = PuzzleState::Ready;
          if (object.puzzle.kind == PuzzleKind::CameraPanel) object.completed = false;
        }
      }
      if (Family(object) == PuzzleFamily::Coordination) Coordination::Refresh(world_, object);
      if (Family(object) == PuzzleFamily::Coordination && object.puzzle.state == PuzzleState::InUse) {
        const unsigned int required = Coordination::RequiredAgents(world_, object);
        const auto held = static_cast<unsigned int>(std::popcount(static_cast<unsigned int>(object.puzzle.coordinationMask)));
        object.puzzle.coordinationProgress = held >= required ? std::min(CoordinationHoldDuration, object.puzzle.coordinationProgress + dt) : 0.f;
        Agent *operatorAgent = nullptr;
        for (auto &agent : world_.agents) if (agent.puzzle.target == object.id) { agent.puzzle.coordinationProgress = object.puzzle.coordinationProgress; if (!operatorAgent) operatorAgent = &agent; }
        if (operatorAgent && object.puzzle.coordinationProgress >= CoordinationHoldDuration) CompletePuzzle(*operatorAgent, object);
      }
      if (object.kind == ObjectKind::Camera) {
        auto &camera = object.camera;
        if (camera.state == CameraState::Disabled && camera.reactivationRemaining > 0.f) {
          camera.reactivationRemaining = std::max(0.f, camera.reactivationRemaining - dt);
          if (camera.reactivationRemaining <= 0.f) { camera.state = CameraState::Operating; object.active = true; EmitSound(SoundKind::Camera, object.position, CameraRange, 0, object.elevation); }
        }
        if (!object.active || camera.state != CameraState::Operating) { ClearCamera(object); continue; }
        camera.suspicionRemaining = std::max(0.f, camera.suspicionRemaining - dt);
        camera.confirmedRemaining = std::max(0.f, camera.confirmedRemaining - dt);
        camera.searchRemaining = std::max(0.f, camera.searchRemaining - dt);
        if (camera.sweeping && camera.trackedAgent == 0 && camera.searchRemaining <= 0.f) {
          if (camera.pauseRemaining > 0.f) camera.pauseRemaining = std::max(0.f, camera.pauseRemaining - dt);
          else {
            camera.sweepAngle += camera.speed * static_cast<float>(camera.direction) * dt;
            if (camera.sweepAngle >= camera.maxAngle || camera.sweepAngle <= camera.minAngle) {
              camera.sweepAngle = std::clamp(camera.sweepAngle, camera.minAngle, camera.maxAngle);
              camera.direction = -camera.direction; camera.pauseRemaining = CameraEndpointPause;
              EmitSound(SoundKind::Camera, object.position, camera.range, 0, object.elevation);
            }
          }
          const float angle = camera.baseAngle + camera.sweepAngle;
          LookTowards(object, {std::cos(angle), std::sin(angle)}, camera.trackingSpeed * dt);
        }
        const auto visible = [&object, &camera, this](const Agent &agent) {
          if (!agent.connected || !agent.alive || agent.incapacitated || agent.extracted) return false;
          const auto delta = agent.position - object.position;
          const float distance = std::hypot(delta.length(), agent.elevation - object.elevation);
          const float posture = agent.stance == Stance::Prone ? GameplayConfig::ProneVisionFactor : agent.stance == Stance::Crouched ? GameplayConfig::CrouchedVisionFactor : 1.f;
          return distance <= camera.range * posture && object.facing.dot(delta) >= camera.cosHalfAngle * distance * object.facing.length() &&
            Visibility::AgentExposed(world_, object.position, agent, object.id, object.elevation + CombatGeometry::HeadCenterHeight(Stance::Standing));
        };
        Agent *seen = nullptr;
        if (camera.trackedAgent != 0) {
          const auto tracked = std::ranges::find_if(world_.agents, [&camera](const Agent &agent) { return agent.id == camera.trackedAgent; });
          if (tracked != world_.agents.end() && visible(*tracked)) seen = &*tracked;
          else { camera.trackedAgent = 0; camera.searchRemaining = CameraSearchDuration; }
        }
        if (!seen) for (auto &agent : world_.agents) if (visible(agent) && (!seen || (agent.position - object.position).lengthSquared() < (seen->position - object.position).lengthSquared())) seen = &agent;
        if (seen) {
          camera.lostSightTime = 0.f;
          if (object.observedAgent != 0 && object.observedAgent != seen->id) { object.progress = 0.f; camera.confirmedRemaining = 0.f; }
          object.observedAgent = seen->id; camera.trackedAgent = seen->id; camera.lastKnown = seen->position;
          camera.searchRemaining = CameraSearchDuration; camera.suspicionRemaining = CameraSuspicionIndicatorDuration;
          LookTowards(object, seen->position - object.position, camera.trackingSpeed * dt);
          const bool confirmed = camera.confirmedRemaining > 0.f;
          object.progress = std::min(camera.confirmationTime, object.progress + dt * Perception::DetectionRate(*seen, (seen->position - object.position).length(), camera.range));
          camera.perception = object.progress >= camera.confirmationTime ? CameraPerception::Tracking : CameraPerception::Suspicion;
          if (object.progress >= camera.confirmationTime) {
            camera.confirmedRemaining = CameraConfirmedIndicatorDuration;
            if (!IsTraining(world_)) {
              if (!confirmed) Detect(*seen, object.id + 1000);
              else RegisterSecurityContact(seen->position, seen->elevation);
            } else if (!confirmed) { EmitSound(SoundKind::DetectionConfirmed, object.position, camera.range, 0, object.elevation); world_.message = "Cámara de prácticas: detección confirmada; sin alarma operativa ni refuerzos."; }
          }
          ReportPerception(*seen, object.progress / std::max(0.001f, camera.confirmationTime), camera.confirmedRemaining > 0.f);
        } else {
          camera.lostSightTime = std::min(CameraLostSightGrace + 1.f, camera.lostSightTime + dt);
          if (camera.lostSightTime > CameraLostSightGrace) object.progress = std::max(0.f, object.progress - dt * camera.confirmationTime * CameraSuspicionDecay);
          camera.perception = camera.searchRemaining > 0.f ? CameraPerception::Searching : CameraPerception::Sweeping;
          if (camera.searchRemaining > 0.f) LookTowards(object, camera.lastKnown - object.position, camera.trackingSpeed * dt);
          if (camera.searchRemaining <= 0.f && camera.confirmedRemaining <= 0.f && camera.suspicionRemaining <= 0.f) object.observedAgent = 0;
        }
        camera.indicatorProgress = camera.confirmedRemaining > 0.f ? 1.f : std::max(object.progress / std::max(0.001f, camera.confirmationTime),
          camera.suspicionRemaining / CameraSuspicionIndicatorDuration * 0.15f);
      } else if (object.kind == ObjectKind::Explosive) {
        auto &trap = object.trap;
        trap.effectRemaining = std::max(0.f, trap.effectRemaining - dt);
        if (!object.active || trap.state == TrapState::Disarmed || trap.state == TrapState::Detonated) continue;
        bool audible = false;
        for (const auto &agent : world_.agents) if (Operational(agent)) {
          const float distance = std::hypot((agent.position - object.position).length(), agent.elevation - object.elevation);
          audible |= distance <= trap.audibleRadius;
          if (distance > trap.audibleRadius || WorldGeometry::RayOccluded(world_, {object.position.x, object.position.y, object.elevation + CombatConfig::ProneHeight}, {agent.position.x, agent.position.y, agent.elevation + CombatConfig::ProneHeight}, object.id)) continue;
          if (trap.state == TrapState::Armed) trap.state = TrapState::Detected;
          if (distance <= trap.triggerRadius && trap.remaining < 0.f) { trap.remaining = trap.fuse; EmitNoise(object.position, trap.audibleRadius, 0, 0, SoundKind::Impact, {}, object.elevation); EmitSound(SoundKind::TrapArm, object.position, trap.audibleRadius, 0, object.elevation); world_.message = "Temporizador activado: aléjate o resuelve el circuito del explosivo."; }
        }
        trap.tickRemaining = std::max(0.f, trap.tickRemaining - dt);
        if (audible && trap.tickRemaining <= 0.f) {
          EmitSound(SoundKind::TrapTick, object.position, trap.audibleRadius, 0, object.elevation);
          Record(EventKind::TrapTick, 0, object.id);
          trap.tickRemaining = trap.remaining < 0.f ? TrapMaximumTickInterval : std::clamp(trap.remaining / trap.fuse * TrapMaximumTickInterval, TrapMinimumTickInterval, TrapMaximumTickInterval);
        }
        if (trap.remaining >= 0.f) { trap.remaining = std::max(0.f, trap.remaining - dt); if (trap.remaining <= 0.f) Detonate(object); }
      }
    }
  }
}
