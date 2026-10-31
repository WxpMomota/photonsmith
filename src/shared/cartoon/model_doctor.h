#pragma once
// model_doctor.h -- modeling, part 10: what is wrong with a mesh, before a film is rendered with it (`ray_tracer --model-check FILM`, `film.py model FILM NAME`).
//
//   holes          edges in only one triangle: the mesh is open there (a sheet is fine; a solid that leaks shows its inside, and glass or fog inside it go wrong)
//   non-manifold   edges in three or more triangles: two shapes stuck through each other, a fin glued edge-on
//   flipped        edges both of whose triangles run the same way along them: a triangle wound against its neighbours, shading dark or lit from inside
//   inside out     a closed mesh whose volume is negative: every triangle faces in
//   degenerate     triangles with (almost) no area: dropped by the renderers, a crack where they were
//   scale          bigger than 500 m or smaller than 5 mm across: a model in centimetres, or in the wrong units
//   heavy          more than two million triangles: slow to load and to render in every frame (meshed too finely, or displaced at too small a size)

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "cartoon_lib.h"

namespace cartoon {
namespace model {

struct Report {
	std::size_t verts = 0, tris = 0, holes = 0, nonManifold = 0, flipped = 0, degenerate = 0;
	double volume = 0.0, area = 0.0;
	Vec3 lo{0, 0, 0}, hi{0, 0, 0};
	bool closed() const { return holes == 0 && nonManifold == 0; }
	std::vector<std::string> problems() const {
		std::vector<std::string> out;
		char buf[200];
		if (tris == 0) return {"it has no triangles"};
		if (holes) std::snprintf(buf, sizeof buf, "%zu open edges (holes in it, or an open sheet)", holes), out.push_back(buf);
		if (nonManifold) std::snprintf(buf, sizeof buf, "%zu edges shared by three or more triangles (shapes stuck through each other)", nonManifold), out.push_back(buf);
		if (flipped) std::snprintf(buf, sizeof buf, "%zu edges between triangles wound against each other (flipped faces)", flipped), out.push_back(buf);
		if (closed() && volume < 0.0) out.push_back("it is inside out (every triangle faces in)");
		if (degenerate) std::snprintf(buf, sizeof buf, "%zu triangles with no area", degenerate), out.push_back(buf);
		const double size = std::max({hi.x - lo.x, hi.y - lo.y, hi.z - lo.z});
		if (size > 500.0) std::snprintf(buf, sizeof buf, "it is %.0f m across: far bigger than a prop (in the wrong units?)", size), out.push_back(buf);
		if (size < 0.005) std::snprintf(buf, sizeof buf, "it is %.4f m across: far smaller than a prop (in the wrong units?)", size), out.push_back(buf);
		if (tris > 2000000) std::snprintf(buf, sizeof buf, "%zu triangles: heavy for every frame (a bigger `detail`, or `displace` at a bigger size)", tris), out.push_back(buf);
		return out;
	}
};

inline Report examine(const Mesh &m) {
	Report r;
	r.verts = m.verts.size(), r.tris = m.tris.size();
	if (m.verts.empty()) return r;
	r.lo = r.hi = m.verts[0];
	for (const Vec3 &p : m.verts) r.lo = {std::min(r.lo.x, p.x), std::min(r.lo.y, p.y), std::min(r.lo.z, p.z)}, r.hi = {std::max(r.hi.x, p.x), std::max(r.hi.y, p.y), std::max(r.hi.z, p.z)};
	const double size = std::max({r.hi.x - r.lo.x, r.hi.y - r.lo.y, r.hi.z - r.lo.z, 1e-9});
	// each edge: how many triangles have it, and how many run each way along it
	struct Use {
		int count = 0, forward = 0;
	};
	std::map<std::pair<int, int>, Use> edges;
	for (const auto &t : m.tris) {
		const Vec3 a = m.verts[t[0]], b = m.verts[t[1]], c = m.verts[t[2]];
		const Vec3 n = cross(b - a, c - a);
		const double area = 0.5 * length(n);
		r.area += area;
		r.volume += dot(a, cross(b, c)) / 6.0;
		if (area < 1e-12 * size * size || t[0] == t[1] || t[1] == t[2] || t[0] == t[2]) ++r.degenerate;
		for (int q = 0; q < 3; ++q) {
			const int u = t[q], v = t[(q + 1) % 3];
			Use &e = edges[{std::min(u, v), std::max(u, v)}];
			++e.count;
			e.forward += u < v;
		}
	}
	for (const auto &[key, e] : edges) {
		if (e.count == 1) ++r.holes;
		else if (e.count > 2) ++r.nonManifold;
		else if (e.forward != 1) ++r.flipped;
	}
	return r;
}

// The mesh with coincident vertices merged (to `tolerance`): meshes put together from pieces, or split for their normals, examined as the surface they make.
inline Mesh welded(const Mesh &in, double tolerance) {
	Mesh out;
	std::map<std::array<long long, 3>, int> index;
	std::vector<int> remap(in.verts.size());
	for (std::size_t i = 0; i < in.verts.size(); ++i) {
		const Vec3 &p = in.verts[i];
		const std::array<long long, 3> key{std::llround(p.x / tolerance), std::llround(p.y / tolerance), std::llround(p.z / tolerance)};
		auto it = index.find(key);
		if (it == index.end()) {
			it = index.emplace(key, static_cast<int>(out.verts.size())).first;
			out.verts.push_back(p);
		}
		remap[i] = it->second;
	}
	for (const auto &t : in.tris) out.tris.push_back({remap[t[0]], remap[t[1]], remap[t[2]]});
	return out;
}

}  // namespace model
}  // namespace cartoon
