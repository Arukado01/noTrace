#include "Managers/GuiManager.h"

#include "Utils/InputBindings.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

GuiManager::GuiManager(sf::RenderWindow &window)
    : window_(window), gui_(window) {
  tgui::Theme::setDefault("Content/Themes/Dark.txt");

  gui_.setTabKeyUsageEnabled(true);
  gui_.setFont("Content/Fonts/Relidux.otf");
  InitializeDisplayConfirmation();
}

void GuiManager::InitializeDisplayConfirmation() {
  displayConfirmationGroup_ = tgui::Group::create();
  displayConfirmationGroup_->setSize("100%", "100%");
  displayConfirmationGroup_->setVisible(false);

  auto shade = tgui::Panel::create({"100%", "100%"});
  shade->getRenderer()->setBackgroundColor({0, 0, 0, 205});
  displayConfirmationGroup_->add(shade);

  auto panel = tgui::Panel::create({660.f, 318.f});
  panel->setPosition({470.f, 291.f});
  panel->getRenderer()->setBackgroundColor({9, 15, 22});
  panel->getRenderer()->setBorderColor({174, 40, 53});
  panel->getRenderer()->setBorders(1);
  shade->add(panel);

  displayConfirmationLabel_ = tgui::Label::create();
  displayConfirmationLabel_->setPosition({30.f, 25.f});
  displayConfirmationLabel_->setSize({600.f, 155.f});
  displayConfirmationLabel_->setTextSize(25);
  displayConfirmationLabel_->getRenderer()->setTextColor({230, 233, 239});
  panel->add(displayConfirmationLabel_);

  keepDisplayButton_ = tgui::Button::create("CONSERVAR");
  keepDisplayButton_->setPosition({30.f, 218.f});
  keepDisplayButton_->setSize({285.f, 60.f});
  keepDisplayButton_->setTextSize(23);
  keepDisplayButton_->onPress([this] { displayDecision_ = true; });
  panel->add(keepDisplayButton_);

  auto revert = tgui::Button::create("REVERTIR");
  revert->setPosition({345.f, 218.f});
  revert->setSize({285.f, 60.f});
  revert->setTextSize(23);
  revert->onPress([this] { displayDecision_ = false; });
  panel->add(revert);
  gui_.add(displayConfirmationGroup_);
}

void GuiManager::ProcessEvent(const sf::Event &event) {
  event.visit([this](const auto &type) {
    this->HandleEvent(type);
    this->gui_.handleEvent(type);
  });
}

void GuiManager::Render() { gui_.draw(); }

void GuiManager::RebindWindow() { gui_.setWindow(window_); }

void GuiManager::Add(const tgui::Widget::Ptr &widget) { gui_.add(widget); }

void GuiManager::Remove(const tgui::Widget::Ptr &widget) {
  gui_.remove(widget);
}

void GuiManager::SyncPresentation(sf::Vector2f logicalSize, const sf::FloatRect &viewport) {
  gui_.setAbsoluteView({0, 0, logicalSize.x, logicalSize.y});
  gui_.setRelativeViewport({viewport.position.x, viewport.position.y,
                            viewport.size.x, viewport.size.y});
}

void GuiManager::BeginModal(const tgui::Widget::Ptr &widget) {
  modalDisabledWidgets_.clear();
  for (const auto &other : gui_.getWidgets()) {
    if (other != widget && other->isEnabled()) {
      modalDisabledWidgets_.push_back(other);
      other->setEnabled(false);
    }
  }
  widget->setEnabled(true);
  widget->setVisible(true);
  widget->moveToFront();
}

void GuiManager::EndModal() {
  for (const auto &widget : modalDisabledWidgets_) {
    widget->setEnabled(true);
  }
  modalDisabledWidgets_.clear();
}

void GuiManager::SetDisplayConfirmation(bool visible, float remaining) {
  if (!visible) {
    if (displayConfirmationGroup_->isVisible()) {
      displayConfirmationGroup_->setVisible(false);
      EndModal();
    }
    displayDecision_.reset();
    return;
  }

  displayConfirmationLabel_->setText(
      "¿CONSERVAR ESTA CONFIGURACIÓN?\n\nSe restaurará la anterior en " +
      std::to_string(static_cast<int>(std::ceil(std::max(remaining, 0.f)))) +
      " s.\nEscape también revierte el cambio.");
  if (!displayConfirmationGroup_->isVisible()) {
    displayDecision_.reset();
    BeginModal(displayConfirmationGroup_);
    keepDisplayButton_->setFocused(true);
  }
}

std::optional<bool> GuiManager::FetchDisplayDecision() {
  return std::exchange(displayDecision_, std::nullopt);
}

void GuiManager::HandleEvent(const sf::Event::JoystickButtonPressed &joystick) {
  if (Input::HardwareToLogical(joystick.button, joystick.joystickId) ==
      GamepadButton::South) {
    gui_.handleEvent(
        sf::Event::MouseButtonPressed{{}, sf::Mouse::getPosition(window_)});
  }
}

void GuiManager::HandleEvent(
    const sf::Event::JoystickButtonReleased &joystick) {
  if (Input::HardwareToLogical(joystick.button, joystick.joystickId) ==
      GamepadButton::South) {
    gui_.handleEvent(
        sf::Event::MouseButtonReleased{{}, sf::Mouse::getPosition(window_)});
  }
}
