#pragma once

#include "NoTraceFirstPersonRenderer.h"
#include "NoTraceFirstPersonGeometry.h"
#include "NoTraceFirstPersonArtGuiTests.h"
#include "Config/NoTraceWeaponConfig.h"
#include "NoTraceSimulation.h"
#include "NoTraceSpatialGeometry.h"
#include "Core/EngineContext.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <numeric>
#include <stdexcept>

namespace EngineTests {
  template<class Capture>
  nlohmann::json RunFirstPersonRenderFixtures(EngineContext &context, sf::RenderWindow &window, const std::filesystem::path &output, Capture capture) {
    using namespace NoTrace;
    unsigned int checks = 0;
    const auto require = [&](bool valid, const char *message) { ++checks; if (!valid) throw std::runtime_error(std::string("FPS render: ") + message); };
    const auto identical = [](const sf::Image &first, const sf::Image &last) {
      return first.getSize() == last.getSize() && std::equal(first.getPixelsPtr(), first.getPixelsPtr() + static_cast<std::size_t>(first.getSize().x) * first.getSize().y * 4, last.getPixelsPtr());
    };
    WorldState world; world.phase = GamePhase::Base; world.size = {1000.f, 800.f};
    Agent observer; observer.id = 1; observer.position = {200.f, 300.f}; observer.facing = {1.f, 0.f}; world.agents.push_back(observer);
    FirstPersonRenderer renderer(context); renderer.Reset(world, 1);
    FirstPersonCamera camera;
    nlohmann::json views = nlohmann::json::array();
    const auto frame = [&](const char *name, bool hud = false) {
      capture(renderer, world, camera, output / (std::string("fps-") + name + ".png"), hud);
      const auto native = renderer.FrameImage();
      require(native.saveToFile(output / (std::string("fps-") + name + "-native.png")), "no se pudo guardar el raster nativo");
      const auto &stats = renderer.Stats();
      views.push_back({{"name", name}, {"width", stats.width}, {"height", stats.height}, {"prisms", stats.prisms}, {"billboards", stats.billboards}, {"actorPixels", stats.actorPixels}, {"cpuSubmissionMilliseconds", stats.milliseconds}});
      return native;
    };
    const auto base = frame("legacy-floor");
    require(base.getSize() == sf::Vector2u{320, 180}, "la presentación1600×900 no utiliza el raster320×180");
    require(renderer.DepthAt({.5f, .9f}) < FirstPersonConfig::RenderDistance && renderer.DepthAt({.5f, .1f}) == FirstPersonConfig::RenderDistance,
      "el suelo LegacyPlane no se distingue del cielo");
    Guard target; target.id = 10; target.position = {340.f, 225.f}; target.facing = {-1.f, 0.f}; world.guards.push_back(target);
    const auto exposed = frame("actor-exposed");
    const auto exposedPixels = renderer.Stats().actorPixels;
    require(exposedPixels > 0 && !identical(base, exposed), "el actor frontal no ocupa píxeles en su posición física");
    Obstacle wall; wall.bounds = {{270.f, 220.f}, {12.f, 150.f}}; wall.height = 25.f;
    world.obstacles.push_back(wall);
    frame("actor-partial");
    require(renderer.Stats().actorPixels > 0 && renderer.Stats().actorPixels < exposedPixels, "la cobertura baja no recorta parcialmente al actor mediante profundidad");
    world.obstacles[0].height = 66.f;
    const auto hidden = frame("actor-wall-occluded");
    require(renderer.Stats().actorPixels == 0, "el actor aparece a través de una pared opaca");
    world.guards.clear();
    require(identical(hidden, frame("actor-wall-redacted")), "el actor oculto deja píxeles distintos de su eliminación");
    world.guards.push_back(target); world.obstacles[0].blocksVision = false;
    frame("actor-glass-obstacle");
    require(renderer.Stats().actorPixels > 0, "un cristal transparente de Obstacle oculta al actor");
    world.obstacles.clear();
    Interactable glass; glass.id = 21; glass.kind = ObjectKind::Armory; glass.propStyle = PropStyle::Glass; glass.position = {276.f, 290.f};
    glass.footprint = {12.f, 150.f}; glass.blocksVision = SpatialOverride::Disabled;
    world.objects.push_back(glass);
    frame("actor-glass-object");
    require(renderer.Stats().actorPixels > 0, "un cristal transparente de Interactable oculta al actor");
    world.objects.clear();
    Interactable door; door.id = 22; door.kind = ObjectKind::Door; door.position = {276.f, 290.f}; door.footprint = {12.f, 150.f}; world.objects.push_back(door);
    frame("door-closed"); require(renderer.Stats().actorPixels == 0, "la puerta cerrada no ocluye al enemigo");
    world.objects[0].open = true;
    frame("door-open"); require(renderer.Stats().actorPixels > 0, "la puerta abierta conserva geometría opaca");
    world.objects.clear();
    world.walkSurfaces = {{1, 0, WalkSurfaceKind::Floor, {{0.f, 0.f}, world.size}, 0.f, {}, 6.f},
      {2, 1, WalkSurfaceKind::Floor, {{260.f, 180.f}, {300.f, 260.f}}, 96.f, {}, 6.f}};
    world.guards[0].elevation = 96.f; camera.pitch = .3f;
    const auto below = frame("floor-underside-occlusion");
    require(renderer.Stats().actorPixels == 0, "un guardia sobre otra planta aparece a través de la losa");
    world.guards.clear();
    require(identical(below, frame("floor-underside-redacted")), "la losa deja píxeles del actor de la planta superior");
    world.agents[0].position = {300.f, 300.f}; world.agents[0].elevation = 96.f; target.position = {390.f, 300.f}; target.elevation = 0.f;
    world.guards.push_back(target); camera.pitch = -.65f;
    const auto above = frame("floor-top-occlusion");
    require(renderer.Stats().actorPixels == 0, "un guardia de planta inferior aparece al mirar a través del suelo");
    world.guards.clear();
    require(identical(above, frame("floor-top-redacted")), "el suelo superior no oculta íntegramente al actor inferior");

    world.walkSurfaces.clear(); world.agents[0] = observer; camera = {};
    target.position = {340.f, 225.f}; target.elevation = 0.f; world.guards.push_back(target);
    const auto idle = frame("actor-idle");
    world.guards[0].staggerRemaining = .2f;
    require(!identical(idle, frame("actor-hurt")), "la reacción de impacto no cambia la pose frontal");
    world.guards[0].staggerRemaining = 0.f; world.guards[0].state = GuardState::Incapacitated;
    const auto down = frame("actor-down"); require(!identical(idle, down), "la inconsciencia no tiene pose propia");
    world.guards[0].state = GuardState::Dead;
    require(!identical(down, frame("actor-dead")), "muerte e inconsciencia utilizan el mismo recurso frontal");
    world.guards.clear();
    Agent ally; ally.id = 2; ally.position = target.position; ally.facing = target.facing; world.agents.push_back(ally);
    for (const auto stance : {Stance::Crouched, Stance::Prone}) {
      world.agents.back().stance = stance;
      const auto name = stance == Stance::Crouched ? "ally-crouched" : "ally-prone";
      frame(name); const auto stancePixels = renderer.Stats().actorPixels;
      ShotTrace hit; hit.from = observer.position; hit.to = ally.position; hit.remaining = .2f; hit.hit = true; hit.targetKind = ShotTarget::Agent; hit.targetId = 2;
      hit.toHeight = CombatGeometry::TorsoCenterHeight(stance); world.shots.push_back(hit);
      frame((std::string(name) + "-hit").c_str());
      require(stancePixels > 0 && renderer.Stats().actorPixels == stancePixels, "un impacto cambia la silueta agachada/tumbada por un cuerpo de pie");
      world.shots.clear();
    }
    world.agents.pop_back();
    renderer.Reset(world, 1);
    const auto weaponIdle = frame("weapon-idle", true);
    world.events.push_back({1, world.tick, 1, EventKind::WeaponLethal});
    world.shots.push_back({observer.position, observer.position + sf::Vector2f{100.f, 0.f}, .5f, true, 1});
    renderer.Update(world, 1, 0.f);
    require(!identical(weaponIdle, frame("weapon-fire", true)), "el evento real de disparo no activa el gesto del arma");
    renderer.Update(world, 1, FirstPersonConfig::RecoilDuration + .01f);
    require(identical(weaponIdle, frame("weapon-no-ttl-reflash", true)), "el mismo evento o el TTL del trazador reinician el retroceso");
    world.events.clear(); world.shots.clear(); world.agents[0].reloadRemaining = WeaponConfig::ReloadDuration;
    require(!identical(weaponIdle, frame("weapon-reload-out", true)), "la recarga real no muestra la retirada del cargador");
    world.agents[0].reloadRemaining = WeaponConfig::ReloadDuration * .25f;
    require(!identical(weaponIdle, frame("weapon-reload-in", true)), "la recarga real no muestra la inserción del cargador");
    world.agents[0].reloadRemaining = 0.f; world.agents[0].health -= 20.f; renderer.Update(world, 1, 0.f);
    require(!identical(weaponIdle, frame("damage-feedback", true)), "la reducción real de salud no activa el indicador de daño");

    Simulation military; military.ResetBase(); military.Deploy(17);
    world = military.GetState(); renderer.Reset(world, 1);
    for (const auto support : {1u, 10u, 23u}) {
      auto &agent = world.agents.front();
      const auto *surface = WorldGeometry::FindSurface(world, support);
      require(surface != nullptr, "la vista militar no encuentra la planta física");
      float nearest = std::numeric_limits<float>::max(); bool found = false;
      const auto center = surface->bounds.position + surface->bounds.size * .5f;
      for (float y = surface->bounds.position.y + 24.f; y < surface->bounds.position.y + surface->bounds.size.y - 24.f; y += 24.f) {
        for (float x = surface->bounds.position.x + 24.f; x < surface->bounds.position.x + surface->bounds.size.x - 24.f; x += 24.f) {
          const sf::Vector2f point{x, y}; const float elevation = WorldGeometry::SurfaceHeight(*surface, point);
          const float distance = (point - center).lengthSquared();
          if (distance >= nearest || WorldGeometry::PositionBlocked(world, point, GameplayConfig::AgentRadius, elevation, CombatConfig::StandingHeight, support)) continue;
          agent.position = point; agent.elevation = elevation; agent.supportId = support; nearest = distance; found = true;
        }
      }
      require(found, "la vista militar se ubica dentro de un sólido");
      camera = {support == 23u ? 2.8f : 0.f, -.05f, 85.f}; agent.facing = FirstPersonGeometry::Forward(camera);
      const auto name = "military-support-" + std::to_string(support);
      frame(name.c_str(), true);
      require(renderer.Stats().prisms >= world.walkSurfaces.size() + world.obstacles.size(), "la vista militar omite geometría real del nivel");
    }
    std::vector<float> timings;
    nlohmann::json fovs = nlohmann::json::array();
    for (const auto fov : {60.f, 85.f, 110.f}) {
      camera.fieldOfView = fov;
      const auto name = "fov-" + std::to_string(static_cast<unsigned int>(fov));
      frame(name.c_str(), true);
      require(std::isfinite(renderer.Stats().milliseconds) && renderer.Stats().milliseconds < 3000.f, "el raycaster se bloquea o produce un tiempo no finito");
      fovs.push_back({{"fieldOfView", fov}, {"cpuSubmissionMilliseconds", renderer.Stats().milliseconds}});
      timings.push_back(renderer.Stats().milliseconds);
    }
    auto report = nlohmann::json{{"status", "PASS"}, {"checks", checks}, {"mode", "fixtures de WorldState preparados; no equivalen a adquisición de controles ni partida manual"},
      {"views", views}, {"fovs", fovs}, {"nativeResolution", {320, 180}}, {"windowResolution", {window.getSize().x, window.getSize().y}},
      {"meanCpuSubmissionMilliseconds", std::accumulate(timings.begin(), timings.end(), 0.f) / timings.size()},
      {"partialOcclusion", true}, {"solidFloorOcclusionBothDirections", true}, {"transparentGlass", true}, {"eventSequenceRecoil", true},
      {"performanceScope", "Debug: CPU, raycasting y envío de dibujo; excluye PNG, presentación final y espera de GPU"}};
    report["art"] = RunFirstPersonArtRenderFixtures(context, output);
    std::ofstream(output / "fps-render-fixtures.json") << report.dump(2);
    return report;
  }
}
