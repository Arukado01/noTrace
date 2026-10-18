#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace NoTrace::Profiling {
  enum class Stage : std::size_t {
    Frame, Simulation, Agents, Guards, Perception, Devices, Security,
    Navigation, NavigationRoute, NavigationPathClear, NavigationBfs,
    VisibilityGrid, VisibilityBoundary, RenderTactical, FloorMesh, GuardCones,
    GeometryBuild, FirstPerson, FirstPersonColumns, FirstPersonRaster,
    FirstPersonActors, FirstPersonViewmodel, Aim, Audio,
    FogMesh, WallMesh, DrawOrder, WorldIndicators, Count
  };

  struct Counter {
    double milliseconds = 0.;
    std::uint64_t calls = 0;
  };

  inline constexpr std::size_t StageCount = static_cast<std::size_t>(Stage::Count);
  inline constexpr std::array<std::string_view, StageCount> StageNames{{
      "Frame", "Simulation", "Agents", "Guards", "Perception", "Devices", "Security",
      "Navigation", "NavigationRoute", "NavigationPathClear", "NavigationBfs",
      "VisibilityGrid", "VisibilityBoundary", "RenderTactical", "FloorMesh", "GuardCones",
      "GeometryBuild", "FirstPerson", "FirstPersonColumns", "FirstPersonRaster",
      "FirstPersonActors", "FirstPersonViewmodel", "Aim", "Audio",
      "FogMesh", "WallMesh", "DrawOrder", "WorldIndicators"}};
  inline thread_local bool enabled = false;
  inline thread_local std::array<Counter, StageCount> counters{};

  inline void Reset() { counters = {}; }
  inline bool Enabled() { return enabled; }
  inline const Counter &Get(Stage stage) { return counters[static_cast<std::size_t>(stage)]; }
  inline std::string_view Name(Stage stage) { return StageNames[static_cast<std::size_t>(stage)]; }
  inline void Count(Stage stage) {
    if (enabled) ++counters[static_cast<std::size_t>(stage)].calls;
  }

  class Scope {
  public:
    explicit Scope(Stage stage) : stage_(stage), active_(enabled) {
      if (active_) started_ = std::chrono::steady_clock::now();
    }
    ~Scope() {
      if (!active_) return;
      auto &counter = counters[static_cast<std::size_t>(stage_)];
      counter.milliseconds += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started_).count();
      ++counter.calls;
    }
    Scope(const Scope &) = delete;
    Scope &operator=(const Scope &) = delete;

  private:
    Stage stage_;
    bool active_;
    std::chrono::steady_clock::time_point started_{};
  };
}

#define NO_TRACE_PROFILE_JOIN_INNER(first, second) first##second
#define NO_TRACE_PROFILE_JOIN(first, second) NO_TRACE_PROFILE_JOIN_INNER(first, second)

#if defined(NO_TRACE_PROFILE)
#define NO_TRACE_PROFILE_SCOPE(stage) ::NoTrace::Profiling::Scope NO_TRACE_PROFILE_JOIN(noTraceProfileScope_, __LINE__)(::NoTrace::Profiling::Stage::stage)
#define NO_TRACE_PROFILE_CALL(stage) ::NoTrace::Profiling::Count(::NoTrace::Profiling::Stage::stage)
#else
#define NO_TRACE_PROFILE_SCOPE(stage)
#define NO_TRACE_PROFILE_CALL(stage)
#endif
