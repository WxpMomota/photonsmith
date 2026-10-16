"""room_fit.py - fits a box-shaped room to a depth map and a photo (numpy only, no model, no GPU).

The photo_to_room.py helper runs a depth model on a photo of a room and hands the result here. This file does the geometry:

  1. every pixel becomes a 3D point (camera looking down +Z, X to the right, Y up) from its depth and the photo's field of view;
  2. the floor's normal gives "up", so a tilted or rolled camera is levelled;
  3. the walls' normals give the room's turn about the vertical (rooms are boxes: every wall is along one of two perpendicular directions);
  4. the floor, ceiling and the walls the photo shows are read off histograms of the points;
  5. each surface gets the colour the photo shows there; the very bright patches (windows, lamps) become emissive panels;
  6. the camera is placed where the photo was taken from, looking the way it looked, so the first render of the room is the photo's view.

Everything is in metres (as far as the depth model's scale is right), Y up, the floor at y = 0, in the room's own frame: the walls are axis-aligned boxes. The Scene Builder's world is
right-handed (looking along +z, +x is on the LEFT of the picture), while the fit works in the camera's own frame (+x to the right); the answer is mirrored in x at the very end so
the photo's left wall is at +x in the Builder. The answer is a plain
dict (see fit_room) that photo_to_room.py writes as room.json and the Scene Builder turns into objects. Walls the photo does not show are assumed (and listed in "notes").
"""
import math

import numpy as np

import room_textures

WALL_THICKNESS = 0.1
DEFAULT_CEILING = 2.6
MIN_ROOM_SIDE = 1.0


def srgb_to_linear(c):
    c = np.asarray(c, dtype=np.float64)
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


# The open-loop light level (used when the Scene Builder cannot measure a draft render, which is what normally sets it): fitted on three rendered rooms (a living room, a dining
# room, a long gallery) whose needed strengths came out between 1 and 4 times this formula's, so it sits in the middle of them. See docs/PHOTO_TO_SCENE.md.
LIGHT_CALIBRATION = 0.56


def picture_brightness(img):
    """How bright a picture looks, for matching the room's light to the photo's: the mean of every channel value (0..1, as displayed) with anything above 0.9 held at 0.9, so a window
    clipped to white does not count for more than a bright wall. scene_room.h's pictureBrightness is the same number for the draft render."""
    a = np.asarray(img, dtype=np.float64)
    if a.max() > 1.5:
        a = a / 255.0
    return float(np.minimum(a, 0.9).mean())


def tone_curve(x):
    """The renderer's tone curve (ACES filmic, Narkowicz's fit), radiance -> display-linear value."""
    return (x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14)


def tone_curve_inverse(y):
    """The radiance the tone curve maps to the display-linear value y (bisection; the curve rises steadily on [0, 4])."""
    lo, hi = 0.0, 4.0
    for _ in range(50):
        mid = (lo + hi) / 2.0
        if tone_curve(mid) < y:
            lo = mid
        else:
            hi = mid
    return (lo + hi) / 2.0


def box_blur(a, k=1):
    """Mean over a (2k+1) x (2k+1) window of a (H, W) or (H, W, C) array, edges repeated."""
    pad = [(k, k), (k, k)] + [(0, 0)] * (a.ndim - 2)
    p = np.pad(a, pad, mode="edge")
    out = np.zeros_like(a, dtype=np.float64)
    h, w = a.shape[:2]
    for dy in range(2 * k + 1):
        for dx in range(2 * k + 1):
            out += p[dy:dy + h, dx:dx + w]
    return out / float((2 * k + 1) ** 2)


def downsample(a, step):
    return a[::step, ::step]


def unproject(depth, hfov_deg):
    """(H, W, 3) points in the camera frame: X right, Y up, Z forward (the depth). The pixel centre is the pixel's own point."""
    h, w = depth.shape
    f = (w / 2.0) / math.tan(math.radians(hfov_deg) / 2.0)
    u = np.arange(w) + 0.5
    v = np.arange(h) + 0.5
    uu, vv = np.meshgrid(u, v)
    x = (uu - w / 2.0) / f * depth
    y = -(vv - h / 2.0) / f * depth
    return np.stack([x, y, depth], axis=-1), f


