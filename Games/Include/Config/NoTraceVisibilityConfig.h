#pragma once

#include <cstddef>

namespace NoTrace::VisibilityConfig {
  inline constexpr float StandingRange = 400.f;
  inline constexpr float CrouchedRange = 330.f;
  inline constexpr float ProneRange = 260.f;
  inline constexpr float CloseRange = 32.f;
  inline constexpr float CosHalfAngle = -0.34202014f;
  inline constexpr float CellSize = 24.f;
  inline constexpr float SurfaceOffset = 0.5f;
  inline constexpr std::size_t MaximumCells = 20000;
}
