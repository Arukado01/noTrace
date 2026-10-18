#include "NoTraceLanQaPilot.h"
#include "Config/NoTraceNavigationConfig.h"
#include "NoTraceCombatGeometry.h"
#include "NoTraceInteraction.h"
#include "NoTraceNavigation.h"
#include "NoTraceVisibility.h"
#include "NoTraceWorldGeometry.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <queue>
#include <stdexcept>

namespace NoTrace::Qa {
  namespace {
    bool Blocked(const WorldState &world, sf::Vector2f point, float elevation, std::uint32_t support, bool allowCameraContact = false) {
      using namespace MissionPilotConfig;
      if (point.x < GameplayConfig::AgentRadius || point.y < GameplayConfig::AgentRadius || point.x > world.size.x - GameplayConfig::AgentRadius || point.y > world.size.y - GameplayConfig::AgentRadius ||
        WorldGeometry::PositionBlocked(world, point, GameplayConfig::AgentRadius + CollisionMargin, elevation, CombatConfig::CrouchedHeight, support)) return true;
      for (const auto &object : world.objects) {
        if (IsTraining(world)) continue;
        if (std::abs(object.elevation - elevation) > GameplayConfig::InteractionDistance) continue;
        const auto clear = [&] { return !WorldGeometry::RayOccluded(world, {object.position.x, object.position.y, object.elevation + CombatGeometry::HeadCenterHeight(Stance::Standing)},
          {point.x, point.y, elevation + CombatGeometry::HeadCenterHeight(Stance::Crouched)}, object.id); };
        if (object.kind == ObjectKind::Explosive && object.active && object.trap.state != TrapState::Disarmed && object.trap.state != TrapState::Detonated &&
            (point - object.position).length() <= object.trap.triggerRadius + GameplayConfig::AgentRadius && clear()) return true;
        if (allowCameraContact || object.kind != ObjectKind::Camera || !object.active || object.camera.state != CameraState::Operating) continue;
        const auto delta = point - object.position;
        const float distance = delta.length();
        if (distance > object.camera.range * GameplayConfig::CrouchedVisionFactor + DeviceMargin || !clear()) continue;
        if (distance <= GameplayConfig::CloseVisionDistance) return true;
        if (object.camera.sweeping) {
          const float angle = std::remainder(std::atan2(delta.y, delta.x) - object.camera.baseAngle, 6.2831853f);
          const float nearest = std::clamp(angle, object.camera.minAngle, object.camera.maxAngle);
          if (std::cos(angle - nearest) >= object.camera.cosHalfAngle) return true;
        } else if (object.facing.dot(delta) >= object.camera.cosHalfAngle * distance * object.facing.length()) return true;
      }
      return false;
    }

    bool CanInteract(const WorldState &world, const Agent &agent, sf::Vector2f point, float elevation, const Interactable &target) {
      const auto closest = WorldGeometry::ClosestPoint(point, WorldGeometry::ObjectFootprint(target));
      const bool reachable = std::abs(elevation - target.elevation) <= LevelConfig::MaximumStepHeight &&
        (closest - point).length() < GameplayConfig::InteractionDistance * MissionPilotConfig::InteractionFraction &&
        !WorldGeometry::RayOccluded(world, {point.x, point.y, elevation + CombatConfig::StandingWeaponHeight},
          {closest.x, closest.y, target.elevation + CombatConfig::StandingWeaponHeight}, target.id);
      if (!reachable || target.id == 0) return reachable;
      auto probe = agent; probe.position = point; probe.elevation = elevation;
      if (Interaction::FindBody(world, probe)) return false;
      const auto *selected = Interaction::FindObject(world, probe);
      return selected && selected->id == target.id;
    }

