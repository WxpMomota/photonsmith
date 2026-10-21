// Result of one importance-sampled pixel-filter draw - mirrors
// src/shared/filter_sampler.h's own FilterSample<T> exactly.
struct MetalFilterSample {
    float px, py;  // sub-pixel position in [-radius, +radius]
    float weight;  // f(p)/pdf(p) - see this function's own comment
};

// Hand-ported MSL device-side mirror of FilterSampler<float,32>::sample()
// (src/shared/filter_sampler.h) - a GATHER, not a scatter: THIS pixel's
// own thread draws (px,py) from the distribution of ITS OWN
// reconstruction filter, which for a filter wider than one pixel
// (uniforms.filterRadius > 0.5, e.g. pbrt-v4's own default Gaussian 1.5)
// legitimately reaches into the geometric area of a NEIGHBOURING pixel -
// the camera ray this sample drives (primaryRayKernel's own caller)
// simply points in a slightly different screen direction for that one
// sample, same as CPU's camera.h get_ray() offsetting pixel_sample by
// `offset.x()/offset.y()` beyond the unit pixel square. No cross-thread
// write/atomic is needed because each pixel independently estimates its
// OWN reconstruction integral this way - exactly pbrt-v4's own per-pixel
// filter-importance-sampling model, not a splat-to-neighbours one.
//
// Can't call FilterSampler<float,32>::sample() directly on-device the
// way OptiX/CUDA does (that backend's host and device code share one
// real C++ compilation, so the SAME class instance - uploaded by
// OptiXRenderer::render() - runs verbatim on the GPU; Metal Shading
// Language is a separate compiled language with no such link) - this is
// a faithful port of that function's own CDF-inversion + intra-cell
// lerp algorithm instead, reading the SAME table (built host-side by
// compileShaderAndDispatch(), copied into Uniforms::filterConditionalCDF/
// filterMarginalCDF - see that field's own comment for why it rides in
// the Uniforms buffer rather than a separate one).
//
// weight = f(p)/pdf(p), which FilterSampler::sample()'s own comment
// proves is the CONSTANT integral() for every reachable (nonzero-
// probability) cell, not a per-cell value - uniforms.filterIntegral
// already IS exactly that constant, so there's no need to also upload
// the f_/pdf_ tables themselves (only the two CDFs, needed for the
// inversion/lerp below) just to recompute a ratio that always reduces
// to the one scalar already on hand.
inline MetalFilterSample sampleFilterPosition(constant Uniforms& uniforms, float u1, float u2) {
    constant float (&marginal)[32] = uniforms.filterMarginalCDF;
    constant float (&conditional)[32][32] = uniforms.filterConditionalCDF;

    int lo = 0, hi = 31;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (marginal[mid] < u2) lo = mid + 1; else hi = mid;
    }
    int row = lo;

    lo = 0; hi = 31;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (conditional[row][mid] < u1) lo = mid + 1; else hi = mid;
    }
    int col = lo;

    float cellW = 2.0 * uniforms.filterRadius / 32.0;
    float cellH = cellW;  // FilterSampler's own build() uses the same formula for both - square cells

    float cdfColPrev = (col > 0) ? conditional[row][col - 1] : 0.0;
    float cdfColCurr = conditional[row][col];
    float du = (cdfColCurr > cdfColPrev) ? (u1 - cdfColPrev) / (cdfColCurr - cdfColPrev) : 0.5;

    float cdfRowPrev = (row > 0) ? marginal[row - 1] : 0.0;
    float cdfRowCurr = marginal[row];
    float dv = (cdfRowCurr > cdfRowPrev) ? (u2 - cdfRowPrev) / (cdfRowCurr - cdfRowPrev) : 0.5;

    MetalFilterSample s;
    s.px = -uniforms.filterRadius + (float(col) + du) * cellW;
    s.py = -uniforms.filterRadius + (float(row) + dv) * cellH;
    s.weight = uniforms.filterIntegral;
    return s;
}

