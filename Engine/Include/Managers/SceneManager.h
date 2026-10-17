#pragma once

#include <optional>
#include <string>
#include <string_view>

class SceneManager {
private:
  std::string currentScene_;
  std::optional<std::string> nextScene_;
  bool quitRequested_ = false;

public:
  void ChangeScene(std::string_view name);
  void RestartCurrentScene();
  void RequestQuit();

private:
  friend class Engine;

  std::optional<std::string> FetchNextScene();
  bool FetchQuitRequest();
};
