#pragma once

namespace NoTrace::CqcConfig {
  inline constexpr float AcquireHold = 0.22f;
  inline constexpr float ReleaseGrace = 0.9f;
  inline constexpr float MinimumRelease = 0.06f;
  inline constexpr float PulseInterval = 0.3f;
  inline constexpr float FacingCosine = 0.65f;
  inline constexpr unsigned int RequiredPulses = 3;
  inline constexpr float PushDistance = 36.f;
  inline constexpr float PushCooldown = 0.8f;
  inline constexpr float PushStagger = 0.35f;
  inline constexpr float PushNoise = 70.f;
}
