#include "NoTraceSimulation.h"
#include "NoTraceCombatGeometry.h"
#include "Config/NoTraceWeaponConfig.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
  using namespace NoTrace;
  int checks = 0;
  void Require(bool value, const char *message) { ++checks; if (!value) throw std::runtime_error(message); }
  WorldState &Fixture(Simulation &simulation, bool partner = false, bool training = false) {
    simulation.ResetBase(partner ? std::vector<PlayerProfile>{{1, "Uno"}, {2, "Dos"}} : std::vector<PlayerProfile>{{1, "Uno"}});
    simulation.SetLoadout(1, {8, 8, 3, 2});
    if (!training) simulation.Deploy(31);
    auto &world = const_cast<WorldState &>(simulation.GetState());
    world.phase = training ? GamePhase::Base : GamePhase::Mission;
    world.mode = training ? SimulationMode::Training : SimulationMode::Operation;
    world.guards.clear(); world.objects.clear(); world.obstacles.clear(); world.events.clear(); world.sounds.clear(); world.shots.clear(); world.noises.clear(); world.footsteps.clear(); world.securityEntries.clear();
    for (auto &agent : world.agents) { agent.privateOrder = PrivateOrder::None; agent.position = {200.f + 80.f * (agent.id - 1), 300.f}; agent.facing = {1.f, 0.f}; }
    return world;
  }
  void Advance(Simulation &simulation, int frames, ActionCommand command = {}) { for (int n = 0; n < frames; ++n) simulation.Step(0.1f, {{1, command}}); }
  ActionCommand Shoot() { ActionCommand command; command.fireSelected = true; command.aimX = 1.f; return command; }
  int Shots(const WorldState &world) { return static_cast<int>(std::ranges::count_if(world.events, [](const MissionEvent &event) { return event.kind == EventKind::WeaponLethal || event.kind == EventKind::WeaponNonLethal; })); }
  void FireOnce(Simulation &simulation) { simulation.Step(0.1f, {{1, Shoot()}}); Advance(simulation, 8); }
  void ReloadAndInventory() {
    Simulation simulation, replica; auto &world = Fixture(simulation);
    Require(world.agents[0].lethalMagazine == WeaponConfig::PistolCapacity && world.agents[0].loadout.lethalAmmo == 8, "El cargador inicial altera la cantidad total preparada");
    for (int n = 0; n < WeaponConfig::PistolCapacity; ++n) FireOnce(simulation);
    Require(Shots(world) == 4 && world.agents[0].lethalMagazine == 0 && world.agents[0].loadout.lethalAmmo == 4, "Vaciar un cargador no consume exactamente cuatro cartuchos del total");
    FireOnce(simulation);
    Require(Shots(world) == 4 && world.agents[0].loadout.lethalAmmo == 4, "Un cargador vacío permite disparar munición de reserva");
    ActionCommand reload; reload.reloadWeapon = true;
    simulation.Step(0.1f, {{1, reload}});
    Require(world.agents[0].reloadRemaining == WeaponConfig::ReloadDuration && world.agents[0].lethalMagazine == 0, "La recarga llena el cargador inmediatamente");
    std::string error;
    Require(replica.LoadSnapshot(simulation.SnapshotFor(1, true), error) && replica.GetState().agents[0].reloadRemaining > 0.f, "El cliente pierde la recarga autoritativa");
    Advance(simulation, 5, Shoot());
    Require(Shots(world) == 4 && world.agents[0].reloadRemaining > 0.f, "Disparar omite la duración de la recarga");
    Advance(simulation, 8);
    Require(world.agents[0].reloadRemaining == 0.f && world.agents[0].lethalMagazine == 4 && world.agents[0].loadout.lethalAmmo == 4, "La recarga gasta dos veces la munición o rellena una cantidad incorrecta");
    FireOnce(simulation);
    Require(Shots(world) == 5 && world.agents[0].lethalMagazine == 3 && world.agents[0].loadout.lethalAmmo == 3, "No se puede disparar una vez completada la recarga");
    simulation.Step(0.1f, {{1, reload}});
    Require(world.agents[0].reloadRemaining == 0.f, "Se inicia una recarga sin cartuchos de reserva");
  }
  void SelectionAndInterruption() {
    Simulation simulation; auto &world = Fixture(simulation);
    ActionCommand choice; choice.weaponChoice = 1;
    simulation.Step(0.1f, {{1, choice}}); FireOnce(simulation);
    Require(world.agents[0].selectedWeapon == WeaponKind::NonLethal && world.agents[0].nonLethalMagazine == 3 && world.agents[0].lethalMagazine == 4, "El disparo seleccionado no respeta el cambio a arma no letal");
    Require(std::ranges::count_if(world.events, [](const MissionEvent &event) { return event.kind == EventKind::WeaponNonLethal; }) == 1, "El arma seleccionada registra otro tipo de disparo");
    ActionCommand reload; reload.reloadWeapon = true; simulation.Step(0.1f, {{1, reload}});
    Require(world.agents[0].reloadRemaining > 0.f, "La recarga no letal no comienza");
    choice.weaponChoice = 0; simulation.Step(0.1f, {{1, choice}});
    Require(world.agents[0].reloadRemaining == 0.f && world.agents[0].selectedWeapon == WeaponKind::Pistol && world.agents[0].nonLethalMagazine == 3, "Cambiar de arma completa una recarga cancelada");
    ActionCommand legacy; legacy.fireNonLethal = true; legacy.aimX = 1.f;
    simulation.Step(0.1f, {{1, legacy}});
    Require(world.agents[0].nonLethalMagazine == 2 && world.agents[0].selectedWeapon == WeaponKind::Pistol, "El acceso no letal directo deja de ser compatible con el arma seleccionada");
    Advance(simulation, 8); FireOnce(simulation);
    simulation.Step(0.1f, {{1, reload}});
    ActionCommand cqc; cqc.cqc = true; cqc.cqcHeld = true; simulation.Step(0.1f, {{1, cqc}});
    Require(world.agents[0].reloadRemaining == 0.f, "La preparación CQC conserva una recarga incompatible");
    Advance(simulation, 10);
    simulation.Step(0.1f, {{1, reload}}); simulation.DisconnectPlayer(1);
    Require(world.agents[0].reloadRemaining == 0.f, "Desconectar deja una recarga activa");
  }
  void ProneReload() {
    for (const bool training : {false, true}) {
      Simulation simulation; auto &world = Fixture(simulation, false, training);
      world.agents[0].stance = Stance::Prone; world.agents[0].lethalMagazine = 0;
      ActionCommand reload; reload.reloadWeapon = true;
      simulation.Step(0.1f, {{1, reload}});
      Require(world.agents[0].reloadRemaining == WeaponConfig::ReloadDuration && world.agents[0].stance == Stance::Prone, "La ausencia de CQC al estar tumbado cancela una recarga legítima");
      Advance(simulation, 5, Shoot());
      Require(world.agents[0].reloadRemaining > 0.f && world.agents[0].lethalMagazine == 0 && Shots(world) == 0, "Estar tumbado permite omitir la duración o el bloqueo de disparo de la recarga");
      Advance(simulation, 8);
      Require(world.agents[0].reloadRemaining == 0.f && world.agents[0].lethalMagazine == WeaponConfig::PistolCapacity && world.agents[0].stance == Stance::Prone, "La recarga tumbado no finaliza con el cargador lleno");
      simulation.Step(0.1f, {{1, Shoot()}});
      Require(Shots(world) == 1 && world.agents[0].lethalMagazine == WeaponConfig::PistolCapacity - 1, "El agente tumbado no puede disparar después de recargar");
    }
  }
  void TrainingProtectionAndTransition() {
    Simulation simulation; auto &world = Fixture(simulation, true, true);
    Guard dummy; dummy.id = 9; dummy.position = {250.f, 300.f}; dummy.facing = {1.f, 0.f}; dummy.trainingDummy = true; dummy.patrol = {dummy.position};
    world.guards.push_back(dummy);
    for (int n = 0; n < 20; ++n) {
      auto &shooter = world.agents[0]; shooter.actionCooldown = 0.f;
      if (shooter.lethalMagazine == 0) { ActionCommand reload; reload.reloadWeapon = true; simulation.Step(0.1f, {{1, reload}}); Advance(simulation, 13); }
      simulation.Step(0.1f, {{1, Shoot()}});
    }
    Require(world.guards[0].trainingHits >= 20 && world.guards[0].health >= 1.f && world.guards[0].state != GuardState::Dead && world.guards[0].state != GuardState::Incapacitated, "Disparos repetidos eliminan o incapacitan un muñeco de práctica");
    world.guards.clear(); world.agents[1].position = {260.f, 300.f};
    auto head = Shoot();
    const auto headOffset = CombatGeometry::ActorVolumes(world.agents[1].position, world.agents[1].facing, Stance::Standing).front().center - world.agents[0].position;
    head.aimSlope = (CombatGeometry::HeadCenterHeight(Stance::Standing) + CombatConfig::HeadHeight * 0.25f - CombatGeometry::WeaponHeight(Stance::Standing)) / headOffset.length();
    world.agents[0].actionCooldown = 0.f;
    if (world.agents[0].lethalMagazine == 0) { ActionCommand reload; reload.reloadWeapon = true; simulation.Step(0.1f, {{1, reload}}); Advance(simulation, 13); }
    simulation.Step(0.1f, {{1, head}});
    Require(world.agents[1].alive && !world.agents[1].incapacitated && world.agents[1].health >= 1.f, "Un disparo letal amistoso ignora la protección autoritativa del entrenamiento");
    Advance(simulation, 8); head.fireSelected = false; head.fireNonLethal = true;
    simulation.Step(0.1f, {{1, head}});
    Require(world.agents[1].alive && !world.agents[1].incapacitated && !world.agents[1].friendlyIncapacitated && world.agents[1].stamina >= 1.f, "Un tiro no letal en cabeza omite la protección de incapacitación directa");
    simulation.SetLoadout(1, {2, 3, 1, 1}); simulation.Deploy(74);
    Require(world.phase == GamePhase::Mission && !IsTraining(world) && !world.guards.empty() && std::ranges::none_of(world.guards, [](const Guard &guard) { return guard.trainingDummy; }), "Desplegar conserva muñecos o reglas de práctica en la operación");
    Require(world.agents[0].loadout.lethalAmmo == 2 && world.agents[0].lethalMagazine == 2 && world.agents[0].loadout.nonLethalAmmo == 3, "El reabastecimiento de entrenamiento contamina el equipo preparado de misión");
    world.contract = ContractId::DoubleCross; world.guards.clear(); world.objects.clear(); world.obstacles.clear(); world.securityEntries.clear();
    world.agents[0].position = {200.f, 300.f}; world.agents[1].position = {260.f, 300.f}; world.agents[0].actionCooldown = 0.f;
    auto lethal = Shoot(); lethal.aimSlope = head.aimSlope;
    simulation.Step(0.1f, {{1, lethal}});
    Require(!world.agents[1].alive && world.agents[1].health == 0.f, "La protección de entrenamiento persiste al recibir daño letal en una misión real");
  }
}
int main() {
  try { ReloadAndInventory(); SelectionAndInterruption(); ProneReload(); TrainingProtectionAndTransition(); std::cout << "PASS armas y seguridad: " << checks << " comprobaciones\n"; return 0; }
  catch (const std::exception &error) { std::cerr << "FAIL armas tras " << checks << ": " << error.what() << '\n'; return 1; }
}
