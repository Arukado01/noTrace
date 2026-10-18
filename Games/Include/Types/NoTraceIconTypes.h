#pragma once

#include <SFML/Graphics/Color.hpp>

namespace NoTrace::Icons {
  enum class Kind { Dossier, Connection, Stairs, Elevator, Door, Interact, Ammunition, Detection, Unconscious, Dead, Count };
  struct Palette { sf::Color outline, shadow, fill, accent, inactive; };
}
