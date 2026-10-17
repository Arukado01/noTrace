#pragma once

#include "Types/NoTraceManeuverTypes.h"
#include <SFML/Window/Keyboard.hpp>

namespace NoTrace::ManeuverConfig {
  inline constexpr sf::Keyboard::Scancode DefaultDiveKey = sf::Keyboard::Scan::Space;
  inline constexpr sf::Keyboard::Scancode DefaultRollKey = sf::Keyboard::Scan::LControl;
  inline constexpr float DiveStart = 0.10f;
  inline constexpr float DiveTravel = 0.35f;
  inline constexpr float DiveLand = 0.12f;
  inline constexpr float DiveRecover = 0.25f;
  inline constexpr float DiveSpeed = 360.f;
  inline constexpr float DiveCooldown = 0.7f;
  inline constexpr float DiveStamina = 18.f;
  inline constexpr float DiveNoise = 160.f;
  inline constexpr float RollStart = 0.08f;
  inline constexpr float RollTravel = 0.30f;
  inline constexpr float RollRecover = 0.18f;
  inline constexpr float RollSpeed = 250.f;
  inline constexpr float RollCooldown = 0.55f;
  inline constexpr float RollStamina = 12.f;
  inline constexpr float RollNoise = 85.f;
  inline constexpr float LandingNoise = 110.f;
  inline constexpr float CollisionTolerance = 0.05f;
  inline constexpr float StartProbe = 2.f;
  inline constexpr float MaximumTravel = DiveSpeed * DiveTravel;
  inline constexpr float Duration(ManeuverKind kind) { return kind == ManeuverKind::Dive ? DiveStart + DiveTravel + DiveLand + DiveRecover : kind == ManeuverKind::Roll ? RollStart + RollTravel + RollRecover : 0.f; }
}
