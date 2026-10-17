#pragma once
#include "Types/InputTypes.h"
#include "Utils/InputBindings.h"
#include <SFML/Window/Event.hpp>
#include <unordered_map>

class InputManager {
private:
  std::unordered_multimap<int, Input::Binding> bindings_;
  bool fullscreenShortcutHeld_ = false;
  sf::Keyboard::Scancode fullscreenShortcutKey_ = sf::Keyboard::Scan::Unknown;

public:
  void Bind(int action, const Input::Binding &binding);
  bool Pressed(int action) const;
  void Clear();
  GlobalShortcut HandleGlobalEvent(const sf::Event &event, bool displayConfirmationPending);

private:
  bool Pressed(const Input::Keyboard &) const;
  bool Pressed(const Input::Mouse &) const;
  bool Pressed(const Input::Gamepad &) const;
  bool Pressed(const Input::Axis &) const;
};
