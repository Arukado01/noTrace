#include "Managers/NetworkManager.h"

#include <SFML/Network/IpAddress.hpp>
#include <SFML/Network/TcpListener.hpp>
#include <SFML/Network/TcpSocket.hpp>
#include <SFML/System/Clock.hpp>
#include <algorithm>
#include <array>
#include <deque>
#include <map>
#include <utility>

struct NetworkManager::State {
  struct Peer {
    sf::TcpSocket socket;
    std::vector<std::uint8_t> incoming;
    std::deque<std::vector<std::uint8_t>> outgoing;
    std::size_t outgoingOffset = 0;
    std::size_t queuedBytes = 0;
    bool connecting = false;
    sf::Clock connectionClock;
    sf::Clock incompleteClock;
  };

  NetworkRole role = NetworkRole::Offline;
  sf::TcpListener listener;
  std::map<PeerId, std::unique_ptr<Peer>> peers;
  std::vector<NetworkEvent> events;
  std::string status = "Sin conexión";
  std::uint16_t port = 0;

  bool Emit(NetworkEventType type, PeerId id, std::string payload) {
    if (events.size() >= NetworkConfig::MaxPendingEvents) {
      if (type == NetworkEventType::Message) {
        return false;
      }
      // Los avisos de desconexión tienen prioridad sobre mensajes que nadie ha consumido.
      const auto message = std::find_if(events.begin(), events.end(), [](const auto &event) {
        return event.type == NetworkEventType::Message;
      });
      events.erase(message == events.end() ? events.begin() : message);
    }
    events.push_back({type, id, std::move(payload)});
    return true;
  }

  void Drop(PeerId id, const std::string &reason) {
    const auto peer = peers.find(id);
    if (peer == peers.end()) {
      return;
    }
    peer->second->socket.disconnect();
    peers.erase(peer);
    status = reason;
    Emit(NetworkEventType::Disconnected, id, reason);
    if (role == NetworkRole::Client) {
      role = NetworkRole::Offline;
      port = 0;
    }
  }

  bool Parse(PeerId id, Peer &peer, std::size_t &frames) {
    while (peer.incoming.size() >= 4 && frames < NetworkConfig::MaxFramesPerUpdate) {
      const auto &data = peer.incoming;
      const std::uint32_t length = (std::uint32_t(data[0]) << 24) |
                                   (std::uint32_t(data[1]) << 16) |
                                   (std::uint32_t(data[2]) << 8) | data[3];
      if (length == 0 || length > NetworkConfig::MaxFrameBytes) {
        return false;
      }
      if (data.size() < 4 + length) {
        break;
      }
      std::string payload(reinterpret_cast<const char *>(data.data() + 4), length);
      if (!Emit(NetworkEventType::Message, id, std::move(payload))) {
        return false;
      }
      peer.incoming.erase(peer.incoming.begin(), peer.incoming.begin() + 4 + length);
      ++frames;
      peer.incompleteClock.restart();
    }
    return true;
  }

  bool Receive(PeerId id, Peer &peer) {
    std::size_t frames = 0;
    std::size_t bytes = 0;
    if (!Parse(id, peer, frames)) {
      return false;
    }
    std::array<std::uint8_t, NetworkConfig::ReceiveChunkBytes> chunk{};
    while (frames < NetworkConfig::MaxFramesPerUpdate && bytes < NetworkConfig::MaxReceiveBytesPerUpdate) {
      std::size_t received = 0;
      const auto result = peer.socket.receive(chunk.data(), chunk.size(), received);
      if (result == sf::Socket::Status::NotReady || result == sf::Socket::Status::Partial) {
        break;
      }
      if (result != sf::Socket::Status::Done || received == 0) {
        return false;
      }
      if (peer.incoming.empty()) {
        peer.incompleteClock.restart();
      }
      bytes += received;
      peer.incoming.insert(peer.incoming.end(), chunk.begin(), chunk.begin() + received);
      if (peer.incoming.size() > NetworkConfig::MaxFrameBytes + 4 + NetworkConfig::ReceiveChunkBytes ||
          !Parse(id, peer, frames)) {
        return false;
      }
    }
    return peer.incoming.empty() ||
           peer.incompleteClock.getElapsedTime().asSeconds() < NetworkConfig::IncompleteFrameTimeoutSeconds;
  }

  bool Flush(Peer &peer) {
    std::size_t budget = NetworkConfig::MaxReceiveBytesPerUpdate;
    while (!peer.outgoing.empty() && budget > 0) {
      auto &frame = peer.outgoing.front();
      const std::size_t remaining = std::min(frame.size() - peer.outgoingOffset, budget);
      std::size_t sent = 0;
      const auto result = peer.socket.send(frame.data() + peer.outgoingOffset, remaining, sent);
      peer.outgoingOffset += sent;
      peer.queuedBytes -= sent;
      budget -= sent;
      if (peer.outgoingOffset == frame.size()) {
        peer.outgoing.pop_front();
        peer.outgoingOffset = 0;
      }
      if (result == sf::Socket::Status::NotReady || result == sf::Socket::Status::Partial) {
        return true;
      }
      if (result != sf::Socket::Status::Done) {
        return false;
      }
    }
    return true;
  }
};

NetworkManager::NetworkManager() : state_(std::make_unique<State>()) {}
NetworkManager::~NetworkManager() = default;

