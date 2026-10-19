// metal_poc_pbrt_loader_punctual.mm
// MetalPocApp::loadPbrtPunctualLights() and the two image-light helpers it uses: a pbrt scene's point, spot, distant, goniometric and projection lights.
// Split out of metal_poc_pbrt_loader.mm (pure code motion) so that file, the most-changed one on the Mac side, stays well under the code-size limit.
#import <Metal/Metal.h>
#import <Foundation/Foundation.h>
#include "metal_poc_app.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>

// A goniometric light with a profile image: the first such light of a scene gets the one dedicated texture slot; false if this one cannot (an image that does not read or decode), and the caller draws it as a point light.
bool PbrtSceneLoader::addGoniometricImageLight(const pbrt_flatten::PunctualLight& pl, const PbrtToWorldFn& toWorld, const float3& emission) {
        std::string bytes;
        if (pbrt_load::loadFileNear(pbrtScenePath, pl.filename, bytes) &&
            pbrt_load::detail::decodeInfiniteLightImage(pl.filename, bytes,
                pbrtGoniometricImagePixels, pbrtGoniometricImageWidth, pbrtGoniometricImageHeight)) {
            havePbrtGoniometricImage = true;
            const float3 pos = toWorld(float3{(float)pl.pos[0], (float)pl.pos[1], (float)pl.pos[2]});
            // Same equal-area octahedral mapping the hardcoded
            // room's own goniometric light already uses
            // (equalAreaSphereToSquare(), section 57) - a
            // right/up frame is needed to express "local"
            // direction the same way. A real IES profile can
            // have genuine azimuthal (non-rotationally-
            // symmetric) variation, so the roll matters here
            // too, not just for Projection below - using the
            // scene's own real worldToLight-derived up
            // (punctualLightWorldUp(), section 98) as the
            // worldUp hint into the SAME cross-product formula
            // makeGoniometricLight() already uses recovers
            // that real roll instead of guessing at it.
            const float3 forward = punctualLightWorldForward(pl.worldToLight);
            const float3 worldUpHint = punctualLightWorldUp(pl.worldToLight);
            const float3 right = simd::normalize(simd::cross(forward, worldUpHint));
            const float3 up = simd::cross(right, forward);
            goniometricLights.push_back(GoniometricLightData{
                PackedFloat3{pos.x, pos.y, pos.z},
                PackedFloat3{forward.x, forward.y, forward.z},
                PackedFloat3{right.x, right.y, right.z},
                PackedFloat3{up.x, up.y, up.z},
                PackedFloat3{emission.x, emission.y, emission.z},
                // pl.scale is ALREADY folded into `emission`
                // above (baseEmission = intensity*pl.scale,
                // see this function's own top-of-loop
                // comment) - GoniometricLightData::scale is
                // an INDEPENDENT multiplier the shader applies
                // on top (the hardcoded room's own light,
                // above, passes emission=I with no pl.scale
                // baked in and a real scale=1.0f here);
                // passing pl.scale a second time here would
                // double-count it.
                /*scale=*/1.0f,
                /*usePbrtTexture=*/1u});
            return true;
        }
        fprintf(stderr, "loadPbrtScene: goniometric light's profile image '%s' could not be "
                        "read/decoded; falling back to the Approx (uniform) case\n", pl.filename.c_str());
    return false;
}

