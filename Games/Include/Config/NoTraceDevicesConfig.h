#pragma once

#include <cstdint>

namespace NoTrace::DevicesConfig {
  inline constexpr float CameraRange = 240.f;
  inline constexpr float CameraCosHalfAngle = 0.8f;
  inline constexpr float CameraConfirmationTime = 1.5f;
  inline constexpr float CameraLostSightGrace = 0.65f;
  inline constexpr float CameraSuspicionDecay = 0.45f;
  inline constexpr float CameraTrackingSpeed = 3.5f;
  inline constexpr float CameraSearchDuration = 3.f;
  inline constexpr float CameraSuspicionIndicatorDuration = 1.5f;
  inline constexpr float CameraConfirmedIndicatorDuration = 5.f;
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
  inline constexpr unsigned int WireCount = 4;
  inline constexpr float CoordinationHoldDuration = 2.f;
  inline constexpr float PuzzleNodeSpacing = 80.f;
  inline constexpr float PuzzleNodeMinimumSeparation = 35.f;
  inline constexpr float PuzzleNodeSize = 16.f;
}
