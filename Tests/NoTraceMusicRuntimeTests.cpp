#include "NoTrace.h"
#include "NoTraceGame.h"
#include "NoTraceSpatialFixture.h"
#include "Config/NoTraceConfig.h"
#include "Config/NoTraceMusicConfig.h"
#include "Config/NoTraceOptionsConfig.h"
#include "Config/NoTraceSecurityConfig.h"
#include "Core/EngineConfig.h"
#include <SFML/Audio/Music.hpp>
#include <SFML/Audio/Sound.hpp>
#include <SFML/System/Clock.hpp>
#include <SFML/System/Sleep.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace EngineTests {
  struct GuiAccess {
    static void Initialize(EngineContext &context) { context.input.hardwarePolling_ = false; }
    static void Delta(EngineContext &context, float seconds) { context.time.deltaTime_ = sf::seconds(seconds); }
    static NoTrace::Session &Session(NoTrace::Game &game) { return game.session_; }
    static NoTrace::OptionsWidgets &Options(NoTrace::Game &game) { return game.options_; }
    static bool Cue(NoTrace::Game &game, NoTrace::SoundKind kind) { return game.audioPlayback_.Play(kind); }
    static void Pause(NoTrace::Game &game) { game.Open(NoTrace::Game::Page::Pause); }
    static void Resume(NoTrace::Game &game) { game.ClosePanel(); }
    static std::vector<tgui::Widget::Ptr> Widgets(EngineContext &context) { return context.gui.gui_.getWidgets(); }
    static void Sync(EngineContext &context) {
      context.window.RefreshPresentation();
      context.gui.RebindWindow();
      context.gui.SyncPresentation(context.window.GetLogicalSize(), context.window.GetViewport());
    }
    static void Render(const Scene &scene, EngineContext &context, sf::RenderWindow &window) {
      window.setView(context.window.GetPresentationView()); window.clear();
      context.renderer.BeginDrawing(); scene.Render();
      window.draw(sf::Sprite(context.renderer.FinishDrawing())); context.gui.Render(); window.display();
    }
  };
}

namespace {
  using namespace NoTrace;
  using Access = EngineTests::GuiAccess;
  using Json = nlohmann::json;
  int checks = 0;

  void Require(bool condition, const std::string &message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
  }

  struct IsolatedSave {
    std::wstring previous;
    explicit IsolatedSave(const std::filesystem::path &file) {
      if (const auto *value = _wgetenv(L"AWESOME_ENGINE_SAVE_FILE")) previous = value;
      Require(_wputenv_s(L"AWESOME_ENGINE_SAVE_FILE", file.c_str()) == 0, "No se pudo aislar el guardado musical");
    }
    ~IsolatedSave() { _wputenv_s(L"AWESOME_ENGINE_SAVE_FILE", previous.c_str()); }
  };

  tgui::Button::Ptr FindButton(const std::vector<tgui::Widget::Ptr> &widgets, const std::string &text) {
    for (const auto &widget : widgets) {
      if (!widget->isVisible()) continue;
      if (const auto button = std::dynamic_pointer_cast<tgui::Button>(widget); button && button->getText().toStdString().starts_with(text)) return button;
      if (const auto container = std::dynamic_pointer_cast<tgui::Container>(widget)) {
        if (const auto found = FindButton(container->getWidgets(), text)) return found;
      }
    }
    return {};
  }

  void Press(EngineContext &context, const std::string &text) {
    const auto button = FindButton(Access::Widgets(context), text);
    Require(button && button->isEnabled(), "No se encuentra un botón activo: " + text);
    button->onPress.emit(button.get(), button->getText());
  }

  Json Snapshot(const std::string &stage, const AudioManager::MusicState &state) {
    return {{"stage", stage}, {"file", state.file}, {"requestedFile", state.requestedFile}, {"layerFile", state.layerFile},
      {"playing", state.playing}, {"layerPlaying", state.layerPlaying}, {"looping", state.looping}, {"transitioning", state.transitioning},
      {"gain", state.gain}, {"layerGain", state.layerGain}, {"intensity", state.intensity}, {"targetIntensity", state.targetIntensity},
      {"volume", state.volume}, {"layerVolume", state.layerVolume}, {"offsetSeconds", state.offsetSeconds},
      {"layerOffsetSeconds", state.layerOffsetSeconds}, {"durationSeconds", state.durationSeconds},
      {"mainStreamsPlaying", state.mainStreamsPlaying}, {"cachedStreams", state.cachedStreams}, {"starts", state.starts}};
  }

