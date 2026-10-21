# Metal backend: how it is built and how to change it

The living reference for `gpu/metal/`, the macOS GPU renderer. Read this first; the other documents answer narrower questions:

| document | answers |
|---|---|
| this file | architecture, the host/shader contracts, how to add things, how to test, what the environment switches do |
| [`METAL_PARITY_STATUS.md`](METAL_PARITY_STATUS.md) | how closely Metal matches the CPU renderer, how that is measured, known differences, the large-scene sweep |
| [`MAC_LIVE_PREVIEW.md`](MAC_LIVE_PREVIEW.md) | the interactive Live Preview (the persistent session, performance) |
| [`history/METAL_GPU_FEASIBILITY.md`](history/METAL_GPU_FEASIBILITY.md) | the development diary: 200+ numbered sections of what was tried, measured and decided, in order. Source comments cite it as "section N". Search it for the *why* of an old decision; do not read it to learn how things work today |

## What it is

A Metal ray-tracing path tracer (hardware ray tracing on Apple GPUs) that renders the same flattened pbrt scene the CPU and OptiX renderers use
(`src/shared/pbrt_load.h` -> `pbrt_flatten.h`). It is one megakernel, `primaryRayKernel`, with the materials, lights and media as device functions
around it. The shader source is **compiled at run time** from the `.metal` files (no offline `.metallib`), so editing a shader needs no rebuild.

## Files

`gpu/metal/` has three folders and one header: `metal_interface.h` (the C API the launcher calls), `host/` (the Objective-C++ renderer: `MetalPocApp`, the pbrt loader,
buffer upload, dispatch, Live Preview), `shaders/` (the `.metal` sources, the two headers valid in both languages, and the list of files to concatenate) and `tests/`
(shader unit tests, math and loader tests, the checks and their `.sh` drivers, `test_scenes/`, the parity golden and the pbrt-pack baseline). The tables below name
files without the folder. The `metal_poc_` prefix is history (the backend began as a proof of concept); it is on every file and has no other meaning.

| file | role |
|---|---|
| `metal_poc_main.mm`, `metal_poc.mm` | the `metal_poc` CLI and `MetalPocApp`: argument parsing, the hand-built demo room (`buildScene`), post-processing and writing the image |
| `metal_poc_pbrt_loader.mm`, `metal_poc_pbrt_loader_punctual.mm` | `loadPbrtScene()`: flattened pbrt scene -> host arrays (geometry, area lights, spheres and the media they bound, infinite lights, media, camera); the second file holds the point/spot/distant/goniometric/projection lights |
| `metal_poc_pbrt_materials.mm` | pbrt material -> the shader's `TriangleMaterial` (`mapPbrtMaterial`, `mapPbrtDiffuseMaterial`), measured-BRDF tables |
| `metal_poc_gpu_resources.mm` | uploads buffers and builds the acceleration structures |
| `metal_poc_dispatch.mm` | `compileShaderAndDispatch()`: eight named stages (library, pipeline, targets, textures, sampling tables, uniforms, resource check, per-frame dispatch) over a `DispatchState` |
| `metal_poc_scene_data.h`, `metal_poc_loader_tests.mm` | `PbrtSceneLoader`: the host-side scene arrays and the `loadPbrt*` / `mapPbrt*` methods (`metal_poc_pbrt_*.mm`), which need no GPU device; `MetalPocApp` derives from it. The tests load bundled scenes and check the arrays |
| `metal_poc_app.h`, `metal_poc_gpu_types.h`, `metal_poc_host_math.h`, `metal_poc_scene_helpers.h` | `MetalPocApp` (the device, buffers, acceleration structures, shader and output), the host copies of the shader structs, math, scene-building helpers |
| `metal_poc_material_ids.metal` | the `METAL_MAT_*` names of every `materialType` (included by host and shader) |
| `metal_poc_shared_types.h` | the light, shape, lens and medium structs: one definition for host and shader |
| `metal_poc_types.metal` | the shader copies of `Uniforms` and `TriangleMaterial`, intersection functions |
| `metal_poc_sampling.metal` | camera and pixel-filter sampling, hemisphere sampling, shading normals, texture lookups, shadow-ray tracing (`traceShadowAny*`) |
| `metal_poc_environment.metal`, `_procedural.metal`, `_bsdf_math.metal`, `_media_math.metal`, `_area_lights.metal` | environment/portal lights; checker, noise and marble; Fresnel, Beer-Lambert and GGX; Henyey-Greenstein, cloud and RGB-grid density; `sampleAreaLight()` and sphere-light cone sampling (all split out of `metal_poc_sampling.metal`) |
| `metal_poc_bump.metal`, `metal_poc_layered_bxdf.metal` | image bump and normal maps; layered BxDF code |
| `metal_poc_materials_*.metal` | one `shadeXxx()` per material family (specular, diffuse, layered, extra, medium, hair) |
| `metal_poc_kernel_state.metal` | the three bundles `primaryRayKernel` passes around (`KernelRes`, `PathState`, `BounceState`) and the macros that give their members their old names |
| `metal_poc_kernel_camera.metal`, `_media.metal`, `_surface.metal`, `_bounce.metal` | the kernel's steps: camera ray and fresh path state; bounded and global media; escape, hit resolution, normal perturbation, albedo, emission; scattering by material, glass-medium bookkeeping and `traceBounce()` |
| `metal_poc_kernel.metal` | `primaryRayKernel` itself: setup, the per-sample loop (path regeneration), accumulation, adaptive sampling |
| `metal_poc_shader_files.h` | the ordered list of shader files concatenated into one source string |
| `metal_live_preview.mm/.h` | the persistent per-scene session behind Live Preview (`realtime_renderer.dylib`) |
| `metal_poc_test_kernels.metal`, `metal_poc_shader_tests*.mm`, `metal_poc_math_tests.cpp`, `metal_poc_validate.cpp`, `metal_poc_crop_check.cpp`, `metal_realtime_dylib_check.cpp` | tests (see below) |
| `metal_cpu_gpu_parity_check.cpp`, `parity_golden.txt` | the CPU-vs-Metal sweep and its golden snapshot |