inline bool sampleRealisticCameraRay(constant Uniforms& uniforms,
                                      device const LensElement* lensElements,
                                      device const ExitPupilBounds* exitPupilBounds,
                                      float u, float v, thread uint& rngState,
                                      float3 su, float3 sv, float3 sw, float3 originWorld,
                                      thread float3& outOrigin, thread float3& outDirection,
                                      thread float& outWeight) {
    outWeight = 0.0;
    if (uniforms.numLensElements == 0u || uniforms.numExitPupilBounds == 0u) return false;

    // NO leading negation on pfx (unlike pbrt-v4/CUDA's own
    // `pfx = -sample.pFilm_x`) - a real sign fix found via a mirrored
    // first render, not assumed: the CUDA reference's own `su` is
    // `RealisticCamera<T>::world_right()`, built from CPU's ALT-camera
    // path (`cameras.h::make_look_at()`'s own `cross(up,forward)`,
    // section 150's own D6 finding), but THIS function is deliberately
    // passed this loader's own `cameraRight` (`cross(forward,up)`,
    // matching CPU's PRIMARY camera instead - see this function's own
    // declaration comment for why). Dropping the negation here is
    // algebraically identical to negating `su`'s own final contribution
    // below (pfx enters the whole downstream trace linearly, only ever
    // multiplied by `su` at the very end) - the same "negate the right-
    // vector term" shape D6/D7's own fixes already used, just applied
    // at the INPUT instead of the output since this trace has many
    // intermediate steps between the two.
    float pfx = (2.0 * u - 1.0) * uniforms.filmHalfX;
    float pfy = (2.0 * v - 1.0) * uniforms.filmHalfY;

    // sample_exit_pupil
    float rFilm = sqrt(pfx * pfx + pfy * pfy);
    float filmDiag = 2.0 * sqrt(uniforms.filmHalfX * uniforms.filmHalfX + uniforms.filmHalfY * uniforms.filmHalfY);
    int sz = int(uniforms.numExitPupilBounds);
    int rIndex = int(rFilm / (filmDiag * 0.5) * float(sz));
    if (rIndex >= sz) rIndex = sz - 1;
    if (rIndex < 0) rIndex = 0;
    ExitPupilBounds b = exitPupilBounds[rIndex];
    if (b.degenerate != 0u) return false;

    float area = (b.xMax - b.xMin) * (b.yMax - b.yMin);
    if (area <= 0.0) return false;
    float ppdf = 1.0 / area;

    float u0 = randFloat(rngState), u1 = randFloat(rngState);
    float lx = b.xMin + u0 * (b.xMax - b.xMin);
    float ly = b.yMin + u1 * (b.yMax - b.yMin);

    float sinTheta = (rFilm > 0.0) ? pfy / rFilm : 0.0;
    float cosTheta0 = (rFilm > 0.0) ? pfx / rFilm : 1.0;
    float ppx = cosTheta0 * lx - sinTheta * ly;
    float ppy = sinTheta * lx + cosTheta0 * ly;
    float ppz = uniforms.lensRearZ;

    float rdx = ppx - pfx, rdy = ppy - pfy, rdz = ppz;
    float rLen = sqrt(rdx * rdx + rdy * rdy + rdz * rdz);

    // trace_lenses_from_film: camera space (film z=0, +z toward scene) ->
    // lens space (z flipped): loz=-oz, ldz=-dz.
    float lox = pfx, loy = pfy, loz = 0.0;
    float ldx = rdx, ldy = rdy, ldz = -rdz;
    float elementZ = 0.0;

    for (int i = int(uniforms.numLensElements) - 1; i >= 0; --i) {
        LensElement el = lensElements[i];
        elementZ -= el.thickness;
        bool isStop = (el.curvatureRadius == 0.0);
        float t, nx = 0.0, ny = 0.0, nz = 0.0;

        if (isStop) {
            if (ldz == 0.0) return false;
            t = (elementZ - loz) / ldz;
            if (t < 0.0) return false;
        } else {
            float zCenter = elementZ + el.curvatureRadius;
            float cox = lox, coy = loy, coz = loz - zCenter;
            float A = ldx * ldx + ldy * ldy + ldz * ldz;
            float B = 2.0 * (ldx * cox + ldy * coy + ldz * coz);
            float C = cox * cox + coy * coy + coz * coz - el.curvatureRadius * el.curvatureRadius;
            float disc = B * B - 4.0 * A * C;
            if (disc < 0.0) return false;
            float sq = sqrt(disc);
            float q = (B < 0.0) ? -0.5 * (B - sq) : -0.5 * (B + sq);
            float t0 = q / A;
            float t1 = C / q;
            if (t0 > t1) { float tmp = t0; t0 = t1; t1 = tmp; }
            bool useCloserT = (ldz > 0.0) != (el.curvatureRadius < 0.0);
            t = useCloserT ? min(t0, t1) : max(t0, t1);
            if (t < 0.0) return false;
            float hx0 = lox + t * ldx, hy0 = loy + t * ldy, hz0 = loz + t * ldz;
            nx = hx0; ny = hy0; nz = hz0 - zCenter;
            float nlen = sqrt(nx * nx + ny * ny + nz * nz);
            if (nlen == 0.0) return false;
            nx /= nlen; ny /= nlen; nz /= nlen;
            if (ldx * nx + ldy * ny + ldz * nz > 0.0) { nx = -nx; ny = -ny; nz = -nz; }
        }

        float hx = lox + t * ldx, hy = loy + t * ldy, hz = loz + t * ldz;
        if (hx * hx + hy * hy > el.apertureRadius * el.apertureRadius) return false;
        lox = hx; loy = hy; loz = hz;

        if (!isStop) {
            float etaI = (el.eta == 0.0) ? 1.0 : el.eta;
            float etaT = (i > 0 && lensElements[i - 1].eta != 0.0) ? lensElements[i - 1].eta : 1.0;
            float len = sqrt(ldx * ldx + ldy * ldy + ldz * ldz);
            float dxn = ldx / len, dyn = ldy / len, dzn = ldz / len;
            float eta = etaI / etaT;
            float cosI = -(dxn * nx + dyn * ny + dzn * nz);
            float sin2T = eta * eta * max(0.0, 1.0 - cosI * cosI);
            if (sin2T >= 1.0) return false;
            float cosT = sqrt(1.0 - sin2T);
            ldx = eta * dxn + (eta * cosI - cosT) * nx;
            ldy = eta * dyn + (eta * cosI - cosT) * ny;
            ldz = eta * dzn + (eta * cosI - cosT) * nz;
        }
    }

    float lensOutOx = lox, lensOutOy = loy, lensOutOz = -loz;
    float lensOutDx = ldx, lensOutDy = ldy, lensOutDz = -ldz;

    float cosThetaW = (rLen > 0.0) ? abs(rdz / rLen) : 0.0;
    float lrz = uniforms.lensRearZ;
    if (lrz <= 0.0) return false;
    float w = (cosThetaW * cosThetaW * cosThetaW * cosThetaW) / (ppdf * lrz * lrz);

    outOrigin = originWorld + lensOutOx * su + lensOutOy * sv + lensOutOz * sw;
    outDirection = normalize(lensOutDx * su + lensOutDy * sv + lensOutDz * sw);
    outWeight = w;
    return true;
}



