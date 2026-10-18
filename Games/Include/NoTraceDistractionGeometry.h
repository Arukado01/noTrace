#pragma once

#include "Config/NoTraceDistractionConfig.h"
#include "NoTraceSpatialGeometry.h"
#include <algorithm>
#include <array>
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

  inline float DecoyCollisionFraction(const WorldState &world, sf::Vector3f from, sf::Vector3f to) {
    using namespace DistractionConfig;
    float fraction = 1.f;
    const auto clip = [&](const sf::FloatRect &bounds, float bottom, float top) {
      float hit = 1.f;
      if (WorldGeometry::SegmentPrism(from, to, WorldGeometry::Expanded(bounds, ProjectileRadius), bottom - ProjectileRadius, top + ProjectileRadius, &hit)) fraction = std::min(fraction, hit);
    };
    for (const auto &obstacle : world.obstacles) if (WorldGeometry::BlocksMovement(obstacle)) clip(obstacle.bounds, obstacle.elevation, obstacle.elevation + WorldGeometry::ObstacleHeight(obstacle));
    for (const auto &object : world.objects) if (WorldGeometry::BlocksMovement(object)) clip(WorldGeometry::ObjectFootprint(object), object.elevation, object.elevation + WorldGeometry::ObjectHeight(object));
    for (const auto &surface : world.walkSurfaces) if (surface.enabled) {
      auto localFrom = from, localTo = to;
      localFrom.z -= WorldGeometry::SurfaceHeight(surface, WorldGeometry::Ground(from)); localTo.z -= WorldGeometry::SurfaceHeight(surface, WorldGeometry::Ground(to));
      float hit = 1.f;
      if (WorldGeometry::SegmentPrism(localFrom, localTo, WorldGeometry::Expanded(surface.bounds, ProjectileRadius), -surface.thickness - ProjectileRadius, ProjectileRadius, &hit)) fraction = std::min(fraction, hit);
    }
    return fraction;
  }

  inline bool DecoyLandingValid(const WorldState &world, sf::Vector2f point, float elevation) {
    using namespace DistractionConfig;
    const auto support = WorldGeometry::FindSupport(world, point, elevation, 0, ProjectileRadius);
    if (!support || std::abs(support->elevation - elevation) > LevelConfig::SupportTolerance || WorldGeometry::PositionBlocked(world, point, ProjectileRadius, elevation, ProjectileRadius * 2.f, support->id)) return false;
    for (const auto offset : std::array<sf::Vector2f, 4>{{{ProjectileRadius, 0.f}, {-ProjectileRadius, 0.f}, {0.f, ProjectileRadius}, {0.f, -ProjectileRadius}}}) {
      const auto edge = WorldGeometry::FindSupport(world, point + offset, elevation, support->id, ProjectileRadius);
      if (!edge || std::abs(edge->elevation - elevation) > LevelConfig::SupportTolerance) return false;
    }
    return true;
  }

  inline float DecoyArcCollision(const WorldState &world, sf::Vector2f origin, sf::Vector2f target, float elevation) {
    using namespace DistractionConfig;
    const auto movement = target - origin;
    const int count = std::max(1, static_cast<int>(std::ceil(movement.length() / SpatialConfig::MaximumMovementStep)));
    sf::Vector3f previous{origin.x, origin.y, elevation + ProjectileRadius + LandingClearance};
    for (int step = 1; step <= count; ++step) {
      const float t = static_cast<float>(step) / count;
      const auto point = origin + movement * t;
      const sf::Vector3f next{point.x, point.y, elevation + ProjectileRadius + LandingClearance + 4.f * DecoyArcHeight * t * (1.f - t)};
      const float hit = DecoyCollisionFraction(world, previous, next);
      if (hit < 1.f) return (static_cast<float>(step - 1) + hit) / count;
      previous = next;
    }
    return 1.f;
  }

  inline DecoyTrajectory TraceDecoy(const WorldState &world, sf::Vector2f origin, sf::Vector2f direction,
      float requestedDistance = DistractionConfig::MaximumDecoyDistance, float elevation = 0.f) {
    using namespace DistractionConfig;
    DecoyTrajectory result; result.from = result.to = origin;
    if (!std::isfinite(elevation) || elevation < LevelConfig::MinimumElevation || elevation > LevelConfig::MaximumElevation) return result;
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
    bool clear = false;
    for (unsigned int correction = 0; correction < MaximumTrajectoryCorrections && distance >= MinimumDecoyDistance; ++correction) {
      const float fraction = DecoyArcCollision(world, origin, origin + direction * distance, elevation);
      if (fraction < 1.f) {
        distance = std::max(0.f, distance * fraction - LandingClearance); result.blocked = true;
        continue;
      }
      const float before = distance;
      while (distance >= MinimumDecoyDistance && !DecoyLandingValid(world, origin + direction * distance, elevation)) distance = std::max(0.f, distance - LandingClearance);
      if (distance < before) { result.blocked = true; continue; }
      clear = true; break;
    }
    if (!clear) { distance = 0.f; result.blocked = true; }
    result.travelled = distance;
    result.to = origin + direction * distance;
    result.valid = clear && distance >= MinimumDecoyDistance;
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
