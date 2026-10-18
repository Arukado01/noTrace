#include "NoTraceSimulation.h"
#include "NoTraceWorldGeometry.h"
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
    for (const auto &dummy : world.guards) Require(dummy.position.y == 210.f && dummy.navigation.empty(), "Un muñeco ejecuta navegación o persecución enemiga");
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
    Require(!world.agents[0].hidden && world.agents[0].position.y < 850.f && !WorldGeometry::PositionBlocked(world, world.agents[0].position, GameplayConfig::AgentRadius),
      "La salida de la mesa de prácticas ignora la orientación WASD o termina en un sólido");
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
  void MissionsRecoverNormalRules() {
    Simulation discreet; discreet.ResetBase(); discreet.SetLoadout(1, {0, 4, 2, 1}); Advance(discreet, 0.5f); discreet.Deploy(11);
    Require(discreet.GetState().agents[0].loadout.lethalAmmo == 0 && discreet.GetState().agents[0].lethalMagazine == 0 &&
      discreet.GetState().agents[0].loadout.nonLethalAmmo == 4 && discreet.GetState().agents[0].loadout.decoys == 2,
      "El reabastecimiento de prácticas introduce munición letal en un equipo de misión sin ella");
    Simulation simulation; simulation.ResetBase({{1, "Uno"}, {2, "Dos"}}); simulation.SetContract(ContractId::DoubleCross); simulation.SetLoadout(1, {2, 1, 1, 1});
    Advance(simulation, 1.f);
    Require(simulation.GetState().agents[0].loadout.lethalAmmo == TrainingConfig::RefilledAmmo, "La práctica no permite reponer suministros");
    simulation.Deploy(23); auto &world = World(simulation);
    Require(world.phase == GamePhase::Mission && world.mode == SimulationMode::Operation && !IsTraining(world) && world.size == sf::Vector2f{1000.f, 760.f},
      "El despliegue conserva el modo o las dimensiones ampliadas de entrenamiento");
    Require(!world.guards.empty() && std::ranges::none_of(world.guards, [](const Guard &guard) { return guard.trainingDummy; }) && !world.securityEntries.empty(),
      "La misión no restaura enemigos y accesos de refuerzos normales");
    Require(world.agents[0].loadout.lethalAmmo == 2 && world.agents[0].loadout.nonLethalAmmo == 1 && world.agents[0].loadout.decoys == 1,
      "Los suministros infinitos sobrescriben el equipo elegido para la misión");
    world.objects.clear(); world.obstacles.clear(); world.guards.clear(); world.securityEntries.clear();
    world.agents[0].position = {200.f, 300.f}; world.agents[1].position = {250.f, 300.f};
    Shoot(simulation, true, 50.f);
    Require(!world.agents[1].alive && !world.agents[1].incapacitated, "La protección de entrenamiento persiste ante un disparo letal en una misión");
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
    std::cout << "PASS entrenamiento: " << checks << " comprobaciones\n"; return 0;
  } catch (const std::exception &error) { std::cerr << "FAIL entrenamiento tras " << checks << ": " << error.what() << '\n'; return 1; }
}
