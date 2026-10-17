#pragma once

#include <cstddef>

namespace NoTrace::VisibilityConfig {
  inline constexpr float ReferenceStandingRange = 400.f;
  inline constexpr float ReferenceCrouchedRange = 330.f;
  inline constexpr float ReferenceProneRange = 260.f;
  inline constexpr float PlayerRangeScale = 0.5f;
  inline constexpr float StandingRange = ReferenceStandingRange * PlayerRangeScale;
  inline constexpr float CrouchedRange = ReferenceCrouchedRange * PlayerRangeScale;
  inline constexpr float ProneRange = ReferenceProneRange * PlayerRangeScale;
  inline constexpr float CloseRange = 32.f;
  inline constexpr float CosHalfAngle = -0.34202014f;
  inline constexpr float CellSize = 24.f;
  inline constexpr float SurfaceOffset = 0.5f;
  inline constexpr std::size_t MaximumCells = 20000;
  inline constexpr std::size_t VisualRayCount = 360;
  inline constexpr unsigned int VisualRayIterations = 12;
}