// A projection light with a slide image (same rules as the goniometric one); false falls back to the uniform beam.
bool PbrtSceneLoader::addProjectionImageLight(const pbrt_flatten::PunctualLight& pl, const PbrtToWorldFn& toWorld, float intensityScale) {
        std::string bytes;
        if (pbrt_load::loadFileNear(pbrtScenePath, pl.filename, bytes) &&
            pbrt_load::detail::decodeInfiniteLightImage(pl.filename, bytes,
                pbrtProjectionImagePixels, pbrtProjectionImageWidth, pbrtProjectionImageHeight)) {
            havePbrtProjectionImage = true;
            // pbrt-v4 builds the light's frame with a y flip (ProjectionLight::Create, `renderFromLight * Scale(1, -1, 1)`): the slide's first row is at the TOP
            // of what the light projects, so the rows are stored upside down for the lookup, as the CPU's loader does (a bright band at the top of the slide
            // lit the bottom of the floor on the CPU and the top on Metal).
            for (int row = 0; row < pbrtProjectionImageHeight / 2; ++row) {
                float* a = &pbrtProjectionImagePixels[(size_t)row * pbrtProjectionImageWidth * 3];
                float* b = &pbrtProjectionImagePixels[(size_t)(pbrtProjectionImageHeight - 1 - row) * pbrtProjectionImageWidth * 3];
                std::swap_ranges(a, a + (size_t)pbrtProjectionImageWidth * 3, b);
            }
            const float3 pos = toWorld(float3{(float)pl.pos[0], (float)pl.pos[1], (float)pl.pos[2]});
            const float3 forward = punctualLightWorldForward(pl.worldToLight);
            // Same look-at-style right/up derivation
            // makeProjectionLight() already uses for the
            // hardcoded room's own light, but with the
            // scene's own REAL worldToLight-derived up
            // (punctualLightWorldUp(), section 98) as the
            // worldUp hint instead of a generic axis - a
            // projected slide has a real, meaningful roll
            // (the image's own up direction), which only the
            // scene's own rotation can supply correctly.
            const float3 worldUpHint = punctualLightWorldUp(pl.worldToLight);
            const float3 right = simd::normalize(simd::cross(forward, worldUpHint));
            const float3 up = simd::cross(right, forward);
            // pl.fovDeg is pbrt's own single "fov" parameter,
            // which real pbrt-v4 (and this project's own
            // src/shared/projection_light.h port, already
            // proven on the CPU/OptiX backends - see its own
            // "screenBounds"/"aspect" comments) always applies
            // to the SHORTER image axis: screen bounds are
            // [-aspect,aspect]x[-1,1] when aspect=width/height
            // >= 1 (a wide image, so Y/vertical is the fixed,
            // fov-sized axis and X/horizontal is the wider
            // one), or [-1,1]x[-1/aspect,1/aspect] when
            // aspect < 1 (a tall image, X fixed, Y taller).
            // tanHalfFovBase below is that fixed axis's own
            // half-angle; the other axis scales by aspect (or
            // 1/aspect) exactly as projection_light.h derives.
            const float tanHalfFovBase = tanf((float)pl.fovDeg * 0.5f * (float)M_PI / 180.0f);
            const float imageAspect = (pbrtProjectionImageHeight > 0)
                ? (float)pbrtProjectionImageWidth / (float)pbrtProjectionImageHeight : 1.0f;
            const bool wide = imageAspect >= 1.0f;
            const float tanHalfFovX = wide ? tanHalfFovBase * imageAspect : tanHalfFovBase;
            const float tanHalfFovYFinal = wide ? tanHalfFovBase : tanHalfFovBase / imageAspect;
            projectionLights.push_back(ProjectionLightData{
                PackedFloat3{pos.x, pos.y, pos.z},
                PackedFloat3{forward.x, forward.y, forward.z},
                PackedFloat3{right.x, right.y, right.z},
                PackedFloat3{up.x, up.y, up.z},
                tanHalfFovX,
                tanHalfFovYFinal,
                (float)pl.scale * intensityScale,
                /*usePbrtTexture=*/1u});
            return true;
        }
        fprintf(stderr, "loadPbrtScene: projection light's slide image '%s' could not be "
                        "read/decoded; falling back to the Approx (uniform beam) case\n", pl.filename.c_str());
    return false;
}

