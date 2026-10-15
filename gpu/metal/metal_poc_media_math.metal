// metal_poc_media_math.metal
// Participating-media math: Henyey-Greenstein phase function, the procedural cloud density, RGB density grids and their slab tests.
// Split out of metal_poc_sampling.metal (pure code motion) for the code-size limit; concatenated in the order of metal_poc_shader_files.h.

// Henyey-Greenstein phase function - the standard analytic model for
// directional (not just isotropic) volume scattering, same one pbrt-v4's
// own HGPhaseFunction implements. `cosTheta` here is dot(wo, wi) in the
// SAME `wo` convention the GGX conductor code above already uses (wo
// points back toward where the ray came from, i.e. `-rayDir`) - under
// that convention, g > 0 peaking at cosTheta == -1 (wi antiparallel to
// wo, i.e. wi roughly EQUAL to the ray's own original travel direction)
// is exactly "forward scattering," matching the physical convention;
// getting this sign backwards is the single easiest mistake to make with
// this formula, so it's called out explicitly rather than left to be
// inferred from the algebra alone.
inline float henyeyGreensteinPhase(float cosTheta, float g) {
    // Same (-0.99, 0.99) clamp the CPU reference applies (src/shared/
    // volume_scattering.h) - denom -> 0 as g -> +/-1 at cosTheta -> -/+1,
    // and the sqrt() floor below only guards the sqrt, not the outer
    // bare `denom` factor, so an unclamped |g| ~ 1 spikes toward Inf/NaN.
    g = clamp(g, -0.99, 0.99);
    float denom = 1.0 + g * g + 2.0 * g * cosTheta;
    return (1.0 - g * g) / (4.0 * M_PI_F * denom * sqrt(max(denom, 1e-6)));
}

// Samples a direction from the HG phase function's own distribution
// relative to `wo` (same convention as the evaluation function above -
// the local frame's own Z axis IS wo, via buildOnb(), so the returned
// direction's dot product with wo equals the sampled `cosTheta` by
// construction, consistent with what henyeyGreensteinPhase() expects to
// be called with for MIS/NEE against this same sample). At g == 0 this
// reduces to a uniform-over-the-sphere DISTRIBUTION - a rotationally-
// invariant distribution is identical whether sampled relative to world
// axes or relative to an arbitrary local frame like `wo` - not a
// separate code path that happens to agree, the same formula
// degenerating correctly at its own
// boundary case.
inline float3 sampleHenyeyGreenstein(float3 wo, float g, thread uint& rngState) {
    g = clamp(g, -0.99, 0.99);  // see henyeyGreensteinPhase()'s own comment
    float u1 = randFloat(rngState);
    float u2 = randFloat(rngState);
    float cosTheta;
    if (abs(g) < 1e-3) {
        cosTheta = 1.0 - 2.0 * u1;
    } else {
        float sqrTerm = (1.0 - g * g) / (1.0 + g - 2.0 * g * u1);
        cosTheta = -1.0 / (2.0 * g) * (1.0 + g * g - sqrTerm * sqrTerm);
    }
    float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));
    float phi = 2.0 * M_PI_F * u2;
    float3 tangent, bitangent;
    buildOnb(wo, tangent, bitangent);
    return sinTheta * cos(phi) * tangent + sinTheta * sin(phi) * bitangent + cosTheta * wo;
}

// E2/section 178: cloud density at a MEDIUM-space point, [0,1]-clamped - a port of
// CloudMedium<T>::compute_density() (5-octave FBm plus, since 2026-10-08, its wispiness warp; OptiX's gpu_cloud_density() still
// leaves the warp out - see that function's comment,
// src/shared/cloud_medium.h). Reuses materialType 17's own
// perlinNoise3D()/kNoisePerm (this file, above) rather than a second
// copy of the same fixed permutation table - the fixed table + Noise()
// formula is exactly the same pbrt-v4 primitive either caller needs.
// `my` (medium-space Y) drives the altitude falloff: pbrt-v4's own
// convention is medium-y=0 is the cloud's dense base, medium-y=1 is
// thinned to nothing.
inline float gpuCloudDensity(GpuCloudMedium cloud, float mx, float my, float mz) {
    float3 pp = cloud.frequency * float3(mx, my, mz);
    // "Wispiness": two octaves of domain warping by the finite-difference gradient of the noise, as CloudMedium::compute_density() does
    // on the CPU (src/shared/cloud_medium.h; pbrt-v4's CloudMedium). Without it the cloud is smoother and denser along its top.
    if (cloud.wispiness > 0.0) {
        float vomega = 0.05 * cloud.wispiness;
        float vlambda = 10.0;
        for (int w = 0; w < 2; ++w) {
            const float3 q = vlambda * pp;
            const float n0 = perlinNoise3D(q);
            const float delta = 0.01;
            pp += vomega * float3((perlinNoise3D(q + float3(delta, 0.0, 0.0)) - n0) / delta,
                                  (perlinNoise3D(q + float3(0.0, delta, 0.0)) - n0) / delta,
                                  (perlinNoise3D(q + float3(0.0, 0.0, delta)) - n0) / delta);
            vomega *= 0.5;
            vlambda *= 1.99;
        }
    }
    float d = 0.0;
    float omega = 0.5, lambda = 1.0;
    for (int oct = 0; oct < 5; ++oct) {
        d += omega * perlinNoise3D(pp * lambda);
        omega *= 0.5;
        lambda *= 1.99;
    }
    d = clamp((1.0 - my) * 4.5 * cloud.density * d, 0.0, 1.0);
    float extra = 2.0 * max(0.0, 0.5 - my);
    return clamp(d + extra, 0.0, 1.0);
}

