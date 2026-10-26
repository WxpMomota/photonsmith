"""Makes pbrt_scenes/film_assets/library/robot.glb: a small rigged robot for films (a `rigged` kind: src/shared/cartoon/film_rigged.h).

A skinned glTF 2.0 model made from boxes - a pelvis, a torso, a head with eyes, two arms and two legs - each box's vertices weighted wholly to its joint (hips, chest,
head, the shoulders, the hips' legs), so it goes through the same skinning as a character from Blender or Mixamo. Its clips:

  idle    a slow sway of the chest and the head (3 s, looped)
  walk    the legs and the arms swinging, the hips bobbing (1 s: two steps)
  wave    the right arm raised and waved, then lowered (2 s)
  cheer   both arms up and shaken (1.6 s)
  nod     the head nodding twice (1 s)

Run it from the repository: python scripts/make_rigged_robot.py [OUT.glb]. Standard library only.
"""
import json
import math
import struct
import sys

OUT = sys.argv[1] if len(sys.argv) > 1 else "pbrt_scenes/film_assets/library/robot.glb"

# joints: name, parent, rest position in the model's world (y up, facing +z; +x is the robot's left)
JOINTS = [
    ("hips", -1, (0.0, 0.92, 0.0)),
    ("chest", 0, (0.0, 1.20, 0.0)),
    ("head", 1, (0.0, 1.52, 0.0)),
    ("arm.L", 1, (0.27, 1.42, 0.0)),
    ("arm.R", 1, (-0.27, 1.42, 0.0)),
    ("leg.L", 0, (0.11, 0.88, 0.0)),
    ("leg.R", 0, (-0.11, 0.88, 0.0)),
]
# materials: body (warm orange, a little glossy), limbs (grey), eyes (glowing-looking dark blue)
MATERIALS = [
    {"name": "body", "pbrMetallicRoughness": {"baseColorFactor": [0.95, 0.45, 0.12, 1.0], "metallicFactor": 0.0, "roughnessFactor": 0.35}},
    {"name": "limbs", "pbrMetallicRoughness": {"baseColorFactor": [0.55, 0.57, 0.62, 1.0], "metallicFactor": 1.0, "roughnessFactor": 0.4}},
    {"name": "eyes", "pbrMetallicRoughness": {"baseColorFactor": [0.05, 0.12, 0.30, 1.0], "metallicFactor": 0.0, "roughnessFactor": 0.2}},
]
# boxes: joint, material, centre, half sizes (in the model's world)
BOXES = [
    (0, 0, (0.0, 0.94, 0.0), (0.16, 0.08, 0.11)),     # pelvis
    (1, 0, (0.0, 1.22, 0.0), (0.22, 0.20, 0.14)),     # torso
    (2, 0, (0.0, 1.63, 0.0), (0.15, 0.12, 0.13)),     # head
    (2, 2, (0.06, 1.66, 0.13), (0.035, 0.035, 0.01)), # left eye
    (2, 2, (-0.06, 1.66, 0.13), (0.035, 0.035, 0.01)),# right eye
    (2, 1, (0.0, 1.80, 0.0), (0.012, 0.06, 0.012)),   # antenna
    (3, 1, (0.30, 1.14, 0.0), (0.045, 0.27, 0.045)),  # left arm
    (4, 1, (-0.30, 1.14, 0.0), (0.045, 0.27, 0.045)), # right arm
    (5, 1, (0.11, 0.45, 0.0), (0.055, 0.43, 0.055)),  # left leg
    (6, 1, (-0.11, 0.45, 0.0), (0.055, 0.43, 0.055)), # right leg
    (5, 0, (0.11, 0.03, 0.04), (0.075, 0.03, 0.10)),  # left foot
    (6, 0, (-0.11, 0.03, 0.04), (0.075, 0.03, 0.10)), # right foot
]


def box(centre, half):
    """24 vertices (4 a face, so each face has its own normal) and 12 triangles."""
    cx, cy, cz = centre
    hx, hy, hz = half
    faces = [((1, 0, 0), (0, 1, 0), (0, 0, 1)), ((-1, 0, 0), (0, 0, 1), (0, 1, 0)), ((0, 1, 0), (0, 0, 1), (1, 0, 0)),
             ((0, -1, 0), (1, 0, 0), (0, 0, 1)), ((0, 0, 1), (1, 0, 0), (0, 1, 0)), ((0, 0, -1), (0, 1, 0), (1, 0, 0))]
    pos, nor, idx = [], [], []
    for n, u, v in faces:
        base = len(pos)
        for su, sv in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
            p = [cx + (n[0] + u[0] * su + v[0] * sv) * hx, cy + (n[1] + u[1] * su + v[1] * sv) * hy, cz + (n[2] + u[2] * su + v[2] * sv) * hz]
            pos.append(p)
            nor.append(list(n))
        # wind so the face looks along its normal
        a, b, c = pos[base], pos[base + 1], pos[base + 2]
        e1 = [b[i] - a[i] for i in range(3)]
        e2 = [c[i] - a[i] for i in range(3)]
        cr = [e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]]
        if sum(cr[i] * n[i] for i in range(3)) >= 0:
            idx += [base, base + 1, base + 2, base, base + 2, base + 3]
        else:
            idx += [base, base + 2, base + 1, base, base + 3, base + 2]
    return pos, nor, idx


def quat(axis, degrees):
    a = math.radians(degrees) / 2.0
    s = math.sin(a)
    return [axis[0] * s, axis[1] * s, axis[2] * s, math.cos(a)]


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


