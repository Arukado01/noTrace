"""Registra poses candidatas desde el rig real antes de generar o publicar el atlas."""

import argparse
import hashlib
import json
from pathlib import Path

import GenerateNoTraceCharactersActions as actions
from GenerateNoTraceCharactersProne import ProneBindings
from ValidateNoTraceCharacterBaseline import ANCHOR_FIELDS, BASELINE_NAME, canonical_sha


REPOSITORY = Path(__file__).resolve().parents[1]


def file_sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def register(root, output):
    if output.exists():
        raise ValueError("El preregistro ya existe; elija otro archivo para conservar la evidencia")
    baseline = json.loads((root / "Manifests" / BASELINE_NAME).read_text(encoding="utf-8"))
    manifest_name = "Manifests/rig-pieces.canonical.json"
    manifest = json.loads((root / manifest_name).read_text(encoding="utf-8"))
    bindings = ProneBindings(root, canonical=True)
    tools = sorted(set(REPOSITORY.glob("Tools/GenerateNoTraceCharacters*.py")) | {
        REPOSITORY / "Tools/InspectNoTraceCharacterRig.py", Path(__file__).resolve()})
    sources = sorted({view["file"] for views in bindings.canonical.values() for view in views.values()})
    contract = {"schemaVersion": 2, "kind": "rig_socket_projection_v1", "baselineCanonicalSha256": canonical_sha(baseline),
                "canonicalManifest": {"file": manifest_name, "canonicalSha256": canonical_sha(manifest)},
                "tools": [{"file": path.relative_to(REPOSITORY).as_posix(), "sha256": file_sha(path)} for path in tools],
                "pieceFiles": [{"file": name, "sha256": file_sha(root / name)} for name in sources], "entries": []}
    originals = {entry["index"]: entry for entry in baseline["frames"]}
    for name, family, first, stance in actions.animation_variants():
        for direction in actions.DIRECTIONS:
            for phase in range(8):
                semantic, images = actions.resolved_frame(bindings, name, family, first, stance, direction, phase)
                semantic = json.loads(json.dumps(semantic, allow_nan=False))
                index = semantic["index"]
                if set(images) != set(originals[index]["rgbaSha256"]):
                    raise ValueError("Las capas del candidato difieren del contrato original: " + str(index))
                contract["entries"].append({"index": index,
                    "identity": {key: semantic[key] for key in ["clip", "stance", "direction", "phase"]},
                    "reason": "Piezas originales completas con bind uniforme fijo y sockets articulados; colocación natural revisable por pose",
                    "rgbaSha256": {layer: hashlib.sha256(image.tobytes()).hexdigest() for layer, image in images.items()},
                    "joints": semantic["joints"], "anchors": {key: semantic[key] for key in ANCHOR_FIELDS},
                    "semantic": semantic, "semanticSha256": canonical_sha(semantic), "jointsSha256": canonical_sha(semantic["joints"])})
        print(name + "/" + stance, flush=True)
    contract["entries"].sort(key=lambda row: row["index"])
    if {row["index"] for row in contract["entries"]} != set(originals):
        raise ValueError("El preregistro no cubre los índices originales exactos")
    for row in contract["tools"]:
        if file_sha(REPOSITORY / row["file"]) != row["sha256"]:
            raise ValueError("Cambió una herramienta durante el preregistro")
    for row in contract["pieceFiles"]:
        if file_sha(root / row["file"]) != row["sha256"]:
            raise ValueError("Cambió una pieza durante el preregistro")
    if canonical_sha(json.loads((root / manifest_name).read_text(encoding="utf-8"))) != contract["canonicalManifest"]["canonicalSha256"]:
        raise ValueError("Cambió el registro durante el preregistro")
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("x", encoding="utf-8") as stream:
        json.dump(contract, stream, indent=2, ensure_ascii=False, allow_nan=False)
        stream.write("\n")
    print(json.dumps({"status": "CANDIDATE_REQUIRES_VISUAL_REVIEW", "frames": len(contract["entries"]),
                      "canonicalSha256": canonical_sha(contract), "output": str(output)}))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=REPOSITORY / "Art/NoTrace/Characters")
    parser.add_argument("--output", type=Path, required=True)
    arguments = parser.parse_args()
    register(arguments.root.resolve(), arguments.output.resolve())
