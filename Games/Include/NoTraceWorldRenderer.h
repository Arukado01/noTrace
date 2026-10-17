#pragma once

#include "Types/NoTraceGameplayTypes.h"
#include "Config/NoTracePresentationConfig.h"
#include <SFML/Graphics/Color.hpp>
#include <cmath>
#include <initializer_list>

struct EngineContext;
namespace sf { class Font; }

namespace NoTrace {
  class WorldRenderer {
  private:
    EngineContext &ctx_;
    const sf::Font *font_;

    static sf::Vector2f SnapToPixels(sf::Vector2f value) {
      return {std::round(value.x / PresentationConfig::PixelSize) * PresentationConfig::PixelSize,
              std::round(value.y / PresentationConfig::PixelSize) * PresentationConfig::PixelSize};
    }

    static sf::Vector2f LogicalCenter(sf::Vector2f logicalSize) {
      return SnapToPixels(logicalSize.componentWiseMul(PresentationConfig::CameraFocusRatio));
    }

    sf::Vector2f CameraGround(const WorldState &world, PlayerId localPlayer) const;
    sf::Vector2f GroundToScreen(sf::Vector2f ground, sf::Vector2f camera) const;
    void Polygon(std::initializer_list<sf::Vector2f> points, sf::Color color) const;
    void PixelRect(sf::Vector2f position, sf::Vector2f size, sf::Color color) const;
    void PixelLine(sf::Vector2f from, sf::Vector2f to, sf::Color color, float thickness = 2.f) const;
    void Label(const std::string &text, sf::Vector2f position, sf::Color color, unsigned int size = PresentationConfig::WorldLabelSize) const;
    void Box(sf::FloatRect bounds, float height, sf::Vector2f camera, sf::Color top, sf::Color left, sf::Color right) const;
    void Floor(const WorldState &world, sf::Vector2f camera) const;
    void ObstacleArt(const Obstacle &obstacle, sf::Vector2f camera) const;
    void ObjectArt(const Interactable &object, sf::Vector2f camera, bool nearby) const;
    void Humanoid(sf::Vector2f feet, sf::Vector2f facing, Stance stance, sf::Color uniform,
                  float animation, bool moving, bool running, bool down, bool grappling) const;
    void AgentArt(const Agent &agent, const WorldState &world, sf::Vector2f camera, bool local) const;
    void GuardArt(const Guard &guard, const WorldState &world, sf::Vector2f camera) const;
    void Vision(const Guard &guard, const WorldState &world, sf::Vector2f camera) const;
    void FieldOfView(sf::Vector2f position, sf::Vector2f facing, float range, sf::Color color, const WorldState &world, sf::Vector2f camera) const;
    void Noise(const NoiseEvent &noise, sf::Vector2f camera) const;
    void Minimap(const WorldState &world, PlayerId localPlayer) const;

  public:
    explicit WorldRenderer(EngineContext &context);
    void Render(const WorldState &world, PlayerId localPlayer) const;
    sf::Vector2f ScreenToGround(sf::Vector2f logical, const WorldState &world, PlayerId localPlayer) const;
    static sf::Vector2f ProjectGround(sf::Vector2f ground, sf::Vector2f camera, sf::Vector2f logicalSize) {
      const auto delta = ground - camera;
      return SnapToPixels(LogicalCenter(logicalSize) + sf::Vector2f{(delta.x - delta.y) * PresentationConfig::GroundProjection.x,
                                                                  (delta.x + delta.y) * PresentationConfig::GroundProjection.y});
    }
    static sf::Vector2f UnprojectGround(sf::Vector2f logical, sf::Vector2f camera, sf::Vector2f logicalSize) {
      const auto delta = logical - LogicalCenter(logicalSize);
      const float horizontal = delta.x / PresentationConfig::GroundProjection.x;
      const float vertical = delta.y / PresentationConfig::GroundProjection.y;
      return camera + sf::Vector2f{(horizontal + vertical) * 0.5f, (vertical - horizontal) * 0.5f};
    }
  };
}
