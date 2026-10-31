// metal_poc_kernel_media.metal
// Participating media in primaryRayKernel's bounce: the bounded medium spheres / cylinders (materialType 28-30) and the global camera medium or
// glass-bounded medium (free-flight sampling, scattering with next-event estimation). Pure code motion out of the old kernel body; see
// metal_poc_kernel_state.metal.

// Bounded media (the intersection `B.result` is a medium sphere or cylinder): steps through or scatters in it, setting
// B.scatteredInMedium / B.passedThroughMediumSphere. Returns false when the path ends here (the emission-only last step cannot scatter).
inline bool traverseBoundedMedia(thread const KernelRes& R, thread PathState& P, thread BounceState& B) {
    KERNEL_RES_ALIASES(R)
    PATH_STATE_ALIASES(P)
    BOUNCE_STATE_ALIASES(B)
    // Bounded, per-object homogeneous medium (A8, section 176) -
    // materialType 28, a sphere whose OWN interior is a real
    // participating medium instead of a solid surface (unlike the
    // global `fogSigmaT` medium just below, which fills the WHOLE
    // scene's interior up to whatever it hits first). Mirrors
    // gpu/optix/scene_builder.cpp's own build_cornell_smoke_gpu()
    // precedent exactly: CPU's own two rotated/translated BOXES
    // have no Metal (or OptiX) primitive to represent directly, so
    // both back-ends approximate each box as a sphere instead - an
    // accepted, already-documented CPU/GPU divergence, not a new
    // approximation invented here.
    //
    // The SAME free-flight sampling technique the global fog just
    // below already uses (this whole kernel's own comment on it
    // applies here identically), just bounded between the
    // sphere's own ENTRY distance (`result.distance`, already
    // known) and its EXIT distance - computed analytically, not
    // via a second intersect() call: a ray's two roots of
    // |O+tD-C|^2=r^2 sum to `2*dot(C-O,D)` (D already normalised,
    // so the quadratic's leading coefficient is exactly 1), so the
    // farther root is just that sum minus the already-known nearer
    // one - no extra acceleration-structure traversal needed.
    // `mat.color`/`mat.ior`/`mat.roughness` carry this sphere's
    // OWN albedo/sigmaT/HG-asymmetry-g (TriangleMaterial's usual
    // "one scalar slot, per-materialType meaning" reuse), letting
    // several differently-tinted/dense medium spheres coexist in
    // one scene, unlike the single shared `uniforms.fogSigmaT`.
    //
    // Real NEE to the scene's own registered area lights from a
    // scatter point inside this medium - the SAME sampleAreaLight()/
    // area-to-solid-angle/MIS machinery the global fog's own NEE
    // block just below already uses (mirrored, not duplicated
    // blindly: only the area-light case, since that's the only
    // light kind A8 - the one scene exercising this so far -
    // actually registers; point/directional/projection/
    // goniometric lights would need the identical treatment the
    // day a scene combining a bounded medium with one of those
    // shows up, not attempted here). A first version of this
    // shipped with NO NEE at all here (phase-sampled continuation
    // only, the same "visible but not NEE-sampled" tier sphere/
    // disk/cylinder-shaped area LIGHTS already established) -
    // rendering and comparing against `--cpu` showed why that
    // tier doesn't transfer to a participating MEDIUM the same
    // way: a light SOURCE is often reached directly or after one
    // bounce, but a scatter point deep in fog reaching a small
    // ceiling light by pure phase-sampled chance is a far
    // lower-probability event, so the smoke rendered as sparse
    // black flecks on an otherwise-empty room instead of
    // recognisable haze - a real correctness gap, not a
    // cosmetic one, fixed properly rather than shipped.
    // At the emission-only step a bounded medium (sphere/cylinder) cannot be scattered in, so the path just ends there; the light seen
    // through it by this one last ray is dropped (an approximation limited to the very last vertex).
    if (lastBounce && result.type == intersection_type::bounding_box &&
        ((result.geometry_id == 0u && sphereMaterials[result.primitive_id].materialType >= METAL_MAT_MEDIUM_HOMOGENEOUS && sphereMaterials[result.primitive_id].materialType <= METAL_MAT_MEDIUM_RGB_GRID) ||
         (result.geometry_id == 2u && cylinderMaterials[result.primitive_id].materialType == METAL_MAT_MEDIUM_HOMOGENEOUS))) return false;
    const MediumEnvRefs medEnv{envMarginalCDF, envConditionalCDF, uniforms.envMapWidth, uniforms.envMapHeight,
                               pbrtEnvMarginalCDF, pbrtEnvConditionalCDF, uniforms.pbrtEnvMapWidth, uniforms.pbrtEnvMapHeight, earthTexture, pbrtEnvTexture};
    if (result.type == intersection_type::bounding_box && result.geometry_id == 0u) {
        uint mediumPrimId = result.primitive_id;
        TriangleMaterial mediumMat = sphereMaterials[mediumPrimId];
        if (mediumMat.materialType == METAL_MAT_MEDIUM_HOMOGENEOUS) {
            shadeHomogeneousMediumSphere(mediumMat, mediumPrimId, result.distance,
                spheres, shutterT, lights, pointLights, directionalLights, uniforms, medEnv, pbrtAreaLightTexture, textureSampler,
                isect, accelStructure, functionTable, shadowSpherePayload,
                rayDir, rayOrigin, throughput, radiance, bsdfPdf, specularBounce, rngState,
                scatteredInMedium, passedThroughMediumSphere, mediumChan);
        } else if (mediumMat.materialType == METAL_MAT_MEDIUM_HETEROGENEOUS) {
            shadeCloudMediumSphere(mediumMat, mediumPrimId, result.distance, spheres, shutterT,
                cloudMediums, lights, pointLights, directionalLights, uniforms, medEnv, pbrtAreaLightTexture, textureSampler,
                isect, accelStructure, functionTable, shadowSpherePayload,
                rayDir, rayOrigin, throughput, radiance, bsdfPdf, specularBounce, rngState,
                scatteredInMedium, passedThroughMediumSphere, envLightSampled);
        } else if (mediumMat.materialType == METAL_MAT_MEDIUM_RGB_GRID) {
            shadeRgbGridMediumSphere(mediumMat, mediumPrimId, result.distance, spheres, shutterT,
                rgbGridMediums, rgbGridData, lights, pointLights, directionalLights, uniforms, medEnv, pbrtAreaLightTexture, textureSampler,
                isect, accelStructure, functionTable, shadowSpherePayload,
                rayDir, rayOrigin, throughput, radiance, bsdfPdf, specularBounce, rngState,
                scatteredInMedium, passedThroughMediumSphere);
        }
    } else if (result.type == intersection_type::bounding_box && result.geometry_id == 2u &&
               cylinderMaterials[result.primitive_id].materialType == METAL_MAT_MEDIUM_HOMOGENEOUS) {
        // A tube bounding a homogeneous medium (Shape "cylinder" + MediumInterface).
        shadeHomogeneousMediumCylinder(cylinderMaterials[result.primitive_id], cylinders[result.primitive_id],
            lights, pointLights, directionalLights, uniforms, medEnv, pbrtAreaLightTexture, textureSampler,
            isect, accelStructure, functionTable, shadowSpherePayload,
            rayDir, rayOrigin, throughput, radiance, bsdfPdf, specularBounce, rngState,
            scatteredInMedium, passedThroughMediumSphere, mediumChan);
    }
    return true;
}

