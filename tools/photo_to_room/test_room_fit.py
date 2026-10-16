"""Tests for room_fit.py: a room of known size is "photographed" (its depth map and picture computed exactly, no model), and the fit must find the room again.

Run with any Python that has numpy:

    python -m unittest tools/photo_to_room/test_room_fit.py
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


def srgb(c):
    return np.array(c, dtype=np.float64) / 255.0


# The room the tests photograph: x in [-2.2, 1.8], z in [-0.5, 4.5] (the camera is at the origin), 2.7 m high; the camera is 1.4 m up.
ROOM_MIN = np.array([-2.2, 0.0, -0.5])
ROOM_MAX = np.array([1.8, 2.7, 4.5])
COLORS = {
    "floor": srgb((150, 110, 70)), "ceiling": srgb((235, 235, 235)), "left": srgb((200, 60, 60)),
    "right": srgb((60, 160, 60)), "back": srgb((220, 215, 200)), "front": srgb((200, 200, 215)),
}
# a window on the back wall, right of centre as photographed: x in [0.2, 1.4], y in [0.9, 2.2]
WINDOW = (0.2, 1.4, 0.9, 2.2)


def photograph(width=240, height=180, hfov=70.0, yaw=20.0, pitch=10.0, roll=0.0, cam_height=1.4, noise=0.0, window=True, seed=1, obstacle=None, want_mask=False, pattern=False):
    """(depth, rgb uint8) of the room from a camera at (0, cam_height, 0): yawed about the vertical (positive turns towards +x), pitched down by `pitch`, rolled by `roll`.
    `obstacle` is a box ((x0, y0, z0), (x1, y1, z1)) of furniture standing in the room; with `want_mask` the answer also holds the (height, width) mask of the pixels that see it."""
    f = (width / 2.0) / math.tan(math.radians(hfov) / 2.0)
    u = np.arange(width) + 0.5
    v = np.arange(height) + 0.5
    uu, vv = np.meshgrid(u, v)
    cam = np.stack([(uu - width / 2.0) / f, -(vv - height / 2.0) / f, np.ones_like(uu)], axis=-1)  # z = 1: t is the depth
    cr, sr = math.cos(math.radians(roll)), math.sin(math.radians(roll))
    Rroll = np.array([[cr, -sr, 0], [sr, cr, 0], [0, 0, 1]])
    cp, sp = math.cos(math.radians(pitch)), math.sin(math.radians(pitch))
    Rpitch = np.array([[1, 0, 0], [0, cp, -sp], [0, sp, cp]])  # looking down: the forward axis (0, 0, 1) becomes (0, -sin, cos)
    cy, sy = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
    Ryaw = np.array([[cy, 0, sy], [0, 1, 0], [-sy, 0, cy]])
    d = cam @ (Ryaw @ Rpitch @ Rroll).T
    origin = np.array([0.0, cam_height, 0.0])
    with np.errstate(divide="ignore", invalid="ignore"):
        tx = np.where(d[..., 0] > 0, (ROOM_MAX[0] - origin[0]) / d[..., 0], (ROOM_MIN[0] - origin[0]) / d[..., 0])
        ty = np.where(d[..., 1] > 0, (ROOM_MAX[1] - origin[1]) / d[..., 1], (ROOM_MIN[1] - origin[1]) / d[..., 1])
        tz = np.where(d[..., 2] > 0, (ROOM_MAX[2] - origin[2]) / d[..., 2], (ROOM_MIN[2] - origin[2]) / d[..., 2])
    t = np.minimum(np.minimum(tx, ty), tz)
    # t is along the ray with camera-z = 1, but d was rotated: the depth along the viewing axis is t itself (the camera z of the ray is 1 by construction)
    which = np.argmin(np.stack([tx, ty, tz], axis=-1), axis=-1)
    hit = origin + d * t[..., None]
    rgb = np.zeros((height, width, 3))
    name = np.empty((height, width), dtype=object)
    for axis, pos_name, neg_name in ((0, "right", "left"), (1, "ceiling", "floor"), (2, "back", "front")):
        pos = (which == axis) & (d[..., axis] > 0)
        neg = (which == axis) & (d[..., axis] <= 0)
        name[pos] = pos_name
        name[neg] = neg_name
    for key, c in COLORS.items():
        rgb[name == key] = c
    if pattern:
        # every surface shows where on it each pixel is: two colour channels run with the two coordinates along the surface (0..1 across the room), the third is fixed
        span = ROOM_MAX - ROOM_MIN
        f = (hit - ROOM_MIN) / span
        for key, (r, g, b) in {"floor": (0, 0.5, 2), "ceiling": (0, 0.9, 2), "left": (0.3, 1, 2), "right": (0.7, 1, 2), "back": (0, 1, 0.3), "front": (0, 1, 0.7)}.items():
            sel = name == key
            rgb[sel] = np.stack([f[..., r] if isinstance(r, int) else np.full_like(f[..., 0], r), f[..., g] if isinstance(g, int) else np.full_like(f[..., 0], g),
                                 f[..., b] if isinstance(b, int) else np.full_like(f[..., 0], b)], axis=-1)[sel]
    if window:
        x0, x1, y0, y1 = WINDOW
        win = (name == "back") & (hit[..., 0] > x0) & (hit[..., 0] < x1) & (hit[..., 1] > y0) & (hit[..., 1] < y1)
        rgb[win] = 1.0
    mask = np.zeros((height, width), dtype=bool)
    if obstacle is not None:
        boxes = obstacle if isinstance(obstacle[0][0], (tuple, list)) else [obstacle]   # one box, or several (a table: a top and its legs)
        for box_lo, box_hi in boxes:
            lo, hi = np.array(box_lo, float), np.array(box_hi, float)
            with np.errstate(divide="ignore", invalid="ignore"):
                t0 = (lo - origin) / d
                t1 = (hi - origin) / d
            near = np.minimum(t0, t1).max(axis=-1)
            far = np.maximum(t0, t1).min(axis=-1)
            hit_box = (far >= np.maximum(near, 0.0)) & (near > 0) & (near < t)
            t = np.where(hit_box, near, t)
            rgb[hit_box] = srgb((150, 130, 100))
            mask = mask | hit_box
    depth = t.copy()
    if noise:
        depth = depth * (1.0 + noise * np.random.default_rng(seed).standard_normal(depth.shape))
    picture = (np.clip(rgb, 0, 1) * 255 + 0.5).astype(np.uint8)
    return (depth, picture, mask) if want_mask else (depth, picture)


@unittest.skipUnless(HAVE_NUMPY, "numpy is not installed")
class RoomFitTests(unittest.TestCase):
    def check_room(self, result, tol=0.2, unseen=()):
        """The walls the photo shows are where the room has them; `unseen` names walls outside the photo, which are only assumed (and must be said so in the notes).
        The result is in the Scene Builder's right-handed frame, the mirror of the photographed world in x: the photo's left wall is at +x."""
        lo, hi = result["room"]["min"], result["room"]["max"]
        for got, want, what in ((hi[0], -ROOM_MIN[0], "left"), (lo[0], -ROOM_MAX[0], "right"), (hi[1], ROOM_MAX[1], "ceiling"), (hi[2], ROOM_MAX[2], "back")):
            if what in unseen:
                self.assertTrue(any(what in n for n in result["notes"]), "an unseen %s wall should be mentioned in the notes" % what)
                continue
            self.assertAlmostEqual(got, want, delta=tol, msg="%s at %.3f, the room has it at %.3f" % (what, got, want))

    def test_a_level_photo_finds_the_room(self):
        depth, rgb = photograph(yaw=0.0, pitch=0.0)
        r = room_fit.fit_room(depth, rgb, 70.0)
        self.check_room(r)
        self.assertAlmostEqual(r["info"]["camera_height"], 1.4, delta=0.1)
        self.assertAlmostEqual(r["camera"]["position"][1], 1.4, delta=0.1)
        self.assertAlmostEqual(r["info"]["turn_degrees"], 0.0, delta=2.0)

    def test_a_turned_and_tilted_camera_is_levelled_and_the_room_squared_up(self):
        depth, rgb = photograph(yaw=20.0, pitch=10.0)   # looking 20 degrees to the right: the left wall is outside the picture
        r = room_fit.fit_room(depth, rgb, 70.0)
        self.check_room(r, unseen=("left",))
        self.assertAlmostEqual(r["info"]["camera_height"], 1.4, delta=0.12)
        # the camera looks 20 degrees to the right of the room's +z, and 10 degrees down
        f = np.array(r["camera"]["target"]) - np.array(r["camera"]["position"])
        yaw = math.degrees(math.atan2(f[0], f[2]))
        pitch = math.degrees(math.asin(-f[1] / np.linalg.norm(f)))
        self.assertAlmostEqual(yaw, -20.0, delta=3.0)   # (the mirror: looking to the right of the photo's frame is towards -x in the Builder)
        self.assertAlmostEqual(pitch, 10.0, delta=3.0)

    def test_a_rolled_camera_is_levelled(self):
        depth, rgb = photograph(yaw=0.0, pitch=5.0, roll=8.0)
        r = room_fit.fit_room(depth, rgb, 70.0)
        self.check_room(r, tol=0.25)
        up = np.array(r["camera"]["up"])
        roll = math.degrees(math.atan2(up[0], up[1]))
        self.assertAlmostEqual(abs(roll), 8.0, delta=3.0)

    def test_noisy_depth_still_finds_the_room(self):
        depth, rgb = photograph(yaw=15.0, pitch=8.0, noise=0.01)
        r = room_fit.fit_room(depth, rgb, 70.0)
        self.check_room(r, tol=0.3)

    def test_surface_colours_follow_the_photo(self):
        depth, rgb = photograph(yaw=0.0, pitch=0.0)
        r = room_fit.fit_room(depth, rgb, 70.0)
        by = {s["name"]: s["color"] for s in r["surfaces"]}
        self.assertGreater(by["Left wall"][0], 2.0 * by["Left wall"][1])      # red wall
        self.assertGreater(by["Right wall"][1], 1.5 * by["Right wall"][0])    # green wall
        self.assertGreater(by["Floor"][0], by["Floor"][2])                    # warm floor
        for c in by.values():
            self.assertTrue(all(0.03 <= v <= 0.9 for v in c))

    def test_a_dark_photo_keeps_its_hue_and_gets_weaker_lights(self):
        depth, rgb = photograph(yaw=0.0, pitch=0.0)
        bright = room_fit.fit_room(depth, rgb, 70.0)
        dim = room_fit.fit_room(depth, (rgb * 0.35).astype(np.uint8), 70.0)
        floor = {s["name"]: s["color"] for s in dim["surfaces"]}["Floor"]
        # the floor is (150, 110, 70) in the photo: red : blue about 5 : 1 in linear light, whatever the exposure; a per-channel clip would flatten it
        self.assertGreater(floor[0] / floor[2], 3.0)   # (a little below 5: the dimmed 8-bit picture has only about 25 levels in its blue)
        self.assertLess(floor[0] / floor[2], 6.5)
        self.assertLess(dim["lights"][0]["strength"], bright["lights"][0]["strength"])

    def test_the_light_level_does_not_depend_on_the_scale_of_the_room(self):
        # the same picture of a room twice the size has a window four times the area in a room four times the surface: the same radiance lights it
        depth, rgb = photograph(yaw=0.0, pitch=0.0)
        small = room_fit.fit_room(depth, rgb, 70.0)
        big = room_fit.fit_room(depth * 2.0, rgb, 70.0)
        self.assertAlmostEqual(big["lights"][0]["strength"] / small["lights"][0]["strength"], 1.0, delta=0.15)
        self.assertAlmostEqual(big["room"]["max"][2], 2.0 * small["room"]["max"][2], delta=0.5)

    def test_a_smaller_window_in_the_same_room_needs_a_brighter_light(self):
        depth, rgb = photograph(yaw=0.0, pitch=0.0)
        r = room_fit.fit_room(depth, rgb, 70.0)
        small = rgb.copy()
        # paint most of the window back to wall colour: a quarter of its width stays white
        wall = np.array([220, 215, 200], np.uint8)
        white = np.all(small == 255, axis=-1)
        cols = np.nonzero(white.any(axis=0))[0]
        small[:, cols[len(cols) // 4:]] = np.where(white[:, cols[len(cols) // 4:]][..., None], wall, small[:, cols[len(cols) // 4:]])
        r2 = room_fit.fit_room(depth, small, 70.0)
        self.assertGreater(r2["lights"][0]["strength"], r["lights"][0]["strength"])

    def test_a_window_becomes_a_light_on_the_wall_it_is_on(self):
        depth, rgb = photograph(yaw=0.0, pitch=0.0)
        r = room_fit.fit_room(depth, rgb, 70.0)
        windows = [l for l in r["lights"] if l["name"].startswith("Window")]
        self.assertEqual(len(windows), 1)
        w = windows[0]
        self.assertEqual(w["surface"], "back")
        self.assertAlmostEqual((w["min"][0] + w["max"][0]) / 2, -(WINDOW[0] + WINDOW[1]) / 2, delta=0.25)
        self.assertAlmostEqual((w["min"][1] + w["max"][1]) / 2, (WINDOW[2] + WINDOW[3]) / 2, delta=0.25)
        self.assertAlmostEqual(w["max"][0] - w["min"][0], WINDOW[1] - WINDOW[0], delta=0.35)

    def test_a_photo_without_a_bright_patch_gets_one_ceiling_light(self):
        depth, rgb = photograph(yaw=0.0, pitch=0.0, window=False)
        r = room_fit.fit_room(depth, rgb, 70.0)
        self.assertEqual(len(r["lights"]), 1)
        self.assertEqual(r["lights"][0]["surface"], "ceiling")
        self.assertTrue(any("ceiling" in n for n in r["notes"]))

    def test_a_given_camera_height_scales_the_room(self):
        depth, rgb = photograph(yaw=0.0, pitch=0.0)
        r = room_fit.fit_room(depth, rgb, 70.0, camera_height=1.0)
        self.assertAlmostEqual(r["info"]["camera_height"], 1.0, delta=0.001)
        self.assertAlmostEqual(r["room"]["max"][1], 2.7 / 1.4, delta=0.2)

    def test_walls_the_photo_does_not_show_are_assumed_and_said_so(self):
        # a narrow field of view straight at the back wall shows neither side wall
        depth, rgb = photograph(yaw=0.0, pitch=0.0, hfov=30.0)
        r = room_fit.fit_room(depth, rgb, 30.0)
        self.assertTrue(any("left wall" in n for n in r["notes"]))
        self.assertTrue(any("right wall" in n for n in r["notes"]))
        self.assertAlmostEqual(r["room"]["max"][2], ROOM_MAX[2], delta=0.2)

    def test_the_surfaces_close_the_room(self):
        depth, rgb = photograph(yaw=0.0, pitch=0.0)
        r = room_fit.fit_room(depth, rgb, 70.0)
        names = [s["name"] for s in r["surfaces"]]
        self.assertEqual(names, ["Floor", "Ceiling", "Left wall", "Right wall", "Back wall", "Front wall"])
        for s in r["surfaces"]:
            self.assertTrue(all(a < b for a, b in zip(s["min"], s["max"])), s["name"])

    def test_a_flat_useless_depth_map_is_refused(self):
        with self.assertRaises(ValueError):
            room_fit.fit_room(np.zeros((60, 80)), np.zeros((60, 80, 3), np.uint8), 70.0)


if __name__ == "__main__":
    unittest.main()
