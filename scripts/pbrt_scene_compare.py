#!/usr/bin/env python3
"""Renders the bundled scenes (pbrt_scenes/*.pbrt) with real pbrt-v4 and with this project's CPU renderer and reports where the pictures differ.

pbrt_compare.py does this for the Scene Builder's combination scenes (every shape under every material). This is the same check for the project's own gallery of hand-written
scenes: media, layered materials, hair, measured and projected lights, the other cameras. It needs a pbrt-v4 executable (see "Building pbrt-v4 on Windows" in docs/PBRT_SUPPORT.md).

    python scripts/pbrt_scene_compare.py --pbrt <pbrt.exe> [--dir pbrt_scenes] [--exe RayTracer_Package/ray_tracer.exe] [--only name,name] [--spp 64] [--size 64] [--depth 8]
                                         [--backend cpu|gpu|wavefront] [--jobs 2] [--timeout 300] [--out results.json] [--keep]

Both renderers get the same scene text with four things set the same way: the film is `--size` x `--size` pixels (this project's command line renders a square picture whatever the file's film is, so the file's aspect is not kept: a wide scene is framed by
its narrower side in both), the integrator is volpath with `--depth` bounces
(pbrt's own default is 5 and this project's command line takes the depth as an argument, so a file's own Integrator line would say different things to the two), and the sample count
and seed come from the command line. The copy is written beside the original (a scene finds its pictures and meshes relative to its own folder) as zz_cmp_<name>.pbrt and removed
afterwards. A scene one of the two cannot render (a feature pbrt-v4 does not have, such as the principled material, or a file this project's loader refuses) is listed apart:
that is a fact about the scene, not a difference of pictures.

Pictures are compared as linear values clipped to [0, 1], in 4x4 blocks: a scene is listed when the mean differs by more than --mean-tol percent (6) or the 99th percentile of the block
differences is above --block-tol (0.08, 0.15 for scenes with glass, subsurface, media or fireflies by name). Needs numpy.
"""
import argparse
import concurrent.futures
import json
import os
import re
import subprocess
import sys
import time

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pbrt_compare as pc   # (the PFM and EXR readers)

NOISY_WORDS = ("glass", "subsurface", "sss", "medium", "fog", "smoke", "cloud", "caustic", "dispers", "hair", "transmission", "bdpt")


def prepare(text, size, depth):
    """The scene text with the film, the integrator and nothing else changed."""
    m = re.search(r'Film\s+"[^"]*"\s+"integer xresolution"\s*\[\s*(\d+)\s*\]\s+"integer yresolution"\s*\[\s*(\d+)\s*\]', text)
    if m:
        ny = size   # (square: this project's command line renders a width x width picture whatever the file's film is)
        text = text[:m.start(1)] + str(size) + text[m.end(1):]
        m2 = re.search(r'"integer yresolution"\s*\[\s*(\d+)\s*\]', text)
        text = text[:m2.start(1)] + str(ny) + text[m2.end(1):]
    integrator = 'Integrator "volpath" "integer maxdepth" [ %d ]\n' % depth
    lines = text.split("\n")
    out, replaced = [], False
    for line in lines:
        if re.match(r'\s*Integrator\b', line):
            if not replaced:
                out.append(integrator.rstrip("\n"))
                replaced = True
            continue
        out.append(line)
    text = "\n".join(out)
    if not replaced:
        text = re.sub(r'(\n\s*WorldBegin\b)', "\n" + integrator.rstrip("\n") + r"\1", text, count=1)
    return text


def run(cmd, cwd, timeout, env=None):
    t0 = time.time()
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, cwd=cwd, timeout=timeout, env=env)
    except subprocess.TimeoutExpired:
        return 124, "timed out after %d s" % timeout, time.time() - t0
    text = re.sub(r"\[[0-9;]*m", "", r.stdout + r.stderr)
    errors = [l.strip() for l in text.splitlines() if "Error" in l or "error" in l]
    tail = " | ".join(errors[:2] if errors else [l for l in text.splitlines() if l.strip()][-2:])[:300]
    return r.returncode, tail, time.time() - t0


