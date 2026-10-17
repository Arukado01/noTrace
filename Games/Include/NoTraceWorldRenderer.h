#pragma once

#include "Types/NoTraceGameplayTypes.h"
#include "Config/NoTracePresentationConfig.h"
#include <SFML/Graphics/Color.hpp>
#include <SFML/Graphics/View.hpp>
#include <algorithm>
#include <cmath>
#include <initializer_list>

struct EngineContext;
namespace sf { class Font; }

namespace NoTrace {
  class WorldRenderer {
  private:
    EngineContext &ctx_;
    const sf::Font *font_;
    float zoom_ = PresentationConfig::DefaultZoom;

    static sf::Vector2f SnapToPixels(sf::Vector2f value) {
      return {std::round(value.x / PresentationConfig::PixelSize) * PresentationConfig::PixelSize,
              std::round(value.y / PresentationConfig::PixelSize) * PresentationConfig::PixelSize};
    }

    static sf::Vector2f LogicalCenter(sf::Vector2f logicalSize) {
      return SnapToPixels(logicalSize.componentWiseMul(PresentationConfig::CameraFocusRatio));
    }

    sf::Vector2f CameraGround(const WorldState &world, PlayerId localPlayer) const;
    sf::Vector2f GroundToScreen(sf::Vector2f ground, sf::Vector2f camera) const;
    void Polygon(std::initializer_list<sf::Vector2f> points, sf::Color color) const;
    void PixelRect(sf::Vector2f position, sf::Vector2f size, sf::Color color) const;
    void PixelLine(sf::Vector2f from, sf::Vector2f to, sf::Color color, float thickness = 2.f) const;
    void Label(const std::string &text, sf::Vector2f position, sf::Color color, unsigned int size = PresentationConfig::WorldLabelSize) const;
    void Box(sf::FloatRect bounds, float height, sf::Vector2f camera, sf::Color top, sf::Color left, sf::Color right) const;
    void Floor(const WorldState &world, sf::Vector2f camera) const;
    void ObstacleArt(const Obstacle &obstacle, sf::Vector2f camera) const;
    void ObjectArt(const Interactable &object, sf::Vector2f camera, bool nearby) const;
    void Humanoid(sf::Vector2f feet, sf::Vector2f facing, Stance stance, sf::Color uniform,
                  float animation, bool moving, bool running, bool down, bool grappling) const;
    void AgentArt(const Agent &agent, const WorldState &world, sf::Vector2f camera, bool local) const;
    void GuardArt(const Guard &guard, const WorldState &world, sf::Vector2f camera) const;
    void Vision(const Guard &guard, const WorldState &world, sf::Vector2f camera) const;
    void FieldOfView(sf::Vector2f position, sf::Vector2f facing, float range, sf::Color color, const WorldState &world, sf::Vector2f camera) const;
    void Noise(const NoiseEvent &noise, sf::Vector2f camera) const;
    void Combat(const WorldState &world, sf::Vector2f camera) const;
    void Minimap(const WorldState &world, PlayerId localPlayer) const;

