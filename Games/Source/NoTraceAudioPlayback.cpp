#include "NoTraceAudioPlayback.h"
#include "Config/NoTraceAudioConfig.h"
#include "Core/EngineContext.h"
#include "NoTraceVisibility.h"
#include "NoTraceAudioGeometry.h"
#include <algorithm>
#include <limits>

namespace NoTrace {
  AudioPlayback::AudioPlayback(EngineContext &context) : context_(context) {
    lastVariants_.fill(std::numeric_limits<std::size_t>::max());
    context_.audio.SetVoiceLimit(AudioConfig::MaximumVoices);
  }

  AudioPlayback::~AudioPlayback() { StopAmbience(); }

  bool AudioPlayback::Play(SoundKind kind, float volume, float pan, float pitch) {
    const auto index = static_cast<std::size_t>(kind);
    if (index >= lastVariants_.size()) return false;
    const auto &profile = AudioConfig::Profile(kind);
    if (profile.count == 0) return false;
    const auto previous = lastVariants_[index];
    std::size_t variant = 0;
    if (profile.count > 1) {
      variant = static_cast<std::size_t>(context_.random.Int(0, static_cast<int>(profile.count) - (previous < profile.count ? 2 : 1)));
      if (previous < profile.count && variant >= previous) ++variant;
    }
    const auto buffer = context_.resources.FetchSound(AudioConfig::VariantFile(kind, variant));
    if (!buffer) return false;
    AudioManager::PlaybackSettings settings;
    settings.bus = profile.loop ? AudioManager::Bus::Ambience : AudioManager::Bus::Effects;
    settings.priority = static_cast<AudioManager::Priority>(profile.priority);
    settings.volume = volume * profile.gain; settings.pan = pan; settings.pitch = pitch;
    settings.loop = profile.loop; settings.maximumInstances = profile.maximumInstances;
    const auto voice = context_.audio.Play(*buffer, "NoTrace." + std::string(AudioConfig::Names[index]), settings);
    if (voice == 0) return false;
    lastVariants_[index] = variant;
    return true;
  }

  bool AudioPlayback::PlayEvent(const SoundEvent &event, const WorldState &world, const Agent &listener, std::optional<sf::Vector2f> cameraRight) {
    if (!Visibility::SoundAudible(world, listener, event)) return false;
    if (AudioConfig::IsSecurityCue(event.kind)) return Play(event.kind, 55.f);
    const float distance = WorldGeometry::SpatialDistance(WorldGeometry::Feet(listener), {event.position.x, event.position.y, event.elevation});
    const float gain = std::clamp(1.f - distance / std::max(1.f, event.radius) * .75f, .15f, 1.f);
    const float wall = AudioGeometry::Occluded(world, listener, event.position, event.elevation) ? AudioConfig::WallHearingFactor : 1.f;
    const float pan = AudioGeometry::Pan(event.position - listener.position, event.radius, cameraRight);
    return Play(event.kind, 65.f * gain * wall, pan);
  }

  void AudioPlayback::UpdateAmbience(const WorldState &world, PlayerId listener) {
    const auto agent = std::ranges::find(world.agents, listener, &Agent::id);
    if (agent == world.agents.end() || agent->extracted || world.phase == GamePhase::Results) { StopAmbience(); return; }
    constexpr std::array kinds{SoundKind::AmbientRoom, SoundKind::AmbientVentilation, SoundKind::AmbientElectrical, SoundKind::ElevatorMove};
    std::array volumes{18.f, 0.f, 0.f, 0.f};
    for (const auto &object : world.objects) {
      if (!object.active || (object.kind != ObjectKind::Computer && object.kind != ObjectKind::CameraPanel && object.kind != ObjectKind::Armory)) continue;
      const float distance = WorldGeometry::SpatialDistance(WorldGeometry::Feet(*agent), {object.position.x, object.position.y, object.elevation});
      const float wall = AudioGeometry::Occluded(world, *agent, object.position, object.elevation) ? AudioConfig::WallHearingFactor : 1.f;
      const auto channel = object.kind == ObjectKind::Armory ? 1u : 2u;
      volumes[channel] = std::max(volumes[channel], std::clamp(1.f - distance / 340.f, 0.f, 1.f) * 22.f * wall);
    }
    for (const auto &lift : world.elevators) if (lift.phase == ElevatorPhase::Moving) {
      const auto cabin = std::ranges::find(world.walkSurfaces, lift.cabinSupport, &WalkSurface::id);
      if (cabin == world.walkSurfaces.end()) continue;
      const auto position = cabin->bounds.position + cabin->bounds.size * .5f;
      const float vertical = lift.elevation - agent->elevation;
      const float distance = std::sqrt((position - agent->position).lengthSquared() + vertical * vertical);
      const float wall = AudioGeometry::Occluded(world, *agent, position, lift.elevation) ? AudioConfig::WallHearingFactor : 1.f;
      volumes[3] = std::max(volumes[3], std::clamp(1.f - distance / 360.f, 0.f, 1.f) * 30.f * wall);
    }
    for (std::size_t index = 0; index < kinds.size(); ++index) {
      if (index == 3 && volumes[index] <= 0.f) { context_.audio.Stop(ambience_[index]); ambience_[index] = 0; continue; }
      if (context_.audio.IsPlaying(ambience_[index])) { context_.audio.SetVoiceVolume(ambience_[index], volumes[index]); continue; }
      const auto buffer = context_.resources.FetchSound(AudioConfig::File(kinds[index]));
      if (!buffer) continue;
      AudioManager::PlaybackSettings settings;
      settings.bus = AudioManager::Bus::Ambience; settings.priority = AudioManager::Priority::Background;
      settings.volume = volumes[index]; settings.loop = true; settings.maximumInstances = 1;
      ambience_[index] = context_.audio.Play(*buffer, "NoTrace." + std::string(AudioConfig::Names[static_cast<std::size_t>(kinds[index])]), settings);
    }
  }

  void AudioPlayback::StopAmbience() {
    for (auto &voice : ambience_) { context_.audio.Stop(voice); voice = 0; }
  }
}
