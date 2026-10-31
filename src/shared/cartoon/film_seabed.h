#pragma once
// film_seabed.h -- the seabed, round 14: what lives on it, and places to film under the sea.
//
//   seabed anemones|seagrass|starfish|urchins|crabs|gardeneels N in X0 Z0 X1 Z1 [seed K]
//            anemones: a column and a disc crowned with a hundred tentacles swaying in the water, clownfish living in them (darting out and ducking back in); seagrass:
//            a meadow of ribbon blades (N clumps) bending with the current and the surge; starfish and urchins (a ball of long dark spines) on the sand; crabs walking
//            sideways in short dashes on their eight legs, claws up; garden eels: a colony standing out of their holes in the sand, swaying, facing into the current - and
//            sinking back into their holes as the camera comes within a few metres
//   wreck X Z [toward A] [length L] [list D]
//            a sunken ship: a rusted hull open where the deck rotted away, its ribs showing, portholes, a fallen mast, settled into the sand on its side by D degrees and
//            grown over with corals
//   arch X Z [toward A] [size S]
//            a rock arch with holes in its roof: the sun comes down through them in shafts; corals and sponges round its foot
//   cenote X Z [radius R] [depth D]
//            a cavern in the rock, flooded, open to the sky through a round hole at the surface: the sun comes down through it in a single beam onto the floor D deep
//            (with a `halocline` it shimmers half way down); roots hang down from the opening
//   reefwall X Z [toward A] [length L] [height H]
//            a wall of reef rising sheer from the floor, its face crowded with corals, sea fans and sponges: with a deep floor, a drop-off into the blue
//
// Coordinates: pbrt's (x, z) in the film file, on the ground there; meshes are made in story space (x mirrored), the places once, as PLY files (film_cache.h).

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "cartoon_lib.h"
#include "film_cache.h"
#include "film_deepsea.h"
#include "film_sealife.h"
#include "film_underwater.h"
#include "nature_lib.h"
#include "sdf_mesh.h"

