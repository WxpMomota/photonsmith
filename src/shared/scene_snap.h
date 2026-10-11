#pragma once
// scene_snap.h -- "snap to other objects" for the Scene Builder's drags: while something is dragged, its edges and middle line up with the edges and middles of the
// objects around it, and it rests on top of them or on the floor, the way smart guides work in a drawing program. Everything here is plain geometry on a Document, so
// the views, the widget and the unit tests share it; std-only, like scene_document.h.
//
// An object's box (aabbOf) is the smallest axis-aligned box round it in world space: exact for a sphere, a box, a quad and the generated shapes (taken from their
// triangles), and the box round the turned box for a cylinder, cone and disk. A mesh file's size is not in the document, so a mesh counts as a point.
//
// snapMove() takes the move the drag would make and the axes the drag changes, and nudges the move so that, on each of those axes, the moving set's box lines up with a
// nearby object's box: its low side, middle or high side with theirs (the same side, or touching: my low side to their high side and the other way round; a middle only
// with a middle). It picks the nearest match within the tolerance (a distance in world units: the views give a few pixels' worth) and says what it matched, so the views can
// draw it. The floor (the plane y = 0) is a target for the moving set's lowest point.

#include "scene_document.h"
#include "scene_view_math.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace scene_doc {

struct Aabb {
	Float3 lo{}, hi{};
	bool valid = false;
	double centre(int axis) const { return 0.5 * (value(lo, axis) + value(hi, axis)); }
	static double value(const Float3& p, int axis) { return axis == 0 ? p.x : axis == 1 ? p.y : p.z; }
	double low(int axis) const { return value(lo, axis); }
	double high(int axis) const { return value(hi, axis); }
};

namespace snap_detail {

inline void grow(Aabb& b, const Float3& p) {
	if (!b.valid) {
		b.lo = b.hi = p;
		b.valid = true;
		return;
	}
	b.lo = {std::min(b.lo.x, p.x), std::min(b.lo.y, p.y), std::min(b.lo.z, p.z)};
	b.hi = {std::max(b.hi.x, p.x), std::max(b.hi.y, p.y), std::max(b.hi.z, p.z)};
}

// A point of the object's own frame, put in the world: turned by its three angles (about X, then Y, then Z), then moved to its place.
inline Float3 toWorld(const Object& o, const scene_view::Mat3& turn, double x, double y, double z) {
	const scene_view::V3 r = turn * scene_view::V3{x, y, z};
	return {o.position.x + r.x, o.position.y + r.y, o.position.z + r.z};
}

// Half the box round the shape in its own frame, for the shapes that are not generated meshes.
inline Float3 halfExtents(const Object& o) {
	switch (o.shape) {
		case ShapeKind::Sphere: return {o.radius, o.radius, o.radius};
		case ShapeKind::Box:
		case ShapeKind::Wedge:
		case ShapeKind::Stairs: return {0.5 * o.size.x, 0.5 * o.size.y, 0.5 * o.size.z};
		case ShapeKind::Quad: return {0.5 * o.size.x, 0.0, 0.5 * o.size.z};
		case ShapeKind::Disk: return {o.radius, 0.0, o.radius};
		case ShapeKind::Cylinder:
		case ShapeKind::Cone: return {o.radius, 0.5 * o.height, o.radius};
		default: return {0.0, 0.0, 0.0};   // a mesh file: its size is not known here
	}
}

}  // namespace snap_detail

// The box round an object in world space (valid = false never happens for an Object: a mesh is a point at its place).
inline Aabb aabbOf(const Object& o) {
	using namespace snap_detail;
	Aabb b;
	const scene_view::Mat3 turn = scene_view::rotationXYZ({o.rotation.x, o.rotation.y, o.rotation.z});
	if (o.shape == ShapeKind::Sphere) {   // a sphere's box does not turn
		const double r = std::fabs(o.radius);
		grow(b, {o.position.x - r, o.position.y - r, o.position.z - r});
		grow(b, {o.position.x + r, o.position.y + r, o.position.z + r});
		return b;
	}
	if (isGeneratedShape(o.shape)) {
		const ShapeMesh mesh = generatedMesh(o);
		for (std::size_t i = 0; i + 2 < mesh.P.size(); i += 3) grow(b, toWorld(o, turn, mesh.P[i], mesh.P[i + 1], mesh.P[i + 2]));
		if (b.valid) return b;
	}
	const Float3 h = halfExtents(o);
	for (int sx = -1; sx <= 1; sx += 2)
		for (int sy = -1; sy <= 1; sy += 2)
			for (int sz = -1; sz <= 1; sz += 2) grow(b, toWorld(o, turn, sx * h.x, sy * h.y, sz * h.z));
	return b;
}

