#pragma once

#include "Config/NoTraceCharacterArtConfig.h"
#include <array>
#include <cstddef>

namespace NoTrace::SpriteConfig {
  inline constexpr const char *Atlas = "NoTrace/Characters/OperativeAtlas.png";
  inline constexpr const char *FrameTable = "Content/Textures/NoTrace/Characters/OperativeAtlas.frames";
  inline constexpr const char *MuzzleTable = "Content/Textures/NoTrace/Characters/OperativeAtlas.muzzles";
  inline constexpr const char *AtlasMetadata = CharacterArtConfig::AtlasMetadata;
  inline constexpr std::size_t CellSize = 192;
  inline constexpr std::size_t Columns = 64;
  inline constexpr std::size_t Directions = 8;
  inline constexpr std::size_t FamilyFrames = Columns * Directions;
  inline constexpr std::size_t FamilyCount = 15;
  inline constexpr std::size_t FrameCount = FamilyFrames * FamilyCount;
  inline constexpr std::size_t AuthoredFrameCount = 4544;
  inline constexpr std::size_t LegacyFrameCount = 896;
  inline constexpr float FeetX = 96.f;
  inline constexpr float FeetY = 144.f;
  inline constexpr float StandingPixels = 72.f;
  inline constexpr float DirectionHysteresisDegrees = 3.f;
  inline constexpr unsigned int SupportAlpha = 220;
  inline constexpr int SupportBandPixels = 6;
  inline constexpr float StandingTorsoPivotY = 109.f;
  inline constexpr float CrouchedTorsoPivotY = 124.f;
  inline constexpr float ProneTorsoPivotY = 133.f;
  enum class Clip { Idle, Walk, Run, CrouchIdle, CrouchWalk, CrouchRun, LieDown, ProneIdle, Crawl, StandUp, Dive, Roll, Aim, Shoot, Reload, Punch, Kick, Grab, Choke, Push, Throw, WallBang, Damage, Fall, Unconscious, Interact, HideEnter, HideExit, CoverEnter, CoverIdle, CoverMove, CrouchEnter, CrouchExit, DragStart, DragLoop, DragStop, Death, GripVictimEnter, GripVictimHold, GripVictimRelease, SuppressorAttach, SuppressorDetach, WeaponHolster, WeaponDraw, TraversalAttach, TraversalMove, TraversalRelease, TraversalHang, TraversalShimmy, TraversalClimb, TraversalTransfer, TraversalDrop, ReviveRise, Count };
  enum class Family { Locomotion, Posture, Melee, StandingWeapons, Context, CrouchedWeapons, ProneWeapons, Push, Drag, Death, GripVictim, Suppressor, WeaponSwitch, Traversal, Recovery };
  struct Animation { std::size_t family, first, count; float fps; bool loop = true, reverse = false; };
  // Los índices de familias se conservan; cada clip tiene ocho poses propias y las páginas se resuelven por metadatos.
  inline constexpr std::array<Animation, static_cast<std::size_t>(Clip::Count)> Animations{{
    {0, 0, 8, 3.f, true},
    {0, 8, 8, 10.f, true},
    {0, 16, 8, 16.f, true},
    {0, 24, 8, 3.f, true},
    {0, 32, 8, 9.f, true},
    {0, 40, 8, 14.f, true},
    {1, 0, 8, 24.f, false},
    {1, 8, 8, 3.f, true},
    {1, 16, 8, 8.f, true},
    {1, 24, 8, 24.f, false},
    {1, 32, 8, 16.f, false},
    {1, 40, 8, 16.f, false},
    {3, 0, 8, 4.f, true},
    {3, 8, 8, 32.f, false},
    {3, 16, 8, 10.f, false},
    {2, 0, 8, 24.f, false},
    {2, 8, 8, 20.f, false},
    {2, 16, 8, 6.f, true},
    {2, 24, 8, 12.f, true},
    {7, 0, 8, 24.f, false},
    {3, 24, 8, 24.f, false},
    {2, 32, 8, 24.f, false},
    {2, 40, 8, 16.f, false},
    {4, 0, 8, 20.f, false},
    {4, 8, 8, 2.f, true},
    {4, 16, 8, 8.f, true},
    {4, 24, 8, 20.f, false},
    {4, 32, 8, 20.f, false},
    {4, 40, 8, 20.f, false},
    {4, 48, 8, 3.f, true},
    {4, 56, 8, 8.f, true},
    {1, 48, 8, 16.f, false},
    {1, 56, 8, 16.f, false},
    {8, 0, 8, 16.f, false},
    {8, 8, 8, 8.f, true},
    {8, 16, 8, 16.f, false},
    {9, 0, 8, 16.f, false},
    {10, 0, 8, 16.f, false},
    {10, 8, 8, 4.f, true},
    {10, 16, 8, 16.f, false},
    {11, 0, 8, 12.f, false},
    {11, 8, 8, 12.f, false},
    {12, 0, 8, 16.f, false},
    {12, 8, 8, 16.f, false},
    {13, 0, 8, 16.f, false},
    {13, 8, 8, 8.f, true},
    {13, 16, 8, 16.f, false},
    {13, 24, 8, 4.f, true},
    {13, 32, 8, 8.f, true},
    {13, 40, 8, 8.f, true},
    {13, 48, 8, 12.f, false},
    {13, 56, 8, 16.f, false},
    {14, 0, 8, 28.f, false}
  }};
}
