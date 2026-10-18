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
  inline constexpr std::array<std::string_view, 32> Files{
    "Pistol", "Rifle", "Suppressed", "Reload", "Impact", "Punch", "Kick", "Grab", "Choke", "Fall",
    "StepHard", "StepSoft", "Knock", "TrapTick", "Explosion", "Lock", "PuzzleStep", "PuzzleFailed",
    "PuzzleSolved", "Camera", "Alarm", "Reinforcement", "Cabinet", "Hideout", "Drag", "Decoy",
    "SuppressorAttach", "SuppressorDetach", "StepHard", "StepSoft", "StepHard", "StepSoft"};
  inline std::string File(SoundKind kind) { return "NoTrace/" + std::string(Files.at(static_cast<std::size_t>(kind))) + ".wav"; }
  inline float Gain(SoundKind kind) {
    switch (kind) {
    case SoundKind::StepRunHard: case SoundKind::StepRunSoft: return 1.2f;
    case SoundKind::StepHard: case SoundKind::StepSoft: return 0.7f;
    case SoundKind::StepCrouchHard: case SoundKind::StepCrouchSoft: return 0.22f;
    case SoundKind::SuppressorAttach: case SoundKind::SuppressorDetach: return 0.8f;
    default: return 1.f;
    }
  }
  inline float Pitch(SoundKind kind) {
    switch (kind) {
    case SoundKind::StepRunHard: case SoundKind::StepRunSoft: return 1.25f;
    case SoundKind::StepCrouchHard: case SoundKind::StepCrouchSoft: return 0.85f;
    default: return 1.f;
    }
  }
}
