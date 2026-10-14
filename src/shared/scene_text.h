// 3D text for the Scene Builder: letters pulled out into a solid. std-only, like the other generated shapes (scene_shapes.h), so the unit tests and the pbrt writer share it with
// the GUI.
//
// The letters' outlines come from a provider the GUI sets (Qt turns a font into outlines: scene_builder_text.cpp); this file turns outlines into a mesh:
//   * triangulateOutlines(): flat triangles for the faces. Outlines may nest (the hole of an "O", an island inside a hole) and may come in any winding, so each is classified by
//     how many others contain it; a polygon with holes is cut into triangles by ear clipping after the holes are bridged to the outer outline (the algorithm of mapbox's earcut).
//   * textMesh(): the front and back faces plus a wall along every outline, with smooth shading round curves and sharp edges at corners.
//
// The mesh stands in the XY plane (letters read left to right along +X, up is +Y), the front faces +Z (what the Builder's default camera looks at), and it is centred on its
// bounding box, like every other shape.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <limits>
#include <mutex>
#include <string>
#include <vector>

#include "scene_shapes.h"

namespace scene_doc {

// What the outline provider is asked for. `align`: 0 left, 1 centre, 2 right (for more than one line). An empty `font` means the application's own.
struct TextParams {
	std::string text = "Text";
	std::string font;
	bool bold = false;
	bool italic = false;
	int align = 1;
};

using TextPoint = std::array<double, 2>;
using TextContour = std::vector<TextPoint>;   // one closed outline (the last point joins the first); the winding does not matter

struct TextOutline {
	std::vector<TextContour> contours;   // x to the right, y up, in whatever unit the provider likes
	double capHeight = 0.0;              // the height of a capital letter in that unit (0: the outlines' own height stands for it)
};

// Fills `out` with the outlines of `p.text` laid out in one block (lines under each other, aligned as asked). Returns false if it cannot (the GUI sets this; nothing else does).
using TextOutlineProvider = bool (*)(const TextParams& p, TextOutline& out);
inline TextOutlineProvider& textOutlineProviderSlot() {
	static TextOutlineProvider provider = nullptr;
	return provider;
}

// The last few text meshes made (see textMesh()); a new provider empties it.
struct TextMeshCache {
	std::mutex mutex;
	std::vector<std::pair<std::string, ShapeMesh>> recent;
};
inline TextMeshCache& textMeshCache() {
	static TextMeshCache cache;
	return cache;
}
inline void setTextOutlineProvider(TextOutlineProvider provider) {
	textOutlineProviderSlot() = provider;
	TextMeshCache& cache = textMeshCache();
	std::lock_guard<std::mutex> lock(cache.mutex);
	cache.recent.clear();
}

constexpr int kMaxTextLength = 400;   // characters (a longer text is cut): the mesh is written into the scene file

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Triangulation
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------

namespace text_detail {

struct Node {
	double x = 0, y = 0;
	int i = 0;                // the index of the point in the face's point list (a bridge makes two nodes of one point)
	int prev = -1, next = -1;
};

inline double triArea(double ax, double ay, double bx, double by, double cx, double cy) { return (by - ay) * (cx - bx) - (bx - ax) * (cy - by); }

inline double signedArea(const TextContour& c) {
	double sum = 0;
	for (std::size_t i = 0, j = c.size() - 1; i < c.size(); j = i++) sum += (c[j][0] - c[i][0]) * (c[i][1] + c[j][1]);
	return sum / 2;   // counter-clockwise (seen with y up) is positive
}

inline bool pointInContour(const TextContour& c, double x, double y) {   // even-odd
	bool inside = false;
	for (std::size_t i = 0, j = c.size() - 1; i < c.size(); j = i++) {
		const double xi = c[i][0], yi = c[i][1], xj = c[j][0], yj = c[j][1];
		if (((yi > y) != (yj > y)) && (x < (xj - xi) * (y - yi) / (yj - yi) + xi)) inside = !inside;
	}
	return inside;
}

struct Ring {
	std::vector<Node> nodes;

