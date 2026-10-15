// metal_poc_bump.metal
// Image bump and normal maps: the height lookup (with its mip chain), the pixel footprint of a hit, the triangle's tangent frame and shading-normal derivatives, and pbrt's
// BumpMap / NormalMap perturbation. Split out of metal_poc_sampling.metal (pure code motion, the same functions in the same order) when that file passed the code-size limit.
// Concatenated after metal_poc_sampling.metal and before the kernel files (metal_poc_shader_files.h).

// ---------------------------------------------------------------------------
// Image bump mapping (pbrt "texture displacement" with a grayscale height map) - port of gpu/optix/gpu_bump_map.h, which
// reproduces the CPU's bump_map_material: displacement = scale * height(u, v) from a bilinear, Repeat lookup (image row 0 at the
// top, texel centres at (i + 0.5) / size), finite differences at +step along u and v, then pbrt's BumpMap perturbation.
// ---------------------------------------------------------------------------
inline float bumpHeightAt(device const float* data, int offset, int w, int h, int x, int y) {
    x = ((x % w) + w) % w;
    y = ((y % h) + h) % h;
    return data[offset + y * w + x];
}

inline float bumpHeightSample(device const float* data, int offset, int w, int h, float u, float v) {
    const float uw = clamp(u, -1024.0, 1024.0);
    const float vw = clamp(1.0 - v, -1024.0, 1024.0);
    const float x = uw * float(w) - 0.5;
    const float y = vw * float(h) - 0.5;
    const int x0 = int(floor(x)), y0 = int(floor(y));
    const float fx = x - float(x0), fy = y - float(y0);
    return (1.0 - fy) * ((1.0 - fx) * bumpHeightAt(data, offset, w, h, x0, y0) + fx * bumpHeightAt(data, offset, w, h, x0 + 1, y0))
         +        fy  * ((1.0 - fx) * bumpHeightAt(data, offset, w, h, x0, y0 + 1) + fx * bumpHeightAt(data, offset, w, h, x0 + 1, y0 + 1));
}

// The height at (u, v) from the mip level that fits `width` = pbrt's 2 * max(|dsdx|, |dtdx|, |dsdy|, |dtdy|) in the texture's own (s, t) coordinates (MIPMap::Filter with the
// default Bilinear filter: level = levels - 1 + log2(width); the coarsest texel when that reaches the last level, otherwise the bilinear lookup in floor(level)). The levels follow
// level 0 in `data`, each a 2x2 box average of the one above (size (n + 1) / 2, the far edge repeated: buildBumpMipChain in metal_poc_pbrt_loader.mm / gpu/optix). A width of 0
// is the full-resolution lookup, which is what a hit with no footprint reads.
inline float bumpHeightFiltered(device const float* data, int offset, int w, int h, float u, float v, float width) {
    if (!(width > 0.0)) return bumpHeightSample(data, offset, w, h, u, v);
    int levels = 1;
    int cw = w, ch = h;
    while (cw > 1 || ch > 1) { cw = max(1, (cw + 1) / 2); ch = max(1, (ch + 1) / 2); ++levels; }
    const float level = float(levels - 1) + log2(max(width, 1e-8f));
    const int want = level >= float(levels - 1) ? levels - 1 : (level > 0.0f ? int(floor(level)) : 0);
    int lw = w, lh = h, off = offset;
    for (int i = 0; i < want; ++i) { off += lw * lh; lw = max(1, (lw + 1) / 2); lh = max(1, (lh + 1) / 2); }
    if (level >= float(levels - 1)) return data[off];   // the 1x1 level
    return bumpHeightSample(data, off, lw, lh, u, v);
}

