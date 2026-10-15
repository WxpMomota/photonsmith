#!/usr/bin/env python3
"""Seeded renders as a fingerprint, for refactors of the renderers' long functions: the same scene, size, samples and seed must give byte-identical pictures before and after a
change that moves code without changing what it computes.

    python scripts/refactor_check.py --exe x64/Release/ray_tracer.exe --write before.txt    # fingerprints (sha1 of the pixels) of every scene on the CPU and both OptiX backends
    python scripts/refactor_check.py --exe x64/Release/ray_tracer.exe --compare before.txt  # exit 1 and list what moved

--twice renders each one two times first and reports a render that is not reproducible by itself (such a scene cannot fingerprint anything). --only a,b limits it; --backends cpu,rec,wf.
"""
import argparse, hashlib, os, subprocess, sys, tempfile

SCENES = ["A1", "A1+--spectral", "cloud-medium-scene+--spectral", "A5", "subsurface-ball", "cloud-medium-scene", "measured-brdf-showroom", "texture-encoding-wrap-invert", "bump-mapped-plane", "hair-material", "curve-hair-tuft", "spectral-gamut-saturation",
          "chromatic-fog-room", "cornell-coated-conductor", "environment-sibenik-cathedral", "blackbody-light", "layered-materials", "instanced-spheres", "normal-mapped-plane"]
REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def render(exe, scene, backend, out, size, spp, depth):
    flags = []
    if "+" in scene:
        scene, flag = scene.split("+", 1)
        if backend != "cpu":
            return None
        flags = [flag]
    path = scene if scene.startswith("A") and scene[1:].isdigit() else os.path.join(REPO, "pbrt_scenes", scene + ".pbrt")
    if not (scene[0] == "A" and scene[1:].isdigit()) and not os.path.exists(path):
        return None
    cmd = [exe] + (["--cpu"] if backend == "cpu" else []) + flags + ["--seed", "5", "--output", out, str(size), str(spp), str(depth), path]
    env = dict(os.environ)
    if backend in ("rec", "wf"):
        env["RAY_TRACER_WAVEFRONT"] = "1" if backend == "wf" else "0"
    if os.path.exists(out):
        os.remove(out)
    r = subprocess.run(cmd, capture_output=True, text=True, cwd=REPO, env=env, timeout=1800)
    if r.returncode != 0 or not os.path.exists(out):
        return "FAILED"
    return hashlib.sha1(open(out, "rb").read()).hexdigest()[:16]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", required=True)
    ap.add_argument("--write")
    ap.add_argument("--compare")
    ap.add_argument("--only", default="")
    ap.add_argument("--backends", default="cpu,rec,wf")
    ap.add_argument("--twice", action="store_true")
    ap.add_argument("--size", type=int, default=40)
    ap.add_argument("--spp", type=int, default=12)
    ap.add_argument("--depth", type=int, default=6)
    a = ap.parse_args()
    exe = os.path.abspath(a.exe)
    tmp = tempfile.mkdtemp(prefix="refactor_check_")
    scenes = [s for s in SCENES if not a.only or s in a.only.split(",")]
    got = {}
    for s in scenes:
        for b in a.backends.split(","):
            out = os.path.join(tmp, "%s_%s.exr" % (s, b))
            h = render(exe, s, b, out, a.size, a.spp, a.depth)
            if h is None:
                continue
            if a.twice:
                h2 = render(exe, s, b, out, a.size, a.spp, a.depth)
                if h2 != h:
                    h += " (NOT REPRODUCIBLE)"
            got["%s/%s" % (s, b)] = h
            print("%-44s %s" % ("%s/%s" % (s, b), h), flush=True)
    if a.write:
        open(a.write, "w").write("".join("%s %s\n" % kv for kv in sorted(got.items())))
        print("wrote", a.write)
    if a.compare:
        want = {}
        for l in open(a.compare).read().splitlines():
            if l.strip() and "(NOT" not in l:   # a render that was not reproducible before cannot say anything now
                k, h = l.rsplit(" ", 1)
                want[k] = h
        bad = [k for k in got if k in want and got[k].split(" ")[0] != want[k].split(" ")[0]]
        for k in bad:
            print("MOVED", k, want[k], "->", got[k])
        print("%d of %d fingerprints moved" % (len(bad), len(got)))
        return 1 if bad else 0
    return 0


sys.exit(main())
