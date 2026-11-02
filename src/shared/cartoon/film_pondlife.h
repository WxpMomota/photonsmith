#pragma once
// film_pondlife.h -- animals of rivers, lakes and shores (water round 7): `pondlife KIND [N] at X Z [radius R] [young N] [toward A] [at-times T...] [seed K]`.
//
//   ducks       a pair and their brood: drake and duck wander about the water leaving a thread of a wake, now and then up-ending to feed; `young N` ducklings paddle in line behind the first
//   swans       the same, slower, their necks in an S
//   heron       stands in the shallows on its long legs, one slow step at a time; at each `strike T` its neck shoots down, a plop, and it comes up with a fish
//   kingfisher  on a perch over the water, a flash of blue and orange; at each `dive T` it goes in head first and is back on the perch with a fish
//   frogs       sit about in a patch of water and jump, one at a time, with a plop
//   otter       floats on its back, turning over now and then
//   beaver      a head and a wake going across the water; at each `slap T` its tail comes down flat, a crack and a big ring, and it is gone for a while
//   trout       rising: a ring here and there in a patch, a back and a tail through the surface
//   salmon      leaping (`toward A`) up out of the water again and again, at the foot of a fall
//
// Coordinates: pbrt's in a film file; meshes in story space (x mirrored). What they do is worked out when the film is loaded (`prepare`), so rings and sounds follow it.

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

#include "cartoon_lib.h"
#include "film_craft.h"
#include "film_stir.h"
#include "sdf_mesh.h"

