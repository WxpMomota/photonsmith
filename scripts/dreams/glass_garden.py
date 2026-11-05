# glass_garden.py: "The Glass Garden" - a 60 s dream showcase film: a mossy hollow at sunset where giant flowers of coloured glass grow round a still pond, the
# low sun shining through their petals and stems (dispersion, coloured glass, light shafts in a thin mist); the sun sets, the sky fades through its afterglow, and the
# garden wakes into a night lit by its own blossoms - an orb of light in every cup - with fireflies, glowing fungi, rising pollen and petals drifting down onto the pond.
# Writes the set (pbrt_scenes/film_assets/the-glass-garden/: garden.pbrt, its PLY meshes and pictures), the film (pbrt_scenes/the-glass-garden.film) and its score
# (film_assets/the-glass-garden/cues.txt). Everything is made from fixed seeds: running it again writes the same files.
#   python scripts/dreams/glass_garden.py
import math, os, sys
import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dream_lib import Set, Sheet, REPO, fbm, hash01, grid, merge, lathe, tube, bezier, transform, glass_medium

KEY = "the-glass-garden"
S = Set(KEY)
rng = np.random.default_rng(11)

# ---- the land ----------------------------------------------------------------------------------------------------------------------------------------------
POND = (1.5, -5.5)   # the pond's middle (x, z) and radius; its water's level
POND_R = 5.2
WATER_Y = -0.10
_hills = fbm(512, 301, octaves=(3, 6, 12, 24), m=8)[4]   # (a row of noise round the horizon: the hills' line)
_ridges = fbm(2048, 307, octaves=(5, 11, 23, 47, 95, 190), m=8, gain=0.62)[4]   # (and the far mountains' crests)


def smooth(e0, e1, x):
    t = min(1.0, max(0.0, (x - e0) / (e1 - e0)))
    return t * t * (3 - 2 * t)


def ground(x, z):
    """The land's height at (x, z): a flat floor of moss in the middle of the hollow, rising to its rim and to hills beyond, a pond sunk in the floor."""
    r = math.hypot(x, z)
    a = (math.atan2(z, x) / (2 * math.pi)) % 1.0
    h = 0.05 * (math.sin(x * 1.3 + 0.4) * math.sin(z * 1.1) + 0.5 * math.sin(x * 3.1) * math.sin(z * 2.7 + 1.0))
    # (the hollow opens to the west-north-west, where the sun sets: there its rim and the near hills fall away to a long view, so the low sun reaches the garden
    # until it sets; the far ridges stay, a silhouette under the sunset)
    off = abs((math.degrees(math.atan2(z, x)) - 150.0 + 180.0) % 360.0 - 180.0)
    opening = smooth(75.0, 25.0, off)
    h += 1.8 * smooth(14, 40, r) * (1 - 0.8 * opening) + 7.0 * smooth(35, 95, r) * (1 - 0.95 * opening)
    hill = _hills[int(a * 511)]
    h += smooth(80, 420, r) * (26.0 + 70.0 * hill) * (1 - 0.9 * opening) + max(0.0, r - 420) * 0.04 * (1 - 0.8 * opening)
    ridge = 1.0 - abs(2.0 * _ridges[int(a * 2047)] - 1.0)   # (ridged noise round the horizon: sharp crests)
    h += smooth(900, 2200, r) * (40.0 + 170.0 * ridge ** 2) * (1 - 0.45 * opening)
    d = math.hypot(x - POND[0], z - POND[1])
    if d < POND_R * 1.2:
        h -= 0.75 * max(0.0, 1.0 - (d / POND_R) ** 2) + 0.06 * smooth(POND_R * 1.2, POND_R * 0.9, d)
    return h


def moss_pictures():
    """The moss (6 m of it a tile): deep emerald and olive cushions, paler new growth on their tops, dark hollows between, a few rust-red sporophyte specks; and its
    normal map (the cushions' relief)."""
    n = 2048
    big, mid, fine = fbm(n, 11, (4, 8, 16)), fbm(n, 12, (32, 64)), fbm(n, 13, (128, 256, 512))
    cushions = np.clip((mid - 0.35) * 2.2, 0, 1)
    h = 0.55 * cushions + 0.30 * fine + 0.15 * big
    deep, olive, pale = np.array([0.08, 0.24, 0.07]), np.array([0.30, 0.38, 0.10]), np.array([0.58, 0.68, 0.22])
    c = deep[None, None] * (1 - big[..., None]) + olive[None, None] * big[..., None]
    c = c * (1 - 0.6 * (h[..., None] ** 2)) + pale[None, None] * 0.6 * (h[..., None] ** 2)
    c *= (0.55 + 0.6 * h[..., None])
    specks = np.random.default_rng(14).random((n, n)) < 0.0012
    c[specks] = np.array([0.45, 0.16, 0.06])
    S.save("moss.png", c)
    h = np.asarray(Image.fromarray((h * 255).astype(np.uint8)).resize((1024, 1024), Image.BICUBIC), dtype=np.float64) / 255.0   # (the relief at half the colour's size)
    gy, gx = np.gradient(h * 0.018)   # (about 1.8 cm of relief across the picture's 6 m: steep, small cushions)
    k = 1024 / 6.0
    N = np.dstack([-gx * k, gy * k, np.ones_like(h)])
    N /= np.linalg.norm(N, axis=2, keepdims=True)
    S.save("moss_n.png", N * 0.5 + 0.5)


