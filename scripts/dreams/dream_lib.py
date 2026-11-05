# dream_lib.py: what the two dream showcase films' generators share (glass_garden.py, sky_islands.py) - noise and pictures, binary PLY meshes, surfaces of revolution and
# swept tubes, and a score sheet (a film's cues.txt). Each generator writes its set (a pbrt-v4 scene and its meshes and pictures) into pbrt_scenes/film_assets/<key>/, its
# film file into pbrt_scenes/<key>.film and its music into pbrt_scenes/film_assets/<key>/cues.txt. Metres, +y up, pbrt's own coordinates.
import math, os
import numpy as np
from PIL import Image

REPO = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))


class Set:
    """The folder a set is written into: its pictures (PNG) and meshes (meshes/mNNN.ply), and the lines of its scene."""

    def __init__(self, key):
        self.key = key
        self.out = os.path.join(REPO, "pbrt_scenes", "film_assets", key)
        os.makedirs(os.path.join(self.out, "meshes"), exist_ok=True)
        for f in os.listdir(os.path.join(self.out, "meshes")):   # (a mesh no longer made must not linger)
            if f.endswith(".ply"):
                os.remove(os.path.join(self.out, "meshes", f))
        self.meshes = 0
        self.lines = []

    def save(self, name, rgb):
        Image.fromarray((np.clip(rgb, 0, 1) * 255 + 0.5).astype(np.uint8)).save(os.path.join(self.out, name))
        return name

    def mesh(self, P, idx, N=None, UV=None):
        """A mesh as a binary PLY file beside the scene and the Shape line that reads it (triangles with no area left out: the loader would warn of each)."""
        P = np.asarray(P, dtype=np.float32)
        T = np.asarray(idx, dtype=np.int64).reshape(-1, 3)
        area = np.linalg.norm(np.cross(P[T[:, 1]] - P[T[:, 0]], P[T[:, 2]] - P[T[:, 0]]), axis=1)
        T = T[area > 1e-10]
        used, T = np.unique(T, return_inverse=True)   # (only the points its triangles use)
        T = T.reshape(-1, 3).astype(np.int32)
        P = P[used]
        cols, props = [P], ["x", "y", "z"]
        if N is not None:
            cols.append(np.asarray(N, dtype=np.float32)[used])
            props += ["nx", "ny", "nz"]
        if UV is not None:
            cols.append(np.asarray(UV, dtype=np.float32)[used])
            props += ["u", "v"]
        verts = np.hstack(cols).astype("<f4")
        name = "meshes/m%03d.ply" % self.meshes
        self.meshes += 1
        head = "ply\nformat binary_little_endian 1.0\nelement vertex %d\n" % len(verts) + "".join("property float %s\n" % p for p in props)
        head += "element face %d\nproperty list uchar int vertex_indices\nend_header\n" % len(T)
        faces = np.zeros(len(T), dtype=[("n", "u1"), ("i", "<i4", 3)])
        faces["n"], faces["i"] = 3, T
        with open(os.path.join(self.out, name), "wb") as f:
            f.write(head.encode("ascii"))
            f.write(verts.tobytes())
            f.write(faces.tobytes())
        return 'Shape "plymesh" "string filename" [ "%s" ]' % name

    def write(self, name, header):
        path = os.path.join(self.out, name)
        with open(path, "w", newline="\n") as f:
            f.write("\n".join(header + self.lines) + "\n")
        return path


# ---- noise -----------------------------------------------------------------------------------------------------------------------------------------------------
def value_noise(n, cells, seed, m=None):
    """An n x m (m = n) picture of smooth value noise with `cells` cells across, tiling."""
    m = m or n
    g = np.random.default_rng(seed).random((cells + 1, cells + 1))
    g[-1, :] = g[0, :]
    g[:, -1] = g[:, 0]
    img = Image.fromarray((g * 255).astype(np.uint8)).resize((n, m), Image.BICUBIC)
    return np.asarray(img, dtype=np.float64) / 255.0


def fbm(n, seed, octaves=(4, 8, 16, 32, 64, 128), m=None, gain=0.55):
    out, amp, tot = np.zeros((m or n, n)), 1.0, 0.0
    for i, c in enumerate(octaves):
        out += amp * value_noise(n, c, seed + i, m)
        tot += amp
        amp *= gain
    return out / tot


def hash01(*k):
    h = 2166136261
    for v in k:
        for b in int(v * 1000003 + 7).to_bytes(8, "little", signed=True):
            h = ((h ^ b) * 16777619) & 0xFFFFFFFF
    return h / 4294967296.0


