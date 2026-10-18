#pragma once

namespace NoTrace::TraversalConfig {
  inline constexpr float AttachReach = 30.f;
  inline constexpr float LinkReach = 14.f;
  inline constexpr float HandHeight = 46.f;
  inline constexpr float BodyGap = 0.02f;
  inline constexpr float ShimmySpeed = 38.f;
  inline constexpr float ClimbClearance = 48.f;
  inline constexpr float TransitionSpeed = 80.f;
  inline constexpr float DropSpeed = 120.f;
  inline constexpr float MinimumTransitionDuration = 0.2f;
  inline constexpr float MaximumDrop = 104.f;
  inline constexpr float SafeDrop = 48.f;
  inline constexpr float FallDamagePerUnit = 1.5f;
  inline constexpr float EndpointTolerance = 0.2f;
}
