"""room_textures.py - the room's floor, ceiling and walls with the photo's own look (numpy only, no model).

room_fit.py gives each surface one flat colour. This reads the real thing off the photo: for every point of a surface (a "texel" of its picture) it finds the photo pixel that sees that
point, with the fitted camera, and takes its colour. A point that something stands in front of (a chair in front of a wall) is skipped, found by comparing the depth the photo measured
with how far the point is, and holes (what the photo does not show, or furniture hid) are filled smoothly from what it does. So a wood floor keeps its grain, a tiled one its tiles,
wallpaper and the pictures on a wall come with the wall. Colours are brightened by the room's exposure like the flat ones (a lit photo is not a reflectance).

Every picture is laid out the way the Scene Builder's Box puts a picture on the face we see (scene_pbrt_writer.h writeBox): the face's own u and v axes, v up the picture.
"""
import math

import numpy as np

# For each surface: which face of its slab is the one we see (axis, sign), and the face's u and v directions, as (axis, sign) of the world axis each runs along.
# (scene_pbrt_writer.h writeBox: +X u=+y v=+z, -X u=+y v=-z, +Y u=+z v=+x, -Y u=-z v=+x, +Z u=+x v=+y, -Z u=-x v=+y.) The names are the photo's: "Left wall" is the wall at the
# left of the picture, which is at +x in the Scene Builder's mirrored frame; the room's inside is on the other side of each slab.
FACES = {
    "Floor":      {"face": (1, +1), "u": (2, +1), "v": (0, +1)},   # the slab's top
    "Ceiling":    {"face": (1, -1), "u": (2, -1), "v": (0, +1)},   # its underside
    "Left wall":  {"face": (0, -1), "u": (1, +1), "v": (2, -1)},   # at +x, seen from the room: its -X face
    "Right wall": {"face": (0, +1), "u": (1, +1), "v": (2, +1)},   # at -x: its +X face
    "Back wall":  {"face": (2, -1), "u": (0, -1), "v": (1, +1)},   # at +z: its -Z face
    "Front wall": {"face": (2, +1), "u": (0, +1), "v": (1, +1)},   # at -z: its +Z face
}


def min_filter(a, radius):
    """The smallest value within `radius` pixels in each direction (a square window): what the nearest thing in a neighbourhood is."""
    out = a
    for axis in (0, 1):
        stack = [out]
        for k in range(1, radius + 1):
            for sign in (-1, 1):
                shifted = np.roll(out, sign * k, axis=axis)
                # (np.roll wraps: put the edge value back instead of the far side's)
                if axis == 0:
                    if sign > 0:
                        shifted[:k] = out[:1]
                    else:
                        shifted[-k:] = out[-1:]
                else:
                    if sign > 0:
                        shifted[:, :k] = out[:, :1]
                    else:
                        shifted[:, -k:] = out[:, -1:]
                stack.append(shifted)
        out = np.minimum.reduce(stack)
    return out


# The layout of a Box's picture on each of its six faces (scene_pbrt_writer.h writeBox), as (axis, sign) of the world axis u and v run along.
_FACE_LAYOUT = {
    (0, +1): ((1, +1), (2, +1)), (0, -1): ((1, +1), (2, -1)),
    (1, +1): ((2, +1), (0, +1)), (1, -1): ((2, -1), (0, +1)),
    (2, +1): ((0, +1), (1, +1)), (2, -1): ((0, -1), (1, +1)),
}


def face_spec(axis, sign):
    """The picture layout (as in FACES) of the face of a slab that looks along `sign` of `axis`: the face a wall shows the room is the one facing the room's inside."""
    u, v = _FACE_LAYOUT[(int(axis), 1 if sign > 0 else -1)]
    return {"face": (int(axis), 1 if sign > 0 else -1), "u": u, "v": v}


