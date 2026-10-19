#!/usr/bin/env bash
# Everything the Mac session verifies before a release, in one command, ending in a report. It builds and checks; it never tags, uploads or publishes (that is a separate, deliberate step).
#
#   scripts/mac_release_check.sh [--out DIR] [--skip step,step] [--dmg FILE] [--combo-py PYTHON] [--list]
#
# Steps, in order (names for --skip):
#   preconditions  on main, clean tree, equal to origin/main, and CI of that commit not red (gh, when installed)
#   tests          cmake + ninja + ctest in build_macos (the Metal tests, sweeps, pbrt pack, parity golden): ~6-10 minutes
#   bundled        scripts/bundled_scene_compare.py --size 96 --spp 64: every bundled scene on the CPU and on Metal, none may differ
#   sweep          scripts/consistency_sweep.py (both backends): no pair off by more than 4%
#   combos         the Scene Builder's 400+ combination scenes, CPU against Metal (needs a Python with Pillow and numpy: --combo-py, or $COMBO_PY); at most 2 flagged (noise-level ones are known)
#   dmg            scripts/build_and_deploy_macos.sh --arch universal, unless --dmg FILE names one already built
#   verify         the dmg itself: hdiutil verify, the app copied out of it, codesign, both binaries universal, the shader files it carries (and none of the test kernels), checksum
#   selftest       the GUI self-test on THAT extracted copy (all default modes plus Live Preview)
# Every step logs to DIR/<step>.log; the report is DIR/report.md. Exit status 0 only when no step failed. A skipped step is listed as skipped, so a report cannot look complete when it is not.
#
# macOS ships bash 3.2: no associative arrays, no mapfile.
set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT=""
SKIP=","
DMG=""
COMBO_PY="${COMBO_PY:-}"
# a stray python3 earlier on PATH (an old framework install) can be killed on start: use the system one first, as gui_selftest_macos.sh does
PY=python3; [[ -x /usr/bin/python3 ]] && PY=/usr/bin/python3
ALL_STEPS="preconditions tests bundled sweep combos dmg verify selftest"

while [[ $# -gt 0 ]]; do
	case "$1" in
		--out) OUT="$2"; shift 2 ;;
		--skip) SKIP=",$2,"; shift 2 ;;
		--dmg) DMG="$2"; shift 2 ;;
		--combo-py) COMBO_PY="$2"; shift 2 ;;
		--list) for s in $ALL_STEPS; do echo "$s"; done; exit 0 ;;
		-h|--help) sed -n '2,22p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
		*) echo "unknown option: $1 (see --help)" >&2; exit 2 ;;
	esac
done
[[ -n "$OUT" ]] || OUT="$(mktemp -d "${TMPDIR:-/tmp}/mac_release_check.XXXXXX")"
mkdir -p "$OUT"
cd "$REPO_ROOT"
export PATH="$HOME/Qt/bin:$PATH"

REPORT_ROWS=""
FAILED=0
START_ALL=$(date +%s)
COMMIT="$(git rev-parse --short HEAD 2>/dev/null || echo unknown)"

record() {   # record <step> <PASS|FAIL|SKIP> <seconds> <detail>
	REPORT_ROWS="${REPORT_ROWS}| $1 | $2 | $3 s | $4 |"$'\n'
	[[ "$2" == "FAIL" ]] && FAILED=$((FAILED + 1))
	printf '%-14s %-5s %5s s  %s\n' "$1" "$2" "$3" "$4"
}

skipped() { [[ "$SKIP" == *",$1,"* ]]; }

# run <step> <function>: the function prints its one-line detail on its last line of output and returns non-zero when the step failed
run() {
	local step="$1" fn="$2"
	if skipped "$step"; then record "$step" SKIP 0 "skipped by --skip"; return; fi
	local t0 t1 detail rc
	t0=$(date +%s)
	"$fn" > "$OUT/$step.log" 2>&1
	rc=$?
	t1=$(date +%s)
	detail="$(tail -n 1 "$OUT/$step.log" | cut -c1-200)"
	if [[ $rc -eq 0 ]]; then record "$step" PASS $((t1 - t0)) "$detail"; else record "$step" FAIL $((t1 - t0)) "$detail (see $OUT/$step.log)"; fi
}

