#include "NoTraceMission.h"
#include "Config/NoTraceGameplayConfig.h"
#include <algorithm>

namespace NoTrace {
  const std::array<Contract, 3> &Contracts() {
    static const std::array<Contract, 3> contracts{{
      {ContractId::GhostArchive, "ARCHIVO FANTASMA", "Recupera el archivo y extrae. Elimina cámaras o distrae las patrullas. La fuerza letal está penalizada.", 1, 1, false, false, false, 600.f, 4},
      {ContractId::SilentRelay, "RELEVO SILENCIOSO", "Recupera el archivo y sabotea el repetidor. Una alarma confirmada cancela este contrato.", 2, 1, false, true, false, 540.f, 1},
      {ContractId::DoubleCross, "DOBLE FILO", "Recupera el archivo y extrae. Las órdenes privadas pueden entrar en conflicto; el traidor debe evitar toda alarma.", 1, 1, true, false, true, 600.f, 4}
    }};
    return contracts;
  }

  const Contract &GetContract(ContractId id) {
    const auto index = static_cast<std::size_t>(id);
    return Contracts()[index < Contracts().size() ? index : 0];
  }

  std::string_view RankName(Rank rank) {
    constexpr std::array<std::string_view, 7> names{"D", "C", "B", "A", "S", "S+", "S++ — Maestro"};
    const auto index = static_cast<std::size_t>(rank);
    return names[index < names.size() ? index : 0];
  }

  std::string_view GuardStateName(GuardState state) {
    constexpr std::array<std::string_view, 7> names{"Patrulla", "Sospecha", "Investiga", "Alerta", "Búsqueda", "Incapacitado", "Eliminado"};
    return names[std::min(static_cast<std::size_t>(state), names.size() - 1)];
  }

  std::string_view PrivateOrderText(PrivateOrder order) {
    switch (order) {
    case PrivateOrder::CopyArchive: return "ORDEN PRIVADA: obtén una copia del archivo secundario y extrae.";
    case PrivateOrder::RecoverDevice: return "ORDEN PRIVADA: recupera el dispositivo del repetidor y extrae.";
    case PrivateOrder::Traitor: return "ORDEN PRIVADA: elimina a todos los demás agentes sin que se active una alarma. El equipo gana si logra extraerse.";
    default: return "Sin orden privada.";
    }
  }

  MissionResult EvaluateMission(const Contract &contract, const WorldState &world, PlayerId player, bool success, std::string reason) {
    using namespace GameplayConfig;
    MissionResult result;
    result.reason = std::move(reason);
    const auto agent = std::find_if(world.agents.begin(), world.agents.end(), [player](const Agent &value) { return value.id == player; });
    const bool extracted = agent != world.agents.end() && agent->extracted;
    result.success = success && extracted && world.objectives >= contract.requiredObjectives;
    result.score = world.objectives * MainObjectiveScore + world.secondary * SecondaryObjectiveScore + (extracted ? ExtractionScore : 0);
    bool clean = true;
    for (const auto &event : world.events) {
      // La elegibilidad se evalúa sobre el registro completo, aunque después desaparezca la alarma.
      switch (event.kind) {
      case EventKind::ConfirmedDetection: result.score -= DetectionPenalty; clean = false; break;
      case EventKind::AgentAlarm: result.score -= AlarmPenalty; clean = false; break;
      case EventKind::WeaponLethal: result.score -= contract.lethalAllowed ? NonLethalPenalty : LethalPenalty; clean = false; break;
      case EventKind::WeaponNonLethal: result.score -= NonLethalPenalty; clean = false; break;
      case EventKind::Cqc: result.score -= CqcPenalty; clean = false; break;
      case EventKind::BodyFound: result.score -= BodyFoundPenalty; break;
      case EventKind::PrivateObjective: if (event.actor == player) result.score += PrivateBonus; break;
      default: break;
      }
    }
    result.score = std::max(0, result.score);
    result.maestroEligible = result.success && clean;
    if (!result.success) {
      result.rank = Rank::D;
    } else if (result.maestroEligible) {
      result.rank = Rank::Maestro;
    } else {
      result.rank = Rank::D;
      for (std::size_t index = 0; index < RankThresholds.size(); ++index) {
        if (result.score >= RankThresholds[index]) result.rank = static_cast<Rank>(index + 1);
      }
    }
    return result;
  }
}
