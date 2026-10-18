import argparse
import hashlib
import json
import re
from datetime import datetime, timezone
from pathlib import Path

from PIL import Image
from GenerateNoTraceCharactersPack import VERSIONS


DIRECTIONS = ["S", "SE", "E", "NE", "N", "NW", "W", "SW"]
FAMILIES = ["Locomotion", "Posture", "Melee", "StandingWeapons", "Context", "CrouchedWeapons", "ProneWeapons", "Push", "Drag", "Death", "GripVictim", "Suppressor", "WeaponSwitch", "Traversal", "Recovery"]
MISSING = ["DragStart", "DragLoop", "DragStop", "Death", "GripVictimEnter", "GripVictimHold", "GripVictimRelease", "SuppressorAttach", "SuppressorDetach", "WeaponHolster", "WeaponDraw", "TraversalAttach", "TraversalMove", "TraversalRelease"]
REUSE = {"CrouchIdle": "CrouchWalk first pose", "ProneIdle": "LieDown final pose", "StandUp": "LieDown reversed", "Push": "Grab cycle", "WallBang": "Punch cycle", "Damage": "Fall first poses", "Unconscious": "Fall final pose", "HideExit": "HideEnter reversed", "CoverIdle": "CoverEnter hold pose", "CoverMove": "CoverEnter cycle", "CrouchEnter": "LieDown first poses", "CrouchExit": "LieDown first poses reversed"}
OFFLINE_ONLY = {"DragStop", "GripVictimEnter", "GripVictimRelease", "TraversalMove", "TraversalRelease"}
EQUIPMENT = {"SuppressorAttach", "SuppressorDetach", "WeaponHolster", "WeaponDraw"}
GUARD_CLIPS = {"Idle", "Walk", "Run", "Shoot", "Punch", "Damage", "Interact", "GripVictimHold", "Fall", "Unconscious", "Death", "ReviveRise"}


def entity_bindings(clip):
    player_binding = "authored_no_direct_state_binding" if clip in OFFLINE_ONLY else "active_standing_body_crouched_upper_on_crouched_lower" if clip == "CoverEnter" else "active"
    return [{"entity": entity, "binding": player_binding, "consumer": "WorldRenderer::AgentWeaponFrame" if clip in EQUIPMENT else "WorldRenderer::SelectActorSprite/SelectBodySprite"} for entity in ["Jugador", "Aliado"]] + [
        {"entity": "Guardia", "binding": "active_shared_original_rig_with_guard_rifle" if clip in GUARD_CLIPS else "not_selected_by_guard_state_machine", "consumer": "WorldRenderer::SelectGuardSprite"},
        {"entity": "Diana mecánica", "binding": "not_humanoid_atlas", "consumer": "WorldRenderer::DummyArt"}]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def inspect(path, columns, rows):
    image = Image.open(path)
    alpha = image.getchannel("A") if image.mode == "RGBA" else None
    cells = []
    for row in range(rows):
        for column in range(columns):
            rect = (round(column * image.width / columns), round(row * image.height / rows), round((column + 1) * image.width / columns), round((row + 1) * image.height / rows))
            cell = image.crop(rect)
            bounds = cell.getchannel("A").point(lambda a: 255 if a >= 64 else 0).getbbox() if alpha else None
            cells.append({"row": row, "column": column, "proposedRect": list(rect), "solidBounds": bounds, "sha256": hashlib.sha256(cell.tobytes()).hexdigest(),
                          "solidTouchesGrid": bool(bounds and (bounds[0] == 0 or bounds[1] == 0 or bounds[2] == cell.width or bounds[3] == cell.height))})
    return {"file": path.as_posix(), "sha256": sha(path), "size": list(image.size), "mode": image.mode,
            "alphaExtrema": alpha.getextrema() if alpha else None, "transparentPixels": alpha.histogram()[0] if alpha else 0,
            "equalIntegerGrid": image.width % columns == 0 and image.height % rows == 0, "grid": [columns, rows], "cells": cells,
            "distinctProposedCells": len(set(cell["sha256"] for cell in cells)), "gridBorderCases": sum(cell["solidTouchesGrid"] for cell in cells)}