def hills_picture():
    """The rim and the hills (u round the hollow, v from the rim up to the far crests): grass and dark woods near, fading to blue-grey towards the crests."""
    w, hgt = 2048, 256
    f = fbm(w, 21, (8, 16, 32, 64), m=hgt)
    v = np.linspace(0, 1, hgt)[:, None]
    near, wood, far = np.array([0.16, 0.24, 0.07]), np.array([0.05, 0.11, 0.05]), np.array([0.30, 0.36, 0.42])
    woods = np.clip((f - 0.45) * 4.0, 0, 1)[..., None]
    c = near * (1 - woods) + wood * woods
    c = c * (1 - v[..., None] ** 0.7) + far * (v[..., None] ** 0.7)
    S.save("hills.png", c * (0.8 + 0.4 * f[..., None]))


def mud_picture():
    f = fbm(512, 31, (8, 16, 32, 64))
    c = np.array([0.06, 0.05, 0.035])[None, None] * (0.7 + 0.6 * f[..., None])
    S.save("mud.png", c)


def land():
    out = []
    # the floor of the hollow: rings round the middle out to 72 m (fine in the middle, where the camera is)
    def floor(u, v):
        r = 72.0 * v ** 1.6
        a = 2 * math.pi * u
        x, z = r * math.cos(a), r * math.sin(a)
        return (x, ground(x, z), z), (x / 6.0, z / 6.0)
    P, idx, N, UV = grid(440, 170, floor)
    mud = np.array([math.hypot(p[0] - POND[0], p[2] - POND[1]) < POND_R * 0.95 for p in P])
    T = np.array(idx).reshape(-1, 3)
    wet = mud[T].all(axis=1)
    out += ['AttributeBegin', 'Material "diffuse" "texture reflectance" "moss" "string normalmap" [ "moss_n.png" ]',
            S.mesh(P, T[~wet].ravel(), N, UV), 'Material "coateddiffuse" "texture reflectance" "mud" "float roughness" [ 0.3 ]', S.mesh(P, T[wet].ravel(), N, UV), 'AttributeEnd']
    # the rim and the hills beyond, out to 2.5 km
    def hills(u, v):
        r = 72.0 + (2500.0 - 72.0) * v ** 2.2
        a = 2 * math.pi * u
        x, z = r * math.cos(a), r * math.sin(a)
        return (x, ground(x, z), z), (u * 6.0, min(1.0, (ground(x, z) - 6.0) / 330.0))
    P, idx, N, UV = grid(480, 60, hills)
    out += ['AttributeBegin', 'Material "diffuse" "texture reflectance" "hills"', S.mesh(P, idx, N, UV), 'AttributeEnd']
    # the pond's water: a still sheet (glass of water's index), its outside the mist above it
    out += ['AttributeBegin', 'Material "dielectric" "float eta" [ 1.333 ]']
    P, idx, N, UV = grid(160, 24, lambda u, v: ((POND[0] + POND_R * 0.97 * v * math.cos(2 * math.pi * u), WATER_Y, POND[1] + POND_R * 0.97 * v * math.sin(2 * math.pi * u)), (u, v)))
    out += [S.mesh(P, idx, np.tile([0.0, 1.0, 0.0], (len(P), 1)), UV), 'AttributeEnd']
    return out


# ---- glass -------------------------------------------------------------------------------------------------------------------------------------------------
TINTS = {   # the colour each glass lets through, and how deep the colour gets per metre
    "rose": ((1.00, 0.30, 0.48), 5.0), "amber": ((1.00, 0.62, 0.16), 5.0), "violet": ((0.58, 0.34, 1.00), 5.0), "aqua": ((0.25, 0.95, 0.85), 4.0),
    "gold": ((1.00, 0.84, 0.30), 4.0), "ice": ((0.62, 0.86, 1.00), 3.0), "leaf": ((0.45, 0.95, 0.40), 3.5), "amethyst": ((0.78, 0.55, 1.00), 1.2),
}
GLOW = {"rose": (1.0, 0.55, 0.62), "amber": (1.0, 0.68, 0.35), "violet": (0.75, 0.6, 1.0), "aqua": (0.5, 1.0, 0.9), "gold": (1.0, 0.85, 0.5), "ice": (0.7, 0.88, 1.0)}
CLEAR = 'Material "dielectric" "float eta" [ 1.52 ] "float abbenumber" [ 26 ]'   # (a flint glass: its colours part where light bends through it)


def tinted(name):
    """A coloured glass: its colour is a filter on the light it lets through at each crossing (`rgb tf`, as an .mtl's Tf: the GPU keeps no medium inside a mesh),
    so a petal's two faces together give about its tint."""
    c, d = TINTS[name]
    tf = [x ** 0.55 for x in c]
    return ['Material "dielectric" "float eta" [ 1.5 ] "rgb tf" [ %.3f %.3f %.3f ]' % tuple(tf)]


def blade(length, width, cup, curl, thick, nu=14, nv=28, tip=0.7):
    """A closed thin solid: a petal or a leaf along +y from its foot at the origin, cupped across (towards +z), curled forward along its length, thickest along its middle
    and meeting itself at its edges."""
    def surface(u, v, side):
        x = (2 * u - 1)
        w = width * (math.sin(math.pi * min(1.0, v * 1.02)) ** tip) * (1.0 - 0.25 * v) + 0.002
        half = thick * (1 - x * x) * math.sin(math.pi * v) ** 0.5
        px = x * w
        py = length * v
        pz = cup * x * x * w + curl * length * v * v
        # (the surface's normal is about +z, tilted by the cup and the curl; offsetting along z is close enough for a thin blade)
        return (px, py, pz + side * half), (u, v)
    top = grid(nu, nv, lambda u, v: surface(u, v, 1.0))
    bottom = grid(nu, nv, lambda u, v: surface(1.0 - u, v, -1.0))
    return merge(top, bottom)


