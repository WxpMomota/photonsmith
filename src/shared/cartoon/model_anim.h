#pragma once
// model_anim.h -- modeling round 3, parts 3 and 4: models whose shapes change over time, and mechanisms. All are film lines naming a placement (`place ... as ID`) or
// one of its pieces (ID.PIECE):
//
//   flex ID.PIECE bend|twist|taper|stretch|squash  T V  T V ...   a modifier whose amount is keyed over time: a neck that stretches, a jelly that squashes, a stalk that
//                                                    bends in the wind (bend and twist in degrees, taper a factor at the top, stretch its height times V, squash its
//                                                    height times V with its width made up so its volume stays)
//   morph ID to MODEL from T0 to T1                  the placed model melts into another, any two shapes (their distance fields blended, model_boolean.h)
//   build ID from T0 to T1 [rise|pop|pieces]         it grows into being: rising out of the ground, popping up with an overshoot, or piece after piece
//   vanish ID from T0 to T1                          it breaks up into its triangles, falling away in a scatter
//   wobble ID.PIECE [AMOUNT]                         the piece lags behind as the model moves and springs back: a jelly, a belly, antennae
//
//   spin ID.PIECE about X Y Z axis X Y Z rpm R [from T0] [to T1]       a wheel, a fan, a propeller, a clock's hand turning
//   gear ID.PIECE driven-by ID.PIECE ratio K about X Y Z axis X Y Z    turning as a meshed gear does: the other way, K times as fast
//   piston ID.PIECE axis X Y Z stroke S rpm R                          sliding back and forth
//   roll ID.PIECE about X Y Z axis X Y Z radius R                      a wheel turning as far as the placement has travelled (by its `move` or `route`)
//   route ID through X Y Z X Y Z ... from T0 to T1                     the placement driven along a smooth path, facing its way
//   aim ID.PIECE at X Y Z | at NAME [about X Y Z]                      the piece turned to face a point or a character (a turret, a head, a lamp)
//   attach ID to NAME [head|hand|chest] [offset X Y Z]                 the placement carried by a character: a hat, a sword, a balloon
//
// Coordinates of `about` and `offset` are the model's own; `through`, `at X Y Z` are the world's (pbrt's).

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "cartoon_lib.h"
#include "model_boolean.h"
#include "model_curves.h"
#include "model_modifiers.h"

