import copy
import hashlib
import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from PIL import Image

from ValidateNoTraceCharacterBaseline import REFERENCES_NAME, REGISTRATION_FILES, TOE_IDENTITIES, TOE_WITNESS_NAME, canonical_sha, compare, create_references, file_sha, frame_record, main, read_json, registration_contracts, semantic_record, validate_toe_witness


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


if __name__ == "__main__":
    unittest.main()
