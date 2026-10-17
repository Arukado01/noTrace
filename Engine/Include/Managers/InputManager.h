#pragma once
#include "Types/InputTypes.h"
#include "Utils/InputBindings.h"
#include <SFML/Window/Event.hpp>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>

namespace EngineTests { struct GuiAccess; }

class InputManager {
private:
  friend struct EngineTests::GuiAccess;
  std::unordered_multimap<int, Input::Binding> bindings_;
  std::unordered_set<sf::Keyboard::Scancode> pressedKeys_;
  std::unordered_set<sf::Mouse::Button> pressedMouseButtons_;
  std::unordered_set<std::uint64_t> pressedGamepadButtons_;
  bool fullscreenShortcutHeld_ = false;
  sf::Keyboard::Scancode fullscreenShortcutKey_ = sf::Keyboard::Scan::Unknown;
  float wheelDelta_ = 0.f;
  bool hardwarePolling_ = true;

public:
  void Bind(int action, const Input::Binding &binding);
  void BeginFrame();
  void CaptureEvent(const sf::Event &event, bool gameplayAllowed = true);
  bool Pressed(int action) const;
  float WheelDelta() const { return wheelDelta_; }
  void Clear();
  GlobalShortcut HandleGlobalEvent(const sf::Event &event, bool displayConfirmationPending);

private:
  bool Pressed(const Input::Keyboard &) const;
  bool Pressed(const Input::Mouse &) const;
  bool Pressed(const Input::Gamepad &) const;
  bool Pressed(const Input::Axis &) const;
};
