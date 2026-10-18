"""Actualiza audio y recursos visuales runtime de NO TRACE en el inventario de producción."""

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
runtime = json.loads((ROOT / "Content/Textures/NoTrace/Characters/V2/CharacterAtlasRuntime.json").read_text(encoding="utf-8"))
assert all(Path(page["file"]).name == page["file"] and Path(page["file"]).suffix == ".png" for page in runtime["pages"]), "Página runtime de personaje fuera de su directorio"
characters = ["Content/Textures/NoTrace/Characters/V2/" + page["file"] for page in runtime["pages"]]
characters += ["Content/Textures/NoTrace/Characters/V2/" + name for name in ("WeaponProps.png", "WeaponSelection.png", "CharacterAtlasMetadata.json", "CharacterAtlasRuntime.json")]
first_person = ["Content/Textures/NoTrace/FirstPerson/" + name for name in ("Actors.png", "Viewmodels.png", "Runtime.json")]
assert all((ROOT / path).is_file() for path in environment + characters + first_person), "Falta un recurso visual obligatorio antes de inventariar"
inventory.write_text("\n".join(sorted(set(current + audio + environment + characters + first_person))) + "\n", encoding="utf-8")
print(f"Inventario: {len(audio)} recursos y documentos de audio; {len(environment) + len(characters) + len(first_person)} visuales verificados")
