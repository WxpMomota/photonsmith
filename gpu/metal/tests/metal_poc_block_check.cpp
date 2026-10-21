// metal_poc_block_check.cpp -- compares two linear EXR renders of the same scene block by block.
//
// A mean cannot see a picture that is wrong in its detail (a roughness image drawn as its average, a mix amount drawn as a flat weight): the pbrt pack's rows compare means. This compares the
// two pictures in blocks of B x B pixels (the mean of each block, so the Monte Carlo noise of two renders averages out) and reports how far the blocks are from each other:
//   metal_poc_block_check <a.exr> <b.exr> <block> [tolerance]
// prints the mean ratio of b / a, the median, 90th percentile and largest relative block difference (|a - b| / max(a, 0.05 * mean(a)) per colour sum), and exits 1 when the 90th percentile
// exceeds `tolerance` (a fraction, default none).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "../../../src/external/tinyexr.h"

static bool load(const char* path, std::vector<float>& rgb, int& w, int& h) {
    float* rgba = nullptr;
    const char* err = nullptr;
    if (LoadEXR(&rgba, &w, &h, path, &err) != TINYEXR_SUCCESS || !rgba) { std::fprintf(stderr, "cannot read %s: %s\n", path, err ? err : "?"); return false; }
    rgb.resize(size_t(w) * h * 3);
    for (size_t i = 0; i < size_t(w) * h; ++i) for (int c = 0; c < 3; ++c) rgb[i * 3 + c] = rgba[i * 4 + c];
    std::free(rgba);
    return true;
}

int main(int argc, char** argv) {
    if (argc < 4) { std::fprintf(stderr, "usage: %s a.exr b.exr block [tolerance]\n", argv[0]); return 2; }
    std::vector<float> a, b;
    int wa, ha, wb, hb;
    if (!load(argv[1], a, wa, ha) || !load(argv[2], b, wb, hb)) return 2;
    if (wa != wb || ha != hb) { std::fprintf(stderr, "size mismatch %dx%d vs %dx%d\n", wa, ha, wb, hb); return 2; }
    const int block = std::max(1, std::atoi(argv[3]));
    const double tolerance = argc > 4 ? std::atof(argv[4]) : -1.0;
    double sumA = 0.0, sumB = 0.0;
    for (size_t i = 0; i < a.size(); ++i) { sumA += a[i]; sumB += b[i]; }
    std::vector<double> bm;
    double meanA = sumA / double(a.size());
    for (int by = 0; by + block <= ha; by += block) {
        for (int bx = 0; bx + block <= wa; bx += block) {
            double va = 0.0, vb = 0.0;
            for (int y = by; y < by + block; ++y) for (int x = bx; x < bx + block; ++x) for (int c = 0; c < 3; ++c) { va += a[(size_t(y) * wa + x) * 3 + c]; vb += b[(size_t(y) * wa + x) * 3 + c]; }
            va /= double(block * block * 3); vb /= double(block * block * 3);
            bm.push_back(std::fabs(va - vb) / std::max(va, 0.05 * meanA));
        }
    }
    std::sort(bm.begin(), bm.end());
    const auto q = [&](double f) { return bm.empty() ? 0.0 : bm[std::min(bm.size() - 1, size_t(f * double(bm.size())))]; };
    std::printf("blocks %zu of %dx%d: mean ratio b/a %.4f, relative block difference median %.4f, p90 %.4f, max %.4f\n", bm.size(), block, block, sumB / std::max(sumA, 1e-30), q(0.5), q(0.9), bm.empty() ? 0.0 : bm.back());
    if (tolerance >= 0.0 && q(0.9) > tolerance) { std::printf("FAIL: the 90th percentile %.4f is above %.4f\n", q(0.9), tolerance); return 1; }
    return 0;
}
