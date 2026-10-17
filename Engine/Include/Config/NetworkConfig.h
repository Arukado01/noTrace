#pragma once

#include <cstddef>
#include <cstdint>

namespace NetworkConfig {
  inline constexpr std::uint16_t DefaultPort = 55031;
  inline constexpr std::uint32_t HostId = 1;
  inline constexpr std::uint32_t FirstClientId = 2;
  inline constexpr std::size_t MaxClients = 3;
  inline constexpr std::size_t MaxFrameBytes = 64 * 1024;
  inline constexpr std::size_t MaxQueuedBytes = 4 * MaxFrameBytes;
  inline constexpr std::size_t MaxPendingEvents = 256;
  inline constexpr std::size_t ReceiveChunkBytes = 4096;
  inline constexpr std::size_t MaxReceiveBytesPerUpdate = 64 * 1024;
  inline constexpr std::size_t MaxFramesPerUpdate = 64;
  inline constexpr float ConnectTimeoutSeconds = 5.f;
  inline constexpr float IncompleteFrameTimeoutSeconds = 5.f;
}