// Uniform sample on a unit disk (r = sqrt(u1) for area-uniform density,
// not r = u1 - the same sqrt used for the hemisphere sample's own radius
// below, same reason: linear r would bunch samples toward the centre).
// Used only by the thin-lens depth-of-field sample in the kernel below -
// a camera aperture is a flat disk, not a hemisphere, so this is its own
// small helper rather than reusing cosineSampleHemisphere's.
inline float2 sampleUnitDisk(thread uint& rngState) {
    float u1 = randFloat(rngState);
    float u2 = randFloat(rngState);
    float r = sqrt(u1);
    float theta = 2.0 * M_PI_F * u2;
    return float2(r * cos(theta), r * sin(theta));
}

// A regular-polygon aperture instead of a circular one - real camera
// lenses focus light through a finite number of physical aperture
// blades, not a perfect circle, which is exactly why out-of-focus
// highlights ("bokeh") in a real photo read as hexagons/pentagons/etc.
// rather than perfectly round discs; `sampleUnitDisk()` above is the
// idealized circular-aperture limit (infinite blades), a real
// approximation this POC's own DOF used unconditionally through step 19.
// Samples uniformly by picking one of `sides` equal triangular wedges
// (origin - vertex_k - vertex_{k+1}) uniformly at random, then a point
// within that wedge via the standard sqrt-for-uniform-triangle-area
// trick - the textbook regular-polygon sampling construction, not an
// approximation of one.
inline float2 samplePolygonAperture(uint sides, thread uint& rngState) {
    float u1 = randFloat(rngState);
    float u2 = randFloat(rngState);
    float u3 = randFloat(rngState);
    uint blade = min(uint(u1 * float(sides)), sides - 1);
    float angleStep = 2.0 * M_PI_F / float(sides);
    float2 vertexA = float2(cos(angleStep * float(blade)), sin(angleStep * float(blade)));
    float2 vertexB = float2(cos(angleStep * float(blade + 1)), sin(angleStep * float(blade + 1)));
    float s = sqrt(u2);
    return s * ((1.0 - u3) * vertexA + u3 * vertexB);
}

inline float3 cosineSampleHemisphere(float3 normal, thread uint& rngState) {
    float u1 = randFloat(rngState);
    float u2 = randFloat(rngState);
    float r = sqrt(u1);
    float theta = 2.0 * M_PI_F * u2;
    float x = r * cos(theta);
    float y = r * sin(theta);
    float z = sqrt(max(0.0, 1.0 - u1));

    // Build an orthonormal basis around `normal` (Duff et al.'s branchless
    // construction) - same "pick any tangent frame, only the normal
    // matters" approach this project's own onb.h uses for the CPU path.
    float sign = normal.z >= 0.0 ? 1.0 : -1.0;
    float a = -1.0 / (sign + normal.z);
    float b = normal.x * normal.y * a;
    float3 tangent = float3(1.0 + sign * normal.x * normal.x * a, sign * b, -sign * normal.x);
    float3 bitangent = float3(b, sign + normal.y * normal.y * a, -normal.y);

    return normalize(x * tangent + y * bitangent + z * normal);
}

// pbrt-v4's own DiffuseBxDF::PDF() (src/shared/bxdfs_simple.h's own
// DiffuseBxDF<T>::scattering_pdf(), the exact CPU reference this function
// is numerically cross-checked against in metal_poc_shader_tests.mm) -
// cos(theta_wi)/pi for a same-side direction, 0 otherwise. `cosWi` is
// already `dot(normal, wi)` - this function doesn't take the vectors
// themselves since every call site already has that dot product on hand.
// Factored out of shadeLambertian() (metal_poc_materials_diffuse.metal)
// so it's independently testable, mirroring the same "pull the pure local-
// frame math out of the big shadeXxx() function so a test kernel can call
// the EXACT production formula" precedent hairScatteringPdfLocal() already
// established (metal_poc_materials_hair.metal, section 183) - not a hand-
// copied duplicate a test could pass even if the real formula were wrong.
inline float lambertianPdf(float cosWi) {
    return cosWi > 0.0 ? cosWi / M_PI_F : 0.0;
}

// pbrt-v4's own DiffuseTransmissionBxDF::PDF() (src/shared/bxdfs_layered.h's
// own DiffuseTransmissionBxDF<T>::scattering_pdf(), same reference role as
// lambertianPdf()'s own comment above) - `cosWi`'s sign picks which lobe
// (reflection when positive, transmission when negative), `pr`/`pt` are the
// max-channel reflectance/transmittance pbrt-v4 uses as each lobe's own
// selection probability (shadeDiffuseTransmission()'s own `pr`/`pt` locals -
// this function takes them as plain parameters rather than re-deriving them
// from a TriangleMaterial, so it stays exactly as reusable/testable as
// lambertianPdf() above). Same factoring rationale.
inline float diffuseTransmissionPdf(float cosWi, float pr, float pt) {
    float pSum = max(pr + pt, 1e-6);
    float lobeProb = (cosWi > 0.0) ? (pr / pSum) : (pt / pSum);
    return lobeProb * abs(cosWi) / M_PI_F;
}

