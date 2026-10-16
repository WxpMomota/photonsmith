"""Tests for room_outline.py and the fit's use of it: rooms that are not one rectangle (an L, an alcove) are "photographed" with exact ray casting (no model) and the walls must come out as the
outline the room has; plain boxes must still be boxes.

Run with any Python that has numpy:

    python -m unittest tools/photo_to_room/test_room_outline.py
"""
import math
import os
import sys
import unittest

try:
    import numpy as np
    HAVE_NUMPY = True
except ImportError:
    HAVE_NUMPY = False

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
if HAVE_NUMPY:
    import room_fit
    import room_outline
    from test_room_fit import photograph, srgb

HEIGHT = 2.7

# an L: the room x -2.2..1.8, z -0.5..4.5 with the corner x 0.3..1.8, z 2.5..4.5 taken out. Walls as (axis, value, lo, hi): axis 0 is the wall x = value spanning z lo..hi.
L_RECTS = [(-2.2, 0.3, -0.5, 4.5), (0.3, 1.8, -0.5, 2.5)]
L_WALLS = [(0, -2.2, -0.5, 4.5), (2, 4.5, -2.2, 0.3), (0, 0.3, 2.5, 4.5), (2, 2.5, 0.3, 1.8), (0, 1.8, -0.5, 2.5), (2, -0.5, -2.2, 1.8)]
# an alcove 1.6 wide and 1.2 deep in the middle of the back wall of the room x -2..2, z -0.5..4
ALCOVE_RECTS = [(-2.0, 2.0, -0.5, 4.0), (-0.8, 0.8, 4.0, 5.2)]
ALCOVE_WALLS = [(0, -2.0, -0.5, 4.0), (2, 4.0, -2.0, -0.8), (0, -0.8, 4.0, 5.2), (2, 5.2, -0.8, 0.8), (0, 0.8, 4.0, 5.2), (2, 4.0, 0.8, 2.0), (0, 2.0, -0.5, 4.0), (2, -0.5, -2.0, 2.0)]


def photograph_outline(rects, walls, width=240, height=180, hfov=70.0, yaw=0.0, pitch=0.0, cam_height=1.4, noise=0.0, seed=1):
    """(depth, rgb uint8) of a room that is the union of axis-aligned `rects` (x0, x1, z0, z1), bounded by `walls`, 2.7 m high, from (0, cam_height, 0)."""
    f = (width / 2.0) / math.tan(math.radians(hfov) / 2.0)
    u = np.arange(width) + 0.5
    v = np.arange(height) + 0.5
    uu, vv = np.meshgrid(u, v)
    cam = np.stack([(uu - width / 2.0) / f, -(vv - height / 2.0) / f, np.ones_like(uu)], axis=-1)
    cp, sp = math.cos(math.radians(pitch)), math.sin(math.radians(pitch))
    Rpitch = np.array([[1, 0, 0], [0, cp, -sp], [0, sp, cp]])
    cy, sy = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
    Ryaw = np.array([[cy, 0, sy], [0, 1, 0], [-sy, 0, cy]])
    d = cam @ (Ryaw @ Rpitch).T
    origin = np.array([0.0, cam_height, 0.0])
    best = np.full(d.shape[:2], np.inf)
    colour = np.zeros(d.shape)

    def inside(x, z):
        ok = np.zeros(x.shape, bool)
        for x0, x1, z0, z1 in rects:
            ok |= (x >= x0 - 1e-6) & (x <= x1 + 1e-6) & (z >= z0 - 1e-6) & (z <= z1 + 1e-6)
        return ok

    def consider(t, valid, rgb):
        nonlocal best, colour
        better = valid & (t > 0) & (t < best)
        best = np.where(better, t, best)
        colour = np.where(better[..., None], np.array(rgb), colour)

    with np.errstate(divide="ignore", invalid="ignore"):
        for i, (axis, value, lo, hi) in enumerate(walls):
            t = (value - origin[0 if axis == 0 else 2]) / d[..., 0 if axis == 0 else 2]
            hit = origin + d * t[..., None]
            other = hit[..., 2] if axis == 0 else hit[..., 0]
            valid = (other >= lo - 1e-6) & (other <= hi + 1e-6) & (hit[..., 1] >= 0) & (hit[..., 1] <= HEIGHT)
            consider(t, valid, srgb((120 + 15 * i, 110, 100 - 8 * i)))
        for level, rgb in ((0.0, srgb((150, 110, 70))), (HEIGHT, srgb((235, 235, 235)))):
            t = (level - origin[1]) / d[..., 1]
            hit = origin + d * t[..., None]
            consider(t, inside(hit[..., 0], hit[..., 2]), rgb)
    depth = best.copy()
    if noise:
        depth = depth * (1.0 + noise * np.random.default_rng(seed).standard_normal(depth.shape))
    return depth, (np.clip(colour, 0, 1) * 255 + 0.5).astype(np.uint8)


