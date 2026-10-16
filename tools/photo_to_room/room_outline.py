"""room_outline.py - a room that is not one rectangle: an L, an alcove, a bay, a chimney breast (numpy only, no model).

room_fit.py first fits a box. When the photo's walls do not sit on that box's four sides, this reads the room's OUTLINE off the walls the photo shows and makes the walls one slab per edge.
Rooms are built of walls along two perpendicular directions (the fit has already turned the room square), so the outline is rectilinear:

  1. every image column gives one boundary sample: where the wall is in that direction (the far end of the wall points of that column, so furniture in front does not count), and
     whether the wall there faces along x or along z;
  2. samples of the same facing in a row make a run; tiny runs are dropped and neighbours on the same line merge, leaving the walls the photo shows;
  3. the walls are joined into a chain: two runs of different facing meet at a corner; two on parallel lines (a step: an alcove, an L's inner corner) are joined by a wall of the other
     facing at the step;
  4. the ends of the chain are carried to the bounding rectangle (what the photo does not show is taken to run square) and closed round the side behind the camera;
  5. the outline is used only when it explains the photo's boundary much better than the box does, and is a simple polygon around the camera (otherwise the box stays).

Everything is in the room's own frame before the Scene Builder's mirror: x to the right, z along the camera's turned forward, the camera at the origin.
"""
import math

import numpy as np

MIN_RUN_SAMPLES = 3
MIN_RUN_SPAN = 0.3        # a wall shorter than this (metres) is noise or a doorframe
MERGE = 0.15              # two runs on lines closer than this are one wall
MISFIT_TOL = 0.25         # a boundary sample further than this from the nearest wall does not belong to it
USE_OUTLINE_IF_BOX_MISFIT_ABOVE = 0.12
USE_OUTLINE_IF_MISFIT_BELOW = 0.08
MAX_EDGES = 14


def boundary_profile(points, nx, nz, vertical, ceiling_y):
    """One sample per image column: (x, z, facing) of the wall in that direction, or None. facing is 0 for a wall whose normal is along x (the wall is the line x = const), 2 for z, None
    when the normal is slanted (a corner pixel). points (h, w, 3), nx / nz / vertical (h, w)."""
    h, w, _ = points.shape
    out = []
    for c in range(w):
        m = vertical[:, c] & (points[:, c, 1] > 0.25) & (points[:, c, 1] < ceiling_y - 0.15)
        if int(m.sum()) < 3:
            out.append(None)
            continue
        p = points[m, c]
        r = np.hypot(p[:, 0], p[:, 2])
        thr = float(np.percentile(r, 80))
        sel = np.abs(r - thr) < 0.25   # (the far end of the column's wall points: what stands in front of the wall is nearer)
        if int(sel.sum()) < 2:
            out.append(None)
            continue
        x, z = float(np.median(p[sel, 0])), float(np.median(p[sel, 2]))
        a, b = float(np.median(nx[m, c][sel])), float(np.median(nz[m, c][sel]))
        facing = 0 if (abs(a) > 0.6 and abs(a) >= abs(b)) else (2 if abs(b) > 0.6 else None)
        out.append((x, z, facing))
    return out


def _value(run):
    return float(np.median(run["xs"] if run["cls"] == 0 else run["zs"]))


def _span(run):
    other = run["zs"] if run["cls"] == 0 else run["xs"]
    return float(max(other) - min(other))


def runs_from_profile(profile):
    """The walls the photo shows, in the order the columns see them: [{"cls": 0 (x = const) or 2 (z = const), "xs", "zs"}], tiny ones dropped, neighbours on one line merged."""
    runs = []
    for s in profile:
        if s is None or s[2] is None:
            continue
        x, z, cls = s
        if runs and runs[-1]["cls"] == cls:
            runs[-1]["xs"].append(x)
            runs[-1]["zs"].append(z)
        else:
            runs.append({"cls": cls, "xs": [x], "zs": [z]})
    changed = True
    while changed:
        changed = False
        keep = [r for r in runs if len(r["xs"]) >= MIN_RUN_SAMPLES and _span(r) >= MIN_RUN_SPAN]
        if len(keep) != len(runs):
            runs, changed = keep, True
        merged = []
        for r in runs:
            if merged and merged[-1]["cls"] == r["cls"] and abs(_value(merged[-1]) - _value(r)) < MERGE:
                merged[-1]["xs"] += r["xs"]
                merged[-1]["zs"] += r["zs"]
                changed = True
            else:
                merged.append(r)
        runs = merged
    return runs


