# Animation toolkit

A short film in Photonsmith is a Python function of time. `build(t)` returns the complete pbrt-v4 scene for the instant `t` (seconds); `anim_lib.py` renders every frame with `ray_tracer.exe`
(GPU, path traced, denoised) and joins the frames into an MP4 with ffmpeg. Plain Python 3, no packages (Pillow is used only for the optional contact sheet). ffmpeg must be on `PATH`.

The renderer itself needs nothing new: each frame is an ordinary `.pbrt` file, so every material, light and shape the renderer supports can be animated.

## "Lamp and Ball"

`lamp_and_ball.py` is a 20 second cartoon built with it: a desk lamp wakes up, notices a ball, hops over, nudges it, watches it bounce off a stack of books and roll home, and tucks its
shade over it.

```
python scripts/anim/lamp_and_ball.py --still 7.2                 # one frame, to look at
python scripts/anim/lamp_and_ball.py --preview --sheet 2,6,9,12  # stills of those times tiled into one picture
python scripts/anim/lamp_and_ball.py --preview                   # the whole film at 480 x 270 (a few minutes)
python scripts/anim/lamp_and_ball.py --final                     # 1280 x 720, 384 samples (about an hour)
```

Frames go to `scripts/anim/out/<name>_<preview|draft|final>/frame_#####.png` and the video next to them. Rendering resumes: frames that already exist are skipped (`--redo` renders them again).

## What `anim_lib.py` gives a film

| | |
|---|---|
| `Track(keys, ease)` | keyframes `(time, value[, ease])`, numbers or vectors; eases: `linear`, `smooth`, `in`, `out`, `back` (overshoot), `elastic` |
| matrices | `translate`, `scale`, `rotate(axis, degrees)`, `frame_matrix`, `mat_mul(a, b, ...)`, `invert_affine` |
| `Mesh` | `lathe(profile)` (anything round), `uv_sphere`, `box`, `rounded_box`; `mesh.to_pbrt(matrix)` writes a `trianglemesh` with normals and uv; squash and stretch (non-uniform scale) keeps correct shading |
| `attr`, `coated`, `diffuse` | small pbrt text helpers |
| `film_cli(build, duration, fps, name)` | the command line above |

## Notes on the cartoon's craft

* Squash and stretch: the lamp crouches before a hop, stretches in the air and squashes on landing; the ball stretches along its velocity while it flies.
* Anticipation and follow-through: the head winds back before the nudge; waking up uses an overshooting ease.
* The arms are solved by two-link inverse kinematics, so the head can be put exactly where the story needs it (over the ball, on the ball, looking at it).
* The ball's motion is a small physics pass; the books' bounce is bisected so that the ball comes to rest exactly where the lamp's shade comes down.
* pbrt is left-handed: the story is written with +x to the right of the picture and `W` mirrors it on the way out.
