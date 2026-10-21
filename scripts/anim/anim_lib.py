"""anim_lib.py -- a small animation toolkit for Photonsmith: describe a short film as a Python function of time, and render it frame by frame.

A film is a function `build(t) -> str` that returns the complete pbrt-v4 scene text for the instant `t` (seconds). This library supplies what such a function needs:

  * Track         keyframes with easing (numbers or vectors)
  * matrices      translate / scale / rotate / look-at frames, composed with `@`-style `mat_mul`
  * Mesh          triangle meshes: lathe (anything round: lamp shades, arms, bases), UV sphere, box; transformed per frame and written as pbrt `trianglemesh`
  * render_film   writes one .pbrt per frame, runs ray_tracer.exe on it (GPU, denoised), skips frames already rendered, and joins the frames into an MP4 with ffmpeg

Everything is plain Python 3 with no packages. See scripts/anim/README.md.
"""
import argparse
import math
import os
import shutil
import subprocess
import sys
import time

# --------------------------------------------------------------------------------------------------------------------------------------------------------------
# Easing and keyframe tracks
# --------------------------------------------------------------------------------------------------------------------------------------------------------------


def clamp01(u):
    return 0.0 if u < 0.0 else (1.0 if u > 1.0 else u)


def ease_linear(u):
    return u


def ease_smooth(u):          # slow in, slow out
    return u * u * (3.0 - 2.0 * u)


def ease_in(u):
    return u * u


def ease_out(u):
    return 1.0 - (1.0 - u) * (1.0 - u)


def ease_back_out(u):        # overshoots the target a little, then settles: the "anticipation / follow-through" of a cartoon
    c1 = 1.70158
    c3 = c1 + 1.0
    v = u - 1.0
    return 1.0 + c3 * v * v * v + c1 * v * v


def ease_elastic_out(u):     # wobbles about the target
    if u <= 0.0:
        return 0.0
    if u >= 1.0:
        return 1.0
    return math.pow(2.0, -9.0 * u) * math.sin((u * 10.0 - 0.75) * (2.0 * math.pi / 3.0)) + 1.0


EASES = {
    "linear": ease_linear, "smooth": ease_smooth, "in": ease_in, "out": ease_out,
    "back": ease_back_out, "elastic": ease_elastic_out,
}


def lerp(a, b, u):
    if isinstance(a, (tuple, list)):
        return tuple(x + (y - x) * u for x, y in zip(a, b))
    return a + (b - a) * u


class Track:
    """Keyframes [(time, value) or (time, value, ease)]: the value at `t` blends from the key before to the key after with the EASE OF THE KEY AFTER (so the ease says how
    the motion ARRIVES). Before the first key it holds the first value, after the last it holds the last."""

    def __init__(self, keys, ease="smooth"):
        self.keys = sorted(((k[0], k[1], k[2] if len(k) > 2 else ease) for k in keys), key=lambda k: k[0])

    def __call__(self, t):
        keys = self.keys
        if t <= keys[0][0]:
            return keys[0][1]
        if t >= keys[-1][0]:
            return keys[-1][1]
        for i in range(1, len(keys)):
            if t <= keys[i][0]:
                t0, v0, _ = keys[i - 1]
                t1, v1, ease = keys[i]
                u = (t - t0) / (t1 - t0) if t1 > t0 else 1.0
                return lerp(v0, v1, EASES[ease](clamp01(u)))
        return keys[-1][1]


# --------------------------------------------------------------------------------------------------------------------------------------------------------------
# Vectors and 4x4 matrices (row-major lists of 16; points are column vectors: p' = M p)
# --------------------------------------------------------------------------------------------------------------------------------------------------------------


def vadd(a, b): return (a[0] + b[0], a[1] + b[1], a[2] + b[2])
def vsub(a, b): return (a[0] - b[0], a[1] - b[1], a[2] - b[2])
def vmul(a, s): return (a[0] * s, a[1] * s, a[2] * s)
def vdot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
def vcross(a, b): return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])
def vlen(a): return math.sqrt(vdot(a, a))


def vnorm(a):
    n = vlen(a)
    return (a[0] / n, a[1] / n, a[2] / n) if n > 1e-12 else (0.0, 1.0, 0.0)


IDENTITY = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]


def mat_mul(*ms):
    out = ms[0]
    for m in ms[1:]:
        out = [sum(out[r * 4 + k] * m[k * 4 + c] for k in range(4)) for r in range(4) for c in range(4)]
    return out


def translate(x, y=None, z=None):
    if y is None:
        x, y, z = x
    return [1, 0, 0, x, 0, 1, 0, y, 0, 0, 1, z, 0, 0, 0, 1]


