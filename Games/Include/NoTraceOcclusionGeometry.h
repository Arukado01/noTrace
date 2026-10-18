#pragma once

#include "NoTraceSpatialGeometry.h"
#include <vector>

namespace NoTrace::WorldGeometry {
  class PreparedOccluders {
    struct Volume {
      sf::FloatRect bounds;
      float bottom = 0.f;
      float top = 0.f;
      sf::Vector2f slope;
      float elevation = 0.f;
      std::uint32_t object = 0;
      bool objectVolume = false;
      bool surface = false;
    };
    std::vector<Volume> volumes_;

    static bool Blocked(const Volume &volume, sf::Vector3f from, sf::Vector3f to, std::uint32_t ignoredObject) {
      if (volume.objectVolume && volume.object == ignoredObject) return false;
      if (volume.surface) {
        from.z -= volume.elevation + (Ground(from) - volume.bounds.position).dot(volume.slope);
        to.z -= volume.elevation + (Ground(to) - volume.bounds.position).dot(volume.slope);
      }
      float intersection = 1.f;
      return SegmentPrism(from, to, volume.bounds, volume.bottom, volume.top, &intersection) && intersection < 1.f - .0001f;
    }

  public:
    explicit PreparedOccluders(const WorldState &world) {
      volumes_.reserve(world.obstacles.size() + world.objects.size() + world.walkSurfaces.size() + 16);
      const auto add = [&](sf::FloatRect bounds, float bottom, float top, std::uint32_t object = 0, bool objectVolume = false) {
        if (top > bottom) volumes_.push_back({bounds, bottom, top, {}, 0.f, object, objectVolume});
      };
      for (const auto &obstacle : world.obstacles) if (obstacle.blocksVision)
        add(obstacle.bounds, obstacle.elevation, obstacle.elevation + ObstacleHeight(obstacle));
      for (const auto &object : world.objects) if (Properties(object).blocksVision) {
        const auto bounds = ObjectFootprint(object);
        const float top = object.elevation + ObjectHeight(object);
        if (HideoutType(object) != HideoutKind::UnderTable) { add(bounds, object.elevation, top, object.id, true); continue; }
        add(bounds, top - SpatialConfig::UnderTableTopThickness, top, object.id, true);
        const auto end = bounds.position + bounds.size;
        const float leg = SpatialConfig::UnderTableTopThickness;
        for (const auto corner : {bounds.position, sf::Vector2f{end.x - leg, bounds.position.y}, sf::Vector2f{bounds.position.x, end.y - leg}, end - sf::Vector2f{leg, leg}})
          add({corner, {leg, leg}}, object.elevation, top - leg, object.id, true);
      }
      for (const auto &surface : world.walkSurfaces) if (surface.enabled)
        volumes_.push_back({surface.bounds, -surface.thickness, 0.f, surface.slope, surface.elevation, 0, false, true});
    }

    bool Occluded(sf::Vector3f from, sf::Vector3f to, std::uint32_t ignoredObject = 0) const {
      for (const auto &volume : volumes_) if (Blocked(volume, from, to, ignoredObject)) return true;
      return false;
    }

    class Query {
      const PreparedOccluders &geometry_;
      std::vector<std::size_t> candidates_;
    public:
      explicit Query(const PreparedOccluders &geometry) : geometry_(geometry) { candidates_.reserve(geometry.volumes_.size()); }
      void Select(sf::Vector2f from, sf::Vector2f to) {
        candidates_.clear();
        const sf::Vector2f skin{SpatialConfig::CollisionSkin, SpatialConfig::CollisionSkin};
        for (std::size_t index = 0; index < geometry_.volumes_.size(); ++index) {
          const auto &bounds = geometry_.volumes_[index].bounds;
          // El filtro conserva margen subpíxel; la intersección final usa los límites físicos originales.
          if (SegmentRect(from, to, {bounds.position - skin, bounds.size + skin * 2.f})) candidates_.push_back(index);
        }
      }
      bool Occluded(sf::Vector3f from, sf::Vector3f to, std::uint32_t ignoredObject = 0) const {
        for (const auto index : candidates_) if (Blocked(geometry_.volumes_[index], from, to, ignoredObject)) return true;
        return false;
      }
    };
  };
}
