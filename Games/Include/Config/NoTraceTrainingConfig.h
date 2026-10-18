#pragma once

#include "Config/NoTraceCombatConfig.h"
#include "Config/NoTraceGameplayConfig.h"
#include <SFML/System/Vector2.hpp>
#include <cstdint>
#include <cstddef>

namespace NoTrace::TrainingConfig {
  inline constexpr sf::Vector2f WorldSize{1900.f, 1060.f};
  inline constexpr std::uint32_t FirstDummyId = 1000;
  inline constexpr unsigned int DummyCount = 3;
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
}
