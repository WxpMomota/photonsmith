#!/usr/bin/env python3
"""Renders the Scene Builder's combination scenes with pbrt-v4 itself and with this project's CPU renderer, and reports where they differ.

Everything else in scripts/ compares one of our renderers with another of ours; this is the one check against an independent implementation (CPU == GPU agreement did not catch a
glass bug before: see docs/PBRT_SUPPORT.md). It needs a pbrt-v4 executable, which this repository does not build: see "Building pbrt-v4 on Windows" in docs/PBRT_SUPPORT.md.

    RT_COMBO_SCENES_DIR=<dir> ray_tracer_tests --gtest_filter=SceneCombosTest.*      # writes the scenes (run from the repository root)
    python scripts/pbrt_compare.py --pbrt <pbrt.exe> --dir <dir> [--exe RayTracer_Package/ray_tracer.exe] [--only sphere__,glass] [--spp 256] [--jobs 3] [--stats]

Both render at the file's size (64 x 64 for the combination scenes) and the same sample count, pbrt to a PFM and this project to a linear EXR. The pictures are compared as linear values
clipped to [0, 1], in 4x4 blocks: a line is printed when the mean differs by more than 6%, or the 99th percentile of the block differences is above the limit (0.08, or 0.15 for glass,
subsurface, transmission and emitters), or three or more blocks differ by more than 0.1. --stats prints the numbers for every scene instead. Needs numpy.
"""
import argparse
import concurrent.futures
import os
import struct
import subprocess
import sys
import time
import zlib

import numpy as np

NOISY = ("glass", "subsurface", "transmission", "emitter")
_LINES = {0: 1, 1: 1, 2: 1, 3: 16}   # lines per block for the compressions ray_tracer writes (none, RLE, ZIPS, ZIP)


def read_pfm(path):
    with open(path, "rb") as f:
        kind = f.readline().strip()
        w, h = map(int, f.readline().split())
        scale = float(f.readline())
        data = np.frombuffer(f.read(), dtype="<f4" if scale < 0 else ">f4")
    channels = 3 if kind == b"PF" else 1
    img = data.reshape(h, w, channels)[::-1]   # a PFM runs bottom to top
    return np.repeat(img, 3, axis=2) if channels == 1 else img


def read_exr(path):
    """Linear RGB of a scanline OpenEXR file with float channels, none/ZIPS/ZIP compression (what ray_tracer writes)."""
    b = open(path, "rb").read()
    assert b[:4] == b"v/1\x01", "not an OpenEXR file"
    i = 8
    attrs = {}
    while b[i] != 0:
        j = b.index(b"\0", i); name = b[i:j].decode(); i = j + 1
        j = b.index(b"\0", i); i = j + 1
        size = struct.unpack("<I", b[i:i + 4])[0]; i += 4
        attrs[name] = b[i:i + size]; i += size
    pos = i + 1
    ch = attrs["channels"]; names = []; k = 0
    while ch[k] != 0:
        j = ch.index(b"\0", k); names.append(ch[k:j].decode()); k = j + 1 + 16
    comp = attrs["compression"][0]; lpb = _LINES[comp]
    x0, y0, x1, y1 = struct.unpack("<4i", attrs["dataWindow"])
    W, H = x1 - x0 + 1, y1 - y0 + 1
    nb = (H + lpb - 1) // lpb
    offs = struct.unpack("<%dQ" % nb, b[pos:pos + 8 * nb])
    img = np.zeros((H, W, 3), dtype=np.float64)
    order = {n: k for k, n in enumerate(names)}
    for off in offs:
        y, size = struct.unpack("<ii", b[off:off + 8]); data = b[off + 8:off + 8 + size]
        rows = min(lpb, H - (y - y0)); raw = rows * len(names) * W * 4
        if comp != 0 and size < raw:
            data = zlib.decompress(data)
            a = bytearray(data)
            for t in range(1, len(a)):
                a[t] = (a[t - 1] + a[t] - 128) & 0xFF
            half = (len(a) + 1) // 2
            d = bytearray(len(a)); d[0::2] = a[:half]; d[1::2] = a[half:]; data = bytes(d)
        vals = np.frombuffer(data, dtype="<f4").reshape(rows, len(names), W)
        for c, n in enumerate("RGB"):
            img[y - y0:y - y0 + rows, :, c] = vals[:, order[n], :]
    return img


