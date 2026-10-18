#pragma once

#include "NoTraceMultilevelTests.h"
#include "NoTraceFurniture.h"
#include "NoTraceInteraction.h"
#include "NoTraceWorldRenderer.h"
#include <filesystem>
#include <fstream>
#include <limits>

namespace EngineTests {
  template<class Capture>
  nlohmann::json RunLevelRenderFixtures(EngineContext &context, const std::filesystem::path &output, Capture capture) {
    using namespace NoTrace;
    unsigned int checks = 0;
    const auto require = [&](bool condition, const char *message) { ++checks; if (!condition) throw std::runtime_error(message); };
    const auto identical = [](const sf::Image &a, const sf::Image &b) {
      return a.getSize() == b.getSize() && std::equal(a.getPixelsPtr(), a.getPixelsPtr() + static_cast<std::size_t>(a.getSize().x) * a.getSize().y * 4, b.getPixelsPtr());
    };
    Simulation simulation; auto &world = MultilevelTests::QuietMission(simulation); auto &agent = world.agents.front();
    WorldRenderer renderer(context);
    nlohmann::json views = nlohmann::json::array();
    for (const auto support : {1u, 10u, 11u, 21u, 23u}) {
      const auto *surface = WorldGeometry::FindSurface(world, support);
      require(surface != nullptr, "La galería no encuentra una planta física");
      agent.position = surface->bounds.position + surface->bounds.size * .5f;
      agent.elevation = WorldGeometry::SurfaceHeight(*surface, agent.position); agent.supportId = support;
      bool clear = false;
      float nearest = std::numeric_limits<float>::max();
      const auto center = agent.position;
      for (float y = surface->bounds.position.y + 20.f; y < surface->bounds.position.y + surface->bounds.size.y - 20.f; y += 24.f) {
        for (float x = surface->bounds.position.x + 20.f; x < surface->bounds.position.x + surface->bounds.size.x - 20.f; x += 24.f) {
          const sf::Vector2f point{x, y};
          const float elevation = WorldGeometry::SurfaceHeight(*surface, point);
          const float distance = (point - center).lengthSquared();
          if (distance < nearest && !WorldGeometry::PositionBlocked(world, point, GameplayConfig::AgentRadius, elevation, CombatConfig::StandingHeight, support)) {
            agent.position = point; agent.elevation = elevation; clear = true; nearest = distance;
          }
        }
      }
      require(clear, "La galería no encuentra una posición física libre en la planta");
      agent.facing = {1.f, 0.f}; agent.weaponAim = agent.facing;
      const auto name = "level-support-" + std::to_string(support) + ".png";
      capture(renderer, world, output / name, false);
      const auto focus = context.window.GetLogicalSize().componentWiseMul(PresentationConfig::CameraFocusRatio);
      require((renderer.ScreenToGround(focus, world, agent.id) - agent.position).length() < 1.f, "La cámara pierde el ancla al cambiar de altura");
      views.push_back({{"file", name}, {"support", support}, {"elevation", agent.elevation}});
    }
    agent.position = {900.f, 220.f}; agent.elevation = 192.f; agent.supportId = 23; agent.facing = {1.f, 0.f};
    Guard below; below.id = 901; below.position = {930.f, 220.f}; below.elevation = 0.f; below.supportId = 1;
    world.guards.push_back(below);
    require(!Visibility::GuardVisible(world, agent, below), "Un suelo sólido permite ver al guardia de otra planta");
    const auto occluded = capture(renderer, world, output / "level-solid-floor-occlusion.png", false);
    world.guards.clear();
    require(identical(occluded, capture(renderer, world, output / "level-solid-floor-redacted.png", false)), "Un guardia bajo el suelo deja píxeles visibles");
    agent.position = {380.f, 950.f}; agent.elevation = 0.f; agent.supportId = 5; agent.facing = {1.f, 0.f};
    auto &table = *std::ranges::find(world.objects, 34u, &Interactable::id);
    require(Furniture::CanFlipTable(world, agent, table), "La mesa del taller no admite volcado físico válido");
    const auto normal = capture(renderer, world, output / "level-table-normal.png", false);
    ActionCommand tool; tool.tool = true; simulation.Step(1.f / 60.f, {{1, tool}});
    require(table.overturned && !Interaction::CanEnterHideout(world, agent, table), "El volcado conserva indebidamente el escondite");
    require(WorldGeometry::BlocksMovement(table) && WorldGeometry::ObjectHeight(table) == SpatialConfig::OverturnedTableHeight, "La mesa volcada no usa su volumen físico");
    require(!identical(normal, capture(renderer, world, output / "level-table-overturned.png", false)), "Volcar la mesa no cambia el sprite en ese mismo tick");
    simulation.Step(1.f / 60.f, {{1, tool}});
    require(!table.overturned, "La segunda acción válida no restablece la mesa");
    agent.position = {340.f, 326.f}; agent.elevation = 0.f; agent.supportId = 1;
    ActionCommand attach; attach.traverse = true; simulation.Step(1.f / 60.f, {{1, attach}});
    require(agent.traversal.mode == TraversalMode::Attach, "La ruta de pared no inicia una transición física");
    for (unsigned int frame = 0; frame < 120; ++frame) simulation.Step(1.f / 60.f, {});
    require(agent.traversal.mode == TraversalMode::Hang, "La transición no llega al estado colgado");
    capture(renderer, world, output / "level-wall-hang.png", false);
    auto report = nlohmann::json{{"status", "PASS"}, {"checks", checks}, {"views", views}, {"floorOcclusion", true}, {"instantTableSpriteAndCollision", true}, {"physicalWallAttach", true}};
    std::ofstream(output / "level-fixtures.json") << report.dump(2);
    return report;
  }
}