def rotate_to(m, up):
    """A mesh made along +y turned so that +y points along `up`."""
    up = np.asarray(up, dtype=np.float64) / np.linalg.norm(up)
    y = np.array([0.0, 1.0, 0.0])
    v = np.cross(y, up)
    s, c = np.linalg.norm(v), float(np.dot(y, up))
    if s < 1e-9:
        R = np.eye(3) if c > 0 else np.diag([1.0, -1.0, -1.0])
    else:
        vx = np.array([[0, -v[2], v[1]], [v[2], 0, -v[0]], [-v[1], v[0], 0]])
        R = np.eye(3) + vx + vx @ vx * ((1 - c) / (s * s))
    P, idx, N, UV = m
    return np.asarray(P) @ R.T, idx, np.asarray(N) @ R.T, UV


def moved(m, at):
    return np.asarray(m[0]) + np.asarray(at), m[1], m[2], m[3]


def frame(axis):
    """Two directions across `axis`."""
    axis = np.asarray(axis, dtype=np.float64) / np.linalg.norm(axis)
    a = np.cross(axis, [0.0, 0.0, 1.0] if abs(axis[1]) > 0.5 else [0.0, 1.0, 0.0])
    a /= np.linalg.norm(a)
    return a, np.cross(axis, a)


FLOWERS = []   # (name, base, top, bloom radius, tint, glow time) for the film's lines


def flower(name, x, z, height, radius, tint, petals, opening, lean, seed, glow_at, orb_too=True, y0=None):
    """A glass flower: a clear stem curving up from the moss, two tinted leaves, a cup of tinted petals round a clear pistil, an orb of light hovering in the cup
    (dark until `glow_at`: a block of its own, `glow_NAME`; none when not `orb_too`, for a flower made once and placed many times). Returns its scene lines."""
    r = np.random.default_rng(seed)
    y0 = ground(x, z) if y0 is None else y0
    base = np.array([x, y0 - 0.05, z])
    la = r.uniform(0, 2 * math.pi)
    top = base + np.array([lean * math.cos(la), height, lean * math.sin(la)])
    p1 = base + np.array([0.0, height * 0.4, 0.0])
    p2 = top - np.array([lean * 0.3 * math.cos(la), height * 0.32, lean * 0.3 * math.sin(la)])
    stem_pts = bezier(base, p1, p2, top, 36)
    sr = 0.035 + 0.012 * height
    radii = [sr * (1.15 - 0.4 * t) for t in np.linspace(0, 1, 36)]
    radii[-1] = sr * 0.5
    out = ['AttributeBegin', CLEAR, S.mesh(*tube(stem_pts, radii, 14 if height > 2 else 8)), 'AttributeEnd']
    axis = stem_pts[-1] - stem_pts[-4]
    axis = axis / np.linalg.norm(axis)
    # the leaves: tinted green blades from low on the stem, arching out
    leaves = []
    for k in range(2):
        t = 0.16 + 0.18 * k + 0.05 * r.random()
        at = stem_pts[int(t * 35)]
        yaw = la + math.pi * (0.6 + k) + r.uniform(-0.4, 0.4)
        out_dir = np.array([math.cos(yaw), 0.0, math.sin(yaw)])
        b = blade(height * 0.42, height * 0.045, 0.5, 0.55, 0.010 + 0.002 * height, 8 if height > 2 else 5, 18 if height > 2 else 10, 0.6)
        b = rotate_to(b, out_dir * 0.75 + np.array([0.0, 1.0, 0.0]))
        leaves.append(moved(b, at))
    out += ['AttributeBegin'] + tinted("leaf") + [S.mesh(*merge(*leaves)), 'AttributeEnd']
    # the bloom
    u_, w_ = frame(axis)
    cup = []
    for k in range(petals):
        yaw = 2 * math.pi * k / petals + r.uniform(-0.12, 0.12)
        radial = math.cos(yaw) * u_ + math.sin(yaw) * w_
        tilt = math.radians(opening + r.uniform(-6, 6))
        d = axis * math.cos(tilt) + radial * math.sin(tilt)      # (the petal's length, out and up from the cup's foot)
        fine = radius > 0.7   # (a small flower's petals coarser: they are never seen close)
        b = blade(radius * r.uniform(0.92, 1.08), radius * 0.36, 0.35, -0.22, 0.018 * radius + 0.004, 12 if fine else 8, 24 if fine else 12, 0.75)
        # its face towards the cup's middle: turn the blade about its length so that its +z points inwards
        P, idx, N, UV = b
        side = np.cross(d, radial)
        side /= np.linalg.norm(side)
        inward = np.cross(side, d)
        Rm = np.stack([side, d, inward], axis=1)   # (local x, y, z -> world)
        cup.append((np.asarray(P) @ Rm.T + top, idx, np.asarray(N) @ Rm.T, UV))
    out += ['AttributeBegin'] + tinted(tint) + [S.mesh(*merge(*cup)), 'AttributeEnd']
    # the pistil: a clear bead on a short neck, and the orb of light above it
    pist = top + axis * (radius * 0.18)
    out += ['AttributeBegin', CLEAR, 'Translate %.4f %.4f %.4f' % tuple(pist), 'Shape "sphere" "float radius" [ %.4f ]' % (radius * 0.11), 'AttributeEnd']
    orb = top + axis * (radius * 0.48)
    if not orb_too:
        return out
    g = GLOW.get(tint, (1.0, 0.8, 0.6))
    L = 2.6 / (radius * 0.11 / 0.1) ** 2   # (an orb's power about the same whatever its flower's size)
    out += ['# @anim glow_%s' % name, 'AttributeBegin', 'AreaLightSource "diffuse" "rgb L" [ %.3f %.3f %.3f ]' % (g[0] * L, g[1] * L, g[2] * L),
            'Material "coateddiffuse" "rgb reflectance" [ 0.82 0.78 0.72 ] "float roughness" [ 0.1 ]', 'Translate %.4f %.4f %.4f' % tuple(orb),
            'Shape "sphere" "float radius" [ %.4f ]' % (radius * 0.11), 'AttributeEnd']   # (a pearl until it lights)
    FLOWERS.append((name, base, top, radius, tint, glow_at + 3.0, orb))   # (+3: the sky's sunset at 40 s)
    return out


