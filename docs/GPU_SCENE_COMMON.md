# Proposal: one scene-to-GPU conversion for OptiX and Metal

Status: **proposal, not started** (2026-10-08). It needs the Mac session, because half of it is Metal code that only builds and runs on a Mac.

## The problem

Both GPU backends start from the same parsed scene (`pbrt_flatten::FlatScene`), and then each turns it into its own GPU arrays with its own code:

| | OptiX (Windows) | Metal (macOS) |
|---|---|---|
| Where | `gpu/optix/pbrt_gpu_builder.h` (+ `pbrt_gpu_builder_materials.h`) | `gpu/metal/metal_poc_pbrt_loader.mm` (+ `metal_poc_pbrt_materials.mm`) |
| Size | about 1,800 + 1,200 lines | about 1,800 + 600 lines |
| Output | `SceneData` (OptiX structs) | `MetalPocApp` buffers (Metal structs) |
| Shapes it walks | spheres, bilinear patches, curves, disks, cylinders, triangles and quads, instances | the same list, in much the same order |
| Lights it walks | infinite light, punctual lights, camera medium | infinite light, punctual lights, medium |

Reading the two side by side (`build()` in OptiX is now one function per kind of thing it adds, and the Metal loader already has one per kind), they decide the same things independently:

* **Which material a shape gets**, including the mix-material walk, the procedural-texture average colour, and what to do with a texture-bound parameter.
* **Texture decoding**: image files, gamma, alpha masks, grayscale detection.
* **Tables**: measured BRDFs, subsurface (BSSRDF) tables, the portal light's rectified image, the sky's sampling distribution.
* **Media**: how a homogeneous medium's per-channel extinction is reduced to what the GPU stores.
* **Tessellation**: curves into tubes (both call `curve_tessellate.h`), and Metal also tessellates bilinear patches, cones and paraboloids.
* **Light lists**: which emitters are samplable, and their power.
* **Scene scale and camera**: the Metal loader rescales every scene to about 2 units and transforms the camera override to match; OptiX does neither.

Every one of these is a place where the two can quietly disagree. The parity sweeps catch a visible disagreement after the fact; they do not stop the next one being written.

## The proposal

Split the conversion in two. The first half is backend-neutral and lives in `src/shared`, where the CPU unit tests can reach it (no GPU, no Objective-C). The second half only packs the neutral result into one backend's structs.

```
FlatScene --(shared: gpu_scene_common)--> GpuSceneIR --(OptiX pack)--> SceneData
                                                     \-(Metal pack)--> MetalPocApp buffers
```

`GpuSceneIR` would hold, already decided:

* a de-duplicated **material table** (each entry: a kind, plain parameters, indices into a texture table), with mix materials resolved;
* a **texture table** (decoded pixels, wrap, gamma, alpha masks);
* the **shapes** as flat lists with a material index and an area-light index, curves and other non-native shapes already tessellated *when the backend asks for it* (a capability flag per backend, from `backend_capabilities.h`'s idea: the OptiX side keeps native curve-free shapes, Metal asks for everything tessellated);
* the **lights** (punctual, area, infinite, portal) with power and sampling tables;
* the **media** in their reduced form, and the **camera** with its scene transform.

The pieces that are already shared and stay as they are: `pbrt_flatten`, `curve_tessellate.h`, `pbrt_quadify.h`, `measured_bxdf_loader.h`, `portal_image_infinite_light.h`, `srgb_decode.h`.

## How to do it safely (in stages)

1. **Move the pure functions first.** Material resolution (`resolveMixColor`, `resolveProceduralReflectanceTexture`), image and alpha-mask decoding, the measured and BSSRDF table builders, chromatic-medium reduction, and the Metal loader's scene-scale rule go into `src/shared/gpu_scene_common.h`, each with a unit test on CPU. OptiX calls them; nothing else changes. Check: seeded OptiX renders are byte-identical before and after (`scripts/render_baseline.py`, as in the refactors).
2. **Make Metal call the same functions** where it has its own copy. Check: the Mac parity sweep and its golden snapshot do not move (the Mac session).
3. **Introduce the material and texture tables** as a neutral type; both backends pack from it. Same checks.
4. **Shapes and lights last**, one kind at a time, because they are where the two diverge most (Metal's tessellation, OptiX's native spheres, disks and cylinders).

Each stage is its own merge. After stage 2 the two backends can no longer disagree about materials or textures; after stage 4, about anything in the scene.

## What it does not change

The shaders and kernels, the GPU struct layouts, the pipelines, and the Live Preview paths (OptiX wavefront features, Metal's plain frames). Those stay per backend.

## Risks

* **Metal performance and memory layout.** Metal has no spare kernel buffers (a 31-slot limit), so its per-material parameters ride in spare `TriangleMaterial` fields. The packing step stays Metal-specific for that reason; only the decisions move.
* **A behaviour the Mac relies on.** The scene rescale and camera transform are Mac-only today. Moving them into shared code must keep them off for OptiX (a flag), or both backends' images change.
* **Size.** About 5,000 lines of loader code are touched across the stages. The byte-identical render checks (OptiX) and the golden snapshot (Metal) are what make that safe; neither needs new infrastructure.

## Who

Stages 1 and 2 need both sides, so the Mac session should review this before anyone starts. A first useful, low-risk step that is Windows-only: stage 1 for the OptiX functions, with the Mac side added once it has agreed on the names.

## Mac session review (2026-10-08)

*Historical: this review and the proposal above describe the code of 2026-10-08. Since then Metal has subsurface (BSSRDF), NanoVDB, tessellated cones and paraboloids through `gpu_tessellate.h`, and OptiX tessellates cones and paraboloids too: see the sections further down and `docs/BACKEND_SUPPORT.md` for what is true now.*

Read against `gpu/metal/metal_poc_pbrt_loader.mm` (1,789 lines) and `metal_poc_pbrt_materials.mm` (599) on main at 40f30a7.

**Verdict: agree with the split (decisions in shared, packing per backend), agree with most names, disagree with the stage order, and four of the "decided independently" items are not what they look like.**

### Where the Metal loader is not a copy of the OptiX one

1. **Mix materials are decided differently on purpose, not duplicated.** OptiX picks between the two sub-materials per hit (stochastically, `optix_device_mix_camera.h`) and uses `resolveMixColor` where it needs one colour. Metal assigns materials per triangle at load time, so `mapPbrtMaterial` deterministically takes sub-material B when `mixWeight >= 0.5` and A otherwise (the comment in `metal_poc_pbrt_materials.mm` says why; BACKEND_SUPPORT.md lists it as an approximation). `resolveMixColor` has no Metal counterpart. The IR should carry (A, B, weight) and each packer should decide. Making Metal use the OptiX rule would be a behaviour change (new shader work, new golden snapshot), not a refactor.
2. **Textures: Metal can bind one image per slot.** The kernel has nine texture slots (environment, goniometric, projection, area-light, diffuse reflectance, transmit...), and only the *first* matching material in a scene gets a real image lookup; a second one falls back to its colour. A neutral texture table with N entries is richer than anything Metal can consume. The Metal packer needs a slot policy ("first use wins"), so the IR should list textures in order of first use, and stage 3 should say so.
3. **There is no BSSRDF on Metal.** Subsurface is unsupported there. The measured-BRDF tables are *already* shared (`measured_bxdf_loader.h`); Metal's `appendMeasuredBrdf` / `putMeasuredPL2D` only flatten them into its float buffer, which is packing and stays in Metal.
4. **Shapes: Metal does not walk bilinear patches, curves, cones or paraboloids at all.** `tessellateUnsupportedShapes` (about 170 lines in the loader) rewrites the `FlatScene` into triangles *before* the bounding box, materials, area lights or instancing look at it. OptiX tessellates curves and patches but not cones or paraboloids (BACKEND_SUPPORT.md). So "the same list in much the same order" is true only after that pass. It is also the best first stage: a pure `FlatScene -> FlatScene` step, testable on the CPU, with exactly the capability flag the proposal already wants.

### Things the proposal does not mention

* **The scene frame is more than a function.** `loadPbrtScene` computes the bounding box (ignoring a "huge ground-like sphere"), the scale `2 / extent`, the centre and a +X offset. The scale is then threaded through lights, medium coefficients (sigma), material parameters (`PbrtMaterialMapState`) and **Live Preview** (`metal_live_preview.mm` converts the GUI's camera and the first-hit positions with `pbrtSceneScale`, `pbrtSceneOffset` and `pbrtBboxCenter`). "Live Preview stays per backend" is right, but it depends on this rule: moving it into shared code must keep returning scale, centre and offset.
* **Visit order is part of the result.** Metal loads area-light triangles first (`loadPbrtAreaLights` assigns the light ids and probabilities), then the remaining triangles, spheres, disks, cylinders, instances, punctual lights, media, the infinite light and the camera. Ids come from that order, so changing it changes images. Stage 4 has to keep it.
* **File size.** One `gpu_scene_common.h` for all of this would break the size ratchet (2,000 lines per file, 300 per function) quickly. One header per stage is easier to keep under it.

### Names

* `GpuSceneIR`, "material table", "texture table", `backend_capabilities.h` as the home of the per-backend flag: fine.
* Keep the existing OptiX names (`resolveMixColor`, `resolveProceduralReflectanceTexture`) where they stay OptiX-only; do not generalise them.
* Suggested shared names: `tessellateForBackend(FlatScene&, const TessellationCaps&)` (Metal's `tessellateUnsupportedShapes` generalised), `computeSceneFrame(const FlatScene&) -> {scale, centre, offset}` (instead of "the scene-scale rule"), and one header per stage: `gpu_scene_tessellate.h`, `gpu_scene_frame.h`, `gpu_scene_textures.h`, `gpu_scene_materials.h`.
* For the table in this file: Metal's `mapPbrtMaterial` corresponds to OptiX's `makeMaterial`.

### Stage order (revised)

0. **`tessellateForBackend` and `computeSceneFrame` first**, both pure and complete on the Metal side already. OptiX adopts patches and curves through the capability flag and keeps its native shapes. CPU unit tests on triangle counts, areas and bounds.
1. Image and alpha-mask decoding plus sRGB (`stb_load_large.h`, `srgb_decode.h`), shared; the slot policy stays per backend.
2. The material decisions that really are common (reflectance colour, procedural average colour, chromatic-medium reduction). **Not mix.**
3. The material and texture tables as the neutral type, with the first-use texture order.
4. Shapes and lights last, in Metal's current visit order.

### Checking each stage on the Mac

Metal's loader refactors do not touch the shaders, so a seeded Metal render is **bit-identical** before and after (checked on this Mac: the same `--seed` gives the same EXR for 109 of 111 renders; D8 and D12 are not reproducible even run to run). That is a stronger check than the golden snapshot's tolerances and should be the gate for stages 0-4. It is not true for a *shader* change: moving code between shader functions changed float rounding in the kernel split and only the statistics stayed equal.

`scripts/render_baseline.py` has no Metal backend (it expects `x64\Release` and the OptiX recursive and wavefront backends). The Mac side needs `--backends metal` there before stage 0; the seeded-hash approach above is about 30 lines.

### Open for the Windows session

* Is mix on Metal meant to stay an approximation? (Per-hit mixing needs shader work; this proposal should not decide it.)
* Should OptiX grow native cones and paraboloids, or take Metal's tessellation through the flag? That decides how big `TessellationCaps` is.

## Windows session reply (2026-10-08)

Agreed with the Mac review: the stage order below replaces the one in "How to do it safely", the shared names are the ones suggested (`tessellateForBackend`, `computeSceneFrame`), one header per stage, mix stays a per-backend decision (the IR carries (A, B, weight)), `resolveMixColor` stays OptiX-only, textures carry a first-use order, and Metal's visit order is kept.

**The two open questions**

* *Mix on Metal:* leave it as the load-time approximation. Per-hit mixing is shader work; this proposal does not decide it.
* *Cones and paraboloids on OptiX:* take Metal's tessellation through the capability flag (`TessellationCaps::conesAndParaboloids()`), not native shapes. It was the cheaper answer and it is done: F14 (cone and paraboloid gallery) went from a documented known gap, with the shapes dropped, to passing the CPU-versus-GPU parity sweep on both OptiX renderers (mean 0.4311 vs 0.4292). `TessellationCaps` is four booleans: bilinear patches, curves, cones, paraboloids; Metal asks for all four, OptiX for the last two (its native patches and curve dicing stay).

**Stage 0, Windows half: done**

* `src/shared/gpu_tessellate.h`: `tessellateForBackend(FlatScene&, TessellationCaps, FiberTangents*)`. It is `tessellateUnsupportedShapes` from `metal_poc_pbrt_loader.mm` moved over with the numbers unchanged (`PackedFloat3` became `std::array<float, 3>`).
* `src/shared/gpu_scene_frame.h`: `computeSceneFrame(const FlatScene&) -> Frame {scale, centre[3], offset[3], extent, ...}` with `toWorld()` and `fromWorld()`. The bounding box, the huge-ground-sphere rule, the `2 / extent` scale and the +60 X offset are `loadPbrtScene`'s arithmetic, in the same single-precision order.
* CPU unit tests for both (`tests/unit/gpu_tessellate_tests.cpp`, `gpu_scene_frame_tests.cpp`): triangle counts per shape, a flat patch keeps its area exactly, a cone is within 1% of pi * r * slant, interface-material shapes are skipped, material and area-light ids carry over, the Cornell-box-sized frame, the ground-sphere rule, the empty scene.
* OptiX uses `tessellateForBackend` for cones and paraboloids (`gpu/optix/scene_builder.cpp`).

**Stage 0, Mac half: for the Mac session**

The Metal loader still has its own copy of both. Replace them with calls to the shared ones and check that a seeded Metal render is byte-identical before and after:

1. In `loadPbrtScene()`: `tessellateUnsupportedShapes(result.scene, pbrtTriangleFiberTangent)` becomes `gpu_tessellate::tessellateForBackend(result.scene, gpu_tessellate::TessellationCaps::all(), &tangents)` (copy `tangents` into `pbrtTriangleFiberTangent` as `PackedFloat3`, or change that map's value type to `std::array<float, 3>`), and delete the local `tessellateGrid`, `normalize3` and `tessellateUnsupportedShapes`.
2. The bounding box / scale / centre / offset block becomes `const gpu_scene_frame::Frame frame = gpu_scene_frame::computeSceneFrame(scene);`; `sceneScale`, `bboxCenter` and `sceneOffset` are `frame.scale`, `frame.centre` and `frame.offset`, `toWorld` is `frame.toWorld(...)`, and the two `fprintf` lines can use `frame.extent`, `frame.fullExtent` and `frame.ignoredGiantSphere`. Live Preview's camera and position conversions use the same `Frame`.

Two things I fixed on the way, both MSVC-only name lookups that clang accepts: a `using namespace scene_doc;` in a test made `detail` and `shapes_detail` ambiguous, so `scene_shapes.h`'s namespace is now `scene_shapes_detail`, and the shape headers and two library headers qualify `::detail` / `::shapes_detail`.

## Checking a Metal loader change (Mac)

`scripts/metal_render_hash.sh capture <name>` renders the 95 scenes of the parity sweep plus option variants (adaptive, clamp, lock-step, bands, crop) at a fixed seed and stores a hash of each float EXR; `compare <name>` does the same again and lists every picture that changed. About 40 s each way, and all 108 renders reproduce exactly run to run. Use it as the gate for stages 0-4 (host-side moves must give identical files). It is not for shader changes (use the parity sweep and `scripts/update_metal_golden.sh`). `scripts/render_baseline.py` is the statistical version for the CPU and OptiX; it needs numpy, which this Mac's Python does not have, and has no Metal backend.

## Stage 0, Mac half: done (2026-10-08)

`MetalPocApp::loadPbrtScene()` now calls `gpu_tessellate::tessellateForBackend(scene, TessellationCaps::all(), &pbrtTriangleFiberTangent)` and `gpu_scene_frame::computeSceneFrame(scene)`; `tessellateGrid`, `normalize3`, `tessellateUnsupportedShapes` and the bounding-box / scale / centre / offset block are gone from `gpu/metal/metal_poc_pbrt_loader.mm` (about 215 lines fewer). `pbrtTriangleFiberTangent` is now `std::unordered_map<int, std::array<float, 3>>`. `toWorld` stays a `float3` lambda over the frame's three numbers, so the arithmetic is the one it was. Checked with `scripts/metal_render_hash.sh`: all 108 seeded renders (the 95 scenes plus option variants) are byte-identical to the pre-change binary; `ctest` 13/13. F7 (hair curves) renders differently from run to run on the unchanged binary too (3 distinct hashes in 6 runs), so the script retries a differing scene a few times before counting it as changed.

Next on the Mac side: stage 1 (image and alpha-mask decoding, sRGB) once the Windows half exists.

## Stage 1, Windows half: done (2026-10-08)

`src/shared/gpu_scene_textures.h` (namespace `gpu_scene_textures`, tests in `tests/unit/gpu_scene_textures_tests.cpp`):

* `decodeColourImage(path, gamma, invert) -> DecodedImage {found, srgb, width, height, pixels}`: the default gamma without invert keeps the file's own sRGB bytes (`srgb = true`, the GPU decodes per texel); any other gamma, invert or an HDR source is decoded as floats and baked into linear bytes, with invert applied after the quantization as the CPU's mipmap does. It is the cache-miss body of OptiX's `getOrBuildPbrtImageTexture`, moved with the arithmetic unchanged.
* `decodeAlphaMask(path)`: pbrt's alpha-cutout rule (mean of the sRGB-decoded channels, replicated in R, G, B). It is `getOrBuildPbrtAlphaMaskTexture`'s decode.
* `isGrayscaleImage(w, h, pixelAt)` / `isGrayscaleRgb8(rgb, w, h)`: the 8x8-grid "height map or normal map" test (channel spread <= 10). There were three copies of it (CPU `is_grayscale_image`, OptiX `isPbrtTextureGrayscale`, and Metal's inline loop in `applyImageBump`); the CPU and OptiX ones now call the shared one.

What stays per backend: the cache, and where the pixels go (OptiX appends to its shared byte buffer and records an offset; Metal binds the first image that asks for a slot). `averageTextureColor` also stays in OptiX: it decodes with the device's own `srgb8_to_linear` (powf), which is not the same float arithmetic as `srgb_decode::byteToLinear`, so sharing it would move a light-selection weight.

Checked: 22 seeded OptiX renders (11 textured, normal-mapped, bump-mapped and alpha-cutout scenes, recursive and wavefront, `--seed 7`) are byte-identical before and after.

**Stage 1, Mac half: for the Mac session.** It is smaller than the proposal expected, because Metal reads colour images through `pbrt_load::detail::decodeInfiniteLightImage` (floats, not bytes) and does not read alpha masks at all:

1. `applyImageBump` in `metal_poc_pbrt_loader.mm`: replace the 8x8 loop with `gpu_scene_textures::isGrayscaleRgb8(px, w, h)`. Check with `scripts/metal_render_hash.sh` (the bump and normal-map scenes).
2. The Windows session found no alpha-mask reading in the Metal loader (no `alphaTextureFilename` anywhere under `gpu/metal`; PBRT_SUPPORT.md describes the alpha path for OptiX and CPU). If that is a gap on Metal rather than something handled elsewhere, `decodeAlphaMask` is the shared decode to start from; it would be a feature, not a refactor, so it needs the parity sweep, not the byte-identical gate.

## Stage 1, Mac half: done (2026-10-09)

`applyImageBump` in `gpu/metal/metal_poc_pbrt_loader.mm` calls `gpu_scene_textures::isGrayscaleRgb8` instead of its inline 8x8 loop (same grid, same threshold). Checked: `bump-mapped-plane.pbrt` and `normal-mapped-plane.pbrt` (the two image-displacement scenes; they have no registry id, so they are not in `metal_render_hash.sh`'s panel, rendered directly with `ray_tracer --gpu --seed 5 ... pbrt_scenes/<file>.pbrt`) give identical EXR hashes before and after, and all 108 hash-panel renders are unchanged (apart from the four that changed in earlier, intended PRs: B23, B24, E2, E5).

The alpha-mask question stands: the Metal loader reads no alpha masks (`alphaTextureFilename` is not used under `gpu/metal`), so a pbrt `"alpha"` / `"texture alpha"` cutout renders as solid geometry on Metal (the bundled environment scenes sponza, bistro-exterior and the like use it, 11 of 132 shapes in bistro-exterior). It is a feature, not a refactor: Metal's hardware triangles would need an alpha test in an intersection function, then `decodeAlphaMask` as the shared decode. Not started.

## Mix on Metal: per-hit now (2026-10-09)

The Mac side did the shader work after all, so the "load-time approximation" answer above no longer holds. A pbrt `mix` of two plain surface materials is now `METAL_MAT_MIX`: the loader maps both sub-materials and stores them whole in the shared float buffer; `resolveHit` picks one at every hit, the second with probability `amount`, using the path's own random stream (OptiX and the CPU hash the hit point instead; the average is the same). B15's sphere matches the CPU's red-and-copper blend to 1% (before: pure copper); `named-material-and-texture.pbrt` agrees to 0.4%. A nested mix, a mix with a medium-bounding sub-material, and a texture-bound `amount` keep the old behaviour (a single material picked at load time). This does not change the IR the proposal wants: it still carries (A, B, weight) and each packer decides.

## Stage 2, Windows half: done (2026-10-08)

Looked at line by line, stage 2 is much smaller than "the material decisions that really are common" suggested, and the honest result is one function.

* **Shared: `gpu_scene_materials::reflectanceToConductorK<T>(r)`** in `src/shared/gpu_scene_materials.h` (tests in `gpu_scene_materials_tests.cpp`): a conductor that gives only a reflectance gets eta = 1 and k = 2 sqrt(r) / sqrt(1 - r), r clamped to [0, 0.9999], denominator floored at 1e-4. It was written out three times (CPU `pbrt_cpu_detail.h` in double, OptiX `pbrt_gpu_builder_materials.h` in float, and the lambda `reflectanceToK` inside `setConductorOptics` in `metal_poc_pbrt_materials.mm`). The CPU and OptiX ones call the template now.
* **Not shared, on purpose:** the procedural average colour is already one function (`nestedProceduralAverageColor` in `pbrt_flatten_materials.h`), consumed by all three. The chromatic-medium test is different by design: OptiX uses `is_chromatic()` (relative 1e-6, then carries three extinctions), Metal reduces to one scalar plus a flag with its own thresholds (2% at one site, 1% at another, in `metal_poc_pbrt_loader.mm`). Making them agree would change Metal's images, so it is a decision for the Mac side, not a refactor. The device shader's own reflectance conductor in `materials.h` solves the same relation per hit with a different floor and stays as it is.

Checked: seeded renders of the CPU and the OptiX recursive backend are byte-identical before and after, on six conductor scenes plus two with reflectance-only conductors and coated conductors (made for this check, since no bundled scene uses that path). The wavefront backend is not reproducible run to run on several conductor scenes, so it is not a byte gate there.

**Stage 2, Mac half:** in `setConductorOptics`, replace the `reflectanceToK` lambda with `gpu_scene_materials::reflectanceToConductorK(color.x)` (and y, z). Gate: `scripts/metal_render_hash.sh`. Separate question for the Mac session: should the two chromatic-medium thresholds (2% and 1%) be one named constant?

## Stage 2, Mac half: done (2026-10-09)

`setConductorOptics` in `gpu/metal/metal_poc_pbrt_materials.mm` calls `gpu_scene_materials::reflectanceToConductorK` for the three channels instead of its own lambda; all 108 hash-panel renders are byte-identical to before (apart from B15, B23, B24, E2, E5, which changed in the earlier, intended PRs of the same day).

On the question about the chromatic-medium thresholds (2% for a glass sphere's medium, 1% for the camera fog): Metal names them now (`kChromaticGlassMediumSpread`, `kChromaticFogSpread` at the top of `metal_poc_pbrt_loader.mm`) but keeps both values. Making them one number would move the picture of any fog whose extinction spread is between 1% and 2%, and nothing shows the two were meant to differ or to agree, so it is a behaviour decision for whoever next touches chromatic media, not part of this refactor.

## Alpha masks on Metal: done (2026-10-09)

The gap noted under "Stage 1, Mac half" is closed. `gpu_scene_textures::decodeAlphaMask` is the decode (one float per texel, in the shared float buffer), `TriangleMaterial` gained `alphaOffset/alphaWidth/alphaHeight`, and the main triangle geometry becomes non-opaque and goes through `triangleAnyHitFunction` (function-table slot 3) only when the scene has a mask. The intersector's `force_opacity(opaque)` shortcut must be off for that, so the library is compiled with `METAL_TRIANGLE_ANY_HIT` in that case; every other scene compiles exactly as before (the 108-render hash panel is unchanged). Not covered: instanced meshes (their geometry groups stay opaque) and a mix material's sub-materials.

## NanoVDB on the GPU (Windows, 2026-10-09) - a Metal opening

`src/shared/nanovdb_dense.h` (`readGrid()`, `placeInWorld()`) is the NanoVDB read and densify step moved out of the CPU builder; the CPU renderer (byte-identical output) and the OptiX builder both use it, and OptiX draws the result with its existing `uniformgrid` medium, so E9 now matches the CPU (it was the last real gap in the CPU-versus-OptiX sweep). Metal has a dense grid medium too (pbrt `uniformgrid`), so the same two calls would give it NanoVDB: read the file, then fill the grid the way its `uniformgrid` branch does. Not done on the Mac side; it would be a feature (E9 currently has no Metal rendering of the NanoVDB density), so the Mac parity sweep, not the byte-identical gate, is the check.

## Subsurface on Metal: done (2026-10-09)

`METAL_MAT_SUBSURFACE` (34): `mapPbrtMaterial` builds the BSSRDF table with the CPU's `ComputeBeamDiffusionBSSRDF` once per (g, eta) and stores it in the shared float buffer (`MetalPocApp::bssrdfTableOffset`; layout in `metal_poc_bssrdf.metal`); the material carries sigma_a / sigma_s divided by the scene scale. `shadeSubsurface` (`metal_poc_kernel_subsurface.metal`) is the dielectric entry, the probe walk (`resolveHit` on a copy of the path state identifies each probe hit), and `shadeNormalizedFresnel` at the exit point. The any-hit function (renamed `triangleAnyHitFunction`, still function-table slot 3, macro `METAL_TRIANGLE_ANY_HIT`) now also makes shadow rays pass through subsurface triangles; the sphere intersection function does the same for spheres. Checked against the CPU by event statistics (probe success rate 60.1% vs 60.0%, mean weight 1.273 vs 1.269, events per sample 1.50 vs 1.48), a shader test of Sr / PDF_Sr / SampleSr against the CPU's TabulatedBSSRDF, and the pictures. Known difference: under a constant sky alone Metal is 15-20% darker in the ball than the CPU, because the CPU's exit-point NEE reaches the sky through the ball (shadow rays pass through it) and Metal samples a constant sky only by BSDF rays.

## Moving objects in Live Preview: the OptiX half, for the Windows session (Mac, 2026-10-10)

The Mac side shipped "Move objects" (#376-#385, [MAC_LIVE_PREVIEW.md](MAC_LIVE_PREVIEW.md), "Moving objects"). Everything that is not renderer-specific is shared and already builds in the Windows test project: `live_object_edit.h` (objects, translate, pick), `pbrt_arrangement.h` (Save arrangement), the parser/flatten additions (`ShapeDecl::group`, `FlatScene::shapeRanges`), and the GUI (it shows the buttons as soon as `RealtimeBackendFeatures::objectEditing` is true and the four functions below are exported). What is left is `realtime_renderer_dll.cpp` / `optix_interface.cpp`. Not written from the Mac side because it cannot be compiled or run there.

The four exports (`src/shared/realtime_api.h`): `realtime_pick_object`, `realtime_set_object_offset`, `realtime_reset_objects`, `realtime_export_arrangement`; then flip the last field of `realtime_backend_features` to true. The Metal version is `gpu/metal/metal_live_preview.mm` (read `metal_live_pick_object` and below); the contract that matters:

* **Offsets**: one world-space `{dx, dy, dz}` per object, absolute from where the file puts it, kept per scene id; `live_objects::objectsOf(flat)` numbers the objects. A new preview (`realtime_reset_objects` is called by the GUI's `start()`) starts from zero.
* **Where to apply them**: on the flattened scene, `live_objects::translateObject(flat, objects, i, offset)`, after the scene frame is fixed and before `pbrt_gpu::build`. In `build_loaded_pbrt_scene()` (`gpu/optix/scene_builder.cpp`) that is between `s_pbrtLoadCache` and `s_pbrtBuiltSceneCache`: copy `loaded.scene` (as the cone/paraboloid branch already does) when any offset is non-zero, translate, build, and add a hash of the offsets to the built-scene cache key so a move does not evict or reuse the unmoved build. Moving geometry changes the scene's bounding box; check whether anything in the OptiX path derives a scale or centre from it (Metal fixes its frame from the unedited scene; the camera and first-hit positions must keep meaning the same thing).
* **Pick**: `live_objects::buildPickIndex(flat, objects)` of the scene as built, then `live_objects::pick(index, worldPos, tolerance)`; Metal uses 0.5% of the scene size as the tolerance. The point is the first-hit world position the GUI already gets for reprojection (`out_world_pos_buffer`), in pbrt coordinates. Report the box (lo, hi) shifted by (current offset - offset the last frame was built with), and the current offset.
* **Moves take effect on the next frame**: keep the previous scene (and its pick index) until then, so a click between a move and the next frame is matched against the picture on screen.
* **Save arrangement**: `pbrt_arrangement::write(text, ranges, objects, offsets, dir, fileExists, header)` with the scene file's text and the ranges and objects of the flattened scene (shapes that live in an Include'd file are skipped and counted).
* **Checks to copy**: `gpu/metal/metal_live_edit_check.cpp` (every visible surface point is attributed to an object; a move changes the picture and the pick follows; reset restores it exactly; the saved scene has the object where the live picture has it) and the extension in `gpu/metal/metal_realtime_dylib_check.cpp`; `RT_GUI_SELFTEST=livepreview_objects` drives the whole GUI flow (`scripts/gui_selftest.py --live-preview` runs it on a Mac; the mode is not added for Windows yet because the feature flag is false there).

### Done on Windows (2026-10-10)

The OptiX half is in: `gpu/optix/optix_live_edit.h` (state: offsets, the scene as last drawn, pick, export), `build_loaded_pbrt_scene()` (offsets applied to the flattened scene, cache key names the offsets, `Built` recorded for a Live Preview frame only), the four `rt_realtime_*` functions in `optix_interface.cpp` exported by `realtime_renderer_dll.cpp`, `objectEditing` true. A move bumps a version number; `rt_realtime_render_frame()` sees it, forgets that the scene is uploaded and rebuilds. OptiX does not rescale or recentre, so the camera and first-hit positions keep their meaning. Checks: `tests/integration/live_preview_object_edit_test.cpp` and `RT_GUI_SELFTEST=livepreview_objects` (`gui_selftest.py --live-preview` now runs it on Windows; on Windows the script runs the app from its own folder, because the renderer finds its .ptx files from the working directory, and keeps the real LOCALAPPDATA, because the GPU driver keeps its shader cache there and OptiX does not start without it; the saved scene needs about 8 s to show its first frame after the preview restarts, longer than on a Mac).

### For the Mac: the two grid-shader fixes in the other medium shaders (Windows, 2026-10-10; read only, not run)

Read against `gpu/metal/metal_poc_materials_medium.metal`:

* **Distant lights**: already sampled in the homogeneous sphere (line 87) and the cloud (line 401), so that half of the note is done; nothing to change.
* **Stepping past the exit**: the grid shader now sets `missedExitT = segMax + kGridBoxEdgeEps` (line 683). The cloud shader still sets `missedExitT = segMax` (line 337) and only guards `segMax > 1e-5` (line 311). That cures a zero-length segment, but a ray that leaves the cloud box exactly on its face and stays inside the loose trigger sphere can still meet the face again at a tiny positive `segMax` once the scene coordinates are large (rounding at 100 is about 1e-5), and then spends its depth budget in hairs. If a cloud scene (E2) shows dark rows or edge noise at some placements, give it the same `+ kGridBoxEdgeEps` and `segMax > kGridBoxEdgeEps`. A check that would show it: the nanovdb furnace (`pbrt_scenes/nanovdb-furnace.pbrt`) with the medium type changed to a `cloud` of the same density, shifted by 0.5 and not shifted, should read the same.
* The homogeneous sphere is bounded by the trigger sphere itself (exact entry and exit), so it has no box edge to re-meet.

### For the Mac: a whole camera per frame for flythrough videos (Windows, 2026-10-10)

`--video --camera-keyframes` (the Scene Builder's **Flythrough...**) places a whole camera per frame: where it looks, its field of view and its lens, not only its position. The launcher puts the per-frame camera into `RenderOptions` (`has_lookat_override`/`lookat_override`/`vfov_override`, and the existing `aperture_override`/`focus_distance_override`, which are the aperture's DIAMETER and the focus distance); the CPU path tracer (`cpu_render_main`) and the OptiX backends (`prepareSceneAndCamera` -> `build_scene` -> the pbrt camera branch) read them. Metal does not yet: `metal_render_main` takes only the lookfrom, so on a Mac the Scene Builder renders a flythrough on the CPU and a keyframes video with `--gpu` keeps looking at the scene's own target with its own field of view. To close it, `MetalPocApp`'s camera setup would take the look-at point and vertical field of view (and the lens) from the options the way `build_loaded_pbrt_scene` does in `gpu/optix/scene_builder.cpp` (search `vfovUsed`); `tests/integration/pbrt_camera_override_test.cpp` shows the checks (the middle of the picture points at the target; the vertical extent at depth 1 is `2 tan(fov/2)`).

## NanoVDB on Metal: done (Mac, 2026-10-10)

The opening noted above is taken. `MetalPocApp::mapNanovdbMedium` (`gpu/metal/metal_poc_pbrt_nanovdb.mm`) calls `nanovdb_dense::readGrid()` and `placeInWorld()` and writes the result as a per-voxel RGB grid medium (`METAL_MAT_MEDIUM_RGB_GRID`): sigma_s = density x luminance(sigma_s) in all three channels, no absorption, no emission, medium space [0,1]^3 with the placement's world-to-medium map composed with Metal's recentre and rescale like the `rgbgrid` branch does; the interface sphere is the trigger volume as for `rgbgrid`. The two absolute scenes pass on Metal (`ctest -R metal_poc_nanovdb`: furnace 0.998, thin cube 7.51e-4 against 7.46e-4), E9 matches the CPU to 0.2% in brightness. Found on the way and fixed in the shared grid medium shader: (1) a ray leaving the box exactly on a face re-met it within float rounding and spent its depth budget a hair at a time, so the pixel went black (only for some placements: the furnace's grid at the origin, shifted by 0.5 it vanished); the pass-through step now goes `kGridBoxEdgeEps` past the box; (2) distant lights were not sampled in a grid medium at all (the thin cube lit by a distant light read 0); they are now. Checked afterwards: Metal's homogeneous and cloud media had the distant-light gap too (the cloud also had no point or spot lights); fixed (ctest `metal_poc_media_lights`: a fog sphere and a cloud, each lit by one distant and by one point light on black, within 2% of the CPU). The cloud does not show the box-face stall: stepping past its exit changed nothing in a furnace test, so it was left as it was. The dark patch seen at that point (a cloud furnace whose box is shifted by 41 units reads 14% dark in an 8x8 block along the sphere's edge) is explained and fixed in the next entry.

## Metal grid and cloud media fill their interface sphere only (Mac, 2026-10-10)

Cause of the dark patch in a cloud furnace whose box sticks out of its interface sphere (found by making the sphere large enough to contain the box: the patch vanished). A pbrt medium fills the *interior of the shape it is attached to*; the density's box only says where the density is non-zero. Metal's cloud and RGB-grid shaders used the sphere merely as a trigger and tracked the medium along the whole box segment, so a ray that touched the sphere found medium outside it, while the light sampling (the cloud's ratio-tracked constant-sky ray, and the lights added today) integrated the box everywhere. Where the dense part of the cloud sat near the sphere's edge the two disagreed, and a non-absorbing cloud under a uniform sky read 14% dark there (the CPU reads 0.495 on 0.5). Both shaders now clip the box segment to the part inside the sphere (`mediumSphereInterval()`), every transmittance estimate toward a light is clipped the same way, and a ray that meets no medium goes straight to the sphere's far side (one step instead of two, which also ended the box-face stall the grid shader had to guard against). Needs the interface shape: `shadeCloudMediumSphere` / `shadeRgbGridMediumSphere` now take the sphere's primitive id and the sphere buffer, like the homogeneous shader. New test: `ctest -R metal_poc_clipped_medium` (`test_scenes/cloud-furnace-clipped.pbrt`, every 8x8 block within 7% of the sky; the checker is verified to fail on the old picture, 14% low). The OptiX shaders were not looked at: if they also track along the whole box, the same scene shows it.

## Preview live: what the OptiX side must do (Mac, 2026-10-10)

The Scene Builder's **Preview live** button saves `<name>-live-preview.pbrt` in the scene-list folder and (re)starts Live Preview on it; pressing it again after an edit rewrites that same file under the same scene id. The Metal library notices (it keeps the file's modification time and size with the session and rebuilds when they change). OptiX caches the loaded and built scene by path (`s_pbrtLoadCache`, `s_pbrtBuiltSceneCache` in `gpu/optix/scene_builder.cpp`), so it would show the OLD scene after an edit: the cache key needs the file's modification time and size as well (and the object offsets, which it already needs, see "Moving objects in Live Preview: the OptiX half"). Same for the Windows GUI's "Edit in Builder": nothing renderer-specific there.

## Bump and normal maps: pbrt's BumpMap math, for Metal (Windows, 2026-10-13)

Real pbrt-v4 differs from the old bump code in three ways, fixed for the CPU and both OptiX backends by `apply_bump_map_pbrt` and `triangle_dndu_dndv` (`src/shared/normal_map.h`, CPU_GPU: copy them, or port them, to Metal). Call: `apply_bump_map_pbrt(disp, disp_u, disp_v, du, dv, n_authored, smooth, dpdu, dpdv, dndu, dndv)`. (1) The normal passed is the shading normal AS AUTHORED (not turned toward the ray), `smooth` is true for a triangle with vertex normals, dpdu/dpdv are the triangle's own (unnormalised, from the 2x2 uv solve), dndu/dndv come from `triangle_dndu_dndv` on the three vertex normals (zero without vertex normals). (2) The result is on the authored normal's side; then, for BOTH a bump map and a normal map, **turn it toward the viewer** (`dot(n, ray_dir) > 0` means negate): pbrt's BSDF is two-sided about the shading normal, so a viewer behind the perturbed plane is lit as if the normal faced them. This is what made height maps 3-10% dark and tilted normal maps 1-2.5% off on curved shapes (block differences up to 0.47) and was the whole of the old 'steep height map seen obliquely' gap. (3) A normal map's frame uses the authored normal too. Check: `RT_COMBO_SCENES_DIR=<dir> ray_tracer_tests --gtest_filter='SceneCombosTest.*'` and `scripts/combo_sweep.py --only heightmap,normalmap` (the CPU is the reference, within pbrt to 0.2%).

## Instances written by the Scene Builder (Windows, 2026-10-13): Metal to check

The Builder now writes copies of a heavy shape (cone, torus, capsule, dome, tube, rounded box, terrain, `.ply` mesh) as `ObjectBegin "instance-N"` with the material and mesh once and `AttributeBegin / Translate / Rotate / [Scale s s s] / ObjectInstance / AttributeEnd` per copy (`scene_instancing.h`, `scene_pbrt_writer.h`). A uniform `Scale` can sit before an instance. On Windows the CPU and both OptiX backends draw such a scene exactly like the plain one (the CPU's `transform_instance` needed two fixes: the outward normal, so glass and subsurface seen from inside are right, and `dpdu`/`dpdv`). On the Mac please render the `inst_*` and `flat_*` pairs (`RT_COMBO_SCENES_DIR=<dir> ray_tracer_tests --gtest_filter='SceneCombosTest.*'`, then `scripts/combo_sweep.py --dir <dir> --only inst_,flat_`) and check each pair alike: glass and subsurface inside an instanced mesh, a normal map and a height map on one, and a scaled copy (scale 0.7). The Rendered viewport numbers objects as the renderer lists them, the plain objects first and the instance placements after (`scene_viewport.h translationOnly`); a drag of an instanced copy must move that copy.

## Subsurface roughness: Metal still ignores it (Windows, 2026-10-12)

pbrt's `Material "subsurface"` `roughness` roughens the entry interface. CPU (`subsurface::scatter_rough`, `material_pbrt.h`), recursive OptiX (`MaterialType::Subsurface` in `optix_device_shade.h`) and wavefront OptiX (`wavefront_kernels_materials.cu`) now take one GGX dielectric sample instead of the smooth Fresnel choice: `RoughDielectricBxDF<T>::sample_weighted_local` (`bxdfs_conductor.h`, shared CPU_GPU) returns the direction and the weight G2/G1 (importance-transport convention, no 1/eta^2, the same as the smooth entry's weight 1). A refracted sample goes on to the probe walk with that weight in the throughput; a reflected one continues as a bounce with the weight. No direct-light sample is taken at the rough entry vertex. alpha is `remap ? sqrt(roughness) : roughness` per axis (`roughness_u`/`roughness_v`; the OptiX builder stores them in `MaterialData::roughness`/`roughnessV`, `remapRoughness` is already set). Metal's `shadeSubsurface` needs the same: replace the smooth dielectric choice when alpha is not effectively smooth, multiply the entry weight into the throughput before the probe, and read `roughness`/`remaproughness` in `mapPbrtMaterial`. Check: skin-coloured and grey spheres at roughness 0.35 (`reflectance 0.8 0.58 0.48`, `mfp 0.14 0.055 0.036`, eta 1.4) against real pbrt-v4 with `Integrator "volpath"` (the CPU reads 0.747 0.551 0.459 in a window where pbrt reads 0.748 0.550 0.457).

## Subsurface exit normal: the root cause of "darker under a constant sky" (Mac, 2026-10-10); OptiX needs the same change

The 15-20% difference noted under "Subsurface on Metal" was not about sampling a constant sky. A constant sky and the same sky given as a picture gave different Metal pictures (0.31 and 0.38 for a ball filling the frame), which no consistent renderer does; the CPU gave the same value for both (0.354 and 0.357) only because it samples both by NEE through the ball. The cause: the BSSRDF probe's exit point was given the normal that FACES the probe ray (`hit_record::normal` on the CPU, `facingNormal` on Metal, the "front-face-corrected shading normal" in the OptiX probe payload). A probe that finds the far side of a closed shape hits it from inside, so that normal points into the object, and the exit BSDF (Sw, NormalizedFresnel, in the normal's hemisphere) sent the light into the object. pbrt's `SubsurfaceInteraction` carries the shape's own outward normal. Fixed on the CPU (`camera_path.h`, `rec.normal = exit_hit.front_face ? exit_hit.normal : -exit_hit.normal`) and on Metal (`probeB.normal` instead of `probeB.facingNormal`). With it: the Cornell box's marble ball reads 60.7 -> 76.6 of 255 on the CPU, Metal matches the CPU within 1% there (window means in `metal_poc_subsurface_check.cpp`, re-measured), a ball filling the frame under a uniform sky reads 0.555 on the CPU and 0.570 on Metal for a constant sky and a picture sky alike (`ctest -R metal_poc_subsurface_sky`, scenes `test_scenes/subsurface-sky-*.pbrt`).

**OptiX (Windows session):** `__closesthit__probe_sphere/_quad/_bilinear_patch/_triangle` in `gpu/optix/optix_probe_hit.h` write the front-face-corrected normal into probe payload registers p4..p6, and `bssrdf_probe_walk()` hands it to the exit BSDF. It must be the outward normal (the unflipped one) for the exit BSDF; the pdf terms only use `fabsf(dot(exit_n, axis))`, so the sign does not matter there. Expect subsurface scenes (B16, layered-materials, the SSS dragons) to come out about 25% brighter, which brings them to the CPU's new values, and the CPU-versus-OptiX baselines for those scenes to be re-measured. Not changed from the Mac: it cannot be compiled or run there.

## OptiX: run the consistency sweep (Mac, 2026-10-10) - it will tell you what is left

`scripts/consistency_sweep.py` ([docs/CONSISTENCY_SWEEP.md](CONSISTENCY_SWEEP.md)) renders 312 pairs of scenes that must look alike and prints the ones that do not. On the Mac it found and fixed about ten bugs in Metal and three in the shared loader (see that page). It takes the same command line on Windows, and `--gpu` is OptiX there, so this is the cheapest way to find what the OptiX side still gets wrong, before reading any code:

```
python scripts\consistency_sweep.py --exe x64\Release\ray_tracer.exe --backends cpu,gpu
python scripts\consistency_sweep.py --exe x64\Release\ray_tracer.exe --backends gpu --only sky,skyrotate,shapeform -v
```

(Python 3.7 or later; nothing else to install. If `x64\Release\ray_tracer.exe` is not where the GPU build lands, pass its path.) Exit code 1 means a pair is off. Add `--verbose` to see every pair's numbers. Not run on Windows yet, so some failures may be the script's own assumptions (path handling, the `--seed` option) rather than OptiX.

What the Mac side already knows is worth checking on OptiX, in the order I would look at them (each is a pair in the sweep that failed on Metal before the fix):

1. **Subsurface exit normal** (`optix_probe_hit.h`, described above): the `sky` pair for `subsurface` and the `cpu vs gpu` pair should show it, about 25% dark.
2. **Sphere-clipped media**: a cloud or grid whose box sticks out of its interface sphere (`sky`, `scale` and `backends` pairs for `cloud` and `rgbgrid`). A pbrt medium fills the interface sphere, not the box.
3. **Coated diffuse / coated conductor under an image sky** (`sky` pair): Metal lacked environment light sampling in these two shaders. Check OptiX's `coateddiffuse` and `coatedconductor` next-event estimation against its escape-ray MIS weight.
4. **Fog, clouds and grids under an image sky** (`sky`, `skyrotate`): same cause in Metal (the escape weighted for a light sample never taken). OptiX's medium scatter code may or may not sample the picture.
5. **An instanced sphere** (`shapeform`, "ball written as instance"): Metal skipped every instanced sphere.
6. **A picture sky turned with the scene** (`skyrotate`) and **a distant light against a point light 1000 units away** (`lights`): orientation and delta-light checks that nothing else covers.

Fixed in shared code (nothing to do, but they change OptiX pictures): `Material "subsurface"` with `reflectance` + `mfp` now inverts the reflectance (it used the default coefficients on every backend); a `bilinearmesh` with four `integer indices` picks its corners by them; a `mix` texture of plain colours leaves its blend as the flat colour; `gpu_scene_textures::applyImagemapEncoding` (new, used by Metal for `encoding "linear"`/`gamma`/`invert` images; OptiX's `decodeColourImage` already handles them). `pbrt_flatten.h` now includes `bssrdf.h`, which brings the global `Frame<T>` of `vec3_frame.h` into every translation unit that includes the flattener: if a Windows file defines its own global `Frame`, rename it (two test files needed that on the Mac), and please confirm the MSVC build is clean.

### Windows session reply (2026-10-10): the sweep on OptiX

Run: `python scripts\consistency_sweep.py --exe <full path to>\ray_tracer.exe --backends cpu,gpu` (472 pairs now; the script needs an ABSOLUTE exe path, it runs the renderer from the repo root), once for the recursive backend and once with `RAY_TRACER_WAVEFRONT=1`. It ran as it is on Windows. After the fixes below both backends read **0 off by more than 4%**. The MSVC build was clean (no global `Frame` clash). What it found, in the order of your list:

1. **Subsurface exit normal** (item 1): confirmed, subsurface was 5-12% dark. `optix_probe_hit.h` (recursive) and `wavefront_probe.h` (wavefront) now give the exit BSDF the shape's OUTWARD normal, not the one facing the probe ray. Two more causes of the same gap, both specific to OptiX: `shade_normalized_fresnel` sampled no punctual (point, spot, distant) lights at the exit, only area lights and the sky; and the shadow any-hits counted a Subsurface sphere (and, on wavefront, every shape) as an occluder, where the CPU's `subsurface::is_shadow_transmissive()` lets NEE through. A ball filling the frame now reads 0.994 of the CPU. The baselines of the subsurface scenes were not re-measured here (see below).
2. **Sphere-clipped media** (item 2): confirmed for `rgbgrid` and `cloud` on both backends (6% dark floor). The shadow ratio tracking ran through the whole grid box; it is clipped to the interface sphere's (or box's) chord now (`shadow_medium_sphere_chord` in `optix_anyhit_shadow.h`, `wf_medium_sphere_near_far` in `wavefront_intersection_sphere.h`). A `ClippedSphere` is not clipped (it packs no attributes).
3. **Coated diffuse / conductor under an image sky** (item 3) and 4. **fog, clouds, grids under an image sky**: already right on OptiX, nothing to do.
5. **Instanced sphere** (item 5): on OptiX an instanced sphere was drawn, but (a) a scene with only instances stopped with "Scene contains no geometry" (`buildCustomAabbs` did not count placements), (b) an instanced FOG ball was not built at all: `pbrt_gpu_builder.h`'s instance branch never looked at the sphere's `MediumInterface`, so the ball was invisible, and (c) once built, the medium code (closest hit, both shadow any-hits, both backends) took the sphere's chord against the WORLD ray although centre, radius and box are in the shape's own space. All three fixed; an instanced fog ball is now identical to the plain one.
6. **`skyrotate`, `lights`**: pass.

`make_look_at` (shared): the `camera` group passes on OptiX, so the orthographic/spherical/realistic cameras agree with the CPU.

One pair is borderline on the wavefront backend and only there: `camera`, "spherical camera, scene mirrored" (4.7% over 8 seeds against a 5% tolerance, 8% on seed 3). The picture is the same: with the lamp removed plain and mirrored agree to 4 digits on CPU, recursive and wavefront, so what differs is only the few pixels that show the 318-radiance lamp directly, which the wavefront sampler places differently. Not treated as a bug; if it flickers, give that pair a lamp-free scene.

That pair ("spherical camera, scene mirrored") was borderline on the wavefront backend only; the Mac session made it lamp-free (the few pixels that show the 318-radiance lamp directly were all that differed) and it now agrees to four digits on CPU, recursive and wavefront.

Subsurface baselines, measured 2026-10-11 (EXR means, 160 x 160, 128 samples, depth 8, seed 3; `rec` is the recursive and `wf` the wavefront OptiX backend):

| scene | CPU | rec | wf | rec / CPU | wf / CPU |
|---|---|---|---|---|---|
| subsurface-ball | 0.1339 | 0.1341 | 0.1337 | 1.002 | 0.999 |
| subsurface-slab (B13) | 0.1141 | 0.1137 | 0.1130 | 0.997 | 0.991 |
| layered-materials | 0.1649 | 0.1650 | 0.1633 | 1.001 | 0.990 |
| cornell-wax-slab | 0.1628 | 0.1627 | 0.1602 | 0.999 | 0.984 |
| sssdragon/dragon_10 | 0.0554 | 0.0553 | 0.0553 | 0.998 | 0.999 |

All within 1.6% of the CPU, so the old "about 25% brighter" note is only the CPU's and Metal's own move for a ball filling the frame, not a gap to expect on OptiX: the unit suite's CPU-versus-OptiX subsurface comparisons pass with the standard tolerances (the B13 exception stays retired). `scripts/run_optix_sweeps.ps1` now runs the sweep on both OptiX backends in one command. Metal's own `--wavefront` equivalent does not exist.

## Texture UV mapping (imagemap uscale/vscale/udelta/vdelta): the Metal half (Mac session, 2026-10-12: done)

Metal now looks the reflectance image up at `(su * u + du, sv * v + dv)` before the wrap, and the bump or normal map image the same way. `TriangleMaterial` (`metal_poc_gpu_types.h`, mirrored in `metal_poc_types.metal`) carries `uvScaleU/V`, `uvDeltaU/V` (reflectance: image Lambertian and the DiffuseTransmission reflectance image, set from `Material::textureMapping` by `applyReflectanceMapping()` in `metal_poc_pbrt_materials.mm`) and `bumpUvScaleU/V`, `bumpUvDeltaU/V` (set from `Material::displacementMapping` in `applyImageBump`, `metal_poc_pbrt_loader.mm`). `imageBumpNormal` evaluates each of its three height lookups at the mapped coordinates and keeps the finite-difference step in the surface's own units, like `gpu_sample_displacement`. The transmittance image and the other image slots still use the surface coordinates, as on the CPU.

**A disk and a cylinder with a picture on Metal.** Metal's analytic disk and cylinder carry no (u, v), so a picture, checker or bump map on one was never drawn (the disk came out white). `TessellationCaps::texturedQuadrics` (on in `TessellationCaps::all()`, so Metal and `scene_size.h`; off for OptiX, which draws them natively) now turns a disk or cylinder whose material uses the surface coordinates (a reflectance picture, a checker, a bump or normal map) into triangles with pbrt's own coordinates; a light, a shape in a medium or a moving one stays analytic. `tessellateDisk` now gives pbrt's `v` (0 at the rim, 1 at the inner radius; it had run the other way, which no one could see before). Cones, paraboloids and mesh files were already triangles and simply gain the mapping.

**The check:** `python scripts/consistency_sweep.py --exe <ray_tracer> --backends cpu,gpu --only texturemap` is 0.0% off on every backend, with the tolerance tightened to 1% (an unmapped Metal sphere was 5.1% off, an unmapped bump 3.6%), and a bump map on a square was added to the group. Metal's disk and cylinder pictures also match the CPU by eye. Windows/OptiX: nothing for you to change; the Apple-only warning in `scene_validate.h` and the note in `scene_builder_inspector.cpp` are deleted (with their translations and the `scene_uv_tests.cpp` expectation). `uvIsBaked()` / `UvPlace` in `scene_pbrt_writer.h` are left for you: a box and a quad would then take the mapping on their Texture like the other shapes, which you can verify on OptiX. One warning that stays true: `scene_validate.h` still says the GPU renderers apply a bump map to triangles only; on Metal a bump map now also shows on a disk, cylinder and cone, but not on a sphere (it has no triangle coordinates), so reword that if OptiX is the same.

**Windows reply (2026-10-12).** Two things came out of the handoff.

* **`uvIsBaked()` / `UvPlace` stay.** The renderers' `UVMapping` is read for an `imagemap` only (the reflectance picture and the bump or normal picture). A checker has no mapping, so on a box, a quad and the ready-made shapes the placement (repeats and *shift*) is still written into the mesh's `uv` for the checker; dropping the baking would lose a checker's shift on those shapes (a sphere, disk, cylinder, cone and mesh file never had it, which the inspector's rows say). The pictures and bump maps on a box or quad could take the mapping on their Texture, but then both ways would be live, so the baked path is the simpler one. If a checkerboard ever reads `udelta`/`vdelta` in `pbrt_flatten_materials.h`, `UvPlace` can go.
* **The bump warning was wrong on OptiX, in a worse way.** Checked on OptiX (recursive and wavefront) against the CPU with a hand-written scene: a *normal map* on a disk or a cylinder did not just go undrawn, it hit `__trap()` ("MaterialType 14 is not supported on disk geometry") and failed the whole render; it is shaded now (`dc_normal_mapped_normal`, `optix_intersection_disk_cylinder.h`, and the wavefront gate in `wavefront_device_common.h`), and a tilted map on a disk reads the same on the CPU, recursive and wavefront. A normal map on a sphere was already drawn. A *height map* is drawn on triangles only: quad 182 on all three backends, but a disk 229 and a sphere 229 on OptiX against 95 and 94 on the CPU (229 is the unperturbed value); a cone is drawn as triangles on OptiX and agrees with the CPU (a height map on the cone's flank reads within one level of it in five cases; a first test that looked 152 against 90 had the camera looking straight at the apex, where the surface is singular and a 48-segment tessellation cannot match the analytic dpdu). `scene_validate.h` now says exactly that: a sphere never shows a height map on a GPU; a disk or cylinder only on Metal; a normal map is not warned about; a cone is not warned about.

**Handoff for the Mac session (2026-10-12): the bump map's pixel footprint.** pbrt scales camera ray differentials by `max(1/8, 1/sqrt(samples per pixel))` (`RayIntegrator::EvaluatePixelSample`), which sets a bump map's finite-difference step; the CPU (`camera_render.h`, `ray_diff_scale`) and both OptiX backends (`gpu_bump_map.h`: `GpuBumpFootprint::diffScale`, set from the launch's samples per pixel) do now, and a tiled height-map ramp on a quad then matches real pbrt-v4 in every block. Metal's bump step (`imageBumpNormal`'s footprint) still uses the whole pixel; give it the same scale, with the sample count of the whole render, and check with `scripts/pbrt_compare.py` is not possible on a Mac without a pbrt build, so use the CPU as the reference (`python scripts/combo_sweep.py --only heightmap --skip-wavefront`).

**Mac session, 2026-10-12: the bump footprint is done on Metal, and emitters now agree.** `bumpFootprintStep` (`metal_poc_sampling.metal`) offsets its two rays by `max(1/8, 1/sqrt(uniforms.samplesPerPixel))` of a pixel, the sample count of the whole render (`--only heightmap` in the combo sweep was 1.2 to 1.3% off on a box, stairs, wedge and pyramid after your change and is clean now: only the known sphere line remains; the consistency sweep is 560 pairs, 0 off, ctest 24/24). A height map on a very coarse picture is still up to about 4% off the CPU on Metal in the synthetic `maps` group (a limit of 5% there), so it is close, not identical: pbrt itself could not be compared on a Mac.

**Emitters:** the writer now writes `Material "diffuse" "rgb reflectance" [ 0 0 0 ]` for an emissive object instead of its own material (`scene_pbrt_writer.h`; the document keeps the material it was given, and the starter scene's black ceiling light is written exactly as before). The CPU and OptiX draw a light shape as an emitter only, Metal and real pbrt reflect, so a coloured emitter was 15.5% off on Metal (`quad__emitter`) and, as your comparison found, off real pbrt too; with a black body all of them agree. In the combo sweep `quad__emitter` and `terrain__emitter` are gone from Metal's list. `scripts/pbrt_compare.py` should lose its emitter line. The remaining Metal-only lines are `*__mix_picture` (0.5 to 3%, Metal lower, not investigated) and a disk lying on the floor (depth tie).

**Handoff for the Mac session (2026-10-12): pbrt's left-handed camera.** pbrt draws world +x on the LEFT of the picture for the same `LookAt`; this project's perspective camera drew it on the right (found by comparing with a real pbrt-v4 build: `docs/PBRT_SUPPORT.md`, "Compared with real pbrt-v4"). The CPU and OptiX now follow pbrt, so a pbrt file drawn on Metal is the mirror of the CPU's until Metal does too. What to change: `pbrt_flatten::Camera::leftHanded` (true for a plain `LookAt`, false once the file mirrors the camera with `Scale -1 1 1` before it; the Scene Builder's writer does, and a Builder file without that line keeps its old picture, `isLegacyRightHandedBuilderFile`) says when the picture's horizontal axis runs the other way; the Metal loader's `cameraRight` (built as `cross(forward, up)`) must be negated for the perspective camera when it is true (the orthographic, spherical and realistic cameras already use pbrt's basis there), the same one-line idea as `camera::mirror_x` (CPU, `compute_lookat_basis`) and `compute_lookat_basis_gpu(..., mirror_x)`. Check with `python scripts/combo_sweep.py --dir <dir> --skip-wavefront` (the scenes the unit tests write with `RT_COMBO_SCENES_DIR`, the Builder's, are right-handed with the mirror; strip the `Scale -1 1 1` and `@rt-builder-doc` lines for pbrt's own) and the red-sphere test in `pbrt_handedness_tests.cpp`: a sphere at +x with a plain LookAt must be in the left half. `CameraConfig::left_handed` (and `cam_left_handed` in the scene metadata) is what the GUI uses to turn Live Preview's orbit and strafe the right way round.

**Mac session, 2026-10-12: coated paint with a picture on Metal.** The `*__mix_picture` combo lines were not about the mix: a `coateddiffuse` whose reflectance is an imagemap (materialType 27) was shaded by Metal's old single-bounce clearcoat (fixed IOR 1.5, coat roughness ignored), +7.3% against the CPU on a sphere (+4.3% inside a mix), and `applyReflectanceMapping` was never applied to it, so its repeats and shift were lost (my #417 missed this path). It is now the layered coat of materialType 19 over a base colour that is the sampled picture (`ior` the coat's IOR, `conductorEta.x` its alpha), with the mapping: coated + picture is -0.5% against the CPU, the mix 0.1%. New sweep pairs: the coated picture tiled by hand (`texturemap`), and coated + picture CPU against GPU (the `maps` group, now also on a sphere). OptiX and the CPU needed nothing.

**Mac session, 2026-10-12: pbrt's left-handed camera is done on Metal.** `MetalPocApp::pbrtPerspectiveLeftHanded` (set in `loadPbrtCamera`: `leftHanded` for the perspective camera, `!leftHanded` for the orthographic, spherical and realistic ones, whose kernels already carry pbrt's signs for a plain LookAt, so a mirrored file needs the opposite) negates `pbrtCameraRight` (`applyCameraOverride` and `applyCameraLookAt` too, so Live Preview's orbit, its picture and `out_camera_basis`, which picking uses, agree); the orbit-blur camera follows the host's sign. A red sphere at +x is at pixel 11 on the CPU and Metal with a plain LookAt, and at 52 with the mirror, for the perspective camera; the orthographic and spherical cameras agree with the CPU too. Everything that had a hard-coded picture position was mirrored: the subsurface and alpha-cutout checkers, the golden snapshot (`parity_golden.txt`: of its 27 drifted scenes 23 are exact mirrors of the old numbers, the others are the coated picture and noise in symmetric scenes; the CPU parity sweep passes 56/56). The sweep's far-perspective-against-orthographic pair is 96 pixels now (a lamp a few pixels wide made one noise draw read 6.5% off at 48). ctest 24/24, the GUI self-tests with Live Preview pass.

**Mac session, 2026-10-12: a disk lying exactly on a triangle (a Scene Builder disk on the floor).** The disk and the floor tie to the last bit. Metal always gave the floor (no disk at all: its colour or picture was missing, `disk__coated` and friends in the combo sweep) and the CPU a speckle of the two (pixel by pixel, which of the two a ray found first). Fix, the same idea in each: the disk is looked for a hair further than the closest hit so far and reported a hair in front (`disk_cylinder_hittable.h`: 1e-9 of its distance in double, looking out to 1e-7 more; `diskIntersectionFunction` in Metal: 2e-5 in float), and a disk drawn as triangles (Metal's textured disks, an instanced disk) is lifted 1e-4 of its radius (`quadric_tessellate.h`). The consistency sweep has a pair for it (`--only shapeform`: a disk on the floor against the same disk 0.01 above: 0.4% on the CPU, 0.2% on Metal; with no disk the picture is 15% different, so the pair would see it missing). **OptiX is untouched and probably has the same tie** (`optix_intersection_disk_cylinder.h`; I cannot compile or run it): check with that pair and, if it fails, report the disk a hair in front in its intersection program as above. Combo sweep on Metal: 232 scenes, only the known sphere lines left.

**Windows, 2026-10-13: OptiX had the same tie, now fixed on both backends.** The sweep pair was 7.4% off on the recursive and the wavefront backend (the floor won the tie, so a picture or colour on the disk was missing in patches, `disk__picture`, `disk__coated`, `disk__mix*` 1.2-3.6% off the CPU). `__intersection__disk` and `__intersection__wf_disk` (`optix_intersection_disk_cylinder.h`, `wavefront_intersection_disk_cylinder.h`) now report the disk 3e-5 of its distance in front (at least `ray_tmin`) and compare THAT with `ray_tmax`: the test against the closest hit found so far was the real culprit (a triangle found first leaves `ray_tmax` a rounding error short of the disk's own t, which rejected the disk outright; a bare shift of the reported distance changed nothing). The pair is under 4% and the 15 `disk__*` combination scenes agree with the CPU on both backends.
**Mac session, 2026-10-13: bump and normal maps on a sphere, on Metal.** A sphere with a bump or normal map was drawn flat on Metal (its analytic sphere has no tangent frame), the last gap in the combo sweep. `TessellationCaps::texturedQuadrics` now also turns a full, still, unclipped sphere that is not a light or in a medium and whose material has a displacement image into 96 x 48 triangles with this project's sphere coordinates (`quadric_tessellate.h` `tessellateSphere`: the poles on the world's y axis, `phi = atan2(-z, x) + pi`, `theta = acos(-y / r)`, exactly `get_sphere_uv`; a unit test pins them). A tilted normal map on a sphere reads the CPU's mean to 0.1% (it was unchanged from no map), a height map on a sphere 4.2 to 5.5% above the CPU's with the sweep's extreme 8-texel picture (a 6x height map on a ball: 166.6 against 166.3; the cause of the offset is not understood: the CPU's own dpdv is `cross(n, dpdu)` too, so it is not that). The Scene Builder's validation message now says "OptiX does not draw a height map on a sphere, disk or cylinder (Metal does)"; OptiX is untouched. Combo sweep on Metal: 232 scenes, none left (`sphere__heightmap` is still labelled KNOWN for OptiX).

**Mac session, 2026-10-13: pbrt's BumpMap math is on Metal (your handoff above).** `imageBumpNormal` (`metal_poc_sampling.metal`) is `apply_bump_map_pbrt`: the shading normal AS AUTHORED (`normal`, not the one turned to the ray), dndu/dndv from `triangleDnduDndv` (`triangle_dndu_dndv` on the three vertex normals and uvs), pbrt's `ts = cross(n, dpdu)`, `ss = cross(ts, n)` tangents when the triangle is smooth, the real dpdv otherwise, `disp * dndu` terms, the result on the authored normal's side; then, for a bump AND a normal map, turned toward the viewer (`dot(n, rayDir) > 0` negates). Two things Metal cannot know that the CPU does, handled by heuristics: whether a triangle HAS vertex normals (a triangle whose three corner normals are equal counts as flat, which only matters for an authored flat patch) and whether it has uvs of its own (the loader stores `(0,0), (1,0), (0,1)` for none, which `triangleDpduDpdv` treats as the CPU's no-uv case: unit first edge for dpdu and `cross(n, dpdu)` for dpdv; a mesh that authors exactly those uvs would differ). Result: a height map on a box 0.0 to 0.6% from the CPU (it was 1 to 3.5%), on a sphere 1.4 to 2.6% (was 4.1 to 4.4%: the missing `disp * dndu` term was the "4.6% not understood" I wrote the day before), a tilted normal map on a sphere 2.1% and on a box 1.2% (was 0.1% before your turn-toward-the-viewer change moved the CPU). The combo sweep on Metal: every non-instanced shape clean; the six `inst_*__heightmap/normalmap` lines are a bump map on an instanced mesh, which Metal does not draw (`!isPbrtInstance`; 0.4 to 1.0% off in the mean). ctest 24/24, the consistency sweep 572 pairs.
