#!/usr/bin/env python3
"""The quick checks CI runs on every push (the build-lists and code-size jobs of .github/workflows/unit-tests.yml), run here before a commit: about a second.

  check_build_lists.py   every test source is in both test build lists (or deliberately MSBuild-only)
  check_env_docs.py      every environment variable the code reads is in docs/ENVIRONMENT.md
  check_code_size.py     no new function over 300 lines or file over 2000, no known one growing

check_code_size.py only sees what git tracks, so a new source file not yet added is not measured (and a commit without it breaks a clean checkout's build): those are
listed too, as a warning. Passing all of this says nothing about the builds or the tests - it is what CI found wrong on 2026-10-28 when only the tests had been run.

Usage: python scripts/pre_commit_checks.py    (from anywhere; exit status 1 when a check fails)
"""
import os
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CHECKS = ("check_build_lists.py", "check_env_docs.py", "check_code_size.py")
SOURCE = (".cpp", ".h", ".hpp", ".cu", ".cuh", ".mm", ".metal", ".c", ".py", ".ps1", ".pro", ".txt", ".vcxproj", ".yml")


def main():
    failed = []
    for script in CHECKS:
        r = subprocess.run([sys.executable, os.path.join(REPO, "scripts", script)], cwd=REPO, capture_output=True, text=True)
        out = (r.stdout + r.stderr).strip()
        print(("ok    " if r.returncode == 0 else "FAIL  ") + script + (": " + out.splitlines()[0] if r.returncode == 0 and out else ""))
        if r.returncode != 0:
            print("      " + out.replace("\n", "\n      "))
            failed.append(script)
    untracked = subprocess.run(["git", "ls-files", "--others", "--exclude-standard"], cwd=REPO, capture_output=True, text=True).stdout.split()
    untracked = [f for f in untracked if f.endswith(SOURCE) and not f.startswith("pbrt_scenes/")]
    if untracked:
        print("note  untracked source files (not measured, and not in a commit until added):")
        for f in untracked:
            print("      " + f)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
