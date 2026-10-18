#include "NoTraceSession.h"
#include "NoTracePerformance.h"
#include "Config/NoTraceGameplayConfig.h"
#include "Config/NoTraceCombatConfig.h"
#include "Config/NoTraceDistractionConfig.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <unordered_set>
#include <utility>

namespace NoTrace {
  namespace {
    using Json = nlohmann::json;
    using namespace GameplayConfig;

    std::string CleanName(std::string name) {
      std::erase_if(name, [](unsigned char value) { return value < 32 || value == 127; });
      if (name.size() > MaximumNameBytes) {
        name.resize(MaximumNameBytes);
      }
      while (!name.empty()) {
        try {
          static_cast<void>(Json(name).dump());
          return name;
        } catch (const Json::exception &) {
          name.pop_back();
        }
      }
      return "Agente";
    }

    bool ReadUnsigned(const Json &value, std::uint64_t &result) {
      if (value.is_number_unsigned()) {
        result = value.get<std::uint64_t>();
        return true;
      }
      if (value.is_number_integer() && value.get<std::int64_t>() >= 0) {
        result = static_cast<std::uint64_t>(value.get<std::int64_t>());
        return true;
      }
      return false;
    }

    bool ReadVector(const Json &value, float &x, float &y) {
      if (!value.is_array() || value.size() != 2 || !value[0].is_number() || !value[1].is_number()) {
        return false;
      }
      x = value[0].get<float>();
      y = value[1].get<float>();
      if (!std::isfinite(x) || !std::isfinite(y)) {
        return false;
      }
      const float length = std::hypot(x, y);
      if (length > 1.f) {
        x /= length;
        y /= length;
      }
      return true;
    }

    ActionCommand Sanitize(ActionCommand command) {
      const auto normalize = [](float &x, float &y) {
        x = std::isfinite(x) ? x : 0.f;
        y = std::isfinite(y) ? y : 0.f;
        const float length = std::hypot(x, y);
        if (length > 1.f) {
          x /= length;
          y /= length;
        }
      };
      normalize(command.moveX, command.moveY);
      normalize(command.aimX, command.aimY);
      command.aimSlope = std::isfinite(command.aimSlope) ? std::clamp(command.aimSlope, -CombatConfig::MaximumAimSlope, CombatConfig::MaximumAimSlope) : 0.f;
      command.decoyDistance = std::isfinite(command.decoyDistance) ? std::clamp(command.decoyDistance, 0.f, DistractionConfig::MaximumDecoyDistance) : 0.f;
      if (command.puzzleInput < -1 || command.puzzleInput > 16) command.puzzleInput = -1;
      if (command.weaponChoice < -1 || command.weaponChoice > 1) command.weaponChoice = -1;
      return command;
    }

    unsigned int CommandFlags(const ActionCommand &command) {
      return (command.run ? 1u : 0u) | (command.crouch ? 2u : 0u) |
             (command.prone ? 4u : 0u) | (command.crawl ? 8u : 0u) |
             (command.interact ? 16u : 0u) | (command.cqc ? 32u : 0u) |
             (command.fireLethal ? 64u : 0u) | (command.fireNonLethal ? 128u : 0u) |
             (command.throwDecoy ? 256u : 0u) | (command.knock ? 512u : 0u) |
             (command.dragBody ? 1024u : 0u) | (command.tool ? 2048u : 0u) |
             (command.coverToggle ? 4096u : 0u) | (command.peekLeft ? 8192u : 0u) | (command.peekRight ? 16384u : 0u) | (command.prepareDecoy ? 32768u : 0u) |
             (command.cqcHeld ? 65536u : 0u) | (command.push ? 131072u : 0u) | (command.reloadWeapon ? 262144u : 0u) | (command.fireSelected ? 524288u : 0u) | (command.aiming ? 1048576u : 0u) | (command.dive ? 2097152u : 0u) | (command.roll ? 4194304u : 0u) | (command.toggleSuppressor ? 8388608u : 0u) | (command.puzzleHeld ? 16777216u : 0u) |
             (command.traverse ? 1u << 25 : 0u) | (command.drop ? 1u << 26 : 0u) | (command.spectateNext ? 1u << 27 : 0u) | (command.spectatePrevious ? 1u << 28 : 0u) | (command.orientBodyToAim ? 1u << 29 : 0u);
    }

