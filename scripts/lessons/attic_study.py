# attic_study.py [OUT_DIR]: "The Attic Study" - an original set for the teaching film pbrt_scenes/how-rays-add-up.film: a study under the roof at night, lit by a desk lamp
# under its shade and by the moon through a window in the gable. Written as a pbrt-v4 scene (attic_study.pbrt) with its meshes (meshes/*.ply) and textures (PNG) beside it -
# by default into pbrt_scenes/film_assets/how-rays-add-up. Metres, +y up; the room's floor centre at the origin, the window's gable towards -z. Run again after a change
# here; the scene and its pictures are committed as written.
#   the room: 5 m wide and 6 m long, low walls 1.2 m high under a pitched roof of boards on rafters, a ridge 3.4 m up; a square window of four panes in the far gable
#   the desk under the lamp: an open book, a glass ball, papers; a globe on its stand beside it; a reading chair on a round rug in the moonlight; low shelves of books
# The set is made for the lesson: almost everything in it is lit by light that has bounced (the lamp's pool on the desk lights the room, the moon's square on the floor
# lights the chair), which is where a ray tracer with few rays a pixel is grainiest, and the glass ball makes a caustic that only many rays find.
import math, os, sys
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sun_hall as kit   # (the meshes as PLY files, the lathes and grids, the noise and marble pictures: the Sun Hall's own)
from chess_terrace import merged, box   # (several meshes as one; an axis-aligned box)

OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "pbrt_scenes", "film_assets", "how-rays-add-up")
os.makedirs(OUT, exist_ok=True)
kit.OUT = OUT          # (kit.save and kit.mesh_text write beside this set's scene)
kit.MESHES.clear()

W, L = 2.5, 3.0        # half the room's width (x) and half its length (z)
KNEE, RIDGE = 1.2, 3.4  # the low walls' height and the ridge's
WIN = (-0.45, 0.45, 1.45, 2.35)   # the window in the far gable: x0 x1 y0 y1
DESK = (-1.55, -1.85)  # the desk's middle (x, z), against the left low wall's end
DESK_Y = 0.76


def roof_y(x):
    return KNEE + (RIDGE - KNEE) * (1.0 - abs(x) / W)


# ---- pictures ------------------------------------------------------------------------------------------------------------------------------------------------
def planks(n, m, width_px, base, seed):
    """Boards along x: each its own shade and grain, dark gaps between them."""
    f = kit.fbm(max(n, m), seed)[:m, :n]
    y, x = np.mgrid[0:m, 0:n]
    board = y // width_px
    rng = np.random.default_rng(seed)
    shade = rng.uniform(0.75, 1.12, 512)[board % 512]
    grain = (0.5 + 0.5 * np.sin((y / width_px * 7.0 + 0.6 * np.sin(x / n * 11.0 + board) + 4.0 * f) * math.pi)) ** 4
    c = np.array(base)[None, None, :] * shade[..., None] * (0.82 + 0.18 * f[..., None]) * (1 - 0.3 * grain[..., None])
    c[(y % width_px) < 2] *= 0.35
    return c


