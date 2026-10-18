#include "NoTraceVisibility.h"
#include "NoTracePerformance.h"
#include "NoTraceAudioGeometry.h"
#include "Config/NoTraceVisibilityConfig.h"
#include "Config/NoTraceDistractionConfig.h"
#include "Config/NoTraceAudioConfig.h"
#include "NoTraceSpatialGeometry.h"
#include "NoTraceOcclusionGeometry.h"
#include "NoTraceCombatGeometry.h"
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

    template<class Occluded>
    bool GeometricallyVisible(const WorldState &world, const Agent &observer, sf::Vector2f point, Stance targetStance, float targetElevation, const Occluded &occluded) {
      using namespace VisibilityConfig;
      if (!std::isfinite(point.x) || !std::isfinite(point.y) || point.x < 0.f || point.y < 0.f || point.x > world.size.x || point.y > world.size.y) return false;
      const auto delta = point - observer.position;
      const float planarDistance = std::hypot(delta.x, delta.y);
      const float targetZ = std::isfinite(targetElevation) ? targetElevation : observer.elevation;
      const float distance = std::hypot(planarDistance, targetZ - observer.elevation);
      const auto clear = [&](float eyeHeight, std::uint32_t ignoredObject = 0) {
        const float targetHeight = CombatGeometry::HeadCenterHeight(targetStance);
        return !occluded(sf::Vector3f{observer.position.x, observer.position.y, observer.elevation + eyeHeight}, {point.x, point.y, targetZ + targetHeight}, ignoredObject);
      };
      if (distance < SurfaceOffset) return true;
      if (observer.hideoutId != 0) {
        const auto hideout = std::ranges::find_if(world.objects, [&observer](const Interactable &object) { return object.id == observer.hideoutId; });
        if (hideout == world.objects.end() || hideout->occupantAgent != observer.id) return false;
        const auto kind = WorldGeometry::HideoutType(*hideout);
        if (kind == HideoutKind::UnderTable && !hideout->lifted) {
          if (distance > ProneRange) return false;
          const auto facing = WeaponFacing(observer);
          if (planarDistance > CloseRange && facing.lengthSquared() > 0.f && facing.dot(delta) / (facing.length() * planarDistance) < CosHalfAngle) return false;
          return clear(SpatialConfig::UnderTableEyeHeight);
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
        return clear(SpatialConfig::UnderTableEyeHeight, hideout->id);
      }
      const float range = observer.stance == Stance::Prone ? ProneRange : observer.stance == Stance::Crouched ? CrouchedRange : StandingRange;
      if (distance > range) return false;
      const bool aimingFromCover = observer.cover.active && observer.aiming &&
        ((observer.cover.peekSide == PeekSide::Left && observer.cover.canPeekLeft) || (observer.cover.peekSide == PeekSide::Right && observer.cover.canPeekRight));
      // La postura sigue apoyada en la pared, pero al asomarse la visión acompaña al arma.
      const auto facing = aimingFromCover ? WeaponFacing(observer) : observer.facing;
      const float facingLength = std::hypot(facing.x, facing.y);
      if (distance > CloseRange && facingLength > 0.f && facing.dot(delta) / (facingLength * distance) < CosHalfAngle) return false;
      return clear(CombatGeometry::HeadCenterHeight(observer.stance));
    }

    void VisualBoundary(const WorldState &world, const Agent &observer, VisibilityGrid &grid, const WorldGeometry::PreparedOccluders &geometry) {
      NO_TRACE_PROFILE_SCOPE(VisibilityBoundary);
      grid.visualOrigin = observer.position; grid.elevation = observer.elevation;
      if (!IsTactical(world)) { grid.visualRadii.clear(); return; }
      grid.visualRadii.resize(VisibilityConfig::VisualRayCount);
      WorldGeometry::PreparedOccluders::Query query(geometry);
      for (std::size_t ray = 0; ray < grid.visualRadii.size(); ++ray) {
        const float angle = 2.f * std::numbers::pi_v<float> * static_cast<float>(ray) / grid.visualRadii.size();
        const sf::Vector2f direction{std::cos(angle), std::sin(angle)};
        query.Select(observer.position, observer.position + direction * VisibilityConfig::StandingRange);
        float visible = 0.f, hidden = VisibilityConfig::StandingRange;
        for (unsigned int iteration = 0; iteration < VisibilityConfig::VisualRayIterations; ++iteration) {
          const float distance = (visible + hidden) * 0.5f;
          const bool clear = GeometricallyVisible(world, observer, observer.position + direction * distance, Stance::Prone,
              std::numeric_limits<float>::quiet_NaN(), [&](sf::Vector3f from, sf::Vector3f to, std::uint32_t ignoredObject) { return query.Occluded(from, to, ignoredObject); });
          if (clear) visible = distance;
          else hidden = distance;
        }
        grid.visualRadii[ray] = visible;
      }
    }
  }

  bool IsGeometricallyVisible(const WorldState &world, const Agent &observer, sf::Vector2f point, Stance targetStance, float targetElevation, const WorldGeometry::PreparedOccluders *geometry) {
    return GeometricallyVisible(world, observer, point, targetStance, targetElevation,
        [&](sf::Vector3f from, sf::Vector3f to, std::uint32_t ignoredObject) { return geometry ? geometry->Occluded(from, to, ignoredObject) : WorldGeometry::RayOccluded(world, from, to, ignoredObject); });
  }

  void BuildVisualBoundary(const WorldState &world, const Agent &observer, VisibilityGrid &grid) {
    const WorldGeometry::PreparedOccluders geometry(world);
    VisualBoundary(world, observer, grid, geometry);
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

  bool AgentExposed(const WorldState &world, sf::Vector2f from, const Agent &target, std::uint32_t ignoredObject, float eyeHeight) {
    if (target.extracted) return false;
    if (target.hidden) {
      const auto hideout = std::ranges::find_if(world.objects, [&target](const Interactable &object) {
        return object.id == target.hideoutId && object.occupantAgent == target.id && WorldGeometry::HideoutType(object) == HideoutKind::UnderTable && !object.lifted;
      });
      if (hideout == world.objects.end()) return false;
    }
    const auto clear = [&](sf::Vector2f point, float height) {
      return !WorldGeometry::RayOccluded(world, {from.x, from.y, eyeHeight}, {point.x, point.y, height}, ignoredObject);
    };
    for (const auto &volume : CombatGeometry::ActorVolumes(target.position, target.facing, target.stance, target.incapacitated, target.elevation)) {
      if (volume.zone != HitZone::Head && volume.zone != HitZone::Torso) continue;
      const sf::Vector2f side{-volume.forward.y, volume.forward.x};
      const float height = (volume.bottom + volume.top) * 0.5f;
      for (float offset : {-0.8f, 0.f, 0.8f}) if (clear(volume.center + side * (volume.halfSize.y * offset), height)) return true;
    }
    return false;
  }

  bool IsVisible(const WorldState &world, const Agent &observer, sf::Vector2f point, Stance targetStance) {
    if (world.phase != GamePhase::Mission) return true;
    if (world.visibility.observer == observer.id) return GridVisible(world.visibility, point);
    return IsGeometricallyVisible(world, observer, point, targetStance);
  }

  bool GuardVisible(const WorldState &world, const Agent &observer, const Guard &guard, const WorldGeometry::PreparedOccluders *geometry) {
    if (guard.bodyHidden) return false;
    if (world.phase != GamePhase::Mission) return true;
    if (world.visibility.authoritative && world.visibility.observer == observer.id) return true;
    return !guard.bodyHidden && IsGeometricallyVisible(world, observer, guard.position,
      guard.state == GuardState::Dead || guard.state == GuardState::Incapacitated || (!guard.trainingDummy && guard.heldBy == 0 && (guard.cqcState == CqcState::KnockedDown || guard.cqcState == CqcState::Restrained)) ? Stance::Prone : Stance::Standing, guard.elevation, geometry);
  }

  bool ObjectVisible(const WorldState &world, const Agent &observer, const Interactable &object, const WorldGeometry::PreparedOccluders *geometry) {
    if (object.kind == ObjectKind::Explosive && object.trap.state == TrapState::Armed) return false;
    if (observer.hideoutId != 0 && observer.hideoutId == object.id) return true;
    if (world.visibility.authoritative && world.visibility.observer == observer.id) return true;
    if (IsGeometricallyVisible(world, observer, object.position, Stance::Standing, object.elevation, geometry)) return true;
    const auto bounds = WorldGeometry::ObjectFootprint(object);
    const float offset = VisibilityConfig::SurfaceOffset;
    const auto end = bounds.position + bounds.size;
    const auto nearest = WorldGeometry::ClosestPoint(observer.position, bounds);
    const auto approach = observer.position - nearest;
    const float length = std::hypot(approach.x, approach.y);
    if (length > 0.f && IsGeometricallyVisible(world, observer, nearest + approach * (offset / length), Stance::Standing, object.elevation, geometry)) return true;
    for (const auto point : {sf::Vector2f{bounds.position.x - offset, bounds.position.y - offset},
                             sf::Vector2f{end.x + offset, bounds.position.y - offset},
                             sf::Vector2f{bounds.position.x - offset, end.y + offset},
                             sf::Vector2f{end.x + offset, end.y + offset}}) {
      if (IsGeometricallyVisible(world, observer, point, Stance::Standing, object.elevation, geometry)) return true;
    }
    return false;
  }

  bool FootstepAudible(const WorldState &world, const Agent &observer, const FootstepCue &cue) {
    if (!IsTactical(world) || !observer.alive || observer.incapacitated || observer.extracted || cue.remaining <= 0.f) return false;
    if (WorldGeometry::SpatialDistance(WorldGeometry::Feet(observer), {cue.position.x, cue.position.y, cue.elevation}) > DistractionConfig::PlayerHearingDistance) return false;
    if (world.visibility.authoritative && world.visibility.observer == observer.id) return true;
    const auto source = std::ranges::find_if(world.guards, [&cue](const Guard &guard) { return guard.id == cue.sourceGuard; });
    return source != world.guards.end() && !source->bodyHidden && source->state != GuardState::Dead && source->state != GuardState::Incapacitated && !GuardVisible(world, observer, *source);
  }

  bool SoundAudible(const WorldState &world, const Agent &observer, const SoundEvent &sound) {
    if (observer.extracted || sound.remaining <= 0.f) return false;
    const bool ownLife = sound.actor == observer.id && (sound.kind == SoundKind::Death || sound.kind == SoundKind::Hurt || sound.kind == SoundKind::Revive);
    if ((!observer.alive || observer.incapacitated) && sound.kind != SoundKind::Explosion && sound.kind != SoundKind::Fall && !ownLife) return false;
    if (world.visibility.authoritative && world.visibility.observer == observer.id) return true;
    if (sound.actor == observer.id) return true;
    const float muffling = AudioGeometry::Occluded(world, observer, sound.position, sound.elevation) ? AudioConfig::WallHearingFactor : 1.f;
    return WorldGeometry::SpatialDistance(WorldGeometry::Feet(observer), {sound.position.x, sound.position.y, sound.elevation}) <= sound.radius * muffling;
  }

  bool NoiseAudible(const WorldState &world, const Agent &observer, const NoiseEvent &noise) {
    return SoundAudible(world, observer, {0, noise.kind, noise.position, noise.radius, noise.remaining, noise.actor, noise.elevation});
  }

  VisibilityGrid BuildGrid(const WorldState &world, const Agent &observer) {
    NO_TRACE_PROFILE_SCOPE(VisibilityGrid);
    if (world.visibility.observer == observer.id) return world.visibility;
    VisibilityGrid grid;
    grid.observer = observer.id; grid.tick = world.tick; grid.cellSize = VisibilityConfig::CellSize;
    grid.columns = static_cast<unsigned int>(std::ceil(world.size.x / grid.cellSize));
    grid.rows = static_cast<unsigned int>(std::ceil(world.size.y / grid.cellSize));
    const auto count = static_cast<std::size_t>(grid.columns) * grid.rows;
    if (count > VisibilityConfig::MaximumCells) return {};
    grid.cells.resize(count, 0);
    const WorldGeometry::PreparedOccluders geometry(world);
    for (unsigned int row = 0; row < grid.rows; ++row) for (unsigned int column = 0; column < grid.columns; ++column) {
      const sf::Vector2f point{std::min(world.size.x - 0.01f, (column + 0.5f) * grid.cellSize), std::min(world.size.y - 0.01f, (row + 0.5f) * grid.cellSize)};
      grid.cells[static_cast<std::size_t>(row) * grid.columns + column] = world.phase != GamePhase::Mission || GeometricallyVisible(world, observer, point, Stance::Prone,
          std::numeric_limits<float>::quiet_NaN(), [&](sf::Vector3f from, sf::Vector3f to, std::uint32_t ignoredObject) { return geometry.Occluded(from, to, ignoredObject); });
    }
    const auto column = static_cast<unsigned int>(std::max(0.f, observer.position.x) / grid.cellSize);
    const auto row = static_cast<unsigned int>(std::max(0.f, observer.position.y) / grid.cellSize);
    if (column < grid.columns && row < grid.rows) grid.cells[static_cast<std::size_t>(row) * grid.columns + column] = 1;
    VisualBoundary(world, observer, grid, geometry);
    return grid;
  }
}
