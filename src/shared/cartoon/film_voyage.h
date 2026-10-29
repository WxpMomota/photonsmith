#pragma once
// film_voyage.h -- travelling in space for a film (film_file.h `ship`; round 10): the craft, the people in them and the camera's journeys (the bodies they travel among
// are film_cosmos.h's).
//
//   ship       a shuttle, a fighter, a freighter, a station or a wheel, flying nose first along its way, its engines glowing, its running lights blinking
//   astronaut  someone floating in a spacesuit, tumbling slowly, tethered to a point or a ship, loose things drifting round them
//   spacecam   a shot's camera on a journey: an orbit, a slingshot flyby, a powers-of-ten zoom, a jump to hyperspace
//   descent    a craft coming down: re-entry's fire, its parachutes, its retro-rockets and the dust they raise as it lands
//   (space)    light in space: planetshine, the lens's flare and glow; sound in space: the hum, engines, breathing, flares, blasts, the rush of hyperspace
//
// Coordinates are pbrt's, as a film file's; meshes are made in story space (x mirrored), as cartoon_lib.h's.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

#include "../camera_keyframes.h"
#include "cartoon_lib.h"
#include "film_cosmos.h"
#include "film_planets.h"
#include "film_soundscape.h"

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

// ---- the camera's journeys ----
// A shot's camera on a journey in space (`spacecam`), about the point `at` (pbrt's), over the whole shot: an `orbit` round it `radius` away, from bearing `from` through
// `degrees`, `elevation` up; a slingshot `flyby` - in from far away, swinging past it `radius` away fastest where it is nearest and out again, looking ahead with it beside
// the way; a powers-of-ten `zoom` - from `from` metres away to `to` metres, out along `bearing` and `elevation`, the distance changing by the same factor every second;
// or a jump to `hyperspace` - at `at` looking along `bearing` and `elevation`, the stars stretching into streaks, a white flash halfway, out at `to` with the streaks
// shrinking back to stars.
struct SpaceCam {
	enum Kind { None, Orbit, Flyby, Zoom, Hyperspace } kind = None;
	Vec3 at, to;
	double radius = 10000.0, degrees = 90.0, from = 0.0, elevation = 10.0, near0 = 1.0e6, near1 = 100.0, fov = 0.0;
};
// The camera of a journey `u` of the way through its shot (0 .. 1).
inline camera_keyframes::Key spaceCamKey(const SpaceCam &c, double u) {
	u = std::clamp(u, 0.0, 1.0);
	Vec3 eye = c.at + Vec3{0.0, 0.0, 1.0} * c.radius, target = c.at;
	switch (c.kind) {
		case SpaceCam::Orbit: eye = c.at + space::direction(c.from + c.degrees * u, c.elevation) * c.radius; break;
		case SpaceCam::Flyby: {   // a hyperbola about `at`, nearest (`radius`) at the bearing `from`; its parameter runs fastest in the middle (a slingshot)
			const Vec3 n = space::direction(c.from, c.elevation), side = normalize(cross(Vec3{0.0, 1.0, 0.0}, n) + Vec3{1e-9, 0.0, 0.0});
			const auto at = [&](double v) {
				const double w = 3.0 * std::asinh(30.0 * (2.0 * v - 1.0)) / std::asinh(30.0);
				return c.at + n * (c.radius * std::cosh(w)) + side * (c.radius * std::sinh(w));
			};
			eye = at(u);
			const Vec3 ahead = normalize(at(std::min(1.0, u + 0.002)) - at(std::max(0.0, u - 0.002)) + Vec3{1e-12, 0.0, 0.0}), toIt = normalize(c.at - eye);
			target = eye + normalize(ahead * 1.3 + toIt) * c.radius;   // (looking ahead, the body off to the side as it swings past)
			break;
		}
		case SpaceCam::Zoom: eye = c.at + space::direction(c.from, c.elevation) * (c.near0 * std::pow(c.near1 / c.near0, smoothstep(0.0, 1.0, u))); break;
		case SpaceCam::Hyperspace: {
			const Vec3 forward = space::direction(c.from, c.elevation);
			const double lurch = std::pow(smoothstep(0.25, 0.5, u), 2.0) * 50.0;   // (a lunge forward as it goes)
			eye = (u < 0.5 ? c.at : c.to) + forward * (u < 0.5 ? lurch : 50.0 * (1.0 - smoothstep(0.5, 0.65, u)));
			target = eye + forward * 1000.0;
			break;
		}
		default: break;
	}
	camera_keyframes::Key key;
	key.pos[0] = eye.x, key.pos[1] = eye.y, key.pos[2] = eye.z;
	key.target[0] = target.x, key.target[1] = target.y, key.target[2] = target.z;
	key.fov = c.fov;
	return key;
}
// A jump to hyperspace `u` of the way through its shot, as scene text: streaks of starlight round the camera, stretching as it goes and shrinking as it comes out, and the
// flash halfway.
inline std::string hyperspaceText(const SpaceCam &c, double u) {
	const camera_keyframes::Key key = spaceCamKey(c, u);
	const Vec3 eye{key.pos[0], key.pos[1], key.pos[2]}, forward = space::direction(c.from, c.elevation);
	const Vec3 side = normalize(cross(forward, Vec3{0.0, 1.0, 0.0}) + Vec3{1e-9, 0.0, 0.0}), up = cross(side, forward);
	const double stretch = u < 0.5 ? smoothstep(0.2, 0.48, u) : 1.0 - smoothstep(0.52, 0.75, u);
	std::string out = "# a jump to hyperspace (film_voyage.h)\n";
	if (stretch > 0.01) {
		Mesh all;
		for (int k = 0; k < 260; ++k) {
			const double a = 2.0 * kPi * hash01(k, 1), off = 15.0 + 90.0 * hash01(k, 2), ahead = 60.0 + 600.0 * hash01(k, 3);
			const Vec3 head = eye + forward * ahead + (side * std::cos(a) + up * std::sin(a)) * off, tail = head - forward * (stretch * (100.0 + 500.0 * hash01(k, 4)) + 0.5);
			const Mesh m = sdf::tubeMesh({{-tail.x, tail.y, tail.z}, {-head.x, head.y, head.z}}, 0.25 + 0.2 * hash01(k, 5), 0.3, 5);
			const int base = static_cast<int>(all.verts.size());
			all.verts.insert(all.verts.end(), m.verts.begin(), m.verts.end());
			all.normals.insert(all.normals.end(), m.normals.begin(), m.normals.end());
			for (const auto &tr : m.tris) all.tris.push_back({tr[0] + base, tr[1] + base, tr[2] + base});
		}
		const Vec3 L = Vec3{0.75, 0.85, 1.0} * (2.0 + 4.0 * stretch);
		out += attr(diffuseMaterial({0.0, 0.0, 0.0}), emitMesh(all, identity()), &L);
	}
	const double flash = std::exp(-std::pow((u - 0.5) / 0.025, 2.0));
	if (flash > 0.01) out += emissiveSphere({-(eye.x + forward.x * 30.0), eye.y + forward.y * 30.0, eye.z + forward.z * 30.0}, 25.0, Vec3{1.0, 1.0, 1.0} * (30.0 * flash));
	return out;
}
inline const std::vector<std::string> &spaceCamKinds() {
	static const std::vector<std::string> k = {"orbit", "flyby", "zoom", "hyperspace"};
	return k;
}
// A `spacecam` line's words into `c`: "" or what is wrong with it.
inline std::string spaceCamLine(const std::vector<std::string> &t, SpaceCam &c) {
	const std::string usage =
		"`spacecam orbit X Y Z radius R [degrees D] [from BEARING] [elevation E] [fov F]`, `spacecam flyby X Y Z radius R [from BEARING] [elevation E] [fov F]`, "
		"`spacecam zoom X Y Z from D0 to D1 [bearing B] [elevation E] [fov F]` or `spacecam hyperspace X Y Z to X Y Z [bearing B] [elevation E] [fov F]`";
	const auto number = [](const std::string &s, double &v) {
		char *end = nullptr;
		v = std::strtod(s.c_str(), &end);
		return !s.empty() && end && *end == '\0';
	};
	if (t.size() < 5 || std::find(spaceCamKinds().begin(), spaceCamKinds().end(), t[1]) == spaceCamKinds().end()) return usage;
	SpaceCam out;
	out.kind = t[1] == "orbit" ? SpaceCam::Orbit : t[1] == "flyby" ? SpaceCam::Flyby : t[1] == "zoom" ? SpaceCam::Zoom : SpaceCam::Hyperspace;
	if (!number(t[2], out.at.x) || !number(t[3], out.at.y) || !number(t[4], out.at.z)) return "where it is about: " + usage;
	bool radius = false, from = false, to = false;
	for (std::size_t i = 5; i < t.size(); i += 2) {
		double v = 0.0;
		const std::string &w = t[i];
		if (out.kind == SpaceCam::Hyperspace && w == "to" && i + 3 < t.size() && number(t[i + 1], out.to.x) && number(t[i + 2], out.to.y) && number(t[i + 3], out.to.z)) {
			to = true, i += 2;
			continue;
		}
		if (i + 1 >= t.size() || !number(t[i + 1], v)) return "\"" + w + "\": " + usage;
		const bool distance = out.kind == SpaceCam::Zoom && (w == "from" || w == "to");
		if ((out.kind == SpaceCam::Orbit || out.kind == SpaceCam::Flyby) && w == "radius" && v > 0.0) out.radius = v, radius = true;
		else if (out.kind == SpaceCam::Orbit && w == "degrees") out.degrees = v;
		else if (distance && v > 0.0) (w == "from" ? out.near0 : out.near1) = v, (w == "from" ? from : to) = true;
		else if (!distance && (w == "from" || w == "bearing")) out.from = v;
		else if (w == "elevation" && v >= -90.0 && v <= 90.0) out.elevation = v;
		else if (w == "fov" && v > 0.0 && v < 180.0) out.fov = v;
		else return "\"" + w + "\": " + usage;
	}
	if ((out.kind == SpaceCam::Orbit || out.kind == SpaceCam::Flyby) && !radius) return "how far (`radius R`): " + usage;
	if (out.kind == SpaceCam::Zoom && !(from && to)) return "from how far and to how far (`from D0 to D1`): " + usage;
	if (out.kind == SpaceCam::Hyperspace && !to) return "where it comes out (`to X Y Z`): " + usage;
	c = out;
	return std::string();
}

