#!/usr/bin/env python3
"""Check that every environment variable the code reads is in docs/ENVIRONMENT.md.

docs/ENVIRONMENT.md asks that a new variable be added to it in the same commit, and by 2026-10-23 eight were missing (the cartoon and background-mode ones). This finds the
names the C++, Python and PowerShell code reads (getenv, _dupenv_s, qEnvironmentVariable..., os.environ, $env:) that start with RAY_TRACER_ or RT_, and reports any the page
does not mention. A row may name a family with a trailing *, as `RT_GUI_SELFTEST_*` does.

Usage: python3 scripts/check_env_docs.py          (from anywhere; exit status 1 and the missing names on a mismatch)
"""
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DIRS = ("src", "launcher", "qt_gui", "gpu/optix", "cpu_renderer", "tests", "scripts", "tools")
SKIP = ("tests/build", "src/external", "_deps")
READ = re.compile(r'(?:getenv|_dupenv_s\([^,]*,[^,]*,|qEnvironmentVariable(?:IsSet|IsEmpty|IntValue)?|environmentValue|environ(?:\.get)?\(?\[?)\s*\(?\s*["\']((?:RAY_TRACER|RT)_[A-Z0-9_]+)["\']'
                  r'|\$env:((?:RAY_TRACER|RT)_[A-Z0-9_]+)')


def names_in_code():
    found = {}
    for d in DIRS:
        for root, _, files in os.walk(os.path.join(REPO, d)):
            rel = os.path.relpath(root, REPO).replace("\\", "/")
            if any(rel.startswith(s) or ("/" + s) in rel for s in SKIP):
                continue
            for f in files:
                if not f.endswith((".h", ".hpp", ".cpp", ".cu", ".mm", ".py", ".ps1")):
                    continue
                path = os.path.join(root, f)
                try:
                    text = open(path, encoding="utf-8", errors="replace").read()
                except OSError:
                    continue
                for m in READ.finditer(text):
                    found.setdefault(m.group(1) or m.group(2), os.path.relpath(path, REPO).replace("\\", "/"))
    return found


def main():
    doc = open(os.path.join(REPO, "docs", "ENVIRONMENT.md"), encoding="utf-8").read()
    families = re.findall(r"`((?:RAY_TRACER|RT|METAL)_[A-Z0-9_]*)\*`", doc)
    missing = []
    for name, where in sorted(names_in_code().items()):
        if ("`" + name + "`") in doc or any(name.startswith(f) for f in families):
            continue
        missing.append("%s (read in %s)" % (name, where))
    if missing:
        print("docs/ENVIRONMENT.md does not mention these environment variables the code reads:\n  " + "\n  ".join(missing))
        return 1
    print("docs/ENVIRONMENT.md mentions every environment variable the code reads")
    return 0


if __name__ == "__main__":
    sys.exit(main())
