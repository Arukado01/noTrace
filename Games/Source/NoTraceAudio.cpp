#include "NoTraceSimulation.h"
#include "Config/NoTraceAudioConfig.h"
#include <cmath>

namespace NoTrace {
  void Simulation::EmitSound(SoundKind kind, sf::Vector2f position, float radius, PlayerId actor) {
    if (!std::isfinite(radius) || radius <= 0.f) return;
    if (world_.sounds.size() >= AudioConfig::MaximumCues) world_.sounds.erase(world_.sounds.begin());
    world_.sounds.push_back({nextSound_++, kind, position, radius, AudioConfig::CueLifetime, actor});
  }
}
