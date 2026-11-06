# chess_terrace.py [OUT_DIR]: "The Chess Terrace" - an original set for the teaching film pbrt_scenes/how-a-lens-focuses.film: a game of chess on a round stone table on a terrace
# at dusk, a candle beside the board, and strings of warm lights across the dark garden beyond the balustrade. Written as a pbrt-v4 scene (chess_terrace.pbrt) with its
# meshes (meshes/*.ply) and textures (PNG) beside it - by default into pbrt_scenes/film_assets/how-a-lens-focuses. Metres, +y up; the board's centre is at the origin
# (x, z) on the table's top, white's side towards +z, the garden towards -z. Run again after a change here; the scene and its pictures are committed as written.
#   the board: 5.5 cm squares of maple and walnut in a walnut frame with a boxwood line; the pieces turned (lathes) - pale boxwood and ebony, a game in its middle
#   the table: a round grey-marble top on a turned pedestal; a beeswax candle in a brass holder, lit
#   the terrace: limestone slabs, a stone balustrade 2.2 m behind the table; beyond it, a lawn, a dark hedge and trees, and four strings of lights between posts
# The set is made for the lesson: things at every distance from the camera - the near pieces, the far ones, the candle, the lights metres away - so a lens can put one
# distance in focus and show the rest blurred, and the small bright lights blur into the round discs a real lens makes of them.
import math, os, sys
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sun_hall as kit   # (the meshes as PLY files, the lathes and grids, the noise and marble pictures: the Sun Hall's own)

OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "pbrt_scenes", "film_assets", "how-a-lens-focuses")
os.makedirs(OUT, exist_ok=True)
kit.OUT = OUT          # (kit.save and kit.mesh_text write beside this set's scene)
kit.MESHES.clear()

SQ = 0.055             # a square of the board
TOP = 0.76             # the table's top
BOARD_T = 0.015        # the board's thickness: the pieces stand on TOP + BOARD_T
FRAME = 0.035          # the board's frame, round the squares
TABLE_R = 0.56
STAND = TOP + BOARD_T


# ---- pictures ------------------------------------------------------------------------------------------------------------------------------------------------
def wood(n, m, base, dark, seed, rings=40.0):
    """Wood grain along x: long streaks of `dark` over `base`, a little wavy."""
    f = kit.fbm(max(n, m), seed)[:m, :n]
    y, x = np.mgrid[0:m, 0:n] / max(n, m)
    g = 0.5 + 0.5 * np.sin((y + 0.04 * np.sin(x * 9.0 + 3.0 * f)) * rings * math.pi + 5.0 * f)
    g = g ** 3
    return np.array(base)[None, None, :] * (1 - g[..., None] * 0.35) + np.array(dark)[None, None, :] * g[..., None] * 0.35


def board_picture(n=2048):
    # the board's top seen from above, u along x (file a to h), v along -z (rank 1 to 8): the frame, a boxwood line, the 64 squares, each of its own piece of wood
    side = 8 * SQ + 2 * FRAME
    img = wood(n, n, (0.30, 0.17, 0.09), (0.12, 0.06, 0.03), 31, 55.0)   # walnut: the frame and the dark squares
    y, x = (np.mgrid[n - 1:-1:-1, 0:n] + 0.5) / n * side - FRAME          # (metres from the a1 corner of the squares; row 0 the far edge)
    inside = (x >= 0) & (x < 8 * SQ) & (y >= 0) & (y < 8 * SQ)
    maple = wood(n, n, (0.86, 0.72, 0.50), (0.62, 0.45, 0.26), 32, 70.0)
    walnut2 = wood(n, n, (0.34, 0.20, 0.11), (0.14, 0.07, 0.04), 33, 60.0)
    fi, ri = np.floor(x / SQ), np.floor(y / SQ)
    light = inside & (((fi + ri) % 2) == 1)
    dark = inside & (((fi + ri) % 2) == 0)
    img[light] = maple[light]
    img[dark] = walnut2[dark]
    # each square's grain a little lighter or darker than its neighbours'
    shade = np.random.default_rng(34).uniform(0.9, 1.08, (8, 8))
    k = np.where(inside, shade[np.clip(ri, 0, 7).astype(int), np.clip(fi, 0, 7).astype(int)], 1.0)
    img = img * k[..., None]
    d = np.maximum(np.maximum(-x, x - 8 * SQ), np.maximum(-y, y - 8 * SQ))
    img[(d > 0.006) & (d < 0.010)] = (0.80, 0.68, 0.45)   # the boxwood line
    kit.save("board.png", img)