def blocks(img):
    h, w, _ = img.shape
    return np.clip(img, 0, 1)[:h // 4 * 4, :w // 4 * 4].reshape(h // 4, 4, w // 4, 4, 3).mean(axis=(1, 3))


def one(args, name, work):
    path = os.path.join(args.dir, name + ".pbrt")
    tmp_name = "zz_cmp_" + name
    tmp = os.path.join(args.dir, tmp_name + ".pbrt")
    res = {"scene": name}
    try:
        text = open(path, encoding="utf-8", errors="replace").read()
        with open(tmp, "w", encoding="utf-8", newline="") as f:
            f.write(prepare(text, args.size, args.depth))
        pfm = os.path.join(work, name + ".pfm")
        exr = os.path.join(work, name + ".exr")
        for p in (pfm, exr):
            if os.path.exists(p):
                os.remove(p)
        code, tail, secs = run([args.pbrt, "--spp", str(args.spp), "--seed", "1", "--outfile", pfm, tmp], os.getcwd(), args.timeout)
        res["pbrt_s"] = round(secs, 1)
        if code != 0 or not os.path.exists(pfm):
            res.update(status="pbrt", why=tail)
            return res
        cmd = [args.exe] + (["--cpu"] if args.backend == "cpu" else []) + ["--seed", "1", "--output", exr, str(args.size), str(args.spp), str(args.depth), tmp]
        env = dict(os.environ)
        if args.backend == "wavefront":
            env["RAY_TRACER_WAVEFRONT"] = "1"
        code, tail, secs = run(cmd, os.getcwd(), args.timeout, env)
        res["ours_s"] = round(secs, 1)
        if code != 0 or not os.path.exists(exr):
            res.update(status="ours", why=tail)
            return res
        a = pc.read_pfm(pfm)
        b = pc.read_exr(exr)
        if a.shape != b.shape:
            res.update(status="shape", why="pbrt %s, ours %s" % (a.shape, b.shape))
            return res
        ca, cb = np.clip(a, 0, 1), np.clip(b, 0, 1)
        d = np.abs(blocks(a) - blocks(b))
        mean_a, mean_b = float(ca.mean()), float(cb.mean())
        res.update(status="ok", mean_pbrt=round(mean_a, 4), mean_ours=round(mean_b, 4),
                   mean_pct=round(100.0 * (mean_b / mean_a - 1.0), 1) if mean_a > 1e-6 else 0.0,
                   block_p99=round(float(np.percentile(d, 99)), 3), blocks_over=int((d > 0.1).sum()), nan=bool(not np.isfinite(b).all() or not np.isfinite(a).all()))
        return res
    except Exception as e:   # (a scene must not stop the run)
        res.update(status="error", why=repr(e))
        return res
    finally:
        if not args.keep and os.path.exists(tmp):
            os.remove(tmp)


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    ap = argparse.ArgumentParser()
    ap.add_argument("--pbrt", required=True)
    ap.add_argument("--dir", default=os.path.join(root, "pbrt_scenes"))
    ap.add_argument("--exe", default=os.path.join(root, "RayTracer_Package", "ray_tracer.exe"))
    ap.add_argument("--only", default="")
    ap.add_argument("--skip", default="my-scene,zz_cmp_")
    ap.add_argument("--backend", default="cpu", choices=("cpu", "gpu", "wavefront"), help="which of this project's renderers to compare with pbrt: the CPU, the recursive OptiX one, or the wavefront OptiX one")
    ap.add_argument("--spp", type=int, default=64)
    ap.add_argument("--size", type=int, default=64)
    ap.add_argument("--depth", type=int, default=8)
    ap.add_argument("--jobs", type=int, default=2)
    ap.add_argument("--timeout", type=int, default=300)
    ap.add_argument("--mean-tol", type=float, default=6.0)
    ap.add_argument("--block-tol", type=float, default=0.08)
    ap.add_argument("--out", default="")
    ap.add_argument("--keep", action="store_true", help="keep the zz_cmp_ copies")
    args = ap.parse_args()
    args.exe = os.path.abspath(args.exe)
    args.pbrt = os.path.abspath(args.pbrt)
    args.dir = os.path.relpath(args.dir)
    work = os.path.join(os.environ.get("TEMP", "."), "pbrt_scene_compare")
    os.makedirs(work, exist_ok=True)
    names = sorted(f[:-5] for f in os.listdir(args.dir) if f.endswith(".pbrt"))
    skip = [s for s in args.skip.split(",") if s]
    names = [n for n in names if not any(s in n for s in skip)]
    if args.only:
        words = args.only.split(",")
        names = [n for n in names if any(w in n for w in words)]
    t0 = time.time()
    results = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for res in pool.map(lambda n: one(args, n, work), names):
            results.append(res)
            tag = res["status"]
            if tag == "ok":
                noisy = any(w in res["scene"] for w in NOISY_WORDS)
                over = abs(res["mean_pct"]) > args.mean_tol or res["block_p99"] > (args.block_tol * (2 if noisy else 1))
                print("%-4s %-44s mean %.4f vs %.4f (%+.1f%%)  block p99 %.3f  blocks>0.1 %d%s" % ("DIFF" if over else "ok", res["scene"], res["mean_pbrt"], res["mean_ours"], res["mean_pct"],
                      res["block_p99"], res["blocks_over"], "  NaN!" if res["nan"] else ""), flush=True)
                res["differs"] = over
            else:
                print("%-4s %-44s %s" % (tag.upper(), res["scene"], res.get("why", "")[:160]), flush=True)
    if args.out:
        with open(args.out, "w") as f:
            json.dump(results, f, indent=1)
    ok = [r for r in results if r["status"] == "ok"]
    diff = [r for r in ok if r.get("differs")]
    print("\n%d scenes: %d compared (%d differ), %d pbrt could not render, %d ours could not (%d s)" % (
        len(results), len(ok), len(diff), sum(r["status"] == "pbrt" for r in results), sum(r["status"] == "ours" for r in results), time.time() - t0))


if __name__ == "__main__":
    main()