// The shortest part of a flight that counts as being inside a listed volume. A ray that has just left one starts on its boundary, where float rounding (the scene sits 60 units to the side, an ulp of
// 4e-6) can leave a hair of "flight" inside it.
constant float kListedMinFlight = 5e-5;

// The listed volumes (Uniforms::mediumListCount: fog, smoke and cloud spheres, whose boundary the trace does not report to the path's own rays): the first one the flight from the ray's origin to the
// nearest surface (result) spends length in, and the part of the flight inside it, [listedStart, listedEnd]. A ray that starts inside one is in it from its origin, and a surface inside a volume ends the
// flight there, as in pbrt. A glass medium's flight is the glass's own, so a path inside one finds none. listedStart stays INFINITY when there is none.
inline void findListedVolume(thread const KernelRes& R, thread PathState& P, thread BounceState& B) {
    KERNEL_RES_ALIASES(R)
    PATH_STATE_ALIASES(P)
    BOUNCE_STATE_ALIASES(B)
    listedStart = INFINITY;
    if (uniforms.mediumListCount == 0u || inGlass) return;
    const float tSurface = (result.type == intersection_type::none) ? INFINITY : result.distance;
    for (uint i = 0u; i < uniforms.mediumListCount; ++i) {
        const uint prim = uint(rgbGridData[uniforms.mediumListOffset + i]);
        const SphereData s = spheres[prim];
        const float3 oc = rayOrigin - float3(s.center);
        const float b = dot(oc, rayDir);
        const float disc = b * b - (dot(oc, oc) - s.radius * s.radius);
        if (!(disc > 0.0f)) continue;
        const float root = sqrt(disc);
        const float t0 = max(-b - root, 0.0f), t1 = min(-b + root, tSurface);
        if (t1 - t0 > kListedMinFlight && t0 < listedStart) {
            listedStart = t0;
            listedEnd = t1;
            listedPrim = prim;
        }
    }
}

