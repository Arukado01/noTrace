#include "NoTraceSimulation.h"
#include "Config/NoTraceGameplayConfig.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <stdexcept>

namespace NoTrace {
  namespace {
    float Length(sf::Vector2f value) { return std::hypot(value.x, value.y); }
    sf::Vector2f Unit(sf::Vector2f value) { const float length = Length(value); return length > 0.0001f ? value / length : sf::Vector2f{}; }
    float Distance(sf::Vector2f left, sf::Vector2f right) { return Length(left - right); }
    bool IsBody(const Guard &guard) { return guard.state == GuardState::Dead || guard.state == GuardState::Incapacitated; }
    bool SegmentRect(sf::Vector2f start, sf::Vector2f end, const sf::FloatRect &rect, float *intersection = nullptr) {
      float near = 0.f, far = 1.f;
      const auto direction = end - start;
      for (int axis = 0; axis < 2; ++axis) {
        const float origin = axis == 0 ? start.x : start.y;
        const float delta = axis == 0 ? direction.x : direction.y;
        const float minimum = axis == 0 ? rect.position.x : rect.position.y;
        const float maximum = minimum + (axis == 0 ? rect.size.x : rect.size.y);
        if (std::abs(delta) < 0.0001f) { if (origin < minimum || origin > maximum) return false; }
        else {
          float first = (minimum - origin) / delta, second = (maximum - origin) / delta;
          if (first > second) std::swap(first, second);
          near = std::max(near, first); far = std::min(far, second);
          if (near > far) return false;
        }
      }
      if (far < 0.f || near > 1.f) return false;
      if (intersection) *intersection = std::max(0.f, near);
      return true;
    }
    float RayCircle(sf::Vector2f from, sf::Vector2f direction, sf::Vector2f center, float radius) {
      const auto offset = center - from;
      const float projection = offset.dot(direction);
      const float perpendicularSquared = offset.dot(offset) - projection * projection;
      if (perpendicularSquared > radius * radius) return std::numeric_limits<float>::infinity();
      const float halfChord = std::sqrt(std::max(0.f, radius * radius - perpendicularSquared));
      if (projection + halfChord < 0.f) return std::numeric_limits<float>::infinity();
      return std::max(0.f, projection - halfChord);
    }
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
    for (auto &agent : world_.agents) if (agent.id == id) agent.loadout = loadout;
  }

  void Simulation::BuildBase() {
    world_.phase = GamePhase::Base;
    world_.size = {1000.f, 760.f};
    world_.guards.clear(); world_.obstacles.clear(); world_.objects.clear(); world_.noises.clear(); world_.shots.clear();
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
    world_.message = "CIO: visita operaciones, armería e inteligencia antes de desplegar.";
  }

  void Simulation::Deploy(std::uint32_t seed) {
    if (world_.phase != GamePhase::Base || std::ranges::none_of(world_.agents, [](const Agent &agent) { return agent.connected; })) return;
    world_.phase = GamePhase::Mission;
    world_.seed = seed;
    world_.tick = 0; world_.elapsed = 0.f; world_.alarm = false; world_.alarmCount = 0;
    world_.objectives = 0; world_.secondary = 0; world_.events.clear(); world_.results.clear(); world_.noises.clear(); world_.shots.clear();
    nextEvent_ = 1; alarmRemaining_ = 0.f;
    std::size_t index = 0;
    const auto connected = std::ranges::count_if(world_.agents, [](const Agent &agent) { return agent.connected; });
    for (auto &agent : world_.agents) {
      agent.position = {GameplayConfig::MissionSpawnX, GameplayConfig::MissionSpawnY + static_cast<float>(index) * GameplayConfig::MissionSpawnSpacing};
      agent.facing = {1.f, 0.f}; agent.stance = Stance::Standing;
      agent.alive = true; agent.incapacitated = false; agent.bodyReported = false; agent.hidden = false; agent.extracted = false; agent.moving = false;
      agent.health = GameplayConfig::MaxHealth; agent.stamina = GameplayConfig::MaxStamina; agent.incapacityRemaining = 0.f; agent.running = false;
      agent.animationTime = 0.f; agent.actionCooldown = 0.f; agent.footstepTime = 0.f; agent.draggingGuard = -1;
      agent.privateComplete = false;
      agent.privateOrder = index % 2 == 0 ? PrivateOrder::CopyArchive : PrivateOrder::RecoverDevice;
      if (GetContract(world_.contract).traitor && connected > 1 && agent.connected && index == seed % static_cast<std::size_t>(connected)) agent.privateOrder = PrivateOrder::Traitor;
      if (agent.connected) ++index;
    }
    BuildMission(seed);
    world_.message = GetContract(world_.contract).briefing;
  }

  void Simulation::BuildMission(std::uint32_t seed) {
    world_.obstacles.clear(); world_.guards.clear(); world_.objects.clear();
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
  }

  void Simulation::ReturnToBase() {
    for (std::size_t index = 0; index < world_.agents.size(); ++index) {
      auto &agent = world_.agents[index];
      agent.position = {130.f + static_cast<float>(index) * 35.f, 150.f};
      agent.alive = true; agent.incapacitated = false; agent.bodyReported = false; agent.extracted = false; agent.hidden = false; agent.stance = Stance::Standing;
      agent.health = GameplayConfig::MaxHealth; agent.stamina = GameplayConfig::MaxStamina; agent.incapacityRemaining = 0.f; agent.running = false; agent.moving = false;
      agent.privateOrder = PrivateOrder::None; agent.privateComplete = false; agent.draggingGuard = -1;
    }
    world_.elapsed = 0.f; world_.alarm = false; world_.noises.clear();
    BuildBase();
  }

  bool Simulation::Blocked(sf::Vector2f position, float radius) const {
    if (position.x < radius || position.y < radius || position.x > world_.size.x - radius || position.y > world_.size.y - radius) return true;
    for (const auto &obstacle : world_.obstacles) if (obstacle.solid) {
      const sf::FloatRect expanded{obstacle.bounds.position - sf::Vector2f{radius, radius}, obstacle.bounds.size + sf::Vector2f{radius * 2.f, radius * 2.f}};
      if (expanded.contains(position)) return true;
    }
    for (const auto &object : world_.objects) if (object.kind == ObjectKind::Door && !object.open && sf::FloatRect{{object.position.x - 12.f - radius, object.position.y - 30.f - radius}, {24.f + radius * 2.f, 60.f + radius * 2.f}}.contains(position)) return true;
    return false;
  }

  void Simulation::Move(sf::Vector2f &position, sf::Vector2f displacement, float radius) const {
    const int steps = std::max(1, static_cast<int>(std::ceil(Length(displacement) / (radius * 0.5f))));
    const auto increment = displacement / static_cast<float>(steps);
    for (int step = 0; step < steps; ++step) {
      const auto horizontal = position + sf::Vector2f{increment.x, 0.f};
      if (!Blocked(horizontal, radius)) position = horizontal;
      const auto vertical = position + sf::Vector2f{0.f, increment.y};
      if (!Blocked(vertical, radius)) position = vertical;
    }
  }