// UNIFORM (not cosine-weighted) hemisphere sampling - materialType 14's
// own velvet material needs this: Ashikhmin & Shirley's own velvet BRDF
// (see shadeVelvet's own comment) is sampled uniformly in the reference
// this was ported from (Blender Cycles' own bsdf_ashikhmin_velvet.h),
// the same "don't bother importance-sampling a niche lobe's own oddly-
// shaped distribution, plain uniform/cosine sampling is simpler and
// still unbiased, just higher-variance" simplification this POC's own
// Oren-Nayar material (materialType 13) already makes too. Same Duff et
// al. branchless ONB construction as cosineSampleHemisphere() above -
// only the (x,y,z) distribution differs (z = u1 directly, not sqrt(u1),
// the standard uniform-over-solid-angle construction).
inline float3 sampleUniformHemisphere(float3 normal, thread uint& rngState) {
    float u1 = randFloat(rngState);
    float u2 = randFloat(rngState);
    float z = u1;
    float r = sqrt(max(0.0, 1.0 - z * z));
    float theta = 2.0 * M_PI_F * u2;
    float x = r * cos(theta);
    float y = r * sin(theta);

    float sign = normal.z >= 0.0 ? 1.0 : -1.0;
    float a = -1.0 / (sign + normal.z);
    float b = normal.x * normal.y * a;
    float3 tangent = float3(1.0 + sign * normal.x * normal.x * a, sign * b, -sign * normal.x);
    float3 bitangent = float3(b, sign + normal.y * normal.y * a, -normal.y);

    return normalize(x * tangent + y * bitangent + z * normal);
}

// Barycentric-interpolated shading normal - replaces the earlier flat
// per-triangle face normal (cross product of two edges, same value at
// every point of a triangle) with a real per-vertex-normal blend, using
// the hit's own barycentric coordinates (triangle_data tag on the
// intersector/intersection_result is what makes those available at all).
// For the hand-authored room quads, the three corner normals stored in
// `normals` are all identical (the quad's own flat face normal, written
// that way host-side) so this reduces to exactly the old flat-shading
// behaviour there - only a real mesh with genuinely different per-vertex
// normals (Suzanne's own `vn` data) sees a different, smoothly-varying
// result. Same interpolation `.obj`/pbrt-v4/this project's own CPU
// triangle.h use for a shading normal, not an approximation of it.
// `positions` (the SAME per-corner-indexed buffer shadingNormalFor's own
// caller already has bound for this triangle buffer - `vertices` for the
// room/mesh case, `suzanneVertices` for the Suzanne-instance case) backs
// the fallback below: a barycentric blend of two opposite (or nearly
// opposite) vertex normals - a mid-edge point between a convex crease's
// two differently-angled vertex normals, or simply a degenerate authored
// normal - can cancel to (near) zero length, and normalizing that would
// produce a NaN shading normal that poisons every later calculation this
// hit feeds into. CPU's triangle.h and all 3 OptiX closest-hit sites
// already guard this exact case (interpolate_shading_normal()/triangle.h's
// own `len2 > 1e-20` check, commit aca9117) by falling back to the
// triangle's own flat FACET normal (cross of two edges) instead - this
// Metal loader's own independent copy of the same interpolation had no
// such guard until now. 1e-20 matches those call sites exactly, not a
// locally-chosen tolerance.
inline float3 shadingNormalFor(uint primId, float2 barycentric, device const packed_float3* normals,
                                device const packed_float3* positions) {
    float3 n0 = float3(normals[primId * 3 + 0]);
    float3 n1 = float3(normals[primId * 3 + 1]);
    float3 n2 = float3(normals[primId * 3 + 2]);
    float w0 = 1.0 - barycentric.x - barycentric.y;
    float3 blended = w0 * n0 + barycentric.x * n1 + barycentric.y * n2;
    float len2 = dot(blended, blended);
    if (len2 > 1e-20) return blended * (1.0 / sqrt(len2));
    float3 p0 = float3(positions[primId * 3 + 0]);
    float3 p1 = float3(positions[primId * 3 + 1]);
    float3 p2 = float3(positions[primId * 3 + 2]);
    return normalize(cross(p1 - p0, p2 - p0));
}

// Same zero-length-blend guard as shadingNormalFor() above, for the one
// caller (Suzanne's own instanced hit, metal_poc_kernel.metal) that has
// no matching per-corner POSITION buffer bound to primaryRayKernel to
// fall back to a true facet normal with - Suzanne's own vertex positions
// only ever reach the GPU as the instanced acceleration structure's own
// geometry-descriptor vertex buffer (metal_poc_gpu_resources.mm's own
// suzanneVertexBuffer), which Metal's hardware traversal consumes
// directly and this shader has no other handle to (adding one would need
// a 32nd kernel buffer argument - MTLArgumentBuffer's own hard "0-30"
// index ceiling for a function this size, confirmed by the compiler
// itself rejecting buffer(31) - not a limit worth taking on for an edge
// case this rare). Falls back to whichever single vertex normal is
// itself non-degenerate instead of the flat facet normal: still exact
// (no interpolation) at that one vertex, never NaN, and visually
// indistinguishable from a true facet-normal fallback for the
// vanishingly rare mid-edge/degenerate-normal hit this guards against in
// the first place.
inline float3 shadingNormalForNoFacet(uint primId, float2 barycentric, device const packed_float3* normals) {
    float3 n0 = float3(normals[primId * 3 + 0]);
    float3 n1 = float3(normals[primId * 3 + 1]);
    float3 n2 = float3(normals[primId * 3 + 2]);
    float w0 = 1.0 - barycentric.x - barycentric.y;
    float3 blended = w0 * n0 + barycentric.x * n1 + barycentric.y * n2;
    float len2 = dot(blended, blended);
    if (len2 > 1e-20) return blended * (1.0 / sqrt(len2));
    if (dot(n0, n0) > 1e-20) return normalize(n0);
    if (dot(n1, n1) > 1e-20) return normalize(n1);
    return normalize(n2);
}

