#pragma once

#include "NoTraceWorldRenderer.h"
#include "Config/NoTraceGameplayConfig.h"
#include "Core/EngineContext.h"
#include <SFML/Graphics/Image.hpp>
#include <array>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace EngineTests {
  template <typename Capture>
  nlohmann::json RunWorldRenderFixtures(EngineContext &context, const std::filesystem::path &output, Capture capture) {
    using namespace NoTrace;
    WorldState world;
    world.phase = GamePhase::Mission;
    world.size = {700.f, 700.f};
    world.elapsed = 1.f;
    world.obstacles = {
        {{{300.f, 300.f}, {100.f, 100.f}}, CoverKind::High, true, "Caja principal"},
        {{{455.f, 380.f}, {85.f, 50.f}}, CoverKind::Low, true, "Caja secundaria"}};
    Interactable terminal;
    terminal.kind = ObjectKind::Terminal;
    terminal.position = {275.f, 460.f};
    terminal.name = "TERMINAL";
    world.objects.push_back(terminal);
    Agent agent;
    agent.id = 1;
    agent.name = "AGENTE LOCAL";
    agent.facing = {1.f, 0.f};
    world.agents.push_back(agent);
    Guard back;
    back.id = 1;
    back.position = {275.f, 350.f};
    Guard front;
    front.id = 2;
    front.position = {425.f, 350.f};
    world.guards = {back, front};
    WorldRenderer renderer(context);
    const std::array<const char *, 5> names{{"behind", "front", "lateral-x", "lateral-y", "diagonal"}};
    const std::array<sf::Vector2f, 5> positions{{{350.f, 270.f}, {350.f, 430.f}, {430.f, 390.f}, {390.f, 430.f}, {430.f, 430.f}}};
    const std::array<const char *, 3> zoomNames{{"min", "normal", "max"}};
    nlohmann::json metadata = nlohmann::json::array();
    for (std::size_t scenario = 0; scenario < positions.size(); ++scenario) {
      world.agents[0].position = positions[scenario];
      const std::vector<sf::FloatRect> footprints{world.obstacles[0].bounds, world.obstacles[1].bounds,
          WorldRenderer::ObjectFootprint(terminal), {back.position, {0.f, 0.f}}, {front.position, {0.f, 0.f}}, {positions[scenario], {0.f, 0.f}}};
      const auto order = WorldRenderer::GroundDrawOrder(footprints);
      const auto rank = [&order](std::size_t index) { return std::find(order.begin(), order.end(), index) - order.begin(); };
      if (!(rank(3) < rank(0) && rank(0) < rank(4))) throw std::runtime_error("Los guardias no rodean la caja con profundidad coherente");
      if ((scenario == 0 && rank(5) > rank(0)) || (scenario != 0 && rank(5) < rank(0))) throw std::runtime_error("El agente no cambia de lado visual al rodear la caja");
      for (std::size_t zoomIndex = 0; zoomIndex < zoomNames.size(); ++zoomIndex) {
        renderer.ResetZoom();
        if (zoomIndex != 1) renderer.AdjustZoom(zoomIndex == 0 ? -100.f : 100.f);
        const auto file = output / ("world-" + std::string(names[scenario]) + "-" + zoomNames[zoomIndex] + ".png");
        const auto pixels = capture(renderer, world, file);
        const auto logicalSize = context.window.GetLogicalSize();
        const auto focus = WorldRenderer::ProjectGround(positions[scenario], positions[scenario], logicalSize, renderer.GetZoom());
        const auto guardTorso = WorldRenderer::ProjectGround(front.position, positions[scenario], logicalSize, renderer.GetZoom()) - sf::Vector2f{0.f, PresentationConfig::ShotHeight * renderer.GetZoom()};
        const auto guardAim = renderer.AimToGround(guardTorso, world, 1);
        if ((guardAim - front.position).lengthSquared() > 0.001f) throw std::runtime_error("El clic en el torso capturado no apunta al suelo del guardia");
        const auto torso = focus + sf::Vector2f{4.f, -22.f} * renderer.GetZoom();
        const auto pixel = torso.componentWiseDiv(logicalSize).componentWiseMul(sf::Vector2f(pixels.getSize()));
        const sf::Vector2u sample{static_cast<unsigned int>(std::floor(pixel.x)), static_cast<unsigned int>(std::floor(pixel.y))};
        const auto actual = pixels.getPixel(sample);
        const auto expected = scenario == 0 ? PresentationConfig::WallTop : PresentationConfig::AgentUniforms[0];
        if (actual != expected) throw std::runtime_error("La captura no muestra la oclusión esperada del torso: " + file.string());
        const auto head = (focus + sf::Vector2f{4.f, -38.f} * renderer.GetZoom()).componentWiseDiv(logicalSize).componentWiseMul(sf::Vector2f(pixels.getSize()));
        const sf::Vector2u headSample{static_cast<unsigned int>(std::floor(head.x)), static_cast<unsigned int>(std::floor(head.y))};
        if (pixels.getPixel(headSample) != PresentationConfig::Skin) throw std::runtime_error("La cabeza no permanece parcialmente visible al rodear la caja: " + file.string());
        metadata.push_back({{"file", file.filename().string()}, {"zoom", renderer.GetZoom()},
            {"agent", {positions[scenario].x, positions[scenario].y}}, {"drawOrder", order},
            {"torsoSample", {sample.x, sample.y}}, {"torsoColor", {actual.r, actual.g, actual.b}},
            {"torsoOccluded", scenario == 0}, {"headSample", {headSample.x, headSample.y}}, {"headVisible", true},
            {"guardTorsoCursor", {guardTorso.x, guardTorso.y}}, {"guardAimGround", {guardAim.x, guardAim.y}}});
      }
    }
    const std::array<const char *, 4> shotNames{{"standing", "crouched", "prone", "prone-blocked"}};
    const std::array<Stance, 4> shotStances{{Stance::Standing, Stance::Crouched, Stance::Prone, Stance::Prone}};
    renderer.ResetZoom();
    for (std::size_t index = 0; index < shotNames.size(); ++index) {
      auto &shooter = world.agents[0];
      shooter.position = {350.f, 470.f};
      shooter.stance = shotStances[index];
      const bool blocked = index == 3;
      world.guards[1].position = blocked ? sf::Vector2f{350.f, 350.f} : sf::Vector2f{475.f, 450.f};
      world.guards[1].state = blocked ? GuardState::Patrol : GuardState::Incapacitated;
      shooter.facing = world.guards[1].position - shooter.position;
      shooter.facing /= shooter.facing.length();
      const auto impact = blocked ? sf::Vector2f{350.f, 400.f} : world.guards[1].position - shooter.facing * GameplayConfig::AgentRadius;
      world.shots = {{shooter.position, impact, GameplayConfig::ShotTraceDuration, false, shooter.id, 0, !blocked, blocked}};
      const auto file = output / ("world-shot-" + std::string(shotNames[index]) + ".png");
      const auto pixels = capture(renderer, world, file);
      const auto logicalSize = context.window.GetLogicalSize();
      const auto muzzle = WorldRenderer::ProjectGround(shooter.position, shooter.position, logicalSize) + WorldRenderer::WeaponOffset(shooter.stance, shooter.facing, true);
      const auto pixel = muzzle.componentWiseDiv(logicalSize).componentWiseMul(sf::Vector2f(pixels.getSize()));
      const sf::Vector2u sample{static_cast<unsigned int>(std::floor(pixel.x)), static_cast<unsigned int>(std::floor(pixel.y))};
      if (pixels.getPixel(sample) != PresentationConfig::Light) throw std::runtime_error("El destello no coincide con la punta del arma según postura: " + file.string());
      metadata.push_back({{"file", file.filename().string()}, {"stance", shotNames[index]},
          {"sourceHeight", WorldRenderer::WeaponHeight(shooter.stance)}, {"impactHeight", WorldRenderer::ShotImpactHeight(world.shots[0], world)},
          {"muzzleSample", {sample.x, sample.y}}, {"blocked", blocked}});
    }
    std::ofstream(output / "world-fixtures.json") << metadata.dump(2);
    return metadata;
  }
}
