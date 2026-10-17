#include "Managers/InputManager.h"

#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
  int checks = 0;

  void Require(bool condition, const char *message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
  }

  sf::Event KeyDown(sf::Keyboard::Scancode code, bool alt = false) {
    sf::Event::KeyPressed key;
    key.scancode = code;
    key.alt = alt;
    return key;
  }

  sf::Event KeyUp(sf::Keyboard::Scancode code) {
    sf::Event::KeyReleased key;
    key.scancode = code;
    return key;
  }

  void BriefKeyboardPulse() {
    InputManager input;
    input.Bind(1, Input::Keyboard{sf::Keyboard::Scan::F24});
    input.Bind(2, Input::Keyboard{sf::Keyboard::Scan::F23});
    input.BeginFrame();
    input.CaptureEvent(KeyDown(sf::Keyboard::Scan::F24));
    input.CaptureEvent(KeyUp(sf::Keyboard::Scan::F24));
    Require(input.Pressed(1), "Se perdió una tecla pulsada y liberada antes de Update");
    Require(input.Pressed(1), "Consultar una acción consumió el pulso antes de otros consumidores");
    Require(!input.Pressed(2), "Un pulso activó una tecla distinta");
    input.BeginFrame();
    Require(!input.Pressed(1), "El pulso de teclado duró más de un frame");
    input.CaptureEvent(KeyDown(sf::Keyboard::Scan::F24));
    Require(input.Pressed(1), "No se capturó una segunda pulsación breve");
    input.Clear();
    input.Bind(1, Input::Keyboard{sf::Keyboard::Scan::F24});
    Require(!input.Pressed(1), "Clear conservó pulsos de la escena anterior");
  }

  void BriefMousePulseAndSharedAction() {
    InputManager input;
    input.Bind(1, Input::Mouse{sf::Mouse::Button::Extra2});
    input.Bind(1, Input::Keyboard{sf::Keyboard::Scan::F24});
    input.BeginFrame();
    input.CaptureEvent(sf::Event::MouseButtonPressed{sf::Mouse::Button::Extra2, {13, 27}});
    input.CaptureEvent(sf::Event::MouseButtonReleased{sf::Mouse::Button::Extra2, {13, 27}});
    Require(input.Pressed(1), "Se perdió un clic breve antes de Update");
    input.BeginFrame();
    Require(!input.Pressed(1), "El clic breve permaneció activo al siguiente frame");
    input.CaptureEvent(KeyDown(sf::Keyboard::Scan::F24));
    input.CaptureEvent(KeyUp(sf::Keyboard::Scan::F24));
    Require(input.Pressed(1), "Se perdió un binding alternativo de la misma acción");
    input.CaptureEvent(sf::Event::FocusLost{});
    Require(!input.Pressed(1), "La pérdida de foco conservó un clic o tecla pendiente");
  }

  void PhysicalGamepadIdentity() {
    constexpr int firstDevice = 7;
    constexpr int secondDevice = 6;
    InputManager input;
    input.Bind(1, Input::Gamepad{GamepadButton::South, firstDevice});
    input.Bind(2, Input::Gamepad{GamepadButton::South, secondDevice});
    const auto button = Input::LogicalToHardware(GamepadButton::South, firstDevice);
    Require(button.has_value(), "La acción South no tiene correspondencia física");
    input.BeginFrame();
    input.CaptureEvent(sf::Event::JoystickButtonPressed{firstDevice, static_cast<unsigned int>(*button)});
    input.CaptureEvent(sf::Event::JoystickButtonReleased{firstDevice, static_cast<unsigned int>(*button)});
    Require(input.Pressed(1), "Se perdió una pulsación breve del mando");
    Require(!input.Pressed(2), "La pulsación de un mando activó otro dispositivo");
    input.CaptureEvent(sf::Event::JoystickDisconnected{firstDevice}, false);
    Require(!input.Pressed(1), "La desconexión conservó pulsos del mando retirado");
    input.CaptureEvent(sf::Event::JoystickButtonPressed{firstDevice, static_cast<unsigned int>(*button)});
    input.BeginFrame();
    Require(!input.Pressed(1), "El pulso de mando duró más de un frame");
  }

  void BlockedRoutingAndFocus() {
    InputManager input;
    input.Bind(1, Input::Keyboard{sf::Keyboard::Scan::F24});
    input.Bind(2, Input::Mouse{sf::Mouse::Button::Extra2});
    input.BeginFrame();
    input.CaptureEvent(KeyDown(sf::Keyboard::Scan::F24), false);
    input.CaptureEvent(KeyUp(sf::Keyboard::Scan::F24), false);
    input.CaptureEvent(sf::Event::MouseButtonPressed{sf::Mouse::Button::Extra2, {}}, false);
    Require(!input.Pressed(1) && !input.Pressed(2), "Un evento bloqueado por modal o edición llegó al juego");
    input.CaptureEvent(KeyDown(sf::Keyboard::Scan::F24), true);
    Require(input.Pressed(1), "La ruta habilitada no capturó el pulso");
    input.CaptureEvent(sf::Event::FocusLost{}, false);
    Require(!input.Pressed(1), "FocusLost no limpió eventos con la ruta de juego bloqueada");
    input.CaptureEvent(sf::Event::FocusGained{});
    Require(!input.Pressed(1), "Recuperar foco reintrodujo una pulsación antigua");
  }

  void GlobalShortcutsRemainSeparate() {
    InputManager input;
    input.Bind(1, Input::Keyboard{sf::Keyboard::Scan::F11});
    input.BeginFrame();
    auto event = KeyDown(sf::Keyboard::Scan::F11);
    Require(input.HandleGlobalEvent(event, false) == GlobalShortcut::ToggleFullscreen, "F11 dejó de solicitar pantalla completa");
    Require(input.HandleGlobalEvent(event, false) == GlobalShortcut::Consume, "F11 sostenida generó otra solicitud");
    Require(!input.Pressed(1), "El manejo global insertó un pulso de juego");
    Require(input.HandleGlobalEvent(KeyUp(sf::Keyboard::Scan::F11), false) == GlobalShortcut::Consume, "F11 no liberó el bloqueo global");
    Require(input.HandleGlobalEvent(event, true) == GlobalShortcut::Consume, "F11 alteró una confirmación pendiente");
    Require(input.HandleGlobalEvent(KeyDown(sf::Keyboard::Scan::Escape), true) == GlobalShortcut::RevertDisplay,
            "Escape dejó de revertir la confirmación");
    Require(input.HandleGlobalEvent(KeyDown(sf::Keyboard::Scan::R), true) == GlobalShortcut::Consume,
            "R atravesó la confirmación de pantalla");
    Require(input.HandleGlobalEvent(KeyDown(sf::Keyboard::Scan::M), true) == GlobalShortcut::Consume,
            "M atravesó la confirmación de pantalla");
    input.HandleGlobalEvent(sf::Event::FocusLost{}, false);
    Require(input.HandleGlobalEvent(KeyDown(sf::Keyboard::Scan::Enter, true), false) == GlobalShortcut::ToggleFullscreen,
            "Alt+Enter dejó de solicitar pantalla completa");
    Require(input.HandleGlobalEvent(KeyUp(sf::Keyboard::Scan::Enter), false) == GlobalShortcut::Consume,
            "Alt+Enter no liberó su bloqueo global");
    const auto start = Input::LogicalToHardware(GamepadButton::Start, 7);
    Require(start.has_value() && input.HandleGlobalEvent(sf::Event::JoystickButtonPressed{7, static_cast<unsigned int>(*start)}, true) == GlobalShortcut::Consume,
            "Start del mando atravesó la confirmación");
  }

  void WheelRoutingAndLifetime() {
    InputManager input;
    auto wheel = [](float delta, sf::Mouse::Wheel axis = sf::Mouse::Wheel::Vertical) {
      return sf::Event(sf::Event::MouseWheelScrolled{axis, delta, {40, 60}});
    };
    input.CaptureEvent(wheel(1.f));
    input.CaptureEvent(wheel(0.5f));
    input.CaptureEvent(wheel(-0.25f));
    Require(input.WheelDelta() == 1.25f, "La rueda perdio pasos o direccion entre eventos");
    Require(input.WheelDelta() == 1.25f, "Consultar la rueda consumio su valor");
    input.CaptureEvent(wheel(5.f, sf::Mouse::Wheel::Horizontal));
    input.CaptureEvent(wheel(std::numeric_limits<float>::quiet_NaN()));
    input.CaptureEvent(wheel(std::numeric_limits<float>::infinity()));
    Require(input.WheelDelta() == 1.25f, "La rueda horizontal o invalida altero el zoom");
    input.BeginFrame();
    Require(input.WheelDelta() == 0.f, "La rueda persistio mas de un frame");
    input.CaptureEvent(wheel(2.f), false);
    Require(input.WheelDelta() == 0.f, "La rueda atraveso una ruta bloqueada");
    input.CaptureEvent(wheel(-2.f));
    input.CaptureEvent(sf::Event::FocusLost{}, false);
    Require(input.WheelDelta() == 0.f, "La perdida de foco conservo zoom pendiente");
    input.CaptureEvent(wheel(3.f));
    input.Clear();
    Require(input.WheelDelta() == 0.f, "Cambiar de escena conservo la rueda anterior");
  }
}

int main() {
  try {
    BriefKeyboardPulse();
    BriefMousePulseAndSharedAction();
    PhysicalGamepadIdentity();
    BlockedRoutingAndFocus();
    GlobalShortcutsRemainSeparate();
    WheelRoutingAndLifetime();
    std::cout << "PASS entrada: " << checks << " comprobaciones de pulsos, foco, rutas y atajos\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL entrada: " << error.what() << '\n';
    return 1;
  }
}