bool NetworkManager::Host(std::uint16_t port) {
  Stop();
  state_->listener.setBlocking(false);
  if (state_->listener.listen(port, sf::IpAddress::AnyV4) != sf::Socket::Status::Done) {
    state_->status = "No se pudo abrir el puerto LAN";
    state_->Emit(NetworkEventType::Error, 0, state_->status);
    return false;
  }
  state_->role = NetworkRole::Host;
  state_->port = state_->listener.getLocalPort();
  state_->status = "Anfitrión LAN en puerto " + std::to_string(state_->port);
  return true;
}

bool NetworkManager::Join(std::string_view ip, std::uint16_t port) {
  Stop();
  const auto address = sf::IpAddress::fromString(ip);
  if (!address || !address->isV4() || port == 0) {
    state_->status = "Dirección IPv4 o puerto no válidos";
    state_->Emit(NetworkEventType::Error, NetworkConfig::HostId, state_->status);
    return false;
  }
  auto peer = std::make_unique<State::Peer>();
  peer->socket.setBlocking(false);
  const auto result = peer->socket.connect(*address, port);
  if (result != sf::Socket::Status::Done && result != sf::Socket::Status::NotReady) {
    state_->status = "No se pudo conectar con el anfitrión";
    state_->Emit(NetworkEventType::Error, NetworkConfig::HostId, state_->status);
    return false;
  }
  peer->connecting = result == sf::Socket::Status::NotReady;
  state_->role = NetworkRole::Client;
  state_->port = port;
  state_->status = peer->connecting ? "Conectando con " + std::string(ip) : "Conexión LAN establecida";
  if (!peer->connecting) {
    state_->Emit(NetworkEventType::Connected, NetworkConfig::HostId, "");
  }
  state_->peers.emplace(NetworkConfig::HostId, std::move(peer));
  return true;
}

void NetworkManager::Stop() {
  state_->peers.clear();
  state_->listener.close();
  state_->events.clear();
  state_->role = NetworkRole::Offline;
  state_->port = 0;
  state_->status = "Sin conexión";
}

void NetworkManager::Disconnect(PeerId peer, const std::string &reason) {
  state_->Drop(peer, reason);
}

void NetworkManager::Update() {
  if (state_->role == NetworkRole::Host) {
    for (std::size_t attempts = 0; attempts < NetworkConfig::MaxClients + 1; ++attempts) {
      auto peer = std::make_unique<State::Peer>();
      if (state_->listener.accept(peer->socket) != sf::Socket::Status::Done) {
        break;
      }
      if (state_->peers.size() >= NetworkConfig::MaxClients) {
        peer->socket.disconnect();
        continue;
      }
      PeerId id = NetworkConfig::FirstClientId;
      while (state_->peers.contains(id)) {
        ++id;
      }
      peer->socket.setBlocking(false);
      state_->peers.emplace(id, std::move(peer));
      state_->Emit(NetworkEventType::Connected, id, "");
    }
  }

  std::vector<PeerId> disconnected;
  for (auto &[id, peer] : state_->peers) {
    if (peer->connecting) {
      if (peer->socket.getRemoteAddress()) {
        peer->connecting = false;
        state_->status = "Conexión LAN establecida";
        state_->Emit(NetworkEventType::Connected, id, "");
      } else if (peer->connectionClock.getElapsedTime().asSeconds() >= NetworkConfig::ConnectTimeoutSeconds) {
        state_->Emit(NetworkEventType::Error, id, "Se agotó el plazo de conexión LAN");
        disconnected.push_back(id);
        continue;
      } else {
        continue;
      }
    }
    if (!state_->Receive(id, *peer) || !state_->Flush(*peer)) {
      disconnected.push_back(id);
    }
  }
  for (const auto id : disconnected) {
    state_->Drop(id, "Conexión cerrada o mensaje LAN fuera de los límites permitidos");
  }
}

bool NetworkManager::Send(PeerId id, const std::string &payload) {
  const auto peer = state_->peers.find(id);
  if (payload.empty() || payload.size() > NetworkConfig::MaxFrameBytes ||
      peer == state_->peers.end() || peer->second->connecting) {
    return false;
  }
  const std::size_t frameSize = payload.size() + 4;
  if (peer->second->queuedBytes + frameSize > NetworkConfig::MaxQueuedBytes) {
    state_->Drop(id, "El participante no consume los mensajes LAN a tiempo");
    return false;
  }
  const auto size = static_cast<std::uint32_t>(payload.size());
  std::vector<std::uint8_t> frame;
  frame.reserve(frameSize);
  frame.push_back(static_cast<std::uint8_t>(size >> 24));
  frame.push_back(static_cast<std::uint8_t>(size >> 16));
  frame.push_back(static_cast<std::uint8_t>(size >> 8));
  frame.push_back(static_cast<std::uint8_t>(size));
  frame.insert(frame.end(), payload.begin(), payload.end());
  peer->second->queuedBytes += frameSize;
  peer->second->outgoing.push_back(std::move(frame));
  return true;
}

bool NetworkManager::Broadcast(const std::string &payload) {
  std::vector<PeerId> ids;
  for (const auto &[id, peer] : state_->peers) {
    if (!peer->connecting) {
      ids.push_back(id);
    }
  }
  bool result = true;
  for (const auto id : ids) {
    result = Send(id, payload) && result;
  }
  return result;
}

std::vector<NetworkEvent> NetworkManager::DrainEvents() {
  return std::exchange(state_->events, {});
}

NetworkRole NetworkManager::GetRole() const { return state_->role; }
std::uint16_t NetworkManager::GetPort() const { return state_->port; }
const std::string &NetworkManager::GetStatus() const { return state_->status; }

std::string NetworkManager::GetLocalAddress() const {
  const auto address = sf::IpAddress::getLocalAddress(sf::IpAddress::Type::IpV4);
  return address ? address->toString() : "IP local no disponible";
}