def crystals(name, x, z, count, height, seed, tint=None):
    """A cluster of six-sided crystals leaning out of the moss, each a prism with a pointed tip (flat faces: no normals given)."""
    r = np.random.default_rng(seed)
    y0 = ground(x, z)
    P, idx = [], []
    for k in range(count):
        h = height * r.uniform(0.35, 1.0)
        rad = h * r.uniform(0.08, 0.13)
        tilt = r.uniform(5, 32) if k else r.uniform(0, 6)
        yaw = r.uniform(0, 360)
        off = (r.uniform(-0.5, 0.5) * height * 0.3, r.uniform(-0.5, 0.5) * height * 0.3)
        ring = lambda y, rr: [(rr * math.cos(math.pi / 3 * i + 0.3 * k), y, rr * math.sin(math.pi / 3 * i + 0.3 * k)) for i in range(6)]
        pts = ring(-0.3, rad) + ring(h * 0.82, rad) + [(0.0, h, 0.0)] + [(0.0, -0.3, 0.0)]
        local = np.array(pts)
        m = transform((local, [], local, np.zeros((len(local), 2))), at=(x + off[0], y0, z + off[1]), yaw=yaw, tilt=tilt)
        b = len(P)
        P += list(m[0])
        for i in range(6):
            j = (i + 1) % 6
            idx += [b + i, b + 6 + i, b + 6 + j, b + i, b + 6 + j, b + j]   # the sides
            idx += [b + 6 + i, b + 12, b + 6 + j]                            # the tip
            idx += [b + i, b + j, b + 13]                                    # the foot
    mat = [tinted(tint)[0]] if tint else ['Material "dielectric" "float eta" [ 1.55 ] "float abbenumber" [ 22 ]']
    return ['AttributeBegin'] + mat + [S.mesh(P, idx), 'AttributeEnd']


def lily_pads():
    """Glass lily pads on the pond (thin tinted discs with a notch) and three small glass lotuses on them, each with an orb that wakes at night."""
    out = []
    r = np.random.default_rng(41)
    pads = []
    spots = [(-1.2, -3.0, 0.55), (2.6, -2.4, 0.45), (3.8, -6.2, 0.6), (-0.6, -7.4, 0.5), (1.4, -8.6, 0.4), (4.4, -4.0, 0.35), (-2.0, -5.0, 0.42)]
    for (px, pz, pr) in spots:
        notch = r.uniform(0, 2 * math.pi)
        def pad(u, v, side, pr=pr, notch=notch, px=px, pz=pz):
            a = notch + 0.25 + (2 * math.pi - 0.5) * u
            rr = pr * v
            th = 0.012 * (1 - v * v)
            return (px + rr * math.cos(a), WATER_Y + 0.006 + side * th, pz + rr * math.sin(a)), (u, v)
        pads.append(grid(40, 8, lambda u, v: pad(u, v, 1.0)))
        pads.append(grid(40, 8, lambda u, v: pad(1.0 - u, v, -1.0)))
    out += ['AttributeBegin'] + tinted("leaf") + [S.mesh(*merge(*pads)), 'AttributeEnd']
    return out, spots


# ---- drifting petals ----------------------------------------------------------------------------------------------------------------------------------------
PETALS = []   # (name, start, path [(t, dx, dy, dz)], spin axis, spin [(t, deg)])


def drifting_petals():
    out = []
    r = np.random.default_rng(51)
    heads = [f for f in FLOWERS if f[3] > 0.8]
    for k in range(22):
        f = heads[k % len(heads)]
        tint = f[4]
        start = np.asarray(f[2]) + np.array([r.uniform(-0.6, 0.6), f[3] * r.uniform(0.4, 0.8), r.uniform(-0.6, 0.6)])
        size = r.uniform(0.14, 0.24)
        b = blade(size, size * 0.4, 0.4, -0.15, 0.006, 10, 16, 0.8)
        b = transform(b, at=tuple(start), yaw=r.uniform(0, 360), tilt=r.uniform(-60, 60))
        name = "petal%d" % k
        out += ['# @anim %s' % name, 'AttributeBegin'] + tinted(tint) + [S.mesh(*b), 'AttributeEnd']
        # its way down: released between 6 s and 46 s, falling slowly (about 0.35 m/s) on a wandering breeze, settling on the pond or the moss and drifting on
        t0 = 6.0 + 40.0 * (k / 22.0) + r.uniform(-1.5, 1.5)
        land_at = np.array([POND[0] + r.uniform(-3.5, 3.5), WATER_Y + 0.02, POND[1] + r.uniform(-3.0, 3.0)]) if k % 3 != 2 else \
            np.array([start[0] + r.uniform(-2.5, 2.5), 0.0, start[2] + r.uniform(-2.5, 2.5)])
        if k % 3 == 2:
            land_at[1] = ground(land_at[0], land_at[2]) + 0.03
        fall = (start[1] - land_at[1]) / 0.35
        t1 = t0 + fall
        path = [(0.0, 0, 0, 0), (t0, 0, 0, 0)]
        steps = 10
        for i in range(1, steps + 1):
            s = i / steps
            p = start + (land_at - start) * s
            p = p + np.array([0.6 * math.sin(s * 5.0 + k), 0.0, 0.5 * math.cos(s * 4.0 + 2 * k)]) * math.sin(math.pi * s)
            path.append((t0 + (t1 - t0) * s, *(p - start)))
        if t1 < 60.0:   # (afloat: drifting on a few centimetres more)
            last = np.array(path[-1][1:])
            path.append((60.0, *(last + np.array([r.uniform(-0.3, 0.3), 0.0, r.uniform(-0.3, 0.3)]))))
        path = [p for p in path if p[0] <= 60.0] or path[:2]
        ax = r.normal(size=3)
        ax /= np.linalg.norm(ax)
        spin = [(0.0, 0.0), (t0, 0.0), (min(60.0, t1), r.uniform(300, 720) * (1 if k % 2 else -1))]
        PETALS.append((name, start, path, ax, spin))
    return out


