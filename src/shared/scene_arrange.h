#pragma once
// scene_arrange.h -- the Scene Builder's tidying tools on a Document: hide and lock items (so what is behind them can be seen and edited, and what is finished is not moved by
// accident), put things down on the floor, and measure what is picked (for Frame selected). Like scene_selection.h every function changes only the Document it is given and
// does nothing else (no selection, no undo): the GUI wraps one call in one undo step. std-only, so the unit tests and the GUI share it.
//
// Hidden and locked are about the Scene Builder's VIEWS only. A hidden object is not drawn in the layout and 3D views and cannot be clicked there, but it is still in the scene
// and still renders (it is how a ceiling is lifted out of the way to look at the room). A locked item is drawn, but clicking and dragging in the views do not pick or move it,
// and the arrow keys leave it; it can still be picked in the list and edited in the properties. Neither is written into the .pbrt text: they live in the builder's own copy
// of the document.

#include "scene_selection.h"
#include "scene_snap.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace scene_doc {

// Hides or shows the items; returns how many changed.
inline int setHidden(Document& d, const ItemSet& wanted, bool hidden) {
	const ItemSet s = makeItemSet(d, wanted.objects, wanted.lights);
	int changed = 0;
	for (int i : s.objects)
		if (d.objects[i].hidden != hidden) { d.objects[i].hidden = hidden; ++changed; }
	for (int i : s.lights)
		if (d.lights[i].hidden != hidden) { d.lights[i].hidden = hidden; ++changed; }
	return changed;
}

// Locks or unlocks the items; returns how many changed.
inline int setLocked(Document& d, const ItemSet& wanted, bool locked) {
	const ItemSet s = makeItemSet(d, wanted.objects, wanted.lights);
	int changed = 0;
	for (int i : s.objects)
		if (d.objects[i].locked != locked) { d.objects[i].locked = locked; ++changed; }
	for (int i : s.lights)
		if (d.lights[i].locked != locked) { d.lights[i].locked = locked; ++changed; }
	return changed;
}

// Everything hidden is shown again / everything locked is unlocked; returns how many changed.
inline int showAll(Document& d) {
	int n = 0;
	for (Object& o : d.objects) if (o.hidden) { o.hidden = false; ++n; }
	for (Light& l : d.lights) if (l.hidden) { l.hidden = false; ++n; }
	return n;
}
inline int unlockAll(Document& d) {
	int n = 0;
	for (Object& o : d.objects) if (o.locked) { o.locked = false; ++n; }
	for (Light& l : d.lights) if (l.locked) { l.locked = false; ++n; }
	return n;
}
inline int hiddenCount(const Document& d) {
	int n = 0;
	for (const Object& o : d.objects) n += o.hidden ? 1 : 0;
	for (const Light& l : d.lights) n += l.hidden ? 1 : 0;
	return n;
}
inline int lockedCount(const Document& d) {
	int n = 0;
	for (const Object& o : d.objects) n += o.locked ? 1 : 0;
	for (const Light& l : d.lights) n += l.locked ? 1 : 0;
	return n;
}

// Puts the picked items down on the floor (the plane y = 0): moves them all, together, by what takes the lowest point of the lowest object to y = 0 (so a table and its
// legs, or a set picked together, keep their places relative to one another; one object resting on another comes down with it). The size of a mesh file is not in the
// document, so a mesh does not count in finding the lowest point (but it is moved with the rest); a pick of meshes and lights alone has nothing to measure and is left.
// Returns false when nothing moved (nothing to measure, or it already stands on the floor).
inline bool dropToFloor(Document& d, const ItemSet& wanted) {
	const ItemSet s = makeItemSet(d, wanted.objects, wanted.lights);
	double lowest = std::numeric_limits<double>::infinity();
	for (int i : s.objects) {
		const Object& o = d.objects[i];
		if (o.shape == ShapeKind::Mesh) continue;
		const Aabb box = aabbOf(o);
		if (box.valid) lowest = std::min(lowest, box.low(1));
	}
	if (!std::isfinite(lowest) || std::fabs(lowest) < 1e-9) return false;
	translateItems(d, s, Float3{0.0, -lowest, 0.0});
	return true;
}

// The box round the picked items, for Frame selected: each object's box (a mesh, whose size is not known, counts as a box of its scale round its position) and each light's
// place. False for an empty pick.
inline bool boundsOf(const Document& d, const ItemSet& wanted, Float3& lo, Float3& hi) {
	const ItemSet s = makeItemSet(d, wanted.objects, wanted.lights);
	bool any = false;
	const auto grow = [&](const Float3& p) {
		if (!any) { lo = hi = p; any = true; return; }
		lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
		hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
	};
	for (int i : s.objects) {
		const Object& o = d.objects[i];
		if (o.shape == ShapeKind::Mesh) {
			const double r = boundingRadius(o);
			grow({o.position.x - r, o.position.y - r, o.position.z - r});
			grow({o.position.x + r, o.position.y + r, o.position.z + r});
			continue;
		}
		const Aabb box = aabbOf(o);
		if (box.valid) { grow(box.lo); grow(box.hi); }
	}
	for (int i : s.lights) grow(d.lights[i].position);
	return any;
}

// Stands the picked items on the ground at `point` (a click on a photo's ground in the camera view): moves them all, together, so that the middle of the box round them (sideways) is at
// the point and the lowest point of the lowest object is at the point's height. A pick of meshes and lights alone has nothing to measure from below, so it is moved sideways only.
// Returns false for an empty pick.
inline bool placeOnGround(Document& d, const ItemSet& wanted, const Float3& point) {
	const ItemSet s = makeItemSet(d, wanted.objects, wanted.lights);
	Float3 lo, hi;
	if (!boundsOf(d, s, lo, hi)) return false;
	translateItems(d, s, Float3{point.x - 0.5 * (lo.x + hi.x), 0.0, point.z - 0.5 * (lo.z + hi.z)});
	double lowest = std::numeric_limits<double>::infinity();
	for (int i : s.objects) {
		const Object& o = d.objects[i];
		if (o.shape == ShapeKind::Mesh) continue;
		const Aabb box = aabbOf(o);
		if (box.valid) lowest = std::min(lowest, box.low(1));
	}
	if (std::isfinite(lowest)) translateItems(d, s, Float3{0.0, point.y - lowest, 0.0});
	return true;
}

// How tall the picked items are together (the box round them), in scene units; 0 for an empty pick.
inline double heightOfItems(const Document& d, const ItemSet& wanted) {
	Float3 lo, hi;
	return boundsOf(d, wanted, lo, hi) ? hi.y - lo.y : 0.0;
}

// Makes the picked items `metres` tall together, by scaling them uniformly about the middle of their base (so they stay where they stand). False for an empty or flat pick or a size
// that scaleItems refuses (a change of more than 100 times either way).
inline bool setHeightOfItems(Document& d, const ItemSet& wanted, double metres) {
	const ItemSet s = makeItemSet(d, wanted.objects, wanted.lights);
	Float3 lo, hi;
	if (!boundsOf(d, s, lo, hi) || !(metres > 0.0) || !(hi.y - lo.y > 1e-9)) return false;
	return scaleItems(d, s, metres / (hi.y - lo.y), Float3{0.5 * (lo.x + hi.x), lo.y, 0.5 * (lo.z + hi.z)});
}

}  // namespace scene_doc
