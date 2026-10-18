#include "NoTraceCombatGeometry.h"
#include "NoTraceSimulation.h"
#include "NoTraceSpatialFixture.h"
#include "NoTraceSpatialGeometry.h"
#include "NoTraceWorldGeometry.h"
#include "Config/NoTraceWeaponConfig.h"
#include "Config/NoTraceTrainingConfig.h"
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
    TestFixture::LegacyPlane(world);
    world.phase = GamePhase::Mission; world.mode = SimulationMode::Operation;
    world.obstacles.clear(); world.objects.clear(); world.guards.clear(); world.noises.clear(); world.shots.clear(); world.securityEntries.clear();
    world.agents[0].position = {200.f, 300.f}; world.agents[0].facing = {1.f, 0.f};
    world.agents[0].loadout = {12, 12, 0, 0};
    if (cooperative) world.agents[1].position = {230.f, 300.f};
    return world;
  }
  Guard &HeldGuard(WorldState &world) {
    Guard guard; guard.id = 11; guard.position = {230.f, 300.f}; guard.facing = {-1.f, 0.f};
    // Las pruebas de zonas aíslan el daño recibido; los contraataques tienen sus propios escenarios.
    guard.attackCooldown = 60.f;
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
    const auto &actor = simulation.GetState().agents[0];
    if ((lethal ? actor.lethalMagazine : actor.nonLethalMagazine) == 0 && (lethal ? actor.loadout.lethalAmmo : actor.loadout.nonLethalAmmo) > 0) {
      const int total = lethal ? actor.loadout.lethalAmmo : actor.loadout.nonLethalAmmo;
      ActionCommand reload; reload.reloadWeapon = true; reload.weaponChoice = lethal ? 0 : 1;
      simulation.Step(1.f / 60.f, {{1, reload}});
      const int frames = static_cast<int>(std::ceil(WeaponConfig::ReloadDuration * 60.f)) + 1;
      for (int frame = 0; frame < frames; ++frame) simulation.Step(1.f / 60.f, {});
      const auto &ready = simulation.GetState().agents[0];
      Require(ready.reloadRemaining == 0.f && (lethal ? ready.lethalMagazine : ready.nonLethalMagazine) > 0 && (lethal ? ready.loadout.lethalAmmo : ready.loadout.nonLethalAmmo) == total, "Recargar entre impactos debe respetar tiempo y total de munición");
    }
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

  void PartialHitKeepsTheGuardsCurrentFloor() {
    Simulation simulation; auto &world = Fixture(simulation);
    world.walkSurfaces = {{1, 0, WalkSurfaceKind::Floor, {{0.f, 0.f}, world.size}, 0.f},
      {2, 1, WalkSurfaceKind::Floor, {{100.f, 200.f}, {700.f, 400.f}}, 96.f}};
    auto &actor = world.agents[0]; actor.elevation = 96.f; actor.supportId = 2;
    Guard guard; guard.id = 93; guard.position = {400.f, 300.f}; guard.elevation = 96.f; guard.supportId = 2; guard.facing = {1.f, 0.f};
    guard.state = GuardState::Search; guard.lastKnown = {500.f, 300.f}; guard.lastKnownElevation = 0.f;
    world.guards.push_back(guard);
    Require(!WorldGeometry::PositionBlocked(world, guard.position, GameplayConfig::AgentRadius, guard.elevation, CombatConfig::StandingHeight, guard.supportId) &&
      !WorldGeometry::PositionBlocked(world, actor.position, GameplayConfig::AgentRadius, actor.elevation, CombatConfig::StandingHeight, actor.supportId),
      "El impacto multinivel necesita cuerpos apoyados físicamente en la misma planta");
    const auto command = AtZone(actor, guard.position, guard.facing, Stance::Standing, HitZone::Torso, false);
    simulation.Step(1.f / 60.f, {{actor.id, command}});
    const auto &wounded = world.guards.front();
    Require(!world.shots.empty() && world.shots.back().targetId == guard.id && world.shots.back().zone == HitZone::Torso &&
      wounded.stun > 0.f && wounded.stun < CombatConfig::MaxGuardStun && wounded.health == CombatConfig::MaxGuardHealth && wounded.state == GuardState::Search,
      "Un impacto lejano no letal debe herir sin incapacitar ni convertirse en una percepción visual");
    Require(wounded.lastKnown == guard.position && wounded.lastKnownElevation == 96.f && wounded.lastKnownElevation == wounded.elevation && !world.alarm &&
      std::ranges::all_of(world.noises, [&](const NoiseEvent &noise) { return WorldGeometry::SpatialDistance(WorldGeometry::Feet(wounded), {noise.position.x, noise.position.y, noise.elevation}) > noise.radius; }),
      "El daño reemplaza la pista de otra planta por la posición y altura físicas del guardia sin apoyarse en oír el disparo");
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
    aboveWorld.obstacles.push_back({{{360.f, 260.f}, {20.f, 80.f}}, CoverKind::Low, true, "Caja"});
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
    Require(headWorld.shots.back().zone == HitZone::Head && headWorld.agents[1].alive && !headWorld.agents[1].incapacitated && headWorld.agents[1].health == GameplayConfig::MaxHealth - CombatConfig::LethalHeadDamage, "Una cabeza que intercepta un tiro enemigo recibe daño mayor que el torso sin duplicar impactos");
  }

  void MissionSurvivabilityAndDiscreteDamage() {
    Simulation simulation; auto &world = Fixture(simulation);
    world.agents[0].position = {300.f, 300.f};
    Guard guard; guard.id = 88; guard.position = {450.f, 300.f}; guard.facing = {-1.f, 0.f}; guard.state = GuardState::Alert; guard.suspicion = 1.f;
    world.guards.push_back(guard);
    for (int hit = 0; hit < 3; ++hit) {
      world.guards[0].attackCooldown = 0.f;
      const auto before = world.agents[0].health;
      simulation.Step(1.f / 60.f, {});
      Require(world.agents[0].health == before - GameplayConfig::GuardShotDamage && world.agents[0].lifeState == LifeState::Alive,
        "Cada disparo normal aplica daño corporal una vez y el jugador sobrevive a varios impactos");
    }
    const auto received = std::ranges::count_if(world.events, [](const MissionEvent &event) { return event.kind == EventKind::AgentDamaged; });
    const auto fired = std::ranges::count_if(world.events, [](const MissionEvent &event) { return event.kind == EventKind::GuardShot; });
    Require(received == 3 && fired == 3 && world.agents[0].health == GameplayConfig::MaxHealth - 3.f * GameplayConfig::GuardShotDamage,
      "Tres balas generan tres daños discretos sin eventos duplicados por fotograma");
    Require(CombatConfig::LethalHeadDamage > GameplayConfig::GuardShotDamage && CombatConfig::AgentLethalHeadDamage > CombatConfig::LethalTorsoDamage,
      "Los impactos en cabeza mantienen una amenaza superior al cuerpo para cada bando");
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

  void PassiveRecoveryThresholdAndFraction() {
    Simulation simulation; auto &world = Fixture(simulation);
    auto &agent = world.agents[0]; agent.health = 80.f; agent.stamina = 20.f;
    Require(agent.healthRecoveryRemaining == 12.f, "El despliegue inicia doce segundos completos de espera de salud");
    for (int step = 0; step < 191; ++step) simulation.Step(0.0625f, {});
    Require(agent.health == 80.f && agent.healthRecoveryRemaining == 0.0625f && agent.stamina == GameplayConfig::MaxStamina,
      "La resistencia puede recuperarse antes de doce segundos sin adelantar la salud");
    simulation.Step(0.0625f, {});
    Require(agent.health == 80.f && agent.healthRecoveryRemaining == 0.f, "El instante exacto de doce segundos no recupera tiempo anterior al umbral");
    simulation.Step(0.0625f, {});
    Require(agent.health == 80.f + GameplayConfig::PassiveHealthRecoveryPerSecond * 0.0625f,
      "El primer intervalo posterior a doce segundos recupera salud lentamente");
    const float initial = agent.health;
    agent.healthRecoveryRemaining = 0.04f; simulation.Step(0.1f, {});
    Require(std::abs(agent.health - initial - GameplayConfig::PassiveHealthRecoveryPerSecond * 0.06f) < 0.0001f,
      "Un paso que cruza el umbral recupera sólo su fracción posterior");
    const float beforeSecond = agent.health;
    for (int step = 0; step < 16; ++step) simulation.Step(0.0625f, {});
    Require(std::abs(agent.health - beforeSecond - GameplayConfig::PassiveHealthRecoveryPerSecond) < 0.0001f,
      "Un segundo autoritativo recupera la tasa configurada independientemente de los fotogramas");
    agent.health = GameplayConfig::MaxHealth - 0.02f; simulation.Step(0.1f, {});
    Require(agent.health == GameplayConfig::MaxHealth, "La recuperación se limita exactamente a los doscientos puntos de salud");
    const auto timer = agent.healthRecoveryRemaining;
    simulation.Step(0.f, {}); simulation.Step(std::numeric_limits<float>::quiet_NaN(), {});
    Require(agent.health == GameplayConfig::MaxHealth && agent.healthRecoveryRemaining == timer, "Un paso temporal inválido no modifica salud ni espera");
    Require(world.events.empty(), "La recuperación pasiva no inventa impactos, rescates ni eventos de puntuación");
  }

  void NewImpactsRestartRecovery() {
    Simulation simulation; auto &world = Fixture(simulation, true);
    for (auto &agent : world.agents) agent.privateOrder = PrivateOrder::CopyArchive;
    auto &target = world.agents[0]; target.health = 150.f;
    target.healthRecoveryRemaining = 1.f;
    const auto fire = [&](bool lethal) {
      auto &actor = world.agents[1]; actor.actionCooldown = 0.f;
      const auto command = AtZone(actor, target.position, target.facing, target.stance, HitZone::Torso, lethal);
      simulation.Step(0.0625f, {{2, command}});
    };
    fire(true);
    Require(target.health == 150.f - CombatConfig::LethalTorsoDamage && target.healthRecoveryRemaining == 12.f,
      "Un impacto letal real reinicia doce segundos completos aunque la espera ya estuviera avanzada");
    const float wounded = target.health;
    for (int step = 0; step < 191; ++step) simulation.Step(0.0625f, {});
    Require(target.health == wounded && target.healthRecoveryRemaining == 0.0625f, "No hay recuperación anticipada tras recibir un impacto nuevo");
    fire(false);
    Require(target.health == wounded && !target.incapacitated && target.healthRecoveryRemaining == 12.f,
      "Un nuevo impacto no letal también reinicia la espera sin convertir resistencia en vida");
    for (int step = 0; step < 192; ++step) simulation.Step(0.0625f, {});
    Require(target.health == wounded && target.healthRecoveryRemaining == 0.f, "Los doce segundos se cuentan desde el último impacto de cualquier tipo");
    simulation.Step(0.0625f, {});
    Require(target.health > wounded && target.health < wounded + 0.1f, "La recuperación se reanuda progresivamente al completar la espera reiniciada");
    auto &laterTarget = world.agents[1]; laterTarget.health = 150.f; laterTarget.healthRecoveryRemaining = 1.f;
    target.actionCooldown = 0.f;
    const auto reverse = AtZone(target, laterTarget.position, laterTarget.facing, laterTarget.stance, HitZone::Torso, true);
    simulation.Step(0.0625f, {{1, reverse}});
    Require(laterTarget.health == 150.f - CombatConfig::LethalTorsoDamage && laterTarget.healthRecoveryRemaining == 12.f,
      "El impacto conserva doce segundos completos aunque el atacante se actualice antes que la víctima");
  }

  void RecoveryNeverReanimatesAndKeepsRescueDeadline() {
    Simulation simulation; auto &world = Fixture(simulation, true);
    for (auto &agent : world.agents) agent.privateOrder = PrivateOrder::CopyArchive;
    auto &target = world.agents[0]; target.health = CombatConfig::LethalTorsoDamage; target.stamina = 0.f;
    const auto command = AtZone(world.agents[1], target.position, target.facing, target.stance, HitZone::Torso, true);
    simulation.Step(0.0625f, {{2, command}});
    Require(target.health == 0.f && target.stamina == 0.f && target.lifeState == LifeState::Incapacitated && target.incapacityRemaining == 55.f,
      "El impacto fatal tiene prioridad sobre resistencia y abre el rescate cooperativo de cincuenta y cinco segundos");
    for (int step = 0; step < 208; ++step) simulation.Step(0.0625f, {});
    Require(target.health == 0.f && target.stamina == 0.f && target.incapacitated && target.healthRecoveryRemaining == 12.f && target.incapacityRemaining == 42.f,
      "Superar doce segundos no regenera vida ni resistencia ni reduce la espera de un incapacitado");
    ActionCommand rescue; rescue.interact = true;
    simulation.Step(0.0625f, {{2, rescue}});
    Require(target.health == GameplayConfig::RevivedHealth && !target.incapacitated && target.lifeState == LifeState::Alive && target.healthRecoveryRemaining == 12.f,
      "Sólo el rescate explícito recupera al compañero y reinicia su espera de salud");
    for (int step = 0; step < 192; ++step) simulation.Step(0.0625f, {});
    Require(target.health == GameplayConfig::RevivedHealth, "Un compañero rescatado espera doce segundos nuevos antes de regenerar");
    simulation.Step(0.0625f, {});
    Require(target.health > GameplayConfig::RevivedHealth, "El compañero vuelve a la recuperación pasiva después del rescate y su espera completa");

    auto &expired = Fixture(simulation, true);
    for (auto &agent : expired.agents) agent.privateOrder = PrivateOrder::CopyArchive;
    auto &down = expired.agents[0]; down.health = 90.f; down.stamina = 0.f;
    down.lifeState = LifeState::Incapacitated; down.incapacitated = true; down.incapacityRemaining = 55.f; down.healthRecoveryRemaining = 0.f;
    for (int step = 0; step < 879; ++step) simulation.Step(0.0625f, {});
    Require(down.health == 90.f && down.stamina == 0.f && down.incapacitated && down.incapacityRemaining == 0.0625f && expired.phase == GamePhase::Mission,
      "La inconsciencia con salud positiva tampoco se recupera al esperar ni al regenerar resistencia");
    simulation.Step(0.0625f, {});
    Require(down.health == 0.f && down.lifeState == LifeState::Dead && down.rescueExpired && expired.phase == GamePhase::Mission && expired.agents[1].alive,
      "Al cumplirse exactamente cincuenta y cinco segundos vence el rescate individual y el compañero conserva la misión");
    for (int step = 0; step < 240; ++step) simulation.Step(0.0625f, {});
    Require(down.health == 0.f && down.stamina == 0.f && down.lifeState == LifeState::Dead && expired.phase == GamePhase::Mission, "La continuidad del compañero no regenera ni reanima al agente muerto");

    auto &solo = Fixture(simulation); solo.agents[0].health = 0.f; solo.agents[0].stamina = 0.f; solo.agents[0].healthRecoveryRemaining = 0.f;
    simulation.Step(0.0625f, {});
    Require(solo.agents[0].lifeState == LifeState::Dead && solo.agents[0].health == 0.f && solo.agents[0].stamina == 0.f && solo.phase == GamePhase::Results,
      "La salud cero individual se confirma antes de regeneración o resistencia aunque el temporizador esté listo");
  }

  void TrainingRetainsSpecialHealthRecovery() {
    Simulation simulation; simulation.ResetBase();
    auto &world = const_cast<WorldState &>(simulation.GetState());
    auto &agent = world.agents[0]; agent.health = 80.f;
    simulation.Step(0.1f, {});
    Require(agent.health == 80.f + TrainingConfig::HealthRecoveryPerSecond * 0.1f && agent.healthRecoveryRemaining == 12.f,
      "La sala de espera conserva su recuperación inmediata especial sin sumar la regeneración lenta de misión");
    agent.health = 0.f; simulation.Step(0.1f, {});
    Require(agent.alive && !agent.incapacitated && agent.lifeState == LifeState::Alive && agent.health > 0.f && world.phase == GamePhase::Base,
      "La salud cero de entrenamiento mantiene su protección sin activar muerte de misión");
    agent.healthRecoveryRemaining = 0.f; simulation.Deploy(7);
    Require(agent.health == GameplayConfig::MaxHealth && agent.healthRecoveryRemaining == 12.f, "Desplegar descarta cualquier espera agotada en prácticas");
    agent.health = 80.f; agent.healthRecoveryRemaining = 0.f; simulation.ReturnToBase();
    Require(agent.health == GameplayConfig::MaxHealth && agent.healthRecoveryRemaining == 12.f && world.phase == GamePhase::Base,
      "Volver a la sala reinicia salud y espera sin trasladar una recuperación de misión");
  }
}

int main() {
  try {
    LogicalVolumes();
    LethalAndNonLethalZones();
    PartialHitKeepsTheGuardsCurrentFloor();
    BulletOcclusionAndPosture();
    ProneProtectionAndMelee();
    MissionSurvivabilityAndDiscreteDamage();
    FriendlyZonesAndSnapshots();
    PassiveRecoveryThresholdAndFraction();
    NewImpactsRestartRecovery();
    RecoveryNeverReanimatesAndKeepsRescueDeadline();
    TrainingRetainsSpecialHealthRecovery();
    std::cout << "PASS combate: " << checks << " comprobaciones de zonas, postura, trayectoria, protección tumbado y recuperación de vida\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL combate tras " << checks << " comprobaciones: " << error.what() << '\n';
    return 1;
  }
}
