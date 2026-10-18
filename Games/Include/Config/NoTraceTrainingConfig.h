#pragma once

#include "Config/NoTraceCombatConfig.h"
#include "Config/NoTraceGameplayConfig.h"
#include "Config/NoTraceLevelConfig.h"
#include "Config/NoTraceTraversalConfig.h"
#include "Types/NoTraceSpatialTypes.h"
#include <SFML/System/Vector2.hpp>
#include <array>
#include <cstdint>
#include <cstddef>

namespace NoTrace::TrainingConfig {
  inline constexpr sf::Vector2f WorldSize{1900.f, 1740.f};
  inline constexpr std::uint32_t FirstDummyId = 1000;
  struct DummySpawn {
    sf::Vector2f position;
    sf::Vector2f facing{1.f, 0.f};
    float elevation = 0.f;
    std::uint32_t supportId = 1;
  };
  inline constexpr std::array<DummySpawn, 5> DummySpawns{{
    {{1210.f, 210.f}},
    {{1300.f, 210.f}},
    {{1390.f, 210.f}},
    // El margen de los bordes permite sacar todo el cuerpo de la losa con un solo empujón.
    {{480.f, 1136.f}, {0.f, -1.f}, LevelConfig::StoreyHeight, 10},
    {{1284.f, 1250.f}, {1.f, 0.f}, LevelConfig::StoreyHeight, 11}
  }};
  inline constexpr unsigned int DummyCount = static_cast<unsigned int>(DummySpawns.size());
  inline constexpr unsigned int MaximumRecordedHits = 1000000;
  inline constexpr float DummyMaximumHealth = CombatConfig::MaxGuardHealth;
  inline constexpr float DummyMinimumHealth = 1.f;
  inline constexpr float DummyFeedbackDuration = 1.2f;
  inline constexpr float DummyReactionDuration = 0.32f;
  inline constexpr unsigned int CameraControlGroup = 42;
  inline constexpr bool UnlimitedAmmo = true;
  inline constexpr int RefilledAmmo = GameplayConfig::MaximumAmmo;
  inline constexpr int RefilledDecoys = GameplayConfig::MaximumDecoys;
  inline constexpr int RefilledTools = GameplayConfig::MaximumTools;
  inline constexpr float TrainingTrapFuse = 8.f;
  inline constexpr float HealthRecoveryPerSecond = 30.f;
  inline constexpr std::size_t MaximumRetainedEvents = 256;
  inline constexpr sf::Vector2f ResetPosition{1120.f, 440.f};

