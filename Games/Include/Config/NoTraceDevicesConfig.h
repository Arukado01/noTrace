#pragma once

#include <cstdint>

namespace NoTrace::DevicesConfig {
  inline constexpr float CameraRange = 240.f;
  inline constexpr float CameraCosHalfAngle = 0.8f;
  inline constexpr float CameraConfirmationTime = 0.85f;
  inline constexpr float CameraSweepMinimum = -0.75f;
  inline constexpr float CameraSweepMaximum = 0.75f;
  inline constexpr float CameraSweepSpeed = 0.4f;
  inline constexpr float CameraEndpointPause = 0.7f;
  inline constexpr float CameraPanelDisabledDuration = 45.f;
  inline constexpr float TrapTriggerRadius = 58.f;
  inline constexpr float TrapAudibleRadius = 180.f;
  inline constexpr float TrapBlastRadius = 120.f;
  inline constexpr float TrapFuseDuration = 5.f;
  inline constexpr float TrapDamage = 110.f;
  inline constexpr float TrapMinimumTickInterval = 0.14f;
  inline constexpr float TrapMaximumTickInterval = 0.8f;
  inline constexpr float ExplosionNoiseRadius = 480.f;
  inline constexpr float ExplosionEffectDuration = 0.7f;
  inline constexpr float PuzzleFailureCooldown = 3.f;
  inline constexpr unsigned int PuzzleMinimumStages = 2;
  inline constexpr unsigned int PuzzleMaximumStages = 6;
  inline constexpr std::uint32_t PuzzleChoiceCount = 4;
}
