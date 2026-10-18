#pragma once

#include "Core/EngineContext.h"
#include "NoTraceWorldRenderer.h"
#include "NoTraceCharacterAtlasFixture.h"
#include <array>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <map>
#include <set>
#include <stdexcept>

namespace EngineTests {
  template <typename Capture>
  nlohmann::json RunOrientationRenderFixtures(EngineContext &context, const std::filesystem::path &output, Capture capture) {
    using namespace NoTrace;
    CharacterAtlasFixture atlas;
    int checks = 0;
    const auto require = [&](bool condition, const std::string &message) {
      ++checks;
      if (!condition) throw std::runtime_error("Orientación gráfica: " + message);
    };
    WorldState world; world.phase = GamePhase::Base; world.size = {1000.f, 1000.f};
    Agent observer; observer.id = 1; observer.position = {400.f, 400.f}; observer.facing = {1.f, 0.f}; world.agents.push_back(observer);
    Agent subject; subject.id = 2; subject.position = {450.f, 400.f}; subject.moving = true; subject.aiming = true; subject.footstepTime = 0.25f; subject.animationTime = 0.25f;
    world.agents.push_back(subject);
    WorldRenderer renderer(context);
    nlohmann::json evidence = nlohmann::json::array();
    const std::array<sf::Vector2f, 8> directions{{{1.f, 0.f}, {1.f, 1.f}, {0.f, 1.f}, {-1.f, 1.f}, {-1.f, 0.f}, {-1.f, -1.f}, {0.f, -1.f}, {1.f, -1.f}}};
    for (int zoomIndex = 0; zoomIndex < 2; ++zoomIndex) {
      renderer.AdjustZoom(zoomIndex == 0 ? -100.f : 100.f);
      const float zoom = renderer.GetZoom();
      const auto logical = context.window.GetLogicalSize();
      const auto focus = WorldRenderer::ProjectGround(observer.position, observer.position, logical);
      auto empty = world; empty.agents.resize(1);
      const auto absent = capture(renderer, empty, output / ("orientation-absent-" + std::to_string(zoomIndex) + ".png"), false);
      const auto nativeScale = sf::Vector2f(absent.getSize()).componentWiseDiv(logical);
      const auto physical = [&](sf::Vector2f point) {
        const auto pixel = (focus + (point - focus) * zoom).componentWiseMul(nativeScale);
        return sf::Vector2i{static_cast<int>(std::floor(pixel.x)), static_cast<int>(std::floor(pixel.y))};
      };
      const auto inImage = [&](sf::Vector2i pixel) { return pixel.x >= 0 && pixel.y >= 0 && pixel.x < static_cast<int>(absent.getSize().x) && pixel.y < static_cast<int>(absent.getSize().y); };
      for (const auto stance : {Stance::Standing, Stance::Crouched, Stance::Prone}) for (std::size_t direction = 0; direction < directions.size(); ++direction) {
        auto &agent = world.agents[1]; agent.stance = stance; agent.facing = directions[direction].normalized(); agent.weaponAim = agent.facing;
        const std::string name = std::to_string(static_cast<int>(stance)) + "-" + std::to_string(direction) + "-" + std::to_string(zoomIndex);
        const auto first = capture(renderer, world, output / ("orientation-" + name + "-forward.png"), false);
        const auto body = WorldRenderer::SelectBodySprite(agent);
        const auto feet = WorldRenderer::ProjectGround(agent.position, observer.position, logical);
        const auto placement = WorldRenderer::PlaceSprite(feet, agent.facing, stance, false, body, {192, 192});
        std::map<std::pair<int, int>, unsigned int> legs;
        std::set<std::pair<int, int>> weaponPixels;
        std::set<std::pair<int, int>> lowerPixels;
        const auto markWeapon = [&] {
          const auto upper = WorldRenderer::SelectWeaponSprite(agent);
          const auto rect = atlas.Rect(upper.index, "upper");
          const auto offset = atlas.Offset(upper.index, "upper");
          const auto &image = atlas.ImageFor(upper.index, "upper");
          const auto upperFeet = renderer.CharacterSprites().AlignUpper(body.index, upper.index, feet);
          const auto upperPlacement = WorldRenderer::PlaceSprite(upperFeet, WeaponFacing(agent), stance, false, upper, {192, 192});
          const auto mark = [&](sf::Vector2f point) {
            const auto firstPixel = physical(point - sf::Vector2f{CharacterArt::Scale() * .5f, CharacterArt::Scale() * .5f});
            const auto lastPixel = physical(point + sf::Vector2f{CharacterArt::Scale() * .5f, CharacterArt::Scale() * .5f});
            for (int y = firstPixel.y - 1; y <= lastPixel.y + 1; ++y) for (int x = firstPixel.x - 1; x <= lastPixel.x + 1; ++x) weaponPixels.emplace(x, y);
          };
          for (int y = 0; y < rect.size.y; ++y) for (int x = 0; x < rect.size.x; ++x) {
            if (image.getPixel(sf::Vector2u(rect.position + sf::Vector2i{x, y})).a == 0) continue;
            mark(upperPlacement.Map(offset + sf::Vector2f{x + .5f, y + .5f}));
          }
          const auto &prop = renderer.CharacterSprites().WeaponMetadata(CharacterArt::Weapon::Pistol, true, upper.index / SpriteConfig::Columns % SpriteConfig::Directions);
          const auto grip = renderer.CharacterSprites().Map(upper.index, renderer.CharacterSprites().Metadata(upper.index).grip, upperFeet);
          for (int y = 0; y < prop.rect.size.y; ++y) for (int x = 0; x < prop.rect.size.x; ++x) {
            if (atlas.weapons.getPixel(sf::Vector2u(prop.rect.position + sf::Vector2i{x, y})).a == 0) continue;
            mark(grip + (sf::Vector2f{x + .5f, y + .5f} - prop.grip) * CharacterArt::Scale());
          }
        };
        markWeapon();
        agent.weaponAim = -agent.facing;
        const auto second = capture(renderer, world, output / ("orientation-" + name + "-backward.png"), false);
        markWeapon();
        const auto rect = atlas.Rect(body.index, "lower");
        const auto lowerOffset = atlas.Offset(body.index, "lower");
        const auto &lowerImage = atlas.ImageFor(body.index, "lower");
        for (int y = 0; y < rect.size.y; ++y) for (int x = 0; x < rect.size.x; ++x) {
          const sf::Vector2f point = lowerOffset + sf::Vector2f{x + 0.5f, y + 0.5f};
          const auto alpha = lowerImage.getPixel(sf::Vector2u(rect.position + sf::Vector2i{x, y})).a;
          const auto lowerPixel = physical(placement.Map(point));
          if (alpha > 0) for (int dy = -2; dy <= 2; ++dy) for (int dx = -2; dx <= 2; ++dx) lowerPixels.emplace(lowerPixel.x + dx, lowerPixel.y + dy);
          if (alpha < SpriteConfig::SupportAlpha) continue;
          const auto pixel = physical(placement.Map(point));
          if (inImage(pixel) && (stance == Stance::Prone || !weaponPixels.contains({pixel.x, pixel.y}))) legs.emplace(std::pair{pixel.x, pixel.y}, alpha);
        }
        int changedLegs = 0, visibleLegs = 0, blendedLegEdges = 0;
        for (const auto &[position, alpha] : legs) {
          const auto [x, y] = position;
          const sf::Vector2u pixel{static_cast<unsigned int>(x), static_cast<unsigned int>(y)};
          if (first.getPixel(pixel) != absent.getPixel(pixel)) ++visibleLegs;
          const auto before = first.getPixel(pixel), after = second.getPixel(pixel);
          const int difference = std::max({std::abs(static_cast<int>(before.r) - after.r), std::abs(static_cast<int>(before.g) - after.g), std::abs(static_cast<int>(before.b) - after.b)});
          // El torso posterior puede alterar el fondo visible a través del alfa de las piernas sin desplazar su silueta.
          if (difference > static_cast<int>(alpha == 255 ? 0 : 255 - alpha + 1)) ++changedLegs;
          else if (difference > 0) ++blendedLegEdges;
        }
        require(visibleLegs > 12, "la máscara del atlas no conserva piernas visibles: " + name);
        require(changedLegs == 0, "la puntería contraria cambia " + std::to_string(changedLegs) + " píxeles de piernas: " + name);
        const auto shadow = placement.Map(renderer.CharacterSprites().Metadata(body.index).anchor);
        int shadowSamples = 0;
        std::set<std::pair<int, int>> testedShadow;
        const float footprint = std::max(stance == Stance::Prone ? 34.f : 24.f, renderer.CharacterSprites().Metadata(body.index).supportWidth * CharacterArt::Scale());
        for (const float y : {-3.f, -1.f, 1.f, 3.f, 5.f}) {
          const float row = y - 1.f, width = footprint * (1.f - row * row / 36.f);
          for (float x = -width * .5f + .5f; x < width * .5f - .5f; x += 1.f) {
          const auto sample = physical(shadow + sf::Vector2f{x, y});
          if (!inImage(sample)) continue;
          if (weaponPixels.contains({sample.x, sample.y}) || lowerPixels.contains({sample.x, sample.y})) continue;
          if (!testedShadow.emplace(sample.x, sample.y).second) continue;
          const auto pixel = sf::Vector2u(sample);
          require(first.getPixel(pixel) == second.getPixel(pixel), "la puntería mueve la sombra del apoyo corporal: " + name);
          if (first.getPixel(pixel) != absent.getPixel(pixel)) ++shadowSamples;
          }
        }
        require(shadowSamples > 0, "no se capturan los píxeles reales de sombra: " + name);
        int changedUpper = 0;
        const auto topLeft = physical(placement.Map({0.f, 0.f})), bottomRight = physical(placement.Map({192.f, 192.f}));
        for (int y = std::max(0, topLeft.y); y <= std::min(static_cast<int>(first.getSize().y) - 1, bottomRight.y); ++y)
          for (int x = std::max(0, topLeft.x); x <= std::min(static_cast<int>(first.getSize().x) - 1, bottomRight.x); ++x)
            if (first.getPixel({static_cast<unsigned int>(x), static_cast<unsigned int>(y)}) != second.getPixel({static_cast<unsigned int>(x), static_cast<unsigned int>(y)})) ++changedUpper;
        require(changedUpper > 20, "el arma y torso no cambian al apuntar hacia el lado opuesto: " + name);
        require(WorldRenderer::SelectBodySprite(agent).index == body.index, "el apuntado sustituyó el ciclo corporal de movimiento");
        evidence.push_back({{"case", name}, {"zoom", zoom}, {"stance", static_cast<int>(stance)}, {"direction", direction},
          {"body_frame", body.index}, {"protected_leg_pixels", visibleLegs}, {"changed_leg_pixels", changedLegs}, {"alpha_blended_leg_edges", blendedLegEdges}, {"weapon_pixels_excluded", weaponPixels.size()},
          {"shadow_samples", shadowSamples}, {"changed_upper_pixels", changedUpper}, {"laser_source", "observer_without_aim"}});
      }
    }
    nlohmann::json report{{"status", "PASS"}, {"checks", checks}, {"cases", evidence},
      {"legComparison", "Máscara de capa lower autorada; se excluye cobertura real del torso/arma. Píxeles opacos exigen igualdad exacta y bordes sólo255-alfa más un nivel de redondeo."},
      {"shadowComparison", "Igualdad exacta de sombra visible fuera de las máscaras lower/upper/arma; el cañón agachado podía tapar la muestra fija+5."}, {"atlasSchema", 3}};
    std::ofstream file(output / "orientation-fixtures.json"); file << report.dump(2);
    return report;
  }
}