## How the kernel is structured

`primaryRayKernel` used to be one 1,631-line function. It is now about 300 lines of setup and the per-sample loop; each bounce is `traceBounce()`,
which calls, in order: `traverseBoundedMedia`, `scatterInGlobalMedium`, then for a surface hit `resolveHit`, `perturbShadingNormal`, `computeAlbedo`,
`addHitEmission`, `scatterAtSurface` (the `shadeXxx()` of the material) and `updateMediumStateAfterBounce`; an escaping ray goes to `shadeEscapedRay`.
A step that can end the path returns `bool` (false = the path ends), the same convention the `shadeXxx()` functions use.

The steps share state through three bundles instead of fifty parameters: `KernelRes` (every texture, buffer, the acceleration structure and function
table; read-only), `PathState` (what a path carries between bounces) and `BounceState` (what one bounce computes). Each function starts with the alias
macros of `metal_poc_kernel_state.metal`, which give the members back their old names, so the bodies are the original kernel code unchanged. MSL inlines
all of it.

The split was checked against the kernel before it: all 12 Metal tests pass (shader tests, the 95-scene CPU/GPU parity sweep with the golden snapshot,
seed reproducibility), render time is unchanged (A1 4.4 -> 4.1 s, B2 5.4 -> 5.0 s, G1 3.2 -> 3.1 s at 400 px / 256 spp), and the pictures agree with the old
kernel's: mean absolute difference 0.37/255 over all 95 scenes at 64 spp, and the means of six scenes at 2048 spp agree to 0.005/255. They are **not
bit-identical**: re-inlining changed float rounding, so a few scenes' paths diverge (E1 differs by 1.4/255 at 64 spp, within its noise). Expect that when
changing the structure again; compare statistics, not hashes. D8 and D12 are not reproducible even run to run.

## Data flow of one render

