import copy
import hashlib
import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from PIL import Image

from ValidateNoTraceCharacterBaseline import ANCHOR_FIELDS, BASELINE_NAME, CROUCH_PUSH_IDENTITIES, CROUCH_PUSH_JOINTS, CROUCH_PUSH_WITNESS_NAME, DEATH_IDENTITIES, DEATH_WITNESS_NAME, IK_FLOOR_WITNESS_NAME, RECOVERY_WITNESS_NAME, REFERENCES_NAME, REGISTRATION_FILES, TOE_IDENTITIES, TOE_WITNESS_NAME, canonical_sha, compare, create_death_witness, create_ik_floor_witness, create_recovery_witness, create_references, file_sha, frame_record, main, read_json, registration_contracts, semantic_record, semantic_spec, snapshot, validate_crouch_push_witness, validate_current, validate_death_witness, validate_toe_witness, verify_socket_sources, witness_descriptor


class CharacterBaselineTests(unittest.TestCase):
    def temporary_directory(self):
        workspace = Path(__file__).resolve().parents[1]
        directory = (workspace / "build/qa-character-baseline-tests").resolve()
        self.assertTrue(directory.is_relative_to(workspace.resolve()))
        directory.mkdir(parents=True, exist_ok=True)
        return tempfile.TemporaryDirectory(dir=directory)

    def fixture(self, position=(1, 2)):
        pixels = Image.new("RGBA", (3, 2), (24, 46, 68, 0))
        pixels.putpixel((1, 0), (80, 100, 120, 160))
        page = Image.new("RGBA", (16, 16))
        page.paste(pixels, position)
        canvas = Image.new("RGBA", (192, 192))
        canvas.paste(pixels, (82, 96))
        entry = {
            "index": 42, "clip": "Idle", "stance": "Standing", "direction": "E", "phase": 0,
            "groundAnchor": [96.0, 144.0], "pelvis": [96.0, 109.0], "muzzle": [-1.0, -1.0],
            "grip": [-1.0, -1.0], "carryGrip": [85.0, 102.0], "supportWidth": 24.0,
            "joints": {"pelvis": [0.0, 0.0, 35.0]}, "page": 0, "rect": [0, 0, 192, 192],
            "pixelSha256": hashlib.sha256(canvas.tobytes()).hexdigest(),
            "art": {"full": {"page": 0, "rect": [*position, 3, 2], "offset": [82, 96]}},
        }
        return entry, [page]

    def baseline(self, record):
        return {"definition": {}, "headerSha256": "header", "sourceRegistrations": [], "originalSources": [], "weaponFiles": [], "frames": [record]}

    def contracts_fixture(self, root):
        entry, pages = self.fixture()
        baseline = self.baseline(frame_record(entry, pages, 192))
        (root / "Manifests").mkdir()
        for name in REGISTRATION_FILES:
            (root / name).write_bytes('{\n  "source": "Élite",\n  "nested": {"value": 1}\n}\n'.encode("utf-8"))
        baseline["sourceRegistrations"] = [{"file": name, "sha256": file_sha(root / name)} for name in REGISTRATION_FILES]
        return baseline, create_references(root, baseline)

    def test_repacking_preserves_canonical_rgba_and_semantics(self):
        before, old_pages = self.fixture((1, 2))
        after, new_pages = self.fixture((9, 11))
        after["page"], after["rect"] = 23, [192, 384, 192, 192]
        self.assertEqual(frame_record(before, old_pages, 192), frame_record(after, new_pages, 192))

    def test_transparent_rgb_tampering_is_detected(self):
        entry, pages = self.fixture()
        pages[0].putpixel((1, 2), (25, 46, 68, 0))
        with self.assertRaisesRegex(ValueError, "RGBA real"):
            frame_record(entry, pages, 192)

    def test_unmodified_index_anchor_and_joint_are_exact(self):
        entry, pages = self.fixture()
        before = self.baseline(frame_record(entry, pages, 192))
        for field in ["groundAnchor", "joints"]:
            changed = copy.deepcopy(entry)
            if field == "groundAnchor":
                changed[field][0] += 0.001
            else:
                changed[field]["pelvis"][2] += 0.001
            errors = compare(before, self.baseline(frame_record(changed, pages, 192)))
            self.assertTrue(any("índice 42" in error for error in errors), field)

    def test_layer_change_is_diagnosed_by_index_and_layer(self):
        entry, pages = self.fixture()
        before = self.baseline(frame_record(entry, pages, 192))
        after = copy.deepcopy(before)
        after["frames"][0]["rgbaSha256"]["full"] = "different"
        self.assertIn("RGBA distinto: índice 42, capa full", compare(before, after))

    def test_sources_and_index_set_cannot_change(self):
        entry, pages = self.fixture()
        before = self.baseline(frame_record(entry, pages, 192))
        after = copy.deepcopy(before)
        after["originalSources"] = [{"file": "source.png", "sha256": "new"}]
        after["frames"][0]["index"] = 43
        self.assertIn("Contrato o fuente distintos: originalSources", compare(before, after))
        self.assertIn("El conjunto de índices cambió", compare(before, after))

    def test_lf_crlf_contracts_are_equivalent(self):
        with self.temporary_directory() as directory:
            root = Path(directory)
            before, references = self.contracts_fixture(root)
            for name in REGISTRATION_FILES:
                path = root / name
                path.write_bytes(path.read_bytes().replace(b"\n", b"\r\n"))
            after = copy.deepcopy(before)
            after["sourceRegistrations"] = [{"file": name, "sha256": file_sha(root / name)} for name in REGISTRATION_FILES]
            self.assertNotEqual(before["sourceRegistrations"], after["sourceRegistrations"])
            baseline_path, references_path = root / "baseline.json", root / "references.json"
            for newline in [b"\n", b"\r\n"]:
                baseline_path.write_bytes(json.dumps(before, ensure_ascii=False, indent=2).encode("utf-8").replace(b"\n", newline))
                references_path.write_bytes(json.dumps(references, ensure_ascii=False, indent=2).encode("utf-8").replace(b"\n", newline))
                self.assertEqual([], compare(read_json(baseline_path), after, read_json(references_path), registration_contracts(root)))

    def test_contract_value_change_is_rejected(self):
        with self.temporary_directory() as directory:
            root = Path(directory)
            baseline, references = self.contracts_fixture(root)
            path = root / REGISTRATION_FILES[0]
            path.write_bytes(path.read_bytes().replace(b'"value": 1', b'"value": 2'))
            errors = compare(baseline, baseline, references, registration_contracts(root))
            self.assertIn("Contrato JSON distinto: " + REGISTRATION_FILES[0], errors)

    def test_duplicate_json_keys_are_rejected_at_every_depth(self):
        with self.temporary_directory() as directory:
            path = Path(directory) / "contract.json"
            for payload in ['{"value":1,"value":1}', '{"nested":{"value":1,"value":2}}']:
                path.write_text(payload, encoding="utf-8")
                with self.assertRaisesRegex(ValueError, "Clave JSON duplicada"):
                    read_json(path)

    def test_sidecar_for_another_baseline_is_rejected(self):
        with self.temporary_directory() as directory:
            root = Path(directory)
            baseline, references = self.contracts_fixture(root)
            references["baselineCanonicalSha256"] = "0" * 64
            with self.assertRaisesRegex(ValueError, "otro baseline"):
                compare(baseline, baseline, references, registration_contracts(root))

    def test_reference_creation_requires_original_raw_sha(self):
        with self.temporary_directory() as directory:
            root = Path(directory)
            baseline, _ = self.contracts_fixture(root)
            path = root / REGISTRATION_FILES[0]
            path.write_bytes(path.read_bytes().replace(b"\n", b"\r\n"))
            with self.assertRaisesRegex(ValueError, "SHA original distinto"):
                create_references(root, baseline)

    def test_check_does_not_create_a_missing_sidecar(self):
        with self.temporary_directory() as directory:
            root = Path(directory)
            baseline, _ = self.contracts_fixture(root)
            path = root / "baseline.json"
            path.write_text(json.dumps(baseline), encoding="utf-8")
            with patch("sys.argv", ["baseline", "check", "--root", str(root), "--baseline", str(path)]):
                with self.assertRaises(FileNotFoundError):
                    main()
            self.assertFalse((root / "Manifests" / REFERENCES_NAME).exists())