def scale(x, y=None, z=None):
    if y is None:
        y = z = x
    return [x, 0, 0, 0, 0, y, 0, 0, 0, 0, z, 0, 0, 0, 0, 1]


def rotate(axis, degrees):
    x, y, z = vnorm(axis)
    c, s = math.cos(math.radians(degrees)), math.sin(math.radians(degrees))
    t = 1.0 - c
    return [t * x * x + c, t * x * y - s * z, t * x * z + s * y, 0,
            t * x * y + s * z, t * y * y + c, t * y * z - s * x, 0,
            t * x * z - s * y, t * y * z + s * x, t * z * z + c, 0,
            0, 0, 0, 1]


def frame_matrix(origin, x_axis, y_hint=(0.0, 1.0, 0.0)):
    """A rigid frame at `origin` whose local +X is `x_axis`, with local +Y as close to `y_hint` as it can be."""
    xa = vnorm(x_axis)
    za = vnorm(vcross(xa, y_hint))
    if vlen(vcross(xa, y_hint)) < 1e-6:
        za = vnorm(vcross(xa, (0.0, 0.0, 1.0)))
    ya = vcross(za, xa)
    return [xa[0], ya[0], za[0], origin[0], xa[1], ya[1], za[1], origin[1], xa[2], ya[2], za[2], origin[2], 0, 0, 0, 1]


def xform_point(m, p):
    return (m[0] * p[0] + m[1] * p[1] + m[2] * p[2] + m[3],
            m[4] * p[0] + m[5] * p[1] + m[6] * p[2] + m[7],
            m[8] * p[0] + m[9] * p[1] + m[10] * p[2] + m[11])


def xform_dir(m, d):
    return (m[0] * d[0] + m[1] * d[1] + m[2] * d[2],
            m[4] * d[0] + m[5] * d[1] + m[6] * d[2],
            m[8] * d[0] + m[9] * d[1] + m[10] * d[2])


def invert_affine(m):
    """Inverse of a matrix whose bottom row is 0 0 0 1 (translations, rotations, scales)."""
    a, b, c, d, e, f, g, h, i = m[0], m[1], m[2], m[4], m[5], m[6], m[8], m[9], m[10]
    det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g)
    inv = [(e * i - f * h) / det, (c * h - b * i) / det, (b * f - c * e) / det,
           (f * g - d * i) / det, (a * i - c * g) / det, (c * d - a * f) / det,
           (d * h - e * g) / det, (b * g - a * h) / det, (a * e - b * d) / det]
    tx, ty, tz = m[3], m[7], m[11]
    return [inv[0], inv[1], inv[2], -(inv[0] * tx + inv[1] * ty + inv[2] * tz),
            inv[3], inv[4], inv[5], -(inv[3] * tx + inv[4] * ty + inv[5] * tz),
            inv[6], inv[7], inv[8], -(inv[6] * tx + inv[7] * ty + inv[8] * tz),
            0, 0, 0, 1]


# --------------------------------------------------------------------------------------------------------------------------------------------------------------
# Triangle meshes
# --------------------------------------------------------------------------------------------------------------------------------------------------------------


class Mesh:
    def __init__(self, verts, normals, uvs, tris):
        self.verts, self.normals, self.uvs, self.tris = verts, normals, uvs, tris

    def transformed(self, m):
        """The mesh through `m`; normals use the inverse transpose, so a squashed or stretched mesh keeps correct shading."""
        inv = invert_affine(m)
        nrm = [(inv[0], inv[4], inv[8]), (inv[1], inv[5], inv[9]), (inv[2], inv[6], inv[10])]   # rows of the inverse transpose
        verts = [xform_point(m, v) for v in self.verts]
        normals = [vnorm((n[0] * nrm[0][0] + n[1] * nrm[0][1] + n[2] * nrm[0][2],
                          n[0] * nrm[1][0] + n[1] * nrm[1][1] + n[2] * nrm[1][2],
                          n[0] * nrm[2][0] + n[1] * nrm[2][1] + n[2] * nrm[2][2])) for n in self.normals]
        return Mesh(verts, normals, self.uvs, self.tris)

    def to_pbrt(self, m=None, flip=False):
        mesh = self.transformed(m) if m is not None else self
        tris = mesh.tris
        if flip:
            tris = [(a, c, b) for (a, b, c) in tris]
        f = lambda v: "%.5f %.5f %.5f" % v
        out = ['Shape "trianglemesh" "integer indices" [ ' + " ".join("%d %d %d" % t for t in tris) + ' ]',
               '  "point3 P" [ ' + "  ".join(f(v) for v in mesh.verts) + ' ]',
               '  "normal N" [ ' + "  ".join(f(n) for n in mesh.normals) + ' ]']
        if mesh.uvs:
            out.append('  "point2 uv" [ ' + "  ".join("%.5f %.5f" % uv for uv in mesh.uvs) + ' ]')
        return "\n".join(out)


