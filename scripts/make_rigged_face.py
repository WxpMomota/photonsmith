"""Makes pbrt_scenes/film_assets/library/mira.glb: a rigged character with a face of morph targets, for films (a `rigged` kind: src/shared/cartoon/film_rigged.h).

Mira is smooth rather than boxes: a body of tubes whose skin is weighted across each joint (the shoulders, elbows, wrists, hips and knees blend over a band, as a
character from Blender or Mixamo is), so its elbows and wrists show what dual-quaternion skinning does for them. Her face is morph targets named as ARKit names them
(Apple's blend shapes, which Ready Player Me, Character Creator and many VTuber models use), so the film drives it as it drives a downloaded character's:

  eyeBlinkLeft/Right  eyeWideLeft/Right  jawOpen  mouthSmileLeft/Right  mouthFrownLeft/Right  mouthFunnel  mouthPucker  mouthStretchLeft/Right
  mouthPressLeft/Right  mouthRollLower  browInnerUp  browDownLeft/Right  browOuterUpLeft/Right

Her clips: idle (a breath and a sway, 3 s), walk (1 s: two steps), wave (2 s), twist (her left forearm turned 170 degrees and back, 2 s: a linear skin collapses there,
a dual-quaternion one keeps its volume). Her materials are named skin, shirt, trousers, shoes, hair, eyeWhite, iris, mouth and brows (`skin-light` finds her skin).

Run it from the repository: python scripts/make_rigged_face.py [OUT.glb]. Standard library only.
"""
import json
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_rigged_robot import Builder, quat  # noqa: E402

OUT = sys.argv[1] if len(sys.argv) > 1 else "pbrt_scenes/film_assets/library/mira.glb"

# joints: name, parent, rest position (y up, facing +z; +x is her left)
JOINTS = [
    ("hips", -1, (0.0, 0.95, 0.0)), ("chest", 0, (0.0, 1.25, 0.0)), ("head", 1, (0.0, 1.52, 0.0)),
    ("shoulder.L", 1, (0.19, 1.42, 0.0)), ("elbow.L", 3, (0.22, 1.14, 0.0)), ("wrist.L", 4, (0.24, 0.90, 0.0)),
    ("shoulder.R", 1, (-0.19, 1.42, 0.0)), ("elbow.R", 6, (-0.22, 1.14, 0.0)), ("wrist.R", 7, (-0.24, 0.90, 0.0)),
    ("leg.L", 0, (0.10, 0.90, 0.0)), ("knee.L", 9, (0.10, 0.50, 0.0)),
    ("leg.R", 0, (-0.10, 0.90, 0.0)), ("knee.R", 11, (-0.10, 0.50, 0.0)),
]
J = {name: i for i, (name, _, _) in enumerate(JOINTS)}
MATERIALS = [
    ("skin", [0.86, 0.62, 0.50], 0.6), ("shirt", [0.20, 0.45, 0.62], 0.8), ("trousers", [0.22, 0.20, 0.26], 0.8), ("shoes", [0.55, 0.20, 0.12], 0.5),
    ("hair", [0.20, 0.10, 0.06], 0.7), ("eyeWhite", [0.93, 0.92, 0.90], 0.3), ("iris", [0.12, 0.30, 0.22], 0.3), ("mouth", [0.30, 0.06, 0.08], 0.7),
    ("brows", [0.16, 0.08, 0.05], 0.8),
]
M = {name: i for i, (name, _, _) in enumerate(MATERIALS)}
MORPHS = ["eyeBlinkLeft", "eyeBlinkRight", "eyeWideLeft", "eyeWideRight", "jawOpen", "mouthSmileLeft", "mouthSmileRight", "mouthFrownLeft", "mouthFrownRight",
          "mouthFunnel", "mouthPucker", "mouthStretchLeft", "mouthStretchRight", "mouthPressLeft", "mouthPressRight", "mouthRollLower", "browInnerUp",
          "browDownLeft", "browDownRight", "browOuterUpLeft", "browOuterUpRight"]


