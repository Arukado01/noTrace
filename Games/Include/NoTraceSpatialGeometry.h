#pragma once

#include "NoTraceWorldGeometry.h"
#include "Config/NoTraceLevelConfig.h"
#include "Config/NoTraceTraversalConfig.h"
#include <optional>
#include <limits>

namespace NoTrace::WorldGeometry {
  struct SupportPoint { std::uint32_t id = 0; float elevation = 0.f; };
  inline sf::Vector3f Feet(const Agent &agent) { return {agent.position.x, agent.position.y, agent.elevation}; }
  inline sf::Vector3f Feet(const Guard &guard) { return {guard.position.x, guard.position.y, guard.elevation}; }
  inline sf::Vector2f Ground(sf::Vector3f point) { return {point.x, point.y}; }
  inline float SpatialDistance(sf::Vector3f first, sf::Vector3f second) { return (second - first).length(); }
  inline float ActorHeight(const Agent &agent) {
    return agent.incapacitated || !agent.alive || agent.stance == Stance::Prone ? CombatConfig::ProneHeight :
        agent.stance == Stance::Crouched ? CombatConfig::CrouchedHeight : CombatConfig::StandingHeight;
  }
  inline float SurfaceHeight(const WalkSurface &surface, sf::Vector2f point) {
    return surface.elevation + (point - surface.bounds.position).dot(surface.slope);
  }
  inline const WalkSurface *FindSurface(const WorldState &world, std::uint32_t id) {
    const auto found = std::ranges::find(world.walkSurfaces, id, &WalkSurface::id);
    return found == world.walkSurfaces.end() ? nullptr : &*found;
  }
  inline const GripSurface *FindGrip(const WorldState &world, std::uint32_t id) {
    const auto found = std::ranges::find(world.gripSurfaces, id, &GripSurface::id);
    return found == world.gripSurfaces.end() ? nullptr : &*found;
  }
  inline bool ContainsSupport(const WalkSurface &surface, sf::Vector2f point) {
    const auto end = surface.bounds.position + surface.bounds.size;
    return surface.enabled && point.x >= surface.bounds.position.x && point.y >= surface.bounds.position.y && point.x <= end.x && point.y <= end.y;
  }
  inline bool TouchingSupports(const WalkSurface &first, const WalkSurface &second) {
    const auto a = first.bounds.position + first.bounds.size, b = second.bounds.position + second.bounds.size;
    const float tolerance = LevelConfig::SupportTolerance;
    return first.bounds.position.x <= b.x + tolerance && second.bounds.position.x <= a.x + tolerance &&
        first.bounds.position.y <= b.y + tolerance && second.bounds.position.y <= a.y + tolerance;
  }
  inline bool ElevatorDocked(const WorldState &world, std::uint32_t id, float elevation) {
    const auto found = std::ranges::find(world.elevators, id, &ElevatorState::id);
    return found != world.elevators.end() && found->phase == ElevatorPhase::Docked && std::abs(found->elevation - elevation) <= LevelConfig::ElevatorDockTolerance;
  }
  inline bool SupportsConnected(const WorldState &world, std::uint32_t from, std::uint32_t to, sf::Vector2f point) {
    if (from == 0 || from == to) return true;
    const auto *first = FindSurface(world, from), *second = FindSurface(world, to);
    if (!first || !second) return false;
    if (first->kind != WalkSurfaceKind::ElevatorCabin && second->kind != WalkSurfaceKind::ElevatorCabin &&
        TouchingSupports(*first, *second) && std::abs(SurfaceHeight(*first, point) - SurfaceHeight(*second, point)) <= LevelConfig::SupportTolerance) return true;
    for (const auto &link : world.verticalLinks) {
      const bool forward = link.fromSupport == from && link.toSupport == to;
      const bool reverse = link.bidirectional && link.fromSupport == to && link.toSupport == from;
      if ((!forward && !reverse) || (link.kind != VerticalLinkKind::Stair && link.kind != VerticalLinkKind::ElevatorDock)) continue;
      if (link.kind == VerticalLinkKind::ElevatorDock && !ElevatorDocked(world, link.elevator, forward ? link.from.z : link.to.z)) continue;
      const auto fromPoint = Ground(forward ? link.from : link.to), toPoint = Ground(forward ? link.to : link.from);
      const auto delta = toPoint - fromPoint;
      const float t = delta.lengthSquared() > 0.f ? std::clamp((point - fromPoint).dot(delta) / delta.lengthSquared(), 0.f, 1.f) : 0.f;
      if ((point - fromPoint - delta * t).length() <= link.width * 0.5f + GameplayConfig::AgentRadius) return true;
    }
    return false;
  }
  inline std::optional<SupportPoint> FindSupport(const WorldState &world, sf::Vector2f point, float elevation, std::uint32_t previousSupport, float radius) {
    if (world.walkSurfaces.empty()) return SupportPoint{0, elevation};
    std::optional<SupportPoint> selected;
    float best = std::numeric_limits<float>::infinity();
    for (const auto &surface : world.walkSurfaces) {
      if (!ContainsSupport(surface, point) || !SupportsConnected(world, previousSupport, surface.id, point)) continue;
      const float height = SurfaceHeight(surface, point), difference = std::abs(height - elevation);
      const float limit = previousSupport == 0 ? LevelConfig::SupportTolerance : LevelConfig::MaximumStepHeight;
      if (difference > limit) continue;
      const float score = difference - (surface.id == previousSupport ? LevelConfig::MaximumStepHeight + LevelConfig::SupportTolerance : 0.f);
      const bool risingStair = surface.kind == WalkSurfaceKind::Stair && height >= elevation && difference <= limit;
      if (!selected || score < best || (risingStair && surface.id != previousSupport && difference < radius)) { selected = SupportPoint{surface.id, height}; best = score; }
    }
    return selected;
  }
  inline bool HeightOverlap(float bottom, float top, float otherBottom, float otherTop) {
    return bottom < otherTop - SpatialConfig::CollisionSkin && top > otherBottom + SpatialConfig::CollisionSkin;
  }
  inline bool PositionBlocked(const WorldState &world, sf::Vector2f position, float radius, float elevation, float height, std::uint32_t ignoreSupport = 0, std::uint32_t ignoreObject = 0) {
    if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(elevation) || !std::isfinite(height) || radius <= 0.f || height <= 0.f ||
        position.x < radius || position.y < radius || position.x > world.size.x - radius || position.y > world.size.y - radius) return true;
    for (const auto &obstacle : world.obstacles) if (BlocksMovement(obstacle) && HeightOverlap(elevation, elevation + height, obstacle.elevation, obstacle.elevation + ObstacleHeight(obstacle)) && CircleOverlaps(position, radius, obstacle.bounds)) return true;
    for (const auto &object : world.objects) if (object.id != ignoreObject && BlocksMovement(object) && HeightOverlap(elevation, elevation + height, object.elevation, object.elevation + ObjectHeight(object)) && CircleOverlaps(position, radius, ObjectFootprint(object))) return true;
    for (const auto &surface : world.walkSurfaces) if (surface.enabled && surface.id != ignoreSupport && CircleOverlaps(position, radius, surface.bounds)) {
      const auto nearest = ClosestPoint(position, surface.bounds);
      const float top = SurfaceHeight(surface, nearest);
      if (ignoreSupport != 0 && SupportsConnected(world, ignoreSupport, surface.id, nearest) && std::abs(top - elevation) <= LevelConfig::MaximumStepHeight) continue;
      if (HeightOverlap(elevation, elevation + height, top - surface.thickness, top)) return true;
    }
    return false;
  }
  inline bool ClearBodySegment(const WorldState &world, sf::Vector3f from, sf::Vector3f to, float radius, float height, std::uint32_t ignoreSupport = 0, std::uint32_t ignoreObject = 0) {
    const auto delta = to - from;
    const int steps = std::max(1, static_cast<int>(std::ceil(delta.length() / SpatialConfig::MaximumMovementStep)));
    for (int step = 0; step <= steps; ++step) {
      const auto point = from + delta * (static_cast<float>(step) / steps);
      if (PositionBlocked(world, Ground(point), radius, point.z, height, ignoreSupport, ignoreObject)) return false;
    }
    return true;
  }
  inline bool MoveOnSupports(const WorldState &world, sf::Vector2f &position, float &elevation, std::uint32_t &support, sf::Vector2f displacement, float radius, float height) {
    const auto before = position;
    const int steps = std::max(1, static_cast<int>(std::ceil(displacement.length() / SpatialConfig::MaximumMovementStep)));
    const auto delta = displacement / static_cast<float>(steps);
    const auto tryMove = [&](sf::Vector2f candidate) {
      const auto next = FindSupport(world, candidate, elevation, support, radius);
      if (!next || PositionBlocked(world, candidate, radius, next->elevation, height, next->id)) return false;
      if (const auto *surface = FindSurface(world, next->id); surface && surface->kind == WalkSurfaceKind::ElevatorCabin) {
        const auto lift = std::ranges::find(world.elevators, surface->id, &ElevatorState::cabinSupport);
        const auto end = surface->bounds.position + surface->bounds.size;
        if (lift != world.elevators.end() && lift->phase != ElevatorPhase::Docked && (candidate.x < surface->bounds.position.x + radius || candidate.y < surface->bounds.position.y + radius || candidate.x > end.x - radius || candidate.y > end.y - radius)) return false;
      }
      position = candidate; elevation = next->elevation; support = next->id; return true;
    };
    for (int step = 0; step < steps; ++step) {
      if (tryMove(position + delta)) continue;
      tryMove(position + sf::Vector2f{delta.x, 0.f}); tryMove(position + sf::Vector2f{0.f, delta.y});
    }
    return (position - before).lengthSquared() > 0.0001f;
  }
  inline bool SegmentPrism(sf::Vector3f from, sf::Vector3f to, sf::FloatRect bounds, float bottom, float top, float *intersection = nullptr) {
    float near = 0.f, far = 1.f;
    const auto delta = to - from;
    const auto axis = [&](float origin, float direction, float minimum, float maximum) {
      if (std::abs(direction) < 0.0001f) return origin >= minimum && origin <= maximum;
      float first = (minimum - origin) / direction, second = (maximum - origin) / direction;
      if (first > second) std::swap(first, second);
      near = std::max(near, first); far = std::min(far, second); return near <= far;
    };
    if (!axis(from.x, delta.x, bounds.position.x, bounds.position.x + bounds.size.x) || !axis(from.y, delta.y, bounds.position.y, bounds.position.y + bounds.size.y) || !axis(from.z, delta.z, bottom, top)) return false;
    if (intersection) *intersection = near;
    return far >= 0.f && near <= 1.f;
  }
  inline bool RayOccluded(const WorldState &world, sf::Vector3f from, sf::Vector3f to, std::uint32_t ignoreObject = 0) {
    const auto blocked = [&](sf::FloatRect bounds, float bottom, float top) {
      float intersection = 1.f;
      return top > bottom && SegmentPrism(from, to, bounds, bottom, top, &intersection) && intersection < 1.f - 0.0001f;
    };
    for (const auto &obstacle : world.obstacles) if (obstacle.blocksVision && blocked(obstacle.bounds, obstacle.elevation, obstacle.elevation + ObstacleHeight(obstacle))) return true;
    for (const auto &object : world.objects) if (object.id != ignoreObject && Properties(object).blocksVision) {
      const auto bounds = ObjectFootprint(object);
      const float top = object.elevation + ObjectHeight(object);
      if (HideoutType(object) != HideoutKind::UnderTable) { if (blocked(bounds, object.elevation, top)) return true; continue; }
      if (blocked(bounds, top - SpatialConfig::UnderTableTopThickness, top)) return true;
      const auto end = bounds.position + bounds.size;
      const float leg = SpatialConfig::UnderTableTopThickness;
      for (const auto corner : {bounds.position, sf::Vector2f{end.x - leg, bounds.position.y}, sf::Vector2f{bounds.position.x, end.y - leg}, end - sf::Vector2f{leg, leg}})
        if (blocked({corner, {leg, leg}}, object.elevation, top - SpatialConfig::UnderTableTopThickness)) return true;
    }
    for (const auto &surface : world.walkSurfaces) if (surface.enabled) {
      auto localFrom = from, localTo = to;
      localFrom.z -= SurfaceHeight(surface, Ground(from)); localTo.z -= SurfaceHeight(surface, Ground(to));
      float intersection = 1.f;
      if (SegmentPrism(localFrom, localTo, surface.bounds, -surface.thickness, 0.f, &intersection) && intersection < 1.f - 0.0001f) return true;
    }
    return false;
  }
  inline SurfaceMaterial MaterialAt(const WorldState &world, std::uint32_t support) {
    const auto *surface = FindSurface(world, support);
    return surface ? surface->material : SurfaceMaterial::Concrete;
  }
}
