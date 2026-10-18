import argparse
import copy
import hashlib
import json
import math
from pathlib import Path, PurePosixPath

from PIL import Image


REPOSITORY = Path(__file__).resolve().parents[1]
DEFAULT_ROOT = REPOSITORY / "Art/NoTrace/Characters"
BASELINE_NAME = "pre-urgent-character-raster-20261018.json"
REFERENCES_NAME = "pre-urgent-character-canonical-references-20261018.json"
TOE_WITNESS_NAME = "pre-urgent-posture-toes-witness-20261018.json"
DEATH_WITNESS_NAME = "pre-urgent-death-child-offset-witness-20261018.json"
CROUCH_PUSH_WITNESS_NAME = "pre-urgent-crouch-push-child-offset-witness-20261018.json"
IK_FLOOR_WITNESS_NAME = "pre-urgent-ik-floor-witness-20261018.json"
RECOVERY_WITNESS_NAME = "pre-urgent-recovery-prone-witness-20261018.json"
IK_FLOOR_DESCRIPTOR_SHA256 = "eb22d6a6fac70ae7a6f19bffd896e4cb56fb962366692de8cd10f645e7f8f781"
RECOVERY_DESCRIPTOR_SHA256 = "c30c705c28e988adbcc598979cad19634f93703e69818dbfd47ded8ae203db51"
REGISTRATION_FILES = ("Manifests/rig-pieces.registration.json", "Manifests/weapons.registration.json", "Manifests/pre-recovery-original-raster.json")
CANONICAL_JSON = "UTF-8; sort_keys=True; separators=(',', ':'); ensure_ascii=False; allow_nan=False"
PACKING_FIELDS = {"art", "page", "rect", "pixelSha256"}
ANCHOR_FIELDS = ("groundAnchor", "pelvis", "muzzle", "grip", "carryGrip", "supportWidth")
IDENTITY_FIELDS = ("index", "clip", "stance", "direction", "phase")
TOE_JOINTS = ("left_toe", "right_toe")
TOE_IDENTITIES = {
    512 + direction_index * 64 + first + phase: {"clip": clip, "stance": "Standing", "direction": direction, "phase": phase}
    for direction_index, direction in enumerate(["S", "SE", "E", "NE", "N", "NW", "W", "SW"])
    for clip, first, phases in [("LieDown", 0, range(3, 7)), ("StandUp", 24, range(1, 7)), ("Dive", 32, [1])]
    for phase in phases
}
DEATH_JOINTS = ("right_toe",)
DEATH_IDENTITIES = {
    4608 + direction_index * 64 + phase: {"clip": "Death", "stance": "Standing", "direction": direction, "phase": phase}
    for direction_index, direction in enumerate(["S", "SE", "E", "NE", "N", "NW", "W", "SW"])
    for phase in range(1, 8)
}
CROUCH_PUSH_IDENTITIES = {
    first + direction_index * 64 + phase: {"clip": clip, "stance": "Standing", "direction": direction, "phase": phase}
    for direction_index, direction in enumerate(["S", "SE", "E", "NE", "N", "NW", "W", "SW"])
    for clip, first in [("CrouchRun", 40), ("Push", 3584)]
    for phase in range(8)
}
CROUCH_PUSH_JOINTS = {index: TOE_JOINTS if identity["clip"] == "CrouchRun" else ("left_toe",) for index, identity in CROUCH_PUSH_IDENTITIES.items()}


def witness_joint_contract(allowed_joints):
    return {str(index): list(joints) for index, joints in allowed_joints.items()} if isinstance(allowed_joints, dict) else list(allowed_joints)


def witness_joint_names(allowed_joints, index):
    return allowed_joints[index] if isinstance(allowed_joints, dict) else allowed_joints


def require(condition, message):
    if not condition:
        raise ValueError(message)


def canonical_json(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False, allow_nan=False)


def canonical_sha(value):
    return hashlib.sha256(canonical_json(value).encode("utf-8")).hexdigest()


def valid_sha(value):
    return isinstance(value, str) and len(value) == 64 and all(character in "0123456789abcdef" for character in value)


def semantic_record(entry):
    return {key: value for key, value in entry.items() if key not in PACKING_FIELDS}


def file_sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read_json(path):
    def unique_object(pairs):
        result = {}
        for key, value in pairs:
            require(key not in result, "Clave JSON duplicada: " + key + " en " + str(path))
            result[key] = value
        return result

    def invalid_constant(value):
        raise ValueError("Constante JSON inválida: " + value + " en " + str(path))

    return json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=unique_object, parse_constant=invalid_constant)


def registration_contracts(root):
    return [{"file": name, "canonicalSha256": canonical_sha(read_json(root / name))} for name in REGISTRATION_FILES]


def create_references(root, baseline):
    original = {entry["file"]: entry["sha256"] for entry in baseline["sourceRegistrations"]}
    require(len(original) == len(baseline["sourceRegistrations"]) == 3 and set(original) == set(REGISTRATION_FILES), "El baseline no contiene los tres contratos originales")
    # Esta creación explícita exige los bytes originales antes de fijar contratos portables.
    for name in REGISTRATION_FILES:
        require(file_sha(root / name) == original[name], "SHA original distinto antes de crear referencias: " + name)
    return {"schemaVersion": 1, "baselineCanonicalSha256": canonical_sha(baseline), "canonicalJson": CANONICAL_JSON, "sourceRegistrations": registration_contracts(root)}


