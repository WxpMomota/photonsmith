#pragma once
// film_abyss.h -- under the sea, round 18: giants in the dark.
//
//   kraken X Y Z [size S] [toward A] [rise T] [reach X Y Z] [glow T] [seed K]   a giant octopus, its head S metres across (6): its great arms curling and reaching (towards
//                                                     `reach` from T on), its eyes catching the light; with `rise T` it comes up out of the deep below over eight seconds
//   giantsquid X Y Z [length L] [toward A] [from T] [glow T] [seed K]            a giant squid L metres long (12) with eyes as big as plates, jetting slowly ahead in pulses,
//                                                     its two long feeding tentacles trailing, clubs at their ends
//   seaserpent NAME at X Y Z to X Y Z ... [length L] [speed S] [from T] [glow T] [seed K]   a sea serpent L metres long (30) swimming along its points, its body in waves,
//                                                     a crest down its back; a shot can follow it by NAME
//   hugeeye X Y Z [toward A] [size S] [open T] [close T]   an eye S metres across (2) in the dark, opening at T: a gold iris, a slit pupil, a faint glow
//   reveal X Y Z at T [bright B] [colour R G B] [over D]   a light that comes up over D seconds (3) from T: behind a giant, its silhouette; in front, the giant itself
//   whalefall X Z [length L] [toward A] [seed K]          a whale's skeleton on the seabed (L long, 14), its bones furred with bacteria, crabs and hagfish at work on it
//   With `glow T` a giant's rows of lights along its body come on at T.
//
// Heights above the seabed until lifted, pbrt's x z (film_parse_sea.h).

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

#include "film_deepsea.h"
#include "film_flow.h"
#include "film_sealife.h"
#include "sdf_mesh.h"

