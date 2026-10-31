# Video generation

This guide explains how to use the video generation feature to create animated ray-traced videos.

## Overview

The video generation feature allows you to:
- Render multiple frames with animated camera movement
- Choose from 6 camera animation paths (orbit, linear, figure8, spiral, tour, showcase)
- Automatically assemble frames into an MP4 video using ffmpeg

This guide covers the CLI. The Qt GUI exposes the same options (camera
path, frame count, FPS, a live duration calculator) on its **Video
Settings** tab, selected by switching Output Mode to "Generate Video" on
the Settings tab - see
[`qt_gui/QT_GUI_DOCUMENTATION.md`](../qt_gui/QT_GUI_DOCUMENTATION.md).

## Prerequisites

1. **Build the project** (if not already built):
   ```powershell
   .\build_all.ps1
   ```

2. **ffmpeg** (external dependency, not bundled):
   - Install from https://ffmpeg.org/download.html and make sure `ffmpeg` is on PATH
   - The launcher invokes it automatically as the last step of a video render
     (`libx264` codec, `yuv420p` pixel format) - if it's missing, the render
     still completes and the rendered/converted frames are kept on disk, but
     the launcher exits with `ERR_VIDEO_ASSEMBLY_FAILED` and no video is written

## Quick Start

### Render Video (Automatic Assembly)

Run the ray tracer in video mode - the MP4 is created automatically:

```powershell
# GPU mode (recommended) - 60 frames, orbit path
.\ray_tracer.exe --video --frames 60 --fps 30 --camera-path orbit 600 100 50

# CPU mode - 30 frames, spiral path
.\ray_tracer.exe --cpu --video --frames 30 --fps 30 --camera-path spiral 400 50 20
```

This will:
1. Create frames in `output/frames/` directory (`frame_0000.png` ... on the GPU, `frame_0000.ppm` ... on the CPU and for GPU SPPM)
2. Automatically assemble them into an MP4 video
3. Output the final video to `output/<name>_video.mp4`

## Command-Line Options

### Video Rendering (`ray_tracer.exe`)

| Flag | Description | Default |
|------|-------------|---------|
| `--video` | Enable video generation mode | (disabled) |
| `--frames`, `-f` | Number of frames to render | 120 |
| `--fps` | Target frames per second | 30 |
| `--speed` | Camera movement speed multiplier | 1.0 |
| `--camera-path`, `-p` | Camera animation path | orbit |
| `--camera-keyframes FILE` | Fly the camera through the places listed in FILE instead of a named path (see *Keyframes*) | |
| `--gpu` | Use GPU renderer (OptiX) | ✓ |
| `--cpu` | Use CPU renderer | |
| `--output`, `-o` | Output path (affects frame dir) | `./output/image.ppm` |

**Positional arguments** (same as single-frame mode):
```
ray_tracer.exe [--video options] [width] [spp] [max_depth]
```

Example:
```powershell
# 800x800 resolution, 100 samples per pixel, 50 ray depth
.\ray_tracer.exe --video --frames 90 --camera-path figure8 800 100 50
```

The video file will be created automatically as `output/<name>_video.mp4`.

### Movement Speed

`--speed` does not change the camera path itself - the camera always
completes the exact same full sweep (one full rotation for orbit/figure8,
two for spiral, the whole start→end traversal for linear) no matter the
speed, so it always swings all the way around/through the scene. Instead,
`--speed` scales how many frames that sweep is spread across: `--frames` is
the "1.0x" baseline frame count, and the actual number of rendered frames is
`frames / speed` (capped at 20000). `0.5x` renders twice as many frames,
spreading the same journey over more of them - and more real video time at
the same `--fps` - so it looks slower without ever cutting the path short.
`2.0x` renders half as many frames, covering the same journey faster (and
in a shorter video).

```powershell
# Half-speed orbit - renders 2x the frames (240 instead of 120), same full
# rotation, twice the video length at the same fps
.\ray_tracer.exe --video --camera-path orbit --frames 120 --speed 0.5

# Double-speed spiral - renders half the frames (60 instead of 120), same
# 2 full rotations, half the video length
.\ray_tracer.exe --video --camera-path spiral --frames 120 --speed 2.0
```

## Camera Animation Paths

### Keyframes: your own route

`--camera-keyframes FILE` replaces the named path with a route through places you choose. The file is plain text (`#` starts a comment):

```
ease 1                      # 1 (the default): start and end gently; 0: the same pace from the first frame to the last; or a curve: back, elastic, bounce, in, out
key  0 2.5 8    0 1 0       # key px py pz tx ty tz: where the camera is, and what it looks at
key  6 1.5 3    0 1 0  60   # ... and a 7th number: its vertical field of view, in degrees
key  0 5 -4     0 1 0  35 0.1 9   # ... and the 8th and 9th: its lens radius and focus distance (0.1 blurs what is not 9 away)
key  0 2.5 -4   0 1 0  hold 2     # "hold SECONDS" at the end of any key line: the camera stays here that long
```

```powershell
.\ray_tracer.exe --video --camera-keyframes route.txt --frames 120 --fps 30 800 64 8 my-scene.pbrt
```

The first frame is exactly the first `key` and the last frame exactly the last (two or more keys are needed). In between the camera passes through every key on a smooth curve (a Catmull-Rom spline through the places, and another through the targets), spending time on each stretch in proportion to how far it is, so the speed is about the same all the way. Every frame is the whole camera: where it is, where it looks, and (when the keys give them) its field of view and lens, all on the same curve. The field of view and the lens are used only when EVERY key gives them (a key without them is the scene's own camera, so a path is not half-and-half): then each frame overrides the scene's own with them, and otherwise the scene's own field of view and lens apply throughout. A **hold** is a pause of the finished video, in seconds (0 to 3600): the camera stands still at that key and the rest of the video's length (`--frames` / `--fps`) is what it has to travel in, at the same even pace. With `ease 1` the camera eases to a stop at every key with a hold (and out of it) as well as at the ends, each run between two stops being one smooth move. Holds that add up to more than 90 % of the video are shortened so that a tenth is left for moving. `--speed` stretches the number of frames as for the other paths. The file is read and checked before any frame is rendered, and a mistake is reported with its line number. The Scene Builder's **Flythrough...** button writes this file from its saved camera views. The code is `src/shared/camera_keyframes.h`. The renderers read the per-frame camera from `RenderOptions` (`has_lookat_override`, `lookat_override`, `vfov_override`, and the existing `aperture_override`/`focus_distance_override`): the CPU path tracer and the OptiX backends do; the Metal backend still follows only the camera's position, so on a Mac use `--cpu` for keyframes that turn or zoom.

### 1. Orbit (Default)
Circular motion around the scene on the XZ plane.

```powershell
.\ray_tracer.exe --video --camera-path orbit
```

**Best for:** Full scene overview, 360° rotation

### 2. Linear
Straight-line movement from start to end position.

```powershell
.\ray_tracer.exe --video --camera-path linear
```

**Best for:** Fly-through effects, cinematic reveals

### 3. Figure-8
Lemniscate pattern (figure-8 shape) motion.

```powershell
.\ray_tracer.exe --video --camera-path figure8
```

**Best for:** Dynamic motion, artistic effect

### 4. Spiral
Spiraling inward while rotating around the scene.

```powershell
.\ray_tracer.exe --video --camera-path spiral
```

**Best for:** Zoom-in effect, dramatic intro/outro

### 5. Tour
Interior walkthrough: a lateral S-curve sway combined with a gentle
forward-and-back glide, a subtle vertical bob, and a look-at point that
drifts independently - the way an actual visitor's head turns while walking
rather than staring at one fixed spot.

```powershell
.\ray_tracer.exe --video --camera-path tour
```

**Best for:** Room/interior scenes (Cornell-box family, architectural
interiors)

### 6. Showcase
Advertisement-style product reveal: an oscillating swing out to one side
and back through the original front-on angle - deliberately not a full
360°, since a studio-lit subject (three-point lighting, like H19's
`crown.pbrt`) is typically only lit/framed from roughly the front and would
swing into its own dim, unlit back half on a full orbit.

```powershell
.\ray_tracer.exe --video --camera-path showcase
```

**Best for:** Single studio-lit hero objects/statues

## Video Presets

`--video-preset ID` sets scene + camera path + frame count + fps + speed
together from one named preset (`src/shared/video_preset.h`), so you don't
need to look up a good combination by hand. Matches against either the
short id or the descriptive key:

| ID | Key | Scene | Path | Frames | FPS | Speed |
|----|-----|-------|------|--------|-----|-------|
| V1 | cornell-orbit | A1 (Cornell Box) | orbit | 90 | 30 | 1.0 |
| V2 | teapot-spin | G6 (Utah Teapot) | orbit | 90 | 30 | 1.0 |
| V3 | one-weekend-flyby | A2 (Bouncing Spheres) | linear | 90 | 30 | 0.8 |
| V4 | next-week-finale | A9 (Final Scene) | figure8 | 120 | 30 | 0.6 |
| V5 | glass-dragon-caustics | G13 (dragon) | spiral | 120 | 30 | 0.5 |
| V6 | sponza-flythrough | H1 (Sponza) | linear | 120 | 24 | 0.5 |
| V7 | lamp-and-ball | (its own scenes) | cartoon:lamp-and-ball | 480 | 24 | 1.0 |
| V8 | ball-parade | (its own scenes) | cartoon:ball-parade | 288 | 24 | 1.0 |
| V9 | little-robot | (its own scenes) | cartoon:little-robot | 768 | 24 | 1.0 |
| V10 | fireflies | (its own scenes) | cartoon:fireflies | 624 | 24 | 1.0 |
| V11 | newtons-cradle | (its own scenes) | cartoon:newtons-cradle | 432 | 24 | 1.0 |
| V12 | domino-run | (its own scenes) | cartoon:domino-run | 576 | 24 | 1.0 |
| V13 | paper-boat | (its own scenes) | cartoon:paper-boat | 576 | 24 | 1.0 |
| V14 | orrery | (its own scenes) | cartoon:orrery | 528 | 24 | 1.0 |
| V15 | dandelion | (its own scenes) | cartoon:dandelion | 528 | 24 | 1.0 |
| V16 | candle | (its own scenes) | cartoon:candle | 480 | 24 | 1.0 |
| V17 | meet-pip | (its own scenes) | cartoon:meet-pip | 432 | 24 | 1.0 |
| V18 | pip-and-the-moon | (its own scenes) | cartoon:pip-and-the-moon | 2304 | 24 | 1.0 |
| V19 | forest-dawn | (its own scenes) | cartoon:forest-dawn | 1008 | 24 | 1.0 |
| V20 | turtle-escape | (its own scenes) | cartoon:turtle-escape | 8184 | 24 | 1.0 |
| V21 | crystal-heart | (its own scenes) | cartoon:crystal-heart | 1440 | 24 | 1.0 |
| V22 | tengwang | (its own scenes) | cartoon:tengwang | 14880 | 24 | 1.0 |
| V23 | nomai-signal | (its own scenes) | cartoon:nomai-signal | 7200 | 24 | 1.0 |

`ray_tracer --list-video-presets` prints the whole list (camera flythroughs and cartoons together: id, key, kind, length, name). The cartoons are presets like the rest: one per film in
`src/shared/cartoon/cartoons.h`, numbered on from the camera presets (V7 to V23 so far), their name, description, frame count and fps taken from the film, so the table below and the film
table cannot disagree.

```powershell
# By id
.\ray_tracer.exe --video-preset V1

# Or by key, with an override (fps wins over the preset's own default)
.\ray_tracer.exe --video-preset cornell-orbit --fps 60
```

## Cartoon presets

A cartoon preset is a short animated film instead of a camera moving round a fixed scene: the scene itself changes from frame to frame (a lamp hops, a ball bounces). It is chosen
as a camera path of its own kind, `cartoon:<key>`, either by `--video-preset <key>` (or id) or by hand:

```powershell
.\ray_tracer.exe --gpu --denoise --video-preset lamp-and-ball 960 128 8                  # 480 frames at 24 fps: 20 seconds
.\ray_tracer.exe --gpu --video --camera-path cartoon:lamp-and-ball --frames 48 960 64 8    # a quick 2 second look (the film is squeezed into the frames you ask for)
```

In the GUI a cartoon is a scene: pick it on the **Cartoons** tab of the scene picker, choose Generate Video, and the video settings (camera path "Cartoon: ...", frames, fps, speed) are already the film's. The **Flythrough Preset** box in Video Settings lists only the camera flythroughs (V1 to V6); `--video-preset` on the command line still takes all of them.

How it renders: each frame is written as a `.pbrt` scene (`src/shared/cartoon/`: the film's `sceneAt(t)`) and rendered by a child run of the launcher with the flags you gave (backend,
denoiser, seed, exposure, tone mapping ...; the video flags and the output path are replaced), then joined by the usual video assembly. Each frame is therefore an ordinary
single-image render of a pbrt file: every renderer feature works, and a frame costs a process start (a second or two) on top of its render. To keep the GPU busy through that, **two frames
are rendered at once** on a GPU (one on the CPU, which a single render already fills): while one renders the other starts up or writes its picture, which takes a 12-frame Newton's Cradle from 50 s to
36 s; `--cartoon-jobs 3` (or the environment variable `RAY_TRACER_CARTOON_JOBS`) is faster still, 32 s, at about 3 GB of video memory for each frame in flight. How many pays depends on the frame, so by default a GPU video
chooses (RunPolicy::framesInFlight, launcher/run_policy.h): four for light frames (up to 30 million samples a frame, pixels times samples), three up to 80 million, two above, and never
more than the card's free memory holds (asked of the NVIDIA driver at the start, launcher/gpu_memory.h; a frame in flight holds about 0.72 GB + 1.75 KB a pixel with the wavefront renderer:
5 GB at 1080p). Measured on an RTX 5080 (16 GB), 24 frames of The Great Turtle Escape each, two at a time against the best (2026-10-23): 120p at 16 samples 9.3 s, 7.6 s four at a time;
480p at 16: 11.9 s, 10.3 s four; 480p at 64: 25.4 s, 22.7 s four; 1080p at 16: 24.3 s, 22.7 s three (13.7 GB); 1080p at 256: 274 s, and no better with one (269 s) or three (275 s);
48 frames at 1080p and 64 samples: 141 s two, 140 s three, and four need more than the card has and crawl as the driver pages video memory out. The workers follow the same number;
`--cartoon-jobs` / `--cartoon-workers` still decide when given. The pictures are put in frame order
whatever order they finish in. `--frames` / `--fps` change how the film is sampled and played back, not its story.

**Workers: a draft-quality film renders about four times faster.** A frame run costs about a second and a half before it renders anything (the process starts, OptiX makes its context and builds
its pipeline), which at a draft quality is most of the frame: 2.4 s for a frame whose rendering takes a fraction of a second. On an OptiX GPU the video therefore starts **two worker processes**
(`--cartoon-workers N`, or the environment variable `RAY_TRACER_CARTOON_WORKERS`; `0` is the process per frame described above) that each render every other frame in a row: for each frame
a worker writes the scene to the same file, the renderer sees that it has changed and uploads it, and everything else (the context, the pipeline, the denoiser) is reused. A worker
leaves a marker file when a frame's picture is whole; the video watches the markers, hands the pictures on in frame order, and **replaces a worker** that ends with frames left or that
makes no progress for `--cartoon-timeout` seconds (a frame that takes a worker down twice fails and is repeated from the one before). Measured on "Pip and the Moon": 24 frames at 400 x 225
and 25 samples took 30 s with a process per frame and 8 s with workers (0.34 s a frame); at 1280 x 720 and 64 samples, where rendering is most of the time, 3.3 s a frame became 2.9 s. The
pictures are the same (a worker's frame differs from a separate run's by noise only: 0.03 of 255 against 6 to 13 between different frames). The CPU and Metal keep a process per frame. A film's frame runs and workers read no film file but
the film's own (the video sets `RAY_TRACER_ONLY_FILM` to its key; a built-in film reads none), not every film file of the scene folders.

**Where a frame's time went, and what is still the limit.** Measured with Nsight Systems on "Pip and the Moon" at 1280 x 720 and 64 samples: the GPU ran kernels for only about 0.9 s of a frame's 2.3 s. Most of the rest was the CPU, on one thread: tone-mapping 920,000 pixels, writing them as 2.7 million numbers of PPM text through iostream, and then the same text read back number by number twice (the frame run makes a PNG, the video makes its numbered PNG and deletes the frame run's). Now the tone map is split over the cores, the text is written and read in one block by hand (`src/shared/ppm_text.h`, byte for byte what iostream wrote and read: the pictures are identical, checked by comparing the files) and a frame run (`--ppm-only`) makes no PNG of its own. 24 frames at 64 samples take 31 s with one worker (55 s before) and 30 s with two (35 s before); a third or fourth worker gains nothing now, because what is left is the GPU itself (`evaluate_materials` is 60% of the kernel time, then the OptiX launches). More memory and more CPU cores do not help from here; the next gain is in the kernels. (`nsys profile` on a one-worker render of a few frames shows it.)

**Using the computer while a video renders: `--background`.** The GUI's Settings tab has a Power box in its Render Settings group: "Full power (fastest)", the default, or "Keep the PC responsive while rendering" (remembered between runs); on the command line it is `--background` (or `RAY_TRACER_BACKGROUND=1`). It lowers the CPU priority of the render and of every worker it starts, lowers their memory priority and their GPU scheduling class, uses half of the CPU cores for a CPU render, renders a cartoon with one worker and one frame at a time, and, the part that gives the graphics card back, pauses after every frame for as long as the frame took (a duty of 50%; `RAY_TRACER_BACKGROUND_DUTY=percent` changes it, 10 to 100). A lower priority alone is not enough on Windows, which shares the card between programs' compute work in turns whatever the priority class: 24 frames of "Pip and the Moon" at 1280 x 720 and 64 samples take 33 s alone, 58 s beside a second render of the same kind at the normal priority, 57 s beside one at the lower priority, and 41 s beside one in `--background` mode (the pauses). The cost is that the background render itself takes about twice as long; the pictures are the same. It applies to videos and to renders, not to Live Preview.

**Full power while nobody uses the computer: Smart (`--smart-power`).** The Power box's middle choice, "Smart: full power while the PC is not in use" (Windows; `RAY_TRACER_BACKGROUND=auto` does the same), renders a video at full power - every worker, every core - while the computer is free, and with `--background`'s pauses while it is busy (`launcher/smart_power.h`). Busy means any of: a key or the mouse used in the last minute (`RAY_TRACER_SMART_IDLE_SECONDS`), a full-screen program, game or presentation, or other programs keeping one of the graphics card's engines 15% busy or half of the processor (this program's own workers and frame runs do not count). It stays busy until half a minute after the last sign, so a short lull does not start the card at full power under a game. The render process reads these every two seconds and says in the log when the verdict changes ("Smart power: the computer is busy (someone is using it): a pause after every frame."); its workers read the verdict, and while the computer is busy each of the N frames rendered at once pauses N times as long (2 s of work and 14 s of pause with four workers), so that the card as a whole is free half of the time; a pause ends as soon as the computer is free again. The priorities are background mode's all the time (they cost nothing on a free computer), the CPU threads full power's. The verdict is taken as a frame ends: a frame already rendering when someone sits down is finished at full speed first. The stuck-frame watchdog allows for the longest pauses (15 minutes become 2 hours with four workers), so a frame that really hangs is noticed later than at full power. Clicking Render counts as using the computer, so a smart video starts polite and goes to full power about a minute and a half after the last key or mouse movement. `ray_tracer --diagnose` prints what Smart sees now (a `Smart Power:` line). On the Mac the signs are not read, so the box does not offer Smart and `--smart-power` is `--background`.

**The first start on a new computer (or after an update of the shaders): the OptiX cache.** OptiX compiles the shaders the first time and keeps the result in its cache (`%LOCALAPPDATA%` under `NVIDIA\OptixCache`), so every later start takes a fraction of a second. Cold, the default renderer's six modules take **18 minutes** on this machine (16 cores; 1074 s measured with the cache off, 124 MB of PTX; they took 1361 s and 160 MB until `shade_material` called its large `shade_material_surface` from one place instead of three, which cut every hit program's inlined copies of it to a third; pictures identical on 22 scenes, the lamp and roughness-image paths included), the wavefront renderer's one module 2.6 s. The default renderer's modules are made only when a default-renderer render needs them (`OptiXRenderer::ensureDefaultRenderer`), so the wavefront renderer, GPU SPPM and Live Preview start in seconds even cold, and a wavefront render starts 0.7 s sooner than before even with a warm cache; the GUI starts `ray_tracer --prepare-gpu` at a low priority when it opens (the status bar says "Preparing the GPU renderer ..." if that takes more than a moment), so the compile happens in the background before the first render; the default renderer prints "Compiling the default renderer's shaders ..." when its compile takes more than five seconds, and `[OptiX] The default renderer's 6 modules were ready in N s` when it is done. Measured and not a lever: OptiX's optimisation level (level 1 took 1374 s against 1361 s). Where the time goes: about 100 CPU-minutes in all, but on 16 cores the first 8 minutes use about 6 (each big module's front end is one task), then a tail of one or two cores for the last 5; perfect parallelism would take about 7 minutes. The levers left are smaller modules still (each geometry module still inlines the whole material system once; `__noinline__` on it was measured twice to make OptiX's compile slower, not faster, in the older single-module build, and has not been tried on this one) and a shading module shared by all geometry types as a continuation callable, which would also change the speed of every render and was not attempted. The cache's size limit is raised to 4 GB (3 GB low water mark) if it is lower, so that old builds' entries are not evicted into a recompile (never lowered; `RAY_TRACER_OPTIX_CACHE_GB=n` sets the limit, 0 leaves OptiX's own; `RAY_TRACER_VERBOSE=1` shows the line when it is raised; `OPTIX_CACHE_MAXSIZE`, when set, is respected as the user's own choice, and `OPTIX_CACHE_MAXSIZE=0` turns the cache off, which is how to measure a cold start).

**Pictures, flags, GPUs and workers (how a video's runs are put together).** A GPU frame (and any GPU render asked for a `.png` file) is written as a PNG by the renderer itself, straight from the picture's bytes, like `.exr` is written (`OutputOptions::write_png`, handed over by the launcher so that the renderer library has no PNG writer of its own); the text PPM that the launcher used to read back and convert (7 MB at 1280 x 720) is no longer made for them, and the PNG is pixel-identical to the one the old route made (checked on a render of B3). A render that is cut afterwards (a crop window) and a file asked for with another name still go through the PPM. The flags a frame's run repeats are decided by one table, `launcher/flag_scopes.h`: Render flags are repeated with their values, the video's own flags (`--video`, `--frames`, `--output` ...) are not, and a flag that is not in the table is not repeated and says so; a test checks that every flag the usage text documents is in the table. `--gpu-device N` (or `RAY_TRACER_GPU_DEVICE`) chooses which NVIDIA GPU is used, as `nvidia-smi` numbers them; a number that is not a device of the computer is said and the first is used. A cartoon video's worker can be replaced between two frames when it holds more than a limit (`--worker-max-mb N`, or `RAY_TRACER_WORKER_MAX_MB`; the default, 0, is never: no limit since 2026-10-23; the GUI's Settings tab sets both, "Graphics Card" (not on a Mac) and "Video Helper Memory", in its Render Settings group, remembered between runs; measured flat at about 440 MB over 120 frames, so this guards against a leak that has not appeared), and on Linux and macOS a worker ends when the launcher that started it does (`ParentWatch` in `launcher/process.h`; not yet tried on those systems). `--stats` prints the host round trips of the wavefront loop (2.1 a bounce), and a test keeps it under 2.5.

**What the GPU time of a Pip frame is made of** (`python scripts/gpu_kernel_bench.py`, Nsight Systems, 1280 x 720, 64 samples, per frame): 0.85 s of kernels; `evaluate_materials` 0.49 s (58%), the OptiX intersection launches 0.23 s (27%), camera rays 0.06 s, shadow rays 0.04 s. Pip's glossy surfaces are `coateddiffuse`, so almost every hit (99.9%) goes to the general `evaluate_materials` kernel and not to the simple or the dielectric one, and it runs pbrt's layered random walk three times per hit (the sample, the MIS pdf and the light sample's `f()`). Measured by skipping one walk: the light sample's `f()` alone is 22% of `evaluate_materials`; all the light samples' walks (the moon's and the sky's `f()` and pdf, the distant light's `f()`) 30%, the continuation's MIS pdf 7% (2026-10-22, 4 frames: 1.97 s, 1.39 s and 1.83 s of `evaluate_materials`). None of these walks can be left out without changing the pictures: each is part of pbrt's estimator, and the one walk that was pure waste, the pdf of a distant or point light's direction (a delta light has no MIS weight), is already removed by the compiler as dead code (skipping it by hand measured 1.970 s against 1.972 s, with byte-identical pictures). Compare runs only when they report the same number of launches: one run of the same day made 1994 instead of 2107 and read 1.85 s, for a reason not found. Tried and not kept: `__launch_bounds__(256, 2)` to fit two blocks in an SM's registers (the kernel uses 138; capping it at 128 made it 4% slower). pbrt-v4 (`src/pbrt/wavefront/`) does what this renderer already does, one specialised kernel per material type and a queue per type, so it offers nothing further for the kernel; Mitsuba 3 and Cycles avoid the walk with an analytic approximation of a coated surface, which would change the pictures and so is not done. Two traps for anyone measuring: the hardware counters (`ncu`) need administrator rights on Windows; and a change to a header the layered BxDF is in (`src/shared/bxdfs_layered.h`) makes OptiX compile the shader code again on the next render, which took 20 minutes here (and the OptiX cache, in `%LOCALAPPDATA%` under `NVIDIA\OptixCache`, forgets older entries once it is over about 1.5 GB, so going back to an earlier build can be a cold start too). A video's workers are restarted by their stall watchdog before that finishes: render one still first.

**The renderer a film is made with.** On an OptiX GPU the default renderer is the recursive one, and the wavefront renderer (`--wavefront`) is about 25% faster on the same scene, but it draws some materials differently. A film can name the wavefront renderer for its frames (`FilmMeta::wavefront`) when its frames were compared with the default renderer's and matched: "Pip and the Moon" does (20 frames spread through the film, each against a 768-sample reference: never more than 0.5 dB worse than the default renderer, usually 2 to 5 dB better at 64 samples, mean brightness within 0.2 of 255). The others keep the default renderer: the wavefront renderer draws the candle's wax and flame clearly darker (18.5 dB), the dandelion's seeds brighter, and chrome and brass (Newton's cradle, the orrery) with visible differences. With the workers above, a 1280 x 720 frame of "Pip and the Moon" at 64 samples takes about 1.7 s (it was 5.2 s at 128 samples with a process per frame): the whole movie takes about an hour. Fewer samples, a shallower ray depth and rendering every other frame all lose quality and are not used: the ray depth does not change the time (these night scenes are mostly lit directly), and the GPU denoiser already uses the albedo and normal guides.

**Movie-like films: fog that ends, the camera's look, plants ("Forest Dawn", 2026-10-22).** The earlier films are built from spheres, boxes and lathed shapes under plain lights. Three things make the newer kind possible. *Light shafts:* a film can stand its camera in mist that ends at a large sphere (`nature::Mist` in `src/shared/cartoon/nature_lib.h`, the pbrt way: the camera medium named before the Camera and one `interface` sphere around the set holding it). The sun and the sky outside the sphere light the mist, so a low sun between trunks draws shafts. A fog that fills all of space cannot do this, because no light from infinitely far gets through it (such a scene renders black). The CPU and the wavefront renderer clip every flight and shadow ray to the sphere, and the default GPU renderer hands such a scene to the wavefront one (see docs/PBRT_SUPPORT.md; `pbrt_scenes/sunbeams-in-mist.pbrt` matches real pbrt-v4 to 1.3%). *The camera's look:* a scene file's `# @rt-look bloom 0.16 vignette 0.32 warmth 0.2 saturation 1.06` line, or the flags `--bloom`, `--vignette`, `--warmth` and `--saturation`, is applied to the linear picture after the denoiser and before the tone map (`src/shared/lens_look.h`): the glow a bright light spreads through a lens, darker corners, a white balance and saturation. A film writes the line into every frame's header, so its stills, its video and the GUI's renders of it all get the same look; a flag wins over the line; an EXR stays raw. *Plants:* `nature_lib.h` makes a rolling ground, grass tufts, trees (a tapering trunk, branches and clusters of translucent leaves), ferns and rocks, and writes each piece once as a pbrt object placed many times (`ObjectInstance`; a placement can lean, which sways a tuft or a cluster in the wind). "Forest Dawn" uses all three: about 140 trees, 1,700 tufts of grass and 3,400 placed objects make a 2.8 MB frame (8.6 MB before the branches were placed objects too), rendered by the wavefront renderer in about 2 s at 640 x 360 and 128 samples. Two lessons from making it: the cameras must stand on the rolling ground (`above(x, y, z)`), not at a fixed height, or a low shot ends up under it; and a real 15 cm leaf 11 m from the lens is 2 pixels wide, so the falling-leaves shot is close to the leaves. Moving meshes are still sharp on the GPU (its motion blur is for spheres and the camera only).

**A long film is safe to render.** A frame that takes longer than `--cartoon-timeout S` seconds (15 minutes on a GPU by default, never on the CPU; `0` = never; the environment variable
`RAY_TRACER_CARTOON_TIMEOUT` also sets it) is stopped and tried once more, so one stuck frame does not hold up the rest. A frame that still fails is repeated from the frame before it, so the
video keeps its length and its sound stays in step (the run says how many) - but only a few: when more than 3 frames (or 2% of the frames, if that is more) failed, the run fails
with an error and makes no video, keeping the frames that came out so that running it again resumes from them (filling hundreds of failed frames and reporting success once hid
an hour of failures). With workers, four frames failing in a row stop the run at once, rather than failing the rest the same way. On an OptiX GPU one worker renders alone until
its first frame is in (on a cold shader cache that frame compiles the shaders, which takes 5 to 15 GB of the computer's memory; workers compiling side by side ran a 32 GB
computer out of memory), and another worker starts only while 6 GB of the computer's memory is free (at least one always runs). **`--resume` continues an interrupted video**: the frames already rendered are kept (each is a numbered picture in the
`frames` folder, written whole or not at all), and only the missing ones are rendered; it does this only when the film, the size, the samples and the flags are the same as the interrupted run's
(a signature file in the folder says so; `--background`, `--smart-power`, `--gpu-device`, `--stats` and `--optix-validate` do not change the pictures and do not count,
`--noise-target` and `--temporal-denoise` do), and otherwise starts again. The GUI passes it for every cartoon video, so a render that was stopped continues when you start it again with the same settings and output folder. A rendered frame leaves only its picture in the folder, not the renderer's own files (a film has thousands).

**After a change to a film, only the frames that changed are rendered again.** Each rendered frame's scene has a fingerprint (`launcher/frame_fingerprint.h`: the scene text, without the
build folder's name, and the content of every file it names - meshes, pictures), kept in the frames folder's `fingerprints.txt`. `program.txt` records the program that rendered the frames
(its size and time, the GPU's shader files beside it, and the fingerprints of two of the film's moments; it is written only after a full, clean run). A resume by the same program keeps every finished frame, as before. A resume by a new program (a film or the renderer
changed) builds each finished frame's scene again and compares fingerprints: a frame whose scene is the same is kept without rendering ("unchanged, kept"), the rest are rendered. So after
moving one shot's camera, a resume renders that shot's frames and keeps the other 7,000. Frames rendered before fingerprints existed have none and are rendered again once. A change to
the renderer itself that leaves every scene the same keeps every frame: delete the frames folder to render them all again.