  struct Runtime {
    sf::RenderWindow &window;
    EngineContext &context;
    bool audition;
    Json states = Json::array();
    std::vector<double> updateMicroseconds;
    std::size_t maximumMainStreams = 0;

    void Audio(float delta) {
      const auto start = std::chrono::steady_clock::now(); context.audio.Update(delta);
      updateMicroseconds.push_back(std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count());
      const auto count = context.audio.GetMusicState().mainStreamsPlaying;
      maximumMainStreams = std::max(maximumMainStreams, count);
      Require(count <= 1, "Dos composiciones principales se reproducen simultáneamente");
    }
    void Frame(Scene &scene, float delta, bool render = false) {
      while (const auto event = window.pollEvent()) {
        Require(!event->is<sf::Event::Closed>(), "Se cerró la ventana durante la prueba musical");
      }
      Access::Delta(context, delta); scene.Update(); scene.UpdateBackground(); Audio(delta);
      if (render || audition) Access::Render(scene, context, window);
    }
    void Advance(Scene &scene, float seconds) {
      for (int frame = 0; frame < static_cast<int>(std::ceil(seconds / .02f)); ++frame) Frame(scene, .02f);
    }
    void Listen(Scene &scene, float seconds) {
      sf::Clock elapsed; sf::Clock delta;
      while (elapsed.getElapsedTime().asSeconds() < seconds) {
        Frame(scene, std::min(delta.restart().asSeconds(), .1f), true); sf::sleep(sf::milliseconds(16));
      }
    }
    void Record(const std::string &stage) {
      const auto state = context.audio.GetMusicState(); states.push_back(Snapshot(stage, state));
      std::cout << stage << " file=" << state.file << " offset=" << state.offsetSeconds << " gain=" << state.gain
        << " layer=" << state.intensity << " streams=" << state.mainStreamsPlaying << " starts=" << state.starts << '\n';
    }
    void Settled(const AudioManager::MusicSettings &settings) {
      const auto state = context.audio.GetMusicState();
      Require(state.file == settings.file && state.requestedFile == settings.file && state.playing && state.looping && !state.transitioning,
        "La escena no conserva su composición real en bucle: " + settings.file);
      Require(state.mainStreamsPlaying == 1 && std::abs(state.gain - 1.f) < .001f, "La transición no termina con una única pista al nivel configurado");
    }
  };

  void VerifyLibrary(ResourceManager &resources, Json &report) {
    for (const auto &settings : {MusicConfig::Menu, MusicConfig::Lobby, MusicConfig::Mission}) {
      auto music = resources.FetchMusic(settings.file);
      Require(music.has_value(), "No se puede decodificar una composición nueva: " + settings.file);
      Require(music->getDuration().asSeconds() >= 120.f && music->getDuration().asSeconds() <= 240.f, "Una composición no alcanza la duración musical requerida");
      Require(music->getSampleRate() == 48000 && music->getChannelCount() == 2, "La música no utiliza el formato de producción 48 kHz estéreo");
      report["library"].push_back({{"file", settings.file}, {"durationSeconds", music->getDuration().asSeconds()},
        {"sampleRate", music->getSampleRate()}, {"channels", music->getChannelCount()}});
    }
    auto layer = resources.FetchMusic(MusicConfig::Mission.layerFile);
    auto base = resources.FetchMusic(MusicConfig::Mission.file);
    Require(layer && base && layer->getDuration() == base->getDuration() && layer->getChannelCount() == base->getChannelCount() &&
      layer->getSampleRate() == base->getSampleRate(), "La capa adaptativa no comparte longitud y formato con la misión");
    Require(MusicConfig::Menu.file != MusicConfig::Lobby.file && MusicConfig::Menu.file != MusicConfig::Mission.file &&
      MusicConfig::Lobby.file != MusicConfig::Mission.file, "Las tres escenas comparten composición");
  }

