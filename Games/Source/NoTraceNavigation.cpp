#include "NoTraceSimulation.h"
#include "NoTracePerformance.h"
#include "NoTraceNavigation.h"
#include "NoTraceWorldGeometry.h"
#include "NoTraceDistractionGeometry.h"
#include "Config/NoTraceAudioConfig.h"
#include "Config/NoTraceNavigationConfig.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>

namespace NoTrace {
  namespace {
    float Length(sf::Vector2f value) { return std::hypot(value.x, value.y); }
    sf::Vector2f Unit(sf::Vector2f value) { const float length = Length(value); return length > 0.0001f ? value / length : sf::Vector2f{}; }
    float Distance(sf::Vector2f first, sf::Vector2f second) { return Length(first - second); }
    bool NavigationGoalBlocked(const WorldState &world, sf::Vector2f target) {
      if (world.walkSurfaces.empty()) return WorldGeometry::PositionBlocked(world, target, GameplayConfig::AgentRadius);
      for (const auto &surface : world.walkSurfaces) if (WorldGeometry::ContainsSupport(surface, target) &&
          !WorldGeometry::PositionBlocked(world, target, GameplayConfig::AgentRadius, WorldGeometry::SurfaceHeight(surface, target), CombatConfig::StandingHeight, surface.id)) return false;
      return true;
    }
    std::vector<std::uint32_t> NavigationGeometry(const WorldState &world, std::uint32_t support, std::uint32_t nextSupport) {
      std::vector<std::uint32_t> result;
      result.reserve(32 + world.obstacles.size() * 7 + world.objects.size() * 9 + world.walkSurfaces.size() * 10 + world.verticalLinks.size() * 16);
      const auto number = [&](float value) { result.push_back(std::bit_cast<std::uint32_t>(value)); };
      const auto point = [&](sf::Vector2f value) { number(value.x); number(value.y); };
      const auto bounds = [&](sf::FloatRect value) { point(value.position); point(value.size); };
      point(world.size);
      result.push_back(static_cast<std::uint32_t>(world.obstacles.size()));
      for (const auto &obstacle : world.obstacles) {
        result.push_back(WorldGeometry::BlocksMovement(obstacle));
        if (WorldGeometry::BlocksMovement(obstacle)) { bounds(obstacle.bounds); number(obstacle.elevation); number(WorldGeometry::ObstacleHeight(obstacle)); }
      }
      result.push_back(static_cast<std::uint32_t>(world.objects.size()));
      for (const auto &object : world.objects) {
        result.push_back(object.id); result.push_back(WorldGeometry::BlocksMovement(object));
        if (WorldGeometry::BlocksMovement(object)) { bounds(WorldGeometry::ObjectFootprint(object)); number(object.elevation); number(WorldGeometry::ObjectHeight(object)); }
      }
      float minimum = std::numeric_limits<float>::infinity(), maximum = -minimum;
      for (const auto id : {support, nextSupport}) if (const auto *surface = WorldGeometry::FindSurface(world, id)) {
        const auto end = surface->bounds.position + surface->bounds.size;
        for (const auto corner : {surface->bounds.position, end, sf::Vector2f{end.x, surface->bounds.position.y}, sf::Vector2f{surface->bounds.position.x, end.y}}) {
          minimum = std::min(minimum, WorldGeometry::SurfaceHeight(*surface, corner)); maximum = std::max(maximum, WorldGeometry::SurfaceHeight(*surface, corner));
        }
      }
      const auto surfaceCount = result.size(); result.push_back(0);
      for (const auto &surface : world.walkSurfaces) {
        if (!surface.enabled) continue;
        // Una cabina alejada en altura no altera el suelo ni el volumen de esta búsqueda.
        if (surface.kind == WalkSurfaceKind::ElevatorCabin && surface.id != support && surface.id != nextSupport &&
            (surface.elevation + LevelConfig::MaximumStepHeight < minimum || surface.elevation - surface.thickness > maximum + CombatConfig::StandingHeight + LevelConfig::MaximumStepHeight)) continue;
        ++result[surfaceCount]; result.push_back(surface.id); result.push_back(static_cast<std::uint32_t>(surface.kind));
        bounds(surface.bounds); number(surface.elevation); point(surface.slope); number(surface.thickness);
      }
      result.push_back(static_cast<std::uint32_t>(world.verticalLinks.size()));
      for (const auto &link : world.verticalLinks) {
        result.push_back(static_cast<std::uint32_t>(link.kind)); result.push_back(link.fromSupport); result.push_back(link.toSupport);
        point(WorldGeometry::Ground(link.from)); number(link.from.z); point(WorldGeometry::Ground(link.to)); number(link.to.z);
        number(link.width); result.push_back(link.bidirectional); result.push_back(link.elevator);
        result.push_back(WorldGeometry::ElevatorDocked(world, link.elevator, link.from.z));
        result.push_back(WorldGeometry::ElevatorDocked(world, link.elevator, link.to.z));
      }
      return result;
    }
  }

  void Simulation::ResetNavigation() {
    navigationSearches_.clear(); navigationQueue_.clear();
    navigationWorkTick_ = std::numeric_limits<std::uint64_t>::max(); navigationWorkRemaining_ = 0;
  }

