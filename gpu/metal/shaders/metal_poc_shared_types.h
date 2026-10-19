// metal_poc_shared_types.h -- the structs the host fills and the shader reads: ONE definition, included by both.
//
// They used to be written twice by hand (the shader's copy in metal_poc_types.metal, the host's in metal_poc_gpu_types.h / metal_poc_host_math.h) with "mirrors ... byte-for-byte" comments,
// kept in step by a layout probe (metal_poc_shader_tests_layout.mm) and by review; the shader unit tests kept a THIRD copy of the light struct, which fell behind when a field was
// added (the sampling tests then read garbage). The same text is valid C++ (the host) and Metal Shading Language (the shader), as metal_poc_material_ids.metal already is for the
// material names: it is the second file of the concatenated shader source (metal_poc_shader_files.h) and is #included by the host.
//
// Rules for what goes in here: plain data only (floats, ints, PackedFloat3, arrays of them), no host-only members, a field is added at the END, and a field that has a sensible
// default says so (the host builds these structs with aggregate initialisation and default member initialisers; the shader only ever reads them). The host names are the struct names;
// the shader's older names are typedefs at the bottom.
#ifndef METAL_POC_SHARED_TYPES_H
#define METAL_POC_SHARED_TYPES_H

#ifdef __METAL_VERSION__
#include <metal_stdlib>
using namespace metal;
// the host's name for a 12-byte vector (no padding: it sits between scalars in these structs)
typedef packed_float3 PackedFloat3;
#else
#include <cstdint>
// A vector of three floats with NO padding (simd::float3 is 16 bytes; the shader's packed_float3 is 12, and so is this). Constructors so that the shared default member initialisers can be
// written PackedFloat3(x, y, z), which is valid on both sides; brace initialisation PackedFloat3{x, y, z} on the host works as it always did.
struct PackedFloat3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
    constexpr PackedFloat3() = default;
    constexpr PackedFloat3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
};
#endif