  void VerifyFailures(Runtime &runtime) {
    const auto before = runtime.context.audio.GetMusicState();
    auto missing = MusicConfig::Mission; missing.file = "NoTrace/NoExiste.ogg";
    Require(!runtime.context.audio.PlayMusic(runtime.context.resources, missing), "Un recurso ausente se anuncia como reproducción correcta");
    auto after = runtime.context.audio.GetMusicState();
    Require(after.file == before.file && after.requestedFile == before.requestedFile && after.starts == before.starts && after.playing,
      "Un fallo de carga interrumpe o reemplaza la composición vigente");
    missing = MusicConfig::Mission; missing.layerFile = "NoTrace/CapaNoExiste.ogg";
    Require(!runtime.context.audio.PlayMusic(runtime.context.resources, missing), "Una capa ausente deja una solicitud musical incompleta");
    runtime.context.audio.Update(std::numeric_limits<float>::quiet_NaN()); runtime.context.audio.Update(-1.f);
    after = runtime.context.audio.GetMusicState();
    Require(std::isfinite(after.gain) && std::isfinite(after.volume) && after.playing && after.starts == before.starts,
      "Un delta inválido corrompe la envolvente o reinicia la música");
    runtime.Record("errores-sin-interrupcion");
  }

  void VerifyAlertFlow(Game &game, Runtime &runtime, Json &report) {
    auto &session = Access::Session(game);
    auto &world = const_cast<WorldState &>(session.GetWorld());
    TestFixture::LegacyPlane(world);
    world.obstacles.clear(); world.objects.clear(); world.guards.clear(); world.securityEntries.clear();
    world.noises.clear(); world.sounds.clear(); world.events.clear();
    world.agents[0].position = {300.f, 300.f}; world.agents[0].facing = {1.f, 0.f};
    Interactable closet; closet.id = 77; closet.kind = ObjectKind::Hideout; closet.hideoutKind = HideoutKind::Closet;
    closet.position = {270.f, 300.f}; closet.facing = {1.f, 0.f}; world.objects.push_back(closet);
    Guard guard; guard.id = 71; guard.position = {380.f, 300.f}; guard.facing = {-1.f, 0.f}; guard.attackCooldown = 1000.f;
    guard.patrol = {guard.position}; world.guards.push_back(guard);
    const auto starts = runtime.context.audio.GetMusicState().starts;
    bool red = false, orange = false, yellow = false, calm = false, hidden = false;
    for (unsigned int step = 0; step < static_cast<unsigned int>((SecurityConfig::SearchDuration + SecurityConfig::VigilanceDuration + 10.f) / .1f) && !calm; ++step) {
      ActionCommand command; command.interact = red && !hidden; session.Submit(command);
      Access::Delta(runtime.context, .1f); game.UpdateBackground(); runtime.Audio(.1f);
      if (command.interact) { hidden = world.agents[0].hidden; Require(hidden, "La pérdida de alerta no procede de un escondite real"); }
      const bool changed = (!red && world.securityAlert == SecurityAlert::Red) || (red && !orange && world.securityAlert == SecurityAlert::Orange) ||
        (orange && !yellow && world.securityAlert == SecurityAlert::Yellow) || (yellow && world.securityAlert == SecurityAlert::Calm);
      if (!changed) continue;
      const auto state = runtime.context.audio.GetMusicState();
      Require(std::abs(state.targetIntensity - MusicConfig::Intensity(world.securityAlert)) < .001f, "Game no aplica la intensidad de su alerta autoritativa");
      Require(std::abs(state.intensity - state.targetIntensity) > .001f, "Cambiar de alerta salta directamente al nivel final sin transición gradual");
      Require(state.starts == starts && state.file == MusicConfig::Mission.file && state.layerPlaying, "Cambiar de alerta reinicia la composición o pierde la capa sincronizada");
      Require(std::abs(state.offsetSeconds - state.layerOffsetSeconds) <= .15f, "La capa de tensión se desincroniza de la composición principal");
      report["alertFlow"].push_back({{"alert", static_cast<int>(world.securityAlert)}, {"tick", world.tick}, {"music", Snapshot("alerta", state)}});
      runtime.Record("alerta-" + std::to_string(static_cast<int>(world.securityAlert)));
      if (runtime.audition) {
        for (unsigned int frame = 0; frame < 180; ++frame) { runtime.Audio(1.f / 60.f); Access::Render(game, runtime.context, runtime.window); sf::sleep(sf::milliseconds(16)); }
      }
      red |= world.securityAlert == SecurityAlert::Red; orange |= world.securityAlert == SecurityAlert::Orange;
      yellow |= world.securityAlert == SecurityAlert::Yellow; calm |= yellow && world.securityAlert == SecurityAlert::Calm;
    }
    Require(red && orange && yellow && calm && hidden, "La misión no completa detección roja, escondite, búsqueda naranja, vigilancia amarilla y calma");
    const auto noises = world.noises.size(), sounds = world.sounds.size();
    for (unsigned int frame = 0; frame < 240; ++frame) runtime.Audio(.02f);
    const auto recovered = runtime.context.audio.GetMusicState();
    Require(recovered.intensity < .001f && recovered.layerVolume < .001f && recovered.starts == starts && recovered.playing,
      "El regreso a calma no desvanece la capa conservando la reproducción base");
    Require(world.noises.size() == noises && world.sounds.size() == sounds, "Mezclar música genera señales acústicas en la simulación");
    runtime.Record("calma-recuperada");
  }

