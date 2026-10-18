#pragma once

#include "Config/NoTraceFirstPersonConfig.h"
#include <SFML/System/Vector2.hpp>
#include <algorithm>

namespace NoTrace::FirstPersonComposition {
  struct Placement {
    sf::Vector2f position;
    float scale = 0.f;
  };

  inline Placement PlaceViewmodel(sf::Vector2f viewport, sf::Vector2f sourceSize, sf::Vector2f anchor, float recoil) {
    // Una escala uniforme ligada a la altura conserva manos y silenciador al variar el aspecto.
    const float scale = viewport.y * FirstPersonConfig::ViewmodelHeightFraction / sourceSize.y;
    const float offset = std::clamp(recoil, 0.f, 1.f) * viewport.y * FirstPersonConfig::ViewmodelRecoilHeightFraction;
    return {{viewport.x * .5f - anchor.x * scale, viewport.y - anchor.y * scale + offset}, scale};
  }
}
