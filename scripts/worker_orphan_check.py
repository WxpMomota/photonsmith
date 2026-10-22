#!/usr/bin/env python3
"""Checks that the workers of a cartoon video end when the launcher that started them is killed (the GUI's Stop kills it). Starts a draft video, waits for its workers, kills the launcher the
hard way, and fails if a worker is still running a few seconds later. Windows: the workers are in a job object that dies with the launcher (launcher/process.h).
Usage: python scripts/worker_orphan_check.py [--exe PATH]"""
import argparse, os, subprocess, sys, tempfile, time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def workers_of(parent):
    if os.name != "nt":
        return []
    out = subprocess.run(["powershell", "-NoProfile", "-Command", "Get-CimInstance Win32_Process | Where-Object { $_.ParentProcessId -eq %d -and $_.CommandLine -like '*--cartoon-worker*' }" % parent + " | ForEach-Object { $_.ProcessId }"],
                         stdout=subprocess.PIPE, text=True).stdout
    return [int(x) for x in out.split()]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", default=os.path.join(REPO, "x64", "Release", "ray_tracer.exe"))
    args = ap.parse_args()
    out = tempfile.mkdtemp(prefix="orphan_check_")
    launcher = subprocess.Popen([args.exe, "--gpu", "--cartoon-workers", "2", "--output", os.path.join(out, "v.ppm"), "--video-preset", "ball-parade", "--frames", "400", "--height", "225", "400", "25", "6"],
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, cwd=os.path.join(REPO, "RayTracer_Package"))
    found = []
    for _ in range(60):
        time.sleep(0.5)
        found = workers_of(launcher.pid)
        if len(found) >= 2:
            break
    if len(found) < 2:
        launcher.kill()
        sys.exit("no workers started (is the GPU free?)")
    print("workers running:", found)
    subprocess.run(["taskkill", "/F", "/PID", str(launcher.pid)], stdout=subprocess.DEVNULL)   # the way the GUI's Stop ends it
    time.sleep(4)
    left = [p for p in workers_of(launcher.pid) if p in found]
    if left:
        for p in left:
            subprocess.run(["taskkill", "/F", "/PID", str(p)], stdout=subprocess.DEVNULL)
        sys.exit("FAIL: workers still running after their launcher was killed: %s" % left)
    print("OK: the workers ended with the launcher")


if __name__ == "__main__":
    main()
