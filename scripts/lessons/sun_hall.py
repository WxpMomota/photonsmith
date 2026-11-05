# sun_hall.py [OUT_DIR]: "The Sun Hall" - an original domed marble rotunda lit only by the sun through an oculus, written as a pbrt-v4 scene (sun_hall.pbrt) with its
# textures (PNG) beside it - by default into pbrt_scenes/film_assets/how-light-bounces, the set of the teaching film pbrt_scenes/how-light-bounces.film. Metres, +y up, the
# hall's centre on the floor at the origin. Run again after a change here; the scene and its pictures are committed as written.
#   the floor: polished slabs of coloured stone - porphyry roundels ringed in serpentine, giallo and serpentine diamonds - and a mosaic ring round the pedestal
#   the wall: sixteen bays, each two tall panels of porphyry and serpentine in giallo frames lined in gold, an attic of lapis panels above; a cornice and a plinth
#   the columns: sixteen fluted shafts of giallo, serpentine and pavonazzetto in turn, on cream bases, under gilded capitals, carrying a pavonazzetto band
#   the dome: a hemisphere with five rings of stepped coffers - cream steps lined in gold round lapis-blue fields, a gilded rosette in each - open at the crown
#   a gold sphere on a serpentine pedestal in the middle; the sun (a distant light) and a dim sky, seen only through the oculus
# The colours are part of the lesson: bounced light carries them (the red and green panels tint the floor in front of them, the gold glows warm).
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
TITLE_ROOM = (0.0, -80.0, 0.0)   # (the closed black box the film's title card is seen in: how-light-bounces.film's first shot)


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


def marble(n, base, vein, seed, scale=6.0, m=None):
    """An n x m picture (m = n when not given) of veined marble: `base` mottled, crossed by warped veins of `vein`."""
    m = m or n
    f = fbm(max(n, m), seed)[:m, :n]
    y, x = np.mgrid[0:m, 0:n] / max(n, m)
    v = np.abs(np.sin((x + 0.6 * y) * scale * math.pi + 6.0 * f))   # veins: where the warped stripes cross zero
    veins = np.clip(1.0 - v * 6.0, 0, 1) ** 2
    c = np.array(base)[None, None, :] * (0.92 + 0.12 * f[..., None])
    return c * (1 - veins[..., None] * 0.45) + np.array(vein)[None, None, :] * veins[..., None] * 0.45


def speckled(n, base, fleck, seed, m=None, amount=0.06):
    """Porphyry or lapis: a mottled ground scattered with small flecks of `fleck` (pale crystals in porphyry, gold pyrite in lapis)."""
    m = m or n
    f = fbm(max(n, m), seed)[:m, :n]
    c = np.array(base)[None, None, :] * (0.85 + 0.25 * f[..., None])
    flecks = np.random.default_rng(seed).random((m, n)) < amount
    c[flecks] = c[flecks] * 0.4 + np.array(fleck) * 0.6
    return c


# the palette: the coloured stones of a Roman hall
STONES = {
    "cream": lambda n, m=None: marble(n, (0.88, 0.84, 0.76), (0.60, 0.56, 0.52), 21, 6.0, m),
    "porphyry": lambda n, m=None: speckled(n, (0.48, 0.09, 0.09), (0.85, 0.70, 0.66), 22, m),
    "serpentine": lambda n, m=None: marble(n, (0.10, 0.30, 0.20), (0.55, 0.75, 0.58), 23, 9.0, m),
    "giallo": lambda n, m=None: marble(n, (0.88, 0.64, 0.24), (0.99, 0.88, 0.60), 24, 5.0, m),
    "pavonazzetto": lambda n, m=None: marble(n, (0.93, 0.91, 0.88), (0.46, 0.22, 0.42), 25, 4.0, m),
    "lapis": lambda n, m=None: speckled(n, (0.08, 0.14, 0.46), (0.95, 0.80, 0.40), 26, m, 0.02),
    "grey": lambda n, m=None: marble(n, (0.44, 0.44, 0.46), (0.75, 0.75, 0.76), 27, 6.0, m),
}
GOLD_PAINT = np.array((0.90, 0.68, 0.28))


