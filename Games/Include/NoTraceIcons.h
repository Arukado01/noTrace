#pragma once

#include "Config/NoTraceIconsConfig.h"
#include <SFML/Graphics/VertexArray.hpp>
#include <SFML/System/Vector2.hpp>

namespace NoTrace { enum class ObjectKind; }
namespace NoTrace::Icons {
  sf::Color Tint(Kind kind);
  Palette Colors(Kind kind, sf::Color tint);
  Kind ForObject(ObjectKind kind);
  sf::VertexArray Geometry(Kind kind, sf::Vector2f center, float pixelSize, sf::Color tint, float progress = 1.f);
}
