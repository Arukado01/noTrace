#include "Core/EngineConfig.h"
#include "Managers/ResourceManager.h"
#include "Managers/SaveManager.h"
#include "Managers/WindowManager.h"
#include <SFML/Audio/Sound.hpp>
#include <SFML/Graphics/Shader.hpp>
#include <SFML/Graphics/Sprite.hpp>
#include <SFML/System/Sleep.hpp>
#include <SFML/Window/Context.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string_view>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace {
  int checks = 0;
  nlohmann::json evidence = nlohmann::json::object();

  void Require(bool condition, const char *message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
  }

  struct SaveIsolation {
    std::wstring previous;
    explicit SaveIsolation(const std::filesystem::path &path) {
      wchar_t *value = nullptr;
      std::size_t length = 0;
      if (_wdupenv_s(&value, &length, L"AWESOME_ENGINE_SAVE_FILE") == 0) {
        const std::unique_ptr<wchar_t, decltype(&std::free)> captured(value, &std::free);
        if (captured) previous = captured.get();
      }
      Require(_wputenv_s(L"AWESOME_ENGINE_SAVE_FILE", path.c_str()) == 0, "No se pudo aislar el guardado de la prueba");
    }
    ~SaveIsolation() { _wputenv_s(L"AWESOME_ENGINE_SAVE_FILE", previous.c_str()); }
  };

  void Pump(sf::RenderWindow &native, WindowManager &manager) {
    while (const auto event = native.pollEvent()) {
      if (const auto *resized = event->getIf<sf::Event::Resized>()) manager.HandleResize(resized->size);
    }
    manager.Update();
  }

  void Confirm(WindowManager &manager) {
    manager.ResolveDisplayChange(true);
    manager.Update();
    Require(!manager.IsDisplayConfirmationPending(), "La confirmación no terminó el cambio de pantalla");
  }

  void Apply(sf::RenderWindow &native, WindowManager &manager, DisplaySettings settings) {
    Require(manager.RequestDisplaySettings(settings), "Se rechazó una configuración válida de pantalla");
    manager.Update();
    if (manager.IsDisplayConfirmationPending()) Confirm(manager);
    Pump(native, manager);
  }

  void VerifyCentered(sf::RenderWindow &native) {
#ifdef _WIN32
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    Require(GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &monitor) != FALSE,
            "No se pudo consultar el monitor principal");
    RECT frame{};
    Require(GetWindowRect(native.getNativeHandle(), &frame) != FALSE, "No se pudo consultar el marco de la ventana");
    Require(frame.left >= monitor.rcWork.left && frame.top >= monitor.rcWork.top &&
            frame.right <= monitor.rcWork.right && frame.bottom <= monitor.rcWork.bottom,
            "La ventana o su barra de título quedan fuera del área de trabajo");
    Require(std::abs((frame.left + frame.right) - (monitor.rcWork.left + monitor.rcWork.right)) <= 2 &&
            std::abs((frame.top + frame.bottom) - (monitor.rcWork.top + monitor.rcWork.bottom)) <= 2,
            "La ventana no está centrada después de establecer su tamaño real");
    evidence["work_area"] = {monitor.rcWork.left, monitor.rcWork.top, monitor.rcWork.right, monitor.rcWork.bottom};
    evidence["last_window_frame"] = {frame.left, frame.top, frame.right, frame.bottom};
