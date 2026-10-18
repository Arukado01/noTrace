#pragma once

#include "NoTraceSimulation.h"
#include "NoTraceSpatialFixture.h"
#include "NoTraceTraversal.h"
#include "Config/NoTraceSecurityConfig.h"
#include <cmath>
#include <limits>
#include <stdexcept>

namespace EngineTests {
  inline int RunRecoverySimulationFixtures() {
    using namespace NoTrace;
    int checks = 0;
    const auto require = [&](bool condition, const char *message) {
      ++checks;
      if (!condition) throw std::runtime_error(message);
    };
    const auto scene = [](Simulation &simulation) -> WorldState & {
      simulation.ResetBase({{1, "Uno"}, {2, "Dos"}}); simulation.Deploy(0);
      auto &world = const_cast<WorldState &>(simulation.GetState()); TestFixture::LegacyPlane(world);
      world.objects.clear(); world.obstacles.clear(); world.guards.clear(); world.securityEntries.clear(); world.noises.clear();
      for (auto &agent : world.agents) agent.privateOrder = PrivateOrder::CopyArchive;
      world.agents[0].position = {400.f, 400.f}; world.agents[1].position = {365.f, 400.f};
      return world;
    };
    for (const auto stance : {Stance::Standing, Stance::Crouched, Stance::Prone}) {
      Simulation simulation, replica;
      auto &world = scene(simulation); auto &victim = world.agents[0];
      victim.stance = stance; victim.health = 0.f;
      simulation.Step(1.f / 60.f, {});
      require(victim.lifeState == LifeState::Incapacitated && victim.actionPose == ActionPose::Fall && victim.incapacityRemaining == GameplayConfig::IncapacityDuration,
          "La autoridad no adquiere la caída y el plazo de rescate antes de incorporar al agente");
      ActionCommand rescue; rescue.interact = true;
      simulation.Step(1.f / 60.f, {{2, rescue}});
      require(victim.lifeState == LifeState::Alive && !victim.incapacitated && victim.actionPose == ActionPose::Revive &&
          std::abs(victim.actionPoseTime - GameplayConfig::StanceTransitionDuration) < .000001f,
          "El rescate deja de recuperar al compañero por usar su transición visual");
      require(victim.stance == stance && victim.transitionTime == 0.f && victim.transitionFrom == stance,
          "La incorporación dedicada conserva una transición StandUp incompatible o cambia la postura de destino");
      const auto remaining = victim.actionPoseTime;
      std::string error;
      auto snapshot = simulation.SnapshotFor(2, true);
      require(replica.LoadSnapshot(snapshot, error), "La réplica rechaza la recuperación autoritativa del compañero visible");
      require(replica.GetState().agents[0].stance == stance && replica.GetState().agents[0].actionPoseTime == remaining &&
          replica.GetState().agents[0].actionPose == ActionPose::Revive, "La réplica altera la fase o destino de recuperación");
      for (float invalid : {-.01f, GameplayConfig::StanceTransitionDuration + .01f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
        auto malformed = snapshot; malformed["agents"][0]["actionPoseTime"] = invalid;
        require(!replica.LoadSnapshot(malformed, error) && replica.GetState().agents[0].actionPoseTime == remaining,
            "La réplica acepta una incorporación imposible o reemplaza el último estado válido");
      }
      for (unsigned int frame = 0; frame < 20; ++frame) {
        const auto before = victim.actionPoseTime;
        simulation.Step(1.f / 60.f, {});
        require(std::abs(victim.actionPoseTime - std::max(0.f, before - 1.f / 60.f)) < .000001f,
            "El rescate no consume una sola vez el tiempo de incorporación");
        if (victim.actionPoseTime > 0.f) require(victim.actionPose == ActionPose::Revive, "Otra pose interrumpe la incorporación antes de su plazo");
      }
      require(victim.stance == stance && victim.actionPoseTime == 0.f && victim.actionPose == ActionPose::None,
          "La recuperación no termina en la postura autoritativa elegida");
      victim.health = 0.f; simulation.Step(1.f / 60.f, {});
      require(victim.lifeState == LifeState::Incapacitated && victim.actionPose == ActionPose::Fall, "Una nueva caída no sustituye la pose final de recuperación");
      simulation.Step(1.f / 60.f, {{2, rescue}});
      require(victim.actionPose == ActionPose::Revive && victim.actionPoseTime > 0.f, "El segundo rescate no inicia una nueva incorporación");
      victim.health = 0.f; simulation.Step(1.f / 60.f, {});
      require(victim.lifeState == LifeState::Incapacitated && victim.actionPose == ActionPose::Fall && victim.incapacityRemaining == GameplayConfig::IncapacityDuration,
          "Una nueva incapacidad conserva la pose Revive o pierde el plazo real de rescate");
    }
    for (const auto stance : {Stance::Standing, Stance::Crouched}) {
      Simulation simulation, replica;
      auto &world = scene(simulation); auto &victim = world.agents[0];
      victim.stance = stance; victim.health = 0.f;
      simulation.Step(1.f / 60.f, {});
      ActionCommand rescue; rescue.interact = true;
      simulation.Step(1.f / 60.f, {{2, rescue}});
      require(victim.actionPose == ActionPose::Revive && victim.actionPoseTime > 0.f, "El fixture de agarre no adquiere una incorporación por rescate real");
      ActionCommand grab; grab.traverse = true;
      const float before = victim.actionPoseTime;
      require(!Traversal::CanTraverse(world, victim), "El intento inválido ya tiene una conexión de agarre");
      simulation.Step(1.f / 60.f, {{1, grab}});
      require(victim.traversal.mode == TraversalMode::Grounded && victim.actionPose == ActionPose::Revive &&
        std::abs(victim.actionPoseTime - (before - 1.f / 60.f)) < .000001f, "Un B inválido cancela o congela la incorporación del rescatado");
      world.walkSurfaces.push_back({1, 0, WalkSurfaceKind::Floor, {{0.f, 0.f}, world.size}, 0.f});
      for (auto &agent : world.agents) agent.supportId = 1;
      Obstacle wall{{{390.f, 358.f}, {220.f, 16.f}}, CoverKind::High, true, "Muro de recuperación"};
      wall.height = 96.f; world.obstacles.push_back(wall);
      world.gripSurfaces.push_back({41, GripKind::Wall, {400.f, 374.f, 64.f}, {600.f, 374.f, 64.f}, {0.f, 1.f}});
      const auto contact = Traversal::BodyAt(world.gripSurfaces[0], 0.f);
      world.verticalLinks.push_back({20, VerticalLinkKind::Attach, 1, 0, 0, 41, {400.f, 400.f, 0.f}, contact, 24.f, true});
      require(Traversal::CanTraverse(world, victim), "El rescatado no dispone de una conexión física válida con el muro");
      const auto origin = WorldGeometry::Feet(victim);
      simulation.Step(1.f / 60.f, {{1, grab}});
      require(victim.traversal.mode == TraversalMode::Attach && victim.traversal.linkId == 20 && victim.traversal.progress > 0.f &&
        victim.actionPose == ActionPose::None && victim.actionPoseTime == 0.f, "Un agarre adquirido realmente conserva la pose Revive y oculta su animación");
      require(WorldGeometry::SpatialDistance(origin, WorldGeometry::Feet(victim)) <= TraversalConfig::TransitionSpeed / 60.f + .001f &&
        !WorldGeometry::PositionBlocked(world, victim.position, GameplayConfig::AgentRadius, victim.elevation, CombatConfig::StandingHeight),
        "Cancelar la incorporación teletransporta al rescatado o atraviesa el muro durante el agarre");
      std::string error;
      require(replica.LoadSnapshot(simulation.SnapshotFor(2), error) && replica.GetState().agents[0].traversal.mode == TraversalMode::Attach &&
        replica.GetState().agents[0].actionPose == ActionPose::None && replica.GetState().agents[0].actionPoseTime == 0.f,
        "La réplica no conserva la adquisición de agarre y la cancelación de Revive");
    }
    Simulation simulation, replica;
    auto &world = scene(simulation);
    Guard witness; witness.id = 89; witness.position = {460.f, 400.f}; witness.facing = {-1.f, 0.f}; witness.suspicion = 1.f; witness.attackCooldown = 60.f;
    world.guards.push_back(witness); simulation.Step(.02f, {});
    require(world.securityAlert == SecurityAlert::Red, "El fixture de incorporación no adquiere un contacto confirmado real");
    world.agents[0].hidden = world.agents[1].hidden = true;
    simulation.Step(.02f, {}); world.guards.clear();
    require(world.securityAlert == SecurityAlert::Orange && world.securityRemaining == SecurityConfig::SearchDuration,
        "El fixture de incorporación no inicia los treinta segundos autoritativos de búsqueda");
    Guard guard; guard.id = 90; guard.position = {430.f, 400.f}; guard.facing = {-1.f, 0.f};
    guard.state = GuardState::Incapacitated; guard.incapacityRemaining = .001f; guard.stun = CombatConfig::MaxGuardStun;
    guard.lastKnown = world.agents[0].position; guard.attackCooldown = 0.f; guard.suspicion = 1.f;
    world.guards.push_back(guard);
    const auto position = guard.position; const float remaining = world.securityRemaining;
    simulation.Step(.02f, {});
    require(world.guards[0].state == GuardState::Search && world.guards[0].wakeRemaining > 0.f &&
        world.guards[0].wakeRemaining <= GameplayConfig::StanceTransitionDuration, "El guardia despierta sin una fase autoritativa de incorporación");
    for (unsigned int frame = 0; frame < 4; ++frame) {
      const float before = world.guards[0].wakeRemaining;
      simulation.Step(.02f, {});
      require(std::abs(world.guards[0].wakeRemaining - (before - .02f)) < .000001f,
          "El tiempo de incorporación no se descuenta una sola vez por paso");
      require(world.guards[0].position == position && world.guards[0].attackTime == 0.f && world.shots.empty(),
          "El guardia se desplaza o combate antes de terminar de incorporarse");
    }
    require(world.securityAlert == SecurityAlert::Orange && std::abs(world.securityRemaining - (remaining - .1f)) < .001f,
        "La incorporación reinicia o detiene el reloj naranja");
    std::string error;
    auto snapshot = simulation.SnapshotFor(1, true);
    require(snapshot["guards"].size() == 1 && replica.LoadSnapshot(snapshot, error), "El cliente visible rechaza el temporizador público de incorporación");
    require(replica.GetState().guards[0].wakeRemaining == world.guards[0].wakeRemaining,
        "La réplica pierde la fase de incorporación del guardia visible");
    for (float invalid : {-.01f, GameplayConfig::StanceTransitionDuration + .01f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
      auto malformed = snapshot; malformed["guards"][0]["wakeRemaining"] = invalid;
      require(!replica.LoadSnapshot(malformed, error), "La réplica acepta un temporizador de incorporación imposible");
    }
    auto incompatible = snapshot; incompatible["guards"][0]["state"] = static_cast<int>(GuardState::Incapacitated);
    incompatible["guards"][0]["incapacityRemaining"] = 5.f;
    require(!replica.LoadSnapshot(incompatible, error), "La réplica permite incorporar a un guardia que sigue inconsciente");
    auto legacy = snapshot; legacy["guards"][0].erase("wakeRemaining");
    require(replica.LoadSnapshot(legacy, error) && replica.GetState().guards[0].wakeRemaining == 0.f,
        "La extensión opcional invalida una réplica anterior sin incorporación");
    world.obstacles = {{{{412.f, 360.f}, {6.f, 80.f}}, CoverKind::High, true, "Oculta la recuperación"}};
    require(simulation.SnapshotFor(1, true)["guards"].empty(), "La señal de incorporación revela un guardia tras la pared");
    world.obstacles.clear();
    for (unsigned int frame = 0; frame < 20 && world.guards[0].wakeRemaining > 0.f; ++frame) simulation.Step(.02f, {});
    require(world.guards[0].wakeRemaining == 0.f, "El guardia no termina su incorporación en el plazo acotado");
    world.agents[0].hidden = world.agents[1].hidden = true;
    world.guards[0].state = GuardState::Patrol; world.guards[0].patrol = {position + sf::Vector2f{100.f, 0.f}};
    world.guards[0].waypoint = 0; world.guards[0].patrolPauseRemaining = 0.f;
    world.securityAlert = SecurityAlert::Calm; world.securityActive = world.alarm = false;
    simulation.Step(.02f, {});
    require(world.guards[0].position != position, "La incorporación terminada bloquea permanentemente la locomoción normal");
    world.guards[0].wakeRemaining = .1f; world.guards[0].state = GuardState::Incapacitated; world.guards[0].incapacityRemaining = 5.f;
    simulation.Step(.02f, {});
    require(world.guards[0].wakeRemaining == 0.f, "Una nueva inconsciencia conserva la incorporación anterior");
    world.guards[0].wakeRemaining = .1f; world.guards[0].state = GuardState::Dead; world.guards[0].health = 0.f;
    simulation.Step(.02f, {});
    require(world.guards[0].wakeRemaining == 0.f, "Un cadáver conserva una incorporación activa");
    return checks;
  }
}
