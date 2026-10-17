#pragma once

#include "Types/NoTraceGameplayTypes.h"
#include <string>
#include <vector>

namespace NoTrace::Qa {
  namespace MissionPilotConfig {
    inline constexpr float RouteCellSize = GameplayConfig::NavigationCellSize;
    inline constexpr float RouteRefresh = 6.f / GameplayConfig::CommandHz;
    inline constexpr float WaypointTolerance = 0.25f;
    inline constexpr float CollisionMargin = 0.5f;
    inline constexpr float DeviceMargin = 25.f;
    inline constexpr float InteractionFraction = 0.5f;
  }

  class MissionPilot {
  public:
    ActionCommand Update(const WorldState &world, PlayerId player, bool collectObjective, bool allowExtraction = true);
    const std::string &Stage() const { return stage_; }
    void Reset();

  private:
    std::vector<sf::Vector2f> route_;
    std::uint32_t target_ = 0;
    float nextPlan_ = 0.f;
    std::string stage_ = "esperando_mision";
  };
}
