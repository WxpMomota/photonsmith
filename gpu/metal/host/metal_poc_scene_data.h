#pragma once
// metal_poc_scene_data.h -- the host-side scene of the Metal renderer and the code that fills it from a pbrt file, with no Metal in it.
//
// MetalPocApp used to hold ~200 members in one struct: the options, the scene arrays, the loader's state, the GPU buffers and the dispatch state, so the loader
// (about 3,600 lines in metal_poc_pbrt_*.mm) could only run inside a program that had created a Metal device. PbrtSceneLoader is the part that needs no GPU:
// the scene arrays (triangles, spheres, disks, cylinders, lights, media), the camera/medium/sky/filter state read from the file, and the loadPbrt*/mapPbrt*
// methods that fill them. MetalPocApp derives from it and adds the device, the buffers, the acceleration structures, the shader and the output, so every
// member keeps its name and meaning. Nothing in it creates or touches a device: a test can build a PbrtSceneLoader, set pbrtScenePath, call
// loadPbrtScene() and look at the arrays on a machine with no GPU (metal_poc_loader_tests.mm). (The headers it includes import Metal for the types they name, so it is
// compiled as Objective-C++.)

#include <cfloat>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <array>
#include <simd/simd.h>

#include "metal_poc_host_math.h"
#include "../../../src/shared/pbrt_load.h"
#include "../../../src/shared/cornell_box_data.h"
#include "../../../src/shared/conductor_data.h"
#include "../../../src/shared/realistic_camera.h"

#include "metal_poc_gpu_types.h"
#include "metal_poc_material_params.h"
#include "metal_poc_scene_helpers.h"