# ---- the set ------------------------------------------------------------------------------------------------------------------------------------------------
GLOW_ON = {}   # flower -> the time its orb lights


def main():
    moss_pictures()
    hills_picture()
    mud_picture()
    head = ['# The Glass Garden - a mossy hollow at sunset where giant flowers of coloured glass grow round a still pond (written by scripts/dreams/glass_garden.py; see',
            '# its header). Metres, +y up. The film adds the sky, the mist round the camera, the trees, the',
            '# fireflies and the fungi.',
            'LookAt 14 4.5 9   -5 3 0   0 1 0', 'Camera "perspective" "float fov" [ 50 ]',
            'Film "rgb" "integer xresolution" [ 1920 ] "integer yresolution" [ 1080 ]', 'Sampler "halton" "integer pixelsamples" [ 64 ]', 'WorldBegin',
            'LightSource "infinite" "rgb L" [ 0.6 0.7 0.9 ]',
            'Texture "moss" "spectrum" "imagemap" "string filename" [ "moss.png" ] "string mapping" [ "uv" ]',
            'Texture "hills" "spectrum" "imagemap" "string filename" [ "hills.png" ]',
            'Texture "mud" "spectrum" "imagemap" "string filename" [ "mud.png" ] "float uscale" [ 2 ] "float vscale" [ 2 ]']
    S.lines += land()
    # the moon's light, rising in the east as the sun goes (block moon: dark until the afterglow) and the moon itself, low over the eastern hills
    S.lines += ['# @anim moon', 'AttributeBegin', 'LightSource "distant" "point3 from" [ 0.80 0.42 -0.42 ] "point3 to" [ 0 0 0 ] "rgb L" [ 0.040 0.048 0.070 ]',
                'AreaLightSource "diffuse" "rgb L" [ 0.9 0.95 1.05 ]', 'Material "diffuse" "rgb reflectance" [ 0 0 0 ]', 'Translate 2400 1260 -1260',
                'Shape "sphere" "float radius" [ 30 ]', 'AttributeEnd']
    # the flowers: the great rose in the west of the hollow (the low sun behind it from the camera's first places), the others round the pond, then the small ones
    big = [("rose", -6.0, 1.5, 6.2, 1.70, "rose", 7, 50, 0.7, 36.0), ("amber", -9.5, -3.0, 4.6, 1.30, "amber", 6, 46, 0.5, 38.5),
           ("violet", -3.0, 6.0, 5.2, 1.40, "violet", 8, 52, 0.6, 37.2), ("aqua", -11.0, 5.5, 3.6, 1.00, "aqua", 6, 44, 0.4, 40.0),
           ("gold", 4.5, 7.5, 4.0, 1.10, "gold", 7, 50, 0.5, 39.3), ("ice", 6.5, -10.5, 5.6, 1.50, "ice", 8, 48, 0.6, 41.0),
           ("rose2", -2.5, -11.5, 3.2, 0.90, "rose", 6, 46, 0.4, 42.0), ("violet2", 8.5, 1.0, 2.8, 0.80, "violet", 6, 50, 0.3, 42.8),
           ("amber2", -14.0, -8.0, 5.0, 1.30, "amber", 7, 46, 0.5, 43.5), ("aqua2", 2.0, 12.0, 3.4, 0.95, "aqua", 6, 48, 0.4, 44.2)]
    for (name, x, z, h, rad, tint, n, opening, lean, glow) in big:
        S.lines += ['# the %s flower' % name] + flower(name, x, z, h, rad, tint, n, opening, lean, hash(name) % 1000 + 7, glow)
    small_tints = ["rose", "amber", "violet", "aqua", "gold", "ice"]
    k, tries = 0, 0
    r = np.random.default_rng(61)
    while k < 24 and tries < 500:
        tries += 1
        x, z = r.uniform(-15, 13), r.uniform(-15, 14)
        if math.hypot(x - POND[0], z - POND[1]) < POND_R + 0.8 or any(math.hypot(x - b[1], z - b[2]) < 1.6 for b in big) or math.hypot(x - 14, z - 9) < 5:
            continue
        h = r.uniform(0.7, 1.9)
        S.lines += flower("small%d" % k, x, z, h, h * r.uniform(0.22, 0.3), small_tints[k % 6], int(r.integers(5, 8)), r.uniform(40, 60), 0.15 * h, 100 + k, 44.0 + 6.0 * r.random())
        k += 1
    # a carpet of little glass flowers: six made once (one of each colour) and placed 220 times over the floor of the hollow, turned and sized each its own way
    protos = []
    for i, tint in enumerate(small_tints):
        h = 0.55
        S.lines += ['ObjectBegin "little_%s"' % tint] + flower("little_" + tint, 0.0, 0.0, h, h * 0.27, tint, 6, 50, 0.06, 300 + i, 0.0, orb_too=False, y0=0.05) + ['ObjectEnd']
        protos.append("little_" + tint)
    placed = 0
    while placed < 220 and tries < 6000:
        tries += 1
        x, z = r.uniform(-16, 14), r.uniform(-16, 15)
        if math.hypot(x, z) > 16 or math.hypot(x - POND[0], z - POND[1]) < POND_R + 0.4 or any(math.hypot(x - b[1], z - b[2]) < 0.9 for b in big):
            continue
        if math.hypot(x - 7.5, z - 6.4) < 1.2 or math.hypot(x - 2.6, z - 4.4) < 1.0:   # (clear of the low dolly's lens)
            continue
        S.lines += ['AttributeBegin', 'Translate %.3f %.3f %.3f' % (x, ground(x, z), z), 'Rotate %.1f 0 1 0' % r.uniform(0, 360), 'Scale %.3f %.3f %.3f' % ((r.uniform(0.6, 1.8),) * 3),
                    'ObjectInstance "%s"' % protos[placed % 6], 'AttributeEnd']
        placed += 1
    # the crystals: a clear cluster by the pond, an amethyst one under the violet flower, a tall clear one at the hollow's edge
    S.lines += ['# the crystals'] + crystals("c1", 5.6, -3.0, 7, 1.6, 71) + crystals("c2", -4.6, 4.2, 6, 1.1, 72, "amethyst") + crystals("c3", -12.5, 0.5, 9, 3.2, 73)
    pads, spots = lily_pads()
    S.lines += ['# the lily pads'] + pads
    for i, (px, pz, pr) in enumerate(spots[:3]):   # small lotuses on three pads
        S.lines += flower("lotus%d" % i, px, pz, 0.12, pr * 0.55, ["rose", "ice", "gold"][i], 8, 62, 0.0, 200 + i, 45.0 + 1.3 * i)
    S.lines += ['# the drifting petals'] + drifting_petals()
    path = S.write("garden.pbrt", head)
    print("wrote", path, "with", S.meshes, "meshes")
    write_film()
    write_score()