def validate_references(baseline, references):
    require(set(references) == {"schemaVersion", "baselineCanonicalSha256", "canonicalJson", "sourceRegistrations"}, "Campos del sidecar canónico incorrectos")
    require(references["schemaVersion"] == 1 and references["canonicalJson"] == CANONICAL_JSON, "Contrato canónico incorrecto")
    require(references["baselineCanonicalSha256"] == canonical_sha(baseline), "Sidecar ligado a otro baseline")
    contracts = references["sourceRegistrations"]
    names = [entry["file"] for entry in contracts]
    require(len(names) == 3 and set(names) == set(REGISTRATION_FILES), "El sidecar debe contener los tres contratos originales")
    for entry in contracts:
        require(set(entry) == {"file", "canonicalSha256"} and len(entry["canonicalSha256"]) == 64 and all(character in "0123456789abcdef" for character in entry["canonicalSha256"]), "Hash canónico inválido: " + entry["file"])


def source_records(root):
    records = {}

    def verify(path, expected):
        path = path.resolve()
        actual = file_sha(path)
        require(actual == expected, "SHA original distinto: " + str(path))
        name = path.relative_to(REPOSITORY).as_posix()
        require(name not in records or records[name] == actual, "Fuente registrada con dos SHA: " + name)
        records[name] = actual

    for entry in read_json(root / "Manifests/rig-pieces.registration.json")["sources"]:
        verify(REPOSITORY / entry["file"], entry["sha256"])
    for entry in read_json(root / "Manifests/weapons.registration.json")["weapons"]:
        verify(REPOSITORY / entry["source"], entry["sourceSha256"])
        verify(root / "Pieces" / entry["file"], entry["sha256"])
    return [{"file": name, "sha256": value} for name, value in sorted(records.items())]


def reconstruct_layer(pages, region, size, index, layer):
    label = "índice " + str(index) + ", capa " + layer
    page_index = region["page"]
    require(type(page_index) is int and 0 <= page_index < len(pages), "Página inválida: " + label)
    page = pages[page_index]
    rect, offset = region["rect"], region["offset"]
    require(len(rect) == 4 and len(offset) == 2 and all(type(value) is int for value in rect + offset), "Región inválida: " + label)
    x, y, width, height = rect
    left, top = offset
    require(0 <= x < x + width <= page.width and 0 <= y < y + height <= page.height, "Recorte fuera del atlas: " + label)
    require(0 <= left < left + width <= size and 0 <= top < top + height <= size, "Recorte fuera de celda: " + label)
    canvas = Image.new("RGBA", (size, size))
    # Copiar sin máscara conserva los cuatro canales, incluidos RGB transparentes.
    canvas.paste(page.crop((x, y, x + width, y + height)), (left, top))
    return hashlib.sha256(canvas.tobytes()).hexdigest()


def frame_record(entry, pages, size):
    semantic = semantic_record(entry)
    layers = {name: reconstruct_layer(pages, region, size, entry["index"], name) for name, region in sorted(entry["art"].items())}
    require(layers.get("full") == entry["pixelSha256"], "RGBA real y pixelSha256 difieren en índice " + str(entry["index"]))
    return {
        **{key: entry[key] for key in IDENTITY_FIELDS},
        "anchors": {key: entry[key] for key in ANCHOR_FIELDS},
        "semanticSha256": canonical_sha(semantic),
        "jointsSha256": canonical_sha(entry["joints"]),
        "rgbaSha256": layers,
    }


def snapshot(root, provenance, semantic_records=None):
    metadata_path = root / "Atlas/CharacterAtlasMetadata.json"
    runtime_path = root / "Atlas/CharacterAtlasRuntime.json"
    metadata, runtime = read_json(metadata_path), read_json(runtime_path)
    require(metadata["schemaVersion"] == 3 and metadata["cellSize"] == 192, "Contrato de atlas incorrecto")
    require(len(metadata["frames"]) == 4544 and len(metadata["clips"]) == 53, "La matriz completa debe contener 4544 poses y 53 clips")
    require(all(type(entry["index"]) is int for entry in runtime["frames"]), "Índice runtime inválido: se exige entero JSON")
    runtime_frames = {entry["index"]: entry for entry in runtime["frames"]}
    require(len(runtime_frames) == 4544 and len(runtime["frames"]) == 4544, "Índices runtime incompletos o duplicados")
    for key, value in runtime.items():
        if key != "frames":
            require(canonical_json(metadata.get(key)) == canonical_json(value), "Cabeceras metadata/runtime distintas: " + key)
    pages, page_records = [], []
    for entry in metadata["pages"]:
        path = root / "Atlas/Packed" / entry["file"]
        actual = file_sha(path)
        require(actual == entry["sha256"], "SHA de página incorrecto: " + str(path))
        with Image.open(path) as image:
            page = image.convert("RGBA")
        require(list(page.size) == entry["size"] and max(page.size) <= 2048, "Dimensiones de página incorrectas")
        pages.append(page)
        page_records.append({"file": entry["file"], "sha256": actual})
    frames, seen = [], set()
    for entry in sorted(metadata["frames"], key=lambda frame: frame["index"]):
        index = entry["index"]
        require(type(index) is int and index not in seen, "Índice metadata duplicado o inválido: " + str(index))
        seen.add(index)
        require(index in runtime_frames, "Falta índice runtime " + str(index))
        projected = {key: entry[key] for key in runtime_frames[index]}
        require(canonical_json(projected) == canonical_json(runtime_frames[index]), "Metadata/runtime difieren en índice " + str(index))
        frames.append(frame_record(entry, pages, metadata["cellSize"]))
        if semantic_records is not None:
            semantic_records[index] = semantic_record(entry)
    require(seen == set(runtime_frames), "Los conjuntos de índices metadata/runtime difieren")
    sources = source_records(root)
    header = {key: value for key, value in metadata.items() if key not in {"frames", "pages", "authoringPages", "status"}}
    return {
        "author": "Carlos Cortina", "organization": "CIO", "date": "2026-10-18", "schemaVersion": 1,
        "provenance": {"commit": provenance, "metadataSha256": file_sha(metadata_path), "runtimeSha256": file_sha(runtime_path), "pages": page_records},
        "definition": {
            "cellSize": 192, "channels": "RGBA", "reconstruction": "Crop packed rect and paste all four channels without a mask at offset into a transparent 192x192 canvas; SHA256 of row-major bytes",
            "canonicalJson": CANONICAL_JSON,
            "semanticExcludedFields": sorted(PACKING_FIELDS), "packingCoordinatesFrozen": False,
        },
        "headerSha256": canonical_sha(header),
        "sourceRegistrations": [{"file": name, "sha256": file_sha(root / name)} for name in REGISTRATION_FILES],
        "originalSources": sources,
        "weaponFiles": [{"file": "Atlas/" + name, "sha256": file_sha(root / "Atlas" / name)} for name in ["WeaponProps.png", "WeaponSelection.png"]],
        "frames": frames,
    }


