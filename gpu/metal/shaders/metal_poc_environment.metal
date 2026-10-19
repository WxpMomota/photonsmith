// metal_poc_environment.metal
// Environment-map and portal (windowed) infinite lights: importance-sampling tables, pdfs, the pbrt-v4 portal, and the pbrt environment lookups.
// Split out of metal_poc_sampling.metal (pure code motion) for the code-size limit; concatenated in the order of metal_poc_shader_files.h.

// --- Environment-map importance sampling (phase 2) --------------------
// Device-side counterpart to gpu/metal/host/metal_poc_host_math.h's own
// EnvDistribution2D/findCdfInterval/sampleEnvDistribution2D/
// pdfEnvDistribution2D (section 69, phase 1) - same CDF-slope-as-pdf
// convention, same piecewise-constant-bucket binary search, just
// reading `device const float*` buffers instead of a `std::vector`, and
// folding the equirectangular direction<->UV conversion (equirectangularUV()
// above, inverted here) and its own sin(theta) solid-angle Jacobian in
// directly, since a device-side caller wants a world DIRECTION and a
// solid-angle pdf, not an image-space (u,v) and an image-space density -
// that conversion has nowhere else to live.

inline int findCdfIntervalDevice(device const float* cdf, int n, float u) {
    int lo = 0, hi = n;
    while (lo + 1 < hi) {
        int mid = (lo + hi) / 2;
        if (cdf[mid] <= u) lo = mid; else hi = mid;
    }
    return (lo < n - 1) ? lo : (n - 1);
}

// Draws a world direction from the environment map's own importance
// distribution and returns its solid-angle pdf - mirrors
// sampleEnvDistribution2D()'s own image-space sampling exactly, then
// inverts equirectangularUV() (phi = 2*pi*(u-0.5), lambda = pi*(v-0.5),
// dir = (cos(lambda)*cos(phi), sin(lambda), cos(lambda)*sin(phi))) and
// applies the equirectangular Jacobian (dOmega = 2*pi^2*cos(lambda)
// du*dv, and cos(lambda) == sin(pi*v) - see docs section 71's own
// derivation) to convert the image-space pdf into the solid-angle one
// every other light-sampling strategy in this shader already returns.
inline float3 sampleEnvironmentDirection(device const float* marginalCDF, device const float* conditionalCDF,
                                          int width, int height, float u1, float u2,
                                          thread float& pdfSolidAngle) {
    int row = findCdfIntervalDevice(marginalCDF, height, u1);
    float rowLo = marginalCDF[row], rowHi = marginalCDF[row + 1];
    float rowSpan = max(rowHi - rowLo, 1e-9);
    float dv = (u1 - rowLo) / rowSpan;
    float v = (float(row) + dv) / float(height);
    float rowPdf = rowSpan * float(height);

    device const float* condRow = conditionalCDF + row * (width + 1);
    int col = findCdfIntervalDevice(condRow, width, u2);
    float colLo = condRow[col], colHi = condRow[col + 1];
    float colSpan = max(colHi - colLo, 1e-9);
    float du = (u2 - colLo) / colSpan;
    float u = (float(col) + du) / float(width);
    float colPdf = colSpan * float(width);

    float pdfImage = rowPdf * colPdf;
    float sinTheta = max(sin(M_PI_F * v), 1e-6);
    pdfSolidAngle = pdfImage / (2.0 * M_PI_F * M_PI_F * sinTheta);

    // Inverse of envMapUV(): theta = pi*v from +y, phi = 2*pi*u, dir = (sin(theta)cos(phi), cos(theta), -sin(theta)sin(phi)).
    float phi = 2.0 * M_PI_F * u;
    float theta = M_PI_F * v;
    float sinT = sin(theta);
    return float3(sinT * cos(phi), cos(theta), -sinT * sin(phi));
}

// Evaluates the SAME solid-angle pdf at an arbitrary world direction -
// what a BSDF-sampled ray that escaped toward some direction needs for
// its own MIS weight against this strategy (the miss-path's own
// contribution, see primaryRayKernel's own comment on this).
inline float pdfEnvironmentDirection(device const float* marginalCDF, device const float* conditionalCDF,
                                      int width, int height, float3 dir) {
    float2 uv = envMapUV(dir);
    int row = clamp(int(uv.y * float(height)), 0, height - 1);
    int col = clamp(int(uv.x * float(width)), 0, width - 1);
    float rowPdf = (marginalCDF[row + 1] - marginalCDF[row]) * float(height);
    device const float* condRow = conditionalCDF + row * (width + 1);
    float colPdf = (condRow[col + 1] - condRow[col]) * float(width);
    float pdfImage = rowPdf * colPdf;
    float sinTheta = max(sin(M_PI_F * uv.y), 1e-6);
    return pdfImage / (2.0 * M_PI_F * M_PI_F * sinTheta);
}

