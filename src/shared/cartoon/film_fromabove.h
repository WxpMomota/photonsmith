#pragma once
// film_fromabove.h -- under the sea, round 18: things from the world above, and the strange places of the deep.
//
//   hook X Z at T [depth D] [yank T] [seed K]        a fishing line coming down from the surface with a worm wriggling on its hook, jigging D metres above the seabed (2);
//                                                    at `yank` it is snatched back up
//   net X Z at T [size S] [school N] [lift T]        a net sinking from the surface, S metres across (8), its weights round its rim; at `lift` it closes and is hauled up,
//                                                    and the fish of school N inside it go up with it
//   anchor X Z at T [chain]                          an anchor dropped from above on its chain, landing in a cloud of silt, its chain up to the surface
//   (and round 18 lets the water rounds' boats float over an underwater film: `boat rowboat NAME ...` seen from below, oars dipping; ducks paddle their feet)
//
//   brinepool X Z [radius R] [seed K]                a lake on the seabed: brine heavier than the sea, its surface a shining line with slow waves, mussels round its shore
//   pillowlava X Z [radius R] [seed K]               lava on the sea floor in pillows: black crusts, the new ones split and glowing orange, bubbles and steam rising off them
//   brinicle X Z at T [length L]                     a finger of ice growing down from the ice overhead (it needs `icecap`) over twenty seconds, and spreading its web of ice
//                                                    over the seabed where it touches
//   turbidity X Z toward A at T [size S] [speed V]   an underwater avalanche: a cloud of sediment pouring down the slope, rolling and spreading
//
// pbrt's x z (film_parse_sea.h); the cpp passes the seabed's height and the surface.

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

#include "film_deepsea.h"
#include "film_flow.h"
#include "film_sealife.h"
#include "film_school.h"
#include "film_treasure.h"
#include "film_undersea.h"
#include "nature_lib.h"
#include "sdf_mesh.h"

