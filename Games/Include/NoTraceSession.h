#pragma once

#include "Managers/NetworkManager.h"
#include "Managers/RandomManager.h"
#include "NoTraceSimulation.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace NoTrace {
  struct LobbyPlayer {
    PlayerId id = 0;
    std::string name;
    bool ready = false;
  };

  class Session {
  public:
    Session(NetworkManager &network, RandomManager &random);
    void StartSolo(const std::string &name);
    bool Host(const std::string &name, std::uint16_t port = NetworkConfig::DefaultPort);
    bool Join(const std::string &ip, std::uint16_t port, const std::string &name);
    void Stop();
    void Tick(float deltaTime, bool localPaused = false);
    void Submit(ActionCommand command);
    void SetReady(bool ready);
    void SetContract(ContractId contract);
    void SetLoadout(Loadout loadout);
    void Deploy();
    void ReturnToBase();
    const WorldState &GetWorld() const;
    PlayerId GetLocalPlayerId() const;
    bool IsHost() const;
    bool IsClient() const;
    bool IsConnected() const;
    bool CanDeploy() const;
    const std::string &GetStatus() const;
    const std::vector<LobbyPlayer> &GetLobbyPlayers() const;

  private:
    struct Participant {
      bool accepted = false;
      float lastMessage = 0.f;
      float lastCommand = 0.f;
      float rateWindow = 0.f;
      unsigned int rateCount = 0;
      std::uint64_t sequence = 0;
      ActionCommand command;
      bool rejected = false;
      float rejectTime = 0.f;
    };

    NetworkManager &network_;
    RandomManager &random_;
    Simulation simulation_;
    NetworkRole role_ = NetworkRole::Offline;
    bool active_ = false;
    bool connected_ = false;
    PlayerId localPlayer_ = 1;
    std::string localName_ = "Agente";
    std::string status_ = "Sin sesión";
    std::vector<LobbyPlayer> lobby_;
    std::unordered_map<PeerId, Participant> participants_;
    ActionCommand localCommand_;
    float elapsed_ = 0.f;
    float lastHostMessage_ = 0.f;
    float commandAccumulator_ = 0.f;
    float snapshotAccumulator_ = 0.f;
    float simulationAccumulator_ = 0.f;
    std::uint64_t commandSequence_ = 0;
    std::uint64_t snapshotSequence_ = 0;
    std::uint64_t receivedSnapshot_ = 0;
    std::uint32_t epoch_ = 0;

    void HandleEvent(const NetworkEvent &event);
    void HandleHostMessage(PeerId peer, const nlohmann::json &message);
    void HandleClientMessage(const nlohmann::json &message);
    void Reject(PeerId peer, const std::string &reason);
    void LoseHost(const std::string &reason);
    void RemoveParticipant(PeerId peer);
    void SendSnapshots();
    void SendCommand();
    void StepSimulation(float deltaTime, bool localPaused);
    void ClearCommands();
  };
}