  void Simulation::Navigate(Guard &guard, sf::Vector2f target, float speed, float dt) {
    using namespace GameplayConfig;
    const auto rayBlocked = [this, &guard, target] {
      for (const auto &obstacle : world_.obstacles) if (obstacle.solid && SegmentRect(guard.position, target, {obstacle.bounds.position - sf::Vector2f{AgentRadius, AgentRadius}, obstacle.bounds.size + sf::Vector2f{AgentRadius * 2.f, AgentRadius * 2.f}})) return true;
      for (const auto &object : world_.objects) if (object.kind == ObjectKind::Door && !object.open && SegmentRect(guard.position, target, {{object.position.x - 12.f - AgentRadius, object.position.y - 30.f - AgentRadius}, {24.f + AgentRadius * 2.f, 60.f + AgentRadius * 2.f}})) return true;
      return false;
    };
    sf::Vector2f destination = target;
    if (rayBlocked()) {
      guard.navigationTimer -= dt;
      if (guard.navigationTimer <= 0.f || Distance(guard.navigationTarget, target) > NavigationCellSize) {
        guard.navigation.clear(); guard.navigationTarget = target; guard.navigationTimer = NavigationRefresh;
        const int columns = static_cast<int>(std::ceil(world_.size.x / NavigationCellSize));
        const int rows = static_cast<int>(std::ceil(world_.size.y / NavigationCellSize));
        if (columns <= 0 || rows <= 0 || columns * rows > 20000) return;
        const int startX = std::clamp(static_cast<int>(guard.position.x / NavigationCellSize), 0, columns - 1);
        const int startY = std::clamp(static_cast<int>(guard.position.y / NavigationCellSize), 0, rows - 1);
        const int start = startY * columns + startX;
        std::vector<int> previous(static_cast<std::size_t>(columns * rows), -1);
        previous[start] = start;
        std::queue<int> queue; queue.push(start);
        int finish = -1;
        while (!queue.empty()) {
          const int current = queue.front(); queue.pop();
          const int x = current % columns, y = current / columns;
          const sf::Vector2f point{(x + 0.5f) * NavigationCellSize, (y + 0.5f) * NavigationCellSize};
          if (Distance(point, target) <= NavigationCellSize) { finish = current; break; }
          for (const auto [dx, dy] : std::array<std::pair<int, int>, 4>{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}}) {
            const int nx = x + dx, ny = y + dy;
            if (nx < 0 || ny < 0 || nx >= columns || ny >= rows) continue;
            const int next = ny * columns + nx;
            if (previous[next] >= 0 || Blocked({(nx + 0.5f) * NavigationCellSize, (ny + 0.5f) * NavigationCellSize}, AgentRadius)) continue;
            previous[next] = current; queue.push(next);
          }
        }
        for (int current = finish; current >= 0 && current != start; current = previous[current]) guard.navigation.push_back({(current % columns + 0.5f) * NavigationCellSize, (current / columns + 0.5f) * NavigationCellSize});
        std::ranges::reverse(guard.navigation);
      }
      while (!guard.navigation.empty() && Distance(guard.position, guard.navigation.front()) <= InvestigationStopDistance) guard.navigation.erase(guard.navigation.begin());
      if (guard.navigation.empty()) return;
      destination = guard.navigation.front();
    } else guard.navigation.clear();
    const auto offset = destination - guard.position;
    if (Length(offset) <= 0.01f) return;
    guard.facing = Unit(offset);
    Move(guard.position, guard.facing * std::min(Length(offset), speed * dt), AgentRadius);
  }

  bool Simulation::Occluded(sf::Vector2f from, sf::Vector2f to, Stance stance) const {
    for (const auto &obstacle : world_.obstacles) {
      if ((obstacle.cover == CoverKind::High || (obstacle.cover == CoverKind::Low && stance != Stance::Standing)) && SegmentRect(from, to, obstacle.bounds)) return true;
    }
    for (const auto &object : world_.objects) if (object.kind == ObjectKind::Door && !object.open && SegmentRect(from, to, {{object.position.x - 12.f, object.position.y - 30.f}, {24.f, 60.f}})) return true;
    return false;
  }

  bool Simulation::CanSee(sf::Vector2f from, sf::Vector2f facing, const Agent &agent, float range) const {
    if (!agent.alive || agent.incapacitated || !agent.connected || agent.hidden || agent.extracted) return false;
    const auto delta = agent.position - from;
    const float distance = Length(delta);
    const float postureFactor = agent.stance == Stance::Prone ? GameplayConfig::ProneVisionFactor : (agent.stance == Stance::Crouched ? GameplayConfig::CrouchedVisionFactor : 1.f);
    if (distance > range * postureFactor) return false;
    if (distance > GameplayConfig::CloseVisionDistance && Unit(facing).dot(Unit(delta)) < GameplayConfig::VisionCosHalfAngle) return false;
    return !Occluded(from, agent.position, agent.stance);
  }

  void Simulation::Record(EventKind kind, PlayerId actor, std::uint32_t target) {
    world_.events.push_back({nextEvent_++, world_.tick, actor, kind, target});
  }

  void Simulation::EmitNoise(sf::Vector2f position, float radius, PlayerId actor, std::uint32_t guardId) {
    world_.noises.push_back({position, radius, GameplayConfig::NoiseDuration, actor, guardId});
    Record(EventKind::Noise, actor);
  }

  void Simulation::Detect(Agent &agent, std::uint32_t observer) {
    Record(EventKind::ConfirmedDetection, agent.id, observer);
    if (!world_.alarm) {
      world_.alarm = true; ++world_.alarmCount;
      Record(EventKind::AgentAlarm, agent.id, observer);
    }
    alarmRemaining_ = GameplayConfig::AlarmDuration;
    world_.message = "DETECCIÓN CONFIRMADA: rompe la línea de visión. Maestro ya no es elegible.";
  }

  void Simulation::Step(float deltaTime, const std::unordered_map<PlayerId, ActionCommand> &commands) {
    if (world_.phase == GamePhase::Results || !std::isfinite(deltaTime) || deltaTime <= 0.f) return;
    const float dt = std::min(deltaTime, GameplayConfig::MaximumStep);
    ++world_.tick;
    if (world_.phase == GamePhase::Mission) world_.elapsed += dt;
    for (auto &shot : world_.shots) shot.remaining -= dt;
    std::erase_if(world_.shots, [](const ShotTrace &shot) { return shot.remaining <= 0.f; });
    for (auto &agent : world_.agents) {
      if (!agent.connected || !agent.alive || agent.extracted) continue;
      if (agent.incapacitated) {
        agent.incapacityRemaining = std::max(0.f, agent.incapacityRemaining - dt);
        if (agent.incapacityRemaining <= 0.f) Revive(agent, agent.id);
        continue;
      }
      const auto found = commands.find(agent.id);
      UpdateAgent(agent, found == commands.end() ? ActionCommand{} : found->second, dt);
    }
    if (world_.phase == GamePhase::Mission) {
      UpdateGuards(dt); UpdateObjects(dt);
      for (auto &noise : world_.noises) noise.remaining -= dt;
      std::erase_if(world_.noises, [](const NoiseEvent &noise) { return noise.remaining <= 0.f; });
      alarmRemaining_ = std::max(0.f, alarmRemaining_ - dt);
      if (alarmRemaining_ == 0.f) world_.alarm = false;
      CheckResults();
    }
  }

  void Simulation::UpdateAgent(Agent &agent, const ActionCommand &command, float dt) {
    using namespace GameplayConfig;
    agent.actionCooldown = std::max(0.f, agent.actionCooldown - dt);
    if (command.crouch) { agent.stance = agent.stance == Stance::Crouched ? Stance::Standing : Stance::Crouched; agent.hidden = false; }
    if (command.prone) { agent.stance = agent.stance == Stance::Prone ? Stance::Standing : Stance::Prone; agent.hidden = false; }
    sf::Vector2f direction{std::isfinite(command.moveX) ? std::clamp(command.moveX, -1.f, 1.f) : 0.f, std::isfinite(command.moveY) ? std::clamp(command.moveY, -1.f, 1.f) : 0.f};
    if (Length(direction) > 1.f) direction = Unit(direction);
    agent.running = command.run && Length(direction) > 0.f && agent.stance == Stance::Standing && agent.draggingGuard < 0 && agent.stamina >= MinimumRunningStamina;
    float speed = agent.stance == Stance::Prone ? CrawlSpeed : (agent.stance == Stance::Crouched ? CrouchSpeed : (agent.running ? RunSpeed : WalkSpeed));
    if (agent.draggingGuard >= 0) speed = DragSpeed;
    const auto previous = agent.position;
    Move(agent.position, direction * speed * dt, AgentRadius);
    agent.moving = Distance(previous, agent.position) > 0.01f;
    if (agent.running && agent.moving) agent.stamina = std::max(0.f, agent.stamina - RunningStaminaPerSecond * dt);
    else agent.stamina = std::min(MaxStamina, agent.stamina + StaminaRecoveryPerSecond * dt);
    if (agent.moving) {
      agent.hidden = false; agent.facing = Unit(direction); agent.animationTime += dt * (agent.running ? 1.6f : 1.f);
      agent.footstepTime += dt;
      if (world_.phase == GamePhase::Mission && agent.footstepTime >= FootstepInterval && agent.stance == Stance::Standing) {
        EmitNoise(agent.position, agent.running ? RunningNoiseRadius : WalkingNoiseRadius, agent.id); agent.footstepTime = 0.f;
      }
    }
    const sf::Vector2f aim{command.aimX, command.aimY};
    if (std::isfinite(aim.x) && std::isfinite(aim.y) && Length(aim) > 0.1f) agent.facing = Unit(aim);
    if (agent.draggingGuard >= 0) for (auto &guard : world_.guards) if (static_cast<int>(guard.id) == agent.draggingGuard) guard.position = agent.position - agent.facing * BodyDragOffset;
    if (command.interact || command.tool) Interact(agent, command.tool);
    if (world_.phase != GamePhase::Mission) return;
    if (command.dragBody) {
      if (agent.draggingGuard >= 0) agent.draggingGuard = -1;
      else for (auto &guard : world_.guards) if (IsBody(guard) && !guard.bodyHidden && Distance(agent.position, guard.position) < InteractionDistance && std::ranges::none_of(world_.agents, [&guard](const Agent &other) { return other.draggingGuard == static_cast<int>(guard.id); })) {
        agent.draggingGuard = static_cast<int>(guard.id); break;
      }
    }
    if (agent.actionCooldown > 0.f) return;
    if (command.cqc) { Cqc(agent); agent.actionCooldown = CqcCooldown; }
    else if (command.fireLethal || command.fireNonLethal) { if (Fire(agent, command.fireLethal)) agent.actionCooldown = WeaponCooldown; }
    else if (command.throwDecoy && agent.loadout.decoys > 0) {
      --agent.loadout.decoys;
      auto target = agent.position + agent.facing * DecoyDistance;
      target.x = std::clamp(target.x, 0.f, world_.size.x); target.y = std::clamp(target.y, 0.f, world_.size.y);
      EmitNoise(target, DecoyNoiseRadius, agent.id); agent.actionCooldown = WeaponCooldown;
      world_.message = "Señuelo: las patrullas investigarán el origen del ruido.";
    } else if (command.knock) {
      const auto nearby = [&agent](const sf::FloatRect &bounds) {
        const sf::Vector2f closest{std::clamp(agent.position.x, bounds.position.x, bounds.position.x + bounds.size.x), std::clamp(agent.position.y, bounds.position.y, bounds.position.y + bounds.size.y)};
        return Distance(agent.position, closest) <= KnockDistance;
      };
      const bool wall = std::ranges::any_of(world_.obstacles, [&nearby](const Obstacle &obstacle) { return obstacle.solid && nearby(obstacle.bounds); });
      const bool door = std::ranges::any_of(world_.objects, [&nearby](const Interactable &object) { return object.kind == ObjectKind::Door && !object.open && nearby({{object.position.x - 12.f, object.position.y - 30.f}, {24.f, 60.f}}); });
      if (wall || door) { EmitNoise(agent.position, KnockNoiseRadius, agent.id); agent.actionCooldown = WeaponCooldown; world_.message = "Golpe en una superficie: las patrullas cercanas investigarán."; }
      else world_.message = "Acércate a una pared, cobertura sólida o puerta cerrada para golpearla.";
    }
  }

  void Simulation::Interact(Agent &agent, bool tool) {
    using namespace GameplayConfig;
    if (world_.phase == GamePhase::Mission && !tool) for (auto &other : world_.agents) {
      if (other.id != agent.id && other.connected && other.alive && other.incapacitated && Distance(agent.position, other.position) < InteractionDistance) {
        if (Occluded(agent.position, other.position, Stance::Prone)) continue;
        Revive(other, agent.id);
        world_.message = "Compañero reanimado.";
        return;
      }
    }
    Interactable *nearest = nullptr;
    float distance = InteractionDistance;
    for (auto &object : world_.objects) {
      const float candidate = Distance(agent.position, object.position);
      if (candidate < distance) { nearest = &object; distance = candidate; }
    }
    if (!nearest) { world_.message = "Acércate a una estación u objeto para interactuar."; return; }
    auto &object = *nearest;
    switch (object.kind) {
    case ObjectKind::Operations:
      if (agent.id == HostPlayerId) SetContract(static_cast<ContractId>((static_cast<unsigned int>(world_.contract) + 1) % Contracts().size()));
      else world_.message = "El anfitrión selecciona el contrato en operaciones.";
      break;
    case ObjectKind::Armory:
      agent.loadout = agent.loadout.lethalAmmo > 0 ? Loadout{0, 6, 5, 4} : Loadout{};
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
      if (object.active && tool && agent.loadout.tools > 0) { --agent.loadout.tools; object.active = false; Record(EventKind::CameraDisabled, agent.id, object.id); world_.message = "Cámara desactivada."; }
      else world_.message = "Usa una herramienta cerca de la cámara.";
      break;
    case ObjectKind::AlarmPanel:
      if (object.active && tool && agent.loadout.tools > 0) {
        --agent.loadout.tools; object.active = false;
        for (auto &camera : world_.objects) if (camera.kind == ObjectKind::Camera) camera.active = false;
        Record(EventKind::Sabotage, agent.id, object.id); world_.message = "Central saboteada: cámaras fuera de servicio.";
      } else world_.message = "Usa una herramienta para sabotear la central.";
      break;
    case ObjectKind::Hideout:
      if (agent.draggingGuard >= 0) {
        for (auto &guard : world_.guards) if (static_cast<int>(guard.id) == agent.draggingGuard) { guard.bodyHidden = true; guard.position = object.position; }
        agent.draggingGuard = -1; world_.message = "Cuerpo oculto.";
      } else { agent.hidden = !agent.hidden; world_.message = agent.hidden ? "Oculto. Moverte abandona el escondite." : "Has abandonado el escondite."; }
      break;
    case ObjectKind::CoopSwitch:
      object.user = agent.id;
      object.progress = std::min(CoopHoldDuration, object.progress + CoopInteractionBoost);
      world_.message = "Repetidor: manteneos cerca y pulsa interactuar. Un agente puede operarlo solo más despacio.";
      if (object.completed && agent.privateOrder == PrivateOrder::RecoverDevice && !agent.privateComplete) { agent.privateComplete = true; Record(EventKind::PrivateObjective, agent.id, object.id); }
      break;
    case ObjectKind::Extraction:
      if (world_.objectives < GetContract(world_.contract).requiredObjectives) world_.message = "Faltan objetivos obligatorios. Extracción no autorizada.";
      else if (agent.privateOrder == PrivateOrder::Traitor) world_.message = "Tu orden privada requiere eliminar a los demás agentes sin alarma.";
      else { agent.extracted = true; Record(EventKind::Extracted, agent.id, object.id); world_.message = "Agente extraído; esperando a los supervivientes."; }
      break;
    }
  }

  void Simulation::Cqc(Agent &agent) {
    using namespace GameplayConfig;
    if (agent.stamina < CqcStaminaCost) { world_.message = "Sin resistencia suficiente para CQC; descansa."; return; }
    Guard *target = nullptr;
    float closest = CqcDistance;
    for (auto &guard : world_.guards) if (!IsBody(guard) || guard.heldBy == agent.id) {
      const float distance = Distance(agent.position, guard.position);
      if (distance < closest && (guard.heldBy == 0 || guard.heldBy == agent.id) && !Occluded(agent.position, guard.position, Stance::Standing)) { closest = distance; target = &guard; }
    }
    if (!target) { world_.message = "CQC requiere aproximarte a un guardia a corta distancia."; return; }
    agent.stamina = std::max(0.f, agent.stamina - CqcStaminaCost);
    Record(EventKind::Cqc, agent.id, target->id);
    if (target->cqcState == CqcState::None) { target->cqcState = CqcState::Grabbed; target->heldBy = agent.id; target->stateTime = 0.f; world_.message = "Agarre. Pulsa CQC de nuevo para derribar."; }
    else if (target->cqcState == CqcState::Grabbed) { target->cqcState = CqcState::KnockedDown; target->stateTime = 0.f; world_.message = "Derribo. Pulsa CQC para inmovilizar."; }
    else if (target->cqcState == CqcState::KnockedDown) { target->cqcState = CqcState::Restrained; world_.message = "Inmovilizado. Pulsa CQC para incapacitar."; }
    else { target->state = GuardState::Incapacitated; target->incapacityRemaining = GuardIncapacityDuration; target->heldBy = 0; target->cqcState = CqcState::None; Record(EventKind::GuardIncapacitated, agent.id, target->id); world_.message = "Guardia incapacitado temporalmente. Puedes arrastrar y ocultar el cuerpo."; }
  }

  sf::Vector2f Simulation::TraceWeapon(sf::Vector2f from, sf::Vector2f direction, float range, bool &blocked) const {
    direction = Unit(direction);
    if (direction.x > 0.f) range = std::min(range, (world_.size.x - from.x) / direction.x);
    else if (direction.x < 0.f) range = std::min(range, -from.x / direction.x);
    if (direction.y > 0.f) range = std::min(range, (world_.size.y - from.y) / direction.y);
    else if (direction.y < 0.f) range = std::min(range, -from.y / direction.y);
    const auto end = from + direction * std::max(0.f, range);
    float closest = 1.f;
    blocked = false;
    const auto clip = [&](const sf::FloatRect &bounds) {
      float intersection = 1.f;
      if (SegmentRect(from, end, bounds, &intersection) && intersection < closest) {
        closest = intersection;
        blocked = true;
      }
    };
    for (const auto &obstacle : world_.obstacles) if (obstacle.solid) clip(obstacle.bounds);
    for (const auto &object : world_.objects) if (object.kind == ObjectKind::Door && !object.open) clip({{object.position.x - 12.f, object.position.y - 30.f}, {24.f, 60.f}});
    return from + (end - from) * closest;
  }

  void Simulation::Incapacitate(Agent &agent, PlayerId actor, std::uint32_t source) {
    agent.incapacitated = true; agent.incapacityRemaining = GameplayConfig::IncapacityDuration;
    agent.stamina = 0.f; agent.moving = false; agent.running = false; agent.hidden = false; agent.draggingGuard = -1;
    Record(EventKind::AgentIncapacitated, actor, source);
    world_.message = "Agente inconsciente: un compañero puede reanimarlo o se recuperará tras unos segundos.";
  }

  void Simulation::Revive(Agent &agent, PlayerId actor) {
    agent.incapacitated = false; agent.incapacityRemaining = 0.f; agent.bodyReported = false;
    agent.health = std::max(agent.health, GameplayConfig::RevivedHealth);
    agent.stamina = GameplayConfig::RevivedStamina;
    agent.actionCooldown = GameplayConfig::WeaponCooldown;
    Record(EventKind::AgentRevived, actor, agent.id);
    world_.message = "Agente recuperado.";
  }

  void Simulation::DamageAgent(Agent &agent, float healthDamage, float staminaDamage, PlayerId actor, std::uint32_t source) {
    if (!agent.alive || agent.incapacitated || agent.extracted) return;
    agent.health = std::max(0.f, agent.health - healthDamage);
    agent.stamina = std::max(0.f, agent.stamina - staminaDamage);
    Record(EventKind::AgentDamaged, actor, agent.id);
    if (agent.health <= 0.f) {
      agent.alive = false; agent.incapacitated = false; agent.incapacityRemaining = 0.f;
      agent.moving = false; agent.running = false; agent.hidden = false; agent.draggingGuard = -1;
      Record(EventKind::AgentKilled, actor, agent.id);
      world_.message = "Un agente ha muerto por sus heridas.";
    } else if (agent.stamina <= 0.f) Incapacitate(agent, actor, source);
    else world_.message = "Agente herido: rompe la línea de visión y busca cobertura.";
  }

  bool Simulation::Fire(Agent &agent, bool lethal) {
    using namespace GameplayConfig;
    int &ammo = lethal ? agent.loadout.lethalAmmo : agent.loadout.nonLethalAmmo;
    if (ammo <= 0) { world_.message = "Sin munición del tipo seleccionado."; return false; }
    --ammo;
    Record(lethal ? EventKind::WeaponLethal : EventKind::WeaponNonLethal, agent.id);
    EmitNoise(agent.position, lethal ? LethalNoiseRadius : NonLethalNoiseRadius, agent.id);
    const auto direction = Unit(agent.facing);
    bool blocked = false;
    auto impact = TraceWeapon(agent.position, direction, WeaponRange, blocked);
    float nearest = Distance(agent.position, impact);
    Guard *guardTarget = nullptr;
    Agent *agentTarget = nullptr;
    for (auto &guard : world_.guards) if (!IsBody(guard)) {
      const float distance = RayCircle(agent.position, direction, guard.position, AgentRadius);
      if (distance < nearest) { nearest = distance; guardTarget = &guard; }
    }
    if (GetContract(world_.contract).traitor) for (auto &other : world_.agents) if (other.id != agent.id && other.alive && other.connected && !other.extracted) {
      const float distance = RayCircle(agent.position, direction, other.position, AgentRadius);
      if (distance < nearest) { nearest = distance; agentTarget = &other; guardTarget = nullptr; }
    }
    const bool hit = guardTarget || agentTarget;
    if (hit) impact = agent.position + direction * nearest;
    world_.shots.push_back({agent.position, impact, ShotTraceDuration, lethal, agent.id, 0, hit, blocked && !hit});
    if (agentTarget) {
      if (lethal) DamageAgent(*agentTarget, MaxHealth, 0.f, agent.id, agentTarget->id);
      else Incapacitate(*agentTarget, agent.id, agentTarget->id);
    }
    else if (guardTarget) {
      guardTarget->state = lethal ? GuardState::Dead : GuardState::Incapacitated;
      guardTarget->incapacityRemaining = lethal ? 0.f : GuardIncapacityDuration;
      guardTarget->heldBy = 0; guardTarget->cqcState = CqcState::None;
      Record(lethal ? EventKind::GuardKilled : EventKind::GuardIncapacitated, agent.id, guardTarget->id);
      world_.message = lethal ? "Guardia eliminado; el cuerpo y el ruido comprometen el sigilo." : "Guardia incapacitado. Oculta el cuerpo.";
    } else world_.message = blocked ? "El disparo impactó contra una cobertura sólida." : "Disparo sin impacto. El uso de armas queda registrado.";
    return true;
  }

  void Simulation::UpdateGuards(float dt) {
    using namespace GameplayConfig;
    for (auto &guard : world_.guards) {
      guard.attackTime = std::max(0.f, guard.attackTime - dt);
      if (guard.state == GuardState::Incapacitated && !guard.bodyHidden && guard.incapacityRemaining > 0.f) {
        guard.incapacityRemaining = std::max(0.f, guard.incapacityRemaining - dt);
        if (guard.incapacityRemaining <= 0.f) {
          guard.state = GuardState::Search; guard.stateTime = 0.f; guard.suspicion = 0.f; guard.lastKnown = guard.position; guard.bodyReported = false;
          for (auto &agent : world_.agents) if (agent.draggingGuard == static_cast<int>(guard.id)) agent.draggingGuard = -1;
        }
      }
      if (IsBody(guard)) continue;
      guard.stateTime += dt;
      guard.attackCooldown = std::max(0.f, guard.attackCooldown - dt);
      if (guard.cqcState != CqcState::None) {
        const auto holder = std::ranges::find_if(world_.agents, [&guard](const Agent &agent) { return agent.id == guard.heldBy && agent.connected && agent.alive && !agent.incapacitated; });
        if (holder == world_.agents.end() || Distance(holder->position, guard.position) > CqcDistance * CqcMaximumStretch || guard.stateTime > CqcEscapeDuration) {
          guard.heldBy = 0; guard.cqcState = CqcState::None; guard.state = GuardState::Search; guard.stateTime = 0.f; guard.lastKnown = guard.position;
        } else continue;
      }
      Agent *seen = nullptr;
      for (auto &agent : world_.agents) if (CanSee(guard.position, guard.facing, agent, world_.alarm ? VisionDistance * AlarmVisionMultiplier : VisionDistance) && (!seen || Distance(guard.position, agent.position) < Distance(guard.position, seen->position))) seen = &agent;
      if (seen) {
        guard.lastKnown = seen->position;
        guard.suspicion += dt / ConfirmationTime;
        if (guard.suspicion >= 1.f) {
          if (guard.state != GuardState::Alert) Detect(*seen, guard.id);
          else alarmRemaining_ = AlarmDuration;
          guard.state = GuardState::Alert; guard.stateTime = 0.f;
          const auto aim = Unit(seen->position - guard.position);
          const float distance = Distance(guard.position, seen->position);
          guard.facing = aim;
          if (distance <= GuardWeaponRange && guard.attackCooldown <= 0.f && (distance <= GuardAttackDistance || guard.ammo > 0)) {
            guard.attackCooldown = GuardAttackInterval; guard.attackTime = AttackAnimationDuration;
            guard.meleeAttack = distance <= GuardAttackDistance;
            bool blocked = false;
            auto impact = TraceWeapon(guard.position, aim, distance, blocked);
            if (guard.meleeAttack) {
              if (!blocked) {
                Record(EventKind::GuardMelee, 0, guard.id);
                DamageAgent(*seen, GuardMeleeDamage, GuardMeleeStaminaDamage, 0, seen->id);
              }
            } else {
              --guard.ammo;
              Agent *hit = nullptr;
              float closest = Distance(guard.position, impact);
              for (auto &agent : world_.agents) if (agent.connected && agent.alive && !agent.incapacitated && !agent.extracted) {
                const float candidate = RayCircle(guard.position, aim, agent.position, AgentRadius);
                if (candidate < closest) { closest = candidate; hit = &agent; }
              }
              if (hit) impact = guard.position + aim * closest;
              world_.shots.push_back({guard.position, impact, ShotTraceDuration, true, 0, guard.id, hit != nullptr, blocked && !hit});
              Record(EventKind::GuardShot, 0, guard.id);
              EmitNoise(guard.position, LethalNoiseRadius, 0, guard.id);
              if (hit) DamageAgent(*hit, GuardShotDamage, GuardShotStaminaDamage, 0, hit->id);
            }
          }
        } else { guard.state = GuardState::Suspicion; guard.stateTime = 0.f; }
      } else {
        guard.suspicion = std::max(0.f, guard.suspicion - dt * SuspicionDecay);
        if (guard.state == GuardState::Alert || (guard.state == GuardState::Suspicion && guard.suspicion == 0.f)) { guard.state = GuardState::Search; guard.stateTime = 0.f; }
        if (guard.state != GuardState::Alert) for (const auto &noise : world_.noises) if (noise.guardId != guard.id && Distance(guard.position, noise.position) <= noise.radius) {
          guard.lastKnown = noise.position; guard.state = GuardState::Investigate; guard.stateTime = 0.f;
          break;
        }
        const auto seesBody = [this, &guard](sf::Vector2f position) {
          const auto offset = position - guard.position;
          const float distance = Length(offset);
          return distance < BodyVisionDistance && (distance < CloseVisionDistance || guard.facing.dot(Unit(offset)) >= VisionCosHalfAngle) && !Occluded(guard.position, position, Stance::Standing);
        };
        for (auto &body : world_.guards) if (body.id != guard.id && IsBody(body) && !body.bodyHidden && !body.bodyReported && seesBody(body.position)) {
          body.bodyReported = true; guard.lastKnown = body.position; guard.state = GuardState::Search; guard.stateTime = 0.f;
          Record(EventKind::BodyFound, 0, body.id); world_.message = "Una patrulla encontró un cuerpo e investiga la zona.";
        }
        for (auto &body : world_.agents) if ((!body.alive || body.incapacitated) && body.connected && !body.bodyReported && seesBody(body.position)) {
          body.bodyReported = true; guard.lastKnown = body.position; guard.state = GuardState::Search; guard.stateTime = 0.f;
          if (!world_.alarm) { world_.alarm = true; ++world_.alarmCount; alarmRemaining_ = AlarmDuration; }
          Record(EventKind::BodyFound, 0, body.id); world_.message = "Una patrulla encontró a un agente caído: alarma de seguridad.";
        }
      }
      sf::Vector2f destination = guard.position;
      if (guard.state == GuardState::Patrol && !guard.patrol.empty()) {
        destination = guard.patrol[guard.waypoint];
        if (Distance(guard.position, destination) < PatrolWaypointDistance) guard.waypoint = (guard.waypoint + 1) % guard.patrol.size();
      } else if (guard.state == GuardState::Investigate || guard.state == GuardState::Alert || guard.state == GuardState::Search) destination = guard.lastKnown;
      if (seen && guard.state == GuardState::Alert && Distance(guard.position, seen->position) <= GuardAttackDistance * 0.8f) destination = guard.position;
      const auto offset = destination - guard.position;
      if (Length(offset) > InvestigationStopDistance) {
        Navigate(guard, destination, guard.state == GuardState::Alert ? GuardAlertSpeed : GuardSpeed, dt);
      } else if (guard.state == GuardState::Search) {
        const float angle = dt * SearchAngularSpeed;
        guard.facing = {guard.facing.x * std::cos(angle) - guard.facing.y * std::sin(angle), guard.facing.x * std::sin(angle) + guard.facing.y * std::cos(angle)};
      }
      if ((guard.state == GuardState::Investigate && guard.stateTime > InvestigationDuration) || (guard.state == GuardState::Search && guard.stateTime > SearchDuration)) { guard.state = GuardState::Patrol; guard.stateTime = 0.f; }
    }
  }

  void Simulation::UpdateObjects(float dt) {
    using namespace GameplayConfig;
    for (auto &object : world_.objects) {
      if (object.kind == ObjectKind::Camera && object.active) {
        Agent *seen = nullptr;
        for (auto &agent : world_.agents) if (CanSee(object.position, object.facing, agent, CameraDistance)) { seen = &agent; break; }
        if (seen) {
          object.progress += dt;
          if (object.progress >= ConfirmationTime) {
            if (object.progress - dt < ConfirmationTime) Detect(*seen, object.id + 1000);
            else alarmRemaining_ = AlarmDuration;
          }
        } else object.progress = std::max(0.f, object.progress - dt);
      } else if (object.kind == ObjectKind::CoopSwitch && !object.completed && object.progress > 0.f) {
        const auto active = std::ranges::count_if(world_.agents, [](const Agent &agent) { return agent.connected && agent.alive && !agent.extracted; });
        const auto nearby = std::ranges::count_if(world_.agents, [&object](const Agent &agent) { return agent.connected && agent.alive && !agent.extracted && Distance(agent.position, object.position) < InteractionDistance; });
        if (nearby >= std::min<std::ptrdiff_t>(2, active)) object.progress += dt * (nearby > 1 ? CooperativeSwitchRate : SoloSwitchRate);
        else object.progress = std::max(0.f, object.progress - dt);
        if (object.progress >= CoopHoldDuration) {
          object.completed = true; object.active = false;
          if (world_.contract == ContractId::SilentRelay) { ++world_.objectives; Record(EventKind::Objective, object.user, object.id); }
          else { ++world_.secondary; Record(EventKind::Secondary, object.user, object.id); }
          Record(EventKind::Sabotage, object.user, object.id);
          for (auto &agent : world_.agents) if (agent.privateOrder == PrivateOrder::RecoverDevice && !agent.privateComplete && Distance(agent.position, object.position) < InteractionDistance) { agent.privateComplete = true; Record(EventKind::PrivateObjective, agent.id, object.id); }
          world_.message = "Repetidor saboteado. El dispositivo y el objetivo están asegurados.";
        }
      }
    }
  }

  void Simulation::CheckResults() {
    if (world_.phase != GamePhase::Mission) return;
    const auto &contract = GetContract(world_.contract);
    if (world_.events.size() >= GameplayConfig::MaximumEvents) { Finish(false, "Registro operacional lleno; operación cancelada."); return; }
    if (world_.elapsed >= contract.timeLimit) { Finish(false, "Se agotó la ventana de extracción."); return; }
    if ((contract.noAlarm && world_.alarmCount > 0) || world_.alarmCount >= contract.maxAlarms) { Finish(false, "Contrato cancelado por alarmas confirmadas."); return; }
    const auto traitor = std::ranges::find_if(world_.agents, [](const Agent &agent) { return agent.privateOrder == PrivateOrder::Traitor && agent.connected; });
    if (traitor != world_.agents.end() && traitor->alive && world_.alarmCount == 0) {
      const bool anyOther = std::ranges::any_of(world_.agents, [&traitor](const Agent &agent) { return agent.connected && agent.id != traitor->id; });
      const bool allDead = std::ranges::none_of(world_.agents, [&traitor](const Agent &agent) { return agent.connected && agent.id != traitor->id && agent.alive; });
      if (anyOther && allDead) { Finish(false, "El traidor eliminó a los demás agentes sin activar alarmas.", true); return; }
    }
    const auto operational = [](const Agent &agent) { return agent.connected && agent.alive && agent.privateOrder != PrivateOrder::Traitor; };
    if (std::ranges::none_of(world_.agents, operational)) { Finish(false, "No quedan agentes del equipo operativos."); return; }
    if (world_.objectives >= contract.requiredObjectives && std::ranges::all_of(world_.agents, [&operational](const Agent &agent) { return !operational(agent) || agent.extracted; })) Finish(true, "Contrato cumplido y equipo extraído.");
  }

  void Simulation::Finish(bool success, const std::string &reason, bool traitorVictory) {
    world_.phase = GamePhase::Results;
    world_.message = reason;
    for (const auto &agent : world_.agents) {
      auto result = EvaluateMission(GetContract(world_.contract), world_, agent.id, success && agent.privateOrder != PrivateOrder::Traitor, reason);
      result.traitorVictory = traitorVictory && agent.privateOrder == PrivateOrder::Traitor;
      if (result.traitorVictory) { result.success = true; result.score = 1000; result.rank = Rank::S; }
      world_.results.emplace(agent.id, result);
    }
  }

  nlohmann::json Simulation::SnapshotFor(PlayerId player) const {
    using nlohmann::json;
    json snapshot{{"version", GameplayConfig::ProtocolVersion}, {"player", player}, {"phase", static_cast<int>(world_.phase)},
      {"contract", static_cast<int>(world_.contract)}, {"seed", world_.seed}, {"tick", world_.tick}, {"size", VectorJson(world_.size)},
      {"elapsed", world_.elapsed}, {"alarm", world_.alarm}, {"alarmRemaining", alarmRemaining_}, {"alarmCount", world_.alarmCount}, {"objectives", world_.objectives},
      {"secondary", world_.secondary}, {"message", world_.message}};
    snapshot["agents"] = json::array();
    for (const auto &agent : world_.agents) {
      json value{{"id", agent.id}, {"name", agent.name}, {"position", VectorJson(agent.position)}, {"facing", VectorJson(agent.facing)},
        {"stance", static_cast<int>(agent.stance)}, {"alive", agent.alive}, {"incapacitated", agent.incapacitated}, {"bodyReported", agent.bodyReported}, {"connected", agent.connected}, {"hidden", agent.hidden},
        {"moving", agent.moving}, {"running", agent.running}, {"extracted", agent.extracted}, {"animationTime", agent.animationTime},
        {"health", agent.health}, {"stamina", agent.stamina}, {"incapacityRemaining", agent.incapacityRemaining}, {"actionCooldown", agent.actionCooldown}, {"footstepTime", agent.footstepTime},
        {"draggingGuard", agent.draggingGuard}, {"loadout", {agent.loadout.lethalAmmo, agent.loadout.nonLethalAmmo, agent.loadout.decoys, agent.loadout.tools}}};
      if (agent.id == player) { value["privateOrder"] = static_cast<int>(agent.privateOrder); value["privateComplete"] = agent.privateComplete; }
      snapshot["agents"].push_back(std::move(value));
    }
    snapshot["guards"] = json::array();
    for (const auto &guard : world_.guards) {
      json patrol = json::array();
      for (auto point : guard.patrol) patrol.push_back(VectorJson(point));
      snapshot["guards"].push_back({{"id", guard.id}, {"position", VectorJson(guard.position)}, {"facing", VectorJson(guard.facing)},
        {"state", static_cast<int>(guard.state)}, {"cqcState", static_cast<int>(guard.cqcState)}, {"patrol", patrol}, {"waypoint", guard.waypoint},
        {"lastKnown", VectorJson(guard.lastKnown)}, {"suspicion", guard.suspicion}, {"bodyHidden", guard.bodyHidden}, {"bodyReported", guard.bodyReported}, {"heldBy", guard.heldBy},
        {"stateTime", guard.stateTime}, {"attackCooldown", guard.attackCooldown}, {"attackTime", guard.attackTime}, {"meleeAttack", guard.meleeAttack}, {"ammo", guard.ammo}, {"incapacityRemaining", guard.incapacityRemaining}});
    }
    snapshot["obstacles"] = json::array();
    for (const auto &obstacle : world_.obstacles) snapshot["obstacles"].push_back({{"position", VectorJson(obstacle.bounds.position)},
      {"size", VectorJson(obstacle.bounds.size)}, {"cover", static_cast<int>(obstacle.cover)}, {"solid", obstacle.solid}, {"name", obstacle.name}});
    snapshot["objects"] = json::array();
    for (const auto &object : world_.objects) snapshot["objects"].push_back({{"id", object.id}, {"kind", static_cast<int>(object.kind)},
      {"position", VectorJson(object.position)}, {"name", object.name}, {"active", object.active}, {"completed", object.completed},
      {"open", object.open}, {"progress", object.progress}, {"facing", VectorJson(object.facing)}});
    snapshot["noises"] = json::array();
    for (const auto &noise : world_.noises) snapshot["noises"].push_back({{"position", VectorJson(noise.position)}, {"radius", noise.radius}, {"remaining", noise.remaining}, {"actor", noise.actor}, {"guardId", noise.guardId}});
    snapshot["shots"] = json::array();
    for (const auto &shot : world_.shots) snapshot["shots"].push_back({{"from", VectorJson(shot.from)}, {"to", VectorJson(shot.to)}, {"remaining", shot.remaining}, {"lethal", shot.lethal}, {"actor", shot.actor}, {"guardId", shot.guardId}, {"hit", shot.hit}, {"blocked", shot.blocked}});
    snapshot["events"] = json::array();
    const auto first = world_.events.size() > 64 ? world_.events.size() - 64 : 0;
    // El anfitrión conserva todo el registro; los clientes reciben solo actividad reciente y nunca órdenes ajenas.
    for (std::size_t index = first; index < world_.events.size(); ++index) {
      const auto &event = world_.events[index];
      if (event.kind == EventKind::PrivateObjective && event.actor != player) continue;
      snapshot["events"].push_back({{"sequence", event.sequence}, {"tick", event.tick}, {"actor", event.actor}, {"kind", static_cast<int>(event.kind)}, {"target", event.target}});
    }
    snapshot["results"] = json::array();
    for (const auto &[id, result] : world_.results) snapshot["results"].push_back({{"id", id}, {"success", result.success}, {"traitorVictory", result.traitorVictory},
      {"maestroEligible", result.maestroEligible}, {"score", result.score}, {"rank", static_cast<int>(result.rank)}, {"reason", result.reason}});
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
      for (const auto &value : array("agents", GameplayConfig::MaximumPlayers)) {
        Agent agent;
        agent.id = value.at("id").get<PlayerId>();
        if (agent.id == 0 || std::ranges::any_of(replacement.agents, [&agent](const Agent &other) { return other.id == agent.id; })) throw std::runtime_error("Identificador de agente repetido");
        agent.name = text(value.at("name"), GameplayConfig::MaximumNameBytes);
        agent.position = ParseVector(value.at("position")); agent.facing = ParseVector(value.at("facing"));
        agent.stance = ParseEnum<Stance>(value.at("stance"), 2);
        agent.alive = value.at("alive").get<bool>(); agent.incapacitated = value.at("incapacitated").get<bool>(); agent.bodyReported = value.at("bodyReported").get<bool>(); agent.connected = value.at("connected").get<bool>(); agent.hidden = value.at("hidden").get<bool>();
        agent.moving = value.at("moving").get<bool>(); agent.running = value.at("running").get<bool>(); agent.extracted = value.at("extracted").get<bool>();
        agent.animationTime = finite(value.at("animationTime"), 86400.f); agent.draggingGuard = value.at("draggingGuard").get<int>();
        agent.health = finite(value.at("health"), GameplayConfig::MaxHealth); agent.stamina = finite(value.at("stamina"), GameplayConfig::MaxStamina);
        agent.incapacityRemaining = finite(value.at("incapacityRemaining"), GameplayConfig::IncapacityDuration);
        agent.actionCooldown = finite(value.at("actionCooldown"), 60.f); agent.footstepTime = finite(value.at("footstepTime"), 86400.f);
        if ((!agent.alive && (agent.incapacitated || agent.incapacityRemaining > 0.f)) || (agent.alive && agent.health <= 0.f) || (agent.incapacitated != (agent.incapacityRemaining > 0.f))) throw std::runtime_error("Estado vital de agente inválido");
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
        replacement.agents.push_back(agent);
      }
      for (const auto &value : array("guards", 32)) {
        Guard guard;
        guard.id = value.at("id").get<std::uint32_t>();
        if (guard.id == 0 || std::ranges::any_of(replacement.guards, [&guard](const Guard &other) { return other.id == guard.id; })) throw std::runtime_error("Identificador de guardia repetido");
        guard.position = ParseVector(value.at("position")); guard.facing = ParseVector(value.at("facing"));
        guard.state = ParseEnum<GuardState>(value.at("state"), 6); guard.cqcState = ParseEnum<CqcState>(value.at("cqcState"), 3);
        const auto &patrol = value.at("patrol");
        if (!patrol.is_array() || patrol.size() > 32) throw std::runtime_error("Patrulla inválida");
        for (const auto &point : patrol) guard.patrol.push_back(ParseVector(point));
        guard.waypoint = value.at("waypoint").get<std::size_t>();
        if (!guard.patrol.empty() && guard.waypoint >= guard.patrol.size()) throw std::runtime_error("Índice de patrulla inválido");
        guard.lastKnown = ParseVector(value.at("lastKnown")); guard.suspicion = finite(value.at("suspicion"), 10000.f);
        guard.bodyHidden = value.at("bodyHidden").get<bool>(); guard.bodyReported = value.at("bodyReported").get<bool>(); guard.heldBy = value.at("heldBy").get<PlayerId>();
        guard.stateTime = finite(value.at("stateTime"), 86400.f); guard.attackCooldown = finite(value.at("attackCooldown"), 60.f);
        guard.attackTime = finite(value.at("attackTime"), GameplayConfig::AttackAnimationDuration); guard.meleeAttack = value.at("meleeAttack").get<bool>();
        if (!value.at("ammo").is_number_integer()) throw std::runtime_error("Munición de guardia inválida");
        const auto ammo = value.at("ammo").get<std::int64_t>();
        if (ammo < 0 || ammo > GameplayConfig::GuardAmmo) throw std::runtime_error("Munición de guardia inválida");
        guard.ammo = static_cast<int>(ammo);
        guard.incapacityRemaining = finite(value.at("incapacityRemaining"), GameplayConfig::GuardIncapacityDuration);
        replacement.guards.push_back(guard);
      }
      for (const auto &value : array("obstacles", 128)) {
        Obstacle obstacle;
        obstacle.bounds = {ParseVector(value.at("position")), ParseVector(value.at("size"))};
        if (obstacle.bounds.size.x <= 0.f || obstacle.bounds.size.y <= 0.f) throw std::runtime_error("Obstáculo sin dimensiones");
        obstacle.cover = ParseEnum<CoverKind>(value.at("cover"), 3); obstacle.solid = value.at("solid").get<bool>(); obstacle.name = text(value.at("name"), 128);
        replacement.obstacles.push_back(obstacle);
      }
      for (const auto &value : array("objects", 64)) {
        Interactable object;
        object.id = value.at("id").get<std::uint32_t>();
        if (object.id == 0 || std::ranges::any_of(replacement.objects, [&object](const Interactable &other) { return other.id == object.id; })) throw std::runtime_error("Identificador de objeto repetido");
        object.kind = ParseEnum<ObjectKind>(value.at("kind"), 11); object.position = ParseVector(value.at("position")); object.name = text(value.at("name"), 128);
        object.active = value.at("active").get<bool>(); object.completed = value.at("completed").get<bool>(); object.open = value.at("open").get<bool>();
        object.progress = finite(value.at("progress"), 86400.f); object.facing = ParseVector(value.at("facing"));
        replacement.objects.push_back(object);
      }
      for (const auto &value : array("noises", 128)) replacement.noises.push_back({ParseVector(value.at("position")), finite(value.at("radius"), 10000.f), finite(value.at("remaining"), 60.f), value.at("actor").get<PlayerId>(), value.value("guardId", std::uint32_t{0})});
      for (const auto &value : array("shots", 128)) replacement.shots.push_back({ParseVector(value.at("from")), ParseVector(value.at("to")), finite(value.at("remaining"), GameplayConfig::ShotTraceDuration), value.at("lethal").get<bool>(), value.at("actor").get<PlayerId>(), value.at("guardId").get<std::uint32_t>(), value.at("hit").get<bool>(), value.at("blocked").get<bool>()});
      for (const auto &value : array("events", 64)) {
        MissionEvent event{value.at("sequence").get<std::uint64_t>(), value.at("tick").get<std::uint64_t>(), value.at("actor").get<PlayerId>(), ParseEnum<EventKind>(value.at("kind"), 21), value.at("target").get<std::uint32_t>()};
        if (event.kind == EventKind::PrivateObjective && event.actor != recipient) throw std::runtime_error("Evento privado ajeno");
        if (!replacement.events.empty() && event.sequence <= replacement.events.back().sequence) throw std::runtime_error("Secuencia de eventos inválida");
        replacement.events.push_back(event);
      }
      for (const auto &value : array("results", GameplayConfig::MaximumPlayers)) {
        MissionResult result;
        const auto id = value.at("id").get<PlayerId>();
        result.success = value.at("success").get<bool>(); result.traitorVictory = value.at("traitorVictory").get<bool>(); result.maestroEligible = value.at("maestroEligible").get<bool>();
        result.score = value.at("score").get<int>(); result.rank = ParseEnum<Rank>(value.at("rank"), 6); result.reason = text(value.at("reason"), 2048);
        if (replacement.results.contains(id) || result.score < 0 || result.score > 1000000) throw std::runtime_error("Resultado inválido");
        replacement.results.emplace(id, result);
      }
      world_ = std::move(replacement);
      alarmRemaining_ = alarmRemaining;
      nextEvent_ = world_.events.empty() ? 1 : world_.events.back().sequence + 1;
      error.clear();
      return true;
    } catch (const std::exception &exception) {
      error = exception.what();
      return false;
    }
  }
}
