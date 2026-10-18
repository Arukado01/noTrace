#include "NoTraceSimulation.h"
#include "NoTraceWorldGeometry.h"
#include "NoTraceInteraction.h"
#include <algorithm>
#include <array>
#include <limits>

namespace NoTrace {
  void Simulation::ToggleHideoutOpening(Agent &agent) {
    const auto object = std::ranges::find_if(world_.objects, [&agent](const Interactable &entry) { return entry.id == agent.hideoutId && entry.occupantAgent == agent.id; });
    if (object == world_.objects.end() || WorldGeometry::HideoutType(*object) != HideoutKind::Closet) return;
    object->open = !object->open;
    EmitSound(object->open ? SoundKind::CabinetOpen : SoundKind::CabinetClose, object->position, GameplayConfig::WalkingNoiseRadius, agent.id, object->elevation);
    world_.message = object->open ? "Puerta entreabierta: visión limitada por la abertura frontal." : "Puerta cerrada: el armario no permite observar el exterior.";
  }

  void Simulation::ObserveHideoutEntry(const Agent &agent, const Interactable &object) {
    if (world_.phase != GamePhase::Mission || WorldGeometry::HideoutType(object) != HideoutKind::UnderTable) return;
    for (auto &guard : world_.guards) {
      if (guard.bodyHidden || guard.state == GuardState::Dead || guard.state == GuardState::Incapacitated || guard.heldBy != 0 || guard.cqcState != CqcState::None) continue;
      if (guard.observedAgent != agent.id) continue;
      if (guard.state != GuardState::Alert && guard.suspicion < 1.f) continue;
      const float range = GameplayConfig::VisionDistance * (world_.alarm ? GameplayConfig::AlarmVisionMultiplier : 1.f);
      if (!CanSee(guard.position, guard.facing, agent, range, guard.elevation)) continue;
      guard.knownHideout = object.id;
      guard.hideoutInspectionTime = 0.f;
      guard.lastKnown = agent.position; guard.lastKnownElevation = agent.elevation;
      guard.navigation.clear();
    }
  }

  bool Simulation::UpdateHideoutInspection(Guard &guard, float dt) {
    if (guard.knownHideout == 0) return false;
    const auto object = std::ranges::find_if(world_.objects, [&guard](const Interactable &entry) { return entry.id == guard.knownHideout; });
    if (object == world_.objects.end() || WorldGeometry::HideoutType(*object) != HideoutKind::UnderTable || object->overturned) {
      guard.knownHideout = 0; guard.hideoutInspectionTime = 0.f; return false;
    }
    if (guard.bodyHidden || guard.heldBy != 0 || guard.cqcState != CqcState::None || guard.state == GuardState::Dead || guard.state == GuardState::Incapacitated) {
      guard.hideoutInspectionTime = 0.f; object->inspectionProgress = 0.f; return false;
    }
    auto overturned = *object; overturned.overturned = true; overturned.lifted = false; overturned.footprint = {};
    const auto bounds = WorldGeometry::ObjectFootprint(overturned);
    const auto end = bounds.position + bounds.size;
    const float radius = GameplayConfig::AgentRadius + SpatialConfig::CollisionSkin;
    const std::array<sf::Vector2f, 4> candidates{{{bounds.position.x - radius, object->position.y}, {end.x + radius, object->position.y},
      {object->position.x, bounds.position.y - radius}, {object->position.x, end.y + radius}}};
    sf::Vector2f approach;
    float nearest = std::numeric_limits<float>::max();
    for (const auto candidate : candidates) {
      const float distance = (candidate - guard.position).lengthSquared();
      if (distance < nearest && !WorldGeometry::PositionBlocked(world_, candidate, GameplayConfig::AgentRadius, object->elevation, CombatConfig::StandingHeight, object->supportId) && HideoutPathClear(*object, candidate, object->position)) { approach = candidate; nearest = distance; }
    }
    if (nearest == std::numeric_limits<float>::max()) { guard.hideoutInspectionTime = 0.f; object->inspectionProgress = 0.f; return false; }
    guard.state = GuardState::Search;
    guard.lastKnown = approach;
    if (std::abs(guard.elevation - object->elevation) > LevelConfig::SupportTolerance || nearest > SpatialConfig::HideoutInspectionReach * SpatialConfig::HideoutInspectionReach) {
      guard.hideoutInspectionTime = 0.f; object->inspectionProgress = 0.f;
      Navigate(guard, approach, GameplayConfig::GuardSpeed, dt, object->elevation);
      return true;
    }
    const auto facing = object->position - guard.position;
    if (facing.lengthSquared() > 0.f) guard.facing = facing.normalized();
    guard.navigation.clear(); guard.attackTime = 0.f; guard.meleeAttack = false;
    guard.hideoutInspectionTime = std::min(SpatialConfig::HideoutInspectionDuration, guard.hideoutInspectionTime + dt);
    object->inspectionProgress = std::max(object->inspectionProgress, guard.hideoutInspectionTime / SpatialConfig::HideoutInspectionDuration);
    if (guard.hideoutInspectionTime < SpatialConfig::HideoutInspectionDuration) return true;
    const auto occupant = std::ranges::find_if(world_.agents, [&object](const Agent &agent) { return agent.id == object->occupantAgent; });
    sf::Vector2f departure;
    const bool occupied = occupant != world_.agents.end() && occupant->hideoutId == object->id;
    if ((occupied && !HideoutExit(*occupant, overturned, departure)) ||
        !Furniture::TablePlacementClear(world_, *object, true, occupied ? occupant->id : 0, departure)) return true;
    object->lifted = false; object->overturned = true; object->footprint = {}; object->inspectionProgress = 1.f;
    EmitSound(SoundKind::TableFlip, object->position, GameplayConfig::WalkingNoiseRadius, 0, object->elevation);
    if (occupant != world_.agents.end() && occupant->hideoutId == object->id) {
      occupant->position = departure;
      occupant->hideoutId = 0; occupant->hideoutExit = {}; occupant->hidden = false; occupant->cover = {}; occupant->decoyGuide = {};
      guard.lastKnown = occupant->position;
      if (CanSee(guard.position, guard.facing, *occupant, GameplayConfig::VisionDistance, guard.elevation)) {
        if (guard.observedAgent != occupant->id) Detect(*occupant, guard.id);
        guard.state = GuardState::Alert; guard.suspicion = 1.f; guard.observedAgent = occupant->id;
      }
    }
    object->occupantAgent = 0; object->occupied = object->occupantGuard != 0;
    guard.knownHideout = 0; guard.hideoutInspectionTime = 0.f;
    world_.message = "Una patrulla ha volcado la mesa tras presenciar la entrada.";
    return true;
  }
}