struct PbrtSceneLoader {
    // the image size: the loader needs the aspect ratio (the camera's field of view is the pbrt file's, along its shorter side)
    uint32_t width = 400;
    uint32_t height = 400;
    // Optional 7th positional CLI arg - a real .pbrt scene file to load
    // via src/shared/pbrt_load.h INSTEAD of buildScene()'s own hardcoded
    // room (see loadPbrtScene()'s own comment for exactly what subset of
    // pbrt this v1 supports). Empty (the default) keeps every existing
    // CLI invocation's behavior identical to before this existed.
    std::string pbrtScenePath;
    // Set by loadPbrtScene() when the scene named mesh files that could not be read and had
    // nothing else to draw (pbrt_load::LoadResult::missingFiles). metal_render_main() turns it
    // into ERR_FILE_NOT_FOUND instead of rendering the empty room and reporting success.
    std::string pbrtMissingAssetsError;
    // True for a scene flagged requires_files (set from the registry by the caller): any unreadable mesh is then fatal,
    // not just "every mesh". See SceneDescriptor::missing_meshes_error.
    bool pbrtRequireAllMeshes = false;
    // Called by loadPbrtScene() on the flattened scene after the scene frame (scale, centre) is fixed and before anything is built from
    // it: the Live Preview uses it to move objects (src/shared/live_object_edit.h). The frame is NOT recomputed afterwards, so moving
    // an object never rescales or recentres the picture.
    std::function<void(pbrt_flatten::FlatScene&)> pbrtSceneEdit;
    // Optional 9th positional CLI arg (see parseArgsAndCreateDevice()'s own
    // argv[8] handling) - a scene_id one of buildHandAuthoredScene()'s own
    // real cases covers (section 116, docs/history/METAL_GPU_FEASIBILITY.md), for a
    // scene with NO pbrt file backing it at all. Mutually exclusive with
    // pbrtScenePath in practice - metal_render_main() only ever sets one of
    // the two, matching cpu_scene_pbrt_path_by_id() either resolving a real
    // path (pbrtScenePath) or not (this one, only if cpu_scene_metal_hand_
    // authored_supported() says so).
    // Set by loadPbrtScene() when pbrtScenePath was given and loaded
    // successfully - buildScene()'s camera-setup call in
    // compileShaderAndDispatch() reads these instead of its own hardcoded
    // literals whenever this is true.
    bool havePbrtCamera = false;
    bool havePbrtTriangleAnyHit = false;   // some triangle has an alpha-cutout mask or a subsurface material: the triangle geometry is then non-opaque and tested by triangleAnyHitFunction
    float3 pbrtCameraPos{0, 0, 0};
    float3 pbrtCameraForward{0, 0, -1};
    // pbrt is left-handed: a pbrt file's perspective camera draws what is on the world's +x side on the LEFT of the picture (pbrt_flatten::Camera::leftHanded, true for a plain
    // LookAt; false once the file mirrors the camera with `Scale -1 1 1` before it). The kernels build the picture from cross(forward, up) with signs that give pbrt's picture
    // for the orthographic, spherical and realistic cameras of a plain LookAt, and the opposite for the perspective camera, so this is set (and `pbrtCameraRight` below is the
    // NEGATION of cross(forward, up); `pbrtCameraUp` stays the true up) for a perspective camera of a plain LookAt, and for the other cameras of a mirrored file.
    bool pbrtPerspectiveLeftHanded = false;
    float3 pbrtCameraRight{1, 0, 0};
    float3 pbrtCameraUp{0, 1, 0};
    float pbrtTanHalfFov = 1.0f;
    // The scene's own lookat point (world/transformed space) and raw
    // (untransformed - it's a direction) up vector, plus the bbox-rescale
    // transform loadPbrtScene() derived - all saved so applyCameraOverride()
    // can recompute pbrtCameraPos/Forward/Right/Up for a caller-supplied
    // lookfrom in the scene's OWN pbrt-file coordinate space, the same
    // space cpu_scene_recommended_camera()/cam_x/y/z already use for every
    // other backend, without needing loadPbrtScene() to run again.
    float3 pbrtCameraLookAtWorld{0, 0, 0};
    float3 pbrtCameraUpRaw{0, 1, 0};
    // Thin-lens DOF for a hand-authored scene (D5, section 137) - both
    // already in this SCENE's own rescaled/offset unit system (a caller
    // computes them from the scene's own real defocus_angle/focus_dist
    // the same way camera.h's own `defocus_radius = focus_dist *
    // tan(defocus_angle/2)` does, then multiplies both by sceneScale).
    // Defaults (0/1) preserve every earlier hand-authored scene's own
    // "no DOF" behaviour exactly - this is a genuinely SEPARATE gap from
    // this struct's own pre-existing "pbrt v1 doesn't parse the pbrt
    // file's own lensradius/focaldistance Camera parameters" limitation
    // (metal_render_main()'s own comment) - unrelated, a hand-authored
    // scene has no pbrt file to parse from at all.
    float pbrtLensRadius = 0.0f;
    float pbrtFocusDistance = 1.0f;
    // Camera shutter motion blur for a hand-authored scene (D13, section
    // 174) - same shape as pbrtLensRadius/pbrtFocusDistance immediately
    // above: a scene builder sets this (already in this scene's own
    // rescaled unit system, sceneScale-multiplied like every other
    // world-space delta here), and compileShaderAndDispatch() reads it
    // into uniforms.cameraVelocity instead of its own previous
    // unconditional zero for every `havePbrtCamera` scene. Default
    // {0,0,0} preserves every OTHER scene's own "no camera motion blur"
    // behaviour exactly - a provable no-op, same as SphereData::
    // centerDelta1's own identical default-preserving shape (F11,
    // section 167).
    PackedFloat3 sceneCameraVelocity{0, 0, 0};
    // Orthographic (parallel-projection) camera for a hand-authored
    // scene (D2/D6, section 150) - mirrors pbrtLensRadius/
    // pbrtFocusDistance's own shape immediately above: a scene builder
    // sets this true and reuses pbrtTanHalfFov as the orthographic
    // screen window's own world-space half-extent (already in this
    // scene's own rescaled unit system, same sceneScale-multiplied
    // convention every other world-space distance here uses) - see
    // Uniforms::cameraOrthographic's own comment (metal_poc.metal) for
    // the full ray-generation mechanism. Default false preserves every
    // earlier hand-authored scene's own perspective-camera behaviour
    // exactly.
    bool havePbrtOrthographic = false;
    // Spherical (360-degree equirectangular panorama) camera for a
    // hand-authored scene (D7, section 152) - mirrors
    // havePbrtOrthographic's own shape immediately above. No extra
    // world-space parameter needed (unlike orthographic's own
    // pbrtTanHalfFov reuse) - a panoramic camera has no screen window
    // or FOV at all, only a position and orientation, both already
    // carried by pbrtCameraPos/Forward/Right/Up. See
    // Uniforms::cameraSpherical's own comment (metal_poc.metal) for the
    // full ray-generation mechanism. Default false preserves every
    // earlier hand-authored scene's own behaviour exactly.
    bool havePbrtSpherical = false;
    // Which mapping havePbrtSpherical's own camera uses - see
    // Uniforms::sphericalMappingEqualArea's own comment
    // (metal_poc_types.metal). Only ever set true by loadPbrtScene()
    // itself (a real pbrt file's own Camera "spherical" "string
    // mapping" ["equalarea"], D11/section 159) - no hand-authored
    // scene (D3/D7) requests it, so this stays false for those,
    // matching their own already-shipped EquiRectangular look exactly.
    bool havePbrtSphericalEqualArea = false;
    float3 pbrtBboxCenter{0, 0, 0};
    float pbrtSceneScale = 1.0f;
    float3 pbrtSceneOffset{0, 0, 0};

