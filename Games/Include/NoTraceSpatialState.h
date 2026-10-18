#pragma once

#include "NoTraceSpatialGeometry.h"
#include "NoTraceTraversal.h"
#include "NoTraceLevel.h"
#include <nlohmann/json.hpp>
#include <unordered_set>
#include <stdexcept>

namespace NoTrace::SpatialState {
  using Json = nlohmann::json;
  inline Json Vec(sf::Vector3f point) { return {point.x, point.y, point.z}; }
  inline float Number(const Json &value, float minimum = LevelConfig::MinimumElevation, float maximum = LevelConfig::MaximumElevation) {
    if (!value.is_number()) throw std::runtime_error("Magnitud espacial no numérica");
    const float result = value.get<float>();
    if (!std::isfinite(result) || result < minimum || result > maximum) throw std::runtime_error("Magnitud espacial fuera de rango");
    return result;
  }
  inline std::uint32_t Integer(const Json &value, std::uint32_t maximum = 1000000) {
    if (!value.is_number_integer() || value < 0 || value > maximum) throw std::runtime_error("Identificador espacial fuera de rango");
    return value.get<std::uint32_t>();
  }
  inline sf::Vector3f Point(const Json &value, const WorldState &world) {
    if (!value.is_array() || value.size() != 3) throw std::runtime_error("Punto espacial inválido");
    return {Number(value[0], 0.f, world.size.x), Number(value[1], 0.f, world.size.y), Number(value[2])};
  }
  inline const Json &Array(const Json &snapshot, const char *name, std::size_t maximum) {
    const auto &array = snapshot.at(name);
    if (!array.is_array() || array.size() > maximum) throw std::runtime_error("Colección espacial demasiado grande");
    return array;
  }
  inline void Write(Json &snapshot, const WorldState &world, bool filtered) {
    snapshot["spatialVersion"] = 1;
    snapshot["geometryHash"] = Level::GeometryHash(world);
    snapshot["surfaces"] = Json::array();
    for (const auto &surface : world.walkSurfaces) snapshot["surfaces"].push_back({surface.id, surface.layer, static_cast<int>(surface.kind), surface.bounds.position.x, surface.bounds.position.y,
      surface.bounds.size.x, surface.bounds.size.y, surface.elevation, surface.slope.x, surface.slope.y, surface.thickness, static_cast<int>(surface.material), surface.enabled});
    snapshot["grips"] = Json::array();
    for (const auto &grip : world.gripSurfaces) snapshot["grips"].push_back({grip.id, static_cast<int>(grip.kind), grip.start.x, grip.start.y, grip.start.z, grip.end.x, grip.end.y, grip.end.z, grip.normal.x, grip.normal.y});
    snapshot["verticalLinks"] = Json::array();
    for (const auto &link : world.verticalLinks) snapshot["verticalLinks"].push_back({link.id, static_cast<int>(link.kind), link.fromSupport, link.toSupport, link.fromGrip, link.toGrip,
      link.from.x, link.from.y, link.from.z, link.to.x, link.to.y, link.to.z, link.width, link.bidirectional, link.elevator});
    snapshot["elevators"] = Json::array();
    for (const auto &lift : world.elevators) snapshot["elevators"].push_back({lift.id, lift.object, lift.cabinSupport, lift.stops, lift.dockSupports, lift.targetStop, static_cast<int>(lift.phase), lift.phaseTime, lift.elevation, lift.requested});
    for (auto &value : snapshot["agents"]) {
      const auto &agent = *std::ranges::find(world.agents, value["id"].get<PlayerId>(), &Agent::id); const auto &state = agent.traversal;
      value["elevation"] = agent.elevation; value["supportId"] = agent.supportId;
      value["traversal"] = {static_cast<int>(state.mode), state.surfaceId, state.linkId, state.targetSupport, state.reversed, state.parameter, state.progress,
        state.direction.x, state.direction.y, Vec(state.from), Vec(state.to)};
    }
    for (auto &value : snapshot["guards"]) {
      const auto &guard = *std::ranges::find(world.guards, value["id"].get<std::uint32_t>(), &Guard::id);
      value["elevation"] = guard.elevation; value["supportId"] = guard.supportId; value["lastKnownElevation"] = filtered ? guard.elevation : guard.lastKnownElevation;
      value["patrolElevations"] = filtered ? std::vector<float>{} : guard.patrolElevations;
    }
    for (auto &value : snapshot["objects"]) {
      const auto &object = *std::ranges::find(world.objects, value["id"].get<std::uint32_t>(), &Interactable::id);
      value["elevation"] = object.elevation; value["supportId"] = object.supportId; value["elevatorId"] = object.elevatorId;
      if (!world.walkSurfaces.empty()) {
        if (object.kind != ObjectKind::Camera && object.kind != ObjectKind::CameraPanel) value.erase("camera");
        if (object.kind != ObjectKind::Explosive) value.erase("trap");
        if (object.puzzle.kind == PuzzleKind::None) value.erase("puzzleDevice");
      }
    }
    for (std::size_t index = 0; index < world.obstacles.size(); ++index) { snapshot["obstacles"][index]["elevation"] = world.obstacles[index].elevation; snapshot["obstacles"][index]["height"] = world.obstacles[index].height; }
  }
  inline void ReadGeometry(const Json &snapshot, WorldState &world) {
    if (Integer(snapshot.at("spatialVersion"), 1) != 1) throw std::runtime_error("Versión espacial incompatible");
    std::unordered_set<std::uint32_t> ids;
    for (const auto &value : Array(snapshot, "surfaces", LevelConfig::MaximumSurfaces)) {
      if (!value.is_array() || value.size() != 13 || !value[12].is_boolean()) throw std::runtime_error("Superficie incompleta");
      WalkSurface surface; surface.id = Integer(value[0]); surface.layer = Integer(value[1], LevelConfig::MaximumLayers - 1); surface.kind = static_cast<WalkSurfaceKind>(Integer(value[2], 4));
      surface.bounds = {{Number(value[3], 0.f, world.size.x), Number(value[4], 0.f, world.size.y)}, {Number(value[5], 0.01f, world.size.x), Number(value[6], 0.01f, world.size.y)}};
      surface.elevation = Number(value[7]); surface.slope = {Number(value[8], -1.f, 1.f), Number(value[9], -1.f, 1.f)};
      surface.thickness = Number(value[10], 0.01f, 32.f); surface.material = static_cast<SurfaceMaterial>(Integer(value[11], 4)); surface.enabled = value[12].get<bool>();
      const auto end = surface.bounds.position + surface.bounds.size;
      if (surface.id == 0 || !ids.insert(surface.id).second || end.x > world.size.x || end.y > world.size.y ||
          WorldGeometry::SurfaceHeight(surface, end) < LevelConfig::MinimumElevation || WorldGeometry::SurfaceHeight(surface, end) > LevelConfig::MaximumElevation ||
          (surface.kind != WalkSurfaceKind::Stair && surface.slope.lengthSquared() > 0.f)) throw std::runtime_error("Geometría de superficie inválida");
      for (const auto point : {surface.bounds.position, end, sf::Vector2f{end.x, surface.bounds.position.y}, sf::Vector2f{surface.bounds.position.x, end.y}})
        if (WorldGeometry::SurfaceHeight(surface, point) < LevelConfig::MinimumElevation || WorldGeometry::SurfaceHeight(surface, point) > LevelConfig::MaximumElevation) throw std::runtime_error("Pendiente fuera de límites");
      world.walkSurfaces.push_back(surface);
    }
    ids.clear();
    for (const auto &value : Array(snapshot, "grips", LevelConfig::MaximumGrips)) {
      if (!value.is_array() || value.size() != 10) throw std::runtime_error("Agarre incompleto");
      GripSurface grip; grip.id = Integer(value[0]); grip.kind = static_cast<GripKind>(Integer(value[1], 2));
      grip.start = Point(Json::array({value[2], value[3], value[4]}), world); grip.end = Point(Json::array({value[5], value[6], value[7]}), world);
      grip.normal = {Number(value[8], -1.f, 1.f), Number(value[9], -1.f, 1.f)};
      if (grip.id == 0 || !ids.insert(grip.id).second || (grip.end - grip.start).length() < 1.f ||
          (grip.kind != GripKind::Pipe && std::abs(grip.normal.lengthSquared() - 1.f) > 0.001f) || (grip.kind == GripKind::Pipe && grip.normal.lengthSquared() != 0.f)) throw std::runtime_error("Superficie de agarre inválida");
      world.gripSurfaces.push_back(grip);
    }
    ids.clear();
    for (const auto &value : Array(snapshot, "elevators", LevelConfig::MaximumElevators)) {
      if (!value.is_array() || value.size() != 10 || !value[3].is_array() || !value[4].is_array() || !value[9].is_boolean()) throw std::runtime_error("Ascensor incompleto");
      ElevatorState lift; lift.id = Integer(value[0]); lift.object = Integer(value[1]); lift.cabinSupport = Integer(value[2]);
      if (value[3].empty() || value[3].size() > LevelConfig::MaximumLayers || value[3].size() != value[4].size()) throw std::runtime_error("Paradas de ascensor inválidas");
      for (std::size_t index = 0; index < value[3].size(); ++index) { lift.stops.push_back(Number(value[3][index])); lift.dockSupports.push_back(Integer(value[4][index])); }
      lift.targetStop = Integer(value[5], static_cast<std::uint32_t>(lift.stops.size() - 1)); lift.phase = static_cast<ElevatorPhase>(Integer(value[6], 3));
      lift.phaseTime = Number(value[7], 0.f, LevelConfig::ElevatorDoorTime); lift.elevation = Number(value[8]); lift.requested = value[9].get<bool>();
      const auto *cabin = WorldGeometry::FindSurface(world, lift.cabinSupport);
      if (lift.id == 0 || !ids.insert(lift.id).second || !cabin || cabin->kind != WalkSurfaceKind::ElevatorCabin || std::abs(cabin->elevation - lift.elevation) > LevelConfig::ElevatorDockTolerance ||
          !std::ranges::is_sorted(lift.stops) || std::adjacent_find(lift.stops.begin(), lift.stops.end()) != lift.stops.end()) throw std::runtime_error("Cabina de ascensor inválida");
      for (const auto id : lift.dockSupports) if (!WorldGeometry::FindSurface(world, id)) throw std::runtime_error("Muelle de ascensor inexistente");
      world.elevators.push_back(std::move(lift));
    }
    ids.clear();
    for (const auto &value : Array(snapshot, "verticalLinks", LevelConfig::MaximumLinks)) {
      if (!value.is_array() || value.size() != 15 || !value[13].is_boolean()) throw std::runtime_error("Conexión vertical incompleta");
      VerticalLink link; link.id = Integer(value[0]); link.kind = static_cast<VerticalLinkKind>(Integer(value[1], 5)); link.fromSupport = Integer(value[2]); link.toSupport = Integer(value[3]); link.fromGrip = Integer(value[4]); link.toGrip = Integer(value[5]);
      link.from = Point(Json::array({value[6], value[7], value[8]}), world); link.to = Point(Json::array({value[9], value[10], value[11]}), world);
      link.width = Number(value[12], GameplayConfig::AgentRadius * 2.f, 256.f); link.bidirectional = value[13].get<bool>(); link.elevator = Integer(value[14]);
      if (link.id == 0 || !ids.insert(link.id).second || (link.fromSupport == 0) == (link.fromGrip == 0) || (link.toSupport == 0) == (link.toGrip == 0)) throw std::runtime_error("Extremos verticales ambiguos");
      const auto endpoint = [&](std::uint32_t support, std::uint32_t grip, sf::Vector3f point) {
        if (support) {
          const auto *surface = WorldGeometry::FindSurface(world, support);
          return surface && WorldGeometry::ContainsSupport(*surface, WorldGeometry::Ground(point)) && (surface->kind == WalkSurfaceKind::ElevatorCabin || std::abs(WorldGeometry::SurfaceHeight(*surface, WorldGeometry::Ground(point)) - point.z) <= LevelConfig::SupportTolerance);
        }
        const auto *surface = WorldGeometry::FindGrip(world, grip);
        return surface && WorldGeometry::SpatialDistance(Traversal::BodyAt(*surface, Traversal::Parameter(*surface, point)), point) <= TraversalConfig::EndpointTolerance;
      };
      if (!endpoint(link.fromSupport, link.fromGrip, link.from) || !endpoint(link.toSupport, link.toGrip, link.to) ||
          (link.kind == VerticalLinkKind::ElevatorDock && std::ranges::none_of(world.elevators, [&](const ElevatorState &lift) { return lift.id == link.elevator; }))) throw std::runtime_error("Conexión sin geometría válida");
      world.verticalLinks.push_back(link);
    }
  }
  inline void ReadEntities(const Json &snapshot, WorldState &world) {
    for (std::size_t index = 0; index < world.agents.size(); ++index) {
      auto &agent = world.agents[index]; const auto &value = snapshot.at("agents")[index];
      agent.elevation = Number(value.at("elevation")); agent.supportId = Integer(value.at("supportId"));
      const auto &state = value.at("traversal");
      if (!state.is_array() || state.size() != 11 || !state[4].is_boolean()) throw std::runtime_error("Estado de desplazamiento incompleto");
      auto &traversal = agent.traversal; traversal.mode = static_cast<TraversalMode>(Integer(state[0], 6)); traversal.surfaceId = Integer(state[1]); traversal.linkId = Integer(state[2]); traversal.targetSupport = Integer(state[3]); traversal.reversed = state[4].get<bool>();
      traversal.parameter = Number(state[5], 0.f, 1.f); traversal.progress = Number(state[6], 0.f, 1.f); traversal.direction = {Number(state[7], -1.f, 1.f), Number(state[8], -1.f, 1.f)};
      traversal.from = Point(state[9], world); traversal.to = Point(state[10], world);
    }
    for (std::size_t index = 0; index < world.guards.size(); ++index) {
      auto &guard = world.guards[index]; const auto &value = snapshot.at("guards")[index];
      guard.elevation = Number(value.at("elevation")); guard.supportId = Integer(value.at("supportId")); guard.lastKnownElevation = Number(value.at("lastKnownElevation"));
      const auto &patrol = value.at("patrolElevations");
      if (!patrol.is_array() || (!patrol.empty() && patrol.size() != guard.patrol.size())) throw std::runtime_error("Alturas de patrulla incompatibles");
      for (const auto &height : patrol) guard.patrolElevations.push_back(Number(height));
    }
    for (std::size_t index = 0; index < world.objects.size(); ++index) {
      auto &object = world.objects[index]; const auto &value = snapshot.at("objects")[index];
      object.elevation = Number(value.at("elevation")); object.supportId = Integer(value.at("supportId")); object.elevatorId = Integer(value.at("elevatorId"));
    }
    for (std::size_t index = 0; index < world.obstacles.size(); ++index) {
      auto &obstacle = world.obstacles[index]; const auto &value = snapshot.at("obstacles")[index];
      obstacle.elevation = Number(world.walkSurfaces.empty() ? value.value("elevation", Json(0.f)) : value.at("elevation")); obstacle.height = Number(world.walkSurfaces.empty() ? value.value("height", Json(0.f)) : value.at("height"), 0.f, LevelConfig::MaximumElevation);
    }
  }
  inline void Validate(const Json &snapshot, const WorldState &world) {
    if (!snapshot.at("geometryHash").is_string() || snapshot.at("geometryHash").get<std::string>() != Level::GeometryHash(world)) throw std::runtime_error("Huella de geometría incompatible");
    const auto supported = [&](sf::Vector2f position, float z, std::uint32_t id) {
      if (world.walkSurfaces.empty()) return id == 0 && z == 0.f;
      const auto *surface = WorldGeometry::FindSurface(world, id);
      return surface && WorldGeometry::ContainsSupport(*surface, position) && std::abs(WorldGeometry::SurfaceHeight(*surface, position) - z) <= LevelConfig::SupportTolerance;
    };
    for (const auto &agent : world.agents) {
      const auto &state = agent.traversal;
      if (state.direction.lengthSquared() > 1.001f) throw std::runtime_error("Dirección de desplazamiento inválida");
      if (state.mode == TraversalMode::Grounded) {
        if (state.surfaceId || state.linkId || state.targetSupport || state.progress != 0.f || state.parameter != 0.f || !supported(agent.position, agent.elevation, agent.supportId)) throw std::runtime_error("Agente sin apoyo físico");
      } else if (state.mode == TraversalMode::Hang || state.mode == TraversalMode::Shimmy) {
        const auto *surface = WorldGeometry::FindGrip(world, state.surfaceId);
        if (!surface || agent.supportId != 0 || state.linkId || WorldGeometry::SpatialDistance(Traversal::BodyAt(*surface, state.parameter), WorldGeometry::Feet(agent)) > LevelConfig::SupportTolerance) throw std::runtime_error("Cuerpo separado del agarre");
      } else {
        const auto link = std::ranges::find(world.verticalLinks, state.linkId, &VerticalLink::id);
        if (agent.supportId != 0 || (link == world.verticalLinks.end() && state.mode != TraversalMode::Drop) ||
            WorldGeometry::SpatialDistance(Traversal::TransitionPoint(state, state.progress), WorldGeometry::Feet(agent)) > LevelConfig::SupportTolerance) throw std::runtime_error("Transición espacial incoherente");
        if (state.targetSupport != 0 && !WorldGeometry::FindSurface(world, state.targetSupport)) throw std::runtime_error("Destino de transición inexistente");
        if (link != world.verticalLinks.end()) {
          const auto fromSupport = state.reversed ? link->toSupport : link->fromSupport, fromGrip = state.reversed ? link->toGrip : link->fromGrip;
          const auto toSupport = state.reversed ? link->fromSupport : link->toSupport;
          const auto endpoint = state.reversed ? link->from : link->to;
          const auto entrance = state.reversed ? link->to : link->from;
          if ((state.reversed && !link->bidirectional) || state.targetSupport != toSupport || state.surfaceId != fromGrip ||
              (state.mode == TraversalMode::Climb) != (link->kind == VerticalLinkKind::Climb) ||
              (state.mode == TraversalMode::Drop) != (link->kind == VerticalLinkKind::Drop)) throw std::runtime_error("Modo o destino de enlace incompatible");
          if (fromSupport && !supported(WorldGeometry::Ground(state.from), state.from.z, fromSupport)) throw std::runtime_error("Entrada sin apoyo físico");
          if (fromGrip) {
            const auto *surface = WorldGeometry::FindGrip(world, fromGrip);
            if (!surface || WorldGeometry::SpatialDistance(Traversal::BodyAt(*surface, Traversal::Parameter(*surface, state.from)), state.from) > LevelConfig::SupportTolerance) throw std::runtime_error("Entrada fuera del agarre");
          }
          if (state.mode != TraversalMode::Drop && (WorldGeometry::SpatialDistance(endpoint, state.to) > LevelConfig::SupportTolerance ||
              WorldGeometry::SpatialDistance(entrance, state.from) > (fromGrip ? TraversalConfig::LinkReach : TraversalConfig::AttachReach))) throw std::runtime_error("Extremo de transición manipulado");
        }
        if (state.mode == TraversalMode::Drop && (WorldGeometry::Ground(state.from) != WorldGeometry::Ground(state.to) || state.to.z > state.from.z ||
            (state.targetSupport && !supported(WorldGeometry::Ground(state.to), state.to.z, state.targetSupport)))) throw std::runtime_error("Caída sin suelo válido");
      }
    }
    for (const auto &guard : world.guards) if (!supported(guard.position, guard.elevation, guard.supportId)) throw std::runtime_error("Guardia sin apoyo físico");
    for (const auto &object : world.objects) {
      if (object.supportId != 0 && !WorldGeometry::FindSurface(world, object.supportId)) throw std::runtime_error("Objeto sin superficie de anclaje");
      if (object.elevatorId != 0 && (object.kind != ObjectKind::Elevator || std::ranges::none_of(world.elevators, [&](const ElevatorState &lift) { return lift.id == object.elevatorId; }))) throw std::runtime_error("Mando de ascensor sin cabina");
    }
  }
}
