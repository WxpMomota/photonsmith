#pragma once
// model_modifiers.h -- modeling, part 3: a modifier stack, as Blender has. A part's mesh (film_model.h: shapes, a cage, a sweep...) goes through its modifiers in the order
// they are written, each changing the whole of it:
//
//   bend A [toward x|z]       curls the part up its height (y) through A degrees, towards +z (or +x): a stalk, a tail, a leaning tower
//   twist A                   turns it about its upright axis A degrees from its foot to its top: a drill, a candy stick, a twisted column
//   taper S                   narrows (S < 1) or widens it from foot (1) to top (S): a cone from a cylinder, a vase
//   mirror x|y|z              a mirrored copy across that plane through the origin is added: model half a face, a car, a ship
//   array N DX DY DZ          N copies, each DX DY DZ on from the one before: a fence, a row of windows, steps
//   radial N                  N copies round the upright axis: petals, wheel spokes, columns round a temple
//   lattice I J K DX DY DZ    the corner I J K (0 or 1 each: the part's box's low or high side in x, y, z) moved, the part bending smoothly with it (a lattice of 2 x 2 x 2)
//   solidify T                an open sheet given a wall T thick: a leaf, a cloak, a bowl from a dish
//   smooth N                  N rounds of smoothing that keep its size (Taubin's lambda-mu)
//   move DX DY DZ, scale S | SX SY SZ, rotate AX AY AZ    the whole part moved, sized, turned (degrees about x, then y, then z)
//
// The part's box (its lowest and highest y, its x and z) is measured before each modifier, so `bend`, `twist`, `taper` and `lattice` work over the part as it then is.

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>

#include "cartoon_lib.h"
#include "sdf_mesh.h"

namespace cartoon {
namespace model {

struct Modifier {
	std::string kind;
	std::vector<double> v;
	std::string axis = "y";   // (mirror's plane; bend's direction: x or z)
};

inline void boxOf(const Mesh &m, Vec3 &lo, Vec3 &hi) {
	lo = {1e30, 1e30, 1e30}, hi = {-1e30, -1e30, -1e30};
	for (const Vec3 &p : m.verts) lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)}, hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
}
// `b` appended to `a` (uvs kept only when both have them).
inline void append(Mesh &a, const Mesh &b) {
	const bool uv = (a.verts.empty() || a.uvs.size() == a.verts.size()) && b.uvs.size() == b.verts.size();
	if (!uv) a.uvs.clear();
	const int base = static_cast<int>(a.verts.size());
	a.verts.insert(a.verts.end(), b.verts.begin(), b.verts.end());
	for (std::size_t i = 0; i < b.verts.size(); ++i) a.normals.push_back(i < b.normals.size() ? b.normals[i] : Vec3{0, 1, 0});
	if (uv) a.uvs.insert(a.uvs.end(), b.uvs.begin(), b.uvs.end());
	for (const auto &t : b.tris) a.tris.push_back({t[0] + base, t[1] + base, t[2] + base});
}
// The mesh moved by `m` (normals by its inverse transpose; a mirroring transform reverses the winding).
inline Mesh transformed(const Mesh &in, const Mat4 &m) {
	Mesh out = in;
	const Mat4 inv = invertAffine(m);
	for (std::size_t i = 0; i < out.verts.size(); ++i) {
		out.verts[i] = transformPoint(m, in.verts[i]);
		if (i < in.normals.size()) {
			const Vec3 n = in.normals[i];
			out.normals[i] = normalize(Vec3{inv[0] * n.x + inv[4] * n.y + inv[8] * n.z, inv[1] * n.x + inv[5] * n.y + inv[9] * n.z, inv[2] * n.x + inv[6] * n.y + inv[10] * n.z});
		}
	}
	const double det = m[0] * (m[5] * m[10] - m[6] * m[9]) - m[1] * (m[4] * m[10] - m[6] * m[8]) + m[2] * (m[4] * m[9] - m[5] * m[8]);
	if (det < 0.0)
		for (auto &t : out.tris) std::swap(t[1], t[2]);
	return out;
}

// The edges in only one face (an open mesh's border), each as (from, to) in its face's winding.
inline std::vector<std::pair<int, int>> borderOf(const Mesh &m) {
	std::map<std::pair<int, int>, int> count;
	for (const auto &t : m.tris)
		for (int q = 0; q < 3; ++q) ++count[{std::min(t[q], t[(q + 1) % 3]), std::max(t[q], t[(q + 1) % 3])}];
	std::vector<std::pair<int, int>> out;
	for (const auto &t : m.tris)
		for (int q = 0; q < 3; ++q)
			if (count[{std::min(t[q], t[(q + 1) % 3]), std::max(t[q], t[(q + 1) % 3])}] == 1) out.push_back({t[q], t[(q + 1) % 3]});
	return out;
}

// Each vertex's neighbours (along the triangles' edges).
inline std::vector<std::vector<int>> neighboursOf(const Mesh &m) {
	std::vector<std::vector<int>> n(m.verts.size());
	for (const auto &t : m.tris)
		for (int q = 0; q < 3; ++q) {
			const int a = t[q], b = t[(q + 1) % 3];
			if (std::find(n[a].begin(), n[a].end(), b) == n[a].end()) n[a].push_back(b);
			if (std::find(n[b].begin(), n[b].end(), a) == n[b].end()) n[b].push_back(a);
		}
	return n;
}

