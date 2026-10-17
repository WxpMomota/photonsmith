"""Tests for room_textures.py: a room whose every surface is painted with "where on the surface am I" is photographed, and the textures made from the photo must show that pattern
where the picture says it should be, laid out the way the Scene Builder's Box puts a picture on the face we see.

Run with any Python that has numpy:

    python -m unittest tools/photo_to_room/test_room_textures.py
"""
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
    import room_textures
    from test_room_fit import photograph, ROOM_MIN, ROOM_MAX


def fit(**kw):
    depth, rgb = photograph(window=False, pattern=True, **kw)
    return room_fit.fit_room(depth, rgb, 70.0, return_geometry=True)


def surface(result, name):
    return next(s for s in result["surfaces"] if s["name"] == name)


def expected_pattern(face_name, world_photo):
    """The photo's colour at a world point (photo frame, x to the right) of a surface: what test_room_fit.photograph paints."""
    span = ROOM_MAX - ROOM_MIN
    f = (world_photo - ROOM_MIN) / span
    table = {"Floor": (0, 0.5, 2), "Ceiling": (0, 0.9, 2), "Left wall": (0.3, 1, 2), "Right wall": (0.7, 1, 2), "Back wall": (0, 1, 0.3), "Front wall": (0, 1, 0.7)}
    out = []
    for c in table[face_name]:
        out.append(f[..., c] if isinstance(c, int) else np.full_like(f[..., 0], c))
    return np.stack(out, axis=-1)