// A real light LIST entry, replacing the single hardcoded kLightCenter/
// kLightHalfExtents/kLightNormal/kLightArea/kLightEmission constants
// steps 4/5's own comments explicitly flagged as "one hardcoded light's
// shape baked into the integrator... a real port would carry a proper
// light list instead." A general parallelogram (center + two full edge
// vectors, not axis-aligned half-extents) rather than the old x/z-only
// shape - edgeU/edgeV/normal/area are precomputed host-side once, not
// re-derived per shading sample.
struct AreaLightData {
    PackedFloat3 center;
    PackedFloat3 edgeU;
    PackedFloat3 edgeV;
    PackedFloat3 normal;
    float area;
    PackedFloat3 emission;
    // Spatially-varying emission via the SAME checkerboard idea
    // materialType 6 already uses for albedo (checkerColor(), tile A is
    // `emission` above, tile B a fixed fraction of it) - `patternTileB`
    // is that fraction; 0.0 (every light before this one) means "tile B
    // is pure black," which combined with `patternScale <= 0.0` below
    // instead SKIPPING the pattern entirely reproduces flat, uniform
    // emission exactly - purely additive, the same "0 is an exact no-op"
    // shape this POC's own knobs already use.
    float patternTileB = 0.0f;
    // Tiles per 0-1 UV unit - reuses the light's OWN NEE sample point
    // (`u.x, u.y` in sampleAreaLight(), the same planar 0-1
    // parameterization addQuad()'s own UVs already establish for this
    // exact quad) as the pattern's own UV, needing no new per-light UV
    // data at all. <= 0.0 means "no pattern," see above.
    float patternScale = 0.0f;
    // pbrt-v4's own "bool twosided" AreaLightSource parameter (section
    // 104) - 0.0 (every light before this one) means this light only
    // emits from the side its own `normal` points toward, matching this
    // POC's original one-sided-only behaviour exactly; nonzero accepts
    // BOTH signs of cosLight in every NEE call site's own visibility
    // check, and the direct-hit code's own facing check (mirrored onto
    // TriangleMaterial::twoSided there, not read from here, so it also
    // covers a non-quad/disk emissive shape with no AreaLight entry).
    float twoSided = 0.0f;
    // A pbrt-loaded scene's own image-based AreaLightSource
    // ("string filename", section 105) - 0.0 (every light before this
    // one) keeps `emission` a flat, direct radiance value; nonzero
    // means `emission` instead holds a pure (scale,scale,scale)
    // MULTIPLIER (pbrt-v4 semantics: image pixel value * scale, no
    // meaningful "L" once an image is given - DiffuseAreaLight ignores
    // L entirely once "filename" is set), and sampleAreaLight() samples
    // pbrtAreaLightTexture at its own NEE sample point's (u.x, u.y)
    // instead of using `emission`/the checker pattern directly. Only
    // ONE textured light is supported (same "one shared slot" tier as
    // sections 90/98's own single-image texture slots) and only for a
    // QUAD-shaped light (this loader's own disk primitive has no UV
    // parameterization to sample a real image against at all, unlike a
    // quad's own addQuad()-assigned UVs) - a disk-shaped textured light
    // still falls back to flat L, a real, separate, still-open gap.
    float useTexture = 0.0f;
    // Power-proportional light-picking data, host-computed once by
    // metal_poc.mm's buildPowerLightSampler() (a direct port of
    // src/shared/power_light_sampler_scaffold.h's own PowerLightSampler -
    // see that function's own comment for the full "why"). `pmf` is this
    // light's own overall selection probability (`power[i] / totalPower`),
    // used directly by both sampleAreaLight() and the direct-hit MIS
    // branch below in place of this POC's old flat `1.0 / lightCount`.
    // `aliasProb`/`aliasIndex` are this light's own Vose alias-table slot
    // (threshold + fallback index) - together they let sampleAreaLight()
    // pick a power-weighted light index in O(1), no loop or running-sum
    // search over `lights` needed despite the non-uniform probabilities.
    float pmf = 0.0f;
    float aliasProb = 1.0f;
    uint32_t aliasIndex = 0;
    // 0.0 (every light before this one) = planar quad, using edgeU/edgeV/
    // normal/area as they always have. 1.0 = a sphere light (B14, section
    // 184) - `edgeU.x` reused as the sphere's own radius (edgeV/normal
    // unused: a sphere has no single fixed plane/normal the way a quad
    // does; `area` still means "total light-source area", here
    // `4*pi*radius^2`, so buildPowerLightSampler()'s own `luminance*area`
    // power estimate and every NEE call site's own generic area-to-solid-
    // angle Jacobian (`distSq/(area*cosLight)`) both work UNCHANGED - only
    // sampleAreaLight()'s own POINT/NORMAL sampling and the direct-hit MIS
    // branch's own per-hit normal (metal_poc_kernel.metal - a sphere's
    // normal varies per-point, unlike a quad's fixed one) need to branch
    // on this at all.
    // kind values: 0 quad, 1 sphere (above), 2 disk (center, edgeU.x = radius, normal, area = pi r^2),
    // 3 cylinder lateral surface (center = base, edgeU = unit axis, edgeV = (radius, height),
    // area = 2 pi r h; sampled normal is outward-radial), 4 triangle (center = vertex 0,
    // edgeU/edgeV = edges from it, normal = winding normal, area = half the parallelogram).
    // All sampled uniformly by area, so every NEE call site's area-to-solid-angle Jacobian is
    // unchanged. See sampleAreaLight().
    float kind = 0.0f;
    // A sphere light's own index into `spheres[]`/`sphereMaterials[]` (-1
    // for a quad light, which isn't in that array at all) - lets a shadow
    // ray toward this light exclude its OWN primitive outright
    // (SpherePayload::shadowIgnorePrimId's own comment) instead of
    // relying purely on a distance epsilon, which a curved surface makes
    // unreliable.
    int spherePrimId = -1;
    // The light's picture: its place in the shared picture atlas (scale u, scale v, offset u, offset v) and, for a triangle light, the texture coordinates of its three vertices
    // (mirrors AreaLightData in metal_poc_host_math.h).
    float imageScaleU = 1.0f;
    float imageScaleV = 1.0f;
    float imageOffsetU = 0.0f;
    float imageOffsetV = 0.0f;
    float triU0 = 0.0f;
    float triV0 = 0.0f;
    float triU1 = 1.0f;
    float triV1 = 0.0f;
    float triU2 = 1.0f;
    float triV2 = 1.0f;
};

