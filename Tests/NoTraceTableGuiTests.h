#pragma once

#include "Core/EngineContext.h"
#include "NoTraceSimulation.h"
#include "NoTraceWorldRenderer.h"
#include <array>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace EngineTests {
  template <typename Capture>
  nlohmann::json RunTableRenderFixtures(EngineContext &context, const std::filesystem::path &output, Capture capture) {
    using namespace NoTrace;
    int checks = 0;
    const auto require = [&](bool condition, const std::string &message) {
      ++checks;
      if (!condition) throw std::runtime_error("Mesa instantánea y visión completa: " + message);
    };
    const auto identical = [](const sf::Image &first, const sf::Image &second) {
      const auto size = first.getSize();
      return size == second.getSize() && std::equal(first.getPixelsPtr(), first.getPixelsPtr() + static_cast<std::size_t>(size.x) * size.y * 4, second.getPixelsPtr());
    };
    Simulation simulation; simulation.ResetBase({{1, "Observador"}}); simulation.Deploy(0);
    auto &world = const_cast<WorldState &>(simulation.GetState());
    world.size = {800.f, 800.f}; world.obstacles.clear(); world.objects.clear(); world.guards.clear(); world.securityEntries.clear(); world.noises.clear(); world.sounds.clear(); world.events.clear();
    auto &agent = world.agents[0]; agent.position = {230.f, 300.f}; agent.facing = {1.f, 0.f};
    Interactable table; table.id = 77; table.kind = ObjectKind::Hideout; table.hideoutKind = HideoutKind::UnderTable;
    table.position = {300.f, 300.f}; table.facing = {-1.f, 0.f}; table.name = "Mesa"; world.objects.push_back(table);
    WorldRenderer renderer(context);
    capture(renderer, world, output / "table-before-entry.png", false);
    ActionCommand use; use.interact = true;
    simulation.Step(0.1f, {{1, use}});
    require(agent.hidden && agent.hideoutId == table.id && agent.position == table.position && agent.actionPose == ActionPose::None &&
      agent.actionPoseTime == 0.f && agent.transitionTime == 0.f, "la interacción real no oculta y posiciona al agente en un solo tick");
    const auto inside = capture(renderer, world, output / "table-instant-inside.png", false);
    auto legacy = world; legacy.agents[0].actionPose = ActionPose::HideEnter; legacy.agents[0].actionPoseTime = GameplayConfig::StanceTransitionDuration;
    require(identical(inside, capture(renderer, legacy, output / "table-no-entry-interpolation.png", false)),
      "una pose de entrada antigua sigue dibujando el cuerpo o la sombra bajo la mesa");
    nlohmann::json views = nlohmann::json::array();
    const std::array<sf::Vector2f, 8> directions{{{1.f, 0.f}, {1.f, 1.f}, {0.f, 1.f}, {-1.f, 1.f}, {-1.f, 0.f}, {-1.f, -1.f}, {0.f, -1.f}, {1.f, -1.f}}};
    for (std::size_t direction = 0; direction < directions.size(); ++direction) {
      agent.facing = directions[direction].normalized();
      const auto file = "table-view-" + std::to_string(direction) + ".png";
      require(identical(inside, capture(renderer, world, output / file, false)), "girar el cuerpo oculto modifica la visión de 360 grados");
      require(Visibility::IsGeometricallyVisible(world, agent, agent.position + agent.facing * 150.f), "falta una dirección visible alrededor de la mesa");
      views.push_back({{"file", file}, {"direction", direction}, {"identicalFog", true}});
    }
    Guard front; front.id = 91; front.position = {450.f, 300.f}; front.facing = {-1.f, 0.f};
    Guard back; back.id = 92; back.position = {150.f, 300.f}; back.facing = {1.f, 0.f};
    world.guards = {front, back};
    require(Visibility::GuardVisible(world, agent, front) && Visibility::GuardVisible(world, agent, back), "la mesa no permite observar guardias delante y detrás");
    capture(renderer, world, output / "table-guards-both-sides.png", false);
    world.obstacles.push_back({{{365.f, 180.f}, {12.f, 240.f}}, CoverKind::High, true, "Pared opaca"});
    require(!Visibility::GuardVisible(world, agent, front) && Visibility::GuardVisible(world, agent, back), "la visión completa no respeta la oclusión sólida");
    const auto blocked = capture(renderer, world, output / "table-wall-occlusion.png", false);
    auto redacted = world; redacted.guards.erase(redacted.guards.begin());
    require(identical(blocked, capture(renderer, redacted, output / "table-wall-redacted.png", false)), "un guardia detrás de la pared deja información en el PNG");
    world.guards.clear(); world.obstacles.clear();
    ActionCommand aim; aim.aiming = true; aim.aimY = 1.f;
    simulation.Step(0.1f, {{1, aim}});
    require(agent.hidden && agent.position == table.position && WeaponFacing(agent).dot({0.f, 1.f}) > 0.999f,
      "el apuntado real cambia de escondite o pierde la dirección del arma");
    capture(renderer, world, output / "table-hidden-aim.png", false);
    agent.facing = {1.f, 0.f}; simulation.Step(0.1f, {{1, use}});
    require(!agent.hidden && agent.hideoutId == 0 && agent.position != table.position && agent.actionPose == ActionPose::None &&
      agent.actionPoseTime == 0.f && agent.transitionTime == 0.f && !WorldGeometry::PositionBlocked(world, agent.position, GameplayConfig::AgentRadius),
      "la salida no aparece inmediatamente en una posición física válida");
    const auto logical = context.window.GetLogicalSize().componentWiseMul(PresentationConfig::CameraFocusRatio);
    const sf::Vector2f focus{std::round(logical.x / PresentationConfig::PixelSize) * PresentationConfig::PixelSize,
      std::round(logical.y / PresentationConfig::PixelSize) * PresentationConfig::PixelSize};
    require((renderer.ScreenToGround(focus, world, agent.id) - agent.position).length() < 0.001f, "la cámara conserva una posición interpolada tras la salida instantánea");
    capture(renderer, world, output / "table-instant-outside.png", false);
    nlohmann::json report{{"status", "PASS"}, {"checks", checks}, {"views", views}, {"entryExit", "instant"}, {"wallGuardPixelsRevealed", 0},
      {"hiddenBodyAndShadow", "omitted"}, {"weaponAimPreserved", true}, {"validExit", {agent.position.x, agent.position.y}}};
    std::ofstream(output / "table-fixtures.json") << report.dump(2);
    return report;
  }
}
