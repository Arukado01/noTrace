#include "NoTraceSimulation.h"
#include "NoTraceNavigation.h"
#include "NoTraceWorldGeometry.h"
#include "NoTraceDistractionGeometry.h"
#include "Config/NoTraceAudioConfig.h"
#include "Config/NoTraceNavigationConfig.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <queue>

namespace NoTrace {
  namespace {
    float Length(sf::Vector2f value) { return std::hypot(value.x, value.y); }
    sf::Vector2f Unit(sf::Vector2f value) { const float length = Length(value); return length > 0.0001f ? value / length : sf::Vector2f{}; }
    float Distance(sf::Vector2f first, sf::Vector2f second) { return Length(first - second); }
  }
  void Simulation::Navigate(Guard &guard, sf::Vector2f target, float speed, float dt, float targetElevation) {
    using GameplayConfig::AgentRadius;
    using namespace NavigationConfig;
    if (dt <= 0.f || speed <= 0.f) return;
    const bool spatial = !world_.walkSurfaces.empty();
    const WalkSurface *currentSurface = nullptr, *nextSurface = nullptr;
    if (spatial) {
      if (const auto support = WorldGeometry::FindSupport(world_, guard.position, guard.elevation, guard.supportId, AgentRadius)) { guard.supportId = support->id; guard.elevation = support->elevation; }
      currentSurface = WorldGeometry::FindSurface(world_, guard.supportId);
      const float targetZ = std::isfinite(targetElevation) ? targetElevation : guard.elevation;
      const auto targetSupport = WorldGeometry::FindSupport(world_, target, targetZ, 0, AgentRadius);
      if (!currentSurface || !targetSupport) { guard.navigation.clear(); return; }
      if (guard.supportId != targetSupport->id) {
        const auto route = Navigation::Route(world_, guard.supportId, targetSupport->id);
        if (route.empty()) { guard.navigation.clear(); return; }
        const auto &step = route.front();
        nextSurface = WorldGeometry::FindSurface(world_, step.toSupport);
        const auto entry = WorldGeometry::Ground(step.entry), exit = WorldGeometry::Ground(step.exit);
        const bool atEntry = Distance(guard.position, entry) <= WaypointTolerance;
        const auto crossing = exit - entry;
        const float along = crossing.lengthSquared() > 0.f ? (guard.position - entry).dot(crossing) / crossing.lengthSquared() : 0.f;
        const auto link = std::ranges::find(world_.verticalLinks, step.linkId, &VerticalLink::id);
        const float halfWidth = link == world_.verticalLinks.end() ? AgentRadius * 2.f : link->width * 0.5f;
        const bool crossingEntry = crossing.lengthSquared() > 0.f && along >= 0.f && along <= 1.f && Distance(guard.position, entry + crossing * along) <= halfWidth;
        const bool crossingLink = atEntry || crossingEntry || currentSurface->kind == WalkSurfaceKind::ElevatorCabin;
        const auto cabinEnd = currentSurface->bounds.position + currentSurface->bounds.size;
        const float clearance = AgentRadius + SpatialConfig::CollisionSkin;
        const bool boardingCabin = currentSurface->kind == WalkSurfaceKind::ElevatorCabin && !WorldGeometry::ElevatorDocked(world_, step.elevator, step.exit.z) &&
            (guard.position.x < currentSurface->bounds.position.x + clearance || guard.position.y < currentSurface->bounds.position.y + clearance ||
             guard.position.x > cabinEnd.x - clearance || guard.position.y > cabinEnd.y - clearance);
        if (step.elevator != 0 && crossingLink && !boardingCabin) {
          auto lift = std::ranges::find(world_.elevators, step.elevator, &ElevatorState::id);
          if (lift == world_.elevators.end()) { guard.navigation.clear(); return; }
          const float dock = currentSurface->kind == WalkSurfaceKind::ElevatorCabin ? step.exit.z : step.entry.z;
          if (!WorldGeometry::ElevatorDocked(world_, lift->id, dock)) {
            if (lift->phase == ElevatorPhase::Docked && !lift->requested) {
              const auto stop = std::ranges::find_if(lift->stops, [&](float z) { return std::abs(z - dock) <= LevelConfig::ElevatorDockTolerance; });
              if (stop != lift->stops.end()) { lift->targetStop = static_cast<unsigned int>(stop - lift->stops.begin()); lift->requested = true; }
            }
            guard.navigation.clear(); guard.navigationStuckTime = 0.f; guard.stepTime = 0.f; return;
          }
        }
        auto inside = exit;
        if (nextSurface) {
          const auto inward = nextSurface->bounds.position + nextSurface->bounds.size * 0.5f - exit;
          if (inward.lengthSquared() > 0.f) inside += inward.normalized() * std::min(AgentRadius, inward.length());
        }
        // El cruce conserva su dirección hasta entrar físicamente en el apoyo siguiente.
        // El cuerpo termina de entrar antes de solicitar el viaje y bloquear el cierre de la puerta.
        target = boardingCabin ? currentSurface->bounds.position + currentSurface->bounds.size * 0.5f : crossingLink ? inside : entry;
      }
    }
    const auto previousPosition = guard.position;
    if (!spatial) ResolvePenetration(guard.position, AgentRadius);
    if (guard.position != previousPosition) { guard.navigation.clear(); guard.navigationTimer = 0.f; }
    guard.navigationTimer = std::max(0.f, guard.navigationTimer - dt);
    const auto pathClear = [this, spatial, currentSurface, nextSurface](sf::Vector2f from, sf::Vector2f to) {
      if (spatial) {
        const auto surfaceAt = [&](sf::Vector2f point) -> const WalkSurface * {
          if (currentSurface && WorldGeometry::ContainsSupport(*currentSurface, point)) return currentSurface;
          if (nextSurface && WorldGeometry::ContainsSupport(*nextSurface, point)) return nextSurface;
          return nullptr;
        };
        const auto *support = surfaceAt(from);
        if (!support) return false;
        float z = WorldGeometry::SurfaceHeight(*support, from); auto supportId = support->id;
        const auto movement = to - from;
        const int count = std::max(1, static_cast<int>(std::ceil(movement.length() / SpatialConfig::MaximumMovementStep)));
        for (int step = 0; step <= count; ++step) {
          const auto point = from + movement * (static_cast<float>(step) / count);
          const auto next = WorldGeometry::FindSupport(world_, point, z, supportId, AgentRadius);
          if (!next || WorldGeometry::PositionBlocked(world_, point, AgentRadius, next->elevation, CombatConfig::StandingHeight, next->id)) return false;
          supportId = next->id; z = next->elevation;
        }
        return true;
      }
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
      const auto displacement = guard.facing * std::min(Length(offset), speed * dt);
      if (spatial) WorldGeometry::MoveOnSupports(world_, guard.position, guard.elevation, guard.supportId, displacement, AgentRadius, CombatConfig::StandingHeight);
      else Move(guard.position, displacement, AgentRadius);
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
}
