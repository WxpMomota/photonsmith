# Mac verification checklist (2026-10-08)

The Windows session changed shared code whose macOS branches it could only syntax-check (`cl /Zs /DRT_HAVE_METAL launcher/main.cpp`). The Mac session ran these on 2026-10-08 (main at 2970f2a, an M2); results below. Delete this file when the open items are settled.

Build and automated checks

- [x] `cmake -S . -B build_macos -G Ninja -DCMAKE_BUILD_TYPE=Release -DRT_BUILD_METAL=ON && cmake --build build_macos` compiles, including `realtime_api.h`, `gpu_backend.h`, `backend_capabilities.h`, `realtime_renderer_mac.cpp` and `camera.h` with `oidn_runtime.h`.
- [x] `ctest` in `build_macos`: 12/12 (shader tests, the 95-scene parity sweep with its golden snapshot, seed reproducibility), and 13/13 with the new `metal_poc_sppm_gpu_falls_back`. `unit_tests` 3690 and `cpu_integrator_tests` 72 pass, built Release like CI and run from the repo root (with the launcher beside them, so the render tests run, none skipped).
  - A **Debug** (no build type) `cpu_integrator_tests` aborts on its first test: `scene_registry.h:328`, "a pbrt-backed scene was built before accelerator_override::set() ran". CI builds Release, where the assert is compiled out, so it never shows. Those test files need an `accelerator_override::set({"", ""})` before the first scene (the AOV tests already do it). Not fixed here.
- [x] `python3 scripts/gui_selftest.py RayTracer_Package_macOS/RayTracerGUI.app --live-preview`: all six modes pass.

Behaviour that changed on the Mac side

- [x] `ray_tracer --gpu --adaptive ...` prints no "has no effect"; it changes the picture (A1, 300 px, 1024 spp: 14.1 s without, 13.1 s with it, 6.6 s with `--adaptive-threshold 0.05`). The Adaptive sampling box is on for Metal.
- [x] Live Preview with the Metal library: Depth of Field, Exposure, Samples/Frame, Max Bounces, Firefly Clamp, Aperture, Focus Distance, Smooth noisy pixels, Auto exposure and **AI denoise** are shown; SVGF, ReSTIR and the rest are hidden (checked on a screenshot of the Render Options tab).
- [x] A single-image `--gpu` render with no Metal device says "ERROR: Metal is not available!" and exits 200 without a picture (tested by running the launcher under `sandbox-exec` with `(deny iokit-open)`).
- [x] `--cpu --denoise` with the denoiser installed: "Denoised with Open Image Denoise", and against a 512 spp reference the error of an 8 spp picture goes from 34.5 to 7.9 (mean abs diff, 0-255). In the GUI, with the CPU renderer selected the image/video AI denoiser box is enabled and ticking it without the library offers the download (declining unticks it).
- [x] **Found and fixed:** `--sppm --gpu` on a Mac stopped with "ERROR: OptiX is not available! (--sppm --gpu requires OptiX)" instead of rendering on the CPU like `--bdpt`, `--mlt` and the debug integrators. It now warns that `--gpu` is ignored and renders on the CPU (`launcher/main.cpp`, with a ctest).

For review

- [ ] `docs/GPU_SCENE_COMMON.md` **is not in the repository** (not on main, not on any branch or in the history the Mac checkout has), so there was nothing to review. Push it, or say where it is.
- [x] `docs/BACKEND_SUPPORT.md`, Metal column: every dagger entry was run and the table now says what was seen. Confirmed: subsurface `-` at the time (a BSSRDF was added later the same day, see CHANGELOG), dispersion `-` at the time (the shader existed but the pbrt loader never selected it; selected since, see CHANGELOG; B23 has no colour fan), uniform grid / NanoVDB `-` (the `interface` material is unsupported), measured `Y`, camera/sphere motion blur `Y`, mesh motion blur static, cones/paraboloids tessellated, RGB-grid medium `Y`. **Wrong in the old table:** the cloud medium - a pbrt scene's cloud is not rendered on Metal (E5 and `cloud-medium-scene.pbrt` show no cloud, a black disc in the second), the same before the kernel split; the parity sweep cannot see it (see `METAL_PARITY_STATUS.md`). **Fixed afterwards:** the loader now builds the cloud and the picture matches the CPU's (see `METAL_PARITY_STATUS.md`).