	// A doubly linked ring over `pts[idx...]`, wound the way asked (counter-clockwise for an outer outline, clockwise for a hole). Returns the ring's first node.
	int link(const std::vector<TextPoint>& pts, const std::vector<int>& idx, bool counterClockwise) {
		double sum = 0;
		for (std::size_t i = 0, j = idx.size() - 1; i < idx.size(); j = i++) sum += (pts[static_cast<std::size_t>(idx[j])][0] - pts[static_cast<std::size_t>(idx[i])][0]) *
		                                                                          (pts[static_cast<std::size_t>(idx[i])][1] + pts[static_cast<std::size_t>(idx[j])][1]);
		const bool ccw = sum / 2 > 0;
		int last = -1, first = -1;
		const auto add = [&](int k) {
			Node n;
			n.x = pts[static_cast<std::size_t>(k)][0];
			n.y = pts[static_cast<std::size_t>(k)][1];
			n.i = k;
			nodes.push_back(n);
			const int id = static_cast<int>(nodes.size()) - 1;
			if (first < 0) first = id;
			if (last >= 0) { nodes[static_cast<std::size_t>(last)].next = id; nodes[static_cast<std::size_t>(id)].prev = last; }
			last = id;
		};
		if (ccw == counterClockwise) for (std::size_t k = 0; k < idx.size(); ++k) add(idx[k]);
		else for (std::size_t k = idx.size(); k-- > 0;) add(idx[k]);
		nodes[static_cast<std::size_t>(last)].next = first;
		nodes[static_cast<std::size_t>(first)].prev = last;
		return first;
	}

	Node& at(int id) { return nodes[static_cast<std::size_t>(id)]; }
	const Node& at(int id) const { return nodes[static_cast<std::size_t>(id)]; }

	static bool same(const Node& a, const Node& b) { return a.x == b.x && a.y == b.y; }

	// Drops duplicated and collinear points. Returns a node still in the ring (or -1 if nothing is left).
	int filterPoints(int start, int end = -1) {
		if (start < 0) return start;
		if (end < 0) end = start;
		int p = start;
		bool again;
		do {
			again = false;
			const int prev = at(p).prev, next = at(p).next;
			if (same(at(p), at(next)) || triArea(at(prev).x, at(prev).y, at(p).x, at(p).y, at(next).x, at(next).y) == 0) {
				at(prev).next = next;
				at(next).prev = prev;
				p = end = prev;
				if (p == at(p).next) return -1;
				again = true;
			} else {
				p = next;
			}
		} while (again || p != end);
		return end;
	}

	static bool pointInTriangle(double ax, double ay, double bx, double by, double cx, double cy, double px, double py) {
		return (cx - px) * (ay - py) >= (ax - px) * (cy - py) && (ax - px) * (by - py) >= (bx - px) * (ay - py) && (bx - px) * (cy - py) >= (cx - px) * (by - py);
	}

	bool isEar(int ear) const {
		const Node& a = at(at(ear).prev);
		const Node& b = at(ear);
		const Node& c = at(at(ear).next);
		if (triArea(a.x, a.y, b.x, b.y, c.x, c.y) >= 0) return false;   // a reflex corner
		const double x0 = std::min({a.x, b.x, c.x}), y0 = std::min({a.y, b.y, c.y}), x1 = std::max({a.x, b.x, c.x}), y1 = std::max({a.y, b.y, c.y});
		for (int p = c.next; p != at(ear).prev; p = at(p).next) {
			const Node& q = at(p);
			if (q.x >= x0 && q.x <= x1 && q.y >= y0 && q.y <= y1 && !(q.x == a.x && q.y == a.y) && !(q.x == c.x && q.y == c.y) &&
			    pointInTriangle(a.x, a.y, b.x, b.y, c.x, c.y, q.x, q.y) && triArea(at(q.prev).x, at(q.prev).y, q.x, q.y, at(q.next).x, at(q.next).y) >= 0)
				return false;
		}
		return true;
	}

