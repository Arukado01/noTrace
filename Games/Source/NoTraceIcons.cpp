#include "NoTraceIcons.h"

#include "Types/NoTraceGameplayTypes.h"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace NoTrace::Icons {
  sf::Color Tint(Kind kind) {
    using namespace PresentationConfig;
    switch (kind) {
    case Kind::Dossier: return Cyan;
    case Kind::Connection: return Green;
    case Kind::Stairs: case Kind::Elevator: return Cyan;
    case Kind::Door: case Kind::Ammunition: return Amber;
    case Kind::Detection: case Kind::Dead: return Red;
    case Kind::Unconscious: return Violet;
    default: return Light;
    }
  }

  Palette Colors(Kind kind, sf::Color tint) {
    using namespace PresentationConfig;
    const auto alpha = tint.a;
    const auto opacity = [alpha](sf::Color color) { color.a = alpha; return color; };
    return {opacity(Outline), opacity(WallLeft), opacity(kind == Kind::Unconscious ? Skin : Light), tint, opacity(WallTop)};
  }

  Kind ForObject(ObjectKind kind) {
    switch (kind) {
    case ObjectKind::Operations: case ObjectKind::Intelligence: case ObjectKind::Terminal: case ObjectKind::Secondary: return Kind::Dossier;
    case ObjectKind::Computer: case ObjectKind::CoopSwitch: case ObjectKind::PuzzleNode: case ObjectKind::CameraPanel: case ObjectKind::AlarmPanel: case ObjectKind::Explosive: return Kind::Connection;
    case ObjectKind::Armory: return Kind::Ammunition;
    case ObjectKind::Door: return Kind::Door;
    case ObjectKind::Elevator: return Kind::Elevator;
    case ObjectKind::Camera: return Kind::Detection;
    default: return Kind::Interact;
    }
  }

  sf::VertexArray Geometry(Kind kind, sf::Vector2f center, float pixelSize, sf::Color tint, float progress) {
    sf::VertexArray pixels(sf::PrimitiveType::Triangles);
    const auto index = static_cast<std::size_t>(kind);
    if (index >= IconsConfig::Glyphs.size() || !std::isfinite(center.x) || !std::isfinite(center.y) || !std::isfinite(pixelSize) || pixelSize < 1.f || tint.a == 0) return pixels;
    pixelSize = std::round(pixelSize);
    progress = std::isfinite(progress) ? std::clamp(progress, 0.f, 1.f) : 0.f;
    const auto palette = Colors(kind, tint);
    const auto &glyph = IconsConfig::Glyphs[index];
    const sf::Vector2f origin{std::round(center.x - glyph.width * pixelSize * .5f), std::round(center.y - IconsConfig::Size * pixelSize * .5f)};
    for (std::size_t y = 0; y < IconsConfig::Size; ++y) for (std::size_t x = 0; x < glyph.width; ++x) {
      const char value = glyph.pixels[y][x];
      if (value == '.') continue;
      auto color = value == 'o' ? palette.outline : value == 's' ? palette.shadow : value == 'f' ? palette.fill : palette.accent;
      if (kind == Kind::Detection && (value == 'a' || value == 'p')) {
        const float angle = std::atan2(static_cast<float>(y) - 7.f, static_cast<float>(x) - 7.5f);
        const float turn = std::fmod(angle + std::numbers::pi_v<float> * 3.f, std::numbers::pi_v<float> * 2.f) / (std::numbers::pi_v<float> * 2.f);
        if (progress <= 0.f || (value == 'a' && turn >= progress)) color = palette.inactive;
      }
      const auto point = origin + sf::Vector2f{static_cast<float>(x), static_cast<float>(y)} * pixelSize;
      const sf::Vector2f right{pixelSize, 0.f}, down{0.f, pixelSize};
      for (const auto vertex : {point, point + right, point + right + down, point, point + right + down, point + down}) pixels.append({vertex, color});
    }
    return pixels;
  }
}
