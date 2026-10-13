#!/usr/bin/env python3
"""Renders every shape-by-material scene the Scene Builder's tests write (SceneCombosTest) on the CPU and both OptiX backends and reports what fails to render or looks different.

    RT_COMBO_SCENES_DIR=<dir> ray_tracer_tests --gtest_filter=SceneCombosTest.*       # writes <shape>__<treatment>.pbrt (run from the repository root)
    python scripts/combo_sweep.py --dir <dir> [--exe RayTracer_Package/ray_tracer.exe] [--only cone,mix] [--jobs 3] [--skip-wavefront]   (--skip-wavefront on a Mac)

For each scene it prints one line when a backend fails to render it (an OptiX trap, say) or when a GPU picture is further from the CPU's than the noise allows: the mean brightness
more than 6% off, or the 99th percentile of the 4x4-block differences above 0.08 (0.15 for glass, subsurface, transmission and emitters, which are noisy at this sample count), or three or more blocks off by more than 0.1.
A scene that fails on every backend is a bad scene, not a renderer bug, and is reported apart. Needs Pillow and numpy.
"""
import argparse
import concurrent.futures
import os
import subprocess
import sys
import time

import numpy as np
from PIL import Image

NOISY = ("glass", "subsurface", "transmission", "emitter")
# docs/SCENE_BUILDER.md, "Bump or normal map": OptiX applies a height map to triangles only. These differ from the CPU on purpose, and the Scene Builder says so in its properties.
KNOWN_NO_HEIGHT_MAP = ("sphere", "disk", "cylinder")
# Differences found by this sweep that are understood and written down (docs/SCENE_BUILDER.md, "What it does not do (yet)"): glass whose flat face lies exactly on the floor (the CPU's 0.001
# minimum hit distance lets light slip under the floor; lift the shape by 0.01 and all three agree), and subsurface on one open sheet.
KNOWN_OTHER = {"pyramid__glass": "glass resting exactly on the floor: the CPU skips a surface within 0.001, OptiX meets it",
               "quad__subsurface": "subsurface on a single open sheet: it needs a closed shape"}


def render(exe, scene, out_png_base, backend, wavefront):
    env = dict(os.environ)
    env["RAY_TRACER_WAVEFRONT"] = "1" if wavefront else "0"
    out = out_png_base + ".ppm"
    for f in (out, out_png_base + ".png"):
        if os.path.exists(f):
            os.remove(f)
    r = subprocess.run([exe, "--" + backend, "--seed", "2", "--output", out, "64", "48", "8", scene], capture_output=True, text=True, env=env, timeout=600)
    png = out_png_base + ".png"
    if r.returncode != 0 or not os.path.exists(png):
        tail = [l for l in (r.stdout + r.stderr).splitlines() if l.strip()][-3:]
        return None, " | ".join(tail)[-220:]
    img = np.asarray(Image.open(png).convert("RGB")).astype(float) / 255.0
    return img, ""


def blocks(img):
    h, w, _ = img.shape
    return img.reshape(h // 4, 4, w // 4, 4, 3).mean(axis=(1, 3))


def block_stats(cpu, other):
    """Differences of 4x4 block means: the 99th percentile (the object is a small part of the frame, so a mean over the frame would hide it) and the count of blocks off by more than 0.1."""
    d = np.abs(blocks(cpu) - blocks(other)).mean(axis=2)
    return float(np.percentile(d, 99)), int((d > 0.10).sum())


def run_scene(args, name):
    scene = os.path.join(args.dir, name + ".pbrt")
    work = os.path.join(args.dir, "out")
    os.makedirs(work, exist_ok=True)
    results = {}
    renders = (("cpu", "cpu", False), ("gpu", "gpu", False)) + (() if args.skip_wavefront else (("wf", "gpu", True),))
    for tag, backend, wf in renders:
        img, err = render(args.exe, scene, os.path.join(work, name + "." + tag), backend, wf)
        results[tag] = (img, err)
    problems = []
    cpu = results["cpu"][0]
    failed = [t for t in results if results[t][0] is None]
    if len(failed) == len(renders):
        return name, ["BAD SCENE: no backend rendered it: " + results["cpu"][1]]
    for t in failed:
        problems.append("%s FAILED TO RENDER: %s" % (t, results[t][1]))
    if cpu is None:
        return name, problems
    noisy = any(k in name for k in NOISY)
    tol = 0.15 if noisy else 0.08
    for t in [r[0] for r in renders][1:]:
        img = results[t][0]
        if img is None:
            continue
        a, b = cpu.mean(), img.mean()
        rel = abs(b - a) / max(a, 1e-6)
        p99, many = block_stats(cpu, img)
        if args.stats:
            problems.append("%s: mean %.3f vs %.3f, block p99 %.3f, blocks > 0.1: %d" % (t, a, b, p99, many))
        elif rel > 0.06 or p99 > tol or many >= 3:
            known = (name.split("__")[0] in KNOWN_NO_HEIGHT_MAP and name.endswith("__heightmap")) or name in KNOWN_OTHER
            why = "OptiX draws no height map on a sphere, disk or cylinder; Metal does" if name.endswith("__heightmap") else KNOWN_OTHER.get(name, "")
            problems.append(("KNOWN (%s): " % why if known else "") + "%s differs from cpu: mean %.3f vs %.3f (%.1f%% off), 99th percentile of 4x4 block differences %.3f, %d blocks off by > 0.1" % (t, a, b, rel * 100, p99, many))
    return name, problems


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", required=True)
    ap.add_argument("--exe", default=os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "RayTracer_Package", "ray_tracer.exe"))
    ap.add_argument("--only", default="", help="comma separated words: only scenes whose name contains one of them")
    ap.add_argument("--jobs", type=int, default=3)
    ap.add_argument("--skip-wavefront", action="store_true", help="render the CPU and the GPU only (on a Mac the GPU is Metal and there is no wavefront backend to compare)")
    ap.add_argument("--stats", action="store_true", help="print the numbers for every scene instead of only the ones over the limits")
    args = ap.parse_args()
    args.exe = os.path.abspath(args.exe)
    names = sorted(f[:-5] for f in os.listdir(args.dir) if f.endswith(".pbrt"))
    if args.only:
        words = args.only.split(",")
        names = [n for n in names if any(w in n for w in words)]
    t0 = time.time()
    bad = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for name, problems in pool.map(lambda n: run_scene(args, n), names):
            for p in problems:
                print("%-34s %s" % (name, p), flush=True)
            bad += 1 if problems and not all(p.startswith("KNOWN") for p in problems) else 0
    print("\n%d scenes, %d with problems (%.0f s)" % (len(names), bad, time.time() - t0))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
