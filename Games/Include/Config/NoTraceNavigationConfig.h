#pragma once

#include "Config/NoTraceGameplayConfig.h"

namespace NoTrace::NavigationConfig {
  inline constexpr float CellSize = GameplayConfig::NavigationCellSize;
  inline constexpr float RefreshInterval = GameplayConfig::NavigationRefresh;
  inline constexpr float WaypointTolerance = 0.15f;
  inline constexpr float StuckInterval = 0.7f;
  inline constexpr float MinimumStepProgress = 0.05f;
  inline constexpr unsigned int RecalculateFailure = 1;
  inline constexpr unsigned int NearbyFailure = 2;
  inline constexpr unsigned int RejectFailure = 3;
  inline constexpr float NearbySearchRadius = CellSize * 2.f;
  inline constexpr float RejectDuration = 5.f;
  inline constexpr int MaximumCells = 20000;
}