def lathe(profile, segments=48, axis="y", smooth=True):
    """A surface of revolution about local +Y: `profile` is a list of (radius, height) points from bottom to top (a point with radius 0 closes the surface at the axis).
    Normals come from the profile's own slope, smoothed along it when `smooth`. uv = (angle / 2 pi, position along the profile)."""
    n = len(profile)
    # the profile's 2D normal at each point (pointing outwards: to the right of the direction of travel, bottom to top)
    pn = []
    for i in range(n):
        a = profile[max(i - 1, 0)]
        b = profile[min(i + 1, n - 1)]
        dr, dy = b[0] - a[0], b[1] - a[1]
        l = math.hypot(dr, dy) or 1.0
        pn.append((dy / l, -dr / l))
    verts, normals, uvs, tris = [], [], [], []
    for j in range(segments + 1):
        ang = 2.0 * math.pi * j / segments
        ca, sa = math.cos(ang), math.sin(ang)
        for i, (r, y) in enumerate(profile):
            verts.append((r * ca, y, r * sa))
            normals.append((pn[i][0] * ca, pn[i][1], pn[i][0] * sa))
            uvs.append((j / segments, i / max(n - 1, 1)))
    for j in range(segments):
        for i in range(n - 1):
            a = j * n + i
            b = (j + 1) * n + i
            tris.append((a, b, a + 1))
            tris.append((b, b + 1, a + 1))
    return Mesh(verts, normals, uvs, tris)


def uv_sphere(radius=1.0, nu=48, nv=24):
    profile = []
    for i in range(nv + 1):
        th = math.pi * i / nv - math.pi / 2.0
        profile.append((radius * math.cos(th), radius * math.sin(th)))
    profile[0] = (0.0, -radius)
    profile[-1] = (0.0, radius)
    return lathe(profile, nu)


def box(sx, sy, sz):
    """An axis-aligned box centred on the origin with flat faces."""
    hx, hy, hz = sx / 2.0, sy / 2.0, sz / 2.0
    faces = [((1, 0, 0), [(hx, -hy, -hz), (hx, hy, -hz), (hx, hy, hz), (hx, -hy, hz)]),
             ((-1, 0, 0), [(-hx, -hy, hz), (-hx, hy, hz), (-hx, hy, -hz), (-hx, -hy, -hz)]),
             ((0, 1, 0), [(-hx, hy, -hz), (-hx, hy, hz), (hx, hy, hz), (hx, hy, -hz)]),
             ((0, -1, 0), [(-hx, -hy, hz), (-hx, -hy, -hz), (hx, -hy, -hz), (hx, -hy, hz)]),
             ((0, 0, 1), [(-hx, -hy, hz), (hx, -hy, hz), (hx, hy, hz), (-hx, hy, hz)]),
             ((0, 0, -1), [(hx, -hy, -hz), (-hx, -hy, -hz), (-hx, hy, -hz), (hx, hy, -hz)])]
    verts, normals, uvs, tris = [], [], [], []
    for nrm, quad in faces:
        base = len(verts)
        for k, v in enumerate(quad):
            verts.append(v)
            normals.append(nrm)
            uvs.append(((0, 0), (1, 0), (1, 1), (0, 1))[k])
        tris += [(base, base + 1, base + 2), (base, base + 2, base + 3)]
    return Mesh(verts, normals, uvs, tris)


