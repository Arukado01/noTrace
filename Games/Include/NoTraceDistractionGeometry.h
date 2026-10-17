#pragma once

#include "Config/NoTraceDistractionConfig.h"
#include "NoTraceWorldGeometry.h"
#include <algorithm>
#include <cmath>

namespace NoTrace::DistractionGeometry {
  struct DecoyTrajectory {
    sf::Vector2f from;
    sf::Vector2f to;
    float requestedDistance = 0.f;
    float travelled = 0.f;
    bool blocked = false;
    bool valid = false;
  };

  inline DecoyTrajectory TraceDecoy(const WorldState &world, sf::Vector2f origin, sf::Vector2f direction,
      float requestedDistance = DistractionConfig::MaximumDecoyDistance) {
    using namespace DistractionConfig;
    DecoyTrajectory result; result.from = result.to = origin;
    const float length = std::hypot(direction.x, direction.y);
    if (!std::isfinite(length) || length < 0.0001f || !std::isfinite(requestedDistance) ||
        !std::isfinite(origin.x) || !std::isfinite(origin.y) || world.size.x < ProjectileRadius * 2.f || world.size.y < ProjectileRadius * 2.f ||
        origin.x < ProjectileRadius || origin.y < ProjectileRadius || origin.x > world.size.x - ProjectileRadius || origin.y > world.size.y - ProjectileRadius) return result;
    direction /= length;
    result.requestedDistance = std::clamp(requestedDistance, MinimumDecoyDistance, MaximumDecoyDistance);
    float distance = result.requestedDistance;
    const auto limit = [&](float start, float delta, float size) {
      if (delta > 0.f) distance = std::min(distance, (size - ProjectileRadius - start) / delta);
      else if (delta < 0.f) distance = std::min(distance, (ProjectileRadius - start) / delta);
    };
    limit(origin.x, direction.x, world.size.x); limit(origin.y, direction.y, world.size.y);
    distance = std::max(0.f, distance);
    result.blocked = distance < result.requestedDistance;
    const auto end = origin + direction * distance;
    float fraction = 1.f;
    WorldGeometry::ForEachSolid(world, [&](const sf::FloatRect &bounds) {
      float intersection = 1.f;
      if (WorldGeometry::SegmentRect(origin, end, WorldGeometry::Expanded(bounds, ProjectileRadius), &intersection) && intersection < fraction) fraction = intersection;
    });
    if (fraction < 1.f) {
      distance = std::max(0.f, distance * fraction - LandingClearance);
      result.blocked = true;
    }
    result.travelled = distance;
    result.to = origin + direction * distance;
    result.valid = distance >= MinimumDecoyDistance;
    return result;
  }

  inline sf::Vector2f DecoyPosition(const DecoyProjectile &disc) {
    const float t = disc.duration > 0.f ? std::clamp(disc.elapsed / disc.duration, 0.f, 1.f) : 1.f;
    return disc.from + (disc.to - disc.from) * t;
  }

  inline float DecoyHeight(const DecoyProjectile &disc) {
    const float t = disc.duration > 0.f ? std::clamp(disc.elapsed / disc.duration, 0.f, 1.f) : 1.f;
    return 4.f * disc.height * t * (1.f - t);
  }

  inline sf::Vector2f QuantizeFootstep(sf::Vector2f position) {
    const float cell = DistractionConfig::FootstepQuantization;
    return {(std::floor(position.x / cell) + 0.5f) * cell, (std::floor(position.y / cell) + 0.5f) * cell};
  }

  inline std::uint8_t FootstepDirection(sf::Vector2f displacement) {
    constexpr float sector = 0.7853981633974483f;
    const int index = static_cast<int>(std::lround(std::atan2(displacement.y, displacement.x) / sector));
    return static_cast<std::uint8_t>((index + 8) % 8);
  }
}