  void VerifyClient(Game &game, Runtime &runtime) {
    auto &client = Access::Session(game); client.Stop();
    NetworkManager hostNetwork; RandomManager hostRandom; Session host(hostNetwork, hostRandom);
    Require(host.Host("Anfitrión de prueba musical", 0), "No se puede iniciar el anfitrión TCP local del cliente musical");
    Require(client.Join("127.0.0.1", hostNetwork.GetPort(), "Cliente musical"), "No se puede conectar el cliente musical por TCP local");
    const auto until = [&](const std::function<bool()> &condition) {
      sf::Clock timeout;
      while (!condition()) {
        Require(timeout.getElapsedTime().asSeconds() < 4.f, "Tiempo agotado al sincronizar el cliente musical local");
        host.Tick(.01f); runtime.Frame(game, .01f); sf::sleep(sf::milliseconds(1));
      }
    };
    until([&] { return client.IsConnected() && host.GetLobbyPlayers().size() == 2; });
    runtime.Advance(game, 4.f); runtime.Settled(MusicConfig::Lobby); runtime.Record("lobby-cliente-local");
    host.SetReady(true); client.SetReady(true); until([&] { return host.CanDeploy(); }); host.Deploy();
    until([&] { return client.GetWorld().phase == GamePhase::Mission; });
    runtime.Advance(game, 4.f); runtime.Settled(MusicConfig::Mission);
    Require(client.IsClient(), "La escena comprobada no conserva el rol de cliente"); runtime.Record("mision-cliente-local");
    host.ReturnToBase(); until([&] { return client.GetWorld().phase == GamePhase::Base; });
    runtime.Advance(game, 4.f); runtime.Settled(MusicConfig::Lobby); runtime.Record("retorno-base-cliente-local");
    client.Stop(); host.Stop();
  }