- [x] `docs/BACKEND_SUPPORT.md`, Metal column: every dagger entry was run and the table now says what was seen. Confirmed: subsurface `-`, dispersion `-` at the time (the shader existed but the pbrt loader never selected it; selected since, see CHANGELOG; B23 has no colour fan), uniform grid / NanoVDB `-` (the `interface` material is unsupported), measured `Y`, camera/sphere motion blur `Y`, mesh motion blur static, cones/paraboloids tessellated, RGB-grid medium `Y`. **Wrong in the old table:** the cloud medium - a pbrt scene's cloud is not rendered on Metal (E5 and `cloud-medium-scene.pbrt` show no cloud, a black disc in the second), the same before the kernel split; the parity sweep cannot see it (see `METAL_PARITY_STATUS.md`). **Fixed afterwards:** the loader now builds the cloud and the picture matches the CPU's (see `METAL_PARITY_STATUS.md`).

## Live Preview keyboard step and scene size (Windows, 2026-10-09)

New shared code the Mac side has not run: the Live Preview's keyboard step is 2% of the scene's size (it was a flat 20 units), from a new `# @rt-size` line in each scene's header (`scripts/stamp_scene_sizes.py`, `ray_tracer --print-scene-size`). `SceneMetadataSnapshot` (`cpu_renderer/scene_metadata_snapshot.h`) gained a last field, `scene_size`, so **rebuild `scene_metadata` and the GUI together**; the GUI and the library must agree on the struct.

1. Rebuild, then from the app folder run `RT_GUI_SELFTEST=livepreview_keys RT_GUI_SELFTEST_SCENES=A1,L51,L52` (ids are per category and can differ, so use `scene_metadata_id_for_key` names to find them): each line should say one press moves 2.00% of the scene (11.1 for the Cornell box, 0.128 for `nanovdb-furnace`, 0.008 for `nanovdb-thin-slab`) and end with `0 of N scenes flagged`.
2. By hand in Live Preview: press W / Up in a small scene (a Scene Builder room) and in the Cornell box; a press should move a sensible fraction of what you see, and zooming in with the wheel should stop just short of the pivot.
3. `python scripts/stamp_scene_sizes.py --check` (needs a built `ray_tracer`; `--exe` for the Mac binary) should list nothing but the two scenes that cannot be measured.


## Scene Builder features (Windows, 2026-10-11)

Everything below was written and tested on Windows (CPU renders, OptiX, the headless GUI self-test); none of it has run on a Mac. Most of it is Qt and shared code that should behave the same, so the self-test is the quick pass; the by-hand items are the ones where Metal or macOS could differ. Tick what you ran and write what you saw under the item; "same as Windows" is a fine answer.

Automated

- [ ] Rebuild (`cmake --build build_macos`) and run `ctest` and the unit tests as before. New test files: `scene_viewport_tests.cpp`, `scene_lighting_tests.cpp` and `scene_projector_tests.cpp` (the last two include CPU renders through `ray_tracer`; they skip if the launcher is not next to the tests).
- [ ] `RT_GUI_SELFTEST=builder` from the app folder (`scripts/gui_selftest.py ... --live-preview` runs it too). It should end `RESULT: OK`. On a machine with Live Preview it also switches the **Rendered** viewport on, recolours and drags an object and waits for the picture to follow (up to a few minutes on the first run; it says "skipped" if the Live Preview renderer is not there). If a step fails, paste its `FAIL:` line.

By hand: the Rendered viewport (Metal)

- [ ] Scene Builder tab, open the starter scene, press **Rendered** (next to Orthographic and Wireframe). The 3D view should fill with a grainy picture that refines (the corner says "Rendered, N samples"), with the names, lights and camera still drawn on top.
- [ ] Orbit and zoom: the picture follows without starting over. Pick an object and drag it by its arrows: the picture follows, and the sample count restarts but the "Rendering..." state does not come back (a drag is sent as an offset, not a restart). If a drag makes the picture jump to a different scene state or goes black, that is the Metal side of `setObjectOffset`.
- [ ] Change a colour or add a shape: the picture restarts after a short pause and shows it. Turn **Rendered** off: the 3D view draws faces again. Start Live Preview from the toolbar with Rendered on: the viewport should switch itself off.

By hand: lighting, sky and projector

