#pragma once

#include "Config/NoTraceFirstPersonConfig.h"
#include <cstddef>

namespace NoTrace {
  struct FirstPersonCamera {
    float yaw = 0.f;
    float pitch = 0.f;
    float fieldOfView = FirstPersonConfig::DefaultFov;
  };
  struct FirstPersonRenderStats {
    unsigned int width = 0;
    unsigned int height = 0;
    std::size_t prisms = 0;
    std::size_t columnCandidates = 0;
    std::size_t billboards = 0;
    std::size_t actorPixels = 0;
    float milliseconds = 0.f;
  };
}
