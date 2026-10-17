"""room_objects.py - puts furniture in the room room_fit.py found (numpy only, no model).

photo_to_room.py segments the photo into objects (chair, sofa, plant, ...), has TripoSR make a mesh from each cut-out, and then needs to say WHERE each goes and HOW BIG it is. That
is here, from the depth the photo gave:

  * the object's mask picks its pixels' 3D points (room_fit's geometry): the lowest is where it stands (on the floor, or on something), the highest how tall it is, and the spread
    across the line of sight how wide;
  * the photo only shows an object's near side, so the object's middle is half its depth further from the camera than the visible points, and the generated mesh says how deep that is;
  * the mesh is turned to face the camera (it was made from the view the photo has: its front is what the photo shows), or lined up with the room's walls when that is close.

Everything in a placement is in the Scene Builder's right-handed frame (room_fit mirrors x at the end; so does this), metres, y up, the floor at y = 0.
"""
import math

import numpy as np

# What counts as furniture or a room's decoration: the names the segmentation model (DETR panoptic, COCO's classes) gives them. People and animals are left out on purpose.
FURNITURE = {
    "chair", "couch", "bed", "dining table", "table-merged", "toilet", "tv", "laptop", "potted plant", "vase", "clock", "refrigerator", "oven", "microwave", "sink", "bench",
    "cabinet-merged", "shelf", "counter", "pillow", "teddy bear", "suitcase", "book", "bottle", "cup", "wine glass", "bowl", "toaster", "hair drier", "umbrella", "stairs",
}
# Things that stand on the floor: their lowest visible point is no guide (a table's thin legs and a chair's feet vanish in a depth map and the lowest point read is the seat or the
# tabletop), so they are taken to stand on it. Plants, vases, cups and the like are not here: they are as often on a table.
FLOOR_STANDING = {"chair", "couch", "bed", "dining table", "table-merged", "toilet", "refrigerator", "oven", "bench", "cabinet-merged", "counter", "shelf", "sink", "suitcase", "stairs"}
FLOOR_PRIOR_BELOW = 0.7      # ... when the lowest point read is below this height (above it, the piece really is on something)
# Furniture that is a slab and four legs or a plain block is built from measured boxes instead of being guessed by the 3D model, which turns thin legs into lumps: the table, and
# the blocks with the depth to give one when the photo only shows the front (metres).
TABLES = {"dining table", "table-merged"}
BLOCKS = {"refrigerator": 0.7, "oven": 0.6, "cabinet-merged": 0.55, "counter": 0.6, "shelf": 0.35, "bed": 2.0}
MIN_STAIR_WIDTH = 0.9   # metres
MIN_AREA = 0.005       # an object must cover at least this fraction of the picture (smaller ones come out as blobs; a chair across a room is under 1%)
MAX_OBJECTS = 6
MIN_SCORE = 0.5
SNAP_DEGREES = 20.0    # a facing direction this close to a wall's direction is lined up with the wall


