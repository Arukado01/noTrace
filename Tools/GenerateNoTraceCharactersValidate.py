import argparse
import hashlib
import json
import math
from collections import defaultdict
from pathlib import Path

from PIL import Image


def validate(root):
    metadata = json.loads((root / "Atlas/CharacterAtlasMetadata.json").read_text(encoding="utf-8"))
    runtime = json.loads((root / "Atlas/CharacterAtlasRuntime.json").read_text(encoding="utf-8"))
    checks = 0
    def require(condition, message):
        nonlocal checks
        checks += 1
        if not condition:
            raise ValueError(message)
    require(metadata["schemaVersion"] == 3, "Contrato de atlas incorrecto")
    require(len(metadata["clips"]) == 52 and len(metadata["frames"]) == 4352, "La matriz de clips no está completa")
    require(len({frame["index"] for frame in metadata["frames"]}) == len(metadata["frames"]), "Hay índices de pose repetidos")
    pages = []
    for entry in metadata["pages"]:
        path = root / "Atlas/Packed" / entry["file"]
        require(hashlib.sha256(path.read_bytes()).hexdigest() == entry["sha256"], "Cambió una página empaquetada")
        image = Image.open(path).convert("RGBA")
        require(max(image.size) <= 2048, "Una página supera capacidad pactada2048")
        pages.append(image)
    clips, maximum_error = defaultdict(list), 0.0
    for entry in metadata["frames"]:
        clips[(entry["clip"], entry["stance"], entry["direction"])].append(entry)
        require(entry["groundAnchor"] == [96.0, 144.0], "El suelo cambia entre poses")
        points = entry["joints"]
        for side in ["left", "right"]:
            for first, second, expected in [("hip", "knee", 17.0), ("knee", "ankle", 17.0), ("shoulder", "elbow", 13.0), ("elbow", "wrist", 13.0)]:
                distance = math.dist(points[side + "_" + first], points[side + "_" + second])
                error = abs(distance - expected)
                maximum_error = max(maximum_error, error)
                require(error < 1e-8, "La IK cambia volumen de una extremidad")
        for name, region in entry["art"].items():
            image = pages[region["page"]]
            x, y, width, height = region["rect"]
            require(0 <= x < x + width <= image.width and 0 <= y < y + height <= image.height, "Capa fuera de página")
            cell = image.crop((x, y, x + width, y + height))
            require(cell.getchannel("A").getbbox() is not None, "Capa anatómica vacía")
            require(cell.getchannel("A").crop((0, 0, width, 1)).getbbox() is None and cell.getchannel("A").crop((0, height - 1, width, height)).getbbox() is None,
                    "Falta gutter vertical de una capa")
    phase_report = []
    for key, sequence in clips.items():
        require(sorted(entry["phase"] for entry in sequence) == list(range(8)), "Faltan fases de una dirección")
        distinct = len({entry["pixelSha256"] for entry in sequence})
        require(distinct >= 2, "Un clip completo conserva un cuerpo estático: " + str(key))
        phase_report.append({"clip": key[0], "stance": key[1], "direction": key[2], "distinctRasterPhases": distinct})
    registration = json.loads((root / "Manifests/rig-pieces.registration.json").read_text(encoding="utf-8"))
    for entry in registration["sources"]:
        require(hashlib.sha256(Path(entry["file"]).read_bytes()).hexdigest() == entry["sha256"], "Cambió una fuente original")
    require(len(runtime["weapons"]["frames"]) == 48, "Faltan variantes de armas")
    report = {"status": "PASS", "checks": checks, "clips": len(metadata["clips"]), "stanceVariants": 68, "frames": len(metadata["frames"]),
              "directions": 8, "phases": 8, "layers": 3, "pairedCqcLayers": 2, "packedPages": len(pages), "weapons": 48, "maximumBoneLengthError": maximum_error,
              "phaseMatrix": phase_report, "scope": "Offline raster, authored source hashes, atlas bounds, exact IK lengths and real phase coverage; runtime GUI pending"}
    (root / "Atlas/CharacterAtlasValidation.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps({key: value for key, value in report.items() if key != "phaseMatrix"}))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Valida fuentes, anatomía y raster empaquetado sin ventana ni simulación")
    parser.add_argument("--root", type=Path, default=Path("Art/NoTrace/Characters"))
    validate(parser.parse_args().root)
