#pragma once
// model_boolean.h -- modeling round 2, part 3: booleans on any piece. Shapes could always be cut and blended (model_sdf.h); this does it for every mesh - a subdivided hull, a
// sweep, a kit part, an imported file. `boolean cut|add|intersect|blend K PIECE` in a piece takes the tool piece (marked `tool`: not drawn itself) out of it, joins it on,
// keeps only where they overlap, or joins it with a K-metre fillet.
//
// How: each mesh becomes a signed distance field sampled on a grid - inside or outside from the parity of a ray along x across each row of cells (a closed mesh crosses it
// an even number of times), and the exact distance to the nearest triangle within two cells of the surface - the two fields combined as shapes are, and the result meshed
// by dual contouring (sharp edges kept). A mesh that is not closed has no inside: the doctor (model_doctor.h) says so before it is used as a tool.

#include <algorithm>
#include <cmath>
#include <vector>

#include "cartoon_lib.h"
#include "model_modifiers.h"
#include "model_sdf.h"

namespace cartoon {
namespace model {

// The distance from p to the triangle abc (Ericson, Real-Time Collision Detection, 5.1.5: the closest point by Voronoi regions).
inline double triangleDistance(Vec3 p, Vec3 a, Vec3 b, Vec3 c) {
	const Vec3 ab = b - a, ac = c - a, ap = p - a;
	const double d1 = dot(ab, ap), d2 = dot(ac, ap);
	if (d1 <= 0 && d2 <= 0) return length(p - a);
	const Vec3 bp = p - b;
	const double d3 = dot(ab, bp), d4 = dot(ac, bp);
	if (d3 >= 0 && d4 <= d3) return length(p - b);
	const double vc = d1 * d4 - d3 * d2;
	if (vc <= 0 && d1 >= 0 && d3 <= 0) return length(p - (a + ab * (d1 / (d1 - d3))));
	const Vec3 cp = p - c;
	const double d5 = dot(ab, cp), d6 = dot(ac, cp);
	if (d6 >= 0 && d5 <= d6) return length(p - c);
	const double vb = d5 * d2 - d1 * d6;
	if (vb <= 0 && d2 >= 0 && d6 <= 0) return length(p - (a + ac * (d2 / (d2 - d6))));
	const double va = d3 * d6 - d5 * d4;
	if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) return length(p - (b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)))));
	const double denom = 1.0 / (va + vb + vc);
	return length(p - (a + ab * (vb * denom) + ac * (vc * denom)));
}