    bool ReadCommand(const Json &message, ActionCommand &command) {
      std::uint64_t flags = 0;
      if (!ReadVector(message.at("move"), command.moveX, command.moveY) ||
          !ReadVector(message.at("aim"), command.aimX, command.aimY) ||
          !ReadUnsigned(message.at("flags"), flags) || flags > (1u << 30) - 1u || !message.at("slope").is_number() || !message.at("decoyDistance").is_number()) {
        return false;
      }
      command.run = (flags & 1) != 0;
      command.crouch = (flags & 2) != 0;
      command.prone = (flags & 4) != 0;
      command.crawl = (flags & 8) != 0;
      command.interact = (flags & 16) != 0;
      command.cqc = (flags & 32) != 0;
      command.fireLethal = (flags & 64) != 0;
      command.fireNonLethal = (flags & 128) != 0;
      command.throwDecoy = (flags & 256) != 0;
      command.knock = (flags & 512) != 0;
      command.dragBody = (flags & 1024) != 0;
      command.tool = (flags & 2048) != 0;
      command.coverToggle = (flags & 4096) != 0;
      command.peekLeft = (flags & 8192) != 0;
      command.peekRight = (flags & 16384) != 0;
      command.prepareDecoy = (flags & 32768) != 0;
      command.cqcHeld = (flags & 65536) != 0;
      command.push = (flags & 131072) != 0;
      command.reloadWeapon = (flags & 262144) != 0; command.fireSelected = (flags & 524288) != 0;
      command.aiming = (flags & 1048576) != 0; command.dive = (flags & 2097152) != 0; command.roll = (flags & 4194304) != 0;
      command.toggleSuppressor = (flags & 8388608) != 0; command.puzzleHeld = (flags & 16777216) != 0;
      command.traverse = (flags & (1u << 25)) != 0; command.drop = (flags & (1u << 26)) != 0;
      command.spectateNext = (flags & (1u << 27)) != 0; command.spectatePrevious = (flags & (1u << 28)) != 0;
      command.orientBodyToAim = (flags & (1u << 29)) != 0;
      command.aimSlope = message.at("slope").get<float>();
      if (!std::isfinite(command.aimSlope) || std::abs(command.aimSlope) > CombatConfig::MaximumAimSlope) return false;
      command.decoyDistance = message.at("decoyDistance").get<float>();
      if (!std::isfinite(command.decoyDistance) || command.decoyDistance < 0.f || command.decoyDistance > DistractionConfig::MaximumDecoyDistance) return false;
      const auto puzzle = message.value("puzzleInput", Json(-1));
      if (!puzzle.is_number_integer() || puzzle < -1 || puzzle > 16) return false;
      command.puzzleInput = puzzle.get<int>();
      std::uint64_t target = 0;
      if (command.puzzleInput < -1 || command.puzzleInput > 16 || !ReadUnsigned(message.value("puzzleTarget", Json(0)), target) || target > 1000000) return false;
      command.puzzleTarget = static_cast<std::uint32_t>(target);
      const auto weapon = message.value("weaponChoice", Json(-1)); if (!weapon.is_number_integer() || weapon < -1 || weapon > 1) return false;
      command.weaponChoice = weapon.get<int>(); if (command.weaponChoice < -1 || command.weaponChoice > 1) return false;
      return true;
    }

    void ClearPulses(ActionCommand &command) {
      command.crouch = false;
      command.prone = false;
      command.interact = false;
      command.cqc = false;
      command.push = false;
      command.fireLethal = false;
      command.fireNonLethal = false;
      command.throwDecoy = false;
      command.knock = false;
      command.tool = false;
      command.coverToggle = false;
      command.puzzleInput = -1; if (!command.puzzleHeld) command.puzzleTarget = 0;
      command.reloadWeapon = false; command.fireSelected = false; command.weaponChoice = -1;
      command.dive = false; command.roll = false; command.toggleSuppressor = false;
      command.traverse = command.drop = command.spectateNext = command.spectatePrevious = false;
    }

