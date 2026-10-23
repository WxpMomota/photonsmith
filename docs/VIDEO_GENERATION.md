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
1. Create frames in `output/frames/` directory
2. Automatically assemble them into an MP4 video
3. Output the final video to `output/<name>_video.mp4`
- `frame_0002.ppm`
- ...
- `frame_0060.ppm`

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
ease 1                      # 1 (the default): start and end gently; 0: the same pace from the first frame to the last
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

`ray_tracer --list-video-presets` prints the whole list (camera flythroughs and cartoons together: id, key, kind, length, name). The cartoons are presets like the rest: one per film in
`src/shared/cartoon/cartoons.h`, numbered on from the camera presets (V7 to V20 so far), their name, description, frame count and fps taken from the film, so the table below and the film
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
36 s; `--cartoon-jobs 3` (or the environment variable `RAY_TRACER_CARTOON_JOBS`) is faster still, 32 s, at about 3 GB of video memory for each frame in flight. For a heavy film at 1920 x 1080 two is the most that pays: 48 frames of The Great Turtle Escape at 64
samples on an RTX 5080 (16 GB) take 159 s one at a time, 141 s two at a time (10.2 GB of video memory) and 140 s three at a time (13.9 GB); four need more than the card has and slow to a
crawl as the driver pages video memory out (2026-10-23). The pictures are put in frame order
whatever order they finish in. `--frames` / `--fps` change how the film is sampled and played back, not its story.

**Workers: a draft-quality film renders about four times faster.** A frame run costs about a second and a half before it renders anything (the process starts, OptiX makes its context and builds
its pipeline), which at a draft quality is most of the frame: 2.4 s for a frame whose rendering takes a fraction of a second. On an OptiX GPU the video therefore starts **two worker processes**
(`--cartoon-workers N`, or the environment variable `RAY_TRACER_CARTOON_WORKERS`; `0` is the process per frame described above) that each render every other frame in a row: for each frame
a worker writes the scene to the same file, the renderer sees that it has changed and uploads it, and everything else (the context, the pipeline, the denoiser) is reused. A worker
leaves a marker file when a frame's picture is whole; the video watches the markers, hands the pictures on in frame order, and **replaces a worker** that ends with frames left or that
makes no progress for `--cartoon-timeout` seconds (a frame that takes a worker down twice fails and is repeated from the one before). Measured on "Pip and the Moon": 24 frames at 400 x 225
and 25 samples took 30 s with a process per frame and 8 s with workers (0.34 s a frame); at 1280 x 720 and 64 samples, where rendering is most of the time, 3.3 s a frame became 2.9 s. The
pictures are the same (a worker's frame differs from a separate run's by noise only: 0.03 of 255 against 6 to 13 between different frames). The CPU and Metal keep a process per frame.

