#pragma once

#include "Types/NoTraceGameplayTypes.h"
#include "Config/NoTraceVisibilityConfig.h"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace NoTrace::Visibility {
  bool HasLineOfSight(const WorldState &world, sf::Vector2f from, sf::Vector2f to, Stance stance = Stance::Standing, std::uint32_t ignoredObject = 0);
  bool IsGeometricallyVisible(const WorldState &world, const Agent &observer, sf::Vector2f point, Stance targetStance = Stance::Standing);
  bool IsVisible(const WorldState &world, const Agent &observer, sf::Vector2f point, Stance targetStance = Stance::Standing);
  bool GuardVisible(const WorldState &world, const Agent &observer, const Guard &guard);
  bool AgentExposed(const WorldState &world, sf::Vector2f from, const Agent &target, std::uint32_t ignoredObject = 0, float eyeHeight = CombatConfig::StandingHeight - CombatConfig::HeadHeight * 0.5f);
  bool ObjectVisible(const WorldState &world, const Agent &observer, const Interactable &object);
  bool FootstepAudible(const WorldState &world, const Agent &observer, const FootstepCue &cue);
  bool NoiseAudible(const WorldState &world, const Agent &observer, const NoiseEvent &noise);
  bool SoundAudible(const WorldState &world, const Agent &observer, const SoundEvent &sound);
  VisibilityGrid BuildGrid(const WorldState &world, const Agent &observer);

  inline void BuildVisualBoundary(const WorldState &world, const Agent &observer, VisibilityGrid &grid) {
    grid.visualOrigin = observer.position;
    if (!IsTactical(world)) { grid.visualRadii.clear(); return; }
    grid.visualRadii.resize(VisibilityConfig::VisualRayCount);
    for (std::size_t ray = 0; ray < grid.visualRadii.size(); ++ray) {
      const float angle = 2.f * std::numbers::pi_v<float> * static_cast<float>(ray) / grid.visualRadii.size();
      const sf::Vector2f direction{std::cos(angle), std::sin(angle)};
      float visible = 0.f, hidden = VisibilityConfig::StandingRange;
      for (unsigned int iteration = 0; iteration < VisibilityConfig::VisualRayIterations; ++iteration) {
        const float distance = (visible + hidden) * 0.5f;
        if (IsGeometricallyVisible(world, observer, observer.position + direction * distance, Stance::Prone)) visible = distance;
        else hidden = distance;
      }
      grid.visualRadii[ray] = visible;
    }
  }
}
