#pragma once

#include "Config/NoTraceAudioConfig.h"
#include "NoTraceSpatialGeometry.h"
#include "NoTraceCombatGeometry.h"
#include <cmath>
#include <optional>

namespace NoTrace::AudioGeometry {
  inline float Pan(sf::Vector2f delta, float radius, std::optional<sf::Vector2f> cameraRight = std::nullopt) {
    float lateral = delta.x;
    if (cameraRight && std::isfinite(cameraRight->x) && std::isfinite(cameraRight->y) && cameraRight->lengthSquared() > 0.0001f) lateral = delta.dot(cameraRight->normalized());
    return std::clamp(lateral / std::max(1.f, radius), -1.f, 1.f);
  }
  inline bool Occluded(const WorldState &world, const Agent &listener, sf::Vector2f source, float elevation) {
    const auto stance = listener.incapacitated || !listener.alive ? Stance::Prone : listener.stance;
    const float ears = listener.elevation + CombatGeometry::HeadCenterHeight(stance);
    return WorldGeometry::RayOccluded(world, {listener.position.x, listener.position.y, ears},
      {source.x, source.y, elevation + AudioConfig::EmitterClearance});
  }
}
