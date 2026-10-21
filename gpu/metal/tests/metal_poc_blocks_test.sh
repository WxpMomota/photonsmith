#!/bin/sh
# CTest driver for metal_poc_texture_blocks (see CMakeLists.txt): the pbrt pack compares MEANS, and a roughness picture or a mix amount drawn as its average has the right mean. This renders
# each scene on the CPU and on Metal (the same seed, 512 samples, 64 pixels) and compares the two in blocks of 8 x 8 pixels with metal_poc_block_check: the 90th percentile of the
# relative block difference must be within the scene's tolerance (the Monte Carlo noise of 512 samples is about 1% per block). Run from the source directory.
# usage: metal_poc_blocks_test.sh <ray_tracer> <metal_poc_block_check> <scratch-dir>
RT="$1"; CHK="$2"; DIR="$3"
status=0
for entry in texture-params:0.05 texture-mix-amount:0.03 texture-means:0.03; do
    scene="${entry%%:*}"; tol="${entry##*:}"
    "$RT" --cpu --seed 3 --output "$DIR/blocks_$scene.cpu.exr" 64 512 12 pbrt_scenes/$scene.pbrt >"$DIR/blocks_$scene.log" 2>&1 || { echo "cpu render of $scene failed"; tail -8 "$DIR/blocks_$scene.log"; exit 1; }
    "$RT" --gpu --seed 3 --output "$DIR/blocks_$scene.gpu.exr" 64 512 12 pbrt_scenes/$scene.pbrt >"$DIR/blocks_$scene.log" 2>&1 || { echo "metal render of $scene failed"; tail -8 "$DIR/blocks_$scene.log"; exit 1; }
    echo "$scene:"
    "$CHK" "$DIR/blocks_$scene.cpu.exr" "$DIR/blocks_$scene.gpu.exr" 8 "$tol" || status=1
done
[ $status = 0 ] && echo BLOCKS_TEST_OK
exit $status
