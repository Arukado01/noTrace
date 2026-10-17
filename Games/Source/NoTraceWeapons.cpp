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
        agent.cqcHoldingGuard != 0 || agent.cqcHoldingAgent != 0 || agent.cqcHeldBy != 0 || agent.cqcAttackTime > 0.f || agent.cqcApproachGuard != 0 || agent.cqcApproachAgent != 0 || agent.maneuver.kind != ManeuverKind::None || agent.draggingGuard >= 0 || agent.puzzle.target != 0) return;
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
        agent.cqcHoldingGuard != 0 || agent.cqcHoldingAgent != 0 || agent.cqcHeldBy != 0 || agent.cqcAttackTime > 0.f || agent.cqcApproachGuard != 0 || agent.cqcApproachAgent != 0 || agent.maneuver.kind != ManeuverKind::None || command.cqc || command.cqcHeld || command.interact || command.tool) {
      CancelReload(agent); return;
    }
    if (command.weaponChoice >= 0 && command.weaponChoice <= 1) {
      const auto choice = static_cast<WeaponKind>(command.weaponChoice);
      if (choice != agent.selectedWeapon) {
        CancelReload(agent); agent.selectedWeapon = choice; agent.reloadingWeapon = choice;
        EmitSound(SoundKind::Lock, agent.position, AudioConfig::QuietRadius, agent.id);
      }
    }
    if (command.toggleSuppressor) {
      auto &equipped = agent.selectedWeapon == WeaponKind::Pistol ? agent.pistolSuppressorEquipped : agent.nonLethalSuppressorEquipped;
      const float durability = agent.selectedWeapon == WeaponKind::Pistol ? agent.pistolSuppressor : agent.nonLethalSuppressor;
      equipped = durability > 0.f ? !equipped : false;
      EmitSound(SoundKind::Lock, agent.position, AudioConfig::QuietRadius, agent.id);
      world_.message = durability <= 0.f ? "Silenciador roto; equipa otro en la preparación de una operación." : equipped ? "Silenciador instalado." : "Silenciador retirado; el arma producirá un ruido fuerte.";
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

  void Simulation::ReportWeaponShot(Agent &agent, bool lethal) {
    using namespace WeaponConfig;
    auto &durability = lethal ? agent.pistolSuppressor : agent.nonLethalSuppressor;
    auto &equipped = lethal ? agent.pistolSuppressorEquipped : agent.nonLethalSuppressorEquipped;
    durability = std::clamp(durability, 0.f, MaximumSuppressor);
    const bool suppressed = equipped && durability > 0.f;
    const float radius = suppressed ? lethal ? PistolSuppressedNoise : NonLethalSuppressedNoise : lethal ? PistolNoise : NonLethalNoise;
    const auto sound = suppressed ? SoundKind::Suppressed : SoundKind::Pistol;
    EmitNoise(agent.position, radius, agent.id, 0, sound);
    EmitSound(sound, agent.position, radius, agent.id);
    if (suppressed) {
      durability = std::max(0.f, durability - (lethal ? PistolSuppressorWear : NonLethalSuppressorWear));
      if (durability == 0.f) {
        equipped = false;
        EmitSound(SoundKind::Impact, agent.position, AudioConfig::QuietRadius, agent.id);
        world_.message = "Silenciador agotado: los siguientes disparos serán audibles a mayor distancia.";
      }
    } else if (durability == 0.f) equipped = false;
  }
}
