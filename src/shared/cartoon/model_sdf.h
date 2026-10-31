#pragma once
// model_sdf.h -- modeling, parts 2 and 4: shapes built from primitives and turned into meshes, soft or hard.
//
// A part of a model (film_model.h) is a list of steps, each a primitive - sphere, box, ellipsoid, capsule, cylinder, torus, cone - placed and turned, combined with what came
// before: `add` (union), `blend K` (a smooth union K metres wide: a character's limbs into its body), `cut` (subtract: a window, a mouth; `cut ... blend K` a soft cut),
// `intersect`. Then the whole part can be rounded (`round R`: every edge R round) or hollowed (`shell T`: a wall T thick). The shape is a signed distance field: negative
// inside.
//
// Hard surface: a part meshed with `hard` keeps its sharp edges. Naive surface nets (sdf_mesh.h) put each vertex at the middle of where the surface crosses its cell, so a
// box's edge is bevelled by a cell's width; dual contouring (Ju, Losasso, Schaefer and Warren, SIGGRAPH 2002) puts it where the planes the surface crosses at meet - the
// quadric error function's minimum, held to its cell - so corners and edges stay crisp. Its normals are split where faces meet at more than `smooth` degrees (Blender's
// auto smooth), so a hard edge shades hard and a curve stays smooth.

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <string>
#include <vector>

#include "cartoon_lib.h"
#include "sdf_mesh.h"

