#pragma once

#include "Config/NoTraceDevicesConfig.h"
#include <SFML/System/Vector2.hpp>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace NoTrace {
  enum class CameraState { Operating, Disabled, Broken };
  enum class CameraPerception { Sweeping, Suspicion, Tracking, Searching };
  enum class TrapState { Armed, Detected, Disarmed, Detonated };
  enum class PuzzleKind { None, Explosive, Computer, Door, CameraPanel, Coordination };
  enum class PuzzleFamily { Automatic, Arrows, Wires, Coordination, Levers };
  enum class PuzzleState { Ready, InUse, Solved, Failed };

  struct CameraDevice {
    CameraState state = CameraState::Operating;
    float baseAngle = 0.f;
    float sweepAngle = 0.f;
    float minAngle = DevicesConfig::CameraSweepMinimum;
    float maxAngle = DevicesConfig::CameraSweepMaximum;
    float speed = DevicesConfig::CameraSweepSpeed;
    float pauseRemaining = 0.f;
    int direction = 1;
    float range = DevicesConfig::CameraRange;
    float cosHalfAngle = DevicesConfig::CameraCosHalfAngle;
    float confirmationTime = DevicesConfig::CameraConfirmationTime;
    float reactivationRemaining = 0.f;
    unsigned int controlGroup = 1;
    bool sweeping = false;
    CameraPerception perception = CameraPerception::Sweeping;
    std::uint32_t trackedAgent = 0;
    sf::Vector2f lastKnown;
    float trackingSpeed = DevicesConfig::CameraTrackingSpeed;
    float searchRemaining = 0.f;
    float suspicionRemaining = 0.f;
    float confirmedRemaining = 0.f;
    float indicatorProgress = 0.f;
    float lostSightTime = 0.f;
  };
  struct TrapDevice {
    TrapState state = TrapState::Armed;
    float triggerRadius = DevicesConfig::TrapTriggerRadius;
    float audibleRadius = DevicesConfig::TrapAudibleRadius;
    float blastRadius = DevicesConfig::TrapBlastRadius;
    float fuse = DevicesConfig::TrapFuseDuration;
    float remaining = -1.f;
    float tickRemaining = 0.f;
    float damage = DevicesConfig::TrapDamage;
    float effectRemaining = 0.f;
  };
  struct PuzzleDevice {
    PuzzleKind kind = PuzzleKind::None;
    PuzzleState state = PuzzleState::Ready;
    unsigned int difficulty = 3;
    float cooldown = 0.f;
    std::string intelligence;
    PuzzleFamily family = PuzzleFamily::Automatic;
    unsigned int sequenceProgress = 0;
    float coordinationProgress = 0.f;
    unsigned int requiredAgents = 2;
    bool soloAllowed = true;
    bool wireCrossingAllowed = true;
    std::uint32_t parentDevice = 0;
    unsigned int nodeIndex = 0;
    std::vector<std::uint8_t> leverOrder;
    std::uint8_t coordinationMask = 0;
    std::uint8_t coordinationReservedMask = 0;
  };
  struct PuzzleSession {
    std::uint32_t target = 0;
    PuzzleKind kind = PuzzleKind::None;
    unsigned int stage = 0;
    unsigned int length = 0;
    PuzzleFamily family = PuzzleFamily::Automatic;
    std::vector<std::uint8_t> sequence;
    std::array<std::uint8_t, 4> wireLabels{};
    std::uint8_t connectedSources = 0;
    std::uint8_t connectedPorts = 0;
    bool holding = false;
    float coordinationProgress = 0.f;
    unsigned int coordinationButton = 0;
  };
  inline constexpr int WireInput(unsigned int source, unsigned int port) { return static_cast<int>((source - 1u) * DevicesConfig::WireCount + port); }
  inline unsigned int MatchingWirePort(const PuzzleSession &session, unsigned int source) {
    for (unsigned int index = 0; index < session.wireLabels.size(); ++index) if (session.wireLabels[index] == source) return index + 1;
    return 0;
  }
}
