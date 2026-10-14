// metal_poc_subsurface_check.cpp - host-only checker for the metal_poc_subsurface CTest (see CMakeLists.txt / metal_poc_subsurface_test.sh).
// pbrt_scenes/subsurface-ball.pbrt is a Cornell box with two balls of pbrt's SubsurfaceMaterial (a tabulated BSSRDF). Rendered at 128x128 by the
// CPU renderer (512 spp), the mean 8-bit values of four windows are the numbers below; Metal's BSSRDF has to land within the tolerance of them. A renderer
// that falls back to an opaque grey diffuse (as Metal did before it had a BSSRDF), or lets the balls cast shadows, is far outside: the balls would come
// out about twice as bright. (The numbers are the CPU renderer's since the exit-normal fix: the balls read about 26% brighter than before it.)
//
// usage: metal_poc_subsurface_check <render.png> <tolerance-percent>
#define STB_IMAGE_IMPLEMENTATION
#include "../../src/external/stb_image.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv) {
    if (argc != 3) { fprintf(stderr, "usage: %s render.png tolerance-percent\n", argv[0]); return 2; }
    int w = 0, h = 0, n = 0;
    unsigned char* px = stbi_load(argv[1], &w, &h, &n, 3);
    if (!px || w != 128 || h != 128) { fprintf(stderr, "FAIL: could not load a 128x128 image from %s\n", argv[1]); return 1; }
    const double tol = atof(argv[2]) / 100.0;
    struct Win { const char* what; int x0, y0, x1, y1; double expect[3]; };
    const Win wins[] = {
        // (pbrt is left-handed and the picture of a pbrt file follows it, so the balls' x ranges are the mirror of an older picture's. The expected means are the CPU's with the
        // subsurface table that agrees with pbrt-v4 (592b2b2) and with subsurface surfaces blocking shadow rays as in pbrt (7a088ad), but WITHOUT the CPU's self-hit: since 7a088ad
        // the CPU's shadow rays from the ball's own exit vertices hit the ball again within 0.02 of their origin (a lone ball under a constant sky read 0.5376 where it read 0.5795
        // and pbrt reads the latter within 0.4%), so the live CPU picture reads 9 to 22% darker here. These numbers are the CPU renderer with that self-hit let through
        // (docs/GPU_SCENE_COMMON.md, "Subsurface shadows"); replace them with the CPU's own once it is fixed, they agree within 1 to 4%.)
        {"big ball",   64,  64,  96, 102, {75.4, 76.7, 69.2}},
        {"small ball", 32,  92,  50, 108, {47.5, 37.3, 32.0}},
        {"back wall",  38,  19,  90,  38, {99.7, 92.0, 84.8}},
        {"floor",      26, 108, 102, 124, {59.7, 54.4, 48.5}},
    };
    int bad = 0;
    for (const Win& win : wins) {
        double m[3] = {0, 0, 0};
        for (int y = win.y0; y < win.y1; ++y)
            for (int x = win.x0; x < win.x1; ++x)
                for (int c = 0; c < 3; ++c) m[c] += px[(y * w + x) * 3 + c];
        const double count = (double)(win.x1 - win.x0) * (win.y1 - win.y0);
        double worst = 0.0;
        for (int c = 0; c < 3; ++c) { m[c] /= count; worst = std::fmax(worst, std::fabs(m[c] - win.expect[c]) / win.expect[c]); }
        const bool ok = worst <= tol;
        printf("%-11s mean (%5.1f,%5.1f,%5.1f) vs CPU (%5.1f,%5.1f,%5.1f): off by %.1f%%  %s\n", win.what, m[0], m[1], m[2],
               win.expect[0], win.expect[1], win.expect[2], worst * 100.0, ok ? "ok" : "TOO FAR");
        if (!ok) ++bad;
    }
    stbi_image_free(px);
    return bad ? 1 : 0;
}
