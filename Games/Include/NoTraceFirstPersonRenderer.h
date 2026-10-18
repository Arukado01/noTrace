#pragma once

#include "NoTraceCombatGeometry.h"
#include "Types/NoTraceFirstPersonTypes.h"
#include <SFML/Graphics/Image.hpp>
#include <memory>

struct EngineContext;

namespace NoTrace {
  class FirstPersonRenderer {
    struct State;
    EngineContext &context_;
    std::unique_ptr<State> state_;
  public:
    explicit FirstPersonRenderer(EngineContext &context);
    ~FirstPersonRenderer();
    void Reset(const WorldState &world, PlayerId player);
    void Update(const WorldState &world, PlayerId player, float deltaTime);
    void Render(const WorldState &world, PlayerId player, const FirstPersonCamera &camera, bool hudVisible = true) const;
    CombatGeometry::AimSolution Aim(const WorldState &world, PlayerId player, const FirstPersonCamera &camera) const;
    const FirstPersonRenderStats &Stats() const;
    float DepthAt(sf::Vector2f normalized) const;
    sf::Image FrameImage() const;
  };
}