def add(a, b): return (a[0] + b[0], a[1] + b[1], a[2] + b[2])
def sub(a, b): return (a[0] - b[0], a[1] - b[1], a[2] - b[2])
def mul(a, k): return (a[0] * k, a[1] * k, a[2] * k)
def norm(a):
    l = math.sqrt(a[0] ** 2 + a[1] ** 2 + a[2] ** 2) or 1.0
    return (a[0] / l, a[1] / l, a[2] / l)
def smooth(e0, e1, x):
    u = min(1.0, max(0.0, (x - e0) / (e1 - e0)))
    return u * u * (3 - 2 * u)


class Part:
    """Vertices, normals, triangles and each vertex's joints and weights; `face` holds each morph's deltas (head parts only)."""
    def __init__(self):
        self.pos, self.nor, self.idx, self.joints, self.weights = [], [], [], [], []
        self.deltas = {}

    def vertex(self, p, n, jw):
        jw = sorted(jw, key=lambda x: -x[1])[:4]
        total = sum(w for _, w in jw) or 1.0
        self.pos.append(p), self.nor.append(n)
        self.joints.append([j for j, _ in jw] + [0] * (4 - len(jw)))
        self.weights.append([w / total for _, w in jw] + [0.0] * (4 - len(jw)))
        return len(self.pos) - 1


def tube(part, path, radii, weight_of, segs=16, rings=28):
    """A tube along a polyline `path` (radius per point), its vertices weighted by weight_of(s) where s runs 0..len along the path; closed at both ends."""
    lengths = [0.0]
    for a, b in zip(path, path[1:]):
        lengths.append(lengths[-1] + math.dist(a, b))
    total = lengths[-1]
    base = len(part.pos)
    for r in range(rings + 1):
        s = total * r / rings
        k = max(i for i in range(len(path) - 1) if lengths[i] <= s + 1e-12) if s < total else len(path) - 2
        u = (s - lengths[k]) / max(1e-9, lengths[k + 1] - lengths[k])
        c = add(path[k], mul(sub(path[k + 1], path[k]), u))
        rad = radii[k] + (radii[k + 1] - radii[k]) * u
        cap = math.sin(math.pi * 0.5 * min(1.0, min(s, total - s) / (rad * 0.9 + 1e-9)))   # (rounded ends)
        d = norm(sub(path[k + 1], path[k]))
        side = norm((d[1], -d[0], 0.0)) if abs(d[2]) < 0.9 else (1.0, 0.0, 0.0)
        side2 = (d[1] * side[2] - d[2] * side[1], d[2] * side[0] - d[0] * side[2], d[0] * side[1] - d[1] * side[0])
        for g in range(segs):
            a = 2 * math.pi * g / segs
            n = add(mul(side, math.cos(a)), mul(side2, math.sin(a)))
            part.vertex(add(c, mul(n, rad * max(0.05, cap))), n, weight_of(s / total))
    for r in range(rings):
        for g in range(segs):
            a, b = base + r * segs + g, base + r * segs + (g + 1) % segs
            part.idx += [a, base + (r + 1) * segs + g, b, b, base + (r + 1) * segs + g, base + (r + 1) * segs + (g + 1) % segs]


def blend(j0, j1, at, band):
    """Weights across a joint: wholly j0 before `at - band`, wholly j1 after `at + band`."""
    def w(s):
        k = smooth(at - band, at + band, s)
        return [(J[j0], 1.0 - k), (J[j1], k)]
    return w


def sphere(part, c, r, jw, nu=36, nv=18, squash=(1.0, 1.0, 1.0), keep=lambda p: True):
    base = len(part.pos)
    for i in range(nv + 1):
        th = math.pi * i / nv
        for j in range(nu + 1):
            ph = 2 * math.pi * j / nu
            n = (math.sin(th) * math.sin(ph), math.cos(th), math.sin(th) * math.cos(ph))
            part.vertex((c[0] + n[0] * r * squash[0], c[1] + n[1] * r * squash[1], c[2] + n[2] * r * squash[2]), norm((n[0] / squash[0], n[1] / squash[1], n[2] / squash[2])), jw)
    for i in range(nv):
        for j in range(nu):
            a, b = base + i * (nu + 1) + j, base + (i + 1) * (nu + 1) + j
            if keep(part.pos[a]):
                part.idx += [a, b, a + 1, a + 1, b, b + 1]
    return base