**Where a frame's time went, and what is still the limit.** Measured with Nsight Systems on "Pip and the Moon" at 1280 x 720 and 64 samples: the GPU ran kernels for only about 0.9 s of a frame's 2.3 s. Most of the rest was the CPU, on one thread: tone-mapping 920,000 pixels, writing them as 2.7 million numbers of PPM text through iostream, and then the same text read back number by number twice (the frame run makes a PNG, the video makes its numbered PNG and deletes the frame run's). Now the tone map is split over the cores, the text is written and read in one block by hand (`src/shared/ppm_text.h`, byte for byte what iostream wrote and read: the pictures are identical, checked by comparing the files) and a frame run (`--ppm-only`) makes no PNG of its own. 24 frames at 64 samples take 31 s with one worker (55 s before) and 30 s with two (35 s before); a third or fourth worker gains nothing now, because what is left is the GPU itself (`evaluate_materials` is 60% of the kernel time, then the OptiX launches). More memory and more CPU cores do not help from here; the next gain is in the kernels. (`nsys profile` on a one-worker render of a few frames shows it.)

**Using the computer while a video renders: `--background`.** The GUI's Settings tab has a Power box in its Render Settings group: "Full power (fastest)", the default, or "Keep the PC responsive while rendering" (remembered between runs); on the command line it is `--background` (or `RAY_TRACER_BACKGROUND=1`). It lowers the CPU priority of the render and of every worker it starts, lowers their memory priority and their GPU scheduling class, uses half of the CPU cores for a CPU render, renders a cartoon with one worker and one frame at a time, and, the part that gives the graphics card back, pauses after every frame for as long as the frame took (a duty of 50%; `RAY_TRACER_BACKGROUND_DUTY=percent` changes it, 10 to 100). A lower priority alone is not enough on Windows, which shares the card between programs' compute work in turns whatever the priority class: 24 frames of "Pip and the Moon" at 1280 x 720 and 64 samples take 33 s alone, 58 s beside a second render of the same kind at the normal priority, 57 s beside one at the lower priority, and 41 s beside one in `--background` mode (the pauses). The cost is that the background render itself takes about twice as long; the pictures are the same. It applies to videos and to renders, not to Live Preview.

**The first start on a new computer (or after an update of the shaders): the OptiX cache.** OptiX compiles the shaders the first time and keeps the result in its cache (`%LOCALAPPDATA%` under `NVIDIA\OptixCache`), so every later start takes a fraction of a second. Cold, the default renderer's six modules take **18 minutes** on this machine (16 cores; 1074 s measured with the cache off, 124 MB of PTX; they took 1361 s and 160 MB until `shade_material` called its large `shade_material_surface` from one place instead of three, which cut every hit program's inlined copies of it to a third; pictures identical on 22 scenes, the lamp and roughness-image paths included), the wavefront renderer's one module 2.6 s. The default renderer's modules are made only when a default-renderer render needs them (`OptiXRenderer::ensureDefaultRenderer`), so the wavefront renderer, GPU SPPM and Live Preview start in seconds even cold, and a wavefront render starts 0.7 s sooner than before even with a warm cache; the GUI starts `ray_tracer --prepare-gpu` at a low priority when it opens (the status bar says "Preparing the GPU renderer ..." if that takes more than a moment), so the compile happens in the background before the first render; the default renderer prints "Compiling the default renderer's shaders ..." when its compile takes more than five seconds, and `[OptiX] The default renderer's 6 modules were ready in N s` when it is done. Measured and not a lever: OptiX's optimisation level (level 1 took 1374 s against 1361 s). Where the time goes: about 100 CPU-minutes in all, but on 16 cores the first 8 minutes use about 6 (each big module's front end is one task), then a tail of one or two cores for the last 5; perfect parallelism would take about 7 minutes. The levers left are smaller modules still (each geometry module still inlines the whole material system once; `__noinline__` on it was measured twice to make OptiX's compile slower, not faster, in the older single-module build, and has not been tried on this one) and a shading module shared by all geometry types as a continuation callable, which would also change the speed of every render and was not attempted. The cache's size limit is raised to 4 GB (3 GB low water mark) if it is lower, so that old builds' entries are not evicted into a recompile (never lowered; `RAY_TRACER_OPTIX_CACHE_GB=n` sets the limit, 0 leaves OptiX's own; `RAY_TRACER_VERBOSE=1` shows the line when it is raised; `OPTIX_CACHE_MAXSIZE`, when set, is respected as the user's own choice, and `OPTIX_CACHE_MAXSIZE=0` turns the cache off, which is how to measure a cold start).

**Pictures, flags, GPUs and workers (how a video's runs are put together).** A GPU frame (and any GPU render asked for a `.png` file) is written as a PNG by the renderer itself, straight from the picture's bytes, like `.exr` is written (`OutputOptions::write_png`, handed over by the launcher so that the renderer library has no PNG writer of its own); the text PPM that the launcher used to read back and convert (7 MB at 1280 x 720) is no longer made for them, and the PNG is pixel-identical to the one the old route made (checked on a render of B3). A render that is cut afterwards (a crop window) and a file asked for with another name still go through the PPM. The flags a frame's run repeats are decided by one table, `launcher/flag_scopes.h`: Render flags are repeated with their values, the video's own flags (`--video`, `--frames`, `--output` ...) are not, and a flag that is not in the table is not repeated and says so; a test checks that every flag the usage text documents is in the table. `--gpu-device N` (or `RAY_TRACER_GPU_DEVICE`) chooses which NVIDIA GPU is used, as `nvidia-smi` numbers them; a number that is not a device of the computer is said and the first is used. A cartoon video's worker can be replaced between two frames when it holds more than a limit (`--worker-max-mb N`, or `RAY_TRACER_WORKER_MAX_MB`; the default, 0, is never: no limit since 2026-10-23; the GUI's Settings tab sets both, "Graphics Card" (not on a Mac) and "Video Helper Memory", in its Render Settings group, remembered between runs; measured flat at about 440 MB over 120 frames, so this guards against a leak that has not appeared), and on Linux and macOS a worker ends when the launcher that started it does (`ParentWatch` in `launcher/process.h`; not yet tried on those systems). `--stats` prints the host round trips of the wavefront loop (2.1 a bounce), and a test keeps it under 2.5.

**What the GPU time of a Pip frame is made of** (`python scripts/gpu_kernel_bench.py`, Nsight Systems, 1280 x 720, 64 samples, per frame): 0.85 s of kernels; `evaluate_materials` 0.49 s (58%), the OptiX intersection launches 0.23 s (27%), camera rays 0.06 s, shadow rays 0.04 s. Pip's glossy surfaces are `coateddiffuse`, so almost every hit (99.9%) goes to the general `evaluate_materials` kernel and not to the simple or the dielectric one, and it runs pbrt's layered random walk three times per hit (the sample, the MIS pdf and the light sample's `f()`). Measured by skipping one walk: the light sample's `f()` alone is 22% of `evaluate_materials`; all the light samples' walks (the moon's and the sky's `f()` and pdf, the distant light's `f()`) 30%, the continuation's MIS pdf 7% (2026-10-22, 4 frames: 1.97 s, 1.39 s and 1.83 s of `evaluate_materials`). None of these walks can be left out without changing the pictures: each is part of pbrt's estimator, and the one walk that was pure waste, the pdf of a distant or point light's direction (a delta light has no MIS weight), is already removed by the compiler as dead code (skipping it by hand measured 1.970 s against 1.972 s, with byte-identical pictures). Compare runs only when they report the same number of launches: one run of the same day made 1994 instead of 2107 and read 1.85 s, for a reason not found. Tried and not kept: `__launch_bounds__(256, 2)` to fit two blocks in an SM's registers (the kernel uses 138; capping it at 128 made it 4% slower). pbrt-v4 (`src/pbrt/wavefront/`) does what this renderer already does, one specialised kernel per material type and a queue per type, so it offers nothing further for the kernel; Mitsuba 3 and Cycles avoid the walk with an analytic approximation of a coated surface, which would change the pictures and so is not done. Two traps for anyone measuring: the hardware counters (`ncu`) need administrator rights on Windows; and a change to a header the layered BxDF is in (`src/shared/bxdfs_layered.h`) makes OptiX compile the shader code again on the next render, which took 20 minutes here (and the OptiX cache, in `%LOCALAPPDATA%` under `NVIDIA\OptixCache`, forgets older entries once it is over about 1.5 GB, so going back to an earlier build can be a cold start too). A video's workers are restarted by their stall watchdog before that finishes: render one still first.

**The renderer a film is made with.** On an OptiX GPU the default renderer is the recursive one, and the wavefront renderer (`--wavefront`) is about 25% faster on the same scene, but it draws some materials differently. A film can name the wavefront renderer for its frames (`FilmMeta::wavefront`) when its frames were compared with the default renderer's and matched: "Pip and the Moon" does (20 frames spread through the film, each against a 768-sample reference: never more than 0.5 dB worse than the default renderer, usually 2 to 5 dB better at 64 samples, mean brightness within 0.2 of 255). The others keep the default renderer: the wavefront renderer draws the candle's wax and flame clearly darker (18.5 dB), the dandelion's seeds brighter, and chrome and brass (Newton's cradle, the orrery) with visible differences. With the workers above, a 1280 x 720 frame of "Pip and the Moon" at 64 samples takes about 1.7 s (it was 5.2 s at 128 samples with a process per frame): the whole movie takes about an hour. Fewer samples, a shallower ray depth and rendering every other frame all lose quality and are not used: the ray depth does not change the time (these night scenes are mostly lit directly), and the GPU denoiser already uses the albedo and normal guides.

