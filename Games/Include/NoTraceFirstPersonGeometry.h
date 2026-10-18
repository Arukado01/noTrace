#pragma once

#include "NoTraceCombatGeometry.h"
#include "NoTraceSpatialGeometry.h"
#include "NoTraceVisibility.h"
#include "Types/NoTraceFirstPersonTypes.h"
#include <limits>
#include <numbers>
#include <optional>
#include <vector>

namespace NoTrace::FirstPersonGeometry {
  struct Prism {
    sf::FloatRect bounds;
    float bottom = 0.f;
    float top = 0.f;
    sf::Vector2f slope;
    unsigned int material = 0;
    std::uint32_t object = 0;
    bool visible = true;
    bool blocksAim = true;
  };
  struct Scene { std::vector<Prism> prisms; };
  struct ColumnIntersection {
    const Prism *prism = nullptr;
    float near = 0.f;
    float far = FirstPersonConfig::RenderDistance;
    sf::Vector3f normal;
  };
  struct RayHit {
    float distance = FirstPersonConfig::RenderDistance;
    sf::Vector3f point;
    sf::Vector3f normal;
    const Prism *prism = nullptr;
  };
  struct PreparedColumn {
    ColumnIntersection intersection;
    float height = 0.f;
    float bottom = 0.f;
    float top = 0.f;
    float slope = 0.f;
  };
  struct PreparedPrism {
    const Prism *prism = nullptr;
    float minimumX = 0.f;
    float maximumX = 0.f;
    float minimumY = 0.f;
    float maximumY = 0.f;
    float height = 0.f;
    float bottom = 0.f;
    float top = 0.f;
    bool insideX = false;
    bool insideY = false;
  };
  struct DepthHit {
    float distance = FirstPersonConfig::RenderDistance;
    float normalX = 0.f;
    float normalY = 0.f;
    float normalZ = 0.f;
    const Prism *prism = nullptr;
  };

  inline FirstPersonCamera Normalize(FirstPersonCamera camera) {
    camera.yaw = std::isfinite(camera.yaw) ? std::remainder(camera.yaw, 2.f * std::numbers::pi_v<float>) : 0.f;
    camera.pitch = std::isfinite(camera.pitch) ? std::clamp(camera.pitch, -FirstPersonConfig::MaximumPitch, FirstPersonConfig::MaximumPitch) : 0.f;
    camera.fieldOfView = std::isfinite(camera.fieldOfView) ? std::clamp(camera.fieldOfView, FirstPersonConfig::MinimumFov, FirstPersonConfig::MaximumFov) : FirstPersonConfig::DefaultFov;
    return camera;
  }
  inline sf::Vector2f Forward(const FirstPersonCamera &camera) {
    const auto normalized = Normalize(camera);
    return {std::cos(normalized.yaw), std::sin(normalized.yaw)};
  }
  inline sf::Vector2f Right(const FirstPersonCamera &camera) {
    const auto forward = Forward(camera);
    return {-forward.y, forward.x};
  }
  inline float FocalLength(const FirstPersonCamera &camera, float width) {
    return width * .5f / std::tan(Normalize(camera).fieldOfView * std::numbers::pi_v<float> / 360.f);
  }
  inline sf::Vector3f CameraRay(const FirstPersonCamera &camera, sf::Vector2f pixel, sf::Vector2f size) {
    const auto forward = Forward(camera), right = Right(camera);
    const float focal = FocalLength(camera, size.x);
    const auto xy = forward + right * ((pixel.x - size.x * .5f) / focal);
    return {xy.x, xy.y, std::tan(Normalize(camera).pitch) + (size.y * .5f - pixel.y) / focal};
  }
  inline sf::Vector3f Eye(const Agent &agent) {
    return {agent.position.x, agent.position.y, agent.elevation + CombatGeometry::HeadCenterHeight(agent.stance)};
  }
  inline unsigned int Material(SurfaceMaterial material) {
    return static_cast<unsigned int>(material);
  }