// world_to_medium_pt() - direct port of CloudMedium<T>::world_to_medium_pt()
// (src/shared/cloud_medium.h). `worldToMediumMat` is row-major, matching
// that function's own `mat[0]*wx + mat[1]*wy + mat[2]*wz + translate[0]`
// convention exactly.
inline float3 worldToMediumPoint(GpuCloudMedium cloud, float3 p) {
    float mx = cloud.worldToMediumMat[0]*p.x + cloud.worldToMediumMat[1]*p.y + cloud.worldToMediumMat[2]*p.z + cloud.worldToMediumTranslate[0];
    float my = cloud.worldToMediumMat[3]*p.x + cloud.worldToMediumMat[4]*p.y + cloud.worldToMediumMat[5]*p.z + cloud.worldToMediumTranslate[1];
    float mz = cloud.worldToMediumMat[6]*p.x + cloud.worldToMediumMat[7]*p.y + cloud.worldToMediumMat[8]*p.z + cloud.worldToMediumTranslate[2];
    return float3(mx, my, mz);
}

// Ray/AABB slab test against the medium's own `boundsMin`/`boundsMax`, in
// MEDIUM space, given the ray ALREADY transformed there (`mo`/`md` -
// direction transformed by the matrix only, no translation, matching
// CloudMedium<T>::sample_ray()'s own convention). Because the world-to-
// medium map is affine, world_point(t) = ray_o + t*ray_d maps to
// medium_point(t) = mo + t*md for the SAME t - so the tMin/tMax this
// returns are valid directly as world-space ray parameters along the
// ORIGINAL (world-space) ray direction, exactly like
// CloudMedium<T>::sample_ray()'s own returned segment. Returns false
// (ray misses the box entirely, or is degenerate) via `outTMin >
// outTMax`.
inline bool cloudAabbSlabIntersect(GpuCloudMedium cloud, float3 mo, float3 md,
                                    thread float& outTMin, thread float& outTMax) {
    float tMin = -1e30, tMax = 1e30;
    float moArr[3] = { mo.x, mo.y, mo.z };
    float mdArr[3] = { md.x, md.y, md.z };
    for (int i = 0; i < 3; ++i) {
        float bmin = cloud.boundsMin[i], bmax = cloud.boundsMax[i];
        if (abs(mdArr[i]) < 1e-12) {
            if (moArr[i] < bmin || moArr[i] > bmax) { outTMin = 1.0; outTMax = 0.0; return false; }
        } else {
            float invD = 1.0 / mdArr[i];
            float t0 = (bmin - moArr[i]) * invD;
            float t1 = (bmax - moArr[i]) * invD;
            if (t0 > t1) { float tmp = t0; t0 = t1; t1 = tmp; }
            tMin = max(tMin, t0);
            tMax = min(tMax, t1);
        }
    }
    outTMin = tMin;
    outTMax = tMax;
    return tMin <= tMax;
}

