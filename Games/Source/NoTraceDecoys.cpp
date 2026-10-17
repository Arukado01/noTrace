#include "NoTraceSimulation.h"
#include "NoTraceDistractionGeometry.h"
#include <algorithm>

namespace NoTrace {
  void Simulation::UpdateDecoys(float dt) {
    for (auto &disc : world_.decoys) {
      const float before = disc.elapsed;
      const auto previous = DistractionGeometry::DecoyPosition(disc);
      disc.elapsed = std::min(disc.duration, disc.elapsed + dt);
      const auto next = DistractionGeometry::DecoyPosition(disc);
      float fraction = 1.f;
      WorldGeometry::ForEachSolid(world_, [&](const sf::FloatRect &bounds) {
        float hit = 1.f;
        if (WorldGeometry::SegmentRect(previous, next, WorldGeometry::Expanded(bounds, DistractionConfig::ProjectileRadius), &hit)) fraction = std::min(fraction, hit);
      });
      if (fraction < 1.f) {
        const auto step = next - previous;
        const float clearance = step.length() > 0.f ? DistractionConfig::LandingClearance / step.length() : 0.f;
        disc.to = previous + step * std::max(0.f, fraction - clearance);
        disc.blocked = true; disc.elapsed = disc.duration;
      }
      if (before < disc.duration && disc.elapsed >= disc.duration) {
        EmitNoise(disc.to, DistractionConfig::DecoyHearingDistance, disc.actor, 0, SoundKind::Decoy);
        EmitSound(SoundKind::Decoy, disc.to, DistractionConfig::DecoyHearingDistance, disc.actor);
      }
    }
    std::erase_if(world_.decoys, [](const DecoyProjectile &disc) { return disc.elapsed >= disc.duration; });
  }
}