  inline Scene BuildScene(const WorldState &world) {
    Scene scene;
    scene.prisms.reserve(world.obstacles.size() + world.walkSurfaces.size() + world.objects.size() * 3 + 5);
    const auto add = [&](sf::FloatRect bounds, float bottom, float top, unsigned int material, std::uint32_t object = 0, sf::Vector2f slope = {}, bool visible = true, bool blocksAim = true) {
      if (bounds.size.x > 0.f && bounds.size.y > 0.f && top > bottom) scene.prisms.push_back({bounds, bottom, top, slope, material, object, visible, blocksAim});
    };
    const auto glass = [&](sf::FloatRect bounds, float bottom, float top, std::uint32_t object, bool blocksAim) {
      add(bounds, bottom, top, 7, object, {}, false, blocksAim);
      const bool alongX = bounds.size.x >= bounds.size.y;
      const auto longAxis = alongX ? sf::Vector2f{bounds.size.x, 0.f} : sf::Vector2f{0.f, bounds.size.y};
      const sf::Vector2f post = alongX ? sf::Vector2f{2.f, bounds.size.y} : sf::Vector2f{bounds.size.x, 2.f};
      add({bounds.position, post}, bottom, top, 1, object, {}, true, blocksAim);
      add({bounds.position + longAxis - (alongX ? sf::Vector2f{2.f, 0.f} : sf::Vector2f{0.f, 2.f}), post}, bottom, top, 1, object, {}, true, blocksAim);
      add(bounds, top - 2.f, top, 1, object, {}, true, blocksAim);
    };
    if (world.walkSurfaces.empty()) add({{}, world.size}, -6.f, 0.f, 3);
    else for (const auto &surface : world.walkSurfaces) if (surface.enabled)
      add(surface.bounds, surface.elevation - surface.thickness, surface.elevation, surface.kind == WalkSurfaceKind::Stair ? 11u : Material(surface.material), surface.id, surface.slope);
    for (const auto &wall : world.obstacles) {
      const float top = wall.elevation + WorldGeometry::ObstacleHeight(wall);
      if (!wall.blocksVision) glass(wall.bounds, wall.elevation, top, 0, wall.solid);
      else add(wall.bounds, wall.elevation, top, wall.cover == CoverKind::Low ? 6u : 5u, 0, {}, true, wall.solid);
    }
    for (const auto &object : world.objects) {
      if (object.kind == ObjectKind::Extraction || object.kind == ObjectKind::Deployment || (object.kind == ObjectKind::Explosive && (object.trap.state == TrapState::Armed || object.trap.state == TrapState::Detonated))) continue;
      const auto bounds = WorldGeometry::ObjectFootprint(object);
      const auto properties = WorldGeometry::Properties(object);
      const float top = object.elevation + WorldGeometry::ObjectHeight(object);
      const unsigned int material = object.propStyle == PropStyle::Server || object.propStyle == PropStyle::Console || object.kind == ObjectKind::Computer ? 9u :
          object.propStyle == PropStyle::Medical ? 12u : object.kind == ObjectKind::Door || object.propStyle == PropStyle::SecurityDoor ? 8u :
          object.propStyle == PropStyle::Lever || object.propStyle == PropStyle::Buttons || object.kind == ObjectKind::PuzzleNode ? 10u :
          object.propStyle == PropStyle::Glass ? 7u : object.propStyle == PropStyle::SafetyLamp ? 15u : Material(WorldGeometry::ObjectMaterial(object));
      if (object.kind == ObjectKind::Door && object.open) continue;
      if (object.propStyle == PropStyle::Elevator && !properties.solid) continue;
      if (WorldGeometry::HideoutType(object) == HideoutKind::UnderTable && !object.overturned) {
        const float leg = SpatialConfig::UnderTableTopThickness;
        add(bounds, top - leg, top, material, object.id);
        const auto end = bounds.position + bounds.size;
        for (const auto corner : {bounds.position, sf::Vector2f{end.x - leg, bounds.position.y}, sf::Vector2f{bounds.position.x, end.y - leg}, end - sf::Vector2f{leg, leg}})
          add({corner, {leg, leg}}, object.elevation, top - leg, material, object.id);
      } else if (object.propStyle == PropStyle::Glass) {
        glass(bounds, object.elevation, top, object.id, properties.solid);
      } else add(bounds, object.elevation, top, material, object.id, {}, true, properties.solid || properties.blocksVision);
    }
    return scene;
  }

  inline bool AgentVisible(const WorldState &world, const Agent &observer, const Agent &target) {
    return target.connected && !target.hidden && !target.extracted && (world.phase != GamePhase::Mission ||
        (world.visibility.authoritative && world.visibility.observer == observer.id) || Visibility::IsGeometricallyVisible(world, observer, target.position, target.stance, target.elevation));
  }

