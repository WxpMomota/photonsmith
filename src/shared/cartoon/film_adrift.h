#pragma once
// film_adrift.h -- things the water carries (water round 5): `adrift KIND NAME at X Z [river N] [from T] [count N] [size S] [seed K]`: a leaf, a paper boat, a log, a bottle with a note in it, a
// rubber duck, a crate or a paper lantern.
//
//   on a river  it goes with the water where it is (faster in the middle), wandering a little across, turning as the water shears past it. A rock turns it aside; one in the way may hold it
//               a few seconds until the water works it off; in the slack behind a rock it may circle before the stream takes it again. Its way is worked out once, when the film is
//               loaded, and is the same every run: a shot can `follow` it by name, low and close, for a whole journey down the river.
//   on a sea    it drifts slowly the way the wind goes, and all of them bob and tip on the water under them.
//
// Coordinates: pbrt's in a film file; meshes in story space (x mirrored).

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "cartoon_lib.h"
#include "film_craft.h"
#include "film_stir.h"
#include "film_stream.h"
#include "film_water.h"

namespace cartoon {
namespace adrift {

struct Thing {
	std::string kind = "leaf", name;
	double x = 0.0, z = 0.0, from = 0.0, size = 1.0;
	int river = -1, count = 1, seed = 1;
	struct Step {
		double s, c, spin;   // (on a river: metres along and across; on a sea: x and z)
	};
	std::vector<std::vector<Step>> ways;   // (each of them, a step a tenth of a second from `from`)
};
inline const std::vector<std::string> &kinds() {
	static const std::vector<std::string> k = {"leaf", "paperboat", "log", "bottle", "duck", "crate", "lantern"};
	return k;
}
constexpr double kStep = 0.1;

// Works out a thing's way down a river, up to `seconds` long.
inline std::vector<Thing::Step> wayDown(const water::River &r, double x, double z, int seed, double seconds) {
	std::vector<Thing::Step> way;
	if (r.path.size() < 2 || r.along.empty()) return way;
	std::size_t best = 0;
	double closest = 1e30;
	for (std::size_t i = 0; i < r.path.size(); ++i) {
		const double d = std::hypot(r.path[i].x - x, r.path[i].z - z);
		if (d < closest) closest = d, best = i;
	}
	double s = r.along[best], spin = 360.0 * hash01(seed, 1);
	const stream::Place p0 = stream::placeAt(r, s);
	double c = (x - p0.at.x) * p0.side.x + (z - p0.at.z) * p0.side.z, hold = 0.0, circle = 0.0, circleAngle = 0.0, circleS = 0.0, circleC = 0.0;
	const double L = r.along.back();
	std::vector<char> used(r.stones.size(), 0);
	for (double t = 0.0; t < seconds; t += kStep) {
		way.push_back({s, c, spin});
		const double width = stream::widthAt(r, s), v = stream::speedAt(r, c, width);
		if (s >= L - 0.2) continue;
		if (hold > 0.0) {   // held against a rock until the water works it off
			hold -= kStep;
			spin += 18.0 * kStep * std::sin(3.0 * t);
			continue;
		}
		if (circle > 0.0) {   // round in the slack behind a rock
			circle -= kStep;
			circleAngle += 1.4 * kStep;
			s = circleS + 0.5 * std::sin(circleAngle), c = circleC + 0.3 * (1.0 - std::cos(circleAngle));
			spin += 40.0 * kStep;
			continue;
		}
		double push = 0.5 * std::sin(0.31 * t + seed) * 0.12;   // (it wanders)
		for (std::size_t n = 0; n < r.stones.size(); ++n) {
			const water::Rock &q = r.stones[n];
			const double ahead = q.s - s, d = c - q.c, reach = 1.6 * q.r + 0.1;
			if (ahead > 0.0 && ahead < 8.0 * q.r && std::abs(d) < reach) {
				push += (d >= 0.0 ? 1.0 : -1.0) * (reach - std::abs(d)) * v / (ahead + q.r);
				if (!used[n] && ahead < 1.1 * q.r && std::abs(d) < 0.5 * q.r) {
					used[n] = 1;
					if (hash01(seed + static_cast<int>(n), 11) < 0.6) hold = 2.0 + 5.0 * hash01(seed + static_cast<int>(n), 12);
				}
			}
			if (ahead < 0.0 && ahead > -3.0 * q.r && std::abs(d) < q.r && !used[n] && hash01(seed + static_cast<int>(n), 13) < 0.4) {
				used[n] = 1, circle = 5.0 + 5.0 * hash01(seed + static_cast<int>(n), 14), circleAngle = 0.0, circleS = s, circleC = c;
			}
		}
		const double shear = (stream::speedAt(r, c + 0.15, width) - stream::speedAt(r, c - 0.15, width)) / 0.3;
		s += v * (0.92 + 0.08 * std::sin(0.7 * t + seed)) * kStep;
		c = std::clamp(c + push * kStep, -0.44 * width, 0.44 * width);
		spin += (40.0 * shear + 6.0 * std::sin(0.5 * t + 2.0 * seed)) * kStep;
	}
	return way;
}
inline void prepare(Thing &th, const std::vector<water::River> &rivers, const water::Sea *sea, double seconds) {
	const double wind = sea ? sea->wind : 0.0;
	th.ways.clear();
	for (int n = 0; n < th.count; ++n) {
		const int k = th.seed * 53 + n;
		const double x = th.x + (th.count > 1 ? 1.6 * (hash01(k, 2) - 0.5) : 0.0), z = th.z + (th.count > 1 ? 1.6 * (hash01(k, 3) - 0.5) : 0.0);
		if (th.river >= 0 && th.river < static_cast<int>(rivers.size())) {
			th.ways.push_back(wayDown(rivers[static_cast<std::size_t>(th.river)], x, z, k, seconds));
			continue;
		}
		std::vector<Thing::Step> way;
		const double a = wind * kPi / 180.0, drift = th.kind == "leaf" ? 0.2 : 0.09;
		double px = x, pz = z, spin = 360.0 * hash01(k, 1);
		for (double t = 0.0; t < seconds; t += kStep) {
			way.push_back({px + 0.5 * std::sin(0.13 * t + k), pz + 0.5 * std::sin(0.11 * t + 2.0 * k), spin});
			px += std::sin(a) * drift * kStep, pz += std::cos(a) * drift * kStep, spin += 7.0 * kStep;
			if (sea)
				for (const water::Sea::Bend &b : sea->bends) {   // round and into a whirlpool
					const double dx = px - b.x, dz = pz - b.z, r = std::max(0.2, std::hypot(dx, dz));
					if (b.kind != 0 || r > 1.3 * b.radius || t + th.from < b.t0 || t + th.from > b.t1) continue;
					const double go = 5.0 * std::min(1.0, 0.2 * b.radius / r), in = 0.9;
					px += (-dz / r * go - dx / r * in) * kStep, pz += (dx / r * go - dz / r * in) * kStep, spin += 60.0 * kStep;
				}
		}
		th.ways.push_back(std::move(way));
	}
}
// Where one of them is at t (nature's) and how it sits: position (pbrt's), turn, tips along x and z.
struct Pose {
	Vec3 at;
	double spin = 0.0, slopeX = 0.0, slopeZ = 0.0;
	Vec3 ahead{0.0, 0.0, 1.0};
};
inline Pose poseOf(const Thing &th, std::size_t n, double t, const std::vector<water::River> &rivers, const water::Sea *sea) {
	Pose p;
	const std::vector<Thing::Step> &way = th.ways[n];
	if (way.empty()) return p;
	const double k = std::clamp((t - th.from) / kStep, 0.0, static_cast<double>(way.size() - 1));
	const std::size_t i = std::min(way.size() - 2 + (way.size() < 2 ? 1 : 0), static_cast<std::size_t>(k));
	const std::size_t j = std::min(way.size() - 1, i + 1);
	const double u = k - static_cast<double>(i), a = lerp(way[i].s, way[j].s, u), b = lerp(way[i].c, way[j].c, u);
	p.spin = lerp(way[i].spin, way[j].spin, u);
	if (th.river >= 0 && th.river < static_cast<int>(rivers.size())) {
		const water::River &r = rivers[static_cast<std::size_t>(th.river)];
		const stream::Place q = stream::placeAt(r, a);
		double y = 0.0, ss = 0.0, sc = 0.0;
		stream::surfaceAt(r, a, b, t, y, ss, sc);
		p.at = q.at + q.side * b;
		p.at.y = y;
		p.slopeX = ss * q.along.x + sc * q.side.x, p.slopeZ = ss * q.along.z + sc * q.side.z;
		p.ahead = q.along;
		return p;
	}
	Vec3 q, nm;
	double fold = 0.0;
	if (sea) {
		water::seaAt(*sea, a, b, t, 0.0, q, nm, fold, 0.3);
		p.at = {a, q.y, b};
		p.slopeX = -nm.x / std::max(0.2, nm.y), p.slopeZ = -nm.z / std::max(0.2, nm.y);
	} else {
		p.at = {a, 0.0, b};
	}
	return p;
}

namespace detail {
inline Mesh leafMesh(double L) {
	Mesh m;
	const int nu = 8, nv = 4;
	for (int j = 0; j <= nu; ++j)
		for (int i = 0; i <= nv; ++i) {
			const double u = j / double(nu), v = (i / double(nv) - 0.5) * 2.0, half = 0.42 * L * std::sin(kPi * std::pow(u, 0.8));
			m.verts.push_back({v * half, 0.1 * L * (v * v + 0.8 * (u - 0.5) * (u - 0.5)), (u - 0.5) * L});
		}
	for (int j = 0; j < nu; ++j)
		for (int i = 0; i < nv; ++i) {
			const int a = j * (nv + 1) + i;
			m.tris.push_back({a, a + 1, a + nv + 1}), m.tris.push_back({a + 1, a + nv + 2, a + nv + 1});
		}
	sdf::computeNormals(m);
	return m;
}
// A folded paper boat: its two sides, its peak, its folds sharp (each face its own points).
inline Mesh paperBoat(double L) {
	Mesh m;
	const auto face = [&m](Vec3 a, Vec3 b, Vec3 c) {
		const int k = static_cast<int>(m.verts.size());
		const Vec3 n = normalize(cross(b - a, c - a));
		for (const Vec3 &p : {a, b, c}) m.verts.push_back(p), m.normals.push_back(n);
		m.tris.push_back({k, k + 1, k + 2});
	};
	const double B = 0.2 * L, H = 0.32 * L, T = 0.62 * L;
	const Vec3 bow{0.0, H, 0.5 * L}, stern{0.0, H, -0.5 * L}, keelA{0.0, 0.0, 0.2 * L}, keelB{0.0, 0.0, -0.2 * L}, top{0.0, T, 0.0};
	for (const double s : {-1.0, 1.0}) {
		const Vec3 mid{s * B, H, 0.0};
		face(keelA, bow, mid), face(keelA, mid, keelB), face(keelB, mid, stern);
		face(bow, top, mid), face(top, stern, mid);
	}
	return m;
}
inline Mesh duckMesh(double L, Mesh &beak) {
	Mesh m;
	craft::detail::add(m, uvSphere(1.0, 20, 12), mul(translate(0.0, 0.3 * L, -0.08 * L), scale(0.38 * L, 0.3 * L, 0.5 * L)));
	craft::detail::add(m, uvSphere(1.0, 16, 10), mul(translate(0.0, 0.3 * L, -0.5 * L), rotate({1.0, 0.0, 0.0}, -30.0), scale(0.1 * L, 0.1 * L, 0.18 * L)));
	craft::detail::add(m, uvSphere(0.25 * L, 18, 12), translate(0.0, 0.72 * L, 0.26 * L));
	craft::detail::add(beak, uvSphere(1.0, 12, 8), mul(translate(0.0, 0.7 * L, 0.5 * L), scale(0.1 * L, 0.04 * L, 0.11 * L)));
	return m;
}
}  // namespace detail

// One of them as scene text.
inline std::string oneText(const Thing &th, std::size_t n, const Pose &p, double t) {
	const int k = th.seed * 53 + static_cast<int>(n);
	const double S = th.size;
	const Vec3 story{-p.at.x, p.at.y, p.at.z};
	const Vec3 up = normalize(Vec3{p.slopeX, 1.0, -p.slopeZ});   // (story space)
	const Mat4 tip = alignY({0.0, 0.0, 0.0}, up);
	const Mat4 place = mul(translate(story), tip, rotate({0.0, 1.0, 0.0}, p.spin));
	if (th.kind == "leaf") {
		const Vec3 tones[4] = {{0.72, 0.3, 0.06}, {0.78, 0.5, 0.08}, {0.55, 0.13, 0.06}, {0.4, 0.42, 0.1}};
		return attr(coated(tones[static_cast<std::size_t>(hash01(k, 7) * 4.0) % 4], 0.3), emitMesh(detail::leafMesh(0.13 * S * (0.8 + 0.4 * hash01(k, 8))), mul(place, translate(0.0, 0.006, 0.0))));
	}
	if (th.kind == "paperboat") return attr(diffuseMaterial({0.92, 0.9, 0.84}), emitMesh(detail::paperBoat(0.3 * S), mul(place, translate(0.0, -0.012, 0.0))));
	if (th.kind == "log") {
		const double r = 0.15 * S, L = 1.7 * S;
		return attr(coated({0.27, 0.19, 0.12}, 0.55), emitMesh(capsule(r, L, 12, 3), mul(place, rotate({1.0, 0.0, 0.0}, 90.0), translate(0.0, -0.5 * L, 0.3 * r))));
	}
	if (th.kind == "bottle") {
		const double H = 0.3 * S;
		Mesh glass = lathe({{0.0, 0.0}, {0.052 * S, 0.004}, {0.055 * S, 0.17 * S}, {0.02 * S, 0.22 * S}, {0.02 * S, H}, {0.0, H}}, 16);
		const Mat4 lie = mul(place, translate(0.0, 0.02, -0.14 * S), rotate({1.0, 0.0, 0.0}, 84.0 + 8.0 * std::sin(0.8 * t + k)));
		std::string out = attr(glassMaterial(1.5, 0.0), emitMesh(glass, lie));
		out += attr(diffuseMaterial({0.86, 0.8, 0.62}), emitMesh(capsule(0.02 * S, 0.1 * S, 8, 2), mul(lie, translate(0.0, 0.05 * S, 0.0))));
		out += attr(diffuseMaterial({0.5, 0.36, 0.2}), emitMesh(capsule(0.017 * S, 0.012 * S, 8, 2), mul(lie, translate(0.0, H, 0.0))));
		return out;
	}
	if (th.kind == "duck") {
		Mesh beak;
		const Mesh body = detail::duckMesh(0.2 * S, beak);
		return attr(coated({0.95, 0.72, 0.08}, 0.12), emitMesh(body, place)) + attr(coated({0.92, 0.35, 0.05}, 0.2), emitMesh(beak, place));
	}
	if (th.kind == "crate") return attr(coated({0.5, 0.36, 0.2}, 0.5), emitMesh(box(0.5 * S, 0.42 * S, 0.5 * S), mul(place, translate(0.0, 0.1 * S, 0.0))));
	// a paper lantern on a little raft
	const Vec3 base{story.x, story.y + 0.02, story.z};
	std::string out = attr(coated({0.32, 0.22, 0.12}, 0.4), emitMesh(box(0.26 * S, 0.03 * S, 0.26 * S), place));
	const Vec3 glow = Vec3{9.0, 4.2, 1.3} * (0.9 + 0.1 * std::sin(11.0 * t + k));
	out += emissiveSphere(base + Vec3{0.0, 0.12 * S, 0.0}, 0.075 * S, glow);
	return out;
}
inline std::string text(const Thing &th, double t, const std::vector<water::River> &rivers, const water::Sea *sea) {
	if (t < th.from) return std::string();
	std::string out = "# carried by the water (film_adrift.h)\n";
	for (std::size_t n = 0; n < th.ways.size(); ++n) out += oneText(th, n, poseOf(th, n, t, rivers, sea), t);
	return out;
}
// What a thing does to the water: a small ring now and then as it bobs, a wake if it is big enough.
inline void stirred(stir::Field &f, const Thing &th, double t, const std::vector<water::River> &rivers, const water::Sea *sea) {
	if (th.kind == "leaf") return;
	for (std::size_t n = 0; n < th.ways.size(); ++n)
		stir::trail(f, [&](double u) { return poseOf(th, n, u, rivers, sea).at; }, th.from, 1e30, t, 0.5, 0.05, 0.0006 * th.size, 0.4);
}

}  // namespace adrift
}  // namespace cartoon
