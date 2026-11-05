# sky_islands.py: "Islands in the Sky" - a 60 s dream showcase film: an archipelago of floating islands high over a sea of cloud. It opens in the blue hour with
# the moon setting behind the islands; the sun rises out of the cloud sea and lights them one by one; a waterfall pours off an island's edge into the clouds; paper
# lanterns rise from a ring of standing stones on the great island and drift up among them; the camera pulls back and up until the whole archipelago hangs in the
# morning over the clouds. Writes the set (pbrt_scenes/film_assets/islands-in-the-sky/: islands.pbrt, its PLY meshes and pictures), the film
# (pbrt_scenes/islands-in-the-sky.film) and its score (film_assets/islands-in-the-sky/cues.txt). Fixed seeds: running it again writes the same files.
#   python scripts/dreams/sky_islands.py
import math, os, sys
import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dream_lib import Set, Sheet, REPO, fbm, fbm3, grid, merge, lathe, tube, bezier

KEY = "islands-in-the-sky"
S = Set(KEY)

# the islands: (name, x, top y, z, radius, underside depth, seed, trees, waterfall facing or None)
ISLANDS = [
    ("great", 0.0, 420.0, 0.0, 150.0, 300.0, 1, 70, None),
    ("north", 380.0, 520.0, -260.0, 90.0, 190.0, 2, 30, None),
    ("falls", -420.0, 360.0, -180.0, 110.0, 230.0, 3, 36, 70.0),
    ("east", 260.0, 300.0, 380.0, 70.0, 150.0, 4, 18, None),
    ("high", -300.0, 600.0, 420.0, 60.0, 140.0, 5, 14, None),
    ("far_e", 760.0, 450.0, 220.0, 120.0, 250.0, 6, 0, None),
    ("far_w", -820.0, 480.0, -620.0, 140.0, 280.0, 7, 0, None),
    ("far_n", 120.0, 700.0, -760.0, 85.0, 180.0, 8, 0, None),
    ("low", -150.0, 250.0, -520.0, 50.0, 110.0, 9, 10, None),
    ("far_s", 300.0, 560.0, 980.0, 110.0, 220.0, 10, 0, None),
]
_r = np.random.default_rng(55)
_k = 0
while _k < 12:   # (small rocks adrift between the islands, none in an island)
    _a, _d = _r.uniform(0, 2 * math.pi), _r.uniform(180, 700)
    _R = _r.uniform(8, 24)
    _x, _y, _z, _D = _d * math.cos(_a), _r.uniform(260, 640), _d * math.sin(_a), _R * _r.uniform(1.6, 2.4)
    if any(math.hypot(_x - o[1], _z - o[3]) < _R + o[4] * 1.3 and _y + 30 > o[2] - o[5] and _y - _D < o[2] + 30 for o in ISLANDS):
        continue
    ISLANDS.append(("rock%d" % _k, _x, _y, _z, _R, _D, 20 + _k, 0, None))
    _k += 1
_outline = {}


def outline(seed, a):
    """An island's edge: its radius at angle a, as a share of its radius (lobed, never round)."""
    if seed not in _outline:
        _outline[seed] = fbm(512, 900 + seed, octaves=(3, 6, 12, 24), m=4)[2]
    f = _outline[seed][int((a / (2 * math.pi)) % 1.0 * 511)]
    return 0.78 + 0.42 * f


def top_height(isl, x, z):
    """The ground on an island's top: gently rolling, a little higher in the middle."""
    name, cx, ty, cz, R, D, seed, trees, wf = isl
    rho = math.hypot(x - cx, z - cz) / (R * outline(seed, math.atan2(z - cz, x - cx)))
    bumps = 0.035 * R * (math.sin((x - cx) / R * 5.1 + seed) * math.sin((z - cz) / R * 4.3 + 2 * seed) + 0.5 * math.sin((x - cx) / R * 11.0) * math.sin((z - cz) / R * 9.0))
    edge = 0.035 * R * (max(0.0, rho - 0.82) / 0.18) ** 2   # (the rim rounded over into the rock)
    return ty + 0.06 * R * (1 - rho * rho) + bumps * (1 - rho) - edge


