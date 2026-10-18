#pragma once

#include "NoTraceFirstPersonArt.h"
#include "NoTraceFirstPersonComposition.h"
#include "NoTraceFirstPersonRenderer.h"
#include "Config/NoTraceWeaponConfig.h"
#include "Core/EngineContext.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <string>

namespace EngineTests {
  template<class Capture>
  nlohmann::json RunFirstPersonCompositionFixtures(EngineContext &context, sf::RenderWindow &window, const std::filesystem::path &output, Capture capture) {
    using namespace NoTrace;
    unsigned int checks = 0;
    const auto require = [&](bool valid, const char *message) { ++checks; if (!valid) throw std::runtime_error(std::string("FPS composición: ") + message); };
    const auto differences = [](const sf::Image &first, const sf::Image &last, sf::IntRect region) {
      std::size_t count = 0;
      for (int y = region.position.y; y < region.position.y + region.size.y; ++y)
        for (int x = region.position.x; x < region.position.x + region.size.x; ++x)
          if (first.getPixel({static_cast<unsigned int>(x), static_cast<unsigned int>(y)}) != last.getPixel({static_cast<unsigned int>(x), static_cast<unsigned int>(y)})) ++count;
      return count;
    };
    const auto signature = [](const sf::Image &image) {
      std::uint64_t hash = 14695981039346656037ull;
      const auto bytes = static_cast<std::size_t>(image.getSize().x) * image.getSize().y * 4;
      for (std::size_t index = 0; index < bytes; ++index) { hash ^= image.getPixelsPtr()[index]; hash *= 1099511628211ull; }
      return hash;
    };
    struct Format { const char *name; sf::Vector2u size; };
    const std::array<Format, 4> formats{{{"16x9", {1280, 720}}, {"16x10", {1280, 800}}, {"4x3", {1280, 960}}, {"21x9", {2100, 900}}}};
    const std::array<const char *, 4> gestures{{"idle", "fire", "reload-out", "reload-in"}};
    require(context.window.GetLogicalSize() == sf::Vector2f{1600.f, 900.f}, "la fixture necesita el tamaño lógico real1600×900");
    const auto originalSize = window.getSize();
    struct RestoreWindow {
      WindowManager &manager;
      sf::RenderWindow &window;
      sf::Vector2u size;
      ~RestoreWindow() { window.setSize(size); manager.HandleResize(size); }
    } restore{context.window, window, originalSize};
    WorldState world; world.phase = GamePhase::Base; world.size = {1000.f, 800.f};
    Agent observer; observer.id = 1; observer.position = {200.f, 300.f}; observer.facing = {1.f, 0.f}; world.agents.push_back(observer);
    Guard target; target.id = 10; target.position = {340.f, 300.f}; target.facing = {-1.f, 0.f}; world.guards.push_back(target);
    FirstPersonCamera camera;
    FirstPersonArt art(context);
    FirstPersonRenderer renderer(context); renderer.Reset(world, 1);
    const auto aim = renderer.Aim(world, 1, camera);
    nlohmann::json logicalCompositions = nlohmann::json::array(), captures = nlohmann::json::array();
    float minimumClearance = 1.f;
    for (const auto weapon : {WeaponKind::Pistol, WeaponKind::NonLethal}) for (const bool suppressed : {false, true})
      for (unsigned int gesture = 0; gesture < gestures.size(); ++gesture) {
        const auto frame = FirstPersonArt::ViewmodelFrame(weapon, suppressed, gesture);
        sf::Vector2i first = frame.rect.size, last;
        std::size_t opaque = 0;
        for (int y = 0; y < frame.rect.size.y; ++y) for (int x = 0; x < frame.rect.size.x; ++x) {
          const auto pixel = sf::Vector2u(frame.rect.position + sf::Vector2i{x, y});
          if (art.ViewmodelPixels().getPixel(pixel).a == 0) continue;
          first.x = std::min(first.x, x); first.y = std::min(first.y, y);
          last.x = std::max(last.x, x + 1); last.y = std::max(last.y, y + 1); ++opaque;
        }
        require(opaque > 1000, "el arma seleccionada no tiene una silueta reconocible");
        for (const auto &format : formats) {
          const sf::Vector2f viewport(format.size);
          const auto placement = FirstPersonComposition::PlaceViewmodel(viewport, sf::Vector2f(frame.rect.size), frame.anchor, gesture == 1 ? 1.f : 0.f);
          const auto topLeft = placement.position + sf::Vector2f(first) * placement.scale;
          const auto bottomRight = placement.position + sf::Vector2f(last) * placement.scale;
          const float clearance = topLeft.y / viewport.y - .5f;
          minimumClearance = std::min(minimumClearance, clearance);
          require(clearance > .06f, "un arma, silenciador o gesto invade el espacio libre de la mira");
          require(topLeft.x >= 0.f && bottomRight.x <= viewport.x && topLeft.y >= 0.f, "la composición recorta lateralmente un arma o silenciador");
          require(bottomRight.y >= viewport.y * .98f, "las muñecas flotan sobre el borde inferior");
          require(std::abs((bottomRight.x - topLeft.x) / (bottomRight.y - topLeft.y) - static_cast<float>(last.x - first.x) / (last.y - first.y)) < .0001f,
            "la composición estira una silueta del atlas");
          logicalCompositions.push_back({{"aspect", format.name}, {"logicalSize", {format.size.x, format.size.y}}, {"weapon", weapon == WeaponKind::Pistol ? "pistol" : "nonlethal"},
            {"suppressed", suppressed}, {"gesture", gestures[gesture]}, {"sourceOpaqueBounds", {first.x, first.y, last.x, last.y}}, {"opaquePixels", opaque},
            {"uniformScale", placement.scale}, {"position", {placement.position.x, placement.position.y}}, {"clearanceBelowAimHeightFraction", clearance}});
        }
      }
    for (const auto &format : formats) {
      window.setSize(format.size); context.window.HandleResize(window.getSize());
      require(window.getSize() == format.size, "la ventana nativa no tiene el formato solicitado");
      sf::Image centralNative, centralWindow;
      std::set<std::uint64_t> variants;
      for (const auto weapon : {WeaponKind::Pistol, WeaponKind::NonLethal}) for (const bool suppressed : {false, true})
        for (unsigned int gesture = 0; gesture < gestures.size(); ++gesture) {
          auto &agent = world.agents[0];
          agent.selectedWeapon = weapon; agent.reloadingWeapon = weapon;
          agent.pistolSuppressorEquipped = suppressed; agent.nonLethalSuppressorEquipped = suppressed;
          agent.reloadRemaining = gesture == 2 ? WeaponConfig::ReloadDuration * .75f : gesture == 3 ? WeaponConfig::ReloadDuration * .25f : 0.f;
          world.events.clear(); renderer.Reset(world, 1);
          if (gesture == 1) {
            // El evento aislado comprueba el gesto sin añadir impactos que cambiarían la zona central.
            world.events.push_back({1, world.tick, 1, weapon == WeaponKind::Pistol ? EventKind::WeaponLethal : EventKind::WeaponNonLethal});
            renderer.Update(world, 1, 0.f);
          }
          const auto name = std::string("fps-composition-") + format.name + '-' + (weapon == WeaponKind::Pistol ? "pistol" : "nonlethal") +
            (suppressed ? "-suppressed-" : "-bare-") + gestures[gesture];
          const auto presented = capture(renderer, world, camera, output / (name + ".png"), true);
          const auto native = renderer.FrameImage();
          require(native.saveToFile(output / (name + "-native.png")), "no se pudo guardar el raster de composición");
          require(presented.getSize() == format.size, "la captura no coincide con la ventana nativa");
          require(native.getSize() == sf::Vector2u{320, 180}, "el letterbox alteró el raster lógico1600×900");
          require(renderer.Stats().actorPixels > 0, "el objetivo frontal no aparece detrás de la mira");
          variants.insert(signature(native));
          const auto actualAim = renderer.Aim(world, 1, camera);
          require(actualAim.ground == aim.ground && actualAim.slope == aim.slope, "el encuadre del arma cambió el destino físico del disparo");
          const auto logical = context.window.GetLogicalSize();
          const auto reticlePixel = window.mapCoordsToPixel(logical * .5f + sf::Vector2f{-9.f, 0.f}, context.window.GetPresentationView());
          require(presented.getPixel(sf::Vector2u(reticlePixel)) == sf::Color{206, 222, 191}, "la mira se desplazó o cambió de color al modificar el encuadre");
          if (centralNative.getSize().x == 0) { centralNative = native; centralWindow = presented; }
          const sf::IntRect centerNative{{static_cast<int>(native.getSize().x * .45f), static_cast<int>(native.getSize().y * .44f)},
            {static_cast<int>(native.getSize().x * .1f), static_cast<int>(native.getSize().y * .12f)}};
          const auto upper = window.mapCoordsToPixel({logical.x * .45f, logical.y * .44f}, context.window.GetPresentationView());
          const auto lower = window.mapCoordsToPixel({logical.x * .55f, logical.y * .56f}, context.window.GetPresentationView());
          const auto nativeDifferences = differences(centralNative, native, centerNative);
          const auto windowDifferences = differences(centralWindow, presented, {upper, lower - upper});
          require(nativeDifferences == 0 && windowDifferences == 0, "un gesto tapa el objetivo o la zona libre alrededor de la mira");
          const auto viewport = context.window.GetViewport();
          captures.push_back({{"name", name}, {"aspect", format.name}, {"nativeWindowSize", {format.size.x, format.size.y}}, {"logicalSize", {logical.x, logical.y}},
            {"rasterSize", {native.getSize().x, native.getSize().y}}, {"viewport", {viewport.position.x, viewport.position.y, viewport.size.x, viewport.size.y}},
            {"weapon", weapon == WeaponKind::Pistol ? "pistol" : "nonlethal"}, {"suppressed", suppressed}, {"gesture", gestures[gesture]},
            {"nativeCenterDifferentPixels", nativeDifferences}, {"presentedCenterDifferentPixels", windowDifferences}, {"aimGround", {actualAim.ground.x, actualAim.ground.y}}, {"aimSlope", actualAim.slope}});
        }
      require(variants.size() == 16, "no se distinguen las dos armas, sus silenciadores y sus cuatro gestos");
    }
    window.setSize(originalSize); context.window.HandleResize(originalSize);
    capture(renderer, world, camera, output / "fps-composition-window-restored.png", true);
    const auto report = nlohmann::json{{"status", "PASS"}, {"checks", checks}, {"captures", captures}, {"logicalCompositions", logicalCompositions},
      {"nativeWindowFormats", formats.size()}, {"capturedVariantsPerWindow", 16}, {"logicalSizeDuringRendererCaptures", {1600, 900}},
      {"minimumOpaqueClearanceBelowAimHeightFraction", minimumClearance}, {"sourceAtlasModified", false},
      {"scope", "64 capturas del renderer en cuatro ventanas con letterbox y lógico fijo1600×900;64 composiciones geométricas con alfa real del atlas en cuatro aspectos lógicos; estados preparados, no adquisición de controles ni prueba balística"}};
    std::ofstream(output / "fps-composition-fixtures.json") << report.dump(2);
    return report;
  }
}