def surface_normals(points, depth):
    """Unit normals (H, W, 3) turned towards the camera, and a (H, W) mask of the pixels whose neighbourhood is one smooth surface (not an edge)."""
    h, w, _ = points.shape
    du = np.zeros_like(points)
    dv = np.zeros_like(points)
    du[:, 1:-1] = points[:, 2:] - points[:, :-2]
    dv[1:-1] = points[2:] - points[:-2]
    n = np.cross(du, dv)
    length = np.linalg.norm(n, axis=-1)
    ok = length > 1e-12
    n = n / np.maximum(length, 1e-12)[..., None]
    # Turn every normal to face the camera (the camera is at the origin; the point itself points away from it).
    facing = np.sum(n * points, axis=-1)
    n = np.where((facing > 0)[..., None], -n, n)
    # An edge: the depth jumps between neighbours (relative to the depth itself), or the point is at the border.
    jump = np.zeros_like(depth)
    jump[:, 1:-1] = np.maximum(np.abs(depth[:, 2:] - depth[:, 1:-1]), np.abs(depth[:, :-2] - depth[:, 1:-1]))
    jump[1:-1] = np.maximum(jump[1:-1], np.maximum(np.abs(depth[2:] - depth[1:-1]), np.abs(depth[:-2] - depth[1:-1])))
    ok &= jump < 0.08 * depth + 0.02
    ok[0, :] = ok[-1, :] = False
    ok[:, 0] = ok[:, -1] = False
    ok &= depth > 0.05
    return n, ok


def rotation_between(a, b):
    """The smallest rotation matrix that turns unit vector a into unit vector b."""
    a = np.asarray(a, float) / np.linalg.norm(a)
    b = np.asarray(b, float) / np.linalg.norm(b)
    v = np.cross(a, b)
    c = float(np.dot(a, b))
    if c > 1.0 - 1e-12:
        return np.eye(3)
    if c < -1.0 + 1e-12:  # opposite: turn half way round any perpendicular axis
        axis = np.cross(a, [1.0, 0.0, 0.0])
        if np.linalg.norm(axis) < 1e-6:
            axis = np.cross(a, [0.0, 1.0, 0.0])
        axis /= np.linalg.norm(axis)
        return 2.0 * np.outer(axis, axis) - np.eye(3)
    s2 = float(np.dot(v, v))
    vx = np.array([[0, -v[2], v[1]], [v[2], 0, -v[0]], [-v[1], v[0], 0]])
    return np.eye(3) + vx + vx @ vx * ((1.0 - c) / s2)


def estimate_up(normals, ok):
    """The camera-frame direction of 'up': the mean normal of the floor and ceiling (what faces within 40 degrees of the current guess, from (0, 1, 0)).
    Returns (up, found): found is False when there was too little floor or ceiling to say."""
    h, w, _ = normals.shape
    up = np.array([0.0, 1.0, 0.0])
    rows = np.arange(h)[:, None] * np.ones((1, w))
    lower = rows >= h * 0.4
    found = False
    for _ in range(8):
        d = normals @ up
        floor = ok & lower & (d > math.cos(math.radians(40)))
        ceiling = ok & ~lower & (d < -math.cos(math.radians(40)))
        count = int(floor.sum() + ceiling.sum())
        if count < 0.02 * h * w:
            break
        found = True
        mean = normals[floor].sum(axis=0) - normals[ceiling].sum(axis=0)
        if np.linalg.norm(mean) < 1e-9:
            break
        new = mean / np.linalg.norm(mean)
        if np.dot(new, up) > 1.0 - 1e-9:
            up = new
            break
        up = new
    return up, found