def camera_model(depth_full, img_full, hfov_deg, R, theta, pos, scale):
    """What projecting a room point back into the photo needs. depth_full (h0, w0) metres, img_full (h0, w0, 3) display RGB 0..1, R the matrix that levelled the camera, theta the
    room's turn about the vertical, pos the camera in the room frame (before the mirror), scale the depth scale (1 unless the camera height was given)."""
    h0, w0 = depth_full.shape
    # The occlusion test looks at the NEAREST depth within a few pixels (about half a percent of the picture): a texel just beside a piece of furniture would otherwise take its edge pixels'
    # colour (the depth there is a blur of the furniture and what is behind it).
    near = min_filter(np.asarray(depth_full, float), max(1, int(round(0.005 * max(h0, w0)))))
    return {"depth": depth_full, "depth_near": near, "image": img_full, "R": np.asarray(R, float), "theta": float(theta), "pos": np.asarray(pos, float), "scale": float(scale),
            "f": (w0 / 2.0) / math.tan(math.radians(hfov_deg) / 2.0), "w": w0, "h": h0}


def project(model, world):
    """world (n, 3) points in the room's own frame (before the mirror) -> (u, v pixel coordinates, z the distance along the camera's axis in the photo's depth units)."""
    q = (np.asarray(world, float) - model["pos"]) / model["scale"]
    c, s = math.cos(-model["theta"]), math.sin(-model["theta"])   # undo the room's turn about the vertical
    x = q[:, 0] * c + q[:, 2] * s
    z = -q[:, 0] * s + q[:, 2] * c
    cam = np.stack([x, q[:, 1], z], axis=-1) @ model["R"]          # R^T applied to row vectors
    zc = cam[:, 2]
    with np.errstate(divide="ignore", invalid="ignore"):
        u = model["w"] / 2.0 + model["f"] * cam[:, 0] / zc
        v = model["h"] / 2.0 - model["f"] * cam[:, 1] / zc
    return u, v, zc


def sample(model, u, v, zc, tolerance=0.12, check_depth=True):
    """The photo's colour at pixel (u, v) for each point, and whether it is a good sample: inside the picture, in front of the camera, and not hidden (the depth the photo measured
    there is about the point's own distance; something nearer means the pixel shows that instead). check_depth=False skips that test: a window is on its wall by definition, and what
    the depth model measures through glass (the view outside) means nothing."""
    h, w = model["h"], model["w"]
    ok = (zc > 0.05) & (u >= 1) & (u <= w - 2) & (v >= 1) & (v <= h - 2)
    uu = np.clip(u, 0, w - 1.001)
    vv = np.clip(v, 0, h - 1.001)
    x0, y0 = np.floor(uu).astype(int), np.floor(vv).astype(int)
    fx, fy = (uu - x0)[:, None], (vv - y0)[:, None]
    img = model["image"]
    colour = (img[y0, x0] * (1 - fx) * (1 - fy) + img[y0, x0 + 1] * fx * (1 - fy) + img[y0 + 1, x0] * (1 - fx) * fy + img[y0 + 1, x0 + 1] * fx * fy)
    d = model["depth_near"]
    measured = np.minimum.reduce([d[y0, x0], d[y0, x0 + 1], d[y0 + 1, x0], d[y0 + 1, x0 + 1]])   # (the nearest thing around the pixel: an occluder's edge counts)
    if check_depth:
        with np.errstate(invalid="ignore"):
            ok &= np.abs(measured - zc) < tolerance * zc + 0.1
    return colour, ok


def upsample2x(a, shape):
    """A coarse (h, w, 3) picture enlarged to `shape` (h2, w2) with bilinear interpolation (a block copy would show as squares in the filled areas)."""
    h, w = a.shape[:2]
    h2, w2 = shape
    ys = np.clip((np.arange(h2) + 0.5) * h / h2 - 0.5, 0, h - 1)
    xs = np.clip((np.arange(w2) + 0.5) * w / w2 - 0.5, 0, w - 1)
    y0 = np.floor(ys).astype(int)
    x0 = np.floor(xs).astype(int)
    y1 = np.minimum(y0 + 1, h - 1)
    x1 = np.minimum(x0 + 1, w - 1)
    fy = (ys - y0)[:, None, None]
    fx = (xs - x0)[None, :, None]
    return (a[y0][:, x0] * (1 - fy) * (1 - fx) + a[y0][:, x1] * (1 - fy) * fx + a[y1][:, x0] * fy * (1 - fx) + a[y1][:, x1] * fy * fx)


