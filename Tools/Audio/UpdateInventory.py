"""Actualiza sólo las entradas de audio de NO TRACE en el inventario de producción."""

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
inventory = ROOT / "Tools/Production/RequiredContent.txt"
current = inventory.read_text(encoding="utf-8").splitlines()
clips = json.loads((ROOT / "Tools/Audio/Library.json").read_text(encoding="utf-8"))["assets"]
audio = ["Content/Audio/NoTrace/" + item["file"] for item in clips]
audio += ["Content/Audio/NoTrace/manifest.json", "Content/Audio/NoTrace/LICENSE.txt", "Content/Audio/NoTrace/CREDITS.md", "Content/Audio/NoTrace/gallery.html"]
assert len(set(audio)) == len(audio)
assert all((ROOT / path).is_file() for path in audio), "Falta un recurso preparado antes de inventariar"
current = [path for path in current if not path.startswith(("Content/Sounds/NoTrace/", "Content/Audio/NoTrace/"))]
environment = ["Content/Textures/NoTrace/Environment/" + name for name in ("MilitaryFurniture.png", "MilitaryMaterials.png", "MilitaryEnvironment.manifest.json")]
characters = [f"Content/Textures/NoTrace/Characters/V2/Characters-{page:02}.png" for page in range(11)]
characters += ["Content/Textures/NoTrace/Characters/V2/" + name for name in ("WeaponProps.png", "WeaponSelection.png", "CharacterAtlasMetadata.json", "CharacterAtlasRuntime.json")]
assert all((ROOT / path).is_file() for path in environment + characters), "Falta un recurso visual obligatorio antes de inventariar"
inventory.write_text("\n".join(sorted(set(current + audio + environment + characters))) + "\n", encoding="utf-8")
print(f"Inventario: {len(audio)} recursos y documentos de audio verificados")
