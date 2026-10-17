#include "NoTraceSimulation.h"
#include "Config/NoTraceDevicesConfig.h"
#include "NoTraceWorldGeometry.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
  using namespace NoTrace;
  int checks = 0;
  void Require(bool condition, const char *message) { ++checks; if (!condition) throw std::runtime_error(message); }
  WorldState &Fixture(Simulation &simulation, bool cooperative = false) {
    simulation.ResetBase(cooperative ? std::vector<PlayerProfile>{{1, "Uno"}, {2, "Dos"}} : std::vector<PlayerProfile>{{1, "Uno"}});
    simulation.Deploy(17);
    auto &world = const_cast<WorldState &>(simulation.GetState());
    world.guards.clear(); world.objects.clear(); world.obstacles.clear(); world.securityEntries.clear(); world.events.clear(); world.sounds.clear(); world.noises.clear();
    world.objects.reserve(12);
    for (auto &agent : world.agents) { agent.privateOrder = PrivateOrder::None; agent.position = {200.f, 300.f}; agent.facing = {1.f, 0.f}; }
    return world;
  }
  void Advance(Simulation &simulation, float seconds) {
    for (int step = 0; step < static_cast<int>(std::ceil(seconds / 0.1f)); ++step) simulation.Step(0.1f, {});
  }
  Interactable &Device(WorldState &world, ObjectKind kind, PuzzleKind puzzle, sf::Vector2f position = {240.f, 300.f}) {
    Interactable object{static_cast<std::uint32_t>(80 + world.objects.size()), kind, position, "Prueba de dispositivo"};
    object.puzzle.kind = puzzle; object.puzzle.difficulty = 3; object.footprint = {24.f, 24.f};
    if (kind == ObjectKind::Explosive || kind == ObjectKind::Camera) { object.solid = SpatialOverride::Disabled; object.blocksVision = SpatialOverride::Disabled; }
    world.objects.push_back(object); return world.objects.back();
  }
  void InteractWith(Simulation &simulation, PlayerId player = 1) { ActionCommand command; command.interact = true; simulation.Step(0.1f, {{player, command}}); }
  ActionCommand Choice(const Agent &agent, int value = -1) {
    ActionCommand command; command.puzzleTarget = agent.puzzle.target;
    if (value >= 0) command.puzzleInput = value;
    else if (agent.puzzle.family == PuzzleFamily::Arrows) command.puzzleInput = agent.puzzle.sequence.at(agent.puzzle.stage);
    else if (agent.puzzle.family == PuzzleFamily::Wires) {
      for (unsigned int source = 1; source <= 4; ++source) if (!(agent.puzzle.connectedSources & (1u << (source - 1)))) { command.puzzleInput = WireInput(source, MatchingWirePort(agent.puzzle, source)); break; }
    }
    return command;
  }
  int WrongChoice(const Agent &agent) {
    if (agent.puzzle.family == PuzzleFamily::Arrows) return agent.puzzle.sequence.at(agent.puzzle.stage) % 4 + 1;
    return WireInput(1, MatchingWirePort(agent.puzzle, 1) % 4 + 1);
  }
  void Solve(Simulation &simulation, PlayerId player = 1) {
    for (unsigned int stage = 0; stage < DevicesConfig::PuzzleMaximumStages && simulation.GetState().agents[player - 1].puzzle.target != 0; ++stage)
      simulation.Step(0.1f, {{player, Choice(simulation.GetState().agents[player - 1])}});
  }
  void CameraSweepAndPerception() {
    Simulation simulation; auto &world = Fixture(simulation);
    auto &camera = Device(world, ObjectKind::Camera, PuzzleKind::None, {400.f, 300.f});
    camera.facing = {-1.f, 0.f}; camera.camera.sweeping = true; camera.camera.baseAngle = 3.14159265f;
    camera.camera.minAngle = -0.1f; camera.camera.maxAngle = 0.1f; camera.camera.speed = 1.f;
    world.agents[0].position = {50.f, 700.f};
    simulation.Step(0.1f, {});
    Require(camera.camera.direction == -1 && camera.camera.pauseRemaining > 0.f, "El barrido no invierte su movimiento ni espera en el límite configurado");
    Require(std::abs(camera.facing.y) > 0.05f && camera.facing.x < -0.9f, "El barrido solo modifica el dibujo y no la orientación de percepción");
    const auto angle = camera.camera.sweepAngle;
    simulation.Step(0.1f, {});
    Require(camera.camera.sweepAngle == angle, "La cámara no respeta la pausa de su barrido");
    camera.camera.sweeping = false; camera.facing = {-1.f, 0.f};
    world.agents[0].position = {200.f, 300.f};
    simulation.Step(0.1f, {});
    Require(camera.progress > 0.f && camera.progress < camera.camera.confirmationTime && world.agents[0].detectionProgress > 0.f, "Entrar en el campo de la cámara no produce sospecha progresiva");
    Advance(simulation, 0.8f);
    Require(world.alarm && world.securityActive && world.agents[0].detectionConfirmed, "La cámara confirmada no activa alarma y respuesta de seguridad");
    Require(std::ranges::any_of(world.events, [](const MissionEvent &event) { return event.kind == EventKind::ConfirmedDetection && event.target == 1080; }), "La cámara no registra su detección autoritativa");

    auto &blocked = Fixture(simulation);
    auto &occluded = Device(blocked, ObjectKind::Camera, PuzzleKind::None, {400.f, 300.f}); occluded.facing = {-1.f, 0.f};
    blocked.obstacles.push_back({{{290.f, 200.f}, {20.f, 200.f}}, CoverKind::High, true, "Pared"});
    Advance(simulation, 1.2f);
    Require(occluded.progress == 0.f && !blocked.alarm, "La cámara detecta a través de una pared sólida");
    blocked.obstacles.clear(); blocked.agents[0].stance = Stance::Prone;
    Advance(simulation, 1.f);
    Require(occluded.progress == 0.f, "La postura tumbada no reduce el alcance real de la cámara");
    blocked.agents[0].stance = Stance::Standing; blocked.agents[0].position = {400.f, 500.f};
    Advance(simulation, 1.f);
    Require(occluded.progress == 0.f, "La cámara detecta fuera del ángulo configurado");
    blocked.agents[0].position = {420.f, 300.f}; Advance(simulation, 0.6f);
    Require(occluded.progress == 0.f && !blocked.alarm, "La cámara detecta a veinte píxeles por detrás fuera de su campo visual");
    blocked.agents[0].position = {200.f, 300.f}; occluded.camera.state = CameraState::Broken;
    Advance(simulation, 1.f);
    Require(occluded.progress == 0.f, "Una cámara averiada continúa detectando");
  }
  void CameraTrackingAndMemory() {
    Simulation simulation; auto &world = Fixture(simulation, true);
    auto &camera = Device(world, ObjectKind::Camera, PuzzleKind::None, {400.f, 300.f}); camera.facing = {-1.f, 0.f};
    world.agents[1].position = {50.f, 700.f}; simulation.Step(0.1f, {});
    Require(camera.camera.trackedAgent == 1 && camera.camera.perception == CameraPerception::Suspicion, "La cámara no conserva al objetivo que inicia la sospecha");
    world.agents[1].position = {300.f, 310.f}; world.agents[0].position = {200.f, 325.f}; simulation.Step(0.1f, {});
    Require(camera.camera.trackedAgent == 1 && camera.camera.lastKnown == world.agents[0].position && camera.facing.y > 0.01f,
      "La cámara salta al agente más cercano o no gira físicamente siguiendo al objetivo");
    Require(camera.camera.confirmationTime < GameplayConfig::ConfirmationTime, "La cámara no confirma antes que un guardia");
    Advance(simulation, 0.2f);
    Require(camera.camera.perception == CameraPerception::Tracking && camera.camera.confirmedRemaining > 4.9f && camera.camera.indicatorProgress == 1.f,
      "La confirmación no tiene un estado y una duración visibles propios");
    const auto known = camera.camera.lastKnown;
    for (auto &agent : world.agents) agent.position = {50.f, 700.f}; simulation.Step(0.1f, {});
    Require(camera.camera.trackedAgent == 0 && camera.camera.perception == CameraPerception::Searching && camera.camera.lastKnown == known,
      "Perder la visión no inicia una búsqueda o actualiza la posición oculta");
    Advance(simulation, 3.2f);
    Require(camera.camera.perception == CameraPerception::Sweeping && camera.camera.confirmedRemaining > 1.f && camera.camera.indicatorProgress == 1.f,
      "El ojo confirmado desaparece al finalizar la búsqueda antes de sus cinco segundos");
    Advance(simulation, 2.f);
    Require(camera.camera.confirmedRemaining == 0.f && camera.camera.indicatorProgress == 0.f && camera.observedAgent == 0,
      "Una cámara sin nuevo contacto conserva una confirmación indefinida");
    world.agents[0].position = {200.f, 325.f}; camera.facing = {-1.f, 0.f}; simulation.Step(0.1f, {});
    world.agents[0].position = {50.f, 700.f}; simulation.Step(0.1f, {});
    Require(camera.camera.suspicionRemaining > 1.3f && camera.camera.indicatorProgress > 0.f, "La sospecha desaparece en cuanto se pierde visión");
    camera.camera.state = CameraState::Disabled; camera.active = false; simulation.Step(0.1f, {});
    Require(camera.camera.indicatorProgress == 0.f && camera.camera.trackedAgent == 0 && camera.camera.searchRemaining == 0.f,
      "Una cámara apagada conserva un ojo o una persecución activos");
  }
  void DoorPuzzleLifecycle() {
    Simulation simulation; auto &world = Fixture(simulation, true);
    auto &door = Device(world, ObjectKind::Door, PuzzleKind::Door); door.locked = true;
    InteractWith(simulation);
    Require(world.agents[0].puzzle.target == door.id && door.user == 1 && door.puzzle.state == PuzzleState::InUse, "No se abre una sesión autoritativa de cerradura");
    InteractWith(simulation, 2);
    Require(world.agents[1].puzzle.target == 0 && door.user == 1, "Dos agentes pueden resolver el mismo dispositivo simultáneamente");
    const auto position = world.agents[0].position;
    ActionCommand forbidden; forbidden.moveX = 1.f; forbidden.fireLethal = true;
    simulation.Step(0.1f, {{1, forbidden}});
    Require(world.agents[0].position == position && world.shots.empty(), "Una sesión de puzle permite desplazarse o disparar");
    auto wrongTarget = Choice(world.agents[0]); ++wrongTarget.puzzleTarget;
    simulation.Step(0.1f, {{1, wrongTarget}});
    Require(world.agents[0].puzzle.stage == 0, "Una elección dirigida a otro objeto avanza el puzle");
    simulation.Step(0.1f, {{1, Choice(world.agents[0], 0)}});
    Require(world.agents[0].puzzle.target == 0 && door.user == 0 && door.locked && !door.open, "Cancelar la cerradura la abre o deja un usuario bloqueado");
    InteractWith(simulation);
    const int invalid = WrongChoice(world.agents[0]);
    simulation.Step(0.1f, {{1, Choice(world.agents[0], invalid)}});
    Require(door.locked && !door.open && door.puzzle.state == PuzzleState::Failed && door.puzzle.cooldown > 0.f, "Fallar una cerradura desbloquea el paso");
    InteractWith(simulation);
    Require(world.agents[0].puzzle.target == 0, "La cerradura ignora su bloqueo temporal");
    Advance(simulation, DevicesConfig::PuzzleFailureCooldown);
    InteractWith(simulation); Solve(simulation);
    Require(!door.locked && door.open && door.completed && door.puzzle.state == PuzzleState::Solved, "El éxito del puzle no cambia la colisión real de la puerta");
    Require(!WorldGeometry::BlocksMovement(door) && world.agents[0].puzzle.target == 0, "La puerta resuelta sigue sólida o conserva la interfaz abierta");
    InteractWith(simulation);
    Require(!door.open && !door.locked, "Una puerta desbloqueada no conserva su interacción normal");
  }
  void ComputerAndCameraPanel() {
    Simulation simulation; auto &world = Fixture(simulation);
    auto &computer = Device(world, ObjectKind::Computer, PuzzleKind::Computer);
    computer.puzzle.family = PuzzleFamily::Arrows;
    computer.puzzle.intelligence = "INTELIGENCIA VERIFICADA: acceso norte.";
    InteractWith(simulation); Solve(simulation);
    Require(computer.completed && world.secondary == 1 && world.message == computer.puzzle.intelligence, "Descifrar un computador no revela inteligencia ni actualiza el objetivo secundario real");
    InteractWith(simulation);
    Require(world.secondary == 1 && world.agents[0].puzzle.target == 0, "Reabrir el computador duplica la recompensa");

    auto &panelWorld = Fixture(simulation);
    auto &panel = Device(panelWorld, ObjectKind::CameraPanel, PuzzleKind::CameraPanel);
    auto &camera = Device(panelWorld, ObjectKind::Camera, PuzzleKind::None, {400.f, 300.f}); camera.facing = {1.f, 0.f}; camera.camera.controlGroup = 1;
    auto &otherGroup = Device(panelWorld, ObjectKind::Camera, PuzzleKind::None, {900.f, 700.f}); otherGroup.camera.controlGroup = 2;
    InteractWith(simulation); Solve(simulation);
    Require(!camera.active && camera.camera.state == CameraState::Disabled && camera.camera.reactivationRemaining > 0.f, "El panel no desactiva el estado real de vigilancia");
    Require(otherGroup.active && otherGroup.camera.state == CameraState::Operating, "El panel desactiva cámaras ajenas a su grupo");
    Advance(simulation, 1.f);
    Require(camera.progress == 0.f && !panelWorld.alarm, "Una cámara desactivada mantiene su percepción");
    panelWorld.agents[0].position = {50.f, 700.f};
    Advance(simulation, DevicesConfig::CameraPanelDisabledDuration);
    Require(camera.active && camera.camera.state == CameraState::Operating && panel.puzzle.state == PuzzleState::Ready, "Las cámaras o su panel no recuperan el estado reactivable configurado");
  }
  void BrokenCameraSurvivesCentralAndPanelReset() {
    Simulation simulation; auto &world = Fixture(simulation);
    auto &broken = Device(world, ObjectKind::Camera, PuzzleKind::None); broken.facing = {1.f, 0.f};
    auto &central = Device(world, ObjectKind::AlarmPanel, PuzzleKind::None, {400.f, 300.f});
    auto &panel = Device(world, ObjectKind::CameraPanel, PuzzleKind::CameraPanel, {600.f, 300.f});
    auto &working = Device(world, ObjectKind::Camera, PuzzleKind::None, {900.f, 700.f}); working.facing = {1.f, 0.f};
    ActionCommand tool; tool.tool = true;
    simulation.Step(0.1f, {{1, tool}});
    Require(broken.camera.state == CameraState::Broken && !broken.active && broken.camera.reactivationRemaining == 0.f, "La herramienta no crea una avería permanente en la cámara");
    world.agents[0].position = {360.f, 300.f}; simulation.Step(0.1f, {{1, tool}});
    Require(!central.active && broken.camera.state == CameraState::Broken && !broken.active && working.camera.state == CameraState::Disabled, "Sabotear la central convierte una cámara averiada en un dispositivo reactivable");
    world.agents[0].position = {560.f, 300.f}; InteractWith(simulation);
    Require(world.agents[0].puzzle.family == PuzzleFamily::Wires && world.agents[0].puzzle.target == panel.id, "La regresión no inicia la intervención real del panel de cables");
    Solve(simulation);
    Require(panel.puzzle.state == PuzzleState::Solved && working.camera.reactivationRemaining > 0.f && broken.camera.state == CameraState::Broken && broken.camera.reactivationRemaining == 0.f, "Resolver el panel programa la reactivación de la cámara averiada");
    world.agents[0].position = {50.f, 700.f}; Advance(simulation, DevicesConfig::CameraPanelDisabledDuration + 0.2f);
    Require(working.active && working.camera.state == CameraState::Operating && !broken.active && broken.camera.state == CameraState::Broken && broken.camera.reactivationRemaining == 0.f, "El vencimiento del panel repara una cámara destruida o impide recuperar la cámara funcional");
  }
  void TrapLifecycleAndDamage() {
    Simulation simulation; auto &world = Fixture(simulation, true);
    auto &trap = Device(world, ObjectKind::Explosive, PuzzleKind::Explosive, {350.f, 300.f});
    world.agents[1].position = {50.f, 700.f};
    simulation.Step(0.1f, {});
    Require(trap.trap.state == TrapState::Detected && trap.trap.remaining < 0.f, "La aproximación audible detona o no descubre la trampa");
    Require(std::ranges::any_of(world.events, [](const MissionEvent &event) { return event.kind == EventKind::TrapTick; }), "La trampa cercana no genera eventos de temporizador");
    world.agents[0].position = {310.f, 300.f}; simulation.Step(0.1f, {});
    Require(trap.trap.remaining > 0.f && trap.trap.remaining < trap.trap.fuse, "La distancia real no activa la mecha del explosivo");
    InteractWith(simulation); simulation.Step(0.1f, {{1, Choice(world.agents[0], 0)}});
    Require(trap.trap.state == TrapState::Detected && trap.active && trap.trap.remaining > 0.f, "Cancelar el puzle desactiva el explosivo");
    InteractWith(simulation); Solve(simulation);
    Require(trap.trap.state == TrapState::Disarmed && !trap.active && trap.trap.remaining < 0.f, "El éxito no aísla el detonador real");
    Advance(simulation, 6.f);
    Require(world.agents[0].health == GameplayConfig::MaxHealth && !world.alarm, "Un explosivo desarmado sigue dañando o activando alarmas");

    auto &blast = Fixture(simulation, true);
    auto &explosive = Device(blast, ObjectKind::Explosive, PuzzleKind::Explosive, {300.f, 300.f}); explosive.trap.fuse = 0.2f;
    blast.agents[0].position = {275.f, 300.f}; blast.agents[1].position = {350.f, 300.f};
    blast.obstacles.push_back({{{320.f, 260.f}, {10.f, 80.f}}, CoverKind::High, true, "Muro contra onda"});
    Guard guard; guard.id = 8; guard.position = {302.f, 300.f}; guard.heldBy = 1; guard.cqcState = CqcState::Grabbed; blast.guards.push_back(guard);
    blast.agents[0].cqcHoldingGuard = 8;
    Advance(simulation, 0.3f);
    Require(explosive.trap.state == TrapState::Detonated && !explosive.active && explosive.trap.effectRemaining > 0.f, "La mecha no produce una detonación con efecto temporal");
    Require(blast.agents[0].health < GameplayConfig::MaxHealth && blast.agents[1].health == GameplayConfig::MaxHealth, "La explosión no daña en su radio o atraviesa una pared sólida");
    Require(blast.guards[0].state == GuardState::Dead, "La explosión no aplica daño real a los guardias");
    Require(blast.agents[0].cqcHoldingGuard == 0 && blast.guards[0].heldBy == 0, "La explosión conserva una sujeción a un guardia neutralizado");
    Require(blast.alarm && blast.securityActive && std::ranges::any_of(blast.events, [](const MissionEvent &event) { return event.kind == EventKind::Explosion; }), "La explosión no comunica sus consecuencias al sistema de seguridad");
    const auto count = std::ranges::count_if(blast.events, [](const MissionEvent &event) { return event.kind == EventKind::Explosion; });
    Advance(simulation, 0.3f);
    Require(std::ranges::count_if(blast.events, [](const MissionEvent &event) { return event.kind == EventKind::Explosion; }) == count, "Un explosivo agotado detona repetidamente");

    auto &incapacitated = Fixture(simulation, true);
    for (auto &agent : incapacitated.agents) agent.position = {50.f, 700.f};
    auto &weakTrap = Device(incapacitated, ObjectKind::Explosive, PuzzleKind::Explosive, {300.f, 300.f});
    weakTrap.trap.damage = 10.f; weakTrap.trap.remaining = 0.1f;
    Guard body; body.id = 9; body.position = {302.f, 300.f}; body.state = GuardState::Incapacitated; body.incapacityRemaining = 60.f;
    incapacitated.guards.push_back(body);
    Advance(simulation, 0.2f);
    Require(incapacitated.guards[0].health < CombatConfig::MaxGuardHealth && incapacitated.guards[0].health > 0.f,
      "Una onda débil no aplica daño parcial al cuerpo inconsciente");
    Require(incapacitated.guards[0].state == GuardState::Incapacitated && incapacitated.guards[0].incapacityRemaining > 59.f,
      "Una explosión no letal despierta al guardia sin una transición de recuperación");
  }
  void PuzzleFailureAndInvalidation() {
    Simulation simulation; auto &world = Fixture(simulation, true);
    auto &trap = Device(world, ObjectKind::Explosive, PuzzleKind::Explosive);
    world.agents[1].position = {50.f, 700.f};
    InteractWith(simulation);
    const int invalid = WrongChoice(world.agents[0]);
    simulation.Step(0.1f, {{1, Choice(world.agents[0], invalid)}});
    Require(trap.trap.state == TrapState::Detonated && world.agents[0].puzzle.target == 0 && trap.user == 0, "Fallar el desarme no detona o deja la sesión abierta");

    auto &moved = Fixture(simulation);
    auto &door = Device(moved, ObjectKind::Door, PuzzleKind::Door); door.locked = true;
    InteractWith(simulation); moved.agents[0].position = {50.f, 700.f}; simulation.Step(0.1f, {});
    Require(moved.agents[0].puzzle.target == 0 && door.user == 0 && door.locked, "Alejarse conserva un usuario o desbloquea la puerta");
    moved.agents[0].position = {200.f, 300.f}; InteractWith(simulation); simulation.DisconnectPlayer(1);
    Require(moved.agents[0].puzzle.target == 0 && door.user == 0 && door.locked, "Desconectarse deja bloqueado el dispositivo para el equipo");
  }
  void WiresRequireActualPairs() {
    Simulation simulation; auto &world = Fixture(simulation);
    auto &panel = Device(world, ObjectKind::CameraPanel, PuzzleKind::CameraPanel); panel.puzzle.wireCrossingAllowed = false;
    InteractWith(simulation);
    Require(world.agents[0].puzzle.family == PuzzleFamily::Wires && world.agents[0].puzzle.wireLabels == std::array<std::uint8_t, 4>{1, 2, 3, 4},
      "El cableado no respeta la configuración sin cruces");
    simulation.Step(0.1f, {{1, Choice(world.agents[0], WireInput(4, 4))}});
    Require(world.agents[0].puzzle.stage == 1 && world.agents[0].puzzle.connectedSources == 8 && world.agents[0].puzzle.connectedPorts == 8,
      "El host no valida una conexión de cable y puerto en el extremo 16 del protocolo");
    simulation.Step(0.1f, {{1, Choice(world.agents[0], WireInput(4, 4))}});
    Require(panel.puzzle.state == PuzzleState::Failed && world.agents[0].puzzle.target == 0 && panel.active,
      "Conectar dos veces el mismo cable permite resolver el circuito");
    Advance(simulation, DevicesConfig::PuzzleFailureCooldown); InteractWith(simulation); Solve(simulation);
    Require(panel.completed && panel.puzzle.state == PuzzleState::Solved, "El cableado no permite reintentar después de su consecuencia temporal");
  }
  void DistributedLeversNeedPhysicalInteraction() {
    Simulation simulation; auto &world = Fixture(simulation);
    auto &computer = Device(world, ObjectKind::Computer, PuzzleKind::Computer);
    computer.puzzle.leverOrder = {2, 4, 1, 3}; computer.puzzle.intelligence = "Inteligencia por circuito distribuido";
    const auto parent = computer.id;
    for (unsigned int index = 0; index < 4; ++index) {
      auto &node = Device(world, ObjectKind::PuzzleNode, PuzzleKind::Computer, {360.f + index * 100.f, 450.f});
      node.puzzle.parentDevice = parent; node.puzzle.nodeIndex = index + 1; node.solid = SpatialOverride::Disabled; node.blocksVision = SpatialOverride::Disabled;
    }
    InteractWith(simulation);
    Require(world.agents[0].puzzle.family == PuzzleFamily::Levers && world.agents[0].puzzle.sequence == computer.puzzle.leverOrder,
      "El panel no comunica la secuencia real de sus palancas");
    simulation.Step(0.1f, {{1, Choice(world.agents[0], 2)}});
    Require(computer.puzzle.sequenceProgress == 0 && !computer.completed, "Una respuesta remota acciona una palanca física");
    simulation.Step(0.1f, {{1, Choice(world.agents[0], 0)}});
    world.agents[0].position = world.objects[1].position - sf::Vector2f{30.f, 0.f}; InteractWith(simulation);
    Require(computer.puzzle.state == PuzzleState::Failed && computer.puzzle.sequenceProgress == 0 && !world.objects[1].open,
      "Un orden incorrecto no reinicia ni bloquea el circuito distribuido");
    Advance(simulation, DevicesConfig::PuzzleFailureCooldown);
    for (const auto index : computer.puzzle.leverOrder) {
      auto &node = world.objects[index]; world.agents[0].position = node.position - sf::Vector2f{30.f, 0.f}; InteractWith(simulation);
    }
    Require(computer.completed && computer.puzzle.state == PuzzleState::Solved && world.secondary == 1 && computer.puzzle.sequenceProgress == 4,
      "Las cuatro acciones físicas correctas no completan el computador");
  }
  void CoordinationNeedsConcurrentHolds() {
    Simulation simulation; auto &world = Fixture(simulation, true);
    auto &relay = Device(world, ObjectKind::CoopSwitch, PuzzleKind::Coordination);
    InteractWith(simulation);
    ActionCommand first; first.puzzleTarget = relay.id; first.puzzleHeld = true;
    for (int frame = 0; frame < 22; ++frame) simulation.Step(0.1f, {{1, first}});
    Require(!relay.completed && relay.puzzle.coordinationProgress == 0.f, "Un único agente elude la coordinación cuando hay dos participantes disponibles");
    ActionCommand join; join.interact = true; simulation.Step(0.1f, {{1, first}, {2, join}});
    Require(world.agents[1].puzzle.target == relay.id && world.agents[0].puzzle.target == relay.id, "La coordinación conserva la exclusividad de un puzle individual");
    ActionCommand second = first;
    for (int frame = 0; frame < 8; ++frame) simulation.Step(0.1f, {{1, first}, {2, second}});
    Require(relay.puzzle.coordinationProgress > 0.7f && world.agents[1].puzzle.coordinationProgress > 0.7f, "El progreso concurrente no se comparte entre participantes");
    simulation.Step(0.1f, {{1, first}});
    Require(relay.puzzle.coordinationProgress == 0.f && !relay.completed, "Soltar un control conserva progreso simultáneo acumulado");
    auto cancel = second; cancel.puzzleInput = 0; simulation.Step(0.1f, {{1, first}, {2, cancel}});
    Require(world.agents[1].puzzle.target == 0 && relay.active, "Cancelar un participante resuelve o bloquea el dispositivo");
    simulation.Step(0.1f, {{1, first}, {2, join}});
    for (int frame = 0; frame < 21; ++frame) simulation.Step(0.1f, {{1, first}, {2, second}});
    Require(relay.completed && !relay.active && world.agents[0].puzzle.target == 0 && world.agents[1].puzzle.target == 0,
      "La coordinación real no aplica su efecto ni libera ambas interfaces");
    auto &solo = Fixture(simulation); auto &soloRelay = Device(solo, ObjectKind::CoopSwitch, PuzzleKind::Coordination);
    InteractWith(simulation); first.puzzleTarget = soloRelay.id;
    for (int frame = 0; frame < 21; ++frame) simulation.Step(0.1f, {{1, first}});
    Require(soloRelay.completed, "El relé auxiliar no permite resolver la familia de coordinación sin conexión");
    auto &disabled = Fixture(simulation, true); auto &disabledRelay = Device(disabled, ObjectKind::CoopSwitch, PuzzleKind::Coordination);
    ActionCommand joinBoth; joinBoth.interact = true; simulation.Step(0.1f, {{1, joinBoth}, {2, joinBoth}});
    first.puzzleTarget = disabledRelay.id; second = first;
    for (int frame = 0; frame < 6; ++frame) simulation.Step(0.1f, {{1, first}, {2, second}});
    disabled.agents[1].incapacitated = true; disabled.agents[1].health = 0.f; disabled.agents[1].incapacityRemaining = 60.f;
    for (int frame = 0; frame < 22; ++frame) simulation.Step(0.1f, {{1, first}});
    Require(!disabledRelay.completed && disabledRelay.puzzle.coordinationProgress == 0.f && disabled.agents[1].puzzle.target == 0,
      "La incapacidad del segundo operador convierte una prueba multijugador en una individual");
    simulation.DisconnectPlayer(2);
    for (int frame = 0; frame < 21; ++frame) simulation.Step(0.1f, {{1, first}});
    Require(disabledRelay.completed, "La desconexión real del segundo jugador no habilita el relé individual");
  }
  void MissionDevicesAreReachable() {
    for (std::uint32_t seed = 0; seed < 6; ++seed) {
      Simulation simulation; simulation.ResetBase(); simulation.Deploy(seed);
      const auto &world = simulation.GetState();
      Require(std::ranges::any_of(world.objects, [](const Interactable &object) { return object.kind == ObjectKind::Explosive && object.puzzle.kind == PuzzleKind::Explosive; }), "La misión no coloca explosivos funcionales");
      Require(std::ranges::any_of(world.objects, [](const Interactable &object) { return object.kind == ObjectKind::Computer && !object.puzzle.intelligence.empty(); }), "La misión no coloca computadores con inteligencia real");
      Require(std::ranges::any_of(world.objects, [](const Interactable &object) { return object.kind == ObjectKind::CameraPanel && object.puzzle.kind == PuzzleKind::CameraPanel; }), "La misión no coloca paneles de cámaras");
      Require(std::ranges::any_of(world.objects, [](const Interactable &object) { return object.kind == ObjectKind::Camera && object.camera.sweeping; }), "La misión no activa el barrido de vigilancia");
      Require(!WorldGeometry::PositionBlocked(world, {185.f, 650.f}, GameplayConfig::AgentRadius), "La cerradura o los dispositivos bloquean el corredor alternativo");
      for (const auto &parent : world.objects) if (parent.kind == ObjectKind::Computer) {
        Require(std::ranges::count_if(world.objects, [&parent](const Interactable &node) { return node.puzzle.parentDevice == parent.id; }) == 4,
          "El computador no tiene cuatro palancas físicas accesibles");
        for (const auto &node : world.objects) if (node.puzzle.parentDevice == parent.id)
          Require(!WorldGeometry::PositionBlocked(world, node.position, GameplayConfig::AgentRadius), "Una palanca distribuida aparece dentro de un sólido");
      }
    }
  }
}

int main() {
  try {
    CameraSweepAndPerception(); CameraTrackingAndMemory(); DoorPuzzleLifecycle(); ComputerAndCameraPanel(); BrokenCameraSurvivesCentralAndPanelReset(); TrapLifecycleAndDamage(); PuzzleFailureAndInvalidation();
    WiresRequireActualPairs(); DistributedLeversNeedPhysicalInteraction(); CoordinationNeedsConcurrentHolds(); MissionDevicesAreReachable();
    std::cout << "NoTraceDeviceTests: " << checks << " comprobaciones correctas.\n"; return 0;
  } catch (const std::exception &exception) { std::cerr << "NoTraceDeviceTests: " << exception.what() << '\n'; return 1; }
}
