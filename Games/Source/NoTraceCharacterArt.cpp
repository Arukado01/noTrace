#include "NoTraceCharacterArt.h"
#include "Config/NoTracePresentationConfig.h"
#include "Core/EngineContext.h"
#include <SFML/Graphics/Sprite.hpp>
#include <nlohmann/json.hpp>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <string>

namespace NoTrace {
  namespace {
    sf::Vector2f Vector(const nlohmann::json &value) { return {value[0].get<float>(), value[1].get<float>()}; }
    sf::IntRect Rectangle(const nlohmann::json &value) { return {{value[0].get<int>(), value[1].get<int>()}, {value[2].get<int>(), value[3].get<int>()}}; }
    bool Inside(sf::IntRect rect, const sf::Texture &texture) {
      return rect.position.x >= 0 && rect.position.y >= 0 && rect.size.x > 0 && rect.size.y > 0 &&
          rect.position.x + rect.size.x <= static_cast<int>(texture.getSize().x) && rect.position.y + rect.size.y <= static_cast<int>(texture.getSize().y);
    }
  }

  CharacterArt::CharacterArt(EngineContext &context) : context_(context), frames_(CharacterArtConfig::FrameCapacity) {
    std::ifstream input(CharacterArtConfig::RuntimeMetadata);
    const auto metadata = nlohmann::json::parse(input);
    if (metadata.at("schemaVersion") != 3) throw std::runtime_error("Versión de catálogo de personajes desconocida");
    for (const auto &page : metadata.at("pages")) {
      auto *texture = context.resources.FetchTexture(std::string(CharacterArtConfig::TextureRoot) + page.at("file").get<std::string>());
      if (!texture) throw std::runtime_error("Falta una página del atlas articulado de NO TRACE");
      texture->setSmooth(false); pages_.push_back(texture);
    }
    for (const auto &entry : metadata.at("frames")) {
      const auto index = entry.at("index").get<std::size_t>();
      auto &frame = frames_.at(index);
      if (frame.valid) throw std::runtime_error("Índice de pose repetido en el catálogo de personajes");
      frame.anchor = Vector(entry.at("groundAnchor")); frame.pelvis = Vector(entry.at("pelvis"));
      frame.carryGrip = Vector(entry.at("carryGrip"));
      frame.grip = Vector(entry.at("grip")); frame.muzzle = Vector(entry.at("muzzle")); frame.supportWidth = entry.at("supportWidth").get<float>();
      std::size_t layer = 0;
      for (const auto *name : {"full", "lower", "upper", "cqcBody", "cqcArms"}) {
        if (!entry.at("art").contains(name)) { ++layer; continue; }
        const auto &source = entry.at("art").at(name);
        auto &region = frame.art[layer++]; region.page = source.at("page").get<std::size_t>();
        region.rect = Rectangle(source.at("rect")); region.offset = Vector(source.at("offset"));
        if (region.page >= pages_.size() || !Inside(region.rect, *pages_[region.page])) throw std::runtime_error("Capa anatómica fuera de su página de atlas");
      }
      frame.valid = true;
    }
    const auto &weapons = metadata.at("weapons");
    weapons_ = context.resources.FetchTexture(std::string(CharacterArtConfig::TextureRoot) + weapons.at("file").get<std::string>());
    if (!weapons_ || weapons.at("frames").size() != weaponFrames_.size()) throw std::runtime_error("Catálogo de armas articuladas incompleto");
    weapons_->setSmooth(false);
    for (std::size_t index = 0; index < weaponFrames_.size(); ++index) {
      const auto &entry = weapons.at("frames")[index]; auto &frame = weaponFrames_[index];
      frame.rect = Rectangle(entry.at("rect")); frame.grip = Vector(entry.at("grip")); frame.muzzle = Vector(entry.at("muzzle"));
      if (!Inside(frame.rect, *weapons_)) throw std::runtime_error("Arma fuera del atlas");
    }
  }

  const CharacterArt::Frame &CharacterArt::Metadata(std::size_t index) const {
    const auto &frame = frames_.at(index);
    if (!frame.valid) throw std::runtime_error("La presentación solicitó una pose no autorada");
    return frame;
  }
  const CharacterArt::WeaponFrame &CharacterArt::WeaponMetadata(Weapon weapon, bool suppressed, std::size_t direction) const {
    return weaponFrames_.at(static_cast<std::size_t>(weapon) * 16 + (suppressed ? 8 : 0) + direction);
  }
  float CharacterArt::Scale() { return PresentationConfig::ActorHeight / CharacterArtConfig::StandingPixels; }
  sf::Vector2f CharacterArt::Map(std::size_t index, sf::Vector2f point, sf::Vector2f feet) const { return feet + (point - Metadata(index).anchor) * Scale(); }
  sf::Vector2f CharacterArt::AlignUpper(std::size_t lower, std::size_t upper, sf::Vector2f feet) const {
    return feet + (Metadata(lower).pelvis - Metadata(upper).pelvis) * Scale();
  }
  sf::Vector2f CharacterArt::Muzzle(std::size_t index, sf::Vector2f feet, Weapon weapon, bool suppressed, std::size_t direction) const {
    const auto &frame = Metadata(index); const auto &prop = WeaponMetadata(weapon, suppressed, direction);
    return Map(index, frame.grip, feet) + (prop.muzzle - prop.grip) * Scale();
  }
  void CharacterArt::Draw(std::size_t index, Layer layer, sf::Vector2f feet, sf::Color color) const {
    const auto &frame = Metadata(index); const auto &region = frame.art.at(static_cast<std::size_t>(layer));
    sf::Sprite sprite(*pages_.at(region.page), region.rect);
    sprite.setOrigin(frame.anchor - region.offset); sprite.setPosition(feet); sprite.setScale({Scale(), Scale()}); sprite.setColor(color);
    context_.renderer.Draw(sprite);
  }
  void CharacterArt::DrawWeapon(std::size_t index, sf::Vector2f feet, Weapon weapon, bool suppressed, std::size_t direction, sf::Color color, bool carry) const {
    const auto &frame = Metadata(index);
    if (frame.grip.x < 0.f && !carry) return;
    const auto &prop = WeaponMetadata(weapon, suppressed, direction);
    sf::Sprite sprite(*weapons_, prop.rect); sprite.setOrigin(prop.grip); sprite.setPosition(Map(index, frame.grip.x < 0.f ? frame.carryGrip : frame.grip, feet));
    sprite.setScale({Scale(), Scale()}); sprite.setColor(color); context_.renderer.Draw(sprite);
  }
}
