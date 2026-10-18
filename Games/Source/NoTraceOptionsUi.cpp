#include "NoTraceOptionsUi.h"
#include "NoTraceGame.h"
#include "Config/NoTraceConfig.h"
#include "Config/NoTraceOptionsConfig.h"
#include "Config/NoTraceControlsConfig.h"
#include "Config/NoTraceFirstPersonConfig.h"
#include "Core/EngineConfig.h"
#include "Core/EngineContext.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>
#include <SFML/System/String.hpp>

namespace NoTrace {
  namespace {
    float Preference(const SaveManager &save, std::string_view key, float fallback, float minimum, float maximum) {
      try {
        const float value = save.Get(key, fallback);
        return std::isfinite(value) ? std::clamp(value, minimum, maximum) : fallback;
      } catch (const nlohmann::json::exception &) { return fallback; }
    }

    int ControlPreference(const SaveManager &save, std::string_view key, int fallback) {
      try {
        const int value = save.Get(key, fallback);
        return value >= 0 && value < static_cast<int>(ControlsConfig::MovementKeys.size()) ? value : fallback;
      } catch (const nlohmann::json::exception &) { return fallback; }
    }

    std::string KeyName(ControlsConfig::Action action) {
      const auto key = ControlsConfig::DefaultKeys[static_cast<std::size_t>(action)];
      if (key == sf::Keyboard::Scan::LShift) return "Mayús";
      if (key == sf::Keyboard::Scan::LAlt) return "Alt izq.";
      if (key == sf::Keyboard::Scan::Tab) return "Tab";
      if (key == sf::Keyboard::Scan::PageUp) return "RePág";
      if (key == sf::Keyboard::Scan::PageDown) return "AvPág";
      const auto encoded = sf::Keyboard::getDescription(key).toUtf8();
      return {encoded.begin(), encoded.end()};
    }

    tgui::Label::Ptr Text(const tgui::Panel::Ptr &panel, const std::string &text, float x, float y,
                         float width, float height = 32.f, unsigned int size = 18) {
      auto label = tgui::Label::create(text);
      label->setPosition({x, y}); label->setSize({width, height}); label->setTextSize(size);
      label->getRenderer()->setTextColor(TEXT_COLOR); label->setIgnoreMouseEvents(true);
      panel->add(label); return label;
    }

    tgui::Button::Ptr Action(EngineContext &context, const tgui::Panel::Ptr &panel, const std::string &text,
                            float x, float y, float width, std::function<void()> callback) {
      auto button = tgui::Button::create(text);
      button->setPosition({x, y}); button->setSize({width, 36.f}); button->setTextSize(18);
      button->getRenderer()->setBackgroundColor({18, 32, 45}); button->getRenderer()->setBackgroundColorHover({61, 25, 37});
      button->getRenderer()->setBorderColor(ACCENT_COLOR); button->getRenderer()->setTextColor(TEXT_COLOR);
      button->onMouseEnter([&context] { context.cursor.SetHovered(true); });
      button->onMouseLeave([&context] { context.cursor.SetHovered(false); });
      button->onPress(std::move(callback)); panel->add(button); return button;
    }

    tgui::ComboBox::Ptr Choice(const tgui::Panel::Ptr &panel, float x, float y, float width) {
      auto box = tgui::ComboBox::create(); box->setPosition({x, y}); box->setSize({width, 36.f});
      box->setTextSize(19); box->setItemsToDisplay(5); panel->add(box); return box;
    }

    void ApplyDisplay(EngineContext &context, const OptionsWidgets &widgets) {
      const auto &modes = context.window.GetAvailableResolutions();
      const int resolution = widgets.resolution->getSelectedItemIndex();
      const int mode = widgets.mode->getSelectedItemIndex();
      const auto limitText = widgets.framerate->getSelectedItemId().toStdString();
      unsigned int limit = 0;
      const auto parsed = std::from_chars(limitText.data(), limitText.data() + limitText.size(), limit);
      if (mode < 0 || parsed.ec != std::errc{} || parsed.ptr != limitText.data() + limitText.size() ||
          (limit != 0 && (limit < 30 || limit > 360)) ||
          (mode != 1 && (resolution < 0 || static_cast<std::size_t>(resolution) >= modes.size()))) {
        widgets.status->setText("Selecciona una resolución y un límite de FPS válidos."); return;
      }
      auto settings = context.window.GetDisplaySettings();
      if (mode != 1) settings.resolution = modes[static_cast<std::size_t>(resolution)];
      settings.fullscreen = mode == 1; settings.borderless = mode == 2;
      settings.verticalSync = widgets.verticalSync->getSelectedItemIndex() == 1;
      settings.framerateLimit = limit;
      if (settings == context.window.GetDisplaySettings()) widgets.status->setText("La configuración seleccionada ya está activa.");
      else widgets.status->setText(context.window.RequestDisplaySettings(settings) ?
        "Confirma el cambio o espera su restauración automática." : "No se puede aplicar esta configuración de pantalla.");
    }
  }

