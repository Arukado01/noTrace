import argparse
import hashlib
import json
from pathlib import Path

from GenerateNoTraceCharactersActions import generate
from GenerateNoTraceCharactersValidate import validate


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def reproduce(root, baseline=None, references=None, exceptions=None, toe_witness=None, death_witness=None, crouch_push_witness=None, ik_floor_witness=None, recovery_witness=None, expected_contract_sha256=None):
    atlas = root / "Atlas"
    paths = sorted((atlas / "Packed").glob("*.png")) + [atlas / "WeaponProps.png", atlas / "WeaponSelection.png", atlas / "CharacterAtlasRuntime.json"]
    before = {path: digest(path) for path in paths}
    generate(root)
    validate(root, baseline, references, exceptions, toe_witness, death_witness=death_witness, crouch_push_witness=crouch_push_witness, ik_floor_witness=ik_floor_witness, recovery_witness=recovery_witness, expected_contract_sha256=expected_contract_sha256)
    records = [{"file": path.name, "sha256": digest(path), "identical": before[path] == digest(path)} for path in paths]
    tools = [{"file": path.as_posix(), "sha256": digest(path)} for path in sorted(Path("Tools").glob("GenerateNoTraceCharacters*.py"))]
    report = {"author": "Carlos Cortina", "organization": "CIO", "date": "2026-10-18", "status": "PASS" if all(item["identical"] for item in records) else "FAIL",
              "sameSourceRegeneration": records, "runtimeMetadataByteIdentical": records[-1]["identical"], "clips": 53, "poses": 4544, "tools": tools,
              "scope": "Regeneración offline desde piezas originales; no modifica Content ni sustituye pruebas gráficas runtime"}
    (root / "Manifests/production-reproducibility.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps({key: value for key, value in report.items() if key not in ["sameSourceRegeneration", "tools"]}))
    if report["status"] != "PASS":
        raise ValueError("La regeneración cambió un recurso empaquetado")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Regenera el atlas de personajes y comprueba identidad binaria sin modificar Content")
    parser.add_argument("--root", type=Path, default=Path("Art/NoTrace/Characters"))
    parser.add_argument("--baseline", type=Path)
    parser.add_argument("--references", type=Path)
    parser.add_argument("--exceptions", type=Path)
    parser.add_argument("--toe-witness", type=Path)
    parser.add_argument("--death-witness", type=Path)
    parser.add_argument("--crouch-push-witness", type=Path)
    parser.add_argument("--ik-floor-witness", type=Path)
    parser.add_argument("--recovery-witness", type=Path)
    parser.add_argument("--expected-contract-sha256")
    args = parser.parse_args()
    reproduce(args.root, args.baseline, args.references, args.exceptions, args.toe_witness, args.death_witness, args.crouch_push_witness, args.ik_floor_witness, args.recovery_witness, args.expected_contract_sha256)
