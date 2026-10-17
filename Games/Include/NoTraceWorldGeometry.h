#pragma once

#include "Config/NoTraceSpatialConfig.h"
#include "Types/NoTraceGameplayTypes.h"
#include <algorithm>
#include <cmath>

namespace NoTrace::WorldGeometry {
  struct ObjectProperties {
    bool solid = true;
    bool blocksVision = true;
    bool wallCover = false;
    bool interactive = true;
    bool decorative = false;
    CoverKind cover = CoverKind::Low;
  };

  inline bool Resolve(SpatialOverride value, bool fallback) {
    return value == SpatialOverride::Default ? fallback : value == SpatialOverride::Enabled;
  }
  inline HideoutKind HideoutType(const Interactable &object) {
    return object.hideoutKind != HideoutKind::None ? object.hideoutKind : object.kind == ObjectKind::Hideout ? HideoutKind::TrashBin : HideoutKind::None;
  }
  inline bool HideoutAllowsBody(const Interactable &object) {
    return HideoutType(object) == HideoutKind::TrashBin || HideoutType(object) == HideoutKind::Closet;
  }
  inline bool HideoutOccupied(const Interactable &object) { return object.occupied || object.occupantAgent != 0 || object.occupantGuard != 0; }
  inline sf::Vector2f HideoutFrontNormal(const Interactable &object) {
    if (std::abs(object.facing.x) >= std::abs(object.facing.y)) return {object.facing.x > 0.f ? 1.f : -1.f, 0.f};
    return {0.f, object.facing.y > 0.f ? 1.f : -1.f};
  }

  inline ObjectProperties Properties(const Interactable &object) {
    ObjectProperties properties;
    switch (object.kind) {
    case ObjectKind::Armory:
    case ObjectKind::Intelligence:
    case ObjectKind::AlarmPanel:
      properties.cover = CoverKind::High; properties.wallCover = true; break;
    case ObjectKind::Door:
      properties.cover = CoverKind::High; break;
    case ObjectKind::Hideout:
      properties.cover = CoverKind::High; break;
    case ObjectKind::Deployment:
    case ObjectKind::Extraction:
    case ObjectKind::Camera:
      properties.solid = false; properties.blocksVision = false; properties.cover = CoverKind::None; break;
    default: break;
    }
    properties.solid = Resolve(object.solid, properties.solid);
    properties.blocksVision = Resolve(object.blocksVision, properties.blocksVision);
    properties.wallCover = Resolve(object.wallCover, properties.wallCover);
    properties.decorative = object.decorative;
    properties.interactive = object.interactive && !object.decorative;
    if (HideoutType(object) != HideoutKind::None) {
      properties.solid = true; properties.blocksVision = true; properties.wallCover = false;
      properties.cover = HideoutType(object) == HideoutKind::UnderTable ? CoverKind::Low : CoverKind::High;
    }
    if (object.kind == ObjectKind::Door && object.open) {
      properties.solid = false; properties.blocksVision = false; properties.wallCover = false;
    }
    if (HideoutType(object) == HideoutKind::UnderTable && object.lifted) {
      properties.solid = false; properties.blocksVision = false; properties.cover = CoverKind::None;
    }
    return properties;
  }

  inline sf::FloatRect ObjectFootprint(const Interactable &object) {
    const auto index = static_cast<std::size_t>(object.kind);
    auto fallback = index < SpatialConfig::ObjectGroundSizes.size() ? SpatialConfig::ObjectGroundSizes[index] : sf::Vector2f{};
    if (HideoutType(object) == HideoutKind::TrashBin) fallback = SpatialConfig::TrashBinSize;
    else if (HideoutType(object) == HideoutKind::Closet) fallback = SpatialConfig::ClosetSize;
    else if (HideoutType(object) == HideoutKind::UnderTable) fallback = SpatialConfig::UnderTableSize;
    const auto size = object.footprint.x > 0.f && object.footprint.y > 0.f ? object.footprint : fallback;
    return {object.position - size * 0.5f, size};
  }