  void ApplyUserPreferences(EngineContext &context) {
    const float master = Preference(context.save, VOLUME_SAVE_KEY, gConfig.globalVolume, MIN_VOLUME, MAX_VOLUME);
    const float effects = Preference(context.save, OptionsConfig::EffectsVolumeSaveKey, OptionsConfig::EffectsVolume, MIN_VOLUME, MAX_VOLUME);
    const float ambience = Preference(context.save, OptionsConfig::AmbienceVolumeSaveKey, OptionsConfig::AmbienceVolume, MIN_VOLUME, MAX_VOLUME);
    const float cursor = Preference(context.save, CURSOR_SPEED_SAVE_KEY, gConfig.cursorSpeed, MIN_CURSOR_SPEED, MAX_CURSOR_SPEED);
    context.audio.SetGlobalVolume(master); context.audio.SetEffectsVolume(effects); context.audio.SetAmbienceVolume(ambience); context.cursor.SetSpeed(cursor);
    context.save.Set(VOLUME_SAVE_KEY, master); context.save.Set(OptionsConfig::EffectsVolumeSaveKey, effects);
    context.save.Set(OptionsConfig::AmbienceVolumeSaveKey, ambience);
    context.save.Set(CURSOR_SPEED_SAVE_KEY, cursor);
    context.save.Set(FirstPersonConfig::FovSaveKey, Preference(context.save, FirstPersonConfig::FovSaveKey,
      FirstPersonConfig::DefaultFov, FirstPersonConfig::MinimumFov, FirstPersonConfig::MaximumFov));
    const int dive = ControlPreference(context.save, ControlsConfig::DiveSaveKey, 0);
    int roll = ControlPreference(context.save, ControlsConfig::RollSaveKey, 1);
    if (roll == dive) roll = dive == 1 ? 0 : 1;
    context.save.Set(ControlsConfig::DiveSaveKey, dive); context.save.Set(ControlsConfig::RollSaveKey, roll);
  }