def noise3(p, seed=0):
    """Smooth 3D value noise in [0, 1) at a point (lattice hash, smoothstep blend)."""
    x, y, z = p
    xi, yi, zi = math.floor(x), math.floor(y), math.floor(z)
    fx, fy, fz = x - xi, y - yi, z - zi
    sx, sy, sz = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy), fz * fz * (3 - 2 * fz)
    def c(i, j, k):
        return hash01(xi + i, yi + j, zi + k, seed)
    x00 = c(0, 0, 0) + (c(1, 0, 0) - c(0, 0, 0)) * sx
    x10 = c(0, 1, 0) + (c(1, 1, 0) - c(0, 1, 0)) * sx
    x01 = c(0, 0, 1) + (c(1, 0, 1) - c(0, 0, 1)) * sx
    x11 = c(0, 1, 1) + (c(1, 1, 1) - c(0, 1, 1)) * sx
    y0 = x00 + (x10 - x00) * sy
    y1 = x01 + (x11 - x01) * sy
    return y0 + (y1 - y0) * sz


def fbm3(p, seed=0, octaves=4):
    tot, amp, s = 0.0, 1.0, 0.0
    for o in range(octaves):
        tot += amp * noise3((p[0] * 2 ** o, p[1] * 2 ** o, p[2] * 2 ** o), seed + o)
        s += amp
        amp *= 0.5
    return tot / s


# ---- meshes ----------------------------------------------------------------------------------------------------------------------------------------------------
def grid(nu, nv, f, wrap_u=False):
    """A (nu+1) x (nv+1) grid of points f(u, v) -> (p, uv) for u, v in 0..1, its triangles and smooth normals (`wrap_u`: the first and last columns meet, their
    normals shared)."""
    P, UV = [], []
    for j in range(nv + 1):
        for i in range(nu + 1):
            p, uv = f(i / nu, j / nv)
            P.append(p)
            UV.append(uv)
    P = np.array(P, dtype=np.float64)
    idx = []
    for j in range(nv):
        for i in range(nu):
            a = j * (nu + 1) + i
            b, c, d = a + 1, a + nu + 1, a + nu + 2
            idx += [a, b, d, a, d, c]
    T = np.array(idx).reshape(-1, 3)
    N = np.zeros_like(P)
    fn = np.cross(P[T[:, 1]] - P[T[:, 0]], P[T[:, 2]] - P[T[:, 0]])
    for k in range(3):
        np.add.at(N, T[:, k], fn)
    if wrap_u:
        for j in range(nv + 1):
            a, b = j * (nu + 1), j * (nu + 1) + nu
            s = N[a] + N[b]
            N[a] = N[b] = s
    N /= np.maximum(1e-12, np.linalg.norm(N, axis=1))[:, None]
    return P, idx, N, np.array(UV)


def merge(*meshes):
    """Several (P, idx, N, UV) meshes as one."""
    P, idx, N, UV, base = [], [], [], [], 0
    for m in meshes:
        P.append(m[0])
        idx += [i + base for i in m[1]]
        N.append(m[2])
        UV.append(m[3])
        base += len(m[0])
    return np.vstack(P), idx, np.vstack(N), np.vstack(UV)


def lathe(profile, segs, at=(0, 0, 0), uscale=1.0):
    """A surface of revolution about +y: profile [(radius, y)] from its first point to its last, u round it, v along the profile."""
    lens = [0.0]
    for a, b in zip(profile, profile[1:]):
        lens.append(lens[-1] + math.hypot(b[0] - a[0], b[1] - a[1]))
    total = lens[-1] or 1.0
    n = len(profile) - 1

    def f(u, v):
        k = min(n - 1, int(v * n))
        t = v * n - k
        r = profile[k][0] + (profile[k + 1][0] - profile[k][0]) * t
        y = profile[k][1] + (profile[k + 1][1] - profile[k][1]) * t
        a = 2 * math.pi * u
        s = lens[k] + (lens[k + 1] - lens[k]) * t
        return (at[0] + r * math.cos(a), at[1] + y, at[2] + r * math.sin(a)), (u * uscale, s / total)

    return grid(segs, n, f, wrap_u=True)


