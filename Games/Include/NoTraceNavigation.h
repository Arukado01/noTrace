#pragma once

#include "NoTraceSpatialGeometry.h"
#include "NoTracePerformance.h"
#include <queue>
#include <unordered_map>

namespace NoTrace::Navigation {
  struct RouteStep {
    std::uint32_t fromSupport = 0;
    std::uint32_t toSupport = 0;
    std::uint32_t linkId = 0;
    sf::Vector3f entry;
    sf::Vector3f exit;
    std::uint32_t elevator = 0;
  };
  inline std::vector<RouteStep> Route(const WorldState &world, std::uint32_t from, std::uint32_t to) {
    NO_TRACE_PROFILE_SCOPE(NavigationRoute);
    if (from == 0 || to == 0 || from == to) return {};
    std::queue<std::uint32_t> pending; pending.push(from);
    std::unordered_map<std::uint32_t, RouteStep> previous; previous[from] = {};
    const auto visit = [&](RouteStep step) {
      if (step.toSupport == 0 || previous.contains(step.toSupport)) return;
      previous[step.toSupport] = step; pending.push(step.toSupport);
    };
    while (!pending.empty() && !previous.contains(to)) {
      const auto current = pending.front(); pending.pop();
      const auto *surface = WorldGeometry::FindSurface(world, current);
      if (!surface || !surface->enabled) continue;
      for (const auto &other : world.walkSurfaces) {
        if (other.id == current || !other.enabled || surface->kind == WalkSurfaceKind::ElevatorCabin || other.kind == WalkSurfaceKind::ElevatorCabin || !WorldGeometry::TouchingSupports(*surface, other)) continue;
        const auto end = surface->bounds.position + surface->bounds.size, otherEnd = other.bounds.position + other.bounds.size;
        const sf::Vector2f lo{std::max(surface->bounds.position.x, other.bounds.position.x), std::max(surface->bounds.position.y, other.bounds.position.y)};
        const sf::Vector2f hi{std::min(end.x, otherEnd.x), std::min(end.y, otherEnd.y)};
        for (const float sample : {0.5f, 0.125f, 0.25f, 0.375f, 0.625f, 0.75f, 0.875f}) {
          const auto crossing = lo + (hi - lo) * sample;
          const float firstZ = WorldGeometry::SurfaceHeight(*surface, crossing), secondZ = WorldGeometry::SurfaceHeight(other, crossing);
          if (std::abs(firstZ - secondZ) > LevelConfig::SupportTolerance) continue;
          const auto center = other.bounds.position + other.bounds.size * 0.5f;
          const auto offset = center - crossing;
          const auto point = crossing + (offset.lengthSquared() > 0.f ? offset.normalized() * std::min(16.f, offset.length()) : sf::Vector2f{});
          if (!WorldGeometry::ClearBodySegment(world, {crossing.x, crossing.y, firstZ}, {point.x, point.y, WorldGeometry::SurfaceHeight(other, point)}, GameplayConfig::AgentRadius, CombatConfig::StandingHeight, current)) continue;
          visit({current, other.id, 0, {crossing.x, crossing.y, firstZ}, {point.x, point.y, WorldGeometry::SurfaceHeight(other, point)}}); break;
        }
      }
      for (const auto &link : world.verticalLinks) {
        if (link.kind != VerticalLinkKind::Stair && link.kind != VerticalLinkKind::ElevatorDock) continue;
        if (link.fromSupport == current) visit({current, link.toSupport, link.id, link.from, link.to, link.elevator});
        else if (link.bidirectional && link.toSupport == current) visit({current, link.fromSupport, link.id, link.to, link.from, link.elevator});
      }
    }
    if (!previous.contains(to)) return {};
    std::vector<RouteStep> result;
    for (auto current = to; current != from;) { const auto step = previous.at(current); result.push_back(step); current = step.fromSupport; }
    std::ranges::reverse(result); return result;
  }
}