def main():
    b = Builder()
    primitives = []
    for material in range(len(MATERIALS)):
        pos, nor, idx, joints, weights = [], [], [], [], []
        for joint, mat, centre, half in BOXES:
            if mat != material:
                continue
            p, n, i = box(centre, half)
            base = len(pos)
            pos += p
            nor += n
            idx += [base + k for k in i]
            joints += [[joint, 0, 0, 0]] * len(p)
            weights += [[1.0, 0.0, 0.0, 0.0]] * len(p)
        flat = lambda rows: [x for r in rows for x in r]
        primitives.append({
            "attributes": {
                "POSITION": b.floats(flat(pos), "VEC3", 3, minmax=True),
                "NORMAL": b.floats(flat(nor), "VEC3", 3),
                "JOINTS_0": b.add(struct.pack("<%dH" % (len(joints) * 4), *flat(joints)), 5123, "VEC4", len(joints)),
                "WEIGHTS_0": b.floats(flat(weights), "VEC4", 4),
            },
            "indices": b.add(struct.pack("<%dH" % len(idx), *idx), 5123, "SCALAR", len(idx)),
            "material": material,
        })
    # the skeleton: joint nodes with their rest translations relative to their parents; the mesh node holds the skin
    nodes = []
    for name, parent, p in JOINTS:
        local = p if parent < 0 else [p[i] - JOINTS[parent][2][i] for i in range(3)]
        nodes.append({"name": name, "translation": [round(x, 6) for x in local]})
    for j, (name, parent, p) in enumerate(JOINTS):
        if parent >= 0:
            nodes[parent].setdefault("children", []).append(j)
    ibm = []
    for name, parent, p in JOINTS:   # the inverse of each joint's rest world matrix: a translation by -p (column-major)
        ibm += [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -p[0], -p[1], -p[2], 1]
    skin = {"joints": list(range(len(JOINTS))), "inverseBindMatrices": b.floats(ibm, "MAT4", 16), "skeleton": 0}
    nodes.append({"name": "robot", "mesh": 0, "skin": 0})
    mesh_node = len(nodes) - 1

    # the clips
    X, Z = (1, 0, 0), (0, 0, 1)
    J = {name: i for i, (name, _, _) in enumerate(JOINTS)}
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

    steps = [i / 8.0 for i in range(9)]   # (a second in eighths)
    swing = lambda t, phase, amp: amp * math.sin(2 * math.pi * t + phase)
    hips_rest = JOINTS[0][2]
    clip("walk", [
        ("leg.L", "rotation", steps, [quat(X, swing(t, 0.0, 28)) for t in steps]),
        ("leg.R", "rotation", steps, [quat(X, swing(t, math.pi, 28)) for t in steps]),
        ("arm.L", "rotation", steps, [quat(X, swing(t, math.pi, 22)) for t in steps]),
        ("arm.R", "rotation", steps, [quat(X, swing(t, 0.0, 22)) for t in steps]),
        ("hips", "translation", steps, [[hips_rest[0], hips_rest[1] + 0.025 * abs(math.cos(2 * math.pi * t)), hips_rest[2]] for t in steps]),
        ("chest", "rotation", steps, [quat((0, 1, 0), swing(t, 0.0, 6)) for t in steps]),
    ])
    idle_t = [i * 0.5 for i in range(7)]
    clip("idle", [
        ("chest", "rotation", idle_t, [quat(Z, 2.5 * math.sin(2 * math.pi * t / 3.0)) for t in idle_t]),
        ("head", "rotation", idle_t, [quat(X, 3.0 * math.sin(2 * math.pi * t / 3.0 + 1.0)) for t in idle_t]),
    ])
    clip("wave", [("arm.R", "rotation", [0.0, 0.4, 0.7, 1.0, 1.3, 1.6, 2.0],
                   [quat(Z, 0), quat(Z, -150), quat(Z, -125), quat(Z, -165), quat(Z, -125), quat(Z, -150), quat(Z, 0)])])
    clip("cheer", [("arm.L", "rotation", [0.0, 0.35, 0.6, 0.85, 1.1, 1.6], [quat(Z, 0), quat(Z, 160), quat(Z, 140), quat(Z, 165), quat(Z, 140), quat(Z, 0)]),
                   ("arm.R", "rotation", [0.0, 0.35, 0.6, 0.85, 1.1, 1.6], [quat(Z, 0), quat(Z, -160), quat(Z, -140), quat(Z, -165), quat(Z, -140), quat(Z, 0)])])
    clip("nod", [("head", "rotation", [0.0, 0.25, 0.5, 0.75, 1.0], [quat(X, 0), quat(X, 18), quat(X, 0), quat(X, 18), quat(X, 0)])])

    doc = {
        "asset": {"version": "2.0", "generator": "scripts/make_rigged_robot.py"},
        "scene": 0,
        "scenes": [{"nodes": [0, mesh_node]}],
        "nodes": nodes,
        "meshes": [{"name": "robot", "primitives": primitives}],
        "skins": [skin],
        "materials": MATERIALS,
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
    with open(OUT, "wb") as f:
        f.write(struct.pack("<4sII", b"glTF", 2, total))
        f.write(struct.pack("<I4s", len(text), b"JSON") + text)
        f.write(struct.pack("<I4s", len(b.bin), b"BIN\0") + bytes(b.bin))
    print("wrote %s: %d bytes, %d joints, %d clips" % (OUT, total, len(JOINTS), len(animations)))


if __name__ == "__main__":
    main()
