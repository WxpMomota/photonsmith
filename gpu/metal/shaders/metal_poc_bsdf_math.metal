// metal_poc_bsdf_math.metal
// Scattering math shared by the material shaders: Fresnel (dielectric, conductor, complex), Beer-Lambert, free-path sampling, GGX (D, G, VNDF sampling, energy table), orthonormal bases.
// Split out of metal_poc_sampling.metal (pure code motion) for the code-size limit; concatenated in the order of metal_poc_shader_files.h.

// The REAL (unpolarized, real-valued-IOR) Fresnel dielectric
// reflectance - ported directly from this project's own CPU renderer
// (src/shared/fresnel.h's own FrDielectric(), mirroring pbrt-v4's
// scattering.h exactly), NOT Schlick's approximation. An earlier
// version of this comment claimed Schlick's approximation was "the same
// one... this project's own CPU dielectric material use[s]" for this
// exact reflect-vs-refract decision - checked while reviewing this
// exact code and found to be WRONG: this project's own `dielectric`
// material (src/TheRestOfYourLife/material_simple.h) uses
// `DielectricBxDF`, which itself calls FrDielectric, not Schlick - a
// documentation inaccuracy as much as a missed accuracy opportunity.
// `cosThetaI` here is ALREADY guaranteed non-negative by construction
// (computed via `facingNormal`, which always faces the incoming ray -
// see the call site), so the `< 0` flip branch below is dead code for
// how this is actually invoked here, kept anyway for a faithful,
// recognizable port rather than a call-site-specific simplification.
inline float frDielectric(float cosThetaI, float eta) {
    cosThetaI = clamp(cosThetaI, -1.0, 1.0);
    if (cosThetaI < 0.0) {
        eta = 1.0 / eta;
        cosThetaI = -cosThetaI;
    }
    float sin2ThetaI = 1.0 - cosThetaI * cosThetaI;
    float sin2ThetaT = sin2ThetaI / (eta * eta);
    if (sin2ThetaT >= 1.0) {
        return 1.0; // Total internal reflection.
    }
    float cosThetaT = sqrt(max(0.0, 1.0 - sin2ThetaT));
    float rParl = (eta * cosThetaI - cosThetaT) / (eta * cosThetaI + cosThetaT);
    float rPerp = (cosThetaI - eta * cosThetaT) / (cosThetaI + eta * cosThetaT);
    return (rParl * rParl + rPerp * rPerp) / 2.0;
}

// Beer-Lambert colour absorption for a dielectric (materialType 2/5) -
// real tinted glass absorbs light proportionally to how far it travels
// THROUGH the medium (a thick paperweight reads far more saturated than
// a thin windowpane of the same glass), not by a flat per-bounce
// multiply the way every earlier version of this POC's dielectric
// branches applied `albedo`. Only fires when `!frontFace` (this hit is
// on the surface's own BACKFACE, i.e. the ray is exiting, not entering) -
// for a CONVEX primitive (true of every dielectric shape this POC has,
// the sphere), the segment just travelled (the previous bounce's own
// entry point to this exit hit) was entirely inside the medium, so
// `result.distance` at the EXIT hit is exactly the in-medium path
// length Beer's law needs, no separate distance-tracking state required.
// A concave dielectric could re-enter/exit multiple times without this
// simple per-hit check catching every segment correctly - not handled,
// same "document the assumption, don't silently rely on it" approach
// this POC's other simplifications use. KNOWN LIMITATION, now reachable:
// materialType 2/5 is also assignable to arbitrary triangle meshes loaded
// from .pbrt scenes (not only the analytic sphere), so a concave glass
// mesh (bowl, torus) will get the wrong path length here versus the
// CPU/OptiX backends' real segment-tracked absorption. Fixing it needs
// per-path "currently inside a medium, entered at distance X" state
// carried through the dielectric shaders - not done.
//
// `mat.color` is reinterpreted here as a per-unit-distance absorption
// COEFFICIENT, not the flat reflectance/tint every other material reads
// it as - {0,0,0} means zero absorption (exp(-0*dist) == 1 exactly, a
// true no-op reproducing perfectly clear glass), not "black," the
// opposite of what {0,0,0} would mean as a reflectance colour elsewhere
// in this same struct.
inline void applyBeerLambertAbsorption(thread float3& throughput, packed_float3 absorption, bool frontFace, float distance) {
    if (!frontFace) {
        throughput *= exp(-float3(absorption) * distance);
    }
}