def create_joint_witness(baseline, original_metadata_path, identities, allowed_joints, allowed_fields=()):
    require(file_sha(original_metadata_path) == baseline["provenance"]["metadataSha256"], "El testigo exige bytes de metadata originales")
    metadata = read_json(original_metadata_path)
    before = {entry["index"]: entry for entry in baseline["frames"]}
    original = {entry["index"]: entry for entry in metadata["frames"]}
    require(len(before) == len(original) == len(metadata["frames"]) == 4544 and set(before) == set(original), "La metadata original no contiene la matriz exacta")
    for index, entry in original.items():
        require(canonical_sha(semantic_record(entry)) == before[index]["semanticSha256"], "Semántica original distinta: " + str(index))
        require(canonical_sha(entry["joints"]) == before[index]["jointsSha256"], "Joints originales distintos: " + str(index))
    witness = {
        "schemaVersion": 1, "baselineCanonicalSha256": canonical_sha(baseline),
        "originalMetadataSha256": baseline["provenance"]["metadataSha256"], "allowedJoints": witness_joint_contract(allowed_joints),
        "frames": [{"index": index, "semantic": semantic_record(original[index]),
                    "semanticSha256": before[index]["semanticSha256"], "jointsSha256": before[index]["jointsSha256"]}
                   for index in sorted(identities)],
    }
    if allowed_fields:
        witness["allowedFields"] = list(allowed_fields)
    validate_joint_witness(baseline, witness, identities, allowed_joints, allowed_fields)
    return witness


def create_toe_witness(baseline, original_metadata_path):
    return create_joint_witness(baseline, original_metadata_path, TOE_IDENTITIES, TOE_JOINTS)


def create_death_witness(baseline, original_metadata_path):
    return create_joint_witness(baseline, original_metadata_path, DEATH_IDENTITIES, DEATH_JOINTS)


def create_crouch_push_witness(baseline, original_metadata_path):
    return create_joint_witness(baseline, original_metadata_path, CROUCH_PUSH_IDENTITIES, CROUCH_PUSH_JOINTS)


def validate_joint_witness(baseline, witness, identities, allowed_joints, allowed_fields=()):
    fields = {"schemaVersion", "baselineCanonicalSha256", "originalMetadataSha256", "allowedJoints", "frames"}
    if allowed_fields:
        fields.add("allowedFields")
    require(isinstance(witness, dict) and set(witness) == fields, "Campos del testigo de pies incorrectos")
    if allowed_fields:
        require(witness["allowedFields"] == list(allowed_fields), "El testigo cambia los campos autorizados")
    require(type(witness["schemaVersion"]) is int and witness["schemaVersion"] == 1, "Versión del testigo incorrecta")
    require(witness["baselineCanonicalSha256"] == canonical_sha(baseline), "Testigo ligado a otro baseline")
    require(witness["originalMetadataSha256"] == baseline["provenance"]["metadataSha256"], "SHA de metadata del testigo distinto")
    require(witness["allowedJoints"] == witness_joint_contract(allowed_joints), "El testigo cambia los joints autorizados")
    require(isinstance(witness["frames"], list) and len(witness["frames"]) == len(identities), "El testigo debe contener " + str(len(identities)) + " registros")
    before = {entry["index"]: entry for entry in baseline["frames"]}
    records = {}
    for row in witness["frames"]:
        require(isinstance(row, dict) and set(row) == {"index", "semantic", "semanticSha256", "jointsSha256"}, "Registro del testigo incorrecto")
        index = row["index"]
        require(type(index) is int and index in identities and index in before and index not in records, "Índice del testigo inválido o duplicado")
        semantic = row["semantic"]
        require(isinstance(semantic, dict) and isinstance(semantic.get("joints"), dict), "Semántica del testigo inválida")
        require(canonical_sha({key: semantic.get(key) for key in identities[index]}) == canonical_sha(identities[index]) and type(semantic.get("index")) is int and semantic["index"] == index, "Identidad del testigo distinta")
        require(all(joint in semantic["joints"] for joint in witness_joint_names(allowed_joints, index)), "Faltan puntas originales")
        require(row["semanticSha256"] == before[index]["semanticSha256"] == canonical_sha(semantic), "Semántica del testigo no coincide con baseline")
        require(row["jointsSha256"] == before[index]["jointsSha256"] == canonical_sha(semantic["joints"]), "Joints del testigo no coinciden con baseline")
        records[index] = semantic
    require(set(records) == set(identities), "El testigo cambia el conjunto de " + str(len(identities)) + " índices")
    return records


def validate_toe_witness(baseline, witness):
    return validate_joint_witness(baseline, witness, TOE_IDENTITIES, TOE_JOINTS)


def validate_death_witness(baseline, witness):
    return validate_joint_witness(baseline, witness, DEATH_IDENTITIES, DEATH_JOINTS)


def validate_crouch_push_witness(baseline, witness):
    return validate_joint_witness(baseline, witness, CROUCH_PUSH_IDENTITIES, CROUCH_PUSH_JOINTS)


