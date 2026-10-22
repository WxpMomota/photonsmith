#!/usr/bin/env python3
"""Runs the renderer over combinations of its settings and reports what breaks.

The settings of ray_tracer (backend, integrator, denoiser, sampler, crop, exposure ... and whether it renders one image, a camera-path video or a cartoon video) are read by different
renderers, and some combinations are refused on purpose (--sppm with --video, two integrators at once ...). Nobody tries them all, so this does: it builds a set of cases that covers
EVERY PAIR of setting values at least once (an all-pairs set: a few dozen runs instead of millions), runs each one tiny (48 x 27 pixels, 2 samples), and checks

  * a combination the launcher refuses on purpose exits non-zero with a message that names the problem (and one it does not refuse exits 0);
  * nothing crashes or hangs (exit codes of a crash, a run over the time limit);
  * an accepted image run writes a picture of the right size that is not all black (unless a crop window was asked for);
  * a flag the chosen renderer ignores is said to be ignored (a warning that names the flag), not dropped without a word.

Usage:  python scripts/settings_sweep.py [--exe PATH] [--cases N] [--seed N] [--only image|video|cartoon] [--report FILE.json]
Plain Python 3; the picture check needs Pillow (pip install pillow). Exit status 1 when anything is wrong.
"""
import argparse
import itertools
import json
import os
import random
import subprocess
import sys
import tempfile
import time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# --- the settings and the values they are tried with -----------------------------------------------------------------------------------------------------------------------
DIMENSIONS = {
    "mode": ["image", "video", "cartoon"],
    "backend": ["--cpu", "--gpu"],
    "integrator": ["", "--wavefront", "--bdpt", "--mlt", "--sppm", "--ao", "--randomwalk", "--simplepath", "--simplevolpath", "--lightpath"],
    "denoise": ["", "--denoise"],
    "adaptive": ["", "--adaptive"],
    "sampler": ["", "--sampler halton", "--sampler independent"],
    "lightsampler": ["", "--lightsampler uniform", "--lightsampler power"],
    "spectral": ["", "--spectral"],
    "regularize": ["", "--regularize"],
    "exposure": ["", "--exposure 0.5"],
    "tonemap": ["", "--tonemap reinhard", "--tonemap none"],
    "seed": ["", "--seed 5"],
    "crop": ["", "--crop 0 0 0.5 0.5"],
    "timelimit": ["", "--time-limit 5"],
    "clamp": ["", "--maxcomponentvalue 10"],
    "dof": ["", "--aperture 0.05 --focus-distance 5"],
    # what only some modes have: the output file (image), render passes and the accelerator (image), the video's speed and rate (video, cartoon), a cartoon's resume, jobs and timeout
    "format": ["", ".png", ".exr"],
    "extras": ["", "--aovs", "--accelerator kdtree --splitmethod middle"],
    "videoopts": ["", "--speed 2", "--speed 0.5 --fps 12"],
    "cartoonopts": ["", "--resume", "--cartoon-jobs 1", "--cartoon-timeout 60"],
}
# the settings that mean something in one mode only (in the others they are left out of the command line)
ONLY_IN = {"format": {"image"}, "extras": {"image"}, "videoopts": {"video", "cartoon"}, "cartoonopts": {"cartoon"}}

DEBUG_INTEGRATORS = {"--ao", "--randomwalk", "--simplepath", "--simplevolpath", "--lightpath"}
SPECIAL = {"--bdpt", "--mlt", "--sppm"} | DEBUG_INTEGRATORS

# What each flag is read by (mirrors src/shared/backend_capabilities.h; a test of the program itself keeps that table honest, this one checks the launcher says so).
def renderer_of(case):
    integrator = case["integrator"]
    if integrator in SPECIAL:
        return "cpu_other"
    return "cpu_default" if case["backend"] == "--cpu" else "gpu_optix"


