#pragma once

#include "NoTraceSpatialGeometry.h"
#include <string_view>

namespace NoTrace::Traversal {
  struct Selection { const VerticalLink *link = nullptr; bool reversed = false; sf::Vector3f from; sf::Vector3f to; };
  inline sf::Vector3f From(const Selection &selection) { return selection.from; }
  inline sf::Vector3f To(const Selection &selection) { return selection.to; }
  inline std::uint32_t FromGrip(const Selection &selection) { return selection.reversed ? selection.link->toGrip : selection.link->fromGrip; }
  inline std::uint32_t ToGrip(const Selection &selection) { return selection.reversed ? selection.link->fromGrip : selection.link->toGrip; }
  inline std::uint32_t ToSupport(const Selection &selection) { return selection.reversed ? selection.link->fromSupport : selection.link->toSupport; }
  inline sf::Vector3f BodyAt(const GripSurface &grip, float parameter) {
    const auto hand = grip.start + (grip.end - grip.start) * parameter;
    const auto offset = grip.kind == GripKind::Pipe ? sf::Vector2f{} : grip.normal * (GameplayConfig::AgentRadius + TraversalConfig::BodyGap);
    return {hand.x + offset.x, hand.y + offset.y, hand.z - TraversalConfig::HandHeight};
  }
  inline float Parameter(const GripSurface &grip, sf::Vector3f body) {
    const auto first = BodyAt(grip, 0.f), delta = BodyAt(grip, 1.f) - first;
    return delta.lengthSquared() > 0.f ? std::clamp((body - first).dot(delta) / delta.lengthSquared(), 0.f, 1.f) : 0.f;
  }
  inline sf::Vector3f TransitionPoint(const TraversalState &state, float progress) {
    if (state.mode != TraversalMode::Climb) return state.from + (state.to - state.from) * progress;
    const float peak = std::max(state.from.z, state.to.z) + TraversalConfig::ClimbClearance;
    if (progress < 1.f / 3.f) return {state.from.x, state.from.y, state.from.z + (peak - state.from.z) * progress * 3.f};
    if (progress < 2.f / 3.f) { const auto point = state.from + (state.to - state.from) * (progress * 3.f - 1.f); return {point.x, point.y, peak}; }
    return {state.to.x, state.to.y, peak + (state.to.z - peak) * (progress * 3.f - 2.f)};
  }
  inline bool Available(const Agent &agent) {
    return agent.connected && agent.alive && !agent.incapacitated && !agent.extracted && !agent.hidden && !agent.forcedFall.active && agent.stance != Stance::Prone &&
        agent.draggingGuard < 0 && agent.puzzle.target == 0 && agent.cqcHoldingAgent == 0 && agent.cqcHoldingGuard == 0 && agent.cqcHeldBy == 0 && agent.maneuver.kind == ManeuverKind::None;
  }
  inline Selection Find(const WorldState &world, const Agent &agent, bool drop = false) {
    Selection selected;
    if (!Available(agent) || (agent.traversal.mode != TraversalMode::Grounded && agent.traversal.mode != TraversalMode::Hang && agent.traversal.mode != TraversalMode::Shimmy)) return selected;
    const bool hanging = agent.traversal.mode != TraversalMode::Grounded;
    float nearest = hanging ? TraversalConfig::LinkReach : TraversalConfig::AttachReach;
    const auto current = WorldGeometry::Feet(agent);
    for (const auto &link : world.verticalLinks) for (const bool reversed : {false, true}) {
      if (reversed && !link.bidirectional) continue;
      Selection candidate{&link, reversed, reversed ? link.to : link.from, reversed ? link.from : link.to};
      const auto fromSupport = reversed ? link.toSupport : link.fromSupport;
      if (drop != (link.kind == VerticalLinkKind::Drop) || (hanging ? FromGrip(candidate) != agent.traversal.surfaceId : fromSupport != agent.supportId)) continue;
      if (link.kind == VerticalLinkKind::Stair || link.kind == VerticalLinkKind::ElevatorDock || (!hanging && link.kind != VerticalLinkKind::Attach && !(reversed && link.kind == VerticalLinkKind::Climb))) continue;
      if (drop) {
        const auto *landing = WorldGeometry::FindSurface(world, ToSupport(candidate));
        if (!landing || !WorldGeometry::ContainsSupport(*landing, agent.position)) continue;
        const float height = WorldGeometry::SurfaceHeight(*landing, agent.position);
        if (height > current.z + LevelConfig::SupportTolerance || current.z - height > TraversalConfig::MaximumDrop) continue;
        candidate.from = current; candidate.to = {current.x, current.y, height};
      }
      const float distance = WorldGeometry::SpatialDistance(current, From(candidate));
      if (distance > nearest) continue;
      const auto target = To(candidate);
      if (const auto id = ToSupport(candidate)) {
        const auto *support = WorldGeometry::FindSurface(world, id);
        if (!support || !WorldGeometry::ContainsSupport(*support, WorldGeometry::Ground(target)) || std::abs(WorldGeometry::SurfaceHeight(*support, WorldGeometry::Ground(target)) - target.z) > LevelConfig::SupportTolerance) continue;
      } else if (!WorldGeometry::FindGrip(world, ToGrip(candidate))) continue;
      if (link.kind == VerticalLinkKind::Climb) {
        TraversalState path; path.mode = TraversalMode::Climb; path.from = current; path.to = target;
        const auto first = TransitionPoint(path, 1.f / 3.f), second = TransitionPoint(path, 2.f / 3.f);
        if (!WorldGeometry::ClearBodySegment(world, current, first, GameplayConfig::AgentRadius, CombatConfig::StandingHeight, agent.supportId) ||
            !WorldGeometry::ClearBodySegment(world, first, second, GameplayConfig::AgentRadius, CombatConfig::StandingHeight) ||
            !WorldGeometry::ClearBodySegment(world, second, target, GameplayConfig::AgentRadius, CombatConfig::StandingHeight, ToSupport(candidate))) continue;
      } else if (!WorldGeometry::ClearBodySegment(world, current, target, GameplayConfig::AgentRadius, CombatConfig::StandingHeight, agent.supportId)) continue;
      selected = candidate; nearest = distance;
    }
    return selected;
  }
  inline bool CanTraverse(const WorldState &world, const Agent &agent, bool drop = false) { return Find(world, agent, drop).link != nullptr; }
  inline std::string_view Prompt(const WorldState &world, const Agent &agent, bool drop = false) {
    const auto selection = Find(world, agent, drop);
    if (!selection.link) return {};
    if (drop) return "Mayús+B · Soltarse";
    if (selection.link->kind == VerticalLinkKind::Climb) return "B · Subir al saliente";
    return agent.traversal.mode == TraversalMode::Grounded ? "B · Agarrarse" : "B · Cambiar de agarre";
  }
  inline sf::Vector3f Hands(const WorldState &world, const Agent &agent) {
    if (const auto *surface = WorldGeometry::FindGrip(world, agent.traversal.surfaceId)) return surface->start + (surface->end - surface->start) * agent.traversal.parameter;
    return {agent.position.x, agent.position.y, agent.elevation + TraversalConfig::HandHeight};
  }
}
