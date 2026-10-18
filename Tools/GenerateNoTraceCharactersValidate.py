import argparse
import hashlib
import json
import math
from collections import defaultdict
from pathlib import Path

from PIL import Image

from ValidateNoTraceCharacterBaseline import validate_current


def validate(root, baseline=None, references=None, exceptions=None, toe_witness=None, report_path=None, death_witness=None, crouch_push_witness=None, ik_floor_witness=None, recovery_witness=None, expected_contract_sha256=None):
    metadata = json.loads((root / "Atlas/CharacterAtlasMetadata.json").read_text(encoding="utf-8"))
    runtime = json.loads((root / "Atlas/CharacterAtlasRuntime.json").read_text(encoding="utf-8"))
    checks = 0
    def require(condition, message):
        nonlocal checks
        checks += 1
        if not condition:
            raise ValueError(message)
    baseline_report = validate_current(root, baseline, references, exceptions, toe_witness, death_witness, crouch_push_witness, ik_floor_witness, recovery_witness, expected_contract_sha256)
    require(not baseline_report["errors"], "El atlas cambia RGBA o semántica fuera de las excepciones exactas: " + "; ".join(baseline_report["errors"][:8]))
    require(metadata["schemaVersion"] == 3, "Contrato de atlas incorrecto")
    require(len(metadata["clips"]) == 53 and len(metadata["frames"]) == 4544, "La matriz de clips no está completa")
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
        if entry["clip"] in ["Grab", "Choke"]:
            axis = [(neck - pelvis) / 20.0 for neck, pelvis in zip(points["neck"], points["pelvis"])]
            forward = [axis[2], 0.0, -axis[0]]
            require(abs(math.dist(points["neck"], points["pelvis"]) - 20.0) < 1e-8, "El torso CQC cambia de longitud")
            for name, local in [("head", (0.0, 0.0, 27.0)), ("backpack", (-3.0, 0.0, 11.0)),
                                ("left_shoulder", (-3.0, 6.5, 14.0)), ("right_shoulder", (-3.0, -6.5, 14.0))]:
                expected = [points["pelvis"][index] + forward[index] * local[0] + axis[index] * local[2] + (local[1] if index == 1 else 0.0) for index in range(3)]
                require(math.dist(points[name], expected) < 1e-8, "Un socket CQC no comparte la transformación del torso: " + name)
            squeeze = [0.0, 0.5, 1.1, 1.5, 2.1, 1.8, 1.0, 0.35][entry["phase"]] if entry["clip"] == "Choke" else math.sin(entry["phase"] / 8.0 * math.tau) * 0.35
            for side, sign in [("left", 1.0), ("right", -1.0)]:
                require(math.dist(points[side + "_ankle"], (0.0, sign * 5.5, 2.0)) < 1e-8, "CQC desplaza un apoyo plantado")
                target = (33.5 if side == "right" else 33.0, sign * (4.5 - squeeze), 52.0 if side == "right" else 51.5)
                require(math.dist(points[side + "_wrist"], target) < 1e-8, "CQC pierde el contacto al resolver el hombro")
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
    previous = json.loads((root / "Manifests/pre-recovery-original-raster.json").read_text(encoding="utf-8"))
    # El manifiesto histórico se conserva; el baseline completo protege RGBA por índice aunque cambie el packing.
    recovery = []
    for stance in ["Standing", "Crouched", "Prone"]:
        for direction in metadata["directions"]:
            sequence = sorted(clips[("ReviveRise", stance, direction)], key=lambda entry: entry["phase"])
            require(sequence[0]["pixelSha256"] == clips[("Unconscious", "Standing", direction)][0]["pixelSha256"], "La recuperación salta al comenzar")
            destination = "ProneIdle" if stance == "Prone" else "CrouchIdle" if stance == "Crouched" else "Idle"
            require(sequence[-1]["pixelSha256"] == clips[(destination, "Standing", direction)][0]["pixelSha256"], "La recuperación no alcanza su postura exacta")
            maximum = max(point[2] for frame in sequence for point in frame["joints"].values()) * 46.0 / 72.0
            limit = 14.0 if stance == "Prone" else 34.0 if stance == "Crouched" else 46.0
            require(maximum <= limit, "La recuperación excede la altura física de su postura")
            require(len({frame["pixelSha256"] for frame in sequence}) == 8, "La recuperación no tiene ocho fases propias")
            recovery.append({"stance": stance, "direction": direction, "maximumJointHeight": maximum, "physicalHeight": limit, "exactEndpoints": True})
    registration = json.loads((root / "Manifests/rig-pieces.registration.json").read_text(encoding="utf-8"))
    for entry in registration["sources"]:
        require(hashlib.sha256(Path(entry["file"]).read_bytes()).hexdigest() == entry["sha256"], "Cambió una fuente original")
    require(len(runtime["weapons"]["frames"]) == 48, "Faltan variantes de armas")
    report = {"status": "PASS", "checks": checks, "clips": len(metadata["clips"]), "stanceVariants": 71, "frames": len(metadata["frames"]),
              "directions": 8, "phases": 8, "layers": 3, "pairedCqcLayers": 2, "packedPages": len(pages), "weapons": 48, "maximumBoneLengthError": maximum_error,
              "phaseMatrix": phase_report, "recovery": recovery, "historicalPreviousFrames": len(previous["frames"]), "historicalPreviousPages": len(previous["pages"]),
              "protectedBaselineFrames": baseline_report["frames"] - len(baseline_report["exactExceptionIndices"]),
              "baselineRgbaLayers": baseline_report["layers"], "exactExceptionIndices": baseline_report["exactExceptionIndices"],
              "baselineCanonicalSha256": baseline_report["baselineCanonicalSha256"], "toeWitnessCanonicalSha256": baseline_report["toeWitnessCanonicalSha256"],
              "deathWitnessCanonicalSha256": baseline_report["deathWitnessCanonicalSha256"],
              "crouchPushWitnessCanonicalSha256": baseline_report["crouchPushWitnessCanonicalSha256"],
              "ikFloorWitnessCanonicalSha256": baseline_report["ikFloorWitnessCanonicalSha256"], "recoveryWitnessCanonicalSha256": baseline_report["recoveryWitnessCanonicalSha256"],
              "scope": "Offline raster, authored source hashes, atlas bounds, exact IK lengths and real phase coverage; runtime GUI pending"}
    (report_path or root / "Atlas/CharacterAtlasValidation.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps({key: value for key, value in report.items() if key != "phaseMatrix"}))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Valida fuentes, anatomía y raster empaquetado sin ventana ni simulación")
    parser.add_argument("--root", type=Path, default=Path("Art/NoTrace/Characters"))
    parser.add_argument("--baseline", type=Path)
    parser.add_argument("--references", type=Path)
    parser.add_argument("--exceptions", type=Path)
    parser.add_argument("--toe-witness", type=Path)
    parser.add_argument("--death-witness", type=Path)
    parser.add_argument("--crouch-push-witness", type=Path)
    parser.add_argument("--ik-floor-witness", type=Path)
    parser.add_argument("--recovery-witness", type=Path)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--expected-contract-sha256")
    args = parser.parse_args()
    validate(args.root, args.baseline, args.references, args.exceptions, args.toe_witness, args.report, death_witness=args.death_witness, crouch_push_witness=args.crouch_push_witness, ik_floor_witness=args.ik_floor_witness, recovery_witness=args.recovery_witness, expected_contract_sha256=args.expected_contract_sha256)