// Same barycentric-blend idea as shadingNormalFor(), for texture
// coordinates instead of normals - a UV buffer parallel to
// vertices/normals, same per-triangle-corner indexing. Every non-textured
// primitive's three corners carry (0,0) (see addQuad()'s/loadObjMesh()'s
// host-side default), which interpolates to (0,0) too - harmless, since
// only materialType == 3 ever reads it.
inline float2 texCoordFor(uint primId, float2 barycentric, device const packed_float2* uvs) {
    float2 uv0 = uvs[primId * 3 + 0];
    float2 uv1 = uvs[primId * 3 + 1];
    float2 uv2 = uvs[primId * 3 + 2];
    float w0 = 1.0 - barycentric.x - barycentric.y;
    return w0 * uv0 + barycentric.x * uv1 + barycentric.y * uv2;
}

// pbrt-v4's stochastic alpha test (src/shared/ray_hash.h, the same function the CPU and OptiX use): a hit is kept with probability
// `alpha`, decided by a hash of the ray, so shadow rays and camera rays make independent, state-free choices.
inline uint rayHashMix(uint h) {   // murmur3 finalizer
    h ^= h >> 16; h *= 0x85ebca6bu;
    h ^= h >> 13; h *= 0xc2b2ae35u;
    h ^= h >> 16;
    return h;
}
inline float rayHash01(float3 o, float3 d) {
    uint h = 0x9e3779b9u;
    h = rayHashMix(h ^ as_type<uint>(o.x)); h = rayHashMix(h ^ as_type<uint>(o.y)); h = rayHashMix(h ^ as_type<uint>(o.z));
    h = rayHashMix(h ^ as_type<uint>(d.x)); h = rayHashMix(h ^ as_type<uint>(d.y)); h = rayHashMix(h ^ as_type<uint>(d.z));
    return float(h >> 8) * (1.0 / 16777216.0);
}
inline bool alphaPasses(float a, float3 o, float3 d) {
    if (a >= 1.0) return true;
    if (a <= 0.0) return false;
    return rayHash01(o, d) <= a;
}

// Any-hit test for the main triangle geometry, which is made non-opaque (so this runs) only when the scene needs it: an alpha-cutout mask
// (a hit is kept with probability alpha, the mask sampled like the CPU's image_texture: nearest texel, (u, v) clamped to [0, 1], v flipped) or
// a subsurface material (a shadow ray passes through it, as on the CPU and OptiX: the BSSRDF accounts for the light inside the object).
// `primitive_id` is the index into the flat triangle arrays.
[[intersection(triangle, triangle_data, instancing)]]
bool triangleAnyHitFunction(
    float2 barycentric [[barycentric_coord]],
    uint groupPrimitiveIndex [[primitive_id]],
    uint instanceIndex [[instance_id]],
    float3 origin [[origin]],
    float3 direction [[direction]],
    device const TriangleMaterial* triMaterials [[buffer(5)]],
    device const packed_float2* uvs [[buffer(6)]],
    device const float* alphaData [[buffer(7)]],
    device const InstanceTransform* instanceTransforms [[buffer(8)]],
    ray_data SpherePayload& payload [[payload]])
{
    // The scene's own triangles are instance 0 (triBase 0); a pbrt ObjectInstance's group stores its triangles once, after them, at that instance's triBase.
    const uint primitiveIndex = groupPrimitiveIndex + instanceTransforms[instanceIndex].triBase;
    const TriangleMaterial mat = triMaterials[primitiveIndex];
    // (A subsurface surface blocks a shadow ray like every pbrt-v4 surface that has a material: it used to be skipped here, and the floor under and beside a subsurface ball read
    // 17% bright; the CPU and both OptiX backends block it now too.)
    if (mat.alphaWidth <= 0) return true;
    const float2 uv = texCoordFor(primitiveIndex, barycentric, uvs);
    const float u = clamp(uv.x, 0.0, 1.0);
    const float v = 1.0 - clamp(uv.y, 0.0, 1.0);
    const int i = min(int(u * float(mat.alphaWidth)), mat.alphaWidth - 1);
    const int j = min(int(v * float(mat.alphaHeight)), mat.alphaHeight - 1);
    const float alpha = alphaData[mat.alphaOffset + j * mat.alphaWidth + i];
    return alphaPasses(alpha, origin, direction);
}

// A per-triangle tangent (constant across the triangle, same "flat is
// fine here" reasoning addQuad()'s own flat face normal already relies
// on - every quad this POC hand-authors is planar with a single UV
// gradient, not a smoothly-varying mesh surface): the standard position/
// UV partial-derivative construction (solve for the UV-space basis that
// maps to world-space edge1/edge2), same technique pbrt-v4's own
// triangle tangent setup uses. `primId`-indexed the same flat, 3-per-
// triangle way vertices/uvs already are - reuses those two buffers
// directly, no new per-triangle data needed host-side.
inline float3 tangentFor(uint primId, device const packed_float3* verts, device const packed_float2* uvs) {
    float3 p0 = float3(verts[primId * 3 + 0]);
    float3 p1 = float3(verts[primId * 3 + 1]);
    float3 p2 = float3(verts[primId * 3 + 2]);
    float2 uv0 = uvs[primId * 3 + 0];
    float2 uv1 = uvs[primId * 3 + 1];
    float2 uv2 = uvs[primId * 3 + 2];
    float3 edge1 = p1 - p0;
    float3 edge2 = p2 - p0;
    float2 duv1 = uv1 - uv0;
    float2 duv2 = uv2 - uv0;
    float det = duv1.x * duv2.y - duv2.x * duv1.y;
    // A degenerate UV mapping (det == 0, e.g. every corner sharing (0,0) -
    // every non-materialType-7 primitive's own default UVs) has no real
    // tangent to solve for; returning SOME unit vector rather than NaN
    // keeps this safe to call unconditionally, even though only
    // materialType == 7 ever actually uses the result.
    if (abs(det) < 1e-10) {
        return normalize(edge1);
    }
    float f = 1.0 / det;
    float3 tangent = f * (duv2.y * edge1 - duv1.y * edge2);
    return normalize(tangent);
}