def descriptor_contract(descriptor, expected_sha, count):
    require(isinstance(descriptor, dict) and len(descriptor) == count, "Número de identidades del descriptor incorrecto")
    identities, joints = {}, {}
    for key, row in descriptor.items():
        require(isinstance(key, str) and key.isdecimal() and str(int(key)) == key, "Índice literal del descriptor inválido")
        require(isinstance(row, dict) and set(row) == {"identity", "allowedJoints"}, "Campos del descriptor incorrectos")
        require(isinstance(row["identity"], dict) and set(row["identity"]) == {"clip", "stance", "direction", "phase"}, "Identidad del descriptor incompleta")
        names = row["allowedJoints"]
        require(isinstance(names, list) and names and all(isinstance(name, str) for name in names) and len(names) == len(set(names)), "Joints literales del descriptor inválidos")
        identities[int(key)], joints[int(key)] = row["identity"], tuple(names)
    require(canonical_sha(descriptor) == expected_sha, "SHA del descriptor literal distinto")
    return identities, joints


def witness_descriptor(witness):
    require(isinstance(witness, dict) and isinstance(witness.get("frames"), list) and isinstance(witness.get("allowedJoints"), dict), "El testigo literal requiere filas y mapa de joints")
    descriptor = {}
    for row in witness["frames"]:
        require(isinstance(row, dict) and type(row.get("index")) is int and isinstance(row.get("semantic"), dict), "Fila del testigo literal inválida")
        key = str(row["index"])
        require(key not in descriptor, "Índice duplicado en testigo literal")
        descriptor[key] = {"identity": {name: row["semantic"].get(name) for name in ["clip", "stance", "direction", "phase"]},
                           "allowedJoints": copy.deepcopy(witness["allowedJoints"].get(key))}
    require(set(witness["allowedJoints"]) == set(descriptor), "El mapa de joints cambia el conjunto literal de índices")
    return descriptor


def create_ik_floor_witness(baseline, original_metadata_path, descriptor):
    identities, joints = descriptor_contract(descriptor, IK_FLOOR_DESCRIPTOR_SHA256, 216)
    return create_joint_witness(baseline, original_metadata_path, identities, joints)


def create_recovery_witness(baseline, original_metadata_path, descriptor):
    identities, joints = descriptor_contract(descriptor, RECOVERY_DESCRIPTOR_SHA256, 48)
    return create_joint_witness(baseline, original_metadata_path, identities, joints, ("carryGrip",))


def semantic_spec(kind, witness):
    if kind == "posture_toes_v1":
        return TOE_IDENTITIES, TOE_JOINTS, ()
    if kind == "death_child_offset_v1":
        return DEATH_IDENTITIES, DEATH_JOINTS, ()
    if kind == "crouch_push_child_offset_v1":
        return CROUCH_PUSH_IDENTITIES, CROUCH_PUSH_JOINTS, ()
    if kind == "ik_floor_v1":
        identities, joints = descriptor_contract(witness_descriptor(witness), IK_FLOOR_DESCRIPTOR_SHA256, 216)
        return identities, joints, ()
    if kind == "recovery_prone_frame_v1":
        identities, joints = descriptor_contract(witness_descriptor(witness), RECOVERY_DESCRIPTOR_SHA256, 48)
        return identities, joints, ("carryGrip",)
    raise ValueError("Semántica permitida sólo en los 88 índices de pies y los contratos literales aprobados")


def semantic_contract_ids(semantic):
    fields = {"kind", "expectedSemanticSha256", "expectedJointsSha256"}
    require(isinstance(semantic, dict), "Excepción semántica incorrecta")
    if semantic.get("kind") == "joint_contracts_v1":
        require(set(semantic) == fields | {"contracts"}, "Campos de composición semántica incorrectos")
        names = semantic["contracts"]
        require(isinstance(names, list) and names and all(isinstance(name, str) for name in names) and len(names) == len(set(names)), "La composición exige ids literales únicos")
        return names
    require(set(semantic) == fields and isinstance(semantic["kind"], str), "Excepción semántica incorrecta")
    return [semantic["kind"]]


