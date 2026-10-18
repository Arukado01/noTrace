#pragma once

#include "NoTraceLevel.h"
#include "NoTraceSimulation.h"
#include <nlohmann/json.hpp>

namespace NoTrace::TestFixture {
  inline void LegacyPlane(WorldState &world) {
    world.walkSurfaces.clear(); world.verticalLinks.clear(); world.gripSurfaces.clear(); world.elevators.clear();
    for (auto &agent : world.agents) { agent.elevation = 0.f; agent.supportId = 0; agent.traversal = {}; agent.forcedFall = {}; }
    for (auto &guard : world.guards) { guard.elevation = 0.f; guard.supportId = 0; guard.lastKnownElevation = 0.f; guard.patrolElevations.clear(); guard.forcedFall = {}; }
    for (auto &object : world.objects) { object.elevation = 0.f; object.supportId = 0; object.elevatorId = 0; }
    for (auto &obstacle : world.obstacles) { obstacle.elevation = 0.f; obstacle.height = 0.f; }
  }
  inline void LegacyPlane(nlohmann::json &snapshot) {
    WorldState plane;
    plane.size = {snapshot.at("size")[0].get<float>(), snapshot.at("size")[1].get<float>()};
    snapshot["surfaces"] = snapshot["grips"] = snapshot["verticalLinks"] = snapshot["elevators"] = nlohmann::json::array();
    snapshot["geometryHash"] = Level::GeometryHash(plane);
    for (auto &agent : snapshot["agents"]) {
      agent["elevation"] = 0.f; agent["supportId"] = 0;
      agent["forcedFall"] = {false, {0.f, 0.f, 0.f}, {0.f, 0.f, 0.f}, 0.f, 0, 0};
      agent["traversal"] = {0, 0, 0, 0, false, 0.f, 0.f, 0.f, 0.f, {0.f, 0.f, 0.f}, {0.f, 0.f, 0.f}};
    }
    for (auto &guard : snapshot["guards"]) { guard["elevation"] = guard["lastKnownElevation"] = 0.f; guard["supportId"] = 0; guard["patrolElevations"] = nlohmann::json::array(); guard["forcedFall"] = {false, {0.f, 0.f, 0.f}, {0.f, 0.f, 0.f}, 0.f, 0, 0}; }
    Simulation defaults; defaults.ResetBase(); LegacyPlane(const_cast<WorldState &>(defaults.GetState()));
    const auto templateObject = defaults.SnapshotFor(1).at("objects").front();
    for (auto &object : snapshot["objects"]) {
      object["elevation"] = 0.f; object["supportId"] = object["elevatorId"] = 0;
      for (const auto key : {"camera", "trap", "puzzleDevice"}) {
        auto merged = templateObject.at(key);
        if (object.contains(key)) merged.update(object.at(key));
        object[key] = std::move(merged);
      }
    }
    for (auto &obstacle : snapshot["obstacles"]) { obstacle["elevation"] = obstacle["height"] = 0.f; }
  }
}