@unittest.skipUnless(HAVE_NUMPY, "numpy is not installed")
class RoomTexturesTests(unittest.TestCase):
    def check(self, yaw, pitch, names, tol=0.07, texels=120):
        result, geometry = fit(yaw=yaw, pitch=pitch)
        for name in names:
            s = surface(result, name)
            image, seen = room_textures.surface_texture(geometry["textures"], s["min"], s["max"], room_textures.FACES[name], texels_per_metre=texels)
            self.assertIsNotNone(image, "%s: the photo shows it (seen %.2f)" % (name, seen))
            spec = room_textures.FACES[name]
            h, w = image.shape[:2]
            pts = room_textures.face_points(s["min"], s["max"], spec, w, h)
            pts[..., 0] = -pts[..., 0]   # the Scene Builder's frame -> the photo's
            want = expected_pattern(name, pts)
            # compare where the photo really shows it (inside the room: the slab's rim beyond the walls is not in the picture)
            inside = np.all((pts >= ROOM_MIN - 0.02) & (pts <= ROOM_MAX + 0.02), axis=-1)
            err = np.abs(image - want)[inside]
            self.assertLess(float(np.median(err)), tol, "%s: the picture is where the surface is (median error %.3f)" % (name, float(np.median(err))))

    def test_a_level_photo_gives_the_floor_and_back_wall_as_they_are(self):
        self.check(0.0, 0.0, ("Floor", "Back wall"))

    def test_a_turned_and_tilted_photo_does_too(self):
        self.check(15.0, 8.0, ("Floor", "Back wall", "Right wall"), tol=0.09)

    def test_the_picture_is_upright_and_the_right_way_round(self):
        result, geometry = fit(yaw=0.0, pitch=0.0)
        s = surface(result, "Back wall")
        image, _ = room_textures.surface_texture(geometry["textures"], s["min"], s["max"], room_textures.FACES["Back wall"], texels_per_metre=100)
        h, w = image.shape[:2]
        # the back wall's pattern: red runs with the photo's x (to the right), green with the height. The Builder's -Z face has u along -x_builder = +x_photo and v up the picture: the
        # picture is the wall as seen from inside the room, so red rises from its left to its right (not mirrored) and green is high at the top
        centre = image[h // 2 - 5:h // 2 + 5]
        self.assertGreater(float(centre[:, w - 15:w - 5, 0].mean()), float(centre[:, 5:15, 0].mean()) + 0.3, "red rises from the picture's left to its right")
        self.assertGreater(float(image[5:15, w // 2, 1].mean()), float(image[h - 15:h - 5, w // 2, 1].mean()) + 0.3, "green is high at the top of the picture")

    def test_what_stands_in_front_is_not_painted_on_the_surface_behind(self):
        # a cabinet in front of the back wall: its brown must not appear on the wall's picture where it stood
        obstacle = ((-0.5, 0.0, 3.0), (0.4, 1.2, 3.5))
        depth, rgb = photograph(window=False, pattern=True, yaw=0.0, pitch=0.0, obstacle=obstacle)
        result, geometry = room_fit.fit_room(depth, rgb, 70.0, return_geometry=True)
        s = surface(result, "Back wall")
        spec = room_textures.FACES["Back wall"]
        image, seen = room_textures.surface_texture(geometry["textures"], s["min"], s["max"], spec, texels_per_metre=100)
        self.assertIsNotNone(image)
        h, w = image.shape[:2]
        pts = room_textures.face_points(s["min"], s["max"], spec, w, h)
        pts[..., 0] = -pts[..., 0]
        behind = (np.abs(pts[..., 0] - (-0.05)) < 0.3) & (pts[..., 1] < 1.0)   # the wall right behind the cabinet
        brown = np.array([150, 130, 100]) / 255.0
        close_to_brown = np.all(np.abs(image - brown) < 0.05, axis=-1)
        self.assertLess(float(close_to_brown[behind].mean()), 0.05, "the cabinet is not on the wall")

    def test_an_object_flush_against_the_wall_is_left_out_when_its_mask_is_given(self):
        # a thin object on the back wall (2 cm in front: the depth test cannot tell it from the wall) is painted on the wall's picture unless its mask says what it is
        obstacle = ((-0.5, 0.5, 4.48), (0.4, 1.4, 4.5))
        depth, rgb, mask = photograph(window=False, pattern=True, yaw=0.0, pitch=0.0, obstacle=obstacle, want_mask=True)
        result, geometry = room_fit.fit_room(depth, rgb, 70.0, return_geometry=True)
        s = surface(result, "Back wall")
        spec = room_textures.FACES["Back wall"]
        brown = np.array([150, 130, 100]) / 255.0

        def brown_share(exclude):
            geometry["textures"]["exclude"] = exclude
            image, _ = room_textures.surface_texture(geometry["textures"], s["min"], s["max"], spec, texels_per_metre=100)
            return float(np.all(np.abs(image - brown) < 0.05, axis=-1).mean())

        self.assertGreater(brown_share(None), 0.02, "without the mask the object is on the wall's picture")
        import room_objects
        self.assertLess(brown_share(room_objects.dilate(mask, 2)), 0.003, "with it, it is not")

    def test_a_surface_the_photo_does_not_show_gets_no_picture(self):
        result, geometry = fit(yaw=0.0, pitch=0.0)
        s = surface(result, "Front wall")   # behind the camera
        image, seen = room_textures.surface_texture(geometry["textures"], s["min"], s["max"], room_textures.FACES["Front wall"])
        self.assertIsNone(image)
        self.assertLess(seen, 0.15)

    def test_holes_are_filled_smoothly_from_what_is_seen(self):
        img = np.zeros((16, 16, 3))
        img[:, :8] = (1.0, 0.0, 0.0)
        valid = np.zeros((16, 16), bool)
        valid[:, :8] = True
        filled = room_textures.push_pull_fill(img, valid)
        self.assertTrue(np.allclose(filled[:, :8], img[:, :8]))
        self.assertGreater(float(filled[:, 8:, 0].min()), 0.9, "the unseen half takes the seen colour")
        self.assertLess(float(filled[:, 8:, 1].max()), 0.1)

    def test_a_windows_picture_is_what_the_photo_shows_there_the_right_way_round(self):
        depth, rgb = photograph(window=True, window_view=True, yaw=0.0, pitch=0.0)
        result, geometry = room_fit.fit_room(depth, rgb, 70.0, return_geometry=True)
        light = next(l for l in result["lights"] if l.get("surface") == "back")
        image, seen = room_textures.surface_texture(geometry["textures"], light["min"], light["max"], room_textures.FACES["Back wall"], texels_per_metre=240, check_depth=False, min_seen=0.5)
        self.assertIsNotNone(image, "the window is in the picture (seen %.2f)" % seen)
        # the view's red runs from 0.94 at the window's left (as seen from inside the room) to 1.0 at its right; green and blue are clipped white
        self.assertGreater(float(image[:, -6:, 0].mean()), float(image[:, :6, 0].mean()) + 0.03)
        self.assertGreater(float(image[..., 1].mean()), 0.93)

    def test_the_helper_writes_a_picture_for_every_surface_and_window(self):
        import tempfile
        import photo_to_room
        from PIL import Image
        depth, rgb = photograph(window=True, window_view=True, yaw=0.0, pitch=0.0)
        result, geometry = room_fit.fit_room(depth, rgb, 70.0, return_geometry=True)
        with tempfile.TemporaryDirectory() as folder:
            photo_to_room.add_textures(result, geometry, folder)
            made = {s["name"]: s.get("texture") for s in result["surfaces"]}
            self.assertEqual(made["Floor"], "textures/floor.png")
            self.assertEqual(made["Back wall"], "textures/back_wall.png")
            self.assertIsNone(made["Front wall"], "behind the camera: no picture")
            for rel in (made["Floor"], made["Back wall"]):
                with Image.open(os.path.join(folder, rel)) as picture:
                    self.assertEqual(picture.mode, "RGB")
            windows = [l for l in result["lights"] if l.get("picture")]
            self.assertEqual(len(windows), 1)
            self.assertTrue(os.path.isfile(os.path.join(folder, windows[0]["picture"])))
            self.assertTrue(any("window" in n for n in result["notes"]))

    def test_every_face_names_a_world_axis_for_u_and_v(self):
        for name, spec in room_textures.FACES.items():
            self.assertEqual(len({spec["face"][0], spec["u"][0], spec["v"][0]}), 3, name)


if __name__ == "__main__":
    unittest.main()
