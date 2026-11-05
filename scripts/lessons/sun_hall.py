# sun_hall.py [OUT_DIR]: "The Sun Hall" - an original domed marble rotunda lit only by the sun through an oculus, written as a pbrt-v4 scene (sun_hall.pbrt) with its
# textures (PNG) beside it - by default into pbrt_scenes/film_assets/how-light-bounces, the set of the teaching film pbrt_scenes/how-light-bounces.film. Metres, +y up, the
# hall's centre on the floor at the origin. Run again after a change here; the scene and its pictures are committed as written.
#   the floor: a disc of polished marble slabs with porphyry roundels (coated diffuse, a picture)
#   the wall: a drum of cream marble with red porphyry panels, a cornice, a ring of 16 fluted columns carrying a stone band
#   the dome: a hemisphere with five rings of stepped coffers, open at the crown (the oculus)
#   a gold sphere on a pedestal in the middle; the sun (a distant light) and a dim sky, seen only through the oculus
import math, os, sys
import numpy as np
from PIL import Image

OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "pbrt_scenes", "film_assets", "how-light-bounces")
os.makedirs(OUT, exist_ok=True)
rng = np.random.default_rng(7)

R = 20.0            # the drum's inner radius
DRUM_H = 18.0       # the drum's height: the dome springs from here
OCULUS = 5.0        # the oculus's radius
COL_R, COL_N, COL_H, COL_RAD = 16.8, 16, 12.0, 0.62


# ---- pictures ------------------------------------------------------------------------------------------------------------------------------------------------
def value_noise(n, cells, seed):
    g = np.random.default_rng(seed).random((cells + 1, cells + 1))
    g[-1, :] = g[0, :]
    g[:, -1] = g[:, 0]
    img = Image.fromarray((g * 255).astype(np.uint8)).resize((n, n), Image.BICUBIC)
    return np.asarray(img, dtype=np.float64) / 255.0


def fbm(n, seed, octaves=(4, 8, 16, 32, 64)):
    out, amp, tot = np.zeros((n, n)), 1.0, 0.0
    for i, c in enumerate(octaves):
        out += amp * value_noise(n, c, seed + i)
        tot += amp
        amp *= 0.55
    return out / tot


def marble(n, base, vein, seed, scale=6.0):
    f = fbm(n, seed)
    y, x = np.mgrid[0:n, 0:n] / n
    v = np.abs(np.sin((x + 0.6 * y) * scale * math.pi + 6.0 * f))   # veins: where the warped stripes cross zero
    veins = np.clip(1.0 - v * 6.0, 0, 1) ** 2
    c = np.array(base)[None, None, :] * (0.92 + 0.12 * f[..., None])
    return c * (1 - veins[..., None] * 0.45) + np.array(vein)[None, None, :] * veins[..., None] * 0.45


def save(name, rgb):
    Image.fromarray((np.clip(rgb, 0, 1) * 255 + 0.5).astype(np.uint8)).save(os.path.join(OUT, name))


def floor_picture(n=2048):
    # one 4 m slab pattern: a cream square with a grey border, a porphyry roundel ringed in green serpentine
    cream = marble(n, (0.86, 0.82, 0.74), (0.55, 0.52, 0.50), 11)
    red = marble(n, (0.52, 0.13, 0.10), (0.75, 0.55, 0.50), 12, 9.0)
    green = marble(n, (0.18, 0.32, 0.24), (0.60, 0.70, 0.62), 13, 9.0)
    grey = marble(n, (0.45, 0.45, 0.46), (0.75, 0.75, 0.76), 14)
    y, x = (np.mgrid[0:n, 0:n] + 0.5) / n - 0.5
    r = np.hypot(x, y)
    img = cream.copy()
    border = np.maximum(np.abs(x), np.abs(y)) > 0.46
    img[border] = grey[border]
    img[r < 0.30] = green[r < 0.30]
    img[r < 0.26] = red[r < 0.26]
    joint = (np.maximum(np.abs(x), np.abs(y)) > 0.497)
    img[joint] *= 0.55
    save("floor.png", img)