1. `loadPbrtScene()` reads the scene and rescales it to ~2 units across (so the shader's fixed ray epsilons stay meaningful), then fills host vectors.
2. `buildGPUResources()` uploads them and builds the acceleration structures (triangles, spheres, disks, cylinders as custom intersection functions, instances).
3. `compileShaderAndDispatch()` concatenates and compiles the shader, builds the pipeline with its intersection function table, uploads textures and sampling
   tables, fills `Uniforms`, checks that no allocation failed (a nil buffer would otherwise silently bind nothing and render a *wrong* picture), then
   returns a re-runnable `renderFrame`. A single-image render runs it once, in row bands sized to about 0.3 s of GPU time (progress ticks and the GPU watchdog);
   Live Preview keeps it and calls it per frame after updating the camera/seed/sample count.
4. `postProcessAndWrite()` tone-maps and writes the PNG/EXR. Outside the `--crop` window the picture is black.

## The 31-slot ceiling and how data fits

Metal allows 31 buffer arguments per kernel and `primaryRayKernel` uses all of them (0-30), plus 9 textures:

| buffers | |
|---|---|
| 0 acceleration structure, 1 `Uniforms`, 6 intersection function table | |
| 2 triangle materials, 3 vertices, 7 normals, 8 uvs | triangle mesh |
| 4 sphere materials, 5 spheres, 13/14 disks, 26/27 cylinders | analytic shapes |
| 10/11 Suzanne's normals/materials, 12 instance table (per instance: normal matrix, translation, `triBase`) | instancing; pbrt `ObjectInstance` groups share the main triangle arrays, see the parity doc |
| 9 area lights, 15 point, 16 directional, 17 projection, 18 goniometric | lights |
| 19/20 environment marginal/conditional CDF, 22/23 the same for the pbrt image light | environment sampling |
| 21 GGX energy table, 24/25 lens elements / exit-pupil bounds | tables, realistic camera |
| 28 cloud media, 29 RGB-grid media, 30 shared float data | media and a general float buffer |

Textures: 0 output, 1 earth map, 2 goniometric, 3 pbrt environment, 4 pbrt goniometric, 5 projection, 6 area-light image, 7 diffuse image, 8 transmit image.

There is no spare slot, so new per-feature data **rides in existing buffers**: the portal light's tables live in the pbrt-environment buffers, measured
BRDF tables, bump-map images and grid media in `rgbGridData` (slot 30), and per-material parameters in unused `TriangleMaterial` fields. Each such
convention is documented where it is set (`metal_poc_pbrt_materials.mm`, `metal_poc_pbrt_loader.mm`) - for example `materialType` 25 is a *family* of
textures selected by `conductorK.y`, and glass bounding a medium carries its sigma_t in `conductorEta`.

## The host/shader contracts

The plain-data structs (the lights, spheres, disks, cylinders, instance transforms, lens elements, cloud and grid media) are written ONCE, in
`metal_poc_shared_types.h`, which is valid C++ and Metal Shading Language and is the second file of the shader source (the host `#include`s it). A new field
goes there, with its default. `Uniforms` and `TriangleMaterial` are still two hand-kept copies, `metal_poc_gpu_types.h` (C++) and `metal_poc_types.metal` (MSL).

* **Add fields at the END** of `Uniforms` and of any other mirrored struct, on both sides, with a safe zero-initialised default. Zero-initialised `Uniforms`
  (the shader tests) must mean "feature off".
* `metal_poc_shader_tests` runs `test_structLayouts`: the shader reports `sizeof`/`alignof` of 14 structs and the offsets of 17 `Uniforms` fields, and the
  host compares. A field on one side only fails it. When you add a mirrored struct, add it to both probe lists
  (`metal_poc_test_kernels.metal`, `metal_poc_shader_tests_layout.mm`).
* `materialType` values are the `METAL_MAT_*` macros in `metal_poc_material_ids.metal`. Never reuse a number. Metal's numbering is its own; OptiX has a
  separate enum.
* **A material's parameters beyond colour, ior and roughness** live in the spare slots of `TriangleMaterial` (`conductorEta`, `conductorK`, `transmitColor`),
  which began as a conductor's IOR. Nothing outside a conductor reads or writes them by those names: the loader uses the setters in
  `metal_poc_material_params.h` (`matparams::setHair`, `setPrincipled`, `setCauchy`, `setMeasuredTable`, `setSubsurface`, `setCoatAlpha`, `setChromaticMedium`,
  `setGlassMedium`, `setMediumBufferIndex`), the shader the views `matHairBetaN()`, `matCauchyA()`, `matMediumSigmaT()` ... after the struct in
  `metal_poc_types.metal`, which also has the table of what each family keeps in which slot. The storage and the picture are unchanged; a new family adds its
  setter and its views. A coated material's layer (thickness, medium albedo and g, maximum depth, samples) has fields of its own (`TriangleMaterial::layer*`, set by `matparams::setLayerMedium`, read by `layerMediumOf()`). A reflectance picture with a spherical, cylindrical or planar mapping has `posMapIndex` (1 + the offset of 21 floats in `rgbGridData`: the kind, the texture-from-Metal-world matrix, v1, v2, udelta, vdelta; `PbrtSceneLoader::claimPositionMapping`), read by `pictureLookup()`; a bump or normal map with such a mapping still uses its uv mapping. Several portal infinite lights: the first portal's frame, corners and size are in `Uniforms`, each further one has a 24-float header in the pbrtEnv conditional buffer after the first picture (`Uniforms::pbrtPortalExtraBase` / `Count`), with its picture and its distribution appended to the two pbrtEnv buffers; `pbrtEnvLeAt` sums them, `pbrtEnvSampleDirection` picks one uniformly and reports the mixture density, `pbrtEnvPdfAt` the mean of the in-window densities (as `portal_light_set.h`). A subsurface material whose reflectance is an image has `conductorEta.z = 1`, `color` = the mean free path (a length, times the scene rescale) and `transmitColor.x` = the picture's scale; `computeAlbedo` looks the picture up at the entry hit and `subsurfaceProbeWalk` inverts the BSSRDF table for it per channel (`bssrdfInvertRhoEff`, pbrt's `SubsurfaceFromDiffuse`). (The procedural-texture family, `materialType` 25, still reads `conductorK.y` as its mode and the rest per mode: see `shadeSurfaceAlbedo`
  in `metal_poc_kernel_surface.metal`.)