// A true DELTA light - zero-area, zero-solid-angle, unlike every AreaLight
// above. Fundamentally simpler to importance-sample than an area light,
// not just a smaller version of one: a delta light has probability
// EXACTLY zero of ever being hit by a BSDF-sampled continuation ray (the
// same "measure zero" reasoning the mirror/dielectric materials already
// use to skip NEE for THEIR own delta lobes, mirrored here from the
// light's side instead of the material's), so there is no competing
// BSDF-sampling strategy to weight against at all - every point light's
// own NEE contribution is added at full weight, unconditionally, with no
// power-heuristic MIS blend and no area-to-solid-angle pdf conversion
// (a delta light's own "pdf" only ever appears as an idealized 1/distSq
// falloff, not a real probability density needing normalization the way
// a light-PICKING probability or an area-sampling pdf does). Point lights
// are NOT part of the `lights[]` array/light-picking scheme above at
// all - since there is no variance-reduction benefit to stochastically
// picking among a handful of always-fully-weighted point lights the way
// there is for choosing among many area lights, every point light's
// contribution is simply summed each bounce instead (see the shading
// loop's own point-light loop below).
// `direction`/`cosOuterAngle`/`cosInnerAngle` turn this into a SPOT light
// when set (Blinn/pbrt-v4's own smoothstep cone falloff between the two
// angles - full emission inside the inner cone, smoothly fading to zero
// at the outer one, not a hard-edged cone) - `cosOuterAngle <= -1.0`
// (every point light added before this one, and this struct's own
// default) means "omnidirectional, not a spot," skipping the falloff
// computation entirely rather than risking a degenerate `smoothstep`
// with equal edges. A spot light is still fundamentally the SAME delta
// light PointLight already is (zero area, no MIS/pdf needed, summed
// unconditionally, never picked) - the cone is a multiplicative falloff
// on its own emission, not a different light TYPE requiring different
// integration math.
struct PointLightData {
    PackedFloat3 position;
    PackedFloat3 emission;
    PackedFloat3 direction = PackedFloat3(0.0f, -1.0f, 0.0f);
    float cosOuterAngle = -1.0f;
    float cosInnerAngle = -1.0f;
};

// A directional ("sun") light - like PointLight, a true delta light
// (zero solid angle, always full NEE weight, no MIS, no pdf conversion),
// but positioned at infinity rather than at a finite point: every shading
// point sees the SAME fixed incoming direction, and there is no 1/distSq
// falloff at all (a real sun's own distance makes that falloff
// imperceptibly close to constant across any scene-sized region) - the
// limit of a point light as distance goes to infinity and emission grows
// to compensate, not a separate kind of light needing new integration
// theory. `direction` is the direction the light itself travels (from
// the sun toward the scene), matching PointLight's own `direction`
// convention for the spot cone.
//
// Originally shipped with fog attenuation skipped entirely for this
// light (unlike PointLight's own shadow ray, which has a known finite
// distance `exp(-fogSigmaT * dist)` can use directly) - a directional
// light's shadow ray has no target distance at all, the light being at
// infinity. Fixed below (`rayBoxExitDistance()`) rather than left as a
// permanent gap: since this scene's own fog fills exactly the room's
// solid geometry, and an UNOCCLUDED shadow ray (by definition, this
// branch only runs when the real scene intersection test found nothing)
// can only have exited through this room's one gap (the open front),
// the distance to where that ray crosses this room's own `[-1,1]^3`
// bounds is exactly the real fog path length - not an arbitrary
// sentinel standing in for one.
struct DirectionalLightData {
    PackedFloat3 direction;
    PackedFloat3 emission;
};

// A "slide projector" light (pbrt-v4's own ProjectionLight, src/shared/
// projection_light.h section 12.5) - a FOURTH delta light type alongside
// PointLight/spot/DirectionalLight, same "summed unconditionally every
// bounce, never picked, no MIS/pdf needed" integration shape every
// earlier delta light already established. Unlike a spot light's own
// smooth cone falloff (a single scalar), this one projects an actual
// IMAGE through a perspective frustum - a real gobo/slide-projector
// effect, not just a differently-shaped intensity curve. Reuses the
// scene's own already-loaded `earthTexture` (bound at texture(1) since
// this POC's very first texture-mapping PR) as the projected image
// rather than needing a second texture binding - a world map projected
// onto a wall/floor like a slide projector, not a new asset.
// `right`/`up`/`forward` are a precomputed (host-side, once)
// orthonormal light-space basis - the same "precompute once, don't
// re-derive per shading sample" approach AreaLight's own edgeU/edgeV/
// normal already uses. `tanHalfFovX`/`tanHalfFovY` fold the frustum's
// half-angle AND aspect ratio into two scalars (rather than a separate
// fov + aspect the way pbrt-v4's own screenBounds derivation does),
// since this POC only ever needs the two independent screen-space
// extents, not pbrt-v4's own more general aspect>=1-vs-aspect<1 code
// path.
struct ProjectionLightData {
    PackedFloat3 position;
    PackedFloat3 forward;
    PackedFloat3 right;
    PackedFloat3 up;
    float tanHalfFovX;
    float tanHalfFovY;
    float scale;
    // Mirrors metal_poc.mm's own ProjectionLightData::usePbrtTexture
    // comment (section 98) - selects pbrtProjectionTexture instead of
    // the room's own shared earthTexture at every call site below.
    uint32_t usePbrtTexture = 0;
};

