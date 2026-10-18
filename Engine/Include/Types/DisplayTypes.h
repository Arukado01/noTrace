#pragma once

#include <SFML/System/Vector2.hpp>

struct DisplaySettings {
  sf::Vector2u resolution;
  bool fullscreen = false;
  bool borderless = false;
  bool verticalSync = false;
  unsigned int framerateLimit = 60;

  bool operator==(const DisplaySettings &) const = default;
};
