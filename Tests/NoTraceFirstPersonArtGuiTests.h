#pragma once

#include "NoTraceFirstPersonArt.h"
#include "Core/EngineContext.h"
#include <SFML/Graphics/RenderTexture.hpp>
#include <SFML/Graphics/Sprite.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>

namespace EngineTests {
  inline nlohmann::json RunFirstPersonArtRenderFixtures(EngineContext &context, const std::filesystem::path &output) {
    using namespace NoTrace;
    unsigned int checks = 0;
    const auto require = [&](bool valid, const char *message) { ++checks; if (!valid) throw std::runtime_error(std::string("FPS arte: ") + message); };
    FirstPersonArt art(context);
    auto *actors = context.resources.FetchTexture(FirstPersonArtConfig::Actors);
    auto *weapons = context.resources.FetchTexture(FirstPersonArtConfig::Viewmodels);
    require(actors && actors == context.resources.FetchTexture(FirstPersonArtConfig::Actors), "el atlas frontal no reutiliza la caché real");
    require(weapons && weapons == context.resources.FetchTexture(FirstPersonArtConfig::Viewmodels), "las armas no reutilizan la caché real");
    require(!actors->isSmooth() && !weapons->isSmooth(), "los sprites frontales interpolan sus píxeles");
    std::set<std::uint64_t> humanFrames, weaponFrames;
    nlohmann::json evidence = nlohmann::json::array();
    const auto inspect = [](const sf::Image &image, sf::IntRect rect) {
      int left = rect.size.x, top = rect.size.y, right = -1, bottom = -1;
      unsigned int count = 0, leftFoot = 0, rightFoot = 0;
      std::uint64_t hash = 14695981039346656037ull;
      for (int y = 0; y < rect.size.y; ++y) for (int x = 0; x < rect.size.x; ++x) {
        const auto color = image.getPixel(sf::Vector2u(rect.position + sf::Vector2i{x, y}));
        for (const auto value : {color.r, color.g, color.b, color.a}) { hash ^= value; hash *= 1099511628211ull; }
        if (color.a < 32) continue;
        ++count; left = std::min(left, x); top = std::min(top, y); right = std::max(right, x); bottom = std::max(bottom, y);
        if (y >= 116 && y < 124) { if (x < 64) ++leftFoot; else ++rightFoot; }
      }
      return nlohmann::json{{"bounds", {left, top, right + 1, bottom + 1}}, {"pixels", count}, {"fingerprint", hash}, {"leftFoot", leftFoot}, {"rightFoot", rightFoot}};
    };
    for (unsigned int variant = 0; variant < 4; ++variant) {
      nlohmann::json walkA, walkB;
      for (unsigned int state = 0; state < 9; ++state) {
        const auto frame = FirstPersonArt::ActorFrame(variant, state);
        auto entry = inspect(art.ActorsPixels(), frame.rect);
        const auto bounds = entry["bounds"].get<std::array<int, 4>>();
        require(entry["pixels"].get<unsigned int>() > 40, "hay una figura frontal vacía");
        require(bounds[0] >= 1 && bounds[2] < 128 && bounds[1] >= 4 && bounds[3] <= 124 && bounds[3] >= 122, "un cuerpo frontal pierde su apoyo o invade otra celda");
        const int maximum = state >= 6 ? 34 : state == 5 ? 83 : 112;
        require(bounds[3] - bounds[1] <= maximum, "una postura supera su altura autorada46/34/14 con redondeo de un píxel fuente");
        require(frame.anchor == sf::Vector2f{64.f, 124.f}, "la pose cambia su ancla física");
        if (variant < 3) humanFrames.insert(entry["fingerprint"].get<std::uint64_t>());
        if (state == 1) walkA = entry;
        if (state == 2) walkB = entry;
        entry["variant"] = variant; entry["state"] = state; evidence.push_back(entry);
      }
      if (variant < 3) {
        require(walkA["leftFoot"].get<unsigned int>() > walkA["rightFoot"].get<unsigned int>() &&
            walkB["rightFoot"].get<unsigned int>() > walkB["leftFoot"].get<unsigned int>(), "las fases de marcha no alternan el apoyo delantero real");
      }
    }
    require(humanFrames.size() == 27, "las27poses humanas no son fuentes distintas");
    for (const auto weapon : {WeaponKind::Pistol, WeaponKind::NonLethal}) for (const bool suppressed : {false, true}) for (unsigned int state = 0; state < 4; ++state) {
      const auto frame = FirstPersonArt::ViewmodelFrame(weapon, suppressed, state);
      auto entry = inspect(art.ViewmodelPixels(), frame.rect);
      const auto bounds = entry["bounds"].get<std::array<int, 4>>();
      require(entry["pixels"].get<unsigned int>() > 100, "un arma o sus manos están vacías");
      require(bounds[0] >= 0 && bounds[2] <= 256 && bounds[1] >= 0 && bounds[3] >= 156 && bounds[3] <= 160, "las manos no llegan al borde inferior de la vista");
      require(frame.anchor == sf::Vector2f{128.f, 160.f}, "el viewmodel pierde su ancla de pantalla");
      weaponFrames.insert(entry["fingerprint"].get<std::uint64_t>());
    }
    require(weaponFrames.size() == 16, "arma, silenciador o recarga reutilizan una imagen sin adaptar");
    const auto capture = [&](const sf::Texture &texture, const char *filename) {
      sf::RenderTexture target(texture.getSize()); target.clear({26, 34, 42}); target.draw(sf::Sprite(texture)); target.display();
      require(target.getTexture().copyToImage().saveToFile(output / filename), "no se pudo guardar el mosaico GPU frontal");
    };
    capture(*actors, "fps-actors-atlas-gpu.png"); capture(*weapons, "fps-viewmodels-atlas-gpu.png");
    nlohmann::json report{{"status", "PASS"}, {"checks", checks}, {"actorFrames", 36}, {"humanFrames", humanFrames.size()}, {"viewmodelFrames", weaponFrames.size()},
        {"trainingStaticAliases", true}, {"scope", "Atlas frontal GPU real y caché; profundidad, combate y LAN se validan en pruebas separadas"}, {"evidence", evidence}};
    std::ofstream(output / "first-person-art-fixtures.json") << report.dump(2);
    return report;
  }
}
