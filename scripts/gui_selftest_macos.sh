#!/usr/bin/env bash
# macOS entry point for the GUI smoke test. The test itself is scripts/gui_selftest.py (the same one Windows uses); this wrapper keeps the
# old command working, runs it with Live Preview checks on (a Mac always has Metal), and picks a Python that works: /usr/bin/python3 first,
# since a stray python3 earlier on PATH (an old framework install) can be unusable.
#   scripts/gui_selftest_macos.sh [path/to/RayTracerGUI.app] [output-dir]
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
py=python3; [[ -x /usr/bin/python3 ]] && py=/usr/bin/python3
args=(--live-preview)
# A new build has to be turned into machine code by Metal once (about two minutes per shader variant on an M2, 108 s and 121 s measured on 2026-10-15), and the GUI self-test never starts
# the warm-up the GUI itself does. Without this the first run of the Scene Builder's Rendered viewport waits that long and fails ("0 frames"); a second run passed. Preparing the bundle's own
# ray_tracer first made the first run pass (measured once, 2026-10-15, after a real shader change); with a warm cache it takes about 2 seconds.
[[ -n "${1:-}" ]] && app_for_prepare="$1" || app_for_prepare="$here/../RayTracer_Package_macOS/RayTracerGUI.app"
if [[ -x "$app_for_prepare/Contents/MacOS/ray_tracer" ]]; then
	"$app_for_prepare/Contents/MacOS/ray_tracer" --prepare-gpu >&2 || echo "gui_selftest_macos: --prepare-gpu failed; the Rendered steps may time out" >&2
fi
[[ -n "${1:-}" ]] && app="$1" || app="$here/../RayTracer_Package_macOS/RayTracerGUI.app"
[[ -n "${2:-}" ]] && args+=(--out "$2")
exec "$py" "$here/gui_selftest.py" "$app" "${args[@]}"
