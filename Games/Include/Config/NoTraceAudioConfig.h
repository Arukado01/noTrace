#pragma once

#include "Types/NoTraceGameplayTypes.h"
#include <array>
#include <string_view>

namespace NoTrace::AudioConfig {
  inline constexpr float CueLifetime = 1.f;
  inline constexpr std::size_t MaximumCues = 128;
  inline constexpr std::size_t MaximumVoices = 24;
  inline constexpr float WallHearingFactor = 0.55f;
  inline constexpr float PositionQuantization = 32.f;
  inline constexpr float ImpactRadius = 100.f;
  inline constexpr float FallRadius = 120.f;
  inline constexpr float AlarmRadius = 900.f;
  inline constexpr float QuietRadius = 70.f;
  inline constexpr float SoftFloorBoundary = 740.f;
  inline constexpr std::array<std::string_view, 26> Files{
    "Pistol", "Rifle", "Suppressed", "Reload", "Impact", "Punch", "Kick", "Grab", "Choke", "Fall",
    "StepHard", "StepSoft", "Knock", "TrapTick", "Explosion", "Lock", "PuzzleStep", "PuzzleFailed",
    "PuzzleSolved", "Camera", "Alarm", "Reinforcement", "Cabinet", "Hideout", "Drag", "Decoy"};
  inline std::string File(SoundKind kind) { return "NoTrace/" + std::string(Files.at(static_cast<std::size_t>(kind))) + ".wav"; }
}
