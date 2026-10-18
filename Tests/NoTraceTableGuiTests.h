#pragma once

#include "Core/EngineContext.h"
#include "NoTraceSimulation.h"
#include "NoTraceWorldRenderer.h"
#include "NoTraceSpatialFixture.h"
#include "NoTraceVisibility.h"
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
      if (!condition) throw std::runtime_error("Mesa instantánea y cono tumbado: " + message);
    };
    const auto identical = [](const sf::Image &first, const sf::Image &second) {
      const auto size = first.getSize();
      return size == second.getSize() && std::equal(first.getPixelsPtr(), first.getPixelsPtr() + static_cast<std::size_t>(size.x) * size.y * 4, second.getPixelsPtr());
    };
    Simulation simulation; simulation.ResetBase({{1, "Observador"}}); simulation.Deploy(0);
    auto &world = const_cast<WorldState &>(simulation.GetState());
    world.size = {800.f, 800.f}; world.obstacles.clear(); world.objects.clear(); world.guards.clear(); world.securityEntries.clear(); world.noises.clear(); world.sounds.clear(); world.events.clear();
    TestFixture::LegacyPlane(world);
    auto &agent = world.agents[0]; agent.position = {230.f, 300.f}; agent.facing = {1.f, 0.f}; agent.weaponAim = {1.f, 0.f};
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
      const auto aim = directions[direction].normalized(); agent.weaponAim = aim;
      const auto file = "table-view-" + std::to_string(direction) + ".png";
      const auto view = capture(renderer, world, output / file, false);
      require(direction == 0 ? identical(inside, view) : !identical(inside, view), "girar el arma no reorienta la niebla del cono tumbado");
      require(Visibility::IsGeometricallyVisible(world, agent, agent.position + aim * 120.f, Stance::Prone) &&
        !Visibility::IsGeometricallyVisible(world, agent, agent.position + aim * (VisibilityConfig::ProneRange + 1.f), Stance::Prone) &&
        !Visibility::IsGeometricallyVisible(world, agent, agent.position - aim * 10.f, Stance::Prone) &&
        !Visibility::IsGeometricallyVisible(world, agent, agent.position + sf::Vector2f{-aim.y, aim.x} * 10.f, Stance::Prone),
        "el cono pierde una dirección, revela un halo cercano o supera el rango tumbado");
      views.push_back({{"file", file}, {"direction", direction}, {"identicalFog", direction == 0}, {"range", VisibilityConfig::ProneRange}});
    }
    agent.weaponAim = {1.f, 0.f};
    const auto beforeBodyTurn = capture(renderer, world, output / "table-body-before-turn.png", false);
    agent.facing = {-1.f, 0.f};
    require(WeaponFacing(agent) == sf::Vector2f{1.f, 0.f} && identical(beforeBodyTurn, capture(renderer, world, output / "table-body-turn-same-cone.png", false)),
      "girar sólo el cuerpo oculto altera el arma, la niebla o dibuja arte corporal bajo mesa");
    Guard front; front.id = 91; front.position = {420.f, 300.f}; front.facing = {-1.f, 0.f};
    Guard back; back.id = 92; back.position = {180.f, 300.f}; back.facing = {1.f, 0.f};
    world.guards = {front, back};
    require(Visibility::GuardVisible(world, agent, front) && !Visibility::GuardVisible(world, agent, back), "el cono del arma revela al guardia contrario detrás del cuerpo");
    capture(renderer, world, output / "table-guard-front-only.png", false);
    agent.weaponAim = {-1.f, 0.f};
    require(!Visibility::GuardVisible(world, agent, front) && Visibility::GuardVisible(world, agent, back), "girar el arma no intercambia las patrullas visibles");
    capture(renderer, world, output / "table-guard-back-only.png", false);
    world.obstacles.push_back({{{235.f, 180.f}, {12.f, 240.f}}, CoverKind::High, true, "Pared opaca"});
    require(!Visibility::GuardVisible(world, agent, front) && !Visibility::GuardVisible(world, agent, back), "el cono tumbado no respeta la pared entre observador y guardia");
    const auto blocked = capture(renderer, world, output / "table-wall-occlusion.png", false);
    auto redacted = world; redacted.guards.erase(redacted.guards.begin() + 1);
    require(identical(blocked, capture(renderer, redacted, output / "table-wall-redacted.png", false)), "un guardia detrás de la pared deja información en el PNG");
    world.guards.clear(); world.obstacles.clear();
    ActionCommand aim; aim.aiming = true; aim.aimY = 1.f;
    simulation.Step(0.1f, {{1, aim}});
    require(agent.hidden && agent.position == table.position && WeaponFacing(agent).dot({0.f, 1.f}) > 0.999f,
      "el apuntado real cambia de escondite o pierde la dirección del arma");
    capture(renderer, world, output / "table-hidden-aim.png", false);
    agent.facing = {1.f, 0.f}; simulation.Step(0.1f, {{1, use}});
    require(!agent.hidden && agent.hideoutId == 0 && agent.position != table.position && agent.actionPose == ActionPose::None &&
      agent.position.y > table.position.y && std::abs(agent.position.x - table.position.x) < SpatialConfig::CollisionSkin &&
      agent.actionPoseTime == 0.f && agent.transitionTime == 0.f && !WorldGeometry::PositionBlocked(world, agent.position, GameplayConfig::AgentRadius),
      "la salida no aparece inmediatamente en una posición física válida");
    const auto logical = context.window.GetLogicalSize().componentWiseMul(PresentationConfig::CameraFocusRatio);
    const sf::Vector2f focus{std::round(logical.x / PresentationConfig::PixelSize) * PresentationConfig::PixelSize,
      std::round(logical.y / PresentationConfig::PixelSize) * PresentationConfig::PixelSize};
    require((renderer.ScreenToGround(focus, world, agent.id) - agent.position).length() < 0.001f, "la cámara conserva una posición interpolada tras la salida instantánea");
    capture(renderer, world, output / "table-instant-outside.png", false);
    ActionCommand step; step.moveY = 1.f;
    for (int frame = 0; frame < 3; ++frame) simulation.Step(.1f, {{1, step}});
    const int tools = agent.loadout.tools;
    ActionCommand flip; flip.tool = true; simulation.Step(.1f, {{1, flip}});
    require(world.objects[0].overturned && !agent.hidden && agent.hideoutId == 0 && agent.loadout.tools == tools &&
      WorldGeometry::ObjectHeight(world.objects[0]) == SpatialConfig::OverturnedTableHeight && WorldGeometry::PositionBlocked(world, table.position, GameplayConfig::AgentRadius),
      "T no voltea físicamente la mesa en el mismo tick o se comporta como esconderse con E");
    capture(renderer, world, output / "table-instant-overturned.png", false);
    nlohmann::json report{{"status", "PASS"}, {"checks", checks}, {"views", views}, {"entryExit", "instant"}, {"wallGuardPixelsRevealed", 0},
      {"hiddenBodyAndShadow", "omitted"}, {"weaponAimPreserved", true}, {"view", "weaponCone"}, {"range", VisibilityConfig::ProneRange}, {"flip", "instantPhysical"}, {"validExit", {agent.position.x, agent.position.y}}};
    std::ofstream(output / "table-fixtures.json") << report.dump(2);
    return report;
  }
}
