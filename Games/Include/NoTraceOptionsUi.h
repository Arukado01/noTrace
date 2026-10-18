#pragma once

#include <TGUI/TGUI.hpp>
#include <SFML/Window/Event.hpp>
#include <functional>
#include <string>

struct EngineContext;

namespace NoTrace {
  struct OptionsWidgets {
    tgui::ComboBox::Ptr resolution;
    tgui::ComboBox::Ptr mode;
    tgui::ComboBox::Ptr verticalSync;
    tgui::ComboBox::Ptr framerate;
    tgui::Slider::Ptr masterVolume;
    tgui::Slider::Ptr effectsVolume;
    tgui::Slider::Ptr ambienceVolume;
    tgui::Slider::Ptr cursorSpeed;
    tgui::Label::Ptr status;
  };

  void ApplyUserPreferences(EngineContext &context);
  OptionsWidgets BuildOptionsUi(EngineContext &context, const tgui::Panel::Ptr &panel,
                                std::function<void()> onControls, std::function<void()> onBack,
                                const std::string &backText);
  void SyncOptionsUi(EngineContext &context, const OptionsWidgets &widgets);
  void BuildControlsUi(EngineContext &context, const tgui::Panel::Ptr &panel,
                       std::function<void()> onApply, std::function<void()> onBack);
  void HandleOptionsKey(const sf::Event &event, const OptionsWidgets &widgets);
}
