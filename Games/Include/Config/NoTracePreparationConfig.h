#pragma once

#include "Types/NoTraceGameplayTypes.h"
#include <array>
#include <string_view>

namespace NoTrace::PreparationConfig {
  inline constexpr std::array<Loadout, 4> Presets{{{4, 4, 3, 2}, {0, 0, 6, 6}, {0, 8, 4, 4}, {2, 2, 5, 6}}};
  inline constexpr std::array<std::string_view, 4> Names{"ESTÁNDAR", "INFILTRACIÓN", "NO LETAL", "SABOTAJE"};
}