**Movie-like films: fog that ends, the camera's look, plants ("Forest Dawn", 2026-10-22).** The earlier films are built from spheres, boxes and lathed shapes under plain lights. Three things make the newer kind possible. *Light shafts:* a film can stand its camera in mist that ends at a large sphere (`nature::Mist` in `src/shared/cartoon/nature_lib.h`, the pbrt way: the camera medium named before the Camera and one `interface` sphere around the set holding it). The sun and the sky outside the sphere light the mist, so a low sun between trunks draws shafts. A fog that fills all of space cannot do this, because no light from infinitely far gets through it (such a scene renders black). The CPU and the wavefront renderer clip every flight and shadow ray to the sphere, and the default GPU renderer hands such a scene to the wavefront one (see docs/PBRT_SUPPORT.md; `pbrt_scenes/sunbeams-in-mist.pbrt` matches real pbrt-v4 to 1.3%). *The camera's look:* a scene file's `# @rt-look bloom 0.16 vignette 0.32 warmth 0.2 saturation 1.06` line, or the flags `--bloom`, `--vignette`, `--warmth` and `--saturation`, is applied to the linear picture after the denoiser and before the tone map (`src/shared/lens_look.h`): the glow a bright light spreads through a lens, darker corners, a white balance and saturation. A film writes the line into every frame's header, so its stills, its video and the GUI's renders of it all get the same look; a flag wins over the line; an EXR stays raw. *Plants:* `nature_lib.h` makes a rolling ground, grass tufts, trees (a tapering trunk, branches and clusters of translucent leaves), ferns and rocks, and writes each piece once as a pbrt object placed many times (`ObjectInstance`; a placement can lean, which sways a tuft or a cluster in the wind). "Forest Dawn" uses all three: about 140 trees, 1,700 tufts of grass and 3,400 placed objects make a 2.8 MB frame (8.6 MB before the branches were placed objects too), rendered by the wavefront renderer in about 2 s at 640 x 360 and 128 samples. Two lessons from making it: the cameras must stand on the rolling ground (`above(x, y, z)`), not at a fixed height, or a low shot ends up under it; and a real 15 cm leaf 11 m from the lens is 2 pixels wide, so the falling-leaves shot is close to the leaves. Moving meshes are still sharp on the GPU (its motion blur is for spheres and the camera only).

