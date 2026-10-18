#pragma once

#include "Config/NoTraceCombatConfig.h"
#include "Config/NoTraceGameplayConfig.h"
#include "Config/NoTraceSpatialConfig.h"
#include <SFML/Graphics/Color.hpp>
#include <SFML/System/Vector2.hpp>
#include <array>
#include <cstddef>
#include <string_view>

namespace NoTrace::PresentationConfig {
  inline constexpr float PrivateOrderIntroDuration = 8.f;
  inline constexpr std::size_t LabelCacheCapacity = 256;
  inline constexpr const char *ActorAtlas = "NoTrace/Characters/SoldiersAtlas.png";
  inline constexpr const char *ProneAtlas = "NoTrace/Characters/ProneAtlas.png";
  inline constexpr const char *ActorFrameTable = "Content/Textures/NoTrace/Characters/SoldiersAtlas.frames";
  inline constexpr const char *ProneFrameTable = "Content/Textures/NoTrace/Characters/ProneAtlas.frames";
  inline constexpr const char *ActorMuzzleTable = "Content/Textures/NoTrace/Characters/SoldiersAtlas.muzzles";
  inline constexpr const char *ProneMuzzleTable = "Content/Textures/NoTrace/Characters/ProneAtlas.muzzles";
  inline constexpr std::size_t ActorAtlasRows = 13;
  inline constexpr std::size_t ActorAtlasColumns = 16;
  inline constexpr std::size_t ProneAtlasRows = 8;
  inline constexpr std::size_t ProneAtlasColumns = 6;
  inline constexpr float WalkFramesPerSecond = 8.f;
  inline constexpr float RunFramesPerSecond = 13.f;
  inline constexpr float CrouchFramesPerSecond = 5.f;
  inline constexpr float CrawlFramesPerSecond = 4.f;
  inline constexpr sf::Vector2f GroundProjection{1.35f, 0.675f};
  inline constexpr sf::Vector2f CameraFocusRatio{0.5f, 0.535f};
  inline constexpr std::array<sf::Vector2f, 4> CoordinationButtonSockets{{{76.f, 103.f}, {128.f, 93.f}, {76.f, 74.f}, {128.f, 64.f}}};
  inline constexpr float PixelSize = 2.f;
  inline constexpr float DefaultZoom = 2.f;
  inline constexpr float MinimumZoom = 1.8f;
  inline constexpr float MaximumZoom = 4.5f;
  inline constexpr float ZoomFactorPerStep = 1.125f;
  inline constexpr float FloorTileSize = 48.f;
  inline constexpr float HighCoverHeight = SpatialConfig::HighCoverHeight;
  inline constexpr float LowCoverHeight = SpatialConfig::LowCoverHeight;
  inline constexpr float HideoutHeight = SpatialConfig::HideoutHeight;
  inline constexpr float ActorHeight = CombatConfig::StandingHeight;
  inline constexpr float ShotHeight = CombatConfig::StandingWeaponHeight;
  inline constexpr float CrouchedShotHeight = CombatConfig::CrouchedWeaponHeight;
  inline constexpr float ProneShotHeight = CombatConfig::ProneWeaponHeight;
  inline constexpr float StandingWeaponOffset = 12.f;
  inline constexpr float ProneWeaponOffset = 24.f;
  inline constexpr float StandingBarrelLength = 16.f;
  inline constexpr float ProneBarrelLength = 12.f;
  inline constexpr float ShotTargetTolerance = 18.f;
  inline constexpr float ActorHalfWidth = 18.f;
  inline constexpr float CrouchedHeight = CombatConfig::CrouchedHeight;
  inline constexpr sf::Vector2f ProneSilhouetteOffset{-26.f, -14.f};
  inline constexpr sf::Vector2f ProneSilhouetteSize{54.f, 24.f};
  inline constexpr unsigned int WorldLabelSize = 14;
  inline constexpr int NoiseCircleSegments = 40;
  inline constexpr int NoiseWaveCount = 2;
  inline constexpr float NoiseWaveLag = 0.22f;
  inline constexpr float FootstepVisualOffset = GameplayConfig::AgentRadius * 0.5f;
  inline constexpr float IntelligenceFeedbackDuration = 8.f;
  inline constexpr int VisionSegments = 14;
  inline constexpr float IconOffset = 12.f;
  inline constexpr float IconHalfSize = 9.f;
  inline constexpr std::size_t BodyIconWidth = 24;
  inline constexpr std::size_t BodyIconHeight = 16;
  inline constexpr float BodyIconPixelSize = 2.f;
  inline constexpr float FogEffectStep = 12.f;
  inline constexpr float FogFeatherWidth = 34.f;
  inline constexpr unsigned int FogFeatherBands = 6;
  inline constexpr unsigned int DetectionEyeSegments = 16;
  inline constexpr float DetectionWarningThreshold = 0.8f;
  inline constexpr float GuardEyeScale = 0.5f;
  inline constexpr float CqcArmReach = 24.f;
  inline constexpr float CqcKickReach = 34.f;
  inline constexpr float CqcRecoil = 4.f;
  inline constexpr float CqcGripSway = 2.f;
  inline constexpr float CqcFallDuration = 0.35f;
  inline constexpr float DummyHeight = ActorHeight;
  inline constexpr sf::Vector2f DummyHealthBarSize{72.f, 8.f};
  inline constexpr float DecoyGuideStep = 12.f;
  inline constexpr float DecoyMarkerSize = 8.f;
  inline constexpr float DecoyDiscRadius = 5.f;
  inline constexpr float AimReticleSize = 5.f;
  inline constexpr float FootstepMarkLength = 7.f;
  inline constexpr sf::Vector2f MinimapPosition{1320.f, 140.f};
  inline constexpr sf::Vector2f DetectionHudCenter = MinimapPosition + sf::Vector2f{-60.f, 24.f};
  inline constexpr sf::Vector2f MinimapSize{240.f, 190.f};
  inline constexpr sf::Vector2f MinimapGroundOffset{10.f, 24.f};
  inline constexpr sf::Vector2f MinimapGroundSize{220.f, 120.f};
  inline constexpr unsigned int MinimapTextSize = 11;
  inline constexpr std::array<float, 4> WalkStride{-4.f, 0.f, 4.f, 0.f};
  inline const sf::Color Void{5, 9, 16};
  inline const sf::Color Fog{3, 7, 12};
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
  inline const sf::Color Orange{238, 139, 58};
  inline const sf::Color Yellow{233, 202, 83};
  inline const sf::Color Red{221, 67, 77};
  inline const sf::Color Violet{168, 130, 214};
  inline const sf::Color Outline{9, 15, 23};
  inline const sf::Color Skin{184, 149, 119};
  inline const sf::Color DummyWood{130, 91, 56};
  inline const sf::Color DummyCloth{193, 169, 125};
  inline const sf::Color DummyClothLight{222, 202, 158};
  inline const sf::Color DummyStitch{119, 94, 61};
  inline const sf::Color DummyMetal{57, 59, 56};
  inline const sf::Color DummyMetalEdge{108, 106, 91};
  inline const sf::Color DummyTarget{222, 214, 187};
  inline const std::array<sf::Color, 4> AgentUniforms{{{46, 92, 111}, {75, 101, 76}, {101, 80, 113}, {116, 97, 65}}};
}
