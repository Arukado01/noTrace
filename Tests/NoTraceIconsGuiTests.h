#pragma once

#include "NoTraceIcons.h"
#include "Types/NoTraceGameplayTypes.h"
#include <SFML/Graphics/Image.hpp>
#include <SFML/Graphics/RenderTexture.hpp>
#include <array>
#include <filesystem>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <string>

namespace EngineTests {
  inline nlohmann::json RunIconRenderFixtures(const std::filesystem::path &output) {
    using namespace NoTrace;
    int checks = 0;
    const auto require = [&](bool condition, const std::string &message) {
      ++checks;
      if (!condition) throw std::runtime_error("Iconos nativos: " + message);
    };
    std::filesystem::create_directories(output);
    const auto render = [](Icons::Kind kind, unsigned int scale, sf::Color tint, float progress = 1.f) {
      const auto width = static_cast<unsigned int>(IconsConfig::Glyphs[static_cast<std::size_t>(kind)].width);
      sf::RenderTexture target({width * scale, 16u * scale});
      target.setSmooth(false); target.clear(sf::Color::Transparent);
      target.draw(Icons::Geometry(kind, {width * .5f * scale, 8.f * scale}, static_cast<float>(scale), tint, progress));
      target.display(); return target.getTexture().copyToImage();
    };
    const auto colored = [](const sf::Image &image, sf::Color color) {
      std::size_t count = 0;
      for (unsigned int y = 0; y < image.getSize().y; ++y) for (unsigned int x = 0; x < image.getSize().x; ++x) count += image.getPixel({x, y}) == color;
      return count;
    };
    std::set<std::uint64_t> fingerprints;
    nlohmann::json entries = nlohmann::json::array();
    std::string gallery = "<!doctype html><meta charset=utf-8><title>NO TRACE · iconos nativos</title><style>body{background:#07131c;color:#c6dada;font:16px system-ui;margin:24px}main{display:grid;grid-template-columns:repeat(5,180px);gap:16px}article{padding:16px;background:#18212a}img{image-rendering:pixelated;display:block;margin:12px 0}small{color:#91a3ae}</style><h1>Iconos nativos</h1><p>Mapas de píxeles C++ de 16 píxeles de alto dibujados con SFML. Sin fuentes de símbolos ni texturas externas.</p><main>";
    for (std::size_t index = 0; index < IconsConfig::Glyphs.size(); ++index) {
      const auto kind = static_cast<Icons::Kind>(index);
      const auto tint = Icons::Tint(kind);
      const auto palette = Icons::Colors(kind, tint);
      const auto native = render(kind, 1, tint);
      const auto size = native.getSize();
      std::uint64_t fingerprint = 1469598103934665603ull;
      for (std::size_t value = 0; value < size.x * size.y * 4; ++value) fingerprint = (fingerprint ^ native.getPixelsPtr()[value]) * 1099511628211ull;
      require(fingerprints.insert(fingerprint).second, "dos acciones diferentes comparten la misma silueta y colores");
      require(colored(native, sf::Color::Transparent) >= 25 && colored(native, palette.fill) >= 8 && colored(native, palette.accent) >= 4,
        "un glifo pierde su silueta, relleno o detalle semántico a escala nativa");
      nlohmann::json scales = nlohmann::json::array();
      const std::string name(IconsConfig::Glyphs[index].name);
      gallery += "<article><strong>" + name + "</strong>";
      for (const unsigned int scale : {1u, 2u, 3u}) {
        const auto image = render(kind, scale, tint);
        for (unsigned int y = 0; y < size.y * scale; ++y) for (unsigned int x = 0; x < size.x * scale; ++x)
          require(image.getPixel({x, y}) == native.getPixel({x / scale, y / scale}), "el escalado entero interpola o desplaza un píxel: " + name);
        const auto file = "icon-" + name + "-" + std::to_string(scale) + "x.png";
        require(image.saveToFile(output / file), "no se conserva la captura SFML: " + name);
        gallery += "<img src='" + file + "' width='" + std::to_string(size.x * scale) + "' height='" + std::to_string(size.y * scale) + "'><small>" + std::to_string(scale) + "×</small>";
        scales.push_back(scale);
      }
      gallery += "</article>";
      const auto hidden = render(kind, 1, sf::Color::Transparent);
      require(colored(hidden, sf::Color::Transparent) == size.x * size.y, "la opacidad cero conserva píxeles del icono");
      entries.push_back({{"kind", name}, {"pixels", {size.x, size.y}}, {"integerScales", scales}, {"rgbaFingerprint", fingerprint}});
    }
    const auto dossier = render(Icons::Kind::Dossier, 1, Icons::Tint(Icons::Kind::Dossier));
    require(dossier.getPixel({10, 3}) == Icons::Tint(Icons::Kind::Dossier), "el dossier pierde la esquina de papel doblada");
    const auto stairs = render(Icons::Kind::Stairs, 1, Icons::Tint(Icons::Kind::Stairs));
    require(stairs.getPixel({10, 2}) == PresentationConfig::Light && stairs.getPixel({4, 8}) == PresentationConfig::Light && stairs.getPixel({4, 2}).a == 0,
      "la escalera pierde sus peldaños separados por altura");
    const auto ammunition = render(Icons::Kind::Ammunition, 1, Icons::Tint(Icons::Kind::Ammunition));
    for (const unsigned int x : {3u, 7u, 11u}) require(ammunition.getPixel({x, 2}) == PresentationConfig::Amber, "la munición no conserva tres cartuchos separados");
    require(ammunition.getPixel({5, 7}).a == 0 && ammunition.getPixel({9, 7}).a == 0, "los cartuchos se funden en un bloque genérico");
    const auto unconscious = render(Icons::Kind::Unconscious, 1, Icons::Tint(Icons::Kind::Unconscious));
    const auto dead = render(Icons::Kind::Dead, 1, Icons::Tint(Icons::Kind::Dead));
    require(unconscious.getSize() == sf::Vector2u{24u, 16u} && unconscious.getPixel({4, 6}) == PresentationConfig::Outline &&
      unconscious.getPixel({16, 1}) == PresentationConfig::Violet && unconscious.getPixel({19, 7}) == PresentationConfig::Violet &&
      unconscious.getPixel({16, 12}) == PresentationConfig::Violet && colored(unconscious, PresentationConfig::Skin) > 20,
      "la inconsciencia pierde el ojo cerrado, cabeza inclinada o sus tres zetas violetas originales");
    require(dead.getPixel({5, 5}) == PresentationConfig::Outline && dead.getPixel({9, 5}) == PresentationConfig::Outline && dead.getPixel({7, 8}) == PresentationConfig::Outline,
      "la calavera pierde las dos órbitas y la nariz diferenciadas");
    std::size_t previous = 0;
    nlohmann::json progressEvidence = nlohmann::json::array();
    for (const float progress : {0.f, .25f, .5f, 1.f}) {
      const auto eye = render(Icons::Kind::Detection, 1, PresentationConfig::Red, progress);
      const auto count = colored(eye, PresentationConfig::Red);
      require(progress == 0.f ? count == 0 : count > previous, "el ojo no representa el progreso creciente de detección");
      require(eye.getPixel({7, 7}) == PresentationConfig::Light, "el ojo pierde el brillo que separa iris y pupila");
      previous = count;
      const auto file = "icon-Detection-progress-" + std::to_string(static_cast<int>(progress * 100.f)) + ".png";
      require(eye.saveToFile(output / file), "no se conserva la progresión del ojo");
      progressEvidence.push_back({{"progress", progress}, {"accentPixels", count}});
    }
    require(Icons::ForObject(ObjectKind::Terminal) == Icons::Kind::Dossier && Icons::ForObject(ObjectKind::PuzzleNode) == Icons::Kind::Connection &&
      Icons::ForObject(ObjectKind::Elevator) == Icons::Kind::Elevator && Icons::ForObject(ObjectKind::Door) == Icons::Kind::Door && Icons::ForObject(ObjectKind::Armory) == Icons::Kind::Ammunition,
      "la selección semántica confunde objetivo, puzle, ascensor, puerta o munición");
    require(Icons::Geometry(Icons::Kind::Count, {}, 1.f, sf::Color::White).getVertexCount() == 0 &&
      Icons::Geometry(Icons::Kind::Door, {std::numeric_limits<float>::quiet_NaN(), 0.f}, 1.f, sf::Color::White).getVertexCount() == 0,
      "un icono o posición inválidos producen geometría");
    gallery += "</main><p>La integración conserva las reglas de visibilidad, privacidad y selección del mundo; este fixture sólo valida el dibujo nativo de iconos.</p>";
    std::ofstream(output / "icons-gallery.html") << gallery;
    nlohmann::json report{{"status", "PASS"}, {"checks", checks}, {"icons", entries}, {"detectionProgress", progressEvidence}};
    std::ofstream(output / "icons-fixtures.json") << report.dump(2);
    return report;
  }
}
