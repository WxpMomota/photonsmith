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
  GUI only - no GPU/OptiX backend on macOS). Run on macOS, not from Windows.
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
  - `-Tier Fast|Render|Slow|Split|All` selects the cheap tests (sharded 16 ways, ~40 s), the CPU-render suites
    (4 shards, ~154 s), the GPU tests (one process, ~270 s), or Fast then Render and Slow at the same time
    (`Split`, ~375 s for the whole suite); see BUILD.md
  - See `tests/TESTING_GUIDE.md` for the full usage pattern

- **`render_baseline.py`** (+ `exr_io.py`, its EXR reader) - Renders 15 scenes on the CPU and both GPU backends and compares them with a
  baseline captured before a change, pixel by pixel with measured noise, to catch brightness shifts of ~0.3% or less that the test suite
  tolerates. `capture <name>` before, `compare <name>` after (`--expect-identical` for a pure refactor). See BUILD.md.
  Needs numpy; baselines go under `baselines/` (git-ignored).

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
