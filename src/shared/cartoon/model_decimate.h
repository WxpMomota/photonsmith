#pragma once
// model_decimate.h -- modeling round 2, part 4: simplifying a mesh. `budget N` on a piece brings it to at most N triangles, and a model far from the camera (film_model.h's
// levels 2 and 4) is drawn with a quarter and a sixteenth of its triangles - for any piece, an imported scan or a displaced sculpt as much as a shape.
//
// Quadric error metric simplification (Garland and Heckbert, "Surface Simplification Using Quadric Error Metrics", SIGGRAPH 1997): every vertex carries the sum of the
// squared distances to the planes of its triangles; the edge whose two ends can be merged into one point with the least of that error goes first, to the point that minimises
// it. An open mesh's border edges carry planes across them too, so the border keeps its outline. A merge that would fold a triangle over (its normal turning more than
// about 80 degrees) or pinch the surface (the two ends sharing more than the two neighbours an edge has: the link condition) is not made.

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <queue>
#include <set>
#include <vector>

#include "cartoon_lib.h"
#include "model_doctor.h"
#include "sdf_mesh.h"

namespace cartoon {
namespace model {

struct Quadric {
	double a[10] = {0};   // (the symmetric 4 x 4: xx xy xz xw yy yz yw zz zw ww)
	void addPlane(Vec3 n, double d, double w) {
		const double p[4] = {n.x, n.y, n.z, d};
		int k = 0;
		for (int i = 0; i < 4; ++i)
			for (int j = i; j < 4; ++j) a[k++] += w * p[i] * p[j];
	}
	void add(const Quadric &q) {
		for (int i = 0; i < 10; ++i) a[i] += q.a[i];
	}
	double error(Vec3 v) const {
		const double x = v.x, y = v.y, z = v.z;
		return a[0] * x * x + 2 * a[1] * x * y + 2 * a[2] * x * z + 2 * a[3] * x + a[4] * y * y + 2 * a[5] * y * z + 2 * a[6] * y + a[7] * z * z + 2 * a[8] * z + a[9];
	}
	// The point of least error, if the 3 x 3 part can be solved.
	bool best(Vec3 &out) const {
		const double m[3][3] = {{a[0], a[1], a[2]}, {a[1], a[4], a[5]}, {a[2], a[5], a[7]}}, b[3] = {-a[3], -a[6], -a[8]};
		double x[3];
		if (!solve3(m, b, x)) return false;
		out = {x[0], x[1], x[2]};
		return std::isfinite(x[0]) && std::isfinite(x[1]) && std::isfinite(x[2]);
	}
};

// The mesh with at most `target` triangles (as it is when it has no more already).
inline Mesh decimated(const Mesh &input, std::size_t target) {
	if (input.tris.size() <= target || input.tris.size() < 8) return input;
	Mesh m = welded(input, 1e-7);
	const int nv = static_cast<int>(m.verts.size());
	std::vector<std::array<int, 3>> faces(m.tris.begin(), m.tris.end());
	std::vector<char> alive(faces.size(), 1);
	std::vector<std::vector<int>> around(nv);
	for (std::size_t f = 0; f < faces.size(); ++f)
		for (int q = 0; q < 3; ++q) around[faces[f][q]].push_back(static_cast<int>(f));
	// the quadrics: each triangle's plane, by its area; each border edge's plane across it, heavily
	std::vector<Quadric> Q(nv);
	std::map<std::pair<int, int>, int> edgeUse;
	for (const auto &f : faces)
		for (int q = 0; q < 3; ++q) ++edgeUse[{std::min(f[q], f[(q + 1) % 3]), std::max(f[q], f[(q + 1) % 3])}];
	for (const auto &f : faces) {
		const Vec3 a = m.verts[f[0]], b = m.verts[f[1]], c = m.verts[f[2]];
		Vec3 n = cross(b - a, c - a);
		const double area = 0.5 * length(n);
		if (area < 1e-20) continue;
		n = normalize(n);
		for (int q = 0; q < 3; ++q) Q[f[q]].addPlane(n, -dot(n, a), area);
		for (int q = 0; q < 3; ++q) {
			const int u = f[q], v = f[(q + 1) % 3];
			if (edgeUse[{std::min(u, v), std::max(u, v)}] != 1) continue;
			const Vec3 e = m.verts[v] - m.verts[u], side = normalize(cross(e, n));
			for (const int w : {u, v}) Q[w].addPlane(side, -dot(side, m.verts[u]), 10.0 * dot(e, e));
		}
	}
	std::vector<int> stamp(nv, 0);
	struct Candidate {
		double cost;
		int u, v, su, sv;
		Vec3 at;
		bool operator<(const Candidate &o) const { return cost > o.cost; }   // (the cheapest on top)
	};
	std::priority_queue<Candidate> heap;
	const auto neighbours = [&](int v) {
		std::set<int> n;
		for (int f : around[v])
			if (alive[f])
				for (int q = 0; q < 3; ++q)
					if (faces[f][q] != v) n.insert(faces[f][q]);
		return n;
	};
	const auto consider = [&](int u, int v) {
		Quadric q = Q[u];
		q.add(Q[v]);
		Vec3 at;
		if (!q.best(at) || length(at - (m.verts[u] + m.verts[v]) * 0.5) > 2.0 * length(m.verts[u] - m.verts[v])) {
			const Vec3 options[3] = {m.verts[u], m.verts[v], (m.verts[u] + m.verts[v]) * 0.5};
			at = options[0];
			for (const Vec3 &o : options)
				if (q.error(o) < q.error(at)) at = o;
		}
		heap.push({std::max(0.0, q.error(at)), u, v, stamp[u], stamp[v], at});
	};
	for (int v = 0; v < nv; ++v)
		for (int w : neighbours(v))
			if (v < w) consider(v, w);
	std::size_t count = faces.size();
	while (count > target && !heap.empty()) {
		const Candidate c = heap.top();
		heap.pop();
		if (stamp[c.u] != c.su || stamp[c.v] != c.sv) continue;   // (a stale entry: one of its ends has moved since)
		// the link condition: the ends share only the one or two vertices across their edge
		const std::set<int> nu = neighbours(c.u), nvv = neighbours(c.v);
		int shared = 0, edgeFaces = 0;
		for (int x : nu) shared += nvv.count(x) ? 1 : 0;
		for (int f : around[c.u])
			if (alive[f] && (faces[f][0] == c.v || faces[f][1] == c.v || faces[f][2] == c.v)) ++edgeFaces;
		if (edgeFaces == 0 || shared > edgeFaces) continue;
		// no triangle may fold over
		bool folds = false;
		for (const int end : {c.u, c.v}) {
			for (int f : around[end]) {
				if (!alive[f]) continue;
				const auto &t = faces[f];
				if ((t[0] == c.u || t[1] == c.u || t[2] == c.u) && (t[0] == c.v || t[1] == c.v || t[2] == c.v)) continue;   // (it goes)
				Vec3 p[3], q[3];
				for (int k = 0; k < 3; ++k) p[k] = m.verts[t[k]], q[k] = t[k] == c.u || t[k] == c.v ? c.at : p[k];
				const Vec3 n0 = cross(p[1] - p[0], p[2] - p[0]), n1 = cross(q[1] - q[0], q[2] - q[0]);
				if (length(n1) < 1e-14 || dot(n0, n1) < 0.17 * length(n0) * length(n1)) folds = true;
			}
		}
		if (folds) continue;
		// merge v into u
		for (int f : around[c.v]) {
			if (!alive[f]) continue;
			auto &t = faces[f];
			if (t[0] == c.u || t[1] == c.u || t[2] == c.u) {
				alive[f] = 0, --count;
				continue;
			}
			for (int k = 0; k < 3; ++k)
				if (t[k] == c.v) t[k] = c.u;
			around[c.u].push_back(f);
		}
		around[c.v].clear();
		m.verts[c.u] = c.at;
		Q[c.u].add(Q[c.v]);
		++stamp[c.u], ++stamp[c.v];
		for (int w : neighbours(c.u)) consider(std::min(c.u, w), std::max(c.u, w));
	}
	// the mesh that is left, its vertices renumbered
	Mesh out;
	std::vector<int> index(nv, -1);
	for (std::size_t f = 0; f < faces.size(); ++f) {
		if (!alive[f]) continue;
		std::array<int, 3> t{};
		for (int k = 0; k < 3; ++k) {
			int &ix = index[faces[f][k]];
			if (ix < 0) ix = static_cast<int>(out.verts.size()), out.verts.push_back(m.verts[faces[f][k]]);
			t[k] = ix;
		}
		if (t[0] != t[1] && t[1] != t[2] && t[0] != t[2]) out.tris.push_back(t);
	}
	sdf::computeNormals(out);
	return out;
}

}  // namespace model
}  // namespace cartoon
