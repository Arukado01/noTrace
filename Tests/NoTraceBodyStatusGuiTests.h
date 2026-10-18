#pragma once

#include "Core/EngineContext.h"
#include "NoTraceWorldRenderer.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace EngineTests {
  template <typename Capture>
  nlohmann::json RunBodyStatusRenderFixtures(EngineContext &context, const std::filesystem::path &output, Capture capture) {
    using namespace NoTrace;
    nlohmann::json evidence = nlohmann::json::array();
    int checks = 0;
    const auto require = [&](bool condition, const std::string &message) {
      ++checks;
      if (!condition) throw std::runtime_error("Iconos de cuerpos: " + message);
    };
    const auto pixelRegion = [&](const sf::Image &image, sf::Vector2f position, sf::Vector2f camera, float zoom) {
      const auto logical = context.window.GetLogicalSize();
      const auto center = WorldRenderer::ProjectGround(position, camera, logical, zoom) -
        sf::Vector2f{0.f, CombatConfig::ProneHeight * zoom + PresentationConfig::IconOffset};
      const auto scale = sf::Vector2f(image.getSize()).componentWiseDiv(logical);
      const auto minimum = (center - sf::Vector2f{28.f, 20.f}).componentWiseMul(scale);
      const auto maximum = (center + sf::Vector2f{28.f, 20.f}).componentWiseMul(scale);
      return sf::IntRect({std::max(0, static_cast<int>(std::floor(minimum.x))), std::max(0, static_cast<int>(std::floor(minimum.y)))},
        {std::min(static_cast<int>(image.getSize().x), static_cast<int>(std::ceil(maximum.x))) - std::max(0, static_cast<int>(std::floor(minimum.x))),
         std::min(static_cast<int>(image.getSize().y), static_cast<int>(std::ceil(maximum.y))) - std::max(0, static_cast<int>(std::floor(minimum.y)))});
    };
    const auto colored = [&](const sf::Image &image, sf::IntRect region, sf::Color color) {
      int count = 0;
      for (int y = 0; y < region.size.y; ++y) for (int x = 0; x < region.size.x; ++x)
        if (image.getPixel(sf::Vector2u(region.position + sf::Vector2i{x, y})) == color) ++count;
      return count;
    };
    const auto difference = [&](const sf::Image &first, const sf::Image &second, sf::IntRect region) {
      int count = 0;
      for (int y = 0; y < region.size.y; ++y) for (int x = 0; x < region.size.x; ++x) {
        const auto pixel = sf::Vector2u(region.position + sf::Vector2i{x, y});
        if (first.getPixel(pixel) != second.getPixel(pixel)) ++count;
      }
      return count;
    };
    WorldState world;
    world.phase = GamePhase::Base; world.size = {800.f, 800.f};
    Agent observer; observer.id = 1; observer.position = {400.f, 400.f}; observer.facing = {1.f, 0.f}; world.agents.push_back(observer);
    Agent partner; partner.id = 2; partner.position = {400.f, 450.f}; partner.lifeState = LifeState::Incapacitated;
    partner.incapacitated = true; partner.incapacityRemaining = 55.f; world.agents.push_back(partner);
    Guard guard; guard.id = 33; guard.position = {450.f, 400.f}; guard.state = GuardState::Incapacitated; world.guards.push_back(guard);
    WorldRenderer renderer(context);
    std::array<int, 3> violetCounts{};
    for (int index = 0; index < 3; ++index) {
      renderer.ResetZoom();
      if (index != 1) renderer.AdjustZoom(index == 0 ? -100.f : 100.f);
      const float zoom = renderer.GetZoom();
      const std::string suffix = index == 0 ? "min" : index == 1 ? "normal" : "max";
      world.guards[0].state = GuardState::Incapacitated; world.guards[0].bodyHidden = false;
      world.agents[1].lifeState = LifeState::Incapacitated; world.agents[1].alive = true; world.agents[1].incapacitated = true; world.agents[1].hidden = false;
      const auto unconscious = capture(renderer, world, output / ("body-status-unconscious-" + suffix + ".png"), false);
      std::array<sf::IntRect, 2> regions{pixelRegion(unconscious, guard.position, observer.position, zoom), pixelRegion(unconscious, partner.position, observer.position, zoom)};
      for (const auto &region : regions) {
        require(region.size.x > 20 && region.size.y > 15, "el icono desaparece o queda fuera de pantalla al cambiar el zoom");
        require(colored(unconscious, region, PresentationConfig::Violet) > 40 && colored(unconscious, region, PresentationConfig::Skin) > 40,
          "la cabeza inclinada y las Zzz no se distinguen en el guardia o el compañero: " + suffix);
      }
      violetCounts[index] = colored(unconscious, regions[0], PresentationConfig::Violet);
      world.guards[0].state = GuardState::Dead;
      world.agents[1].lifeState = LifeState::Dead; world.agents[1].alive = false; world.agents[1].incapacitated = false;
      const auto dead = capture(renderer, world, output / ("body-status-dead-" + suffix + ".png"), false);
      for (const auto &region : regions) {
        require(colored(dead, region, PresentationConfig::Red) > 40 && colored(dead, region, PresentationConfig::Light) > 40,
          "el cadáver no muestra la calavera con señal roja: " + suffix);
        require(difference(unconscious, dead, region) > 100, "muerte e inconsciencia reutilizan el mismo símbolo");
      }
      world.guards[0].bodyHidden = true; world.agents[1].hidden = true;
      const auto hidden = capture(renderer, world, output / ("body-status-hidden-" + suffix + ".png"), false);
      auto empty = world; empty.guards.clear(); empty.agents.resize(1);
      const auto absent = capture(renderer, empty, output / ("body-status-absent-" + suffix + ".png"), false);
      for (const auto &region : regions) require(difference(hidden, absent, region) == 0, "un cuerpo oculto conserva píxeles de su indicador");
      world.guards[0].bodyHidden = false; world.agents[1].hidden = false;
      evidence.push_back({{"zoom", zoom}, {"size", {PresentationConfig::BodyIconWidth * PresentationConfig::BodyIconPixelSize,
        PresentationConfig::BodyIconHeight * PresentationConfig::BodyIconPixelSize}}, {"unconscious_guard_pixels", violetCounts[index]},
        {"guard_and_agent_distinct_states", "PASS"}, {"hidden_bodies", "PASS"}});
    }
    require(*std::max_element(violetCounts.begin(), violetCounts.end()) < *std::min_element(violetCounts.begin(), violetCounts.end()) * 1.3f,
      "el tamaño del glifo cambia con la escala del mundo");

    world.phase = GamePhase::Mission; world.agents[0].position = {280.f, 300.f};
    world.agents[1].position = {370.f, 320.f}; world.guards[0].position = {370.f, 300.f};
    world.obstacles = {{{{320.f, 200.f}, {20.f, 220.f}}, CoverKind::High, true, "Muro de oclusión"}};
    for (const float direction : {-100.f, 100.f}) {
      renderer.AdjustZoom(direction);
      for (const bool dead : {false, true}) {
        world.guards[0].state = dead ? GuardState::Dead : GuardState::Incapacitated;
        world.agents[1].lifeState = dead ? LifeState::Dead : LifeState::Incapacitated;
        world.agents[1].alive = !dead; world.agents[1].incapacitated = !dead;
        require(!Visibility::GuardVisible(world, world.agents[0], world.guards[0]) &&
          !Visibility::IsVisible(world, world.agents[0], world.agents[1].position, world.agents[1].stance), "la pared deja visible el cuerpo del fixture");
        const std::string suffix = std::string(direction < 0.f ? "min" : "max") + (dead ? "-dead" : "-unconscious");
        const auto blocked = capture(renderer, world, output / ("body-status-wall-" + suffix + ".png"), false);
        auto empty = world; empty.guards.clear(); empty.agents.resize(1);
        const auto absent = capture(renderer, empty, output / ("body-status-wall-absent-" + suffix + ".png"), false);
        for (const auto position : {world.guards[0].position, world.agents[1].position})
          require(difference(blocked, absent, pixelRegion(blocked, position, world.agents[0].position, renderer.GetZoom())) == 0,
            "el icono revela un cuerpo situado tras la pared: " + suffix);
        evidence.push_back({{"zoom", renderer.GetZoom()}, {"wall", suffix}, {"body_pixels_revealed", 0}});
      }
    }
    nlohmann::json report{{"status", "PASS"}, {"checks", checks}, {"cases", evidence}};
    std::ofstream file(output / "body-status-fixtures.json"); file << report.dump(2);
    return report;
  }
}