def rounded_box(sx, sy, sz, radius, steps=4):
    """A box with rounded edges (a book, a block): built by pushing a subdivided box's vertices onto a rounded shape."""
    hx, hy, hz = sx / 2.0 - radius, sy / 2.0 - radius, sz / 2.0 - radius
    n = steps * 2 + 2
    verts, normals, uvs, tris = [], [], [], []
    axes = [((1, 0, 0), (0, 1, 0), (0, 0, 1)), ((-1, 0, 0), (0, 0, 1), (0, 1, 0)), ((0, 1, 0), (0, 0, 1), (1, 0, 0)),
            ((0, -1, 0), (1, 0, 0), (0, 0, 1)), ((0, 0, 1), (1, 0, 0), (0, 1, 0)), ((0, 0, -1), (0, 1, 0), (1, 0, 0))]
    for nrm, ua, va in axes:
        base = len(verts)
        for iy in range(n + 1):
            for ix in range(n + 1):
                u = ix / n * 2.0 - 1.0
                v = iy / n * 2.0 - 1.0
                p = (nrm[0] + ua[0] * u + va[0] * v, nrm[1] + ua[1] * u + va[1] * v, nrm[2] + ua[2] * u + va[2] * v)   # on the cube's surface
                outer = (p[0] * (hx + radius), p[1] * (hy + radius), p[2] * (hz + radius))   # the point on the full-size cube
                inner = (max(-hx, min(hx, outer[0])), max(-hy, min(hy, outer[1])), max(-hz, min(hz, outer[2])))   # the nearest point of the shrunk box
                d = vsub(outer, inner)
                nn = vnorm(d) if vlen(d) > 1e-9 else nrm
                verts.append(vadd(inner, vmul(nn, radius)))
                normals.append(nn)
                uvs.append((ix / n, iy / n))
        for iy in range(n):
            for ix in range(n):
                a = base + iy * (n + 1) + ix
                tris += [(a, a + 1, a + n + 2), (a, a + n + 2, a + n + 1)]
    return Mesh(verts, normals, uvs, tris)


# --------------------------------------------------------------------------------------------------------------------------------------------------------------
# Scene text helpers
# --------------------------------------------------------------------------------------------------------------------------------------------------------------


def fmt3(v):
    return "%.5f %.5f %.5f" % tuple(v)


def attr(material, shapes, emission=None):
    """An AttributeBegin ... AttributeEnd block: one material (pbrt text) and one or more shape strings, optionally an area light."""
    out = ["AttributeBegin"]
    if emission is not None:
        out.append('  AreaLightSource "diffuse" "rgb L" [ %s ]' % fmt3(emission))
    out.append("  " + material)
    for s in (shapes if isinstance(shapes, (list, tuple)) else [shapes]):
        out.append(s)
    out.append("AttributeEnd")
    return "\n".join(out)


def coated(color, roughness=0.05):
    return 'Material "coateddiffuse" "rgb reflectance" [ %s ] "float roughness" [ %.4f ]' % (fmt3(color), roughness)


def diffuse(color):
    return 'Material "diffuse" "rgb reflectance" [ %s ]' % fmt3(color)


# --------------------------------------------------------------------------------------------------------------------------------------------------------------
# Rendering and assembly
# --------------------------------------------------------------------------------------------------------------------------------------------------------------

DEFAULT_EXE = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "RayTracer_Package", "ray_tracer.exe"))


def frame_name(i):
    return "frame_%05d" % i


def render_frame(scene_text, base_path, width, height, spp, depth, exe, denoise=True, seed=1, keep_scene=False):
    """Writes `base_path`.pbrt and renders it to `base_path`.png; returns True on success."""
    pbrt_path = base_path + ".pbrt"
    png_path = base_path + ".png"
    with open(pbrt_path, "w", encoding="utf-8", newline="\n") as f:
        f.write(scene_text)
    cmd = [exe, "--gpu"] + (["--denoise"] if denoise else []) + ["--seed", str(seed), "--height", str(height), "--output", png_path,
                                                                    str(width), str(spp), str(depth), pbrt_path]
    res = subprocess.run(cmd, cwd=os.path.dirname(exe), capture_output=True, text=True)
    ok = res.returncode == 0 and os.path.exists(png_path)
    if not ok:
        sys.stderr.write("render failed for %s\n%s\n%s\n" % (pbrt_path, res.stdout[-1500:], res.stderr[-1500:]))
    elif not keep_scene:
        os.remove(pbrt_path)
    return ok


def assemble(frames_dir, fps, out_path, first=0, extra_filters=None, crf=14):
    ffmpeg = shutil.which("ffmpeg")
    if not ffmpeg:
        sys.stderr.write("ffmpeg is not on PATH: the frames stay in %s\n" % frames_dir)
        return False
    cmd = [ffmpeg, "-y", "-framerate", str(fps), "-start_number", str(first), "-i", os.path.join(frames_dir, "frame_%05d.png")]
    if extra_filters:
        cmd += ["-vf", extra_filters]
    cmd += ["-c:v", "libx264", "-crf", str(crf), "-pix_fmt", "yuv420p", out_path]
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        sys.stderr.write(res.stderr[-2000:] + "\n")
    return res.returncode == 0


