#pragma once
// film_airpocket.h -- under the sea, round 18: air under the water.
//
//   divingbell X Y Z [size S] [lamp L] [seed K]   an old iron bell (S metres across, 2.4 by default) hanging on its chain from the surface, open underneath: inside it a
//                                                 pocket of air whose floor is the water's own surface - silver from below, as every surface seen from under the water at
//                                                 a slant is - lit by a lamp; a few bubbles spill from its rim now and then
//   aircave X Y Z [radius R] [lamp L] [seed K]     a dome of rock over a pocket of air (its surface at Y above the seabed), the same silver ceiling of water
//   helmet WHO [size S]                            a glass bubble helmet over a character's head (dry inside: it talks without bubbles), with a brass collar
//   bigbubble X Y Z [radius R] [at T] [rise S] [wobble W] [seed K]   one great bubble, R metres (0.05 to 3), wobbling as it goes up from T at S m/s (by its size), bending
//                                                 what is seen through it; gone at the surface
//   blowring WHO at T                              a character blows a bubble ring (round 14's, film_undersea.h) from its mouth at T
//
// Heights above the seabed, pbrt's x z (film_parse_sea.h). The air's surfaces are dielectric with the bubbles' eta (air in water, 0.7502).

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

#include "film_deepsea.h"
#include "film_flow.h"
#include "film_sealife.h"
#include "film_undersea.h"
#include "film_underwater.h"
#include "nature_lib.h"
#include "sdf_mesh.h"