# ---- pictures ---------------------------------------------------------------------------------------------------------------------------------------------
def pictures():
    n = 2048
    big, mid, fine = fbm(n, 21, (4, 8, 16)), fbm(n, 22, (32, 64)), fbm(n, 23, (128, 256, 512))
    # the meadow (12 m a tile): grass of several greens, lighter tops, and flowers in it (white, yellow, a little blue)
    c = np.array([0.16, 0.32, 0.07])[None, None] * (0.7 + 0.5 * big[..., None]) + np.array([0.10, 0.10, 0.02])[None, None] * mid[..., None]
    c *= (0.75 + 0.45 * fine[..., None])
    rng = np.random.default_rng(24)
    for colour, amount in (((0.92, 0.90, 0.82), 0.004), ((0.95, 0.80, 0.25), 0.003), ((0.55, 0.62, 0.95), 0.0015)):
        dots = rng.random((n, n)) < amount * (0.4 + big)
        c[dots] = colour
    S.save("meadow.png", c)
    # the rock beneath (u round the island, v down it, 30 m a tile): strata of sandstone, ochre and grey, streaked down by rain, darker where it is wet
    w, h = 1024, 1024
    y = np.linspace(0, 1, h)[:, None]
    warp = fbm(w, 31, (4, 8, 16), m=h)
    bands = np.sin((y + 0.08 * warp) * 2 * math.pi * 11) * 0.5 + 0.5
    bands2 = np.sin((y + 0.05 * warp) * 2 * math.pi * 37) * 0.5 + 0.5
    sand, ochre, grey = np.array([0.46, 0.42, 0.37]), np.array([0.40, 0.33, 0.27]), np.array([0.33, 0.33, 0.33])
    rock = sand * bands[..., None] + ochre * (1 - bands[..., None])
    rock = rock * (0.75 + 0.25 * bands2[..., None])
    rock = rock * (1 - 0.35 * (fbm(w, 32, (8, 16, 32), m=h) > 0.6)[..., None]) + grey * 0.35 * (fbm(w, 32, (8, 16, 32), m=h) > 0.6)[..., None]
    streaks = fbm(w, 33, (16, 32), m=8)[3][None, :]
    rock *= (0.88 + 0.16 * streaks[..., None])
    S.save("rock.png", rock * 0.82)
    # the moss and grass hanging over the lip, fading into the rock (the top of the rock picture's own band)
    lip = np.array([0.14, 0.26, 0.07])[None, None] * (0.7 + 0.5 * fbm(512, 34, (8, 16, 32))[..., None])
    S.save("lip.png", lip)


