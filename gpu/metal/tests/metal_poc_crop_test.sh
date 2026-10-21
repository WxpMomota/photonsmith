#!/bin/sh
# CTest driver for metal_poc_crop_window (see CMakeLists.txt): renders scene
# A1 twice through the real `ray_tracer --gpu` path - once whole, once with
# --crop 0.25 0.25 0.75 0.75 - then runs metal_poc_crop_check on the pair.
# At 128x128 that window resolves to pixels [32,96) x [32,96) (the shared
# lround()-based resolver, src/shared/cameras.h). Prints CROP_TEST_OK only if
# the checker passes. Then the same through a scene file whose Film has a cropwindow, and the cut to the window.
# usage: metal_poc_crop_test.sh <ray_tracer> <metal_poc_crop_check> <scratch-dir>
RT="$1"; CHK="$2"; DIR="$3"
"$RT" --gpu --output "$DIR/crop_test_full.png" 128 8 3 A1 >"$DIR/crop_render.log" 2>&1 || { echo "full render failed"; tail -15 "$DIR/crop_render.log"; exit 1; }
"$RT" --gpu --crop 0.25 0.25 0.75 0.75 --output "$DIR/crop_test_crop.png" 128 8 3 A1 >"$DIR/crop_render.log" 2>&1 || { echo "crop render failed"; tail -15 "$DIR/crop_render.log"; exit 1; }
"$CHK" "$DIR/crop_test_full.png" "$DIR/crop_test_crop.png" 32 96 32 96 8 || exit 1
# The file's own Film "cropwindow" (0.25 0.75 0.25 0.75) must draw the same window --crop draws: black outside, the uncropped pixels inside.
S=gpu/metal/tests/test_scenes
"$RT" --gpu --output "$DIR/crop_file_full.png" 128 8 3 $S/film-nocrop.pbrt >"$DIR/crop_render.log" 2>&1 || { echo "file full render failed"; tail -15 "$DIR/crop_render.log"; exit 1; }
"$RT" --gpu --output "$DIR/crop_file_crop.png" 128 8 3 $S/film-cropwindow.pbrt >"$DIR/crop_render.log" 2>&1 || { echo "file crop render failed"; tail -15 "$DIR/crop_render.log"; exit 1; }
"$CHK" "$DIR/crop_file_full.png" "$DIR/crop_file_crop.png" 32 96 32 96 8 || exit 1
# A scene whose crop is its picture is written as the window alone (METAL_CUT_TO_CROP stands in for the registry flag, which only the Head has): 64 x 64.
METAL_CUT_TO_CROP=1 "$RT" --gpu --output "$DIR/crop_file_cut.png" 128 8 3 $S/film-cropwindow.pbrt >"$DIR/crop_render.log" 2>&1 || { echo "cut render failed"; tail -15 "$DIR/crop_render.log"; exit 1; }
W=$(sips -g pixelWidth "$DIR/crop_file_cut.png" | awk '/pixelWidth/ {print $2}'); H=$(sips -g pixelHeight "$DIR/crop_file_cut.png" | awk '/pixelHeight/ {print $2}')
[ "$W" = 64 ] && [ "$H" = 64 ] || { echo "FAIL: the cut picture is ${W}x${H}, expected 64x64"; exit 1; }
echo CROP_TEST_OK
