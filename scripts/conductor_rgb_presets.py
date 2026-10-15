#!/usr/bin/env python3
"""Prints the RGB conductor presets (src/shared/conductor_data.h) from pbrt-v4's metal spectra.

pbrt's conductor takes eta(lambda) and k(lambda) as spectra and the film turns the reflected spectrum into RGB. This renderer shades with one complex index per colour channel, and
the presets were those spectra read at three wavelengths (630, 532, 467 nm), which gives copper a normal-incidence reflectance of (0.924, 0.610, 0.569) where the film sees
(0.951, 0.638, 0.540) (pbrt_scenes/rough-metal-lamp.pbrt read red 3% and green 4% low, blue 6% high on every backend), and gold red 8% low and blue 12% high.

So: eta stays what it was (the angular behaviour), and k is solved per channel so that the Fresnel reflectance at normal incidence R0 = ((n-1)^2 + k^2) / ((n+1)^2 + k^2) equals the
RGB reflectance the film sees: R0(lambda) of the spectra projected with the CIE quadrature of src/shared/measured_rgb_quadrature.h (scripts/measured_rgb_weights.py), capped at 0.995
(a saturated red of gold projects above 1 through the sRGB matrix).

    python scripts/conductor_rgb_presets.py --pbrt-src C:/Users/xinpe/source/repos/pbrt-v4
"""
import argparse
import math
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pbrt-src", required=True)
    args = ap.parse_args()
    src = open(os.path.join(args.pbrt_src, "src", "pbrt", "util", "spectrum.cpp"), encoding="utf-8", errors="replace").read()

    def table(name):
        m = re.search(r"const Float %s\[\]\s*=\s*\{(.*?)\};" % name, src, re.S)
        v = [float(x) for x in re.findall(r"[-+]?\d+\.?\d*(?:[eE][-+]?\d+)?", m.group(1))]
        return v[0::2], v[1::2]

    hdr = open(os.path.join(ROOT, "src", "shared", "measured_rgb_quadrature.h")).read()

    def macro(n):
        return [float(x.strip().rstrip("f")) for x in re.search(r"#define %s (.*)" % n, hdr).group(1).split(",")]

    lam = macro("RT_MEASURED_LAMBDAS")
    weights = [macro("RT_MEASURED_WR"), macro("RT_MEASURED_WG"), macro("RT_MEASURED_WB")]

    def at(xs, ys, l):
        if l <= xs[0]:
            return ys[0]
        if l >= xs[-1]:
            return ys[-1]
        for i in range(len(xs) - 1):
            if xs[i] <= l <= xs[i + 1]:
                t = (l - xs[i]) / (xs[i + 1] - xs[i])
                return ys[i] * (1 - t) + ys[i + 1] * t

    for metal in ("Au", "Ag", "Al", "Cu"):
        ex, ey = table(metal + "_eta")
        kx, ky = table(metal + "_k")

        def r0(l):
            n, k = at(ex, ey, l), at(kx, ky, l)
            return ((n - 1) ** 2 + k * k) / ((n + 1) ** 2 + k * k)

        target = [min(0.995, max(0.0, sum(weights[c][i] * r0(lam[i]) for i in range(len(lam))))) for c in range(3)]
        eta = [at(ex, ey, l) for l in (630.0, 532.0, 467.0)]
        k = []
        for c in range(3):
            n = eta[c]
            k2 = (target[c] * (n + 1) ** 2 - (n - 1) ** 2) / (1 - target[c])
            k.append(math.sqrt(max(k2, 0.0)))
        print("%s  eta %.3f %.3f %.3f   k %.3f %.3f %.3f   (R0 %.3f %.3f %.3f)" % ((metal,) + tuple(eta) + tuple(k) + tuple(target)))


main()
