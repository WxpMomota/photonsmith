// metal_poc_shader_tests_geometry.mm
// Where a ray comes from and what a surface looks like to it: the mip lookup of a bump height map, pbrt's cone sampling of a sphere light, and the side a spawned ray leaves from. Each
// is a shader function dispatched directly (metal_poc_test_kernels.metal) and checked against a host reference, so a wrong index, level or offset fails here, not as a few percent of a
// picture. The pictures themselves are checked against the CPU by scripts/consistency_sweep.py (the oddmap, spawn and fog groups, and metal_poc_cpu_gpu_sweep in ctest).
#include "metal_poc_shader_tests_common.h"
#include "metal_poc_bump_mip.h"
#include "../../src/shared/measured_rgb_quadrature.h"   // the wavelengths and weights a measured BSDF's table becomes RGB with (macros only)

// (metal_poc_shader_tests_mipmap_ref.cpp: the CPU mipmap, in a translation unit of its own)
void cpuMipmapReference(const float* pixels, int w, int h, const float* queries, int n, float* out);

// bumpHeightFiltered() against the CPU's mipmap (pbrt's MIPMap::Filter, Bilinear, Repeat): the same levels, the same level choice, the same lookup. A 64 x 32 height map (powers of two:
// the loader resamples any other size first), queried across the whole range of footprints, from none (the full-resolution lookup) to wider than the picture (the one-texel level), at
// points inside the picture and far outside it (Repeat).
void testBumpHeightFiltered(id<MTLDevice> device, id<MTLLibrary> library, id<MTLCommandQueue> queue) {
    const int w = 64, h = 32;
    std::mt19937 rng(8675309);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f), wide(-6.0f, 6.0f);
    std::vector<float> data((size_t)w * h);
    for (float& v : data) v = unit(rng) * unit(rng);   // a picture with detail at every scale
    const std::vector<float> level0 = data;
    appendHeightMipChain(data, 0, w, h);

    const int n = 600;
    std::vector<simd::float3> queries(n);
    for (int i = 0; i < n; ++i) {
        const float u = (i % 3 == 0) ? wide(rng) : unit(rng), v = (i % 3 == 1) ? wide(rng) : unit(rng);
        // footprints from 1/1024 of the picture to several pictures: log-uniform, plus some exact zeros (no footprint) and some beyond the last level
        const float width = (i % 10 == 0) ? 0.0f : std::exp2(-10.0f + 12.5f * unit(rng));
        queries[i] = simd::float3{u, v, width};
    }
    std::vector<float> flat(3 * (size_t)n), expected(n);
    for (int i = 0; i < n; ++i) { flat[3 * i] = queries[i].x; flat[3 * i + 1] = queries[i].y; flat[3 * i + 2] = queries[i].z; }
    cpuMipmapReference(level0.data(), w, h, flat.data(), n, expected.data());
    const int dims[2] = {w, h};
    id<MTLBuffer> dataBuf = makeBuffer(device, data.data(), data.size() * sizeof(float));
    id<MTLBuffer> qBuf = makeBuffer(device, queries.data(), n * sizeof(simd::float3));
    id<MTLBuffer> dimBuf = makeBuffer(device, dims, sizeof(dims));
    id<MTLBuffer> outBuf = makeOutputBuffer(device, n * sizeof(float));
    if (!runKernel(device, library, queue, @"test_bumpHeightFiltered", @[dataBuf, qBuf, dimBuf, outBuf], nil, n)) return;
    const float* out = (const float*)outBuf.contents;
    int bad = 0;
    for (int i = 0; i < n; ++i) {
        // the shader works in float32 with u up to +-6 on a 64-texel picture: a few 1e-5 of rounding in the texel position moves a lookup by a part in 1e4 of one texel's step
        if (std::fabs(out[i] - expected[i]) > 2e-4f + 1e-3f * std::fabs(expected[i])) {
            if (bad < 5) fprintf(stderr, "  bumpHeightFiltered case %d: u %.4f v %.4f width %.5f: shader %.6f, CPU mipmap %.6f\n", i, queries[i].x, queries[i].y, queries[i].z, out[i], expected[i]);
            ++bad;
        }
    }
    char label[160];
    snprintf(label, sizeof(label), "bumpHeightFiltered matches the CPU mipmap (Bilinear, Repeat) on %d queries (%d differ)", n, bad);
    expectTrue(label, bad == 0);
}

