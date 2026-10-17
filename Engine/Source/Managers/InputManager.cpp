#include "Managers/InputManager.h"
#include "SFML/Window/Joystick.hpp"
#include "Utils/InputBindings.h"
#include <algorithm>
#include <cmath>
#include <type_traits>

void InputManager::Bind(int action, const Input::Binding &binding) {
  bindings_.emplace(action, binding);
}

void InputManager::BeginFrame() {
  pressedKeys_.clear();
  keyEdges_.clear();
  pressedMouseButtons_.clear();
  pressedGamepadButtons_.clear();
  wheelDelta_ = 0.f;
}

void InputManager::CaptureEvent(const sf::Event &event, bool gameplayAllowed) {
  if (event.is<sf::Event::FocusLost>()) {
    heldKeys_.clear(); heldMouseButtons_.clear(); heldGamepadButtons_.clear();
    BeginFrame();
    return;
  }
  if (const auto *disconnected = event.getIf<sf::Event::JoystickDisconnected>()) {
    std::erase_if(heldGamepadButtons_, [disconnected](std::uint64_t button) { return (button >> 32) == disconnected->joystickId; });
    std::erase_if(pressedGamepadButtons_, [disconnected](std::uint64_t button) {
      return (button >> 32) == disconnected->joystickId;
    });
  }
  if (const auto *key = event.getIf<sf::Event::KeyReleased>()) heldKeys_.erase(key->scancode);
  if (const auto *mouse = event.getIf<sf::Event::MouseButtonReleased>()) heldMouseButtons_.erase(mouse->button);
  if (const auto *gamepad = event.getIf<sf::Event::JoystickButtonReleased>()) heldGamepadButtons_.erase((static_cast<std::uint64_t>(gamepad->joystickId) << 32) | gamepad->button);
  if (!gameplayAllowed) {
    return;
  }
  if (const auto *wheel = event.getIf<sf::Event::MouseWheelScrolled>()) {
    if (wheel->wheel == sf::Mouse::Wheel::Vertical && std::isfinite(wheel->delta)) {
      wheelDelta_ = std::clamp(wheelDelta_ + wheel->delta, -100.f, 100.f);
    }
    return;
  }
  // Conserva los pulsos breves hasta Update aunque su liberación llegue en el mismo frame.
  if (const auto *key = event.getIf<sf::Event::KeyPressed>()) {
    if (key->scancode != sf::Keyboard::Scan::Unknown) {
      if (heldKeys_.insert(key->scancode).second) keyEdges_.insert(key->scancode);
      pressedKeys_.insert(key->scancode);
    }
  } else if (const auto *mouse = event.getIf<sf::Event::MouseButtonPressed>()) {
    heldMouseButtons_.insert(mouse->button);
    pressedMouseButtons_.insert(mouse->button);
  } else if (const auto *gamepad = event.getIf<sf::Event::JoystickButtonPressed>()) {
    if (gamepad->joystickId < sf::Joystick::Count && gamepad->button < sf::Joystick::ButtonCount) {
      pressedGamepadButtons_.insert((static_cast<std::uint64_t>(gamepad->joystickId) << 32) | gamepad->button);
      heldGamepadButtons_.insert((static_cast<std::uint64_t>(gamepad->joystickId) << 32) | gamepad->button);
    }
  }
}

/**Busca todos los bindings de esta acción. Luego revisa cada binding.
 * Dependiendo del tipo de entrada que sea, comprueba si esa entrada está
 * presionada. Si al menos una está presionada, devuelve true **/

//  Pressed(action):

//     bindings = todos los bindings de action <Keyboard, Mouse, Gamepad, Axis>

//     para cada binding:
//         si la entrada está presionada:
//             return true

//     return false
bool InputManager::Pressed(int action) const {
  const auto [first, last] = bindings_.equal_range(action);

  return std::any_of(first, last, [this](const auto &entry) {
    return std::visit(
        [this](const auto &binding) { return this->Pressed(binding); },
        entry.second);
  });
}

void InputManager::Clear() {
  bindings_.clear();
  heldKeys_.clear(); heldMouseButtons_.clear(); heldGamepadButtons_.clear();
  BeginFrame();
}

bool InputManager::Held(int action) const {
  const auto [first, last] = bindings_.equal_range(action);
  return std::any_of(first, last, [this](const auto &entry) {
    return std::visit([this](const auto &binding) {
      using Binding = std::decay_t<decltype(binding)>;
      if constexpr (std::is_same_v<Binding, Input::Keyboard>) return heldKeys_.contains(binding.key) || (hardwarePolling_ && sf::Keyboard::isKeyPressed(binding.key));
      else if constexpr (std::is_same_v<Binding, Input::Mouse>) return heldMouseButtons_.contains(binding.button) || (hardwarePolling_ && sf::Mouse::isButtonPressed(binding.button));
      else if constexpr (std::is_same_v<Binding, Input::Gamepad>) {
        const auto button = Input::LogicalToHardware(binding.button, binding.id);
        return button && (heldGamepadButtons_.contains((static_cast<std::uint64_t>(binding.id) << 32) | static_cast<unsigned int>(*button)) || (hardwarePolling_ && sf::Joystick::isButtonPressed(binding.id, *button)));
      } else return Pressed(binding);
    }, entry.second);
  });
}