def wall_picture(n=2048):
    # 4 m of wall: cream marble with a red porphyry panel framed in a thin grey band
    cream = marble(n, (0.88, 0.84, 0.76), (0.60, 0.56, 0.52), 21)
    red = marble(n, (0.56, 0.14, 0.11), (0.80, 0.60, 0.55), 22, 9.0)
    grey = marble(n, (0.50, 0.50, 0.50), (0.80, 0.80, 0.80), 23)
    y, x = (np.mgrid[0:n, 0:n] + 0.5) / n
    img = cream.copy()
    frame = (np.abs(x - 0.5) < 0.34) & (np.abs(y - 0.5) < 0.40)
    panel = (np.abs(x - 0.5) < 0.31) & (np.abs(y - 0.5) < 0.37)
    img[frame] = grey[frame]
    img[panel] = red[panel]
    save("wall.png", img)


def column_pictures(n=1024, flutes=20):
    save("column.png", marble(n, (0.90, 0.87, 0.80), (0.62, 0.60, 0.56), 31, 3.0))
    # the flutes as a tangent-space normal map: x round the column (u), z out of it
    u = (np.arange(n) + 0.5) / n
    phase = (u * flutes) % 1.0
    # each flute a shallow concave channel with a flat fillet between: the slope of a circular groove
    t = (phase - 0.5) / 0.42
    slope = np.where(np.abs(t) < 1.0, -t / np.sqrt(np.maximum(1e-4, 1.0 - t * t)) * 0.55, 0.0)
    nx = -slope / np.sqrt(1 + slope * slope)
    nz = 1.0 / np.sqrt(1 + slope * slope)
    img = np.zeros((n, n, 3))
    img[..., 0] = (0.5 + 0.5 * nx)[None, :]
    img[..., 1] = 0.5
    img[..., 2] = (0.5 + 0.5 * nz)[None, :]
    save("column_flutes.png", img)


# ---- meshes --------------------------------------------------------------------------------------------------------------------------------------------------
def mesh_text(P, idx, N=None, UV=None):
    s = 'Shape "trianglemesh"\n  "point3 P" [ ' + " ".join("%.4f %.4f %.4f" % tuple(p) for p in P) + " ]\n"
    if N is not None:
        s += '  "normal N" [ ' + " ".join("%.4f %.4f %.4f" % tuple(q) for q in N) + " ]\n"
    if UV is not None:
        s += '  "point2 uv" [ ' + " ".join("%.4f %.4f" % tuple(q) for q in UV) + " ]\n"
    return s + '  "integer indices" [ ' + " ".join(str(i) for i in idx) + " ]\n"


def grid(nu, nv, f):
    """A (nu+1) x (nv+1) grid of points f(i/nu, j/nv) -> (p, uv), its triangles and smooth normals."""
    P, UV = [], []
    for j in range(nv + 1):
        for i in range(nu + 1):
            p, uv = f(i / nu, j / nv)
            P.append(p)
            UV.append(uv)
    P = np.array(P)
    idx = []
    for j in range(nv):
        for i in range(nu):
            a = j * (nu + 1) + i
            b, c, d = a + 1, a + nu + 1, a + nu + 2
            idx += [a, b, d, a, d, c]
    N = np.zeros_like(P)
    T = np.array(idx).reshape(-1, 3)
    fn = np.cross(P[T[:, 1]] - P[T[:, 0]], P[T[:, 2]] - P[T[:, 0]])
    for k in range(3):
        np.add.at(N, T[:, k], fn)
    N /= np.maximum(1e-12, np.linalg.norm(N, axis=1))[:, None]
    return P, idx, N, np.array(UV)


def lathe(profile, segs, at=(0, 0, 0), uscale=1.0, vscale=1.0):
    """A surface of revolution about +y: profile [(radius, y)], u round it (0..uscale), v up the profile's length (0..vscale)."""
    lens = [0.0]
    for a, b in zip(profile, profile[1:]):
        lens.append(lens[-1] + math.hypot(b[0] - a[0], b[1] - a[1]))
    total = lens[-1] or 1.0

    def f(u, v):
        k = min(len(profile) - 2, int(v * (len(profile) - 1)))
        t = v * (len(profile) - 1) - k
        r = profile[k][0] + (profile[k + 1][0] - profile[k][0]) * t
        y = profile[k][1] + (profile[k + 1][1] - profile[k][1]) * t
        a = 2 * math.pi * u
        s = lens[k] + (lens[k + 1] - lens[k]) * t
        return (at[0] + r * math.cos(a), at[1] + y, at[2] + r * math.sin(a)), (u * uscale, s / total * vscale)

    return grid(segs, len(profile) - 1, f)


