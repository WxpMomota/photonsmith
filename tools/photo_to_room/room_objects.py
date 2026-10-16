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
    "cabinet-merged", "shelf", "counter", "pillow", "teddy bear", "suitcase", "book", "bottle", "cup", "wine glass", "bowl", "toaster", "hair drier", "umbrella",
}
MIN_AREA = 0.012       # an object must cover at least this fraction of the picture (smaller ones come out as blobs)
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


def place_object(geometry, mask, min_points=20):
    """Where one object stands, from its mask: {"front": (x, z) the middle of the visible side, "dir": (x, z) the unit vector from the camera towards it, "bottom", "height", "width"}
    in the photo's own frame (x to the right), or None when too little of it has depth to say. `geometry` is what room_fit.fit_room(..., return_geometry=True) gives."""
    points, ok, step = geometry["points"], geometry["ok"], geometry["step"]
    h, w = ok.shape
    m = np.asarray(mask, dtype=bool)[::step, ::step]
    if m.shape != ok.shape:   # (a mask of another size: pad or cut to the grid)
        fixed = np.zeros(ok.shape, dtype=bool)
        fixed[:min(h, m.shape[0]), :min(w, m.shape[1])] = m[:h, :w]
        m = fixed
    p = points[m & ok]
    if len(p) < min_points:
        return None
    # depth outliers (an edge pixel that took the wall's depth): keep what is near the median distance from the camera
    dist = np.hypot(p[:, 0], p[:, 2])
    med = float(np.median(dist))
    p = p[np.abs(dist - med) < max(0.8, 0.5 * med)]
    if len(p) < min_points:
        return None
    bottom = float(np.percentile(p[:, 1], 2))
    top = float(np.percentile(p[:, 1], 98))
    if bottom < 0.12 or m[-3:].any():
        bottom = 0.0   # it stands on the floor; an object that runs off the bottom of the picture shows no base at all, and is taken to stand there too
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
