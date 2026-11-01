"""Makes the films' rigged animals (src/shared/cartoon/film_animals.h): skinned glTF 2.0 models with their clips, in pbrt_scenes/film_assets/library/animals/.

  one skin                        a quadruped's trunk, neck, upper legs and the masses of its shoulders and haunches are drawn as tubes and blobs and then fused (`fuse`): a distance
                                  field of them, blended where they meet, and one surface round it. A leg grows out of the body; it is not a tube pushed into a tube.

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
def hash01(i, salt):   # (a fixed number from 0 to 1, so the models come out the same each time)
    x = ((i * 2654435761) ^ (salt * 2246822519)) & 0xffffffff
    x = ((x ^ (x >> 15)) * 2246822519) & 0xffffffff
    x = ((x ^ (x >> 13)) * 3266489917) & 0xffffffff
    return (x ^ (x >> 16)) / 4294967296.0


def quat(axis, degrees):
    a = math.radians(degrees) / 2.0
    s = math.sin(a)
    ax = norm(axis)
    return [ax[0] * s, ax[1] * s, ax[2] * s, math.cos(a)]


def qmul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return [aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx, aw * bz + ax * by - ay * bx + az * bw, aw * bw - ax * bx - ay * by - az * bz]


def qrot(q, v):   # (a vector turned by a quaternion)
    u = q[:3]
    t = mul(cross(u, v), 2.0)
    return add(add(v, mul(t, q[3])), cross(u, t))


def qconj(q): return [-q[0], -q[1], -q[2], q[3]]


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
        self.pos, self.nor, self.joints, self.weights, self.idx, self.uv = [], [], [], [], [], []

    def vertex(self, p, n, skin, uv=(0.995, 0.995)):
        skin = sorted(skin, key=lambda jw: -jw[1])[:4]
        total = sum(w for _, w in skin) or 1.0
        j = [jw[0] for jw in skin] + [0] * (4 - len(skin))
        w = [jw[1] / total for jw in skin] + [0.0] * (4 - len(skin))
        self.pos.append(p)
        self.nor.append(norm(n))
        self.joints.append(j)
        self.weights.append(w)
        self.uv.append(uv)
        return len(self.pos) - 1


def tube(mesh, centres, radii, skins, sides=14, up=(0, 1, 0), caps=True, rect=None, skinAt=None):
    """A tube through `centres`, its cross-section at each an ellipse of radii (across, up), each ring skinned as `skins` says ([(joint, weight)...]).
    `rect` (x0, y0, x1, y1): its place in the coat's picture (x along the tube, y round it from the underside: the seam is under the belly); none: no pattern.
    `skinAt(point, skin)`: what a point of a ring really follows, when not the whole ring moves alike."""
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
        for k in range(sides + 1):   # (the first point again at the end: the picture's seam)
            a = 2 * math.pi * k / sides - math.pi / 2
            off = add(mul(u, math.cos(a) * radii[i][0]), mul(v, math.sin(a) * radii[i][1]))
            nrm = add(mul(u, math.cos(a) / max(1e-6, radii[i][0])), mul(v, math.sin(a) / max(1e-6, radii[i][1])))
            uv = (rect[0] + (rect[2] - rect[0]) * i / max(1, n - 1), rect[1] + (rect[3] - rect[1]) * k / sides) if rect else (0.995, 0.995)
            ring.append(mesh.vertex(add(centres[i], off), nrm, skinAt(add(centres[i], off), skins[i]) if skinAt else skins[i], uv))
        rings.append(ring)
    for i in range(n - 1):
        for k in range(sides):
            a, b, c, d = rings[i][k], rings[i][k + 1], rings[i + 1][k + 1], rings[i + 1][k]
            mesh.idx += [a, c, b, a, d, c]
    if caps:
        for end, sign in ((0, -1), (n - 1, 1)):
            t = norm(sub(centres[min(end + 1, n - 1)], centres[max(end - 1, 0)]))
            centre = mesh.vertex(centres[end], mul(t, sign), skins[end], mesh.uv[rings[end][0]])
            for k in range(sides):
                a, b = rings[end][k], rings[end][k + 1]
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


class Source:
    """A tube to be fused: rings as tube() takes them, with where each falls along the coat's picture (`along`), the `rect` of the picture and the `mesh` it goes to. No rect: a mass under
    the skin, which shapes it and gives it no colour of its own."""
    def __init__(self, centres, radii, skins, up=(0, 1, 0), rect=None, mesh=None, along=None):
        self.centres, self.radii, self.skins, self.rect, self.mesh = centres, radii, skins, rect, mesh
        n = len(centres)
        self.along = along or [i / max(1, n - 1) for i in range(n)]
        self.frames = []
        for i in range(n):
            t = norm(sub(centres[min(i + 1, n - 1)], centres[max(i - 1, 0)]))
            u = cross(norm(up), t)
            if math.sqrt(dot(u, u)) < 1e-6:
                u = cross([1, 0, 0], t)
            u = norm(u)
            self.frames.append((t, u, cross(t, u)))

    def near(self, p, i):
        """How far `p` is outside segment i of it (below 0: inside), where along the segment, and its place across: (distance, w, x, y, a, b)."""
        c0, c1 = self.centres[i], self.centres[i + 1]
        s = sub(c1, c0)
        w = max(0.0, min(1.0, dot(sub(p, c0), s) / max(1e-12, dot(s, s))))
        c = lerp(c0, c1, w)
        f0, f1 = self.frames[i], self.frames[i + 1]
        u, v = lerp(f0[1], f1[1], w), lerp(f0[2], f1[2], w)
        a = max(1e-5, self.radii[i][0] + (self.radii[i + 1][0] - self.radii[i][0]) * w)
        b = max(1e-5, self.radii[i][1] + (self.radii[i + 1][1] - self.radii[i][1]) * w)
        q = sub(p, c)
        x, y = dot(q, u), dot(q, v)
        k = math.sqrt((x / a) ** 2 + (y / b) ** 2)
        r = math.sqrt(x * x + y * y)
        d = r * (1.0 - 1.0 / k) if k > 1e-6 else -min(a, b)
        if w <= 0.0 or w >= 1.0:
            axial = abs(dot(q, f0[0] if w <= 0.0 else f1[0]))
            if (w <= 0.0 and i == 0) or (w >= 1.0 and i + 2 == len(self.centres)):   # (past an end of the tube: rounded off)
                d = math.sqrt(max(d, 0.0) ** 2 + axial * axial) if d > 0.0 else max(d, axial - 1e-9)
            else:   # (past a ring within it: the outer corner of a bend is still inside, and no more than that)
                d = max(d, axial - 0.75 * math.sqrt(dot(s, s)))
        return d, w, x, y, a, b


def fuse(sources, h, blend, softSkin):
    """One surface round `sources`, cells `h` across, blended over `blend` where they meet, its triangles and their points going to the sources' meshes: a distance field (each tube stamped
    over its own box, as most of space is empty) and surface nets. Each point follows the bones of the nearest tubes, shared by how near they are; each triangle takes the coat's picture
    of the one tube nearest all of it, so the picture does not smear across the join."""
    lo = [min(c[k] - max(r) for s in sources for c, r in zip(s.centres, s.radii)) - 4 * h - blend for k in range(3)]
    hi = [max(c[k] + max(r) for s in sources for c, r in zip(s.centres, s.radii)) + 4 * h + blend for k in range(3)]
    nx, ny, nz = [int((hi[k] - lo[k]) / h) + 2 for k in range(3)]
    far_ = 1e3
    field = [far_] * (nx * ny * nz)
    index = lambda x, y, z: x + nx * (y + ny * z)
    band = 3 * h + blend
    for s in sources:
        own = {}
        for i in range(len(s.centres) - 1):
            r = max(max(s.radii[i]), max(s.radii[i + 1])) + band
            box0 = [max(0, int((min(s.centres[i][k], s.centres[i + 1][k]) - r - lo[k]) / h)) for k in range(3)]
            box1 = [min((nx, ny, nz)[k] - 1, int((max(s.centres[i][k], s.centres[i + 1][k]) + r - lo[k]) / h) + 1) for k in range(3)]
            for z in range(box0[2], box1[2] + 1):
                for y in range(box0[1], box1[1] + 1):
                    base = nx * (y + ny * z)
                    for x in range(box0[0], box1[0] + 1):
                        d = s.near([lo[0] + x * h, lo[1] + y * h, lo[2] + z * h], i)[0]
                        at = base + x
                        was = own.get(at)
                        if was is None or d < was:
                            own[at] = d
        for at, d in own.items():   # (smooth union: a fillet `blend` wide where it meets what is there)
            e = field[at]
            w = max(blend - abs(e - d), 0.0) / blend
            field[at] = min(e, d) - w * w * blend * 0.25
    # surface nets: a point in each cell the surface passes through, a quad across each edge it cuts
    corners = [(i & 1, (i >> 1) & 1, (i >> 2) & 1) for i in range(8)]
    edges = [(0, 1), (2, 3), (4, 5), (6, 7), (0, 2), (1, 3), (4, 6), (5, 7), (0, 4), (1, 5), (2, 6), (3, 7)]
    cell, points = {}, []
    for z in range(nz - 1):
        for y in range(ny - 1):
            for x in range(nx - 1):
                f = [field[index(x + c[0], y + c[1], z + c[2])] for c in corners]
                if min(f) >= 0.0 or max(f) < 0.0:
                    continue
                total, count = [0.0, 0.0, 0.0], 0
                for a, b in edges:
                    if (f[a] < 0.0) != (f[b] < 0.0):
                        w = f[a] / (f[a] - f[b])
                        total = add(total, lerp(corners[a], corners[b], w))
                        count += 1
                cell[(x, y, z)] = len(points)
                points.append([lo[k] + ((x, y, z)[k] + total[k] / count) * h for k in range(3)])
    quads = []
    for (x, y, z), _ in list(cell.items()):
        here = field[index(x, y, z)]
        for axis, step in ((0, (1, 0, 0)), (1, (0, 1, 0)), (2, (0, 0, 1))):
            there = field[index(x + step[0], y + step[1], z + step[2])]
            if (here < 0.0) == (there < 0.0):
                continue
            p, q = (axis + 1) % 3, (axis + 2) % 3
            around = []
            for a, b in ((0, 0), (1, 0), (1, 1), (0, 1)):
                c = [x, y, z]
                c[p] -= a
                c[q] -= b
                around.append(cell.get(tuple(c)))
            if None not in around:
                quads.append(around if here < 0.0 else around[::-1])
    # once over with the neighbours' average, for the steps of the grid
    linked = [set() for _ in points]
    for q in quads:
        for i in range(4):
            linked[q[i]].add(q[(i + 1) % 4])
            linked[q[i]].add(q[(i + 3) % 4])
    for _ in range(2):
        moved = []
        for i, p in enumerate(points):
            if linked[i]:
                mean = [sum(points[j][k] for j in linked[i]) / len(linked[i]) for k in range(3)]
                moved.append(lerp(p, mean, 0.6))
            else:
                moved.append(p)
        points = moved
    tris = []
    for a, b, c, d in quads:
        tris += [(a, b, c), (a, c, d)]
    normals = [[0.0, 0.0, 0.0] for _ in points]
    for a, b, c in tris:
        n = cross(sub(points[b], points[a]), sub(points[c], points[a]))
        for v in (a, b, c):
            normals[v] = add(normals[v], n)
    # what each point follows, and which tube is nearest it
    nearest, skins = [], []
    for p in points:
        reach = []
        for t, s in enumerate(sources):
            best = None
            for i in range(len(s.centres) - 1):
                mid = lerp(s.centres[i], s.centres[i + 1], 0.5)
                if best is not None and math.sqrt(dot(sub(p, mid), sub(p, mid))) > best[0] + 3.0 * max(s.radii[i]) + 3.0 * blend:
                    continue
                d, w, x, y, a, b = s.near(p, i)
                if best is None or d < best[0]:
                    best = (d, i, w, x, y)
            reach.append((best[0], t, best[1], best[2], best[3], best[4]))
        least = min(r[0] for r in reach)
        weights = {}
        for d, t, i, w, x, y in reach:
            share = math.exp(-((d - least) / (0.5 * blend)) ** 2)
            if share < 0.03:
                continue
            for skin, part in ((sources[t].skins[i], 1.0 - w), (sources[t].skins[i + 1], w)):
                for joint, value in skin:
                    weights[joint] = weights.get(joint, 0.0) + value * part * share
        skins.append(softSkin(p, sorted(weights.items(), key=lambda jw: -jw[1])[:4]))
        coloured = [r for r in reach if sources[r[1]].rect]
        nearest.append({r[1]: r for r in coloured})
    made = {}
    def placeOf(t, v):   # (where a point falls on the coat's picture, seen from tube t)
        r = nearest[v].get(t)
        s = sources[t]
        if r is None:
            return None
        _, _, i, w, x, y = r
        a = (s.along[i] + (s.along[i + 1] - s.along[i]) * w)
        turn = (math.atan2(y, x) + math.pi / 2.0) / (2.0 * math.pi) % 1.0
        return (s.rect[0] + (s.rect[2] - s.rect[0]) * a, turn)
    for a, b, c in tris:
        scores = {}
        for v in (a, b, c):
            for t, r in nearest[v].items():
                scores[t] = scores.get(t, 0.0) + r[0]
        t = min(scores, key=lambda k: scores[k] + (1e3 if any(k not in nearest[v] for v in (a, b, c)) else 0.0))
        s = sources[t]
        uv = [placeOf(t, v) for v in (a, b, c)]
        turns = [q[1] for q in uv]
        if max(turns) - min(turns) > 0.5:   # (across the seam under the belly: all on one side of it)
            uv = [(q[0], 0.999 if q[1] < 0.5 else q[1]) for q in uv]
        ids = []
        for v, q in zip((a, b, c), uv):
            key = (v, t, q[1] == 0.999)
            if key not in made:
                made[key] = s.mesh.vertex(points[v], normals[v], skins[v], (q[0], s.rect[1] + (s.rect[3] - s.rect[1]) * q[1]))
            ids.append(made[key])
        s.mesh.idx += [ids[0], ids[2], ids[1]]
    return len(points), len(tris)


def sheet(mesh, base, tip, wide, cup, skin, rows=7, columns=6):
    """One layer of skin from `base` to `tip`, `wide` across at its broadest and hollowed along `cup` (a vector: how deep and which way): an ear, which is thin enough for the light to
    come through."""
    along = sub(tip, base)
    across = norm(cross(along, cup))
    grid = []
    for r in range(rows + 1):
        u = r / rows
        half = 0.5 * wide * math.sin(math.pi * min(1.0, 0.16 + 0.84 * u)) ** 0.7 * (1.0 - 0.15 * u)
        row = []
        for c in range(columns + 1):
            w = 2.0 * c / columns - 1.0
            row.append(add(add(add(base, mul(along, u)), mul(across, half * w)), mul(cup, 1.0 - w * w)))
        grid.append(row)
    ids = []
    for r in range(rows + 1):
        ids.append([])
        for c in range(columns + 1):
            du = sub(grid[min(r + 1, rows)][c], grid[max(r - 1, 0)][c])
            dv = sub(grid[r][min(c + 1, columns)], grid[r][max(c - 1, 0)])
            ids[r].append(mesh.vertex(grid[r][c], cross(dv, du), skin))
    for r in range(rows):
        for c in range(columns):
            a, b, d, e = ids[r][c], ids[r][c + 1], ids[r + 1][c + 1], ids[r + 1][c]
            mesh.idx += [a, d, b, a, e, d]


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
                "TEXCOORD_0": b.floats(flat(mesh.uv), "VEC2", 2),
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
# (round 13) each kind's eyes: the iris's colour and the pupil's shape - a grazer's a horizontal bar (a wide view of the horizon), a fox's a vertical slit,
# the rest round.
EYES = {"deer": ("bar", (0.16, 0.09, 0.04)), "horse": ("bar", (0.14, 0.07, 0.03)), "zebra": ("bar", (0.14, 0.08, 0.04)), "giraffe": ("bar", (0.18, 0.1, 0.05)),
        "elephant": ("round", (0.25, 0.15, 0.07)), "wolf": ("round", (0.75, 0.6, 0.2)), "lion": ("round", (0.72, 0.48, 0.12)), "fox": ("slit", (0.8, 0.45, 0.1)),
        "bear": ("round", (0.3, 0.16, 0.06)), "rabbit": ("round", (0.22, 0.11, 0.05))}

# A head's shape, from the poll to the nose: (along it, how wide, how deep, how far the jaw hangs below the nose's line), by family. A grazer's is long and deep at the jaw; a dog's has a
# broad skull, a sharp stop at the brow and a narrow muzzle; a cat's is short and blunt with heavy jowls; a bear's is broad and flat; a rabbit's round.
SKULLS = {
    "grazer": [(0.0, 0.55, 0.6, 0.0), (0.08, 0.9, 1.0, 0.1), (0.2, 1.08, 1.32, 0.3), (0.34, 0.95, 1.28, 0.34), (0.5, 0.72, 0.96, 0.24), (0.66, 0.58, 0.72, 0.14), (0.82, 0.6, 0.66, 0.1),
               (0.93, 0.64, 0.66, 0.08), (0.985, 0.46, 0.46, 0.06), (1.0, 0.12, 0.12, 0.06)],
    "dog": [(0.0, 0.6, 0.62, 0.0), (0.08, 0.95, 1.0, 0.06), (0.2, 1.15, 1.15, 0.12), (0.34, 1.05, 1.0, 0.16), (0.46, 0.72, 0.62, 0.14), (0.62, 0.52, 0.46, 0.12), (0.8, 0.46, 0.4, 0.1),
            (0.93, 0.46, 0.38, 0.08), (0.985, 0.34, 0.28, 0.06), (1.0, 0.1, 0.1, 0.06)],
    "cat": [(0.0, 0.7, 0.7, 0.0), (0.1, 1.0, 1.0, 0.05), (0.25, 1.2, 1.12, 0.12), (0.42, 1.15, 1.0, 0.18), (0.58, 0.9, 0.82, 0.2), (0.74, 0.82, 0.74, 0.18), (0.88, 0.78, 0.66, 0.14),
            (0.96, 0.62, 0.5, 0.1), (0.99, 0.42, 0.34, 0.1), (1.0, 0.16, 0.14, 0.1)],
    "bear": [(0.0, 0.75, 0.7, 0.0), (0.1, 1.05, 1.0, 0.05), (0.28, 1.2, 1.1, 0.12), (0.45, 1.05, 0.92, 0.18), (0.62, 0.72, 0.66, 0.18), (0.8, 0.62, 0.56, 0.14), (0.93, 0.58, 0.5, 0.1),
             (0.985, 0.44, 0.38, 0.08), (1.0, 0.12, 0.12, 0.08)],
    "rabbit": [(0.0, 0.7, 0.7, 0.0), (0.15, 1.0, 1.0, 0.05), (0.35, 1.1, 1.05, 0.1), (0.55, 0.95, 0.9, 0.14), (0.75, 0.75, 0.72, 0.14), (0.9, 0.55, 0.5, 0.1), (0.98, 0.34, 0.3, 0.08), (1.0, 0.14, 0.12, 0.08)],
}
FAMILY = {"wolf": "dog", "fox": "dog", "lion": "cat", "bear": "bear", "rabbit": "rabbit"}
PANTS = ("wolf", "fox", "lion")   # (a dog or a cat cools itself through its open mouth)

# What a muzzle ends in: the colour of its bare wet skin.
NOSES = {"deer": (0.03, 0.025, 0.025), "horse": (0.1, 0.085, 0.08), "wolf": (0.015, 0.014, 0.014), "lion": (0.32, 0.17, 0.13), "zebra": (0.02, 0.02, 0.02), "giraffe": (0.07, 0.055, 0.05),
         "bear": (0.02, 0.018, 0.018), "fox": (0.02, 0.016, 0.016), "rabbit": (0.45, 0.26, 0.25)}

SPECIES = {
    "deer": dict(H=1.0, Lb=0.95, barrel=(0.2, 0.25), neck=0.55, neckUp=55, head=0.3, headR=0.08, tail=0.14, tailR=0.04, ears=0.13, hoof=True, antlers=True, mane=False,
                 body=(0.36, 0.21, 0.11), legs=(0.30, 0.19, 0.11), feet=(0.05, 0.04, 0.035), extra=(0.42, 0.36, 0.28), swing=(22, 26, 34)),
    "horse": dict(H=1.55, Lb=1.35, barrel=(0.3, 0.37), neck=0.85, neckUp=45, head=0.6, headR=0.12, tail=0.7, tailR=0.08, ears=0.14, hoof=True, antlers=False, mane=True,
                  body=(0.28, 0.13, 0.06), legs=(0.20, 0.10, 0.05), feet=(0.06, 0.05, 0.045), extra=(0.04, 0.03, 0.025), swing=(20, 25, 33)),
    "wolf": dict(H=0.8, Lb=0.8, barrel=(0.15, 0.2), neck=0.32, neckUp=25, head=0.3, headR=0.09, tail=0.45, tailR=0.07, ears=0.11, hoof=False, antlers=False, mane=False,
                 body=(0.33, 0.30, 0.26), legs=(0.42, 0.39, 0.34), feet=(0.12, 0.11, 0.1), extra=(0.08, 0.07, 0.06), swing=(22, 26, 36), tailHang=False, bushy=1.6),
    # (round 13) the savanna's and the forest's: each with the parts that make it what it is
    "lion": dict(H=1.05, Lb=1.1, barrel=(0.24, 0.29), neck=0.36, neckUp=22, head=0.38, headR=0.14, tail=0.85, tailR=0.04, ears=0.07, hoof=False, antlers=False, mane=False,
                 body=(0.72, 0.52, 0.28), legs=(0.70, 0.52, 0.30), feet=(0.55, 0.40, 0.25), extra=(0.40, 0.22, 0.10), swing=(22, 26, 38), tailHang=True, lionMane=True, tuft=True,
                 flexRun=14, gaits={"walk": ((0.0, 0.25, 0.5, 0.75), 0.65), "trot": ((0.0, 0.5, 0.5, 0.0), 0.45), "run": ((0.0, 0.5, 0.1, 0.6), 0.28)}),
    "zebra": dict(H=1.35, Lb=1.15, barrel=(0.27, 0.33), neck=0.72, neckUp=45, head=0.52, headR=0.11, tail=0.5, tailR=0.05, ears=0.15, hoof=True, antlers=False, mane=True,
                  body=(0.93, 0.92, 0.88), legs=(0.92, 0.91, 0.87), feet=(0.05, 0.05, 0.045), extra=(0.05, 0.05, 0.05), swing=(20, 25, 33), tuft=True),
    "giraffe": dict(H=2.6, Lb=1.3, barrel=(0.3, 0.42), neck=1.9, neckUp=62, head=0.6, headR=0.11, tail=0.9, tailR=0.04, ears=0.14, hoof=True, antlers=False, mane=True,
                    body=(0.86, 0.68, 0.40), legs=(0.88, 0.75, 0.52), feet=(0.12, 0.09, 0.07), extra=(0.40, 0.25, 0.12), swing=(18, 22, 30), ossicones=True, tuft=True),
    "elephant": dict(H=2.7, Lb=1.9, barrel=(0.72, 0.85), neck=0.35, neckUp=8, head=0.75, headR=0.42, tail=0.9, tailR=0.04, ears=0.0, hoof=False, antlers=False, mane=False,
                     body=(0.42, 0.40, 0.38), legs=(0.40, 0.38, 0.36), feet=(0.30, 0.28, 0.26), extra=(0.93, 0.88, 0.76), swing=(16, 20, 24), trunk=True, tusks=True, bigEars=True,
                     pillar=True, tuft=True, legThick=0.6),
    "bear": dict(H=1.0, Lb=1.15, barrel=(0.38, 0.44), neck=0.3, neckUp=12, head=0.4, headR=0.15, tail=0.08, tailR=0.05, ears=0.07, hoof=False, antlers=False, mane=False,
                 body=(0.28, 0.18, 0.11), legs=(0.24, 0.15, 0.09), feet=(0.10, 0.07, 0.05), extra=(0.20, 0.13, 0.08), swing=(20, 24, 32), tailHang=False, roundEars=True),
    "fox": dict(H=0.38, Lb=0.45, barrel=(0.085, 0.10), neck=0.15, neckUp=30, head=0.17, headR=0.05, tail=0.42, tailR=0.045, ears=0.08, hoof=False, antlers=False, mane=False,
                body=(0.80, 0.36, 0.10), legs=(0.20, 0.12, 0.08), feet=(0.10, 0.07, 0.05), extra=(0.95, 0.93, 0.90), swing=(22, 26, 38), tailHang=False, bushy=2.2,
                whiteTip=True, flexRun=11),
    "rabbit": dict(H=0.2, Lb=0.24, barrel=(0.07, 0.085), neck=0.05, neckUp=35, head=0.09, headR=0.04, tail=0.035, tailR=0.03, ears=0.11, hoof=False, antlers=False, mane=False,
                   body=(0.48, 0.40, 0.32), legs=(0.45, 0.38, 0.30), feet=(0.85, 0.82, 0.78), extra=(0.95, 0.94, 0.92), swing=(28, 34, 42), tailHang=False, whiteTail=True,
                   gaits={"walk": ((0.0, 0.3, 0.0, 0.3), 0.55), "trot": ((0.0, 0.4, 0.0, 0.4), 0.4), "run": ((0.0, 0.45, 0.04, 0.5), 0.3)}, hop=True, flexRun=14),
}
# Creatures of other worlds, from the same generator: a strider on long thin legs with a small body slung between them and a long searching neck; a low broad crawler.
SPECIES.update({
    "strider": dict(H=2.4, Lb=0.8, barrel=(0.2, 0.24), neck=1.3, neckUp=38, head=0.42, headR=0.08, tail=0.9, tailR=0.03, ears=0.2, hoof=True, antlers=False, mane=False,
                    body=(0.12, 0.3, 0.32), legs=(0.08, 0.2, 0.24), feet=(0.05, 0.1, 0.1), extra=(0.9, 0.5, 0.2), swing=(16, 20, 26), legThick=0.7),
    "crawler": dict(H=0.6, Lb=1.2, barrel=(0.36, 0.22), neck=0.2, neckUp=8, head=0.4, headR=0.16, tail=1.0, tailR=0.09, ears=0.0, hoof=False, antlers=False, mane=False,
                    body=(0.34, 0.14, 0.36), legs=(0.28, 0.1, 0.3), feet=(0.1, 0.05, 0.1), extra=(0.1, 0.5, 0.45), swing=(22, 26, 32), tailHang=False, flexRun=12, bushy=0.8),
})
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
    # what moves under the skin: a shoulder blade and a haunch to a side, sliding as the leg works; the belly, swinging late; the flank, which twitches. And the ears.
    rx0, ry0 = s["barrel"]
    midY = 0.5 * (hipY + shoulderY) - 0.03 * H
    earAt = {side: add(headPos, [side * s["headR"] * 0.6, s["headR"] * 0.9, -0.05 * s["head"]]) for side in (1, -1)}
    hd = [0, -math.sin(math.radians(35)), math.cos(math.radians(35))]
    below = norm(cross(hd, [1, 0, 0]))
    if below[1] > 0: below = mul(below, -1.0)
    hinge = add(add(headPos, mul(hd, 0.2 * s["head"])), mul(below, 0.62 * s["headR"]))
    eyeR = max(0.016 * H, 0.14 * s["headR"])
    eyeAt = {side: add(add(headPos, add(mul(hd, 0.12 * s["head"]), [side * s["headR"] * 0.85, s["headR"] * 0.35, 0])), [-side * 0.4 * eyeR, 0, 0]) for side in (1, -1)}
    joints += [("blade.l", 1, [rx0 * 0.8, shoulderY - 0.16 * H, Lb / 2 - 0.02 * H]), ("blade.r", 1, [-rx0 * 0.8, shoulderY - 0.16 * H, Lb / 2 - 0.02 * H]),
               ("haunch.l", 0, [rx0 * 0.85, hipY - 0.16 * H, -Lb / 2]), ("haunch.r", 0, [-rx0 * 0.85, hipY - 0.16 * H, -Lb / 2]),
               ("belly", 0, [0, midY - ry0 * 0.8, 0.1 * Lb]), ("flank.l", 0, [rx0 * 0.95, midY, 0.05 * Lb]), ("flank.r", 0, [-rx0 * 0.95, midY, 0.05 * Lb]),
               ("ear.l", 3, earAt[1]), ("ear.r", 3, earAt[-1]), ("jaw", 3, hinge), ("tongue", len(joints) + 9, add(hinge, mul(hd, 0.3 * s["head"]))), ("blink.l", 3, eyeAt[1]), ("blink.r", 3, eyeAt[-1])]
    J = {n: i for i, (n, _, _) in enumerate(joints)}
    P = {n: p for n, _, p in joints}
    body, belly, feet, extra, eyes, pupil, nose, ear, lid, tongue = Mesh(), Mesh(), Mesh(), Mesh(), Mesh(), Mesh(), Mesh(), Mesh(), Mesh(), Mesh()
    # how much of the barrel's skin each of them carries: a soft patch, so the skin rolls and does not fold
    soft = [(J["blade.l"], P["blade.l"], (0.8 * rx0, 0.2 * H, 0.2 * H), 1), (J["blade.r"], P["blade.r"], (0.8 * rx0, 0.2 * H, 0.2 * H), -1),
            (J["haunch.l"], P["haunch.l"], (0.8 * rx0, 0.22 * H, 0.22 * H), 1), (J["haunch.r"], P["haunch.r"], (0.8 * rx0, 0.22 * H, 0.22 * H), -1),
            (J["belly"], P["belly"], (1.3 * rx0, 0.5 * ry0, 0.42 * Lb), 0),
            (J["flank.l"], P["flank.l"], (0.5 * rx0, 0.16 * H, 0.17 * H), 1), (J["flank.r"], P["flank.r"], (0.5 * rx0, 0.16 * H, 0.17 * H), -1)]
    def softSkin(p, skin):
        moved, taken = [], 0.0
        for joint, c, r, side in soft:
            if side * p[0] < 0:
                continue
            d = sum(((p[i] - c[i]) / r[i]) ** 2 for i in range(3))
            w = 0.8 * math.exp(-d)
            if w > 0.02:
                moved.append((joint, w))
                taken += w
        if taken > 0.85:
            moved = [(j, w * 0.85 / taken) for j, w in moved]
            taken = 0.85
        return [(j, w * (1.0 - taken)) for j, w in skin] + moved

    # the barrel: from behind the hips to before the chest, deepest at the girth, the belly line a little up towards the flank
    # A body is its lines: the top line runs from the croup, down a dip in the back, up to the withers; the belly tucks up to the flank and deepens behind the elbow; hips, flank
    # and ribs have their own widths. (An elephant's or a bear's, `round`, keeps most of its bulk.)
    centres, radii, skins = [], [], []
    rx, ry = s["barrel"]
    z0, z1 = -Lb / 2 - 0.22 * H, Lb / 2 + 0.2 * H
    def bump(u, at, width): return math.exp(-((u - at) / width) ** 2)
    line = 0.35 if s.get("pillar") or s.get("roundEars") else 1.0
    for i in range(37):
        u = 0.5 - 0.5 * math.cos(math.pi * i / 36.0)   # (the rings closer together towards the ends, where the body turns in)
        z = z0 + (z1 - z0) * u
        shape = max(0.02, math.sin(math.pi * u)) ** 0.42   # (rounded at the rump and the breast, not cut off)
        mid = lerp(pelvis, chest, u)[1] - 0.03 * H
        top = mid + ry * shape * (1.0 + line * (0.1 * bump(u, 0.74, 0.1) - 0.1 * bump(u, 0.5, 0.17) + 0.07 * bump(u, 0.2, 0.12)))    # (withers, the dip of the back, the croup)
        under = mid - ry * shape * (1.0 + line * (0.2 * bump(u, 0.68, 0.17) - 0.25 * bump(u, 0.27, 0.13)))                           # (the deep girth, the tucked flank)
        width = rx * shape * (1.0 + line * (0.13 * bump(u, 0.2, 0.13) - 0.14 * bump(u, 0.4, 0.09) + 0.08 * bump(u, 0.62, 0.12) + 0.09 * bump(u, 0.8, 0.08)))   # (the haunches, the flank, the ribs, the shoulders)
        centres.append([0, 0.5 * (top + under), z])
        radii.append((width, 0.5 * (top - under)))
        wc = min(1.0, max(0.0, (u - 0.35) / 0.3))
        skins.append([(J["pelvis"], 1 - wc), (J["chest"], wc)])
    fused = [Source(centres, radii, skins, rect=(0.0, 0.0, 0.5, 0.5), mesh=body)]
    # the neck: an arched crest from the withers to the poll, deep at its base and narrowing to the throat
    centres, radii, skins = [], [], []
    neckStart = add(neckBase, [0, -0.16 * H, -0.14 * H])
    crown = add(lerp(neckStart, headPos, 0.55), [0, 0.1 * s["neck"], -0.05 * s["neck"]])
    for i in range(13):
        u = i / 12.0
        p = add(mul(neckStart, (1 - u) ** 2), add(mul(crown, 2 * u * (1 - u)), mul(headPos, u * u)))
        grows = min(1.0, 0.45 + 3.0 * u)   # (out of the chest, with no collar where it meets it)
        r = (0.58 * rx * grows * (1 - 0.55 * u ** 0.8), 0.86 * ry * grows * (1 - 0.62 * u ** 0.75))
        centres.append(p)
        radii.append(r)
        skins.append([(J["chest"], max(0.0, 1 - 2.5 * u)), (J["neck"], min(1.0, 2.5 * u) * (1 - max(0.0, (u - 0.7) / 0.3))), (J["head"], max(0.0, (u - 0.7) / 0.3))])
    fused.append(Source(centres, radii, skins, rect=(0.5, 0.0, 0.5 + 0.25 * min(1.0, s["neck"] / 0.8), 0.5), mesh=body))
    # the head: from the poll down to the muzzle, wide at the forehead, deep at the jaw, narrowing along the nose and flaring a little at the nostrils
    hd = [0, -math.sin(math.radians(35)), math.cos(math.radians(35))]
    below = norm(cross(hd, [1, 0, 0]))
    if below[1] > 0: below = mul(below, -1.0)
    profile = SKULLS[FAMILY.get(name, "grazer")]
    centres, radii = [], []
    open_ = not s.get("trunk")   # (the upper head ends where the mouth is, and the lower jaw is its own: an elephant's stays shut behind its trunk)
    for u, wide, deep, jaw in profile:
        upper = 0.8 if open_ and u >= 0.4 else 1.0
        centres.append(add(add(add(headPos, mul(hd, (u - 0.15) * s["head"])), mul(below, jaw * s["headR"])), mul(below, -(1.0 - upper) * 0.6 * deep * s["headR"])))
        radii.append((s["headR"] * wide, s["headR"] * deep * upper))
    hr = s["headR"]
    # the bones of a face under its skin: the ridge over each eye, the muscle of the cheek
    skull = [Source(centres, radii, [[(J["head"], 1.0)]] * len(profile), rect=(0.75, 0.0, 1.0, 0.25), mesh=body)]
    for side in (1, -1):
        brow = add(headPos, add(mul(hd, 0.1 * s["head"]), [side * 0.74 * hr, 0.6 * hr, 0]))
        skull.append(Source([add(brow, mul(hd, 0.13 * s["head"] * u)) for u in (-1.0, -0.4, 0.4, 1.0)], [(0.03 * hr, 0.03 * hr), (0.2 * hr, 0.16 * hr), (0.2 * hr, 0.16 * hr), (0.03 * hr, 0.03 * hr)], [[(J["head"], 1.0)]] * 4, up=(1, 0, 0)))
        cheek = add(add(headPos, add(mul(hd, 0.3 * s["head"]), [side * 0.66 * hr, 0, 0])), mul(below, 0.5 * hr))
        skull.append(Source([add(cheek, mul(hd, 0.2 * s["head"] * u)) for u in (-1.0, -0.4, 0.4, 1.0)], [(0.03 * hr, 0.03 * hr), (0.26 * hr, 0.46 * hr), (0.26 * hr, 0.46 * hr), (0.03 * hr, 0.03 * hr)], [[(J["head"], 1.0)]] * 4, up=(1, 0, 0)))
    fuse(skull, 0.13 * hr, 0.22 * hr, lambda p, skin: skin)
    if open_:   # the lower jaw, hinged behind the cheek; the mouth's dark roof and floor, and the tongue, which only an open mouth shows
        rows = [p for p in profile if 0.36 <= p[0] <= 0.97]
        jawLine = [add(add(add(headPos, mul(hd, (u - 0.15) * s["head"])), mul(below, jaw * hr)), mul(below, 0.74 * deep * hr)) for u, wide, deep, jaw in rows]
        jawR = [(0.68 * wide * hr, 0.26 * deep * hr) for u, wide, deep, jaw in rows]
        jawR[-1] = (jawR[-1][0] * 0.4, jawR[-1][1] * 0.4)
        n = len(rows)
        tube(body, jawLine, jawR, [[(J["jaw"], 1.0)]] * n, sides=12, up=below, rect=(0.75 + 0.25 * rows[0][0], 0.0, 0.75 + 0.25 * rows[-1][0], 0.25))
        roof = [add(c, mul(below, -0.5 * r[1])) for c, r in zip(jawLine, jawR)]
        tube(lid, jawLine, [(r[0] * 0.9, r[1] * 1.15) for r in jawR], [[(J["jaw"], 1.0)]] * n, sides=8, up=below)
        tube(lid, roof, [(r[0] * 0.9, r[1] * 0.8) for r in jawR], [[(J["head"], 1.0)]] * n, sides=8, up=below)
        tongueWay = [add(c, mul(below, -1.05 * r[1])) for c, r in zip(jawLine, jawR)]
        tube(tongue, tongueWay, [(r[0] * 0.55, r[1] * 0.42) for r in jawR], [[(J["tongue"], 1.0)]] * n, sides=8, up=below)
    if name in NOSES:   # the end of the muzzle, bare and wet, its nostrils dark; and the line of the mouth back along the jaw
        tip = centres[-1]
        back = mul(hd, -1.0)
        tube(nose, [add(tip, mul(back, 0.1 * s["head"])), add(tip, mul(back, 0.05 * s["head"])), add(tip, mul(back, 0.012 * s["head"])), add(tip, mul(hd, 0.008 * s["head"]))],
             [(0.45 * hr, 0.3 * hr), (0.6 * hr, 0.46 * hr), (0.5 * hr, 0.42 * hr), (0.14 * hr, 0.12 * hr)], [[(J["head"], 1.0)]] * 4, sides=12, up=below)
        for side in (1, -1):
            n0 = add(add(add(tip, mul(back, 0.02 * s["head"])), [side * 0.3 * hr, 0, 0]), mul(below, -0.1 * hr))
            tube(lid, [add(n0, mul(hd, -0.012 * s["head"])), add(n0, mul(hd, 0.012 * s["head"]))], [(0.12 * hr, 0.07 * hr)] * 2, [[(J["head"], 1.0)]] * 2, sides=8, up=below)
            line = []
            for u, wide, deep, jaw in profile[3:-1]:
                at = add(add(headPos, mul(hd, (u - 0.15) * s["head"])), mul(below, (jaw + 0.58 * deep) * hr))
                line.append(add(at, [side * 0.7 * wide * hr, 0, 0]))
            tube(lid, line, [(0.035 * hr, 0.035 * hr)] * len(line), [[(J["head"], 1.0)]] * len(line), sides=5)
    if s.get("trunk"):   # an elephant's trunk: on from the muzzle, hanging, curling a little forward at its tip
        m0 = centres[-1]
        trunk = [m0, add(m0, [0, -0.25 * H, 0.06 * H]), add(m0, [0, -0.5 * H, 0.08 * H]), add(m0, [0, -0.68 * H, 0.06 * H]), add(m0, [0, -0.76 * H, 0.1 * H])]
        tr = [(0.12 * H, 0.11 * H), (0.085 * H, 0.08 * H), (0.06 * H, 0.055 * H), (0.045 * H, 0.04 * H), (0.035 * H, 0.03 * H)]
        tube(body, trunk, tr, [[(J["head"], 1.0)]] * 5, sides=12, rect=(0.75, 0.25, 1.0, 0.5))
    if s.get("tusks"):
        for side in (1, -1):
            b0 = add(centres[-2], [side * s["headR"] * 0.45, -s["headR"] * 0.35, 0])
            tube(extra, [b0, add(b0, [side * 0.03 * H, -0.12 * H, 0.1 * H]), add(b0, [side * 0.05 * H, -0.16 * H, 0.22 * H]), add(b0, [side * 0.05 * H, -0.12 * H, 0.3 * H])],
                 [(0.035 * H, 0.035 * H), (0.03 * H, 0.03 * H), (0.022 * H, 0.022 * H), (0.004 * H, 0.004 * H)], [[(J["head"], 1.0)]] * 4, sides=8)
    if s.get("bigEars"):   # an elephant's ears: great flaps from the sides of the head, hanging back over the neck
        for side in (1, -1):
            e0 = add(headPos, [side * s["headR"] * 0.8, s["headR"] * 0.2, -0.05])
            panel(body, [e0, add(e0, [side * 0.1 * H, 0.12 * H, -0.12 * H]), add(e0, [side * 0.16 * H, -0.05 * H, -0.3 * H]), add(e0, [side * 0.12 * H, -0.32 * H, -0.22 * H]),
                         add(e0, [side * 0.04 * H, -0.3 * H, -0.05 * H])], 0.02 * H, [[(J["head"], 1.0)]] * 5, [side, 0, 0.25])
    if s.get("ossicones"):   # a giraffe's horns: two short furred knobs on its crown
        for side in (1, -1):
            o0 = add(headPos, [side * s["headR"] * 0.4, s["headR"] * 0.85, -0.03])
            tube(extra, [o0, add(o0, [side * 0.01, 0.11, -0.02]), add(o0, [side * 0.012, 0.13, -0.022])], [(0.022, 0.022), (0.018, 0.018), (0.028, 0.028)], [[(J["head"], 1.0)]] * 3, sides=8)
    if s.get("lionMane"):   # a lion's mane: a shaggy ruff round the head and down the neck, longer under the chin
        for i in range(36):
            a = 2 * math.pi * i / 36
            for ring, (u, length) in enumerate(((0.85, 0.22), (0.55, 0.26), (0.25, 0.2))):
                base = lerp(add(neckBase, [0, -0.02 * H, -0.04 * H]), headPos, u)
                d = norm([math.cos(a), math.sin(a) * 0.9 - 0.15, -0.35])
                r = 0.6 * rx * (1 - 0.3 * u) + 0.02
                root = add(base, mul([math.cos(a), math.sin(a), 0], r))
                l = length * H * (1.25 if math.sin(a) < -0.3 else 1.0) * (0.8 + 0.4 * ((i * 7 + ring * 3) % 5) / 4)
                tube(extra, [root, add(root, mul(d, l * 0.6)), add(root, add(mul(d, l), [0, -0.05 * l, -0.1 * l]))], [(0.05 * H, 0.03 * H), (0.035 * H, 0.02 * H), (0.004, 0.004)],
                     [[(J["neck"], 1.0 - u), (J["head"], u)]] * 3, sides=5)
    for side in (1, -1):   # eyes and ears
        e = add(headPos, add(mul(hd, 0.12 * s["head"]), [side * s["headR"] * 0.85, s["headR"] * 0.35, 0]))
        # the eye: a ball set in its socket, wet, so it holds the sky; the pupil curved on it, (across, up) in the shape of its kind's; dark lids round it
        r = eyeR
        c = eyeAt[side]
        # the upper lid, up in its socket until it blinks
        arc = [0.02, 0.5, 0.9, 1.15]
        tube(lid, [add(c, [0, 1.07 * r * math.cos(a), 0]) for a in arc], [(1.07 * r * math.sin(a), 1.07 * r * math.sin(a)) for a in arc], [[(J["blink.l" if side > 0 else "blink.r"], 1.0)]] * 4, sides=12, up=(0, 0, 1), caps=False)
        c = eyeAt[side]
        ball = [(-1.0, 0.02), (-0.7, 0.71), (-0.3, 0.95), (0.2, 0.98), (0.6, 0.8), (0.87, 0.5), (1.0, 0.03)]
        tube(eyes, [add(c, [side * a * r, 0, 0]) for a, _ in ball], [(b * r, b * r) for _, b in ball], [[(J["head"], 1.0)]] * len(ball), sides=14, up=(0, 0, 1))
        shape = EYES.get(name, ("round", None))[0]
        pr = {"bar": (0.62, 0.2), "slit": (0.11, 0.62)}.get(shape, (0.42, 0.42))
        cap = [1.0, 0.8, 0.5, 0.02]   # (how wide of the pupil, from its edge to the front of the ball)
        tube(pupil, [add(c, [side * r * 1.012 * math.sqrt(1.0 - (a * max(pr)) ** 2), 0, 0]) for a in cap], [(a * pr[1] * r, a * pr[0] * r) for a in cap], [[(J["head"], 1.0)]] * 4, sides=12, up=(0, 0, 1))
        rim = [add(e, [side * 0.1 * r, 1.0 * r * math.sin(2 * math.pi * k / 16), 1.0 * r * math.cos(2 * math.pi * k / 16)]) for k in range(17)]
        tube(lid, rim, [(0.14 * r, 0.14 * r)] * 17, [[(J["head"], 1.0)]] * 17, sides=6, caps=False)
        eb = earAt[side]
        et = add(eb, [side * 0.35 * s["ears"], 0.9 * s["ears"], -0.15 * s["ears"]])
        earJoint = J["ear.l" if side > 0 else "ear.r"]
        if s["ears"] > 0 and s.get("roundEars"):   # (a bear's: small and round)
            tube(body, [eb, lerp(eb, et, 0.5), lerp(eb, et, 0.8)], [(0.3 * s["ears"], 0.15 * s["ears"]), (0.35 * s["ears"], 0.15 * s["ears"]), (0.12 * s["ears"], 0.1 * s["ears"])],
                 [[(J["head"], 1.0)]] * 3, sides=8)
        elif s["ears"] > 0:   # (a single sheet of skin, hollowed towards the front and out; a low sun shines through it)
            sheet(ear, add(eb, [-side * 0.08 * s["ears"], -0.28 * s["ears"], 0.0]), et, 0.62 * s["ears"], mul(norm([side * 0.5, 0.0, 1.0]), -0.1 * s["ears"]), [(earJoint, 1.0)])   # (its root inside the skull)
    if s["antlers"]:   # a deer's antlers: a beam back and up from each side of the crown, with tines forward
        for side in (1, -1):
            base = add(headPos, [side * s["headR"] * 0.45, s["headR"] * 1.0, 0.0])
            beam = [base, add(base, [side * 0.08, 0.16, -0.06]), add(base, [side * 0.16, 0.30, -0.04]), add(base, [side * 0.19, 0.44, 0.04]), add(base, [side * 0.16, 0.55, 0.12])]
            tube(extra, beam, [(0.022, 0.022), (0.019, 0.019), (0.016, 0.016), (0.012, 0.012), (0.004, 0.004)], [[(J["head"], 1.0)]] * 5, sides=7)
            for k, at in ((1, 0.1), (2, 0.12), (3, 0.1)):
                p = beam[k]
                tube(extra, [p, add(p, [side * 0.01, at * 0.8, at])], [(0.012, 0.012), (0.003, 0.003)], [[(J["head"], 1.0)]] * 2, sides=6)
    if s["mane"]:   # a horse's mane: locks of hair from the crest, falling to one side; a zebra's and a giraffe's stand up, short and stiff
        hangs = name == "horse"
        for i in range(46):
            u = i / 45.0
            p = add(mul(add(neckStart, [0, 0.72 * ry, 0.02]), (1 - u) ** 2), add(mul(add(crown, [0, 0.52 * ry, 0]), 2 * u * (1 - u)), mul(add(headPos, [0, 0.8 * s["headR"], -0.03]), u * u)))
            lean = 0.5 * (hash01(i, 1) - 0.5)
            skin = [[(J["neck"], 1.0 - u), (J["head"], u)]] * 4
            if hangs:
                length = 0.16 * H * (0.7 + 0.5 * hash01(i, 2)) * (0.7 + 0.5 * math.sin(math.pi * u))
                ends = [p, add(p, [0.05 * length, 0.02 * length, -0.08 * length]), add(p, [0.26 * length, -0.45 * length, -0.1 * length]), add(p, [0.34 * length, -1.0 * length, lean * 0.2 * length])]
                tube(extra, ends, [(0.018, 0.026), (0.016, 0.024), (0.012, 0.016), (0.002, 0.002)], skin, sides=5)
            else:
                length = 0.1 * H * (0.8 + 0.4 * hash01(i, 2))
                ends = [p, add(p, [0.03 * lean * length, 0.45 * length, -0.08 * length]), add(p, [0.05 * lean * length, 0.8 * length, -0.14 * length]), add(p, [0.06 * lean * length, length, -0.16 * length])]
                tube(extra, ends, [(0.012, 0.032), (0.011, 0.03), (0.009, 0.024), (0.002, 0.004)], skin, sides=5)
    # the tail
    hang = s.get("tailHang", True)
    tip = add(tailBase, [0, -s["tail"] * (0.85 if hang else 0.6), -s["tail"] * (0.35 if hang else 0.75)])
    centres = [lerp(tailBase, tip, u / 5.0) for u in range(6)]
    bushy = s.get("bushy", 1.0)
    radii = [(s["tailR"] * (1.0 - 0.5 * u / 5.0) * (bushy if 1 <= u <= 4 else 1.0),) * 2 for u in range(6)]
    if name == "horse":   # a horse's tail: a short dock, and a fall of hair from it
        tube(extra, centres[:3], [(s["tailR"] * 0.8,) * 2, (s["tailR"] * 0.7,) * 2, (s["tailR"] * 0.4,) * 2], [[(J["tail"], 1.0)]] * 3, sides=8)
        for i in range(44):
            a, r = 2.0 * math.pi * hash01(i, 3), 0.7 * s["tailR"] * math.sqrt(hash01(i, 4))
            at = add(centres[1], [r * math.cos(a), r * math.sin(a) * 0.6, 0.0])
            reach = s["tail"] * (0.7 + 0.45 * hash01(i, 5))
            wander = 0.1 * reach * (hash01(i, 6) - 0.5)
            strand = [at, add(at, [wander * 0.2, -0.05 * reach, -0.12 * reach]), add(at, [wander * 0.6, -0.5 * reach, -0.25 * reach]), add(at, [wander, -1.0 * reach, -0.2 * reach])]
            tube(extra, strand, [(0.013, 0.013), (0.015, 0.015), (0.012, 0.012), (0.002, 0.002)], [[(J["tail"], 1.0)]] * 4, sides=5)
    else:
        tube(extra if name == "zebra" else body, centres, radii, [[(J["tail"], 1.0)]] * 6, sides=8, rect=None if name == "zebra" else (0.75, 0.25, 1.0, 0.5))
    if s.get("tuft"):   # a tuft at the tail's tip (a lion's, a zebra's, a giraffe's, an elephant's)
        tube(extra, [tip, add(tip, [0, -0.06 * H, -0.02 * H]), add(tip, [0, -0.11 * H, -0.03 * H])], [(s["tailR"] * 1.2,) * 2, (s["tailR"] * 1.6,) * 2, (0.005,) * 2], [[(J["tail"], 1.0)]] * 3, sides=6)
    if s.get("whiteTail"):   # a rabbit's white scut
        tube(extra, [add(tailBase, [0, -0.01, 0.01]), tip, add(tip, [0, 0.01, -0.02])], [(s["tailR"] * 1.3,) * 2, (s["tailR"] * 1.5,) * 2, (0.004,) * 2], [[(J["tail"], 1.0)]] * 3, sides=8)
    if s.get("whiteTip"):   # a fox's white tail tip
        tube(extra, [lerp(tailBase, tip, 0.8), tip, add(tip, mul(norm(sub(tip, tailBase)), 0.04))], [(s["tailR"] * bushy * 0.8,) * 2, (s["tailR"] * 0.9,) * 2, (0.004,) * 2],
             [[(J["tail"], 1.0)]] * 3, sides=8)
    # the legs: the upper leg (muscled, inside the body at its top), the lower leg, the foot, a hoof or a paw
    for leg, fore, side in LEGS:
        top, knee, foot = P[leg + ".upper"], P[leg + ".lower"], P[leg + ".foot"]
        ground = [foot[0], 0.0, foot[2] + (0.03 * H if fore > 0 else 0.02 * H)]
        upperTop = [top[0] * 0.55, top[1] + 0.09 * H, top[2]]   # (well inside the body: the leg grows out of it, and its top never shows)
        thick = rx * (0.55 if fore > 0 else 0.75) * s.get("legThick", 1.0)
        u_, l_, f_ = J[leg + ".upper"], J[leg + ".lower"], J[leg + ".foot"]
        parentJ = J["chest"] if fore > 0 else J["pelvis"]
        # a leg's own shape: the muscle of the upper leg running out into tendon above the knee (the hock behind), the joint itself standing out, the thin straight bone of the
        # cannon, the fetlock's knob, the pastern down to the foot. Each ring: where it is, how wide, how deep (front to back), what moves it.
        rings = [(upperTop, 0.25, 0.32, [(parentJ, 0.7), (u_, 0.3)]), (lerp(upperTop, knee, 0.1), 0.7, 0.9, [(parentJ, 0.5), (u_, 0.5)]), (lerp(upperTop, knee, 0.25), 0.92, 1.18, [(parentJ, 0.2), (u_, 0.8)]), (lerp(upperTop, knee, 0.45), 0.8, 1.04, [(u_, 1.0)]),
                 (lerp(upperTop, knee, 0.66), 0.56, 0.74, [(u_, 1.0)]), (lerp(upperTop, knee, 0.84), 0.36, 0.48, [(u_, 0.85), (l_, 0.15)]), (add(knee, [0, 0.03 * H, 0]), 0.33, 0.43, [(u_, 0.65), (l_, 0.35)]),
                 (knee, 0.38, 0.5, [(u_, 0.35), (l_, 0.65)]), (lerp(knee, foot, 0.18), 0.27, 0.34, [(l_, 1.0)]), (lerp(knee, foot, 0.5), 0.2, 0.25, [(l_, 1.0)]),
                 (lerp(knee, foot, 0.82), 0.2, 0.24, [(l_, 0.8), (f_, 0.2)]), (foot, 0.28, 0.33, [(l_, 0.5), (f_, 0.5)]), (lerp(foot, ground, 0.35), 0.2, 0.23, [(f_, 1.0)]), (lerp(foot, ground, 0.7), 0.18, 0.21, [(f_, 1.0)])]
        centres = [r[0] for r in rings]
        radii = [(r[1] * thick, r[2] * thick) for r in rings]
        skins = [r[3] for r in rings]
        if s.get("pillar"):   # (an elephant's legs: pillars, hardly narrowing to the foot)
            radii = radii[:3] + [(0.75 * thick, 0.8 * thick)] * (len(rings) - 3)
        legIndex = [l for l, _, _ in LEGS].index(leg)
        # the thigh or the forearm's top is part of the body's one skin, down past the knee, where the lean lower leg comes out of it
        rows = (0.5 + 0.125 * legIndex, 0.5 + 0.125 * (legIndex + 1))
        cut, end = 5, 9
        last = len(centres) - 1
        tapered = radii[:end - 2] + [(radii[end - 2][0] * 0.95, radii[end - 2][1] * 0.95), (radii[end - 1][0] * 0.7, radii[end - 1][1] * 0.7)]   # (it goes in under the lower leg's skin)
        fused.append(Source(centres[:end], tapered, skins[:end], up=(0, 0, 1), rect=(0.0, rows[0], 0.5, rows[1]), mesh=belly, along=[i / last for i in range(end)]))
        tube(belly, centres[cut:], [(radii[cut][0] * 0.9, radii[cut][1] * 0.9)] + radii[cut + 1:], skins[cut:], sides=14, up=(0, 0, 1), rect=(0.5 * cut / last, rows[0], 0.5, rows[1]))
        if s["hoof"]:
            tube(feet, [add(ground, [0, 0.07 * H, -0.01 * H]), add(ground, [0, 0.002, 0.012 * H])], [(0.21 * thick, 0.2 * thick), (0.26 * thick, 0.28 * thick)], [[(f_, 1.0)]] * 2, sides=10, up=(0, 0, 1))
        else:   # a paw: a pad forward of the foot
            tube(feet, [add(ground, [0, 0.03 * H, -0.03 * H]), add(ground, [0, 0.025 * H, 0.06 * H])], [(0.25 * thick, 0.14 * thick), (0.2 * thick, 0.1 * thick)], [[(f_, 1.0)]] * 2, sides=10)
    # what fills the skin out where a leg meets the body: the shoulder over its blade, the haunch, the breast between the forelegs
    for side in (1, -1):
        for bone, leg, at, wide, long_ in (("chest", "fl", [side * rx * 0.72, shoulderY - 0.17 * H, Lb / 2 + 0.01 * H], 0.4 * rx, 0.2 * H),
                                           ("pelvis", "hl", [side * rx * 0.74, hipY - 0.17 * H, -Lb / 2 - 0.05 * H], 0.48 * rx, 0.24 * H)):
            along = [add(at, [0, long_ * u, 0.12 * long_ * u]) for u in (1.0, 0.75, 0.3, -0.3, -0.75, -1.0)]
            sizes = [(wide * k, 0.6 * long_ * k) for k in (0.05, 0.66, 1.0, 0.85, 0.6, 0.3)]   # (a muscle tapers down into its tendon)
            fused.append(Source(along, sizes, [[(J[bone], 1.0 - u), (J[leg + ".upper"], u)] for u in (0.0, 0.1, 0.35, 0.65, 0.9, 1.0)]))
    if not s.get("hop"):
        at = [0, shoulderY - 0.25 * H, Lb / 2 + 0.17 * H]
        fused.append(Source([add(at, [0, 0.1 * H * u, 0.03 * H * u]) for u in (1.0, 0.5, -0.5, -1.0)], [(rx * k, rx * k * 0.8) for k in (0.08, 0.5, 0.5, 0.08)], [[(J["chest"], 1.0)]] * 4))
    points, triangles = fuse(fused, 0.015 * (H + 3.0 * rx), 0.05 * H, softSkin)
    print("  %s: one skin of %d points, %d triangles" % (name, points, triangles))
    materials = [material("coat", s["body"]), material("legs", s["legs"]), material("hooves", s["feet"], 0.5), material("extra", s["extra"], 0.6),
                 material("eyes", EYES.get(name, (None, (0.02, 0.015, 0.01)))[1], 0.03), material("pupil", (0.004, 0.004, 0.004), 0.03),
                 material("nose", NOSES.get(name, (0.05, 0.04, 0.04)), 0.16), material("ear", s["body"], 0.9), material("lid", (0.012, 0.01, 0.01), 0.45), material("tongue", (0.62, 0.27, 0.28), 0.25)]
    meshes = [body, belly, feet, extra, eyes, pupil, nose, ear, lid, tongue]

    # ---- the clips ----
    clips = []
    swings = dict(zip(("walk", "trot", "run"), s["swing"]))
    gaits = s.get("gaits", GAITS)
    n = 32
    times = [i / n for i in range(n + 1)]
    pelvisRest, chestRest = pelvis, chest
    theta = lambda v: math.atan2(-v[2], -v[1])   # (a leg's angle in the plane of its swing, 0 straight down, a turn about +x adds to it)
    for gait, (starts, duty) in gaits.items():
        A = swings[gait]
        tracks = []
        beats = 1 if gait == "run" or s.get("hop") else 2   # (the body bobs once a step: twice a cycle in the walk and trot, once in the gallop and a hop)
        if s.get("hop"):   # (a hop: up off the hind feet and down onto the fore)
            bob = [[pelvisRest[0], pelvisRest[1] + (0.35 if gait == "run" else 0.25) * H * max(0.0, math.sin(2 * math.pi * t)), pelvisRest[2]] for t in times]
        else:
            bob = [[pelvisRest[0], pelvisRest[1] - 0.012 * H + (0.03 if gait == "run" else 0.012) * H * math.cos(2 * math.pi * beats * t), pelvisRest[2]] for t in times]
        flexA = s.get("flexRun", 7) if gait == "run" or s.get("hop") else 1.5   # the spine flexing (the gallop's gathering and stretching: deep in a cat's bound)
        # the weight going from side to side: the hips dropping over the leg that is off the ground, the shoulders the other way
        sway = 3.0 if gait == "run" else 2.6 if gait == "walk" else 1.6
        pelvisQ = [qmul(quat(X, flexA * math.sin(2 * math.pi * t)), quat(Z, sway * math.sin(2 * math.pi * t + 0.5))) for t in times]
        chestQ = [qmul(quat(X, -2 * flexA * math.sin(2 * math.pi * t)), quat(Z, -1.5 * sway * math.sin(2 * math.pi * t + 0.5))) for t in times]
        S = 2 * legLen["fl"] * math.sin(math.radians(A))   # (how far a foot travels back under the body while it is down)
        for (leg, fore, side), start in zip(LEGS, starts):
            if not s.get("hop"):
                # A foot on the ground stays where it was put: each leg is solved from there, so the body's bob and sway and the spine's flex all leave it standing. A leg at the end of
                # its stride cannot reach: the heel comes up, the foot turning about the toe, and the shoulder or the hip gives the rest, as they do.
                top, knee, ankle = P[leg + ".upper"], P[leg + ".lower"], P[leg + ".foot"]
                a, b = sub(knee, top), sub(ankle, knee)
                L1, L2 = math.hypot(a[1], a[2]), math.hypot(b[1], b[2])
                reach = 0.995 * (L1 + L2)
                bend = 1.0 if fore > 0 else -1.0   # (the fore knee forward, the hind hock back)
                contact = [ankle[0], 0.0, ankle[2] + (0.03 if fore > 0 else 0.02) * H]
                restFoot = sub(contact, ankle)
                F = math.hypot(restFoot[1], restFoot[2])
                parentRest = chest if fore > 0 else pelvis
                upperRel = sub(top, parentRest)
                lift = (0.22 if gait == "run" else 0.14 if gait == "trot" else 0.11) * H
                def parentAt(i):
                    if fore > 0:
                        return add(bob[i], qrot(pelvisQ[i], sub(chest, pelvis))), qmul(pelvisQ[i], chestQ[i])
                    return bob[i], pelvisQ[i]
                def stance(u):   # where the foot is on the ground, u of the way through its time down: the toe's place, the ankle's as it would be, and how loaded the leg is
                    z = contact[2] + S * (0.5 - u / duty)
                    load = math.sin(math.pi * u / duty) ** 2
                    return [contact[0], 0.0, z], [ankle[0], ankle[1] * (1.0 - 0.16 * load), z - restFoot[2]], load
                def solve(i, toe, wanted, down):
                    at, turn = parentAt(i)
                    U = add(at, qrot(turn, upperRel))
                    to = sub(wanted, U)
                    d = math.sqrt(dot(to, to))
                    slide = [0.0, 0.0, 0.0]
                    if d > reach:
                        ankleAt = add(U, mul(to, reach / d))
                        short = math.sqrt(dot(sub(ankleAt, toe), sub(ankleAt, toe))) - F if down else 0.0
                        give = min(max(0.0, short), 0.1 * H)   # (the shoulder's or the hip's share)
                        slide = mul(norm(to), give)
                        wanted = add(add(U, slide), mul(to, reach / d))
                        U = add(U, slide)
                    here = qrot(qconj(turn), sub(wanted, U))
                    d = max(abs(L1 - L2) + 1e-4, min(math.hypot(here[1], here[2]), L1 + L2 - 1e-4))
                    aim = theta(here)
                    open1 = math.acos(max(-1.0, min(1.0, (L1 * L1 + d * d - L2 * L2) / (2 * L1 * d))))
                    open2 = math.acos(max(-1.0, min(1.0, (L2 * L2 + d * d - L1 * L1) / (2 * L2 * d))))
                    upper, lower = aim - bend * open1, aim + bend * open2
                    pitch = 2.0 * math.atan2(turn[0], turn[3])   # (how far the parent is turned about x)
                    return upper - theta(a), lower - theta(b) - (upper - theta(a)), pitch, wanted, qrot(qconj(turn), slide)
                ups, los, fts, shifts = [], [], [], []
                for i, t in enumerate(times):
                    u = (t - start) % 1.0
                    if u < duty:
                        toe, wanted, load = stance(u)
                        phiU, phiL, pitch, ankleAt, slide = solve(i, toe, wanted, True)
                        v0 = sub(toe, ankleAt)
                        world = theta(v0) - theta(restFoot) - math.radians(16.0 if gait == "run" else 9.0) * load
                    else:
                        v = (u - duty) / (1 - duty)
                        ease = 0.5 - 0.5 * math.cos(math.pi * v)
                        z = contact[2] + S * (ease - 0.5)
                        wanted = [ankle[0], ankle[1] + lift * math.sin(math.pi * v) ** 1.3, z - restFoot[2]]
                        phiU, phiL, pitch, ankleAt, slide = solve(i, wanted, wanted, False)
                        world = math.radians(0.9 * (45.0 if gait == "run" else 35.0)) * math.sin(math.pi * v)   # (folded under as it comes through)
                    ups.append(quat(X, math.degrees(phiU)))
                    los.append(quat(X, math.degrees(phiL)))
                    fts.append(quat(X, math.degrees(world - (phiU + phiL) - pitch)))
                    shifts.append(add(sub(top, parentRest), slide))
                tracks += [(leg + ".upper", "rotation", times, ups), (leg + ".lower", "rotation", times, los), (leg + ".foot", "rotation", times, fts), (leg + ".upper", "translation", times, shifts)]
                # the blade or the haunch rides on it, under the skin: it slides with the leg, and stands out when the leg takes the weight
                soft1 = ("blade" if fore > 0 else "haunch") + (".l" if side > 0 else ".r")
                rest = sub(P[soft1], parentRest)
                travel = 0.075 * H if fore > 0 else 0.05 * H
                moved = []
                for i, t in enumerate(times):
                    u = (t - start) % 1.0
                    forward = 0.5 - u / duty if u < duty else 0.5 - math.cos(math.pi * (u - duty) / (1 - duty)) * 0.5 - 0.5
                    bulge = math.sin(math.pi * u / duty) ** 2 if u < duty else 0.0
                    moved.append(add(rest, [side * 0.012 * H * bulge, 0.02 * H * bulge - 0.012 * H * forward * forward, travel * 2.0 * forward]))
                tracks.append((soft1, "translation", times, moved))
                continue
            up, lo, ft = [], [], []
            for t in times:
                u = (t - start) % 1.0
                load = 0.0
                if u < duty:   # down: the foot travels back under the body at an even pace, the fetlock sinking as the weight comes onto it
                    phi = A * (1 - 2 * u / duty)
                    flex = 0.0
                    load = math.sin(math.pi * u / duty) ** 2
                else:          # up: swung forward, the lower leg folded to clear the ground
                    v = (u - duty) / (1 - duty)
                    phi = -A + 2 * A * (0.5 - 0.5 * math.cos(math.pi * v))
                    flex = math.sin(math.pi * v) * (45 if gait == "run" else 35)
                # (a rotation about +x turns a leg's foot back: the forward swing is a negative angle) the fore cannon folds back, the hind one forward
                up.append(quat(X, -phi + (0.25 * flex if fore < 0 else 0.0)))
                lo.append(quat(X, flex if fore > 0 else -1.4 * flex))
                ft.append(quat(X, (0.6 * flex if fore > 0 else 0.7 * flex) - (20.0 if gait == "run" else 11.0) * load))
            tracks += [(leg + ".upper", "rotation", times, up), (leg + ".lower", "rotation", times, lo), (leg + ".foot", "rotation", times, ft)]
        if name in PANTS and gait != "walk":   # (hot work: the mouth open, the tongue out a little, breathing with the stride)
            gape = 14.0 if gait == "run" else 9.0
            tracks.append(("jaw", "rotation", times, [quat(X, gape + 3.0 * math.sin(2 * math.pi * t)) for t in times]))
            tongueRest = sub(P["tongue"], P["jaw"])
            tracks.append(("tongue", "translation", times, [add(tongueRest, mul(hd, 0.1 * s["head"])) for t in times]))
        tracks.append(("pelvis", "translation", times, bob))
        tracks.append(("pelvis", "rotation", times, pelvisQ))
        tracks.append(("chest", "rotation", times, chestQ))
        # the belly swings as the body rises and falls, a moment late, and from side to side with the hips; the more the heavier it is
        swing = {"run": 0.03, "trot": 0.022}.get(gait, 0.012) * H * (1.5 if s.get("pillar") or s.get("roundEars") else 1.0)
        bellyRest = sub(P["belly"], pelvis)
        tracks.append(("belly", "translation", times, [add(bellyRest, [0.5 * swing * math.sin(2 * math.pi * (t - 0.14)), swing * math.cos(2 * math.pi * beats * (t - 0.09)), 0.0]) for t in times]))
        nod = 9 if gait == "run" else 4 if gait == "walk" else 2
        tracks.append(("neck", "rotation", times, [quat(X, nod * math.sin(2 * math.pi * beats * t + 0.6)) for t in times]))
        # an animal holds its head steadier than its neck: its eyes stay on what is ahead
        tracks.append(("head", "rotation", times, [quat(X, -0.7 * nod * math.sin(2 * math.pi * beats * t + 0.6)) for t in times]))
        # the tail: swinging with the hips at a walk, streaming out and rippling at a gallop
        tracks.append(("tail", "rotation", times, [qmul(quat(X, -(24 + 9 * math.sin(4 * math.pi * t)) if gait == "run" else -6), quat(Y, (4 if gait == "run" else 9) * math.sin(2 * math.pi * t + 1.0))) for t in times]))
        clips.append((gait, tracks))
    # graze: the neck down so the muzzle is in the grass, nibbling, the head lifting a little
    gt = [i * 0.25 for i in range(17)]
    down = -(s["neckUp"] + 40)
    ft = [i * 0.05 for i in range(101)]   # (a skin's twitch is quicker than the rest of the body: a shiver over the flank, a fly shaken off)
    def twitch(at, side, joint):
        rest = sub(P[joint], pelvis)
        return (joint, "translation", ft, [add(rest, [side * 0.009 * H * math.exp(-((t - at) / 0.09) ** 2) * math.sin(2 * math.pi * 7.0 * (t - at)), 0.0, 0.0]) for t in ft])
    def earTrack(joint, shift, length):   # (an ear turned to a sound and back, now and then)
        et_ = [i * 0.1 for i in range(int(length * 10) + 1)]
        def turn(t):
            u = ((t + shift) % length) / length
            return 34.0 * math.exp(-((u - 0.3) / 0.05) ** 2) * (1.0 if 0 <= u else 0.0)
        return (joint, "rotation", et_, [quat(Y, turn(t) * (1 if joint == "ear.l" else -1)) for t in et_])
    def blink(joint, side, moments):   # (the upper lid down over the eye and up, a sixth of a second)
        return (joint, "rotation", ft, [quat(Z, side * 74.0 * max(math.exp(-((t - at) / 0.07) ** 2) for at in moments)) for t in ft])
    chew = ("jaw", "rotation", ft, [qmul(quat(X, 2.2 + 2.2 * math.sin(2 * math.pi * 1.9 * t)), quat(Y, 2.0 * math.sin(2 * math.pi * 0.95 * t))) for t in ft])
    clips.append(("graze", [
        chew, blink("blink.l", -1, (1.1, 3.3)), blink("blink.r", 1, (1.1, 3.3)),
        twitch(1.4, 1, "flank.l"), twitch(3.1, -1, "flank.r"), earTrack("ear.l", 0.0, 4.0), earTrack("ear.r", 1.7, 4.0),
        ("neck", "rotation", gt, [quat(X, -down - 6 * max(0.0, math.sin(2 * math.pi * t / 4.0)) ** 4) for t in gt]),
        ("head", "rotation", gt, [quat(X, 25 + 6 * math.sin(2 * math.pi * t * 1.5)) for t in gt]),
        ("tail", "rotation", gt, [quat(Y, 12 * math.sin(2 * math.pi * t / 2.0) ** 9) for t in gt]),
        ("chest", "rotation", gt, [quat(X, 4 + 0.8 * math.sin(2 * math.pi * t / 2.0)) for t in gt]),   # (breathing)
        ("fl.upper", "rotation", gt, [quat(X, -8) for t in gt]), ("fr.upper", "rotation", gt, [quat(X, -8) for t in gt]),
    ]))
    # idle: standing, looking round (the head turning), the tail flicking
    it = [i * 0.25 for i in range(21)]
    panting = [("jaw", "rotation", ft, [quat(X, 7.0 + 2.5 * math.sin(2 * math.pi * 2.6 * t)) for t in ft])] if name in PANTS else []
    clips.append(("idle", panting + [
        blink("blink.l", -1, (0.7, 2.6, 4.4)), blink("blink.r", 1, (0.7, 2.6, 4.4)),
        twitch(0.9, -1, "flank.r"), twitch(3.6, 1, "flank.l"), earTrack("ear.l", 0.8, 5.0), earTrack("ear.r", 3.0, 5.0),
        ("neck", "rotation", it, [qmul(quat(Y, 20 * math.sin(2 * math.pi * t / 5.0)), quat(X, -3 * math.sin(2 * math.pi * t / 2.5))) for t in it]),
        ("head", "rotation", it, [quat(Y, 15 * math.sin(2 * math.pi * t / 5.0 + 0.8)) for t in it]),
        ("tail", "rotation", it, [quat(Y, 15 * math.sin(2 * math.pi * t / 1.25) ** 7) for t in it]),
        ("chest", "rotation", it, [quat(X, 1.0 * math.sin(2 * math.pi * t / 2.5)) for t in it]),       # (breathing)
        ("pelvis", "rotation", it, [quat(Z, 1.6 * math.sin(2 * math.pi * t / 5.0)) for t in it]),      # (easing its weight from one hind leg to the other)
    ]))
    # what an animal does with a moment to itself (each five seconds, what it does at the start and then standing, so a pause of any length can take one)
    breathing = ("chest", "rotation", it, [quat(X, 1.0 * math.sin(2 * math.pi * t / 2.5)) for t in it])
    fast = [i * 0.04 for i in range(126)]
    def burst(t, length): return math.sin(math.pi * min(1.0, t / length)) ** 2
    # a shake, from the shoulders back, the skin going with it
    clips.append(("shake", [
        ("chest", "rotation", fast, [quat(Z, 9.0 * burst(t, 1.3) * math.sin(2 * math.pi * 6.0 * t)) for t in fast]),
        ("pelvis", "rotation", fast, [quat(Z, -5.0 * burst(t - 0.15, 1.3) * math.sin(2 * math.pi * 6.0 * t) if t > 0.15 else 0.0) for t in fast]),
        ("neck", "rotation", fast, [quat(Y, 14.0 * burst(t, 1.3) * math.sin(2 * math.pi * 3.0 * t)) for t in fast]),
        ("tail", "rotation", fast, [quat(Y, 26.0 * burst(t, 1.5) * math.sin(2 * math.pi * 6.0 * t + 1.0)) for t in fast]),
        blink("blink.l", -1, (0.5,)), blink("blink.r", 1, (0.5,)), earTrack("ear.l", 2.3, 5.0), earTrack("ear.r", 2.3, 5.0),
    ]))
    # a fore foot stamped at a fly, twice
    def stamp(t):
        return max(math.exp(-((t - 0.8) / 0.16) ** 2), 0.8 * math.exp(-((t - 1.6) / 0.15) ** 2))
    clips.append(("stamp", [
        ("fr.upper", "rotation", fast, [quat(X, -22.0 * stamp(t)) for t in fast]), ("fr.lower", "rotation", fast, [quat(X, 62.0 * stamp(t)) for t in fast]),
        ("fr.foot", "rotation", fast, [quat(X, -30.0 * stamp(t)) for t in fast]), twitch(0.7, -1, "flank.r"), twitch(1.5, 1, "flank.l"),
        ("tail", "rotation", it, [quat(Y, 26 * math.sin(2 * math.pi * t / 1.25) ** 7) for t in it]), breathing,
    ]))
    # round to its own flank at a fly, and back
    def round_(t): return math.exp(-((t - 1.6) / 0.9) ** 4)
    clips.append(("nuzzle", [
        ("neck", "rotation", it, [qmul(quat(Y, 62.0 * round_(t)), quat(X, 16.0 * round_(t))) for t in it]), ("head", "rotation", it, [quat(Y, 42.0 * round_(t)) for t in it]),
        ("chest", "rotation", it, [quat(Y, 10.0 * round_(t)) for t in it]), twitch(1.5, 1, "flank.l"), earTrack("ear.l", 1.0, 5.0), earTrack("ear.r", 1.0, 5.0),
    ]))
    # a drink, the muzzle low and level, the throat working
    clips.append(("drink", [
        ("neck", "rotation", gt, [quat(X, s["neckUp"] + 52 - 2.0 * max(0.0, math.sin(2 * math.pi * t / 3.0)) ** 4) for t in gt]), ("head", "rotation", gt, [quat(X, -8) for t in gt]),
        ("chest", "rotation", gt, [quat(X, 6 + 0.6 * math.sin(2 * math.pi * t / 2.0)) for t in gt]), ("fl.upper", "rotation", gt, [quat(X, -11) for t in gt]),
        ("fr.upper", "rotation", gt, [quat(X, -11) for t in gt]), ("jaw", "rotation", ft, [quat(X, 2.0 + 2.0 * math.sin(2 * math.pi * 2.6 * t)) for t in ft]),
    ]))
    # a young one, its head up under its mother, its tail going
    clips.append(("suckle", [
        ("neck", "rotation", gt, [quat(X, s["neckUp"] * 0.75 + 0.6 * math.sin(2 * math.pi * t / 0.9)) for t in gt]), ("head", "rotation", gt, [quat(X, -28.0) for t in gt]),
        ("tail", "rotation", fast, [quat(Y, 18.0 * math.sin(2 * math.pi * 3.0 * t)) for t in fast]), ("jaw", "rotation", ft, [quat(X, 3.0 + 2.5 * math.sin(2 * math.pi * 2.2 * t)) for t in ft]),
    ]))
    # down to rest, over six seconds, and stays: the forelegs folding, the hind end after it, the head up (a clip not to be looped)
    lt = [i * 0.1 for i in range(61)]
    def ease(a, b, t): return (lambda u: u * u * (3.0 - 2.0 * u))(max(0.0, min(1.0, (t - a) / (b - a))))
    drop = hipY - 1.02 * ry0 - 0.03 * H
    def fold(leg, fore, a, b):
        k = 1.0 if fore else -1.0
        return [(leg + ".upper", "rotation", lt, [quat(X, -k * 62.0 * ease(a, b, t)) for t in lt]), (leg + ".lower", "rotation", lt, [quat(X, k * 128.0 * ease(a, b, t)) for t in lt]),
                (leg + ".foot", "rotation", lt, [quat(X, -k * 52.0 * ease(a, b, t)) for t in lt])]
    if not s.get("pillar"):
        clips.append(("lie", fold("fl", True, 0.3, 2.0) + fold("fr", True, 0.5, 2.2) + fold("hl", False, 1.6, 3.4) + fold("hr", False, 1.7, 3.5) + [
            ("pelvis", "translation", lt, [add(pelvis, [0, -drop * ease(1.5, 3.6, t), 0]) for t in lt]),
            ("pelvis", "rotation", lt, [quat(X, -13.0 * (ease(0.4, 2.0, t) - ease(1.8, 3.5, t))) for t in lt]),
            ("neck", "rotation", lt, [quat(X, 14.0 * ease(0.4, 2.0, t) - 18.0 * ease(3.0, 4.2, t)) for t in lt]),
        ]))
    strides = {g: 2 * legLen["fl"] * math.sin(math.radians(swings[g])) / gaits[g][1] for g in gaits}
    return joints, meshes, materials, clips, strides


# ---- a penguin: upright, waddling on short legs, its flippers held out ---------------------------------------------------------------------------------------------------
def penguin():
    H = 0.75
    joints = [("body", -1, [0, 0.12, 0]), ("head", 0, [0, 0.62, 0.02]), ("flipper.L", 0, [0.11, 0.48, 0]), ("flipper.R", 0, [-0.11, 0.48, 0]),
              ("leg.L", 0, [0.06, 0.12, 0.02]), ("leg.R", 0, [-0.06, 0.12, 0.02])]
    J = {n: i for i, (n, _, _) in enumerate(joints)}
    back, front, beak, feet, eyes = Mesh(), Mesh(), Mesh(), Mesh(), Mesh()
    centres = [[0, 0.06 + 0.56 * u / 8.0, -0.01] for u in range(9)]
    radii = [(0.14 * math.sin(math.pi * (0.08 + 0.84 * u / 8.0)) ** 0.7, 0.13 * math.sin(math.pi * (0.08 + 0.84 * u / 8.0)) ** 0.7) for u in range(9)]
    tube(back, centres, radii, [[(J["body"], 1.0 - max(0.0, u / 8.0 - 0.7) / 0.3), (J["head"], max(0.0, u / 8.0 - 0.7) / 0.3)] for u in range(9)], sides=16, up=(0, 0, 1))
    fc = [[0, 0.1 + 0.42 * u / 6.0, 0.03] for u in range(7)]
    tube(front, fc, [(0.11 * math.sin(math.pi * (0.1 + 0.8 * u / 6.0)) ** 0.7, 0.1 * math.sin(math.pi * (0.1 + 0.8 * u / 6.0)) ** 0.7) for u in range(7)], [[(J["body"], 1.0)]] * 7, sides=14, up=(0, 0, 1))
    hp = joints[J["head"]][2]
    tube(beak, [add(hp, [0, 0.0, 0.07]), add(hp, [0, -0.02, 0.15])], [(0.018, 0.014), (0.003, 0.003)], [[(J["head"], 1.0)]] * 2, sides=8)
    for side in (1, -1):
        e = add(hp, [side * 0.05, 0.02, 0.06])
        tube(eyes, [add(e, [-side * 0.004, 0, 0]), add(e, [side * 0.004, 0, 0])], [(0.008, 0.008)] * 2, [[(J["head"], 1.0)]] * 2, sides=6)
        f0 = joints[J["flipper." + ("L" if side > 0 else "R")]][2]
        panel(back, [f0, add(f0, [side * 0.05, -0.04, 0.02]), add(f0, [side * 0.07, -0.3, -0.01]), add(f0, [side * 0.02, -0.24, -0.03])], 0.016,
              [[(J["flipper." + ("L" if side > 0 else "R")], 1.0)]] * 4, [side, 0, 0])
        l0 = joints[J["leg." + ("L" if side > 0 else "R")]][2]
        tube(feet, [l0, add(l0, [0, -0.1, 0.02]), add(l0, [0, -0.115, 0.09])], [(0.02, 0.02), (0.018, 0.012), (0.03, 0.006)], [[(J["leg." + ("L" if side > 0 else "R")], 1.0)]] * 3, sides=8)
    materials = [material("coat", (0.04, 0.04, 0.05), 0.5), material("belly", (0.93, 0.92, 0.9), 0.6), material("beak", (0.9, 0.5, 0.1), 0.4), material("feet", (0.85, 0.45, 0.15), 0.6),
                 material("eyes", (0.02, 0.015, 0.01), 0.15)]
    n = 16
    times = [i / n for i in range(n + 1)]
    walk = [  # the waddle: rolled side to side onto each foot in turn, the feet stepping, the flippers out for balance
        ("body", "rotation", times, [qmul(quat(Z, 11 * math.sin(2 * math.pi * t)), quat(Y, 5 * math.sin(2 * math.pi * t))) for t in times]),
        ("body", "translation", times, [[0.02 * math.sin(2 * math.pi * t), 0.12 + 0.012 * abs(math.cos(2 * math.pi * t)), 0] for t in times]),
        ("leg.L", "rotation", times, [quat(X, -22 * math.sin(2 * math.pi * t)) for t in times]),
        ("leg.R", "rotation", times, [quat(X, 22 * math.sin(2 * math.pi * t)) for t in times]),
        ("flipper.L", "rotation", times, [quat(Z, 25 + 6 * math.sin(2 * math.pi * t)) for t in times]),
        ("flipper.R", "rotation", times, [quat(Z, -25 + 6 * math.sin(2 * math.pi * t)) for t in times]),
    ]
    it = [i * 0.25 for i in range(21)]
    idle = [("head", "rotation", it, [quat(Y, 30 * math.sin(2 * math.pi * t / 5.0)) for t in it]),
            ("flipper.L", "rotation", it, [quat(Z, 10 + 25 * max(0.0, math.sin(2 * math.pi * t / 2.5)) ** 6) for t in it]),
            ("flipper.R", "rotation", it, [quat(Z, -10 - 25 * max(0.0, math.sin(2 * math.pi * t / 2.5)) ** 6) for t in it])]
    clips = [("walk", walk), ("trot", walk), ("run", walk), ("idle", idle), ("graze", idle)]
    return joints, [back, front, beak, feet, eyes], materials, clips, {"walk": 0.24, "trot": 0.24, "run": 0.24}


# ---- a snake: a chain of segments that slithers, a wave running back along it --------------------------------------------------------------------------------------
def snake():
    L, N = 1.6, 14
    seg = L / N
    joints = [("seg0", -1, [0, 0.03, L / 2])]
    for i in range(1, N + 1):
        joints.append(("seg%d" % i, i - 1, [0, 0.03, L / 2 - seg * i]))
    J = {n: i for i, (n, _, _) in enumerate(joints)}
    body, belly, eyes = Mesh(), Mesh(), Mesh()
    centres, radii, skins = [], [], []
    for k in range(4 * N + 1):
        u = k / (4.0 * N)
        z = L / 2 - L * u
        r = 0.035 * (min(1.0, u / 0.08) ** 0.5) * (1.0 - 0.85 * max(0.0, (u - 0.55) / 0.45))
        centres.append([0, 0.03, z + 0.04 * (1 - min(1.0, u / 0.04))])
        radii.append((max(0.004, r * 1.1), max(0.004, r * 0.85)))
        f = u * N
        i0 = min(N, int(f))
        w = f - i0
        skins.append([(J["seg%d" % i0], 1.0 - w), (J["seg%d" % min(N, i0 + 1)], w)])
    tube(body, centres, radii, skins, sides=12, rect=(0.0, 0.0, 0.5, 0.5))
    for side in (1, -1):
        e = [side * 0.026, 0.045, L / 2 - 0.02]
        tube(eyes, [add(e, [-side * 0.003, 0, 0]), add(e, [side * 0.003, 0, 0])], [(0.006, 0.006)] * 2, [[(J["seg0"], 1.0)]] * 2, sides=6)
    materials = [material("coat", (0.30, 0.34, 0.14), 0.45), material("legs", (0.82, 0.78, 0.55)), material("eyes", (0.02, 0.015, 0.01), 0.15)]
    n = 16
    times = [i / n for i in range(n + 1)]
    A, waves = 32.0, 1.4
    absolute = lambda i, t: A * math.sin(2 * math.pi * (t + waves * i / N)) * min(1.0, 0.4 + i / N)
    slither = []
    for i in range(N + 1):   # each segment's own turn: the difference of the wave's heading from its neighbour's (a chain's joints turn relative to their parents)
        slither.append(("seg%d" % i, "rotation", times, [quat(Y, absolute(i, t) - (absolute(i - 1, t) if i > 0 else 0.0)) for t in times]))
    it = [i * 0.25 for i in range(21)]
    rest = [("seg%d" % i, "rotation", it, [quat(Y, 8 * math.sin(2 * math.pi * (t / 5.0 + i / N)) - (8 * math.sin(2 * math.pi * (t / 5.0 + (i - 1) / N)) if i > 0 else 0.0)) for t in it])
            for i in range(N + 1)]
    rest.append(("seg0", "translation", it, [[0, 0.03 + 0.04 * max(0.0, math.sin(2 * math.pi * t / 5.0)) ** 2, L / 2] for t in it]))   # (lifting its head to look)
    clips = [("walk", slither), ("trot", slither), ("run", slither), ("idle", rest), ("graze", rest)]
    wave = L / waves
    return joints, [body, belly, eyes], materials, clips, {"walk": 0.7 * wave, "trot": 0.7 * wave, "run": 0.7 * wave}


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
    # (round 13) perched: the wings folded down and back along the body, the head looking round; the flare of a landing: the wings high and swept forward, beating back
    ptimes = [i * 0.5 for i in range(11)]
    perch = []
    for tag, side in (("L", 1), ("R", -1)):
        perch.append(("wing." + tag, "rotation", ptimes, [qmul(quat(Y, side * 85), quat(Z, -side * 12)) for t in ptimes]))   # (the arm back along the body)
        perch.append(("fore." + tag, "rotation", ptimes, [quat(Y, -side * 165) for t in ptimes]))   # (the forearm forward over it)
        perch.append(("hand." + tag, "rotation", ptimes, [quat(Y, side * 165) for t in ptimes]))    # (the hand back again: a folded wing's Z)
    perch.append(("head", "rotation", ptimes, [quat(Y, 35 * math.sin(2 * math.pi * t / 5.0)) for t in ptimes]))
    perch.append(("tail", "rotation", ptimes, [quat(X, -20) for t in ptimes]))
    ftimes = [i / 16.0 for i in range(17)]
    flare = []
    for tag, side in (("L", 1), ("R", -1)):
        flare.append(("wing." + tag, "rotation", ftimes, [qmul(quat(Z, side * (55 + 25 * math.cos(2 * math.pi * t))), quat(Y, side * 20)) for t in ftimes]))
        flare.append(("hand." + tag, "rotation", ftimes, [quat(Y, side * 15 * math.sin(2 * math.pi * t)) for t in ftimes]))
    flare.append(("tail", "rotation", ftimes, [quat(X, 25) for t in ftimes]))
    return joints, meshes, materials, [("flap", flap), ("glide", glide), ("perch", perch), ("flare", flare)]


def main():
    os.makedirs(OUT, exist_ok=True)
    for name, s in SPECIES.items():
        joints, meshes, materials, clips, strides = quadruped(name, s)
        write(os.path.join(OUT, name + ".glb"), joints, meshes, materials, clips, Builder())
        print("   strides (m a cycle): " + ", ".join("%s %.3f" % (g, v) for g, v in strides.items()))
    for name, make in (("penguin", penguin), ("snake", snake)):
        joints, meshes, materials, clips, strides = make()
        write(os.path.join(OUT, name + ".glb"), joints, meshes, materials, clips, Builder())
        print("   strides (m a cycle): " + ", ".join("%s %.3f" % (g, v) for g, v in strides.items()))
    for name, s in BIRDS.items():
        joints, meshes, materials, clips = bird(name, s)
        write(os.path.join(OUT, name + ".glb"), joints, meshes, materials, clips, Builder())


if __name__ == "__main__":
    main()
