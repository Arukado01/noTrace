#include "NoTraceSimulation.h"
#include "NoTracePerformance.h"
#include "Config/NoTraceSecurityConfig.h"
#include "Config/NoTraceNavigationConfig.h"
#include "NoTraceVisibility.h"
#include "NoTraceWorldGeometry.h"
#include "NoTraceSpatialGeometry.h"
#include "NoTraceNavigation.h"
#include "Config/NoTraceAudioConfig.h"
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
    securityLastKnownElevation_ = 0.f;
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

  void Simulation::RegisterSecurityContact(sf::Vector2f position, float elevation) {
    if (!world_.securityActive) reinforcementTimer_ = SecurityConfig::InitialArrivalDelay;
    SetSecurityAlert(SecurityAlert::Red);
    securityHasClue_ = true;
    securityContactAge_ = 0.f;
    securityContactTick_ = world_.tick;
    securityLastKnown_ = position;
    securityLastKnownElevation_ = elevation;
    for (auto &guard : world_.guards) if (guard.state != GuardState::Dead && guard.state != GuardState::Incapacitated && guard.state != GuardState::Alert && guard.heldBy == 0 && guard.bodyTargetKind == BodyTargetKind::None) {
      guard.lastKnown = position; guard.lastKnownElevation = elevation;
      if (guard.state == GuardState::Patrol || guard.state == GuardState::Retiring || guard.state == GuardState::Vigilance) { guard.state = GuardState::Search; guard.stateTime = 0.f; guard.navigation.clear(); guard.patrolPauseRemaining = 0.f; }
    }
  }

  void Simulation::SetSecurityAlert(SecurityAlert alert) {
    if (world_.securityAlert == alert) return;
    securityAlertChangeTick_ = world_.tick;
    world_.securityAlert = alert;
    world_.securityActive = alert == SecurityAlert::Red || alert == SecurityAlert::Orange;
    world_.alarm = world_.securityActive;
    world_.securityRemaining = alert == SecurityAlert::Orange ? SecurityConfig::SearchDuration : alert == SecurityAlert::Yellow ? SecurityConfig::VigilanceDuration : 0.f;
    alarmRemaining_ = world_.securityRemaining;
    const auto cue = alert == SecurityAlert::Red ? SoundKind::DetectionConfirmed : alert == SecurityAlert::Orange ? SoundKind::SearchStart :
        alert == SecurityAlert::Yellow ? SoundKind::VigilanceStart : SoundKind::AlertEnd;
    EmitSound(cue, world_.size * .5f, world_.size.length(), 0, 0.f);
    if (alert != SecurityAlert::Yellow && alert != SecurityAlert::Calm) return;
    for (auto &guard : world_.guards) if (guard.state != GuardState::Dead && guard.state != GuardState::Incapacitated && guard.heldBy == 0 && guard.bodyTargetKind == BodyTargetKind::None) {
      for (auto &object : world_.objects) if (object.id == guard.knownHideout && !object.lifted) object.inspectionProgress = 0.f;
      guard.knownHideout = 0; guard.hideoutInspectionTime = 0.f;
      guard.state = guard.reinforcement ? GuardState::Retiring : alert == SecurityAlert::Yellow ? GuardState::Vigilance : GuardState::Patrol;
      guard.stateTime = 0.f; guard.suspicion = 0.f; guard.observedAgent = 0; guard.navigation.clear(); guard.patrolPauseRemaining = 0.f;
      if (alert == SecurityAlert::Calm) { guard.lastKnown = guard.position; guard.lastKnownElevation = guard.elevation; }
    }
  }

  void Simulation::RegisterBodyDiscovery(Guard &observer, sf::Vector2f position, std::uint32_t body, float elevation) {
    const bool inactive = !world_.securityActive;
    if (inactive) { reinforcementTimer_ = SecurityConfig::InitialArrivalDelay; ++world_.alarmCount; }
    securityHasClue_ = true;
    if (world_.securityAlert != SecurityAlert::Red) {
      securityLastKnown_ = position; securityLastKnownElevation_ = elevation; securityContactAge_ = 0.f;
      if (world_.securityAlert == SecurityAlert::Orange) {
        world_.securityRemaining = SecurityConfig::SearchDuration; alarmRemaining_ = world_.securityRemaining; securityAlertChangeTick_ = world_.tick;
      } else SetSecurityAlert(SecurityAlert::Orange);
    }
    const auto clue = world_.securityAlert == SecurityAlert::Red ? securityLastKnown_ : position;
    const float clueElevation = world_.securityAlert == SecurityAlert::Red ? securityLastKnownElevation_ : elevation;
    Record(EventKind::BodyFound, 0, body);
    for (auto &guard : world_.guards) if (guard.state != GuardState::Dead && guard.state != GuardState::Incapacitated && guard.heldBy == 0 && guard.bodyTargetKind == BodyTargetKind::None && guard.state != GuardState::Alert) {
      guard.lastKnown = clue; guard.lastKnownElevation = clueElevation; guard.state = GuardState::Search; guard.stateTime = 0.f; guard.navigation.clear(); guard.patrolPauseRemaining = 0.f;
    }
    observer.lastKnown = clue; observer.lastKnownElevation = clueElevation;
    world_.message = world_.securityAlert == SecurityAlert::Red ? "Cuerpo inspeccionado y comunicado: continúa la respuesta al contacto confirmado." : "Cuerpo inspeccionado y comunicado: alerta naranja, búsqueda distribuida en la zona.";
  }

  void Simulation::WakeGuard(Guard &guard) {
    if (guard.state != GuardState::Incapacitated || guard.health <= 0.f || guard.bodyHidden || guard.storedIn != 0) return;
    guard.state = world_.securityActive ? GuardState::Search : guard.reinforcement ? GuardState::Retiring :
        world_.securityAlert == SecurityAlert::Yellow ? GuardState::Vigilance : GuardState::Patrol;
    guard.stateTime = 0.f; guard.incapacityRemaining = 0.f; guard.stun = 0.f; guard.staggerRemaining = 0.f;
    guard.wakeRemaining = GameplayConfig::StanceTransitionDuration;
    guard.heldBy = 0; guard.cqcState = CqcState::None; guard.bodyReported = false; guard.observedAgent = 0;
    guard.suspicion = 0.f; guard.indicatorProgress = 0.f; guard.suspicionRemaining = 0.f; guard.detectedTime = 0.f;
    guard.attackTime = 0.f; guard.attackCooldown = GameplayConfig::GuardAttackInterval; guard.meleeAttack = false;
    guard.navigation.clear(); guard.navigationTimer = 0.f; guard.navigationStuckTime = 0.f; guard.navigationFailures = 0; guard.navigationRejectTime = 0.f;
    guard.patrolPauseRemaining = 0.f; guard.stepTime = 0.f; guard.knownHideout = 0; guard.hideoutInspectionTime = 0.f;
    guard.bodyTargetKind = BodyTargetKind::None; guard.bodyTargetId = 0; guard.bodyInspectionTime = 0.f;
    guard.lastKnown = securityHasClue_ && world_.securityAlert != SecurityAlert::Calm ? securityLastKnown_ : guard.position;
    guard.lastKnownElevation = securityHasClue_ && world_.securityAlert != SecurityAlert::Calm ? securityLastKnownElevation_ : guard.elevation;
    for (auto &agent : world_.agents) if (agent.draggingGuard == static_cast<int>(guard.id)) agent.draggingGuard = -1;
    EmitSound(SoundKind::Wake, guard.position, AudioConfig::QuietRadius, 0, guard.elevation);
  }

  bool Simulation::UpdateBodyInvestigation(Guard &guard, float dt) {
    if (guard.bodyTargetKind == BodyTargetKind::None) return false;
    sf::Vector2f position;
    float elevation = 0.f;
    std::uint32_t support = 0;
    bool *reported = nullptr;
    Guard *unconscious = nullptr;
    if (guard.bodyTargetKind == BodyTargetKind::Guard) {
      const auto body = std::ranges::find_if(world_.guards, [&guard](const Guard &other) { return other.id == guard.bodyTargetId; });
      if (body != world_.guards.end() && (body->state == GuardState::Dead || body->state == GuardState::Incapacitated) && !body->bodyHidden && body->storedIn == 0) {
        position = body->position; elevation = body->elevation; support = body->supportId; reported = &body->bodyReported;
        if (body->state == GuardState::Incapacitated && body->health > 0.f && body->heldBy == 0 &&
            std::ranges::none_of(world_.agents, [&body](const Agent &agent) { return agent.draggingGuard == static_cast<int>(body->id); })) unconscious = &*body;
      }
    } else {
      const auto body = std::ranges::find_if(world_.agents, [&guard](const Agent &other) { return other.id == guard.bodyTargetId; });
      if (body != world_.agents.end() && body->connected && (!body->alive || body->incapacitated) && !body->hidden && body->hideoutId == 0) { position = body->position; elevation = body->elevation; support = body->supportId; reported = &body->bodyReported; }
    }
    if (!reported || (*reported && !unconscious)) {
      guard.bodyTargetKind = BodyTargetKind::None; guard.bodyTargetId = 0; guard.bodyInspectionTime = 0.f; guard.state = GuardState::Search; guard.stateTime = 0.f;
      return false;
    }
    guard.lastKnown = position; guard.lastKnownElevation = elevation; guard.patrolPauseRemaining = 0.f; guard.attackTime = 0.f;
    const auto postpone = [&] {
      guard.bodyTargetKind = BodyTargetKind::None; guard.bodyTargetId = 0; guard.bodyInspectionTime = 0.f;
      guard.state = world_.securityActive ? GuardState::Search : guard.reinforcement ? GuardState::Retiring :
          world_.securityAlert == SecurityAlert::Yellow ? GuardState::Vigilance : GuardState::Patrol;
      guard.stateTime = 0.f; guard.navigation.clear(); guard.navigationTimer = 0.f;
      guard.lastKnown = securityHasClue_ && world_.securityAlert != SecurityAlert::Calm ? securityLastKnown_ : guard.position;
      guard.lastKnownElevation = securityHasClue_ && world_.securityAlert != SecurityAlert::Calm ? securityLastKnownElevation_ : guard.elevation;
    };
    const sf::Vector3f bodyPosition{position.x, position.y, elevation};
    const float distance = WorldGeometry::SpatialDistance(WorldGeometry::Feet(guard), bodyPosition);
    if (distance > SecurityConfig::BodyInspectionReach || std::abs(elevation - guard.elevation) > LevelConfig::MaximumStepHeight ||
        !WorldGeometry::ClearBodySegment(world_, WorldGeometry::Feet(guard), bodyPosition, GameplayConfig::AgentRadius, CombatConfig::StandingHeight, guard.supportId)) {
      guard.state = GuardState::ApproachBody; guard.bodyInspectionTime = 0.f;
      auto approach = position;
      float nearest = std::numeric_limits<float>::max();
      for (unsigned int sector = 0; sector < 16; ++sector) {
        const float angle = static_cast<float>(sector) * 0.3926990817f;
        const auto candidate = position + sf::Vector2f{std::cos(angle), std::sin(angle)} * SecurityConfig::BodyApproachDistance;
        const float candidateDistance = (candidate - guard.position).lengthSquared();
        const auto candidateSupport = WorldGeometry::FindSupport(world_, candidate, elevation, support, GameplayConfig::AgentRadius);
        if (candidateDistance < nearest && candidateSupport && !WorldGeometry::PositionBlocked(world_, candidate, GameplayConfig::AgentRadius, candidateSupport->elevation, CombatConfig::StandingHeight, candidateSupport->id) &&
            WorldGeometry::ClearBodySegment(world_, {candidate.x, candidate.y, candidateSupport->elevation}, bodyPosition, GameplayConfig::AgentRadius, CombatConfig::StandingHeight, candidateSupport->id)) { nearest = candidateDistance; approach = candidate; }
      }
      if (nearest == std::numeric_limits<float>::max()) {
        guard.navigationRejectedTarget = position; guard.navigationRecoveryTarget = guard.position; guard.navigationRejectTime = NavigationConfig::RejectDuration;
        postpone();
        return false;
      }
      const auto currentSupport = WorldGeometry::FindSupport(world_, guard.position, guard.elevation, guard.supportId, GameplayConfig::AgentRadius);
      const auto targetSupport = WorldGeometry::FindSupport(world_, approach, elevation, support, GameplayConfig::AgentRadius);
      if (!currentSupport || !targetSupport || (currentSupport->id != targetSupport->id && Navigation::Route(world_, currentSupport->id, targetSupport->id).empty())) {
        guard.navigationRejectedTarget = position; guard.navigationRecoveryTarget = guard.position; guard.navigationRejectTime = NavigationConfig::RejectDuration;
        postpone();
        return false;
      }
      Navigate(guard, approach, GameplayConfig::GuardSpeed, dt, elevation);
      if (guard.navigationRejectTime > 0.f && (guard.navigationRejectedTarget - approach).length() <= NavigationConfig::CellSize) {
        postpone();
        return false;
      }
      return true;
    }
    guard.state = GuardState::InspectBody; guard.navigation.clear(); guard.stepTime = 0.f;
    const auto direction = position - guard.position;
    if (direction.lengthSquared() > 0.0001f) guard.facing = direction.normalized();
    guard.bodyInspectionTime = std::min(SecurityConfig::BodyInspectionDuration, guard.bodyInspectionTime + dt);
    if (guard.bodyInspectionTime >= SecurityConfig::BodyInspectionDuration) {
      const bool discovered = !*reported;
      *reported = true;
      const auto body = guard.bodyTargetId;
      guard.bodyTargetKind = BodyTargetKind::None; guard.bodyTargetId = 0; guard.bodyInspectionTime = 0.f;
      if (discovered) RegisterBodyDiscovery(guard, position, body, elevation);
      if (unconscious) {
        WakeGuard(*unconscious);
        guard.state = world_.securityActive ? GuardState::Search : guard.reinforcement ? GuardState::Retiring :
            world_.securityAlert == SecurityAlert::Yellow ? GuardState::Vigilance : GuardState::Patrol;
        guard.stateTime = 0.f; guard.navigation.clear();
        guard.lastKnown = securityHasClue_ && world_.securityAlert != SecurityAlert::Calm ? securityLastKnown_ : guard.position;
        guard.lastKnownElevation = securityHasClue_ && world_.securityAlert != SecurityAlert::Calm ? securityLastKnownElevation_ : guard.elevation;
        world_.message = "Guardia asistido: el compañero vuelve al servicio sin reiniciar la respuesta de seguridad.";
      }
    }
    return true;
  }

  sf::Vector2f Simulation::SecuritySearchDestination(const Guard &guard) const {
    if (!securityHasClue_ || world_.securityAlert == SecurityAlert::Calm) return guard.lastKnown;
    const auto accessible = [&](sf::Vector2f point) {
      const auto support = WorldGeometry::FindSupport(world_, point, guard.lastKnownElevation, 0, GameplayConfig::AgentRadius);
      return support && !WorldGeometry::PositionBlocked(world_, point, GameplayConfig::AgentRadius, support->elevation, CombatConfig::StandingHeight, support->id);
    };
    const auto phase = static_cast<unsigned int>(guard.stateTime / SecurityConfig::SearchRouteInterval);
    if ((phase + guard.id) % 3u == 1u && !world_.objects.empty()) {
      for (std::size_t index = 0; index < world_.objects.size(); ++index) {
        const auto &object = world_.objects[(index + guard.id) % world_.objects.size()];
        if (WorldGeometry::HideoutType(object) == HideoutKind::None || std::abs(object.elevation - guard.lastKnownElevation) > LevelConfig::MaximumStepHeight || (object.position - guard.lastKnown).length() > SecurityConfig::SearchAreaRadius) continue;
        const auto normal = WorldGeometry::HideoutFrontNormal(object);
        const auto size = WorldGeometry::ObjectFootprint(object).size;
        const sf::Vector2f tangent{-normal.y, normal.x};
        const float lane = (static_cast<float>(guard.id % 5u) - 2.f) * GameplayConfig::AgentRadius * 2.5f;
        const auto approach = object.position + normal * ((std::abs(normal.x) * size.x + std::abs(normal.y) * size.y) * 0.5f + GameplayConfig::AgentRadius + 8.f) + tangent * lane;
        if (accessible(approach)) return approach;
      }
    }
    constexpr std::array<sf::Vector2f, 8> directions{{{1.f, 0.f}, {0.70710678f, 0.70710678f}, {0.f, 1.f}, {-0.70710678f, 0.70710678f}, {-1.f, 0.f}, {-0.70710678f, -0.70710678f}, {0.f, -1.f}, {0.70710678f, -0.70710678f}}};
    for (std::size_t attempt = 0; attempt < directions.size(); ++attempt) {
      const auto sector = (guard.id + phase * 3u + attempt) % directions.size();
      const float radius = SecurityConfig::SearchProbeDistance + static_cast<float>(guard.id / directions.size()) * SecurityConfig::SearchProbeRingSpacing;
      const auto candidate = guard.lastKnown + directions[sector] * radius;
      if (accessible(candidate)) return candidate;
    }
    return guard.lastKnown;
  }

  bool Simulation::SecurityEntryValid(const SecurityEntry &entry) const {
    const auto support = WorldGeometry::FindSupport(world_, entry.position, 0.f, 0, GameplayConfig::AgentRadius);
    if (!support) return false;
    const auto rally = WorldGeometry::FindSupport(world_, entry.rally, 0.f, support->id, GameplayConfig::AgentRadius);
    if (!rally || WorldGeometry::PositionBlocked(world_, entry.position, GameplayConfig::AgentRadius, support->elevation, CombatConfig::StandingHeight, support->id) ||
        WorldGeometry::PositionBlocked(world_, entry.rally, GameplayConfig::AgentRadius, rally->elevation, CombatConfig::StandingHeight, rally->id) ||
        !WorldGeometry::ClearBodySegment(world_, {entry.position.x, entry.position.y, 0.f}, {entry.rally.x, entry.rally.y, 0.f}, GameplayConfig::AgentRadius, CombatConfig::StandingHeight, support->id)) return false;
    for (const auto &agent : world_.agents) if (agent.connected && agent.alive && !agent.extracted) {
      if (WorldGeometry::SpatialDistance(WorldGeometry::Feet(agent), {entry.position.x, entry.position.y, 0.f}) < SecurityConfig::MinimumSpawnAgentDistance || Visibility::IsGeometricallyVisible(world_, agent, entry.position, Stance::Standing, 0.f)) return false;
    }
    return std::ranges::none_of(world_.guards, [&entry](const Guard &guard) { return WorldGeometry::SpatialDistance(WorldGeometry::Feet(guard), {entry.position.x, entry.position.y, 0.f}) < GameplayConfig::AgentRadius * 2.5f; });
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
      const auto support = WorldGeometry::FindSupport(world_, entry.position, 0.f, 0, GameplayConfig::AgentRadius);
      const auto rally = WorldGeometry::FindSupport(world_, entry.rally, 0.f, support ? support->id : 0, GameplayConfig::AgentRadius);
      if (!support || !rally) continue;
      guard.elevation = support->elevation; guard.supportId = support->id;
      const auto facing = entry.rally - entry.position;
      if (facing.lengthSquared() > 0.0001f) guard.facing = facing / facing.length();
      guard.state = GuardState::Search;
      guard.lastKnown = securityLastKnown_;
      guard.lastKnownElevation = securityLastKnownElevation_;
      guard.reinforcement = true;
      guard.reinforcementExit = entry.position;
      guard.patrol = {entry.rally, entry.position};
      guard.patrolElevations = {rally->elevation, support->elevation};
      world_.guards.push_back(std::move(guard));
      EmitSound(SoundKind::Reinforcement, entry.position, DevicesConfig::ExplosionNoiseRadius, 0, 0.f);
      ++spawned;
    }
    if (spawned == 0) return false;
    ++world_.reinforcementWaves;
    world_.message = "La central de seguridad envía refuerzos por los accesos de servicio.";
    return true;
  }

  void Simulation::UpdateSecurity(float dt) {
    NO_TRACE_PROFILE_SCOPE(Security);
    using namespace SecurityConfig;
    if (!securityHasClue_) return;
    securityContactAge_ += dt;
    if (securityContactTick_ != world_.tick) {
      if (world_.securityAlert == SecurityAlert::Red) {
        SetSecurityAlert(SecurityAlert::Orange);
        for (auto &guard : world_.guards) if (guard.state == GuardState::Patrol || guard.state == GuardState::Retiring || guard.state == GuardState::Vigilance) {
          guard.state = GuardState::Search; guard.stateTime = 0.f; guard.lastKnown = securityLastKnown_; guard.lastKnownElevation = securityLastKnownElevation_; guard.navigation.clear();
        }
        world_.message = "Contacto perdido: alerta naranja, treinta segundos de búsqueda.";
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
        const auto support = WorldGeometry::FindSupport(world_, exit, 0.f, 0, GameplayConfig::AgentRadius);
        return support && !WorldGeometry::PositionBlocked(world_, exit, GameplayConfig::AgentRadius, support->elevation, CombatConfig::StandingHeight, support->id) &&
            std::ranges::any_of(world_.securityEntries, [exit](const SecurityEntry &entry) { return entry.position == exit; });
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
      if (!guard.reinforcement || guard.state != GuardState::Retiring || std::abs(guard.elevation) > LevelConfig::SupportTolerance ||
          WorldGeometry::SpatialDistance(WorldGeometry::Feet(guard), {guard.reinforcementExit.x, guard.reinforcementExit.y, 0.f}) > ExitArrivalDistance ||
          WorldGeometry::PositionBlocked(world_, guard.reinforcementExit, GameplayConfig::AgentRadius, 0.f, CombatConfig::StandingHeight, guard.supportId)) return false;
      if (std::ranges::none_of(world_.securityEntries, [&guard](const SecurityEntry &entry) { return entry.position == guard.reinforcementExit; })) return false;
      for (const auto &agent : world_.agents) if (agent.connected && agent.alive && !agent.extracted && Visibility::GuardVisible(world_, agent, guard)) return false;
      EmitSound(SoundKind::ReinforcementExit, guard.reinforcementExit, GameplayConfig::WalkingNoiseRadius, 0, 0.f);
      return true;
    });
  }
}
