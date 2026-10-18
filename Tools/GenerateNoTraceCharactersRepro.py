import argparse
import hashlib
import json
from pathlib import Path

from GenerateNoTraceCharactersActions import generate
from GenerateNoTraceCharactersValidate import validate


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def reproduce(root):
    atlas = root / "Atlas"
    paths = sorted((atlas / "Packed").glob("*.png")) + [atlas / "WeaponProps.png", atlas / "WeaponSelection.png", atlas / "CharacterAtlasRuntime.json"]
    before = {path: digest(path) for path in paths}
    generate(root)
    validate(root)
    records = [{"file": path.name, "sha256": digest(path), "identical": before[path] == digest(path)} for path in paths]
    tools = [{"file": path.as_posix(), "sha256": digest(path)} for path in sorted(Path("Tools").glob("GenerateNoTraceCharacters*.py"))]
    report = {"author": "Carlos Cortina", "organization": "CIO", "date": "2026-10-18", "status": "PASS" if all(item["identical"] for item in records) else "FAIL",
              "sameSourceRegeneration": records, "runtimeMetadataByteIdentical": records[-1]["identical"], "clips": 52, "poses": 4352, "tools": tools,
              "scope": "Regeneración offline desde piezas originales; no modifica Content ni sustituye pruebas gráficas runtime"}
    (root / "Manifests/production-reproducibility.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps({key: value for key, value in report.items() if key not in ["sameSourceRegeneration", "tools"]}))
    if report["status"] != "PASS":
        raise ValueError("La regeneración cambió un recurso empaquetado")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Regenera el atlas de personajes y comprueba identidad binaria sin modificar Content")
    parser.add_argument("--root", type=Path, default=Path("Art/NoTrace/Characters"))
    reproduce(parser.parse_args().root)
