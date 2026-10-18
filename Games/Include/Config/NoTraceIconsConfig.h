#pragma once

#include "Config/NoTracePresentationConfig.h"
#include "Types/NoTraceIconTypes.h"
#include <array>
#include <cstddef>
#include <string_view>

namespace NoTrace::IconsConfig {
  inline constexpr std::size_t Size = 16;
  using Bitmap = std::array<std::string_view, Size>;
  struct Definition { std::string_view name; Bitmap pixels; std::size_t width = Size; };
  inline constexpr std::array<Definition, static_cast<std::size_t>(Icons::Kind::Count)> Glyphs{{
    {"Dossier", {{
      "................", "...oooooooo.....", "..offfffffoo....", "..offfffffaoo...",
      "..offfffffaaao..", "..offffffooooo..", "..offfffffffso..", "..offaaaffffso..",
      "..offfffffffso..", "..offaaaafffso..", "..offfffffffso..", "..offaaaafffso..",
      "..offfffffffso..", "..osssssssssoo..", "...oooooooooo...", "................"
    }}},
    {"Connection", {{
      "................", "................", "..oo........oo..", "..oao......oao..",
      ".ooaoo....ooaoo.", ".offfo....offfo.", ".offfop..pofffo.", ".offfoaaaaofffo.",
      ".offfoaaaaofffo.", ".offfop..pofffo.", ".ossoo....oosso.", "..ooo......ooo..",
      "...oa......ao...", "...oaaaaaaaao...", "....oooooooo....", "................"
    }}},
    {"Stairs", {{
      "................", "..........oooo..", ".........offfo..", "........oaaaao..",
      ".......ooaaaao..", "......offffffo..", ".....oaaaaoooo..", "....ooaaaao.....",
      "...offffffo.....", "..oaaaaoooo.....", ".ooaaaao........", ".offfffo........",
      ".ossoooo........", ".oooo...........", "................", "................"
    }}},
    {"Elevator", {{
      "....a.....a.....", "...aaa....a.....", "....a....aaa....", "....a.....a.....",
      "..oooooooooooo..", "..offfffffffso..", "..offffoffffso..", "..offffoffffso..",
      "..offffoffffso..", "..offffoffffso..", "..offffoffffso..", "..offffoffffso..",
      "..offffoffffso..", "..offffoffffso..", "..oaaaaaaaaaao..", "..oooooooooooo.."
    }}},
    {"Door", {{
      "................", "..oooooooooooo..", "..offfffffffoo..", "..oaossssssfoo..",
      "..oaossssssfoo..", "..oaossssssfoo..", "..oaossssssfoo..", "..oaosssassfoo..",
      "..oaosssafsfoo..", "..oaossssssfoo..", "..oaossssssfoo..", "..oaossssssfoo..",
      "..oaossssssfoo..", "..oaoooooooofo..", "..oooooooooooo..", "................"
    }}},
    {"Interact", {{
      "................", ".......oo.......", "....oooffooo....", "...offoffoffo...",
      "...offoffoffooo.", "...offoffoffofo.", "...offoffoffofo.", "...offffffffofo.",
      ".oooffffffffofo.", ".offofffffffso..", "..offfffffffso..", "...offffffffso..",
      "....offffffso...", ".....oaaaaoo....", ".....oaaaao.....", "......oooo......"
    }}},
    {"Ammunition", {{
      "................", "...o...o...o....", "..oao.oao.oao...", "..oao.oao.oao...",
      "..oao.oao.oao...", "..ooo.ooo.ooo...", "..ofo.ofo.ofo...", "..ofo.ofo.ofo...",
      "..ofo.ofo.ofo...", "..ofo.ofo.ofo...", "..ofo.ofo.ofo...", "..oso.oso.oso...",
      "..ooo.ooo.ooo...", "..oao.oao.oao...", "..ooo.ooo.ooo...", "................"
    }}},
    {"Detection", {{
      "................", "................", ".....oooooo.....", "...ooaaaaaaoo...",
      "..oaffffffffao..", ".oafffppppfffao.", "oafffppppppfffao", "oafffppffppfffao",
      "oafffppppppfffao", ".oafffppppfffao.", "..oaffffffffao..", "...ooaaaaaaoo...",
      ".....oooooo.....", "................", "................", "................"
    }}},
    {"Unconscious", {{
      "........................", "................aaaaa...", "......aaaaa........a....", "....affffffa......a.....",
      "...affffffffa....a......", "..affffffffffa..aaaaa...", "..afoofffffffa..........", "...affofffffffa....aaaa.",
      "...affffffooofa......a..", "....afffffffffa.....a...", ".....afooffffa.....aaaa.", "......afffffa...........",
      ".......aaaaa....aaa.....", ".................a......", "................aaa.....", "........................"
    }}, 24},
    {"Dead", {{
      "................", ".....aaaaaa.....", "...aaffffffaa...", "..affffffffffa..",
      "..affffffffffa..", "..affooffooffa..", "..affooffooffa..", "..affffffffffa..",
      "...afffoofffa...", "....affffffa....", "....affffffa....", "....afofofoa....",
      "....affffffa....", ".....aaaaaa.....", "................", "................"
    }}}
  }};
  consteval bool Valid() {
    for (const auto &glyph : Glyphs) for (const auto row : glyph.pixels) {
      if (row.size() != glyph.width) return false;
      for (const char pixel : row) if (std::string_view(".osfap").find(pixel) == std::string_view::npos) return false;
    }
    return true;
  }
  static_assert(Valid(), "Los iconos deben contener 16 filas de píxeles tipados con su ancho declarado");
}