// ---- light in space ----
// A planet's light on what is near it (`space ... planetshine S`): the sunlit planet as a broad lamp in its surface's colour, as bright as how much of its lit face the
// camera's corner of space sees and how big it looks from there - a fill on the night sides of ships, moons and astronauts beside it. A spotlight halfway from the camera
// to the planet's surface, aimed at the camera (a distant light from the planet's direction would be shadowed by the planet itself, and a point light would light the
// planet too); none when the planet is far or turned dark side on.
inline std::string planetshineText(const std::vector<planets::Planet> &bodies, double nt, Vec3 eye, Vec3 sunDir, double strength) {
	std::string out;
	for (std::size_t i = 0; i < bodies.size(); ++i) {
		const planets::Planet &p = bodies[i];
		const Vec3 c = planets::centreAt(bodies, i, nt), toEye = eye - c;
		const double d = std::max(length(toEye), p.radius * 1.001), lit = 0.5 * (1.0 + dot(toEye * (1.0 / d), sunDir)), size = std::pow(p.radius / d, 2.0);
		const double amount = strength * lit * size * 3.0;
		if (amount < 1e-4) continue;
		Vec3 albedo{0.0, 0.0, 0.0};   // (its surface's colour, on average)
		for (int k = 0; k < 64; ++k) {
			const double z = 2.0 * (k + 0.5) / 64.0 - 1.0, a = 2.39996 * k;
			double sea = 0.0, glow = 0.0;
			albedo = albedo + planets::surfaceAt(p, {std::sqrt(1.0 - z * z) * std::cos(a), std::sqrt(1.0 - z * z) * std::sin(a), z}, sea, glow) * (1.0 / 64.0);
		}
		const double r = 0.5 * (d - p.radius), I = amount * r * r;   // (as bright at the camera as the planet's light: E = I / r^2)
		const Vec3 from = eye + (c - eye) * (r / d);
		char buf[400];
		std::snprintf(buf, sizeof buf, "LightSource \"spot\" \"point3 from\" [ %.4f %.4f %.4f ] \"point3 to\" [ %.4f %.4f %.4f ] \"float coneangle\" [ 70 ] \"float conedelta\" [ 20 ] \"rgb I\" [ %.6g %.6g %.6g ]\n",
					  from.x, from.y, from.z, eye.x, eye.y, eye.z, albedo.x * I, albedo.y * I, albedo.z * I);
		out += "# planetshine from " + p.name + " (film_voyage.h)\n" + buf;
	}
	return out;
}
// A frame's scene text with the look of space (`space ... flare F bloom B`): the lens's flare and glow added to its `# @rt-look` line (lens_look.h; later words win), or a
// line of their own at its top. As it was when neither is asked for.
inline std::string withSpaceLook(std::string scene, double flare, double bloom) {
	if (flare <= 0.0 && bloom <= 0.0) return scene;
	char words[96];
	std::snprintf(words, sizeof words, " flare %.3f bloom %.3f", flare, bloom);
	const std::size_t look = scene.find("# @rt-look");
	if (look == std::string::npos) return "# @rt-look" + std::string(words) + "\n" + scene;
	std::size_t end = scene.find('\n', look);
	if (end == std::string::npos) end = scene.size();
	if (end > look && scene[end - 1] == '\r') --end;
	return scene.insert(end, words);
}