// Samples a free-flight distance from an exponential (Beer-Lambert)
// transmittance distribution with extinction coefficient sigmaT, given a
// uniform random draw u in [0,1) - the standard inverse-CDF formula: T(t) =
// exp(-sigmaT*t) is the probability of surviving to distance t, so
// t = -ln(1-u)/sigmaT inverts it (max(1-u, 1e-6) only guards u==1.0 exactly,
// which would otherwise take log(0) = -inf). This is the core decision that
// makes free-flight sampling unbiased: comparing the returned t against a
// known surface/boundary distance (t < dist => scattered before reaching
// it) needs no separate transmittance/pdf-ratio correction in either
// outcome, because p(t) = sigmaT*exp(-sigmaT*t) exactly cancels the
// extinction term either way - see this function's two call sites' own
// comments for the full derivation. Extracted here (previously duplicated
// verbatim at both call sites) so it has one implementation and is
// independently testable - metal_poc_shader_tests_media.mm's own
// testSampleFreePathDistance() is the numeric cross-check.
inline float sampleFreePathDistance(float u, float sigmaT) {
    return -log(max(1.0 - u, 1e-6)) / sigmaT;
}

// ---------------------------------------------------------------------------
// GGX / Trowbridge-Reitz microfacet distribution + height-correlated Smith
// masking-shadowing - the standard model materialType == 4 (rough
// conductor) uses below, same formulation pbrt-v4's own
// TrowbridgeReitzDistribution implements. Genuinely ANISOTROPIC (alphaX,
// alphaY, not a single scalar alpha) - all four take full LOCAL-frame
// vectors (tangent/bitangent/normal components), not just a `NdotX`
// scalar, since the anisotropic case needs the vector's azimuthal
// (tangent/bitangent) components too, not only its angle to the normal.
// Passing alphaX == alphaY reduces every one of these EXACTLY to the
// isotropic formulas this POC used through step 22 (verified
// algebraically, not just assumed) - not a separate code path, the same
// formula degenerating correctly at its own isotropic boundary case,
// same spirit as the Henyey-Greenstein phase function's own g==0 case
// (step 19). Cross-checked against Blender Cycles' own anisotropic GGX
// implementation (`bsdf_aniso_D`/`bsdf_aniso_lambda` in
// intern/cycles/kernel/closure/bsdf_microfacet.h) before being committed
// here, not derived from first principles alone this time.
// ---------------------------------------------------------------------------
inline float ggxD(float3 hLocal, float alphaX, float alphaY) {
    float3 hr = float3(hLocal.x / alphaX, hLocal.y / alphaY, hLocal.z);
    float lenSq = max(dot(hr, hr), 1e-12);
    return (1.0 / M_PI_F) / max(alphaX * alphaY * lenSq * lenSq, 1e-12);
}

// Smith's Lambda function (anisotropic GGX closed form) - how much of a
// microfacet's neighbourhood is masked/shadowed as seen from direction
// `wLocal`, folded into G1/G below rather than used standalone.
inline float ggxLambda(float3 wLocal, float alphaX, float alphaY) {
    float wz2 = max(wLocal.z * wLocal.z, 1e-12);
    float sqrAlphaTanN = (alphaX * alphaX * wLocal.x * wLocal.x + alphaY * alphaY * wLocal.y * wLocal.y) / wz2;
    return 0.5 * (sqrt(1.0 + sqrAlphaTanN) - 1.0);
}

inline float ggxG1(float3 wLocal, float alphaX, float alphaY) {
    return 1.0 / (1.0 + ggxLambda(wLocal, alphaX, alphaY));
}

// Height-correlated Smith masking-shadowing for a full reflection lobe
// (both the view and light direction masked/shadowed jointly, not treated
// as independent) - the same correlated form pbrt-v4 uses, less energy
// loss at grazing angles than a naive G1(wo)*G1(wi) product.
inline float ggxG(float3 woLocal, float3 wiLocal, float alphaX, float alphaY) {
    return 1.0 / (1.0 + ggxLambda(woLocal, alphaX, alphaY) + ggxLambda(wiLocal, alphaX, alphaY));
}

