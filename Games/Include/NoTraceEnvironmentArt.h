#pragma once

#include "Types/NoTraceGameplayTypes.h"
#include <SFML/Graphics/Color.hpp>
#include <array>
#include <functional>

struct EngineContext;
namespace sf { class Texture; }

namespace NoTrace {
  class EnvironmentArt {
  public:
    using Project = std::function<sf::Vector2f(sf::Vector2f, float)>;
    using Shade = std::function<float(sf::Vector2f)>;
    explicit EnvironmentArt(EngineContext &context);
    void Surface(const WalkSurface &surface, const Project &project, const Shade &shade = {}) const;
    void Wall(const Obstacle &wall, const Project &project) const;
    bool Object(const Interactable &object, sf::Vector2f feet) const;
    sf::Vector2f ObjectPoint(const Interactable &object, sf::Vector2f feet, sf::Vector2f sourcePoint) const;
    void Grip(const GripSurface &grip, const Project &project) const;
  private:
    struct Frame { sf::IntRect rect; sf::Vector2f anchor; sf::Vector2f size; float bottom = 0.f; };
    struct ObjectPlacement { sf::Vector2f scale, anchor; };
    static ObjectPlacement PlacementFor(const Interactable &object, const Frame &frame);
    EngineContext &context_;
    sf::Texture *furniture_;
    sf::Texture *materials_;
    std::array<Frame, 24> frames_;
    void Quad(const std::array<sf::Vector2f, 4> &points, std::size_t material, sf::Color tint, sf::Vector2f fraction = {1.f, 1.f}) const;
    void ColoredQuad(const std::array<sf::Vector2f, 4> &points, std::size_t material, const std::array<sf::Color, 4> &colors, sf::Vector2f fraction) const;
  };
}
