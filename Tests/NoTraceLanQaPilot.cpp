#include "NoTraceLanQaPilot.h"
#include "Config/NoTraceNavigationConfig.h"
#include "NoTraceCombatGeometry.h"
#include "NoTraceVisibility.h"
#include "NoTraceWorldGeometry.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <queue>

namespace NoTrace::Qa {
  namespace {
    bool Blocked(const WorldState &world, sf::Vector2f point) {
      using namespace MissionPilotConfig;
      if (WorldGeometry::PositionBlocked(world, point, GameplayConfig::AgentRadius + CollisionMargin)) return true;
      for (const auto &object : world.objects) {
        if (object.kind == ObjectKind::Explosive && object.active && object.trap.state != TrapState::Disarmed && object.trap.state != TrapState::Detonated &&
            (point - object.position).length() <= object.trap.triggerRadius + DeviceMargin && Visibility::HasLineOfSight(world, object.position, point, Stance::Standing, object.id)) return true;
        if (object.kind != ObjectKind::Camera || !object.active || object.camera.state != CameraState::Operating) continue;
        const auto delta = point - object.position;
        const float distance = delta.length();
        if (distance > object.camera.range * GameplayConfig::CrouchedVisionFactor + DeviceMargin || !Visibility::HasLineOfSight(world, object.position, point, Stance::Crouched, object.id)) continue;
        if (distance <= GameplayConfig::CloseVisionDistance) return true;
        if (object.camera.sweeping) {
          const float angle = std::remainder(std::atan2(delta.y, delta.x) - object.camera.baseAngle, 6.2831853f);
          const float nearest = std::clamp(angle, object.camera.minAngle, object.camera.maxAngle);
          if (std::cos(angle - nearest) >= object.camera.cosHalfAngle) return true;
        } else if (object.facing.dot(delta) >= object.camera.cosHalfAngle * distance * object.facing.length()) return true;
      }
      return false;
    }

    bool CanInteract(const WorldState &world, sf::Vector2f point, const Interactable &target) {
      const auto closest = WorldGeometry::ClosestPoint(point, WorldGeometry::ObjectFootprint(target));
      return (closest - point).length() < GameplayConfig::InteractionDistance * MissionPilotConfig::InteractionFraction &&
          Visibility::HasLineOfSight(world, point, closest, Stance::Standing, target.id);
    }

    std::vector<sf::Vector2f> PlanRoute(const WorldState &world, const Agent &agent, const Interactable &target) {
      using namespace MissionPilotConfig;
      const int columns = static_cast<int>(std::ceil(world.size.x / RouteCellSize));
      const int rows = static_cast<int>(std::ceil(world.size.y / RouteCellSize));
      if (columns <= 0 || rows <= 0 || columns > NavigationConfig::MaximumCells / rows) return {};
      const auto pointAt = [columns](int index) {
        return sf::Vector2f{(index % columns + 0.5f) * RouteCellSize, (index / columns + 0.5f) * RouteCellSize};
      };
      const int sx = std::clamp(static_cast<int>(agent.position.x / RouteCellSize), 0, columns - 1);
      const int sy = std::clamp(static_cast<int>(agent.position.y / RouteCellSize), 0, rows - 1);
      const int start = sy * columns + sx;
      std::vector<int> previous(static_cast<std::size_t>(columns * rows), -1);
      previous[start] = start;
      std::queue<int> queue;
      queue.push(start);
      int finish = -1;
      while (!queue.empty()) {
        const int current = queue.front(); queue.pop();
        const auto point = pointAt(current);
        if (!Blocked(world, point) && CanInteract(world, point, target)) { finish = current; break; }
        const int x = current % columns, y = current / columns;
        for (const auto [dx, dy] : std::array<std::pair<int, int>, 4>{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}}) {
          const int nx = x + dx, ny = y + dy, next = ny * columns + nx;
          if (nx < 0 || ny < 0 || nx >= columns || ny >= rows || previous[next] != -1 || Blocked(world, pointAt(next))) continue;
          previous[next] = current;
          queue.push(next);
        }
      }
      if (finish < 0) return {};
      std::vector<sf::Vector2f> route;
      for (int index = finish; index != start; index = previous[index]) route.push_back(pointAt(index));
      if (route.empty()) route.push_back(pointAt(finish));
      std::ranges::reverse(route);
      return route;
    }

    ActionCommand AimHead(const Agent &agent, const Guard &guard) {
      ActionCommand command;
      const auto head = CombatGeometry::ActorVolumes(guard.position, guard.facing, Stance::Standing).front().center;
      const auto offset = head - agent.position;
      command.aimX = offset.x;
      command.aimY = offset.y;
      const float height = CombatGeometry::HeadCenterHeight(Stance::Standing) + CombatConfig::HeadHeight * 0.25f;
      command.aimSlope = (height - CombatGeometry::WeaponHeight(agent.stance)) / std::max(0.001f, offset.length());
      return command;
    }

    void Defend(const WorldState &world, const Agent &agent, ActionCommand &command) {
      command.weaponChoice = static_cast<int>(WeaponKind::NonLethal);
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
        const float height = CombatGeometry::WeaponHeight(aimingAgent.stance);
        const auto hit = CombatGeometry::IntersectActor(agent.position, direction, height, aim.aimSlope, GameplayConfig::WeaponRange, guard.position, guard.facing, Stance::Standing);
        if (!hit || hit->zone != HitZone::Head) continue;
        bool blocked = false;
        const auto clip = [&](const sf::FloatRect &bounds, float top) {
          const CombatGeometry::HitVolume volume{bounds.position + bounds.size * 0.5f, {1.f, 0.f}, bounds.size * 0.5f, 0.f, top, HitZone::None};
          const auto obstruction = CombatGeometry::IntersectVolume(agent.position, direction, height, aim.aimSlope, hit->distance, volume);
          blocked |= obstruction && *obstruction < hit->distance;
        };
        for (const auto &obstacle : world.obstacles) if (WorldGeometry::BlocksMovement(obstacle)) clip(obstacle.bounds, WorldGeometry::ObstacleHeight(obstacle));
        for (const auto &object : world.objects) if (WorldGeometry::BlocksMovement(object)) clip(WorldGeometry::ObjectFootprint(object), WorldGeometry::ObjectHeight(object));
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
    if (!collectObjective) {
      const auto host = std::ranges::find_if(world.agents, [](const Agent &entry) { return entry.id == GameplayConfig::HostPlayerId; });
      if (world.objectives == 0 || host == world.agents.end() || !host->extracted) { stage_ = "esperando_extraccion_anfitrion"; return command; }
    }
    const auto target = std::ranges::find_if(world.objects, [kind](const Interactable &object) { return object.kind == kind; });
    if (target == world.objects.end()) { stage_ = "objetivo_no_observado"; return command; }
    if (kind == ObjectKind::Extraction && !allowExtraction) { stage_ = "esperando_autorizacion_extraccion"; Defend(world, agent, command); return command; }
    stage_ = kind == ObjectKind::Terminal ? "recuperando_archivo" : "regresando_extraccion";
    if (CanInteract(world, agent.position, *target)) {
      command.interact = true;
      stage_ = kind == ObjectKind::Terminal ? "interactuando_archivo" : "interactuando_extraccion";
      return command;
    }
    if (target_ != target->id || world.elapsed >= nextPlan_ || route_.empty()) {
      route_ = PlanRoute(world, agent, *target);
      target_ = target->id;
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