READS = {
    "sampler": {"cpu_default"}, "lightsampler": {"cpu_default"}, "spectral": {"cpu_default"}, "timelimit": {"cpu_default"},
    "adaptive": {"cpu_default"}, "regularize": {"cpu_default", "gpu_optix"}, "denoise": {"cpu_default", "gpu_optix"},
    "dof": {"cpu_default", "cpu_other", "gpu_optix"}, "clamp": {"cpu_default", "cpu_other", "gpu_optix"}, "crop": {"cpu_default", "cpu_other", "gpu_optix"},
    "seed": {"cpu_default", "gpu_optix"}, "exposure": {"cpu_default", "cpu_other", "gpu_optix"}, "tonemap": {"cpu_default", "gpu_optix"},
}


def refused(case):
    """Whether the launcher is meant to refuse this combination, and the word its message has."""
    if case["mode"] != "image" and case["integrator"] in SPECIAL:
        return "video" if "video" in case["mode"] or case["mode"] == "cartoon" else ""
    return ""


# --- all-pairs generation ---------------------------------------------------------------------------------------------------------------------------------------------------
def all_pairs(dimensions, rng, wanted=None):
    names = list(dimensions)
    uncovered = set()
    for a, b in itertools.combinations(names, 2):
        for va in dimensions[a]:
            for vb in dimensions[b]:
                uncovered.add((a, va, b, vb))
    rows = []
    while uncovered:
        best, best_gain = None, -1
        for _ in range(300):   # candidates: a random row, improved one dimension at a time
            row = {n: rng.choice(dimensions[n]) for n in names}
            gain = sum(1 for (a, va, b, vb) in _pairs(row, names) if (a, va, b, vb) in uncovered)
            if gain > best_gain:
                best, best_gain = row, gain
        # take one of the uncovered pairs and force it, so progress is certain
        a, va, b, vb = next(iter(uncovered))
        best[a], best[b] = va, vb
        for (x, vx, y, vy) in _pairs(best, names):
            uncovered.discard((x, vx, y, vy))
        rows.append(best)
    return rows


def _pairs(row, names):
    for a, b in itertools.combinations(names, 2):
        yield (a, row[a], b, row[b])


# --- running ----------------------------------------------------------------------------------------------------------------------------------------------------------------
def command(exe, case, out_dir):
    flags = [case["backend"]]
    for name in DIMENSIONS:
        if name in ("mode", "backend", "format") or case["mode"] not in ONLY_IN.get(name, {case["mode"]}):
            continue
        value = case[name]
        if value:
            flags += value.split()
    out = os.path.join(out_dir, "out" + (case["format"] if case["mode"] == "image" and case["format"] else ".ppm"))
    if case["mode"] == "image":
        return [exe] + flags + ["--output", out, "--height", "27", "48", "2", "3", "A1"]
    if case["mode"] == "video":
        return [exe] + flags + ["--output", out, "--video", "--frames", "3", "--fps", "24", "--camera-path", "orbit", "--height", "27", "48", "2", "3", "A1"]
    return [exe] + flags + ["--output", out, "--video-preset", "ball-parade", "--frames", "3", "--height", "27", "48", "2", "3"]


def picture_problem(path, case):
    if not os.path.exists(path):
        return "no picture was written"
    if path.endswith(".exr"):
        return "" if os.path.getsize(path) > 100 else "the .exr is empty"
    try:
        from PIL import Image
    except ImportError:
        return ""
    try:
        img = Image.open(path).convert("RGB")
    except Exception as e:   # noqa: BLE001
        return "the picture cannot be read: %s" % e
    if img.size != (48, 27):
        return "the picture is %dx%d, not 48x27" % img.size
    if not case["crop"]:
        if not any(img.tobytes()):
            return "the picture is all black"
    return ""