    void MergePulses(ActionCommand &command, const ActionCommand &previous) {
      command.reloadWeapon |= previous.reloadWeapon;
      command.dive |= previous.dive; command.roll |= previous.roll; command.toggleSuppressor |= previous.toggleSuppressor;
      command.traverse |= previous.traverse; command.drop |= previous.drop;
      command.spectateNext |= previous.spectateNext; command.spectatePrevious |= previous.spectatePrevious;
      if (command.weaponChoice == -1) command.weaponChoice = previous.weaponChoice;
      if (command.puzzleInput == -1 && previous.puzzleInput != -1) { command.puzzleInput = previous.puzzleInput; command.puzzleTarget = previous.puzzleTarget; }
      if ((previous.fireLethal || previous.fireNonLethal || previous.fireSelected) && !command.fireLethal && !command.fireNonLethal && !command.fireSelected) {
        // El disparo pendiente conserva la dirección capturada al pulsar.
        command.aimX = previous.aimX;
        command.aimY = previous.aimY;
        command.aimSlope = previous.aimSlope;
      }
      if (previous.throwDecoy && !command.throwDecoy) {
        command.aimX = previous.aimX; command.aimY = previous.aimY;
        command.decoyDistance = previous.decoyDistance;
      }
      command.crouch |= previous.crouch;
      command.prone |= previous.prone;
      command.interact |= previous.interact;
      command.cqc |= previous.cqc;
      command.fireSelected |= previous.fireSelected;
      command.push |= previous.push;
      command.fireLethal |= previous.fireLethal;
      command.fireNonLethal |= previous.fireNonLethal;
      command.throwDecoy |= previous.throwDecoy;
      command.knock |= previous.knock;
      command.tool |= previous.tool;
      command.coverToggle |= previous.coverToggle;
    }

    Loadout ClampLoadout(Loadout loadout) {
      loadout.lethalAmmo = std::clamp(loadout.lethalAmmo, 0, MaximumAmmo);
      loadout.nonLethalAmmo = std::clamp(loadout.nonLethalAmmo, 0, MaximumAmmo);
      loadout.decoys = std::clamp(loadout.decoys, 0, MaximumDecoys);
      loadout.tools = std::clamp(loadout.tools, 0, MaximumTools);
      return loadout;
    }

    bool ReadLoadout(const Json &message, Loadout &loadout) {
      std::array<std::uint64_t, 4> values{};
      const std::array keys{"lethal", "nonlethal", "decoys", "tools"};
      for (std::size_t index = 0; index < keys.size(); ++index) {
        if (!ReadUnsigned(message.at(keys[index]), values[index])) {
          return false;
        }
      }
      if (values[0] > MaximumAmmo || values[1] > MaximumAmmo || values[2] > MaximumDecoys || values[3] > MaximumTools) {
        return false;
      }
      loadout = {static_cast<int>(values[0]), static_cast<int>(values[1]), static_cast<int>(values[2]), static_cast<int>(values[3])};
      return true;
    }
  }

  Session::Session(NetworkManager &network, RandomManager &random) : network_(network), random_(random) {
    simulation_.ResetBase();
  }

  void Session::Stop() {
    network_.Stop();
    role_ = NetworkRole::Offline;
    active_ = false;
    connected_ = false;
    localPlayer_ = NetworkConfig::HostId;
    lobby_.clear();
    participants_.clear();
    deploymentLoadouts_.clear();
    elapsed_ = 0.f;
    lastHostMessage_ = 0.f;
    commandAccumulator_ = 0.f;
    snapshotAccumulator_ = 0.f;
    snapshotPending_ = false;
    simulationAccumulator_ = 0.f;
    commandSequence_ = 0;
    snapshotSequence_ = 0;
    receivedSnapshot_ = 0;
    epoch_ = 0;
    localCommand_ = {};
    status_ = "Sin sesión";
    simulation_.ResetBase();
  }

  void Session::StartSolo(const std::string &name) {
    Stop();
    localName_ = CleanName(name);
    active_ = connected_ = true;
    lobby_.push_back({localPlayer_, localName_, true});
    simulation_.ResetBase({{localPlayer_, localName_}});
    status_ = "Operación individual";
  }

