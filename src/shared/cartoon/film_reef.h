#pragma once
// film_reef.h -- reefs, small creatures, night dives and a kelp forest, round 15:
//
//   reef N in X0 Z0 X1 Z1 [bleached] [fluoresce] [seed K]
//            a coral reef: staghorn (antlers of branches, pale polyps at their tips), table corals (a broad plate on a stalk), brain corals (a dome grooved in meanders),
//            sea fans (a lattice spread across the current) and soft corals (fleshy branches tufted with polyps, swaying); `bleached`: all of it white, as a reef
//            dying in warm water; `fluoresce`: the polyps and tips glow green and orange (lighting nothing) - film it at night with a blue lamp (`underwater lamp 1 blue`)
//   critters nudibranchs|seahorses|shrimp|mantisshrimp N in X0 Z0 X1 Z1 [seed K]
//            the small things a macro lens is for: nudibranchs (bright slugs with frilled edges, horns and a plume of gills, creeping), seahorses (tails curled round a
//            stalk of grass, a fin fluttering on the back), cleaner shrimp (red with a white stripe, long white feelers waving) and mantis shrimps (in a burrow, eyes on
//            stalks turning each its own way)
//   nightdive [glow G] [spawn T0 T1]
//            night under the sea (with a night `sky`): the plankton lights up blue round anything that moves - a diver's fins, a school, the swimmers - fading over a
//            second; from T0 to T1 the corals spawn, bundles rising from them like snow falling upwards; the reef fish sleep, hardly moving
//   kelpforest N in X0 Z0 X1 Z1 [seed K]
//            giant kelp: N stalks from holdfasts on the seabed all the way up to the surface, a gas bladder at each blade, and the last metres lying along the surface in a
//            canopy that shades the water under it, the light coming down between in beams; it all sways with the swell and the current
//
// Coordinates: pbrt's (x, z) in the film file, on the seabed there; meshes in story space (x mirrored).

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

