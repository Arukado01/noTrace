#pragma once

#include "Managers/AudioManager.h"
#include "Types/NoTraceGameplayTypes.h"

namespace NoTrace::MusicConfig {
  inline constexpr float SceneFadeOutSeconds = 1.25f;
  inline constexpr float SceneFadeInSeconds = 1.75f;
  inline constexpr float IntensitySeconds = 4.5f;
  inline constexpr float LayerSyncToleranceSeconds = .08f;
  inline const AudioManager::MusicSettings Menu{
    "NoTrace/OperacionEncubierta.ogg", {}, 58.f, 0.f, SceneFadeOutSeconds, SceneFadeInSeconds, IntensitySeconds, LayerSyncToleranceSeconds};
  inline const AudioManager::MusicSettings Lobby{
    "NoTrace/PreparacionTactica.ogg", {}, 60.f, 0.f, SceneFadeOutSeconds, SceneFadeInSeconds, IntensitySeconds, LayerSyncToleranceSeconds};
  inline const AudioManager::MusicSettings Mission{
    "NoTrace/InfiltracionActiva.ogg", "NoTrace/InfiltracionTension.ogg", 48.f, 35.f,
    SceneFadeOutSeconds, SceneFadeInSeconds, IntensitySeconds, LayerSyncToleranceSeconds};

  inline constexpr float Intensity(SecurityAlert alert) {
    switch (alert) {
    case SecurityAlert::Yellow: return .12f;
    case SecurityAlert::Orange: return .38f;
    case SecurityAlert::Red: return .7f;
    default: return 0.f;
    }
  }
}
