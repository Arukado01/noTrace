#pragma once

#include <cstddef>

namespace NoTrace::LevelConfig {
  inline constexpr float Width = 1600.f;
  inline constexpr float Height = 1200.f;
  inline constexpr float StoreyHeight = 96.f;
  inline constexpr float MinimumElevation = -64.f;
  inline constexpr float MaximumElevation = 512.f;
  inline constexpr float SupportTolerance = 0.2f;
  inline constexpr float MaximumStepHeight = 8.f;
  inline constexpr float SlabThickness = 6.f;
  inline constexpr float PillarWidth = 22.f;
  inline constexpr float PillarHeight = StoreyHeight - SlabThickness;
  inline constexpr float ElevatorSpeed = 48.f;
  inline constexpr float ElevatorDoorTime = 0.45f;
  inline constexpr float ElevatorDockTolerance = 0.1f;
  inline constexpr float ElevatorInteractionReach = 48.f;
  inline constexpr unsigned int MaximumLayers = 3;
  inline constexpr std::size_t MaximumSurfaces = 256;
  inline constexpr std::size_t MaximumGrips = 128;
  inline constexpr std::size_t MaximumLinks = 256;
  inline constexpr std::size_t MaximumElevators = 8;
}
