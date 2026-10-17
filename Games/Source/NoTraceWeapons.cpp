#include "NoTraceSimulation.h"
#include "Config/NoTraceAudioConfig.h"
#include "Config/NoTraceWeaponConfig.h"
#include <algorithm>

namespace NoTrace {
  void Simulation::CancelReload(Agent &agent) {
    agent.reloadRemaining = 0.f;
    agent.reloadingWeapon = agent.selectedWeapon;
  }

  void Simulation::BeginReload(Agent &agent) {
    using namespace WeaponConfig;
    if (!IsTactical(world_) || !agent.connected || !agent.alive || agent.incapacitated || agent.hidden || agent.extracted || agent.reloadRemaining > 0.f ||
        agent.cqcHoldingGuard != 0 || agent.cqcHoldingAgent != 0 || agent.cqcHeldBy != 0 || agent.cqcAttackTime > 0.f || agent.draggingGuard >= 0 || agent.puzzle.target != 0) return;
    const bool lethal = agent.selectedWeapon == WeaponKind::Pistol;
    auto &total = lethal ? agent.loadout.lethalAmmo : agent.loadout.nonLethalAmmo;
    const int magazine = lethal ? agent.lethalMagazine : agent.nonLethalMagazine;
    const int capacity = lethal ? PistolCapacity : NonLethalCapacity;
    if (IsTraining(world_)) total = std::max(total, TrainingAmmo);
    if (magazine >= std::min(capacity, total)) { world_.message = total <= 0 ? "No quedan cartuchos para recargar." : "El cargador ya contiene toda la munición disponible."; return; }
    agent.reloadingWeapon = agent.selectedWeapon;
    agent.reloadRemaining = ReloadDuration;
    EmitSound(SoundKind::Reload, agent.position, AudioConfig::QuietRadius, agent.id);
    world_.message = "Recargando: el arma no puede disparar hasta completar la maniobra.";
  }

  void Simulation::UpdateWeapon(Agent &agent, const ActionCommand &command, float dt) {
    using namespace WeaponConfig;
    if (!IsTactical(world_) || !agent.alive || agent.incapacitated || agent.hidden || agent.extracted || agent.puzzle.target != 0 || agent.draggingGuard >= 0 ||
        agent.cqcHoldingGuard != 0 || agent.cqcHoldingAgent != 0 || agent.cqcHeldBy != 0 || agent.cqcAttackTime > 0.f || command.cqc || command.cqcHeld || command.interact || command.tool) {
      CancelReload(agent); return;
    }
    if (command.weaponChoice >= 0 && command.weaponChoice <= 1) {
      const auto choice = static_cast<WeaponKind>(command.weaponChoice);
      if (choice != agent.selectedWeapon) { CancelReload(agent); agent.selectedWeapon = choice; agent.reloadingWeapon = choice; }
    }
    if (agent.reloadRemaining > 0.f) {
      agent.reloadRemaining = std::max(0.f, agent.reloadRemaining - dt);
      if (agent.reloadRemaining == 0.f) {
        auto &magazine = agent.reloadingWeapon == WeaponKind::Pistol ? agent.lethalMagazine : agent.nonLethalMagazine;
        const int total = agent.reloadingWeapon == WeaponKind::Pistol ? agent.loadout.lethalAmmo : agent.loadout.nonLethalAmmo;
        magazine = std::min(agent.reloadingWeapon == WeaponKind::Pistol ? PistolCapacity : NonLethalCapacity, total);
        world_.message = "Recarga completada.";
      }
    }
    if (command.reloadWeapon) BeginReload(agent);
  }
}