    // Set by loadPbrtScene() when the scene has a valid homogeneous
    // camera medium (scene.cameraMediumIndex, already validated by
    // pbrt_flatten.h's own resolution pass - see that field's own
    // comment) - read instead of the hardcoded fog defaults in
    // compileShaderAndDispatch()'s own havePbrtCamera override block.
    // triangle index (FlatScene::triangles) -> NEE light index, for the triangles of a
    // non-quad emissive mesh registered as per-triangle lights (AreaLightData kind 4).
    std::unordered_map<int, int32_t> pbrtEmissiveTriangleLightId;
    // triangle index -> unit fibre tangent for the tube triangles tessellated from pbrt curves, so the
    // hair shader (materialType 31) gets the real fibre direction instead of the normal-as-tangent proxy.
    std::unordered_map<int, std::array<float, 3>> pbrtTriangleFiberTangent;
    bool havePbrtMedium = false;
    float pbrtFogSigmaT = 0.0f;
    float3 pbrtFogSigmaT3{0, 0, 0};   // per-channel extinction (chromatic camera medium); pbrtFogSigmaT is its mean
    bool pbrtFogChromatic = false;
    float3 pbrtFogAlbedo{1, 1, 1};
    float pbrtFogAsymmetryG = 0.0f;

    // pbrt-v4 Film "maxcomponentvalue" (FlatScene::maxComponentValue), applied by
    // the kernel as a per-sample firefly clamp. 1e9 = pbrt's default (unbounded).
    float pbrtMaxComponentValue = 1e9f;
    // --maxcomponentvalue as an explicit request (RenderOptions::max_component_value, anything other than its 1e9 "not requested" default): it replaces the
    // scene's own Film value, as on the CPU. 0 = not requested.
    float maxComponentValueOverride = 0.0f;

    // Set by loadPbrtScene() from the scene's own resolved PixelFilter
    // (pbrt_flatten::FlatScene::filter - already per-kind-default-
    // resolved by flatten(), the SAME struct gpu/optix/scene_builder.cpp
    // reads for its own identical GpuCameraParams::filterKind/etc wiring)
    // - read instead of the class-level defaults below in
    // compileShaderAndDispatch()'s own havePbrtCamera override block.
    // The in-struct defaults here (gaussian/1.5/1/3/1/3/0.5/3.0) are
    // pbrt-v4's own PixelFilter class defaults, so a HAND-AUTHORED scene
    // (this member never gets touched at all for one) already gets a
    // real Gaussian r=1.5 reconstruction filter for free, matching CPU's
    // own default exactly - not a sentinel needing a runtime fallback
    // branch the way gpu/optix/optix_types.h's own filterRadius<=0
    // convention needs (GpuCameraParams there is reused/zero-initialized
    // across multiple code paths; MetalPocApp's own members are each
    // only ever written once, by this exact scene's own loadPbrtScene()
    // call, so a plain default member initializer is sufficient here).
    std::string pbrtFilterKind = "gaussian";
    float pbrtFilterB = 1.0f / 3.0f;
    float pbrtFilterC = 1.0f / 3.0f;
    float pbrtFilterSigma = 0.5f;
    float pbrtFilterTau = 3.0f;
    float pbrtFilterRadius = 1.5f;

    // Set by loadPbrtScene() for a constant-colour (no image)
    // LightSource "infinite" - see that function's own comment and
    // metal_poc.metal's own mirrored one on why this is miss-path-only,
    // no NEE/MIS strategy.
    bool havePbrtConstantEnvLight = false;
    float3 pbrtEnvColor{0, 0, 0};