def film_cli(build, duration, fps=24, name="film", default_size=(1280, 720), out_root=None, extra_filters=None):
    """The command line of a film script. `build(t)` returns the scene text for time t.

      python film.py --still 3.5            one frame, at t = 3.5 s, to <name>_still.png
      python film.py --preview              a quick low-resolution render of every frame, and the video
      python film.py --final                the full-quality render and the video
      python film.py --frames 100-160       only those frame numbers (resumes: frames already rendered are skipped; --redo renders them again)
    """
    ap = argparse.ArgumentParser(description="Render the film '%s' (%.1f s at %d fps)" % (name, duration, fps))
    ap.add_argument("--still", type=float, default=None, metavar="T", help="render one frame at time T seconds")
    ap.add_argument("--preview", action="store_true", help="low resolution and few samples")
    ap.add_argument("--final", action="store_true", help="full resolution and many samples")
    ap.add_argument("--sheet", default=None, metavar="T1,T2,...", help="render stills at those times and tile them into one contact sheet (needs Pillow)")
    ap.add_argument("--frames", default=None, help="frame range, e.g. 0-99 (default: all)")
    ap.add_argument("--width", type=int, default=None)
    ap.add_argument("--height", type=int, default=None)
    ap.add_argument("--spp", type=int, default=None)
    ap.add_argument("--depth", type=int, default=8)
    ap.add_argument("--exe", default=DEFAULT_EXE)
    ap.add_argument("--redo", action="store_true", help="render frames again even if their picture exists")
    ap.add_argument("--no-video", action="store_true")
    ap.add_argument("--keep-scenes", action="store_true", help="keep each frame's .pbrt next to its picture")
    args = ap.parse_args()

    w, h = default_size
    spp = 128
    if args.preview:
        w, h, spp = 480, 270, 24
    if args.final:
        spp = 384
    if args.width:
        w = args.width
        h = args.height or int(round(w * default_size[1] / default_size[0]))
    elif args.height:
        h = args.height
    spp = args.spp or spp
    root = out_root or os.path.join(os.path.dirname(os.path.abspath(sys.argv[0])), "out")
    tag = "preview" if args.preview else ("final" if args.final else "draft")
    frames_dir = os.path.join(root, name + "_" + tag)
    os.makedirs(frames_dir, exist_ok=True)

    if args.sheet:
        times = [float(x) for x in args.sheet.split(",")]
        os.makedirs(root, exist_ok=True)
        paths = []
        for tt in times:
            base = os.path.join(root, "%s_sheet_%s" % (name, ("%.2f" % tt).replace(".", "_")))
            if not render_frame(build(tt), base, w, h, spp, args.depth, args.exe, keep_scene=args.keep_scenes):
                return 1
            paths.append(base + ".png")
        try:
            from PIL import Image, ImageDraw
        except ImportError:
            print("Pillow is not installed: the stills are in", root)
            return 0
        cols = 3
        rows = (len(paths) + cols - 1) // cols
        sheet = Image.new("RGB", (cols * w, rows * h))
        for k, pth in enumerate(paths):
            im = Image.open(pth).convert("RGB")
            ImageDraw.Draw(im).text((8, 6), "t = %.2f" % times[k], fill=(255, 255, 255))
            sheet.paste(im, ((k % cols) * w, (k // cols) * h))
        out = os.path.join(root, name + "_sheet.png")
        sheet.save(out)
        print("wrote", out)
        return 0

    if args.still is not None:
        base = os.path.join(root, "%s_still_%s" % (name, ("%.2f" % args.still).replace(".", "_")))
        os.makedirs(root, exist_ok=True)
        t0 = time.time()
        ok = render_frame(build(args.still), base, w, h, spp, args.depth, args.exe, keep_scene=args.keep_scenes)
        print(("wrote %s.png (%.1f s)" % (base, time.time() - t0)) if ok else "failed")
        return 0 if ok else 1

    total = int(round(duration * fps))
    first, last = 0, total - 1
    if args.frames:
        a, _, b = args.frames.partition("-")
        first, last = int(a), int(b or a)
    t_start = time.time()
    done = 0
    for i in range(first, last + 1):
        base = os.path.join(frames_dir, frame_name(i))
        if os.path.exists(base + ".png") and not args.redo:
            continue
        if not render_frame(build(i / fps), base, w, h, spp, args.depth, args.exe, keep_scene=args.keep_scenes):
            return 1
        done += 1
        elapsed = time.time() - t_start
        left = (last - i) * elapsed / done
        print("frame %d / %d  (%.1f s each, about %d min left)" % (i, last, elapsed / done, left / 60.0), flush=True)
    if not args.no_video and first == 0 and last == total - 1:
        out_mp4 = os.path.join(root, "%s_%s.mp4" % (name, tag))
        print("assembling", out_mp4)
        if assemble(frames_dir, fps, out_mp4, extra_filters=extra_filters):
            print("wrote", out_mp4)
    return 0