- [ ] Add > **Lighting (replaces the lights)**: try each of the six on the starter scene with Rendered on. Studio should show three directions of light on a dark ground, Overcast soft shadows, Sunny and Golden hour a blue sky and a sun (golden hour low and warm), Moonlit a dark blue night, Light box a white surround. Undo (Ctrl/Cmd+Z) brings the old lights back each time.
- [ ] Select the Sky light, tick off **Sun & sky**, choose an `.hdr` or `.exr` panorama, then change **Rotate sky** between 0 and 180: the bright part of the picture should move to the other side of the scene (check with Rendered on, and with a normal Render). Metal reads the `Rotate` before the `LightSource "infinite"` in the saved file; if the sky does not turn, say which renderer.
- [ ] Add > **Projector (slide light)**: a window of four panes should be projected on the floor and the objects, upright and not mirrored (the panes' bright squares appear on the floor beyond the aim point). Change the **Field of view**, the **Slide picture** (try a picture with text on it, and an `.exr`) and aim it at a wall: the picture should read the right way round. In the 3D view the dotted frustum should end at the same rectangle the light lands on. If the slide is mirrored or upside down on Metal only, report which way.

By hand: things the renderers draw (spot-check against a CPU render of the same scene)

- [ ] Add > Volumes: fog, smoke and a cloud, each with a light shining through them (Rendered on, and Render). A cloud is a lumpy sphere, fog and smoke fill their shape; none should be a cube or a hard-edged black disc.
- [ ] A **Subsurface** material (preset skin or candle wax) on a ball under a light: it should glow softly, not look like plain plastic or black. **Marble** and **Noise** patterns on a matte ball; a **bump** or **normal** picture on a box (Metal applies it to triangle shapes).
- [ ] Camera: set the focal length to 200 mm and the f-number to 1.4, **Pick focus in the 3D view**, click a ball: the 3D view draws the focal plane, and a Render shows the depth of field with that ball sharp.
- [ ] Preview live and Edit in Builder still round-trip a scene that now contains a projector or a Lighting setup.

Report back by editing this section, or by sending the failing `FAIL:` lines.

### Mac results (2026-10-11)

Everything here ran on an M2 Mac with Metal; nothing was clicked by hand, so what the checklist calls "by hand" is covered where a test can say it and left unticked where only eyes can.

Automated

- [x] Rebuilt, `ctest` 23 of 23, unit tests 3,934 pass. `scene_viewport_tests` is in `unit_tests`; `scene_lighting_tests` and `scene_projector_tests` are in `cpu_integrator_tests` (102 pass; their render tests run, the launcher is found when the tests run from `build_macos/<a subfolder>`). The same render tests run against **Metal** (`--gpu` instead of `--cpu`, compiled by hand): all 15 lighting and projector tests pass.
- [x] `RT_GUI_SELFTEST=builder` ends `RESULT: OK` (and `livepreview` too), including the Rendered viewport steps: it produces frames and refines them, the red box is in the picture, recolouring it changes the picture, **dragging the box moves it with 0 restarts**, orbiting keeps the picture coming, and the switch turns it off. One thing to know: the GPU preview step of `builder` takes about **90 seconds on a cold shader cache** (Metal compiles the renderer on first use after a new build), and `gui_selftest.py` used to give a mode 60 seconds, so the first runs after every build reported "hung" and a rerun passed. The waits are 300 s for `builder` and 240 s for `livepreview` now.

By hand (Metal)

- [x] Rendered viewport: the steps above are the self-test's own, on Metal. Not checked: starting Live Preview from the toolbar with Rendered on switches the viewport off.
- [ ] Orbit and zoom by eye, and the six **Lighting** presets: not looked at. The overcast plate (albedo times sky brightness) and the studio rig's irradiance are tests, and they pass on Metal.
- [x] **Rotate sky**: `TurningAnImageSkyHalfWayRoundMovesItsLightToTheOtherSideOfABall` passes on Metal (and the CPU).
- [x] **Projector**: all three render tests pass on Metal: the slide lands on the wall right way up and not mirrored aimed level, down and sideways, and a white slide lights a surface like a point light of the same strength. Not checked: the dotted frustum in the 3D view.
- [x] **Volumes**: the Beer-Lambert absorber and the "white volume in a uniform sky is invisible" tests pass on Metal. The third volume test, a grid medium in a box bigger than its sphere, **failed on Metal: a `uniformgrid` inside an interface sphere drew as nothing at all**. Fixed (the loader hands it to Metal's RGB grid medium; CPU and Metal now agree to 0.3% on that test and on `uniformgrid-medium.pbrt`), see the CHANGELOG. Fog, smoke and cloud by eye: not looked at.
- [x] **Normal map**: the tilt test (`cos 30`) passes on Metal. Subsurface skin and candle wax, marble and noise, and a bump picture by eye: not looked at.
- [ ] Camera focus and depth of field in the 3D view: not run.
- [ ] Preview live and Edit in Builder with a projector or Lighting setup: not run.

## Mixed materials and texture repeats (Windows, 2026-10-11)

Two more Scene Builder features that have only run on Windows: new test files `scene_mix_tests.cpp` and `scene_uv_tests.cpp` (each has a CPU render test; as before, run the same render tests with `--gpu` if you can).

- [ ] `scene_mix_tests`: `AMixOfTwoMatteMaterialsReadsTheBlendOfTheirAlbedos` on Metal. A big plate with a mix of two matte albedos (0.8 and 0.2) under a uniform sky should read (1 - t) * 0.8 + t * 0.2 at t = 0, 0.25, 0.7 and 1, within about 6%. pbrt's `mix` material picks the material per hit point by hashing the point, so on Metal check that two different amounts really give different brightness, not an average of the two.
- [ ] By eye: select an object, tick **Mix with a second material** at the bottom of its material properties (the second starts as a rough copper), set the amount to 0.5 and the first material to red matte: the ball should look like red paint worn through to metal, in a fine grain, in the 3D view's Rendered mode and in a normal Render, with CPU and Metal looking alike.
- [ ] `scene_uv_tests`: `APictureRepeatsAndMovesAcrossAQuad` on Metal: a picture that is red on its left half and blue on its right, on a quad, with **Texture repeats across** 1, 2 and with **Texture shift across** 0.5: unrepeated one half is red and the other blue, repeated twice each half has one of each, shifted by half the halves swap.
- [ ] By eye: a box with a picture and **Texture repeats across/up** 4: the picture tiles four times on each face; a negative number mirrors it. A sphere with a picture says in its properties that the repeats have no effect on it.


## Rounded box, cyclorama and terrain (Windows, 2026-10-11)

Three more generated shapes (`scene_more_shapes_tests.cpp`, no render tests: they are plain triangle meshes, so only the usual Metal drawing of triangles is involved).

- [ ] By eye, with Rendered on and in a normal Render on Metal: Add > **Cyclorama (studio backdrop)** with the **Studio** lighting and a **Rounded box** standing on its floor: the floor should curve smoothly up into the wall with no visible seam or crease, the rounded box should have smooth rounded edges (no faceting at its corners), and both should look like the CPU render of the same scene.
- [ ] Add > **Terrain (rolling ground)**: smooth rolling hills lit by a sun; change **Seed**: a different ground each time; detail 128: still renders without trouble. Compare with the CPU.
- [ ] Review fixes (self-test covers them; by eye on the Mac): with **Rendered** on, the Orthographic button is greyed and the numpad 5 key does nothing; **Through camera** with a lens radius above 0 restarts the render with depth of field.


## Studio Showcase and the review fixes (Windows, 2026-10-11)

- [ ] Open **Studio Showcase** (Settings tab, Materials; or `ray_tracer --gpu --output out.png 800 256 8 pbrt_scenes/studio-showcase.pbrt`) on Metal and compare it with the CPU render of the same file (`--cpu`): the cyclorama backdrop, the window pattern on the wall (the projector), the worn-paint rounded box (a mix of coated red and rough copper, in a fine grain), the mossy block at the front left (the Mossy stone mix: stone grey with green, a pale mint at this sample count), the wax ball (subsurface), the glass ball over the tiled checker and the gold ball. On Windows the CPU and OptiX means agree to 0.1%. It is listed as `B26` in the Materials tab: check that selecting it in the scene list and pressing Render renders this scene and not Rough Metal Spheres (B1; a file tagged with a built-in category used to take that id).
- [ ] The fixes from your review (portrait framing, the lens diameter, texture shift in repeats, the sensor width of a saved view, applying Sunny twice) are covered by unit tests; by eye on the Mac, with **Rendered** on: set the picture to portrait (render size 600 x 800) and look through the camera: the framing should match the overlays, and with a lens radius above 0 the depth of field should now be the strength a normal Render has.
- [ ] CI: the Windows and macOS unit-test jobs now build the CPU launcher and give it to the tests through `RT_LAUNCHER`, so the render tests (about fifteen) should show as run, not SKIPPED, in the next run: if a job fails at "Build the launcher (ray_tracer)", paste its log.


## Ready-made mixes, the Denoise tick, interface polish, normal maps on disks and the combo sweep (Windows, 2026-10-12)

Everything below ran on Windows only (CPU, recursive OptiX and wavefront OptiX; 5,029 unit tests pass, the GUI self-tests pass). What each item needs from a Mac is in the box.

**Unit tests**

- [ ] `scene_mix_tests`: the new `SceneMixPresetTest` (each of the eight ready-made mixes is valid, plain in its second material, written as a pbrt mix and survives the file; applying one keeps the bump slot) and `SceneMixPresetRenderTest.ADirtySnowPresetReadsTheBlendItsAmountSays` (a plate of the Dirty snow mix under a white sky reads `(1 - amount) * first + amount * second`, within 6%: run it with `--gpu` too if you can, as for the other mix test).
- [ ] `scene_showcase_tests`: the showcase now has two mixed materials (the worn paint box and a Mossy stone block), and `pbrt_scenes/studio-showcase.pbrt` was written again, so `TheCommittedFileIsExactlyWhatTheCodeWritesAndItOpensInTheBuilder` must still pass on the Mac (it compares numbers rounded to 10 digits).

**By eye (Metal, in Rendered mode and in a normal Render, compared with the CPU)**

- [ ] **Ready-made mix** (a list under *Mix with a second material* in an object's material properties, with a small picture of each): pick each of the eight on a sphere: Rusty metal (grey metal with rust), Chipped paint (blue coat showing steel), Dusty glass (glass with a dusting of diffuse), Mossy stone, Wet sand, Worn leather, Scuffed plastic, Dirty snow. Each should look like its CPU render, in a fine grain. Dusty glass is the one to watch: it mixes a dielectric with a diffuse material. One undo takes the preset away.
- [ ] The **Studio Showcase** picture (the Scene Builder tab, Rendered on) has a pale green rounded block at the front left (Mossy stone); it should be there and look the same as on the CPU.
- [ ] **Interface**, in the Scene Builder tab, at your usual window size and then narrower (about 1100 pixels), in English and in Japanese or French: the properties pane starts wide enough that a name, a picture path and the X/Y/Z boxes have room (the label sits above the box); checkboxes are on their own line; the 3D view's tool buttons wrap instead of running off the edge; the **Add** menu has **Props**, **Volumes**, **Lights** and **Lighting** as submenus; the long explanations (mix, camera, projector, sky, fog, subsurface) are folded to one **About this** line that opens on a click; the Camera path tick, Flythrough slider and Play button appear only once there are two saved views. The Mac's Qt style differs from Windows: look for clipped labels or text running into a box, and say which control.
- [ ] **Denoise** (a tick in the Preview row, next to *Use the GPU*): on a Mac it needs Intel Open Image Denoise for both the CPU and the Metal preview. Ticking it without the library should ask to download it (the same prompt as the Render Options tab), and untick again if you decline. With the library: a **Draft** preview (480 pixels, 16 samples) with the tick should be much cleaner than without it, with edges, the window pattern on the showcase's wall and the tile's picture still sharp; **Render picture...** must not be denoised. The choice is kept after restarting the program. (On Windows with the GPU, NVIDIA's denoiser makes a Draft picture 2.4 to 6 times closer to a 1024-sample one: the CPU/OIDN path has not been tried on Windows, the library is not installed there.)
- [ ] **Normal map on a disk or a cylinder**: on Windows this used to stop the whole OptiX render (a `__trap()`); it works now. On Metal: add a disk (and a cylinder), give it a normal map picture (an RGB picture of tilted normals), and Render: it should draw, shaded like the CPU. Also `python scripts/consistency_sweep.py --exe <ray_tracer> --backends cpu,gpu --only normalmap,texturemap` should report 0 pairs off (it has new "flat normal map changes nothing" pairs for a disk and a cylinder).
- [ ] The warning about a bump picture changed (`scene_validate.h`): a height map (grayscale) on a **sphere** says the GPU does not draw it; on a **disk or cylinder** the text says OptiX does not and Metal does, which should be true of your Metal build (the height map should show on a disk and a cylinder, not on a sphere); a **cone**, a box and the ready-made shapes get no warning; a normal map gets none anywhere. Tell me if a height map shows or does not show on Metal where the text says otherwise.

**The combo sweep** (`scripts/combo_sweep.py`): every shape under 15 material treatments, and the showcase under each lighting, rendered on the CPU and the GPU and compared block by block. From the repository root:

```
RT_COMBO_SCENES_DIR=/tmp/combos <build>/ray_tracer_tests --gtest_filter='SceneCombosTest.*'
python scripts/combo_sweep.py --dir /tmp/combos --exe <build>/ray_tracer --skip-wavefront
```

- [ ] It prints a line for a scene that does not render on a backend or looks different, and ends `N scenes, M with problems`. On Windows the only differences are three known ones (a height map on a sphere, disk or cylinder, glass whose flat face lies exactly on the floor, and subsurface on a single open quad; the script labels them KNOWN). **On the Mac the height map on a disk and a cylinder should not differ** (Metal draws it there), so those two should disappear from the KNOWN lines, and anything else that appears is new: paste it, with the scene's name (for example `cylinder__normalmap`). Needs Pillow and numpy (`pip install pillow numpy`).