namespace cartoon {
namespace airpocket {

struct Bell {
	Vec3 at;   // (its rim's middle)
	double size = 2.4, lamp = 1.0;
	int seed = 1;
};
struct Cave {
	Vec3 at;   // (the middle of its surface)
	double radius = 4.0, lamp = 0.0;
	int seed = 1;
};
struct Helmet {
	std::string who;
	double size = 0.0;   // (0: by the wearer's head)
};
struct Big {
	Vec3 at;
	double radius = 0.5, t = 0.0, rise = -1.0, wobble = 1.0;
	int seed = 1;
};
struct Ring {
	std::string who;
	double t = 0.0;
};
struct World {
	std::vector<Bell> bells;
	std::vector<Cave> caves;
	std::vector<Helmet> helmets;
	std::vector<Big> bigs;
	std::vector<Ring> rings;
	bool any() const { return !bells.empty() || !caves.empty() || !helmets.empty() || !bigs.empty() || !rings.empty(); }
};
inline constexpr const char *kAirSurface = "Material \"dielectric\" \"float eta\" [ 0.7502 ]";

namespace detail {
using deepsea::detail::add;
using sealife::detail::story;
inline Mesh ellipsoid(Vec3 c, Vec3 r, int nu = 20, int nv = 12) {
	Mesh m;
	add(m, uvSphere(1.0, nu, nv), mul(translate(c), scale(r.x, r.y, r.z)));
	return m;
}
// A disc of water's surface, facing down (the water below it, the air above), gently stirred, R across, at story-space c.
inline Mesh surfaceDisc(Vec3 c, double R, double t, int seed) {
	Mesh m;
	const int rings = 10, around = 36;
	m.verts.push_back(c), m.normals.push_back({0.0, -1.0, 0.0});
	for (int r = 1; r <= rings; ++r)
		for (int a = 0; a < around; ++a) {
			const double rr = R * r / rings, aa = 2.0 * kPi * a / around;
			const double h = 0.006 * std::sin(6.0 * rr - 1.7 * t + seed) * (r < rings ? 1.0 : 0.0);
			m.verts.push_back(c + Vec3{rr * std::cos(aa), h, rr * std::sin(aa)}), m.normals.push_back(normalize(Vec3{-0.036 * std::cos(6.0 * rr - 1.7 * t + seed) * std::cos(aa), -1.0, -0.036 * std::cos(6.0 * rr - 1.7 * t + seed) * std::sin(aa)}));
		}
	for (int a = 0; a < around; ++a) m.tris.push_back({0, 1 + (a + 1) % around, 1 + a});
	for (int r = 1; r < rings; ++r)
		for (int a = 0; a < around; ++a) {
			const int p = 1 + (r - 1) * around + a, q = 1 + (r - 1) * around + (a + 1) % around;
			m.tris.push_back({p, q + around, p + around});
			m.tris.push_back({p, q, q + around});
		}
	return m;
}
}  // namespace detail

// A diving bell (see the top) at nature's time t; `level` the surface its chain goes up to, `c` the current (its spilled bubbles).
inline std::string bellText(const Bell &b, double t, double level, const underwater::Current &c, std::vector<undersea::Bubble> &bubbles) {
	using namespace detail;
	const double R = 0.5 * b.size, H = 0.95 * b.size, sway = 0.6 * std::sin(0.35 * t + b.seed);
	const Mat4 place = mul(translate(story(b.at)), rotate({0, 0, 1}, sway), rotate({1, 0, 0}, 0.5 * std::cos(0.3 * t + b.seed)));
	Mesh iron = lathe({{R, 0.0}, {1.04 * R, 0.06 * H}, {0.98 * R, 0.4 * H}, {0.82 * R, 0.78 * H}, {0.5 * R, 0.97 * H}, {0.0, H}}, 36), rivets, glass, lampM, chain;
	iron.uvs.clear();
	for (int q = 0; q < 24; ++q) {   // (rivets in a band, and round portholes)
		const double a = 2.0 * kPi * q / 24.0;
		add(rivets, ellipsoid({1.0 * R * std::cos(a), 0.2 * H, 1.0 * R * std::sin(a)}, {0.025, 0.025, 0.025}, 6, 4));
	}
	for (int q = 0; q < 4; ++q) {
		const double a = 2.0 * kPi * q / 4.0 + 0.4;
		add(glass, ellipsoid({0.9 * R * std::cos(a), 0.55 * H, 0.9 * R * std::sin(a)}, {0.11 * R + 0.02, 0.11 * R + 0.02, 0.11 * R + 0.02}, 12, 8));
	}
	add(lampM, ellipsoid({0.0, 0.8 * H, 0.0}, {0.07, 0.09, 0.07}, 10, 8));
	const Vec3 top = transformPoint(place, {0.0, H, 0.0});
	const double rise = level + 0.3 - top.y, linkStep = 0.09;   // (a link is 0.124 tall: a step under that keeps them through each other, at any depth)
	const int links = rise > 0.0 ? std::min(134, static_cast<int>(rise / linkStep) + 1) : 0;   // (a long way up: the camera sees only the lower 12 m of it)
	for (int q = 0; q < links; ++q) {   // its chain, up to the surface: links in turn across each other
		const double u = std::min(1.0, q * linkStep / rise), y = top.y + q * linkStep;
		const Vec3 at{top.x + 0.3 * std::sin(kPi * u) * sway, y, top.z};
		Mesh link = torus(0.05, 0.012, 12, 6);
		add(chain, link, mul(translate(at), rotate({0, 1, 0}, q % 2 == 0 ? 0.0 : 90.0), rotate({1, 0, 0}, 90.0)));
	}
	std::string out = "# a diving bell (film_airpocket.h)\n";
	out += attr(metalMaterial({0.32, 0.3, 0.28}, 0.45), emitMesh(iron, place) + emitMesh(rivets, place));
	out += attr("Material \"thindielectric\" \"float eta\" [ 1.5 ]", emitMesh(glass, place));
	if (!chain.tris.empty()) out += attr(metalMaterial({0.35, 0.33, 0.3}, 0.4), emitMesh(chain, identity()));
	// the air inside: its floor the water's surface, half way up the bell
	out += attr(kAirSurface, emitMesh(surfaceDisc({0.0, 0.42 * H, 0.0}, 0.94 * R, t, b.seed), place));
	if (b.lamp > 0.0) {
		const Vec3 L{4.0 * b.lamp, 3.0 * b.lamp, 1.6 * b.lamp};
		out += flow::glowAttr(emitMesh(lampM, place), L);
		const Vec3 l = story(transformPoint(place, {0.0, 0.7 * H, 0.0}));
		char buf[200];
		std::snprintf(buf, sizeof buf, "AttributeBegin\n  LightSource \"point\" \"point3 from\" [ %.4f %.4f %.4f ] \"rgb I\" [ %.3f %.3f %.3f ]\nAttributeEnd\n", l.x, l.y, l.z, 1.8 * b.lamp, 1.3 * b.lamp, 0.7 * b.lamp);
		out += buf;
	}
	for (int burst = 0; burst < 4; ++burst) {   // a few big bubbles spilling from its rim as it rocks, every six seconds
		const int window = static_cast<int>(std::floor(t / 6.0)) - burst;   // (its own six seconds: the same burst from one frame to the next, not one that moved on every six)
		const double when = 6.0 * window + 2.0 * hash01(window, b.seed);
		for (int q = 0; q < 6; ++q) {
			const int key = static_cast<int>(when * 10.0) * 13 + q + b.seed;
			const double a = 2.0 * kPi * hash01(key, 3);
			const Vec3 rim = story(transformPoint(place, {1.02 * R * std::cos(a), 0.02, 1.02 * R * std::sin(a)}));
			undersea::Bubble bb;
			if (undersea::bubbleAt(rim, 0.006 + 0.01 * hash01(key, 4), t - when - 0.05 * q, t, key, level, c, bb)) bubbles.push_back(bb);
		}
	}
	return out;
}

// An air cave (see the top) at t: a thick hood of rock over the air, standing on legs of rock down to the seabed (`bed`, pbrt's y under it) - an overhang, open below.
inline std::string caveText(const Cave &v, double t, double bed) {
	using namespace detail;
	const double R = v.radius;
	Mesh rock = lathe({{1.15 * R, -0.6}, {1.05 * R, 0.1}, {0.95 * R, 0.45 * R}, {0.6 * R, 0.8 * R}, {0.0, 0.92 * R}, {0.0, 1.35 * R}, {0.75 * R, 1.2 * R}, {1.35 * R, 0.7 * R}, {1.55 * R, 0.0},
					   {1.3 * R, -0.75}, {1.15 * R, -0.6}}, 40);
	rock.uvs.clear();
	for (int k = 0; k < 4; ++k) {   // (its legs: lumpy pillars from under its rim to the seabed)
		const double a = 2.0 * kPi * (k + 0.2 * hash01(k, v.seed)) / 4.0 + 0.4;
		std::vector<Vec3> leg;
		std::vector<double> radii;
		for (int j = 0; j <= 8; ++j) {
			const double u = j / 8.0;
			leg.push_back({1.3 * R * std::cos(a) * (1.0 + 0.15 * u), -0.6 - u * (v.at.y - bed - 0.6 + 0.5), 1.3 * R * std::sin(a) * (1.0 + 0.15 * u)});
			radii.push_back((0.28 + 0.12 * u + 0.05 * std::sin(7.0 * u + k)) * R);
		}
		add(rock, flow::tube(leg, radii, 12));
	}
	for (std::size_t i = 0; i < rock.verts.size(); ++i) {   // (lumpy)
		const Vec3 p = rock.verts[i];
		rock.verts[i] = p + rock.normals[i] * (0.12 * R * (nature::noise2(p.x * 0.6 + v.seed, p.z * 0.6 + p.y * 0.4, v.seed) - 0.5));
	}
	const Mat4 place = translate(story(v.at));
	std::string out = "# an air cave (film_airpocket.h)\n";
	out += attr(diffuseMaterial({0.28, 0.25, 0.22}), emitMesh(rock, place));
	out += attr(kAirSurface, emitMesh(surfaceDisc({0.0, 0.0, 0.0}, 1.02 * R, t, v.seed), place));
	if (v.lamp > 0.0) {
		const Vec3 l = v.at + Vec3{0.0, 0.5 * R, 0.0};   // (pbrt's)
		char buf[200];
		std::snprintf(buf, sizeof buf, "AttributeBegin\n  LightSource \"point\" \"point3 from\" [ %.4f %.4f %.4f ] \"rgb I\" [ %.3f %.3f %.3f ]\nAttributeEnd\n", l.x, l.y, l.z, 3.0 * v.lamp, 2.2 * v.lamp, 1.2 * v.lamp);
		out += buf;
	}
	return out;
}

// A glass bubble helmet round a head at `head` (story space, its middle) of radius `r`, facing `fwd` (story space).
inline std::string helmetText(Vec3 head, double r, Vec3 fwd) {
	using namespace detail;
	Mesh dome = uvSphere(r, 32, 20), collar;
	dome.uvs.clear();
	std::vector<Vec3> ring;
	for (int q = 0; q <= 32; ++q) ring.push_back({0.82 * r * std::cos(2.0 * kPi * q / 32), -0.62 * r, 0.82 * r * std::sin(2.0 * kPi * q / 32)});
	add(collar, sdf::tubeMesh(ring, 0.12 * r, 1.0, 8));
	const Mat4 place = mul(translate(head), sealife::detail::facingOf(fwd));
	return "# a bubble helmet (film_airpocket.h)\n" + attr("Material \"thindielectric\" \"float eta\" [ 1.5 ]", emitMesh(dome, place)) + attr(metalMaterial({0.85, 0.62, 0.32}, 0.25), emitMesh(collar, place));
}

// Where a big bubble is at nature's time t (pbrt's) and how big, or false (not yet blown, or at the surface).
inline bool bigAt(const Big &b, double t, double level, const underwater::Current &c, Vec3 &at, double &r) {
	const double age = t - b.t;
	if (age < 0.0) return false;
	const double speed = b.rise > 0.0 ? b.rise : std::min(0.9, undersea::riseSpeed(b.radius) * 0.6);   // (a great one rises slower than its size says: it is a cartoon's)
	const Vec3 drift = c.drift(t) - c.drift(b.t);
	at = b.at + Vec3{drift.x + 0.08 * b.radius * std::sin(1.3 * age + b.seed), speed * age, drift.z + 0.08 * b.radius * std::cos(1.1 * age)};
	r = b.radius * std::cbrt((10.3 + std::max(0.0, level - b.at.y)) / (10.3 + std::max(0.0, level - at.y)));
	return at.y + r < level - 0.02;
}
// A big bubble at t: a ball wobbling in its low modes (squashed, rocking, a ripple round it), as scene text.
inline std::string bigText(const Big &b, double t, double level, const underwater::Current &c) {
	Vec3 at;
	double r = 0.0;
	if (!bigAt(b, t, level, c, at, r)) return std::string();
	Mesh m = uvSphere(1.0, 40, 24);
	m.uvs.clear();
	const double age = t - b.t, w = 0.06 * b.wobble;
	for (std::size_t i = 0; i < m.verts.size(); ++i) {
		const Vec3 n = m.verts[i];
		const double p2 = 0.5 * (3.0 * n.y * n.y - 1.0), ph = std::atan2(n.z, n.x);
		const double d = 1.0 - 0.12 * std::min(1.0, r) * n.y * std::abs(n.y)   // (flatter underneath, as a big bubble is)
						 + w * p2 * std::sin(2.0 * kPi * 1.1 * age + b.seed) + w * 0.7 * n.y * std::sin(ph * 3.0 + 3.0 * age) * (1.0 - n.y * n.y) + w * 0.5 * std::sin(ph * 2.0 - 2.3 * age) * (1.0 - n.y * n.y);
		m.verts[i] = n * (r * d);
	}
	sdf::computeNormals(m);
	return "# a big bubble (film_airpocket.h)\n" + attr(kAirSurface, undersea::detail::noShadow(emitMesh(m, translate(detail::story(at)))));
}

}  // namespace airpocket
}  // namespace cartoon