def estimate_turn(nx, nz, weight):
    """The angle (radians, in (-pi/4, pi/4]) the room's walls are turned by about the vertical axis, and how sure that is (0..1): walls come in perpendicular pairs, so
    the angle of the wall normals is read modulo 90 degrees (the mean of e^(4i phi))."""
    if weight.size == 0:
        return 0.0, 0.0
    phi = np.arctan2(nz, nx)
    z = np.sum(weight * np.exp(4j * phi))
    total = float(np.sum(weight))
    if total <= 0:
        return 0.0, 0.0
    theta = np.angle(z) / 4.0
    return float(theta), float(abs(z) / total)


def turn_xz(x, z, theta):
    """Turns the vectors (x, z) so that a direction at angle theta ends up along +x."""
    c, s = math.cos(theta), math.sin(theta)
    return x * c + z * s, -x * s + z * c


def dense_extreme(values, support, low, bin_size=0.06, min_count=8):
    """The position of the outermost dense band of `values` on the low side (low=True: the smallest) or the high side, ignoring thin scatter: a band is a bin (of bin_size)
    holding at least `support` samples, plus the bins next to it. Returns None when nothing is dense enough."""
    if values.size < min_count:
        return None
    lo, hi = float(values.min()), float(values.max())
    n = max(1, int(math.ceil((hi - lo) / bin_size)) + 1)
    hist, edges = np.histogram(values, bins=n, range=(lo, lo + n * bin_size))
    smooth = hist + np.pad(hist[:-1], (1, 0)) + np.pad(hist[1:], (0, 1))
    good = np.nonzero(smooth >= max(support, min_count))[0]
    if good.size == 0:
        return None
    i = int(good[0] if low else good[-1])
    # The band's own position: the mean of the samples in the bin and its neighbours.
    a, b = edges[max(i - 1, 0)], edges[min(i + 2, n)]
    inside = values[(values >= a) & (values < b)]
    if inside.size == 0:
        return None
    return float(np.median(inside))


def label_blobs(mask):
    """4-connected components of a boolean (H, W) mask: a list of arrays of (row, col) pixels, biggest first."""
    h, w = mask.shape
    seen = np.zeros_like(mask, dtype=bool)
    blobs = []
    for r in range(h):
        for c in range(w):
            if not mask[r, c] or seen[r, c]:
                continue
            stack = [(r, c)]
            seen[r, c] = True
            pixels = []
            while stack:
                y, x = stack.pop()
                pixels.append((y, x))
                for ny, nx in ((y + 1, x), (y - 1, x), (y, x + 1), (y, x - 1)):
                    if 0 <= ny < h and 0 <= nx < w and mask[ny, nx] and not seen[ny, nx]:
                        seen[ny, nx] = True
                        stack.append((ny, nx))
            blobs.append(np.array(pixels))
    blobs.sort(key=len, reverse=True)
    return blobs


def mirror_x(result):
    """The result in the Scene Builder's right-handed frame: every x negated (a box's min and max swapped so min stays below max)."""
    def flip_box(b):
        b["min"][0], b["max"][0] = -b["max"][0], -b["min"][0]
    for key in ("position", "target", "up"):
        result["camera"][key][0] = -result["camera"][key][0]
    flip_box(result["room"])
    for b in result["surfaces"] + result["lights"]:
        flip_box(b)
    return result


def box(name, lo, hi, color=None, **extra):
    d = {"name": name, "min": [round(float(v), 4) for v in lo], "max": [round(float(v), 4) for v in hi]}
    if color is not None:
        d["color"] = [round(float(v), 4) for v in color]
    d.update(extra)
    return d


