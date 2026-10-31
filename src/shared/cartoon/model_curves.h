#pragma once
// model_curves.h -- modeling, part 5: shapes from curves.
//
//   sweep    a profile (a circle, a rectangle, a star, or any outline) carried along a smooth path through points (a Catmull-Rom spline): pipes, ropes, rails, cables,
//            tentacles, vines, a handrail, a pretzel. `twist A` turns the profile A degrees along the way, `taper S` narrows it to S at the end. The profile's frame is carried
//            along without spinning (rotation-minimizing frames, Wang, Juttler, Zheng and Liu, "Computation of Rotation Minimizing Frames", 2008: the double reflection), so a
//            rectangle stays flat round a bend instead of corkscrewing. A closed path (`loop`) joins up; an open one gets its ends capped.
//   loft     a skin through rings one above another, each its own width and depth and (optionally) offset: a boat's hull, a bottle, a vase, a fuselage, a wing.

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "cartoon_lib.h"
#include "sdf_mesh.h"

namespace cartoon {
namespace model {

// A profile: a closed outline in its plane (x across, y up), counter-clockwise.
inline std::vector<std::pair<double, double>> circleProfile(double r, int sides = 16) {
	std::vector<std::pair<double, double>> p;
	for (int i = 0; i < sides; ++i) p.push_back({r * std::cos(2.0 * kPi * i / sides), r * std::sin(2.0 * kPi * i / sides)});
	return p;
}
inline std::vector<std::pair<double, double>> rectProfile(double w, double h) { return {{-0.5 * w, -0.5 * h}, {0.5 * w, -0.5 * h}, {0.5 * w, 0.5 * h}, {-0.5 * w, 0.5 * h}}; }
inline std::vector<std::pair<double, double>> starProfile(int points, double outer, double inner) {
	std::vector<std::pair<double, double>> p;
	for (int i = 0; i < 2 * points; ++i) {
		const double r = i % 2 ? inner : outer, a = kPi * i / points + 0.5 * kPi;
		p.push_back({r * std::cos(a), r * std::sin(a)});
	}
	return p;
}

// A Catmull-Rom spline through `pts`, `perSpan` samples between each two (centripetal: no loops or cusps where points bunch up). `loop`: closed.
inline std::vector<Vec3> spline(const std::vector<Vec3> &pts, int perSpan, bool loop) {
	const int n = static_cast<int>(pts.size());
	if (n < 2) return pts;
	const auto P = [&](int i) { return loop ? pts[((i % n) + n) % n] : pts[std::clamp(i, 0, n - 1)]; };
	std::vector<Vec3> out;
	const int spans = loop ? n : n - 1;
	for (int s = 0; s < spans; ++s) {
		const Vec3 p0 = s == 0 && !loop ? P(0) * 2.0 - P(1) : P(s - 1), p1 = P(s), p2 = P(s + 1), p3 = s + 2 >= n && !loop ? P(n - 1) * 2.0 - P(n - 2) : P(s + 2);
		const auto knot = [](double t, Vec3 a, Vec3 b) { return t + std::sqrt(std::max(1e-9, length(b - a))); };
		const double t0 = 0.0, t1 = knot(t0, p0, p1), t2 = knot(t1, p1, p2), t3 = knot(t2, p2, p3);
		for (int k = 0; k < perSpan; ++k) {
			const double t = t1 + (t2 - t1) * k / perSpan;
			const Vec3 a1 = p0 * ((t1 - t) / (t1 - t0)) + p1 * ((t - t0) / (t1 - t0)), a2 = p1 * ((t2 - t) / (t2 - t1)) + p2 * ((t - t1) / (t2 - t1)),
					   a3 = p2 * ((t3 - t) / (t3 - t2)) + p3 * ((t - t2) / (t3 - t2));
			const Vec3 b1 = a1 * ((t2 - t) / (t2 - t0)) + a2 * ((t - t0) / (t2 - t0)), b2 = a2 * ((t3 - t) / (t3 - t1)) + a3 * ((t - t1) / (t3 - t1));
			out.push_back(b1 * ((t2 - t) / (t2 - t1)) + b2 * ((t - t1) / (t2 - t1)));
		}
	}
	if (!loop) out.push_back(pts.back());
	return out;
}

struct Sweep {
	std::vector<std::pair<double, double>> profile;
	std::vector<Vec3> path;
	double twist = 0.0, taper = 1.0;
	bool loop = false;
};
inline Mesh sweepMesh(const Sweep &sw) {
	Mesh m;
	const std::vector<Vec3> c = spline(sw.path, 12, sw.loop);
	const int n = static_cast<int>(c.size()), k = static_cast<int>(sw.profile.size());
	if (n < 2 || k < 2) return m;
	const auto tangentAt = [&](int i) {
		const Vec3 a = sw.loop ? c[(i + n - 1) % n] : c[std::max(0, i - 1)], b = sw.loop ? c[(i + 1) % n] : c[std::min(n - 1, i + 1)];
		return normalize(b - a);
	};
	// the first frame: any normal to the first tangent; then the double reflection carries it along
	Vec3 t = tangentAt(0), r = std::abs(t.y) < 0.9 ? normalize(cross(t, Vec3{0, 1, 0})) : normalize(cross(t, Vec3{1, 0, 0}));
	std::vector<Vec3> rs(n), ts(n);
	rs[0] = r, ts[0] = t;
	for (int i = 0; i + 1 < n; ++i) {
		const Vec3 v1 = c[i + 1] - c[i];
		const double c1 = dot(v1, v1);
		if (c1 < 1e-18) {
			rs[i + 1] = rs[i], ts[i + 1] = ts[i];
			continue;
		}
		const Vec3 rL = rs[i] - v1 * (2.0 / c1 * dot(v1, rs[i])), tL = ts[i] - v1 * (2.0 / c1 * dot(v1, ts[i]));
		const Vec3 t1 = tangentAt(i + 1), v2 = t1 - tL;
		const double c2 = dot(v2, v2);
		rs[i + 1] = c2 < 1e-18 ? rL : rL - v2 * (2.0 / c2 * dot(v2, rL)), ts[i + 1] = t1;
	}
	for (int i = 0; i < n; ++i) {
		const double u = static_cast<double>(i) / (n - 1), a = sw.twist * kPi / 180.0 * u, s = lerp(1.0, sw.taper, u);
		const Vec3 x0 = rs[i], y0 = cross(ts[i], rs[i]);
		const Vec3 x = x0 * std::cos(a) + y0 * std::sin(a), y = y0 * std::cos(a) - x0 * std::sin(a);
		for (const auto &[px, py] : sw.profile) m.verts.push_back(c[i] + (x * px + y * py) * s);
	}
	const int rings = sw.loop ? n : n - 1;
	for (int i = 0; i < rings; ++i)
		for (int j = 0; j < k; ++j) {
			const int a = i * k + j, b = i * k + (j + 1) % k, cc = ((i + 1) % n) * k + (j + 1) % k, d = ((i + 1) % n) * k + j;
			m.tris.push_back({a, cc, d}), m.tris.push_back({a, b, cc});
		}
	if (!sw.loop && k >= 3) {   // the two ends capped (a fan from each end's middle)
		for (int end = 0; end < 2; ++end) {
			const int ring = end == 0 ? 0 : n - 1, centre = static_cast<int>(m.verts.size());
			Vec3 mid{0, 0, 0};
			for (int j = 0; j < k; ++j) mid = mid + m.verts[ring * k + j];
			m.verts.push_back(mid * (1.0 / k));
			for (int j = 0; j < k; ++j) {
				const int a = ring * k + j, b = ring * k + (j + 1) % k;
				end == 0 ? m.tris.push_back({centre, b, a}) : m.tris.push_back({centre, a, b});
			}
		}
	}
	sdf::computeNormals(m);
	return m;
}

// A loft: rings at heights y, each rx wide and rz deep about (x, z), all of the same profile (a circle of `sides`, or a square with `square`).
struct Ring {
	double y = 0.0, rx = 0.5, rz = 0.5, x = 0.0, z = 0.0;
};
struct Loft {
	std::vector<Ring> rings;
	int sides = 24;
	bool square = false, capTop = true, capBottom = true;
};
inline Mesh loftMesh(const Loft &lf) {
	Mesh m;
	std::vector<Ring> rings = lf.rings;
	std::sort(rings.begin(), rings.end(), [](const Ring &a, const Ring &b) { return a.y < b.y; });
	const int n = static_cast<int>(rings.size()), k = lf.square ? 4 * std::max(1, lf.sides / 4) : std::max(3, lf.sides);
	if (n < 2) return m;
	for (const Ring &r : rings)
		for (int j = 0; j < k; ++j) {
			const double a = 2.0 * kPi * j / k;
			double cx = std::cos(a), cz = std::sin(a);
			if (lf.square) {   // (a circle pushed out onto its square)
				const double big = std::max(std::abs(cx), std::abs(cz));
				cx /= big, cz /= big;
			}
			m.verts.push_back({r.x + r.rx * cx, r.y, r.z + r.rz * cz});
		}
	for (int i = 0; i + 1 < n; ++i)
		for (int j = 0; j < k; ++j) {
			const int a = i * k + j, b = i * k + (j + 1) % k, c = (i + 1) * k + (j + 1) % k, d = (i + 1) * k + j;
			m.tris.push_back({a, c, b}), m.tris.push_back({a, d, c});
		}
	for (int end = 0; end < 2; ++end) {
		if ((end == 0 && !lf.capBottom) || (end == 1 && !lf.capTop)) continue;
		const int ring = end == 0 ? 0 : n - 1, centre = static_cast<int>(m.verts.size());
		m.verts.push_back({rings[ring].x, rings[ring].y, rings[ring].z});
		for (int j = 0; j < k; ++j) {
			const int a = ring * k + j, b = ring * k + (j + 1) % k;
			end == 0 ? m.tris.push_back({centre, a, b}) : m.tris.push_back({centre, b, a});
		}
	}
	sdf::computeNormals(m);
	return m;
}

}  // namespace model
}  // namespace cartoon