namespace cartoon {
namespace pondlife {

// Something it does that touches the water.
struct Touch {
	double t = 0.0, x = 0.0, z = 0.0, size = 0.05;
	int kind = 0;   // (0 a plop, 1 a slap, 2 a rise)
};
struct Life {
	std::string kind = "ducks";
	int count = 1, young = 0, seed = 1;
	double x = 0.0, z = 0.0, radius = 6.0, toward = 0.0, height = 1.6;
	std::vector<double> times;   // (a heron's strikes, a kingfisher's dives, a beaver's slaps; nature's time)
	std::vector<Touch> touches;  // (worked out on load)
	std::vector<Touch> leaps;    // (a salmon's take-offs, each its own: in `touches`, sorted by time, one fish's landing came between another's take-off and landing)
};
inline const std::vector<std::string> &kinds() {
	static const std::vector<std::string> k = {"ducks", "swans", "heron", "kingfisher", "frogs", "otter", "beaver", "trout", "salmon"};
	return k;
}

namespace detail {
inline Vec3 story(Vec3 p) { return {-p.x, p.y, p.z}; }
// What is drawn, by colour.
struct Parts {
	std::vector<std::pair<Vec3, Mesh>> list;
	void add(Vec3 colour, const Mesh &m, const Mat4 &place) {
		for (auto &p : list)
			if (length(p.first - colour) < 1e-6) {
				craft::detail::add(p.second, m, place);
				return;
			}
		list.push_back({colour, Mesh{}});
		craft::detail::add(list.back().second, m, place);
	}
	void ball(Vec3 colour, Vec3 at, Vec3 radii, const Mat4 &place, int nu = 14, int nv = 9) { add(colour, uvSphere(1.0, nu, nv), mul(place, translate(at), scale(radii.x, radii.y, radii.z))); }
	void tube(Vec3 colour, const std::vector<Vec3> &pts, double r, const Mat4 &place, double end = 0.6, int sides = 7) { add(colour, sdf::tubeMesh(pts, r, end, sides), place); }
	std::string text(double rough = 0.45) const {
		std::string out;
		for (const auto &p : list) out += attr(coated(p.first, rough), emitMesh(p.second, identity()));
		return out;
	}
};
inline Vec3 wanderAt(const Life &l, int n, double t, double pace = 0.09) {
	const int k = l.seed * 97 + n;
	const double a = t * pace * (0.7 + 0.5 * hash01(k, 1)) + 6.283 * hash01(k, 2);
	return {l.x + l.radius * (0.7 * std::sin(a) + 0.3 * std::sin(2.3 * a + 6.0 * hash01(k, 3))), 0.0, l.z + l.radius * (0.7 * std::cos(1.3 * a) + 0.3 * std::sin(1.7 * a + 6.0 * hash01(k, 4)))};
}
inline Vec3 wayOf(const std::function<Vec3(double)> &at, double t) {
	const Vec3 d = at(t + 0.4) - at(t - 0.4);
	return length(d) > 1e-6 ? normalize(d) : Vec3{0.0, 0.0, 1.0};
}
inline Mat4 placed(Vec3 at, Vec3 way) { return mul(translate(story(at)), craft::sealifeFacing(story(way))); }
// Where a heron stands at t (pbrt's) and the way it faces: a slow step every nine seconds along `toward`, three metres there and three back.
inline Vec3 heronAt(const Life &l, double t, Vec3 &facing) {
	facing = {std::sin(l.toward * kPi / 180.0), 0.0, std::cos(l.toward * kPi / 180.0)};
	const double stepEvery = 9.0, n = std::floor(t / stepEvery), f = t / stepEvery - n;
	const double along = std::fmod(0.35 * (n + smoothstep(0.0, 0.2, f)), 6.0);
	if (along > 3.0) {   // (coming back: turned round, not snapped back to the start)
		const Vec3 start = Vec3{l.x, 0.0, l.z} + facing * 3.0;
		facing = facing * -1.0;
		return start + facing * (along - 3.0);
	}
	return Vec3{l.x, 0.0, l.z} + facing * along;
}

inline void duck(Parts &p, const Mat4 &at, int sort, double t, int seed) {   // sort 0 drake, 1 duck, 2 duckling
	const double s = sort == 2 ? 0.42 : 1.0;
	Mat4 m = mul(at, scale(s));
	const double cycle = 19.0 + 13.0 * hash01(seed, 5), f = std::fmod(t + 40.0 * hash01(seed, 6), cycle), up = sort == 2 ? 0.0 : smoothstep(0.0, 0.5, f) * (1.0 - smoothstep(3.0, 3.6, f));
	if (up > 0.0) m = mul(m, rotate({1.0, 0.0, 0.0}, 105.0 * up));   // (up-ended, feeding)
	const Vec3 chest = sort == 0 ? Vec3{0.34, 0.18, 0.1} : sort == 1 ? Vec3{0.42, 0.3, 0.18} : Vec3{0.74, 0.6, 0.22};
	const Vec3 flank = sort == 0 ? Vec3{0.62, 0.6, 0.54} : sort == 1 ? Vec3{0.4, 0.3, 0.2} : Vec3{0.46, 0.34, 0.16};
	const Vec3 head = sort == 0 ? Vec3{0.03, 0.26, 0.12} : sort == 1 ? Vec3{0.45, 0.34, 0.22} : Vec3{0.8, 0.68, 0.25};
	const Vec3 bill = sort == 0 ? Vec3{0.85, 0.68, 0.1} : sort == 1 ? Vec3{0.75, 0.42, 0.1} : Vec3{0.35, 0.25, 0.12};
	p.ball(flank, {0.0, 0.05, -0.02}, {0.1, 0.075, 0.17}, m);
	p.ball(chest, {0.0, 0.07, 0.11}, {0.085, 0.075, 0.09}, m);
	p.ball(flank, {0.0, 0.1, -0.12}, {0.045, 0.03, 0.09}, mul(m, rotate({1.0, 0.0, 0.0}, -22.0)));
	p.tube(head, {{0.0, 0.1, 0.13}, {0.0, 0.17, 0.15}}, 0.03, m, 0.85);
	if (sort == 0) p.add({0.92, 0.92, 0.88}, torus(0.03, 0.006, 12, 4), mul(m, translate(0.0, 0.135, 0.14), rotate({1.0, 0.0, 0.0}, 12.0)));
	p.ball(head, {0.0, 0.2, 0.16}, {0.044, 0.044, 0.052}, m);
	p.ball(bill, {0.0, 0.192, 0.215}, {0.022, 0.009, 0.035}, m, 8, 5);
	p.ball({0.02, 0.02, 0.02}, {0.033, 0.208, 0.18}, {0.006, 0.006, 0.006}, m, 6, 4);
	p.ball({0.02, 0.02, 0.02}, {-0.033, 0.208, 0.18}, {0.006, 0.006, 0.006}, m, 6, 4);
	for (const double side : {-1.0, 1.0}) {   // (round 18) its feet paddling under the water, in turn: what a camera under it sees
		const double kick = std::sin(2.0 * kPi * 2.2 * t + (side > 0.0 ? kPi : 0.0) + seed);
		const Vec3 hip{side * 0.04, 0.0, -0.03}, ankle{side * 0.05, -0.075, -0.05 - 0.045 * kick};
		p.tube({0.85, 0.48, 0.1}, {hip, ankle}, 0.007, m, 0.8, 5);
		p.ball({0.9, 0.5, 0.1}, ankle + Vec3{0.0, -0.004, -0.018}, {0.026, 0.004 + 0.003 * std::max(0.0, kick), 0.032}, m, 8, 4);
	}
}
inline void swan(Parts &p, const Mat4 &m, double t, int seed) {
	const Vec3 white{0.92, 0.92, 0.88};
	const double sway = 0.012 * std::sin(0.5 * t + seed);
	p.ball(white, {0.0, 0.1, -0.05}, {0.2, 0.15, 0.38}, m);
	p.ball(white, {0.0, 0.2, -0.26}, {0.1, 0.09, 0.18}, mul(m, rotate({1.0, 0.0, 0.0}, -32.0)));
	for (const double side : {-1.0, 1.0}) p.ball(white, {side * 0.13, 0.19, -0.1}, {0.075, 0.08, 0.26}, mul(m, rotate({1.0, 0.0, 0.0}, -9.0)));
	p.tube(white, {{0.0, 0.14, 0.26}, {0.0, 0.3, 0.36}, {sway, 0.46, 0.33}, {sway, 0.6, 0.27}, {sway, 0.68, 0.31}}, 0.034, m, 0.75, 8);
	p.ball(white, {sway, 0.7, 0.33}, {0.038, 0.036, 0.055}, m);
	p.ball({0.85, 0.36, 0.07}, {sway, 0.685, 0.395}, {0.017, 0.012, 0.042}, m, 8, 5);
	p.ball({0.02, 0.02, 0.02}, {sway, 0.705, 0.365}, {0.018, 0.016, 0.014}, m, 8, 5);
}
inline void fishIn(Parts &p, Vec3 at, double L, const Mat4 &m) { p.ball({0.78, 0.8, 0.82}, at, {0.012 * L / 0.1, 0.018 * L / 0.1, 0.5 * L}, m, 8, 5); }
inline void heron(Parts &p, const Mat4 &m, double strike, bool fish, double step) {   // strike 0 at rest to 1 at the water
	const Vec3 grey{0.5, 0.53, 0.56}, pale{0.82, 0.82, 0.8}, legs{0.32, 0.28, 0.2}, bill{0.8, 0.62, 0.2};
	const double H = 0.72;
	for (const double side : {-1.0, 1.0}) {
		const double swing = side * 0.1 * step;
		p.tube(legs, {{side * 0.05, -0.3, swing}, {side * 0.05, 0.3, swing + 0.02}, {side * 0.05, H + 0.04, 0.0}}, 0.015, m, 1.0, 6);
	}
	const Mat4 body = mul(m, translate(0.0, H + 0.14, -0.04), rotate({1.0, 0.0, 0.0}, -34.0));   // (held upright, its tail down)
	p.ball(grey, {0.0, 0.0, 0.0}, {0.085, 0.1, 0.25}, body);
	p.ball({0.3, 0.33, 0.37}, {0.0, 0.02, -0.26}, {0.055, 0.03, 0.2}, body);
	for (const double side : {-1.0, 1.0}) p.ball({0.36, 0.38, 0.42}, {side * 0.075, 0.015, -0.05}, {0.025, 0.07, 0.22}, body);
	const Vec3 base{0.0, H + 0.24, 0.13};
	const Vec3 rest{0.0, H + 0.62, 0.2}, reach{0.0, 0.08, 0.72};
	const Vec3 head = lerp(rest, reach, strike), bend = lerp(Vec3{0.0, H + 0.42, 0.02}, Vec3{0.0, H - 0.1, 0.5}, strike);
	p.tube(pale, {base, bend, lerp(bend, head, 0.5), head}, 0.024, m, 0.65, 7);
	p.ball(pale, head, {0.034, 0.036, 0.06}, m);
	const Vec3 ahead = normalize(lerp(Vec3{0.0, -0.1, 1.0}, Vec3{0.0, -1.0, 0.3}, strike));
	p.tube(bill, {head, head + ahead * 0.17}, 0.016, m, 0.12, 6);
	p.tube({0.03, 0.03, 0.05}, {head + Vec3{0.0, 0.02, -0.02}, head + Vec3{0.0, 0.03, -0.2}}, 0.007, m, 0.2, 5);
	if (fish) fishIn(p, head + ahead * 0.17, 0.15, m);
}
inline void kingfisher(Parts &p, const Mat4 &m, bool fish) {
	const Vec3 blue{0.03, 0.42, 0.72}, orange{0.86, 0.42, 0.08};
	p.ball(blue, {0.0, 0.0, -0.01}, {0.032, 0.036, 0.06}, m);
	p.ball(orange, {0.0, -0.012, 0.012}, {0.03, 0.03, 0.05}, m);
	p.ball(blue, {0.0, 0.04, 0.045}, {0.034, 0.032, 0.035}, m);
	p.ball(blue, {0.0, -0.005, -0.08}, {0.012, 0.008, 0.04}, m);
	p.tube({0.05, 0.05, 0.06}, {{0.0, 0.038, 0.07}, {0.0, 0.034, 0.135}}, 0.008, m, 0.15, 5);
	if (fish) fishIn(p, {0.0, 0.03, 0.14}, 0.06, m);
}
inline void frog(Parts &p, const Mat4 &m, double crouch) {
	const Vec3 green{0.24, 0.42, 0.14}, pale{0.72, 0.74, 0.5};
	const double h = lerp(0.03, 0.02, crouch);
	p.ball(green, {0.0, h + 0.015, 0.0}, {0.033, 0.024, 0.05}, m);
	p.ball(pale, {0.0, h, 0.01}, {0.03, 0.016, 0.04}, m);
	for (const double side : {-1.0, 1.0}) {
		p.ball({0.72, 0.6, 0.15}, {side * 0.022, h + 0.038, 0.032}, {0.011, 0.011, 0.011}, m, 8, 5);
		p.tube(green, {{side * 0.03, h, -0.03}, {side * 0.065, h + 0.02, -0.01}, {side * 0.05, 0.0, -0.05}, {side * 0.075, 0.0, 0.01}}, 0.009, m, 0.6, 5);
		p.tube(green, {{side * 0.028, h, 0.03}, {side * 0.04, 0.0, 0.05}}, 0.006, m, 0.7, 5);
	}
}
inline void otter(Parts &p, const Mat4 &m, double t, int seed) {
	const Vec3 fur{0.26, 0.17, 0.1}, belly{0.5, 0.4, 0.3};
	const Mat4 r = mul(m, rotate({0.0, 0.0, 1.0}, std::fmod(t + seed, 16.0) < 2.0 ? 180.0 * smoothstep(0.0, 2.0, std::fmod(t + seed, 16.0)) : 0.0));
	p.ball(fur, {0.0, 0.0, 0.0}, {0.13, 0.09, 0.42}, r);
	p.ball(belly, {0.0, 0.05, 0.08}, {0.1, 0.06, 0.25}, r);
	p.ball(fur, {0.0, 0.06, 0.48}, {0.08, 0.07, 0.1}, r);
	p.ball({0.05, 0.03, 0.02}, {0.0, 0.08, 0.57}, {0.022, 0.016, 0.016}, r, 8, 5);
	for (const double side : {-1.0, 1.0}) p.ball({0.04, 0.03, 0.02}, {side * 0.05, 0.1, 0.5}, {0.012, 0.012, 0.012}, r, 6, 4), p.ball(fur, {side * 0.06, 0.1, 0.2}, {0.025, 0.025, 0.06}, r, 8, 5);
	p.tube(fur, {{0.0, 0.0, -0.4}, {0.0, 0.02, -0.65}, {0.0, 0.03, -0.85}}, 0.06, r, 0.25, 7);
}
inline void beaver(Parts &p, const Mat4 &m, double slap) {
	const Vec3 fur{0.28, 0.18, 0.1};
	p.ball(fur, {0.0, -0.03, -0.1}, {0.14, 0.1, 0.32}, m);
	p.ball(fur, {0.0, 0.03, 0.24}, {0.09, 0.075, 0.11}, m);
	p.ball({0.04, 0.03, 0.02}, {0.0, 0.05, 0.34}, {0.024, 0.018, 0.016}, m, 8, 5);
	for (const double side : {-1.0, 1.0}) p.ball({0.15, 0.1, 0.06}, {side * 0.07, 0.1, 0.2}, {0.016, 0.016, 0.01}, m, 6, 4);
	if (slap > 0.0) p.ball({0.09, 0.08, 0.08}, {0.0, 0.16 * std::sin(kPi * slap) * 2.0, -0.55}, {0.09, 0.016, 0.18}, mul(m, rotate({1.0, 0.0, 0.0}, -46.0 * std::sin(kPi * slap))));
}
inline void fishBack(Parts &p, const Mat4 &m, double L) {
	const Vec3 back{0.32, 0.33, 0.27}, side{0.8, 0.76, 0.66};
	p.ball(back, {0.0, 0.02 * L, 0.0}, {0.09 * L, 0.11 * L, 0.5 * L}, m);
	p.ball(side, {0.0, -0.02 * L, 0.0}, {0.095 * L, 0.1 * L, 0.48 * L}, m);
	p.ball(back, {0.0, 0.0, -0.54 * L}, {0.012 * L, 0.11 * L, 0.1 * L}, m, 8, 5);
	p.ball(back, {0.0, 0.13 * L, 0.05 * L}, {0.008 * L, 0.05 * L, 0.08 * L}, m, 8, 5);
}
}  // namespace detail

// A beaver is under the water for a while after each slap.
inline double goneAfter(const Life &l, double t) {
	for (const double u : l.times)
		if (t > u + 0.35 && t < u + 7.0) return smoothstep(u + 0.35, u + 0.8, t) * (1.0 - smoothstep(u + 6.3, u + 7.0, t));
	return 0.0;
}
// Works out what it does to the water over a film of `length` seconds.
inline void prepare(Life &l, double length) {
	l.touches.clear();
	const auto put = [&l](double t, double x, double z, double size, int kind) { l.touches.push_back({t, x, z, size, kind}); };
	if (l.kind == "heron")
		for (const double u : l.times) {   // (where its bill goes in: in front of where it stands then)
			Vec3 facing;
			const Vec3 at = detail::heronAt(l, u + 0.22, facing) + facing * 0.72;
			put(u + 0.22, at.x, at.z, 0.05, 0);
		}
	if (l.kind == "kingfisher")
		for (const double u : l.times) {
			const double a = l.toward * kPi / 180.0;
			put(u + 0.55, l.x + 1.4 * std::sin(a), l.z + 1.4 * std::cos(a), 0.05, 0);
			put(u + 1.0, l.x + 1.4 * std::sin(a), l.z + 1.4 * std::cos(a), 0.04, 0);
		}
	if (l.kind == "beaver")
		for (const double u : l.times) {
			const Vec3 p = detail::wanderAt(l, 0, u, 0.07);
			put(u + 0.3, p.x, p.z, 0.32, 1);
		}
	if (l.kind == "frogs")
		for (int n = 0; n < l.count; ++n)
			for (double t = 2.0 + 7.0 * hash01(l.seed + n, 1), g = 0; t < length; t += 7.0 + 11.0 * hash01(l.seed + n, static_cast<int>(g) + 3), ++g) {
				const int k = l.seed * 41 + n * 7;
				const double a = 6.283 * hash01(k, static_cast<int>(g) + 5), d = l.radius * std::sqrt(hash01(k, static_cast<int>(g) + 200));
				put(t, l.x + d * std::cos(a), l.z + d * std::sin(a), 0.035, 0);
			}
	if (l.kind == "trout")
		for (int n = 0; n < l.count; ++n)
			for (double t = 1.0 + 6.0 * hash01(l.seed + n, 2), g = 0; t < length; t += 3.0 + 6.0 * hash01(l.seed + n, static_cast<int>(g) + 3), ++g) {
				const int k = l.seed * 43 + n * 11;
				const double a = 6.283 * hash01(k, static_cast<int>(g) + 5), d = l.radius * std::sqrt(hash01(k, static_cast<int>(g) + 200));
				put(t, l.x + d * std::cos(a), l.z + d * std::sin(a), 0.07, 2);
			}
	l.leaps.clear();
	if (l.kind == "salmon") {
		const double a = l.toward * kPi / 180.0;
		for (int n = 0; n < l.count; ++n)
			for (double t = 1.0 + 4.0 * hash01(l.seed + n, 2), g = 0; t < length; t += 3.0 + 4.0 * hash01(l.seed + n, static_cast<int>(g) + 3), ++g) {
				const int k = l.seed * 47 + n * 13;
				const double x = l.x + l.radius * (hash01(k, static_cast<int>(g) + 5) - 0.5), z = l.z + l.radius * (hash01(k, static_cast<int>(g) + 200) - 0.5);
				put(t, x, z, 0.14, 0);
				put(t + 1.05, x + 1.3 * std::sin(a), z + 1.3 * std::cos(a), 0.16, 0);
				l.leaps.push_back({t, x, z, 0.14, 0});
			}
	}
	std::sort(l.touches.begin(), l.touches.end(), [](const Touch &a, const Touch &b) { return a.t < b.t; });
}

// The animals at t (nature's) as scene text. `levelAt`: the water's height at pbrt's (x, z).
template <class LevelAt>
inline std::string text(const Life &l, double t, LevelAt levelAt) {
	using namespace detail;
	Parts p;
	const auto onWater = [&](Vec3 q, double lift = 0.0) { q.y = levelAt(q.x, q.z) + lift; return q; };
	const Vec3 facing{std::sin(l.toward * kPi / 180.0), 0.0, std::cos(l.toward * kPi / 180.0)};
	if (l.kind == "ducks" || l.kind == "swans") {
		for (int n = 0; n < l.count; ++n)
			for (int who = 0; who < 2; ++who) {
				const auto at = [&l, n, who](double u) { return wanderAt(l, n, u - 2.2 * who, l.kind == "swans" ? 0.05 : 0.09); };
				const Mat4 m = placed(onWater(at(t), 0.02 * std::sin(1.6 * t + n)), wayOf(at, t));
				if (l.kind == "swans") swan(p, m, t, l.seed + n + who);
				else duck(p, m, who, t, l.seed + 3 * n + who);
			}
		for (int y = 0; y < l.young; ++y) {
			const int k = l.seed * 59 + y;
			const auto at = [&l, y, k](double u) { return wanderAt(l, 0, u - 2.2 - 3.0 * (y + 1), l.kind == "swans" ? 0.05 : 0.09) + Vec3{0.25 * std::sin(0.8 * u + k), 0.0, 0.25 * std::cos(0.7 * u + 2 * k)}; };
			const Mat4 m = placed(onWater(at(t)), wayOf(at, t));
			if (l.kind == "swans") duck(p, mul(m, scale(1.3)), 2, t, k);
			else duck(p, m, 2, t, k);
		}
	} else if (l.kind == "heron") {
		double strike = 0.0;
		bool fish = false;
		for (const double u : l.times) {
			strike = std::max(strike, smoothstep(u, u + 0.2, t) * (1.0 - smoothstep(u + 0.3, u + 0.7, t)));
			if (t > u + 0.25 && t < u + 2.4) fish = true;
		}
		const double f = t / 9.0 - std::floor(t / 9.0), step = f < 0.2 ? std::sin(kPi * f / 0.2) : 0.0;
		Vec3 way;
		const Vec3 at = heronAt(l, t, way);
		heron(p, placed(onWater(at, -0.1), way), strike, fish, step);
	} else if (l.kind == "kingfisher") {
		const Vec3 perch{l.x, levelAt(l.x, l.z) + l.height, l.z}, water = perch + facing * 1.4 + Vec3{0.0, -l.height, 0.0};
		p.tube({0.26, 0.2, 0.14}, {story(perch + facing * -0.7 + Vec3{0.0, -0.24, 0.0}), story(perch + Vec3{0.0, -0.05, 0.0}), story(perch + facing * 0.32 + Vec3{0.0, 0.03, 0.0})}, 0.017, identity(), 0.4);
		Vec3 at = perch, way = facing;
		bool fish = false, shown = true;
		for (const double u : l.times) {
			if (t < u || t > u + 2.2) continue;
			const double f = t - u;
			if (f < 0.55) at = lerp(perch, water, f / 0.55) + Vec3{0.0, 0.3 * std::sin(kPi * f / 0.55), 0.0}, way = normalize(water - perch);
			else if (f < 1.0) shown = false;
			else at = lerp(water, perch, smoothstep(1.0, 2.0, f)) + Vec3{0.0, 0.3 * std::sin(kPi * std::min(1.0, f - 1.0)), 0.0}, way = normalize(perch - water), fish = true;
		}
		if (t > 0.0)
			for (const double u : l.times)
				if (t > u + 2.0 && t < u + 6.0) fish = true;
		if (shown) kingfisher(p, mul(translate(story(at)), craft::sealifeFacing(story(way))), fish);
	} else if (l.kind == "frogs") {
		for (int n = 0; n < l.count; ++n) {
			const int k = l.seed * 41 + n * 7;
			Vec3 from{l.x + l.radius * (hash01(k, 1) - 0.5), 0.0, l.z + l.radius * (hash01(k, 2) - 0.5)}, to = from;
			double when = -1e9;
			int seen = 0;
			for (const Touch &q : l.touches) {
				if (seen++ % l.count != n) continue;
				if (q.t - 0.5 > t) {
					to = {q.x, 0.0, q.z}, when = q.t - 0.5;
					break;
				}
				from = {q.x, 0.0, q.z};
			}
			const double u = when > -1e8 ? std::clamp((t - when) / 0.5, 0.0, 1.0) : 0.0;
			const Vec3 at = lerp(from, to, u) + Vec3{0.0, 0.22 * std::sin(kPi * u), 0.0};
			frog(p, placed(onWater(at, at.y), normalize(to - from + Vec3{1e-6, 0.0, 0.0})), u > 0.0 ? 0.0 : 1.0);
		}
	} else if (l.kind == "otter") {
		for (int n = 0; n < l.count; ++n) {
			const auto at = [&l, n](double u) { return wanderAt(l, n, u, 0.06); };
			otter(p, placed(onWater(at(t), 0.02), wayOf(at, t)), t, l.seed + n);
		}
	} else if (l.kind == "beaver") {
		const double gone = goneAfter(l, t);
		const auto at = [&l](double u) { return wanderAt(l, 0, u, 0.07); };
		double slap = 0.0;
		for (const double u : l.times)
			if (t > u && t < u + 0.35) slap = (t - u) / 0.35;
		if (gone < 0.95) beaver(p, placed(onWater(at(t), -0.28 * gone), wayOf(at, t)), slap);
	} else if (l.kind == "trout") {
		for (const Touch &q : l.touches) {
			if (t < q.t - 0.12 || t > q.t + 0.34) continue;
			const double u = (t - q.t + 0.12) / 0.46, a = hash01(static_cast<int>(q.t * 100.0), 1) * 360.0;
			const Mat4 m = mul(translate(story(onWater({q.x, 0.0, q.z}, 0.07 * std::sin(kPi * u) - 0.05))), rotate({0.0, 1.0, 0.0}, a), rotate({1.0, 0.0, 0.0}, 34.0 - 68.0 * u));
			fishBack(p, m, 0.3);
		}
	} else if (l.kind == "salmon") {
		for (const Touch &a : l.leaps) {
			if (t < a.t || t > a.t + 1.05) continue;
			const double u = (t - a.t) / 1.05;
			const Vec3 at = Vec3{a.x, levelAt(a.x, a.z) + 1.25 * 4.0 * u * (1.0 - u), a.z} + facing * (1.3 * u);
			fishBack(p, mul(translate(story(at)), craft::sealifeFacing(story(normalize(facing * 1.3 + Vec3{0.0, 5.0 * (1.0 - 2.0 * u), 0.0})))), 0.7);
		}
	}
	if (p.list.empty()) return std::string();
	return "# water's animals, " + l.kind + " (film_pondlife.h)\n" + p.text(l.kind == "swans" ? 0.5 : 0.4);
}
// What they do to the water by t: rings, and a thread of a wake behind what swims.
inline void stirred(stir::Field &f, const Life &l, double t) {
	for (const Touch &q : l.touches)
		if (q.t < t && q.t > t - 30.0) f.rings.push_back(stir::ringOf(q.x, q.z, q.t, q.size, 0.22 * q.size, q.kind != 2));
	if (l.kind == "ducks" || l.kind == "swans" || l.kind == "beaver" || l.kind == "otter") {
		const double pace = l.kind == "swans" ? 0.05 : l.kind == "ducks" ? 0.09 : l.kind == "otter" ? 0.06 : 0.07;   // (each as `text` swims it)
		for (int n = 0; n < std::max(1, l.count); ++n)
			stir::trail(f, [&l, n, pace](double u) { return detail::wanderAt(l, n, u, pace); }, -1e9, 1e30, t, 0.35, 0.05, l.kind == "swans" ? 0.003 : 0.0018, 0.3);
	}
}

}  // namespace pondlife
}  // namespace cartoon
