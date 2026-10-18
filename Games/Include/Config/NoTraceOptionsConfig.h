#pragma once

#include <array>
#include <string_view>

namespace NoTrace::OptionsConfig {
  inline constexpr std::string_view EffectsVolumeSaveKey = "NoTrace:EffectsVolume";
  inline constexpr float EffectsVolume = 100.f;
  inline constexpr std::array<unsigned int, 7> FramerateLimits{0, 30, 60, 120, 144, 240, 360};
}
