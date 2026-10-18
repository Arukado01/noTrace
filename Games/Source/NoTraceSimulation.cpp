#include "NoTraceSimulation.h"
#include "Config/NoTraceCombatConfig.h"
#include "Config/NoTraceAudioConfig.h"
#include "Config/NoTraceCqcConfig.h"
#include "Config/NoTraceDistractionConfig.h"
#include "Config/NoTraceGameplayConfig.h"
#include "Config/NoTraceNavigationConfig.h"
#include "Config/NoTraceSecurityConfig.h"
#include "Config/NoTraceVisibilityConfig.h"
#include "Config/NoTraceWeaponConfig.h"
#include "NoTraceCombatGeometry.h"
#include "NoTraceDistractionGeometry.h"
#include "NoTracePerception.h"
#include "NoTraceVisibility.h"
#include "NoTraceWorldGeometry.h"
#include "NoTraceStateExtensions.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <stdexcept>
#include <unordered_set>

namespace NoTrace {
  namespace {
    float Length(sf::Vector2f value) { return std::hypot(value.x, value.y); }
    sf::Vector2f Unit(sf::Vector2f value) { const float length = Length(value); return length > 0.0001f ? value / length : sf::Vector2f{}; }
    float Distance(sf::Vector2f left, sf::Vector2f right) { return Length(left - right); }
    bool IsBody(const Guard &guard) { return guard.state == GuardState::Dead || guard.state == GuardState::Incapacitated; }
    nlohmann::json VectorJson(sf::Vector2f value) { return {value.x, value.y}; }
    sf::Vector2f ParseVector(const nlohmann::json &value) {
      if (!value.is_array() || value.size() != 2) throw std::runtime_error("Vector inválido");
      const sf::Vector2f vector{value.at(0).get<float>(), value.at(1).get<float>()};
      if (!std::isfinite(vector.x) || !std::isfinite(vector.y) || std::abs(vector.x) > 100000.f || std::abs(vector.y) > 100000.f) throw std::runtime_error("Coordenadas inválidas");
      return vector;
    }
    template <typename Enum> Enum ParseEnum(const nlohmann::json &value, int maximum) {
      const int number = value.get<int>();
      if (number < 0 || number > maximum) throw std::runtime_error("Enumeración inválida");
      return static_cast<Enum>(number);
    }
  }

  void Simulation::ResetBase(const std::vector<PlayerProfile> &players) {
    world_ = WorldState{};
    nextEvent_ = 1;
    for (const auto &player : players) AddPlayer(player.id, player.name);
    BuildBase();
  }

  bool Simulation::AddPlayer(PlayerId id, const std::string &name) {
    if (world_.phase != GamePhase::Base || id == 0) return false;
    const auto previous = std::ranges::find_if(world_.agents, [id](const Agent &agent) { return agent.id == id; });
    if (previous != world_.agents.end()) {
      if (previous->connected) return false;
      Agent replacement;
      replacement.id = id; replacement.name = name.substr(0, GameplayConfig::MaximumNameBytes); replacement.position = {160.f, 150.f};
      *previous = std::move(replacement);
      return true;
    }
    if (world_.agents.size() >= GameplayConfig::MaximumPlayers) return false;
    Agent agent;
    agent.id = id;
    agent.name = name.substr(0, GameplayConfig::MaximumNameBytes);
    agent.position = {130.f + static_cast<float>(world_.agents.size()) * 35.f, 150.f};
    world_.agents.push_back(agent);
    return true;
  }

  void Simulation::DisconnectPlayer(PlayerId id) {
    for (auto &agent : world_.agents) if (agent.id == id && agent.connected) {
      agent.connected = false;
      agent.moving = false; agent.running = false;
      CancelManeuver(agent); agent.aiming = false;
      CancelPuzzle(agent);
      ReleaseCqc(agent);
      ReleaseHideout(agent);
      agent.draggingGuard = -1;
      Record(EventKind::Disconnected, id);
    }
    for (auto &guard : world_.guards) if (guard.heldBy == id) {
      guard.heldBy = 0; guard.cqcState = CqcState::None;
    }
    if (world_.phase == GamePhase::Mission) CheckResults();
  }

  void Simulation::SetContract(ContractId id) {
    if (world_.phase == GamePhase::Base && static_cast<unsigned int>(id) < Contracts().size()) {
      world_.contract = id;
      world_.message = GetContract(id).briefing;
    }
  }

  void Simulation::SetLoadout(PlayerId id, Loadout loadout) {
    if (world_.phase != GamePhase::Base) return;
    loadout.lethalAmmo = std::clamp(loadout.lethalAmmo, 0, GameplayConfig::MaximumAmmo);
    loadout.nonLethalAmmo = std::clamp(loadout.nonLethalAmmo, 0, GameplayConfig::MaximumAmmo);
    loadout.decoys = std::clamp(loadout.decoys, 0, GameplayConfig::MaximumDecoys);
    loadout.tools = std::clamp(loadout.tools, 0, GameplayConfig::MaximumTools);
    for (auto &agent : world_.agents) if (agent.id == id) {
      agent.loadout = loadout;
      agent.operationLoadout = loadout;
      agent.lethalMagazine = std::min(WeaponConfig::PistolCapacity, loadout.lethalAmmo);
      agent.nonLethalMagazine = std::min(WeaponConfig::NonLethalCapacity, loadout.nonLethalAmmo);
      CancelReload(agent);
    }
  }

  void Simulation::BuildBase() {
    ResetSecurity();
    for (auto &agent : world_.agents) agent.healthRecoveryRemaining = GameplayConfig::PassiveHealthRecoveryDelay;
    for (auto &agent : world_.agents) { agent.cover = {}; agent.decoyGuide = {}; agent.hideoutId = 0; agent.hideoutExit = {}; agent.hidden = false; agent.draggingGuard = -1; agent.actionPose = ActionPose::None; agent.actionPoseTime = 0.f; }
    world_.phase = GamePhase::Base;
    world_.size = {1000.f, 760.f};
    world_.guards.clear(); world_.obstacles.clear(); world_.objects.clear(); world_.noises.clear(); world_.shots.clear(); world_.footsteps.clear(); world_.decoys.clear();
    world_.objects = {
      {1, ObjectKind::Operations, {210.f, 210.f}, "OPERACIONES"},
      {2, ObjectKind::Armory, {720.f, 210.f}, "ARMERÍA"},
      {3, ObjectKind::Intelligence, {210.f, 560.f}, "INTELIGENCIA"},
      {4, ObjectKind::Deployment, {720.f, 560.f}, "DESPLIEGUE"}
    };
    world_.obstacles = {
      {{{380.f, 90.f}, {190.f, 180.f}}, CoverKind::High, true, "Archivo CIO"},
      {{{380.f, 460.f}, {190.f, 180.f}}, CoverKind::High, true, "Comunicaciones"},
      {{{80.f, 350.f}, {180.f, 28.f}}, CoverKind::Low, true, "Banco"},
      {{{700.f, 350.f}, {180.f, 28.f}}, CoverKind::Low, true, "Banco"}
    };
    BuildTraining();
  }

  void Simulation::Deploy(std::uint32_t seed) {
    if (world_.phase != GamePhase::Base || std::ranges::none_of(world_.agents, [](const Agent &agent) { return agent.connected; })) return;
    world_.phase = GamePhase::Mission;
    world_.seed = seed;
    world_.tick = 0; world_.elapsed = 0.f; world_.alarm = false; world_.alarmCount = 0;
    world_.objectives = 0; world_.secondary = 0; world_.events.clear(); world_.results.clear(); world_.noises.clear(); world_.shots.clear(); world_.footsteps.clear(); world_.decoys.clear();
    nextEvent_ = 1; alarmRemaining_ = 0.f;
    std::size_t index = 0;
    const auto connected = std::ranges::count_if(world_.agents, [](const Agent &agent) { return agent.connected; });
    for (auto &agent : world_.agents) {
      agent.position = {GameplayConfig::MissionSpawnX, GameplayConfig::MissionSpawnY + static_cast<float>(index) * GameplayConfig::MissionSpawnSpacing};
      agent.facing = {1.f, 0.f}; agent.stance = Stance::Standing;
      agent.lifeState = LifeState::Alive; agent.rescueExpired = false; agent.alive = true; agent.incapacitated = false; agent.bodyReported = false; agent.hidden = false; agent.extracted = false; agent.moving = false;
      agent.health = GameplayConfig::MaxHealth; agent.stamina = GameplayConfig::MaxStamina; agent.incapacityRemaining = 0.f; agent.running = false;
      agent.healthRecoveryRemaining = GameplayConfig::PassiveHealthRecoveryDelay;
      agent.animationTime = 0.f; agent.actionCooldown = 0.f; agent.footstepTime = 0.f; agent.draggingGuard = -1;
      agent.loadout = agent.operationLoadout;
      agent.maneuver = {}; agent.aiming = false; agent.transitionTime = 0.f;
      agent.pistolSuppressor = agent.nonLethalSuppressor = 100.f; agent.pistolSuppressorEquipped = agent.nonLethalSuppressorEquipped = true;
      agent.lethalMagazine = std::min(WeaponConfig::PistolCapacity, agent.loadout.lethalAmmo);
      agent.nonLethalMagazine = std::min(WeaponConfig::NonLethalCapacity, agent.loadout.nonLethalAmmo);
      CancelReload(agent);
      agent.privateComplete = false;
      agent.privateOrder = index % 2 == 0 ? PrivateOrder::CopyArchive : PrivateOrder::RecoverDevice;
      if (GetContract(world_.contract).traitor && connected > 1 && agent.connected && index == seed % static_cast<std::size_t>(connected)) agent.privateOrder = PrivateOrder::Traitor;
      if (agent.connected) ++index;
    }
    BuildMission(seed);
    BuildPuzzleNodes();
    world_.message = GetContract(world_.contract).briefing;
  }

  void Simulation::BuildMission(std::uint32_t seed) {
    world_.mode = SimulationMode::Operation;
    world_.size = {GameplayConfig::MissionWidth, GameplayConfig::MissionHeight};
    ResetSecurity();
    for (auto &agent : world_.agents) { agent.cover = {}; agent.decoyGuide = {}; agent.hideoutId = 0; agent.hideoutExit = {}; agent.hidden = false; agent.draggingGuard = -1; agent.actionPose = ActionPose::None; agent.actionPoseTime = 0.f; }
    world_.obstacles.clear(); world_.guards.clear(); world_.objects.clear(); world_.footsteps.clear();
    const float offset = static_cast<float>(seed % 3) * 38.f;
    // Los módulos mantienen corredores exteriores y accesos entre todas las zonas de objetivos.
    world_.obstacles = {
      {{{220.f, 150.f + offset}, {28.f, 255.f}}, CoverKind::High, true, "Muro oeste"},
      {{{220.f, 465.f + offset}, {28.f, 120.f}}, CoverKind::High, true, "Muro oeste"},
      {{{470.f, 180.f}, {220.f, 28.f}}, CoverKind::High, true, "Sala de archivos"},
      {{{665.f, 180.f}, {28.f, 190.f}}, CoverKind::High, true, "Sala de archivos"},
      {{{410.f, 440.f - offset}, {130.f, 34.f}}, CoverKind::Low, true, "Cobertura baja"},
      {{{735.f, 440.f + offset}, {95.f, 44.f}}, CoverKind::High, true, "Contenedores"},
      {{{325.f, 250.f}, {70.f, 40.f}}, CoverKind::Low, true, "Consola"},
      {{{65.f, 260.f}, {65.f, 70.f}}, CoverKind::Hide, false, "Escondite"},
      {{{850.f, 590.f}, {70.f, 70.f}}, CoverKind::Hide, false, "Escondite"}
    };
    world_.objects = {
      {10, ObjectKind::Door, {234.f, 435.f + offset}, "Puerta de servicio"},
      {11, ObjectKind::Terminal, {570.f, 275.f}, "Archivo principal"},
      {12, ObjectKind::Secondary, {860.f, 190.f}, "Archivo secundario"},
      {13, ObjectKind::Camera, {600.f, 390.f}, "Cámara"},
      {14, ObjectKind::Hideout, {95.f, 295.f}, "Escondite oeste"},
      {15, ObjectKind::Hideout, {885.f, 625.f}, "Escondite este"},
      {16, ObjectKind::CoopSwitch, {840.f, 360.f}, "Repetidor cooperativo"},
      {17, ObjectKind::Extraction, {105.f, 665.f}, "Extracción"},
      {18, ObjectKind::AlarmPanel, {350.f, 110.f}, "Central de seguridad"}
    };
    for (auto &object : world_.objects) {
      if (object.kind == ObjectKind::Door) { object.locked = true; object.puzzle.kind = PuzzleKind::Door; object.puzzle.difficulty = 2u + seed % 2u; }
      if (object.kind == ObjectKind::Camera) { object.camera.sweeping = true; object.camera.baseAngle = 3.14159265f; object.camera.sweepAngle = object.camera.minAngle; const float initialAngle = object.camera.baseAngle + object.camera.sweepAngle; object.facing = {std::cos(initialAngle), std::sin(initialAngle)}; }
    }
    Interactable explosive{21, ObjectKind::Explosive, {300.f, 550.f}, "Explosivo oculto"};
    explosive.puzzle.kind = PuzzleKind::Explosive; explosive.puzzle.difficulty = 3u + seed % 2u;
    explosive.footprint = {16.f, 12.f}; explosive.solid = SpatialOverride::Disabled; explosive.blocksVision = SpatialOverride::Disabled;
    world_.objects.push_back(explosive);
    Interactable computer{22, ObjectKind::Computer, {770.f, 590.f}, "Computador de inteligencia"};
    computer.puzzle.kind = PuzzleKind::Computer; computer.puzzle.difficulty = 3u + static_cast<unsigned int>(world_.contract);
    computer.footprint = {38.f, 28.f};
    computer.puzzle.intelligence = world_.contract == ContractId::SilentRelay ? "INTELIGENCIA: el repetidor está en el sector este (840,360). La central controla el grupo de cámaras 1." :
      world_.contract == ContractId::DoubleCross ? "INTELIGENCIA: archivo principal (570,275), archivo secundario (860,190). Las órdenes privadas requieren verificar la lealtad del equipo." :
      "INTELIGENCIA: archivo principal (570,275). El corredor sur evita la cerradura de servicio; el panel norte desactiva las cámaras durante 45 segundos.";
    world_.objects.push_back(computer);
    Interactable cameraPanel{23, ObjectKind::CameraPanel, {510.f, 110.f}, "Panel de cámaras"};
    cameraPanel.puzzle.kind = PuzzleKind::CameraPanel; cameraPanel.puzzle.difficulty = 3;
    cameraPanel.footprint = {28.f, 24.f}; world_.objects.push_back(cameraPanel);
    for (auto &object : world_.objects) if (object.kind == ObjectKind::Hideout) object.hideoutKind = HideoutKind::TrashBin;
    Interactable closet{19, ObjectKind::Hideout, {600.f, 650.f}, "Armario de mantenimiento"};
    closet.hideoutKind = HideoutKind::Closet; world_.objects.push_back(closet);
    Interactable table{20, ObjectKind::Hideout, {360.f, 650.f}, "Mesa de servicio"};
    table.hideoutKind = HideoutKind::UnderTable; world_.objects.push_back(table);
    for (std::uint32_t index = 0; index < 3 + seed % 2; ++index) {
      Guard guard;
      guard.id = index + 1;
      if (index == 0) guard.patrol = {{330.f, 370.f}, {550.f, 370.f}, {550.f, 570.f}, {330.f, 570.f}};
      else if (index == 1) guard.patrol = {{740.f, 110.f}, {910.f, 110.f}, {910.f, 300.f}, {740.f, 300.f}};
      else if (index == 2) guard.patrol = {{315.f, 95.f}, {610.f, 95.f}, {610.f, 140.f}, {315.f, 140.f}};
      else guard.patrol = {{720.f, 580.f}, {920.f, 580.f}, {920.f, 690.f}, {720.f, 690.f}};
      guard.waypoint = (seed + index) % guard.patrol.size();
      guard.position = guard.patrol[guard.waypoint];
      guard.waypoint = (guard.waypoint + 1) % guard.patrol.size();
      guard.facing = Unit(guard.patrol[guard.waypoint] - guard.position);
      world_.guards.push_back(guard);
    }
    world_.securityEntries = {{{982.f, 110.f}, {910.f, 110.f}}, {{982.f, 690.f}, {920.f, 690.f}}, {{315.f, 18.f}, {315.f, 95.f}}};
  }

  void Simulation::ReturnToBase() {
    for (std::size_t index = 0; index < world_.agents.size(); ++index) {
      auto &agent = world_.agents[index];
      agent.position = {130.f + static_cast<float>(index) * 35.f, 150.f};
      agent.lifeState = LifeState::Alive; agent.rescueExpired = false; agent.alive = true; agent.incapacitated = false; agent.bodyReported = false; agent.extracted = false; agent.hidden = false; agent.stance = Stance::Standing;
      agent.health = GameplayConfig::MaxHealth; agent.stamina = GameplayConfig::MaxStamina; agent.incapacityRemaining = 0.f; agent.running = false; agent.moving = false;
      agent.privateOrder = PrivateOrder::None; agent.privateComplete = false; agent.draggingGuard = -1;
    }
    world_.elapsed = 0.f; world_.alarm = false; world_.noises.clear();
    BuildBase();
  }

  bool Simulation::Blocked(sf::Vector2f position, float radius) const {
    return WorldGeometry::PositionBlocked(world_, position, radius);
  }

  void Simulation::ResolvePenetration(sf::Vector2f &position, float radius) const {
    if (!std::isfinite(position.x) || !std::isfinite(position.y)) return;
    position.x = std::clamp(position.x, radius, std::max(radius, world_.size.x - radius));
    position.y = std::clamp(position.y, radius, std::max(radius, world_.size.y - radius));
    if (!Blocked(position, radius)) return;
    const auto origin = position;
    float bestDistance = std::numeric_limits<float>::infinity();
    const auto consider = [&](sf::Vector2f candidate) {
      const float distance = (candidate - origin).lengthSquared();
      if (distance < bestDistance && !Blocked(candidate, radius)) { bestDistance = distance; position = candidate; }
    };
    WorldGeometry::ForEachSolid(world_, [&](const sf::FloatRect &bounds) {
      const auto expanded = WorldGeometry::Expanded(bounds, radius + SpatialConfig::CollisionSkin);
      const auto minimum = expanded.position, maximum = expanded.position + expanded.size;
      consider({minimum.x, origin.y}); consider({maximum.x, origin.y});
      consider({origin.x, minimum.y}); consider({origin.x, maximum.y});
      consider(minimum); consider(maximum); consider({minimum.x, maximum.y}); consider({maximum.x, minimum.y});
    });
  }

  void Simulation::Move(sf::Vector2f &position, sf::Vector2f displacement, float radius) const {
    ResolvePenetration(position, radius);
    if (!std::isfinite(displacement.x) || !std::isfinite(displacement.y)) return;
    const float maximumStep = std::max(SpatialConfig::CollisionSkin, std::min(SpatialConfig::MaximumMovementStep, radius * 0.5f));
    const int steps = std::max(1, static_cast<int>(std::ceil(Length(displacement) / maximumStep)));
    const auto increment = displacement / static_cast<float>(steps);
    for (int step = 0; step < steps; ++step) {
      const auto horizontal = position + sf::Vector2f{increment.x, 0.f};
      if (!Blocked(horizontal, radius)) position = horizontal;
      const auto vertical = position + sf::Vector2f{0.f, increment.y};
      if (!Blocked(vertical, radius)) position = vertical;
    }
  }