// The rough-conductor microfacet BRDF value, D*G*F/(4*NdotO*NdotI) - pbrt-
// v4's own ConductorBxDF::f() (src/shared/bxdfs_conductor.h's own
// ConductorBxDF<T>::f(), the CPU reference this is numerically cross-
// checked against in metal_poc_shader_tests.mm). Deliberately does NOT
// include `energyScale` (shadeConductor()'s own multi-scatter energy-
// compensation term, section 72/73) - that's an extra correction this
// POC adds ON TOP of the base microfacet BRDF, applied at each call site
// AFTER this function returns, not part of the closed-form formula
// itself (the CPU reference has no such term either, so including it
// here would make this untestable against that reference). Factored out
// of shadeConductor() (metal_poc_materials_specular.metal, 7 call sites)
// so it's independently testable, the same rationale as
// lambertianPdf()/diffuseTransmissionPdf() above.
inline float3 ggxConductorF(float Dh, float G, float3 F, float NdotO, float NdotI) {
    // Zero (not a clamped tiny denominator) when either direction is at or
    // below the shading hemisphere - max(..., 1e-6) alone turned a NEGATIVE
    // 4*NdotO*NdotI into a small POSITIVE one, returning a spuriously
    // large positive BRDF for a VNDF sample that landed just below the
    // surface (silhouettes, bump-mapped/shading-normal-divergent hits).
    // Same convention lambertianPdf() uses above for cosWi <= 0.
    if (NdotO <= 0.0 || NdotI <= 0.0) return float3(0.0);
    return Dh * G * F / max(4.0 * NdotO * NdotI, 1e-6);
}

// The rough-conductor VNDF-sampling PDF, D*G1/(4*NdotO) - pbrt-v4's own
// ConductorBxDF::pdf() (== ggx_vndf_reflection_pdf(), same header) -
// purely geometric, no Fresnel term, matching that reference exactly.
// Factored out of shadeConductor() (4 call sites), same rationale as
// ggxConductorF() above.
inline float ggxConductorPdf(float Dh, float G1, float NdotO) {
    // See ggxConductorF()'s own comment - same below-hemisphere guard.
    if (NdotO <= 0.0) return 0.0;
    return (Dh * G1) / max(4.0 * NdotO, 1e-6);
}

// Schlick's Fresnel approximation for a CONDUCTOR: F0 (reflectance at
// normal incidence) is itself an RGB colour here, not derived from a
// scalar IOR the way frDielectric()'s own real-valued formula is - a
// metal's complex refractive index (real eta + imaginary k, wavelength-
// dependent) is what actually produces that colour, and this whole curve
// is approximated from its own F0 value rather than solving the full
// complex-Fresnel equations, unlike this POC's own dielectric material,
// which now uses the exact real-valued formula (frDielectric()) instead
// of Schlick's own approximation of it.
inline float3 fresnelSchlickConductor(float cosTheta, float3 F0) {
    float t = pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
    return F0 + (float3(1.0) - F0) * t;
}

// frComplex() - the REAL complex-valued Fresnel reflectance for a
// conductor interface (pbrt-v4's own FrComplex, src/pbrt/util/
// scattering.h; ported from this POC's own reference copy at
// src/shared/fresnel.h). Unlike fresnelSchlickConductor()'s single-F0
// curve (which can only ever interpolate towards white at grazing
// angles), a genuine complex index of refraction (eta + i*k, both
// wavelength/channel-dependent) reproduces the real per-channel colour
// SHIFT actual metals show at grazing incidence - the same kind of
// upgrade frDielectric() (section 55) already made for this POC's
// dielectric materials over Schlick's own approximation of THAT curve.
// All complex arithmetic expanded manually (no complex<> type on
// Metal), identical to the reference's own GPU-compatible expansion.
inline float frComplex(float cosThetaI, float etaR, float etaK) {
    cosThetaI = clamp(cosThetaI, 0.0, 1.0);
    float sin2I = 1.0 - cosThetaI * cosThetaI;

    // Complex Snell's law: sin2T = sin2I / (etaR + i*etaK)^2
    float denomR = etaR * etaR - etaK * etaK;
    float denomI = 2.0 * etaR * etaK;
    float denomSq = denomR * denomR + denomI * denomI;
    float sin2TR = sin2I * denomR / denomSq;
    float sin2TI = -sin2I * denomI / denomSq;

    // cosT = sqrt(1 - sin2T) via the standard complex sqrt formula.
    float cR = 1.0 - sin2TR;
    float cI = -sin2TI;
    float mag = sqrt(cR * cR + cI * cI);
    float cosTR = sqrt(max(0.0, (mag + cR) * 0.5));
    float cosTI = (cI >= 0.0 ? 1.0 : -1.0) * sqrt(max(0.0, (mag - cR) * 0.5));

    // r_parl = (eta*cosI - cosT) / (eta*cosI + cosT), eta = etaR + i*etaK.
    float ecR = etaR * cosThetaI - cosTR;
    float ecI = etaK * cosThetaI - cosTI;
    float edR = etaR * cosThetaI + cosTR;
    float edI = etaK * cosThetaI + cosTI;
    float edSq = edR * edR + edI * edI;
    float rpR = (ecR * edR + ecI * edI) / edSq;
    float rpI = (ecI * edR - ecR * edI) / edSq;
    float rParlSq = rpR * rpR + rpI * rpI;

    // r_perp = (cosI - eta*cosT) / (cosI + eta*cosT).
    float etcR = etaR * cosTR - etaK * cosTI;
    float etcI = etaR * cosTI + etaK * cosTR;
    float ncR = cosThetaI - etcR;
    float ncI = -etcI;
    float ndR = cosThetaI + etcR;
    float ndI = etcI;
    float ndSq = ndR * ndR + ndI * ndI;
    float rsR = (ncR * ndR + ncI * ndI) / ndSq;
    float rsI = (ncI * ndR - ncR * ndI) / ndSq;
    float rPerpSq = rsR * rsR + rsI * rsI;

    return (rParlSq + rPerpSq) * 0.5;
}

