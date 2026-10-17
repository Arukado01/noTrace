#include "NoTraceVisibility.h"
#include "Config/NoTraceVisibilityConfig.h"
#include "Config/NoTraceDistractionConfig.h"
#include "NoTraceWorldGeometry.h"
#include <algorithm>
#include <cmath>

namespace NoTrace::Visibility {
  namespace {
    bool GridVisible(const VisibilityGrid &grid, sf::Vector2f point) {
      if (grid.cellSize <= 0.f || point.x < 0.f || point.y < 0.f) return false;
      const auto column = static_cast<unsigned int>(point.x / grid.cellSize);
      const auto row = static_cast<unsigned int>(point.y / grid.cellSize);
      if (column >= grid.columns || row >= grid.rows) return false;
      const auto index = static_cast<std::size_t>(row) * grid.columns + column;
      return index < grid.cells.size() && grid.cells[index] != 0;
    }

    bool GeometricVisibility(const WorldState &world, const Agent &observer, sf::Vector2f point, Stance targetStance) {
      using namespace VisibilityConfig;
      if (!std::isfinite(point.x) || !std::isfinite(point.y) || point.x < 0.f || point.y < 0.f || point.x > world.size.x || point.y > world.size.y) return false;
      const auto delta = point - observer.position;
      const float distance = std::hypot(delta.x, delta.y);
      if (distance < SurfaceOffset) return true;
      const float range = observer.stance == Stance::Prone ? ProneRange : observer.stance == Stance::Crouched ? CrouchedRange : StandingRange;
      if (distance > range) return false;
      const float facingLength = std::hypot(observer.facing.x, observer.facing.y);
      if (distance > CloseRange && facingLength > 0.f && observer.facing.dot(delta) / (facingLength * distance) < CosHalfAngle) return false;
      const auto lowStance = observer.stance == Stance::Standing ? targetStance : observer.stance;
      return HasLineOfSight(world, observer.position, point, lowStance);
    }
  }

  bool HasLineOfSight(const WorldState &world, sf::Vector2f from, sf::Vector2f to, Stance stance, std::uint32_t ignoredObject) {
    for (const auto &obstacle : world.obstacles) {
      float intersection = 1.f;
      if (WorldGeometry::BlocksVision(obstacle, stance) && WorldGeometry::SegmentRect(from, to, obstacle.bounds, &intersection) && intersection < 1.f - 0.0001f) return false;
    }
    for (const auto &object : world.objects) {
      if (ignoredObject != 0 && object.id == ignoredObject) continue;
      float intersection = 1.f;
      if (WorldGeometry::BlocksVision(object, stance) && WorldGeometry::SegmentRect(from, to, WorldGeometry::ObjectFootprint(object), &intersection) && intersection < 1.f - 0.0001f) return false;
    }
    return true;
  }

  bool IsVisible(const WorldState &world, const Agent &observer, sf::Vector2f point, Stance targetStance) {
    if (world.phase != GamePhase::Mission) return true;
    if (world.visibility.observer == observer.id) return GridVisible(world.visibility, point);
    return GeometricVisibility(world, observer, point, targetStance);
  }

  bool GuardVisible(const WorldState &world, const Agent &observer, const Guard &guard) {
    if (guard.bodyHidden) return false;
    if (world.visibility.authoritative && world.visibility.observer == observer.id) return true;
    return !guard.bodyHidden && IsVisible(world, observer, guard.position,
      guard.state == GuardState::Dead || guard.state == GuardState::Incapacitated || guard.cqcState == CqcState::KnockedDown || guard.cqcState == CqcState::Restrained ? Stance::Prone : Stance::Standing);
  }

  bool ObjectVisible(const WorldState &world, const Agent &observer, const Interactable &object) {
    if (observer.hideoutId != 0 && observer.hideoutId == object.id) return true;
    if (world.visibility.authoritative && world.visibility.observer == observer.id) return true;
    if (IsVisible(world, observer, object.position)) return true;
    const auto bounds = WorldGeometry::ObjectFootprint(object);
    const float offset = VisibilityConfig::SurfaceOffset;
    const auto end = bounds.position + bounds.size;
    const auto nearest = WorldGeometry::ClosestPoint(observer.position, bounds);
    const auto approach = observer.position - nearest;
    const float length = std::hypot(approach.x, approach.y);
    if (length > 0.f && IsVisible(world, observer, nearest + approach * (offset / length))) return true;
    for (const auto point : {sf::Vector2f{bounds.position.x - offset, bounds.position.y - offset},
                             sf::Vector2f{end.x + offset, bounds.position.y - offset},
                             sf::Vector2f{bounds.position.x - offset, end.y + offset},
                             sf::Vector2f{end.x + offset, end.y + offset}}) {
      if (IsVisible(world, observer, point)) return true;
    }
    return false;
  }

  bool FootstepAudible(const WorldState &world, const Agent &observer, const FootstepCue &cue) {
    if (world.phase != GamePhase::Mission || !observer.alive || observer.incapacitated || observer.extracted || cue.remaining <= 0.f) return false;
    if ((cue.position - observer.position).lengthSquared() > DistractionConfig::PlayerHearingDistance * DistractionConfig::PlayerHearingDistance) return false;
    if (world.visibility.authoritative && world.visibility.observer == observer.id) return true;
    const auto source = std::ranges::find_if(world.guards, [&cue](const Guard &guard) { return guard.id == cue.sourceGuard; });
    return source != world.guards.end() && !source->bodyHidden && source->state != GuardState::Dead && source->state != GuardState::Incapacitated && !GuardVisible(world, observer, *source);
  }

  VisibilityGrid BuildGrid(const WorldState &world, const Agent &observer) {
    if (world.visibility.observer == observer.id) return world.visibility;
    VisibilityGrid grid;
    grid.observer = observer.id; grid.tick = world.tick; grid.cellSize = VisibilityConfig::CellSize;
    grid.columns = static_cast<unsigned int>(std::ceil(world.size.x / grid.cellSize));
    grid.rows = static_cast<unsigned int>(std::ceil(world.size.y / grid.cellSize));
    const auto count = static_cast<std::size_t>(grid.columns) * grid.rows;
    if (count > VisibilityConfig::MaximumCells) return {};
    grid.cells.resize(count, 0);
    for (unsigned int row = 0; row < grid.rows; ++row) for (unsigned int column = 0; column < grid.columns; ++column) {
      const sf::Vector2f point{std::min(world.size.x - 0.01f, (column + 0.5f) * grid.cellSize), std::min(world.size.y - 0.01f, (row + 0.5f) * grid.cellSize)};
      grid.cells[static_cast<std::size_t>(row) * grid.columns + column] = world.phase != GamePhase::Mission || GeometricVisibility(world, observer, point, Stance::Standing);
    }
    const auto column = static_cast<unsigned int>(std::max(0.f, observer.position.x) / grid.cellSize);
    const auto row = static_cast<unsigned int>(std::max(0.f, observer.position.y) / grid.cellSize);
    if (column < grid.columns && row < grid.rows) grid.cells[static_cast<std::size_t>(row) * grid.columns + column] = 1;
    return grid;
  }
}
