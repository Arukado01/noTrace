#include "NoTraceVisibility.h"
#include "Config/NoTraceVisibilityConfig.h"
#include "Config/NoTraceDistractionConfig.h"
#include "Config/NoTraceAudioConfig.h"
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

  }

  bool IsGeometricallyVisible(const WorldState &world, const Agent &observer, sf::Vector2f point, Stance targetStance) {
    using namespace VisibilityConfig;
    if (!std::isfinite(point.x) || !std::isfinite(point.y) || point.x < 0.f || point.y < 0.f || point.x > world.size.x || point.y > world.size.y) return false;
    const auto delta = point - observer.position;
    const float distance = std::hypot(delta.x, delta.y);
    if (distance < SurfaceOffset) return true;
    if (observer.hideoutId != 0) {
      const auto hideout = std::ranges::find_if(world.objects, [&observer](const Interactable &object) { return object.id == observer.hideoutId; });
      if (hideout == world.objects.end() || hideout->occupantAgent != observer.id) return false;
      const auto kind = WorldGeometry::HideoutType(*hideout);
      if (kind == HideoutKind::UnderTable && !hideout->lifted) {
        const float facingLength = observer.facing.length();
        if (distance > SpatialConfig::UnderTableViewRange || facingLength <= 0.f || observer.facing.dot(delta) / (facingLength * distance) < SpatialConfig::UnderTableViewCosHalfAngle) return false;
        const float targetHeight = targetStance == Stance::Standing ? CombatConfig::StandingHeight - CombatConfig::HeadHeight * 0.5f : targetStance == Stance::Crouched ? CombatConfig::CrouchedHeight - CombatConfig::HeadHeight * 0.5f : SpatialConfig::UnderTableEyeHeight;
        const float rise = targetHeight - SpatialConfig::UnderTableEyeHeight;
        if (rise > 0.f) {
          const float underside = WorldGeometry::ObjectHeight(*hideout) - SpatialConfig::UnderTableTopThickness;
          const float crossing = (underside - SpatialConfig::UnderTableEyeHeight) / rise;
          if (crossing >= 0.f && crossing <= 1.f && WorldGeometry::ObjectFootprint(*hideout).contains(observer.position + delta * crossing)) return false;
        }
        return HasLineOfSight(world, observer.position, point, Stance::Prone, hideout->id);
      }
      if (kind != HideoutKind::Closet || !hideout->open || hideout->hideoutAperture <= 0.f || distance > SpatialConfig::ClosetViewRange) return false;
      const auto normal = WorldGeometry::HideoutFrontNormal(*hideout);
      if (normal.dot(delta) / distance < SpatialConfig::ClosetViewCosHalfAngle) return false;
      const sf::Vector2f tangent{-normal.y, normal.x};
      const auto size = WorldGeometry::ObjectFootprint(*hideout).size;
      const float depth = (std::abs(normal.x) * size.x + std::abs(normal.y) * size.y) * 0.5f;
      const float halfWidth = (std::abs(tangent.x) * size.x + std::abs(tangent.y) * size.y) * 0.5f * std::clamp(hideout->hideoutAperture, 0.f, 1.f);
      const float forward = normal.dot(delta);
      if (forward <= 0.f || std::abs(tangent.dot(delta) * depth / forward) > halfWidth) return false;
      return HasLineOfSight(world, observer.position, point, Stance::Prone, hideout->id);
    }
    const float range = observer.stance == Stance::Prone ? ProneRange : observer.stance == Stance::Crouched ? CrouchedRange : StandingRange;
    if (distance > range) return false;
    const float facingLength = std::hypot(observer.facing.x, observer.facing.y);
    if (distance > CloseRange && facingLength > 0.f && observer.facing.dot(delta) / (facingLength * distance) < CosHalfAngle) return false;
    const auto lowStance = observer.stance == Stance::Standing ? targetStance : observer.stance;
    return HasLineOfSight(world, observer.position, point, lowStance);
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
    return IsGeometricallyVisible(world, observer, point, targetStance);
  }

  bool GuardVisible(const WorldState &world, const Agent &observer, const Guard &guard) {
    if (guard.bodyHidden) return false;
    if (world.visibility.authoritative && world.visibility.observer == observer.id) return true;
    return !guard.bodyHidden && IsVisible(world, observer, guard.position,
      guard.state == GuardState::Dead || guard.state == GuardState::Incapacitated || (!guard.trainingDummy && guard.heldBy == 0 && (guard.cqcState == CqcState::KnockedDown || guard.cqcState == CqcState::Restrained)) ? Stance::Prone : Stance::Standing);
  }

  bool ObjectVisible(const WorldState &world, const Agent &observer, const Interactable &object) {
    if (object.kind == ObjectKind::Explosive && object.trap.state == TrapState::Armed) return false;
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
    if (!IsTactical(world) || !observer.alive || observer.incapacitated || observer.extracted || cue.remaining <= 0.f) return false;
    if ((cue.position - observer.position).lengthSquared() > DistractionConfig::PlayerHearingDistance * DistractionConfig::PlayerHearingDistance) return false;
    if (world.visibility.authoritative && world.visibility.observer == observer.id) return true;
    const auto source = std::ranges::find_if(world.guards, [&cue](const Guard &guard) { return guard.id == cue.sourceGuard; });
    return source != world.guards.end() && !source->bodyHidden && source->state != GuardState::Dead && source->state != GuardState::Incapacitated && !GuardVisible(world, observer, *source);
  }

  bool SoundAudible(const WorldState &world, const Agent &observer, const SoundEvent &sound) {
    if (observer.extracted || sound.remaining <= 0.f) return false;
    if ((!observer.alive || observer.incapacitated) && sound.kind != SoundKind::Explosion && sound.kind != SoundKind::Fall) return false;
    if (world.visibility.authoritative && world.visibility.observer == observer.id) return true;
    if (sound.actor == observer.id) return true;
    const float muffling = HasLineOfSight(world, observer.position, sound.position) ? 1.f : AudioConfig::WallHearingFactor;
    return (observer.position - sound.position).length() <= sound.radius * muffling;
  }

  bool NoiseAudible(const WorldState &world, const Agent &observer, const NoiseEvent &noise) {
    return SoundAudible(world, observer, {0, noise.kind, noise.position, noise.radius, noise.remaining, noise.actor});
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
      grid.cells[static_cast<std::size_t>(row) * grid.columns + column] = world.phase != GamePhase::Mission || IsGeometricallyVisible(world, observer, point, Stance::Prone);
    }
    const auto column = static_cast<unsigned int>(std::max(0.f, observer.position.x) / grid.cellSize);
    const auto row = static_cast<unsigned int>(std::max(0.f, observer.position.y) / grid.cellSize);
    if (column < grid.columns && row < grid.rows) grid.cells[static_cast<std::size_t>(row) * grid.columns + column] = 1;
    BuildVisualBoundary(world, observer, grid);
    return grid;
  }
}
