#pragma once

#include <cstddef>

namespace NoTrace::SecurityConfig {
  inline constexpr float InitialArrivalDelay = 3.f;
  inline constexpr float ReinforcementInterval = 8.f;
  inline constexpr std::size_t GuardsPerWave = 2;
  inline constexpr std::size_t MaximumActiveReinforcements = 6;
  inline constexpr unsigned int MaximumWaves = 3;
  inline constexpr float SearchDuration = 30.f;
  inline constexpr float PatrolRecoveryDelay = SearchDuration;
  inline constexpr float WithdrawalDelay = 12.f;
  inline constexpr float VigilanceDuration = WithdrawalDelay;
  inline constexpr float MinimumSpawnAgentDistance = 180.f;
  inline constexpr float EntryRetryDelay = 1.f;
  inline constexpr float ExitArrivalDistance = 12.f;
  inline constexpr float SearchProbeDistance = 72.f;
  inline constexpr float SearchProbeRingSpacing = 32.f;
  inline constexpr float SearchAreaRadius = 240.f;
  inline constexpr float SearchRouteInterval = 4.f;
  inline constexpr float BodyInspectionDuration = 1.5f;
  inline constexpr float BodyApproachDistance = 28.f;
  inline constexpr float BodyInspectionReach = 36.f;
  inline constexpr float PatrolPauseMinimum = 10.f;
  inline constexpr float PatrolPauseMaximum = 20.f;
  inline constexpr float GuardLostSightGrace = 0.8f;
  inline constexpr float DetectionNearFactor = 1.15f;
  inline constexpr float DetectionDistanceReduction = 0.45f;
  inline constexpr float StationaryDetectionFactor = 0.8f;
  inline constexpr float RunningDetectionFactor = 1.3f;
  inline constexpr float CrouchedDetectionFactor = 0.7f;
  inline constexpr float ProneDetectionFactor = 0.45f;
}
