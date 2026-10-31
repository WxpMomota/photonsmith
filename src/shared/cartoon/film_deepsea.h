#pragma once
// film_deepsea.h -- animals under the sea, round 14: the deep's own lights, the octopus and the cuttlefish, and what flies through the open water.
//
//   deepsea anglerfish|lanternfish|combjellies|siphonophores N in X0 Y0 Z0 X1 Y1 Z1 [seed K]
//            anglerfish: a dark round fish with a gape of needle teeth hanging still, its lure glowing blue-white on the rod over its mouth (a small light: it lights its
//            own face); lanternfish: a loose school of small silver fish with rows of blue lights along their bellies (lighting nothing); comb jellies: clear ovals with eight
//            rows of beating combs that break the light into rainbows running down them (thin-film conductors, round 13's iridescence: they need a light - a lamp - to
//            show); siphonophores: a colony strung out on a stem tens of metres long, swimming bells at its head, glowing faintly blue all along
//   deepsea sparks
//            the water lights up blue where an animal of the deep swims through it, fading over a second behind it (plankton touched into light)
//   octopus X Y Z [toward A] [size S] [jet T]... [ink T]... [hide T] [seed K]
//            an octopus crawling over the seabed on its arms (each reaching and pulling in its own time, the tips curling); `hide T`: it stops, curls its arms in, its skin
//            turns the colour of the sand and rises in bumps; `jet T`: it blanches and shoots off backwards, mantle first, arms streaming behind, and settles again; `ink T`:
//            a cloud of ink left where it was, spreading and drifting with the current
//   cuttlefish N at X Y Z [radius R] [seed K]
//            cuttlefish hovering, the fin round their mantle rippling, dark bands passing over their backs (the "passing cloud" display)
//   swimmers mantas|eaglerays|stingrays|sealions|penguins|otters N at X Y Z [radius R] [rise T] [seed K]
//            mantas: great flat rays flying slowly on wings 4 m across, dark above and white below; eagle rays: smaller, faster, white-spotted, long tails; stingrays: lying
//            buried in the sand until `rise T` - then they lift off in a cloud of sand and glide away, their rims rippling; sea lions: looping and rolling, a few bubbles;
//            penguins: rocketing past on beating wings with trails of bubbles from their feathers; otters: floating on their backs at the surface, turning over now and then
//
// Coordinates: pbrt's in the film file (on a terrain, heights are above the ground there: lifted()); meshes in story space (x mirrored), as the toolkit's are.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "cartoon_lib.h"
#include "film_fire.h"
#include "film_macro.h"
#include "film_sealife.h"
#include "film_undersea.h"
#include "nature_lib.h"