def save(name, rgb):
    Image.fromarray((np.clip(rgb, 0, 1) * 255 + 0.5).astype(np.uint8)).save(os.path.join(OUT, name))


def put(img, mask, stone):
    img[mask] = stone[mask]


def floor_picture(n=2048):
    # 8 m of floor, four 4 m slabs in a checker: porphyry roundels ringed in serpentine with a yellow eye, and diamonds of giallo and serpentine round a porphyry square,
    # each slab in a grey border with a dark joint
    S = {k: STONES[k](n) for k in ("cream", "porphyry", "serpentine", "giallo", "grey")}
    y, x = (np.mgrid[0:n, 0:n] + 0.5) / n * 2.0
    qx, qy = np.floor(x), np.floor(y)
    lx, ly = x - qx - 0.5, y - qy - 0.5
    r, box, diamond = np.hypot(lx, ly), np.maximum(np.abs(lx), np.abs(ly)), np.abs(lx) + np.abs(ly)
    a = (qx + qy) % 2 == 0
    img = S["cream"].copy()
    put(img, box > 0.46, S["grey"])
    put(img, a & (r < 0.31), S["serpentine"])
    put(img, a & (r < 0.26), S["porphyry"])
    put(img, a & (r < 0.07), S["giallo"])
    put(img, ~a & (diamond < 0.41), S["giallo"])
    put(img, ~a & (diamond < 0.34), S["serpentine"])
    put(img, ~a & (box < 0.13), S["porphyry"])
    img[box > 0.495] *= 0.5
    save("floor.png", img)


def mosaic_picture(w=4096, h=512):
    # the ring round the pedestal, laid flat (u round it, v out from the pedestal): bands of giallo, porphyry with a running wave, cream, a lapis field with sixteen
    # giallo petals, cream, serpentine, giallo - in small square tesserae with dark grout
    S = {k: STONES[k](w, h) for k in ("cream", "porphyry", "serpentine", "giallo", "lapis")}
    v, u = (np.mgrid[0:h, 0:w] + 0.5) / np.array([h, w])[:, None, None]
    img = S["cream"].copy()
    put(img, v < 0.08, S["giallo"])
    wave = 0.14 + 0.03 * np.sin(u * 2 * math.pi * 24)
    put(img, (v >= 0.08) & (v < wave + 0.06), S["porphyry"])
    field = (v >= 0.30) & (v < 0.62)
    put(img, field, S["lapis"])
    pu = (u * 16) % 1.0 - 0.5
    petal = field & (np.abs(pu) < 0.42 * np.sin(math.pi * np.clip((v - 0.30) / 0.32, 0, 1)))
    put(img, petal, S["giallo"])
    put(img, (v >= 0.70) & (v < 0.86), S["serpentine"])
    put(img, v >= 0.92, S["giallo"])
    # tesserae: about 3 cm squares (the ring's middle is about 22 m round)
    tu, tv = (u * 720) % 1.0, (v * 96) % 1.0
    grout = (np.minimum(tu, 1 - tu) < 0.08) | (np.minimum(tv, 1 - tv) < 0.08)
    img[grout] *= 0.55
    save("mosaic.png", img)