def slab_picture(n=2048):
    # 4 m of terrace: limestone slabs 0.8 m by 0.4-0.8 m in running courses, each a slightly different stone, the joints dark
    img = np.zeros((n, n, 3))
    stone = kit.marble(n, (0.70, 0.66, 0.58), (0.55, 0.52, 0.47), 41, 3.0)
    y, x = (np.mgrid[0:n, 0:n] + 0.5) / n * 4.0
    row = np.floor(y / 0.8)
    xs = x + (row % 2) * 0.4
    col = np.floor(xs / 0.8)
    rng = np.random.default_rng(42)
    tint = rng.uniform(0.86, 1.08, (6, 8))
    img[:] = stone * tint[row.astype(int) % 6, col.astype(int) % 8][..., None]
    joint = (np.abs(y - (row + 0.5) * 0.8) > 0.392) | (np.abs(xs - (col + 0.5) * 0.8) > 0.392)
    img[joint] *= 0.45
    kit.save("slabs.png", img)


def pictures():
    board_picture()
    slab_picture()
    kit.save("table_marble.png", kit.marble(1024, (0.58, 0.58, 0.60), (0.32, 0.32, 0.35), 43, 5.0))
    kit.save("balustrade.png", kit.marble(512, (0.78, 0.74, 0.66), (0.62, 0.58, 0.52), 44, 4.0))
    hedge = kit.fbm(512, 45)
    kit.save("hedge.png", np.stack([0.025 + 0.03 * hedge, 0.055 + 0.06 * hedge, 0.025 + 0.025 * hedge], -1))
    kit.save("lawn.png", np.stack([0.04 + 0.03 * kit.fbm(512, 46), 0.08 + 0.05 * kit.fbm(512, 47), 0.03 + 0.02 * kit.fbm(512, 48)], -1))


# ---- shapes --------------------------------------------------------------------------------------------------------------------------------------------------
def merged(parts):
    """Several (P, idx[, N, UV]) meshes as one: one PLY file and one Shape line instead of one for each baluster or bulb."""
    Ps, Is, Ns, Us, base = [], [], [], [], 0
    for part in parts:
        P = np.asarray(part[0], dtype=np.float64)
        Ps.append(P)
        Is.append(np.asarray(part[1]) + base)
        Ns.append(np.asarray(part[2]) if len(part) > 2 and part[2] is not None else None)
        Us.append(np.asarray(part[3]) if len(part) > 3 and part[3] is not None else np.zeros((len(P), 2)))
        base += len(P)
    N = None if any(n is None for n in Ns) else np.vstack(Ns)
    return np.vstack(Ps), np.concatenate(Is), N, np.vstack(Us)


def box(x0, y0, z0, x1, y1, z1, uvscale=1.0):
    """An axis-aligned box: four corners a face, each face its own (flat normals), uv in metres times uvscale."""
    P, idx, N, UV = [], [], [], []
    faces = [((0, 1, 0), [(x0, y1, z0), (x1, y1, z0), (x1, y1, z1), (x0, y1, z1)]), ((0, -1, 0), [(x0, y0, z1), (x1, y0, z1), (x1, y0, z0), (x0, y0, z0)]),
             ((0, 0, 1), [(x0, y0, z1), (x0, y1, z1), (x1, y1, z1), (x1, y0, z1)]), ((0, 0, -1), [(x1, y0, z0), (x1, y1, z0), (x0, y1, z0), (x0, y0, z0)]),
             ((1, 0, 0), [(x1, y0, z1), (x1, y1, z1), (x1, y1, z0), (x1, y0, z0)]), ((-1, 0, 0), [(x0, y0, z0), (x0, y1, z0), (x0, y1, z1), (x0, y0, z1)])]
    for n, quad in faces:
        b = len(P)
        for p in quad:
            P.append(p)
            N.append(n)
            a = [abs(c) for c in n]
            UV.append(((p[2] if a[0] else p[0]) * uvscale, (p[2] if a[1] else p[1]) * uvscale))
        idx += [b, b + 1, b + 2, b, b + 2, b + 3]
    return np.array(P), idx, np.array(N, dtype=float), np.array(UV)