// --- Punctual lights (point/spot/distant) ---------------------------
void PbrtSceneLoader::loadPbrtPunctualLights(const pbrt_flatten::FlatScene& scene, const PbrtToWorldFn& toWorld, float sceneScale) {
    // Point and spot both reuse PointLightData - the SAME GPU buffer/
    // shading path the hardcoded room's own point+spot lights already use
    // (see that struct's own comment: direction/cosOuterAngle/
    // cosInnerAngle default to "omnidirectional" unless a spot cone
    // overrides them, exactly PointLightData's own existing convention).
    // Distant reuses DirectionalLightData.
    //
    // Intensity/radiance scale compensation: this app's own point/spot
    // falloff is a real 1/distance^2 term evaluated in ITS OWN internal
    // (rescaled) coordinate space (metal_poc.metal's own `/ plDistSq`
    // division, on toWorld()-transformed positions) - leaving a
    // pbrt-authored "I" unchanged while every point/spot light's own
    // distance to a hit point shrinks by sceneScale would inflate
    // apparent brightness by 1/sceneScale^2 relative to the same scene
    // rendered at its own native scale. Multiplying by sceneScale*
    // sceneScale exactly cancels that: irradiance = I/d^2, d'=d*
    // sceneScale => I'=I*sceneScale^2 keeps I'/d'^2 == I/d^2. Distant
    // lights need no such compensation - a directional light's own
    // contribution has no distance term at all (metal_poc.metal's own
    // DirectionalLight comment), the same reason this loader's area
    // lights above also need none (their own area shrinks by
    // sceneScale^2 in lockstep with d^2, cancelling exactly).
    //
    // Goniometric/Projection real profile/slide images: the FIRST light
    // of each kind that names one gets it (section 98, below); a SECOND
    // one of the same kind, or one whose image fails to load/decode,
    // falls back to the Approx (no-image) case and is counted here.
    const float intensityScale = sceneScale * sceneScale;
    size_t skippedImageBasedLights = 0;
    for (const pbrt_flatten::PunctualLight& pl : scene.punctualLights) {
        const float3 baseEmission{(float)(pl.intensity[0] * pl.scale),
                                   (float)(pl.intensity[1] * pl.scale),
                                   (float)(pl.intensity[2] * pl.scale)};
        switch (pl.kind) {
            case pbrt_flatten::PunctualLightKind::Point: {
                const float3 pos = toWorld(float3{(float)pl.pos[0], (float)pl.pos[1], (float)pl.pos[2]});
                const float3 emission = baseEmission * intensityScale;
                pointLights.push_back(PointLightData{
                    PackedFloat3{pos.x, pos.y, pos.z},
                    PackedFloat3{emission.x, emission.y, emission.z}});
                break;
            }
            case pbrt_flatten::PunctualLightKind::Spot: {
                const float3 pos = toWorld(float3{(float)pl.pos[0], (float)pl.pos[1], (float)pl.pos[2]});
                const float3 dir = simd::normalize(float3{(float)pl.dir[0], (float)pl.dir[1], (float)pl.dir[2]});
                const float3 emission = baseEmission * intensityScale;
                pointLights.push_back(PointLightData{
                    PackedFloat3{pos.x, pos.y, pos.z},
                    PackedFloat3{emission.x, emission.y, emission.z},
                    PackedFloat3{dir.x, dir.y, dir.z},
                    /*cosOuterAngle=*/cosf((float)pl.coneAngleDeg * (float)M_PI / 180.0f),
                    /*cosInnerAngle=*/cosf((float)pl.falloffStartAngleDeg * (float)M_PI / 180.0f)});
                break;
            }
            case pbrt_flatten::PunctualLightKind::Distant: {
                // pl.dir is "wi" (toward the light, see PunctualLight's own
                // field comment); DirectionalLightData::direction is the
                // light's own direction of TRAVEL - the sign convention
                // the hardcoded room's own existing directional light
                // already establishes (its `direction` points along -z,
                // "through the room's own open front", i.e. the way the
                // light travels, not back toward its source).
                const float3 wi = simd::normalize(float3{(float)pl.dir[0], (float)pl.dir[1], (float)pl.dir[2]});
                directionalLights.push_back(DirectionalLightData{
                    PackedFloat3{-wi.x, -wi.y, -wi.z},
                    PackedFloat3{baseEmission.x, baseEmission.y, baseEmission.z}});
                break;
            }
            case pbrt_flatten::PunctualLightKind::Goniometric: {
                // A real per-light IES profile image (section 98) - only
                // the FIRST such light in the scene gets one (this loader
                // has one dedicated pbrtGoniometricTexture slot, same
                // "one shared texture" constraint the hardcoded room's
                // own single goniometricTexture already has - see
                // metal_poc.h's own havePbrtGoniometricImage comment). A
                // second one, or a decode failure, falls back to the
                // Approx case below exactly as if no filename were named
                // at all - erring toward a safe, working (if less
                // accurate) render over dropping the light entirely.
                if (pl.hadImageFilename && !havePbrtGoniometricImage && addGoniometricImageLight(pl, toWorld, baseEmission * intensityScale)) break;
                if (pl.hadImageFilename) ++skippedImageBasedLights;
                // No profile image named (or a second one/a decode
                // failure, both handled above) - pbrt-v4's own documented
                // "Approx" fallback (uniform isotropic intensity, see
                // pbrt_scenes/punctual-lights.pbrt's own header comment
                // for why this is the common case, not just a
                // simplification), which is EXACTLY what a plain
                // omnidirectional PointLightData already represents -
                // isotropic has no direction to get right at all, unlike
                // Projection's own cone/aim (still deferred below), so
                // this needed no new geometry work.
                const float3 pos = toWorld(float3{(float)pl.pos[0], (float)pl.pos[1], (float)pl.pos[2]});
                const float3 emission = baseEmission * intensityScale;
                pointLights.push_back(PointLightData{
                    PackedFloat3{pos.x, pos.y, pos.z},
                    PackedFloat3{emission.x, emission.y, emission.z}});
                break;
            }
            case pbrt_flatten::PunctualLightKind::Projection: {
                // A real per-light slide image (section 98) - same "one
                // dedicated slot, first light wins, decode failure falls
                // back to Approx" reasoning as Goniometric above.
                if (pl.hadImageFilename && !havePbrtProjectionImage && addProjectionImageLight(pl, toWorld, intensityScale)) break;
                if (pl.hadImageFilename) ++skippedImageBasedLights;
                // No slide image named - pbrt-v4's own documented
                // "Approx" fallback: a uniform white cone-shaped beam
                // (ProjectionLight::make_uniform(), matching this
                // project's own CPU builder - see pbrt_cpu_builder.h's
                // own kUniformSlide comment). Represented here as a
                // hard-edged PointLightData spot (cosOuterAngle ==
                // cosInnerAngle - spotLightFalloff()'s own
                // max(...,1e-6) denominator guard keeps this a clean
                // cutoff, not a divide-by-zero) rather than a genuinely
                // new light type: a uniform intensity within a cone and
                // zero outside it is exactly what a spot cone already
                // is, just without the smoothstep edge softening a real
                // spot's inner/outer split gives.
                //
                // Aim direction: see punctualLightWorldForward()'s own
                // comment (metal_poc_host_math.h) for the full
                // derivation - verified there against a known rotation
                // case, not just by hand here.
                const float3 pos = toWorld(float3{(float)pl.pos[0], (float)pl.pos[1], (float)pl.pos[2]});
                const float3 dir = punctualLightWorldForward(pl.worldToLight);
                const float3 emission = baseEmission * intensityScale;
                const float cosHalfFov = cosf(0.5f * (float)pl.fovDeg * (float)M_PI / 180.0f);
                pointLights.push_back(PointLightData{
                    PackedFloat3{pos.x, pos.y, pos.z},
                    PackedFloat3{emission.x, emission.y, emission.z},
                    PackedFloat3{dir.x, dir.y, dir.z},
                    /*cosOuterAngle=*/cosHalfFov,
                    /*cosInnerAngle=*/cosHalfFov});
                break;
            }
            default:
                break;
        }
    }
    if (skippedImageBasedLights > 0)
        fprintf(stderr, "loadPbrtScene: %zu goniometric/projection light(s) with a real profile/slide "
                        "image fell back to the Approx (uniform) case - only the first light of each "
                        "kind gets its own image, and a failed decode also falls back here\n",
                skippedImageBasedLights);
}