def exception_contract(baseline, current, exceptions, toe_witness=None, semantic_records=None, death_witness=None, crouch_push_witness=None, ik_floor_witness=None, recovery_witness=None):
    if exceptions is None:
        return {}
    fields = {"schemaVersion", "baselineCanonicalSha256", "toeWitnessCanonicalSha256", "entries"}
    witnesses = {"posture_toes_v1": (toe_witness, "toeWitnessCanonicalSha256"),
                 "death_child_offset_v1": (death_witness, "deathWitnessCanonicalSha256"),
                 "crouch_push_child_offset_v1": (crouch_push_witness, "crouchPushWitnessCanonicalSha256"),
                 "ik_floor_v1": (ik_floor_witness, "ikFloorWitnessCanonicalSha256"),
                 "recovery_prone_frame_v1": (recovery_witness, "recoveryWitnessCanonicalSha256")}
    require(isinstance(exceptions, dict) and fields <= set(exceptions) <= fields | {key for _, key in witnesses.values()}, "Campos de excepciones incorrectos")
    require(type(exceptions["schemaVersion"]) is int and exceptions["schemaVersion"] == 1, "Versión de excepciones incorrecta")
    require(exceptions["baselineCanonicalSha256"] == canonical_sha(baseline), "Excepciones ligadas a otro baseline")
    entries = exceptions["entries"]
    require(isinstance(entries, list) and entries, "La lista explícita de excepciones está vacía")
    before = {entry["index"]: entry for entry in baseline["frames"]}
    after = {entry["index"]: entry for entry in current["frames"]}
    require(len(before) == len(baseline["frames"]) and len(after) == len(current["frames"]), "Índices duplicados antes de validar excepciones")
    result, witnessed = {}, {}
    for row in entries:
        require(isinstance(row, dict) and set(row) == {"index", "identity", "reason", "rgbaSha256", "semantic"}, "Entrada de excepción incorrecta")
        require(isinstance(row["reason"], str) and bool(row["reason"].strip()), "El motivo de excepción debe ser texto no vacío")
        index = row["index"]
        require(type(index) is int and index in before and index in after and index not in result, "Índice de excepción inválido o duplicado")
        original, candidate = before[index], after[index]
        identity = {key: original[key] for key in IDENTITY_FIELDS if key != "index"}
        require(isinstance(row["identity"], dict) and canonical_sha(row["identity"]) == canonical_sha(identity), "Identidad de excepción distinta: " + str(index))
        layers = row["rgbaSha256"]
        require(isinstance(layers, dict), "Las capas esperadas deben ser un objeto")
        require(set(layers) <= set(original["rgbaSha256"]) and set(layers) <= set(candidate["rgbaSha256"]), "La excepción inventa una capa")
        for layer, expected in layers.items():
            require(valid_sha(expected), "SHA RGBA esperado inválido")
            require(expected != original["rgbaSha256"][layer], "Excepción RGBA ociosa: " + str(index) + "/" + layer)
            require(candidate["rgbaSha256"][layer] == expected, "RGBA no coincide con expected SHA: " + str(index) + "/" + layer)
        semantic = row["semantic"]
        require(layers or semantic is not None, "Entrada de excepción ociosa")
        if semantic is not None:
            kinds = semantic_contract_ids(semantic)
            require(all(kind in witnesses for kind in kinds), "Semántica permitida sólo en los 88 índices de pies y los contratos literales aprobados")
            scopes = {}
            for kind in kinds:
                witness = witnesses[kind][0]
                if witness is not None or kind in ["posture_toes_v1", "death_child_offset_v1", "crouch_push_child_offset_v1"]:
                    scopes[kind] = semantic_spec(kind, witness)
                    identities = scopes[kind][0]
                    require(index in identities, "Semántica permitida sólo en los 88 índices de pies y los contratos literales según kind")
                    require(canonical_sha(identity) == canonical_sha(identities[index]), "Identidad semántica de pies distinta")
            require(all(valid_sha(semantic[key]) for key in ["expectedSemanticSha256", "expectedJointsSha256"]), "SHA semántico esperado inválido")
            require(semantic["expectedSemanticSha256"] != original["semanticSha256"] and semantic["expectedJointsSha256"] != original["jointsSha256"], "Excepción semántica ociosa")
            require(semantic_records is not None and index in semantic_records, "Falta testigo o semántica real para pies")
            actual = semantic_records[index]
            require(isinstance(actual, dict) and isinstance(actual.get("joints"), dict), "Semántica actual inválida")
            permitted_joints, permitted_fields, expected = set(), set(), None
            for kind in kinds:
                witness, witness_key = witnesses[kind]
                require(witness is not None, "Falta testigo o semántica real para pies")
                require(valid_sha(exceptions.get(witness_key)) and exceptions[witness_key] == canonical_sha(witness), "SHA del testigo semántico distinto")
                identities, joints, extra_fields = scopes[kind]
                if kind not in witnessed:
                    witnessed[kind] = validate_joint_witness(baseline, witness, identities, joints, extra_fields)
                original_semantic = witnessed[kind][index]
                names = witness_joint_names(joints, index)
                require(any(canonical_json(actual["joints"].get(name)) != canonical_json(original_semantic["joints"][name]) for name in names), "Contrato semántico ocioso en índice " + str(index))
                permitted_joints.update(names)
                permitted_fields.update(extra_fields)
                expected = copy.deepcopy(original_semantic) if expected is None else expected
            for joint in permitted_joints:
                point = actual["joints"].get(joint)
                require(isinstance(point, (list, tuple)) and len(point) == 3 and all(type(value) in [int, float] and math.isfinite(value) for value in point), "Punta no finita o inválida")
                expected["joints"][joint] = point
            if "carryGrip" in permitted_fields:
                from GenerateNoTraceCharactersRig import project
                expected["carryGrip"] = list(project(actual["joints"]["right_wrist"], identity["direction"]))
                require(canonical_json(actual.get("carryGrip")) == canonical_json(expected["carryGrip"]), "carryGrip debe ser la proyección exacta de right_wrist")
            require(canonical_sha(actual) == canonical_sha(expected), "La excepción cambia campos o joints ajenos a las puntas: " + str(index))
            require(canonical_sha(actual) == candidate["semanticSha256"] == semantic["expectedSemanticSha256"], "Semántica no coincide con expected SHA: " + str(index))
            require(canonical_sha(actual["joints"]) == candidate["jointsSha256"] == semantic["expectedJointsSha256"], "Joints no coinciden con expected SHA: " + str(index))
            if "carryGrip" in permitted_fields:
                row = {**row, "approvedCarryGrip": expected["carryGrip"]}
        result[index] = row
    for kind, (_, witness_key) in witnesses.items():
        require(kind in witnessed or exceptions.get(witness_key) is None, "Testigo " + ("Death " if kind == "death_child_offset_v1" else "") + "declarado sin excepciones semánticas")
    return result


def socket_source_contract(exceptions, expected_contract_sha256):
    require(valid_sha(expected_contract_sha256), "Schema2 exige expected_contract_sha256 aprobado externamente")
    require(canonical_sha(exceptions) == expected_contract_sha256, "SHA externo del preregistro distinto")
    fields = {"schemaVersion", "kind", "baselineCanonicalSha256", "canonicalManifest", "tools", "pieceFiles", "entries"}
    require(set(exceptions) == fields and type(exceptions["schemaVersion"]) is int and exceptions["schemaVersion"] == 2, "Campos schema2 incorrectos")
    require(exceptions["kind"] == "rig_socket_projection_v1", "Kind schema2 incorrecto")

    def record(value, prefix, hash_field):
        require(isinstance(value, dict) and set(value) == {"file", hash_field}, "Registro de fuente schema2 incorrecto")
        name = value["file"]
        require(isinstance(name, str) and bool(name) and "\\" not in name and ":" not in name, "Ruta de fuente schema2 inválida")
        path = PurePosixPath(name)
        require(not path.is_absolute() and ".." not in path.parts and name == path.as_posix() and name.startswith(prefix), "Ruta de fuente schema2 fuera de alcance")
        require(valid_sha(value[hash_field]), "SHA de fuente schema2 inválido")
        return value

    manifest = record(exceptions["canonicalManifest"], "Manifests/", "canonicalSha256")
    result = {"canonicalManifest": manifest}
    for key, prefix in [("tools", "Tools/"), ("pieceFiles", "Pieces/")]:
        values = exceptions[key]
        require(isinstance(values, list) and bool(values), "Lista de fuentes schema2 vacía: " + key)
        records = [record(value, prefix, "sha256") for value in values]
        names = [value["file"] for value in records]
        require(len(names) == len(set(names)), "Fuentes schema2 duplicadas: " + key)
        result[key] = records
    return result