def profile_with_ball(profile, ball_r, ball_y, steps=10):
    """A lathe profile ending in a ball on its top (the pawn's head, the bishop's and the queen's finial): the profile, then the ball's outline from its neck to its top."""
    out = list(profile)
    for k in range(steps + 1):
        a = -math.pi / 2 + math.pi * k / steps
        r = ball_r * math.cos(a)
        y = ball_y + ball_r * math.sin(a)
        if k == 0 and r < 1e-6:
            continue
        out.append((max(r, 0.0), y))
    return out


BASE = [(0.0, 0.0), (0.0195, 0.0), (0.0195, 0.004), (0.0175, 0.0055), (0.0175, 0.0075), (0.014, 0.011)]
PIECES = {   # (radius, height) profiles, metres, from the foot up; a king 9.6 cm tall, a pawn 5 cm, as tournament pieces are
    "pawn": profile_with_ball([(0.0, 0.0), (0.016, 0.0), (0.016, 0.0035), (0.0145, 0.005), (0.0145, 0.0065), (0.011, 0.009), (0.0075, 0.019), (0.006, 0.028),
                               (0.0095, 0.030), (0.0095, 0.032), (0.0055, 0.034)], 0.0095, 0.0415),
    "rook": [(0.0, 0.0), (0.018, 0.0), (0.018, 0.004), (0.016, 0.0055), (0.016, 0.0075), (0.0125, 0.011), (0.0105, 0.030), (0.0115, 0.040), (0.0145, 0.042),
             (0.0145, 0.056), (0.0105, 0.056), (0.0105, 0.051), (0.0, 0.051)],
    "bishop": profile_with_ball(BASE + [(0.0085, 0.030), (0.0065, 0.040), (0.010, 0.042), (0.010, 0.044), (0.006, 0.046), (0.0095, 0.052), (0.0105, 0.060),
                                        (0.0085, 0.068), (0.004, 0.0735)], 0.004, 0.077, 6),
    "queen": profile_with_ball(BASE + [(0.0095, 0.035), (0.0075, 0.052), (0.012, 0.055), (0.012, 0.0575), (0.0075, 0.060), (0.009, 0.068), (0.0135, 0.077),
                                       (0.0115, 0.079), (0.006, 0.081)], 0.0055, 0.0855, 6),
    "king": BASE + [(0.010, 0.038), (0.008, 0.056), (0.0125, 0.059), (0.0125, 0.0615), (0.008, 0.064), (0.0095, 0.071), (0.0125, 0.079), (0.0105, 0.081),
                    (0.0045, 0.082), (0.0, 0.082)],
}


def piece(kind, x, z):
    P, idx, N, UV = kit.lathe(PIECES[kind], 48, at=(x, STAND, z))
    parts = [(P, idx, N, UV)]
    if kind == "king":   # its cross: two little bars on the crown
        parts.append(box(x - 0.0022, STAND + 0.081, z - 0.0022, x + 0.0022, STAND + 0.098, z + 0.0022))
        parts.append(box(x - 0.0068, STAND + 0.0885, z - 0.0022, x + 0.0068, STAND + 0.0925, z + 0.0022))
    if kind == "rook":   # the battlements: four notches cut by four teeth standing on the rim
        for k in range(4):
            a = math.pi / 4 + k * math.pi / 2
            cx, cz = x + 0.0125 * math.cos(a), z + 0.0125 * math.sin(a)
            parts.append(box(cx - 0.0035, STAND + 0.056, cz - 0.0035, cx + 0.0035, STAND + 0.061, cz + 0.0035))
    return parts


def square(name):
    """The board's square `name` (e.g. "e4") as x, z: files a..h along +x, ranks 1..8 along -z (white's side towards +z, the camera's)."""
    f = "abcdefgh".index(name[0])
    r = int(name[1])
    return (f - 3.5) * SQ * FILE_SIGN, (4.5 - r) * SQ


FILE_SIGN = -1.0   # (the a-file at +x: pbrt's LookAt from +z towards -z puts +x on the picture's left, so white's a-file is on the left as a player sees it)
# a game in its middle (a Ruy Lopez some twenty moves in): white has castled; black's queen has come out; a knight each was traded off
WHITE = {"king": ["g1"], "queen": ["d2"], "rook": ["a1", "e1"], "bishop": ["c2", "e3"],
         "pawn": ["a3", "b2", "c3", "d4", "e4", "f2", "g2", "h3"]}
