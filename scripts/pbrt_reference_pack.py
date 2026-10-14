#!/usr/bin/env python3
"""Builds tests/data/pbrt_reference_means.txt: what real pbrt-v4 draws of every bundled scene it can read, and how far each of this project's renderers is from it.

pbrt_scene_compare.py compares the bundled scenes with pbrt, but only where a pbrt-v4 build exists (one machine). This freezes its answer into a table the unit tests read
(tests/unit/pbrt_reference_pack_tests.cpp), so a change that moves a scene away from pbrt is caught anywhere, without pbrt:

    python scripts/pbrt_reference_pack.py --pbrt <pbrt.exe> [--exe RayTracer_Package/ray_tracer.exe] [--only a,b] [--jobs 4] [--out tests/data/pbrt_reference_means.txt]

Per scene (64 x 64, depth 8): pbrt's mean picture value (the mean of every value of every channel, unclipped) at --pbrt-spp samples, twice with different seeds (their
disagreement is the noise of the reference); then for each renderer the ratio ours / pbrt at --ref-spp samples (1.0 for a scene that agrees, the known offset for one that does not:
the test then watches the offset, not just the agreement) and the noise of a single render at the test's own sample count (--test-spp, over --noise-runs seeds), which sets the
tolerance: max(3%, 4 x that noise + the reference's own).

Table columns (whitespace separated, `#` lines are comments):
    name  test_spp  depth  size  pbrt_mean_r  pbrt_mean_g  pbrt_mean_b  pbrt_noise  cpu_ratio cpu_tol  recursive_ratio recursive_tol  wavefront_ratio wavefront_tol
A partial run (--only) rewrites just its own rows. A renderer that failed on the scene (or took too long) has ratio 0, tolerance 0: the test skips it. Regenerate the whole table when pbrt-v4 is rebuilt or a renderer changes ON PURPOSE
(a change that should move a scene is exactly what the table exists to flag); commit the diff with the reason.
"""
import argparse
import concurrent.futures
import os
import re
import subprocess
import sys
import threading
import time

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import pbrt_compare as pc            # (the PFM and EXR readers)
import pbrt_scene_compare as psc     # (prepare(): the scene text both renderers get)

GPU_LOCK = threading.Lock()   # one GPU: the OptiX renders run one at a time


def run(cmd, timeout, env=None):
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout, env=env)
        return r.returncode
    except subprocess.TimeoutExpired:
        return 124


def pbrt_mean(args, tmp, tag, seed):
    pfm = os.path.join(args.work, tag + ".pfm")
    if os.path.exists(pfm):
        os.remove(pfm)
    code = run([args.pbrt, "--spp", str(args.pbrt_spp), "--seed", str(seed), "--outfile", pfm, tmp], args.timeout)
    if code != 0 or not os.path.exists(pfm):
        return None
    img = pc.read_pfm(pfm)
    return img.reshape(-1, 3).mean(axis=0) if np.isfinite(img).all() else None


def ours_mean(args, backend, scene_path, spp, seed, tag):
    exr = os.path.join(args.work, tag + ".exr")
    if os.path.exists(exr):
        os.remove(exr)
    cmd = [args.exe] + (["--cpu"] if backend == "cpu" else []) + ["--seed", str(seed), "--output", exr, str(args.size), str(spp), str(args.depth), scene_path]
    env = dict(os.environ)
    if backend == "wavefront":
        env["RAY_TRACER_WAVEFRONT"] = "1"
    if backend == "cpu":
        code = run(cmd, args.timeout, env)
    else:
        with GPU_LOCK:
            code = run(cmd, args.timeout, env)
    if code != 0 or not os.path.exists(exr):
        return None
    img = pc.read_exr(exr)
    return img.reshape(-1, 3).mean(axis=0) if np.isfinite(img).all() else None