// A look-up in a picture at pbrt texture coordinates (s, t) (t up, before the image's top-down row order), honouring the texture's "string wrap": clamp reads the edge texel
// beyond the picture, black reads black beyond it, repeat (the sampler's own mode) tiles it. 0 clamp, 1 repeat, 2 black (MipWrapMode).
// pbrt's spherical, cylindrical and planar texture mappings of a hit point: `d` is the 21-float block the host stores (PbrtSceneLoader::claimPositionMapping): the kind, the
// texture-from-Metal-world 3x4 matrix, v1, v2, udelta, vdelta (src/shared/texture_position_mapping.h, positionToSt).
inline float2 positionMappedSt(device const float* d, float3 hitPoint) {
    const int kind = int(d[0] + 0.5);
    const float x = d[1] * hitPoint.x + d[2] * hitPoint.y + d[3] * hitPoint.z + d[4];
    const float y = d[5] * hitPoint.x + d[6] * hitPoint.y + d[7] * hitPoint.z + d[8];
    const float z = d[9] * hitPoint.x + d[10] * hitPoint.y + d[11] * hitPoint.z + d[12];
    if (kind == 1) {   // spherical
        const float len = sqrt(x * x + y * y + z * z);
        const float cosTheta = len > 0.0 ? clamp(z / len, -1.0f, 1.0f) : 1.0f;
        float phi = atan2(y, x);
        if (phi < 0.0) phi += 2.0 * M_PI_F;
        return float2(acos(cosTheta) / M_PI_F, phi / (2.0 * M_PI_F));
    }
    if (kind == 2) return float2((M_PI_F + atan2(y, x)) / (2.0 * M_PI_F), z);   // cylindrical
    return float2(d[19] + (x * d[13] + y * d[14] + z * d[15]), d[20] + (x * d[16] + y * d[17] + z * d[18]));   // planar
}

// A picture reference (1 + the offset of 12 floats in the shared float buffer, PbrtSceneLoader::claimImageRef): the picture's value at a hit, bilinear with texel centres and pbrt's
// (s, 1 - t) orientation, wrapped as the texture says (0 clamp, 1 repeat, 2 black), times the reference's scale. `uv` is the surface's own; a position-based mapping uses the hit point.
inline float3 sampleImageRef(device const float* grid, int ref1, float2 uv, float3 hitPoint) {
    device const float* r = grid + (ref1 - 1);
    const int offset = int(r[0] + 0.5), w = int(r[1] + 0.5), h = int(r[2] + 0.5), wrap = int(r[3] + 0.5);
    const int posMap = int(r[9] + 0.5);
    const float2 st = posMap > 0 ? positionMappedSt(grid + (posMap - 1), hitPoint) : float2(r[5] * uv.x + r[7], r[6] * uv.y + r[8]);
    const float fx = st.x * float(w) - 0.5, fy = (1.0 - st.y) * float(h) - 0.5;
    const int x0 = int(floor(fx)), y0 = int(floor(fy));
    const float ax = fx - float(x0), ay = fy - float(y0);
    float3 acc = float3(0.0);
    for (int j = 0; j < 2; ++j) {
        for (int i = 0; i < 2; ++i) {
            int ix = x0 + i, iy = y0 + j;
            const float wgt = (i == 0 ? 1.0 - ax : ax) * (j == 0 ? 1.0 - ay : ay);
            if (wrap == 1) { ix = ((ix % w) + w) % w; iy = ((iy % h) + h) % h; }
            else if (wrap == 2) { if (ix < 0 || ix >= w || iy < 0 || iy >= h) continue; }
            else { ix = clamp(ix, 0, w - 1); iy = clamp(iy, 0, h - 1); }
            device const float* px = grid + offset + (iy * w + ix) * 3;
            acc += wgt * float3(px[0], px[1], px[2]);
        }
    }
    return acc * r[4];
}

inline float4 samplePictureWrapped(texture2d<float, access::sample> picture, sampler pictureSampler, float2 st, int wrapMode) {
    if (wrapMode == 2 && (st.x < 0.0 || st.x > 1.0 || st.y < 0.0 || st.y > 1.0)) return float4(0.0);
    if (wrapMode == 0) {
        const float2 halfTexel = 0.5 / float2(float(picture.get_width()), float(picture.get_height()));
        st = clamp(st, halfTexel, 1.0 - halfTexel);
    }
    return picture.sample(pictureSampler, float2(st.x, 1.0 - st.y));
}

