#pragma once

#include "NoTraceVisibility.h"
#include "NoTraceMission.h"
#include "NoTraceSpatialGeometry.h"
#include "NoTraceLevel.h"
#include "NoTraceFurniture.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace NoTrace::Interaction {
  inline const Interactable *FindObject(const WorldState &world, const Agent &agent) {
    const Interactable *nearest = nullptr;
    float distance = GameplayConfig::InteractionDistance;
    for (const auto &object : world.objects) {
      if (agent.traversal.mode != TraversalMode::Grounded || std::abs(agent.elevation - object.elevation) > LevelConfig::MaximumStepHeight || !WorldGeometry::Properties(object).interactive || (agent.draggingGuard >= 0 && WorldGeometry::HideoutType(object) == HideoutKind::None)) continue;
      const auto bounds = WorldGeometry::ObjectFootprint(object);
      const float candidate = WorldGeometry::DistanceToBounds(agent.position, bounds);
      if (candidate < distance && !WorldGeometry::RayOccluded(world, {agent.position.x, agent.position.y, agent.elevation + CombatConfig::StandingWeaponHeight}, {WorldGeometry::ClosestPoint(agent.position, bounds).x, WorldGeometry::ClosestPoint(agent.position, bounds).y, object.elevation + CombatConfig::StandingWeaponHeight}, object.id)) {
        nearest = &object; distance = candidate;
      }
    }
    return nearest;
  }

  inline const Guard *FindBody(const WorldState &world, const Agent &agent) {
    const Guard *nearest = nullptr;
    float distance = GameplayConfig::InteractionDistance;
    for (const auto &guard : world.guards) {
      if ((guard.state != GuardState::Dead && guard.state != GuardState::Incapacitated && !(IsTraining(world) && guard.trainingDummy && guard.heldBy == 0)) || guard.bodyHidden || guard.storedIn != 0) continue;
      const float candidate = WorldGeometry::SpatialDistance(WorldGeometry::Feet(agent), WorldGeometry::Feet(guard));
      if (std::abs(agent.elevation - guard.elevation) > LevelConfig::MaximumStepHeight || agent.traversal.mode != TraversalMode::Grounded) continue;
      if (candidate >= distance || std::ranges::any_of(world.agents, [&](const Agent &other) { return other.id != agent.id && other.draggingGuard == static_cast<int>(guard.id); })) continue;
      if (!WorldGeometry::ClearBodySegment(world, WorldGeometry::Feet(agent), WorldGeometry::Feet(guard), GameplayConfig::AgentRadius, CombatConfig::ProneHeight, agent.supportId)) continue;
      nearest = &guard; distance = candidate;
    }
    return nearest;
  }

  inline bool HideoutPathClear(const WorldState &world, const Interactable &object, sf::Vector2f from, sf::Vector2f to) {
    return WorldGeometry::ClearBodySegment(world, {from.x, from.y, object.elevation}, {to.x, to.y, object.elevation}, GameplayConfig::AgentRadius, CombatConfig::ProneHeight, object.supportId, object.id);
  }

  inline bool HideoutExit(const WorldState &world, const Agent &agent, const Interactable &object, sf::Vector2f &position) {
    const auto available = [&](sf::Vector2f candidate) {
      const auto support = WorldGeometry::FindSupport(world, candidate, object.elevation, object.supportId, GameplayConfig::AgentRadius);
      return support && !WorldGeometry::PositionBlocked(world, candidate, GameplayConfig::AgentRadius, support->elevation, CombatConfig::ProneHeight, support->id) && HideoutPathClear(world, object, object.position, candidate);
    };
    if (WorldGeometry::HideoutType(object) == HideoutKind::Closet) {
      for (const auto candidate : std::array{agent.hideoutExit, WorldGeometry::HideoutFrontPosition(object, GameplayConfig::AgentRadius)}) {
        if (WorldGeometry::AtHideoutEntrance(object, candidate, GameplayConfig::AgentRadius) && available(candidate)) { position = candidate; return true; }
      }
      return false;
    }
    if (WorldGeometry::HideoutType(object) == HideoutKind::UnderTable) {
      if (agent.facing.lengthSquared() <= 0.0001f) return false;
      const auto direction = agent.facing.normalized();
      const auto bounds = WorldGeometry::Expanded(WorldGeometry::ObjectFootprint(object), GameplayConfig::AgentRadius + SpatialConfig::CollisionSkin);
      const auto maximum = bounds.position + bounds.size;
      const float horizontal = direction.x > 0.f ? (maximum.x - agent.position.x) / direction.x : direction.x < 0.f ? (bounds.position.x - agent.position.x) / direction.x : std::numeric_limits<float>::infinity();
      const float vertical = direction.y > 0.f ? (maximum.y - agent.position.y) / direction.y : direction.y < 0.f ? (bounds.position.y - agent.position.y) / direction.y : std::numeric_limits<float>::infinity();
      const float distance = std::min(horizontal, vertical) + SpatialConfig::CollisionSkin;
      const auto candidate = agent.position + direction * distance;
      if (!std::isfinite(distance) || distance <= 0.f || !available(candidate)) return false;
      position = candidate; return true;
    }
    const auto bounds = WorldGeometry::ObjectFootprint(object);
    const auto minimum = bounds.position, maximum = minimum + bounds.size;
    const float radius = GameplayConfig::AgentRadius + SpatialConfig::CollisionSkin;
    for (const auto candidate : std::array<sf::Vector2f, 9>{{agent.hideoutExit, {minimum.x - radius, object.position.y}, {maximum.x + radius, object.position.y},
        {object.position.x, minimum.y - radius}, {object.position.x, maximum.y + radius}, minimum - sf::Vector2f{radius, radius}, maximum + sf::Vector2f{radius, radius},
        {minimum.x - radius, maximum.y + radius}, {maximum.x + radius, minimum.y - radius}}}) {
      if (available(candidate)) { position = candidate; return true; }
    }
    return false;
  }

  inline bool CanEnterHideout(const WorldState &world, const Agent &agent, const Interactable &object) {
    if (WorldGeometry::HideoutType(object) == HideoutKind::None || !WorldGeometry::Properties(object).interactive || object.lifted || object.overturned || std::abs(agent.elevation - object.elevation) > LevelConfig::MaximumStepHeight || agent.traversal.mode != TraversalMode::Grounded ||
        WorldGeometry::HideoutOccupied(object) || !WorldGeometry::AtHideoutEntrance(object, agent.position, GameplayConfig::AgentRadius) ||
        !HideoutPathClear(world, object, agent.position, object.position)) return false;
    if (agent.draggingGuard < 0) return true;
    const auto body = std::ranges::find_if(world.guards, [&](const Guard &guard) { return static_cast<int>(guard.id) == agent.draggingGuard; });
    return WorldGeometry::HideoutAllowsBody(object) && body != world.guards.end() && !body->trainingDummy &&
        (body->state == GuardState::Dead || body->state == GuardState::Incapacitated) && !body->bodyHidden && body->storedIn == 0 &&
        WorldGeometry::DistanceToBounds(body->position, WorldGeometry::ObjectFootprint(object)) <= GameplayConfig::InteractionDistance &&
        WorldGeometry::AtHideoutEntrance(object, body->position, GameplayConfig::AgentRadius) && HideoutPathClear(world, object, body->position, object.position);
  }

  inline std::string Prompt(const WorldState &world, const Agent &agent) {
    if (!agent.connected || !agent.alive || agent.incapacitated || agent.extracted || agent.maneuver.kind != ManeuverKind::None || agent.cqcHeldBy != 0 || agent.traversal.mode != TraversalMode::Grounded) return {};
    if (agent.staggerRemaining > 0.f && agent.hideoutId == 0) return {};
    if (agent.cqcHoldingGuard || agent.cqcHoldingAgent) return "F · Someter";
    if (agent.cqcPressPending || agent.cqcAttackTime > 0.f || agent.puzzle.target != 0 || agent.reloadRemaining > 0.f) return {};
    if (agent.hidden) {
      const auto hideout = std::ranges::find_if(world.objects, [&](const Interactable &object) { return object.id == agent.hideoutId && object.occupantAgent == agent.id; });
      sf::Vector2f exit;
      return hideout != world.objects.end() && HideoutExit(world, agent, *hideout, exit) ? "E · Salir" : "";
    }
    if (world.phase == GamePhase::Mission) for (const auto &other : world.agents) {
      if (other.id == agent.id || !other.alive || !other.incapacitated || !other.connected || other.incapacityRemaining <= 0.f ||
          WorldGeometry::SpatialDistance(WorldGeometry::Feet(agent), WorldGeometry::Feet(other)) >= GameplayConfig::InteractionDistance || std::abs(agent.elevation - other.elevation) > LevelConfig::MaximumStepHeight ||
          WorldGeometry::RayOccluded(world, {agent.position.x, agent.position.y, agent.elevation + CombatConfig::ProneHeight}, {other.position.x, other.position.y, other.elevation + CombatConfig::ProneHeight})) continue;
      return "E · Reanimar";
    }
    if (agent.draggingGuard < 0 && FindBody(world, agent)) return "E · Arrastrar";
    const auto *object = FindObject(world, agent);
    if (!object) return {};
    if (WorldGeometry::HideoutType(*object) != HideoutKind::None) {
      const bool enter = CanEnterHideout(world, agent, *object), flip = Furniture::CanFlipTable(world, agent, *object);
      if (flip) return enter ? "E · Ocultarse / T · Volcar mesa" : object->overturned ? "T · Levantar mesa" : "T · Volcar mesa";
      return enter ? agent.draggingGuard >= 0 ? "E · Ocultar cuerpo" : "E · Ocultarse" : "";
    }
    if (agent.draggingGuard >= 0) return {};
    if (object->puzzle.kind != PuzzleKind::None && !(object->kind == ObjectKind::Door && !object->locked)) {
      if (object->puzzle.parentDevice != 0) {
        const auto parent = std::ranges::find(world.objects, object->puzzle.parentDevice, &Interactable::id);
        if (parent == world.objects.end() || !parent->active || parent->puzzle.cooldown > 0.f || parent->puzzle.state == PuzzleState::Solved ||
            (parent->puzzle.family != PuzzleFamily::Levers && !(parent->puzzle.family == PuzzleFamily::Automatic && parent->puzzle.kind == PuzzleKind::Computer))) return {};
      }
      return object->active && object->puzzle.state != PuzzleState::Solved && object->puzzle.cooldown <= 0.f &&
          (object->user == 0 || object->user == agent.id || object->puzzle.kind == PuzzleKind::Coordination || object->puzzle.parentDevice != 0) ? "E · Operar" : "";
    }
    switch (object->kind) {
    case ObjectKind::Elevator: return Level::ElevatorPrompt(world, agent, *object);
    case ObjectKind::Camera: case ObjectKind::AlarmPanel: return object->active && agent.loadout.tools > 0 ? "T · Desactivar" : "";
    case ObjectKind::Terminal: return !object->completed ? "E · Recoger" : "";
    case ObjectKind::Secondary: return !object->completed ? "E · Recoger" : agent.privateOrder == PrivateOrder::CopyArchive && !agent.privateComplete ? "E · Copiar" : "";
    case ObjectKind::Extraction: return world.phase == GamePhase::Mission && agent.privateOrder != PrivateOrder::Traitor && world.objectives >= GetContract(world.contract).requiredObjectives ? "E · Extraer" : "";
    case ObjectKind::Operations: return agent.id == GameplayConfig::HostPlayerId ? "E · Operaciones" : "";
    case ObjectKind::Armory: return "E · Equipo";
    case ObjectKind::Intelligence: return "E · Inteligencia";
    case ObjectKind::Deployment: return "E · Despliegue";
    case ObjectKind::Door: return object->open ? "E · Cerrar" : "E · Abrir";
    case ObjectKind::TrainingReset: return IsTraining(world) ? "E · Reiniciar" : "";
    default: return {};
    }
  }
}
