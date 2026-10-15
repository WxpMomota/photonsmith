// metal_poc_procedural.metal
// Procedural textures: checker, 3D checker, Perlin noise / fbm / turbulence, pbrt marble.
// Split out of metal_poc_sampling.metal (pure code motion) for the code-size limit; concatenated in the order of metal_poc_shader_files.h.

// A PROCEDURAL texture (materialType 6) - analytic, computed directly
// from the hit's own UV, no image/sampler involved at all, unlike
// materialType 3's earthTexture lookup or step 18's equirectangularUV()
// (both still ultimately a texture2d::sample() call). `scale` tiles are
// per UV unit; alternating tiles pick `colorA`/`colorB` based on the
// parity of floor(u*scale)+floor(v*scale) - the textbook checkerboard
// construction, same one this project's own CPU checker_texture.h uses.
inline float3 checkerColor(float2 uv, float scale, float3 colorA, float3 colorB) {
    float2 tile = floor(uv * scale);
    float parity = fmod(tile.x + tile.y, 2.0);
    return (abs(parity) < 0.5) ? colorA : colorB;
}

// materialType 16's own albedo function - the REAL 3D world-space
// checkerboard this project's own CPU renderer's checker_texture (src/
// TheRestOfYourLife/texture.h) actually implements: parity of
// floor(p.x/scale)+floor(p.y/scale)+floor(p.z/scale), keyed purely on the
// hit's own world-space POSITION, not a UV coordinate at all - unlike
// checkerColor() above (materialType 6), which needs a triangle's own
// interpolated UV and so only ever fires on a triangle (see that
// function's own comment). Needing no UV is exactly why this one CAN run
// on a sphere hit (checkerColor() above cannot - metal_poc.metal's
// sphere-intersection path computes no UV at all, the documented reason
// category-G's own mesh gallery ground uses a flat quad instead of a
// checker sphere, section 117) - this closes that gap for scenes that
// only ever needed the REAL 3D book-checker in the first place, section
// 121, docs/history/METAL_GPU_FEASIBILITY.md.
inline float3 checker3DColor(float3 p, float scale, float3 colorA, float3 colorB) {
    float3 cell = floor(p / scale);
    float parity = fmod(abs(cell.x) + abs(cell.y) + abs(cell.z), 2.0);
    return (parity < 0.5) ? colorA : colorB;
}

// materialType 17's own noise field - a direct port of this project's
// own CPU/GPU-shared `src/shared/noise.h` (pbrt-v4's Noise()/
// Turbulence(), CPU_GPU-tagged - already NVCC/CUDA-portable, but MSL
// itself can't #include that header directly, so this is a genuine
// re-transcription of the SAME fixed permutation table and formulas,
// not a from-scratch reimplementation). Section 124, docs/
// METAL_GPU_FEASIBILITY.md. `kNoisePerm` is pbrt-v4's own fixed table
// (noise.cpp) - identical values, do not reorder.
constant int kNoisePerm[512] = {
    151,160,137, 91, 90, 15,131, 13,201, 95, 96, 53,194,233,  7,225,
    140, 36,103, 30, 69,142,  8, 99, 37,240, 21, 10, 23,190,  6,148,
    247,120,234, 75,  0, 26,197, 62, 94,252,219,203,117, 35, 11, 32,
     57,177, 33, 88,237,149, 56, 87,174, 20,125,136,171,168, 68,175,
     74,165, 71,134,139, 48, 27,166, 77,146,158,231, 83,111,229,122,
     60,211,133,230,220,105, 92, 41, 55, 46,245, 40,244,102,143, 54,
     65, 25, 63,161,  1,216, 80, 73,209, 76,132,187,208, 89, 18,169,
    200,196,135,130,116,188,159, 86,164,100,109,198,173,186,  3, 64,
     52,217,226,250,124,123,  5,202, 38,147,118,126,255, 82, 85,212,
    207,206, 59,227, 47, 16, 58, 17,182,189, 28, 42,223,183,170,213,
    119,248,152,  2, 44,154,163, 70,221,153,101,155,167, 43,172,  9,
    129, 22, 39,253, 19, 98,108,110, 79,113,224,232,178,185,112,104,
    218,246, 97,228,251, 34,242,193,238,210,144, 12,191,179,162,241,
     81, 51,145,235,249, 14,239,107, 49,192,214, 31,181,199,
    106,157,184, 84,204,176,115,121, 50, 45,127,  4,150,254,138,236,
    205, 93,222,114, 67, 29, 24, 72,243,141,128,195, 78, 66,215, 61,
    156,180,
    // second copy (identical to first 256 entries, starting at index 256)
    151,160,137, 91, 90, 15,131, 13,201, 95, 96, 53,194,233,
      7,225,140, 36,103, 30, 69,142,  8, 99, 37,240, 21, 10, 23,190,
      6,148,247,120,234, 75,  0, 26,197, 62, 94,252,219,203,117, 35,
     11, 32, 57,177, 33, 88,237,149, 56, 87,174, 20,125,136,171,168,
     68,175, 74,165, 71,134,139, 48, 27,166, 77,146,158,231, 83,111,
    229,122, 60,211,133,230,220,105, 92, 41, 55, 46,245, 40,244,102,
    143, 54, 65, 25, 63,161,  1,216, 80, 73,209, 76,132,187,208, 89,
     18,169,200,196,135,130,116,188,159, 86,164,100,109,198,173,186,
      3, 64, 52,217,226,250,124,123,  5,202, 38,147,118,126,255, 82,
     85,212,207,206, 59,227, 47, 16, 58, 17,182,189, 28, 42,223,183,
    170,213,119,248,152,  2, 44,154,163, 70,221,153,101,155,167, 43,
    172,  9,129, 22, 39,253, 19, 98,108,110, 79,113,224,232,178,185,
    112,104,218,246, 97,228,251, 34,242,193,238,210,144, 12,191,179,
    162,241, 81, 51,145,235,249, 14,239,107, 49,192,214, 31,181,199
};

