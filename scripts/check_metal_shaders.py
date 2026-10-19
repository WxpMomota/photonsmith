#!/usr/bin/env python3
"""Compiles the Metal shader sources offline, with Apple's own compiler and no GPU: the same files, in the same order, as the renderer's run-time compile.

The renderer builds its shaders from source on the machine that runs it (MTLDevice newLibraryWithSource), so a syntax error or an undeclared name is only found by a Mac with a
Metal device that renders something; the hosted CI runners have none, and a broken shader used to stay green there. `xcrun metal` needs neither a device nor the Metal toolchain
download, and takes about a second for all of it. Both variants the renderer compiles are checked (METAL_TRIANGLE_ANY_HIT on and off).

  python3 scripts/check_metal_shaders.py [--shader-dir gpu/metal/shaders] [--warnings]

Exit code 0: both variants compile. Anything else prints the compiler's errors. Skips (exit 0, says so) where `xcrun metal` does not exist (Linux, Windows).
"""
import argparse, os, re, shutil, subprocess, sys, tempfile

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--shader-dir", default=os.path.join(REPO, "gpu", "metal", "shaders"))
    ap.add_argument("--warnings", action="store_true", help="show the compiler's warnings too (they are hidden by default: the run-time compile ignores them)")
    args = ap.parse_args()
    if shutil.which("xcrun") is None:
        print("check_metal_shaders: no xcrun on this machine, skipped")
        return 0
    probe = subprocess.run(["xcrun", "-sdk", "macosx", "metal", "--version"], capture_output=True, text=True)
    if probe.returncode != 0:
        print("check_metal_shaders: xcrun has no Metal compiler on this machine, skipped:", (probe.stderr or "").strip()[:200])
        return 0
    header = open(os.path.join(args.shader_dir, "metal_poc_shader_files.h")).read()
    files = re.findall(r'"(metal_poc_[a-z_0-9]+\.(?:metal|h))"', header)   # the list the renderer concatenates, in its order
    if not files:
        print("check_metal_shaders: found no shader file names in metal_poc_shader_files.h")
        return 1
    missing = [f for f in files if not os.path.exists(os.path.join(args.shader_dir, f))]
    if missing:
        print("check_metal_shaders: listed but missing:", ", ".join(missing))
        return 1
    # a shader file that is on disk but not in the list is never compiled (the app packaging copies metal_poc_*.metal, so it would ship unused)
    on_disk = sorted(f for f in os.listdir(args.shader_dir) if re.fullmatch(r"metal_poc_[a-z_0-9]+\.metal", f) or f == "metal_poc_shared_types.h")
    unlisted = [f for f in on_disk if f not in files]
    if unlisted:
        print("check_metal_shaders: on disk but not in metal_poc_shader_files.h (never compiled):", ", ".join(unlisted))
        return 1
    # what a user's run-time compile concatenates (the production list), and what the shader unit tests compile (the same plus the test kernels): both must compile, in both variants
    tests = [f for f in files if "test_kernels" in f]
    production = [f for f in files if f not in tests]
    bad = 0
    with tempfile.TemporaryDirectory() as tmp:
        for what, names in (("production", production), ("production + test kernels", production + tests)):
            source = "\n".join(open(os.path.join(args.shader_dir, f)).read() for f in names)
            src = os.path.join(tmp, "all_shaders.metal")
            open(src, "w").write(source)
            for label, macros in (("default", []), ("METAL_TRIANGLE_ANY_HIT", ["-DMETAL_TRIANGLE_ANY_HIT=1"])):
                cmd = ["xcrun", "-sdk", "macosx", "metal", "-c"] + ([] if args.warnings else ["-w"]) + macros + [src, "-o", os.path.join(tmp, "all.air")]
                r = subprocess.run(cmd, capture_output=True, text=True)
                if r.returncode != 0:
                    bad += 1
                    print("check_metal_shaders: %s, %d files, variant %s FAILED:" % (what, len(names), label))
                    print((r.stderr or r.stdout)[:6000])
                else:
                    print("check_metal_shaders: %s, %d files (%d lines), variant %s compiles" % (what, len(names), source.count("\n") + 1, label))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
