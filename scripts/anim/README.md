# Looking at a cartoon film

The cartoon films (Lamp and Ball, Ball Parade, The Little Robot, Fireflies) are C++: `src/shared/cartoon/cartoon_lib.h` is the toolkit, there is one header per film, and `cartoons.h`
lists them. A film is a function `std::string sceneAt(double t)` that returns the complete pbrt-v4 scene for the instant `t` (seconds), so every material, light and shape the renderer
supports can be animated. There is one implementation of the toolkit; the GUI, `--video-preset` and this script all render the same thing.

`film.py` is plain Python 3 and only drives `ray_tracer` (the GPU, denoised, unless you pass `--cpu`):

```
python scripts/anim/film.py list                            # the films and their lengths
python scripts/anim/film.py still fireflies@13.5            # one frame, to look at (no @: the film's own best still)
python scripts/anim/film.py sheet fireflies --count 16      # the whole film as a contact sheet: stills tiled with their times (needs Pillow)
python scripts/anim/film.py sheet little-robot --times 2,8,20,30
python scripts/anim/film.py video fireflies                 # the finished video
```

A frame is also an ordinary scene: `ray_tracer --gpu --denoise 1280 256 8 cartoon:fireflies@13.5`, and the GUI's Cartoons tab lists a still of each film.

## Writing a film

Add `src/shared/cartoon/<name>.h` with `headerAt(t)`, `sceneAt(t)`, `shotStarts()` and `kSamples` in the style of the others, and a row at the end of the table in `cartoons.h`: key, name, description,
length, fps, still time and the `meta` record (the next free preset number, tags from the vocabulary, one name for each shot, samples). Rebuild the renderer (`ray_tracer.exe` and
`scene_metadata.dll`: the GUI does not compile the films, it reads them from that library, qt_gui/film_info.h), `film.py sheet <key>` to look at it,
`film.py doctor <key>` to have every shot checked (black, very dark, blown-out and flat frames, jumps across cuts, slow shots, the whole render's time), then
`python scripts/measure_cartoons.py <key> --write` for its cost and `RT_UPDATE_SNAPSHOT=1 ray_tracer_tests --gtest_filter=CartoonFilmSnapshot*`; paste `ray_tracer --film-table` into the docs. `docs/VIDEO_GENERATION.md` has the user's side.

## The film kit: start a long film here

A film told in many shots (tengwang, 59 of them) starts from the kit instead of its own plumbing:

- `film_kit.h`: a `kit::Moment` (camera, exposure, look, haze, depth, light, content) that a film's `Frame` derives from with what its own shots set (which sky, a sun); a
  `kit::Shot<Frame>` table (start, name, a function filling the moment at time t) from which `startsOf`, `kit::shotNamesOf` and `kit::momentAt` give shotStarts(), the shot names
  for the film table and the moment of any time; and `kit::sceneText` / `kit::headerText`, which write the scene and its header - a moment with an iso of 1 or less is a black frame
  (a cut to black, a title card), made a near-black scene at an ordinary exposure (an exposure of almost nothing took the renderer minutes a frame). `kit::cam`, `glide`, `ease`.
- `kit_people.h`: robed figures sculpted from blended shapes in a `Pose` and a `Dress` (standing, clasping, bowing, kneeling, walking, carrying; ladies, soldiers, a beard),
  placed as cached PLY objects (`kit::crowdText(film, version, people)`) or sculpted for a frame (`figureMeshes`, `figureText`), and horses, boats, birds and skeins, sheep.
- `film_sky.h` (painted HDR skies from dawn to night, with clouds and stars), `film_cache.h` (heavy meshes written once as PLY files), `Caption` (words on the screen).

`tengwang.h` is the worked example. Then `film.py doctor`, and `--draft 8` to watch the whole film with its sound before the real render.

## What the toolkit gives a film

| | |
|---|---|
| `Track` / `Track3` | keyframes `{time, value, ease}`; eases: linear, smooth, in, out, back (overshoot), elastic |
| matrices | `translate`, `scale`, `rotate(axis, degrees)`, `mul(a, b, ...)`, `invertAffine` |
| `Mesh` | `lathe(profile)` (anything round), `uvSphere`, `box`, `roundedBox`, `torus`, `capsule`; `appendPbrt` writes a `trianglemesh` with normals and uv; squash and stretch (non-uniform scale) keeps correct shading |
| `limbIk` | two-link inverse kinematics, so a hand can be put exactly where the story needs it |
| `Shot`, `cameraAt` | cuts and camera moves: each shot has its own eye, target, fov and lens, and `cameraAt(shots, t)` picks the one that is on |
| `attr`, `coated`, `diffuseMaterial` | small pbrt text helpers |

## Notes on the craft

* Squash and stretch: a character crouches before a hop, stretches in the air and squashes on landing.
* Anticipation and follow-through: the head winds back before a nudge; waking up uses an overshooting ease.
* Light the film from the film: the fireflies are small emitters, so they light the grass round them; the moon is a large cool emitter.
* The story space is +x to the right of the picture, +y up, +z towards the camera; every emitted x is negated because pbrt is left-handed.
