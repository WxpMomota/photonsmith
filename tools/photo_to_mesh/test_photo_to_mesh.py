"""Tests for the parts of photo_to_mesh.py that do not need PyTorch or a model: how the mesh is oriented, how vertex colours are baked into a
texture, and what --check reports. Run with the helper's own environment (it has numpy and scipy), or any Python that has them:

    %LOCALAPPDATA%\\RayTracerPhoto\\venv\\Scripts\\python.exe -m unittest tools/photo_to_mesh/test_photo_to_mesh.py

The tests skip themselves when numpy or scipy is missing.
"""
import contextlib
import importlib.util
import io
import os
import unittest

try:
    import numpy as np
    import scipy  # noqa: F401  (the baker needs it)
    HAVE_DEPS = True
except ImportError:
    HAVE_DEPS = False

HERE = os.path.dirname(os.path.abspath(__file__))


def load_helper():
    spec = importlib.util.spec_from_file_location("photo_to_mesh", os.path.join(HERE, "photo_to_mesh.py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def box(sx, sy, sz):
    """A closed box centred on the origin with outward-facing triangles: (vertices, faces)."""
    x, y, z = sx / 2, sy / 2, sz / 2
    v = np.array([[-x, -y, -z], [x, -y, -z], [x, y, -z], [-x, y, -z], [-x, -y, z], [x, -y, z], [x, y, z], [-x, y, z]], dtype=float)
    f = np.array([[0, 2, 1], [0, 3, 2], [4, 5, 6], [4, 6, 7], [0, 1, 5], [0, 5, 4], [2, 3, 7], [2, 7, 6], [1, 2, 6], [1, 6, 5], [3, 0, 4], [3, 4, 7]])
    return v, f


def volume(v, f):
    return float(np.einsum("ij,ij->", v[f[:, 0]], np.cross(v[f[:, 1]], v[f[:, 2]])) / 6.0)


@unittest.skipUnless(HAVE_DEPS, "numpy and scipy are needed")
class OrientMeshTest(unittest.TestCase):
    def setUp(self):
        self.helper = load_helper()

    def test_stands_on_the_floor_two_tall_and_centred(self):
        # raw: depth (X, the front direction) 1, width (Y) 2, height (Z, up) 4, sitting anywhere
        v, f = box(1.0, 2.0, 4.0)
        v = v + [5.0, -3.0, 7.0]
        out, _ = self.helper.orient_mesh(np, v, f)
        lo, hi = out.min(axis=0), out.max(axis=0)
        self.assertAlmostEqual(lo[1], 0.0)          # on y = 0
        self.assertAlmostEqual(hi[1], 2.0)          # 2 tall
        self.assertAlmostEqual((lo[0] + hi[0]) / 2, 0.0)
        self.assertAlmostEqual((lo[2] + hi[2]) / 2, 0.0)
        # raw width (2) became x, raw depth (1) became z, both scaled by 2/4
        self.assertAlmostEqual(hi[0] - lo[0], 1.0)
        self.assertAlmostEqual(hi[2] - lo[2], 0.5)

    def test_front_faces_plus_z_and_is_not_mirrored(self):
        # a marker vertex on the raw +X face (the front) and one on the raw +Y face (the object's left, seen from the front)
        v = np.array([[1.0, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0], [-1.0, 0.0, 0.0], [0.0, -1.0, 0.0], [0.0, 0.0, -1.0]])
        f = np.array([[0, 1, 2], [0, 2, 4], [0, 4, 5], [0, 5, 1], [3, 2, 1], [3, 4, 2], [3, 5, 4], [3, 1, 5]])  # an octahedron
        out, _ = self.helper.orient_mesh(np, v, f)
        centre_x = (out[:, 0].min() + out[:, 0].max()) / 2
        self.assertGreater(out[0, 2], out[3, 2])    # raw +X (front) ends up towards +Z
        # a turn, not a mirror: a viewer at raw +X (the front) looking back with Z up has raw +Y on their right; a viewer at +Z with Y up has +X there
        self.assertGreater(out[1, 0], out[4, 0])
        self.assertAlmostEqual(centre_x, 0.0)

    def test_triangles_face_outward_whichever_way_they_came(self):
        v, f = box(1.0, 1.0, 1.0)
        self.assertGreater(volume(v, f), 0)
        out_v, out_f = self.helper.orient_mesh(np, v, f)
        self.assertGreater(volume(out_v, out_f), 0)
        out_v, out_f = self.helper.orient_mesh(np, v, f[:, ::-1])   # inside-out input
        self.assertGreater(volume(out_v, out_f), 0)


@unittest.skipUnless(HAVE_DEPS, "numpy and scipy are needed")
class LevelBaseTest(unittest.TestCase):
    def setUp(self):
        self.helper = load_helper()
        rng = np.random.default_rng(7)
        # a sofa-like block 2 wide, 2 tall, 1 deep, its six faces sampled (as a surface mesh's vertices would be), standing on y = 0
        lo, hi = np.array([-1.0, 0.0, -0.5]), np.array([1.0, 2.0, 0.5])
        faces = []
        for axis in range(3):
            for side in (lo, hi):
                p = rng.uniform(lo, hi, size=(1500, 3))
                p[:, axis] = side[axis]
                faces.append(p)
        self.block = np.vstack(faces)

    @staticmethod
    def roll(points, degrees):
        a = np.radians(degrees)
        r = np.array([[np.cos(a), -np.sin(a), 0.0], [np.sin(a), np.cos(a), 0.0], [0.0, 0.0, 1.0]])
        return points @ r.T

    def base_tilt(self, v):
        low = v[v[:, 1] < v[:, 1].min() + 0.12 * (v[:, 1].max() - v[:, 1].min())]
        _, _, vt = np.linalg.svd(low - low.mean(axis=0), full_matrices=False)
        n = vt[2] if vt[2][1] >= 0 else -vt[2]
        return float(np.degrees(np.arccos(min(1.0, n[1]))))

    def test_a_leaning_piece_is_stood_up_flat_on_the_floor(self):
        leaning = self.roll(self.block, 12.0)
        leaning = leaning - [0.0, leaning[:, 1].min(), 0.0]
        self.assertGreater(self.base_tilt(leaning), 5.0)
        out = self.helper.level_base(np, leaning)
        self.assertLess(self.base_tilt(out), 1.5)
        self.assertAlmostEqual(float(out[:, 1].min()), 0.0, places=6)
        self.assertAlmostEqual(float(out[:, 1].max()), 2.0, places=6)
        self.assertAlmostEqual(float(out[:, 0].min() + out[:, 0].max()), 0.0, places=6)

    def test_a_turn_keeps_the_triangles_outward(self):
        v, f = box(1.0, 2.0, 1.0)
        rng = np.random.default_rng(3)
        dense = np.vstack([v, rng.uniform(v.min(axis=0), v.max(axis=0), size=(3000, 3))])
        out = self.helper.level_base(np, self.roll(dense, 9.0))[: len(v)]
        self.assertGreater(volume(out, f), 0)

    def test_what_is_not_worth_it_or_not_believable_is_left_alone(self):
        slab = np.random.default_rng(9).uniform([-1.0, 0.0, -0.5], [1.0, 0.02, 0.5], size=(3000, 3))   # (a flat sheet: its plane is exactly what it was turned to)
        for what, points, degrees in (("a barely leaning piece", self.block, 0.4), ("a sheet on its side", slab, 40.0)):
            tilted = self.roll(points, degrees)
            tilted = tilted - [0.0, tilted[:, 1].min(), 0.0]
            self.assertTrue(np.array_equal(self.helper.level_base(np, tilted), tilted), "%s at %s degrees" % (what, degrees))
        rng = np.random.default_rng(5)
        pole = rng.uniform([-0.05, 0.0, -0.05], [0.05, 2.0, 0.05], size=(3000, 3))   # a stem: its base has no plane
        pole = self.roll(pole, 10.0)
        pole = pole - [0.0, pole[:, 1].min(), 0.0]
        self.assertTrue(np.array_equal(self.helper.level_base(np, pole), pole))
        few = self.block[:20]
        self.assertTrue(np.array_equal(self.helper.level_base(np, few), few))


@unittest.skipUnless(HAVE_DEPS, "numpy and scipy are needed")
class BakeTest(unittest.TestCase):
    def setUp(self):
        self.helper = load_helper()

    def test_a_painted_triangle_fills_with_its_colour_and_the_gaps_take_the_nearest(self):
        uvs = np.array([[0.1, 0.1], [0.4, 0.1], [0.1, 0.4]], dtype=np.float32)
        colours = np.array([[1, 0, 0], [1, 0, 0], [1, 0, 0]], dtype=np.float32)
        tex = self.helper.bake_vertex_colours(np, uvs, np.array([[0, 1, 2]]), colours, 32)
        self.assertEqual(tex.shape, (32, 32, 3))
        self.assertTrue(np.allclose(tex[6, 6], [1, 0, 0]))      # inside the triangle
        self.assertTrue(np.allclose(tex[30, 30], [1, 0, 0]))    # far outside: nearest painted texel, never black
        self.assertGreater(tex.min(axis=(0, 1))[0], 0.99)

    def test_two_far_triangles_keep_their_own_colours(self):
        uvs = np.array([[0.05, 0.05], [0.25, 0.05], [0.05, 0.25], [0.75, 0.75], [0.95, 0.75], [0.75, 0.95]], dtype=np.float32)
        colours = np.array([[1, 0, 0]] * 3 + [[0, 0, 1]] * 3, dtype=np.float32)
        tex = self.helper.bake_vertex_colours(np, uvs, np.array([[0, 1, 2], [3, 4, 5]]), colours, 64)
        self.assertTrue(np.allclose(tex[6, 6], [1, 0, 0]))
        self.assertTrue(np.allclose(tex[56, 56], [0, 0, 1]))

    def test_colours_blend_across_a_triangle(self):
        uvs = np.array([[0.0, 0.0], [1.0, 0.0], [0.0, 1.0]], dtype=np.float32)
        colours = np.array([[0, 0, 0], [1, 0, 0], [0, 0, 0]], dtype=np.float32)
        tex = self.helper.bake_vertex_colours(np, uvs, np.array([[0, 1, 2]]), colours, 64)
        self.assertLess(tex[2, 2, 0], 0.15)                     # near the black corner
        self.assertGreater(tex[2, 60, 0], 0.85)                 # near the red corner
        self.assertTrue(0.4 < tex[2, 31, 0] < 0.6)              # halfway


class CheckEnvironmentTest(unittest.TestCase):
    def test_report_is_one_key_value_fact_per_line_and_flags_a_missing_triposr(self):
        helper = load_helper()
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            helper.check_environment(os.path.join(HERE, "no_such_triposr_folder"))
        lines = [l for l in out.getvalue().splitlines() if l.strip()]
        self.assertTrue(lines)
        for line in lines:
            self.assertIn(": ", line, line)
        text = out.getvalue()
        for key in ("Python:", "PyTorch:", "Transformers:", "Graphics Card for PyTorch:", "TripoSR Code:", "TripoSR Weights:", "Background Remover Model"):
            self.assertIn(key, text)
        triposr = [l for l in lines if l.startswith("TripoSR Code:")][0]
        # (a TripoSR next to the environment may exist on a machine with the helper installed; only the explicit bad path must not count)
        self.assertTrue("missing" in triposr or "present" in triposr)


if __name__ == "__main__":
    unittest.main()
