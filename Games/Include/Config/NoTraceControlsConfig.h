#pragma once

#include <SFML/Window/Keyboard.hpp>
#include <array>
#include <cstddef>
#include <string_view>

namespace NoTrace::ControlsConfig {
  enum Action : int { Left, Right, Up, Down, Run, Crouch, Prone, Interact, Cqc, Lethal, NonLethal, Decoy, Knock, Drag, Tool,
    Orders, Lobby, WallCover, Peek, Shove, Reload, SelectPistol, SelectNonLethal, Dive, Roll, Suppressor, WeaponMenu, Count };
  inline constexpr std::array<sf::Keyboard::Scancode, static_cast<std::size_t>(Action::Count)> DefaultKeys{
    sf::Keyboard::Scan::A, sf::Keyboard::Scan::D, sf::Keyboard::Scan::W, sf::Keyboard::Scan::S,
    sf::Keyboard::Scan::LShift, sf::Keyboard::Scan::C, sf::Keyboard::Scan::Z,
    sf::Keyboard::Scan::E, sf::Keyboard::Scan::F, sf::Keyboard::Scan::Unknown, sf::Keyboard::Scan::Unknown,
    sf::Keyboard::Scan::G, sf::Keyboard::Scan::Unknown, sf::Keyboard::Scan::Unknown, sf::Keyboard::Scan::T,
    sf::Keyboard::Scan::Tab, sf::Keyboard::Scan::F2, sf::Keyboard::Scan::LAlt, sf::Keyboard::Scan::Unknown, sf::Keyboard::Scan::V,
    sf::Keyboard::Scan::R, sf::Keyboard::Scan::Num1, sf::Keyboard::Scan::Num2,
    sf::Keyboard::Scan::Unknown, sf::Keyboard::Scan::Unknown, sf::Keyboard::Scan::H, sf::Keyboard::Scan::Q
  };
  inline constexpr std::array<sf::Keyboard::Scancode, 5> MovementKeys{
    sf::Keyboard::Scan::Space, sf::Keyboard::Scan::LControl, sf::Keyboard::Scan::X, sf::Keyboard::Scan::J, sf::Keyboard::Scan::K
  };
  inline constexpr std::array<std::string_view, MovementKeys.size()> MovementKeyNames{"Espacio", "Ctrl izq.", "X", "J", "K"};
  inline constexpr std::string_view DiveSaveKey = "NoTrace:DiveKey";
  inline constexpr std::string_view RollSaveKey = "NoTrace:RollKey";
  inline constexpr std::array<std::string_view, 6> Categories{
    "Movimiento y posturas", "Apuntado y armas", "Interacción y combate",
    "Cobertura y escondites", "Acciones tácticas", "Interfaz y navegación"
  };
}