  OptionsWidgets BuildOptionsUi(EngineContext &context, const tgui::Panel::Ptr &panel,
                                std::function<void()> onControls, std::function<void()> onBack,
                                const std::string &backText) {
    OptionsWidgets widgets;
    Text(panel, "OPCIONES / NO TRACE", 30.f, 20.f, 714.f, 42.f, 27);
    Text(panel, "Resolución de ventana", 30.f, 78.f, 300.f);
    widgets.resolution = Choice(panel, 340.f, 74.f, 404.f);
    Text(panel, "Modo de pantalla", 30.f, 124.f, 300.f);
    widgets.mode = Choice(panel, 340.f, 120.f, 404.f);
    Text(panel, "VSync", 30.f, 170.f, 160.f);
    widgets.verticalSync = Choice(panel, 185.f, 166.f, 185.f);
    widgets.verticalSync->addItem("Desactivado"); widgets.verticalSync->addItem("Activado");
    Text(panel, "Límite FPS", 405.f, 170.f, 150.f);
    widgets.framerate = Choice(panel, 555.f, 166.f, 189.f);
    for (const auto limit : OptionsConfig::FramerateLimits) widgets.framerate->addItem(limit ? std::to_string(limit) : "Sin límite", std::to_string(limit));
    widgets.mode->onItemSelect([resolution = widgets.resolution](const tgui::String &item) {
      resolution->setEnabled(item != "Pantalla completa");
    });
    widgets.verticalSync->onItemSelect([framerate = widgets.framerate](const tgui::String &item) {
      framerate->setEnabled(item != "Activado");
    });
    const auto volume = [&](const char *title, float y, float value, auto callback) {
      auto label = Text(panel, std::format("{}: {}%", title, static_cast<int>(std::lround(value))), 30.f, y, 300.f);
      auto slider = tgui::Slider::create(MIN_VOLUME, MAX_VOLUME); slider->setStep(VOLUME_STEP);
      slider->setPosition({340.f, y + 8.f}); slider->setSize({404.f, 22.f}); slider->setFocusable(true); slider->setValue(value);
      slider->onValueChange([label, title, callback](float changed) {
        callback(changed); label->setText(std::format("{}: {}%", title, static_cast<int>(std::lround(changed))));
      });
      panel->add(slider); return slider;
    };
    widgets.masterVolume = volume("Volumen general", 210.f, context.audio.GetGlobalVolume(), [&context](float value) {
      context.audio.SetGlobalVolume(value); context.save.Set(VOLUME_SAVE_KEY, context.audio.GetGlobalVolume());
    });
    widgets.effectsVolume = volume("Volumen de efectos", 244.f, context.audio.GetEffectsVolume(), [&context](float value) {
      context.audio.SetEffectsVolume(value); context.save.Set(OptionsConfig::EffectsVolumeSaveKey, context.audio.GetEffectsVolume());
    });
    widgets.ambienceVolume = volume("Volumen de ambiente", 278.f, context.audio.GetAmbienceVolume(), [&context](float value) {
      context.audio.SetAmbienceVolume(value); context.save.Set(OptionsConfig::AmbienceVolumeSaveKey, context.audio.GetAmbienceVolume());
    });
    auto cursorLabel = Text(panel, "Cursor del mando: " + std::to_string(static_cast<int>(context.cursor.GetSpeed())), 30.f, 312.f, 300.f);
    widgets.cursorSpeed = tgui::Slider::create(MIN_CURSOR_SPEED, MAX_CURSOR_SPEED);
    widgets.cursorSpeed->setStep(CURSOR_SPEED_STEP); widgets.cursorSpeed->setFocusable(true);
    widgets.cursorSpeed->setPosition({340.f, 320.f}); widgets.cursorSpeed->setSize({404.f, 22.f});
    widgets.cursorSpeed->setValue(context.cursor.GetSpeed());
    widgets.cursorSpeed->onValueChange([&context, cursorLabel](float value) {
      context.cursor.SetSpeed(value); context.save.Set(CURSOR_SPEED_SAVE_KEY, value);
      cursorLabel->setText("Cursor del mando: " + std::to_string(static_cast<int>(value)));
    });
    panel->add(widgets.cursorSpeed);
    const float fov = Preference(context.save, FirstPersonConfig::FovSaveKey, FirstPersonConfig::DefaultFov, FirstPersonConfig::MinimumFov, FirstPersonConfig::MaximumFov);
    auto fovLabel = Text(panel, std::format("Visión FPS: {:.0f}°", fov), 30.f, 346.f, 300.f);
    widgets.firstPersonFov = tgui::Slider::create(FirstPersonConfig::MinimumFov, FirstPersonConfig::MaximumFov);
    widgets.firstPersonFov->setStep(1.f); widgets.firstPersonFov->setFocusable(true);
    widgets.firstPersonFov->setPosition({340.f, 354.f}); widgets.firstPersonFov->setSize({404.f, 22.f}); widgets.firstPersonFov->setValue(fov);
    widgets.firstPersonFov->onValueChange([&context, fovLabel](float value) {
      context.save.Set(FirstPersonConfig::FovSaveKey, value); fovLabel->setText(std::format("Visión FPS: {:.0f}°", value));
    });
    panel->add(widgets.firstPersonFov);
    Text(panel, "Pantalla completa usa el escritorio. VSync sustituye el límite de FPS.", 30.f, 378.f, 714.f, 22.f, 13);
    widgets.status = Text(panel, "Aplica y confirma vídeo. Los demás ajustes se guardan al cambiarlos.", 30.f, 400.f, 714.f, 28.f, 16);
    Action(context, panel, "Controles", 30.f, 434.f, 714.f, std::move(onControls));
    Action(context, panel, "RESTABLECER VÍDEO", 30.f, 474.f, 345.f, [&context, widgets] {
      const auto defaults = context.window.GetDefaultDisplaySettings();
      if (defaults != context.window.GetDisplaySettings()) context.window.RequestDisplaySettings(defaults);
      else SyncOptionsUi(context, widgets);
      widgets.status->setText("Vídeo predeterminado solicitado; confirma el cambio de pantalla.");
    });
    Action(context, panel, "RESTABLECER AUDIO Y MANDO", 399.f, 474.f, 345.f, [widgets] {
      widgets.masterVolume->setValue(gConfig.globalVolume); widgets.effectsVolume->setValue(OptionsConfig::EffectsVolume);
      widgets.ambienceVolume->setValue(OptionsConfig::AmbienceVolume);
      widgets.cursorSpeed->setValue(gConfig.cursorSpeed);
      widgets.status->setText("Audio y velocidad del mando restaurados y guardados.");
    });
    Action(context, panel, "APLICAR PANTALLA", 30.f, 514.f, 714.f, [&context, widgets] { ApplyDisplay(context, widgets); });
    Action(context, panel, backText, 30.f, 554.f, 714.f, std::move(onBack));
    SyncOptionsUi(context, widgets); return widgets;
  }