// --- pbrt-v4 portal (windowed) infinite light --------------------------------------------------------------------------------------
// LightSource "infinite" "point3 portal[4]" restricts an image environment light to the directions seen through a quad. A float
// port of gpu/optix/gpu_portal_light_shared.h (itself a port of src/shared/portal_image_infinite_light.h), reading data the host
// builds from a real PortalImageInfiniteLightData: the rectified image, the distribution's cell values and its summed-area table.
// Uniforms::pbrtHasPortalLight selects it; the data rides in the existing pbrtEnv buffers so no new buffer slot is needed:
//   pbrtEnvMarginalCDF    = [ distFunc (w*h floats) | satSum (w*h floats) ]
//   pbrtEnvConditionalCDF = rectified RGB image (w*h*3 floats, scale NOT applied - Uniforms::pbrtPortalScale is)
// Unlike every other light here, the window depends on the ORIGIN of the ray (the quad subtends a different set of directions from
// each point), so sampling, pdf and radiance all take the shading point / ray origin.
// The summed-area table is float here (the CPU and OptiX use double). A window integral is a difference of nearby prefix sums, so
// its relative error is ~1e-7 * (total / window): fine for windows that are a sizeable part of the image, coarser for a tiny one.

inline float3 portalToLocal(constant Uniforms& u, float3 v) {
    return float3(dot(v, float3(u.portalFrameX)), dot(v, float3(u.portalFrameY)), dot(v, float3(u.portalFrameZ)));
}

inline float3 portalFromLocal(constant Uniforms& u, float3 v) {
    return float3(u.portalFrameX) * v.x + float3(u.portalFrameY) * v.y + float3(u.portalFrameZ) * v.z;
}

// Render-space direction -> image uv in [0,1]^2 and the Jacobian d(uv)/dw. False when the direction is behind the portal.
inline bool portalImageFromRender(constant Uniforms& u, float3 wRender, thread float& uOut, thread float& vOut, thread float& duvDw) {
    const float3 w = portalToLocal(u, wRender);
    if (w.z <= 0.0) return false;
    duvDw = M_PI_F * M_PI_F * (1.0 - w.x * w.x) * (1.0 - w.y * w.y) / w.z;
    const float alpha = atan2(w.x, w.z);
    const float beta = atan2(w.y, w.z);
    uOut = clamp((alpha + 0.5 * M_PI_F) / M_PI_F, 0.0, 1.0);
    vOut = clamp((beta + 0.5 * M_PI_F) / M_PI_F, 0.0, 1.0);
    return true;
}

inline float3 portalRenderFromImage(constant Uniforms& u, float uu, float vv, thread float& duvDw) {
    const float alpha = -0.5 * M_PI_F + uu * M_PI_F;
    const float beta = -0.5 * M_PI_F + vv * M_PI_F;
    const float3 w = normalize(float3(tan(alpha), tan(beta), 1.0));
    duvDw = M_PI_F * M_PI_F * (1.0 - w.x * w.x) * (1.0 - w.y * w.y) / w.z;
    return portalFromLocal(u, w);
}

// [uMin,uMax] x [vMin,vMax] of the portal quad as seen from p (its diagonal corners p0 and p2).
inline bool portalImageBounds(constant Uniforms& u, float3 p, thread float& uMin, thread float& vMin, thread float& uMax, thread float& vMax) {
    const float3 d0 = normalize(float3(u.portalP0) - p);
    const float3 d2 = normalize(float3(u.portalP2) - p);
    float u0, v0, u2, v2, jac;
    if (!portalImageFromRender(u, d0, u0, v0, jac)) return false;
    if (!portalImageFromRender(u, d2, u2, v2, jac)) return false;
    uMin = min(u0, u2); uMax = max(u0, u2);
    vMin = min(v0, v2); vMax = max(v0, v2);
    return true;
}

