#pragma once

#include "Config/NoTraceGameplayConfig.h"
#include "Config/NoTraceCombatConfig.h"
#include "Config/NoTraceDistractionConfig.h"
#include "Config/NoTraceSpatialConfig.h"
#include "Types/NoTraceDeviceTypes.h"
#include "Types/NoTraceManeuverTypes.h"
#include <SFML/System/Vector2.hpp>
#include <SFML/Graphics/Rect.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace NoTrace {
  using PlayerId = std::uint32_t;
  enum class GamePhase { Base, Mission, Results };
  enum class SimulationMode { Operation, Training };
  enum class WeaponKind { Pistol, NonLethal };
  enum class ContractId { GhostArchive, SilentRelay, DoubleCross };
  enum class Stance { Standing, Crouched, Prone };
  enum class LifeState { Alive, Incapacitated, Dead };
  enum class HitZone { None, Head, Torso, Limbs };
  enum class ShotTarget { None, Agent, Guard };
  enum class ActionPose { None, ThrowDecoy, KnockWall, Push, Subdue, Punch, Kick, Recover, Grab, Choke, Aim, Shoot, Reload, TakeDamage, Fall, Interact, HideEnter, HideExit, CoverEnter };
  enum class SoundKind { Pistol, Rifle, Suppressed, Reload, Impact, Punch, Kick, Grab, Choke, Fall, StepHard, StepSoft, Knock, TrapTick, Explosion, Lock, PuzzleStep, PuzzleFailed, PuzzleSolved, Camera, Alarm, Reinforcement, Cabinet, Hideout, Drag, Decoy, SuppressorAttach, SuppressorDetach, StepRunHard, StepRunSoft, StepCrouchHard, StepCrouchSoft };
  enum class GuardState { Patrol, Suspicion, Investigate, Alert, Search, Incapacitated, Dead, Retiring };
  enum class CoverKind { None, Low, High, Hide };
  enum class SpatialOverride { Default, Enabled, Disabled };
  enum class PeekSide { None, Left, Right };
  enum class HideoutKind { None, TrashBin, Closet, UnderTable };
  enum class CqcState { None, Grabbed, KnockedDown, Restrained };
  enum class ObjectKind { Operations, Armory, Intelligence, Deployment, Door, Terminal, Secondary, Camera, Hideout, CoopSwitch, Extraction, AlarmPanel, Explosive, Computer, CameraPanel, TrainingReset, PuzzleNode };
  enum class PrivateOrder { None, CopyArchive, RecoverDevice, Traitor };
  enum class EventKind { Objective, Secondary, Extracted, ConfirmedDetection, AgentAlarm, Noise, WeaponLethal, WeaponNonLethal, Cqc, GuardKilled, GuardIncapacitated, BodyFound, CameraDisabled, Sabotage, PrivateObjective, AgentKilled, Disconnected, GuardShot, GuardMelee, AgentDamaged, AgentIncapacitated, AgentRevived, TrapTick, Explosion, PuzzleStarted, PuzzleStep, PuzzleFailed, PuzzleSolved, CameraSweep };
  enum class Rank { D, C, B, A, S, SPlus, Maestro };

  struct PlayerProfile { PlayerId id = 1; std::string name = "Agente"; };
  struct Loadout { int lethalAmmo = GameplayConfig::StartingPistolAmmo; int nonLethalAmmo = GameplayConfig::StartingNonLethalAmmo; int decoys = 3; int tools = 2; };
  struct ActionCommand {
    float moveX = 0.f;
    float moveY = 0.f;
    float aimX = 0.f;
    float aimY = 0.f;
    float aimSlope = 0.f;
    float decoyDistance = DistractionConfig::MaximumDecoyDistance;
    bool run = false;
    bool crouch = false;
    bool prone = false;
    bool crawl = false;
    bool interact = false;
    bool cqc = false;
    bool cqcHeld = false;
    bool push = false;
    bool fireLethal = false;
    bool fireNonLethal = false;
    bool throwDecoy = false;
    bool prepareDecoy = false;
    bool knock = false;
    bool dragBody = false;
    bool tool = false;
    bool coverToggle = false;
    bool peekLeft = false;
    bool peekRight = false;
    int puzzleInput = -1;
    std::uint32_t puzzleTarget = 0;
    bool reloadWeapon = false;
    bool fireSelected = false;
    int weaponChoice = -1;
    bool aiming = false;
    bool dive = false;
    bool roll = false;
    bool toggleSuppressor = false;
    bool puzzleHeld = false;
  };
  struct AgentCover {
    bool active = false;
    sf::Vector2f anchor;
    sf::Vector2f normal;
    sf::FloatRect surface;
    int obstacle = -1;
    std::uint32_t object = 0;
    PeekSide peekSide = PeekSide::None;
    bool canPeekLeft = false;
    bool canPeekRight = false;
  };
  struct DecoyGuide {
    bool active = false;
    sf::Vector2f direction;
    float distance = 0.f;
    sf::Vector2f destination;
    sf::Vector2f maximum;
    float travelled = 0.f;
    float maximumTravelled = 0.f;
    bool valid = false;
    bool blocked = false;
    bool maximumBlocked = false;
  };
  struct Agent {
    PlayerId id = 1;
    std::string name;
    sf::Vector2f position;
    sf::Vector2f facing{1.f, 0.f};
    Stance stance = Stance::Standing;
    sf::Vector2f weaponAim;
    float aimSlope = 0.f;
    Loadout loadout;
    Loadout operationLoadout;
    WeaponKind selectedWeapon = WeaponKind::Pistol;
    WeaponKind reloadingWeapon = WeaponKind::Pistol;
    int lethalMagazine = 4;
    int nonLethalMagazine = 4;
    float reloadRemaining = 0.f;
    float pistolSuppressor = 100.f;
    float nonLethalSuppressor = 100.f;
    bool pistolSuppressorEquipped = true;
    bool nonLethalSuppressorEquipped = true;
    bool aiming = false;
    ManeuverState maneuver;
    Stance transitionFrom = Stance::Standing;
    float transitionTime = 0.f;
    LifeState lifeState = LifeState::Alive;
    bool rescueExpired = false;
    bool alive = true;
    bool incapacitated = false;
    bool bodyReported = false;
    bool connected = true;
    float detectionProgress = 0.f;
    bool detectionConfirmed = false;
    float detectionIndicatorRemaining = 0.f;
    float confirmedIndicatorRemaining = 0.f;
    bool hidden = false;
    std::uint32_t hideoutId = 0;
    sf::Vector2f hideoutExit;
    bool moving = false;
    bool running = false;
    bool extracted = false;
    float health = GameplayConfig::MaxHealth;
    float stamina = GameplayConfig::MaxStamina;
    float incapacityRemaining = 0.f;
    float animationTime = 0.f;
    float actionCooldown = 0.f;
    std::uint32_t cqcHoldingGuard = 0;
    PlayerId cqcHoldingAgent = 0;
    PlayerId cqcHeldBy = 0;
    std::uint32_t cqcApproachGuard = 0;
    PlayerId cqcApproachAgent = 0;
    float cqcApproachTime = 0.f;
    int cqcApproachSide = 0;
    bool friendlyIncapacitated = false;
    bool cqcPressPending = false;
    bool cqcInputDown = false;
    bool cqcNextKick = false;
    bool cqcStrikePending = false;
    float cqcAttackTime = 0.f;
    PuzzleSession puzzle;
    float cqcHeldPressTime = 0.f;
    bool cqcLongPressArmed = false;
    float cqcHoldTime = 0.f;
    float cqcReleaseTime = 0.f;
    float cqcPulseTime = 0.f;
    bool cqcPulseReady = false;
    unsigned int cqcProgress = 0;
    float staggerRemaining = 0.f;
    float footstepTime = 0.f;
    ActionPose actionPose = ActionPose::None;
    float actionPoseTime = 0.f;
    int draggingGuard = -1;
    PrivateOrder privateOrder = PrivateOrder::None;
    bool privateComplete = false;
    AgentCover cover;
    DecoyGuide decoyGuide;
  };
  struct Guard {
    std::uint32_t id = 0;
    bool trainingDummy = false;
    unsigned int trainingHits = 0;
    float trainingFeedback = 0.f;
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
    float attackTime = 0.f;
    bool meleeAttack = false;
    int ammo = GameplayConfig::GuardAmmo;
    float health = CombatConfig::MaxGuardHealth;
    float stun = 0.f;
    float detectedTime = 0.f;
    float suspicionRemaining = 0.f;
    float indicatorProgress = 0.f;
    float incapacityRemaining = 0.f;
    bool bodyHidden = false;
    std::uint32_t storedIn = 0;
    bool bodyReported = false;
    PlayerId heldBy = 0;
    float staggerRemaining = 0.f;
    std::vector<sf::Vector2f> navigation;
    sf::Vector2f navigationTarget;
    float navigationTimer = 0.f;
    float navigationStuckTime = 0.f;
    unsigned int navigationFailures = 0;
    float navigationRejectTime = 0.f;
    sf::Vector2f navigationRejectedTarget;
    sf::Vector2f navigationRecoveryTarget;
    float patrolPauseRemaining = 0.f;
    float perceptionLostTime = 0.f;
    float stepTime = 0.f;
    std::uint32_t knownHideout = 0;
    float hideoutInspectionTime = 0.f;
    PlayerId observedAgent = 0;
    bool reinforcement = false;
    sf::Vector2f reinforcementExit;
  };
  struct Obstacle {
    sf::FloatRect bounds;
    CoverKind cover = CoverKind::High;
    bool solid = true;
    std::string name;
    bool blocksVision = true;
    bool wallCover = true;
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
    PlayerId observedAgent = 0;
    sf::Vector2f facing{-1.f, 0.f};
    sf::Vector2f footprint;
    SpatialOverride solid = SpatialOverride::Default;
    SpatialOverride blocksVision = SpatialOverride::Default;
    SpatialOverride wallCover = SpatialOverride::Default;
    bool interactive = true;
    bool decorative = false;
    HideoutKind hideoutKind = HideoutKind::None;
    PlayerId occupantAgent = 0;
    std::uint32_t occupantGuard = 0;
    bool occupied = false;
    float hideoutAperture = SpatialConfig::ClosetDefaultAperture;
    bool lifted = false;
    float inspectionProgress = 0.f;
    bool locked = false;
    CameraDevice camera;
    TrapDevice trap;
    PuzzleDevice puzzle;
  };
  struct NoiseEvent { sf::Vector2f position; float radius = 0.f; float remaining = 0.f; PlayerId actor = 0; std::uint32_t guardId = 0; SoundKind kind = SoundKind::Impact; float duration = GameplayConfig::NoiseDuration; };
  struct DecoyProjectile { sf::Vector2f from; sf::Vector2f to; float elapsed = 0.f; float duration = 0.f; float height = 0.f; PlayerId actor = 0; bool blocked = false; };
  struct SoundEvent { std::uint64_t sequence = 0; SoundKind kind = SoundKind::Impact; sf::Vector2f position; float radius = 0.f; float remaining = 0.f; PlayerId actor = 0; };
  struct FootstepCue {
    sf::Vector2f position;
    std::uint8_t direction = 0;
    float remaining = 0.f;
    std::uint32_t sourceGuard = 0;
  };
  struct ShotTrace {
    sf::Vector2f from;
    sf::Vector2f to;
    float remaining = 0.f;
    bool lethal = false;
    PlayerId actor = 0;
    std::uint32_t guardId = 0;
    bool hit = false;
    bool blocked = false;
    HitZone zone = HitZone::None;
    float fromHeight = CombatConfig::StandingWeaponHeight;
    float toHeight = CombatConfig::StandingWeaponHeight;
    std::uint32_t targetId = 0;
    ShotTarget targetKind = ShotTarget::None;
  };
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
    std::vector<std::uint32_t> secondaryTargets{12};
  };
  struct MissionResult {
    bool success = false;
    bool traitorVictory = false;
    bool maestroEligible = false;
    std::string maestroDeniedReason;
    int score = 0;
    Rank rank = Rank::D;
    std::string reason;
  };
  struct VisibilityGrid {
    sf::Vector2f visualOrigin;
    std::vector<float> visualRadii;
    bool authoritative = false;
    PlayerId observer = 0;
    std::uint64_t tick = 0;
    float cellSize = 0.f;
    unsigned int columns = 0;
    unsigned int rows = 0;
    std::vector<std::uint8_t> cells;
  };
  struct SecurityEntry {
    sf::Vector2f position;
    sf::Vector2f rally;
  };
  struct RescueSignal { PlayerId agent = 0; sf::Vector2f direction; float remaining = 0.f; };
  struct WorldState {
    GamePhase phase = GamePhase::Base;
    SimulationMode mode = SimulationMode::Operation;
    ContractId contract = ContractId::GhostArchive;
    std::uint32_t seed = 0;
    std::uint64_t tick = 0;
    sf::Vector2f size{1000.f, 760.f};
    float elapsed = 0.f;
    bool alarm = false;
    int alarmCount = 0;
    bool securityActive = false;
    unsigned int reinforcementWaves = 0;
    std::vector<SecurityEntry> securityEntries;
    int objectives = 0;
    int secondary = 0;
    std::vector<Agent> agents;
    std::vector<Guard> guards;
    std::vector<Obstacle> obstacles;
    std::vector<Interactable> objects;
    std::vector<NoiseEvent> noises;
    std::vector<DecoyProjectile> decoys;
    std::vector<SoundEvent> sounds;
    std::vector<ShotTrace> shots;
    std::vector<FootstepCue> footsteps;
    std::vector<MissionEvent> events;
    std::vector<RescueSignal> rescueSignals;
    std::unordered_map<PlayerId, MissionResult> results;
    VisibilityGrid visibility;
    std::string message;
  };
  inline int ReserveAmmo(const Agent &agent, WeaponKind weapon) {
    const int total = weapon == WeaponKind::Pistol ? agent.loadout.lethalAmmo : agent.loadout.nonLethalAmmo;
    const int magazine = weapon == WeaponKind::Pistol ? agent.lethalMagazine : agent.nonLethalMagazine;
    return std::max(0, total - magazine);
  }
  inline sf::Vector2f WeaponFacing(const Agent &agent) {
    return std::isfinite(agent.weaponAim.x) && std::isfinite(agent.weaponAim.y) && agent.weaponAim.lengthSquared() > 0.01f ? agent.weaponAim.normalized() : agent.facing;
  }
  inline bool IsTraining(const WorldState &world) { return world.mode == SimulationMode::Training && world.phase == GamePhase::Base; }
  inline bool IsTactical(const WorldState &world) { return world.phase == GamePhase::Mission || IsTraining(world); }
}