  void SyncOptionsUi(EngineContext &context, const OptionsWidgets &widgets) {
    const auto settings = context.window.GetDisplaySettings();
    const auto &modes = context.window.GetAvailableResolutions();
    widgets.resolution->removeAllItems();
    for (const auto size : modes) widgets.resolution->addItem(std::format("{} x {}", size.x, size.y));
    const auto selectedResolution = settings.fullscreen ? context.window.GetWindowedResolution() : settings.resolution;
    const auto found = std::find(modes.begin(), modes.end(), selectedResolution);
    if (found != modes.end()) widgets.resolution->setSelectedItemByIndex(static_cast<std::size_t>(found - modes.begin()));
    else if (!modes.empty()) widgets.resolution->setSelectedItemByIndex(0);
    widgets.mode->removeAllItems(); widgets.mode->addItem("Ventana");
    widgets.mode->addItem("Pantalla completa"); widgets.mode->addItem("Ventana sin bordes");
    widgets.mode->setSelectedItemByIndex(settings.fullscreen ? 1 : settings.borderless ? 2 : 0);
    widgets.resolution->setEnabled(!settings.fullscreen);
    widgets.verticalSync->setSelectedItemByIndex(settings.verticalSync ? 1 : 0);
    const auto limit = std::to_string(settings.framerateLimit);
    if (widgets.framerate->getItemById(limit).empty()) widgets.framerate->addItem(limit, limit);
    widgets.framerate->setSelectedItemById(limit);
    widgets.framerate->setEnabled(!settings.verticalSync);
    widgets.masterVolume->setValue(context.audio.GetGlobalVolume());
    widgets.effectsVolume->setValue(context.audio.GetEffectsVolume());
    widgets.ambienceVolume->setValue(context.audio.GetAmbienceVolume());
    widgets.cursorSpeed->setValue(context.cursor.GetSpeed());
    widgets.firstPersonFov->setValue(Preference(context.save, FirstPersonConfig::FovSaveKey, FirstPersonConfig::DefaultFov, FirstPersonConfig::MinimumFov, FirstPersonConfig::MaximumFov));
  }

