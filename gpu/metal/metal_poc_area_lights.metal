// metal_poc_area_lights.metal
// Area-light sampling: the LightSample result, image-textured disk / cylinder lights, sphere-light cone sampling, and sampleAreaLight().
// Split out of metal_poc_sampling.metal (pure code motion) for the code-size limit; concatenated in the order of metal_poc_shader_files.h.

// An AreaLightSource "filename" image is read with CLAMP, not repeat: pbrt's Image::Bilerp clamps its pixel coordinates (an imagemap Texture's default is repeat, which is what the shared
// textureSampler does). textured-twosided-lights read blue 5% high on Metal with the repeating sampler (its picture's edge blended with the opposite edge).
constexpr sampler areaLightImageSampler(coord::normalized, address::clamp_to_edge, filter::linear);

// The place of a light's picture in the atlas (scale u, scale v, offset u, offset v).
inline float4 areaLightPlace(AreaLight light) { return float4(light.imageScaleU, light.imageScaleV, light.imageOffsetU, light.imageOffsetV); }

// One picture of the atlas, read as pbrt reads an area light's image: with CLAMP, bilinear. Several lights may carry different pictures (a room's windows), stacked in one texture; a
// bilinear read at a picture's edge would blend in the pixels of its neighbour, so the coordinate is pulled in to the centre of the picture's own outermost pixels first, which is exactly
// what clamping gives (the edge pixel, on its own). A picture that fills the texture (place = 1, 1, 0, 0) reads as it always did.
inline float3 areaLightPicture(texture2d<float, access::sample> atlas, float2 uv, float4 place) {
    const float2 size = float2(float(atlas.get_width()), float(atlas.get_height()));
    const float2 edge = 0.5 / max(place.xy * size, float2(1.0));
    return atlas.sample(areaLightImageSampler, place.zw + place.xy * clamp(uv, edge, 1.0 - edge)).rgb;
}

// Picks one light from `lights`, power-proportionally (see AreaLight's own
// `pmf`/`aliasProb`/`aliasIndex` comment - replaces this POC's old uniform
// 1/lightCount picking), and samples a uniform point on its parallelogram -
// the "pick a light, then a point on it" step every NEE call site
// (Lambertian and GGX conductor both) shares verbatim; only what happens
// with the sampled point differs per BSDF. The picked light's own `pmf` is
// returned (not a flat 1/lightCount constant anymore, so it can no longer
// be folded in as a plain scalar the way the old comment here described) -
// every caller multiplies `ls.pmf` into its own area-to-solid-angle pdf
// expression instead.
struct LightSample {
    float3 point;
    float3 normal;
    float3 emission;
    float area;
    float pmf;
    // Mirrors AreaLight::twoSided below (section 104) - copied out of
    // the picked light here so every NEE call site's own cosLight check
    // can read it without a second lights[] lookup.
    float twoSided;
    // Mirrors AreaLight::spherePrimId (-1 for a quad light) - see that
    // field's own comment.
    int spherePrimId;
};

// Image-textured disk / cylinder area lights. pbrt maps the image over a disk with u = angle / 2pi (measured from the shape's object x axis towards
// y) and v = (R - r) / R, and over a cylinder with u = angle / 2pi and v = height fraction; image rows run top to bottom, so the texture is
// sampled at (u, 1 - v). The loader stores the object x axis in AreaLight::edgeV for a disk and in AreaLight::normal for a cylinder.
inline float2 diskLightTexCoord(AreaLight light, float3 p) {
    const float3 n = float3(light.normal);
    const float3 t = float3(light.edgeV);
    const float3 b = cross(n, t);
    const float3 rel = p - float3(light.center);
    const float px = dot(rel, t), py = dot(rel, b);
    float phi = (abs(px) + abs(py) < 1e-12) ? 0.0 : atan2(py, px);   // fast-math atan2(0, 0) is NaN
    if (phi < 0.0) phi += 2.0 * M_PI_F;
    const float radius = max(light.edgeU.x, 1e-6);
    const float r = clamp(sqrt(max(dot(rel, rel) - dot(rel, n) * dot(rel, n), 0.0)), 0.0, radius);
    return float2(phi * (0.5 / M_PI_F), r / radius);   // (u, 1 - v) with v = (R - r) / R
}

