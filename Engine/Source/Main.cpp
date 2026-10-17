#include <SFML/GpuPreference.hpp>
#include <SFML/System/Sleep.hpp>

#include "Core/Engine.h"
#include "SFML/System/Time.hpp"
#include <iostream>
#include <string_view>

SFML_DEFINE_DISCRETE_GPU_PREFERENCE

int main(int argc, char *argv[]) {
  std::string_view initialScene = "Menu";
  if (argc == 3 && std::string_view(argv[1]) == "--scene") {
    initialScene = argv[2];
    if (initialScene != "Menu" && initialScene != "Bounce" && initialScene != "Clicker" && initialScene != "NoTraceGame") {
      std::cerr << "Escena de desarrollo no válida. Usa Menu, NoTraceGame, Bounce o Clicker.\n";
      return 1;
    }
  } else if (argc != 1) {
    std::cerr << "Uso: AwesomeEngine.exe [--scene Menu|NoTraceGame|Bounce|Clicker]\n";
    return 1;
  }
  Engine engine(initialScene);

  while (engine.IsRunning()) {
    engine.ProcessEvent();

    if (!engine.IsRunning()) {
      break;
    }

    engine.Update();
    if (!engine.HasFocus()) {
      sf::sleep(sf::milliseconds(10));
      continue;
    }

    engine.Render();
  }
}
