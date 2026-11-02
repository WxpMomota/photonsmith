#pragma once
// film_seatown.h -- under the sea, round 18: a town on the seabed.
//
//   seatown X Z [radius R] [houses N] [lamps N] [traffic N] [nomarket] [seed K]
//       houses round a plaza R metres out (14 by default): spiral shell towers, sponge domes and coral towers, their doors to the plaza, their round windows lit warm
//       from inside; a ring street of flat stones with jellyfish street lamps on posts, glowing as they pulse; a market in the plaza - striped awnings swaying over
//       counters of shells and pearls; and fish traffic: N fish (40) going round the ring street both ways in two lanes, and in and out along the streets to the
//       plaza, where a glowing signal turns red and green every six seconds and the fish on the streets wait for it
//
// pbrt's x z, on the seabed (film_parse_sea.h); the cpp passes the seabed's height.

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

#include "film_deepsea.h"
#include "film_flow.h"
#include "film_sealife.h"
#include "nature_lib.h"
#include "sdf_mesh.h"

namespace cartoon {
namespace seatown {

struct Town {
	double x = 0.0, z = 0.0, radius = 14.0;
	int houses = 10, lamps = 8, traffic = 40, seed = 1;
	bool market = true;
};

namespace detail {
using deepsea::detail::add;
using sealife::detail::facingOf;
using sealife::detail::story;
inline Mesh ellipsoid(Vec3 c, Vec3 r, int nu = 16, int nv = 10) {
	Mesh m;
	add(m, uvSphere(1.0, nu, nv), mul(translate(c), scale(r.x, r.y, r.z)));
	return m;
}
inline Mesh boxAt(Vec3 c, Vec3 size) {
	Mesh m;
	add(m, box(size.x, size.y, size.z), translate(c));
	return m;
}
// The signal at the plaza: green (true) or red, by the six-second turn.
inline bool green(double t) { return std::fmod(t, 12.0) < 6.0; }
}  // namespace detail

// Where traffic fish i is at t (pbrt's) and its way, about the town's middle `c` (pbrt's, its ground's height there).
inline Vec3 fishAt(const Town &w, int i, double t, const Vec3 &c, Vec3 &fwd) {
	const int s = w.seed * 131 + i;
	const double street = 0.47 * w.radius;
	if (i % 4 != 3) {   // round the ring street, both ways, in two lanes
		const double dir = i % 2 == 0 ? 1.0 : -1.0, lane = i % 2 == 0 ? 1.2 : 1.7, speed = 1.1 + 0.4 * hash01(s, 1);
		const double a = 2.0 * kPi * hash01(s, 2) + dir * speed * t / street;
		const double r = street + (i % 2 == 0 ? -0.5 : 0.5) + 0.2 * std::sin(0.7 * t + s);
		fwd = {-std::sin(a) * dir, 0.0, std::cos(a) * dir};
		return c + Vec3{r * std::cos(a), lane + 0.15 * std::sin(1.3 * t + s), r * std::sin(a)};
	}
	// in and out along a street to the plaza - going while the signal is green, every one of them stopping where it is while it is red (a cartoon's red light, green light)
	const int streets = std::max(3, w.houses / 2);
	const double a = 2.0 * kPi * ((i / 4) % streets + 0.5) / streets, trip = 2.0 * street, speed = 1.3;
	const double into = std::fmod(t, 12.0), moved = 6.0 * std::floor(t / 12.0) + (into < 6.0 ? into : 6.0) - 0.2 * smoothstep(5.6, 6.0, into) * (1.0 - smoothstep(11.8, 12.0, into));
	const double u = std::fmod(moved * speed / trip + 2.0 * hash01(s, 3), 2.0);   // (0..1 in, 1..2 out)
	const bool in = u < 1.0;
	const double along = in ? u : 2.0 - u;   // (0 at the street's far end, 1 at the plaza)
	const double r = lerp(1.25 * street, 0.3 * street, along);
	fwd = Vec3{std::cos(a), 0.0, std::sin(a)} * (in ? -1.0 : 1.0);
	return c + Vec3{r * std::cos(a) + 0.4 * std::sin(a), 1.4 + 0.1 * std::sin(2.0 * t + s), r * std::sin(a) - 0.4 * std::cos(a)};
}

// The town at t as scene text; `bedAt` the seabed's height (pbrt's x z).
inline std::string townText(const Town &w, double t, const std::function<double(double, double)> &bedAt) {
	using namespace detail;
	const Vec3 c{w.x, bedAt(w.x, w.z), w.z};
	std::string out = "# a town under the sea (film_seatown.h)\n", lights;
	Mesh shell, sponge, coral, doors, windows, stones, posts, counters, pearls, shells, lampBells, lampArms;
	std::vector<std::pair<Vec3, Mesh>> awnings;
	const auto ground = [&](double x, double z) { return bedAt(x, z); };
	// the houses round the plaza, their doors to it
	for (int h = 0; h < w.houses; ++h) {
		const int s = w.seed * 71 + h;
		const double a = 2.0 * kPi * (h + 0.3 * hash01(s, 1)) / w.houses, r = (0.72 + 0.12 * hash01(s, 2)) * w.radius;
		const double hx = w.x + r * std::cos(a), hz = w.z + r * std::sin(a), hy = ground(hx, hz);
		const Vec3 toPlaza = normalize(Vec3{w.x - hx, 0.0, w.z - hz});
		const Mat4 place = mul(translate(story({hx, hy, hz})), facingOf(story(toPlaza)));   // (+z of the house: its front, to the plaza)
		{   // (what lights its front: one warm lamp, not its windows - film_flow.h glowAttr)
			char buf[200];
			std::snprintf(buf, sizeof buf, "AttributeBegin\n  LightSource \"point\" \"point3 from\" [ %.4f %.4f %.4f ] \"rgb I\" [ 2.2 1.5 0.6 ]\nAttributeEnd\n", hx + 3.0 * toPlaza.x, hy + 2.0, hz + 3.0 * toPlaza.z);
			lights += buf;
		}
		const int kind = (h + w.seed) % 3;
		const double size = 0.9 + 0.35 * hash01(s, 3);
		std::vector<Vec3> lit;   // (its windows: house space, with the way they face)
		std::vector<Vec3> facing;
		if (kind == 0) {   // a spiral shell tower
			Mesh m = lathe({{2.0 * size, 0.0}, {1.9 * size, 1.2 * size}, {1.45 * size, 3.0 * size}, {0.9 * size, 4.8 * size}, {0.35 * size, 6.4 * size}, {0.0, 7.2 * size}}, 36);   // (a conch's cone)
			m.uvs.clear();
			std::vector<Vec3> ridge;   // (and the ridge of its whorls, spiralling up it)
			std::vector<double> radii;
			const auto coneR = [&](double y) {   // (the cone's radius at a height: its profile's)
				const double ys[6] = {0.0, 1.2, 3.0, 4.8, 6.4, 7.2}, rs[6] = {2.0, 1.9, 1.45, 0.9, 0.35, 0.0};
				const double h = std::clamp(y / size, 0.0, 7.2);
				int k = 0;
				while (k < 4 && h > ys[k + 1]) ++k;
				return size * lerp(rs[k], rs[k + 1], (h - ys[k]) / (ys[k + 1] - ys[k]));
			};
			for (int q = 0; q <= 160; ++q) {
				const double u = q / 160.0, y = 6.9 * size * u, rr = coneR(y) + 0.08 * size;
				ridge.push_back({rr * std::cos(5.0 * 2.0 * kPi * u), y, rr * std::sin(5.0 * 2.0 * kPi * u)});
				radii.push_back((0.28 - 0.22 * u) * size);
			}
			add(m, flow::tube(ridge, radii, 8));
			for (int q = 0; q < 9; ++q) {   // (knobs on its shoulders)
				const double u = 0.2 + 0.07 * q, a = 5.0 * 2.0 * kPi * u + 0.6;
				const double rr = coneR(6.9 * size * u) + 0.15 * size;
				add(m, sdf::tubeMesh({{rr * std::cos(a), 6.9 * size * u, rr * std::sin(a)}, {(rr + 0.35 * size) * std::cos(a), 6.9 * size * u + 0.25 * size, (rr + 0.35 * size) * std::sin(a)}}, 0.12 * size, 0.4, 8));
			}
			add(shell, m, place);
			for (int q = 0; q < 3; ++q) lit.push_back({0.0, size * (1.4 + 1.6 * q), size * (1.93 - 0.47 * q)}), facing.push_back({0.0, 0.0, 1.0});
			lit.push_back({1.5 * size, size * 2.2, 0.4 * size}), facing.push_back({1.0, 0.0, 0.2});
			add(doors, ellipsoid({0.0, 0.55 * size, 1.95 * size}, {0.45 * size, 0.75 * size, 0.12}), place);
		} else if (kind == 1) {   // a sponge dome, holes for windows
			Mesh m = lathe({{2.2 * size, -0.2}, {2.1 * size, 1.0 * size}, {1.6 * size, 2.2 * size}, {0.8 * size, 2.9 * size}, {0.0, 3.1 * size}}, 36);
			m.uvs.clear();
			for (std::size_t i = 0; i < m.verts.size(); ++i) m.verts[i] = m.verts[i] + m.normals[i] * (0.12 * size * (nature::noise2(m.verts[i].x * 1.3 + s, m.verts[i].y * 1.3 + m.verts[i].z, s) - 0.5));
			add(sponge, m, place);
			for (int q = 0; q < 4; ++q) {
				const double b = -0.9 + 0.6 * q;
				lit.push_back({2.0 * size * std::sin(b), 1.5 * size, 2.0 * size * std::cos(b)}), facing.push_back({std::sin(b), 0.1, std::cos(b)});
			}
			add(doors, ellipsoid({0.0, 0.6 * size, 2.15 * size}, {0.5 * size, 0.85 * size, 0.12}), place);
			add(sponge, sdf::tubeMesh({{0.8 * size, 2.3 * size, -0.5 * size}, {0.9 * size, 3.6 * size, -0.5 * size}}, 0.25 * size, 0.9, 10), place);   // (a chimney)
		} else {   // a coral tower: floors, balconies
			Mesh m;
			for (int q = 0; q < 3; ++q) {
				add(m, lathe({{1.3 * size, 0.0}, {1.2 * size, 1.4 * size}, {1.35 * size, 1.6 * size}, {0.0, 1.62 * size}}, 26), translate(0.0, 1.6 * size * q, 0.0));
				add(m, torus(1.45 * size, 0.1 * size, 30, 6), translate(0.0, 1.6 * size * q + 1.5 * size, 0.0));
				lit.push_back({0.0, 1.6 * size * q + 0.8 * size, 1.28 * size}), facing.push_back({0.0, 0.0, 1.0});
				lit.push_back({1.1 * size, 1.6 * size * q + 0.8 * size, -0.6 * size}), facing.push_back({0.85, 0.0, -0.5});
			}
			for (int q = 0; q < 6; ++q) add(m, sdf::tubeMesh({{0.4 * std::cos(q), 4.8 * size, 0.4 * std::sin(q)}, {0.9 * std::cos(q), 6.0 * size + 0.4 * hash01(q, s), 0.9 * std::sin(q)}}, 0.12 * size, 0.6, 8));   // (its crown of coral)
			m.uvs.clear();
			add(coral, m, place);
			lit.erase(lit.begin());
			facing.erase(facing.begin());
			add(doors, ellipsoid({0.0, 0.6 * size, 1.3 * size}, {0.45 * size, 0.8 * size, 0.12}), place);
		}
		for (std::size_t q = 0; q < lit.size(); ++q) {   // the round windows, lit warm from inside
			const Vec3 f = normalize(facing[q]);
			Mesh win = ellipsoid({0.0, 0.0, 0.0}, {0.28 * size, 0.28 * size, 0.06}, 14, 8);
			add(windows, win, mul(place, translate(lit[q]), facingOf(f)));
		}
	}
	// the ring street: flat stones, the jellyfish lamps along it
	const double street = 0.47 * w.radius;
	for (int q = 0; q < static_cast<int>(2.0 * kPi * street / 0.7); ++q) {
		const int s = w.seed * 17 + q;
		const double a = q * 0.7 / street + 0.15 * hash01(s, 1), r = street + 0.9 * (hash01(s, 2) - 0.5);
		const double sx = w.x + r * std::cos(a), sz = w.z + r * std::sin(a);
		add(stones, ellipsoid(story({sx, ground(sx, sz) + 0.02, sz}), {0.3 + 0.1 * hash01(s, 3), 0.04, 0.25 + 0.1 * hash01(s, 4)}, 10, 4));
	}
	for (int q = 0; q < w.lamps; ++q) {
		const double a = 2.0 * kPi * (q + 0.5) / w.lamps, r = street + 1.0;
		const double lx = w.x + r * std::cos(a), lz = w.z + r * std::sin(a), ly = ground(lx, lz);
		const Vec3 foot = story({lx, ly, lz}), top = story({lx, ly + 3.2, lz}), hook = story({lx - 0.5 * std::cos(a), ly + 3.4, lz - 0.5 * std::sin(a)});
		add(posts, sdf::tubeMesh({foot, top, hook, hook + Vec3{0.0, -0.25, 0.0}}, 0.05, 0.8, 8));
		{
			char buf[200];
			std::snprintf(buf, sizeof buf, "AttributeBegin\n  LightSource \"point\" \"point3 from\" [ %.4f %.4f %.4f ] \"rgb I\" [ 0.5 1.3 1.8 ]\nAttributeEnd\n", lx - 0.5 * std::cos(a), ly + 2.8, lz - 0.5 * std::sin(a));
			lights += buf;
		}
		const double ph = std::fmod(t / 1.7 + 0.37 * q, 1.0), squeeze = ph < 0.3 ? std::sin(kPi * ph / 0.3) : 0.0;
		const Vec3 bellAt = hook + Vec3{0.0, -0.45, 0.0};
		Mesh bell = lathe({{0.0, 0.3}, {0.22, 0.26}, {0.36, 0.12}, {0.4, 0.0}, {0.0, 0.06}}, 18);
		bell.uvs.clear();
		add(lampBells, bell, mul(translate(bellAt), scale(1.0 - 0.15 * squeeze, 1.0 + 0.1 * squeeze, 1.0 - 0.15 * squeeze)));
		for (int k = 0; k < 6; ++k) {
			const double b = 2.0 * kPi * k / 6;
			std::vector<Vec3> line;
			for (int j = 0; j <= 6; ++j) line.push_back(bellAt + Vec3{0.3 * std::cos(b) * (1.0 - j / 9.0) + 0.06 * std::sin(1.4 * t - j + k), -0.15 * j, 0.3 * std::sin(b) * (1.0 - j / 9.0)});
			add(lampArms, sdf::tubeMesh(line, 0.012, 0.4, 4));
		}
	}
	// the market in the plaza
	if (w.market)
		for (int q = 0; q < 4; ++q) {
			const int s = w.seed * 29 + q;
			const double a = 2.0 * kPi * (q + 0.125) / 4.0, r = 0.2 * w.radius;
			const double mx = w.x + r * std::cos(a), mz = w.z + r * std::sin(a), my = ground(mx, mz);
			const Mat4 place = mul(translate(story({mx, my, mz})), facingOf(story(Vec3{std::cos(a), 0.0, std::sin(a)})));
			add(counters, boxAt({0.0, 0.5, 0.0}, {1.8, 1.0, 0.7}), place);
			for (const double sx : {-0.85, 0.85})
				for (const double sz : {-0.3, 0.3}) add(posts, sdf::tubeMesh({{sx, 0.0, sz}, {sx, 2.3, sz}}, 0.04, 1.0, 6), place);
			for (int k = 0; k < 14; ++k)   // (its goods: pearls and shells on the counter)
				add(k % 2 == 0 ? pearls : shells, ellipsoid({-0.75 + 0.11 * k, 1.05, 0.15 * std::sin(k * 1.7)}, k % 2 == 0 ? Vec3{0.05, 0.05, 0.05} : Vec3{0.08, 0.03, 0.06}, 8, 6), place);
			std::vector<Vec3> left, right;   // (its awning: a striped cloth on the posts, its front edge rippling in the current)
			for (int j = 0; j <= 8; ++j) {
				const double u = j / 8.0, ripple = 0.06 * std::sin(2.0 * t + 6.0 * u + s);
				left.push_back(transformPoint(place, {-0.95, 2.4 - 0.3 * u + ripple, -0.4 + 1.1 * u}));
				right.push_back(transformPoint(place, {0.95, 2.4 - 0.3 * u + ripple, -0.4 + 1.1 * u}));
			}
			const Vec3 hue = q % 2 == 0 ? Vec3{0.85, 0.2, 0.2} : Vec3{0.15, 0.45, 0.8};
			awnings.push_back({hue, flow::strip(left, right)});
		}
	{   // the signal, in the middle of the plaza
		const double sy = ground(w.x, w.z);
		add(posts, sdf::tubeMesh({story({w.x, sy, w.z}), story({w.x, sy + 2.2, w.z})}, 0.07, 1.0, 8));
		const Vec3 L = green(t) ? Vec3{0.3, 3.0, 0.6} : Vec3{3.2, 0.25, 0.2};
		out += flow::glowAttr(emitMesh(ellipsoid(story({w.x, sy + 2.45, w.z}), {0.22, 0.22, 0.22}, 14, 10), identity()), L);
	}
	out += attr(coated({0.92, 0.72, 0.62}, 0.25), emitMesh(shell, identity()));
	out += attr(diffuseMaterial({0.85, 0.62, 0.25}), emitMesh(sponge, identity()));
	out += attr(coated({0.88, 0.4, 0.45}, 0.4), emitMesh(coral, identity()));
	out += attr(diffuseMaterial({0.08, 0.06, 0.05}), emitMesh(doors, identity()));
	const Vec3 warm{3.2, 2.1, 0.8};
	out += flow::glowAttr(emitMesh(windows, identity()), warm);
	out += attr(diffuseMaterial({0.62, 0.6, 0.55}), emitMesh(stones, identity()));
	out += attr(coated({0.2, 0.4, 0.3}, 0.4), emitMesh(posts, identity()));
	out += attr(coated({0.55, 0.38, 0.22}, 0.5), emitMesh(counters, identity()));
	out += attr(coated({0.95, 0.93, 0.9}, 0.05), emitMesh(pearls, identity()));
	out += attr(coated({0.95, 0.7, 0.55}, 0.3), emitMesh(shells, identity()));
	for (const auto &[hue, cloth] : awnings) out += attr(diffuseMaterial(hue), emitMesh(cloth, identity()));
	const double pulse = 0.75 + 0.25 * std::sin(2.0 * kPi * t / 1.7);
	const Vec3 glow{0.6 * pulse, 1.6 * pulse, 2.2 * pulse};
	out += flow::glowAttr(emitMesh(lampBells, identity()), glow);
	out += attr(translucentMaterial({0.3, 0.5, 0.6}, {0.4, 0.7, 0.8}), emitMesh(lampArms, identity()));
	// the traffic
	Mesh fish[6];
	for (int i = 0; i < w.traffic; ++i) {
		Vec3 fwd;
		const Vec3 p = fishAt(w, i, t, c, fwd);
		const int k = static_cast<int>(std::floor(t * 6.0 + i)) % sealife::detail::kPoses;
		add(fish[i % 6], sealife::detail::fish(k, 0.22, i % 6 < 2 ? 2.0 : 1.2), mul(translate(story(p)), facingOf(story(fwd))));
	}
	for (int q = 0; q < 6; ++q) out += attr(coated(sealife::detail::reefColour(q), 0.3), emitMesh(fish[q], identity()));
	return out + lights;
}

}  // namespace seatown
}  // namespace cartoon
