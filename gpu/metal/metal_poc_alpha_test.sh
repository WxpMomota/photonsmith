#!/bin/sh
# CTest driver for metal_poc_alpha_cutout (see CMakeLists.txt): renders pbrt_scenes/alpha-cutout.pbrt and its ObjectInstance twin (a square with a "texture alpha"
# cutout mask in front of a wall) through the real `ray_tracer --gpu` path and runs metal_poc_alpha_check on the picture.
# Prints ALPHA_TEST_OK only if the checker passes.
# usage: metal_poc_alpha_test.sh <ray_tracer> <metal_poc_alpha_check> <scratch-dir>
RT="$1"; CHK="$2"; DIR="$3"
# the square as a plain mesh, and the same square as an ObjectInstance (an instanced mesh's mask is tested through the instance's triangle base)
for scene in pbrt_scenes/alpha-cutout.pbrt gpu/metal/test_scenes/alpha-cutout-instanced.pbrt; do
    "$RT" --gpu --seed 5 --output "$DIR/alpha_test.png" 128 32 4 "$scene" >/dev/null 2>&1 || { echo "render failed: $scene"; exit 1; }
    echo "$scene"
    "$CHK" "$DIR/alpha_test.png" || exit 1
done
echo ALPHA_TEST_OK
