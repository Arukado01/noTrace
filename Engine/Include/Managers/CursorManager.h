#pragma once
#include <SFML/Graphics/CircleShape.hpp>
#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/Graphics/View.hpp>

class ResourceManager;

class CursorManager {
private:
  sf::RenderWindow &window_;
  sf::CircleShape shape_;
  sf::Texture *texture_;
  sf::Texture *hoverTexture_;
  float speed_;
  bool visible_;
  bool hovered_;
  bool modalVisible_ = false;

public:
  CursorManager(sf::RenderWindow &window, ResourceManager &resources);

  void SetSpeed(float speed);
  float GetSpeed() const;

  void SetVisible(bool visible);
  bool IsVisible() const;
  void SetHovered(bool hovered);
  void SetModalVisible(bool visible);

  void SetPosition(sf::Vector2f position);
  sf::Vector2f GetPosition() const;
  sf::Vector2f GetPosition(const sf::View &view) const;

private:
  friend class Engine;
  void Update(float deltaTime);
  void Render() const;
};
