#include "NoTraceSimulation.h"
#include "NoTraceDistractionGeometry.h"
#include <algorithm>

namespace NoTrace {
  void Simulation::UpdateDecoys(float dt) {
    for (auto &disc : world_.decoys) {
      const float before = disc.elapsed;
      const auto previous = DistractionGeometry::DecoyPosition(disc);
      const float previousHeight = disc.elevation + DistractionConfig::ProjectileRadius + DistractionConfig::LandingClearance + DistractionGeometry::DecoyHeight(disc);
      disc.elapsed = std::min(disc.duration, disc.elapsed + dt);
      const auto next = DistractionGeometry::DecoyPosition(disc);
      const float nextHeight = disc.elevation + DistractionConfig::ProjectileRadius + DistractionConfig::LandingClearance + DistractionGeometry::DecoyHeight(disc);
      const float fraction = DistractionGeometry::DecoyCollisionFraction(world_, {previous.x, previous.y, previousHeight}, {next.x, next.y, nextHeight});
      float impactElevation = disc.elevation;
      if (fraction < 1.f) {
        const auto step = next - previous;
        const float clearance = step.length() > 0.f ? DistractionConfig::LandingClearance / step.length() : 0.f;
        disc.to = previous + step * std::max(0.f, fraction - clearance);
        impactElevation = previousHeight + (nextHeight - previousHeight) * fraction;
        disc.blocked = true; disc.elapsed = disc.duration;
      }
      if (before < disc.duration && disc.elapsed >= disc.duration && (fraction < 1.f || DistractionGeometry::DecoyLandingValid(world_, disc.to, disc.elevation))) {
        EmitNoise(disc.to, DistractionConfig::DecoyHearingDistance, disc.actor, 0, SoundKind::Decoy, {}, disc.elevation);
        EmitSound(SoundKind::Decoy, disc.to, DistractionConfig::DecoyHearingDistance, disc.actor, impactElevation);
      }
    }
    std::erase_if(world_.decoys, [](const DecoyProjectile &disc) { return disc.elapsed >= disc.duration; });
  }
}