def generate(root):
    output = root / "Manifests"
    output.mkdir(parents=True, exist_ok=True)
    source_config = Path("Games/Include/Config/NoTraceSpriteConfig.h")
    enum = re.search(r"enum class Clip\s*\{(.*?)\};", source_config.read_text(encoding="utf-8"), re.S)
    clips = [name.strip() for name in enum.group(1).split(",") if name.strip() != "Count"]
    atlas_path = root / "Atlas/CharacterAtlasMetadata.json"
    atlas = json.loads(atlas_path.read_text(encoding="utf-8")) if atlas_path.exists() else None
    review_path = root / "Manifests/runtime-review.json"
    review = json.loads(review_path.read_text(encoding="utf-8")) if review_path.exists() else {}
    accepted = review.get("status") == "PASS" and review.get("runtimeMetadataSha256") == sha(root / "Atlas/CharacterAtlasRuntime.json")
    sources = []
    for path in sorted((root / "Sources").glob("*.png")):
        identity = "Identity" in path.name
        parts = "RigParts" in path.name
        combined = "8x8" in path.name
        weapon = path.name.startswith("Weapon-")
        source = inspect(path, 4 if identity or parts or weapon or not combined else 8, 4 if parts or weapon else 8 if combined else 2)
        source["author"] = "Carlos Cortina"
        source["generator"] = "OpenAI image_gen builtin"
        source["prompt"] = (root / "Prompts" / (path.stem + ".txt")).as_posix()
        source["status"] = "rejected_grid_overlap" if combined else "rejected_direction_view" if "rejected" in path.name else "source_candidate_pending_pose_and_game_scale_review"
        source["activeGameContent"] = False
        if combined:
            source["rejection"] = "Siluetas de perfiles que invaden límites propuestos y no se separaron con seguridad; tamaño no divisible por8 sólo es información de layout, no un motivo suficiente de rechazo por sí solo"
        elif "rejected" in path.name:
            source["rejection"] = "La vista de piezas sigue siendo frontal y no representa la dirección solicitada; no usar como cobertura direccional"
        if parts:
            source["issues"] = ["Comprobar que sólo el muslo derecho tenga funda", "Usar rectángulos completos revisados, no asumir cuadrícula exacta", "Validar volúmenes y uniones a96px antes de aceptar rig"]
            identity_match = re.search(r"RigParts-([A-Z]+)-v(\d+)", path.name)
            if identity_match and "rejected" not in path.name:
                direction, version = identity_match.group(1), int(identity_match.group(2))
                source["status"] = "registered_authored_parts_used_by_rig" if VERSIONS.get(direction) == version else "superseded_semantic_or_joint_defect"
                source["activeGameContent"] = VERSIONS.get(direction) == version
                if source["activeGameContent"] and accepted:
                    source["status"] = "registered_authored_parts_runtime_reviewed"
        if weapon:
            source["activeGameContent"] = True
            source["status"] = "registered_weapon_props_runtime_reviewed" if accepted else "registered_weapon_props_runtime_review_pending"
        if "-S-v1" in path.name and not parts:
            source["issues"] = ["Las fases de recoil giran torso/arma lateralmente; corregir y validar antes de integrar"]
        sources.append(source)
    matrix = [{"clipId": index, "clip": clip, "entityBindings": entity_bindings(clip), "before": REUSE.get(clip, "Existing full-body source; reauthor identity and true phases") if index < 33 else "New authored sequence",
               "classification": "redesign", "newSource": "prone matched body/aim direction candidates" if clip in ["Aim", "Shoot"] else None,
               "integration": "CharacterArt paged raster and actual renderer states" if atlas else "pending", "executedVisualTests": review.get("clipEvidence", {}).get(clip, []) if accepted else [],
               "status": "runtime_render_reviewed" if accepted and review.get("clipEvidence", {}).get(clip) else "authored_offline_validated_no_dedicated_runtime_case" if accepted else "generated_and_integrated_runtime_validation_pending" if atlas else "not_complete"} for index, clip in enumerate(clips)]
    report = {"author": "Carlos Cortina", "organization": "CIO", "generatedAtUtc": datetime.now(timezone.utc).isoformat(), "schemaVersion": 2,
              "scope": "original_sources_authored_rig_paged_raster_and_runtime_contract", "activeGameContentChanged": bool(atlas), "originalClipCount": 33, "currentClipCount": len(clips),
              "matrix": matrix, "initialMissingClips": MISSING, "missingClips": [{"clip": clip, "status": "requires_real_sequence_and_state_integration"} for clip in MISSING if clip not in clips],
              "familyProposal": [{"id": index, "name": name, "status": "current_id_preserved" if index < 7 else "appended_authored_and_integrated" if atlas else "append_proposed_not_integrated"} for index, name in enumerate(FAMILIES)],
              "sources": sources, "atlasMetadata": str(atlas_path) if atlas else None,
              "runtimeReview": review_path.as_posix() if accepted else None,
              "warning": "Hash distinto no demuestra movimiento correcto; la validación offline no sustituye capturas runtime ni partida manual"}
    (output / "characters-v2.sources.json").write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
    print(json.dumps({"sourceFiles": len(sources), "currentClips": len(clips), "missingClips": len(report["missingClips"]), "rejectedSheets": sum(source["status"].startswith("rejected") for source in sources), "report": str(output / "characters-v2.sources.json")}))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Cataloga fuentes originales y rechazos sin modificar Content ni el juego")
    parser.add_argument("--root", type=Path, default=Path("Art/NoTrace/Characters"))
    generate(parser.parse_args().root)