# ---- the islands ------------------------------------------------------------------------------------------------------------------------------------------
def island(isl):
    name, cx, ty, cz, R, D, seed, trees, wf = isl
    out = []
    rng = np.random.default_rng(seed)
    nu = 180 if R > 80 else 120

    def top(u, v):
        a = 2 * math.pi * u
        rr = R * outline(seed, a) * v
        x, z = cx + rr * math.cos(a), cz + rr * math.sin(a)
        return (x, top_height(isl, x, z), z), ((x - cx) / 12.0, (z - cz) / 12.0)
    P, idx, N, UV = grid(nu, 40, top, wrap_u=True)
    out += ['AttributeBegin', 'Material "diffuse" "texture reflectance" "meadow"', S.mesh(P, idx, N, UV), 'AttributeEnd']
    # the underside: from the lip down to a root of rock, bulging and ridged, the strata round it
    ridges = fbm(512, 950 + seed, octaves=(6, 12, 24, 48), m=4)[2]

    def under(u, v):
        a = 2 * math.pi * u
        edge = R * outline(seed, a)
        lipx, lipz = cx + edge * math.cos(a), cz + edge * math.sin(a)
        ly = top_height(isl, lipx, lipz)
        # the rock: narrowing to a root, bulging out just under the lip, its strata weathered into ledges, lumped and fluted by 3D noise, its foot ragged
        n = fbm3((math.cos(a) * 2.3 + seed, v * 5.5, math.sin(a) * 2.3), seed, 4) - 0.5
        n2 = fbm3((math.cos(a) * 7.0, v * 16.0 + seed, math.sin(a) * 7.0), seed + 50, 3) - 0.5
        prof = (1 - v) ** shape_e * (1 + 0.04 * math.sin(math.pi * min(1.0, v / 0.06)) + shape_b * math.sin(math.pi * min(1.0, v * 1.4))) + 0.015
        k = v * 12.0 + 0.25 * n
        ledge = 0.035 * (k - math.floor(k)) ** 4   # (each stratum weathered back under the one above: ledges)
        rr = edge * prof * (1 - ledge) * (1 + 0.22 * n * (0.15 + v)) * (1 + 0.04 * n2 * (v > 0.06))
        tip = (rng_tip[0] * v * v, rng_tip[1] * v * v)
        x, z = cx + rr * math.cos(a) + tip[0], cz + rr * math.sin(a) + tip[1]
        y = ly - D * v * (1 + 0.35 * n * v) - 0.03 * R * math.sin(math.pi * min(1.0, v * 4)) * (v < 0.25)
        return (x, y, z), (u * max(1, round(2 * math.pi * R / 90)), (ty - y) / 90.0)   # (a tile of the rock 90 m: its strata some 8 m each)
    rng_tip = (rng.uniform(-0.3, 0.3) * R, rng.uniform(-0.3, 0.3) * R)
    shape_e = rng.uniform(0.55, 1.35)   # (each island its own shape: a broad bowl, a cone, a long spire)
    shape_b = rng.uniform(0.0, 0.22)
    P, idx, N, UV = grid(nu, 60, under, wrap_u=True)
    # (the top band, just under the lip, the hanging moss; the rest the rock)
    T = np.array(idx).reshape(-1, 3)
    v_of = np.repeat(np.arange(61), nu + 1) / 60.0
    mossy = (v_of[T] < 0.06).all(axis=1)
    out += ['AttributeBegin', 'Material "diffuse" "texture reflectance" "lip"', S.mesh(P, T[mossy].ravel(), N, UV),
            'Material "diffuse" "texture reflectance" "rock"', S.mesh(P, T[~mossy].ravel(), N, UV), 'AttributeEnd']
    # spurs: lesser roots of rock hanging beside the main one under a big island
    spurs = []
    for k in range(5 if R > 80 else 2):
        a = rng.uniform(0, 2 * math.pi)
        rr = R * outline(seed, a) * rng.uniform(0.30, 0.62)
        x0, z0 = cx + rr * math.cos(a), cz + rr * math.sin(a)
        y0 = ty - D * rng.uniform(0.18, 0.3)
        Ls = D * rng.uniform(0.35, 0.6)
        pts = bezier((x0, y0, z0), (x0 + rng.uniform(-4, 4), y0 - Ls * 0.4, z0 + rng.uniform(-4, 4)), (x0 + rng.uniform(-8, 8), y0 - Ls * 0.75, z0 + rng.uniform(-8, 8)),
                     (x0 + rng.uniform(-10, 10), y0 - Ls, z0 + rng.uniform(-10, 10)), 30)
        r0 = R * rng.uniform(0.16, 0.24)
        radii = [r0 * (1 - t) ** 0.8 * (1 + 0.18 * math.sin(t * 17 + k)) + 0.3 for t in np.linspace(0, 1, 30)]
        P2, idx2, N2, UV2 = tube(pts, radii, 20)
        UV2 = np.array([(uv[0] * 2, (ty - p[1]) / 90.0) for uv, p in zip(UV2, P2)])
        spurs.append((P2, idx2, N2, UV2))
    if spurs:
        out += ['AttributeBegin', 'Material "diffuse" "texture reflectance" "rock"', S.mesh(*merge(*spurs)), 'AttributeEnd']
    # roots and vines hanging from under the lip
    hang = []
    for k in range(int(6 + R / 12)):
        a = rng.uniform(0, 2 * math.pi)
        edge = R * outline(seed, a) * rng.uniform(0.80, 0.95)
        x0, z0 = cx + edge * math.cos(a), cz + edge * math.sin(a)
        y0 = top_height(isl, x0, z0) - rng.uniform(4, 14)
        L = rng.uniform(15, 55) * (R / 100)
        pts = bezier((x0, y0, z0), (x0 + rng.uniform(-3, 3), y0 - L * 0.4, z0 + rng.uniform(-3, 3)),
                     (x0 + rng.uniform(-5, 5), y0 - L * 0.75, z0 + rng.uniform(-5, 5)), (x0 + rng.uniform(-6, 6), y0 - L, z0 + rng.uniform(-6, 6)), 18)
        rad = rng.uniform(0.25, 0.7) * (R / 100)
        hang.append(tube(pts, [rad * (1 - 0.9 * t) for t in np.linspace(0, 1, 18)], 7))
    if hang:
        out += ['AttributeBegin', 'Material "diffuse" "rgb reflectance" [ 0.12 0.10 0.07 ]', S.mesh(*merge(*hang)), 'AttributeEnd']
    return out


def stones():
    """A ring of nine standing stones on the great island's crown, the lanterns' place, and a low flat altar stone in the middle."""
    isl = ISLANDS[0]
    cx, cz = isl[1] + 10.0, isl[3] - 12.0
    out, parts = [], []
    rng = np.random.default_rng(77)
    for k in range(9):
        a = 2 * math.pi * k / 9 + 0.2
        x, z = cx + 13.0 * math.cos(a), cz + 13.0 * math.sin(a)
        y = top_height(isl, x, z) - 0.6
        h = rng.uniform(3.6, 5.2)
        w, d = rng.uniform(1.1, 1.5), rng.uniform(0.6, 0.9)
        def f(u, v, x=x, y=y, z=z, h=h, w=w, d=d, a=a, k=k):
            ang = 2 * math.pi * u
            taper = 1.0 - 0.35 * v
            px = w * taper * math.cos(ang) * (1 + 0.08 * math.sin(ang * 3 + k))
            pz = d * taper * math.sin(ang)
            ca, sa = math.cos(a + math.pi / 2), math.sin(a + math.pi / 2)
            return (x + px * ca - pz * sa, y + h * v + (0.25 * h * (v ** 6) * 0), z + px * sa + pz * ca), (u * 2, v * h / 3)
        parts.append(grid(16, 8, f, wrap_u=True))
    out += ['AttributeBegin', 'Material "diffuse" "texture reflectance" "rock"', S.mesh(*merge(*parts)), 'AttributeEnd']
    y = top_height(isl, cx, cz)
    alt = lathe([(0.0, y - 0.3), (2.4, y - 0.3), (2.5, y + 0.5), (2.3, y + 0.75), (0.0, y + 0.75)], 32, at=(cx, 0, cz))
    out += ['AttributeBegin', 'Material "diffuse" "texture reflectance" "rock"', S.mesh(*alt), 'AttributeEnd']
    return out, (cx, y, cz)