// E4/section 179: world_to_medium_pt() + ray/AABB slab test for
// GpuRgbGridMedium - same shape as worldToMediumPoint()/
// cloudAabbSlabIntersect() just above (a separate, parallel copy rather
// than a shared generic helper, matching gpu/optix/optix_intersection_
// sphere.h's own convention of a separate inline slab test per medium
// type "for consistency" - that file's own comment on box_slab_intersect).
inline float3 rgbGridWorldToMediumPoint(GpuRgbGridMedium grid, float3 p) {
    float mx = grid.worldToMediumMat[0]*p.x + grid.worldToMediumMat[1]*p.y + grid.worldToMediumMat[2]*p.z + grid.worldToMediumTranslate[0];
    float my = grid.worldToMediumMat[3]*p.x + grid.worldToMediumMat[4]*p.y + grid.worldToMediumMat[5]*p.z + grid.worldToMediumTranslate[1];
    float mz = grid.worldToMediumMat[6]*p.x + grid.worldToMediumMat[7]*p.y + grid.worldToMediumMat[8]*p.z + grid.worldToMediumTranslate[2];
    return float3(mx, my, mz);
}

inline bool rgbGridAabbSlabIntersect(GpuRgbGridMedium grid, float3 mo, float3 md,
                                      thread float& outTMin, thread float& outTMax) {
    float tMin = -1e30, tMax = 1e30;
    float moArr[3] = { mo.x, mo.y, mo.z };
    float mdArr[3] = { md.x, md.y, md.z };
    for (int i = 0; i < 3; ++i) {
        float bmin = grid.boundsMin[i], bmax = grid.boundsMax[i];
        if (abs(mdArr[i]) < 1e-12) {
            if (moArr[i] < bmin || moArr[i] > bmax) { outTMin = 1.0; outTMax = 0.0; return false; }
        } else {
            float invD = 1.0 / mdArr[i];
            float t0 = (bmin - moArr[i]) * invD;
            float t1 = (bmax - moArr[i]) * invD;
            if (t0 > t1) { float tmp = t0; t0 = t1; t1 = tmp; }
            tMin = max(tMin, t0);
            tMax = min(tMax, t1);
        }
    }
    outTMin = tMin;
    outTMax = tMax;
    return tMin <= tMax;
}

// Clamped voxel fetch, one flat channel block (nx*ny*nz floats) of the
// shared `rgbGridData` buffer - direct port of gpu/optix/
// optix_intersection_sphere.h's own gpu_rgb_grid_at().
inline float gpuRgbGridAt(device const float* d, int nx, int ny, int nz, int x, int y, int z) {
    // Out-of-range voxels read as ZERO (the lookup interpolates towards zero beyond the outermost voxel centres), as pbrt-v4's
    // SampledGrid, the CPU and OptiX do - clamping to the edge voxel made the grid denser at its border than they render it.
    if (x < 0 || x >= nx || y < 0 || y >= ny || z < 0 || z >= nz) return 0.0;
    return d[x + nx * (y + ny * z)];
}

// Trilinear lookup into one channel of a GpuRgbGridMedium's flat voxel
// data, px/py/pz in normalized [0,1]^3 medium space - direct port of
// gpu/optix/optix_intersection_sphere.h's own gpu_rgb_grid_trilinear().
inline float gpuRgbGridTrilinear(device const float* d, int nx, int ny, int nz,
                                  float px, float py, float pz) {
    float gx = px*nx - 0.5, gy = py*ny - 0.5, gz = pz*nz - 0.5;
    int ix0 = int(floor(gx)), iy0 = int(floor(gy)), iz0 = int(floor(gz));
    float fx = gx-ix0, fy = gy-iy0, fz = gz-iz0;
    float c000 = gpuRgbGridAt(d,nx,ny,nz, ix0,   iy0,   iz0);
    float c100 = gpuRgbGridAt(d,nx,ny,nz, ix0+1, iy0,   iz0);
    float c010 = gpuRgbGridAt(d,nx,ny,nz, ix0,   iy0+1, iz0);
    float c110 = gpuRgbGridAt(d,nx,ny,nz, ix0+1, iy0+1, iz0);
    float c001 = gpuRgbGridAt(d,nx,ny,nz, ix0,   iy0,   iz0+1);
    float c101 = gpuRgbGridAt(d,nx,ny,nz, ix0+1, iy0,   iz0+1);
    float c011 = gpuRgbGridAt(d,nx,ny,nz, ix0,   iy0+1, iz0+1);
    float c111 = gpuRgbGridAt(d,nx,ny,nz, ix0+1, iy0+1, iz0+1);
    float c00 = mix(c000, c100, fx);
    float c10 = mix(c010, c110, fx);
    float c01 = mix(c001, c101, fx);
    float c11 = mix(c011, c111, fx);
    float c0 = mix(c00, c10, fy);
    float c1 = mix(c01, c11, fy);
    return mix(c0, c1, fz);
}
