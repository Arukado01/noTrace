#pragma once

#include <TGUI/TGUI.hpp>
#include <string_view>

namespace NoTrace {
  inline constexpr std::string_view MENU_SCENE = "Menu";
  inline constexpr std::string_view RECEPTION_SCENE = "NoTraceReception";
  inline constexpr std::string_view VOLUME_SAVE_KEY = "NoTrace:GlobalVolume";
  inline constexpr std::string_view CURSOR_SPEED_SAVE_KEY = "NoTrace:CursorSpeed";
  inline constexpr float MIN_VOLUME = 0.f;
  inline constexpr float MAX_VOLUME = 100.f;
  inline constexpr float VOLUME_STEP = 1.f;
  inline constexpr float MIN_CURSOR_SPEED = 200.f;
  inline constexpr float MAX_CURSOR_SPEED = 1600.f;
  inline constexpr float CURSOR_SPEED_STEP = 50.f;
  inline constexpr float PAGE_FADE_DURATION = 0.24f;
  inline constexpr float BACKGROUND_ZOOM = 1.04f;
  inline constexpr unsigned int HEADING_TEXT_SIZE = 27;
  inline constexpr unsigned int BODY_TEXT_SIZE = 20;
  inline constexpr unsigned int SMALL_TEXT_SIZE = 15;
  inline constexpr unsigned int BUTTON_TEXT_SIZE = 28;
  inline constexpr std::string_view BACKGROUND_TEXTURE = "NoTrace/Backgrounds/MenuBackground.png";
  inline constexpr std::string_view LOGO_TEXTURE = "NoTrace/Branding/Logo.png";
  inline constexpr std::string_view EMBLEM_TEXTURE = "NoTrace/Branding/WindowIcon.png";
  inline const tgui::Color BACKGROUND_COLOR{4, 9, 15};
  inline const tgui::Color ACCENT_COLOR{230, 49, 67};
  inline const tgui::Color TEXT_COLOR{229, 233, 239};
  inline const tgui::Color MUTED_COLOR{143, 158, 177};
}
