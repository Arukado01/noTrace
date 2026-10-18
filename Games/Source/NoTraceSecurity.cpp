#include "NoTraceSimulation.h"
#include "Config/NoTraceSecurityConfig.h"
#include "NoTraceVisibility.h"
#include "NoTraceWorldGeometry.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace NoTrace {
  void Simulation::ResetSecurity() {
    world_.sounds.clear(); world_.decoys.clear();
    for (auto &agent : world_.agents) { CancelPuzzle(agent); CancelManeuver(agent); agent.maneuver = {}; agent.aiming = false; agent.friendlyIncapacitated = false; agent.detectionIndicatorRemaining = agent.confirmedIndicatorRemaining = 0.f; }
    securityContactAge_ = 0.f;
    reinforcementTimer_ = 0.f;
    securityLastKnown_ = {};
    securityHasClue_ = false;
    securityContactTick_ = 0;
    securityAlertChangeTick_ = 0;
    nextGuardId_ = 1;
    world_.securityActive = false;
    world_.securityAlert = SecurityAlert::Calm;
    world_.securityRemaining = 0.f;
    world_.reinforcementWaves = 0;
    world_.securityEntries.clear();
    for (auto &agent : world_.agents) { ReleaseCqc(agent); agent.staggerRemaining = 0.f; agent.detectionProgress = 0.f; agent.detectionConfirmed = false; }
  }

  void Simulation::RegisterSecurityContact(sf::Vector2f position) {
    if (!world_.securityActive) reinforcementTimer_ = SecurityConfig::InitialArrivalDelay;
    SetSecurityAlert(SecurityAlert::Red);
    securityHasClue_ = true;
    securityContactAge_ = 0.f;
    securityContactTick_ = world_.tick;
    securityLastKnown_ = position;
    for (auto &guard : world_.guards) if (guard.state != GuardState::Dead && guard.state != GuardState::Incapacitated && guard.state != GuardState::Alert && guard.heldBy == 0 && guard.bodyTargetKind == BodyTargetKind::None) {
      guard.lastKnown = position;
      if (guard.state == GuardState::Patrol || guard.state == GuardState::Retiring || guard.state == GuardState::Vigilance) { guard.state = GuardState::Search; guard.stateTime = 0.f; guard.navigation.clear(); guard.patrolPauseRemaining = 0.f; }
    }
  }

  void Simulation::SetSecurityAlert(SecurityAlert alert) {
    securityAlertChangeTick_ = world_.tick;
    world_.securityAlert = alert;
    world_.securityActive = alert == SecurityAlert::Red || alert == SecurityAlert::Orange;
    world_.alarm = world_.securityActive;
    world_.securityRemaining = alert == SecurityAlert::Orange ? SecurityConfig::SearchDuration : alert == SecurityAlert::Yellow ? SecurityConfig::VigilanceDuration : 0.f;
    alarmRemaining_ = world_.securityRemaining;
    if (alert != SecurityAlert::Yellow && alert != SecurityAlert::Calm) return;
    for (auto &guard : world_.guards) if (guard.state != GuardState::Dead && guard.state != GuardState::Incapacitated && guard.heldBy == 0 && guard.bodyTargetKind == BodyTargetKind::None) {
      for (auto &object : world_.objects) if (object.id == guard.knownHideout && !object.lifted) object.inspectionProgress = 0.f;
      guard.knownHideout = 0; guard.hideoutInspectionTime = 0.f;
      guard.state = guard.reinforcement ? GuardState::Retiring : alert == SecurityAlert::Yellow ? GuardState::Vigilance : GuardState::Patrol;
      guard.stateTime = 0.f; guard.suspicion = 0.f; guard.observedAgent = 0; guard.navigation.clear(); guard.patrolPauseRemaining = 0.f;
      if (alert == SecurityAlert::Calm) guard.lastKnown = guard.position;
    }
  }

  void Simulation::RegisterBodyDiscovery(Guard &observer, sf::Vector2f position, std::uint32_t body) {
    const bool inactive = !world_.securityActive;
    if (inactive) { reinforcementTimer_ = SecurityConfig::InitialArrivalDelay; ++world_.alarmCount; }
    securityHasClue_ = true;
    if (world_.securityAlert != SecurityAlert::Red) { securityLastKnown_ = position; securityContactAge_ = 0.f; SetSecurityAlert(SecurityAlert::Orange); }
    const auto clue = world_.securityAlert == SecurityAlert::Red ? securityLastKnown_ : position;
    Record(EventKind::BodyFound, 0, body);
    for (auto &guard : world_.guards) if (guard.state != GuardState::Dead && guard.state != GuardState::Incapacitated && guard.heldBy == 0 && guard.bodyTargetKind == BodyTargetKind::None && guard.state != GuardState::Alert) {
      guard.lastKnown = clue; guard.state = GuardState::Search; guard.stateTime = 0.f; guard.navigation.clear(); guard.patrolPauseRemaining = 0.f;
    }
    observer.lastKnown = clue;
    world_.message = world_.securityAlert == SecurityAlert::Red ? "Cuerpo inspeccionado y comunicado: continúa la respuesta al contacto confirmado." : "Cuerpo inspeccionado y comunicado: alerta naranja, búsqueda distribuida en la zona.";
  }

  bool Simulation::UpdateBodyInvestigation(Guard &guard, float dt) {
    if (guard.bodyTargetKind == BodyTargetKind::None) return false;
    sf::Vector2f position;
    bool *reported = nullptr;
    if (guard.bodyTargetKind == BodyTargetKind::Guard) {
      const auto body = std::ranges::find_if(world_.guards, [&guard](const Guard &other) { return other.id == guard.bodyTargetId; });
      if (body != world_.guards.end() && (body->state == GuardState::Dead || body->state == GuardState::Incapacitated) && !body->bodyHidden && body->storedIn == 0) { position = body->position; reported = &body->bodyReported; }
    } else {
      const auto body = std::ranges::find_if(world_.agents, [&guard](const Agent &other) { return other.id == guard.bodyTargetId; });
      if (body != world_.agents.end() && body->connected && (!body->alive || body->incapacitated) && !body->hidden && body->hideoutId == 0) { position = body->position; reported = &body->bodyReported; }
    }
    if (!reported || *reported) {
      guard.bodyTargetKind = BodyTargetKind::None; guard.bodyTargetId = 0; guard.bodyInspectionTime = 0.f; guard.state = GuardState::Search; guard.stateTime = 0.f;
      return false;
    }
    guard.lastKnown = position; guard.patrolPauseRemaining = 0.f; guard.attackTime = 0.f;
    const float distance = (position - guard.position).length();
    if (distance > SecurityConfig::BodyInspectionReach || !Visibility::HasLineOfSight(world_, guard.position, position)) {
      guard.state = GuardState::ApproachBody; guard.bodyInspectionTime = 0.f;
      auto approach = position;
      float nearest = std::numeric_limits<float>::max();
      for (unsigned int sector = 0; sector < 16; ++sector) {
        const float angle = static_cast<float>(sector) * 0.3926990817f;
        const auto candidate = position + sf::Vector2f{std::cos(angle), std::sin(angle)} * SecurityConfig::BodyApproachDistance;
        const float candidateDistance = (candidate - guard.position).lengthSquared();
        if (candidateDistance < nearest && !Blocked(candidate, GameplayConfig::AgentRadius) && Visibility::HasLineOfSight(world_, candidate, position)) { nearest = candidateDistance; approach = candidate; }
      }
      if (nearest != std::numeric_limits<float>::max()) Navigate(guard, approach, GameplayConfig::GuardSpeed, dt);
      return true;
    }
    guard.state = GuardState::InspectBody; guard.navigation.clear(); guard.stepTime = 0.f;
    if (distance > 0.01f) guard.facing = (position - guard.position) / distance;
    guard.bodyInspectionTime = std::min(SecurityConfig::BodyInspectionDuration, guard.bodyInspectionTime + dt);
    if (guard.bodyInspectionTime >= SecurityConfig::BodyInspectionDuration) {
      *reported = true;
      const auto body = guard.bodyTargetId;
      guard.bodyTargetKind = BodyTargetKind::None; guard.bodyTargetId = 0; guard.bodyInspectionTime = 0.f;
      RegisterBodyDiscovery(guard, position, body);
    }
    return true;
  }

  sf::Vector2f Simulation::SecuritySearchDestination(const Guard &guard) const {
    if (!securityHasClue_ || world_.securityAlert == SecurityAlert::Calm) return guard.lastKnown;
    const auto phase = static_cast<unsigned int>(guard.stateTime / SecurityConfig::SearchRouteInterval);
    if ((phase + guard.id) % 3u == 1u && !world_.objects.empty()) {
      for (std::size_t index = 0; index < world_.objects.size(); ++index) {
        const auto &object = world_.objects[(index + guard.id) % world_.objects.size()];
        if (WorldGeometry::HideoutType(object) == HideoutKind::None || (object.position - guard.lastKnown).length() > SecurityConfig::SearchAreaRadius) continue;
        const auto normal = WorldGeometry::HideoutFrontNormal(object);
        const auto size = WorldGeometry::ObjectFootprint(object).size;
        const sf::Vector2f tangent{-normal.y, normal.x};
        const float lane = (static_cast<float>(guard.id % 5u) - 2.f) * GameplayConfig::AgentRadius * 2.5f;
        const auto approach = object.position + normal * ((std::abs(normal.x) * size.x + std::abs(normal.y) * size.y) * 0.5f + GameplayConfig::AgentRadius + 8.f) + tangent * lane;
        if (!Blocked(approach, GameplayConfig::AgentRadius)) return approach;
      }
    }
    constexpr std::array<sf::Vector2f, 8> directions{{{1.f, 0.f}, {0.70710678f, 0.70710678f}, {0.f, 1.f}, {-0.70710678f, 0.70710678f}, {-1.f, 0.f}, {-0.70710678f, -0.70710678f}, {0.f, -1.f}, {0.70710678f, -0.70710678f}}};
    for (std::size_t attempt = 0; attempt < directions.size(); ++attempt) {
      const auto sector = (guard.id + phase * 3u + attempt) % directions.size();
      const float radius = SecurityConfig::SearchProbeDistance + static_cast<float>(guard.id / directions.size()) * SecurityConfig::SearchProbeRingSpacing;
      const auto candidate = guard.lastKnown + directions[sector] * radius;
      if (!Blocked(candidate, GameplayConfig::AgentRadius)) return candidate;
    }
    return guard.lastKnown;
  }

  bool Simulation::SecurityEntryValid(const SecurityEntry &entry) const {
    if (Blocked(entry.position, GameplayConfig::AgentRadius) || Blocked(entry.rally, GameplayConfig::AgentRadius)) return false;
    bool blocked = false;
    WorldGeometry::ForEachSolid(world_, [&](const sf::FloatRect &bounds) {
      blocked |= WorldGeometry::SegmentRect(entry.position, entry.rally, WorldGeometry::Expanded(bounds, GameplayConfig::AgentRadius));
    });
    if (blocked) return false;
    for (const auto &agent : world_.agents) if (agent.connected && agent.alive && !agent.extracted) {
      if ((agent.position - entry.position).length() < SecurityConfig::MinimumSpawnAgentDistance || Visibility::IsVisible(world_, agent, entry.position)) return false;
    }
    return std::ranges::none_of(world_.guards, [&entry](const Guard &guard) { return (guard.position - entry.position).length() < GameplayConfig::AgentRadius * 2.5f; });
  }

  bool Simulation::SpawnReinforcementWave() {
    using namespace SecurityConfig;
    const auto active = std::ranges::count_if(world_.guards, [](const Guard &guard) { return guard.reinforcement && guard.state != GuardState::Dead && guard.state != GuardState::Incapacitated; });
    if (static_cast<std::size_t>(active) >= MaximumActiveReinforcements || world_.reinforcementWaves >= MaximumWaves || !securityHasClue_) return false;
    const auto available = std::min(GuardsPerWave, MaximumActiveReinforcements - static_cast<std::size_t>(active));
    std::size_t spawned = 0;
    for (const auto &entry : world_.securityEntries) {
      if (spawned >= available) break;
      if (!SecurityEntryValid(entry)) continue;
      while (std::ranges::any_of(world_.guards, [this](const Guard &guard) { return guard.id == nextGuardId_; })) ++nextGuardId_;
      Guard guard;
      guard.id = nextGuardId_++;
      guard.position = entry.position;
      const auto facing = entry.rally - entry.position;
      if (facing.lengthSquared() > 0.0001f) guard.facing = facing / facing.length();
      guard.state = GuardState::Search;
      guard.lastKnown = securityLastKnown_;
      guard.reinforcement = true;
      guard.reinforcementExit = entry.position;
      guard.patrol = {entry.rally, entry.position};
      world_.guards.push_back(std::move(guard));
      EmitSound(SoundKind::Reinforcement, entry.position, DevicesConfig::ExplosionNoiseRadius);
      ++spawned;
    }
    if (spawned == 0) return false;
    ++world_.reinforcementWaves;
    world_.message = "La central de seguridad envía refuerzos por los accesos de servicio.";
    return true;
  }

  void Simulation::UpdateSecurity(float dt) {
    using namespace SecurityConfig;
    if (!securityHasClue_) return;
    securityContactAge_ += dt;
    if (securityContactTick_ != world_.tick) {
      if (world_.securityAlert == SecurityAlert::Red) {
        SetSecurityAlert(SecurityAlert::Orange);
        for (auto &guard : world_.guards) if (guard.state == GuardState::Patrol || guard.state == GuardState::Retiring || guard.state == GuardState::Vigilance) {
          guard.state = GuardState::Search; guard.stateTime = 0.f; guard.lastKnown = securityLastKnown_; guard.navigation.clear();
        }
        world_.message = "Contacto perdido: alerta naranja, veinte segundos de búsqueda.";
      } else if (securityAlertChangeTick_ != world_.tick && (world_.securityAlert == SecurityAlert::Orange || world_.securityAlert == SecurityAlert::Yellow)) {
        world_.securityRemaining = std::max(0.f, world_.securityRemaining - dt);
        if (world_.securityRemaining <= 0.00001f) {
          if (world_.securityAlert == SecurityAlert::Orange) {
            SetSecurityAlert(SecurityAlert::Yellow);
            world_.message = "Alerta amarilla: doce segundos de vigilancia; los refuerzos se retiran.";
          } else {
            SetSecurityAlert(SecurityAlert::Calm);
            world_.message = "Zona asegurada: las patrullas recuperan sus rutas normales.";
          }
        }
      }
    }
    alarmRemaining_ = world_.securityRemaining;
    if (world_.securityActive) {
      reinforcementTimer_ = std::max(0.f, reinforcementTimer_ - dt);
      if (reinforcementTimer_ <= 0.f && world_.reinforcementWaves < MaximumWaves) reinforcementTimer_ = SpawnReinforcementWave() ? ReinforcementInterval : EntryRetryDelay;
    }
    for (auto &guard : world_.guards) if (guard.reinforcement && guard.state == GuardState::Retiring) {
      const auto validExit = [this](sf::Vector2f exit) {
        return !Blocked(exit, GameplayConfig::AgentRadius) && std::ranges::any_of(world_.securityEntries, [exit](const SecurityEntry &entry) { return entry.position == exit; });
      };
      if (!validExit(guard.reinforcementExit)) {
        float nearest = std::numeric_limits<float>::max();
        for (const auto &entry : world_.securityEntries) if (validExit(entry.position)) {
          const float distance = (entry.position - guard.position).lengthSquared();
          if (distance < nearest) { nearest = distance; guard.reinforcementExit = entry.position; guard.navigation.clear(); }
        }
      }
    }
    std::erase_if(world_.guards, [this](const Guard &guard) {
      if (!guard.reinforcement || guard.state != GuardState::Retiring || (guard.position - guard.reinforcementExit).length() > ExitArrivalDistance || Blocked(guard.reinforcementExit, GameplayConfig::AgentRadius)) return false;
      if (std::ranges::none_of(world_.securityEntries, [&guard](const SecurityEntry &entry) { return entry.position == guard.reinforcementExit; })) return false;
      for (const auto &agent : world_.agents) if (agent.connected && agent.alive && !agent.extracted && Visibility::GuardVisible(world_, agent, guard)) return false;
      return true;
    });
  }
}
