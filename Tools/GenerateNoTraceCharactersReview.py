import argparse
import hashlib
import json
from collections import defaultdict
from datetime import datetime, timezone
from pathlib import Path

from GenerateNoTraceCharactersCatalog import generate


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def review(root, log, captures, started):
    start_time = datetime.fromisoformat(started.replace("Z", "+00:00"))
    if start_time.tzinfo is None:
        raise ValueError("La fecha de inicio debe incluir zona horaria")
    if log.stat().st_mtime < start_time.timestamp():
        raise ValueError("El log GUI precede el inicio declarado")
    if "100% tests passed" not in log.read_text(encoding="utf-8-sig"):
        raise ValueError("El log GUI completo no acredita una ejecución aprobada")
    runtime = root / "Atlas/CharacterAtlasRuntime.json"
    content = Path("Content/Textures/NoTrace/Characters/V2/CharacterAtlasRuntime.json")
    if digest(runtime) != digest(content):
        raise ValueError("Content difiere del catálogo de personajes auditado")
    atlas = json.loads((root / "Atlas/CharacterAtlasMetadata.json").read_text(encoding="utf-8"))
    assets = []
    for path in sorted((root / "Atlas/Packed").glob("*.png")) + [root / "Atlas/WeaponProps.png", root / "Atlas/WeaponSelection.png", root / "Atlas/CharacterAtlasMetadata.json", runtime]:
        deployed = content.parent / path.name
        if digest(path) != digest(deployed):
            raise ValueError("Un recurso activo difiere de Art: " + path.name)
        assets.append({"file": deployed.as_posix(), "sha256": digest(deployed)})
    clips_by_index = {entry["index"]: entry["clip"] for entry in atlas["frames"]}
    evidence = defaultdict(set)
    reports = []
    for name in ["world-fixtures.json", "character-fixtures.json", "orientation-fixtures.json", "icons-fixtures.json", "level-fixtures.json"]:
        path = captures / name
        if path.stat().st_mtime < start_time.timestamp():
            raise ValueError("El informe no pertenece a esta ejecución GUI: " + name)
        if path.stat().st_mtime > log.stat().st_mtime:
            raise ValueError("El informe es posterior al cierre del log GUI: " + name)
        report = json.loads(path.read_text(encoding="utf-8"))
        if isinstance(report, dict) and report.get("status") != "PASS":
            raise ValueError("Un informe gráfico no acredita PASS: " + name)
        reports.append({"file": path.as_posix(), "sha256": digest(path)})
        entries = report if isinstance(report, list) else report.get("evidence", [])
        for entry in entries:
            indices = [actor["frame"] for actor in entry.get("selectedFrames", []) + entry.get("poses", [])]
            if "atlasFrame" in entry:
                indices.append(entry["atlasFrame"])
            for index in indices:
                if index in clips_by_index:
                    evidence[clips_by_index[index]].add(entry["file"])
            case = entry.get("case", "")
            gesture = "SuppressorAttach" if case.startswith("suppressor-") else None
            if gesture:
                evidence[gesture].add(entry["file"])
            if case.startswith("switch-"):
                evidence["WeaponHolster" if entry["phase"] < .5 else "WeaponDraw"].add(entry["file"])
    report = {"author": "Carlos Cortina", "organization": "CIO", "date": datetime.now(timezone.utc).isoformat(), "status": "PASS",
              "runtimeMetadataSha256": digest(runtime), "runtimeAssets": assets, "fullGuiLog": {"file": log.as_posix(), "sha256": digest(log), "startedUtc": start_time.isoformat(),
                  "finishedUtc": datetime.fromtimestamp(log.stat().st_mtime, timezone.utc).isoformat()}, "reports": reports,
              "clipEvidence": {clip: sorted(files) for clip, files in sorted(evidence.items())},
              "scope": "Estados preparados dibujados por el renderer real, revisión visual CQC/prone y regresión GUI completa; no acredita partida manual ni LAN entre dos equipos"}
    (root / "Manifests/runtime-review.json").write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
    registration_path = root / "Manifests/rig-pieces.registration.json"
    registration = json.loads(registration_path.read_text(encoding="utf-8"))
    registration["status"] = "registered_original_parts_runtime_reviewed"
    registration["runtimeReview"] = "runtime-review.json"
    registration_path.write_text(json.dumps(registration, indent=2, ensure_ascii=False), encoding="utf-8")
    pieces_path = root / "Pieces/OperativeAuthoredParts.manifest.json"
    pieces = json.loads(pieces_path.read_text(encoding="utf-8"))
    pieces["registrationSha256"] = digest(registration_path)
    pieces_path.write_text(json.dumps(pieces, indent=2, ensure_ascii=False), encoding="utf-8")
    generate(root)
    print(json.dumps({"status": "PASS", "runtimeClipsObserved": len(evidence), "offlineClips": len(atlas["clips"]), "report": str(root / "Manifests/runtime-review.json")}))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Vincula aceptación gráfica ejecutada con el catálogo sin regenerar imágenes")
    parser.add_argument("--root", type=Path, default=Path("Art/NoTrace/Characters"))
    parser.add_argument("--log", type=Path, required=True)
    parser.add_argument("--started", required=True, help="Inicio UTC real del proceso GUI, en formato ISO8601")
    parser.add_argument("--captures", type=Path, default=Path("build/gui-test-sandbox/captures"))
    arguments = parser.parse_args()
    review(arguments.root, arguments.log, arguments.captures, arguments.started)