def push_pull_fill(image, valid):
    """Fills the texels that are not valid smoothly from the ones that are (push-pull: average down a pyramid, then fill each level from the coarser). image (h, w, 3), valid (h, w)."""
    if valid.all():
        return image
    levels = []
    img, wgt = image * valid[..., None], valid.astype(np.float64)
    while True:
        levels.append((img, wgt))
        h, w = wgt.shape
        if h <= 1 and w <= 1:
            break
        ph, pw = h + (h % 2), w + (w % 2)
        img = np.pad(img, ((0, ph - h), (0, pw - w), (0, 0)))
        wgt = np.pad(wgt, ((0, ph - h), (0, pw - w)))
        img = img.reshape(ph // 2, 2, pw // 2, 2, 3).sum(axis=(1, 3))
        wgt = wgt.reshape(ph // 2, 2, pw // 2, 2).sum(axis=(1, 3))
    # fill from the coarsest level up
    fill = None
    for img, wgt in reversed(levels):
        with np.errstate(invalid="ignore", divide="ignore"):
            here = np.where(wgt[..., None] > 0, img / np.maximum(wgt[..., None], 1e-12), 0.0)
        if fill is not None:
            up = upsample2x(fill, here.shape[:2])
            here = np.where(wgt[..., None] > 0, here, up)
        fill = here
    return np.where(valid[..., None], image, fill)


def face_points(box_min, box_max, spec, width, height):
    """The world points (in the Scene Builder's frame) of a picture of width x height texels laid on the visible face of a slab: texel (column, row) <-> u = column / (width - 1), v = 1 - row / (height - 1)."""
    axis, sign = spec["face"]
    lo, hi = np.asarray(box_min, float), np.asarray(box_max, float)
    cols, rows = np.meshgrid(np.arange(width), np.arange(height))
    u = cols / max(width - 1, 1)
    v = 1.0 - rows / max(height - 1, 1)
    pts = np.zeros((height, width, 3))
    for t, (a, s) in ((u, spec["u"]), (v, spec["v"])):
        # u (or v) = 0 is at the low end of axis a when s = +1, the high end when s = -1
        frac = t if s > 0 else 1.0 - t
        pts[..., a] = lo[a] + frac * (hi[a] - lo[a])
    pts[..., axis] = hi[axis] if sign > 0 else lo[axis]
    return pts


def surface_texture(model, box_min, box_max, spec, exposure=1.0, texels_per_metre=180, max_size=1024, min_seen=0.15, check_depth=True):
    """A picture of one surface from the photo: (uint8 RGB array, the fraction of it the photo actually showed), or (None, fraction) when the photo showed too little of it.
    box_min / box_max: the slab in the Scene Builder's frame (x mirrored, as room_fit's result has it)."""
    lo, hi = np.asarray(box_min, float), np.asarray(box_max, float)
    ua, va = spec["u"][0], spec["v"][0]
    size_u, size_v = float(hi[ua] - lo[ua]), float(hi[va] - lo[va])
    width = int(min(max_size, max(16, round(size_u * texels_per_metre))))
    height = int(min(max_size, max(16, round(size_v * texels_per_metre))))
    pts = face_points(lo, hi, spec, width, height).reshape(-1, 3)
    pts_photo = pts.copy()
    pts_photo[:, 0] = -pts_photo[:, 0]   # the photo's frame has x to the right: undo the Scene Builder's mirror
    u, v, zc = project(model, pts_photo)
    colour, good = sample(model, u, v, zc, check_depth=check_depth)
    seen = float(good.mean())
    if seen < min_seen:
        return None, seen
    image = colour.reshape(height, width, 3)
    valid = good.reshape(height, width)
    image = push_pull_fill(image, valid)
    return image, seen


def to_reflectance_png(display_rgb, exposure):
    """The sampled picture (display RGB 0..1) as 8-bit sRGB brightened by the room's exposure (linear light, hue kept), like room_objects.relight_texture."""
    import room_objects
    return room_objects.relight_texture((np.clip(display_rgb, 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8), exposure)