// A mesh's signed distance, sampled on a grid of `cell` cubes (trilinear between samples; outside the grid, the distance to it plus its band).
struct MeshField {
	Vec3 lo{0, 0, 0};
	double cell = 0.01;
	int nx = 0, ny = 0, nz = 0;
	std::vector<float> d;
	double sample(int i, int j, int k) const {
		i = std::clamp(i, 0, nx - 1), j = std::clamp(j, 0, ny - 1), k = std::clamp(k, 0, nz - 1);
		return d[(static_cast<std::size_t>(k) * ny + j) * nx + i];
	}
	double operator()(Vec3 p) const {
		if (d.empty()) return 1e9;
		const Vec3 q = (p - lo) * (1.0 / cell);
		const Vec3 hi{static_cast<double>(nx - 1), static_cast<double>(ny - 1), static_cast<double>(nz - 1)};
		const Vec3 c{std::clamp(q.x, 0.0, hi.x), std::clamp(q.y, 0.0, hi.y), std::clamp(q.z, 0.0, hi.z)};
		const double outside = length(q - c) * cell;
		const int i = std::min(static_cast<int>(c.x), nx - 2), j = std::min(static_cast<int>(c.y), ny - 2), k = std::min(static_cast<int>(c.z), nz - 2);
		const double u = c.x - i, v = c.y - j, w = c.z - k;
		const double x00 = lerp(sample(i, j, k), sample(i + 1, j, k), u), x10 = lerp(sample(i, j + 1, k), sample(i + 1, j + 1, k), u);
		const double x01 = lerp(sample(i, j, k + 1), sample(i + 1, j, k + 1), u), x11 = lerp(sample(i, j + 1, k + 1), sample(i + 1, j + 1, k + 1), u);
		return lerp(lerp(x00, x10, v), lerp(x01, x11, v), w) + outside;
	}
};
inline MeshField meshField(const Mesh &m, double cell) {
	MeshField f;
	f.cell = cell;
	if (m.tris.empty()) return f;
	Vec3 lo, hi;
	boxOf(m, lo, hi);
	const double pad = 3.0 * cell;
	f.lo = lo - Vec3{pad, pad, pad};
	f.nx = static_cast<int>(std::ceil((hi.x - lo.x + 2 * pad) / cell)) + 1, f.ny = static_cast<int>(std::ceil((hi.y - lo.y + 2 * pad) / cell)) + 1,
	f.nz = static_cast<int>(std::ceil((hi.z - lo.z + 2 * pad) / cell)) + 1;
	const std::size_t n = static_cast<std::size_t>(f.nx) * f.ny * f.nz;
	const float band = static_cast<float>(2.0 * cell);
	std::vector<float> dist(n, band);
	// the distance near the surface: each triangle against the samples round it
	for (const auto &t : m.tris) {
		const Vec3 a = m.verts[t[0]], b = m.verts[t[1]], c = m.verts[t[2]];
		const Vec3 tlo{std::min({a.x, b.x, c.x}) - 2 * cell, std::min({a.y, b.y, c.y}) - 2 * cell, std::min({a.z, b.z, c.z}) - 2 * cell};
		const Vec3 thi{std::max({a.x, b.x, c.x}) + 2 * cell, std::max({a.y, b.y, c.y}) + 2 * cell, std::max({a.z, b.z, c.z}) + 2 * cell};
		const int i0 = std::max(0, static_cast<int>((tlo.x - f.lo.x) / cell)), i1 = std::min(f.nx - 1, static_cast<int>((thi.x - f.lo.x) / cell) + 1);
		const int j0 = std::max(0, static_cast<int>((tlo.y - f.lo.y) / cell)), j1 = std::min(f.ny - 1, static_cast<int>((thi.y - f.lo.y) / cell) + 1);
		const int k0 = std::max(0, static_cast<int>((tlo.z - f.lo.z) / cell)), k1 = std::min(f.nz - 1, static_cast<int>((thi.z - f.lo.z) / cell) + 1);
		for (int k = k0; k <= k1; ++k)
			for (int j = j0; j <= j1; ++j)
				for (int i = i0; i <= i1; ++i) {
					float &slot = dist[(static_cast<std::size_t>(k) * f.ny + j) * f.nx + i];
					slot = std::min(slot, static_cast<float>(triangleDistance(f.lo + Vec3{i * cell, j * cell, k * cell}, a, b, c)));
				}
	}
	// inside or out: a ray along +x across each row (y, z), the crossings counted (a sample is inside after an odd number of them)
	std::vector<std::vector<double>> rows(static_cast<std::size_t>(f.ny) * f.nz);
	for (const auto &t : m.tris) {
		const Vec3 a = m.verts[t[0]], b = m.verts[t[1]], c = m.verts[t[2]];
		// (the rows run off the grid by odd amounts - one through a mesh's vertex or along its edge miscounts - so the rows a triangle spans are found with those amounts)
		const double oy = 0.0137 * cell, oz = 0.0291 * cell;
		const int j0 = std::max(0, static_cast<int>(std::ceil((std::min({a.y, b.y, c.y}) - f.lo.y - oy) / cell))), j1 = std::min(f.ny - 1, static_cast<int>(std::floor((std::max({a.y, b.y, c.y}) - f.lo.y - oy) / cell)));
		const int k0 = std::max(0, static_cast<int>(std::ceil((std::min({a.z, b.z, c.z}) - f.lo.z - oz) / cell))), k1 = std::min(f.nz - 1, static_cast<int>(std::floor((std::max({a.z, b.z, c.z}) - f.lo.z - oz) / cell)));
		for (int k = k0; k <= k1; ++k)
			for (int j = j0; j <= j1; ++j) {
				const double y = f.lo.y + j * cell + oy, z = f.lo.z + k * cell + oz;
				const double e = (b.y - a.y) * (c.z - a.z) - (c.y - a.y) * (b.z - a.z);
				if (std::abs(e) < 1e-30) continue;
				const double w1 = ((y - a.y) * (c.z - a.z) - (c.y - a.y) * (z - a.z)) / e, w2 = ((b.y - a.y) * (z - a.z) - (y - a.y) * (b.z - a.z)) / e;
				if (w1 < 0 || w2 < 0 || w1 + w2 > 1) continue;
				rows[static_cast<std::size_t>(k) * f.ny + j].push_back(a.x + w1 * (b.x - a.x) + w2 * (c.x - a.x));
			}
	}
	f.d.assign(n, band);
	for (int k = 0; k < f.nz; ++k)
		for (int j = 0; j < f.ny; ++j) {
			std::vector<double> &xs = rows[static_cast<std::size_t>(k) * f.ny + j];
			std::sort(xs.begin(), xs.end());
			std::size_t crossed = 0;
			for (int i = 0; i < f.nx; ++i) {
				const double x = f.lo.x + i * cell;
				while (crossed < xs.size() && xs[crossed] < x) ++crossed;
				const std::size_t q = (static_cast<std::size_t>(k) * f.ny + j) * f.nx + i;
				f.d[q] = crossed % 2 == 1 ? -dist[q] : dist[q];
			}
		}
	return f;
}

// `a` and `b` combined (op as a shape step's: add, blend K, cut [soft K], intersect) and meshed again on `cell` cubes with sharp edges.
inline Mesh combined(const Mesh &a, const Mesh &b, Op op, double k, double cell) {
	const MeshField fa = meshField(a, cell), fb = meshField(b, cell);
	const auto f = [&](Vec3 p) {
		const double da = fa(p), db = fb(p);
		switch (op) {
		case Op::Add: return std::min(da, db);
		case Op::Blend: return sdf::smin(da, db, std::max(1e-6, k));
		case Op::Cut: return k > 0.0 ? sdf::smax(da, -db, k) : std::max(da, -db);
		case Op::Intersect: return k > 0.0 ? sdf::smax(da, db, k) : std::max(da, db);
		}
		return da;
	};
	Vec3 lo, hi, lo2, hi2;
	boxOf(a, lo, hi);
	if (op == Op::Add || op == Op::Blend) {
		boxOf(b, lo2, hi2);
		lo = {std::min(lo.x, lo2.x), std::min(lo.y, lo2.y), std::min(lo.z, lo2.z)}, hi = {std::max(hi.x, hi2.x), std::max(hi.y, hi2.y), std::max(hi.z, hi2.z)};
	}
	const Vec3 pad{2 * cell + k, 2 * cell + k, 2 * cell + k};
	return dualContour(f, lo - pad, hi + pad, cell);
}

}  // namespace model
}  // namespace cartoon