def one(args, name):
    path = os.path.join(args.dir, name + ".pbrt")
    tmp = os.path.join(args.dir, "zz_pack_" + name + ".pbrt")
    try:
        with open(tmp, "w", encoding="utf-8", newline="") as f:
            f.write(psc.prepare(open(path, encoding="utf-8", errors="replace").read(), args.size, args.depth))
        a = pbrt_mean(args, tmp, name + "_p1", 1)
        b = pbrt_mean(args, tmp, name + "_p2", 2) if a is not None else None
        if a is None or b is None:
            return name, None, "pbrt cannot render it"
        ref = (a + b) / 2.0
        ref_scalar = float(ref.mean())
        if ref_scalar < 1e-5:
            return name, None, "black in pbrt"
        pbrt_noise = float(abs(a.mean() - b.mean()) / ref_scalar)
        cols = []
        for backend in ("cpu", "recursive", "wavefront"):
            if backend != "cpu" and not args.gpu:
                cols.append((0.0, 0.0)); continue
            kind = "cpu" if backend == "cpu" else backend
            m = ours_mean(args, "gpu" if backend == "recursive" else kind, path, args.ref_spp, 1, name + "_" + backend + "_ref")
            if m is None:
                cols.append((0.0, 0.0)); continue
            ratio = float(m.mean() / ref_scalar)
            runs = []
            for s in range(args.noise_runs):   # (the OptiX renders are quick: as many runs as the CPU, or the noise of a spectral wavefront scene is underestimated and its test flaky)
                v = ours_mean(args, "gpu" if backend == "recursive" else kind, path, args.test_spp, 100 + s, name + "_" + backend + "_n%d" % s)
                if v is not None:
                    runs.append(float(v.mean()))
            sigma = float(np.std(runs, ddof=1) / np.mean(runs)) if len(runs) >= 2 and np.mean(runs) > 0 else 0.1
            # A sampler that ignores the seed (halton) lands on the same value at every seed, a few % from its own converged mean at a low sample count (pbrt's does the
            # same): when the test-count mean is further from the converged one than noise explains, the table holds that value and the test watches it.
            if len(runs) >= 2 and np.mean(runs) > 0:
                off = float(np.mean(runs) / m.mean()) - 1.0
                if abs(off) > max(3.0 * sigma / len(runs) ** 0.5, 0.01):
                    ratio = float(np.mean(runs) / ref_scalar)
            tol = min(0.6, max(0.03, 4.0 * sigma + pbrt_noise))
            cols.append((ratio, tol))
        return name, (ref, pbrt_noise, cols), ""
    except Exception as e:   # (a scene must not stop the run)
        return name, None, repr(e)
    finally:
        if os.path.exists(tmp):
            os.remove(tmp)


def main():
    root = os.path.dirname(HERE)
    ap = argparse.ArgumentParser()
    ap.add_argument("--pbrt", required=True)
    ap.add_argument("--dir", default=os.path.join(root, "pbrt_scenes"))
    ap.add_argument("--exe", default=os.path.join(root, "RayTracer_Package", "ray_tracer.exe"))
    ap.add_argument("--out", default=os.path.join(root, "tests", "data", "pbrt_reference_means.txt"))
    ap.add_argument("--only", default="")
    ap.add_argument("--skip", default="my-scene,zz_")
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--size", type=int, default=64)
    ap.add_argument("--depth", type=int, default=8)
    ap.add_argument("--pbrt-spp", type=int, default=512)
    ap.add_argument("--ref-spp", type=int, default=512)
    ap.add_argument("--test-spp", type=int, default=64)
    ap.add_argument("--noise-runs", type=int, default=6)
    ap.add_argument("--timeout", type=int, default=180)
    ap.add_argument("--no-gpu", dest="gpu", action="store_false")
    args = ap.parse_args()
    args.work = os.path.join(os.environ.get("TEMP", "."), "pbrt_reference_pack")
    os.makedirs(args.work, exist_ok=True)
    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    args.exe = os.path.abspath(args.exe)
    skip = [s for s in args.skip.split(",") if s]
    names = sorted(os.path.splitext(f)[0] for f in os.listdir(args.dir) if f.endswith(".pbrt") and not any(f.startswith(s) for s in skip))
    if args.only:
        names = [n for n in names if n in args.only.split(",")]
    t0 = time.time()
    rows, skipped, kept = {}, {}, {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for name, data, why in pool.map(lambda n: one(args, n), names):
            if data is None:
                skipped[name] = why
                print("skip %-44s %s" % (name, why), flush=True)
            else:
                rows[name] = data
                ref, noise, cols = data
                print("ok   %-44s pbrt %.4f (noise %.1f%%)  cpu %.3f/%.0f%%  rec %.3f/%.0f%%  wf %.3f/%.0f%%" % (
                    name, ref.mean(), 100 * noise, cols[0][0], 100 * cols[0][1], cols[1][0], 100 * cols[1][1], cols[2][0], 100 * cols[2][1]), flush=True)
    if args.only and os.path.exists(args.out):   # a partial run updates its own rows and keeps the rest of the table
        for line in open(args.out, encoding="utf-8"):
            if line.strip() and not line.startswith("#"):
                kept[line.split()[0]] = line.rstrip("\n")
    with open(args.out, "w", newline="\n") as f:
        f.write("# pbrt-v4 reference means of the bundled scenes, and how far each renderer is from them. Written by scripts/pbrt_reference_pack.py; read by tests/unit/pbrt_reference_pack_tests.cpp.\n")
        f.write("# name test_spp depth size pbrt_mean_r pbrt_mean_g pbrt_mean_b pbrt_noise cpu_ratio cpu_tol recursive_ratio recursive_tol wavefront_ratio wavefront_tol\n")
        for name in sorted(set(rows) | set(kept)):
            if name not in rows:
                f.write(kept[name] + "\n")
                continue
            ref, noise, cols = rows[name]
            f.write("%s %d %d %d %.7g %.7g %.7g %.4f %s\n" % (
                name, args.test_spp, args.depth, args.size, ref[0], ref[1], ref[2], noise, " ".join("%.4f %.4f" % c for c in cols)))
    print("\n%d scenes in the table, %d skipped (%s), %.0f s" % (len(rows), len(skipped), "pbrt cannot read or black" if skipped else "-", time.time() - t0))


if __name__ == "__main__":
    main()
