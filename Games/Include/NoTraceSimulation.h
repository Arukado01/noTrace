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
    nlohmann::json SnapshotFor(PlayerId player, bool visibilityFiltered = false) const;
    bool LoadSnapshot(const nlohmann::json &snapshot, std::string &error);
    bool CanSee(sf::Vector2f from, sf::Vector2f facing, const Agent &agent, float range = 260.f) const;

  private:
    WorldState world_;
    float alarmRemaining_ = 0.f;
    std::uint64_t nextEvent_ = 1;
    float securityContactAge_ = 0.f;
    float reinforcementTimer_ = 0.f;
    sf::Vector2f securityLastKnown_;
    bool securityHasClue_ = false;
    std::uint32_t nextGuardId_ = 1;
    void ResetSecurity();
    void RegisterSecurityContact(sf::Vector2f position);
    void UpdateSecurity(float dt);
    bool SpawnReinforcementWave();
    bool SecurityEntryValid(const SecurityEntry &entry) const;
    void BuildBase();
    void BuildMission(std::uint32_t seed);
    void UpdateAgent(Agent &agent, const ActionCommand &command, float dt);
    void UpdateGuards(float dt);
    void UpdateObjects(float dt);
    void CheckResults();
    void Finish(bool success, const std::string &reason, bool traitorVictory = false);
    void Interact(Agent &agent, bool tool);
    void Cqc(Agent &agent);
    bool UpdateCqc(Agent &agent, const ActionCommand &command, float dt);
    void ReleaseCqc(Agent &agent);
    void Push(Agent &agent);
    bool Fire(Agent &agent, bool lethal);
    sf::Vector2f TraceWeapon(sf::Vector2f from, sf::Vector2f direction, float range, bool &blocked, float fromHeight = CombatConfig::StandingWeaponHeight, float slope = 0.f) const;
    void DamageAgent(Agent &agent, float healthDamage, float staminaDamage, PlayerId actor, std::uint32_t source, bool enemyBullet = false);
    void HitGuard(Guard &guard, HitZone zone, bool lethal, PlayerId actor);
    void Incapacitate(Agent &agent, PlayerId actor, std::uint32_t source);
    void Revive(Agent &agent, PlayerId actor);
    void EmitNoise(sf::Vector2f position, float radius, PlayerId actor, std::uint32_t guardId = 0);
    void HandleDistractions(Agent &agent, const ActionCommand &command);
    void Record(EventKind kind, PlayerId actor, std::uint32_t target = 0);
    void Detect(Agent &agent, std::uint32_t observer);
    void Move(sf::Vector2f &position, sf::Vector2f displacement, float radius) const;
    bool Blocked(sf::Vector2f position, float radius) const;
    bool Occluded(sf::Vector2f from, sf::Vector2f to, Stance targetStance) const;
    void Navigate(Guard &guard, sf::Vector2f target, float speed, float dt);
    void ResolvePenetration(sf::Vector2f &position, float radius) const;
    void ToggleCover(Agent &agent);
    bool UpdateCover(Agent &agent, const ActionCommand &command, sf::Vector2f direction, float dt);
    bool CoverSurfaceValid(const AgentCover &cover) const;
    bool CoverPeekTarget(const AgentCover &cover, PeekSide side, sf::Vector2f &target) const;
    void ReleaseHideout(Agent &agent);
    bool HideoutPathClear(const Interactable &object, sf::Vector2f from, sf::Vector2f to) const;
    bool HideoutExit(const Agent &agent, const Interactable &object, sf::Vector2f &position) const;
    bool UseHideout(Agent &agent, Interactable &object);
    void ToggleHideoutOpening(Agent &agent);
    void ObserveHideoutEntry(const Agent &agent, const Interactable &object);
    bool UpdateHideoutInspection(Guard &guard, float dt);
    Guard *NearbyBody(const Agent &agent);
    void UpdateBodyDrag(Agent &agent, bool held);
    void MoveDraggedBody(Agent &agent, sf::Vector2f previous, float dt);
  };
}
