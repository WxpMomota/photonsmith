// metal_poc_bump_mip.h -- the mip chain of a bump height map, as the shader's bumpHeightFiltered() (metal_poc_bump.metal) reads it. Host side only, shared by the pbrt loader and the shader tests.
//
// Level 0 is already in `data` at `offset` (w x h floats, row 0 at the top). Each further level is appended straight after the one above it: a 2x2 box average, the size (n + 1) / 2 and the
// far edge repeated, down to one texel. For the power-of-two pictures the loader hands over (it resamples any other size up first, src/shared/image_resample.h) that is exactly pbrt's
// Image::GeneratePyramid, which the CPU's mipmap.h and the OptiX bump chain also build.
#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

inline void appendHeightMipChain(std::vector<float>& data, size_t offset, int w, int h) {
    size_t cur = offset;
    while (w > 1 || h > 1) {
        const int nw = std::max(1, (w + 1) / 2), nh = std::max(1, (h + 1) / 2);
        const size_t next = data.size();
        for (int y = 0; y < nh; ++y)
            for (int x = 0; x < nw; ++x) {
                const int x1 = std::min(2 * x + 1, w - 1), y1 = std::min(2 * y + 1, h - 1);
                const float v = 0.25f * (data[cur + (size_t)(2 * y) * w + 2 * x] + data[cur + (size_t)(2 * y) * w + x1] +
                                         data[cur + (size_t)y1 * w + 2 * x] + data[cur + (size_t)y1 * w + x1]);
                data.push_back(v);
            }
        cur = next; w = nw; h = nh;
    }
}