// The box round several objects (the ones at `indices`, which must be in range).
inline Aabb aabbOfObjects(const Document& d, const std::vector<int>& indices) {
	Aabb all;
	for (int i : indices) {
		if (i < 0 || i >= static_cast<int>(d.objects.size())) continue;
		const Aabb b = aabbOf(d.objects[static_cast<std::size_t>(i)]);
		snap_detail::grow(all, b.lo);
		snap_detail::grow(all, b.hi);
	}
	return all;
}

// What one axis of a snap matched: which side of the moving box (0 low, 1 middle, 2 high), which object (an index, or -1 for the floor) and which side of it, and where.
struct SnapGuide {
	int axis = 0;
	double value = 0.0;   // the coordinate they now share on `axis`
	int object = -1;
	int mine = 0;
	int theirs = 0;
};

struct SnapResult {
	Float3 delta{};                   // the move to make: the one asked for, nudged on the axes that snapped
	std::vector<SnapGuide> guides;    // one per axis that snapped
};

// `delta` is the move a drag asks of the objects at `moving`; `active` says which axes the drag changes (the others are left exactly as asked); `tolerance` is how
// far, in world units, a side may be from another to snap to it. Objects not in `moving` are the targets. Nothing snaps for an empty set.
inline SnapResult snapMove(const Document& d, const std::vector<int>& moving, const Float3& delta, const bool active[3], double tolerance, bool floorToo = true) {
	SnapResult r;
	r.delta = delta;
	const Aabb box = aabbOfObjects(d, moving);
	if (!box.valid || !(tolerance > 0.0)) return r;
	std::vector<char> isMoving(d.objects.size(), 0);
	for (int i : moving)
		if (i >= 0 && i < static_cast<int>(d.objects.size())) isMoving[static_cast<std::size_t>(i)] = 1;
	std::vector<Aabb> targets(d.objects.size());
	for (std::size_t i = 0; i < d.objects.size(); ++i)
		if (!isMoving[i]) targets[i] = aabbOf(d.objects[i]);

	for (int axis = 0; axis < 3; ++axis) {
		if (!active[axis]) continue;
		const double move = Aabb::value(delta, axis);
		const double mine[3] = {box.low(axis) + move, box.centre(axis) + move, box.high(axis) + move};
		double best = tolerance + 1.0, bestShift = 0.0;
		bool contact = false;   // the best match so far is a touch (low to high), which wins a tie
		SnapGuide guide;
		const auto consider = [&](double theirs, int mySide, int theirSide, int object, bool touch) {
			const double shift = theirs - mine[mySide];
			const double gap = std::fabs(shift);
			if (gap > tolerance) return;
			if (gap < best - 1e-12 || (std::fabs(gap - best) <= 1e-12 && touch && !contact)) {
				best = gap;
				bestShift = shift;
				contact = touch;
				guide = {axis, theirs, object, mySide, theirSide};
			}
		};
		for (std::size_t i = 0; i < targets.size(); ++i) {
			const Aabb& t = targets[i];
			if (!t.valid) continue;
			const int object = static_cast<int>(i);
			consider(t.low(axis), 0, 0, object, false);          // my low side with their low side: flush
			consider(t.high(axis), 2, 2, object, false);         // high with high
			consider(t.centre(axis), 1, 1, object, false);       // middle with middle: lined up
			consider(t.high(axis), 0, 2, object, true);          // my low side on their high side: resting on or against it
			consider(t.low(axis), 2, 0, object, true);           // my high side against their low side
		}
		if (floorToo && axis == 1) consider(0.0, 0, 0, -1, true);   // the lowest point on the floor
		if (best <= tolerance) {
			(axis == 0 ? r.delta.x : axis == 1 ? r.delta.y : r.delta.z) = move + bestShift;
			r.guides.push_back(guide);
		}
	}
	return r;
}

}  // namespace scene_doc