def chain_from_runs(runs):
    """The open chain of wall points through the runs: [(x, z)...] from the start of the first wall to the end of the last, or None for fewer than two walls."""
    if len(runs) < 2:
        return None

    def line_point(run, x, z):   # a sample projected onto its wall's line
        return (_value(run), z) if run["cls"] == 0 else (x, _value(run))

    chain = [line_point(runs[0], runs[0]["xs"][0], runs[0]["zs"][0])]
    for a, b in zip(runs, runs[1:]):
        if a["cls"] != b["cls"]:
            chain.append((_value(a), _value(b)) if a["cls"] == 0 else (_value(b), _value(a)))   # the corner: x from the x-wall, z from the z-wall
        elif a["cls"] == 0:   # two walls x = const: a step, joined by a wall z = const where the view passes from one to the other
            zt = 0.5 * (a["zs"][-1] + b["zs"][0])
            chain += [(_value(a), zt), (_value(b), zt)]
        else:
            xt = 0.5 * (a["xs"][-1] + b["xs"][0])
            chain += [(xt, _value(a)), (xt, _value(b))]
    last = runs[-1]
    chain.append(line_point(last, last["xs"][-1], last["zs"][-1]))
    return chain


def _extend(p, d, rect):
    """p carried along the axis direction d to the rectangle's side (xlo, xhi, zlo, zhi)."""
    xlo, xhi, zlo, zhi = rect
    if abs(d[0]) < 1e-9:
        return (p[0], zhi if d[1] > 0 else zlo)
    return (xhi if d[0] > 0 else xlo, p[1])


def _unit_axis(a, b):
    dx, dz = b[0] - a[0], b[1] - a[1]
    if abs(dx) >= abs(dz):
        return (math.copysign(1.0, dx) if dx else 1.0, 0.0)
    return (0.0, math.copysign(1.0, dz) if dz else 1.0)


def _perimeter(p, rect):
    xlo, xhi, zlo, zhi = rect
    w, h = xhi - xlo, zhi - zlo
    tol = 1e-6
    if abs(p[1] - zlo) < tol:
        return p[0] - xlo
    if abs(p[0] - xhi) < tol:
        return w + (p[1] - zlo)
    if abs(p[1] - zhi) < tol:
        return w + h + (xhi - p[0])
    return 2 * w + h + (zhi - p[1])


def _arc(start, end, rect, ccw):
    """The corners passed going round the rectangle from start to end (inclusive of both), counter-clockwise or clockwise."""
    xlo, xhi, zlo, zhi = rect
    w, h = xhi - xlo, zhi - zlo
    corners = [((xlo, zlo), 0.0), ((xhi, zlo), w), ((xhi, zhi), w + h), ((xlo, zhi), 2 * w + h)]
    total = 2 * (w + h)
    s0, s1 = _perimeter(start, rect), _perimeter(end, rect)
    pts = [start]
    if ccw:
        span = (s1 - s0) % total
        for corner, s in sorted(corners, key=lambda cs: (cs[1] - s0) % total):
            if 1e-6 < (s - s0) % total < span - 1e-6:
                pts.append(corner)
    else:
        span = (s0 - s1) % total
        for corner, s in sorted(corners, key=lambda cs: (s0 - cs[1]) % total):
            if 1e-6 < (s0 - s) % total < span - 1e-6:
                pts.append(corner)
    pts.append(end)
    return pts


