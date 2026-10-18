#pragma once

#include "NoTraceSimulation.h"
#include "NoTraceSpatialGeometry.h"
#include "NoTraceNavigation.h"
#include "NoTraceTraversal.h"
#include "NoTraceVisibility.h"
#include "NoTraceCombatGeometry.h"
#include "NoTraceFurniture.h"
#include "NoTraceInteraction.h"
#include <iostream>
#include <stdexcept>

namespace NoTrace::MultilevelTests {
  inline void Require(bool condition, const char *message) { if (!condition) throw std::runtime_error(message); }
  inline WorldState &QuietMission(Simulation &simulation) {
    simulation.ResetBase(); simulation.Deploy(2);
    auto &world = const_cast<WorldState &>(simulation.GetState());
    world.guards.clear(); world.securityEntries.clear();
    for (auto &object : world.objects) if (object.kind == ObjectKind::Camera || object.kind == ObjectKind::Explosive) object.active = false;
    return world;
  }
  inline void Replica(const Simulation &simulation) {
    Simulation replica; std::string error;
    if (!replica.LoadSnapshot(simulation.SnapshotFor(1), error)) throw std::runtime_error("Snapshot multinivel: " + error);
    Require(replica.GetState().agents.front().supportId == simulation.GetState().agents.front().supportId &&
      replica.GetState().agents.front().elevation == simulation.GetState().agents.front().elevation, "La réplica cambia el apoyo o la elevación");
  }
  inline void BudgetAndProtocol() {
    for (const std::size_t count : std::array<std::size_t, 3>{1, 3, GameplayConfig::MaximumPlayers}) {
      Simulation simulation; std::vector<PlayerProfile> players;
      for (unsigned int index = 0; index < count; ++index) players.push_back({index + 1, "Agente"});
      simulation.ResetBase(players); simulation.Deploy(17);
      const auto full = simulation.SnapshotFor(1), filtered = simulation.SnapshotFor(1, true);
      std::cout << "Multinivel agentes=" << count << " completo=" << full.dump().size() << " filtrado=" << filtered.dump().size() << " bytes\n";
      Require(full.dump().size() < GameplayConfig::MaximumSnapshotBytes && filtered.dump().size() < GameplayConfig::MaximumSnapshotBytes, "El nivel y sus agentes exceden el transporte");
      Replica(simulation);
      for (unsigned int recipient = 1; recipient <= count; ++recipient) {
        Simulation replica; std::string error;
        const auto own = simulation.SnapshotFor(recipient, true);
        if (!replica.LoadSnapshot(own, error)) throw std::runtime_error("Destinatario multinivel: " + error);
        auto old = own; old["version"] = 10;
        Require(!replica.LoadSnapshot(old, error), "El esquema espacial acepta el protocolo anterior");
        auto malformed = own; malformed["surfaces"][0][2] = 99;
        Require(!replica.LoadSnapshot(malformed, error), "El cliente acepta un tipo de superficie inexistente");
        malformed = own; malformed["grips"][0][8] = 7.f;
        Require(!replica.LoadSnapshot(malformed, error), "El cliente acepta una normal de agarre inválida");
      }
    }
  }
  inline void MilitaryMapAndStairs() {
    Simulation simulation; auto &world = QuietMission(simulation); auto &agent = world.agents.front();
    Require(world.walkSurfaces.size() >= 20 && world.elevators.size() == 1 && world.gripSurfaces.size() == 3, "El nivel no contiene plantas, ascensor y agarres reales");
    for (const auto id : {11u, 12u, 16u, 17u}) Require(std::ranges::any_of(world.objects, [id](const Interactable &object) { return object.id == id; }), "El mapa cambia los identificadores de los objetivos");
    for (const auto target : {10u, 11u, 13u, 20u, 21u, 23u, 24u}) Require(!Navigation::Route(world, 1, target).empty(), "Una zona militar carece de conexión navegable");
    agent.position = {240.f, 510.f}; agent.elevation = 0.f; agent.supportId = 1;
    ActionCommand move; move.moveY = -1.f;
    float previous = 0.f;
    for (unsigned int frame = 0; frame < 135; ++frame) {
      simulation.Step(1.f / 60.f, {{1, move}});
      Require(agent.elevation >= previous - 0.01f && agent.elevation - previous <= GameplayConfig::WalkSpeed / 60.f + 0.01f, "La escalera teletransporta o invierte la altura al subir");
      Require(!WorldGeometry::PositionBlocked(world, agent.position, GameplayConfig::AgentRadius, agent.elevation, WorldGeometry::ActorHeight(agent), agent.supportId), "La escalera introduce al agente en un sólido");
      previous = agent.elevation;
    }
    Require(agent.elevation == 96.f && agent.supportId == 10, "Los controles no alcanzan el descansillo de la primera planta"); Replica(simulation);
    move.moveY = 1.f;
    for (unsigned int frame = 0; frame < 135; ++frame) simulation.Step(1.f / 60.f, {{1, move}});
    Require(agent.elevation == 0.f && agent.supportId == 1, "Bajar la escalera no recupera el suelo real");
    agent.position = {1510.f, 460.f}; agent.elevation = 0.f; agent.supportId = 1;
    move = {}; move.moveX = -1.f; previous = 0.f;
    for (unsigned int frame = 0; frame < 135; ++frame) {
      simulation.Step(1.f / 60.f, {{1, move}});
      Require(agent.elevation >= previous - 0.01f && !WorldGeometry::PositionBlocked(world, agent.position, GameplayConfig::AgentRadius, agent.elevation, WorldGeometry::ActorHeight(agent), agent.supportId), "La escalera este atraviesa su losa o una pasarela superpuesta");
      previous = agent.elevation;
    }
    Require(agent.elevation == 96.f && agent.supportId == 11, "La escalera este no desembarca en la sala de control");
    move.moveX = 1.f;
    for (unsigned int frame = 0; frame < 135; ++frame) simulation.Step(1.f / 60.f, {{1, move}});
    Require(agent.elevation == 0.f && agent.supportId == 1, "La escalera este bloquea el descenso bajo otra pasarela");
    agent.position = {650.f, 260.f}; agent.elevation = 96.f; agent.supportId = 10;
    move = {}; move.moveX = 1.f;
    for (unsigned int frame = 0; frame < 100; ++frame) simulation.Step(1.f / 60.f, {{1, move}});
    Require(agent.supportId == 11 && agent.elevation == 96.f, "El puente no conecta las dos alas sin bajar de planta");
    agent.position = {1040.f, 700.f}; agent.elevation = 96.f; agent.supportId = 13;
    move = {}; move.moveY = -1.f;
    for (unsigned int frame = 0; frame < 120; ++frame) simulation.Step(1.f / 60.f, {{1, move}});
    Require(agent.position.y >= 670.f && agent.elevation == 96.f, "Caminar sale del borde y cambia de planta sin enlace");
    Replica(simulation);
  }
  inline void GuardNavigationFollowsStairs() {
    Simulation simulation; auto &world = QuietMission(simulation);
    world.agents.front().position = {80.f, 1080.f}; world.agents.front().supportId = 5;
    Guard guard; guard.id = 99; guard.position = {240.f, 510.f}; guard.facing = {0.f, -1.f}; guard.supportId = 1;
    guard.patrol = {{240.f, 270.f}}; guard.patrolElevations = {96.f}; world.guards.push_back(guard);
    const auto follow = [&](sf::Vector2f target, float targetZ) {
      auto &moving = world.guards.front(); moving.patrol = {target}; moving.patrolElevations = {targetZ}; moving.waypoint = 0;
      for (unsigned int frame = 0; frame < 600 && ((moving.position - target).length() > GameplayConfig::PatrolWaypointDistance || std::abs(moving.elevation - targetZ) > LevelConfig::SupportTolerance); ++frame) {
        const float previous = moving.elevation;
        simulation.Step(1.f / 60.f, {});
        Require(std::abs(moving.elevation - previous) <= GameplayConfig::GuardSpeed / 60.f + 0.01f, "La navegación del guardia teletransporta su elevación");
        Require(!WorldGeometry::PositionBlocked(world, moving.position, GameplayConfig::AgentRadius, moving.elevation, CombatConfig::StandingHeight, moving.supportId), "El guardia atraviesa un sólido al usar la escalera");
      }
      if ((moving.position - target).length() > GameplayConfig::PatrolWaypointDistance || std::abs(moving.elevation - targetZ) > LevelConfig::SupportTolerance)
        std::cout << "Guardia en " << moving.position.x << ',' << moving.position.y << ',' << moving.elevation << " apoyo=" << moving.supportId << " objetivo=" << target.x << ',' << target.y << ',' << targetZ << '\n';
      Require((moving.position - target).length() <= GameplayConfig::PatrolWaypointDistance && std::abs(moving.elevation - targetZ) <= LevelConfig::SupportTolerance, "El guardia no sigue físicamente un enlace de escalera entre plantas");
    };
    follow({240.f, 270.f}, 96.f); follow({240.f, 510.f}, 0.f);
    world.guards.front().position = {1360.f, 350.f}; world.guards.front().elevation = 96.f; world.guards.front().supportId = 15;
    world.guards.front().navigation.clear(); world.guards.front().patrolPauseRemaining = 0.f;
    follow({1360.f, 120.f}, 192.f); follow({1360.f, 350.f}, 96.f);
  }
  inline void PillarsSupportAndOccludePhysically() {
    Simulation simulation; auto &world = QuietMission(simulation); auto &agent = world.agents.front();
    const auto *floor = WorldGeometry::FindSurface(world, 10);
    Require(floor != nullptr, "Los pilares carecen de planta superior");
    unsigned int pillars = 0;
    for (const auto &pillar : world.obstacles) if (pillar.name == "Pilar exterior del archivo") {
      ++pillars;
      const auto center = pillar.bounds.position + pillar.bounds.size * .5f;
      const float top = pillar.elevation + WorldGeometry::ObstacleHeight(pillar);
      Require(pillar.bounds.size == sf::Vector2f{LevelConfig::PillarWidth, LevelConfig::PillarWidth} && pillar.elevation == 0.f &&
        top == floor->elevation - floor->thickness && WorldGeometry::BlocksMovement(pillar) && WorldGeometry::BlocksVision(pillar), "El pilar no sostiene físicamente la cara inferior de la planta");
      Require(WorldGeometry::ContainsSupport(*floor, pillar.bounds.position) && WorldGeometry::ContainsSupport(*floor, pillar.bounds.position + pillar.bounds.size), "El pilar sobresale de la estructura que sostiene");
      for (const auto &other : world.obstacles) if (&other != &pillar && WorldGeometry::HeightOverlap(pillar.elevation, top, other.elevation, other.elevation + WorldGeometry::ObstacleHeight(other)))
        Require(!pillar.bounds.findIntersection(other.bounds), "El pilar invade otro sólido a su altura");
      for (const auto &object : world.objects) if (WorldGeometry::BlocksMovement(object) && WorldGeometry::HeightOverlap(pillar.elevation, top, object.elevation, object.elevation + WorldGeometry::ObjectHeight(object)))
        Require(!pillar.bounds.findIntersection(WorldGeometry::ObjectFootprint(object)), "El pilar invade un objetivo o un mueble");
      Require(WorldGeometry::PositionBlocked(world, center, GameplayConfig::AgentRadius, 0.f, CombatConfig::StandingHeight, 1) &&
        !WorldGeometry::PositionBlocked(world, center, GameplayConfig::AgentRadius, floor->elevation, CombatConfig::StandingHeight, floor->id), "El pilar pierde su colisión o bloquea indebidamente la planta superior");
      const sf::Vector3f from{pillar.bounds.position.x - 4.f, center.y, CombatConfig::StandingWeaponHeight};
      const sf::Vector3f to{pillar.bounds.position.x + pillar.bounds.size.x + 4.f, center.y, CombatConfig::StandingWeaponHeight};
      Require(WorldGeometry::RayOccluded(world, from, to) && !WorldGeometry::RayOccluded(world, from + sf::Vector3f{0.f, 0.f, floor->elevation}, to + sf::Vector3f{0.f, 0.f, floor->elevation}), "El pilar no ocluye a su altura o tapa otra planta");
      agent.position = center - sf::Vector2f{40.f, 0.f}; agent.elevation = 0.f; agent.supportId = 1;
      ActionCommand move; move.moveX = 1.f;
      for (unsigned int frame = 0; frame < 60; ++frame) simulation.Step(1.f / 60.f, {{agent.id, move}});
      Require(agent.position.x <= pillar.bounds.position.x - GameplayConfig::AgentRadius + LevelConfig::SupportTolerance &&
        !WorldGeometry::PositionBlocked(world, agent.position, GameplayConfig::AgentRadius, agent.elevation, CombatConfig::StandingHeight, agent.supportId), "Los controles atraviesan el pilar o dejan al agente penetrando el sólido");
    }
    Require(pillars == 2, "La base no contiene los dos pilares físicos del archivo"); Replica(simulation);
  }
  inline void GripTransferClimbAndDrop() {
    Simulation simulation; auto &world = QuietMission(simulation); auto &agent = world.agents.front();
    agent.position = {340.f, 326.f}; agent.elevation = 0.f; agent.supportId = 1;
    Require(Traversal::CanTraverse(world, agent) && !Traversal::Prompt(world, agent).empty(), "El agarre válido no comparte consulta y pista");
    ActionCommand attach; attach.traverse = true; simulation.Step(1.f / 60.f, {{1, attach}});
    for (unsigned int frame = 0; frame < 120 && agent.traversal.mode != TraversalMode::Hang; ++frame) { simulation.Step(1.f / 60.f, {}); Replica(simulation); }
    Require(agent.traversal.mode == TraversalMode::Hang && agent.traversal.surfaceId == 41 && agent.supportId == 0, "La pared no sostiene físicamente al agente");
    ActionCommand shimmy; shimmy.moveX = 1.f; shimmy.run = true;
    const auto noiseCount = world.noises.size(); const float start = agent.position.x;
    for (unsigned int frame = 0; frame < 100; ++frame) simulation.Step(1.f / 60.f, {{1, shimmy}});
    Require(agent.position.x > start + 50.f && agent.position.x < 410.f && agent.elevation == 18.f && !agent.running, "El movimiento de agarre utiliza la carrera terrestre o pierde altura");
    Require(world.noises.size() == noiseCount, "El agarre genera pasos normales");
    for (unsigned int frame = 0; frame < 150; ++frame) simulation.Step(1.f / 60.f, {{1, shimmy}});
    Require(Traversal::CanTraverse(world, agent), "No se alcanza la transferencia vertical desde el mismo agarre");
    simulation.Step(1.f / 60.f, {{1, attach}});
    for (unsigned int frame = 0; frame < 180 && agent.traversal.mode != TraversalMode::Hang; ++frame) simulation.Step(1.f / 60.f, {});
    Require(agent.traversal.surfaceId == 42 && agent.elevation == 96.f, "La transferencia no conserva el agarre superior"); Replica(simulation);
    Require(Traversal::CanTraverse(world, agent), "El saliente no ofrece subida contextual");
    simulation.Step(1.f / 60.f, {{1, attach}});
    for (unsigned int frame = 0; frame < 180 && agent.traversal.mode != TraversalMode::Grounded; ++frame) { simulation.Step(1.f / 60.f, {}); Replica(simulation); }
    Require(agent.traversal.mode == TraversalMode::Grounded && agent.supportId == 10 && agent.elevation == 96.f && agent.position.y <= 272.f, "La subida atraviesa el parapeto o no aterriza sobre el apoyo");
    Require(Traversal::CanTraverse(world, agent), "El saliente no permite volver al agarre");
    simulation.Step(1.f / 60.f, {{1, attach}});
    for (unsigned int frame = 0; frame < 180 && agent.traversal.mode != TraversalMode::Hang; ++frame) simulation.Step(1.f / 60.f, {});
    Require(agent.traversal.surfaceId == 42, "El recorrido inverso del saliente no es real");
    for (unsigned int frame = 0; frame < 160; ++frame) simulation.Step(1.f / 60.f, {{1, shimmy}});
    Require(agent.position.x == 600.f && Traversal::CanTraverse(world, agent), "El saliente no enlaza con el tubo real");
    simulation.Step(1.f / 60.f, {{1, attach}});
    for (unsigned int frame = 0; frame < 120 && agent.traversal.mode != TraversalMode::Hang; ++frame) simulation.Step(1.f / 60.f, {});
    Require(agent.traversal.surfaceId == 43 && agent.position.y == 330.f && agent.elevation == 96.f, "La transferencia al tubo atraviesa el parapeto o pierde el contacto");
    for (unsigned int frame = 0; frame < 350; ++frame) simulation.Step(1.f / 60.f, {{1, shimmy}});
    Require(agent.position.x == 820.f && agent.traversal.surfaceId == 43 && Traversal::CanTraverse(world, agent), "Los controles no recorren el tubo hasta su salida");
    simulation.Step(1.f / 60.f, {{1, attach}});
    for (unsigned int frame = 0; frame < 180 && agent.traversal.mode != TraversalMode::Grounded; ++frame) simulation.Step(1.f / 60.f, {});
    Require(agent.supportId == 11 && agent.elevation == 96.f, "El tubo no permite subir a la otra ala"); Replica(simulation);
    simulation.Step(1.f / 60.f, {{1, attach}});
    for (unsigned int frame = 0; frame < 180 && agent.traversal.mode != TraversalMode::Hang; ++frame) simulation.Step(1.f / 60.f, {});
    shimmy.moveX = -1.f;
    for (unsigned int frame = 0; frame < 350; ++frame) simulation.Step(1.f / 60.f, {{1, shimmy}});
    Require(agent.position.x == 600.f && Traversal::CanTraverse(world, agent), "El recorrido inverso del tubo no vuelve al saliente");
    simulation.Step(1.f / 60.f, {{1, attach}});
    for (unsigned int frame = 0; frame < 120 && agent.traversal.mode != TraversalMode::Hang; ++frame) simulation.Step(1.f / 60.f, {});
    Require(agent.traversal.surfaceId == 42, "El tubo no transfiere de vuelta a la pared");
    ActionCommand drop; drop.drop = true;
    Require(Traversal::CanTraverse(world, agent, true), "La caída válida no tiene destino físico");
    simulation.Step(1.f / 60.f, {{1, drop}});
    for (unsigned int frame = 0; frame < 150 && agent.traversal.mode != TraversalMode::Grounded; ++frame) simulation.Step(1.f / 60.f, {});
    Require(agent.supportId == 1 && agent.elevation == 0.f && agent.health < GameplayConfig::MaxHealth, "Soltarse no cae al suelo o evita el daño por altura");
    Replica(simulation);
  }
  inline void ElevatorCarriesAndSnapshots() {
    Simulation simulation; auto &world = QuietMission(simulation); auto &agent = world.agents.front();
    agent.position = {1400.f, 690.f}; agent.elevation = 0.f; agent.supportId = 90;
    const auto hash = Level::GeometryHash(world);
    ActionCommand use; use.interact = true; simulation.Step(1.f / 60.f, {{1, use}});
    Require(world.elevators.front().requested, "E no solicita el ascensor real");
    bool between = false;
    for (unsigned int frame = 0; frame < 240; ++frame) {
      simulation.Step(1.f / 60.f, {});
      const auto &lift = world.elevators.front();
      Require(agent.elevation == lift.elevation && agent.supportId == 90, "La cabina deja atrás a su ocupante");
      if (lift.elevation > 0.f && lift.elevation < 96.f) { between = true; Replica(simulation); }
    }
    Require(between && world.elevators.front().phase == ElevatorPhase::Docked && agent.elevation == 96.f, "El ascensor salta de planta o no completa apertura y viaje");
    Require(hash == Level::GeometryHash(world), "La posición dinámica de la cabina cambia la huella estática del nivel");
    ActionCommand exit; exit.moveX = -1.f;
    for (unsigned int frame = 0; frame < 30; ++frame) simulation.Step(1.f / 60.f, {{1, exit}});
    Require(agent.supportId == 16 && agent.elevation == 96.f, "El ascensor no permite desembarcar por el muelle de la planta correcta");
    auto valid = simulation.SnapshotFor(1); Simulation replica; std::string error;
    Require(replica.LoadSnapshot(valid, error), "La réplica rechaza el desembarco válido");
    for (unsigned int mutation = 0; mutation < 6; ++mutation) {
      auto invalid = valid;
      if (mutation == 0) invalid["agents"][0]["elevation"] = 0.f;
      if (mutation == 1) invalid["surfaces"][0][7] = 900.f;
      if (mutation == 2) invalid["verticalLinks"][0][2] = 9999;
      if (mutation == 3) invalid["elevators"][0][2] = 9999;
      if (mutation == 4) invalid["agents"][0]["traversal"][0] = static_cast<int>(TraversalMode::Hang);
      if (mutation == 5) invalid["geometryHash"] = "0000000000000000";
      Require(!replica.LoadSnapshot(invalid, error), "El parser acepta geometría o apoyo manipulado");
      Require(replica.GetState().agents.front().elevation == 96.f, "Un rechazo espacial altera el estado previo");
    }
    Require(valid.dump().size() < GameplayConfig::MaximumSnapshotBytes, "El nivel completo excede el transporte");
  }
  inline void GuardBoardsAndLeavesElevator() {
    Simulation simulation; auto &world = QuietMission(simulation);
    world.agents.front().position = {80.f, 1080.f}; world.agents.front().supportId = 5;
    Guard guard; guard.id = 99; guard.position = {1340.f, 690.f}; guard.elevation = 0.f; guard.supportId = 5;
    guard.patrol = {{1330.f, 690.f}}; guard.patrolElevations = {96.f}; world.guards.push_back(guard);
    const auto follow = [&](float elevation, std::uint32_t support) {
      auto &moving = world.guards.front(); moving.patrolElevations = {elevation}; moving.patrolPauseRemaining = 0.f; moving.waypoint = 0;
      bool travelled = false;
      for (unsigned int frame = 0; frame < 1500 && (moving.supportId != support || std::abs(moving.elevation - elevation) > LevelConfig::SupportTolerance ||
          (moving.position - moving.patrol.front()).length() > GameplayConfig::PatrolWaypointDistance); ++frame) {
        const float previous = moving.elevation; simulation.Step(1.f / 60.f, {});
        travelled |= moving.supportId == 90 && world.elevators.front().phase == ElevatorPhase::Moving;
        Require(std::abs(moving.elevation - previous) <= GameplayConfig::GuardSpeed / 60.f + 0.01f, "La cabina teletransporta al guardia entre plantas");
        Require(!WorldGeometry::PositionBlocked(world, moving.position, GameplayConfig::AgentRadius, moving.elevation, CombatConfig::StandingHeight, moving.supportId),
          "El guardia atraviesa una losa o el marco al viajar en ascensor");
      }
      if (!travelled || moving.supportId != support) std::cout << "Ascensor guardia pos=" << moving.position.x << ',' << moving.position.y << ',' << moving.elevation <<
        " apoyo=" << moving.supportId << " fase=" << static_cast<int>(world.elevators.front().phase) << " solicitado=" << world.elevators.front().requested << '\n';
      Require(travelled && moving.supportId == support && std::abs(moving.elevation - elevation) <= LevelConfig::SupportTolerance,
        "El guardia no embarca, viaja y desembarca físicamente mediante navegación del ascensor");
      Replica(simulation);
    };
    follow(96.f, 16); follow(0.f, 5);
  }
  inline void HangingVisibilityUsesRealObservers() {
    for (const bool camera : {false, true}) for (const bool occluded : {false, true}) {
      Simulation simulation; auto &world = QuietMission(simulation); auto &agent = world.agents.front();
      agent.position = {340.f, 326.f}; agent.elevation = 0.f; agent.supportId = 1;
      ActionCommand attach; attach.traverse = true; simulation.Step(1.f / 60.f, {{1, attach}});
      for (unsigned int frame = 0; frame < 120 && agent.traversal.mode != TraversalMode::Hang; ++frame) simulation.Step(1.f / 60.f, {});
      Require(agent.traversal.mode == TraversalMode::Hang && !agent.hidden, "El escenario no alcanza un agarre real antes de evaluar vigilancia");
      if (occluded) {
        Obstacle blocker{{{320.f, 350.f}, {40.f, 12.f}}, CoverKind::High, true, "Pantalla de vigilancia"}; blocker.height = 96.f; world.obstacles.push_back(blocker);
      }
      if (camera) {
        Interactable observer{901, ObjectKind::Camera, {340.f, 400.f}, "Cámara del agarre"}; observer.facing = {0.f, -1.f}; observer.supportId = 1;
        world.objects.push_back(observer);
      } else {
        Guard observer; observer.id = 99; observer.position = {340.f, 400.f}; observer.facing = {0.f, -1.f}; observer.supportId = 1; observer.attackCooldown = 60.f;
        world.guards.push_back(observer);
      }
      for (unsigned int frame = 0; frame < 300; ++frame) simulation.Step(1.f / 60.f, {});
      const bool confirmed = std::ranges::any_of(world.events, [](const MissionEvent &event) { return event.kind == EventKind::ConfirmedDetection || event.kind == EventKind::AgentAlarm; });
      Require(agent.traversal.mode == TraversalMode::Hang && agent.alive && !agent.incapacitated, "La vigilancia del agarre interrumpe el escenario por daño o caída artificial");
      Require(confirmed != occluded, "El guardia o la cámara ignoran la oclusión real o conceden invisibilidad al estado Hang");
      Require(occluded ? !world.alarm : world.alarm, "La alerta del observador no corresponde al contacto físico con el cuerpo colgado");
      Replica(simulation);
    }
  }
  inline void TableStatesAreDiscrete() {
    Simulation simulation; auto &world = QuietMission(simulation); auto &agent = world.agents.front();
    auto &table = *std::ranges::find(world.objects, 34u, &Interactable::id);
    agent.position = {390.f, 950.f}; agent.elevation = 0.f; agent.supportId = 5;
    Require(Furniture::CanFlipTable(world, agent, table), "La mesa accesible no ofrece un cambio físico válido");
    const auto position = table.position; const auto size = WorldGeometry::ObjectFootprint(table).size;
    ActionCommand flip; flip.tool = true; simulation.Step(1.f / 60.f, {{1, flip}});
    Require(table.overturned && !table.lifted && table.position == position && WorldGeometry::ObjectFootprint(table).size == SpatialConfig::OverturnedTableSize &&
      WorldGeometry::ObjectHeight(table) == SpatialConfig::OverturnedTableHeight && WorldGeometry::Properties(table).cover == CoverKind::None, "La mesa volcada mezcla estados o no cambia huella y altura en el mismo tick");
    Require(!Interaction::CanEnterHideout(world, agent, table), "La mesa volcada conserva ocultación");
    Require(WorldGeometry::PositionBlocked(world, table.position, GameplayConfig::AgentRadius, 0.f, CombatConfig::ProneHeight, table.supportId), "La mesa volcada pierde su colisión física");
    Replica(simulation);
    simulation.Step(1.f / 60.f, {}); Require(table.overturned, "El estado de la mesa interpola o se revierte sin nueva pulsación");
    simulation.Step(1.f / 60.f, {{1, flip}});
    Require(!table.overturned && !table.lifted && WorldGeometry::ObjectFootprint(table).size == size && Interaction::CanEnterHideout(world, agent, table), "Restablecer la mesa no recupera exactamente la geometría normal");
    ActionCommand use; use.interact = true; simulation.Step(1.f / 60.f, {{1, use}});
    Require(agent.hidden && agent.actionPose == ActionPose::None && agent.transitionTime == 0.f, "Entrar bajo mesa introduce una transición corporal");
    simulation.Step(1.f / 60.f, {{1, flip}});
    Require(!table.overturned && table.occupantAgent == agent.id, "Una mesa ocupada se vuelca encima de su ocupante");
    auto invalid = simulation.SnapshotFor(1); for (auto &value : invalid["objects"]) if (value["id"] == 34) value["overturned"] = true;
    Simulation replica; std::string error; Require(!replica.LoadSnapshot(invalid, error), "El parser acepta una mesa volcada y ocupada");
    const auto hiddenPosition = agent.position, bodyFacing = agent.facing;
    // La salida sigue el ratón aunque WASD solicite el lado opuesto.
    use.aimX = -1.f; use.moveX = 1.f; simulation.Step(1.f / 60.f, {{1, use}});
    Require(!agent.hidden && agent.actionPose == ActionPose::None && agent.transitionTime == 0.f, "Salir de mesa no conserva el cambio instantáneo y válido");
    Require(agent.hideoutId == 0 && !WorldGeometry::HideoutOccupied(table) && WeaponFacing(agent) == sf::Vector2f{-1.f, 0.f} && agent.facing == bodyFacing &&
      agent.position.x < hiddenPosition.x && std::abs(agent.position.y - hiddenPosition.y) < SpatialConfig::CollisionSkin, "La salida de mesa no sigue el arma o WASD gira el cuerpo oculto");
    Require(!WorldGeometry::PositionBlocked(world, agent.position, GameplayConfig::AgentRadius, agent.elevation, WorldGeometry::ActorHeight(agent), agent.supportId) &&
      Interaction::HideoutPathClear(world, table, hiddenPosition, agent.position), "La salida de mesa atraviesa geometría o termina sin una huella libre"); Replica(simulation);
  }
  inline void CombatCannotCrossFloors() {
    WorldState world; world.phase = GamePhase::Mission;
    Agent agent; agent.position = {300.f, 300.f}; agent.supportId = 1;
    Guard guard; guard.id = 1; guard.position = {324.f, 300.f}; guard.elevation = 96.f; guard.supportId = 2;
    world.agents.push_back(agent); world.guards.push_back(guard);
    world.walkSurfaces = {{1, 0, WalkSurfaceKind::Floor, {{0.f, 0.f}, world.size}, 0.f}, {2, 1, WalkSurfaceKind::Floor, {{200.f, 200.f}, {200.f, 200.f}}, 96.f}};
    Require(!Simulation::FindCqcTarget(world, agent, Simulation::CqcAction::Grab) && !Simulation::FindCqcTarget(world, agent, Simulation::CqcAction::Push), "El CQC alcanza a un objetivo en otra planta");
    Require(!CombatGeometry::IntersectActor(agent.position, {1.f, 0.f}, CombatConfig::StandingWeaponHeight, 0.f, 100.f, guard.position, guard.facing, Stance::Standing, false, guard.elevation), "Una bala horizontal impacta un volumen de otra planta");
    world.guards.front().elevation = 0.f; world.guards.front().supportId = 1;
    Require(Simulation::FindCqcTarget(world, agent, Simulation::CqcAction::Grab).has_value(), "La protección entre plantas impide el agarre válido en el mismo suelo");
  }
  inline void Run() {
    BudgetAndProtocol(); TableStatesAreDiscrete(); CombatCannotCrossFloors(); MilitaryMapAndStairs(); GuardNavigationFollowsStairs(); PillarsSupportAndOccludePhysically(); GripTransferClimbAndDrop(); ElevatorCarriesAndSnapshots(); GuardBoardsAndLeavesElevator(); HangingVisibilityUsesRealObservers();
    std::cout << "PASS multinivel: escaleras, puentes, agarre, transferencia, caída, ascensor y validación de snapshots\n";
  }
}