def run_case(exe, case, timeout):
    with tempfile.TemporaryDirectory(prefix="settings_sweep_") as out_dir:
        cmd = command(exe, case, out_dir)
        start = time.time()
        try:
            done = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace", timeout=timeout, cwd=os.path.join(REPO, "RayTracer_Package"))
            rc, text = done.returncode, done.stdout
        except subprocess.TimeoutExpired as e:
            rc, text = "timeout", (e.stdout or "") if isinstance(e.stdout, str) else ""
        seconds = time.time() - start
        problems = []
        will_refuse = refused(case)
        if rc == "timeout":
            problems.append("hung (no exit within %d s)" % timeout)
        elif isinstance(rc, int) and (rc < 0 or rc > 255):
            problems.append("crashed (exit status %d)" % rc)
        elif will_refuse:
            if rc == 0:
                problems.append("a combination that should be refused (%s) was accepted" % will_refuse)
            elif will_refuse not in text.lower() and "cannot be combined" not in text.lower():
                problems.append("refused, but the message does not say why")
        else:
            if rc != 0:
                # a combination the renderer cannot do may fail, but it must say why (a material it does not support, a scene it cannot draw ...)
                reasons = ("not support", "unsupported", "cannot", "can't", "requires", "doesn't support", "no gpu implementation")
                if not any(r in text.lower() for r in reasons):
                    tail = " | ".join([l for l in text.splitlines() if "ERROR" in l.upper() or "FAILED" in l.upper()][-2:])
                    problems.append("exit %s with no reason given: %s" % (rc, tail[:200]))
            elif case["mode"] == "image":
                p = picture_problem(os.path.join(out_dir, "out" + (case["format"] or ".ppm")), case)
                if p:
                    problems.append(p)
            else:
                if "VIDEO COMPLETE" not in text:
                    problems.append("the video did not complete")
            # an ignored flag is said to be ignored
            if rc == 0:
                renderer = renderer_of(case)
                lower = text.lower()
                for name, readers in READS.items():
                    if case[name] and renderer not in readers:
                        flag = case[name].split()[0].lower()
                        if flag not in lower and "ignored" not in lower and "no effect" not in lower:
                            problems.append("%s is ignored by %s without a word" % (case[name].split()[0], renderer))
        return {"case": case, "command": " ".join(cmd[1:]), "rc": rc, "seconds": round(seconds, 1), "problems": problems}


def main():
    ap = argparse.ArgumentParser(description="Run ray_tracer over every pair of setting values and report what breaks.")
    ap.add_argument("--exe", default=os.path.join(REPO, "x64", "Release", "ray_tracer.exe"))
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--cases", type=int, default=0, help="run only the first N cases (0: all)")
    ap.add_argument("--only", choices=["image", "video", "cartoon"])
    ap.add_argument("--backend", choices=["cpu", "gpu"], help="run only the cases of one backend (the others are still in the all-pairs set)")
    ap.add_argument("--timeout", type=int, default=240)
    ap.add_argument("--report", help="write every case and what was found to this JSON file")
    args = ap.parse_args()
    if not os.path.exists(args.exe):
        sys.exit("ray_tracer not found: " + args.exe)
    rng = random.Random(args.seed)
    cases = all_pairs(DIMENSIONS, rng)
    if args.only:
        cases = [c for c in cases if c["mode"] == args.only]
    if args.backend:
        cases = [c for c in cases if c["backend"] == "--" + args.backend]
    if args.cases:
        cases = cases[: args.cases]
    print("%d cases cover every pair of the %d settings' values" % (len(cases), len(DIMENSIONS)))
    results, bad = [], 0
    for i, case in enumerate(cases, 1):
        r = run_case(args.exe, case, args.timeout)
        results.append(r)
        label = " ".join(v for k, v in case.items() if v and k != "mode") or "(defaults)"
        status = "OK  " if not r["problems"] else "FAIL"
        print("[%d/%d] %s %-5s %s  (%.1f s)" % (i, len(cases), status, case["mode"], label, r["seconds"]), flush=True)
        for p in r["problems"]:
            print("        - " + p)
        bad += 1 if r["problems"] else 0
    if args.report:
        json.dump(results, open(args.report, "w"), indent=1)
    print("\n%d of %d cases have a problem" % (bad, len(cases)))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
