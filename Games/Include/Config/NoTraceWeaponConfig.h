#pragma once

#include "Config/NoTraceGameplayConfig.h"

namespace NoTrace::WeaponConfig {
  inline constexpr int PistolCapacity = 4;
  inline constexpr int NonLethalCapacity = 4;
  inline constexpr int TrainingAmmo = GameplayConfig::MaximumAmmo;
  inline constexpr float ReloadDuration = 1.2f;
  inline constexpr float TrainingMinimumHealth = 1.f;
  inline constexpr float TrainingReaction = 0.2f;
}
