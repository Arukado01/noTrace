#pragma once

#include "NoTraceSpatialGeometry.h"

namespace NoTrace::Furniture {
  inline bool TablePlacementClear(const WorldState &world, const Interactable &table, bool overturned, PlayerId departing = 0, sf::Vector2f departure = {}) {
    auto next = table; next.overturned = overturned; next.lifted = false; next.footprint = {};
    const auto bounds = WorldGeometry::ObjectFootprint(next);
    const auto end = bounds.position + bounds.size;
    if (bounds.position.x < 0.f || bounds.position.y < 0.f || end.x > world.size.x || end.y > world.size.y) return false;
    if (const auto *surface = WorldGeometry::FindSurface(world, table.supportId)) for (const auto point : {bounds.position, end, sf::Vector2f{end.x, bounds.position.y}, sf::Vector2f{bounds.position.x, end.y}}) {
      if (!WorldGeometry::ContainsSupport(*surface, point) || std::abs(WorldGeometry::SurfaceHeight(*surface, point) - table.elevation) > LevelConfig::SupportTolerance) return false;
    }
    const auto overlaps = [&](float z, float height) { return WorldGeometry::HeightOverlap(table.elevation, table.elevation + WorldGeometry::ObjectHeight(next), z, z + height); };
    for (const auto &other : world.agents) if (other.connected && !other.extracted && overlaps(other.elevation, WorldGeometry::ActorHeight(other)) &&
        WorldGeometry::CircleOverlaps(other.id == departing ? departure : other.position, GameplayConfig::AgentRadius, bounds)) return false;
    for (const auto &guard : world.guards) if (!guard.bodyHidden && guard.storedIn == 0 && overlaps(guard.elevation, CombatConfig::StandingHeight) &&
        WorldGeometry::CircleOverlaps(guard.position, GameplayConfig::AgentRadius, bounds)) return false;
    for (const auto &wall : world.obstacles) if (WorldGeometry::BlocksMovement(wall) && overlaps(wall.elevation, WorldGeometry::ObstacleHeight(wall)) && bounds.findIntersection(wall.bounds)) return false;
    for (const auto &object : world.objects) if (object.id != table.id && WorldGeometry::BlocksMovement(object) && overlaps(object.elevation, WorldGeometry::ObjectHeight(object)) &&
        bounds.findIntersection(WorldGeometry::ObjectFootprint(object))) return false;
    return true;
  }

  inline bool CanFlipTable(const WorldState &world, const Agent &agent, const Interactable &table) {
    if (WorldGeometry::HideoutType(table) != HideoutKind::UnderTable || WorldGeometry::HideoutOccupied(table) ||
        !agent.connected || !agent.alive || agent.incapacitated || agent.hidden || agent.extracted || agent.draggingGuard >= 0 ||
        agent.traversal.mode != TraversalMode::Grounded || agent.maneuver.kind != ManeuverKind::None || agent.cqcHeldBy || agent.cqcHoldingGuard || agent.cqcHoldingAgent ||
        agent.cqcPressPending || agent.cqcAttackTime > 0.f || agent.staggerRemaining > 0.f || agent.reloadRemaining > 0.f || agent.puzzle.target ||
        std::abs(agent.elevation - table.elevation) > LevelConfig::SupportTolerance) return false;
    const auto original = WorldGeometry::ObjectFootprint(table);
    if (WorldGeometry::DistanceToBounds(agent.position, original) >= GameplayConfig::InteractionDistance ||
        WorldGeometry::RayOccluded(world, WorldGeometry::Feet(agent) + sf::Vector3f{0.f, 0.f, CombatConfig::StandingWeaponHeight},
          {table.position.x, table.position.y, table.elevation + SpatialConfig::UnderTableHeight}, table.id)) return false;
    return TablePlacementClear(world, table, !table.overturned);
  }
}