# ---- the film -----------------------------------------------------------------------------------------------------------------------------------------------
def nums(*v):
    return "  ".join("%.3f" % x for x in v)


def write_film():
    L = []
    L += ['# The Glass Garden: a dream showcase film, written by scripts/dreams/glass_garden.py (change that and run it again, not this file).',
          '# A mossy hollow at sunset where giant flowers of coloured glass grow round a still pond. The low sun shines through their petals and stems - coloured glass,',
          '# dispersion, light shafts in a thin mist - then sets; the sky fades through its afterglow, and the garden wakes into a night lit by its own blossoms, with',
          '# fireflies, glowing fungi, rising pollen and petals drifting down onto the water. Glass looks its best at a ray depth of 12 or more.',
          'film    %s' % KEY, 'name    The Glass Garden',
          'about   A dream: giant flowers of coloured glass round a still pond in a mossy hollow, the sunset shining through them, then a night lit by their own blossoms.',
          'scene   film_assets/%s/garden.pbrt' % KEY, 'length  60', 'still   20', 'genre   showcase', 'tags    glass, dusk, night, emissive, backlight, fog, dof', '',
          '# the sun and sky: from a low sun into the afterglow and the first of the night; the camera opens up as the light goes',
          'sky       17:40 20:22  south 180  latitude 40  physical', 'exposure  auto 2', 'optics    rays 0.7  alpenglow 0.8', 'mist      0.0011  radius 260', 'ambience  wind 0.12', 'mute      sounds', '',
          '# trees round the hollow\'s rim, ferns and rocks round the pond, glowing fungi in the moss, fireflies at night']
    r = np.random.default_rng(81)
    for i in range(40):   # (none in the west, where the sun sets: the sky behind the flowers stays open)
        a = math.radians(-120 + 220 * i / 40 + r.uniform(-2, 2))
        rr = r.uniform(46, 72)
        x, z = rr * math.cos(a), rr * math.sin(a)
        L.append('place  tree  at %.2f %.3f %.2f  scale %.2f  turn %d  seed %d' % (x, ground(x, z) - 0.05, z, r.uniform(1.8, 3.0), r.integers(0, 360), i + 1))
    for i in range(28):   # (and a belt of wood on the rim behind them, in patches each at its own height)
        a = math.radians(-125 + 230 * i / 28)
        rr = r.uniform(58, 90)
        x, z = rr * math.cos(a), rr * math.sin(a)
        L.append('scatter  tree 7 in %.1f %.1f %.1f %.1f  scale 2.0 3.4  seed %d  y %.2f' % (x - 7, z - 7, x + 7, z + 7, 200 + i, ground(x, z) - 0.1))
    for i in range(60):   # (woods on the hills, out to 260 m: the hills read as wooded country, not bare slopes)
        a = math.radians(195 + 275 * r.random())   # (all round but the sunset's quarter, 110-190 degrees)
        rr = r.uniform(95, 260)
        x, z = rr * math.cos(a), rr * math.sin(a)
        L.append('scatter  tree 14 in %.1f %.1f %.1f %.1f  scale 2.6 4.2  seed %d  y %.2f' % (x - 14, z - 14, x + 14, z + 14, 400 + i, ground(x, z) - 0.3))
    for i in range(10):
        a = 2 * math.pi * i / 10 + 0.3
        rr = r.uniform(15, 18)
        x, z = rr * math.cos(a), rr * math.sin(a)
        L.append('scatter  bush 5 in %.1f %.1f %.1f %.1f  scale 0.8 1.4  seed %d  y %.2f' % (x - 2.5, z - 2.5, x + 2.5, z + 2.5, 300 + i, ground(x, z) - 0.05))
    for i in range(14):
        a = 2 * math.pi * i / 14 + r.uniform(-0.15, 0.15)
        rr = POND_R + r.uniform(0.3, 1.2)
        x, z = POND[0] + rr * math.cos(a), POND[1] + rr * math.sin(a)
        L.append('place  %s  at %.2f %.3f %.2f  scale %.2f  turn %d  seed %d' % ("rock" if i % 3 == 0 else "fern", x, ground(x, z) - 0.02, z, r.uniform(0.35, 0.8), r.integers(0, 360), 50 + i))
    L += ['scatter  grass 900 in -16 -16 14 14  scale 0.5 1.0  seed 4  y 0.02', 'scatter  fern 70 in -16 -16 14 14  scale 0.5 0.9  seed 6  y 0.02',
          'fireflies 260  in -18 -18 16 16  height 2.6  during 40 60', '']
    L += ['# the moon comes up as the sun goes', 'light  moon  0 0   36 0   46 1   60 1', '']
    L += ['# the blossoms wake one by one as the light goes (each flower\'s orb: its block glow_NAME)']
    for (name, base, top, rad, tint, glow_at, orb) in FLOWERS:
        L.append('light  glow_%s  0 0   %.2f 0   %.2f 1   60 1' % (name, glow_at, glow_at + 2.5))
    L += ['', '# pollen rising from the great rose once it glows', 'swarm  pollen 240 from %s radius 2.4 during 44 60 velocity 0 0.22 0 spread 0.7 gravity -0.005 life 16 colour 1 0.78 0.45 bright 1.0 size 0.02 pulse 0.3 seed 5' % nums(*FLOWERS[0][6]), '']
    L += ['# the drifting petals: each falls from a bloom on a wandering breeze, tumbling, onto the pond or the moss']
    for (name, start, path, ax, spin) in PETALS:
        L.append('move  %s  ' % name + "   ".join("%.2f %.3f %.3f %.3f" % p for p in path))
        L.append('turn  %s about %s axis %s   ' % (name, nums(*start), nums(*ax)) + "   ".join("%.2f %.1f" % s for s in spin))
    # (no title over the opening and no credits after it: the film is the picture and its music alone)
    L += ['# the shots: slow, gliding cameras (key: from, at, fov [lens radius, focus distance])',
          'shot 0 The hollow at sunset',
          '  key  16 9.5 15    -6 3.2 0.5    46',
          '  key  9.5 2.8 7.5   -6 3.6 0.8    42',
          '  fade 2.0',
          'shot 9 Among the stems',
          '  key  7.5 0.55 6.4    -6 1.6 1.2    40  0.025 9',
          '  key  2.6 0.45 4.4    -8 2.4 0.2    40  0.025 8',
          'shot 18 The great rose',
          '  key  -1.6 5.0 4.6    -6 6.1 1.5    44  0.02 5.5',
          '  key  -2.6 7.3 -1.9   -6 6.3 1.5    44  0.02 5',
          'shot 27 The still pond',
          '  key  7.0 0.32 -1.6   -4 1.6 -6.5    42',
          '  key  5.6 0.28 -6.8   -6 2.2 -3.2    42',
          'shot 36 The garden wakes',
          '  key  10 2.3 5.2      -5 2.6 -1     46',
          '  key  6.2 2.0 3.6     -5.5 2.8 -1.5   46',
          'shot 47 Lights on the water',
          '  key  7.2 0.55 -9.0   -5 2.4 0     42',
          '  key  6.0 0.45 -9.6   -6 2.6 0.5   42',
          'shot 54 The night garden',
          '  key  8 3 8          -4 2 -1        48',
          '  key  25 15 23       -4 1.2 -2      52']
    path = os.path.join(REPO, "pbrt_scenes", KEY + ".film")
    with open(path, "w", newline="\n") as f:
        f.write("\n".join(L) + "\n")
    print("wrote", path)


