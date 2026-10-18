#pragma once

#include "Core/EngineContext.h"
#include "NoTraceSimulation.h"
#include "NoTraceSpatialFixture.h"
#include "NoTraceVisibility.h"
#include "NoTraceWorldGeometry.h"
#include "NoTraceWorldRenderer.h"
#include <SFML/Graphics/Image.hpp>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

namespace EngineTests {
  template <typename Capture>
  nlohmann::json RunCoverPeekRenderFixtures(EngineContext &context, const std::filesystem::path &output, Capture capture) {
    using namespace NoTrace;
    int checks = 0;
    const auto require = [&](bool condition, const std::string &message) {
      ++checks;
      if (!condition) throw std::runtime_error("Visión al asomarse: " + message);
    };
    Simulation simulation;
    simulation.ResetBase({{1, "Observador"}}); simulation.Deploy(0);
    auto &world = const_cast<WorldState &>(simulation.GetState());
    TestFixture::LegacyPlane(world);
    world.size = {800.f, 700.f}; world.elapsed = 0.f; world.visibility = {};
    world.obstacles = {{{{200.f, 150.f}, {30.f, 200.f}}, CoverKind::High, true, "Pared de cobertura"}};
    world.objects.clear(); world.guards.clear(); world.noises.clear(); world.events.clear(); world.sounds.clear();
    world.shots.clear(); world.securityEntries.clear();
    auto &observer = world.agents.front();
    observer.position = {175.f, 180.f}; observer.facing = {-1.f, 0.f}; observer.weaponAim = {1.f, 0.f};
    const sf::Vector2f rightProbe{285.f, 110.f}, leftProbe{95.f, 125.f};
    WorldRenderer renderer(context); renderer.ResetZoom();
    nlohmann::json phases = nlohmann::json::array();
    const auto advance = [&](const ActionCommand &command, unsigned int frames, float dt = 0.1f) {
      for (unsigned int frame = 0; frame < frames; ++frame) simulation.Step(dt, {{1, command}});
    };
    const auto difference = [](sf::Color first, sf::Color second) {
      return std::max({std::abs(static_cast<int>(first.r) - second.r), std::abs(static_cast<int>(first.g) - second.g), std::abs(static_cast<int>(first.b) - second.b)});
    };
    const auto snapshot = [&](const char *phase, bool rightClear, bool leftClear) {
      const auto prefix = std::string("cover-peek-") + phase;
      const auto pixels = capture(renderer, world, output / (prefix + ".png"), false);
      auto reference = world; reference.phase = GamePhase::Base;
      const auto floor = capture(renderer, reference, output / (prefix + "-clear-reference.png"), false);
      const auto logical = context.window.GetLogicalSize();
      const auto scale = sf::Vector2f(pixels.getSize()).componentWiseDiv(logical);
      const auto patch = [&](sf::Vector2f center, bool clear, const char *side) {
        int samples = 0, clearSamples = 0, fogSamples = 0;
        for (int y = -3; y <= 3; ++y) for (int x = -3; x <= 3; ++x) {
          const auto point = center + sf::Vector2f{static_cast<float>(x) * 2.f, static_cast<float>(y) * 2.f};
          const auto projected = WorldRenderer::ProjectGround(point, observer.position, logical, renderer.GetZoom()).componentWiseMul(scale);
          const sf::Vector2i coordinate{static_cast<int>(std::floor(projected.x)), static_cast<int>(std::floor(projected.y))};
          require(coordinate.x >= 0 && coordinate.y >= 0 && coordinate.x < static_cast<int>(pixels.getSize().x) && coordinate.y < static_cast<int>(pixels.getSize().y), prefix + ": muestra fuera del PNG");
          const sf::Vector2u pixel{static_cast<unsigned int>(coordinate.x), static_cast<unsigned int>(coordinate.y)};
          const auto actual = pixels.getPixel(pixel), expected = floor.getPixel(pixel);
          require(difference(expected, PresentationConfig::Fog) > 8, prefix + ": la referencia no distingue suelo y niebla");
          clearSamples += difference(actual, expected) <= 3;
          fogSamples += difference(actual, PresentationConfig::Fog) <= 3;
          ++samples;
        }
        require((clear ? clearSamples : fogSamples) >= samples * 9 / 10, prefix + ": la niebla no sigue el lado " + side +
          " (suelo " + std::to_string(clearSamples) + ", niebla " + std::to_string(fogSamples) + ")");
        return nlohmann::json{{"center", {center.x, center.y}}, {"samples", samples}, {"clearSamples", clearSamples}, {"fogSamples", fogSamples}, {"expectedClear", clear}};
      };
      const auto right = patch(rightProbe, rightClear, "derecho"), left = patch(leftProbe, leftClear, "izquierdo");
      require(observer.cover.active && observer.facing == observer.cover.normal, prefix + ": la puntería gira el cuerpo apoyado");
      require(!WorldGeometry::PositionBlocked(world, observer.position, GameplayConfig::AgentRadius, observer.elevation, WorldGeometry::ActorHeight(observer), observer.supportId), prefix + ": el cuerpo penetra la pared");
      const auto filtered = simulation.SnapshotFor(1, true);
      require(filtered.at("visibility").at("visualOrigin") == nlohmann::json::array({observer.position.x, observer.position.y}), prefix + ": el snapshot adelanta el origen al destino del asomado");
      std::ofstream(output / (prefix + "-snapshot.json")) << simulation.SnapshotFor(1).dump(2);
      std::ofstream(output / (prefix + "-filtered.json")) << filtered.dump(2);
      phases.push_back({{"phase", phase}, {"file", prefix + ".png"}, {"clearReference", prefix + "-clear-reference.png"},
        {"tick", world.tick}, {"position", {observer.position.x, observer.position.y}}, {"anchor", {observer.cover.anchor.x, observer.cover.anchor.y}},
        {"bodyFacing", {observer.facing.x, observer.facing.y}}, {"weaponAim", {observer.weaponAim.x, observer.weaponAim.y}},
        {"peekSide", static_cast<int>(observer.cover.peekSide)}, {"aiming", observer.aiming}, {"rightPatch", right}, {"leftPatch", left}});
    };

    ActionCommand enter; enter.coverToggle = true; enter.aiming = true; enter.aimX = 1.f;
    advance(enter, 1);
    require(observer.cover.active && observer.cover.normal == sf::Vector2f{-1.f, 0.f}, "la simulación no se apoya en la cara física de la pared");
    const auto anchor = observer.cover.anchor;
    snapshot("before", false, true);

    ActionCommand peek; peek.peekLeft = true; peek.aiming = true; peek.aimX = 1.f;
    advance(peek, 1, 1.f / 60.f);
    require(observer.cover.peekSide == PeekSide::Left && observer.position.y > 150.f, "el primer paso no captura el recorrido todavía protegido");
    snapshot("moving", false, false);
    advance(peek, 6);
    require(observer.position.y < 150.f && observer.weaponAim == sf::Vector2f{1.f, 0.f}, "el asomado no alcanza físicamente el extremo con el arma orientada");
    snapshot("aim-right", true, false);

    const auto peekPosition = observer.position;
    peek.aimX = -1.f; advance(peek, 1);
    require(observer.position == peekPosition && observer.weaponAim == sf::Vector2f{-1.f, 0.f}, "girar el arma mueve la cobertura o no cambia la puntería");
    snapshot("aim-left", false, true);

    ActionCommand withdraw; withdraw.aiming = true; withdraw.aimX = 1.f; advance(withdraw, 6);
    require(observer.cover.peekSide == PeekSide::None && (observer.position - anchor).length() <= SpatialConfig::CoverPositionTolerance, "el retorno no recupera el ancla protegida");
    snapshot("returned", false, true);

    nlohmann::json report{{"status", "PASS"}, {"checks", checks}, {"cases", phases}, {"flag", "--cover-peek-visuals-only"},
      {"comparison", "Parches de suelo de 49 muestras comparados contra PNG sin niebla de la misma fase y contra el color opaco de niebla; al menos 90 % debe coincidir."},
      {"simulation", "Entrada, desplazamiento al extremo, giro del arma y retorno proceden de Simulation::Step; el origen visual debe seguir siempre la posición física actual."}};
    std::ofstream(output / "cover-peek-fixtures.json") << report.dump(2);
    return report;
  }
}
