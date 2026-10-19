// metal_poc_shader_tests_mipmap_ref.cpp -- the CPU's own mipmap (src/shared/mipmap.h: pbrt's MIPMap::Filter) as a reference for the shader test of bumpHeightFiltered().
// A translation unit of its own because mipmap.h and metal_poc_host_math.h (which the shader tests include) both define ToneMapMode; plain floats in and out, nothing shared.
#include "../../../src/shared/mipmap.h"

// `pixels` w x h floats (row 0 at the top), queries n triples (u, v, width) with v up as the shader takes it; out[i] = the Bilinear, Repeat lookup at the level that fits width.
void cpuMipmapReference(const float* pixels, int w, int h, const float* queries, int n, float* out) {
    std::vector<color> colours((size_t)w * h);
    for (size_t i = 0; i < colours.size(); ++i) colours[i] = color(pixels[i], pixels[i], pixels[i]);
    MipMapOptions opts;
    opts.filter = MipFilter::Bilinear;
    opts.wrap = MipWrapMode::Repeat;
    const mipmap reference(colours, w, h, opts);
    for (int i = 0; i < n; ++i) {
        const float u = queries[3 * i], v = queries[3 * i + 1], width = queries[3 * i + 2];
        // the image's rows run top-down: t = 1 - v. width = 2 * max(|ds|, |dt|): ds = width / 2 on one axis is the same max.
        out[i] = reference.filter(u, 1.0f - v, width * 0.5f, 0.0f, 0.0f, 0.0f).x();
    }
}