// Cone sampling of a sphere light from a point outside it (pbrt-v4 Sphere::Sample(ctx, u)): every point is on the sphere, on the side the reference point sees, its normal points out
// of the sphere, the pdf is 1 / (2 pi (1 - cos theta_max)) in solid angle, and the directions are uniform in the cone (the mean cosine about the axis is (1 + cos theta_max) / 2).
void testSphereLightCone(id<MTLDevice> device, id<MTLLibrary> library, id<MTLCommandQueue> queue) {
    std::mt19937 rng(424242);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f), coord(-4.0f, 4.0f), logR(-1.9f, 0.5f);
    const int cases = 400, perCase = 64;   // 64 stratified samples of each of 400 configurations; the distances run from just outside the sphere to a thousand radii away (the small-angle branch)
    const int n = cases * perCase;
    std::vector<simd::float3> centers(n), refs(n);
    std::vector<float> radii(n);
    std::vector<simd::float2> us(n);
    for (int c = 0; c < cases; ++c) {
        const simd::float3 centre{coord(rng), coord(rng), coord(rng)};
        const float radius = std::exp(logR(rng));
        simd::float3 dir{coord(rng), coord(rng), coord(rng)};
        dir = simd::normalize(dir);
        const float distance = radius * (1.05f + std::exp(unit(rng) * 7.0f) * (c % 4 == 0 ? 6.0f : 0.05f));
        for (int s = 0; s < perCase; ++s) {
            const int i = c * perCase + s;
            centers[i] = centre;
            radii[i] = radius;
            refs[i] = centre + dir * distance;
            us[i] = simd::float2{((s % 8) + unit(rng)) / 8.0f, ((s / 8) + unit(rng)) / 8.0f};
        }
    }
    id<MTLBuffer> cBuf = makeBuffer(device, centers.data(), n * sizeof(simd::float3));
    id<MTLBuffer> rBuf = makeBuffer(device, radii.data(), n * sizeof(float));
    id<MTLBuffer> refBuf = makeBuffer(device, refs.data(), n * sizeof(simd::float3));
    id<MTLBuffer> uBuf = makeBuffer(device, us.data(), n * sizeof(simd::float2));
    id<MTLBuffer> pBuf = makeOutputBuffer(device, n * sizeof(simd::float3));
    id<MTLBuffer> nBuf = makeOutputBuffer(device, n * sizeof(simd::float3));
    id<MTLBuffer> pdfBuf = makeOutputBuffer(device, n * sizeof(float));
    if (!runKernel(device, library, queue, @"test_sphereLightCone", @[cBuf, rBuf, refBuf, uBuf, pBuf, nBuf, pdfBuf], nil, n)) return;
    const simd::float3* pts = (const simd::float3*)pBuf.contents;
    const simd::float3* nrm = (const simd::float3*)nBuf.contents;
    const float* pdf = (const float*)pdfBuf.contents;
    int offSphere = 0, notVisible = 0, badNormal = 0, badPdf = 0, badMean = 0;
    for (int c = 0; c < cases; ++c) {
        const double radius = radii[c * perCase];
        const simd::float3 toCentre = centers[c * perCase] - refs[c * perCase];
        const double d = simd::length(toCentre);
        const double sin2 = radius * radius / (d * d);
        const double cosMax = std::sqrt(std::max(0.0, 1.0 - sin2));
        const double expectedPdf = 1.0 / (2.0 * M_PI * (1.0 - cosMax));
        double meanCos = 0.0;
        for (int s = 0; s < perCase; ++s) {
            const int i = c * perCase + s;
            const simd::float3 p = pts[i], nn = nrm[i];
            // float32 positions at a thousand radii: the point is the centre plus radius times a unit vector, good to about 1e-6 of the distance
            if (std::fabs(simd::length(p - centers[i]) - radii[i]) > 2e-3f * radii[i] + 1e-6f * (float)d) ++offSphere;
            if (simd::dot(nn, (p - centers[i]) / radii[i]) < 0.995f) ++badNormal;
            if (simd::dot(nn, refs[i] - p) < -1e-4f * (float)d) ++notVisible;
            if (std::fabs(pdf[i] - expectedPdf) > 2e-3 * expectedPdf) ++badPdf;
            const simd::float3 toPoint = simd::normalize(p - refs[i]);
            meanCos += simd::dot(toPoint, simd::normalize(toCentre));
        }
        meanCos /= perCase;
        // uniform in the cone: the mean cosine about the axis is (1 + cosMax) / 2, to within the stratified sample's own spread (tiny cones have a tiny spread, so allow a floor)
        if (std::fabs(meanCos - 0.5 * (1.0 + cosMax)) > 0.02 * (1.0 - cosMax) + 2e-6) ++badMean;
    }
    char label[200];
    snprintf(label, sizeof(label), "cone-sampled points are on the sphere (%d of %d off)", offSphere, n); expectTrue(label, offSphere == 0);
    snprintf(label, sizeof(label), "cone-sampled normals point out of the sphere (%d bad)", badNormal); expectTrue(label, badNormal == 0);
    snprintf(label, sizeof(label), "cone-sampled points face the reference point (%d do not)", notVisible); expectTrue(label, notVisible == 0);
    snprintf(label, sizeof(label), "the cone pdf is 1 / (2 pi (1 - cos theta_max)) (%d bad)", badPdf); expectTrue(label, badPdf == 0);
    snprintf(label, sizeof(label), "the cone samples are uniform in solid angle (%d of %d configurations off)", badMean, cases); expectTrue(label, badMean == 0);
}