def fit(rects, walls, **kw):
    depth, rgb = photograph_outline(rects, walls, **kw)
    return room_fit.fit_room(depth, rgb, 70.0, return_geometry=True)


def close(a, b, tol=0.3):
    return abs(a - b) <= tol


def has_vertex(outline, x, z, tol=0.3):
    return any(close(px, x, tol) and close(pz, z, tol) for px, pz in outline)


@unittest.skipUnless(HAVE_NUMPY, "numpy is not installed")
class OutlineTests(unittest.TestCase):
    def test_an_l_shaped_room_gets_an_outline_of_six_walls(self):
        result, geometry = fit(L_RECTS, L_WALLS)
        outline = result["room"].get("outline")
        self.assertIsNotNone(outline, "the L is not a box: %s %s" % (result["room"], result["notes"]))
        self.assertEqual(len(outline), 6)
        # (the Scene Builder's frame is the photo's mirrored in x)
        for x, z in ((2.2, -0.5), (2.2, 4.5), (-0.3, 4.5), (-0.3, 2.5), (-1.8, 2.5), (-1.8, -0.5)):
            self.assertTrue(has_vertex(outline, x, z), "the L has a corner at (%.1f, %.1f); outline %s" % (x, z, outline))
        names = [s["name"] for s in result["surfaces"]]
        self.assertEqual(names[:2], ["Floor", "Ceiling"])
        self.assertEqual(len(names), 8)
        self.assertTrue(any("not a plain box" in n for n in result["notes"]))

    def test_an_alcove_gets_its_own_walls(self):
        result, _ = fit(ALCOVE_RECTS, ALCOVE_WALLS)
        outline = result["room"].get("outline")
        self.assertIsNotNone(outline, "the alcove is not a box: %s" % result["notes"])
        self.assertEqual(len(outline), 8)
        for x, z in ((0.8, 5.2), (-0.8, 5.2), (0.8, 4.0), (-0.8, 4.0)):
            self.assertTrue(has_vertex(outline, x, z, 0.35), "the alcove has a corner at (%.1f, %.1f); outline %s" % (x, z, outline))

    def test_the_l_survives_a_turned_camera_and_noisy_depth(self):
        result, _ = fit(L_RECTS, L_WALLS, yaw=12.0, pitch=5.0, noise=0.01)
        outline = result["room"].get("outline")
        self.assertIsNotNone(outline, result["notes"])
        self.assertGreaterEqual(len(outline), 6)

    def test_a_plain_box_stays_a_box(self):
        for kw in ({}, {"yaw": 20.0, "pitch": 10.0}):
            depth, rgb = photograph(window=False, **dict({"yaw": 0.0, "pitch": 0.0}, **kw))
            result = room_fit.fit_room(depth, rgb, 70.0)
            self.assertNotIn("outline", result["room"], "a box photographed %s has no outline" % kw)
            self.assertEqual([s["name"] for s in result["surfaces"]], ["Floor", "Ceiling", "Left wall", "Right wall", "Back wall", "Front wall"])

    def test_the_outline_can_be_switched_off(self):
        depth, rgb = photograph_outline(L_RECTS, L_WALLS)
        result = room_fit.fit_room(depth, rgb, 70.0, allow_outline=False)
        self.assertNotIn("outline", result["room"])
        self.assertEqual([s["name"] for s in result["surfaces"]], ["Floor", "Ceiling", "Left wall", "Right wall", "Back wall", "Front wall"])

    def test_every_wall_slab_faces_into_the_room_and_closes_it(self):
        result, _ = fit(L_RECTS, L_WALLS)
        walls = [s for s in result["surfaces"] if s["name"].startswith("Wall")]
        self.assertEqual(len(walls), 6)
        for s in walls:
            self.assertIn("face", s)
            self.assertTrue(all(a < b for a, b in zip(s["min"], s["max"])), s["name"])
            axis, sign = s["face"]
            self.assertIn(axis, (0, 2))
            self.assertIn(sign, (-1, 1))
        # a point inside the L (in the Scene Builder's mirrored frame the long arm is x -0.3..2.2, z -0.5..4.5) is on the side every wall's visible face looks
        inside = (0.7, 1.0, 1.0)
        for s in walls:
            axis, sign = s["face"]
            plane = s["max"][axis] if sign > 0 else s["min"][axis]
            self.assertGreater((inside[axis] - plane) * sign, -0.05, "%s faces away from the room" % s["name"])

    def test_the_helpers_agree_with_the_geometry(self):
        poly = [(0.0, 0.0), (4.0, 0.0), (4.0, 2.0), (2.0, 2.0), (2.0, 4.0), (0.0, 4.0)]
        self.assertGreater(room_outline.signed_area(poly), 0)
        self.assertAlmostEqual(room_outline.polygon_area(poly), 12.0, places=6)
        self.assertTrue(room_outline.is_simple(poly))
        self.assertTrue(room_outline.contains(poly, (1.0, 1.0)))
        self.assertFalse(room_outline.contains(poly, (3.0, 3.0)))
        bowtie = [(0.0, 0.0), (2.0, 2.0), (2.0, 0.0), (0.0, 2.0)]
        self.assertFalse(room_outline.is_simple(bowtie))
        walls = room_outline.walls_from_polygon(poly, 0.1)
        self.assertEqual(len(walls), 6)
        by = {(w["axis"], round(w["value"], 3), round(w["lo"], 3)): w for w in walls}
        self.assertEqual(by[(2, 0.0, 0.0)]["side"], 1, "the wall z = 0 has the room at larger z")
        self.assertEqual(by[(0, 4.0, 0.0)]["side"], -1, "the wall x = 4 has the room at smaller x")
        self.assertEqual(by[(2, 2.0, 2.0)]["side"], -1, "the inner wall z = 2 (x 2..4) has the room at smaller z")
        # the inner (reflex) corner at (2, 2): neither wall runs past it into the room
        inner_x = by[(0, 2.0, 2.0)]   # x = 2, z 2..4: its lo end (z = 2) is the reflex corner
        inner_z = by[(2, 2.0, 2.0)]   # z = 2, x 2..4: its lo end (x = 2) is the reflex corner
        self.assertEqual(inner_x["ext_lo"], 0.0)
        self.assertEqual(inner_z["ext_lo"], 0.0)
        self.assertEqual(inner_x["ext_hi"], 0.1)   # the far end (0, 4) corner is convex

    def test_too_few_samples_or_one_wall_gives_no_outline(self):
        self.assertIsNone(room_outline.fit_outline([None] * 30, (-2, 2, -1, 4), (0, -1)))
        profile = [(1.0 + 0.01 * i, 3.0, 2) for i in range(40)]   # a single wall z = 3
        self.assertIsNone(room_outline.fit_outline(profile, (-2, 2, -1, 3.0), (0, -1)))


if __name__ == "__main__":
    unittest.main()
