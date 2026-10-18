#include "Managers/ResourceManager.h"
#include "Utils/Log.h"
#include <filesystem>

sf::Texture *ResourceManager::FetchTexture(const std::string &filename) {
  if (!textures_.contains(filename)) {
    if (!textures_[filename].loadFromFile("Content/Textures/" + filename)) {
      LOG_ERROR("Failed to load texture: {}", filename);
      return nullptr;
    }
  }

  return &textures_.at(filename);
}

sf::SoundBuffer *ResourceManager::FetchSound(const std::string &filename) {
  const auto [entry, inserted] = sounds_.try_emplace(filename);
  if (inserted) {
    const auto current = std::filesystem::path("Content/Audio") / filename;
    const auto path = std::filesystem::exists(current) ? current : std::filesystem::path("Content/Sounds") / filename;
    if (!entry->second.loadFromFile(path)) {
      sounds_.erase(entry);
      LOG_ERROR("Failed to load sound: {}", filename);
      return nullptr;
    }
  }

  return &entry->second;
}

sf::Font *ResourceManager::FetchFont(const std::string &filename) {
  if (!fonts_.contains(filename)) {
    if (!fonts_[filename].openFromFile("Content/Fonts/" + filename)) {
      LOG_ERROR("Failed to load font: {}", filename);
      return nullptr;
    }
  }

  return &fonts_.at(filename);
}

std::optional<sf::Music>
ResourceManager::FetchMusic(const std::string &filename) const {
  sf::Music music;

  if (!music.openFromFile("Content/Musics/" + filename)) {
    LOG_ERROR("Failed to load music: {}", filename);
    return std::nullopt;
  }

  return music;
}