// ---- sound in space ----
// No sound carries in space, so a film's is the cinema's: what is near is heard at once (not a sound's 343 m/s later), quieter further off and panned to its side, with
// little reverb (there are no walls). Under it all, the hum of space - a low rumble and a slow deep tone (`space ... hum H`, 0 for silence).
//
//   ships        the rumble of their engines while they burn, from where they are: a craft passing swells, sweeps across and fades
//   astronauts   breathing inside the helmet and a radio's blip now and then, when the camera is close to them
//   stars        each flare's deep whoomp
//   supernovae   the blast's boom and its long rumble
//   black holes  a drone so low it is felt more than heard, near them
//   hyperspace   a rising rush as the stars stretch, the boom of the flash, the rush falling away as they come back
struct SpaceSounds {
	double hum = 0.6;
	std::vector<Ship> ships;
	std::vector<Astronaut> astronauts;
	std::vector<Star> stars;
	std::vector<Remnant> remnants;
	std::vector<BlackHole> blackHoles;
	struct Jump {
		double t0 = 0.0, t1 = 0.0;   // (film time: the shot's)
	};
	std::vector<Jump> jumps;
	std::function<double(double)> natureAt;   // nature's time at a film time (what the bodies move by)
};
// A sound from pbrt's `p`, made at `t`, heard at once: `gain` within `within` metres, falling off as 1/d beyond, panned to its side.
inline void heard(audio::Mix &m, const soundscape::Ears &ears, double t, Vec3 p, const std::vector<float> &sound, double gain, double within) {
	const soundscape::Ears::At a = ears(t);
	const Vec3 d = p - a.eye;
	const double dist = length(d), g = gain * within / std::max(within, dist);
	if (g < 2e-4 || sound.empty()) return;
	const double pan = dist > 1e-6 ? 0.85 * std::clamp(dot(d * (1.0 / dist), a.right), -1.0, 1.0) : 0.0;
	m.put(t, sound, g, pan, 0.08);
}
inline void spaceSound(audio::Mix &m, const soundscape::Ears &ears, const SpaceSounds &s) {
	const double T = m.seconds();
	const auto nature = [&s](double t) { return s.natureAt ? s.natureAt(t) : t; };
	if (s.hum > 0.0) {   // the hum of space: a rumble in grains half a grain apart, and a deep tone swelling every eight seconds
		const std::vector<std::vector<float>> rumble = soundscape::roar(22.0, 110.0, 2.0, 2101u);
		int k = 0;
		for (double t = 0.0; t < T; t += 1.0, ++k) m.put(t, rumble[static_cast<std::size_t>(k) % rumble.size()], 0.25 * s.hum, 0.0, 0.1);
		const std::vector<float> tone = audio::pad(audio::hz(28.0), 9.0, 3.0, 3.0);
		for (double t = 0.0; t < T; t += 8.0) m.put(t, tone, 0.05 * s.hum, std::sin(t) * 0.3, 0.2);
	}
	for (std::size_t i = 0; i < s.ships.size(); ++i) {   // engines
		const Ship &sh = s.ships[i];
		if (!(sh.engines == 1 || (sh.engines < 0 && length(sh.move) > 1e-9))) continue;
		const std::vector<std::vector<float>> burn = soundscape::roar(30.0, 260.0, 0.6, 2201u + static_cast<unsigned>(i));
		int k = 0;
		for (double t = 0.0; t < T; t += 0.3, ++k) {
			const Vec3 p = transformPoint(shipPlacement(sh, nature(t)), {0.0, 0.0, 0.0});
			heard(m, ears, t, {-p.x, p.y, p.z}, burn[static_cast<std::size_t>(k) % burn.size()], 0.6, 2.0 * sh.size);
		}
	}
	for (std::size_t i = 0; i < s.astronauts.size(); ++i) {   // breathing in the helmet, a radio's blip, near them
		const Astronaut &a = s.astronauts[i];
		const unsigned seed = 2301u + 17u * static_cast<unsigned>(i);
		const auto close = [&](double t) { return length(astronautWaist(a, nature(t)) - ears(t).eye) < 4.0 * a.size; };
		for (double t = 0.5 + 2.0 * audio::unit(seed); t < T; t += 3.6 + 0.6 * audio::unit(seed + static_cast<unsigned>(t * 10.0))) {
			if (!close(t)) continue;
			m.put(t, audio::noiseBurst(1.1, 0.5, 0.6, 500.0, 1600.0, seed + 1u), 0.10, 0.0, 0.05);         // (in)
			m.put(t + 1.5, audio::noiseBurst(1.4, 0.1, 1.1, 350.0, 900.0, seed + 2u), 0.08, 0.0, 0.05);   // (out)
		}
		for (double t = 3.0 + 6.0 * audio::unit(seed + 3u); t < T; t += 9.0 + 6.0 * audio::unit(seed + static_cast<unsigned>(t))) {
			if (!close(t)) continue;
			m.put(t, audio::chirp(1250.0, 1300.0, 0.07, 0.0, 0.0), 0.04, 0.0, 0.0);
			m.put(t + 0.12, audio::chirp(1650.0, 1700.0, 0.07, 0.0, 0.0), 0.04, 0.0, 0.0);
		}
	}
	for (std::size_t i = 0; i < s.stars.size(); ++i)   // a flare's whoomp (nature's time: heard at the film time it comes to)
		for (std::size_t k = 0; k < s.stars[i].flares.size(); ++k) {
			if (nature(0.0) > s.stars[i].flares[k]) continue;   // (a flare before the film began is not heard: it used to whoomp at 0 s)
			for (double t = 0.0; t < T; t += 1.0 / 24.0)
				if (nature(t) >= s.stars[i].flares[k]) {
					heard(m, ears, t, s.stars[i].at, audio::noiseBurst(3.0, 0.05, 1.8, 50.0, 900.0, 2401u + static_cast<unsigned>(i * 7 + k)), 0.6, 30.0 * s.stars[i].radius);
					break;
				}
		}
	for (std::size_t i = 0; i < s.remnants.size(); ++i) {   // a supernova's boom and long rumble
		const Remnant &r = s.remnants[i];
		if (r.kind != "supernova" || nature(0.0) > r.blast) continue;   // (one that blew up before the film began is not heard either)
		for (double t = 0.0; t < T; t += 1.0 / 24.0)
			if (nature(t) >= r.blast) {
				heard(m, ears, t, r.at, audio::noiseBurst(7.0, 0.01, 3.5, 35.0, 500.0, 2501u + static_cast<unsigned>(i)), 1.0, 50.0 * r.size);
				const std::vector<std::vector<float>> after = soundscape::roar(20.0, 160.0, 2.0, 2511u + static_cast<unsigned>(i));
				for (int g = 0; g < 16 && t + 1.5 + g < T; ++g) heard(m, ears, t + 1.5 + g, r.at, after[static_cast<std::size_t>(g) % after.size()], 0.5 * std::exp(-g / 6.0), 50.0 * r.size);
				break;
			}
	}
	for (std::size_t i = 0; i < s.blackHoles.size(); ++i) {   // a black hole's drone
		const std::vector<std::vector<float>> drone = soundscape::roar(16.0, 55.0, 2.0, 2601u + static_cast<unsigned>(i));
		int k = 0;
		for (double t = 0.0; t < T; t += 1.0, ++k) heard(m, ears, t, s.blackHoles[i].at, drone[static_cast<std::size_t>(k) % drone.size()], 0.9, 15.0 * s.blackHoles[i].radius);
	}
	for (std::size_t i = 0; i < s.jumps.size(); ++i) {   // hyperspace: the rush rising, the boom, the rush falling away
		const SpaceSounds::Jump &j = s.jumps[i];
		const double span = j.t1 - j.t0;
		for (int g = 0; g < 14; ++g) {   // (as the hyperspace text's streaks: stretching from a fifth of the way to halfway, shrinking to three quarters)
			const double u = 0.2 + 0.55 * g / 13.0, rise = u < 0.5 ? (u - 0.2) / 0.3 : 1.0 - (u - 0.5) / 0.25;
			m.put(j.t0 + u * span, audio::noiseBurst(0.5 * span / 13.0 + 0.3, 0.1, 0.25, 200.0 + 4000.0 * rise, 300.0 + 5000.0 * rise, 2701u + static_cast<unsigned>(g)), 0.25 * std::max(0.0, rise), 0.0, 0.1);
		}
		m.put(j.t0 + 0.5 * span, audio::noiseBurst(3.0, 0.005, 1.5, 40.0, 1200.0, 2801u + static_cast<unsigned>(i)), 0.9, 0.0, 0.3);
	}
}

