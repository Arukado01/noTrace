#pragma once

#include "Config/NoTraceSpriteConfig.h"
#include <SFML/Graphics/Image.hpp>
#include <filesystem>
#include <fstream>
#include <map>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <vector>

namespace EngineTests {
  struct CharacterAtlasFixture {
    nlohmann::json metadata;
    std::vector<sf::Image> pages;
    sf::Image weapons;
    std::map<std::size_t, nlohmann::json> frames;
    static sf::Vector2f Vector(const nlohmann::json &value) { return {value[0].get<float>(), value[1].get<float>()}; }
    static sf::IntRect Rectangle(const nlohmann::json &value) { return {{value[0].get<int>(), value[1].get<int>()}, {value[2].get<int>(), value[3].get<int>()}}; }
    explicit CharacterAtlasFixture(const std::filesystem::path &project = {}) {
      std::ifstream input(project / NoTrace::CharacterArtConfig::RuntimeMetadata);
      metadata = nlohmann::json::parse(input);
      if (metadata.at("schemaVersion") != 3 || metadata.at("frames").size() != NoTrace::SpriteConfig::AuthoredFrameCount) throw std::runtime_error("Contrato de poses autoradas incompleto");
      const auto directory = project / "Content/Textures" / NoTrace::CharacterArtConfig::TextureRoot;
      for (const auto &page : metadata.at("pages")) {
        sf::Image image;
        if (!image.loadFromFile(directory / page.at("file").get<std::string>())) throw std::runtime_error("Falta página del atlas en la evidencia visual");
        pages.push_back(std::move(image));
      }
      if (!weapons.loadFromFile(directory / metadata.at("weapons").at("file").get<std::string>())) throw std::runtime_error("Falta atlas de armas en la evidencia visual");
      for (const auto &entry : metadata.at("frames")) {
        if (!frames.emplace(entry.at("index").get<std::size_t>(), entry).second) throw std::runtime_error("Pose duplicada en metadatos");
      }
    }
    const sf::Image &ImageFor(std::size_t index, const char *layer) const { return pages.at(frames.at(index).at("art").at(layer).at("page").get<std::size_t>()); }
    sf::IntRect Rect(std::size_t index, const char *layer) const { return Rectangle(frames.at(index).at("art").at(layer).at("rect")); }
    sf::Vector2f Offset(std::size_t index, const char *layer) const { return Vector(frames.at(index).at("art").at(layer).at("offset")); }
  };
}