class CharacterBaselineExceptionTests(unittest.TestCase):
    temporary_directory = CharacterBaselineTests.temporary_directory
    fixture = CharacterBaselineTests.fixture
    baseline = CharacterBaselineTests.baseline
    contracts_fixture = CharacterBaselineTests.contracts_fixture

    def raster_fixture(self):
        entry, pages = self.fixture()
        before = self.baseline(frame_record(entry, pages, 192))
        after = copy.deepcopy(before)
        after["frames"][0]["rgbaSha256"]["full"] = "b" * 64
        row = {"index": 42, "identity": {key: entry[key] for key in ["clip", "stance", "direction", "phase"]}, "reason": "Corrección de raster full revisada para Idle E fase 0, índice 42",
               "rgbaSha256": {"full": "b" * 64}, "semantic": None}
        exceptions = {"schemaVersion": 1, "baselineCanonicalSha256": canonical_sha(before),
                      "toeWitnessCanonicalSha256": None, "entries": [row]}
        return before, after, exceptions

    def toe_fixture(self):
        template, pages = self.fixture()
        original, records = {}, []
        for index, identity in sorted(TOE_IDENTITIES.items()):
            entry = copy.deepcopy(template)
            entry.update(identity | {"index": index})
            entry["joints"].update({"left_ankle": [0.0, 5.5, 2.0], "right_ankle": [0.0, -5.5, 2.0],
                                    "left_toe": [5.0, 7.0, 0.0], "right_toe": [5.0, -7.0, 0.0]})
            original[index] = semantic_record(entry)
            records.append(frame_record(entry, pages, 192))
        before = self.baseline(records[0])
        before["frames"] = records
        before["provenance"] = {"metadataSha256": "d" * 64, "commit": "fixture"}
        witness = {"schemaVersion": 1, "baselineCanonicalSha256": canonical_sha(before),
                   "originalMetadataSha256": "d" * 64, "allowedJoints": ["left_toe", "right_toe"],
                   "frames": [{"index": row["index"], "semantic": original[row["index"]],
                               "semanticSha256": row["semanticSha256"], "jointsSha256": row["jointsSha256"]} for row in records]}
        index = 515
        actual = copy.deepcopy(original[index])
        actual["joints"]["left_toe"][0] += 0.25
        after = copy.deepcopy(before)
        candidate = next(row for row in after["frames"] if row["index"] == index)
        candidate["semanticSha256"] = canonical_sha(actual)
        candidate["jointsSha256"] = canonical_sha(actual["joints"])
        exceptions = {"schemaVersion": 1, "baselineCanonicalSha256": canonical_sha(before),
                      "toeWitnessCanonicalSha256": canonical_sha(witness), "entries": [
                          {"index": index, "identity": TOE_IDENTITIES[index], "reason": "Puntas relativas al tobillo en LieDown S fase 3, índice 515", "rgbaSha256": {},
                           "semantic": {"kind": "posture_toes_v1", "expectedSemanticSha256": candidate["semanticSha256"],
                                        "expectedJointsSha256": candidate["jointsSha256"]}}]}
        return before, after, exceptions, witness, {index: actual}

    def death_fixture(self):
        template, pages = self.fixture()
        original, records = {}, []
        for index, identity in sorted(DEATH_IDENTITIES.items()):
            entry = copy.deepcopy(template)
            entry.update(identity | {"index": index})
            entry["joints"].update({"left_ankle": [30.0, 7.0, 2.0], "right_ankle": [30.0, -14.0, 2.0],
                                    "left_toe": [34.0, 7.0, 1.0], "right_toe": [20.0, -8.0, 1.0]})
            original[index] = semantic_record(entry)
            records.append(frame_record(entry, pages, 192))
        before = self.baseline(records[0])
        before["frames"] = records
        before["provenance"] = {"metadataSha256": "d" * 64, "commit": "fixture"}
        witness = {"schemaVersion": 1, "baselineCanonicalSha256": canonical_sha(before),
                   "originalMetadataSha256": "d" * 64, "allowedJoints": ["right_toe"],
                   "frames": [{"index": row["index"], "semantic": original[row["index"]],
                               "semanticSha256": row["semanticSha256"], "jointsSha256": row["jointsSha256"]} for row in records]}
        index = 4609
        actual = copy.deepcopy(original[index])
        actual["joints"]["right_toe"][0] += .25
        after = copy.deepcopy(before)
        candidate = next(row for row in after["frames"] if row["index"] == index)
        candidate["semanticSha256"], candidate["jointsSha256"] = canonical_sha(actual), canonical_sha(actual["joints"])
        exceptions = {"schemaVersion": 1, "baselineCanonicalSha256": canonical_sha(before), "toeWitnessCanonicalSha256": None,
                      "deathWitnessCanonicalSha256": canonical_sha(witness), "entries": [
                          {"index": index, "identity": DEATH_IDENTITIES[index], "reason": "Retarget de tobillo conserva offset local right_toe en Death S fase 1",
                           "rgbaSha256": {}, "semantic": {"kind": "death_child_offset_v1", "expectedSemanticSha256": candidate["semanticSha256"],
                                                         "expectedJointsSha256": candidate["jointsSha256"]}}]}
        return before, after, exceptions, witness, {index: actual}

    def crouch_push_fixture(self, index=40):
        template, pages = self.fixture()
        original, records = {}, []
        for frame_index, identity in sorted(CROUCH_PUSH_IDENTITIES.items()):
            entry = copy.deepcopy(template)
            entry.update(identity | {"index": frame_index})
            entry["joints"].update({"left_ankle": [-7.0, 7.0, 2.0], "right_ankle": [10.0, -6.0, 2.0],
                                    "left_toe": [5.0, 7.0, 0.0], "right_toe": [15.0, -7.5, 0.0]})
            original[frame_index] = semantic_record(entry)
            records.append(frame_record(entry, pages, 192))
        before = self.baseline(records[0])
        before["frames"] = records
        before["provenance"] = {"metadataSha256": "d" * 64, "commit": "fixture"}
        witness = {"schemaVersion": 1, "baselineCanonicalSha256": canonical_sha(before), "originalMetadataSha256": "d" * 64,
                   "allowedJoints": {str(key): list(joints) for key, joints in CROUCH_PUSH_JOINTS.items()},
                   "frames": [{"index": row["index"], "semantic": original[row["index"]],
                               "semanticSha256": row["semanticSha256"], "jointsSha256": row["jointsSha256"]} for row in records]}
        actual = copy.deepcopy(original[index])
        for joint in CROUCH_PUSH_JOINTS[index]:
            actual["joints"][joint][0] += .25
        after = copy.deepcopy(before)
        candidate = next(row for row in after["frames"] if row["index"] == index)
        candidate["semanticSha256"], candidate["jointsSha256"] = canonical_sha(actual), canonical_sha(actual["joints"])
        exceptions = {"schemaVersion": 1, "baselineCanonicalSha256": canonical_sha(before), "toeWitnessCanonicalSha256": None,
                      "crouchPushWitnessCanonicalSha256": canonical_sha(witness), "entries": [
                          {"index": index, "identity": CROUCH_PUSH_IDENTITIES[index], "reason": "Retarget de tobillo conserva sólo descendientes autorizados en este índice",
                           "rgbaSha256": {}, "semantic": {"kind": "crouch_push_child_offset_v1", "expectedSemanticSha256": candidate["semanticSha256"],
                                                         "expectedJointsSha256": candidate["jointsSha256"]}}]}
        return before, after, exceptions, witness, {index: actual}

    def test_exact_raster_exception_passes_without_relaxing_other_fields(self):
        before, after, exceptions = self.raster_fixture()
        self.assertEqual([], compare(before, after, exceptions=exceptions))
        after["frames"][0]["anchors"]["groundAnchor"][0] += 0.001
        self.assertTrue(any("anchors" in error for error in compare(before, after, exceptions=exceptions)))

    def test_unlisted_layer_and_index_are_still_rejected(self):
        before, after, exceptions = self.raster_fixture()
        before["frames"][0]["rgbaSha256"]["lower"] = "a" * 64
        after["frames"][0]["rgbaSha256"]["lower"] = "c" * 64
        exceptions["baselineCanonicalSha256"] = canonical_sha(before)
        self.assertIn("RGBA distinto: índice 42, capa lower", compare(before, after, exceptions=exceptions))
        other = copy.deepcopy(before["frames"][0])
        other["index"] = 43
        before["frames"].append(other)
        after["frames"].append(copy.deepcopy(other))
        after["frames"][-1]["rgbaSha256"]["full"] = "e" * 64
        exceptions["baselineCanonicalSha256"] = canonical_sha(before)
        self.assertIn("RGBA distinto: índice 43, capa full", compare(before, after, exceptions=exceptions))

    def test_expected_new_hash_is_exact(self):
        before, after, exceptions = self.raster_fixture()
        after["frames"][0]["rgbaSha256"]["full"] = "c" * 64
        with self.assertRaisesRegex(ValueError, "expected SHA"):
            compare(before, after, exceptions=exceptions)

    def test_unused_exception_is_rejected(self):
        before, after, exceptions = self.raster_fixture()
        exceptions["entries"][0]["rgbaSha256"]["full"] = before["frames"][0]["rgbaSha256"]["full"]
        with self.assertRaisesRegex(ValueError, "ociosa"):
            compare(before, after, exceptions=exceptions)

    def test_strict_schema_rejects_wildcards_duplicates_and_extra_fields(self):
        mutations = [
            lambda value: value.update({"clips": ["*"]}),
            lambda value: value["entries"].append(copy.deepcopy(value["entries"][0])),
            lambda value: value["entries"][0].update({"family": "Posture"}),
            lambda value: value["entries"][0]["identity"].update({"clip": "*"}),
            lambda value: value["entries"][0]["identity"].update({"phase": 0.0}),
            lambda value: value["entries"][0].update({"index": True}),
            lambda value: value["entries"][0]["rgbaSha256"].update({"full": "B" * 64}),
            lambda value: value["entries"][0]["rgbaSha256"].update({"newLayer": "c" * 64}),
            lambda value: value.update({"entries": []}),
            lambda value: value.update({"schemaVersion": True}),
        ]
        for mutate in mutations:
            before, after, exceptions = self.raster_fixture()
            mutate(exceptions)
            with self.subTest(contract=exceptions), self.assertRaises(ValueError):
                compare(before, after, exceptions=exceptions)

    def test_exception_is_bound_to_one_baseline(self):
        before, after, exceptions = self.raster_fixture()
        exceptions["baselineCanonicalSha256"] = "0" * 64
        with self.assertRaisesRegex(ValueError, "otro baseline"):
            compare(before, after, exceptions=exceptions)

    def test_exact_toe_exception_passes_with_original_witness(self):
        before, after, exceptions, witness, semantics = self.toe_fixture()
        self.assertEqual(88, len(validate_toe_witness(before, witness)))
        self.assertEqual([], compare(before, after, exceptions=exceptions, toe_witness=witness, semantic_records=semantics))

    def test_new_hash_cannot_approve_other_joint_or_field_changes(self):
        for field in ["ankle", "anchor", "phaseType", "extraJoint"]:
            before, after, exceptions, witness, semantics = self.toe_fixture()
            actual = semantics[515]
            if field == "ankle":
                actual["joints"]["left_ankle"][0] += 0.25
            elif field == "anchor":
                actual["groundAnchor"][0] += 0.25
            elif field == "phaseType":
                actual["phase"] = 3.0
            else:
                actual["joints"]["new"] = [1, 2, 3]
            candidate = next(row for row in after["frames"] if row["index"] == 515)
            candidate["semanticSha256"] = canonical_sha(actual)
            candidate["jointsSha256"] = canonical_sha(actual["joints"])
            exceptions["entries"][0]["semantic"].update({"expectedSemanticSha256": candidate["semanticSha256"], "expectedJointsSha256": candidate["jointsSha256"]})
            with self.subTest(field=field), self.assertRaisesRegex(ValueError, "ajenos a las puntas"):
                compare(before, after, exceptions=exceptions, toe_witness=witness, semantic_records=semantics)

    def test_semantic_exception_requires_witness_and_real_semantics(self):
        before, after, exceptions, witness, semantics = self.toe_fixture()
        for kwargs in [{}, {"toe_witness": witness}, {"semantic_records": semantics}]:
            with self.subTest(keys=kwargs.keys()), self.assertRaisesRegex(ValueError, "Falta testigo"):
                compare(before, after, exceptions=exceptions, **kwargs)

    def test_witness_cannot_expand_joints_or_replace_original_values(self):
        for field in ["allowedJoints", "values", "indices", "hash"]:
            before, after, exceptions, witness, semantics = self.toe_fixture()
            if field == "allowedJoints":
                witness["allowedJoints"].append("left_ankle")
            elif field == "values":
                witness["frames"][0]["semantic"]["joints"]["left_ankle"][0] += 1
            elif field == "indices":
                witness["frames"][1] = copy.deepcopy(witness["frames"][0])
            else:
                exceptions["toeWitnessCanonicalSha256"] = "0" * 64
            if field != "hash":
                exceptions["toeWitnessCanonicalSha256"] = canonical_sha(witness)
            with self.subTest(field=field), self.assertRaises(ValueError):
                compare(before, after, exceptions=exceptions, toe_witness=witness, semantic_records=semantics)

    def test_semantics_cannot_be_exempted_outside_the_88_toe_indices(self):
        before, after, exceptions = self.raster_fixture()
        exceptions["entries"][0]["semantic"] = {"kind": "posture_toes_v1", "expectedSemanticSha256": "a" * 64, "expectedJointsSha256": "b" * 64}
        with self.assertRaisesRegex(ValueError, "88 índices"):
            compare(before, after, exceptions=exceptions)

    def test_sources_remain_protected_when_art_has_an_exception(self):
        before, after, exceptions = self.raster_fixture()
        after["originalSources"] = [{"file": "changed.png", "sha256": "c" * 64}]
        self.assertIn("Contrato o fuente distintos: originalSources", compare(before, after, exceptions=exceptions))

    def test_check_never_creates_a_missing_exception_file(self):
        with self.temporary_directory() as directory:
            root = Path(directory)
            baseline, references = self.contracts_fixture(root)
            baseline_path, references_path, exceptions_path = root / "baseline.json", root / "references.json", root / "missing-exceptions.json"
            baseline_path.write_text(json.dumps(baseline), encoding="utf-8")
            references_path.write_text(json.dumps(references), encoding="utf-8")
            with patch("sys.argv", ["baseline", "check", "--root", str(root), "--baseline", str(baseline_path), "--references", str(references_path), "--exceptions", str(exceptions_path)]):
                with self.assertRaises(FileNotFoundError):
                    main()
            self.assertFalse(exceptions_path.exists())
            self.assertFalse((root / "Manifests" / TOE_WITNESS_NAME).exists())

    def test_check_never_creates_a_missing_toe_witness(self):
        with self.temporary_directory() as directory:
            root = Path(directory)
            contract_baseline, _ = self.contracts_fixture(root)
            before, _, exceptions, witness, _ = self.toe_fixture()
            before["sourceRegistrations"] = contract_baseline["sourceRegistrations"]
            references = create_references(root, before)
            witness["baselineCanonicalSha256"] = canonical_sha(before)
            exceptions["baselineCanonicalSha256"] = canonical_sha(before)
            exceptions["toeWitnessCanonicalSha256"] = canonical_sha(witness)
            for filename, value in [("baseline.json", before), ("references.json", references), ("exceptions.json", exceptions)]:
                (root / filename).write_text(json.dumps(value), encoding="utf-8")
            with patch("sys.argv", ["baseline", "check", "--root", str(root), "--baseline", str(root / "baseline.json"), "--references", str(root / "references.json"), "--exceptions", str(root / "exceptions.json")]):
                with self.assertRaises(FileNotFoundError):
                    main()
            self.assertFalse((root / "Manifests" / TOE_WITNESS_NAME).exists())

    def test_explicit_witness_creation_cannot_overwrite_existing_file(self):
        with self.temporary_directory() as directory:
            root = Path(directory)
            baseline, _ = self.contracts_fixture(root)
            (root / "baseline.json").write_text(json.dumps(baseline), encoding="utf-8")
            witness_path = root / "Manifests" / TOE_WITNESS_NAME
            witness_path.write_bytes(b"immutable")
            with patch("sys.argv", ["baseline", "freeze-toe-witness", "--root", str(root), "--baseline", str(root / "baseline.json"), "--original-metadata", str(root / "original.json")]):
                with self.assertRaisesRegex(ValueError, "inmutable"):
                    main()
            self.assertEqual(b"immutable", witness_path.read_bytes())

    def test_anchor_numeric_types_are_exact_even_with_art_exception(self):
        before, after, exceptions = self.raster_fixture()
        after["frames"][0]["anchors"]["groundAnchor"][0] = 96
        self.assertTrue(any("anchors" in error for error in compare(before, after, exceptions=exceptions)))

    def test_header_cannot_change_with_art_exception(self):
        before, after, exceptions = self.raster_fixture()
        after["headerSha256"] = "changed"
        self.assertIn("Contrato o fuente distintos: headerSha256", compare(before, after, exceptions=exceptions))

    def test_duplicate_snapshot_indices_are_rejected_without_an_exception_file(self):
        before, after, _ = self.raster_fixture()
        after["frames"].append(copy.deepcopy(after["frames"][0]))
        with self.assertRaisesRegex(ValueError, "duplicados"):
            compare(before, after)

    def test_reason_is_required_and_cannot_be_empty(self):
        for value in [None, "", "   \n\t", False, 42]:
            before, after, exceptions = self.raster_fixture()
            exceptions["entries"][0]["reason"] = value
            with self.subTest(reason=value), self.assertRaisesRegex(ValueError, "motivo"):
                compare(before, after, exceptions=exceptions)
        before, after, exceptions = self.raster_fixture()
        del exceptions["entries"][0]["reason"]
        with self.assertRaisesRegex(ValueError, "Entrada de excepción"):
            compare(before, after, exceptions=exceptions)

    def test_runtime_json_types_are_exact_before_raster_reads(self):
        metadata = {"schemaVersion": 3, "cellSize": 192, "clips": [{}] * 53, "pages": [],
                    "frames": [{"index": index, "phase": 1} for index in range(4544)]}
        for field, value, message in [("index", True, "Índice runtime"), ("index", 1.0, "Índice runtime"),
                                      ("cellSize", 192.0, "Cabeceras metadata/runtime"),
                                      ("phase", 1.0, "Metadata/runtime difieren")]:
            with self.subTest(field=field, value=value), self.temporary_directory() as directory:
                root = Path(directory)
                (root / "Atlas").mkdir()
                runtime = copy.deepcopy(metadata)
                if field == "cellSize":
                    runtime[field] = value
                elif field == "index":
                    runtime["frames"][1][field] = value
                else:
                    runtime["frames"][0][field] = value
                (root / "Atlas/CharacterAtlasMetadata.json").write_text(json.dumps(metadata), encoding="utf-8")
                (root / "Atlas/CharacterAtlasRuntime.json").write_text(json.dumps(runtime), encoding="utf-8")
                with self.assertRaisesRegex(ValueError, message):
                    snapshot(root, "fixture")

    def test_death_exact_56_witness_allows_only_right_toe(self):
        before, after, exceptions, witness, semantics = self.death_fixture()
        self.assertEqual(56, len(validate_death_witness(before, witness)))
        self.assertEqual([], compare(before, after, exceptions=exceptions, death_witness=witness, semantic_records=semantics))

    def test_death_new_hash_cannot_approve_other_joint_or_fields(self):
        for field in ["left_toe", "right_ankle", "head", "anchor", "phaseType"]:
            before, after, exceptions, witness, semantics = self.death_fixture()
            actual = semantics[4609]
            if field in ["left_toe", "right_ankle"]:
                actual["joints"][field][0] += .5
            elif field == "head":
                actual["joints"][field] = [0.0, 0.0, 9.0]
            elif field == "anchor":
                actual["groundAnchor"][0] += .5
            else:
                actual["phase"] = 1.0
            candidate = next(row for row in after["frames"] if row["index"] == 4609)
            candidate["semanticSha256"], candidate["jointsSha256"] = canonical_sha(actual), canonical_sha(actual["joints"])
            exceptions["entries"][0]["semantic"].update({"expectedSemanticSha256": candidate["semanticSha256"], "expectedJointsSha256": candidate["jointsSha256"]})
            with self.subTest(field=field), self.assertRaisesRegex(ValueError, "ajenos a las puntas"):
                compare(before, after, exceptions=exceptions, death_witness=witness, semantic_records=semantics)

    def test_death_witness_cannot_expand_scope_or_change_original(self):
        for field in ["allowedJoints", "values", "identity", "indices", "hash"]:
            before, after, exceptions, witness, semantics = self.death_fixture()
            if field == "allowedJoints":
                witness["allowedJoints"].append("left_toe")
            elif field == "values":
                witness["frames"][0]["semantic"]["joints"]["right_ankle"][0] += 1
            elif field == "identity":
                witness["frames"][0]["semantic"]["clip"] = "Fall"
            elif field == "indices":
                witness["frames"][1] = copy.deepcopy(witness["frames"][0])
            else:
                exceptions["deathWitnessCanonicalSha256"] = "0" * 64
            if field != "hash":
                exceptions["deathWitnessCanonicalSha256"] = canonical_sha(witness)
            with self.subTest(field=field), self.assertRaises(ValueError):
                compare(before, after, exceptions=exceptions, death_witness=witness, semantic_records=semantics)

    def test_semantic_kinds_cannot_cross_authorized_index_sets(self):
        for fixture, kind in [(self.death_fixture, "posture_toes_v1"), (self.toe_fixture, "death_child_offset_v1")]:
            before, after, exceptions, witness, semantics = fixture()
            exceptions["entries"][0]["semantic"]["kind"] = kind
            with self.subTest(kind=kind), self.assertRaisesRegex(ValueError, "según kind"):
                compare(before, after, exceptions=exceptions, toe_witness=witness, death_witness=witness, semantic_records=semantics)

    def test_death_needs_explicit_bound_witness_and_real_semantics(self):
        before, after, exceptions, witness, semantics = self.death_fixture()
        for kwargs in [{}, {"death_witness": witness}, {"semantic_records": semantics}]:
            with self.subTest(keys=kwargs.keys()), self.assertRaisesRegex(ValueError, "Falta testigo"):
                compare(before, after, exceptions=exceptions, **kwargs)
        del exceptions["deathWitnessCanonicalSha256"]
        with self.assertRaisesRegex(ValueError, "SHA del testigo"):
            compare(before, after, exceptions=exceptions, death_witness=witness, semantic_records=semantics)

    def test_three_separate_witnesses_can_coexist(self):
        before, after, exceptions, toe_witness, semantics = self.toe_fixture()
        death_before, death_after, death_exceptions, death_witness, death_semantics = self.death_fixture()
        before["frames"] += death_before["frames"]
        after["frames"] += death_after["frames"]
        for document in [exceptions, toe_witness, death_witness]:
            document["baselineCanonicalSha256"] = canonical_sha(before)
        exceptions["toeWitnessCanonicalSha256"] = canonical_sha(toe_witness)
        exceptions["deathWitnessCanonicalSha256"] = canonical_sha(death_witness)
        exceptions["entries"] += death_exceptions["entries"]
        other_before, other_after, other_exceptions, other_witness, other_semantics = self.crouch_push_fixture()
        before["frames"] += other_before["frames"]
        after["frames"] += other_after["frames"]
        for document in [exceptions, toe_witness, death_witness, other_witness]:
            document["baselineCanonicalSha256"] = canonical_sha(before)
        exceptions["toeWitnessCanonicalSha256"] = canonical_sha(toe_witness)
        exceptions["deathWitnessCanonicalSha256"] = canonical_sha(death_witness)
        exceptions["crouchPushWitnessCanonicalSha256"] = canonical_sha(other_witness)
        exceptions["entries"] += other_exceptions["entries"]
        self.assertEqual([], compare(before, after, exceptions=exceptions, toe_witness=toe_witness,
                                     death_witness=death_witness, crouch_push_witness=other_witness,
                                     semantic_records=semantics | death_semantics | other_semantics))

    def test_check_never_creates_a_missing_death_witness(self):
        with self.temporary_directory() as directory:
            root = Path(directory)
            contract_baseline, _ = self.contracts_fixture(root)
            before, _, exceptions, witness, _ = self.death_fixture()
            before["sourceRegistrations"] = contract_baseline["sourceRegistrations"]
            references = create_references(root, before)
            witness["baselineCanonicalSha256"] = canonical_sha(before)
            exceptions["baselineCanonicalSha256"], exceptions["deathWitnessCanonicalSha256"] = canonical_sha(before), canonical_sha(witness)
            for filename, value in [("baseline.json", before), ("references.json", references), ("exceptions.json", exceptions)]:
                (root / filename).write_text(json.dumps(value), encoding="utf-8")
            with patch("sys.argv", ["baseline", "check", "--root", str(root), "--baseline", str(root / "baseline.json"), "--references", str(root / "references.json"), "--exceptions", str(root / "exceptions.json")]):
                with self.assertRaises(FileNotFoundError):
                    main()
            self.assertFalse((root / "Manifests" / DEATH_WITNESS_NAME).exists())

    def test_explicit_death_witness_creator_cannot_overwrite_or_accept_wrong_original(self):
        with self.temporary_directory() as directory:
            root = Path(directory)
            baseline, _ = self.contracts_fixture(root)
            (root / "baseline.json").write_text(json.dumps(baseline), encoding="utf-8")
            witness = root / "Manifests" / DEATH_WITNESS_NAME
            witness.write_bytes(b"immutable")
            with patch("sys.argv", ["baseline", "freeze-death-witness", "--root", str(root), "--baseline", str(root / "baseline.json"), "--original-metadata", str(root / "original.json")]):
                with self.assertRaisesRegex(ValueError, "inmutable"):
                    main()
            self.assertEqual(b"immutable", witness.read_bytes())
            original = root / "original.json"
            original.write_bytes(b"{}")
            baseline["provenance"] = {"metadataSha256": "d" * 64}
            with self.assertRaisesRegex(ValueError, "bytes de metadata originales"):
                create_death_witness(baseline, original)

    def test_unused_death_witness_declaration_is_rejected(self):
        before, after, exceptions = self.raster_fixture()
        exceptions["deathWitnessCanonicalSha256"] = "a" * 64
        with self.assertRaisesRegex(ValueError, "Death declarado"):
            compare(before, after, exceptions=exceptions)

    def test_crouch_push_exact_witness_has_128_identity_specific_joint_contracts(self):
        for index in [40, 3584]:
            before, after, exceptions, witness, semantics = self.crouch_push_fixture(index)
            self.assertEqual(128, len(validate_crouch_push_witness(before, witness)))
            self.assertEqual(["left_toe", "right_toe"], witness["allowedJoints"]["40"])
            self.assertEqual(["left_toe"], witness["allowedJoints"]["3584"])
            self.assertEqual([], compare(before, after, exceptions=exceptions, crouch_push_witness=witness, semantic_records=semantics))

    def test_crouch_push_other_joints_are_protected_even_with_matching_new_hashes(self):
        for index, joint in [(40, "left_ankle"), (40, "pelvis"), (3584, "right_toe"), (3584, "left_ankle")]:
            before, after, exceptions, witness, semantics = self.crouch_push_fixture(index)
            actual = semantics[index]
            actual["joints"][joint][0] += .5
            candidate = next(row for row in after["frames"] if row["index"] == index)
            candidate["semanticSha256"], candidate["jointsSha256"] = canonical_sha(actual), canonical_sha(actual["joints"])
            exceptions["entries"][0]["semantic"].update({"expectedSemanticSha256": candidate["semanticSha256"], "expectedJointsSha256": candidate["jointsSha256"]})
            with self.subTest(index=index, joint=joint), self.assertRaisesRegex(ValueError, "ajenos a las puntas"):
                compare(before, after, exceptions=exceptions, crouch_push_witness=witness, semantic_records=semantics)

    def test_crouch_push_witness_cannot_broaden_joint_mapping_or_index_scope(self):
        for field in ["pushRight", "crouchAnkle", "identity", "duplicate", "extraIndex"]:
            before, after, exceptions, witness, semantics = self.crouch_push_fixture()
            if field == "pushRight":
                witness["allowedJoints"]["3584"].append("right_toe")
            elif field == "crouchAnkle":
                witness["allowedJoints"]["40"].append("left_ankle")
            elif field == "identity":
                witness["frames"][0]["semantic"]["clip"] = "Idle"
            elif field == "duplicate":
                witness["frames"][1] = copy.deepcopy(witness["frames"][0])
            else:
                witness["allowedJoints"]["0"] = ["left_toe"]
            exceptions["crouchPushWitnessCanonicalSha256"] = canonical_sha(witness)
            with self.subTest(field=field), self.assertRaises(ValueError):
                compare(before, after, exceptions=exceptions, crouch_push_witness=witness, semantic_records=semantics)

    def test_crouch_push_kind_cannot_authorize_a_posture_index(self):
        before, after, exceptions, witness, semantics = self.toe_fixture()
        exceptions["entries"][0]["semantic"]["kind"] = "crouch_push_child_offset_v1"
        with self.assertRaisesRegex(ValueError, "según kind"):
            compare(before, after, exceptions=exceptions, crouch_push_witness=witness, semantic_records=semantics)

    def test_explicit_crouch_push_witness_creation_cannot_overwrite_existing(self):
        with self.temporary_directory() as directory:
            root = Path(directory)
            baseline, _ = self.contracts_fixture(root)
            (root / "baseline.json").write_text(json.dumps(baseline), encoding="utf-8")
            path = root / "Manifests" / CROUCH_PUSH_WITNESS_NAME
            path.write_bytes(b"immutable")
            with patch("sys.argv", ["baseline", "freeze-crouch-push-witness", "--root", str(root), "--baseline", str(root / "baseline.json"), "--original-metadata", str(root / "original.json")]):
                with self.assertRaisesRegex(ValueError, "inmutable"):
                    main()
            self.assertEqual(b"immutable", path.read_bytes())

    def test_explicit_crouch_push_witness_path_is_read_without_default_creation(self):
        with self.temporary_directory() as directory:
            root = Path(directory)
            contract_baseline, _ = self.contracts_fixture(root)
            before, after, exceptions, witness, semantics = self.crouch_push_fixture()
            before["sourceRegistrations"] = contract_baseline["sourceRegistrations"]
            after["sourceRegistrations"] = before["sourceRegistrations"]
            references = create_references(root, before)
            witness["baselineCanonicalSha256"] = canonical_sha(before)
            exceptions["baselineCanonicalSha256"], exceptions["crouchPushWitnessCanonicalSha256"] = canonical_sha(before), canonical_sha(witness)
            for name, value in [("baseline.json", before), ("references.json", references), ("exceptions.json", exceptions), ("explicit-witness.json", witness)]:
                (root / name).write_text(json.dumps(value), encoding="utf-8")

            def candidate_snapshot(_root, _provenance, semantic_records):
                semantic_records.update(semantics)
                return after

            with patch("ValidateNoTraceCharacterBaseline.snapshot", candidate_snapshot):
                report = validate_current(root, root / "baseline.json", root / "references.json", root / "exceptions.json",
                                          crouch_push_witness_path=root / "explicit-witness.json")
            self.assertEqual("PASS", report["status"])
            self.assertEqual(canonical_sha(witness), report["crouchPushWitnessCanonicalSha256"])
            with self.assertRaises(FileNotFoundError):
                validate_current(root, root / "baseline.json", root / "references.json", root / "exceptions.json")
            self.assertFalse((root / "Manifests" / CROUCH_PUSH_WITNESS_NAME).exists())

    def test_repro_passes_explicit_witness_paths_once_without_content_generation(self):
        import GenerateNoTraceCharactersRepro as repro

        with self.temporary_directory() as directory:
            root = Path(directory)
            (root / "Atlas/Packed").mkdir(parents=True)
            (root / "Manifests").mkdir()
            for name in ["WeaponProps.png", "WeaponSelection.png", "CharacterAtlasRuntime.json"]:
                (root / "Atlas" / name).write_bytes(b"unchanged")
            paths = [root / name for name in ["baseline.json", "references.json", "exceptions.json", "toe.json", "death.json", "crouch-push.json", "floor.json", "recovery.json"]]
            for pin in [None, "a" * 64]:
                with patch.object(repro, "generate") as generate, patch.object(repro, "validate") as validate, patch("builtins.print"):
                    repro.reproduce(root, *paths, expected_contract_sha256=pin)
                generate.assert_called_once_with(root)
                validate.assert_called_once_with(root, *paths[:4], death_witness=paths[4], crouch_push_witness=paths[5], ik_floor_witness=paths[6], recovery_witness=paths[7], expected_contract_sha256=pin)