// A "goniometric" light (pbrt-v4's own GoniometricLight, src/shared/
// goniometric_light.h) - a FIFTH delta light type, modeling a real IES
// photometric profile: unlike a spot light's own single symmetric cone,
// a genuine light fixture's intensity can vary in complex, non-radially-
// symmetric ways (real IES files often show scalloped, multi-lobed, or
// asymmetric distributions) - captured here as a 2D image indexed by
// direction (via equalAreaSphereToSquare()), the same "the image itself
// IS the directional intensity function" idea projectionLightRadiance()
// already uses, just addressed by DIRECTION FROM the light instead of
// a perspective-projected UV onto a distant plane. No real IES data file
// exists in this repo, so `image` here is a small PROCEDURALLY generated
// pattern (metal_poc.mm's own buildGoniometricProfileImage()) rather
// than a new binary asset - concentric rings of varying brightness
// around the light's own forward axis, chosen specifically because it's
// a directional pattern a plain scalar cone falloff (spotLightFalloff())
// could never produce, the same "prove this is doing something a
// simpler existing light couldn't" reasoning every earlier delta light
// increment already used.
struct GoniometricLightData {
    PackedFloat3 position;
    PackedFloat3 forward;
    PackedFloat3 right;
    PackedFloat3 up;
    PackedFloat3 emission;
    float scale;
    // Mirrors ProjectionLight::usePbrtTexture above - selects
    // pbrtGoniometricTexture instead of the room's own shared
    // goniometricTexture.
    uint32_t usePbrtTexture = 0;
};

// Mirrors metal_poc.mm's own GpuLensElementData byte-for-byte.
struct GpuLensElementData {
    float curvatureRadius;
    float thickness;
    float eta;
    float apertureRadius;
};

// Mirrors metal_poc.mm's own GpuExitPupilBoundsData byte-for-byte.
struct GpuExitPupilBoundsData {
    float xMin, xMax, yMin, yMax;
    uint32_t degenerate;
};

// A sphere is a custom (non-triangle) primitive - Metal has no built-in
// sphere intersection the way it does for triangles, so this needs an
// explicit bounding-box geometry + an intersection function (below) to
// tell the intersector how to test a ray against one. center/radius is
// the whole shape; material lives in a separate 1:1-indexed array (own
// buffer, not embedded here) purely so sphereIntersectionFunction - which
// only needs geometry, never material - doesn't have to carry material
// data through the intersection-function boundary at all.
struct SphereData {
    PackedFloat3 center;
    float radius;
    // Object motion blur (F11, section 167): pbrt's ActiveTransform
    // "StartTime"/"EndTime" bakes a SECOND centre into pbrt_flatten.h's
    // own Sphere::center1 - this is that second centre, stored as a
    // DELTA (center1 - center) rather than an absolute point specifically
    // so every non-moving sphere across every OTHER scene (every
    // `SphereData{center, radius}` 2-field brace-init already in this
    // file/metal_poc.mm) keeps working with zero code changes: C++
    // aggregate-init value-initializes an omitted trailing member to
    // (0,0,0), and centerDelta1==0 makes the interpolation below a
    // provable no-op for any shutterT, the same "safe by construction"
    // property gpu/optix/optix_intersection_sphere.h's own
    // center/center1 pair relies on (that file's own comment).
    PackedFloat3 centerDelta1 = PackedFloat3(0, 0, 0);
    // The rows of the matrix that carries a world direction into the frame this renderer's older sphere (u, v) are defined in (see metal_poc_gpu_types.h).
    PackedFloat3 uvRot0 = PackedFloat3(1, 0, 0);
    PackedFloat3 uvRot1 = PackedFloat3(0, 1, 0);
    PackedFloat3 uvRot2 = PackedFloat3(0, 0, 1);
};

