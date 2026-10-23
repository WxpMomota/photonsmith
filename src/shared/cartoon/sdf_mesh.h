#pragma once
// sdf_mesh.h -- sculpted shapes for the cartoon characters. A shape is a signed distance function (negative inside) built from spheres, ellipsoids, tapered
// capsules and rounded boxes, blended into one another with a smooth minimum, so a nose grows out of a face and an arm out of a shoulder the way clay would.
// Two meshers turn a shape into a triangle mesh with smooth normals taken from the function's gradient:
//   radialMesh  -- for a shape that every ray from a centre leaves once (a head with its muzzle, cheeks, nose and ears): a fine latitude-longitude grid of rays,
//                  each marched in from outside to the surface. Few triangles, a clean silhouette even in a close-up.
//   surfaceNets -- for anything (a bent leg with its foot and toes, a hand with fingers, a sweater): the surface crossing each cell of a grid, every vertex then
//                  pulled onto the true surface.
// The functions are written in whatever frame the caller likes (usually the world: the meshes are emitted with identity()).

#include <algorithm>
#include <cmath>
#include <vector>

#include "cartoon_lib.h"

namespace cartoon {
namespace sdf {

// ---- blending -------------------------------------------------------------------------------------------------------------------------------------------------------
// A smooth minimum: the union of two shapes with a fillet about `k` wide where they meet (k = 0: a sharp union). Never above min(a, b), so it stays a distance bound.
inline double smin(double a, double b, double k) {
	if (k <= 0.0) return std::min(a, b);
	const double h = std::max(k - std::abs(a - b), 0.0) / k;
	return std::min(a, b) - h * h * k * 0.25;
}
inline double smax(double a, double b, double k) { return -smin(-a, -b, k); }

// ---- primitives -----------------------------------------------------------------------------------------------------------------------------------------------------
inline double sphere(Vec3 p, Vec3 c, double r) { return length(p - c) - r; }

// An ellipsoid with radii `r` along the axes of `frame` (a rigid frame: its inverse is passed, so many evaluations share one inversion). A close bound, not exact.
inline double ellipsoid(Vec3 p, const Mat4 &inverseFrame, Vec3 r) {
	const Vec3 q = transformPoint(inverseFrame, p);
	const double k0 = length({q.x / r.x, q.y / r.y, q.z / r.z});
	const double k1 = length({q.x / (r.x * r.x), q.y / (r.y * r.y), q.z / (r.z * r.z)});
	return k1 > 1e-12 ? k0 * (k0 - 1.0) / k1 : -std::min(r.x, std::min(r.y, r.z));
}
inline double ellipsoid(Vec3 p, Vec3 c, Vec3 r) { return ellipsoid(p, translate(c * -1.0), r); }

// A tapered capsule: a sphere of radius ra at a joined smoothly to a sphere of radius rb at b (a limb: thick at the shoulder, thinner at the wrist).
inline double roundCone(Vec3 p, Vec3 a, Vec3 b, double ra, double rb) {
	const Vec3 ba = b - a;
	const double l2 = dot(ba, ba);
	if (l2 < 1e-14) return length(p - a) - std::max(ra, rb);
	const double rr = ra - rb, a2 = l2 - rr * rr, il2 = 1.0 / l2;
	const Vec3 pa = p - a;
	const double y = dot(pa, ba), z = y - l2;
	const Vec3 w = pa * l2 - ba * y;
	const double x2 = dot(w, w), y2 = y * y * l2, z2 = z * z * l2;
	const double k = (rr > 0.0 ? 1.0 : (rr < 0.0 ? -1.0 : 0.0)) * rr * rr * x2;
	if ((z > 0.0 ? 1.0 : -1.0) * a2 * z2 > k) return std::sqrt(x2 + z2) * il2 - rb;
	if ((y > 0.0 ? 1.0 : -1.0) * a2 * y2 < k) return std::sqrt(x2 + y2) * il2 - ra;
	return (std::sqrt(x2 * a2 * il2) + y * rr) * il2 - ra;
}
inline double capsule(Vec3 p, Vec3 a, Vec3 b, double r) {
	const Vec3 pa = p - a, ba = b - a;
	const double h = clamp01(dot(pa, ba) / std::max(dot(ba, ba), 1e-14));
	return length(pa - ba * h) - r;
}

// A box with half sizes `half` and edges rounded by `round`, in a rigid frame (inverse passed).
inline double roundBox(Vec3 p, const Mat4 &inverseFrame, Vec3 half, double round) {
	const Vec3 q0 = transformPoint(inverseFrame, p);
	const Vec3 q = {std::abs(q0.x) - half.x + round, std::abs(q0.y) - half.y + round, std::abs(q0.z) - half.z + round};
	const Vec3 m = {std::max(q.x, 0.0), std::max(q.y, 0.0), std::max(q.z, 0.0)};
	return length(m) + std::min(std::max(q.x, std::max(q.y, q.z)), 0.0) - round;
}

// ---- gradients and the surface ----------------------------------------------------------------------------------------------------------------------------------------
template <typename F>
inline Vec3 gradient(const F &f, Vec3 p, double h) {
	return {f({p.x + h, p.y, p.z}) - f({p.x - h, p.y, p.z}), f({p.x, p.y + h, p.z}) - f({p.x, p.y - h, p.z}), f({p.x, p.y, p.z + h}) - f({p.x, p.y, p.z - h})};
}
template <typename F>
inline Vec3 normalAt(const F &f, Vec3 p, double h) {
	return normalize(gradient(f, p, h));
}
// Newton steps from p onto the surface (along the gradient), never moving further than `maxMove` in all.
template <typename F>
inline Vec3 project(const F &f, Vec3 p, double h, double maxMove, int steps = 3) {
	const Vec3 start = p;
	for (int i = 0; i < steps; ++i) {
		const double d = f(p);
		const Vec3 g = gradient(f, p, h) * (0.5 / h);
		const double gg = dot(g, g);
		if (gg < 1e-12) break;
		p = p - g * (d / gg);
	}
	const Vec3 moved = p - start;
	if (length(moved) > maxMove) p = start + normalize(moved) * maxMove;
	return p;
}

// Where the ray from `centre` in direction `dir` leaves the shape (marched in from `rMax` outside it), or centre + dir * rMin when it misses.
template <typename F>
inline Vec3 radialHit(const F &f, Vec3 centre, Vec3 dir, double rMax, double rMin = 0.0) {
	double t = rMax, outside = rMax;
	const double tol = rMax * 1e-5;
	for (int i = 0; i < 200 && t > rMin; ++i) {
		const double d = f(centre + dir * t);
		if (d < tol) break;
		outside = t;
		t -= std::max(d, tol);
	}
	if (t <= rMin) return centre + dir * rMin;
	// bisection between the last point outside and the first inside
	double lo = t, hi = outside;
	for (int i = 0; i < 24; ++i) {
		const double mid = 0.5 * (lo + hi);
		(f(centre + dir * mid) > 0.0 ? hi : lo) = mid;
	}
	return centre + dir * (0.5 * (lo + hi));
}

// ---- meshers ------------------------------------------------------------------------------------------------------------------------------------------------------------
// A star-shaped shape round `centre`: nu rays round the vertical axis `up` by nv from pole to pole, in the frame whose +y is `up` and +x is `forward`.
template <typename F>
inline Mesh radialMesh(const F &f, Vec3 centre, double rMax, int nu, int nv, Vec3 up = {0.0, 1.0, 0.0}, Vec3 forward = {1.0, 0.0, 0.0}) {
	const Vec3 Y = normalize(up), X = normalize(forward - Y * dot(forward, Y)), Z = cross(X, Y);
	const double h = rMax * 2e-3;
	Mesh m;
	for (int j = 0; j <= nv; ++j) {
		const double th = kPi * j / nv;   // 0 at the top pole
		for (int i = 0; i <= nu; ++i) {
			const double ph = 2.0 * kPi * i / nu;
			const Vec3 dir = Y * std::cos(th) + (X * std::cos(ph) + Z * std::sin(ph)) * std::sin(th);
			const Vec3 p = radialHit(f, centre, dir, rMax, rMax * 0.02);
			m.verts.push_back(p);
			m.normals.push_back(normalAt(f, p, h));
		}
	}
	const int row = nu + 1;
	for (int j = 0; j < nv; ++j)
		for (int i = 0; i < nu; ++i) {
			const int a = j * row + i, b = a + 1, c = a + row, d = c + 1;
			if (j > 0) m.tris.push_back({a, b, c});
			if (j < nv - 1) m.tris.push_back({b, d, c});
		}
	return m;
}

// Any shape inside the box [lo, hi], meshed on a grid of `cell` sized cubes (naive surface nets, with every vertex pulled onto the surface).
template <typename F>
inline Mesh surfaceNets(const F &f, Vec3 lo, Vec3 hi, double cell) {
	const int nx = std::max(2, static_cast<int>(std::ceil((hi.x - lo.x) / cell)) + 1), ny = std::max(2, static_cast<int>(std::ceil((hi.y - lo.y) / cell)) + 1),
			  nz = std::max(2, static_cast<int>(std::ceil((hi.z - lo.z) / cell)) + 1);
	const auto at = [&](int i, int j, int k) { return (static_cast<std::size_t>(k) * ny + j) * nx + i; };
	std::vector<float> v(static_cast<std::size_t>(nx) * ny * nz);
	for (int k = 0; k < nz; ++k)
		for (int j = 0; j < ny; ++j)
			for (int i = 0; i < nx; ++i) v[at(i, j, k)] = static_cast<float>(f(lo + Vec3{i * cell, j * cell, k * cell}));
	Mesh m;
	std::vector<int> index(v.size(), -1);   // (the vertex of cell i, j, k, stored at its lowest corner)
	static const int corner[8][3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0}, {0, 0, 1}, {1, 0, 1}, {0, 1, 1}, {1, 1, 1}};
	static const int edge[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
	const double h = cell * 0.25;
	for (int k = 0; k < nz - 1; ++k)
		for (int j = 0; j < ny - 1; ++j)
			for (int i = 0; i < nx - 1; ++i) {
				float c[8];
				int inside = 0;
				for (int q = 0; q < 8; ++q) {
					c[q] = v[at(i + corner[q][0], j + corner[q][1], k + corner[q][2])];
					inside += c[q] < 0.0f;
				}
				if (inside == 0 || inside == 8) continue;
				Vec3 sum{};
				int n = 0;
				for (const auto &e : edge) {
					const float a = c[e[0]], b = c[e[1]];
					if ((a < 0.0f) == (b < 0.0f)) continue;
					const double u = a / (a - b);
					const Vec3 pa = {double(corner[e[0]][0]), double(corner[e[0]][1]), double(corner[e[0]][2])}, pb = {double(corner[e[1]][0]), double(corner[e[1]][1]), double(corner[e[1]][2])};
					sum = sum + lerp(pa, pb, u);
					++n;
				}
				Vec3 p = lo + (Vec3{double(i), double(j), double(k)} + sum * (1.0 / n)) * cell;
				p = project(f, p, h, cell * 0.9);
				index[at(i, j, k)] = static_cast<int>(m.verts.size());
				m.verts.push_back(p);
				m.normals.push_back(normalAt(f, p, h));
			}
	// one quad round every grid edge the surface crosses (the four cells that share the edge), wound so that it faces out
	const auto quad = [&](int a, int b, int c, int d, bool flip) {
		if (a < 0 || b < 0 || c < 0 || d < 0) return;
		if (flip) std::swap(b, d);
		// (a vertex pulled onto the surface can land on or next to a neighbour's: no triangle that is degenerate here, or once the scene text rounds its corners to
		// 10 micrometres)
		const auto tri = [&](int p, int q, int r) {
			const Vec3 &vp = m.verts[static_cast<std::size_t>(p)], &vq = m.verts[static_cast<std::size_t>(q)], &vr = m.verts[static_cast<std::size_t>(r)];
			const double shortest = std::min(length(vq - vp), std::min(length(vr - vq), length(vp - vr)));
			if (shortest > std::max(4e-5, cell * 0.02) && length(cross(vq - vp, vr - vp)) > cell * cell * 1e-4) m.tris.push_back({p, q, r});
		};
		// split along the shorter diagonal
		if (length(m.verts[static_cast<std::size_t>(a)] - m.verts[static_cast<std::size_t>(c)]) <= length(m.verts[static_cast<std::size_t>(b)] - m.verts[static_cast<std::size_t>(d)])) {
			tri(a, b, c);
			tri(a, c, d);
		} else {
			tri(a, b, d);
			tri(b, c, d);
		}
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

// The vertex normals of a mesh from its triangles (area weighted), for meshes built by hand.
inline void computeNormals(Mesh &m) {
	m.normals.assign(m.verts.size(), Vec3{});
	for (const auto &t : m.tris) {
		const Vec3 n = cross(m.verts[static_cast<std::size_t>(t[1])] - m.verts[static_cast<std::size_t>(t[0])], m.verts[static_cast<std::size_t>(t[2])] - m.verts[static_cast<std::size_t>(t[0])]);
		for (int q = 0; q < 3; ++q) m.normals[static_cast<std::size_t>(t[q])] = m.normals[static_cast<std::size_t>(t[q])] + n;
	}
	for (Vec3 &n : m.normals) n = length(n) > 1e-20 ? normalize(n) : Vec3{0.0, 1.0, 0.0};
}

// A smooth tube along a polyline (a drawn line on a face: a smile, a brow), `r` thick in the middle and tapering to `r * endScale` at both ends, with round ends.
inline Mesh tubeMesh(const std::vector<Vec3> &pts, double r, double endScale = 0.55, int sides = 10) {
	Mesh m;
	const int n = static_cast<int>(pts.size());
	if (n < 2) return m;
	const auto radiusAt = [&](int i) {
		const double u = static_cast<double>(i) / (n - 1);
		return r * lerp(endScale, 1.0, std::sin(kPi * u));
	};
	Vec3 prevSide{};
	for (int i = 0; i < n; ++i) {
		const Vec3 t = normalize(pts[static_cast<std::size_t>(std::min(i + 1, n - 1))] - pts[static_cast<std::size_t>(std::max(i - 1, 0))]);
		Vec3 side = i == 0 ? cross(t, Vec3{0.0, 1.0, 0.0}) : prevSide - t * dot(prevSide, t);
		if (length(side) < 1e-6) side = cross(t, Vec3{1.0, 0.0, 0.0});
		side = normalize(side);
		prevSide = side;
		const Vec3 up = cross(side, t);
		const double ri = radiusAt(i);
		for (int k = 0; k < sides; ++k) {
			const double a = 2.0 * kPi * k / sides;
			const Vec3 d = side * std::cos(a) + up * std::sin(a);
			m.verts.push_back(pts[static_cast<std::size_t>(i)] + d * ri);
			m.normals.push_back(d);
		}
	}
	for (int i = 0; i + 1 < n; ++i)
		for (int k = 0; k < sides; ++k) {
			const int a = i * sides + k, b = i * sides + (k + 1) % sides, c = a + sides, d = b + sides;
			m.tris.push_back({a, c, b});
			m.tris.push_back({b, c, d});
		}
	// round ends: a small fan to a point just past each end
	for (int e = 0; e < 2; ++e) {
		const int ring = e == 0 ? 0 : (n - 1) * sides;
		const Vec3 dir = e == 0 ? normalize(pts[0] - pts[1]) : normalize(pts[static_cast<std::size_t>(n - 1)] - pts[static_cast<std::size_t>(n - 2)]);
		const int tip = static_cast<int>(m.verts.size());
		m.verts.push_back(pts[static_cast<std::size_t>(e == 0 ? 0 : n - 1)] + dir * radiusAt(e == 0 ? 0 : n - 1));
		m.normals.push_back(dir);
		for (int k = 0; k < sides; ++k) {
			const int a = ring + k, b = ring + (k + 1) % sides;
			if (e == 0) m.tris.push_back({tip, a, b});
			else m.tris.push_back({tip, b, a});
		}
	}
	return m;
}

// A decal that lies on the surface of a shape: an ellipse with half sizes (rx, ry) along `xAxis` and its perpendicular in the surface, centred where the ray from `centre`
// in direction `dir` meets the shape, every vertex pushed onto the surface (seen from `centre`) and lifted `lift` off it. `rim` (0..1) flattens the ellipse's upper half
// into a "D" (an open mouth).
template <typename F>
inline Mesh decal(const F &f, Vec3 centre, Vec3 dir, Vec3 xAxis, double rx, double ry, double lift, double rMax, double flatTop = 0.0, int rings = 6, int around = 28) {
	Mesh m;
	const Vec3 c = radialHit(f, centre, normalize(dir), rMax);
	const Vec3 nrm = normalAt(f, c, rMax * 2e-3);
	const Vec3 X = normalize(xAxis - nrm * dot(xAxis, nrm)), Y = cross(nrm, X);
	const auto place = [&](double u, double v) {
		const Vec3 q = c + X * u + Y * v;
		const Vec3 s = radialHit(f, centre, normalize(q - centre), rMax);
		const Vec3 n = normalAt(f, s, rMax * 2e-3);
		m.verts.push_back(s + n * lift);
		m.normals.push_back(n);
	};
	place(0.0, 0.0);
	for (int r = 1; r <= rings; ++r)
		for (int a = 0; a < around; ++a) {
			const double s = static_cast<double>(r) / rings, ang = 2.0 * kPi * a / around;
			double v = ry * s * std::sin(ang);
			if (v > 0.0) v *= 1.0 - flatTop;
			place(rx * s * std::cos(ang), v);
		}
	for (int a = 0; a < around; ++a) m.tris.push_back({0, 1 + a, 1 + (a + 1) % around});
	for (int r = 1; r < rings; ++r)
		for (int a = 0; a < around; ++a) {
			const int i0 = 1 + (r - 1) * around, i1 = 1 + r * around, b = (a + 1) % around;
			m.tris.push_back({i0 + a, i1 + a, i0 + b});
			m.tris.push_back({i0 + b, i1 + a, i1 + b});
		}
	return m;
}

// The box round a set of points, grown by `pad` (for surfaceNets).
inline void boundsOf(const std::vector<Vec3> &pts, double pad, Vec3 &lo, Vec3 &hi) {
	lo = hi = pts.front();
	for (const Vec3 &p : pts) {
		lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
		hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
	}
	lo = lo - Vec3{pad, pad, pad};
	hi = hi + Vec3{pad, pad, pad};
}

}  // namespace sdf
}  // namespace cartoon