LANTERNS = []   # (name, start, path)


def lanterns(centre):
    """Sixty paper lanterns waiting round the altar, each a glowing lathe of paper (the camera sees it; next-event estimation leaves it out: "bool sampled" false,
    so sixty of them cost nothing to light with), released one after another and rising on the morning air, drifting with a light wind and wandering."""
    out = []
    rng = np.random.default_rng(88)
    cx, cy, cz = centre
    prof = [(0.17, 0.0), (0.27, 0.18), (0.32, 0.42), (0.30, 0.66), (0.22, 0.86), (0.10, 0.92), (0.0, 0.93)]
    for k in range(60):
        a = rng.uniform(0, 2 * math.pi)
        rr = rng.uniform(3.0, 10.5)
        x, z = cx + rr * math.cos(a), cz + rr * math.sin(a)
        y = top_height(ISLANDS[0], x, z) + 0.05
        s = rng.uniform(0.85, 1.25)
        m = lathe([(p[0] * s, p[1] * s) for p in prof], 14, at=(x, y, z))
        warm = rng.uniform(0.85, 1.15)
        L = 1.7 * warm
        name = "lantern%d" % k
        out += ['# @anim %s' % name, 'AttributeBegin', 'AreaLightSource "diffuse" "rgb L" [ %.3f %.3f %.3f ] "bool sampled" false "bool twosided" true' % (L, L * 0.48, L * 0.13),
                'Material "diffuse" "rgb reflectance" [ 0.9 0.8 0.6 ]', S.mesh(*m), 'AttributeEnd']
        # released from 39 s to 50 s, rising at 1.2 to 2.4 m/s (slowly at first, as the flame warms the air in it), on a light wind towards the sunrise, wandering
        t0 = 37.0 + 11.0 * (k / 60.0) + rng.uniform(-0.5, 0.5)
        up = rng.uniform(1.2, 2.4)
        wind = np.array([0.55, 0.0, 0.35]) * rng.uniform(0.6, 1.2)
        path = [(0.0, 0, 0, 0), (t0, 0, 0, 0)]
        for i in range(1, 13):
            t = t0 + (60.0 - t0) * i / 12
            dt = t - t0
            rise = up * (dt - 1.2 * (1 - math.exp(-dt / 1.2)))
            wander = np.array([math.sin(dt * 0.4 + k) * 2.0, 0.0, math.cos(dt * 0.33 + 2 * k) * 2.0]) * min(1.0, dt / 4)
            p = wind * dt + wander + np.array([0.0, rise, 0.0])
            path.append((t, *p))
        LANTERNS.append((name, (x, y, z), path))
    return out


# ---- the waterfall ----------------------------------------------------------------------------------------------------------------------------------------
FALL = {}


