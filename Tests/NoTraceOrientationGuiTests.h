#pragma once

#include "Core/EngineContext.h"
#include "NoTraceWorldRenderer.h"
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
    struct Frame { sf::IntRect rectangle; sf::Vector2f muzzle; };
    std::ifstream rectangles(SpriteConfig::FrameTable), muzzles(SpriteConfig::MuzzleTable);
    std::vector<Frame> frames;
    Frame frame;
    while (rectangles >> frame.rectangle.position.x >> frame.rectangle.position.y >> frame.rectangle.size.x >> frame.rectangle.size.y) {
      if (!(muzzles >> frame.muzzle.x >> frame.muzzle.y)) throw std::runtime_error("Falta una boca del atlas en el fixture de orientación");
      frames.push_back(frame);
    }
    sf::Image atlas;
    if (!atlas.loadFromFile(std::filesystem::path("Content/Textures") / SpriteConfig::Atlas) || frames.size() != SpriteConfig::FrameCount)
      throw std::runtime_error("No se puede inspeccionar el atlas real para comparar las piernas");
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
        const auto placement = WorldRenderer::PlaceSprite(feet, agent.facing, stance, false, body, frames.at(body.index).rectangle.size);
        const auto pivot = WorldRenderer::TorsoPivot(stance), lowerNormal = WorldRenderer::TorsoForward(stance, agent.facing);
        std::map<std::pair<int, int>, unsigned int> legs;
        std::set<std::pair<int, int>> weaponPixels;
        const auto markWeapon = [&] {
          const auto upper = WorldRenderer::SelectWeaponSprite(agent);
          const auto &rect = frames.at(upper.index).rectangle;
          const auto upperPlacement = WorldRenderer::PlaceSprite(feet, WeaponFacing(agent), stance, false, upper, rect.size);
          const auto normal = WorldRenderer::TorsoForward(stance, WeaponFacing(agent));
          for (int y = 0; y < rect.size.y; ++y) for (int x = 0; x < rect.size.x; ++x) {
            const sf::Vector2f point{x + 0.5f, y + 0.5f};
            const auto color = atlas.getPixel(sf::Vector2u(rect.position + sf::Vector2i{x, y}));
            const int maximum = std::max({color.r, color.g, color.b}), minimum = std::min({color.r, color.g, color.b});
            if (color.a < SpriteConfig::SupportAlpha || maximum - minimum > 18 || maximum > 140 || (point - pivot).dot(normal) < 0.f) continue;
            const auto pixel = physical(upperPlacement.Map(point));
            weaponPixels.emplace(pixel.x, pixel.y);
          }
        };
        markWeapon();
        agent.weaponAim = -agent.facing;
        const auto second = capture(renderer, world, output / ("orientation-" + name + "-backward.png"), false);
        markWeapon();
        const auto &rect = frames.at(body.index).rectangle;
        for (int y = 0; y < rect.size.y; ++y) for (int x = 0; x < rect.size.x; ++x) {
          const sf::Vector2f point{x + 0.5f, y + 0.5f};
          if ((point - pivot).dot(lowerNormal) > -6.f || atlas.getPixel(sf::Vector2u(rect.position + sf::Vector2i{x, y})).a < SpriteConfig::SupportAlpha) continue;
          const auto pixel = physical(placement.Map(point));
          if (inImage(pixel) && !weaponPixels.contains({pixel.x, pixel.y})) legs.emplace(std::pair{pixel.x, pixel.y}, atlas.getPixel(sf::Vector2u(rect.position + sf::Vector2i{x, y})).a);
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
        const auto start = body.index / 4 * 4;
        const auto support = WorldRenderer::MeasureGroundSupport(atlas, {frames.at(start).rectangle, frames.at(start + 1).rectangle,
          frames.at(start + 2).rectangle, frames.at(start + 3).rectangle});
        const auto shadow = placement.Map(support.anchor);
        int shadowSamples = 0;
        for (const float x : {-6.f, 0.f, 6.f}) {
          const auto sample = physical(shadow + sf::Vector2f{x, 5.f});
          if (!inImage(sample)) continue;
          const auto pixel = sf::Vector2u(sample);
          require(first.getPixel(pixel) == second.getPixel(pixel), "la puntería mueve la sombra del apoyo corporal: " + name);
          if (first.getPixel(pixel) != absent.getPixel(pixel)) ++shadowSamples;
        }
        require(shadowSamples > 0, "no se capturan los píxeles reales de sombra: " + name);
        int changedUpper = 0;
        const auto topLeft = physical(placement.Map({0.f, 0.f})), bottomRight = physical(placement.Map(sf::Vector2f(rect.size)));
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
      {"legComparison", "Los píxeles opacos exigen igualdad exacta; los bordes admiten sólo la contribución posterior 255-alfa y un nivel de redondeo. El fallo previo de seis muestras provenía de alfa248–252 y diferencias RGB de uno o dos niveles."}};
    std::ofstream file(output / "orientation-fixtures.json"); file << report.dump(2);
    return report;
  }
}
