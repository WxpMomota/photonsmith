#pragma once
// film_sealife.h -- life under the sea (round 13), for films under water (film_underwater.h) or anywhere a sea is:
//
//   baitball X Y Z [count N] [radius R] [strike T]... [hunter sharks|dolphins] [seed S]
//            a bait ball: hundreds of silver fish packed into a ball that mills round its middle, each turned along its circle; at each `strike` a hunter (a shark,
//            or a dolphin) dashes through it on a line, and the fish near its path burst away from it - the ball splits round the hunter - and drift back as it goes
//   reeffish N in X0 Y0 Z0 X1 Y1 Z1 [seed S]
//            reef fish in a box: each circling a home of its own, in the reef's colours (a yellow tang, a blue tang, an orange clownfish with white bands, a
//            purple, a grey with dark stripes, a green wrasse)
//   jellyfish N in X0 Y0 Z0 X1 Y1 Z1 [glow G] [seed S]
//            jellyfish drifting up in pulses: a bell that contracts and relaxes (a lathe profile in time), a frilled rim and trailing tentacles that sway; the bell
//            lets light through (diffuse transmission) and glows faintly at its rim, a glow that lights nothing (never sampled)
//   seaturtles N at X Y Z [radius R] [seed S]
//            sea turtles flying through the water on their front flippers (a slow downstroke, a feathered upstroke), round a circle about `at`
//
// Coordinates: pbrt's in the film file (on a terrain, heights are above the ground there, as everywhere in a film: lifted()); meshes are made in story space (x mirrored) for emitMesh and the instance placements, as the rest of the toolkit's are.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "cartoon_lib.h"
#include "nature_lib.h"