**A long film is safe to render.** A frame that takes longer than `--cartoon-timeout S` seconds (15 minutes on a GPU by default, never on the CPU; `0` = never; the environment variable
`RAY_TRACER_CARTOON_TIMEOUT` also sets it) is stopped and tried once more, so one stuck frame does not hold up the rest. A frame that still fails is repeated from the frame before it, so the
video keeps its length and its sound stays in step (the run says how many). **`--resume` continues an interrupted video**: the frames already rendered are kept (each is a numbered picture in the
`frames` folder, written whole or not at all), and only the missing ones are rendered; it does this only when the film, the size, the samples and the flags are the same as the interrupted run's
(a signature file in the folder says so), and otherwise starts again. The GUI passes it for every cartoon video, so a render that was stopped continues when you start it again with the same settings and output folder. A rendered frame leaves only its picture in the folder, not the renderer's own files (a film has thousands).

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

A frame of a film is also a scene: `ray_tracer --gpu 1280 256 8 cartoon:fireflies@13.5` renders that moment as one picture (`cartoon:<key>` alone is the film's own best still), which is the quickest
way to look at a change to a film. The GUI's scene list has a Cartoons tab (beside Self-Contained and Requires External Files) with one still per film; it is listed from the films themselves (a still's file is written the first time it is rendered), so a new film appears there by itself, and its Film time box renders any other moment of the film as a picture.

Making a film of your own: the toolkit is `src/shared/cartoon/cartoon_lib.h` (the only implementation: keyframes, transforms, meshes, hop squash and stretch, inverse-kinematics arms, shots with camera moves), and `python scripts/anim/film.py` shows a film's stills, a contact sheet or the video without the GUI;
a finished film is a function `std::string sceneAt(double t)` (with `headerAt(t)`, `shotStarts()` and `kSamples` beside it) and a row in the table in `src/shared/cartoon/cartoons.h`; that row alone makes it a video preset
(`video_preset::all()` builds the preset from it), a scene on the GUI's Cartoons tab and a line of `--list-video-presets`.