**Writing a film that renders fast:** a glowing ball must be `emissiveSphere(centre, radius, light)` (pbrt's own sphere, one light), never an emitting `uvSphere` mesh: every triangle of an emitting
mesh is a light of its own to sample, so a mesh ball is hundreds of lights. The Candle's 70 out-of-focus lights took a frame from 7 s to 19 s as meshes, and cost 7 s again as spheres.

| Key | Name | Length | Shots | Tags | What happens |
|-----|------|--------|-------|------|--------------|
| lamp-and-ball | Lamp and Ball | 20 s | 1 | character, physics, emissive, night | A small yellow desk lamp wakes up in the dark, notices a red ball, hops over, nudges it, watches it bounce off the books and roll home, and tucks its shade over it. |
| ball-parade | Ball Parade | 12 s | 1 | physics, reflections | Seven glossy balls drop in one after another, bounce in a travelling wave on a checker floor with squash and stretch, and settle one by one into a row. |
| little-robot | The Little Robot | 32 s | 5 | character, emissive, dusk, dof | A small robot wakes in a junkyard at dusk, finds a seedling dying in a tin can, waters it, and watches it bloom into a glowing flower. |
| fireflies | Fireflies | 26 s | 4 | emissive, particles, night, dof | A night meadow lit by fireflies: a lone firefly wanders, a second arrives, their flashes drift into step, they circle and spiral up to the moon as the meadow wakes. |
| newtons-cradle | Newton's Cradle | 18 s | 3 | metal, reflections, physics, dof | Five chrome balls on strings: one is lifted and let go, the far ball flies out and back, and the swing passes from side to side and slowly dies, with the room in the chrome. |
| domino-run | Domino Run | 24 s | 4 | physics, emissive, dof | A red marble taps the first of 130 rainbow dominoes laid in a spiral; the wave runs inwards, chased by the camera, and the last domino lights a small lamp in the middle. |
| paper-boat | The Paper Boat | 24 s | 3 | water, reflections, dusk, backlight | A folded paper boat sails across a pond in the rain at dusk: every drop starts rings of ripples that rock the boat and cross one another, the rain eases, and the boat sails on towards the low sun. |
| orrery | The Orrery | 22 s | 3 | metal, reflections, emissive | A turn round a brass model of a small solar system: a glowing sun, five enamel planets on arms (one with a moon, one with a ring), engraved orbits and a pair of meshing gears, all lit by the sun alone. |
| dandelion | The Dandelion | 22 s | 3 | translucent, backlight, particles, dof, dusk | A gust blows the seeds off a dandelion clock one by one, windward side first, and they drift away on their little parachutes, glowing where the low sun shines through. |
| candle | The Candle | 20 s | 3 | subsurface, emissive, dof, night | One candle burns down in front of out-of-focus fairy lights: the flame flickers, light glows through the wax, drips run down its side, and at the end the flame dies in a thread of smoke. |
| meet-pip | Meet Pip | 18 s | 3 | character, emissive, dof | Pip, a small round blue creature with two antennae, says hello, shows what its face can do (surprise, sadness, a scowl, a hop of joy), follows a glowing orb with its eyes and walks off across the stage. |
| pip-and-the-moon | Pip and the Moon | 96 s | 13 | character, emissive, water, reflections, night | Pip finds the moon in a pond, tries to touch it and breaks it, scoops it up in a bucket and dances with it, trips on the way home and loses it, and is shown the real one by a firefly. With music and sounds. |
| forest-dawn | Forest Dawn | 42 s | 5 | fog, nature, dawn, translucent, backlight | A misty forest clearing from the blue before sunrise to the first warm light: the last fireflies go out, the sun comes up behind the hills and shines between the trunks in shafts through the mist, dew catches it on the grass, and leaves fall through the beams. |
| turtle-escape | The Great Turtle Escape | 341 s | 74 | character, comedy, water, glass, night | A tiny turtle escapes its perfect tank like a spy breaking out of prison - rocks, a thermometer, a hose and a robot vacuum stand in its way - and dives into a pool that turns out to be the tub its owner filled for it. |
| crystal-heart | The Crystal Heart | 60 s | 6 | glass, emissive, fog, water, dawn | A valley of colossal glowing crystals rising out of a mirror lake, from the deep blue before dawn to sunrise: the sun breaks through the ridge and its light falls between the spires through the mist, and the giant crystal floating over the valley wakes, beats and ignites, its light cutting through the mist in beams. With music. |
| tengwang | Preface to the Pavilion of Prince Teng | 620 s | 59 | character, water, dusk, night, fog | One autumn day at Hongzhou after Wang Bo's preface of AD 675, from the sea of clouds before sunrise to the stars over the Gan River: the Pavilion of Prince Teng, the feast, the young poet alone on its high terrace above the whole land, the sunset and the solitary duck, the world inside his brush, and his resolve. With the preface's words on the screen in Chinese and English, and music. |
| nomai-signal | The Last Nomai Signal | 300 s | 32 | character, emissive, night, dawn, fog | An unofficial fan film after Outer Wilds: a lone Nomai explorer follows a faint repeating signal to a small moon of a gas giant, finds the ruins of an expedition that came there long before, wakes their observatory deep under the ground, and learns the signal comes from a star that is already gone. The explorer answers it anyway. With music. |

A frame of a film is also a scene: `ray_tracer --gpu 1280 256 8 cartoon:fireflies@13.5` renders that moment as one picture (`cartoon:<key>` alone is the film's own best still), which is the quickest
way to look at a change to a film. The GUI's scene list has a Cartoons tab (beside Self-Contained and Requires External Files) with one still per film; it is listed from the films themselves (a still's file is written the first time it is rendered), so a new film appears there by itself, and its Film time box renders any other moment of the film as a picture.

Making a film of your own: the toolkit is `src/shared/cartoon/cartoon_lib.h` (the only implementation: keyframes, transforms, meshes, hop squash and stretch, inverse-kinematics arms, shots with camera moves), and `python scripts/anim/film.py` shows a film's stills, a contact sheet or the video without the GUI;
a finished film is a function `std::string sceneAt(double t)` (with `headerAt(t)`, `shotStarts()` and `kSamples` beside it) and a row in the table in `src/shared/cartoon/cartoons.h`; that row alone makes it a video preset
(`video_preset::all()` builds the preset from it), a scene on the GUI's Cartoons tab and a line of `--list-video-presets`.

**A draft of a film: `--draft N`** (the GUI's Draft box under the video settings): a cartoon video renders one frame in N (2 to 24) and repeats it in the frames after it, so the
video keeps its length and the film's soundtrack is added and stays in step - the whole film, to watch and listen to, in a fraction of the time (The Crystal Heart, 1440 frames, at
213 x 120 and 4 samples with `--draft 8`: 27 seconds with its sound). The frames a draft renders are real renders: a render at the same size, samples and flags with `--resume`
(the GUI always resumes a film) and no draft renders only the frames the draft left out.

**Checking a film before rendering it: `python scripts/anim/film.py doctor <key>`.** It renders every shot at its start, middle and end, small (120 pixels high, 8 samples, three at a time:
about six minutes for the 59 shots of tengwang), and reports what looks wrong, framed in red (a warning) or yellow (a note) on a contact sheet with a row a shot: black frames in the
middle of a shot (a black shot, or a black start or end with a picture in the middle, is a cut or a fade: a note), very dark frames (a light boxed in, an exposure far too low), blown-out
frames (more than 8% of the picture near white), flat frames (the camera inside something or against a wall), jumps in brightness across a cut, and frames much slower to render than
the rest; and it estimates the whole film's render time from the film's measured cost a frame. It exits 1 when there is a warning. `ray_tracer --film-shots <key>` prints what it
reads of a film (its facts and its shots, tab-separated). Every problem it looks for was found by eye at least once while making tengwang.

*Framing checks.* A film can name what each shot is about: its row's `subjectsAt(t)` (with `film_kit.h`, a shot sets `f.subjects`, even without building the scene: the explorer's
chest, a face, the fire). For such a film the doctor also renders a depth pass with each frame (`--aovs`, on the CPU, a few seconds a frame; `--no-framing` skips it), asks
`ray_tracer --film-subjects <key> <t> <width> <height>` where each subject falls in the picture (`src/shared/cartoon/film_subjects.h`: through the camera of the moment's own header), and warns
when a subject is out of the frame or behind the camera (a note at a shot's start or end when it is in the frame in the middle: it enters or leaves), when it is hidden (everything
round its pixel is nearer than it), and when most of the frame is within 20 cm of the camera (against a wall). A subject point should be on what is seen, not inside it: the Nomai
film's moon is `moonFacing(eye)`, the point of its surface nearest the camera, not its centre. A unit test (`FilmSubjects`) holds every such film to subjects in the frame in the
middle of each shot, without rendering. Its first run on the Nomai film found two shots that lost their subject (The answer looked up at the machine with the explorer below the
frame; the canyon reveal looked at the ground before the rim), both reframed.

**Changing a shot's camera without a rebuild: `pbrt_scenes/film_assets/<key>/cameras.txt`.** A film's cameras file is read when a frame is made
(`src/shared/cartoon/shot_cameras.h`; every film has it, through the film registry), so its video, its stills, the GUI's renders and the doctor all see the change at once.
A section a shot, by its number (1 is the first, as the doctor's sheet counts) or its name, and `[*]` for every shot:

    [28]
    dolly 1.5        # forward along the view (negative: back); the aim stays where it was
    truck -0.4       # to the right, aim and all;   pedestal 0.3: up, aim and all
    pan 4            # turn the aim to the right, degrees;   tilt -6: aim down
    zoom 1.2         # the code's field of view divided by 1.2;   fov 50: set it
    stops 0.5        # the exposure times 2^0.5;   iso 300: set it
    [The answer]
    tilt 3

The change is made to the camera the code gives at each moment, so a moving camera keeps its move. A film made from a file (below; not one in parts) makes the change in its own header, before
what is built round the camera - the water's surface, the decals, the sound's ears - so those follow the moved camera. A word the file does not know is printed once and left out. With the
fingerprints above, a resumed video renders again only the frames of the shots the file changed (the file is part of `program.txt`'s stamp, so an edit always makes a resume
compare the frames). When a change is right, it can stay in the file or be moved into the film's code.

**A character's moves and its fixed parts, written once.** `src/shared/cartoon/rig.h` is what any film's characters do: `rig::PathWalk` walks a path of points at a speed
(where the walker is, which way it faces, how many steps it has made, whether it is moving) and lists its footfalls (when and where each foot came down, feet in turn: the
dust puffs and footstep sounds come from the same list); `rig::blink` blinks once a period at a moment a seed picks; `rig::aimAt` turns a head to look at something within
what a neck can do; `rig::sitAmount` eases a sit down and a stand up. The Nomai explorer's walks, footstep dust and blinks are these. `film_cache::part(film, name, material,
frame, make)` makes a part that never changes shape - a mask, a tank, a pair of horns - once as a PLY in the film's cache and places it in each frame by its frame (the
explorer's mask, horns, pack and tank: the horns were sculpted again on every frame, and the mask's meshes written into every frame's text). A close-up of the explorer's
face went from 33 s to 29 s a frame; the renderer loads the same triangles (2,368,110 at 45 s, before and after). `film_cache::placement(m)` is the ConcatTransform for
any such PLY.

**Sound from the motion: a film's events and a library of sound effects.** A film can report what its motion does that the soundtrack should hear (its row's `events`, a
list of `FilmEvent`: a footfall, an impact, a flash, a touch, each with its time, strength and pan). `src/shared/cartoon/film_events.h` makes the list from the same walks
and moments the shots use: `film_events::footfalls` for a `rig::PathWalk` in a shot's span, `film_events::stepsAlong` for a body moved along a curve (a step each time its
distance passes one), `film_events::at` for one event at a place; each is panned by where the moment's camera sees it (+x to the right of the picture) and quietened with
its distance. `src/shared/cartoon/sfx.h` is the sound library - footsteps on dust, stone, grass, wood, metal and snow (a different take for each step), impacts light to
heavy, flashes, a hand on stone - and `sfx::play(mix, events, palette)` places them. The Nomai film's footsteps (72 events: outside in dust, in the ruins on stone), its
touchdown and the hands on the stone come from its events now, not from times typed into the score: a walk that is retimed retimes its steps. `ray_tracer --film-events <key>`
prints a film's list (`event  seconds  kind  strength  pan  seed`).

**Fewer samples where a shot does not need them: `python scripts/anim/film.py budget <key> --write`.** A video renders every frame at its samples a pixel, but a dark
interior, a fade or a wide still shot is as clean with fewer as a firelit close-up is with all of them. The budget renders the middle of each shot (at 360 lines) at the
film's samples, at half and at a quarter, each twice with different seeds and denoised: the difference of the two is the noise the denoiser leaves, and the difference of
their mean from the full render's is what fewer samples lose. A shot may drop to the fewest samples whose noise is no more than the film's median shot has at full samples
(or its own, if more) and whose loss is within 1.5 times that. `--write` puts a `samples` line (a share of the video's samples) for each such shot into the film's cameras
file, between two marker lines that the next `--write` replaces; the frame's header then carries `# @rt-samples <share>`, the video renders the frame at that share (never
fewer than one sample), and a resume renders again only the frames whose share changed. `--shots` measures some shots only. On five shots of the Nomai film the giant
in space and the canyon dropped to a quarter (no visible change, side by side) and the face close-up, the dark hall and the campfire kept all 32.

**A camera inside smoke or a cloud.** Smoke, flames, a volcano's plume and clouds are media that fill a sphere round their box, the air outside. On Windows the CPU and the
GPU sample such a volume along each ray up to the nearest surface (the default GPU renderer hands a frame with something inside a volume to the wavefront renderer), so a camera can fly into
smoke or through a cloud, and the ground, trees and animals inside a volume are in it as they should be. The Mac's renderer (Metal) does the same since 2026-10-30. `ray_tracer --film-media <key>` samples each shot five times (no render) and lists the
moments the camera is inside a medium that is not its own (mist round the camera and the sea are its own, and fine).

**Feet that slide, and flicker: the doctor watches the motion.** A film can report its characters' feet (its row's `feetAt(t)`: each foot's place and whether the walk means
it to be on the ground; a film_kit shot sets `f.feet`, the Nomai explorer's are `explorerFeet(pose, frame)`). `src/shared/cartoon/film_checks.h` samples every shot at the
film's frame rate and reports the fastest a planted foot moves (`ray_tracer --film-feet <key>`, no render); the doctor notes more than 0.10 m/s and warns above 0.25.
A foot on a moving floor is reported in that floor's space (the sinking platform). On the Nomai film the check found the explorer's swinging gait skating at 0.55 to
0.92 m/s in every walk (the foot's sine swing did not keep pace with the body), feet that slid down the floating steps with the body, and a snap from walking to
standing in the elevator; the gait now keeps a planted foot going back under the body at its own pace (`gait()`), the feet stand on a ramp's slope (`ExplorerPose::slope`)
and the elevator stops on the step it is on - every walk is now under 0.07 m/s but the floating steps (0.11: the body turning on the 6 m spiral). `film.py doctor --motion`
also renders three frames in a row from each shot's middle at the samples the video gives that shot, and the same three at four times as many: the second difference
of each (b - (a + c) / 2) holds the motion and the shimmer, the motion is in both and cancels, and what is left is the denoiser's frame-to-frame flicker (a note above
0.004, a warning above 0.008; the Nomai shots measured 0.001 to 0.003, the campfire 0.0044). It is how a budget that took too many samples away would show.

**Acting with gestures.** A gesture is a named pose from a character's library, eased in, held and eased out at film times (`rig::Gesture{"point up", 180.0, 181.5,
184.0, 184.0}`: easing in from 180 s, fully there at 181.5, gone at 184), and `rig::act(pose, gestures, t, library)` puts them on a pose in their order. A library
(`rig::PoseLibrary<P>`) is the character's own: each pose blends the parts it changes by the gesture's weight, and may move with the time since the gesture began (a nod).
The Nomai explorer's (`explorerPoses()`, nomai_explorer.h) has reach to the wall, hand on the lectern, point up, kneel and touch, look up, look down, surprised, hands
together, raise the sceptre, nod, shake head, sit and crouch. A gesture's `contact` time is a hand meeting something: `rig::contactsOf` lists them and the film's events turn
them into touches. The Nomai film's acting is one list (`acting()` in nomai_signal.h) that the shots read; its four hand-written moves were moved onto it with the frames'
fingerprints unchanged. A gesture ends when its shot does (its out times at the shot's end): one held "for ever" would carry into a later shot that also acts.

**Screen direction across the cuts.** `film_checks::screenDirection` (`ray_tracer --film-cuts <key>`, and the doctor) takes each cut's subjects through the cameras on its
two sides, each camera held for the moment (so a tracking camera shows the walker's real direction): a moving subject that goes left to right before the cut and right to
left after it is a flip (a warning when it is the same thing in the same place; a note when a traveller is cut to another place - a journey should keep its direction), and
two subjects that swap sides mean the camera crossed the line between them (a warning). Only motion more across the view than into it or out of it has a direction to
keep: a walk away from the camera has none. The Nomai film has none of these.

**Music cues in a text file: `pbrt_scenes/film_assets/<key>/cues.txt`.** Read when the soundtrack is made (`src/shared/cartoon/music_cues.h`; the film registry adds the cues
to every film's soundtrack, and a film with a cues file and no score gets one), so a note added, moved or made quieter needs no rebuild, and a cue placed by its shot moves
with the shot when it is retimed:

    [The canyon]                       # times after a section are from that shot's start ([12]: the 12th shot; [*] or none: the film's start)
    2.2   piano D4 3.0                 # notes as MIDI numbers or names; a number with a point after them is the length
    4.0   strings D3 A3 D4  length 6  gain 0.04  pan -0.2  send 0.8
    6.5   pluck A4 C5 E5  spread 0.25  # an arpeggio
    @77.2 bell 86                      # @: a film time

The instruments are the generic ones (bell, mallet, pluck, bass, pad, strings, knock, swell, and the sound effects footstep-<surface>, impact, flash, touch) and the film's
own (its row's `instruments`: the Nomai film's guitar, banjo, piano, resonance, drone and beep). `ray_tracer --film-cues <key>` prints each cue at its film time and what
cannot be played. The Nomai film's three banjo notes on the ridge and the third eye's tone moved from the score's code into its cues file; the mix is the same (46 of its
26 million samples differ by one step, from the order of the sums).

**What a film says about itself** is its row and nothing else, and nothing is typed twice. The row has the key, the name, a description of the story (one sentence, without the length or the number
of shots: those are worked out), the length and fps, the still's time, and a `meta` record: `id` (the number of its video preset, V<id>: give a new film the next number and never reuse one, so
reordering or removing a film moves no preset), `tags` (from the vocabulary `kTagVocabulary`: what the film shows or how it is lit), `shotNames` (one for each shot of its `shotStarts()`),
`samples` (the samples per pixel its own Sampler line asks for) and `secondsPerFrame` (what a 1280 x 720 frame of it takes on the GPU with the denoiser, written by `python scripts/measure_cartoons.py --write`).
The scene list shows all of it for the film's still (length, shots by name, tags, cost), and searching the list for a tag finds the films; the GUI's Shot box beside the Film time box jumps to a shot.

**The film timeline in the GUI.** Beside the Film time box is a slider over the whole film (in tenths of a second). With a film's still chosen, Live Preview draws the moment the
film time is at - its scene id is `cartoon:<key>@<seconds>`, which `cpu_scene_pbrt_path_by_id` turns into a scene file of its own (`pbrt_discover::filmMoment`, written to the
stills folder's `moments` folder; only the latest moment of each film is kept) - from the film's own camera then (`scene_metadata_film_camera`). Scrubbing the slider or the box
draws the new moment a quarter of a second after it rests. A film made from a file is read again when the file (or its scene) is saved: once a second the GUI asks whether it
was (`scene_metadata_film_changed`), and Live Preview draws the moment again from the new file; a saved file with a mistake is said in the log and the film stays as it was.
The film's length and shot names in the GUI stay as they were until the program starts again. A film whose characters have big meshes (the Nomai explorer: a moment of
A Walk at Dusk is 19 MB of scene) takes a few seconds to draw each new moment.
The film table above is the text `ray_tracer --film-table` prints, and a test (`CartoonMeta`) fails when this document, the tags, the shot names, the numbers or a description that restates a length
are out of step with the table; `tests/data/cartoon_films_snapshot.txt` pins each film's `meta` line and what its scene holds (`RT_UPDATE_SNAPSHOT=1` rewrites it).

**Sound.** Every film has a soundtrack (the short ones' are in `src/shared/cartoon/short_scores.h`, each placed on its film's own motion: Lamp and Ball's bounces come from its physics pass, every ball of the parade plays its own note as it lands, each domino ticks as it strikes the next, each drop on the pond plinks where the film draws its ring; a test, `CartoonSound`, holds every film to one). a function `void soundtrack(audio::Mix &)` named in its row (`Film::soundtrack`) that places notes and noises on a mix of the film's length.
`src/shared/cartoon/cartoon_audio.h` is a small synthesiser with no sample files: a few instruments (a marimba-like mallet, a glockenspiel, a plucked string, a pad, a bass), the noises a
cartoon needs (a splash, a knock on wood, a boing, a slide), a voice that chirps, a reverb and a limiter. Every sound is a pure function of its arguments, so a film sounds the same each time it
is rendered. The film can share its timeline with the picture: in "Pip and the Moon" the footsteps, Pip's chirps and the splashes come from the same lists (`pip_moon_story.h`) that move its
feet, open its mouth and strike the water, so what you hear is on the beat of what you see. A video of the film's own length and frame rate (the preset's defaults) gets the sound added by
ffmpeg, and the .wav is kept beside the video (`<name>_video.wav`); a video of the whole film at another length (another frame count or `--speed`) gets the soundtrack stretched to its
length at the same pitch (`audio::stretched`: overlapping 40 ms windows, each placed where it best continues the last), so every sound stays on its picture, and one of the same length at
another frame rate gets it as it is. On a Mac the built-in
encoder cannot add sound, so a film's sound needs ffmpeg there (without it the video is silent and the .wav is beside it).

**Words on the screen: a caption file.** A film's captions are a UTF-8 text file read when the video is made, so a line is corrected, retimed or put into another language with no
rebuild: change the file and make the video again with `--resume` (the GUI always resumes a film): no frame is rendered again, the changed words are laid again on the
pictures as rendered, which are kept beside the frames that have words (`clean_NNNN.png`). tengwang's is `pbrt_scenes/film_assets/tengwang/captions.txt`; a film gets
its own by returning `caption_file::read(film_cache::asset("<film>/captions.txt"))` from its row's `captions`. The file has styles, then captions:

```
# a style: a name and key=value settings (font, index for a .ttc face, size as a share of the frame's height, colour r,g,b from 0 to 1, weight in extra pixels at 1080 lines)
style zh  font=simsun.ttc size=0.057 colour=1,0.965,0.886 weight=0.67
style en  font=palai.ttf  size=0.032 colour=0.925,0.894,0.831
# a caption: start and end in seconds of the film, optional fade= centre= width= gap=, then its lines, each "style: text"
caption 9.0 15.5 fade=1 centre=0.86 width=0.583 gap=0.45 | zh: 豫章故郡，洪都新府。 | en: The ancient commandery of Yuzhang is now called Hongzhou.
```

The text is set in the font at the frame's own size (sharp at any resolution), centred, with a soft dark halo so it reads over a bright sky; a line wider than the caption's `width`
wraps at its spaces into rows of even length. A font is looked for as a full path, then in `pbrt_scenes/film_assets/fonts`, the system's fonts folder and the program's `fonts`
folder; a font that cannot be found is reported once and Noto Sans SC is used instead. A line the file cannot read is reported with its number and left out. The video also gets the
captions as subtitles, `<name>_video.srt` beside it, with times scaled to the video's length (a video made at another length or frame rate keeps its subtitles in step).

## Films from files: animate a pbrt scene without C++

A film does not have to be C++. A **film file** (`name.film`, plain text) animates an ordinary pbrt scene of your own: a camera that cuts between shots, parts that move, turn, grow, light up
and appear. Put the `.film` file and its scene in your `pbrt_scenes` folder (or the per-user scenes folder) and the film is a scene of the Cartoons tab and a video preset like the built-in
ones, with no rebuild. `examples/animated/windmill.film` and `windmill.pbrt` are a complete example (a windmill whose sails turn, a ball that rolls past, a lamp that fades up, two camera shots).

The parts that move are marked in the pbrt file with a comment line right before an `AttributeBegin` ... `AttributeEnd` block:

```
# @anim sails
AttributeBegin
  ...
AttributeEnd
```

and the film file says what happens to them (`#` starts a comment; times are seconds; coordinates are the pbrt scene's own):

```
film     windmill                  # the key: lower-case letters, digits, '-' (default: the file's name)
name     The Windmill              # what the scene list calls it
about    The sails turn, a ball rolls past, a lamp comes on.
scene    windmill.pbrt             # relative to this file (required)
length   12                        # seconds (required; more than 0, at most 3600)
fps      24                        # default 24 (a whole number, 1 to 240)
still    6                         # the moment the scene list's picture shows (default: the middle; after the end: an error)
tags     emissive,dusk             # words from the scene list's tags (others are ignored)
ease     1                         # 1: motions start and stop gently (the default); 0: even pace (wherever the line is); or a curve (below)

shot 0 The windmill                # a camera shot from 0 s; the next shot is a hard cut
  key  0 3 14   0 2.5 0   38       # where the camera is, what it looks at, [fov [lensRadius focusDistance]], [hold S]
  key  -4 3.5 12  0 2.8 0  38      #   (the same `key` lines as a --camera-keyframes file: the camera goes through them)
  ease camera 0                    #   (this shot's camera at an even pace; default 1, gently)
shot 7 The lamp
  key  ...

turn  sails about 0 3.4 0.95 axis 0 0 1   0 0   12 -720    # turn NAME about a point, around an axis: pairs of time and degrees
move  ball  0 0 0 0   12 14 0 0                            # translate NAME: groups of time x y z
grow  flower about 0 0 0   0 0.1   4 1                     # scale NAME about a point: pairs of time and factor
light lamp  0 0   4 0   8 1   12 1                         # the power of the lights in NAME times k: pairs of time and factor
show  ghost 3 5   8 9                                      # NAME is in the scene only between these times
```

Between two keys a value changes smoothly (evenly with `ease 0`; or by a cartoon curve, Blender's keyframe easing: `ease back` overshoots and settles, `ease elastic` springs past and back, `ease bounce` drops onto the value and bounces, `ease in` and `ease out` start or stop gently - for the whole film, for one `move`/`turn`/`grow` line with `move NAME ease back T X Y Z ...`, or for a shot's camera with `ease camera back`: src/shared/ease_curves.h); before the first it holds the first value and after the last the last. A shot with one `key` is a camera that stays; a film with
no shots keeps the scene's own camera. Meshes and pictures the pbrt file names by a relative path are looked up beside the pbrt file. `ray_tracer --film-check FILE.film` loads a film file and its
scene and says what it found or what is wrong, with the file and line; with no file it lists the films found in the scene folders and the files that were left out (a film with a problem is left out
of the scene list, never half-loaded). A film made from a file has a video preset number of its own, `V` and a number from 1000 up worked out from its key, so it never changes when other films
are added. The sound of a built-in film is C++; a film file's is its set's sound (`ambience`, rain, thunder, and the water it has), its characters' footsteps, touches and lines, and its
music: its cues file (`film_assets/<key>/cues.txt`), or with none the score drafted from it (`--film-score`, made when the soundtrack is made, so it follows the film's changes; `music
none` leaves it out, and a film with no music and nothing else to hear gets a soft wind). Every film, built in or from a file, has a soundtrack.

**A film blocked out as a screenplay: characters in a film file.** A film file can bring in characters from the library of kinds (`film_file::characterKinds`, filled in
film_kinds.h: `nomai-explorer`, `pip`, `person` and `turtle`), walk them along paths, act with gestures from their pose library and follow them with the camera, with no C++:

```
character  kai nomai-explorer  at 0 0 4.2  facing 180   # NAME KIND, where it stands (x y z; y is the ground) and which way it faces (0: +z, 90: +x); `detail CELL` for its sculpting
walk       kai  1.0 0.9   0 -1.2   1.6 -5.2             # NAME, from 1 s at 0.9 m/s, through these points (x z) from where it is (corners are turned over 2 m)
gesture    kai  "look up" 12.4 13.2                     # a pose of its library, eased in from 12.4 s to 13.2 s (and out OUT0 OUT1 if given; `contact T`: a touch heard)
shot 4 Along the path
  follow   kai  behind 3.4  up 1.9  side 0.7  fov 36      # instead of `key` lines: the camera behind it, at a height, to its right, looking at its chest
shot 9 The lantern
  key ...
  subject  kai                                          # what the shot is about (the doctor's framing checks); a follow shot is about whom it follows
```

After a walk the character stands with its feet where they came down. A film with characters reports what its shots are about, its characters' feet (the sliding-feet check)
and its footfalls and gesture contacts, which are its soundtrack's footsteps and touches; its cues file adds the music. `pbrt_scenes/a-walk-at-dusk.film` is a complete example:
a set (`film_assets/a-walk-at-dusk/set.pbrt`: a sandy flat at dusk, stones, a path, a lantern that lights), the explorer walking the path, looking up and pointing, four shots
(one following it) and a cues file. A new kind of character is a `CharacterKind` added in `film_kinds::install()`: its scene text and feet at a moment, its chest, its step.

**Several sets in one film: a film in parts.** A film file can be cut into parts with `part NAME [dissolve|fade|wipe SECONDS]` lines. Each part is a film of its own, with its
set (`scene`), its `length`, its characters and walks, its shots, its dressing and its sound, and the parts play one after another as one film (`film_file::partsOf` splits the
file, `film_parts.h` joins the parts' films): its shots are the parts' shots in order, each part's soundtrack is laid at its start (crossfaded into the next), its captions,
footsteps, feet and subjects are moved to the part's start, and a part that comes in with a dissolve or a wipe shows the other part's scene on the other side of the cut. The
lines before the first part are every part's - a cast, a default set, an fps - except the film's own `film`, `name`, `about`, `tags`, `still` (a time in the whole film) and
`music`, which are the whole film's: written inside a part they are an error, as a `shot` or `key` before the first part is, and `credits` belong to the last part
(or before the first). The whole film's cameras, cues and captions files are its own (`film_assets/<key>/`, its shots named as the parts name them); with no cues file each part plays the
score drafted from it. `ray_tracer --film-check FILE` checks each part as the film it is (a problem names the file's own line), and a part can be read on its own by its key,
`<key>--2` (`--film-lines`, `--film-score`). `pbrt_scenes/pip-goes-out.film` is an example: Pip trots down the lantern path at dusk, and after a fade comes out into a morning
meadow on the stage set (`sky`, grass, trees, birds) and cheers.

**The library: casts and sets by name.** A character or a set used in more than one film is kept once in the film library and brought in by name. `cast NAME [as WHO] [at X Y Z]
[facing D] ...` brings in a character as `library/WHO.cast` says it (WHO is NAME unless given): a cast file is what a `character` line says of a character but where it
stands - a `kind` line, then its size, its parts' colours and its voice (`pitch` in semitones, `rate`), one to a line - and the rest of the `cast` line goes on top (`cast kid
as pip-child at 0 0 1 skin #a0d0ff`). `set NAME` reads `library/NAME.set` there: film-file lines - its `scene` (from the set's own folder), `sky`, `place`, `scatter`,
`ambience`, `terrain`, `forest`, `water`, weather, even characters and other sets (four deep at most) - but not the film's own (`film`, `name`, `about`, `tags`, `still`,
`length`, `fps`, `music`, `shot`, `key`, `part`, `title`, `episode`, `theme`, `credits`, `credit`: an error in a set). A film looks beside its own file
first (`library/` next to it), then in `pbrt_scenes/film_assets/library`, which comes with casts (pip, pip-child, kai, ann, sam, shelly, robot) and sets (dusk-path, meadow,
valley, void). A problem in a set's lines is said with the set file's name and line (also one found after the film is read, such as an unknown kind or a block not marked
in the scene); a cast with no `kind` line names the cast file, and a cast file's `rigged` model is found from the cast's folder. `pip-goes-out.film` uses `set dusk-path`, `set meadow` and `cast pip`. Any character can have a
voice of its own the same way: `character bo person pitch -2 rate 0.9` (each line's own `pitch` and `rate` go on top).

**Real voices: lines in the computer's own voices.** `voices system` in a film file (or `voice system` on one character's `character` or `cast` line, or `voice
zira` - a word of a voice's name) has the lines said by the computer's text-to-speech instead of the babble voice (`src/shared/cartoon/film_tts.h`): offline, with no
download - on Windows the System.Speech voices (Settings > Time & language > Speech lists them; Microsoft David and Zira come with Windows), on a Mac `say`, elsewhere
espeak-ng. `voice system` picks one of the installed voices by the character's name, so two characters differ. A line is said into a WAV in the film cache
(`<temp>/photonsmith_film_cache/_voices`, named by its words, voice, pitch and pace), made once by whichever run needs it first and then read by every frame's run; the
film takes it as a recorded voice - the mouth follows its vowels and loudness, the music is ducked under it, its caption lasts as long as it is heard, it is panned and
quietened from where the camera is. The character's `pitch` and `rate` and the line's own go into it (SSML prosody); a small creature (Pip, the turtle) is raised towards
its kind's voice. The speech engine runs as a program of its own with no console window (`launcher/process.h`); the GUI's preview starts none, so there a line not yet
said shows the babble mouth. Real voices are slower than the babble: `ray_tracer --film-lines <key>` lists each line's start and end, to space them. Where the computer
has no voice (or no voice with the word asked for) the line keeps its babble voice and the run says so.

**Acting from the words.** `acting words` in a film file has each line bring the gesture and the expression its words ask for (`src/shared/cartoon/film_acting.h`),
so a film blocked out from a screenplay is acted without a `gesture` and a `feel` for every line: a greeting waves, a yes nods, a no shakes the head, "look" or "there"
points, "hmm" or "maybe" thinks, a happy exclamation cheers, a thank-you bows, a plain question shrugs; the words of a feeling show on the face (happy, sad, angry, scared,
worried, thinking; an exclaimed question or "wow!" is a surprise). Each comes at the word that brings it, one gesture and one expression a line at most, only poses the
speaker's kind has (the turtle has no shrug; the masked explorer no face), and none where the film gives that character a gesture or a feeling of its own during the line
(a script's parentheticals win). The timing is the line's syllables (the babble timing, the same in every program, whatever voice says it). A film made from a script
(`--film-from-script`) has the line.

**Rigged characters from glTF models.** `rigged KIND FILE.glb [height H] [stride S] [turn DEGREES] [walk CLIP] [idle CLIP] [run CLIP]` in a film file makes a kind of
character from a rigged glTF 2.0 model with animation clips - one made in Blender, or from Mixamo (download it as glTF/GLB, or bring its FBX into Blender and export glTF)
- and `character NAME KIND ...` brings it in like any other (`src/shared/gltf_skin.h` reads the skeleton, the skins and the clips; `src/shared/cartoon/film_rigged.h` makes
the kind). Standing it plays its idle clip (one named "idle"; none: its rest pose), walking its walk clip in step with its walk (one cycle of the clip is two steps; its run
clip when it runs), and a gesture plays the clip named as its pose (`gesture bob "wave" 3 3.4 5 5.4`), eased in and out; its clips other than walk, idle and run are its
poses. It is sized to `height` (1.7 by default) and stood on the ground; what its walk clip moves it forward is taken out, since the film walks it. Its materials are the
file's base colours and pictures (written once to the film cache), metal where the file says. A skinned mesh is moved by its joints (four weights a vertex), a mesh
without a skin by its node. It has no face (no `feel`) and its hands are its own (no props). The FILE is beside the film file or in the film library; a cast file can name
its model (`rigged robot.glb height 1.2`) so that `cast robo as robot` is all a film needs: the library has `robot.cast` and `robot.glb`, a small orange robot with idle,
walk, wave, cheer and nod clips, made by `scripts/make_rigged_robot.py` as a real skinned model.

**Changes made in the GUI, written into the film file.** For a film made from a file, the film timeline's row (Settings, a film's still in Render Single Image) has
**Camera** and **Start shot here**. Move the view in Live Preview (it starts at the film time's moment, from the shot's own camera), then Camera > *Use this view for the
shot* writes it as the shot's camera (its `key` lines, a `follow`, a move, `auto` or a flight replaced by one `key`, the shot's own field of view kept), or *Add this view as
the shot's next key* adds it after the shot's keys, so the camera moves on to it. *Start shot here* moves the start of the shot the film time is in to the film time (not
the first shot of the film or of a part, and not past its neighbours; an `auto` shot's own cuts are made by the film). The file is changed in place, line for line, its
comments and order kept (`src/shared/cartoon/film_edit.h`; the GUI asks the renderer's library for the file and its shots as they are now:
`scene_metadata_film_file_shots`), and the film is read again: Live Preview draws the moment again. The shot box's times are the ones the program started with until it
starts again. A built-in film has neither button: its shots are code, and its cameras file changes them.

**Titles, credits and a theme.** `title ["TEXT"] [at T] [for S]` lays a title card over the opening (the film's name unless given, large and centred, fading in and
out); `episode N ["NAME"]` adds "Episode N: NAME" under it. `credits` adds cards of three seconds after the story, over black: the film's name (and episode), its cast
five to a card ("Pip - a pip"), then its `credit "TEXT"` lines, a word on the voices when the computer said them, and "Made with Photonsmith". The film becomes that much
longer; the story fades into the credits (a `Credits` shot), whose frames are a scene with nothing lit in it, so they cost nothing to render; the sky's time of day and the
rest of the story keep the story's own length. `theme jingle` plays a fanfare of its own under the title (a bell's arpeggio over a chord); `theme FILE.wav [gain G]` plays
a recording from the film library (`library/` beside the film file, or `film_assets/library`), so a series can open every episode with the same theme. In a film in parts
the title and the theme are the first part's and the credits the last's, both with the whole film's name. `pip-goes-out.film` has them.

