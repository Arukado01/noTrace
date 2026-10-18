#include "NoTraceSimulation.h"
#include "NoTraceTraversal.h"
#include "Config/NoTraceAudioConfig.h"
#include <cmath>

namespace NoTrace {
  bool Simulation::UpdateTraversal(Agent &agent, const ActionCommand &command, float dt) {
    auto &state = agent.traversal;
    if ((command.traverse || command.drop) && Traversal::Available(agent)) {
      const auto selection = Traversal::Find(world_, agent, command.drop);
      if (selection.link) {
        if (state.surfaceId != 0) EmitSound(SoundKind::GripRelease, agent.position, AudioConfig::QuietRadius, agent.id);
        CancelReload(agent); CancelPuzzle(agent); ReleaseCqc(agent); agent.cover = {};
        if (agent.actionPose == ActionPose::Revive) { agent.actionPose = ActionPose::None; agent.actionPoseTime = 0.f; }
        state.linkId = selection.link->id; state.reversed = selection.reversed; state.progress = 0.f;
        state.from = WorldGeometry::Feet(agent); state.to = Traversal::To(selection); state.targetSupport = Traversal::ToSupport(selection);
        state.mode = command.drop ? TraversalMode::Drop : selection.link->kind == VerticalLinkKind::Climb ? TraversalMode::Climb :
            state.surfaceId != 0 ? TraversalMode::Transfer : TraversalMode::Attach;
        agent.stance = Stance::Standing; agent.supportId = 0; agent.footstepTime = 0.f;
      }
    }
    if (state.mode == TraversalMode::Grounded) return false;
    agent.aiming = false; agent.moving = false; agent.running = false; agent.decoyGuide = {};
    if (state.mode == TraversalMode::Hang || state.mode == TraversalMode::Shimmy) {
      const auto *surface = WorldGeometry::FindGrip(world_, state.surfaceId);
      if (!surface) { CancelTraversal(agent); return true; }
      const auto tangent = surface->end - surface->start;
      const sf::Vector2f horizontal{tangent.x, tangent.y};
      sf::Vector2f direction{command.moveX, command.moveY};
      if (!std::isfinite(direction.x) || !std::isfinite(direction.y)) direction = {};
      if (direction.lengthSquared() > 1.f) direction = direction.normalized();
      const float signedSpeed = horizontal.lengthSquared() > 0.f ? direction.dot(horizontal.normalized()) * TraversalConfig::ShimmySpeed : 0.f;
      const float parameter = std::clamp(state.parameter + (tangent.length() > 0.f ? signedSpeed * dt / tangent.length() : 0.f), 0.f, 1.f);
      const auto candidate = Traversal::BodyAt(*surface, parameter);
      if (WorldGeometry::ClearBodySegment(world_, WorldGeometry::Feet(agent), candidate, GameplayConfig::AgentRadius, CombatConfig::StandingHeight)) {
        agent.moving = WorldGeometry::SpatialDistance(WorldGeometry::Feet(agent), candidate) > 0.01f;
        agent.position = WorldGeometry::Ground(candidate); agent.elevation = candidate.z; state.parameter = parameter;
      }
      state.mode = agent.moving ? TraversalMode::Shimmy : TraversalMode::Hang;
      agent.footstepTime = agent.moving ? agent.footstepTime + dt : 0.f;
      if (agent.footstepTime >= AudioConfig::GripFoleyInterval) {
        agent.footstepTime = std::fmod(agent.footstepTime, AudioConfig::GripFoleyInterval);
        EmitSound(SoundKind::GripMove, agent.position, AudioConfig::QuietRadius, agent.id);
      }
      state.direction = agent.moving && horizontal.lengthSquared() > 0.f ? horizontal.normalized() * (signedSpeed < 0.f ? -1.f : 1.f) : sf::Vector2f{};
      if (surface->normal.lengthSquared() > 0.f) agent.facing = -surface->normal;
      else if (state.direction.lengthSquared() > 0.f) agent.facing = state.direction;
      return true;
    }
    const float length = WorldGeometry::SpatialDistance(state.from, state.to) + (state.mode == TraversalMode::Climb ? 2.f * TraversalConfig::ClimbClearance : 0.f);
    const float speed = state.mode == TraversalMode::Drop ? TraversalConfig::DropSpeed : TraversalConfig::TransitionSpeed;
    const float duration = std::max(TraversalConfig::MinimumTransitionDuration, length / speed);
    const float progress = std::min(1.f, state.progress + dt / duration);
    const auto candidate = Traversal::TransitionPoint(state, progress);
    auto previous = WorldGeometry::Feet(agent);
    const auto clearTo = [&](sf::Vector3f point) {
      const bool clear = WorldGeometry::ClearBodySegment(world_, previous, point, GameplayConfig::AgentRadius, WorldGeometry::ActorHeight(agent));
      previous = point; return clear;
    };
    bool clear = true;
    if (state.mode == TraversalMode::Climb) for (const float boundary : {1.f / 3.f, 2.f / 3.f})
      if (state.progress < boundary && progress > boundary) clear = clear && clearTo(Traversal::TransitionPoint(state, boundary));
    if (!clear || !clearTo(candidate)) { CancelTraversal(agent); return true; }
    agent.moving = WorldGeometry::SpatialDistance(WorldGeometry::Feet(agent), candidate) > 0.01f;
    agent.position = WorldGeometry::Ground(candidate); agent.elevation = candidate.z; state.progress = progress;
    const sf::Vector2f movement{state.to.x - state.from.x, state.to.y - state.from.y};
    state.direction = movement.lengthSquared() > 0.f ? movement.normalized() : sf::Vector2f{};
    if (progress < 1.f) return true;
    const auto found = std::ranges::find(world_.verticalLinks, state.linkId, &VerticalLink::id);
    const auto targetGrip = found == world_.verticalLinks.end() ? 0u : state.reversed ? found->fromGrip : found->toGrip;
    if (const auto *surface = WorldGeometry::FindGrip(world_, targetGrip)) {
      const float parameter = Traversal::Parameter(*surface, candidate); state = {};
      state.surfaceId = targetGrip; state.parameter = parameter; state.mode = TraversalMode::Hang;
      agent.footstepTime = 0.f;
      EmitSound(SoundKind::GripAttach, agent.position, AudioConfig::QuietRadius, agent.id);
      if (surface->normal.lengthSquared() > 0.f) agent.facing = -surface->normal;
    } else {
      const float fall = state.mode == TraversalMode::Drop ? std::max(0.f, state.from.z - state.to.z) : 0.f;
      agent.supportId = state.targetSupport; state = {};
      agent.footstepTime = 0.f;
      if (agent.supportId != 0) EmitSound(fall > 0.f ? SoundKind::Land : SoundKind::GetUp, agent.position, AudioConfig::QuietRadius, agent.id);
      if (agent.supportId == 0 && !world_.walkSurfaces.empty() && agent.elevation <= LevelConfig::MinimumElevation) ConfirmDeath(agent, 0, false);
      if (fall > TraversalConfig::SafeDrop && agent.alive && !agent.incapacitated) DamageAgent(agent, (fall - TraversalConfig::SafeDrop) * TraversalConfig::FallDamagePerUnit, 0.f, 0, 0);
    }
    return true;
  }

  void Simulation::CancelTraversal(Agent &agent) {
    if (agent.traversal.mode == TraversalMode::Grounded) return;
    const auto from = WorldGeometry::Feet(agent);
    const WalkSurface *landing = nullptr; float height = LevelConfig::MinimumElevation;
    for (const auto &surface : world_.walkSurfaces) if (WorldGeometry::ContainsSupport(surface, agent.position)) {
      const float candidate = WorldGeometry::SurfaceHeight(surface, agent.position);
      if (candidate <= from.z + LevelConfig::SupportTolerance && candidate > height) { height = candidate; landing = &surface; }
    }
    agent.traversal = {}; agent.supportId = 0;
    if (landing && std::abs(height - from.z) <= LevelConfig::SupportTolerance) { agent.elevation = height; agent.supportId = landing->id; return; }
    agent.traversal.mode = TraversalMode::Drop; agent.traversal.from = from; agent.traversal.to = {from.x, from.y, height}; agent.traversal.targetSupport = landing ? landing->id : 0;
  }
}
