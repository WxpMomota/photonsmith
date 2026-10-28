#pragma once
// film_voyage.h -- travelling in space for a film (film_file.h `ship`; round 10): the craft, the people in them and the camera's journeys (the bodies they travel among
// are film_cosmos.h's).
//
//   ship       a shuttle, a fighter, a freighter, a station or a wheel, flying nose first along its way, its engines glowing, its running lights blinking
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

}  // namespace cosmos
}  // namespace cartoon
