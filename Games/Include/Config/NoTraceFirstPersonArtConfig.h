#pragma once

#include <cstddef>

namespace NoTrace::FirstPersonArtConfig {
  inline constexpr const char *Actors = "NoTrace/FirstPerson/Actors.png";
  inline constexpr const char *Viewmodels = "NoTrace/FirstPerson/Viewmodels.png";
  inline constexpr const char *Metadata = "Content/Textures/NoTrace/FirstPerson/Runtime.json";
  inline constexpr unsigned int ActorCell = 128, ActorColumns = 9, ActorRows = 4;
  inline constexpr unsigned int ViewmodelWidth = 256, ViewmodelHeight = 160, ViewmodelColumns = 4, ViewmodelRows = 4;
  inline constexpr float StandingPixels = 112.f, FeetX = 64.f, FeetY = 124.f;
  enum class ActorState : unsigned int { Idle, WalkA, WalkB, Fire, Hurt, Crouch, Prone, Down, Dead };
  enum class ActorVariant : unsigned int { GuardA, GuardB, Ally, Training };
  enum class ViewmodelState : unsigned int { Idle, Fire, ReloadOut, ReloadIn };
}
