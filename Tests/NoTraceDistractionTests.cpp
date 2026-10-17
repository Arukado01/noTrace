#include "NoTraceDistractionGeometry.h"
#include "NoTraceSimulation.h"
#include "NoTraceVisibility.h"
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
  WorldState &Fixture(Simulation &simulation) {
    simulation.ResetBase(); simulation.Deploy(0);
    auto &world = const_cast<WorldState &>(simulation.GetState());
    world.objects.clear(); world.obstacles.clear(); world.guards.clear(); world.noises.clear(); world.shots.clear(); world.footsteps.clear();
    world.agents[0].position = {100.f, 500.f}; world.agents[0].facing = {1.f, 0.f};
    return world;
  }
  void Advance(Simulation &simulation, int frames) {
    for (int frame = 0; frame < frames; ++frame) simulation.Step(0.1f, {});
  }

  void SharedTrajectory() {
    WorldState world;
    auto path = DistractionGeometry::TraceDecoy(world, {100.f, 500.f}, {1.f, 0.f});
    Require(path.valid && !path.blocked && path.travelled == DistractionConfig::MaximumDecoyDistance && path.travelled > 170.f, "El alcance configurable debe ampliar el lanzamiento anterior");
    Require(path.to == sf::Vector2f{100.f + DistractionConfig::MaximumDecoyDistance, 500.f}, "La guía debe partir del agente y seguir su dirección");
    path = DistractionGeometry::TraceDecoy(world, {950.f, 500.f}, {1.f, 0.f});
    Require(path.valid && path.blocked && path.to.x <= world.size.x - DistractionConfig::ProjectileRadius, "El límite del mapa debe recortar el destino previsto");
    world.obstacles.push_back({{{250.f, 450.f}, {20.f, 100.f}}, CoverKind::High, true, "Muro"});
    path = DistractionGeometry::TraceDecoy(world, {100.f, 500.f}, {1.f, 0.f});
    Require(path.valid && path.blocked && path.to.x < 250.f - DistractionConfig::ProjectileRadius && path.to.x > 240.f, "La trayectoria termina antes del primer muro sólido");
    world.obstacles.clear();
    Interactable door{9, ObjectKind::Door, {300.f, 500.f}, "Puerta"}; world.objects.push_back(door);
    path = DistractionGeometry::TraceDecoy(world, {100.f, 500.f}, {1.f, 0.f});
    Require(path.blocked && path.to.x < WorldGeometry::ObjectFootprint(door).position.x, "Una puerta cerrada bloquea el señuelo");
    world.objects[0].open = true;
    Require(!DistractionGeometry::TraceDecoy(world, {100.f, 500.f}, {1.f, 0.f}).blocked, "Una puerta abierta permite el lanzamiento");
    Require(DistractionGeometry::TraceDecoy(world, {100.f, 500.f}, {1.f, 0.f}, 10000.f).requestedDistance == DistractionConfig::MaximumDecoyDistance, "El host debe limitar una distancia solicitada excesiva");
    Require(!DistractionGeometry::TraceDecoy(world, {100.f, 500.f}, {}, 200.f).valid && !DistractionGeometry::TraceDecoy(world, {100.f, 500.f}, {1.f, 0.f}, std::numeric_limits<float>::quiet_NaN()).valid, "Una guía sin dirección o distancia finita no autoriza un lanzamiento");
  }

  void AuthoritativeDecoyAndHearing() {
    Simulation simulation;
    auto &world = Fixture(simulation);
    Guard guard; guard.id = 8; guard.position = {500.f, 500.f}; guard.facing = {1.f, 0.f}; world.guards.push_back(guard);
    ActionCommand command; command.throwDecoy = true; command.aimX = 1.f; command.decoyDistance = 320.f;
    const auto preview = DistractionGeometry::TraceDecoy(world, world.agents[0].position, {1.f, 0.f}, command.decoyDistance);
    simulation.Step(0.1f, {{1, command}});
    Require(world.noises.size() == 1 && world.noises[0].position == preview.to && world.noises[0].radius == DistractionConfig::DecoyHearingDistance, "El ruido del host debe coincidir con la guía compartida");
    Require(world.agents[0].loadout.decoys == 2 && world.guards[0].state == GuardState::Investigate && world.guards[0].lastKnown == preview.to, "El lanzamiento consume un señuelo y dirige la investigación al punto de caída");
    Require(world.agents[0].actionPose == ActionPose::ThrowDecoy && world.agents[0].actionPoseTime > 0.f, "Un lanzamiento ejecutado conserva su fotograma de acción autoritativo");
    Require(!world.alarm && std::ranges::none_of(world.events, [](const MissionEvent &event) { return event.kind == EventKind::ConfirmedDetection; }), "Oír un señuelo no revela automáticamente al agente");
    simulation.Step(0.1f, {{1, command}});
    Require(world.agents[0].loadout.decoys == 2, "El cooldown no permite duplicar el lanzamiento");
    auto &blocked = Fixture(simulation);
    blocked.obstacles.push_back({{{120.f, 480.f}, {20.f, 40.f}}, CoverKind::High, true, "Pared cercana"});
    simulation.Step(0.1f, {{1, command}});
    Require(blocked.noises.empty() && blocked.agents[0].loadout.decoys == 3, "Un lanzamiento sin espacio válido no consume señuelos ni genera ruido ficticio");
    Require(blocked.agents[0].actionPose == ActionPose::None, "Un lanzamiento rechazado no simula una animación de éxito");
    auto &clipped = Fixture(simulation);
    clipped.obstacles.push_back({{{250.f, 450.f}, {20.f, 100.f}}, CoverKind::High, true, "Pared"});
    const auto expected = DistractionGeometry::TraceDecoy(clipped, clipped.agents[0].position, {1.f, 0.f}, command.decoyDistance);
    simulation.Step(0.1f, {{1, command}});
    Require(clipped.noises.size() == 1 && clipped.noises[0].position == expected.to && expected.blocked, "El host no lanza un señuelo a través de un obstáculo");
  }

  void ContextualWallKnock() {
    Simulation simulation;
    auto &world = Fixture(simulation);
    ActionCommand knock; knock.knock = true; knock.aimX = 1.f;
    simulation.Step(0.1f, {{1, knock}});
    Require(world.noises.empty(), "Q no inventa una pared en un espacio vacío");
    Require(world.agents[0].actionPose == ActionPose::None, "Un golpe rechazado no genera fotograma de acción");
    world.obstacles.push_back({{{128.f, 480.f}, {20.f, 40.f}}, CoverKind::High, true, "Pared"});
    knock.aimX = -1.f;
    simulation.Step(0.1f, {{1, knock}});
    Require(world.noises.empty(), "Golpear requiere mirar hacia la cara de la pared");
    knock.aimX = 1.f; world.agents[0].stance = Stance::Prone;
    simulation.Step(0.1f, {{1, knock}});
    Require(world.noises.empty(), "Una postura tumbada incompatible no ejecuta el golpe");
    world.agents[0].stance = Stance::Standing; knock.interact = true;
    simulation.Step(0.1f, {{1, knock}});
    Require(world.noises.empty(), "El golpe no interrumpe una interacción incompatible");
    knock.interact = false;
    Guard guard; guard.id = 10; guard.position = {300.f, 500.f}; guard.facing = {1.f, 0.f}; world.guards.push_back(guard);
    simulation.Step(0.1f, {{1, knock}});
    Require(world.noises.size() == 1 && world.noises[0].position == sf::Vector2f{128.f, 500.f} && world.noises[0].radius == DistractionConfig::KnockHearingDistance, "El golpe genera ruido en la superficie real de la pared");
    Require(world.agents[0].actionPose == ActionPose::KnockWall && world.agents[0].actionPoseTime > 0.f, "Un golpe validado conserva un fotograma de acción replicable");
    Require(world.guards[0].state == GuardState::Investigate && world.guards[0].lastKnown == world.noises[0].position && !world.alarm, "El guardia investiga el origen acústico sin conocer al agente");
    for (const auto kind : {HideoutKind::TrashBin, HideoutKind::UnderTable, HideoutKind::None}) {
      auto &excluded = Fixture(simulation);
      Interactable object{90, kind == HideoutKind::None ? ObjectKind::Door : ObjectKind::Hideout, {}, "Superficie no autorizada"};
      object.hideoutKind = kind;
      object.position = {128.f + WorldGeometry::ObjectFootprint(object).size.x * 0.5f, 500.f};
      excluded.objects.push_back(object);
      simulation.Step(0.1f, {{1, knock}});
      Require(excluded.noises.empty() && excluded.agents[0].actionPose == ActionPose::None, "Contenedores, mesas y puertas sin permiso de pared no generan golpe ni ruido");
    }
  }

  void ApproximateFootstepsAndPrivacy() {
    Simulation simulation, client;
    auto &world = Fixture(simulation);
    world.obstacles.push_back({{{180.f, 450.f}, {20.f, 100.f}}, CoverKind::High, true, "Separación visual"});
    Guard guard; guard.id = 33; guard.position = {240.f, 500.f}; guard.facing = {1.f, 0.f}; guard.patrol = {{340.f, 500.f}}; world.guards.push_back(guard);
    Advance(simulation, 5);
    Require(world.footsteps.size() == 1 && world.footsteps[0].remaining > 0.f, "Un enemigo que se desplaza produce pistas al intervalo configurado");
    const auto &cue = world.footsteps[0];
    Require(cue.direction == 0 && cue.position != world.guards[0].position && Visibility::FootstepAudible(world, world.agents[0], cue), "Las pistas comunican dirección y posición aproximada cuando el enemigo no es visible");
    Require(world.noises.empty() && !world.alarm, "Los pasos aliados no distraen artificialmente a otras patrullas");
    auto snapshot = simulation.SnapshotFor(1, true);
    Require(snapshot["guards"].empty() && snapshot["footsteps"].size() == 1 && !snapshot["footsteps"][0].contains("sourceGuard"), "El snapshot permite la pista acústica sin revelar identidad ni cuerpo oculto");
    Require(snapshot["footsteps"][0].size() == 3 && !snapshot["footsteps"][0].contains("health"), "La pista autorizada contiene solo posición aproximada, dirección y duración");
    std::string error;
    Require(client.LoadSnapshot(snapshot, error) && client.GetState().footsteps[0].sourceGuard == 0, "El cliente no reconstruye la identidad privada del emisor");
    world.guards[0].patrol.clear();
    Advance(simulation, 10);
    Require(world.footsteps.empty() && world.guards[0].stepTime == 0.f, "Un enemigo inmóvil no añade pistas y las anteriores desaparecen");
    world.guards[0].state = GuardState::Incapacitated; world.guards[0].incapacityRemaining = 10.f; world.guards[0].patrol = {{340.f, 500.f}};
    Advance(simulation, 10);
    Require(world.footsteps.empty(), "Un cuerpo incapacitado no produce pasos nuevos");
    Require(DistractionGeometry::FootstepDirection({0.f, 1.f}) == 2 && DistractionGeometry::FootstepDirection({-1.f, 0.f}) == 4 && DistractionGeometry::FootstepDirection({0.f, -1.f}) == 6, "La dirección de las pistas utiliza ocho sectores estables");
  }
}

int main() {
  try {
    SharedTrajectory();
    AuthoritativeDecoyAndHearing();
    ContextualWallKnock();
    ApproximateFootstepsAndPrivacy();
    std::cout << "PASS distracciones: " << checks << " comprobaciones de trayectoria, golpes, audición y pasos privados\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL distracciones tras " << checks << " comprobaciones: " << error.what() << '\n';
    return 1;
  }
}
