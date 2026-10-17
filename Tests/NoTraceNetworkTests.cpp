#include "Managers/NetworkManager.h"

#include <SFML/Network/IpAddress.hpp>
#include <SFML/Network/TcpListener.hpp>
#include <SFML/Network/TcpSocket.hpp>
#include <SFML/System/Clock.hpp>
#include <SFML/System/Sleep.hpp>
#include <array>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
  void Require(bool condition, const char *message) {
    if (!condition) {
      throw std::runtime_error(message);
    }
  }

  void Until(const std::function<bool()> &condition, const std::function<void()> &tick, float seconds = 3.f) {
    sf::Clock clock;
    while (!condition()) {
      Require(clock.getElapsedTime().asSeconds() < seconds, "Tiempo agotado en prueba de transporte");
      tick();
      sf::sleep(sf::milliseconds(1));
    }
  }

  struct Pair {
    NetworkManager host;
    NetworkManager client;
    PeerId clientId = 0;
    bool clientConnected = false;
    std::vector<NetworkEvent> hostEvents;
    std::vector<NetworkEvent> clientEvents;

    Pair() {
      Require(host.Host(0), "No se pudo crear anfitrión en puerto efímero");
      Require(host.GetPort() != 0, "Puerto efímero no asignado");
      Require(client.Join("127.0.0.1", host.GetPort()), "No se inició conexión cliente");
      Until([&] { return clientId != 0 && clientConnected; }, [&] { Tick(); });
      Require(clientId == NetworkConfig::FirstClientId, "Identidad de cliente incorrecta");
    }

    void Tick() {
      host.Update();
      client.Update();
      for (auto &event : host.DrainEvents()) {
        if (event.type == NetworkEventType::Connected) {
          clientId = event.peer;
        }
        hostEvents.push_back(std::move(event));
      }
      for (auto &event : client.DrainEvents()) {
        if (event.type == NetworkEventType::Connected) {
          clientConnected = event.peer == NetworkConfig::HostId;
        }
        clientEvents.push_back(std::move(event));
      }
    }
  };

  std::size_t Messages(const std::vector<NetworkEvent> &events) {
    std::size_t count = 0;
    for (const auto &event : events) {
      count += event.type == NetworkEventType::Message;
    }
    return count;
  }

  bool Has(const std::vector<NetworkEvent> &events, NetworkEventType type, const std::string &payload = "") {
    for (const auto &event : events) {
      if (event.type == type && (payload.empty() || event.payload == payload)) {
        return true;
      }
    }
    return false;
  }

  std::vector<std::uint8_t> Frame(const std::string &payload) {
    const auto size = static_cast<std::uint32_t>(payload.size());
    std::vector<std::uint8_t> data{static_cast<std::uint8_t>(size >> 24), static_cast<std::uint8_t>(size >> 16),
                                  static_cast<std::uint8_t>(size >> 8), static_cast<std::uint8_t>(size)};
    data.insert(data.end(), payload.begin(), payload.end());
    return data;
  }

  void RejectedConnectionDoesNotConnect() {
    sf::TcpListener reservation;
    Require(reservation.listen(0, sf::IpAddress::LocalHostV4) == sf::Socket::Status::Done,
            "No se pudo reservar el puerto de prueba");
    const auto port = reservation.getLocalPort();
    reservation.close();
    NetworkManager client;
    if (client.Join("127.0.0.1", port)) {
      bool finished = false;
      Until([&] { return finished; }, [&] {
        client.Update();
        const auto events = client.DrainEvents();
        Require(!Has(events, NetworkEventType::Connected), "Se anunció conexión antes de completar TCP");
        finished = Has(events, NetworkEventType::Error) || Has(events, NetworkEventType::Disconnected);
      }, NetworkConfig::ConnectTimeoutSeconds + 1.f);
    }
    Require(client.GetRole() == NetworkRole::Offline, "Cliente rechazado permanece activo");
    Require(!client.Send(NetworkConfig::HostId, "unreachable"), "Envío aceptado sin conexión TCP");
    Require(!Has(client.DrainEvents(), NetworkEventType::Connected), "Conexión rechazada generó Connected");
  }

  void ExchangeAndDisconnect() {
    Pair pair;
    Require(pair.client.Send(NetworkConfig::HostId, "{\"command\":\"crouch\"}"), "No se encoló comando");
    Require(pair.host.Broadcast("{\"alarm\":false}"), "No se encoló snapshot");
    Until([&] { return Messages(pair.hostEvents) == 1 && Messages(pair.clientEvents) == 1; }, [&] { pair.Tick(); });
    Require(Has(pair.hostEvents, NetworkEventType::Message, "{\"command\":\"crouch\"}"), "Comando alterado");
    Require(Has(pair.clientEvents, NetworkEventType::Message, "{\"alarm\":false}"), "Snapshot alterado");
    pair.client.Stop();
    Until([&] { return Has(pair.hostEvents, NetworkEventType::Disconnected); }, [&] { pair.Tick(); });
    Require(pair.host.GetRole() == NetworkRole::Host, "Un cliente desconectado cerró al anfitrión");
  }

  void HostDisconnect() {
    Pair pair;
    pair.host.Stop();
    Until([&] { return Has(pair.clientEvents, NetworkEventType::Disconnected); }, [&] { pair.Tick(); });
    Require(pair.client.GetRole() == NetworkRole::Offline, "Cliente permanece conectado sin anfitrión");
  }

  void MaximumPayloadAndQueue() {
    Pair pair;
    const std::string payload(NetworkConfig::MaxFrameBytes, 'x');
    Require(pair.client.Send(NetworkConfig::HostId, payload), "Carga máxima rechazada");
    Until([&] { return Has(pair.hostEvents, NetworkEventType::Message, payload); }, [&] { pair.Tick(); });
    Require(!pair.client.Send(NetworkConfig::HostId, ""), "Carga vacía aceptada");
    Require(!pair.client.Send(NetworkConfig::HostId, payload + "x"), "Carga excesiva aceptada");
    Require(!pair.host.Send(99, "unknown"), "Destinatario desconocido aceptado");
    bool saturated = false;
    for (int i = 0; i < 8; ++i) {
      if (!pair.host.Send(pair.clientId, payload)) {
        saturated = true;
        break;
      }
    }
    Require(saturated, "Cola de salida ilimitada");
    auto events = pair.host.DrainEvents();
    Require(Has(events, NetworkEventType::Disconnected), "Saturación no desconecta al participante lento");
  }

  void RawFrames(bool oversized) {
    NetworkManager host;
    Require(host.Host(0), "No se creó servidor para prueba raw");
    sf::TcpSocket raw;
    Require(raw.connect(sf::IpAddress::LocalHostV4, host.GetPort(), sf::seconds(1)) == sf::Socket::Status::Done,
            "No se conectó peer raw");
    PeerId id = 0;
    std::vector<NetworkEvent> events;
    const auto tick = [&] {
      host.Update();
      for (auto &event : host.DrainEvents()) {
        if (event.type == NetworkEventType::Connected) {
          id = event.peer;
        }
        events.push_back(std::move(event));
      }
    };
    Until([&] { return id != 0; }, tick);
    if (oversized) {
      const std::array<std::uint8_t, 4> prefix{0, 1, 0, 1};
      Require(raw.send(prefix.data(), prefix.size()) == sf::Socket::Status::Done, "Fallo envío cabecera excesiva");
      Until([&] { return Has(events, NetworkEventType::Disconnected); }, tick);
      Require(Messages(events) == 0, "Mensaje excesivo entregado al consumidor");
      return;
    }
    const auto first = Frame("fragmented");
    Require(raw.send(first.data(), 2) == sf::Socket::Status::Done, "Fallo envío fragmento");
    tick();
    Require(Messages(events) == 0, "Cabecera parcial produjo mensaje");
    Require(raw.send(first.data() + 2, first.size() - 2) == sf::Socket::Status::Done, "Fallo envío resto");
    auto coalesced = Frame("second");
    const auto third = Frame("third");
    coalesced.insert(coalesced.end(), third.begin(), third.end());
    Require(raw.send(coalesced.data(), coalesced.size()) == sf::Socket::Status::Done, "Fallo envío combinado");
    Until([&] { return Messages(events) == 3; }, tick);
    Require(Has(events, NetworkEventType::Message, "fragmented") &&
                Has(events, NetworkEventType::Message, "second") && Has(events, NetworkEventType::Message, "third"),
            "Se perdieron límites de mensajes TCP");
  }

  void RosterLimitAndInvalidAddress() {
    NetworkManager invalid;
    Require(!invalid.Join("dirección-inválida", 1), "Dirección inválida aceptada");
    Require(invalid.GetRole() == NetworkRole::Offline, "Dirección inválida cambia estado");
    NetworkManager host;
    Require(host.Host(0), "No se creó servidor para límite de sala");
    std::array<sf::TcpSocket, NetworkConfig::MaxClients + 1> sockets;
    std::size_t accepted = 0;
    for (auto &socket : sockets) {
      Require(socket.connect(sf::IpAddress::LocalHostV4, host.GetPort(), sf::seconds(1)) == sf::Socket::Status::Done,
              "No se conectó socket de prueba");
      host.Update();
      for (const auto &event : host.DrainEvents()) {
        accepted += event.type == NetworkEventType::Connected;
      }
    }
    Require(accepted == NetworkConfig::MaxClients, "Se excedió el límite de participantes");
  }

  void IncompleteFrameAndEventBudget() {
    NetworkManager host;
    Require(host.Host(0), "No se creó servidor para límite de recepción");
    sf::TcpSocket slow;
    Require(slow.connect(sf::IpAddress::LocalHostV4, host.GetPort(), sf::seconds(1)) == sf::Socket::Status::Done,
            "No se conectó peer lento");
    std::vector<NetworkEvent> events;
    const auto tick = [&] {
      host.Update();
      for (auto &event : host.DrainEvents()) {
        events.push_back(std::move(event));
      }
    };
    Until([&] { return Has(events, NetworkEventType::Connected); }, tick);
    const std::array<std::uint8_t, 5> incomplete{0, 0, 0, 10, 'x'};
    Require(slow.send(incomplete.data(), incomplete.size()) == sf::Socket::Status::Done, "Fallo envío incompleto");
    Until([&] { return Has(events, NetworkEventType::Disconnected); }, tick,
          NetworkConfig::IncompleteFrameTimeoutSeconds + 2.f);
    Require(Messages(events) == 0, "Se entregó mensaje incompleto");

    sf::TcpSocket flooding;
    Require(flooding.connect(sf::IpAddress::LocalHostV4, host.GetPort(), sf::seconds(1)) == sf::Socket::Status::Done,
            "No se conectó peer de saturación");
    host.Update();
    host.DrainEvents();
    std::vector<std::uint8_t> flood;
    const auto frame = Frame("bounded");
    for (std::size_t i = 0; i < NetworkConfig::MaxPendingEvents + 1; ++i) {
      flood.insert(flood.end(), frame.begin(), frame.end());
    }
    Require(flooding.send(flood.data(), flood.size()) == sf::Socket::Status::Done, "Fallo envío saturación");
    for (int i = 0; i < 20; ++i) {
      host.Update();
      sf::sleep(sf::milliseconds(1));
    }
    const auto queued = host.DrainEvents();
    Require(queued.size() <= NetworkConfig::MaxPendingEvents, "Cola de eventos ilimitada");
    Require(Has(queued, NetworkEventType::Disconnected), "Saturación de eventos no desconecta al emisor");
  }

  int SmokeHost(std::uint16_t port) {
    NetworkManager host;
    Require(host.Host(port), "No se pudo abrir anfitrión smoke");
    std::cout << "READY " << host.GetPort() << std::endl;
    bool hello = false;
    bool goodbye = false;
    bool disconnected = false;
    Until([&] { return hello && goodbye && disconnected; }, [&] {
      host.Update();
      for (const auto &event : host.DrainEvents()) {
        if (event.type == NetworkEventType::Message && event.payload == "smoke:client") {
          hello = true;
          Require(host.Send(event.peer, "smoke:host"), "Respuesta smoke no encolada");
        }
        goodbye = goodbye || (event.type == NetworkEventType::Message && event.payload == "smoke:bye");
        disconnected = disconnected || event.type == NetworkEventType::Disconnected;
      }
    }, 15.f);
    std::cout << "PASS host: conexión, intercambio y desconexión" << std::endl;
    return 0;
  }

  int SmokeClient(const std::string &ip, std::uint16_t port) {
    NetworkManager client;
    Require(client.Join(ip, port), "No se inició cliente smoke");
    bool replied = false;
    Until([&] { return replied; }, [&] {
      client.Update();
      for (const auto &event : client.DrainEvents()) {
        if (event.type == NetworkEventType::Connected) {
          Require(client.Send(NetworkConfig::HostId, "smoke:client"), "Saludo smoke no encolado");
        }
        if (event.type == NetworkEventType::Message && event.payload == "smoke:host") {
          replied = true;
          Require(client.Send(NetworkConfig::HostId, "smoke:bye"), "Despedida smoke no encolada");
        }
        Require(event.type != NetworkEventType::Error && event.type != NetworkEventType::Disconnected,
                "Cliente smoke perdió conexión");
      }
    }, 10.f);
    client.Update();
    sf::sleep(sf::milliseconds(100));
    client.Stop();
    std::cout << "PASS client: conexión e intercambio" << std::endl;
    return 0;
  }
}

int main(int argc, char **argv) {
  try {
    if (argc == 3 && std::string(argv[1]) == "--host") {
      return SmokeHost(static_cast<std::uint16_t>(std::stoul(argv[2])));
    }
    if (argc == 4 && std::string(argv[1]) == "--client") {
      return SmokeClient(argv[2], static_cast<std::uint16_t>(std::stoul(argv[3])));
    }
    Require(argc == 1, "Uso: NoTraceNetworkTests [--host puerto | --client IPv4 puerto]");
    RejectedConnectionDoesNotConnect();
    ExchangeAndDisconnect();
    HostDisconnect();
    MaximumPayloadAndQueue();
    RawFrames(false);
    RawFrames(true);
    RosterLimitAndInvalidAddress();
    IncompleteFrameAndEventBudget();
    std::cout << "PASS transporte: intercambio, límites, fragmentación, sala y desconexiones" << std::endl;
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
