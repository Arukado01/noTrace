#pragma once

#include "NoTraceSpatialGeometry.h"

namespace NoTrace::ForcedFalls {
  inline WorldGeometry::SupportPoint Landing(const WorldState &world, sf::Vector2f position, float elevation, float tolerance = LevelConfig::SupportTolerance) {
    WorldGeometry::SupportPoint result{0, LevelConfig::MinimumElevation};
    for (const auto &surface : world.walkSurfaces) {
      if (!WorldGeometry::ContainsSupport(surface, position)) continue;
      const float height = WorldGeometry::SurfaceHeight(surface, position);
      if (height <= elevation + tolerance && (result.id == 0 || height > result.elevation)) result = {surface.id, height};
    }
    return result;
  }
  inline sf::Vector3f Position(const ForcedFallState &state) { return state.from + (state.to - state.from) * state.progress; }
}