def column(x, z):
    prof = [(0.0, 0.0), (0.95, 0.0), (0.95, 0.25), (0.82, 0.32), (0.86, 0.45), (0.70, 0.55), (COL_RAD, 0.62)]
    # the shaft, a little swollen a third of the way up (entasis), then the capital: a flared bell and a square-ish abacus as a broad disc
    shaft = [(COL_RAD * (1.0 + 0.04 * math.sin(math.pi * min(1, (y - 0.62) / (COL_H - 2.0)) ** 0.7)) * (1 - 0.08 * (y - 0.62) / (COL_H - 2.0)), y)
             for y in np.linspace(0.62, COL_H - 1.4, 24)]
    cap = [(COL_RAD * 0.92, COL_H - 1.3), (COL_RAD * 1.05, COL_H - 1.15), (COL_RAD * 1.45, COL_H - 0.55), (1.15, COL_H - 0.45), (1.15, COL_H), (0.0, COL_H)]
    base = lathe(prof, 48, (x, 0, z))
    body = lathe(shaft, 64, (x, 0, z), 1.0, 4.0)
    top = lathe(cap, 48, (x, 0, z))
    return base, body, top


def dome():
    # the inside of a hemisphere of radius R from the drum's top to the oculus, five rings of coffers each stepped in three times
    el_top = math.acos(OCULUS / R)
    rows, cols, sub = 5, 28, 14
    band_lo, band_hi = 0.05, el_top - 0.20           # (a plain band at the springing and round the oculus)

    def depth(el, az):
        if el < band_lo or el > band_hi:
            return 0.0
        fr = (el - band_lo) / (band_hi - band_lo) * rows
        fc = az / (2 * math.pi) * cols
        a, b = fr - math.floor(fr), fc - math.floor(fc)
        d = min(a, 1 - a, b, 1 - b)                   # distance in from the coffer's edge (of its own size)
        steps = min(3, int(max(0.0, d - 0.06) / 0.07))
        return 0.33 * steps * (1.0 - 0.45 * fr / rows)  # (shallower towards the crown)

    def f(u, v):
        az, el = 2 * math.pi * u, el_top * v
        r = R + depth(el, az)
        return (r * math.cos(el) * math.cos(az), DRUM_H + r * math.sin(el), r * math.cos(el) * math.sin(az)), (u * 8, v * 4)

    return grid(cols * sub, rows * sub + 30, f)


