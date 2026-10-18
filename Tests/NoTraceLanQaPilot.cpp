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

namespace NoTrace::Qa {
  namespace {
    bool Blocked(const WorldState &world, sf::Vector2f point, float elevation, std::uint32_t support) {
      using namespace MissionPilotConfig;
      if (point.x < GameplayConfig::AgentRadius || point.y < GameplayConfig::AgentRadius || point.x > world.size.x - GameplayConfig::AgentRadius || point.y > world.size.y - GameplayConfig::AgentRadius ||
        WorldGeometry::PositionBlocked(world, point, GameplayConfig::AgentRadius + CollisionMargin, elevation, CombatConfig::CrouchedHeight, support)) return true;
      for (const auto &object : world.objects) {
        if (std::abs(object.elevation - elevation) > GameplayConfig::InteractionDistance) continue;
        const auto clear = [&] { return !WorldGeometry::RayOccluded(world, {object.position.x, object.position.y, object.elevation + CombatGeometry::HeadCenterHeight(Stance::Standing)},
          {point.x, point.y, elevation + CombatGeometry::HeadCenterHeight(Stance::Crouched)}, object.id); };
        if (object.kind == ObjectKind::Explosive && object.active && object.trap.state != TrapState::Disarmed && object.trap.state != TrapState::Detonated &&
            (point - object.position).length() <= object.trap.triggerRadius + GameplayConfig::AgentRadius && clear()) return true;
        if (object.kind != ObjectKind::Camera || !object.active || object.camera.state != CameraState::Operating) continue;
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

    std::vector<sf::Vector2f> PlanRoute(const WorldState &world, const Agent &agent, const Interactable &target) {
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
        const auto route = Navigation::Route(world, agent.supportId, target.supportId);
        if (route.empty()) return {};
        const auto &step = route.front(); nextSupport = WorldGeometry::FindSurface(world, step.toSupport); intermediate = true;
        auto entry = WorldGeometry::Ground(step.entry), exit = WorldGeometry::Ground(step.exit);
        if (step.linkId == 0 && nextSupport && currentSupport->slope.lengthSquared() == 0.f && nextSupport->slope.lengthSquared() == 0.f) {
          const auto firstEnd = currentSupport->bounds.position + currentSupport->bounds.size, nextEnd = nextSupport->bounds.position + nextSupport->bounds.size;
          const sf::Vector2f minimum{std::max(currentSupport->bounds.position.x, nextSupport->bounds.position.x), std::max(currentSupport->bounds.position.y, nextSupport->bounds.position.y)};
          const sf::Vector2f maximum{std::min(firstEnd.x, nextEnd.x), std::min(firstEnd.y, nextEnd.y)};
          const auto crossing = WorldGeometry::ClosestPoint(agent.position, {minimum, maximum - minimum});
          const auto inward = nextSupport->bounds.position + nextSupport->bounds.size * .5f - crossing;
          const auto beyond = crossing + (inward.lengthSquared() > 0.f ? inward.normalized() * std::min(GameplayConfig::AgentRadius, inward.length()) : sf::Vector2f{});
          if (WorldGeometry::ClearBodySegment(world, {crossing.x, crossing.y, agent.elevation}, {beyond.x, beyond.y, agent.elevation}, GameplayConfig::AgentRadius, CombatConfig::CrouchedHeight, agent.supportId)) { entry = crossing; exit = beyond; }
        }
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
        return (currentSupport && !support) || Blocked(world, point, support ? WorldGeometry::SurfaceHeight(*support, point) : 0.f, support ? support->id : 0);
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
        if (!blocked(point) && (intermediate ? (point - intermediatePoint).length() <= reach : CanInteract(world, agent, point, elevation, target))) { finish = current; break; }
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

  ActionCommand MissionPilot::Update(const WorldState &world, PlayerId player, bool collectObjective, bool allowExtraction) {
    ActionCommand command;
    if (world.phase != GamePhase::Mission) { Reset(); return command; }
    if (world.contract != ContractId::GhostArchive) { stage_ = "contrato_no_admitido"; return command; }
    const auto found = std::ranges::find_if(world.agents, [player](const Agent &agent) { return agent.id == player; });
    if (found == world.agents.end() || !found->connected || !found->alive || found->incapacitated) { stage_ = "agente_no_operativo"; return command; }
    const auto &agent = *found;
    if (agent.extracted) { stage_ = "extraido"; return command; }
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
      route_ = PlanRoute(world, agent, *target);
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