def relight_texture(rgb8, exposure):
    """A furniture texture the way room_fit treats the walls: the picture's colours are lit pixels, not reflectances, so they are brightened by the same `exposure` the room's surfaces
    got (in linear light, keeping the hue: whatever would go over 0.9 is scaled down as a whole). `rgb8` is a (h, w, 3) uint8 array in sRGB; returns the same kind."""
    a = rgb8.astype(np.float64) / 255.0
    lin = np.where(a <= 0.04045, a / 12.92, ((a + 0.055) / 1.055) ** 2.4) * float(exposure)
    top = lin.max(axis=-1, keepdims=True)
    lin = lin * np.minimum(1.0, 0.9 / np.maximum(top, 1e-9))
    out = np.where(lin <= 0.0031308, lin * 12.92, 1.055 * np.power(np.maximum(lin, 0.0), 1 / 2.4) - 0.055)
    return (np.clip(out, 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8)


# What is in a room but is not its walls and floor: it is left out of their pictures (a staircase, a chair or a bag in front of a wall must not be painted on it too). The furniture
# classes above, and the other things the segmentation model names.
OBJECT_LABELS = set(FURNITURE) | {"stairs", "person", "handbag", "backpack", "teddy bear", "laptop", "keyboard", "mouse", "remote", "cell phone", "tv", "book", "clock", "scissors", "bird",
                                  "cat", "dog", "umbrella", "tie", "bottle", "cup", "wine glass", "bowl", "banana", "apple", "orange", "toothbrush", "vase", "potted plant", "bench"}


def dilate(mask, radius):
    """The mask grown by `radius` pixels in every direction (a square window)."""
    out = np.asarray(mask, dtype=bool).copy()
    for _ in range(radius):
        grown = out.copy()
        grown[1:] |= out[:-1]
        grown[:-1] |= out[1:]
        grown[:, 1:] |= out[:, :-1]
        grown[:, :-1] |= out[:, 1:]
        out = grown
    return out


def object_mask(segments, radius=3):
    """Everything in the photo that is a thing in the room (see OBJECT_LABELS), grown a little (an object's edge pixels blend with what is behind it): (h, w) bool, or None."""
    union = None
    for s in segments:
        if s["label"] in OBJECT_LABELS:
            union = np.asarray(s["mask"], dtype=bool) if union is None else (union | np.asarray(s["mask"], dtype=bool))
    return dilate(union, radius) if union is not None else None


def mask_overlap(a, b):
    inter = float(np.logical_and(a, b).sum())
    union = float(np.logical_or(a, b).sum())
    return inter / union if union > 0 else 0.0


def choose_objects(segments, max_count=MAX_OBJECTS, min_area=MIN_AREA, min_score=MIN_SCORE):
    """The segments worth making a mesh of: furniture by name, big enough, sure enough, not on top of one already chosen; the biggest first.
    Each segment is {"label": str, "score": float, "mask": bool array}. Returns the chosen ones, each with "area" (fraction of the picture) added."""
    chosen = []
    candidates = []
    for s in segments:
        if s["label"] not in FURNITURE or s["score"] < min_score:
            continue
        mask = np.asarray(s["mask"], dtype=bool)
        area = float(mask.mean())
        if area >= min_area:
            candidates.append(dict(s, mask=mask, area=area))
    candidates.sort(key=lambda s: s["area"], reverse=True)
    for s in candidates:
        if len(chosen) >= max_count:
            break
        if any(mask_overlap(s["mask"], c["mask"]) > 0.4 for c in chosen):
            continue
        chosen.append(s)
    return chosen


def masked_points(geometry, mask, min_points=20, require_ok=True):
    """The 3D points (and the photo's colours there, as display RGB 0..1) of an object's mask, without depth outliers; (None, None, mask) when there are too few. require_ok=False takes
    every pixel of the mask, not only those on a smooth surface (a staircase's treads are seen at a grazing angle and the depth map drops them)."""
    points, ok, step = geometry["points"], geometry["ok"], geometry["step"]
    h, w = ok.shape
    m = np.asarray(mask, dtype=bool)[::step, ::step]
    if m.shape != ok.shape:   # (a mask of another size: pad or cut to the grid)
        fixed = np.zeros(ok.shape, dtype=bool)
        fixed[:min(h, m.shape[0]), :min(w, m.shape[1])] = m[:h, :w]
        m = fixed
    sel = (m & ok) if require_ok else m.copy()
    inner = m.copy()   # one pixel in from the border: a border pixel's blurred depth lands in mid-air between the object and what is behind it
    inner[1:] &= m[:-1]
    inner[:-1] &= m[1:]
    inner[:, 1:] &= m[:, :-1]
    inner[:, :-1] &= m[:, 1:]
    if int((inner & (ok if require_ok else True)).sum()) >= max(2 * min_points, 0.4 * int(sel.sum())):
        sel = inner & ok if require_ok else inner
    p = points[sel]
    if len(p) < min_points:
        return None, None, m
    colours = geometry["image"][sel] if "image" in geometry else np.full((len(p), 3), 0.5)
    dist = np.hypot(p[:, 0], p[:, 2])
    med = float(np.median(dist))
    keep = np.abs(dist - med) < max(0.8, 0.5 * med)
    if int(keep.sum()) < min_points:
        return None, None, m
    return p[keep], colours[keep], m


def reflectance(display_rgb, exposure):
    """A surface's colour from the photo's pixels there, the way room_fit does it for the walls: linear light, the median, brightened by the room's exposure with the hue kept."""
    a = np.asarray(display_rgb, dtype=np.float64)
    lin = np.where(a <= 0.04045, a / 12.92, ((a + 0.055) / 1.055) ** 2.4)
    c = np.maximum(np.median(lin, axis=0) * float(exposure), 1e-4)
    c = c * min(1.0, 0.9 / float(c.max()))
    return [round(float(v), 4) for v in np.maximum(c, 0.03)]


def mirror_box(lo, hi):
    """A box from the photo's frame (x to the right) in the Scene Builder's (x mirrored): min and max, x swapped."""
    return [round(-hi[0], 4), round(lo[1], 4), round(lo[2], 4)], [round(-lo[0], 4), round(hi[1], 4), round(hi[2], 4)]


def top_footprint(geometry, m, top_y):
    """The footprint (x0, x1, z0, z1) of a flat top at height top_y, from the rays through the pixels that see it: each ray is meant for the plane y = top_y, so a pixel whose depth was
    thrown off (a top is seen at a grazing angle, and its far rows sit against a big depth jump to the wall) still gives the right place. None when the camera is below the top or too few pixels."""
    pts, cam = geometry["points"][m], geometry["camera"]
    if top_y >= cam[1] - 0.05 or len(pts) < 8:
        return None
    rows = np.nonzero(m)[0]
    ys = geometry["points"][m][:, 1]
    row_mean = {int(r): float(ys[rows == r].mean()) for r in np.unique(rows)}
    top_rows = [r for r, v in row_mean.items() if v >= top_y - 0.12]
    if len(top_rows) < 2:
        return None
    on_top = np.isin(rows, top_rows)
    d = pts[on_top] - cam
    d[:, 1] = np.minimum(d[:, 1], -1e-6)   # (looking down onto the top)
    tt = (top_y - cam[1]) / d[:, 1]
    hit = cam + d * tt[:, None]
    xmin, xmax, zmin, zmax, _ = geometry["box"]
    inside = (hit[:, 0] > xmin - 0.1) & (hit[:, 0] < xmax + 0.1) & (hit[:, 2] > zmin - 0.1) & (hit[:, 2] < zmax + 0.1)
    hit = hit[inside]
    if len(hit) < 8:
        return None
    return (float(np.percentile(hit[:, 0], 2)), float(np.percentile(hit[:, 0], 98)), float(np.percentile(hit[:, 2], 2)), float(np.percentile(hit[:, 2], 98)))


def table_parts(geometry, mask, name="Table"):
    """A table from its top: a slab the size of the visible top (the camera looks down on it) on four legs to the floor. [{name, min, max, color}] in the Scene Builder's frame, or None
    when too little of the top is seen."""
    p, colours, m = masked_points(geometry, mask)
    if p is None:
        return None
    top_y = float(np.percentile(p[:, 1], 98))
    if top_y < 0.3 or top_y > 1.6:
        return None
    top = p[:, 1] >= top_y - 0.08
    if int(top.sum()) < 25:
        return None
    x0, x1 = float(np.percentile(p[top, 0], 2)) - 0.03, float(np.percentile(p[top, 0], 98)) + 0.03
    z0, z1 = float(np.percentile(p[top, 2], 2)) - 0.03, float(np.percentile(p[top, 2], 98)) + 0.03
    seen = top_footprint(geometry, m, top_y)   # the rays see more of the top than the depth does: never less than the depth said
    if seen is not None:
        x0, x1, z0, z1 = min(x0, seen[0]), max(x1, seen[1]), min(z0, seen[2]), max(z1, seen[3])
    if x1 - x0 < 0.3 or z1 - z0 < 0.3:
        return None
    colour = reflectance(colours[top], geometry.get("exposure", 1.0))
    thick, leg, inset = 0.045, 0.06, 0.03
    parts = [("%s top" % name, (x0, top_y - thick, z0), (x1, top_y, z1))]
    for i, (lx, lz) in enumerate(((x0 + inset, z0 + inset), (x1 - inset - leg, z0 + inset), (x0 + inset, z1 - inset - leg), (x1 - inset - leg, z1 - inset - leg))):
        parts.append(("%s leg %d" % (name, i + 1), (lx, 0.0, lz), (lx + leg, top_y - thick, lz + leg)))
    out = []
    for n, lo, hi in parts:
        a, b = mirror_box(lo, hi)
        out.append({"name": n, "min": a, "max": b, "color": colour})
    return out


def wall_behind(geometry, axis, sign, near_edge, lateral_lo, lateral_hi):
    """The coordinate of the wall behind an object (in the photo's frame): of the walls across `axis` that the room lies in front of (when the object is further out, at higher values, that
    is a wall with the room at lower values), the nearest one beyond the object's near edge whose stretch covers at least half of the object's width. None when there is none."""
    best = None
    width = max(lateral_hi - lateral_lo, 1e-6)
    for wl in geometry.get("walls", ()):
        if wl["axis"] != axis or wl["side"] != (-1 if sign > 0 else 1):
            continue
        beyond = wl["value"] - near_edge if sign > 0 else near_edge - wl["value"]
        if beyond < -0.05:
            continue
        overlap = min(lateral_hi, wl["hi"]) - max(lateral_lo, wl["lo"])
        if overlap < 0.5 * width:
            continue
        if best is None or beyond < best[0]:
            best = (beyond, wl["value"])
    return best[1] if best else None


def stairs_piece(geometry, mask, name="Stairs"):
    """A staircase from its mask: a Scene Builder stairs object as {"kind": "stairs", "name", "min", "max" (the box it fills, in the Scene Builder's frame), "ascend" (the horizontal
    direction it climbs, a unit axis vector there), "steps", "color"}, or None when what the mask shows is not a believable staircase. The run is whichever way the points spread further,
    the ascent is the way they rise, the rise comes from their height (the base is taken to be on the floor), and a step is about 18 cm."""
    p, colours, m = masked_points(geometry, mask, min_points=40, require_ok=False)
    if p is None:
        return None
    ranges = [float(np.percentile(p[:, a], 95) - np.percentile(p[:, a], 5)) for a in (0, 2)]
    axis = 0 if ranges[0] > ranges[1] else 2            # the run axis: x or z of the photo's frame
    across = 2 if axis == 0 else 0
    run_lo, run_hi = float(np.percentile(p[:, axis], 3)), float(np.percentile(p[:, axis], 97))
    wid_lo, wid_hi = float(np.percentile(p[:, across], 3)), float(np.percentile(p[:, across], 97))
    corr = float(np.corrcoef(p[:, axis], p[:, 1])[0, 1]) if p[:, axis].std() > 1e-6 and p[:, 1].std() > 1e-6 else 0.0
    if abs(corr) < 0.3:
        return None                                      # it does not rise along a line: not a staircase
    sign = 1.0 if corr > 0 else -1.0
    bottom = 0.0
    top = float(np.percentile(p[:, 1], 98))
    rise, run, width = top - bottom, run_hi - run_lo, wid_hi - wid_lo
    if width < MIN_STAIR_WIDTH:   # the side away from the camera is hidden behind the steps themselves: a flight is never much narrower than this
        if 0.5 * (wid_lo + wid_hi) >= 0.0:
            wid_hi = wid_lo + MIN_STAIR_WIDTH
        else:
            wid_lo = wid_hi - MIN_STAIR_WIDTH
        width = MIN_STAIR_WIDTH
    if rise < 0.5 or rise > 6.0 or width > 3.0 or run < 0.5:
        return None
    slope = rise / run                                    # (a stair is about 0.6 to 0.8: only a part of it is seen, but it must climb like one)
    if not (0.3 <= slope <= 1.4):
        run = rise / 0.7                                  # what is seen does not give the run: take a usual stair
        if sign > 0:
            run_hi = run_lo + run
        else:
            run_lo = run_hi - run
    lo, hi = [0.0, bottom, 0.0], [0.0, top, 0.0]
    lo[axis], hi[axis] = run_lo, run_hi
    lo[across], hi[across] = wid_lo, wid_hi
    a, b = mirror_box(lo, hi)
    ascend = [0.0, 0.0]
    ascend[0 if axis == 0 else 1] = sign * (-1.0 if axis == 0 else 1.0)   # (x is mirrored for the Scene Builder; z is not)
    return {"kind": "stairs", "name": name, "min": a, "max": b, "ascend": ascend, "steps": int(min(30, max(3, round(rise / 0.18)))), "color": reflectance(colours, geometry.get("exposure", 1.0))}


def block_parts(geometry, mask, label, place, name="Block"):
    """A plain block (a fridge, a cabinet, a counter, a bed) from what the photo shows: as wide and as tall as the visible side, as deep as the top if the camera sees it, else a
    typical depth for the kind (and out to the wall when one is just behind). Standing on the floor, the front facing the camera along the nearer room axis. [{name, min, max, color}]
    in the Scene Builder's frame, or None."""
    p, colours, _ = masked_points(geometry, mask)
    if p is None:
        return None
    top_y = float(np.percentile(p[:, 1], 98))
    if top_y < 0.2 or top_y > geometry["box"][4]:
        return None
    dx, dz = place["dir"]
    along_z = abs(dz) >= abs(dx)   # the front faces the camera along z (else along x)
    xmin, xmax, zmin, zmax, _ = geometry["box"]
    lo = np.array([float(np.percentile(p[:, 0], 2)), 0.0, float(np.percentile(p[:, 2], 2))])
    hi = np.array([float(np.percentile(p[:, 0], 98)), top_y, float(np.percentile(p[:, 2], 98))])
    axis = 2 if along_z else 0
    sign = 1.0 if (dz if along_z else dx) > 0 else -1.0   # +1: the object is further out along the axis than the camera, so its far side is at higher values
    measured = hi[axis] - lo[axis]
    depth = max(measured, BLOCKS.get(label, 0.5))
    other = 0 if along_z else 2
    wall = wall_behind(geometry, axis, sign, lo[axis] if sign > 0 else hi[axis], lo[other], hi[other])
    if wall is None:
        wall = (zmax if along_z else xmax) if sign > 0 else (zmin if along_z else xmin)
    if sign > 0:
        far = lo[axis] + depth
        if abs(wall - far) < 0.35 or far > wall:
            far = wall
        lo[axis], hi[axis] = lo[axis], far
    else:
        far = hi[axis] - depth
        if abs(wall - far) < 0.35 or far < wall:
            far = wall
        lo[axis], hi[axis] = far, hi[axis]
    if np.any(hi - lo < 0.1):
        return None
    a, b = mirror_box(lo, hi)
    return [{"name": name, "min": a, "max": b, "color": reflectance(colours, geometry.get("exposure", 1.0))}]


def place_object(geometry, mask, min_points=20, label=None):
    """Where one object stands, from its mask: {"front": (x, z) the middle of the visible side, "dir": (x, z) the unit vector from the camera towards it, "bottom", "height", "width"}
    in the photo's own frame (x to the right), or None when too little of it has depth to say. `geometry` is what room_fit.fit_room(..., return_geometry=True) gives; `label` (its
    name) lets a piece of furniture that stands on the floor be put there whatever its thin legs showed."""
    p, _, m = masked_points(geometry, mask, min_points)
    if p is None:
        return None
    bottom = float(np.percentile(p[:, 1], 6))   # (not the very lowest: a few border pixels with blurred depth hang in mid-air below an object)
    top = float(np.percentile(p[:, 1], 98))
    if bottom < 0.12 or m[-3:].any():
        bottom = 0.0   # it stands on the floor; an object that runs off the bottom of the picture shows no base at all, and is taken to stand there too
    elif label in FLOOR_STANDING and bottom < FLOOR_PRIOR_BELOW:
        bottom = 0.0   # (see FLOOR_STANDING)
    height = top - bottom
    if height < 0.08:
        return None
    cx, cz = float(np.median(p[:, 0])), float(np.median(p[:, 2]))
    distance = math.hypot(cx, cz)
    if distance < 0.2:
        return None
    dx, dz = cx / distance, cz / distance
    across = p[:, 0] * (-dz) + p[:, 2] * dx
    width = float(np.percentile(across, 95) - np.percentile(across, 5))
    return {"front": (cx, cz), "dir": (dx, dz), "bottom": bottom, "height": height, "width": max(width, 0.05)}


def finalize_object(place, mesh_size, box):
    """The mesh's placement in the Scene Builder's frame. `mesh_size` is the generated mesh's (width, height, depth) (it stands on y = 0, centred, its front towards +z, about 2 tall);
    `box` is room_fit's (xmin, xmax, zmin, zmax, ceiling) in the photo's frame. Returns {"position": [x, y, z], "yaw": degrees, "scale": s, "size": [w, h, d]} (size is the scaled
    mesh's), or None when the mesh has no size."""
    mw, mh, md = (max(float(v), 1e-6) for v in mesh_size)
    sh = place["height"] / mh
    sw = place["width"] / mw
    # The height is the surest of the two (the width is read across a view that may hide part of the object, or see it at an angle); the width only nudges the size, by 12% at most
    scale = sh * min(max(math.sqrt(sw / sh), 0.88), 1.12)
    depth = md * scale
    dx, dz = place["dir"]
    cx = place["front"][0] + dx * depth / 2.0
    cz = place["front"][1] + dz * depth / 2.0
    xmin, xmax, zmin, zmax, ceiling = box
    half = 0.5 * max(mw, md) * scale
    cx = min(max(cx, xmin + min(half, (xmax - xmin) / 2.0)), xmax - min(half, (xmax - xmin) / 2.0))
    cz = min(max(cz, zmin + min(half, (zmax - zmin) / 2.0)), zmax - min(half, (zmax - zmin) / 2.0))
    # Facing the camera, in the Scene Builder's frame (x mirrored): the mesh's front is +z, and a turn of `yaw` about +y takes +z to (sin yaw, cos yaw)
    yaw = math.degrees(math.atan2(dx, -dz))
    # lined up with the walls when it is nearly so (a sofa against a wall faces along it)
    nearest = round(yaw / 90.0) * 90.0
    if abs(yaw - nearest) <= SNAP_DEGREES:
        yaw = nearest
    if yaw <= -180.0:
        yaw += 360.0
    if yaw > 180.0:
        yaw -= 360.0
    return {"position": [round(-cx, 4), round(place["bottom"], 4), round(cz, 4)], "yaw": round(yaw, 2), "scale": round(scale, 4),
            "size": [round(mw * scale, 4), round(mh * scale, 4), round(md * scale, 4)]}