  inline std::optional<ColumnIntersection> IntersectColumn(const Prism &prism, sf::Vector3f origin, sf::Vector2f direction, float limit = FirstPersonConfig::RenderDistance) {
    ColumnIntersection result{&prism, FirstPersonConfig::NearPlane, limit, {}};
    const auto axis = [&](float from, float delta, float minimum, float maximum, sf::Vector3f normal) {
      if (std::abs(delta) < .000001f) return from >= minimum && from <= maximum;
      float first = (minimum - from) / delta, second = (maximum - from) / delta;
      if (first > second) { std::swap(first, second); normal = -normal; }
      if (first > result.near) { result.near = first; result.normal = normal; }
      result.far = std::min(result.far, second);
      return result.near <= result.far;
    };
    if (!axis(origin.x, direction.x, prism.bounds.position.x, prism.bounds.position.x + prism.bounds.size.x, {-1.f, 0.f, 0.f}) ||
        !axis(origin.y, direction.y, prism.bounds.position.y, prism.bounds.position.y + prism.bounds.size.y, {0.f, -1.f, 0.f})) return std::nullopt;
    return result;
  }
  inline std::optional<RayHit> IntersectRow(const ColumnIntersection &column, sf::Vector3f origin, sf::Vector3f direction, float limit = FirstPersonConfig::RenderDistance) {
    const auto &prism = *column.prism;
    float near = column.near, far = std::min(column.far, limit);
    auto normal = column.normal;
    const float height = origin.z - (sf::Vector2f{origin.x, origin.y} - prism.bounds.position).dot(prism.slope);
    const float delta = direction.z - sf::Vector2f{direction.x, direction.y}.dot(prism.slope);
    if (std::abs(delta) < .000001f) { if (height < prism.bottom || height > prism.top) return std::nullopt; }
    else {
      float first = (prism.bottom - height) / delta, second = (prism.top - height) / delta;
      sf::Vector3f plane{prism.slope.x, prism.slope.y, -1.f};
      if (first > second) { std::swap(first, second); plane = -plane; }
      if (first > near) { near = first; normal = plane; }
      far = std::min(far, second);
    }
    if (near > far || far < FirstPersonConfig::NearPlane) return std::nullopt;
    return RayHit{near, origin + direction * near, normal, column.prism};
  }
  inline std::optional<RayHit> Raycast(const Scene &scene, sf::Vector3f origin, sf::Vector3f direction, float limit = FirstPersonConfig::RenderDistance, bool aimOnly = false) {
    std::optional<RayHit> result;
    for (const auto &prism : scene.prisms) {
      if (aimOnly ? !prism.blocksAim : !prism.visible) continue;
      const auto column = IntersectColumn(prism, origin, {direction.x, direction.y}, result ? result->distance : limit);
      if (!column) continue;
      const auto hit = IntersectRow(*column, origin, direction, result ? result->distance : limit);
      if (hit && (!result || hit->distance < result->distance)) result = hit;
    }
    return result;
  }

