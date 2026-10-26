"""Makes the films' rigged animals (src/shared/cartoon/film_animals.h): skinned glTF 2.0 models with their clips, in pbrt_scenes/film_assets/library/animals/.

  deer.glb, horse.glb, wolf.glb   a quadruped each, from one generator: a skeleton of a pelvis and a chest (the spine), a neck and a head, a tail, and four legs of three
                                  bones (the upper leg, the lower leg, the foot), and a body lofted round it - a barrel, a neck, a head with its muzzle, ears, eyes, a tail,
                                  hooves or paws, a deer's antlers, a horse's mane - skinned smoothly across the joints (each ring of a tube weighted between the bones it
                                  spans). Clips:
                                    walk    the four-beat walk (left hind, left fore, right hind, right fore a quarter of a cycle apart; each foot down for 0.65 of it)
                                    trot    the two-beat trot (the diagonal pairs together, each foot down for 0.45)
                                    run     the rotary gallop (the hind pair, then the fore pair, each foot down for 0.3; the spine flexing and the head nodding)
                                    graze   the head down in the grass, nibbling, lifting a little now and then (4 s)
                                    idle    standing: the head turning to look, the ears and the tail flicking (5 s)
                                  A cycle of a gait moves the animal 2 L sin(A) / duty (L its leg's length, A the swing of its upper leg): film_animals.h's strides are those.
  eagle.glb, goose.glb            a bird each: a body, a neck and a head with a beak, a fan of a tail, and two wings of three bones (the arm, the forearm, the hand), each a
                                  thin two-sided panel. Clips: flap (a wingbeat: down with the wing spread, up with it folded at the wrist; 1 s) and glide (spread, flexing).

Every model faces +z with y up, +x on its left, standing on y = 0 (a bird with its body at y = 0), in metres at life size. Run it from the repository:
python scripts/make_rigged_animals.py [OUT_DIR]. Standard library only.
"""
import json
import math
import os
import struct
import sys

OUT = sys.argv[1] if len(sys.argv) > 1 else "pbrt_scenes/film_assets/library/animals"


# ---- vectors and quaternions --------------------------------------------------------------------------------------------------------------------------------------------
def add(a, b): return [a[i] + b[i] for i in range(3)]
def sub(a, b): return [a[i] - b[i] for i in range(3)]
def mul(a, k): return [a[i] * k for i in range(3)]
def dot(a, b): return sum(a[i] * b[i] for i in range(3))
def cross(a, b): return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]
def norm(a):
    l = math.sqrt(dot(a, a))
    return [x / l for x in a] if l > 1e-12 else [0.0, 1.0, 0.0]
def lerp(a, b, u): return [a[i] + (b[i] - a[i]) * u for i in range(3)]


def quat(axis, degrees):
    a = math.radians(degrees) / 2.0
    s = math.sin(a)
    ax = norm(axis)
    return [ax[0] * s, ax[1] * s, ax[2] * s, math.cos(a)]


def qmul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return [aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx, aw * bz + ax * by - ay * bx + az * bw, aw * bw - ax * bx - ay * by - az * bz]


X, Y, Z = (1, 0, 0), (0, 1, 0), (0, 0, 1)


