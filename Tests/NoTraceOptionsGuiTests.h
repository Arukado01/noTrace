#pragma once

#include "NoTraceOptionsUi.h"
#include "Config/NoTraceConfig.h"
#include "Config/NoTraceOptionsConfig.h"
#include "Config/NoTraceControlsConfig.h"
#include "Config/NoTraceFirstPersonConfig.h"
#include "Core/EngineConfig.h"
#include "Core/EngineContext.h"
#include <SFML/Audio/Listener.hpp>
#include <SFML/Audio/Sound.hpp>
#include <SFML/Audio/SoundBuffer.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>

namespace EngineTests {
  inline void VerifyOptionsPreferences(sf::RenderWindow &window, const std::filesystem::path &output) {
    struct RestoreEnvironment {
      std::wstring previous;
      RestoreEnvironment() { if (const auto *value = _wgetenv(L"AWESOME_ENGINE_SAVE_FILE")) previous = value; }
      ~RestoreEnvironment() {
        _wputenv_s(L"AWESOME_ENGINE_SAVE_FILE", previous.c_str());
        sf::Listener::setGlobalVolume(0.f);
      }
    } restore;
    const auto directory = std::filesystem::absolute(output / "options-persistence");
    std::filesystem::create_directories(directory);
    const auto saveFile = directory / L"Preferencias de presentación ñ.json";
    if (_wputenv_s(L"AWESOME_ENGINE_SAVE_FILE", saveFile.c_str()) != 0) throw std::runtime_error("No se pudo aislar el guardado de opciones");
    nlohmann::json checks = nlohmann::json::array();
    const auto require = [&](bool condition, const char *message) {
      if (!condition) throw std::runtime_error(message);
      checks.push_back({{"result", "PASS"}, {"check", message}});
    };
    const auto press = [&](const tgui::Panel::Ptr &panel, const char *text) {
      for (const auto &widget : panel->getWidgets()) {
        if (const auto button = std::dynamic_pointer_cast<tgui::Button>(widget); button && button->getText() == text) {
          button->onPress.emit(button.get(), button->getText()); return;
        }
      }
      throw std::runtime_error(std::string("No existe botón de opciones: ") + text);
    };
    {
      EngineContext context(window);
      context.save.Clear(); NoTrace::ApplyUserPreferences(context);
      auto panel = tgui::Panel::create({780.f, 596.f});
      int openedControls = 0, back = 0;
      const auto widgets = NoTrace::BuildOptionsUi(context, panel, [&] { ++openedControls; }, [&] { ++back; }, "VOLVER");
      widgets.mode->setSelectedItemByIndex(1);
      require(!widgets.resolution->isEnabled(), "Pantalla completa deshabilita una resolución que usa el escritorio");
      widgets.mode->setSelectedItemByIndex(2);
      require(widgets.resolution->isEnabled(), "Ventana sin bordes conserva el selector de resolución");
      widgets.verticalSync->setSelectedItemByIndex(1);
      require(!widgets.framerate->isEnabled(), "VSync sustituye el límite de FPS en la interfaz");
      widgets.verticalSync->setSelectedItemByIndex(0);
      require(widgets.framerate->isEnabled(), "Desactivar VSync recupera el límite de FPS");
      press(panel, "APLICAR PANTALLA");
      require(widgets.status->getText().toStdString().starts_with("Selecciona una resolución"), "Las opciones rechazan una resolución ausente sin modificar la ventana");
      press(panel, "Controles"); press(panel, "VOLVER");
      require(openedControls == 1 && back == 1, "Las opciones mantienen navegación funcional hacia controles y retorno");
      std::array<std::int16_t, 22050> silence{};
      sf::SoundBuffer buffer;
      require(buffer.loadFromSamples(silence.data(), silence.size(), 1, 22050, {sf::SoundChannel::Mono}), "El ensayo de audio crea un búfer silencioso real");
      auto voice = std::make_shared<sf::Sound>(buffer);
      context.audio.RegisterEffect(voice, 80.f); voice->setLooping(true); voice->play();
      auto ambientVoice = std::make_shared<sf::Sound>(buffer);
      context.audio.RegisterSource(ambientVoice, 60.f, AudioManager::Bus::Ambience); ambientVoice->setLooping(true); ambientVoice->play();
      require(voice->getStatus() == sf::SoundSource::Status::Playing, "El bus de efectos se prueba mientras la voz está reproduciéndose");
      widgets.effectsVolume->setValue(25.f);
      require(std::abs(voice->getVolume() - 20.f) < 0.01f && context.audio.GetEffectsVolume() == 25.f,
              "El volumen de efectos modifica inmediatamente una voz activa conservando su ganancia base");
      widgets.ambienceVolume->setValue(40.f);
      require(ambientVoice->getStatus() == sf::SoundSource::Status::Playing && std::abs(ambientVoice->getVolume() - 24.f) < .01f && std::abs(voice->getVolume() - 20.f) < .01f && context.save.Get(NoTrace::OptionsConfig::AmbienceVolumeSaveKey, -1.f) == 40.f,
              "El control de ambiente mezcla una voz continua real y guarda su volumen sin alterar los efectos");
      widgets.ambienceVolume->setFocused(true);
      sf::Event::KeyPressed ambientKey; ambientKey.scancode = sf::Keyboard::Scan::Right;
      NoTrace::HandleOptionsKey(ambientKey, widgets);
      require(widgets.ambienceVolume->getValue() == 41.f && std::abs(ambientVoice->getVolume() - 24.6f) < .01f, "El teclado ajusta inmediatamente el bus ambiental");
      widgets.ambienceVolume->setValue(40.f); widgets.ambienceVolume->setFocused(false);
      widgets.masterVolume->setValue(50.f);
      require(context.audio.GetGlobalVolume() == 50.f && std::abs(voice->getVolume() - 20.f) < 0.01f,
              "El volumen general y el bus de efectos conservan factores independientes");
      widgets.cursorSpeed->setValue(1250.f);
      require(context.cursor.GetSpeed() == 1250.f && context.save.Get(NoTrace::CURSOR_SPEED_SAVE_KEY, 0.f) == 1250.f,
              "El control del mando aplica y guarda su velocidad real");
      widgets.firstPersonFov->setValue(94.f); widgets.firstPersonFov->setFocused(true);
      sf::Event::KeyPressed fovKey; fovKey.scancode = sf::Keyboard::Scan::Right; NoTrace::HandleOptionsKey(fovKey, widgets);
      require(widgets.firstPersonFov->getValue() == 95.f && context.save.Get(NoTrace::FirstPersonConfig::FovSaveKey, 0.f) == 95.f,
              "El teclado ajusta y guarda el campo visual de primera persona");
      widgets.firstPersonFov->setFocused(false);
      voice->stop(); voice.reset(); context.audio.SetEffectsVolume(25.f); context.audio.RegisterEffect({}, 80.f);
      ambientVoice->stop(); ambientVoice.reset();
      require(context.audio.GetEffectsVolume() == 25.f, "El bus elimina referencias caducadas y admite sonidos ausentes");
      auto controls = tgui::Panel::create({780.f, 596.f});
      int applied = 0;
      NoTrace::BuildControlsUi(context, controls, [&] { ++applied; }, [] {});
      std::size_t categories = 0;
      std::string controlText;
      for (const auto &widget : controls->getWidgets()) if (const auto label = std::dynamic_pointer_cast<tgui::Label>(widget)) {
        const auto text = label->getText().toStdString(); controlText += text + "\n";
        categories += std::ranges::count(NoTrace::ControlsConfig::Categories, std::string_view(text));
      }
      require(categories == NoTrace::ControlsConfig::Categories.size() && controlText.find("nombres y orden privada") != std::string::npos,
              "Controles muestra las seis categorías reales y describe TAB mantenida para nombres y orden privada");
      std::vector<tgui::ComboBox::Ptr> selectors;
      for (const auto &widget : controls->getWidgets()) if (auto choice = std::dynamic_pointer_cast<tgui::ComboBox>(widget)) selectors.push_back(choice);
      require(selectors.size() == 2, "El panel presenta únicamente las dos acciones que admiten reasignación");
      selectors[0]->setSelectedItemByIndex(2); selectors[1]->setSelectedItemByIndex(2);
      press(controls, "GUARDAR CONTROLES");
      require(applied == 0 && context.save.Get("NoTrace:DiveKey", -1) == 0 && context.save.Get("NoTrace:RollKey", -1) == 1,
              "Una asignación duplicada no sustituye los controles guardados");
      selectors[1]->setSelectedItemByIndex(3); press(controls, "GUARDAR CONTROLES");
      require(applied == 1 && context.save.Get("NoTrace:DiveKey", -1) == 2 && context.save.Get("NoTrace:RollKey", -1) == 3,
              "Guardar controles conserva dos teclas distintas y notifica su aplicación");
      press(controls, "RESTAURAR CONTROLES");
      require(applied == 2 && context.save.Get("NoTrace:DiveKey", -1) == 0 && context.save.Get("NoTrace:RollKey", -1) == 1,
              "Restaurar controles recupera Espacio y Ctrl y notifica su aplicación");
      selectors[0]->setSelectedItemByIndex(2); selectors[1]->setSelectedItemByIndex(3); press(controls, "GUARDAR CONTROLES");
      auto remapped = tgui::Panel::create({780.f, 596.f});
      NoTrace::BuildControlsUi(context, remapped, [] {}, [] {});
      bool currentKeys = false;
      for (const auto &widget : remapped->getWidgets()) if (const auto label = std::dynamic_pointer_cast<tgui::Label>(widget)) {
        const auto text = label->getText().toStdString();
        currentKeys |= text.find("X: zambullida") != std::string::npos && text.find("J: rodada") != std::string::npos;
      }
      require(currentKeys, "Reabrir Controles informa las teclas de maniobra guardadas en lugar de las predeterminadas");
    }
    require(std::filesystem::is_regular_file(saveFile), "Cerrar el primer contexto escribe las preferencias en una ruta Unicode aislada");
    {
      EngineContext context(window); NoTrace::ApplyUserPreferences(context);
      require(context.audio.GetGlobalVolume() == 50.f && context.audio.GetEffectsVolume() == 25.f && context.audio.GetAmbienceVolume() == 40.f && context.cursor.GetSpeed() == 1250.f,
              "Un nuevo contexto restaura volumen general, efectos, ambiente y velocidad del mando desde disco");
      require(context.save.Get("NoTrace:DiveKey", -1) == 2 && context.save.Get("NoTrace:RollKey", -1) == 3,
              "Un nuevo contexto conserva las reasignaciones de ambas acciones");
      require(context.save.Get(NoTrace::FirstPersonConfig::FovSaveKey, 0.f) == 95.f,
              "Un nuevo contexto conserva el campo visual FPS desde el guardado aislado");
      context.save.Set(NoTrace::VOLUME_SAVE_KEY, "incorrecto");
      context.save.Set(NoTrace::OptionsConfig::EffectsVolumeSaveKey, std::numeric_limits<float>::quiet_NaN());
      context.save.Set(NoTrace::OptionsConfig::AmbienceVolumeSaveKey, "incorrecto");
      context.save.Set(NoTrace::CURSOR_SPEED_SAVE_KEY, -100.f);
      context.save.Set("NoTrace:DiveKey", 999); context.save.Set("NoTrace:RollKey", "incorrecto");
      context.save.Set(NoTrace::FirstPersonConfig::FovSaveKey, "incorrecto");
      NoTrace::ApplyUserPreferences(context);
      require(context.audio.GetGlobalVolume() == gConfig.globalVolume && context.audio.GetEffectsVolume() == NoTrace::OptionsConfig::EffectsVolume &&
              context.audio.GetAmbienceVolume() == NoTrace::OptionsConfig::AmbienceVolume && context.cursor.GetSpeed() == NoTrace::MIN_CURSOR_SPEED && context.save.Get("NoTrace:DiveKey", -1) == 0 && context.save.Get("NoTrace:RollKey", -1) == 1,
              "Tipos de guardado inválidos, NaN y rangos incorrectos se normalizan sin lanzar excepciones");
      require(context.save.Get(NoTrace::FirstPersonConfig::FovSaveKey, 0.f) == NoTrace::FirstPersonConfig::DefaultFov,
              "El campo visual inválido se normaliza sin impedir entrar en primera persona");
      context.audio.SetGlobalVolume(200.f); require(context.audio.GetGlobalVolume() == 100.f, "El volumen general limita valores superiores al máximo");
      context.audio.SetEffectsVolume(-1.f); require(context.audio.GetEffectsVolume() == 0.f, "El bus de efectos limita valores negativos");
      context.audio.SetEffectsVolume(std::numeric_limits<float>::infinity());
      require(context.audio.GetEffectsVolume() == 100.f, "El bus de efectos rechaza valores no finitos");
    }
    const auto encodedSave = saveFile.generic_u8string();
    std::ofstream report(output / "options-preferences.json");
    report << nlohmann::json{{"result", "PASS"}, {"save", std::string(encodedSave.begin(), encodedSave.end())}, {"checks", checks}}.dump(2);
    if (!report) throw std::runtime_error("No se pudo escribir la evidencia de opciones");
  }
}