	// Clips ears off the ring until it is gone. When a whole turn finds no ear (a self-touching outline) the ring is filtered once, and then the corner that turns the most is
	// clipped, so this always ends.
	void earcut(int ear, std::vector<int>& tris) {
		ear = filterPoints(ear);
		if (ear < 0) return;
		int stop = ear, pass = 0;
		while (at(ear).prev != at(ear).next) {
			const int prev = at(ear).prev, next = at(ear).next;
			if (isEar(ear)) {
				tris.push_back(at(prev).i);
				tris.push_back(at(ear).i);
				tris.push_back(at(next).i);
				at(prev).next = next;
				at(next).prev = prev;
				ear = at(next).next;   // (skipping one: the next ear is rarely the neighbour)
				stop = at(next).next;
				pass = 0;
				continue;
			}
			ear = next;
			if (ear != stop) continue;
			if (pass == 0) {
				const int filtered = filterPoints(ear);
				if (filtered < 0) return;
				ear = stop = filtered;
				pass = 1;
				continue;
			}
			int best = ear, p = ear;
			double bestArea = -std::numeric_limits<double>::max();
			do {
				const double a = -triArea(at(at(p).prev).x, at(at(p).prev).y, at(p).x, at(p).y, at(at(p).next).x, at(at(p).next).y);
				if (a > bestArea) { bestArea = a; best = p; }
				p = at(p).next;
			} while (p != ear);
			const int bp = at(best).prev, bn = at(best).next;
			tris.push_back(at(bp).i);
			tris.push_back(at(best).i);
			tris.push_back(at(bn).i);
			at(bp).next = bn;
			at(bn).prev = bp;
			ear = stop = bn;
			pass = 0;
		}
	}

	bool locallyInside(int a, int b) const {
		const Node& na = at(a);
		const Node& nb = at(b);
		const Node& pa = at(na.prev);
		const Node& xa = at(na.next);
		return triArea(pa.x, pa.y, na.x, na.y, xa.x, xa.y) < 0 ? triArea(na.x, na.y, nb.x, nb.y, xa.x, xa.y) >= 0 && triArea(na.x, na.y, pa.x, pa.y, nb.x, nb.y) >= 0
		                                                          : triArea(na.x, na.y, nb.x, nb.y, pa.x, pa.y) < 0 || triArea(na.x, na.y, xa.x, xa.y, nb.x, nb.y) < 0;
	}

	// Where a hole is joined to the outline: the point of the outline the hole's leftmost point can see to the left (earcut's findHoleBridge).
	int findHoleBridge(int hole, int outer) const {
		int p = outer;
		const double hx = at(hole).x, hy = at(hole).y;
		double qx = -std::numeric_limits<double>::infinity();
		int m = -1;
		do {
			const Node& n = at(p);
			const Node& nx = at(n.next);
			if (hy <= n.y && hy >= nx.y && nx.y != n.y) {
				const double x = n.x + (hy - n.y) * (nx.x - n.x) / (nx.y - n.y);
				if (x <= hx && x > qx) {
					qx = x;
					m = n.x < nx.x ? p : n.next;
					if (x == hx) return m;
				}
			}
			p = n.next;
		} while (p != outer);
		if (m < 0) return -1;
		const int stop = m;
		const double mx = at(m).x, my = at(m).y;
		double tanMin = std::numeric_limits<double>::infinity();
		p = m;
		do {
			const Node& n = at(p);
			if (hx >= n.x && n.x >= mx && hx != n.x &&
			    pointInTriangle(hy < my ? hx : qx, hy, mx, my, hy < my ? qx : hx, hy, n.x, n.y)) {
				const double t = std::fabs(hy - n.y) / (hx - n.x);
				if (locallyInside(p, hole) && (t < tanMin || (t == tanMin && (n.x > at(m).x || (n.x == at(m).x && true))))) {
					m = p;
					tanMin = t;
				}
			}
			p = n.next;
		} while (p != stop);
		return m;
	}