    // Set by loadPbrtScene() for an image-based LightSource "infinite" -
    // the decoded pixels are already linear float RGB, row-major
    // (pbrt_load::loadFile()'s own resolution of InfiniteLight::
    // imagePixels - no filesystem/decode work needed on this side).
    // Mutually exclusive with havePbrtConstantEnvLight above.
    bool havePbrtImageEnvLight = false;
    std::vector<float> pbrtEnvImagePixels;
    int pbrtEnvImageWidth = 0;
    int pbrtEnvImageHeight = 0;

    // Set by loadPbrtScene() for an image infinite light with a portal[4] window (pbrt-v4's PortalImageInfiniteLight). Mutually exclusive with
    // havePbrtImageEnvLight: the plain environment texture/CDFs are not built for it. The three arrays come from a real host-side
    // PortalImageInfiniteLightData (exactly what the CPU builds), uploaded into the pbrtEnv buffers - see portalSampleLi() in metal_poc_environment.metal.
    bool havePbrtPortalLight = false;
    std::vector<float> pbrtPortalRectified;   // w*h*3, scale NOT applied
    std::vector<float> pbrtPortalDistFunc;    // w*h
    std::vector<float> pbrtPortalSatSum;      // w*h (float here; the CPU keeps double)
    int pbrtPortalWidth = 0, pbrtPortalHeight = 0;
    float pbrtPortalScale = 1.0f;
    float3 pbrtPortalFrameX{1, 0, 0}, pbrtPortalFrameY{0, 1, 0}, pbrtPortalFrameZ{0, 0, 1};
    float3 pbrtPortalP0{0, 0, 0}, pbrtPortalP2{0, 0, 0};

    // Set by loadPbrtScene() for the FIRST pbrt-loaded goniometric/
    // projection light that names a real profile/slide image and
    // successfully decodes (section 98) - unlike infinite light's own
    // imagePixels above, PunctualLight::filename is NOT pre-resolved/
    // decoded by pbrt_load.h (see that struct's own comment: only
    // InfiniteLight gets that treatment), so this loader does its own
    // pbrt_load::loadFileNear() + pbrt_load::detail::
    // decodeInfiniteLightImage() call - reusing that decoder rather than
    // writing a second one, since a goniometric/projection image is
    // decoded exactly the same way (extension-dispatched, EXR via
    // tinyexr or stb_image's float loader for everything else). Only ONE
    // slot per kind, mirroring this POC's existing "one shared texture"
    // architecture (goniometricTexture/earthTexture, section 57/91) -
    // a SECOND image-based light of the same kind in one scene is warned
    // and falls back to the Approx case (section 91/92), same as an
    // unsupported light kind entirely. Row-major linear float RGB, same
    // layout as pbrtEnvImagePixels above.
    bool havePbrtGoniometricImage = false;
    std::vector<float> pbrtGoniometricImagePixels;
    int pbrtGoniometricImageWidth = 0;
    int pbrtGoniometricImageHeight = 0;

    bool havePbrtProjectionImage = false;
    std::vector<float> pbrtProjectionImagePixels;
    int pbrtProjectionImageWidth = 0;
    int pbrtProjectionImageHeight = 0;

    // A pbrt-loaded scene's own image-based AreaLightSource
    // ("string filename", section 105) - same "one shared slot, first
    // light wins" constraint as the goniometric/projection images
    // above, and QUAD-shaped lights only (see AreaLight::useTexture's
    // own comment for why a disk-shaped one still falls back to flat L).
    bool havePbrtAreaLightImage = false;
    // The pictures of the scene's area lights (AreaLightSource "filename") live in ONE atlas texture, stacked top to bottom (buildAreaLightAtlas): the pixels, the atlas's size, and for each
    // picture file its place in it (scale u, scale v, offset u, offset v). A room's windows may each carry another picture.
    std::unordered_map<std::string, std::array<float, 4>> pbrtAreaLightImagePlaces;
    void buildAreaLightAtlas(const pbrt_flatten::FlatScene& scene);
    // True if `em` names a picture that is in the atlas; `place` is then where.
    bool claimAreaLightImage(const pbrt_flatten::Emission& em, std::array<float, 4>& place) const;
    std::unordered_set<int> pbrtImageEmissiveTriangles;   // the triangles of a mesh light that carries a picture (they take it from their own uv)
    std::vector<float> pbrtAreaLightImagePixels;
    int pbrtAreaLightImageWidth = 0;
    int pbrtAreaLightImageHeight = 0;