  bool Session::Host(const std::string &name, std::uint16_t port) {
    Stop();
    localName_ = CleanName(name);
    if (!network_.Host(port)) {
      status_ = network_.GetStatus();
      return false;
    }
    role_ = NetworkRole::Host;
    active_ = connected_ = true;
    epoch_ = static_cast<std::uint32_t>(random_.Int(1, std::numeric_limits<int>::max()));
    lobby_.push_back({localPlayer_, localName_, false});
    simulation_.ResetBase({{localPlayer_, localName_}});
    status_ = network_.GetStatus();
    return true;
  }

  bool Session::Join(const std::string &ip, std::uint16_t port, const std::string &name) {
    Stop();
    localName_ = CleanName(name);
    if (!network_.Join(ip, port)) {
      status_ = network_.GetStatus();
      return false;
    }
    role_ = NetworkRole::Client;
    active_ = true;
    localPlayer_ = 0;
    simulation_.ResetBase({});
    status_ = "Conectando con el anfitrión...";
    return true;
  }

  const WorldState &Session::GetWorld() const { return simulation_.GetState(); }
  PlayerId Session::GetLocalPlayerId() const { return localPlayer_; }
  bool Session::IsHost() const { return role_ == NetworkRole::Host; }
  bool Session::IsClient() const { return role_ == NetworkRole::Client; }
  bool Session::IsConnected() const { return connected_; }
  const std::string &Session::GetStatus() const { return status_; }
  const std::vector<LobbyPlayer> &Session::GetLobbyPlayers() const { return lobby_; }

  bool Session::CanDeploy() const {
    if (!active_ || !connected_ || IsClient() || GetWorld().phase != GamePhase::Base || lobby_.empty()) {
      return false;
    }
    return role_ == NetworkRole::Offline ||
           std::ranges::all_of(lobby_, [](const LobbyPlayer &player) { return player.ready; });
  }

  void Session::Submit(ActionCommand command) {
    if (!active_ || !connected_) {
      return;
    }
    command = Sanitize(command);
    MergePulses(command, localCommand_);
    localCommand_ = command;
  }

  void Session::SetReady(bool ready) {
    if (!active_ || !connected_ || GetWorld().phase != GamePhase::Base) {
      return;
    }
    if (IsClient()) {
      network_.Send(NetworkConfig::HostId, Json{{"type", "ready"}, {"ready", ready}}.dump());
      return;
    }
    for (auto &player : lobby_) {
      if (player.id == localPlayer_) {
        player.ready = ready;
      }
    }
    snapshotPending_ = true;
  }

  void Session::SetContract(ContractId contract) {
    if (IsClient() || !active_ || GetWorld().phase != GamePhase::Base ||
        static_cast<unsigned int>(contract) >= Contracts().size()) {
      return;
    }
    if (contract != GetWorld().contract) {
      simulation_.SetContract(contract);
      for (auto &player : lobby_) {
        player.ready = role_ == NetworkRole::Offline;
      }
      snapshotPending_ = true;
    }
  }

  void Session::SetLoadout(Loadout loadout) {
    if (!active_ || !connected_ || GetWorld().phase != GamePhase::Base) {
      return;
    }
    loadout = ClampLoadout(loadout);
    if (IsClient()) {
      network_.Send(NetworkConfig::HostId, Json{{"type", "loadout"}, {"lethal", loadout.lethalAmmo},
                    {"nonlethal", loadout.nonLethalAmmo}, {"decoys", loadout.decoys}, {"tools", loadout.tools}}.dump());
      return;
    }
    simulation_.SetLoadout(localPlayer_, loadout);
    if (role_ != NetworkRole::Offline) {
      SetReady(false);
    }
    snapshotPending_ = true;
  }

  void Session::ClearCommands() {
    localCommand_ = {};
    simulationAccumulator_ = 0.f;
    for (auto &[id, participant] : participants_) {
      participant.command = {};
    }
  }

  void Session::Deploy() {
    if (!CanDeploy()) {
      status_ = IsClient() ? "El anfitrión inicia la operación" : "Todos los agentes deben estar listos en la base";
      return;
    }
    ++epoch_;
    ClearCommands();
    deploymentLoadouts_.clear();
    for (const auto &agent : GetWorld().agents) deploymentLoadouts_.emplace(agent.id, agent.operationLoadout);
    simulation_.Deploy(static_cast<std::uint32_t>(random_.Int(1, std::numeric_limits<int>::max())));
    status_ = "Operación en curso";
    snapshotPending_ = true;
  }

