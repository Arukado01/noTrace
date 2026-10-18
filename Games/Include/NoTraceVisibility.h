#pragma once

#include "Types/NoTraceGameplayTypes.h"
#include "Config/NoTraceVisibilityConfig.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <limits>

namespace NoTrace::WorldGeometry { class PreparedOccluders; }

namespace NoTrace::Visibility {
  bool UsesFieldOfView(const WorldState &world, const Agent &observer);
  bool HasLineOfSight(const WorldState &world, sf::Vector2f from, sf::Vector2f to, Stance stance = Stance::Standing, std::uint32_t ignoredObject = 0);
  bool IsGeometricallyVisible(const WorldState &world, const Agent &observer, sf::Vector2f point, Stance targetStance = Stance::Standing, float targetElevation = std::numeric_limits<float>::quiet_NaN(), const WorldGeometry::PreparedOccluders *geometry = nullptr);
  bool IsVisible(const WorldState &world, const Agent &observer, sf::Vector2f point, Stance targetStance = Stance::Standing);
  bool GuardVisible(const WorldState &world, const Agent &observer, const Guard &guard, const WorldGeometry::PreparedOccluders *geometry = nullptr);
  bool AgentExposed(const WorldState &world, sf::Vector2f from, const Agent &target, std::uint32_t ignoredObject = 0, float eyeHeight = CombatConfig::StandingHeight - CombatConfig::HeadHeight * 0.5f);
  bool ObjectVisible(const WorldState &world, const Agent &observer, const Interactable &object, const WorldGeometry::PreparedOccluders *geometry = nullptr);
  bool FootstepAudible(const WorldState &world, const Agent &observer, const FootstepCue &cue);
  bool NoiseAudible(const WorldState &world, const Agent &observer, const NoiseEvent &noise);
  bool SoundAudible(const WorldState &world, const Agent &observer, const SoundEvent &sound);
  VisibilityGrid BuildGrid(const WorldState &world, const Agent &observer);

  void BuildVisualBoundary(const WorldState &world, const Agent &observer, VisibilityGrid &grid);
}