  public:
    explicit WorldRenderer(EngineContext &context);
    void AdjustZoom(float steps) { zoom_ = ZoomAfterSteps(zoom_, steps); }
    float GetZoom() const { return zoom_; }
    void ResetZoom() { zoom_ = PresentationConfig::DefaultZoom; }
    void Render(const WorldState &world, PlayerId localPlayer) const;
    sf::Vector2f ScreenToGround(sf::Vector2f logical, const WorldState &world, PlayerId localPlayer) const;
    sf::Vector2f AimToGround(sf::Vector2f logical, const WorldState &world, PlayerId localPlayer) const;
    static float ClampZoom(float zoom) {
      return std::isfinite(zoom) ? std::clamp(zoom, PresentationConfig::MinimumZoom, PresentationConfig::MaximumZoom) : PresentationConfig::DefaultZoom;
    }
    static float ZoomAfterSteps(float zoom, float steps) {
      zoom = ClampZoom(zoom);
      if (!std::isfinite(steps)) return zoom;
      const float boundedSteps = std::clamp(steps, -100.f, 100.f);
      return ClampZoom(zoom * std::pow(PresentationConfig::ZoomFactorPerStep, boundedSteps));
    }
    static sf::View WorldView(sf::Vector2f logicalSize, float zoom) {
      zoom = ClampZoom(zoom);
      const auto focus = LogicalCenter(logicalSize);
      return sf::View(sf::FloatRect(focus - focus / zoom, logicalSize / zoom));
    }
    static sf::Vector2f ProjectGround(sf::Vector2f ground, sf::Vector2f camera, sf::Vector2f logicalSize, float zoom = PresentationConfig::DefaultZoom) {
      const auto delta = ground - camera;
      const auto focus = LogicalCenter(logicalSize);
      const auto point = SnapToPixels(focus + sf::Vector2f{(delta.x - delta.y) * PresentationConfig::GroundProjection.x,
                                                         (delta.x + delta.y) * PresentationConfig::GroundProjection.y});
      return focus + (point - focus) * ClampZoom(zoom);
    }
    static sf::Vector2f UnprojectGround(sf::Vector2f logical, sf::Vector2f camera, sf::Vector2f logicalSize, float zoom = PresentationConfig::DefaultZoom) {
      const auto delta = (logical - LogicalCenter(logicalSize)) / ClampZoom(zoom);
      const float horizontal = delta.x / PresentationConfig::GroundProjection.x;
      const float vertical = delta.y / PresentationConfig::GroundProjection.y;
      return camera + sf::Vector2f{(horizontal + vertical) * 0.5f, (vertical - horizontal) * 0.5f};
    }
    static sf::FloatRect ObjectFootprint(const Interactable &object) {
      const auto size = PresentationConfig::ObjectGroundSizes[static_cast<std::size_t>(object.kind)];
      return {object.position - size * 0.5f, size};
    }
    static float WeaponHeight(Stance stance) {
      if (stance == Stance::Prone) return PresentationConfig::ProneShotHeight;
      if (stance == Stance::Crouched) return PresentationConfig::CrouchedShotHeight;
      return PresentationConfig::ShotHeight;
    }
    static sf::Vector2f WeaponOffset(Stance stance, sf::Vector2f facing, bool barrelTip = false) {
      const float side = facing.x - facing.y >= 0.f ? 1.f : -1.f;
      const float reach = stance == Stance::Prone ? PresentationConfig::ProneWeaponOffset : PresentationConfig::StandingWeaponOffset;
      sf::Vector2f offset{reach * side, -WeaponHeight(stance)};
      if (barrelTip) {
        const sf::Vector2f aim{facing.x - facing.y, (facing.x + facing.y) * 0.5f};
        const float length = stance == Stance::Prone ? PresentationConfig::ProneBarrelLength : PresentationConfig::StandingBarrelLength;
        offset += aim * (length / std::max(std::hypot(aim.x, aim.y), 0.01f));
      }
      return offset;
    }
    static Stance ShotStance(const ShotTrace &shot, const WorldState &world) {
      const auto agent = std::find_if(world.agents.begin(), world.agents.end(), [&shot](const Agent &candidate) { return candidate.id == shot.actor; });
      if (shot.actor == 0 || agent == world.agents.end()) return Stance::Standing;
      return !agent->alive || agent->incapacitated ? Stance::Prone : agent->stance;
    }
    static float ShotImpactHeight(const ShotTrace &shot, const WorldState &world) {
      float height = WeaponHeight(ShotStance(shot, world));
      if (!shot.hit) return height;
      float nearest = PresentationConfig::ShotTargetTolerance * PresentationConfig::ShotTargetTolerance;
      const auto consider = [&](sf::Vector2f position, Stance stance) {
        const float distance = (position - shot.to).lengthSquared();
        if (distance < nearest) { nearest = distance; height = WeaponHeight(stance); }
      };
      for (const auto &guard : world.guards) {
        if (guard.bodyHidden || (shot.actor == 0 && guard.id == shot.guardId)) continue;
        const bool down = guard.state == GuardState::Dead || guard.state == GuardState::Incapacitated || guard.cqcState == CqcState::KnockedDown || guard.cqcState == CqcState::Restrained;
        consider(guard.position, down ? Stance::Prone : Stance::Standing);
      }
      for (const auto &agent : world.agents) {
        if (agent.id == shot.actor || agent.extracted || !agent.connected) continue;
        consider(agent.position, !agent.alive || agent.incapacitated ? Stance::Prone : agent.stance);
      }
      return height;
    }
    static sf::Vector2f ResolveAimGround(sf::Vector2f logical, const WorldState &world, PlayerId localPlayer, sf::Vector2f logicalSize, float zoom) {
      zoom = ClampZoom(zoom);
      const auto local = std::find_if(world.agents.begin(), world.agents.end(), [localPlayer](const Agent &agent) { return agent.id == localPlayer; });
      const auto camera = local == world.agents.end() ? world.size * 0.5f : local->position;
      std::vector<sf::Vector2f> candidates;
      std::vector<sf::FloatRect> footprints;
      const auto consider = [&](sf::Vector2f position, Stance stance) {
        const auto feet = ProjectGround(position, camera, logicalSize, zoom);
        const float height = stance == Stance::Crouched ? PresentationConfig::CrouchedHeight : PresentationConfig::ActorHeight;
        const sf::FloatRect silhouette = stance == Stance::Prone
            ? sf::FloatRect(feet + PresentationConfig::ProneSilhouetteOffset * zoom, PresentationConfig::ProneSilhouetteSize * zoom)
            : sf::FloatRect(feet - sf::Vector2f{PresentationConfig::ActorHalfWidth, height} * zoom,
                            sf::Vector2f{PresentationConfig::ActorHalfWidth * 2.f, height + PresentationConfig::PixelSize} * zoom);
        if (silhouette.contains(logical)) {
          candidates.push_back(position);
          footprints.push_back({position, {0.f, 0.f}});
        }
      };
      for (const auto &guard : world.guards) {
        if (guard.bodyHidden || guard.state == GuardState::Dead || guard.state == GuardState::Incapacitated) continue;
        const bool down = guard.cqcState == CqcState::KnockedDown || guard.cqcState == CqcState::Restrained;
        consider(guard.position, down ? Stance::Prone : Stance::Standing);
      }
      for (const auto &agent : world.agents) {
        if (agent.id == localPlayer || !agent.alive || agent.incapacitated || agent.extracted || !agent.connected || agent.hidden) continue;
        consider(agent.position, agent.stance);
      }
      if (!candidates.empty()) return candidates[GroundDrawOrder(footprints).back()];
      const float height = WeaponHeight(local == world.agents.end() ? Stance::Standing : local->stance);
      return UnprojectGround(logical + sf::Vector2f{0.f, height * zoom}, camera, logicalSize, zoom);
    }
    static std::vector<std::size_t> GroundDrawOrder(const std::vector<sf::FloatRect> &footprints) {
      const auto count = footprints.size();
      std::vector<std::vector<std::size_t>> successors(count);
      std::vector<std::size_t> predecessors(count, 0);
      const auto behind = [](const sf::FloatRect &first, const sf::FloatRect &second) {
        const auto end = first.position + first.size;
        return end.x <= second.position.x || end.y <= second.position.y;
      };
      for (std::size_t first = 0; first < count; ++first) for (std::size_t second = first + 1; second < count; ++second) {
        const bool firstBehind = behind(footprints[first], footprints[second]);
        const bool secondBehind = behind(footprints[second], footprints[first]);
        if (firstBehind == secondBehind) continue;
        const auto back = firstBehind ? first : second;
        const auto front = firstBehind ? second : first;
        successors[back].push_back(front);
        ++predecessors[front];
      }
      const auto depth = [&footprints](std::size_t index) {
        const auto center = footprints[index].position + footprints[index].size * 0.5f;
        return center.x + center.y;
      };
      std::vector<bool> emitted(count, false);
      std::vector<std::size_t> order;
      order.reserve(count);
      while (order.size() < count) {
        auto next = count;
        for (std::size_t index = 0; index < count; ++index) {
          if (emitted[index] || predecessors[index] != 0) continue;
          if (next == count || depth(index) < depth(next)) next = index;
        }
        // Las huellas que se cruzan pueden formar ciclos; el centro conserva un desempate estable.
        if (next == count) for (std::size_t index = 0; index < count; ++index) {
          if (!emitted[index] && (next == count || depth(index) < depth(next))) next = index;
        }
        emitted[next] = true;
        order.push_back(next);
        for (const auto front : successors[next]) if (predecessors[front] > 0) --predecessors[front];
      }
      return order;
    }
  };
}