def waterfall():
    """Water pouring off the falls island's eastern lip: thrown out at 2.5 m/s, falling 330 m into the cloud sea (8 s), spreading as it falls - a sheet of white,
    aerated water broken into strands and clumps (an alpha picture). Six sheets carry the same strands moved on by a sixth of their spacing each: shown one a frame
    in turn, the clumps run down the fall at the speed the water falls."""
    isl = ISLANDS[2]
    face = math.radians(isl[8])
    dx, dz = math.sin(face), math.cos(face)
    edge = isl[4] * outline(isl[6], math.atan2(dz, dx)) * 0.93
    lx, lz = isl[1] + edge * dx, isl[3] + edge * dz
    ly = top_height(isl, lx, lz) - 0.3
    FALL["lip"] = (lx, ly, lz)
    T, v0, g, W = 8.2, 2.5, 9.8, 40.0
    ax, az = -dz, dx   # (across it)
    out = []
    n = 1024
    rng = np.random.default_rng(99)
    cols = np.zeros(n)
    x = np.linspace(0, 1, n)
    for k in range(120):   # strands across the sheet: each a soft column of its own width and strength
        c, w, st = rng.uniform(0, 1), rng.uniform(0.006, 0.03), rng.uniform(0.6, 1.0)
        cols = np.maximum(cols, st * np.exp(-((x - c) / w) ** 2))
    period = 0.06   # (in v, the share of the fall's time: a clump about every half second of falling)
    # the clumps: each strand its own mix of harmonics of the clump spacing (whole numbers of it, so that six frames on, moved by one spacing, the pattern is
    # what it was: the six sheets loop without a jump), times a still pattern of where the fall is thicker or thinner
    def per_strand(seed):   # (a value for each column that changes from strand to strand: random, smoothed over about a strand's width)
        v = np.random.default_rng(seed).random(n)
        k = np.exp(-np.linspace(-2, 2, 9) ** 2)
        return np.convolve(np.concatenate([v[-8:], v, v[:8]]), k / k.sum(), mode="same")[8:-8]
    amp = [0.4 + 0.6 * per_strand(130 + h) for h in range(4)]
    phs = [2 * math.pi * 3.0 * per_strand(140 + h) for h in range(4)]
    still = 0.55 + 0.9 * fbm(n, 150, (6, 12, 24))
    y = np.linspace(0, 1, n)[:, None]
    for k in range(6):
        moving = sum(amp[h][None, :] * np.sin(2 * math.pi * (h + 1) * (y / period - k / 6.0) + phs[h][None, :]) for h in range(4)) / 2.2
        clumps = np.clip(0.5 + 0.5 * moving, 0, 1)
        a = np.clip((cols[None, :] ** 1.6) * (0.45 + 0.9 * clumps) * still * (1.2 - 0.45 * y), 0, 1)
        a = np.flipud(a)   # (row 0 is the picture's top, v = 1)
        Image.fromarray((a * 255).astype(np.uint8)).save(os.path.join(S.out, "fall%d.png" % k))

    def sheet(u, v):
        t = T * v
        spread = W * (1 + 0.8 * v)
        across = (u - 0.5) * spread
        bow = 1.5 * math.sin(math.pi * u) * (1 + v)
        px = lx + dx * (v0 * t + bow) + ax * across
        pz = lz + dz * (v0 * t + bow) + az * across
        return (px, ly - 0.5 * g * t * t, pz), (u, 1.0 - v)
    P, idx, N, UV = grid(16, 120, sheet)
    for k in range(6):
        out += ['Texture "fall%d" "float" "imagemap" "string filename" [ "fall%d.png" ]' % (k, k), '# @anim fall%d' % k, 'AttributeBegin',
                'Material "diffusetransmission" "rgb reflectance" [ 0.95 0.96 0.98 ] "rgb transmittance" [ 0.30 0.32 0.34 ]',
                S.mesh(P, idx, N, UV) + ' "texture alpha" "fall%d"' % k, 'AttributeEnd']
    return out


# ---- the set and the film ---------------------------------------------------------------------------------------------------------------------------------
def main():
    pictures()
    head = ['# Islands in the Sky - floating islands over a sea of cloud (written by scripts/dreams/sky_islands.py; see its header). Metres, +y up; the cloud sea',
            '# lies from 0 to about 170 m, the islands float from 250 m to 700 m. The film adds the sky, the clouds, the haze, the trees, the birds and the waterfall.',
            'LookAt 180 470 300   -400 420 -300   0 1 0', 'Camera "perspective" "float fov" [ 50 ]',
            'Film "rgb" "integer xresolution" [ 1920 ] "integer yresolution" [ 1080 ]', 'Sampler "halton" "integer pixelsamples" [ 64 ]', 'WorldBegin',
            'LightSource "infinite" "rgb L" [ 0.5 0.6 0.8 ]',
            'Texture "meadow" "spectrum" "imagemap" "string filename" [ "meadow.png" ]',
            'Texture "rock" "spectrum" "imagemap" "string filename" [ "rock.png" ]',
            'Texture "lip" "spectrum" "imagemap" "string filename" [ "lip.png" ] "float uscale" [ 4 ] "float vscale" [ 4 ]']
    # the moon, setting in the west-south-west as the sun comes up behind the camera's first view (block moon: it fades into the morning sky)
    S.lines += ['# @anim moon', 'AttributeBegin', 'LightSource "distant" "point3 from" [ -0.80 0.20 -0.56 ] "point3 to" [ 0 0 0 ] "rgb L" [ 0.030 0.034 0.045 ]',
                'AreaLightSource "diffuse" "rgb L" [ 3.2 3.3 3.5 ]', 'Material "diffuse" "rgb reflectance" [ 0 0 0 ]', 'Translate -6400 1700 -4480',
                'Shape "sphere" "float radius" [ 95 ]', 'AttributeEnd']
    for isl in ISLANDS:
        S.lines += ['# the island "%s"' % isl[0]] + island(isl)
    st, centre = stones()
    S.lines += ['# the standing stones'] + st + ['# the lanterns'] + lanterns(centre)
    S.lines += ['# the waterfall: six sheets of falling water, one for each frame of six in turn (the film shows them so), so the water runs'] + waterfall()
    path = S.write("islands.pbrt", head)
    print("wrote", path, "with", S.meshes, "meshes")
    write_film(centre)
    write_score()