// spawnSide(): 0.001's direction is the geometric normal on the side of the ray; the old shading-normal offset where the geometric normal was not set.
void testSpawnSide(id<MTLDevice> device, id<MTLLibrary> library, id<MTLCommandQueue> queue) {
    const simd::float3 up{0, 1, 0}, down{0, -1, 0}, tilted = simd::normalize(simd::float3{0.9f, 0.2f, 0.0f}), none{0, 0, 0};
    const simd::float3 geometric[4] = {up, up, none, up};
    const simd::float3 fallbacks[4] = {tilted, tilted, tilted, tilted};
    const simd::float3 dirs[4] = {simd::normalize(simd::float3{0.1f, 1.0f, 0.0f}),     // leaves on the surface's side: up
                                  simd::normalize(simd::float3{0.9f, -0.05f, 0.0f}),   // the tilted shading normal would call this "above"; it goes below the surface: down
                                  simd::normalize(simd::float3{0.1f, -1.0f, 0.0f}),    // no geometric normal set: the fallback, whatever the direction
                                  simd::normalize(simd::float3{1.0f, 0.0f, 0.0f})};    // exactly along the surface: the surface's own side
    const simd::float3 expected[4] = {up, down, tilted, up};
    id<MTLBuffer> gBuf = makeBuffer(device, geometric, sizeof(geometric));
    id<MTLBuffer> fBuf = makeBuffer(device, fallbacks, sizeof(fallbacks));
    id<MTLBuffer> dBuf = makeBuffer(device, dirs, sizeof(dirs));
    id<MTLBuffer> oBuf = makeOutputBuffer(device, sizeof(expected));
    if (!runKernel(device, library, queue, @"test_spawnSide", @[gBuf, fBuf, dBuf, oBuf], nil, 4)) return;
    const simd::float3* out = (const simd::float3*)oBuf.contents;
    for (int i = 0; i < 4; ++i) {
        char label[120];
        snprintf(label, sizeof(label), "spawnSide case %d", i);
        expectTrue(label, simd::length(out[i] - expected[i]) < 1e-5f);
    }
}

// The measured-BSDF quadrature table in the shader is a copy of src/shared/measured_rgb_quadrature.h (the CPU and both OptiX backends read the header): they must be the same numbers.
void testMeasuredQuadratureTable(id<MTLDevice> device, id<MTLLibrary> library, id<MTLCommandQueue> queue) {
    const float lambdas[RT_MEASURED_BINS] = { RT_MEASURED_LAMBDAS };
    const float wr[RT_MEASURED_BINS] = { RT_MEASURED_WR }, wg[RT_MEASURED_BINS] = { RT_MEASURED_WG }, wb[RT_MEASURED_BINS] = { RT_MEASURED_WB };
    id<MTLBuffer> outBuf = makeOutputBuffer(device, RT_MEASURED_BINS * 4 * sizeof(float));
    if (!runKernel(device, library, queue, @"test_measuredQuadratureTable", @[outBuf], nil, RT_MEASURED_BINS)) return;
    const float* out = (const float*)outBuf.contents;
    int bad = 0;
    for (int k = 0; k < RT_MEASURED_BINS; ++k)
        if (out[4 * k] != lambdas[k] || out[4 * k + 1] != wr[k] || out[4 * k + 2] != wg[k] || out[4 * k + 3] != wb[k]) ++bad;
    char label[160];
    snprintf(label, sizeof(label), "measured quadrature table: the shader's %d wavelengths and weights equal measured_rgb_quadrature.h (%d differ)", RT_MEASURED_BINS, bad);
    expectTrue(label, bad == 0);
    // and, as the header promises, a flat spectrum of 1 reads (1, 1, 1)
    double sum[3] = {0, 0, 0};
    for (int k = 0; k < RT_MEASURED_BINS; ++k) { sum[0] += wr[k]; sum[1] += wg[k]; sum[2] += wb[k]; }
    expectNear("measured quadrature: the red weights sum to 1", sum[0], 1.0, 2e-3);
    expectNear("measured quadrature: the green weights sum to 1", sum[1], 1.0, 2e-3);
    expectNear("measured quadrature: the blue weights sum to 1", sum[2], 1.0, 2e-3);
}
