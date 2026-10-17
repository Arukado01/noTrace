#pragma once
#include <TGUI/Backend/SFML-Graphics.hpp>
#include <TGUI/TGUI.hpp>
#include <optional>
#include <vector>

namespace EngineTests { struct GuiAccess; }

class GuiManager {
private:
  sf::RenderWindow &window_;
  tgui::Gui gui_;
  std::vector<tgui::Widget::Ptr> modalDisabledWidgets_;
  tgui::Group::Ptr displayConfirmationGroup_;
  tgui::Label::Ptr displayConfirmationLabel_;
  tgui::Button::Ptr keepDisplayButton_;
  std::optional<bool> displayDecision_;

public:
  GuiManager(sf::RenderWindow &window);

  void Add(const tgui::Widget::Ptr &widget);
  void Remove(const tgui::Widget::Ptr &widget);
  bool IsTextInputFocused() const;

private:
  friend class Engine;
  friend struct EngineTests::GuiAccess;
  void ProcessEvent(const sf::Event &);
  void Render();
  void RebindWindow();
  void SyncPresentation(sf::Vector2f logicalSize, const sf::FloatRect &viewport);
  void BeginModal(const tgui::Widget::Ptr &widget);
  void EndModal();
  void SetDisplayConfirmation(bool visible, float remaining);
  std::optional<bool> FetchDisplayDecision();

private:
  void InitializeDisplayConfirmation();
  void HandleEvent(const sf::Event::JoystickButtonPressed &);
  void HandleEvent(const sf::Event::JoystickButtonReleased &);
  void HandleEvent(const auto &) {}
};
