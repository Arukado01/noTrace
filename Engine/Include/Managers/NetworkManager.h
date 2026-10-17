#pragma once

#include "Config/NetworkConfig.h"
#include "Types/NetworkTypes.h"
#include <memory>
#include <string>
#include <string_view>
#include <vector>

class NetworkManager {
private:
  struct State;
  std::unique_ptr<State> state_;

public:
  NetworkManager();
  ~NetworkManager();
  NetworkManager(const NetworkManager &) = delete;
  NetworkManager &operator=(const NetworkManager &) = delete;

  bool Host(std::uint16_t port = NetworkConfig::DefaultPort);
  bool Join(std::string_view ip, std::uint16_t port = NetworkConfig::DefaultPort);
  void Stop();
  void Disconnect(PeerId peer, const std::string &reason);
  void Update();
  bool Send(PeerId peer, const std::string &payload);
  bool Broadcast(const std::string &payload);
  std::vector<NetworkEvent> DrainEvents();
  NetworkRole GetRole() const;
  std::uint16_t GetPort() const;
  std::string GetLocalAddress() const;
  const std::string &GetStatus() const;
};
