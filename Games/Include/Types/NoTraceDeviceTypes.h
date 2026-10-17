#pragma once

#include "Config/NoTraceDevicesConfig.h"
#include <cstdint>
#include <string>

namespace NoTrace {
  enum class CameraState { Operating, Disabled, Broken };
  enum class TrapState { Armed, Detected, Disarmed, Detonated };
  enum class PuzzleKind { None, Explosive, Computer, Door, CameraPanel };
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
  };
  struct PuzzleSession {
    std::uint32_t target = 0;
    PuzzleKind kind = PuzzleKind::None;
    unsigned int stage = 0;
    unsigned int length = 0;
    unsigned int clueA = 0;
    unsigned int clueB = 0;
  };
}