// A second, genuinely DIFFERENT custom-primitive shape - not another
// sphere (spheres already prove "N primitives sharing ONE intersection
// function", via boundingBoxCount and a shared function/buffer, since
// step 3/PR #4). Every custom primitive in this POC before now has gone
// through function-table SLOT 0 (every geometry's own
// intersectionFunctionTableOffset was 0). This disk goes through slot 1
// instead, next to the spheres' geometry within the SAME primitive
// acceleration structure (metal_poc.mm's own sphereAccelDesc now has TWO
// geometryDescriptors, not one) - the first time this POC's function
// table actually has more than one distinct entry, and the first time
// `intersection_result::geometry_id` (not just primitive_id) matters for
// telling two hits apart.
struct DiskData {
    PackedFloat3 center;
    PackedFloat3 normal;
    float radius;
};

// A third custom-primitive shape (section 171) - a FULL (no partial
// azimuthal sweep, no motion blur) finite cylinder, the same scope cut
// DiskData's own "full circle only" comment already established for
// disks. Stored fully baked in WORLD SPACE (base point + unit axis +
// radius + height), not as an object-space shape plus a per-primitive
// affine transform the way gpu/optix/optix_types.h's own CylinderData
// does - Metal's intersection-function mechanism has no per-primitive
// instance-transform plumbing the way OptiX's optixGetWorldToObject
// TransformMatrix() does (InstanceTransform, elsewhere in this file, is
// a POC-wide side-channel for the couple of actually-INSTANCED objects,
// not a general per-primitive facility), so baking directly into world
// space at LOAD time (metal_poc.mm's own loadPbrtCylinders(), mirroring
// loadPbrtDisks()'s identical "transform once at load, not per-ray"
// choice) avoids needing that machinery at all - a world-space axis-
// aligned-to-nothing-in-particular tube test is no harder to write than
// an object-space one, and this shape has rotational symmetry around
// its own axis (no reference "phi=0" direction to preserve, unlike a
// partial sweep would need), so there's no orientation info lost by
// working in world space directly.
struct CylinderData {
    PackedFloat3 base;   // world-space point at the axis's zMin end
    PackedFloat3 axis;   // world-space UNIT direction, base -> top
    float radius;         // world-space radius
    float height;         // world-space axis length (top = base + axis*height)
};

// Mirrors MTLPackedFloat4x3's own layout (4 packed_float3 COLUMNS, no
// padding) byte-for-byte - a plain side-channel buffer of per-instance
// transforms, one entry per `instanceDescs[]` slot on the host side,
// indexed here by `intersection_result::instance_id`. Metal's own
// `intersection_result<instancing, ...>` type does NOT expose the
// instance's object-to-world transform as a queryable field (confirmed
// by probing the compiler directly - `object_to_world_transform` and
// several other plausible names all fail to compile, "no member
// named..."), unlike OptiX's `optixGetWorldToObjectTransformMatrix()`
// equivalent. This buffer is this POC's own stand-in: uploaded with
// EXACTLY the same transform values used to build each instance
// descriptor's own `transformationMatrix` (see metal_poc.mm's
// addTransformedSuzanneInstance()), so the two can't drift apart from
// each other by construction.
struct InstanceTransform {
    PackedFloat3 col0;
    PackedFloat3 col1;
    PackedFloat3 col2;
    PackedFloat3 col3;
    uint32_t triBase = 0;   // see InstanceTransform in metal_poc_gpu_types.h
};