# ---- the score ----------------------------------------------------------------------------------------------------------------------------------------------
def write_score():
    """A dream in D Lydian, about 66 beats a minute: a glassy pad and a choir under bells and a harp-like pluck; the sunset swells, the afterglow thins to a
    single high drone, and each blossom that wakes rings a bell of its own; the night garden ends on a long open chord."""
    sh = Sheet("The Glass Garden")
    beat = 60.0 / 66.0
    sh.section("The hollow at sunset (0-9): out of silence, a pad and the choir, bells falling like light")
    sh.cue(0.0, "drone", ["D2"], length=9.5, gain=0.045, send=0.9)
    sh.cue(0.4, "pad", ["D3", "A3", "E4", "F#4"], length=8.8, gain=0.040, send=0.9)
    sh.cue(1.6, "choir", ["A4", "D5"], length=7.0, gain=0.024, send=0.95)
    for i, n in enumerate(["A5", "F#5", "E5", "D5", "G#5", "E5", "C#6", "A5"]):
        sh.cue(1.2 + i * beat, "bell", [n], length=3.0, gain=0.018, pan=-0.3 + 0.08 * i, send=0.95)
    sh.section("Among the stems (9-18): the pluck walks between the stems, the strings come in")
    prog = [["D3", "A3", "F#4", "G#4"], ["B2", "F#3", "D4", "A4"], ["G2", "D3", "B3", "F#4"], ["A2", "E3", "C#4", "G#4"]]
    for i, ch in enumerate(prog):
        t = 9.0 + i * 4 * beat
        sh.cue(t, "pad", ch, length=4 * beat + 0.4, gain=0.036, send=0.9)
        sh.cue(t, "bass", [ch[0]], length=4 * beat, gain=0.05, send=0.4)
        for j in range(8):
            sh.cue(t + j * beat * 0.5, "pluck", [ch[(j % 3) + 1].replace("3", "5").replace("4", "5") if j % 2 else ch[(j % 3) + 1]], length=1.2, gain=0.020, pan=0.35 * math.sin(j), send=0.7)
    sh.cue(12.5, "strings", ["D4", "A4", "E5"], length=6.0, gain=0.026, send=0.85)
    sh.section("The great rose (18-27): the theme on the strings and choir, the swell as the sun shines through it")
    theme = [("A4", 2), ("C#5", 1), ("E5", 1), ("G#5", 3), ("F#5", 1), ("E5", 2), ("D5", 2), ("E5", 4)]
    t = 18.2
    for n, d in theme:
        sh.cue(t, "strings", [n], length=d * beat + 0.3, gain=0.030, send=0.85)
        sh.cue(t, "choir", [n], length=d * beat + 0.5, gain=0.014, send=0.95)
        t += d * beat
    for i, ch in enumerate([["D3", "A3", "F#4"], ["E3", "B3", "G#4"], ["F#3", "C#4", "A4"], ["B2", "F#3", "D4", "E4"]]):
        sh.cue(18.0 + i * 4 * beat, "pad", ch, length=4 * beat + 0.4, gain=0.040, send=0.9)
        sh.cue(18.0 + i * 4 * beat, "bass", [ch[0]], length=4 * beat, gain=0.05, send=0.4)
    sh.cue(23.0, "swell", [], length=3.6, gain=0.05, send=0.85)
    sh.section("The still pond (27-36): the sun sets - the music thins to a high drone and a slow piano over it")
    sh.cue(27.0, "pad", ["D3", "A3", "E4"], length=9.0, gain=0.032, send=0.95)
    sh.cue(27.0, "drone", ["A1"], length=9.5, gain=0.035, send=0.9)
    for i, n in enumerate(["F#5", "E5", "A4", "B4", "E5", "D5", "A4"]):
        sh.cue(27.6 + i * 1.25, "piano", [n], length=2.4, gain=0.030, pan=0.15 * math.sin(i), send=0.85)
    sh.cue(31.5, "choir", ["E5", "A5"], length=5.0, gain=0.016, send=0.95)
    sh.section("The garden wakes (36-47): each blossom rings a bell as it lights; the harp and the choir grow under them")
    bells = ["D6", "A5", "F#6", "E6", "C#6", "A6", "G#5", "B5", "E6", "F#6", "D6"]
    for i, (name, base, top, rad, tint, glow_at, orb) in enumerate(f for f in FLOWERS if not f[0].startswith("small")):
        sh.cue(glow_at, "bell", [bells[i % len(bells)]], length=4.0, gain=0.024, pan=max(-0.8, min(0.8, -orb[0] / 14.0)), send=0.95)
    for i, ch in enumerate([["D3", "A3", "F#4", "G#4"], ["G3", "D4", "B4"], ["E3", "B3", "G#4", "C#5"], ["A2", "E3", "C#4", "E4"]]):
        sh.cue(36.0 + i * 2.75, "pad", ch, length=3.2, gain=0.036, send=0.9)
        sh.cue(36.0 + i * 2.75, "pluck", ch[1:], length=2.0, gain=0.020, send=0.75, spread=0.16)
    sh.cue(40.0, "choir", ["A4", "E5", "F#5"], length=7.0, gain=0.022, send=0.95)
    sh.section("Lights on the water (47-54): the theme again, softly on the piano, a bell for each ripple")
    t = 47.2
    for n, d in theme[:6]:
        sh.cue(t, "piano", [n], length=d * beat + 0.4, gain=0.028, send=0.85)
        t += d * beat * 0.9
    sh.cue(47.0, "pad", ["B2", "F#3", "D4", "A4"], length=7.2, gain=0.034, send=0.9)
    for i in range(6):
        sh.cue(48.0 + i * 1.1, "mallet", [["A5", "E6", "F#6", "C#6", "E6", "A6"][i]], length=2.0, gain=0.014, pan=0.4 * math.sin(i * 1.7), send=0.95)
    sh.section("The night garden (54-60): the long open chord as the camera rises, the choir and the bells fading into the credits")
    sh.cue(54.0, "pad", ["D3", "A3", "E4", "F#4", "G#4"], length=9.0, gain=0.040, send=0.95)
    sh.cue(54.0, "strings", ["D4", "A4", "F#5"], length=8.0, gain=0.026, send=0.9)
    sh.cue(54.5, "choir", ["A4", "D5", "F#5"], length=8.0, gain=0.022, send=0.95)
    sh.cue(54.0, "bass", ["D2"], length=8.0, gain=0.05, send=0.5)
    for i, n in enumerate(["D6", "A5", "F#5", "E5", "D5"]):
        sh.cue(55.0 + i * 1.2, "bell", [n], length=4.0, gain=0.016, pan=-0.2 + 0.1 * i, send=0.95)
    print("wrote", sh.write(KEY))


if __name__ == "__main__":
    main()
