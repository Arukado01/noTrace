#include "Managers/CursorManager.h"

#include <SFML/Graphics/Sprite.hpp>
#include <SFML/Window/Joystick.hpp>
#include <SFML/Window/Mouse.hpp>

#include <algorithm>

#include "Core/EngineConfig.h"
#include "Managers/ResourceManager.h"

CursorManager::CursorManager(sf::RenderWindow &window, ResourceManager &resources)
    : window_(window), shape_(gConfig.cursorRadius),
      texture_(resources.FetchTexture(gConfig.cursorTexture)),
      hoverTexture_(resources.FetchTexture(gConfig.cursorHoverTexture)),
      speed_(gConfig.cursorSpeed), visible_(true), hovered_(false) {
  shape_.setFillColor(gConfig.cursorColor);
  shape_.setOrigin(shape_.getGeometricCenter());
  shape_.setPosition(gConfig.windowSize / 2.f);
  window_.setMouseCursorVisible(false);
}

void CursorManager::Update(float deltaTime) {
  if (window_.getSize().x != 0 && window_.getSize().y != 0) {
    shape_.setPosition(window_.mapPixelToCoords(sf::Mouse::getPosition(window_), window_.getView()));
  }
  if (!sf::Joystick::isConnected(0)) {
    return;
  }

  sf::Vector2f joystickDirection(
      sf::Joystick::getAxisPosition(0, sf::Joystick::Axis::X) / 100,
      sf::Joystick::getAxisPosition(0, sf::Joystick::Axis::Y) / 100);

  const float directionLength = joystickDirection.length();
  if (directionLength > gConfig.joystickDeadzone) {
    joystickDirection /= std::max(1.f, directionLength);
    shape_.move(joystickDirection * speed_ * std::max(0.f, deltaTime));
    const sf::View &presentation = window_.getView();
    const sf::Vector2f minimum = presentation.getCenter() - presentation.getSize() / 2.f;
    const sf::Vector2f maximum = minimum + presentation.getSize() - sf::Vector2f(1.f, 1.f);
    shape_.setPosition(
        {std::clamp(shape_.getPosition().x, minimum.x, std::max(minimum.x, maximum.x)),
         std::clamp(shape_.getPosition().y, minimum.y, std::max(minimum.y, maximum.y))});

    SetPosition(shape_.getPosition());
  }
}

void CursorManager::Render() const {
  const sf::Vector2u windowSize = window_.getSize();
  if ((!visible_ && !modalVisible_) || windowSize.x == 0 || windowSize.y == 0) {
    return;
  }

  const sf::View previousView = window_.getView();
  const sf::Vector2f pixelPosition(window_.mapCoordsToPixel(shape_.getPosition(), previousView));
  // La punta comparte la muestra del juego y conserva su tamaño físico fuera del escalado lógico.
  window_.setView(sf::View(sf::FloatRect({0.f, 0.f}, sf::Vector2f(windowSize))));

  const sf::Texture *selected = hovered_ && hoverTexture_ ? hoverTexture_ : texture_;
  if (selected && selected->getSize().x > 0 && selected->getSize().y > 0) {
    sf::Sprite sprite(*selected);
    sprite.setPosition(pixelPosition);
    sprite.setScale({gConfig.cursorSize / selected->getSize().x,
                     gConfig.cursorSize / selected->getSize().y});
    window_.draw(sprite);
  } else {
    sf::CircleShape fallback = shape_;
    fallback.setPosition(pixelPosition);
    window_.draw(fallback);
  }

  window_.setView(previousView);
}

void CursorManager::SetSpeed(float speed) { speed_ = std::max(0.f, speed); }

float CursorManager::GetSpeed() const { return speed_; }

void CursorManager::SetVisible(bool visible) {
  visible_ = visible;
  window_.setMouseCursorVisible(false);
}

bool CursorManager::IsVisible() const { return visible_; }

void CursorManager::SetHovered(bool hovered) { hovered_ = hovered; }

void CursorManager::SetModalVisible(bool visible) {
  modalVisible_ = visible;
  hovered_ = false;
  window_.setMouseCursorVisible(false);
}

void CursorManager::SetPosition(sf::Vector2f position) {
  shape_.setPosition(position);
  sf::Mouse::setPosition(
      window_.mapCoordsToPixel(position, window_.getView()), window_);
}

sf::Vector2f CursorManager::GetPosition() const {
  // La simulación y el dibujo comparten una sola muestra del cursor durante cada fotograma.
  return shape_.getPosition();
}

sf::Vector2f CursorManager::GetPosition(const sf::View &view) const {
  if (window_.getSize().x == 0 || window_.getSize().y == 0) {
    return shape_.getPosition();
  }

  const sf::FloatRect presentation = window_.getView().getViewport();
  const sf::FloatRect viewport = view.getViewport();
  sf::View composedView = view;
  // El viewport del mundo ocupa una región de la presentación, que puede tener bandas laterales.
  composedView.setViewport(sf::FloatRect(
      {presentation.position.x + viewport.position.x * presentation.size.x,
       presentation.position.y + viewport.position.y * presentation.size.y},
      {viewport.size.x * presentation.size.x, viewport.size.y * presentation.size.y}));
  return window_.mapPixelToCoords(window_.mapCoordsToPixel(shape_.getPosition(), window_.getView()), composedView);
}
