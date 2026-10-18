#include "NoTraceSimulation.h"
#include "NoTraceSpatialFixture.h"
#include "NoTraceWorldGeometry.h"
#include "NoTraceTraversal.h"
#include "Config/NoTraceCqcConfig.h"
#include "Config/NoTraceTrainingConfig.h"
#include "Config/NoTraceWeaponConfig.h"
#include "NoTracePerception.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
  using namespace NoTrace;
  int checks = 0;
  void Require(bool condition, const char *message) { ++checks; if (!condition) throw std::runtime_error(message); }
  WorldState &World(Simulation &simulation) { return const_cast<WorldState &>(simulation.GetState()); }
  void Advance(Simulation &simulation, float seconds) {
    for (int frame = 0; frame < static_cast<int>(std::ceil(seconds / 0.1f)); ++frame) simulation.Step(0.1f, {});
  }
  void Advance(Simulation &simulation, ActionCommand command, float seconds) {
    for (int frame = 0; frame < static_cast<int>(std::ceil(seconds / 0.1f)); ++frame) simulation.Step(0.1f, {{1, command}});
  }
  Interactable &Object(WorldState &world, std::uint32_t id) {
    const auto found = std::ranges::find_if(world.objects, [id](const Interactable &object) { return object.id == id; });
    if (found == world.objects.end()) throw std::runtime_error("No existe el dispositivo de prácticas");
    return *found;
  }
  void Interact(Simulation &simulation, sf::Vector2f position, sf::Vector2f facing = {1.f, 0.f}) {
    auto &agent = World(simulation).agents[0]; agent.position = position; agent.facing = facing; agent.actionCooldown = 0.f;
    ActionCommand command; command.interact = true; command.aimX = facing.x; command.aimY = facing.y;
    simulation.Step(0.1f, {{1, command}});
  }
  void Solve(Simulation &simulation) {
    const auto initial = simulation.GetState().agents[0].puzzle;
    if (initial.family == PuzzleFamily::Levers) {
      ActionCommand cancel; cancel.puzzleTarget = initial.target; cancel.puzzleInput = 0; simulation.Step(0.1f, {{1, cancel}});
      for (const auto index : initial.sequence) {
        const auto &objects = simulation.GetState().objects;
        const auto node = std::ranges::find_if(objects, [&initial, index](const Interactable &object) { return object.puzzle.parentDevice == initial.target && object.puzzle.nodeIndex == index; });
        if (node == objects.end()) throw std::runtime_error("Falta una palanca física del circuito de prácticas");
        Interact(simulation, node->position - sf::Vector2f{30.f, 0.f});
      }
      return;
    }
    for (unsigned int stage = 0; stage < DevicesConfig::PuzzleMaximumStages && simulation.GetState().agents[0].puzzle.target != 0; ++stage) {
      const auto puzzle = simulation.GetState().agents[0].puzzle;
      ActionCommand command; command.puzzleTarget = puzzle.target;
      if (puzzle.family == PuzzleFamily::Arrows) command.puzzleInput = puzzle.sequence.at(puzzle.stage);
      else if (puzzle.family == PuzzleFamily::Wires) for (unsigned int source = 1; source <= 4; ++source) if (!(puzzle.connectedSources & (1u << (source - 1)))) {
        command.puzzleInput = WireInput(source, MatchingWirePort(puzzle, source)); break;
      }
      simulation.Step(0.1f, {{1, command}});
    }
  }
  void Shoot(Simulation &simulation, bool lethal, float distance) {
    auto &agent = World(simulation).agents[0];
    if ((lethal ? agent.lethalMagazine : agent.nonLethalMagazine) <= 0) {
      ActionCommand reload; reload.weaponChoice = lethal ? 0 : 1; reload.reloadWeapon = true;
      simulation.Step(0.1f, {{1, reload}}); Advance(simulation, WeaponConfig::ReloadDuration + 0.1f);
    }
    ActionCommand fire; fire.fireLethal = lethal; fire.fireNonLethal = !lethal; fire.aimX = 1.f;
    fire.aimSlope = (CombatConfig::StandingHeight - CombatConfig::HeadHeight * 0.5f - CombatConfig::StandingWeaponHeight) / distance;
    simulation.Step(0.1f, {{1, fire}}); Advance(simulation, GameplayConfig::WeaponCooldown + 0.1f);
  }
  void LobbyHasAnArenaWithoutEnemyGuards() {
    Simulation simulation; simulation.ResetBase();
    const auto &world = simulation.GetState();
    Require(IsTraining(world) && IsTactical(world) && world.phase == GamePhase::Base && world.size == TrainingConfig::WorldSize,
      "La sala no conserva un modo táctico de entrenamiento explícito");
    Require(world.guards.size() == TrainingConfig::DummyCount && std::ranges::all_of(world.guards, [](const Guard &guard) { return guard.trainingDummy; }),
      "La sala contiene guardias enemigos o no incluye sus objetivos de prácticas");
    for (const auto kind : {ObjectKind::Operations, ObjectKind::Armory, ObjectKind::Intelligence, ObjectKind::Deployment, ObjectKind::Camera,
         ObjectKind::Explosive, ObjectKind::Door, ObjectKind::Computer, ObjectKind::CameraPanel, ObjectKind::CoopSwitch, ObjectKind::PuzzleNode, ObjectKind::TrainingReset})
      Require(std::ranges::any_of(world.objects, [kind](const Interactable &object) { return object.kind == kind; }), "La sala ha eliminado una estación o un dispositivo utilizable");
    for (const auto kind : {HideoutKind::Closet, HideoutKind::TrashBin, HideoutKind::UnderTable})
      Require(std::ranges::any_of(world.objects, [kind](const Interactable &object) { return object.hideoutKind == kind; }), "Falta un tipo de escondite en el campo de prácticas");
    Advance(simulation, 35.f);
    Require(world.guards.size() == TrainingConfig::DummyCount && !world.alarm && !world.securityActive && world.reinforcementWaves == 0 && world.securityEntries.empty(),
      "La espera genera patrullas, alarmas o refuerzos enemigos");
    for (std::size_t index = 0; index < world.guards.size(); ++index) {
      const auto &dummy = world.guards[index]; const auto &spawn = TrainingConfig::DummySpawns.at(index);
      Require(dummy.position == spawn.position && dummy.facing == spawn.facing && dummy.elevation == spawn.elevation && dummy.supportId == spawn.supportId && dummy.navigation.empty(),
        "Un muñeco abandona su puesto configurado o ejecuta navegación enemiga");
    }
    auto &editable = World(simulation);
    for (std::size_t index = 0; index < TrainingConfig::MaximumRetainedEvents + 10; ++index) editable.events.push_back({index + 1, 0, 1, EventKind::Noise, 0});
    simulation.Step(0.1f, {});
    Require(world.events.size() == TrainingConfig::MaximumRetainedEvents && world.events.front().sequence == 11,
      "El entrenamiento indefinido acumula un registro ilimitado que desborda las instantáneas");
  }
  void PlayerProtectionAndRealReload() {
    Simulation simulation; simulation.ResetBase({{1, "Uno"}, {2, "Dos"}});
    auto &world = World(simulation); world.objects.clear(); world.obstacles.clear();
    world.agents[0].position = {200.f, 300.f}; world.agents[1].position = {250.f, 300.f};
    for (int shot = 0; shot < 6; ++shot) Shoot(simulation, true, 50.f);
    Require(world.agents[1].alive && !world.agents[1].incapacitated && world.agents[1].health >= 1.f && world.agents[1].health < GameplayConfig::MaxHealth,
      "Los disparos letales matan o no registran impactos durante el entrenamiento");
    for (int shot = 0; shot < 6; ++shot) Shoot(simulation, false, 50.f);
    Require(world.agents[1].alive && !world.agents[1].incapacitated && !world.agents[1].friendlyIncapacitated,
      "Los disparos no letales desmayan al compañero en la sala segura");
    world.agents[0].selectedWeapon = WeaponKind::Pistol; world.agents[0].lethalMagazine = 0;
    ActionCommand reload; reload.reloadWeapon = true; simulation.Step(0.1f, {{1, reload}});
    Require(world.agents[0].reloadRemaining > 0.f && world.agents[0].lethalMagazine == 0, "La munición infinita evita la maniobra real de recarga");
    Advance(simulation, WeaponConfig::ReloadDuration + 0.1f);
    Require(world.agents[0].lethalMagazine == WeaponConfig::PistolCapacity && world.agents[0].loadout.lethalAmmo == TrainingConfig::RefilledAmmo,
      "La recarga de prácticas no completa el cargador o agota la reserva infinita");
  }
  void DummiesSurviveWeaponsAndSubdual() {
    Simulation simulation; simulation.ResetBase(); auto &world = World(simulation);
    world.agents[0].position = {1100.f, 210.f}; world.agents[0].facing = {1.f, 0.f};
    for (int shot = 0; shot < 6; ++shot) { Shoot(simulation, true, 110.f); Shoot(simulation, false, 110.f); }
    auto &dummy = world.guards[0];
    Require(dummy.trainingHits >= 12 && dummy.health >= TrainingConfig::DummyMinimumHealth && dummy.health < TrainingConfig::DummyMaximumHealth,
      "El muñeco no registra impactos letales y no letales ni mantiene vida visible");
    Require(dummy.state != GuardState::Dead && dummy.state != GuardState::Incapacitated && dummy.stun == 0.f && !dummy.bodyHidden,
      "El daño repetido elimina, incapacita u oculta un muñeco indestructible");
    const auto hits = dummy.trainingHits;
    world.agents[0].position = dummy.position - sf::Vector2f{28.f, 0.f}; world.agents[0].actionCooldown = 0.f; world.agents[0].stamina = GameplayConfig::MaxStamina;
    ActionCommand hold; hold.cqcHeld = true; hold.cqc = true; hold.aimX = 1.f;
    simulation.Step(0.1f, {{1, hold}}); hold.cqc = false; Advance(simulation, hold, 0.3f);
    Require(dummy.heldBy == 1 && world.agents[0].cqcHoldingGuard == dummy.id, "El muñeco no permite iniciar un agarre de entrenamiento");
    for (unsigned int pulse = 0; pulse < CqcConfig::RequiredPulses; ++pulse) {
      Advance(simulation, 0.2f); hold.cqc = true; simulation.Step(0.1f, {{1, hold}}); hold.cqc = false; Advance(simulation, hold, 0.1f);
    }
    Require(dummy.heldBy == 0 && world.agents[0].cqcHoldingGuard == 0 && dummy.trainingHits > hits && dummy.state != GuardState::Incapacitated && dummy.state != GuardState::Dead,
      "Someter el muñeco crea un cuerpo real o deja bloqueada la sujeción");
    Require(dummy.trainingFeedback > 0.f && world.guards.size() == TrainingConfig::DummyCount, "El muñeco no presenta reacción o desaparece tras las pruebas");
  }
  void CamerasTrapsAndPuzzlesStayActive() {
    Simulation simulation; simulation.ResetBase(); auto &world = World(simulation);
    auto &camera = Object(world, 100); camera.camera.sweeping = false; camera.facing = {-1.f, 0.f};
    world.agents[0].position = {1570.f, 260.f}; Advance(simulation, 1.f);
    Require(!world.agents[0].detectionConfirmed && camera.progress > 0.f && camera.progress < camera.camera.confirmationTime && !world.alarm && !world.securityActive,
      "La cámara de entrenamiento confirma antes del umbral gradual o genera una respuesta enemiga durante la sospecha");
    const float cameraRate = Perception::DetectionRate(world.agents[0], (camera.position - world.agents[0].position).length(), camera.camera.range);
    const float confirmationRemaining = (camera.camera.confirmationTime - camera.progress) / cameraRate;
    Advance(simulation, confirmationRemaining + GameplayConfig::MaximumStep);
    Require(world.agents[0].detectionConfirmed && world.agents[0].detectionProgress == 1.f && !world.alarm && !world.securityActive,
      "La cámara de entrenamiento no detecta o activa una respuesta operativa enemiga");
    Interact(simulation, {1600.f, 430.f}); Solve(simulation);
    Require(!camera.active && camera.camera.state == CameraState::Disabled, "El panel de prácticas no modifica la percepción real de las cámaras");
    Interact(simulation, {1460.f, 600.f}); Solve(simulation);
    Require(Object(world, 103).open && !Object(world, 103).locked && !WorldGeometry::BlocksMovement(Object(world, 103)), "La cerradura de entrenamiento no abre el paso real");
    Interact(simulation, {1640.f, 650.f}); Solve(simulation);
    Require(Object(world, 104).completed && world.secondary == 0 && world.objectives == 0 && world.message.find("PRÁCTICA COMPLETADA") != std::string::npos,
      "El computador no revela su práctica o contamina la puntuación de misión");
    const auto relay = std::ranges::find_if(world.objects, [](const Interactable &object) { return object.kind == ObjectKind::CoopSwitch; });
    Require(relay != world.objects.end(), "La sala no incluye el ejercicio de coordinación");
    Interact(simulation, relay->position - sf::Vector2f{40.f, 0.f});
    ActionCommand hold; hold.puzzleTarget = relay->id; hold.puzzleHeld = true; Advance(simulation, hold, 2.1f);
    Require(relay->completed && world.secondary == 0 && world.objectives == 0, "La sincronización de prácticas no funciona en solitario o contamina objetivos");
    auto &trap = Object(world, 102); trap.trap.fuse = 0.1f;
    world.agents[0].position = {1220.f, 650.f}; world.guards[0].position = {1235.f, 650.f};
    simulation.Step(0.1f, {});
    Require(trap.trap.state == TrapState::Detonated && trap.trap.effectRemaining > 0.f, "La protección de la sala desactiva la explosión o sus efectos");
    Require(world.agents[0].alive && !world.agents[0].incapacitated && world.agents[0].health >= 1.f && world.guards[0].trainingHits > 0 && world.guards[0].health >= 1.f,
      "La explosión mata o desmaya a un jugador o muñeco protegido");
    Require(!world.alarm && world.alarmCount == 0 && !world.securityActive && world.reinforcementWaves == 0, "La trampa de entrenamiento genera alarma o refuerzos enemigos");
    Advance(simulation, 0.3f);
    Interact(simulation, TrainingConfig::ResetPosition - sf::Vector2f{40.f, 0.f});
    Require(trap.active && trap.trap.state == TrapState::Armed && trap.trap.remaining < 0.f && camera.active && Object(world, 104).puzzle.state == PuzzleState::Ready,
      "La consola no rearma trampas, cámaras y puzles para repetir indefinidamente");
    Require(!Object(world, 103).open && Object(world, 103).locked && world.guards[0].trainingHits == 0 && world.guards[0].health == TrainingConfig::DummyMaximumHealth,
      "Rearmar no restablece la cerradura o la presentación de daño de los muñecos");
    Require(relay->active && !relay->completed && relay->puzzle.coordinationProgress == 0.f && Object(world, 104).puzzle.sequenceProgress == 0 &&
      std::ranges::none_of(world.objects, [](const Interactable &object) { return object.kind == ObjectKind::PuzzleNode && object.open; }),
      "Rearmar no permite repetir la coordinación o el circuito de palancas");
    Object(world, 103).open = true; Object(world, 103).locked = false; world.guards[0].position = Object(world, 103).position;
    Interact(simulation, TrainingConfig::ResetPosition - sf::Vector2f{40.f, 0.f});
    Require(Object(world, 103).open && !Object(world, 103).locked && !WorldGeometry::PositionBlocked(world, world.guards[0].position, GameplayConfig::AgentRadius),
      "Rearmar cierra una puerta sobre un muñeco desplazado y lo incrusta en un sólido");
  }
  void HideoutsWorkInTheLobby() {
    Simulation simulation; simulation.ResetBase(); auto &world = World(simulation);
    Interact(simulation, WorldGeometry::HideoutFrontPosition(Object(world, 105), GameplayConfig::AgentRadius));
    Require(world.agents[0].hidden && world.agents[0].hideoutId == 105, "La sala no permite entrar en su armario");
    ActionCommand leave; leave.interact = true; simulation.Step(0.1f, {{1, leave}});
    Require(!world.agents[0].hidden, "El armario de prácticas no permite salir");
    Interact(simulation, {1210.f, 850.f});
    Require(world.agents[0].hideoutId == 106, "El contenedor de prácticas no funciona");
    simulation.Step(0.1f, {{1, leave}});
    Interact(simulation, {1425.f, 850.f});
    Require(world.agents[0].hideoutId == 107 && world.agents[0].stance == Stance::Prone, "La mesa de entrenamiento no permite ocultarse tumbado");
    Require(world.agents[0].actionPose == ActionPose::None && world.agents[0].actionPoseTime == 0.f && world.agents[0].transitionTime == 0.f,
      "La entrada bajo la mesa de prácticas conserva una animación o transición gradual");
    const auto position = world.agents[0].position, facing = world.agents[0].facing;
    ActionCommand mouse; mouse.aimY = -1.f; simulation.Step(0.1f, {{1, mouse}});
    Require(world.agents[0].position == position && world.agents[0].facing == facing && world.agents[0].hidden,
      "El ratón gira o desplaza al agente oculto en la mesa de prácticas");
    leave.moveY = -1.f; leave.aimX = 1.f; simulation.Step(0.1f, {{1, leave}});
    Require(!world.agents[0].hidden && world.agents[0].position.x > 1480.f && std::abs(world.agents[0].position.y - 850.f) < SpatialConfig::CollisionSkin &&
      world.agents[0].facing == facing && !WorldGeometry::PositionBlocked(world, world.agents[0].position, GameplayConfig::AgentRadius),
      "La salida de la mesa de prácticas ignora el ratón, utiliza WASD o termina en un sólido");
    Require(world.agents[0].actionPose == ActionPose::None && world.agents[0].actionPoseTime == 0.f && world.agents[0].transitionTime == 0.f,
      "La salida de la mesa de prácticas reproduce una animación retirada únicamente para mesas");
  }
  void DummiesAllowSafeDragging() {
    Simulation simulation; simulation.ResetBase(); auto &world = World(simulation);
    auto &dummy = world.guards[0]; const auto origin = dummy.position;
    world.agents[0].position = origin - sf::Vector2f{28.f, 0.f};
    ActionCommand drag; drag.dragBody = true; drag.moveY = -1.f;
    Advance(simulation, drag, 0.5f);
    Require(world.agents[0].draggingGuard == static_cast<int>(dummy.id) && dummy.position != origin,
      "Mantener E no permite probar el arrastre físico de un muñeco vivo");
    Require(dummy.state == GuardState::Patrol && !dummy.bodyHidden && dummy.health >= 1.f,
      "El arrastre de prácticas convierte al muñeco en un cuerpo incapacitado");
    const auto released = dummy.position; simulation.Step(0.1f, {}); Advance(simulation, 0.3f);
    Require(world.agents[0].draggingGuard < 0 && dummy.position == released, "Soltar E conserva un arrastre o una navegación inesperada del muñeco");
  }
  void RequireTrainingReplica(const Simulation &simulation, PlayerId recipient = 1, bool filtered = false) {
    Simulation replica; std::string error;
    const auto snapshot = simulation.SnapshotFor(recipient, filtered);
    if (!replica.LoadSnapshot(snapshot, error)) throw std::runtime_error("Instantánea del circuito de entrenamiento: " + error);
    const auto &source = simulation.GetState(), &copy = replica.GetState();
    Require(IsTraining(copy) && copy.size == source.size && copy.walkSurfaces.size() == source.walkSurfaces.size() &&
      copy.gripSurfaces.size() == source.gripSurfaces.size() && copy.verticalLinks.size() == source.verticalLinks.size() && copy.elevators.size() == source.elevators.size(),
      "La réplica pierde el circuito físico de la sala de entrenamiento");
    const auto original = std::ranges::find(source.agents, recipient, &Agent::id), restored = std::ranges::find(copy.agents, recipient, &Agent::id);
    Require(original != source.agents.end() && restored != copy.agents.end() && original->position == restored->position &&
      original->supportId == restored->supportId && original->elevation == restored->elevation && original->traversal.mode == restored->traversal.mode &&
      original->traversal.surfaceId == restored->traversal.surfaceId,
      "La réplica cambia la posición, planta o agarre del agente de prácticas");
    Require(snapshot.dump().size() < GameplayConfig::MaximumSnapshotBytes, "El circuito de entrenamiento excede el límite de las instantáneas");
  }
  void RequireTrainingDummyReplica(const Simulation &simulation, std::uint32_t id, bool filtered) {
    Simulation replica; std::string error;
    Require(replica.LoadSnapshot(simulation.SnapshotFor(1, filtered), error), "La réplica rechaza una diana elevada en caída o rearmada");
    const auto &source = simulation.GetState(), &copy = replica.GetState();
    const auto original = std::ranges::find(source.guards, id, &Guard::id), restored = std::ranges::find(copy.guards, id, &Guard::id);
    Require(original != source.guards.end() && restored != copy.guards.end(), "La instantánea pierde la diana elevada de prácticas");
    Require(restored->trainingDummy && restored->position == original->position && restored->elevation == original->elevation && restored->supportId == original->supportId &&
      restored->forcedFall.active == original->forcedFall.active && restored->forcedFall.progress == original->forcedFall.progress && restored->health == original->health,
      "La réplica cambia la posición, caída, apoyo o protección de la diana elevada");
  }
  void ElevatedTrainingDummiesCanBePushedRearmedAndPushedAgain() {
    unsigned int elevated = 0;
    for (std::size_t index = 0; index < TrainingConfig::DummySpawns.size(); ++index) {
      const auto &spawn = TrainingConfig::DummySpawns[index]; if (spawn.elevation <= 0.f) continue;
      ++elevated;
      Simulation simulation; simulation.ResetBase(); auto &world = World(simulation); auto &agent = world.agents.front();
      const auto id = TrainingConfig::FirstDummyId + static_cast<std::uint32_t>(index);
      const auto target = std::ranges::find(world.guards, id, &Guard::id);
      Require(target != world.guards.end(), "La sala no construye la diana elevada configurada");
      auto &dummy = *target;
      for (unsigned int attempt = 0; attempt < 3; ++attempt) {
        agent.position = spawn.position - spawn.facing * CqcConfig::GripSeparation;
        agent.facing = spawn.facing; agent.elevation = spawn.elevation; agent.supportId = spawn.supportId;
        const auto selected = Simulation::FindCqcTarget(world, agent, Simulation::CqcAction::Push);
        Require(selected && selected->guard == id, "La diana elevada no ofrece el empujón V desde su acceso posterior real");
        ActionCommand push; push.push = true; push.aimX = spawn.facing.x; push.aimY = spawn.facing.y;
        simulation.Step(0.1f, {{1, push}});
        Require(dummy.forcedFall.active && dummy.supportId == 0 && dummy.elevation == spawn.elevation && dummy.forcedFall.targetSupport == 1 && dummy.forcedFall.to.z == 0.f,
          "V no precipita la diana elevada hacia el suelo real del entrenamiento");
        Require((dummy.position - spawn.position).dot(spawn.facing) > GameplayConfig::AgentRadius,
          "El empujón no saca físicamente la diana del borde de su plataforma");
        RequireTrainingDummyReplica(simulation, id, false);
        const auto horizontal = dummy.position; float previous = dummy.elevation; bool intermediate = false;
        for (unsigned int frame = 0; frame < 20 && dummy.forcedFall.active; ++frame) {
          simulation.Step(0.1f, {});
          Require(dummy.position == horizontal && dummy.elevation <= previous && previous - dummy.elevation <= TraversalConfig::DropSpeed * 0.1f + 0.01f,
            "La diana elevada teletransporta su caída o cambia de trayectoria sin otro empujón");
          if (!intermediate && dummy.elevation > 0.f && dummy.elevation < spawn.elevation) {
            intermediate = true;
            RequireTrainingDummyReplica(simulation, id, false); RequireTrainingDummyReplica(simulation, id, true);
            if (attempt == 1) break;
          }
          previous = dummy.elevation;
        }
        Require(intermediate && dummy.trainingDummy && dummy.health >= TrainingConfig::DummyMinimumHealth && dummy.state != GuardState::Dead && dummy.state != GuardState::Incapacitated,
          "La caída de la diana pierde su animación intermedia o la protección del entrenamiento");
        if (attempt != 1) {
          Require(!dummy.forcedFall.active && dummy.supportId == 1 && dummy.elevation == 0.f,
            "La diana elevada no completa su caída sobre el primer piso físico inferior");
          RequireTrainingDummyReplica(simulation, id, false); RequireTrainingDummyReplica(simulation, id, true);
        } else Require(dummy.forcedFall.active, "La práctica no conserva una caída en curso antes de probar el rearme inmediato");
        agent.elevation = 0.f; agent.supportId = TrainingConfig::TraversalCourse::GroundSupport;
        Interact(simulation, TrainingConfig::ResetPosition - sf::Vector2f{40.f, 0.f});
        Require(dummy.position == spawn.position && dummy.facing == spawn.facing && dummy.elevation == spawn.elevation && dummy.supportId == spawn.supportId && !dummy.forcedFall.active,
          "Rearmar no devuelve la diana a su plataforma o conserva una caída pendiente");
        RequireTrainingDummyReplica(simulation, id, false);
        Advance(simulation, CqcConfig::PushCooldown + 0.1f);
      }
    }
    Require(elevated >= 2, "El entrenamiento no contiene las dos dianas elevadas para practicar empujones por el borde");
  }
  void TraverseCourse(Simulation &simulation, bool drop = false) {
    ActionCommand command; command.traverse = !drop; command.drop = drop;
    simulation.Step(1.f / 60.f, {{1, command}});
    auto &agent = World(simulation).agents.front();
    Require(agent.traversal.mode != TraversalMode::Grounded && agent.traversal.mode != TraversalMode::Hang && agent.traversal.mode != TraversalMode::Shimmy,
      "El control contextual no inicia la maniobra del circuito de prácticas");
    for (unsigned int frame = 0; frame < 600 && agent.traversal.mode != TraversalMode::Grounded && agent.traversal.mode != TraversalMode::Hang; ++frame)
      simulation.Step(1.f / 60.f, {});
    Require(agent.traversal.mode == TraversalMode::Grounded || agent.traversal.mode == TraversalMode::Hang,
      "La maniobra de prácticas no alcanza un apoyo o agarre estable");
    Require(!WorldGeometry::PositionBlocked(simulation.GetState(), agent.position, GameplayConfig::AgentRadius, agent.elevation, WorldGeometry::ActorHeight(agent), agent.supportId),
      "El circuito de agarres deja al agente dentro de una estructura");
    RequireTrainingReplica(simulation);
  }
  void ShimmyCourse(Simulation &simulation, float targetX) {
    auto &agent = World(simulation).agents.front(); const auto grip = agent.traversal.surfaceId;
    for (unsigned int frame = 0; frame < 900 && std::abs(targetX - agent.position.x) > 0.05f; ++frame) {
      ActionCommand command; command.moveX = std::clamp((targetX - agent.position.x) / (TraversalConfig::ShimmySpeed / 60.f), -1.f, 1.f); command.run = true;
      simulation.Step(1.f / 60.f, {{1, command}});
      Require(agent.traversal.surfaceId == grip && !agent.running && agent.supportId == 0,
        "El desplazamiento lateral de prácticas pierde el agarre o utiliza la carrera terrestre");
    }
    simulation.Step(1.f / 60.f, {});
    Require(std::abs(targetX - agent.position.x) <= 0.05f && agent.traversal.mode == TraversalMode::Hang,
      "Los controles no alcanzan el enlace del agarre de prácticas");
  }
  void WalkCourse(Simulation &simulation, sf::Vector2f target, bool *crossedBridge = nullptr) {
    auto &agent = World(simulation).agents.front();
    for (unsigned int frame = 0; frame < 900 && (target - agent.position).length() > 0.05f; ++frame) {
      const auto delta = target - agent.position;
      const auto direction = delta.normalized() * std::min(1.f, delta.length() / (GameplayConfig::WalkSpeed / 60.f));
      const float previous = agent.elevation;
      ActionCommand command; command.moveX = direction.x; command.moveY = direction.y;
      simulation.Step(1.f / 60.f, {{1, command}});
      if (crossedBridge && agent.supportId == 12) *crossedBridge = true;
      Require(agent.traversal.mode == TraversalMode::Grounded && std::abs(agent.elevation - previous) <= GameplayConfig::WalkSpeed / 60.f + 0.01f,
        "La escalera o el puente de prácticas teletransporta al agente entre plantas");
      Require(!WorldGeometry::PositionBlocked(simulation.GetState(), agent.position, GameplayConfig::AgentRadius, agent.elevation, WorldGeometry::ActorHeight(agent), agent.supportId),
        "El recorrido a pie de prácticas atraviesa una estructura");
    }
    Require((target - agent.position).length() <= 0.05f, "Los controles no alcanzan el destino a pie del circuito de prácticas");
  }
  void TrainingWallLedgeAndPipeArePlayableBothWays() {
    Simulation simulation; simulation.ResetBase(); auto &world = World(simulation); auto &agent = world.agents.front();
    Require(world.walkSurfaces.size() == TrainingConfig::TraversalCourse::Surfaces.size() && world.elevators.size() == 1,
      "El entrenamiento no construye las plantas y el ascensor de su circuito");
    for (const auto kind : {GripKind::Wall, GripKind::Ledge, GripKind::Pipe})
      Require(std::ranges::any_of(world.gripSurfaces, [kind](const GripSurface &grip) { return grip.kind == kind; }), "Falta un tipo de agarre practicable");
    agent.position = {340.f, 1366.f}; agent.elevation = 0.f; agent.supportId = 5;
    Require(Traversal::CanTraverse(world, agent) && !Traversal::Prompt(world, agent).empty(), "La pared de prácticas no ofrece la acción B desde el suelo");
    TraverseCourse(simulation);
    Require(agent.traversal.surfaceId == 41 && agent.elevation == 18.f && agent.supportId == 0,
      "La pared de prácticas no sostiene al agente tras pulsar B");
    ShimmyCourse(simulation, 500.f); TraverseCourse(simulation);
    Require(agent.traversal.surfaceId == 42 && agent.elevation == TrainingConfig::TraversalCourse::Storey,
      "El muro de prácticas no permite cambiar al saliente elevado");
    TraverseCourse(simulation);
    Require(agent.traversal.mode == TraversalMode::Grounded && agent.supportId == 10 && agent.position == sf::Vector2f{500.f, 1310.f},
      "El saliente de prácticas no permite subir a su plataforma real");
    TraverseCourse(simulation);
    Require(agent.traversal.surfaceId == 42, "La plataforma de prácticas no permite volver al saliente");
    ShimmyCourse(simulation, 600.f); TraverseCourse(simulation);
    Require(agent.traversal.surfaceId == 43 && agent.position == sf::Vector2f{600.f, 1370.f}, "El saliente de prácticas no conecta con el tubo");
    ShimmyCourse(simulation, 820.f); TraverseCourse(simulation);
    Require(agent.supportId == 11 && agent.elevation == TrainingConfig::TraversalCourse::Storey && agent.position == sf::Vector2f{840.f, 1370.f},
      "El extremo del tubo no permite subir a la segunda plataforma");
    TraverseCourse(simulation); ShimmyCourse(simulation, 600.f); TraverseCourse(simulation);
    Require(agent.traversal.surfaceId == 42, "El recorrido inverso del tubo no devuelve al saliente de prácticas");
    Require(Traversal::CanTraverse(world, agent, true), "El saliente de prácticas no ofrece un suelo válido al soltarse");
    TraverseCourse(simulation, true);
    Require(agent.traversal.mode == TraversalMode::Grounded && agent.supportId == 1 && agent.elevation == 0.f && agent.alive && !agent.incapacitated,
      "Mayús+B no deja al agente sobre el suelo protegido de entrenamiento");
  }
  void WaitForTrainingElevator(Simulation &simulation, float target, bool occupied) {
    auto &world = World(simulation); auto &agent = world.agents.front(); bool travelled = false;
    for (unsigned int frame = 0; frame < 600 && (world.elevators.front().phase != ElevatorPhase::Docked || world.elevators.front().requested); ++frame) {
      const float previous = world.elevators.front().elevation;
      simulation.Step(1.f / 60.f, {});
      const auto &lift = world.elevators.front();
      if (lift.elevation > 0.f && lift.elevation < TrainingConfig::TraversalCourse::Storey) travelled = true;
      Require(std::abs(lift.elevation - previous) <= LevelConfig::ElevatorSpeed / 60.f + 0.01f, "El ascensor de prácticas salta entre plantas");
      if (occupied) Require(agent.supportId == TrainingConfig::TraversalCourse::CabinSupport && agent.elevation == lift.elevation,
        "El ascensor de prácticas deja atrás a su ocupante");
      if (frame % 30 == 0) RequireTrainingReplica(simulation);
    }
    Require(travelled && world.elevators.front().phase == ElevatorPhase::Docked && !world.elevators.front().requested && world.elevators.front().elevation == target,
      "El ascensor de prácticas no completa el viaje y la apertura de puertas");
  }
  void TrainingStairsBridgeAndElevatorWorkWithControls() {
    Simulation simulation; simulation.ResetBase(); auto &world = World(simulation); auto &agent = world.agents.front();
    agent.position = {240.f, 1544.f}; agent.elevation = 0.f; agent.supportId = 5;
    WalkCourse(simulation, {240.f, 1328.f});
    Require(agent.supportId == 10 && agent.elevation == TrainingConfig::TraversalCourse::Storey, "La escalera de prácticas no alcanza la plataforma superior");
    RequireTrainingReplica(simulation);
    WalkCourse(simulation, {240.f, 1544.f});
    Require(agent.supportId == 5 && agent.elevation == 0.f, "La escalera de prácticas no permite regresar al suelo");
    WalkCourse(simulation, {240.f, 1250.f}); WalkCourse(simulation, {650.f, 1250.f});
    bool crossedBridge = false; WalkCourse(simulation, {840.f, 1250.f}, &crossedBridge);
    Require(crossedBridge && agent.supportId == 11 && agent.elevation == TrainingConfig::TraversalCourse::Storey,
      "El puente de prácticas no conecta las plataformas a la misma altura");
    WalkCourse(simulation, {840.f, 1400.f}); WalkCourse(simulation, TrainingConfig::TraversalCourse::ElevatorPanelPosition);
    Require(agent.supportId == 15, "El puente y la plataforma no permiten alcanzar el muelle superior del ascensor");
    ActionCommand use; use.interact = true; simulation.Step(1.f / 60.f, {{1, use}});
    Require(world.elevators.front().requested, "El panel superior de prácticas no llama a la cabina");
    WaitForTrainingElevator(simulation, TrainingConfig::TraversalCourse::Storey, false);
    WalkCourse(simulation, TrainingConfig::TraversalCourse::ElevatorPosition);
    Require(agent.supportId == TrainingConfig::TraversalCourse::CabinSupport, "El muelle superior no permite embarcar en la cabina");
    simulation.Step(1.f / 60.f, {{1, use}}); WaitForTrainingElevator(simulation, 0.f, true);
    WalkCourse(simulation, TrainingConfig::TraversalCourse::ElevatorPanelPosition);
    Require(agent.supportId == 5 && agent.elevation == 0.f, "El ascensor de prácticas no permite desembarcar en el suelo");
    WalkCourse(simulation, TrainingConfig::TraversalCourse::ElevatorPosition);
    simulation.Step(1.f / 60.f, {{1, use}}); WaitForTrainingElevator(simulation, TrainingConfig::TraversalCourse::Storey, true);
    WalkCourse(simulation, TrainingConfig::TraversalCourse::ElevatorPanelPosition);
    Require(agent.supportId == 15 && agent.elevation == TrainingConfig::TraversalCourse::Storey, "La vuelta en ascensor no recupera el muelle superior");
    RequireTrainingReplica(simulation);
  }
  void TrainingCourseSurvivesJoiningReconnectingAndDeployment() {
    Simulation simulation; simulation.ResetBase();
    const auto original = simulation.SnapshotFor(1);
    Require(simulation.AddPlayer(2, "Dos"), "El circuito impide incorporar a otro jugador en la base");
    RequireTrainingReplica(simulation, 2); RequireTrainingReplica(simulation, 2, true);
    simulation.DisconnectPlayer(2);
    Require(simulation.AddPlayer(2, "Dos de nuevo"), "El circuito impide reconectar a un jugador en la base");
    RequireTrainingReplica(simulation, 1); RequireTrainingReplica(simulation, 1, true);
    RequireTrainingReplica(simulation, 2); RequireTrainingReplica(simulation, 2, true);
    for (const auto &agent : simulation.GetState().agents)
      Require(agent.supportId == TrainingConfig::TraversalCourse::GroundSupport && agent.elevation == 0.f,
        "Un jugador incorporado o reconectado nace sin el suelo de entrenamiento");
    simulation.Deploy(23);
    Require(!IsTraining(simulation.GetState()) && simulation.GetState().phase == GamePhase::Mission && simulation.GetState().size != TrainingConfig::WorldSize,
      "El despliegue conserva el circuito o las dimensiones de la sala de entrenamiento");
    simulation.ReturnToBase();
    const auto restored = simulation.SnapshotFor(1);
    for (const auto key : {"surfaces", "grips", "verticalLinks", "elevators", "geometryHash"})
      Require(restored.at(key) == original.at(key), "Regresar a la base no reconstruye las plantas y conexiones originales del circuito");
    RequireTrainingReplica(simulation, 1); RequireTrainingReplica(simulation, 2, true);
  }
  void MissionsRecoverNormalRules() {
    Simulation discreet; discreet.ResetBase(); discreet.SetLoadout(1, {0, 4, 2, 1}); Advance(discreet, 0.5f); discreet.Deploy(11);
    Require(discreet.GetState().agents[0].loadout.lethalAmmo == 0 && discreet.GetState().agents[0].lethalMagazine == 0 &&
      discreet.GetState().agents[0].loadout.nonLethalAmmo == 4 && discreet.GetState().agents[0].loadout.decoys == 2,
      "El reabastecimiento de prácticas introduce munición letal en un equipo de misión sin ella");
    Simulation simulation; simulation.ResetBase({{1, "Uno"}, {2, "Dos"}}); simulation.SetContract(ContractId::DoubleCross); simulation.SetLoadout(1, {2, 1, 1, 1});
    Advance(simulation, 1.f);
    Require(simulation.GetState().agents[0].loadout.lethalAmmo == TrainingConfig::RefilledAmmo, "La práctica no permite reponer suministros");
    simulation.Deploy(23); auto &world = World(simulation);
    Require(world.phase == GamePhase::Mission && world.mode == SimulationMode::Operation && !IsTraining(world) && world.size == sf::Vector2f{LevelConfig::Width, LevelConfig::Height},
      "El despliegue conserva el modo o las dimensiones ampliadas de entrenamiento");
    Require(!world.guards.empty() && std::ranges::none_of(world.guards, [](const Guard &guard) { return guard.trainingDummy; }) && !world.securityEntries.empty(),
      "La misión no restaura enemigos y accesos de refuerzos normales");
    Require(world.agents[0].loadout.lethalAmmo == 2 && world.agents[0].loadout.nonLethalAmmo == 1 && world.agents[0].loadout.decoys == 1,
      "Los suministros infinitos sobrescriben el equipo elegido para la misión");
    world.objects.clear(); world.obstacles.clear(); world.guards.clear(); world.securityEntries.clear();
    TestFixture::LegacyPlane(world);
    for (auto &agent : world.agents) agent.privateOrder = PrivateOrder::CopyArchive;
    world.agents[0].position = {200.f, 300.f}; world.agents[1].position = {250.f, 300.f};
    Shoot(simulation, true, 50.f);
    Require(world.agents[1].alive && world.agents[1].incapacitated && world.agents[1].health == 0.f && world.agents[1].incapacityRemaining > 0.f, "La protección de entrenamiento persiste ante un disparo letal en una misión");
    Advance(simulation, GameplayConfig::IncapacityDuration);
    Require(!world.agents[1].alive && !world.agents[1].incapacitated && world.agents[1].lifeState == LifeState::Dead && world.phase == GamePhase::Mission, "El plazo de rescate no confirma la muerte individual o finaliza la misión del superviviente");
    simulation.ReturnToBase();
    Require(IsTraining(simulation.GetState()) && simulation.GetState().guards.size() == TrainingConfig::DummyCount,
      "Regresar de una misión no recupera la sala segura y sus objetivos de prácticas");
  }
  void TrainingSnapshotsStayAuthoritative() {
    Simulation simulation, replica; simulation.ResetBase();
    World(simulation).agents[0].position = {1100.f, 210.f};
    simulation.Step(0.1f, {});
    std::string error;
    Require(replica.LoadSnapshot(simulation.SnapshotFor(1), error), "El estado completo no conserva las reglas de entrenamiento");
    Require(IsTraining(replica.GetState()) && replica.GetState().guards.size() == TrainingConfig::DummyCount && replica.GetState().guards[0].trainingDummy,
      "La réplica convierte muñecos de entrenamiento en enemigos normales");
    Require(replica.LoadSnapshot(simulation.SnapshotFor(1, true), error) && !replica.GetState().guards.empty() && replica.GetState().guards[0].trainingDummy,
      "La instantánea filtrada pierde las propiedades de los muñecos percibidos");
    auto invalid = simulation.SnapshotFor(1); invalid["mode"] = static_cast<int>(SimulationMode::Operation);
    Require(!replica.LoadSnapshot(invalid, error), "El cliente admite muñecos protegidos fuera del modo de entrenamiento");
  }
}

int main() {
  try {
    LobbyHasAnArenaWithoutEnemyGuards(); PlayerProtectionAndRealReload(); DummiesSurviveWeaponsAndSubdual(); CamerasTrapsAndPuzzlesStayActive();
    HideoutsWorkInTheLobby(); DummiesAllowSafeDragging(); MissionsRecoverNormalRules(); TrainingSnapshotsStayAuthoritative();
    TrainingWallLedgeAndPipeArePlayableBothWays(); TrainingStairsBridgeAndElevatorWorkWithControls(); TrainingCourseSurvivesJoiningReconnectingAndDeployment(); ElevatedTrainingDummiesCanBePushedRearmedAndPushedAgain();
    std::cout << "PASS entrenamiento: " << checks << " comprobaciones\n"; return 0;
  } catch (const std::exception &error) { std::cerr << "FAIL entrenamiento tras " << checks << ": " << error.what() << '\n'; return 1; }
}