// E2/section 178: a real heterogeneous, procedural Perlin-FBm-density
// medium (pbrt-v4 CloudMedium, src/shared/cloud_medium.h) - ported from
// gpu/optix/optix_intersection_sphere.h's own gpu_cloud_density()/
// delta-tracking closest-hit code, the hand-duplicated no-wispiness GPU
// variant BOTH GPU backends now use (OptiX's own comment there explains
// why calling CloudMedium<T>::compute_density() directly device-side is
// unsafe on that backend - not applicable to Metal's very different
// compute-kernel-loop architecture, but matched anyway for GPU-backend
// parity rather than re-deriving a Metal-only variant). A separate
// buffer (not folded into TriangleMaterial the way materialType 28's
// homogeneous medium reuses color/ior/roughness directly) because this
// needs far more state (a 3x3 affine world-to-medium transform) than
// TriangleMaterial's remaining spare fields could hold - materialType 29
// stores only an index into this buffer (TriangleMaterial::conductorEta.x,
// cast to uint - unused by any real conductor, the same spare-field-reuse
// convention every other materialType already follows) plus its own
// albedo (`color`) and phase_g (`roughness`), matching how OptiX's own
// MaterialData redundantly caches both alongside cloudMediumIdx.
struct GpuCloudMedium {
    float boundsMin[3];
    float boundsMax[3];
    // world_to_medium(p) = worldToMediumMat * p + worldToMediumTranslate
    // (row-major 3x3, matching CloudMedium<T>::world_to_medium_pt()).
    float worldToMediumMat[9];
    float worldToMediumTranslate[3];
    float sigmaA;
    float sigmaS;
    float density;
    float wispiness;   // unread by gpuCloudDensity() (GPU variant has no
                        // wispiness term) - kept only so this struct stays
                        // a byte-for-byte mirror of CloudMedium<T>'s own
                        // constructor argument list, for readability.
    float frequency;
};

// E4/section 179: a real heterogeneous per-voxel R/G/B scattering grid
// (pbrt-v4 RGBGridMedium, src/shared/rgb_nebula_generator.h's own
// generator) - the flat GPU metadata analog of CPU's `RGBGridMediumData`
// (which can't be used device-side directly, same reason
// `CloudMedium<T>` can't - see `GpuCloudMedium`'s own comment). Ported
// from `gpu/optix/optix_types.h`'s own `GpuRgbGridMedium` (materialType
// 29's own OptiX precedent), which documents the SAME deliberate
// simplification kept here: a single GLOBAL majorant (`sigmaMaj`)
// instead of CPU's real per-voxel DDA majorant grid, to keep delta
// tracking algorithmically simple. No `leDataOffset`/`Le_scale`
// (per-voxel emission) fields - E4's own scene never sets any (a pure
// scattering-only nebula), so this struct omits what OptiX's own
// version carries for scenes this port doesn't need.
struct GpuRgbGridMedium {
    // Medium-space unit cube [0,1]^3, NOT a world-space AABB (unlike
    // OptiX's own same-named fields) - always {0,0,0}/{1,1,1} in
    // practice, matching GpuCloudMedium::boundsMin/boundsMax's own
    // convention, since rgbGridAabbSlabIntersect() (metal_poc_sampling.
    // metal) compares these directly against a ray already transformed
    // into medium space.
    float boundsMin[3];
    float boundsMax[3];
    float worldToMediumMat[9];
    float worldToMediumTranslate[3];
    int nx, ny, nz;
    // Element offset into the shared flat `rgbGridData` buffer - the R
    // channel block starts here, G at +nx*ny*nz, B at +2*(nx*ny*nz).
    int dataOffset;
    float sigmaScale;
    float sigmaMaj;
    float phaseG;
    // Constant absorption coefficient added to every channel (already in this scene's distance units). pbrt's
    // RGBGridMedium defaults sigma_a to 1 when the scene gives no sigma_a grid; 0 = pure scattering (old behaviour).
    float sigmaAConst;
    // Element offset of the per-voxel sigma_a blocks (R, then G at +nx*ny*nz, B at +2*nx*ny*nz) in `rgbGridData`; -1 when
    // the scene gave no "rgb sigma_a" (then sigmaAConst applies to every channel).
    int saDataOffset;
    // Per-voxel emission ("rgb Le"): element offset of the R, G, B blocks in `rgbGridData`, or -1 for none; leScale is pbrt's "Lescale".
    int leDataOffset;
    float leScale;
};

#ifdef __METAL_VERSION__
// the shader's own names for the structs whose host name differs
typedef AreaLightData AreaLight;
typedef PointLightData PointLight;
typedef DirectionalLightData DirectionalLight;
typedef ProjectionLightData ProjectionLight;
typedef GoniometricLightData GoniometricLight;
typedef GpuLensElementData LensElement;
typedef GpuExitPupilBoundsData ExitPupilBounds;
#endif

#endif  // METAL_POC_SHARED_TYPES_H
