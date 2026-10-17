#pragma once

#include "Types/NoTraceGameplayTypes.h"

namespace NoTrace::Visibility {
  bool HasLineOfSight(const WorldState &world, sf::Vector2f from, sf::Vector2f to, Stance stance = Stance::Standing, std::uint32_t ignoredObject = 0);
  bool IsVisible(const WorldState &world, const Agent &observer, sf::Vector2f point, Stance targetStance = Stance::Standing);
  bool GuardVisible(const WorldState &world, const Agent &observer, const Guard &guard);
  bool ObjectVisible(const WorldState &world, const Agent &observer, const Interactable &object);
  bool FootstepAudible(const WorldState &world, const Agent &observer, const FootstepCue &cue);
  VisibilityGrid BuildGrid(const WorldState &world, const Agent &observer);
}