def wall_picture(w=1024, h=2304):
    # one bay of wall, 8 m wide (between two columns) and the drum's 18 m high: a grey plinth; two tall panels, porphyry and serpentine, each in a giallo frame with a gold
    # line inside it and a pavonazzetto roundel at its middle; a cream band behind the columns' entablature; an attic of four small lapis panels framed in giallo
    S = {k: STONES[k](w, h) for k in ("cream", "porphyry", "serpentine", "giallo", "pavonazzetto", "lapis", "grey")}
    y, x = (np.mgrid[h - 1:-1:-1, 0:w] + 0.5) * np.array([18.0 / h, 8.0 / w])[:, None, None]   # (metres; row 0 is the top)
    img = S["cream"].copy()
    put(img, y < 0.7, S["grey"])
    for cx, stone, other in ((2.0, "porphyry", "serpentine"), (6.0, "serpentine", "porphyry")):
        dx, dy = np.abs(x - cx), np.abs(y - 6.05)
        put(img, (dx < 1.62) & (dy < 5.05), S["giallo"])
        put(img, (dx < 1.40) & (dy < 4.83), S[stone])
        line = ((np.abs(dx - 1.30) < 0.025) & (dy < 4.73)) | ((np.abs(dy - 4.73) < 0.025) & (dx < 1.30))
        img[line] = GOLD_PAINT
        # a roundel at the panel's middle: a giallo ring round a disc of the other panel's stone, a gold eye
        rr = np.hypot(x - cx, y - 6.05)
        put(img, rr < 0.66, S["giallo"])
        img[np.abs(rr - 0.58) < 0.02] = GOLD_PAINT
        put(img, rr < 0.52, S[other])
        img[rr < 0.10] = GOLD_PAINT
    for cx in (1.0, 3.0, 5.0, 7.0):
        dx, dy = np.abs(x - cx), np.abs(y - 15.3)
        put(img, (dx < 0.78) & (dy < 1.35), S["giallo"])
        put(img, (dx < 0.68) & (dy < 1.25), S["lapis"])
    img[(np.abs(y - 13.55) < 0.04) | (np.abs(y - 17.05) < 0.04)] = GOLD_PAINT
    save("wall.png", img)


def coffer_picture(n=512):
    # one coffer (u across it, v up it): cream steps with a gold line at each step's edge, the deepest field lapis blue (the gilded rosette is a shape of its own)
    lapis, cream = STONES["lapis"](n), STONES["cream"](n)
    y, x = (np.mgrid[0:n, 0:n] + 0.5) / n
    d = np.minimum(np.minimum(x, 1 - x), np.minimum(y, 1 - y))
    img = cream.copy()
    put(img, d > 0.27, lapis)
    for edge in (0.13, 0.20, 0.27):
        img[np.abs(d - edge) < 0.006] = GOLD_PAINT
    save("coffer.png", img)


COLUMN_STONES = ("giallo", "serpentine", "pavonazzetto")


def column_pictures(n=1024, flutes=20):
    for k in COLUMN_STONES:
        save("column_%s.png" % k, STONES[k](n))
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
    # (the triangles that have no area once their points are written to 4 decimals - the middle of a disc, a lathe's axis - are left out: the loader would drop each
    # with a warning, and a film's frames are meant to load without one)
    Q = np.round(np.asarray(P, dtype=np.float64), 4)
    T = np.asarray(idx).reshape(-1, 3)
    area = np.linalg.norm(np.cross(Q[T[:, 1]] - Q[T[:, 0]], Q[T[:, 2]] - Q[T[:, 0]]), axis=1)
    idx = T[area > 1e-9].reshape(-1).tolist()
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


ROWS, COLS = 5, 28


def dome_bands():
    el_top = math.acos(OCULUS / R)
    return el_top, 0.05, el_top - 0.20   # (a plain band at the springing and round the oculus, the coffers between)


def coffer_depth(el, az):
    el_top, band_lo, band_hi = dome_bands()
    if el < band_lo or el > band_hi:
        return 0.0
    fr = (el - band_lo) / (band_hi - band_lo) * ROWS
    fc = az / (2 * math.pi) * COLS
    a, b = fr - math.floor(fr), fc - math.floor(fc)
    d = min(a, 1 - a, b, 1 - b)                   # distance in from the coffer's edge (of its own size)
    steps = min(3, int(max(0.0, d - 0.06) / 0.07))
    return 0.33 * steps * (1.0 - 0.45 * fr / ROWS)  # (shallower towards the crown)