// Samples the flight through the listed volume found by findListedVolume(): a collision scatters (B.scatteredInMedium), a flight that gets through the volume's far side goes on from there
// (B.passedThroughMediumSphere, the next bounce looks for the next volume), and one that the nearest surface ended is left as it is: the surface is shaded with this ray's own intersection.
inline bool traverseListedVolume(thread const KernelRes& R, thread PathState& P, thread BounceState& B) {
    KERNEL_RES_ALIASES(R)
    PATH_STATE_ALIASES(P)
    BOUNCE_STATE_ALIASES(B)
    if (!(listedStart < INFINITY)) return true;
    const TriangleMaterial vm = sphereMaterials[listedPrim];
    const MediumEnvRefs medEnv{envMarginalCDF, envConditionalCDF, uniforms.envMapWidth, uniforms.envMapHeight,
                               pbrtEnvMarginalCDF, pbrtEnvConditionalCDF, uniforms.pbrtEnvMapWidth, uniforms.pbrtEnvMapHeight, earthTexture, pbrtEnvTexture};
    const bool endedBySurface = (result.type != intersection_type::none) && listedEnd >= result.distance;
    const float3 originBefore = rayOrigin;
    if (vm.materialType == METAL_MAT_MEDIUM_HOMOGENEOUS) {
        shadeHomogeneousMediumSpan(vm, listedStart, listedEnd, lights, pointLights, directionalLights, uniforms, medEnv, pbrtAreaLightTexture, textureSampler,
            isect, accelStructure, functionTable, shadowSpherePayload,
            rayDir, rayOrigin, throughput, radiance, bsdfPdf, specularBounce, rngState,
            scatteredInMedium, passedThroughMediumSphere, mediumChan, lastBounce);
    } else if (vm.materialType == METAL_MAT_MEDIUM_HETEROGENEOUS) {
        shadeCloudMediumSphere(vm, listedPrim, listedEnd, spheres, shutterT,
            cloudMediums, lights, pointLights, directionalLights, uniforms, medEnv, pbrtAreaLightTexture, textureSampler,
            isect, accelStructure, functionTable, shadowSpherePayload,
            rayDir, rayOrigin, throughput, radiance, bsdfPdf, specularBounce, rngState,
            scatteredInMedium, passedThroughMediumSphere, envLightSampled, listedEnd, lastBounce);
    } else if (vm.materialType == METAL_MAT_MEDIUM_RGB_GRID) {
        shadeRgbGridMediumSphere(vm, listedPrim, listedEnd, spheres, shutterT,
            rgbGridMediums, rgbGridData, lights, pointLights, directionalLights, uniforms, medEnv, pbrtAreaLightTexture, textureSampler,
            isect, accelStructure, functionTable, shadowSpherePayload,
            rayDir, rayOrigin, throughput, radiance, bsdfPdf, specularBounce, rngState,
            scatteredInMedium, passedThroughMediumSphere, listedEnd, lastBounce);
    }
    // Through the medium up to a surface: no step to a boundary, the surface is where this ray's own trace found it.
    if (passedThroughMediumSphere && endedBySurface) {
        rayOrigin = originBefore;
        passedThroughMediumSphere = false;
    }
    return true;
}