  void Session::ReturnToBase() {
    if (!active_ || IsClient()) {
      return;
    }
    ++epoch_;
    ClearCommands();
    simulation_.ReturnToBase();
    for (auto &player : lobby_) {
      player.ready = role_ == NetworkRole::Offline;
    }
    status_ = role_ == NetworkRole::Offline ? "Base CIO: operación individual" : "Base CIO: esperando preparación";
    snapshotPending_ = true;
  }

  bool Session::RestartOperation() {
    if (!active_ || !connected_ || IsClient() || (GetWorld().phase != GamePhase::Mission && GetWorld().phase != GamePhase::Results)) {
      return false;
    }
    const auto seed = GetWorld().seed;
    ++epoch_;
    ClearCommands();
    simulation_.ReturnToBase();
    for (const auto &[id, loadout] : deploymentLoadouts_) simulation_.SetLoadout(id, loadout);
    simulation_.Deploy(seed);
    status_ = "Operación reiniciada";
    snapshotPending_ = true;
    return true;
  }

  void Session::Reject(PeerId peer, const std::string &reason) {
    const auto found = participants_.find(peer);
    if (found == participants_.end() || found->second.rejected) {
      return;
    }
    network_.Send(peer, Json{{"type", "reject"}, {"reason", reason}}.dump());
    found->second.rejected = true;
    found->second.rejectTime = elapsed_;
    found->second.command = {};
  }

  void Session::RemoveParticipant(PeerId peer) {
    const auto found = participants_.find(peer);
    if (found == participants_.end()) {
      return;
    }
    if (found->second.accepted) {
      simulation_.DisconnectPlayer(peer);
      std::erase_if(lobby_, [peer](const LobbyPlayer &player) { return player.id == peer; });
      status_ = "Un agente se ha desconectado; su mando queda neutralizado";
    }
    participants_.erase(found);
  }

  void Session::LoseHost(const std::string &reason) {
    network_.Stop();
    connected_ = active_ = false;
    localCommand_ = {};
    lobby_.clear();
    simulation_.ResetBase({});
    status_ = "Error LAN: " + reason + ". Regresa al menú para crear o unirte a otra sesión.";
  }

  void Session::HandleEvent(const NetworkEvent &event) {
    if (!active_) {
      return;
    }
    if (event.type == NetworkEventType::Connected) {
      if (IsHost()) {
        Participant participant;
        participant.lastMessage = participant.lastCommand = participant.rateWindow = elapsed_;
        participants_[event.peer] = participant;
        if (GetWorld().phase != GamePhase::Base) {
          Reject(event.peer, "La operación ya comenzó; espera a que el equipo regrese a la base");
        }
      } else if (IsClient() && event.peer == NetworkConfig::HostId) {
        network_.Send(event.peer, Json{{"type", "hello"}, {"version", GameplayConfig::ProtocolVersion}, {"name", localName_}}.dump());
      }
      return;
    }
    if (event.type == NetworkEventType::Disconnected || event.type == NetworkEventType::Error) {
      if (IsClient()) {
        LoseHost(event.payload.empty() ? "Se perdió la conexión con el anfitrión" : event.payload);
      } else if (IsHost() && event.peer != 0) {
        RemoveParticipant(event.peer);
      } else if (!event.payload.empty()) {
        status_ = event.payload;
      }
      return;
    }
    if (event.type != NetworkEventType::Message || event.payload.size() > NetworkConfig::MaxFrameBytes) {
      return;
    }
    try {
      const Json message = Json::parse(event.payload);
      if (!message.is_object() || !message.contains("type") || !message.at("type").is_string()) {
        if (IsHost()) {
          Reject(event.peer, "Mensaje de sesión no válido");
        } else {
          LoseHost("El anfitrión envió un mensaje de sesión no válido");
        }
        return;
      }
      if (IsHost()) {
        HandleHostMessage(event.peer, message);
      } else if (IsClient() && event.peer == NetworkConfig::HostId) {
        HandleClientMessage(message);
      }
    } catch (const Json::exception &) {
      if (IsHost()) {
        Reject(event.peer, "Formato o valores LAN no válidos");
      } else if (IsClient()) {
        LoseHost("Formato o valores LAN no válidos");
      }
    }
  }