namespace cartoon {
namespace deepsea {

struct Group {
	std::string kind;
	int count = 6, seed = 1;
	Vec3 lo, hi;
};
struct Octopus {
	Vec3 at;
	double toward = 0.0, size = 1.0, hide = -1.0;
	std::vector<double> jets, inks;
	int seed = 1;
};
struct Cuttle {
	Vec3 at;
	int count = 1, seed = 1;
	double radius = 1.5;
};
struct Swimmers {
	std::string kind;
	int count = 1, seed = 1;
	Vec3 at;
	double radius = 8.0, rise = -1.0;
};
struct Life {
	std::vector<Group> groups;
	bool sparks = false;
	std::vector<Octopus> octopuses;
	std::vector<Cuttle> cuttles;
	std::vector<Swimmers> swimmers;
	bool any() const { return !groups.empty() || !octopuses.empty() || !cuttles.empty() || !swimmers.empty(); }
};
inline const std::vector<std::string> &deepKinds() {
	static const std::vector<std::string> k = {"anglerfish", "lanternfish", "combjellies", "siphonophores"};
	return k;
}
inline const std::vector<std::string> &swimmerKinds() {
	static const std::vector<std::string> k = {"mantas", "eaglerays", "stingrays", "sealions", "penguins", "otters"};
	return k;
}

namespace detail {
using sealife::detail::facingOf;
using sealife::detail::fin;
using sealife::detail::story;
// `m` appended to `into` (their uvs kept only when both have them for every point).
inline void add(Mesh &into, const Mesh &m, const Mat4 &place = identity()) {
	const bool uv = (into.verts.empty() || into.uvs.size() == into.verts.size()) && m.uvs.size() == m.verts.size();
	if (!uv) into.uvs.clear();
	const int base = static_cast<int>(into.verts.size());
	const Mat4 inv = invertAffine(place);
	for (std::size_t i = 0; i < m.verts.size(); ++i) {
		into.verts.push_back(transformPoint(place, m.verts[i]));
		const Vec3 n = i < m.normals.size() ? m.normals[i] : Vec3{0, 1, 0};
		into.normals.push_back(normalize(Vec3{inv[0] * n.x + inv[4] * n.y + inv[8] * n.z, inv[1] * n.x + inv[5] * n.y + inv[9] * n.z, inv[2] * n.x + inv[6] * n.y + inv[10] * n.z}));
		if (uv) into.uvs.push_back(m.uvs[i]);
	}
	for (const auto &t : m.tris) into.tris.push_back({t[0] + base, t[1] + base, t[2] + base});
}
inline Mat4 placed(Vec3 at, Vec3 fwd, double roll = 0.0, double s = 1.0) { return mul(translate(story(at)), facingOf(story(fwd)), rotate({0, 0, 1}, roll), scale(s)); }
// A glow that lights nothing (never sampled): the deep's own lights, seen but not lighting what is round them.
inline std::string glowMaterial(Vec3 L) {
	char buf[220];
	std::snprintf(buf, sizeof buf, "AreaLightSource \"diffuse\" \"rgb L\" [ %.4f %.4f %.4f ] \"bool twosided\" true \"bool sampled\" false\n  Material \"diffuse\" \"rgb reflectance\" [ 0 0 0 ]", L.x, L.y, L.z);
	return buf;
}
// Little balls at `points` (story space), radius `r`, as one mesh.
inline Mesh beads(const std::vector<Vec3> &points, double r) {
	Mesh m;
	const Mesh b = uvSphere(1.0, 6, 4);
	for (const Vec3 &p : points) {
		const int base = static_cast<int>(m.verts.size());
		for (std::size_t i = 0; i < b.verts.size(); ++i) m.verts.push_back(p + b.verts[i] * r), m.normals.push_back(b.normals[i]);
		for (const auto &t : b.tris) m.tris.push_back({t[0] + base, t[1] + base, t[2] + base});
	}
	return m;
}
inline Mesh cone(double r, double len, int sides = 6) { return lathe({{r, 0.0}, {0.0, len}}, sides); }
}  // namespace detail

// ---- the deep ----
// Where member i of a group is at t (pbrt's) and the way it swims: a slow loop round a home in the box (anglers hang nearly still).
inline Vec3 groupAt(const Group &g, int i, double t, Vec3 *fwd = nullptr) {
	const int s = g.seed * 389 + i;
	const Vec3 home{g.lo.x + (g.hi.x - g.lo.x) * hash01(s, 1), g.lo.y + (g.hi.y - g.lo.y) * hash01(s, 2), g.lo.z + (g.hi.z - g.lo.z) * hash01(s, 3)};
	const double speed = g.kind == "anglerfish" ? 0.03 : g.kind == "lanternfish" ? 0.35 : g.kind == "combjellies" ? 0.05 : 0.04;
	const double R = g.kind == "lanternfish" ? 1.5 + 2.0 * hash01(s, 4) : 0.6 + 0.6 * hash01(s, 4), w = speed / R * (hash01(s, 5) < 0.5 ? -1.0 : 1.0), a = w * t + 6.2832 * hash01(s, 6);
	if (fwd) *fwd = Vec3{-std::sin(a) * w, 0.03 * std::cos(0.5 * a), std::cos(a) * w};
	return home + Vec3{R * std::cos(a), 0.25 * R * std::sin(0.7 * a + i), R * std::sin(a)};
}
// An anglerfish `L` long (forward +z): a round dark body, a gape of needle teeth, small eyes, the rod over its mouth (the lure is drawn by itself: anglerText).
inline Mesh anglerMesh(double L, double sway, Mesh &teeth, Mesh &mouth) {
	Mesh m = sealife::detail::body(L, 0.85, 0.95, sway, {{0.0, -0.5}, {0.05, -0.42}, {0.16, -0.2}, {0.27, 0.05}, {0.3, 0.22}, {0.24, 0.38}, {0.12, 0.47}, {0.0, 0.5}});
	detail::fin(m, {sway * 0.25 * L, 0.0, -0.45 * L}, {sway * 0.3 * L, 0.14 * L, -0.62 * L}, {sway * 0.3 * L, -0.14 * L, -0.62 * L});
	for (const double side : {-1.0, 1.0}) detail::fin(m, {side * 0.22 * L, -0.02 * L, 0.05 * L}, {side * 0.34 * L, -0.08 * L, -0.08 * L}, {side * 0.22 * L, -0.06 * L, -0.06 * L});
	detail::add(m, sdf::tubeMesh({{0.0, 0.26 * L, 0.2 * L}, {0.0, 0.42 * L, 0.3 * L}, {0.0, 0.47 * L, 0.45 * L}, {0.0, 0.42 * L, 0.6 * L}}, 0.008 * L, 0.6, 5));   // (the rod)
	for (const double side : {-1.0, 1.0}) detail::add(m, uvSphere(0.02 * L, 8, 6), translate(side * 0.12 * L, 0.13 * L, 0.33 * L));
	for (int k = 0; k < 18; ++k) {   // needle teeth round the gape, pointing in and forward
		const double a = 2.0 * kPi * k / 18.0, len = (0.04 + 0.04 * hash01(k, 3)) * L;
		const Vec3 base{0.12 * L * std::cos(a), 0.1 * L * std::sin(a) - 0.03 * L, 0.44 * L};
		detail::add(teeth, detail::cone(0.007 * L, len), alignY(base, normalize(Vec3{-std::cos(a), -std::sin(a), 0.9})));
	}
	detail::add(mouth, lathe({{0.0, 0.0}, {0.12 * L, 0.0}}, 18), alignY({0.0, -0.03 * L, 0.425 * L}, {0.0, 0.0, 1.0}, {0.0, 1.0, 0.0}));
	return m;
}
inline std::string anglerText(const Group &g, double t) {
	std::string out = "# anglerfish (film_deepsea.h)\n";
	for (int i = 0; i < g.count; ++i) {
		const int s = g.seed * 71 + i;
		Vec3 fwd;
		const Vec3 at = groupAt(g, i, t, &fwd);
		const double L = 0.3 + 0.25 * hash01(s, 1);
		Mesh teeth, mouth;
		const Mesh body = anglerMesh(L, 0.08 * std::sin(1.3 * t + s), teeth, mouth);
		const Mat4 place = detail::placed(at, fwd, 6.0 * std::sin(0.4 * t + s));
		out += attr(coated({0.06, 0.045, 0.045}, 0.45), emitMesh(body, place));
		out += attr(coated({0.82, 0.83, 0.78}, 0.1), emitMesh(teeth, place));
		out += attr(diffuseMaterial({0.01, 0.0, 0.0}), emitMesh(mouth, place));
		const double pulse = 0.75 + 0.25 * std::sin(2.0 * kPi * t / (2.5 + hash01(s, 2)) + s);
		const Vec3 lure = transformPoint(place, {0.0, 0.42 * L, 0.62 * L});
		out += emissiveSphere({lure.x, lure.y, lure.z}, 0.028 * L, Vec3{0.45, 0.85, 1.0} * (60.0 * pulse));   // (centre in story space)
	}
	return out;
}
// Lanternfish at t: their bodies (instanced: a pose of the tail beat each) and the rows of lights along them (one glowing mesh for the school).
inline std::string lanternText(const Group &g, double t) {
	std::string out = "# lanternfish (film_deepsea.h)\n";
	for (int k = 0; k < sealife::detail::kPoses; ++k)
		out += nature::objectBegin("deep_lantern_" + std::to_string(k)) + attr(metalMaterial({0.32, 0.36, 0.42}, 0.25), emitMesh(sealife::detail::fish(k, 0.09), identity())) + nature::objectEnd();
	std::vector<Vec3> lights;
	for (int i = 0; i < g.count; ++i) {
		Vec3 fwd;
		const Vec3 at = groupAt(g, i, t, &fwd);
		const int pose = static_cast<int>(std::floor((t * 5.0 + hash01(i, g.seed + 9)) * sealife::detail::kPoses)) % sealife::detail::kPoses;
		const Mat4 place = detail::placed(at, fwd);
		out += nature::placeObject("deep_lantern_" + std::to_string(pose), place);
		for (int q = 0; q < 7; ++q) {   // (a row along the belly and one along each flank)
			const double z = (-0.3 + 0.1 * q) * 0.09;
			lights.push_back(transformPoint(place, {0.0, -0.011, z}));
			for (const double side : {-1.0, 1.0}) lights.push_back(transformPoint(place, {side * 0.008, -0.006, z + 0.004}));
		}
	}
	return out + attr(detail::glowMaterial(Vec3{0.25, 0.6, 1.0} * 6.0), emitMesh(detail::beads(lights, 0.0016), identity()));
}
// Comb jellies at t: clear ovals, their eight comb rows rippling with rainbows running down them (each comb's film thickness in eight steps: a material each).
inline std::string combJellyText(const Group &g, double t) {
	std::string out = "# comb jellies (film_deepsea.h)\n";
	Mesh body, rows[8];
	for (int i = 0; i < g.count; ++i) {
		const int s = g.seed * 53 + i;
		Vec3 fwd;
		const Vec3 at = groupAt(g, i, t, &fwd);
		const double size = 0.08 + 0.12 * hash01(s, 1);
		const Mat4 place = mul(translate(detail::story(at)), rotate({0, 1, 0}, 40.0 * t * 0.1 + 360.0 * hash01(s, 2)), rotate({1, 0, 0}, 10.0 * std::sin(0.3 * t + s)), scale(size));
		detail::add(body, lathe({{0.0, -0.5}, {0.3, -0.42}, {0.42, -0.12}, {0.4, 0.2}, {0.26, 0.42}, {0.0, 0.5}}, 20), place);
		for (int r = 0; r < 8; ++r) {
			const double a = 2.0 * kPi * r / 8.0;
			for (int q = 0; q < 12; ++q) {
				const double v = -0.38 + 0.74 * q / 12.0, rad = 0.43 * std::cos(v * 2.4) + 0.02;
				const double wave = std::sin(2.0 * kPi * (2.5 * v - 1.6 * t) + r);   // (the combs beat in waves running down the row)
				const int band = std::clamp(static_cast<int>(std::floor((0.5 + 0.5 * wave) * 8.0)), 0, 7);
				const Vec3 c{rad * std::cos(a), v, rad * std::sin(a)}, out_{std::cos(a), 0.0, std::sin(a)};
				const Vec3 along{0.0, 0.05, 0.0}, side = Vec3{-std::sin(a), 0.0, std::cos(a)} * 0.03;
				Mesh plate;
				detail::fin(plate, c + out_ * 0.012 - side, c + out_ * (0.02 + 0.01 * wave) + along, c + out_ * 0.012 + side);
				detail::add(rows[band], plate, place);
			}
		}
	}
	out += attr("Material \"dielectric\" \"float eta\" [ 1.04 ]", undersea::detail::noShadow(emitMesh(body, identity())));
	for (int b = 0; b < 8; ++b)
		if (!rows[b].tris.empty()) out += attr(macro::detail::filmMetal({0.3, 0.3, 0.32}, 220.0 + 40.0 * b, 0.2), emitMesh(rows[b], identity()));
	return out;
}
// A siphonophore at t: a stem strung out along a gentle wave, swimming bells at its head, clusters every quarter metre glowing faintly blue with their tentacles hanging.
inline std::string siphonophoreText(const Group &g, double t) {
	std::string out = "# siphonophores (film_deepsea.h)\n";
	Mesh stems, bells, tentacles;
	std::vector<Vec3> glows;
	for (int i = 0; i < g.count; ++i) {
		const int s = g.seed * 211 + i;
		const double len = 6.0 + 14.0 * hash01(s, 1), ang = 2.0 * kPi * hash01(s, 2);
		const Vec3 dir = normalize(Vec3{std::cos(ang), 0.15 * (hash01(s, 3) - 0.5), std::sin(ang)}), side = normalize(cross(dir, Vec3{0, 1, 0}) + Vec3{1e-9, 0, 0});
		const Vec3 head = Vec3{g.lo.x + (g.hi.x - g.lo.x) * hash01(s, 4), g.lo.y + (g.hi.y - g.lo.y) * hash01(s, 5), g.lo.z + (g.hi.z - g.lo.z) * hash01(s, 6)} + dir * (0.05 * t);
		const auto along = [&](double u) { return head - dir * (u * len) + side * (0.5 * std::sin(2.0 * kPi * 3.0 * u - 0.4 * t)) + Vec3{0.0, 0.35 * std::sin(2.0 * kPi * 2.0 * u + 0.3 * t), 0.0}; };
		std::vector<Vec3> line;
		for (int q = 0; q <= 120; ++q) line.push_back(detail::story(along(q / 120.0)));
		detail::add(stems, sdf::tubeMesh(line, 0.005, 1.0, 5));
		for (int b = 0; b < 6; ++b) {   // the swimming bells, pulsing
			const double pulse = 1.0 - 0.15 * std::max(0.0, std::sin(2.0 * kPi * t / 1.1 + b));
			const Vec3 p = along(-0.004 * b) + side * (0.04 * (b % 2 ? 1.0 : -1.0));
			detail::add(bells, lathe({{0.0, 0.05}, {0.035 * pulse, 0.04}, {0.04 * pulse, 0.0}, {0.03 * pulse, -0.03}}, 10), alignY(detail::story(p), detail::story(dir)));
		}
		for (double u = 0.03; u < 1.0; u += 0.25 / len) {
			const Vec3 p = along(u);
			glows.push_back(detail::story(p));
			std::vector<Vec3> hang;
			for (int q = 0; q <= 5; ++q) hang.push_back(detail::story(p + Vec3{0.03 * std::sin(t + u * 40.0 + q), -0.07 * q, 0.03 * std::cos(0.8 * t + u * 30.0)}));
			detail::add(tentacles, sdf::tubeMesh(hang, 0.0025, 0.3, 4));
		}
	}
	out += attr(translucentMaterial({0.25, 0.3, 0.35}, {0.6, 0.7, 0.8}), emitMesh(stems, identity()) + emitMesh(bells, identity()) + emitMesh(tentacles, identity()));
	return out + attr(detail::glowMaterial(Vec3{0.15, 0.45, 1.0} * 2.5), emitMesh(detail::beads(glows, 0.018), identity()));
}
// The plankton touched into light behind each animal of the deep that swims (the last second of its path), in three steps of fading.
inline std::string sparksText(const Life &life, double t) {
	std::vector<Vec3> fresh[3];
	for (const Group &g : life.groups) {
		if (g.kind != "lanternfish" && g.kind != "anglerfish") continue;
		for (int i = 0; i < g.count; ++i)
			for (int q = 0; q < 12; ++q) {
				const double age = (q + 0.5) / 12.0;
				const Vec3 p = groupAt(g, i, t - age);
				const int key = static_cast<int>(std::floor((t - age) * 12.0)) * 131 + i * 7 + q;
				fresh[std::min(2, q / 4)].push_back(detail::story(p + Vec3{0.08 * (hash01(key, 1) - 0.5), 0.08 * (hash01(key, 2) - 0.5), 0.08 * (hash01(key, 3) - 0.5)}));
			}
	}
	std::string out;
	for (int k = 0; k < 3; ++k)
		if (!fresh[k].empty()) out += attr(detail::glowMaterial(Vec3{0.2, 0.55, 1.0} * (5.0 / (1 + 2 * k))), emitMesh(detail::beads(fresh[k], 0.004), identity()));
	return out.empty() ? out : "# sparks in the water (film_deepsea.h)\n" + out;
}

// ---- the octopus ----
// How far an octopus is into its hiding (0 to 1), its jet (the last one: seconds since, or a negative), and where it is (pbrt's) at t.
inline double hideOf(const Octopus &o, double t) { return o.hide >= 0.0 ? smoothstep(o.hide, o.hide + 1.5, t) : 0.0; }
inline double jetSince(const Octopus &o, double t) {
	double since = -1.0;
	for (const double j : o.jets)
		if (t >= j) since = t - j;
	return since;
}
inline Vec3 octopusAt(const Octopus &o, double t, Vec3 &fwd) {
	const double a = o.toward * kPi / 180.0;
	fwd = {std::sin(a), 0.0, std::cos(a)};
	double crawl = t;   // (it crawls at 6 cm a second until it hides)
	if (o.hide >= 0.0) crawl = std::min(t, o.hide + 0.75);
	Vec3 p = o.at + fwd * (0.06 * o.size * crawl);
	for (const double j : o.jets)   // each jet: off backwards, mantle first, and down again
		if (t > j) {
			const double s = t - j;
			p = p - fwd * (2.6 * o.size * (1.0 - std::exp(-s / 0.7))) + Vec3{0.0, 0.7 * o.size * std::sin(kPi * std::min(1.0, s / 3.0)), 0.0};
		}
	return p;
}
// An octopus's meshes at t (local: forward +z, up +y, its middle on the ground at the origin): skin and suckers.
inline void octopusMesh(const Octopus &o, double t, Mesh &skin, Mesh &suckers) {
	const double S = o.size, hide = hideOf(o, t), jet = jetSince(o, t);
	const double streaming = jet >= 0.0 ? std::exp(-std::max(0.0, jet - 1.2) / 0.8) : 0.0;   // (its arms trailing behind while it shoots off)
	const double breathe = 1.0 + 0.06 * std::sin(2.0 * kPi * t / 2.4) - (jet >= 0.0 && jet < 2.0 ? 0.15 * std::abs(std::sin(2.0 * kPi * jet / 0.5)) : 0.0);
	const auto bumps = [&](Mesh m, double amount) {   // (papillae: the skin raised in points when it hides)
		for (std::size_t i = 0; i < m.verts.size(); ++i)
			m.verts[i] = m.verts[i] + m.normals[i] * (amount * 0.008 * S * nature::noise2(m.verts[i].x * 60.0 / S + m.verts[i].y * 31.0 / S, m.verts[i].z * 60.0 / S, o.seed + 5));
		return m;
	};
	Mesh mantle = lathe({{0.0, -0.5}, {0.32, -0.4}, {0.48, -0.1}, {0.45, 0.2}, {0.3, 0.42}, {0.0, 0.5}}, 18);
	mantle.uvs.clear();
	detail::add(skin, bumps(mantle, hide), mul(alignY({0.0, 0.13 * S, -0.1 * S}, normalize(Vec3{0.0, 0.5 + 0.4 * streaming, -1.0})), scale(0.19 * S * breathe, 0.26 * S, 0.19 * S * breathe)));
	Mesh head = uvSphere(1.0, 14, 10);
	head.uvs.clear();
	detail::add(skin, bumps(head, hide), mul(translate(0.0, 0.07 * S, 0.03 * S), scale(0.075 * S, 0.06 * S, 0.075 * S)));
	for (const double side : {-1.0, 1.0}) {   // eyes on turrets, with a dark bar of a pupil
		detail::add(skin, uvSphere(0.022 * S, 10, 8), translate(side * 0.055 * S, 0.11 * S, 0.05 * S));
		detail::add(suckers, uvSphere(1.0, 8, 6), mul(translate(side * 0.071 * S, 0.112 * S, 0.056 * S), scale(0.004 * S, 0.003 * S, 0.012 * S)));
	}
	for (int k = 0; k < 8; ++k) {   // the arms
		const int s = o.seed * 29 + k;
		const double a = 2.0 * kPi * (k + 0.5) / 8.0, L = 0.55 * S * (0.9 + 0.2 * hash01(s, 1));
		const double reach = (0.75 + 0.25 * std::sin(2.0 * kPi * t / 3.2 + 1.7 * k)) * (1.0 - 0.45 * hide);
		const double curl = 1.2 + 0.8 * std::sin(2.0 * kPi * t / 4.1 + k) + 3.0 * hide;
		const Vec3 root{0.045 * S * std::sin(a), 0.03 * S, 0.03 * S + 0.045 * S * std::cos(a)};
		std::vector<Vec3> pts;
		for (int q = 0; q <= 14; ++q) {
			const double u = q / 14.0;
			const double turn = a + curl * u * u * u * (k % 2 ? 1.0 : -1.0);   // (the tip curling round)
			const Vec3 out{std::sin(turn), 0.0, std::cos(turn)};
			Vec3 p = root + out * (L * u * reach) + Vec3{0.0, 0.05 * S * std::sin(kPi * u) * (0.4 + 0.6 * std::sin(2.0 * kPi * t / 3.2 + k)) * (1.0 - hide) - 0.02 * S * u, 0.0};
			const Vec3 behind = root + Vec3{0.03 * S * std::sin(a) * (1.0 - u), 0.02 * S * std::sin(5.0 * u + t * 3.0 + k), 0.0} + Vec3{0.0, 0.0, 1.0} * (L * u * 1.05);   // (streaming)
			p = lerp(p, behind, streaming);
			pts.push_back(p);
		}
		Mesh arm = sdf::tubeMesh(pts, 0.021 * S, 0.12, 8);
		detail::add(skin, bumps(arm, hide));
		std::vector<Vec3> cups;
		for (int q = 2; q < 13; ++q) cups.push_back(pts[static_cast<std::size_t>(q)] + Vec3{0.0, -0.016 * S * (1.0 - 0.06 * q), 0.0});
		Mesh b = detail::beads(cups, 0.006 * S);
		detail::add(suckers, b);
	}
}
// An ink cloud left at `p` (pbrt's) at `since` seconds: a dark puff spreading out, drifting and thinning (a grid medium in its own sphere, the sea outside it).
inline std::string inkText(const std::string &name, Vec3 p, double since, int seed) {
	if (since < 0.0 || since > 14.0) return std::string();
	const double R = 0.25 + 0.9 * (1.0 - std::exp(-since / 2.0)), fade = std::exp(-since / 5.0);
	const int n = 18;
	std::string sa, ss;   // (an "rgbgrid": it absorbs - a "uniformgrid" only scatters - so the ink is dark)
	for (int k = 0; k < n; ++k)
		for (int j = 0; j < n; ++j)
			for (int i = 0; i < n; ++i) {
				const double x = (i + 0.5) / n - 0.5, y = (j + 0.5) / n - 0.5, z = (k + 0.5) / n - 0.5;
				const double r = std::sqrt(x * x + y * y + z * z) * 2.0;
				const double billow = fire::detail::fbm3(x * 4.0 + 3.0, y * 4.0 - 0.15 * since, z * 4.0, seed);
				const double d = std::max(0.0, smoothstep(1.0, 0.3, r * (0.6 + 0.8 * billow)) * fade);
				char buf[64];
				std::snprintf(buf, sizeof buf, "%.3f %.3f %.3f ", 7.0 * d, 7.5 * d, 8.0 * d);
				sa += buf;
				std::snprintf(buf, sizeof buf, "%.3f %.3f %.3f ", 0.5 * d, 0.45 * d, 0.4 * d);
				ss += buf;
			}
	char buf[600];
	std::snprintf(buf, sizeof buf,
				  "# ink (film_deepsea.h)\nAttributeBegin\n  Translate %.4f %.4f %.4f\n  Scale %.4f %.4f %.4f\n  MakeNamedMedium \"%s\" \"string type\" [ \"rgbgrid\" ] \"integer nx\" [ %d ] \"integer ny\" [ %d ] \"integer nz\" [ %d ] \"float g\" [ 0.2 ]\n",
				  p.x - R, p.y - R, p.z - R, 2.0 * R, 2.0 * R, 2.0 * R, name.c_str(), n, n, n);
	std::string out = buf + ("    \"rgb sigma_a\" [ " + sa + "]\n    \"rgb sigma_s\" [ " + ss + "]\nAttributeEnd\n");
	std::snprintf(buf, sizeof buf, "AttributeBegin\n  Translate %.4f %.4f %.4f\n  Material \"interface\"\n  MediumInterface \"%s\" \"sea\"\n  Shape \"sphere\" \"float radius\" [ %.4f ]\nAttributeEnd\n", p.x, p.y, p.z,
				  name.c_str(), R * 1.75);
	return out + buf;
}
inline std::string octopusText(const Octopus &o, int index, double t, const underwater::Current &c) {
	Vec3 fwd;
	const Vec3 at = octopusAt(o, t, fwd);
	Mesh skin, suckers;
	octopusMesh(o, t, skin, suckers);
	const double hide = hideOf(o, t), jet = jetSince(o, t), pale = jet >= 0.0 ? std::exp(-jet / 2.5) : 0.0;
	const Vec3 colour = lerp(lerp(Vec3{0.48, 0.17, 0.09}, Vec3{0.5, 0.44, 0.33}, hide), Vec3{0.85, 0.78, 0.68}, pale);
	const Mat4 place = detail::placed(at, fwd);
	std::string out = "# an octopus (film_deepsea.h)\n" + attr(coated(colour, 0.35), emitMesh(skin, place)) + attr(coated(lerp(Vec3{0.85, 0.7, 0.62}, colour, 0.3), 0.3), emitMesh(suckers, place));
	for (std::size_t k = 0; k < o.inks.size(); ++k) {
		Vec3 f2;
		const Vec3 where = octopusAt(o, o.inks[k], f2) + Vec3{0.0, 0.45 * o.size + 0.04 * std::max(0.0, t - o.inks[k]), 0.0} + (c.drift(t) - c.drift(o.inks[k]));   // (rising a little as it spreads)
		out += inkText("ink_" + std::to_string(index) + "_" + std::to_string(k), where, t - o.inks[k], o.seed * 13 + static_cast<int>(k));
	}
	return out;
}

// ---- the cuttlefish ----
// The passing-cloud picture at frame k of 12 (a second's loop): dark bands crossing the back from head to tail.
inline std::string cloudsPicture(const std::string &film, int k) {
	char name[40];
	std::snprintf(name, sizeof name, "cuttle_clouds_%02d", k);
	return film_sky::picture(film, name, 64, 64, [&](std::vector<float> &rgb) {
		for (int j = 0; j < 64; ++j)
			for (int i = 0; i < 64; ++i) {
				const double u = (i + 0.5) / 64.0, v = (j + 0.5) / 64.0;
				const double back = std::pow(std::max(0.0, std::cos(2.0 * kPi * (u - 0.25))), 0.6);   // (the bands are on its back)
				const double band = 0.5 + 0.5 * std::sin(2.0 * kPi * (5.0 * v + k / 12.0) + 0.6 * std::sin(2.0 * kPi * u * 3.0));
				const double dark = back * smoothstep(0.55, 0.85, band);
				const double mottle = 0.9 + 0.1 * std::sin(i * 1.7) * std::sin(j * 2.3);
				const Vec3 c = lerp(Vec3{0.62, 0.52, 0.42}, Vec3{0.16, 0.11, 0.08}, dark) * mottle;
				const std::size_t q = (static_cast<std::size_t>(j) * 64 + i) * 3;
				rgb[q] = static_cast<float>(c.x), rgb[q + 1] = static_cast<float>(c.y), rgb[q + 2] = static_cast<float>(c.z);
			}
	});
}
inline std::string cuttleText(const std::string &film, const Cuttle &c, double t) {
	const std::string picture = cloudsPicture(film, static_cast<int>(std::floor(t * 12.0)) % 12);
	std::string out = "# cuttlefish (film_deepsea.h)\nAttributeBegin\n  Texture \"cuttle_clouds\" \"spectrum\" \"imagemap\" \"string filename\" [ \"" + picture + "\" ]\n";
	for (int i = 0; i < c.count; ++i) {
		const int s = c.seed * 61 + i;
		const double L = 0.3 + 0.15 * hash01(s, 1), w = (0.12 + 0.06 * hash01(s, 2)) / std::max(0.3, c.radius) * (hash01(s, 3) < 0.5 ? -1.0 : 1.0), a = w * t + 2.0 * kPi * hash01(s, 4);
		const Vec3 at = c.at + Vec3{c.radius * std::cos(a), 0.2 * std::sin(0.5 * t + s), c.radius * std::sin(a)} * (c.count > 1 ? 1.0 : 0.3);
		const Vec3 fwd{-std::sin(a) * w, 0.0, std::cos(a) * w};
		const Mat4 place = detail::placed(at, fwd, 4.0 * std::sin(0.7 * t + s));
		Mesh mantle = lathe({{0.0, -0.5}, {0.1, -0.42}, {0.17, -0.2}, {0.18, 0.1}, {0.15, 0.3}, {0.1, 0.42}, {0.0, 0.46}}, 20);
		for (std::size_t q = 0; q < mantle.verts.size(); ++q) {   // (along +z, flattened)
			const Vec3 v = mantle.verts[q], n = mantle.normals[q];
			mantle.verts[q] = Vec3{v.x * L * 1.0, v.z * L * 0.62, v.y * L};
			mantle.normals[q] = normalize(Vec3{n.x, n.z / 0.62, n.y});
		}
		for (auto &tr : mantle.tris) std::swap(tr[1], tr[2]);
		out += "  Material \"coateddiffuse\" \"texture reflectance\" \"cuttle_clouds\" \"float roughness\" [ 0.3 ]\n" + emitMesh(mantle, place);
		Mesh fins, head;
		for (const double side : {-1.0, 1.0}) {   // the fin round the mantle, rippling in waves running back
			std::vector<Vec3> inner, outer;
			for (int q = 0; q <= 24; ++q) {
				const double z = (-0.44 + 0.82 * q / 24.0) * L, half = 0.17 * L * std::sqrt(std::max(0.0, 1.0 - std::pow(z / (0.48 * L), 2.0)));
				const double wave = 0.025 * L * std::sin(2.0 * kPi * (z / L * 3.0 + 2.2 * t) + side);
				inner.push_back({side * half * 0.97, 0.0, z});
				outer.push_back({side * (half + 0.075 * L), wave, z});
			}
			for (int q = 0; q < 24; ++q) {
				detail::fin(fins, inner[q], outer[q], inner[q + 1]);
				detail::fin(fins, inner[q + 1], outer[q], outer[q + 1]);
			}
			detail::add(head, uvSphere(0.045 * L, 10, 8), translate(side * 0.1 * L, 0.04 * L, 0.42 * L));
		}
		detail::add(head, uvSphere(1.0, 12, 8), mul(translate(0.0, 0.0, 0.45 * L), scale(0.11 * L, 0.08 * L, 0.1 * L)));
		for (int k = 0; k < 8; ++k) {   // the arms, bundled forward
			const double a2 = 2.0 * kPi * k / 8.0;
			std::vector<Vec3> pts;
			for (int q = 0; q <= 6; ++q) {
				const double u = q / 6.0;
				pts.push_back({0.05 * L * std::cos(a2) * (1.0 - 0.6 * u), 0.04 * L * std::sin(a2) * (1.0 - 0.6 * u) - 0.02 * L * u * u, (0.5 + 0.22 * u) * L});
			}
			detail::add(head, sdf::tubeMesh(pts, 0.012 * L, 0.2, 6));
		}
		head.uvs.clear();
		out += "  " + translucentMaterial({0.6, 0.55, 0.5}, {0.25, 0.22, 0.2}) + "\n" + emitMesh(fins, place);
		out += "  " + coated({0.55, 0.46, 0.38}, 0.3) + "\n" + emitMesh(head, place);
	}
	return out + "AttributeEnd\n";
}

// ---- the open water's swimmers ----
// A ray `span` across (forward +z) at flap phase `ph`: its top and its belly (separate, for their colours) with uvs over its outline, cephalic fins (`horns`) and a tail
// `tail` times as long as its body.
inline void rayMesh(double span, double ph, double flap, bool horns, double tail, Mesh &top, Mesh &belly) {
	const int nu = 24, nv = 12;
	for (int side = 0; side < 2; ++side) {
		Mesh &m = side == 0 ? top : belly;
		const int base = static_cast<int>(m.verts.size());
		for (int j = 0; j <= nv; ++j)
			for (int i = 0; i <= nu; ++i) {
				const double u = -1.0 + 2.0 * i / nu, v = j / double(nv), au = std::abs(u);
				const double lead = 0.2 * span * std::pow(1.0 - au, 0.7), trail = -0.12 * span * std::pow(1.0 - au, 1.6);
				const double z = lerp(trail, lead, v), thick = 0.07 * span * (1.0 - au) * (1.0 - au) * std::sin(kPi * std::clamp(v, 0.02, 0.98));
				const double y = flap * span * std::pow(au, 1.6) * std::sin(ph - 1.6 * au) + (side == 0 ? 0.5 : -0.5) * thick;
				m.verts.push_back({u * 0.5 * span, y, z});
				m.normals.push_back({0.0, side == 0 ? 1.0 : -1.0, 0.0});
				m.uvs.push_back({0.5 + 0.5 * u, v});
			}
		const int w = nu + 1;
		for (int j = 0; j < nv; ++j)
			for (int i = 0; i < nu; ++i) {
				const int a = base + j * w + i;
				if (side == 0) m.tris.push_back({a, a + w, a + 1}), m.tris.push_back({a + 1, a + w, a + w + 1});
				else m.tris.push_back({a, a + 1, a + w}), m.tris.push_back({a + 1, a + w + 1, a + w});
			}
		sdf::computeNormals(m);
	}
	if (horns)
		for (const double s : {-1.0, 1.0}) detail::fin(belly, {s * 0.07 * span, 0.0, 0.19 * span}, {s * 0.09 * span, -0.03 * span, 0.27 * span}, {s * 0.11 * span, 0.0, 0.17 * span});
	belly.uvs.clear();
	std::vector<Vec3> whip;
	for (int q = 0; q <= 8; ++q) whip.push_back({0.02 * span * std::sin(ph * 0.5 + q * 0.6), 0.0, -0.1 * span - q / 8.0 * tail * 0.32 * span});
	detail::add(belly, sdf::tubeMesh(whip, 0.012 * span, 0.15, 5));
}
// White spots on an eagle ray's dark back.
inline std::string spotsPicture(const std::string &film) {
	return film_sky::picture(film, "eagleray_spots", 128, 128, [](std::vector<float> &rgb) {
		for (int j = 0; j < 128; ++j)
			for (int i = 0; i < 128; ++i) {
				double spot = 0.0;
				for (int k = 0; k < 90; ++k) {
					const double dx = (i + 0.5) / 128.0 - hash01(k, 1), dy = (j + 0.5) / 128.0 - hash01(k, 2);
					spot = std::max(spot, smoothstep(0.02, 0.012, std::sqrt(dx * dx + dy * dy)));
				}
				const Vec3 c = lerp(Vec3{0.05, 0.06, 0.08}, Vec3{0.85, 0.86, 0.84}, spot);
				const std::size_t q = (static_cast<std::size_t>(j) * 128 + i) * 3;
				rgb[q] = static_cast<float>(c.x), rgb[q + 1] = static_cast<float>(c.y), rgb[q + 2] = static_cast<float>(c.z);
			}
	});
}
// Where swimmer i of a group is at t (pbrt's), facing `fwd`, rolled `roll` degrees.
inline Vec3 swimmerAt(const Swimmers &g, int i, double t, Vec3 &fwd, double &roll, double level) {
	const int s = g.seed * 433 + i;
	const double speed = g.kind == "mantas" ? 0.9 : g.kind == "eaglerays" ? 1.3 : g.kind == "sealions" ? 2.6 : g.kind == "penguins" ? 2.4 : g.kind == "otters" ? 0.08 : 0.6;
	const double R = g.radius * (0.7 + 0.5 * hash01(s, 1)), w = speed / R * (hash01(s, 2) < 0.5 ? -1.0 : 1.0), a = w * t + 2.0 * kPi * (i + 0.3 * hash01(s, 3)) / std::max(1, g.count);
	const double bob = g.kind == "penguins" ? 0.5 * R * 0.25 * std::sin(3.0 * a) : g.kind == "sealions" ? 0.3 * R * std::sin(2.0 * a + s) : 0.6 * std::sin(0.3 * t + s);
	Vec3 p = g.at + Vec3{R * std::cos(a), bob, R * std::sin(a)};
	fwd = normalize(Vec3{-std::sin(a) * w, (g.kind == "penguins" ? 0.25 * 3.0 * std::cos(3.0 * a) : g.kind == "sealions" ? 0.6 * std::cos(2.0 * a + s) : 0.0) * std::abs(w) * R * 0.5, std::cos(a) * w});
	roll = g.kind == "sealions" ? 70.0 * std::sin(0.9 * t + s) : g.kind == "mantas" || g.kind == "eaglerays" ? -25.0 * (w > 0 ? 1.0 : -1.0) * std::min(1.0, std::abs(w) * R / 1.5) : 0.0;
	if (g.kind == "otters") {   // floating on its back at the surface, now and then turning right over
		p.y = level - 0.07;
		fwd = normalize(Vec3{fwd.x, 0.0, fwd.z});
		const double every = 9.0 + 6.0 * hash01(s, 4), into = std::fmod(t + every * hash01(s, 5), every);
		roll = 180.0 + (into < 1.4 ? 360.0 * smoothstep(0.0, 1.4, into) : 0.0);
	}
	if (g.kind == "stingrays") {   // buried in the sand until it rises, then gliding off a little above the bottom
		const double since = g.rise >= 0.0 ? t - g.rise - 0.4 * hash01(s, 6) : -1.0;
		const double ang = 2.0 * kPi * hash01(s, 7);
		const Vec3 home = g.at + Vec3{R * std::cos(ang), 0.0, R * std::sin(ang)}, dir{std::cos(ang + 1.2), 0.0, std::sin(ang + 1.2)};
		fwd = dir, roll = 0.0;
		p = since < 0.0 ? home + Vec3{0.0, -0.03, 0.0} : home + dir * (0.6 * std::max(0.0, since - 0.8)) + Vec3{0.0, 0.4 * smoothstep(0.0, 1.2, since), 0.0};
	}
	return p;
}
// A torpedo of a body (forward +z) `L` long: a sea lion's, a penguin's, an otter's.
inline Mesh torpedo(double L, double wide, double deep, double sway, double headBulge) {
	return sealife::detail::body(L, wide, deep, sway, {{0.0, -0.5}, {0.05, -0.42}, {0.13, -0.2}, {0.16, 0.0}, {0.14, 0.2}, {0.09 + headBulge, 0.33}, {0.08, 0.42}, {0.0, 0.5}});
}
inline std::string swimmersText(const std::string &film, const Swimmers &g, double t, double level, const underwater::Current &c) {
	std::string out = "# " + g.kind + " (film_deepsea.h)\n";
	std::vector<undersea::Bubble> trail;
	for (int i = 0; i < g.count; ++i) {
		const int s = g.seed * 59 + i;
		Vec3 fwd;
		double roll = 0.0;
		const Vec3 at = swimmerAt(g, i, t, fwd, roll, level);
		const Mat4 place = detail::placed(at, fwd, roll);
		if (g.kind == "mantas" || g.kind == "eaglerays" || g.kind == "stingrays") {
			const bool manta = g.kind == "mantas", eagle = g.kind == "eaglerays", sting = g.kind == "stingrays";
			const double span = manta ? 3.6 + 1.2 * hash01(s, 1) : eagle ? 1.5 + 0.4 * hash01(s, 1) : 0.7 + 0.3 * hash01(s, 1);
			const double since = sting && g.rise >= 0.0 ? t - g.rise : -1.0, moving = sting ? smoothstep(0.0, 1.0, since) : 1.0;
			const double ph = 2.0 * kPi * t / (manta ? 4.5 : eagle ? 2.4 : 0.9) + s;
			Mesh top, belly;
			rayMesh(span, ph, (manta ? 0.13 : eagle ? 0.16 : 0.04) * moving, manta, manta ? 0.5 : eagle ? 2.5 : 1.0, top, belly);
			if (sting)   // (a round disc of a body, flattened)
				for (Mesh *m : {&top, &belly})
					for (Vec3 &v : m->verts) v = Vec3{v.x * 0.85, v.y * 0.6, v.z > 0.0 ? v.z * 1.9 : v.z * 1.6};
			if (eagle) out += "AttributeBegin\n  Texture \"eagle_spots\" \"spectrum\" \"imagemap\" \"string filename\" [ \"" + spotsPicture(film) + "\" ]\n  Material \"coateddiffuse\" \"texture reflectance\" \"eagle_spots\" \"float roughness\" [ 0.3 ]\n" + emitMesh(top, place) + "AttributeEnd\n";
			else out += attr(coated(sting && since < 0.0 ? Vec3{0.5, 0.44, 0.32} : manta ? Vec3{0.06, 0.07, 0.08} : Vec3{0.32, 0.27, 0.2}, 0.35), emitMesh(top, place));
			out += attr(coated(manta || eagle ? Vec3{0.86, 0.86, 0.84} : Vec3{0.8, 0.76, 0.7}, 0.3), emitMesh(belly, place));
			if (sting && since >= 0.0 && since < 5.0) {   // the cloud of sand it lifts off in: grains thrown out and settling
				std::vector<Vec3> grains;
				Vec3 f0;
				double r0 = 0.0;
				Swimmers before = g;
				before.rise = -1.0;
				const Vec3 home = swimmerAt(before, i, t, f0, r0, level);
				for (int k = 0; k < 260; ++k) {
					const double a = 2.0 * kPi * hash01(k, s + 1), v = 0.4 + 0.8 * hash01(k, s + 2), up = 0.3 + 0.9 * hash01(k, s + 3);
					const double tt = std::min(since, 2.5);
					grains.push_back(detail::story(home + Vec3{std::cos(a) * v * (1.0 - std::exp(-tt)), up * tt * std::exp(-0.6 * tt) * 0.8, std::sin(a) * v * (1.0 - std::exp(-tt))}));
				}
				out += attr(diffuseMaterial({0.55, 0.48, 0.36}), emitMesh(detail::beads(grains, 0.006 + 0.004 * smoothstep(0.0, 3.0, since)), identity()));
			}
			continue;
		}
		if (g.kind == "sealions" || g.kind == "penguins") {
			const bool lion = g.kind == "sealions";
			const double L = lion ? 1.9 + 0.4 * hash01(s, 1) : 0.68 + 0.1 * hash01(s, 1);
			const double stroke = std::sin(2.0 * kPi * t / (lion ? 1.4 : 0.32) + s);
			Mesh body = torpedo(L, lion ? 0.62 : 0.66, lion ? 0.6 : 0.72, 0.06 * std::sin(2.0 * kPi * t / 1.1 + s), lion ? 0.01 : 0.02);
			Mesh dark, light;
			for (const auto &tr : body.tris) {   // a penguin is black on its back, white beneath; a sea lion brown all over
				const Vec3 n = body.normals[static_cast<std::size_t>(tr[0])] + body.normals[static_cast<std::size_t>(tr[1])] + body.normals[static_cast<std::size_t>(tr[2])];
				Mesh &to = !lion && n.y < -0.1 ? light : dark;
				const int base = static_cast<int>(to.verts.size());
				for (int q = 0; q < 3; ++q) to.verts.push_back(body.verts[static_cast<std::size_t>(tr[q])]), to.normals.push_back(body.normals[static_cast<std::size_t>(tr[q])]);
				to.tris.push_back({base, base + 1, base + 2});
			}
			for (const double side : {-1.0, 1.0}) {   // the flippers: the front ones stroking, a sea lion's rear ones trailing
				const double up = (lion ? 0.6 : 0.9) * stroke;
				const Vec3 root{side * 0.12 * L, 0.0, (lion ? 0.18 : 0.12) * L};
				detail::fin(dark, root, root + Vec3{side * (lion ? 0.38 : 0.32) * L * std::cos(up), (lion ? 0.38 : 0.32) * L * std::sin(up), -0.12 * L}, root + Vec3{0.0, 0.0, -0.16 * L});
				if (lion) detail::fin(dark, {side * 0.05 * L, 0.0, -0.42 * L}, {side * 0.12 * L, 0.02 * L * stroke, -0.62 * L}, {side * 0.02 * L, 0.0, -0.58 * L});
			}
			if (!lion) detail::add(dark, detail::cone(0.018 * L, 0.12 * L), alignY({0.0, 0.0, 0.48 * L}, {0.0, -0.1, 1.0}));   // (the beak)
			out += attr(coated(lion ? Vec3{0.28, 0.2, 0.13} : Vec3{0.04, 0.045, 0.05}, 0.18), emitMesh(dark, place));
			if (!light.tris.empty()) out += attr(coated({0.88, 0.88, 0.86}, 0.25), emitMesh(light, place));
			const auto path = [&g, i, level](double tt) {
				Vec3 f;
				double r = 0.0;
				return swimmerAt(g, i, tt, f, r, level);
			};
			undersea::trailBubbles(path, t, lion ? 1.5 : 22.0, lion ? 1.5 : 1.0, s, level, c, trail);
			continue;
		}
		// an otter, on its back: a furry body, a pale face, paws held on its chest, feet up
		const double L = 1.0 + 0.2 * hash01(s, 1);
		Mesh body = torpedo(L, 0.6, 0.5, 0.0, 0.03), face;
		detail::add(face, uvSphere(1.0, 12, 8), mul(translate(0.0, 0.02 * L, 0.46 * L), scale(0.08 * L, 0.07 * L, 0.08 * L)));
		for (const double side : {-1.0, 1.0}) {
			detail::add(body, uvSphere(0.03 * L, 8, 6), translate(side * 0.05 * L, -0.07 * L, 0.3 * L + 0.01 * L * std::sin(3.0 * t + side)));   // (its paws, on its chest: below it in its own frame)
			detail::add(body, uvSphere(1.0, 8, 6), mul(translate(side * 0.07 * L, -0.06 * L, -0.42 * L), scale(0.04 * L, 0.02 * L, 0.07 * L)));
		}
		out += attr(coated({0.26, 0.19, 0.12}, 0.5), emitMesh(body, place)) + attr(coated({0.6, 0.55, 0.47}, 0.5), emitMesh(face, place));
	}
	return out + undersea::bubblesText(trail);
}

// The film's animals with their heights made pbrt's (on a terrain heights are above the ground there).
inline Life lifted(Life life, const std::function<double(double, double)> &groundAt) {
	for (Group &g : life.groups) {
		const double y = groundAt(0.5 * (g.lo.x + g.hi.x), 0.5 * (g.lo.z + g.hi.z));
		g.lo.y += y, g.hi.y += y;
	}
	for (Octopus &o : life.octopuses) o.at.y += groundAt(o.at.x, o.at.z);
	for (Cuttle &c : life.cuttles) c.at.y += groundAt(c.at.x, c.at.z);
	for (Swimmers &s : life.swimmers)
		if (s.kind != "otters") s.at.y += groundAt(s.at.x, s.at.z);
	return life;
}
// All of it at t.
inline std::string text(const std::string &film, const Life &life, double t, double level, const underwater::Current &c) {
	if (!life.any()) return std::string();
	std::string out;
	for (const Group &g : life.groups) {
		if (g.kind == "anglerfish") out += anglerText(g, t);
		else if (g.kind == "lanternfish") out += lanternText(g, t);
		else if (g.kind == "combjellies") out += combJellyText(g, t);
		else out += siphonophoreText(g, t);
	}
	if (life.sparks) out += sparksText(life, t);
	for (std::size_t i = 0; i < life.octopuses.size(); ++i) out += octopusText(life.octopuses[i], static_cast<int>(i), t, c);
	for (const Cuttle &cu : life.cuttles) out += cuttleText(film, cu, t);
	for (const Swimmers &s : life.swimmers) out += swimmersText(film, s, t, level, c);
	return out;
}

}  // namespace deepsea
}  // namespace cartoon