namespace cartoon {
namespace sealife {

struct BaitBall {
	Vec3 at;
	int count = 400, seed = 1;
	double radius = 2.5;
	std::vector<double> strikes;
	std::string hunter = "sharks";
};
struct Reef {
	Vec3 lo, hi;
	int count = 40, seed = 1;
};
struct Jellies {
	Vec3 lo, hi;
	int count = 12, seed = 1;
	double glow = 1.0;
};
struct Turtles {
	Vec3 at;
	int count = 2, seed = 1;
	double radius = 6.0;
};
struct Life {
	std::vector<BaitBall> balls;
	std::vector<Reef> reefs;
	std::vector<Jellies> jellies;
	std::vector<Turtles> turtles;
	bool any() const { return !balls.empty() || !reefs.empty() || !jellies.empty() || !turtles.empty(); }
};

namespace detail {
constexpr int kPoses = 8;
inline Vec3 story(Vec3 p) { return {-p.x, p.y, p.z}; }
inline void append(Mesh &into, const Mesh &m) {
	const int base = static_cast<int>(into.verts.size());
	into.verts.insert(into.verts.end(), m.verts.begin(), m.verts.end());
	into.normals.insert(into.normals.end(), m.normals.begin(), m.normals.end());
	for (const auto &t : m.tris) into.tris.push_back({t[0] + base, t[1] + base, t[2] + base});
}
inline void fin(Mesh &m, Vec3 a, Vec3 b, Vec3 c) {   // (a two-sided triangle)
	const Vec3 n = normalize(cross(b - a, c - a));
	const int base = static_cast<int>(m.verts.size());
	for (const Vec3 &p : {a, b, c}) m.verts.push_back(p), m.normals.push_back(n);
	for (const Vec3 &p : {a, c, b}) m.verts.push_back(p), m.normals.push_back(n * -1.0);
	m.tris.push_back({base, base + 1, base + 2}), m.tris.push_back({base + 3, base + 4, base + 5});
}
// A fish-like body along +z, `len` long: `profile` its radius (a share of its length) along it, made `wide` times as wide and `deep` times as tall, its tail swayed by `sway`
// (a share of its length).
inline Mesh body(double len, double wide, double deep, double sway, const std::vector<std::pair<double, double>> &profile) {
	Mesh m;
	const Mesh lathed = lathe(profile, 12);
	for (std::size_t i = 0; i < lathed.verts.size(); ++i) {
		const Vec3 v = lathed.verts[i], n = lathed.normals[i];
		const double z = v.y * len, bend = z < 0.0 ? sway * (z / len) * (z / len) * len : 0.0;
		m.verts.push_back({v.x * len * wide + bend, v.z * len * deep, z});
		m.normals.push_back(normalize(Vec3{n.x / wide, n.z / deep, n.y}));
	}
	for (const auto &t : lathed.tris) m.tris.push_back({t[0], t[2], t[1]});   // (y and z swapped: the winding turns over)
	return m;
}
// A small fish at phase k of its tail beat (forward +z), `len` long; `deep` 2 for a tall reef fish (a tang).
inline Mesh fish(int k, double len, double deep = 1.0) {
	const double sway = 0.3 * std::sin(2.0 * kPi * k / kPoses);
	Mesh m = body(len, 0.45, deep, sway, {{0.0, -0.5}, {0.08, -0.35}, {0.14, -0.05}, {0.12, 0.2}, {0.06, 0.4}, {0.0, 0.5}});
	const double tx = sway * 0.25 * len;
	fin(m, {tx * 0.8, 0.0, -0.45 * len}, {tx, 0.18 * len, -0.68 * len}, {tx, -0.18 * len, -0.68 * len});
	return m;
}
// A shark (forward +z) `len` long at phase k of its tail beat: a long fusiform body, a tall dorsal fin, broad pectorals, a tail longer above than below.
inline Mesh shark(int k, double len) {
	const double sway = 0.22 * std::sin(2.0 * kPi * k / kPoses);
	Mesh m = body(len, 0.65, 0.75, sway, {{0.0, -0.5}, {0.05, -0.4}, {0.13, -0.15}, {0.15, 0.05}, {0.12, 0.25}, {0.07, 0.42}, {0.0, 0.5}});
	const double tx = sway * 0.3 * len;
	fin(m, {0.0, 0.1 * len, 0.08 * len}, {0.0, 0.3 * len, -0.06 * len}, {0.0, 0.11 * len, -0.12 * len});   // (the dorsal fin)
	for (const double side : {-1.0, 1.0}) fin(m, {side * 0.08 * len, -0.06 * len, 0.14 * len}, {side * 0.3 * len, -0.16 * len, -0.02 * len}, {side * 0.08 * len, -0.07 * len, 0.0});
	fin(m, {tx * 0.8, 0.0, -0.45 * len}, {tx * 1.2, 0.3 * len, -0.68 * len}, {tx, 0.0, -0.56 * len});
	fin(m, {tx * 0.8, 0.0, -0.45 * len}, {tx, 0.0, -0.56 * len}, {tx * 1.1, -0.15 * len, -0.6 * len});
	return m;
}
inline const Vec3 &reefColour(int c) {
	static const Vec3 col[6] = {{0.95, 0.80, 0.08}, {0.10, 0.30, 0.85}, {0.95, 0.42, 0.05}, {0.50, 0.15, 0.62}, {0.55, 0.58, 0.62}, {0.15, 0.70, 0.35}};
	return col[c % 6];
}
// Bands round a fish of fish(k, len, deep) at shares `at` of its length from its middle (a clownfish's white bands, a sergeant major's dark ones), each following the body's
// cross-section a little outside it, bent with the tail as the body is.
inline Mesh bands(int k, double len, double deep, const std::vector<double> &at) {
	const double sway = 0.3 * std::sin(2.0 * kPi * k / kPoses);
	const std::vector<std::pair<double, double>> profile{{0.0, -0.5}, {0.08, -0.35}, {0.14, -0.05}, {0.12, 0.2}, {0.06, 0.4}, {0.0, 0.5}};
	Mesh m;
	for (const double u : at) {
		double r = 0.0;   // (the profile's radius at u)
		for (std::size_t i = 0; i + 1 < profile.size(); ++i)
			if (u >= profile[i].second && u <= profile[i + 1].second) r = lerp(profile[i].first, profile[i + 1].first, (u - profile[i].second) / (profile[i + 1].second - profile[i].second));
		const double z = u * len, bend = z < 0.0 ? sway * u * u * len : 0.0;
		std::vector<Vec3> ring;
		for (int q = 0; q <= 16; ++q) {
			const double a = 2.0 * kPi * q / 16;
			ring.push_back({std::cos(a) * r * len * 0.45 * 1.04 + bend, std::sin(a) * r * len * deep * 1.04, z});
		}
		append(m, sdf::tubeMesh(ring, 0.035 * len, 1.0, 5));
	}
	return m;
}
inline std::string objects(bool ball, bool sharks, bool reef) {
	std::string out;
	for (int k = 0; k < kPoses; ++k) {
		if (ball) out += nature::objectBegin("sea_bait_" + std::to_string(k)) + attr(metalMaterial({0.78, 0.82, 0.86}, 0.25), emitMesh(fish(k, 0.18), identity())) + nature::objectEnd();
		if (sharks) out += nature::objectBegin("sea_shark_" + std::to_string(k)) + attr(coated({0.42, 0.46, 0.50}, 0.3), emitMesh(shark(k, 2.6), identity())) + nature::objectEnd();
		if (reef)
			for (int c = 0; c < 6; ++c) {   // (the clownfish with its white bands, the grey one with its dark stripes)
				const double deep = c == 0 || c == 1 ? 2.2 : 1.2;
				std::string text = attr(coated(reefColour(c), 0.3), emitMesh(fish(k, 0.16, deep), identity()));
				if (c == 2) text += attr(coated({0.95, 0.95, 0.93}, 0.3), emitMesh(bands(k, 0.16, deep, {0.28, 0.02, -0.3}), identity()));
				if (c == 4) text += attr(coated({0.06, 0.06, 0.08}, 0.3), emitMesh(bands(k, 0.16, deep, {0.3, 0.15, 0.0, -0.15, -0.3}), identity()));
				out += nature::objectBegin("sea_reef_" + std::to_string(c) + "_" + std::to_string(k)) + text + nature::objectEnd();
			}
	}
	return out;
}
inline Mat4 facingOf(Vec3 fwd) {
	fwd = normalize(fwd);
	Vec3 right = cross(Vec3{0, 1, 0}, fwd);
	right = length(right) > 1e-9 ? normalize(right) : Vec3{1, 0, 0};
	const Vec3 up = cross(fwd, right);
	return {right.x, up.x, fwd.x, 0, right.y, up.y, fwd.y, 0, right.z, up.z, fwd.z, 0, 0, 0, 0, 1};
}
}  // namespace detail

// A bait ball at t (see the top).
inline std::string ballText(const BaitBall &b, double t) {
	std::string out = "# a bait ball (film_sealife.h)\n";
	const bool sharks = b.hunter != "dolphins";
	// the hunter's dash for each strike: in from 14 m away on a line through the middle, at 9 m/s
	struct Dash {
		Vec3 a, d;
		double at;
	};
	std::vector<Dash> dashes;
	for (std::size_t s = 0; s < b.strikes.size(); ++s) {
		const double ang = 2.0 * kPi * hash01(static_cast<int>(s), b.seed + 3), el = 0.4 * (hash01(static_cast<int>(s), b.seed + 4) - 0.5);
		const Vec3 d = normalize(Vec3{std::cos(ang), el, std::sin(ang)});
		dashes.push_back({b.at - d * 14.0, d, b.strikes[s] - 14.0 / 9.0});
	}
	for (int i = 0; i < b.count; ++i) {
		const double u = hash01(i, b.seed + 5), v = hash01(i, b.seed + 6), w = hash01(i, b.seed + 7);
		const double r = b.radius * std::cbrt(0.15 + 0.85 * u), ph = 2.0 * kPi * v, h = (2.0 * w - 1.0) * 0.85;
		const double omega = 0.45 / std::max(0.4, r);   // (milling: the inner fish round faster)
		const double a = ph + omega * t;
		const double rr = r * std::sqrt(1.0 - h * h);
		Vec3 p = b.at + Vec3{rr * std::cos(a), r * h * 0.8, rr * std::sin(a)};
		Vec3 fwd{-std::sin(a), 0.05 * std::sin(3.0 * a + i), std::cos(a)};
		for (const Dash &d : dashes) {   // bursting away from a hunter's path, and drifting back
			const double since = t - d.at;
			if (since < 0.0 || since > 6.0) continue;
			const Vec3 hunter = d.a + d.d * (9.0 * since);
			const Vec3 rel = p - hunter;
			const double along = dot(rel, d.d), dist = length(rel - d.d * along);
			const double reach = std::max(0.0, 2.6 - dist) * std::exp(-std::max(0.0, along) / 3.0) * (along > -6.0 ? 1.0 : 0.0);
			const double pass = std::clamp((dot(b.at - d.a, d.d) / 9.0 - since + 0.4), -10.0, 10.0);   // (how long before the hunter reaches the middle)
			const double burst = reach * smoothstep(1.0, -0.3, pass) * std::exp(-std::max(0.0, -pass) / 2.0);
			const Vec3 outward = normalize(rel - d.d * along + Vec3{1e-9, 0, 0});
			p = p + outward * (1.6 * burst);
			if (burst > 0.05) fwd = normalize(lerp(fwd, outward, std::min(1.0, burst)));
		}
		const int pose = static_cast<int>(std::floor((t * 4.0 + hash01(i, b.seed + 8)) * detail::kPoses)) % detail::kPoses;
		out += nature::placeObject("sea_bait_" + std::to_string(pose), mul(translate(detail::story(p)), detail::facingOf(detail::story(fwd)), scale(0.8 + 0.4 * hash01(i, b.seed + 9))));
	}
	for (std::size_t s = 0; s < dashes.size(); ++s) {   // the hunters
		const Dash &d = dashes[s];
		const double since = t - d.at;
		if (since < 0.0 || since > 28.0 / 9.0) continue;
		const Vec3 p = d.a + d.d * (9.0 * since);
		const int pose = static_cast<int>(std::floor(t * 2.5 * detail::kPoses)) % detail::kPoses;
		if (sharks) out += nature::placeObject("sea_shark_" + std::to_string(pose), mul(translate(detail::story(p)), detail::facingOf(detail::story(d.d))));
		else out += attr(coated({0.45, 0.5, 0.55}, 0.2), emitMesh(detail::body(2.2, 0.7, 0.75, 0.15 * std::sin(t * 12.0), {{0.0, -0.5}, {0.1, -0.3}, {0.15, 0.0}, {0.12, 0.3}, {0.04, 0.5}}),
																	  mul(translate(detail::story(p)), detail::facingOf(detail::story(d.d)))));
	}
	return out;
}
// Reef fish at t: each circling a home of its own, in its colour.
inline std::string reefText(const Reef &r, double t) {
	std::string out = "# reef fish (film_sealife.h)\n";
	for (int i = 0; i < r.count; ++i) {
		const Vec3 home{r.lo.x + (r.hi.x - r.lo.x) * hash01(i, r.seed + 1), r.lo.y + (r.hi.y - r.lo.y) * hash01(i, r.seed + 2), r.lo.z + (r.hi.z - r.lo.z) * hash01(i, r.seed + 3)};
		const double R = 0.3 + 0.9 * hash01(i, r.seed + 4), w = (0.25 + 0.3 * hash01(i, r.seed + 5)) * (hash01(i, r.seed + 6) < 0.5 ? -1.0 : 1.0), a = w * t + 6.2832 * hash01(i, r.seed + 7);
		const Vec3 p = home + Vec3{R * std::cos(a), 0.15 * std::sin(1.3 * a), R * std::sin(a)};
		const Vec3 fwd{-std::sin(a) * w, 0.0, std::cos(a) * w};
		const int c = static_cast<int>(hash01(i, r.seed + 8) * 6.0) % 6;
		const int pose = static_cast<int>(std::floor((t * 3.0 + hash01(i, r.seed + 9)) * detail::kPoses)) % detail::kPoses;
		out += nature::placeObject("sea_reef_" + std::to_string(c) + "_" + std::to_string(pose), mul(translate(detail::story(p)), detail::facingOf(detail::story(fwd)), scale(0.8 + 0.5 * hash01(i, r.seed + 10))));
	}
	return out;
}
// Jellyfish at t (see the top): each pulsing every 1.2 to 2 s, rising a little with each pulse and sinking slowly between them.
inline std::string jellyText(const Jellies &j, double t) {
	std::string out = "# jellyfish (film_sealife.h)\n";
	for (int i = 0; i < j.count; ++i) {
		const int s = j.seed * 89 + i;
		const double period = 1.2 + 0.8 * hash01(s, 1), ph = std::fmod(t / period + hash01(s, 2), 1.0);
		const double squeeze = ph < 0.3 ? std::sin(kPi * ph / 0.3) : 0.0;   // (the bell's quick contraction, then a slow relaxing)
		const double size = 0.15 + 0.2 * hash01(s, 3);
		const double rise = 0.08 * size * std::floor(t / period) + 0.08 * size * smoothstep(0.0, 0.4, ph) - 0.01 * t;
		const Vec3 at{j.lo.x + (j.hi.x - j.lo.x) * hash01(s, 4), j.lo.y + (j.hi.y - j.lo.y) * hash01(s, 5) + rise, j.lo.z + (j.hi.z - j.lo.z) * hash01(s, 6)};
		const double wide = 1.0 - 0.28 * squeeze, tall = 1.0 + 0.2 * squeeze;
		Mesh bell = lathe({{0.0, 0.62 * tall}, {0.45 * wide, 0.55 * tall}, {0.85 * wide, 0.32 * tall}, {1.0 * wide, 0.05}, {0.92 * wide, -0.02}, {0.8 * wide, 0.06}, {0.0, 0.25 * tall}}, 24);
		const Mat4 place = mul(translate(detail::story(at)), rotate({1, 0, 0}, 8.0 * std::sin(0.3 * t + s)), scale(size));
		const Vec3 tint = hash01(s, 7) < 0.5 ? Vec3{0.75, 0.55, 0.9} : Vec3{0.6, 0.8, 0.95};
		out += attr(translucentMaterial(tint * 0.5, tint * 0.8), emitMesh(bell, place));
		// the rim's faint glow (lighting nothing), and the tentacles trailing and swaying
		const Vec3 L = tint * (0.35 * j.glow);
		char glow[200];
		std::snprintf(glow, sizeof glow, "AreaLightSource \"diffuse\" \"rgb L\" [ %.4f %.4f %.4f ] \"bool twosided\" true \"bool sampled\" false\n  Material \"diffuse\" \"rgb reflectance\" [ 0 0 0 ]", L.x, L.y, L.z);
		Mesh rim, tentacles;
		std::vector<Vec3> ring;
		for (int k = 0; k <= 24; ++k) ring.push_back({std::cos(2.0 * kPi * k / 24) * 0.96 * wide, 0.02, std::sin(2.0 * kPi * k / 24) * 0.96 * wide});
		detail::append(rim, sdf::tubeMesh(ring, 0.035, 1.0, 6));
		for (int k = 0; k < 10; ++k) {
			const double a = 2.0 * kPi * k / 10;
			std::vector<Vec3> line;
			for (int q = 0; q <= 10; ++q) {
				const double u = q / 10.0, swayA = 0.25 * u * std::sin(1.4 * t - 3.0 * u + k);
				line.push_back({std::cos(a) * 0.8 * wide * (1.0 - 0.4 * u) + swayA, -u * (2.5 + hash01(s * 13 + k, 8)), std::sin(a) * 0.8 * wide * (1.0 - 0.4 * u) + 0.6 * swayA});
			}
			detail::append(tentacles, sdf::tubeMesh(line, 0.012, 0.3, 4));
		}
		out += attr(glow, emitMesh(rim, place));
		out += attr(translucentMaterial(tint * 0.4, tint * 0.7), emitMesh(tentacles, place));
	}
	return out;
}
// Sea turtles at t: round a circle, their front flippers stroking (down slow, up feathered), their rear flippers steering.
inline std::string turtleText(const Turtles &tu, double t) {
	std::string out = "# sea turtles (film_sealife.h)\n";
	for (int i = 0; i < tu.count; ++i) {
		const int s = tu.seed * 97 + i;
		const double w = 0.12 + 0.05 * hash01(s, 1), a = w * t + 2.0 * kPi * i / std::max(1, tu.count);
		const double R = tu.radius * (0.8 + 0.4 * hash01(s, 2));
		const Vec3 at = tu.at + Vec3{R * std::cos(a), 1.2 * std::sin(0.4 * t + i), R * std::sin(a)};
		const Vec3 fwd{-std::sin(a), 0.1 * std::cos(0.4 * t + i), std::cos(a)};
		const double stroke = std::sin(2.0 * kPi * t / 2.6 + i), L = 0.9;
		const Mat4 place = mul(translate(detail::story(at)), detail::facingOf(detail::story(fwd)), rotate({0, 0, 1}, -8.0 * stroke));
		Mesh shell = lathe({{0.0, -0.5}, {0.3, -0.42}, {0.45, -0.15}, {0.48, 0.1}, {0.38, 0.35}, {0.0, 0.48}}, 18);
		for (Vec3 &v : shell.verts) v = Vec3{v.x * L, v.z * L * 0.35 + 0.02, v.y * L};   // (flattened, lying along +z)
		for (Vec3 &n : shell.normals) n = normalize(Vec3{n.x, n.z / 0.35, n.y});
		for (auto &tr : shell.tris) std::swap(tr[1], tr[2]);
		shell.uvs.clear();
		Mesh skin = lathe({{0.0, -0.08}, {0.08, -0.05}, {0.09, 0.05}, {0.0, 0.1}}, 10);
		for (std::size_t k = 0; k < skin.verts.size(); ++k) skin.verts[k] = Vec3{skin.verts[k].x * L, skin.verts[k].z * L * 0.8, skin.verts[k].y * L + 0.58 * L}, skin.normals[k] = Vec3{skin.normals[k].x, skin.normals[k].z, skin.normals[k].y};
		for (auto &tr : skin.tris) std::swap(tr[1], tr[2]);
		skin.uvs.clear();   // (the flippers added below have none)
		for (const double side : {-1.0, 1.0}) {   // the front flippers (long, stroking) and the rear (short, steering)
			const double up = 0.9 * stroke, sweep = 0.4 * std::cos(2.0 * kPi * t / 2.6 + i);
			const Vec3 root{side * 0.3 * L, 0.0, 0.25 * L}, tip = root + Vec3{side * 0.6 * L * std::cos(up), 0.6 * L * std::sin(up), -0.25 * L + sweep * 0.2 * L};
			detail::fin(skin, root, tip, root + Vec3{0.0, 0.0, -0.22 * L});
			detail::fin(skin, Vec3{side * 0.25 * L, 0.0, -0.35 * L}, Vec3{side * 0.42 * L, 0.0, -0.5 * L + 0.05 * L * stroke}, Vec3{side * 0.2 * L, 0.0, -0.45 * L});
		}
		out += attr(coated({0.35, 0.27, 0.12}, 0.35), emitMesh(shell, place));
		out += attr(diffuseMaterial({0.45, 0.45, 0.32}), emitMesh(skin, place));
	}
	return out;
}
// The film's sea life with its heights made pbrt's: on a terrain a film's heights are above the ground (a ball's middle, a box's middle, a circle's middle).
inline Life lifted(Life life, const std::function<double(double, double)> &groundAt) {
	for (BaitBall &b : life.balls) b.at.y += groundAt(b.at.x, b.at.z);
	for (Turtles &tu : life.turtles) tu.at.y += groundAt(tu.at.x, tu.at.z);
	for (Reef &r : life.reefs) {
		const double g = groundAt(0.5 * (r.lo.x + r.hi.x), 0.5 * (r.lo.z + r.hi.z));
		r.lo.y += g, r.hi.y += g;
	}
	for (Jellies &j : life.jellies) {
		const double g = groundAt(0.5 * (j.lo.x + j.hi.x), 0.5 * (j.lo.z + j.hi.z));
		j.lo.y += g, j.hi.y += g;
	}
	return life;
}
// All of a film's sea life at t.
inline std::string text(const Life &life, double t) {
	if (!life.any()) return std::string();
	bool sharks = false;
	for (const BaitBall &b : life.balls) sharks = sharks || b.hunter != "dolphins";
	std::string out = detail::objects(!life.balls.empty(), sharks, !life.reefs.empty());
	for (const BaitBall &b : life.balls) out += ballText(b, t);
	for (const Reef &r : life.reefs) out += reefText(r, t);
	for (const Jellies &j : life.jellies) out += jellyText(j, t);
	for (const Turtles &tu : life.turtles) out += turtleText(tu, t);
	return out;
}

}  // namespace sealife
}  // namespace cartoon
