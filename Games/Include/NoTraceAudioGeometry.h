#pragma once

#include "Config/NoTraceAudioConfig.h"
#include "NoTraceSpatialGeometry.h"
#include "NoTraceCombatGeometry.h"

namespace NoTrace::AudioGeometry {
  inline bool Occluded(const WorldState &world, const Agent &listener, sf::Vector2f source, float elevation) {
    const auto stance = listener.incapacitated || !listener.alive ? Stance::Prone : listener.stance;
    const float ears = listener.elevation + CombatGeometry::HeadCenterHeight(stance);
    return WorldGeometry::RayOccluded(world, {listener.position.x, listener.position.y, ears},
      {source.x, source.y, elevation + AudioConfig::EmitterClearance});
  }
}
