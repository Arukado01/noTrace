#pragma once

#include <numbers>

namespace NoTrace::FirstPersonConfig {
  inline constexpr char FovSaveKey[] = "NoTrace:FirstPersonFov";
  inline constexpr float DefaultFov = 85.f;
  inline constexpr float MinimumFov = 60.f;
  inline constexpr float MaximumFov = 110.f;
  inline constexpr float MouseSensitivity = .0035f;
  inline constexpr float MaximumPitch = std::numbers::pi_v<float> / 3.f;
  inline constexpr float RenderDistance = 768.f;
  inline constexpr float NearPlane = .08f;
  inline constexpr unsigned int RenderWidth = 320;
  inline constexpr unsigned int MinimumRenderHeight = 120;
  inline constexpr unsigned int MaximumRenderHeight = 256;
  inline constexpr float MaterialTileSize = 48.f;
  inline constexpr float RecoilDuration = .13f;
  inline constexpr float DryFireDuration = .22f;
  inline constexpr float DamageDuration = .28f;
  inline constexpr float ImpactSize = 2.2f;
}
