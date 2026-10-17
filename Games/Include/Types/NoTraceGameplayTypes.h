#pragma once

#include <SFML/System/Vector2.hpp>
#include <SFML/Graphics/Rect.hpp>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace NoTrace {
  using PlayerId = std::uint32_t;
  enum class GamePhase { Base, Mission, Results };
  enum class ContractId { GhostArchive, SilentRelay, DoubleCross };
  enum class Stance { Standing, Crouched, Prone };
  enum class GuardState { Patrol, Suspicion, Investigate, Alert, Search, Incapacitated, Dead };
  enum class CoverKind { None, Low, High, Hide };
  enum class CqcState { None, Grabbed, KnockedDown, Restrained };
  enum class ObjectKind { Operations, Armory, Intelligence, Deployment, Door, Terminal, Secondary, Camera, Hideout, CoopSwitch, Extraction, AlarmPanel };
  enum class PrivateOrder { None, CopyArchive, RecoverDevice, Traitor };
  enum class EventKind { Objective, Secondary, Extracted, ConfirmedDetection, AgentAlarm, Noise, WeaponLethal, WeaponNonLethal, Cqc, GuardKilled, GuardIncapacitated, BodyFound, CameraDisabled, Sabotage, PrivateObjective, AgentKilled, Disconnected };
  enum class Rank { D, C, B, A, S, SPlus, Maestro };

  struct PlayerProfile { PlayerId id = 1; std::string name = "Agente"; };
  struct Loadout { int lethalAmmo = 4; int nonLethalAmmo = 4; int decoys = 3; int tools = 2; };
  struct ActionCommand {
    float moveX = 0.f;
    float moveY = 0.f;
    float aimX = 0.f;
    float aimY = 0.f;
    bool run = false;
    bool crouch = false;
    bool prone = false;
    bool crawl = false;
    bool interact = false;
    bool cqc = false;
    bool fireLethal = false;
    bool fireNonLethal = false;
    bool throwDecoy = false;
    bool knock = false;
    bool dragBody = false;
    bool tool = false;
  };
  struct Agent {
    PlayerId id = 1;
    std::string name;
    sf::Vector2f position;
    sf::Vector2f facing{1.f, 0.f};
    Stance stance = Stance::Standing;
    Loadout loadout;
    bool alive = true;
    bool incapacitated = false;
    bool bodyReported = false;
    bool connected = true;
    bool hidden = false;
    bool moving = false;
    bool running = false;
    bool extracted = false;
    float animationTime = 0.f;
    float actionCooldown = 0.f;
    float footstepTime = 0.f;
    int draggingGuard = -1;
    PrivateOrder privateOrder = PrivateOrder::None;
    bool privateComplete = false;
  };
  struct Guard {
    std::uint32_t id = 0;
    sf::Vector2f position;
    sf::Vector2f facing{1.f, 0.f};
    GuardState state = GuardState::Patrol;
    CqcState cqcState = CqcState::None;
    std::vector<sf::Vector2f> patrol;
    std::size_t waypoint = 0;
    sf::Vector2f lastKnown;
    float suspicion = 0.f;
    float stateTime = 0.f;
    float attackCooldown = 0.f;
    bool bodyHidden = false;
    bool bodyReported = false;
    PlayerId heldBy = 0;
    std::vector<sf::Vector2f> navigation;
    sf::Vector2f navigationTarget;
    float navigationTimer = 0.f;
  };
  struct Obstacle {
    sf::FloatRect bounds;
    CoverKind cover = CoverKind::High;
    bool solid = true;
    std::string name;
  };
  struct Interactable {
    std::uint32_t id = 0;
    ObjectKind kind = ObjectKind::Door;
    sf::Vector2f position;
    std::string name;
    bool active = true;
    bool completed = false;
    bool open = false;
    float progress = 0.f;
    PlayerId user = 0;
    sf::Vector2f facing{-1.f, 0.f};
  };
  struct NoiseEvent { sf::Vector2f position; float radius = 0.f; float remaining = 0.f; PlayerId actor = 0; };
  struct MissionEvent { std::uint64_t sequence = 0; std::uint64_t tick = 0; PlayerId actor = 0; EventKind kind = EventKind::Noise; std::uint32_t target = 0; };
  struct Contract {
    ContractId id = ContractId::GhostArchive;
    std::string name;
    std::string briefing;
    int requiredObjectives = 1;
    int secondaryObjectives = 1;
    bool lethalAllowed = false;
    bool noAlarm = false;
    bool traitor = false;
    float timeLimit = 600.f;
    int maxAlarms = 4;
  };
  struct MissionResult {
    bool success = false;
    bool traitorVictory = false;
    bool maestroEligible = false;
    int score = 0;
    Rank rank = Rank::D;
    std::string reason;
  };
  struct WorldState {
    GamePhase phase = GamePhase::Base;
    ContractId contract = ContractId::GhostArchive;
    std::uint32_t seed = 0;
    std::uint64_t tick = 0;
    sf::Vector2f size{1000.f, 760.f};
    float elapsed = 0.f;
    bool alarm = false;
    int alarmCount = 0;
    int objectives = 0;
    int secondary = 0;
    std::vector<Agent> agents;
    std::vector<Guard> guards;
    std::vector<Obstacle> obstacles;
    std::vector<Interactable> objects;
    std::vector<NoiseEvent> noises;
    std::vector<MissionEvent> events;
    std::unordered_map<PlayerId, MissionResult> results;
    std::string message;
  };
}
