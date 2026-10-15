#!/usr/bin/env python3
"""Metal against real pbrt-v4: renders every scene of tests/data/pbrt_reference_means.txt on the Metal backend (and, with --cpu, the CPU too) at the size, sample count and depth the
pack was made with, and prints each scene's mean per channel as a ratio of pbrt's. A scene is listed when a channel is off by more than --tolerance percent (default 3).
The pack's own cpu_ratio column says how far the CPU is, so a scene where Metal is off and the CPU is not is Metal's to fix.

  python3 scripts/metal_pbrt_pack.py [--exe build_macos/ray_tracer] [--tolerance 3] [--only name,name] [--cpu] [--spp-factor 4]

A scene with a small visible lamp is noisy at the pack's 64 samples (killeroo-simple reads 8.7% low on one seed and 4% high on another): --spp-factor 4 or more settles those.
"""
import argparse, os, subprocess, sys, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from consistency_sweep import read_exr_means

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", default=os.path.join(REPO, "build_macos", "ray_tracer"))
    ap.add_argument("--tolerance", type=float, default=3.0)
    ap.add_argument("--only", default="")
    ap.add_argument("--cpu", action="store_true", help="also render the CPU (a column to tell whose fault a difference is)")
    ap.add_argument("--spp-factor", type=int, default=1, help="render with this many times the pack's samples per pixel (the pbrt means are of the scene, not of the sample count)")
    args = ap.parse_args()
    only = [x for x in args.only.split(",") if x]
    work = tempfile.mkdtemp(prefix="metal_pbrt_pack_")
    bad = []
    n = 0
    for line in open(os.path.join(REPO, "tests", "data", "pbrt_reference_means.txt")):
        if line.startswith("#") or not line.strip():
            continue
        f = line.split()
        name, spp, depth, size = f[0], str(int(f[1]) * args.spp_factor), f[2], f[3]
        ref = [float(x) for x in f[4:7]]
        cpu_ratio = [float(f[8])]
        if only and name not in only:
            continue
        scene = os.path.join(REPO, "pbrt_scenes", name + ".pbrt")
        if not os.path.exists(scene):
            continue
        n += 1
        res = {}
        for backend in (("gpu", "cpu") if args.cpu else ("gpu",)):
            out = os.path.join(work, "%s_%s.exr" % (name, backend))
            r = subprocess.run([args.exe, "--" + backend, "--seed", "3", "--output", out, size, spp, depth, scene], capture_output=True, text=True, cwd=REPO, timeout=1800)
            if r.returncode != 0 or not os.path.exists(out):
                res[backend] = None
                continue
            res[backend] = read_exr_means(out)
        if res["gpu"] is None:
            print("%-48s did not render" % name)
            continue
        ratio = [g / p if p > 1e-9 else 1.0 for g, p in zip(res["gpu"], ref)]
        worst = max(abs(x - 1.0) for x in ratio) * 100.0
        flag = worst > args.tolerance
        if flag:
            bad.append((name, worst))
        line_out = "%-48s metal/pbrt %s%s  (pbrt cpu ratio %.3f)" % (name, " ".join("%.3f" % x for x in ratio), "  <-- %.1f%%" % worst if flag else "", cpu_ratio[0])
        if res.get("cpu"):
            line_out += "  cpu/pbrt %s" % " ".join("%.3f" % (c / p if p > 1e-9 else 1.0) for c, p in zip(res["cpu"], ref))
        print(line_out, flush=True)
    print("\n%d scenes, %d off by more than %g%%" % (n, len(bad), args.tolerance))
    return 0


if __name__ == "__main__":
    sys.exit(main())
