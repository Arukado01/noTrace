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
    inline constexpr float SupportEntryMargin = CollisionMargin + WaypointTolerance;
    inline constexpr float DeviceMargin = 25.f;
    inline constexpr float InteractionFraction = 0.95f;
    inline constexpr float ProgressDistance = 0.5f;
    inline constexpr float StuckEvidenceSeconds = 3.f;
    inline constexpr float StuckLimitSeconds = 12.f;
  }

  class MissionPilot {
  public:
    ActionCommand Update(const WorldState &world, PlayerId player, bool collectObjective, bool allowExtraction = true);
    ActionCommand TravelTo(const WorldState &world, PlayerId player, sf::Vector2f position, std::uint32_t support, bool defend = false);
    static bool AtDestination(const Agent &agent, sf::Vector2f position, std::uint32_t support);
    void EnableTour();
    bool TourDone() const { return tourComplete_; }
    const std::vector<std::uint32_t> &VisitedSupports() const { return visitedSupports_; }
    const std::vector<std::uint32_t> &CompletedCheckpoints() const { return completedCheckpoints_; }
    const std::string &Stage() const { return stage_; }
    void Reset();

  private:
    std::vector<sf::Vector2f> route_;
    std::uint32_t target_ = 0;
    std::uint32_t support_ = 0;
    float nextPlan_ = 0.f;
    float nextPuzzleInput_ = 0.f;
    std::string stage_ = "esperando_mision";
    bool tourEnabled_ = false, tourInitialized_ = false, tourComplete_ = false;
    std::uint32_t tourSeed_ = 0;
    std::vector<std::uint32_t> tourSupports_, visitedSupports_, completedCheckpoints_;
    void ObserveSupport(const Agent &agent);
  };
}
