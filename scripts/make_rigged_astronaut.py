"""Makes pbrt_scenes/film_assets/library/nova.glb: Nova, a small astronaut for cartoons (a `rigged` kind: src/shared/cartoon/film_rigged.h; nova.cast names it).

A child in a white suit, with the big head and small body a cartoon's character has, the face in the open behind a bubble of glass. It has the robot's skeleton and clips (idle, walk, wave,
cheer, nod: make_rigged_robot.py), so a film can use the two the same way, and is made of smooth round pieces instead of boxes, each weighted wholly to its joint.

Run it from the repository: python scripts/make_rigged_astronaut.py [OUT.glb]. Standard library only.
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "pbrt_scenes", "film_assets", "library", "nova.glb")
sys.argv = sys.argv[:1]
import make_rigged_robot as robot  # noqa: E402

robot.OUT = out
robot.ARM_UP = 120.0 / 165.0   # (its arms up as a V: lifted the robot's 165 degrees, nearly straight up, they went into its bubble)
robot.JOINTS = [
    ("hips", -1, (0.0, 0.40, 0.0)),
    ("chest", 0, (0.0, 0.54, 0.0)),
    ("head", 1, (0.0, 0.76, 0.0)),
    ("arm.L", 1, (0.17, 0.66, 0.0)),
    ("arm.R", 1, (-0.17, 0.66, 0.0)),
    ("leg.L", 0, (0.08, 0.36, 0.0)),
    ("leg.R", 0, (-0.08, 0.36, 0.0)),
]
SUIT, TRIM, SKIN, DARK, GLASS, CHEEK = 0, 1, 2, 3, 4, 5
robot.MATERIALS = [
    {"name": "suit", "pbrMetallicRoughness": {"baseColorFactor": [0.93, 0.93, 0.9, 1.0], "metallicFactor": 0.0, "roughnessFactor": 0.55}},
    {"name": "trim", "pbrMetallicRoughness": {"baseColorFactor": [0.95, 0.42, 0.1, 1.0], "metallicFactor": 0.0, "roughnessFactor": 0.45}},
    {"name": "skin", "pbrMetallicRoughness": {"baseColorFactor": [0.86, 0.62, 0.48, 1.0], "metallicFactor": 0.0, "roughnessFactor": 0.7}},
    {"name": "dark", "pbrMetallicRoughness": {"baseColorFactor": [0.03, 0.03, 0.05, 1.0], "metallicFactor": 0.0, "roughnessFactor": 0.12}},
    {"name": "glass", "pbrMetallicRoughness": {"baseColorFactor": [1.0, 1.0, 1.0, 1.0], "metallicFactor": 0.0, "roughnessFactor": 0.03}, "extensions": {"KHR_materials_transmission": {"transmissionFactor": 1.0}}},
    {"name": "cheek", "pbrMetallicRoughness": {"baseColorFactor": [0.9, 0.42, 0.36, 1.0], "metallicFactor": 0.0, "roughnessFactor": 0.7}},
]
robot.BOXES = [
    (0, SUIT, (0.0, 0.42, 0.0), (0.13, 0.08, 0.11)),      # hips
    (1, SUIT, (0.0, 0.57, 0.0), (0.16, 0.15, 0.13)),      # body
    (1, TRIM, (0.0, 0.6, 0.128), (0.07, 0.05, 0.012)),    # the patch on its chest
    (1, SUIT, (0.0, 0.58, -0.17), (0.12, 0.13, 0.06)),    # its pack
    (2, SUIT, (0.0, 0.73, 0.0), (0.15, 0.04, 0.15)),      # the collar
    (2, SKIN, (0.0, 0.91, 0.02), (0.165, 0.16, 0.16)),    # its head, big as a cartoon's
    (2, SUIT, (0.0, 0.93, -0.08), (0.2, 0.2, 0.15)),      # the back of the helmet
    (2, DARK, (0.065, 0.93, 0.163), (0.032, 0.042, 0.016)),   # its eyes
    (2, DARK, (-0.065, 0.93, 0.163), (0.032, 0.042, 0.016)),
    (2, CHEEK, (0.105, 0.87, 0.13), (0.03, 0.02, 0.012)),     # its cheeks
    (2, CHEEK, (-0.105, 0.87, 0.13), (0.03, 0.02, 0.012)),
    (2, CHEEK, (0.0, 0.85, 0.165), (0.02, 0.009, 0.008)),     # its mouth
    (2, GLASS, (0.0, 0.92, 0.02), (0.235, 0.225, 0.225)),     # the bubble
    (2, TRIM, (0.0, 1.16, -0.02), (0.016, 0.04, 0.016)),      # its little aerial's tip
    (3, SUIT, (0.2, 0.52, 0.0), (0.05, 0.14, 0.055)),     # arms
    (4, SUIT, (-0.2, 0.52, 0.0), (0.05, 0.14, 0.055)),
    (3, TRIM, (0.205, 0.39, 0.0), (0.055, 0.045, 0.055)),     # gloves
    (4, TRIM, (-0.205, 0.39, 0.0), (0.055, 0.045, 0.055)),
    (5, SUIT, (0.085, 0.2, 0.0), (0.065, 0.16, 0.07)),    # legs
    (6, SUIT, (-0.085, 0.2, 0.0), (0.065, 0.16, 0.07)),
    (5, TRIM, (0.085, 0.045, 0.03), (0.07, 0.045, 0.11)),     # boots
    (6, TRIM, (-0.085, 0.045, 0.03), (0.07, 0.045, 0.11)),
]


def ball(centre, half, rows=12, columns=16):
    """A round piece: the same as the robot's box to the rest of its generator."""
    pos, nor, idx = [], [], []
    for j in range(rows + 1):
        theta = math.pi * j / rows
        for i in range(columns + 1):
            phi = 2.0 * math.pi * i / columns
            d = (math.sin(theta) * math.cos(phi), math.cos(theta), math.sin(theta) * math.sin(phi))
            pos.append([centre[k] + d[k] * half[k] for k in range(3)])
            n = [d[k] / max(1e-6, half[k]) for k in range(3)]
            l = math.sqrt(sum(x * x for x in n)) or 1.0
            nor.append([x / l for x in n])
    for j in range(rows):
        for i in range(columns):
            a, b = j * (columns + 1) + i, (j + 1) * (columns + 1) + i
            idx += [a, a + 1, b, a + 1, b + 1, b]
    return pos, nor, idx


robot.box = ball

if __name__ == "__main__":
    robot.main()
