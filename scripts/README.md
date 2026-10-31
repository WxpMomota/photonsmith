# Scripts

Utility scripts for building, packaging, and testing the ray tracer.

## Contents

### Building (local dev loop - shared RayTracer_Package/ folder)
- **`build_all.ps1`** - Build everything: launcher, renderers, tests, Qt GUI
  - Flags: `-Configuration Debug|Release`, `-SkipTests`, `-SkipGui`, `-Deploy`, `-Clean`
  - Stops a running RayTracerGUI.exe first; it locks the build output
  - Every individual-project MSBuild call passes `/p:SolutionDir` explicitly -
    see package.ps1's own comment on why (building a single .vcxproj instead
    of through ray_tracer.sln otherwise sends each project's post-build
    "deploy to RayTracer_Package" step to the wrong nested folder)
  - Usage: `.\scripts\build_all.ps1`
- **`build_all.bat`** - Batch equivalent
- **`build_and_deploy.ps1`** - build_all.ps1 followed by the Qt deployment
  step, for quickly running/testing your own local build - NOT for producing
  a release; use package.ps1 for that (below)
- **`build_and_deploy_macos.sh`** - macOS equivalent (CPU renderer + CLI + Qt
  GUI + the Metal GPU backend, into a `.app` and `.dmg`; `--arch universal`,
  `--skip-dmg`). Run on macOS, not from Windows.
- **`cuda_build.ps1`** - Called by `build_optix.targets`: compiles the CUDA/OptiX
  sources in parallel, each only when its source, a header it included or its
  flags changed (see BUILD.md).
- **`setup_env.ps1`** / **`setup_env.bat`** - Auto-detect and set the CUDA/OptiX
  SDK paths a build needs; run once on a fresh machine before the first build.

### Deployment
- **`deploy_launcher.ps1`** - Verifies ray_tracer.exe and the PTX files reached
  RayTracer_Package/ after a build
- **`deploy_qt_gui.ps1`** - Copies the Qt runtime DLLs next to RayTracerGUI.exe