class CharacterLiteralJointTests(unittest.TestCase):
    temporary_directory = CharacterBaselineTests.temporary_directory

    @classmethod
    def setUpClass(cls):
        cls.root = Path(__file__).resolve().parents[1] / "Art/NoTrace/Characters"
        cls.original = read_json(cls.root / "Manifests" / BASELINE_NAME)
        cls.witness_files = {"posture_toes_v1": (TOE_WITNESS_NAME, "toeWitnessCanonicalSha256", "toe_witness"),
                             "death_child_offset_v1": (DEATH_WITNESS_NAME, "deathWitnessCanonicalSha256", "death_witness"),
                             "crouch_push_child_offset_v1": (CROUCH_PUSH_WITNESS_NAME, "crouchPushWitnessCanonicalSha256", "crouch_push_witness"),
                             "ik_floor_v1": (IK_FLOOR_WITNESS_NAME, "ikFloorWitnessCanonicalSha256", "ik_floor_witness"),
                             "recovery_prone_frame_v1": (RECOVERY_WITNESS_NAME, "recoveryWitnessCanonicalSha256", "recovery_witness")}
        cls.witnesses = {kind: read_json(cls.root / "Manifests" / entry[0]) for kind, entry in cls.witness_files.items()}
        cls.scopes = {kind: semantic_spec(kind, witness)[0] for kind, witness in cls.witnesses.items()}
        cls.original_semantics = {row["index"]: row["semantic"] for witness in cls.witnesses.values() for row in witness["frames"]}

    def candidate_fixture(self, indices, overrides=None):
        from GenerateNoTraceCharactersActions import body_pose
        from GenerateNoTraceCharactersRig import project

        before, after = self.original, copy.deepcopy(self.original)
        frames = {frame["index"]: frame for frame in after["frames"]}
        exceptions = {"schemaVersion": 1, "baselineCanonicalSha256": canonical_sha(before), "toeWitnessCanonicalSha256": None, "entries": []}
        semantics = {}
        for index in indices:
            actual = copy.deepcopy(self.original_semantics[index])
            actual["joints"] = body_pose(actual["clip"], actual["phase"], actual["stance"])
            kinds = [kind for kind, scope in self.scopes.items() if index in scope]
            if overrides is not None:
                kinds = overrides
            if index in self.scopes["recovery_prone_frame_v1"]:
                actual["carryGrip"] = list(project(actual["joints"]["right_wrist"], actual["direction"]))
                frames[index]["anchors"]["carryGrip"] = actual["carryGrip"]
            frames[index]["semanticSha256"], frames[index]["jointsSha256"] = canonical_sha(actual), canonical_sha(actual["joints"])
            semantic = {"kind": kinds[0] if len(kinds) == 1 else "joint_contracts_v1", "expectedSemanticSha256": frames[index]["semanticSha256"], "expectedJointsSha256": frames[index]["jointsSha256"]}
            if len(kinds) != 1:
                semantic["contracts"] = kinds
            exceptions["entries"].append({"index": index, "identity": {key: actual[key] for key in ["clip", "stance", "direction", "phase"]},
                                           "reason": "Fixture de validación de contratos literales; no aprueba recursos producidos", "rgbaSha256": {}, "semantic": semantic})
            for kind in kinds:
                exceptions[self.witness_files[kind][1]] = canonical_sha(self.witnesses[kind])
            semantics[index] = actual
        kwargs = {entry[2]: self.witnesses[kind] for kind, entry in self.witness_files.items()}
        return before, after, exceptions, semantics, kwargs

    def refresh_hashes(self, after, exceptions, semantics, index):
        frame = next(frame for frame in after["frames"] if frame["index"] == index)
        row = next(row for row in exceptions["entries"] if row["index"] == index)
        frame["semanticSha256"], frame["jointsSha256"] = canonical_sha(semantics[index]), canonical_sha(semantics[index]["joints"])
        row["semantic"].update({"expectedSemanticSha256": frame["semanticSha256"], "expectedJointsSha256": frame["jointsSha256"]})

    def test_all_488_literal_semantic_candidates_validate_without_raster_approval(self):
        self.assertEqual(488, len(self.original_semantics))
        before, after, exceptions, semantics, kwargs = self.candidate_fixture(sorted(self.original_semantics))
        self.assertEqual([], compare(before, after, exceptions=exceptions, semantic_records=semantics, **kwargs))

    def test_posture_and_death_overlaps_require_both_explicit_contracts(self):
        for kind, expected in [("posture_toes_v1", 32), ("death_child_offset_v1", 16)]:
            overlap = set(self.scopes[kind]) & set(self.scopes["ik_floor_v1"])
            self.assertEqual(expected, len(overlap))
            index = min(overlap)
            before, after, exceptions, semantics, kwargs = self.candidate_fixture([index], [kind])
            with self.subTest(kind=kind), self.assertRaisesRegex(ValueError, "ajenos a las puntas"):
                compare(before, after, exceptions=exceptions, semantic_records=semantics, **kwargs)

    def test_composition_rejects_duplicate_unknown_empty_or_uncovered_contracts(self):
        index = min(set(self.scopes["posture_toes_v1"]) & set(self.scopes["ik_floor_v1"]))
        for names in [[], ["ik_floor_v1", "ik_floor_v1"], ["*"], ["posture_toes_v1", "death_child_offset_v1"], [False]]:
            before, after, exceptions, semantics, kwargs = self.candidate_fixture([index])
            exceptions["entries"][0]["semantic"].update({"kind": "joint_contracts_v1", "contracts": names})
            with self.subTest(names=names), self.assertRaises(ValueError):
                compare(before, after, exceptions=exceptions, semantic_records=semantics, **kwargs)

    def test_recovery_carry_grip_is_derived_from_actual_wrist_projection(self):
        index = min(self.scopes["recovery_prone_frame_v1"])
        before, after, exceptions, semantics, kwargs = self.candidate_fixture([index])
        self.assertEqual([], compare(before, after, exceptions=exceptions, semantic_records=semantics, **kwargs))
        semantics[index]["carryGrip"][0] += .25
        next(frame for frame in after["frames"] if frame["index"] == index)["anchors"]["carryGrip"] = semantics[index]["carryGrip"]
        self.refresh_hashes(after, exceptions, semantics, index)
        with self.assertRaisesRegex(ValueError, "proyección exacta"):
            compare(before, after, exceptions=exceptions, semantic_records=semantics, **kwargs)

    def test_recovery_preserves_all_other_semantic_anchors_even_with_new_matching_hashes(self):
        index = min(self.scopes["recovery_prone_frame_v1"])
        for field in ["groundAnchor", "pelvis", "muzzle", "grip", "supportWidth"]:
            before, after, exceptions, semantics, kwargs = self.candidate_fixture([index])
            if isinstance(semantics[index][field], list):
                semantics[index][field][0] += .25
            else:
                semantics[index][field] += .25
            next(frame for frame in after["frames"] if frame["index"] == index)["anchors"][field] = semantics[index][field]
            self.refresh_hashes(after, exceptions, semantics, index)
            with self.subTest(field=field), self.assertRaisesRegex(ValueError, "ajenos a las puntas"):
                compare(before, after, exceptions=exceptions, semantic_records=semantics, **kwargs)

    def test_recovery_candidate_anchor_record_cannot_disagree_with_semantics(self):
        index = min(self.scopes["recovery_prone_frame_v1"])
        for field in ["carryGrip", "groundAnchor", "pelvis", "muzzle", "grip", "supportWidth"]:
            before, after, exceptions, semantics, kwargs = self.candidate_fixture([index])
            frame = next(frame for frame in after["frames"] if frame["index"] == index)
            if isinstance(frame["anchors"][field], list):
                frame["anchors"][field] = [frame["anchors"][field][0]+.25, *frame["anchors"][field][1:]]
            else:
                frame["anchors"][field] += .25
            with self.subTest(field=field):
                errors = compare(before, after, exceptions=exceptions, semantic_records=semantics, **kwargs)
                self.assertIn("Semántica distinta: índice " + str(index) + ", campo anchors", errors)

    def test_floor_scope_cannot_add_head_or_expand_literal_indices_even_with_matching_witness_sha(self):
        index = min(self.scopes["ik_floor_v1"])
        for change in ["head", "index", "identity"]:
            before, after, exceptions, semantics, kwargs = self.candidate_fixture([index])
            witness = copy.deepcopy(kwargs["ik_floor_witness"])
            if change == "head":
                witness["allowedJoints"][str(index)].append("head")
            elif change == "index":
                witness["allowedJoints"]["0"] = ["left_elbow"]
            else:
                witness["frames"][0]["semantic"]["direction"] = "N"
            kwargs["ik_floor_witness"] = witness
            exceptions["ikFloorWitnessCanonicalSha256"] = canonical_sha(witness)
            with self.subTest(change=change), self.assertRaises(ValueError):
                compare(before, after, exceptions=exceptions, semantic_records=semantics, **kwargs)

    def test_recovery_witness_cannot_relax_pelvis_or_other_anchor_fields(self):
        index = min(self.scopes["recovery_prone_frame_v1"])
        for change in ["pelvisJoint", "groundAnchorField"]:
            before, after, exceptions, semantics, kwargs = self.candidate_fixture([index])
            witness = copy.deepcopy(kwargs["recovery_witness"])
            if change == "pelvisJoint":
                witness["allowedJoints"][str(index)].append("pelvis")
            else:
                witness["allowedFields"].append("groundAnchor")
            kwargs["recovery_witness"] = witness
            exceptions["recoveryWitnessCanonicalSha256"] = canonical_sha(witness)
            with self.subTest(change=change), self.assertRaises(ValueError):
                compare(before, after, exceptions=exceptions, semantic_records=semantics, **kwargs)

    def test_fixed_witness_creators_reject_changed_descriptor_before_reading_original(self):
        for kind, creator in [("ik_floor_v1", create_ik_floor_witness), ("recovery_prone_frame_v1", create_recovery_witness)]:
            descriptor = witness_descriptor(self.witnesses[kind])
            descriptor[next(iter(descriptor))]["allowedJoints"].append("head_unapproved")
            with self.subTest(kind=kind), self.assertRaisesRegex(ValueError, "SHA del descriptor"):
                creator(self.original, Path("missing-original.json"), descriptor)

    def test_check_never_creates_missing_floor_or_recovery_witnesses(self):
        for kind in ["ik_floor_v1", "recovery_prone_frame_v1"]:
            index = min(self.scopes[kind])
            _, _, exceptions, _, _ = self.candidate_fixture([index])
            with self.subTest(kind=kind), self.temporary_directory() as directory:
                root = Path(directory)
                path = root / "exceptions.json"
                path.write_text(json.dumps(exceptions), encoding="utf-8")
                with self.assertRaises(FileNotFoundError):
                    validate_current(root, self.root / "Manifests" / BASELINE_NAME, self.root / "Manifests" / REFERENCES_NAME, path)
                self.assertFalse((root / "Manifests").exists())