// Evaluates frComplex() independently per RGB channel - the real
// per-channel complex Fresnel this POC's GGX conductor material
// (materialType 4/9) now uses in place of fresnelSchlickConductor().
inline float3 frComplexRGB(float cosThetaI, float3 eta, float3 k) {
    return float3(frComplex(cosThetaI, eta.x, k.x),
                  frComplex(cosThetaI, eta.y, k.y),
                  frComplex(cosThetaI, eta.z, k.z));
}

// Builds an orthonormal (tangent, bitangent) frame around `n` - same Duff
// et al. construction cosineSampleHemisphere uses inline, factored out
// here since GGX sampling needs to move both the outgoing direction and
// the sampled half-vector between world space and this local frame
// explicitly (unlike cosineSampleHemisphere, which only ever produces a
// world-space result and never needs the frame itself back).
inline void buildOnb(float3 n, thread float3& tangent, thread float3& bitangent) {
    float sign = n.z >= 0.0 ? 1.0 : -1.0;
    float a = -1.0 / (sign + n.z);
    float b = n.x * n.y * a;
    tangent = float3(1.0 + sign * n.x * n.x * a, sign * b, -sign * n.x);
    bitangent = float3(b, sign + n.y * n.y * a, -n.y);
}

// Tangent frame for ANISOTROPIC materials (materialType 4's brushed-metal
// sphere, alphaX != alphaY), where - unlike an isotropic BRDF/phase
// function - the highlight orientation depends on which way the tangent
// points. NOTE this is numerically the same construction as buildOnb()
// above (Duff et al.), on purpose: an earlier version projected a fixed
// world-space reference axis onto the tangent plane (Gram-Schmidt, "lines
// of longitude"), but that had a hard discontinuity where the normal
// crosses the reference axis's fallback threshold (see below). Any
// tangent field on a sphere has a singularity somewhere (hairy-ball
// theorem); the Duff construction's is a single, well-behaved one rather
// than a visible seam, and there is no per-vertex tangent data on this
// POC's analytic sphere to do better. Kept as a separate function so a
// future real per-vertex-tangent path has one place to slot in.
// Branchless orthonormal basis from a unit normal - Duff, Burgess,
// Christensen, Hery, Kensler, Liani, Villemin, "Building an Orthonormal
// Basis, Revisited" (JCGT 2017), ported from this POC's own reference
// copy at src/shared/microfacet.h's BuildArbitraryTangentFrame(). Fixes
// a real bug this function used to have: the earlier "switch to a
// different world axis when `normal` gets too close to the reference
// direction" construction (`if (abs(dot(refDir, normal)) > 0.999)
// refDir = ...`) has a HARD DISCONTINUITY exactly at that 0.999
// threshold - as `normal` sweeps across it (e.g. anywhere on a sphere
// whose surface normal passes near world +/-Y), the chosen tangent/
// bitangent axes pop to a completely different orientation with no
// continuous transition. Invisible for an ISOTROPIC GGX lobe
// (rotationally symmetric in the tangent plane, so the frame's own
// orientation never affects the result) - a real, visible seam for a
// genuinely ANISOTROPIC one (materialType 4's own brushed-metal
// sphere, alphaX != alphaY), which is the only caller of this function.
// This formulation (using copysign rather than a manual branch) has no
// hard discontinuity at the sign flip / reference-axis threshold, unlike
// the one it replaces (its one remaining singularity is the unavoidable
// hairy-ball one noted at the top of this comment).
inline void buildAnisotropicOnb(float3 normal, thread float3& tangent, thread float3& bitangent) {
    float sign = copysign(1.0, normal.z);
    float a = -1.0 / (sign + normal.z);
    float b = normal.x * normal.y * a;
    tangent = float3(1.0 + sign * normal.x * normal.x * a, sign * b, -sign * normal.x);
    bitangent = float3(b, sign + normal.y * normal.y * a, -normal.y);
}