### Packaging (for sharing a release with others)
- **`package.ps1`** - Builds and assembles a clean, tiered distribution
  package directly into the same `RayTracer_Package\` folder as the local
  dev build above (windeployqt + VC++ redist DLLs + README/launcher on top
  of whatever's already there) - not a separate copy. The folder itself is
  never wiped/pruned (RayTracerGUI.exe and its Qt runtime have no other
  copy to rebuild from, so deleting them to "downgrade" a tier would
  destroy the only copy of that build), but `-Zip`'s output always matches
  the current `-Tier`: it excludes any higher-tier files left over from a
  previous run (e.g. `-Tier Lite -Zip` after a previous `-Tier Full` build
  excludes `RayTracerGUI.exe`, the Qt runtime, and the GPU DLLs/PTX from
  the zip). Without `-Zip`, running the folder directly can still launch
  more than the requested tier if a higher tier was built there before -
  the script warns when this applies.
  - `-Tier Lite|Medium|Full` (default `Full`):
    - `Lite` - RayTracer.exe (CLI) only, CPU rendering, no Qt/GUI
    - `Medium` - + RayTracerGUI.exe + Qt runtime, CPU rendering only
    - `Full` - + GPU rendering (OptiX/CUDA, Live Preview)
  - `-Configuration Debug|Release` (default `Release`)
  - `-SkipBuild` - assemble from whatever's already built, skip the build step
  - `-Zip` - also produce `releases\RayTracer_<Tier>_<date>.zip`
  - The CUDA runtime is statically linked into every binary that needs it
    (no `cudart64_*.dll` is ever required on the target machine - only the
    NVIDIA driver's own `nvcuda.dll`, never bundled). Medium cannot exclude
    Qt's multimedia/ffmpeg DLLs - they're a hard compile-time import of
    RayTracerGUI.exe (`QT += multimedia`), not a strippable runtime plugin;
    see the script's own header comment for why that would need a source
    change, not a packaging one.
  - Every tier also gets the scene collection and the licences: the files
    Git tracks under `pbrt_scenes\` (about 6 MB; most scenes load from
    there, so a package without it cannot render A1), `images\earthmap.jpg`,
    `models\suzanne.obj` and `models\spot.obj`, plus `LICENSE`,
    `THIRD_PARTY_NOTICES.md` and `licenses\`. Large downloaded scene assets
    are never packaged. The script stops with an error if one of these is
    missing from the result.
  - Templates for the in-package `README.txt`/`launcher.bat` live in
    `scripts\templates\` - edit those, not this script, to change their wording.
  - Usage: `.\scripts\package.ps1 -Tier Lite` / `-Tier Full -Zip`

### Testing & Verification
- **`compare_images.py`** - Python script to compare rendered images
  - Compares CPU vs GPU output
  - Pixel-by-pixel difference analysis
  - Usage: `python scripts/compare_images.py image1.ppm image2.ppm`
- **`consistency_sweep.py`** - Renders hundreds of pairs of scenes that must look alike (moved, scaled, mirrored, turned; constant against
  picture sky; CPU against GPU) and prints the pairs that do not. See `docs/CONSISTENCY_SWEEP.md`.
- **`run_optix_sweeps.ps1`** - Builds the renderer and runs that sweep on both OptiX backends (recursive, then wavefront with
  `RAY_TRACER_WAVEFRONT=1`), ending with one PASS/FAIL line each (exit code 0: nothing off). `-NoBuild`, `-Only sky,shapeform`,
  `-Backend Wavefront`, `-Detail`. About ten minutes a backend; run it after a change that can alter a picture, since the Mac cannot run OptiX.
- **`make_material_thumbnails.py`** - Renders `qt_gui/material_thumbs/<preset>.png`, the pictures of the Scene Builder's material presets (a sphere in a small
  studio, CPU, from scenes the unit tests write with the Scene Builder's own document code). Run it after adding or changing a preset, then add the new
  file to `qt_gui/resources.qrc` (it prints the lines).
- **`run_tests_parallel.ps1`** - Shards `ray_tracer_tests.exe` across multiple
  parallel processes (`GTEST_TOTAL_SHARDS`), with per-shard scratch
  directories to avoid output-file collisions
  - `-Tier Fast|Render|Slow|Split|All` selects the cheap tests (sharded 16 ways, ~75 s), the CPU-render suites
    (4 shards, ~154 s), the GPU tests (one process, ~340 s), or Fast then Render and Slow at the same time
    (`Split`, the default, ~375 s for the whole suite); see BUILD.md
  - See `tests/TESTING_GUIDE.md` for the full usage pattern
- **`pre_commit_checks.py`** - CI's quick checks before a commit (about a second): `check_build_lists.py` (every
  test file in both test builds), `check_env_docs.py` (every environment variable in `docs/ENVIRONMENT.md`) and
  `check_code_size.py` (no new function over 300 lines or file over 2000), plus a warning for source files git
  does not track yet.
- **`gui_selftest.py`** - Starts the built GUI headless in each `RT_GUI_SELFTEST` mode and fails on a crash, a hang
  or a wrong result (`--modes`, `--live-preview`); Windows and macOS (`gui_selftest_macos.sh` wraps it).
- **`gtest_annotations.py`** - Prints the failed tests of gtest XML reports as GitHub Actions annotations (CI).
- **`run_gallery_isolated.ps1`** - Renders every gallery scene that needs external files, each in its own process
  (`-Cpu`, `-SkipLarge`, `-Filter`); see BUILD.md.
- **Comparing with real pbrt-v4:** `pbrt_compare.py` (the Scene Builder's combination scenes), `pbrt_scene_compare.py`
  (the bundled scenes), `pbrt_reference_pack.py` (writes `tests/data/pbrt_reference_means.txt`, the frozen pbrt means
  the tests check against), `pbrt_rough_glass_reference.py` (an independent path-level reference for rough glass).
- **Comparing our own renderers:** `backend_parity.py` (CPU against OptiX), `bundled_scene_compare.py` (every bundled
  scene CPU against GPU, mirrored or flipped pictures too), `combo_sweep.py` (the shape-by-material scenes),
  `settings_sweep.py` (pairs of settings, `docs/SETTINGS_SWEEP.md`), `refactor_check.py` (seeded renders that must stay
  byte-identical across a refactor), `worker_orphan_check.py` (a film's workers end when the launcher is killed).
- **Mac only:** `mac_release_check.sh` (everything checked before a release), `check_metal_shaders.py` (compiles the
  Metal shaders offline), `metal_pbrt_pack.py`, `metal_render_hash.sh`, `metal_large_scenes_sweep.sh`,
  `update_metal_golden.sh` (refreshes `gpu/metal/tests/parity_golden.txt`).

- **`render_baseline.py`** (+ `exr_io.py`, its EXR reader) - Renders 15 scenes on the CPU and both GPU backends and compares them with a
  baseline captured before a change, pixel by pixel with measured noise, to catch brightness shifts of ~0.3% or less that the test suite
  tolerates. `capture <name>` before, `compare <name>` after (`--expect-identical` for a pure refactor). See BUILD.md.
  Needs numpy; baselines go under `baselines/` (git-ignored).

### Films
- **`anim/film.py`** - Previews, checks and renders the films (`preview`, `video`, `doctor`, `budget`, `baseline`, ...);
  see [`anim/README.md`](anim/README.md) and `docs/VIDEO_GENERATION.md`.
- **`measure_cartoons.py`** - Measures what a frame of each film costs and writes it into the film table.
- **`make_rigged_animals.py`**, **`make_rigged_robot.py`**, **`make_rigged_face.py`**, **`make_tengwang_assets.py`** -
  Make the films' generated assets in `pbrt_scenes/film_assets/` (rigged glTF animals, the robot, Mira, the Prince
  Teng pictures).
- **`gpu_kernel_bench.py`** - GPU time per kernel of a short film render, from Nsight Systems.

### Optional helpers
- **`setup_photo_to_mesh.ps1`** / **`.sh`** - Sets up the Scene Builder's "Object from a photo" helper (a private
  Python environment with TripoSR) in the per-user data folder.
- **`setup_video_to_acting.ps1`** - Sets up the films' "act it yourself" helper (MediaPipe): a video of you acting
  drives a character's body and face.

### Generators
- **`stamp_scene_sizes.py`** - Writes each bundled scene's `# @rt-size` header line (`--check` lists missing ones).
- **`gen_scene_packs.py`** (`qt_gui/scene_packs.txt`) and **`update_asset_manifest.sh`**
  (`qt_gui/downloadable_assets.txt`) - The catalogues behind the GUI's "Download missing files".