// The triangle's UNnormalized dpdu and dpdv (the 2x2 UV-edge Jacobian solve), as the CPU's rec.dpdu / rec.dpdv. A degenerate UV mapping
// falls back to the first edge for dpdu and cross(n, dpdu) for dpdv.
inline void triangleDpduDpdv(uint primId, device const packed_float3* verts, device const packed_float2* uvs, float3 n,
                             thread float3& dpdu, thread float3& dpdv) {
    const float3 p0 = float3(verts[primId * 3 + 0]), p1 = float3(verts[primId * 3 + 1]), p2 = float3(verts[primId * 3 + 2]);
    const float2 uv0 = uvs[primId * 3 + 0], uv1 = uvs[primId * 3 + 1], uv2 = uvs[primId * 3 + 2];
    const float3 e1 = p1 - p0, e2 = p2 - p0;
    const float2 d1 = uv1 - uv0, d2 = uv2 - uv0;
    const float det = d1.x * d2.y - d1.y * d2.x;
    // (A triangle without uvs of its own is stored with pbrt's default corner uvs (0,0), (1,0), (1,1), whose solve is dpdu = p1 - p0, dpdv = p2 - p1, as on the CPU.)
    if (abs(det) > 1e-12) {
        const float inv = 1.0 / det;
        dpdu = inv * (d2.y * e1 - d1.y * e2);
        dpdv = inv * (-d2.x * e1 + d1.x * e2);
        return;
    }
    const float len2 = dot(e1, e1);
    dpdu = len2 > 1e-14 ? e1 * rsqrt(len2) : float3(1.0, 0.0, 0.0);
    dpdv = cross(n, dpdu);
}

// (dudx, dvdx, dudy, dvdy) of the pixel's footprint at p for a ray (origin O, unit direction d) of the perspective camera that hit the surface at p: the two
// one-pixel-offset rays are intersected with the tangent plane (n, p) and the offsets projected onto (dpdu, dpdv) by least squares -
// the CPU's SurfaceInteraction::compute_differentials(), as in gpu_bump_footprint_step(). 0 where the CPU would have no footprint
// (all zero; callers then use 0.001). The finite-difference step is 0.5 * (|dudx| + |dudy|); pbrt gives every hit this footprint (CameraBase::Approximate_dp_dxy), as if a camera ray from its origin saw the point. The pixel footprint matters: on a distant or tiled surface it spans many texels, so the CPU's primary bump
// is a smoothed one and a fixed small step would read the texture at full detail and render it too bright.
inline float4 bumpFootprint(constant Uniforms& u, float3 O, float3 d, float3 p, float3 n, float3 dpdu, float3 dpdv) {
    const float3 fwd = float3(u.cameraForward);
    const float cf = dot(fwd, d);
    if (cf <= 1e-6) return float4(0.0);
    const float3 D = d / cf;   // the unnormalized direction that lands on the viewport plane (distance 1 along forward)
    // pbrt scales camera ray differentials by max(1/8, 1/sqrt(samples per pixel)) (RayIntegrator::EvaluatePixelSample), as the CPU and OptiX now do: with many samples the
    // step is a fraction of a pixel, not the whole of it. (The sample count of the whole render, which is what uniforms.samplesPerPixel is.)
    const float diffScale = max(0.125, 1.0 / sqrt(float(max(u.samplesPerPixel, 1u))));
    // cameraRight / cameraUp are unit vectors: the viewport at distance 1 along forward is 2 * tanHalfFov * (aspect, 1) wide, so one pixel is that over the pixel counts.
    const float3 Dx = D + float3(u.cameraRight) * (diffScale * 2.0 * u.tanHalfFov * u.aspect / float(u.width));
    const float3 Dy = D + float3(u.cameraUp) * (diffScale * 2.0 * u.tanHalfFov / float(u.height));
    const float dnx = dot(n, Dx), dny = dot(n, Dy);
    if (dnx == 0.0 || dny == 0.0) return float4(0.0);
    const float3 pop = p - O;
    const float3 dpdx = Dx * (dot(n, pop) / dnx) - pop;
    const float3 dpdy = Dy * (dot(n, pop) / dny) - pop;
    const float a00 = dot(dpdu, dpdu), a01 = dot(dpdu, dpdv), a11 = dot(dpdv, dpdv);
    const float det = a00 * a11 - a01 * a01;
    if (!(abs(det) > 0.0)) return float4(0.0);
    const float inv = 1.0 / det;
    const float bx0 = dot(dpdu, dpdx), bx1 = dot(dpdv, dpdx), by0 = dot(dpdu, dpdy), by1 = dot(dpdv, dpdy);
    const float dudx = (a11 * bx0 - a01 * bx1) * inv;
    const float dudy = (a11 * by0 - a01 * by1) * inv;
    const float dvdx = (a00 * bx1 - a01 * bx0) * inv;
    const float dvdy = (a00 * by1 - a01 * by0) * inv;
    const float step = 0.5 * (abs(dudx) + abs(dudy));
    return (step == step && step < 1e8) ? float4(dudx, dvdx, dudy, dvdy) : float4(0.0);
}