class CharacterSocketProjectionTests(unittest.TestCase):
    temporary_directory = CharacterBaselineTests.temporary_directory
    fixture = CharacterBaselineTests.fixture
    baseline = CharacterBaselineTests.baseline
    contracts_fixture = CharacterBaselineTests.contracts_fixture

    def socket_fixture(self):
        entry, pages = self.fixture()
        before = self.baseline(frame_record(entry, pages, 192))
        before["provenance"] = {"commit": "fixture"}
        semantic = semantic_record(entry)
        semantic["joints"]["pelvis"][2] = 32.5
        semantic["pelvis"] = [96.0, 111.5]
        semantic["muzzle"] = [123.0, 98.0]
        semantic["grip"] = [115.0, 102.0]
        semantic["carryGrip"] = [115.0, 102.0]
        semantic["supportWidth"] = 25.5
        after = copy.deepcopy(before)
        candidate = after["frames"][0]
        candidate.update({"anchors": {key: semantic[key] for key in ANCHOR_FIELDS},
                          "semanticSha256": canonical_sha(semantic), "jointsSha256": canonical_sha(semantic["joints"]),
                          "rgbaSha256": {"full": "a" * 64}})
        contract = {"schemaVersion": 2, "kind": "rig_socket_projection_v1", "baselineCanonicalSha256": canonical_sha(before),
                    "canonicalManifest": {"file": "Manifests/canonical.json", "canonicalSha256": "b" * 64},
                    "tools": [{"file": "Tools/rig.py", "sha256": "c" * 64}],
                    "pieceFiles": [{"file": "Pieces/E-arm.png", "sha256": "d" * 64}],
                    "entries": [{"index": 42, "identity": {key: entry[key] for key in ["clip", "stance", "direction", "phase"]},
                                 "reason": "Sockets originales completos corrigen reposo E fase0 y anclas del arma",
                                 "rgbaSha256": copy.deepcopy(candidate["rgbaSha256"]), "joints": copy.deepcopy(semantic["joints"]),
                                 "anchors": copy.deepcopy(candidate["anchors"]), "semantic": copy.deepcopy(semantic),
                                 "semanticSha256": candidate["semanticSha256"], "jointsSha256": candidate["jointsSha256"]}]}
        sources = {key: copy.deepcopy(contract[key]) for key in ["canonicalManifest", "tools", "pieceFiles"]}
        return before, after, contract, {42: semantic}, sources

    def check(self, values, **overrides):
        before, after, contract, semantics, sources = values
        kwargs = {"exceptions": contract, "semantic_records": semantics, "expected_contract_sha256": canonical_sha(contract), "verified_source_hashes": sources}
        kwargs.update(overrides)
        return compare(before, after, **kwargs)

    def test_exact_full_pose_contract_allows_registered_joints_and_weapon_anchors(self):
        self.assertEqual([], self.check(self.socket_fixture()))

    def test_external_hash_is_mandatory_and_cannot_be_learned_from_edited_contract(self):
        values = self.socket_fixture()
        for value in [None, "", "e" * 64]:
            with self.subTest(pin=value), self.assertRaises(ValueError):
                self.check(values, expected_contract_sha256=value)
        approved = canonical_sha(values[2])
        values[2]["entries"][0]["reason"] += " modificado"
        with self.assertRaisesRegex(ValueError, "SHA externo"):
            self.check(values, expected_contract_sha256=approved)

    def test_external_pin_cannot_be_ignored_by_downgrading_to_schema1(self):
        values = self.socket_fixture()
        with self.assertRaisesRegex(ValueError, "downgrade"):
            compare(values[0], values[0], expected_contract_sha256=canonical_sha(values[2]))
        before, after, legacy = CharacterBaselineExceptionTests().raster_fixture()
        with self.assertRaisesRegex(ValueError, "downgrade"):
            compare(before, after, exceptions=legacy, expected_contract_sha256=canonical_sha(values[2]))

    def test_joint_anchor_and_rgba_tampering_are_rejected_after_consistent_runtime_rehash(self):
        for field in ["joint", "anchor", "rgba"]:
            values = self.socket_fixture()
            _, after, _, semantics, _ = values
            actual = semantics[42]
            if field == "joint":
                actual["joints"]["pelvis"][2] += .001
            elif field == "anchor":
                actual["muzzle"][0] += .001
                after["frames"][0]["anchors"]["muzzle"][0] += .001
            else:
                after["frames"][0]["rgbaSha256"]["full"] = "f" * 64
            after["frames"][0]["semanticSha256"] = canonical_sha(actual)
            after["frames"][0]["jointsSha256"] = canonical_sha(actual["joints"])
            with self.subTest(field=field), self.assertRaises(ValueError):
                self.check(values)

    def test_schema_rejects_duplicates_bool_indices_wildcards_and_missing_full_layers(self):
        mutations = [lambda c: c["entries"].append(copy.deepcopy(c["entries"][0])),
                     lambda c: c["entries"][0].update({"index": True}),
                     lambda c: c["entries"][0]["identity"].update({"clip": "*"}),
                     lambda c: c["entries"][0]["identity"].update({"phase": False}),
                     lambda c: c["entries"][0].update({"rgbaSha256": {}}),
                     lambda c: c["entries"][0]["rgbaSha256"].update({"newLayer": "a" * 64}),
                     lambda c: c["entries"][0]["anchors"].pop("muzzle"),
                     lambda c: c.update({"clips": ["*"]}),
                     lambda c: c.update({"schemaVersion": True}),
                     lambda c: c.update({"entries": []}),
                     lambda c: c["tools"].append(copy.deepcopy(c["tools"][0])),
                     lambda c: c["pieceFiles"].append(copy.deepcopy(c["pieceFiles"][0]))]
        for mutate in mutations:
            values = self.socket_fixture()
            mutate(values[2])
            with self.subTest(mutation=mutate), self.assertRaises(ValueError):
                self.check(values)

    def test_literal_semantic_joints_and_anchor_records_must_agree(self):
        for field in ["joints", "anchors", "semanticSha256", "jointsSha256"]:
            values = self.socket_fixture()
            row = values[2]["entries"][0]
            if field == "joints":
                row[field]["pelvis"][0] += .1
            elif field == "anchors":
                row[field]["pelvis"][0] += .1
            else:
                row[field] = "f" * 64
            with self.subTest(field=field), self.assertRaises(ValueError):
                self.check(values)

    def test_unlisted_index_remains_baseline_exact(self):
        values = self.socket_fixture()
        before, after, contract, _, _ = values
        other = copy.deepcopy(before["frames"][0]); other["index"] = 43
        before["frames"].append(other); after["frames"].append(copy.deepcopy(other))
        after["frames"][-1]["rgbaSha256"]["full"] = "e" * 64
        contract["baselineCanonicalSha256"] = canonical_sha(before)
        self.assertIn("RGBA distinto: índice 43, capa full", self.check(values))

    def test_original_header_definition_source_contracts_pngs_and_weapons_remain_protected(self):
        for field in ["headerSha256", "definition", "sourceRegistrations", "originalSources", "weaponFiles"]:
            values = self.socket_fixture()
            values[1][field] = "changed"
            with self.subTest(field=field):
                self.assertIn("Contrato o fuente distintos: " + field, self.check(values))

    def test_source_manifest_tool_and_piece_hashes_require_verified_inputs(self):
        values = self.socket_fixture()
        with self.assertRaises(ValueError):
            self.check(values, verified_source_hashes=None)
        for key in ["canonicalManifest", "tools", "pieceFiles"]:
            values = self.socket_fixture()
            if key == "canonicalManifest":
                values[4][key]["canonicalSha256"] = "f" * 64
            else:
                values[4][key][0]["sha256"] = "f" * 64
            with self.subTest(source=key), self.assertRaisesRegex(ValueError, "manifiesto canónico"):
                self.check(values)

    def test_contract_source_paths_cannot_escape_authorized_roots(self):
        for key, value in [("canonicalManifest", "../outside.json"), ("tools", "Tools/../outside.py"), ("pieceFiles", "Pieces/../../outside.png"), ("tools", "C:/outside.py"), ("pieceFiles", "Pieces\\outside.png")]:
            values = self.socket_fixture()
            record = values[2][key] if key == "canonicalManifest" else values[2][key][0]
            record["file"] = value
            with self.subTest(source=key, path=value), self.assertRaisesRegex(ValueError, "Ruta"):
                self.check(values)

    def test_bool_or_duplicate_actual_indices_are_rejected(self):
        for duplicate in [False, True]:
            values = self.socket_fixture()
            if duplicate:
                values[1]["frames"].append(copy.deepcopy(values[1]["frames"][0]))
            else:
                values[1]["frames"][0]["index"] = True
            with self.subTest(duplicate=duplicate), self.assertRaisesRegex(ValueError, "Índices"):
                self.check(values)

    def test_source_verification_reads_actual_manifest_tools_and_complete_piece_bytes(self):
        with self.temporary_directory() as directory:
            root = Path(directory); (root / "Manifests").mkdir(); (root / "Pieces").mkdir(); (root / "Tools").mkdir()
            manifest = root / "Manifests/canonical.json"; manifest.write_text('{"socket": [1, 2]}', encoding="utf-8")
            tool = root / "Tools/rig.py"; tool.write_bytes(b"original tool\n")
            piece = root / "Pieces/E-arm.png"; Image.new("RGBA", (2, 3), (4, 5, 6, 7)).save(piece)
            values = self.socket_fixture(); contract = values[2]
            contract["canonicalManifest"]["canonicalSha256"] = canonical_sha(read_json(manifest))
            contract["tools"][0]["sha256"] = file_sha(tool); contract["pieceFiles"][0]["sha256"] = file_sha(piece)
            expected = canonical_sha(contract)
            with patch("ValidateNoTraceCharacterBaseline.REPOSITORY", root):
                values[4].update(verify_socket_sources(root, contract, expected))
                self.assertEqual([], self.check(values))
                for file in [manifest, tool, piece]:
                    before = file.read_bytes()
                    if file == manifest:
                        file.write_text('{"socket": [1, 3]}', encoding="utf-8")
                    else:
                        file.write_bytes(before + b"changed")
                    verified = verify_socket_sources(root, contract, expected)
                    with self.subTest(file=file.name), self.assertRaises(ValueError):
                        self.check(values, verified_source_hashes=verified)
                    file.write_bytes(before)

    def test_check_reads_schema2_pin_without_creating_or_updating_preregistered_files(self):
        with self.temporary_directory() as directory:
            root = Path(directory); before, references = self.contracts_fixture(root)
            _, after, contract, semantics, _ = self.socket_fixture()
            before["provenance"] = {"commit": "fixture"}; after["sourceRegistrations"] = copy.deepcopy(before["sourceRegistrations"])
            references = create_references(root, before); contract["baselineCanonicalSha256"] = canonical_sha(before)
            for name, value in [("baseline.json", before), ("references.json", references), ("exceptions.json", contract)]:
                (root / name).write_text(json.dumps(value), encoding="utf-8")
            original = {p.name: p.read_bytes() for p in root.glob("*.json")}
            args = ["baseline", "check", "--root", str(root), "--baseline", str(root / "baseline.json"), "--references", str(root / "references.json"), "--exceptions", str(root / "exceptions.json")]
            with patch("sys.argv", args), self.assertRaisesRegex(ValueError, "expected_contract_sha256"):
                main()
            self.assertEqual(original, {p.name: p.read_bytes() for p in root.glob("*.json")})

    def test_validate_current_routes_schema2_semantics_and_real_file_verification(self):
        with self.temporary_directory() as directory:
            root = Path(directory); before, _ = self.contracts_fixture(root)
            before["provenance"] = {"commit": "fixture"}
            _, after, contract, semantics, _ = self.socket_fixture()
            after["sourceRegistrations"] = copy.deepcopy(before["sourceRegistrations"])
            references = create_references(root, before)
            (root / "Pieces").mkdir(); (root / "Tools").mkdir()
            (root / "Manifests/canonical.json").write_text('{"sockets": [1, 2]}', encoding="utf-8")
            (root / "Tools/rig.py").write_bytes(b"approved rig\n")
            Image.new("RGBA", (2, 3), (4, 5, 6, 7)).save(root / "Pieces/E-arm.png")
            contract["baselineCanonicalSha256"] = canonical_sha(before)
            contract["canonicalManifest"]["canonicalSha256"] = canonical_sha(read_json(root / "Manifests/canonical.json"))
            contract["tools"][0]["sha256"] = file_sha(root / "Tools/rig.py")
            contract["pieceFiles"][0]["sha256"] = file_sha(root / "Pieces/E-arm.png")
            for name, value in [("baseline.json", before), ("references.json", references), ("exceptions.json", contract)]:
                (root / name).write_text(json.dumps(value), encoding="utf-8")

            def candidate_snapshot(actual_root, commit, records):
                self.assertEqual(root, actual_root)
                self.assertEqual("fixture", commit)
                self.assertIsInstance(records, dict)
                records.update(copy.deepcopy(semantics))
                return copy.deepcopy(after)

            kwargs = {"expected_contract_sha256": canonical_sha(contract)}
            args = [root, root / "baseline.json", root / "references.json", root / "exceptions.json"]
            with patch("ValidateNoTraceCharacterBaseline.REPOSITORY", root), patch("ValidateNoTraceCharacterBaseline.snapshot", candidate_snapshot):
                report = validate_current(*args, **kwargs)
                self.assertEqual("PASS", report["status"])
                self.assertEqual(kwargs["expected_contract_sha256"], report["rigSocketContractCanonicalSha256"])
                (root / "Pieces/E-arm.png").write_bytes(b"unapproved piece")
                with self.assertRaisesRegex(ValueError, "manifiesto canónico"):
                    validate_current(*args, **kwargs)


if __name__ == "__main__":
    unittest.main()