step_preconditions() {
	local branch
	branch="$(git rev-parse --abbrev-ref HEAD)"
	[[ "$branch" == "main" ]] || { echo "not on main (on $branch)"; return 1; }
	[[ -z "$(git status --porcelain)" ]] || { echo "the working tree has changes (git status)"; return 1; }
	git fetch -q origin || { echo "git fetch failed"; return 1; }
	[[ "$(git rev-parse HEAD)" == "$(git rev-parse origin/main)" ]] || { echo "main differs from origin/main (pull or push first)"; return 1; }
	if command -v gh >/dev/null 2>&1; then
		local red
		red="$(gh run list --commit "$(git rev-parse HEAD)" --json workflowName,conclusion -q '.[] | select(.conclusion == "failure") | .workflowName' 2>/dev/null | tr '\n' ' ')"
		[[ -z "$red" ]] || { echo "CI is red for this commit: $red"; return 1; }
	fi
	"$PY" -c "pass" 2>/dev/null || { echo "$PY does not run (a Python that starts is needed for the comparison scripts)"; return 1; }
	echo "on main at $COMMIT, clean, equal to origin/main, CI not red"
}

step_tests() {
	( cd build_macos && cmake .. > /dev/null && ninja && ctest -j4 --output-on-failure ) || { echo "build or ctest failed"; return 1; }
	echo "ctest passed"
}

step_bundled() {
	local line differ
	line="$("$PY" scripts/bundled_scene_compare.py --size 96 --spp 64 2>&1 | tee /dev/stderr | grep -E '^[0-9]+ scenes,' | tail -n 1)"
	differ="$(echo "$line" | sed -E 's/^[0-9]+ scenes, ([0-9]+) differ.*/\1/')"
	echo "${line:-no result line printed (the script did not finish)}"
	[[ "$differ" =~ ^[0-9]+$ ]] && [[ "$differ" -eq 0 ]]
}

step_sweep() {
	local line off
	line="$("$PY" scripts/consistency_sweep.py 2>&1 | tee /dev/stderr | grep -E '^[0-9]+ pairs,' | tail -n 1)"
	off="$(echo "$line" | sed -E 's/^[0-9]+ pairs, ([0-9]+) off.*/\1/')"
	echo "${line:-no result line printed (the script did not finish)}"
	[[ "$off" =~ ^[0-9]+$ ]] && [[ "$off" -eq 0 ]]
}

step_combos() {
	[[ -n "$COMBO_PY" && -x "$COMBO_PY" ]] || { echo "no Python with Pillow and numpy: pass --combo-py (or --skip combos)"; return 1; }
	local dir="$OUT/combos" line flagged
	mkdir -p "$dir"
	cmake --build build_tests --target cpu_integrator_tests > /dev/null || { echo "building cpu_integrator_tests failed"; return 1; }
	TEMP="$OUT" RT_COMBO_SCENES_DIR="$dir" build_tests/cpu_integrator_tests_mac --gtest_filter='SceneCombosTest.*' > /dev/null || { echo "generating the combination scenes failed"; return 1; }
	line="$("$COMBO_PY" scripts/combo_sweep.py --dir "$dir" --exe build_macos/ray_tracer --skip-wavefront 2>&1 | tee /dev/stderr | grep -E '^[0-9]+ scenes, ' | tail -n 1)"
	flagged="$(echo "$line" | sed -E 's/^[0-9]+ scenes, ([0-9]+) with problems.*/\1/')"
	echo "$line (at most 2 are tolerated: noise-level ones)"
	[[ "$flagged" =~ ^[0-9]+$ ]] && [[ "$flagged" -le 2 ]]
}

step_dmg() {
	if [[ -n "$DMG" ]]; then
		[[ -f "$DMG" ]] || { echo "--dmg: $DMG does not exist"; return 1; }
		cp "$DMG" "$OUT/RayTracerGUI-macOS-universal.dmg" && echo "using $DMG"
		return
	fi
	bash scripts/build_and_deploy_macos.sh --arch universal || { echo "the dmg build failed"; return 1; }
	cp RayTracer_Package_macOS/RayTracerGUI.dmg "$OUT/RayTracerGUI-macOS-universal.dmg" || { echo "no dmg was produced"; return 1; }
	echo "built RayTracerGUI-macOS-universal.dmg"
}