def main():
    parts = {name: Part() for name, _, _ in MATERIALS}
    # ---- the body ----
    tube(parts["shirt"], [(0, 0.98, 0), (0, 1.22, 0), (0, 1.46, 0)], [0.15, 0.16, 0.13], blend("hips", "chest", 0.45, 0.2), segs=28)
    for side, s in (("L", 1), ("R", -1)):
        sh, el, wr = JOINTS[J["shoulder." + side]][2], JOINTS[J["elbow." + side]][2], JOINTS[J["wrist." + side]][2]
        upper = math.dist(sh, el)
        tube(parts["shirt"], [sh, el, wr], [0.052, 0.045, 0.038],
             lambda u, side=side, upper=upper / (upper + math.dist(el, wr)): [(J["chest"], 1.0 - smooth(0.0, 0.08, u))] + blend("shoulder." + side, "elbow." + side, upper, 0.07)(u)
             if u < upper + 0.15 else blend("elbow." + side, "wrist." + side, 0.97, 0.05)(u))
        tube(parts["skin"], [wr, add(wr, (0.006 * s, -0.10, 0.0))], [0.032, 0.026], lambda u, side=side: [(J["wrist." + side], 1.0)], segs=14, rings=8)   # the hand
        hip, knee = JOINTS[J["leg." + side]][2], JOINTS[J["knee." + side]][2]
        ankle = (hip[0], 0.07, 0.0)
        tube(parts["trousers"], [add(hip, (0, 0.08, 0)), knee, ankle], [0.075, 0.058, 0.048],
             lambda u, side=side: [(J["hips"], 1.0 - smooth(0.0, 0.12, u))] + blend("leg." + side, "knee." + side, 0.5, 0.06)(u))
        tube(parts["shoes"], [add(ankle, (0, -0.02, -0.04)), add(ankle, (0, -0.03, 0.11))], [0.05, 0.045], lambda u, side=side: [(J["knee." + side], 1.0)], segs=14, rings=8)
    tube(parts["skin"], [(0, 1.42, 0), (0, 1.56, 0.0)], [0.045, 0.05], blend("chest", "head", 0.5, 0.4), segs=16, rings=10)   # the neck
    # ---- the head: skin, hair, eyes, lids, brows, mouth (all on the head joint) ----
    head = [(J["head"], 1.0)]
    H = (0.0, 1.66, 0.0)
    sphere(parts["skin"], H, 0.13, head, squash=(0.92, 1.0, 0.95))
    sphere(parts["hair"], add(H, (0, 0.025, -0.01)), 0.137, head, squash=(0.95, 0.97, 0.97), keep=lambda p: p[1] > H[1] + 0.085 or p[2] < H[2] - 0.02)
    eyes = {}
    for side, s in (("Left", 1), ("Right", -1)):
        c = (0.042 * s, H[1] + 0.012, 0.098)
        eyes[side] = c
        sphere(parts["eyeWhite"], c, 0.024, head, nu=24, nv=12)
        sphere(parts["iris"], add(c, (0, 0, 0.021)), 0.0115, head, nu=20, nv=10, squash=(1.0, 1.0, 0.45))
    # lids: caps over each eye's top, rotated down to blink (their morphs)
    lids = {}
    face = {"lids": Part(), "brows": parts.pop("brows"), "mouth": parts.pop("mouth")}   # (the parts that move with the morphs: a mesh of their own)
    for side, c in eyes.items():
        part = face["lids"]
        start = len(part.pos)
        r = 0.0262
        for i in range(9):
            th = math.radians(5 + 70 * i / 8)   # from the top of the eye forward-down
            for j in range(17):
                ph = math.radians(-100 + 200 * j / 16)
                n = (math.sin(th) * math.sin(ph), math.cos(th), math.sin(th) * math.cos(ph))
                part.vertex(add(c, mul(n, r)), n, head)
        for i in range(8):
            for j in range(16):
                a, b = start + i * 17 + j, start + (i + 1) * 17 + j
                part.idx += [a, b, a + 1, a + 1, b, b + 1]
        lids[side] = (start, len(part.pos), c)
    # brows: short tubes above the eyes
    brows = {}
    for side, s in (("Left", 1), ("Right", -1)):
        part = face["brows"]
        start = len(part.pos)
        inner, outer = (0.018 * s, H[1] + 0.042, 0.121), (0.066 * s, H[1] + 0.039, 0.104)
        tube(part, [inner, outer], [0.006, 0.005], lambda u: head, segs=8, rings=8)
        brows[side] = (start, len(part.pos), s)
    # the mouth: a dark oval lying on the face (a disc of rings), its morphs open, round, widen and press it
    part = face["mouth"]
    mstart = len(part.pos)
    centre = (0.0, H[1] - 0.062, 0.112)
    for ring in range(7):
        rr = ring / 6.0
        for g in range(32):
            a = 2 * math.pi * g / 32
            u, v = math.cos(a) * rr, math.sin(a) * rr
            p = (centre[0] + 0.040 * u, centre[1] + 0.006 * v, centre[2] - 0.010 * u * u)
            part.vertex(p, norm((p[0] * 0.3, 0.0, 1.0)), head)
    for ring in range(6):
        for g in range(32):
            a, b = mstart + ring * 32 + g, mstart + ring * 32 + (g + 1) % 32
            part.idx += [a, b, a + 32, b, b + 32, a + 32]
    # ---- the morph targets: each a delta for every vertex of every part ----
    for part in face.values():
        part.deltas = {m: [(0.0, 0.0, 0.0)] * len(part.pos) for m in MORPHS}

    def rot_x(p, c, deg):   # p turned about the horizontal axis through c (positive: down over the eye)
        a = math.radians(deg)
        y, z = p[1] - c[1], p[2] - c[2]
        return (p[0], c[1] + y * math.cos(a) - z * math.sin(a), c[2] + y * math.sin(a) + z * math.cos(a))
    for side, (a, b, c) in lids.items():
        for i in range(a, b):
            p = face["lids"].pos[i]
            face["lids"].deltas["eyeBlink" + side][i] = sub(rot_x(p, c, 80), p)
            face["lids"].deltas["eyeWide" + side][i] = sub(rot_x(p, c, -20), p)
    for side, (a, b, s) in brows.items():
        other = "Left" if side == "Right" else "Right"
        for i in range(a, b):
            p = face["brows"].pos[i]
            inner = 1.0 - smooth(0.02, 0.07, abs(p[0]))
            face["brows"].deltas["browInnerUp"][i] = (0.0, 0.014 * inner, 0.0)
            face["brows"].deltas["browDown" + side][i] = (0.0, -0.010 - 0.006 * inner, 0.002)
            face["brows"].deltas["browOuterUp" + side][i] = (0.0, 0.013 * (1.0 - inner), 0.0)
    mouth = face["mouth"]
    for i in range(mstart, len(mouth.pos)):
        p = mouth.pos[i]
        u, v = (p[0] - centre[0]) / 0.040, (p[1] - centre[1]) / 0.006
        corner = max(0.0, abs(u) - 0.4) / 0.6
        mouth.deltas["jawOpen"][i] = (0.0, -0.030 * max(0.0, -v) * (1 - 0.4 * u * u) + 0.004 * max(0.0, v), 0.0)
        for side, s in (("Left", 1), ("Right", -1)):
            on = corner if u * s > 0 else 0.0
            mouth.deltas["mouthSmile" + side][i] = (0.006 * s * on, 0.010 * on * on, -0.004 * on)
            mouth.deltas["mouthFrown" + side][i] = (0.0, -0.010 * on * on, 0.0)
            mouth.deltas["mouthStretch" + side][i] = (0.012 * s * on, 0.0, -0.003 * on)
            mouth.deltas["mouthPress" + side][i] = (0.0, -0.9 * (p[1] - centre[1]) * (0.5 + 0.5 * on), 0.0)
        mouth.deltas["mouthFunnel"][i] = (-0.016 * u, 0.012 * v, 0.008)
        mouth.deltas["mouthPucker"][i] = (-0.022 * u, -0.3 * (p[1] - centre[1]), 0.012)
        mouth.deltas["mouthRollLower"][i] = (0.0, 0.004 * max(0.0, -v), -0.004 * max(0.0, -v))

    # ---- the file ----
    b = Builder()
    flat = lambda rows: [x for r in rows for x in r]
    body_prims, face_prims = [], []
    for name, part, morphs in [(n, p, False) for n, p in parts.items()] + [("skin" if n == "lids" else n, p, True) for n, p in face.items()]:
        if not part.pos:
            continue
        prim = {
            "attributes": {
                "POSITION": b.floats(flat(part.pos), "VEC3", 3, minmax=True),
                "NORMAL": b.floats(flat(part.nor), "VEC3", 3),
                "JOINTS_0": b.add(struct.pack("<%dH" % (len(part.joints) * 4), *flat(part.joints)), 5123, "VEC4", len(part.joints)),
                "WEIGHTS_0": b.floats(flat(part.weights), "VEC4", 4),
            },
            "indices": b.add(struct.pack("<%dH" % len(part.idx), *part.idx), 5123, "SCALAR", len(part.idx)),
            "material": M[name],
        }
        if morphs:
            prim["targets"] = [{"POSITION": b.floats(flat(part.deltas[m]), "VEC3", 3, minmax=True)} for m in MORPHS]
        (face_prims if morphs else body_prims).append(prim)
    nodes = []
    for name, parent, p in JOINTS:
        local = p if parent < 0 else [p[i] - JOINTS[parent][2][i] for i in range(3)]
        nodes.append({"name": name, "translation": [round(x, 6) for x in local]})
    for j, (name, parent, p) in enumerate(JOINTS):
        if parent >= 0:
            nodes[parent].setdefault("children", []).append(j)
    ibm = []
    for name, parent, p in JOINTS:
        ibm += [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -p[0], -p[1], -p[2], 1]
    skin = {"joints": list(range(len(JOINTS))), "inverseBindMatrices": b.floats(ibm, "MAT4", 16), "skeleton": 0}
    nodes.append({"name": "mira", "mesh": 0, "skin": 0})
    nodes.append({"name": "mira-face", "mesh": 1, "skin": 0})
    mesh_node = len(nodes) - 2

    X, Y, Z = (1, 0, 0), (0, 1, 0), (0, 0, 1)
    animations = []

    def clip(name, tracks):
        samplers, channels = [], []
        for joint, path, times, values in tracks:
            per = 4 if path == "rotation" else 3
            inp = b.floats(times, "SCALAR", 1, minmax=True)
            out = b.floats([x for v in values for x in v], "VEC4" if per == 4 else "VEC3", per)
            samplers.append({"input": inp, "output": out, "interpolation": "LINEAR"})
            channels.append({"sampler": len(samplers) - 1, "target": {"node": J[joint], "path": path}})
        animations.append({"name": name, "samplers": samplers, "channels": channels})

    def qmul(a, c):
        return [a[3] * c[0] + a[0] * c[3] + a[1] * c[2] - a[2] * c[1], a[3] * c[1] - a[0] * c[2] + a[1] * c[3] + a[2] * c[0],
                a[3] * c[2] + a[0] * c[1] - a[1] * c[0] + a[2] * c[3], a[3] * c[3] - a[0] * c[0] - a[1] * c[1] - a[2] * c[2]]
    steps = [i / 8.0 for i in range(9)]
    swing = lambda t, phase, amp: amp * math.sin(2 * math.pi * t + phase)
    hips = JOINTS[0][2]
    clip("walk", [
        ("leg.L", "rotation", steps, [quat(X, swing(t, 0.0, 26)) for t in steps]),
        ("leg.R", "rotation", steps, [quat(X, swing(t, math.pi, 26)) for t in steps]),
        ("knee.L", "rotation", steps, [quat(X, -max(0.0, swing(t, -1.2, 35))) for t in steps]),
        ("knee.R", "rotation", steps, [quat(X, -max(0.0, swing(t, math.pi - 1.2, 35))) for t in steps]),
        ("shoulder.L", "rotation", steps, [quat(X, swing(t, math.pi, 20)) for t in steps]),
        ("shoulder.R", "rotation", steps, [quat(X, swing(t, 0.0, 20)) for t in steps]),
        ("elbow.L", "rotation", steps, [quat(X, -15 + swing(t, math.pi, 8)) for t in steps]),
        ("elbow.R", "rotation", steps, [quat(X, -15 + swing(t, 0.0, 8)) for t in steps]),
        ("hips", "translation", steps, [[hips[0], hips[1] + 0.02 * abs(math.cos(2 * math.pi * t)), hips[2]] for t in steps]),
    ])
    idle_t = [i * 0.5 for i in range(7)]
    clip("idle", [
        ("chest", "rotation", idle_t, [quat(Z, 2.0 * math.sin(2 * math.pi * t / 3.0)) for t in idle_t]),
        ("shoulder.L", "rotation", idle_t, [quat(Z, 6) for t in idle_t]),
        ("shoulder.R", "rotation", idle_t, [quat(Z, -6) for t in idle_t]),
        ("elbow.L", "rotation", idle_t, [quat(X, -10) for t in idle_t]),
        ("elbow.R", "rotation", idle_t, [quat(X, -10) for t in idle_t]),
    ])
    wt = [0.0, 0.4, 0.7, 1.0, 1.3, 1.6, 2.0]
    clip("wave", [("shoulder.R", "rotation", wt, [quat(Z, a) for a in (0, -150, -140, -150, -140, -150, 0)]),
                  ("elbow.R", "rotation", wt, [quat(Z, a) for a in (0, -30, -55, -30, -55, -30, 0)])])
    tt = [0.0, 0.5, 1.0, 1.5, 2.0]
    clip("twist", [("shoulder.L", "rotation", tt, [quat(Z, a) for a in (0, 70, 70, 70, 0)]),
                   ("elbow.L", "rotation", tt, [qmul(quat(Z, e), quat(Y, a)) for e, a in ((0, 0), (40, 170), (40, 170), (40, 170), (0, 0))])])

    doc = {
        "asset": {"version": "2.0", "generator": "scripts/make_rigged_face.py"},
        "scene": 0, "scenes": [{"nodes": [0, mesh_node, mesh_node + 1]}],
        "nodes": nodes,
        "meshes": [{"name": "mira", "primitives": body_prims},
                   {"name": "mira-face", "primitives": face_prims, "weights": [0.0] * len(MORPHS), "extras": {"targetNames": MORPHS}}],
        "skins": [skin],
        "materials": [{"name": n, "pbrMetallicRoughness": {"baseColorFactor": c + [1.0], "metallicFactor": 0.0, "roughnessFactor": r}} for n, c, r in MATERIALS],
        "animations": animations,
        "buffers": [{"byteLength": len(b.bin)}], "bufferViews": b.views, "accessors": b.accessors,
    }
    text = json.dumps(doc, separators=(",", ":")).encode("utf-8")
    while len(text) % 4:
        text += b" "
    while len(b.bin) % 4:
        b.bin.append(0)
    total = 12 + 8 + len(text) + 8 + len(b.bin)
    with open(OUT, "wb") as f:
        f.write(struct.pack("<4sII", b"glTF", 2, total))
        f.write(struct.pack("<I4s", len(text), b"JSON") + text)
        f.write(struct.pack("<I4s", len(b.bin), b"BIN\0") + bytes(b.bin))
    print("wrote %s: %d bytes, %d joints, %d morphs, %d clips" % (OUT, total, len(JOINTS), len(MORPHS), len(animations)))


if __name__ == "__main__":
    main()