// dndu and dndv of a triangle's interpolated shading normal from its three vertex normals and uvs (src/shared/normal_map.h triangle_dndu_dndv, pbrt-v4
// Triangle::InteractionFromIntersection): what a bump map's `displacement * dndu` term needs. A degenerate uv mapping gives zero.
inline void triangleDnduDndv(uint primId, device const packed_float3* normals, device const packed_float2* uvs, thread float3& dndu, thread float3& dndv) {
    const float3 n0 = float3(normals[primId * 3 + 0]), n1 = float3(normals[primId * 3 + 1]), n2 = float3(normals[primId * 3 + 2]);
    const float2 uv0 = uvs[primId * 3 + 0], uv1 = uvs[primId * 3 + 1], uv2 = uvs[primId * 3 + 2];
    const float du02 = uv0.x - uv2.x, dv02 = uv0.y - uv2.y, du12 = uv1.x - uv2.x, dv12 = uv1.y - uv2.y;
    const float det = du02 * dv12 - dv02 * du12;
    if (abs(det) < 1e-9) { dndu = float3(0.0); dndv = float3(0.0); return; }
    const float inv = 1.0 / det;
    const float3 dn1 = n0 - n2, dn2 = n1 - n2;
    dndu = (dv12 * dn1 - dv02 * dn2) * inv;
    dndv = (du02 * dn2 - du12 * dn1) * inv;
}

// Does the triangle carry vertex normals that differ (a smooth mesh)? The shader cannot tell "has normals" from "has flat normals", so equal corner normals count as flat.
inline bool triangleIsSmooth(uint primId, device const packed_float3* normals) {
    const float3 n0 = float3(normals[primId * 3 + 0]), n1 = float3(normals[primId * 3 + 1]), n2 = float3(normals[primId * 3 + 2]);
    return any(abs(n0 - n1) > 1e-5) || any(abs(n0 - n2) > 1e-5);
}

// The perturbed shading normal at texture coordinate (u, v): pbrt-v4's BumpMap() as the CPU and OptiX do (src/shared/normal_map.h apply_bump_map_pbrt):
//   dpdu' = shading.dpdu + (disp_u - disp) / du * n + disp * dndu,  dpdv' likewise,  ns = normalize(cross(dpdu', dpdv')) on the side of n
// where n is the shading normal AS AUTHORED (not turned toward the ray; the caller turns the result toward the viewer), and for a smooth triangle the tangents are
// pbrt's ts = cross(n, dpdu), ss = cross(ts, n). (u, v) are the surface's own coordinates; uvMap = (su, sv, du, dv) is the picture's imagemap mapping, applied to each of the
// three height lookups. The finite-difference step stays in surface units, so a tiled picture's slope is its slope per surface unit (as on the CPU).
inline float3 imageBumpNormal(device const float* data, int offset, int w, int h, float scale, float u, float v, float4 uvMap,
                              float3 n, bool smooth, float3 dpdu, float3 dpdv, float3 dndu, float3 dndv, float4 footprint) {
    const float footprintStep = 0.5 * (abs(footprint.x) + abs(footprint.z));
    const float step = footprintStep > 1e-8 ? footprintStep : 0.001;   // 0.001: the CPU's no-footprint fallback
    // The height is read through the pixel's footprint in the displacement texture's own coordinates, like the CPU (pbrt hands the shifted context's dudx.. on).
    float width = 0.0;
    if (footprintStep > 1e-8)
        width = 2.0 * max(max(abs(uvMap.x * footprint.x), abs(uvMap.y * footprint.y)), max(abs(uvMap.x * footprint.z), abs(uvMap.y * footprint.w)));
    const float disp   = scale * bumpHeightFiltered(data, offset, w, h, uvMap.x * u          + uvMap.z, uvMap.y * v          + uvMap.w, width);
    const float dispU  = scale * bumpHeightFiltered(data, offset, w, h, uvMap.x * (u + step) + uvMap.z, uvMap.y * v          + uvMap.w, width);
    const float dispV  = scale * bumpHeightFiltered(data, offset, w, h, uvMap.x * u          + uvMap.z, uvMap.y * (v + step) + uvMap.w, width);
    if (dot(dpdv, dpdv) < 1e-12) dpdv = cross(n, dpdu);
    if (dot(dpdv, dpdv) < 1e-12) dpdv = float3(0.0, 1.0, 0.0);
    float3 su = dpdu, sv = dpdv;
    if (smooth) {
        const float3 ts = cross(n, dpdu);
        if (dot(ts, ts) > 0.0) {
            su = cross(ts, n);
            sv = ts;
        } else {   // dpdu along the normal: any frame round it (pbrt's CoordinateSystem)
            const float sgn = n.z >= 0.0 ? 1.0 : -1.0;
            const float a = -1.0 / (sgn + n.z), b = n.x * n.y * a;
            su = float3(1.0 + sgn * n.x * n.x * a, sgn * b, -sgn * n.x);
            sv = float3(b, sgn + n.y * n.y * a, -n.y);
        }
    }
    const float3 bu = su + ((dispU - disp) / step) * n + disp * dndu;
    const float3 bv = sv + ((dispV - disp) / step) * n + disp * dndv;
    float3 nn = cross(bu, bv);
    const float len2 = dot(nn, nn);
    if (!(len2 > 1e-16) || isnan(len2) || isinf(len2)) return n;
    nn *= rsqrt(len2);
    return dot(nn, n) < 0.0 ? -nn : nn;
}