def _distance_to_polyline(p, poly):
    best = 1e30
    for a, b in zip(poly, poly[1:]):
        ax, az, bx, bz = a[0], a[1], b[0], b[1]
        dx, dz = bx - ax, bz - az
        t = 0.0 if dx == 0 and dz == 0 else max(0.0, min(1.0, ((p[0] - ax) * dx + (p[1] - az) * dz) / (dx * dx + dz * dz)))
        best = min(best, math.hypot(p[0] - (ax + t * dx), p[1] - (az + t * dz)))
    return best


def close_chain(chain, rect, back):
    """The closed outline: the chain's ends carried to the rectangle, joined round the side `back` (a direction, x and z, from the camera to what it cannot see) points to. rect is
    (xlo, xhi, zlo, zhi) and holds the chain."""
    start = _extend(chain[0], tuple(-v for v in _unit_axis(chain[0], chain[1])), rect)
    end = _extend(chain[-1], _unit_axis(chain[-2], chain[-1]), rect)
    # the point on the rectangle in the direction `back`
    xlo, xhi, zlo, zhi = rect
    bx, bz = back
    ts = []
    if abs(bx) > 1e-9:
        ts.append(((xhi if bx > 0 else xlo) / bx))
    if abs(bz) > 1e-9:
        ts.append(((zhi if bz > 0 else zlo) / bz))
    t = min(t for t in ts if t > 0) if any(t > 0 for t in ts) else 1.0
    behind = (bx * t, bz * t)
    arcs = []
    for ccw in (True, False):
        arc = _arc(end, start, rect, ccw)
        arcs.append((_distance_to_polyline(behind, arc), arc))
    arc = min(arcs, key=lambda a: a[0])[1]
    # start (the first wall carried back to the rectangle) -> the chain -> end (the last wall carried on) -> the rectangle's corners behind the camera -> back to start
    full = ([start] if math.hypot(start[0] - chain[0][0], start[1] - chain[0][1]) > 1e-9 else []) + list(chain)
    if math.hypot(end[0] - chain[-1][0], end[1] - chain[-1][1]) > 1e-9:
        full.append(end)
    full += arc[1:-1]
    return full


def clean_polygon(poly):
    """The polygon without repeated points and without a vertex in the middle of a straight run."""
    pts = []
    for p in poly:
        if not pts or math.hypot(p[0] - pts[-1][0], p[1] - pts[-1][1]) > 1e-6:
            pts.append(p)
    if len(pts) > 1 and math.hypot(pts[0][0] - pts[-1][0], pts[0][1] - pts[-1][1]) < 1e-6:
        pts.pop()
    changed = True
    while changed and len(pts) > 3:
        changed = False
        for i in range(len(pts)):
            a, b, c = pts[i - 1], pts[i], pts[(i + 1) % len(pts)]
            if abs((b[0] - a[0]) * (c[1] - b[1]) - (b[1] - a[1]) * (c[0] - b[0])) < 1e-9:
                del pts[i]
                changed = True
                break
    return pts


def signed_area(poly):
    return 0.5 * sum(a[0] * b[1] - b[0] * a[1] for a, b in zip(poly, poly[1:] + poly[:1]))


def _segments_cross(p1, p2, p3, p4):
    def orient(a, b, c):
        return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])
    d1, d2, d3, d4 = orient(p3, p4, p1), orient(p3, p4, p2), orient(p1, p2, p3), orient(p1, p2, p4)
    return (d1 * d2 < -1e-12) and (d3 * d4 < -1e-12)


def is_simple(poly):
    n = len(poly)
    for i in range(n):
        for j in range(i + 1, n):
            if abs(i - j) <= 1 or (i == 0 and j == n - 1):
                continue
            if _segments_cross(poly[i], poly[(i + 1) % n], poly[j], poly[(j + 1) % n]):
                return False
    return True


def contains(poly, p):
    inside = False
    for a, b in zip(poly, poly[1:] + poly[:1]):
        if (a[1] > p[1]) != (b[1] > p[1]) and p[0] < a[0] + (p[1] - a[1]) * (b[0] - a[0]) / (b[1] - a[1]):
            inside = not inside
    return inside


