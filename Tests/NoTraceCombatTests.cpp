#include "NoTraceCombatGeometry.h"
#include "NoTraceSimulation.h"
#include "NoTraceWorldGeometry.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
  using namespace NoTrace;
  int checks = 0;
  void Require(bool condition, const char *message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
  }
  WorldState &Fixture(Simulation &simulation, bool cooperative = false) {
    simulation.ResetBase(cooperative ? std::vector<PlayerProfile>{{1, "Uno"}, {2, "Dos"}} : std::vector<PlayerProfile>{{1, "Uno"}});
    if (cooperative) simulation.SetContract(ContractId::DoubleCross);
    simulation.Deploy(0);
    auto &world = const_cast<WorldState &>(simulation.GetState());
    world.obstacles.clear(); world.objects.clear(); world.guards.clear(); world.noises.clear(); world.shots.clear();
    world.agents[0].position = {200.f, 300.f}; world.agents[0].facing = {1.f, 0.f};
    world.agents[0].loadout = {12, 12, 0, 0};
    if (cooperative) world.agents[1].position = {230.f, 300.f};
    return world;
  }
  Guard &HeldGuard(WorldState &world) {
    Guard guard; guard.id = 11; guard.position = {230.f, 300.f}; guard.facing = {-1.f, 0.f};
    guard.cqcState = CqcState::Grabbed; guard.heldBy = 1;
    world.guards.push_back(guard);
    return world.guards.back();
  }
  ActionCommand Aim(sf::Vector2f from, Stance stance, sf::Vector2f point, float height, bool lethal) {
    const auto offset = point - from;
    const float distance = std::max(0.001f, offset.length());
    ActionCommand command;
    command.aimX = offset.x / distance; command.aimY = offset.y / distance;
    command.aimSlope = (height - CombatGeometry::WeaponHeight(stance)) / distance;
    command.fireLethal = lethal; command.fireNonLethal = !lethal;
    return command;
  }
  ActionCommand AtZone(const Agent &actor, sf::Vector2f position, sf::Vector2f facing, Stance stance, HitZone zone, bool lethal) {
    const auto volumes = CombatGeometry::ActorVolumes(position, facing, stance);
    const auto volume = std::ranges::find_if(volumes, [zone](const auto &part) { return part.zone == zone; });
    Require(volume != volumes.end(), "La anatomía necesita todas las zonas de impacto");
    return Aim(actor.position, actor.stance, volume->center, (volume->bottom + volume->top) * 0.5f, lethal);
  }
  void Shot(Simulation &simulation, HitZone zone, bool lethal) {
    const auto &world = simulation.GetState();
    const auto &guard = world.guards.front();
    simulation.Step(1.f / 60.f, {{1, AtZone(world.agents[0], guard.position, guard.facing, Stance::Standing, zone, lethal)}});
  }
  void Rest(Simulation &simulation, int frames = 42) {
    for (int frame = 0; frame < frames; ++frame) simulation.Step(1.f / 60.f, {});
  }

  void LogicalVolumes() {
    for (const auto stance : {Stance::Standing, Stance::Crouched, Stance::Prone}) {
      for (const auto facing : {sf::Vector2f{1.f, 0.f}, sf::Vector2f{0.f, 1.f}, sf::Vector2f{-0.6f, -0.8f}}) {
        const sf::Vector2f target{400.f, 300.f};
        const auto volumes = CombatGeometry::ActorVolumes(target, facing, stance);
        for (const auto zone : {HitZone::Head, HitZone::Torso, HitZone::Limbs}) {
          const auto part = std::ranges::find_if(volumes, [zone](const auto &value) { return value.zone == zone; });
          const auto view = stance == Stance::Prone && zone != HitZone::Head ? sf::Vector2f{-facing.y, facing.x} : facing;
          const auto start = part->center + view * 180.f;
          const auto direction = -view;
          const float source = CombatGeometry::WeaponHeight(stance);
          const float slope = ((part->bottom + part->top) * 0.5f - source) / 180.f;
          const auto hit = CombatGeometry::IntersectActor(start, direction, source, slope, 200.f, target, facing, stance);
          Require(hit && hit->zone == zone, "La orientación y postura deben conservar la zona lógica apuntada");
          Require(hit->height >= part->bottom && hit->height <= part->top, "La altura del impacto debe pertenecer al volumen real");
        }
      }
    }
    const auto miss = CombatGeometry::IntersectActor({200.f, 300.f}, {1.f, 0.f}, 70.f, 0.f, 300.f, {400.f, 300.f}, {-1.f, 0.f}, Stance::Standing);
    Require(!miss, "Un rayo por encima de la cabeza no debe impactar el cuerpo");
    Require(CombatGeometry::WeaponHeight(Stance::Standing) > CombatGeometry::WeaponHeight(Stance::Crouched) && CombatGeometry::WeaponHeight(Stance::Crouched) > CombatGeometry::WeaponHeight(Stance::Prone), "El origen del arma debe seguir la postura");
  }

  void LethalAndNonLethalZones() {
    for (const auto zone : {HitZone::Head, HitZone::Torso, HitZone::Limbs}) {
      Simulation simulation;
      auto &world = Fixture(simulation); HeldGuard(world);
      Shot(simulation, zone, true);
      const auto &guard = simulation.GetState().guards.front();
      Require(!simulation.GetState().shots.empty() && simulation.GetState().shots.back().zone == zone, "El host debe resolver y registrar la zona del disparo");
      Require(guard.health == CombatConfig::MaxGuardHealth - CombatGeometry::LethalDamage(zone), "El daño letal depende de la zona calculada");
      Require((guard.state == GuardState::Dead) == (zone == HitZone::Head), "Solo la cabeza debe matar de inmediato con salud completa");
      const auto &shot = simulation.GetState().shots.back();
      Require(shot.targetId == guard.id && shot.fromHeight == CombatGeometry::WeaponHeight(world.agents[0].stance), "El efecto conserva identidad y altura autoritativas");
      if (zone != HitZone::Head) {
        for (int attempt = 0; attempt < 5 && world.guards[0].state != GuardState::Dead; ++attempt) { Rest(simulation); Shot(simulation, zone, true); }
        Require(world.guards[0].state == GuardState::Dead && world.guards[0].health == 0.f, "Los impactos letales en torso o extremidades acumulan heridas hasta eliminar");
      }
    }
    for (const auto zone : {HitZone::Head, HitZone::Torso, HitZone::Limbs}) {
      Simulation simulation;
      auto &world = Fixture(simulation); HeldGuard(world);
      Shot(simulation, zone, false);
      Require(world.guards[0].health == CombatConfig::MaxGuardHealth && world.guards[0].state != GuardState::Dead, "La fuerza no letal nunca reduce la salud ni mata");
      Require((world.guards[0].state == GuardState::Incapacitated) == (zone == HitZone::Head), "El impacto no letal de cabeza incapacita de inmediato");
      if (zone != HitZone::Head) {
        Require(world.guards[0].stun > 0.f && world.guards[0].stun < CombatConfig::MaxGuardStun, "Torso y extremidades acumulan aturdimiento verificable");
        for (int attempt = 0; attempt < 4 && world.guards[0].state != GuardState::Incapacitated; ++attempt) { Rest(simulation); Shot(simulation, zone, false); }
        Require(world.guards[0].state == GuardState::Incapacitated && world.guards[0].health == CombatConfig::MaxGuardHealth, "Los impactos no letales acumulados terminan en inconsciencia recuperable");
      }
    }
  }

  void BulletOcclusionAndPosture() {
    Simulation simulation;
    auto &world = Fixture(simulation); HeldGuard(world);
    world.guards[0].position = {400.f, 300.f};
    world.obstacles.push_back({{{290.f, 260.f}, {20.f, 80.f}}, CoverKind::High, true, "Muro"});
    Shot(simulation, HitZone::Head, true);
    Require(world.guards[0].health == CombatConfig::MaxGuardHealth && world.shots.back().blocked, "El disparo a cabeza no atraviesa un muro alto");
    auto &lowWorld = Fixture(simulation); HeldGuard(lowWorld); lowWorld.guards[0].position = {400.f, 300.f};
    lowWorld.obstacles.push_back({{{290.f, 260.f}, {20.f, 80.f}}, CoverKind::Low, true, "Caja"});
    Shot(simulation, HitZone::Torso, true);
    Require(lowWorld.guards[0].health == CombatConfig::MaxGuardHealth && lowWorld.shots.back().blocked, "Una cobertura baja intercepta un rayo situado por debajo de su altura");
    auto &aboveWorld = Fixture(simulation); HeldGuard(aboveWorld); aboveWorld.guards[0].position = {400.f, 300.f};
    aboveWorld.obstacles.push_back({{{290.f, 260.f}, {20.f, 80.f}}, CoverKind::Low, true, "Caja"});
    Shot(simulation, HitZone::Head, true);
    Require(aboveWorld.guards[0].state == GuardState::Dead, "Una trayectoria por encima de una caja baja conserva el impacto en cabeza");
    for (const auto stance : {Stance::Standing, Stance::Crouched, Stance::Prone}) {
      auto &posedWorld = Fixture(simulation); HeldGuard(posedWorld); posedWorld.agents[0].stance = stance;
      Shot(simulation, HitZone::Head, true);
      Require(posedWorld.guards[0].state == GuardState::Dead && posedWorld.shots.back().fromHeight == CombatGeometry::WeaponHeight(stance), "La puntería elevada debe funcionar desde cualquier postura del tirador");
    }
  }

  void ProneProtectionAndMelee() {
    Simulation simulation;
    auto &world = Fixture(simulation);
    world.agents[0].stance = Stance::Prone;
    Guard guard; guard.id = 21; guard.position = {300.f, 300.f}; guard.facing = {-1.f, 0.f}; guard.state = GuardState::Alert; guard.suspicion = 1.f;
    world.guards.push_back(guard);
    for (int frame = 0; frame < 3; ++frame) simulation.Step(0.1f, {});
    Require(world.agents[0].health == GameplayConfig::MaxHealth && world.guards[0].ammo == GameplayConfig::GuardAmmo && world.shots.empty(), "El enemigo no dispara ni aplica daño de bala a un agente tumbado detectado");
    Require(world.guards[0].position.x < 300.f, "La protección tumbado no impide al guardia perseguir");
    for (int frame = 0; frame < 5; ++frame) simulation.Step(0.1f, {});
    Require(world.agents[0].health < GameplayConfig::MaxHealth && std::ranges::any_of(world.events, [](const MissionEvent &event) { return event.kind == EventKind::GuardMelee; }), "El guardia puede alcanzar y golpear a un agente tumbado");
    Require(std::ranges::none_of(world.events, [](const MissionEvent &event) { return event.kind == EventKind::GuardShot; }), "El combate melee contra un agente tumbado no genera disparos ocultos");

    auto &crossWorld = Fixture(simulation, true);
    crossWorld.agents[0].position = {400.f, 300.f}; crossWorld.agents[0].stance = Stance::Standing;
    crossWorld.agents[1].position = {300.f, 300.f}; crossWorld.agents[1].stance = Stance::Prone; crossWorld.agents[1].hidden = true;
    guard.position = {200.f, 300.f}; guard.facing = {1.f, 0.f}; crossWorld.guards.push_back(guard);
    simulation.Step(0.1f, {});
    Require(crossWorld.agents[0].health < GameplayConfig::MaxHealth && crossWorld.agents[1].health == GameplayConfig::MaxHealth, "Un agente tumbado tampoco recibe daño al cruzarse en un tiro dirigido a otro agente");
    Require(crossWorld.shots.back().targetId == 1, "El impacto autoritativo omite interceptores tumbados protegidos");

    auto &headWorld = Fixture(simulation, true);
    headWorld.agents[0].position = {400.f, 300.f};
    headWorld.agents[1].position = {300.f, 300.f}; headWorld.agents[1].stance = Stance::Crouched; headWorld.agents[1].hidden = true;
    headWorld.guards.push_back(guard);
    simulation.Step(0.1f, {});
    Require(headWorld.shots.back().zone == HitZone::Head && !headWorld.agents[1].alive && headWorld.agents[1].health == 0.f, "Una cabeza que intercepta un tiro letal enemigo conserva el daño inmediato de cabeza");
  }

  void FriendlyZonesAndSnapshots() {
    Simulation simulation, replica;
    auto &world = Fixture(simulation, true);
    const auto command = AtZone(world.agents[0], world.agents[1].position, world.agents[1].facing, world.agents[1].stance, HitZone::Torso, true);
    simulation.Step(1.f / 60.f, {{1, command}});
    Require(world.agents[1].alive && world.agents[1].health == GameplayConfig::MaxHealth - CombatConfig::LethalTorsoDamage, "La traición conserva el daño diferenciado por zonas del agente");
    auto snapshot = simulation.SnapshotFor(1);
    std::string error;
    Require(replica.LoadSnapshot(snapshot, error), "El snapshot del combate por zonas debe ser válido");
    Require(replica.GetState().shots.back().zone == HitZone::Torso && replica.GetState().shots.back().targetId == 2, "El cliente recibe la zona y víctima resueltas por el host");
    Require(replica.GetState().shots.back().fromHeight == world.shots.back().fromHeight && replica.GetState().shots.back().toHeight == world.shots.back().toHeight, "El cliente no reconstruye ni inventa alturas del disparo");
    auto &nonLethal = Fixture(simulation, true);
    const auto head = AtZone(nonLethal.agents[0], nonLethal.agents[1].position, nonLethal.agents[1].facing, Stance::Standing, HitZone::Head, false);
    simulation.Step(1.f / 60.f, {{1, head}});
    Require(nonLethal.agents[1].alive && nonLethal.agents[1].incapacitated && nonLethal.agents[1].health == GameplayConfig::MaxHealth, "Un impacto no letal en cabeza del agente produce inconsciencia sin muerte");
  }
}

int main() {
  try {
    LogicalVolumes();
    LethalAndNonLethalZones();
    BulletOcclusionAndPosture();
    ProneProtectionAndMelee();
    FriendlyZonesAndSnapshots();
    std::cout << "PASS combate: " << checks << " comprobaciones de zonas, postura, trayectoria y protección tumbado\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL combate tras " << checks << " comprobaciones: " << error.what() << '\n';
    return 1;
  }
}