def frames_along(pts):
    """Rotation-minimising frames (tangent, normal, binormal) along a polyline."""
    pts = np.asarray(pts, dtype=np.float64)
    T = np.gradient(pts, axis=0)
    T /= np.maximum(1e-12, np.linalg.norm(T, axis=1))[:, None]
    up = np.array([0.0, 0.0, 1.0]) if abs(T[0][1]) > 0.9 else np.array([0.0, 1.0, 0.0])
    Nn = [np.cross(up, T[0])]
    Nn[0] /= np.linalg.norm(Nn[0])
    for i in range(1, len(pts)):
        v = Nn[-1] - np.dot(Nn[-1], T[i]) * T[i]
        Nn.append(v / max(1e-12, np.linalg.norm(v)))
    Nn = np.array(Nn)
    return T, Nn, np.cross(T, Nn)


def tube(pts, radii, sides=16, caps=True):
    """A tube along a polyline with a radius at each point (closed at both ends when `caps`: the radii should then fall to 0 there)."""
    pts = np.asarray(pts, dtype=np.float64)
    T, Nn, B = frames_along(pts)
    n = len(pts) - 1
    lens = np.concatenate([[0.0], np.cumsum(np.linalg.norm(np.diff(pts, axis=0), axis=1))])

    def f(u, v):
        k = min(n, int(round(v * n)))
        a = 2 * math.pi * u
        p = pts[k] + radii[k] * (math.cos(a) * Nn[k] + math.sin(a) * B[k])
        return tuple(p), (u, lens[k] / max(1e-9, lens[-1]))

    return grid(sides, n, f, wrap_u=True)


def bezier(p0, p1, p2, p3, n):
    t = np.linspace(0, 1, n)[:, None]
    p0, p1, p2, p3 = (np.asarray(p, dtype=np.float64) for p in (p0, p1, p2, p3))
    return (1 - t) ** 3 * p0 + 3 * (1 - t) ** 2 * t * p1 + 3 * (1 - t) * t ** 2 * p2 + t ** 3 * p3


def transform(m, M=None, at=(0, 0, 0), yaw=0.0, tilt=0.0, scale=1.0):
    """A mesh scaled, tilted about x (degrees), turned about y (degrees) and moved to `at`."""
    P, idx, N, UV = m
    ct, st = math.cos(math.radians(tilt)), math.sin(math.radians(tilt))
    cy, sy = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
    Rx = np.array([[1, 0, 0], [0, ct, -st], [0, st, ct]])
    Ry = np.array([[cy, 0, sy], [0, 1, 0], [-sy, 0, cy]])
    R = Ry @ Rx
    return (np.asarray(P) * scale) @ R.T + np.asarray(at), idx, np.asarray(N) @ R.T, UV


# ---- materials -------------------------------------------------------------------------------------------------------------------------------------------------
def glass_medium(name, colour, density):
    """A coloured glass's inside: an absorbing medium whose light lost per metre is greatest in the colours the glass is not (`colour` its tint, 0..1; `density`
    how deep the colour gets per metre)."""
    a = [max(0.0, -math.log(max(1e-3, c))) * density for c in colour]
    return 'MakeNamedMedium "%s" "string type" [ "homogeneous" ] "rgb sigma_a" [ %.4f %.4f %.4f ] "rgb sigma_s" [ 0 0 0 ] "float scale" [ 1 ]' % (name, *a)


# ---- the score -------------------------------------------------------------------------------------------------------------------------------------------------
NOTE = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}


def midi(name):
    """A note name (C4, F#3, Bb2) as its MIDI number; a number is kept."""
    if isinstance(name, (int, float)):
        return int(name)
    n = NOTE[name[0]]
    k = 1
    while k < len(name) and name[k] in "#b":
        n += 1 if name[k] == "#" else -1
        k += 1
    return n + 12 * (int(name[k:]) + 1)


class Sheet:
    """A film's cues file: every cue at a film time (@)."""

    def __init__(self, title):
        self.lines = ["# %s: a composed score (written by its generator in scripts/dreams; change it as any cues file). Every time is the film's (@)." % title]

    def section(self, name):
        self.lines += ["", "# --- " + name]

    def cue(self, t, inst, notes=(), length=None, gain=0.05, pan=0.0, send=0.6, spread=None):
        w = ["@%.2f" % t, inst] + [str(midi(n)) for n in notes]
        if length is not None:
            w += ["length", "%.2f" % length]
        w += ["gain", "%.3f" % gain]
        if pan:
            w += ["pan", "%.2f" % pan]
        w += ["send", "%.2f" % send]
        if spread is not None:
            w += ["spread", "%.2f" % spread]
        self.lines.append("  ".join(w[:2]) + "  " + " ".join(w[2:]))

    def write(self, key):
        path = os.path.join(REPO, "pbrt_scenes", "film_assets", key, "cues.txt")
        with open(path, "w", newline="\n") as f:
            f.write("\n".join(self.lines) + "\n")
        return path
