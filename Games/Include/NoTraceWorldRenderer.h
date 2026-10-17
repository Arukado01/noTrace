#pragma once

#include "Types/NoTraceGameplayTypes.h"
#include "Config/NoTracePresentationConfig.h"
#include "NoTraceCombatGeometry.h"
#include "NoTraceVisibility.h"
#include "NoTraceWorldGeometry.h"
#include <SFML/Graphics/Color.hpp>
#include <SFML/Graphics/View.hpp>
#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <optional>
#include <numbers>

struct EngineContext;
namespace sf { class Font; class Texture; }

namespace NoTrace {
  class WorldRenderer {
  public:
    using AimSolution = CombatGeometry::AimSolution;
    enum class GuardIndicator { None, Suspicion, Confirmed, Combat, Unconscious };
    struct SpriteFrame { std::size_t index = 0; bool prone = false; bool mirror = false; };
    struct DetectionVisual { unsigned int segments = 0; sf::Color color; bool confirmed = false; };
    struct SpritePlacement {
      sf::Vector2f position, origin, scale;
      sf::Vector2f Map(sf::Vector2f point) const { return position + (point - origin).componentWiseMul(scale); }
    };
  private:
    EngineContext &ctx_;
    const sf::Font *font_;
    sf::Texture *actorAtlas_;
    sf::Texture *proneAtlas_;
    std::vector<sf::IntRect> actorFrames_;
    std::vector<sf::IntRect> proneFrames_;
    std::vector<sf::Vector2f> actorMuzzles_;
    std::vector<sf::Vector2f> proneMuzzles_;
    float zoom_ = PresentationConfig::DefaultZoom;
    bool decoyPreviewActive_ = false;
    sf::Vector2f decoyPreviewDirection_;
    float decoyPreviewDistance_ = 0.f;

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
    void Fog(const WorldState &world, sf::Vector2f camera, const VisibilityGrid &visibility) const;
    void ObstacleArt(const Obstacle &obstacle, sf::Vector2f camera) const;
    void ObjectArt(const Interactable &object, sf::Vector2f camera, bool nearby) const;
    SpriteFrame AgentFrame(const Agent &agent, const WorldState &world, bool local) const;
    SpriteFrame GuardFrame(const Guard &guard, const WorldState &world) const;
    std::optional<sf::Vector2f> SourceMuzzle(const WorldState &world, const ShotTrace &shot, sf::Vector2f camera, const Agent *observer) const;
    void Humanoid(sf::Vector2f feet, sf::Vector2f facing, Stance stance, sf::Color uniform, SpriteFrame frame, bool down, bool guard = false) const;
    void AgentArt(const Agent &agent, const WorldState &world, sf::Vector2f camera, bool local) const;
    void GuardArt(const Guard &guard, const WorldState &world, sf::Vector2f camera) const;
    void Vision(const Guard &guard, const WorldState &world, sf::Vector2f camera) const;
    void FieldOfView(sf::Vector2f position, sf::Vector2f facing, float range, sf::Color color, const WorldState &world, sf::Vector2f camera) const;
    void Noise(const NoiseEvent &noise, sf::Vector2f camera) const;
    void Combat(const WorldState &world, sf::Vector2f camera, const Agent *observer) const;
    void GuardIcon(const Guard &guard, sf::Vector2f camera) const;
    void DecoyPreview(const WorldState &world, sf::Vector2f camera, const Agent *observer) const;
    void Footsteps(const WorldState &world, sf::Vector2f camera, const Agent *observer) const;
    void Minimap(const WorldState &world, PlayerId localPlayer, const VisibilityGrid &visibility) const;
    void DetectionEye(const WorldState &world, const Agent *observer) const;

