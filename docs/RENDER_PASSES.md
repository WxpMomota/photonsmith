# Render passes (AOVs)

A render pass shows one property of the scene instead of the finished lit picture. Compositing programs (Blender, Nuke, After Effects, Fusion) use them to adjust a render after the fact: relight with the normals, add fog or depth of field from the depth, recolour a surface from the albedo.

`--aovs` (or **Also write render passes** in the Render Options tab) writes these channels next to the image:

| Channel | What it holds |
|---|---|
| `albedo.R/G/B` | the surface's own colour, without lighting: a matte surface gives its colour, glass and mirrors their tint; 0 on a pure light and where nothing is hit |
| `normal.X/Y/Z` | the surface direction in world space, turned to face the camera, unit length; 0 where nothing is hit |
| `depth.Z` | the distance from the camera to the surface along the ray (not along the view axis); 0 where nothing is hit |
| `uv.U/V` | the surface's texture coordinates |
| `A` | coverage: 1 where the camera sees a surface, 0 where it sees only the background, in between on silhouettes |

Where a pixel is part surface and part background, the surface channels are averaged over the surface samples only, so an object's edge keeps its own value instead of fading towards 0 (use `A` to blend).

**Where they go.** If the image is an `.exr` (`ray_tracer ... --output shot.exr --aovs`), the passes are merged into it: one multilayer EXR with `R G B` (the render) and the passes above. For any other image type they are written to `<name>.aovs.exr` beside it. Single images only (not video).

**How they are made.** The beauty image comes from whichever renderer you chose (CPU, OptiX or Metal). The passes come from a separate first-hit pass over the camera's rays on the CPU scene, with up to 16 samples per pixel for anti-aliasing, so they cost very little, work the same for every renderer, and never change the picture. `src/TheRestOfYourLife/aov_pass.h` is the pass; `cpu_render_main_aovs()` writes it; `cpu_merge_exr_passes()` merges it into an image's EXR; `src/shared/exr_writer.h` has the multi-channel EXR reader and writer (channels are written sorted by name, as the format requires).

## Why is this pixel this colour? (the pixel probe)

The passes above are for compositing. When a pixel is simply the wrong brightness against another renderer (or against a closed form), the probe says where its light came from:

```
ray_tracer --cpu --probe 120 45 400 4000 8 kroken --height 300
```

`--probe X Y` renders ONE pixel (column X, row Y, counted from the top left of the image the other arguments describe) with the CPU path tracer and, instead of a picture, prints:

* the pixel's mean radiance (linear, before exposure and tone mapping);
* that radiance split by **source**: emitters the paths hit (MIS-weighted), the sky or background a path escapes to, and the next-event estimates toward area lights, toward the sky and portal lights, and toward point / spot / distant lights;
* the same split by **bounce** (0 is light arriving at the first hit);
* each **area light** that lit it, with its share and where the paths found it;
* what the **camera rays hit first**: material class, position, normal, uv, front or back face.

The samples-per-pixel argument is the number of paths averaged (a few thousand is a good number; the report is only as steady as that average). The report is also written to `<--output>.probe.txt` when `--output` is given. It reports on the default path tracer on the CPU: the GPU backends and the other integrators do not book their light by source (a `--probe` with `--sppm`, `--bdpt`, `--mlt` or a debug integrator is refused).

In the GUI, right-click a rendered picture in the Preview tab and choose "Why is this pixel this colour?": it runs the probe on the pixel under the pointer with the settings that image was rendered with (a few seconds for a small scene, longer for a big one: the scene is loaded again) and shows the report. It is only offered for images rendered in the current session.

How it works: `src/shared/path_probe.h` is a sink that `camera_path.h`'s `ray_color()` reports into at the places it adds light to a path (one null-pointer check per call site when no probe runs). The test `PathProbeIntegration` checks the books against a closed form (a diffuse floor of albedo 0.5 under a white sky returns exactly 0.5, all of it the sky's) and that what the probe books is what `ray_color()` returns, sample for sample.

**Not (yet) there:** object / material ids, direct-versus-indirect light splits, a shadow catcher, light groups. The Metal renderer writes no passes of its own, and the denoiser on a Mac sees colour only (the passes could guide it; see DENOISING.md).
