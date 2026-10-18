#pragma once

#include <SFML/Graphics/Rect.hpp>
#include <SFML/System/Vector2.hpp>
#include <SFML/System/Vector3.hpp>
#include <cstdint>
#include <vector>

namespace NoTrace {
  enum class SurfaceMaterial { Concrete, Steel, Grate, Industrial, Soil };
  enum class WalkSurfaceKind { Floor, Stair, Catwalk, Bridge, ElevatorCabin };
  enum class GripKind { Wall, Ledge, Pipe };
  enum class VerticalLinkKind { Stair, ElevatorDock, Attach, GripTransfer, Climb, Drop };
  enum class ElevatorPhase { Docked, Closing, Moving, Opening };

  struct WalkSurface {
    std::uint32_t id = 0;
    unsigned int layer = 0;
    WalkSurfaceKind kind = WalkSurfaceKind::Floor;
    sf::FloatRect bounds;
    float elevation = 0.f;
    sf::Vector2f slope;
    float thickness = 6.f;
    SurfaceMaterial material = SurfaceMaterial::Concrete;
    bool enabled = true;
  };
  struct GripSurface {
    std::uint32_t id = 0;
    GripKind kind = GripKind::Wall;
    sf::Vector3f start;
    sf::Vector3f end;
    sf::Vector2f normal;
  };
  struct VerticalLink {
    std::uint32_t id = 0;
    VerticalLinkKind kind = VerticalLinkKind::Stair;
    std::uint32_t fromSupport = 0;
    std::uint32_t toSupport = 0;
    std::uint32_t fromGrip = 0;
    std::uint32_t toGrip = 0;
    sf::Vector3f from;
    sf::Vector3f to;
    float width = 36.f;
    bool bidirectional = true;
    std::uint32_t elevator = 0;
  };
  struct ElevatorState {
    std::uint32_t id = 0;
    std::uint32_t object = 0;
    std::uint32_t cabinSupport = 0;
    std::vector<float> stops;
    std::vector<std::uint32_t> dockSupports;
    unsigned int targetStop = 0;
    ElevatorPhase phase = ElevatorPhase::Docked;
    float phaseTime = 0.f;
    float elevation = 0.f;
    bool requested = false;
  };
}