  void Simulation::Navigate(Guard &guard, sf::Vector2f target, float speed, float dt) {
    using GameplayConfig::AgentRadius;
    using namespace NavigationConfig;
    if (dt <= 0.f || speed <= 0.f) return;
    const auto previousPosition = guard.position;
    ResolvePenetration(guard.position, AgentRadius);
    if (guard.position != previousPosition) { guard.navigation.clear(); guard.navigationTimer = 0.f; }
    guard.navigationTimer = std::max(0.f, guard.navigationTimer - dt);
    const auto pathClear = [this](sf::Vector2f from, sf::Vector2f to) {
      if (Blocked(to, AgentRadius)) return false;
      bool blocked = false;
      const float radius = AgentRadius - SpatialConfig::CollisionSkin;
      const auto movement = to - from;
      WorldGeometry::ForEachSolid(world_, [&](const sf::FloatRect &bounds) {
        blocked |= WorldGeometry::SegmentRect(from, to, {bounds.position - sf::Vector2f{radius, 0.f}, bounds.size + sf::Vector2f{radius * 2.f, 0.f}});
        blocked |= WorldGeometry::SegmentRect(from, to, {bounds.position - sf::Vector2f{0.f, radius}, bounds.size + sf::Vector2f{0.f, radius * 2.f}});
        const auto maximum = bounds.position + bounds.size;
        for (const auto corner : {bounds.position, maximum, sf::Vector2f{bounds.position.x, maximum.y}, sf::Vector2f{maximum.x, bounds.position.y}}) {
          const float along = movement.lengthSquared() > 0.f ? std::clamp((corner - from).dot(movement) / movement.lengthSquared(), 0.f, 1.f) : 0.f;
          blocked |= (corner - from - movement * along).lengthSquared() < radius * radius;
        }
      });
      return !blocked;
    };
    const auto nearby = [&] {
      sf::Vector2f result = guard.position;
      float bestDistance = std::numeric_limits<float>::infinity();
      for (const auto direction : std::array<sf::Vector2f, 8>{{{1.f, 0.f}, {0.f, 1.f}, {-1.f, 0.f}, {0.f, -1.f}, {1.f, 1.f}, {-1.f, 1.f}, {-1.f, -1.f}, {1.f, -1.f}}}) {
        const auto candidate = guard.position + Unit(direction) * NearbySearchRadius;
        const float distance = Distance(candidate, target);
        if (pathClear(guard.position, candidate) && distance < bestDistance) { bestDistance = distance; result = candidate; }
      }
      return result;
    };
    const bool rejected = guard.navigationRejectTime > 0.f && Distance(guard.navigationRejectedTarget, target) <= CellSize;
    if (rejected) target = guard.navigationRecoveryTarget;
    if (Distance(guard.navigationTarget, target) > CellSize) {
      guard.navigation.clear(); guard.navigationTimer = 0.f; guard.navigationStuckTime = 0.f; guard.navigationFailures = 0;
    }
    guard.navigationTarget = target;
    const auto buildPath = [&] {
        guard.navigation.clear(); guard.navigationTimer = RefreshInterval;
        const int columns = static_cast<int>(std::ceil(world_.size.x / CellSize));
        const int rows = static_cast<int>(std::ceil(world_.size.y / CellSize));
        if (columns <= 0 || rows <= 0 || columns * rows > MaximumCells) return;
        const auto pointAt = [columns](int index) { return sf::Vector2f{(index % columns + 0.5f) * CellSize, (index / columns + 0.5f) * CellSize}; };
        std::vector<int> previous(static_cast<std::size_t>(columns * rows), -1);
        std::queue<int> queue;
        const int startX = static_cast<int>(guard.position.x / CellSize), startY = static_cast<int>(guard.position.y / CellSize);
        // Los primeros nodos parten de la posición real y no cortan la esquina de su celda.
        for (int y = std::max(0, startY - 1); y <= std::min(rows - 1, startY + 1); ++y) for (int x = std::max(0, startX - 1); x <= std::min(columns - 1, startX + 1); ++x) {
          const int index = y * columns + x;
          if (pathClear(guard.position, pointAt(index))) { previous[index] = index; queue.push(index); }
        }
        int finish = -1;
        int closest = -1;
        float closestDistance = Distance(guard.position, target);
        while (!queue.empty()) {
          const int current = queue.front(); queue.pop();
          const int x = current % columns, y = current / columns;
          const auto point = pointAt(current);
          const float distance = Distance(point, target);
          if (distance < closestDistance) { closestDistance = distance; closest = current; }
          if (pathClear(point, target)) { finish = current; break; }
          for (const auto [dx, dy] : std::array<std::pair<int, int>, 4>{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}}) {
            const int nx = x + dx, ny = y + dy;
            if (nx < 0 || ny < 0 || nx >= columns || ny >= rows) continue;
            const int next = ny * columns + nx;
            if (previous[next] >= 0 || !pathClear(point, pointAt(next))) continue;
            previous[next] = current; queue.push(next);
          }
        }
        const bool reachesTarget = finish >= 0;
        if (!reachesTarget) finish = closest;
        for (int current = finish; current >= 0; current = previous[current]) {
          guard.navigation.push_back(pointAt(current));
          if (previous[current] == current) break;
        }
        std::ranges::reverse(guard.navigation);
        if (reachesTarget) guard.navigation.push_back(target);
    };
    if (pathClear(guard.position, target)) { guard.navigation.clear(); guard.navigation.push_back(target); }
    else if ((guard.navigation.empty() && guard.navigationTimer <= 0.f) ||
        (!guard.navigation.empty() && !pathClear(guard.position, guard.navigation.front()))) buildPath();
    while (!guard.navigation.empty() && Distance(guard.position, guard.navigation.front()) <= WaypointTolerance) guard.navigation.erase(guard.navigation.begin());
    const auto beforeMovement = guard.position;
    if (!guard.navigation.empty()) {
      const auto offset = guard.navigation.front() - guard.position;
      guard.facing = Unit(offset);
      Move(guard.position, guard.facing * std::min(Length(offset), speed * dt), AgentRadius);
    }
    if (Distance(guard.position, target) <= WaypointTolerance) { guard.navigationStuckTime = 0.f; guard.navigationFailures = 0; return; }
    if (Distance(guard.position, beforeMovement) >= std::min(MinimumStepProgress, speed * dt * 0.5f)) guard.navigationStuckTime = 0.f;
    else guard.navigationStuckTime += dt;
    if (guard.navigationStuckTime < StuckInterval || rejected) return;
    guard.navigationStuckTime = 0.f;
    guard.navigationFailures = std::min(guard.navigationFailures + 1, RejectFailure);
    if (guard.navigationFailures == RecalculateFailure) { guard.navigation.clear(); guard.navigationTimer = 0.f; }
    else if (guard.navigationFailures == NearbyFailure) {
      guard.navigation.clear(); guard.navigationTimer = RefreshInterval;
      guard.navigation.push_back(nearby());
    } else {
      guard.navigationRejectedTarget = target; guard.navigationRecoveryTarget = nearby(); guard.navigationRejectTime = RejectDuration;
      guard.navigation.clear(); guard.navigationTimer = 0.f;
      if (guard.state == GuardState::Patrol && guard.patrol.size() > 1) guard.waypoint = (guard.waypoint + 1) % guard.patrol.size();
    }
  }

  bool Simulation::Occluded(sf::Vector2f from, sf::Vector2f to, Stance stance) const {
    return !Visibility::HasLineOfSight(world_, from, to, stance);
  }

  bool Simulation::CanSee(sf::Vector2f from, sf::Vector2f facing, const Agent &agent, float range) const {
    if (!agent.alive || agent.incapacitated || !agent.connected || agent.extracted) return false;
    const auto delta = agent.position - from;
    const float distance = Length(delta);
    const float postureFactor = agent.stance == Stance::Prone ? GameplayConfig::ProneVisionFactor : (agent.stance == Stance::Crouched ? GameplayConfig::CrouchedVisionFactor : 1.f);
    if (distance > range * postureFactor) return false;
    if (distance > GameplayConfig::CloseVisionDistance && Unit(facing).dot(Unit(delta)) < GameplayConfig::VisionCosHalfAngle) return false;
    return Visibility::AgentExposed(world_, from, agent);
  }

  void Simulation::Record(EventKind kind, PlayerId actor, std::uint32_t target) {
    world_.events.push_back({nextEvent_++, world_.tick, actor, kind, target});
    sf::Vector2f position;
    for (const auto &agent : world_.agents) if (agent.id == actor) position = agent.position;
    if (actor == 0) for (const auto &guard : world_.guards) if (guard.id == target) position = guard.position;
    switch (kind) {
    case EventKind::GuardShot: EmitSound(SoundKind::Rifle, position, GameplayConfig::LethalNoiseRadius); break;
    case EventKind::GuardMelee: EmitSound(SoundKind::Punch, position, AudioConfig::ImpactRadius); break;
    case EventKind::GuardIncapacitated:
      for (const auto &guard : world_.guards) if (guard.id == target) position = guard.position;
      EmitSound(SoundKind::Fall, position, AudioConfig::FallRadius, actor); break;
    case EventKind::AgentAlarm:
      for (const auto &object : world_.objects) if (object.id == target || (target >= 1000 && object.id == target - 1000)) position = object.position;
      EmitSound(SoundKind::Alarm, position, AudioConfig::AlarmRadius, actor);
      EmitNoise(position, AudioConfig::AlarmRadius, actor, 0, SoundKind::Alarm); break;
    default: break;
    }
  }

  void Simulation::EmitNoise(sf::Vector2f position, float radius, PlayerId actor, std::uint32_t guardId, SoundKind kind) {
    if (!std::isfinite(radius) || radius <= 0.f) return;
    if (world_.noises.size() >= AudioConfig::MaximumCues) world_.noises.erase(world_.noises.begin());
    world_.noises.push_back({position, radius, GameplayConfig::NoiseDuration, actor, guardId, kind, GameplayConfig::NoiseDuration});
    Record(EventKind::Noise, actor);
  }

  void Simulation::HandleDistractions(Agent &agent, const ActionCommand &command) {
    using namespace DistractionConfig;
    if ((!command.throwDecoy && !command.knock) || !IsTactical(world_) || !agent.connected ||
        !agent.alive || agent.incapacitated || agent.hidden || agent.extracted || agent.actionCooldown > 0.f) return;
    const bool held = std::ranges::any_of(world_.guards, [&agent](const Guard &guard) { return guard.heldBy == agent.id && guard.cqcState != CqcState::None; });
    if (agent.draggingGuard >= 0 || command.dragBody || command.interact || command.tool || held) {
      world_.message = "Termina la interacción o suelta el cuerpo antes de usar una distracción.";
      return;
    }
    if (command.throwDecoy) {
      if (agent.loadout.decoys <= 0) { world_.message = "No quedan señuelos."; return; }
      const sf::Vector2f target{command.aimX, command.aimY};
      const auto trajectory = DistractionGeometry::TraceDecoy(world_, agent.position, target.lengthSquared() > 0.01f ? Unit(target) : agent.facing, command.decoyDistance);
      if (!trajectory.valid) { world_.message = "Lanzamiento bloqueado: busca una trayectoria despejada."; return; }
      if (world_.decoys.size() >= MaximumFlyingDecoys) { world_.message = "Espera a que aterricen los discos en vuelo."; return; }
      --agent.loadout.decoys;
      world_.decoys.push_back({trajectory.from, trajectory.to, 0.f, std::max(DecoyMinimumFlightTime, trajectory.travelled / DecoyFlightSpeed), DecoyArcHeight, agent.id, trajectory.blocked});
      agent.actionPose = ActionPose::ThrowDecoy; agent.actionPoseTime = GameplayConfig::AttackAnimationDuration;
      agent.actionCooldown = GameplayConfig::WeaponCooldown;
      world_.message = trajectory.blocked ? "Señuelo detenido por un obstáculo: las patrullas investigarán el punto de caída." : "Señuelo lanzado: las patrullas investigarán el punto de caída.";
      return;
    }
    if (!agent.cover.active || !CoverSurfaceValid(agent.cover) || agent.stance == Stance::Prone) { world_.message = "Usa F junto a una pared mientras estás en cobertura."; return; }
    const auto contact = WorldGeometry::ClosestPoint(agent.cover.anchor, agent.cover.surface);
    if (Distance(agent.position, contact) > KnockDistance) { world_.message = "Acércate a la pared para golpearla."; return; }
    agent.facing = agent.cover.normal;
    EmitNoise(contact, KnockHearingDistance, agent.id, 0, SoundKind::Knock);
    EmitSound(SoundKind::Knock, contact, KnockHearingDistance, agent.id);
    agent.actionPose = ActionPose::KnockWall; agent.actionPoseTime = GameplayConfig::AttackAnimationDuration;
    agent.actionCooldown = GameplayConfig::WeaponCooldown;
    world_.message = "Golpe en la pared: las patrullas cercanas investigarán su origen.";
  }

  void Simulation::ReportPerception(Agent &agent, float progress, bool confirmed) {
    agent.detectionProgress = std::max(agent.detectionProgress, std::clamp(progress, 0.f, 1.f));
    agent.detectionIndicatorRemaining = GameplayConfig::SuspicionIndicatorDuration;
    if (confirmed) { agent.confirmedIndicatorRemaining = GameplayConfig::ConfirmedIndicatorDuration; agent.detectionConfirmed = true; }
  }

  void Simulation::Detect(Agent &agent, std::uint32_t observer) {
    if (!agent.connected || !agent.alive || agent.incapacitated || agent.extracted) return;
    Record(EventKind::ConfirmedDetection, agent.id, observer);
    if (!world_.alarm) {
      world_.alarm = true; ++world_.alarmCount;
      Record(EventKind::AgentAlarm, agent.id, observer);
    }
    RegisterSecurityContact(agent.position);
    ReportPerception(agent, 1.f, true);
    world_.message = "DETECCIÓN CONFIRMADA: rompe la línea de visión. Maestro ya no es elegible.";
  }

  void Simulation::Step(float deltaTime, const std::unordered_map<PlayerId, ActionCommand> &commands) {
    if (world_.phase == GamePhase::Results || !std::isfinite(deltaTime) || deltaTime <= 0.f) return;
    if (world_.phase == GamePhase::Mission) { CheckResults(); if (world_.phase == GamePhase::Results) return; }
    const float dt = std::min(deltaTime, GameplayConfig::MaximumStep);
    ++world_.tick;
    for (auto &sound : world_.sounds) sound.remaining -= dt;
    std::erase_if(world_.sounds, [](const SoundEvent &sound) { return sound.remaining <= 0.f; });
    if (IsTactical(world_)) world_.elapsed += dt;
    if (IsTraining(world_)) UpdateTraining(dt);
    for (auto &shot : world_.shots) shot.remaining -= dt;
    std::erase_if(world_.shots, [](const ShotTrace &shot) { return shot.remaining <= 0.f; });
    // Todos los plazos vencen antes de procesar órdenes para que el rescate no dependa del orden de agentes.
    for (auto &agent : world_.agents) if (agent.connected && agent.alive && agent.incapacitated && !agent.extracted) {
      agent.incapacityRemaining = std::max(0.f, agent.incapacityRemaining - dt);
      if (agent.incapacityRemaining <= 0.f) ConfirmDeath(agent, 0, true);
    }
    // La recuperación precede a todas las órdenes para que un impacto reinicie el plazo sin depender del orden de agentes.
    if (world_.phase == GamePhase::Mission && !IsTraining(world_)) for (auto &agent : world_.agents) {
      if (!agent.connected || !agent.alive || agent.incapacitated || agent.extracted || agent.lifeState != LifeState::Alive || agent.health <= 0.f) continue;
      const float recoveryTime = std::max(0.f, dt - agent.healthRecoveryRemaining);
      agent.healthRecoveryRemaining = std::max(0.f, agent.healthRecoveryRemaining - dt);
      agent.health = std::min(GameplayConfig::MaxHealth, agent.health + GameplayConfig::PassiveHealthRecoveryPerSecond * recoveryTime);
    }
    for (auto &agent : world_.agents) {
      agent.transitionTime = std::max(0.f, agent.transitionTime - dt);
      agent.animationTime += dt;
      if (!agent.alive || agent.incapacitated) agent.actionPoseTime = std::max(0.f, agent.actionPoseTime - dt);
      if (!agent.connected || !agent.alive || agent.extracted || agent.incapacitated) {
        agent.aiming = false; agent.detectionProgress = 0.f; agent.detectionConfirmed = false;
        agent.detectionIndicatorRemaining = agent.confirmedIndicatorRemaining = 0.f;
      }
      agent.detectionIndicatorRemaining = std::max(0.f, agent.detectionIndicatorRemaining - dt);
      agent.confirmedIndicatorRemaining = std::max(0.f, agent.confirmedIndicatorRemaining - dt);
      agent.detectionConfirmed = agent.confirmedIndicatorRemaining > 0.f;
      if (agent.detectionIndicatorRemaining == 0.f && !agent.detectionConfirmed) agent.detectionProgress = 0.f;
      if (!agent.connected || !agent.alive || agent.extracted) continue;
      if (agent.incapacitated) continue;
      if (!IsTraining(world_) && agent.health <= 0.f) { Incapacitate(agent, 0, agent.id); continue; }
      const auto found = commands.find(agent.id);
      UpdateAgent(agent, found == commands.end() ? ActionCommand{} : found->second, dt);
    }
    if (IsTactical(world_)) {
      UpdateDecoys(dt);
      if (!IsTraining(world_)) UpdateGuards(dt);
      UpdateObjects(dt);
      if (!IsTraining(world_)) UpdateSecurity(dt);
      for (auto &noise : world_.noises) noise.remaining -= dt;
      std::erase_if(world_.noises, [](const NoiseEvent &noise) { return noise.remaining <= 0.f; });
      CheckResults();
    }
  }

  bool Simulation::CoverSurfaceValid(const AgentCover &cover) const {
    if (!cover.active || std::abs(Length(cover.normal) - 1.f) > 0.001f || (std::abs(cover.normal.x) > 0.001f && std::abs(cover.normal.y) > 0.001f)) return false;
    if (cover.obstacle >= 0) {
      const auto index = static_cast<std::size_t>(cover.obstacle);
      return index < world_.obstacles.size() && WorldGeometry::SupportsWallCover(world_.obstacles[index]) && world_.obstacles[index].bounds == cover.surface;
    }
    const auto found = std::ranges::find_if(world_.objects, [&](const Interactable &object) { return object.id == cover.object; });
    return cover.object != 0 && found != world_.objects.end() && WorldGeometry::SupportsWallCover(*found) && WorldGeometry::ObjectFootprint(*found) == cover.surface;
  }

  void Simulation::ToggleCover(Agent &agent) {
    if (agent.cover.active) { agent.cover = {}; world_.message = "Cobertura abandonada: movimiento libre."; return; }
    if (agent.stance == Stance::Prone || agent.draggingGuard >= 0) { world_.message = "Levántate y suelta el cuerpo para apoyarte en una pared."; return; }
    AgentCover nearest;
    float distance = SpatialConfig::CoverAttachDistance;
    const auto consider = [&](const sf::FloatRect &surface, int obstacle, std::uint32_t object) {
      const float radius = GameplayConfig::AgentRadius + SpatialConfig::CollisionSkin;
      const auto minimum = surface.position, maximum = surface.position + surface.size;
      const std::array<sf::Vector2f, 4> normals{{{-1.f, 0.f}, {1.f, 0.f}, {0.f, -1.f}, {0.f, 1.f}}};
      const std::array<sf::Vector2f, 4> anchors{{
          {minimum.x - radius, std::clamp(agent.position.y, minimum.y, maximum.y)},
          {maximum.x + radius, std::clamp(agent.position.y, minimum.y, maximum.y)},
          {std::clamp(agent.position.x, minimum.x, maximum.x), minimum.y - radius},
          {std::clamp(agent.position.x, minimum.x, maximum.x), maximum.y + radius}}};
      for (std::size_t side = 0; side < anchors.size(); ++side) {
        const float gap = Distance(agent.position, anchors[side]);
        if (gap > distance || Blocked(anchors[side], GameplayConfig::AgentRadius)) continue;
        auto reached = agent.position; Move(reached, anchors[side] - reached, GameplayConfig::AgentRadius);
        if (Distance(reached, anchors[side]) > SpatialConfig::CoverPositionTolerance) continue;
        nearest.active = true; nearest.anchor = anchors[side]; nearest.normal = normals[side]; nearest.surface = surface;
        nearest.obstacle = obstacle; nearest.object = object; distance = gap;
      }
    };
    for (std::size_t index = 0; index < world_.obstacles.size(); ++index) if (WorldGeometry::SupportsWallCover(world_.obstacles[index])) consider(world_.obstacles[index].bounds, static_cast<int>(index), 0);
    for (const auto &object : world_.objects) if (WorldGeometry::SupportsWallCover(object)) consider(WorldGeometry::ObjectFootprint(object), -1, object.id);
    if (!nearest.active) { world_.message = "No hay una pared o cobertura alta válida a tu alcance."; return; }
    agent.cover = nearest; agent.position = nearest.anchor; agent.facing = nearest.normal; agent.hidden = false; agent.running = false;
    world_.message = "Cobertura: espalda a la pared; acércate al extremo para asomarte automáticamente.";
  }

  bool Simulation::CoverPeekTarget(const AgentCover &cover, PeekSide side, sf::Vector2f &target) const {
    return CoverSurfaceValid(cover) && WorldGeometry::CoverPeekPosition(cover, side, target) &&
           WorldGeometry::ClearMovementSegment(world_, cover.anchor, target, GameplayConfig::AgentRadius);
  }

  bool Simulation::UpdateCover(Agent &agent, const ActionCommand &command, sf::Vector2f direction, float dt) {
    if (!agent.cover.active) return false;
    if (!CoverSurfaceValid(agent.cover) || agent.stance == Stance::Prone || agent.draggingGuard >= 0 || Blocked(agent.cover.anchor, GameplayConfig::AgentRadius)) { agent.cover = {}; return false; }
    auto &cover = agent.cover;
    sf::Vector2f left, right;
    cover.canPeekLeft = CoverPeekTarget(cover, PeekSide::Left, left);
    cover.canPeekRight = CoverPeekTarget(cover, PeekSide::Right, right);
    const bool automaticLeft = cover.canPeekLeft && Distance(cover.anchor, left) <= SpatialConfig::AutomaticPeekDistance;
    const bool automaticRight = cover.canPeekRight && Distance(cover.anchor, right) <= SpatialConfig::AutomaticPeekDistance;
    const bool wantsLeft = (command.peekLeft || (!command.peekRight && automaticLeft)) && cover.canPeekLeft;
    const bool wantsRight = !wantsLeft && (command.peekRight || automaticRight) && cover.canPeekRight;
    const sf::Vector2f tangent{-cover.normal.y, cover.normal.x};
    if (!command.peekLeft && !command.peekRight) {
      auto destination = cover.anchor + tangent * (direction.dot(tangent) * SpatialConfig::CoverSlideSpeed * dt);
      if (std::abs(cover.normal.x) > 0.5f) destination.y = std::clamp(destination.y, cover.surface.position.y, cover.surface.position.y + cover.surface.size.y);
      else destination.x = std::clamp(destination.x, cover.surface.position.x, cover.surface.position.x + cover.surface.size.x);
      Move(cover.anchor, destination - cover.anchor, GameplayConfig::AgentRadius);
    }
    const auto destination = wantsLeft ? left : wantsRight ? right : cover.anchor;
    cover.peekSide = wantsLeft ? PeekSide::Left : wantsRight ? PeekSide::Right : PeekSide::None;
    const auto movement = destination - agent.position;
    Move(agent.position, Unit(movement) * std::min(Length(movement), SpatialConfig::CoverPeekSpeed * dt), GameplayConfig::AgentRadius);
    cover.canPeekLeft = CoverPeekTarget(cover, PeekSide::Left, left);
    cover.canPeekRight = CoverPeekTarget(cover, PeekSide::Right, right);
    agent.running = false;
    agent.facing = cover.normal;
    return true;
  }

  bool Simulation::HideoutPathClear(const Interactable &object, sf::Vector2f from, sf::Vector2f to) const {
    const float radius = GameplayConfig::AgentRadius;
    for (const auto &obstacle : world_.obstacles) if (WorldGeometry::BlocksMovement(obstacle) && WorldGeometry::SegmentRect(from, to, WorldGeometry::Expanded(obstacle.bounds, radius))) return false;
    for (const auto &other : world_.objects) if (other.id != object.id && WorldGeometry::BlocksMovement(other) && WorldGeometry::SegmentRect(from, to, WorldGeometry::Expanded(WorldGeometry::ObjectFootprint(other), radius))) return false;
    return true;
  }

  bool Simulation::HideoutExit(const Agent &agent, const Interactable &object, sf::Vector2f &position) const {
    if (WorldGeometry::HideoutType(object) == HideoutKind::Closet) {
      const std::array<sf::Vector2f, 2> candidates{{agent.hideoutExit, WorldGeometry::HideoutFrontPosition(object, GameplayConfig::AgentRadius)}};
      for (const auto candidate : candidates) if (WorldGeometry::AtHideoutEntrance(object, candidate, GameplayConfig::AgentRadius) &&
          !Blocked(candidate, GameplayConfig::AgentRadius) && HideoutPathClear(object, object.position, candidate)) { position = candidate; return true; }
      return false;
    }
    if (WorldGeometry::HideoutType(object) == HideoutKind::UnderTable) {
      const auto direction = Unit(agent.facing);
      if (Length(direction) <= 0.f) return false;
      const auto expanded = WorldGeometry::Expanded(WorldGeometry::ObjectFootprint(object), GameplayConfig::AgentRadius + SpatialConfig::CollisionSkin);
      const auto minimum = expanded.position, maximum = expanded.position + expanded.size;
      const float horizontal = direction.x > 0.f ? (maximum.x - agent.position.x) / direction.x : direction.x < 0.f ? (minimum.x - agent.position.x) / direction.x : std::numeric_limits<float>::infinity();
      const float vertical = direction.y > 0.f ? (maximum.y - agent.position.y) / direction.y : direction.y < 0.f ? (minimum.y - agent.position.y) / direction.y : std::numeric_limits<float>::infinity();
      const float distance = std::min(horizontal, vertical) + SpatialConfig::CollisionSkin;
      if (!std::isfinite(distance) || distance <= 0.f) return false;
      const auto candidate = agent.position + direction * distance;
      if (Blocked(candidate, GameplayConfig::AgentRadius) || !HideoutPathClear(object, agent.position, candidate)) return false;
      position = candidate; return true;
    }
    const auto bounds = WorldGeometry::ObjectFootprint(object);
    const auto minimum = bounds.position, maximum = bounds.position + bounds.size;
    const float radius = GameplayConfig::AgentRadius + SpatialConfig::CollisionSkin;
    const std::array<sf::Vector2f, 9> candidates{{agent.hideoutExit,
        {minimum.x - radius, object.position.y}, {maximum.x + radius, object.position.y},
        {object.position.x, minimum.y - radius}, {object.position.x, maximum.y + radius},
        minimum - sf::Vector2f{radius, radius}, maximum + sf::Vector2f{radius, radius},
        {minimum.x - radius, maximum.y + radius}, {maximum.x + radius, minimum.y - radius}}};
    for (const auto candidate : candidates) if (!Blocked(candidate, GameplayConfig::AgentRadius) && HideoutPathClear(object, object.position, candidate)) { position = candidate; return true; }
    return false;
  }

  void Simulation::ReleaseHideout(Agent &agent) {
    agent.decoyGuide = {};
    if (agent.hideoutId != 0) {
      ReleaseCqc(agent);
      const auto object = std::ranges::find_if(world_.objects, [&](const Interactable &entry) { return entry.id == agent.hideoutId; });
      if (object != world_.objects.end()) {
        sf::Vector2f exit;
        if (HideoutExit(agent, *object, exit)) agent.position = exit;
        if (object->occupantAgent == agent.id) { object->occupantAgent = 0; object->occupied = object->occupantGuard != 0; }
      }
    }
    agent.hideoutId = 0; agent.hideoutExit = {}; agent.hidden = false; agent.actionPose = ActionPose::None; agent.actionPoseTime = 0.f;
    ResolvePenetration(agent.position, GameplayConfig::AgentRadius);
  }

  bool Simulation::UseHideout(Agent &agent, Interactable &object) {
    if (WorldGeometry::HideoutType(object) == HideoutKind::None || !WorldGeometry::Properties(object).interactive) return false;
    if (object.lifted) { world_.message = "La mesa está levantada y ya no proporciona ocultación."; return true; }
    if (agent.hideoutId == object.id && object.occupantAgent == agent.id) {
      sf::Vector2f exit;
      if (!HideoutExit(agent, object, exit)) { world_.message = "La salida está bloqueada; espera una posición libre."; return true; }
      object.occupantAgent = 0; object.occupied = object.occupantGuard != 0; agent.position = exit; agent.hideoutId = 0; agent.hideoutExit = {}; agent.hidden = false;
      object.inspectionProgress = 0.f;
      if (WorldGeometry::HideoutType(object) == HideoutKind::Closet) object.open = false;
      const bool table = WorldGeometry::HideoutType(object) == HideoutKind::UnderTable;
      agent.actionPose = table ? ActionPose::None : ActionPose::HideExit; agent.actionPoseTime = table ? 0.f : GameplayConfig::StanceTransitionDuration;
      if (table) { agent.transitionTime = 0.f; agent.transitionFrom = agent.stance; }
      EmitSound(SoundKind::Hideout, agent.position, AudioConfig::QuietRadius, agent.id);
      world_.message = "Has salido del escondite."; return true;
    }
    if (WorldGeometry::HideoutOccupied(object)) { world_.message = "Este escondite ya está ocupado."; return true; }
    if (!WorldGeometry::AtHideoutEntrance(object, agent.position, GameplayConfig::AgentRadius)) { world_.message = "El armario solo permite acceder por su puerta frontal."; return true; }
    if (!HideoutPathClear(object, agent.position, object.position)) { world_.message = "Una superficie impide acceder al escondite."; return true; }
    if (agent.draggingGuard >= 0) {
      if (!WorldGeometry::HideoutAllowsBody(object)) { world_.message = "La mesa permite ocultar un agente, pero no almacenar cuerpos."; return true; }
      const auto body = std::ranges::find_if(world_.guards, [&](const Guard &guard) { return static_cast<int>(guard.id) == agent.draggingGuard; });
      if (body != world_.guards.end() && body->trainingDummy) { world_.message = "Suelta la diana de prácticas antes de usar el escondite."; return true; }
      if (body == world_.guards.end() || !IsBody(*body) || body->bodyHidden || body->storedIn != 0 ||
          WorldGeometry::DistanceToBounds(body->position, WorldGeometry::ObjectFootprint(object)) > GameplayConfig::InteractionDistance ||
          !WorldGeometry::AtHideoutEntrance(object, body->position, GameplayConfig::AgentRadius) || !HideoutPathClear(object, body->position, object.position)) { world_.message = "Acerca el cuerpo al mismo acceso del escondite."; return true; }
      body->position = object.position; body->bodyHidden = true; body->storedIn = object.id; body->heldBy = 0; body->cqcState = CqcState::None;
      body->navigation.clear(); object.occupantGuard = body->id; object.occupied = true; agent.draggingGuard = -1;
      world_.message = "Cuerpo almacenado. El escondite tiene capacidad para un único ocupante."; return true;
    }
    ObserveHideoutEntry(agent, object);
    EmitSound(SoundKind::Hideout, agent.position, AudioConfig::QuietRadius, agent.id);
    agent.hideoutExit = agent.position; agent.position = object.position; agent.hideoutId = object.id; agent.hidden = true;
    agent.cover = {}; agent.moving = false; agent.running = false; agent.draggingGuard = -1; agent.actionPose = ActionPose::HideEnter; agent.actionPoseTime = GameplayConfig::StanceTransitionDuration;
    CancelManeuver(agent); agent.aiming = false;
    if (WorldGeometry::HideoutType(object) == HideoutKind::UnderTable) {
      agent.stance = Stance::Prone; agent.transitionFrom = agent.stance; agent.transitionTime = 0.f;
      agent.actionPose = ActionPose::None; agent.actionPoseTime = 0.f;
    }
    object.occupantAgent = agent.id; object.occupied = true;
    if (WorldGeometry::HideoutType(object) == HideoutKind::Closet) object.open = false;
    world_.message = WorldGeometry::HideoutType(object) == HideoutKind::Closet ? "Oculto en el armario. T abre o cierra la abertura; E permite salir por la puerta." : WorldGeometry::HideoutType(object) == HideoutKind::UnderTable ? "Oculto bajo la mesa: visión de 360 grados. WASD orienta la salida; E permite salir." : "Oculto. Pulsa E para salir por un acceso libre.";
    return true;
  }

  Guard *Simulation::NearbyBody(const Agent &agent) {
    Guard *nearest = nullptr; float distance = GameplayConfig::InteractionDistance;
    for (auto &guard : world_.guards) if ((IsBody(guard) || (IsTraining(world_) && guard.trainingDummy && guard.heldBy == 0)) && !guard.bodyHidden && guard.storedIn == 0) {
      const float candidate = Distance(agent.position, guard.position);
      if (candidate >= distance || std::ranges::any_of(world_.agents, [&](const Agent &other) { return other.id != agent.id && other.draggingGuard == static_cast<int>(guard.id); })) continue;
      if (!WorldGeometry::ClearMovementSegment(world_, agent.position, guard.position, GameplayConfig::AgentRadius)) continue;
      nearest = &guard; distance = candidate;
    }
    return nearest;
  }

  void Simulation::UpdateBodyDrag(Agent &agent, bool held) {
    if (!held || agent.hideoutId != 0 || agent.hidden) { agent.draggingGuard = -1; return; }
    if (agent.draggingGuard >= 0) {
      const auto body = std::ranges::find_if(world_.guards, [&](const Guard &guard) { return static_cast<int>(guard.id) == agent.draggingGuard; });
      if (body != world_.guards.end() && (IsBody(*body) || (IsTraining(world_) && body->trainingDummy && body->heldBy == 0)) && !body->bodyHidden && body->storedIn == 0 && Distance(agent.position, body->position) <= GameplayConfig::InteractionDistance &&
          WorldGeometry::ClearMovementSegment(world_, agent.position, body->position, GameplayConfig::AgentRadius)) return;
      agent.draggingGuard = -1;
    }
    if (const auto body = NearbyBody(agent)) { agent.draggingGuard = static_cast<int>(body->id); agent.cover = {}; }
  }

  void Simulation::MoveDraggedBody(Agent &agent, sf::Vector2f previous, float dt) {
    if (agent.draggingGuard < 0) return;
    const auto body = std::ranges::find_if(world_.guards, [&](const Guard &guard) { return static_cast<int>(guard.id) == agent.draggingGuard; });
    if (body == world_.guards.end()) { agent.draggingGuard = -1; return; }
    const auto target = agent.position - Unit(agent.position - body->position) * GameplayConfig::BodyDragOffset;
    const auto offset = target - body->position;
    auto candidate = body->position;
    Move(candidate, Unit(offset) * std::min(Length(offset), GameplayConfig::DragSpeed * dt), GameplayConfig::AgentRadius);
    if (!WorldGeometry::ClearMovementSegment(world_, body->position, candidate, GameplayConfig::AgentRadius) || !WorldGeometry::ClearMovementSegment(world_, agent.position, candidate, GameplayConfig::AgentRadius) || Distance(agent.position, candidate) > GameplayConfig::InteractionDistance) { agent.position = previous; return; }
    body->position = candidate;
  }

  void Simulation::UpdateAgent(Agent &agent, const ActionCommand &command, float dt) {
    if (UpdatePuzzle(agent, command)) { CancelReload(agent); agent.aiming = false; agent.actionPose = ActionPose::Interact; agent.actionPoseTime = GameplayConfig::StanceTransitionDuration; return; }
    using namespace GameplayConfig;
    agent.decoyGuide = {};
    agent.actionCooldown = std::max(0.f, agent.actionCooldown - dt);
    agent.actionPoseTime = std::max(0.f, agent.actionPoseTime - dt);
    if (agent.actionPoseTime == 0.f) agent.actionPose = ActionPose::None;
    if (agent.cqcHeldBy != 0) {
      const auto owner = std::ranges::find_if(world_.agents, [&](const Agent &entry) { return entry.id == agent.cqcHeldBy; });
      if (owner == world_.agents.end() || owner->cqcHoldingAgent != agent.id || !owner->connected || !owner->alive || owner->incapacitated || owner->hidden || owner->extracted) ReleaseCqc(agent);
      else {
        CancelReload(agent);
        agent.aiming = false;
        agent.facing = owner->facing; agent.moving = false; agent.running = false; agent.cover = {}; agent.decoyGuide = {};
        agent.actionPose = ActionPose::Grab; agent.actionPoseTime = GameplayConfig::AttackAnimationDuration;
        agent.cqcInputDown = command.cqcHeld;
        return;
      }
    }
    if (agent.hideoutId != 0) {
      ReleaseCqc(agent);
      agent.aiming = false;
      const sf::Vector2f direction{command.moveX,command.moveY};
      if (std::isfinite(direction.x) && std::isfinite(direction.y) && direction.lengthSquared()>0.01f) agent.facing=Unit(direction);
      agent.moving = false; agent.running = false; agent.draggingGuard = -1; agent.cover = {};
      agent.stamina = std::min(MaxStamina, agent.stamina + StaminaRecoveryPerSecond * dt);
      const auto object = std::ranges::find_if(world_.objects, [&](const Interactable &entry) { return entry.id == agent.hideoutId; });
      if (object == world_.objects.end() || object->occupantAgent != agent.id) ReleaseHideout(agent);
      else {
        agent.position = object->position; agent.hidden = true;
        if (WorldGeometry::HideoutType(*object) == HideoutKind::UnderTable) {
          agent.stance = Stance::Prone;
          const sf::Vector2f aim{command.aimX, command.aimY};
          if (std::isfinite(aim.x) && std::isfinite(aim.y) && aim.lengthSquared() > 0.01f) agent.weaponAim = Unit(aim);
          agent.aimSlope = std::isfinite(command.aimSlope) ? std::clamp(command.aimSlope, -CombatConfig::MaximumAimSlope, CombatConfig::MaximumAimSlope) : 0.f;
        }
        if (command.tool) ToggleHideoutOpening(agent); else if (command.interact) UseHideout(agent, *object);
      }
      return;
    }
    agent.staggerRemaining = std::max(0.f, agent.staggerRemaining - dt);
    if (agent.staggerRemaining > 0.f) { ReleaseCqc(agent); agent.aiming = false; agent.moving = false; agent.running = false; return; }
    if (UpdateManeuver(agent, command, dt)) return;
    const bool wasAiming = agent.aiming;
    agent.aiming = command.aiming && !agent.hidden && !agent.incapacitated && agent.puzzle.target == 0 && agent.cqcHoldingGuard == 0 && agent.cqcHoldingAgent == 0;
    if (wasAiming != agent.aiming) EmitSound(SoundKind::Camera, agent.position, AudioConfig::QuietRadius, agent.id);
    UpdateWeapon(agent, command, dt);
    sf::Vector2f direction{std::isfinite(command.moveX) ? std::clamp(command.moveX, -1.f, 1.f) : 0.f, std::isfinite(command.moveY) ? std::clamp(command.moveY, -1.f, 1.f) : 0.f};
    if (Length(direction) > 1.f) direction = Unit(direction);
    if (agent.cqcHoldingGuard == 0 && agent.cqcHoldingAgent == 0 && agent.cqcApproachGuard == 0 && agent.cqcApproachAgent == 0 && agent.cqcAttackTime <= 0.f) {
      if (agent.cover.active) agent.facing = agent.cover.normal;
      else if (direction.lengthSquared() > 0.0001f) agent.facing = Unit(direction);
    }
    if (UpdateCqc(agent, command, dt)) return;
    UpdateBodyDrag(agent, command.dragBody);
    const auto previousStance = agent.stance;
    if (command.crouch) { agent.stance = agent.stance == Stance::Crouched ? Stance::Standing : Stance::Crouched; agent.hidden = false; }
    if (command.prone) { agent.stance = agent.stance == Stance::Prone ? Stance::Standing : Stance::Prone; agent.hidden = false; }
    if (previousStance != agent.stance) { agent.transitionFrom = previousStance; agent.transitionTime = StanceTransitionDuration; }
    Move(agent.position, {}, AgentRadius);
    if (command.coverToggle) { ToggleCover(agent); if (agent.cover.active) { agent.actionPose = ActionPose::CoverEnter; agent.actionPoseTime = StanceTransitionDuration; } }
    agent.running = !agent.cover.active && command.run && Length(direction) > 0.f && agent.stance != Stance::Prone && agent.draggingGuard < 0 && agent.stamina >= MinimumRunningStamina;
    float speed = agent.stance == Stance::Prone ? CrawlSpeed : (agent.stance == Stance::Crouched ? (agent.running ? CrouchRunningSpeed : CrouchSpeed) : (agent.running ? RunSpeed : WalkSpeed));
    if (agent.draggingGuard >= 0) speed = DragSpeed;
    const auto previous = agent.position;
    if (!UpdateCover(agent, command, direction, dt)) Move(agent.position, direction * speed * dt, AgentRadius);
    MoveDraggedBody(agent, previous, dt);
    agent.moving = Distance(previous, agent.position) > 0.01f;
    if (agent.running && agent.moving) agent.stamina = std::max(0.f, agent.stamina - RunningStaminaPerSecond * dt);
    else agent.stamina = std::min(MaxStamina, agent.stamina + StaminaRecoveryPerSecond * dt);
    if (agent.moving) {
      agent.hidden = false; agent.animationTime += dt * (agent.running ? 0.6f : 0.f);
      agent.footstepTime += agent.stance == Stance::Prone ? 0.f : dt;
      const float interval = agent.stance == Stance::Crouched ? CrouchedFootstepInterval : agent.running ? RunningFootstepInterval : FootstepInterval;
      if (IsTactical(world_) && agent.stance != Stance::Prone && agent.footstepTime >= interval) {
        const float radius = agent.stance == Stance::Crouched ? (agent.running ? CrouchedRunningNoiseRadius : CrouchedNoiseRadius) : agent.running ? RunningNoiseRadius : WalkingNoiseRadius;
        const bool soft = agent.position.x >= AudioConfig::SoftFloorBoundary;
        const auto kind = agent.stance == Stance::Crouched ? (soft ? SoundKind::StepCrouchSoft : SoundKind::StepCrouchHard) : agent.running ? (soft ? SoundKind::StepRunSoft : SoundKind::StepRunHard) : (soft ? SoundKind::StepSoft : SoundKind::StepHard);
        EmitNoise(agent.position, radius, agent.id, 0, kind);
        EmitSound(agent.draggingGuard >= 0 ? SoundKind::Drag : kind, agent.position, radius, agent.id);
        agent.footstepTime = std::fmod(agent.footstepTime, interval);
      }
    } else agent.footstepTime = 0.f;
    if (agent.stance == Stance::Prone) agent.footstepTime = 0.f;
    const sf::Vector2f aim{command.aimX, command.aimY};
    agent.aimSlope = std::isfinite(command.aimSlope) ? std::clamp(command.aimSlope, -CombatConfig::MaximumAimSlope, CombatConfig::MaximumAimSlope) : 0.f;
    if (std::isfinite(aim.x) && std::isfinite(aim.y) && Length(aim) > 0.1f && (agent.aiming || command.fireSelected || command.fireLethal || command.fireNonLethal)) agent.weaponAim = Unit(aim);
    if (agent.cover.active) agent.facing = agent.cover.normal;
    if (command.prepareDecoy && IsTactical(world_) && agent.loadout.decoys > 0 && agent.draggingGuard < 0 && agent.actionCooldown <= 0.f &&
        !command.dragBody && !command.interact && !command.tool && !command.cqc && !command.cqcHeld && !command.push && !command.knock && !command.fireLethal && !command.fireNonLethal && !command.fireSelected &&
        std::ranges::none_of(world_.guards, [&](const Guard &guard) { return guard.heldBy == agent.id && guard.cqcState != CqcState::None; })) {
      const float distance = std::isfinite(command.decoyDistance) ? std::clamp(command.decoyDistance, 0.f, DistractionConfig::MaximumDecoyDistance) : 0.f;
      const auto throwDirection = aim.lengthSquared() > 0.01f ? Unit(aim) : agent.facing;
      const auto trajectory = DistractionGeometry::TraceDecoy(world_, agent.position, throwDirection, distance);
      const auto maximum = DistractionGeometry::TraceDecoy(world_, agent.position, throwDirection);
      agent.decoyGuide = {true, throwDirection, distance, trajectory.to, maximum.to, trajectory.travelled, maximum.travelled, trajectory.valid, trajectory.blocked, maximum.blocked};
    }
    if (command.interact || command.tool) Interact(agent, command.tool);
    if (agent.puzzle.target != 0) { agent.actionPose = ActionPose::Interact; agent.actionPoseTime = StanceTransitionDuration; return; }
    if (agent.hideoutId != 0 || agent.draggingGuard >= 0) return;
    if (!IsTactical(world_)) return;
    if (agent.actionCooldown > 0.f) return;
    if (command.push) Push(agent);
    else if (command.fireLethal || command.fireNonLethal || command.fireSelected) {
      const bool lethal = command.fireLethal || (command.fireSelected && !command.fireNonLethal && agent.selectedWeapon == WeaponKind::Pistol);
      if (Fire(agent, lethal)) agent.actionCooldown = WeaponCooldown;
    }
    else HandleDistractions(agent, command);
  }

  void Simulation::Interact(Agent &agent, bool tool) {
    using namespace GameplayConfig;
    if (world_.phase == GamePhase::Mission && !tool && agent.connected && agent.alive && !agent.incapacitated && !agent.hidden && !agent.extracted) for (auto &other : world_.agents) {
      if (other.id == agent.id || !other.connected || !other.alive || !other.incapacitated || other.lifeState != LifeState::Incapacitated || other.incapacityRemaining <= 0.f ||
          other.privateOrder == PrivateOrder::Traitor || agent.privateOrder == PrivateOrder::Traitor || Distance(agent.position, other.position) >= InteractionDistance || Occluded(agent.position, other.position, Stance::Prone)) continue;
      // Un rescate válido tiene prioridad sobre el arrastre de un cuerpo que comparte el alcance de E.
      agent.draggingGuard = -1;
      Revive(other, agent.id);
      return;
    }
    if (agent.draggingGuard >= 0) {
      if (tool) { world_.message = "Suelta E antes de utilizar una herramienta."; return; }
      Interactable *hideout = nullptr; float nearest = InteractionDistance;
      for (auto &object : world_.objects) {
        const float distance = WorldGeometry::DistanceToBounds(agent.position, WorldGeometry::ObjectFootprint(object));
        if (WorldGeometry::HideoutType(object) != HideoutKind::None && WorldGeometry::Properties(object).interactive && distance < nearest &&
            Visibility::HasLineOfSight(world_, agent.position, WorldGeometry::ClosestPoint(agent.position, WorldGeometry::ObjectFootprint(object)), Stance::Standing, object.id)) { hideout = &object; nearest = distance; }
      }
      if (hideout) UseHideout(agent, *hideout);
      else world_.message = "Mantén E para arrastrar; pulsa E junto a un contenedor vacío para almacenar el cuerpo.";
      return;
    }
    if (!tool && NearbyBody(agent)) { world_.message = "Mantén E para arrastrar el cuerpo neutralizado."; return; }
    if (IsTraining(world_) && !tool) for (auto &dummy : world_.guards) if (dummy.trainingDummy && Distance(agent.position, dummy.position) < InteractionDistance) {
      DamageTrainingDummy(dummy, 0.f); world_.message = "Diana indestructible: prueba armas, golpes y agarre por detrás."; return;
    }
    Interactable *nearest = nullptr;
    float distance = InteractionDistance;
    for (auto &object : world_.objects) {
      if (!WorldGeometry::Properties(object).interactive) continue;
      const float candidate = WorldGeometry::DistanceToBounds(agent.position, WorldGeometry::ObjectFootprint(object));
      if (candidate < distance && Visibility::HasLineOfSight(world_, agent.position, WorldGeometry::ClosestPoint(agent.position, WorldGeometry::ObjectFootprint(object)), Stance::Standing, object.id)) { nearest = &object; distance = candidate; }
    }
    if (!nearest) { world_.message = "Acércate a una estación u objeto para interactuar."; return; }
    auto &object = *nearest;
    if (WorldGeometry::HideoutType(object) != HideoutKind::None) {
      if (!tool) UseHideout(agent, object);
      else world_.message = "Pulsa E para utilizar el escondite.";
      return;
    }
    if (object.puzzle.kind != PuzzleKind::None && !(object.kind == ObjectKind::Door && !object.locked)) {
      BeginPuzzle(agent, object);
      return;
    }
    switch (object.kind) {
    case ObjectKind::TrainingReset: ResetTrainingDevice(agent); break;
    case ObjectKind::Operations:
      if (agent.id == HostPlayerId) SetContract(static_cast<ContractId>((static_cast<unsigned int>(world_.contract) + 1) % Contracts().size()));
      else world_.message = "El anfitrión selecciona el contrato en operaciones.";
      break;
    case ObjectKind::Armory:
      SetLoadout(agent.id, agent.operationLoadout.lethalAmmo > 0 ? Loadout{0, 32, 5, 4} : Loadout{});
      world_.message = agent.loadout.lethalAmmo > 0 ? "Equipo estándar: letal, no letal, señuelos y herramientas." : "Equipo discreto: sin arma letal, más señuelos y herramientas.";
      break;
    case ObjectKind::Intelligence: world_.message = GetContract(world_.contract).briefing; break;
    case ObjectKind::Deployment: world_.message = "DESPLIEGUE LISTO: confirma desde el panel de operación."; break;
    case ObjectKind::Door:
      object.open = !object.open;
      EmitNoise(object.position, WalkingNoiseRadius, agent.id);
      world_.message = object.open ? "Puerta abierta; también hay un acceso por el corredor exterior." : "Puerta cerrada.";
      break;
    case ObjectKind::Terminal:
      if (!object.completed) { object.completed = true; ++world_.objectives; Record(EventKind::Objective, agent.id, object.id); world_.message = "Archivo principal recuperado. Vuelve a extracción cuando el contrato esté cumplido."; }
      break;
    case ObjectKind::Secondary:
      if (!object.completed) { object.completed = true; ++world_.secondary; Record(EventKind::Secondary, agent.id, object.id); }
      if (agent.privateOrder == PrivateOrder::CopyArchive && !agent.privateComplete) { agent.privateComplete = true; Record(EventKind::PrivateObjective, agent.id, object.id); }
      world_.message = "Archivo secundario recuperado.";
      break;
    case ObjectKind::Camera:
      if (object.active && tool && agent.loadout.tools > 0) { --agent.loadout.tools; object.active = false; object.camera.state = CameraState::Broken; object.camera.reactivationRemaining = 0.f; object.progress = 0.f; object.observedAgent = 0; Record(EventKind::CameraDisabled, agent.id, object.id); EmitSound(SoundKind::Camera, object.position, CameraDistance, agent.id); world_.message = "Cámara averiada: no puede reactivarse."; }
      else world_.message = "Usa una herramienta cerca de la cámara.";
      break;
    case ObjectKind::AlarmPanel:
      if (object.active && tool && agent.loadout.tools > 0) {
        --agent.loadout.tools; object.active = false;
        for (auto &camera : world_.objects) if (camera.kind == ObjectKind::Camera && camera.camera.state != CameraState::Broken) { camera.active = false; camera.camera.state = CameraState::Disabled; camera.camera.reactivationRemaining = 0.f; camera.progress = 0.f; camera.observedAgent = 0; }
        Record(EventKind::Sabotage, agent.id, object.id); world_.message = "Central saboteada: cámaras fuera de servicio.";
      } else world_.message = "Usa una herramienta para sabotear la central.";
      break;
    case ObjectKind::Hideout:
      UseHideout(agent, object);
      break;
    case ObjectKind::CoopSwitch:
    case ObjectKind::PuzzleNode:
      BeginPuzzle(agent, object);
      break;
    case ObjectKind::Extraction:
      if (world_.objectives < GetContract(world_.contract).requiredObjectives) world_.message = "Faltan objetivos obligatorios. Extracción no autorizada.";
      else if (agent.privateOrder == PrivateOrder::Traitor) world_.message = "Tu orden privada requiere eliminar a los demás agentes sin alarma.";
      else { agent.extracted = true; Record(EventKind::Extracted, agent.id, object.id); world_.message = "Agente extraído; esperando a los supervivientes."; }
      break;
    case ObjectKind::Explosive:
    case ObjectKind::Computer:
    case ObjectKind::CameraPanel:
      BeginPuzzle(agent, object);
      break;
    }
  }

  sf::Vector2f Simulation::TraceWeapon(sf::Vector2f from, sf::Vector2f direction, float range, bool &blocked, float fromHeight, float slope) const {
    direction = Unit(direction);
    if (direction.x > 0.f) range = std::min(range, (world_.size.x - from.x) / direction.x);
    else if (direction.x < 0.f) range = std::min(range, -from.x / direction.x);
    if (direction.y > 0.f) range = std::min(range, (world_.size.y - from.y) / direction.y);
    else if (direction.y < 0.f) range = std::min(range, -from.y / direction.y);
    blocked = slope < 0.f && fromHeight / -slope < range;
    if (blocked) range = fromHeight / -slope;
    float closest = std::max(0.f, range);
    const auto clip = [&](const sf::FloatRect &bounds, float height) {
      const CombatGeometry::HitVolume volume{bounds.position + bounds.size * 0.5f, {1.f, 0.f}, bounds.size * 0.5f, 0.f, height, HitZone::None};
      const auto intersection = CombatGeometry::IntersectVolume(from, direction, fromHeight, slope, closest, volume);
      if (intersection && *intersection < closest) {
        closest = *intersection;
        blocked = true;
      }
    };
    for (const auto &obstacle : world_.obstacles) if (WorldGeometry::BlocksMovement(obstacle)) clip(obstacle.bounds, WorldGeometry::ObstacleHeight(obstacle));
    for (const auto &object : world_.objects) if (WorldGeometry::BlocksMovement(object)) clip(WorldGeometry::ObjectFootprint(object), WorldGeometry::ObjectHeight(object));
    return from + direction * closest;
  }

  void Simulation::ConfirmDeath(Agent &agent, PlayerId actor, bool rescueExpired) {
    if (agent.lifeState == LifeState::Dead) return;
    CancelReload(agent); CancelPuzzle(agent); CancelManeuver(agent); ReleaseCqc(agent); ReleaseHideout(agent);
    agent.lifeState = LifeState::Dead; agent.alive = false; agent.incapacitated = false; agent.rescueExpired = rescueExpired;
    agent.health = 0.f; agent.stamina = 0.f; agent.incapacityRemaining = 0.f; agent.friendlyIncapacitated = false;
    agent.healthRecoveryRemaining = GameplayConfig::PassiveHealthRecoveryDelay;
    agent.aiming = false; agent.moving = false; agent.running = false; agent.hidden = false; agent.draggingGuard = -1; agent.cover = {};
    agent.detectionProgress = 0.f; agent.detectionConfirmed = false; agent.detectionIndicatorRemaining = agent.confirmedIndicatorRemaining = 0.f;
    agent.actionPose = ActionPose::Fall; agent.actionPoseTime = GameplayConfig::AttackAnimationDuration;
    Record(EventKind::AgentKilled, actor, agent.id);
    world_.message = rescueExpired ? "Se agotaron los 55 segundos para rescatar al compañero." : "Un agente ha muerto por sus heridas.";
  }

  void Simulation::Incapacitate(Agent &agent, PlayerId actor, std::uint32_t source) {
    if (agent.lifeState == LifeState::Dead || agent.incapacitated) return;
    agent.healthRecoveryRemaining = GameplayConfig::PassiveHealthRecoveryDelay;
    CancelReload(agent); CancelManeuver(agent); agent.aiming = false;
    CancelPuzzle(agent); ReleaseCqc(agent);
    if (IsTraining(world_)) {
      agent.lifeState = LifeState::Alive; agent.rescueExpired = false; agent.alive = true; agent.incapacitated = false; agent.friendlyIncapacitated = false; agent.incapacityRemaining = 0.f;
      agent.health = std::max(WeaponConfig::TrainingMinimumHealth, agent.health);
      agent.stamina = std::max(WeaponConfig::TrainingMinimumHealth, agent.stamina);
      agent.staggerRemaining = WeaponConfig::TrainingReaction;
      EmitSound(SoundKind::Impact, agent.position, AudioConfig::ImpactRadius, actor);
      world_.message = "Impacto de entrenamiento: el agente sigue operativo.";
      return;
    }
    const bool rescuer = agent.privateOrder != PrivateOrder::Traitor && std::ranges::any_of(world_.agents, [&agent](const Agent &other) {
      return other.id != agent.id && other.connected && other.alive && !other.incapacitated && !other.extracted && other.privateOrder != PrivateOrder::Traitor;
    });
    if (agent.health <= 0.f && !rescuer) { ConfirmDeath(agent, actor, false); return; }
    EmitSound(SoundKind::Fall, agent.position, AudioConfig::FallRadius, agent.id);
    agent.detectionProgress = 0.f; agent.detectionConfirmed = false;
    agent.detectionIndicatorRemaining = agent.confirmedIndicatorRemaining = 0.f;
    ReleaseHideout(agent);
    agent.lifeState = LifeState::Incapacitated; agent.rescueExpired = false;
    agent.incapacitated = true; agent.incapacityRemaining = GameplayConfig::IncapacityDuration;
    agent.friendlyIncapacitated = actor != 0 && actor != agent.id;
    agent.cover = {};
    agent.stamina = 0.f; agent.moving = false; agent.running = false; agent.hidden = false; agent.draggingGuard = -1;
    agent.actionPose = ActionPose::Fall; agent.actionPoseTime = GameplayConfig::AttackAnimationDuration;
    Record(EventKind::AgentIncapacitated, actor, source);
    world_.message = "Agente incapacitado: un compañero debe recuperarlo con E antes de 55 segundos.";
  }

  void Simulation::Revive(Agent &agent, PlayerId actor) {
    const auto rescuer = std::ranges::find_if(world_.agents, [actor](const Agent &other) { return other.id == actor; });
    if (world_.phase != GamePhase::Mission || agent.lifeState != LifeState::Incapacitated || !agent.alive || !agent.incapacitated || agent.incapacityRemaining <= 0.f ||
        rescuer == world_.agents.end() || actor == agent.id || !rescuer->connected || !rescuer->alive || rescuer->incapacitated || rescuer->extracted || rescuer->hidden ||
        rescuer->privateOrder == PrivateOrder::Traitor || agent.privateOrder == PrivateOrder::Traitor || Distance(rescuer->position, agent.position) >= GameplayConfig::InteractionDistance || Occluded(rescuer->position, agent.position, Stance::Prone)) return;
    ReleaseCqc(agent); ReleaseHideout(agent);
    agent.lifeState = LifeState::Alive; agent.rescueExpired = false;
    agent.incapacitated = false; agent.incapacityRemaining = 0.f; agent.bodyReported = false; agent.friendlyIncapacitated = false;
    agent.cover = {};
    agent.health = std::max(agent.health, GameplayConfig::RevivedHealth);
    agent.healthRecoveryRemaining = GameplayConfig::PassiveHealthRecoveryDelay;
    agent.stamina = GameplayConfig::RevivedStamina;
    agent.actionCooldown = GameplayConfig::WeaponCooldown;
    agent.actionPose = ActionPose::Recover; agent.actionPoseTime = GameplayConfig::AttackAnimationDuration;
    Record(EventKind::AgentRevived, actor, agent.id);
    world_.message = "Compañero recuperado mediante rescate.";
  }

  void Simulation::DamageAgent(Agent &agent, float healthDamage, float staminaDamage, PlayerId actor, std::uint32_t source, bool enemyBullet) {
    if (!agent.alive || agent.lifeState == LifeState::Dead || agent.extracted || agent.incapacitated) return;
    // La protección tumbado se aplica también aquí aunque cambie la selección de blancos enemigos.
    if (enemyBullet && agent.stance == Stance::Prone) return;
    if (!std::isfinite(healthDamage) || !std::isfinite(staminaDamage) || healthDamage < 0.f || staminaDamage < 0.f) return;
    agent.healthRecoveryRemaining = GameplayConfig::PassiveHealthRecoveryDelay;
    CancelReload(agent); CancelPuzzle(agent); CancelManeuver(agent); agent.aiming = false; ReleaseCqc(agent);
    agent.actionPose = ActionPose::TakeDamage; agent.actionPoseTime = GameplayConfig::AttackAnimationDuration;
    agent.health = std::max(0.f, agent.health - healthDamage);
    agent.stamina = std::max(0.f, agent.stamina - staminaDamage);
    Record(EventKind::AgentDamaged, actor, agent.id);
    if (IsTraining(world_)) {
      agent.health = std::max(WeaponConfig::TrainingMinimumHealth, agent.health);
      agent.stamina = std::max(WeaponConfig::TrainingMinimumHealth, agent.stamina);
      agent.lifeState = LifeState::Alive; agent.rescueExpired = false; agent.alive = true; agent.incapacitated = false; agent.friendlyIncapacitated = false; agent.incapacityRemaining = 0.f;
      agent.staggerRemaining = WeaponConfig::TrainingReaction;
      world_.message = "Impacto de práctica; la seguridad del entrenamiento impide muerte e inconsciencia.";
      return;
    }
    // La transición vital se confirma antes de permitir cualquier actualización de resistencia.
    if (agent.health <= 0.f) { Incapacitate(agent, actor, source); return; }
    if (agent.stamina <= 0.f) Incapacitate(agent, actor, source);
    else world_.message = "Agente herido: rompe la línea de visión y busca cobertura.";
  }

  void Simulation::HitGuard(Guard &guard, HitZone zone, bool lethal, PlayerId actor) {
    using namespace GameplayConfig;
    if (IsTraining(world_)) {
      if (guard.heldBy != 0) for (auto &owner : world_.agents) if (owner.id == guard.heldBy) { ReleaseCqc(owner); break; }
      DamageTrainingDummy(guard, lethal ? CombatGeometry::LethalDamage(zone) : CombatGeometry::StunDamage(zone));
      return;
    }
    if (IsBody(guard)) return;
    if (guard.heldBy != 0) for (auto &owner : world_.agents) if (owner.id == guard.heldBy) { ReleaseCqc(owner); break; }
    if (lethal) guard.health = std::max(0.f, guard.health - CombatGeometry::LethalDamage(zone));
    else guard.stun = std::min(CombatConfig::MaxGuardStun, guard.stun + CombatGeometry::StunDamage(zone));
    if (guard.health <= 0.f || guard.stun >= CombatConfig::MaxGuardStun) {
      guard.state = guard.health <= 0.f ? GuardState::Dead : GuardState::Incapacitated;
      guard.attackTime = AttackAnimationDuration;
      guard.incapacityRemaining = guard.health <= 0.f ? 0.f : GuardIncapacityDuration;
      guard.heldBy = 0; guard.cqcState = CqcState::None;
      Record(guard.health <= 0.f ? EventKind::GuardKilled : EventKind::GuardIncapacitated, actor, guard.id);
      world_.message = lethal ? "Guardia eliminado; el cuerpo y el ruido comprometen el sigilo." : "Guardia inconsciente. Oculta el cuerpo.";
    } else {
      guard.state = GuardState::Search; guard.stateTime = 0.f; guard.lastKnown = guard.position;
      world_.message = lethal ? "Guardia herido; un impacto en torso o extremidades no siempre es mortal." : "Guardia aturdido; los impactos no letales acumulan inconsciencia.";
    }
  }

  bool Simulation::Fire(Agent &agent, bool lethal) {
    using namespace GameplayConfig;
    if (agent.reloadRemaining > 0.f) { world_.message = "Termina la recarga antes de disparar."; return false; }
    int &ammo = lethal ? agent.loadout.lethalAmmo : agent.loadout.nonLethalAmmo;
    int &magazine = lethal ? agent.lethalMagazine : agent.nonLethalMagazine;
    if (ammo <= 0) { world_.message = "Sin munición del tipo seleccionado."; return false; }
    if (magazine <= 0) { world_.message = "Cargador vacío: pulsa R para recargar."; return false; }
    --ammo; --magazine;
    Record(lethal ? EventKind::WeaponLethal : EventKind::WeaponNonLethal, agent.id);
    ReportWeaponShot(agent, lethal);
    agent.actionPose = ActionPose::Shoot; agent.actionPoseTime = AttackAnimationDuration;
    const auto direction = Unit(WeaponFacing(agent));
    const float fromHeight = CombatGeometry::WeaponHeight(agent.stance);
    const float slope = std::isfinite(agent.aimSlope) ? std::clamp(agent.aimSlope, -CombatConfig::MaximumAimSlope, CombatConfig::MaximumAimSlope) : 0.f;
    bool blocked = false;
    auto impact = TraceWeapon(agent.position, direction, WeaponRange, blocked, fromHeight, slope);
    float nearest = Distance(agent.position, impact);
    HitZone zone = HitZone::None;
    Guard *guardTarget = nullptr;
    Agent *agentTarget = nullptr;
    for (auto &guard : world_.guards) if (!IsBody(guard) && (!IsTraining(world_) || guard.trainingDummy)) {
      const bool down = !guard.trainingDummy && guard.heldBy == 0 && (guard.cqcState == CqcState::KnockedDown || guard.cqcState == CqcState::Restrained);
      const auto intersection = CombatGeometry::IntersectActor(agent.position, direction, fromHeight, slope, nearest, guard.position, guard.facing, Stance::Standing, down);
      if (intersection && intersection->distance < nearest) { nearest = intersection->distance; zone = intersection->zone; guardTarget = &guard; }
    }
    if (IsTraining(world_) || GetContract(world_.contract).traitor) for (auto &other : world_.agents) if (other.id != agent.id && other.alive && other.connected && !other.extracted) {
      const auto intersection = CombatGeometry::IntersectActor(agent.position, direction, fromHeight, slope, nearest, other.position, other.facing, other.stance, other.incapacitated);
      if (intersection && intersection->distance < nearest) { nearest = intersection->distance; zone = intersection->zone; agentTarget = &other; guardTarget = nullptr; }
    }
    const bool hit = guardTarget || agentTarget;
    if (hit) impact = agent.position + direction * nearest;
    world_.shots.push_back({agent.position, impact, ShotTraceDuration, lethal, agent.id, 0, hit, blocked && !hit, zone, fromHeight, std::max(0.f, fromHeight + slope * nearest), agentTarget ? agentTarget->id : guardTarget ? guardTarget->id : 0, agentTarget ? ShotTarget::Agent : guardTarget ? ShotTarget::Guard : ShotTarget::None});
    if (hit || blocked) EmitSound(SoundKind::Impact, impact, AudioConfig::ImpactRadius, agent.id);
    if (agentTarget) {
      if (lethal) DamageAgent(*agentTarget, zone == HitZone::Head ? CombatConfig::AgentLethalHeadDamage : CombatGeometry::LethalDamage(zone), 0.f, agent.id, agentTarget->id);
      else if (zone == HitZone::Head) Incapacitate(*agentTarget, agent.id, agentTarget->id);
      else DamageAgent(*agentTarget, 0.f, CombatGeometry::StunDamage(zone), agent.id, agentTarget->id);
    }
    else if (guardTarget) {
      HitGuard(*guardTarget, zone, lethal, agent.id);
    } else world_.message = blocked ? "El disparo impactó contra una cobertura sólida." : "Disparo sin impacto. El uso de armas queda registrado.";
    return true;
  }

  void Simulation::UpdateGuards(float dt) {
    using namespace GameplayConfig;
    for (auto &cue : world_.footsteps) cue.remaining -= dt;
    std::erase_if(world_.footsteps, [](const FootstepCue &cue) { return cue.remaining <= 0.f; });
    for (auto &guard : world_.guards) {
      const auto previousPosition = guard.position;
      const auto recordFootstep = [this, &guard, previousPosition, dt] {
        const auto displacement = guard.position - previousPosition;
        if (Length(displacement) > DistractionConfig::MinimumFootstepMovement) {
          guard.stepTime += dt;
          if (guard.stepTime >= DistractionConfig::FootstepInterval) {
            guard.stepTime = std::fmod(guard.stepTime, DistractionConfig::FootstepInterval);
            if (world_.footsteps.size() >= DistractionConfig::MaximumFootstepCues) world_.footsteps.erase(world_.footsteps.begin());
            auto approximate = DistractionGeometry::QuantizeFootstep(guard.position);
            approximate.x = std::clamp(approximate.x, 0.f, world_.size.x - SpatialConfig::CollisionSkin);
            approximate.y = std::clamp(approximate.y, 0.f, world_.size.y - SpatialConfig::CollisionSkin);
            world_.footsteps.push_back({approximate, DistractionGeometry::FootstepDirection(displacement), DistractionConfig::FootstepLifetime, guard.id});
            EmitSound(guard.position.x >= AudioConfig::SoftFloorBoundary ? SoundKind::StepSoft : SoundKind::StepHard, guard.position, DistractionConfig::PlayerHearingDistance);
          }
        } else guard.stepTime = 0.f;
      };
      guard.attackTime = std::max(0.f, guard.attackTime - dt);
      guard.detectedTime = std::max(0.f, guard.detectedTime - dt);
      if (guard.state == GuardState::Incapacitated && !guard.bodyHidden && guard.incapacityRemaining > 0.f) {
        guard.incapacityRemaining = std::max(0.f, guard.incapacityRemaining - dt);
        if (guard.incapacityRemaining <= 0.f) WakeGuard(guard);
      }
      if (IsBody(guard)) { UpdateHideoutInspection(guard, dt); guard.stepTime = 0.f; guard.bodyTargetKind = BodyTargetKind::None; guard.bodyTargetId = 0; guard.bodyInspectionTime = 0.f; continue; }
      guard.stun = std::max(0.f, guard.stun - CombatConfig::GuardStunRecoveryPerSecond * dt);
      guard.stateTime += dt;
      guard.suspicionRemaining = std::max(0.f, guard.suspicionRemaining - dt);
      if (guard.suspicionRemaining == 0.f && guard.detectedTime == 0.f) guard.indicatorProgress = 0.f;
      guard.attackCooldown = std::max(0.f, guard.attackCooldown - dt);
      guard.navigationRejectTime = std::max(0.f, guard.navigationRejectTime - dt);
      if (guard.cqcState != CqcState::None || guard.heldBy != 0) { UpdateHideoutInspection(guard, dt); guard.stepTime = 0.f; guard.bodyTargetKind = BodyTargetKind::None; guard.bodyTargetId = 0; guard.bodyInspectionTime = 0.f; continue; }
      guard.staggerRemaining = std::max(0.f, guard.staggerRemaining - dt);
      if (guard.staggerRemaining > 0.f) {
        guard.stepTime = 0.f; guard.attackTime = 0.f; guard.hideoutInspectionTime = 0.f;
        guard.bodyTargetKind = BodyTargetKind::None; guard.bodyTargetId = 0; guard.bodyInspectionTime = 0.f;
        for (auto &object : world_.objects) if (object.id == guard.knownHideout) object.inspectionProgress = 0.f;
        continue;
      }
      if (guard.bodyTargetKind != BodyTargetKind::None && guard.state != GuardState::ApproachBody && guard.state != GuardState::InspectBody) {
        guard.bodyTargetKind = BodyTargetKind::None; guard.bodyTargetId = 0; guard.bodyInspectionTime = 0.f;
      }
      Agent *seen = nullptr;
      for (auto &agent : world_.agents) if (CanSee(guard.position, guard.facing, agent, world_.alarm ? VisionDistance * AlarmVisionMultiplier : VisionDistance) && (!seen || Distance(guard.position, agent.position) < Distance(guard.position, seen->position))) seen = &agent;
      if (!seen && UpdateHideoutInspection(guard, dt)) {
        if (guard.state == GuardState::Alert) for (const auto &agent : world_.agents) if (agent.id == guard.observedAgent && CanSee(guard.position, guard.facing, agent)) RegisterSecurityContact(agent.position);
        recordFootstep(); continue;
      }
      if (seen) {
        guard.hideoutInspectionTime = 0.f;
        guard.bodyTargetKind = BodyTargetKind::None; guard.bodyTargetId = 0; guard.bodyInspectionTime = 0.f;
        for (auto &object : world_.objects) if (object.id == guard.knownHideout) object.inspectionProgress = 0.f;
      }
      if (seen) {
        if (guard.observedAgent != 0 && guard.observedAgent != seen->id) guard.suspicion = 0.f;
        guard.perceptionLostTime = 0.f;
        guard.observedAgent = seen->id;
        guard.lastKnown = seen->position;
        guard.suspicion = std::min(1.f, guard.suspicion + dt * Perception::DetectionRate(*seen, Distance(guard.position, seen->position), VisionDistance) / ConfirmationTime);
        guard.indicatorProgress = guard.suspicion; guard.suspicionRemaining = SuspicionIndicatorDuration;
        ReportPerception(*seen, guard.suspicion, guard.suspicion >= 1.f);
        if (guard.suspicion >= 1.f) {
          if (guard.state != GuardState::Alert) { Detect(*seen, guard.id); guard.detectedTime = ConfirmedIndicatorDuration; }
          else RegisterSecurityContact(seen->position);
          seen->detectionConfirmed = true; guard.detectedTime = ConfirmedIndicatorDuration;
          guard.state = GuardState::Alert; guard.stateTime = 0.f;
          const auto aim = Unit(seen->position - guard.position);
          const float distance = Distance(guard.position, seen->position);
          guard.facing = aim;
          const bool melee = distance <= GuardAttackDistance;
          if (distance <= GuardWeaponRange && guard.attackCooldown <= 0.f && (melee || (seen->stance != Stance::Prone && guard.ammo > 0))) {
            guard.attackCooldown = GuardAttackInterval; guard.attackTime = AttackAnimationDuration;
            guard.meleeAttack = melee;
            const float fromHeight = CombatGeometry::WeaponHeight(Stance::Standing);
            const float slope = (CombatGeometry::TorsoCenterHeight(seen->stance) - fromHeight) / std::max(1.f, distance);
            bool blocked = false;
            auto impact = TraceWeapon(guard.position, aim, distance, blocked, fromHeight, melee ? 0.f : slope);
            if (guard.meleeAttack) {
              if (!blocked) {
                Record(EventKind::GuardMelee, 0, guard.id);
                DamageAgent(*seen, GuardMeleeDamage, GuardMeleeStaminaDamage, 0, seen->id);
              }
            } else {
              --guard.ammo;
              Agent *hit = nullptr;
              float closest = Distance(guard.position, impact);
              HitZone zone = HitZone::None;
              for (auto &agent : world_.agents) if (agent.connected && agent.alive && !agent.incapacitated && !agent.extracted && agent.stance != Stance::Prone) {
                const auto candidate = CombatGeometry::IntersectActor(guard.position, aim, fromHeight, slope, closest, agent.position, agent.facing, agent.stance);
                if (candidate && candidate->distance < closest) { closest = candidate->distance; zone = candidate->zone; hit = &agent; }
              }
              if (hit) impact = guard.position + aim * closest;
              world_.shots.push_back({guard.position, impact, ShotTraceDuration, true, 0, guard.id, hit != nullptr, blocked && !hit, zone, fromHeight, std::max(0.f, fromHeight + slope * closest), hit ? hit->id : 0, hit ? ShotTarget::Agent : ShotTarget::None});
              Record(EventKind::GuardShot, 0, guard.id);
              EmitNoise(guard.position, LethalNoiseRadius, 0, guard.id, SoundKind::Rifle);
              if (hit) {
                const float damage = zone == HitZone::Head ? CombatConfig::LethalHeadDamage : zone == HitZone::Limbs ? CombatConfig::GuardLimbDamage : GuardShotDamage;
                DamageAgent(*hit, damage, GuardShotStaminaDamage, 0, hit->id, true);
              }
            }
          }
        } else { guard.state = GuardState::Suspicion; guard.stateTime = 0.f; }
      } else {
        guard.perceptionLostTime = std::min(SecurityConfig::GuardLostSightGrace + 1.f, guard.perceptionLostTime + dt);
        if (guard.perceptionLostTime > SecurityConfig::GuardLostSightGrace) guard.suspicion = std::max(0.f, guard.suspicion - dt * SuspicionDecay);
        if (guard.state == GuardState::Alert || (guard.state == GuardState::Suspicion && guard.suspicion == 0.f)) { guard.state = GuardState::Search; guard.stateTime = 0.f; }
        if (guard.bodyTargetKind == BodyTargetKind::None && guard.state != GuardState::Retiring) for (const auto &noise : world_.noises) if (noise.guardId != guard.id && Distance(guard.position, noise.position) <= noise.radius) {
          if (noise.kind == SoundKind::Alarm && guard.state == GuardState::Search) continue;
          guard.lastKnown = noise.position; guard.state = GuardState::Investigate; guard.stateTime = 0.f;
          break;
        }
        const auto seesBody = [this, &guard](sf::Vector2f position) {
          const auto offset = position - guard.position;
          const float distance = Length(offset);
          return distance < BodyVisionDistance && (distance < CloseVisionDistance || guard.facing.dot(Unit(offset)) >= VisionCosHalfAngle) && !Occluded(guard.position, position, Stance::Standing);
        };
        const auto reservedBody = [this](BodyTargetKind kind, std::uint32_t id) {
          return std::ranges::any_of(world_.guards, [kind, id](const Guard &other) { return other.bodyTargetKind == kind && other.bodyTargetId == id && !IsBody(other) && other.heldBy == 0 && other.cqcState == CqcState::None && other.staggerRemaining <= 0.f; });
        };
        const auto reachableBody = [&guard](sf::Vector2f position) {
          return guard.navigationRejectTime <= 0.f || (guard.navigationRejectedTarget - position).length() > SecurityConfig::BodyInspectionReach;
        };
        if (guard.bodyTargetKind == BodyTargetKind::None) for (const auto &body : world_.guards) if (body.id != guard.id && IsBody(body) && !body.bodyHidden && body.storedIn == 0 &&
            (!body.bodyReported || (body.state == GuardState::Incapacitated && body.health > 0.f && body.heldBy == 0 &&
              std::ranges::none_of(world_.agents, [&body](const Agent &agent) { return agent.draggingGuard == static_cast<int>(body.id); }))) &&
            reachableBody(body.position) && seesBody(body.position) && !reservedBody(BodyTargetKind::Guard, body.id)) {
          guard.bodyTargetKind = BodyTargetKind::Guard; guard.bodyTargetId = body.id; guard.bodyInspectionTime = 0.f;
          guard.lastKnown = body.position; guard.state = GuardState::ApproachBody; guard.stateTime = 0.f; guard.navigation.clear();
          break;
        }
        if (guard.bodyTargetKind == BodyTargetKind::None) for (const auto &body : world_.agents) if ((!body.alive || body.incapacitated) && body.connected && !body.hidden && body.hideoutId == 0 && !body.bodyReported && reachableBody(body.position) && seesBody(body.position) && !reservedBody(BodyTargetKind::Agent, body.id)) {
          guard.bodyTargetKind = BodyTargetKind::Agent; guard.bodyTargetId = body.id; guard.bodyInspectionTime = 0.f;
          guard.lastKnown = body.position; guard.state = GuardState::ApproachBody; guard.stateTime = 0.f; guard.navigation.clear();
          break;
        }
        if (UpdateBodyInvestigation(guard, dt)) { recordFootstep(); continue; }
      }
      if (world_.alarm && guard.state == GuardState::Patrol) { guard.state = GuardState::Search; guard.stateTime = 0.f; guard.lastKnown = securityHasClue_ ? securityLastKnown_ : guard.position; }
      if (guard.state != GuardState::Patrol) guard.patrolPauseRemaining = 0.f;
      sf::Vector2f destination = guard.position;
      if (guard.state == GuardState::Patrol && !guard.patrol.empty()) {
        if (guard.patrolPauseRemaining > 0.f) guard.patrolPauseRemaining = std::max(0.f, guard.patrolPauseRemaining - dt);
        else {
          destination = guard.patrol[guard.waypoint];
          if (Distance(guard.position, destination) < PatrolWaypointDistance) {
            const auto tick = static_cast<std::uint32_t>(world_.elapsed * GameplayConfig::SimulationHz);
            const std::uint32_t sample = (world_.seed ^ (guard.id * 2246822519u) ^ (static_cast<std::uint32_t>(guard.waypoint) * 3266489917u) ^ tick) * 1664525u + 1013904223u;
            const float fraction = static_cast<float>(sample >> 8u) / 16777215.f;
            guard.patrolPauseRemaining = SecurityConfig::PatrolPauseMinimum + fraction * (SecurityConfig::PatrolPauseMaximum - SecurityConfig::PatrolPauseMinimum);
            guard.waypoint = (guard.waypoint + 1) % guard.patrol.size();
            const auto watch = guard.patrol[guard.waypoint] - guard.position;
            if (Length(watch) > 0.01f) guard.facing = Unit(watch);
            guard.navigation.clear(); destination = guard.position;
          }
        }
      } else if (guard.state == GuardState::Investigate || guard.state == GuardState::Alert || guard.state == GuardState::Search || guard.state == GuardState::Vigilance) destination = guard.lastKnown;
      else if (guard.state == GuardState::Retiring) destination = guard.reinforcementExit;
      if (!seen && (guard.state == GuardState::Search || guard.state == GuardState::Vigilance)) destination = SecuritySearchDestination(guard);
      if (seen && guard.state == GuardState::Alert && Distance(guard.position, seen->position) <= GuardAttackDistance * 0.8f) destination = guard.position;
      const auto offset = destination - guard.position;
      if (Length(offset) > InvestigationStopDistance) {
        Navigate(guard, destination, guard.state == GuardState::Alert ? GuardAlertSpeed : GuardSpeed, dt);
      } else if (guard.state == GuardState::Search || guard.state == GuardState::Vigilance) {
        const float angle = dt * SearchAngularSpeed;
        guard.facing = {guard.facing.x * std::cos(angle) - guard.facing.y * std::sin(angle), guard.facing.x * std::sin(angle) + guard.facing.y * std::cos(angle)};
      }
      if ((guard.state == GuardState::Investigate && guard.stateTime > InvestigationDuration && !world_.securityActive && world_.securityAlert != SecurityAlert::Yellow) || (guard.state == GuardState::Search && guard.stateTime > SecurityConfig::SearchDuration && world_.securityAlert == SecurityAlert::Calm)) { guard.state = GuardState::Patrol; guard.stateTime = 0.f; }
      recordFootstep();
    }
  }

  void Simulation::UpdateObjects(float dt) { UpdateDevices(dt); }

  void Simulation::CheckResults() {
    if (world_.phase != GamePhase::Mission) return;
    const auto &contract = GetContract(world_.contract);
    if (std::ranges::any_of(world_.agents, [](const Agent &agent) { return agent.rescueExpired && agent.privateOrder != PrivateOrder::Traitor; })) { Finish(false, "Se agotaron los 55 segundos para rescatar al compañero."); return; }
    if (world_.events.size() >= GameplayConfig::MaximumEvents) { Finish(false, "Registro operacional lleno; operación cancelada."); return; }
    if (world_.elapsed >= contract.timeLimit) { Finish(false, "Se agotó la ventana de extracción."); return; }
    if ((contract.noAlarm && world_.alarmCount > 0) || world_.alarmCount >= contract.maxAlarms) { Finish(false, "Contrato cancelado por alarmas confirmadas."); return; }
    const auto traitor = std::ranges::find_if(world_.agents, [](const Agent &agent) { return agent.privateOrder == PrivateOrder::Traitor && agent.connected; });
    if (traitor != world_.agents.end() && traitor->alive && world_.alarmCount == 0) {
      const bool anyOther = std::ranges::any_of(world_.agents, [&traitor](const Agent &agent) { return agent.connected && agent.id != traitor->id; });
      const bool allDead = std::ranges::none_of(world_.agents, [&traitor](const Agent &agent) { return agent.connected && agent.id != traitor->id && agent.alive; });
      if (anyOther && allDead) { Finish(false, "El traidor eliminó a los demás agentes sin activar alarmas.", true); return; }
    }
    const auto operational = [](const Agent &agent) { return agent.connected && agent.alive && !agent.incapacitated && agent.privateOrder != PrivateOrder::Traitor; };
    if (std::ranges::none_of(world_.agents, operational)) { Finish(false, "No quedan agentes del equipo operativos."); return; }
    const bool rescuePending = std::ranges::any_of(world_.agents, [](const Agent &agent) { return agent.connected && agent.alive && agent.incapacitated && agent.privateOrder != PrivateOrder::Traitor; });
    if (!rescuePending && world_.objectives >= contract.requiredObjectives && std::ranges::all_of(world_.agents, [&operational](const Agent &agent) { return !operational(agent) || agent.extracted; })) Finish(true, "Contrato cumplido y equipo extraído.");
  }

  void Simulation::Finish(bool success, const std::string &reason, bool traitorVictory) {
    for (auto &agent : world_.agents) { CancelPuzzle(agent); ReleaseCqc(agent); CancelManeuver(agent); agent.aiming = false; }
    world_.phase = GamePhase::Results;
    world_.message = reason;
    for (const auto &agent : world_.agents) {
      auto result = EvaluateMission(GetContract(world_.contract), world_, agent.id, success && agent.privateOrder != PrivateOrder::Traitor, reason);
      result.traitorVictory = traitorVictory && agent.privateOrder == PrivateOrder::Traitor;
      if (result.traitorVictory) { result.success = true; result.score = 1000; result.rank = Rank::S; }
      world_.results.emplace(agent.id, result);
    }
  }

  nlohmann::json Simulation::SnapshotFor(PlayerId player, bool visibilityFiltered) const {
    using nlohmann::json;
    const auto foundObserver = std::ranges::find_if(world_.agents, [player](const Agent &agent) { return agent.id == player; });
    const Agent *observer = foundObserver == world_.agents.end() ? nullptr : &*foundObserver;
    const bool filtered = visibilityFiltered && IsTactical(world_);
    const auto visible = [&](sf::Vector2f point, Stance stance = Stance::Standing) {
      return !filtered || (observer && Visibility::IsVisible(world_, *observer, point, stance));
    };
    std::unordered_set<std::uint32_t> visibleGuards, visibleObjects, visibleAgents;
    for (const auto &guard : world_.guards) if (!filtered || (observer && Visibility::GuardVisible(world_, *observer, guard))) visibleGuards.insert(guard.id);
    for (const auto &object : world_.objects) if (!filtered || (observer && Visibility::ObjectVisible(world_, *observer, object))) visibleObjects.insert(object.id);
    for (const auto &agent : world_.agents) if (!filtered || agent.id == player || (!agent.hidden && visible(agent.position, agent.stance))) visibleAgents.insert(agent.id);
    if (filtered && observer) for (const auto &agent : world_.agents) {
      const bool held = observer->cqcHeldBy == agent.id && agent.cqcHoldingAgent == player;
      const bool holding = observer->cqcHoldingAgent == agent.id && agent.cqcHeldBy == player;
      if ((held || holding) && agent.connected && agent.alive && !agent.hidden && !observer->hidden && Distance(agent.position, observer->position) < GameplayConfig::CqcDistance * GameplayConfig::CqcMaximumStretch) visibleAgents.insert(agent.id);
    }
    json snapshot{{"version", GameplayConfig::ProtocolVersion}, {"player", player}, {"phase", static_cast<int>(world_.phase)},
      {"contract", static_cast<int>(world_.contract)}, {"seed", world_.seed}, {"tick", world_.tick}, {"size", VectorJson(world_.size)},
      {"elapsed", world_.elapsed}, {"alarm", world_.alarm}, {"alarmRemaining", alarmRemaining_}, {"alarmCount", world_.alarmCount}, {"objectives", world_.objectives},
      {"secondary", world_.secondary}, {"message", filtered ? "Operación en curso." : world_.message}, {"visibilityFiltered", filtered},
      {"securityActive", world_.securityActive}, {"securityAlert", static_cast<int>(world_.securityAlert)}, {"securityRemaining", world_.securityRemaining}, {"reinforcementWaves", world_.reinforcementWaves}};
    snapshot["rescueSignals"] = json::array();
    if (filtered && observer && observer->privateOrder != PrivateOrder::Traitor) for (const auto &ally : world_.agents) {
      if (ally.id == player || !ally.connected || !ally.incapacitated || ally.privateOrder == PrivateOrder::Traitor) continue;
      const auto delta = ally.position - observer->position;
      snapshot["rescueSignals"].push_back({{"agent",ally.id},{"direction",VectorJson(delta.lengthSquared()>0.0001f?Unit(delta):sf::Vector2f{1.f,0.f})},{"remaining",ally.incapacityRemaining}});
    }
    snapshot["securityEntries"] = json::array();
    if (!filtered) {
      for (const auto &entry : world_.securityEntries) snapshot["securityEntries"].push_back({{"position", VectorJson(entry.position)}, {"rally", VectorJson(entry.rally)}});
      snapshot["securityInternal"] = {{"contactAge", securityContactAge_}, {"waveTimer", reinforcementTimer_},
        {"lastKnown", VectorJson(securityLastKnown_)}, {"hasClue", securityHasClue_}, {"nextGuard", nextGuardId_}, {"contactTick",securityContactTick_}, {"changeTick",securityAlertChangeTick_}};
    }
    snapshot["visibility"] = nullptr;
    if (filtered && observer) {
      const auto grid = Visibility::BuildGrid(world_, *observer);
      snapshot["visibility"] = {{"observer", grid.observer}, {"tick", grid.tick}, {"cellSize", grid.cellSize},
        {"columns", grid.columns}, {"rows", grid.rows}, {"cells", grid.cells}, {"visualOrigin", VectorJson(grid.visualOrigin)}, {"visualRadii", grid.visualRadii}};
    }
    snapshot["agents"] = json::array();
    for (const auto &agent : world_.agents) {
      if (!visibleAgents.contains(agent.id)) continue;
      auto cover = agent.cover;
      if (filtered && agent.id != player && cover.object != 0 && !visibleObjects.contains(cover.object)) cover = {};
      json value{{"id", agent.id}, {"name", agent.name}, {"position", VectorJson(agent.position)}, {"facing", VectorJson(agent.facing)},
        {"weaponAim",VectorJson(agent.weaponAim)}, {"healthRecoveryRemaining",agent.healthRecoveryRemaining},
        {"stance", static_cast<int>(agent.stance)}, {"alive", agent.alive}, {"incapacitated", agent.incapacitated}, {"bodyReported", agent.bodyReported}, {"connected", agent.connected}, {"hidden", agent.hidden},
        {"moving", agent.moving}, {"running", agent.running}, {"extracted", agent.extracted}, {"animationTime", agent.animationTime},
        {"health", agent.health}, {"stamina", agent.stamina}, {"incapacityRemaining", agent.incapacityRemaining}, {"actionCooldown", agent.actionCooldown}, {"footstepTime", agent.footstepTime},
        {"draggingGuard", filtered && agent.id != player && !visibleGuards.contains(static_cast<std::uint32_t>(agent.draggingGuard)) ? -1 : agent.draggingGuard}, {"aimSlope", agent.aimSlope},
        {"hideoutId", agent.hideoutId}, {"hideoutExit", VectorJson(agent.hideoutExit)},
        {"actionPose", static_cast<int>(agent.actionPose)}, {"actionPoseTime", agent.actionPoseTime},
        {"cqcHoldingGuard", !filtered || visibleGuards.contains(agent.cqcHoldingGuard) ? agent.cqcHoldingGuard : 0},
        {"cqcProgress", agent.cqcProgress}, {"cqcHoldTime", agent.cqcHoldTime}, {"cqcReleaseTime", agent.cqcReleaseTime},
        {"cqcPulseTime", agent.cqcPulseTime}, {"cqcPulseReady", agent.cqcPulseReady}, {"staggerRemaining", agent.staggerRemaining},
        {"detectionProgress", agent.id == player ? agent.detectionProgress : 0.f}, {"detectionConfirmed", agent.id == player && agent.detectionConfirmed},
        {"cover", {{"active", cover.active}, {"anchor", VectorJson(cover.anchor)}, {"normal", VectorJson(cover.normal)},
          {"surfacePosition", VectorJson(cover.surface.position)}, {"surfaceSize", VectorJson(cover.surface.size)},
          {"obstacle", cover.obstacle}, {"object", cover.object}, {"peekSide", static_cast<int>(cover.peekSide)},
          {"canPeekLeft", cover.canPeekLeft}, {"canPeekRight", cover.canPeekRight}}},
        {"loadout", {agent.loadout.lethalAmmo, agent.loadout.nonLethalAmmo, agent.loadout.decoys, agent.loadout.tools}}};
      if (agent.id == player) {
        value["privateOrder"] = static_cast<int>(agent.privateOrder); value["privateComplete"] = agent.privateComplete;
        const auto &guide = agent.decoyGuide;
        value["decoyGuide"] = {{"active", guide.active}, {"direction", VectorJson(guide.direction)}, {"distance", guide.distance},
          {"destination", VectorJson(guide.destination)}, {"maximum", VectorJson(guide.maximum)}, {"travelled", guide.travelled},
          {"maximumTravelled", guide.maximumTravelled}, {"valid", guide.valid}, {"blocked", guide.blocked}, {"maximumBlocked", guide.maximumBlocked}};
      }
      snapshot["agents"].push_back(std::move(value));
    }
    snapshot["guards"] = json::array();
    for (const auto &guard : world_.guards) {
      if (!visibleGuards.contains(guard.id)) continue;
      json patrol = json::array();
      if (!filtered) for (auto point : guard.patrol) patrol.push_back(VectorJson(point));
      snapshot["guards"].push_back({{"id", guard.id}, {"position", VectorJson(guard.position)}, {"facing", VectorJson(guard.facing)},
        {"state", static_cast<int>(guard.state)}, {"cqcState", static_cast<int>(guard.cqcState)}, {"patrol", patrol}, {"waypoint", filtered ? 0 : guard.waypoint},
        {"lastKnown", VectorJson(filtered ? guard.position : guard.lastKnown)}, {"suspicion", guard.suspicion}, {"bodyHidden", guard.bodyHidden}, {"bodyReported", guard.bodyReported}, {"heldBy", !filtered || visibleAgents.contains(guard.heldBy) ? guard.heldBy : 0},
        {"stateTime", guard.stateTime}, {"attackCooldown", guard.attackCooldown}, {"attackTime", guard.attackTime}, {"meleeAttack", guard.meleeAttack}, {"ammo", guard.ammo}, {"incapacityRemaining", guard.incapacityRemaining},
        {"health", guard.health}, {"stun", guard.stun}, {"detectedTime", guard.detectedTime}, {"storedIn", guard.storedIn}, {"stepTime", guard.stepTime},
        {"knownHideout", filtered ? 0 : guard.knownHideout}, {"hideoutInspectionTime", guard.hideoutInspectionTime},
        {"observedAgent", !filtered || guard.observedAgent == player ? guard.observedAgent : 0}, {"reinforcement", guard.reinforcement},
        {"reinforcementExit", VectorJson(filtered ? sf::Vector2f{} : guard.reinforcementExit)}, {"staggerRemaining", guard.staggerRemaining},
        {"bodyTargetKind",filtered ? 0 : static_cast<int>(guard.bodyTargetKind)}, {"bodyTargetId",filtered ? 0 : guard.bodyTargetId}, {"bodyInspectionTime",guard.bodyInspectionTime}});
    }
    snapshot["obstacles"] = json::array();
    for (const auto &obstacle : world_.obstacles) snapshot["obstacles"].push_back({{"position", VectorJson(obstacle.bounds.position)},
      {"size", VectorJson(obstacle.bounds.size)}, {"cover", static_cast<int>(obstacle.cover)}, {"solid", obstacle.solid},
      {"blocksVision", obstacle.blocksVision}, {"wallCover", obstacle.wallCover}, {"name", obstacle.name}});
    snapshot["objects"] = json::array();
    for (const auto &object : world_.objects) {
      if (!visibleObjects.contains(object.id)) continue;
      snapshot["objects"].push_back({{"id", object.id}, {"kind", static_cast<int>(object.kind)},
      {"position", VectorJson(object.position)}, {"name", object.name}, {"active", object.active}, {"completed", object.completed},
      {"open", object.open}, {"progress", object.progress}, {"facing", VectorJson(object.facing)},
      {"footprint", VectorJson(object.footprint)}, {"solid", static_cast<int>(object.solid)},
      {"blocksVision", static_cast<int>(object.blocksVision)}, {"wallCover", static_cast<int>(object.wallCover)}, {"interactive", object.interactive}, {"decorative", object.decorative},
      {"hideoutKind", static_cast<int>(object.hideoutKind)}, {"occupied", WorldGeometry::HideoutOccupied(object)}, {"occupantAgent", !filtered || object.occupantAgent == player ? object.occupantAgent : 0}, {"occupantGuard", filtered ? 0 : object.occupantGuard},
      {"hideoutAperture", object.hideoutAperture}, {"lifted", object.lifted}, {"inspectionProgress", object.inspectionProgress}, {"observedAgent", !filtered || object.observedAgent == player ? object.observedAgent : 0}});
    }
    snapshot["footsteps"] = json::array();
    for (const auto &cue : world_.footsteps) {
      if (filtered && (!observer || !Visibility::FootstepAudible(world_, *observer, cue))) continue;
      json value{{"position", VectorJson(cue.position)}, {"direction", cue.direction}, {"remaining", cue.remaining}};
      if (!filtered) value["sourceGuard"] = cue.sourceGuard;
      snapshot["footsteps"].push_back(std::move(value));
    }
    snapshot["noises"] = json::array();
    for (const auto &noise : world_.noises) {
      if (filtered && (!observer || !Visibility::NoiseAudible(world_, *observer, noise))) continue;
      auto point = noise.position;
      if (filtered && noise.actor != player && !visible(point)) { point.x = std::floor(point.x / AudioConfig::PositionQuantization) * AudioConfig::PositionQuantization; point.y = std::floor(point.y / AudioConfig::PositionQuantization) * AudioConfig::PositionQuantization; }
      snapshot["noises"].push_back({{"position", VectorJson(point)}, {"radius", noise.radius}, {"remaining", noise.remaining}, {"actor", !filtered || noise.actor == player ? noise.actor : 0}, {"guardId", !filtered ? noise.guardId : 0}, {"kind", static_cast<int>(noise.kind)}, {"duration", noise.duration}});
    }
    snapshot["shots"] = json::array();
    for (const auto &shot : world_.shots) {
      if (!visible(shot.from) && !visible(shot.to)) continue;
      auto from = shot.from, to = shot.to;
      const bool sourceKnown = !filtered || (shot.guardId ? visibleGuards.contains(shot.guardId) : shot.actor == 0 || visibleAgents.contains(shot.actor));
      const bool targetKnown = !filtered || shot.targetKind == ShotTarget::None || (shot.targetKind == ShotTarget::Guard ? visibleGuards.contains(shot.targetId) : visibleAgents.contains(shot.targetId));
      const bool sourceVisible = visible(from) && sourceKnown, targetVisible = visible(to) && targetKnown;
      if (filtered && !sourceKnown && !(shot.targetKind == ShotTarget::Agent && shot.targetId == player)) continue;
      if (!sourceVisible || !targetVisible) {
        bool found = false;
        for (int sample = 0; sample <= 64; ++sample) {
          const auto point = shot.from + (shot.to - shot.from) * (static_cast<float>(sample) / 64.f);
          if (!visible(point)) continue;
          if (!found) { from = point; found = true; }
          to = point;
        }
        if (!found) continue;
      }
      snapshot["shots"].push_back({{"from", VectorJson(from)}, {"to", VectorJson(to)}, {"remaining", shot.remaining}, {"lethal", shot.lethal},
        {"actor", sourceVisible ? shot.actor : 0}, {"guardId", sourceVisible ? shot.guardId : 0}, {"hit", targetVisible && shot.hit}, {"blocked", targetVisible && shot.blocked},
        {"zone", targetVisible ? static_cast<int>(shot.zone) : 0}, {"fromHeight", shot.fromHeight}, {"toHeight", shot.toHeight}, {"targetId", targetVisible ? shot.targetId : 0}, {"targetKind", targetVisible ? static_cast<int>(shot.targetKind) : 0}});
    }
    snapshot["events"] = json::array();
    const auto first = world_.events.size() > 64 ? world_.events.size() - 64 : 0;
    // El anfitrión conserva todo el registro; los clientes reciben solo actividad reciente y nunca órdenes ajenas.
    for (std::size_t index = first; index < world_.events.size(); ++index) {
      const auto &event = world_.events[index];
      if (event.kind == EventKind::PrivateObjective && event.actor != player) continue;
      if (filtered && event.actor != player) {
        const bool localDamage = (event.kind == EventKind::AgentDamaged || event.kind == EventKind::AgentKilled || event.kind == EventKind::AgentIncapacitated || event.kind == EventKind::AgentRevived) && event.target == player;
        const bool guardEvent = event.kind == EventKind::GuardShot || event.kind == EventKind::GuardMelee || event.kind == EventKind::GuardKilled || event.kind == EventKind::GuardIncapacitated || event.kind == EventKind::BodyFound;
        if (!localDamage && !(guardEvent && visibleGuards.contains(event.target))) continue;
      }
      const bool hiddenObserver = filtered && (event.kind == EventKind::ConfirmedDetection || event.kind == EventKind::AgentAlarm) && !visibleGuards.contains(event.target);
      const bool hiddenGuard = filtered && (event.kind == EventKind::GuardKilled || event.kind == EventKind::GuardIncapacitated) && !visibleGuards.contains(event.target);
      snapshot["events"].push_back({{"sequence", event.sequence}, {"tick", event.tick}, {"actor", event.actor}, {"kind", static_cast<int>(event.kind)}, {"target", hiddenObserver || hiddenGuard ? 0 : event.target}});
    }
    snapshot["results"] = json::array();
    for (const auto &[id, result] : world_.results) snapshot["results"].push_back({{"id", id}, {"success", result.success}, {"traitorVictory", result.traitorVictory},
      {"maestroEligible", result.maestroEligible}, {"score", result.score}, {"rank", static_cast<int>(result.rank)}, {"reason", result.reason}, {"maestroDeniedReason",result.maestroDeniedReason}});
    StateExtensions::Write(snapshot, world_, player, filtered);
    return snapshot;
  }

  bool Simulation::LoadSnapshot(const nlohmann::json &snapshot, std::string &error) {
    try {
      if (!snapshot.is_object() || snapshot.dump().size() > GameplayConfig::MaximumSnapshotBytes || snapshot.at("version").get<std::uint32_t>() != GameplayConfig::ProtocolVersion) throw std::runtime_error("Versión o tamaño de estado inválido");
      const auto array = [&snapshot](const char *key, std::size_t maximum) -> const nlohmann::json & {
        const auto &values = snapshot.at(key);
        if (!values.is_array() || values.size() > maximum) throw std::runtime_error("Colección de estado inválida");
        return values;
      };
      const auto finite = [](const nlohmann::json &value, float maximum) {
        const float number = value.get<float>();
        if (!std::isfinite(number) || number < 0.f || number > maximum) throw std::runtime_error("Magnitud de estado inválida");
        return number;
      };
      const auto text = [](const nlohmann::json &value, std::size_t maximum) {
        const auto result = value.get<std::string>();
        if (result.size() > maximum) throw std::runtime_error("Texto de estado demasiado largo");
        return result;
      };
      WorldState replacement;
      const PlayerId recipient = snapshot.at("player").get<PlayerId>();
      replacement.phase = ParseEnum<GamePhase>(snapshot.at("phase"), 2);
      replacement.contract = ParseEnum<ContractId>(snapshot.at("contract"), 2);
      replacement.seed = snapshot.at("seed").get<std::uint32_t>(); replacement.tick = snapshot.at("tick").get<std::uint64_t>();
      replacement.size = ParseVector(snapshot.at("size"));
      if (replacement.size.x < 100.f || replacement.size.y < 100.f || replacement.size.x > 10000.f || replacement.size.y > 10000.f) throw std::runtime_error("Tamaño de mapa inválido");
      replacement.elapsed = finite(snapshot.at("elapsed"), 86400.f);
      const float alarmRemaining = finite(snapshot.at("alarmRemaining"), GameplayConfig::AlarmDuration);
      replacement.alarm = snapshot.at("alarm").get<bool>(); replacement.alarmCount = snapshot.at("alarmCount").get<int>();
      replacement.objectives = snapshot.at("objectives").get<int>(); replacement.secondary = snapshot.at("secondary").get<int>();
      if (replacement.alarmCount < 0 || replacement.alarmCount > 10000 || replacement.objectives < 0 || replacement.objectives > 100 || replacement.secondary < 0 || replacement.secondary > 100) throw std::runtime_error("Contadores de estado inválidos");
      replacement.message = text(snapshot.at("message"), 2048);
      replacement.securityActive = snapshot.value("securityActive", false);
      replacement.securityAlert = ParseEnum<SecurityAlert>(snapshot.at("securityAlert"), 3);
      replacement.securityRemaining = finite(snapshot.at("securityRemaining"), SecurityConfig::SearchDuration);
      if (replacement.securityAlert == SecurityAlert::Yellow && replacement.securityRemaining > SecurityConfig::VigilanceDuration) throw std::runtime_error("Plazo de vigilancia inválido");
      replacement.reinforcementWaves = snapshot.value("reinforcementWaves", 0u);
      if (replacement.reinforcementWaves > SecurityConfig::MaximumWaves) throw std::runtime_error("Oleadas de seguridad inválidas");
      if (snapshot.contains("securityEntries")) for (const auto &value : array("securityEntries", 8)) {
        if (snapshot.value("visibilityFiltered", false)) throw std::runtime_error("Entradas privadas en estado filtrado");
        const SecurityEntry entry{ParseVector(value.at("position")), ParseVector(value.at("rally"))};
        for (const auto point : {entry.position, entry.rally}) if (point.x < 0.f || point.y < 0.f || point.x > replacement.size.x || point.y > replacement.size.y) throw std::runtime_error("Entrada de seguridad fuera del mapa");
        replacement.securityEntries.push_back(entry);
      }
      float contactAge = 0.f, waveTimer = 0.f; sf::Vector2f lastSecurityContact; bool hasSecurityClue = false; std::uint32_t nextGuard = 1;
      std::uint64_t contactTick=0, changeTick=0;
      if (snapshot.contains("securityInternal")) {
        if (snapshot.value("visibilityFiltered", false)) throw std::runtime_error("Pista privada de seguridad en cliente");
        const auto &security = snapshot.at("securityInternal");
        contactAge = finite(security.at("contactAge"), 86400.f); waveTimer = finite(security.at("waveTimer"), 86400.f);
        lastSecurityContact = ParseVector(security.at("lastKnown")); hasSecurityClue = security.at("hasClue").get<bool>();
        nextGuard = security.at("nextGuard").get<std::uint32_t>();
        contactTick=security.value("contactTick",std::uint64_t{0}); changeTick=security.value("changeTick",std::uint64_t{0});
        if (contactTick>replacement.tick || changeTick>replacement.tick) throw std::runtime_error("Tiempo de alerta adelantado");
        if (nextGuard == 0 || nextGuard > 1000000) throw std::runtime_error("Secuencia de refuerzos inválida");
      }
      if (snapshot.contains("rescueSignals")) for (const auto &value : array("rescueSignals", GameplayConfig::MaximumPlayers - 1)) {
        RescueSignal signal{value.at("agent").get<PlayerId>(),ParseVector(value.at("direction")),finite(value.at("remaining"), GameplayConfig::IncapacityDuration)};
        if (!signal.agent || signal.agent == recipient || signal.remaining <= 0.f || std::abs(signal.direction.lengthSquared() - 1.f) > 0.01f ||
            std::ranges::any_of(replacement.rescueSignals,[&](const RescueSignal &other){return signal.agent == other.agent;})) throw std::runtime_error("Señal de rescate inválida");
        replacement.rescueSignals.push_back(signal);
      }
      for (const auto &value : array("agents", GameplayConfig::MaximumPlayers)) {
        Agent agent;
        agent.id = value.at("id").get<PlayerId>();
        if (agent.id == 0 || std::ranges::any_of(replacement.agents, [&agent](const Agent &other) { return other.id == agent.id; })) throw std::runtime_error("Identificador de agente repetido");
        agent.name = text(value.at("name"), GameplayConfig::MaximumNameBytes);
        agent.position = ParseVector(value.at("position")); agent.facing = ParseVector(value.at("facing"));
        agent.weaponAim = ParseVector(value.at("weaponAim"));
        if (agent.weaponAim.lengthSquared() > 1.01f) throw std::runtime_error("Dirección de arma inválida");
        agent.healthRecoveryRemaining = finite(value.at("healthRecoveryRemaining"), GameplayConfig::PassiveHealthRecoveryDelay);
        agent.stance = ParseEnum<Stance>(value.at("stance"), 2);
        agent.alive = value.at("alive").get<bool>(); agent.incapacitated = value.at("incapacitated").get<bool>(); agent.bodyReported = value.at("bodyReported").get<bool>(); agent.connected = value.at("connected").get<bool>(); agent.hidden = value.at("hidden").get<bool>();
        agent.moving = value.at("moving").get<bool>(); agent.running = value.at("running").get<bool>(); agent.extracted = value.at("extracted").get<bool>();
        agent.animationTime = finite(value.at("animationTime"), 86400.f); agent.draggingGuard = value.at("draggingGuard").get<int>();
        agent.health = finite(value.at("health"), GameplayConfig::MaxHealth); agent.stamina = finite(value.at("stamina"), GameplayConfig::MaxStamina);
        agent.incapacityRemaining = finite(value.at("incapacityRemaining"), GameplayConfig::IncapacityDuration);
        agent.actionCooldown = finite(value.at("actionCooldown"), 60.f); agent.footstepTime = finite(value.at("footstepTime"), 86400.f);
        agent.aimSlope = value.value("aimSlope", 0.f);
        agent.hideoutId = value.value("hideoutId", std::uint32_t{0});
        agent.hideoutExit = ParseVector(value.value("hideoutExit", VectorJson(sf::Vector2f{})));
        agent.actionPose = ParseEnum<ActionPose>(value.value("actionPose", nlohmann::json(0)), 18);
        agent.actionPoseTime = finite(value.value("actionPoseTime", nlohmann::json(0.f)), std::max({GameplayConfig::AttackAnimationDuration, CqcConfig::KickCooldown, GameplayConfig::StanceTransitionDuration}));
        agent.cqcHoldingGuard = value.value("cqcHoldingGuard", std::uint32_t{0});
        agent.cqcProgress = value.value("cqcProgress", 0u);
        if (agent.cqcProgress > CqcConfig::RequiredPulses) throw std::runtime_error("Progreso CQC inválido");
        agent.cqcHoldTime = finite(value.value("cqcHoldTime", nlohmann::json(0.f)), 86400.f);
        agent.cqcReleaseTime = finite(value.value("cqcReleaseTime", nlohmann::json(0.f)), CqcConfig::MinimumRelease + GameplayConfig::MaximumStep);
        agent.cqcPulseTime = finite(value.value("cqcPulseTime", nlohmann::json(0.f)), 86400.f);
        agent.cqcPulseReady = value.value("cqcPulseReady", false);
        agent.staggerRemaining = finite(value.value("staggerRemaining", nlohmann::json(0.f)), CqcConfig::PushStagger);
        agent.detectionProgress = finite(value.value("detectionProgress", nlohmann::json(0.f)), 1.f);
        agent.detectionConfirmed = value.value("detectionConfirmed", false);
        if (agent.id != recipient && (agent.detectionProgress > 0.f || agent.detectionConfirmed)) throw std::runtime_error("Progreso de detección ajeno");
        if (!std::isfinite(agent.aimSlope) || std::abs(agent.aimSlope) > CombatConfig::MaximumAimSlope) throw std::runtime_error("Elevación de apuntado inválida");
        if (value.contains("cover")) {
          const auto &cover = value.at("cover");
          agent.cover.active = cover.at("active").get<bool>(); agent.cover.anchor = ParseVector(cover.at("anchor")); agent.cover.normal = ParseVector(cover.at("normal"));
          agent.cover.surface = {ParseVector(cover.at("surfacePosition")), ParseVector(cover.at("surfaceSize"))};
          agent.cover.obstacle = cover.at("obstacle").get<int>(); agent.cover.object = cover.at("object").get<std::uint32_t>();
          agent.cover.peekSide = ParseEnum<PeekSide>(cover.at("peekSide"), 2);
          agent.cover.canPeekLeft = cover.at("canPeekLeft").get<bool>(); agent.cover.canPeekRight = cover.at("canPeekRight").get<bool>();
          if (agent.cover.active && (agent.cover.surface.size.x <= 0.f || agent.cover.surface.size.y <= 0.f || std::abs(agent.cover.normal.lengthSquared() - 1.f) > 0.01f ||
              agent.cover.anchor.x < GameplayConfig::AgentRadius || agent.cover.anchor.y < GameplayConfig::AgentRadius ||
              agent.cover.anchor.x > replacement.size.x - GameplayConfig::AgentRadius || agent.cover.anchor.y > replacement.size.y - GameplayConfig::AgentRadius)) throw std::runtime_error("Cobertura inválida");
        }
        if ((!agent.alive && (agent.incapacitated || agent.incapacityRemaining > 0.f)) || (agent.alive && !agent.incapacitated && agent.health <= 0.f) || (agent.incapacitated != (agent.incapacityRemaining > 0.f))) throw std::runtime_error("Estado vital de agente inválido");
        const auto &loadout = value.at("loadout");
        if (!loadout.is_array() || loadout.size() != 4) throw std::runtime_error("Equipo inválido");
        std::array<int, 4> counts{};
        for (std::size_t index = 0; index < counts.size(); ++index) {
          counts[index] = loadout.at(index).get<int>();
          const int maximum = index < 2 ? GameplayConfig::MaximumAmmo : (index == 2 ? GameplayConfig::MaximumDecoys : GameplayConfig::MaximumTools);
          if (counts[index] < 0 || counts[index] > maximum) throw std::runtime_error("Cantidad de equipo inválida");
        }
        agent.loadout = {counts[0], counts[1], counts[2], counts[3]};
        if (agent.id == recipient && value.contains("privateOrder")) { agent.privateOrder = ParseEnum<PrivateOrder>(value.at("privateOrder"), 3); agent.privateComplete = value.at("privateComplete").get<bool>(); }
        else if (value.contains("privateOrder") || value.contains("privateComplete")) throw std::runtime_error("Orden ajena incluida en estado público");
        if (value.contains("decoyGuide")) {
          if (agent.id != recipient) throw std::runtime_error("Guía de lanzamiento ajena");
          const auto &valueGuide = value.at("decoyGuide"); auto &guide = agent.decoyGuide;
          guide.active = valueGuide.at("active").get<bool>(); guide.direction = ParseVector(valueGuide.at("direction"));
          guide.distance = finite(valueGuide.at("distance"), DistractionConfig::MaximumDecoyDistance);
          guide.destination = ParseVector(valueGuide.at("destination")); guide.maximum = ParseVector(valueGuide.at("maximum"));
          guide.travelled = finite(valueGuide.at("travelled"), DistractionConfig::MaximumDecoyDistance);
          guide.maximumTravelled = finite(valueGuide.at("maximumTravelled"), DistractionConfig::MaximumDecoyDistance);
          guide.valid = valueGuide.at("valid").get<bool>(); guide.blocked = valueGuide.at("blocked").get<bool>(); guide.maximumBlocked = valueGuide.at("maximumBlocked").get<bool>();
          if (guide.active && (!agent.alive || agent.incapacitated || agent.hidden || agent.extracted || std::abs(guide.direction.lengthSquared() - 1.f) > 0.01f ||
              std::abs((guide.destination - agent.position).length() - guide.travelled) > 0.15f || std::abs((guide.maximum - agent.position).length() - guide.maximumTravelled) > 0.15f ||
              guide.destination.x < 0.f || guide.destination.y < 0.f || guide.destination.x > replacement.size.x || guide.destination.y > replacement.size.y ||
              guide.maximum.x < 0.f || guide.maximum.y < 0.f || guide.maximum.x > replacement.size.x || guide.maximum.y > replacement.size.y)) throw std::runtime_error("Guía autoritativa inválida");
        }
        replacement.agents.push_back(agent);
      }
      for (const auto &value : array("guards", 32)) {
        Guard guard;
        guard.id = value.at("id").get<std::uint32_t>();
        if (guard.id == 0 || std::ranges::any_of(replacement.guards, [&guard](const Guard &other) { return other.id == guard.id; })) throw std::runtime_error("Identificador de guardia repetido");
        guard.position = ParseVector(value.at("position")); guard.facing = ParseVector(value.at("facing"));
        guard.state = ParseEnum<GuardState>(value.at("state"), 10); guard.cqcState = ParseEnum<CqcState>(value.at("cqcState"), 3);
        const auto &patrol = value.at("patrol");
        if (!patrol.is_array() || patrol.size() > 32) throw std::runtime_error("Patrulla inválida");
        for (const auto &point : patrol) guard.patrol.push_back(ParseVector(point));
        guard.waypoint = value.at("waypoint").get<std::size_t>();
        if (!guard.patrol.empty() && guard.waypoint >= guard.patrol.size()) throw std::runtime_error("Índice de patrulla inválido");
        guard.lastKnown = ParseVector(value.at("lastKnown")); guard.suspicion = finite(value.at("suspicion"), 1.f);
        guard.bodyHidden = value.at("bodyHidden").get<bool>(); guard.bodyReported = value.at("bodyReported").get<bool>(); guard.heldBy = value.at("heldBy").get<PlayerId>();
        guard.stateTime = finite(value.at("stateTime"), 86400.f); guard.attackCooldown = finite(value.at("attackCooldown"), 60.f);
        guard.attackTime = finite(value.at("attackTime"), GameplayConfig::AttackAnimationDuration); guard.meleeAttack = value.at("meleeAttack").get<bool>();
        if (!value.at("ammo").is_number_integer()) throw std::runtime_error("Munición de guardia inválida");
        const auto ammo = value.at("ammo").get<std::int64_t>();
        if (ammo < 0 || ammo > GameplayConfig::GuardAmmo) throw std::runtime_error("Munición de guardia inválida");
        guard.ammo = static_cast<int>(ammo);
        guard.incapacityRemaining = finite(value.at("incapacityRemaining"), GameplayConfig::GuardIncapacityDuration);
        guard.health = finite(value.value("health", nlohmann::json(CombatConfig::MaxGuardHealth)), CombatConfig::MaxGuardHealth);
        guard.stun = finite(value.value("stun", nlohmann::json(0.f)), CombatConfig::MaxGuardStun);
        guard.detectedTime = finite(value.value("detectedTime", nlohmann::json(0.f)), GameplayConfig::ConfirmedIndicatorDuration);
        guard.storedIn = value.value("storedIn", std::uint32_t{0});
        guard.stepTime = finite(value.value("stepTime", nlohmann::json(0.f)), 86400.f);
        guard.bodyTargetKind = ParseEnum<BodyTargetKind>(value.at("bodyTargetKind"), 2);
        guard.bodyTargetId = value.at("bodyTargetId").get<std::uint32_t>();
        guard.bodyInspectionTime = finite(value.at("bodyInspectionTime"), SecurityConfig::BodyInspectionDuration);
        if ((guard.bodyTargetKind == BodyTargetKind::None) != (guard.bodyTargetId == 0)) throw std::runtime_error("Reserva de cuerpo incoherente");
        if (snapshot.value("visibilityFiltered", false) && (guard.bodyTargetKind != BodyTargetKind::None || guard.bodyTargetId != 0)) throw std::runtime_error("Objetivo privado de inspección incluido en cliente");
        guard.staggerRemaining = finite(value.value("staggerRemaining", nlohmann::json(0.f)), std::max(CqcConfig::PushStagger, CqcConfig::KickStagger));
        guard.knownHideout = value.value("knownHideout", std::uint32_t{0});
        guard.hideoutInspectionTime = finite(value.value("hideoutInspectionTime", nlohmann::json(0.f)), SpatialConfig::HideoutInspectionDuration);
        guard.observedAgent = value.value("observedAgent", PlayerId{0});
        guard.reinforcement = value.value("reinforcement", false);
        guard.reinforcementExit = ParseVector(value.value("reinforcementExit", VectorJson(sf::Vector2f{})));
        if (snapshot.value("visibilityFiltered", false) && (guard.knownHideout || guard.reinforcementExit != sf::Vector2f{})) throw std::runtime_error("Conocimiento privado del guardia incluido en cliente");
        if (snapshot.value("visibilityFiltered", false) && guard.observedAgent != 0 && guard.observedAgent != recipient) throw std::runtime_error("Identidad observada ajena en guardia");
        if (guard.reinforcement && (guard.reinforcementExit.x < 0.f || guard.reinforcementExit.y < 0.f || guard.reinforcementExit.x > replacement.size.x || guard.reinforcementExit.y > replacement.size.y)) throw std::runtime_error("Salida de refuerzo fuera del mapa");
        replacement.guards.push_back(guard);
      }
      for (const auto &value : array("obstacles", 128)) {
        Obstacle obstacle;
        obstacle.bounds = {ParseVector(value.at("position")), ParseVector(value.at("size"))};
        if (obstacle.bounds.size.x <= 0.f || obstacle.bounds.size.y <= 0.f) throw std::runtime_error("Obstáculo sin dimensiones");
        obstacle.cover = ParseEnum<CoverKind>(value.at("cover"), 3); obstacle.solid = value.at("solid").get<bool>(); obstacle.name = text(value.at("name"), 128);
        obstacle.blocksVision = value.value("blocksVision", true); obstacle.wallCover = value.value("wallCover", true);
        replacement.obstacles.push_back(obstacle);
      }
      for (const auto &value : array("objects", 64)) {
        Interactable object;
        object.id = value.at("id").get<std::uint32_t>();
        if (object.id == 0 || std::ranges::any_of(replacement.objects, [&object](const Interactable &other) { return other.id == object.id; })) throw std::runtime_error("Identificador de objeto repetido");
        object.kind = ParseEnum<ObjectKind>(value.at("kind"), 16); object.position = ParseVector(value.at("position")); object.name = text(value.at("name"), 128);
        object.active = value.at("active").get<bool>(); object.completed = value.at("completed").get<bool>(); object.open = value.at("open").get<bool>();
        object.progress = finite(value.at("progress"), 86400.f); object.facing = ParseVector(value.at("facing"));
        if (value.contains("footprint")) {
          object.footprint = ParseVector(value.at("footprint"));
          if (object.footprint.x < 0.f || object.footprint.y < 0.f || (object.footprint.x == 0.f) != (object.footprint.y == 0.f) || object.footprint.x > replacement.size.x || object.footprint.y > replacement.size.y) throw std::runtime_error("Huella de objeto inválida");
        }
        object.solid = ParseEnum<SpatialOverride>(value.value("solid", nlohmann::json(0)), 2);
        object.blocksVision = ParseEnum<SpatialOverride>(value.value("blocksVision", nlohmann::json(0)), 2);
        object.wallCover = ParseEnum<SpatialOverride>(value.value("wallCover", nlohmann::json(0)), 2);
        object.interactive = value.value("interactive", true); object.decorative = value.value("decorative", false);
        object.hideoutKind = ParseEnum<HideoutKind>(value.value("hideoutKind", nlohmann::json(0)), 3);
        object.occupantAgent = value.value("occupantAgent", PlayerId{0}); object.occupantGuard = value.value("occupantGuard", std::uint32_t{0});
        object.occupied = value.value("occupied", object.occupantAgent != 0 || object.occupantGuard != 0);
        object.hideoutAperture = finite(value.value("hideoutAperture", nlohmann::json(SpatialConfig::ClosetDefaultAperture)), 1.f);
        object.inspectionProgress = finite(value.value("inspectionProgress", nlohmann::json(0.f)), 1.f);
        object.lifted = value.value("lifted", false); object.observedAgent = value.value("observedAgent", PlayerId{0});
        if (snapshot.value("visibilityFiltered", false) && object.observedAgent != 0 && object.observedAgent != recipient) throw std::runtime_error("Identidad observada ajena en cámara");
        if (object.lifted && (WorldGeometry::HideoutType(object) != HideoutKind::UnderTable || object.occupantAgent != 0 || object.occupantGuard != 0)) throw std::runtime_error("Mesa levantada con ocupante o tipo inválido");
        const bool hasOccupant = object.occupantAgent != 0 || object.occupantGuard != 0;
        if ((!snapshot.value("visibilityFiltered", false) && object.occupied != hasOccupant) || (hasOccupant && !object.occupied)) throw std::runtime_error("Estado de ocupación incoherente");
        if (object.occupantAgent && object.occupantGuard) throw std::runtime_error("Escondite con capacidad excedida");
        if ((object.occupantAgent || object.occupantGuard) && WorldGeometry::HideoutType(object) == HideoutKind::None) throw std::runtime_error("Ocupante sin escondite compatible");
        replacement.objects.push_back(object);
      }
      if (snapshot.contains("footsteps")) for (const auto &value : array("footsteps", 128)) {
        FootstepCue cue;
        cue.position = ParseVector(value.at("position"));
        if (!value.at("direction").is_number_integer()) throw std::runtime_error("Dirección de pista inválida");
        const int direction = value.at("direction").get<int>();
        if (direction < 0 || direction > 7 || cue.position.x < 0.f || cue.position.y < 0.f || cue.position.x > replacement.size.x || cue.position.y > replacement.size.y) throw std::runtime_error("Pista de pasos inválida");
        cue.direction = static_cast<std::uint8_t>(direction);
        cue.remaining = finite(value.at("remaining"), DistractionConfig::FootstepLifetime);
        cue.sourceGuard = value.value("sourceGuard", std::uint32_t{0});
        if (snapshot.value("visibilityFiltered", false) && value.contains("sourceGuard")) throw std::runtime_error("Pista con identidad privada");
        replacement.footsteps.push_back(cue);
      }
      for (const auto &value : array("noises", 128)) replacement.noises.push_back({ParseVector(value.at("position")), finite(value.at("radius"), 10000.f), finite(value.at("remaining"), 60.f), value.at("actor").get<PlayerId>(), value.value("guardId", std::uint32_t{0})});
      for (const auto &value : array("shots", 128)) {
        ShotTrace shot{ParseVector(value.at("from")), ParseVector(value.at("to")), finite(value.at("remaining"), GameplayConfig::ShotTraceDuration), value.at("lethal").get<bool>(), value.at("actor").get<PlayerId>(), value.at("guardId").get<std::uint32_t>(), value.at("hit").get<bool>(), value.at("blocked").get<bool>()};
        shot.zone = ParseEnum<HitZone>(value.value("zone", nlohmann::json(0)), 3);
        const float maximumHeight = GameplayConfig::WeaponRange * CombatConfig::MaximumAimSlope + CombatConfig::StandingHeight;
        shot.fromHeight = finite(value.value("fromHeight", nlohmann::json(0.f)), CombatConfig::StandingHeight);
        shot.toHeight = finite(value.value("toHeight", nlohmann::json(0.f)), maximumHeight);
        shot.targetId = value.value("targetId", std::uint32_t{0});
        shot.targetKind = ParseEnum<ShotTarget>(value.value("targetKind", nlohmann::json(0)), 2);
        replacement.shots.push_back(shot);
      }
      for (const auto &value : array("events", 64)) {
        MissionEvent event{value.at("sequence").get<std::uint64_t>(), value.at("tick").get<std::uint64_t>(), value.at("actor").get<PlayerId>(), ParseEnum<EventKind>(value.at("kind"), 28), value.at("target").get<std::uint32_t>()};
        if (event.kind == EventKind::PrivateObjective && event.actor != recipient) throw std::runtime_error("Evento privado ajeno");
        if (!replacement.events.empty() && event.sequence <= replacement.events.back().sequence) throw std::runtime_error("Secuencia de eventos inválida");
        replacement.events.push_back(event);
      }
      for (const auto &value : array("results", GameplayConfig::MaximumPlayers)) {
        MissionResult result;
        const auto id = value.at("id").get<PlayerId>();
        result.success = value.at("success").get<bool>(); result.traitorVictory = value.at("traitorVictory").get<bool>(); result.maestroEligible = value.at("maestroEligible").get<bool>();
        result.score = value.at("score").get<int>(); result.rank = ParseEnum<Rank>(value.at("rank"), 6); result.reason = text(value.at("reason"), 2048); result.maestroDeniedReason = text(value.value("maestroDeniedReason",nlohmann::json("")),2048);
        if (replacement.results.contains(id) || result.score < 0 || result.score > 1000000) throw std::runtime_error("Resultado inválido");
        replacement.results.emplace(id, result);
      }
      if (snapshot.value("visibilityFiltered", false)) {
        const auto &grid = snapshot.at("visibility");
        if (!grid.is_object() || grid.at("observer").get<PlayerId>() != recipient || grid.at("tick").get<std::uint64_t>() != replacement.tick) throw std::runtime_error("Destinatario de visibilidad inválido");
        replacement.visibility.authoritative = true; replacement.visibility.observer = recipient; replacement.visibility.tick = replacement.tick;
        replacement.visibility.visualOrigin = ParseVector(grid.at("visualOrigin"));
        const auto &radii = grid.at("visualRadii");
        if (!radii.is_array() || radii.size() != VisibilityConfig::VisualRayCount) throw std::runtime_error("Frontera visual incompleta");
        if (replacement.visibility.visualOrigin.x < 0.f || replacement.visibility.visualOrigin.y < 0.f || replacement.visibility.visualOrigin.x > replacement.size.x || replacement.visibility.visualOrigin.y > replacement.size.y) throw std::runtime_error("Origen de visión fuera del mapa");
        for (const auto &radius : radii) replacement.visibility.visualRadii.push_back(finite(radius, VisibilityConfig::StandingRange));
        replacement.visibility.cellSize = finite(grid.at("cellSize"), VisibilityConfig::CellSize);
        replacement.visibility.columns = grid.at("columns").get<unsigned int>(); replacement.visibility.rows = grid.at("rows").get<unsigned int>();
        if (replacement.visibility.cellSize != VisibilityConfig::CellSize || replacement.visibility.columns != static_cast<unsigned int>(std::ceil(replacement.size.x / VisibilityConfig::CellSize)) ||
            replacement.visibility.rows != static_cast<unsigned int>(std::ceil(replacement.size.y / VisibilityConfig::CellSize))) throw std::runtime_error("Dimensiones de visibilidad inválidas");
        const auto count = static_cast<std::size_t>(replacement.visibility.columns) * replacement.visibility.rows;
        const auto &cells = grid.at("cells");
        if (count > VisibilityConfig::MaximumCells || !cells.is_array() || cells.size() != count) throw std::runtime_error("Máscara de visibilidad inválida");
        for (const auto &cell : cells) {
          if (!cell.is_number_integer() || (cell != 0 && cell != 1)) throw std::runtime_error("Celda de visibilidad inválida");
          replacement.visibility.cells.push_back(cell.get<std::uint8_t>());
        }
        if (std::ranges::none_of(replacement.agents, [recipient](const Agent &agent) { return agent.id == recipient; })) throw std::runtime_error("Estado sin agente local");
      }
      for (const auto &object : replacement.objects) {
        if (object.occupantGuard && !WorldGeometry::HideoutAllowsBody(object)) throw std::runtime_error("El escondite no admite cuerpos");
        if (object.occupantAgent) {
          const auto agent = std::ranges::find_if(replacement.agents, [&](const Agent &entry) { return entry.id == object.occupantAgent; });
          if (agent == replacement.agents.end() || !agent->hidden || agent->hideoutId != object.id) throw std::runtime_error("Ocupación de agente incoherente");
        }
        if (object.occupantGuard) {
          const auto guard = std::ranges::find_if(replacement.guards, [&](const Guard &entry) { return entry.id == object.occupantGuard; });
          if (guard == replacement.guards.end() || !guard->bodyHidden || guard->storedIn != object.id) throw std::runtime_error("Ocupación de cuerpo incoherente");
        }
      }
      for (const auto &guard : replacement.guards) if (guard.knownHideout) {
        const auto object = std::ranges::find_if(replacement.objects, [&](const Interactable &entry) { return entry.id == guard.knownHideout; });
        if (object == replacement.objects.end() || WorldGeometry::HideoutType(*object) != HideoutKind::UnderTable) throw std::runtime_error("Escondite conocido inexistente");
      }
      for (const auto &guard : replacement.guards) if (guard.storedIn) {
        const auto object = std::ranges::find_if(replacement.objects, [&](const Interactable &entry) { return entry.id == guard.storedIn; });
        if (!guard.bodyHidden || (guard.state != GuardState::Dead && guard.state != GuardState::Incapacitated) || object == replacement.objects.end() || object->occupantGuard != guard.id) throw std::runtime_error("Cuerpo almacenado inválido");
      }
      for (const auto &agent : replacement.agents) {
        if (agent.cqcHoldingGuard) {
          const auto guard = std::ranges::find_if(replacement.guards, [&](const Guard &entry) { return entry.id == agent.cqcHoldingGuard; });
          if (!agent.alive || agent.incapacitated || agent.hidden || guard == replacement.guards.end() || guard->heldBy != agent.id || guard->cqcState == CqcState::None ||
              guard->state == GuardState::Dead || guard->state == GuardState::Incapacitated || (guard->position - agent.position).length() > GameplayConfig::CqcDistance * GameplayConfig::CqcMaximumStretch) throw std::runtime_error("Sujeción CQC incoherente");
        }
        if (agent.hideoutId) {
          const auto object = std::ranges::find_if(replacement.objects, [&](const Interactable &entry) { return entry.id == agent.hideoutId; });
          if (!agent.hidden || agent.moving || agent.running || agent.draggingGuard >= 0 || agent.cover.active || object == replacement.objects.end() || object->occupantAgent != agent.id ||
              !WorldGeometry::ObjectFootprint(*object).contains(agent.position) || (WorldGeometry::HideoutType(*object) == HideoutKind::UnderTable && agent.stance != Stance::Prone)) throw std::runtime_error("Agente oculto inválido");
        }
        const auto &cover = agent.cover;
        if (!cover.active) {
          if (cover.peekSide != PeekSide::None || cover.canPeekLeft || cover.canPeekRight || cover.obstacle != -1 || cover.object != 0) throw std::runtime_error("Cobertura inactiva con acciones o referencias activas");
          continue;
        }
        const bool horizontal = std::abs(cover.normal.x) > 0.999f && std::abs(cover.normal.y) < 0.001f;
        const bool vertical = std::abs(cover.normal.y) > 0.999f && std::abs(cover.normal.x) < 0.001f;
        if ((!horizontal && !vertical) || cover.obstacle < -1) throw std::runtime_error("Normal de cobertura no axial");
        sf::FloatRect surface;
        if (cover.obstacle >= 0) {
          const auto index = static_cast<std::size_t>(cover.obstacle);
          if (cover.object != 0 || index >= replacement.obstacles.size() || !WorldGeometry::SupportsWallCover(replacement.obstacles[index])) throw std::runtime_error("Referencia de pared de cobertura inválida");
          surface = replacement.obstacles[index].bounds;
        } else {
          const auto object = std::ranges::find_if(replacement.objects, [&](const Interactable &entry) { return entry.id == cover.object; });
          if (cover.object == 0 || object == replacement.objects.end() || !WorldGeometry::SupportsWallCover(*object)) throw std::runtime_error("Referencia de objeto de cobertura inválida");
          surface = WorldGeometry::ObjectFootprint(*object);
        }
        if (surface != cover.surface) throw std::runtime_error("La huella de cobertura contradice su superficie real");
        const auto minimum = surface.position, maximum = surface.position + surface.size;
        const float radius = GameplayConfig::AgentRadius + SpatialConfig::CollisionSkin;
        const float expected = horizontal ? (cover.normal.x < 0.f ? minimum.x - radius : maximum.x + radius) : (cover.normal.y < 0.f ? minimum.y - radius : maximum.y + radius);
        const float actual = horizontal ? cover.anchor.x : cover.anchor.y;
        const float along = horizontal ? cover.anchor.y : cover.anchor.x;
        const float lower = horizontal ? minimum.y : minimum.x, upper = horizontal ? maximum.y : maximum.x;
        const float tolerance = SpatialConfig::CoverPositionTolerance;
        if (std::abs(actual - expected) > tolerance || along < lower - tolerance || along > upper + tolerance ||
            std::abs((agent.position - cover.anchor).dot(cover.normal)) > tolerance || WorldGeometry::PositionBlocked(replacement, cover.anchor, GameplayConfig::AgentRadius) ||
            WorldGeometry::PositionBlocked(replacement, agent.position, GameplayConfig::AgentRadius)) throw std::runtime_error("Posición de cobertura fuera de su cara válida");
        const float bodyAlong = horizontal ? agent.position.y : agent.position.x;
        if (bodyAlong < lower - SpatialConfig::CoverPeekClearance - tolerance || bodyAlong > upper + SpatialConfig::CoverPeekClearance + tolerance) throw std::runtime_error("Asomado fuera del extremo permitido");
        sf::Vector2f left, right;
        const bool canLeft = WorldGeometry::CoverPeekPosition(cover, PeekSide::Left, left) && WorldGeometry::ClearMovementSegment(replacement, cover.anchor, left, GameplayConfig::AgentRadius);
        const bool canRight = WorldGeometry::CoverPeekPosition(cover, PeekSide::Right, right) && WorldGeometry::ClearMovementSegment(replacement, cover.anchor, right, GameplayConfig::AgentRadius);
        if ((!replacement.visibility.authoritative && (cover.canPeekLeft != canLeft || cover.canPeekRight != canRight)) ||
            (cover.peekSide == PeekSide::Left && !cover.canPeekLeft) || (cover.peekSide == PeekSide::Right && !cover.canPeekRight)) throw std::runtime_error("Lado de asomado incompatible con la geometría");
      }
      StateExtensions::Read(snapshot, replacement, recipient);
      if (!snapshot.value("visibilityFiltered", false)) for (const auto &guard : replacement.guards) {
        if (guard.bodyTargetKind == BodyTargetKind::Guard && std::ranges::none_of(replacement.guards, [&guard](const Guard &body) { return body.id == guard.bodyTargetId; })) throw std::runtime_error("Guardia investigado inexistente");
        if (guard.bodyTargetKind == BodyTargetKind::Agent && std::ranges::none_of(replacement.agents, [&guard](const Agent &body) { return body.id == guard.bodyTargetId; })) throw std::runtime_error("Agente investigado inexistente");
      }
      world_ = std::move(replacement);
      nextSound_ = world_.sounds.empty() ? 1 : world_.sounds.back().sequence + 1;
      alarmRemaining_ = alarmRemaining;
      securityContactAge_ = contactAge; reinforcementTimer_ = waveTimer; securityLastKnown_ = lastSecurityContact;
      securityHasClue_ = hasSecurityClue; nextGuardId_ = nextGuard;
      securityContactTick_=contactTick; securityAlertChangeTick_=changeTick;
      nextEvent_ = world_.events.empty() ? 1 : world_.events.back().sequence + 1;
      error.clear();
      return true;
    } catch (const std::exception &exception) {
      error = exception.what();
      return false;
    }
  }
}
