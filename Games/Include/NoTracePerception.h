#pragma once

#include "Types/NoTraceGameplayTypes.h"
#include "Config/NoTraceSecurityConfig.h"
#include <algorithm>

namespace NoTrace::Perception {
  inline float DetectionRate(const Agent &agent, float distance, float range) {
    const float distanceFactor = SecurityConfig::DetectionNearFactor - SecurityConfig::DetectionDistanceReduction * std::clamp(distance / std::max(1.f, range), 0.f, 1.f);
    const float postureFactor = agent.stance == Stance::Prone ? SecurityConfig::ProneDetectionFactor : agent.stance == Stance::Crouched ? SecurityConfig::CrouchedDetectionFactor : 1.f;
    const float movementFactor = agent.running ? SecurityConfig::RunningDetectionFactor : agent.moving ? 1.f : SecurityConfig::StationaryDetectionFactor;
    return distanceFactor * postureFactor * movementFactor;
  }
}
