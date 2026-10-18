#pragma once

#include "Types/NoTraceGameplayTypes.h"
#include <string>

namespace NoTrace::Level {
  void BuildMilitary(WorldState &world);
  std::string GeometryHash(const WorldState &world);
  bool CanUseElevator(const WorldState &world, const Agent &agent, const Interactable &object);
  std::string ElevatorPrompt(const WorldState &world, const Agent &agent, const Interactable &object);
}