  namespace TraversalCourse {
    inline constexpr std::uint32_t GroundSupport = 1;
    inline constexpr std::uint32_t ElevatorId = 1;
    inline constexpr std::uint32_t ElevatorObject = 109;
    inline constexpr std::uint32_t CabinSupport = 90;
    inline constexpr float Storey = LevelConfig::StoreyHeight;
    inline constexpr float BodyOffset = GameplayConfig::AgentRadius + TraversalConfig::BodyGap;
    inline constexpr sf::FloatRect WallBounds{{330.f, 1324.f}, {280.f, 16.f}};
    inline constexpr sf::FloatRect LedgeBounds{{280.f, 1324.f}, {400.f, 16.f}};
    inline constexpr sf::Vector2f ElevatorPosition{1400.f, 1400.f};
    inline constexpr sf::Vector2f ElevatorPanelPosition{1340.f, 1400.f};
    inline constexpr std::array<WalkSurface, 10> Surfaces{{
      {1, 0, WalkSurfaceKind::Floor, {{0.f, 0.f}, {1900.f, 1360.f}}, 0.f, {}, LevelConfig::SlabThickness, SurfaceMaterial::Concrete},
      {5, 0, WalkSurfaceKind::Floor, {{0.f, 1360.f}, {1360.f, 380.f}}, 0.f, {}, LevelConfig::SlabThickness, SurfaceMaterial::Concrete},
      {6, 0, WalkSurfaceKind::Floor, {{1440.f, 1360.f}, {460.f, 380.f}}, 0.f, {}, LevelConfig::SlabThickness, SurfaceMaterial::Concrete},
      {7, 0, WalkSurfaceKind::Floor, {{1360.f, 1440.f}, {80.f, 300.f}}, 0.f, {}, LevelConfig::SlabThickness, SurfaceMaterial::Concrete},
      {10, 1, WalkSurfaceKind::Floor, {{180.f, 1120.f}, {500.f, 220.f}}, Storey, {}, LevelConfig::SlabThickness, SurfaceMaterial::Industrial},
      {11, 1, WalkSurfaceKind::Floor, {{800.f, 1120.f}, {500.f, 320.f}}, Storey, {}, LevelConfig::SlabThickness, SurfaceMaterial::Steel},
      {12, 1, WalkSurfaceKind::Bridge, {{680.f, 1200.f}, {120.f, 100.f}}, Storey, {}, LevelConfig::SlabThickness, SurfaceMaterial::Grate},
      {15, 1, WalkSurfaceKind::Catwalk, {{1300.f, 1360.f}, {60.f, 80.f}}, Storey, {}, LevelConfig::SlabThickness, SurfaceMaterial::Grate},
      {20, 0, WalkSurfaceKind::Stair, {{200.f, 1340.f}, {80.f, 192.f}}, Storey, {0.f, -0.5f}, LevelConfig::SlabThickness, SurfaceMaterial::Steel},
      {90, 0, WalkSurfaceKind::ElevatorCabin, {{1360.f, 1360.f}, {80.f, 80.f}}, 0.f, {}, LevelConfig::SlabThickness, SurfaceMaterial::Steel}
    }};
    inline constexpr std::array<GripSurface, 3> Grips{{
      {41, GripKind::Wall, {340.f, 1340.f, 64.f}, {600.f, 1340.f, 64.f}, {0.f, 1.f}},
      {42, GripKind::Ledge, {340.f, 1340.f, Storey + TraversalConfig::HandHeight}, {600.f, 1340.f, Storey + TraversalConfig::HandHeight}, {0.f, 1.f}},
      {43, GripKind::Pipe, {600.f, 1370.f, Storey + TraversalConfig::HandHeight}, {820.f, 1370.f, Storey + TraversalConfig::HandHeight}, {}}
    }};
    inline constexpr std::array<VerticalLink, 12> Links{{
      {1, VerticalLinkKind::Stair, 5, 20, 0, 0, {240.f, 1544.f, 0.f}, {240.f, 1532.f, 0.f}, 64.f, true},
      {2, VerticalLinkKind::Stair, 20, 10, 0, 0, {240.f, 1340.f, Storey}, {240.f, 1328.f, Storey}, 64.f, true},
      {7, VerticalLinkKind::ElevatorDock, 5, 90, 0, 0, {1348.f, 1400.f, 0.f}, {1400.f, 1400.f, 0.f}, 64.f, true, ElevatorId},
      {8, VerticalLinkKind::ElevatorDock, 15, 90, 0, 0, {1348.f, 1400.f, Storey}, {1400.f, 1400.f, Storey}, 64.f, true, ElevatorId},
      {20, VerticalLinkKind::Attach, 5, 0, 0, 41, {340.f, 1366.f, 0.f}, {340.f, 1340.f + BodyOffset, 18.f}, 24.f, true},
      {21, VerticalLinkKind::GripTransfer, 0, 0, 41, 42, {500.f, 1340.f + BodyOffset, 18.f}, {500.f, 1340.f + BodyOffset, Storey}, 24.f, true},
      {22, VerticalLinkKind::Climb, 0, 10, 42, 0, {500.f, 1340.f + BodyOffset, Storey}, {500.f, 1310.f, Storey}, 24.f, true},
      {23, VerticalLinkKind::GripTransfer, 0, 0, 42, 43, {600.f, 1340.f + BodyOffset, Storey}, {600.f, 1370.f, Storey}, 24.f, true},
      {24, VerticalLinkKind::Climb, 0, 11, 43, 0, {820.f, 1370.f, Storey}, {840.f, 1370.f, Storey}, 24.f, true},
      {25, VerticalLinkKind::Drop, 0, 1, 41, 0, {340.f, 1340.f + BodyOffset, 18.f}, {340.f, 1340.f + BodyOffset, 0.f}, 24.f, false},
      {26, VerticalLinkKind::Drop, 0, 1, 42, 0, {340.f, 1340.f + BodyOffset, Storey}, {340.f, 1340.f + BodyOffset, 0.f}, 24.f, false},
      {27, VerticalLinkKind::Drop, 0, 5, 43, 0, {700.f, 1370.f, Storey}, {700.f, 1370.f, 0.f}, 24.f, false}
    }};
  }
}