inline float2 cylinderLightTexCoord(AreaLight light, float3 p) {
    const float3 axis = float3(light.edgeU);
    const float3 t = float3(light.normal);
    const float3 b = cross(axis, t);
    const float3 rel = p - float3(light.center);
    const float px = dot(rel, t), py = dot(rel, b);
    float phi = (abs(px) + abs(py) < 1e-12) ? 0.0 : atan2(py, px);   // fast-math atan2(0, 0) is NaN
    if (phi < 0.0) phi += 2.0 * M_PI_F;
    const float v = clamp(dot(rel, axis) / max(light.edgeV.y, 1e-6), 0.0, 1.0);
    return float2(phi * (0.5 / M_PI_F), 1.0 - v);
}

// pbrt-v4's sphere light (Sphere::Sample(ctx, u), PDF(ctx, wi)): from a point OUTSIDE the sphere, sample the cone of directions it subtends instead of its surface. Uniform
// area sampling spends half its samples on the far side (no light) and weighs the rest by 1/cos, so a small bright sphere (radius 0.15, L 400) gave a few huge samples and many
// zeros: the same mean, but far more variance, which Film "maxcomponentvalue" turned into a darker picture (19% on maxcomponentvalue-firefly-clamp). Inside the sphere, or
// without a reference point, the caller keeps uniform area sampling. The pdf is solid angle: 1 / (2 pi (1 - cos(theta max))).
inline float sphereLightOneMinusCosMax(float radius, float distSq) {
    const float sin2 = radius * radius / distSq;
    return sin2 < 0.00068523f ? sin2 * 0.5f + sin2 * sin2 * 0.125f : 1.0f - sqrt(max(0.0f, 1.0f - sin2));
}
inline bool sphereLightSeenFrom(float3 center, float radius, float3 ref) {
    const float3 d = center - ref;
    return isfinite(ref.x) && dot(d, d) > radius * radius * 1.0001f;
}
// The solid-angle pdf of the cone sample (one light, before the light-selection pmf).
inline float sphereLightConePdf(float3 center, float radius, float3 ref) {
    const float3 d = center - ref;
    return 1.0f / (2.0f * M_PI_F * sphereLightOneMinusCosMax(radius, dot(d, d)));
}
inline void sampleSphereLightCone(float3 center, float radius, float3 ref, float2 u, thread float3& point, thread float3& normal) {
    const float3 toCenter = center - ref;
    const float d2 = dot(toCenter, toCenter);
    const float sin2Max = radius * radius / d2;
    const float sinMax = sqrt(sin2Max);
    float cosTheta, sin2Theta;
    if (sin2Max < 0.00068523f) {   // small angle: avoid 1 - cos cancelling
        sin2Theta = sin2Max * u.x;
        cosTheta = sqrt(1.0f - sin2Theta);
    } else {
        const float cosMax = sqrt(max(0.0f, 1.0f - sin2Max));
        cosTheta = (cosMax - 1.0f) * u.x + 1.0f;
        sin2Theta = 1.0f - cosTheta * cosTheta;
    }
    const float cosAlpha = sin2Theta / sinMax + cosTheta * sqrt(max(0.0f, 1.0f - sin2Theta / sin2Max));
    const float sinAlpha = sqrt(max(0.0f, 1.0f - cosAlpha * cosAlpha));
    const float phi = 2.0f * M_PI_F * u.y;
    float3 t, b;
    const float3 wc = toCenter * rsqrt(d2);
    buildOnb(wc, t, b);
    normal = -(sinAlpha * cos(phi) * t + sinAlpha * sin(phi) * b + cosAlpha * wc);
    point = center + radius * normal;
}