def verify_socket_sources(root, exceptions, expected_contract_sha256):
    declared = socket_source_contract(exceptions, expected_contract_sha256)

    def verified(base, record, canonical=False):
        base = base.resolve()
        path = (base / record["file"]).resolve()
        require(path.is_relative_to(base), "Fuente schema2 resuelta fuera de su raíz")
        key = "canonicalSha256" if canonical else "sha256"
        return {"file": record["file"], key: canonical_sha(read_json(path)) if canonical else file_sha(path)}

    return {"canonicalManifest": verified(root, declared["canonicalManifest"], True),
            "tools": [verified(REPOSITORY, row) for row in declared["tools"]],
            "pieceFiles": [verified(root, row) for row in declared["pieceFiles"]]}


def socket_projection_contract(baseline, current, exceptions, semantic_records, expected_contract_sha256, verified_source_hashes):
    declared = socket_source_contract(exceptions, expected_contract_sha256)
    require(exceptions["baselineCanonicalSha256"] == canonical_sha(baseline), "Schema2 ligado a otro baseline")
    require(verified_source_hashes is not None and canonical_json(declared) == canonical_json(verified_source_hashes), "Fuentes, herramientas o manifiesto canónico schema2 distintos")
    entries = exceptions["entries"]
    require(isinstance(entries, list) and bool(entries), "Schema2 exige filas literales no vacías")
    before = {row["index"]: row for row in baseline["frames"]}
    after = {row["index"]: row for row in current["frames"]}
    result, previous = {}, -1
    fields = {"index", "identity", "reason", "rgbaSha256", "joints", "anchors", "semantic", "semanticSha256", "jointsSha256"}
    for row in entries:
        require(isinstance(row, dict) and set(row) == fields, "Entrada schema2 incorrecta")
        index = row["index"]
        require(type(index) is int and index >= 0 and index > previous and index in before and index in after, "Índice schema2 inválido, duplicado o desordenado")
        previous = index
        original, actual = before[index], after[index]
        identity = {key: original[key] for key in IDENTITY_FIELDS if key != "index"}
        require(isinstance(row["identity"], dict) and canonical_json(row["identity"]) == canonical_json(identity), "Identidad schema2 distinta")
        require(isinstance(row["reason"], str) and bool(row["reason"].strip()), "Motivo schema2 vacío")
        layers = row["rgbaSha256"]
        require(isinstance(layers, dict) and set(layers) == set(original["rgbaSha256"]) == set(actual["rgbaSha256"]), "Schema2 debe declarar todas las capas originales exactas")
        require(all(valid_sha(value) for value in layers.values()) and canonical_json(layers) == canonical_json(actual["rgbaSha256"]), "RGBA schema2 distinto del preregistro")
        joints, anchors, semantic = row["joints"], row["anchors"], row["semantic"]
        require(isinstance(joints, dict) and bool(joints), "Joints schema2 incompletos")
        require(all(isinstance(name, str) and bool(name) and isinstance(point, list) and len(point) == 3 and all(type(value) in [int, float] and math.isfinite(value) for value in point) for name, point in joints.items()), "Joint schema2 inválido o no finito")
        require(isinstance(anchors, dict) and set(anchors) == set(ANCHOR_FIELDS), "Anclas schema2 incompletas")
        for name, point in anchors.items():
            values = [point] if name == "supportWidth" else point
            require(isinstance(values, list) and len(values) == (1 if name == "supportWidth" else 2) and all(type(value) in [int, float] and math.isfinite(value) for value in values), "Ancla schema2 inválida o no finita")
        require(canonical_json(anchors["groundAnchor"]) == canonical_json(original["anchors"]["groundAnchor"]), "Schema2 no modifica el ancla de suelo")
        require(isinstance(semantic, dict) and not (set(semantic) & PACKING_FIELDS), "Semántica schema2 incluye packing o es inválida")
        require(all(key in semantic for key in (*IDENTITY_FIELDS, *ANCHOR_FIELDS, "joints")), "Semántica schema2 incompleta")
        require(type(semantic["index"]) is int and semantic["index"] == index and canonical_json({key: semantic[key] for key in identity}) == canonical_json(identity), "Identidad semántica schema2 distinta")
        require(canonical_json(semantic["joints"]) == canonical_json(joints) and canonical_json({key: semantic[key] for key in ANCHOR_FIELDS}) == canonical_json(anchors), "Joints o anclas schema2 no coinciden con semántica")
        require(valid_sha(row["semanticSha256"]) and valid_sha(row["jointsSha256"]) and canonical_sha(semantic) == row["semanticSha256"] and canonical_sha(joints) == row["jointsSha256"], "Hashes semánticos schema2 incorrectos")
        require(semantic_records is not None and index in semantic_records and canonical_json(semantic_records[index]) == canonical_json(semantic), "Semántica real schema2 distinta del preregistro")
        require(actual["semanticSha256"] == row["semanticSha256"] and actual["jointsSha256"] == row["jointsSha256"] and canonical_json(actual["anchors"]) == canonical_json(anchors), "Registro actual schema2 distinto del preregistro")
        result[index] = {**row, "approvedAnchors": anchors}
    return result