#endif
  }

  void VerifyCoordinates(sf::RenderWindow &native, WindowManager &manager) {
    const auto &view = manager.GetPresentationView();
    for (const auto logical : {sf::Vector2f{200.f, 150.f}, sf::Vector2f{800.f, 450.f}, sf::Vector2f{1370.f, 780.f}}) {
      const auto pixel = native.mapCoordsToPixel(logical, view);
      const auto restored = native.mapPixelToCoords(pixel, view);
      const auto viewport = native.getViewport(view);
      const auto unitsPerPixel = manager.GetLogicalSize().componentWiseDiv(sf::Vector2f(viewport.size));
      const auto error = (restored - logical).componentWiseDiv(unitsPerPixel);
      evidence["coordinate_checks"].push_back({{"resolution", {native.getSize().x, native.getSize().y}},
        {"logical", {logical.x, logical.y}}, {"pixel", {pixel.x, pixel.y}},
        {"restored", {restored.x, restored.y}}, {"error_pixels", {error.x, error.y}}});
      // SFML trunca cada eje a píxeles enteros; la diagonal puede perder más de una unidad lógica.
      Require(error.x >= -1.01f && error.x <= .01f && error.y >= -1.01f && error.y <= .01f,
              "El cambio de pantalla desalineó las coordenadas lógicas y los píxeles del cursor");
    }
  }

  nlohmann::json TransitionProbe(sf::RenderWindow &native, WindowManager &manager,
                                  ResourceManager &resources, int iterations) {
    const auto *texture = resources.FetchTexture("NoTrace/Characters/OperativeAtlas.png");
    const auto *font = resources.FetchFont("Relidux.otf");
    const std::string soundFile = "NoTrace/Weapons/SuppressorAttach-01.wav";
    const auto *sound = resources.FetchSound(soundFile);
    Require(texture && font && sound, "No se pudieron cargar los recursos compartidos de comprobación");
    Require(native.setActive(), "No se pudo activar el contexto de comprobación");
    const auto handle = texture->getNativeHandle();
    const auto window = native.getNativeHandle();
    const auto context = sf::Context::getActiveContextId();
    sf::Sound playing(*sound);
    playing.setLooping(true);
    playing.setVolume(0.f);
    playing.play();
    sf::Shader shader;
    const bool shaderAvailable = sf::Shader::isAvailable();
    if (shaderAvailable) {
      Require(shader.loadFromMemory("void main() { gl_FragColor = gl_Color; }", sf::Shader::Type::Fragment),
              "No se pudo preparar el shader compartido de comprobación");
    }
    const auto shaderHandle = shader.getNativeHandle();
    nlohmann::json metrics = nlohmann::json::array();
    for (int i = 0; i < iterations; ++i) {
      const auto windowed = manager.GetWindowedResolution();
      const auto start = std::chrono::steady_clock::now();
      manager.ToggleFullscreen(); manager.Update();
      native.clear(); native.draw(sf::Sprite(*texture)); native.display();
      const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
      Require(native.getNativeHandle() == window, "La transición recreó la ventana nativa");
      Require(native.setActive() && sf::Context::getActiveContextId() == context, "La transición sustituyó el contexto gráfico");
      Require(resources.FetchTexture("NoTrace/Characters/OperativeAtlas.png") == texture && texture->getNativeHandle() == handle,
              "La transición sustituyó la textura compartida");
      Require(resources.FetchFont("Relidux.otf") == font && resources.FetchSound(soundFile) == sound,
              "La transición volvió a cargar la fuente o el audio");
      Require(shader.getNativeHandle() == shaderHandle, "La transición sustituyó el shader cargado");
      Require(playing.getStatus() == sf::SoundSource::Status::Playing, "La transición interrumpió el sonido activo");
      const bool recreated = manager.ConsumeWindowRecreated();
      Require(!recreated, "La transición solicitó reinicializar los recursos de TGUI");
      Require(elapsed < 250.0, "La transición y su primer fotograma exceden 250 milisegundos");
      metrics.push_back({{"iteration", i}, {"fullscreen", manager.GetDisplaySettings().fullscreen},
                         {"milliseconds", elapsed}, {"apply_milliseconds", manager.GetLastTransitionMilliseconds()},
                         {"recreated", recreated}, {"context", context}, {"texture", handle}});
      if (manager.GetDisplaySettings().fullscreen) {
        Require(native.getSize() == sf::VideoMode::getDesktopMode().size,
                "La pantalla completa no utiliza la resolución real del escritorio");
      } else {
        Require(native.getSize() == windowed, "Volver a ventana perdió la resolución elegida");
        VerifyCentered(native);
      }
      Confirm(manager);
      Pump(native, manager);
      VerifyCoordinates(native, manager);
    }
    playing.stop();
    return metrics;
  }

  double FrameTime(sf::RenderWindow &native, int frames) {
    native.clear(); native.display();
    const auto started = std::chrono::steady_clock::now();
    for (int i = 0; i < frames; ++i) { native.clear(); native.display(); }
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
  }

  void VerifyPacing(sf::RenderWindow &native, WindowManager &manager) {
    auto settings = manager.GetDisplaySettings();
    settings.verticalSync = false; settings.framerateLimit = 30;
    const auto position = native.getPosition();
    Apply(native, manager, settings);
    Require(native.getPosition() == position, "Cambiar el límite de FPS movió una ventana que no cambió de tamaño");
    const double limited = FrameTime(native, 6);
    Require(limited >= 170.0 && limited < 1000.0, "El límite de 30 FPS no regula los fotogramas reales");
    settings.framerateLimit = 0;
    Apply(native, manager, settings);
    const double unlimited = FrameTime(native, 6);
    Require(unlimited < limited * .85, "Desactivar el límite de FPS no modificó el ritmo real de presentación");
    evidence["six_frames_30fps_milliseconds"] = limited;
    evidence["six_frames_unlimited_milliseconds"] = unlimited;
    settings.verticalSync = true; settings.framerateLimit = 30;
    Apply(native, manager, settings);
    Require(manager.GetDisplaySettings().verticalSync && manager.GetDisplaySettings().framerateLimit == 30,
            "La sincronización vertical perdió la preferencia de FPS");
#ifdef _WIN32
    using SwapInterval = int (WINAPI *)();
    const auto interval = reinterpret_cast<SwapInterval>(sf::Context::getFunction("wglGetSwapIntervalEXT"));
    evidence["swap_interval_query"] = interval != nullptr;
    if (interval) Require(interval() == 1, "La opción VSync no se aplicó al contexto gráfico real");
#endif
    const double synchronized = FrameTime(native, 6);
    evidence["six_frames_vsync_milliseconds"] = synchronized;
    settings.verticalSync = false; settings.framerateLimit = 60;
    Apply(native, manager, settings);
#ifdef _WIN32
    if (interval) Require(interval() == 0, "Desactivar VSync no modificó el contexto gráfico real");
#endif
  }

  void VerifyCapture(sf::RenderWindow &native, WindowManager &manager, ResourceManager &resources,
                     const std::filesystem::path &sandbox) {
    const auto *texture = resources.FetchTexture("NoTrace/Characters/OperativeAtlas.png");
    native.setView(manager.GetPresentationView());
    const sf::Color background{87, 17, 123};
    native.clear(background);
    native.draw(sf::Sprite(*texture));
    sf::Texture capture(native.getSize());
    capture.update(native);
    const auto image = capture.copyToImage();
    std::size_t changed = 0;
    for (unsigned int y = 0; y < image.getSize().y; y += 3) {
      for (unsigned int x = 0; x < image.getSize().x; x += 3) changed += image.getPixel({x, y}) != background;
    }
    Require(changed > 100, "Después de las transiciones el atlas ya no se dibuja en la ventana");
    Require(image.saveToFile(sandbox / "display-resources-after.png"), "No se pudo guardar la evidencia gráfica de pantalla");
    native.display();
    evidence["resource_capture_changed_pixels"] = changed;
  }

  DisplaySettings RunDisplaySuite(sf::RenderWindow &native, WindowManager &manager,
                                   ResourceManager &resources, const std::filesystem::path &sandbox) {
    VerifyCentered(native);
    const auto initialPosition = native.getPosition();
    const auto initialHandle = native.getNativeHandle();
    for (int frame = 0; frame < 20; ++frame) manager.Update();
    Require(native.getPosition() == initialPosition, "La ventana salta de posición sin un cambio de pantalla");
    auto invalid = manager.GetDisplaySettings(); invalid.resolution = {0, 0};
    Require(!manager.RequestDisplaySettings(invalid), "Se aceptó una resolución vacía");
    invalid = manager.GetDisplaySettings(); invalid.resolution.x = std::numeric_limits<unsigned int>::max();
    Require(!manager.RequestDisplaySettings(invalid), "Se aceptó una resolución fuera del monitor");
    invalid = manager.GetDisplaySettings(); invalid.framerateLimit = 1;
    Require(!manager.RequestDisplaySettings(invalid), "Se aceptó un límite de FPS inferior al mínimo");
    invalid.framerateLimit = 361;
    Require(!manager.RequestDisplaySettings(invalid), "Se aceptó un límite de FPS superior al máximo");
    invalid = manager.GetDisplaySettings(); invalid.fullscreen = true; invalid.borderless = true;
    Require(!manager.RequestDisplaySettings(invalid), "Se aceptaron modos de pantalla contradictorios");
    const auto modes = manager.GetAvailableResolutions();
    evidence["window_resolutions_checked"] = modes.size();
    for (const auto resolution : modes) {
      auto settings = manager.GetDisplaySettings(); settings.resolution = resolution;
      Apply(native, manager, settings);
      Require(native.getSize() == resolution, "Cambiar la resolución no respeta las dimensiones elegidas");
      VerifyCentered(native);
      VerifyCoordinates(native, manager);
      Require(native.getNativeHandle() == initialHandle, "Cambiar la resolución recreó la ventana");
    }
    auto settings = manager.GetDefaultDisplaySettings(); settings.fullscreen = false; settings.borderless = false;
    Apply(native, manager, settings);
    VerifyPacing(native, manager);
    settings = manager.GetDisplaySettings(); settings.borderless = true;
    Apply(native, manager, settings);
    VerifyCentered(native);
#ifdef _WIN32
    Require((GetWindowLongPtrW(native.getNativeHandle(), GWL_STYLE) & (WS_CAPTION | WS_THICKFRAME)) == 0,
            "La opción sin bordes conserva el marco nativo de Windows");
#endif
    evidence["transitions"] = TransitionProbe(native, manager, resources, 12);
    Require(manager.GetDisplaySettings().borderless, "El atajo no restauró el estilo anterior de ventana");
    VerifyCapture(native, manager, resources, sandbox);
    const auto previous = manager.GetDisplaySettings();
    const auto previousPosition = native.getPosition();
    settings = previous; settings.borderless = false;
    Require(manager.RequestDisplaySettings(settings), "No se pudo preparar la cancelación del modo de pantalla");
    manager.Update();
    Require(manager.IsDisplayConfirmationPending(), "El cambio de modo no exige confirmación");
    manager.ResolveDisplayChange(false); manager.Update(); Pump(native, manager);
    Require(manager.GetDisplaySettings() == previous && native.getPosition() == previousPosition,
            "Cancelar no restauró el modo, el tamaño y la posición anteriores");
    native.setVisible(false);
    Require(manager.RequestDisplaySettings(settings), "No se pudo preparar la restauración sin foco");
    manager.Update();
    sf::sleep(sf::seconds(gConfig.displayConfirmationSeconds + .1f));
    manager.Update(); Pump(native, manager);
    Require(!manager.IsDisplayConfirmationPending() && manager.GetDisplaySettings() == previous,
            "La confirmación de pantalla no vence con tiempo real sin foco");
    evidence["unfocused_timeout_seconds"] = gConfig.displayConfirmationSeconds + .1f;
    settings = previous; settings.fullscreen = true; settings.borderless = false;
    settings.verticalSync = true; settings.framerateLimit = 144;
    Apply(native, manager, settings);
    return manager.GetDisplaySettings();
  }
}

