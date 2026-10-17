#include "NoTraceGame.h"
#include "Config/NoTraceDevicesConfig.h"
#include <algorithm>
#include <array>
#include <format>
#include <memory>

namespace NoTrace {
  void Game::BuildPuzzle() {
    const auto *agent = LocalAgent(); if (!agent || !agent->puzzle.target) return;
    const auto &puzzle = agent->puzzle;
    puzzleStageRendered_ = puzzle.stage; puzzleTargetRendered_ = puzzle.target; puzzleProgress_.reset();
    Label("INTERVENCIÓN TÁCTICA CIO", {30.f, 20.f}, {714.f, 45.f}, 28);
    Label("La operación y los temporizadores continúan. ESC cancela.", {30.f, 76.f}, {714.f, 35.f}, 18);
    if (puzzle.family == PuzzleFamily::Arrows) {
      const std::array<std::string, 4> arrows{"ARRIBA", "DERECHA", "ABAJO", "IZQUIERDA"};
      const std::array<std::string, 4> glyphs{"^ ARRIBA", "> DERECHA", "V ABAJO", "< IZQUIERDA"};
      Label(std::format("SECUENCIA DE FLECHAS  ·  Paso {} / {}", puzzle.stage + 1, puzzle.length), {30.f, 130.f}, {714.f, 45.f}, 24);
      std::array<std::string, 2> sequence;
      for (unsigned int index = 0; index < puzzle.sequence.size(); ++index) {
        auto &row = sequence[index / 3];
        if (index % 3) row += " / ";
        if (puzzle.sequence[index] >= 1 && puzzle.sequence[index] <= glyphs.size()) row += glyphs[puzzle.sequence[index] - 1];
      }
      Label("Reproduce el orden del circuito con los cuatro controles.", {30.f, 192.f}, {714.f, 48.f}, 20);
      for (unsigned int row = 0; row < sequence.size(); ++row) if (!sequence[row].empty()) Label(sequence[row], {30.f, 250.f + row * 38.f}, {714.f, 34.f}, 22);
      for (unsigned int index = 0; index < arrows.size(); ++index) {
        auto button = Button(std::to_string(index + 1) + "  " + arrows[index], 340.f, [this, index] { pendingPuzzleInput_ = static_cast<int>(index + 1); });
        button->setPosition({30.f + index * 183.f, 340.f}); button->setSize({165.f, 58.f}); button->setTextSize(18);
      }
    } else if (puzzle.family == PuzzleFamily::Wires) {
      const std::array<tgui::Color, 4> colors{{{170, 58, 65}, {48, 119, 175}, {159, 127, 37}, {50, 139, 93}}};
      Label(std::format("CABLEADO  ·  {} / 4 enlaces aislados", puzzle.stage), {30.f, 122.f}, {714.f, 40.f}, 24);
      auto selection = Label("Elige un cable y después el puerto del mismo color y letra.", {30.f, 166.f}, {714.f, 55.f}, 18);
      auto selected = std::make_shared<unsigned int>(0);
      for (unsigned int index = 0; index < DevicesConfig::WireCount; ++index) {
        const unsigned int source = index + 1, portLabel = puzzle.wireLabels[index];
        auto input = Button("CABLE " + std::string(1, static_cast<char>('A' + index)), 230.f + index * 53.f, [selected, source, selection] {
          *selected = source; selection->setText("Cable " + std::string(1, static_cast<char>('A' + source - 1)) + " seleccionado: elige su puerto de destino.");
        });
        input->setPosition({30.f, 230.f + index * 53.f}); input->setSize({245.f, 43.f}); input->getRenderer()->setBackgroundColor(colors[index]);
        input->setEnabled(!(puzzle.connectedSources & (1u << index)));
        auto output = Button("PUERTO " + std::to_string(index + 1) + "  /  " + std::string(1, static_cast<char>('A' + portLabel - 1)), 230.f + index * 53.f, [this, selected, index] {
          if (*selected != 0) pendingPuzzleInput_ = WireInput(*selected, index + 1);
        });
        output->setPosition({499.f, 230.f + index * 53.f}); output->setSize({245.f, 43.f});
        if (portLabel >= 1 && portLabel <= colors.size()) output->getRenderer()->setBackgroundColor(colors[portLabel - 1]);
        output->setEnabled(!(puzzle.connectedPorts & (1u << index)));
        Label((puzzle.connectedSources & (1u << index)) ? "AISLADO" : "CONECTAR  >", {294.f, 239.f + index * 53.f}, {190.f, 30.f}, 16);
      }
    } else if (puzzle.family == PuzzleFamily::Coordination) {
      Label("SINCRONIZACIÓN DE INTERRUPTORES", {30.f, 136.f}, {714.f, 45.f}, 24);
      Label("Mantén el botón 2 segundos. En equipo participan dos agentes.\nEn solitario se habilita el relé auxiliar.\nSoltar o cancelar reinicia el progreso.", {30.f, 198.f}, {714.f, 100.f}, 18);
      puzzleProgress_ = tgui::ProgressBar::create(); puzzleProgress_->setPosition({30.f, 320.f}); puzzleProgress_->setSize({714.f, 34.f});
      puzzleProgress_->setMaximum(100); puzzleProgress_->setValue(static_cast<unsigned int>(std::clamp(puzzle.coordinationProgress / DevicesConfig::CoordinationHoldDuration, 0.f, 1.f) * 100.f));
      puzzleProgress_->getRenderer()->setFillColor({47, 133, 113}); puzzleProgress_->getRenderer()->setBackgroundColor({18, 32, 45}); panel_->add(puzzleProgress_);
      auto hold = Button("MANTENER INTERRUPTOR", 380.f, [] {});
      hold->onMousePress([this] { pendingPuzzleHeld_ = true; });
      hold->onMouseRelease([this] { pendingPuzzleHeld_ = false; });
      hold->onMouseLeave([this] { pendingPuzzleHeld_ = false; });
    } else if (puzzle.family == PuzzleFamily::Levers) {
      Label("SECUENCIA DE PALANCAS DISTRIBUIDAS", {30.f, 130.f}, {714.f, 45.f}, 24);
      std::string sequence;
      for (const auto node : puzzle.sequence) { if (!sequence.empty()) sequence += "  >  "; sequence += static_cast<char>('A' + node - 1); }
      Label(sequence, {30.f, 190.f}, {714.f, 55.f}, 32);
      Label(std::format("Progreso: {} / {}. Cierra el panel y pulsa E en las palancas.\nSigue el orden indicado. Un error reinicia el circuito.", puzzle.stage, puzzle.length), {30.f, 255.f}, {714.f, 92.f}, 18);
      Label("Busca las palancas A, B, C y D junto al computador.\nSus letras aparecen cuando puedes verlas.", {30.f, 360.f}, {714.f, 65.f}, 18);
    }
    Button(puzzle.family == PuzzleFamily::Levers ? "VOLVER A LAS PALANCAS" : "CANCELAR SIN RESOLVER", 480.f, [this] { pendingPuzzleHeld_ = false; pendingPuzzleInput_ = 0; });
  }
}
