#include "NoTraceSimulation.h"
#include "NoTraceWorldGeometry.h"
#include "Config/NoTraceCqcConfig.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
  using namespace NoTrace;
  int checks = 0;
  void Require(bool condition, const char *message) { ++checks; if (!condition) throw std::runtime_error(message); }
  WorldState &Fixture(Simulation &simulation, bool partner = false) {
    simulation.ResetBase(partner ? std::vector<PlayerProfile>{{1,"Uno"},{2,"Dos"}} : std::vector<PlayerProfile>{{1,"Uno"}});
    simulation.Deploy(31);
    auto &world = const_cast<WorldState &>(simulation.GetState());
    world.guards.clear(); world.objects.clear(); world.obstacles.clear(); world.noises.clear(); world.shots.clear(); world.events.clear();
    world.agents[0].position = {200.f, 300.f}; world.agents[0].facing = {1.f, 0.f};
    for (auto &agent : world.agents) agent.privateOrder = PrivateOrder::None;
    if (partner) { world.agents[1].position = {228.f, 300.f}; world.agents[1].facing = {1.f, 0.f}; }
    else { Guard guard; guard.id = 9; guard.position = {228.f, 300.f}; guard.facing = {1.f, 0.f}; guard.patrol = {guard.position}; world.guards.push_back(guard); }
    return world;
  }
  void Advance(Simulation &simulation, ActionCommand command, int count) { for (int frame = 0; frame < count; ++frame) simulation.Step(0.1f, {{1, command}}); }
  ActionCommand Held() { ActionCommand command; command.cqcHeld = true; command.aimX = 1.f; return command; }
  void AcquisitionAndSubdue() {
    Simulation simulation, replica; auto &world = Fixture(simulation);
    auto held = Held(); held.cqc = true;
    simulation.Step(0.1f, {{1, held}});
    Require(world.agents[0].cqcHoldingGuard == 0, "Una pulsación instantánea completa el agarre");
    held.cqc = false; Advance(simulation, held, 3);
    Require(world.guards[0].heldBy == 1 && world.guards[0].cqcState == CqcState::Grabbed, "Mantener F no inicia la sujeción autoritativa");
    const auto agentPosition = world.agents[0].position, guardPosition = world.guards[0].position;
    held.moveX = 1.f; held.fireLethal = true; Advance(simulation, held, 15);
    Require(world.agents[0].position == agentPosition && world.guards[0].position == guardPosition && world.shots.empty(), "Movimiento o disparo altera accidentalmente el agarre");
    Require(world.agents[0].cqcProgress == 0, "Mantener F cuenta como pulsaciones repetidas");
    std::string error;
    Require(replica.LoadSnapshot(simulation.SnapshotFor(1, true), error), "El cliente rechaza la sujeción sincronizada");
    Require(replica.GetState().agents[0].cqcHoldingGuard == 9 && replica.GetState().guards[0].heldBy == 1, "La sujeción se pierde en LAN");
    auto invalid = simulation.SnapshotFor(1, true);
    invalid["visibility"]["visualRadii"][0] = -1.f;
    Require(!replica.LoadSnapshot(invalid, error) && replica.GetState().agents[0].cqcHoldingGuard == 9, "Una frontera inválida altera el estado confirmado del cliente");
    invalid = simulation.SnapshotFor(1, true); invalid["agents"][0]["cqcHoldingGuard"] = 7777;
    Require(!replica.LoadSnapshot(invalid, error), "El cliente acepta un agarre con objetivo inexistente");
    for (unsigned int progress = 1; progress <= CqcConfig::RequiredPulses; ++progress) {
      Advance(simulation, {}, 2);
      auto pulse = Held(); pulse.cqc = true; simulation.Step(0.1f, {{1, pulse}});
      if (progress < CqcConfig::RequiredPulses) {
        Require(world.agents[0].cqcProgress == progress, "Una pulsación deliberada no aumenta el sometimiento");
        Advance(simulation, pulse, 2);
        Require(world.agents[0].cqcProgress == progress, "La repetición de un único evento completa más fases");
      }
    }
    Require(world.guards[0].state == GuardState::Incapacitated && world.guards[0].health == CombatConfig::MaxGuardHealth && world.guards[0].heldBy == 0, "El sometimiento no libera a un guardia inconsciente y vivo");
    Require(world.agents[0].cqcHoldingGuard == 0, "El jugador conserva una referencia al enemigo neutralizado");
    Require(std::ranges::any_of(world.events, [](const MissionEvent &event) { return event.kind == EventKind::Cqc; }), "CQC no invalida Maestro mediante el registro real");
  }
  void RejectionAndCancellation() {
    Simulation simulation; auto &world = Fixture(simulation);
    auto held = Held(); held.aimX = -1.f; Advance(simulation, held, 5);
    Require(world.agents[0].cqcHoldingGuard == 0, "Se agarra a un enemigo fuera de orientación");
    world.obstacles.push_back({{{212.f, 260.f}, {4.f, 80.f}}, CoverKind::High, true, "Pared"});
    held.aimX = 1.f; Advance(simulation, held, 5);
    Require(world.agents[0].cqcHoldingGuard == 0, "Se agarra atravesando paredes");
    Fixture(simulation); Advance(simulation, Held(), 3); Advance(simulation, {}, 11);
    Require(world.agents[0].cqcHoldingGuard == 0 && world.guards[0].heldBy == 0 && world.guards[0].cqcState == CqcState::None, "Soltar F deja la pareja bloqueada");
    Fixture(simulation); Advance(simulation, Held(), 3); simulation.DisconnectPlayer(1);
    Require(world.guards[0].heldBy == 0 && world.guards[0].cqcState == CqcState::None, "La desconexión conserva un agarre huérfano");
  }
  void PushAndCollisions() {
    for (const bool partner : {false, true}) {
      Simulation simulation; auto &world = Fixture(simulation, partner);
      world.obstacles.push_back({{{270.f, 250.f}, {20.f, 100.f}}, CoverKind::High, true, "Pared de empuje"});
      ActionCommand push; push.push = true; push.aimX = 1.f;
      simulation.Step(0.1f, {{1, push}});
      const auto position = partner ? world.agents[1].position : world.guards[0].position;
      Require(position.x > 228.f && position.x <= 258.01f && !WorldGeometry::PositionBlocked(world, position, GameplayConfig::AgentRadius), "El empujón atraviesa la pared o no desplaza al personaje");
      const auto health = partner ? world.agents[1].health : world.guards[0].health;
      Require(health == 100.f, "El empujón causa daño letal");
      simulation.Step(0.1f, {{1, push}});
      Require((partner ? world.agents[1].position : world.guards[0].position) == position, "El empujón no respeta su enfriamiento");
      Simulation replica; std::string error;
      Require(replica.LoadSnapshot(simulation.SnapshotFor(1, true), error), "El cliente rechaza el estado tras un empujón");
    }
  }
}
int main() {
  try { AcquisitionAndSubdue(); RejectionAndCancellation(); PushAndCollisions(); std::cout << "PASS CQC y empujones: " << checks << " comprobaciones\n"; return 0; }
  catch (const std::exception &error) { std::cerr << "FAIL CQC tras " << checks << ": " << error.what() << '\n'; return 1; }
}
