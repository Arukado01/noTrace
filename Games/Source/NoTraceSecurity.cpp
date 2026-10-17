#include "NoTraceSimulation.h"
#include "Config/NoTraceSecurityConfig.h"
#include "NoTraceVisibility.h"
#include "NoTraceWorldGeometry.h"
#include <algorithm>
#include <cmath>

namespace NoTrace {
  void Simulation::ResetSecurity() {
    world_.sounds.clear();
    for (auto &agent : world_.agents) { CancelPuzzle(agent); agent.friendlyIncapacitated = false; }
    securityContactAge_ = 0.f;
    reinforcementTimer_ = 0.f;
    securityLastKnown_ = {};
    securityHasClue_ = false;
    nextGuardId_ = 1;
    world_.securityActive = false;
    world_.reinforcementWaves = 0;
    world_.securityEntries.clear();
    for (auto &agent : world_.agents) { ReleaseCqc(agent); agent.staggerRemaining = 0.f; agent.detectionProgress = 0.f; agent.detectionConfirmed = false; }
  }

  void Simulation::RegisterSecurityContact(sf::Vector2f position) {
    if (!world_.securityActive) reinforcementTimer_ = SecurityConfig::InitialArrivalDelay;
    world_.securityActive = true;
    securityHasClue_ = true;
    securityContactAge_ = 0.f;
    securityLastKnown_ = position;
    alarmRemaining_ = GameplayConfig::AlarmDuration;
    for (auto &guard : world_.guards) if (guard.reinforcement && guard.state != GuardState::Dead && guard.state != GuardState::Incapacitated && guard.heldBy == 0) {
      guard.lastKnown = position;
      if (guard.state == GuardState::Patrol || guard.state == GuardState::Retiring) { guard.state = GuardState::Search; guard.stateTime = 0.f; guard.navigation.clear(); }
    }
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
    if (world_.securityActive && securityContactAge_ >= PatrolRecoveryDelay) {
      world_.securityActive = false;
      world_.alarm = false;
      alarmRemaining_ = 0.f;
      for (auto &guard : world_.guards) if (guard.state != GuardState::Dead && guard.state != GuardState::Incapacitated && guard.heldBy == 0) {
        for (auto &object : world_.objects) if (object.id == guard.knownHideout && !object.lifted) object.inspectionProgress = 0.f;
        guard.knownHideout = 0; guard.hideoutInspectionTime = 0.f;
        guard.state = GuardState::Patrol; guard.stateTime = 0.f; guard.suspicion = 0.f; guard.observedAgent = 0;
        guard.lastKnown = guard.position; guard.navigation.clear();
      }
      world_.message = "Contacto perdido: las patrullas recuperan sus zonas de vigilancia.";
    }
    if (world_.securityActive) {
      reinforcementTimer_ = std::max(0.f, reinforcementTimer_ - dt);
      if (reinforcementTimer_ <= 0.f && world_.reinforcementWaves < MaximumWaves) {
        reinforcementTimer_ = SpawnReinforcementWave() ? ReinforcementInterval : EntryRetryDelay;
      }
    } else if (securityContactAge_ >= PatrolRecoveryDelay + WithdrawalDelay) {
      for (auto &guard : world_.guards) if (guard.reinforcement && (guard.state == GuardState::Patrol || guard.state == GuardState::Search) && guard.heldBy == 0) {
        for (auto &object : world_.objects) if (object.id == guard.knownHideout && !object.lifted) object.inspectionProgress = 0.f;
        guard.knownHideout = 0; guard.hideoutInspectionTime = 0.f;
        guard.state = GuardState::Retiring; guard.stateTime = 0.f; guard.navigation.clear(); guard.suspicion = 0.f;
      }
    }
    std::erase_if(world_.guards, [this](const Guard &guard) {
      if (!guard.reinforcement || guard.state != GuardState::Retiring || (guard.position - guard.reinforcementExit).length() > ExitArrivalDistance) return false;
      for (const auto &agent : world_.agents) if (agent.connected && agent.alive && !agent.extracted && Visibility::GuardVisible(world_, agent, guard)) return false;
      return true;
    });
  }
}