  inline PreparedColumn PrepareColumn(const ColumnIntersection &column, sf::Vector3f origin, sf::Vector2f direction) {
    const auto &prism = *column.prism;
    const float height = origin.z - (sf::Vector2f{origin.x, origin.y} - prism.bounds.position).dot(prism.slope);
    return {column, height, prism.bottom - height, prism.top - height, direction.dot(prism.slope)};
  }
  inline PreparedPrism PreparePrism(const Prism &prism, sf::Vector3f origin) {
    const float minimumX = prism.bounds.position.x, maximumX = minimumX + prism.bounds.size.x;
    const float minimumY = prism.bounds.position.y, maximumY = minimumY + prism.bounds.size.y;
    const float height = origin.z - ((origin.x - minimumX) * prism.slope.x + (origin.y - minimumY) * prism.slope.y);
    return {&prism, minimumX - origin.x, maximumX - origin.x, minimumY - origin.y, maximumY - origin.y,
      height, prism.bottom - height, prism.top - height, origin.x >= minimumX && origin.x <= maximumX, origin.y >= minimumY && origin.y <= maximumY};
  }
  inline bool IntersectPreparedColumn(const PreparedPrism &prepared, float directionX, float directionY, PreparedColumn &result, float limit = FirstPersonConfig::RenderDistance) {
    float near = FirstPersonConfig::NearPlane, far = limit;
    float normalX = 0.f, normalY = 0.f, normalZ = 0.f;
    if (std::abs(directionX) < .000001f) { if (!prepared.insideX) return false; }
    else {
      float first = prepared.minimumX / directionX, second = prepared.maximumX / directionX;
      float normal = -1.f;
      if (first > second) { std::swap(first, second); normal = 1.f; }
      if (first > near) { near = first; normalX = normal; normalY = normal > 0.f ? -0.f : 0.f; normalZ = normalY; }
      far = std::min(far, second);
      if (!(near <= far)) return false;
    }
    if (std::abs(directionY) < .000001f) { if (!prepared.insideY) return false; }
    else {
      float first = prepared.minimumY / directionY, second = prepared.maximumY / directionY;
      float normal = -1.f;
      if (first > second) { std::swap(first, second); normal = 1.f; }
      if (first > near) { near = first; normalY = normal; normalX = normal > 0.f ? -0.f : 0.f; normalZ = normalX; }
      far = std::min(far, second);
      if (!(near <= far)) return false;
    }
    result = {{prepared.prism, near, far, {normalX, normalY, normalZ}}, prepared.height, prepared.bottom, prepared.top,
      directionX * prepared.prism->slope.x + directionY * prepared.prism->slope.y};
    return true;
  }
  inline bool NearestPreparedRow(const PreparedColumn &prepared, float directionZ, DepthHit &nearest) {
    const auto &column = prepared.intersection;
    const auto &prism = *column.prism;
    float near = column.near, far = std::min(column.far, nearest.distance);
    float normalX = column.normal.x, normalY = column.normal.y, normalZ = column.normal.z;
    const float delta = directionZ - prepared.slope;
    if (std::abs(delta) < .000001f) { if (prepared.height < prism.bottom || prepared.height > prism.top) return false; }
    else {
      float first = prepared.bottom / delta, second = prepared.top / delta;
      const bool reversed = first > second;
      if (reversed) std::swap(first, second);
      if (first > near) {
        near = first;
        normalX = reversed ? -prism.slope.x : prism.slope.x;
        normalY = reversed ? -prism.slope.y : prism.slope.y;
        normalZ = reversed ? 1.f : -1.f;
      }
      far = std::min(far, second);
    }
    if (near > far || far < FirstPersonConfig::NearPlane || (nearest.prism && !(near < nearest.distance))) return false;
    nearest = {near, normalX, normalY, normalZ, column.prism};
    return true;
  }
  inline std::optional<RayHit> IntersectPreparedRow(const PreparedColumn &prepared, sf::Vector3f origin, sf::Vector3f direction, float limit = FirstPersonConfig::RenderDistance) {
    DepthHit hit{limit};
    if (!NearestPreparedRow(prepared, direction.z, hit)) return std::nullopt;
    return RayHit{hit.distance, origin + direction * hit.distance, {hit.normalX, hit.normalY, hit.normalZ}, hit.prism};
  }

  inline CombatGeometry::AimSolution ResolveAim(const WorldState &world, const Agent &agent, const FirstPersonCamera &camera) {
    const auto direction = Forward(camera);
    const auto origin = Eye(agent);
    const float slope = std::tan(Normalize(camera).pitch);
    const auto scene = BuildScene(world);
    const auto geometry = Raycast(scene, origin, {direction.x, direction.y, slope}, GameplayConfig::WeaponRange, true);
    float distance = geometry ? geometry->distance : GameplayConfig::WeaponRange;
    const auto actor = [&](sf::Vector2f position, sf::Vector2f facing, Stance stance, bool down, float elevation) {
      const auto hit = CombatGeometry::IntersectActor(agent.position, direction, origin.z, slope, distance, position, facing, stance, down, elevation);
      if (hit) distance = hit->distance;
    };
    for (const auto &guard : world.guards) if (!guard.bodyHidden && guard.storedIn == 0 && Visibility::GuardVisible(world, agent, guard))
      actor(guard.position, guard.facing, Stance::Standing, guard.state == GuardState::Incapacitated || guard.state == GuardState::Dead, guard.elevation);
    for (const auto &other : world.agents) if (other.id != agent.id && AgentVisible(world, agent, other))
      actor(other.position, other.facing, other.stance, other.incapacitated || !other.alive, other.elevation);
    distance = std::clamp(distance, 1.f, GameplayConfig::WeaponRange);
    const float targetHeight = origin.z + slope * distance;
    const float weaponHeight = agent.elevation + CombatGeometry::WeaponHeight(agent.stance);
    return {agent.position + direction * distance, std::clamp((targetHeight - weaponHeight) / distance, -CombatConfig::MaximumAimSlope, CombatConfig::MaximumAimSlope)};
  }
}
