#!/usr/bin/env python3
"""Renders every bundled pbrt_scenes/*.pbrt on the CPU and on the GPU (Metal on a Mac) and lists the scenes whose mean picture differs, and, by a left/right and top/bottom test, the ones
whose picture is mirrored or flipped (a mean cannot see that). A cheap, wide net for a GPU backend that has fallen behind the CPU reference (the Windows session compares the CPU and
OptiX with real pbrt-v4 in scripts/pbrt_scene_compare.py; this is the Mac counterpart without pbrt).

usage: python3 scripts/bundled_scene_compare.py [--exe build_macos/ray_tracer] [--size 48] [--spp 32] [--depth 8] [--only word,word] [--tolerance 8]
Scenes that need files that are not in the checkout are reported as "did not render".
"""
import argparse
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import consistency_sweep as cs

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def render(exe, backend, scene, out, size, spp, depth, timeout):
    try:
        r = subprocess.run([exe, "--" + backend, "--seed", "3", "--output", out, str(size), str(spp), str(depth), scene], capture_output=True, text=True, cwd=REPO, timeout=timeout)
    except subprocess.TimeoutExpired:
        return None
    return out if r.returncode == 0 and os.path.exists(out) else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", default=os.path.join(REPO, "build_macos", "ray_tracer"))
    ap.add_argument("--size", type=int, default=48)
    ap.add_argument("--spp", type=int, default=32)
    ap.add_argument("--depth", type=int, default=8)
    ap.add_argument("--only", default="")
    ap.add_argument("--tolerance", type=float, default=8.0, help="percent of the CPU mean")
    ap.add_argument("--timeout", type=int, default=300)
    args = ap.parse_args()
    only = [w for w in args.only.split(",") if w]
    scenes = sorted(f for f in os.listdir(os.path.join(REPO, "pbrt_scenes")) if f.endswith(".pbrt") and (not only or any(w in f for w in only)))
    work = tempfile.mkdtemp(prefix="bundled_compare.")
    bad = 0
    skipped = []
    for name in scenes:
        path = os.path.join("pbrt_scenes", name)
        out = {b: render(args.exe, b, path, os.path.join(work, name + "." + b + ".exr"), args.size, args.spp, args.depth, args.timeout) for b in ("cpu", "gpu")}
        if not out["cpu"] or not out["gpu"]:
            skipped.append("%s (%s)" % (name, ", ".join(b for b in ("cpu", "gpu") if not out[b])))
            continue
        try:
            mc, mg = cs.read_exr_means(out["cpu"]), cs.read_exr_means(out["gpu"])
            (rc, cc), (rg, cg) = cs.read_exr_rows_columns(out["cpu"]), cs.read_exr_rows_columns(out["gpu"])
        except Exception as e:   # (an EXR the small reader cannot read)
            skipped.append("%s (%s)" % (name, e))
            continue
        worst = max(abs(g - c) / max(abs(c), 1e-6) for c, g in zip(mc, mg)) * 100.0

        def lopsided(line):   # which half is brighter, and by how much (only counted when the CPU's two halves differ clearly)
            h = len(line) // 2
            a, b = sum(line[:h]), sum(line[h:])
            return (a - b) / max(a + b, 1e-9)
        def correlation(a, b):
            n = len(a)
            ma, mb = sum(a) / n, sum(b) / n
            da, db = [x - ma for x in a], [x - mb for x in b]
            denom = (sum(x * x for x in da) * sum(x * x for x in db)) ** 0.5
            return sum(x * y for x, y in zip(da, db)) / denom if denom > 0 else 0.0

        flips = []
        for what, lc, lg, pc_, pg in (("left-right", lopsided(cc), lopsided(cg), cc, cg), ("top-bottom", lopsided(rc), lopsided(rg), rc, rg)):
            # Lopsided the other way is not enough: a noisy picture (the realistic camera's, at 48 pixels) is lopsided by chance. The GPU's profile must also fit the CPU's
            # reversed profile clearly better than the CPU's own.
            if abs(lc) > 0.06 and lc * lg < 0 and abs(lg) > 0.02 and correlation(pc_, list(reversed(pg))) > correlation(pc_, pg) + 0.2:
                flips.append(what)
        if worst > args.tolerance or flips:
            bad += 1
            print("%-48s cpu %.4f gpu %.4f  %5.1f%% off%s" % (name, mc[1], mg[1], worst, ("   MIRRORED/FLIPPED: " + ", ".join(flips)) if flips else ""), flush=True)
    print("\n%d scenes, %d differ, %d did not render" % (len(scenes), bad, len(skipped)))
    for s in skipped:
        print("   did not render:", s)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