- **`gen_environment_scenes.py`** - Generates the `pbrt_scenes/environment-*.pbrt` scenes from `models/*.obj` + `.mtl`.
- **`make_model_thumbnails.sh`** - The model library's pictures (`models/thumbnails/`).
- **`pack_glb.py`** - Repacks a glTF model as a slim single `.glb`.
- **`conductor_rgb_presets.py`**, **`measured_rgb_weights.py`** - Regenerate RGB tables from pbrt-v4's spectra.

### Maintenance
- **`clean_vs_cache.bat`** - Batch script to clean Visual Studio build cache
  - Removes intermediate files
  - Cleans .vs folder
  - Resets build state
  - Usage: `scripts\clean_vs_cache.bat`

## Usage Examples

### Create Distribution Package
```powershell
# From repository root - builds and packages in one step.
# Most setups only ever build one tier. If you need more than one tier's
# zip from a single build cycle, build smallest to largest as shown below -
# all tiers share one RayTracer_Package\ folder and are additive, so
# building Full first would leave its extra GPU/GUI files sitting in what's
# supposed to be a smaller Lite/Medium package.
.\scripts\package.ps1 -Tier Lite -Zip     # CLI only, smallest
.\scripts\package.ps1 -Tier Medium -Zip   # GUI, CPU rendering only
.\scripts\package.ps1 -Tier Full -Zip     # everything, GPU + GUI
```

### Compare Two Renders
```bash
# Compare CPU and GPU outputs
python scripts/compare_images.py output_cpu.ppm output_gpu.ppm
```

### Clean Build Cache
```cmd
# Reset Visual Studio build state
scripts\clean_vs_cache.bat
```

## Related Scripts

Other scripts are located in specific directories:
- **Qt GUI scripts:** `/qt_gui/verify_qt6.ps1`
- **Test scripts:** `/tests/build_and_run_tests.ps1`, `/tests/build_and_run_tests.bat`

## Dependencies

- **PowerShell scripts:** Require PowerShell 5.0+
- **Python scripts:** Require Python 3.6+ with PIL/Pillow
- **Batch scripts:** Run on Windows cmd.exe