    // A pbrt-loaded scene's own Diffuse/CoatedDiffuse material with a
    // "texture reflectance" bound to a bare "imagemap" Texture (F5/F9,
    // section 166) - same "one shared slot, first material wins"
    // constraint as every other pbrt-loaded image above.
    // A SECOND image slot (texture index 8) for a DiffuseTransmission material whose "texture transmittance" is an
    // imagemap while its reflectance is a different one (J2): reflectance uses the diffuse slot above, transmittance this.
    bool havePbrtTransmitImage = false;
    std::vector<float> pbrtTransmitImagePixels;
    int pbrtTransmitImageWidth = 0;
    int pbrtTransmitImageHeight = 0;
    std::string pbrtTransmitImageFilename;
    bool havePbrtDiffuseImage = false;
    std::vector<float> pbrtDiffuseImagePixels;
    int pbrtDiffuseImageWidth = 0;
    int pbrtDiffuseImageHeight = 0;
    // The filename actually loaded into the fields above - lets a
    // SECOND triangle sharing the SAME already-loaded textured material
    // (e.g. a 2-triangle quad) correctly get materialType 26 too,
    // distinguishing "the same material's own second triangle" from "a
    // genuinely different second texture" (metal_poc.mm's own
    // MaterialKind::Diffuse case comment has the full story).
    std::string pbrtDiffuseImageFilename;


    // --- Host-side scene data, built by buildScene() --------------------
    std::vector<PackedFloat3> verts, normals;
    std::vector<PackedFloat2> uvs;
    std::vector<TriangleMaterial> materials;
    std::vector<PackedFloat3> suzanneVerts, suzanneNormals;
    std::vector<PackedFloat2> suzanneUVs;
    std::vector<TriangleMaterial> suzanneMaterials;
    std::vector<AreaLightData> lights;
    std::vector<SphereData> spheres;
    std::vector<TriangleMaterial> sphereMaterials;
    std::vector<DiskData> disks;
    std::vector<TriangleMaterial> diskMaterials;
    std::vector<CylinderData> cylinders;
    std::vector<TriangleMaterial> cylinderMaterials;
    // E2's own backing buffer for materialType 29 (GpuCloudMedium's own
    // comment) - empty for every scene but E2.
    std::vector<GpuCloudMedium> cloudMediums;
    // E4's own backing buffers for materialType 30 (GpuRgbGridMedium's
    // own comment) - both empty for every scene but E4. `rgbGridData` is
    // the flat, concatenated R-then-G-then-B voxel data every
    // `rgbGridMediums` entry's own `dataOffset` indexes into.
    std::vector<GpuRgbGridMedium> rgbGridMediums;
    std::vector<float> rgbGridData;
    std::vector<PointLightData> pointLights;
    std::vector<DirectionalLightData> directionalLights;
    std::vector<ProjectionLightData> projectionLights;
    std::vector<GoniometricLightData> goniometricLights;
    // The goniometric light's own procedurally-generated intensity image
    // (see buildGoniometricProfileImage()'s own comment) - built by
    // buildScene() alongside `goniometricLights`, uploaded as a real
    // MTLTexture by buildGPUResources() (needs `device`, not available
    // yet inside buildScene()).
    std::vector<uint8_t> goniometricImage;
    int goniometricImageSize = 0;
    uint32_t triangleCount = 0;
    // Realistic (multi-element-lens) camera (D4/D8, section 157) - built
    // by a hand-authored scene's own builder (metal_poc_scenes_d.mm),
    // directly reading a REAL, host-instantiated `RealisticCamera<float>`'s
    // own GPU-port accessors - see Uniforms::cameraRealistic's own
    // comment for the full mechanism.
    bool havePbrtRealisticCamera = false;
    std::vector<GpuLensElementData> realisticLensElements;
    std::vector<GpuExitPupilBoundsData> realisticExitPupilBounds;
    float realisticFilmHalfX = 0.0f, realisticFilmHalfY = 0.0f, realisticLensRearZ = 0.0f;