def compare(baseline, current, references=None, contracts=None, exceptions=None, toe_witness=None, semantic_records=None, death_witness=None, crouch_push_witness=None, ik_floor_witness=None, recovery_witness=None, expected_contract_sha256=None, verified_source_hashes=None):
    errors = []
    for label, document in [("baseline", baseline), ("actual", current)]:
        indices = [frame["index"] for frame in document["frames"]]
        require(all(type(index) is int for index in indices) and len(indices) == len(set(indices)), "Índices inválidos o duplicados en " + label)
    socket_contract = exceptions is not None and exceptions.get("schemaVersion") == 2
    require(expected_contract_sha256 is None or socket_contract, "El pin externo exige un contrato schema2; no admite downgrade")
    if socket_contract:
        approved = socket_projection_contract(baseline, current, exceptions, semantic_records, expected_contract_sha256, verified_source_hashes)
    else:
        approved = exception_contract(baseline, current, exceptions, toe_witness, semantic_records, death_witness, crouch_push_witness, ik_floor_witness, recovery_witness)
    if references is not None:
        validate_references(baseline, references)
        require(contracts is not None, "Faltan los contratos actuales")
        expected = {entry["file"]: entry["canonicalSha256"] for entry in references["sourceRegistrations"]}
        actual = {entry["file"]: entry["canonicalSha256"] for entry in contracts}
        require(len(contracts) == len(actual) == 3 and set(actual) == set(REGISTRATION_FILES), "Contratos actuales incompletos o duplicados")
        for name in REGISTRATION_FILES:
            if actual[name] != expected[name]:
                errors.append("Contrato JSON distinto: " + name)
    elif baseline["sourceRegistrations"] != current["sourceRegistrations"]:
        errors.append("Contrato o fuente distintos: sourceRegistrations")
    for key in ["definition", "headerSha256", "originalSources", "weaponFiles"]:
        if baseline[key] != current[key]:
            errors.append("Contrato o fuente distintos: " + key)
    before = {frame["index"]: frame for frame in baseline["frames"]}
    after = {frame["index"]: frame for frame in current["frames"]}
    if set(before) != set(after):
        errors.append("El conjunto de índices cambió")
    for index in sorted(set(before) & set(after)):
        original, candidate = before[index], after[index]
        exception = approved.get(index)
        for key in sorted(set(original) | set(candidate)):
            if key == "rgbaSha256":
                for layer in sorted(set(original[key]) | set(candidate[key])):
                    if original[key].get(layer) != candidate[key].get(layer):
                        if exception is None or exception["rgbaSha256"].get(layer) != candidate[key].get(layer):
                            errors.append("RGBA distinto: índice " + str(index) + ", capa " + layer)
            elif key == "anchors" and exception is not None and "approvedAnchors" in exception:
                if canonical_json(exception["approvedAnchors"]) != canonical_json(candidate[key]):
                    errors.append("Semántica distinta: índice " + str(index) + ", campo anchors")
            elif key == "anchors" and exception is not None and "approvedCarryGrip" in exception:
                anchors = {**original[key], "carryGrip": exception["approvedCarryGrip"]}
                if canonical_json(anchors) != canonical_json(candidate[key]):
                    errors.append("Semántica distinta: índice " + str(index) + ", campo anchors")
            elif canonical_json(original.get(key)) != canonical_json(candidate.get(key)):
                if exception is None or exception["semantic"] is None or key not in {"semanticSha256", "jointsSha256"}:
                    errors.append("Semántica distinta: índice " + str(index) + ", campo " + key)
    return errors


def validate_current(root, baseline_path=None, references_path=None, exceptions_path=None, toe_witness_path=None, death_witness_path=None, crouch_push_witness_path=None, ik_floor_witness_path=None, recovery_witness_path=None, expected_contract_sha256=None):
    baseline_path = baseline_path or root / "Manifests" / BASELINE_NAME
    references_path = references_path or root / "Manifests" / REFERENCES_NAME
    baseline, references = read_json(baseline_path), read_json(references_path)
    validate_references(baseline, references)
    exceptions = read_json(exceptions_path) if exceptions_path is not None else None
    require(exceptions is None or isinstance(exceptions, dict), "El contrato de excepciones debe ser un objeto")
    kinds = set()
    socket_contract = exceptions is not None and exceptions.get("schemaVersion") == 2
    require(expected_contract_sha256 is None or socket_contract, "El pin externo exige un contrato schema2; no admite downgrade")
    verified_sources = verify_socket_sources(root, exceptions, expected_contract_sha256) if socket_contract else None
    if exceptions is not None and not socket_contract and isinstance(exceptions.get("entries"), list):
        for row in exceptions["entries"]:
            if isinstance(row, dict) and row.get("semantic") is not None:
                kinds.update(semantic_contract_ids(row["semantic"]))
    toe_witness = read_json(toe_witness_path or root / "Manifests" / TOE_WITNESS_NAME) if "posture_toes_v1" in kinds else None
    death_witness = read_json(death_witness_path or root / "Manifests" / DEATH_WITNESS_NAME) if "death_child_offset_v1" in kinds else None
    crouch_push_witness = read_json(crouch_push_witness_path or root / "Manifests" / CROUCH_PUSH_WITNESS_NAME) if "crouch_push_child_offset_v1" in kinds else None
    ik_floor_witness = read_json(ik_floor_witness_path or root / "Manifests" / IK_FLOOR_WITNESS_NAME) if "ik_floor_v1" in kinds else None
    recovery_witness = read_json(recovery_witness_path or root / "Manifests" / RECOVERY_WITNESS_NAME) if "recovery_prone_frame_v1" in kinds else None
    semantics = {} if kinds or socket_contract else None
    current = snapshot(root, baseline["provenance"]["commit"], semantics)
    contracts = registration_contracts(root)
    errors = compare(baseline, current, references, contracts, exceptions, toe_witness, semantics, death_witness, crouch_push_witness, ik_floor_witness, recovery_witness, expected_contract_sha256, verified_sources)
    return {"status": "FAIL" if errors else "PASS", "frames": len(current["frames"]),
            "layers": sum(len(frame["rgbaSha256"]) for frame in current["frames"]),
            "canonicalSourceContracts": len(contracts), "baselineCanonicalSha256": canonical_sha(baseline),
            "exactExceptionIndices": sorted(row["index"] for row in exceptions["entries"]) if exceptions is not None else [],
            "toeWitnessCanonicalSha256": canonical_sha(toe_witness) if toe_witness is not None else None,
            "deathWitnessCanonicalSha256": canonical_sha(death_witness) if death_witness is not None else None,
            "crouchPushWitnessCanonicalSha256": canonical_sha(crouch_push_witness) if crouch_push_witness is not None else None,
            "ikFloorWitnessCanonicalSha256": canonical_sha(ik_floor_witness) if ik_floor_witness is not None else None,
            "recoveryWitnessCanonicalSha256": canonical_sha(recovery_witness) if recovery_witness is not None else None,
            "rigSocketContractCanonicalSha256": expected_contract_sha256 if socket_contract else None, "errors": errors}