// pbrt-v4's own Grad(): maps a lattice-point hash to one of 12 gradient
// directions - direct port of noise.h's own noise_detail::Grad<T>().
inline float noiseGrad(int x, int y, int z, float dx, float dy, float dz) {
    int h = kNoisePerm[kNoisePerm[kNoisePerm[x & 255] + (y & 255)] + (z & 255)];
    h &= 15;
    float u = (h < 8 || h == 12 || h == 13) ? dx : dy;
    float v = (h < 4 || h == 12 || h == 13) ? dy : dz;
    return ((h & 1) ? -u : u) + ((h & 2) ? -v : v);
}

// pbrt-v4's own NoiseWeight(): quintic C2 smoothstep (6t^5-15t^4+10t^3) -
// direct port, replacing Book-3's older cubic (only C1, visible seams).
inline float noiseWeight(float t) {
    float t3 = t * t * t, t4 = t3 * t, t5 = t4 * t;
    return 6.0 * t5 - 15.0 * t4 + 10.0 * t3;
}

// pbrt-v4's own Noise(x,y,z) - trilinear-interpolated gradient noise in
// [-1,1], direct port of noise.h's own perlin_noise<T>().
inline float perlinNoise3D(float3 p) {
    const float wrap = float(1 << 30);
    p = fmod(p, wrap);
    int3 i = int3(floor(p));
    float3 d = p - float3(i);
    int ix = i.x & 255, iy = i.y & 255, iz = i.z & 255;

    float w000 = noiseGrad(ix,   iy,   iz,   d.x,       d.y,       d.z);
    float w100 = noiseGrad(ix+1, iy,   iz,   d.x - 1.0, d.y,       d.z);
    float w010 = noiseGrad(ix,   iy+1, iz,   d.x,       d.y - 1.0, d.z);
    float w110 = noiseGrad(ix+1, iy+1, iz,   d.x - 1.0, d.y - 1.0, d.z);
    float w001 = noiseGrad(ix,   iy,   iz+1, d.x,       d.y,       d.z - 1.0);
    float w101 = noiseGrad(ix+1, iy,   iz+1, d.x - 1.0, d.y,       d.z - 1.0);
    float w011 = noiseGrad(ix,   iy+1, iz+1, d.x,       d.y - 1.0, d.z - 1.0);
    float w111 = noiseGrad(ix+1, iy+1, iz+1, d.x - 1.0, d.y - 1.0, d.z - 1.0);

    float wx = noiseWeight(d.x), wy = noiseWeight(d.y), wz = noiseWeight(d.z);
    float x00 = mix(w000, w100, wx);
    float x10 = mix(w010, w110, wx);
    float x01 = mix(w001, w101, wx);
    float x11 = mix(w011, w111, wx);
    float y0 = mix(x00, x10, wy);
    float y1 = mix(x01, x11, wy);
    return mix(y0, y1, wz);
}

// pbrt-v4's own Turbulence(), no-antialiasing overload (this project's
// own `turbulence_simple<T>()`, noise.h) - sum of |noise| across
// `maxOctaves`, each octave at 1.99x the previous frequency and
// `omega`x the previous amplitude. Used by materialType 17's own marble
// pattern, matching CPU's `noise_texture`/`perlin::turb()` exactly
// (depth 7, omega 0.5 - see that class's own comment).
// pbrt-v4 FBm without the per-pixel anti-aliasing octave cut: a port of
// src/shared/noise.h's fbm_simple() (plain sum of signed Perlin octaves).
inline float fbmSimple(float3 p, float omega, int maxOctaves) {
    float sum = 0.0, lambda = 1.0, o = 1.0;
    for (int i = 0; i < maxOctaves; ++i) {
        sum += o * perlinNoise3D(p * lambda);
        lambda *= 1.99;
        o *= omega;
    }
    return sum;
}

// pbrt-v4 MarbleTexture, ported from CPU's marble_texture::value() (texture.h): `p` is the
// already-scaled point. Cubic-Bezier segment of a fixed 9-knot marble spline, x1.5 and
// clamped to 1.
constant float3 kMarbleKnots[9] = {
    float3(.58, .58, .60), float3(.58, .58, .60), float3(.58, .58, .60),
    float3(.50, .50, .50), float3(.60, .59, .58), float3(.58, .58, .60),
    float3(.58, .58, .60), float3(.20, .20, .33), float3(.58, .58, .60)
};
inline float3 pbrtMarbleColor(float3 p, float omega, int octaves, float variation) {
    float fbmVal = fbmSimple(p, omega, octaves);
    float marble = p.y + variation * fbmVal;
    float t = 0.5 + 0.5 * sin(marble);
    const int nSeg = 6;
    int first = min(int(t * float(nSeg)), nSeg - 1);
    float lt = t * float(nSeg) - float(first);
    float s = 1.0 - lt;
    float3 rgb = s*s*s * kMarbleKnots[first] + 3.0*s*s*lt * kMarbleKnots[first+1]
               + 3.0*s*lt*lt * kMarbleKnots[first+2] + lt*lt*lt * kMarbleKnots[first+3];
    return min(rgb * 1.5, float3(1.0));
}

inline float turbulenceSimple(float3 p, float omega, int maxOctaves) {
    float sum = 0.0, lambda = 1.0, o = 1.0;
    for (int i = 0; i < maxOctaves; ++i) {
        sum += o * abs(perlinNoise3D(p * lambda));
        lambda *= 1.99;
        o *= omega;
    }
    return sum;
}