def dome_point(el, az, r):
    return (r * math.cos(el) * math.cos(az), DRUM_H + r * math.sin(el), r * math.cos(el) * math.sin(az))


def dome():
    """The inside of a hemisphere of radius R from the drum's top to the oculus: the plain band at the springing, the five rings of coffers each stepped in three times
    (its picture one coffer a tile), and the plain band round the oculus."""
    el_top, band_lo, band_hi = dome_bands()
    sub = 14

    def part(e0, e1, rows_v, coffered):
        def f(u, v):
            az, el = 2 * math.pi * u, e0 + (e1 - e0) * v
            uv = (u * COLS, (el - band_lo) / (band_hi - band_lo) * ROWS) if coffered else (u * 8, v)
            return dome_point(el, az, R + coffer_depth(el, az)), uv
        return grid(COLS * sub, rows_v, f)

    return part(0.0, band_lo, 4, False), part(band_lo, band_hi, ROWS * sub, True), part(band_hi, el_top, 12, False)


def rosettes():
    """A gilded eight-petalled rosette at the bottom of every coffer, facing into the hall, its size the coffer's."""
    el_top, band_lo, band_hi = dome_bands()
    P, idx = [], []
    rings, spokes = 6, 48
    for row in range(ROWS):
        for col in range(COLS):
            el = band_lo + (row + 0.5) / ROWS * (band_hi - band_lo)
            az = 2 * math.pi * (col + 0.5) / COLS
            depth = coffer_depth(el, az)
            centre = np.array(dome_point(el, az, R + depth - 0.02))
            n = np.array(dome_point(el, az, 1.0)) - np.array([0.0, DRUM_H, 0.0])   # (outward)
            t1 = np.array([-math.sin(az), 0.0, math.cos(az)])
            t2 = np.cross(n, t1)
            size = 0.22 * R * math.cos(el) * 2 * math.pi / COLS * (1.0 - 0.3 * row / ROWS)   # (a little under a quarter of the coffer's width)
            base = len(P)
            P.append(centre - n * 0.30 * size)   # (the boss at its middle)
            for i in range(1, rings + 1):
                s = i / rings
                for j in range(spokes):
                    th = 2 * math.pi * j / spokes
                    rr = size * s * (0.72 + 0.28 * math.cos(8 * th))
                    lift = 0.30 * size * (1 - s * s) * (0.8 + 0.2 * math.cos(8 * th))
                    P.append(centre + (t1 * math.cos(th) + t2 * math.sin(th)) * rr - n * lift)
            for j in range(spokes):
                idx += [base, base + 1 + j, base + 1 + (j + 1) % spokes]
            for i in range(rings - 1):
                r0, r1 = base + 1 + i * spokes, base + 1 + (i + 1) * spokes
                for j in range(spokes):
                    j1 = (j + 1) % spokes
                    idx += [r0 + j, r1 + j, r1 + j1, r0 + j, r1 + j1, r0 + j1]
    return np.array(P), idx


GOLD = 'Material "conductor" "rgb eta" [ 0.143 0.374 1.442 ] "rgb k" [ 3.983 2.385 1.603 ] "float roughness" [ %.2f ]'


