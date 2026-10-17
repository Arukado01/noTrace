#pragma once

#include "NoTraceMission.h"
#include <nlohmann/json.hpp>
#include <unordered_map>

namespace NoTrace {
  class Simulation {
  public:
    void ResetBase(const std::vector<PlayerProfile> &players = {{1, "Agente"}});
    bool AddPlayer(PlayerId id, const std::string &name);
    void DisconnectPlayer(PlayerId id);
    void SetContract(ContractId id);
    void SetLoadout(PlayerId id, Loadout loadout);
    void Deploy(std::uint32_t seed);
    void Step(float deltaTime, const std::unordered_map<PlayerId, ActionCommand> &commands);
    void ReturnToBase();
    const WorldState &GetState() const { return world_; }
    nlohmann::json SnapshotFor(PlayerId player) const;
    bool LoadSnapshot(const nlohmann::json &snapshot, std::string &error);
    bool CanSee(sf::Vector2f from, sf::Vector2f facing, const Agent &agent, float range = 260.f) const;

  private:
    WorldState world_;
    float alarmRemaining_ = 0.f;
    std::uint64_t nextEvent_ = 1;
    void BuildBase();
    void BuildMission(std::uint32_t seed);
    void UpdateAgent(Agent &agent, const ActionCommand &command, float dt);
    void UpdateGuards(float dt);
    void UpdateObjects(float dt);
    void CheckResults();
    void Finish(bool success, const std::string &reason, bool traitorVictory = false);
    void Interact(Agent &agent, bool tool);
    void Cqc(Agent &agent);
    void Fire(Agent &agent, bool lethal);
    void EmitNoise(sf::Vector2f position, float radius, PlayerId actor);
    void Record(EventKind kind, PlayerId actor, std::uint32_t target = 0);
    void Detect(Agent &agent, std::uint32_t observer);
    void Move(sf::Vector2f &position, sf::Vector2f displacement, float radius) const;
    bool Blocked(sf::Vector2f position, float radius) const;
    bool Occluded(sf::Vector2f from, sf::Vector2f to, Stance targetStance) const;
    void Navigate(Guard &guard, sf::Vector2f target, float speed, float dt);
  };
}