// The global camera medium (or the medium inside a glass sphere): samples a free-flight distance against the surface hit, and on a scatter
// does next-event estimation with every light kind and samples a new direction (B.scatteredInMedium). Returns false when the path ends.
inline bool scatterInGlobalMedium(thread const KernelRes& R, thread PathState& P, thread BounceState& B) {
    KERNEL_RES_ALIASES(R)
    PATH_STATE_ALIASES(P)
    BOUNCE_STATE_ALIASES(B)

    // Homogeneous-medium free-flight distance sampling: draws a
    // random scattering distance from the medium's own
    // transmittance distribution (t = -ln(1-u)/sigmaT) and
    // compares it against the surface hit's own distance. This
    // ONE stochastic comparison is what makes BOTH "reached the
    // surface without scattering" and "scattered partway there"
    // come out unbiased with NO extra transmittance/pdf-ratio
    // multiplier needed in either case - a well-known result (the
    // pdf of sampling t this way, p(t) = sigmaT*exp(-sigmaT*t),
    // exactly cancels the extinction term(s) either way):
    //   scatter event (t < surfaceDist): weight = sigmaS*T(t)/p(t)
    //     = sigmaS/sigmaT (the albedo below, nothing else)
    //   reached surface (t >= surfaceDist): weight =
    //     T(surfaceDist)/P(t>=surfaceDist) = 1 exactly (survival
    //     probability under an exponential distribution IS the
    //     transmittance) - so the existing surface-shading code
    //     below needs NO changes at all for this case.
    //
    // Gated on an ACTUAL surface hit existing at all
    // (result.type != none) - the fog fills the scene's INTERIOR
    // (bounded implicitly by the room's own geometry, see this
    // struct's own comment), not empty space beyond a miss. An
    // earlier version of this code used FLT_MAX as a miss ray's
    // own "surface distance," which is a bug, not a deliberately
    // unbounded medium: since a sampled t is a finite real number
    // with probability 1, `t < FLT_MAX` is true for EVERY miss
    // ray, meaning no ray could ever actually reach the sky/
    // environment-map code below once fog was enabled at all -
    // every escaping ray incorrectly kept "scattering" in a
    // medium that should have already ended at the scene's own
    // boundary. Invisible in this POC's own default scene (the
    // room's 5 closed walls mean almost every PRIMARY ray already
    // hits something at 40 degrees FOV - only secondary/GI
    // bounces reflecting out through the open front ever actually
    // missed, a small enough fraction to not read as an obvious
    // artifact), but a real correctness bug, caught by this PR's
    // own wide-FOV/pulled-back verification render for the
    // environment-map feature - that render came back an
    // unexplained near-black speckled mess, and tracing why
    // surfaced this.
    // UPDATE: the fog is now only ever enabled by a pbrt scene's camera medium
    // (the hand-authored room is gone), and a pbrt camera medium extends to
    // infinity: a ray that escapes the scene still scatters with probability 1
    // (CPU does the same). So a miss ray gets an unbounded free-flight limit
    // instead of being skipped - without this the sky region of a foggy scene
    // (E10) rendered black instead of haze-lit.
    if (!scatteredInMedium && !passedThroughMediumSphere && (inGlass || uniforms.fogSigmaT > 0.0)) {
        const float fogSigmaTHere = (fogChan >= 0) ? float3(uniforms.fogSigmaT3)[fogChan] : uniforms.fogSigmaT;
        const float curSigmaT = inGlass ? (glassChan >= 0 ? glassSigmaT3[glassChan] : glassSigmaT3.x) : fogSigmaTHere;
        const float curG = inGlass ? glassG : uniforms.fogAsymmetryG;
        const float3 curAlbedo = inGlass ? glassAlbedo : float3(uniforms.fogAlbedo);
        // Transmittance along shadow rays: a glass sphere's own medium is attenuated stochastically in
        // sphereIntersectionFunction, so only the camera fog is applied analytically here.
        const float shadowSigmaT = inGlass ? 0.0f : fogSigmaTHere;
        shadowSpherePayload.shadowChannel = inGlass ? glassChan : fogChan;
        float surfaceDist = (result.type == intersection_type::none) ? INFINITY : result.distance;
        surfaceDist = min(surfaceDist, listedStart);   // the camera fog's flight ends where a listed volume begins (inside one, the volume alone is sampled)
        // The camera fog may end at a sphere (FlatScene::cameraMediumBound): only the part of the flight inside it can collide, so the flight is drawn from where it enters and
        // counts as a collision only before it leaves (a glass medium fills its whole flight).
        float flightStart = 0.0f, flightEnd = surfaceDist;
        if (!inGlass && uniforms.fogBoundRadius > 0.0f) {
            const float fogLen = fogLength(uniforms, rayOrigin, rayDir, surfaceDist, flightStart);
            flightEnd = flightStart + fogLen;
            if (!(fogLen > 0.0f)) flightEnd = -1.0f;   // the ray misses the fog: nothing to collide with
        }
        float u = randFloat(rngState);
        float t = flightStart + sampleFreePathDistance(u, curSigmaT);
        if (t < flightEnd) {
            if (lastBounce) return false;   // emission-only step: no scattering, the path ends
            scatteredInMedium = true;
            fromMediumScatter = true;   // the camera fog and glass interiors do no image-sky light sampling: the continuation ray's escape takes the whole sky
            float3 scatterPoint = rayOrigin + rayDir * t;

            // NEE from the scatter point - same sampleAreaLight()/
            // area-to-solid-angle/MIS machinery every surface
            // material's own NEE branch already uses, with the
            // Henyey-Greenstein phase function VALUE (no cosine
            // term - a volume scattering event has no surface to
            // cosine-weight against, unlike a BRDF) standing in
            // for the BSDF value. `wo` (direction back toward
            // where this ray came from) is captured BEFORE
            // `rayDir` gets overwritten below by the sampled
            // continuation direction, same `wo` convention the
            // GGX conductor code uses. `exp(-sigmaT*dist)`
            // attenuates this shadow ray's own contribution by the
            // medium's transmittance along ITS length too - the
            // free-flight sampling above only accounts for the
            // PRIMARY ray's path, a shadow ray is a separate,
            // deterministic occlusion test that needs this factor
            // applied explicitly or it would silently ignore the
            // fog lying between the scatter point and the light.
            float3 wo = -rayDir;
            LightSample ls = sampleAreaLight(lights, uniforms.lightCount, rngState, pbrtAreaLightTexture, textureSampler, scatterPoint);
            float3 toLight = ls.point - scatterPoint;
            float distSq = dot(toLight, toLight);
            float dist = sqrt(distSq);
            float3 wi = toLight / dist;
            float cosLight = dot(ls.normal, -wi);
            if ((cosLight > 0.0 || (ls.twoSided != 0.0 && cosLight < 0.0))) {
                ray shadowRay;
                shadowRay.origin = scatterPoint;
                shadowRay.direction = wi;
                shadowRay.min_distance = 0.001f;
                shadowRay.max_distance = dist - 0.002f;
                intersection_result<instancing, triangle_data> shadowResult =
                    traceShadowAnyP(isect, shadowRay, accelStructure, functionTable, shadowSpherePayload);
                if (shadowResult.type == intersection_type::none) {
                    float pdfSolidAngle = (distSq / (ls.area * abs(cosLight))) * ls.pmf;
                    // HG's own sampling pdf for direction wi EQUALS
                    // its own phase function value at the same
                    // cosTheta - a defining property (the phase
                    // function IS already a normalized pdf over
                    // the sphere), so the same call serves both as
                    // the "BSDF" value AND its own competing MIS
                    // pdf, no separate pdf expression needed the
                    // way a surface BRDF's importance-sampled pdf
                    // usually differs from its raw value.
                    float phaseValue = henyeyGreensteinPhase(dot(wo, wi), curG);
                    float weight = (pdfSolidAngle * pdfSolidAngle)
                        / (pdfSolidAngle * pdfSolidAngle + phaseValue * phaseValue);
                    float tin;
                    float transmittance = exp(-shadowSigmaT * (inGlass ? dist : fogLength(uniforms, shadowRay.origin, shadowRay.direction, dist, tin)));
                    // `* curAlbedo`: this scattering
                    // event's own albedo weight - the SAME real bug
                    // materialType 28's own NEE block once had
                    // (section 176's own comment on that fix, found
                    // while diagnosing A8), left unfixed here at the
                    // time since the default fogAlbedo
                    // (0.85,0.88,0.95) makes the omission a sub-5%
                    // colour error, invisible in practice - fixed
                    // properly now that it's been flagged. Without
                    // this, every fog NEE contribution below (this
                    // one and all 4 delta-light ones) rendered as
                    // the LIGHT's own colour with no tint from the
                    // fog's own albedo at all.
                    radiance += throughput * curAlbedo * phaseValue * ls.emission * transmittance
                                / pdfSolidAngle * weight;
                }
            }

            // Point lights: summed unconditionally, not picked,
            // full weight, no MIS - see PointLight's own comment.
            for (uint pli = 0; pli < uniforms.pointLightCount; ++pli) {
                PointLight pl = pointLights[pli];
                float3 toPointLight = float3(pl.position) - scatterPoint;
                float plDistSq = dot(toPointLight, toPointLight);
                float plDist = sqrt(plDistSq);
                float3 plWi = toPointLight / plDist;
                ray plShadowRay;
                plShadowRay.origin = scatterPoint;
                plShadowRay.direction = plWi;
                plShadowRay.min_distance = 0.001f;
                plShadowRay.max_distance = plDist - 0.002f;
                intersection_result<instancing, triangle_data> plShadowResult =
                    traceShadowAnyP(isect, plShadowRay, accelStructure, functionTable, shadowSpherePayload);
                if (plShadowResult.type == intersection_type::none) {
                    float plPhaseValue = henyeyGreensteinPhase(dot(wo, plWi), curG);
                    float plTin;
                    float plTransmittance = exp(-shadowSigmaT * (inGlass ? plDist : fogLength(uniforms, plShadowRay.origin, plShadowRay.direction, plDist, plTin)));
                    float plSpot = spotLightFalloff(-plWi, float3(pl.direction), pl.cosOuterAngle, pl.cosInnerAngle);
                    radiance += throughput * curAlbedo * plPhaseValue * float3(pl.emission) * plSpot * plTransmittance / plDistSq;
                }
            }

            // Directional lights: summed unconditionally, not
            // picked - see DirectionalLight's own comment. No
            // distance falloff, but fog attenuation now DOES
            // apply, using fogExitLength() as the real path
            // length - see that function's own comment.
            for (uint dli = 0; dli < uniforms.directionalLightCount; ++dli) {
                DirectionalLight dl = directionalLights[dli];
                float3 dlWi = normalize(-float3(dl.direction));
                ray dlShadowRay;
                dlShadowRay.origin = scatterPoint;
                dlShadowRay.direction = dlWi;
                dlShadowRay.min_distance = 0.001f;
                dlShadowRay.max_distance = kDirectionalLightMaxDistance;
                intersection_result<instancing, triangle_data> dlShadowResult =
                    traceShadowAnyP(isect, dlShadowRay, accelStructure, functionTable, shadowSpherePayload);
                if (dlShadowResult.type == intersection_type::none) {
                    float dlPhaseValue = henyeyGreensteinPhase(dot(wo, dlWi), curG);
                    float dlExitDist = inGlass ? 0.0f : fogExitLength(uniforms, scatterPoint, dlWi);
                    float dlTransmittance = exp(-shadowSigmaT * dlExitDist);
                    radiance += throughput * curAlbedo * dlPhaseValue * float3(dl.emission) * dlTransmittance;
                }
            }

            // Projection ("slide projector") lights: same
            // unconditional-sum, no-MIS shape as every other
            // delta light here - see ProjectionLight's own
            // comment.
            for (uint pji = 0; pji < uniforms.projectionLightCount; ++pji) {
                ProjectionLight pj = projectionLights[pji];
                float3 toProjLight = float3(pj.position) - scatterPoint;
                float pjDistSq = dot(toProjLight, toProjLight);
                float pjDist = sqrt(pjDistSq);
                float3 pjWi = toProjLight / pjDist;
                float3 pjRadiance = projectionLightRadiance(-pjWi, pj.forward, pj.right, pj.up,
                                                             pj.tanHalfFovX, pj.tanHalfFovY, pj.scale,
                                                             (pj.usePbrtTexture != 0u ? pbrtProjectionTexture : earthTexture), textureSampler);
                if (any(pjRadiance > float3(0.0))) {
                    ray pjShadowRay;
                    pjShadowRay.origin = scatterPoint;
                    pjShadowRay.direction = pjWi;
                    pjShadowRay.min_distance = 0.001f;
                    pjShadowRay.max_distance = pjDist - 0.002f;
                    intersection_result<instancing, triangle_data> pjShadowResult =
                        traceShadowAnyP(isect, pjShadowRay, accelStructure, functionTable, shadowSpherePayload);
                    if (pjShadowResult.type == intersection_type::none) {
                        float pjPhaseValue = henyeyGreensteinPhase(dot(wo, pjWi), curG);
                        float pjTin;
                        float pjTransmittance = exp(-shadowSigmaT * (inGlass ? pjDist : fogLength(uniforms, pjShadowRay.origin, pjShadowRay.direction, pjDist, pjTin)));
                        radiance += throughput * curAlbedo * pjPhaseValue * pjRadiance * pjTransmittance / pjDistSq;
                    }
                }
            }

            // Goniometric lights: same unconditional-sum, no-MIS
            // shape as every other delta light here - see
            // GoniometricLight's own comment.
            for (uint gli = 0; gli < uniforms.goniometricLightCount; ++gli) {
                GoniometricLight gl = goniometricLights[gli];
                float3 toGoniLight = float3(gl.position) - scatterPoint;
                float glDistSq = dot(toGoniLight, toGoniLight);
                float glDist = sqrt(glDistSq);
                float3 glWi = toGoniLight / glDist;
                float3 glRadiance = goniometricLightRadiance(-glWi, gl.forward, gl.right, gl.up,
                                                              gl.emission, gl.scale,
                                                              (gl.usePbrtTexture != 0u ? pbrtGoniometricTexture : goniometricTexture), textureSampler);
                if (any(glRadiance > float3(0.0))) {
                    ray glShadowRay;
                    glShadowRay.origin = scatterPoint;
                    glShadowRay.direction = glWi;
                    glShadowRay.min_distance = 0.001f;
                    glShadowRay.max_distance = glDist - 0.002f;
                    intersection_result<instancing, triangle_data> glShadowResult =
                        traceShadowAnyP(isect, glShadowRay, accelStructure, functionTable, shadowSpherePayload);
                    if (glShadowResult.type == intersection_type::none) {
                        float glPhaseValue = henyeyGreensteinPhase(dot(wo, glWi), curG);
                        float glTin;
                        float glTransmittance = exp(-shadowSigmaT * (inGlass ? glDist : fogLength(uniforms, glShadowRay.origin, glShadowRay.direction, glDist, glTin)));
                        radiance += throughput * curAlbedo * glPhaseValue * glRadiance * glTransmittance / glDistSq;
                    }
                }
            }

            float3 newDir = sampleHenyeyGreenstein(wo, curG, rngState);
            rayDir = newDir;
            rayOrigin = scatterPoint;
            throughput *= curAlbedo;
            bsdfPdf = henyeyGreensteinPhase(dot(wo, newDir), curG);
            specularBounce = false;
        }
    }
    return true;
}
