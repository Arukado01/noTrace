#pragma once

#include <cstddef>

namespace NoTrace::SecurityConfig {
  inline constexpr float InitialArrivalDelay = 3.f;
  inline constexpr float ReinforcementInterval = 8.f;
  inline constexpr std::size_t GuardsPerWave = 2;
  inline constexpr std::size_t MaximumActiveReinforcements = 6;
  inline constexpr unsigned int MaximumWaves = 3;
  inline constexpr float SearchDuration = 9.f;
  inline constexpr float PatrolRecoveryDelay = 14.f;
  inline constexpr float WithdrawalDelay = 12.f;
  inline constexpr float MinimumSpawnAgentDistance = 180.f;
  inline constexpr float EntryRetryDelay = 1.f;
  inline constexpr float ExitArrivalDistance = 12.f;
  inline constexpr float SearchProbeDistance = 48.f;
}