  Json Run(sf::RenderWindow &window, bool audition, bool soak) {
    Json report{{"status", "RUNNING"}, {"audition", audition}, {"soak", soak}, {"library", Json::array()}, {"alertFlow", Json::array()}};
    {
      EngineContext context(window); Access::Initialize(context); context.save.Clear();
      context.save.Set(VOLUME_SAVE_KEY, 45.f); context.save.Set(OptionsConfig::MusicVolumeSaveKey, 70.f);
      context.save.Set(DisplayConfig::WidthSaveKey, 1280u); context.save.Set(DisplayConfig::HeightSaveKey, 720u);
      context.save.Set(DisplayConfig::FullscreenSaveKey, false); context.save.Set(DisplayConfig::BorderlessSaveKey, false);
      context.save.Set(DisplayConfig::VerticalSyncSaveKey, false); context.save.Set(DisplayConfig::FramerateLimitSaveKey, 0u);
      context.window.Initialize(); window.setVisible(audition); Access::Sync(context);
      VerifyLibrary(context.resources, report);
      Runtime runtime{window, context, audition};
      Menu menu(context); menu.Start(); runtime.Advance(menu, 3.f); runtime.Listen(menu, audition ? 8.f : .15f); runtime.Settled(MusicConfig::Menu); runtime.Record("menu");
      const auto first = context.audio.GetMusicState();
      Require(first.offsetSeconds > .05f, "El stream del menú no avanza realmente en SFML");
      runtime.Advance(menu, 1.f);
      Require(context.audio.GetMusicState().starts == first.starts, "Actualizar el menú reinicia su composición");
      Press(context, "OPCIONES"); runtime.Advance(menu, .2f); Press(context, "VOLVER");
      Require(context.audio.GetMusicState().starts == first.starts, "Abrir y cerrar opciones reinicia la música del menú");
      if (soak) {
        sf::Clock elapsed; float previous = context.audio.GetMusicState().offsetSeconds; bool wrapped = false;
        while (elapsed.getElapsedTime().asSeconds() < first.durationSeconds + 2.f) {
          runtime.Listen(menu, .1f); const auto state = context.audio.GetMusicState();
          wrapped |= state.offsetSeconds + 1.f < previous; previous = state.offsetSeconds;
          Require(state.starts == first.starts && state.playing && state.looping, "El bucle prolongado reinicia o detiene el menú");
        }
        Require(wrapped, "No se observó el cruce natural del final al inicio del stream");
        report["soakElapsedSeconds"] = elapsed.getElapsedTime().asSeconds(); report["naturalLoopObserved"] = true; runtime.Record("menu-bucle-natural");
      }
      VerifyFailures(runtime);
      menu.OnCleanup(); Game game(context); game.Start(); runtime.Advance(game, 4.f); runtime.Settled(MusicConfig::Lobby); runtime.Record("lobby");
      Press(context, "OPERACIÓN INDIVIDUAL"); runtime.Advance(game, .1f);
      auto &session = Access::Session(game); Require(session.IsConnected(), "La escena no inicia la sesión individual");
      session.SetReady(true); session.Deploy(); runtime.Advance(game, 4.f); runtime.Settled(MusicConfig::Mission);
      runtime.Listen(game, audition ? 8.f : .15f); runtime.Record("mision-individual");
      Require(context.audio.GetMusicState().layerPlaying, "La misión no reproduce su capa continua sincronizada");
      for (const auto kind : {SoundKind::StepHard, SoundKind::Pistol, SoundKind::Alarm, SoundKind::Button}) {
        Require(Access::Cue(game, kind), "No se puede mezclar un efecto relevante con la composición de misión");
        if (audition) runtime.Listen(game, 1.f);
      }
      Require(context.audio.GetMusicState().playing && context.audio.ActiveVoices() > 0, "Reproducir efectos interrumpe el stream musical");
      VerifyAlertFlow(game, runtime, report);
      const auto beforePause = context.audio.GetMusicState(); Access::Pause(game); runtime.Listen(game, .15f);
      Require(context.audio.GetMusicState().starts == beforePause.starts && context.audio.GetMusicState().offsetSeconds > beforePause.offsetSeconds,
        "La pausa reinicia o detiene la música continua de misión");
      Press(context, "OPCIONES"); auto &options = Access::Options(game);
      options.musicVolume->setValue(25.f); options.effectsVolume->setValue(30.f); options.masterVolume->setValue(35.f);
      const auto quieter = context.audio.GetMusicState();
      Require(std::abs(quieter.volume - MusicConfig::Mission.volume * .25f) < .01f && context.audio.GetGlobalVolume() == 35.f &&
        context.save.Get(OptionsConfig::MusicVolumeSaveKey, -1.f) == 25.f, "El control real de música no aplica inmediatamente su ganancia o no guarda la preferencia");
      options.musicVolume->setValue(0.f); Require(context.audio.GetMusicState().volume == 0.f && context.audio.GetMusicState().playing,
        "Silenciar música detiene el stream y pierde su continuidad");
      options.musicVolume->setValue(25.f); runtime.Record("opciones-volumen"); Access::Resume(game);
      const auto beforeDisplay = context.audio.GetMusicState(); const auto initialDisplay = context.window.GetDisplaySettings();
      auto borderless = initialDisplay; borderless.borderless = true;
      Require(context.window.RequestDisplaySettings(borderless), "No se puede solicitar ventana sin bordes para la regresión musical");
      context.window.Update(); context.window.ResolveDisplayChange(true); context.window.Update(); window.setVisible(audition); Access::Sync(context);
      runtime.Listen(game, .15f);
      Require(context.audio.GetMusicState().starts == beforeDisplay.starts && context.audio.GetMusicState().offsetSeconds > beforeDisplay.offsetSeconds,
        "Recrear la ventana corta o reinicia el stream musical");
      Require(context.window.RequestDisplaySettings(initialDisplay), "No se puede restaurar el modo de ventana inicial");
      context.window.Update(); context.window.ResolveDisplayChange(true); context.window.Update(); window.setVisible(audition); Access::Sync(context);
      runtime.Record("recreacion-ventana");
      const auto startsBeforeRestart = context.audio.GetMusicState().starts;
      Require(session.RestartOperation(), "No se puede reiniciar la operación individual"); runtime.Advance(game, .2f);
      Require(context.audio.GetMusicState().starts == startsBeforeRestart, "Reiniciar una misión recarga la misma composición");
      session.ReturnToBase(); runtime.Advance(game, 4.f); runtime.Settled(MusicConfig::Lobby); runtime.Record("retorno-base");
      session.Stop(); Require(session.Host("Música anfitrión", 0), "No se puede iniciar sesión LAN local en puerto efímero");
      runtime.Advance(game, .2f); session.SetReady(true); session.Deploy(); runtime.Advance(game, 4.f);
      Require(session.IsHost() && session.GetWorld().phase == GamePhase::Mission, "La sesión anfitriona no despliega misión");
      runtime.Settled(MusicConfig::Mission); runtime.Record("mision-anfitrion-local");
      session.ReturnToBase(); runtime.Advance(game, 4.f); runtime.Settled(MusicConfig::Lobby);
      VerifyClient(game, runtime);
      game.OnCleanup(); menu.Start(); runtime.Advance(menu, 4.f); runtime.Settled(MusicConfig::Menu); runtime.Listen(menu, audition ? 5.f : .1f);
      Require(std::abs(context.audio.GetMusicState().volume - MusicConfig::Menu.volume * .25f) < .01f, "La preferencia musical no se conserva al volver al menú");
      runtime.Record("retorno-menu");
      context.audio.StopMusic(.5f); runtime.Audio(.2f); const auto fading = context.audio.GetMusicState();
      Require(fading.playing && fading.gain > 0.f && fading.gain < 1.f, "Detener música no aplica un desvanecimiento gradual");
      runtime.Audio(1.f); Require(!context.audio.GetMusicState().playing && !context.audio.GetMusicState().layerPlaying, "La limpieza conserva streams activos");
      std::ranges::sort(runtime.updateMicroseconds);
      report["audioUpdateP95Microseconds"] = runtime.updateMicroseconds[static_cast<std::size_t>((runtime.updateMicroseconds.size() - 1) * .95)];
      report["audioUpdateMaximumMicroseconds"] = runtime.updateMicroseconds.back(); report["audioUpdateSamples"] = runtime.updateMicroseconds.size();
      report["maximumMainStreamsPlaying"] = runtime.maximumMainStreams; report["states"] = std::move(runtime.states);
      menu.OnCleanup(); context.audio.StopMusic();
    }
    {
      EngineContext reopened(window); ApplyUserPreferences(reopened);
      Require(reopened.audio.GetMusicVolume() == 25.f && reopened.audio.GetGlobalVolume() == 35.f && reopened.audio.GetEffectsVolume() == 30.f,
        "Reabrir EngineContext no recupera las preferencias musicales persistidas");
      report["preferencesRecoveredAfterRestart"] = true;
    }
    report["checks"] = checks; report["status"] = "PASS"; return report;
  }
}

