#!/usr/bin/env python3
"""Metal against real pbrt-v4: renders every scene of tests/data/pbrt_reference_means.txt on the Metal backend (and, with --cpu, the CPU too) at the size, sample count and depth the
pack was made with, and prints each scene's mean per channel as a ratio of pbrt's. A scene is listed when a channel is off by more than --tolerance percent (default 3).
The pack's own cpu_ratio column says how far the CPU is, so a scene where Metal is off and the CPU is not is Metal's to fix.

  python3 scripts/metal_pbrt_pack.py [--exe build_macos/ray_tracer] [--tolerance 3] [--only name,name] [--cpu] [--spp-factor 4]
  python3 scripts/metal_pbrt_pack.py --spp-factor 4 --write-baseline gpu/metal/tests/pbrt_pack_baseline.txt     # remember where Metal stands against pbrt (after a change you understand)
  python3 scripts/metal_pbrt_pack.py --spp-factor 4 --baseline gpu/metal/tests/pbrt_pack_baseline.txt [--drift 0.01] [--max-off 0.03]   # ctest metal_poc_pbrt_pack
The baseline gate fails (exit 1) when
  * a scene moved by more than --drift since the baseline was written,
  * a scene renders that is not in the baseline at all (a new row of the table would otherwise never be gated: write it with --write-baseline, after looking at its ratios),
  * a scene is off pbrt by more than --max-off in some channel and its baseline row has no "# known: <why>" note. Such a row is a value we know is wrong and have pinned; the note says
    whose fault it is (pbrt's table says the CPU is off too, a heavy-tailed scene, ...), so a pinned error cannot go unremarked. --write-baseline keeps the notes of the file it rewrites
    and marks new ones "# known: UNEXPLAINED", which fails until somebody writes the reason.

A scene with a small visible lamp is noisy at the pack's 64 samples (killeroo-simple reads 8.7% low on one seed and 4% high on another): --spp-factor 4 or more settles those.
"""
import argparse, os, subprocess, sys, tempfile
import shutil
import atexit
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
    ap.add_argument("--write-baseline", help="write each scene's Metal/pbrt ratios to this file")
    ap.add_argument("--baseline", help="compare each scene's ratios with this file: exit 1 when one moved by more than --drift, or a scene that rendered no longer does")
    ap.add_argument("--max-off", type=float, default=0.0, help="with --baseline: fail when a scene is off pbrt by more than this fraction in a channel unless its baseline row carries a '# known:' note (0 = do not check)")
    ap.add_argument("--drift", type=float, default=0.01, help="how far a ratio may move from the baseline (default 0.01 = 1%%; a small visible lamp is noisy at the pack's samples, so keep --spp-factor 4)")
    args = ap.parse_args()
    baseline, known = {}, {}
    old_known = {}
    for path in (args.write_baseline, args.baseline):   # the notes of the file being rewritten (or compared with) survive a rewrite
        if path and os.path.exists(path):
            for bl in open(path):
                if "# known:" in bl and not bl.startswith("#"):
                    old_known[bl.split()[0]] = bl.split("# known:", 1)[1].strip()
    if args.baseline:
        for bl in open(args.baseline):
            if bl.startswith("#") or not bl.strip():
                continue
            bf = bl.split()
            baseline[bf[0]] = [float(x) for x in bf[1:4]]
            if "# known:" in bl:
                known[bf[0]] = bl.split("# known:", 1)[1].strip()
    written, drifted = {}, []
    only = [x for x in args.only.split(",") if x]
    work = tempfile.mkdtemp(prefix="metal_pbrt_pack_")
    atexit.register(shutil.rmtree, work, ignore_errors=True)   # (it used to leave one folder of pictures behind per run)
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
            if name in baseline:
                drifted.append((name, "no longer renders"))
            continue
        ratio = [g / p if p > 1e-9 else 1.0 for g, p in zip(res["gpu"], ref)]
        written[name] = ratio
        if args.baseline and name not in baseline:
            drifted.append((name, "is not in the baseline (a new row of the table: look at its ratios %s, then add it with --write-baseline)" % " ".join("%.3f" % x for x in ratio)))
        if args.baseline and args.max_off > 0 and max(abs(x - 1.0) for x in ratio) > args.max_off and name not in known:
            drifted.append((name, "is off pbrt by %.1f%% and its baseline row has no '# known: <why>' note" % (max(abs(x - 1.0) for x in ratio) * 100.0)))
        if name in baseline:
            moved = max(abs(a - b) for a, b in zip(ratio, baseline[name]))
            if moved > args.drift:
                drifted.append((name, "ratios %s, baseline %s" % (" ".join("%.3f" % x for x in ratio), " ".join("%.3f" % x for x in baseline[name]))))
        worst = max(abs(x - 1.0) for x in ratio) * 100.0
        flag = worst > args.tolerance
        if flag:
            bad.append((name, worst))
        line_out = "%-48s metal/pbrt %s%s  (pbrt cpu ratio %.3f)" % (name, " ".join("%.3f" % x for x in ratio), "  <-- %.1f%%" % worst if flag else "", cpu_ratio[0])
        if res.get("cpu"):
            line_out += "  cpu/pbrt %s" % " ".join("%.3f" % (c / p if p > 1e-9 else 1.0) for c, p in zip(res["cpu"], ref))
        print(line_out, flush=True)
    print("\n%d scenes, %d off by more than %g%%" % (n, len(bad), args.tolerance))
    if args.write_baseline:
        with open(args.write_baseline, "w") as fh:
            fh.write("# Metal's mean / real pbrt-v4's mean per colour channel, written by scripts/metal_pbrt_pack.py --write-baseline (rendered at %d times the pack's samples).\n" % args.spp_factor)
            fh.write("# ctest metal_poc_pbrt_pack fails when a scene moves by more than --drift (0.01) from this. Rewrite it only after a change that moves Metal toward pbrt, and say so in the commit.\n")
            for nm in sorted(written):
                note = ""
                if max(abs(x - 1.0) for x in written[nm]) > (args.max_off if args.max_off > 0 else 0.03):
                    note = "  # known: " + old_known.get(nm, "UNEXPLAINED")
                fh.write("%s %.4f %.4f %.4f%s\n" % ((nm,) + tuple(written[nm]) + (note,)))
        print("baseline of %d scenes written to %s" % (len(written), args.write_baseline))
    if args.baseline:
        for nm, why in drifted:
            print("DRIFT %-44s %s" % (nm, why))
        print("%d scenes against the baseline of %d, %d moved by more than %g" % (len(written), len(baseline), len(drifted), args.drift))
        return 1 if drifted else 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
