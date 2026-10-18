#include "NoTraceSpatialFixture.h"
#pragma once

#include "Core/EngineContext.h"
#include "NoTraceSimulation.h"
#include "NoTraceWorldRenderer.h"
#include <SFML/Graphics/RenderWindow.hpp>
#include <array>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace EngineTests {
  template <typename Capture>
  nlohmann::json RunAlertHudRenderFixtures(EngineContext &context, sf::RenderWindow &window, const std::filesystem::path &output, Capture capture) {
    using namespace NoTrace;
    int checks = 0;
    const auto require = [&](bool condition, const std::string &message) {
      ++checks;
      if (!condition) throw std::runtime_error("Ojo global del HUD: " + message);
    };
    const auto expectedColor = [](SecurityAlert alert) {
      return alert == SecurityAlert::Red ? PresentationConfig::Red : alert == SecurityAlert::Orange ? PresentationConfig::Orange : PresentationConfig::Yellow;
    };
    const auto verifyEye = [&](const sf::Image &image, SecurityAlert alert) {
      const auto point = window.mapCoordsToPixel(PresentationConfig::DetectionHudCenter + sf::Vector2f{-4.f, 0.f}, context.window.GetPresentationView());
      require(point.x >= 0 && point.y >= 0 && point.x < static_cast<int>(image.getSize().x) && point.y < static_cast<int>(image.getSize().y),
        "el ojo queda fuera del viewport al cambiar la presentación");
      const auto pixel = image.getPixel(sf::Vector2u(point));
      require(alert == SecurityAlert::Calm ? pixel != PresentationConfig::Red && pixel != PresentationConfig::Orange && pixel != PresentationConfig::Yellow :
        pixel == expectedColor(alert), "el iris no coincide con la alerta autoritativa o conserva el progreso individual");
      return nlohmann::json{{"pixel", {point.x, point.y}}, {"color", {pixel.r, pixel.g, pixel.b}}, {"alert", static_cast<int>(alert)}};
    };
    const auto previousSettings = context.window.GetDisplaySettings();
    const auto previousSize = window.getSize();
    WorldState world; world.phase = GamePhase::Mission; world.size = {1000.f, 1000.f};
    Agent observer; observer.id = 1; observer.position = {500.f, 500.f}; observer.facing = {1.f, 0.f}; world.agents.push_back(observer);
    WorldRenderer renderer(context);
    nlohmann::json layouts = nlohmann::json::array();
    for (int layout = 0; layout < 5; ++layout) {
      if (layout < 3) window.setSize(std::array<sf::Vector2u, 3>{{{1280, 720}, {1280, 960}, {1600, 900}}}[layout]);
      else {
        auto settings = context.window.GetDisplaySettings(); settings.fullscreen = layout == 3; settings.borderless = layout == 4; settings.resolution = {1600, 900};
        require(context.window.RequestDisplaySettings(settings), "el fixture no puede solicitar el modo de pantalla");
        context.window.Update(); context.window.ResolveDisplayChange(true); context.window.Update();
        require(context.window.GetDisplaySettings().fullscreen == (layout == 3) && context.window.GetDisplaySettings().borderless == (layout == 4),
          "WindowManager no aplicó el modo de pantalla solicitado");
      }
      context.window.HandleResize(window.getSize());
      for (int zoom = 0; zoom < 3; ++zoom) {
        renderer.ResetZoom();
        if (zoom != 1) renderer.AdjustZoom(zoom == 0 ? -100.f : 100.f);
        for (const auto alert : {SecurityAlert::Red, SecurityAlert::Orange, SecurityAlert::Yellow, SecurityAlert::Calm}) {
          world.securityAlert = alert;
          world.agents[0].detectionProgress = alert == SecurityAlert::Red ? 0.f : 1.f;
          world.agents[0].detectionConfirmed = alert != SecurityAlert::Red;
          const auto file = "alert-eye-" + std::to_string(layout) + "-" + std::to_string(zoom) + "-" + std::to_string(static_cast<int>(alert)) + ".png";
          const auto image = capture(renderer, world, output / file, true);
          auto evidence = verifyEye(image, alert);
          evidence["layout"] = layout; evidence["zoom"] = renderer.GetZoom(); evidence["file"] = file;
          evidence["nativeSize"] = {window.getSize().x, window.getSize().y};
          layouts.push_back(evidence);
        }
      }
    }
    require(context.window.RequestDisplaySettings(previousSettings), "el fixture no puede restaurar el modo inicial");
    context.window.Update(); context.window.ResolveDisplayChange(true); context.window.Update();
    window.setSize(previousSize); context.window.HandleResize(previousSize);
    renderer.ResetZoom();

    Simulation simulation; simulation.ResetBase({{1, "Observador"}}); simulation.Deploy(0);
    auto &live = const_cast<WorldState &>(simulation.GetState());
    NoTrace::TestFixture::LegacyPlane(live);
    live.obstacles.clear(); live.objects.clear(); live.guards.clear(); live.securityEntries.clear(); live.noises.clear(); live.sounds.clear(); live.events.clear();
    live.agents[0].position = {300.f, 300.f}; live.agents[0].facing = {1.f, 0.f};
    Interactable closet; closet.id = 77; closet.kind = ObjectKind::Hideout; closet.hideoutKind = HideoutKind::Closet;
    closet.position = {270.f, 300.f}; closet.facing = {1.f, 0.f}; live.objects.push_back(closet);
    Guard guard; guard.id = 71; guard.position = {380.f, 300.f}; guard.facing = {-1.f, 0.f}; guard.attackCooldown = 1000.f;
    guard.patrol = {guard.position}; live.guards.push_back(guard);
    nlohmann::json flow = nlohmann::json::array();
    bool red = false, orange = false, yellow = false, calm = false, hidden = false;
    for (unsigned int step = 0; step < static_cast<unsigned int>((SecurityConfig::SearchDuration + SecurityConfig::VigilanceDuration + 8.f) / 0.1f) && !calm; ++step) {
      ActionCommand command; command.interact = red && !hidden;
      simulation.Step(0.1f, {{1, command}});
      if (command.interact) { hidden = live.agents[0].hidden && live.agents[0].hideoutId == closet.id; require(hidden, "la pérdida de contacto no proviene de una interacción real de escondite"); }
      const bool changed = (!red && live.securityAlert == SecurityAlert::Red) || (red && !orange && live.securityAlert == SecurityAlert::Orange) ||
        (orange && !yellow && live.securityAlert == SecurityAlert::Yellow) || (yellow && live.securityAlert == SecurityAlert::Calm);
      if (!changed) continue;
      const auto file = "alert-eye-flow-" + std::to_string(static_cast<int>(live.securityAlert)) + ".png";
      const auto image = capture(renderer, live, output / file, true);
      auto evidence = verifyEye(image, live.securityAlert); evidence["file"] = file; evidence["tick"] = live.tick; evidence["elapsed"] = live.elapsed;
      flow.push_back(evidence);
      red |= live.securityAlert == SecurityAlert::Red; orange |= live.securityAlert == SecurityAlert::Orange;
      yellow |= live.securityAlert == SecurityAlert::Yellow; calm |= yellow && live.securityAlert == SecurityAlert::Calm;
    }
    require(red && orange && yellow && calm && hidden, "la simulación no completó roja, pérdida de contacto, naranja, amarilla y patrulla");
    nlohmann::json report{{"status", "PASS"}, {"checks", checks}, {"layouts", layouts}, {"realAlertFlow", flow}};
    std::ofstream(output / "alert-hud-fixtures.json") << report.dump(2);
    return report;
  }
}