# ---- the file -----------------------------------------------------------------------------------------------------------------------------------------------------------
class Builder:
    def __init__(self):
        self.bin = bytearray()
        self.views, self.accessors = [], []

    def add(self, data, ctype, kind, count, extra=None):
        while len(self.bin) % 4:
            self.bin.append(0)
        self.views.append({"buffer": 0, "byteOffset": len(self.bin), "byteLength": len(data)})
        self.bin += data
        acc = {"bufferView": len(self.views) - 1, "componentType": ctype, "count": count, "type": kind}
        if extra:
            acc.update(extra)
        self.accessors.append(acc)
        return len(self.accessors) - 1

    def floats(self, values, kind, per, minmax=False):
        extra = None
        if minmax:
            cols = [values[i::per] for i in range(per)]
            extra = {"min": [min(c) for c in cols], "max": [max(c) for c in cols]}
        return self.add(struct.pack("<%df" % len(values), *values), 5126, kind, len(values) // per, extra)


class Mesh:
    """Vertices with their normals, joints and weights (up to four), triangles; one per material."""
    def __init__(self):
        self.pos, self.nor, self.joints, self.weights, self.idx = [], [], [], [], []

    def vertex(self, p, n, skin):
        skin = sorted(skin, key=lambda jw: -jw[1])[:4]
        total = sum(w for _, w in skin) or 1.0
        j = [jw[0] for jw in skin] + [0] * (4 - len(skin))
        w = [jw[1] / total for jw in skin] + [0.0] * (4 - len(skin))
        self.pos.append(p)
        self.nor.append(norm(n))
        self.joints.append(j)
        self.weights.append(w)
        return len(self.pos) - 1


def tube(mesh, centres, radii, skins, sides=14, up=(0, 1, 0), caps=True):
    """A tube through `centres`, its cross-section at each an ellipse of radii (across, up), each ring skinned as `skins` says ([(joint, weight)...])."""
    rings = []
    n = len(centres)
    for i in range(n):
        t = norm(sub(centres[min(i + 1, n - 1)], centres[max(i - 1, 0)]))
        u = cross(norm(up), t)
        if math.sqrt(dot(u, u)) < 1e-6:
            u = cross([1, 0, 0], t)
        u = norm(u)
        v = cross(t, u)
        ring = []
        for k in range(sides):
            a = 2 * math.pi * k / sides
            off = add(mul(u, math.cos(a) * radii[i][0]), mul(v, math.sin(a) * radii[i][1]))
            nrm = add(mul(u, math.cos(a) / max(1e-6, radii[i][0])), mul(v, math.sin(a) / max(1e-6, radii[i][1])))
            ring.append(mesh.vertex(add(centres[i], off), nrm, skins[i]))
        rings.append(ring)
    for i in range(n - 1):
        for k in range(sides):
            a, b, c, d = rings[i][k], rings[i][(k + 1) % sides], rings[i + 1][(k + 1) % sides], rings[i + 1][k]
            mesh.idx += [a, c, b, a, d, c]
    if caps:
        for end, sign in ((0, -1), (n - 1, 1)):
            t = norm(sub(centres[min(end + 1, n - 1)], centres[max(end - 1, 0)]))
            centre = mesh.vertex(centres[end], mul(t, sign), skins[end])
            for k in range(sides):
                a, b = rings[end][k], rings[end][(k + 1) % sides]
                mesh.idx += [centre, b, a] if sign > 0 else [centre, a, b]


def panel(mesh, outline, thickness, skins, normal):
    """A thin two-sided panel: `outline` (a convex fan from its first point), lifted by thickness/2 each side along `normal`, each point skinned by `skins`."""
    nrm = norm(normal)
    for side in (1, -1):
        base = len(mesh.pos)
        for p, s in zip(outline, skins):
            mesh.vertex(add(p, mul(nrm, side * thickness / 2)), mul(nrm, side), s)
        for k in range(1, len(outline) - 1):
            mesh.idx += [base, base + k, base + k + 1] if side > 0 else [base, base + k + 1, base + k]


def write(path, joints, meshes, materials, clips, b):
    """The file: the skeleton (joints with their rest positions in the model's world), one mesh of a primitive a material, one skin, the clips."""
    primitives = []
    flat = lambda rows: [x for r in rows for x in r]
    for m, mesh in enumerate(meshes):
        if not mesh.pos:
            continue
        primitives.append({
            "attributes": {
                "POSITION": b.floats(flat(mesh.pos), "VEC3", 3, minmax=True),
                "NORMAL": b.floats(flat(mesh.nor), "VEC3", 3),
                "JOINTS_0": b.add(struct.pack("<%dH" % (len(mesh.joints) * 4), *flat(mesh.joints)), 5123, "VEC4", len(mesh.joints)),
                "WEIGHTS_0": b.floats(flat(mesh.weights), "VEC4", 4),
            },
            "indices": b.add(struct.pack("<%dI" % len(mesh.idx), *mesh.idx), 5125, "SCALAR", len(mesh.idx)),
            "material": m,
        })
    nodes = []
    for name, parent, p in joints:
        local = p if parent < 0 else [p[i] - joints[parent][2][i] for i in range(3)]
        nodes.append({"name": name, "translation": [round(x, 6) for x in local]})
    for j, (name, parent, p) in enumerate(joints):
        if parent >= 0:
            nodes[parent].setdefault("children", []).append(j)
    ibm = []
    for name, parent, p in joints:
        ibm += [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -p[0], -p[1], -p[2], 1]
    skin = {"joints": list(range(len(joints))), "inverseBindMatrices": b.floats(ibm, "MAT4", 16), "skeleton": 0}
    nodes.append({"name": os.path.splitext(os.path.basename(path))[0], "mesh": 0, "skin": 0})
    J = {name: i for i, (name, _, _) in enumerate(joints)}
    animations = []
    for name, tracks in clips:
        samplers, channels = [], []
        for joint, kind, times, values in tracks:
            per = 4 if kind == "rotation" else 3
            samplers.append({"input": b.floats(times, "SCALAR", 1, minmax=True), "output": b.floats(flat(values), "VEC4" if per == 4 else "VEC3", per), "interpolation": "LINEAR"})
            channels.append({"sampler": len(samplers) - 1, "target": {"node": J[joint], "path": kind}})
        animations.append({"name": name, "samplers": samplers, "channels": channels})
    doc = {
        "asset": {"version": "2.0", "generator": "scripts/make_rigged_animals.py"},
        "scene": 0,
        "scenes": [{"nodes": [0, len(nodes) - 1]}],
        "nodes": nodes,
        "meshes": [{"name": "body", "primitives": primitives}],
        "skins": [skin],
        "materials": materials,
        "animations": animations,
        "buffers": [{"byteLength": len(b.bin)}],
        "bufferViews": b.views,
        "accessors": b.accessors,
    }
    text = json.dumps(doc, separators=(",", ":")).encode("utf-8")
    while len(text) % 4:
        text += b" "
    while len(b.bin) % 4:
        b.bin.append(0)
    total = 12 + 8 + len(text) + 8 + len(b.bin)
    with open(path, "wb") as f:
        f.write(struct.pack("<4sII", b"glTF", 2, total))
        f.write(struct.pack("<I4s", len(text), b"JSON") + text)
        f.write(struct.pack("<I4s", len(b.bin), b"BIN\0") + bytes(b.bin))
    verts = sum(len(m.pos) for m in meshes)
    print("wrote %s: %d bytes, %d joints, %d vertices, clips %s" % (path, total, len(joints), verts, ", ".join(c[0] for c in clips)))


def material(name, colour, roughness=0.8, metallic=0.0):
    return {"name": name, "pbrMetallicRoughness": {"baseColorFactor": list(colour) + [1.0], "metallicFactor": metallic, "roughnessFactor": roughness}}


# ---- quadrupeds ---------------------------------------------------------------------------------------------------------------------------------------------------------
# Each in metres: shoulder height (H), body length (shoulder to hip, Lb), barrel radii (across, up), neck length and how upright (degrees above level), head length,
# tail length and thickness, ears (length), hooves or paws, antlers, mane; colours (body, belly/legs, hooves, extras); and the gaits' swing (degrees).
SPECIES = {
    "deer": dict(H=1.0, Lb=0.95, barrel=(0.2, 0.25), neck=0.55, neckUp=55, head=0.3, headR=0.08, tail=0.14, tailR=0.04, ears=0.13, hoof=True, antlers=True, mane=False,
                 body=(0.36, 0.21, 0.11), legs=(0.30, 0.19, 0.11), feet=(0.05, 0.04, 0.035), extra=(0.42, 0.36, 0.28), swing=(22, 26, 34)),
    "horse": dict(H=1.55, Lb=1.35, barrel=(0.3, 0.37), neck=0.85, neckUp=45, head=0.6, headR=0.12, tail=0.7, tailR=0.08, ears=0.14, hoof=True, antlers=False, mane=True,
                  body=(0.28, 0.13, 0.06), legs=(0.20, 0.10, 0.05), feet=(0.06, 0.05, 0.045), extra=(0.04, 0.03, 0.025), swing=(20, 25, 33)),
    "wolf": dict(H=0.8, Lb=0.8, barrel=(0.15, 0.2), neck=0.32, neckUp=25, head=0.3, headR=0.09, tail=0.45, tailR=0.07, ears=0.11, hoof=False, antlers=False, mane=False,
                 body=(0.33, 0.30, 0.26), legs=(0.42, 0.39, 0.34), feet=(0.12, 0.11, 0.1), extra=(0.08, 0.07, 0.06), swing=(22, 26, 36)),
}
GAITS = {   # (each foot's start of stance as a share of the cycle: left hind, left fore, right hind, right fore), the share of the cycle a foot is down
    "walk": ((0.0, 0.25, 0.5, 0.75), 0.65),
    "trot": ((0.0, 0.5, 0.5, 0.0), 0.45),
    "run": ((0.0, 0.40, 0.08, 0.50), 0.30),
}
LEGS = [("hl", -1, 1.0), ("fl", 1, 1.0), ("hr", -1, -1.0), ("fr", 1, -1.0)]   # (name, fore +1 or hind -1, left +1 or right -1)


def quadruped(name, s):
    H, Lb = s["H"], s["Lb"]
    w = s["barrel"][0] * 0.62                                 # the legs' spread
    hipY, shoulderY = H * 0.97, H * 1.0
    pelvis, chest = [0, hipY, -Lb / 2], [0, shoulderY, Lb / 2]
    na = math.radians(s["neckUp"])
    neckBase = [0, shoulderY + 0.05 * H, Lb / 2 + 0.12 * H]
    headPos = add(neckBase, [0, s["neck"] * math.sin(na), s["neck"] * math.cos(na)])
    tailBase = [0, hipY + 0.02, -Lb / 2 - 0.16 * H]
    joints = [("pelvis", -1, pelvis), ("chest", 0, chest), ("neck", 1, neckBase), ("head", 2, headPos), ("tail", 0, tailBase)]
    legLen = {}
    for leg, fore, side in LEGS:
        top = [side * w, (shoulderY if fore > 0 else hipY) - 0.1 * H, (Lb / 2 if fore > 0 else -Lb / 2) + (0.0 if fore > 0 else -0.03 * H)]
        knee = [side * w, 0.52 * H if fore > 0 else 0.46 * H, top[2] + (0.02 * H if fore > 0 else -0.10 * H)]
        foot = [side * w, 0.14 * H, top[2] + (0.03 * H if fore > 0 else -0.05 * H)]
        parent = 1 if fore > 0 else 0
        joints += [(leg + ".upper", parent, top), (leg + ".lower", len(joints), knee), (leg + ".foot", len(joints) + 1, foot)]
        legLen[leg] = top[1]
    J = {n: i for i, (n, _, _) in enumerate(joints)}
    P = {n: p for n, _, p in joints}
    body, belly, feet, extra, eyes = Mesh(), Mesh(), Mesh(), Mesh(), Mesh()

    # the barrel: from behind the hips to before the chest, deepest at the girth, the belly line a little up towards the flank
    centres, radii, skins = [], [], []
    rx, ry = s["barrel"]
    z0, z1 = -Lb / 2 - 0.22 * H, Lb / 2 + 0.2 * H
    for i in range(19):
        u = i / 18.0
        z = z0 + (z1 - z0) * u
        shape = max(0.12, math.sin(math.pi * (0.015 + 0.97 * u))) ** 0.42   # (rounded at the rump and the breast, not cut off)
        girth = 1.0 + 0.12 * math.exp(-((u - 0.68) / 0.18) ** 2)   # (deeper behind the shoulders)
        y = lerp(pelvis, chest, u)[1] - 0.03 * H + 0.02 * H * math.sin(math.pi * u)
        centres.append([0, y, z])
        radii.append((rx * shape * (0.9 + 0.1 * girth), ry * shape * girth))
        wc = min(1.0, max(0.0, (u - 0.35) / 0.3))
        skins.append([(J["pelvis"], 1 - wc), (J["chest"], wc)])
    tube(body, centres, radii, skins, sides=16)
    # the neck: from the top of the chest up to the head, thick at its base
    centres, radii, skins = [], [], []
    for i in range(7):
        u = i / 6.0
        p = lerp(add(neckBase, [0, -0.08 * H, -0.05 * H]), headPos, u)
        r = (0.55 * rx * (1 - 0.45 * u), 0.75 * ry * (1 - 0.55 * u))
        centres.append(p)
        radii.append(r)
        skins.append([(J["chest"], max(0.0, 1 - 2.5 * u)), (J["neck"], min(1.0, 2.5 * u) * (1 - max(0.0, (u - 0.7) / 0.3))), (J["head"], max(0.0, (u - 0.7) / 0.3))])
    tube(body, centres, radii, skins, sides=12)
    # the head: forward and down from the top of the neck to the muzzle
    hd = [0, -math.sin(math.radians(35)), math.cos(math.radians(35))]
    centres, radii = [], []
    for i in range(6):
        u = i / 5.0
        centres.append(add(headPos, mul(hd, (u - 0.15) * s["head"])))
        k = 1.0 - 0.45 * u
        radii.append((s["headR"] * k * (0.9 if u > 0.1 else 1.0), s["headR"] * 1.15 * k))
    tube(body, centres, radii, [[(J["head"], 1.0)]] * 6, sides=12)
    for side in (1, -1):   # eyes and ears
        e = add(headPos, add(mul(hd, 0.12 * s["head"]), [side * s["headR"] * 0.85, s["headR"] * 0.35, 0]))
        tube(eyes, [add(e, [-side * 0.006, 0, 0]), add(e, [side * 0.006, 0, 0])], [(0.016 * H, 0.016 * H)] * 2, [[(J["head"], 1.0)]] * 2, sides=8)
        eb = add(headPos, [side * s["headR"] * 0.6, s["headR"] * 0.9, -0.05 * s["head"]])
        et = add(eb, [side * 0.35 * s["ears"], 0.9 * s["ears"], -0.15 * s["ears"]])
        tube(body, [eb, lerp(eb, et, 0.5), et], [(0.25 * s["ears"], 0.12 * s["ears"]), (0.22 * s["ears"], 0.1 * s["ears"]), (0.02 * s["ears"], 0.02 * s["ears"])], [[(J["head"], 1.0)]] * 3, sides=8)
    if s["antlers"]:   # a deer's antlers: a beam back and up from each side of the crown, with tines forward
        for side in (1, -1):
            base = add(headPos, [side * s["headR"] * 0.45, s["headR"] * 1.0, 0.0])
            beam = [base, add(base, [side * 0.08, 0.16, -0.06]), add(base, [side * 0.16, 0.30, -0.04]), add(base, [side * 0.19, 0.44, 0.04]), add(base, [side * 0.16, 0.55, 0.12])]
            tube(extra, beam, [(0.022, 0.022), (0.019, 0.019), (0.016, 0.016), (0.012, 0.012), (0.004, 0.004)], [[(J["head"], 1.0)]] * 5, sides=7)
            for k, at in ((1, 0.1), (2, 0.12), (3, 0.1)):
                p = beam[k]
                tube(extra, [p, add(p, [side * 0.01, at * 0.8, at])], [(0.012, 0.012), (0.003, 0.003)], [[(J["head"], 1.0)]] * 2, sides=6)
    if s["mane"]:   # a horse's mane: a ridge along the top of the neck
        for i in range(8):
            u = i / 7.0
            p = lerp(add(neckBase, [0, 0.1 * H, -0.06 * H]), add(headPos, [0, 0.06, -0.05]), u)
            tube(extra, [add(p, [0, -0.02, 0]), add(p, [0, 0.09, -0.07])], [(0.025, 0.05), (0.01, 0.02)], [[(J["neck"], 1.0 - u), (J["head"], u)]] * 2, sides=6)
    # the tail
    tip = add(tailBase, [0, -s["tail"] * (0.85 if name != "wolf" else 0.6), -s["tail"] * (0.35 if name != "wolf" else 0.75)])
    centres = [lerp(tailBase, tip, u / 5.0) for u in range(6)]
    radii = [(s["tailR"] * (1.0 - 0.5 * u / 5.0) * (1.6 if name == "wolf" and 1 <= u <= 4 else 1.0),) * 2 for u in range(6)]
    tube(extra if name == "horse" else body, centres, radii, [[(J["tail"], 1.0)]] * 6, sides=8)
    # the legs: the upper leg (muscled, inside the body at its top), the lower leg, the foot, a hoof or a paw
    for leg, fore, side in LEGS:
        top, knee, foot = P[leg + ".upper"], P[leg + ".lower"], P[leg + ".foot"]
        ground = [foot[0], 0.0, foot[2] + (0.03 * H if fore > 0 else 0.02 * H)]
        upperTop = add(top, [0, 0.16 * H, 0])
        thick = rx * (0.55 if fore > 0 else 0.75)
        centres = [upperTop, lerp(upperTop, knee, 0.45), add(knee, [0, 0.03 * H, 0]), knee, lerp(knee, foot, 0.5), foot, lerp(foot, ground, 0.6)]
        radii = [(thick, thick * 1.3), (thick * 0.8, thick * 1.0), (0.32 * thick, 0.4 * thick), (0.3 * thick, 0.35 * thick), (0.2 * thick, 0.22 * thick), (0.2 * thick, 0.22 * thick),
                 (0.17 * thick, 0.19 * thick)]
        u_, l_, f_ = J[leg + ".upper"], J[leg + ".lower"], J[leg + ".foot"]
        parentJ = J["chest"] if fore > 0 else J["pelvis"]
        skins = [[(parentJ, 0.5), (u_, 0.5)], [(u_, 1.0)], [(u_, 0.7), (l_, 0.3)], [(u_, 0.3), (l_, 0.7)], [(l_, 1.0)], [(l_, 0.5), (f_, 0.5)], [(f_, 1.0)]]
        tube(belly, centres, radii, skins, sides=10, up=(0, 0, 1))
        if s["hoof"]:
            tube(feet, [add(ground, [0, 0.07 * H, -0.01 * H]), add(ground, [0, 0.002, 0.012 * H])], [(0.21 * thick, 0.2 * thick), (0.26 * thick, 0.28 * thick)], [[(f_, 1.0)]] * 2, sides=10, up=(0, 0, 1))
        else:   # a paw: a pad forward of the foot
            tube(feet, [add(ground, [0, 0.03 * H, -0.03 * H]), add(ground, [0, 0.025 * H, 0.06 * H])], [(0.25 * thick, 0.14 * thick), (0.2 * thick, 0.1 * thick)], [[(f_, 1.0)]] * 2, sides=10)
    materials = [material("coat", s["body"]), material("legs", s["legs"]), material("hooves", s["feet"], 0.5), material("extra", s["extra"], 0.6),
                 material("eyes", (0.02, 0.015, 0.01), 0.15)]
    meshes = [body, belly, feet, extra, eyes]

    # ---- the clips ----
    clips = []
    swings = dict(zip(("walk", "trot", "run"), s["swing"]))
    n = 16
    times = [i / n for i in range(n + 1)]
    pelvisRest, chestRest = pelvis, chest
    for gait, (starts, duty) in GAITS.items():
        A = swings[gait]
        tracks = []
        for (leg, fore, side), start in zip(LEGS, starts):
            up, lo, ft = [], [], []
            for t in times:
                u = (t - start) % 1.0
                if u < duty:   # down: the foot travels back under the body at an even pace
                    phi = A * (1 - 2 * u / duty)
                    flex = 0.0
                else:          # up: swung forward, the lower leg folded to clear the ground
                    v = (u - duty) / (1 - duty)
                    phi = -A + 2 * A * (0.5 - 0.5 * math.cos(math.pi * v))
                    flex = math.sin(math.pi * v) * (45 if gait == "run" else 35)
                # (a rotation about +x turns a leg's foot back: the forward swing is a negative angle) the fore cannon folds back, the hind one forward
                up.append(quat(X, -phi + (0.25 * flex if fore < 0 else 0.0)))
                lo.append(quat(X, flex if fore > 0 else -1.4 * flex))
                ft.append(quat(X, (0.6 * flex if fore > 0 else 0.7 * flex)))
            tracks += [(leg + ".upper", "rotation", times, up), (leg + ".lower", "rotation", times, lo), (leg + ".foot", "rotation", times, ft)]
        beats = 1 if gait == "run" else 2   # (the body bobs once a step: twice a cycle in the walk and trot, once in the gallop)
        bob = [[pelvisRest[0], pelvisRest[1] + (0.03 if gait == "run" else 0.012) * H * math.cos(2 * math.pi * beats * t), pelvisRest[2]] for t in times]
        tracks.append(("pelvis", "translation", times, bob))
        flexA = 7 if gait == "run" else 1.5   # the spine flexing (the gallop's gathering and stretching)
        tracks.append(("pelvis", "rotation", times, [quat(X, flexA * math.sin(2 * math.pi * t)) for t in times]))
        tracks.append(("chest", "rotation", times, [quat(X, -2 * flexA * math.sin(2 * math.pi * t)) for t in times]))
        nod = 9 if gait == "run" else 4 if gait == "walk" else 2
        tracks.append(("neck", "rotation", times, [quat(X, nod * math.sin(2 * math.pi * beats * t + 0.6)) for t in times]))
        tracks.append(("tail", "rotation", times, [quat(X, -(20 if gait == "run" else 6)) for t in times]))
        clips.append((gait, tracks))
    # graze: the neck down so the muzzle is in the grass, nibbling, the head lifting a little
    gt = [i * 0.25 for i in range(17)]
    down = -(s["neckUp"] + 40)
    clips.append(("graze", [
        ("neck", "rotation", gt, [quat(X, -down - 6 * max(0.0, math.sin(2 * math.pi * t / 4.0)) ** 4) for t in gt]),
        ("head", "rotation", gt, [quat(X, 25 + 6 * math.sin(2 * math.pi * t * 1.5)) for t in gt]),
        ("tail", "rotation", gt, [quat(Y, 12 * math.sin(2 * math.pi * t / 2.0) ** 9) for t in gt]),
        ("chest", "rotation", gt, [quat(X, 4) for t in gt]),
        ("fl.upper", "rotation", gt, [quat(X, -8) for t in gt]), ("fr.upper", "rotation", gt, [quat(X, -8) for t in gt]),
    ]))
    # idle: standing, looking round (the head turning), the tail flicking
    it = [i * 0.25 for i in range(21)]
    clips.append(("idle", [
        ("neck", "rotation", it, [qmul(quat(Y, 20 * math.sin(2 * math.pi * t / 5.0)), quat(X, -3 * math.sin(2 * math.pi * t / 2.5))) for t in it]),
        ("head", "rotation", it, [quat(Y, 15 * math.sin(2 * math.pi * t / 5.0 + 0.8)) for t in it]),
        ("tail", "rotation", it, [quat(Y, 15 * math.sin(2 * math.pi * t / 1.25) ** 7) for t in it]),
    ]))
    strides = {g: 2 * legLen["fl"] * math.sin(math.radians(swings[g])) / GAITS[g][1] for g in GAITS}
    return joints, meshes, materials, clips, strides


# ---- birds --------------------------------------------------------------------------------------------------------------------------------------------------------------
BIRDS = {
    "eagle": dict(span=2.0, body=0.45, bodyR=0.09, neck=0.08, head=0.07, beak=0.06, tail=0.3, colours=((0.16, 0.10, 0.06), (0.85, 0.83, 0.78), (0.85, 0.83, 0.78), (0.8, 0.6, 0.1))),
    "goose": dict(span=1.6, body=0.5, bodyR=0.11, neck=0.32, head=0.06, beak=0.05, tail=0.12, colours=((0.32, 0.29, 0.25), (0.05, 0.05, 0.05), (0.85, 0.85, 0.82), (0.05, 0.05, 0.05))),
}


def bird(name, s):
    span, Lb, r = s["span"], s["body"], s["bodyR"]
    shoulder = [r * 0.6, r * 0.3, Lb * 0.15]
    arm, fore = span * 0.16, span * 0.16
    joints = [("body", -1, [0, 0, 0]), ("neck", 0, [0, r * 0.4, Lb * 0.45]), ("head", 1, [0, r * 0.4 + s["neck"] * 0.6, Lb * 0.45 + s["neck"] * 0.8]), ("tail", 0, [0, 0, -Lb * 0.45])]
    for side, tag in ((1, "L"), (-1, "R")):
        sh = [side * shoulder[0], shoulder[1], shoulder[2]]
        joints += [("wing.%s" % tag, 0, sh), ("fore.%s" % tag, len(joints), add(sh, [side * arm, 0, 0])), ("hand.%s" % tag, len(joints) + 1, add(sh, [side * (arm + fore), 0, 0]))]
    J = {n: i for i, (n, _, _) in enumerate(joints)}
    P = {n: p for n, _, p in joints}
    body, head, tail, beak, eyes = Mesh(), Mesh(), Mesh(), Mesh(), Mesh()
    centres = [[0, 0, -Lb * 0.5 + Lb * u / 6.0] for u in range(7)]
    radii = [(r * math.sin(math.pi * (0.1 + 0.8 * u / 6.0)) ** 0.6,) * 2 for u in range(7)]
    tube(body, centres, radii, [[(J["body"], 1.0)]] * 7, sides=12)
    nb, hp = P["neck"], P["head"]
    tube(head if name == "goose" else body, [add(nb, [0, -r * 0.3, -r * 0.2]), lerp(nb, hp, 0.5), hp], [(r * 0.55, r * 0.6), (r * 0.35, r * 0.38), (s["head"] * 0.7, s["head"] * 0.75)],
         [[(J["body"], 0.6), (J["neck"], 0.4)], [(J["neck"], 1.0)], [(J["head"], 1.0)]], sides=10)
    tube(head, [add(hp, [0, 0, -s["head"] * 0.6]), hp, add(hp, [0, -s["head"] * 0.1, s["head"] * 0.7])], [(s["head"] * 0.8,) * 2, (s["head"],) * 2, (s["head"] * 0.6,) * 2],
         [[(J["head"], 1.0)]] * 3, sides=10)
    bb = add(hp, [0, -s["head"] * 0.1, s["head"] * 0.65])
    tube(beak, [bb, add(bb, [0, -s["beak"] * 0.25, s["beak"]])], [(s["head"] * 0.35, s["head"] * 0.4), (0.004, 0.004)], [[(J["head"], 1.0)]] * 2, sides=8)
    for side in (1, -1):
        e = add(hp, [side * s["head"] * 0.75, s["head"] * 0.25, s["head"] * 0.25])
        tube(eyes, [add(e, [-side * 0.004, 0, 0]), add(e, [side * 0.004, 0, 0])], [(0.008, 0.008)] * 2, [[(J["head"], 1.0)]] * 2, sides=6)
    tb = P["tail"]
    panel(tail, [tb, add(tb, [-s["tail"] * 0.45, 0, -s["tail"]]), add(tb, [0, 0, -s["tail"] * 1.05]), add(tb, [s["tail"] * 0.45, 0, -s["tail"]])], 0.012, [[(J["tail"], 1.0)]] * 4, [0, 1, 0])
    # the wings: the arm and forearm a broad panel from the leading edge back, the hand tapering to the tip (an eagle's spread into fingered primaries)
    chord = span * 0.2
    for side, tag in ((1, "L"), (-1, "R")):
        w, f, h = J["wing." + tag], J["fore." + tag], J["hand." + tag]
        sh, el, wr = P["wing." + tag], P["fore." + tag], P["hand." + tag]
        tipX = side * span * 0.5
        lead = lambda p: add(p, [0, 0, chord * 0.25])
        trail = lambda p, k: add(p, [0, 0, -chord * k])
        panel(body, [lead(sh), lead(el), trail(el, 0.85), trail(sh, 0.8)], 0.02, [[(w, 1.0)], [(w, 0.5), (f, 0.5)], [(w, 0.5), (f, 0.5)], [(w, 1.0)]], [0, 1, 0])
        panel(body, [lead(el), lead(wr), trail(wr, 0.8), trail(el, 0.85)], 0.016, [[(w, 0.5), (f, 0.5)], [(f, 0.5), (h, 0.5)], [(f, 0.5), (h, 0.5)], [(w, 0.5), (f, 0.5)]], [0, 1, 0])
        if name == "eagle":
            for k in range(5):   # the primaries: five feathers splayed from the hand, the fingered tip of a soaring bird
                a = math.radians(-8 + 9 * k)
                root = add(wr, [side * 0.04, 0, -chord * (0.1 + 0.14 * k)])
                tip = add(root, [side * span * 0.18 * math.cos(a), 0, span * 0.18 * math.sin(a) * 0.5 - 0.02 * k])
                pw = 0.045
                panel(body, [root, add(tip, [0, 0, pw * 0.3]), add(tip, [0, 0, -pw * 0.3]), add(root, [0, 0, -pw])], 0.008, [[(h, 1.0)]] * 4, [0, 1, 0])
        else:
            panel(body, [lead(wr), [tipX, wr[1], wr[2] + chord * 0.05], trail(wr, 0.7)], 0.012, [[(f, 0.4), (h, 0.6)], [(h, 1.0)], [(f, 0.4), (h, 0.6)]], [0, 1, 0])
    c = s["colours"]
    materials = [material("plumage", c[0]), material("head", c[1]), material("tail", c[2]), material("beak", c[3], 0.4), material("eyes", (0.02, 0.015, 0.01), 0.15)]
    meshes = [body, head, tail, beak, eyes]
    n = 16
    times = [i / n for i in range(n + 1)]
    flap = []
    for tag, side in (("L", 1), ("R", -1)):
        # (about +z, a positive angle raises the left wing; the right wing mirrors it) down with the wing spread, up with it folded at the wrist
        sh = [quat(Z, side * (40 * math.cos(2 * math.pi * t) + 5)) for t in times]
        el = [quat(Y, side * 25 * max(0.0, -math.sin(2 * math.pi * t))) for t in times]
        wr = [quat(Y, -side * 35 * max(0.0, -math.sin(2 * math.pi * t))) for t in times]
        flap += [("wing." + tag, "rotation", times, sh), ("fore." + tag, "rotation", times, el), ("hand." + tag, "rotation", times, wr)]
    flap.append(("body", "translation", times, [[0, -0.04 * math.sin(2 * math.pi * t), 0] for t in times]))
    flap.append(("tail", "rotation", times, [quat(X, 4 * math.sin(2 * math.pi * t)) for t in times]))
    gtimes = [i * 0.5 for i in range(9)]
    glide = []
    for tag, side in (("L", 1), ("R", -1)):
        glide.append(("wing." + tag, "rotation", gtimes, [quat(Z, side * (8 + 2 * math.sin(2 * math.pi * t / 4.0 + side))) for t in gtimes]))
        glide.append(("hand." + tag, "rotation", gtimes, [quat(Z, side * (-5 + 2 * math.sin(2 * math.pi * t / 2.0))) for t in gtimes]))
    glide.append(("tail", "rotation", gtimes, [quat(Y, 4 * math.sin(2 * math.pi * t / 4.0)) for t in gtimes]))
    return joints, meshes, materials, [("flap", flap), ("glide", glide)]


def main():
    os.makedirs(OUT, exist_ok=True)
    for name, s in SPECIES.items():
        joints, meshes, materials, clips, strides = quadruped(name, s)
        write(os.path.join(OUT, name + ".glb"), joints, meshes, materials, clips, Builder())
        print("   strides (m a cycle): " + ", ".join("%s %.3f" % (g, v) for g, v in strides.items()))
    for name, s in BIRDS.items():
        joints, meshes, materials, clips = bird(name, s)
        write(os.path.join(OUT, name + ".glb"), joints, meshes, materials, clips, Builder())


if __name__ == "__main__":
    main()