// The offset direction for a ray spawned at a hit: 0.001 along the GEOMETRIC normal toward the side of `dir`, as pbrt-v4 offsets spawned rays (SpawnRay), so a direction the shading
// normal sends below the surface (a bumped or normal-mapped one, or an interpolated one at a grazing angle) leaves it instead of starting above it and hitting it. `fallback` is
// what the offset was before, used where the geometric normal was not set.
inline float3 spawnSide(TriangleMaterial mat, float3 fallback, float3 dir) {
    const float3 g = float3(mat.spawnNormal);
    if (dot(g, g) < 0.5) return fallback;
    return dot(dir, g) >= 0.0 ? g : -g;
}

// Procedural "egg carton" bump map - an analytic height field h(u,v)
// instead of a sampled normal-map texture (no new image asset needed for
// this POC to demonstrate genuine tangent-space shading-normal
// perturbation): standard bump-mapping math, perturbing the normal by
// the height field's own partial derivatives along the tangent/
// bitangent axes (`normal - dh/du * tangent - dh/dv * bitangent`,
// renormalized) rather than actually displacing geometry - the textbook
// distinction between bump mapping (shading only, what this is) and
// real displacement mapping (which this is NOT). `strength` scales the
// derivative term directly - 0.0 (every material type other than 7)
// exactly reproduces the unperturbed normal, a true no-op, not just a
// visually-close approximation of one.
inline float3 proceduralBumpNormal(float3 normal, float3 tangent, float2 uv, float strength) {
    float3 bitangent = cross(normal, tangent);
    const float freqU = 4.0;
    const float freqV = 4.0;
    float au = uv.x * 2.0 * M_PI_F * freqU;
    float av = uv.y * 2.0 * M_PI_F * freqV;
    // `strength` scales the slope DIRECTLY (capped at 1 by cos/sin, so
    // `strength` itself is the max tilt magnitude along each tangent
    // axis) rather than also carrying a 2*pi*freq amplitude factor - a
    // literal height-field derivative would include that factor, but even
    // at this modest freqU/freqV it inflates to ~25x, drowning out the
    // unit normal entirely regardless of how small `strength` is. Tuned
    // as a slope, not a physical height, the same "whatever reads well"
    // spirit checkerColor()'s own tile-B-darkening fraction already uses
    // instead of a physically-derived constant. freqU/freqV == 4 (a few
    // bumps across this panel's own 0-1 UV span) rather than something
    // higher-frequency - a bump's own spatial period needs to stay well
    // above this scene's pixel footprint per UV unit, or it aliases into
    // per-pixel noise indistinguishable from Monte Carlo grain instead of
    // a visible bump shape (a real mistake this PR's own first attempt at
    // this material made, caught by inspecting a raw shading-normal
    // visualization render, not assumed away).
    float dhdu = strength * cos(au) * sin(av);
    float dhdv = strength * sin(au) * cos(av);
    float3 bumped = normal - (dhdu * tangent + dhdv * bitangent);
    return normalize(bumped);
}

// Standard equirectangular direction-to-UV mapping (longitude from
// atan2, latitude from asin) - a genuinely different way of sampling
// `earthTexture` than texCoordFor()'s own per-vertex-UV lookup above:
// this one has no notion of a surface or a mesh at all, just a ray
// DIRECTION, the way a real environment/IBL map is sampled for a miss
// ray (or, in a fuller renderer, for image-based lighting on rough
// surfaces too - not implemented here, this POC only uses it for the
// miss/"sky" case).
inline float2 equirectangularUV(float3 dir) {
    float u = atan2(dir.z, dir.x) * (1.0 / (2.0 * M_PI_F)) + 0.5;
    float v = asin(clamp(dir.y, -1.0, 1.0)) * (1.0 / M_PI_F) + 0.5;
    return float2(u, v);
}

// (u, v) of the outward normal `n` of a sphere: pbrt's coordinates, about the sphere's own z axis. The rows of `uvRot` carry the world direction into the frame the older
// mapping above is defined in (the project's get_sphere_uv: theta = acos(-y), phi = atan2(-z, x) + pi), where it is called as before with z negated.
inline float2 sphereUV(packed_float3 row0, packed_float3 row1, packed_float3 row2, float3 n) {
    const float3 d = float3(dot(float3(row0), n), dot(float3(row1), n), dot(float3(row2), n));
    return equirectangularUV(float3(d.x, d.y, -d.z));
}

// The surface's own (u, v) at a hit: a sphere's by its normal, a triangle's by its barycentrics, zero for a disk or a cylinder (they have none).
inline float2 hitUV(uint primId, float2 barycentric, device const packed_float2* uvs, device const SphereData* spheres, float3 normal, bool isSphere, bool isTriangleHit) {
    if (isSphere) return sphereUV(spheres[primId].uvRot0, spheres[primId].uvRot1, spheres[primId].uvRot2, normal);
    if (isTriangleHit) return texCoordFor(primId, barycentric, uvs);
    return float2(0.0);
}

