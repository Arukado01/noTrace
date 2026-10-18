"""Genera el catálogo C++ desde las recetas y conserva el orden del enum de red."""

import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
types = (ROOT / "Games/Include/Types/NoTraceGameplayTypes.h").read_text(encoding="utf-8")
kinds = [name.strip() for name in re.search(r"enum class SoundKind \{([^}]+)\}", types).group(1).split(",")]
assert kinds.pop() == "Count"
assets = json.loads((ROOT / "Tools/Audio/Library.json").read_text(encoding="utf-8"))["assets"]
by_kind = {kind: [asset for asset in assets if asset["kind"] == kind] for kind in kinds}
assert set(kinds) == {asset["kind"] for asset in assets}
lines = ["#pragma once", "", "// Catálogo generado por Tools/Audio/GenerateCatalog.py desde grabaciones CC0 verificadas.", "namespace NoTrace::AudioConfig {", "  inline constexpr std::array<ClipSet, static_cast<std::size_t>(SoundKind::Count)> Profiles{{"]
for kind, clips in by_kind.items():
    assert 1 <= len(clips) <= 6
    variants = [json.dumps(asset["file"]) for asset in clips] + ['""'] * (6 - len(clips))
    step = kind.startswith("Step")
    ambient = clips[0]["loop"]
    gain = .35 if "Crouch" in kind else .85 if "Run" in kind else .6 if step else 1.
    priority = 0 if ambient else 1 if step else 4 if kind in ("Alarm", "DetectionConfirmed", "Explosion") else 3 if kind in ("Pistol", "Rifle", "NonLethal", "Hurt", "Death", "Fall") else 2
    instances = 1 if ambient or kind in ("Alarm", "SearchStart", "VigilanceStart", "AlertEnd") else 3 if step else 4
    lines.append("    ClipSet{{" + ", ".join(variants) + "}, " + f"{len(clips)}, {gain:.2f}f, {priority}, {instances}, {'true' if ambient else 'false'}" + "},")
lines += ["  }};", "  inline constexpr std::array<std::string_view, Profiles.size()> Names{"]
lines += ["    " + ", ".join(json.dumps(kind) for kind in kinds) + "};"]
lines += ["  inline constexpr auto Files = [] {", "    std::array<std::string_view, Profiles.size()> files{};", "    for (std::size_t index = 0; index < files.size(); ++index) files[index] = Profiles[index].variants[0];", "    return files;", "  }();", "}", ""]
(ROOT / "Games/Include/Config/NoTraceAudioCatalog.h").write_text("\n".join(lines), encoding="utf-8")
print(f"Catálogo: {len(kinds)} familias, {len(assets)} variantes")