    std::vector<sf::Vector2f> PlanRoute(const WorldState &world, const Agent &agent, const Interactable &target, bool pointDestination = false, bool allowCameraContact = false) {
      using namespace MissionPilotConfig;
      const int columns = static_cast<int>(std::ceil(world.size.x / RouteCellSize));
      const int rows = static_cast<int>(std::ceil(world.size.y / RouteCellSize));
      if (columns <= 0 || rows <= 0 || columns > NavigationConfig::MaximumCells / rows) return {};
      const auto *currentSupport = WorldGeometry::FindSurface(world, agent.supportId);
      const WalkSurface *nextSupport = nullptr;
      bool intermediate = false;
      sf::Vector2f intermediatePoint;
      float reach = RouteCellSize;
      if (currentSupport && agent.supportId != target.supportId) {
        auto route = Navigation::Route(world, agent.supportId, target.supportId);
        if (pointDestination && std::ranges::any_of(route, [](const Navigation::RouteStep &step) { return step.elevator != 0; })) {
          // El recorrido a pie no anuncia una cabina que el piloto todavía no ha llamado.
          WorldState walking;
          walking.size = world.size; walking.walkSurfaces = world.walkSurfaces; walking.obstacles = world.obstacles; walking.objects = world.objects;
          for (const auto &link : world.verticalLinks) if (link.kind == VerticalLinkKind::Stair) walking.verticalLinks.push_back(link);
          route = Navigation::Route(walking, agent.supportId, target.supportId);
        }
        if (route.empty()) return {};
        const auto &step = route.front(); nextSupport = WorldGeometry::FindSurface(world, step.toSupport); intermediate = true;
        auto entry = WorldGeometry::Ground(step.entry), exit = WorldGeometry::Ground(step.exit);
        if (step.linkId != 0 && nextSupport) {
          const auto crossing = exit - entry;
          if (crossing.lengthSquared() > 0.f) {
            const auto inside = exit + crossing.normalized() * SupportEntryMargin;
            const float height = WorldGeometry::SurfaceHeight(*nextSupport, inside);
            // Un extremo sobre el límite no basta cuando la tolerancia del waypoint lo borra antes de pisarlo.
            if (WorldGeometry::ContainsSupport(*nextSupport, inside) &&
                !WorldGeometry::PositionBlocked(world, inside, GameplayConfig::AgentRadius + CollisionMargin, height, CombatConfig::CrouchedHeight, nextSupport->id)) exit = inside;
          }
        }
        // Las superficies contiguas conservan el portal libre que muestrea la navegación física.
        if (const auto link = std::ranges::find(world.verticalLinks, step.linkId, &VerticalLink::id); link != world.verticalLinks.end())
          reach = std::max(RouteCellSize, link->width * .5f - GameplayConfig::AgentRadius);
        const auto crossing = exit - entry;
        const float along = crossing.lengthSquared() > 0.f ? (agent.position - entry).dot(crossing) / crossing.lengthSquared() : 0.f;
        const bool crossingEntry = along >= 0.f && along <= 1.f && (agent.position - entry - crossing * along).length() <= reach;
        intermediatePoint = (agent.position - entry).length() <= reach || crossingEntry ? exit : entry;
      }
      const auto supportAt = [&](sf::Vector2f point) -> const WalkSurface * {
        if (nextSupport && nextSupport->kind == WalkSurfaceKind::Stair && WorldGeometry::ContainsSupport(*nextSupport, point) &&
          (!currentSupport || WorldGeometry::SurfaceHeight(*nextSupport, point) > WorldGeometry::SurfaceHeight(*currentSupport, point) + LevelConfig::SupportTolerance)) return nextSupport;
        if (currentSupport && WorldGeometry::ContainsSupport(*currentSupport, point)) return currentSupport;
        if (nextSupport && WorldGeometry::ContainsSupport(*nextSupport, point)) return nextSupport;
        return nullptr;
      };
      const auto blocked = [&](sf::Vector2f point) {
        const auto *support = supportAt(point);
        return (currentSupport && !support) || Blocked(world, point, support ? WorldGeometry::SurfaceHeight(*support, point) : 0.f, support ? support->id : 0, allowCameraContact);
      };
      const int sx = std::clamp(static_cast<int>(agent.position.x / RouteCellSize), 0, columns - 1);
      const int sy = std::clamp(static_cast<int>(agent.position.y / RouteCellSize), 0, rows - 1);
      const int start = sy * columns + sx;
      const auto pointAt = [columns, start, &agent](int index) {
        return index == start ? agent.position : sf::Vector2f{(index % columns + 0.5f) * RouteCellSize, (index / columns + 0.5f) * RouteCellSize};
      };
      std::vector<int> previous(static_cast<std::size_t>(columns * rows), -1);
      previous[start] = start;
      std::queue<int> queue;
      queue.push(start);
      int finish = -1;
      while (!queue.empty()) {
        const int current = queue.front(); queue.pop();
        const auto point = pointAt(current);
        const auto *support = supportAt(point);
        const float elevation = support ? WorldGeometry::SurfaceHeight(*support, point) : 0.f;
        const bool atTarget = pointDestination ? support && support->id == target.supportId && (point - target.position).length() <= RouteCellSize : CanInteract(world, agent, point, elevation, target);
        if (!blocked(point) && (intermediate ? (point - intermediatePoint).length() <= reach : atTarget)) { finish = current; break; }
        const int x = current % columns, y = current / columns;
        for (const auto [dx, dy] : std::array<std::pair<int, int>, 8>{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}}}) {
          const int nx = x + dx, ny = y + dy, next = ny * columns + nx;
          if (nx < 0 || ny < 0 || nx >= columns || ny >= rows || previous[next] != -1 || blocked(pointAt(next))) continue;
          if (currentSupport) {
            auto moved = point; auto z = elevation; auto id = support ? support->id : 0;
            WorldGeometry::MoveOnSupports(world, moved, z, id, pointAt(next) - point, GameplayConfig::AgentRadius, CombatConfig::CrouchedHeight);
            const auto *expected = supportAt(pointAt(next));
            if ((moved - pointAt(next)).length() > .01f || !expected || std::abs(z - WorldGeometry::SurfaceHeight(*expected, pointAt(next))) > LevelConfig::SupportTolerance) continue;
          }
          previous[next] = current;
          queue.push(next);
        }
      }
      if (finish < 0) return {};
      std::vector<sf::Vector2f> route;
      for (int index = finish; index != start; index = previous[index]) route.push_back(pointAt(index));
      if (route.empty()) route.push_back(intermediate ? intermediatePoint : pointAt(finish));
      std::ranges::reverse(route);
      return route;
    }

    ActionCommand AimHead(const Agent &agent, const Guard &guard) {
      ActionCommand command;
      const auto head = CombatGeometry::ActorVolumes(guard.position, guard.facing, Stance::Standing).front().center;
      const auto offset = head - agent.position;
      command.aimX = offset.x;
      command.aimY = offset.y;
      const float height = guard.elevation + CombatGeometry::HeadCenterHeight(Stance::Standing) + CombatConfig::HeadHeight * 0.25f;
      command.aimSlope = (height - agent.elevation - CombatGeometry::WeaponHeight(agent.stance)) / std::max(0.001f, offset.length());
      return command;
    }

    void Defend(const WorldState &world, const Agent &agent, ActionCommand &command) {
      command.weaponChoice = static_cast<int>(WeaponKind::NonLethal);
      command.toggleSuppressor = !agent.nonLethalSuppressorEquipped && agent.nonLethalSuppressor > 0.f;
      command.reloadWeapon = agent.nonLethalMagazine == 0 && agent.loadout.nonLethalAmmo > 0 && agent.reloadRemaining <= 0.f;
      Agent aimingAgent = agent;
      if (command.crouch) aimingAgent.stance = Stance::Crouched;
      const Guard *nearest = nullptr;
      float distance = GameplayConfig::WeaponRange;
      for (const auto &guard : world.guards) {
        if (guard.state == GuardState::Dead || guard.state == GuardState::Incapacitated || !Visibility::GuardVisible(world, agent, guard)) continue;
        const auto offset = guard.position - agent.position;
        if (offset.length() < 0.1f || offset.length() >= distance) continue;
        const auto aim = AimHead(aimingAgent, guard);
        const sf::Vector2f direction = sf::Vector2f{aim.aimX, aim.aimY}.normalized();
        const float height = agent.elevation + CombatGeometry::WeaponHeight(aimingAgent.stance);
        const auto hit = CombatGeometry::IntersectActor(agent.position, direction, height, aim.aimSlope, GameplayConfig::WeaponRange, guard.position, guard.facing, Stance::Standing, false, guard.elevation);
        if (!hit || hit->zone != HitZone::Head) continue;
        const auto point = agent.position + direction * hit->distance;
        const bool blocked = WorldGeometry::RayOccluded(world, {agent.position.x, agent.position.y, height}, {point.x, point.y, hit->height});
        if (!blocked) { nearest = &guard; distance = offset.length(); }
      }
      if (!nearest) return;
      const auto aim = AimHead(aimingAgent, *nearest);
      command.aimX = aim.aimX;
      command.aimY = aim.aimY;
      command.aimSlope = aim.aimSlope;
      command.fireSelected = agent.nonLethalMagazine > 0 && agent.reloadRemaining <= 0.f && !command.reloadWeapon;
    }
  }

  void MissionPilot::Reset() {
    route_.clear();
    target_ = 0;
    support_ = 0;
    nextPlan_ = 0.f;
    stage_ = "esperando_mision";
  }

  void MissionPilot::EnableTour() {
    tourEnabled_ = true; tourInitialized_ = false; tourComplete_ = false;
    tourSupports_.clear(); visitedSupports_.clear(); completedCheckpoints_.clear(); nextPuzzleInput_ = 0.f; Reset();
  }

  void MissionPilot::ObserveSupport(const Agent &agent) {
    if (agent.supportId != 0 && std::ranges::find(visitedSupports_, agent.supportId) == visitedSupports_.end()) visitedSupports_.push_back(agent.supportId);
  }

  bool MissionPilot::AtDestination(const Agent &agent, sf::Vector2f position, std::uint32_t support) {
    return agent.supportId == support && (agent.position - position).length() <= MissionPilotConfig::RouteCellSize;
  }

  ActionCommand MissionPilot::TravelTo(const WorldState &world, PlayerId player, sf::Vector2f position, std::uint32_t support, bool defend) {
    ActionCommand command; command.aimX = 1.f;
    const auto found = std::ranges::find(world.agents, player, &Agent::id);
    const auto *surface = WorldGeometry::FindSurface(world, support);
    if (found == world.agents.end() || !found->connected || !found->alive || found->incapacitated || !surface) { stage_ = "destino_no_disponible"; return command; }
    const auto &agent = *found; ObserveSupport(agent);
    if (agent.stance != Stance::Crouched) command.crouch = true;
    if (AtDestination(agent, position, support)) { stage_ = "destino_alcanzado"; return command; }
    Interactable target; target.position = position; target.elevation = WorldGeometry::SurfaceHeight(*surface, position); target.supportId = support; target.footprint = {1.f, 1.f};
    const auto routeKey = support | 0x80000000u;
    if (target_ != routeKey || support_ != agent.supportId || world.elapsed >= nextPlan_) {
      route_ = PlanRoute(world, agent, target, true, tourEnabled_); target_ = routeKey; support_ = agent.supportId; nextPlan_ = world.elapsed + MissionPilotConfig::RouteRefresh;
    }
    while (!route_.empty() && (route_.front() - agent.position).length() < MissionPilotConfig::WaypointTolerance) route_.erase(route_.begin());
    stage_ = route_.empty() ? "esperando_ruta_transitable" : "recorriendo_nivel";
    if (!route_.empty()) {
      const auto offset = route_.front() - agent.position;
      const auto direction = offset / std::max(offset.length(), GameplayConfig::CrouchSpeed / GameplayConfig::CommandHz);
      command.moveX = direction.x; command.moveY = direction.y;
    }
    if (defend) Defend(world, agent, command);
    return command;
  }

  ActionCommand MissionPilot::Update(const WorldState &world, PlayerId player, bool collectObjective, bool allowExtraction) {
    ActionCommand command;
    if (world.phase != GamePhase::Mission) { Reset(); return command; }
    if (world.contract != ContractId::GhostArchive) { stage_ = "contrato_no_admitido"; return command; }
    const auto found = std::ranges::find_if(world.agents, [player](const Agent &agent) { return agent.id == player; });
    if (found == world.agents.end() || !found->connected || !found->alive || found->incapacitated) { stage_ = "agente_no_operativo"; return command; }
    const auto &agent = *found;
    ObserveSupport(agent);
    if (agent.extracted) { stage_ = "extraido"; return command; }
    if (tourEnabled_ && !tourComplete_) {
      if (agent.puzzle.target != 0) {
        stage_ = "resolviendo_puerta_del_recorrido";
        if (world.elapsed < nextPuzzleInput_) return command;
        command.puzzleTarget = agent.puzzle.target; nextPuzzleInput_ = world.elapsed + MissionPilotConfig::RouteRefresh;
        if (agent.puzzle.family == PuzzleFamily::Arrows && agent.puzzle.stage < agent.puzzle.sequence.size()) command.puzzleInput = agent.puzzle.sequence[agent.puzzle.stage];
        else if (agent.puzzle.family == PuzzleFamily::Wires) {
          for (unsigned int source = 1; source <= 4; ++source) if (!(agent.puzzle.connectedSources & (1u << (source - 1)))) { command.puzzleInput = WireInput(source, MatchingWirePort(agent.puzzle, source)); break; }
        }
        else if (agent.puzzle.family != PuzzleFamily::Arrows) throw std::runtime_error("La puerta del recorrido requiere una familia de puzle no admitida por el piloto");
        return command;
      }
      // El almacén tiene una única puerta cerrada: se abre con interacción y puzle reales antes de recorrerlo.
      const auto door = std::ranges::find_if(world.objects, [](const Interactable &object) { return object.kind == ObjectKind::Door && !object.open; });
      if (door != world.objects.end()) {
        if (agent.stance != Stance::Crouched) command.crouch = true;
        stage_ = "abriendo_puerta_del_recorrido";
        if (CanInteract(world, agent, agent.position, agent.elevation, *door)) { command.interact = true; return command; }
        if (target_ != door->id || support_ != agent.supportId || world.elapsed >= nextPlan_) {
          route_ = PlanRoute(world, agent, *door, false, tourEnabled_); target_ = door->id; support_ = agent.supportId; nextPlan_ = world.elapsed + MissionPilotConfig::RouteRefresh;
        }
        while (!route_.empty() && (route_.front() - agent.position).length() < MissionPilotConfig::WaypointTolerance) route_.erase(route_.begin());
        if (!route_.empty()) {
          const auto offset = route_.front() - agent.position;
          const auto direction = offset / std::max(offset.length(), GameplayConfig::CrouchSpeed / GameplayConfig::CommandHz);
          command.moveX = direction.x; command.moveY = direction.y;
        } else stage_ = "esperando_ruta_transitable";
        Defend(world, agent, command); return command;
      }
      if (!tourInitialized_ || tourSeed_ != world.seed) {
        tourInitialized_ = true; tourSeed_ = world.seed; tourSupports_.clear(); completedCheckpoints_.clear();
        for (const auto id : {10u, 12u, 11u, 15u, 20u, 23u, 22u, 21u, 25u, 24u, 13u, 14u, 16u, 1u, 5u, 7u, 6u})
          if (const auto *surface = WorldGeometry::FindSurface(world, id); surface && surface->enabled) tourSupports_.push_back(id);
      }
      if (!tourSupports_.empty()) {
        const auto *surface = WorldGeometry::FindSurface(world, tourSupports_.front());
        if (!surface) throw std::runtime_error("Desapareció una superficie del recorrido QA");
        std::optional<sf::Vector2f> checkpoint;
        for (const auto fraction : std::array<sf::Vector2f, 9>{{{.5f, .5f}, {.25f, .5f}, {.75f, .5f}, {.5f, .25f}, {.5f, .75f}, {.25f, .25f}, {.75f, .25f}, {.25f, .75f}, {.75f, .75f}}}) {
          const auto point = surface->bounds.position + surface->bounds.size.componentWiseMul(fraction);
          const float height = WorldGeometry::SurfaceHeight(*surface, point);
          if (!Blocked(world, point, height, surface->id, tourEnabled_)) { checkpoint = point; break; }
        }
        if (!checkpoint) throw std::runtime_error("No hay un punto físico libre en la zona QA " + std::to_string(surface->id));
        if (AtDestination(agent, *checkpoint, surface->id)) {
          completedCheckpoints_.push_back(surface->id); tourSupports_.erase(tourSupports_.begin()); Reset();
        } else return TravelTo(world, player, *checkpoint, surface->id, true);
      }
      if (!tourSupports_.empty()) { stage_ = "recorriendo_nivel"; return command; }
      tourComplete_ = true; Reset();
    }
    if (agent.stance != Stance::Crouched) command.crouch = true;
    const auto kind = world.objectives == 0 && collectObjective ? ObjectKind::Terminal : ObjectKind::Extraction;
    const auto observed = std::ranges::find_if(world.objects, [kind](const Interactable &object) { return object.kind == kind; });
    const Interactable *target = observed == world.objects.end() ? nullptr : &*observed;
    Interactable exploration;
    if (!target && kind == ObjectKind::Terminal) {
      const auto floor = std::ranges::find_if(world.walkSurfaces, [](const WalkSurface &surface) { return surface.layer == 1 && surface.kind == WalkSurfaceKind::Floor; });
      if (floor != world.walkSurfaces.end()) {
        exploration.position = floor->bounds.position + floor->bounds.size - sf::Vector2f{GameplayConfig::AgentRadius * 3.f, GameplayConfig::AgentRadius * 3.f};
        exploration.elevation = WorldGeometry::SurfaceHeight(*floor, exploration.position); exploration.supportId = floor->id;
        exploration.footprint = {1.f, 1.f}; target = &exploration;
      }
    }
    if (!target && kind == ObjectKind::Extraction) {
      exploration.position = {GameplayConfig::MissionSpawnX, GameplayConfig::MissionSpawnY + GameplayConfig::AgentRadius};
      if (const auto support = WorldGeometry::FindSupport(world, exploration.position, 0.f, 0, GameplayConfig::AgentRadius)) {
        exploration.elevation = support->elevation; exploration.supportId = support->id; exploration.footprint = {1.f, 1.f}; target = &exploration;
      }
    }
    if (!target) { stage_ = "objetivo_no_observado"; return command; }
    if (kind == ObjectKind::Extraction && !allowExtraction) { stage_ = "esperando_autorizacion_extraccion"; Defend(world, agent, command); return command; }
    stage_ = kind == ObjectKind::Terminal ? "recuperando_archivo" : "regresando_extraccion";
    if (CanInteract(world, agent, agent.position, agent.elevation, *target)) {
      if (target == &exploration) { command.moveX = kind == ObjectKind::Terminal ? -1.f : 1.f; stage_ = "explorando_planta_publica"; return command; }
      Defend(world, agent, command);
      if (command.fireSelected || command.reloadWeapon || command.toggleSuppressor || agent.reloadRemaining > 0.f) { stage_ = "defensa_antes_de_interactuar"; return command; }
      command.interact = true;
      stage_ = kind == ObjectKind::Terminal ? "interactuando_archivo" : "interactuando_extraccion";
      return command;
    }
    if (target_ != target->id || support_ != agent.supportId || world.elapsed >= nextPlan_) {
      route_ = PlanRoute(world, agent, *target, false, tourEnabled_);
      target_ = target->id;
      support_ = agent.supportId;
      nextPlan_ = world.elapsed + MissionPilotConfig::RouteRefresh;
    }
    while (!route_.empty() && (route_.front() - agent.position).length() < MissionPilotConfig::WaypointTolerance) route_.erase(route_.begin());
    if (!route_.empty()) {
      const auto offset = route_.front() - agent.position;
      const float distance = GameplayConfig::CrouchSpeed / GameplayConfig::CommandHz;
      const auto direction = offset / std::max(offset.length(), distance);
      command.moveX = direction.x;
      command.moveY = direction.y;
    } else stage_ = "esperando_ruta_transitable";
    Defend(world, agent, command);
    return command;
  }
}
