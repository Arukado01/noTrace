#pragma once

#include "NoTrace.h"
#include "Config/NoTraceConfig.h"
#include "Config/NoTraceOptionsConfig.h"
#include "Config/NoTraceFirstPersonConfig.h"
#include "Core/EngineConfig.h"
#include <TGUI/Text.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <stdexcept>
#include <vector>

namespace EngineTests {
  template <typename RootWidgets, typename Capture>
  void VerifyMainMenuOptions(EngineContext &context, sf::RenderWindow &window,
                             const std::filesystem::path &output, RootWidgets getWidgets, Capture capture) {
    struct RestorePreferences {
      EngineContext &context;
      struct Entry { std::string_view key; bool present; nlohmann::json value; };
      std::vector<Entry> entries;
      float master, effects, ambience, music, cursor;
      explicit RestorePreferences(EngineContext &ctx)
          : context(ctx), master(ctx.audio.GetGlobalVolume()), effects(ctx.audio.GetEffectsVolume()), ambience(ctx.audio.GetAmbienceVolume()), music(ctx.audio.GetMusicVolume()), cursor(ctx.cursor.GetSpeed()) {
        for (const auto key : std::array<std::string_view, 7>{NoTrace::VOLUME_SAVE_KEY, NoTrace::OptionsConfig::EffectsVolumeSaveKey, NoTrace::OptionsConfig::AmbienceVolumeSaveKey, NoTrace::OptionsConfig::MusicVolumeSaveKey,
             NoTrace::CURSOR_SPEED_SAVE_KEY, "NoTrace:DiveKey", "NoTrace:RollKey"})
          entries.push_back({key, ctx.save.Has(key), ctx.save.Get(key, nlohmann::json{})});
      }
      ~RestorePreferences() {
        for (const auto &entry : entries) {
          if (entry.present) context.save.Set(entry.key, entry.value);
          else context.save.Erase(entry.key);
        }
        context.audio.SetGlobalVolume(master); context.audio.SetEffectsVolume(effects); context.audio.SetAmbienceVolume(ambience); context.audio.SetMusicVolume(music); context.cursor.SetSpeed(cursor);
      }
    } restore(context);
    const auto initialRoots = getWidgets().size();
    const auto originalWindowSize = window.getSize();
    NoTrace::Menu menu(context);
    struct Cleanup {
      NoTrace::Menu &menu;
      bool active = false;
      ~Cleanup() { if (active) menu.OnCleanup(); }
    } cleanup{menu};
    nlohmann::json checks = nlohmann::json::array(), metrics = nlohmann::json::array();
    const auto require = [&](bool condition, const std::string &message) {
      if (!condition) throw std::runtime_error(message);
      checks.push_back({{"result", "PASS"}, {"check", message}});
    };
    const auto visibleWidgets = [&] {
      std::vector<tgui::Widget::Ptr> result;
      const std::function<void(const tgui::Widget::Ptr &)> visit = [&](const tgui::Widget::Ptr &widget) {
        if (!widget->isVisible()) return;
        result.push_back(widget);
        if (const auto container = std::dynamic_pointer_cast<tgui::Container>(widget))
          for (const auto &child : container->getWidgets()) visit(child);
      };
      for (const auto &widget : getWidgets()) visit(widget);
      return result;
    };
    const auto findButton = [&](const char *text) -> tgui::Button::Ptr {
      for (const auto &widget : visibleWidgets())
        if (const auto button = std::dynamic_pointer_cast<tgui::Button>(widget); button && button->getText() == text) return button;
      return {};
    };
    const auto press = [&](const char *text) {
      const auto button = findButton(text);
      require(button && button->isEnabled(), std::string("El menú presenta una acción visible y habilitada: ") + text);
      button->onPress.emit(button.get(), button->getText());
    };
    const auto escape = [&] {
      sf::Event::KeyPressed key; key.scancode = sf::Keyboard::Scan::Escape; menu.OnEvent(key);
    };
    const auto measure = [&](const char *page) {
      for (const auto &widget : visibleWidgets()) {
        if (const auto label = std::dynamic_pointer_cast<tgui::Label>(widget); label && !label->getText().empty()) {
          const auto border = label->getSharedRenderer()->getBorders();
          const auto padding = label->getSharedRenderer()->getPadding();
          const float width = label->getSize().x - border.getLeftPlusRight() - padding.getLeftPlusRight();
          const float height = label->getSize().y - border.getTopPlusBottom() - padding.getTopPlusBottom();
          auto probe = tgui::Label::copy(label); probe->setAutoSize(true); probe->setMaximumTextWidth(std::max(1.f, width));
          const bool fits = probe->getSize().y <= label->getSize().y + 1.f;
          metrics.push_back({{"page", page}, {"kind", "label"}, {"text", label->getText().toStdString()},
                             {"availableWidth", width}, {"availableHeight", height}, {"fullHeight", probe->getSize().y}, {"fits", fits}});
          require(fits, std::string("El texto del menú cabe en su área: ") + label->getText().toStdString());
        } else if (const auto button = std::dynamic_pointer_cast<tgui::Button>(widget)) {
          const auto border = button->getSharedRenderer()->getBorders();
          tgui::Text probe; probe.setFont(button->getInheritedFont()); probe.setCharacterSize(button->getTextSize()); probe.setString(button->getText());
          const auto available = button->getSize() - tgui::Vector2f(border.getLeftPlusRight(), border.getTopPlusBottom());
          const bool fits = probe.getSize().x <= available.x + 1.f && probe.getSize().y <= available.y + 1.f;
          metrics.push_back({{"page", page}, {"kind", "button"}, {"text", button->getText().toStdString()},
                             {"availableWidth", available.x}, {"availableHeight", available.y}, {"fullWidth", probe.getSize().x}, {"fullHeight", probe.getSize().y}, {"fits", fits}});
          require(fits, std::string("El botón del menú cabe en su área: ") + button->getText().toStdString());
        }
        const auto position = widget->getAbsolutePosition();
        const auto size = widget->getSize();
        require(position.x >= -1.f && position.y >= -1.f && position.x + size.x <= gConfig.windowSize.x + 1.f && position.y + size.y <= gConfig.windowSize.y + 1.f,
                "Un widget visible del menú permanece dentro del área lógica de presentación");
      }
    };
    menu.Start(); cleanup.active = true;
    require(getWidgets().size() == initialRoots + 1, "Start del menú registra una única raíz GUI propia");
    require(findButton("NUEVA OPERACIÓN") != nullptr, "El menú principal real muestra Nueva operación");
    press("OPCIONES");
    require(findButton("APLICAR PANTALLA") && !findButton("NUEVA OPERACIÓN"), "Opciones oculta las acciones del menú principal");
    std::vector<tgui::ComboBox::Ptr> combos;
    std::vector<tgui::Slider::Ptr> sliders;
    for (const auto &widget : visibleWidgets()) {
      if (const auto combo = std::dynamic_pointer_cast<tgui::ComboBox>(widget)) combos.push_back(combo);
      if (const auto slider = std::dynamic_pointer_cast<tgui::Slider>(widget)) sliders.push_back(slider);
    }
    require(combos.size() == 4 && sliders.size() == 6, "El menú real ofrece cuatro selectores de vídeo y ajustes de audio, música, mando y campo visual FPS");
    require(combos[1]->getItems().size() == 3 && combos[1]->getItems()[2] == "Ventana sin bordes", "El menú real ofrece ventana, pantalla completa y sin bordes");
    require(combos[3]->getItemById("0") == "Sin límite" && combos[3]->getItemById("360") == "360", "El menú real ofrece límites de FPS funcionales e ilimitado");
    combos[1]->setSelectedItemByIndex(1); require(!combos[0]->isEnabled(), "El menú real deshabilita resolución de ventana al elegir pantalla completa");
    combos[1]->setSelectedItemByIndex(0); require(combos[0]->isEnabled(), "Volver a ventana recupera su resolución configurable");
    combos[2]->setSelectedItemByIndex(1); require(!combos[3]->isEnabled(), "El menú real prioriza VSync sobre el límite de FPS");
    combos[2]->setSelectedItemByIndex(0); require(combos[3]->isEnabled(), "Desactivar VSync recupera el selector de FPS");
    sliders[0]->setValue(25.f); sliders[0]->setFocused(true);
    sf::Event::KeyPressed right; right.scancode = sf::Keyboard::Scan::Right; menu.OnEvent(right);
    require(context.audio.GetGlobalVolume() == 26.f && context.save.Get(NoTrace::VOLUME_SAVE_KEY, 0.f) == 26.f,
            "El teclado del menú aplica y guarda el volumen general inmediatamente");
    sliders[1]->setValue(44.f); sliders[2]->setValue(40.f); sliders[3]->setValue(35.f); sliders[4]->setValue(1200.f);
    sliders[5]->setValue(91.f);
    require(context.save.Get(NoTrace::FirstPersonConfig::FovSaveKey, 0.f) == 91.f, "El menú real guarda inmediatamente el campo visual de primera persona");
    require(context.audio.GetEffectsVolume() == 44.f && context.audio.GetAmbienceVolume() == 40.f && context.audio.GetMusicVolume() == 35.f && context.cursor.GetSpeed() == 1200.f &&
            context.save.Get(NoTrace::OptionsConfig::EffectsVolumeSaveKey, 0.f) == 44.f && context.save.Get(NoTrace::OptionsConfig::AmbienceVolumeSaveKey, 0.f) == 40.f &&
            context.save.Get(NoTrace::OptionsConfig::MusicVolumeSaveKey, 0.f) == 35.f && context.save.Get(NoTrace::CURSOR_SPEED_SAVE_KEY, 0.f) == 1200.f,
            "Los ajustes reales del menú aplican y guardan efectos, ambiente, música y velocidad del mando");
    capture(menu, output / "main-menu-options.png");
    require(std::filesystem::is_regular_file(output / "main-menu-options.png"), "El menú real produce una captura de sus opciones");
    measure("main-menu-options");
    press("Controles");
    require(findButton("GUARDAR CONTROLES") && !findButton("APLICAR PANTALLA"), "El menú abre su panel real de controles sin cambiar de escena");
    capture(menu, output / "main-menu-controls.png");
    require(std::filesystem::is_regular_file(output / "main-menu-controls.png"), "El menú real produce una captura de sus controles");
    measure("main-menu-controls");
    combos.clear();
    for (const auto &widget : visibleWidgets()) if (const auto combo = std::dynamic_pointer_cast<tgui::ComboBox>(widget)) combos.push_back(combo);
    require(combos.size() == 2, "El menú real muestra las asignaciones de zambullida y rodada");
    combos[0]->setSelectedItemByIndex(4); combos[1]->setSelectedItemByIndex(3); press("GUARDAR CONTROLES");
    require(findButton("APLICAR PANTALLA") && context.save.Get("NoTrace:DiveKey", -1) == 4 && context.save.Get("NoTrace:RollKey", -1) == 3,
            "Guardar controles desde el menú conserva las teclas y regresa a opciones");
    press("Controles"); press("RESTAURAR CONTROLES");
    require(context.save.Get("NoTrace:DiveKey", -1) == 0 && context.save.Get("NoTrace:RollKey", -1) == 1 && findButton("APLICAR PANTALLA"),
            "El menú restaura los controles predeterminados y regresa a opciones");
    press("Controles"); escape();
    require(findButton("APLICAR PANTALLA") != nullptr, "Escape regresa de controles a opciones");
    escape(); require(findButton("NUEVA OPERACIÓN") != nullptr, "Escape regresa de opciones al menú principal");
    press("OPCIONES"); press("VOLVER"); require(findButton("NUEVA OPERACIÓN") != nullptr, "Volver del menú de opciones conserva la navegación principal");
    menu.OnCleanup(); cleanup.active = false;
    require(getWidgets().size() == initialRoots, "OnCleanup del menú retira su raíz GUI sin eliminar las anteriores");
    require(window.getSize() == originalWindowSize, "El ensayo de opciones del menú no cambia la ventana de la suite");
    std::ofstream report(output / "main-menu-options.json");
    report << nlohmann::json{{"result", "PASS"}, {"checks", checks}, {"textMetrics", metrics}}.dump(2);
    if (!report) throw std::runtime_error("No se pudo escribir la evidencia del menú real");
  }
}
