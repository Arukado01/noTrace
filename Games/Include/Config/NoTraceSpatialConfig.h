#pragma once

#include <SFML/System/Vector2.hpp>
#include <array>

namespace NoTrace::SpatialConfig {
  inline constexpr float CollisionSkin = 0.01f;
  inline constexpr float MaximumMovementStep = 4.f;
  inline constexpr float CoverAttachDistance = 24.f;
  inline constexpr float CoverSlideSpeed = 60.f;
  inline constexpr float CoverPeekSpeed = 135.f;
  inline constexpr float CoverPeekEdgeReach = 55.f;
  inline constexpr float CoverPeekClearance = 6.f;
  inline constexpr float CoverPositionTolerance = 0.15f;
  inline constexpr std::array<sf::Vector2f, 12> ObjectGroundSizes{{
      {78.f, 48.f}, {70.f, 26.f}, {60.f, 38.f}, {100.f, 100.f},
      {24.f, 64.f}, {38.f, 28.f}, {38.f, 28.f}, {8.f, 8.f},
      {48.f, 48.f}, {44.f, 34.f}, {100.f, 100.f}, {28.f, 24.f}}};
  inline constexpr std::array<float, 12> ObjectHeights{{26.f, 58.f, 42.f, 0.f, 66.f, 30.f, 30.f, 50.f, 40.f, 26.f, 0.f, 42.f}};
  inline constexpr float HighCoverHeight = 66.f;
  inline constexpr float LowCoverHeight = 24.f;
  inline constexpr float HideoutHeight = 40.f;
  inline constexpr sf::Vector2f TrashBinSize{48.f, 48.f};
  inline constexpr sf::Vector2f ClosetSize{48.f, 28.f};
  inline constexpr sf::Vector2f UnderTableSize{78.f, 48.f};
  inline constexpr float ClosetHeight = 64.f;
  inline constexpr float UnderTableHeight = 28.f;
}
