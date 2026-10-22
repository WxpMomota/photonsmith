#!/usr/bin/env python3
"""Measure what one frame of each cartoon film costs, and write it into the film table.

The cost is what a frame of the finished video takes: the film is rendered as a video of a few frames spread evenly over its length (so a heavy part and a light one both count), at
1280 x 720 and the samples the film's own Sampler line asks for, on the GPU with the denoiser, and the rendering time ray_tracer reports is divided by the frame count. The result goes
into `meta.secondsPerFrame` of the film's row in src/shared/cartoon/cartoons.h, which the scene list quotes ("A frame takes about 5 s").

  python scripts/measure_cartoons.py                 measure every film and print the table
  python scripts/measure_cartoons.py --write         ... and write the numbers into cartoons.h
  python scripts/measure_cartoons.py fireflies candle  only these films
  python scripts/measure_cartoons.py --frames 8      more frames per film (default 6: the time is reported in whole seconds, so more frames is a finer average)

Run it on a quiet machine (nothing else using the GPU), with the program built: it uses RayTracer_Package/ray_tracer or --exe PATH.
"""
import argparse
import os
import re
import subprocess
import sys
import tempfile

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TABLE = os.path.join(REPO, "src", "shared", "cartoon", "cartoons.h")


def find_exe(explicit):
    if explicit:
        return os.path.abspath(explicit)
    for name in ("ray_tracer.exe", "ray_tracer"):
        path = os.path.join(REPO, "RayTracer_Package", name)
        if os.path.exists(path):
            return path
    sys.exit("ray_tracer not found in RayTracer_Package/; pass --exe PATH")


def measure(exe, key, samples, frames):
    out_dir = tempfile.mkdtemp(prefix="measure_%s_" % key)
    cmd = [exe, "--gpu", "--denoise", "--video", "--camera-path", "cartoon:" + key, "--frames", str(frames), "--output", os.path.join(out_dir, "v.ppm"), "--height", "720", "1280", str(samples), "8"]
    done = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, cwd=os.path.join(REPO, "RayTracer_Package"))
    m = re.search(r"Rendering time: ([0-9.]+) seconds", done.stdout)
    if done.returncode != 0 or not m:
        sys.exit("render of %s failed:\n%s" % (key, done.stdout[-1500:]))
    return float(m.group(1)) / frames


# The samples each film asks for are constants of the film's own header (kSamples); this reads them out of the sources so that the measurement renders what the film recommends.
def samples_from_sources():
    result = {}
    folder = os.path.join(REPO, "src", "shared", "cartoon")
    for name in os.listdir(folder):
        if not name.endswith(".h"):
            continue
        text = open(os.path.join(folder, name), encoding="utf-8").read()
        m = re.search(r"constexpr int kSamples = (\d+);", text)
        if m:
            result[name[:-2].replace("_", "-")] = int(m.group(1))
    return result


def main():
    p = argparse.ArgumentParser(description="Measure what one frame of each cartoon film costs.")
    p.add_argument("keys", nargs="*", help="film keys (default: all)")
    p.add_argument("--exe")
    p.add_argument("--frames", type=int, default=6)
    p.add_argument("--write", action="store_true", help="write the numbers into cartoons.h")
    args = p.parse_args()
    exe = find_exe(args.exe)
    sources = samples_from_sources()
    text = subprocess.run([exe, "--list-video-presets"], stdout=subprocess.PIPE, text=True, cwd=os.path.join(REPO, "RayTracer_Package")).stdout
    keys = [line.split()[1] for line in text.splitlines() if len(line.split()) > 2 and line.split()[2] == "cartoon"]
    if args.keys:
        keys = [k for k in keys if k in args.keys]
    results = {}
    for key in keys:
        samples = sources.get(key)
        if samples is None:
            sys.exit("no kSamples found for %s" % key)
        seconds = measure(exe, key, samples, args.frames)
        results[key] = seconds
        print("%-16s %4d samples  %5.1f s per frame" % (key, samples, seconds))
    if args.write:
        table = open(TABLE, encoding="utf-8", newline="").read()
        for key, seconds in results.items():
            pattern = re.compile(r'(\{"%s", .*?::kSamples, )[0-9.]+(,\s*true\}|\})' % re.escape(key), re.S)
            table, n = pattern.subn(lambda m: "%s%.1f%s" % (m.group(1), seconds, m.group(2)), table, count=1)
            if n != 1:
                sys.exit("cannot find the row of %s in cartoons.h" % key)
        open(TABLE, "w", encoding="utf-8", newline="").write(table)
        print("written to", TABLE)


if __name__ == "__main__":
    main()
