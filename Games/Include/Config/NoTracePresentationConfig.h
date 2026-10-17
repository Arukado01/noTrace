#pragma once

#include <SFML/Graphics/Color.hpp>
#include <SFML/System/Vector2.hpp>
#include <array>

namespace NoTrace::PresentationConfig {
  inline constexpr sf::Vector2f GroundProjection{1.35f, 0.675f};
  inline constexpr sf::Vector2f CameraFocusRatio{0.5f, 0.535f};
  inline constexpr float PixelSize = 2.f;
  inline constexpr float FloorTileSize = 48.f;
  inline constexpr float HighCoverHeight = 66.f;
  inline constexpr float LowCoverHeight = 24.f;
  inline constexpr float HideoutHeight = 40.f;
  inline constexpr float ActorHeight = 46.f;
  inline constexpr unsigned int WorldLabelSize = 14;
  inline constexpr int NoiseCircleSegments = 40;
  inline constexpr int VisionSegments = 14;
  inline constexpr sf::Vector2f MinimapPosition{1320.f, 140.f};
  inline constexpr sf::Vector2f MinimapSize{240.f, 190.f};
  inline constexpr sf::Vector2f MinimapGroundOffset{10.f, 24.f};
  inline constexpr sf::Vector2f MinimapGroundSize{220.f, 120.f};
  inline constexpr unsigned int MinimapTextSize = 11;
  inline constexpr std::array<float, 4> WalkStride{-4.f, 0.f, 4.f, 0.f};
  inline const sf::Color Void{5, 9, 16};
  inline const sf::Color Floor{24, 33, 42};
  inline const sf::Color FloorAlternate{27, 37, 46};
  inline const sf::Color FloorGrid{35, 47, 56};
  inline const sf::Color WallTop{69, 86, 98};
  inline const sf::Color WallLeft{41, 54, 68};
  inline const sf::Color WallRight{31, 42, 55};
  inline const sf::Color CrateTop{91, 95, 73};
  inline const sf::Color CrateLeft{57, 65, 54};
  inline const sf::Color CrateRight{41, 49, 45};
  inline const sf::Color Light{198, 218, 218};
  inline const sf::Color Cyan{73, 188, 202};
  inline const sf::Color Green{110, 200, 147};
  inline const sf::Color Amber{226, 173, 78};
  inline const sf::Color Red{221, 67, 77};
  inline const sf::Color Violet{168, 130, 214};
  inline const sf::Color Outline{9, 15, 23};
  inline const sf::Color Skin{184, 149, 119};
  inline const std::array<sf::Color, 4> AgentUniforms{{{46, 92, 111}, {75, 101, 76}, {101, 80, 113}, {116, 97, 65}}};
}
