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
    command.puzzleInput = value < 0 ? static_cast<int>((agent.puzzle.clueA + agent.puzzle.clueB) % 4u + 1u) : value;
    return command;
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
    blocked.agents[0].position = {200.f, 300.f}; occluded.camera.state = CameraState::Broken;
    Advance(simulation, 1.f);
    Require(occluded.progress == 0.f, "Una cámara averiada continúa detectando");
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
    const int invalid = static_cast<int>((world.agents[0].puzzle.clueA + world.agents[0].puzzle.clueB + 1u) % 4u + 1u);
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
    computer.puzzle.intelligence = "INTELIGENCIA VERIFICADA: acceso norte.";
    InteractWith(simulation); Solve(simulation);
    Require(computer.completed && world.secondary == 1 && world.message == computer.puzzle.intelligence, "Descifrar un computador no revela inteligencia ni actualiza el objetivo secundario real");
    InteractWith(simulation);
    Require(world.secondary == 1 && world.agents[0].puzzle.target == 0, "Reabrir el computador duplica la recompensa");

    auto &panelWorld = Fixture(simulation);
    auto &panel = Device(panelWorld, ObjectKind::CameraPanel, PuzzleKind::CameraPanel);
    auto &camera = Device(panelWorld, ObjectKind::Camera, PuzzleKind::None, {400.f, 300.f}); camera.facing = {-1.f, 0.f}; camera.camera.controlGroup = 1;
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
    const int invalid = static_cast<int>((world.agents[0].puzzle.clueA + world.agents[0].puzzle.clueB + 1u) % 4u + 1u);
    simulation.Step(0.1f, {{1, Choice(world.agents[0], invalid)}});
    Require(trap.trap.state == TrapState::Detonated && world.agents[0].puzzle.target == 0 && trap.user == 0, "Fallar el desarme no detona o deja la sesión abierta");

    auto &moved = Fixture(simulation);
    auto &door = Device(moved, ObjectKind::Door, PuzzleKind::Door); door.locked = true;
    InteractWith(simulation); moved.agents[0].position = {50.f, 700.f}; simulation.Step(0.1f, {});
    Require(moved.agents[0].puzzle.target == 0 && door.user == 0 && door.locked, "Alejarse conserva un usuario o desbloquea la puerta");
    moved.agents[0].position = {200.f, 300.f}; InteractWith(simulation); simulation.DisconnectPlayer(1);
    Require(moved.agents[0].puzzle.target == 0 && door.user == 0 && door.locked, "Desconectarse deja bloqueado el dispositivo para el equipo");
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
    }
  }
}

int main() {
  try {
    CameraSweepAndPerception(); DoorPuzzleLifecycle(); ComputerAndCameraPanel(); TrapLifecycleAndDamage(); PuzzleFailureAndInvalidation(); MissionDevicesAreReachable();
    std::cout << "NoTraceDeviceTests: " << checks << " comprobaciones correctas.\n"; return 0;
  } catch (const std::exception &exception) { std::cerr << "NoTraceDeviceTests: " << exception.what() << '\n'; return 1; }
}