    // pbrt ObjectInstance (loadPbrtObjectInstances): each used group's triangles are stored once, after the scene's own triangles, and each
    // placement is a hardware instance of that group's own acceleration structure (pbrtGroupAS, same order as pbrtInstGroups).
    struct PbrtInstancedGroup { uint32_t triBase = 0, triCount = 0; };
    struct PbrtInstancePlacement {
        uint32_t group = 0;
        float linear[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};      // object -> scene, column-major 3x3
        float normalMat[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};   // its inverse transpose
        float translation[3] = {0, 0, 0};
    };
    std::vector<PbrtInstancedGroup> pbrtInstGroups;
    std::vector<PbrtInstancePlacement> pbrtInstPlacements;
    uint32_t pbrtInstTriTotal = 0;                       // triangles appended for groups: the tail of the shared arrays, not part of primAS
    uint32_t pbrtInstanceFirstId = 0xFFFFFFFFu;          // Metal instance id of the first pbrt placement (Uniforms::pbrtInstanceFirst)

    // See its own comment (defined just above buildScene()) - loads a
    // real .pbrt file's geometry/materials/lights/camera into the SAME
    // vectors buildScene()'s own hardcoded room uses. Only called from
    // buildScene() itself, when pbrtScenePath is non-empty.
    void loadPbrtScene();
    // The 9 phases loadPbrtScene() itself is now just a thin dispatcher
    // over (section 109 - split from one ~950-line function, mirroring
    // the SAME "monolithic function -> named phase methods" refactor
    // main()/PR #56 and primaryRayKernel/PR #65 already went through
    // once each, at a comparable size). Every phase takes the shared
    // read-only state it needs as EXPLICIT parameters (never a NEW
    // class member) - toWorld/materialFor as std::function, matching
    // this file's own already-established idiom for mapMaterial -
    // specifically to avoid PR #56's own documented bug class (a
    // local's type annotation left in place after converting it to a
    // member, silently redeclaring a same-named shadowing local): there
    // is nothing here to accidentally redeclare, since none of this
    // shared state becomes a member at all. Every phase writes its own
    // results straight into the ALREADY-existing MetalPocApp members
    // (spheres/disks/lights/pointLights/...) exactly as the single
    // monolithic function used to - only the CALLING convention changed,
    // not where any result actually lives.
    using PbrtToWorldFn = std::function<float3(float3)>;
    using PbrtMaterialForFn = std::function<TriangleMaterial(int)>;
    // pbrt material -> TriangleMaterial (metal_poc_pbrt_materials.mm); PbrtMaterialMapState carries the per-scene inputs and caches.
    // What mapPbrtMaterial() needs besides the material itself: the scene (a Mix material names two others by index), the scene-rescale
    // transform (procedural textures keyed on the pbrt-world hit point fold it in), and two per-scene caches.
    struct PbrtMaterialMapState {
        const pbrt_flatten::FlatScene& scene;
        float sceneScale;
        float3 bboxCenter;
        float3 sceneOffset;
        std::unordered_set<std::string> warnedUnsupportedMaterialKinds;
        std::unordered_map<std::string, int> measuredTableCache;   // resolved .bsdf path -> descriptor index in rgbGridData, or -1
        std::map<std::pair<double, double>, int> bssrdfTableCache;   // (g, eta) -> element offset of that BSSRDF table in rgbGridData
        std::unordered_map<const void*, TriangleMaterial> mixCache;   // a Mix material (by address) -> its mapped METAL_MAT_MIX entry, so each is stored once
    };
    TriangleMaterial mapPbrtMaterial(PbrtMaterialMapState& st, const pbrt_flatten::Material& m, int depth);
    bool mapPbrtPerHitMix(PbrtMaterialMapState& st, const pbrt_flatten::Material& m, int depth, TriangleMaterial& out);
    int bssrdfTableOffset(PbrtMaterialMapState& st, double g, double eta);
    TriangleMaterial mapPbrtSubsurfaceMaterial(PbrtMaterialMapState& st, const pbrt_flatten::Material& m);
    // A pbrt "nanovdb" medium as a per-voxel RGB grid medium (metal_poc_pbrt_nanovdb.mm); false (mat untouched) when the file or grid cannot be used.
    bool mapNanovdbMedium(const pbrt_flatten::Medium& nm, float sceneScale, float cOffX, float cOffY, float cOffZ, TriangleMaterial& mat);
    TriangleMaterial mapPbrtDiffuseMaterial(const pbrt_flatten::Material& m, float sceneScale, float3 bboxCenter, float3 sceneOffset);
    // Area lights ("single quad, 2 triangles" shape only) - populates
    // `lights`, and the two out-params the very next phase
    // (loadPbrtRemainingTriangles) needs: which triangles this phase
    // already consumed, and any light emission a too-complex (non-quad)
    // light still needs applied directly to its own triangles' material.
    void loadPbrtAreaLights(const pbrt_flatten::FlatScene& scene, const PbrtToWorldFn& toWorld,
        const PbrtMaterialForFn& materialFor, std::vector<bool>& triangleHandled,
        std::unordered_map<int, std::pair<float3, bool>>& unhandledLightEmission);
    // Every triangle loadPbrtAreaLights() didn't already consume as a
    // light's own quad - ordinary geometry, or an unhandled (non-quad)
    // light's own triangles (emissive via `unhandledLightEmission`, not
    // NEE-registered).
    void loadPbrtRemainingTriangles(const pbrt_flatten::FlatScene& scene, const PbrtToWorldFn& toWorld,
        const PbrtMaterialForFn& materialFor, const std::vector<bool>& triangleHandled,
        const std::unordered_map<int, std::pair<float3, bool>>& unhandledLightEmission);
    void applyPbrtAlphaMask(TriangleMaterial& mat, const pbrt_flatten::Material& m);
    std::map<std::string, std::array<int, 3>> pbrtAlphaMasks;   // alpha mask file -> {offset in rgbGridData, width, height}
    void applyPbrtImageBump(TriangleMaterial& mat, const pbrt_flatten::Material& m);
    float pbrtBumpHeightScale = 1.0f;   // the scene rescale factor, applied to a height map's scale
    std::map<std::string, std::array<int, 4>> pbrtBumpImages;   // displacement image file -> {offset in rgbGridData, width, height, 1 if a normal map}; width 0 = unusable
    void loadPbrtSpheres(const pbrt_flatten::FlatScene& scene, const std::vector<pbrt_flatten::Sphere>& sphereList, const PbrtToWorldFn& toWorld,
        const PbrtMaterialForFn& materialFor, float sceneScale);
    // The pieces of loadPbrtSpheres: a sphere's material (or the medium it bounds), and its sphere-light registration.
    TriangleMaterial sphereMaterial(const pbrt_flatten::FlatScene& scene, const pbrt_flatten::Sphere& s, const PbrtToWorldFn& toWorld,
        const PbrtMaterialForFn& materialFor, float sceneScale);
    void rgbGridSphereMaterial(const pbrt_flatten::Medium& gm, const PbrtToWorldFn& toWorld, float sceneScale, TriangleMaterial& mat);
    void cloudSphereMaterial(const pbrt_flatten::Medium& cm, const PbrtToWorldFn& toWorld, float sceneScale, TriangleMaterial& mat);
    void emitSphereLight(const pbrt_flatten::FlatScene& scene, const pbrt_flatten::Sphere& s, const SphereData& sd, const float3& center, TriangleMaterial& mat);
    void loadPbrtDisks(const pbrt_flatten::FlatScene& scene, const PbrtToWorldFn& toWorld,
        const PbrtMaterialForFn& materialFor, float sceneScale);
    void loadPbrtCylinders(const pbrt_flatten::FlatScene& scene, const PbrtToWorldFn& toWorld,
        const PbrtMaterialForFn& materialFor, float sceneScale);
    void loadPbrtObjectInstances(const pbrt_flatten::FlatScene& scene, const PbrtToWorldFn& toWorld,
        const PbrtMaterialForFn& materialFor, float sceneScale);
    void loadPbrtPunctualLights(const pbrt_flatten::FlatScene& scene, const PbrtToWorldFn& toWorld, float sceneScale);
    bool addGoniometricImageLight(const pbrt_flatten::PunctualLight& pl, const PbrtToWorldFn& toWorld, const float3& emission);
    bool addProjectionImageLight(const pbrt_flatten::PunctualLight& pl, const PbrtToWorldFn& toWorld, float intensityScale);
    void loadPbrtMedium(const pbrt_flatten::FlatScene& scene, float sceneScale);
    void loadPbrtInfiniteLight(const pbrt_flatten::FlatScene& scene, const PbrtToWorldFn& toWorld);
    void loadPbrtCamera(const pbrt_flatten::FlatScene& scene, const PbrtToWorldFn& toWorld,
        float3 bboxCenter, float sceneScale, float3 sceneOffset);
};
