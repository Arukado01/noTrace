#pragma once

namespace NoTrace::CqcConfig {
  inline constexpr float AcquireHold = 0.22f;
  inline constexpr float ReleaseGrace = 0.9f;
  inline constexpr float MinimumRelease = 0.06f;
  inline constexpr float PulseInterval = 0.3f;
  inline constexpr float FacingCosine = 0.65f;
  inline constexpr float RearCosine = 0.7f;
  inline constexpr float MinimumGripDistance = 20.f;
  inline constexpr float MaximumPressBuffer = 1.f;
  inline constexpr float GripSeparation = 28.f;
  inline constexpr float ApproachSpeed = 180.f;
  inline constexpr float ApproachTimeout = 1.2f;
  inline constexpr float ApproachBlockedTolerance = 0.05f;
  inline constexpr float PunchReach = 38.f;
  inline constexpr float KickReach = 44.f;
  inline constexpr float PunchWindup = 0.12f;
  inline constexpr float KickWindup = 0.18f;
  inline constexpr float PunchRecovery = 0.26f;
  inline constexpr float KickRecovery = 0.34f;
  inline constexpr float PunchCooldown = 0.52f;
  inline constexpr float KickCooldown = 0.68f;
  inline constexpr float PunchStun = 35.f;
  inline constexpr float KickStun = 48.f;
  inline constexpr float PunchStagger = 0.32f;
  inline constexpr float KickStagger = 0.48f;
  inline constexpr float PunchStamina = 8.f;
  inline constexpr float KickStamina = 11.f;
  inline constexpr float PunchNoise = 75.f;
  inline constexpr float KickNoise = 95.f;
  inline constexpr float GripSoundRadius = 70.f;
  inline constexpr unsigned int RequiredPulses = 3;
  inline constexpr float PushDistance = 36.f;
  inline constexpr float PushCooldown = 0.8f;
  inline constexpr float PushStagger = 0.35f;
  inline constexpr float PushNoise = 70.f;
}
