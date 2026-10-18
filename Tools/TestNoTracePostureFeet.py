"""Valida pies de las transiciones sin renderizar ni depender de archivos de QA."""
import json
import math

from GenerateNoTraceCharactersActions import blend_toes, body_pose, prone_pose
from GenerateNoTraceCharactersRig import DIRECTIONS, project, skeleton


def assert_point(actual, expected):
    assert math.dist(actual, expected) < 1e-9, (actual, expected)


def test_endpoints():
    fixtures = [
        ("LieDown", 0, skeleton("Idle", 0)), ("LieDown", 7, prone_pose(7)),
        ("StandUp", 0, prone_pose(0)), ("StandUp", 7, skeleton("Idle", 7)),
        ("Dive", 0, skeleton("Idle", 0)), ("Dive", 7, prone_pose(7)),
    ]
    for clip, phase, expected in fixtures:
        actual = body_pose(clip, phase)
        for side in ["left", "right"]:
            for joint in ["ankle", "toe"]:
                key = side + "_" + joint
                assert_point(actual[key], expected[key])


def test_relative_anchor_and_angular_fixtures():
    first, second, target = {}, {}, {}
    for side in ["left", "right"]:
        first[side + "_ankle"], first[side + "_toe"] = (10, 20, 2), (14, 20, 1)
        second[side + "_ankle"], second[side + "_toe"] = (-30, 40, 5), (-30, 44, 4)
        target[side + "_ankle"] = (80, -50, 7)
    result = blend_toes(dict(target), first, second, 0.5)
    for side in ["left", "right"]:
        assert_point(result[side + "_toe"], (80 + math.sqrt(8), -50 + math.sqrt(8), 6))
        assert result[side + "_ankle"] == target[side + "_ankle"]
    for side in ["left", "right"]:
        second[side + "_toe"] = (-34, 40, 4)
    result = blend_toes(dict(target), first, second, 0.5)
    assert_point(result["left_toe"], (80, -46, 6))
    assert_point(result["right_toe"], (80, -54, 6))
    for amount, offset in [(0.0, (4, 0, -1)), (1.0, (-4, 0, -1))]:
        result = blend_toes(dict(target), first, second, amount)
        for side in ["left", "right"]:
            assert_point(result[side + "_toe"], tuple(a + b for a, b in zip(target[side + "_ankle"], offset)))


def test_continuous_transitions():
    metrics = {"samples": 0, "minimumLength": math.inf, "maximumLength": 0.0,
               "minimumProjectedPixels": math.inf, "minimumSupportZ": math.inf,
               "maximumRelativeStep": 0.0, "maximumWorldStep": 0.0}
    for clip in ["LieDown", "StandUp", "Dive"]:
        previous = None
        for sample in range(701):
            phase = sample / 100.0
            points = body_pose(clip, phase)
            assert all(math.isfinite(value) for point in points.values() for value in point), (clip, phase)
            offsets = {}
            for side, sign in [("left", 1.0), ("right", -1.0)]:
                ankle, toe = points[side + "_ankle"], points[side + "_toe"]
                offset = tuple(b - a for a, b in zip(ankle, toe))
                offsets[side] = offset
                size = math.dist(ankle, toe)
                assert 4.2426 <= size <= 5.5902, (clip, phase, side, size)
                assert sign * offset[1] >= 1.0 - 1e-9, (clip, phase, side, "Giro hacia dentro")
                assert min(ankle[2], toe[2]) >= -1e-9, (clip, phase, side, "Apoyo negativo")
                metrics["minimumLength"] = min(metrics["minimumLength"], size)
                metrics["maximumLength"] = max(metrics["maximumLength"], size)
                metrics["minimumSupportZ"] = min(metrics["minimumSupportZ"], ankle[2], toe[2])
                for direction in DIRECTIONS:
                    projected = math.dist(project(ankle, direction), project(toe, direction))
                    assert projected > 2.9, (clip, phase, side, direction, projected)
                    metrics["minimumProjectedPixels"] = min(metrics["minimumProjectedPixels"], projected)
                if previous:
                    relative_step = math.dist(offset, previous[0][side])
                    world_step = math.dist(toe, previous[1][side + "_toe"])
                    assert relative_step < 0.1, (clip, phase, side, "Salto relativo", relative_step)
                    assert world_step < 0.5, (clip, phase, side, "Salto mundial", world_step)
                    metrics["maximumRelativeStep"] = max(metrics["maximumRelativeStep"], relative_step)
                    metrics["maximumWorldStep"] = max(metrics["maximumWorldStep"], world_step)
            previous = offsets, points
            metrics["samples"] += 1
    return metrics


if __name__ == "__main__":
    test_endpoints()
    test_relative_anchor_and_angular_fixtures()
    print(json.dumps({"status": "PASS", **test_continuous_transitions()}))
