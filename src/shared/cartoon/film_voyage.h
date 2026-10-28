#pragma once
// film_voyage.h -- travelling in space for a film (film_file.h `ship`; round 10): the craft, the people in them and the camera's journeys (the bodies they travel among
// are film_cosmos.h's).
//
//   ship       a shuttle, a fighter, a freighter, a station or a wheel, flying nose first along its way, its engines glowing, its running lights blinking
//   astronaut  someone floating in a spacesuit, tumbling slowly, tethered to a point or a ship, loose things drifting round them
//
// Coordinates are pbrt's, as a film file's; meshes are made in story space (x mirrored), as cartoon_lib.h's.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "cartoon_lib.h"
#include "film_cosmos.h"

namespace cartoon {
namespace cosmos {

// ---- ships and stations ----
// A craft in space, `size` long (or across): a `shuttle` (a white body with delta wings and a tail fin), a `fighter` (slim, four wings in an X, four engines), a `freighter`
// (a long spine hung with coloured containers, a bridge at its bow, one great engine), a `station` (modules round a hub, wings of solar panels on a truss, turning slowly)
// or a `wheel` (a ring station on spokes, spinning `spin` turns a minute, its windows lit). It is where `at` says at nature's time 0 and moves `move` metres a second, its
// nose along its way (or `heading` degrees about the up axis and `pitch` up from level when it is still); its engines glow while it moves (`engines on|off`), and it
// carries running lights - red to port, green to starboard and a white strobe.
struct Ship {
	std::string name, kind = "shuttle";
	Vec3 at{0.0, 0.0, -2000.0}, move{0.0, 0.0, 0.0};
	double size = 60.0, heading = 0.0, pitch = 0.0, spin = 1.0;
	int engines = -1;   // -1: while it moves
	bool lights = true;
	Vec3 colour{0.85, 0.86, 0.88};
	int seed = 1;
};
inline const std::vector<std::string> &shipKinds() {
	static const std::vector<std::string> k = {"shuttle", "fighter", "freighter", "station", "wheel"};
	return k;
}
// One part of a craft, in its own frame (a unit long, nose along +x, up +y; story space): its shape, where, its material and the light it gives (none: 0).
struct ShipPart {
	Mesh mesh;
	Mat4 place = identity();
	std::string material;
	Vec3 light{0.0, 0.0, 0.0};
	bool lit = false;
};
// A flat plate - a wing, a fin, a solar panel - the polygon `pts` (u, v) in the plane of `uAxis` and `vAxis`, `thick` thick, its faces flat.
inline Mesh plate(const std::vector<std::pair<double, double>> &pts, double thick, Vec3 uAxis, Vec3 vAxis) {
	const Vec3 n = normalize(cross(uAxis, vAxis));
	Mesh m;
	const int k = static_cast<int>(pts.size());
	std::vector<Vec3> top, bottom;
	for (const auto &p : pts) top.push_back(uAxis * p.first + vAxis * p.second + n * (0.5 * thick)), bottom.push_back(uAxis * p.first + vAxis * p.second - n * (0.5 * thick));
	const auto face = [&m](const std::vector<Vec3> &ring, Vec3 normal, bool flip) {
		const int base = static_cast<int>(m.verts.size());
		for (const Vec3 &v : ring) m.verts.push_back(v), m.normals.push_back(normal);
		for (int i = 1; i + 1 < static_cast<int>(ring.size()); ++i) m.tris.push_back(flip ? std::array<int, 3>{base, base + i + 1, base + i} : std::array<int, 3>{base, base + i, base + i + 1});
	};
	face(top, n, false);
	face(bottom, n * -1.0, true);
	for (int i = 0; i < k; ++i) {   // its edges
		const int j = (i + 1) % k;
		const Vec3 e = top[static_cast<std::size_t>(j)] - top[static_cast<std::size_t>(i)];
		const Vec3 side = normalize(cross(e, n));
		face({bottom[static_cast<std::size_t>(i)], bottom[static_cast<std::size_t>(j)], top[static_cast<std::size_t>(j)], top[static_cast<std::size_t>(i)]}, side, dot(cross(bottom[static_cast<std::size_t>(j)] - bottom[static_cast<std::size_t>(i)], top[static_cast<std::size_t>(i)] - bottom[static_cast<std::size_t>(i)]), side) < 0.0);
	}
	return m;
}
// A capsule along +x from x0 to x1 (radius r), centred on (y, z).
inline ShipPart pod(double x0, double x1, double r, double y, double z, const std::string &material) {
	ShipPart p;
	p.mesh = capsule(r, std::max(1e-6, x1 - x0 - 2.0 * r), 16, 4);
	p.place = mul(translate(x0 + r, y, z), rotate({0.0, 0.0, 1.0}, -90.0));
	p.material = material;
	return p;
}
// The parts of a craft (a unit long) at nature's time `nt`: what spins spins.
inline std::vector<ShipPart> shipParts(const Ship &s, double nt, bool enginesOn) {
	std::vector<ShipPart> out;
	const std::string hull = diffuseMaterial(s.colour), dark = diffuseMaterial({0.12, 0.12, 0.13}), glass = metalMaterial({0.05, 0.06, 0.08}, 0.05),
					  metal = metalMaterial({0.6, 0.6, 0.62}, 0.25), panels = metalMaterial({0.08, 0.12, 0.3}, 0.1);
	const auto add = [&out](Mesh m, const Mat4 &place, const std::string &material) {
		ShipPart p;
		p.mesh = std::move(m), p.place = place, p.material = material;
		out.push_back(p);
	};
	const auto glow = [&out](Vec3 at, double r, Vec3 light) {
		ShipPart p;
		p.mesh = uvSphere(r, 12, 6), p.place = translate(at.x, at.y, at.z), p.material = diffuseMaterial({0.0, 0.0, 0.0}), p.light = light, p.lit = true;
		out.push_back(p);
	};
	const double flicker = 0.85 + 0.15 * std::sin(nt * 37.0 + s.seed) * std::sin(nt * 23.0);
	const Vec3 flame = Vec3{0.55, 0.75, 1.0} * (6.0 * flicker);
	std::vector<Vec3> engines;
	double wing = 0.4;   // (half its span: where the running lights go)
	if (s.kind == "shuttle") {
		out.push_back(pod(-0.5, 0.5, 0.09, 0.0, 0.0, hull));
		add(uvSphere(0.06, 16, 8), mul(translate(0.32, 0.055, 0.0), scale(1.4, 0.6, 0.9)), glass);
		add(plate({{0.1, 0.0}, {-0.42, 0.0}, {-0.36, 0.42}, {-0.24, 0.42}}, 0.02, {1.0, 0.0, 0.0}, {0.0, 0.0, 1.0}), translate(0.0, -0.03, 0.0), hull);
		add(plate({{0.1, 0.0}, {-0.42, 0.0}, {-0.36, -0.42}, {-0.24, -0.42}}, 0.02, {1.0, 0.0, 0.0}, {0.0, 0.0, 1.0}), translate(0.0, -0.03, 0.0), hull);
		add(plate({{-0.2, 0.0}, {-0.48, 0.0}, {-0.46, 0.22}, {-0.38, 0.22}}, 0.015, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}), translate(0.0, 0.05, 0.0), hull);
		for (const double z : {-0.05, 0.05}) out.push_back(pod(-0.52, -0.38, 0.035, -0.01, z, dark)), engines.push_back({-0.53, -0.01, z});
	} else if (s.kind == "fighter") {
		out.push_back(pod(-0.5, 0.5, 0.05, 0.0, 0.0, hull));
		add(uvSphere(0.04, 16, 8), mul(translate(0.2, 0.035, 0.0), scale(1.8, 0.6, 0.8)), glass);
		for (const double a : {20.0, -20.0, 160.0, -160.0}) {
			add(plate({{-0.1, 0.03}, {-0.42, 0.03}, {-0.4, 0.45}, {-0.3, 0.45}}, 0.012, {1.0, 0.0, 0.0}, {0.0, 0.0, 1.0}), rotate({1.0, 0.0, 0.0}, a), hull);
			const double r = a * kPi / 180.0;
			out.push_back(pod(-0.5, -0.25, 0.03, 0.09 * std::sin(r), 0.09 * std::cos(r), dark));
			engines.push_back({-0.51, 0.09 * std::sin(r), 0.09 * std::cos(r)});
		}
		wing = 0.45;
	} else if (s.kind == "freighter") {
		add(box(0.9, 0.05, 0.05), translate(-0.02, 0.0, 0.0), metal);
		add(roundedBox(0.1, 0.09, 0.12, 0.02), translate(0.45, 0.04, 0.0), hull);
		add(box(0.03, 0.02, 0.1), translate(0.5, 0.08, 0.0), glass);
		for (int i = 0; i < 8; ++i)
			for (int side = -1; side <= 1; side += 2) {
				const Vec3 c = lerp(Vec3{0.7, 0.25, 0.15}, Vec3{0.2, 0.4, 0.65}, hash01(s.seed * 41 + i * 2 + side, 1)) * (0.7 + 0.4 * hash01(s.seed * 41 + i, 2));
				add(box(0.085, 0.07, 0.06), translate(0.32 - 0.1 * i, 0.0, 0.058 * side), diffuseMaterial(c));
			}
		ShipPart bell = pod(-0.56, -0.44, 0.06, 0.0, 0.0, dark);
		out.push_back(bell);
		engines.push_back({-0.57, 0.0, 0.0});
		wing = 0.12;
	} else if (s.kind == "station") {
		const double turn = std::fmod(nt * 6.0 * s.spin, 360.0);   // (a slow turn about its long axis, the panels following the sun)
		out.push_back(pod(-0.25, 0.25, 0.06, 0.0, 0.0, hull));
		for (const double x : {-0.12, 0.12}) add(capsule(0.045, 0.16, 16, 4), mul(rotate({1.0, 0.0, 0.0}, turn), translate(x, -0.08, 0.0)), hull);
		add(box(0.03, 0.03, 1.0), rotate({1.0, 0.0, 0.0}, turn), metal);
		for (const double z : {-0.42, -0.27, 0.27, 0.42})
			for (const double x : {-0.09, 0.09}) add(box(0.16, 0.006, 0.13), mul(rotate({1.0, 0.0, 0.0}, turn), translate(x, 0.0, z)), panels);
		wing = 0.5;
	} else {   // wheel
		const double turn = std::fmod(nt * 6.0 * s.spin, 360.0);   // (turns a minute)
		const Mat4 spinning = rotate({1.0, 0.0, 0.0}, turn);
		add(torus(0.45, 0.04, 64, 12), mul(spinning, rotate({0.0, 0.0, 1.0}, 90.0)), hull);
		ShipPart windows;
		windows.mesh = torus(0.45, 0.041, 64, 4), windows.place = mul(spinning, rotate({0.0, 0.0, 1.0}, 90.0), scale(1.0, 0.25, 1.0)), windows.material = diffuseMaterial({0.0, 0.0, 0.0});
		windows.light = Vec3{1.0, 0.8, 0.5} * 1.5, windows.lit = true;
		out.push_back(windows);
		for (int k = 0; k < 4; ++k) add(box(0.02, 0.9, 0.02), mul(spinning, rotate({1.0, 0.0, 0.0}, 45.0 * k)), metal);
		out.push_back(pod(-0.12, 0.12, 0.06, 0.0, 0.0, metal));
		wing = 0.5;
	}
	if (enginesOn)
		for (const Vec3 &e : engines) glow(e, s.kind == "freighter" ? 0.032 : 0.028, flame);
	if (s.lights) {
		glow({-0.1, 0.0, -wing}, 0.008, Vec3{1.0, 0.05, 0.03} * 5.0);
		glow({-0.1, 0.0, wing}, 0.008, Vec3{0.05, 1.0, 0.1} * 5.0);
		if (std::fmod(nt + 0.37 * s.seed, 1.3) < 0.1) glow({-0.3, 0.07, 0.0}, 0.01, Vec3{1.0, 1.0, 1.0} * 20.0);
	}
	return out;
}
// Where a craft is at nature's time `nt` (pbrt's) and its frame there: the story-space transform of its unit-long parts.
inline Mat4 shipPlacement(const Ship &s, double nt) {
	const Vec3 at = s.at + s.move * nt;
	double yaw = s.heading * kPi / 180.0, pitch = s.pitch * kPi / 180.0;
	if (length(s.move) > 1e-9) {
		const Vec3 f = normalize(Vec3{-s.move.x, s.move.y, s.move.z});   // (story space)
		yaw = std::atan2(-f.z, f.x), pitch = std::asin(std::clamp(f.y, -1.0, 1.0));
	}
	return mul(translate(-at.x, at.y, at.z), rotate({0.0, 1.0, 0.0}, yaw * 180.0 / kPi), rotate({0.0, 0.0, 1.0}, pitch * 180.0 / kPi), scale(s.size, s.size, s.size));
}
// A craft at nature's time `nt` as scene text.
inline std::string shipText(const Ship &s, double nt) {
	const bool enginesOn = s.engines == 1 || (s.engines < 0 && length(s.move) > 1e-9);
	const Mat4 M = shipPlacement(s, nt);
	std::string out = "# a " + s.kind + " (film_cosmos.h): " + s.name + "\n";
	for (const ShipPart &p : shipParts(s, nt, enginesOn)) out += attr(p.material, emitMesh(p.mesh, mul(M, p.place)), p.lit ? &p.light : nullptr);
	return out;
}

// ---- astronauts ----
// Someone floating in a spacesuit, `size` tall (1.8 m): a white suit (or `suit R G B`), a backpack, a helmet with a gold visor and two lamps, limbs drifting in the slow
// sway of zero-g; where `at` says at nature's time 0, drifting `move` metres a second and tumbling `tumble` degrees a second about a tilted axis; held by a `tether` - a
// lazy line from its waist to a point (or to a `ship`, by name, wherever it is now); with `floaters N` loose things drifting round it (tools, bolts, wobbling globes of
// water).
struct Astronaut {
	std::string name, tetherShip;
	Vec3 at{0.0, 0.0, -6.0}, move{0.0, 0.0, 0.0}, suit{0.92, 0.92, 0.9};
	Vec3 tether{0.0, 0.0, 0.0};
	bool tethered = false, lamps = true;
	double size = 1.8, tumble = 4.0;
	int floaters = 0, seed = 1;
};
// The story-space transform of its parts (a 1.8 m figure, its waist at its own origin, up +y, facing +z) at nature's time `nt`.
inline Mat4 astronautPlacement(const Astronaut &a, double nt) {
	const Vec3 at = a.at + a.move * nt;
	const Vec3 axis = normalize(Vec3{0.3 + hash01(a.seed, 1), 0.4, 0.2 + hash01(a.seed, 2)});
	return mul(translate(-at.x, at.y, at.z), rotate(axis, std::fmod(360.0 * hash01(a.seed, 3) + a.tumble * nt, 360.0)), scale(a.size / 1.8));
}
// Where its waist is (pbrt's) at nature's time `nt`: where a tether is clipped on.
inline Vec3 astronautWaist(const Astronaut &a, double nt) {
	const Vec3 p = transformPoint(astronautPlacement(a, nt), {0.0, 0.0, -0.18});
	return {-p.x, p.y, p.z};
}
// An astronaut at nature's time `nt` as scene text: its suit, its limbs swaying, its lamps, its tether to `tetherTo` (pbrt's) when it has one, and its floaters.
inline std::string astronautText(const Astronaut &a, double nt, const Vec3 *tetherTo = nullptr) {
	const Mat4 M = astronautPlacement(a, nt);
	const std::string suit = diffuseMaterial(a.suit), pack = diffuseMaterial(a.suit * 0.8), dark = diffuseMaterial({0.15, 0.15, 0.16}), visor = metalMaterial({1.0, 0.78, 0.34}, 0.03);
	std::string out = "# an astronaut (film_cosmos.h): " + a.name + "\n";
	const auto part = [&](const Mesh &m, const Mat4 &place, const std::string &material) { out += attr(material, emitMesh(m, mul(M, place))); };
	const auto sway = [&](int k, double amount) { return amount * std::sin(0.55 * nt + 1.7 * k + a.seed); };
	part(roundedBox(0.46, 0.6, 0.3, 0.1), translate(0.0, 0.3, 0.0), suit);   // its torso (the waist at 0)
	part(roundedBox(0.42, 0.52, 0.18, 0.06), translate(0.0, 0.33, -0.24), pack);
	part(box(0.18, 0.1, 0.03), translate(0.0, 0.4, 0.16), dark);   // (the panel on its chest)
	part(uvSphere(0.17, 24, 12), translate(0.0, 0.78, 0.0), suit);
	part(uvSphere(0.15, 24, 12), mul(translate(0.0, 0.78, 0.06), scale(0.95, 0.82, 0.95)), visor);
	// its limbs: tubes through shoulder, elbow, hand (hip, knee, foot), drifting
	for (int side = -1; side <= 1; side += 2) {
		const double sx = side;
		const Vec3 shoulder{0.28 * sx, 0.5, 0.0}, elbow{(0.42 + sway(side, 0.05)) * sx, 0.28 + sway(side + 3, 0.06), 0.12}, hand{(0.46 + sway(side + 5, 0.06)) * sx, 0.1 + sway(side + 7, 0.1), 0.3};
		part(sdf::tubeMesh({shoulder, elbow, hand}, 0.075, 0.9, 10), identity(), suit);
		part(uvSphere(0.065, 12, 6), translate(hand.x, hand.y, hand.z), dark);
		const Vec3 hip{0.12 * sx, -0.02, 0.0}, knee{(0.16 + sway(side + 9, 0.03)) * sx, -0.38, 0.18 + sway(side + 11, 0.06)}, foot{0.15 * sx, -0.78 + sway(side + 13, 0.05), 0.02};
		part(sdf::tubeMesh({hip, knee, foot}, 0.09, 0.9, 10), identity(), suit);
		part(roundedBox(0.13, 0.12, 0.26, 0.04), translate(foot.x, foot.y - 0.04, foot.z + 0.05), dark);
	}
	if (a.lamps) {
		const Vec3 L{1.0, 0.9, 0.75};
		for (int side = -1; side <= 1; side += 2) {
			const Vec3 L2 = L * 8.0;
			out += attr(diffuseMaterial({0.0, 0.0, 0.0}), emitMesh(uvSphere(0.025, 10, 5), mul(M, translate(0.15 * side, 0.86, 0.1))), &L2);
		}
	}
	if (tetherTo) {   // a lazy line from its waist, sagging and swaying a little
		const Vec3 w = astronautWaist(a, nt), t = *tetherTo, d = t - w;
		const Vec3 side = normalize(cross(d, Vec3{0.0, 1.0, 0.0}) + Vec3{1e-6, 0.0, 0.0}), bow = normalize(cross(side, d) + Vec3{0.0, 1e-6, 0.0});
		std::vector<Vec3> pts;
		for (int i = 0; i <= 40; ++i) {
			const double u = i / 40.0, belly = std::sin(kPi * u) * length(d) * (0.12 + 0.04 * std::sin(0.4 * nt + a.seed));
			const Vec3 p = w + d * u + bow * belly + side * (std::sin(2.0 * kPi * u + 0.7 * nt) * std::sin(kPi * u) * 0.05 * length(d));
			pts.push_back({-p.x, p.y, p.z});
		}
		out += "# its tether\n" + attr(diffuseMaterial({0.85, 0.75, 0.3}), emitMesh(sdf::tubeMesh(pts, 0.012 * a.size / 1.8, 1.0, 6), identity()));
	}
	// loose things drifting round it
	for (int k = 0; k < a.floaters; ++k) {
		const int sd = a.seed * 401 + k;
		const Vec3 home = a.at + a.move * nt + Vec3{hash01(sd, 1) - 0.5, hash01(sd, 2) - 0.5, hash01(sd, 3) - 0.5} * (3.0 * a.size) +
						  Vec3{hash01(sd, 4) - 0.5, hash01(sd, 5) - 0.5, hash01(sd, 6) - 0.5} * (0.1 * a.size * nt);
		const Mat4 place = mul(translate(-home.x, home.y, home.z), rotate(normalize(Vec3{hash01(sd, 7), hash01(sd, 8), 0.3}), std::fmod(nt * (20.0 + 40.0 * hash01(sd, 9)), 360.0)), scale(a.size / 1.8));
		const int what = k % 3;
		if (what == 0) {   // a wrench
			out += attr(metalMaterial({0.7, 0.7, 0.72}, 0.2), emitMesh(box(0.22, 0.03, 0.012), place) + emitMesh(torus(0.025, 0.01, 12, 6), mul(place, translate(0.12, 0.0, 0.0), rotate({1.0, 0.0, 0.0}, 90.0))));
		} else if (what == 1) {   // a bolt
			out += attr(metalMaterial({0.6, 0.55, 0.45}, 0.15), emitMesh(capsule(0.012, 0.06, 8, 2), place));
		} else {   // a globe of water, wobbling
			const double wob = 0.12 * std::sin(nt * 3.0 + k);
			out += attr(glassMaterial(1.33), emitMesh(uvSphere(0.04, 16, 8), mul(place, scale(1.0 + wob, 1.0 - wob, 1.0 + 0.5 * wob))));
		}
	}
	return out;
}

}  // namespace cosmos
}  // namespace cartoon