namespace cartoon {
namespace seabed {

struct Patch {
	std::string kind;
	int count = 10, seed = 1;
	double x0 = -5.0, z0 = -5.0, x1 = 5.0, z1 = 5.0;
};
struct Place {
	std::string kind;   // wreck, arch, cenote, reefwall, wreckroom (round 16)
	double x = 0.0, z = 0.0, toward = 0.0, size = 1.0, length = 30.0, height = 20.0, list = 20.0, radius = 12.0, depth = 25.0;
};
struct World {
	std::vector<Patch> patches;
	std::vector<Place> places;
	bool any() const { return !patches.empty() || !places.empty(); }
};
inline const std::vector<std::string> &kinds() {
	static const std::vector<std::string> k = {"anemones", "seagrass", "starfish", "urchins", "crabs", "gardeneels"};
	return k;
}

namespace detail {
using deepsea::detail::add;
using deepsea::detail::beads;
using deepsea::detail::cone;
using sealife::detail::facingOf;
using sealife::detail::fin;
using sealife::detail::story;
inline Vec3 spot(const Patch &p, int i, int salt) { return {p.x0 + (p.x1 - p.x0) * hash01(i * 7 + p.seed * 9973, salt), 0.0, p.z0 + (p.z1 - p.z0) * hash01(i * 7 + p.seed * 9973, salt + 1)}; }
}  // namespace detail

// ---- life on the seabed ----
// Anemones at t, swaying in `flow` (pbrt's, metres a second), with their clownfish.
inline std::string anemoneText(const Patch &p, double t, const std::function<double(double, double)> &groundAt, const Vec3 &flow) {
	static const Vec3 col[4] = {{0.75, 0.25, 0.55}, {0.45, 0.65, 0.35}, {0.9, 0.55, 0.3}, {0.85, 0.8, 0.65}};
	std::string out = "# anemones (film_seabed.h)\n";
	Mesh fish[2];   // (clownfish: orange, and their white bands)
	for (int i = 0; i < p.count; ++i) {
		const Vec3 at = detail::spot(p, i, 1);
		const Vec3 foot{at.x, groundAt(at.x, at.z), at.z};
		const int s = p.seed * 47 + i;
		const double R = 0.12 + 0.12 * hash01(s, 1), H = 0.08 + 0.06 * hash01(s, 2);
		Mesh column = lathe({{R * 0.8, 0.0}, {R * 0.75, H}, {R, H}, {0.0, H + 0.01}}, 20), tent;
		column.uvs.clear();
		const int n = 70 + static_cast<int>(60.0 * hash01(s, 3));
		for (int k = 0; k < n; ++k) {   // the tentacles, swaying together with the water and each a little on its own
			const double a = 2.0 * kPi * hash01(k, s + 4), r = R * std::sqrt(0.15 + 0.85 * hash01(k, s + 5)), len = (0.6 + 0.6 * hash01(k, s + 6)) * R;
			const Vec3 root{r * std::cos(a), H, r * std::sin(a)};
			const Vec3 lean = Vec3{-flow.x, 0.0, flow.z} * 1.4 + Vec3{0.25 * std::sin(1.3 * t + k * 0.3 + i), 0.0, 0.25 * std::cos(1.1 * t + k * 0.21)};
			std::vector<Vec3> pts;
			for (int q = 0; q <= 5; ++q) {
				const double u = q / 5.0;
				pts.push_back(root + (Vec3{std::cos(a) * 0.45, 1.0, std::sin(a) * 0.45} + lean * u) * (len * u));
			}
			detail::add(tent, sdf::tubeMesh(pts, 0.011 * R / 0.18, 0.55, 5));
		}
		const Mat4 place = translate(detail::story(foot));
		out += attr(coated(col[s % 4] * 0.8, 0.3), emitMesh(column, place)) + attr(translucentMaterial(col[s % 4], col[s % 4] * 0.4), emitMesh(tent, place));
		for (int f = 0; f < 2 + s % 2; ++f) {   // its clownfish: round its crown, now and then ducking down into it
			const double ph = 2.0 * kPi * hash01(f, s + 9), w = 0.6 + 0.3 * hash01(f, s + 10), a = w * t + ph;
			const double duck = smoothstep(0.75, 0.95, 0.5 + 0.5 * std::sin(0.35 * t + 2.0 * f + i));
			const Vec3 c = foot + Vec3{0.8 * R * std::cos(a), H + 0.18 * (1.0 - duck) + 0.03, 0.8 * R * std::sin(a)};
			const Vec3 fwd{-std::sin(a), 0.0, std::cos(a)};
			const int pose = static_cast<int>(std::floor((t * 4.0 + 0.3 * f) * sealife::detail::kPoses)) % sealife::detail::kPoses;
			const Mat4 m = mul(translate(detail::story(c)), detail::facingOf(detail::story(fwd)), scale(0.55));
			detail::add(fish[0], sealife::detail::fish(pose, 0.16, 1.2), m);
			detail::add(fish[1], sealife::detail::bands(pose, 0.16, 1.2, {0.28, 0.02, -0.3}), m);
		}
	}
	return out + attr(coated({0.95, 0.42, 0.05}, 0.3), emitMesh(fish[0], identity())) + attr(coated({0.95, 0.95, 0.93}, 0.3), emitMesh(fish[1], identity()));
}
// A seagrass meadow: N clumps of ribbon blades bending with the flow and the surge.
inline std::string seagrassText(const Patch &p, double t, const std::function<double(double, double)> &groundAt, const Vec3 &flow) {
	Mesh m;
	for (int i = 0; i < p.count; ++i) {
		const Vec3 at = detail::spot(p, i, 3);
		const Vec3 foot{-at.x, groundAt(at.x, at.z), at.z};
		for (int b = 0; b < 12; ++b) {
			const int s = p.seed * 83 + i * 17 + b;
			const double h = 0.3 + 0.5 * hash01(s, 1), a = 2.0 * kPi * hash01(s, 2);
			const Vec3 base = foot + Vec3{0.12 * std::cos(a), 0.0, 0.12 * std::sin(a)};
			const Vec3 lean = Vec3{-flow.x, 0.0, flow.z} * 1.6 + Vec3{0.25 * std::sin(0.9 * t + at.x * 0.7 + b), 0.0, 0.15 * std::cos(0.8 * t + at.z * 0.5)};
			const auto path = [&](double u) { return base + Vec3{0.0, h * u * (1.0 - 0.35 * u * length(lean)), 0.0} + lean * (h * u * u * 0.8); };
			nature::addStrip(m, path, [](double u) { return 0.006 * (1.0 - 0.5 * u); }, normalize(Vec3{std::cos(a), 0.0, std::sin(a)}), 6);
		}
	}
	return m.tris.empty() ? std::string() : "# seagrass (film_seabed.h)\n" + attr(translucentMaterial({0.16, 0.32, 0.08}, {0.12, 0.25, 0.05}), emitMesh(m, identity()));
}
// Starfish (five arms, in reds and oranges) and urchins (a ball of long dark spines, swaying a little), lying on the sand.
inline std::string starsAndUrchinsText(const Patch &p, double t, const std::function<double(double, double)> &groundAt) {
	std::string out = "# " + p.kind + " (film_seabed.h)\n";
	static const Vec3 col[4] = {{0.85, 0.3, 0.1}, {0.75, 0.15, 0.2}, {0.55, 0.2, 0.55}, {0.9, 0.6, 0.15}};
	Mesh body, spines;
	for (int i = 0; i < p.count; ++i) {
		const Vec3 at = detail::spot(p, i, 5);
		const int s = p.seed * 31 + i;
		const Vec3 foot{at.x, groundAt(at.x, at.z), at.z};
		if (p.kind == "starfish") {
			const double R = 0.08 + 0.08 * hash01(s, 1);
			Mesh star;
			const int around = 60;
			star.verts.push_back({0.0, 0.025 * R / 0.1, 0.0}), star.normals.push_back({0, 1, 0});
			for (int k = 0; k <= around; ++k) {
				const double a = 2.0 * kPi * k / around, arm = 0.35 + 0.65 * std::pow(std::abs(std::cos(2.5 * a)), 3.0);
				star.verts.push_back({R * arm * std::cos(a), 0.008, R * arm * std::sin(a)});
				star.normals.push_back(normalize(Vec3{std::cos(a) * 0.4, 1.0, std::sin(a) * 0.4}));
			}
			for (int k = 1; k <= around; ++k) star.tris.push_back({0, k + 1, k});
			out += attr(coated(col[s % 4], 0.6), emitMesh(star, mul(translate(detail::story(foot)), rotate({0, 1, 0}, 360.0 * hash01(s, 2)))));
		} else {
			const double R = 0.04 + 0.03 * hash01(s, 1);
			detail::add(body, uvSphere(R, 12, 8), translate(detail::story(foot + Vec3{0.0, 0.7 * R, 0.0})));
			for (int k = 0; k < 90; ++k) {
				const double z = 2.0 * hash01(k, s + 3) - 0.6, a = 2.0 * kPi * hash01(k, s + 4), rr = std::sqrt(std::max(0.0, 1.0 - z * z));
				const Vec3 d = normalize(Vec3{rr * std::cos(a) + 0.05 * std::sin(0.8 * t + k), z, rr * std::sin(a)});
				detail::add(spines, detail::cone(0.003, R * (2.0 + 1.5 * hash01(k, s + 5)), 4), alignY(detail::story(foot + Vec3{0.0, 0.7 * R, 0.0} + d * (0.9 * R)), detail::story(d)));
			}
		}
	}
	if (!body.tris.empty()) out += attr(coated({0.12, 0.05, 0.12}, 0.4), emitMesh(body, identity())) + attr(coated({0.06, 0.03, 0.07}, 0.3), emitMesh(spines, identity()));
	return out;
}
// Crabs at t: each walking sideways in short dashes (its own line, out and back), legs stepping in alternate fours, claws held up.
// `pale` (round 16): the ghostly white crabs of a hydrothermal vent.
inline std::string crabText(const Patch &p, double t, const std::function<double(double, double)> &groundAt, bool pale = false) {
	Mesh shell, legs;
	for (int i = 0; i < p.count; ++i) {
		const int s = p.seed * 37 + i;
		const Vec3 home = detail::spot(p, i, 7);
		const double ang = 2.0 * kPi * hash01(s, 1), S = 0.08 + 0.06 * hash01(s, 2);
		const Vec3 side{std::cos(ang), 0.0, std::sin(ang)}, fwd{-std::sin(ang), 0.0, std::cos(ang)};
		const double cycle = 5.0 + 3.0 * hash01(s, 3), into = std::fmod(t + cycle * hash01(s, 4), cycle);
		const double dashing = into < 1.6 ? 1.0 : 0.0, n = std::floor((t + cycle * hash01(s, 4)) / cycle);
		const double along = 0.5 * std::sin(kPi * 0.5 * n) + 0.5 * smoothstep(0.0, 1.6, into) * std::cos(kPi * 0.5 * n) * (static_cast<int>(n) % 2 ? -1.0 : 1.0);
		const Vec3 at = home + side * (along * 0.6);
		const Vec3 foot{at.x, groundAt(at.x, at.z) + 0.6 * S, at.z};
		const Mat4 place = mul(translate(detail::story(foot)), detail::facingOf(detail::story(fwd)));
		detail::add(shell, uvSphere(1.0, 14, 8), mul(place, scale(0.55 * S, 0.22 * S, 0.42 * S)));
		for (const double sd : {-1.0, 1.0}) {
			for (int k = 0; k < 4; ++k) {   // four legs a side, stepping
				const double z = (0.18 - 0.14 * k) * S, step = dashing * std::sin(2.0 * kPi * t * 3.0 + (k + (sd > 0 ? 0 : 1)) * kPi);
				const Vec3 hip{sd * 0.45 * S, 0.0, z}, knee{sd * 0.85 * S, 0.25 * S + 0.08 * S * std::max(0.0, step), z + 0.05 * S * step}, toe{sd * 1.15 * S, -0.6 * S, z + 0.08 * S * step};
				std::vector<Vec3> leg;
				for (const Vec3 &q : {hip, knee, toe}) leg.push_back(transformPoint(place, q));
				detail::add(legs, sdf::tubeMesh(leg, 0.05 * S, 0.5, 5));
			}
			std::vector<Vec3> arm;   // a claw, held up and forward
			for (const Vec3 &q : {Vec3{sd * 0.3 * S, 0.0, 0.35 * S}, Vec3{sd * 0.55 * S, 0.3 * S, 0.65 * S}, Vec3{sd * 0.35 * S, 0.35 * S, 0.85 * S}}) arm.push_back(transformPoint(place, q));
			detail::add(legs, sdf::tubeMesh(arm, 0.07 * S, 0.9, 6));
			detail::add(shell, uvSphere(1.0, 10, 6), mul(place, translate(sd * 0.35 * S, 0.35 * S, 0.95 * S), scale(0.12 * S, 0.1 * S, 0.2 * S)));
		}
	}
	return "# crabs (film_seabed.h)\n" + attr(coated(pale ? Vec3{0.85, 0.84, 0.8} : Vec3{0.75, 0.28, 0.12}, 0.3), emitMesh(shell, identity())) +
		   attr(coated(pale ? Vec3{0.88, 0.86, 0.82} : Vec3{0.8, 0.4, 0.2}, 0.35), emitMesh(legs, identity()));
}
// Garden eels at t: standing out of their holes, swaying, facing into the flow; sinking into their holes while the camera (pbrt's eye) is within 4 m, rising again after.
inline std::string gardenEelText(const Patch &p, double t, const std::function<double(double, double)> &groundAt, const Vec3 &flow, const Vec3 &eye) {
	Mesh eels, holes;
	const Vec3 into = length(flow) > 1e-6 ? normalize(Vec3{-flow.x, 0.0, -flow.z}) : Vec3{0.0, 0.0, 1.0};
	for (int i = 0; i < p.count; ++i) {
		const int s = p.seed * 43 + i;
		const Vec3 at = detail::spot(p, i, 9);
		const Vec3 foot{at.x, groundAt(at.x, at.z), at.z};
		const double away = length(Vec3{eye.x - foot.x, 0.0, eye.z - foot.z});
		const double out_ = smoothstep(2.5, 4.5, away), h = (0.25 + 0.2 * hash01(s, 1)) * out_;
		detail::add(holes, lathe({{0.0, 0.002}, {0.02, 0.004}, {0.035, 0.0}}, 10), translate(detail::story(foot)));
		if (h < 0.01) continue;
		std::vector<Vec3> pts;
		for (int q = 0; q <= 10; ++q) {   // up out of the hole, curving over into the flow like a question mark
			const double u = q / 10.0, sway = 0.04 * std::sin(1.2 * t + u * 3.0 + s);
			pts.push_back(detail::story(foot + Vec3{0.0, h * u, 0.0} + into * (0.35 * h * u * u * u) + Vec3{sway, 0.0, 0.6 * sway} * u - Vec3{0.0, 0.12 * h * smoothstep(0.7, 1.0, u), 0.0}));
		}
		detail::add(eels, sdf::tubeMesh(pts, 0.007, 0.8, 6));
	}
	return "# garden eels (film_seabed.h)\n" + attr(coated({0.82, 0.8, 0.72}, 0.3), emitMesh(eels, identity())) + attr(diffuseMaterial({0.08, 0.07, 0.06}), emitMesh(holes, identity()));
}

// ---- places ----
// A rock's surface: the signed distance `f` (story space, metres) meshed in the box, lumpy by `rough` metres (big bulges, and smaller knobs on them).
template <typename F>
inline Mesh rockMesh(const F &f, Vec3 lo, Vec3 hi, double cell, int salt, double rough) {
	const auto lumpy = [&](Vec3 p) {
		const double big = nature::noise2(p.x * 0.6 / rough + p.y * 0.35 / rough, p.z * 0.6 / rough - p.y * 0.25 / rough, salt) - 0.5;
		const double small = nature::noise2(p.x * 2.1 / rough - p.y * 1.3 / rough, p.z * 2.1 / rough + p.y * 0.9 / rough, salt + 7) - 0.5;
		return f(p) + rough * (1.6 * big + 0.5 * small);
	};
	Mesh m = sdf::surfaceNets(lumpy, lo, hi, cell);
	sdf::computeNormals(m);
	return m;
}
// A sunken ship `length` long (along +z, its keel at y 0, its deck 0.3 length wide): the hull planks round its ribs, open amidships, a broken mast lying across it.
inline Mesh wreckHull(double length, Mesh &ribs, Mesh &portholes) {
	Mesh hull;
	const double W = 0.17 * length, H = 0.13 * length;
	const int nz = 40, nr = 16;
	const auto halfWidth = [&](double v) { return W * (v > 0.75 ? std::sqrt(std::max(0.0, 1.0 - std::pow((v - 0.75) / 0.25, 2.0))) : v < 0.08 ? 0.75 + 0.25 * v / 0.08 : 1.0); };
	for (int j = 0; j <= nz; ++j)
		for (int i = 0; i <= nr; ++i) {
			const double v = j / double(nz), a = kPi * i / nr;   // (round the hull from one gunwale, under the keel, to the other)
			const double hw = halfWidth(v), z = (v - 0.5) * length;
			hull.verts.push_back({-hw * std::cos(a), H * (1.0 - std::sin(a)) * (0.35 + 0.65 * std::sqrt(hw / W)), z});
			hull.normals.push_back({-std::cos(a), -std::sin(a), 0.0});
		}
	for (int j = 0; j < nz; ++j)
		for (int i = 0; i < nr; ++i) {
			const int a = j * (nr + 1) + i;
			if (j > 14 && j < 26 && (i < 3 || i > nr - 3) && (j + i) % 3 != 0) continue;   // (planks rotted through near the gunwales amidships: the ribs show)
			hull.tris.push_back({a, a + nr + 1, a + 1}), hull.tris.push_back({a + 1, a + nr + 1, a + nr + 2});
		}
	sdf::computeNormals(hull);
	for (int j = 2; j < nz; j += 2) {   // the ribs
		std::vector<Vec3> rib;
		const double v = j / double(nz);
		for (int i = 0; i <= nr; ++i) {
			const double a = kPi * i / nr, hw = halfWidth(v) * 0.97;
			rib.push_back({-hw * std::cos(a), H * (1.0 - std::sin(a)) * (0.35 + 0.65 * std::sqrt(hw / W)) + 0.05, (v - 0.5) * length});
		}
		deepsea::detail::add(ribs, sdf::tubeMesh(rib, 0.012 * length, 1.0, 6));
	}
	for (int k = 0; k < 10; ++k)   // portholes along both sides
		for (const double side : {-1.0, 1.0}) deepsea::detail::add(portholes, torus(0.012 * length, 0.003 * length, 16, 6), alignY({side * W * 1.01, 0.72 * H, (-0.3 + 0.06 * k) * length}, {side, 0.0, 0.0}));
	// the deck at bow and stern, the mast broken and lying across
	for (const double end : {-1.0, 1.0}) deepsea::detail::add(ribs, box(1.8 * W, 0.01 * length, 0.18 * length), translate(0.0, H * 0.98, end * 0.36 * length));
	deepsea::detail::add(ribs, sdf::tubeMesh({{-0.6 * W, H * 1.0, -0.05 * length}, {0.5 * W, H * 1.1, 0.12 * length}, {1.6 * W, H * 0.2, 0.3 * length}}, 0.012 * length, 0.8, 8));
	return hull;
}
inline std::string placeText(const std::string &film, const Place &p, int index, const std::function<double(double, double)> &groundAt) {
	const double y = groundAt(p.x, p.z);
	const Mat4 frame = mul(translate(detail::story({p.x, y, p.z})), rotate({0, 1, 0}, -p.toward));   // (story space: its +z along its facing)
	char name[64];
	std::string out = "# a " + p.kind + " (film_seabed.h)\n";
	const std::string rock = coated({0.36, 0.32, 0.27}, 0.7);
	if (p.kind == "wreck") {
		std::snprintf(name, sizeof name, "wreck_%d_%d", static_cast<int>(p.length * 10), index);
		const Mat4 sunk = mul(frame, translate(0.0, -0.06 * p.length, 0.0), rotate({0, 0, 1}, p.list));
		Mesh ribs, portholes;
		const Mesh hull = wreckHull(p.length, ribs, portholes);
		out += film_cache::part(film, std::string(name) + "_hull", coated({0.3, 0.17, 0.09}, 0.75), sunk, [&] { return hull; });
		out += film_cache::part(film, std::string(name) + "_ribs", coated({0.22, 0.13, 0.08}, 0.8), sunk, [&] { return ribs; });
		out += attr(coated({0.12, 0.1, 0.08}, 0.3), emitMesh(portholes, sunk));
		underwater::Coral growth;   // (corals grown on its gunwales and decks)
		growth.count = static_cast<int>(p.length * 1.2), growth.seed = index + 5;
		const Vec3 a = transformPoint(sunk, {-0.17 * p.length, 0.0, -0.45 * p.length}), b = transformPoint(sunk, {0.17 * p.length, 0.0, 0.45 * p.length});
		growth.x0 = std::min(-a.x, -b.x), growth.x1 = std::max(-a.x, -b.x), growth.z0 = std::min(a.z, b.z), growth.z1 = std::max(a.z, b.z);
		const Mat4 inv = invertAffine(sunk);
		const double W = 0.17 * p.length, H = 0.13 * p.length;
		out += underwater::coralText(growth, [&](double x, double z) {   // (on the hull's planking where it is there - inside it, on its curve - else on the sand)
			const Vec3 l = transformPoint(inv, detail::story({x, y, z}));
			if (std::abs(l.z) > 0.42 * p.length || std::abs(l.x) > W) return groundAt(x, z);
			return transformPoint(sunk, {l.x, H * (1.0 - std::sqrt(std::max(0.0, 1.0 - l.x * l.x / (W * W)))), l.z}).y;
		});
		return out;
	}
	if (p.kind == "arch") {
		const double S = 6.0 * p.size;
		std::snprintf(name, sizeof name, "arch_%d_%d", static_cast<int>(S * 100), index);
		out += film_cache::part(film, name, rock, frame, [&] {
			const auto f = [S](Vec3 q) {
				const double mass = sdf::roundBox(q, translate(0.0, -0.4 * S, 0.0), {1.5 * S, 0.75 * S, 0.8 * S}, 0.4 * S);
				const double tunnel = sdf::capsule(q, {0.0, 0.25 * S, -2.0 * S}, {0.0, 0.25 * S, 2.0 * S}, 0.55 * S);
				double holes = 1e9;
				for (int k = 0; k < 3; ++k) holes = std::min(holes, sdf::capsule(q, {(-0.6 + 0.6 * k) * S, 0.2 * S, (0.15 - 0.15 * k) * S}, {(-0.55 + 0.6 * k) * S, 1.6 * S, (0.2 - 0.15 * k) * S}, (0.14 + 0.05 * k) * S));
				return sdf::smax(sdf::smax(mass, -tunnel, 0.3 * S), -holes, 0.1 * S);
			};
			return rockMesh(f, {-2.4 * S, -0.4 * S, -1.6 * S}, {2.4 * S, 1.6 * S, 1.6 * S}, 0.05 * S, 17, 0.12 * S);
		});
		underwater::Coral foot;
		foot.count = static_cast<int>(18 * p.size), foot.seed = index + 11;
		foot.x0 = p.x - 1.6 * S, foot.x1 = p.x + 1.6 * S, foot.z0 = p.z - 1.6 * S, foot.z1 = p.z + 1.6 * S;
		return out + underwater::coralText(foot, groundAt);
	}
	if (p.kind == "cenote") {
		const double R = p.radius, D = p.depth;
		std::snprintf(name, sizeof name, "cenote_%d_%d_%d", static_cast<int>(R * 10), static_cast<int>(D * 10), index);
		// (heights from the floor: the cavern's floor at y, its roof at the surface's height, the opening above it)
		out += film_cache::part(film, name, rock, frame, [&] {
			const auto f = [R, D](Vec3 q) {
				const double outer = sdf::roundBox(q, translate(0.0, -0.5 * D, 0.0), {3.0 * R, 0.5 * D + 6.0, 3.0 * R}, std::min(0.9 * R, 0.4 * D + 4.0));   // (a rounded mass of rock)
				const double cavern = sdf::ellipsoid(q, Vec3{0.0, 0.42 * D, 0.0}, Vec3{R * 1.2, 0.62 * D, R});
				const double opening = sdf::capsule(q, {0.0, 0.6 * D, 0.0}, {0.0, D + 10.0, 0.0}, 0.32 * R);
				return sdf::smax(sdf::smax(outer, -cavern, 1.5), -opening, 1.0);
			};
			return rockMesh(f, {-3.5 * R, -1.0, -3.5 * R}, {3.5 * R, D + 8.0, 3.5 * R}, std::max(0.3, R / 18.0), 23, 0.1 * R);
		});
		Mesh roots;   // roots hanging down through the opening
		for (int k = 0; k < 24; ++k) {
			const double a = 2.0 * kPi * hash01(k, 3), r = 0.3 * R * (0.8 + 0.2 * hash01(k, 4)), len = 2.0 + 0.4 * D * hash01(k, 5);
			std::vector<Vec3> pts;
			for (int q = 0; q <= 8; ++q) pts.push_back({r * std::cos(a) * (1.0 - 0.15 * q / 8.0) + 0.2 * std::sin(q + k), D + 4.0 - len * q / 8.0, r * std::sin(a) * (1.0 - 0.15 * q / 8.0)});
			deepsea::detail::add(roots, sdf::tubeMesh(pts, 0.04, 0.3, 5));
		}
		return out + attr(coated({0.25, 0.18, 0.12}, 0.8), emitMesh(roots, frame));
	}
	if (p.kind == "wreckroom") {   // (round 16) inside a wreck: a corridor of rusted plates to swim along, its length along +z of its frame
		const double L = p.length, W = 2.4, H = 2.2, wall = 0.15;
		std::snprintf(name, sizeof name, "wreckroom_%d_%d", static_cast<int>(L * 10), index);
		out += film_cache::part(film, name, coated({0.28, 0.16, 0.09}, 0.8), frame, [&] {
			const auto f = [L, W, H, wall](Vec3 q) {
				const double outer = sdf::roundBox(q, translate(0.0, -0.5 * H, 0.0), {0.5 * W + wall, 0.5 * H + wall, 0.5 * L}, 0.05);
				const double inner = sdf::roundBox(q, translate(0.0, -0.5 * H, 0.0), {0.5 * W, 0.5 * H, 0.5 * L + 1.0}, 0.03);   // (open at both ends: the doors)
				double holes = 1e9;
				for (double z = -0.5 * L + 1.0; z < 0.5 * L - 0.5; z += 1.5)   // portholes along both sides
					holes = std::min(holes, sdf::capsule(q, {-0.5 * W - 0.5, 1.35, z}, {0.5 * W + 0.5, 1.35, z}, 0.2));
				holes = std::min(holes, sdf::roundBox(q, translate(-0.3, -H, -0.15 * L), {0.5, 0.4, 0.9}, 0.1));   // (a hatch broken open in the deck overhead)
				return sdf::smax(sdf::smax(outer, -inner, 0.04), -holes, 0.03);
			};
			Mesh m = sdf::surfaceNets(f, {-0.5 * W - 0.4, -0.3, -0.5 * L - 0.2}, {0.5 * W + 0.4, H + 0.4, 0.5 * L + 0.2}, 0.07);
			sdf::computeNormals(m);
			return m;
		});
		Mesh debris;   // pipes along the ceiling, crates fallen over, a beam down across the floor
		for (const double side : {-1.0, 1.0}) deepsea::detail::add(debris, sdf::tubeMesh({{side * 0.9, H - 0.2, -0.5 * L}, {side * 0.9, H - 0.2, 0.5 * L}}, 0.06, 1.0, 8));
		for (int k = 0; k < 5; ++k) {
			const double z = -0.4 * L + 0.8 * L * hash01(k, index + 3);
			deepsea::detail::add(debris, box(0.5, 0.4, 0.5), mul(translate((hash01(k, index + 4) - 0.5) * 1.4, 0.2, z), rotate({0, 1, 0}, 60.0 * hash01(k, index + 5)), rotate({0, 0, 1}, 15.0 * hash01(k, index + 6))));
		}
		deepsea::detail::add(debris, sdf::tubeMesh({{-1.1, 0.1, 0.1 * L}, {1.0, 0.9, 0.18 * L}}, 0.1, 1.0, 6));
		out += attr(coated({0.22, 0.14, 0.09}, 0.85), emitMesh(debris, frame));
		out += attr(diffuseMaterial({0.42, 0.38, 0.3}), emitMesh(box(W, 0.06, L), mul(frame, translate(0.0, 0.03, 0.0))));   // (the floor deep in silt)
		return out;
	}
	// a reef wall: a slab of rock rising sheer, its face crowded with growth
	const double L = p.length, H = p.height;
	std::snprintf(name, sizeof name, "reefwall_%d_%d_%d", static_cast<int>(L * 10), static_cast<int>(H * 10), index);
	out += film_cache::part(film, name, rock, frame, [&] {
		const auto f = [L, H](Vec3 q) { return sdf::roundBox(q, translate(0.0, -0.5 * H, 2.0), {0.5 * L, 0.5 * H, 2.0 + 0.6 * std::sin(q.x * 0.4) + 0.4 * std::sin(q.y * 0.7)}, 0.8); };
		return rockMesh(f, {-0.5 * L - 3.0, -1.0, -2.5}, {0.5 * L + 3.0, H + 2.5, 7.0}, std::max(0.25, H / 60.0), 29, 0.7);
	});
	Mesh fans, sponges;   // sea fans and sponges on its face (the face is towards -z in its frame)
	const int n = static_cast<int>(L * H / 6.0);
	for (int k = 0; k < n; ++k) {
		const double x = (hash01(k, 1) - 0.5) * L, yy = 0.5 + (H - 1.0) * hash01(k, 2), s = 0.3 + 0.7 * hash01(k, 3);
		if (k % 3 == 0) {   // a fan: a lacy flat spread, its lattice as a mesh of thin branches
			const Vec3 root{x, yy, -0.3};
			for (int b = 0; b < 7; ++b) {
				const double a = kPi * (0.15 + 0.7 * b / 6.0);
				deepsea::detail::add(fans, sdf::tubeMesh({root, root + Vec3{std::cos(a) * 0.4 * s, std::sin(a) * 0.5 * s, -0.25 * s}, root + Vec3{std::cos(a) * 0.9 * s, std::sin(a) * 1.0 * s, -0.4 * s}}, 0.012 * s, 0.4, 4));
			}
		} else {
			deepsea::detail::add(sponges, lathe({{0.1 * s, 0.0}, {0.14 * s, 0.4 * s}, {0.1 * s, 0.4 * s}, {0.08 * s, 0.32 * s}}, 10), alignY({x, yy, -0.1}, normalize(Vec3{0.0, 0.4, -1.0})));
		}
	}
	sponges.uvs.clear();
	return out + attr(coated({0.75, 0.3, 0.35}, 0.5), emitMesh(fans, frame)) + attr(coated({0.85, 0.55, 0.2}, 0.6), emitMesh(sponges, frame));
}

// All of it at t (`eye` pbrt's, for the garden eels; `flow` the current now, pbrt's).
inline std::string text(const std::string &film, const World &w, double t, const std::function<double(double, double)> &groundAt, const Vec3 &flow, const Vec3 &eye) {
	if (!w.any()) return std::string();
	std::string out;
	for (const Patch &p : w.patches) {
		if (p.kind == "anemones") out += anemoneText(p, t, groundAt, flow);
		else if (p.kind == "seagrass") out += seagrassText(p, t, groundAt, flow);
		else if (p.kind == "crabs") out += crabText(p, t, groundAt);
		else if (p.kind == "gardeneels") out += gardenEelText(p, t, groundAt, flow, eye);
		else out += starsAndUrchinsText(p, t, groundAt);
	}
	for (std::size_t i = 0; i < w.places.size(); ++i) out += placeText(film, w.places[i], static_cast<int>(i), groundAt);
	return out;
}

}  // namespace seabed
}  // namespace cartoon