def fit_room(depth, rgb, hfov_deg, camera_height=None, max_side=160, return_geometry=False):
    """Fits a room to one photo's depth map.

    depth: (H, W) metres along the viewing direction. rgb: (H, W, 3) uint8 (or floats in 0..1) of the same photo. hfov_deg: the photo's horizontal field of view.
    camera_height: when given (metres), everything is scaled so the camera is that high above the floor (the depth model's scale is a guess).
    return_geometry: also return what room_objects.py needs to put furniture in the room (see below): (result, geometry) instead of result.
    Returns a dict:
      camera   {position, target, up, vfov}   where the photo was taken from, in the room's frame
      room     {min, max}                      the inside of the room (x, y, z), floor at y = 0
      surfaces [box...]                        floor, ceiling and four walls as thin slabs with a linear-light colour
      lights   [box...]                        emissive panels: windows, lamps (or one ceiling light when the photo shows none)
      notes    [str...]                        what was assumed, for the user
    """
    depth = np.asarray(depth, dtype=np.float64)
    img = np.asarray(rgb)
    if img.dtype != np.float64 and img.dtype != np.float32:
        img = img.astype(np.float64) / 255.0
    img = img.astype(np.float64)
    h0, w0 = depth.shape
    depth_full, img_full = depth, img   # (kept at full size: room_textures reads the photo's own pixels)
    step = max(1, int(math.ceil(max(h0, w0) / float(max_side))))
    depth = box_blur(downsample(depth, step), 1)
    img = downsample(img, step)
    h, w = depth.shape
    notes = []

    points, f = unproject(depth, hfov_deg)
    normals, ok = surface_normals(points, depth)
    if int(ok.sum()) < 0.1 * h * w:
        raise ValueError("the depth map is too rough to find any surfaces in")

    # ---- level the camera: up from the floor and ceiling
    up, found_up = estimate_up(normals, ok)
    if not found_up:
        notes.append("The floor was not clear in the photo, so the camera is assumed level.")
    R = rotation_between(up, [0.0, 1.0, 0.0])
    P = points.reshape(-1, 3) @ R.T
    N = normals.reshape(-1, 3) @ R.T
    OK = ok.reshape(-1)
    forward = R @ np.array([0.0, 0.0, 1.0])
    cam_up = R @ np.array([0.0, 1.0, 0.0])

    # ---- the floor: the lowest large up-facing surface
    upfacing = OK & (N[:, 1] > 0.92)
    if int(upfacing.sum()) >= 20:
        ys = P[upfacing, 1]
        y0 = float(np.percentile(ys, 8))
        near = ys[(ys > y0 - 0.12) & (ys < y0 + 0.12)]
        floor_y = float(np.median(near))
    else:
        floor_y = float(np.percentile(P[OK, 1], 3))
        notes.append("No floor could be found; the lowest point seen is taken as the floor.")
    P[:, 1] -= floor_y
    cam_h = -floor_y
    if cam_h < 0.2:
        cam_h = 1.5
        notes.append("The camera height could not be told; 1.5 m is assumed.")
    scale = 1.0
    if camera_height is not None and camera_height > 0:
        scale = camera_height / cam_h
        P *= scale
        cam_h = camera_height
    elif not (0.4 <= cam_h <= 3.0):
        notes.append("The depth model put the camera %.1f m above the floor, which is unlikely; check the room's size (or give the camera height)." % cam_h)
    pos = np.array([0.0, cam_h, 0.0])

    # ---- the walls' turn about the vertical
    vertical = OK & (np.abs(N[:, 1]) < 0.3) & (P[:, 1] > 0.15)
    theta, sure = estimate_turn(N[vertical, 0], N[vertical, 2], np.ones(int(vertical.sum())))
    if sure < 0.35:
        theta = 0.0
        notes.append("The walls' direction was not clear, so the room is lined up with the camera.")
    X, Z = turn_xz(P[:, 0], P[:, 2], theta)
    NX, NZ = turn_xz(N[:, 0], N[:, 2], theta)
    fx, fz = turn_xz(forward[0], forward[2], theta)
    forward_room = np.array([fx, forward[1], fz])
    PR = np.stack([X, P[:, 1], Z], axis=-1)
    PRimg = PR.reshape(h, w, 3)

    nvert = max(int(vertical.sum()), 1)
    support = max(8, int(0.03 * nvert))

    # ---- the walls the photo shows: the outermost dense band of wall-facing points on each side of the camera
    side = {}
    for key, coord, normal_axis, lowside in (("left", X, NX, True), ("right", X, NX, False), ("back", Z, NZ, False), ("front", Z, NZ, True)):
        on_axis = vertical & (np.abs(normal_axis) > 0.8)
        part = on_axis & ((coord < -0.2) if lowside else (coord > 0.2))
        side[key] = dense_extreme(coord[part], support, lowside)

    extent_x = PR[vertical, 0] if vertical.any() else PR[OK, 0]
    extent_z = PR[vertical, 2] if vertical.any() else PR[OK, 2]
    margin = 0.3
    seen = {k: v is not None for k, v in side.items()}
    if side["left"] is None:
        side["left"] = min(float(extent_x.min()) - margin, -MIN_ROOM_SIDE / 2) if extent_x.size else -2.0
        notes.append("The left wall is not in the photo; it is placed just beyond what the photo shows.")
    if side["right"] is None:
        side["right"] = max(float(extent_x.max()) + margin, MIN_ROOM_SIDE / 2) if extent_x.size else 2.0
        notes.append("The right wall is not in the photo; it is placed just beyond what the photo shows.")
    if side["back"] is None:
        side["back"] = max(float(extent_z.max()) + margin, MIN_ROOM_SIDE) if extent_z.size else 4.0
        notes.append("The far wall is not in the photo; it is placed just beyond what the photo shows.")
    if side["front"] is None:
        side["front"] = -0.4  # the wall behind the camera: never seen, just behind it
        if forward_room[2] < 0.5:
            notes.append("The wall behind the camera is not in the photo; it is placed just behind it.")
    xmin, xmax = min(side["left"], -0.2), max(side["right"], 0.2)
    zmin, zmax = min(side["front"], -0.2), max(side["back"], 0.2)

    # ---- the ceiling
    down = OK & (N[:, 1] < -0.8)
    ceiling_y = dense_extreme(PR[down, 1], max(8, int(0.01 * h * w)), False) if int(down.sum()) >= 20 else None
    top_walls = float(np.percentile(PR[vertical, 1], 98)) if vertical.any() else 0.0
    if ceiling_y is None or ceiling_y < max(top_walls, 1.8):
        ceiling_y = max(DEFAULT_CEILING, top_walls + 0.05)
        notes.append("The ceiling is not in the photo; it is assumed %.1f m high." % ceiling_y)
    ceiling_y = float(min(max(ceiling_y, cam_h + 0.3), 6.0))

    # ---- colours: what the photo shows on each surface, as linear reflectance
    lin = srgb_to_linear(img).reshape(-1, 3)
    near_tol = 0.12
    assign = {}
    planes = {
        "floor": (PR[:, 1] < near_tol, N[:, 1] > 0.7),
        "ceiling": (np.abs(PR[:, 1] - ceiling_y) < near_tol * 1.5, N[:, 1] < -0.7),
        "left": (np.abs(PR[:, 0] - xmin) < near_tol, NX > 0.7),
        "right": (np.abs(PR[:, 0] - xmax) < near_tol, NX < -0.7),
        "back": (np.abs(PR[:, 2] - zmax) < near_tol, NZ < -0.7),
        "front": (np.abs(PR[:, 2] - zmin) < near_tol, NZ > 0.7),
    }
    for key, (near_plane, facing) in planes.items():
        assign[key] = OK & near_plane & facing
    lum_all = []
    colors = {}
    for key, m in assign.items():
        if int(m.sum()) >= 6:
            colors[key] = np.median(lin[m], axis=0)
            lum_all.append(lin[m] @ np.array([0.2126, 0.7152, 0.0722]))
    defaults = {"floor": (0.35, 0.3, 0.26), "ceiling": (0.8, 0.8, 0.8), "left": (0.7, 0.68, 0.64), "right": (0.7, 0.68, 0.64), "back": (0.72, 0.7, 0.66), "front": (0.7, 0.68, 0.64)}
    for key in planes:
        if key not in colors:
            colors[key] = np.array(defaults[key])
    # A photo is lit; reflectance is what the surface does with light. Without knowing the light, scale so the photo's typical surface is a typical room's 0.45.
    k = 1.0
    if lum_all:
        typical = float(np.median(np.concatenate(lum_all)))
        if typical > 1e-3:
            k = float(np.clip(0.45 / typical, 0.5, 3.0))   # (a dark room's walls are dark: the exposure is only partly undone)
    for key in colors:
        c = np.maximum(colors[key] * k, 1e-4)
        c = c * min(1.0, 0.9 / float(c.max()))              # too bright: scale all three, so the hue stays (a per-channel clip turns a floor orange)
        colors[key] = np.maximum(c, 0.03)

    t = WALL_THICKNESS
    surfaces = [
        box("Floor", (xmin - t, -t, zmin - t), (xmax + t, 0.0, zmax + t), colors["floor"]),
        box("Ceiling", (xmin - t, ceiling_y, zmin - t), (xmax + t, ceiling_y + t, zmax + t), colors["ceiling"]),
        box("Left wall", (xmin - t, 0.0, zmin - t), (xmin, ceiling_y, zmax + t), colors["left"]),
        box("Right wall", (xmax, 0.0, zmin - t), (xmax + t, ceiling_y, zmax + t), colors["right"]),
        box("Back wall", (xmin, 0.0, zmax), (xmax, ceiling_y, zmax + t), colors["back"]),
        box("Front wall", (xmin, 0.0, zmin - t), (xmax, ceiling_y, zmin), colors["front"]),
    ]

    # ---- lights: patches that are clipped to white in the photo (a window, a lamp)
    lights = []
    bright = box_blur(img.min(axis=-1), 1) >= 0.93   # (ordinary white paint is about 0.9; a window or a lamp in the photo is clipped to 1)
    blobs = [b for b in label_blobs(bright) if max(6, int(0.004 * h * w)) <= len(b) <= 0.25 * h * w][:4]   # (not a whole washed-out wall)
    # the camera ray of a pixel, in the room frame
    def ray(r, c):
        d = R @ np.array([(c + 0.5 - w / 2.0) / f, -(r + 0.5 - h / 2.0) / f, 1.0])
        dx, dz = turn_xz(d[0], d[2], theta)
        return np.array([dx, d[1], dz])

    surfaces_to_try = [("ceiling", 1, ceiling_y), ("left", 0, xmin), ("right", 0, xmax), ("back", 2, zmax), ("front", 2, zmin)]
    panel_thickness = 0.03
    for pix in blobs:
        rays = np.array([ray(r, c) for r, c in pix])
        measured = PRimg[pix[:, 0], pix[:, 1]]
        # Which surface is the patch on: the one most of its pixels' rays hit inside the room, and where the depth model's own points are closest to it.
        best = None
        for name, axis, value in surfaces_to_try:
            with np.errstate(divide="ignore", invalid="ignore"):
                tt = (value - pos[axis]) / rays[:, axis]
            good = tt > 0.05
            if good.mean() < 0.5:
                continue
            hit = pos[None, :] + rays * tt[:, None]
            inside = ((hit[:, 0] > xmin - 0.05) & (hit[:, 0] < xmax + 0.05) & (hit[:, 1] > -0.05) & (hit[:, 1] < ceiling_y + 0.05)
                      & (hit[:, 2] > zmin - 0.05) & (hit[:, 2] < zmax + 0.05))
            frac = float((good & inside).mean())
            gap = float(np.median(np.abs(measured[:, axis] - value)))
            score = frac - 0.15 * min(gap, 3.0)
            if frac > 0.5 and (best is None or score > best[0]):
                best = (score, name, axis, value, hit[good & inside])
        if best is None:
            continue
        _, name, axis, value, hit = best
        color = np.clip(srgb_to_linear(img[pix[:, 0], pix[:, 1]]).mean(axis=0), 0.0, 1.0)
        color = color / max(float(color.max()), 1e-6)
        lo, hi = hit.min(axis=0), hit.max(axis=0)
        # a thin panel just inside the room: the surfaces at the low end of an axis (left, front) are entered going up, the others going down
        if name in ("left", "front"):
            lo[axis], hi[axis] = value, value + panel_thickness
        else:
            lo[axis], hi[axis] = value - panel_thickness, value
        lo = np.maximum(lo, [xmin, 0.0, zmin])   # (kept on the wall)
        hi = np.minimum(hi, [xmax, ceiling_y, zmax])
        if np.any(hi - lo <= 0):
            continue
        kind = "Ceiling light" if name == "ceiling" else "Window light"
        lights.append(box("%s %d" % (kind, len(lights) + 1), lo, hi, color, strength=8.0 if name == "ceiling" else 6.0, surface=name))
    if not lights:
        cx, cz = (xmin + xmax) / 2.0, (zmin + zmax) / 2.0
        half = min(0.4, (xmax - xmin) / 6.0, (zmax - zmin) / 6.0)
        lights.append(box("Ceiling light 1", (cx - half, ceiling_y - 0.03, cz - half), (cx + half, ceiling_y, cz + half), (1.0, 0.93, 0.8), strength=10.0, surface="ceiling"))
        notes.append("No window or lamp was bright enough to see, so one light is put in the middle of the ceiling.")

    # How strong the lights are: bright enough that the room's average picture value is the photo's. A closed room lit by panels of radiance L and total area A_p has walls of about
    # rho * Phi / (pi * A_tot * (1 - rho)) with Phi = pi * L * A_p, and the picture value is that radiance through the renderer's tone curve; this inverts it. It is a rough
    # guess (furniture, windows' own light and the room's shape all change it), so the strength is limited and is easy to change in the Builder.
    area_total = 2.0 * ((xmax - xmin) * (ceiling_y) + (xmax - xmin) * (zmax - zmin) + ceiling_y * (zmax - zmin))
    panel_area = 0.0
    for lt in lights:
        e = sorted(np.array(lt["max"]) - np.array(lt["min"]))
        panel_area += float(e[1] * e[2])
    mean_display = float(np.clip(img.mean(), 0.02, 0.95))
    target = tone_curve_inverse(float(srgb_to_linear(mean_display)))
    areas = {"floor": (xmax - xmin) * (zmax - zmin), "ceiling": (xmax - xmin) * (zmax - zmin), "left": ceiling_y * (zmax - zmin), "right": ceiling_y * (zmax - zmin),
             "back": ceiling_y * (xmax - xmin), "front": ceiling_y * (xmax - xmin)}
    lum_w = np.array([0.2126, 0.7152, 0.0722])
    rho = sum(areas[key] * float(colors[key] @ lum_w) for key in areas) / max(sum(areas.values()), 1e-6)
    rho = float(np.clip(rho, 0.05, 0.8))
    if panel_area > 0:
        strength = float(np.clip(LIGHT_CALIBRATION * target * area_total * (1.0 - rho) / (rho * panel_area), 3.0, 150.0))
        for lt in lights:
            lt["strength"] = round(strength, 2)

    target = pos + forward_room
    vfov = 2.0 * math.degrees(math.atan(math.tan(math.radians(hfov_deg) / 2.0) * h0 / float(w0)))
    geometry = {
        # the photo's pixels as 3D points in the room's own frame BEFORE the mirror (x as the photo sees it: to the right), on the same grid the fit used: (h, w, 3), the matching
        # "this pixel has a surface" mask, and the step that grid is subsampled by from the full-size picture
        "points": PRimg, "ok": ok, "step": step, "camera": pos.copy(), "image": img, "exposure": k,
        "textures": room_textures.camera_model(depth_full, img_full, hfov_deg, R, theta, pos, scale),
        "box": (xmin, xmax, zmin, zmax, ceiling_y),
    }
    result = mirror_x({
        "camera": {"position": [round(float(v), 4) for v in pos], "target": [round(float(v), 4) for v in target], "up": [round(float(v), 5) for v in cam_up], "vfov": round(vfov, 3)},
        "room": {"min": [round(xmin, 4), 0.0, round(zmin, 4)], "max": [round(xmax, 4), round(ceiling_y, 4), round(zmax, 4)]},
        "surfaces": surfaces,
        "lights": lights,
        "notes": notes,
        "info": {"turn_degrees": round(math.degrees(theta), 2), "turn_confidence": round(sure, 3), "camera_height": round(cam_h, 3), "depth_scale": round(scale, 4), "walls_seen": seen, "exposure": round(k, 4)},
    })
    return (result, geometry) if return_geometry else result