	// Joins the hole to the outline with a bridge (two coincident edges), turning outline and hole into one ring.
	int bridge(int a, int b) {
		Node a2 = at(a), b2 = at(b);
		const int na = static_cast<int>(nodes.size());
		nodes.push_back(a2);
		nodes.push_back(b2);
		const int ia = na, ib = na + 1;
		const int an = at(a).next, bp = at(b).prev;
		at(a).next = b;
		at(b).prev = a;
		at(ia).next = an;
		at(an).prev = ia;
		at(ib).next = ia;
		at(ia).prev = ib;
		at(bp).next = ib;
		at(ib).prev = bp;
		return ib;
	}

	int eliminateHoles(const std::vector<int>& holes, int outer) {
		std::vector<int> queue;
		for (int h : holes) {
			int leftmost = h, p = h;
			do {
				if (at(p).x < at(leftmost).x || (at(p).x == at(leftmost).x && at(p).y < at(leftmost).y)) leftmost = p;
				p = at(p).next;
			} while (p != h);
			queue.push_back(leftmost);
		}
		std::sort(queue.begin(), queue.end(), [this](int a, int b) { return at(a).x < at(b).x; });
		for (int hole : queue) {
			const int b = findHoleBridge(hole, outer);
			if (b < 0) continue;   // (a hole outside its outline: left out)
			const int reverse = bridge(b, hole);
			filterPoints(reverse, at(reverse).next);   // (the collinear points round the cuts)
			outer = filterPoints(b, at(b).next);
			if (outer < 0) return -1;
		}
		return outer;
	}
};

}  // namespace text_detail

// A face triangulated: `points` are the cleaned outline points (every contour's, one after another) and `triangles` index them, counter-clockwise.
struct TextFace {
	std::vector<TextPoint> points;
	std::vector<int> triangles;
	std::vector<std::vector<int>> outlines;   // the cleaned outlines as point indices, outer ones counter-clockwise and holes clockwise (the order the walls are made in)
};

// Triangulates a set of closed outlines the way a font fills them: an outline inside an odd number of others is a hole in the one containing it. Degenerate outlines (under
// three points, no area) are dropped.
inline TextFace triangulateOutlines(const std::vector<TextContour>& input) {
	using namespace text_detail;
	TextFace face;
	std::vector<TextContour> cs;
	for (TextContour c : input) {
		TextContour clean;
		for (const TextPoint& p : c)
			if (clean.empty() || p[0] != clean.back()[0] || p[1] != clean.back()[1]) clean.push_back(p);
		while (clean.size() > 1 && clean.front()[0] == clean.back()[0] && clean.front()[1] == clean.back()[1]) clean.pop_back();
		if (clean.size() >= 3 && std::fabs(signedArea(clean)) > 1e-12) cs.push_back(std::move(clean));
	}
	const std::size_t n = cs.size();
	std::vector<int> depth(n, 0), parent(n, -1);
	for (std::size_t i = 0; i < n; ++i) {
		double bestArea = std::numeric_limits<double>::max();
		for (std::size_t j = 0; j < n; ++j) {
			if (i == j) continue;
			// (a point of i that is not on j: the middle of its first edge, which is inside j exactly when the whole of i is, for outlines that do not cross)
			const double px = (cs[i][0][0] + cs[i][1][0]) / 2, py = (cs[i][0][1] + cs[i][1][1]) / 2;
			if (pointInContour(cs[j], px, py) && pointInContour(cs[j], cs[i][0][0], cs[i][0][1]) == pointInContour(cs[j], px, py)) {
				++depth[i];
				const double a = std::fabs(signedArea(cs[j]));
				if (a < bestArea) { bestArea = a; parent[i] = static_cast<int>(j); }
			}
		}
	}
	// Point indices: every cleaned outline's points one after another.
	std::vector<std::vector<int>> ids(n);
	for (std::size_t i = 0; i < n; ++i)
		for (const TextPoint& p : cs[i]) { ids[i].push_back(static_cast<int>(face.points.size())); face.points.push_back(p); }
	for (std::size_t i = 0; i < n; ++i) {
		if (depth[i] % 2 != 0) continue;   // a hole: with its outline below
		Ring ring;
		const int outer = ring.link(face.points, ids[i], true);
		std::vector<int> holes;
		for (std::size_t j = 0; j < n; ++j)
			if (depth[j] % 2 == 1 && parent[j] == static_cast<int>(i)) holes.push_back(ring.link(face.points, ids[j], false));
		int start = outer;
		if (!holes.empty()) start = ring.eliminateHoles(holes, outer);
		if (start >= 0) ring.earcut(start, face.triangles);
	}
	// The outlines wound for the walls: an outer one counter-clockwise, a hole clockwise.
	for (std::size_t i = 0; i < n; ++i) {
		std::vector<int> o = ids[i];
		if ((signedArea(cs[i]) > 0) != (depth[i] % 2 == 0)) std::reverse(o.begin(), o.end());
		face.outlines.push_back(std::move(o));
	}
	return face;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------
// The mesh
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------

// Extrudes outlines into a solid: letters `height` tall (a capital letter, `capHeight` in the outlines' unit; 0 takes their own height), stretched `stretch` times sideways, `depth`
// thick. Corners sharper than about 35 degrees stay sharp, the rest of a wall is smooth. Centred on its bounding box.
inline ShapeMesh extrudeOutlines(const TextOutline& outline, double height, double stretch, double depth) {
	using namespace scene_shapes_detail;
	ShapeMesh m;
	const TextFace face = triangulateOutlines(outline.contours);
	if (face.points.empty() || face.triangles.empty()) return m;
	double x0 = std::numeric_limits<double>::max(), y0 = x0, x1 = -x0, y1 = x1;
	for (const TextPoint& p : face.points) { x0 = std::min(x0, p[0]); x1 = std::max(x1, p[0]); y0 = std::min(y0, p[1]); y1 = std::max(y1, p[1]); }
	const double cap = outline.capHeight > 0 ? outline.capHeight : std::max(1e-9, y1 - y0);
	const double sy = std::max(1e-9, height) / cap, sx = sy * std::max(1e-6, stretch);
	const double cx = (x0 + x1) / 2, cy = (y0 + y1) / 2, hz = std::max(1e-6, depth) / 2;
	const double w = std::max(1e-9, x1 - x0), h = std::max(1e-9, y1 - y0);
	const auto world = [&](const TextPoint& p, double z) { return V{(p[0] - cx) * sx, (p[1] - cy) * sy, z}; };

	// The two faces.
	std::vector<int> front(face.points.size()), back(face.points.size());
	for (std::size_t i = 0; i < face.points.size(); ++i) {
		const double u = (face.points[i][0] - x0) / w, v = (face.points[i][1] - y0) / h;
		front[i] = addVertex(m, world(face.points[i], hz), {0, 0, 1}, u, v);
		back[i] = addVertex(m, world(face.points[i], -hz), {0, 0, -1}, 1 - u, v);
	}
	for (std::size_t t = 0; t + 2 < face.triangles.size(); t += 3) {
		const int a = face.triangles[t], b = face.triangles[t + 1], c = face.triangles[t + 2];
		addTriangle(m, front[static_cast<std::size_t>(a)], front[static_cast<std::size_t>(b)], front[static_cast<std::size_t>(c)], {0, 0, 1});
		addTriangle(m, back[static_cast<std::size_t>(a)], back[static_cast<std::size_t>(b)], back[static_cast<std::size_t>(c)], {0, 0, -1});
	}

	// The walls: one strip of quads along every outline. The outward normal of an edge (seen from the ink) is to the right of its direction, for outer outlines (counter-clockwise)
	// and holes (clockwise) alike.
	const double crease = std::cos(35.0 * 3.14159265358979323846 / 180.0);
	for (const std::vector<int>& loop : face.outlines) {
		const std::size_t k = loop.size();
		std::vector<V> edgeNormal(k);
		std::vector<double> edgeLength(k);
		for (std::size_t i = 0; i < k; ++i) {
			const TextPoint& a = face.points[static_cast<std::size_t>(loop[i])];
			const TextPoint& b = face.points[static_cast<std::size_t>(loop[(i + 1) % k])];
			const double dx = (b[0] - a[0]) * sx, dy = (b[1] - a[1]) * sy, len = std::hypot(dx, dy);
			edgeLength[i] = len;
			edgeNormal[i] = len > 0 ? V{dy / len, -dx / len, 0} : V{0, 0, 0};
		}
		double along = 0, total = 0;
		for (double l : edgeLength) total += l;
		for (std::size_t i = 0; i < k; ++i) {
			const std::size_t next = (i + 1) % k, prev = (i + k - 1) % k;
			if (edgeLength[i] <= 0) continue;
			const auto vertexNormal = [&](std::size_t other) {
				const V n0 = edgeNormal[i], n1 = edgeNormal[other];
				if (dot(n0, n1) >= crease) return unit({n0.x + n1.x, n0.y + n1.y, 0});
				return n0;
			};
			const V nStart = vertexNormal(prev), nEnd = vertexNormal(next);
			const TextPoint& a = face.points[static_cast<std::size_t>(loop[i])];
			const TextPoint& b = face.points[static_cast<std::size_t>(loop[next])];
			const double u0 = total > 0 ? along / total : 0, u1 = total > 0 ? (along + edgeLength[i]) / total : 1;
			along += edgeLength[i];
			const int a0 = addVertex(m, world(a, -hz), nStart, u0, 0), a1 = addVertex(m, world(a, hz), nStart, u0, 1);
			const int b0 = addVertex(m, world(b, -hz), nEnd, u1, 0), b1 = addVertex(m, world(b, hz), nEnd, u1, 1);
			addTriangle(m, a0, b0, b1, edgeNormal[i]);
			addTriangle(m, a0, b1, a1, edgeNormal[i]);
		}
	}
	return m;
}

// The mesh of an object's text, or an empty mesh when no provider is set or the text has no outlines (a space, an empty string). The last few meshes are kept: the views, the
// writer and the snapping all ask for the same one, and making it means a font lookup and a triangulation.
inline ShapeMesh textMesh(const TextParams& params, double height, double stretch, double depth) {
	TextOutlineProvider provider = textOutlineProviderSlot();
	if (!provider) return ShapeMesh();
	TextParams p = params;
	if (static_cast<int>(p.text.size()) > kMaxTextLength) p.text.resize(static_cast<std::size_t>(kMaxTextLength));
	char numbers[160];
	std::snprintf(numbers, sizeof numbers, "%.17g|%.17g|%.17g|%d|%d|%d|", height, stretch, depth, p.bold ? 1 : 0, p.italic ? 1 : 0, p.align);
	const std::string key = std::string(numbers) + p.font + "|" + p.text;
	TextMeshCache& cache = textMeshCache();
	{
		std::lock_guard<std::mutex> lock(cache.mutex);
		for (const auto& entry : cache.recent)
			if (entry.first == key) return entry.second;
	}
	TextOutline outline;
	ShapeMesh mesh;
	if (provider(p, outline)) mesh = extrudeOutlines(outline, height, stretch, depth);
	{
		std::lock_guard<std::mutex> lock(cache.mutex);
		if (cache.recent.size() >= 8) cache.recent.erase(cache.recent.begin());
		cache.recent.emplace_back(key, mesh);
	}
	return mesh;
}

}  // namespace scene_doc