  public:
    explicit WorldRenderer(EngineContext &context);
    void AdjustZoom(float steps) { zoom_ = ZoomAfterSteps(zoom_, steps); }
    float GetZoom() const { return zoom_; }
    void ResetZoom() { zoom_ = PresentationConfig::DefaultZoom; }
    void SetDecoyPreview(bool active, sf::Vector2f direction, float distance) {
      decoyPreviewActive_ = active;
      decoyPreviewDirection_ = direction;
      decoyPreviewDistance_ = std::isfinite(distance) ? std::max(0.f, distance) : 0.f;
    }
    void Render(const WorldState &world, PlayerId localPlayer, bool hudVisible = true) const;
    sf::Vector2f ScreenToGround(sf::Vector2f logical, const WorldState &world, PlayerId localPlayer) const;
    sf::Vector2f AimToGround(sf::Vector2f logical, const WorldState &world, PlayerId localPlayer) const;
    AimSolution AimAt(sf::Vector2f logical, const WorldState &world, PlayerId localPlayer) const;
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
    static sf::Vector2f ProjectGround(sf::Vector2f ground, sf::Vector2f camera, sf::Vector2f logicalSize, float zoom = 1.f) {
      const auto delta = ground - camera;
      const auto focus = LogicalCenter(logicalSize);
      const auto point = SnapToPixels(focus + sf::Vector2f{(delta.x - delta.y) * PresentationConfig::GroundProjection.x,
                                                         (delta.x + delta.y) * PresentationConfig::GroundProjection.y});
      return focus + (point - focus) * ClampZoom(zoom);
    }
    static sf::Vector2f UnprojectGround(sf::Vector2f logical, sf::Vector2f camera, sf::Vector2f logicalSize, float zoom = 1.f) {
      const auto delta = (logical - LogicalCenter(logicalSize)) / ClampZoom(zoom);
      const float horizontal = delta.x / PresentationConfig::GroundProjection.x;
      const float vertical = delta.y / PresentationConfig::GroundProjection.y;
      return camera + sf::Vector2f{(horizontal + vertical) * 0.5f, (vertical - horizontal) * 0.5f};
    }
    static sf::FloatRect ObjectFootprint(const Interactable &object) {
      return WorldGeometry::ObjectFootprint(object);
    }
    static float WeaponHeight(Stance stance) {
      return CombatGeometry::WeaponHeight(stance);
    }
    static SpriteFrame SelectSprite(sf::Vector2f facing, Stance stance, float animation, bool moving, bool running, bool down, bool grappling,
        bool braced = false, bool peeking = false, bool guard = false, bool action = false) {
      const sf::Vector2f projected{(facing.x - facing.y) * PresentationConfig::GroundProjection.x, (facing.x + facing.y) * PresentationConfig::GroundProjection.y};
      constexpr float turn = 0.78539816339f;
      const int direction = (static_cast<int>(std::lround(std::atan2(projected.x, -projected.y) / turn)) + 8) % 8;
      const auto frame = static_cast<std::size_t>(std::max(0.f, animation) * (running ? PresentationConfig::RunFramesPerSecond : PresentationConfig::WalkFramesPerSecond));
      if (stance == Stance::Prone && !down) {
        const auto crawl = static_cast<std::size_t>(std::max(0.f, animation) * PresentationConfig::CrawlFramesPerSecond);
        const std::size_t column = moving || action ? 1 + crawl % 2 : 0;
        return {static_cast<std::size_t>(direction) * PresentationConfig::ProneAtlasColumns + (guard ? 3 : 0) + column, true, false};
      }
      const std::array<std::size_t, 8> agentRows{{4, 3, 2, 1, 0, 1, 6, 5}}, guardRows{{11, 10, 9, 8, 7, 8, 9, 10}};
      const bool mirror = guard ? direction >= 5 : direction == 5;
      std::size_t column = moving ? 1 + frame % 4 : 0;
      if (stance == Stance::Crouched) column = moving ? 6 + static_cast<std::size_t>(std::max(0.f, animation) * PresentationConfig::CrouchFramesPerSecond) % 2 : 5;
      if (braced) column = peeking ? 13 : 12;
      if (grappling || action) column = 15;
      if (down) column = 11;
      return {(guard ? guardRows[direction] : agentRows[direction]) * PresentationConfig::ActorAtlasColumns + column, false, mirror};
    }
    static sf::FloatRect SpriteBounds(sf::Vector2f facing, Stance stance, bool down) {
      float left = 0.f, right = 0.f, top = 0.f, bottom = 0.f;
      const auto include = [&](sf::Vector2f ground, float height) {
        const sf::Vector2f point{(ground.x - ground.y) * PresentationConfig::GroundProjection.x, (ground.x + ground.y) * PresentationConfig::GroundProjection.y - height};
        left = std::min(left, point.x); right = std::max(right, point.x); top = std::min(top, point.y); bottom = std::max(bottom, point.y);
      };
      for (const auto &volume : CombatGeometry::ActorVolumes({}, facing, stance, down)) {
        const sf::Vector2f side{-volume.forward.y, volume.forward.x};
        for (const float x : {-1.f, 1.f}) for (const float y : {-1.f, 1.f}) {
          const auto corner = volume.center + volume.forward * volume.halfSize.x * x + side * volume.halfSize.y * y;
          include(corner, volume.bottom); include(corner, volume.top);
        }
      }
      if (!down) {
        const auto muzzle = WeaponOffset(stance, facing, true);
        left = std::min(left, muzzle.x); right = std::max(right, muzzle.x);
        top = std::min(top, muzzle.y); bottom = std::max(bottom, muzzle.y);
      }
      return {{left, top}, {std::max(1.f, right - left), std::max(1.f, bottom - top)}};
    }
    static SpritePlacement PlaceSprite(sf::Vector2f feet, sf::Vector2f facing, Stance stance, bool down, SpriteFrame frame, sf::Vector2i size) {
      const auto bounds = SpriteBounds(facing, stance, down);
      const sf::Vector2f scale{bounds.size.x / size.x, bounds.size.y / size.y};
      const sf::Vector2f origin{-bounds.position.x / scale.x, -bounds.position.y / scale.y};
      return {SnapToPixels(feet), {frame.mirror ? static_cast<float>(size.x) - origin.x : origin.x, origin.y}, {frame.mirror ? -scale.x : scale.x, scale.y}};
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
    static float ShotImpactHeight(const ShotTrace &shot, const WorldState &) { return shot.toHeight; }
    static GuardIndicator IndicatorFor(const Guard &guard) {
      if (guard.bodyHidden || guard.storedIn != 0 || guard.state == GuardState::Dead) return GuardIndicator::None;
      if (guard.state == GuardState::Incapacitated) return GuardIndicator::Unconscious;
      if (guard.detectedTime > 0.f) return GuardIndicator::Confirmed;
      if (guard.state == GuardState::Alert) return GuardIndicator::Combat;
      if (guard.state == GuardState::Suspicion || guard.state == GuardState::Investigate || guard.state == GuardState::Search) return GuardIndicator::Suspicion;
      return GuardIndicator::None;
    }
    static bool VisibleCell(const VisibilityGrid &grid, sf::Vector2f point) {
      if (grid.cellSize <= 0.f || point.x < 0.f || point.y < 0.f) return false;
      const auto column = static_cast<unsigned int>(point.x / grid.cellSize);
      const auto row = static_cast<unsigned int>(point.y / grid.cellSize);
      const auto index = static_cast<std::size_t>(row) * grid.columns + column;
      return column < grid.columns && row < grid.rows && index < grid.cells.size() && grid.cells[index] != 0;
    }
    static float FogBoundaryRadius(const VisibilityGrid &grid, float angle) {
      if (grid.visualRadii.size() != VisibilityConfig::VisualRayCount || !std::isfinite(angle)) return 0.f;
      const float turn = 2.f * std::numbers::pi_v<float>;
      const float sample = (angle - std::floor(angle / turn) * turn) * grid.visualRadii.size() / turn;
      const auto first = static_cast<std::size_t>(sample) % grid.visualRadii.size();
      const auto radius = [&grid](std::size_t index) {
        const auto count = grid.visualRadii.size();
        const float result = std::min({grid.visualRadii[(index + count - 1) % count], grid.visualRadii[index], grid.visualRadii[(index + 1) % count]});
        return std::isfinite(result) ? std::clamp(result, 0.f, VisibilityConfig::StandingRange) : 0.f;
      };
      const float fraction = sample - std::floor(sample);
      return radius(first) * (1.f - fraction) + radius((first + 1) % grid.visualRadii.size()) * fraction;
    }
    static float FogOpacity(const VisibilityGrid &grid, sf::Vector2f point) {
      const auto delta = point - grid.visualOrigin;
      const float limit = FogBoundaryRadius(grid, std::atan2(delta.y, delta.x));
      if (limit <= 0.f || delta.length() >= limit) return 1.f;
      float distance = PresentationConfig::FogFeatherWidth;
      const auto vertex = [&](std::size_t index) {
        const float angle = index * 2.f * std::numbers::pi_v<float> / grid.visualRadii.size();
        return grid.visualOrigin + sf::Vector2f{std::cos(angle), std::sin(angle)} * FogBoundaryRadius(grid, angle);
      };
      auto first = vertex(0);
      for (std::size_t index = 1; index <= grid.visualRadii.size(); ++index) {
        const auto last = vertex(index % grid.visualRadii.size());
        const auto edge = last - first;
        const float along = edge.lengthSquared() > 0.f ? std::clamp((point - first).dot(edge) / edge.lengthSquared(), 0.f, 1.f) : 0.f;
        distance = std::min(distance, (point - first - edge * along).length());
        first = last;
      }
      const float blend = std::clamp(1.f - distance / PresentationConfig::FogFeatherWidth, 0.f, 1.f);
      return blend * blend * (3.f - 2.f * blend);
    }
    static DetectionVisual DetectionFor(const Agent &agent) {
      const float progress = std::isfinite(agent.detectionProgress) ? std::clamp(agent.detectionProgress, 0.f, 1.f) : 0.f;
      const bool confirmed = agent.detectionConfirmed;
      auto color = confirmed || progress >= PresentationConfig::DetectionWarningThreshold ? PresentationConfig::Red : PresentationConfig::Amber;
      color.a = static_cast<std::uint8_t>(96.f + progress * 159.f);
      return {confirmed ? PresentationConfig::DetectionEyeSegments : static_cast<unsigned int>(std::ceil(progress * PresentationConfig::DetectionEyeSegments)), color, confirmed};
    }
    static bool FootprintVisible(const WorldState &world, const Agent &observer, sf::FloatRect footprint) {
      if (world.phase != GamePhase::Mission) return true;
      const auto end = footprint.position + footprint.size;
      const sf::Vector2f closest{std::clamp(observer.position.x, footprint.position.x, end.x), std::clamp(observer.position.y, footprint.position.y, end.y)};
      if (Visibility::IsVisible(world, observer, closest)) return true;
      for (const auto point : {footprint.position, sf::Vector2f{end.x, footprint.position.y}, end, sf::Vector2f{footprint.position.x, end.y}}) {
        if (Visibility::IsVisible(world, observer, point)) return true;
      }
      return false;
    }
    static AimSolution ResolveAim(sf::Vector2f logical, const WorldState &world, PlayerId localPlayer, sf::Vector2f logicalSize, float zoom) {
      zoom = ClampZoom(zoom);
      const auto local = std::find_if(world.agents.begin(), world.agents.end(), [localPlayer](const Agent &agent) { return agent.id == localPlayer; });
      const auto camera = local == world.agents.end() ? world.size * 0.5f : local->position;
      const float weaponHeight = WeaponHeight(local == world.agents.end() ? Stance::Standing : local->stance);
      AimSolution result{UnprojectGround(logical + sf::Vector2f{0.f, weaponHeight * zoom}, camera, logicalSize, zoom), 0.f};
      const auto base = UnprojectGround(logical, camera, logicalSize, zoom);
      const sf::Vector2f heightRay{0.5f / PresentationConfig::GroundProjection.y, 0.5f / PresentationConfig::GroundProjection.y};
      float closestHeight = -1.f;
      const auto consider = [&](sf::Vector2f position, sf::Vector2f facing, Stance stance, bool down) {
        for (const auto &volume : CombatGeometry::ActorVolumes(position, facing, stance, down)) {
          const float viewHeight = CombatConfig::StandingHeight + PresentationConfig::PixelSize;
          const float viewRate = heightRay.length();
          const auto intersection = CombatGeometry::IntersectVolume(base + heightRay * viewHeight, -heightRay / viewRate, viewHeight, -1.f / viewRate, viewHeight * viewRate, volume);
          if (!intersection) continue;
          const float height = std::max(volume.bottom, viewHeight - *intersection / viewRate - 0.01f);
          if (height < closestHeight) continue;
          const auto point = base + heightRay * height;
          const float distance = (point - camera).length();
          if (distance <= 0.01f) continue;
          closestHeight = height;
          result = {point, std::clamp((height - weaponHeight) / distance, -CombatConfig::MaximumAimSlope, CombatConfig::MaximumAimSlope)};
        }
      };
      for (const auto &guard : world.guards) {
        if (guard.bodyHidden || guard.storedIn != 0 || guard.state == GuardState::Dead || guard.state == GuardState::Incapacitated) continue;
        if (local != world.agents.end() && !Visibility::GuardVisible(world, *local, guard)) continue;
        const bool down = guard.cqcState == CqcState::KnockedDown || guard.cqcState == CqcState::Restrained;
        consider(guard.position, guard.facing, Stance::Standing, down);
      }
      for (const auto &agent : world.agents) {
        if (agent.id == localPlayer || !agent.alive || agent.incapacitated || agent.extracted || !agent.connected || agent.hidden) continue;
        if (local != world.agents.end() && !Visibility::IsVisible(world, *local, agent.position, agent.stance)) continue;
        consider(agent.position, agent.facing, agent.stance, false);
      }
      return result;
    }
    static sf::Vector2f ResolveAimGround(sf::Vector2f logical, const WorldState &world, PlayerId localPlayer, sf::Vector2f logicalSize, float zoom) {
      return ResolveAim(logical, world, localPlayer, logicalSize, zoom).ground;
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