def main():
    parser = argparse.ArgumentParser(description="Congela o compara RGBA y semántica por índice sin depender del packing")
    parser.add_argument("mode", choices=["freeze", "freeze-references", "freeze-toe-witness", "freeze-death-witness", "freeze-crouch-push-witness", "freeze-ik-floor-witness", "freeze-recovery-witness", "check"])
    parser.add_argument("--root", type=Path, default=DEFAULT_ROOT)
    parser.add_argument("--baseline", type=Path)
    parser.add_argument("--references", type=Path)
    parser.add_argument("--exceptions", type=Path)
    parser.add_argument("--expected-contract-sha256")
    parser.add_argument("--toe-witness", type=Path)
    parser.add_argument("--death-witness", type=Path)
    parser.add_argument("--crouch-push-witness", type=Path)
    parser.add_argument("--ik-floor-witness", type=Path)
    parser.add_argument("--recovery-witness", type=Path)
    parser.add_argument("--scope", type=Path)
    parser.add_argument("--original-metadata", type=Path)
    parser.add_argument("--provenance", default="1280d50")
    args = parser.parse_args()
    baseline_path = args.baseline or args.root / "Manifests" / BASELINE_NAME
    references_path = args.references or args.root / "Manifests" / REFERENCES_NAME
    if args.mode == "freeze":
        require(not baseline_path.exists(), "El baseline existente es inmutable: " + str(baseline_path))
        current = snapshot(args.root, args.provenance)
        with baseline_path.open("x", encoding="utf-8", newline="\n") as stream:
            json.dump(current, stream, ensure_ascii=False, separators=(",", ":"), allow_nan=False)
            stream.write("\n")
        print(json.dumps({"status": "FROZEN", "baseline": str(baseline_path), "sha256": file_sha(baseline_path), "frames": len(current["frames"]), "layers": sum(len(frame["rgbaSha256"]) for frame in current["frames"]), "originalSources": len(current["originalSources"])}))
        return
    baseline = read_json(baseline_path)
    witness_modes = {"freeze-toe-witness": (args.toe_witness, TOE_WITNESS_NAME, create_toe_witness, False),
                     "freeze-death-witness": (args.death_witness, DEATH_WITNESS_NAME, create_death_witness, False),
                     "freeze-crouch-push-witness": (args.crouch_push_witness, CROUCH_PUSH_WITNESS_NAME, create_crouch_push_witness, False),
                     "freeze-ik-floor-witness": (args.ik_floor_witness, IK_FLOOR_WITNESS_NAME, create_ik_floor_witness, True),
                     "freeze-recovery-witness": (args.recovery_witness, RECOVERY_WITNESS_NAME, create_recovery_witness, True)}
    if args.mode in witness_modes:
        require(args.original_metadata is not None, "Crear el testigo exige --original-metadata explícito")
        override, filename, creator, scoped = witness_modes[args.mode]
        witness_path = override or args.root / "Manifests" / filename
        require(not witness_path.exists(), "El testigo existente es inmutable: " + str(witness_path))
        require(not scoped or args.scope is not None, "Crear el testigo literal exige --scope explícito")
        witness = creator(baseline, args.original_metadata, read_json(args.scope)) if scoped else creator(baseline, args.original_metadata)
        with witness_path.open("x", encoding="utf-8", newline="\n") as stream:
            stream.write(canonical_json(witness) + "\n")
        print(json.dumps({"status": "WITNESS_FROZEN", "mode": args.mode, "witness": str(witness_path), "records": len(witness["frames"]), "canonicalSha256": canonical_sha(witness)}))
        return
    if args.mode == "freeze-references":
        require(not references_path.exists(), "El sidecar existente es inmutable: " + str(references_path))
        references = create_references(args.root, baseline)
        with references_path.open("x", encoding="utf-8", newline="\n") as stream:
            stream.write(canonical_json(references) + "\n")
        print(json.dumps({"status": "REFERENCES_FROZEN", "references": str(references_path), "baselineCanonicalSha256": references["baselineCanonicalSha256"], "referencesCanonicalSha256": canonical_sha(references)}))
        return
    report = validate_current(args.root, baseline_path, references_path, args.exceptions, args.toe_witness, args.death_witness, args.crouch_push_witness, args.ik_floor_witness, args.recovery_witness, args.expected_contract_sha256)
    print(json.dumps(report, ensure_ascii=False))
    require(not report["errors"], "El atlas difiere del baseline inmutable fuera de sus excepciones exactas")


if __name__ == "__main__":
    main()
