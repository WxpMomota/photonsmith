#!/bin/sh
# CTest driver for metal_poc_error_event (see CMakeLists.txt): rendering a downloadable scene whose files are not on this machine through the real `ray_tracer --gpu` must end like the CPU
# and OptiX do (src/shared/render_events.h): exit code 3 and one line `@rt {"type":"error",...,"reason":"scene-file-missing",...}` naming the scene, so the GUI can say "download the files"
# in its own language instead of "use CPU mode". Uses the Bitterli living room (H22); skipped when it has been downloaded. Run from the source directory.
# usage: metal_poc_events_test.sh <ray_tracer> <scratch-dir>
RT="$1"; DIR="$2"
if [ -f pbrt_scenes/bitterli/living-room/scene-v4.pbrt ]; then echo "EVENT_TEST_SKIPPED: H22 is downloaded here"; exit 0; fi
"$RT" --gpu --output "$DIR/event_test.png" 64 4 4 H22 >"$DIR/event_test.log" 2>&1
code=$?
[ "$code" = 3 ] || { echo "FAIL: exit code $code, expected 3 (ERR_FILE_NOT_FOUND)"; tail -12 "$DIR/event_test.log"; exit 1; }
grep -q '^@rt {"type":"error","code":"3","reason":"scene-file-missing"' "$DIR/event_test.log" || { echo "FAIL: no scene-file-missing event"; tail -12 "$DIR/event_test.log"; exit 1; }
grep -q '"scene":"H22"' "$DIR/event_test.log" || { echo "FAIL: the event does not name the scene"; exit 1; }
echo EVENT_TEST_OK
