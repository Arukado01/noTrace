#pragma once

#include <cstddef>
namespace NoTrace::DistractionConfig {
  inline constexpr float MaximumDecoyDistance = 340.f;
  inline constexpr float MinimumDecoyDistance = 24.f;
  inline constexpr float ProjectileRadius = 3.f;
  inline constexpr float LandingClearance = 0.5f;
  inline constexpr float DecoyHearingDistance = 420.f;
  inline constexpr float DecoyArcHeight = 64.f;
  inline constexpr float DecoyFlightSpeed = 340.f;
  inline constexpr float DecoyMinimumFlightTime = 0.25f;
  inline constexpr std::size_t MaximumFlyingDecoys = 32;
  inline constexpr float KnockDistance = 28.f;
  inline constexpr float KnockFacingCosine = 0.65f;
  inline constexpr float KnockHearingDistance = 190.f;
  inline constexpr float PlayerHearingDistance = 200.f;
  inline constexpr float FootstepInterval = 0.45f;
  inline constexpr float FootstepLifetime = 0.8f;
  inline constexpr float FootstepQuantization = 32.f;
  inline constexpr float MinimumFootstepMovement = 0.05f;
  inline constexpr std::size_t MaximumFootstepCues = 128;
}