int main(int argc, char **argv) {
  try {
    const bool baseline = argc > 1 && std::string_view(argv[1]) == "--baseline";
    const auto sandbox = std::filesystem::absolute("build/display-test-sandbox");
    std::filesystem::create_directories(sandbox);
    SaveIsolation isolation(sandbox / (baseline ? "BaselineSave.json" : "Preferences.json"));
    DisplaySettings expected;
    sf::Vector2u expectedWindowed;
    {
      SaveManager save;
      save.Clear();
      sf::RenderWindow native(sf::VideoMode(gConfig.windowResolution), "NO TRACE / DISPLAY QA");
      const auto initialHandle = native.getNativeHandle();
      WindowManager manager(native, save);
      ResourceManager resources;
      manager.Initialize(); Pump(native, manager);
      Require(native.getNativeHandle() == initialHandle && !manager.ConsumeWindowRecreated(),
              "La inicialización recreó innecesariamente la ventana del motor");
      if (baseline) {
        std::cout << TransitionProbe(native, manager, resources, 6).dump(2) << '\n';
        native.close();
        return 0;
      }
      expected = RunDisplaySuite(native, manager, resources, sandbox);
      expectedWindowed = manager.GetWindowedResolution();
      native.close();
    }
    {
      SaveManager save;
      sf::RenderWindow native(sf::VideoMode(gConfig.windowResolution), "NO TRACE / PERSISTENCIA QA");
      WindowManager manager(native, save);
      manager.Initialize(); Pump(native, manager);
      Require(manager.GetDisplaySettings() == expected, "Cerrar y abrir perdió las preferencias de vídeo");
      Require(manager.GetWindowedResolution() == expectedWindowed, "La pantalla completa perdió la resolución de ventana guardada");
      manager.ToggleFullscreen(); manager.Update(); Confirm(manager); Pump(native, manager);
      Require(!manager.GetDisplaySettings().fullscreen && manager.GetDisplaySettings().borderless &&
              native.getSize() == expectedWindowed, "Al reiniciar no se recupera la ventana anterior a pantalla completa");
      VerifyCentered(native);
      native.close();
    }
    evidence["checks"] = checks;
    std::ofstream(sandbox / "display-metrics.json") << evidence.dump(2) << '\n';
    std::cout << "PASS DISPLAY: " << checks << " comprobaciones\n" << evidence.dump(2) << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL DISPLAY: " << error.what() << '\n';
    return 1;
  }
}
