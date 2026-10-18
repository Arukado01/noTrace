#pragma once

#include "Config/NoTraceGameplayConfig.h"

namespace NoTrace::WeaponConfig {
  inline constexpr int PistolCapacity = 4;
  inline constexpr int NonLethalCapacity = 4;
  inline constexpr int TrainingAmmo = GameplayConfig::MaximumAmmo;
  inline constexpr float ReloadDuration = 1.2f;
  inline constexpr float EquipmentGestureDuration = .65f;
  inline constexpr float TrainingMinimumHealth = 1.f;
  inline constexpr float TrainingReaction = 0.2f;
  inline constexpr float MaximumSuppressor = 100.f;
  inline constexpr float PistolSuppressorWear = 20.f;
  inline constexpr float NonLethalSuppressorWear = 12.5f;
  inline constexpr float PistolNoise = 360.f;
  inline constexpr float NonLethalNoise = 260.f;
  inline constexpr float PistolSuppressedNoise = 85.f;
  inline constexpr float NonLethalSuppressedNoise = 65.f;
}
