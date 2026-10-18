#pragma once

#include "NoTraceMission.h"
#include <algorithm>
#include <random>
#include <span>
#include <string_view>

namespace NoTrace::Outcomes {
  enum class Decision { Continue, CooperativeSuccess, TraitorSuccess, Failure };
  struct Resolution { Decision decision = Decision::Continue; std::string_view reason; };

  inline void AssignPrivateOrders(std::span<Agent> agents, bool traitorEnabled) {
    std::random_device entropy;
    std::uniform_int_distribution<int> normalOrder(0, 1);
    std::vector<Agent *> participants;
    for (auto &agent : agents) {
      agent.privateComplete = false;
      agent.privateOrder = PrivateOrder::None;
      if (!agent.connected) continue;
      agent.privateOrder = normalOrder(entropy) == 0 ? PrivateOrder::CopyArchive : PrivateOrder::RecoverDevice;
      participants.push_back(&agent);
    }
    if (!traitorEnabled || participants.size() < 2) return;
    // La elección privada no comparte generador ni semilla con el mapa público.
    std::uniform_int_distribution<std::size_t> select(0, participants.size() - 1);
    participants[select(entropy)]->privateOrder = PrivateOrder::Traitor;
  }

  inline Resolution Resolve(const WorldState &world) {
    if (world.phase != GamePhase::Mission) return {};
    const auto &contract = GetContract(world.contract);
    if (world.events.size() >= GameplayConfig::MaximumEvents) return {Decision::Failure, "Registro operacional lleno; operación cancelada."};
    if (world.elapsed >= contract.timeLimit) return {Decision::Failure, "Se agotó la ventana de extracción."};
    if ((contract.noAlarm && world.alarmCount > 0) || world.alarmCount >= contract.maxAlarms) return {Decision::Failure, "Contrato cancelado por alarmas confirmadas."};
    const auto operational = [](const Agent &agent) { return agent.connected && agent.alive && !agent.incapacitated && !agent.extracted; };
    const auto traitor = std::ranges::find(world.agents, PrivateOrder::Traitor, &Agent::privateOrder);
    if (traitor != world.agents.end() && operational(*traitor) && world.alarmCount == 0) {
      const bool hasVictims = std::ranges::any_of(world.agents, [&](const Agent &agent) { return agent.id != traitor->id; });
      const bool allDead = std::ranges::all_of(world.agents, [&](const Agent &agent) { return agent.id == traitor->id || agent.lifeState == LifeState::Dead; });
      if (hasVictims && allDead) return {Decision::TraitorSuccess, "El traidor eliminó a los demás agentes sin activar alarmas."};
    }
    const auto survivingTeam = [](const Agent &agent) { return agent.connected && agent.alive && agent.privateOrder != PrivateOrder::Traitor; };
    const bool teamSurvives = std::ranges::any_of(world.agents, survivingTeam);
    const bool allSurvivorsExtracted = std::ranges::all_of(world.agents, [&](const Agent &agent) { return !survivingTeam(agent) || agent.extracted; });
    if (teamSurvives && world.objectives >= contract.requiredObjectives && allSurvivorsExtracted) return {Decision::CooperativeSuccess, "Contrato cumplido y supervivientes extraídos."};
    if (std::ranges::none_of(world.agents, operational)) return {Decision::Failure, "No quedan participantes operativos capaces de continuar."};
    if (!teamSurvives) return {Decision::Failure, "No quedan participantes con un objetivo alcanzable."};
    return {};
  }

  inline const Agent *SpectatorTarget(const WorldState &world, PlayerId recipient, PlayerId previous, int direction = 0) {
    const auto owner = std::ranges::find(world.agents, recipient, &Agent::id);
    if (owner == world.agents.end() || !owner->connected || owner->lifeState != LifeState::Dead) return nullptr;
    std::vector<const Agent *> survivors;
    for (const auto &agent : world.agents) if (agent.id != recipient && agent.connected && agent.alive && !agent.extracted) survivors.push_back(&agent);
    if (survivors.empty()) return nullptr;
    const auto selected = std::ranges::find_if(survivors, [&](const Agent *agent) { return agent->id == previous; });
    if (selected == survivors.end()) return survivors.front();
    const auto index = static_cast<int>(selected - survivors.begin());
    const auto count = static_cast<int>(survivors.size());
    return survivors[static_cast<std::size_t>((index + (direction > 0 ? 1 : direction < 0 ? -1 : 0) + count) % count)];
  }
}
