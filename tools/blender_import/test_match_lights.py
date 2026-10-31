#!/usr/bin/env python3
"""Tests of match_lights.py's parts that need no renderer: python tools/blender_import/test_match_lights.py"""
import unittest
import numpy as np
import match_lights as ml


class Gains(unittest.TestCase):
    def test_it_finds_the_gains_that_made_the_picture(self):
        r = np.random.default_rng(1)
        basis = r.random((5000, 4)) ** 3
        rest = r.random(5000) * 0.02
        truth = np.array([0.55, 1.7, 0.0, 3.0])
        target = basis @ truth + rest
        got = ml.solve_gains(basis, rest, target, 1.0 / (0.03 + target))
        np.testing.assert_allclose(got, truth, atol=2e-3)

    def test_a_gain_is_never_negative(self):
        r = np.random.default_rng(2)
        basis = r.random((800, 2))
        got = ml.solve_gains(basis, np.zeros(800), basis[:, 0] - 0.5 * basis[:, 1], np.ones(800))
        self.assertTrue((got >= 0.0).all())


class Lights(unittest.TestCase):
    LINES = ['LightSource "infinite" "rgb L" [ 0.1 0.1 0.3 ]', '  LightSource "spot" "rgb I" [ 1 1 10 ]', '  LightSource "spot" "rgb I" [ 2 2 20 ]', '  LightSource "point" "rgb I" [ 9 3 1 ]',
             '  AreaLightSource "diffuse" "rgb L" [ 5 5 5 ] "bool camera" false', '  AreaLightSource "diffuse" "rgb L" [ 1 1 1 ] "bool sampled" false', 'Shape "sphere"']

    def test_groups_by_kind_and_colour_and_leaves_out_glows(self):
        g = ml.light_groups(self.LINES)
        self.assertEqual(g['sky'], [0])
        self.assertEqual(sorted(sum(g.values(), [])), [0, 1, 2, 3, 4], 'the glow-only mesh is left alone')
        self.assertTrue(any(v == [1, 2] for v in g.values()), 'the two blue spots are one group')

    def test_scaling_a_light(self):
        self.assertIn('"float scale" [ 2 ]', ml.scaled('LightSource "spot" "rgb I" [ 1 1 1 ]', 2.0))
        self.assertIn('"float scale" 7', ml.scaled('AreaLightSource "diffuse" "float scale" 3.5', 2.0))
        self.assertIn('"float scale" [ 3 ]', ml.scaled('X "float scale" [ 1.5 ] Y', 2.0))


class Curves(unittest.TestCase):
    def test_filmic_middle_grey(self):
        self.assertAlmostEqual(float(ml.filmic_to_linear(np.array([0.502]))[0]), 0.18, delta=0.012)
        self.assertAlmostEqual(float(ml.filmic_to_linear(np.array([0.7569]))[0]), 0.72, delta=0.05)


if __name__ == '__main__':
    unittest.main()