int main(int argc, char **argv) {
  const auto output = std::filesystem::absolute("build/music-runtime-sandbox"); std::filesystem::create_directories(output);
  bool audition = false, soak = false;
  for (int argument = 1; argument < argc; ++argument) {
    const std::string option = argv[argument];
    if (option == "--audition") audition = true;
    else if (option == "--soak") soak = true;
    else { std::cerr << "Uso: NoTraceMusicRuntimeTests [--audition] [--soak]\n"; return 2; }
  }
  try {
    IsolatedSave save(output / "Save.json");
    sf::RenderWindow window(sf::VideoMode({1280, 720}), "NO TRACE - PRUEBA MUSICAL", sf::Style::Default); window.setVisible(audition);
    const auto report = Run(window, audition, soak); window.close();
    const auto file = output / (soak ? "music-soak.json" : audition ? "music-audition.json" : "music-runtime.json");
    std::ofstream(file) << report.dump(2);
    std::cout << "PASS " << checks << " comprobaciones musicales; evidencia=" << file.string() << '\n'; return 0;
  } catch (const std::exception &error) {
    std::ofstream(output / "music-failure.json") << Json{{"status", "FAIL"}, {"checks", checks}, {"error", error.what()}}.dump(2);
    std::cerr << "FAIL música: " << error.what() << '\n'; return 1;
  }
}
