#pragma once
// film_treasure.h -- round 17: treasure and props under the sea, and things dropped into the water falling as things fall in water.
//
//   chest X Z [toward A] [open T] [seed K]     a treasure chest on the seabed, iron-banded, facing A; at T its lid creaks open, a burst of bubbles escapes and a heap of
//                                             gold coins inside glows warm
//   clam X Z [shut T]... [nopearl] [seed K]    a giant clam, its wavy lips gaping and breathing, a pearl inside; at each T it snaps shut (on a nosy fish, a finger) and
//                                             opens again three seconds later
//   bottle X Y Z [seed K]                      a bottle with a message rolled inside: Y above the seabed; one at the surface floats, bobbing, and drifts with the current,
//                                             one below sinks and lies rocking on the sand
//   shells N in X0 Z0 X1 Z1 [seed K]           N shells scattered over the seabed (1 to 400): scallops, cones, starfish and sand dollars
//   sink coin|anchor|rock|ball X Y Z at T [seed K]
//                                             something let go at T, Y above the seabed (above the water: dropped in from the air, with a splash): a coin flutters down
//                                             like a leaf, an anchor plunges, a rock sinks at its slow terminal speed - each landing in a puff of silt - and a ball,
//                                             buoyant, rises and bobs at the surface
//
// pbrt's x z; meshes in story space (x mirrored) for emitMesh. Things fall in water at a terminal speed reached in a fraction of a second: the speed relaxes towards it
// (dropped from the air, from the speed it hit the water at) - fallOf.

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "film_deepsea.h"
#include "film_giants.h"
#include "film_undersea.h"
#include "nature_lib.h"