  void Session::HandleHostMessage(PeerId peer, const Json &message) {
    const auto found = participants_.find(peer);
    if (found == participants_.end() || found->second.rejected) {
      return;
    }
    auto &participant = found->second;
    const auto type = message.at("type").get<std::string>();
    if (!participant.accepted) {
      std::uint64_t version = 0;
      if (type != "hello" || !ReadUnsigned(message.at("version"), version) ||
          version != GameplayConfig::ProtocolVersion || !message.at("name").is_string() ||
          GetWorld().phase != GamePhase::Base) {
        Reject(peer, "Versión incompatible o sesión no disponible");
        return;
      }
      const auto name = message.at("name").get<std::string>();
      if (name != CleanName(name) || lobby_.size() >= GameplayConfig::MaximumPlayers ||
          !simulation_.AddPlayer(peer, name)) {
        Reject(peer, "Nombre no válido o equipo completo");
        return;
      }
      participant.accepted = true;
      participant.lastMessage = elapsed_;
      lobby_.push_back({peer, name, false});
      for (auto &player : lobby_) {
        player.ready = false;
      }
      network_.Send(peer, Json{{"type", "welcome"}, {"version", GameplayConfig::ProtocolVersion},
                              {"player", peer}, {"epoch", epoch_}}.dump());
      status_ = "Agente " + name + " conectado; confirma la preparación del equipo";
      snapshotPending_ = true;
      return;
    }

    if (elapsed_ - participant.rateWindow >= 1.f) {
      participant.rateWindow = elapsed_;
      participant.rateCount = 0;
    }
    if (++participant.rateCount > GameplayConfig::MaximumCommandsPerSecond) {
      Reject(peer, "Demasiados comandos LAN por segundo");
      return;
    }
    participant.lastMessage = elapsed_;
    if (type == "command") {
      std::uint64_t sequence = 0, epoch = 0;
      if (!ReadUnsigned(message.at("seq"), sequence) || !ReadUnsigned(message.at("epoch"), epoch)) {
        Reject(peer, "Secuencia de mando no válida");
        return;
      }
      if (epoch != epoch_ || sequence <= participant.sequence) {
        return;
      }
      ActionCommand command;
      if (!ReadCommand(message, command)) {
        Reject(peer, "Ejes o acciones de mando no válidos");
        return;
      }
      MergePulses(command, participant.command);
      participant.command = command;
      participant.sequence = sequence;
      participant.lastCommand = elapsed_;
    } else if (type == "ready" && GetWorld().phase == GamePhase::Base && message.at("ready").is_boolean()) {
      for (auto &player : lobby_) {
        if (player.id == peer) {
          player.ready = message.at("ready").get<bool>();
        }
      }
      snapshotPending_ = true;
    } else if (type == "loadout" && GetWorld().phase == GamePhase::Base) {
      Loadout loadout;
      if (!ReadLoadout(message, loadout)) {
        Reject(peer, "Equipamiento fuera de los límites permitidos");
        return;
      }
      simulation_.SetLoadout(peer, loadout);
      for (auto &player : lobby_) {
        if (player.id == peer) {
          player.ready = false;
        }
      }
      snapshotPending_ = true;
    }
  }

