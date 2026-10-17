#pragma once

#include <SFML/System/Vector2.hpp>

namespace NoTrace {
  enum class ManeuverKind { None, Dive, Roll };
  enum class ManeuverPhase { None, Start, Travel, Land, Recover };
  struct ManeuverState {
    ManeuverKind kind = ManeuverKind::None;
    ManeuverPhase phase = ManeuverPhase::None;
    sf::Vector2f direction{1.f, 0.f};
    float time = 0.f;
    float cooldown = 0.f;
    float travelled = 0.f;
    bool blocked = false;
  };
}
