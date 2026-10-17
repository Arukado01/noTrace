#include "Managers/InputManager.h"
#include "SFML/Window/Joystick.hpp"
#include "Utils/InputBindings.h"
#include <algorithm>

void InputManager::Bind(int action, const Input::Binding &binding) {
  bindings_.emplace(action, binding);
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

void InputManager::Clear() { bindings_.clear(); }

GlobalShortcut InputManager::HandleGlobalEvent(const sf::Event &event,
                                             bool displayConfirmationPending) {
  if (event.is<sf::Event::FocusLost>() || event.is<sf::Event::FocusGained>()) {
    // Una liberación perdida al recrear la ventana no debe bloquear los siguientes atajos.
    if (event.is<sf::Event::FocusLost>() ||
        (fullscreenShortcutHeld_ && !sf::Keyboard::isKeyPressed(fullscreenShortcutKey_))) {
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
  return sf::Keyboard::isKeyPressed(binding.key);
}

bool InputManager::Pressed(const Input::Mouse &binding) const {
  return sf::Mouse::isButtonPressed(binding.button);
}

bool InputManager::Pressed(const Input::Gamepad &binding) const {
  const auto button = Input::LogicalToHardware(binding.button, binding.id);
  return button ? sf::Joystick::isButtonPressed(binding.id, *button) : false;
}

/** Se lee el valor del joysticj axis y se divide entre 100 para normalizarlo
 * entre -1 y 1 **/
bool InputManager::Pressed(const Input::Axis &binding) const {
  const float axis =
      sf::Joystick::getAxisPosition(binding.id, binding.axis) / 100;
  return (binding.threshold >= 0) ? axis > binding.threshold
                                  : axis < binding.threshold;
}
