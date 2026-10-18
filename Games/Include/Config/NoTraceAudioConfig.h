#pragma once

#include "Types/NoTraceGameplayTypes.h"
#include "Config/NoTraceLevelConfig.h"
#include "Config/NoTraceCombatConfig.h"
#include <array>
#include <string>
#include <string_view>

namespace NoTrace::AudioConfig {
  inline constexpr float CueLifetime = 1.f;
  inline constexpr float MinimumElevation = LevelConfig::MinimumElevation;
  inline constexpr float MaximumElevation = GameplayConfig::MaximumShotEndpointHeight;
  inline constexpr float EmitterClearance = 2.f;
  inline constexpr std::size_t MaximumCues = 128;
  inline constexpr std::size_t MaximumVoices = 24;
  inline constexpr float WallHearingFactor = 0.55f;
  inline constexpr float PositionQuantization = 32.f;
  inline constexpr float ImpactRadius = 100.f;
  inline constexpr float FallRadius = 120.f;
  inline constexpr float AlarmRadius = 900.f;
  inline constexpr float QuietRadius = 70.f;
  inline constexpr float SoftFloorBoundary = 740.f;
  inline constexpr float ProneFoleyInterval = .55f;
  inline constexpr float GripFoleyInterval = .65f;
  inline constexpr bool IsSecurityCue(SoundKind kind) {
    return kind == SoundKind::DetectionConfirmed || kind == SoundKind::SearchStart || kind == SoundKind::VigilanceStart || kind == SoundKind::AlertEnd;
  }
  struct ClipSet {
    std::array<std::string_view, 6> variants;
    std::size_t count;
    float gain;
    unsigned int priority;
    std::size_t maximumInstances;
    bool loop;
  };
  struct StepKinds { SoundKind walk; SoundKind run; SoundKind crouch; };
  inline constexpr StepKinds ConcreteSteps{SoundKind::StepHard, SoundKind::StepRunHard, SoundKind::StepCrouchHard};
  inline constexpr StepKinds MetalSteps{SoundKind::StepMetal, SoundKind::StepRunMetal, SoundKind::StepCrouchMetal};
  inline constexpr StepKinds SoilSteps{SoundKind::StepSoft, SoundKind::StepRunSoft, SoundKind::StepCrouchSoft};
  inline constexpr StepKinds StairSteps{SoundKind::StepStair, SoundKind::StepRunStair, SoundKind::StepCrouchStair};
  inline constexpr bool IsFootstep(SoundKind kind) {
    return kind == ConcreteSteps.walk || kind == ConcreteSteps.run || kind == ConcreteSteps.crouch ||
        kind == MetalSteps.walk || kind == MetalSteps.run || kind == MetalSteps.crouch ||
        kind == SoilSteps.walk || kind == SoilSteps.run || kind == SoilSteps.crouch ||
        kind == StairSteps.walk || kind == StairSteps.run || kind == StairSteps.crouch;
  }
  inline constexpr SoundKind Footstep(StepKinds material, Stance stance, bool running) {
    return stance != Stance::Standing ? material.crouch : running ? material.run : material.walk;
  }
}

#include "Config/NoTraceAudioCatalog.h"

namespace NoTrace::AudioConfig {
  inline const ClipSet &Profile(SoundKind kind) { return Profiles.at(static_cast<std::size_t>(kind)); }
  inline std::string VariantFile(SoundKind kind, std::size_t variant) {
    const auto &profile = Profile(kind);
    if (profile.count == 0) return {};
    return "NoTrace/" + std::string(profile.variants.at(variant % profile.count));
  }
  inline std::string File(SoundKind kind) { return VariantFile(kind, 0); }
  inline float Gain(SoundKind kind) { return Profile(kind).gain; }
  inline constexpr float Pitch(SoundKind) { return 1.f; }
}
