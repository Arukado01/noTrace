#pragma once

#include <SFML/System/Vector2.hpp>
#include <SFML/System/Vector3.hpp>
#include <cstdint>

namespace NoTrace {
  enum class TraversalMode { Grounded, Attach, Hang, Shimmy, Transfer, Climb, Drop };
  struct TraversalState {
    TraversalMode mode = TraversalMode::Grounded;
    std::uint32_t surfaceId = 0;
    std::uint32_t linkId = 0;
    std::uint32_t targetSupport = 0;
    bool reversed = false;
    float parameter = 0.f;
    float progress = 0.f;
    sf::Vector2f direction;
    sf::Vector3f from;
    sf::Vector3f to;
  };
}
