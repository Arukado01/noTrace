#include "NoTraceSimulation.h"
#include "Config/NoTraceAudioConfig.h"
#include <cmath>

namespace NoTrace {
  float Simulation::EmissionElevation(sf::Vector2f position, PlayerId actor, std::uint32_t guardId) const {
    if (actor != 0) for (const auto &agent : world_.agents) if (agent.id == actor) return agent.elevation;
    if (guardId != 0) for (const auto &guard : world_.guards) if (guard.id == guardId) return guard.elevation;
    for (const auto &object : world_.objects) if (object.position == position) return object.elevation;
    for (const auto &guard : world_.guards) if (guard.position == position) return guard.elevation;
    for (const auto &agent : world_.agents) if (agent.position == position) return agent.elevation;
    return 0.f;
  }

  void Simulation::EmitSound(SoundKind kind, sf::Vector2f position, float radius, PlayerId actor, float elevation) {
    if (static_cast<std::size_t>(kind) >= static_cast<std::size_t>(SoundKind::Count) || !std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(radius) || radius <= 0.f) return;
    if (std::isnan(elevation)) elevation = EmissionElevation(position, actor);
    if (!std::isfinite(elevation) || elevation < AudioConfig::MinimumElevation || elevation > AudioConfig::MaximumElevation) return;
    if (world_.sounds.size() >= AudioConfig::MaximumCues) world_.sounds.erase(world_.sounds.begin());
    world_.sounds.push_back({nextSound_++, kind, position, radius, AudioConfig::CueLifetime, actor, elevation});
  }
}