namespace cartoon {
namespace treasure {

struct Chest {
	double x = 0.0, z = 0.0, toward = 0.0, open = 1e30;
	int seed = 1;
};
struct Clam {
	double x = 0.0, z = 0.0;
	std::vector<double> shut;
	bool pearl = true;
	int seed = 1;
};
struct Bottle {
	Vec3 at;
	int seed = 1;
};
struct Shells {
	int count = 40;
	double x0 = -3.0, z0 = -3.0, x1 = 3.0, z1 = 3.0;
	int seed = 1;
};
struct Sink {
	std::string kind = "rock";
	Vec3 at;
	double t = 0.0;
	int seed = 1;
};
struct World {
	std::vector<Chest> chests;
	std::vector<Clam> clams;
	std::vector<Bottle> bottles;
	std::vector<Shells> shells;
	std::vector<Sink> sinks;
	bool any() const { return !chests.empty() || !clams.empty() || !bottles.empty() || !shells.empty() || !sinks.empty(); }
};
// The heights made pbrt's (above the seabed there).
inline World lifted(World w, const std::function<double(double, double)> &groundAt) {
	for (Bottle &b : w.bottles) b.at.y += groundAt(b.at.x, b.at.z);
	for (Sink &s : w.sinks) s.at.y += groundAt(s.at.x, s.at.z);
	return w;
}

namespace detail {
using deepsea::detail::add;
using sealife::detail::facingOf;
using sealife::detail::story;
inline Mat4 placed(Vec3 at, Vec3 fwd) { return mul(translate(story(at)), facingOf(story(fwd))); }
inline Vec3 toward(double a) { return {std::sin(a * kPi / 180.0), 0.0, std::cos(a * kPi / 180.0)}; }
inline Mesh ellipsoid(Vec3 c, Vec3 r, int nu = 14, int nv = 8) {
	Mesh m;
	add(m, uvSphere(1.0, nu, nv), mul(translate(c), scale(r.x, r.y, r.z)));
	return m;
}
inline Mesh disc(double r, double h, int n = 14) { return lathe({{0.0, -h / 2.0}, {r, -h / 2.0}, {r, h / 2.0}, {0.0, h / 2.0}}, n); }
inline const Vec3 gold{0.95, 0.72, 0.3}, brass{0.85, 0.65, 0.35}, iron{0.25, 0.23, 0.22}, wood{0.36, 0.22, 0.12};
}  // namespace detail

// ---- a falling thing ----
// How far a thing has fallen `tau` seconds after it was let go in water (or met the water at speed `v0`), with terminal speed `vt` (negative: it rises) and the time
// it takes to reach it `tc`: its speed relaxes from v0 to vt.
inline double fallOf(double tau, double v0, double vt, double tc) {
	if (tau <= 0.0) return 0.0;
	return vt * tau + (v0 - vt) * tc * (1.0 - std::exp(-tau / tc));
}
struct Fall {
	double vt = 1.8, tc = 0.2, silt = 0.6, drift = 0.3, size = 0.12;
};
inline Fall fallFor(const std::string &kind) {
	if (kind == "coin") return {0.3, 0.05, 0.08, 1.0, 0.012};
	if (kind == "anchor") return {2.6, 0.3, 1.0, 0.05, 0.5};
	if (kind == "ball") return {-0.9, 0.15, 0.0, 1.0, 0.11};
	return {1.8, 0.2, 0.6, 0.1, 0.12};
}
// Where a sink's thing is at t (pbrt's), its tilt in degrees, and when it landed (or reached the surface) - 1e30 if not yet.
inline Vec3 sinkAt(const Sink &s, double t, double level, const std::function<double(double, double)> &bedAt, const underwater::Current &c, double &tilt, double &landed) {
	const Fall f = fallFor(s.kind);
	const double g = 9.81;
	// through the air first, if it was let go above the water
	const double airDrop = std::max(0.0, s.at.y - level), tAir = std::sqrt(2.0 * airDrop / g), v0 = g * tAir, tWater = s.t + tAir;
	tilt = 0.0, landed = 1e30;
	if (t < tWater) return s.at - Vec3{0.0, 0.5 * g * (t - s.t) * (t - s.t) * (t > s.t ? 1.0 : 0.0), 0.0};
	const double y0 = std::min(s.at.y, level);
	const double bed = bedAt(s.at.x, s.at.z) + f.size * 0.5, top = level - f.size * 0.3;
	// when it lands (or surfaces): the fall is monotonic, so halve the interval
	const double goal = f.vt > 0.0 ? y0 - bed : top - y0;
	double lo = 0.0, hi = 1200.0;
	if (goal <= 0.0) hi = 0.0;
	else if (std::abs(fallOf(hi, f.vt > 0.0 ? v0 : 0.0, std::abs(f.vt), f.tc)) < goal) hi = 1e30;
	else
		for (int k = 0; k < 50; ++k) {
			const double mid = 0.5 * (lo + hi);
			(fallOf(mid, f.vt > 0.0 ? v0 : 0.0, std::abs(f.vt), f.tc) < goal ? lo : hi) = mid;
		}
	landed = tWater + hi;
	const double tau = std::min(t - tWater, hi);
	const double d = fallOf(tau, f.vt > 0.0 ? v0 : 0.0, std::abs(f.vt), f.tc);
	Vec3 p{s.at.x, f.vt > 0.0 ? y0 - d : y0 + d, s.at.z};
	p = p + (c.drift(tWater + tau) - c.drift(tWater)) * f.drift;
	if (s.kind == "coin" && t < landed) {   // (a coin flutters: side to side, tipping as it goes)
		const double w = 2.0 * kPi * 1.1 * (t - tWater);
		p = p + Vec3{0.08 * std::sin(w), 0.0, 0.05 * std::sin(0.5 * w + s.seed)};
		tilt = 45.0 * std::cos(w);
	}
	if (s.kind == "anchor") tilt = t >= landed ? 70.0 : 0.0;
	if (s.kind == "ball" && t >= landed) {   // bobbing at the surface, drifting on
		p.y = level - 0.3 * f.size + 0.03 * std::sin(2.2 * t + s.seed);
		p = p + c.drift(t) - c.drift(landed);
	}
	return p;
}
inline std::string sinkText(const Sink &s, double t, double level, const std::function<double(double, double)> &bedAt, const underwater::Current &c) {
	using namespace detail;
	if (t < s.t) return std::string();
	double tilt = 0.0, landed = 1e30;
	const Vec3 p = sinkAt(s, t, level, bedAt, c, tilt, landed);
	const Fall f = fallFor(s.kind);
	std::string out = "# a " + s.kind + " let go at " + std::to_string(s.t) + " s (film_treasure.h)\n";
	const Mat4 m = mul(placed(p, toward(37.0 * s.seed)), rotate({1, 0, 0}, tilt));
	Mesh mesh;
	if (s.kind == "coin") {
		add(mesh, disc(f.size, 0.0025));
		out += attr(metalMaterial(gold, 0.15), emitMesh(mesh, m));
	} else if (s.kind == "anchor") {
		add(mesh, sdf::tubeMesh({{0.0, -0.4, 0.0}, {0.0, 0.45, 0.0}}, 0.035, 1.0, 8));   // the shank, the arms, the stock and the ring
		add(mesh, sdf::tubeMesh({{-0.32, -0.15, 0.0}, {-0.26, -0.36, 0.0}, {0.0, -0.44, 0.0}, {0.26, -0.36, 0.0}, {0.32, -0.15, 0.0}}, 0.03, 1.0, 8));
		add(mesh, sdf::tubeMesh({{0.0, 0.35, -0.3}, {0.0, 0.35, 0.3}}, 0.025, 1.0, 6));
		add(mesh, ellipsoid({0.0, 0.52, 0.0}, {0.07, 0.07, 0.02}));
		out += attr(coated(iron, 0.6), emitMesh(mesh, m));
	} else if (s.kind == "ball") {
		add(mesh, uvSphere(f.size, 20, 12));
		Mesh band;
		add(band, ellipsoid({0.0, 0.0, 0.0}, {f.size * 1.01, f.size * 0.3, f.size * 1.01}, 20, 6));
		out += attr(coated({0.9, 0.15, 0.1}, 0.15), emitMesh(mesh, m)) + attr(coated({0.95, 0.95, 0.92}, 0.15), emitMesh(band, m));
	} else {
		add(mesh, nature::rock(f.size, s.seed));
		out += attr(diffuseMaterial({0.42, 0.4, 0.37}), emitMesh(mesh, m));
	}
	const double tAir = std::sqrt(2.0 * std::max(0.0, s.at.y - level) / 9.81);
	if (s.at.y > level) {   // the splash it made going in, and the bubbles it dragged down
		out += giants::sprayText({s.at.x, level, s.at.z}, t - s.t - tAir, s.kind == "coin" ? 6 : 40, s.kind == "anchor" ? 3.0 : 1.6, s.kind == "coin" ? 0.008 : 0.025, s.seed * 7);
		std::vector<undersea::Bubble> bs;
		const int n = s.kind == "coin" ? 3 : 24;
		for (int k = 0; k < n; ++k) {
			undersea::Bubble b;
			const double depth = 0.6 * hash01(k, s.seed + 3) * (s.kind == "anchor" ? 2.0 : 1.0);
			const Vec3 at{s.at.x + 0.15 * (hash01(k, s.seed + 4) - 0.5), level - depth, s.at.z + 0.15 * (hash01(k, s.seed + 5) - 0.5)};
			if (undersea::bubbleAt(at, 0.002 + 0.006 * hash01(k, s.seed + 6), t - s.t - tAir - 0.1 * depth, t, s.seed * 13 + k, level, c, b)) bs.push_back(b);
		}
		out += undersea::bubblesText(bs);
	}
	if (t >= landed && f.silt > 0.0) out += undersea::siltText({p.x, bedAt(p.x, p.z), p.z}, t - landed, f.silt, s.seed * 19);
	if (s.kind == "ball" && t >= landed) out += giants::sprayText({p.x, level, p.z}, t - landed, 10, 0.8, 0.01, s.seed * 23);   // (it pops up through the surface)
	return out;
}

// ---- a treasure chest ----
inline std::string chestText(const Chest &ch, double t, const std::function<double(double, double)> &bedAt, double level, const underwater::Current &c) {
	using namespace detail;
	const double bed = bedAt(ch.x, ch.z);
	const Mat4 m = mul(placed({ch.x, bed - 0.03, ch.z}, toward(ch.toward)), rotate({0, 0, 1}, 4.0 * (hash01(1, ch.seed) - 0.5)));   // (sunk a little in the sand, not quite level)
	const double since = t - ch.open;
	double open = smoothstep(0.0, 1.6, since);   // (creaking up, and settling back with a little bounce)
	if (since > 1.6) open *= 1.0 + 0.05 * std::sin(5.0 * (since - 1.6)) * std::exp(-2.0 * (since - 1.6));
	Mesh body, lid, bands, coins, glow;
	add(body, roundedBox(0.8, 0.42, 0.5, 0.03), translate({0.0, 0.21, 0.0}));
	// the lid: a half drum hinged at the back's top edge
	Mesh drum;
	for (int i = 0; i <= 12; ++i) {
		const double a = kPi * i / 12.0;
		for (const double x : {-0.4, 0.4}) drum.verts.push_back({x, 0.25 * std::sin(a), 0.25 * std::cos(a)}), drum.normals.push_back({0.0, std::sin(a), std::cos(a)});
	}
	for (int i = 0; i < 12; ++i) drum.tris.push_back({2 * i, 2 * i + 2, 2 * i + 1}), drum.tris.push_back({2 * i + 1, 2 * i + 2, 2 * i + 3});
	add(drum, box(0.8, 0.02, 0.5), translate({0.0, 0.01, 0.0}));
	for (const double x : {-0.4, 0.4}) {   // (its two ends, half discs)
		const int c0 = static_cast<int>(drum.verts.size());
		drum.verts.push_back({x, 0.0, 0.0}), drum.normals.push_back({x > 0.0 ? 1.0 : -1.0, 0.0, 0.0});
		for (int i = 0; i <= 12; ++i) drum.verts.push_back({x, 0.25 * std::sin(kPi * i / 12.0), 0.25 * std::cos(kPi * i / 12.0)}), drum.normals.push_back({x > 0.0 ? 1.0 : -1.0, 0.0, 0.0});
		for (int i = 0; i < 12; ++i) drum.tris.push_back({c0, c0 + 1 + i, c0 + 2 + i});
	}
	const Mat4 hinge = mul(translate({0.0, 0.42, -0.25}), rotate({1, 0, 0}, -105.0 * std::max(0.0, open)), translate({0.0, 0.0, 0.25}));
	drum.uvs.clear();   // (the box's uvs and none for the rest: a mesh's uvs are all or nothing)
	add(lid, drum, hinge);
	for (const double x : {-0.3, 0.3}) {
		add(bands, box(0.05, 0.44, 0.52), translate({x, 0.21, 0.0}));
		Mesh arc;
		add(arc, ellipsoid({0.0, 0.0, 0.0}, {0.028, 0.255, 0.255}, 16, 6), identity());
		add(bands, arc, mul(hinge, translate({x, 0.0, 0.0})));
	}
	add(bands, box(0.09, 0.1, 0.03), translate({0.0, 0.36, 0.255}));   // (the lock)
	for (int k = 0; k < 70; ++k) {   // the coins heaped inside, mounded in the middle
		const double u = hash01(k, ch.seed + 5) - 0.5, v = hash01(k, ch.seed + 6) - 0.5;
		const double y = 0.36 + 0.07 * (1.0 - 4.0 * (u * u + v * v)) + 0.02 * hash01(k, ch.seed + 7);
		add(coins, disc(0.03, 0.005, 10), mul(translate({0.7 * u, y, 0.42 * v}), rotate({1, 0, 0}, 50.0 * (hash01(k, ch.seed + 8) - 0.5))));
	}
	add(glow, box(0.7, 0.01, 0.4), translate({0.0, 0.34, 0.0}));
	std::string out = "# a treasure chest (film_treasure.h)\n" + attr(coated(wood, 0.55), emitMesh(body, m)) + attr(coated(wood * 1.1, 0.55), emitMesh(lid, m)) +
					  attr(metalMaterial(iron, 0.35), emitMesh(bands, m)) + attr(metalMaterial(gold, 0.12), emitMesh(coins, m));
	if (open > 0.05) out += attr(deepsea::detail::glowMaterial(Vec3{3.0, 2.0, 0.8} * std::min(1.0, open)), emitMesh(glow, m));   // (the gold's warm glow, seen as the lid lifts)
	if (since >= 0.0) {   // the air trapped under the lid let out in a rush
		std::vector<undersea::Bubble> bs;
		const Vec3 mouth = Vec3{ch.x, bed + 0.45, ch.z} + toward(ch.toward) * 0.15;
		for (int k = 0; k < 40; ++k) {
			undersea::Bubble b;
			const Vec3 at = mouth + Vec3{0.6 * (hash01(k, ch.seed + 9) - 0.5), 0.0, 0.3 * (hash01(k, ch.seed + 10) - 0.5)};
			if (undersea::bubbleAt(at, 0.003 + 0.012 * std::pow(hash01(k, ch.seed + 11), 2.0), since - 0.4 * hash01(k, ch.seed + 12), t, ch.seed * 29 + k, level, c, b)) bs.push_back(b);
		}
		out += undersea::bubblesText(bs) + undersea::siltText({ch.x, bed, ch.z}, since, 0.3, ch.seed * 31);
	}
	return out;
}

// ---- a giant clam ----
inline double clamGape(const Clam &cl, double t) {
	double g = 0.75 + 0.15 * std::sin(0.6 * t + cl.seed);   // (breathing)
	for (const double at : cl.shut) {
		const double since = t - at;
		if (since >= 0.0 && since < 4.0) g = std::min(g, g * (since < 0.12 ? 1.0 - since / 0.12 : smoothstep(3.0, 4.0, since)));   // (snapping shut, and slowly opening again)
	}
	return g;
}
inline std::string clamText(const Clam &cl, double t, const std::function<double(double, double)> &bedAt) {
	using namespace detail;
	const double bed = bedAt(cl.x, cl.z), gape = clamGape(cl, t), W = 0.55;
	const Mat4 m = mul(placed({cl.x, bed - 0.04, cl.z}, toward(360.0 * hash01(1, cl.seed))), rotate({0, 0, 1}, 3.0));
	// each half: a dome with deep wavy ribs (side 1 the upper, -1 the lower), the halves meeting in a zigzag
	const auto half = [&](double side) {
		Mesh h;
		const int nu = 36, nv = 8;
		for (int j = 0; j <= nv; ++j)
			for (int i = 0; i <= nu; ++i) {
				const double phi = 2.0 * kPi * i / nu, th = 0.5 * kPi * j / nv, rib = 1.0 + 0.07 * std::sin(10.0 * phi) * std::sin(th);
				const double zig = 0.035 * W * std::sin(10.0 * phi) * std::pow(std::sin(th), 8.0);
				h.verts.push_back({0.85 * W * std::sin(th) * std::cos(phi) * rib, side * (0.28 * W * std::cos(th) + zig), 0.4 * W * std::sin(th) * std::sin(phi) * rib});
			}
		for (int j = 0; j < nv; ++j)
			for (int i = 0; i < nu; ++i) {
				const int a = j * (nu + 1) + i, b = a + 1, c = a + nu + 1, d = c + 1;
				if (j > 0) side > 0 ? h.tris.push_back({a, c, b}) : h.tris.push_back({a, b, c});   // (the first row's a and b are both the pole)
				side > 0 ? h.tris.push_back({b, c, d}) : h.tris.push_back({b, d, c});
			}
		sdf::computeNormals(h);
		return h;
	};
	Mesh shells, mantle, pearl;
	const double open = 25.0 * gape;
	add(shells, half(1.0), mul(translate({0.0, 0.0, -0.4 * W}), rotate({1, 0, 0}, -open), translate({0.0, 0.0, 0.4 * W})));   // (hinged along its back)
	add(shells, half(-1.0));
	add(mantle, ellipsoid({0.0, 0.05 * gape, 0.0}, {0.85 * W, 0.04 + 0.05 * gape, 0.3 * W}, 20, 8));
	if (cl.pearl) add(pearl, uvSphere(0.035, 16, 10), translate({0.0, 0.07 + 0.06 * gape, 0.05 * W}));
	std::string out = "# a giant clam (film_treasure.h)\n" + attr(coated({0.78, 0.74, 0.66}, 0.5), emitMesh(shells, m)) +
					  attr(coated(lerp(Vec3{0.1, 0.35, 0.55}, Vec3{0.2, 0.6, 0.45}, hash01(2, cl.seed)), 0.25), emitMesh(mantle, m));
	if (cl.pearl) out += attr(coated({0.95, 0.93, 0.9}, 0.05), emitMesh(pearl, m));
	return out;
}

// ---- a message in a bottle ----
inline std::string bottleText(const Bottle &b, double t, double level, const std::function<double(double, double)> &bedAt, const underwater::Current &c) {
	using namespace detail;
	const double bed = bedAt(b.at.x, b.at.z);
	const bool floating = b.at.y >= level - 0.1;
	Vec3 p = b.at;
	double roll = 0.0, pitch = 0.0;
	if (floating) {
		p = p + c.drift(t);
		p.y = level - 0.02 + 0.03 * std::sin(1.9 * t + b.seed);
		roll = 18.0 * std::sin(1.3 * t + b.seed), pitch = 70.0 + 10.0 * std::sin(0.9 * t);   // (floating neck up, tipping)
	} else {
		p.y = bed + 0.04;
		roll = 6.0 * std::sin(0.7 * t + b.seed) * (c.on ? 1.0 : 0.3);   // (rocking on the sand)
	}
	const Mat4 m = mul(placed(p, toward(360.0 * hash01(1, b.seed))), rotate({1, 0, 0}, pitch), rotate({0, 0, 1}, roll), rotate({1, 0, 0}, -90.0));
	Mesh glass, cork, paper;
	add(glass, lathe({{0.0, -0.12}, {0.045, -0.12}, {0.048, -0.1}, {0.048, 0.04}, {0.03, 0.08}, {0.015, 0.1}, {0.015, 0.14}, {0.0, 0.14}}, 20));
	add(cork, lathe({{0.0, 0.13}, {0.013, 0.13}, {0.014, 0.165}, {0.0, 0.165}}, 10));
	add(paper, lathe({{0.0, -0.08}, {0.022, -0.08}, {0.022, 0.03}, {0.0, 0.03}}, 12));
	return "# a message in a bottle (film_treasure.h)\n" + attr("Material \"dielectric\" \"float eta\" [ 1.1300 ] \"float roughness\" [ 0.0200 ]", emitMesh(glass, m)) +
		   attr(diffuseMaterial({0.55, 0.4, 0.25}), emitMesh(cork, m)) + attr(diffuseMaterial({0.85, 0.78, 0.6}), emitMesh(paper, m));
}

// ---- shells over the sand ----
inline std::string shellsText(const Shells &sh, const std::function<double(double, double)> &bedAt) {
	using namespace detail;
	Mesh scallops, cones, stars, dollars;
	for (int k = 0; k < std::clamp(sh.count, 1, 400); ++k) {
		const double x = sh.x0 + (sh.x1 - sh.x0) * hash01(k, sh.seed + 1), z = sh.z0 + (sh.z1 - sh.z0) * hash01(k, sh.seed + 2);
		const double kind = hash01(k, sh.seed + 3), s = 0.6 + 0.8 * hash01(k, sh.seed + 4);
		const Mat4 m = mul(translate(story({x, bedAt(x, z) + 0.004, z})), rotate({0, 1, 0}, 360.0 * hash01(k, sh.seed + 5)), scale(s));
		if (kind < 0.35) {   // a scallop: a ribbed fan
			Mesh f;
			const int n = 14;
			f.verts.push_back({0.0, 0.0, -0.02}), f.normals.push_back({0.0, 1.0, 0.0});
			for (int i = 0; i <= n; ++i) {
				const double a = kPi * (0.15 + 0.7 * i / n), r = 0.035 * (1.0 + 0.05 * (i % 2));
				f.verts.push_back({r * std::cos(a), 0.008 + 0.003 * (i % 2), -0.02 + r * std::sin(a)}), f.normals.push_back({0.0, 1.0, 0.0});
			}
			for (int i = 0; i < n; ++i) f.tris.push_back({0, i + 2, i + 1});
			add(scallops, f, m);
		} else if (kind < 0.6) {
			add(cones, lathe({{0.0, 0.0}, {0.012, 0.005}, {0.014, 0.02}, {0.006, 0.045}, {0.0, 0.05}}, 10), mul(m, rotate({0, 0, 1}, 90.0), translate({0.0, -0.02, 0.0})));
		} else if (kind < 0.8) {   // a starfish: five arms
			for (int a = 0; a < 5; ++a) {
				const double ang = 2.0 * kPi * a / 5.0;
				add(stars, sdf::tubeMesh({{0.0, 0.006, 0.0}, {0.05 * std::cos(ang), 0.004, 0.05 * std::sin(ang)}}, 0.009, 0.3, 6), m);
			}
		} else {
			add(dollars, disc(0.025, 0.004, 14), m);
		}
	}
	return "# shells (film_treasure.h)\n" + attr(coated({0.92, 0.75, 0.62}, 0.4), emitMesh(scallops, identity())) + attr(coated({0.8, 0.6, 0.42}, 0.3), emitMesh(cones, identity())) +
		   attr(diffuseMaterial({0.85, 0.35, 0.15}), emitMesh(stars, identity())) + attr(diffuseMaterial({0.85, 0.82, 0.74}), emitMesh(dollars, identity()));
}

inline std::string text(const World &w, double t, double level, const std::function<double(double, double)> &bedAt, const underwater::Current &c) {
	std::string out;
	for (const Chest &ch : w.chests) out += chestText(ch, t, bedAt, level, c);
	for (const Clam &cl : w.clams) out += clamText(cl, t, bedAt);
	for (const Bottle &b : w.bottles) out += bottleText(b, t, level, bedAt, c);
	for (const Shells &sh : w.shells) out += shellsText(sh, bedAt);
	for (const Sink &s : w.sinks) out += sinkText(s, t, level, bedAt, c);
	return out;
}

}  // namespace treasure
}  // namespace cartoon
