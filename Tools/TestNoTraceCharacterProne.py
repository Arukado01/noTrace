import copy
import hashlib
import json
import math
import unittest
from pathlib import Path
from unittest.mock import patch

from PIL import Image

import GenerateNoTraceCharactersRig as rig
from GenerateNoTraceCharactersActions import body_pose
from GenerateNoTraceCharactersProne import DIAMETERS, ProneBindings, required
from ValidateNoTraceCharacterBaseline import canonical_sha


class CharacterProneTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.root = Path(__file__).resolve().parents[1] / "Art/NoTrace/Characters"
        cls.prone = ProneBindings(cls.root)

    def assert_raster_equal(self, first, second):
        self.assertEqual((192, 192), first.size)
        self.assertEqual("RGBA", first.mode)
        self.assertEqual(hashlib.sha256(first.tobytes()).hexdigest(), hashlib.sha256(second.tobytes()).hexdigest())

    def test_registered_sources_and_crops_load_eight_directions(self):
        self.assertEqual(set(rig.DIRECTIONS), set(self.prone.banks))
        for bank in self.prone.banks.values():
            self.assertTrue(set(rig.NAMES).issubset(bank))
        self.assertEqual(set(rig.DIRECTIONS), set(self.prone.original))

    def test_changed_source_sha_is_rejected_before_image_decode(self):
        manifest = json.loads((self.root / "Manifests/prone-rig-pieces.registration.json").read_text(encoding="utf-8"))
        source = self.prone.workspace / manifest["banks"][0]["file"]
        read_bytes = Path.read_bytes

        def changed_bytes(path):
            original = read_bytes(path)
            return original + b"altered" if path == source else original

        with patch.object(Path, "read_bytes", changed_bytes), patch("GenerateNoTraceCharactersProne.Image.open") as decode:
            with self.assertRaisesRegex(ValueError, "Cambió una fuente"):
                ProneBindings(self.root)
            decode.assert_not_called()

    def view_fixture(self):
        image = Image.new("RGBA", (25, 25))
        image.putpixel((8, 8), (255, 0, 0, 255))
        image.putpixel((18, 8), (0, 255, 0, 255))
        image.putpixel((8, 18), (0, 0, 255, 255))
        entry = {"sourceRect": [0, 0, 25, 25], "proximalPx": [8.5, 8.5], "distalPx": [18.5, 8.5], "uniformScale": 1.0}
        return image, entry

    def test_changed_crop_rgba_sha_is_rejected(self):
        image, entry = self.view_fixture()
        with self.assertRaisesRegex(ValueError, "RGBA de pieza"):
            ProneBindings.read_view(image, entry, "0" * 64)

    def test_degenerate_axis_is_rejected(self):
        image, entry = self.view_fixture()
        entry["distalPx"] = entry["proximalPx"][:]
        with self.assertRaisesRegex(ValueError, "Eje tumbado sin longitud"):
            ProneBindings.read_view(image, entry, hashlib.sha256(image.tobytes()).hexdigest())

    def test_similarity_keeps_two_pivots_and_transverse_scale(self):
        image, entry = self.view_fixture()
        view = ProneBindings.read_view(image, entry, hashlib.sha256(image.tobytes()).hexdigest())
        canvas = Image.new("RGBA", (192, 192))
        factor = self.prone.bone(canvas, view, (60, 50), (60, 80))
        self.assertEqual(3.0, factor)
        centres, spans = [], []
        for colour in [(255, 0, 0, 255), (0, 255, 0, 255), (0, 0, 255, 255)]:
            pixels = [(x, y) for y in range(192) for x in range(192) if canvas.getpixel((x, y)) == colour]
            self.assertTrue(pixels)
            centres.append(tuple(sum(point[axis] for point in pixels) / len(pixels) for axis in range(2)))
            spans.append(tuple(max(point[axis] for point in pixels) - min(point[axis] for point in pixels) + 1 for axis in range(2)))
        for actual, expected in zip(centres, [(60, 50), (60, 80), (30, 50)]):
            self.assertLessEqual(math.dist(actual, expected), 1.5)
        self.assertAlmostEqual(30.0, math.dist(centres[0], centres[1]))
        self.assertAlmostEqual(30.0, math.dist(centres[0], centres[2]))
        self.assertEqual([(3, 3)] * 3, spans)

    def test_central_similarity_maps_pivot_and_equal_perpendicular_axes(self):
        fixtures = [((0, 0), (96, 144), .25, 0), ((4.5, 7.5), (72, 93), .8, math.pi / 2),
                    ((125, 83), (42, 64), 2.5, -math.pi / 3), ((-3, 5), (101, 52), 1.0, math.pi)]

        def mapped(matrix, point):
            return (matrix[0]*point[0]+matrix[1]*point[1]+matrix[2], matrix[3]*point[0]+matrix[4]*point[1]+matrix[5])

        for pivot, target, factor, angle in fixtures:
            with self.subTest(pivot=pivot, factor=factor, angle=angle):
                forward, inverse = rig.similarity_transform(pivot, target, factor, angle)
                self.assertLess(math.dist(mapped(forward, pivot), target), 1e-10)
                self.assertLess(math.dist(mapped(inverse, target), pivot), 1e-10)
                self.assertAlmostEqual(factor, math.hypot(forward[0], forward[3]))
                self.assertAlmostEqual(factor, math.hypot(forward[1], forward[4]))
                self.assertAlmostEqual(0.0, forward[0]*forward[1]+forward[3]*forward[4])
                self.assertAlmostEqual(factor*factor, forward[0]*forward[4]-forward[1]*forward[3])

    def test_retarget_preserves_declared_child_offsets_without_other_branch_changes(self):
        original = {"pelvis": (0, 0, 0), "neck": (0, 0, 5), "head": (1, 1, 7), "backpack": (-1, 0, 4),
                    "right_wrist": (2, -3, 6), "weapon_grip": (3, -3, 6), "weapon_muzzle": (8, -3, 6),
                    "right_ankle": (1, 2, 3), "right_toe": (4, 6, 2), "left_ankle": (-1, 2, 3), "left_toe": (-4, 6, 2)}
        points = copy.deepcopy(original)
        rig.retarget_joint(points, "right_ankle", (11, -2, 5))
        expected = original | {"right_ankle": (11, -2, 5), "right_toe": (14, 2, 4)}
        self.assertEqual(expected, points)
        rig.retarget_joint(points, "right_ankle", (11, -2, 5))
        self.assertEqual(expected, points)
        rig.retarget_joint(points, "neck", (-4, 5, 10))
        expected.update({"neck": (-4, 5, 10), "head": (-3, 6, 12), "backpack": (-5, 5, 9),
                         "right_wrist": (-2, 2, 11), "weapon_grip": (-1, 2, 11), "weapon_muzzle": (4, 2, 11)})
        self.assertEqual(expected, points)
        repeated = copy.deepcopy(original)
        rig.retarget_joint(repeated, "right_ankle", (11, -2, 5))
        rig.retarget_joint(repeated, "neck", (-4, 5, 10))
        self.assertEqual(points, repeated)

    def test_body_frame_dorsal_tracks_torso_and_shoulders_instead_of_world_x(self):
        fixtures = [((10, 0, 0), (7, 3, 0), (7, -3, 0), (0, 0, 1)),
                    ((-10, 0, 0), (-7, 3, 0), (-7, -3, 0), (0, 0, -1)),
                    ((0, 10, 0), (-3, 7, 0), (3, 7, 0), (0, 0, 1)),
                    ((0, 0, 10), (0, 3, 7), (0, -3, 7), (-1, 0, 0)),
                    ((-10, 0, 0), (-7, -3, 0), (-7, 3, 0), (0, 0, 1))]
        for neck, left, right, dorsal in fixtures:
            points = {"pelvis": (0, 0, 0), "neck": neck, "left_shoulder": left, "right_shoulder": right}
            original = copy.deepcopy(points)
            frame = rig.body_frame(points)
            with self.subTest(neck=neck, left=left):
                self.assertLess(math.dist(dorsal, frame["dorsal"]), 1e-10)
                for axis in frame.values():
                    self.assertAlmostEqual(1.0, math.sqrt(sum(value*value for value in axis)))
                self.assertEqual(original, points)

    def test_crouch_run_and_push_retarget_keep_local_foot_offsets(self):
        for clip in ["CrouchRun", "Push"]:
            for phase in range(8):
                points = body_pose(clip, phase)
                for side, sign in [("left", 1), ("right", -1)]:
                    actual = tuple(points[side + "_toe"][axis]-points[side + "_ankle"][axis] for axis in range(3))
                    with self.subTest(clip=clip, phase=phase, side=side):
                        self.assertLess(math.dist((5.0, sign*1.5, -2.0), actual), 1e-10)

    def test_grounded_ik_matches_exact_circle_plane_fixtures(self):
        fixtures = [((0, 0, 10), (10, 0, 0), 13, 0, math.sqrt(69)),
                    ((0, 0, 10), (12, 0, 2), 17, 2, math.sqrt(185))]
        for first, last, bone, x, y in fixtures:
            for sign in [-1, 1]:
                with self.subTest(bone=bone, sign=sign):
                    middle = rig.grounded_joint(first, last, (0, sign, -2), bone)
                    self.assertLess(math.dist((x, sign*y, 0), middle), 1e-10)
                    self.assertAlmostEqual(bone, math.dist(first, middle))
                    self.assertAlmostEqual(bone, math.dist(middle, last))
                    angle = rig.joint_support_angle(first, last, middle)
                    restored = rig.joint_from_support_angle(first, last, angle, bone)
                    self.assertLess(math.dist(restored, middle), 1e-10)
        first, last, bend = (0, 0, 30), (10, 0, 20), (0, 1, 1)
        self.assertEqual(rig.elbow(first, last, bend, 13), rig.grounded_joint(first, last, bend, 13))

    def test_recovery_prone_continuity_includes_both_limits_and_all_eight_projections(self):
        start, end = body_pose("ReviveRise", 0, "Prone"), body_pose("ReviveRise", 7, "Prone")
        for phase, target in [(1e-6, start), (7-1e-6, end)]:
            points = body_pose("ReviveRise", phase, "Prone")
            self.assertEqual(set(target), set(points))
            for joint in target:
                self.assertLess(math.dist(target[joint], points[joint]), 1e-4)
        previous, previous_dorsal = start, rig.body_frame(start)["dorsal"]
        for sample in range(1, 1001):
            points = body_pose("ReviveRise", sample*7/1000, "Prone")
            dorsal = rig.body_frame(points)["dorsal"]
            self.assertGreater(sum(a*b for a, b in zip(previous_dorsal, dorsal)), .999)
            # La altura física convierte 72 px a 46 unidades; la proyección amplifica el paso del mundo.
            self.assertLessEqual(max(point[2] for point in points.values())*46/72, 14+1e-8)
            self.assertGreaterEqual(min(point[2] for point in points.values()), -1e-8)
            self.assertLess(max(math.dist(points[joint], previous[joint]) for joint in points), .5)
            for direction in rig.DIRECTIONS:
                for joint in points:
                    self.assertLess(math.dist(rig.project(points[joint], direction), rig.project(previous[joint], direction)), 1.0)
            previous, previous_dorsal = points, dorsal

    def test_488_semantic_changes_touch_only_union_of_exact_witness_joint_contracts(self):
        manifests = [("pre-urgent-posture-toes-witness-20261018.json", 88),
                     ("pre-urgent-death-child-offset-witness-20261018.json", 56),
                     ("pre-urgent-crouch-push-child-offset-witness-20261018.json", 128),
                     ("pre-urgent-ik-floor-witness-20261018.json", 216),
                     ("pre-urgent-recovery-prone-witness-20261018.json", 48)]
        originals, permitted = {}, {}
        for filename, count in manifests:
            witness = json.loads((self.root / "Manifests" / filename).read_text(encoding="utf-8"))
            self.assertEqual(count, len(witness["frames"]))
            for entry in witness["frames"]:
                original = entry["semantic"]
                allowed = witness["allowedJoints"]
                allowed = allowed[str(entry["index"])] if isinstance(allowed, dict) else allowed
                index = entry["index"]
                if index in originals:
                    self.assertEqual(originals[index], original)
                originals[index] = original
                permitted.setdefault(index, set()).update(allowed)
        for index, original in originals.items():
            current = body_pose(original["clip"], original["phase"], original["stance"])
            changed = {joint for joint in set(original["joints"]) | set(current)
                       if canonical_sha(original["joints"].get(joint)) != canonical_sha(current.get(joint))}
            with self.subTest(index=index, clip=original["clip"]):
                self.assertTrue(changed)
                self.assertTrue(changed <= permitted[index])
        self.assertEqual(488, len(originals))
        baseline = json.loads((self.root / "Manifests/pre-urgent-character-raster-20261018.json").read_text(encoding="utf-8"))
        poses, changed_indices = {}, set()
        for frame in baseline["frames"]:
            key = frame["clip"], frame["phase"], frame["stance"]
            if key not in poses:
                poses[key] = canonical_sha(body_pose(*key))
            if poses[key] != frame["jointsSha256"]:
                changed_indices.add(frame["index"])
        self.assertEqual(set(originals), changed_indices)

    def test_interleaved_incompatible_poses_do_not_inherit_scale_or_state(self):
        fixtures = [("Aim", 3, "Prone"), ("Unconscious", 0, "Standing"), ("Choke", 4, "Standing"),
                    ("CoverIdle", 5, "Standing"), ("Death", 7, "Standing"), ("ReviveRise", 3, "Prone")]
        fresh = ProneBindings(self.root)
        expected = {}
        for clip, phase, stance in fixtures:
            for direction in rig.DIRECTIONS:
                for layer in ["full", "lower", "upper"]:
                    image = fresh.render(body_pose(clip, phase, stance), direction, clip, phase, stance, layer)
                    expected[clip, phase, stance, direction, layer] = hashlib.sha256(image.tobytes()).hexdigest()
        sources = [(view["image"], hashlib.sha256(view["image"].tobytes()).hexdigest())
                   for bank in self.prone.banks.values() for view in bank.values()]
        retained = []
        for clip, phase, stance in reversed(fixtures):
            points = body_pose(clip, phase, stance)
            preserved = copy.deepcopy(points)
            for direction in reversed(rig.DIRECTIONS):
                for layer in ["upper", "full", "lower"]:
                    self.prone.render(body_pose("Grab", 3), "NE", "Grab", 3, "Standing", "cqcArms")
                    image = self.prone.render(points, direction, clip, phase, stance, layer)
                    digest = hashlib.sha256(image.tobytes()).hexdigest()
                    self.assertEqual(expected[clip, phase, stance, direction, layer], digest)
                    retained.append((image, digest))
            self.assertEqual(preserved, points)
        for image, digest in retained + sources:
            self.assertEqual(digest, hashlib.sha256(image.tobytes()).hexdigest())

    def test_death_w7_bound_piece_volume_is_finite_and_boots_stay_compact(self):
        points = body_pose("Death", 7)
        projected = {name: rig.project(point, "W") for name, point in points.items()}
        registered = {id(view["image"]): view for bank in self.prone.banks.values() for view in bank.values()}
        registered.update({id(view["image"]): view for view in self.prone.auxiliary})
        place = rig.uniform_piece
        measured_roles, measured_boots = set(), set()

        def checked_place(canvas, piece, pivot, target, factor, angle=0.0, mip=None):
            self.assertTrue(math.isfinite(factor) and factor > 0)
            isolated = Image.new("RGBA", (192, 192))
            place(isolated, piece, pivot, target, factor, angle, mip)
            bounds = isolated.getchannel("A").point(lambda alpha: 255 if alpha >= 96 else 0).getbbox()
            self.assertIsNotNone(bounds)
            view = registered.get(id(piece), {})
            role = view.get("role", view.get("piece"))
            source_diameter = view.get("diameterSource", view.get("naturalDiameterPixels", {}).get("source"))
            if role in DIAMETERS and source_diameter:
                self.assertLessEqual(source_diameter*factor, 1.4*DIAMETERS[role])
                measured_roles.add(role)
            for side in ["left", "right"]:
                first, second = projected[side + "_ankle"], projected[side + "_toe"]
                midpoint = tuple((first[axis]+second[axis])/2 for axis in range(2))
                if role == side + "_boot" or math.dist(target, midpoint) < 1e-8:
                    limit = math.dist(first, second) + 2*DIAMETERS[side + "_boot"] + 2
                    self.assertLessEqual(max(bounds[2]-bounds[0], bounds[3]-bounds[1]), limit)
                    measured_boots.add(side)
            return place(canvas, piece, pivot, target, factor, angle, mip)

        with patch.object(rig, "uniform_piece", checked_place):
            image = self.prone.render(points, "W", "Death", 7)
        self.assertGreaterEqual(len(measured_roles), 6)
        self.assertEqual({"left", "right"}, measured_boots)
        self.assertEqual((192, 192), image.size)

    def test_recovery_48_endpoints_match_target_rasters_and_joints(self):
        count = 0
        for stance, destination in [("Standing", "Idle"), ("Crouched", "CrouchIdle"), ("Prone", "ProneIdle")]:
            for phase, target_clip in [(0, "Unconscious"), (7, destination)]:
                points = body_pose("ReviveRise", phase, stance)
                expected = body_pose(target_clip, 0)
                self.assertEqual(expected, points)
                preserved = copy.deepcopy(points)
                for direction in rig.DIRECTIONS:
                    count += 1
                    for layer in ["full", "lower", "upper"]:
                        with self.subTest(stance=stance, phase=phase, direction=direction, layer=layer):
                            self.assert_raster_equal(self.prone.render(points, direction, "ReviveRise", phase, stance, layer),
                                                     self.prone.render(expected, direction, target_clip, 0, "Standing", layer))
                self.assertEqual(preserved, points)
        self.assertEqual(48, count)

    def test_48_vertical_endpoints_preserve_original_three_layers(self):
        count = 0
        for clip, phase in [("LieDown", 0), ("StandUp", 7), ("Dive", 0), ("Fall", 0), ("Death", 0), ("ReviveRise", 7)]:
            points = body_pose(clip, phase)
            for direction in rig.DIRECTIONS:
                count += 1
                for layer in ["full", "lower", "upper"]:
                    with self.subTest(clip=clip, phase=phase, direction=direction, layer=layer):
                        self.assert_raster_equal(self.prone.render(points, direction, clip, phase, "Standing", layer),
                                                 rig.render(self.prone.original[direction], points, direction, layer))
        self.assertEqual(48, count)

    def test_unaffected_clips_preserve_three_raster_layers_in_eight_directions(self):
        for clip in ["Idle", "Walk", "Run", "CrouchIdle", "CrouchWalk", "Punch", "Kick", "Grab", "Choke", "Roll"]:
            self.assertFalse(required(clip, "Standing"))
            points = body_pose(clip, 3)
            preserved = copy.deepcopy(points)
            for direction in rig.DIRECTIONS:
                for layer in ["full", "lower", "upper"]:
                    with self.subTest(clip=clip, direction=direction, layer=layer):
                        self.assert_raster_equal(self.prone.render(points, direction, clip, 3, "Standing", layer),
                                                 rig.render(self.prone.original[direction], points, direction, layer))
            self.assertEqual(preserved, points)

    def test_cqc_layers_remain_original(self):
        for clip in ["Grab", "Choke"]:
            points = body_pose(clip, 3)
            for direction in rig.DIRECTIONS:
                for layer in ["cqcBody", "cqcArms"]:
                    with self.subTest(clip=clip, direction=direction, layer=layer):
                        self.assert_raster_equal(self.prone.render(points, direction, clip, 3, "Standing", layer),
                                                 rig.render(self.prone.original[direction], points, direction, layer))

    def test_complete_4544_matrix_has_finite_pose_inputs(self):
        metadata = json.loads((self.root / "Atlas/CharacterAtlasMetadata.json").read_text(encoding="utf-8"))
        frames = metadata["frames"]
        identities = {(entry["clip"], entry["stance"], entry["direction"], entry["phase"]) for entry in frames}
        self.assertEqual(4544, len(frames))
        self.assertEqual(4544, len(identities))
        self.assertEqual(1216, sum(required(entry["clip"], entry["stance"]) for entry in frames))
        for clip, stance, phase in {(entry["clip"], entry["stance"], entry["phase"]) for entry in frames}:
            with self.subTest(clip=clip, stance=stance, phase=phase):
                points = body_pose(clip, phase, stance)
                self.assertTrue(points)
                self.assertTrue(all(len(point) == 3 and all(math.isfinite(value) for value in point) for point in points.values()))
                for side in ["left", "right"]:
                    for first, middle, last, bone in [("shoulder", "elbow", "wrist", 13), ("hip", "knee", "ankle", 17)]:
                        a, b, c = (points[side + "_" + name] for name in [first, middle, last])
                        self.assertGreaterEqual(b[2], -1e-8)
                        self.assertAlmostEqual(bone, math.dist(a, b))
                        self.assertAlmostEqual(bone, math.dist(b, c))


if __name__ == "__main__":
    unittest.main()