  void Session::HandleClientMessage(const Json &message) {
    const auto type = message.at("type").get<std::string>();
    if (type == "reject") {
      LoseHost(message.at("reason").get<std::string>());
      return;
    }
    if (type == "welcome" && !connected_) {
      std::uint64_t version = 0, player = 0, epoch = 0;
      if (!ReadUnsigned(message.at("version"), version) || version != GameplayConfig::ProtocolVersion ||
          !ReadUnsigned(message.at("player"), player) || player < NetworkConfig::FirstClientId ||
          player >= NetworkConfig::FirstClientId + NetworkConfig::MaxClients ||
          !ReadUnsigned(message.at("epoch"), epoch) || epoch > std::numeric_limits<std::uint32_t>::max()) {
        LoseHost("Respuesta de conexión incompatible");
        return;
      }
      localPlayer_ = static_cast<PlayerId>(player);
      epoch_ = static_cast<std::uint32_t>(epoch);
      connected_ = true;
      lastHostMessage_ = elapsed_;
      status_ = "Conectado; esperando preparación del equipo";
      return;
    }
    if (type != "snapshot" || !connected_) {
      return;
    }
    std::uint64_t sequence = 0, epoch = 0;
    if (!ReadUnsigned(message.at("seq"), sequence) || !ReadUnsigned(message.at("epoch"), epoch) ||
        epoch > std::numeric_limits<std::uint32_t>::max()) {
      LoseHost("Instantánea de sesión no válida");
      return;
    }
    if (sequence <= receivedSnapshot_) {
      return;
    }
    const auto &players = message.at("lobby");
    if (!players.is_array() || players.empty() || players.size() > GameplayConfig::MaximumPlayers) {
      LoseHost("Lista de agentes no válida");
      return;
    }
    std::vector<LobbyPlayer> lobby;
    std::unordered_set<PlayerId> ids;
    for (const auto &player : players) {
      std::uint64_t id = 0;
      if (!player.is_object() || !ReadUnsigned(player.at("id"), id) || id < NetworkConfig::HostId ||
          id >= NetworkConfig::FirstClientId + NetworkConfig::MaxClients ||
          !player.at("name").is_string() || !player.at("ready").is_boolean()) {
        LoseHost("Datos de agente no válidos");
        return;
      }
      const auto name = player.at("name").get<std::string>();
      if (name != CleanName(name) || !ids.insert(static_cast<PlayerId>(id)).second) {
        LoseHost("Identidad de agente no válida");
        return;
      }
      lobby.push_back({static_cast<PlayerId>(id), name, player.at("ready").get<bool>()});
    }
    if (!ids.contains(localPlayer_) || !ids.contains(NetworkConfig::HostId)) {
      LoseHost("El equipo ya no incluye este agente");
      return;
    }
    std::uint64_t recipient = 0;
    if (!ReadUnsigned(message.at("world").at("player"), recipient) || recipient != localPlayer_) {
      LoseHost("La instantánea pertenece a otro agente");
      return;
    }
    std::string error;
    if (!simulation_.LoadSnapshot(message.at("world"), error)) {
      LoseHost("Instantánea rechazada: " + error);
      return;
    }
    if (epoch_ != epoch) {
      localCommand_ = {};
    }
    epoch_ = static_cast<std::uint32_t>(epoch);
    receivedSnapshot_ = sequence;
    lastHostMessage_ = elapsed_;
    lobby_ = std::move(lobby);
    status_ = GetWorld().phase == GamePhase::Base ? "Base CIO: conectado al anfitrión" : "Operación LAN en curso";
  }

  void Session::SendSnapshots(bool force) {
    if (!IsHost() || !active_) {
      return;
    }
    NO_TRACE_PROFILE_SCOPE(SessionSnapshot);
    Json lobby = Json::array();
    for (const auto &player : lobby_) {
      lobby.push_back({{"id", player.id}, {"name", player.name}, {"ready", player.ready}});
    }
    const auto sequence = ++snapshotSequence_;
    for (auto &[id, participant] : participants_) {
      // Un mando nuevo confirma actividad del cliente y limita la acumulación de estados periódicos.
      if (participant.accepted && !participant.rejected && (force || participant.sequence > participant.lastSnapshotCommandSequence)) {
        // Cada destinatario recibe únicamente su propia orden privada.
        const Json message = [&] {
          NO_TRACE_PROFILE_SCOPE(SnapshotBuild);
          return Json{{"type", "snapshot"}, {"seq", sequence}, {"epoch", epoch_},
                      {"lobby", lobby}, {"world", simulation_.SnapshotFor(id, true)}};
        }();
        const auto payload = [&] {
          NO_TRACE_PROFILE_SCOPE(SnapshotSerialize);
          return message.dump();
        }();
        const bool sent = [&] {
          NO_TRACE_PROFILE_SCOPE(SnapshotSend);
          return network_.Send(id, payload);
        }();
        if (!sent) {
          status_ = "No se pudo enviar una instantánea LAN";
        } else {
          participant.lastSnapshotCommandSequence = participant.sequence;
        }
      }
    }
  }