inline float portalSatInt(device const float* sat, int w, int h, int x, int y) {
    if (x == 0 || y == 0) return 0.0;
    x = min(x - 1, w - 1);
    y = min(y - 1, h - 1);
    return sat[y * w + x];
}

// Bilinearly interpolated prefix sum (SummedAreaTable::Lookup()).
inline float portalSatLookup(device const float* sat, int w, int h, float x, float y) {
    x *= float(w);
    y *= float(h);
    const int x0 = int(x), y0 = int(y);
    const float v00 = portalSatInt(sat, w, h, x0, y0), v10 = portalSatInt(sat, w, h, x0 + 1, y0);
    const float v01 = portalSatInt(sat, w, h, x0, y0 + 1), v11 = portalSatInt(sat, w, h, x0 + 1, y0 + 1);
    const float dx = x - float(x0), dy = y - float(y0);
    return (1.0 - dx) * (1.0 - dy) * v00 + (1.0 - dx) * dy * v01 + dx * (1.0 - dy) * v10 + dx * dy * v11;
}

inline float portalSatIntegral(device const float* sat, int w, int h, float uMin, float vMin, float uMax, float vMax) {
    const float s = (portalSatLookup(sat, w, h, uMax, vMax) - portalSatLookup(sat, w, h, uMin, vMax))
                  + (portalSatLookup(sat, w, h, uMin, vMin) - portalSatLookup(sat, w, h, uMax, vMin));
    return max(s / float(w * h), 0.0f);
}

inline void portalUvToCell(int w, int h, float uu, float vv, thread int& ix, thread int& iy) {
    ix = min(int(uu * float(w)), w - 1);
    iy = min(int(vv * float(h)), h - 1);
}

// Radiance reaching `origin` along `dir`: zero unless dir lies in the window the portal subtends from origin (eval_Le_rgb()).
inline float3 portalLe(constant Uniforms& u, device const float* rectified, float3 origin, float3 dir) {
    float uu, vv, jac;
    if (!portalImageFromRender(u, normalize(dir), uu, vv, jac)) return float3(0.0);
    float bMinU, bMinV, bMaxU, bMaxV;
    if (!portalImageBounds(u, origin, bMinU, bMinV, bMaxU, bMaxV)) return float3(0.0);
    if (uu < bMinU || uu > bMaxU || vv < bMinV || vv > bMaxV) return float3(0.0);
    int ix, iy;
    portalUvToCell(int(u.pbrtPortalWidth), int(u.pbrtPortalHeight), uu, vv, ix, iy);
    device const float* px = rectified + (iy * int(u.pbrtPortalWidth) + ix) * 3;
    return u.pbrtPortalScale * float3(px[0], px[1], px[2]);
}

// Solid-angle pdf of sampling `dir` from p (pdf_li()).
inline float portalPdfLi(constant Uniforms& u, device const float* distAndSat, float3 p, float3 dir) {
    const int w = int(u.pbrtPortalWidth), h = int(u.pbrtPortalHeight);
    float uu, vv, duvDw;
    if (!portalImageFromRender(u, normalize(dir), uu, vv, duvDw)) return 0.0;
    if (duvDw == 0.0) return 0.0;
    float bMinU, bMinV, bMaxU, bMaxV;
    if (!portalImageBounds(u, p, bMinU, bMinV, bMaxU, bMaxV)) return 0.0;
    const float funcInt = portalSatIntegral(distAndSat + w * h, w, h, bMinU, bMinV, bMaxU, bMaxV);
    if (funcInt == 0.0) return 0.0;
    int ix, iy;
    portalUvToCell(w, h, uu, vv, ix, iy);
    return (distAndSat[iy * w + ix] / funcInt) / duvDw;
}