namespace cartoon {
namespace reef {

struct Reef {
	int count = 40, seed = 1;
	double x0 = -6.0, z0 = -6.0, x1 = 6.0, z1 = 6.0;
	bool bleached = false, fluoresce = false;
};
struct Critters {
	std::string kind;
	int count = 4, seed = 1;
	double x0 = -1.0, z0 = -1.0, x1 = 1.0, z1 = 1.0;
};
struct Night {
	bool on = false;
	double glow = 1.0, spawn0 = -1.0, spawn1 = -1.0;
};
struct KelpForest {
	int count = 30, seed = 1;
	double x0 = -10.0, z0 = -10.0, x1 = 10.0, z1 = 10.0;
};
struct World {
	std::vector<Reef> reefs;
	std::vector<Critters> critters;
	std::vector<KelpForest> kelp;
	Night night;
	bool any() const { return !reefs.empty() || !critters.empty() || !kelp.empty() || night.on; }
};
inline const std::vector<std::string> &critterKinds() {
	static const std::vector<std::string> k = {"nudibranchs", "seahorses", "shrimp", "mantisshrimp"};
	return k;
}

namespace detail {
using deepsea::detail::add;
using deepsea::detail::beads;
using deepsea::detail::cone;
using deepsea::detail::glowMaterial;
using sealife::detail::story;
// A coral piece's foot (pbrt's) and the kind it is: 0 staghorn, 1 table, 2 brain, 3 fan, 4 soft.
inline Vec3 footOf(const Reef &r, int i, const std::function<double(double, double)> &groundAt, int &kind) {
	const int s = r.seed * 911 + i;
	const double x = r.x0 + (r.x1 - r.x0) * hash01(s, 1), z = r.z0 + (r.z1 - r.z0) * hash01(s, 2), u = hash01(s, 3);
	kind = u < 0.3 ? 0 : u < 0.45 ? 1 : u < 0.65 ? 2 : u < 0.75 ? 3 : 4;
	return {x, groundAt(x, z), z};
}
}  // namespace detail

// ---- the reef ----
// A reef at t (in the current's `flow`, pbrt's): its corals in their colours (or bleached), their polyps; at night spawning between spawn0 and spawn1.
// The corals that do not move (staghorn, table, brain) and their polyps are made once, as PLY files named by the film and the reef's `index` (film_cache.h); the fans and
// soft corals sway in every frame.
inline std::string reefText(const std::string &film, const Reef &r, int index, double t, const std::function<double(double, double)> &groundAt, const Vec3 &flow) {
	static const Vec3 tones[5] = {{0.62, 0.5, 0.36}, {0.35, 0.45, 0.3}, {0.72, 0.62, 0.32}, {0.7, 0.2, 0.3}, {0.85, 0.45, 0.55}};
	static const Vec3 tips[3] = {{0.55, 0.3, 0.75}, {0.35, 0.6, 0.9}, {0.95, 0.85, 0.6}};
	const Vec3 white{0.92, 0.92, 0.9};
	Mesh pieces[5];
	std::vector<Vec3> polypDots, softDots;   // (the still corals' polyps, and the swaying soft corals')
	for (int i = 0; i < r.count; ++i) {
		int kind = 0;
		const Vec3 foot = detail::footOf(r, i, groundAt, kind);
		const int s = r.seed * 613 + i;
		const double size = 0.6 + 0.8 * hash01(s, 4), turn = 360.0 * hash01(s, 5);
		const Mat4 place = mul(translate(detail::story(foot)), rotate({0, 1, 0}, turn));
		Mesh m;
		if (kind == 0) {   // staghorn: forking antlers, the tips pale
			std::function<void(Vec3, Vec3, double, double, int)> grow = [&](Vec3 a, Vec3 d, double len, double rad, int depth) {
				const Vec3 b = a + d * len;
				detail::add(m, sdf::tubeMesh({a, a + d * (0.5 * len) + Vec3{0.02 * std::sin(depth + i), 0.0, 0.0}, b}, rad, depth == 0 ? 0.5 : 0.85, 6));
				if (depth == 0) {
					polypDots.push_back(transformPoint(place, b));
					return;
				}
				for (int k = 0; k < 2; ++k) {
					const double a2 = 2.0 * kPi * hash01(s * 7 + depth * 13 + k, 6) + k * kPi;
					grow(b, normalize(d + Vec3{0.55 * std::cos(a2), 0.25, 0.55 * std::sin(a2)}), len * 0.8, rad * 0.78, depth - 1);
				}
			};
			for (int k = 0; k < 3; ++k) {
				const double a = 2.0 * kPi * k / 3.0 + hash01(s, 7);
				grow({0, 0, 0}, normalize(Vec3{0.45 * std::cos(a), 1.0, 0.45 * std::sin(a)}), 0.16 * size, 0.022 * size, 4);
			}
		} else if (kind == 1) {   // table: a stalk and a broad plate, its rim a little wavy
			detail::add(m, sdf::tubeMesh({{0, 0, 0}, {0, 0.2 * size, 0}, {0.02, 0.35 * size, 0.0}}, 0.05 * size, 0.8, 8));
			Mesh plate = lathe({{0.0, 0.36 * size}, {0.6 * size, 0.38 * size}, {0.7 * size, 0.4 * size}, {0.68 * size, 0.37 * size}, {0.0, 0.33 * size}}, 36);
			plate.uvs.clear();
			for (Vec3 &v : plate.verts) v.y += 0.02 * size * std::sin(6.0 * std::atan2(v.z, v.x)) * std::hypot(v.x, v.z) / (0.7 * size);
			detail::add(m, plate);
			for (int k = 0; k < 40; ++k) {
				const double a = 2.0 * kPi * hash01(k, s), rr = 0.65 * size * std::sqrt(hash01(k, s + 1));
				polypDots.push_back(transformPoint(place, {rr * std::cos(a), 0.41 * size, rr * std::sin(a)}));
			}
		} else if (kind == 2) {   // brain: a dome grooved in meanders
			Mesh dome = lathe([&] {
				std::vector<std::pair<double, double>> p;
				for (int k = 0; k <= 20; ++k) {
					const double a = 0.5 * kPi * k / 20.0;
					p.push_back({0.4 * size * std::cos(a) + (k == 20 ? 0.0 : 1e-4), 0.32 * size * std::sin(a)});
				}
				std::reverse(p.begin(), p.end());
				return p;
			}(), 64);
			dome.uvs.clear();
			for (std::size_t q = 0; q < dome.verts.size(); ++q) {
				Vec3 &v = dome.verts[q];
				const double w = std::sin(26.0 * v.x / size + 3.0 * nature::noise2(v.x * 4.0 / size, v.z * 4.0 / size, s)) * std::sin(26.0 * v.z / size + 2.0 * nature::noise2(v.z * 3.0 / size, v.x * 3.0 / size, s + 1));
				v = v - dome.normals[q] * (0.025 * size * (std::abs(w) < 0.25 ? 1.0 : 0.0));
			}
			sdf::computeNormals(dome);
			detail::add(m, dome);
		} else if (kind == 3) {   // a sea fan: a lattice spread across the current
			const Vec3 across = normalize(Vec3{flow.z + 1e-6, 0.0, -flow.x});
			for (int b = 0; b < 9; ++b) {
				const double a = kPi * (0.12 + 0.76 * b / 8.0);
				const Vec3 dir = normalize(Vec3{0.0, std::sin(a), 0.0} + across * std::cos(a));
				std::vector<Vec3> line;
				for (int q = 0; q <= 5; ++q) line.push_back(dir * (0.18 * size * q) + Vec3{0.0, 0.0, 0.02 * std::sin(q + b + 0.6 * t)});
				detail::add(m, sdf::tubeMesh(line, 0.007 * size, 0.5, 4));
			}
			for (int ring = 1; ring <= 4; ++ring) {   // the cross-links
				std::vector<Vec3> arc;
				for (int q = 0; q <= 12; ++q) {
					const double a = kPi * (0.12 + 0.76 * q / 12.0);
					arc.push_back((Vec3{0.0, std::sin(a), 0.0} + across * std::cos(a)) * (0.2 * size * ring) + Vec3{0.0, 0.0, 0.02 * std::sin(q + ring + 0.6 * t)});
				}
				detail::add(m, sdf::tubeMesh(arc, 0.004 * size, 1.0, 4));
			}
		} else {   // soft coral: fleshy branches, swaying, tufted with polyps
			for (int b = 0; b < 9; ++b) {
				const double a = 2.0 * kPi * hash01(b, s), lean = 0.3 + 0.3 * hash01(b, s + 1), h = (0.25 + 0.2 * hash01(b, s + 2)) * size;
				const Vec3 sway = Vec3{-flow.x, 0.0, flow.z} * 0.5 + Vec3{0.06 * std::sin(1.1 * t + b), 0.0, 0.05 * std::cos(0.9 * t + b)};
				std::vector<Vec3> line;
				for (int q = 0; q <= 6; ++q) {
					const double u = q / 6.0;
					line.push_back(Vec3{lean * std::cos(a) * h * u, h * u, lean * std::sin(a) * h * u} + sway * (u * u));
				}
				detail::add(m, sdf::tubeMesh(line, 0.03 * size, 0.4, 7));
				for (int k = 0; k < 6; ++k) softDots.push_back(transformPoint(place, line[4 + k % 3] + Vec3{0.02 * (hash01(k, b + s) - 0.5), 0.01, 0.02 * (hash01(k + 1, b + s) - 0.5)} * size));
			}
		}
		detail::add(pieces[kind], m, place);
		(void)tips;
	}
	std::string out = "# a coral reef (film_reef.h)\n";
	char name[96];
	for (int k = 0; k < 5; ++k) {
		if (pieces[k].tris.empty()) continue;
		const std::string material = k == 4 ? translucentMaterial(r.bleached ? white : tones[k], (r.bleached ? white : tones[k]) * 0.4) : coated(r.bleached ? white : tones[k], 0.5);
		std::snprintf(name, sizeof name, "reef_%d_%d_%d_%d_%d_%d_%d", index, r.seed, r.count, static_cast<int>(std::lround(r.x0 * 10 + r.z1 * 7)), static_cast<int>(std::lround(r.x1 * 10 - r.z0 * 3)), k, r.bleached ? 1 : 0);
		out += k < 3 ? film_cache::part(film, name, material, identity(), [&] { return pieces[k]; }) : attr(material, emitMesh(pieces[k], identity()));
	}
	const std::string polypMaterial = r.fluoresce ? detail::glowMaterial({0.25, 1.4, 0.45}) : coated(r.bleached ? white : tips[r.seed % 3], 0.4);
	if (!polypDots.empty()) {
		std::snprintf(name, sizeof name, "reef_%d_%d_%d_%d_%d_polyps", index, r.seed, r.count, static_cast<int>(std::lround(r.x0 * 10 + r.z1 * 7)), static_cast<int>(std::lround(r.x1 * 10 - r.z0 * 3)));
		out += film_cache::part(film, name, polypMaterial, identity(), [&] { return detail::beads(polypDots, 0.008); });
	}
	if (!softDots.empty()) out += attr(polypMaterial, emitMesh(detail::beads(softDots, 0.008), identity()));
	return out;
}
// Coral spawning at t (from spawn0 to spawn1): bundles rising from the reef's corals, slowly, drifting (`drift(t)`: how far the current has carried the water).
inline std::string spawnText(const std::vector<Reef> &reefs, const std::vector<underwater::Coral> &corals, double t, double t0, double t1,
							 const std::function<double(double, double)> &groundAt, const underwater::Current &c) {
	if (t < t0) return std::string();
	std::vector<Vec3> feet;
	for (const Reef &r : reefs)
		for (int i = 0; i < r.count; ++i) {
			int kind = 0;
			feet.push_back(detail::footOf(r, i, groundAt, kind) + Vec3{0.0, 0.25, 0.0});
		}
	for (const underwater::Coral &co : corals)
		for (int i = 0; i < co.count; ++i) {
			const int s = co.seed * 613 + i;
			const double x = co.x0 + (co.x1 - co.x0) * hash01(s, 1), z = co.z0 + (co.z1 - co.z0) * hash01(s, 2);
			feet.push_back({x, groundAt(x, z) + 0.3, z});
		}
	if (feet.empty()) return std::string();
	std::string out = "# coral spawning (film_reef.h)\nAttributeBegin\n  " + coated({0.95, 0.55, 0.6}, 0.3) + "\n";
	char buf[200];
	const int per = std::max(1, 3000 / static_cast<int>(feet.size()));
	for (std::size_t f = 0; f < feet.size(); ++f)
		for (int k = 0; k < per; ++k) {
			const int key = static_cast<int>(f) * 7919 + k;
			const double born = t0 + (t1 - t0) * hash01(key, 1);
			const double age = t - born;
			if (age < 0.0 || age > 40.0) continue;
			const Vec3 drift = c.drift(t) - c.drift(born);
			const Vec3 p = feet[f] + Vec3{0.1 * std::sin(0.7 * age + key) + 0.05 * (hash01(key, 2) - 0.5) + drift.x, (0.06 + 0.04 * hash01(key, 3)) * age, 0.1 * std::cos(0.6 * age + key) + drift.z};
			std::snprintf(buf, sizeof buf, "  AttributeBegin\n    Translate %.4f %.4f %.4f\n    Shape \"sphere\" \"float radius\" [ %.4f ]\n  AttributeEnd\n", p.x, p.y, p.z, 0.0025 + 0.0015 * hash01(key, 4));
			out += buf;
		}
	return out + "AttributeEnd\n";
}

// ---- the small creatures ----
inline std::string crittersText(const Critters &c, double t, const std::function<double(double, double)> &groundAt) {
	std::string out = "# " + c.kind + " (film_reef.h)\n";
	Mesh a, b, d;   // (each kind's two or three colours)
	for (int i = 0; i < c.count; ++i) {
		const int s = c.seed * 433 + i;
		double x = c.x0 + (c.x1 - c.x0) * hash01(s, 1), z = c.z0 + (c.z1 - c.z0) * hash01(s, 2);
		const double heading = 360.0 * hash01(s, 3);
		if (c.kind == "nudibranchs") {   // creeping 3 mm a second along its heading
			x += 0.003 * t * std::sin(heading * kPi / 180.0), z += 0.003 * t * std::cos(heading * kPi / 180.0);
			const double L = 0.04 + 0.03 * hash01(s, 4);
			const Mat4 place = mul(translate(detail::story({x, groundAt(x, z) + 0.004, z})), rotate({0, 1, 0}, heading));
			Mesh body = sealife::detail::body(L, 0.55, 0.3, 0.0, {{0.0, -0.5}, {0.1, -0.35}, {0.14, -0.05}, {0.13, 0.25}, {0.09, 0.42}, {0.0, 0.5}});
			for (Vec3 &v : body.verts) v.y = std::max(v.y, -0.01 * L) + 0.03 * L;   // (flat underneath)
			Mesh trim, spots;   // (round 16: each in one of five patterns - a body colour, its frill, horns and gills in another, and spots on some)
			Mesh &frill = trim;
			for (const double side : {-1.0, 1.0})
				for (int q = 0; q < 16; ++q) {
					const double z0 = (-0.42 + 0.84 * q / 16.0) * L, z1 = (-0.42 + 0.84 * (q + 1) / 16.0) * L;
					const auto hw = [&](double zz) { return 0.16 * L * std::sqrt(std::max(0.0, 1.0 - std::pow(zz / (0.46 * L), 2.0))); };
					const auto wave = [&](double zz) { return 0.012 * L * std::sin(zz / L * 40.0 + 2.0 * t); };
					sealife::detail::fin(frill, {side * hw(z0) * 0.9, 0.035 * L, z0}, {side * (hw(z0) + 0.06 * L), 0.035 * L + wave(z0), z0}, {side * hw(z1) * 0.9, 0.035 * L, z1});
					sealife::detail::fin(frill, {side * hw(z1) * 0.9, 0.035 * L, z1}, {side * (hw(z0) + 0.06 * L), 0.035 * L + wave(z0), z0}, {side * (hw(z1) + 0.06 * L), 0.035 * L + wave(z1), z1});
				}
			for (const double side : {-1.0, 1.0}) detail::add(trim, detail::cone(0.012 * L, 0.18 * L, 6), alignY({side * 0.05 * L, 0.08 * L, 0.36 * L}, {side * 0.3, 1.0, 0.4}));
			for (int g = 0; g < 7; ++g) {   // the gill plume
				const double ga = 2.0 * kPi * g / 7.0;
				detail::add(trim, sdf::tubeMesh({{0.0, 0.08 * L, -0.28 * L}, {0.06 * L * std::cos(ga), 0.2 * L, -0.28 * L + 0.06 * L * std::sin(ga)}}, 0.01 * L, 0.3, 4));
			}
			static const Vec3 bodies[5] = {{0.45, 0.12, 0.6}, {0.08, 0.15, 0.55}, {0.92, 0.9, 0.86}, {0.04, 0.04, 0.05}, {0.95, 0.4, 0.05}};
			static const Vec3 trims[5] = {{0.95, 0.5, 0.05}, {0.95, 0.85, 0.1}, {0.95, 0.55, 0.1}, {0.95, 0.85, 0.15}, {0.25, 0.1, 0.55}};
			const int scheme = static_cast<int>(hash01(s, 9) * 5.0) % 5;
			if (scheme != 0 && scheme != 4) {   // spots over its back (white, yellow, or the trim's colour)
				std::vector<Vec3> dots;
				for (int q = 0; q < 26; ++q) dots.push_back({(hash01(q, s + 1) - 0.5) * 0.18 * L, 0.06 * L, (hash01(q, s + 2) - 0.5) * 0.75 * L});
				spots = detail::beads(dots, 0.012 * L);
			}
			out += attr(coated(bodies[scheme], 0.2), emitMesh(body, place)) + attr(coated(trims[scheme], 0.2), emitMesh(trim, place));
			if (!spots.tris.empty()) out += attr(coated(scheme == 3 ? trims[scheme] : Vec3{0.95, 0.95, 0.92}, 0.2), emitMesh(spots, place));
		} else if (c.kind == "seahorses") {   // upright, its tail curled round a stalk of grass, bobbing, the fin on its back a blur
			const double H = 0.15 + 0.05 * hash01(s, 4), y = groundAt(x, z), bob = 0.01 * std::sin(0.8 * t + s);
			const Mat4 place = mul(translate(detail::story({x, y, z})), rotate({0, 1, 0}, heading));
			detail::add(d, sdf::tubeMesh({{0.03 * H, 0.0, 0.0}, {0.03 * H, 2.0 * H, 0.0}}, 0.012 * H, 1.0, 5), place);   // (the stalk)
			std::vector<Vec3> spine;
			for (int q = 0; q <= 16; ++q) {   // the tail curled round the stalk at the foot, the body rising in an S, the head bent forward
				const double u = q / 16.0;
				if (u < 0.35) {
					const double a = u / 0.35 * 4.0 * kPi;
					spine.push_back({0.03 * H + 0.05 * H * std::cos(a), 0.25 * H + 0.12 * H * u / 0.35, 0.05 * H * std::sin(a)});
				} else {
					const double w = (u - 0.35) / 0.65;
					spine.push_back({0.06 * H + 0.12 * H * std::sin(w * kPi * 0.9), 0.37 * H + 0.6 * H * w + bob, 0.06 * H * std::sin(w * 3.0)});
				}
			}
			// a thin curled tail, a deep ridged belly, a neck, and the head turned forward with its long snout and a crown on top
			detail::add(a, sdf::tubeMesh(std::vector<Vec3>(spine.begin(), spine.begin() + 7), 0.022 * H, 0.35, 8), place);
			detail::add(a, sdf::tubeMesh(std::vector<Vec3>(spine.begin() + 6, spine.begin() + 14), 0.075 * H, 0.45, 10), place);
			for (int r = 7; r < 13; ++r) detail::add(a, torus(0.075 * H * (1.0 - 0.05 * (r - 7)), 0.008 * H, 14, 4), mul(place, alignY(spine[static_cast<std::size_t>(r)], normalize(spine[r + 1] - spine[r - 1]))));
			const Vec3 head = spine.back() + Vec3{0.02 * H, 0.03 * H, 0.0};
			detail::add(a, sdf::tubeMesh({spine[13], spine[15], head}, 0.04 * H, 1.0, 8), place);   // (the neck)
			detail::add(a, uvSphere(1.0, 10, 8), mul(place, translate(head), scale(0.07 * H, 0.055 * H, 0.05 * H)));
			detail::add(a, sdf::tubeMesh({head, head + Vec3{0.17 * H, -0.06 * H, 0.0}}, 0.018 * H, 0.8, 6), place);   // (the snout)
			detail::add(a, detail::cone(0.02 * H, 0.06 * H, 6), mul(place, translate(head + Vec3{-0.01 * H, 0.04 * H, 0.0})));   // (the crown)
			for (const double side : {-1.0, 1.0}) detail::add(b, uvSphere(0.012 * H, 6, 4), mul(place, translate(head + Vec3{0.02 * H, 0.01 * H, side * 0.045 * H})));   // (its eyes)
			const double flutter = std::sin(t * 60.0 + s);   // (a fin beating too fast to see: drawn spread)
			sealife::detail::fin(b, spine[11] + Vec3{-0.05 * H, 0.0, 0.0}, spine[13] + Vec3{-0.12 * H, 0.0, 0.02 * H * flutter}, spine[12] + Vec3{-0.06 * H, 0.04 * H, 0.0});
		} else if (c.kind == "shrimp") {   // a cleaner shrimp: red, a white stripe down the back, long white feelers waving
			const double L = 0.05 + 0.02 * hash01(s, 4);
			const Mat4 place = mul(translate(detail::story({x, groundAt(x, z) + 0.008, z})), rotate({0, 1, 0}, heading));
			std::vector<Vec3> spine;
			for (int q = 0; q <= 8; ++q) spine.push_back({0.0, 0.1 * L * std::sin(q / 8.0 * kPi), (0.5 - q / 8.0) * L});
			detail::add(a, sdf::tubeMesh(spine, 0.11 * L, 0.35, 8), place);
			std::vector<Vec3> stripe;
			for (const Vec3 &p : spine) stripe.push_back(p + Vec3{0.0, 0.1 * L, 0.0});
			detail::add(b, sdf::tubeMesh(stripe, 0.035 * L, 0.5, 5), place);
			for (int f = 0; f < 4; ++f) {   // feelers
				const double side = f % 2 ? 1.0 : -1.0, wave = std::sin(3.0 * t + f + s);
				std::vector<Vec3> line;
				for (int q = 0; q <= 8; ++q) {
					const double u = q / 8.0;
					line.push_back({side * (0.05 + 0.4 * u) * L, 0.1 * L + 0.4 * L * u * (f < 2 ? 1.0 : 0.3) + 0.1 * L * wave * u, (0.5 + 1.4 * u) * L});
				}
				detail::add(b, sdf::tubeMesh(line, 0.006 * L, 0.5, 3), place);
			}
			for (int k = 0; k < 5; ++k)
				for (const double side : {-1.0, 1.0}) detail::add(a, sdf::tubeMesh({{side * 0.05 * L, 0.0, (0.25 - 0.1 * k) * L}, {side * 0.18 * L, -0.08 * L, (0.27 - 0.1 * k) * L}}, 0.012 * L, 0.6, 3), place);
		} else {   // a mantis shrimp half out of its burrow, eyes on stalks each turning its own way
			const double L = 0.1 + 0.05 * hash01(s, 4);
			const double y = groundAt(x, z);
			const Mat4 place = mul(translate(detail::story({x, y, z})), rotate({0, 1, 0}, heading));
			detail::add(d, lathe({{0.0, 0.001}, {0.09 * L * 10.0 * 0.1, 0.002}, {0.14 * L * 10.0 * 0.1, 0.0}}, 16), place);   // (the burrow's mouth)
			std::vector<Vec3> spine;
			for (int q = 0; q <= 8; ++q) spine.push_back({0.0, 0.05 * L + 0.04 * L * (q / 8.0), (0.15 - 0.6 * q / 8.0) * L});
			detail::add(a, sdf::tubeMesh(spine, 0.12 * L, 0.6, 8), place);
			for (const double side : {-1.0, 1.0}) {
				const double look = 40.0 * std::sin(1.3 * t + side * 2.0 + s);   // (each eye its own way)
				const Vec3 base{side * 0.04 * L, 0.12 * L, 0.18 * L}, eye = base + Vec3{side * 0.03 * L + 0.04 * L * std::sin(look * kPi / 180.0), 0.1 * L, 0.03 * L * std::cos(look * kPi / 180.0)};
				detail::add(a, sdf::tubeMesh({base, eye}, 0.012 * L, 1.0, 4), place);
				detail::add(b, uvSphere(0.03 * L, 8, 6), mul(place, translate(eye)));
				detail::add(d, sdf::tubeMesh({{side * 0.08 * L, 0.06 * L, 0.12 * L}, {side * 0.12 * L, 0.12 * L, 0.25 * L}, {side * 0.06 * L, 0.06 * L, 0.3 * L}}, 0.025 * L, 0.7, 5), place);   // (the folded claws)
			}
		}
	}
	const bool nudi = c.kind == "nudibranchs", horse = c.kind == "seahorses", shrimp = c.kind == "shrimp";
	const Vec3 colA = nudi ? Vec3{0.45, 0.12, 0.6} : horse ? Vec3{0.85, 0.6, 0.12} : shrimp ? Vec3{0.8, 0.08, 0.06} : Vec3{0.2, 0.55, 0.3};
	const Vec3 colB = nudi ? Vec3{0.95, 0.5, 0.05} : horse ? Vec3{0.9, 0.75, 0.4} : shrimp ? Vec3{0.95, 0.95, 0.92} : Vec3{0.15, 0.3, 0.8};
	const Vec3 colD = horse ? Vec3{0.25, 0.45, 0.15} : shrimp ? Vec3{0.8, 0.08, 0.06} : Vec3{0.85, 0.35, 0.1};
	if (!a.tris.empty()) out += attr(coated(colA, 0.25), emitMesh(a, identity()));
	if (!b.tris.empty()) out += attr(horse ? translucentMaterial(colB, colB * 0.5) : coated(colB, 0.25), emitMesh(b, identity()));
	if (!d.tris.empty()) out += attr(c.kind == "mantisshrimp" ? diffuseMaterial({0.03, 0.03, 0.03}) : coated(colD, 0.4), emitMesh(d, identity()));
	if (c.kind == "mantisshrimp") {   // (its folded claws in orange: drawn over the burrow's mouth in d, so a second pass for them is not worth it - they read as dark)
	}
	return out;
}

// ---- night: the water lighting up round what moves ----
// The plankton sparks round each path's last second (pbrt's points at times), in three steps of fading, as glows that light nothing.
inline std::string glowText(const std::vector<std::function<Vec3(double)>> &paths, double t, double glow) {
	std::vector<Vec3> fresh[3];
	for (std::size_t i = 0; i < paths.size(); ++i)
		for (int q = 0; q < 14; ++q) {
			const double age = (q + 0.5) / 14.0;
			const Vec3 p = paths[i](t - age);
			for (int k = 0; k < 2; ++k) {
				const int key = static_cast<int>(std::floor((t - age) * 14.0)) * 131 + static_cast<int>(i) * 7 + k;
				fresh[std::min(2, q / 5)].push_back(detail::story(p + Vec3{0.25 * (hash01(key, 1) - 0.5), 0.25 * (hash01(key, 2) - 0.5), 0.25 * (hash01(key, 3) - 0.5)}));
			}
		}
	std::string out;
	for (int k = 0; k < 3; ++k)
		if (!fresh[k].empty()) out += attr(detail::glowMaterial(Vec3{0.15, 0.5, 1.0} * (6.0 * glow / (1 + 2 * k))), emitMesh(detail::beads(fresh[k], 0.004), identity()));
	return out.empty() ? out : "# the plankton lighting up (film_reef.h)\n" + out;
}

// ---- the kelp forest ----
// A kelp forest at t (the surface at `level`, the swell's sway and the current's `flow`, pbrt's): stalks from the seabed to the surface, a blade a metre or so long every
// third of a metre with a bladder at its base, the
// canopy lying along the surface.
inline std::string kelpForestText(const KelpForest &k, double t, double level, const std::function<double(double, double)> &groundAt, const Vec3 &flow) {
	Mesh blades, stalks, bladders, holdfasts;
	const Vec3 drift = length(flow) > 1e-6 ? normalize(Vec3{flow.x, 0.0, flow.z}) : Vec3{0.0, 0.0, 1.0};
	for (int i = 0; i < k.count; ++i) {
		const int s = k.seed * 977 + i;
		const double x = k.x0 + (k.x1 - k.x0) * hash01(s, 1), z = k.z0 + (k.z1 - k.z0) * hash01(s, 2);
		const Vec3 root{x, groundAt(x, z), z};
		const double depth = std::max(1.0, level - 0.12 - root.y), canopy = 3.0 + 3.0 * hash01(s, 3), ph = 2.0 * kPi * hash01(s, 4);
		const Vec3 lay = normalize(drift + Vec3{0.5 * (hash01(s, 5) - 0.5), 0.0, 0.5 * (hash01(s, 6) - 0.5)});
		const Vec3 sway = Vec3{0.8 * std::sin(0.45 * t + ph + 0.05 * x), 0.0, 0.6 * std::cos(0.4 * t + ph)} + Vec3{flow.x, 0.0, flow.z} * 3.0;
		const double total = depth + canopy;
		const auto stalk = [&](double u) {   // (u 0 at the holdfast, 1 at the canopy's end; pbrt's)
			const double s2 = u * total;
			if (s2 <= depth) {
				const double v = s2 / depth;
				return root + Vec3{0.0, s2, 0.0} + sway * (v * v * 0.6) + Vec3{0.25 * std::sin(v * 7.0 + ph), 0.0, 0.0};
			}
			const Vec3 top = root + Vec3{0.0, depth, 0.0} + sway * 0.6 + Vec3{0.25 * std::sin(7.0 + ph), 0.0, 0.0};
			return top + lay * (s2 - depth) + Vec3{0.0, 0.03 * std::sin(2.0 * (s2 - depth) + t), 0.0};
		};
		const auto storyStalk = [&](double u) { return detail::story(stalk(u)); };
		nature::addStrip(stalks, storyStalk, [](double) { return 0.025; }, Vec3{1.0, 0.0, 0.0}, 30);
		detail::add(holdfasts, nature::rock(0.25, s), translate(detail::story(root)));
		const int n = static_cast<int>(total / 0.32);
		for (int b = 1; b < n; ++b) {   // a blade every half metre, a bladder at its base, alternating sides; flat on the surface in the canopy
			const double u = static_cast<double>(b) / n;
			const Vec3 at = stalk(u), along = normalize(stalk(std::min(1.0, u + 0.01)) - stalk(std::max(0.0, u - 0.01)));
			const bool flat = u * total > depth - 1.2;   // (in the canopy, and the blades just under it: they lie along the surface, not up through it)
			const Vec3 side = normalize(cross(along, Vec3{0.0, 1.0, 0.0}) + Vec3{1e-9, 0, 0}) * (b % 2 ? 1.0 : -1.0);
			const Vec3 dir = flat ? normalize(side + lay * 0.7) : normalize(side * 0.6 + along + Vec3{flow.x, 0.0, flow.z});
			const Vec3 tip = at + dir * 0.06;
			nature::addLeaf(blades, detail::story(tip), detail::story(dir), flat ? Vec3{0.0, 1.0, 0.0} : detail::story(normalize(cross(dir, along) + Vec3{0, 1e-9, 0})), 1.0 + 0.5 * hash01(s + b, 7),
							0.2);
			detail::add(bladders, uvSphere(0.025, 4, 3), translate(detail::story(at + dir * 0.03)));
		}
	}
	const std::string material = translucentMaterial({0.32, 0.25, 0.08}, {0.3, 0.24, 0.06});
	return "# a kelp forest (film_reef.h)\n" + attr(material, emitMesh(blades, identity())) + attr(material, emitMesh(stalks, identity())) +
		   attr(coated({0.4, 0.32, 0.1}, 0.3), emitMesh(bladders, identity())) + attr(coated({0.22, 0.17, 0.08}, 0.6), emitMesh(holdfasts, identity()));
}

// All of it at t (the flows pbrt's; `moving`: the paths that light the water up at night).
inline std::string text(const std::string &film, const World &w, const std::vector<underwater::Coral> &corals, double t, double level, const std::function<double(double, double)> &groundAt, const underwater::Current &c,
						const std::vector<std::function<Vec3(double)>> &moving) {
	if (!w.any()) return std::string();
	std::string out;
	const Vec3 flow = c.velocity(t);
	for (std::size_t i = 0; i < w.reefs.size(); ++i) out += reefText(film, w.reefs[i], static_cast<int>(i), t, groundAt, flow);
	for (const Critters &cr : w.critters) out += crittersText(cr, t, groundAt);
	for (const KelpForest &k : w.kelp) out += kelpForestText(k, t, level, groundAt, flow);
	if (w.night.on) {
		out += glowText(moving, t, w.night.glow);
		if (w.night.spawn1 > w.night.spawn0) out += spawnText(w.reefs, corals, t, w.night.spawn0, w.night.spawn1, groundAt, c);
	}
	return out;
}

}  // namespace reef
}  // namespace cartoon