* `Uniforms` is read as a `constant` buffer; the loader and `dsFillUniforms` fill it. `liveRender` writes only camera/seed/sample fields per frame.

## Recipes

**A new material.** (1) Add `METAL_MAT_FOO` to `metal_poc_material_ids.metal`. (2) Write `shadeFoo()` in the right `metal_poc_materials_*.metal` (take
the closest existing one as the template: it must do the light sampling, MIS and continuation like its neighbours). (3) Add the dispatch branch in
`primaryRayKernel` (the `materialType ==` chain). (4) Map the pbrt material in `mapPbrtMaterial()` (or the CPU/hand-built scene helper). (5) Add a
`test_*` kernel and a `metal_poc_shader_tests_*.mm` check for the pdf/f when it has closed forms. (6) Add or pick a pbrt scene that uses it, run the
parity sweep, and re-snapshot the golden if the change is intended.

**A new `Uniforms` field.** Append it in both structs, give it a default, fill it in `dsFillUniforms`, add it to the layout probe if it is near the end, run the
shader tests.

**A new pbrt feature.** Teach `src/shared/pbrt_flatten*.h` first (the CPU and OptiX use it too), then map it in the loader. If Metal cannot support it yet, make
the loader warn and fall back rather than fail (see the existing "binds ... to a texture, which is not supported" warnings).

**A shader change** needs no rebuild: run the test or `metal_poc` directly. After a shader change the first Live Preview/GUI start recompiles the shaders (about 15 s once).

## Testing

| what | command | protects |
|---|---|---|
| everything | `cd build_macos && METAL_PARITY_STRICT=1 ctest` (~2 min, see [parity doc](METAL_PARITY_STATUS.md) for the build) | the rows below |
| device-side shader tests | `metal_poc_shader_tests` | BSDF/pdf/sampling functions against closed forms; the struct layouts |
| math tests | `metal_poc_math_tests` | host math |
| smoke/validate/crop/seed/exit-code | `metal_poc_smoke_*`, `metal_poc_crop_window`, `metal_poc_seed_reproducible`, `metal_poc_write_failure_exit_code` | the CLI contract |
| live-preview dylib | `metal_realtime_dylib`, `metal_realtime_dylib_other_cwd` | the C ABI the GUI loads; scene lookup from `/` |
| CPU vs Metal sweep + golden snapshot | `metal_poc_cpu_gpu_parity` | brightness/regions against the CPU, and drift against Metal's own previous output |
| large third-party scenes | `scripts/metal_large_scenes_sweep.sh` | the H1-H21 scenes load and render (needs the downloads) |
| the real GUI, headless | `python3 scripts/gui_selftest.py <App.app> --live-preview` | the packaged app, Live Preview frames and picture change |