  void BuildControlsUi(EngineContext &context, const tgui::Panel::Ptr &panel,
                       std::function<void()> onApply, std::function<void()> onBack) {
    using Control = ControlsConfig::Action;
    Text(panel, "CONTROLES", 30.f, 20.f, 714.f, 42.f, 27);
    const int diveKey = ControlPreference(context.save, ControlsConfig::DiveSaveKey, 0);
    const int rollKey = ControlPreference(context.save, ControlsConfig::RollSaveKey, 1);
    const std::array<std::string, 6> descriptions{
      std::format("{}{}{}{} / flechas: movimiento\n{}: correr · {}: agacharse · {}: tumbarse\n{}: zambullida · {}: rodada", KeyName(Control::Up), KeyName(Control::Left), KeyName(Control::Down), KeyName(Control::Right),
        KeyName(Control::Run), KeyName(Control::Crouch), KeyName(Control::Prone), ControlsConfig::MovementKeyNames[diveKey], ControlsConfig::MovementKeyNames[rollKey]),
      std::format("Ratón izq.: disparar · der.: apuntar\n{}: recarga · {}/{}: arma · {}: silenciador\nMayús+F: cambiar vista · Ratón: mirar", KeyName(Control::Reload), KeyName(Control::SelectPistol), KeyName(Control::SelectNonLethal), KeyName(Control::Suppressor)),
      std::format("{}: interactuar / rescatar\n{}: golpes; mantener para agarrar\n{} en agarre: someter · {}: empujar", KeyName(Control::Interact), KeyName(Control::Cqc), KeyName(Control::Cqc), KeyName(Control::Shove)),
      std::format("{}: cobertura / asomarse · {}: pared\n{}: entrar / salir; mantener: arrastrar\n{}: armario / voltear mesa", KeyName(Control::WallCover), KeyName(Control::Cqc), KeyName(Control::Interact), KeyName(Control::Tool)),
      std::format("{}: mantener / soltar señuelo\n{}: agarre / trepar · Mayús+{}: soltar\nRueda: zoom táctico", KeyName(Control::Decoy), KeyName(Control::Traverse), KeyName(Control::Traverse)),
      std::format("Mantén {}: nombres y orden privada\nMantén {}: armas · {}/{}: espectador\n{}: sala · Esc: pausa · F11: pantalla", KeyName(Control::Orders), KeyName(Control::WeaponMenu), KeyName(Control::SpectatePrevious), KeyName(Control::SpectateNext), KeyName(Control::Lobby))
    };
    for (std::size_t index = 0; index < descriptions.size(); ++index) {
      const float x = index % 2 == 0 ? 30.f : 399.f, y = 70.f + static_cast<float>(index / 2) * 100.f;
      auto title = Text(panel, std::string(ControlsConfig::Categories[index]), x, y, 345.f, 24.f, 16);
      title->getRenderer()->setTextColor({110, 200, 147});
      Text(panel, descriptions[index], x, y + 25.f, 345.f, 72.f, 14);
    }
    Text(panel, "Reasignación de maniobras", 30.f, 382.f, 714.f, 24.f, 18);
    const auto selector = [&](const char *name, float x, int selected) {
      Text(panel, name, x, 410.f, 345.f, 22.f, 16);
      auto box = Choice(panel, x, 434.f, 345.f);
      for (const auto key : ControlsConfig::MovementKeyNames) box->addItem(std::string(key));
      box->setSelectedItemByIndex(selected); return box;
    };
    auto dive = selector("Zambullida (correr de pie)", 30.f, diveKey);
    auto roll = selector("Rodada (agachado)", 399.f, rollKey);
    auto status = Text(panel, "Sólo estas dos maniobras admiten reasignación. Elige teclas distintas.", 30.f, 474.f, 714.f, 26.f, 15);
    Action(context, panel, "GUARDAR CONTROLES", 30.f, 506.f, 714.f, [&context, dive, roll, status, onApply] {
      const int diveKey = dive->getSelectedItemIndex(), rollKey = roll->getSelectedItemIndex();
      if (diveKey < 0 || rollKey < 0 || diveKey == rollKey) { status->setText("Selecciona dos teclas diferentes."); return; }
      context.save.Set(ControlsConfig::DiveSaveKey, diveKey); context.save.Set(ControlsConfig::RollSaveKey, rollKey); onApply();
    });
    Action(context, panel, "RESTAURAR CONTROLES", 30.f, 548.f, 345.f, [&context, onApply] {
      context.save.Set(ControlsConfig::DiveSaveKey, 0); context.save.Set(ControlsConfig::RollSaveKey, 1); onApply();
    });
    Action(context, panel, "VOLVER A OPCIONES", 399.f, 548.f, 345.f, std::move(onBack));
  }

  void HandleOptionsKey(const sf::Event &event, const OptionsWidgets &widgets) {
    const auto *key = event.getIf<sf::Event::KeyPressed>(); if (!key) return;
    for (const auto &slider : {widgets.masterVolume, widgets.effectsVolume, widgets.ambienceVolume, widgets.cursorSpeed, widgets.firstPersonFov}) {
      if (!slider || !slider->isFocused()) continue;
      switch (key->scancode) {
      case sf::Keyboard::Scan::Left: slider->setValue(slider->getValue() - slider->getStep()); break;
      case sf::Keyboard::Scan::Right: slider->setValue(slider->getValue() + slider->getStep()); break;
      case sf::Keyboard::Scan::Home: slider->setValue(slider->getMinimum()); break;
      case sf::Keyboard::Scan::End: slider->setValue(slider->getMaximum()); break;
      default: break;
      }
      return;
    }
  }

  void Game::BuildOptions() {
    options_ = BuildOptionsUi(ctx, panel_, [this] { Open(Page::Controls); }, [this] { Open(Page::Pause); }, "VOLVER A LA PAUSA");
    resolutionBox_ = options_.resolution; modeBox_ = options_.mode; status_ = options_.status;
    observedDisplay_ = ctx.window.GetDisplaySettings();
  }

  void Game::SyncDisplayOptions() {
    observedDisplay_ = ctx.window.GetDisplaySettings(); SyncOptionsUi(ctx, options_);
  }
}