// Image normal map ("texture displacement" with an RGB image) - port of the CPU's normal_map_material / pbrt-v4's NormalMap(): the texel (bilinear,
// Repeat, read linear; 3 interleaved floats per texel, row 0 at the top, same addressing as the height map above) decodes to a tangent-space
// normal 2*rgb - 1, which is rotated into the shading frame built from the (normalized) dpdu and the facing normal.
inline float3 bumpRgbAt(device const float* data, int offset, int w, int h, int x, int y) {
    x = ((x % w) + w) % w;
    y = ((y % h) + h) % h;
    const int i = offset + (y * w + x) * 3;
    return float3(data[i], data[i + 1], data[i + 2]);
}

inline float3 imageNormalMapNormal(device const float* data, int offset, int w, int h, float u, float v, float3 n, float3 dpdu) {
    const float uw = clamp(u, -1024.0, 1024.0);
    const float vw = clamp(1.0 - v, -1024.0, 1024.0);
    const float x = uw * float(w) - 0.5;
    const float y = vw * float(h) - 0.5;
    const int x0 = int(floor(x)), y0 = int(floor(y));
    const float fx = x - float(x0), fy = y - float(y0);
    const float3 rgb = (1.0 - fy) * ((1.0 - fx) * bumpRgbAt(data, offset, w, h, x0, y0) + fx * bumpRgbAt(data, offset, w, h, x0 + 1, y0))
                     +        fy  * ((1.0 - fx) * bumpRgbAt(data, offset, w, h, x0, y0 + 1) + fx * bumpRgbAt(data, offset, w, h, x0 + 1, y0 + 1));
    float3 ns = 2.0 * rgb - 1.0;
    const float len = length(ns);
    ns = len > 1e-8 ? ns / len : float3(0.0, 0.0, 1.0);
    float3 t = dpdu;
    const float tl = length(t);
    t = tl > 1e-8 ? t / tl : float3(1.0, 0.0, 0.0);
    float3 g = t - dot(t, n) * n;   // Gram-Schmidt: the tangent perpendicular to the normal
    const float gl = length(g);
    if (!(gl > 1e-8)) return n;
    g /= gl;
    float3 b = cross(n, g);
    b /= max(length(b), 1e-8);
    float3 out = ns.x * g + ns.y * b + ns.z * n;
    const float ol = length(out);
    if (!(ol > 1e-8)) return n;
    out /= ol;
    return dot(out, n) < 0.0 ? -out : out;
}
