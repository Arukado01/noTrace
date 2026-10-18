#include "NoTraceFirstPersonArt.h"
#include "Core/EngineContext.h"
#include <stdexcept>

namespace NoTrace {
  FirstPersonArt::FirstPersonArt(EngineContext &context) {
    auto *actors = context.resources.FetchTexture(FirstPersonArtConfig::Actors);
    auto *viewmodels = context.resources.FetchTexture(FirstPersonArtConfig::Viewmodels);
    if (!actors || !viewmodels) throw std::runtime_error("Faltan recursos frontales de primera persona");
    if (actors->getSize() != sf::Vector2u{FirstPersonArtConfig::ActorCell * FirstPersonArtConfig::ActorColumns, FirstPersonArtConfig::ActorCell * FirstPersonArtConfig::ActorRows} ||
        viewmodels->getSize() != sf::Vector2u{FirstPersonArtConfig::ViewmodelWidth * FirstPersonArtConfig::ViewmodelColumns, FirstPersonArtConfig::ViewmodelHeight * FirstPersonArtConfig::ViewmodelRows})
      throw std::runtime_error("El atlas frontal no coincide con su contrato");
    actors->setSmooth(false); viewmodels->setSmooth(false);
    // El raycaster conserva una copia CPU de texturas administradas y evita lecturas GPU por fotograma.
    actors_ = actors->copyToImage(); viewmodels_ = viewmodels->copyToImage();
  }
  const sf::Image &FirstPersonArt::ActorsPixels() const { return actors_; }
  const sf::Image &FirstPersonArt::ViewmodelPixels() const { return viewmodels_; }
  FirstPersonArt::Frame FirstPersonArt::ActorFrame(unsigned int variant, unsigned int state) {
    if (variant >= FirstPersonArtConfig::ActorRows || state >= FirstPersonArtConfig::ActorColumns) throw std::out_of_range("Pose frontal fuera del atlas");
    return {{{static_cast<int>(state * FirstPersonArtConfig::ActorCell), static_cast<int>(variant * FirstPersonArtConfig::ActorCell)},
        {FirstPersonArtConfig::ActorCell, FirstPersonArtConfig::ActorCell}}, {FirstPersonArtConfig::FeetX, FirstPersonArtConfig::FeetY}};
  }
  FirstPersonArt::Frame FirstPersonArt::ViewmodelFrame(WeaponKind weapon, bool suppressed, unsigned int state) {
    if (state >= FirstPersonArtConfig::ViewmodelColumns) throw std::out_of_range("Pose de arma fuera del atlas");
    const unsigned int row = (weapon == WeaponKind::Pistol ? 0 : 2) + (suppressed ? 1 : 0);
    return {{{static_cast<int>(state * FirstPersonArtConfig::ViewmodelWidth), static_cast<int>(row * FirstPersonArtConfig::ViewmodelHeight)},
        {FirstPersonArtConfig::ViewmodelWidth, FirstPersonArtConfig::ViewmodelHeight}}, {128.f, 160.f}};
  }
}
