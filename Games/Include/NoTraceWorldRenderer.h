#pragma once

#include "Types/NoTraceGameplayTypes.h"
#include "Config/NoTracePresentationConfig.h"
#include "Config/NoTraceCqcConfig.h"
#include "Config/NoTraceSpriteConfig.h"
#include "Config/NoTraceManeuverConfig.h"
#include "Config/NoTraceWeaponConfig.h"
#include "NoTraceCombatGeometry.h"
#include "NoTraceVisibility.h"
#include "NoTraceWorldGeometry.h"
#include "NoTraceSpatialGeometry.h"
#include "NoTraceEnvironmentArt.h"
#include "NoTraceCharacterArt.h"
#include <SFML/Graphics/Color.hpp>
#include <SFML/Graphics/Image.hpp>
#include <SFML/Graphics/Text.hpp>
#include <SFML/Graphics/View.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <list>
#include <optional>
#include <numbers>
#include <unordered_map>

struct EngineContext;
namespace sf { class Font; class Texture; class VertexArray; }

namespace NoTrace {
  class WorldRenderer {
  public:
    using AimSolution = CombatGeometry::AimSolution;
    enum class GuardIndicator { None, Suspicion, Confirmed, Combat, Unconscious, Searching, Dead };
    struct SpriteFrame { std::size_t index = 0; bool prone = false; bool mirror = false; bool operative = false; };
    struct DetectionVisual { unsigned int segments = 0; sf::Color color; bool confirmed = false; };
    struct CqcVisual { float extension = 0.f; float phase = 0.f; bool kick = false; bool gripping = false; };
    struct SpritePlacement {
      sf::Vector2f position, origin, scale;
      sf::Vector2f Map(sf::Vector2f point) const { return position + (point - origin).componentWiseMul(scale); }
    };
    struct GroundSupport { sf::Vector2f anchor{SpriteConfig::FeetX, SpriteConfig::FeetY}; float width = 0.f; };
    struct ShadowSupport { std::uint32_t id = 0; float elevation = 0.f; };
    struct FogContour {
      struct Edge { sf::Vector2f first, delta; float lengthSquared = 0.f; };
      std::array<Edge, VisibilityConfig::VisualRayCount> edges;
    };
  private:
    struct RenderVisibility { std::vector<std::uint8_t> objects, guards; };
    struct CachedLabel { std::string text; unsigned int size; sf::Text drawable; };
    EngineContext &ctx_;
    EnvironmentArt environmentArt_;
    CharacterArt characterArt_;
    mutable float cameraElevation_ = 0.f;
    const sf::Font *font_;
    mutable std::list<CachedLabel> labelCache_;
    mutable std::unordered_map<PlayerId, std::size_t> agentSpriteDirections_;
    mutable std::unordered_map<std::uint32_t, std::size_t> guardSpriteDirections_;
    float zoom_ = PresentationConfig::DefaultZoom;
    bool namesVisible_ = false;
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
    sf::Vector2f GroundToScreen(sf::Vector2f ground, sf::Vector2f camera, float elevation = 0.f) const;
    void Polygon(std::initializer_list<sf::Vector2f> points, sf::Color color) const;
    void PixelRect(sf::Vector2f position, sf::Vector2f size, sf::Color color) const;
    void PixelLine(sf::Vector2f from, sf::Vector2f to, sf::Color color, float thickness = 2.f) const;
    void Label(const std::string &text, sf::Vector2f position, sf::Color color, unsigned int size = PresentationConfig::WorldLabelSize) const;
    void Box(sf::FloatRect bounds, float height, sf::Vector2f camera, sf::Color top, sf::Color left, sf::Color right) const;
    void Floor(const WorldState &world, sf::Vector2f camera) const;
    void Fog(const WorldState &world, const sf::VertexArray &mask) const;
    void RescueIndicators(const WorldState &world, const Agent *observer) const;
    void NameIndicators(const WorldState &world, const Agent *observer, sf::Vector2f camera) const;
    void ObstacleArt(const Obstacle &obstacle, sf::Vector2f camera) const;
    void ObjectArt(const Interactable &object, sf::Vector2f camera, bool nearby) const;
    SpriteFrame AgentFrame(const Agent &agent, const WorldState &world, bool local) const;
    SpriteFrame GuardFrame(const Guard &guard, const WorldState &world) const;
    std::optional<SpriteFrame> AgentWeaponFrame(const Agent &agent, const WorldState &world) const;
    std::optional<sf::Vector2f> SourceMuzzle(const WorldState &world, const ShotTrace &shot, sf::Vector2f camera, const Agent *observer) const;
    void Humanoid(sf::Vector2f feet, sf::Vector2f facing, Stance stance, sf::Color uniform, SpriteFrame frame, bool down, bool guard = false, sf::Vector2f bodyOffset = {}, const Agent *articulated = nullptr, std::optional<SpriteFrame> upperFrame = std::nullopt, bool separateCqc = false, bool drawShadow = true) const;
    void HumanoidShadow(sf::Vector2f feet, Stance stance, SpriteFrame frame, bool down) const;
    void AgentShadowArt(const Agent &agent, const WorldState &world, sf::Vector2f camera, bool local) const;
    void GuardShadowArt(const Guard &guard, const WorldState &world, sf::Vector2f camera) const;
    void AgentArt(const Agent &agent, const WorldState &world, sf::Vector2f camera, bool local, bool deferCqc = false) const;
    void GuardArt(const Guard &guard, const WorldState &world, sf::Vector2f camera) const;
    void DummyArt(const Guard &dummy, const WorldState &world, sf::Vector2f camera) const;
    void DummyStatus(const Guard &dummy, const WorldState &world, sf::Vector2f camera, const Agent *observer) const;
    void CqcArt(const Agent &agent, const WorldState &world, sf::Vector2f camera, sf::Vector2f feet, sf::Color uniform) const;
    bool CqcContactVisible(const Agent &agent, const WorldState &world, sf::Vector2f camera) const;
    void ExplosionArt(const Interactable &object, const WorldState &world, sf::Vector2f camera) const;
    void Vision(const Guard &guard, const WorldState &world, sf::Vector2f camera, const VisibilityGrid *viewer = nullptr, const FogContour *contour = nullptr) const;
    void FieldOfView(sf::Vector2f position, sf::Vector2f facing, float range, sf::Color color, const WorldState &world, sf::Vector2f camera, float cosHalfAngle = GameplayConfig::VisionCosHalfAngle, float elevation = 0.f, const VisibilityGrid *viewer = nullptr, const FogContour *contour = nullptr) const;
    void Noise(const NoiseEvent &noise, sf::Vector2f camera) const;
    void Combat(const WorldState &world, sf::Vector2f camera, const Agent *observer) const;
    void GuardIcon(const Guard &guard, sf::Vector2f camera) const;
    void BodyStatus(const Agent &agent, sf::Vector2f camera) const;
    void BodyStatusGlyph(sf::Vector2f center, GuardIndicator indicator) const;
    void CameraIcon(const Interactable &object, sf::Vector2f camera) const;
    void DecoyPreview(const WorldState &world, sf::Vector2f camera, const Agent *observer) const;
    void DecoyArt(const DecoyProjectile &disc, sf::Vector2f camera) const;
    void AimLaser(const WorldState &world, sf::Vector2f camera, const Agent *observer) const;
    void Footsteps(const WorldState &world, sf::Vector2f camera, const Agent *observer) const;
    void Minimap(const WorldState &world, PlayerId localPlayer, const sf::VertexArray &fogMask, const RenderVisibility &visibilityCache, const WorldGeometry::PreparedOccluders *geometry = nullptr) const;
    void DetectionEye(const WorldState &world, const Agent *observer) const;
    void EyeGlyph(sf::Vector2f center, DetectionVisual visual, float scale, bool brackets) const;

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
    void SetNamesVisible(bool visible) { namesVisible_ = visible; }
    bool NamesVisible() const { return namesVisible_; }
    const CharacterArt &CharacterSprites() const { return characterArt_; }
    sf::Vector2f ScreenToGround(sf::Vector2f logical, const WorldState &world, PlayerId localPlayer) const;
    sf::Vector2f AimToGround(sf::Vector2f logical, const WorldState &world, PlayerId localPlayer) const;
    AimSolution AimAt(sf::Vector2f logical, const WorldState &world, PlayerId localPlayer) const;
    static std::optional<ShadowSupport> ShadowSupportBelow(const WorldState &world, sf::Vector2f position, float elevation) {
      if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(elevation) || elevation < LevelConfig::MinimumElevation || elevation > LevelConfig::MaximumElevation) return std::nullopt;
      std::optional<ShadowSupport> receiver;
      for (const auto &surface : world.walkSurfaces) {
        if (!WorldGeometry::ContainsSupport(surface, position)) continue;
        const float height = WorldGeometry::SurfaceHeight(surface, position);
        if (!std::isfinite(height) || height < LevelConfig::MinimumElevation || height > LevelConfig::MaximumElevation || height > elevation + LevelConfig::SupportTolerance) continue;
        if (!receiver || height > receiver->elevation || (height == receiver->elevation && surface.id < receiver->id)) receiver = ShadowSupport{surface.id, height};
      }
      return receiver;
    }
    static std::optional<ShadowSupport> TraversalShadowSupport(const WorldState &world, const Agent &agent) {
      if ((agent.traversal.mode == TraversalMode::Grounded && !agent.forcedFall.active) || agent.hidden || agent.extracted) return std::nullopt;
      return ShadowSupportBelow(world, agent.position, agent.elevation);
    }
    static float ClampZoom(float zoom) {
      return std::isfinite(zoom) ? std::clamp(zoom, PresentationConfig::MinimumZoom, PresentationConfig::MaximumZoom) : PresentationConfig::DefaultZoom;
    }
    static float LowerFloorFogOpacity(float observerElevation, float surfaceElevation) {
      if (!std::isfinite(observerElevation) || !std::isfinite(surfaceElevation)) return 0.f;
      const float difference = observerElevation - surfaceElevation - LevelConfig::SupportTolerance;
      if (difference <= 0.f) return 0.f;
      return std::clamp(1.f - std::exp(-PresentationConfig::LowerFloorFogDensity * difference / LevelConfig::StoreyHeight),
          0.f, PresentationConfig::LowerFloorFogMaximumOpacity);
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
      return focus + (point - focus) * (std::isfinite(zoom) && zoom > 0.f ? zoom : 1.f);
    }
    static sf::Vector2f UnprojectGround(sf::Vector2f logical, sf::Vector2f camera, sf::Vector2f logicalSize, float zoom = 1.f) {
      const auto delta = (logical - LogicalCenter(logicalSize)) / (std::isfinite(zoom) && zoom > 0.f ? zoom : 1.f);
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
    static std::size_t SpriteDirection(sf::Vector2f facing) {
      const sf::Vector2f screen{(facing.x - facing.y) * PresentationConfig::GroundProjection.x, (facing.x + facing.y) * PresentationConfig::GroundProjection.y};
      const int sector = static_cast<int>(std::lround(std::atan2(screen.x, screen.y) / (std::numbers::pi_v<float> * 0.25f)));
      return static_cast<std::size_t>((sector + 8) % 8);
    }
    static std::size_t StableSpriteDirection(sf::Vector2f facing, std::optional<std::size_t> previous) {
      const sf::Vector2f screen{(facing.x - facing.y) * PresentationConfig::GroundProjection.x, (facing.x + facing.y) * PresentationConfig::GroundProjection.y};
      if (!std::isfinite(screen.x) || !std::isfinite(screen.y) || screen.lengthSquared() < 0.000001f)
        return previous && *previous < SpriteConfig::Directions ? *previous : 0;
      if (previous && *previous < SpriteConfig::Directions) {
        const float turn = std::numbers::pi_v<float> * 2.f;
        const float offset = std::remainder(std::atan2(screen.x, screen.y) - static_cast<float>(*previous) * turn / SpriteConfig::Directions, turn);
        const float threshold = turn / (SpriteConfig::Directions * 2.f) + SpriteConfig::DirectionHysteresisDegrees * std::numbers::pi_v<float> / 180.f;
        if (std::abs(offset) <= threshold) return *previous;
      }
      return SpriteDirection(facing);
    }
    static GroundSupport MeasureGroundSupport(const sf::Image &image, const std::array<sf::IntRect, 4> &frames) {
      std::array<float, 4> centers{}, widths{};
      for (std::size_t index = 0; index < frames.size(); ++index) {
        const auto &frame = frames[index];
        int bottom = -1, left = frame.size.x, right = -1;
        for (int y = frame.size.y - 1; y >= 0 && bottom < 0; --y) for (int x = 0; x < frame.size.x; ++x)
          if (image.getPixel(sf::Vector2u(frame.position + sf::Vector2i{x, y})).a >= SpriteConfig::SupportAlpha) bottom = y;
        if (bottom < 0) { centers[index] = SpriteConfig::FeetX; continue; }
        for (int y = std::max(0, bottom - SpriteConfig::SupportBandPixels); y <= bottom; ++y) for (int x = 0; x < frame.size.x; ++x) {
          if (image.getPixel(sf::Vector2u(frame.position + sf::Vector2i{x, y})).a < SpriteConfig::SupportAlpha) continue;
          left = std::min(left, x); right = std::max(right, x);
        }
        centers[index] = (left + right + 1) * 0.5f; widths[index] = static_cast<float>(right - left + 1);
      }
      std::sort(centers.begin(), centers.end()); std::sort(widths.begin(), widths.end());
      // La mediana del ciclo evita que un pie en vuelo o un extremo del arma hagan bailar la sombra.
      return {{(centers[1] + centers[2]) * 0.5f, SpriteConfig::FeetY}, (widths[1] + widths[2]) * 0.5f};
    }
    static SpriteFrame ClipFrame(SpriteConfig::Clip clip, sf::Vector2f facing, float seconds, std::optional<float> progress = std::nullopt) {
      const auto &animation = SpriteConfig::Animations.at(static_cast<std::size_t>(clip));
      const auto elapsed = static_cast<std::size_t>(std::max(0.f, std::isfinite(seconds) ? seconds : 0.f) * animation.fps);
      auto frame = progress ? std::min(animation.count - 1, static_cast<std::size_t>(std::clamp(*progress, 0.f, 1.f) * animation.count)) :
          animation.loop ? elapsed % animation.count : std::min(elapsed, animation.count - 1);
      if (animation.reverse) frame = animation.count - 1 - frame;
      return {animation.family * SpriteConfig::FamilyFrames + SpriteDirection(facing) * SpriteConfig::Columns + animation.first + frame, false, false, true};
    }
    static SpriteFrame SelectActorSprite(const Agent &agent, bool throwingPreview = false) {
      using SpriteConfig::Clip;
      const auto select = [&](Clip clip, std::optional<float> progress = std::nullopt) {
        auto frame = ClipFrame(clip, agent.facing, agent.animationTime, progress);
        if (agent.stance != Stance::Standing && (clip == Clip::Aim || clip == Clip::Shoot || clip == Clip::Reload || clip == Clip::Throw))
          frame.index = (agent.stance == Stance::Crouched ? 5 : 6) * SpriteConfig::FamilyFrames + frame.index % SpriteConfig::FamilyFrames;
        return frame;
      };
      if (agent.forcedFall.active) return select(Clip::TraversalDrop, agent.forcedFall.progress);
      if ((!agent.alive || agent.incapacitated) && agent.actionPose == ActionPose::Fall && agent.actionPoseTime > 0.f)
        return select(agent.alive ? Clip::Fall : Clip::Death, 1.f - agent.actionPoseTime / GameplayConfig::AttackAnimationDuration);
      if (!agent.alive || agent.incapacitated) return select(agent.alive ? Clip::Unconscious : Clip::Death, agent.alive ? std::nullopt : std::optional{1.f});
      if (agent.traversal.mode != TraversalMode::Grounded) {
        const auto clip = agent.traversal.mode == TraversalMode::Attach ? Clip::TraversalAttach : agent.traversal.mode == TraversalMode::Hang ? Clip::TraversalHang :
            agent.traversal.mode == TraversalMode::Shimmy ? Clip::TraversalShimmy : agent.traversal.mode == TraversalMode::Transfer ? Clip::TraversalTransfer :
            agent.traversal.mode == TraversalMode::Climb ? Clip::TraversalClimb : Clip::TraversalDrop;
        return select(clip, agent.traversal.mode == TraversalMode::Hang || agent.traversal.mode == TraversalMode::Shimmy || agent.traversal.mode == TraversalMode::Climb ? std::nullopt : std::optional{agent.traversal.progress});
      }
      if (agent.draggingGuard >= 0) return select(agent.moving ? Clip::DragLoop : Clip::DragStart, agent.moving ? std::nullopt : std::optional{1.f});
      if (agent.maneuver.kind != ManeuverKind::None) {
        const auto &motion = agent.maneuver;
        const bool dive = motion.kind == ManeuverKind::Dive;
        const auto clip = dive ? Clip::Dive : Clip::Roll;
        const float travel = dive ? ManeuverConfig::DiveTravel : ManeuverConfig::RollTravel;
        const float start = dive ? ManeuverConfig::DiveStart : ManeuverConfig::RollStart;
        const float land = dive ? ManeuverConfig::DiveLand : 0.f;
        const float recover = dive ? ManeuverConfig::DiveRecover : ManeuverConfig::RollRecover;
        const float frames = static_cast<float>(SpriteConfig::Animations[static_cast<std::size_t>(clip)].count);
        // El viaje recorre las poses intermedias; contacto y recuperación avanzan hasta la pose final sin retroceder.
        const float frame = motion.phase == ManeuverPhase::Start ? std::clamp(motion.time / start, 0.f, 1.f) :
            motion.phase == ManeuverPhase::Travel ? 1.f + (frames - (dive ? 3.f : 2.f)) * std::clamp((motion.time - start) / travel, 0.f, 1.f) :
            motion.phase == ManeuverPhase::Land && dive ? frames - 2.f + std::clamp((motion.time - start - travel) / land, 0.f, 1.f) :
            frames - 1.f + std::clamp((motion.time - start - travel - land) / recover, 0.f, 1.f);
        const float phase = frame / frames;
        return ClipFrame(clip, agent.maneuver.direction, agent.maneuver.time, phase);
      }
      if (agent.cqcHoldingGuard || agent.cqcHoldingAgent) return agent.actionPose == ActionPose::Choke && agent.actionPoseTime > 0.f ?
          select(Clip::Choke, 1.f - agent.actionPoseTime / GameplayConfig::AttackAnimationDuration) : select(Clip::Grab);
      if (agent.cqcHeldBy) return select(Clip::GripVictimHold);
      if (agent.cqcAttackTime > 0.f) {
        const bool kick = !agent.cqcNextKick;
        const float duration = kick ? CqcConfig::KickCooldown : CqcConfig::PunchCooldown;
        const float windup = kick ? CqcConfig::KickWindup : CqcConfig::PunchWindup;
        const float elapsed = std::max(0.f, duration - agent.cqcAttackTime);
        const float phase = elapsed < windup ? 0.f : elapsed < windup + (duration - windup) * 0.4f ? 0.25f : elapsed < duration * 0.85f ? 0.5f : 0.75f;
        return select(kick ? Clip::Kick : Clip::Punch, phase);
      }
      if (agent.actionPose == ActionPose::Revive && agent.actionPoseTime > 0.f) {
        auto frame = select(Clip::ReviveRise, 1.f - agent.actionPoseTime / GameplayConfig::StanceTransitionDuration);
        frame.index += agent.stance == Stance::Crouched ? 8 : agent.stance == Stance::Prone ? 16 : 0;
        return frame;
      }
      if (agent.reloadRemaining > 0.f) return select(Clip::Reload, 1.f - agent.reloadRemaining / WeaponConfig::ReloadDuration);
      if (agent.actionPoseTime > 0.f) {
        const float duration = agent.actionPose == ActionPose::HideEnter || agent.actionPose == ActionPose::HideExit || agent.actionPose == ActionPose::CoverEnter ? GameplayConfig::StanceTransitionDuration : GameplayConfig::AttackAnimationDuration;
        const float phase = 1.f - agent.actionPoseTime / duration;
        switch (agent.actionPose) {
        case ActionPose::ThrowDecoy: return select(Clip::Throw, phase);
        case ActionPose::KnockWall: return ClipFrame(Clip::CoverIdle, agent.cover.active ? agent.cover.normal : agent.facing, 0.f, 0.f);
        case ActionPose::Push: return select(Clip::Push, phase);
        case ActionPose::Punch: return select(Clip::Punch, phase);
        case ActionPose::Kick: return select(Clip::Kick, phase);
        case ActionPose::Shoot: return select(Clip::Shoot, phase);
        case ActionPose::TakeDamage: return select(Clip::Damage, phase);
        case ActionPose::Fall: return select(Clip::Fall, phase);
        case ActionPose::Interact: return agent.puzzle.target != 0 ? select(Clip::Interact) : select(Clip::Interact, 1.f - agent.actionPoseTime / GameplayConfig::StanceTransitionDuration);
        case ActionPose::HideEnter: return select(Clip::HideEnter, phase);
        case ActionPose::HideExit: return select(Clip::HideExit, phase);
        case ActionPose::CoverEnter: return select(Clip::CoverEnter, phase);
        default: break;
        }
      }
      if (agent.transitionTime > 0.f && (agent.transitionFrom == Stance::Prone || agent.stance == Stance::Prone)) {
        const float progress = 1.f - agent.transitionTime / GameplayConfig::StanceTransitionDuration;
        return select(agent.stance == Stance::Prone ? Clip::LieDown : Clip::StandUp, progress);
      }
      if (agent.transitionTime > 0.f && agent.transitionFrom != agent.stance)
        return select(agent.stance == Stance::Crouched ? Clip::CrouchEnter : Clip::CrouchExit, 1.f - agent.transitionTime / GameplayConfig::StanceTransitionDuration);
      if (throwingPreview) return select(Clip::Throw, 0.25f);
      if (agent.aiming) return select(Clip::Aim);
      if (agent.cover.active && agent.cover.peekSide == PeekSide::None) return ClipFrame(agent.moving ? Clip::CoverMove : Clip::CoverIdle, agent.cover.normal, agent.animationTime);
      if (agent.cover.active) return select(agent.moving ? Clip::CoverMove : Clip::CoverIdle);
      if (agent.stance == Stance::Prone) return select(agent.moving ? Clip::Crawl : Clip::ProneIdle);
      if (agent.stance == Stance::Crouched) return select(agent.moving ? agent.running ? Clip::CrouchRun : Clip::CrouchWalk : Clip::CrouchIdle);
      return select(agent.moving ? agent.running ? Clip::Run : Clip::Walk : Clip::Idle);
    }
    static bool HasWeaponLayer(const Agent &agent) {
      return agent.alive && !agent.incapacitated && !agent.hidden && !agent.forcedFall.active && agent.maneuver.kind == ManeuverKind::None && agent.cqcHeldBy == 0 &&
          agent.cqcHoldingGuard == 0 && agent.cqcHoldingAgent == 0 && agent.cqcAttackTime <= 0.f && agent.actionPose != ActionPose::KnockWall && agent.actionPose != ActionPose::Revive && agent.traversal.mode == TraversalMode::Grounded && agent.draggingGuard < 0 &&
          (agent.aiming || agent.reloadRemaining > 0.f || (agent.actionPose == ActionPose::Shoot && agent.actionPoseTime > 0.f));
    }
    static SpriteFrame SelectBodySprite(const Agent &agent, bool throwingPreview = false) {
      if (agent.actionPose == ActionPose::Revive && agent.actionPoseTime > 0.f) return SelectActorSprite(agent);
      if (agent.cover.active && agent.alive && !agent.incapacitated && agent.stance == Stance::Standing &&
          agent.actionPose == ActionPose::CoverEnter && agent.actionPoseTime > 0.f)
        return ClipFrame(SpriteConfig::Clip::CoverEnter, agent.cover.normal, 0.f, 1.f - agent.actionPoseTime / GameplayConfig::StanceTransitionDuration);
      if (agent.cover.active && agent.alive && !agent.incapacitated) return ClipFrame(agent.stance == Stance::Crouched ? agent.moving ? SpriteConfig::Clip::CrouchWalk : SpriteConfig::Clip::CrouchIdle : agent.moving ? SpriteConfig::Clip::CoverMove : SpriteConfig::Clip::CoverIdle,
          agent.cover.normal, agent.animationTime, agent.moving ? std::nullopt : std::optional{0.f});
      if (!HasWeaponLayer(agent)) return SelectActorSprite(agent, throwingPreview);
      auto body = agent; body.aiming = false; body.reloadRemaining = 0.f;
      if (body.actionPose == ActionPose::Shoot) { body.actionPose = ActionPose::None; body.actionPoseTime = 0.f; }
      return SelectActorSprite(body, throwingPreview);
    }
    static SpriteFrame SelectWeaponSprite(const Agent &agent) {
      auto upper = agent; upper.cover = {}; upper.moving = false; upper.facing = WeaponFacing(agent); upper.aiming = true;
      return SelectActorSprite(upper);
    }
    static sf::Vector2f TorsoPivot(Stance stance) {
      return {SpriteConfig::FeetX, stance == Stance::Standing ? SpriteConfig::StandingTorsoPivotY : stance == Stance::Crouched ? SpriteConfig::CrouchedTorsoPivotY : SpriteConfig::ProneTorsoPivotY};
    }
    static sf::Vector2f TorsoForward(Stance stance, sf::Vector2f facing) {
      const sf::Vector2f projected{(facing.x - facing.y) * PresentationConfig::GroundProjection.x, (facing.x + facing.y) * PresentationConfig::GroundProjection.y};
      return stance == Stance::Prone && projected.lengthSquared() > 0.01f ? projected.normalized() : sf::Vector2f{0.f, -1.f};
    }
    static sf::Vector2f CoverMuzzle(const Agent &agent, sf::Vector2f feet) {
      const auto aim = WeaponFacing(agent);
      const sf::Vector2f projected{(aim.x - aim.y) * PresentationConfig::GroundProjection.x, (aim.x + aim.y) * PresentationConfig::GroundProjection.y};
      return feet - sf::Vector2f{0.f, WeaponHeight(agent.stance) + 4.f} + projected.normalized() * 26.f;
    }
    static SpriteFrame SelectGuardSprite(const Guard &guard, const WorldState &world) {
      using SpriteConfig::Clip;
      const auto select = [&](Clip clip, std::optional<float> phase = std::nullopt) { return ClipFrame(clip, guard.facing, world.elapsed + guard.id * 0.25f, phase); };
      if (guard.forcedFall.active) return select(Clip::TraversalDrop, guard.forcedFall.progress);
      const bool falling = guard.state == GuardState::Incapacitated && guard.incapacityRemaining > GameplayConfig::GuardIncapacityDuration - PresentationConfig::CqcFallDuration;
      if (falling) return select(Clip::Fall, (GameplayConfig::GuardIncapacityDuration - guard.incapacityRemaining) / PresentationConfig::CqcFallDuration);
      if (guard.state == GuardState::Dead && guard.attackTime > 0.f) return select(Clip::Fall, 1.f - guard.attackTime / GameplayConfig::AttackAnimationDuration);
      if (guard.state == GuardState::Dead) return select(Clip::Death, 1.f);
      if (guard.state == GuardState::Incapacitated || (guard.heldBy == 0 &&
          (guard.cqcState == CqcState::KnockedDown || guard.cqcState == CqcState::Restrained))) return select(Clip::Unconscious);
      if (guard.heldBy) return select(Clip::GripVictimHold);
      if (guard.wakeRemaining > 0.f) return select(Clip::ReviveRise, 1.f - guard.wakeRemaining / GameplayConfig::StanceTransitionDuration);
      if (guard.staggerRemaining > 0.f) return select(Clip::Damage, std::clamp(1.f - guard.staggerRemaining / CqcConfig::PunchStagger, 0.f, 1.f));
      if (guard.attackTime > 0.f) return select(guard.meleeAttack ? Clip::Punch : Clip::Shoot, 1.f - guard.attackTime / GameplayConfig::AttackAnimationDuration);
      if (guard.state == GuardState::InspectBody) return ClipFrame(Clip::Interact, guard.facing, guard.bodyInspectionTime);
      const auto destination = guard.state == GuardState::Patrol && !guard.patrol.empty() ? guard.patrol[guard.waypoint % guard.patrol.size()] : guard.lastKnown;
      const bool moving = (guard.state != GuardState::Patrol || guard.patrolPauseRemaining <= 0.f) && (world.visibility.authoritative ? guard.stepTime > 0.f :
          (destination - guard.position).lengthSquared() > GameplayConfig::InvestigationStopDistance * GameplayConfig::InvestigationStopDistance);
      return select(moving ? guard.state == GuardState::Alert ? Clip::Run : Clip::Walk : Clip::Idle);
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
      if (frame.operative) {
        const float scale = PresentationConfig::ActorHeight / SpriteConfig::StandingPixels;
        return {SnapToPixels(feet), {SpriteConfig::FeetX, SpriteConfig::FeetY}, {scale, scale}};
      }
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
      if (guard.trainingDummy || guard.bodyHidden || guard.storedIn != 0) return GuardIndicator::None;
      if (guard.state == GuardState::Dead) return GuardIndicator::Dead;
      if (guard.state == GuardState::Incapacitated) return GuardIndicator::Unconscious;
      if (guard.detectedTime > 0.f) return GuardIndicator::Confirmed;
      if (guard.state == GuardState::Alert) return GuardIndicator::Combat;
      if (guard.state == GuardState::Search || guard.state == GuardState::ApproachBody || guard.state == GuardState::InspectBody || guard.state == GuardState::Vigilance)
        return GuardIndicator::Searching;
      if (guard.suspicionRemaining > 0.f || guard.state == GuardState::Suspicion || guard.state == GuardState::Investigate) return GuardIndicator::Suspicion;
      return GuardIndicator::None;
    }
    static GuardIndicator BodyIndicatorFor(const Agent &agent) {
      if (agent.hidden || agent.extracted) return GuardIndicator::None;
      if (agent.lifeState == LifeState::Dead || !agent.alive) return GuardIndicator::Dead;
      return agent.lifeState == LifeState::Incapacitated || agent.incapacitated ? GuardIndicator::Unconscious : GuardIndicator::None;
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
    static FogContour PrepareFogContour(const VisibilityGrid &grid) {
      FogContour contour{};
      if (grid.visualRadii.size() != VisibilityConfig::VisualRayCount) return contour;
      const auto vertex = [&](std::size_t index) {
        const float angle = index * 2.f * std::numbers::pi_v<float> / grid.visualRadii.size();
        return grid.visualOrigin + sf::Vector2f{std::cos(angle), std::sin(angle)} * FogBoundaryRadius(grid, angle);
      };
      auto first = vertex(0);
      for (std::size_t index = 1; index <= grid.visualRadii.size(); ++index) {
        const auto last = vertex(index % grid.visualRadii.size());
        const auto edge = last - first;
        contour.edges[index - 1] = {first, edge, edge.lengthSquared()};
        first = last;
      }
      return contour;
    }
    static float FogOpacity(const VisibilityGrid &grid, sf::Vector2f point, const FogContour *contour = nullptr) {
      const auto delta = point - grid.visualOrigin;
      const float limit = FogBoundaryRadius(grid, std::atan2(delta.y, delta.x));
      if (limit <= 0.f || delta.length() >= limit) return 1.f;
      float distance = PresentationConfig::FogFeatherWidth;
      if (contour) {
        for (const auto &edge : contour->edges) {
          const float along = edge.lengthSquared > 0.f ? std::clamp((point - edge.first).dot(edge.delta) / edge.lengthSquared, 0.f, 1.f) : 0.f;
          distance = std::min(distance, (point - edge.first - edge.delta * along).length());
        }
      } else {
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
      }
      const float blend = std::clamp(1.f - distance / PresentationConfig::FogFeatherWidth, 0.f, 1.f);
      return blend * blend * (3.f - 2.f * blend);
    }
    static DetectionVisual DetectionFor(const Agent &agent) {
      const float progress = std::isfinite(agent.detectionProgress) ? std::clamp(agent.detectionProgress, 0.f, 1.f) : 0.f;
      const bool confirmed = agent.detectionConfirmed || agent.confirmedIndicatorRemaining > 0.f;
      auto color = confirmed || progress >= PresentationConfig::DetectionWarningThreshold ? PresentationConfig::Red : PresentationConfig::Amber;
      color.a = static_cast<std::uint8_t>(96.f + progress * 159.f);
      return {confirmed ? PresentationConfig::DetectionEyeSegments : static_cast<unsigned int>(std::ceil(progress * PresentationConfig::DetectionEyeSegments)), color, confirmed};
    }
    static DetectionVisual SecurityEyeFor(const WorldState &world) {
      switch (world.securityAlert) {
      case SecurityAlert::Red: return {PresentationConfig::DetectionEyeSegments, PresentationConfig::Red, true};
      case SecurityAlert::Orange: return {PresentationConfig::DetectionEyeSegments, PresentationConfig::Orange, false};
      case SecurityAlert::Yellow: return {PresentationConfig::DetectionEyeSegments, PresentationConfig::Yellow, false};
      default: return {};
      }
    }
    static bool AnimateHideoutEntry(const WorldState &world, const Agent &agent, bool local) {
      if (!local || !agent.hidden || agent.actionPose != ActionPose::HideEnter || agent.actionPoseTime <= 0.f) return false;
      const auto object = std::ranges::find_if(world.objects, [&](const Interactable &candidate) { return candidate.id == agent.hideoutId; });
      return object == world.objects.end() || WorldGeometry::HideoutType(*object) != HideoutKind::UnderTable;
    }
    static DetectionVisual GuardDetectionFor(const Guard &guard) {
      Agent observed;
      observed.detectionProgress = std::max(guard.suspicion, guard.indicatorProgress);
      observed.detectionConfirmed = guard.detectedTime > 0.f || guard.state == GuardState::Alert;
      return DetectionFor(observed);
    }
    static CqcVisual CqcPoseFor(const Agent &agent) {
      if (agent.cqcHoldingGuard != 0 || agent.cqcHoldingAgent != 0) {
        const float phase = std::clamp(1.f - agent.actionPoseTime / GameplayConfig::AttackAnimationDuration, 0.f, 1.f);
        return {agent.actionPose == ActionPose::Choke ? 0.7f + 0.3f * std::sin(phase * std::numbers::pi_v<float>) : 0.65f, phase, false, true};
      }
      if (agent.cqcAttackTime <= 0.f) return {};
      const bool kick = !agent.cqcNextKick;
      const float duration = kick ? CqcConfig::KickCooldown : CqcConfig::PunchCooldown;
      const float windup = kick ? CqcConfig::KickWindup : CqcConfig::PunchWindup;
      const float elapsed = std::clamp(duration - agent.cqcAttackTime, 0.f, duration);
      const float extension = elapsed < windup ? -0.25f * (1.f - elapsed / windup) : std::max(0.f, 1.f - (elapsed - windup) / (duration - windup));
      return {extension, elapsed / duration, kick, false};
    }
    static bool FootprintVisible(const WorldState &world, const Agent &observer, sf::FloatRect footprint, const WorldGeometry::PreparedOccluders *geometry = nullptr) {
      if (!IsTactical(world)) return true;
      const auto end = footprint.position + footprint.size;
      const sf::Vector2f closest{std::clamp(observer.position.x, footprint.position.x, end.x), std::clamp(observer.position.y, footprint.position.y, end.y)};
      // Los bordes sólidos requieren rayos exactos; el centro de su celda puede caer dentro del muro.
      if (Visibility::IsGeometricallyVisible(world, observer, closest, Stance::Standing, std::numeric_limits<float>::quiet_NaN(), geometry)) return true;
      for (const auto point : {footprint.position, sf::Vector2f{end.x, footprint.position.y}, end, sf::Vector2f{footprint.position.x, end.y}}) {
        if (Visibility::IsGeometricallyVisible(world, observer, point, Stance::Standing, std::numeric_limits<float>::quiet_NaN(), geometry)) return true;
      }
      return false;
    }
    static bool DummyHealthVisible(const Guard &dummy, const WorldState &world, const Agent *observer) {
      return IsTraining(world) && dummy.trainingDummy && observer && !dummy.bodyHidden && dummy.storedIn == 0 &&
          Visibility::GuardVisible(world, *observer, dummy) && (dummy.trainingFeedback > 0.f ||
          (dummy.position - observer->position).length() <= GameplayConfig::InteractionDistance);
    }
    static bool CqcOverlayClear(const WorldState &world, const Agent &agent, sf::Vector2f target) {
      if (!WorldGeometry::ClearMovementSegment(world, agent.position, target, GameplayConfig::AgentRadius)) return false;
      const auto end = ProjectGround(target, agent.position, {});
      const sf::FloatRect action{{std::min(0.f, end.x) - PresentationConfig::ActorHalfWidth, std::min(0.f, end.y) - PresentationConfig::ActorHeight},
          {std::abs(end.x) + PresentationConfig::ActorHalfWidth * 2.f, std::abs(end.y) + PresentationConfig::ActorHeight + PresentationConfig::PixelSize}};
      const auto overlaps = [&](sf::FloatRect bounds, float height) {
        sf::Vector2f minimum{100000.f, 100000.f}, maximum{-100000.f, -100000.f};
        const auto last = bounds.position + bounds.size;
        for (const auto corner : {bounds.position, last, sf::Vector2f{last.x, bounds.position.y}, sf::Vector2f{bounds.position.x, last.y}}) {
          const auto point = ProjectGround(corner, agent.position, {});
          minimum.x = std::min(minimum.x, point.x); minimum.y = std::min(minimum.y, point.y - height);
          maximum.x = std::max(maximum.x, point.x); maximum.y = std::max(maximum.y, point.y);
        }
        return minimum.x < action.position.x + action.size.x && maximum.x > action.position.x &&
            minimum.y < action.position.y + action.size.y && maximum.y > action.position.y;
      };
      for (const auto &obstacle : world.obstacles) if (WorldGeometry::BlocksMovement(obstacle) && overlaps(obstacle.bounds, WorldGeometry::ObstacleHeight(obstacle))) return false;
      for (const auto &object : world.objects) if (WorldGeometry::BlocksMovement(object) && overlaps(WorldGeometry::ObjectFootprint(object), WorldGeometry::ObjectHeight(object))) return false;
      return true;
    }
    static ShotTrace LaserFor(const WorldState &world, const Agent &agent) {
      const auto direction = CombatGeometry::Unit(WeaponFacing(agent));
      const float height = agent.elevation + WeaponHeight(agent.stance), slope = std::clamp(agent.aimSlope, -CombatConfig::MaximumAimSlope, CombatConfig::MaximumAimSlope);
      float range = GameplayConfig::WeaponRange;
      if (direction.x > 0.f) range = std::min(range, (world.size.x - agent.position.x) / direction.x);
      if (direction.x < 0.f) range = std::min(range, -agent.position.x / direction.x);
      if (direction.y > 0.f) range = std::min(range, (world.size.y - agent.position.y) / direction.y);
      if (direction.y < 0.f) range = std::min(range, -agent.position.y / direction.y);
      if (slope < 0.f) range = std::min(range, height / -slope);
      float closest = std::max(0.f, range);
      const auto clip = [&](sf::FloatRect bounds, float bottom, float top) {
        const CombatGeometry::HitVolume volume{bounds.position + bounds.size * 0.5f, {1.f, 0.f}, bounds.size * 0.5f, bottom, top, HitZone::None};
        if (const auto hit = CombatGeometry::IntersectVolume(agent.position, direction, height, slope, closest, volume)) closest = std::min(closest, *hit);
      };
      for (const auto &obstacle : world.obstacles) if (WorldGeometry::BlocksMovement(obstacle)) clip(obstacle.bounds, obstacle.elevation, obstacle.elevation + WorldGeometry::ObstacleHeight(obstacle));
      for (const auto &object : world.objects) if (WorldGeometry::BlocksMovement(object)) clip(WorldGeometry::ObjectFootprint(object), object.elevation, object.elevation + WorldGeometry::ObjectHeight(object));
      for (const auto &surface : world.walkSurfaces) {
        if (!surface.enabled) continue;
        const float denominator = slope - surface.slope.dot(direction);
        if (std::abs(denominator) < .00001f) continue;
        const float at = (WorldGeometry::SurfaceHeight(surface, agent.position) - height) / denominator;
        if (at > .01f && at < closest && WorldGeometry::ContainsSupport(surface, agent.position + direction * at)) closest = at;
      }
      ShotTrace trace; trace.from = agent.position; trace.to = agent.position + direction * closest;
      trace.fromHeight = height; trace.toHeight = std::max(0.f, height + slope * closest); trace.actor = agent.id;
      return trace;
    }
    static AimSolution ResolveAim(sf::Vector2f logical, const WorldState &world, PlayerId localPlayer, sf::Vector2f logicalSize, float zoom) {
      zoom = ClampZoom(zoom);
      const auto local = std::find_if(world.agents.begin(), world.agents.end(), [localPlayer](const Agent &agent) { return agent.id == localPlayer; });
      const auto camera = local == world.agents.end() ? world.size * 0.5f : local->position;
      const float cameraZ = local == world.agents.end() ? 0.f : local->elevation;
      const float weaponHeight = cameraZ + WeaponHeight(local == world.agents.end() ? Stance::Standing : local->stance);
      AimSolution result{UnprojectGround(logical + sf::Vector2f{0.f, (weaponHeight - cameraZ) * zoom}, camera, logicalSize, zoom), 0.f};
      const auto base = UnprojectGround(logical - sf::Vector2f{0.f, cameraZ * zoom}, camera, logicalSize, zoom);
      const sf::Vector2f heightRay{0.5f / PresentationConfig::GroundProjection.y, 0.5f / PresentationConfig::GroundProjection.y};
      float closestHeight = -1.f;
      const auto consider = [&](sf::Vector2f position, sf::Vector2f facing, Stance stance, bool down, float elevation) {
        for (const auto &volume : CombatGeometry::ActorVolumes(position, facing, stance, down, elevation)) {
          const float viewHeight = LevelConfig::MaximumElevation + CombatConfig::StandingHeight + PresentationConfig::PixelSize;
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
        const bool down = !guard.trainingDummy && guard.heldBy == 0 && (guard.cqcState == CqcState::KnockedDown || guard.cqcState == CqcState::Restrained);
        consider(guard.position, guard.facing, Stance::Standing, down, guard.elevation);
      }
      for (const auto &agent : world.agents) {
        if (agent.id == localPlayer || !agent.alive || agent.incapacitated || agent.extracted || !agent.connected || agent.hidden) continue;
        if (local != world.agents.end() && !world.visibility.authoritative && !Visibility::IsGeometricallyVisible(world, *local, agent.position, agent.stance, agent.elevation)) continue;
        consider(agent.position, agent.facing, agent.stance, false, agent.elevation);
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
