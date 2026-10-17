#pragma once

#include "Types/NoTraceGameplayTypes.h"
#include <array>
#include <string_view>

namespace NoTrace {
  const std::array<Contract, 3> &Contracts();
  const Contract &GetContract(ContractId id);
  std::string_view RankName(Rank rank);
  std::string_view GuardStateName(GuardState state);
  std::string_view PrivateOrderText(PrivateOrder order);
  MissionResult EvaluateMission(const Contract &contract, const WorldState &world, PlayerId player, bool success, std::string reason = {});
}