BLACK = {"king": ["g8"], "queen": ["c7"], "rook": ["a8", "e8"], "bishop": ["b7", "e7"],
         "pawn": ["a6", "b5", "c5", "d6", "e5", "f7", "g7", "h6"]}


def catenary(a, b, sag, n):
    """n+1 points hanging from a to b, `sag` metres low in the middle."""
    a, b = np.array(a, float), np.array(b, float)
    pts = []
    for i in range(n + 1):
        t = i / n
        p = a + (b - a) * t
        p[1] -= sag * 4.0 * t * (1 - t)
        pts.append(p)
    return pts


def tube(pts, r, sides=6):
    """A thin tube along a polyline (a light string's wire)."""
    def f(u, v):
        k = min(len(pts) - 2, int(v * (len(pts) - 1)))
        t = v * (len(pts) - 1) - k
        p = pts[k] + (pts[k + 1] - pts[k]) * t
        d = pts[k + 1] - pts[k]
        d /= np.linalg.norm(d)
        side = np.cross(d, [0.0, 1.0, 0.0])
        side /= max(1e-9, np.linalg.norm(side))
        up = np.cross(side, d)
        a = 2 * math.pi * u
        return tuple(p + (side * math.cos(a) + up * math.sin(a)) * r), (u, v)
    return kit.grid(sides, len(pts) - 1, f)


# four strings of lights across the garden, each from post to post (metres): near, middle, far, farthest - their discs grow with their distance from the focus
STRINGS = [((-3.4, 2.45, -3.0), (3.6, 2.55, -3.6), 0.45, 21), ((-4.6, 2.7, -5.6), (4.2, 2.6, -6.2), 0.55, 25),
           ((-5.5, 2.9, -8.8), (5.8, 3.0, -9.4), 0.65, 29), ((-7.0, 3.2, -12.5), (6.5, 3.3, -13.0), 0.75, 31)]
BULB_COLOURS = [(1.0, 0.70, 0.36), (1.0, 0.78, 0.48), (1.0, 0.62, 0.30), (1.0, 0.74, 0.55), (0.98, 0.56, 0.36)]


