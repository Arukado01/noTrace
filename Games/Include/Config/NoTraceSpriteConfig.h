#pragma once

#include <array>
#include <cstddef>

namespace NoTrace::SpriteConfig {
  inline constexpr const char *Atlas = "NoTrace/Characters/OperativeAtlas.png";
  inline constexpr const char *FrameTable = "Content/Textures/NoTrace/Characters/OperativeAtlas.frames";
  inline constexpr const char *MuzzleTable = "Content/Textures/NoTrace/Characters/OperativeAtlas.muzzles";
  inline constexpr std::size_t CellSize = 96;
  inline constexpr std::size_t Columns = 16;
  inline constexpr std::size_t Directions = 8;
  inline constexpr std::size_t FamilyFrames = Columns * Directions;
  inline constexpr std::size_t FrameCount = FamilyFrames * 7;
  inline constexpr float FeetX = 48.f;
  inline constexpr float FeetY = 88.f;
  inline constexpr float StandingPixels = 72.f;
  inline constexpr float DirectionHysteresisDegrees = 3.f;
  inline constexpr unsigned int SupportAlpha = 220;
  inline constexpr int SupportBandPixels = 6;
  inline constexpr float StandingTorsoPivotY = 62.f;
  inline constexpr float CrouchedTorsoPivotY = 72.f;
  inline constexpr float ProneTorsoPivotY = 70.f;
  enum class Clip { Idle, Walk, Run, CrouchIdle, CrouchWalk, CrouchRun, LieDown, ProneIdle, Crawl, StandUp, Dive, Roll,
    Aim, Shoot, Reload, Punch, Kick, Grab, Choke, Push, Throw, WallBang, Damage, Fall, Unconscious, Interact,
    HideEnter, HideExit, CoverEnter, CoverIdle, CoverMove, CrouchEnter, CrouchExit, Count };
  struct Animation { std::size_t family, first, count; float fps; bool loop = true, reverse = false; };
  // Los clips comparten ciclos completos compatibles; ninguna dirección depende de un reflejo.
  inline constexpr std::array<Animation, static_cast<std::size_t>(Clip::Count)> Animations{{
    {0, 0, 4, 3.f}, {0, 4, 4, 8.f}, {0, 8, 4, 12.f}, {0, 12, 1, 1.f}, {0, 12, 4, 6.f}, {0, 12, 4, 10.f},
    {1, 0, 4, 12.f, false}, {1, 3, 1, 1.f}, {1, 4, 4, 6.f}, {1, 0, 4, 12.f, false, true},
    {1, 8, 4, 8.f, false}, {1, 12, 4, 8.f, false},
    {3, 0, 4, 4.f}, {3, 4, 4, 16.f, false}, {3, 8, 4, 5.f, false},
    {2, 0, 4, 12.f, false}, {2, 4, 4, 10.f, false}, {2, 8, 4, 5.f}, {2, 12, 4, 6.f},
    {2, 8, 4, 12.f, false}, {3, 12, 4, 12.f, false}, {2, 0, 4, 12.f, false},
    {4, 8, 2, 8.f, false}, {4, 8, 4, 10.f, false}, {4, 11, 1, 1.f}, {4, 4, 4, 6.f},
    {4, 12, 4, 10.f, false}, {4, 12, 4, 10.f, false, true}, {4, 0, 4, 10.f, false}, {4, 1, 1, 1.f}, {4, 0, 4, 6.f},
    {1, 0, 2, 8.f, false}, {1, 0, 2, 8.f, false, true}
  }};
}
