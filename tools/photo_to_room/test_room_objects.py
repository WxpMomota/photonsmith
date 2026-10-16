"""Tests for room_objects.py: furniture of known size and place is "photographed" in the analytic room of test_room_fit.py, and its placement must come out right.

Run with any Python that has numpy:

    python -m unittest tools/photo_to_room/test_room_objects.py
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
    import room_objects
    from test_room_fit import photograph

# a cabinet 0.8 wide, 0.9 tall and 0.8 deep, standing on the floor ahead and a little to the left: x -0.5..0.3, z 2.0..2.8 in the photographed world (+x to the right)
CABINET = ((-0.5, 0.0, 2.0), (0.3, 0.9, 2.8))
MESH = (1.0, 2.0, 1.0)   # what TripoSR makes: about 2 tall, centred, standing on y = 0, front towards +z


def fit_with_object(obstacle=CABINET, **kw):
    depth, rgb, mask = photograph(yaw=0.0, pitch=0.0, window=False, obstacle=obstacle, want_mask=True, **kw)
    result, geometry = room_fit.fit_room(depth, rgb, 70.0, return_geometry=True)
    return result, geometry, mask


@unittest.skipUnless(HAVE_NUMPY, "numpy is not installed")
class RoomObjectsTests(unittest.TestCase):
    def test_the_furniture_stands_where_it_stood_and_is_as_tall_as_it_was(self):
        result, geometry, mask = fit_with_object()
        place = room_objects.place_object(geometry, mask)
        self.assertIsNotNone(place)
        self.assertAlmostEqual(place["bottom"], 0.0, delta=0.01)
        self.assertAlmostEqual(place["height"], 0.9, delta=0.12)
        self.assertAlmostEqual(place["width"], 0.8, delta=0.25)
        final = room_objects.finalize_object(place, MESH, geometry["box"])
        # the middle of the cabinet is x = -0.1, z = 2.4 in the photographed world: x is mirrored for the Scene Builder
        self.assertAlmostEqual(final["position"][0], 0.1, delta=0.3)
        self.assertAlmostEqual(final["position"][1], 0.0, delta=0.01)
        self.assertAlmostEqual(final["position"][2], 2.4, delta=0.4)
        self.assertAlmostEqual(final["size"][1], 0.9, delta=0.15)
        self.assertAlmostEqual(final["scale"] * MESH[1], final["size"][1], delta=1e-3)

    def test_an_object_whose_base_is_in_the_picture_is_measured_from_its_base(self):
        far = ((-0.5, 0.0, 3.6), (0.3, 0.9, 4.0))   # far enough that its foot is inside the picture
        _, geometry, mask = fit_with_object(obstacle=far)
        h = geometry["ok"].shape[0] * geometry["step"]
        self.assertFalse(mask[h - 3:].any(), "the test object is not cut off by the picture's edge")
        place = room_objects.place_object(geometry, mask)
        self.assertIsNotNone(place)
        self.assertAlmostEqual(place["bottom"], 0.0, delta=0.12)
        self.assertAlmostEqual(place["height"], 0.9, delta=0.15)

    def test_it_faces_the_camera_and_lines_up_with_the_room(self):
        _, geometry, mask = fit_with_object()
        final = room_objects.finalize_object(room_objects.place_object(geometry, mask), MESH, geometry["box"])
        self.assertAlmostEqual(abs(final["yaw"]), 180.0, delta=0.01)   # front towards -z, where the camera is, and snapped to the room's axis

    def test_the_facing_turn_matches_the_builders_rotation(self):
        # an object far to the Builder's +x (the photo's left) and ahead faces back towards the camera at the origin: a turn of `yaw` about +y takes the front (0, 0, 1) to (sin, cos)
        place = {"front": (-2.0, 4.0), "dir": (-2.0 / math.hypot(2, 4), 4.0 / math.hypot(2, 4)), "bottom": 0.0, "height": 1.0, "width": 0.5}
        final = room_objects.finalize_object(place, MESH, (-5.0, 5.0, -1.0, 8.0, 3.0))
        px, _, pz = final["position"]
        self.assertGreater(px, 0.0, "mirrored: the photo's left is +x")
        to_camera = np.array([-px, -pz]) / math.hypot(px, pz)
        facing = np.array([math.sin(math.radians(final["yaw"])), math.cos(math.radians(final["yaw"]))])
        self.assertGreater(float(facing @ to_camera), 0.99, "yaw %.1f degrees" % final["yaw"])

    def test_an_object_in_the_middle_of_a_wall_direction_is_not_snapped(self):
        place = {"front": (3.0, 3.0), "dir": (math.sqrt(0.5), math.sqrt(0.5)), "bottom": 0.0, "height": 1.0, "width": 0.5}
        final = room_objects.finalize_object(place, MESH, (-5.0, 5.0, -1.0, 8.0, 3.0))
        self.assertAlmostEqual(abs(final["yaw"]), 135.0, delta=0.01)   # 45 degrees from every wall: left as it is

    def test_the_height_decides_the_size_not_a_narrow_view(self):
        place = {"front": (0.0, 3.0), "dir": (0.0, 1.0), "bottom": 0.0, "height": 1.0, "width": 0.1}   # a thin sliver of a wide object
        final = room_objects.finalize_object(place, MESH, (-5.0, 5.0, -1.0, 8.0, 3.0))
        self.assertGreaterEqual(final["scale"], 0.88 * 0.5 * 0.999)   # the height alone says 0.5; a thin view of it may only take 12% off
        self.assertLessEqual(final["scale"], 0.5 * 1.0001)

    def test_it_stays_inside_the_room(self):
        place = {"front": (9.0, 9.0), "dir": (0.7, 0.7), "bottom": 0.0, "height": 1.0, "width": 0.5}
        final = room_objects.finalize_object(place, MESH, (-2.0, 2.0, -0.4, 4.0, 2.7))
        self.assertLessEqual(abs(final["position"][0]), 2.0)
        self.assertLessEqual(final["position"][2], 4.0)

    def test_too_little_depth_is_refused(self):
        _, geometry, _ = fit_with_object()
        h, w = geometry["ok"].shape
        tiny = np.zeros((h * geometry["step"], w * geometry["step"]), dtype=bool)
        tiny[10:12, 10:12] = True
        self.assertIsNone(room_objects.place_object(geometry, tiny))

    def test_something_on_a_table_stands_above_the_floor(self):
        # a 0.3 m vase on a 0.7 m high table top (a box 0.7 high, the vase on its top)
        _, geometry, _ = fit_with_object(obstacle=((-0.2, 0.7, 2.2), (0.2, 1.0, 2.6)))
        depth, rgb, mask = photograph(yaw=0.0, pitch=0.0, window=False, obstacle=((-0.2, 0.7, 2.2), (0.2, 1.0, 2.6)), want_mask=True)
        place = room_objects.place_object(geometry, mask)
        self.assertIsNotNone(place)
        self.assertAlmostEqual(place["bottom"], 0.7, delta=0.1)
        self.assertAlmostEqual(place["height"], 0.3, delta=0.1)


# a table 1.2 x 0.8 with a 5 cm top at 0.75 m and four thin legs, ahead of the camera: x -0.6..0.6, z 2.2..3.0
TABLE = [((-0.6, 0.70, 2.2), (0.6, 0.75, 3.0)),
         ((-0.58, 0.0, 2.22), (-0.52, 0.70, 2.28)), ((0.52, 0.0, 2.22), (0.58, 0.70, 2.28)),
         ((-0.58, 0.0, 2.92), (-0.52, 0.70, 2.98)), ((0.52, 0.0, 2.92), (0.58, 0.70, 2.98))]


@unittest.skipUnless(HAVE_NUMPY, "numpy is not installed")
class MeasuredFurnitureTests(unittest.TestCase):
    def test_a_table_is_a_top_the_size_of_the_visible_top_on_four_legs_to_the_floor(self):
        _, geometry, mask = fit_with_object(obstacle=TABLE)
        parts = room_objects.table_parts(geometry, mask, "Table 1")
        self.assertIsNotNone(parts)
        self.assertEqual(len(parts), 5)
        top = parts[0]
        self.assertEqual(top["name"], "Table 1 top")
        self.assertAlmostEqual(top["max"][1], 0.75, delta=0.08)
        self.assertAlmostEqual(top["max"][0] - top["min"][0], 1.2, delta=0.25)
        self.assertAlmostEqual(top["max"][2] - top["min"][2], 0.8, delta=0.25)
        self.assertAlmostEqual((top["min"][2] + top["max"][2]) / 2, 2.6, delta=0.25)
        for leg in parts[1:]:
            self.assertEqual(leg["min"][1], 0.0, "a leg stands on the floor")
            self.assertAlmostEqual(leg["max"][1], top["min"][1], delta=1e-3)   # and reaches the underside of the top
            self.assertGreater(leg["max"][0] - leg["min"][0], 0.02)
        for part in parts:
            self.assertTrue(all(a < b for a, b in zip(part["min"], part["max"])))
            self.assertTrue(all(0.03 <= c <= 0.9 for c in part["color"]))

    def test_a_table_the_photo_barely_shows_is_left_to_the_3d_model(self):
        _, geometry, mask = fit_with_object(obstacle=TABLE)
        tiny = np.zeros_like(mask)
        ys, xs = np.nonzero(mask)
        tiny[ys[:30], xs[:30]] = True
        self.assertIsNone(room_objects.table_parts(geometry, tiny))

    def test_floor_standing_furniture_stands_on_the_floor_whatever_its_legs_showed(self):
        seat = ((-0.2, 0.40, 2.5), (0.2, 0.65, 2.9))   # a chair seen as a seat and the foot of its back at 0.4 m: its legs are too thin for a depth map
        _, geometry, mask = fit_with_object(obstacle=seat)
        self.assertAlmostEqual(room_objects.place_object(geometry, mask)["bottom"], 0.4, delta=0.1, msg="without a label the lowest point is believed")
        placed = room_objects.place_object(geometry, mask, label="chair")
        self.assertEqual(placed["bottom"], 0.0)
        self.assertAlmostEqual(placed["height"], 0.65, delta=0.1)
        # a vase on a table is not on the floor
        vase = ((-0.15, 0.80, 2.4), (0.15, 1.15, 2.8))   # (a table lamp's size: a small thing is too few pixels to place)
        _, geometry, mask = fit_with_object(obstacle=vase)
        self.assertAlmostEqual(room_objects.place_object(geometry, mask, label="vase")["bottom"], 0.8, delta=0.1)

    def test_a_block_against_the_wall_is_as_deep_as_the_gap_to_the_wall(self):
        fridge = ((-0.3, 0.0, 3.5), (0.3, 1.8, 3.9))   # its visible front is at z = 3.5; the back wall is at 4.5
        _, geometry, mask = fit_with_object(obstacle=fridge)
        place = room_objects.place_object(geometry, mask, label="refrigerator")
        parts = room_objects.block_parts(geometry, mask, "refrigerator", place, "Refrigerator 1")
        self.assertEqual(len(parts), 1)
        b = parts[0]
        self.assertAlmostEqual(b["max"][1], 1.8, delta=0.15)
        self.assertAlmostEqual(b["max"][0] - b["min"][0], 0.6, delta=0.2)
        self.assertAlmostEqual(b["min"][2], 3.5, delta=0.15)
        self.assertAlmostEqual(b["max"][2], 4.5, delta=0.15, msg="out to the wall just behind it")
        self.assertEqual(b["min"][1], 0.0)

    def test_a_block_with_nothing_behind_it_gets_the_typical_depth(self):
        cabinet = ((-0.3, 0.0, 2.0), (0.3, 0.9, 2.2))   # a wide thin front: 0.6 x 0.9, only its front seen, nothing near the back wall
        _, geometry, mask = fit_with_object(obstacle=cabinet)
        place = room_objects.place_object(geometry, mask, label="cabinet-merged")
        b = room_objects.block_parts(geometry, mask, "cabinet-merged", place, "Cabinet 1")[0]
        self.assertAlmostEqual(b["max"][2] - b["min"][2], 0.55, delta=0.15)   # (the kind's typical depth; the top is seen a little)


@unittest.skipUnless(HAVE_NUMPY, "numpy is not installed")
class RelightTextureTests(unittest.TestCase):
    @staticmethod
    def linear(v):
        v = v / 255.0
        return v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4

    def test_the_exposure_scales_linear_light_and_keeps_the_hue(self):
        tex = np.array([[[100, 60, 30], [128, 128, 128]]], dtype=np.uint8)
        out = room_objects.relight_texture(tex, 2.0)
        for before, after in ((tex[0, 0], out[0, 0]), (tex[0, 1], out[0, 1])):
            ratios = [self.linear(float(a)) / self.linear(float(b)) for a, b in zip(after, before)]
            for r in ratios:
                self.assertAlmostEqual(r, 2.0, delta=0.1)

    def test_too_bright_is_scaled_down_as_a_whole_not_clipped_per_channel(self):
        tex = np.array([[[255, 200, 100]]], dtype=np.uint8)
        out = room_objects.relight_texture(tex, 3.0)[0, 0]
        lin = [self.linear(float(v)) for v in out]
        self.assertLessEqual(max(lin), 0.9 + 0.01)
        before = [self.linear(float(v)) for v in tex[0, 0]]
        self.assertAlmostEqual(lin[1] / lin[0], before[1] / before[0], delta=0.03)   # the red : green ratio survives
        self.assertAlmostEqual(lin[2] / lin[0], before[2] / before[0], delta=0.03)

    def test_an_exposure_of_one_changes_nothing(self):
        tex = np.random.default_rng(3).integers(0, 200, size=(4, 4, 3)).astype(np.uint8)   # (below the 0.9 cap, which also applies at an exposure of one)
        self.assertTrue(np.all(np.abs(room_objects.relight_texture(tex, 1.0).astype(int) - tex.astype(int)) <= 1))


@unittest.skipUnless(HAVE_NUMPY, "numpy is not installed")
class ChooseObjectsTests(unittest.TestCase):
    def seg(self, label, score, box, shape=(100, 100)):
        m = np.zeros(shape, dtype=bool)
        y0, y1, x0, x1 = box
        m[y0:y1, x0:x1] = True
        return {"label": label, "score": score, "mask": m}

    def test_furniture_is_chosen_biggest_first_and_the_rest_is_left(self):
        segs = [self.seg("wall-other-merged", 0.99, (0, 100, 0, 100)), self.seg("chair", 0.9, (60, 80, 10, 30)), self.seg("couch", 0.9, (50, 90, 40, 90)),
                self.seg("person", 0.99, (10, 50, 10, 30)), self.seg("potted plant", 0.95, (20, 40, 70, 80))]
        got = room_objects.choose_objects(segs)
        self.assertEqual([s["label"] for s in got], ["couch", "chair", "potted plant"])

    def test_small_unsure_and_overlapping_segments_are_dropped(self):
        segs = [self.seg("chair", 0.9, (0, 2, 0, 2)), self.seg("chair", 0.3, (10, 40, 10, 40)), self.seg("couch", 0.9, (50, 90, 40, 90)), self.seg("bench", 0.9, (55, 90, 45, 90))]
        got = room_objects.choose_objects(segs)
        self.assertEqual([s["label"] for s in got], ["couch"], "the bench is the same pixels as the couch")

    def test_there_is_a_limit(self):
        segs = [self.seg("chair", 0.9, (i * 10, i * 10 + 8, 0, 20)) for i in range(9)]
        self.assertEqual(len(room_objects.choose_objects(segs, max_count=4)), 4)


if __name__ == "__main__":
    unittest.main()