The GitHub macOS runner has no hardware ray tracing, so CI builds and runs what it can and skips the device tests; on a developer Mac run the strict ctest
before every PR that touches `gpu/metal/` or shared headers (and rebuild after `git pull` first - a stale binary has produced a false "CPU bug" before).
A change that must not alter pictures (a refactor) should be checked with a byte compare of rendered PNGs against a build of the previous `main`.

## The first run on a Mac

Metal compiles the whole path tracer (every material and medium in one kernel) into machine code for the GPU the first time an app asks, and keeps the result for that app. On an M2 that
is about 90 to 120 seconds per shader variant (108 s and 121 s for the two on an M2 on 2026-10-15, so about four minutes for both; two variants: a scene with an any-hit triangle test, for an alpha mask or a subsurface material, and one without). It is the compute pipeline
creation in `createKernelPipeline()` (`metal_poc_dispatch.mm`) that takes the time; compiling the source is about one second. Everything after is a cache hit (no time at all), until the source,
the app or the OS changes.

So nothing waits for it on screen: `ray_tracer --prepare-gpu` and `metal_prepare_renderer()` build both variants and exit, the GUI runs them in the background at its start (`qt_gui/gpu_warmup.h`:
a low-priority thread for the library Live Preview uses, which runs in the GUI's own process, and a niced `ray_tracer --prepare-gpu` for the renders the Render button starts: each process has
a cache of its own), and a run that is still building after three seconds prints a line saying so. The status bar says "Preparing the GPU renderer for this Mac (first run only...)" while it
works. `RT_NO_GPU_WARMUP=1` turns the background build off; the GUI self-test never starts it, so `scripts/gui_selftest_macos.sh` now runs the bundle's `ray_tracer --prepare-gpu` first (measured once, 2026-10-15: after a real shader change the first self-test run then passed, where before the Scene Builder's Rendered viewport failed with "0 frames" until a second run); `gui_selftest.py` on its own still waits the full time on a new build.

**Where the cold-compile time goes (measured 2026-10-19, M2, both variants built in parallel, `--prepare-gpu`):** the full shader takes 127.5 s. With the `else if (mat.materialType == ...)` branches
of 12 rarely used families made `false &&` in `scatterAtSurface` (so the compiler drops the code, which is what a Metal function constant set to false would do), the same kernel takes **17.6 s**. Turning off one
group at a time: the four layered ones (coated diffuse, coated conductor, image clearcoat, normalized Fresnel) 69.7 s; measured, subsurface and hair 88.8 s; principled, Oren-Nayar, velvet, diffuse transmission and
clearcoat 85.0 s. The savings overlap (they do not add up), so no single family is to blame: the cost is the whole megakernel with everything inlined into it.

