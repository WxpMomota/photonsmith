#pragma once
// forest_dawn.h -- "Forest Dawn", a 42 second film with no characters and no words: a misty forest clearing from the blue before sunrise to the first warm light. The last fireflies
// drift out, the sun comes up behind the hills and its light falls between the trunks as shafts through the mist, dew drops on backlit grass catch it, leaves let go and turn over in the
// beams, and the camera rises as the mist thins. Five shots: a slow push into the blue clearing, low against the rising sun, a macro of dew on the grass, the falling leaves, a crane up.
//
// What it shows of the renderer: the camera inside a fog that ends at a sphere (nature_lib.h, Mist), so the sun outside it draws real shafts of light; leaves and grass that let the light
// through (diffusetransmission); a forest of a few thousand plants as placed objects (ObjectInstance); depth of field; and the picture's look (# @rt-look: bloom, vignette, warmth).
//
// The sun is one glowing sphere far beyond the mist (an emissive sphere is ONE light, sampled as a cone): it is both the disc you see and all the sunlight, so nothing is lit twice. The
// story space is +x to the right of the picture, +y up, +z towards the camera; the sun rises at -z, ahead of every shot but the last.

#include "nature_lib.h"

namespace cartoon {
namespace forest_dawn {

constexpr double kDuration = 42.0;
constexpr int kFps = 24;
constexpr int kSamples = 256;

using nature::Ground;
using nature::Mist;

inline const Ground &ground() {
	static const Ground g{0.55, 11.0, 7};
	return g;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The light over time
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------

// The sun's height above the horizon in degrees: below it for the first seconds, clearing the hills (half a degree to 3 degrees) between 7 and 13 s, rising more and more slowly after.
inline double sunElevation(double t) { return -4.0 + 15.0 * (1.0 - std::exp(-t / 20.0)); }
inline Vec3 sunDirection(double t) {
	const double el = sunElevation(t) * kPi / 180.0, az = -14.0 * kPi / 180.0;
	return normalize({std::sin(az) * std::cos(el), std::sin(el), -std::cos(az) * std::cos(el)});
}
// How much of the sun is up (0 hidden behind the hills, 1 clear): its light fades in as it rises.
inline double sunUp(double t) { return smoothstep(0.5, 3.0, sunElevation(t)); }
// Its colour: deep orange at the horizon, warm gold a few degrees up.
inline Vec3 sunColour(double t) {
	const double u = smoothstep(1.0, 10.0, sunElevation(t));
	return lerp(Vec3{1.0, 0.42, 0.13}, Vec3{1.0, 0.80, 0.55}, u);
}
// The sky's own light: deep blue before dawn, paler and warmer as the sun comes up.
inline Vec3 skyColour(double t) {
	const double u = smoothstep(0.0, 18.0, t);
	return lerp(Vec3{0.018, 0.032, 0.075}, Vec3{0.11, 0.14, 0.20}, u);
}
// The mist thins as the day warms.
inline double mistDensity(double t) { return lerp(0.034, 0.016, smoothstep(12.0, kDuration, t)); }
inline Mist mistAt(double t) {
	Mist m;
	// (big enough for every camera, small enough that the sun is not lost in it: its light crosses at most the radius)
	m.centre = {0.0, 0.0, -8.0};
	m.radius = 35.0;
	m.density = mistDensity(t);
	m.tint = lerp(Vec3{0.92, 0.98, 1.12}, Vec3{1.0, 1.0, 1.03}, smoothstep(6.0, 20.0, t));
	m.g = 0.62;
	return m;
}
// The grade follows the light: cool before sunrise, warm after.
inline double warmthAt(double t) { return lerp(-0.35, 0.22, smoothstep(5.0, 20.0, t)); }

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// Where things are
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------

struct Tree {
	Vec3 base;
	double height, radius, lean, yaw;
	int variant, salt;
};
// Trees on a jittered grid, leaving the clearing the shots look through (|x| < 3.5 and in front of z = -10) open.
inline const std::vector<Tree> &trees() {
	static const std::vector<Tree> v = [] {
		std::vector<Tree> out;
		int k = 0;
		for (double cz = -54.0; cz <= 6.0; cz += 5.5)
			for (double cx = -36.0; cx <= 36.0; cx += 5.5, ++k) {
				const double x = cx + 4.0 * (hash01(k, 31) - 0.5), z = cz + 4.0 * (hash01(k, 32) - 0.5);
				if (std::abs(x) < 3.5 && z > -10.0) continue;
				if (z > 2.0 && std::abs(x) < 9.0) continue;   // nothing between the first shots' camera and the clearing
				if (hash01(k, 33) < 0.18) continue;
				const double h = 15.0 + 6.0 * hash01(k, 34);
				out.push_back({ground().at(x, z) - Vec3{0.0, 0.15, 0.0}, h, 0.28 + 0.26 * hash01(k, 35), 1.2 * (hash01(k, 36) - 0.5), 360.0 * hash01(k, 37), k % 3, k});
			}
		return out;
	}();
	return v;
}

struct Tuft {
	Vec3 base;
	double scale, yaw;
	int variant;
};
// Grass in the clearing and along its edges, thicker near the camera; the hero tuft for the macro shot is the first.
inline const std::vector<Tuft> &tufts() {
	static const std::vector<Tuft> v = [] {
		std::vector<Tuft> out;
		out.push_back({ground().at(0.0, 0.0), 1.15, 20.0, 0});
		for (int i = 0; out.size() < 1700; ++i) {
			const double x = -14.0 + 28.0 * hash01(i, 41), z = -22.0 + 34.0 * hash01(i, 42);
			const double keep = std::exp(-std::abs(x) / 9.0) * (0.35 + 0.65 * smoothstep(-22.0, 6.0, z));
			if (hash01(i, 43) > keep) continue;
			if (std::abs(x) < 0.35 && std::abs(z) < 0.6) continue;   // around the hero tuft
			out.push_back({ground().at(x, z), 0.7 + 0.6 * hash01(i, 44), 360.0 * hash01(i, 45), i % 4});
		}
		return out;
	}();
	return v;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The pieces, written once per frame as objects
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------

constexpr double kTuftHeight = 0.42;
constexpr int kTuftBlades = 42;

inline std::string grassMaterial(int variant) {
	const Vec3 r[4] = {{0.16, 0.30, 0.08}, {0.20, 0.32, 0.09}, {0.22, 0.30, 0.10}, {0.14, 0.27, 0.08}};
	return translucentMaterial(r[variant % 4], r[variant % 4] * 1.25 + Vec3{0.06, 0.08, 0.0});
}
inline std::string leafMaterial(int variant) {
	const Vec3 r[4] = {{0.16, 0.26, 0.07}, {0.20, 0.28, 0.07}, {0.24, 0.27, 0.08}, {0.14, 0.23, 0.07}};
	return translucentMaterial(r[variant % 4], Vec3{0.30, 0.42, 0.10} + r[variant % 4] * 0.4);
}

inline const std::string &objectDefinitions() {
	static const std::string s = [] {
		std::string out;
		for (int v = 0; v < 4; ++v)
			out += nature::objectBegin("tuft" + std::to_string(v)) + attr(grassMaterial(v), emitMesh(nature::grassTuft(kTuftBlades, kTuftHeight, 0.11, 0.012, 100 + 17 * v), identity())) + nature::objectEnd();
		for (int v = 0; v < 4; ++v)
			out += nature::objectBegin("leaves" + std::to_string(v)) + attr(leafMaterial(v), emitMesh(nature::leafCluster(170, 2.6, 0.24, 300 + 23 * v), identity())) + nature::objectEnd();
		for (int v = 0; v < 2; ++v)
			out += nature::objectBegin("fern" + std::to_string(v)) + attr(translucentMaterial({0.12, 0.25, 0.07}, {0.22, 0.36, 0.08}), emitMesh(nature::fern(9, 0.9, 500 + 31 * v), identity())) +
				   nature::objectEnd();
		for (int v = 0; v < 3; ++v)
			out += nature::objectBegin("trunk" + std::to_string(v)) + attr(coated({0.11, 0.085, 0.065}, 0.85), emitMesh(nature::trunk(1.0, 1.0, 0.0, 600 + 7 * v), identity())) + nature::objectEnd();
		// a branch: the unit one, from y = 0 to 1 with radius 1 at its foot, placed (stretched and turned) for each
		out += nature::objectBegin("branch") + attr(coated({0.10, 0.08, 0.06}, 0.85), emitMesh(lathe({{1.0, 0.0}, {0.75, 0.5}, {0.5, 1.0}, {0.0, 1.0}}, 7), identity())) + nature::objectEnd();
		for (int v = 0; v < 3; ++v)
			out += nature::objectBegin("rock" + std::to_string(v)) + attr(coated({0.20, 0.21, 0.17}, 0.7), emitMesh(nature::rock(1.0, 700 + 13 * v), identity())) + nature::objectEnd();
		return out;
	}();
	return s;
}

// A tree: its trunk (the unit trunk scaled; a lean is a shear), branches out to its leaf clusters, and the clusters, which sway a little.
inline std::string buildTree(const Tree &tr, double t, std::string &branches) {
	std::string out;
	const double lean = tr.lean / tr.height;
	const Mat4 place = mul(translate(tr.base), rotate({0, 1, 0}, tr.yaw));
	// The unit trunk is 1 high with radius 1: scale it to the tree, and lean it.
	out += nature::placeObject("trunk" + std::to_string(tr.variant), mul(place, nature::shearXZ(lean, 0.0), scale(tr.radius, tr.height, tr.radius)));
	const int clusters = 9 + static_cast<int>(6 * hash01(tr.salt, 51));
	for (int c = 0; c < clusters; ++c) {
		const double s = 0.58 + 0.40 * hash01(tr.salt * 31 + c, 52);
		const double a = 2.0 * kPi * hash01(tr.salt * 31 + c, 53), reach = (0.6 + 2.6 * hash01(tr.salt * 31 + c, 54)) * (1.15 - s);
		const Vec3 onTrunk = {lean * s * tr.height, s * tr.height, 0.0};   // (the shear moves the trunk by lean * y)
		const Vec3 centre = onTrunk + Vec3{reach * std::cos(a), 0.6 + 0.8 * hash01(tr.salt * 31 + c, 55), reach * std::sin(a)};
		const Vec3 w = nature::windLean(tr.base.x + centre.x, tr.base.z + centre.z, t, 0.025);
		const Vec3 from = transformPoint(place, onTrunk), to = transformPoint(place, centre * 0.92 + onTrunk * 0.08);
		const double r = 0.05 + 0.04 * (1.0 - s);
		branches += nature::placeObject("branch", mul(alignY(from, to - from), scale(r, length(to - from), r)));
		const double size = 0.8 + 0.5 * hash01(tr.salt * 31 + c, 56);
		out += nature::placeObject("leaves" + std::to_string((tr.salt + c) % 4),
								   mul(place, translate(centre), nature::shearXZ(w.x, w.z), rotate({0, 1, 0}, 360.0 * hash01(tr.salt * 31 + c, 57)), scale(size)));
	}
	return out;
}

inline std::string buildGround(double t) {
	std::string out;
	out += attr(coated({0.075, 0.085, 0.045}, 0.9), emitMesh(nature::groundMesh(ground(), 0.0, -14.0, 130.0, 90), identity()));
	// the hills the sun comes up behind, a ring beyond the mist
	Mesh hills;
	const int seg = 160;
	for (int j = 0; j <= seg; ++j) {
		const double a = 2.0 * kPi * j / seg;
		const double h = 1.5 + 4.5 * nature::fbm2(std::cos(a) * 3.0 + 10.0, std::sin(a) * 3.0, 91);
		for (int r = 0; r < 3; ++r) {
			const double rad = r == 0 ? 72.0 : (r == 1 ? 120.0 : 170.0), y = r == 0 ? -1.0 : (r == 1 ? h : h * 1.25);
			hills.verts.push_back({rad * std::cos(a), y, -14.0 + rad * std::sin(a)});
			hills.normals.push_back(normalize({-std::cos(a) * 0.3, 1.0, -std::sin(a) * 0.3}));
		}
	}
	for (int j = 0; j < seg; ++j)
		for (int r = 0; r < 2; ++r) {
			const int a = j * 3 + r, b = (j + 1) * 3 + r;
			hills.tris.push_back({a, a + 1, b});
			hills.tris.push_back({b, a + 1, b + 1});
		}
	out += attr(diffuseMaterial({0.04, 0.05, 0.035}), emitMesh(hills, identity()));
	// grass, ferns and rocks
	for (std::size_t i = 0; i < tufts().size(); ++i) {
		const Tuft &tf = tufts()[i];
		const Vec3 w = nature::windLean(tf.base.x, tf.base.z, t, 0.10);
		out += nature::placeObject("tuft" + std::to_string(tf.variant), mul(translate(tf.base), nature::shearXZ(w.x, w.z), rotate({0, 1, 0}, tf.yaw), scale(tf.scale)));
	}
	for (int i = 0; i < 70; ++i) {
		const double x = -16.0 + 32.0 * hash01(i, 61), z = -30.0 + 30.0 * hash01(i, 62);
		if (std::abs(x) < 2.0 && z > -12.0) continue;
		out += nature::placeObject("fern" + std::to_string(i % 2), mul(translate(ground().at(x, z)), rotate({0, 1, 0}, 360.0 * hash01(i, 63)), scale(0.8 + 0.8 * hash01(i, 64))));
	}
	for (int i = 0; i < 18; ++i) {
		const double x = -18.0 + 36.0 * hash01(i, 71), z = -34.0 + 36.0 * hash01(i, 72);
		if (std::abs(x) < 1.5 && z > -8.0) continue;
		out += nature::placeObject("rock" + std::to_string(i % 3), mul(translate(ground().at(x, z) - Vec3{0.0, 0.1, 0.0}), rotate({0, 1, 0}, 360.0 * hash01(i, 73)), scale(0.5 + 1.3 * hash01(i, 74))));
	}
	return out;
}

// The tips of the hero tuft's blades, where the dew hangs (the same blades nature::grassTuft builds for variant 0, before the tuft is placed).
inline std::vector<Vec3> heroTips() {
	const Mesh m = nature::grassTuft(kTuftBlades, kTuftHeight, 0.11, 0.012, 100);
	std::vector<Vec3> tips;
	// each blade is a strip of 5 rows of 2 vertices: the drops hang three quarters and half way up (the tip itself is too thin to hold one)
	for (int b = 0; b < kTuftBlades; ++b) {
		const std::size_t last = static_cast<std::size_t>(b) * 10 + 6, lower = static_cast<std::size_t>(b) * 10 + 4;
		tips.push_back((m.verts[last] + m.verts[last + 1]) * 0.5);
		tips.push_back((m.verts[lower] + m.verts[lower + 1]) * 0.5);
	}
	return tips;
}

// Dew on the hero tuft: little water spheres where the blades are, moving with the tuft's sway. Only drawn where the macro shot can see them.
inline std::string buildDew(double t) {
	const Tuft &hero = tufts()[0];
	const Vec3 w = nature::windLean(hero.base.x, hero.base.z, t, 0.10);
	const Mat4 place = mul(translate(hero.base), nature::shearXZ(w.x, w.z), rotate({0, 1, 0}, hero.yaw), scale(hero.scale));
	std::string spheres;
	const std::vector<Vec3> tips = heroTips();
	char buf[160];
	for (std::size_t i = 0; i < tips.size(); ++i) {
		if (hash01(static_cast<int>(i), 81) < 0.35) continue;
		const Vec3 p = transformPoint(place, tips[i] - Vec3{0.0, 0.004, 0.0});
		std::snprintf(buf, sizeof(buf), "AttributeBegin\n  Translate %.5f %.5f %.5f\n  Shape \"sphere\" \"float radius\" [ %.5f ]\nAttributeEnd\n", -p.x, p.y, p.z, 0.0028 + 0.0025 * hash01(static_cast<int>(i), 82));
		spheres += buf;
	}
	return attr(glassMaterial(1.333), spheres);
}

// Fireflies before sunrise: small glowing balls drifting in the clearing, going out one by one.
inline std::string buildFireflies(double t) {
	std::string out;
	for (int i = 0; i < 26; ++i) {
		const double off = 2.0 + 9.0 * hash01(i, 91);
		const double glow = (0.5 + 0.5 * std::sin(2.1 * t + 7.0 * hash01(i, 92))) * (1.0 - smoothstep(off, off + 1.5, t));
		if (glow <= 0.02) continue;
		const Vec3 p = {-6.0 + 12.0 * hash01(i, 93) + 0.6 * std::sin(0.4 * t + 5.0 * hash01(i, 94)), 0.6 + 1.6 * hash01(i, 95) + 0.25 * std::sin(0.7 * t + 3.0 * hash01(i, 96)),
						-9.0 + 12.0 * hash01(i, 97) + 0.5 * std::cos(0.33 * t + 4.0 * hash01(i, 98))};
		out += emissiveSphere(p, 0.025, Vec3{9.0, 11.0, 2.5} * glow);
	}
	return out;
}

// Leaves that let go in the second half and fall through the light, turning over as they go.
inline std::string buildFallingLeaves(double t) {
	std::string out;
	Mesh leaves;
	for (int i = 0; i < 60; ++i) {
		const double start = 20.0 + 16.0 * hash01(i, 101);
		const double a = t - start;
		if (a < 0.0) continue;
		const Vec3 from = {-3.5 + 6.0 * hash01(i, 102), 4.5 + 3.5 * hash01(i, 103), -5.0 + 5.5 * hash01(i, 104)};
		const double fall = 0.42 + 0.22 * hash01(i, 105);
		const Vec3 p = from + Vec3{0.9 * std::sin(1.3 * a + 6.0 * hash01(i, 106)) + 0.35 * a, -fall * a, 0.6 * std::cos(1.1 * a + 6.0 * hash01(i, 107))};
		if (p.y < ground().height(p.x, p.z) + 0.02) continue;
		const double spin = 2.2 * a + 6.0 * hash01(i, 108), tilt = 0.9 * std::sin(1.7 * a + 3.0 * hash01(i, 109));
		const Vec3 dir = normalize({std::cos(spin), 0.4 * std::sin(tilt), std::sin(spin)});
		nature::addLeaf(leaves, p, dir, normalize({std::sin(tilt), 1.0, std::cos(spin) * 0.3}), 0.17, 0.09);
	}
	// the hero leaf: it crosses shot 4 close to the lens, in and out of a beam
	if (t >= 26.0 && t <= 35.0) {
		const double a = t - 26.0;
		const Vec3 p = {0.6 - 0.18 * a + 0.35 * std::sin(1.4 * a), 3.9 - 0.36 * a, 0.2 + 0.25 * std::cos(1.1 * a)};
		const double spin = 1.7 * a, tilt = 0.8 * std::sin(1.9 * a);
		nature::addLeaf(leaves, Vec3{p.x, ground().height(p.x, p.z) + p.y, p.z},
						normalize({std::cos(spin), 0.4 * std::sin(tilt), std::sin(spin)}), normalize({std::sin(tilt), 1.0, 0.3 * std::cos(spin)}), 0.2, 0.11);
	}
	if (!leaves.tris.empty()) out += attr(leafMaterial(2), emitMesh(leaves, identity()));
	return out;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The shots and the scene
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------

// A point `y` above the ground at (x, z): the cameras and what they look at stand on the rolling ground, not on y = 0.
inline Vec3 above(double x, double y, double z) { return {x, ground().height(x, z) + y, z}; }

inline const std::vector<Shot> &shots() {
	static const std::vector<Shot> s = [] {
		std::vector<Shot> v;
		// 1. A slow push into the blue clearing; the last fireflies.
		v.push_back({0.0, Track3::withEases({{0.0, above(1.6, 1.9, 11.0), Ease::Smooth}, {9.0, above(0.6, 1.7, 5.5), Ease::Smooth}}), Track3::withEases({{0.0, above(0.0, 2.6, -20.0), Ease::Smooth}, {9.0, above(-0.4, 2.8, -20.0), Ease::Smooth}}),
					 Track1::withEases({{0.0, 42.0, Ease::Smooth}}), 0.02});
		// 2. Low against the rising sun: the shafts come in between the trunks.
		v.push_back({9.0, Track3::withEases({{9.0, above(-1.2, 0.55, 3.0), Ease::Smooth}, {18.0, above(-0.6, 0.95, 0.8), Ease::Smooth}}), Track3::withEases({{9.0, above(-2.6, 3.2, -30.0), Ease::Smooth}, {18.0, above(-2.0, 3.6, -30.0), Ease::Smooth}}),
					 Track1::withEases({{9.0, 46.0, Ease::Smooth}}), 0.006});
		// 3. Macro: dew on the hero tuft, the light behind it out of focus.
		v.push_back({18.0, Track3::withEases({{18.0, above(0.22, 0.33, 0.95), Ease::Smooth}, {26.0, above(0.08, 0.36, 0.78), Ease::Smooth}}), Track3::withEases({{18.0, above(0.0, 0.33, 0.0), Ease::Smooth}, {26.0, above(0.0, 0.34, 0.0), Ease::Smooth}}),
					 Track1::withEases({{18.0, 26.0, Ease::Smooth}}), 0.006});
		// 4. Leaves falling through the beams.
		v.push_back({26.0, Track3::withEases({{26.0, above(2.0, 1.9, 3.0), Ease::Smooth}, {34.0, above(1.5, 1.3, 2.4), Ease::Smooth}}), Track3::withEases({{26.0, above(-0.4, 3.4, -1.5), Ease::Smooth}, {34.0, above(-0.4, 1.4, -1.5), Ease::Smooth}}),
					 Track1::withEases({{26.0, 42.0, Ease::Smooth}}), 0.012});
		// 5. A crane up and back as the mist thins.
		v.push_back({34.0, Track3::withEases({{34.0, above(0.0, 1.8, 8.0), Ease::Smooth}, {kDuration, above(1.5, 9.5, 17.0), Ease::Smooth}}), Track3::withEases({{34.0, above(0.0, 3.0, -20.0), Ease::Smooth}, {kDuration, above(-1.0, 3.5, -22.0), Ease::Smooth}}),
					 Track1::withEases({{34.0, 48.0, Ease::Smooth}}), 0.02});
		return v;
	}();
	return s;
}

inline std::vector<double> shotStarts() {
	std::vector<double> v;
	for (const Shot &s : shots()) v.push_back(s.start);
	return v;
}

// Everything up to and including WorldBegin: the look of the moment, the mist the camera stands in, the camera, the picture's size, the sampler and the integrator.
inline std::string headerAt(double t) {
	char look[160];
	std::snprintf(look, sizeof(look), "# @rt-look bloom 0.16 vignette 0.32 warmth %.3f saturation 1.06\n", warmthAt(t));
	// a held camera: a few centimetres of wander, a few millimetres for the macro shot (shot 3)
	CameraView v = cameraAt(shots(), t);
	const double hand = v.shot == 2 ? 0.0015 : 0.012;
	v.eye = v.eye + handheld(t, hand, 1);
	v.target = v.target + handheld(t, hand * 3.0, 2);
	return std::string(look) + nature::mistBeforeCamera(mistAt(t)) + sceneHeader(cameraText(v), kSamples);
}

inline std::string sceneAt(double t) {
	std::string out = headerAt(t);
	out += nature::mistBoundary(mistAt(t));
	char buf[200];
	const Vec3 sky = skyColour(t);
	std::snprintf(buf, sizeof(buf), "LightSource \"infinite\" \"rgb L\" [ %.5f %.5f %.5f ]\n", sky.x, sky.y, sky.z);
	out += buf;
	// The sun: a glowing ball of radius 4.4 m 400 m away (beyond the mist and the hills): its disc and all of the sunlight.
	const double up = sunUp(t);
	if (up > 0.0) out += emissiveSphere(sunDirection(t) * 400.0 + Vec3{0.0, -1.5, -8.0}, 4.4, sunColour(t) * (28000.0 * up));
	out += objectDefinitions();
	std::string branches;
	for (const Tree &tr : trees()) out += buildTree(tr, t, branches);
	out += branches;
	out += buildGround(t);
	if (t >= 17.0 && t <= 27.0) out += buildDew(t);
	if (t < 13.0) out += buildFireflies(t);
	out += buildFallingLeaves(t);
	return out;
}

}  // namespace forest_dawn
}  // namespace cartoon