// One modifier on `m`.
inline Mesh applied(const Mesh &in, const Modifier &mod) {
	Mesh m = in;
	Vec3 lo, hi;
	boxOf(m, lo, hi);
	const double height = std::max(1e-9, hi.y - lo.y);
	const auto arg = [&](std::size_t i, double fallback) { return i < mod.v.size() ? mod.v[i] : fallback; };
	if (mod.kind == "bend") {
		const double total = arg(0, 90.0) * kPi / 180.0;
		if (std::abs(total) < 1e-9) return m;
		const double R = height / total;   // (the arc's radius at the axis)
		const bool towardX = mod.axis == "x";
		for (Vec3 &p : m.verts) {
			const double th = total * (p.y - lo.y) / height;
			double &side = towardX ? p.x : p.z;
			const double r = R - side;
			const double y = lo.y + r * std::sin(th), s = R - r * std::cos(th);
			p.y = y, side = s;
		}
	} else if (mod.kind == "twist" || mod.kind == "taper") {
		for (Vec3 &p : m.verts) {
			const double u = (p.y - lo.y) / height;
			if (mod.kind == "twist") {
				const double a = arg(0, 90.0) * kPi / 180.0 * u, c = std::cos(a), s = std::sin(a);
				p = {c * p.x + s * p.z, p.y, -s * p.x + c * p.z};
			} else {
				const double k = lerp(1.0, arg(0, 0.5), u);
				p.x *= k, p.z *= k;
			}
		}
	} else if (mod.kind == "mirror") {
		const Vec3 s = mod.axis == "x" ? Vec3{-1, 1, 1} : mod.axis == "y" ? Vec3{1, -1, 1} : Vec3{1, 1, -1};
		append(m, transformed(in, scale(s.x, s.y, s.z)));
		return m;
	} else if (mod.kind == "array" || mod.kind == "radial") {
		const int n = std::clamp(static_cast<int>(arg(0, 2)), 1, 500);
		m = in;
		for (int k = 1; k < n; ++k)
			append(m, transformed(in, mod.kind == "array" ? translate(Vec3{arg(1, 1.0), arg(2, 0.0), arg(3, 0.0)} * static_cast<double>(k)) : rotate({0, 1, 0}, 360.0 * k / n)));
		return m;
	} else if (mod.kind == "lattice") {
		const int ci = arg(0, 1) > 0.5, cj = arg(1, 1) > 0.5, ck = arg(2, 1) > 0.5;
		const Vec3 d{arg(3, 0.0), arg(4, 0.0), arg(5, 0.0)}, size{std::max(1e-9, hi.x - lo.x), height, std::max(1e-9, hi.z - lo.z)};
		for (Vec3 &p : m.verts) {
			const double u = (p.x - lo.x) / size.x, v = (p.y - lo.y) / size.y, w = (p.z - lo.z) / size.z;
			const double weight = (ci ? u : 1.0 - u) * (cj ? v : 1.0 - v) * (ck ? w : 1.0 - w);   // (trilinear: the moved corner's share)
			p = p + d * weight;
		}
	} else if (mod.kind == "solidify") {
		const double t = arg(0, 0.02);
		sdf::computeNormals(m);
		Mesh inner = m;
		for (std::size_t i = 0; i < inner.verts.size(); ++i) inner.verts[i] = inner.verts[i] - m.normals[i] * t;
		for (auto &tr : inner.tris) std::swap(tr[1], tr[2]);
		const int base = static_cast<int>(m.verts.size());
		const std::vector<std::pair<int, int>> border = borderOf(m);
		append(m, inner);
		for (const auto &[a, b] : border) m.tris.push_back({b, a, a + base}), m.tris.push_back({b, a + base, b + base});   // (the rim joining the two sheets)
	} else if (mod.kind == "smooth") {
		const std::vector<std::vector<int>> nb = neighboursOf(m);
		const int rounds = std::clamp(static_cast<int>(arg(0, 2)), 0, 50);
		for (int r = 0; r < 2 * rounds; ++r) {
			const double k = r % 2 == 0 ? 0.5 : -0.53;   // (shrink, then grow back a little more: Taubin's lambda and mu)
			std::vector<Vec3> next = m.verts;
			for (std::size_t i = 0; i < m.verts.size(); ++i) {
				if (nb[i].empty()) continue;
				Vec3 c{0, 0, 0};
				for (int j : nb[i]) c = c + m.verts[j];
				c = c * (1.0 / nb[i].size());
				next[i] = m.verts[i] + (c - m.verts[i]) * k;
			}
			m.verts = next;
		}
	} else if (mod.kind == "move") {
		return transformed(in, translate(Vec3{arg(0, 0.0), arg(1, 0.0), arg(2, 0.0)}));
	} else if (mod.kind == "scale") {
		const double s = arg(0, 1.0);
		return transformed(in, scale(s, arg(1, s), arg(2, s)));
	} else if (mod.kind == "rotate") {
		return transformed(in, mul(rotate({0, 0, 1}, arg(2, 0.0)), rotate({0, 1, 0}, arg(1, 0.0)), rotate({1, 0, 0}, arg(0, 0.0))));
	}
	sdf::computeNormals(m);
	return m;
}

inline const std::vector<std::string> &modifierKinds() {
	static const std::vector<std::string> k = {"bend", "twist", "taper", "mirror", "array", "radial", "lattice", "solidify", "smooth", "move", "scale", "rotate"};
	return k;
}

}  // namespace model
}  // namespace cartoon