namespace cartoon {
namespace abyss {

struct Kraken {
	Vec3 at, reach;
	double size = 6.0, toward = 0.0, rise = -1.0, reachFrom = 1e30, glow = 1e30;
	int seed = 1;
};
struct Squid {
	Vec3 at;
	double length = 12.0, toward = 0.0, from = 0.0, glow = 1e30;
	int seed = 1;
};
struct Serpent {
	std::string name;
	std::vector<Vec3> path;
	double length = 30.0, speed = 2.5, from = 0.0, glow = 1e30;
	int seed = 1;
};
struct Eye {
	Vec3 at;
	double toward = 0.0, size = 2.0, open = 0.0, close = 1e30;
};
struct Reveal {
	Vec3 at, colour{0.5, 0.75, 1.0};
	double t = 0.0, bright = 1.0, over = 3.0;
};
struct WhaleFall {
	double x = 0.0, z = 0.0, length = 14.0, toward = 0.0;
	int seed = 1;
};
struct World {
	std::vector<Kraken> krakens;
	std::vector<Squid> squids;
	std::vector<Serpent> serpents;
	std::vector<Eye> eyes;
	std::vector<Reveal> reveals;
	std::vector<WhaleFall> falls;
	bool any() const { return !krakens.empty() || !squids.empty() || !serpents.empty() || !eyes.empty() || !reveals.empty() || !falls.empty(); }
};

namespace detail {
using deepsea::detail::add;
using sealife::detail::facingOf;
using sealife::detail::fin;
using sealife::detail::story;
inline Mesh ellipsoid(Vec3 c, Vec3 r, int nu = 20, int nv = 12) {
	Mesh m;
	add(m, uvSphere(1.0, nu, nv), mul(translate(c), scale(r.x, r.y, r.z)));
	return m;
}
inline Vec3 facing(double degrees) { return {std::sin(degrees * kPi / 180.0), 0.0, std::cos(degrees * kPi / 180.0)}; }
// An arm along `line` (local space), tapering from r0 to r1, with suckers along its underside (`down` the way they face).
inline void arm(Mesh &skin, Mesh &suckers, const std::vector<Vec3> &line, double r0, double r1, int every = 2) {
	std::vector<double> radii;
	for (std::size_t i = 0; i < line.size(); ++i) radii.push_back(lerp(r0, r1, static_cast<double>(i) / (line.size() - 1)));
	add(skin, flow::tube(line, radii, 10));
	for (std::size_t i = 1; i + 1 < line.size(); i += static_cast<std::size_t>(every)) {
		const Vec3 along = normalize(line[i + 1] - line[i - 1]);
		Vec3 side = cross(along, Vec3{0.0, 1.0, 0.0});
		if (length(side) < 1e-3) side = {1.0, 0.0, 0.0};
		side = normalize(side);
		const Vec3 under = normalize(cross(side, along)) * -1.0;
		add(suckers, ellipsoid(line[i] + under * (0.85 * radii[i]), Vec3{0.32, 0.32, 0.32} * radii[i], 8, 5));
	}
}
// Lights in a row along `line` (local), each a small sphere: into `m`.
inline void photophores(Mesh &m, const std::vector<Vec3> &line, double r, double lift) {
	for (std::size_t i = 0; i < line.size(); i += 2) add(m, ellipsoid(line[i] + Vec3{0.0, lift, 0.0}, {r, r, r}, 6, 4));
}
// A glowing emitter's words, `amount` 0..1 of `colour`.
inline std::string glowing(Vec3 colour, double amount, const Mesh &m, const Mat4 &place) {
	const Vec3 L = colour * std::max(0.02, amount);
	return flow::glowAttr(emitMesh(m, place), L);
}
}  // namespace detail

// ---- the kraken ----
inline std::string krakenText(const Kraken &k, double t) {
	using namespace detail;
	if (k.rise >= 0.0 && t < k.rise) return std::string();   // (still down in the deep: its arms would reach up through the bottom)
	const double S = k.size, up = k.rise >= 0.0 ? -3.0 * S * (1.0 - smoothstep(k.rise, k.rise + 8.0, t)) : 0.0;
	const Vec3 at = k.at + Vec3{0.0, up + 0.15 * S * std::sin(0.25 * t + k.seed), 0.0};
	const Mat4 place = mul(translate(story(at)), facingOf(story(facing(k.toward))));
	const Mat4 toLocal = invertAffine(place);
	Mesh skin, suckers, eyes, pupils, lights;
	const double breathe = 1.0 + 0.05 * std::sin(0.6 * t + k.seed);
	add(skin, ellipsoid({0.0, 0.45 * S, -0.35 * S}, Vec3{0.45 * S, 0.55 * S, 0.62 * S} * breathe, 32, 20));   // (its mantle, swept back)
	add(skin, ellipsoid({0.0, 0.05 * S, 0.0}, {0.42 * S, 0.3 * S, 0.4 * S}, 28, 16));   // (its head, where the arms meet)
	for (const double side : {1.0, -1.0}) {
		add(eyes, ellipsoid({side * 0.3 * S, 0.15 * S, 0.22 * S}, Vec3{0.11, 0.1, 0.11} * S, 18, 12));
		add(pupils, ellipsoid({side * 0.33 * S, 0.15 * S, 0.31 * S}, Vec3{0.05, 0.015, 0.04} * S, 14, 8));
	}
	const Vec3 reachLocal = transformPoint(toLocal, story(k.reach));
	const double reaching = smoothstep(k.reachFrom, k.reachFrom + 4.0, t);
	for (int a = 0; a < 8; ++a) {   // eight great arms, curling slowly; with `reach`, the front two stretch for it
		const double ang = 2.0 * kPi * (a + 0.5) / 8.0, ph = 0.35 * t + 1.3 * a + k.seed;
		const bool front = a == 0 || a == 7;
		std::vector<Vec3> line;
		for (int j = 0; j <= 18; ++j) {
			const double u = j / 18.0, reachOut = S * (0.3 + 2.4 * u);
			Vec3 q{std::sin(ang) * reachOut, -0.1 * S - 0.6 * S * u + 1.6 * S * u * u * (0.55 + 0.45 * std::sin(ph + 3.0 * u)), std::cos(ang) * reachOut};   // (out along the bottom, the tips rising)
			const double curl = 2.2 * u * u * u;   // (the tip rolling round on itself)
			q = q + Vec3{0.8 * S * curl * std::sin(ph * 1.3 + 6.0 * u), 0.5 * S * curl * std::cos(ph + 6.0 * u), 0.8 * S * curl * std::cos(ph + 6.0 * u)};
			if (front && reaching > 0.0) q = lerp(q, lerp(Vec3{0.0, 0.0, 0.3 * S}, reachLocal + Vec3{(a == 0 ? 1.0 : -1.0) * 0.3 * S * (1.0 - u), 0.0, 0.0}, u), reaching * std::min(1.0, 1.5 * u));
			line.push_back(q);
		}
		arm(skin, suckers, line, 0.13 * S, 0.012 * S, 2);
		if (t >= k.glow) photophores(lights, line, 0.02 * S, 0.12 * S);
	}
	std::string out = "# a kraken (film_abyss.h)\n";
	out += attr(coated({0.45, 0.12, 0.1}, 0.35), emitMesh(skin, place));
	out += attr(coated({0.85, 0.65, 0.6}, 0.4), emitMesh(suckers, place));
	out += attr(coated({0.85, 0.7, 0.2}, 0.05), emitMesh(eyes, place));
	out += attr(coated({0.02, 0.02, 0.02}, 0.05), emitMesh(pupils, place));
	if (!lights.tris.empty()) out += glowing({0.3, 1.6, 2.4}, smoothstep(k.glow, k.glow + 2.0, t), lights, place);
	return out;
}

// ---- the giant squid ----
inline std::string squidText(const Squid &q, double t) {
	using namespace detail;
	const double L = q.length, age = std::max(0.0, t - q.from);
	const double pulses = std::floor(age / 3.0), within = std::fmod(age, 3.0);   // (a jet every three seconds: a surge, then gliding)
	const double travelled = 0.05 * L * (pulses + smoothstep(0.0, 1.0, within));
	const Vec3 dir = facing(q.toward);
	const Vec3 at = q.at + dir * travelled + Vec3{0.0, 0.05 * L * std::sin(0.2 * t + q.seed), 0.0};
	const Mat4 place = mul(translate(story(at)), facingOf(story(dir)));
	const double squeeze = within < 0.6 ? std::sin(kPi * within / 0.6) : 0.0;
	Mesh skin, suckers, eyes, pupils, lights, fins;
	const double M = 0.4 * L;   // (its mantle: the back of it, pointed, fins at its tip)
	Mesh mantle = lathe({{0.0, -M}, {0.04 * L, -0.8 * M}, {0.075 * L * (1.0 - 0.2 * squeeze), -0.3 * M}, {0.07 * L * (1.0 - 0.2 * squeeze), 0.0}, {0.05 * L, 0.05 * M}}, 24);
	for (Vec3 &v : mantle.verts) v = Vec3{v.x, v.z, v.y};
	for (Vec3 &n : mantle.normals) n = Vec3{n.x, n.z, n.y};
	for (auto &tr : mantle.tris) std::swap(tr[1], tr[2]);
	mantle.uvs.clear();
	add(skin, mantle);
	for (const double side : {1.0, -1.0}) fin(fins, {0.0, 0.0, -0.85 * M}, {side * 0.12 * L, 0.0, -0.75 * M + 0.02 * L * std::sin(2.0 * t)}, {0.0, 0.0, -0.55 * M});
	add(skin, ellipsoid({0.0, 0.0, 0.08 * L}, {0.055 * L, 0.05 * L, 0.06 * L}));   // (its head)
	for (const double side : {1.0, -1.0}) {
		add(eyes, ellipsoid({side * 0.05 * L, 0.01 * L, 0.08 * L}, Vec3{0.022, 0.022, 0.022} * L, 18, 12));
		add(pupils, ellipsoid({side * 0.07 * L, 0.01 * L, 0.08 * L}, Vec3{0.006, 0.012, 0.012} * L, 12, 8));
	}
	for (int a = 0; a < 10; ++a) {   // eight arms, and its two long tentacles trailing far behind, clubs at their ends
		const bool tentacle = a >= 8;
		const double ang = tentacle ? (a == 8 ? 0.6 : -0.6) : 2.0 * kPi * (a + 0.5) / 8.0, len = tentacle ? 1.1 * L : 0.32 * L;
		std::vector<Vec3> line;
		for (int j = 0; j <= 16; ++j) {
			const double u = j / 16.0, spread = (1.0 - squeeze) * 0.08 * L * u;
			line.push_back({std::sin(ang) * (0.03 * L + spread) + 0.04 * L * u * std::sin(1.1 * t - 4.0 * u + a), std::cos(ang) * (0.03 * L + spread), 0.12 * L + len * u * (tentacle ? 1.0 : 0.9)});
		}
		arm(skin, suckers, line, tentacle ? 0.008 * L : 0.018 * L, tentacle ? 0.004 * L : 0.003 * L, tentacle ? 4 : 2);
		if (tentacle) add(skin, ellipsoid(line.back(), Vec3{0.014, 0.01, 0.05} * L, 12, 8));
		if (t >= q.glow && !tentacle) photophores(lights, line, 0.004 * L, 0.0);
	}
	std::string out = "# a giant squid (film_abyss.h)\n";
	out += attr(coated({0.62, 0.22, 0.2}, 0.35), emitMesh(skin, place) + emitMesh(fins, place));
	out += attr(coated({0.85, 0.65, 0.6}, 0.4), emitMesh(suckers, place));
	out += attr(coated({0.75, 0.8, 0.85}, 0.03), emitMesh(eyes, place)) + attr(coated({0.02, 0.02, 0.03}, 0.05), emitMesh(pupils, place));
	if (!lights.tris.empty()) out += glowing({0.2, 1.3, 2.6}, smoothstep(q.glow, q.glow + 2.0, t), lights, place);
	return out;
}

// ---- the sea serpent ----
// Where a serpent's head is at nature's time t (pbrt's) along its points, and the distance along them.
inline double serpentAlong(const Serpent &s, double t) { return std::max(0.0, (t - s.from) * s.speed); }
inline Vec3 pathPoint(const Serpent &s, double d, Vec3 *way = nullptr) {
	if (s.path.empty()) return {};
	double left = d;
	for (std::size_t i = 1; i < s.path.size(); ++i) {
		const double seg = length(s.path[i] - s.path[i - 1]);
		if (left <= seg || i + 1 == s.path.size()) {
			if (way) *way = normalize(s.path[i] - s.path[i - 1] + Vec3{1e-9, 0.0, 0.0});
			return s.path[i - 1] + (s.path[i] - s.path[i - 1]) * (left / std::max(1e-9, seg));
		}
		left -= seg;
	}
	if (way) *way = {0.0, 0.0, 1.0};
	return s.path[0] + Vec3{0.0, 0.0, d};
}
inline Vec3 serpentHead(const Serpent &s, double t, Vec3 *way = nullptr) { return pathPoint(s, serpentAlong(s, t), way); }
inline std::string serpentText(const Serpent &s, double t) {
	using namespace detail;
	if (s.path.empty()) return std::string();
	const double head = serpentAlong(s, t), R = 0.022 * s.length;
	std::vector<Vec3> spine;
	std::vector<double> radii;
	const int n = 60;
	for (int i = 0; i <= n; ++i) {   // its body along the way its head came, in sideways waves running back along it
		const double u = static_cast<double>(i) / n, d = head - u * s.length;
		Vec3 way;
		Vec3 p = pathPoint(s, std::max(0.0, d), &way);
		if (d < 0.0) p = s.path[0] + way * d;
		const Vec3 side = normalize(cross(Vec3{0.0, 1.0, 0.0}, way + Vec3{1e-9, 0.0, 0.0}));
		const double wave = 0.06 * s.length * std::sin(2.0 * kPi * (2.5 * u - 0.4 * t) + s.seed) * (0.3 + 0.7 * u);
		p = p + side * wave + Vec3{0.0, 0.02 * s.length * std::sin(2.0 * kPi * (1.2 * u - 0.25 * t)), 0.0};
		spine.push_back(story(p));
		radii.push_back(R * (u < 0.06 ? 0.75 + 4.0 * u : 1.0 - 0.85 * std::pow(std::max(0.0, u - 0.06) / 0.94, 1.4)));
	}
	Mesh body = flow::tube(spine, radii, 14), crest, eyes, lights;
	for (int i = 2; i < n - 4; i += 2) {   // its crest down its back
		const Vec3 a = spine[static_cast<std::size_t>(i)], b = spine[static_cast<std::size_t>(i + 2)];
		const double r = radii[static_cast<std::size_t>(i)];
		fin(crest, a + Vec3{0.0, 0.9 * r, 0.0}, b + Vec3{0.0, 0.9 * r, 0.0}, (a + b) * 0.5 + Vec3{0.0, 2.2 * r, 0.0} + normalize(a - b) * (0.6 * r));
		if (t >= s.glow) add(lights, ellipsoid(a + Vec3{0.0, 0.0, 0.0} + normalize(cross(b - a, Vec3{0.0, 1.0, 0.0})) * (0.95 * r), Vec3{0.12, 0.12, 0.12} * r, 6, 4));
	}
	const Vec3 h = spine[0], hw = normalize(spine[0] - spine[1]);   // its head: a snout, eyes, horns
	const Mat4 hp = mul(translate(h), facingOf(hw));
	Mesh skull;
	add(skull, ellipsoid({0.0, 0.1 * R, 0.6 * R}, {0.95 * R, 0.75 * R, 1.6 * R}), hp);
	add(skull, sdf::tubeMesh({{0.5 * R, 0.6 * R, 0.0}, {0.9 * R, 1.6 * R, -1.2 * R}}, 0.15 * R, 0.3, 6), hp);
	add(skull, sdf::tubeMesh({{-0.5 * R, 0.6 * R, 0.0}, {-0.9 * R, 1.6 * R, -1.2 * R}}, 0.15 * R, 0.3, 6), hp);
	for (const double side : {1.0, -1.0}) add(eyes, ellipsoid({side * 0.6 * R, 0.35 * R, 1.0 * R}, Vec3{0.22, 0.2, 0.22} * R, 12, 8), hp);
	std::string out = "# a sea serpent: " + s.name + " (film_abyss.h)\n";
	out += attr(coated({0.12, 0.38, 0.32}, 0.25), emitMesh(body, identity()) + emitMesh(skull, identity()));
	out += attr(coated({0.75, 0.3, 0.12}, 0.35), emitMesh(crest, identity()));
	out += glowing({2.6, 1.8, 0.3}, 0.6, eyes, identity());
	if (!lights.tris.empty()) out += glowing({0.2, 1.8, 1.4}, smoothstep(s.glow, s.glow + 2.0, t), lights, identity());
	return out;
}

// ---- the huge eye ----
inline std::string eyeText(const Eye &e, double t) {
	using namespace detail;
	const double S = 0.5 * e.size;
	const double open = smoothstep(e.open, e.open + 1.8, t) * (1.0 - smoothstep(e.close, e.close + 0.8, t)), blink = std::fmod(t - e.open, 9.0) < 0.25 && t > e.open + 3.0 ? 0.7 : 0.0;
	const double lids = std::clamp(open - blink, 0.0, 1.0);
	const Mat4 place = mul(translate(story(e.at)), facingOf(story(facing(e.toward))));
	Mesh ball = uvSphere(S, 32, 20), iris, pupil, skin;
	ball.uvs.clear();
	add(iris, ellipsoid({0.0, 0.0, 0.93 * S}, {0.55 * S, 0.55 * S, 0.12 * S}, 28, 12));
	add(pupil, ellipsoid({0.0, 0.0, 1.01 * S}, {0.09 * S, 0.42 * S, 0.04 * S}, 16, 10));
	// the lids: the skin round it, closing over it from above and below
	const double gap = (2.0 + 52.0 * lids) * kPi / 180.0;   // (each lid: a cap of a slightly bigger ball, from its pole to `gap` short of the eye's middle)
	for (const double side : {1.0, -1.0}) {
		std::vector<std::pair<double, double>> arc;
		for (int k = 0; k <= 12; ++k) {
			const double phi = (kPi / 2.0 - gap) * k / 12.0;
			arc.push_back({1.06 * S * std::sin(phi), side * 1.06 * S * std::cos(phi)});
		}
		Mesh lid = lathe(arc, 32);
		lid.uvs.clear();
		add(skin, lid);
	}
	add(skin, torus(1.15 * S, 0.35 * S, 36, 12), rotate({1, 0, 0}, 90.0));
	std::string out = "# a huge eye in the dark (film_abyss.h)\n";
	out += attr(coated({0.85, 0.82, 0.7}, 0.08), emitMesh(ball, place));
	out += glowing({2.2, 1.4, 0.2}, 0.35 * lids + 0.02, iris, place);
	out += attr(coated({0.01, 0.01, 0.01}, 0.05), emitMesh(pupil, place));
	out += attr(coated({0.22, 0.2, 0.18}, 0.5), emitMesh(skin, place));
	return out;
}

// ---- a light that reveals ----
inline std::string revealText(const Reveal &r, double t) {
	const double a = smoothstep(r.t, r.t + r.over, t);
	if (a <= 0.0) return std::string();
	const Vec3 I = r.colour * (40.0 * r.bright * a);   // (the water scatters it everywhere: much more and the whole sea glows)
	char buf[240];
	std::snprintf(buf, sizeof buf, "# a light that reveals (film_abyss.h)\nAttributeBegin\n  LightSource \"point\" \"point3 from\" [ %.4f %.4f %.4f ] \"rgb I\" [ %.3f %.3f %.3f ]\nAttributeEnd\n", r.at.x, r.at.y, r.at.z, I.x, I.y, I.z);
	return buf;
}

// ---- a whale fall ----
inline std::string whaleFallText(const WhaleFall &w, double t, const std::function<double(double, double)> &bedAt) {
	using namespace detail;
	const double L = w.length;
	const Vec3 dir = facing(w.toward), side = normalize(cross(Vec3{0.0, 1.0, 0.0}, dir));
	const auto onBed = [&](double along, double across, double lift) {
		const double x = w.x + dir.x * along + side.x * across, z = w.z + dir.z * along + side.z * across;
		return story({x, bedAt(x, z) + lift, z});
	};
	Mesh bones, mats, crabs, hags;
	for (int v = 0; v < 34; ++v) {   // its spine of vertebrae, curving a little
		const double along = -0.45 * L + 0.75 * L * v / 33.0, bend = 0.04 * L * std::sin(v * 0.2 + w.seed);
		add(bones, ellipsoid(onBed(along, bend, 0.18 * L / 14.0), Vec3{0.14, 0.12, 0.1} * (L / 14.0) * (1.0 - 0.6 * std::max(0.0, -along / (0.45 * L))), 10, 6));
	}
	for (int r = 0; r < 14; ++r) {   // its ribs, arching over and fallen to the sides
		const double along = 0.05 * L + 0.02 * L * r;
		for (const double sd : {1.0, -1.0}) {
			std::vector<Vec3> arc;
			const double fall = 0.6 + 0.4 * hash01(r, w.seed + (sd > 0 ? 1 : 2));
			for (int j = 0; j <= 10; ++j) {
				const double u = j / 10.0, a = kPi * 0.5 * u;
				arc.push_back(onBed(along + 0.02 * L * u, sd * 0.16 * L * std::sin(a) * (1.0 + 0.3 * fall), (0.2 * L * std::cos(a) * (1.0 - fall) + 0.02 * L) * (1.0 - 0.6 * u)));
			}
			add(bones, sdf::tubeMesh(arc, 0.02 * L / 14.0 * 3.0, 0.5, 6));
		}
	}
	add(bones, ellipsoid(onBed(0.42 * L, 0.0, 0.2 * L / 14.0), Vec3{0.7, 0.35, 1.4} * (L / 14.0), 22, 12));   // (its skull)
	for (const double sd : {1.0, -1.0}) add(bones, sdf::tubeMesh({onBed(0.45 * L, sd * 0.2 * L / 14.0, 0.05), onBed(0.6 * L, sd * 0.5 * L / 14.0, 0.05), onBed(0.75 * L, sd * 0.3 * L / 14.0, 0.05)}, 0.12 * L / 14.0, 0.6, 8));   // (its jaws)
	for (int q = 0; q < 160; ++q) {   // bacterial mats, white, in patches hugging the bones
		const double along = (hash01(q, w.seed + 3) - 0.55) * 0.9 * L, across = (hash01(q, w.seed + 4) - 0.5) * (hash01(q, w.seed + 7) < 0.7 ? 0.12 : 0.35) * L;
		add(mats, ellipsoid(onBed(along, across, 0.01), {0.06 + 0.14 * hash01(q, 5), 0.008, 0.05 + 0.12 * hash01(q, 6)}, 8, 4));
	}
	for (int c = 0; c < 8; ++c) {   // crabs picking over it, and hagfish winding through
		const double a = 0.4 * t + 2.0 * kPi * c / 8.0, along = 0.4 * L * std::cos(a * 0.5 + c), across = 0.18 * L * std::sin(a + c);
		add(crabs, ellipsoid(onBed(along, across, 0.06), {0.12, 0.05, 0.09}, 10, 6));
	}
	for (int h = 0; h < 5; ++h) {
		std::vector<Vec3> eel;
		for (int j = 0; j <= 10; ++j) {
			const double u = j / 10.0, s0 = std::fmod(0.3 * t + 0.2 * h, 1.0) * L - 0.5 * L;
			eel.push_back(onBed(s0 - 0.5 * u, 0.12 * L * std::sin(h * 1.7) + 0.12 * std::sin(6.0 * u - 4.0 * t + h), 0.3 + 0.05 * h));
		}
		add(hags, sdf::tubeMesh(eel, 0.025, 0.4, 6));
	}
	std::string out = "# a whale fall (film_abyss.h)\n";
	out += attr(coated({0.82, 0.78, 0.66}, 0.5), emitMesh(bones, identity()));
	out += attr(diffuseMaterial({0.92, 0.85, 0.7}), emitMesh(mats, identity()));
	out += attr(coated({0.78, 0.3, 0.2}, 0.4), emitMesh(crabs, identity()));
	out += attr(coated({0.6, 0.45, 0.45}, 0.4), emitMesh(hags, identity()));
	return out;
}

}  // namespace abyss
}  // namespace cartoon