  inline float ObjectHeight(const Interactable &object) {
    if (HideoutType(object) == HideoutKind::UnderTable && object.lifted) return 0.f;
    if (HideoutType(object) == HideoutKind::TrashBin) return SpatialConfig::HideoutHeight;
    if (HideoutType(object) == HideoutKind::Closet) return SpatialConfig::ClosetHeight;
    if (HideoutType(object) == HideoutKind::UnderTable) return SpatialConfig::UnderTableHeight;
    const auto index = static_cast<std::size_t>(object.kind);
    return index < SpatialConfig::ObjectHeights.size() ? SpatialConfig::ObjectHeights[index] : 0.f;
  }

  inline float ObstacleHeight(const Obstacle &obstacle) {
    if (obstacle.cover == CoverKind::High) return SpatialConfig::HighCoverHeight;
    if (obstacle.cover == CoverKind::Hide) return SpatialConfig::HideoutHeight;
    return SpatialConfig::LowCoverHeight;
  }

  inline bool BlocksMovement(const Interactable &object) { return Properties(object).solid; }
  inline bool BlocksMovement(const Obstacle &obstacle) { return obstacle.solid; }
  inline bool BlocksVision(const Interactable &object, Stance stance = Stance::Standing) {
    const auto properties = Properties(object);
    return properties.blocksVision && (properties.cover == CoverKind::High || (properties.cover == CoverKind::Low && stance != Stance::Standing));
  }
  inline bool BlocksVision(const Obstacle &obstacle, Stance stance = Stance::Standing) {
    return obstacle.blocksVision && (obstacle.cover == CoverKind::High || (obstacle.cover == CoverKind::Low && stance != Stance::Standing));
  }
  inline bool SupportsWallCover(const Interactable &object) {
    const auto properties = Properties(object);
    return properties.solid && properties.blocksVision && properties.wallCover && properties.cover == CoverKind::High;
  }
  inline bool SupportsWallCover(const Obstacle &obstacle) {
    return obstacle.solid && obstacle.blocksVision && obstacle.wallCover && obstacle.cover == CoverKind::High;
  }
  inline sf::FloatRect Expanded(const sf::FloatRect &bounds, float radius) {
    return {bounds.position - sf::Vector2f{radius, radius}, bounds.size + sf::Vector2f{radius * 2.f, radius * 2.f}};
  }
  inline sf::Vector2f ClosestPoint(sf::Vector2f point, const sf::FloatRect &bounds) {
    return {std::clamp(point.x, bounds.position.x, bounds.position.x + bounds.size.x),
            std::clamp(point.y, bounds.position.y, bounds.position.y + bounds.size.y)};
  }
  inline float DistanceToBounds(sf::Vector2f point, const sf::FloatRect &bounds) {
    return (point - ClosestPoint(point, bounds)).length();
  }
  inline bool CircleOverlaps(sf::Vector2f point, float radius, const sf::FloatRect &bounds) {
    return (point - ClosestPoint(point, bounds)).lengthSquared() < radius * radius - SpatialConfig::CollisionSkin * SpatialConfig::CollisionSkin;
  }
  inline bool SegmentRect(sf::Vector2f start, sf::Vector2f end, const sf::FloatRect &bounds, float *intersection = nullptr) {
    float near = 0.f, far = 1.f;
    const auto direction = end - start;
    for (int axis = 0; axis < 2; ++axis) {
      const float origin = axis == 0 ? start.x : start.y;
      const float delta = axis == 0 ? direction.x : direction.y;
      const float minimum = axis == 0 ? bounds.position.x : bounds.position.y;
      const float maximum = minimum + (axis == 0 ? bounds.size.x : bounds.size.y);
      if (std::abs(delta) < 0.0001f) { if (origin < minimum || origin > maximum) return false; }
      else {
        float first = (minimum - origin) / delta, second = (maximum - origin) / delta;
        if (first > second) std::swap(first, second);
        near = std::max(near, first); far = std::min(far, second);
        if (near > far) return false;
      }
    }
    if (far < 0.f || near > 1.f) return false;
    if (intersection) *intersection = std::max(0.f, near);
    return true;
  }
  inline bool AtHideoutEntrance(const Interactable &object, sf::Vector2f point, float radius = 0.f) {
    if (HideoutType(object) != HideoutKind::Closet) return true;
    const auto normal = HideoutFrontNormal(object);
    const sf::Vector2f tangent{-normal.y, normal.x};
    const auto size = ObjectFootprint(object).size;
    const float depth = (std::abs(normal.x) * size.x + std::abs(normal.y) * size.y) * 0.5f;
    const float width = (std::abs(tangent.x) * size.x + std::abs(tangent.y) * size.y) * 0.5f;
    const auto offset = point - object.position;
    const float forward = offset.dot(normal);
    if (forward < depth + radius - SpatialConfig::CollisionSkin) return false;
    const float crossing = offset.dot(tangent) * depth / std::max(depth, forward);
    return std::abs(crossing) + radius <= width + SpatialConfig::CollisionSkin;
  }
  inline sf::Vector2f HideoutFrontPosition(const Interactable &object, float radius) {
    const auto normal = HideoutFrontNormal(object);
    const auto size = ObjectFootprint(object).size;
    const float depth = (std::abs(normal.x) * size.x + std::abs(normal.y) * size.y) * 0.5f;
    return object.position + normal * (depth + radius + SpatialConfig::CollisionSkin);
  }
  template <typename Visit>
  void ForEachSolid(const WorldState &world, Visit visit) {
    for (const auto &obstacle : world.obstacles) if (BlocksMovement(obstacle)) visit(obstacle.bounds);
    for (const auto &object : world.objects) if (BlocksMovement(object)) visit(ObjectFootprint(object));
  }
  inline bool PositionBlocked(const WorldState &world, sf::Vector2f position, float radius) {
    if (!std::isfinite(position.x) || !std::isfinite(position.y) || position.x < radius || position.y < radius || position.x > world.size.x - radius || position.y > world.size.y - radius) return true;
    bool blocked = false;
    ForEachSolid(world, [&](const sf::FloatRect &bounds) { blocked |= CircleOverlaps(position, radius, bounds); });
    return blocked;
  }
  inline bool ClearMovementSegment(const WorldState &world, sf::Vector2f from, sf::Vector2f to, float radius) {
    const auto displacement = to - from;
    const float step = std::max(SpatialConfig::CollisionSkin, std::min(SpatialConfig::MaximumMovementStep, radius * 0.5f));
    const int count = std::max(1, static_cast<int>(std::ceil(displacement.length() / step)));
    for (int index = 0; index <= count; ++index) if (PositionBlocked(world, from + displacement * (static_cast<float>(index) / count), radius)) return false;
    return true;
  }
  inline bool CoverPeekPosition(const AgentCover &cover, PeekSide side, sf::Vector2f &target) {
    if (!cover.active || side == PeekSide::None) return false;
    const sf::Vector2f tangent{-cover.normal.y, cover.normal.x};
    const auto minimum = cover.surface.position, maximum = cover.surface.position + cover.surface.size;
    const std::array<sf::Vector2f, 4> corners{{minimum, maximum, {minimum.x, maximum.y}, {maximum.x, minimum.y}}};
    float edge = corners.front().dot(tangent);
    for (const auto corner : corners) edge = side == PeekSide::Left ? std::max(edge, corner.dot(tangent)) : std::min(edge, corner.dot(tangent));
    const float anchor = cover.anchor.dot(tangent), direction = side == PeekSide::Left ? 1.f : -1.f;
    if (std::abs(edge - anchor) > SpatialConfig::CoverPeekEdgeReach) return false;
    target = cover.anchor + tangent * (edge - anchor + direction * SpatialConfig::CoverPeekClearance);
    return true;
  }
}