**Hands on the set and on each other.** `touch NAME T0 T1 X Y Z [left|right]` puts a hand on a place of the set (pbrt's coordinates) from T0 to T1 - a wall to lean on,
a door's handle (with a `turn` of the door's block), a lamp to switch on; `hold A B T0 T1` holds A's right hand and B's left together where they meet between them; `hug A B
T0 T1` puts each one's arms round the other's sides. Each is reached for over the 0.6 s before T0 and let go over the 0.5 s after T1, like a prop, and heard as a touch at
T0. The hands go as far as the arms reach: stand two characters who hug close (about a third of a metre chest to chest for two people).

**Shot lighting: key, fill and rim.** A shot can light its subject the way a film crew does (`src/shared/cartoon/shot_lighting.h`): `lighting [key K] [fill F] [rim R]
[side left|right] [on WHO]` in a film file's shot, or `lighting ...` in a section of a film's cameras file (`film_assets/<key>/cameras.txt`, so the built-in films can have
it too). Three spot lights are added to the frame's world, placed round the subject from where the camera is, so they follow every camera: a warm key 40 degrees to one
side of the camera and 35 up, a cool fill 50 degrees to the other side and a little up, and a rim behind and high. They light but are not seen, and their cones are wide
and soft. Each strength is the light it gives the subject (its irradiance, in the units of a sun's `L`: the stage's sun is 2.2), whatever the camera's distance: a light
stands as far from the subject as the camera does and is that much stronger. The subject is what the camera looks at, or the character `on` names (its chest); an `auto`
shot lights each speaker in turn. `lighting` alone is key 1.2, fill 0.4, rim 0.8. `only cast` makes the three light the characters alone and not the set (light
linking, as in Blender: `docs/PBRT_SUPPORT.md`, Lights): a film file puts every character's shapes in the link group `cast`, so `lighting rim 1.5 only cast` lifts a
character off a background that keeps its own light and shadows. `only GROUP` names any other group a scene's shapes are in (`"string linkgroup"`).

**The toon look: cel shading and ink lines.** `toon [bands N] [lines W] [ink COLOUR] [saturation S] [shadow S]` in a film file draws every frame as a cartoon
(`src/shared/toon_look.h`): the light on each surface in a few flat bands, ink lines round what stands in front of something, along sharp creases and against the sky, and
stronger colour. It is made after the render, in the run that rendered the frame, from the picture and its passes (a cheap first-hit pass on the CPU: albedo, normal,
depth, coverage; `launcher/toon_frame.h`), so it works on every backend. The light at a pixel is the picture over its surface's colour, smoothed over the surface without
crossing an edge (the render's noise goes), measured against the picture's bright end (its 95th percentile) and put into bands (the darkest `shadow` of the brightest); the
picture there is the surface's colour - textures stay - in that light, with the light's own colour. Lines are where the depth jumps (a floor seen edge-on draws none) or the
normal turns by more than about 55 degrees, `lines` pixels wide at 1080 lines. The film writes a `# @rt-toon ...` line into every frame's scene, and any scene file with that
line gets the look; `--toon` gives any picture or video it (the scene's style, or the default). Default: 3 bands, lines 2, near-black ink, saturation 1.2, shadow 0.35. A
frame worker reads the frame's scene file again for the passes (`cpu_reload_scene_file`), so each frame's lines are its own. `pip-goes-out.film` uses it.

**Toon lines from the geometry, and a ramp per material** (Blender's Line Art and NPR materials). With the ID passes (`docs/RENDER_PASSES.md`) the toon look also knows
which material and which object is at each pixel. `materials on` draws a line where one material meets another (a character's sleeve and hand), `objects on` where one
object meets another with no depth jump between them - Line Art's intersection lines, a foot on the floor, a cup on a table - and `inner I` draws the lines inside a
shape (creases, materials, objects) at I times the outline's width (`inner 0.5`: the bold outline and fine inner lines of a cartoon). `shadow-tint COLOUR` colours the
darkest band (a cartoon's shadows are blue or purple, not only darker), and `highlight H` with `highlight-colour COLOUR` puts a flat glossy spot where the light is above H
of the brightest. `material NAME ...` or `object NAME ...` starts a ramp of that material's or object's own: the words after it (`bands`, `shadow`, `shadow-tint`,
`highlight`, `highlight-colour`) up to the next one are its, and the rest of the picture keeps the picture's. A material is named by its scene name (a pbrt
`MakeNamedMaterial`); an object is a link group, and a film file's characters are `cast/<name>`: `toon bands 3 inner 0.5 materials on objects on shadow-tint #6a5acd
object cast/pip bands 2 shadow 0.5 highlight 0.92` gives Pip two bands, purple shadows and a glint, the set three bands. All of it is off unless written, so a film's
frames keep their look; when it is on, the frame's ID passes are made with its other passes (`cpu_render_aov_planes` with ids: twelve planes).

**Lines drawn as by hand** (Blender's Freestyle line styles: `src/shared/ink_strokes.h`). More toon words: `line-depth D` (0..1) thins lines further from the camera, as Freestyle's "Distance from Camera" does; `wobble W` lets every line wander up to W pixels (at 1080 high), as a hand's line does; `boil on` draws that wobble afresh every other frame, the shimmer of hand-drawn lines (the film writes each frame's `seed`); `pen ANGLE` makes lines thick and thin like a broad pen held at that angle (a line along the nib is thinnest; `pen-amount A`, 0..1, how much); and `ink-tint T` colours each line from the surface it edges (0 the ink, 1 a deep shade of the surface's own colour), the coloured lines of modern cartoons. A look without these words draws exactly as before. `toon bands 3 lines 3 wobble 2 boil on pen 30 ink-tint 0.6 line-depth 0.6` is a soft, inked storybook line.

**Edges, crevices and a look per layer.** `edges E` lightens the outer edges of things and `cavity C` darkens their creases and corners (0..1 each; `edge-width W` how far, pixels at 1080), as Blender's Workbench cavity does: a hand-painted wear on the edges, from the normal and depth passes (`src/shared/cavity.h`). `cel off` keeps the rendered light (no bands), so lines and edges can go on a realistic picture. And as Blender's view layers composite characters and sets apart, through the object IDs (a film's characters are its `cast/...` groups): `cel cast` bands the characters' light only and leaves the set as rendered (`cel set` the other way), and `paint SIZE` paints the picture (the Kuwahara filter, SIZE pixels at 1080) where `paint-on` says - the set by default, or `cast`, or `all` - before the bands and lines: `toon cel cast paint 10 lines 2.5` is painted sets with graphic, inked characters, Arcane's and Spider-Verse's mix.

**A rim of light and hatched shadows.** `rim R` (0..2) puts a band of light along a character's outline, as a toon material's Fresnel band does in Blender (Layer Weight into a ramp): `rim-width W` pixels wide (at 1080; 5), all of it on the lit side and `rim-shadow S` of it (0.2) on the other, in `rim-colour` (#rrggbb or r,g,b; white). The lit side is where the light is (from the passes); `rim-angle DEG` names it instead (the light's direction on the screen: 0 from the right, 90 from above). `rim-on` says what gets one: `auto` (the default: the film's characters when the scene has any, else everything - the far edge of a floor against the sky has an outline too), `cast`, `set` or `all`. `hatch SPACING` (3..40 pixels at 1080) inks the shadows: lines at `hatch-angle` (45) where the light is in the darker bands, thicker as it darkens, crossed by a second set in the deepest shadow (`hatch-style cross`, the default; `lines` for one set, `dots` for a manga screentone whose dots grow as it darkens), as dark as `hatch-amount` (0.8). Both are drawn in the picture's plane, as a pen or a sheet of screentone draws them, so they stay put while a character moves under them. `toon bands 3 lines 2.5 rim 1 hatch 7` (`src/shared/toon_look.h`).

**The kinds of character.** Each walks with its own gait with its feet held where they come down (a turtle's four), blinks on its own, opens its mouth when it speaks (a
`sample ... by NAME` cue) and has its own poses for gestures; a gesture naming a pose its kind does not have is an error when the film is loaded, and the message lists the
kind's poses.

| kind | what | size | poses |
|---|---|---|---|
| `nomai-explorer` | the masked Nomai explorer, with a staff | 2.1 m | its thirteen (nomai_explorer.h) |
| `pip` | Pip, the round creature with a big head (cartoon_actor.h) | 1.5 m, `scale` | wave, point, reach, cheer, shrug, think, look up, look down, nod, shake head, bow, hop |
| `person` | a cartoon adult in a sweater (human_actor.h) | 1.8 m | wave, point, reach, cheer, shrug, think, hands on hips, look up, look down, nod, shake head, bow, crouch |
| `turtle` | the turtle (turtle_actor.h) | 13 cm long, `scale` | hide, stretch, wave, reach, rear up, swim, look up, look down, nod, shake head |

`scale` on a character line (`character tess turtle at 1.6 0 -2 scale 6`) sizes a kind that can be scaled, its stride and the point the camera looks at with it.

**Character looks.** A character line also colours its parts: `character ann person sweater red hair blond trousers navy`, `character kim pip skin pink`,
`character tess turtle scale 5 shell #3070c0 rim gold`. A colour is a name (red, orange, yellow, green, teal, blue, navy, purple, pink, brown, black, white, grey, cream,
blond, ginger, tan, olive, lilac, mint, sky, gold, silver, crimson, pale, dark, lime, peach, denim), `r,g,b` from 0 to 1, or `#rrggbb` as a colour picker gives it (a '#'
followed by six hex digits is a colour, not a comment). The parts: a person's skin, hair, sweater, trousers, shoes, soles, eyes, lips and cheeks; Pip's skin, belly, feet,
cheeks and eyes; the turtle's skin, shell, seams, rings, rim, belly, cheeks, eyes and brows. A part the kind does not have is refused with the kind's list; the Nomai
explorer's colours are its own.

**Dialogue: `say`.** A line of a film file makes a character speak:

```
say  pip  0.5  "Look at the moon! Is it made of cheese?"      # NAME, from 0.5 s, the words
say  ann  4.0  "No, it is made of rock, I think."  rate 1.2  pitch -1  nocaption   # faster, a semitone lower, no caption
```

One line gives three things (film_speech.h). The timing: each word is cut into syllables at its vowel groups (a silent e is not one; a character of another script, a
Chinese one, is one of its own), about 0.17 s each over `rate`, with pauses at commas (0.22 s) and full stops, '!' and '?' (0.42 s). The mouth: each syllable's vowel gives
its shape - open for an "ah", wide for an "ee", round for an "oo" - the lips meet for an m, b or p, between the syllables of a phrase it goes half open from one shape to the
next and it shuts at a pause; Pip, the person and the turtle draw it, and the masked explorer's staff glows with the voice. The voice: a cartoon babble made from the words, so
no recording or speech engine is needed - a buzz at the character's pitch (its kind's, moved up to 1.5 semitones by its name, lower when it is scaled up) shaped by the three
formants of each vowel, a hiss for an s or an f, a pop for a t or a k; it falls over a sentence, rises at the end of a question and is higher for '!'. The line is panned to
where the camera sees the speaker and quietened with the distance. And a caption: the words from the line's start to a little after its end (cut short where the next line
begins), beside any of the film's captions file (`film_assets/<key>/captions.txt`). A recorded line can be used instead: a cues file's `sample FILE.wav ... by NAME` gives
the character's mouth the recording's own shapes (film_speech.h, `mouthsOf`): every hundredth of a second its first two formants are found (the centres of its energy between
200 Hz and 1 kHz and between 0.9 and 3 kHz), compared in octaves with the centres each of the five vowels gives, and the vowels' mouths blended by how near each is, as open as
the voice is loud; quiet, the mouth shuts. Measured on the Windows speech voice: "father" opens the mouth (open 0.84, round 0.04), "see" widens it (wide 0.79) and "moon"
rounds it (round 0.58). The references are an adult voice's: a child's or a small creature's higher voice is read less well.

**Faces: `feel`.** `feel pip sad 8 8.6 12 13 amount 0.8` eases an expression onto a character's face and off again, as a gesture is eased (no out times: held; `amount` 0
to 2, 1 by default; two at once add up). The expressions (film_faces.h) are happy, sad, angry, surprised, scared, worried, sleepy, smug, thinking and disgusted, each a few
numbers any face can take - the smile, the brows' height and slant, how open the eyes are and how the lids slant, the lids lowered, the mouth hanging open, the ears, a tip and
a tilt of the head - and each kind draws them its own way: Pip with its brows, lids, mouth and ears, the person with brows, eyes and mouth, the turtle with its brows, a
squint for anger and its mouth. The masked explorer has no face; a `feel` line for it is refused (a gesture shows how it feels). A feeling and a line can go together: the
mouth speaks while the brows are sad.

**Props and looks: `prop`, `pick`, `put`, `give`, `look`.** A block of the scene marked `# @anim cup` becomes something characters handle:

```
prop  cup  at 0.75 0.86 0.1          # the block and where its handle is in the scene
look  ann  at cup 1.0 1.5 3.0 3.4    # ann looks at it (her head and eyes; a character's head, a prop, or a point X Y Z), and away again
pick  ann  cup 2.5                   # she reaches for it and takes it at 2.5 s (her usual hand; `left` or `right`)
give  ann  cup 6.5 to pip            # their hands meet where both can reach and it is pip's from then on
put   pip  cup 9.5 at -1.3 0 1.0     # pip puts it down there, where it stays
```

A hand reaches for the prop over the 0.6 s before it is taken, put down or given and settles over the 0.5 s after; in between the holder carries it in front of them, so it
goes where they go, and the prop follows the hand that holds it (whatever else the arm is doing). Reaching low makes a person crouch and bend, Pip lean and the explorer
crouch; the explorer takes things with its left hand (its staff is in its right). Where two hands meet to give a prop is found by letting both reach and moving the point
between where they got to; when they cannot meet the film is refused with how far apart the hands stay ("bring them closer"), as are a pick of a prop someone holds, a put
or give by someone who does not hold it, and two changes to a prop less than 1.1 s apart. Each pick, put and give is a touch on the soundtrack.

**Walks that find their way: `walk NAME T SPEED to X Z`.** Instead of the points of its path, a walk can name where it goes (`to X Z`, as many as it likes, one after
another), and the way is found when the film is loaded (film_paths.h): the set at the walk's start - without its characters and props - is flattened and everything of it
between the walker's ankles and its head (`0.15` m and its height, from the ground it stands on) is drawn on a map of the floor seen from above (triangles cut to that slab,
spheres, disks and cylinders), with the characters standing there as discs of their size; the map is grown by the walker's own size and the shortest way across it is found
(A* over 5-12 cm cells, without cutting between two blocked cells) and straightened wherever nothing is in the way; the walk then turns its corners gradually as any walk.
Walks are found in time order, so a walker goes round whoever stands where an earlier walk left them. A place that is in the way, or one with no way to it, refuses the film
("ann cannot find a way to 0 0: something is in the way there"), and so do two characters whose walks run into each other (closer than 80% of their sizes while one of them
walks; the sizes on the floor: Pip 0.5 m, the person 0.25 m, the explorer 0.3 m, the turtle 7 cm, times their scale).

**Camera moves: `orbit`, `crane`, `push`, `track`, `handheld`, `focus`.** A shot's camera can move about a character instead of following keys:

```
shot 0 Round
  orbit  ann  radius 3.5  degrees 120  height 1.5  from 0   # round her, 120 degrees over the shot from her front (to her left), 1.5 m up
shot 4 Up
  crane  ann  from 0.6  to 3.5  distance 4                # in front of her, 4 m out, rising from 0.6 m to 3.5 m
shot 8 In
  push   ann  from 5  to 1.8  height 1.55                 # in towards her, from 5 m to 1.8 m
  focus  ann  10 pip                                      # (any shot) in focus: ann, then pip from 10 s (pulled over 0.6 s)
  aperture 0.06                                           # its lens (0.02 when a focus is given without it)
shot 12 Along
  track  ann  side 3  height 1.5                          # beside her as she walks, 3 m to her right (negative: her left), level with her
  handheld 1                                              # (any shot) a gentle wander of where the camera is and what it looks at
```

Each move looks at the character's chest and works from where the character is at every moment, so it holds on a character that walks; a crane and a push ease in and out,
an orbit goes at an even pace. A move's character is what the shot is about (the doctor's framing checks). `handheld` (0 to 4) adds a slow, fixed wander - centimetres - to
any shot, an `auto` shot's included; `focus` pulls the focus between characters at the times given.

**Automatic coverage: `auto`.** A shot can leave its cameras to the film:

```
shot 0 Talk
  auto  pip ann       # the characters it covers (none named: those who speak in it, then everyone)
  side  left          # where the first of them is in the frame (left, the default, or right)
```