  void Session::SendCommand() {
    const Json message{{"type", "command"}, {"seq", ++commandSequence_}, {"epoch", epoch_},
                       {"move", {localCommand_.moveX, localCommand_.moveY}},
                       {"aim", {localCommand_.aimX, localCommand_.aimY}}, {"slope", localCommand_.aimSlope}, {"decoyDistance", localCommand_.decoyDistance}, {"flags", CommandFlags(localCommand_)}, {"puzzleInput", localCommand_.puzzleInput}, {"puzzleTarget", localCommand_.puzzleTarget}, {"weaponChoice", localCommand_.weaponChoice}};
    network_.Send(NetworkConfig::HostId, message.dump());
    ClearPulses(localCommand_);
  }

  void Session::StepSimulation(float deltaTime, bool localPaused) {
    if (role_ == NetworkRole::Offline && localPaused) {
      simulationAccumulator_ = 0.f;
      localCommand_ = {};
      return;
    }
    if (localPaused) {
      localCommand_ = {};
    }
    simulationAccumulator_ += std::min(deltaTime, 0.25f);
    constexpr float step = 1.f / GameplayConfig::SimulationHz;
    while (simulationAccumulator_ >= step) {
      std::unordered_map<PlayerId, ActionCommand> commands;
      commands[localPlayer_] = localCommand_;
      for (auto &[id, participant] : participants_) {
        if (participant.accepted && !participant.rejected && elapsed_ - participant.lastCommand <= GameplayConfig::CommandTimeout) {
          commands[id] = participant.command;
        }
      }
      const auto contract = GetWorld().contract;
      simulation_.Step(step, commands);
      if (contract != GetWorld().contract && IsHost()) {
        for (auto &player : lobby_) {
          player.ready = false;
        }
      }
      ClearPulses(localCommand_);
      for (auto &[id, participant] : participants_) {
        ClearPulses(participant.command);
      }
      simulationAccumulator_ -= step;
    }
  }

  void Session::Tick(float deltaTime, bool localPaused, float networkDeltaTime) {
    const float dt = std::isfinite(deltaTime) ? std::clamp(deltaTime, 0.f, 1.f) : 0.f;
    // La red conserva segundos reales aunque el motor recorte el avance de la simulación.
    const float networkDt = std::isfinite(networkDeltaTime) && networkDeltaTime >= 0.f ? networkDeltaTime : dt;
    elapsed_ += networkDt;
    network_.Update();
    for (const auto &event : network_.DrainEvents()) {
      HandleEvent(event);
    }
    if (!active_) {
      return;
    }
    if (IsHost()) {
      std::vector<PeerId> expired;
      for (const auto &[id, participant] : participants_) {
        if ((participant.rejected && elapsed_ - participant.rejectTime >= 0.1f) ||
            elapsed_ - participant.lastMessage > GameplayConfig::SessionTimeout) {
          expired.push_back(id);
        }
      }
      for (const auto id : expired) {
        network_.Disconnect(id, "El participante no completó o mantuvo la sesión");
        RemoveParticipant(id);
      }
    } else if (IsClient() && elapsed_ - lastHostMessage_ > GameplayConfig::SessionTimeout) {
      LoseHost("El anfitrión dejó de responder");
      return;
    }
    if (IsClient()) {
      if (localPaused) {
        localCommand_ = {};
      }
      if (connected_) {
        commandAccumulator_ += networkDt;
        if (commandAccumulator_ >= 1.f / GameplayConfig::CommandHz) {
          commandAccumulator_ = std::fmod(commandAccumulator_, 1.f / GameplayConfig::CommandHz);
          SendCommand();
        }
      }
      return;
    }
    StepSimulation(dt, localPaused);
    if (IsHost()) {
      snapshotAccumulator_ += networkDt;
      const bool periodic = snapshotAccumulator_ >= 1.f / GameplayConfig::SnapshotHz;
      if (periodic) {
        snapshotAccumulator_ = std::fmod(snapshotAccumulator_, 1.f / GameplayConfig::SnapshotHz);
      }
      // Los cambios de sala del mismo ciclo comparten un estado final sin saturar la cola TCP.
      if (periodic || snapshotPending_) {
        SendSnapshots(std::exchange(snapshotPending_, false));
      }
    }
  }
}
