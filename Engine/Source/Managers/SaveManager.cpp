#include "Managers/SaveManager.h"
#include <fstream>
#include <cstdlib>
#include <memory>

SaveManager::SaveManager() {
  // El lanzador puede aislar el guardado sin cambiar las rutas de recursos de la escena.
#ifdef _WIN32
  wchar_t *environment = nullptr; std::size_t count = 0;
  if (_wdupenv_s(&environment, &count, L"AWESOME_ENGINE_SAVE_FILE") == 0) {
    const std::unique_ptr<wchar_t, decltype(&std::free)> path(environment, &std::free);
    if (path && *path) file_ = path.get();
  }
#else
  if (const auto *path = std::getenv("AWESOME_ENGINE_SAVE_FILE"); path && *path) file_ = path;
#endif
  if (std::ifstream file{file_}) {
    values_ = nlohmann::json::parse(file);
  } else {
    values_ = nlohmann::json::object();
  }
}

SaveManager::~SaveManager() {
  if (!values_.empty()) {
    if (std::ofstream file{file_}) {
      file << values_.dump(4);
    }
  }
}

bool SaveManager::Has(std::string_view key) const {
  return values_.contains(key);
}

void SaveManager::Erase(std::string_view key) { values_.erase(key); }

void SaveManager::Clear() { values_.clear(); }