def pictures():
    kit.save("floor.png", planks(1024, 1024, 64, (0.46, 0.30, 0.17), 61))      # 4 m of floor: 25 cm boards
    kit.save("roof.png", planks(1024, 1024, 48, (0.52, 0.38, 0.24), 62))       # the roof's boards
    kit.save("plaster.png", np.stack([0.74 + 0.05 * kit.fbm(512, 63), 0.69 + 0.05 * kit.fbm(512, 63), 0.60 + 0.05 * kit.fbm(512, 63)], -1))
    # the rug: rings of madder red, indigo and ochre with a border
    n = 1024
    y, x = (np.mgrid[0:n, 0:n] + 0.5) / n * 2 - 1
    r = np.hypot(x, y)
    rug = np.zeros((n, n, 3)) + (0.45, 0.10, 0.08)
    for r0, r1, c in ((0.0, 0.18, (0.75, 0.55, 0.22)), (0.30, 0.36, (0.10, 0.14, 0.32)), (0.55, 0.62, (0.75, 0.55, 0.22)), (0.80, 0.92, (0.10, 0.14, 0.32))):
        rug[(r >= r0) & (r < r1)] = c
    a = np.arctan2(y, x)
    rug[(r > 0.66) & (r < 0.76) & (np.abs(np.sin(a * 24)) > 0.7)] = (0.80, 0.70, 0.50)
    rug *= (0.85 + 0.25 * kit.fbm(n, 64))[..., None]
    kit.save("rug.png", rug)
    # the globe: seas and made-up lands (noise above a level), lines of latitude and longitude
    w, h = 1024, 512
    f = kit.fbm(w, 65)[:h, :]
    lat, lon = np.mgrid[0:h, 0:w] / np.array([h, w])[:, None, None]
    land = f > 0.56
    g = np.zeros((h, w, 3)) + (0.20, 0.42, 0.55)
    g[land] = (0.78, 0.68, 0.45)
    g[(np.abs((lat * 12) % 1 - 0.5) > 0.48) | (np.abs((lon * 24) % 1 - 0.5) > 0.48)] *= 0.6
    kit.save("globe.png", g * (0.9 + 0.15 * f[..., None]))
    # the book's open pages: lines of type in two columns
    w, h = 512, 256
    page = np.zeros((h, w, 3)) + (0.92, 0.88, 0.78)
    yy, xx = np.mgrid[0:h, 0:w]
    lines = ((yy % 9) < 3) & (yy > 24) & (yy < h - 24) & ((np.abs(xx - w / 4) < w / 4 - 24) | (np.abs(xx - 3 * w / 4) < w / 4 - 24))
    word = np.random.default_rng(66).random((h // 9 + 1, w // 14 + 1)) > 0.18
    page[lines & word[yy // 9, xx // 14]] = (0.25, 0.22, 0.20)
    kit.save("pages.png", page)


# ---- shapes --------------------------------------------------------------------------------------------------------------------------------------------------
def gable(z, window, facing):
    """An end wall under the roof's slopes at z: squares of 5 cm, kept where any corner is under the roof (the roof hides the steps), the window's square left open."""
    P, idx, UV = [], [], []
    s = 0.05
    for j in range(int(RIDGE / s) + 1):
        for i in range(int(2 * W / s)):
            x0, y0 = -W + i * s, j * s
            x1, y1 = x0 + s, y0 + s
            if y0 >= max(roof_y(x0), roof_y(x1)):
                continue
            if window and x0 >= WIN[0] - 1e-6 and x1 <= WIN[1] + 1e-6 and y0 >= WIN[2] - 1e-6 and y1 <= WIN[3] + 1e-6:
                continue
            b = len(P)
            P += [(x0, y0, z), (x1, y0, z), (x1, y1, z), (x0, y1, z)]
            UV += [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]
            idx += [b, b + 1, b + 2, b, b + 2, b + 3]
    return np.array(P), idx, np.tile([0, 0, facing], (len(P), 1)), np.array(UV)


def slope(sign):
    """One side of the roof (sign -1: over the left low wall, +1: the right), from the low wall's top to the ridge, a little past each gable."""
    def f(u, v):
        x = sign * (W + 0.3) * (1 - u)   # (from 30 cm outside the low wall, so the two overlap and no light leaks in where they meet)
        return (x, KNEE + (RIDGE - KNEE) * (1.0 - abs(x) / W), -L - 0.1 + (2 * L + 0.2) * v), (u * 3.0, v * 6.2 / 4.0)
    P, idx, N, UV = kit.grid(1, 1, f)
    return P, idx, -N if sign > 0 else N, UV


def cylinder(x, z, r, y0, y1, segs=24):
    return kit.lathe([(0.0, y0), (r, y0), (r, y1), (0.0, y1)], segs, at=(x, 0, z))


def books(x0, z0, z1, y, depth, rng, groups):
    """A shelf's row of books from z0 to z1, spines facing +x... each book a box of its own height and thickness, into `groups` (one list of boxes a colour)."""
    z = z0
    while z < z1:
        t = rng.uniform(0.022, 0.055)
        h = rng.uniform(0.17, 0.27)
        if z + t > z1:
            break
        lean = rng.random() < 0.04
        groups[rng.integers(len(groups))].append(box(x0, y, z, x0 + depth * rng.uniform(0.75, 1.0), y + (h * 0.8 if lean else h), z + t))
        z += t + rng.uniform(0.0, 0.004)


BOOK_COLOURS = [(0.36, 0.08, 0.06), (0.10, 0.18, 0.12), (0.12, 0.12, 0.25), (0.42, 0.28, 0.12), (0.18, 0.10, 0.06), (0.55, 0.48, 0.34)]


def main():
    pictures()
    out = ['# The Attic Study - an original set: a study under the roof at night, a desk lamp and the moon (written by attic_study.py; see its header).',
           'LookAt 0.9 1.45 2.6   -0.6 0.95 -2.2   0 1 0', 'Camera "perspective" "float fov" [ 55 ]',
           'Film "rgb" "integer xresolution" [ 1920 ] "integer yresolution" [ 1080 ]', 'Sampler "halton" "integer pixelsamples" [ 64 ]', 'WorldBegin',
           '# the night outside, seen through the window, and the moon: its light falls through the window in a square on the floor, onto the chair',
           'LightSource "infinite" "rgb L" [ 0.06 0.08 0.16 ]',
           'LightSource "distant" "point3 from" [ -0.12 1.1 -1.0 ] "point3 to" [ 0 0 0 ] "rgb L" [ 3.6 4.2 5.7 ]']
    textures = [("floor", "floor.png"), ("roof", "roof.png"), ("plaster", "plaster.png"), ("rug", "rug.png"), ("globe", "globe.png"), ("pages", "pages.png")]
    out += ['Texture "%s" "spectrum" "imagemap" "string filename" [ "%s" ]' % t for t in textures]
    # the floor, the low walls, the gables, the roof's boards and its rafters
    P, idx, N, UV = kit.grid(1, 1, lambda u, v: ((-W + 2 * W * u, 0.0, -L + 2 * L * v), ((-W + 2 * W * u) / 4.0, (-L + 2 * L * v) / 4.0)))
    out += ['AttributeBegin', 'Material "coateddiffuse" "texture reflectance" "floor" "float roughness" [ 0.25 ]',
            kit.mesh_text(P, idx, np.tile([0, 1, 0], (len(P), 1)), UV), 'AttributeEnd']
    knees = []
    for sx in (-1, 1):
        P, idx, N, UV = kit.grid(1, 1, lambda u, v, sx=sx: ((sx * W, (KNEE + 0.3) * v, -L + 2 * L * u), ((-L + 2 * L * u) / 2.0, (KNEE + 0.3) * v / 2.0)))
        knees.append((P, idx, np.tile([-sx, 0, 0], (len(P), 1)), UV))
    out += ['AttributeBegin', 'Material "diffuse" "texture reflectance" "plaster"', kit.mesh_text(*merged(knees)), kit.mesh_text(*gable(-L, True, 1)),
            kit.mesh_text(*gable(L, False, -1)), 'AttributeEnd']
    out += ['AttributeBegin', 'Material "diffuse" "texture reflectance" "roof"', kit.mesh_text(*slope(-1)), kit.mesh_text(*slope(1)), 'AttributeEnd']
    rafters = []
    run = math.hypot(W, RIDGE - KNEE)
    tilt = math.atan2(RIDGE - KNEE, W)
    for k in range(11):
        z = -L + 0.3 + k * 0.54
        for sx in (-1, 1):   # (each rafter a beam 7 cm wide and 14 deep, hung just under the boards from the low wall's top to the ridge)
            P, idx, N, UV = box(0.0, -0.15, z - 0.035, run, -0.01, z + 0.035, 2.0)
            c, s_ = math.cos(tilt), math.sin(tilt)
            rot = np.array([[c, -s_, 0], [s_, c, 0], [0, 0, 1]])   # (about z: along the slope, rising towards the ridge)
            P, N = P @ rot.T, N @ rot.T
            if sx < 0:
                P[:, 0] -= W
            else:
                P[:, 0] = W - P[:, 0]
                N[:, 0] = -N[:, 0]
            P[:, 1] += KNEE
            rafters.append((P, idx, N, UV))
    rafters.append(box(-0.06, RIDGE - 0.2, -L, 0.06, RIDGE, L, 2.0))   # the ridge beam
    out += ['AttributeBegin', 'Material "diffuse" "rgb reflectance" [ 0.30 0.20 0.12 ]', kit.mesh_text(*merged(rafters)), 'AttributeEnd']
    # the window: a frame and a cross of glazing bars (no glass: the moonlight comes in clean), a sill
    x0, x1, y0, y1 = WIN
    zf = -L
    frame = [box(x0 - 0.06, y0 - 0.06, zf - 0.08, x1 + 0.06, y0, zf + 0.04), box(x0 - 0.06, y1, zf - 0.08, x1 + 0.06, y1 + 0.06, zf + 0.04),
             box(x0 - 0.06, y0, zf - 0.08, x0, y1, zf + 0.04), box(x1, y0, zf - 0.08, x1 + 0.06, y1, zf + 0.04),
             box(-0.018, y0, zf - 0.05, 0.018, y1, zf + 0.01), box(x0, (y0 + y1) / 2 - 0.018, zf - 0.05, x1, (y0 + y1) / 2 + 0.018, zf + 0.01),
             box(x0 - 0.12, y0 - 0.10, zf, x1 + 0.12, y0 - 0.06, zf + 0.16)]
    out += ['AttributeBegin', 'Material "diffuse" "rgb reflectance" [ 0.70 0.68 0.62 ]', kit.mesh_text(*merged(frame)), 'AttributeEnd']
    # the desk: a top on four legs, a drawer front
    dx, dz = DESK
    desk = [box(dx - 0.75, DESK_Y - 0.04, dz - 0.38, dx + 0.75, DESK_Y, dz + 0.38, 2.0)]
    for lx in (-0.7, 0.7):
        for lz in (-0.33, 0.33):
            desk.append(box(dx + lx - 0.03, 0.0, dz + lz - 0.03, dx + lx + 0.03, DESK_Y - 0.04, dz + lz + 0.03))
    desk.append(box(dx - 0.70, DESK_Y - 0.16, dz + 0.30, dx + 0.70, DESK_Y - 0.04, dz + 0.34))
    out += ['AttributeBegin', 'Material "coateddiffuse" "rgb reflectance" [ 0.24 0.12 0.06 ] "float roughness" [ 0.12 ]', kit.mesh_text(*merged(desk)), 'AttributeEnd']
    # on the desk: an open book (two pages, a leather cover under them), loose papers, the glass ball under the lamp
    bx, bz = dx + 0.15, dz + 0.05
    cover = box(bx - 0.26, DESK_Y, bz - 0.17, bx + 0.26, DESK_Y + 0.012, bz + 0.17)
    out += ['AttributeBegin', 'Material "diffuse" "rgb reflectance" [ 0.30 0.07 0.05 ]', kit.mesh_text(*cover), 'AttributeEnd']
    P, idx, N, UV = kit.grid(16, 1, lambda u, v: ((bx - 0.25 + 0.5 * u, DESK_Y + 0.016 + 0.022 * math.sin(math.pi * u) ** 0.6 * (1 if abs(u - 0.5) > 0.02 else 0.3),
                                                   bz - 0.16 + 0.32 * v), (u, v)))
    out += ['AttributeBegin', 'Material "diffuse" "texture reflectance" "pages"', kit.mesh_text(P, idx, N, UV), 'AttributeEnd']
    papers = [box(dx - 0.62, DESK_Y, dz - 0.25, dx - 0.40, DESK_Y + 0.002, dz + 0.05), box(dx - 0.58, DESK_Y + 0.002, dz - 0.10, dx - 0.36, DESK_Y + 0.004, dz + 0.18)]
    out += ['AttributeBegin', 'Material "diffuse" "rgb reflectance" [ 0.88 0.86 0.80 ]', kit.mesh_text(*merged(papers)), 'AttributeEnd']
    out += ['AttributeBegin', 'Material "dielectric" "float eta" [ 1.5 ]', 'Translate %.3f %.3f %.3f' % (dx - 0.22, DESK_Y + 0.045, dz - 0.18),
            'Shape "sphere" "float radius" [ 0.045 ]', 'AttributeEnd']
    # the lamp: a brass base and arm, a dark green enamel shade (white inside), the bulb under it - the only light in the room besides the moon
    lx, lz = dx - 0.45, dz - 0.22
    lamp = [cylinder(lx, lz, 0.08, DESK_Y, DESK_Y + 0.025), cylinder(lx, lz, 0.012, DESK_Y + 0.025, DESK_Y + 0.42)]
    out += ['AttributeBegin', 'Material "conductor" "rgb eta" [ 0.27 0.68 1.12 ] "rgb k" [ 3.61 2.62 2.29 ] "float roughness" [ 0.2 ]', kit.mesh_text(*merged(lamp)),
            'AttributeEnd']
    sx, sy, sz = lx + 0.14, DESK_Y + 0.40, lz + 0.06    # the shade's top, leaning over the book
    shade = kit.lathe([(0.025, 0.0), (0.05, -0.04), (0.11, -0.13), (0.125, -0.16)], 48, at=(sx, sy, sz))
    out += ['AttributeBegin', 'Material "coateddiffuse" "rgb reflectance" [ 0.05 0.20 0.10 ] "float roughness" [ 0.08 ]', kit.mesh_text(*shade), 'AttributeEnd']
    inner = kit.lathe([(0.024, -0.002), (0.049, -0.042), (0.109, -0.132), (0.123, -0.160)], 48, at=(sx, sy, sz))
    out += ['AttributeBegin', 'Material "diffuse" "rgb reflectance" [ 0.85 0.83 0.78 ]', kit.mesh_text(inner[0], inner[1], -inner[2], inner[3]), 'AttributeEnd']
    out += ['AttributeBegin', '  AreaLightSource "diffuse" "rgb L" [ 900 660 380 ]', '  Material "diffuse" "rgb reflectance" [ 0 0 0 ]',
            '  Translate %.3f %.3f %.3f' % (sx, sy - 0.07, sz), '  Shape "sphere" "float radius" [ 0.028 ]', 'AttributeEnd']
    # the globe on its stand, beside the desk
    gx, gz = dx + 1.05, dz + 0.15
    stand = [cylinder(gx, gz, 0.16, 0.0, 0.03), cylinder(gx, gz, 0.02, 0.03, 0.78)]
    ring = kit.lathe([(0.315 * math.cos(a), 1.02 + 0.315 * math.sin(a)) for a in np.linspace(-math.pi / 2, math.pi / 2, 24)], 6, at=(gx, 0, gz))
    out += ['AttributeBegin', 'Material "conductor" "rgb eta" [ 0.27 0.68 1.12 ] "rgb k" [ 3.61 2.62 2.29 ] "float roughness" [ 0.25 ]', kit.mesh_text(*merged(stand)),
            'AttributeEnd']
    out += ['AttributeBegin', 'Material "coateddiffuse" "texture reflectance" "globe" "float roughness" [ 0.1 ]', 'Translate %.3f 1.02 %.3f' % (gx, gz),
            'Rotate -90 1 0 0', 'Shape "sphere" "float radius" [ 0.28 ]', 'AttributeEnd']
    # the reading chair on its rug, in the moonlight, turned towards the desk
    cx, cz = 1.2, -2.05
    out += ['AttributeBegin', 'Material "diffuse" "texture reflectance" "rug"', 'Translate %.3f 0.004 %.3f' % (0.3, -1.15), 'Rotate -90 1 0 0',
            'Shape "disk" "float radius" [ 1.1 ]', 'AttributeEnd']
    chair = [box(-0.40, 0.12, -0.38, 0.40, 0.46, 0.38), box(-0.40, 0.46, 0.22, 0.40, 1.05, 0.40), box(-0.50, 0.12, -0.38, -0.36, 0.68, 0.40),
             box(0.36, 0.12, -0.38, 0.50, 0.68, 0.40)]
    legs = [box(sx_ * 0.42 - 0.025, 0.0, sz_ * 0.33 - 0.025, sx_ * 0.42 + 0.025, 0.12, sz_ * 0.33 + 0.025) for sx_ in (-1, 1) for sz_ in (-1, 1)]
    out += ['AttributeBegin', 'Translate %.3f 0 %.3f' % (cx, cz), 'Rotate -125 0 1 0', 'Material "diffuse" "rgb reflectance" [ 0.38 0.08 0.07 ]',
            kit.mesh_text(*merged(chair)), 'Material "diffuse" "rgb reflectance" [ 0.12 0.07 0.04 ]', kit.mesh_text(*merged(legs)), 'AttributeEnd']
    # the low shelves along the right wall, full of books
    rng = np.random.default_rng(71)
    shelf = [box(W - 0.32, 0.0, -2.7, W, 0.02, 1.2), box(W - 0.32, 1.08, -2.7, W, 1.12, 1.2)]
    for y in (0.36, 0.72):
        shelf.append(box(W - 0.32, y, -2.7, W, y + 0.02, 1.2))
    for z in (-2.7, -1.35, 0.0, 1.2):
        shelf.append(box(W - 0.32, 0.0, z - 0.02, W, 1.12, z))
    out += ['AttributeBegin', 'Material "diffuse" "rgb reflectance" [ 0.26 0.16 0.09 ]', kit.mesh_text(*merged(shelf)), 'AttributeEnd']
    groups = [[] for _ in BOOK_COLOURS]
    for y in (0.02, 0.38, 0.74):
        for z0, z1 in ((-2.68, -1.37), (-1.33, -0.02), (0.02, 1.18)):
            books(W - 0.30, z0, z1, y, 0.26, rng, groups)
    for c, g in zip(BOOK_COLOURS, groups):
        out += ['AttributeBegin', 'Material "coateddiffuse" "rgb reflectance" [ %.2f %.2f %.2f ] "float roughness" [ 0.3 ]' % c, kit.mesh_text(*merged(g)), 'AttributeEnd']
    # an oil lamp on top of the shelves: a brass foot, a glass chimney, its flame - the warm light on the books and the roof over them
    ox, oy, oz = W - 0.16, 1.12, -0.35
    foot = kit.lathe([(0.0, 0.0), (0.06, 0.0), (0.06, 0.01), (0.03, 0.03), (0.055, 0.07), (0.05, 0.10), (0.02, 0.11), (0.0, 0.11)], 32, at=(ox, oy, oz))
    out += ['AttributeBegin', 'Material "conductor" "rgb eta" [ 0.27 0.68 1.12 ] "rgb k" [ 3.61 2.62 2.29 ] "float roughness" [ 0.2 ]', kit.mesh_text(*foot), 'AttributeEnd']
    chimney = kit.lathe([(0.022, 0.11), (0.035, 0.15), (0.03, 0.20), (0.022, 0.27)], 32, at=(ox, oy, oz))
    out += ['AttributeBegin', 'Material "thindielectric" "float eta" [ 1.5 ]', kit.mesh_text(*chimney), 'AttributeEnd']
    out += ['AttributeBegin', '  AreaLightSource "diffuse" "rgb L" [ 700 420 150 ]', '  Material "diffuse" "rgb reflectance" [ 0 0 0 ]',
            '  Translate %.3f %.3f %.3f' % (ox, oy + 0.16, oz), '  Scale 1 1.8 1', '  Shape "sphere" "float radius" [ 0.011 ]', 'AttributeEnd']
    open(os.path.join(OUT, "attic_study.pbrt"), "w").write("\n".join(out) + "\n")
    print("wrote", os.path.join(OUT, "attic_study.pbrt"), "-", len(kit.MESHES), "meshes")


if __name__ == "__main__":
    main()