namespace cartoon {
namespace model {

struct Prim {
	std::string kind = "sphere";   // sphere, box, ellipsoid, capsule, cylinder, torus, cone
	Vec3 at{0, 0, 0};
	Vec3 size{1, 1, 1};   // sphere: radius in x; box and ellipsoid: full sizes; cylinder: radius, height; torus: ring radius, tube radius; cone: bottom radius, top radius, height
	Vec3 to{0, 1, 0};     // capsule: the other end (radius in size.x)
	Vec3 turn{0, 0, 0};   // degrees about x, then y, then z
	double round = 0.0;   // a box's or cylinder's edges rounded
};
enum class Op { Add, Blend, Cut, Intersect };
struct Step {
	Op op = Op::Add;
	double k = 0.0;   // (blend width; a soft cut's)
	Prim prim;
};

inline Mat4 frameOf(const Prim &p) { return mul(translate(p.at), rotate({0, 0, 1}, p.turn.z), rotate({0, 1, 0}, p.turn.y), rotate({1, 0, 0}, p.turn.x)); }
// The primitive's signed distance at `p` (the model's space). `inv`: frameOf(p)'s inverse.
inline double primDistance(const Prim &pr, const Mat4 &inv, Vec3 p) {
	const Vec3 q = transformPoint(inv, p);
	if (pr.kind == "sphere") return length(q) - pr.size.x;
	if (pr.kind == "box") return sdf::roundBox(q, identity(), pr.size * 0.5, pr.round);
	if (pr.kind == "ellipsoid") return sdf::ellipsoid(q, identity(), pr.size * 0.5);
	if (pr.kind == "capsule") return sdf::capsule(p, pr.at, pr.to, pr.size.x);
	if (pr.kind == "cylinder") {
		const double r = std::hypot(q.x, q.z) - (pr.size.x - pr.round), h = std::abs(q.y) - (0.5 * pr.size.y - pr.round);
		return std::min(std::max(r, h), 0.0) + std::hypot(std::max(r, 0.0), std::max(h, 0.0)) - pr.round;
	}
	if (pr.kind == "torus") return std::hypot(std::hypot(q.x, q.z) - pr.size.x, q.y) - pr.size.y;
	if (pr.kind == "cone") return sdf::roundCone(q, {0, -0.5 * pr.size.z, 0}, {0, 0.5 * pr.size.z, 0}, pr.size.x, pr.size.y);
	return 1e9;
}
// The primitive's thinnest measure (the meshing cube must be smaller than it, or the shape falls between the samples).
inline double thinnestOf(const Prim &p) {
	if (p.kind == "sphere" || p.kind == "capsule") return 2.0 * p.size.x;
	if (p.kind == "torus") return 2.0 * p.size.y;
	if (p.kind == "cylinder") return std::min(2.0 * p.size.x, p.size.y);
	if (p.kind == "cone") return std::min({2.0 * std::max(p.size.x, p.size.y), p.size.z});
	return std::min({p.size.x, p.size.y, p.size.z});
}
// How far the primitive reaches from its place (a bounding sphere's radius).
inline double reachOf(const Prim &p) {
	if (p.kind == "sphere") return p.size.x;
	if (p.kind == "capsule") return p.size.x;   // (round each end: bounds() takes both)
	if (p.kind == "torus") return p.size.x + p.size.y;
	if (p.kind == "cone") return std::hypot(std::max(p.size.x, p.size.y), 0.5 * p.size.z);
	if (p.kind == "cylinder") return std::hypot(p.size.x, 0.5 * p.size.y);
	return 0.5 * length(p.size);
}

struct Sdf {
	std::vector<Step> steps;
	double round = 0.0, shell = 0.0;
	std::vector<Mat4> inverses;   // (each step's frame inverted: prepare())
	void prepare() {
		inverses.clear();
		for (const Step &s : steps) inverses.push_back(invertAffine(frameOf(s.prim)));
	}
	double operator()(Vec3 p) const {
		double d = 1e9;
		for (std::size_t i = 0; i < steps.size(); ++i) {
			const double e = primDistance(steps[i].prim, inverses[i], p);
			switch (steps[i].op) {
			case Op::Add: d = std::min(d, e); break;
			case Op::Blend: d = sdf::smin(d, e, std::max(1e-6, steps[i].k)); break;
			case Op::Cut: d = steps[i].k > 0.0 ? sdf::smax(d, -e, steps[i].k) : std::max(d, -e); break;
			case Op::Intersect: d = steps[i].k > 0.0 ? sdf::smax(d, e, steps[i].k) : std::max(d, e); break;
			}
		}
		if (shell > 0.0) d = std::abs(d) - shell;
		return d - round;
	}
	// The box round everything added (a cut or an intersection takes nothing away from it), with a margin.
	void bounds(Vec3 &lo, Vec3 &hi) const {
		lo = {1e9, 1e9, 1e9}, hi = {-1e9, -1e9, -1e9};
		for (const Step &s : steps) {
			if (s.op == Op::Cut || s.op == Op::Intersect) continue;
			const double r = reachOf(s.prim) + s.k + round + shell;
			const Vec3 ends[2] = {s.prim.at, s.prim.kind == "capsule" ? s.prim.to : s.prim.at};
			for (const Vec3 &c : ends) lo = {std::min(lo.x, c.x - r), std::min(lo.y, c.y - r), std::min(lo.z, c.z - r)}, hi = {std::max(hi.x, c.x + r), std::max(hi.y, c.y + r), std::max(hi.z, c.z + r)};
		}
	}
};

// ---- meshing with sharp edges ----
// A 3 x 3 system solved by Cramer's rule (false when it is singular).
inline bool solve3(const double a[3][3], const double b[3], double x[3]) {
	const auto det = [](const double m[3][3]) {
		return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
	};
	const double d = det(a);
	if (std::abs(d) < 1e-12) return false;
	for (int c = 0; c < 3; ++c) {
		double m[3][3];
		for (int r = 0; r < 3; ++r)
			for (int k = 0; k < 3; ++k) m[r][k] = k == c ? b[r] : a[r][k];
		x[c] = det(m) / d;
	}
	return true;
}
// Any shape in [lo, hi] meshed by dual contouring on `cell` cubes: each cell's vertex where the planes the surface crosses its edges at come closest to meeting (a little pull
// to their mean point keeps a flat or curved patch tidy), held inside the cell.
template <typename F>
inline Mesh dualContour(const F &f, Vec3 lo, Vec3 hi, double cell) {
	cell *= previewCoarsening();
	const int nx = std::max(2, static_cast<int>(std::ceil((hi.x - lo.x) / cell)) + 1), ny = std::max(2, static_cast<int>(std::ceil((hi.y - lo.y) / cell)) + 1),
			  nz = std::max(2, static_cast<int>(std::ceil((hi.z - lo.z) / cell)) + 1);
	const auto at = [&](int i, int j, int k) { return (static_cast<std::size_t>(k) * ny + j) * nx + i; };
	std::vector<float> v(static_cast<std::size_t>(nx) * ny * nz);
	for (int k = 0; k < nz; ++k)
		for (int j = 0; j < ny; ++j)
			for (int i = 0; i < nx; ++i) v[at(i, j, k)] = static_cast<float>(f(lo + Vec3{i * cell, j * cell, k * cell}));
	Mesh m;
	std::vector<int> index(v.size(), -1);
	static const int corner[8][3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0}, {0, 0, 1}, {1, 0, 1}, {0, 1, 1}, {1, 1, 1}};
	static const int edge[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
	const double h = cell * 0.1;
	for (int k = 0; k < nz - 1; ++k)
		for (int j = 0; j < ny - 1; ++j)
			for (int i = 0; i < nx - 1; ++i) {
				float c[8];
				int inside = 0;
				for (int q = 0; q < 8; ++q) inside += (c[q] = v[at(i + corner[q][0], j + corner[q][1], k + corner[q][2])]) < 0.0f;
				if (inside == 0 || inside == 8) continue;
				const Vec3 base = lo + Vec3{double(i), double(j), double(k)} * cell;
				double A[3][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}}, b[3] = {0, 0, 0};
				Vec3 mass{0, 0, 0};
				int n = 0;
				for (const auto &e : edge) {
					const float fa = c[e[0]], fb = c[e[1]];
					if ((fa < 0.0f) == (fb < 0.0f)) continue;
					const Vec3 pa = base + Vec3{double(corner[e[0]][0]), double(corner[e[0]][1]), double(corner[e[0]][2])} * cell;
					const Vec3 pb = base + Vec3{double(corner[e[1]][0]), double(corner[e[1]][1]), double(corner[e[1]][2])} * cell;
					// (where on the edge: the samples' straight line, then two halvings of the shape itself)
					double u0 = 0.0, u1 = 1.0, u = fa / (fa - fb);
					const bool aIn = fa < 0.0f;
					for (int it = 0; it < 6; ++it) {
						const bool in = f(lerp(pa, pb, u)) < 0.0;
						(in == aIn ? u0 : u1) = u;
						u = 0.5 * (u0 + u1);
					}
					const Vec3 p = lerp(pa, pb, u), nrm = sdf::normalAt(f, p, h);
					const double np = dot(nrm, p), nn[3] = {nrm.x, nrm.y, nrm.z};
					for (int r = 0; r < 3; ++r) {
						for (int q = 0; q < 3; ++q) A[r][q] += nn[r] * nn[q];
						b[r] += nn[r] * np;
					}
					mass = mass + p;
					++n;
				}
				mass = mass * (1.0 / n);
				const double lambda = 0.05 * n;   // (the pull to the mean point: decides where a flat patch's vertex sits, and is too weak to round a real corner)
				const double mm[3] = {mass.x, mass.y, mass.z};
				for (int r = 0; r < 3; ++r) A[r][r] += lambda, b[r] += lambda * mm[r];
				double x[3];
				Vec3 p = mass;
				if (solve3(A, b, x)) p = {x[0], x[1], x[2]};
				p = {std::clamp(p.x, base.x, base.x + cell), std::clamp(p.y, base.y, base.y + cell), std::clamp(p.z, base.z, base.z + cell)};
				index[at(i, j, k)] = static_cast<int>(m.verts.size());
				m.verts.push_back(p);
				m.normals.push_back(sdf::normalAt(f, p, h));
			}
	const auto quad = [&](int a, int b, int c, int d, bool flip) {
		if (a < 0 || b < 0 || c < 0 || d < 0) return;
		if (flip) std::swap(b, d);
		const auto tri = [&](int p, int q, int r) {
			if (length(cross(m.verts[q] - m.verts[p], m.verts[r] - m.verts[p])) > cell * cell * 1e-6) m.tris.push_back({p, q, r});
		};
		// split along the diagonal that bends less (the one that keeps a sharp edge an edge)
		const auto bend = [&](int p, int q, int r, int s) {
			const Vec3 n1 = cross(m.verts[q] - m.verts[p], m.verts[r] - m.verts[p]), n2 = cross(m.verts[r] - m.verts[p], m.verts[s] - m.verts[p]);
			return dot(n1, n2) / std::max(1e-30, length(n1) * length(n2));
		};
		if (bend(a, b, c, d) >= bend(b, c, d, a)) tri(a, b, c), tri(a, c, d);
		else tri(a, b, d), tri(b, c, d);
	};
	for (int k = 0; k < nz; ++k)
		for (int j = 0; j < ny; ++j)
			for (int i = 0; i < nx; ++i) {
				const bool in0 = v[at(i, j, k)] < 0.0f;
				if (i + 1 < nx && j > 0 && k > 0 && in0 != (v[at(i + 1, j, k)] < 0.0f))
					quad(index[at(i, j - 1, k - 1)], index[at(i, j, k - 1)], index[at(i, j, k)], index[at(i, j - 1, k)], !in0);
				if (j + 1 < ny && i > 0 && k > 0 && in0 != (v[at(i, j + 1, k)] < 0.0f))
					quad(index[at(i - 1, j, k - 1)], index[at(i - 1, j, k)], index[at(i, j, k)], index[at(i, j, k - 1)], !in0);
				if (k + 1 < nz && i > 0 && j > 0 && in0 != (v[at(i, j, k + 1)] < 0.0f))
					quad(index[at(i - 1, j - 1, k)], index[at(i, j - 1, k)], index[at(i, j, k)], index[at(i - 1, j, k)], !in0);
			}
	return m;
}

// Normals split where faces meet at more than `degrees` (Blender's auto smooth): each corner of each triangle takes the mean of the normals of the faces round its vertex
// that turn less than that from its own face, and a vertex is copied for each different normal.
inline Mesh autoSmooth(const Mesh &in, double degrees) {
	const double limit = std::cos(degrees * kPi / 180.0);
	std::vector<Vec3> faceN(in.tris.size());
	std::vector<std::vector<int>> around(in.verts.size());
	for (std::size_t t = 0; t < in.tris.size(); ++t) {
		const auto &tr = in.tris[t];
		const Vec3 n = cross(in.verts[tr[1]] - in.verts[tr[0]], in.verts[tr[2]] - in.verts[tr[0]]);
		faceN[t] = length(n) > 1e-30 ? normalize(n) : Vec3{0, 1, 0};
		for (int q = 0; q < 3; ++q) around[tr[q]].push_back(static_cast<int>(t));
	}
	Mesh out;
	std::map<std::pair<int, std::array<long long, 3>>, int> made;   // (a vertex and its normal, rounded: one copy each)
	for (std::size_t t = 0; t < in.tris.size(); ++t) {
		std::array<int, 3> tri{};
		for (int q = 0; q < 3; ++q) {
			const int v = in.tris[t][q];
			Vec3 n{0, 0, 0};
			for (int o : around[v])
				if (dot(faceN[o], faceN[t]) >= limit) n = n + faceN[o];
			n = length(n) > 1e-30 ? normalize(n) : faceN[t];
			const std::array<long long, 3> key{std::llround(n.x * 1e4), std::llround(n.y * 1e4), std::llround(n.z * 1e4)};
			auto it = made.find({v, key});
			if (it == made.end()) {
				it = made.emplace(std::make_pair(v, key), static_cast<int>(out.verts.size())).first;
				out.verts.push_back(in.verts[v]);
				out.normals.push_back(n);
				if (in.uvs.size() == in.verts.size()) out.uvs.push_back(in.uvs[v]);
			}
			tri[q] = it->second;
		}
		out.tris.push_back(tri);
	}
	return out;
}

}  // namespace model
}  // namespace cartoon
