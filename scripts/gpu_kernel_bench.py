#!/usr/bin/env python3
"""GPU time per kernel for a one-worker "Pip and the Moon" render of a few frames, from Nsight Systems (Windows; nsys needs no administrator rights, the hardware counters of ncu do).

usage: python scripts/gpu_kernel_bench.py [frames] [width] [height] [spp] [label]

The label is only printed. Two runs of the same build agree to 0.1%, so a difference of a few percent in one kernel is real. After a change to the GPU code the FIRST render takes many
minutes (OptiX compiles the PTX again, 20 minutes after a change to a header the layered BxDF is in; see docs/VIDEO_GENERATION.md): render one still before timing, or the workers'
stall watchdog restarts the compile again and again and nothing is ever finished."""
import csv, glob, io, os, shutil, subprocess, sys, tempfile

frames = sys.argv[1] if len(sys.argv) > 1 else "4"
w = sys.argv[2] if len(sys.argv) > 2 else "1280"
h = sys.argv[3] if len(sys.argv) > 3 else "720"
spp = sys.argv[4] if len(sys.argv) > 4 else "64"
tag = sys.argv[5] if len(sys.argv) > 5 else "run"
nsys = (sorted(glob.glob(r"C:\Program Files\NVIDIA Corporation\Nsight Systems *\target-windows-x64\nsys.exe")) or ["nsys"])[-1]
pkg = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "RayTracer_Package")
tmp = tempfile.mkdtemp(prefix="kb_")
rep = os.path.join(tmp, "p")
subprocess.run([nsys, "profile", "-o", rep, "--force-overwrite", "true", "--trace=cuda", os.path.join(pkg, "ray_tracer.exe"), "--gpu", "--cartoon-workers", "1", "--output", os.path.join(tmp, "v.ppm"),
                "--video-preset", "pip-and-the-moon", "--frames", frames, "--height", h, w, spp, "8"], cwd=pkg, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
out = subprocess.run([nsys, "stats", "--report", "cuda_gpu_kern_sum", "--format", "csv", rep + ".nsys-rep"], stdout=subprocess.PIPE, text=True).stdout
rows = list(csv.DictReader(io.StringIO(out[out.index("Time (%)"):])))
total = sum(float(r["Total Time (ns)"]) for r in rows)
print("[%s] frames=%s %sx%s spp=%s  GPU kernel total %.3f s (%.3f s/frame)" % (tag, frames, w, h, spp, total / 1e9, total / 1e9 / int(frames)))
for r in rows[:6]:
    print("   %-28s %8.3f s  %5d launches" % (r["Name"], float(r["Total Time (ns)"]) / 1e9, int(r["Instances"])))
shutil.rmtree(tmp, ignore_errors=True)
