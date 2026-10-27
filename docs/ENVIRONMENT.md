# Environment variables

The program's behaviour can be changed from the environment. They were spread over thirty files and written down nowhere, so a setting that changed a render could not be found by looking at the command line. This is the list. A variable is a **setting** (meant to be set by a person or a script), **plumbing** (one part of the program sets it for another: do not set it yourself), or a **test hook** (read only by tests and self-tests).

Rule for new ones: prefer a command-line option or a setting in the GUI; add an environment variable only for something a test or a script has to change from outside, and add it to this page in the same commit.

## Settings

| Variable | Read by | What it does |
|----------|---------|--------------|
| `RAY_TRACER_THREADS` | `src/TheRestOfYourLife/thread_count.h`, `launcher/main.cpp` | Number of CPU render threads, instead of the machine's hardware thread count |
| `RAY_TRACER_MODE` | `gpu/optix/renderer_config.h` | OptiX path tracer: `recursive` (default), `wavefront` or `auto` |
| `RAY_TRACER_FALLBACK` | `gpu/optix/renderer_config.h` | What the OptiX renderer falls back to when the mode asked for cannot be used: `none`, `recursive` or `fastest` |
| `RAY_TRACER_VERBOSE` | `gpu/optix/renderer_config.h` | More console output from the GPU renderer |
| `RAY_TRACER_PBRT_DIR` | `qt_gui/scene_builder_files.cpp`, `qt_gui/app_log.cpp` | The folder the Scene Builder's "Add to scene list" saves into (and scene discovery scans), instead of the per-user folder |
| `RAY_TRACER_LOG_DIR` | `qt_gui/app_log.cpp` | Where the GUI writes its log files |
| `RAY_TRACER_STATE_DIR` | `qt_gui/scene_builder_autosave.cpp` | Where the Scene Builder keeps the unsaved scene while it is edited |
| `RAY_TRACER_NO_CRASH_PROMPT` | `qt_gui/crash_recovery.cpp` | Skip the "The program did not close properly last time" dialog (start normally) |
| `RAY_TRACER_PHOTO3D_PYTHON`, `RAY_TRACER_PHOTO3D_SETUP` | `qt_gui/photo_import.cpp` | The Python interpreter and the set-up script of the photo-to-scene helpers, instead of the ones next to the program |
| `RT_TRIPOSR_DIR` | `tools/photo_to_mesh/photo_to_mesh.py` | Where the TripoSR model lives |
| `RT_OIDN_DIR` | `src/shared/oidn_runtime.h` | A folder with an Intel Open Image Denoise release (`lib/` or `bin/`), searched first |
| `RT_ASSET_BASE_URL` | `qt_gui/asset_downloader.cpp` | Replaces the base URL "Download missing files" fetches the statue models from (a local server, for tests and mirrors) |
| `RAY_TRACER_BACKGROUND`, `RAY_TRACER_BACKGROUND_DUTY` | `launcher/run_policy.h` | `1`: as `--background` (lower priorities, one worker, a pause after each frame of a video); the duty is the share of the time it renders (the pauses follow from it) |
| `RAY_TRACER_CARTOON_JOBS`, `RAY_TRACER_CARTOON_WORKERS`, `RAY_TRACER_CARTOON_TIMEOUT` | `launcher/cartoon_render.h` | As `--cartoon-jobs`, `--cartoon-workers` and `--cartoon-timeout` (the flag wins): how many frames of a cartoon video at once, how many long-lived worker processes (0: a process per frame), and how long one frame may take |
| `RAY_TRACER_WORKER_MAX_MB` | `launcher/cartoon_render.h` | As `--worker-max-mb`: a cartoon worker that holds more memory than this is replaced between two frames (0: never) |
| `RAY_TRACER_GPU_DEVICE` | `gpu/optix/optix_renderer_init.cpp`, `launcher/main.cpp`, `launcher/video_render.h` | Which graphics card the OptiX renderer uses (as `--gpu-device`, which sets it for the programs a video starts too) |
| `RAY_TRACER_OPTIX_CACHE_GB` | `gpu/optix/optix_renderer_init.cpp` | The size limit OptiX's shader cache is raised to (4 by default; only ever raised), so that an old build's shaders are not evicted into a 20-minute recompile |
| `RT_NO_GPU_WARMUP`, `RT_LIVE_SCHEDULER`, `RT_METAL_POST_EFFECTS` | `qt_gui/gpu_warmup.cpp`, `qt_gui/realtime_preview_session.cpp`, `gpu/metal/host/metal_poc.mm` | macOS only: skip the Metal warm-up, turn off the Live Preview scheduler, turn off Metal's post effects |

## Plumbing (set by the program)