// Samples a half-vector from the GGX distribution of VISIBLE normals
// (Heitz 2018, "Sampling the GGX Distribution of Visible Normals"), given
// the outgoing direction `woLocal` already in the local (Z-up == shading
// normal) frame. Dramatically lower variance than importance-sampling
// D(h) directly, especially near grazing angles - the same algorithm
// pbrt-v4's TrowbridgeReitzDistribution::Sample_wm implements, chosen
// here for the same reason: it's what makes a rough-conductor path
// tracer converge in a reasonable sample count instead of needing far
// more samples to beat down grazing-angle noise.
// Generalized to anisotropic alphaX/alphaY (Heitz 2018's own Section
// 3.2/3.4 stretch-and-unstretch steps, using alphaX/alphaY on their
// respective axes instead of one shared alpha) - alphaX == alphaY
// reduces this exactly to the isotropic version this POC used through
// step 22, cross-checked against Blender Cycles' own
// `microfacet_ggx_sample_vndf` before being committed here.
inline float3 sampleGGXVNDF(float3 woLocal, float alphaX, float alphaY, thread uint& rngState) {
    float3 Vh = normalize(float3(alphaX * woLocal.x, alphaY * woLocal.y, woLocal.z));
    float lensq = Vh.x * Vh.x + Vh.y * Vh.y;
    float3 T1 = lensq > 0.0 ? float3(-Vh.y, Vh.x, 0.0) / sqrt(lensq) : float3(1.0, 0.0, 0.0);
    float3 T2 = cross(Vh, T1);

    float u1 = randFloat(rngState);
    float u2 = randFloat(rngState);
    float r = sqrt(u1);
    float phi = 2.0 * M_PI_F * u2;
    float t1 = r * cos(phi);
    float t2 = r * sin(phi);
    float s = 0.5 * (1.0 + Vh.z);
    t2 = (1.0 - s) * sqrt(max(0.0, 1.0 - t1 * t1)) + s * t2;

    float3 Nh = t1 * T1 + t2 * T2 + sqrt(max(0.0, 1.0 - t1 * t1 - t2 * t2)) * Vh;
    float3 Ne = float3(alphaX * Nh.x, alphaY * Nh.y, max(0.0, Nh.z));
    return normalize(Ne);
}

// GGX multi-scatter energy compensation (phase 2 - see
// metal_poc_host_math.h's own buildGGXEnergyTable() comment for the full
// "why"). Bilinear lookup into the E(roughness, mu) table built once at
// host startup, mirroring sampleGGXEnergyTable()'s own host-side
// interpolation exactly (same index math, same clamping) rather than
// texture2d::sample() - this is plain float data with no image-file/
// sRGB concerns, and a `device const float*` buffer already matches the
// layout buildGGXEnergyTable() itself produces with no repacking needed.
inline float sampleGGXEnergyTableDevice(device const float* E, uint roughRes, uint muRes,
                                         float roughness, float mu) {
    float rf = roughness * float(roughRes) - 0.5;
    float mf = mu * float(muRes) - 0.5;
    int r0 = int(floor(rf)), m0 = int(floor(mf));
    float rt = rf - float(r0), mt = mf - float(m0);
    int r1 = r0 + 1, m1 = m0 + 1;
    r0 = clamp(r0, 0, int(roughRes) - 1);
    r1 = clamp(r1, 0, int(roughRes) - 1);
    m0 = clamp(m0, 0, int(muRes) - 1);
    m1 = clamp(m1, 0, int(muRes) - 1);
    float e00 = E[r0 * int(muRes) + m0];
    float e10 = E[r1 * int(muRes) + m0];
    float e01 = E[r0 * int(muRes) + m1];
    float e11 = E[r1 * int(muRes) + m1];
    float e0 = e00 + (e10 - e00) * rt;
    float e1 = e01 + (e11 - e01) * rt;
    return e0 + (e1 - e0) * mt;
}