// Bisection against the CDF given by the window integral (WindowedPiecewiseConstant2D::SampleBisection()). mode 0 is the marginal in x over the
// whole window height, normalised by `norm`; mode 1 the conditional in y over the column strip [colMin, colMax].
inline float portalBisect(device const float* sat, int w, int h, int mode, float bMinU, float bMinV, float bMaxU, float bMaxV,
                          float colMin, float colMax, float norm, float uRand, float lo, float hi, int n) {
    for (int guard = 0; guard < 64 && ceil(float(n) * hi) - floor(float(n) * lo) > 1.0; ++guard) {
        const float mid = 0.5 * (lo + hi);
        const float P = (mode == 0) ? portalSatIntegral(sat, w, h, bMinU, bMinV, mid, bMaxV) / norm
                                    : portalSatIntegral(sat, w, h, colMin, bMinV, colMax, mid) / norm;
        if (P > uRand) hi = mid; else lo = mid;
    }
    const float Plo = (mode == 0) ? portalSatIntegral(sat, w, h, bMinU, bMinV, lo, bMaxV) / norm
                                  : portalSatIntegral(sat, w, h, colMin, bMinV, colMax, lo) / norm;
    const float Phi = (mode == 0) ? portalSatIntegral(sat, w, h, bMinU, bMinV, hi, bMaxV) / norm
                                  : portalSatIntegral(sat, w, h, colMin, bMinV, colMax, hi) / norm;
    const float t = (Phi - Plo > 0.0) ? (uRand - Plo) / (Phi - Plo) : 0.5;
    return min(hi, max(lo, lo + t * (hi - lo)));
}

// Importance-samples a direction toward the portal from p (sample_li()). False when the window is empty from p.
inline bool portalSampleLi(constant Uniforms& u, device const float* distAndSat, float3 p, float ru, float rv,
                           thread float3& dirOut, thread float& pdfOut) {
    const int w = int(u.pbrtPortalWidth), h = int(u.pbrtPortalHeight);
    device const float* sat = distAndSat + w * h;
    float bMinU, bMinV, bMaxU, bMaxV;
    if (!portalImageBounds(u, p, bMinU, bMinV, bMaxU, bMaxV)) return false;
    const float bInt = portalSatIntegral(sat, w, h, bMinU, bMinV, bMaxU, bMaxV);
    if (bInt == 0.0) return false;
    const float sx = portalBisect(sat, w, h, 0, bMinU, bMinV, bMaxU, bMaxV, 0.0, 0.0, bInt, ru, bMinU, bMaxU, w);
    float colMin = floor(sx * float(w)) / float(w);
    float colMax = ceil(sx * float(w)) / float(w);
    if (colMin == colMax) colMax += 1.0 / float(w);
    const float condInt = portalSatIntegral(sat, w, h, colMin, bMinV, colMax, bMaxV);
    if (condInt == 0.0) return false;
    const float sy = portalBisect(sat, w, h, 1, bMinU, bMinV, bMaxU, bMaxV, colMin, colMax, condInt, rv, bMinV, bMaxV, h);
    int ix, iy;
    portalUvToCell(w, h, sx, sy, ix, iy);
    const float mapPdf = distAndSat[iy * w + ix] / bInt;
    float duvDw;
    dirOut = portalRenderFromImage(u, sx, sy, duvDw);
    if (duvDw == 0.0) return false;
    pdfOut = mapPdf / duvDw;
    return true;
}

// The three entry points every environment-NEE site and the miss path use for a pbrt scene's own image infinite light. They dispatch
// on Uniforms::pbrtHasPortalLight, so a site only changes which function it calls; the plain-image behaviour is exactly as before.
inline float3 pbrtEnvSampleDirection(constant Uniforms& u, device const float* marginalOrDist, device const float* conditionalOrRect,
                                      uint width, uint height, float3 hitPoint, float u1, float u2, thread float& pdfSolidAngle) {
    if (u.pbrtHasPortalLight != 0u) {
        float3 dir;
        if (!portalSampleLi(u, marginalOrDist, hitPoint, u1, u2, dir, pdfSolidAngle)) { pdfSolidAngle = 0.0; return float3(0.0, 1.0, 0.0); }
        return dir;
    }
    return sampleEnvironmentDirection(marginalOrDist, conditionalOrRect, int(width), int(height), u1, u2, pdfSolidAngle);
}

inline float3 pbrtEnvLeAt(constant Uniforms& u, device const float* conditionalOrRect, texture2d<float, access::sample> envTexture,
                           sampler s, float3 origin, float3 dir) {
    if (u.pbrtHasPortalLight != 0u) return portalLe(u, conditionalOrRect, origin, dir);
    return envTexture.sample(s, envMapUV(dir)).rgb;
}

inline float pbrtEnvPdfAt(constant Uniforms& u, device const float* marginalOrDist, device const float* conditionalOrRect,
                           uint width, uint height, float3 origin, float3 dir) {
    if (u.pbrtHasPortalLight != 0u) return portalPdfLi(u, marginalOrDist, origin, dir);
    return pdfEnvironmentDirection(marginalOrDist, conditionalOrRect, int(width), int(height), dir);
}