bool InputManager::JustPressed(int action) const {
  const auto [first, last] = bindings_.equal_range(action);
  return std::any_of(first, last, [this](const auto &entry) {
    return std::visit([this](const auto &binding) {
      using Binding = std::decay_t<decltype(binding)>;
      if constexpr (std::is_same_v<Binding, Input::Keyboard>) return keyEdges_.contains(binding.key);
      else if constexpr (std::is_same_v<Binding, Input::Mouse>) return pressedMouseButtons_.contains(binding.button);
      else if constexpr (std::is_same_v<Binding, Input::Gamepad>) {
        const auto button = Input::LogicalToHardware(binding.button, binding.id);
        return button && pressedGamepadButtons_.contains((static_cast<std::uint64_t>(binding.id) << 32) | static_cast<unsigned int>(*button));
      } else return false;
    }, entry.second);
  });
}

GlobalShortcut InputManager::HandleGlobalEvent(const sf::Event &event,
                                             bool displayConfirmationPending) {
  if (event.is<sf::Event::FocusLost>() || event.is<sf::Event::FocusGained>()) {
    // Una liberación perdida al recrear la ventana no debe bloquear los siguientes atajos.
    if (event.is<sf::Event::FocusLost>() ||
        (fullscreenShortcutHeld_ && (!hardwarePolling_ || !sf::Keyboard::isKeyPressed(fullscreenShortcutKey_)))) {
      fullscreenShortcutHeld_ = false;
      fullscreenShortcutKey_ = sf::Keyboard::Scan::Unknown;
    }
    return GlobalShortcut::None;
  }

  if (const auto *key = event.getIf<sf::Event::KeyPressed>()) {
    if (key->scancode == sf::Keyboard::Scan::F11 ||
        (key->scancode == sf::Keyboard::Scan::Enter && key->alt)) {
      if (fullscreenShortcutHeld_) {
        return GlobalShortcut::Consume;
      }
      fullscreenShortcutHeld_ = true;
      fullscreenShortcutKey_ = key->scancode;
      return displayConfirmationPending ? GlobalShortcut::Consume : GlobalShortcut::ToggleFullscreen;
    }
    if (displayConfirmationPending) {
      if (key->scancode == sf::Keyboard::Scan::Escape) {
        return GlobalShortcut::RevertDisplay;
      }
      if (key->scancode == sf::Keyboard::Scan::R || key->scancode == sf::Keyboard::Scan::M) {
        return GlobalShortcut::Consume;
      }
    }
  }

  if (const auto *key = event.getIf<sf::Event::KeyReleased>()) {
    if (key->scancode == fullscreenShortcutKey_) {
      fullscreenShortcutHeld_ = false;
      fullscreenShortcutKey_ = sf::Keyboard::Scan::Unknown;
      return GlobalShortcut::Consume;
    }
    if (key->scancode == sf::Keyboard::Scan::F11) {
      return GlobalShortcut::Consume;
    }
  }

  if (displayConfirmationPending) {
    std::optional<GamepadButton> button;
    if (const auto *pressed = event.getIf<sf::Event::JoystickButtonPressed>()) {
      button = Input::HardwareToLogical(pressed->button, pressed->joystickId);
    } else if (const auto *released = event.getIf<sf::Event::JoystickButtonReleased>()) {
      button = Input::HardwareToLogical(released->button, released->joystickId);
    }
    if (button == GamepadButton::Select || button == GamepadButton::Start) {
      return GlobalShortcut::Consume;
    }
  }
  return GlobalShortcut::None;
}

bool InputManager::Pressed(const Input::Keyboard &binding) const {
  return pressedKeys_.contains(binding.key) || (hardwarePolling_ && sf::Keyboard::isKeyPressed(binding.key));
}

bool InputManager::Pressed(const Input::Mouse &binding) const {
  return pressedMouseButtons_.contains(binding.button) || (hardwarePolling_ && sf::Mouse::isButtonPressed(binding.button));
}

bool InputManager::Pressed(const Input::Gamepad &binding) const {
  if (binding.id < 0 || binding.id >= static_cast<int>(sf::Joystick::Count)) return false;
  const auto button = Input::LogicalToHardware(binding.button, binding.id);
  if (!button) return false;
  const auto key = (static_cast<std::uint64_t>(binding.id) << 32) | static_cast<unsigned int>(*button);
  return pressedGamepadButtons_.contains(key) || (hardwarePolling_ && sf::Joystick::isButtonPressed(binding.id, *button));
}

/** Se lee el valor del joysticj axis y se divide entre 100 para normalizarlo
 * entre -1 y 1 **/
bool InputManager::Pressed(const Input::Axis &binding) const {
  if (!hardwarePolling_) return false;
  const float axis =
      sf::Joystick::getAxisPosition(binding.id, binding.axis) / 100;
  return (binding.threshold >= 0) ? axis > binding.threshold
                                  : axis < binding.threshold;
}