// A material's per-pixel parameters from pictures (TriangleMaterial::roughRef / condRoughRef, PbrtSceneLoader::mapPbrtMaterial): this hit's roughness, in the hit's own copy of the
// material, as OptiX does (patch_roughness_textures: roughness = scale * the picture's value, isotropic). The host stored the flat value (the picture's mean) as the fallback. A reference's
// slot 10 says the value is a perceptual roughness to remap to the GGX alpha (sqrt), as the host does for a flat one.
inline void patchPerPixelMaterial(thread TriangleMaterial& mat, device const float* grid, uint primId, float2 barycentric, device const packed_float2* uvs,
                                  device const SphereData* spheres, float3 normal, float3 hitPoint, bool isSphere, bool isTriangleHit) {
    if (mat.roughRef == 0 && mat.condRoughRef == 0) return;
    const float2 uv = hitUV(primId, barycentric, uvs, spheres, normal, isSphere, isTriangleHit);
    if (mat.roughRef != 0) {
        const float v = max(0.0f, sampleImageRef(grid, mat.roughRef, uv, hitPoint).x);
        const float alpha = (grid[mat.roughRef - 1 + 10] > 0.5) ? sqrt(v) : v;
        if (mat.materialType == METAL_MAT_ROUGH_CONDUCTOR) {
            // shadeConductor() squares the roughness-style value it is given into alpha, so a conductor holds sqrt(alpha) in both slots (alphaX = ior, alphaY = roughness; isotropic)
            mat.ior = sqrt(alpha);
            mat.roughness = mat.ior;
        } else {
            mat.roughness = alpha;
        }
    }
    if (mat.condRoughRef != 0 && mat.materialType == METAL_MAT_COATED_CONDUCTOR) {
        const float v = max(0.0f, sampleImageRef(grid, mat.condRoughRef, uv, hitPoint).x);
        mat.transmitColor = packed_float3((grid[mat.condRoughRef - 1 + 10] > 0.5) ? sqrt(v) : v, mat.transmitColor.y, mat.transmitColor.z);
    }
}

// The environment-map (sky image) mapping, the one the CPU's sky_light and the OptiX backends use: row 0 of the image is straight up
// (v = theta / pi, theta measured from +y) and u = phi / (2 pi) with phi = atan2(-z, x) taken into [0, 1). equirectangularUV() above is a
// different, older convention (v = 1 at +y, longitude mirrored and shifted) that the procedural patterns and sphere textures still use, so
// environment lookups, the importance-sampling table and its pdf must go through THIS function and sampleEnvironmentDirection()'s inverse.
inline float2 envMapUV(float3 dir) {
    float u = atan2(-dir.z, dir.x) * (0.5 / M_PI_F);
    if (u < 0.0) u += 1.0;
    float v = acos(clamp(dir.y, -1.0, 1.0)) * (1.0 / M_PI_F);
    return float2(u, v);
}


// A shadow ray starts 0.001 off the surface along its normal and used to stop 0.002 short of the sampled point measured along the ORIGINAL
// direction from the hit point. For a light whose plane is nearly parallel to the receiver (an up-facing plate under a ceiling) a ray more
// than ~60 degrees from the normal crossed the light's own plane before that end point, so the light shadowed itself (87% of the samples at
// an oblique ceiling point, 3x too dark). Re-aim the ray at the sampled point from the offset origin and stop 0.002 short of it.
// `r.direction` must still be the unit vector from `hitPoint` to the target, `dist` the distance to it.
inline void reaimShadowRay(thread ray& r, float3 hitPoint, float dist) {
    const float3 toTarget = (hitPoint + r.direction * dist) - r.origin;
    const float len = length(toTarget);
    r.direction = toTarget / max(len, 1e-8f);
    r.max_distance = len - 0.002f;
}

// Shadow-ray visibility query: any intersection blocks the ray, so the
// traversal can stop at the first one instead of searching for the closest.
// Same argument list as the call it replaces, only the intersector mode differs.
inline intersection_result<instancing, triangle_data> traceShadowAny(
    intersector<instancing, triangle_data> isect, ray shadowRay,
    instance_acceleration_structure accelStructure,
    intersection_function_table<instancing, triangle_data> functionTable) {
    intersector<instancing, triangle_data> anyIsect = isect;
    anyIsect.accept_any_intersection(true);
    // Explicit payload: the 3-argument intersect() overload hands the intersection
    // function an implicitly default-constructed payload whose fields are NOT
    // reliably initialised (the struct's default member initialisers are not
    // applied), so a sphere shadow test could run with garbage in
    // shutterT/isShadowRay/shadowIgnorePrimId and over-block (C4: a sphere's
    // shadow extended well past its true extent).
    SpherePayload payload{0.0, true, -1};
    return anyIsect.intersect(shadowRay, accelStructure, functionTable, payload);
}

// The shadow ray of a hit: as traceShadowAny(), with the path's own colour channel and time (TriangleMaterial::shadowChannel / rayTime, set by resolveHit()). Without them a shadow
// ray through a chromatic medium sphere used the MEAN extinction while the path's throughput followed one channel (coloured fog read 3% off in red), and a moving sphere cast its
// shadow from where it is at time 0, whatever the time of the sample.
inline intersection_result<instancing, triangle_data> traceShadowAnyM(
    intersector<instancing, triangle_data> isect, ray shadowRay,
    instance_acceleration_structure accelStructure,
    intersection_function_table<instancing, triangle_data> functionTable,
    TriangleMaterial mat) {
    intersector<instancing, triangle_data> anyIsect = isect;
    anyIsect.accept_any_intersection(true);
    SpherePayload payload{mat.rayTime, true, -1};
    payload.shadowChannel = mat.shadowChannel;
    return anyIsect.intersect(shadowRay, accelStructure, functionTable, payload);
}

inline intersection_result<instancing, triangle_data> traceShadowAnyP(
    intersector<instancing, triangle_data> isect, ray shadowRay,
    instance_acceleration_structure accelStructure,
    intersection_function_table<instancing, triangle_data> functionTable,
    thread SpherePayload& payload) {
    intersector<instancing, triangle_data> anyIsect = isect;
    anyIsect.accept_any_intersection(true);
    return anyIsect.intersect(shadowRay, accelStructure, functionTable, payload);
}
