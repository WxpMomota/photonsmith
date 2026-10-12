#!/usr/bin/env python3
"""Renders the pictures the Scene Builder's material preset list shows: qt_gui/material_thumbs/<preset id>.png, 64 x 64, a sphere of the preset in a small studio.

The scenes are written by the unit tests (RT_PRESET_SCENES_DIR), from the same document code the Scene Builder writes its own scenes with, so a preset's picture is made from exactly
what choosing it gives. Run it again after adding or changing a preset, then add the new file to qt_gui/resources.qrc (the script prints the lines to paste for any that are missing).

    python scripts/make_material_thumbnails.py [--exe x64/Release/ray_tracer.exe] [--tests bin/Release/ray_tracer_tests.exe] [--spp 256]

CPU render, so it needs no GPU; about 15 seconds a preset at 256 samples (the subsurface ones are slower).
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# Presets whose pictures are noisy at the ordinary sample count (light that goes through glass or under a surface is found by chance): they get four times as many.
NOISY = {"mix-dusty-glass", "skin", "milk", "marble-stone", "jade", "candle-wax", "glass", "frosted-glass", "water", "diamond"}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", default=os.path.join(REPO, "x64", "Release", "ray_tracer.exe"))
    ap.add_argument("--tests", default=os.path.join(REPO, "bin", "Release", "ray_tracer_tests.exe"))
    ap.add_argument("--spp", type=int, default=256)
    ap.add_argument("--only", default="", help="comma separated preset ids (default: all)")
    args = ap.parse_args()
    exe = os.path.abspath(args.exe)
    if not os.path.exists(exe):
        sys.exit("ray_tracer not found: " + exe)
    if not os.path.exists(args.tests):
        sys.exit("ray_tracer_tests not found: " + args.tests)
    scenes = tempfile.mkdtemp(prefix="presets.")
    out_dir = os.path.join(REPO, "qt_gui", "material_thumbs")
    os.makedirs(out_dir, exist_ok=True)
    try:
        env = dict(os.environ, RT_PRESET_SCENES_DIR=scenes)
        r = subprocess.run([args.tests, "--gtest_filter=SceneMaterialsMoreTest.WritesOneSceneForEveryPresetWhenAsked"], capture_output=True, text=True, env=env, cwd=REPO)
        if r.returncode != 0:
            sys.exit("the preset scenes were not written:\n" + r.stdout[-600:] + r.stderr[-600:])
        wanted = set(x for x in args.only.split(",") if x)
        ids = sorted(f[:-5] for f in os.listdir(scenes) if f.endswith(".pbrt"))
        for pid in ids:
            if wanted and pid not in wanted:
                continue
            ppm = os.path.join(scenes, pid + ".ppm")
            spp = args.spp * (4 if pid in NOISY else 1)
            r = subprocess.run([exe, "--cpu", "--seed", "2", "--output", ppm, "64", str(spp), "20", os.path.join(scenes, pid + ".pbrt")], capture_output=True, text=True, cwd=REPO)
            png = os.path.join(scenes, pid + ".png")
            if r.returncode != 0 or not os.path.exists(png):
                sys.exit("%s did not render: %s" % (pid, (r.stdout + r.stderr)[-400:]))
            shutil.copyfile(png, os.path.join(out_dir, pid + ".png"))
            print("made", pid)
        qrc = open(os.path.join(REPO, "qt_gui", "resources.qrc"), encoding="utf-8").read()
        missing = [pid for pid in ids if "material_thumbs/%s.png" % pid not in qrc]
        if missing:
            print("\nadd to qt_gui/resources.qrc:")
            for pid in missing:
                print("        <file>material_thumbs/%s.png</file>" % pid)
    finally:
        shutil.rmtree(scenes, ignore_errors=True)


if __name__ == "__main__":
    main()
