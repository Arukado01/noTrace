#pragma once

#include "Config/NoTraceWeaponConfig.h"
#include <cstddef>

namespace NoTrace::CharacterArtConfig {
  inline constexpr const char *TextureRoot = "NoTrace/Characters/V2/";
  inline constexpr const char *AtlasMetadata = "Content/Textures/NoTrace/Characters/V2/CharacterAtlasMetadata.json";
  inline constexpr const char *RuntimeMetadata = "Content/Textures/NoTrace/Characters/V2/CharacterAtlasRuntime.json";
  inline constexpr std::size_t FrameCapacity = 15 * 8 * 64;
  inline constexpr float StandingPixels = 72.f;
  inline constexpr float WeaponGestureDuration = WeaponConfig::EquipmentGestureDuration;
}
