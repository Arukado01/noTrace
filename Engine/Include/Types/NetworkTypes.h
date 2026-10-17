#pragma once

#include <cstdint>
#include <string>

using PeerId = std::uint32_t;

enum class NetworkRole { Offline, Host, Client };
enum class NetworkEventType { Connected, Message, Disconnected, Error };

struct NetworkEvent {
  NetworkEventType type;
  PeerId peer = 0;
  std::string payload;
};