When the film is loaded the shot becomes shots of its own, named after it ("Talk: everyone", "Talk: pip", ...), so cameras files, cues, the cut checks and the doctor see
them as any shots: a two-shot from its start (everyone from their feet to above the tallest head), a shot of each speaker from 0.15 s before their line - over the shoulder
of the nearest of the others, framed to the speaker's head and shoulders by their size, with a shallow focus on them (one alone: from in front, turned a third of the way to
the camera side) - and back to the two-shot when nobody speaks for more than 2 s; no cut comes less than a second after the last. Every camera stays on one side of the line
from the first covered character to the second, so screen direction holds across the cuts (`--film-cuts` finds nothing to warn of), and each shot is about who it shows (the
doctor's framing checks). The cameras follow the characters over the shot (a key at its start and its end).

**Transitions: `dissolve`, `fade`, `wipe`, `fadeout`.** A line in a shot says how it comes in, over so many seconds centred on its cut:

```
shot 0 Wide
  key ...
  fade 1          # the film fades up from black (the first shot can only fade in)
shot 2 Close
  key ...
  dissolve 1.2    # the wide shot dissolves into this one
shot 4 Side
  key ...
  wipe 1          # this one comes in from the left over the last
fadeout 0.8       # (a film line) the film fades to black over its last 0.8 s
```

They are made when the video is assembled, on the finished frames and before the captions (launcher/film_transitions.h), so a still of the film has none. A frame in a
dissolve or a wipe is two pictures: its own and the other side's - the scene at the same moment seen by the camera of the shot across the cut (`Film::sceneAcross`; with that
shot's cameras-file changes) - which the video renders, for just those frames, into the frames folder's `across` folder; the two are blended in linear light (half way through
a dissolve is half of each one's light, not of their pixel values), a wipe with a soft edge. A fade takes the frame to black and up again. Each frame's own picture is kept
beside it (`plain_NNNN.png`) and what was made is recorded (`transitions.done`), so a resumed video makes nothing twice and a transition that is changed or removed is made
again from the pictures as rendered - without rendering a frame. (Editing a film file otherwise re-renders only the frames whose scene changed: moving a cut renders again the
frames whose camera changed, as for any resumed film.)

**Dressing a set: `place`, `scatter`, `sky`, `ambience`.** A film file can add to its set without touching the pbrt file (film_dressing.h):

```
place    tree  at -3 0 -6  scale 1.2  turn 40         # a tree, rock, fern, bush or grass tuft, its foot there (x y z); `seed` for another shape
scatter  grass 400 in -8 -14 8 3  scale 0.7 1.3  seed 3   # so many of a kind over a rectangle of the ground (x0 z0 x1 z1; `y` its height)
sky      18:20 19:20  south 180  latitude 40          # the sun and sky follow the time of day over the film
sky      5:10 6:10  physical  altitude 1200          # or the atmosphere itself (see "The physical sky and the air" below)
ambience birds 0.7                                    # a sound under the whole film: wind, birds, crickets, water or night, at a gain
```

The things are nature_lib's (a tree about 3.6 m high at scale 1 with a dozen or more leaf clusters, a rock 1 m across), written once as pbrt objects and placed as instances,
so hundreds of tufts cost little; leaves and grass sway in the wind. Grass, ferns and bushes are soft: a walk that finds its way goes through them, while trees and rocks
are in its way. `sky` puts in the Scene Builder's Sun & sky (scene_sky.h): a Hosek-Wilkie sky picture (made once for each tenth of a degree of the sun, in the film's cache
folder) and a sun whose colour and strength come from the air its light crosses, both in the same units; the sun's place comes from the hour, the latitude and the day of
the year (`day`, 172 by default, the longest), with `south` saying which way south is on the set (as a character's `facing`; west is a quarter turn on from it); below the
horizon the sun goes out and the sky dims into twilight. A film with a `sky` leaves out its scene's own infinite and distant lights. The ambiences are made like the films'
own (crickets, birds and wind as in Forest Dawn and the Nomai film; a brook of rushing noise and bubbles; night is crickets with an owl far off).

**Cartoon marks: `mark`.** (film_marks.h, src/shared/screen_marks.h) The signs a cartoon draws by a character, drawn over the finished frame as a 2D layer, the way
Blender's Grease Pencil draws over a render:

```
mark pip !  4.2                  # a "!" pops up over Pip at 4.2 s and goes 1.8 s later
mark ann ?  6 8.5                # a question mark from 6 s to 8.5 s
mark pip speed 10 12             # speed lines behind Pip while it moves
mark kim heart 3 colour red size 1.3
mark at 0.5 0.4 -2 impact 7.3    # the jagged star of an impact at a place (pbrt's coordinates)
```

The kinds are exclaim (`!`), question (`?`), sweat, anger, heart, zzz, sparkle, impact and speed. Each is a flat shape of colour inside a dark ink outline. It pops in past
its size and settles, moves in its own way and shrinks away at its end: a `!` jolts, a `?` rocks, a heart floats up and beats, sweat runs down, the anger mark throbs, the
z's rise one after another, sparkles twinkle, and speed lines are drawn afresh twelve times a second, as hand-drawn ones would be. A sign goes over the top of its character
(sweat and anger beside its head) and is as big as the character is on the screen, so it follows the character and the camera. Speed lines trail the character's chest the
way it moves on the screen, and only while it moves. The film writes each frame's marks, worked out through that moment's camera, as `# @rt-mark` lines in the frame's
header. The launcher draws them last, after the render, the look and the toon look, so nothing shades, grades or blurs them, and they are never lit and cast no shadow.
An EXR is left as rendered.

**Cloth: `cloth`.** (film_cloth.h) Flags, capes, sheets and curtains, simulated as Blender's cloth modifier simulates them. The cloth is points joined by links that keep
its weave's lengths and resist folding; gravity pulls it, the wind blows each little triangle by how squarely it meets the air, and it is kept out of the ground, the body it
hangs on and the boxes it falls over. It is solved as position-based dynamics in small steps (300 a second):

```
cloth flag at 2 3 -4 size 1.2 0.8 colour red                    # a flag on a pole standing from the ground to 2 3 -4 (`pole H`: a shorter pole)
cloth cape on pip length 0.8 colour purple                       # from a character's shoulders, swinging as it walks
cloth sheet at 0 1.6 0 size 2 2 drop 3 over 0 0.4 0 1.2 0.8 0.8  # held flat, let go at 3 s, falls over a box (centre, size)
cloth curtain at 0 2.5 -3 size 2 2.2 facing 0 colour cream        # hangs from a rail and stirs in the air
```

Every cloth also takes `stiff S` (0 silk to 1 canvas; 0.5), `wind S` (how much the wind blows it; 1) and `ground Y`. The wind is the film's `wind`; with none, a
flag flies in a 4 m/s breeze and a curtain stirs. A frame is a function of its time. The cloth is simulated from a second before the film (so it has settled) in fixed
steps up to the frame. A process rendering frames in order keeps where it got to, so the next frame takes only the steps between, and a frame comes out the same in any
order. A cloth renders as a smooth triangle mesh (with uv, ready for a texture), and a flag gets its pole.

**Cartoon motion: `animate`, `squash`, `jiggle`.** (film_motion.h) Three of animation's principles, as Blender's animators get them:

```
animate on twos                     # every character holds each pose for two frames; the camera still moves every frame
animate pip on threes from 10 to 14 # one character, for a while (ones, twos, threes, fours)
squash 1                            # squash and stretch for every character (0 off, up to 2); `squash pip 1.5` for one
jiggle pip 1.5                      # follow-through: Pip's antennae swing behind its head and settle (0 off, up to 3)
```

*On twos* is the hand-drawn feel (and Spider-Verse's), Blender's Stepped Interpolation modifier. A character, its face, its lip sync, its cape and the marks over it
all keep the pose of the frame it holds, counted from the line's start; a character's own line wins over the film's. *Squash and stretch* deforms the whole character,
keeping its volume. It stretches along the way it moves, more the faster it goes in its own heights a second (up to a third longer: a smear when it dashes). As it comes
to rest, a bouncy spring on its body runs on past its stop and back: it squashes on landing and wobbles before it settles. A jump reads crouch, stretch up, round at
the top, stretch down, squash. *Jiggle* hangs what dangles from a character's head (Pip's antennae) on a loose spring that follows the head and the bob of its walk.
All three are off unless a film asks, so a film without them draws exactly as before. A frame is still a function of its time: the springs run over the second
before it (rig::follow).

**Smear frames: `smear`.** `smear pip 1.5 multiples 3` (or `smear 1` for every character) draws a dash as Blender Studio's animators draw one: the character drawn out back along its way, its leading side where it is, over S frames' travel (0 off, up to 3), and with `multiples N` (up to 4) copies of it left along that way - the cartoon's multiples. Only at a dash's speed: none below 2.5 of its heights a second, all of it from 5, so a walk or a run is drawn as it is. On top of `squash`'s stretch (film_motion.h).

**A rigid-body world: `rigid`.** As Blender's Rigid Body World (our own small solver: impulses with friction and bounce, sleeping; src/shared/cartoon/rigid_world.h), the scene's blocks become boxes and balls that fall, knock into each other and the ground, tumble and come to rest:

```
rigid b1 box 0.3 0.3 0.3 at 0 0.15 0                                # three blocks in a stack (each `# @anim bN` in the scene, its middle where `at` says)
rigid b2 box 0.3 0.3 0.3 at 0 0.45 0
rigid b3 box 0.3 0.3 0.3 at 0 0.75 0
rigid ball sphere 0.15 at -2 0.15 0 start 0.5 velocity 5 0 0 mass 3   # a ball rolled into them at 0.5 s
rigid apple sphere 0.08 at 0.6 1.6 0.4 start 1.2                   # an apple let go at 1.2 s
```

Each body rests where the scene has it, asleep, until something pushes it (`start T` with `velocity X Y Z` and `spin X Y Z` in degrees a second, or another body hitting it), so a stack stands until it is knocked. `turn DEG` (a box turned about the vertical, as the scene has it), `mass M`, `bounce B` (0..1), `friction F`, and `fixed` for a still obstacle the others hit. pbrt's coordinates, as `bounce` takes them; the ground is at 0. The world is worked out once from the film's start in steps of 1/480 s, so a frame is the same whichever is rendered first, and its hard landings are heard (film events). Boxes meet at their corners and faces (an edge resting on an edge is not caught).

**Onion skins and motion paths: `--film-onion`.** `ray_tracer --film-onion pip-goes-out 1 4 sheet.png [960 540] [EVERY]` writes an animator's sheet of the stretch from 1 s to 4 s, as Blender's onion skinning and motion paths show one: the frame in the middle in a quick flat shading, faint ghosts of the characters every EVERY frames (3) before it in red and after it in green, and each character's path over every frame as a line with a dot a frame - dots far apart are fast, bunched are slow, so the spacing shows the easing. All seen by the middle frame's camera. It is made from the scene's first-hit passes on the CPU, not a render: about a second a frame for a big set, to check timing, arcs and spacing before rendering (launcher/onion_frame.h).

**Shaped bokeh: `aperture ... blades`.** A shot's `aperture 0.05 blades 6 rotation 15` gives its lens six blades: out-of-focus lights are drawn as hexagons, as a real lens draws them; `ratio 1.5` squeezes the aperture into an anamorphic lens's tall ovals. A pbrt Camera's `"integer blades"`, `"float bladesrotation"` (degrees) and `"float apertureratio"` (our extension; Cycles' Blades, Rotation and Ratio, `src/shared/aperture_shape.h`); under 3 blades (the default) the aperture is round, as before. CPU and both OptiX renderers.
**Weather and particles: `weather`, `particles`, `burst`.** (film_particles.h)

```
weather   rain 0.8 from 4 to 14            # rain, snow, leaves, dust or ash; amount 1 a good shower (up to 4); eased in and out over 1.5 s; `ground Y`
particles sparks at 1 0.9 -2  3 5  rate 80 # sparks, dust or a splash thrown from a place from 3 s to 5 s, 80 a second (`speed S`)
burst     splash at 0 0 -1  6.2  count 60  # all at once
```

Every particle is a fixed function of time - its birth and its throw come from its number - so frames stay independent. Weather fills tiles of the world 10 m square round
the camera (out to 14 m) with the same pattern in each, so it stands still in the world whatever the camera does and is as dense near the camera as it should be however big
the set: rain as thin light streaks the length a drop falls while the shutter is open, facing the camera, with a dot of a splash where each lands; snow as little balls that
drift; leaves tumbling as they fall. Sparks glow (each a small light) and fall, dust rises and settles, a splash's drops (water) arc and fall back. Rain is heard: a hiss that
grows and fades with it. (Rain and snow cast shadows: a low sun stretches them into lines.)

**Temporal denoising: `--temporal-denoise [R]` (`film.py video <key> --temporal 2`).** A film's frames are rendered one by one, in any order, so a filter over time can
only work on what the frames are when they are all done: launcher/film_temporal.h makes each frame again from itself and the R frames either side in its shot (1 to 3, 2
when not given; never across a cut), taking a neighbour's pixel in as far as the 5 x 5 neighbourhood round it is the same in both frames - the same picture under other
noise - and not where something moved or the light changed. With it every frame is rendered with a seed of its own (the base `--seed` and its number): with one seed for
all, the noise is the same in every frame (it does not flicker where nothing moves, but it does not average away either). The denoised frames go to the frames folder's
`temporal` folder, which the video is assembled from (the frames as rendered are kept); each is remembered with the frames it was made from, so a resumed video makes again
only those whose neighbours changed. A still camera on a walking person at 8 samples: the error against a 512-sample picture went from 2.84 to 2.14 (the floor from 3.21 to
2.10, the walker's box from 4.54 to 3.91 - moving parts are not smeared), and the frame-to-frame flicker of the floor from 3.79 to 1.54.

### Better cartoons, round 4 (2026-10-29)

**Clean face shadows: `face-normals`.** `face-normals ann 1` (or `face-normals` for every character) shades a character's head as if it were a sphere, so a toon or cel light gives the face one clean shadow shape instead of a band broken up by the nose, the lips and the brows, as anime artists get with hand-edited normals. Each material of the character gets `"point3 normalcenter"` (the middle of its head), `"float normalradius"` and `"float normalblend"` (0 to 1, how much): within the radius the shading normal turns to the sphere's, fading out to the mesh's own by 1.3 radii (src/shared/proxy_normal.h; CPU, both OptiX renderers).

**Walks with a personality: a gait word.** `walk ann 0.2 0.9 sneak 3 0` - the word after the speed is one of `sneak`, `tiptoe`, `stomp`, `strut`, `skip`, `shuffle`, `stagger` (none: plain). Each gives the walk its own stride, crouch, bounce, lean and sway: a sneak crouches and leans in with long slow steps, a tiptoe rises onto its toes with short ones, a stomp drops hard onto each foot, a strut leans back and rolls its hips, a skip hops, a shuffle drags short steps bent forward, a stagger weaves and turns. Easing in as it sets off and out as it stops (film_motion.h, gaitPose), for every kind of character.

**Anticipation and settle: `anticipate`.** `anticipate 1` (or `anticipate ann 1.5`, up to 3) works out from each character's own motion what the principle of anticipation asks for, so no key has to: before it sets off it leans back, and it drags back as it speeds up; when it stops it overshoots forward and settles upright; before a turn it turns a little the other way, and after one it turns on past and back (film_motion.h, anticipation).

**Motion capture: `mocap`.** `mocap bob dance.bvh at 2 [to 6] [speed S] [turn DEG] [travel]` plays a BVH motion capture (CMU, Mixamo, Rokoko exports) on a rigged character (`rigged KIND FILE.glb`) from 2 s: the capture's bones are matched to the model's by name (Hips, Spine, LeftUpLeg, mixamorig:..., with the usual spellings), each bone's turn is carried over from the capture's rest pose to the model's (a T-pose capture on an arms-down model works), and `travel` moves the character with the capture's hips (scaled to its height) instead of dancing on the spot. Blended in and out over 0.3 s. The file is looked for beside the film (or, for a `mocap` line of a library set, in the set's folder), then in film_assets/library (src/shared/cartoon/mocap_bvh.h).

**Characters push the rigid world.** Every character is a moving body to the `rigid` world: its body (a ball of its size at half its height) and its feet push the blocks they walk into, so a walk through a stack knocks it over and a kick sends a ball rolling - with nothing else to write. They are not pushed back (rigid_world.h, `Kinematic`).

**Matte paintings: `matte`.** `matte valley.png from 0 1.5 6 to 0 1.5 -10 [fov 45] [distance 60] [floor 0] [bright 1]` projects a painted background from a place, as a 2.5D matte painting is: the painting is laid onto a shape along the projector's rays at that distance (or where they meet the floor, with `floor Y`), so a camera that moves a little in front of it sees parallax as on a real set. It glows with the painting and lights nothing (it is not sampled as a light, and only lights itself), and casts no shadow. The picture is looked for beside the film, then in the library (film_matte.h).

**Grading with a LUT, and a colour script.** A cameras file's `look` line (or `--look`) takes `lut warm.cube [lut-mix 0.8]`: a .cube grade (3D or 1D, Resolve, Photoshop and OCIO all write them) laid on the finished picture before the look's print (halftone, vintage ...), looked for beside the film, in the library's `luts` folder, then in the library (src/shared/cube_lut.h). `ray_tracer --film-colours <key> script.png [WIDTH] [gpu]` makes the film's colour script, as a production designer pins one up: a small render of the middle of every shot in order, each over a strip of its five main colours and a band of its average, so the film's colour arc can be seen at a glance (launcher/colour_script.h).

**Samples a shot from a noise target: `--noise-target N`.** Before the frames, each shot's middle frame is rendered twice at a quarter of the size and a quarter of the samples under two seeds; half their difference, against the picture's brightness, is the shot's noise. Each shot then gets the samples that bring it to N (0.005 to 0.5; 0.02 is clean), from a quarter to four times the samples asked for: a foggy night shot gets more, a plain daylight one fewer, so every shot comes out about equally clean for the samples spent. The factors are written beside the frames (`shot_samples.txt`) and handed to every frame's run (launcher/noise_budget.h, shot_samples.h).

**Rendering on more than one computer: `--render-farm DIR`.** Start the same film with the same settings on each computer (a PC and a Mac, say) with the same shared folder - a Windows share, a Mac's shared folder, a network drive:

```
ray_tracer --gpu --video --camera-path cartoon:pip-goes-out --render-farm \\pc\films\pip --output pip.mp4 1920 256 12
```

Each takes the next frames no one has taken, twelve at a time, renders them as it always would and puts each finished picture in DIR; when no frame is left to take it waits for the others' and makes the video from all of them, so every computer that took part ends with the whole film. Nothing has to be started first. A frame is taken by creating its claim file, which only one computer can do; a computer rewrites its claims every minute while it renders, so a claim not rewritten for ten minutes is a computer that stopped and its frames are taken again (a computer's own claims from a run before are taken back at once). A frame that fails twice is given up and filled from its neighbour, as any failed frame is. `DIR/farm.txt` holds the video's settings (film, frames, size, samples, depth, draft, seeds, noise target): a computer started with others is refused. With `--noise-target` one computer measures the shots for all. Both computers need the same version of the program and the film. Two runs on one computer need `RAY_TRACER_FARM_NAME` set to a name each (launcher/render_farm.h).

**Storyboard and animatic.** `ray_tracer --film-storyboard <key> board.png [WIDTH] [colour]` draws the film as a story artist would: a panel a shot (two or three for a long one), three to a row, each a pencil drawing of the shot with its number, time and length under it and the film's words spoken in it. `ray_tracer --film-animatic <key> animatic.mp4 [WIDTH] [FPS] [colour]` draws every frame (12 a second, 480 wide) with the shot and the time in a corner and makes them a video with the film's soundtrack, to watch the cut, the timing and the sound together before anything is rendered. A drawing is made from the frame's first-hit passes, not a render - four pencil tones with hatching in the darkest, ink where one thing ends and another begins, lighter lines further away, and detail too fine to draw (far grass) left out - so a 25-second film's animatic takes about three minutes (launcher/storyboard.h, pass_drawing.h).

### Nature films

A film file can be a landscape with nothing of the set's own in it: `pbrt_scenes/film_assets/wild/set.pbrt` is an empty set (a camera, a sampler, a plain sky that a `sky`
line replaces) to name with `scene` and build on with the lines below.

**The land: `terrain`.** (film_terrain.h)

```
terrain  mountains  size 1500  height 320  seed 3   # hills, mountains, dunes, canyon or coast (sizes and heights of their own when not given)
terrain  canyon  focus 40 -120  snow 200  erosion 1.5  y -10  at 0 0  far off
```

The land is a square `size` metres across centred `at` X Z (pbrt's), its ground from `y` up to `height` above it. It is made from its numbers alone, the same in every frame and
every process: a grid of heights (257 x 257, and eight samples beyond each edge) from noise of the kind's shape - mountains are warped ridged noise gathered into massifs with a
valley floor round the `focus` (the centre when not given), hills gentle noise, dunes asymmetric ridges (a long windward face, a steep slip face) across a wind that comes
from the seed, a canyon mesas in steps with cliffs between and a river winding through the focus, a coast a sea floor on its -z side shelving up (as d ~ x^(2/3), with a
sandbar off the beach) to a beach at a quarter of its height, the shore bending into bays, rocky headlands dropping straight into deeper water, and hills behind (40 m high
unless given; with a sea and no `y`, its beach is put at the sea's level) - then carved by tens of thousands of droplets that run downhill, pick up
ground as they speed up and drop it as they slow (`erosion`, 0 to 3: mountains 1, hills 0.7, canyons 0.5, dunes none). Where the water ran most is the land's flow.

What is drawn: one mesh over the square whose rows and columns crowd round the focus (about 40 cm apart there, several metres at the edge; about 160,000 points), its
heights the grid smoothed (Catmull-Rom) with a little detail on top; one picture over the whole square painted from the height, the slope and the flow at each point (grass
and lighter meadows in the valleys, darker growth where water gathers, scree above, bare rock on the steep faces, snow above the snow line where it is not too steep to hold -
`snow` metres above `y`, mountains at 0.45 of their height when not given; sand, paler on the crests; a canyon's red and ochre bands by height); the cliffs as meshes of their
own (the steep triangles, cut by which way they face) with a picture projected from the side - rock strata, or the canyon's bands - since a picture painted from above smears
down a cliff; and the far ranges, a ring from the square's edge out to four times its size that carries the same land on beyond the square (mountains growing higher with
distance) without a step where they meet, painted the same way and towards the blue-grey of distance (`far off` leaves them out). The meshes are PLY files and the pictures HDR
files in the film's cache (film_cache.h): a mountain range is made in about 20 s the first time and read in a fraction of a second after that; a frame of it renders in about
a second at 640 x 360 and 32 samples.

On a terrain every height in the film file is above the ground: what is placed or scattered stands on it, and a character brought in at X Y Z stands Y above it; walks find
their way over it like any set's floor (film_paths.h reads its meshes). (Two limits: the side pictures are projected along x or z, so a cliff facing 45 degrees between them
shows them stretched by up to 1.4; the GPU renderers read pictures without a mip pyramid, so the painted ground shimmers a little when seen from far above.)

On a terrain the camera's `key` heights are above the ground too: the camera's above the ground under it, and what it looks at above the ground under that.

**Water: `water`, `river`, `waterfall`.** (film_water.h)

```
water     sea  level 0  waves 1.2  wind 200    # sea, lake or clear; its surface's height (pbrt's y, not above the ground); the waves' significant height and the way they run
river     0 -300  20 -100  0 0  width 12        # waypoints downstream (pbrt's x z), or `river from X Z` (downhill from there) or `river canyon` (a canyon's riverbed)
waterfall at 40 12 -80  width 8  drop 18  facing 180   # the middle of its lip (on a terrain, 12 m above the ground there)
```

*The sea* is an ocean spectrum (film_ocean.h, Tessendorf's FFT ocean): a 128 x 128 tile of waves of every length the wind raises (Phillips' spectrum for the wind that, fully
risen, makes waves of `waves` significant height; few run against the wind), each turning at the speed deep water gives its length, summed by an inverse FFT each frame -
heights, the sideways pull that sharpens the crests (`choppy`), slopes, and where the surface folds over (the foam). A second tile a quarter the size, turned, adds chop that
does not line up with the first, so the tiling is not seen. (`model gerstner` keeps the older sea of twenty summed Gerstner waves.) It is written every frame as a mesh in front of the camera only: a
wedge a little wider than the picture, its rings crowding round the camera - centimetres apart at its feet, tens of metres at the horizon, out to 8 km, flat beyond 2.5 km so the
horizon is a clean line (about 16,500 points, about 1.2 MB of text a frame). Ripples too small for the mesh are in its normals, fading with distance. Foam lies on the crests the
waves fold over (`foam off`). The water is a smooth coat over the colour of deep water, so the sky is in it at a grazing angle and its body shows looking down; a lake is calmer
(5 cm when `waves` is not given) and greener; `clear` water is glass (eta 1.33) and shows the ground under it. On a terrain the water fills what lies below its level: the ground
just above the level is drawn under it (a clean shore, not islands as small as the fine detail), the shore is painted sand, wet sand and mud, and foam laps up the beach and back
along the waterline (traced over the terrain's grid every frame).

*A river* is a ribbon through its waypoints, smoothed, whose water never runs uphill. On a terrain its way is worked out when the film is loaded - in the lowest ground across
its width, or downhill from a point (`from`: steps down the slope of the ground with a little momentum, until it leaves the square or the ground stops falling), or along a
canyon's own riverbed (`canyon`) - and a bed is cut into the terrain under it, so no rise of the ground hides it. Its ripples are carried downstream at its `speed`, and it is
white where it drops fast.

*A waterfall* pours from its lip in the arc thrown water takes (out at about 1.2 m/s, falling under gravity, spreading as it falls): strands of white water broken into clumps
that fall with it, in front of a see-through sheet, onto a pool with foam churning where it lands, and spray thrown up there (a `particles splash` the length of the film). On a
terrain the land is shaped for it - raised behind the lip into a plateau whose front is the cliff (painted with the cliffs' side picture), lowered before it into a basin for its
pool; elsewhere it brings a face of rock of its own. Water is heard unless the film file says how: a sea brings an `ambience sea` (the wash, a wave breaking now and then), a
river `ambience water`, a waterfall `ambience falls` (a roar).

**Clouds you can see round: `clouds`.** (film_clouds.h)

```
clouds  cumulus   cover 0.4  base 900  wind 90  speed 6    # cumulus (800 m across), towering (1500 m, from 1000 m up) or wisps (high, thin, 5 km up)
clouds  towering  cover 0.2  density 1.4  area 8000  at 0 -2000  seed 3
```

Each cloud is a volume of droplets - a `uniformgrid` medium inside a pass-through sphere, which every renderer draws: it scatters the sun (forward, g 0.8), is dark
underneath and bright where the sun shines through its edges, and throws a shadow that moves over the land. Its densities (48 x 22 x 48 for a cumulus) come from blobs - one big
one, a ring of smaller ones heaped round it, a few on top, a broad skirt low down - cut flat at the base where the air stops rising and eaten into billows at the edges by 3D
noise; a towering cloud is a tower of billows of every size from a broad base to a crown and an anvil; wisps are long thin streaks. Each is written once as a MakeNamedMedium in a
file of the film's cache and named in every frame by an `Include` inside the transform that places it (each stretched along x by up to 1.7, so no two look alike). The clouds are
spread over a square `area` metres across round `at` X Z (6 km, 12 km for wisps), as many as `cover` asks for, never so close that their spheres overlap (a renderer follows one
medium at a time); they drift with the `wind` at `speed` and wrap round the area. `sigma_s` is 0.09 per metre times `density`. Several `clouds` lines make layers (keep their
heights apart). The camera may fly into a cloud (on the Mac it sees one only in part from inside). They cost little: a frame of a hilly land with a dozen cumulus renders
in about 2.5 s at 640 x 360 and 128 samples (with the denoiser).

**A deck of cloud to fly through, and fog banks: `clouds deck`, `fogbank`.** (film_clouds.h; nature round 9, 2026-10-27)

```
clouds   deck  base 520  thick 220  cover 0.7  area 7000      # one layer over the area, its gaps letting the sun down
fogbank  at 0 0  size 1500  depth 60  density 0.05  wind 80  speed 3  during 2 18
```

A deck is one cloud layer over a square `area` metres across (8 km): a 160 x 14 x 160 grid closed over about its `cover` share of the sky (0.75) by noise, with gaps, a
flat base at `base` (600 m), a heaving top `thick` metres up (250) and a fading border; `sigma_s` 0.03 per metre times `density` (some 30 m to see through inside). It
drifts (keep the area bigger than the film's drift, it does not wrap). A camera can climb through it: a grey ceiling with gaps from below, a whiteout inside, a sea of
cloud with the peaks standing out above. A fog bank pools in the low ground round `at` X Z (a place in a valley or on a lake): `size` metres across (600), its flat top
`depth` metres over the ground there (40) - lower ground fills deeper, higher stands out of it - dense most of the way up and thinning to a heaving top, in wisps,
`density` its scattering per metre (0.03). It drifts with `wind` at `speed`, and with `during T0 T1` it settles in at T0 and burns off (sinks) by T1. The land runs
through both and the camera may be inside either: every renderer samples a volume along each ray up to the nearest surface, and the GPU cuts a grid volume's flight to
its own box, so a deck's kilometres-wide sphere does not hide a fog bank under it (`pbrt_scenes/fog-under-cloud-deck.pbrt`). `ray_tracer --film-scene KEY@SECONDS OUT.pbrt`
writes a frame's scene text to look into.

**An eruption: an ash column, lightning in it, falling ash, pyroclastic flows.** (`volcano` options; film_fire.h; nature round 9, 2026-10-27)

```
volcano  at 0 -1500  height 450  radius 900  erupting 2 60  column 3000  wind 90  lightning 8  ashfall 1  flow 14 toward 20  flow 30
weather  ash 2                                                 # ash on its own, as rain or snow
```

`column H` turns the plume into an ash column climbing H metres (300 to 20000) over the crater's rim: about 120 m a second, slowing as it goes (6 to 60 s to the top),
widening as it climbs and leaning downwind (`wind`, a compass bearing, 90 by default), and once it is up its head spreads into an umbrella (out to 0.6 H from its middle) drifting
downwind. It is one grey `uniformgrid` of 56 x 56 x 56 written into each frame (some 1 MB of text), its foot lit orange from inside by the lava fountains. `lightning N`
(1 to 100; it needs a column) flashes N bolts in the column from a third of the way up down to the crater and the upper flanks, the first once the column is half up, each
lighting the ash round it, its thunder heard. `ashfall A` (up to 4) brings flakes of ash fluttering down round the camera once the umbrella has spread over, until 30 s after
the eruption. `flow T [toward DEGREES]` sends a pyroclastic flow down from the rim at T (in the eruption), the way given or a way of its own, down the land as lava would go:
it races off at a tenth of the volcano's radius a second and slows as it spreads, reaching up to 2.4 radii; a grey cloud hugging the ground, thicker and wider the further it
has gone, its head billowing highest, lofting and thinning away behind over half a minute, its roar heard from its front. Its grid stays below the column's, so the two
never overlap. The camera may be inside any of them.

**The sea's giants: `whale`, `dolphins`.** (film_giants.h; nature round 9, 2026-10-27)

```
water    sea  level 0  waves 0.6
whale    at -40 -120  toward 90  length 14  speed 2  breach 14  breach 40   # `during T0 T1`, `seed N`
dolphins 6  at 30 -40  toward 270  speed 6                                   # `during T0 T1`, `seed N`
```

A whale is a humpback (`length` 4 to 30 m, 14 by default; a body sculpted as a signed distance function and meshed once: long white pectorals, a pale throat, broad
flukes) swimming along its heading (`toward`, a compass bearing) at `speed` (2 m/s). Every 16 s or so its back rolls up out of the water and down again - gently up,
steeply down, its flukes lifting last - and it blows as it comes over: a bushy spout of spray that hangs and drifts. Its body follows its way as a train its track (each part
where the part ahead was), so it arches over the roll and its flukes beat. At `breach T` (as many as you like, inside its `during`) it bursts out nearly upright 2.7 s
before landing (for 14 m: launched at 15 m/s, flung right clear), twists onto its back as it falls and crashes down: spray as it comes out, a great crash of spray as it lands
(a couple of thousand drops), a patch of foam spreading on the waves and breaking up over half a minute, bubbles if the camera is under water; round a breach it swims deep.
Dolphins (1 to 40, 2.6 m long, grey with pale bellies) porpoise along their heading at `speed` (6 m/s): each leaps clear in a low arc every 2.6 to 3.8 s (a metre above the
water, a second in the air), a splash where it leaves and where it goes back in. Heard: the whale's blows, the breach's wash and the boom of its crash, its song under water;
the dolphins' splashes and their whistles. Both need a `water sea`. They move in nature's time, so `slowmo` slows a breach.

**Living light on a night: `bioluminescence`, `fireflies`, `fungi`.** (film_glow.h; nature round 9, 2026-10-27)

```
bioluminescence  strength 1  colour blue                     # the stirred sea glows: blue, green or teal
fireflies 300  in -40 25 20 70  height 3  together          # `during T0 T1`, `brightness B`, `seed N`
fungi    40  in -25 35 5 60  colour green  brightness 2     # clusters of glowing mushrooms
```

`bioluminescence` makes the sea glow where it is stirred: the foam on folding crests, the wash along the shore, the white water of `surf`, and the spray and foam of whales
and dolphins give out light (they become emitters, so they light what is round them). `fireflies COUNT in X0 Z0 X1 Z1` (up to 2000) wander slowly over the patch, up to
`height` metres above the ground (2), each a tiny glowing sphere flashing yellow-green for a third of a second every 2 to 4 s, a faint glow between; with `together` their
flashes drift into one shared beat (2.5 s) over the first half minute until the whole swarm flashes as one. `fungi COUNT in ...` (up to 500 clusters) puts 3 to 12 small
mushrooms in each, pale stems and glowing round caps 2 to 6 cm across, breathing slowly brighter and dimmer. Every glow is divided by the frame's exposure (when it is
above 1), so it looks as bright on the screen however far `exposure auto` opens the night camera up - `strength` and `brightness` are how it looks, not how much light it
is. `bioluminescence` needs a `water` line.

**Ice: `glacier`, `iceberg`, `seaice`, `waterfall ... frozen`.** (film_ice.h; nature round 9, 2026-10-27)

```
glacier   from 0 400  toward 180  length 500  width 140  thick 30  calve 12   # `seed N`; `calve T` as often as you like
iceberg   at -120 -250  size 40                                               # `seed N`: its shape
seaice    cover 0.5  floe 10                                                  # `seed N`
waterfall at 0 30 -40  drop 25  width 8  frozen
```

Ice is pbrt's `subsurface` material (light goes in and comes out blue, the reds absorbed over a few metres); faces that look up carry snow, the steep ones show the ice.
A glacier is a tongue of ice laid over the land from `from` along `toward` (a compass bearing): `thick` metres in its middle (0.18 of its width when not given),
thinning to nothing at its edges, its surface following a smoothed bed and heaved, cut by fields of crevasses across its flow (bowed downstream, more towards its front
and its edges). It ends in a fluted cliff that stands down into the sea where there is one (needs a `terrain`). At `calve T` a slab off the front (a twentieth of its
width deep, a third of it wide) leans out, topples into the water in 3 s with a crack and a roar, bursts into spray and foam, then floats off; the front stands back by
the slab's depth. An iceberg floats on the sea (`size` metres across at the water, 1 to 2000): flat-topped and steep-sided above, a broad keel below, bobbing and turning.
`seaice` covers `cover` of the sea round the camera (out to 40 floes) with floes `floe` metres across riding the waves. `frozen` hangs a waterfall in ice: icicles from
its lip, a quarter of them reaching the bottom as columns, a frozen pool, no spray and no roar. Icebergs and sea ice need a `water` line.

**One wind, and tornadoes: `wind`, `tornado`.** (film_wind.h; nature round 9, 2026-10-27)

```
wind     100 12                                                # the way it blows (a compass bearing: 0 +z, 90 +x) and m/s, 0 to 60
tornado  from -500 -900  to 400 -600  during 2 38  width 40  height 600   # `seed N`
```

`wind` is one wind over the whole set, taken by everything that moves with the air unless its own line gives its own `wind` (or `speed`): clouds drift with it (at 1.5
times its speed: faster aloft), fog banks (at 0.3 of it), the sea's waves run with it, rain, snow, dust and ash slant and stream with it, a volcano's ash column and a
wildfire go with it, a storm blows with it, smoke from a fire bends away from it, and the grass and trees lean with it - a steady lean (its speed / 12, up to 1.5) under
gusts that sweep across them (`gusts` at its speed / 8 when the film has no `gusts` line). A tornado crosses from its first point to its second over `during` (at least
12 s), wandering: its funnel lowers from the cloud over the first 6 s and ropes out over the last 6, a twisting cone `height` metres tall (its cloud base, 700) narrowing to
a rope `width` metres across at the ground (40), leaning and snaking more as it ropes out; round its foot a churning cloud of dust with dark debris whirled round and
flung out, or over water a ring of spray (a waterspout); heard as a roar. The funnel is one grid and the cloud at its foot another below it (they never overlap); keep a
`clouds deck` base above its top. (`tornado N at X Y Z ...`, with `at` as its second word, is a different line: a barracuda tornado under the sea, round 16.)

**Fur and feathers: `herd ... fur A`, `flock ... feathers A`.** (film_fur.h; nature round 9, 2026-10-27)

```
herd     wolves 6  from -40 0  to 200 -20  fur 1     # deer, horses, wolves: 0 to 4
flock    eagles 2  at 0 40 0  radius 30  feathers 1   # eagles and geese: 0 to 4
```

A coat of strands over each posed animal: small tapered ribbons rooted on the skin, shared out by the rest pose's areas and placed by their numbers, so the same strand
stays on the same spot from pose to pose and the coat does not crawl; each leaves the skin a little, lies back along the body and droops. A deer's coat is 3 cm strands
(6000 at `fur 1`), a horse's short and sleek (1.8 cm, 9000), a wolf's long and shaggy (6 cm, 7000); a bird's plumage is broad, blunt, flat-lying feathers (3.5 cm, 4000).
Only the coat, legs, plumage, head and tail grow it - not hooves, antlers, eyes or a beak. The strands are a little lighter than the skin under them, with a sheen. They
are triangles, not pbrt curves: each pose is an object instance, and an instance holds only triangles and spheres. A pose with its coat is its own object
(`anim_wolves_run_3_coat100`), so a herd still costs a few poses.

**The sky's optics: `optics`.** (film_sky.h, film_dressing.h; nature round 9, 2026-10-27)

```
optics   halo 1  rays 0.8  fogbow 0.7  alpenglow 1  greenflash     # each 0 to 1; any of them
```

Painted into the sky's picture, as bright as the sky they are on (as the rainbow is): `halo` the 22-degree ring round the sun (reddish on its inner edge, the sky inside it
a little darker), sun dogs at the sun's height just outside it (red towards the sun, a white tail away from it) and a faint 46-degree ring; `fogbow` a broad white bow 40
degrees round the point opposite the sun, faintly orange outside and blue inside; `rays` crepuscular rays, beams of light and shadow fanning from the sun low in the sky,
and fainter anticrepuscular rays converging opposite it. A sky without them keeps its old picture (its cached file's name is unchanged). `alpenglow` is a rose light
from 1.2 degrees above the horizon where the sun went, from 2.5 degrees before sunset to 6 degrees after: the peaks catch it while the valleys are in their shadow.
`greenflash` puts a green fleck where the sun's last sliver sets (or first rises) for the moment its centre is 0.05 to 0.35 degrees below the horizon, divided by the
frame's exposure so it shows green; it sits 30 km out at the true horizon, so mountains hide it as they hide the sun - look over the sea or a plain.

**Caustics traced from the surface, and a rainbow in a waterfall's spray: `caustics traced`, `waterfall ... rainbow`.** (film_caustics.h; nature round 9, 2026-10-27)

```
caustics   traced                                            # (or `painted`, the default)
waterfall  at -28.5 25 33  drop 30  width 10  facing 139  rainbow
```

`caustics traced` works the net of light out from the water's surface instead of painting it: a tile of ripples 4 m across (two dozen waves 15 cm to 1.5 m long, each
running at the speed deep water gives it, looped over 2.4 s), and through each of half a million points of it a ray of sunlight refracted into the water (Snell's law)
and followed down to the bed - bright where the rays crowd, dim where they spread, the average the light a flat surface lets through. Worked out for beds 0.3, 0.6, 1.2,
2.4, 4.8 and 9.6 m down (in between, blended), it sharpens to bright lines where the ripples focus (a metre or so) and coarsens and softens deeper, as a real net does.
It is used wherever the films draw caustics: the seabed under water (for the depth under the camera), the light shafts, and the sand of the shallows (each point for its
own depth). `rainbow` on a `waterfall` makes its spray (a sphere round its foot) glow in the bow's colours wherever the camera sees it 40.5 to 42.5 degrees from the point
opposite the sun, red outside, violet inside: a faintly emitting volume that hardly absorbs, so it neither hides nor shadows what is behind it, densest in the middle of
the spray. It shows only while the sun is up and behind the camera, and moves with the camera as a real one does: to see it, put the fall about 40 degrees to one side of
the camera's shadow.

**The ground close up: `closeup`, `tracks`.** (film_ground.h; nature round 9, 2026-10-27)

```
closeup  size 16                                                  # `cell C`: a point every C metres (0.05)
tracks   from -3 0 to 30 -4  during 2 30  kind boots              # boots, paws or hooves; `stride L`, `seed N`
```

`closeup` lays a patch of fine ground over the land in front of the camera (`size` metres across, centred a third of its size ahead of the camera, snapped to a 1 m
grid so it does not swim), a point every `cell` metres: wind ripples a hand apart in sand (dunes, a coast, a desert biome), soft drifts and wind-carved ridges in snow
(when `snowcover` is over 0.3), grit and pebbles on bare earth; painted with the land's own colours, snow where it lies, darker and glossy when it is wet; its edge eases
down into the land. On it, footprints: a `tracks` trail - one print every half stride, left and right in turn, made step by step from its first point to its second over
its `during` - and the hoof and paw prints of every herd along the ways its animals have come over the last half minute; each a dent with a rim pushed up round it (boots
with a heel and a tread, paws with a pad and four toes, cloven hooves), deep in sand, snow or mud and barely there on dry earth. On a coast the band from 0.3 m under to
0.5 m above the sea's level is wet sand the wash has just left: its ripples washed smooth, dark, under a film of water as smooth as glass (`coateddiffuse` roughness 0),
so the sky shows in it. It needs a `terrain`. The patch is some 100 000 points (at the defaults): keep it for shots near the ground.

### The universe (round 10, 2026-10-27)

Films set in space: planets and their air, rings and moons, nebulae, galaxies, stars, comets, black holes, ships and the people in them. Their lines are read by
film_parse_space.h; each feature's own file is named with it. **Scale:** the GPU works in single precision, so keep a space film's world within a few hundred kilometres
of the origin - planets a few thousand to some tens of thousands of metres across, some tens of kilometres off - and let the cuts and the camera's moves tell the real
distances (the Nomai film's gas giant is 17.5 km across at 60 km).

**A set in space: `space`.** (film_space.h)

```
scene   film_assets/wild/set.pbrt
space   stars 1  milkyway 1  sun 200 10          # `sunsize DEGREES` (0.53), `sunlight S` (1), `sun off`, `seed N`
```

No ground, no sky, no air (a film with `space` has no `sky`, `storm`, `air` or `fog`; the set's own sky lights are left out): black, with some 60 000 stars all round,
painted into the sky's picture - their brightnesses spread as the real sky's (each magnitude fainter about two and a half times as many, from -1 to 9), their colours
from their temperatures (a black body's, worked out from Planck's law through the CIE curves: most a little orange, a few blue-white, some deep red), the faint ones
crowding towards the Milky Way. It is not a catalogue of the real stars: the constellations are not there. The Milky Way is a band of light round a great circle,
brighter and wider towards its centre (bearing 66, on the horizon), knotted with star clouds and broken by dark lanes of dust. The sun is a disc painted into the same
picture at `sun BEARING ELEVATION` (degrees; 60 15 by default), `sunsize` across, a little darker towards its rim, so bright that its light is the films' sunlight (3
units of irradiance) times `sunlight`: the sky light's importance sampling finds it, a planet's night side is dark and shadows are as soft as the sun is big. (Not a
glowing ball far off, as the Nomai film's sun: the wavefront renderer, which draws every frame with a volume in it, lights almost nothing with so small and far a sphere.)

**A floating origin.** The GPU renderers work in float, whose positions grow coarse far from the origin (0.06 m at 1000 km), so every frame of a film with `space`
is moved - its camera and its whole world, by the same whole number of kilometres - to put the camera within a kilometre of the origin (film_space.h `recentred`). A
film need not do anything about it. (Before 2026-10-28 the GPU's camera rays themselves snapped to a grid of directions far out, and a planet 75 km away was drawn
in blocks; that is fixed in the renderers.)

**Planets: `planet`.** (film_planets.h)

```
planet  home   ocean  at -75000 0 -20000  radius 6000                 # `tilt DEGREES` (15), `spin SECONDS` (600), `seed N`
planet  giant  gas    at -45000 0 -20000  radius 6000  tilt 10
```

A world lit by the sun (`space`'s; a day side, a night side and the soft line between), a pbrt sphere with its surface painted once into a picture for its kind and seed,
its poles up, tipped by `tilt`, turning once every `spin` seconds of nature's time. Kinds: `rocky` (grey highlands, dark plains, craters with soft bright rims and rays),
`desert` (rust and ochre, dark rock, dunes in bands, white polar caps), `ice` (white and pale blue, crossed by long brown cracks), `ocean` (continents of forest, grass,
desert and mountain by latitude under swirls of cloud, white caps; its seas are smooth, a coated surface whose roughness is a second picture, so they catch the sun in a
glint), `gas` (a giant: bands of cloud torn into eddies at their edges, a great oval storm) and `lava` (dark crust broken by glowing rivers of lava that light its night
side). Each needs `at X Y Z` and a `radius` (1 to 200 000 m), and its own name.

**A planet's air: `planet ... air S [colour C]`.** (film_planets.h)

```
planet  home  ocean   at 0 0 -20000      radius 6000  air 1                 # 0 to 10; 1 an Earth's
planet  dune  desert  at 40000 0 -20000  radius 6000  air 1  colour orange  # blue (the default), orange, white or green
```

A shell of air round the planet, densest at its ground and thinning by e every scale height (1.2% of its radius), nothing above eight of them. It scatters sunlight
- `blue` as air does (Rayleigh: blue about eight times as strongly as red, scattering evenly every way), `orange` a haze (forward-scattering), `white` cloud, `green` -
so its rim glows against space, the day side is a little hazed, and along the line between day and night the light through the air comes red. Thick enough at `air 1`
that a line of sight grazing the ground passes through about one and a half of its own lengths in blue. It is one grid (144 cubed) written once into the film's cache,
in a sphere round the planet; the frame is then drawn by the wavefront renderer, which samples it along every ray.

**Night sides: `planet ... cities S aurora S`.** (film_planets.h)

```
planet  home  ocean  at 0 0 -20000  radius 6000  tilt 30  air 1  cities 1  aurora 1     # each 0 to 10
```

`cities` lights the planet's land with the warm glow of cities - more just inland of the coasts, in clusters, the bigger ones joined by faint roads, none at sea, under
the ice caps or on a gas giant - an emitting picture on the planet's own sphere, so it shows on the night side and is drowned by day. `aurora` hangs an oval round each
pole at about 67 degrees of latitude, wavering and drifting with nature's time: a curtain of light from 1% to 3.5% of the radius above the ground (the films' aurora
picture: green below, red above) standing in a glowing band 5 degrees wide, which is what the oval looks like from orbit. It is drawn over the night side only (an
emitting ribbon, which casts a shadow like glass, would otherwise stripe the day side).

**Rings: `planet ... rings INNER OUTER [opacity S]`.** (film_planets.h)

```
planet  giant  gas  at 0 0 -40000  radius 8000  tilt 25  rings 1.3 2.4      # in the planet's radii, 1 to 10; `opacity` 0 to 3 (1)
```

A ring system in the planet's equator's plane (tipped with it): 160 ringlets of ice and dust (pbrt disks with an inner radius) from INNER to OUTER planet radii, each as
wide as the rings are dense there - bands of their own, a wide dark division at 62% of the way out and a narrow gap at 92%, a faint dusty inner edge - so the stars and
the planet show through where they are thin (more with a lower `opacity`). Lit by the sun: the planet's shadow falls across the rings behind it, and the rings' own
striped shadow falls on the planet.

**Moons and eclipses: `moon`.** (film_planets.h)

```
moon    luna  rocky  of home  radius 1500  orbit 9000  period 80  phase 60  incline 5     # and any planet option: air, rings ...
```

A body going round another (a planet or another moon, named on an earlier line) on a circle `orbit` metres from its middle, once every `period` seconds of nature's
time (negative: the other way), starting `phase` degrees round, its orbit's plane tipped `incline` degrees; it is a planet of its own in every other way. Moons cast their
shadows across their planets and pass into their planets' shadows (the sun does it). **Eclipses:** when a planet or moon, seen from the camera, covers the sun, the
camera sees the sun's disc eaten away (the bodies hide it); from 97% covered, the diamond ring - a blaze at the limb where the last of the sun peeks out - and in
totality the corona: pearly streamers round the black disc (an emitting card far beyond the body, its picture painted once). A body a little bigger in the sky than the
sun gives a total eclipse; a smaller one, an annular one or a transit. For a narrow shot (12 degrees or less) with the sun in it, the sun is drawn as a glowing ball
instead of the sky picture's disc, so it stays round however close the lens (the picture leaves it out then).

**Nebulae: `nebula`.** (film_cosmos.h)

```
nebula  veil    at 0 0 -30000       size 20000  kind emission      # `brightness B` (1, up to 20), `seed N`
nebula  towers  at 120000 0 -30000  size 20000  kind pillars
```

A cloud of glowing gas `size` metres across: `emission` - its gas glowing the deep red of hydrogen outside and the teal of oxygen towards its hot middle, knotted and
wispy, its outline ragged, crossed by dark lanes of dust that hide and redden what is behind them; `dark` - a cloud of dust in front (towards a camera looking down -z)
silhouetted against glowing gas behind it; `pillars` - three columns of dust rising from a dense base in glowing gas, their edges lit. Four young blue-white stars burn
near its middle. It is one "rgbgrid" medium (72 cubed; it absorbs, scatters and gives out light per voxel) written once into the film's cache, in a sphere round it - the
camera can fly into it. Its glow is calibrated by eye to about the brightness of a sunlit planet at `brightness 1`.

**Galaxies: `galaxy`.** (film_cosmos.h)

```
galaxy  swirl  at 0 0 -60000       size 50000  tilt 55                       # `brightness B` (1, up to 20), `seed N`
galaxy  bar    at 120000 0 -60000  size 50000  kind barred  tilt 20  turn 30
galaxy  oval   at 240000 0 -60000  size 30000  kind elliptical  tilt 40
```

A galaxy `size` metres across its disc: `spiral` - two arms of young blue stars winding out from a warm yellow bulge, pink knots of glowing gas along them and dark
dust lanes on their inner edges; `barred` - the same, its arms starting from the ends of a bright bar through the bulge; `elliptical` - a smooth yellow glow, brightest
in its middle, fading out. Untilted its disc faces a camera looking down -z; `tilt` tips it about x (90: edge on, its dust lane across it), `turn` turns it in its
own plane. It is an "rgbgrid" medium in the box round its tipped disc (144 voxels along its longest side), written once into the film's cache.

**Stars close up: `star`.** (film_cosmos.h)

```
star  sol    at 0 0 -30000       radius 5000  kind sun       # `brightness B` (1, up to 20), `seed N`
star  ruby   at 100000 0 -30000  radius 8000  kind red
star  rigel  at 200000 0 -30000  radius 4000  temperature 26000
```

A star seen close: a glowing ball in the colour of its temperature (red 3200 K, orange 4500, sun 5800, white 9500, blue 26000, or its own `temperature K`,
1500 to 50000), its surface a picture painted once - granulation, two belts of sunspots (a dark umbra in a lighter penumbra) with bright faculae round them - turning
slowly; round it a glow (a small emitting grid out to three radii, streaked a little) that falls off with height. It is a light: it lights planets near it. The `space`
sun is a separate thing (the far sun in the sky); turn it off (`space sun off`) for a film round another star.

**Flares and prominences: `star ... prominences N flare T`.** (film_cosmos.h)

```
star  sol  at 0 0 -30000  radius 5000  kind sun  prominences 14  flare 2  flare 18     # 0 to 40 prominences; as many flares as you like, each at its time
```

Prominences are arcs of glowing plasma rising off the star's limb and falling back into it, each rooted at both ends in the surface, up to half a radius high, swaying slowly;
they glow orange-red and are seen best against the black beside the star. A flare at time T (nature's time) blazes on the surface - a white-hot spot that flares in half a
second and fades over a few - and throws off a cloud of plasma: a shell of glowing blobs that swells outward and fades over a minute, then is gone.

**Black holes: `blackhole`.** (film_cosmos.h)

```
blackhole  maw  at 0 0 -30000  radius 2000  tilt 8  jets 18     # `disc D` (8, 4 to 30 radii), `turn`, `spin S` (1; negative turns it the other way), `brightness`, `seed`
```

A black hole as films show it: its shadow, a black ball `radius` across, and round it an accretion disc out to `disc` radii - 260 streams of gas going round, white-hot at the
inner edge (three radii) and orange to red further out, the inner ones faster (Kepler's pace: ten seconds a turn at the inner edge, slower outside). The side of the disc coming
towards the camera is brighter and bluer, the side going away dimmer and redder (Doppler beaming, worked out for where the camera is in every frame). `tilt` tips it from edge-on
(8 degrees: nearly edge-on, the classic view). The part of the disc behind the hole is also drawn bent up over the top of the shadow, and fainter under it, with a thin bright
photon ring hugging the shadow - the way its gravity bends that light towards the camera; these follow the camera too. `jets L` adds two beams of blue-white plasma knots
streaming out of its poles to L radii. It is a cartoon of the lens, not a lens: the stars behind it are not bent.

**Comets: `comet`.** (film_cosmos.h)

```
comet  halley  at -8000 0 -30000  move 400 0 0  size 400  tail 22000     # `away X Y Z` (the tails' way; else away from the `space` sun), `brightness`, `seed`
```

A comet: its nucleus, a dark lumpy rock `size` across (pocked with craters, tumbling slowly), where it is at time 0 and moving `move` metres a second; round it the coma, a
green-white glow; and its two tails `tail` long, pointing away from the sun whichever way the comet moves - the ion tail straight, thin, blue and streaked, the dust tail
broader, warm white and curving back along the way it came. The glow is one grid in the film's cache (in the comet's own frame, made once and carried along with it); it is
dense enough to be smooth on the GPU, so it dims the stars behind it a little.

**Asteroid belts and fields: `asteroids`.** (film_cosmos.h)

```
asteroids  belt   at 0 0 -30000  radius 20000  width 4000  count 4000  size 400  tilt 10     # a ring going round its centre
asteroids  chase  at 0 0 -3000   kind field  radius 2500  count 1500  size 120              # a ball of drifting rocks to fly through
```

Rocks by the thousand, most of them small and a few `size` across, grey to brown, each tumbling: a `ring` (the default) is a band `width` wide (at most `radius`) round its centre at `radius`, a
tenth as thick and thickest in its middle, its rocks going round at Kepler's pace (a turn in ten minutes at the band's middle, the inner ones faster; `spin` scales it and
negative turns it the other way), tipped by `tilt` and turned by `turn`; a `field` is a ball `radius` round whose rocks drift slowly. Every rock is a placement of one of
eight rock shapes (lumpy, cratered), so 20000 of them (the most) cost little. Real belts are almost empty; these are as crowded as a film wants them.

**Dying stars: `remnant`.** (film_cosmos.h)

```
remnant  crab   at 0 0 -40000       size 12000  kind supernova  blast 3     # a red giant until nature's time 3, then it blows up
remnant  vela   at 60000 0 -40000   size 8000   kind pulsar  period 2  tilt 25
remnant  ring   at 120000 0 -40000  size 6000   kind planetary
```

A `supernova` is a red giant until `blast T`; then a white flash swells and fades over a few seconds while its shell of gas races out - torn into filaments and knots,
blue-white inside and red at its edge - slowing as it nears its `size`. A `pulsar` is a tiny blue-white star whose two beams of light, out of poles tipped `tilt` from its
spin axis, sweep round once every `period` seconds like a lighthouse's, in a smaller glowing cloud of its wind (a ring round its waist, jets from its poles). A `planetary`
nebula is an hourglass shell of teal gas rimmed with red round a tiny white-hot dwarf. Each glow is one grid in the film's cache (a supernova's is the same grid, placed
bigger as it grows: it brightens as it spreads).

**Ships and stations: `ship`.** (film_cosmos.h)

```
ship  ark     at 0 0 -500      kind shuttle    size 60   move 0 0 -40          # flies nose first along its `move`
ship  wasp    at 40 10 -300    kind fighter    size 14   move 5 0 -90  seed 2
ship  mule    at -300 -50 -900 kind freighter  size 300  move 0 0 -15  colour 0.7 0.5 0.3
ship  haven   at 0 0 -4000     kind station    size 200  heading 30
ship  spindle at 0 0 -8000     kind wheel      size 500  spin 1.5              # turns a minute
```

A craft `size` long (or across), where `at` says at nature's time 0 and moving `move` metres a second with its nose along its way (still: `heading` and `pitch`). A `shuttle`
is a white body with delta wings and a tail fin; a `fighter` slim with four wings in an X and four engines; a `freighter` a long spine hung with coloured containers, a bridge
at its bow and one great engine; a `station` modules round a hub with wings of solar panels on a truss, turning slowly; a `wheel` a ring on spokes, its windows lit, spinning
`spin` turns a minute. `colour R G B` paints its hull. Its engines glow blue-white and flicker while it moves (`engines on|off` to choose), and it carries running lights - red
to port, green to starboard, a white strobe (`lights off` to put them out).

**Astronauts and zero-g: `astronaut`.** (film_cosmos.h)

```
ship       ark  at 0 0 -40  kind shuttle  size 30  move 0 0 -2
astronaut  kim  at 0 0 -10  tether ark  floaters 6  tumble 5           # tethered to the ship (named before it) wherever it is now
astronaut  lee  at 3 1 -8   tether 0 0 -30  suit 0.95 0.55 0.2  move 0.05 0 0
```

Someone floating in a spacesuit `size` tall (1.8 m): a white suit (or `suit R G B`), a backpack, a helmet with a gold visor that mirrors what is round it and two lamps,
arms and legs drifting in the slow sway of zero-g; where `at` says at nature's time 0, drifting `move` metres a second and tumbling `tumble` degrees a second (4) about a
tilted axis. A `tether` is a lazy line from their waist - sagging and swaying - to a point, or to a `ship` named before them (it follows the ship). `floaters N` sets loose
things drifting and spinning round them: wrenches, bolts and wobbling globes of water. `lamps off` puts out the helmet lamps.

**The camera's journeys: `spacecam`.** (film_voyage.h; a line of a shot, in place of its `key`s)

```
shot 0 Round
  spacecam orbit  0 0 -30000  radius 9000  degrees 120  from 0  elevation 15     # round a planet, 120 degrees over the shot
shot 10 Past
  spacecam flyby  0 0 -30000  radius 5000  from 30                             # a slingshot: in from far, swinging past, out again
shot 20 Down
  spacecam zoom   0 0 -30000  from 1000000  to 4000  bearing 0  elevation 20   # powers of ten: the same factor closer every second
shot 30 Jump
  spacecam hyperspace  0 0 0  to 500000 0 0  bearing 90  elevation 0           # streaks, a flash halfway, out at `to`
```

A shot's camera on a journey about a point, over the whole shot. An `orbit` goes round it `radius` away from the bearing `from` through `degrees`, `elevation` up. A `flyby` is a
slingshot: in from far away, swinging past it nearest (`radius`) at the bearing `from` and out the other side - fastest where it is nearest - looking ahead along its way with
the body passing beside it. A `zoom` closes from `from` metres to `to` metres (or opens, the other way) along `bearing` and `elevation`, the distance changing by the same factor
every second: the powers-of-ten shot from a galaxy to a planet (the floating origin keeps it sharp). A `hyperspace` jump looks along `bearing`/`elevation` from the point: the
stars stretch into streaks of light round the camera as it lunges forward, a white flash fills the screen halfway, and it comes out at `to` as the streaks shrink back to
stars. `fov F` sets the lens for any of them.

**Light in space: `space ... flare F bloom B planetshine S`.** (film_voyage.h, lens_look.h)

```
space  sun 60 10  flare 0.4  bloom 0.15  planetshine 2
```

In space there is no air to soften the light, so what makes it look photographed is the lens. `flare F` (0 to 1) adds a lens flare from the brightest lights in the
picture - the sun, a flash, a star's blaze, not a lit planet: a streak drawn out sideways from each and a row of ghosts mirrored through the picture's centre in the amber,
blue, green and rose of a lens's coatings. `bloom B` spreads their glow (as `# @rt-look bloom`). Both go into each frame's look line (and are a `# @rt-look flare F` for any
scene: lens_look.h). `planetshine S` lights what is near a planet with the planet: its sunlit face as a broad lamp in its surface's colour, as bright as how much of that face
is turned to the camera and how big the planet looks - so the night side of a ship, a moon or an astronaut beside a bright planet is filled with its blue or ochre light rather
than black (the path tracer finds that light too, but slowly and noisily; this is a fill on top of it).

**Sound in space.** (film_voyage.h; `space ... hum H`)

No sound carries in space, so a space film's sound is the cinema's: what is near is heard at once (not a sound's 343 m/s later), quieter further off and panned to its side,
with little reverb. Under it all is the hum of space - a low rumble and a slow deep tone (`hum H`: 0.6, up to 4; `hum 0` for silence). Ships rumble while their engines burn,
so a craft passing swells, sweeps across the picture and fades; near an astronaut the camera hears them breathe inside their helmet and their radio's blip; a star's flare
lands with a deep whoomp and a supernova's blast with a boom and a long rumble; a black hole drones so low it is felt more than heard; a jump to hyperspace rises in a rush
as the stars stretch, booms at the flash and falls away. All of it is the film's soundscape, under its music and dialogue like a nature film's sounds.

**Re-entry and landing: `descent`.** (film_voyage.h)

```
descent  pod  from 0 20000 -60000  to 0 0 -2000  start 0  land 30  kind shuttle  size 10  chutes on   # `heat H` (1), `colour R G B`, `seed N`
```

A craft coming down from `from` (high and fast) at nature's time `start` to touching down at `to` at `land`, along a curve that runs long and shallow and then drops, slowing
all the way, its nose along its way but never steeper than 35 degrees down. Early on it is wrapped in re-entry's fire: a glowing bow shock before its nose and a burning wake
streaming back along its path, shedding sparks (`heat H` makes it fiercer, 0 none). Then its three parachutes open (`chutes off` to leave them out); just before it lands its
retro-rockets fire and kick up a ring of dust from the ground; then it rests where it landed. It roars while it burns, and the retro-rockets' blast and the thump of touching
down are heard from where it is (sound in space, above). Put a planet with air under it (or land on a terrain) for the ground it comes down to.

**Forests: `forest`.** (film_forest.h)

```
forest  mixed 4000  season autumn                 # conifer, broadleaf, birch, palm, shrub or mixed; so many trees where the land suits them
forest  conifer 800  in -300 -400 300 0  below 180  steep 0.15  scale 0.7 1.4  near 150  seed 3
```

Each species is grown once in four variants: a conifer as a trunk with twenty tiers of drooping arms, each with pads of needles, round a dark core; a broadleaf tree as a short
trunk whose limbs spread wide and fork into twigs, inside a crown of leaf clusters filling an ellipsoid (most towards its outer shell, where they show); a birch slender and white
with a tall narrow crown; a palm a curved trunk with a crown of arching fronds; a shrub a mound of leaves on a few stems. Each variant has a near version (tens of thousands of
triangles: every leaf) and a far one (a trunk and lumps or a cone of foliage in its place, the same colours); both are PLY files in the film's cache wrapped in pbrt objects, and
every tree is a placement of one of them, leaning a little in the wind. Trees nearer the camera than `near` metres (120, times the tree's size) are drawn near, the rest far, so a
valley of 4000 trees is about 4,000 placements of a few dozen objects: a frame of one renders in a few seconds.

Where they grow: tried at points of a fixed sequence over the rectangle (`in X0 Z0 X1 Z1`; the whole terrain when not given), kept on ground no steeper than `steep` (0.2: about
37 degrees; no trees on cliffs), out of the water and its edge, out of rivers' beds, below the tree line (`below H` metres above the land's base; for mountains 0.55 of their
height, conifers getting smaller towards it), above `above`, and more often where water gathers (the terrain's flow). A `mixed` wood is chosen by height: broadleaf trees, birches
and shrubs low down, conifers coming in on the way up, conifers alone near the tree line. Seasons: `summer`; `autumn` (each broadleaf tree orange, red, yellow or brown, birches
yellow); `winter` (broadleaf trees and birches bare, showing their twigs; conifers with snow on their tiers); `spring` (fresh light green, some broadleaf trees and shrubs in
blossom). Walks do not go round forest trees (they are not part of the set's floor map).

**The land in action: `geyser`, `rockfall`, `avalanche`, `quake`.** (film_geology.h)

```
geyser     at 0 60  height 30  every 30  last 10  first 1   # a vent erupting every 30 s for 10 s, the first at 1 s
rockfall   from 120 -200  at 4  count 12  size 1.2          # rocks breaking away and coming down the slope
avalanche  from -200 -350  at 2  width 70  speed 22         # a powder avalanche racing down
quake      10 14  strength 1                                 # the ground shaking
```

A geyser is a vent on a low, pale mound of sinter with a steaming pool round it; each eruption builds over a second, throws a column of water up as high as `height`
(gouts and spray arcing out and falling back), and dies away over its last two seconds, while a plume of white steam rises and drifts and lingers after it (and wisps from
the pool between). A rockfall's rocks break away where the land is steepest within 25 m of `from`, a few metres apart and over a second or so, and come down the steepest
way: gravity along the slope against a rolling friction, hopping as their speed throws them, tumbling, a puff of dust where each lands, until the ground levels out. An
avalanche races down the steepest way from where it breaks away, from rest up to its speed: a cloud of snow crowding behind its front and billowing up the further back it
is, over a white flow on the ground. A quake shakes the camera, fast and jolting, eased in and out. All are heard (film_soundscape.h): the geyser's roar while it erupts,
each rock's knock and crack as it lands, the avalanche's rumble from its front, a quake's rumble all round. A rockfall and an avalanche need a `terrain`; their ways down
are worked out when the film is loaded.

**Documentary cameras: herds and flocks as subjects, `slowmo`.** (film_file.h)

```
herd   horses 14  from -40 0  to 400 0  flee 2
slowmo 6 12 0.25                         # from 6 s to 12 s nature runs at a quarter of its speed
shot 0 Alongside
  track herd1 side 14 height 1.6 fov 38  # beside the herd as it gallops (a vehicle alongside)
  focus herd1                            # and focused on it
  aperture 0.04
shot 12 Above
  follow flock1 behind 40 up 10 fov 30   # after the eagles
```

A shot's moves and focus take a herd or a flock as they take a character: `herd1`, `herd2` ... and `flock1` ... are the film's herds and flocks in the order of their lines.
A herd's place is the middle of its leading animals (the bunch the camera is on), its front the way it is going, and the camera looks at its backs; a flock's is the middle
of its birds or fish (geese: their leader), its front the way they fly. So `follow` drives behind a herd, `track` runs alongside it, `orbit` circles it, `push` and `crane`
move in on it and rise over it, and `focus` keeps it sharp - as it walks, grazes, bolts and gallops away. They are the shot's subjects for the film doctor's framing checks.

`slowmo T0 T1 [RATE]` is a high-speed camera's slow motion: from T0 to T1 nature's time - the animals, the water and surf, the clouds, the wind - runs at RATE of the
film's (0.25 when not given), eased into and out of over 0.4 s, while the camera keeps the film's pace; `timelapse` sets its speed elsewhere. A herd's hoofbeats and the
eagles' cries keep time with what is seen.

**Biomes: `biome`.** (film_terrain.h, film_forest.h)

```
terrain  hills  size 1500  height 40
biome    savanna                 # golden grass and red earth, acacias and bushes, crickets and wind
biome    jungle  plants 6000     # deep greens and mossy rock, kapok giants over palms, broadleaf trees and ferns, birds and insects
```

A biome is a land's colours, its plants and its sound, from one line (it needs a `terrain`): `savanna` (golden grass, greener where water gathers, patches of red earth;
acacias with their flat umbrellas of crowns and a few bushes), `jungle` (deep greens, mossy rock; kapok giants 25-30 m tall on buttress roots with broad domes of crowns
over palms, broadleaf trees and ferns), `desert` (sand, bands of red rock on the slopes, scattered stones; saguaro cacti - ribbed columns with arms bending up - among
scrub), `tundra` (moss and lichen, snow on the heights and drifted in the hollows; low scrub and a few stunted conifers) or `arctic` (snow, blue ice where it is steep,
rock where it is steeper; nothing grows). It plants a forest of its mix (`plants N`, or as many as the land's size suits) unless the film plants its own, and is heard as
it is unless the film has an `ambience`. The species and mixes are a `forest`'s too: `forest acacia 200`, `forest jungle 3000 in ...`, `forest desert 400`.

**Plants changing over time: `season A to B`, `grow`, `flowers`.** (film_forest.h, film_flowers.h)

```
forest   broadleaf 400  season summer to autumn from 2 to 10   # each tree turning in its own time between 2 s and 10 s
forest   mixed 2000  season autumn to winter from 0 to 20      # the leaves turning and falling, the land's bare by the end (leaves fall round the camera)
forest   birch 300  grow 0 30                                  # a time-lapse: each tree from a sapling to its full size
flowers  poppies 3000  in -6 -6 6 4  bloom 1 7  close 20 26   # opening one by one in the morning, closing at dusk
flowers  sunflowers 40  in 3 -10 8 -6                         # their heads follow the sun across the sky
```

A forest can go from one season to another over the film: each tree turns over its own 40% of the change (from somewhere in its first 60%), through eight stages
between the two seasons - its leaves changing colour (a broadleaf tree to its own autumn colour), dropping one by one into winter (each stage keeps a share of the summer
crown's leaves, the same leaves going first at every stage; deciduous trees only - conifers and palms keep theirs), coming out again in spring, blossom and a conifer's
snow coming and going the same way. A change that drops leaves brings `weather leaves` round the camera (falling to the land under it) unless the film has its own.
`grow T0 T1` grows each tree from a sapling to its size over its own half of that time. The stages are objects like the seasons' own (their meshes PLY files in the film's
cache), so a turning forest of thousands costs a few more objects, not more trees.

`flowers KIND COUNT` is a meadow over `in X0 Z0 X1 Z1` (10 m round the middle when not given), on the land and out of the water: poppies (a nodding bud that lifts its
head, then four red petals opening from a cup to a bowl round a dark centre), daisies (white rays folded over a yellow disc, opening flat), lupins (a purple spike opening
from the bottom up over palmate leaves) or sunflowers (a tall stem and broad leaves, a heavy head whose rays unfold, turned to the sun from the `sky` line - east to west
over the day, back to the east at night). `bloom T0 T1` opens them, each over its own part of that time; `close T0 T1` closes them again; `scale S`, `seed N`. A kind is
made in three variants at eight stages of opening, each an object written once a frame when it is used and placed once per flower, leaning in the wind.

**Animals: `flock`, `herd`.** (film_wildlife.h)

```
flock  starlings 2000  at 0 45 -110  radius 50       # a murmuration (on a terrain, 45 m above the ground there)
flock  geese 9  from 60 30 -40  to -200 40 -120      # a skein, in a V
flock  fish 80  at 0 -3 -10  radius 6                # a school
flock  butterflies 12  in -3 -1 3 -5                 # over a patch of meadow (`height H` lifts them)
flock  eagles 3  at 0 120 -300  radius 60           # circling up a thermal
herd   deer 10  from -15 -22  to 30 -26  speed 1.2   # crossing the land (deer, horses or wolves)
herd   horses 30  from -200 0  to 300 -40  flee 12  # walking, grazing; at 12 s they bolt and gallop away
```

Starlings and fish flock: each keeps apart from those nearest it, turns as its seven nearest neighbours turn, closes on them, and is drawn to a centre that wanders round where
the flock was put (within its `radius`), so the flock swirls, stretches and folds; they fly at about their own speed (starlings 11 m/s, fish 1.4; `speed S`), bank into their
turns and beat their wings (or tails) at their own rates. The flight is worked out once when the film is loaded, 24 steps a second over the film (neighbours found through a
grid, so a flock of thousands is quick), and each frame takes the step at its time. Geese fly in a V behind a leader on its way from one point to another, each half a wingbeat
behind the one ahead, and are heard (an `ambience geese` of honking unless the film file says otherwise); butterflies wander their patch near the ground, flitting up and down
(orange, yellow and blue, twice life size so they read); eagles circle the thermal at `at`, each on a circle of its own within `radius` (60 m), banked into it, rising
slowly, gliding with a few wingbeats now and then. A herd walks from one point to the other in a loose bunch (a pack of wolves closer), on the ground and pitched to its slope,
each animal stopping for a few seconds now and then to graze (wolves: to stand and look round), and grazing when it arrives; from `flee T` on they bolt one after another,
breaking into a trot and a gallop over a second and a half (deer 10 m/s, horses 13, wolves 11) and running on past where they were going. `during T0 T1` limits when a flock is
there; `start T` when a herd sets off; `speed S` its walking pace; `seed N` changes them.

Starlings, fish and butterflies are drawn from eight poses of their wingbeat or tail beat. Deer, horses, wolves, eagles and geese are rigged glTF models
(`film_assets/library/animals/*.glb`, made by `scripts/make_rigged_animals.py`, posed by gltf_skin.h: film_animals.h): a skeleton of a spine, a neck, a head, a tail and four
legs of three bones (a bird's wings of three), a body lofted round it and skinned smoothly across its joints, and clips - the four-beat walk, the trot of diagonal pairs, the
rotary gallop with its spine flexing, grazing, standing and looking round; a bird's wingbeat (folding at the wrist on the upstroke) and its glide. A gait is chosen by the
animal's speed and played by the ground it covers - one cycle of its clip for each stride the clip makes - so the hooves do not slide. Either way a kind's poses are pbrt
objects written once a frame (a rigged kind's only those the frame uses: 16 to a gait's cycle) and placed once per animal: 2000 starlings are 2000 placements.

**The sky's spectacles: `aurora`, `lightning`, `meteors`, `rainbow`.** (film_skyfx.h; the sky itself is film_sky.h's)

```
sky       22:00 23:00  south 0  latitude 64  day 355   # a winter night in the north: stars, the Milky Way
aurora    green  brightness 1.2  during 4 30            # green, red or violet; `facing D` (the film's north when it has a sky), `distance M`
meteors   40  during 0 20
lightning 6  at 0 -2500  radius 1200  during 1 11       # `base H` (the cloud base it falls from), `seed N`
rainbow   0.8  during 6 16                              # opposite the sun; it needs a `sky` line
```

The `sky` line's sky is film_sky.h's picture now: the clear sky by day, round sunset a glow round the sun, a warm afterglow along the horizon under it and the pink belt
opposite, then twilight, and at night the Milky Way and the stars, turning about the celestial pole 15 degrees an hour (as far as it is night). A `rainbow` is painted into it:
the primary bow 42 degrees from the point opposite the sun (red outside), the fainter secondary at 51 (red inside), brighter inside the primary and darker between them, eased
in and out over two seconds - seen against the sky (not in front of the land).

An aurora is three curtains of light some kilometres off towards the pole: ribbons whose folds drift and whose rays run along them, a bright lower edge (green, red or violet)
fading up into a red-violet crown, each ray as tall as it is strong - a lamp is drawn as pure light, so a curtain ends where its glow does instead of fading to black against
the stars - lighting the land and the snow below a little. A lightning strike comes at a time of its own (spread over its `during`, none within 1.5 s of another): a jagged,
branching bolt from the cloud base to the ground (midpoint displacement, a few branches), flickering in three or four strokes over a third of a second, with a flash at its top
that lights the clouds from inside and the land; its thunder comes as long after as sound takes to come from it to the camera at that moment (a crack when it is near, then
rolls of rumble, quieter the further off). Meteors streak across a stretch of the upper sky in under a second, a bright head and a fading tail. Night scenes are dark: use
the film's exposure (`--exposure`, or the auto-exposure of feature 7 below).

**Aerial and time-lapse cameras, and an exposure that follows the light: `fly`, `timelapse`, `exposure`.** (film_camera.h)

```
timelapse 60                                   # nature 60 times faster: the clouds, the water, the wind in the trees
exposure  auto                                 # 1 by day, opening up through sunset to about 30 times at night (`exposure auto 1.5`: a key; `exposure 4`: fixed)
sky       19:30 22:30  south 180  latitude 46  # three hours of sunset into night over the film
shot 0 Over the valley
  fly  -500 400  -100 100  200 -200  500 -700  height 70     # (instead of `key` lines) waypoints, pbrt's x z
```

A `fly` is a drone's flight: its waypoints smoothed into a path; the drone keeps `height` metres above the land - above the highest ground from 40 m behind it to 80 m ahead
(so it rises before a ridge instead of skimming it), smoothed again over 50 m each way so it does not bob over every hump, and never lower than half its height - and looks 400 m
ahead along its way and gently down (the horizon in the picture), or at a point (`look X Y Z`, its height above the land there; `fov F`). It becomes the shot's camera keys,
spaced along the path, so the drone goes at an even pace, easing in at the start and out at the end; a `fly` shot has no `key` lines.

`timelapse F` runs nature F times faster than the film - the clouds drift, the waves roll, rivers and waterfalls run and the trees sway at F times their pace - while the
camera, the characters and the animals keep theirs; the sun and the stars go as the `sky` line's hours say (three hours over twenty seconds is a time-lapse already). `exposure
auto` follows the sun: 1 by day (8 degrees up and higher), opening up smoothly to about 30 times (2^4.9) when it is 12 degrees below the horizon, so a film that runs from
sunset into night stays readable; it is written into each frame's Film line as pbrt's `"float iso"` (100 times the exposure, or the set's own iso multiplied by it).

**Weather that changes the land: `wet`, `snowcover`, `gusts`, `mist`.** (film_terrain.h, film_dressing.h, nature_lib.h)

```
weather   rain 1.2 from 2 to 12
wet       0.9  from 3 to 8            # an amount (0 to 1) reached from T0 to T1 (without from/to: from the start)
snowcover 0.8  from 0 to 30           # snow settling on the land as the film goes on
gusts     1.5  wind 90  speed 7       # fronts of wind sweeping across the grass and the trees
mist      0.003  radius 400           # a mist round the camera: scattering per metre, out to R metres (density x radius about 1 is a good haze)
```

On a terrain, wet ground is darker (its picture scaled by 1 - 0.45 x the wetness) under a glossy coat (rougher when damp, near a mirror when soaked), and once it is wet
through (above 0.35) still water lies in the flat hollows where water gathers, each at its hollow's lowest point (puddles: a mesh made once from the land). Snow is mixed into
the land's picture where the ground is level enough to hold it (a mask from its slope, a little patchy), times the amount - so it settles as the amount grows - and on the far
ranges the same way; steep rock and cliffs stay bare. (Each is a texture graph of the land's pictures, a constant, scale and mix, which every renderer draws per pixel; amounts
are kept to steps of 0.025 so frames a moment apart are the same text.) A winter forest (`forest ... season winter`) matches it. Gusts are fronts that sweep across at `speed`
along `wind`, bending grass, bushes and trees as they pass (a sharp front, easing behind it, the fronts' lines broken by noise). The mist is the camera's own medium ended
by a sphere round the camera (nature_lib.h's Mist: the sun and sky outside light it, so a low sun draws shafts), rendered with the wavefront renderer. Fog banks lying on the
land as volumes in spheres were tried and left out (a medium in a sphere with the land inside it is drawn wrongly by the GPU renderers); valley fog is `fog`, below.

**The physical sky and the air: `sky ... physical`, `air`.** (src/shared/atmosphere.h)

```
sky   5:10 6:10  physical  altitude 1200   # the sky and the sun's light from the atmosphere itself, as an eye 1200 m above the sea sees them
air   strength 2  haze 1.5                 # the air between the camera and the far land: the distant ranges fade into blue
```

`physical` makes the sky's picture and the sun's light from a model of the Earth's atmosphere instead of the Hosek-Wilkie sky and its painted glow, afterglow and
belt: the air's molecules scattering the sunlight (Rayleigh: three times stronger in the blue than the red), its haze scattering it forward round the sun (Mie; the sky
line's `haze`, 3 a clear day), ozone absorbing it (which keeps the twilight sky blue), over a round Earth seen from `altitude` metres up. Each direction is integrated
through the air (single scattering, through a table of the transmittance to the top of the air, which is 0 where the Earth is in the way: its shadow) with Hillaire's
approximation of all the further scattering. What the eye sees round sunrise and sunset falls out of it: the sun reddening and dimming as it nears the horizon and its
light crosses more air (the sun's distant light is that light too), the glow round it, the pink belt of Venus opposite it above the blue-grey of the Earth's shadow,
and the sky still lit high up after the sun has set, fading into night. From a mountain the sky is darker and the sun whiter. A picture takes a second or two (on every
processor), made once for each quarter degree of the sun.

`air` is aerial perspective: the camera's own medium, the atmosphere's scattering at the sky's `altitude` (Rayleigh's blue and the haze's grey; its haze the sky's
unless given), thinning with height over the haze's 1.2 km scale height - the air near the ground, between the camera and the land; the air above it is in the sky's
picture already - and ended at a sphere round the camera (`radius`, 4.5 times the terrain's size, or 8 km). The real air (`strength 1`) gives a land a few kilometres
across only a little haze on its far ranges; `strength` thickens it for a smaller land. It is the `"heightfog"` medium (see `fog`) with an extinction that differs per
colour channel and a `"float radius"`: a flight is drawn per channel in units of the optical depth per unit of extinction, where the layer is homogeneous, and turned back
into a distance, on the CPU and the GPU wavefront renderer. (In an air as strongly coloured and thick as a hundred times the real one the GPU's spectral colours and
the CPU's RGB ones part by several percent, as they do for any such medium; in the real air they agree.) It does not go with `fog`, `mist` or `underwater`.

**Surf on a coast: `surf`.** (film_surf.h)

```
terrain  coast  size 600  seed 2      # a sea floor shelving up to a beach, bays and headlands
water    sea  level 0  waves 0.6      # the open sea's own waves
surf     1.5  period 10  toward 20    # a swell 1.5 m high out at sea, a crest every 10 s, running towards facing 20 (the sea's wind when not given)
```

The swell comes in from the open sea and breaks on the land's shore. When the film is loaded the time it takes to reach each point of the terrain's grid is worked out - a
shortest-time sweep from the deep water (and the open sea at the square's edge) at the speed sqrt(g d) the depth d allows - and a crest is wherever that time is a whole
number of periods ago, so the crests slow in the shallows, bend to follow the depth lines and turn to face the beach, as real swell does (refraction). Each grows as it shoals
(as d^(-1/4), Green's law) and sharpens to a narrow crest between flat troughs; where it reaches 0.78 of the depth it breaks, and from there in it is a bore 0.78 of the depth
high, white at its front and in the turbulent water behind it, keeping the water's mean level. The open sea's own waves give way to it as the water shoals; streaks of foam
are left on the surf zone and drift along the shore; spray blows off the breaking crests near the camera; and water less than 2.5 m deep is clear (glass, eta 1.33) over sand
netted with moving caustics. It needs a `water sea` and a `terrain` (`coast` is made for it, but any land with a shore will do). The surf is part of the sea's mesh, so it
is drawn by every renderer.

**Nature heard from where it is.** (film_soundscape.h)

A nature film's sounds are placed where they come from and heard from the camera as it moves - later the further off they are (sound goes 343 m a second: a crash
across a bay comes after the wave is seen to break), quieter with distance (as 1/d beyond a few metres, a little more over kilometres of air), panned to the side they are
on, with more of the reverb the further off. Nothing in the film file asks for it:

- a waterfall roars from the pool at its foot;
- a fire roars low and crackles; a wildfire roars over its land and crackles all across it; a lava flow hisses at its front and bubbles pop in it; a volcano rumbles;
- the surf crashes where each wave breaks near the camera (each point of the shore when the swell reaches it there, louder in its sets), its wash fizzing after;
- a herd's hooves strike at each footfall of its gait - the four-beat walk, the trot's pairs, the gallop's rolling thunder - deer lighter than horses, wolves' pads soft; a
  wolf standing howls now and then; eagles cry from the sky;
- birds sing from the trees near the camera, by day.

`mute birds` takes every bird out of the soundtrack (the trees' and an ambience's songs, the eagles' cries, the geese, the night's owl), though they are still seen.

A continuous sound is laid as overlapping grains a fraction of a second apart, each placed from where the camera is when it is heard; an event is placed once. They are
mixed under the film's ambiences (which stay round the camera as before), its weather, footsteps, thunder, lines and music. `ray_tracer --film-sound <key> OUT.wav` writes
a film's soundtrack alone, to listen to.

**Storms: `storm`.** (film_file.h, film_dressing.h, film_particles.h)

```
storm  rain  strength 0.9  from 2 to 30  wind 120  speed 14   # a rainstorm rolling in and passing over
storm  snow  strength 1  from 0 to 60  speed 18               # a blizzard
storm  sand  from 5 to 40  wind 270  speed 20                 # a dust storm
```

A storm builds up from `from` to its `strength` (over a third of its time, two to eight seconds) and dies away by `to`, and brings what it is made of unless the film says
otherwise: the sky darkens and greys under a deck of cloud that closes over it (a dust storm's turns ochre) and drifts across (the sky's picture turned a fifth of a degree
a second); the sun dims behind it and is hidden once it has closed; in a rainstorm grey rain shafts hang under the deck round the horizon (painted into the sky's picture,
so every renderer draws them). Its rain, snow or dust (`weather`, as heavy as the storm is at each moment) is blown by its wind - `speed` metres a second from `wind` (a
facing), gusting - so rain slants and drifts downwind, snow streams (a blizzard) and dust is carried along low over the ground in streaks; none is drawn within 1.5 m of the
lens. Gusts sweep the grass and trees; a rainstorm wets the ground and brings lightning and thunder, a blizzard lays snow; the wind is heard. And the air round the camera
thickens with it - a murk of rain, a white-out of snow, a wall of dust - a `"heightfog"` medium 120 m deep over the land, out to 400 m (so it goes with clouds, but not with
`fog`, `air`, `mist` or `underwater`, any of which it leaves alone). A film without a `sky` line gets one (3 pm) for the storm to darken.

**Fog in the valleys: `fog`.** (src/shared/height_fog.h)

```
fog   0.05  top 35  thickness 5  lift 6 11   # scattering per metre below the height 35 (pbrt's y), thinning over 5 m above it; lifting from 6 s to 11 s
```

The fog is the camera's own medium, but one that lies in a layer: as dense as `DENSITY` below `top`, thinning out above it over `thickness` (its density at height y is
DENSITY / (1 + e^((y - top) / thickness))), so it fills the valleys and the hills and peaks stand out of a white sea, and the sun and sky light it from above through the
thin air. Its `top` is a height in the world (pbrt's y), not above the ground (on a terrain: a quarter of the land's height unless given, `thickness` 6% of it).
`lift T0 T1` burns it off: from T0 to T1 it thins away to nothing and sinks three thicknesses. This is the renderer's own medium type, `"heightfog"` (not pbrt's: a
`MakeNamedMedium "x" "string type" [ "heightfog" ] "rgb sigma_s" [...] "float top" [...] "float thickness" [...]` before the Camera line, with `MediumInterface "" "x"`):
its optical depth along any ray is exact (the profile integrates in closed form), so a shadow ray's transmittance is exact and a free flight is sampled by solving for the
height where its sampled depth is reached - no ray marching. It is drawn on the CPU and by the wavefront renderer (the default GPU renderer hands such a scene to it);
unlike a homogeneous camera fog it goes with clouds and flames (their own media), since it has thinned to nothing at their height. It is grey (a per-channel colour is taken
by its brightness), fills the air round the camera, so it does not go with `mist` or `underwater`, and Metal does not draw it yet.

**Under the sea: `underwater`, `kelp`, `coral`.** (film_underwater.h)

```
terrain    dunes  size 400  height 6  y -20  seed 3  far off   # a sandy seabed 20 m down
water      sea  level 0  waves 0.8
underwater clarity 2  colour teal                         # blue, green or teal; clarity 1 a clear sea (about 25 m), 2 tropical, 0.5 murky
kelp       50  in -25 -45 25 -10  height 10
coral      40  in -10 -20 10 -5
flock      fish 80  at 0 5 -18  radius 5
```

The camera is in the sea: its own medium, which absorbs red fastest and blue slowest and scatters the blue-green glow all round, ended by a sphere 3 km across whose top is
the water's surface, so everything under the surface is water (near enough) and the sun and sky light it down through its depth - a sphere round the camera instead lit the
water through its whole height and showed its edge. (It is drawn with the wavefront renderer.) The surface is seen from below as a thin water surface, which lets the sun
through. Caustics - the net of light the waves focus onto the seabed - are a decal on the seabed round the camera: sand coloured by a picture of the net (two layers of moving
cells, bright along their edges, tileable, a loop of 24 pictures over 2.4 s), tiled every 4 m; a projector above the camera shines a coarse version of the same net down
through the water, so the medium shows shafts of light (the GPU renderers read a projector's picture at 64 x 64 at most: shafts, not lines). Marine snow drifts down round the
camera. Kelp rises in gentle curves with blades along it, swaying with the swell; corals - lumpy brain corals, branching corals and tube sponges - stand in bright colours on the
seabed (on a terrain, its ground; otherwise a sandy floor 15 m under the surface). `underwater` needs the film's `water` line, and fills the air round the camera, so it does not go
with `mist`.

**Fire, lava and smoke: `fire`, `lava`, `volcano`, `wildfire`.** (film_fire.h)

```
fire      at 0 0 -6  size 1.2                          # flames 1.2 m tall (on a terrain, on the ground there); `during T0 T1`, `smoke off`, `seed N`
volcano   at 0 -1500  height 450  radius 900           # a cone raised in the land, erupting (`erupting T0 T1`; `column`, `lightning`, `ashfall`, `flow`: round 9)
lava      from 120 -300  width 6  speed 0.4            # a flow down the land from a vent (`start T`)
wildfire  in -200 -300 200 -100  during 0 20  wind 0   # a fire front sweeping across with the wind
```

Flames are a glowing volume: an `rgbgrid` medium whose voxels give out light (pbrt's `Le`; it glows where it absorbs), in a pass-through sphere, written into every frame -
a teardrop of hot gas, widest low down, broken into tongues by 3D noise that climbs through it, white-yellow where it is hottest, then orange, then deep red at its tips; a point
light among them flickers with them (with how much is burning that frame) and lights what is round them, and sparks fly up (`particles sparks`). Smoke rises above a fire:
a dark `uniformgrid` volume that absorbs more than it scatters, spreading and billowing as it climbs and leaning with any drift. A volcano is a cone raised in the land (part of
the terrain: steeper towards the top, ridged, with a crater); erupting, it throws lava fountains (sparks, hundreds a second), glows in its crater (a light down in it, which
lights the plume's base from below), and a towering plume of ash leans away from it; three lava flows run down its flanks from the crater's rim. Lava finds its way down the land
as water would (film_water.h's way downhill) but lies on the ground: a ribbon whose surface is a crust of dark plates between glowing cracks (an area light with a picture),
creeping downhill, its front advancing. A wildfire is a line of flames (one every 10 m, 7 to 12 m tall) across its rectangle that sweeps from one side to the other over its
time along the wind, smoke plumes of their own sizes rising behind it. Flames and smoke are kept in spheres apart from each other (where two overlap, the GPU samples the second only beyond the first). Night fires
want `exposure auto` (feature 7) or a lower one by day.

**Music that follows the film.** Three things (music_cues.h, film_score.h):

- *Ducking.* A film file's music cues are mixed on their own and laid on the soundtrack brought down under the characters' lines - by up to two thirds while a line is
  heard (its `say` lines' loudness and its recorded voices), in 0.06 s, back up over half a second after it - so the music never fights the words.
- *Hits on the cuts.* `oncuts mallet A2 1.5 gain 0.04` in a cues file plays at the start of every shot but the first.
- *A drafted score: `ray_tracer --film-score <key> [OUT]`.* A cues file to start from, made from what happens in each shot: its mood from what the characters feel in it
  (their `feel` lines; nothing felt is calm) and its pace (running is lively, a short shot quicker) give a key, a tempo and chords - happy: C major I-V-vi-IV at 112 a minute
  with a bell; sad: A minor i-VI-III-VII at 66 with strings; scared, worried or angry: A minor i-iv with a knocking pulse; surprised: a sus chord and a swell; sleepy or
  thinking: a soft pad and a mallet; calm: I-IV-I-V with a pluck - each chord held by a pad with the bass on its root and a melody of its notes on the beat, quiet under
  the lines. Printed, or written to OUT (`film_assets/<key>/cues.txt` is where the film reads it); change it as any cues file.

**A film file from a screenplay: `ray_tracer --film-from-script SCRIPT.fountain OUT.film [--set SET.pbrt]`.** A script written in Fountain (fountain.io, the plain
screenplay format most screenwriting programs read and write) becomes a film file to start from (cartoon/fountain.h). The title page's `Title` names the film (its key is
made from it) and `Cast` says what kind each character is and its size (`Cast: PIP=pip, TESS=turtle 6`; anyone else is a person). Each scene heading (`EXT. A MEADOW - DAY`,
or a line forced with a leading '.') is a shot whose cameras are chosen from what happens (`auto`), with its action kept as comments; each character's cue and dialogue is a
`say` line, the lines one after another (each as long as its syllables, 0.6 s apart, 1.2 s into a new scene); a parenthetical is an expression (`feel`: "(excited)" is happy,
"(nervous)" worried, "(gasps)" surprised ...) and, when the kind has it, a gesture ("(waving)" waves) while the line is said. `FADE IN:` fades the film up, `DISSOLVE TO:`
dissolves into the next scene, `FADE OUT.` fades it out; notes `[[...]]`, the boneyard `/* ... */`, emphasis and centred titles are left out. The characters stand round the
middle of the stage set (`film_assets/stage/set.pbrt`, a plain floor, sky and sun; or the set named) facing it, two facing each other. `film_assets/stage/example.fountain`
is a short example: three characters, four lines.

With three or more, the coverage now keeps its cameras on the side the characters' faces look to (unless the shot says `side`), backs its two-shot away until everyone is in
the frame, and shoots a speaker much smaller than the one they speak to from in front rather than over a tall shoulder.

**Crowds: `crowd NAME KIND COUNT in X0 Z0 X1 Z1 [seed S] [from T] [to T]`.** Extras for the background: `crowd folk person 10 in -6 -9 6 -3 seed 2` brings in ten people
named folk-1 to folk-10. The rectangle is cut into a cell each (so they never meet) and each wanders in its own: a pause of 1.5 to 5 s, a walk that finds its way to another
place in its cell at a pace of its own, a pause ... from `from` until `to` (or the film's end). Each has its own look - a person's sweater, trousers, hair and skin from a
palette, Pip's skin and size, the turtle's shell and size - and idle life, and they turn to whoever speaks. The same seed gives the same crowd every time. A crowd is left out
of the walking-into-each-other check and of an `auto` shot's cast (unless the shot names them); a character's own walks go round the extras standing where it walks. Ten
people and four Pips load in about a second and a half and render in about 5.5 s a frame at 480 x 270.

**Uneven ground.** Characters stand and walk on the set's own floor: up a ramp or a flight of steps onto a platform, over a hill, down again (film_paths.h, `Terrain`).
The set is read as columns: in each cell of a map (5 to 12 cm), the heights each thing spans there. A walker's floor is found from where it stands a cell at a time - the
highest surface within a step of the floor beside it (a step is 0.17 of the character's height: 0.3 m for a person) that is level enough to stand on (25 degrees at most: a
boulder's flank or a roof's pitch is not a floor) and has room above it for the walker - so a rise or a drop of more than a step is a cliff it neither climbs nor walks off.
A walk that finds its way (`to`) goes by this floor, round the cliffs, and is followed a quarter of a metre at a time at the floor's height; a walk with its own points keeps
to the floor under them; a jump lands on the highest floor it can reach. On uneven ground its planted feet stand on the floor under them. Heights within 5 cm of the set's
ground are that ground (a path's slabs), so a level set stays exactly level and its films do not change; a place that cannot be reached is refused ("it cannot get up or down
there").

**Running, jumping, sitting: `run`, `jump`, `sit`.**

```
run   kai 4.0 3.0   2 -6              # as `walk` (points, or `to` a place, finding its way), running
jump  kai 7.0 to 3 -7  height 0.5     # a jump to there, rising half a metre
sit   kai 9.0 height 0.45 until 14    # sits down at 9 s on a seat that high (0: the ground), stands up at 14 s
```

A run's gait is 1.6 times as wide as a walk's, so its stride is (the planted feet still held where they come down, its footsteps louder), and the body leans into it. A
jump gathers itself in the third of a second before it, rises in an arc for the time a fall from its height takes twice (0.7 s for half a metre), stretched in the air with
its legs drawn up, lands where it was sent with a squash that is heard, and its feet are then held from where it landed. Sitting eases down over 0.7 s: a person's hips onto
the seat with the feet forward, Pip lifted onto it (or squatting on the ground), the turtle lying on its belly, the Nomai explorer as its own sit; it stands up the same way.
Walks, runs and jumps go in time order, each from where the last left the character.

**Idle life.** A character nobody tells what to do is not a statue: while someone speaks the others turn their heads and eyes to the speaker (just after the line begins,
back a little after it ends, the look moving smoothly from one speaker to the next) and the speaker looks at the nearest of them; standing, a character breathes, shifts its
weight from foot to foot, lets its head drift a few degrees, and its eyes dart - a quick jump every second or two to another point near what it looks at (between the eyes and
the mouth of whoever it listens to) or near straight ahead (film_kinds.h, `idleAt`). A `look` line wins over the listening; `idle off` in a film file turns all of it off.
Pip, the person and the turtle have it; the Nomai explorer keeps its own (it already breathes and blinks).

`ray_tracer --film-lines <key>` lists the lines with their start, end and syllables; `ray_tracer --film-sound <key> OUT.wav` writes a film's soundtrack alone, to listen to
the lines (and the music and footsteps) without rendering a frame; `--film-check` counts a film file's characters and lines.

**A quick preview of a whole film: `python scripts/anim/film.py preview <key>`.** Building a film's characters, not rendering them, is most of a frame's time (the Nomai
explorer: 8.7 s at its usual detail, 40 s for a face close-up; the renderer takes 3 to 5 s for a 1080p frame), so a real-time renderer would not make a film quick to watch.
`--preview` builds every sculpted shape three times coarser (`sdf::surfaceNets`, `previewCoarsening()`; its meshes are cached apart, and its frames go to
`frames-<key>-preview`, never the film's own); `film.py preview` adds 640 x 360, 4 samples, one frame in two (`--every N`) and the soundtrack, and resumes, so after an edit only
the frames whose scene changed are rendered again. A Nomai frame went from 18 s to 2.6 s; the whole five-minute film, one frame in four, previewed in 13.7 minutes.

**What a change to shared code did to every film: `python scripts/anim/film.py baseline`.** The films share a lot of code (the rig, the gait, the kit, the skies), so a change
made for one film can quietly change the others. `tests/data/film_baseline.txt` records every film shot by shot: the fingerprint of the scene at each shot's middle, built at
preview coarseness (`ray_tracer --film-baseline FILE`, src/shared/cartoon/film_baseline.h; a character's shape still changes when its code does). `film.py baseline` lists
the films and shots that now differ (and exits 1); `film.py baseline --write` records them as they are, after a change made on purpose - the diff of the file shows which
shots it touched. All 18 films, 226 shots: 128 s the first time, 33 s once the meshes are cached. `film.py baseline nomai-signal,tengwang` looks at some films only.

**Follow-through: what lags and settles.** `rig::Spring` (stiffness, damping) and `rig::follow(spring, target, t)` / `rig::lag(...)` give the place of something that trails a
moving point - a robe's hem, an antenna, a tail, a lantern on a pole. A frame is a function of its time alone, so the spring is run at a fixed step (1/240 s) over the
3 s before t, starting at rest on the point: what came before has died away, and frames still render in any order on any worker. Moving steadily, it trails by speed x damping
/ stiffness; when the point stops it swings past and settles. The Nomai explorer's robe hem trails its body in the walking shots (`robeTrail`, `walkPose`'s `trail`), and every
character of a film file gets the same (`CharacterMoment::trail`). The baseline showed exactly the shots it changed: the five walking shots of the Nomai film and the four of
A Walk at Dusk, and no other film.

**Props with physics.** `src/shared/cartoon/prop_physics.h`: `physics::Bounce` (a ball dropped or thrown: it flies, bounces lower each time by its restitution, keeps its grip's
share of its speed along the ground, rolls to a stop turning by distance over radius) and `physics::Topple` (a block tipping over its bottom edge under gravity once pushed past its
balance, until it lies flat), each worked out from its start so any frame's place is exact, and each landing an impact (`impactsOf`: when, where, how hard). In a film file:

```
bounce  pebble  5.0  from -0.5 2.2 -2.0  velocity 0.3 0 -0.4  radius 0.07     # the marked block `pebble`, its centre at `from` (as the scene has it); restitution, grip, rolling
topple  tablet  8.4  pivot -1.04 0 -3.0  axis 0 0 1  height 0.6  depth 0.08  push 40   # tips about its edge through `pivot` (right-hand turn about `axis`); lean
```

Each landing is heard (its strength from how hard it hit, panned by the camera). A Walk at Dusk drops a pebble onto the path ahead of the explorer and tips over a stone tablet as
it passes.

**Feet locked to the ground.** A gait moves the feet in the body's space, so on a curve, a ramp or uneven ground a planted foot slides. `rig::FootLock` keeps it where it came
down: while planted, its place is that spot in the world (at the ground's height there, `groundAt`) carried into the body's space, and the leg's own IK bends to reach it; once
lifted it follows its gait over the ground, easing out of where the lock held it over a quarter of a second, so it never jumps. Any character can have one: it needs only its
frame and steps at a time and its gait's foot place and planted flag (`explorerFootLock` builds the explorer's; `ExplorerPose::lockFeet`). The Nomai film's walks on the moon
(on its uneven ground) and down the floating steps, and every film-file character (`CharacterKind::footAt` and `plantedAt`), have their feet locked: the sliding-feet check now
finds 10⁻¹² m/s where it found up to 0.11 (the spiral) and 0.27 (a corner in a film file), and on a straight flat walk the lock gives exactly the gait's feet. What is left is
one frame where a walk starts from a standing pose (under 0.1 m/s).

**Motion blur of moving meshes on the GPU.** A mesh under pbrt's object motion (`ActiveTransform "StartTime"` / `"EndTime"` around it) was blurred on the CPU only;
the GPU drew it at its start. Now the GPU's triangles are built with two motion keys whenever one of them moves - their StartTime and EndTime places (the loader keeps the
moving ones' EndTime vertices: `pbrt_flatten::Triangle::motion`, `FlatScene::triangleEnds`; `OptixRenderer::setTriangleMotion`) - and each ray's time blends them, on the
default and the wavefront renderer alike (SPPM keeps them still). The hit programs needed nothing new (they take the hit point from the ray; shading uses the StartTime
normals). The default renderer's shadow rays were traced at time 0, so a moving thing cast the shadow of where it started - a sphere too; they now use their sample's
time (`LaunchParams::rayTimes`). `pbrt_scenes/mesh-motion-blur.pbrt` has a still box beside one moving 2 m over the shutter: the CPU, the default GPU renderer and the
wavefront renderer show the same streak. A film file blurs its moving blocks (move, turn, grow, bounce, topple) over `shutter` of a frame, 0.5 by default (a film camera's
180-degree shutter; `shutter 0`: sharp): the block's transform at the frame's time and that much later.

**Recorded sound and voices.** A cues file can lay a recording on the soundtrack: `2.2  sample voice.wav  gain 0.5  by kai` plays a WAV file beside the cues file
(8, 16, 24 or 32-bit PCM or 32-bit float, any number of channels, any rate: `audio::readWav`, resampled to the mix's rate). `by NAME` makes it that character's voice:
`music_cues::voiceAt(key, NAME, t, ...)` is how loud it is at each moment (its loudness each 1/100 s, the loudest of the line 1) - a film's code opens a mouth by it,
and a film-file character gets it as `CharacterMoment::voice` (the masked Nomai explorer's staff lights up as it speaks). The cues file is part of a resumed video's
program stamp, so a voice moved re-renders the frames it changes. A Walk at Dusk's explorer says a short wordless phrase to the first star
(`film_assets/a-walk-at-dusk/voice.wav`, made by formant synthesis - any recording of your own goes in its place).

**A shot's colour grade.** A cameras file grades a shot: `lift` (light added, lifting the shadows), `gamma` (the middle brightened above 1), `gain` (multiplied) and `tint`
(a colour cast that keeps the brightness), each one number or red, green and blue; `[*]` grades the whole film and a shot's own grade goes on top. They are written onto
the frame's `# @rt-look` line (`lens_look.h` applies them with the bloom, vignette, warmth and saturation, on the linear picture before the tone map), so stills,
videos and the GUI all show them, and a resume re-renders the frames whose grade changed. The doctor notes a cut across which the colour cast changes (the two frames'
mean chromaticity more than 0.05 apart: "warmer", "cooler", "greener"), which a grade can then match.

**A film's colour curve: `tonemap`.** The curve that turns the render's light into the picture's colours, in a cameras file (`[*]` for the whole film, a shot's own on
top) or with `--tonemap` (which wins): `aces` (the default), `agx` (Blender's default since 4.0), `agx-punchy` (AgX with more contrast and colour, like Blender's "AgX -
Punchy"), `neutral` (Khronos PBR Neutral), `reinhard`, `none` (`src/shared/tone_map.h`). ACES works on each channel alone, so a very bright saturated light changes hue as
its strongest channel saturates first - a glowing blue crystal turns cyan, a red flame yellow, an orange haze yellow-white. AgX and PBR Neutral work on the colour as a
whole: AgX fades it towards white keeping its hue, PBR Neutral leaves everything below its knee exactly as painted and compresses only the brightest. On a Crystal Heart frame
(30 s, the haze behind the heart): ACES turns the haze's bright side yellow; AgX keeps it rose but is paler and flatter; AgX Punchy keeps the hue with about ACES's contrast;
PBR Neutral is the most saturated. The GUI's Tone mapping box (Render Options) offers them too.

**Star glints, and any look word for a film or a shot: `look`.** A cameras file's `look WORDS` adds words to the frame's `# @rt-look` line (`[*]` for the whole film, a
shot's own after it, so they win), and `--look "WORDS"` does the same for one render. Among them the star glint of Blender's Glare node ("Streaks", "Simple Star"):
`star S` (0-1: how much of the light above the threshold goes into each ray), `star-points N` (4: a cross; 6, 8 ...), `star-angle D` (degrees from the right; 45 with 4
points: an x), `star-length L` (each ray's reach, a share of the picture's width; 0.08) and `star-threshold T` (linear luminance a light must pass; 1). Every light above
the threshold gets the rays - the sparks round a crystal, a gem, a lamp, a magic spark - and nothing below it. Made as Blender makes its streaks (Kawase's method: passes
of four taps, 1, 4, 16, 64 pixels apart), at half the picture's size, on the linear picture before the bloom: `look star 0.35 star-length 0.12 star-threshold 2` puts a
thin cross on each of Crystal Heart's floating sparks.

**A painting: `paint`.** `look paint 8` turns the picture into strokes of paint that follow its edges - an oil or gouache look with every edge kept sharp - by the anisotropic Kuwahara filter (Kyprianidis et al. 2009/2010; Blender's compositor Kuwahara node, "Anisotropic"): at each pixel an ellipse along the local edge is cut into eight sectors and the pixel takes the colour of the evenest ones, so a flat area becomes one stroke and the colour never smears across an edge. `paint SIZE` is the strokes' radius in pixels at a picture 1080 high (4 fine, 8 brushy, 16 bold); `paint-sharpness` (0-1, 0.5: how strongly the evenest sector wins), `paint-stretch` (0-2, 1: how far a stroke runs along an edge; 0 gives round dabs) and `paint-smoothness` (pixels at 1080, 4: how even the strokes' directions are). It runs first, on the linear picture, so the bloom, glints and grade go over the paint; it also wipes away a render's grain, so a painted film can use fewer samples. `src/shared/kuwahara.h`; it costs about a second a 1080p frame at size 8.

**Round shadows on coarse characters: `terminator`.** A film's `terminator 0.1` puts `"float terminator" [ 0.1 ]` on every material of its frames: a smooth-shaded mesh's shadow rays near its terminator start from the round surface its normals describe (Cycles' shadow terminator offset, `src/shared/shadow_terminator.h`), so a low-poly character or prop under a low sun has a smooth shadow edge instead of notches along its facets. 0.1 is Blender's default; 0 (the default here) leaves the frames as they were. CPU and both OptiX renderers (docs/PBRT_SUPPORT.md).

**God rays: `sunbeams`.** `look sunbeams 0.5` streaks the light of the sun or a lamp out across the frame from where it is, as Blender's compositor Sun Beams node does (`src/shared/sun_beams.h`): every pixel gathers the bright light (above `sunbeams-threshold`, linear luminance 1) along the line towards the source, `sunbeams-length` of the way (0.35), nearer light counting more. Where leaves or a window frame stand in front of the light, the gaps send out beams and the leaves the shadows between them - god rays for the cost of a blur, against tracing light through a mist. The source is the middle of the brightest light unless `sunbeams-at X Y` gives it (0..1 across and down, outside the picture for a sun just out of the frame).

**A comic print, a watercolour, an old film: `halftone`, `watercolor`, `vintage`.** Look words made last of all, on the finished picture as it is shown (after the tone map, the toon look and the marks; `src/shared/print_look.h`). `look halftone 7` prints it as a comic book is printed: cyan, magenta, yellow and black dots, each ink on a screen at its own angle and its dots as big as its share, 7 pixels apart at 1080, on warm paper; the darkest black is solid, so ink lines stay lines (`halftone-mix M`, 0..1, lays it over the picture only partly). `look watercolor 1` (0..2) paints it in watercolour, after Bousseau et al.'s "Interactive watercolor rendering" (NPAR 2006): the colours bled softly, pigment pooled darker where a wash ends, its grains and uneven flow, the paper's tooth. `look vintage 1` (0..2) shows it as a worn 1930s print: grain, a flickering light, the frame weaving a pixel or two, dust and short-lived scratches, darker corners and a sepia tone (`vintage-tone T`, 0..1, 0.5); a film's frames each get their own `seed`, so the grain and the dust change from frame to frame and a frame rendered again is the same. A still (`# @rt-look` in a scene file, or `--look "vintage 1"`) has seed 0 unless it gives one. With `--temporal-denoise` the frames are rendered without these three (`--defer-print`) and get them after the denoising, which would otherwise average a vintage film's grain and dust away.

### Space films, round 11 (2026-10-29)

Ten more things for films set in space. Each line is read by film_parse_space.h; the feature's own file is named with it. A film with nothing but these lines can use
`set void` (film_assets/library/void.set: a camera and nothing else) instead of a scene of its own.

**Orbits, burns and thrusters: `orbit`, `burn`.** (film_orbits.h)

```
ship    sat   at 0 0 0  kind shuttle  size 60
orbit   sat   around home  radius 1500  period 30            # `apoapsis A` (an ellipse), `incline`, `node`, `phase` (degrees), `backwards`
orbit   sat   around home  radius 1500  period 30  to 600 at 12   # a transfer: down to a circle of 600 m, its first burn at 12 s
burn    sat   20 22                                          # its engine also fires from 20 s to 22 s
```

A ship on an `orbit` goes round a planet (or a point, `around X Y Z`) by Kepler's laws: a circle at an even pace, an ellipse fastest at its nearest point, its nose
along its way. `to R2 at T` is a Hohmann transfer: a burn at T, half a turn on the ellipse that touches both circles (its time from Kepler's third law), a second
burn, then round the new circle. Going down, both burns slow it, so it first turns round to fire ahead - a flip by its thrusters: a quick push to start turning, a
coast, a quick push to stop - and back again after. The engine's flame shows only while it burns (its sound follows it); small puffs of gas come from its thrusters
whenever it starts or stops turning.

**Space battles: `laser`, `shield`, `explode`.** (film_battle.h)

```
laser   a  at 1  to b  count 3 every 0.5  speed 300  colour red    # bolts from a's guns at b (or `to X Y Z`, or straight ahead)
shield  b  radius 30  colour 0.35 0.7 1                             # stops bolts: a ripple of light where each strikes
explode b  at 6  size 40                                            # a flash, a fireball, a shock ring, tumbling debris; b is gone after it
```

A bolt is a bright rod (a white core in its colour) aimed at where its target will be when it gets there; a strike sparks, or ripples a shield. Each sounds.

**Alien and robot characters.** (film_aliens.h) Three new kinds for `character` and `cast`: `blob` (a jelly that hops, its eyes on stalks), `squid` (six tentacles
trailing, the front two its arms) and `hoverbot` (a hovering robot with a screen for a face and noodle arms). They walk and `gesture` (`wave`, `point`, `cheer` ...) as people do.

**The real night sky: `space ... stars real`, `sky ... stars real`.** (film_starsky.h)

```
space   stars real  constellations  date 1969-07-20     # the 9 096 stars of the Yale Bright Star Catalogue where they are; their figures; the planets that night
sky     ... stars real                                  # a night on the ground: the real stars wheeling round the pole (its latitude, `day`)
```

`stars real` paints the Yale Bright Star Catalogue (to magnitude 6.5, each in its colour) instead of made-up stars, with fainter made-up ones below that, and turns
the Milky Way to its true place. `constellations` draws the figures' lines faintly (d3-celestial's lines). `date YYYY-MM-DD` (1800 to 2050) puts the planets among the
stars where they were that day (JPL's Keplerian elements) and the sun where it was, unless `sun B E` says otherwise. On the ground, the sky is turned for the film's
latitude, day and hour, so Polaris stands over the north and the stars turn about it through the night. (Data: `film_assets/library/stars`; THIRD_PARTY_NOTICES.md.)

**Weather on planets: `planet ... flow F storms N clouds C lightning L`.** (film_planets.h) A gas giant's bands drift east and west at their own paces (`flow`; jets
alternating, fastest at the equator) and it has `storms` more ovals in its bands. A solid world's `clouds` (0 to 1) are a layer above its ground that drifts over it
and shades it; `lightning` flickers in them on the night side.

**Low gravity: `gravity`, `footprints`.** (film_spec.h, film_world.cpp)

```
gravity     1.62          # the Moon's (m/s^2; 9.81 by default): jumps hang longer, walks become a loping bound, thrown and dropped things fall slower
footprints                # everyone leaves prints (or `footprints ann bob`); in low gravity each step kicks up a slow arc of dust
```

**Floating water and clutter: `blob`, `drift`.** (film_zerog.h)

```
blob   drop  at 0 1.4 2  size 0.05  move 0.02 0 0  split 4     # a ball of water ringing as a free drop rings (Rayleigh's modes); `split T`: into two
blob   tear  on ann  grow 1 3                                   # a tear that cannot fall, growing on a character's face
drift  junk  20  at 0 1.3 1  box 2 1.5 2  kind mixed            # crumbs, pens, tools: tumbling, bouncing off the box's walls
```

**Ship and station interiors: `interior`.** (film_interior.h)

```
interior  corridor  at 0 0 -10  facing 0  length 10       # hexagonal panels, ribs, light strips, blinking lights
interior  cockpit   at 0 0 0                              # a great window onto space, glowing consoles, two seats
interior  airlock   at 20 0 0  cycle 2                    # at 2 s: the inner door shuts, amber lights turn, the outer door opens in a puff of mist
interior  ring      at 60 0 0  length 20                  # a ring station's floor curving up ahead and behind
```

`light R G B` tints their lamps; `seed N` changes the details.

**Holograms and screens: `hologram`, `screen`.** (film_screens.h)

```
hologram  map    globe  at -0.45 0 1.2  size 0.7                         # also `ship` (`ship fighter`), `orbits`, `map`
hologram  craft  ship   at 0.45 0 1.2   size 0.7  ship fighter  colour 1 0.6 0.2   # `spin DEG/s` (20), `flicker F` (1)
screen    tv     at 1.9 1.5 1.4  show radar  size 0.8 0.6  facing -90  range 2000   # also bars, static, trace, `picture FILE`
```

A hologram is a picture of light over a projector, cut into thin slices that climb slowly (the scanlines: it is see-through between them), turning, flickering, now
and then slipping sideways for a moment. A globe's land is brighter than its seas; `orbits` shows moons going round; `map` is a hilly land drawn in contour lines.
A screen shows colour bars, static, a picture (beside the film, else in the library), a radar - its beam sweeping, the film's ships in `range` of `centre` as blips
that glow as the beam passes and fade - or a heartbeat trace. What glows in them is no light to aim at (`"bool sampled" false`), so they cost a frame almost nothing; a
hologram's lens is one small light that tints what is near. A screen does not show another camera's live picture: a frame is one render, with no second render inside it.

**An explainer's drawings: `label`, `orbitline`, `scalebar`, `compare`.** (film_explain.h)

```
label      "Probe 1"  on probe  from 1.5 to 8  colour 0.5 0.9 1   # a card over a craft, planet, moon or character (or `at X Y Z`); `size S` (0.04 of the frame)
orbitline  probe  dashed  from 2                                # the path a craft on an orbit, or a moon, will take, drawing itself out
scalebar   2000  at 0 -1600 0  text "2,000 km"                 # a bar of that length across the frame, ticks at its ends, its length under it
compare    Earth Mars Moon Jupiter  at 0 0 -3500  height 1200 from 6   # planets at their true sizes on one line, each named with its size
```

Drawn as a diagram is drawn over a picture: thin glowing lines and dark cards edged in colour, their words in Noto Sans (the program's own font, so they look the
same everywhere), always facing the camera and always the same size in the frame however far off what they name is (worked out for each frame's camera). They pop up
at `from` (a label's line drawing out from what it names, an orbit line drawing itself round) and away at `to`; the times are the story's. `compare` knows the Sun,
the planets, Pluto, Ceres, the Moon, Io, Europa, Ganymede and Titan (true radii), and the film's own planets; it outlines each and staggers names that would overlap.

### Characters, round 12 (2026-10-30)

Ten things that make a film's characters look and move as in a feature film. The lines are read by film_parse_character.h (and `act` and `rigged` by film_parse.cpp).

**Skin that light goes into: `skin-light [NAME] D`.** The skin of the person, Pip, the turtle - and a rigged model's materials whose names have the word skin, body, face, head or hand and no word of clothing (`Wolf3D_Body` yes, `BodyArmor` no) - becomes pbrt's
subsurface material (cartoon_lib.h `skinMaterial`): the colour it showed, with light travelling D times a cartoon skin's depth under it, red furthest and blue least (4.2, 1.7
and 1.0 mm at 1, scaled with the character's size). Ears, fingers and a nose glow red with a light behind them and a face looks soft. 0 (the default) is the painted skin as
before; 1 is the usual; up to 6. It is noisier than a painted skin: the films' adaptive sampling and denoiser take that out. CPU and both OptiX renderers.

**Real eyes: `eyes [NAME] real|painted`.** (eye_model.h) An eye built as an eye is: a white ball with a hole where the iris shows; an iris of radial fibres in three zones (a
warm collarette round the pupil, the iris's colour, a dark limbal ring) set back inside it; a pupil in front of the iris; and round them a clear wet shell, the ball's
surface with the cornea bulging out of it - glass (n 1.376) that bends the iris as the eye turns and reflects the scene's own lights and sky as catchlights, so no painted
spark is needed. The shell and the white cast no shadow (`"bool shadow" false`), so the iris is lit. The pupil opens in the dark (from the film's sky: a high sun 0.22 of the
iris, night 0.58) and with surprise, fear and delight, and narrows with anger. For the person, Pip and the turtle; an eye smaller than about 1.5 cm across (a small turtle's)
is thinner than the renderers' ray offset and loses its iris.

**Faces on rigged models: morph targets.** (gltf_skin.h, face_morphs.h) A rigged model's morph targets (Blender's shape keys) are read with their names (the mesh's
`extras.targetNames`), its own weights at rest, and clips that animate them. A film drives them as it drives a built-in face: blinks on its own, the feelings of `feel` lines
and the mouth of `say` lines. They are found by the names the usual tools give them, in any case and after a prefix ("blendShape1.", "Face."): ARKit's 52 (Ready Player Me,
Character Creator, VTuber models: eyeBlinkLeft, jawOpen, mouthSmileLeft, browInnerUp, mouthFunnel ...), Oculus's visemes (viseme_aa, viseme_PP ...), VRoid's (Fcl_EYE_Close,
Fcl_MTH_A ... Fcl_ALL_Joy) and plain ones (blink, smile, A I U E O). A rigged model with a face now takes `feel` lines.

The library has a rigged character with a face to try it: `rigged mira film_assets/library/mira.glb` (made by `scripts/make_rigged_face.py`: 21 ARKit face shapes, clips
idle, walk, wave and twist).

**Lip sync from the sounds.** (film_lipsync.h) A `say` line's mouth now takes the shape of each sound, not only each vowel. Each word is read into the fifteen visemes of
Oculus's lip sync by English spelling rules (th, sh, ch, ph, wh, ck, ng, qu, a soft c and g, silent letters, a magic e, the vowel pairs, an r after a vowel) and a list of
common words English does not spell as it says ("the", "one", "said", "would" ...). A letter of another script is its syllable's onset and vowel. The sounds share the time
the word's syllables are spoken, and the mouth follows them with coarticulation (Cohen and Massaro): a t, k or n takes its neighbours' shape, the lips must meet for an m, b or
p and touch the teeth for an f or a v, and the shapes lead the sound by 35 ms. The built-in faces get the open, wide and round of the blend, a face with viseme morphs the
visemes themselves, one with A I U E O or ARKit's jaw and lips the nearest of those. A recording's mouth (a cues file's `sample ... by NAME`) is turned into visemes too.

**Elbows that keep their volume: dual-quaternion skinning.** A rigged model is now skinned with dual quaternions (Kavan et al. 2007) instead of linear blending, so a
twisted wrist and a bent elbow keep their volume instead of pinching to a "candy wrapper". `rigged ... skinning linear` brings back the old way (an auto-rigged model,
below, blends linearly unless it says `skinning dq`: its blends are wide, and dual quaternions balloon a wide blend round a shoulder raised over the head).

**Hair of strands: `hair [NAME] STYLE ...`.** (film_hair.h) Thousands of pbrt curves with pbrt's hair material (Marschner's model) on the person or Pip, in place of the
sculpted hair: `short`, `bob`, `long`, `ponytail`, `curly` or `mop` (or `off`), with `length L` (metres), `density D` (a share of the style's 11,000 to 16,000 strands),
`curl C`, `frizz F`, `clump C`, `colour C` (else the character's `hair` colour) and `width W`. Roots grow inside a hairline; a strand leaves the scalp, flows away from the
crown (a parting sweeps a bob's, a long head's and a ponytail's front off the face), falls under gravity - the world's down, so a tilted head's hair still hangs - and lies
over the skull; neighbours gather into clumps at their tips. A painted scalp in the hair's colour is under it. The tips trail where the head was 0.06 s before, more for long
hair, so it swings when the head turns or nods and settles when it stops; the film's gusts blow it. About 10,000 curves cost a few seconds a frame on the GPU.

**Clothes: `cloth skirt|coat|scarf on NAME ...`.** (film_cloth.h) Three more worn cloths beside the cape, simulated as it is: a skirt (a ring of cloth round the waist,
its first and last columns joined; `width` is across the waist, `flare F` how far its hem stands out), coat tails (round the back and sides from the waist, open in front) and
a scarf's end hanging from the front of the neck. Each is held where the body holds it at every step and pushed out of the chest, the hips and both legs (balls from the
hips to each foot), so a skirt swings round a stride.

**Feet on the ground: `footing [NAME] ground|flat`.** A character standing on uneven ground - a slope, a stair, a rock, the film's `terrain` - stands on the floor under it
(its height snaps to it when the film is loaded: a floor near where it was put, else the film's land however far above or below), each foot on the floor under that foot, its hips down to the lower foot so that leg reaches it (the other knee bends; at
most a third of its height), and the person's shoes tilted to the slope under them. A walk's planted feet already found the floor under them; now a standing character does
too. `flat`, the default, keeps the old way.

**Act it yourself on video: `act NAME FILE.face at T`.** (face_track.h, tools/video_to_acting) Film yourself acting (the whole body in the picture, facing the camera); the
helper turns the video into `me.bvh`, the body as a motion capture for a `mocap` line, and `me.face`, the face as ARKit's 52 shapes over time, for an `act` line:

```
powershell -ExecutionPolicy Bypass -File scripts\setup_video_to_acting.ps1        # once: a Python environment with MediaPipe, OpenCV, NumPy and MediaPipe's two models
%LOCALAPPDATA%\RayTracerActing\venv\Scripts\python.exe tools\video_to_acting\video_to_acting.py me.mp4 me --models %LOCALAPPDATA%\RayTracerActing
```

```
rigged  mira  film_assets/library/mira.glb
mocap   mira  me.bvh  at 2              # the body (a rigged character)
act     mira  me.face at 2              # the face: a rigged model's ARKit morphs as they are
act     ann   me.face at 2 amount 0.8   # ... or a built-in character's feelings and mouth (to T1, amount A: how much); refused for one with no face
```

It runs on this computer with Google's MediaPipe (Apache 2.0): its pose landmarker's 33 points in metres become a skeleton named as Mixamo names bones (Hips, Spine, Spine1,
Neck, Head, LeftArm ... RightFoot) in a T-pose of the actor's own bone lengths, each joint's turn found from the points (the hips and the chest from their four corners, the
head from the ears and the nose, the limbs as the turn that takes the rest bone onto the bone), smoothed over five frames. The hips do not travel; walk the character with the
film's `walk` lines. `video_to_acting.py --selftest OUT` writes a made-up capture (a wave and a nod, a blink, an "ah") without a video or MediaPipe.

**A skeleton for a model with none: `rigged KIND FILE autorig`.** (auto_rig.h) An .obj or .ply (an object made from a photo, a sculpt), or a .glb with no skeleton and no
clips, gets a humanoid skeleton fitted inside it when it is loaded: slices find its body's middle, its legs and its arms (out at the shoulders, a T or an A pose, or hanging
at its sides down to the hand's tip). Its skin is weighted to the bones nearest it by its distance in each bone's thicknesses (so the side of the chest stays with the chest,
not the arm hanging beside it), never to the other side's bones and the legs only below the hips. It gets the clips idle (a T pose's arms lowered), walk, wave and nod, and its
bones have Mixamo's names, so `mocap` plays captures on it. It stands facing +z (glTF's front), y up. It is a guess, not a rigger's work: an odd pose, a long robe or a tail
moves roughly. `autorig` on a rigged model fits a new skeleton anyway.

### Real animals, round 13 (2026-10-30)

Ten things for nature films about real animals. The lines are read by film_parse_nature.h; the animals are the rigged models `scripts/make_rigged_animals.py` writes into
`pbrt_scenes/film_assets/library/animals/`.

**More species.** Lions (a mane), zebras, giraffes (ossicones, the long neck), elephants (trunk, tusks, big ears, pillar legs), bears, foxes (a bushy tail with a white tip),
rabbits (they hop), penguins (they waddle) and snakes (an S-curve sliding along), beside deer, horses and wolves: `herd KIND COUNT from X Z to X Z`. Each has its strides,
the speeds where it changes gait and its flight speed (film_animals.h `species`).

**Coat patterns: `herd ... coat PATTERN`.** (film_coats.h) A zebra's stripes and a giraffe's patches grow by reaction-diffusion (Gray-Scott: stripes when it spreads one way
faster than the other, spots otherwise) and a giraffe's network is a Voronoi pattern; tiger, cheetah, leopard, `shade` (darker above, pale below: countershading) and plain.
Each animal of a herd has its own variant of the pattern. The picture is painted once into the film's cache and wrapped round the model by its uvs. Zebras and giraffes have
theirs without being asked.

**Hero fur: `herd ... hero N`.** The first N animals of a herd get fur of real strands (pbrt curves in the hair material) over their coats, coloured from the pattern, for
close-ups (`herofur A` how much); the rest of the herd keeps the cheap ribbons of `fur`.

**Gaits.** Walk, trot and gallop for the four-legged kinds, with each kind's own swing; a bounding gallop for the cats and dogs, hopping for rabbits, a waddle, a slither;
animals lean into their turns.

**Behaviour: `hunt PREDATORS PREY at T [stalk S] [catch]`, `herd ... young N`, `herd ... name N`.** Two named herds: the predators creep up for `stalk` seconds (3), unseen,
then charge - a burst faster than the prey's flight, tiring after 8 s; the prey take fright as the charge comes (or when a stalker is within 8 m) and scatter, swerving.
With `catch` the first animal a predator reaches falls on its side and the pride gathers at it. `young N`: young ones following their mothers at half their size.

**Animals and water.** On a film with `water`, an animal in the water throws spray from its legs (more and higher the faster it goes) and leaves foam rings widening behind
it; out of the water it is wet for six seconds - its coat darker and glossy, drops falling - and shakes itself, flinging a ring of spray.

**Birds that really fly: `flight eagles|geese COUNT from X Y Z to X Y Z at T [speed S]`.** Perched at `from`, wings folded and looking round; they take off one after
another with hard wingbeats, fly an arc (flapping as they climb, gliding as they come down), flare to land - the body up, wings high and beating back - and perch at `to`.

**Life under the sea.** (film_sealife.h)

```
baitball   0 9 -6  count 500  radius 2.2  strike 4  strike 9  hunter sharks   # or dolphins
reeffish   40 in -4 0.5 -2 4 3 4
jellyfish  8 in -5 7 -10 5 12 -2  glow 1.5
seaturtles 2 at 0 7 -4  radius 5
```

A bait ball is hundreds of silver fish milling round its middle; at each `strike` a hunter dashes through it on a line (reaching the middle at that time) and the fish near
its path burst away from it, then drift back. Reef fish circle homes of their own in reef colours (yellow and blue tangs, an orange clownfish with white bands, a grey with
dark stripes). Jellyfish pulse - a quick contraction of the bell, a slow relaxing - rising a little with each pulse; the bell lets the light through and its rim glows without
lighting anything. Sea turtles fly on their front flippers round a circle. On a terrain every height is above the ground there.

**The small world: `insects`, `dew`, and thin-film iridescence.** (film_macro.h)

```
dew        400 in -0.25 -0.25 0.25 0.25
insects    bees 2 in -0.12 0.06 -0.12 0.12 0.12 0.05
insects    dragonflies 1 in -0.2 0.12 -0.2 0.2 0.18 0
insects    beetles 2 in -0.06 0 -0.06 0.06 0 0.06  film 380
shot 0 A
  key 0 0.06 0.35   0 0.05 0   35 0.004 0.35   # close, with a lens 4 mm across focused at 35 cm: a shallow depth of field
```

Sizes are real (a bee 1.3 cm). Bees and dragonflies hover, then dart to the next point. Their wings beat far faster than a frame, so each wing is drawn as the fan it sweeps,
with an alpha picture: see-through where the wing passes fast, densest at the ends of the stroke where it turns; the renderers' stochastic alpha test makes it partly
transparent. Beetles walk on six legs in two tripods. Dew is blades of grass bent over with drops of water (glass, index 1.33) along them and at their tips.

Beetles' shells, a dragonfly's body and the wings are iridescent: the renderer's conductor takes a thin film, `Material "conductor" ... "float filmthickness" [ NM ]
"float filmeta" [ 1.56 ]` (fresnel.h `FrThinFilm`): the light reflected at the film's top and at its foot interferes (Airy's sum over the trips across the film, per
polarisation), so the colour depends on the film's thickness and changes with the angle it is seen at, as a beetle's, a soap film's or an oil slick's does. Each channel
is the mean of three wavelengths across its band. With no film it is the bare conductor exactly. CPU, both OptiX renderers and SPPM; pbrt-v4 has no such parameter (it
draws the bare metal), and Metal does not yet.

**Night and the long lens: `night`, `cameratrap`, `shimmer`, and animal calls.** (film_night.h)

```
night      eyeshine 1  flash 1
cameratrap ir  name CAM 03  clock 02:14  date 2026-10-30  temp -2
shimmer    0.004
```

`eyeshine`: the animals' pupils glow back at the camera in their kind's colour (the tapetum behind the retina: green-gold for cats and dogs, yellow for deer, blue-green for
horses, orange for bears, a rabbit's red), a glow that lights nothing. The models' eyes have an iris and a pupil of their kind's shape: a horizontal bar for the grazers, a
vertical slit for a fox, round for the rest. `flash`: a light on the camera. `cameratrap`: a trail camera's picture - its flash, a darker edge, and the strip along the foot
of the frame (its name, the date, the clock running on from `clock`, the temperature) on a card in front of the lens; `ir` its infrared night look, grey with a green cast.
`shimmer A`: heat shimmer, the picture wavering as hot air rises off the ground (lens_look.h `shimmer`, A a share of the picture's height, strongest at the foot of the
frame); the film writes each frame's time into its look line so the ripples move.

Animals call (film_soundscape.h): a standing herd's callers now and then - a lion's roar (a moaning rise, the roar, the grunts), an elephant's trumpet over a low rumble, a
wolf's howl, a bear's growl, a fox's bark, a zebra's or a penguin's bray, a horse's whinny, a deer's bark - from a small synthesised voice (a buzz through two formants,
roughened for a growl), placed where the animal is; geese honk and eagles cry as a flight goes.

### Under the sea, round 14 (2026-10-30)

Ten things for films under the water (with `water` and `underwater`). Heights of what is placed on these lines are above the seabed - a terrain's ground, or the plain
sandy floor 15 m under the surface when there is none (the camera's keys are as ever: above a terrain's ground, else in the world).

**The surface from below.** (film_undersea.h) Looking up from under the water, the whole sky is squeezed into a circle overhead - Snell's window, 48.6 degrees from straight
up - and beyond it the surface is a mirror of what is under it (total internal reflection). The mirror is laid just under the sea's thin surface wherever the line of sight
meets a wave past the critical angle, each wave at its own slope, so the window's edge ripples and wobbles; it casts no shadows (ray visibility), so the sun still comes down
through it. Nothing to write: every underwater frame has it.

**Half above, half below: `waterline`.** A shot's word (`shot 4 Over and under` then `waterline`): its camera floats at the surface, a dome port half in the water. The air
above and the water below are split where the waves stand on the dome, so the line rocks up and down the picture as they go by; the lens look draws its meniscus - a dark,
wavering edge with a bright one over it - and drops of water running slowly down the dome above it (`lens_look.h` `waterline Y`, `drops D`, `drops-time T`). Any camera
put within 0.6 m of the surface floats on it too.

**Depth and light: `underwater ... lamp L ripples`, `halocline Y`, `exposure auto`.** The water takes red first, then orange and yellow, as the light comes down through it
(it always did); `exposure auto` now opens up as the camera goes down, and the painted caustics fade out on a bed deeper than 10 m and are gone below 35 m. `lamp L` puts a
diver's lamp on the camera (a warm spot a little to the side of the lens): the colours the depth took come back close to it, and specks in the water glint in its beam
(backscatter) - with a lamp the exposure opens up only a little. `ripples`: the sand of the seabed in ripples. `halocline Y [strength S]`: the boundary between two waters
at height Y (in the world), a slowly heaving sheet that bends the light a little and mirrors at a glancing look.

```
underwater clarity 1.5  colour blue  lamp 0.8  ripples
exposure   auto
halocline  -9
```

**The deep: `deepsea KIND N in X0 Y0 Z0 X1 Y1 Z1`, `deepsea sparks`.** (film_deepsea.h) Anglerfish hang nearly still, a gape of needle teeth under the lure on their rod,
glowing blue-white (a small light: it lights its own face); lanternfish swim in loose schools, rows of blue lights along their bellies; comb jellies drift, rainbows running
down their eight comb rows (thin-film conductors, round 13's iridescence - they need light, a lamp, to show); siphonophores trail tens of metres of stem, swimming bells at
the head, glowing faintly blue all along. `deepsea sparks`: the water lights up blue behind the animals of the deep as they swim. The deep's own lights light nothing (they
are never sampled), so a film 300 m down is dark but for them and the lamp.

```
terrain    dunes  size 400  height 1  y -300  far off
deepsea    lanternfish 60  in -4 2 -8 4 4 -3
deepsea    anglerfish 2  in -1 1 -3 1 1.5 -2
deepsea    sparks
```

**The octopus and the cuttlefish: `octopus X Y Z [toward A] [size S] [jet T]... [ink T]... [hide T]`, `cuttlefish N at X Y Z [radius R]`.** An octopus crawls over the
seabed on its arms, each reaching and pulling in its own time, the tips curling. `hide T`: it stops, curls its arms in, its skin turns the colour of the sand and rises in
bumps. `jet T`: it blanches and shoots off backwards, mantle first, arms streaming behind, and settles again. `ink T`: a dark cloud left where it was, spreading, rising a
little and drifting with the current (an absorbing grid medium: on the CPU, a frame with it loses the water round the camera - render it on the GPU). Cuttlefish hover,
the fin round the mantle rippling in waves, dark bands passing over their backs.

**Animals that fly through water: `swimmers mantas|eaglerays|stingrays|sealions|penguins|otters N at X Y Z [radius R] [rise T]`.** Mantas fly slowly on wings 4 m across,
dark above, white below; eagle rays are smaller and quicker, white-spotted, with long tails. Stingrays lie buried in the sand until `rise T`, then lift off in a cloud of
sand and glide away, their rims rippling. Sea lions loop and roll; penguins rocket past on beating wings with trails of bubbles from their feathers; otters float on their
backs at the surface (whatever Y says), turning right over now and then.

**Bubbles: `bubbles stream X Y Z [rate R] [size S] [during T0 T1]`, `bubbles breath X Y Z [every P]`, `bubbles net X Z at T [radius R] [depth D]`, `bubbles ring X Y Z at
T`.** (film_undersea.h) Each bubble rises at the speed its size gives it (a few millimetres at about a quarter of a metre a second, zigzagging; the big ones as flattened caps
that rock, faster), grows as the pressure falls on the way up (Boyle's law), drifts with the current and is gone at the surface. They are air in water - a dielectric of eta
1/1.333, so they mirror silver at their edges - and cast no shadows. A stream comes from a vent or a crack; a breath is a diver's, a burst every P seconds of a few big caps
and a cloud of small bubbles (heard: the regulator's hiss, then the burble); a net is a humpback's spiral curtain, blown over 10 s; a ring rises and widens. Under the water
the sea is heard as a reef - a low hush, the crackle of snapping shrimp and a burble now and then - not as waves (`ambience reef`).

**Life on the seabed: `seabed anemones|seagrass|starfish|urchins|crabs|gardeneels N in X0 Z0 X1 Z1`.** (film_seabed.h) Anemones crowned with a hundred swaying tentacles,
clownfish living in them, darting out and ducking back in; seagrass meadows (N clumps) bending with the current; starfish and urchins on the sand; crabs walking sideways in
short dashes, claws up; garden eels standing out of their holes facing into the current - sinking back in as the camera comes within a few metres.

**One current for everything: `current SPEED toward A [surge P]`.** (film_underwater.h Current) The water's flow, SPEED metres a second towards A degrees: kelp, seagrass and
anemone tentacles lean with it, the marine snow, the jellyfish and the bubbles drift in it, ink is carried by it, and reef fish turn to hold station against it. `surge P`
swings it back and forth every P seconds (as much as SPEED each way) about a slow onward drift - the push and pull near a shore.

```
current  0.25 toward 90  surge 6
seabed   anemones 6  in -3 -6 3 -3
seabed   gardeneels 60  in 4 -8 10 -2
```

**Places to film: `wreck X Z [toward A] [length L] [list D]`, `arch X Z [toward A] [size S]`, `cenote X Z [radius R] [depth D]`, `reefwall X Z [toward A] [length L]
[height H]`.** (film_seabed.h; each made once as a PLY file) A sunken ship, its rusted hull open where the deck rotted away, ribs and portholes, a fallen mast, settled on
its side and grown over with corals. A rock arch with holes in its roof the sun shines down through. A cenote: a cavern in the rock, flooded, open to the sky through a round
hole at the surface - one beam of sunlight down to its floor D below (with a `halocline` half way down), roots hanging from the opening. A reef wall rising sheer from the
floor, crowded with sea fans and sponges: with a deep floor (a terrain far down), a drop-off into the blue.

```
wreck     0 -20  toward 60  length 28  list 18
arch      -25 5  toward 90  size 1.2
cenote    0 40  radius 10  depth 18
reefwall  25 0  toward -90  length 30  height 16
```

Metal (the Mac) does not draw the comb jellies' thin films yet.

### Under the sea, round 15 (2026-10-30)

Ten more things for films under the water. As in round 14, the heights on these lines are above the seabed (a terrain's ground, or the plain floor 15 m under the
surface when there is none).

**Light on everything.** (film_sealight.h; nothing to write) Most of the sun's light now comes down through the water as a projector's slide of the caustic net, aimed
down the sun's way as the surface bends it: the net plays over the sand, the rocks, the fish and a diver alike, and the water shows it as shafts of light. The sun itself
keeps a fifth of its light, so shadows stay. The slide reaches up to 24 m round the camera, less round a camera near the sand (its pixels must be small for a macro lens),
and the net softens on a deeper bed and is gone below 35 m. On the GPU a slide is read whole (before, it was squeezed to 64 x 64): the default and the wavefront renderers
both read a slide bigger than that from the scene's textures (ProjectionLightGPU::texIdx). `caustics traced` keeps the round-9 way.

**The sun and the rain from below.** Looking up, the sun is a cluster of glints dancing on the ripples where the line to it leaves the water, each a little star in the
picture. With rain falling (`weather rain`, or a rainy `storm`) each drop that lands near the camera leaves a widening silver ring under the surface and a few tiny
bubbles; the rain itself is not drawn under the water.

**A scuba diver: `diver NAME from X Y Z to X Y Z [via X Y Z]... [at T] [speed S] [lamp] [ok T]... [suit black|blue|red]`.** (film_diver.h) A diver in a wetsuit, mask
and fins, a tank on their back, swims level along the path from time T at S metres a second (0.4) with a slow flutter kick, hovering before and after. Each breath out is a
burst of bubbles from the regulator, rising from where the diver was; it is heard (the regulator's hiss, the burble). `lamp`: a torch in the right hand lights the way;
`ok T`: the OK sign in front of the mask for two seconds.

**Schools that behave: `school sardines|anchovies|jacks|barracuda N at X Y Z [radius R] [strike T]... [hunter sharks|dolphins|sealions]`.** (film_school.h) Up to 3000 fish
flocking, worked out once when the film is first rendered. The school parts round the camera and any diver that comes through it, and at each `strike` a hunter dashes
through the middle: the fish bolt out sideways from its line, turning together - the turn flashes their silver flanks in a wave - and close again behind it.

**Big animals: `bigfish whaleshark|hammerheads|reefsharks|dolphins|humpbacks N at X Y Z [radius R]`.** Whale sharks (the broad flat head, the wide mouth open, white spots in
rows), hammerheads, reef sharks, dolphins (their tails beating up and down) and humpbacks (the giants' bodies of round 9, long white flippers, slow strokes) swimming round
a circle.

```
diver      Ana from -6 3 -2 to 6 3 -10 via 0 2.5 -6 at 2 lamp ok 30 suit blue
school     sardines 600 at 0 4 -12 radius 4 strike 20 hunter sharks
bigfish    whaleshark 1 at 0 6 -20 radius 14
```

**A coral reef: `reef N in X0 Z0 X1 Z1 [bleached] [fluoresce]`.** (film_reef.h) Staghorn (antlers of branches, pale polyps at their tips), table corals, brain corals grooved
in meanders, sea fans spread across the current and soft corals swaying. The corals that do not move are made once as PLY files. `bleached`: all of it white;
`fluoresce`: the polyps glow green (lighting nothing) - film it at night with a blue lamp, `underwater lamp 1 blue`.

**Small creatures: `critters nudibranchs|seahorses|shrimp|mantisshrimp N in X0 Z0 X1 Z1`.** Nudibranchs (bright, frilled, horns and a plume of gills, creeping), seahorses
(their tails curled round a stalk of grass, a fin a blur on their backs), cleaner shrimp (red with a white stripe, feelers waving) and mantis shrimps (half out of a burrow,
eyes on stalks each turning its own way). They are a few centimetres long: put the camera close, with a narrow field of view and `aperture` for a shallow focus.

**A night dive: `nightdive [glow G] [spawn T0 T1]`.** (with a night `sky`) The plankton lights up blue round anything that moves - a diver, a school's leading fish, the
swimmers, the big animals - fading over a second; from T0 to T1 the corals spawn, bundles rising from them like snow falling upwards; the reef fish sleep, hardly moving.

```
sky        23:00 23:00
underwater lamp 1 blue
reef       50 in -6 -10 6 -3 fluoresce
nightdive  glow 2 spawn 2 18
```

**A kelp forest: `kelpforest N in X0 Z0 X1 Z1`.** Giant kelp: N stalks from holdfasts on the seabed all the way to the surface, a blade a metre or so long every third of a
metre with a gas bladder at its base, the last metres lying along the surface in a canopy that shades the water under it. It sways with the swell and the current.

**Camera work: `divecam [A]`, `follow NAME`.** `divecam` (a shot's word): a diver's camera, rising on each breath in and sinking as it goes out, the view tipping with it and
swaying slowly (A 0 to 4). `follow` follows a diver by name, as it follows a character, and schools, swimmers and big animals by their line: `follow school1`,
`follow swimmers2`, `follow bigfish1` (the first animal of that line, a school's middle). A slow rise to the surface with the camera keys ends half above it when the last key is
at the surface (round 14's waterline).

Metal (the Mac) still reads a projector's slide at 64 x 64.

### Under the sea, round 16 (2026-10-30)

Ten more things for films under the water. As in rounds 14 and 15, the heights on these lines are above the seabed (a terrain's ground, or the plain floor 15 m under
the surface when there is none); x and z are pbrt's, as on the round-14 and 15 lines.

**Cartoon characters under the water.** (nothing to write) A character brought in under the surface (its height 0.3 m or more below it) swims: its walks are swims at its
own height, the body laid level along its way with a breaststroke and a flutter kick, bobbing gently; standing still it treads water upright. Its hair floats up and sways
in the current. While it talks bubbles burble up from its mouth, and its voice is muffled - the high sounds gone, a little more echo.

**A boat overhead: `boat X Z [toward A] [length L] [speed S] [during T0 T1] [anchor T] [seed K]`.** (film_boat.h) A boat on the surface (L 3 to 60 m, 8) going its way at S
metres a second (2): seen from below, the hull - red under the waterline - and its shadow on the sand, the propeller's trail of bubbles behind it. `anchor T`: it eases to
a stop and drops its anchor, which falls on its chain and lands in a puff of silt. Its engine is heard, a low drone from where it is, quieter at anchor.

**A sardine run: `sardinerun X Y Z [radius R] [gannets N] [dolphins N] [from T] [seed K]`.** A bait ball of sardines (R 3) herded by dolphins, sharks striking through it
every few seconds; from T gannets fold their wings and plunge in one after another, each splash leaving a column of bubbles. The dolphins whistle and click, each gannet
cracks into the water.

**Hydrothermal vents: `vents N in X0 Z0 X1 Z1 [seed K]`.** (film_vents.h) Black smokers: chimneys of mineral rock two to six metres tall, crusted yellow at the mouth, each
pouring a black plume that billows up and spreads (stopping short of the surface in a shallow sea); the water over the mouth shimmers in the heat; round the foot, tube
worms with red plumes, pale crabs and swarming shrimp. Film it deep, with a lamp.

**Silt and murk: `murk X Y Z [radius R] [green|grey] [density D]`, `silt X Z at T [size S]`.** A ball of murky water - coastal green or grey with silt - thickest in the
middle, in patches, and fading out at its edge so it has none; `silt`: a puff of silt kicked up from the bed at T that billows out and settles. A diver finning within a
metre of the bed kicks up silt behind them on their own.

**Inside a wreck: `wreckroom X Z [toward A] [length L]`.** (film_seabed.h) A corridor of rusted plates to swim along (L 4 to 60 m, 12): portholes letting in shafts of the
light outside, a hatch overhead, debris and silt on the floor. Take a lamp in: `underwater lamp 1`.

**Behaviour: `cleaningstation X Y Z`, `tornado N at X Y Z [around NAME] [radius R] [height H]`, `playmate X Y Z [with NAME]`, `damselfish N at X Y Z [radius R]`.**
(film_behaviour.h) A cleaning station: a grouper hanging still over a coral head, mouth open, while cleaner wrasse dart over it and in at its mouth, other fish
circling for their turn. A barracuda tornado: N fish (10 to 600) circling in
a column round a point - or round a diver, following them. A playful sea lion that loops and rolls round a diver and blows bubbles at them. Territorial damselfish that
dart at anyone - the camera, a diver - who comes near their patch.

**Close up.** (nothing to write) With the camera within a metre and a half of the sand, the sand where it looks is grains in their own colours, bits of broken shell and
pebbles, fixed in place as the camera moves; plain sand, seen close, is speckled. The caustic slide is now aimed where the sun's beam reaches the camera (before, deep down
it lit the sand metres off and a macro shot could be outside it). Nudibranchs come in five colour schemes, with spots; seahorses are bigger on thinner stalks.

**The sounds of the sea.** (with `ambience reef` and `ambience sea`) Under the water the reef's hush and crackle, above it the waves' wash, switching as the camera crosses
the waterline. Boats' engines, dolphins' whistles and clicks, the humpbacks' song and the gannets' plunges are heard from where they are.

**A real underwater camera: `underwater ... [port flat|dome] [filter red|magenta]`.** Behind a flat port the field of view is a third narrower (the water makes things look
a third nearer) and the picture bends and fringes with colour towards its edges; a dome port (the default) does neither. A red filter (blue water) or a magenta one (green
water) puts back the warm colours the water takes away.

```
underwater clarity 2  colour teal  port flat  filter red
boat       0 -14  toward 90  length 10  speed 2  during 6 40  anchor 13
sardinerun 0 6 -30  gannets 8  dolphins 3  from 22
vents      3 in -6 -46 6 -40
murk       14 3 -20  radius 6  green
tornado    150 at 0 3 -8  around Ana
wreckroom  30 -20  length 12
```

The GPU's wavefront renderer now draws a fog, smoke or ink volume inside another one - the murk, the vents' plumes, round 14's octopus ink, all inside the sea round the
camera: before, it sampled the inner one only beyond the outer one's far side, so the inner showed only as its shadow. Its light's shadow rays take the inner one in place
of the sea too (they had counted both, and a murk cast a dark disc on the sand even when thin). And a camera at the waterline stays a little under the surface at its lens
with the waves (at nine tenths of the wave it was above the water in a trough, and the picture's lower half showed the surface's top).

The lines of rounds 14 to 16 (divers, schools, boats, vents, murk, the seabed's life and places, bubbles, `waterline` shots...) need an `underwater` line: without one the
film is not read, where before they were left out of the picture without a word. `underwater` in turn needs a `water ... level Y` line, and cannot be in the same film as
`mist` or `fog` (both fill the air round the camera). `tornado ... around NAME` and `playmate ... with NAME` name a diver of the film, matched exactly. A `tornado` line is
the barracuda's when its second word is `at` (`tornado N at X Y Z ...`) and the wind's otherwise (`tornado from X Z to X Z ...`, nature round 9). The lines that
place animals, plants or bubbles also take `[seed K]` (the line's usage, said when it is wrong, lists its words). An underwater film is heard as a reef under the water and as the sea's waves above it, unless its `ambience` lines say otherwise. A boat's wake and a swimmer's
trail are bubbles left where they were shed, rising, as the boat goes on. A school is worked out once for a film and kept in its cache folder (each frame renders in a
process of its own, and working a big school out again in every one cost seconds a frame).

### Under the sea, round 17 (2026-10-30)

Ten things for cartoons under the water. As in rounds 14 to 16, the heights on these lines are above the seabed (a terrain's ground, or the plain floor 15 m under the
surface when there is none); x and z are pbrt's. A character's y and the camera's keys are the world's.

**The cartoon look under the water.** (nothing to write: a film with `toon` and `underwater`) The colour script follows the depth: near the surface warm and clear, deeper
bluer and a little more saturated (film_undersea.h toonDepthLook). The caustic net is drawn in three soft bands instead of a ramp (film_sealight.h toonNet), the ink lines
thin with distance and take a little of the water's colour (`line-depth`, `ink-tint`, set unless the `toon` line says otherwise), and the marine snow is left out (each
speck got an ink outline and the water filled with black dots). For the same reason the bubbles get no ink: they are put in link group `fx/bubbles` and the toon line
names it `noink fx/bubbles` (toon_look.h: `noink NAME`, any object - a link group - drawn with no lines on or round it).

**The sea's characters: `character NAME fish|crab|octopus`.** (film_sea_kinds.h) A round cartoon fish (big eyes that look and blink, lips that move as it talks, fins that
wave), a crab that scuttles sideways on six legs and snaps its claws as it talks, and an octopus whose eight arms trail as it swims. They take the gestures of every kind
(`wave`, `point`, `cheer`, `shrug`, `nod`, and the crab's `snap`), `say` and `feel`. A fish and an octopus swim at their own height and stay upright as they go (a person
under the water is laid level for a breaststroke); a crab walks on the seabed under the water and does not swim.

**Characters meeting the water.** (nothing to write) A character crossing the surface makes a splash; after going in it trails bubbles for three seconds, after coming out
it drips for two and a half. Swimming fast leaves a wake of bubbles, and a step on the sand under the water kicks up a little silt.

**Comic sea life.** (film_vignettes.h) `clownfish X Y Z [count N]`: an anemone with one to four clownfish darting round it, diving into its tentacles to hide whenever
anyone - the camera, a diver, a character - comes within a metre and a half. `pufferfish X Y Z [at T]...`: a pufferfish pottering round a small circle that blows itself up
into a spiky ball when someone comes within a metre, or at T, holds it four seconds and goes down again. `flounder X Z [at T]`: a flounder lying flat in the sand, the
sand's colour, its two eyes swivelling; at T it bursts off the bottom in a puff of silt, flaps two metres away and settles. `moray X Y Z [toward A] [at T]...`: a moray eel
in a hole in a rock, breathing, its mouth opening and closing; at each T it lunges out half its length, jaws wide. `cleanershrimp NAME [from T0] [to T1]`: a red and white
cleaner shrimp walking about on character NAME's hand. `dugong X Z [radius R]`: a dugong cruising round a seagrass meadow, every nine seconds nosing down to crop the grass
in a puff of silt. Each takes `[seed K]`.

**New places: `seacave X Z [toward A] [radius R]`, `ruins X Z [toward A] [length L]`, `icecap [thickness T] [holes N] [hole X Z]...`.** (film_seabed.h, film_undersea.h) A sea cave: a dome
of rock (R 3 to 30 m, 6) with its mouth towards A and holes in its roof letting shafts of light down. Ruins: a sunken city's paving, columns (some broken, one fallen), an
archway and a statue on its plinth, weed and sponges growing on them (L 6 to 60 m, 12). Ice: a sheet of sea ice over the whole water (T 0.2 to 6 m thick, lumpy underneath),
translucent, the sun glowing through it blue and dim - brighter through clear ice, darker under the thick snowy floes; in each 30 m square of the world a breathing hole with a chance of N in 4 (2), and one at each `hole X Z` (2.5 m across), where the sun comes down in a beam.

**Treasure and props.** (film_treasure.h) `chest X Z [toward A] [open T]`: an iron-banded chest sunk a little in the sand; at T its lid creaks open with a bounce, a rush of
bubbles escapes and the gold coins heaped inside glow warm. `clam X Z [shut T]... [nopearl]`: a giant clam, its wavy lips gaping and breathing, a pearl inside; at each T
it snaps shut and opens again three seconds later. `bottle X Y Z`: a message in a bottle - at the surface it floats neck up, bobbing, and drifts with the current; below it
lies rocking on the sand. `shells N in X0 Z0 X1 Z1`: scallops, cones, starfish and sand dollars over the sand (1 to 400).

**Things fall as in water: `sink coin|anchor|rock|ball X Y Z at T`.** Something let go at T (Y above the water: dropped in from the air, a splash and a trail of bubbles as
it goes in). In water a thing reaches its terminal speed in a moment (treasure::fallOf): a coin flutters down like a leaf at 0.3 m/s, swinging side to side; an anchor
plunges at 2.6 m/s and lands tipped over in a big puff of silt; a rock sinks at 1.8 m/s; a ball rises and bobs at the surface. The current carries them as they go. Under
the water (any film with `underwater`) the props that bounce or topple and the `rigid` world fall at half the film's gravity (a stone's weight in water), the water slows
whatever moves and turns (rigid_world.h `drag`), a bounce is soft and a roll soon stops, a hard landing on the sand throws up silt, and squash and stretch is halved.

**A storm and the moon from below.** (film_sealight.h) With a `storm` (or rain) overhead, seen from under the water: the light dims and brightens as the storm's clouds race
over, white foam streaks the surface along the wind (dark against the bright water above, each streak fixed in the drifting water), every lightning stroke flashes cold
down through the water, and the water rocks in a slow surge - the kelp, the bubbles, the silt along the bottom stirred every seven seconds (a storm sets the `current`
when the film has none). `moonlight [elevation E] [toward A] [bright B]`: a full moon E degrees up (2 to 90, 35) towards A, its cold silver light a distant light and its
disc in the sky; under the water, at night, the caustic net and the glints on the surface are the moon's (it is found as the sun is). It works above the water too.

**Cleaner frames.** A film under the water has its video's frames denoised with their neighbours by default (`--temporal-denoise 1`; `--no-temporal-denoise` for none;
launcher/film_temporal.h): its open water, smooth and still, boiled from frame to frame as each frame's denoiser left blotches of its own. The wavefront renderer, which
draws the sea round the camera, already gives the denoiser the water's colour where a camera ray scatters in it before reaching a surface (its guides fade with the water).

**The story kit.** `python scripts/anim/film.py new reef|wreck|ice NAME` writes a starter film (scripts/anim/starters: a sunny reef with a fish, a crab, a pufferfish and a
chest; an old wreck with an octopus, a moray, a clam and a coin falling from a boat; under the ice with seals), its key and name filled in, ready to change; `new` alone
lists them. The score drafted for a film under the water (film_score.h, when it has no cues file) is the sea's: slower, its chords with their sevenths, a mallet melody,
everything deep in the reverb, and runs of high notes rising like bubbles. Its music - drafted or a cues file's - is heard muffled while the camera is under the water
(a low-pass at 900 Hz, eased across the waterline: DataFilm::musicCutoffs, audio::Mix::lowPass), as voices already were. A line said under the water is captioned in a
glassy speech bubble with little bubbles rising to it (launcher/film_captions.h bubbled).

### Models in a film file (2026-10-30)

A film file can now build its own props, buildings, vehicles and creatures: a `model NAME` block, read up to a line `end`, then `place NAME at X Y Z [scale S] [turn A]`
or `scatter NAME N in X0 Z0 X1 Z1 [y Y] [scale A B] [seed K]` as for a tree. A model is made of pieces (`piece NAME [colour R G B] [rough R] [plastic|matte|metal|glass|glow G]`;
lines before the first piece go into one called `main`), each meshed once and kept in the film's cache folder by a hash of what makes it. Its coordinates are its own metres,
y up, placed by its origin. `ray_tracer --model-check FILM` reports every piece's triangles and what is wrong with it; `python scripts/anim/film.py model FILM NAME` adds a
turntable sheet of the model from four sides.

**Shapes: `sphere`, `box`, `ellipsoid`, `capsule`, `cylinder`, `torus`, `cone`.** (film_model.h, model_sdf.h) `sphere X Y Z radius R`, `box X Y Z size W H D [round R]`,
`ellipsoid X Y Z size W H D`, `capsule X Y Z to X Y Z radius R`, `cylinder X Y Z radius R height H [round R]`, `torus X Y Z radius R tube r`, `cone X Y Z radius R1 R2 height H`,
each `[turn AX AY AZ]`. A shape is added to the piece (`add`, the default), joined in smoothly (`blend K SHAPE ...`, K metres of fillet: an arm into a body), cut out
(`cut SHAPE ...`, or `cut soft K SHAPE ...`) or intersected (`intersect SHAPE ...`). `round R` rounds every edge of the piece, `shell T` hollows it to a wall T thick,
`detail CELL` sets the meshing cube's size (a fiftieth of the piece by default).

**Hard surface: `hard [smooth DEGREES]`.** A piece is meshed by dual contouring (Ju et al., 2002): each vertex sits where the planes the surface crosses its cell meet, so a
box's corners and a machined edge stay crisp instead of being bevelled by a cell's width. `hard` splits its normals where faces meet at more than 35 degrees (or the given
angle), as Blender's auto smooth does, so a hard edge shades hard and a curve stays smooth.

**Subdivision surfaces: `cage`.** (model_subdiv.h) A coarse cage of polygons smoothed by Catmull-Clark subdivision: `cage box W H D [crease S]`, `cage file NAME.obj` (a
cage modelled elsewhere, under film_assets), or `cage` followed by `v X Y Z` and `f A B C D` lines (vertices numbered from 0); `crease A B S` makes an edge semi-sharp (sharp
for S levels, then smoothing: Pixar's creases), `corner V` pins a vertex, `pull V DX DY DZ` moves one, `extrude FACE D [scale S]` pushes a face out on new sides (an arm
from a body), `levels N` subdivides N times (2).

**Modifiers.** (model_modifiers.h) After a piece's shapes, applied in the order written: `bend A [toward x|z]`, `twist A`, `taper S`, `mirror x|y|z`, `array N DX DY DZ`,
`radial N`, `lattice I J K DX DY DZ` (a corner of the piece's box moved, the piece bending smoothly with it), `solidify T` (an open sheet given a wall), `smooth N`, `move`,
`scale` and `rotate`.

**Curves: `sweep`, `loft`.** (model_curves.h) `sweep circle R|rect W H|star N R1 R2|shape X Y ... path X Y Z X Y Z ... [twist A] [taper S] [loop]`: a profile carried
along a smooth path (centripetal Catmull-Rom) without spinning (rotation-minimizing frames), its ends capped - pipes, ropes, rails, tentacles. `loft [sides N] [square]
[open]` then `ring Y RX RZ [at X Z]` lines: a skin through rings - a hull, a bottle, a vase.

**Painted textures: `paint`.** (model_texture.h) `paint base R G B [wear R G B A] [dirt R G B A] [mottle A] [stripes R G B W [x|y|z]] [spots R G B A] [res N]` bakes a
picture for the piece, its UVs unwrapped automatically (box projection into a 3 x 2 atlas): the base colour, blotchy with `mottle`, worn to the wear colour where the
surface bulges (edges, corners: its curvature) and dirty in its creases.

**Displacement: `displace noise|rock|bumps|scales|wood|ribs AMOUNT [size S] [seed K]`.** (model_detail.h) Every point moved along its normal by a pattern S across, after
the mesh is split finely enough for it to show: in the geometry, so the same on every renderer.

**Kit parts and generators.** (model_kit.h) `kit crate W H D`, `kit barrel R H`, `kit panel W H`, `kit window W H`, `kit door W H`, `kit wheel R W [SPOKES]`,
`kit pipe R X Y Z X Y Z ...`, each `[at X Y Z] [turn AX AY AZ]`, join a piece. `make building [floors N] [width W] [depth D] [storey H] [windows N] [roof gable|flat]`,
`make chair|table|shelf|lamp [size S]`, `make car|rocket|boat [size S]`, all `[colour R G B]`, write a whole model's pieces with their materials.

**Levels of detail and instancing.** A model placed more than once in a frame is written once as a pbrt object and instanced, one copy in memory however many there are;
each placement is drawn at the level its size on the screen asks for - as written within 25 of its sizes from the camera, its fields meshed twice as coarse and its cage
a level less subdivided out to 100, four times as coarse beyond.

### Models in a film file, round 2 (2026-10-31)

**Moving models: `place MODEL at X Y Z ... as ID`.** A placement named with `as ID` is moved, turned, grown and shown or hidden by the film's own `move`, `turn`,
`grow` and `show` lines, as a marked block of the scene is - `move ID ...` in the world's coordinates (pbrt's), and `ID.PIECE` for one piece of it in the model's own
coordinates: `turn car1.door about 0.9 1 0 axis 0 1 0  2 0  3 80` swings a door open, `show robot1.eyes 0 4` hides the eyes after 4 s. A placement that moves is written
piece by piece each frame; the others are still instanced.

**Models as characters: `character NAME MODEL at X Y Z`.** (film_model_kind.h) A model of the film is a kind of character - the film's own, as a rigged model's is - and
walks, gestures, looks, feels and speaks. Its rig comes from its pieces' names: `head` (with `eyes`, `pupils` and `mouth`) turns to what it looks at and nods, the eyes
blink and the pupils follow the look, the mouth opens as it talks; `leg.1` .. `leg.N` swing from their tops as it walks (with no legs it hops along, squashing as it
lands); `arm.l` and `arm.r` swing and act its gestures (wave, point, cheer, shrug, nod, snap); `tail` wags; the rest is its body, which bobs and breathes. `make creature`
names its pieces so. A piece's shape keys - `key NAME` followed by the modifiers that make it (lattice, bend, twist, taper, move, scale, rotate, smooth) - are blended in
by what it feels and says: `smile` (or `happy`), `sad`, `open` (or `talk`), `surprised`, `angry` and `blink`. `character ... body red` colours its piece called body.

**Booleans on any piece: `tool`, `boolean cut|add|intersect|blend K PIECE [soft K]`.** (model_boolean.h) A piece marked `tool` is not drawn; another piece cuts it out of
itself, adds it, keeps only their overlap, or blends it on with a K-metre fillet - whatever either is made of (a subdivided cage, a sweep, a kit part, an imported mesh).
Both become signed distance fields on a grid (inside or out by a ray's crossings, the exact distance near the surface) and the result is meshed with sharp edges.

**Simplifying: `budget N`.** (model_decimate.h) A piece brought to at most N triangles by quadric error simplification (Garland and Heckbert), its outline and shape kept;
a model far from the camera is drawn with a quarter or a sixteenth of its triangles.

**Meshes in and out: `mesh FILE [fit H] [scale S] [at X Y Z] [turn AX AY AZ]`, `ray_tracer --model-export FILM MODEL OUT.obj`.** (model_import.h) A PLY, OBJ, glTF or GLB
file (under film_assets, or a path) joins a piece, to be cut, modified, displaced, painted or simplified like anything else; `fit H` sizes it H metres tall and stands it on
the ground. A glTF's own picture comes with it. `--model-export` writes a model's pieces as an OBJ with its .mtl, to carry on in Blender.

**Parameters, variants and the library.** `model NAME params A 1 B 2` gives a model parameters, `$A` in its lines (or `$A*2`, `$A/2`, `$A+0.1`, `$A-0.1`);
`model NAME2 from NAME A 3` makes another with other values; `model NAME2 from NAME variants N A lo hi` makes N of them with A drawn between lo and hi, and a placement
or scatter of NAME2 takes one of them each. `model NAME2 from LAMP` with no model LAMP in the film reads `library/LAMP.model` (beside the film file, or under film_assets):
a model block in a file of its own, shared by every film.

**Pictures on models: `texture FILE [scale S]`, `decal FILE at X Y Z toward X Y Z size W H`, `displace ... bake`.** (model_texture.h) A picture as a piece's colour - on an
imported mesh's own uvs, or projected from the sides of a box S metres across; a decal (a logo, a label, a face, a poster) stuck on where it is projected, its alpha kept;
and a displacement pattern baked into a normal map instead of moved into the geometry - the detail at a fraction of the triangles.

**Living things: `make tree [kind oak|pine|palm|birch] [height H]`, `make bush`, `make rocks [count N]`, `make coral`, `make mushroom`, `make creature [legs N] [arms N] [tail 1]`.**
(model_grow.h) Branching trees (each branch a tapered sweep, leaf clumps at their tips; tiers of needles; fronds), lumpy rock clusters, branching coral, a spotted
toadstool, and creatures from a body plan, ready to be characters.

**Places: `make street [houses N] [length L]`, `make room [width W] [depth D] [height H] [furnished 0]`.** Two rows of houses of different heights, widths, colours and roofs
facing a road with pavements, road markings and lamp posts; a room with a door and a window cut through its walls, a striped wooden floor and furniture set out by rule
(a table and its chairs, a shelf, a lamp).

**Things that break: `shatter ID at T [pieces N] [force F] [seed K]`.** (model_fracture.h) The placement breaks at T into N chunks (a Voronoi fracture of each piece, its
broken faces flat): they burst out at about F metres a second, tumble, fall, bounce and come to rest, in a puff of dust, and the break is heard.

## Examples

### Example 1: Quick Preview (Low Quality)
Fast render for testing camera paths:

```powershell
# 30 frames, 200x200 resolution, 10 samples/pixel - video assembles automatically
.\ray_tracer.exe --video --frames 30 --fps 30 --camera-path orbit 200 10 20
```

Estimated time: ~1-2 minutes (GPU), ~5-10 minutes (CPU)

### Example 2: High Quality Production
Full quality video for final output:

```powershell
# 120 frames, 800x800 resolution, 500 samples/pixel
.\ray_tracer.exe --gpu --video --frames 120 --fps 30 --camera-path orbit 800 500 50
```

Estimated time: ~10-30 minutes (GPU), several hours (CPU)

### Example 3: Multiple Camera Paths
Create different videos from the same scene (each run clears and re-populates `output/frames/`, then assembles its own `output/<name>_video.mp4`):

```powershell
# Orbit path
.\ray_tracer.exe --video --frames 60 --camera-path orbit 600 100 50

# Spiral path (re-renders frames; --output picks the video's name/location -
# it's derived from the output stem as <stem>_video.mp4, so without --output
# both runs above would overwrite the same output/image_video.mp4)
.\ray_tracer.exe --video --frames 60 --camera-path spiral --output .\output\spiral.ppm 600 100 50
```

## Performance Tips

### GPU Mode (Recommended)
- **Much faster** for video generation (10-100x speedup)
- Use higher sample counts for quality (100-500 spp)
- Requires CUDA-capable GPU + OptiX 9.1

### CPU Mode
- Good for testing and low-frame-count videos
- Use lower sample counts (10-50 spp) for faster renders
- Multi-threaded (uses all CPU cores)

### Quality vs. Speed Trade-offs

| Setting | Preview | Production |
|---------|---------|------------|
| Frames | 30-60 | 120-240 |
| Resolution | 200-400 | 600-1200 |
| Samples/Pixel | 10-50 | 100-1000 |
| Render Time (GPU) | 1-5 min | 10-60 min |

**Adaptive sampling (on by default for a film on a GPU).** A film's frames are rendered with `--adaptive`: a pixel stops taking samples once the
noise it would show in the picture (gamma 2.2) is under `--adaptive-threshold` (default 0.003, under one level of 255 before the denoiser), checked
from an eighth of the samples on (`src/shared/pixel_convergence_rule.h`, the same rule on the CPU and both GPU renderers; the wavefront renderer
traces several samples of each remaining pixel in one pass once few are left). `--no-adaptive` turns it off. Measured at 1920x1080, 500 samples,
50 bounces: a landscape frame (Fire and Ice) 13.6 s -> 4.7 s at 0.005 and 6.7 s at 0.0025, with the denoised picture's error against a 3000-sample
reference up 17% and 4% (and its brightness 0.17% lower at 0.003); a lamp-lit cartoon (Turtle Escape, Ball Parade) and a foggy one (Crystal Heart)
are still too noisy at 500 samples for a pixel to stop (raw noise of 4-6 levels of 255 that the denoiser removes), so they cost what they did. A
loose threshold darkens a little: a pixel lit through rare paths can look settled before they arrive (the Cornell box: 1.8% dark at 0.02). What a
frame took is in the worker's log: `[OptiX] Adaptive sampling: N samples a pixel on average`.

## Troubleshooting

### What happened during a long render
Every video keeps its own log beside it, `<stem>_video.log`: everything the run printed (the console scrolls the early lines away, and is gone when the run ends). The run ends with a summary - "No problems were reported", or how many warnings and failures were printed and each different one with how many times (a `[score]` line, a frame that failed) - on the console and at the end of the log. A frame that failed says where its own run's output is (`frames-<key>/work*/frames.log` or `worker.log`). A cartoon's frames also say how long each one's scene took to make, and the run sums it up ("Time: ... making a frame's scene 1.80 s of that (32%)"): when making the scenes, not rendering them, sets the pace, the run says so.

### "No frame_*.ppm files found"
Check that video rendering completed successfully. Look for `output/frames/` directory (a cartoon's frames are in `output/frames-<film key>/`, e.g. `frames-pip-and-the-moon`, so rendering one film never clears another's).

### "Failed to create frames directory ... being used by another process"
Another render is still using that frames folder. The render's workers (`--cartoon-worker`) end with the program that started them, also when the GUI's Stop kills it, so this means a render is really still running (look for `ray_tracer.exe` in Task Manager) or an older version left workers behind: end them and start again. `python scripts/worker_orphan_check.py` checks that workers end with their launcher.

### Video is too fast/slow
Adjust FPS during rendering:
```powershell
# Render at 60 FPS
.\ray_tracer.exe --video --fps 60 --frames 120
```

### Video file not created
Check the render log for ffmpeg errors (missing from PATH, unsupported codec, etc. - the launcher prints the exact ffmpeg command it ran, which you can also run manually to debug). Also confirm the renderer completed successfully and didn't crash during frame rendering.

### Frames look different between renders
- Video mode uses **animated camera positions**
- Single-frame mode uses **static camera position**
- This is expected behavior

### Out of memory during rendering
Reduce resolution or samples per pixel:
```powershell
# Lower settings
.\ray_tracer.exe --video --frames 60 400 50 30
```

## File Structure

```
output/
├── frames/                  # Cleared and repopulated by each --video run (a cartoon's: frames-<film key>/, kept for --resume)
│   ├── frame_0000.ppm       # Rendered frames, original frame index (.png on the GPU)
│   ├── frame_0001.ppm
│   ├── ...
│   ├── enc_0000.png         # PNG conversions, renumbered contiguously
│   ├── enc_0001.png         # (skips any frame that failed to render; a film's frames keep their numbers, see "A long film is safe to render")
│   └── ...
├── <stem>_video.mp4         # Final video, assembled by ffmpeg (<stem> from --output)
├── <stem>_video.log         # What the run printed, ending with its warnings and failures summed up
└── image.ppm                # Single-frame renders (normal mode)
```

## Next Steps

1. **Experiment with camera paths** to find the best view
2. **Adjust render quality** (resolution, samples/pixel) for your needs
3. **Try different FPS values** (24 for film look, 30 for smooth, 60 for very smooth)
4. **Customize camera paths** by editing `launcher/camera_path.h` (advanced)

## See Also

- [BUILD.md](../BUILD.md) - Build instructions
- [README.md](../README.md) - Project overview
- [launcher/camera_path.h](../launcher/camera_path.h) - Camera path implementation
