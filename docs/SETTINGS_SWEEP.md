# Settings sweep: every pair of settings

The renderer's settings are read by different renderers (the CPU path tracer, the CPU's other integrators, OptiX, Metal), and some combinations are refused on purpose (`--sppm` with
`--video`, two integrators at once). `src/shared/backend_capabilities.h` says which renderer reads which option and the launcher warns about one that is ignored, but nobody tries the
combinations by hand. `scripts/settings_sweep.py` does.

```
python scripts/settings_sweep.py                       # every pair of setting values, CPU and GPU
python scripts/settings_sweep.py --backend cpu         # only the CPU cases (no GPU needed)
python scripts/settings_sweep.py --only cartoon       # only the cartoon-video cases
python scripts/settings_sweep.py --report sweep.json   # every case and what was found, as JSON
```

It builds an **all-pairs** set of cases (every value of every setting appears with every value of every other setting in at least one run: about 75 runs instead of millions) over the
mode (one image, a camera-path video, a cartoon video), the backend, the integrator (default, wavefront, BDPT, MLT, SPPM and the five debug integrators), the denoiser, adaptive sampling, the
sampler, the light sampler, spectral rendering, regularize, exposure, tone mapping, the seed, a crop window, a time limit, the component clamp, a depth-of-field override, the output file
format, render passes, the accelerator, a video's speed and rate, and a cartoon's resume, jobs and timeout. Each run is tiny (48 x 27 pixels, 2 samples), and the sweep checks that

* a combination the launcher refuses on purpose exits non-zero and says why, and one it does not refuse exits 0 (or fails with a reason: a material the chosen mode does not support, a
  scene the GPU cannot draw);
* nothing crashes or hangs;
* an image run writes a picture of the right size that is not all black (unless a crop window was asked for);
* a video run completes;
* a flag the chosen renderer ignores is said to be ignored (a warning that names the flag).

The first run found three things, all fixed: a video of an odd size failed in ffmpeg (yuv420p needs even sizes; the picture is now cropped by one row or column), a cartoon video in which every frame failed ended the program abnormally (its PNG converter thread was still running), and `--sppm --gpu` on a scene GPU SPPM cannot draw (it only has spheres and quads: the Cornell Box, the default scene, is triangles) failed instead of using the CPU's SPPM, which renders every scene (it now says so and does). Run it after changing how settings are read or how the launcher starts a render; it needs a built `ray_tracer`, Pillow for the picture check, and about ten minutes with a GPU.