step_verify() {
	local dmg="$OUT/RayTracerGUI-macOS-universal.dmg" mount="$OUT/mount" app="$OUT/app/RayTracerGUI.app" rc=0
	[[ -f "$dmg" ]] || { echo "no dmg in $OUT (the dmg step did not run)"; return 1; }
	hdiutil verify "$dmg" > /dev/null || { echo "hdiutil verify failed"; return 1; }
	mkdir -p "$mount" "$OUT/app"
	hdiutil attach "$dmg" -mountpoint "$mount" -nobrowse -readonly -quiet || { echo "could not mount the dmg"; return 1; }
	cp -R "$mount/RayTracerGUI.app" "$OUT/app/" || rc=1
	hdiutil detach "$mount" -quiet || true
	[[ $rc -eq 0 ]] || { echo "the app is not in the dmg"; return 1; }
	codesign --verify --deep --strict "$app" || { echo "codesign --verify failed"; return 1; }
	local bin
	for bin in "$app/Contents/MacOS/RayTracerGUI" "$app/Contents/MacOS/ray_tracer"; do
		lipo -archs "$bin" | grep -q arm64 && lipo -archs "$bin" | grep -q x86_64 || { echo "$(basename "$bin") is not universal: $(lipo -archs "$bin")"; return 1; }
	done
	# the shader files the app carries: exactly the production list (metal_poc_shader_files.h), and none of the test kernels
	local want have
	want="$(grep -cE '^[[:space:]]+"metal_poc_[a-z_0-9]+\.metal",' gpu/metal/shaders/metal_poc_shader_files.h)"
	have="$(ls "$app/Contents/MacOS" | grep -cE '^metal_poc_.*\.metal$')"
	[[ "$want" == "$have" ]] || { echo "the app carries $have shader files, the production list has $want"; return 1; }
	[[ ! -e "$app/Contents/MacOS/metal_poc_test_kernels.metal" ]] || { echo "the app carries the test kernels"; return 1; }
	echo "dmg verified: $(du -h "$dmg" | cut -f1), sha256 $(shasum -a 256 "$dmg" | cut -c1-16)..., signature ok, universal, $have shader files"
}

step_selftest() {
	local app="$OUT/app/RayTracerGUI.app"
	[[ -d "$app" ]] || { echo "no extracted app (the verify step did not run)"; return 1; }
	# The installer carries three models and no environments (docs/MODELS.md), but the composite self-test adds a glTF model and a photographic sky from the libraries, which a source checkout has in
	# full. The self-test starts the app from "/", so the libraries are copied into this scratch copy of the app (after the dmg was verified, so its seal does not matter any more).
	mkdir -p "$app/Contents/MacOS/models"
	cp -R models/gltf "$app/Contents/MacOS/models/" && cp -R models/thumbnails "$app/Contents/MacOS/models/" && cp -R environments "$app/Contents/MacOS/" || { echo "could not copy the model and environment libraries into the extracted app"; return 1; }
	scripts/gui_selftest_macos.sh "$app" "$OUT/selftest" 2>&1 | tee /dev/stderr | grep -E '^(PASS|FAIL)' > "$OUT/selftest.summary"
	grep -q '^FAIL' "$OUT/selftest.summary" && { grep '^FAIL' "$OUT/selftest.summary" | head -n 1; return 1; }
	echo "$(grep -c '^PASS' "$OUT/selftest.summary") self-test modes passed on the extracted app"
}

echo "mac_release_check at $COMMIT, output in $OUT"
run preconditions step_preconditions
run tests step_tests
run bundled step_bundled
run sweep step_sweep
run combos step_combos
run dmg step_dmg
run verify step_verify
run selftest step_selftest

{
	echo "# Mac release check"
	echo
	echo "commit \`$COMMIT\` ($(git log -1 --format=%s 2>/dev/null | cut -c1-100)), $(date '+%Y-%m-%d %H:%M'), $(sw_vers -productVersion 2>/dev/null) on $(uname -m), $(( $(date +%s) - START_ALL )) s in all"
	echo
	echo "| step | result | time | detail |"
	echo "|---|---|---|---|"
	printf '%s' "$REPORT_ROWS"
} > "$OUT/report.md"

echo
if [[ $FAILED -eq 0 ]]; then echo "no step failed; report: $OUT/report.md"; else echo "$FAILED step(s) FAILED; report: $OUT/report.md"; fi
[[ $FAILED -eq 0 ]]