def nums(*v):
    return "  ".join("%.3f" % x for x in v)


def write_film(centre):
    L = ['# Islands in the Sky: a dream showcase film, written by scripts/dreams/sky_islands.py (change that and run it again, not this file).',
         '# An archipelago of floating islands over a sea of cloud: the blue hour and a setting moon, the sunrise lighting the islands one by one, a waterfall pouring',
         '# off an island into the clouds, lanterns rising from a ring of standing stones, and a pull-back over the whole archipelago in the morning light.',
         'film    %s' % KEY, 'name    Islands in the Sky',
         'about   A dream: floating islands over a sea of cloud at dawn - a setting moon, the sunrise, a waterfall falling into the clouds, and lanterns rising into the morning.',
         'scene   film_assets/%s/islands.pbrt' % KEY, 'length  60', 'still   27', 'genre   showcase', 'tags    dawn, nature, fog, emissive, backlight, water', '',
         '# the dawn: the blue hour, the sun rising out of the cloud sea at about 22 s, then the low gold of the first hour; the camera opens up in the dark',
         'sky       4:04 5:24  south 180  latitude 40  physical', 'exposure  auto 1.6', 'optics    rays 0.8  alpenglow 1', 'shutter   0                                   # (moving blocks sharp: the wavefront GPU renderer, which the haze needs, cannot draw a moving mesh yet)',
         'clouds    deck  base 0  thick 170  cover 0.88  area 9000  density 1.2',
         'ambience  wind 0.25', '']
    L += ['# woods and meadows on the islands (each patch at its island\'s height)']
    rng = np.random.default_rng(91)
    for isl in ISLANDS:
        name, cx, ty, cz, R, D, seed, trees, wf = isl
        if trees <= 0:
            continue
        for k in range(max(1, trees // 10)):
            a = rng.uniform(0, 2 * math.pi)
            rr = R * rng.uniform(0.15, 0.6)
            x, z = cx + rr * math.cos(a), cz + rr * math.sin(a)
            if name == "great" and math.hypot(x - centre[0], z - centre[2]) < 30:
                continue
            s = min(18.0, R * 0.16)
            L.append('scatter  tree 10 in %.1f %.1f %.1f %.1f  scale 2.4 4.0  seed %d  y %.2f' % (x - s, z - s, x + s, z + s, 10 * seed + k, top_height(isl, x, z) - 0.3))
        L.append('scatter  bush %d in %.1f %.1f %.1f %.1f  scale 1.2 2.0  seed %d  y %.2f' % (trees // 2, cx - R * 0.5, cz - R * 0.5, cx + R * 0.5, cz + R * 0.5, 500 + seed, ty + 0.03 * R))
    L.append('scatter  grass 900 in %.1f %.1f %.1f %.1f  scale 0.6 1.1  seed 7  y %.2f' % (centre[0] - 28, centre[2] - 28, centre[0] + 28, centre[2] + 28, centre[1] - 0.1))
    L.append('scatter  rock 14 in %.1f %.1f %.1f %.1f  scale 0.6 1.6  seed 8  y %.2f' % (centre[0] - 26, centre[2] - 26, centre[0] + 26, centre[2] + 26, centre[1] - 0.2))
    # the waterfall: from the lip of the falls island, pouring into the cloud sea
    isl = ISLANDS[2]
    a = math.radians(90 - isl[8])   # (a facing measured from +z towards +x; the lip where the island's edge faces that way)
    dx, dz = math.sin(math.radians(isl[8])), math.cos(math.radians(isl[8]))
    edge = isl[4] * outline(isl[6], math.atan2(dz, dx)) * 0.97
    lx, lz = isl[1] + edge * dx, isl[3] + edge * dz
    ly = top_height(isl, lx, lz) - 0.5
    L += ['', '# the waterfall off the falls island' + chr(39) + 's eastern lip runs: its six sheets shown one a frame in turn']
    for k in range(6):
        spans = []
        for f in range(0, 60 * 24 + 1):
            if f % 6 == k:
                spans.append('%.4f %.4f' % (max(0.0, f / 24.0 - 0.02), f / 24.0 + 0.02))
        L.append('show  fall%d  ' % k + '  '.join(spans))
    L.append('')
    L += ['# birds: a skein of geese crossing at sunrise, two eagles circling over the great island',
          'flock  geese 11  from -700 470 300  to 900 520 -200', 'flock  eagles 2  at 40 520 -40  radius 70', '']
    L += ['# the lanterns: released one after another from the stones, rising on the morning air']
    for (name, start, path) in LANTERNS:
        L.append('move  %s  ' % name + "   ".join("%.2f %.2f %.2f %.2f" % p for p in path))
    L += ['', '# the moon sets into the morning', 'light  moon  0 1   18 1   30 0.25   44 0', '']
    L += ['title  "Islands in the Sky"  at 1.5  for 5.5', 'credits', 'credit  "A dream made with light: a sea of cloud, a sunrise, and the air between the islands"', '']
    gx, gy, gz = centre
    L += ['# the shots (key: from, at, fov [lens radius, focus distance])',
          'shot 0 Before dawn',
          '  key  560 405 620    -500 440 -340    44',
          '  key  500 400 560    -560 440 -380    44',
          '  fade 2.0',
          'shot 10 The sea of clouds',
          '  key  -260 215 420    -10 300 30     46',
          '  key  -120 232 260    20 330 -20     46',
          'shot 21 Sunrise',
          '  key  %s    700 470 520    52' % nums(gx - 70, gy + 9, gz - 60),
          '  key  %s    720 480 470    52' % nums(gx - 50, gy + 12, gz - 75),
          'shot 31 The falls',
          '  key  %s    %s    46' % (nums(lx + 105, ly + 55, lz - 70), nums(lx + 8, ly - 70, lz + 3)),
          '  key  %s    %s    46' % (nums(lx + 95, ly + 25, lz - 85), nums(lx + 12, ly - 140, lz + 4)),
          'shot 41 Lanterns',
          '  key  %s    %s    50' % (nums(gx - 22, gy + 2.5, gz + 26), nums(gx + 4, gy + 16, gz - 4)),
          '  key  %s    %s    50' % (nums(gx - 16, gy + 12, gz + 20), nums(gx + 8, gy + 30, gz - 8)),
          'shot 51 The world above the clouds',
          '  key  %s    %s    50' % (nums(gx - 60, gy + 60, gz + 110), nums(gx + 30, gy + 40, gz - 20)),
          '  key  -900 1050 1500    60 420 -80    52']
    path = os.path.join(REPO, "pbrt_scenes", KEY + ".film")
    with open(path, "w", newline="\n") as f:
        f.write("\n".join(L) + "\n")
    print("wrote", path)


def write_score():
    """An epic dream in E-flat Lydian, about 72 beats a minute: a low choir and a drone in the blue hour; harp arpeggios as the clouds glide by; the horns' call and
    timpani as the sun breaks out of the cloud sea; the strings' theme over the falls; bells and a soaring choir for the lanterns; the full orchestra's last chord as
    the camera pulls away."""
    sh = Sheet("Islands in the Sky")
    beat = 60.0 / 72.0
    sh.section("Before dawn (0-10): a low drone, the choir alone, bells like stars going out")
    sh.cue(0.0, "drone", ["Eb2"], length=10.5, gain=0.050, send=0.9)
    sh.cue(0.5, "choir", ["Bb3", "Eb4"], length=9.0, gain=0.026, send=0.95)
    sh.cue(4.5, "choir", ["G4", "Bb4"], length=6.0, gain=0.020, send=0.95)
    for i, n in enumerate(["Bb5", "G5", "F5", "Eb5", "D5", "Bb4"]):
        sh.cue(1.5 + i * 1.4, "bell", [n], length=3.5, gain=0.014, pan=-0.4 + 0.15 * i, send=0.95)
    sh.section("The sea of clouds (10-21): harp arpeggios gliding, the pad rising, the strings creeping in")
    prog = [["Eb3", "Bb3", "G4", "A4"], ["C3", "G3", "Eb4", "Bb4"], ["Ab2", "Eb3", "C4", "G4"], ["Bb2", "F3", "D4", "A4"]]
    for i, ch in enumerate(prog):
        t = 10.0 + i * 4 * beat * 0.8
        sh.cue(t, "pad", ch, length=4 * beat * 0.8 + 0.5, gain=0.038, send=0.9)
        sh.cue(t, "bass", [ch[0]], length=4 * beat * 0.8, gain=0.05, send=0.4)
        sh.cue(t, "pluck", ch[1:] + [ch[2].replace("4", "5")], length=2.5, gain=0.022, send=0.75, spread=0.14)
        sh.cue(t + 2 * beat * 0.8, "pluck", ch[1:], length=2.0, gain=0.018, send=0.75, spread=0.12)
    sh.cue(15.0, "strings", ["Eb4", "Bb4", "F5"], length=6.5, gain=0.024, send=0.85)
    sh.cue(19.0, "swell", [], length=2.6, gain=0.06, send=0.85)
    sh.section("Sunrise (21-31): the horns' call and the timpani as the sun breaks out of the clouds, the choir in full")
    sh.cue(21.0, "impact", [], length=0.8, gain=0.040, send=0.7)
    for i, (n, d) in enumerate([("Bb3", 1), ("Eb4", 1), ("F4", 1), ("G4", 3), ("F4", 1), ("Eb4", 1), ("Bb4", 4)]):
        sh.cue(21.2 + sum(x[1] for x in [("Bb3", 1), ("Eb4", 1), ("F4", 1), ("G4", 3), ("F4", 1), ("Eb4", 1), ("Bb4", 4)][:i]) * beat, "brass", [n], length=d * beat + 0.3, gain=0.034, send=0.7)
    for i, ch in enumerate([["Eb3", "Bb3", "G4"], ["F3", "C4", "A4"], ["G3", "D4", "Bb4"], ["Ab2", "Eb3", "C4", "G4"]]):
        sh.cue(21.0 + i * 2.5, "strings", ch, length=2.9, gain=0.032, send=0.85)
        sh.cue(21.0 + i * 2.5, "bass", [ch[0].replace("3", "2")], length=2.5, gain=0.06, send=0.4)
        sh.cue(21.0 + i * 2.5, "timpani", ["Eb2" if i % 2 == 0 else "Bb1"], length=1.2, gain=0.030, send=0.5)
    sh.cue(22.0, "choir", ["Eb4", "G4", "Bb4", "D5"], length=8.5, gain=0.030, send=0.95)
    sh.section("The falls (31-41): the theme on the strings over a rolling pulse, the harp as the spray")
    theme = [("G4", 2), ("Bb4", 1), ("C5", 1), ("D5", 3), ("C5", 1), ("Bb4", 2), ("A4", 2), ("Bb4", 4)]
    t = 31.2
    for n, d in theme:
        sh.cue(t, "strings", [n, n.replace("4", "5").replace("5", "5")], length=d * beat + 0.3, gain=0.030, send=0.85)
        t += d * beat * 0.9
    for i, ch in enumerate([["Eb3", "Bb3", "G4"], ["C3", "G3", "Eb4"], ["Ab2", "Eb3", "C4"], ["Bb2", "F3", "D4"]]):
        sh.cue(31.0 + i * 2.5, "pad", ch, length=2.9, gain=0.034, send=0.9)
        sh.cue(31.0 + i * 2.5, "bass", [ch[0].replace("3", "2")], length=2.5, gain=0.05, send=0.4)
        for j in range(6):
            sh.cue(31.0 + i * 2.5 + j * 0.42, "pluck", [ch[(j % 2) + 1].replace("3", "5").replace("4", "5")], length=1.2, gain=0.015, pan=0.3 * math.sin(j + i), send=0.75)
    sh.section("Lanterns (41-51): bells for the lanterns, the choir soaring, the orchestra swelling")
    for i in range(14):
        sh.cue(41.0 + i * 0.72, "bell", [["Eb6", "Bb5", "G5", "F6", "D6", "Bb5", "C6"][i % 7]], length=3.0, gain=0.016, pan=0.5 * math.sin(i * 1.3), send=0.95)
    for i, ch in enumerate([["Eb3", "Bb3", "G4", "D5"], ["Ab2", "Eb3", "C4", "G4"], ["F3", "C4", "A4", "Eb5"], ["Bb2", "F3", "D4", "Bb4"]]):
        sh.cue(41.0 + i * 2.5, "strings", ch, length=2.9, gain=0.032, send=0.85)
        sh.cue(41.0 + i * 2.5, "bass", [ch[0].replace("3", "2")], length=2.5, gain=0.055, send=0.4)
    sh.cue(42.0, "choir", ["Bb4", "D5", "G5"], length=9.0, gain=0.030, send=0.95)
    sh.cue(46.5, "brass", ["Eb4", "Bb4"], length=4.0, gain=0.026, send=0.7)
    sh.section("The world above the clouds (51-60): the last chord of everything as the camera pulls away, into the credits")
    sh.cue(51.0, "impact", [], length=0.8, gain=0.030, send=0.7)
    sh.cue(51.0, "timpani", ["Eb2"], length=2.0, gain=0.035, send=0.5)
    sh.cue(51.0, "strings", ["Eb3", "Bb3", "G4", "D5", "A5"], length=9.5, gain=0.034, send=0.9)
    sh.cue(51.0, "brass", ["Eb3", "Bb3", "G4"], length=8.0, gain=0.026, send=0.7)
    sh.cue(51.5, "choir", ["Eb4", "G4", "Bb4", "D5"], length=9.0, gain=0.030, send=0.95)
    sh.cue(51.0, "bass", ["Eb2"], length=9.0, gain=0.06, send=0.4)
    sh.cue(51.0, "drone", ["Eb1"], length=10.0, gain=0.035, send=0.9)
    for i, n in enumerate(["Bb6", "G6", "Eb6", "D6", "Bb5", "G5"]):
        sh.cue(52.0 + i * 1.3, "bell", [n], length=4.0, gain=0.014, pan=-0.3 + 0.12 * i, send=0.95)
    print("wrote", sh.write(KEY))


if __name__ == "__main__":
    main()