| Variable | Set by | Read by | Meaning |
|----------|--------|---------|---------|
| `RAY_TRACER_USER_ASSETS` | `qt_gui/main.cpp` (to the user's data folder, or a temporary one in a self-test) | the GUI, `scene_metadata.dll`, the renderers, `src/shared/pbrt_asset_check.h`, `src/shared/pbrt_load.h`, `src/shared/oidn_runtime.h` | Where "Download missing files" puts what it fetches (`pbrt_scenes/...`, `models/...`, `denoiser/`); every file lookup falls back there when the file is not next to the program |
| `RAY_TRACER_WAVEFRONT`, `RAY_TRACER_OPTIX_VALIDATION`, `RAY_TRACER_STATS` | `launcher/main.cpp` from `--wavefront`, `--optix-validate` and `--stats` | `gpu/optix/` | The command-line option, as the OptiX library reads it (`RAY_TRACER_STATS` prints the per-render statistics, `[WF-STATS]` and `[REC-STATS]`) |
| `RAY_TRACER_FILM_PREVIEW` | `launcher/main.cpp` from `--preview` (and `--film-baseline`, which records at that coarseness) | `src/shared/cartoon/cartoon_lib.h` (read once, before any film's scene is made) | How much coarser a film's characters are built: a quick look at a film, in a fraction of the time; the frame runs a video starts inherit it |

`RAY_TRACER_USER_ASSETS` is the one variable that is both: a person can set it before starting the GUI to keep downloads elsewhere. It is read through one function (`pbrt_asset_check::userAssetRoot()`) everywhere the C++ code needs it.

## Test hooks

| Variable | Read by | Meaning |
|----------|---------|---------|
| `RT_GUI_SELFTEST` and `RT_GUI_SELFTEST_*` | `qt_gui/mainwindow_selftest*.cpp` | The GUI's self-test modes (`livepreview`, `queue`, `renderfail`, `download`, `composite`, ...) and their options: the mode's own comment says which |
| `RT_UPDATE_SNAPSHOT` | `tests/unit/scene_registry_snapshot_tests.cpp`, `tests/unit/cartoon_snapshot_tests.cpp` | Rewrite `tests/data/scene_registry_snapshot.txt` / `cartoon_films_snapshot.txt` instead of checking them |
| `RT_LAUNCHER`, `RT_REQUIRE_LAUNCHER` | `tests/unit/agreement_test_helpers.h` | Where the launcher is for the CPU/GPU agreement tests, and whether a missing one fails or skips them |
| `RT_REQUIRE_FIXTURES` | `tests/unit/fixture_skip.h` | A test whose file of the repository (a scene, a snapshot, a list) is not found fails instead of skipping. CI and `scripts/run_tests_parallel.ps1` set it |
| `RT_SOUNDTRACK_WAV`, `RT_SOUNDTRACK_DIR` | `tests/unit/cartoon_films_tests.cpp` | Where the soundtrack tests write the film's mix (one file; a folder, a `<key>.wav` for each film), to listen to |
| `RT_MACHINE_TEMP` | `tests/unit/film_whole_tests.cpp` | Set by `scripts/run_tests_parallel.ps1` in its shards (which have a TEMP of their own) to the machine's temporary folder: the film tests run the program with it, so they share the film cache the program's runs use instead of building a cold one in every shard |
| `RT_TEST_VAR` | `tests/unit/cartoon_tests.cpp` | Set by a test for a child process it starts, to check that the variable reaches it |
| `RT_FORBID_GPU` | `gpu/optix/optix_renderer_init.cpp` | Set by `scripts/run_tests_parallel.ps1` in its Fast tier's shards: a test there that asks for the GPU stops the shard (a GPU test must say GPU in its name) |
| `RT_SKIP_GALLERY`, `RT_GALLERY_ALL`, `RT_GALLERY_CPU`, `RT_GALLERY_LIST` | `tests/integration/gallery_scenes_render_test.cpp`, `tests/unit/pbrt_reference_pack_tests.cpp` | Skip the gallery render suite; include the Large Scenes (gigabyte meshes, off by default); also render each scene on the CPU and compare brightness (off by default); list which scenes would run (`scripts/run_gallery_isolated.ps1`) |
| `RAY_TRACER_RUN_VALIDATION_SWEEP` | `tests/integration/optix_validation_sweep_test.cpp` | Run the OptiX validation-layer sweep (slow) |
| `RAY_TRACER_GLB_DIR`, `RAY_TRACER_RECENT_SCAN_DIR` | `tests/unit/gltf_import_tests.cpp`, `qt_gui/recent_renders.cpp` | A folder of `.glb` files to read as a test; the only folder the Recent Renders list scans in a self-test |
| `RT_OIDN_RESCAN_DIR`, `RT_PRESET_SCENES_DIR`, `RT_COMBO_SCENES_DIR`, `RT_VOLUMES_PBRT`, `RT_FOREST_PBRT`, `RT_SKY_GALLERY_OUT`, `RT_SHOWCASE_OUT`, `RT_SCENE_BUILDER_STARTER_OUT` | the unit tests that name them | Where a test finds or writes the files it uses |
| `METAL_PARITY_*` | `gpu/metal/tests/metal_cpu_gpu_parity_check.cpp` | The Metal parity sweep's options (macOS; see `docs/METAL_PARITY_STATUS.md`) |