def blocks(img):
    h, w, _ = img.shape
    return img.reshape(h // 4, 4, w // 4, 4, 3).mean(axis=(1, 3))


def run(cmd, env=None, cwd=None):
    r = subprocess.run(cmd, capture_output=True, text=True, env=env, cwd=cwd, timeout=1800)
    tail = " | ".join([l for l in (r.stdout + r.stderr).splitlines() if l.strip()][-3:])[-240:]
    return r.returncode, tail


def compare_scene(args, name):
    scene = os.path.join(args.dir, name + ".pbrt")
    work = os.path.join(args.dir, "pbrt_out")
    os.makedirs(work, exist_ok=True)
    pfm, exr = os.path.join(work, name + ".pfm"), os.path.join(work, name + ".exr")
    for f in (pfm, exr):
        if os.path.exists(f):
            os.remove(f)
    code, tail = run([args.pbrt, "--spp", str(args.spp), "--seed", "1", "--outfile", pfm, scene])
    if code != 0 or not os.path.exists(pfm):
        return name, ["pbrt FAILED: " + tail]
    code2, tail2 = run([args.exe, "--cpu", "--seed", "1", "--output", exr, "64", str(args.spp), "8", scene])
    if code2 != 0 or not os.path.exists(exr):
        return name, ["ours FAILED: " + tail2]
    ref = np.clip(read_pfm(pfm).astype(np.float64), 0, 1)
    ours = np.clip(read_exr(exr), 0, 1)
    if args.mirror:
        ours = ours[:, ::-1]   # (pbrt is left-handed, this renderer right-handed: the same file comes out left-right mirrored; flip ours to compare everything else)
    if ref.shape != ours.shape:
        return name, ["sizes differ: pbrt %s, ours %s" % (ref.shape, ours.shape)]
    a, b = ref.mean(), ours.mean()
    rel = abs(b - a) / max(a, 1e-6)
    d = np.abs(blocks(ref) - blocks(ours)).mean(axis=2)
    p99, many = float(np.percentile(d, 99)), int((d > 0.10).sum())
    tol = 0.15 if any(k in name for k in NOISY) else 0.08
    if args.stats:
        return name, ["mean pbrt %.4f ours %.4f (%+.1f%%), block p99 %.3f, blocks > 0.1: %d" % (a, b, (b - a) / max(a, 1e-6) * 100, p99, many)]
    if rel > 0.06 or p99 > tol or many >= 3:
        return name, ["differs from pbrt: mean %.4f vs %.4f (%+.1f%%), 99th percentile of 4x4 block differences %.3f, %d blocks off by > 0.1" % (a, b, (b - a) / max(a, 1e-6) * 100, p99, many)]
    return name, []


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    ap = argparse.ArgumentParser()
    ap.add_argument("--pbrt", required=True, help="a pbrt-v4 executable")
    ap.add_argument("--dir", required=True)
    ap.add_argument("--exe", default=os.path.join(root, "RayTracer_Package", "ray_tracer.exe"))
    ap.add_argument("--only", default="")
    ap.add_argument("--spp", type=int, default=256)
    ap.add_argument("--jobs", type=int, default=3)
    ap.add_argument("--stats", action="store_true")
    ap.add_argument("--mirror", action="store_true", help="flip our picture left to right before comparing (our renderer draws a pbrt file mirrored: see docs/PBRT_SUPPORT.md)")
    args = ap.parse_args()
    args.pbrt, args.exe = os.path.abspath(args.pbrt), os.path.abspath(args.exe)
    names = sorted(f[:-5] for f in os.listdir(args.dir) if f.endswith(".pbrt"))
    if args.only:
        words = args.only.split(",")
        names = [n for n in names if any(w in n for w in words)]
    t0, bad = time.time(), 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for name, problems in pool.map(lambda n: compare_scene(args, n), names):
            for p in problems:
                print("%-34s %s" % (name, p), flush=True)
            bad += 1 if problems and not args.stats else 0
    print("\n%d scenes, %d differ from pbrt (%.0f s)" % (len(names), bad, time.time() - t0))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