  void Simulation::CancelNavigationSearch(std::uint32_t guard) {
    navigationSearches_.erase(guard); std::erase(navigationQueue_, guard);
  }

  void Simulation::PrepareNavigationTick() {
    if (navigationWorkTick_ == world_.tick) return;
    navigationWorkTick_ = world_.tick; navigationWorkRemaining_ = NavigationConfig::SearchNodesPerTick;
    std::vector<std::uint32_t> invalid;
    for (const auto &[id, search] : navigationSearches_) {
      const auto guard = std::ranges::find(world_.guards, id, &Guard::id);
      if (guard == world_.guards.end() || guard->bodyHidden || guard->storedIn != 0 || guard->state == GuardState::Dead || guard->state == GuardState::Incapacitated || guard->forcedFall.active || guard->heldBy != 0 ||
          guard->cqcState != CqcState::None || guard->state != search.intent || guard->position != search.origin || guard->elevation != search.elevation ||
          guard->supportId != search.support || world_.tick < search.requestedTick || world_.tick - search.requestedTick > 1 ||
          NavigationGeometry(world_, search.support, search.nextSupport) != search.geometry) invalid.push_back(id);
    }
    for (const auto id : invalid) CancelNavigationSearch(id);
  }

  bool Simulation::NavigationPathClear(sf::Vector2f from, sf::Vector2f to, std::uint32_t currentSupport, std::uint32_t nextSupport) const {
    NO_TRACE_PROFILE_SCOPE(NavigationPathClear);
    using GameplayConfig::AgentRadius;
    if (!world_.walkSurfaces.empty()) {
      const auto *currentSurface = WorldGeometry::FindSurface(world_, currentSupport), *nextSurface = WorldGeometry::FindSurface(world_, nextSupport);
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
  }

  void Simulation::AdvanceNavigationSearches() {
    if (navigationWorkRemaining_ == 0 || navigationQueue_.empty()) return;
    NO_TRACE_PROFILE_SCOPE(NavigationBfs);
    std::vector<std::uint32_t> invalid;
    for (const auto &[id, search] : navigationSearches_)
      if (NavigationGeometry(world_, search.support, search.nextSupport) != search.geometry) invalid.push_back(id);
    for (const auto id : invalid) CancelNavigationSearch(id);
    while (navigationWorkRemaining_ > 0 && !navigationQueue_.empty()) {
      const auto id = navigationQueue_.front(); navigationQueue_.pop_front();
      const auto found = navigationSearches_.find(id);
      if (found == navigationSearches_.end()) continue;
      auto &search = found->second;
      --navigationWorkRemaining_;
      const auto pointAt = [&](int index) { return sf::Vector2f{(index % search.columns + 0.5f) * NavigationConfig::CellSize, (index / search.columns + 0.5f) * NavigationConfig::CellSize}; };
      const auto clear = [&](sf::Vector2f from, sf::Vector2f to) { return NavigationPathClear(from, to, search.support, search.nextSupport); };
      if (search.seed < search.seeds.size()) {
        const int index = search.seeds[search.seed++];
        if (clear(search.origin, pointAt(index))) { search.previous[index] = index; search.pending.push_back(index); }
      } else if (search.head < search.pending.size()) {
        const int current = search.pending[search.head++];
        const int x = current % search.columns, y = current / search.columns;
        const auto point = pointAt(current); const float distance = Distance(point, search.target);
        if (distance < search.closestDistance) { search.closestDistance = distance; search.closest = current; }
        if (!search.goalBlocked && clear(point, search.target)) { search.finish = current; search.complete = true; search.reachesTarget = true; }
        else for (const auto [dx, dy] : std::array<std::pair<int, int>, 4>{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}}) {
          const int nx = x + dx, ny = y + dy;
          if (nx < 0 || ny < 0 || nx >= search.columns || ny >= search.rows) continue;
          const int next = ny * search.columns + nx;
          if (search.previous[next] >= 0 || !clear(point, pointAt(next))) continue;
          search.previous[next] = current; search.pending.push_back(next);
        }
      }
      if (!search.complete && search.seed == search.seeds.size() && search.head == search.pending.size()) { search.finish = search.closest; search.complete = true; }
      if (!search.complete) navigationQueue_.push_back(id);
    }
  }
  void Simulation::Navigate(Guard &guard, sf::Vector2f target, float speed, float dt, float targetElevation) {
    NO_TRACE_PROFILE_SCOPE(Navigation);
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
      if (!currentSurface || !targetSupport) { guard.navigation.clear(); CancelNavigationSearch(guard.id); return; }
      if (guard.supportId != targetSupport->id) {
        const auto route = Navigation::Route(world_, guard.supportId, targetSupport->id);
        if (route.empty()) { guard.navigation.clear(); CancelNavigationSearch(guard.id); return; }
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
          if (lift == world_.elevators.end()) { guard.navigation.clear(); CancelNavigationSearch(guard.id); return; }
          const float dock = currentSurface->kind == WalkSurfaceKind::ElevatorCabin ? step.exit.z : step.entry.z;
          if (!WorldGeometry::ElevatorDocked(world_, lift->id, dock)) {
            if (lift->phase == ElevatorPhase::Docked && !lift->requested) {
              const auto stop = std::ranges::find_if(lift->stops, [&](float z) { return std::abs(z - dock) <= LevelConfig::ElevatorDockTolerance; });
              if (stop != lift->stops.end()) { lift->targetStop = static_cast<unsigned int>(stop - lift->stops.begin()); lift->requested = true; }
            }
            guard.navigation.clear(); CancelNavigationSearch(guard.id); guard.navigationStuckTime = 0.f; guard.stepTime = 0.f; return;
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
    PrepareNavigationTick();
    const auto previousPosition = guard.position;
    if (!spatial) ResolvePenetration(guard.position, AgentRadius);
    if (guard.position != previousPosition) { guard.navigation.clear(); CancelNavigationSearch(guard.id); guard.navigationTimer = 0.f; }
    guard.navigationTimer = std::max(0.f, guard.navigationTimer - dt);
    const auto supportId = currentSurface ? currentSurface->id : 0, nextSupportId = nextSurface ? nextSurface->id : 0;
    const auto pathClear = [&](sf::Vector2f from, sf::Vector2f to) { return NavigationPathClear(from, to, supportId, nextSupportId); };
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
      guard.navigation.clear(); CancelNavigationSearch(guard.id); guard.navigationTimer = 0.f; guard.navigationStuckTime = 0.f; guard.navigationFailures = 0;
    }
    guard.navigationTarget = target;
    if (const auto active = navigationSearches_.find(guard.id); active != navigationSearches_.end()) {
      const auto &search = active->second;
      if (search.origin != guard.position || search.elevation != guard.elevation || search.support != supportId || search.nextSupport != nextSupportId ||
          search.intent != guard.state || search.geometry != NavigationGeometry(world_, supportId, nextSupportId)) CancelNavigationSearch(guard.id);
    }
    if (NavigationGoalBlocked(world_, target)) {
      // Sin volumen final válido no hay ruta; los pasos de recuperación conservan su comprobación física.
      CancelNavigationSearch(guard.id); guard.navigationTimer = RefreshInterval;
      if (!guard.navigation.empty() && !pathClear(guard.position, guard.navigation.front())) guard.navigation.clear();
    } else if (pathClear(guard.position, target)) { CancelNavigationSearch(guard.id); guard.navigation.clear(); guard.navigation.push_back(target); }
    else if (navigationSearches_.contains(guard.id) || (guard.navigation.empty() && guard.navigationTimer <= 0.f) ||
        (!guard.navigation.empty() && !pathClear(guard.position, guard.navigation.front()))) {
      if (!navigationSearches_.contains(guard.id)) {
        guard.navigation.clear(); guard.navigationTimer = RefreshInterval;
        NavigationSearch search;
        search.columns = static_cast<int>(std::ceil(world_.size.x / CellSize)); search.rows = static_cast<int>(std::ceil(world_.size.y / CellSize));
        if (search.columns <= 0 || search.rows <= 0 || search.columns * search.rows > MaximumCells) return;
        search.origin = guard.position; search.target = target; search.elevation = guard.elevation; search.support = supportId; search.nextSupport = nextSupportId;
        search.intent = guard.state; search.closestDistance = Distance(guard.position, target); search.geometry = NavigationGeometry(world_, supportId, nextSupportId);
        search.goalBlocked = NavigationGoalBlocked(world_, target);
        search.previous.assign(static_cast<std::size_t>(search.columns * search.rows), -1);
        const int startX = static_cast<int>(guard.position.x / CellSize), startY = static_cast<int>(guard.position.y / CellSize);
        // Las semillas conservan el orden y la posición real; cada comprobación consume presupuesto compartido.
        for (int y = std::max(0, startY - 1); y <= std::min(search.rows - 1, startY + 1); ++y)
          for (int x = std::max(0, startX - 1); x <= std::min(search.columns - 1, startX + 1); ++x) search.seeds.push_back(y * search.columns + x);
        navigationSearches_.emplace(guard.id, std::move(search)); navigationQueue_.push_back(guard.id);
      }
      navigationSearches_.at(guard.id).requestedTick = world_.tick;
      AdvanceNavigationSearches();
      const auto active = navigationSearches_.find(guard.id);
      // Esperar una ruta completa no cuenta como atasco ni permite avanzar por una ruta parcial.
      if (active == navigationSearches_.end() || !active->second.complete) { guard.stepTime = 0.f; return; }
      const auto &search = active->second;
      const auto pointAt = [&](int index) { return sf::Vector2f{(index % search.columns + 0.5f) * CellSize, (index / search.columns + 0.5f) * CellSize}; };
      for (int current = search.finish; current >= 0; current = search.previous[current]) {
        guard.navigation.push_back(pointAt(current));
        if (search.previous[current] == current) break;
      }
      std::ranges::reverse(guard.navigation);
      if (search.reachesTarget) guard.navigation.push_back(search.target);
      guard.navigationTimer = RefreshInterval;
      CancelNavigationSearch(guard.id);
    }
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