inline LightSample sampleAreaLight(device const AreaLight* lights, uint lightCount, thread uint& rngState,
                                    texture2d<float, access::sample> pbrtAreaLightTexture, sampler textureSampler, float3 refPoint = float3(INFINITY)) {
    // max(lightCount, 1u) guards the `- 1` below from underflowing (uint
    // wraps to 0xFFFFFFFF, not -1) if this were ever called on a 0-light
    // scene - not reachable with this POC's own hardcoded 2-light scene,
    // but every NEE call site calls this unconditionally with no count
    // check of its own, so this needs to be safe on its own terms.
    uint lastIdx = max(lightCount, 1u) - 1;
    // Vose alias-table sample (see PowerLightSampler::sample() in
    // src/shared/power_light_sampler_scaffold.h, ported verbatim): map u
    // into [0, lightCount), split into a slot index and its own
    // fractional remainder, then either accept that slot or fall through
    // to its precomputed alias - O(1) regardless of how skewed the
    // per-light probabilities are, unlike a running-sum/binary-search CDF
    // walk over `lights`.
    float scaled = randFloat(rngState) * float(lightCount);
    uint slot = min(uint(scaled), lastIdx);
    float frac = scaled - float(slot);
    uint idx = (frac < lights[slot].aliasProb) ? slot : lights[slot].aliasIndex;
    idx = min(idx, lastIdx);
    AreaLight light = lights[idx];
    float2 u = float2(randFloat(rngState), randFloat(rngState));
    LightSample result;
    float coneArea = 0.0f;
    if (light.kind > 0.5 && light.kind < 1.5) {
        // Sphere light (B14, section 184) - uniform-AREA sampling (not
        // pbrt-v4's own lower-variance cone-sampling, which would need
        // the shading point's own origin threaded through here and into
        // every one of this function's ~29 NEE call sites just to build
        // the cone's axis - see AreaLight::kind's own comment for why
        // this is the deliberately simpler, still fully unbiased choice:
        // every call site already reads back a generic (point, normal,
        // area, pmf) tuple and applies its OWN area-to-solid-angle
        // Jacobian, `light.area` already holds this sphere's true surface
        // area (4*pi*r^2, set by the scene builder), so that Jacobian
        // works correctly here unchanged - just higher variance than a
        // cone-sampled point would give, same "correct but not maximally
        // efficient" tradeoff pbrt's own generic Shape::Sample() fallback
        // makes for any shape lacking a specialized Sample(refPoint)).
        // Standard uniform-sphere-direction sampling (z uniform in
        // [-1,1], phi uniform in [0,2pi)) - the sampled direction IS the
        // point's own outward normal, scaled by radius and offset from
        // center gives the point itself.
        float z = 1.0 - 2.0 * u.x;
        float r = sqrt(max(0.0, 1.0 - z * z));
        float phi = 2.0 * M_PI_F * u.y;
        float3 dir = float3(r * cos(phi), r * sin(phi), z);
        float radius = light.edgeU.x;
        result.point = float3(light.center) + radius * dir;
        result.normal = dir;
        result.emission = float3(light.emission);
        if (sphereLightSeenFrom(float3(light.center), radius, refPoint)) {
            sampleSphereLightCone(float3(light.center), radius, refPoint, u, result.point, result.normal);
            // the callers turn an area into a solid-angle pdf as distSq / (area * cos): hand them the area that gives the cone's pdf
            const float3 toPoint = result.point - refPoint;
            const float distSq = dot(toPoint, toPoint);
            const float cosL = max(abs(dot(result.normal, -toPoint * rsqrt(distSq))), 1e-6f);
            coneArea = distSq / (cosL * sphereLightConePdf(float3(light.center), radius, refPoint));
        }
    } else if (light.kind > 1.5 && light.kind < 2.5) {
        // Disk light (pbrt "disk" AreaLightSource): uniform-area sampling of a flat disk.
        // center = disk centre, edgeU.x = radius, normal = the emitting side's normal,
        // area = pi*r^2. r = R*sqrt(u), phi = 2*pi*v is exactly uniform over the disk.
        float3 n = float3(light.normal);
        float3 t, b;
        if (light.useTexture > 0.0) { t = float3(light.edgeV); b = cross(n, t); }   // the image's own angle origin
        else buildOnb(n, t, b);
        float rr = light.edgeU.x * sqrt(u.x);
        float ph = 2.0 * M_PI_F * u.y;
        result.point = float3(light.center) + rr * (cos(ph) * t + sin(ph) * b);
        result.normal = n;
        result.emission = (light.useTexture > 0.0)
            ? areaLightPicture(pbrtAreaLightTexture, float2(ph * (0.5 / M_PI_F), rr / max(light.edgeU.x, 1e-6)), areaLightPlace(light)) * float3(light.emission)
            : float3(light.emission);
    } else if (light.kind > 2.5 && light.kind < 3.5) {
        // Cylinder light (pbrt "cylinder", lateral surface only): uniform-area sampling.
        // center = base centre, edgeU = unit axis, edgeV.x = radius, edgeV.y = height,
        // area = 2*pi*r*h. The sampled normal is the OUTWARD radial direction.
        float3 axis = float3(light.edgeU);
        float3 t, b;
        if (light.useTexture > 0.0) { t = float3(light.normal); b = cross(axis, t); }   // the image's own angle origin
        else buildOnb(axis, t, b);
        float ph = 2.0 * M_PI_F * u.y;
        float3 radial = cos(ph) * t + sin(ph) * b;
        result.point = float3(light.center) + axis * (light.edgeV.y * u.x) + radial * light.edgeV.x;
        result.normal = radial;
        result.emission = (light.useTexture > 0.0)
            ? areaLightPicture(pbrtAreaLightTexture, float2(ph * (0.5 / M_PI_F), 1.0 - u.x), areaLightPlace(light)) * float3(light.emission)
            : float3(light.emission);
    } else if (light.kind > 3.5) {
        // Triangle light (one triangle of a non-quad emissive mesh, e.g. a fan): center =
        // vertex 0, edgeU/edgeV = the two edges from it, normal = the winding normal,
        // area = half the parallelogram. Fold the unit square onto the triangle.
        float2 uu = u;
        if (uu.x + uu.y > 1.0) { uu = float2(1.0 - uu.x, 1.0 - uu.y); }
        result.point = float3(light.center) + uu.x * float3(light.edgeU) + uu.y * float3(light.edgeV);
        result.normal = float3(light.normal);
        // a picture on the mesh: read where the mesh's own uv put this point (vertex 0 weighs 1 - u - v, vertex 1 u, vertex 2 v: the same barycentrics as the point above)
        result.emission = (light.useTexture > 0.0)
            ? areaLightPicture(pbrtAreaLightTexture, (1.0 - uu.x - uu.y) * float2(light.triU0, light.triV0) + uu.x * float2(light.triU1, light.triV1) + uu.y * float2(light.triU2, light.triV2),
                               areaLightPlace(light)) * float3(light.emission)
            : float3(light.emission);
    } else {
        float3 edgeU = float3(light.edgeU);
        float3 edgeV = float3(light.edgeV);
        result.point = float3(light.center) - 0.5 * edgeU - 0.5 * edgeV + u.x * edgeU + u.y * edgeV;
        result.normal = float3(light.normal);
        // Patterned emission (see AreaLight's own comment): the SAME (u.x,
        // u.y) this NEE sample point was just built from doubles as the
        // pattern's own UV coordinate, no separate UV needed. `patternScale
        // <= 0.0` (every light before this one) skips this entirely,
        // reproducing flat `light.emission` exactly.
        result.emission = (light.useTexture > 0.0)
            ? areaLightPicture(pbrtAreaLightTexture, u, areaLightPlace(light)) * float3(light.emission)
            : (light.patternScale > 0.0)
                ? checkerColor(u, light.patternScale, float3(light.emission), float3(light.emission) * light.patternTileB)
                : float3(light.emission);
    }
    result.area = coneArea > 0.0f ? coneArea : light.area;
    result.pmf = light.pmf;
    result.twoSided = light.twoSided;
    result.spherePrimId = light.spherePrimId;
    return result;
}