namespace cartoon {
namespace fromabove {

struct Hook {
	double x = 0.0, z = 0.0, at = 0.0, depth = 2.0, yank = 1e30;
	int seed = 1;
};
struct Net {
	double x = 0.0, z = 0.0, at = 0.0, size = 8.0, lift = 1e30;
	int school = 0, seed = 1;
};
struct Anchor {
	double x = 0.0, z = 0.0, at = 0.0;
};
struct Brine {
	double x = 0.0, z = 0.0, radius = 6.0;
	int seed = 1;
};
struct Lava {
	double x = 0.0, z = 0.0, radius = 5.0;
	int seed = 1;
};
struct Brinicle {
	double x = 0.0, z = 0.0, at = 0.0, length = 0.0;   // (0: down to the seabed)
};
struct Slump {
	double x = 0.0, z = 0.0, toward = 0.0, at = 0.0, size = 1.0, speed = 4.0;
};
struct World {
	std::vector<Hook> hooks;
	std::vector<Net> nets;
	std::vector<Anchor> anchors;
	std::vector<Brine> brines;
	std::vector<Lava> lavas;
	std::vector<Brinicle> brinicles;
	std::vector<Slump> slumps;
	bool any() const { return !hooks.empty() || !nets.empty() || !anchors.empty() || !brines.empty() || !lavas.empty() || !brinicles.empty() || !slumps.empty(); }
};

namespace detail {
using deepsea::detail::add;
using sealife::detail::story;
inline Mesh ellipsoid(Vec3 c, Vec3 r, int nu = 14, int nv = 8) {
	Mesh m;
	add(m, uvSphere(1.0, nu, nv), mul(translate(c), scale(r.x, r.y, r.z)));
	return m;
}
}  // namespace detail

// ---- a hook and line ----
// How far below the surface the hook is at t (pbrt's y), going down at half a metre a second to its depth, jigging, snatched up at `yank`.
inline double hookY(const Hook &h, double t, double level, double bed) {
	const double bottom = bed + h.depth;
	double y = std::max(bottom, level - 0.5 * std::max(0.0, t - h.at));
	if (y <= bottom + 1e-6) y += 0.15 * std::max(0.0, std::sin(2.0 * kPi * 0.4 * t + h.seed));   // (jigging)
	if (t > h.yank) y += 3.0 * (t - h.yank) * (t - h.yank) + 0.5 * (t - h.yank);
	return y;
}
inline std::string hookText(const Hook &h, double t, double level, double bed) {
	using namespace detail;
	if (t < h.at) return std::string();
	const double y = hookY(h, t, level, bed);
	if (y > level + 0.3) return std::string();
	const Vec3 eye{h.x + 0.03 * std::sin(0.7 * t), y, h.z};
	Mesh line, steel, worm, lead;
	add(line, sdf::tubeMesh({story({h.x, level + 0.5, h.z}), story(eye + Vec3{0.0, 0.07, 0.0})}, 0.0012, 1.0, 4));
	add(lead, ellipsoid(story(eye + Vec3{0.0, 0.25, 0.0}), {0.012, 0.018, 0.012}, 8, 6));
	std::vector<Vec3> bend;   // the hook: down its shank, round the bend, up to its barb
	for (int q = 0; q <= 10; ++q) {
		const double u = q / 10.0;
		if (u < 0.5) bend.push_back(eye + Vec3{0.0, 0.07 - 0.14 * u, 0.0});
		else {
			const double a = kPi * (u - 0.5) / 0.5 * 1.1;
			bend.push_back(eye + Vec3{0.0, -0.025 - 0.02 * std::sin(a), 0.02 - 0.02 * std::cos(a)});
		}
	}
	for (Vec3 &p : bend) p = story(p);
	add(steel, sdf::tubeMesh(bend, 0.0018, 0.6, 5));
	std::vector<Vec3> w;   // the worm, wriggling
	for (int q = 0; q <= 10; ++q) {
		const double u = q / 10.0;
		w.push_back(story(eye + Vec3{0.012 * std::sin(9.0 * t + 6.0 * u), -0.04 - 0.05 * u, 0.025 + 0.02 * std::sin(7.0 * t - 5.0 * u + h.seed)}));
	}
	add(worm, sdf::tubeMesh(w, 0.004, 0.6, 6));
	std::string out = "# a hook and line (film_fromabove.h)\n";
	out += attr(diffuseMaterial({0.85, 0.85, 0.8}), emitMesh(line, identity()));
	out += attr(metalMaterial({0.7, 0.7, 0.72}, 0.15), emitMesh(steel, identity()) + emitMesh(lead, identity()));
	out += attr(coated({0.75, 0.35, 0.35}, 0.3), emitMesh(worm, identity()));
	return out;
}

// ---- a net ----
// The net's middle at t (pbrt's), how open it is (1 spread, 0 drawn shut), sinking from the surface to its school's depth, hauled up at `lift`.
inline Vec3 netAt(const Net &n, double t, double level, double depthTo, double &open) {
	const double down = level - std::min(depthTo, 0.4 * std::max(0.0, t - n.at));
	open = 1.0 - smoothstep(n.lift, n.lift + 2.0, t);
	double y = std::max(down, level - depthTo);
	if (t > n.lift) y += 0.8 * (t - n.lift);
	return {n.x, std::min(y, level + 1.0), n.z};
}
inline std::string netText(const Net &n, double t, double level, double depthTo) {
	using namespace detail;
	if (t < n.at) return std::string();
	double open = 1.0;
	const Vec3 c = netAt(n, t, level, depthTo, open);
	if (c.y > level + 0.8) return std::string();
	const double R = 0.5 * n.size;
	Mesh strings, weights, floats;
	const int around = 24, down = 10;
	const auto knot = [&](int a, int d) {   // (a bag: wide at its mouth, drawn in as it closes; its mouth hangs below its middle, sagging and waving)
		const double ang = 2.0 * kPi * a / around, u = static_cast<double>(d) / down;
		const double r = R * (1.0 - 0.75 * u * u) * lerp(0.25, 1.0, open) * (1.0 - 0.5 * (1.0 - open) * (1.0 - u));
		return story(c + Vec3{r * std::cos(ang) + 0.1 * std::sin(1.3 * t + a), -1.5 * u * R * 0.5 + 0.4 * R * (1.0 - u) * (1.0 - open) + 0.15 * std::sin(1.1 * t + 0.6 * a), r * std::sin(ang)});
	};
	for (int a = 0; a < around; ++a) {
		std::vector<Vec3> line;
		for (int d = 0; d <= down; ++d) line.push_back(knot(a, d));
		add(strings, sdf::tubeMesh(line, 0.014, 1.0, 4));
	}
	for (int d = 0; d <= down; ++d) {
		std::vector<Vec3> ring;
		for (int a = 0; a <= around; ++a) ring.push_back(knot(a % around, d));
		add(strings, sdf::tubeMesh(ring, d == 0 ? 0.03 : 0.014, 1.0, 4));
	}
	for (int a = 0; a < around; a += 2) add(weights, ellipsoid(knot(a, 0), {0.05, 0.05, 0.05}, 8, 6));
	add(strings, sdf::tubeMesh({knot(0, 0), story({c.x, level + 1.0, c.z})}, 0.012, 1.0, 5));   // (its line up to the boat)
	std::string out = "# a net (film_fromabove.h)\n";
	out += attr(diffuseMaterial({0.85, 0.6, 0.25}), emitMesh(strings, identity()));   // (an orange net: it is seen against the blue)
	out += attr(metalMaterial({0.35, 0.35, 0.36}, 0.4), emitMesh(weights, identity()));
	return out;
}
// The school's fish inside the net when it closes, hauled up with it (its worked-out swim: the fish within the net's mouth at `lift` follow it up).
inline void haul(school::School &sc, const Net &n, double level, double depthTo) {
	if (sc.pos.size() < 3 || n.lift >= 1e29) return;
	const std::size_t k0 = std::min(sc.pos.size() - 1, static_cast<std::size_t>(std::max(0.0, n.lift / sc.dt)));
	double open = 1.0;
	const Vec3 c0 = netAt(n, n.lift, level, depthTo, open);
	std::vector<std::size_t> caught;
	for (std::size_t i = 0; i < static_cast<std::size_t>(sc.count); ++i)
		if (length(sc.pos[k0][i] - (c0 + Vec3{0.0, -0.35 * n.size, 0.0})) < 0.6 * n.size) caught.push_back(i);
	for (std::size_t k = k0; k < sc.pos.size(); ++k) {
		const double t = k * sc.dt;
		const Vec3 c = netAt(n, t, level, depthTo, open);
		const double b = smoothstep(n.lift, n.lift + 2.0, t);
		for (const std::size_t i : caught) {
			const double ph = 2.0 * kPi * (0.7 * t + hash01(static_cast<int>(i), 51));
			const Vec3 inBag = c + Vec3{0.3 * n.size * 0.5 * (hash01(static_cast<int>(i), 52) - 0.5) + 0.08 * std::cos(ph), -0.45 * n.size * 0.5 * hash01(static_cast<int>(i), 53), 0.3 * n.size * 0.5 * (hash01(static_cast<int>(i), 54) - 0.5) + 0.08 * std::sin(ph)};
			sc.pos[k][i] = lerp(sc.pos[k][i], inBag, b);
			if (inBag.y > level - 0.1) sc.pos[k][i].y = level - 0.1;   // (out of the water with the net: held at the surface, then gone with it)
		}
	}
	for (std::size_t k = k0; k < sc.pos.size(); ++k)
		for (const std::size_t i : caught) {
			const std::size_t a = k == 0 ? 0 : k - 1, b = std::min(sc.pos.size() - 1, k + 1);
			const Vec3 v = (sc.pos[b][i] - sc.pos[a][i]) * (1.0 / ((b - a) * sc.dt));
			if (length(v) > 0.05) sc.vel[k][i] = v;
		}
}

// ---- an anchor ----
// Where the anchor is at t (pbrt's y) and whether it has landed (and when).
inline double anchorY(const Anchor &a, double t, double level, double bed, double &landed) {
	const treasure::Fall f = treasure::fallFor("anchor");
	const double drop = level - bed - 0.3;
	landed = 1e30;
	if (t < a.at) return level + 2.0;
	double lo = 0.0, hi = 120.0;
	for (int k = 0; k < 50; ++k) (treasure::fallOf(0.5 * (lo + hi), 0.0, f.vt, f.tc) < drop ? lo : hi) = 0.5 * (lo + hi);
	landed = a.at + hi;
	return std::max(bed + 0.3, level - treasure::fallOf(t - a.at, 0.0, f.vt, f.tc));
}
inline std::string anchorText(const Anchor &a, double t, double level, double bed) {
	using namespace detail;
	double landed = 1e30;
	const double y = anchorY(a, t, level, bed, landed);
	if (y > level) return std::string();
	const double tilt = t >= landed ? 62.0 * smoothstep(landed, landed + 0.6, t) : 4.0 * std::sin(3.0 * t);
	const Mat4 place = mul(translate(story({a.x, y, a.z})), rotate({0, 0, 1}, tilt));
	Mesh iron, chain;
	add(iron, sdf::tubeMesh({{0.0, 0.0, 0.0}, {0.0, 1.1, 0.0}}, 0.05, 1.0, 8));
	add(iron, sdf::tubeMesh({{-0.45, 0.45, 0.0}, {-0.4, 0.15, 0.0}, {-0.2, 0.0, 0.0}, {0.0, -0.05, 0.0}, {0.2, 0.0, 0.0}, {0.4, 0.15, 0.0}, {0.45, 0.45, 0.0}}, 0.05, 0.6, 8));
	for (const double side : {1.0, -1.0}) add(iron, ellipsoid({side * 0.47, 0.5, 0.0}, {0.12, 0.08, 0.04}, 10, 6));   // (its flukes)
	add(iron, sdf::tubeMesh({{-0.3, 0.95, 0.0}, {0.3, 0.95, 0.0}}, 0.035, 1.0, 6));   // (its stock)
	add(iron, torus(0.08, 0.02, 14, 6), mul(translate(0.0, 1.18, 0.0), rotate({1, 0, 0}, 90.0)));
	const Vec3 ring = transformPoint(place, {0.0, 1.18, 0.0});
	const double slack = t >= landed ? 1.5 : 0.2;
	for (int q = 0; q < 80; ++q) {   // its chain, link by link, up to the surface
		const double u = q / 80.0, yy = lerp(ring.y, level + 0.2, u);
		if (yy - ring.y > 18.0) break;
		const Vec3 at{ring.x + slack * std::sin(kPi * u) * 0.8, yy, ring.z + slack * std::sin(kPi * u) * 0.5};
		add(chain, torus(0.07, 0.016, 12, 5), mul(translate(at), rotate({0, 1, 0}, q % 2 == 0 ? 0.0 : 90.0), rotate({1, 0, 0}, 90.0)));
	}
	return "# an anchor (film_fromabove.h)\n" + attr(metalMaterial({0.25, 0.23, 0.22}, 0.5), emitMesh(iron, place) + emitMesh(chain, identity()));
}

// ---- a brine pool ----
inline std::string brineText(const Brine &b, double t, const std::function<double(double, double)> &bedAt) {
	using namespace detail;
	const double R = b.radius, y = bedAt(b.x, b.z) + 0.03;   // (just over the sand: the plain seabed would hide it a hair under)
	const int rings = 14, around = 48;
	const auto shore = [&](double a) { return R * (1.0 + 0.18 * (nature::noise2(std::cos(a) * 1.5 + b.seed, std::sin(a) * 1.5, b.seed) - 0.5)); };
	Mesh surface, mussels;
	surface.verts.push_back(story({b.x, y, b.z})), surface.normals.push_back({0.0, 1.0, 0.0});
	for (int r = 1; r <= rings; ++r)
		for (int a = 0; a < around; ++a) {
			const double ang = 2.0 * kPi * a / around, rr = shore(ang) * r / rings;
			const double wave = 0.02 * std::sin(1.4 * rr - 0.5 * t + b.seed) * (r < rings ? 1.0 : 0.0);
			surface.verts.push_back(story({b.x + rr * std::cos(ang), y + wave, b.z + rr * std::sin(ang)}));
			surface.normals.push_back(normalize(Vec3{0.028 * std::cos(1.4 * rr - 0.5 * t + b.seed) * std::cos(ang), 1.0, -0.028 * std::cos(1.4 * rr - 0.5 * t + b.seed) * std::sin(ang)}));
		}
	for (int a = 0; a < around; ++a) surface.tris.push_back({0, 1 + a, 1 + (a + 1) % around});
	for (int r = 1; r < rings; ++r)
		for (int a = 0; a < around; ++a) {
			const int p = 1 + (r - 1) * around + a, q = 1 + (r - 1) * around + (a + 1) % around;
			surface.tris.push_back({p, p + around, q + around});
			surface.tris.push_back({p, q + around, q});
		}
	for (int q = 0; q < 260; ++q) {   // mussels crowding its shore
		const double ang = 2.0 * kPi * hash01(q, b.seed + 1), rr = shore(ang) * (1.0 + 0.12 * hash01(q, b.seed + 2));
		const double mx = b.x + rr * std::cos(ang), mz = b.z + rr * std::sin(ang);
		add(mussels, ellipsoid(story({mx, bedAt(mx, mz) + 0.03, mz}), {0.05, 0.03, 0.08}, 8, 5), rotate({0, 1, 0}, 0.0));
	}
	std::string out = "# a brine pool (film_fromabove.h)\n";
	out += attr("Material \"conductor\" \"rgb reflectance\" [ 0.55 0.66 0.74 ] \"float roughness\" [ 0.04 ]", emitMesh(surface, identity()));   // (the brine's surface: a silver mirror of the water above, as it looks from a little way off)
	out += attr(coated({0.06, 0.07, 0.1}, 0.2), emitMesh(mussels, identity()));
	return out;
}

// ---- pillow lava ----
inline std::string lavaText(const Lava &l, double t, const std::function<double(double, double)> &bedAt, double level, const underwater::Current &c, std::vector<undersea::Bubble> &bubbles) {
	using namespace detail;
	Mesh crust, cracks;
	for (int q = 0; q < 46; ++q) {
		const int s = l.seed * 53 + q;
		const double a = 2.0 * kPi * hash01(s, 1), r = l.radius * std::sqrt(hash01(s, 2)), size = 0.4 + 0.5 * hash01(s, 3);
		const double px = l.x + r * std::cos(a), pz = l.z + r * std::sin(a), py = bedAt(px, pz) + 0.45 * size * (1.0 + 0.8 * (1.0 - r / l.radius));
		Mesh pillow = uvSphere(1.0, 16, 10);
		pillow.uvs.clear();
		for (Vec3 &v : pillow.verts) v = v + v * (0.08 * (nature::noise2(v.x * 3.0 + s, v.z * 3.0 + v.y * 2.0, s) - 0.5));
		add(crust, pillow, mul(translate(story({px, py, pz})), rotate({0, 1, 0}, 360.0 * hash01(s, 4)), scale(size * 1.3, size * 0.75, size)));
		if (hash01(s, 5) < 0.35) {   // a new one: split open and glowing
			for (int k = 0; k < 3; ++k) {
				const double b = 2.0 * kPi * hash01(s, 6 + k);
				std::vector<Vec3> crack;
				for (int j = 0; j <= 5; ++j) crack.push_back(story({px + size * 1.25 * std::cos(b + 0.3 * j), py + size * (0.5 - 0.15 * j), pz + size * 0.95 * std::sin(b + 0.3 * j)}));
				add(cracks, sdf::tubeMesh(crack, 0.04 * size, 0.4, 5));
			}
			for (int k = 0; k < 6; ++k) {   // (steam and bubbles off it)
				const double born = std::floor(t * 2.0) / 2.0 - 0.5 * k;
				const int key = s * 31 + static_cast<int>(born * 2.0);
				undersea::Bubble bb;
				if (undersea::bubbleAt({px + 0.3 * size * (hash01(key, 1) - 0.5), py + 0.6 * size, pz + 0.3 * size * (hash01(key, 2) - 0.5)}, 0.002 + 0.004 * hash01(key, 3), t - born, t, key, level, c, bb)) bubbles.push_back(bb);
			}
		}
	}
	const double flicker = 0.85 + 0.15 * std::sin(7.0 * t + l.seed) * std::sin(3.1 * t);
	const Vec3 L{9.0 * flicker, 2.6 * flicker, 0.4 * flicker};
	std::string out = "# pillow lava (film_fromabove.h)\n";
	out += attr(coated({0.04, 0.035, 0.035}, 0.3), emitMesh(crust, identity()));
	if (!cracks.tris.empty()) out += flow::glowAttr(emitMesh(cracks, identity()), L);
	char buf[200];
	std::snprintf(buf, sizeof buf, "AttributeBegin\n  LightSource \"point\" \"point3 from\" [ %.4f %.4f %.4f ] \"rgb I\" [ %.3f %.3f %.3f ]\nAttributeEnd\n", l.x, bedAt(l.x, l.z) + 1.0, l.z, 9.0 * flicker, 3.0 * flicker, 0.6 * flicker);
	return out + buf;
}

// ---- a brinicle ----
inline std::string brinicleText(const Brinicle &b, double t, double top, const std::function<double(double, double)> &bedAt) {
	using namespace detail;
	if (t < b.at) return std::string();
	const double bed = bedAt(b.x, b.z), full = b.length > 0.0 ? std::min(b.length, top - bed) : top - bed;
	const double grown = full * smoothstep(0.0, 20.0, t - b.at), touched = b.length > 0.0 && b.length < top - bed ? 0.0 : smoothstep(18.0, 40.0, t - b.at);
	Mesh ice, web;
	std::vector<Vec3> line;
	std::vector<double> radii;
	for (int q = 0; q <= 20; ++q) {
		const double u = q / 20.0;
		line.push_back(story({b.x + 0.15 * std::sin(5.0 * u + b.at), top - grown * u, b.z + 0.1 * std::cos(4.0 * u)}));
		radii.push_back(lerp(0.35, 0.08, u) * (1.0 + 0.2 * std::sin(17.0 * u)));
	}
	add(ice, flow::tube(line, radii, 12));
	for (int k = 0; k < 14 && touched > 0.0; ++k) {   // its web of ice, spreading over the seabed
		const double a = 2.0 * kPi * (k + 0.3 * hash01(k, 3)) / 14.0, reach = (1.5 + 2.5 * hash01(k, 4)) * touched;
		std::vector<Vec3> twig;
		for (int j = 0; j <= 8; ++j) {
			const double u = j / 8.0, x = b.x + reach * u * std::cos(a + 0.3 * std::sin(4.0 * u + k)), z = b.z + reach * u * std::sin(a + 0.3 * std::sin(4.0 * u + k));
			twig.push_back(story({x, bedAt(x, z) + 0.03, z}));
		}
		add(web, sdf::tubeMesh(twig, 0.03, 0.4, 5));
	}
	std::string out = "# a brinicle (film_fromabove.h)\n";
	out += attr("Material \"dielectric\" \"float eta\" [ 1.31 ] \"float roughness\" [ 0.25 ]", emitMesh(ice, identity()));
	if (!web.tris.empty()) out += attr(diffuseMaterial({0.85, 0.9, 0.95}), emitMesh(web, identity()));
	return out;
}

// ---- an underwater avalanche ----
// The front of the slump at t, and how far it has gone; nothing before T or 40 s after.
inline std::string slumpText(const Slump &s, double t, const std::function<double(double, double)> &bedAt) {
	const double age = t - s.at;
	if (age < 0.0 || age > 40.0) return std::string();
	const double a = s.toward * kPi / 180.0, gone = s.speed * age * (1.0 - 0.012 * age);
	std::string out;
	for (int k = 0; k < 9; ++k) {   // (puffs of sediment along the way it has come, the front the biggest, rolling and spreading)
		const double back = gone * k / 9.0, side = (k % 3 - 1) * 1.5 * s.size;
		const double x = s.x + std::sin(a) * (gone - back) + std::cos(a) * side, z = s.z + std::cos(a) * (gone - back) - std::sin(a) * side;
		out += undersea::siltText({x, bedAt(x, z), z}, std::min(20.0, age - back / std::max(0.1, s.speed) + 0.5), s.size * (1.6 - 0.1 * k), 401 + k * 17 + static_cast<int>(s.at));
	}
	return out.empty() ? out : "# an underwater avalanche (film_fromabove.h)\n" + out;
}

}  // namespace fromabove
}  // namespace cartoon