**What was done with that (2026-10-19): the pipeline is built for the families the scene uses.** `primaryRayKernel` has five function constants (`kFcLayered`, `kFcMeasured`, `kFcSubsurface`, `kFcHair`, `kFcExtras`,
`metal_poc_kernel_bounce.metal`; a constant the host does not set counts as true, so the shader unit tests and the offline compile see the full kernel). `dsBuildPipeline()` collects the families from the scene's
material arrays (`FamilySet`, `host/metal_poc_families.h`: a Mix material, which hides the two it mixes, takes everything) and `createKernelPipeline()` builds the kernel with exactly those; Metal caches
each distinct set separately. A scene of ordinary materials (diffuse, conductors, glass, textures, media, lights) compiles in about 16 s the first time instead of about 127 s; a scene with hair, say, builds
only the hair family on top. `ray_tracer --prepare-gpu` (what the GUI's warm-up runs for renders) builds that small renderer first for both variants, then the full one, so the first render of an ordinary
scene is ready in well under half a minute. **Live Preview always builds the full kernel** (`liveSession`, and `metal_prepare_renderer()` for its warm-up): a material can be changed to any family while it
runs, and rebuilding the pipeline mid-session would be a pause of up to two minutes. `METAL_FULL_KERNEL=1` forces the full kernel for a render (to compare a picture with and without the specialization).
The cost, as expected: someone who opens many different scenes compiles one pipeline per distinct set of families they meet (each at most as long as the full one, and cached from then on).

## Environment switches

The production switches below are parsed once, in `gpu/metal/host/metal_poc_tuning.h` (`MetalTuning`): a value that is not a whole number in range is ignored with a message (it used to go through `atoi`, so `METAL_TG_W=abc` was a threadgroup of width 0), and a run prints the switches it had when any differs from the default.

| variable | effect |
|---|---|
| `METAL_BANDS=N` | force N equal row bands instead of time-targeted ones |
| `METAL_REGEN=0\|1` | force path regeneration off/on (the host picks per scene: on below ~1500 triangles) |
| `METAL_CENSUS=1` | print per-scene lane utilisation and distinct materials per group-bounce |
| `METAL_TG_W`, `METAL_TG_H`, `METAL_TG_PRINT` | threadgroup shape override / print |
| `METAL_FULL_KERNEL=1` | build the pipeline with every material family, not only the ones the scene uses (to compare a picture with and without the specialization; the first run of a scene then compiles for about two minutes) |
| `METAL_PARITY_*` | the sweep: `STRICT`, `ALL`, `MODELS`, `ONLY_SCENE_ID`, `SEED`, `SPP`, `DEPTH`, `ADAPTIVE`, `GOLDEN`, `DUMP`, `KEEP`, `TIMING`, `WORKERS`, `SHARD` (see the parity doc) |
| `RT_METAL_POST_EFFECTS=list` | the PNG's optional finishing effects, from `vignette,aberration,denoise` (or `all` / `none`). Unset = just `denoise` (a bilateral blur that hides some noise: the Mac has no AI denoiser). The vignette (corners 18% darker) and the red/blue colour shift used to be applied to every Metal PNG although the CPU and OptiX renderers add neither, so the same scene looked different on a Mac; they are opt-in now. `.exr` output never gets any of them |
| `RAY_TRACER_USER_ASSETS` | the per-user folder the loader also searches for downloaded scene files |
| `RT_METAL_SHADER_DIR` (compile-time) | where the shader sources are found when running from a build tree; an installed app finds them next to the executable |

## Conventions worth knowing

* Roughness: the shader squares the roughness-style value it is given; the loader must store *perceptual* roughness, not alpha.
* There is a path-throughput ceiling (50 per channel, like the CPU and OptiX) and adaptive sampling is **off** unless asked for.
* Shadow rays are re-aimed from their offset origin (`reaimShadowRay`); clear/thin/rough glass blocks shadow rays like pbrt-v4; `Material "interface"` does not.
* Image skies put image row 0 straight up on all three backends (`envMapUV`).
* **`primaryRayKernel` is one ~1,600-line function on purpose, and splitting it costs speed.** Measured 2026-10-07 on an M2: moving only its first block (the 168-line camera-ray generation: filter sample, shutter time, orthographic/spherical/realistic/pinhole/orbit-blur rays, thin-lens depth of field) into an `inline` helper that takes the RNG and the outputs by `thread` reference made 8 of 10 test scenes 3-10% slower, reproducibly (the original code reproduced its own timings to within 1-2%), while one scene (realistic camera) got 18% faster; renders also changed at the bit level in some scenes (those same scenes already differ between a cold and a warm shader cache, so that part is not conclusive). The cost is the compiler's register allocation and inlining across the helper boundary in a megakernel with ~40 live path-state variables. So the host-side stages are split (`metal_poc_dispatch.mm`, `metal_poc_gpu_resources.mm`, the loader) but the kernel stays whole; if you try again, measure best-of-3 wall times on a camera-mode set and the material scenes, and expect to need a `PathState` struct for the whole loop rather than extracting pieces. `scripts/check_code_size.py` lists the kernel in its baseline for this reason.
* A change to the code *shape* of the kernel can make bit-stable scenes nondeterministic (it has happened); measure with the byte compare above.
* When Metal and the CPU disagree, prove who is right with an independent integral or an explicit-mesh copy of the shape before "fixing" either.