def main():
    floor_picture()
    mosaic_picture()
    wall_picture()
    coffer_picture()
    column_pictures()
    textures = [('floor', 'floor.png'), ('mosaic', 'mosaic.png'), ('wall', 'wall.png'), ('coffer', 'coffer.png')] + [('col_' + k, 'column_%s.png' % k) for k in COLUMN_STONES]
    for name in ('cream', 'serpentine', 'pavonazzetto'):   # (plain stone parts: the cornice, the plinth, the band, the pedestal)
        save('stone_%s.png' % name, STONES[name](512))
        textures.append(('stone_' + name, 'stone_%s.png' % name))
    out = ['# The Sun Hall - an original domed marble rotunda, lit by the sun through an oculus (written by sun_hall.py; see its header).',
           'LookAt 0 2.0 15   0 8.5 -10   0 1 0', 'Camera "perspective" "float fov" [ 70 ]',
           'Film "rgb" "integer xresolution" [ 1920 ] "integer yresolution" [ 1080 ]', 'Sampler "halton" "integer pixelsamples" [ 64 ]', 'WorldBegin',
           '# the sky (seen only through the oculus) and the sun, 58 degrees up behind the opening camera: its beam falls through the oculus low on the far wall,',
           '# behind the columns',
           'LightSource "infinite" "rgb L" [ 1.10 1.40 2.00 ]',
           'LightSource "distant" "point3 from" [ 0.25 1.6 1.0 ] "point3 to" [ 0 0 0 ] "rgb L" [ 36.0 32.0 26.0 ]']
    out += ['Texture "%s" "spectrum" "imagemap" "string filename" [ "%s" ]' % t for t in textures]
    stone = lambda name: 'Material "diffuse" "texture reflectance" "stone_%s"' % name
    # the floor: a disc of polished slabs (8 m of pattern a tile), and the mosaic ring round the pedestal just above it
    P, idx, N, UV = grid(256, 64, lambda u, v: ((R * v * math.cos(2 * math.pi * u), 0.0, R * v * math.sin(2 * math.pi * u)),
                                                  (R * v * math.cos(2 * math.pi * u) / 8.0, R * v * math.sin(2 * math.pi * u) / 8.0)))
    out += ['AttributeBegin', 'Material "coateddiffuse" "texture reflectance" "floor" "float roughness" [ 0.04 ]', mesh_text(P, idx, np.tile([0, 1, 0], (len(P), 1)), UV), 'AttributeEnd']
    r0, r1 = 1.75, 5.5
    P, idx, N, UV = grid(512, 8, lambda u, v: (((r0 + (r1 - r0) * v) * math.cos(2 * math.pi * u), 0.004, (r0 + (r1 - r0) * v) * math.sin(2 * math.pi * u)), (u, 1.0 - v)))
    out += ['AttributeBegin', 'Material "coateddiffuse" "texture reflectance" "mosaic" "float roughness" [ 0.08 ]', mesh_text(P, idx, np.tile([0, 1, 0], (len(P), 1)), UV), 'AttributeEnd']
    # the drum: the wall (u round it, one 8 m bay a tile, sixteen bays round, one picture up its height), a cornice where the dome springs, a plinth at its foot
    P, idx, N, UV = grid(512, 36, lambda u, v: ((R * math.cos(2 * math.pi * u), DRUM_H * v, R * math.sin(2 * math.pi * u)), (COL_N * u, v)))
    out += ['AttributeBegin', 'Material "diffuse" "texture reflectance" "wall"', mesh_text(P, idx, -np.array([[p[0], 0, p[2]] for p in P]) / R, UV), 'AttributeEnd']
    cornice = lathe([(R + 0.01, DRUM_H - 1.2), (R - 0.35, DRUM_H - 1.1), (R - 0.45, DRUM_H - 0.6), (R - 0.9, DRUM_H - 0.35), (R - 0.9, DRUM_H), (R + 0.01, DRUM_H)], 400, uscale=32)
    plinth = lathe([(R + 0.01, 0.0), (R - 0.5, 0.0), (R - 0.5, 0.5), (R - 0.35, 0.7), (R + 0.01, 0.7)], 400, uscale=32)
    out += ['AttributeBegin', stone('cream'), mesh_text(*cornice), mesh_text(*plinth), 'AttributeEnd']
    # the columns - shafts of giallo, serpentine and pavonazzetto in turn, fluted, on cream bases, under gilded capitals - and the band they carry
    for k in range(COL_N):
        a = 2 * math.pi * k / COL_N   # (on the bays' edges, where the wall's pilasters are)
        base, body, top = column(COL_R * math.cos(a), COL_R * math.sin(a))
        kind = COLUMN_STONES[k % len(COLUMN_STONES)]
        out += ['AttributeBegin', stone('cream'), mesh_text(*base), GOLD % 0.28, mesh_text(*top),
                'Material "diffuse" "texture reflectance" "col_%s" "string normalmap" [ "column_flutes.png" ]' % kind, mesh_text(*body), 'AttributeEnd']
    band = lathe([(COL_R + 1.25, COL_H), (COL_R - 1.25, COL_H), (COL_R - 1.25, COL_H + 1.0), (COL_R - 1.4, COL_H + 1.15), (COL_R - 1.4, COL_H + 1.45),
                  (COL_R + 1.4, COL_H + 1.45), (COL_R + 1.4, COL_H + 1.15), (COL_R + 1.25, COL_H + 1.0), (COL_R + 1.25, COL_H)], 400, uscale=32)
    out += ['AttributeBegin', stone('pavonazzetto'), mesh_text(*band), 'AttributeEnd']
    # the dome: cream bands, the coffers (cream steps lined in gold round lapis fields), a gilded rosette in each
    lower, coffers, upper = dome()
    out += ['AttributeBegin', stone('cream'), mesh_text(*lower), mesh_text(*upper), 'Material "diffuse" "texture reflectance" "coffer"', mesh_text(*coffers), 'AttributeEnd']
    out += ['AttributeBegin', GOLD % 0.22, mesh_text(*rosettes()), 'AttributeEnd']
    el_top = math.acos(OCULUS / R)
    yt = DRUM_H + R * math.sin(el_top)
    rim = lathe([(OCULUS, yt), (OCULUS - 0.05, yt + 0.9), (OCULUS + 1.2, yt + 0.9)], 160)
    out += ['AttributeBegin', 'Material "diffuse" "rgb reflectance" [ 0.62 0.58 0.52 ]', mesh_text(*rim), 'AttributeEnd']
    # the dome's and the drum's outsides: the stone is solid (a single shell let the sun through where the coffers' steps tilt its shading normals)
    shell = [(R + 1.6, 0.0), (R + 1.6, DRUM_H)] + [((R + 1.6) * math.cos(e), DRUM_H + (R + 1.6) * math.sin(e)) for e in np.linspace(0.0, el_top, 40)[1:]]
    shell += [(OCULUS + 1.2, yt + 0.9)]
    out += ['AttributeBegin', 'Material "diffuse" "rgb reflectance" [ 0.55 0.52 0.48 ]', mesh_text(*lathe(shell, 200)), 'AttributeEnd']
    # the gold sphere on its pedestal
    ped = lathe([(0.0, 0.0), (1.6, 0.0), (1.6, 0.3), (1.25, 0.45), (1.0, 0.55), (0.9, 1.6), (1.15, 1.75), (1.15, 1.95), (0.0, 1.95)], 96, uscale=3)
    out += ['AttributeBegin', 'Material "coateddiffuse" "texture reflectance" "stone_serpentine" "float roughness" [ 0.06 ]', mesh_text(*ped), 'AttributeEnd']
    out += ['AttributeBegin', GOLD % 0.06, 'Translate 0 3.35 0', 'Shape "sphere" "float radius" [ 1.4 ]', 'AttributeEnd']
    # the title room: a small closed black box far below the hall, where the film's opening camera sits, so its title card is over black (and costs next to nothing)
    c, h = np.array(TITLE_ROOM), 2.0
    corners = np.array([[x, y, z] for x in (-h, h) for y in (-h, h) for z in (-h, h)]) + c
    faces = [(0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)]
    box_idx = sum(([a, b, d, b, c2, d] for a, b, c2, d in faces), [])
    out += ['AttributeBegin', 'Material "diffuse" "rgb reflectance" [ 0.02 0.02 0.02 ]', mesh_text(corners, box_idx), 'AttributeEnd']
    open(os.path.join(OUT, "sun_hall.pbrt"), "w").write("\n".join(out) + "\n")
    print("wrote", os.path.join(OUT, "sun_hall.pbrt"))


if __name__ == "__main__":
    main()