def misfit(samples, polyline_closed):
    """The fraction of boundary samples (x, z) further than MISFIT_TOL from the polygon's nearest edge."""
    if not samples:
        return 0.0
    poly = list(polyline_closed) + [polyline_closed[0]]
    far = sum(1 for s in samples if _distance_to_polyline(s, poly) > MISFIT_TOL)
    return far / float(len(samples))


def fit_outline(profile, box, back):
    """The room's outline when it is not the box. profile from boundary_profile; box (xmin, xmax, zmin, zmax) the rectangle already fitted; back the direction from the camera to what
    it cannot see. Returns {"polygon": [(x, z)...] counter-clockwise, "box_misfit", "misfit", "walls": n} or None (keep the box)."""
    samples = [(s[0], s[1]) for s in profile if s is not None and s[2] is not None]
    if len(samples) < 12:
        return None
    xmin, xmax, zmin, zmax = box
    rect_poly = [(xmin, zmin), (xmax, zmin), (xmax, zmax), (xmin, zmax)]
    box_misfit = misfit(samples, rect_poly)
    if box_misfit <= USE_OUTLINE_IF_BOX_MISFIT_ABOVE:
        return None
    runs = runs_from_profile(profile)
    chain = chain_from_runs(runs)
    if chain is None:
        return None
    xs = [p[0] for p in chain] + [xmin, xmax, 0.0]
    zs = [p[1] for p in chain] + [zmin, zmax, 0.0]
    rect = (min(xs), max(xs), min(zs), max(zs))
    poly = clean_polygon(close_chain(chain, rect, back))
    if len(poly) < 5 or len(poly) > MAX_EDGES or len(poly) % 2:
        return None
    if not is_simple(poly) or not contains(poly, (0.0, 0.0)):
        return None
    if signed_area(poly) < 0:
        poly = poly[::-1]
    m = misfit(samples, poly)
    if m > USE_OUTLINE_IF_MISFIT_BELOW or m > 0.5 * box_misfit:
        return None
    for a, b in zip(poly, poly[1:] + poly[:1]):
        if math.hypot(b[0] - a[0], b[1] - a[1]) < 0.2:
            return None
    return {"polygon": [(float(a), float(b)) for a, b in poly], "box_misfit": box_misfit, "misfit": m, "walls": len(poly)}


def walls_from_polygon(poly, thickness):
    """The walls of a counter-clockwise outline: [{"axis" (0: the wall is x = value, 2: z = value), "value", "lo", "hi" (its extent along the other axis), "side" (+1: the room is at larger
    coordinates than the wall), "ext_lo", "ext_hi" (how far its slab runs past each end: the thickness at a convex corner, nothing at a reflex one, where it would poke into the room)}]."""
    n = len(poly)
    walls = []
    for i in range(n):
        a, b, c = poly[i], poly[(i + 1) % n], poly[(i + 2) % n]
        prev = poly[i - 1]
        dx, dz = b[0] - a[0], b[1] - a[1]
        if abs(dx) < 1e-9:       # along z: the wall x = const; the room is on the left of the direction of travel, (-dz, dx)
            axis, value, lo, hi, side = 0, a[0], min(a[1], b[1]), max(a[1], b[1]), (1 if -dz > 0 else -1)
        else:
            axis, value, lo, hi, side = 2, a[1], min(a[0], b[0]), max(a[0], b[0]), (1 if dx > 0 else -1)

        def convex(p, q, r):   # a left turn p -> q -> r is convex for a counter-clockwise outline
            return (q[0] - p[0]) * (r[1] - q[1]) - (q[1] - p[1]) * (r[0] - q[0]) > 0
        start_convex, end_convex = convex(prev, a, b), convex(a, b, c)
        along_start_is_lo = (a[1] < b[1]) if axis == 0 else (a[0] < b[0])
        lo_convex, hi_convex = (start_convex, end_convex) if along_start_is_lo else (end_convex, start_convex)
        walls.append({"axis": axis, "value": float(value), "lo": float(lo), "hi": float(hi), "side": side,
                      "ext_lo": thickness if lo_convex else 0.0, "ext_hi": thickness if hi_convex else 0.0})
    return walls


def polygon_area(poly):
    return abs(signed_area(poly))