**What a film says about itself** is its row and nothing else, and nothing is typed twice. The row has the key, the name, a description of the story (one sentence, without the length or the number
of shots: those are worked out), the length and fps, the still's time, and a `meta` record: `id` (the number of its video preset, V<id>: give a new film the next number and never reuse one, so
reordering or removing a film moves no preset), `tags` (from the vocabulary `kTagVocabulary`: what the film shows or how it is lit), `shotNames` (one for each shot of its `shotStarts()`),
`samples` (the samples per pixel its own Sampler line asks for) and `secondsPerFrame` (what a 1280 x 720 frame of it takes on the GPU with the denoiser, written by `python scripts/measure_cartoons.py --write`).
The scene list shows all of it for the film's still (length, shots by name, tags, cost), and searching the list for a tag finds the films; the GUI's Shot box beside the Film time box jumps to a shot.
The film table above is the text `ray_tracer --film-table` prints, and a test (`CartoonMeta`) fails when this document, the tags, the shot names, the numbers or a description that restates a length
are out of step with the table; `tests/data/cartoon_films_snapshot.txt` pins each film's `meta` line and what its scene holds (`RT_UPDATE_SNAPSHOT=1` rewrites it).

**Sound.** A film can have a soundtrack: a function `void soundtrack(audio::Mix &)` named in its row (`Film::soundtrack`) that places notes and noises on a mix of the film's length.
`src/shared/cartoon/cartoon_audio.h` is a small synthesiser with no sample files: a few instruments (a marimba-like mallet, a glockenspiel, a plucked string, a pad, a bass), the noises a
cartoon needs (a splash, a knock on wood, a boing, a slide), a voice that chirps, a reverb and a limiter. Every sound is a pure function of its arguments, so a film sounds the same each time it
is rendered. The film can share its timeline with the picture: in "Pip and the Moon" the footsteps, Pip's chirps and the splashes come from the same lists (`pip_moon_story.h`) that move its
feet, open its mouth and strike the water, so what you hear is on the beat of what you see. A video of the film's own length and frame rate (the preset's defaults) gets the sound added by
ffmpeg, and the .wav is kept beside the video (`<name>_video.wav`); a video of another length is made without sound, since the sound is made for the film's own timing. On a Mac the built-in
encoder cannot add sound, so a film's sound needs ffmpeg there (without it the video is silent and the .wav is beside it).

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
length   12                        # seconds (required)
fps      24                        # default 24
still    6                         # the moment the scene list's picture shows (default: the middle)
tags     emissive,dusk             # words from the scene list's tags (others are ignored)
ease     1                         # 1: motions start and stop gently (the default); 0: even pace

shot 0 The windmill                # a camera shot from 0 s; the next shot is a hard cut
  key  0 3 14   0 2.5 0   38       # where the camera is, what it looks at, [fov [lensRadius focusDistance]], [hold S]
  key  -4 3.5 12  0 2.8 0  38      #   (the same `key` lines as a --camera-keyframes file: the camera goes through them)
shot 7 The lamp
  key  ...

turn  sails about 0 3.4 0.95 axis 0 0 1   0 0   12 -720    # turn NAME about a point, around an axis: pairs of time and degrees
move  ball  0 0 0 0   12 14 0 0                            # translate NAME: groups of time x y z
grow  flower about 0 0 0   0 0.1   4 1                     # scale NAME about a point: pairs of time and factor
light lamp  0 0   4 0   8 1   12 1                         # the power of the lights in NAME times k: pairs of time and factor
show  ghost 3 5   8 9                                      # NAME is in the scene only between these times
```

Between two keys a value changes smoothly (evenly with `ease 0`); before the first it holds the first value and after the last the last. A shot with one `key` is a camera that stays; a film with
no shots keeps the scene's own camera. Meshes and pictures the pbrt file names by a relative path are looked up beside the pbrt file. `ray_tracer --film-check FILE.film` loads a film file and its
scene and says what it found or what is wrong, with the file and line; with no file it lists the films found in the scene folders and the files that were left out (a film with a problem is left out
of the scene list, never half-loaded). A film made from a file has a video preset number of its own, `V` and a number from 1000 up worked out from its key, so it never changes when other films
are added. The sound of a built-in film is C++; a film file has none.

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

## Troubleshooting

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
├── frames/                  # Cleared and repopulated by each --video run
│   ├── frame_0001.ppm       # Rendered frames, original frame index
│   ├── frame_0002.ppm
│   ├── ...
│   ├── enc_0000.png         # PNG conversions, renumbered contiguously
│   ├── enc_0001.png         # (skips any frame that failed to render)
│   └── ...
├── <stem>_video.mp4         # Final video, assembled by ffmpeg (<stem> from --output)
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