// ---- re-entry and landing ----
// A craft coming down (`descent`): from `from` high and fast at nature's time `start` to touching down at `to` at `land`, along a curve that runs long and shallow and then
// drops, slowing all the way. Early on it is wrapped in re-entry's fire - a bow shock of glowing plasma before its nose and a burning wake streaming behind it, sparks shed
// along it; then (`chutes on`) its parachutes open; just before it lands its retro-rockets fire and kick up a ring of dust from the ground; then it rests.
struct Descent {
	std::string name;
	Ship craft;   // what comes down: its kind, size, colour, seed (its place and motion are the descent's)
	Vec3 from{0.0, 20000.0, -60000.0}, to{0.0, 0.0, -2000.0};
	double start = 0.0, land = 30.0, heat = 1.0;
	bool chutes = true;
};
// How far along its descent it is at nature's time `nt` (0 .. 1, slowing as it comes), and where it is then (pbrt's).
inline double descentTime(const Descent &d, double nt) { return std::clamp((nt - d.start) / std::max(1e-6, d.land - d.start), 0.0, 1.0); }   // (the share of its time)
inline double descentU(const Descent &d, double nt) { return 1.0 - std::pow(1.0 - descentTime(d, nt), 2.2); }
inline Vec3 descentAt(const Descent &d, double s) {
	const Vec3 bend{d.to.x, d.to.y + 0.35 * (d.from.y - d.to.y), d.to.z};   // (a long shallow run, then the drop)
	return d.from * ((1.0 - s) * (1.0 - s)) + bend * (2.0 * s * (1.0 - s)) + d.to * (s * s);
}
// How hot it is (0 .. 1) that far along: the fire of re-entry, early in the descent.
inline double descentHeat(const Descent &d, double s) { return d.heat * smoothstep(0.0, 0.06, s) * (1.0 - smoothstep(0.35, 0.6, s)); }
inline std::string descentText(const Descent &d, double nt) {
	const double s = descentU(d, nt), u = descentTime(d, nt), heat = descentHeat(d, s), size = d.craft.size;
	const Vec3 at = descentAt(d, s), ahead = descentAt(d, std::min(1.0, s + 0.002)) - descentAt(d, std::max(0.0, s - 0.002));
	Ship c = d.craft;   // (its nose along its way, but never steeper than 35 degrees down: it lands level)
	c.at = at, c.move = {0.0, 0.0, 0.0}, c.engines = 0;
	const Vec3 flat{-ahead.x, 0.0, ahead.z};   // (story space)
	if (length(flat) > 1e-9) c.heading = std::atan2(-flat.z, flat.x) * 180.0 / kPi;
	c.pitch = std::max(-35.0, std::atan2(ahead.y, std::max(1e-9, std::hypot(ahead.x, ahead.z))) * 180.0 / kPi);
	std::string out = "# a descent (film_voyage.h): " + d.name + "\n" + shipText(c, nt);
	const Mat4 M = shipPlacement(c, nt);
	const auto story = [](Vec3 p) { return Vec3{-p.x, p.y, p.z}; };
	const double flicker = 0.8 + 0.2 * std::sin(nt * 31.0) * std::sin(nt * 17.0 + 1.0);
	if (heat > 0.01) {
		// the bow shock: a glowing lens of plasma before its nose
		const Vec3 shock = Vec3{1.0, 0.55, 0.3} * (5.0 * heat * flicker);
		out += "# its re-entry fire\n" + attr(diffuseMaterial({0.0, 0.0, 0.0}), emitMesh(uvSphere(1.0, 20, 10), mul(M, translate(0.55, 0.0, 0.0), scale(0.1, 0.3, 0.3))), &shock);
		// its burning wake, streaming back along its way - white-hot at the craft, cooling to red as it trails off and thinning - and sparks shed along it
		const Vec3 back = normalize(ahead * -1.0 + Vec3{1e-12, 0.0, 0.0}), side = normalize(cross(back, Vec3{0.0, 1.0, 0.0}) + Vec3{1e-9, 0.0, 0.0}), up = cross(side, back);
		const double len = size * (6.0 + 8.0 * heat);
		for (int part = 0; part < 4; ++part) {
			Mesh wake;
			for (int k = 0; k < 7; ++k) {
				std::vector<Vec3> pts;
				const double a = 2.0 * kPi * (k + 0.37 * part) / 7.0;
				for (int i = 0; i <= 6; ++i) {
					const double v = (part + i / 6.0) / 4.0, spread = size * (0.1 + 0.6 * v * (0.7 + 0.6 * hash01(k, 11))),
								 wob = 0.12 * size * v * std::sin(nt * 9.0 + k * 1.3 + v * 12.0);
					pts.push_back(story(at + back * (0.45 * size + len * v) + (side * std::cos(a) + up * std::sin(a)) * spread + up * wob));
				}
				const Mesh m = sdf::tubeMesh(pts, size * (0.09 - 0.018 * part), 0.8, 6);
				const int base = static_cast<int>(wake.verts.size());
				wake.verts.insert(wake.verts.end(), m.verts.begin(), m.verts.end());
				wake.normals.insert(wake.normals.end(), m.normals.begin(), m.normals.end());
				for (const auto &tr : m.tris) wake.tris.push_back({tr[0] + base, tr[1] + base, tr[2] + base});
			}
			const double cool = part / 3.0;
			const Vec3 burn = lerp(Vec3{1.0, 0.8, 0.55}, Vec3{1.0, 0.3, 0.08}, cool) * (4.0 * heat * flicker * std::exp(-0.6 * part));
			out += attr(diffuseMaterial({0.0, 0.0, 0.0}), emitMesh(wake, identity()), &burn);
		}
		for (int k = 0; k < 24; ++k) {
			const double v = std::fmod(hash01(k, 1) + nt * 0.7, 1.0);
			const Vec3 p = at + back * (size * (1.0 + 14.0 * v)) + (side * (hash01(k, 2) - 0.5) + up * (hash01(k, 3) - 0.5)) * (size * (0.3 + 1.2 * v));
			out += emissiveSphere(story(p), size * 0.02, Vec3{1.0, 0.7, 0.35} * (6.0 * heat * (1.0 - v)));
		}
	}
	if (d.chutes && u > 0.55 && u < 0.985) {   // its parachutes: three canopies, opening
		const double open = smoothstep(0.55, 0.6, u);
		for (int k = 0; k < 3; ++k) {
			const double a = 2.0 * kPi * k / 3.0;
			const Vec3 top = at + Vec3{std::cos(a) * 0.45 * size, 1.6 * size, std::sin(a) * 0.45 * size};
			std::vector<std::pair<double, double>> dome;
			for (int i = 0; i <= 8; ++i) {
				const double b = 0.5 * kPi * i / 8.0;
				dome.push_back({0.5 * size * open * std::cos(b), 0.3 * size * open * std::sin(b)});
			}
			out += attr(diffuseMaterial(k % 2 ? Vec3{0.9, 0.9, 0.88} : Vec3{0.95, 0.45, 0.12}), emitMesh(lathe(dome, 20), translate(-top.x, top.y, top.z)));
			out += attr(diffuseMaterial({0.8, 0.8, 0.8}), emitMesh(sdf::tubeMesh({story(at), story(top)}, 0.006 * size, 1.0, 4), identity()));
		}
	}
	if (u > 0.93 && u < 1.0) {   // the retro-rockets and the dust they kick up, in its last moments
		const double burn = smoothstep(0.93, 0.95, u), dust = smoothstep(0.94, 1.0, u);
		out += emissiveSphere(story(at - Vec3{0.0, 0.25 * size, 0.0}), 0.12 * size, Vec3{1.0, 0.75, 0.45} * (8.0 * burn * flicker));
		for (int k = 0; k < 28; ++k) {
			const double a = 2.0 * kPi * k / 28.0 + hash01(k, 7), r = size * (0.6 + 2.2 * dust * (0.7 + 0.6 * hash01(k, 8)));
			const Vec3 p{d.to.x + std::cos(a) * r, d.to.y + 0.15 * size * dust, d.to.z + std::sin(a) * r};
			out += attr(diffuseMaterial({0.45, 0.4, 0.34}), emitMesh(uvSphere(size * (0.12 + 0.25 * dust) * (0.7 + 0.6 * hash01(k, 9)), 10, 5), translate(-p.x, p.y, p.z)));
		}
	}
	return out;
}
// A descent's sound (film_voyage.h's sound in space): the roar of re-entry's fire while it burns, the retro-rockets' blast and the thump of touching down, from where it is.
inline void descentSound(audio::Mix &m, const soundscape::Ears &ears, const std::vector<Descent> &all, const std::function<double(double)> &natureAt) {
	const double T = m.seconds();
	for (std::size_t i = 0; i < all.size(); ++i) {
		const Descent &d = all[i];
		const std::vector<std::vector<float>> roar = soundscape::roar(40.0, 900.0, 0.6, 2901u + static_cast<unsigned>(i));
		int k = 0;
		const double u0 = descentTime(d, natureAt ? natureAt(0.0) : 0.0);
		bool retro = u0 > 0.93, down = u0 >= 1.0;   // (what had happened before the film began is not heard at 0 s)
		for (double t = 0.0; t < T; t += 0.3, ++k) {
			const double nt = natureAt ? natureAt(t) : t, s = descentU(d, nt), u = descentTime(d, nt), heat = descentHeat(d, s);
			const Vec3 at = descentAt(d, s);
			if (heat > 0.01) heard(m, ears, t, at, roar[static_cast<std::size_t>(k) % roar.size()], 0.8 * heat, 3.0 * d.craft.size);
			if (!retro && u > 0.93) retro = true, heard(m, ears, t, at, audio::noiseBurst(2.5, 0.05, 1.2, 80.0, 2500.0, 2911u + static_cast<unsigned>(i)), 0.7, 3.0 * d.craft.size);
			if (!down && u >= 1.0) down = true, heard(m, ears, t, at, audio::noiseBurst(1.2, 0.002, 0.5, 40.0, 300.0, 2921u + static_cast<unsigned>(i)), 0.9, 3.0 * d.craft.size);
		}
	}
}
// The words of a `descent` line into `d`: "" or what is wrong with it.
inline std::string descentLine(const std::vector<std::string> &t, Descent &d) {
	const std::string usage = "`descent NAME from X Y Z to X Y Z start T0 land T1 [kind shuttle|fighter|freighter|station|wheel] [size S] [colour R G B] [chutes on|off] [heat H] [seed N]` "
							  "(land after start; size 1 to 20000 m; heat 0 to 4)";
	const auto number = [](const std::string &s, double &v) {
		char *end = nullptr;
		v = std::strtod(s.c_str(), &end);
		return !s.empty() && end && *end == '\0';
	};
	if (t.size() < 2) return usage;
	Descent out;
	out.name = t[1], out.craft.name = t[1];
	bool from = false, to = false, start = false, land = false;
	for (std::size_t i = 2; i < t.size();) {
		const std::string &w = t[i];
		Vec3 v;
		double a = 0.0;
		if ((w == "from" || w == "to" || w == "colour") && i + 3 < t.size() && number(t[i + 1], v.x) && number(t[i + 2], v.y) && number(t[i + 3], v.z)) {
			if (w == "colour" && (std::min({v.x, v.y, v.z}) < 0.0 || std::max({v.x, v.y, v.z}) > 1.0)) return "\"colour\": " + usage;
			if (w == "from") out.from = v, from = true;
			else if (w == "to") out.to = v, to = true;
			else out.craft.colour = v;
			i += 4;
		} else if (w == "kind" && i + 1 < t.size() && std::find(shipKinds().begin(), shipKinds().end(), t[i + 1]) != shipKinds().end()) {
			out.craft.kind = t[i + 1], i += 2;
		} else if (w == "chutes" && i + 1 < t.size() && (t[i + 1] == "on" || t[i + 1] == "off")) {
			out.chutes = t[i + 1] == "on", i += 2;
		} else if (i + 1 < t.size() && number(t[i + 1], a) &&
				   ((w == "start") || (w == "land") || (w == "size" && a >= 1.0 && a <= 20000.0) || (w == "heat" && a >= 0.0 && a <= 4.0) || w == "seed")) {
			if (w == "start") out.start = a, start = true;
			else if (w == "land") out.land = a, land = true;
			else if (w == "size") out.craft.size = a;
			else if (w == "heat") out.heat = a;
			else out.craft.seed = static_cast<int>(a);
			i += 2;
		} else {
			return "\"" + w + "\": " + usage;
		}
	}
	if (!(from && to && start && land)) return "where from, where to, when it starts and when it lands: " + usage;
	if (out.land <= out.start) return "\"land\" before \"start\": " + usage;
	d = out;
	return std::string();
}

}  // namespace cosmos
}  // namespace cartoon