namespace cartoon {
namespace model {

struct Flex {
	std::string name, kind;
	std::vector<std::pair<double, double>> keys;
};
struct Morph {
	std::string id, to;
	double t0 = 0.0, t1 = 1.0;
};
struct Build {
	std::string id, style = "rise";
	double t0 = 0.0, t1 = 1.0;
};
struct Vanish {
	std::string id;
	double t0 = 0.0, t1 = 1.0;
};
struct Wobble {
	std::string name;
	double amount = 1.0;
};
struct Mech {
	std::string kind, name;            // spin, gear, piston, roll, route, aim, attach
	Vec3 about{0, 0, 0}, axis{0, 1, 0}, at{0, 0, 0}, offset{0, 0, 0};
	double rpm = 0.0, ratio = 1.0, stroke = 0.1, radius = 0.3, t0 = -1e30, t1 = 1e30;
	std::string driver, target, where = "head";   // (gear's driver; aim's or attach's character; attach's place on it)
	std::vector<Vec3> path;
};
struct Animated {
	std::vector<Flex> flexes;
	std::vector<Morph> morphs;
	std::vector<Build> builds;
	std::vector<Vanish> vanishes;
	std::vector<Wobble> wobbles;
	std::vector<Mech> mechs;
	bool names(const std::string &n) const {
		const auto has = [&](const auto &v) { return std::any_of(v.begin(), v.end(), [&](const auto &x) { return x.name == n; }); };
		const auto hasId = [&](const auto &v) { return std::any_of(v.begin(), v.end(), [&](const auto &x) { return x.id == n; }); };
		return has(flexes) || has(wobbles) || has(mechs) || hasId(morphs) || hasId(builds) || hasId(vanishes);
	}
};

// A keyed amount at t (linear between keys, held before the first and after the last).
inline double keyedAt(const std::vector<std::pair<double, double>> &keys, double t) {
	if (keys.empty()) return 0.0;
	if (t <= keys.front().first) return keys.front().second;
	for (std::size_t i = 1; i < keys.size(); ++i)
		if (t <= keys[i].first) return lerp(keys[i - 1].second, keys[i].second, (t - keys[i - 1].first) / std::max(1e-9, keys[i].first - keys[i - 1].first));
	return keys.back().second;
}
// The mesh flexed by `kind` at amount `v`.
inline Mesh flexed(const Mesh &m, const std::string &kind, double v) {
	if (kind == "bend" || kind == "twist" || kind == "taper") return applied(m, {kind, {v}, "z"});
	Vec3 lo, hi;
	boxOf(m, lo, hi);
	const double k = std::max(0.01, v), side = kind == "squash" ? 1.0 / std::sqrt(k) : 1.0;
	const Vec3 base{0.5 * (lo.x + hi.x), lo.y, 0.5 * (lo.z + hi.z)};
	return transformed(m, mul(translate(base), scale(side, k, side), translate(base * -1.0)));
}
// Two shapes melted into each other at u (0 the first, 1 the second): their distance fields blended and meshed again on `cell` cubes.
inline Mesh morphed(const Mesh &a, const Mesh &b, double u, double cell) {
	if (u <= 0.0) return a;
	if (u >= 1.0) return b;
	const MeshField fa = meshField(a, cell), fb = meshField(b, cell);
	Vec3 lo, hi, lo2, hi2;
	boxOf(a, lo, hi), boxOf(b, lo2, hi2);
	lo = {std::min(lo.x, lo2.x), std::min(lo.y, lo2.y), std::min(lo.z, lo2.z)}, hi = {std::max(hi.x, hi2.x), std::max(hi.y, hi2.y), std::max(hi.z, hi2.z)};
	const Vec3 pad{2 * cell, 2 * cell, 2 * cell};
	return dualContour([&](Vec3 p) { return lerp(fa(p), fb(p), u); }, lo - pad, hi + pad, cell);
}
// A piece growing into being at u (0 nothing, 1 whole): `rise` up out of the ground, `pop` up with an overshoot; `pieces` pops each piece (k of n) in its own slice.
inline Mat4 buildMatrix(const std::string &style, double u, int k, int n, Vec3 lo, Vec3 hi) {
	if (style == "pieces") u = std::clamp(u * n - k, 0.0, 1.0);
	const Vec3 base{0.5 * (lo.x + hi.x), lo.y, 0.5 * (lo.z + hi.z)};
	if (style == "rise") return mul(translate(base), scale(1.0, std::max(1e-4, smoothstep(0.0, 1.0, u)), 1.0), translate(base * -1.0));
	const double s = u <= 0.0 ? 1e-4 : u >= 1.0 ? 1.0 : 1.0 + 2.7 * std::pow(u - 1.0, 3.0) + 1.7 * std::pow(u - 1.0, 2.0);   // (an ease that overshoots: back out)
	return mul(translate(base), scale(std::max(1e-4, s)), translate(base * -1.0));
}
// The mesh breaking up at u: each triangle falls away at its own moment (seeded), drifting down and out from the middle and shrinking as it goes.
inline Mesh vanished(const Mesh &m, double u, int seed) {
	if (u <= 0.0) return m;
	Mesh out;
	Vec3 lo, hi;
	boxOf(m, lo, hi);
	const Vec3 mid = (lo + hi) * 0.5;
	for (std::size_t t = 0; t < m.tris.size(); ++t) {
		const double start = 0.85 * hash01(static_cast<int>(t) * 7919 + seed, 1), local = std::clamp((u - start) / 0.15, 0.0, 1.0);
		if (local >= 1.0) continue;
		const Vec3 c = (m.verts[m.tris[t][0]] + m.verts[m.tris[t][1]] + m.verts[m.tris[t][2]]) * (1.0 / 3.0);
		const Vec3 drift = normalize(c - mid + Vec3{1e-6, 0, 0}) * (0.3 * local) + Vec3{0, -0.4 * local * local, 0};
		const int base = static_cast<int>(out.verts.size());
		for (int q = 0; q < 3; ++q) out.verts.push_back(c + (m.verts[m.tris[t][q]] - c) * (1.0 - local) + drift);
		out.tris.push_back({base, base + 1, base + 2});
	}
	sdf::computeNormals(out);
	return out;
}
// The mesh bent by a lag `offset` (the model's space), most at its top: what wobbles behind as it moves.
inline Mesh wobbled(const Mesh &m, Vec3 offset) {
	Mesh out = m;
	Vec3 lo, hi;
	boxOf(m, lo, hi);
	const double H = std::max(1e-6, hi.y - lo.y);
	for (Vec3 &p : out.verts) {
		const double h = (p.y - lo.y) / H;
		p = p + offset * (h * h);
	}
	sdf::computeNormals(out);
	return out;
}
// Where along a route (a Catmull-Rom spline through `path`, pbrt's) the placement is at u (0..1), and the way it faces there (degrees about the upright, from +z).
inline Vec3 routeAt(const std::vector<Vec3> &path, double u, double &heading) {
	const std::vector<Vec3> curve = spline(path, 24, false);
	if (curve.size() < 2) {
		heading = 0.0;
		return path.empty() ? Vec3{0, 0, 0} : path[0];
	}
	// (by distance along it, not by spline parameter)
	std::vector<double> run(curve.size(), 0.0);
	for (std::size_t i = 1; i < curve.size(); ++i) run[i] = run[i - 1] + length(curve[i] - curve[i - 1]);
	const double target = std::clamp(u, 0.0, 1.0) * run.back();
	std::size_t i = 1;
	while (i + 1 < curve.size() && run[i] < target) ++i;
	const double f = (target - run[i - 1]) / std::max(1e-9, run[i] - run[i - 1]);
	const Vec3 d = curve[i] - curve[i - 1];
	heading = std::atan2(d.x, d.z) * 180.0 / kPi;
	return lerp(curve[i - 1], curve[i], f);
}

}  // namespace model
}  // namespace cartoon