def main():
    pictures()
    out = ['# The Chess Terrace - an original set: chess at dusk by candlelight, strings of lights across the garden (written by chess_terrace.py; see its header).',
           'LookAt 0.05 0.86 0.48   0.0 0.80 -0.10   0 1 0',
           'Camera "perspective" "float fov" [ 38 ] "float lensradius" [ 0.012 ] "float focaldistance" [ 0.45 ]',
           'Film "rgb" "integer xresolution" [ 1920 ] "integer yresolution" [ 1080 ]', 'Sampler "halton" "integer pixelsamples" [ 64 ]', 'WorldBegin',
           '# the dusk sky: a deep blue glow over everything (no sun: it has set)',
           'LightSource "infinite" "rgb L" [ 0.045 0.065 0.130 ]',
           '# a paper lantern hanging high over the table, out of every shot: the warm light the board is lit by',
           'AttributeBegin', '  AreaLightSource "diffuse" "rgb L" [ 55.0 38.5 21.0 ]', '  Material "diffuse" "rgb reflectance" [ 0.8 0.75 0.65 ]',
           '  Translate -0.15 2.60 -0.02', '  Shape "sphere" "float radius" [ 0.20 ]', 'AttributeEnd']
    textures = [("board", "board.png"), ("slabs", "slabs.png"), ("table_marble", "table_marble.png"), ("balustrade", "balustrade.png"), ("hedge", "hedge.png"),
                ("lawn", "lawn.png")]
    out += ['Texture "%s" "spectrum" "imagemap" "string filename" [ "%s" ]' % t for t in textures]
    # the terrace: slabs from the balustrade to well behind the camera; the lawn below and beyond it
    P, idx, N, UV = kit.grid(1, 1, lambda u, v: ((-8 + 16 * u, 0.0, -2.35 + 9.0 * v), ((-8 + 16 * u) / 4.0, (-2.35 + 9.0 * v) / 4.0)))
    out += ['AttributeBegin', 'Material "diffuse" "texture reflectance" "slabs"', kit.mesh_text(P, idx, np.tile([0, 1, 0], (len(P), 1)), UV), 'AttributeEnd']
    P, idx, N, UV = kit.grid(1, 1, lambda u, v: ((-40 + 80 * u, -0.6, -40.0 + 37.6 * v), ((-40 + 80 * u) / 3.0, (-40.0 + 37.6 * v) / 3.0)))
    out += ['AttributeBegin', 'Material "diffuse" "texture reflectance" "lawn"', kit.mesh_text(P, idx, np.tile([0, 1, 0], (len(P), 1)), UV), 'AttributeEnd']
    # the terrace's wall down to the lawn, under the balustrade
    out += ['AttributeBegin', 'Material "diffuse" "texture reflectance" "balustrade"', kit.mesh_text(*box(-8, -0.6, -2.55, 8, 0.0, -2.35, 1.0)), 'AttributeEnd']
    # the balustrade: a plinth, turned balusters every 25 cm, a rail
    bal = [box(-8, 0.0, -2.52, 8, 0.14, -2.18, 1.0), box(-8, 0.86, -2.55, 8, 0.96, -2.15, 1.0)]
    for k in range(-31, 32):
        x = k * 0.25
        bal.append(kit.lathe([(0.0, 0.14), (0.07, 0.14), (0.07, 0.19), (0.045, 0.24), (0.075, 0.45), (0.06, 0.60), (0.035, 0.70), (0.05, 0.76), (0.07, 0.80),
                              (0.07, 0.86), (0.0, 0.86)], 20, at=(x, 0.0, -2.35)))
    out += ['AttributeBegin', 'Material "diffuse" "texture reflectance" "balustrade"', kit.mesh_text(*merged(bal)), 'AttributeEnd']
    # the garden: a long hedge, and trees behind it (dark shapes against the sky; their crowns lumpy spheres)
    out += ['AttributeBegin', 'Material "diffuse" "texture reflectance" "hedge"', kit.mesh_text(*box(-30, -0.6, -16.5, 30, 1.8, -15.0, 0.5)), 'AttributeEnd']
    rng = np.random.default_rng(51)
    trees = []
    for k in range(14):
        x, z, r = -26 + k * 4.0 + rng.uniform(-1.2, 1.2), -19.5 - rng.uniform(0, 5), rng.uniform(2.2, 3.6)
        trees.append(kit.lathe([(0.0, -0.6), (0.25, -0.6), (0.2, 2.5), (0.0, 2.5)], 10, at=(x, 0, z)))
        crown = kit.lathe([(r * math.sin(math.pi * i / 12), r * (1 - math.cos(math.pi * i / 12))) for i in range(13)], 24, at=(x, 2.0 + r * 0.3, z))
        P = crown[0].copy()
        P += crown[2] * (0.07 * r * (rng.random((len(P), 1)) - 0.5))   # (a little lumpy)
        trees.append((P, crown[1], None, crown[3]))
    out += ['AttributeBegin', 'Material "diffuse" "rgb reflectance" [ 0.02 0.035 0.02 ]', kit.mesh_text(*merged(trees)), 'AttributeEnd']
    # the strings of lights: their posts, their wires, their bulbs (small spheres that glow; not sampled as lights - they are far and many, and the lantern lights the set)
    posts, wires = [], []
    for a, b, sag, n in STRINGS:
        for p in (a, b):
            posts.append(box(p[0] - 0.05, -0.6, p[2] - 0.05, p[0] + 0.05, p[1] + 0.1, p[2] + 0.05))
        wires.append(tube(catenary(a, b, sag, 60), 0.004))
    out += ['AttributeBegin', 'Material "diffuse" "rgb reflectance" [ 0.06 0.045 0.035 ]', kit.mesh_text(*merged(posts)), kit.mesh_text(*merged(wires)), 'AttributeEnd']
    k = 0
    for a, b, sag, n in STRINGS:
        for p in catenary(a, b, sag, n)[1:-1]:
            c = BULB_COLOURS[k % len(BULB_COLOURS)]
            k += 1
            out += ['AttributeBegin', '  AreaLightSource "diffuse" "rgb L" [ %.2f %.2f %.2f ] "bool sampled" false' % (c[0] * 30, c[1] * 30, c[2] * 30),
                    '  Material "diffuse" "rgb reflectance" [ 0.8 0.8 0.8 ]', '  Translate %.3f %.3f %.3f' % (p[0], p[1] - 0.06, p[2]),
                    '  Shape "sphere" "float radius" [ 0.035 ]', 'AttributeEnd']
    # the table: a turned pedestal on a foot, the marble top
    ped = kit.lathe([(0.0, 0.0), (0.30, 0.0), (0.30, 0.04), (0.22, 0.07), (0.09, 0.12), (0.07, 0.40), (0.09, 0.62), (0.16, 0.68), (0.16, 0.71), (0.0, 0.71)], 64,
                    uscale=2, vscale=1)
    top = kit.lathe([(0.0, 0.71), (TABLE_R - 0.02, 0.71), (TABLE_R, 0.73), (TABLE_R, 0.745), (TABLE_R - 0.012, TOP), (0.0, TOP)], 128, uscale=1, vscale=1)
    out += ['AttributeBegin', 'Material "coateddiffuse" "texture reflectance" "table_marble" "float roughness" [ 0.05 ]', kit.mesh_text(*ped), kit.mesh_text(*top),
            'AttributeEnd']
    # the board: its top (the picture), its sides walnut
    side = 8 * SQ + 2 * FRAME
    h = side / 2
    bt = kit.grid(1, 1, lambda u, v: ((-h + side * u, STAND, h - side * v), (u, v)))
    out += ['AttributeBegin', 'Material "coateddiffuse" "texture reflectance" "board" "float roughness" [ 0.12 ]',
            kit.mesh_text(bt[0], bt[1], np.tile([0, 1, 0], (len(bt[0]), 1)), bt[3]),
            'Material "coateddiffuse" "rgb reflectance" [ 0.22 0.12 0.06 ] "float roughness" [ 0.15 ]',
            kit.mesh_text(*box(-h, TOP, -h, h, STAND - 0.0005, h, 4.0)), 'AttributeEnd']
    # the pieces: boxwood and ebony
    for side_name, pieces, mat in (("white", WHITE, 'Material "coateddiffuse" "rgb reflectance" [ 0.80 0.66 0.44 ] "float roughness" [ 0.10 ]'),
                                   ("black", BLACK, 'Material "coateddiffuse" "rgb reflectance" [ 0.035 0.025 0.02 ] "float roughness" [ 0.06 ]')):
        parts = []
        for kind, squares in pieces.items():
            for s in squares:
                parts += piece(kind, *square(s))
        out += ['# the %s pieces' % side_name, 'AttributeBegin', mat, kit.mesh_text(*merged(parts)), 'AttributeEnd']
    # the candle, lit: a brass holder, a beeswax candle, a flame (a small glowing drop) - near the board's far left corner
    cx, cz = -0.36, -0.20
    holder = kit.lathe([(0.0, TOP), (0.055, TOP), (0.055, TOP + 0.006), (0.02, TOP + 0.02), (0.014, TOP + 0.05), (0.03, TOP + 0.06), (0.03, TOP + 0.066),
                        (0.0, TOP + 0.066)], 48, at=(cx, 0, cz))
    wax = kit.lathe([(0.0, TOP + 0.066), (0.012, TOP + 0.066), (0.012, TOP + 0.20), (0.010, TOP + 0.204), (0.0, TOP + 0.204)], 32, at=(cx, 0, cz))
    out += ['AttributeBegin', 'Material "conductor" "rgb eta" [ 0.27 0.68 1.12 ] "rgb k" [ 3.61 2.62 2.29 ] "float roughness" [ 0.18 ]', kit.mesh_text(*holder),
            'Material "coateddiffuse" "rgb reflectance" [ 0.85 0.66 0.32 ] "float roughness" [ 0.3 ]', kit.mesh_text(*wax), 'AttributeEnd']
    out += ['AttributeBegin', '  AreaLightSource "diffuse" "rgb L" [ 60 34 11 ]', '  Material "diffuse" "rgb reflectance" [ 0 0 0 ]',
            '  Translate %.3f %.3f %.3f' % (cx, TOP + 0.222, cz), '  Scale 1 2.1 1', '  Shape "sphere" "float radius" [ 0.0075 ]', 'AttributeEnd']
    open(os.path.join(OUT, "chess_terrace.pbrt"), "w").write("\n".join(out) + "\n")
    print("wrote", os.path.join(OUT, "chess_terrace.pbrt"), "-", len(kit.MESHES), "meshes")


if __name__ == "__main__":
    main()