def main():
    floor_picture()
    wall_picture()
    column_pictures()
    out = ['# The Sun Hall - an original domed marble rotunda, lit by the sun through an oculus (written by sun_hall.py; see its header).',
           'LookAt 0 2.0 15   0 8.5 -10   0 1 0', 'Camera "perspective" "float fov" [ 70 ]',
           'Film "rgb" "integer xresolution" [ 1920 ] "integer yresolution" [ 1080 ]', 'Sampler "halton" "integer pixelsamples" [ 64 ]', 'WorldBegin',
           '# the sky (seen only through the oculus) and the sun, 58 degrees up behind the opening camera: its beam falls through the oculus low on the far wall,',
           '# behind the columns',
           'LightSource "infinite" "rgb L" [ 1.10 1.40 2.00 ]',
           'LightSource "distant" "point3 from" [ 0.25 1.6 1.0 ] "point3 to" [ 0 0 0 ] "rgb L" [ 36.0 32.0 26.0 ]',
           'Texture "floor" "spectrum" "imagemap" "string filename" [ "floor.png" ]',
           'Texture "wall" "spectrum" "imagemap" "string filename" [ "wall.png" ]',
           'Texture "colm" "spectrum" "imagemap" "string filename" [ "column.png" ]']
    # the floor: a disc, slabs 4 m square
    P, idx, N, UV = grid(256, 64, lambda u, v: ((R * v * math.cos(2 * math.pi * u), 0.0, R * v * math.sin(2 * math.pi * u)),
                                                  (R * v * math.cos(2 * math.pi * u) / 4.0, R * v * math.sin(2 * math.pi * u) / 4.0)))
    out += ['AttributeBegin', 'Material "coateddiffuse" "texture reflectance" "floor" "float roughness" [ 0.04 ]', mesh_text(P, idx, np.tile([0, 1, 0], (len(P), 1)), UV), 'AttributeEnd']
    # the drum: the wall (u round it, 4 m panels), a cornice where the dome springs, a plinth at its foot
    P, idx, N, UV = grid(400, 18, lambda u, v: ((R * math.cos(2 * math.pi * u), DRUM_H * v, R * math.sin(2 * math.pi * u)), (2 * math.pi * R * u / 4.0, DRUM_H * v / 4.0)))
    out += ['AttributeBegin', 'Material "diffuse" "texture reflectance" "wall"', mesh_text(P, idx, -np.array([[p[0], 0, p[2]] for p in P]) / R, UV), 'AttributeEnd']
    cornice = lathe([(R + 0.01, DRUM_H - 1.2), (R - 0.35, DRUM_H - 1.1), (R - 0.45, DRUM_H - 0.6), (R - 0.9, DRUM_H - 0.35), (R - 0.9, DRUM_H), (R + 0.01, DRUM_H)], 400)
    plinth = lathe([(R + 0.01, 0.0), (R - 0.5, 0.0), (R - 0.5, 0.5), (R - 0.35, 0.7), (R + 0.01, 0.7)], 400)
    stone = 'Material "diffuse" "rgb reflectance" [ 0.80 0.76 0.68 ]'
    out += ['AttributeBegin', stone, mesh_text(*cornice), mesh_text(*plinth), 'AttributeEnd']
    # the columns and the band they carry
    flutes = 'Material "diffuse" "texture reflectance" "colm" "string normalmap" [ "column_flutes.png" ]'
    plain = 'Material "diffuse" "texture reflectance" "colm"'
    for k in range(COL_N):
        a = 2 * math.pi * (k + 0.5) / COL_N
        base, body, top = column(COL_R * math.cos(a), COL_R * math.sin(a))
        out += ['AttributeBegin', plain, mesh_text(*base), mesh_text(*top), flutes, mesh_text(*body), 'AttributeEnd']
    band = lathe([(COL_R + 1.25, COL_H), (COL_R - 1.25, COL_H), (COL_R - 1.25, COL_H + 1.0), (COL_R - 1.4, COL_H + 1.15), (COL_R - 1.4, COL_H + 1.45),
                  (COL_R + 1.4, COL_H + 1.45), (COL_R + 1.4, COL_H + 1.15), (COL_R + 1.25, COL_H + 1.0), (COL_R + 1.25, COL_H)], 400)
    out += ['AttributeBegin', stone, mesh_text(*band), 'AttributeEnd']
    # the dome and the oculus's rim
    out += ['AttributeBegin', 'Material "diffuse" "rgb reflectance" [ 0.82 0.79 0.72 ]', mesh_text(*dome()), 'AttributeEnd']
    el_top = math.acos(OCULUS / R)
    yt = DRUM_H + R * math.sin(el_top)
    rim = lathe([(OCULUS, yt), (OCULUS - 0.05, yt + 0.9), (OCULUS + 1.2, yt + 0.9)], 160)
    out += ['AttributeBegin', 'Material "diffuse" "rgb reflectance" [ 0.62 0.58 0.52 ]', mesh_text(*rim), 'AttributeEnd']
    # the dome's and the drum's outsides: the stone is solid (a single shell let the sun through where the coffers' steps tilt its shading normals)
    shell = [(R + 1.6, 0.0), (R + 1.6, DRUM_H)] + [((R + 1.6) * math.cos(e), DRUM_H + (R + 1.6) * math.sin(e)) for e in np.linspace(0.0, el_top, 40)[1:]]
    shell += [(OCULUS + 1.2, yt + 0.9)]
    out += ['AttributeBegin', 'Material "diffuse" "rgb reflectance" [ 0.55 0.52 0.48 ]', mesh_text(*lathe(shell, 200)), 'AttributeEnd']
    # the gold sphere on its pedestal
    ped = lathe([(0.0, 0.0), (1.6, 0.0), (1.6, 0.3), (1.25, 0.45), (1.0, 0.55), (0.9, 1.6), (1.15, 1.75), (1.15, 1.95), (0.0, 1.95)], 96)
    out += ['AttributeBegin', 'Material "diffuse" "rgb reflectance" [ 0.18 0.20 0.17 ]', mesh_text(*ped), 'AttributeEnd']
    out += ['AttributeBegin', 'Material "conductor" "rgb eta" [ 0.143 0.374 1.442 ] "rgb k" [ 3.983 2.385 1.603 ] "float roughness" [ 0.06 ]',
            'Translate 0 3.35 0', 'Shape "sphere" "float radius" [ 1.4 ]', 'AttributeEnd']
    open(os.path.join(OUT, "sun_hall.pbrt"), "w").write("\n".join(out) + "\n")
    print("wrote", os.path.join(OUT, "sun_hall.pbrt"))


if __name__ == "__main__":
    main()
