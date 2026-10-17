#include "NoTraceWorldRenderer.h"
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
  int checks = 0;
  void Require(bool condition, const char *message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
  }
  bool Near(sf::Vector2f first, sf::Vector2f second, float tolerance) {
    return std::abs(first.x - second.x) <= tolerance && std::abs(first.y - second.y) <= tolerance;
  }
}

int main() {
  try {
    using NoTrace::WorldRenderer;
    const sf::Vector2f size{1600.f, 900.f};
    const sf::Vector2f camera{330.f, 475.f};
    const auto center = WorldRenderer::ProjectGround(camera, camera, size);
    Require(Near(center, {800.f, 482.f}, 0.001f), "Centro lógico distinto del anclaje visual del agente");
    Require(Near(WorldRenderer::UnprojectGround(center, camera, size), camera, 0.001f), "El cursor sobre el agente no vuelve a su suelo");
    Require(Near(WorldRenderer::ProjectGround(camera + sf::Vector2f{40.f, 0.f}, camera, size) - center, {54.f, 28.f}, 0.001f), "Eje X axonométrico invertido o mal escalado");
    Require(Near(WorldRenderer::ProjectGround(camera + sf::Vector2f{0.f, 40.f}, camera, size) - center, {-54.f, 28.f}, 0.001f), "Eje Y axonométrico invertido o mal escalado");
    Require(Near(WorldRenderer::UnprojectGround(center + sf::Vector2f{108.f, 0.f}, camera, size) - camera, {40.f, -40.f}, 0.001f), "Movimiento horizontal de pantalla no corresponde al suelo");
    Require(Near(WorldRenderer::UnprojectGround(center + sf::Vector2f{0.f, 54.f}, camera, size) - camera, {40.f, 40.f}, 0.001f), "Movimiento vertical de pantalla no corresponde al suelo");
    const std::array<sf::Vector2f, 4> sizes{{{1600.f, 900.f}, {1280.f, 720.f}, {1024.f, 768.f}, {2560.f, 1440.f}}};
    const std::array<sf::Vector2f, 4> cameras{{{0.f, 0.f}, {330.f, 475.f}, {1000.f, 760.f}, {-250.f, 1200.f}}};
    for (const auto logicalSize : sizes) for (const auto groundCamera : cameras) {
      for (float x : {-100.f, 0.f, 87.25f, 500.f, 1000.f}) for (float y : {-80.f, 0.f, 191.4f, 760.f}) {
        const sf::Vector2f ground{x, y};
        const auto projected = WorldRenderer::ProjectGround(ground, groundCamera, logicalSize);
        const auto restored = WorldRenderer::UnprojectGround(projected, groundCamera, logicalSize);
        Require(Near(ground, restored, 1.12f), "La cuantización del arte introduce más de un píxel de error por eje lógico");
        Require(std::fmod(projected.x, NoTrace::PresentationConfig::PixelSize) == 0.f &&
                    std::fmod(projected.y, NoTrace::PresentationConfig::PixelSize) == 0.f,
                "La proyección produce posiciones fuera de la cuadrícula de píxeles");
        const sf::Vector2f translated{131.f, -47.f};
        Require(Near(projected, WorldRenderer::ProjectGround(ground + translated, groundCamera + translated, logicalSize), 0.001f),
                "La cámara altera la posición relativa de objetos y actores");
      }
      for (const auto cursor : std::array<sf::Vector2f, 5>{{{0.f, 0.f}, {127.f, 323.f}, {800.f, 482.f}, {1599.f, 899.f}, {-60.f, 1100.f}}}) {
        const auto ground = WorldRenderer::UnprojectGround(cursor, groundCamera, logicalSize);
        const auto represented = WorldRenderer::ProjectGround(ground, groundCamera, logicalSize);
        Require(Near(cursor, represented, 1.001f), "El destino del cursor no coincide con su representación lógica");
      }
    }
    std::cout << "PASS presentación: " << checks << " comprobaciones de ejes, cámara, cursor y cuantización sin ventana\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL presentación: " << error.what() << '\n';
    return 1;
  }
}
