#pragma once
// tengwang_life.h -- what lives and grows round the Pavilion of Prince Teng (tengwang.h): trees (weeping willows on the banks; autumn trees in crimson, gold and green round the
// villages and in the groves; pines on the mountain roads), the people (robed figures of the Tang: scholars and officials in round-collared robes with wide sleeves and black
// caps with ribbons hanging behind, ladies in high-waisted skirts with silk shawls, servants, soldiers, and Wang Bo himself), horses, boats (fishing sampans and sailing junks),
// birds (geese in their skeins, the solitary wild duck, an eagle) and a flock of sheep.
//
// The trees and the standing people are pbrt objects whose meshes are PLY files (film_cache.h), placed many times; Wang Bo, the horses' legs and the birds' wings move, and are
// written into each frame. The people are sculpted from blended shapes (sdf_mesh.h) and kept simple (no faces: they are mostly seen small, from behind, against the light).

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "tengwang_city.h"

namespace cartoon {
namespace tengwang {

constexpr int kLifeVersion = 4;

// An ellipsoid's distance by its centre and radii (sdf::ellipsoid, with the centre typed so a braced list is never taken for a frame).
inline double ell(Vec3 p, Vec3 centre, Vec3 radii) { return sdf::ellipsoid(p, centre, radii); }

// An object made of PLY parts with their materials: defined once in a frame (its ObjectBegin text) and placed by placeObject.
struct ObjectDef {
	std::string name;
	std::vector<std::pair<std::string, std::string>> parts;   // (material text, PLY path)
	std::string text() const {
		std::string out = nature::objectBegin(name);
		for (const auto &[mat, file] : parts) out += attr(mat, film_cache::plyShape(file));
		return out + nature::objectEnd();
	}
};

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// Trees
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
inline std::string leafMaterial(Vec3 c) {
	char buf[220];
	std::snprintf(buf, sizeof buf, "Material \"diffusetransmission\" \"rgb reflectance\" [ %.3f %.3f %.3f ] \"rgb transmittance\" [ %.3f %.3f %.3f ]", c.x, c.y, c.z, c.x * 0.7, c.y * 0.7, c.z * 0.5);
	return buf;
}
inline std::string barkMaterial() { return "Material \"diffuse\" \"rgb reflectance\" [ 0.11 0.08 0.06 ]"; }

// A weeping willow: a leaning trunk that forks, and hundreds of long hanging strands of narrow leaves.
inline Mesh willowTrunk(int salt) {
	Mesh m = nature::trunk(4.5, 0.32, 0.6, salt);
	for (int b = 0; b < 5; ++b) {
		const double a = 2.0 * kPi * (b + hash01(b, salt)) / 5.0;
		const Vec3 from{0.3, 3.8 + 0.4 * hash01(b, salt + 1), 0.0}, to = from + Vec3{2.6 * std::cos(a), 2.4 + 0.8 * hash01(b, salt + 2), 2.6 * std::sin(a)};
		append(m, sdf::tubeMesh({from, lerp(from, to, 0.5) + Vec3{0.0, 0.4, 0.0}, to}, 0.13, 0.4, 7), identity());
	}
	return m;
}
// The strands hang from the boughs' ends all over a rounded crown (a dome), each first arching out and then falling, so the tree is a soft weeping mound, not a curtain.
inline Mesh willowStrands(int salt) {
	Mesh m;
	for (int s = 0; s < 520; ++s) {
		const double a = 2.0 * kPi * hash01(s, salt + 3), e = std::asin(hash01(s, salt + 4));   // (more of them towards the dome's top)
		const double r = 3.6 * std::cos(e) * (0.85 + 0.25 * hash01(s, salt + 5));
		const Vec3 top{0.3 + r * std::cos(a), 5.4 + 3.0 * std::sin(e), r * std::sin(a)};
		const double len = (1.6 + 3.0 * hash01(s, salt + 6)) * (0.6 + 0.6 * std::sin(e));
		const Vec3 out = normalize(Vec3{std::cos(a), 0.0, std::sin(a)});
		const Vec3 side = Vec3{-out.z, 0.0, out.x};
		const double arch = 0.4 + 0.5 * std::cos(e);
		nature::addStrip(m, [&](double u) { return top + out * (arch * std::sin(kPi * 0.5 * (std::min)(1.0, u * 2.0))) + Vec3{0.0, 0.25 * std::sin(kPi * u) - len * u * u, 0.0}; },
						 [](double u) { return 0.11 * (1.0 - 0.7 * u); }, side, 7);
	}
	return m;
}
// An autumn tree: a trunk with a few boughs and a crown of leaf clusters.
inline Mesh broadTrunk(double h, int salt) {
	Mesh m = nature::trunk(h * 0.55, 0.28 * h / 9.0 + 0.12, 0.3, salt);
	for (int b = 0; b < 6; ++b) {
		const double a = 2.0 * kPi * (b + hash01(b, salt)) / 6.0;
		const Vec3 from{0.0, h * (0.42 + 0.1 * hash01(b, salt + 1)), 0.0}, to = from + Vec3{h * 0.3 * std::cos(a), h * (0.25 + 0.1 * hash01(b, salt + 2)), h * 0.3 * std::sin(a)};
		append(m, sdf::tubeMesh({from, to}, 0.07 * h / 9.0 + 0.04, 0.35, 6), identity());
	}
	return m;
}
inline Mesh broadCrown(double h, int salt) {
	Mesh m;
	for (int c = 0; c < 22; ++c) {
		const double a = 2.0 * kPi * hash01(c, salt + 7), y = h * (0.55 + 0.4 * hash01(c, salt + 8)), r = h * 0.32 * std::sqrt(1.0 - std::pow((y / h - 0.72) / 0.32, 2.0) * 0.5) * std::sqrt(hash01(c, salt + 9));
		const Mesh cluster = nature::leafCluster(26, h * 0.22, h * 0.07, salt * 13 + c);
		append(m, cluster, translate(r * std::cos(a), y, r * std::sin(a)));
	}
	return m;
}
// A pine: a straight trunk and layers of dark needles in flattened tiers.
inline Mesh pineTrunk(double h, int salt) { return nature::trunk(h, 0.22 * h / 10.0 + 0.06, 0.4, salt); }
inline Mesh pineNeedles(double h, int salt) {
	Mesh m;
	for (int tier = 0; tier < 6; ++tier) {
		const double y = h * (0.35 + 0.11 * tier), r = h * (0.32 - 0.045 * tier);
		for (int k = 0; k < 3; ++k) {
			const double a = 2.0 * kPi * (k + hash01(tier * 7 + k, salt)) / 3.0;
			append(m, nature::rock(r * 1.2, salt + tier * 3 + k), mul(translate(r * 0.4 * std::cos(a), y, r * 0.4 * std::sin(a)), scale(1.0, 0.45, 1.0)));
		}
	}
	return m;
}

struct TreeKind {
	const char *name;
	Vec3 leaf;
};
inline const std::vector<TreeKind> &treeKinds() {
	static const std::vector<TreeKind> k = {{"willow", {0.22, 0.32, 0.07}}, {"maple", {0.70, 0.08, 0.04}}, {"ginkgo", {0.85, 0.62, 0.06}}, {"camphor", {0.12, 0.26, 0.06}}, {"tallow", {0.80, 0.30, 0.06}},
											 {"pine", {0.05, 0.12, 0.05}}};
	return k;
}
// The trees as objects ("tree_willow", "tree_maple", ...), their meshes made once.
inline const std::vector<ObjectDef> &treeObjects() {
	static const std::vector<ObjectDef> defs = [] {
		std::vector<ObjectDef> v;
		for (const TreeKind &k : treeKinds()) {
			const std::string n = k.name;
			ObjectDef d;
			d.name = "tree_" + n;
			const int salt = static_cast<int>(std::hash<std::string>()(n) % 1000);
			if (n == "willow") {
				d.parts.push_back({barkMaterial(), film_cache::ply(kFilmKey, versioned("tree_willow_trunk", kLifeVersion), [&] { return willowTrunk(salt); })});
				d.parts.push_back({leafMaterial(k.leaf), film_cache::ply(kFilmKey, versioned("tree_willow_leaves", kLifeVersion), [&] { return willowStrands(salt); })});
			} else if (n == "pine") {
				d.parts.push_back({barkMaterial(), film_cache::ply(kFilmKey, versioned("tree_pine_trunk", kLifeVersion), [&] { return pineTrunk(12.0, salt); })});
				d.parts.push_back({"Material \"diffuse\" \"rgb reflectance\" [ 0.04 0.10 0.05 ]", film_cache::ply(kFilmKey, versioned("tree_pine_needles", kLifeVersion), [&] { return pineNeedles(12.0, salt); })});
			} else {
				d.parts.push_back({barkMaterial(), film_cache::ply(kFilmKey, versioned("tree_" + n + "_trunk", kLifeVersion), [&] { return broadTrunk(10.0, salt); })});
				d.parts.push_back({leafMaterial(k.leaf), film_cache::ply(kFilmKey, versioned("tree_" + n + "_leaves", kLifeVersion), [&] { return broadCrown(10.0, salt); })});
			}
			v.push_back(d);
		}
		return v;
	}();
	return defs;
}

struct TreePlace {
	int kind;   // index in treeKinds()
	Vec3 at;
	double size, yaw;
};
// Where the trees stand: willows along both banks near the Pavilion, autumn trees round the villages, in the groves of the plain near the river, and in the city's courtyards.
inline const std::vector<TreePlace> &treePlaces() {
	static const std::vector<TreePlace> places = [] {
		std::vector<TreePlace> v;
		int salt = 0;
		const auto add = [&](int kind, double x, double z, double size) {
			++salt;
			v.push_back({kind, {x, groundHeight(x, z) - 0.2, z}, size * (0.8 + 0.4 * hash01(salt, 91)), 360.0 * hash01(salt, 92)});
		};
		const auto autumn = [&](int s) {
			const double h = hash01(s, 93);
			return h < 0.30 ? 1 : (h < 0.55 ? 2 : (h < 0.82 ? 3 : 4));
		};
		// willows: behind the quay, then along the natural banks up and down stream, and along the far bank
		for (double x = -2600.0; x < 2600.0; x += 11.0 + 6.0 * hash01(static_cast<int>(x), 94)) {
			const double z = std::abs(x) < 380.0 ? nearBank(x) + 30.0 : nearBank(x) + 7.0 + 6.0 * hash01(static_cast<int>(x), 95);
			if (std::abs(x) < 70.0 || (std::abs(x - kGateX) < 30.0)) continue;   // (the Pavilion's view, and the road to the gate)
			add(0, x, z, 1.0);
			if (hash01(static_cast<int>(x), 96) < 0.55) add(autumn(static_cast<int>(x)), x + 4.0, z + 9.0, 0.9);
			const double farBank = riverMiddle(x) - riverHalfWidth(x) - 8.0 - 8.0 * hash01(static_cast<int>(x), 97);
			if (hash01(static_cast<int>(x), 98) < 0.7) add(hash01(static_cast<int>(x), 99) < 0.6 ? 0 : autumn(static_cast<int>(x) + 1), x, farBank, 1.1);
		}
		// round each village
		for (const Vec3 &c : villagePlaces()) {
			for (int k = 0; k < 14; ++k) {
				++salt;
				const double a = 2.0 * kPi * hash01(salt, 101), r = 20.0 + 70.0 * std::sqrt(hash01(salt, 102));
				add(autumn(salt), c.x + r * std::cos(a), c.z + r * std::sin(a), 1.0);
			}
		}
		// groves on the plain within a few kilometres
		for (int k = 0; k < 9000 && v.size() < 2600; ++k) {
			const double x = -5000.0 + 10000.0 * hash01(k, 103), z = -4500.0 + 4300.0 * hash01(k, 104);
			if (fromWater(x, z) < 40.0 || nature::fbm2(x / 900.0, z / 900.0, 33) < 0.62 || groundHeight(x, z) > 25.0) continue;
			add(autumn(k + 7), x, z, 1.0);
		}
		// the city's courtyards
		for (int k = 0; k < 160; ++k) {
			const double x = kCityX0 + 60.0 + (kCityX1 - kCityX0 - 120.0) * hash01(k, 105), z = kCityZ0 + 60.0 + (kCityZ1 - kCityZ0 - 120.0) * hash01(k, 106);
			const double cx = std::fmod(x - kCityX0 - 22.0, 70.0), cz = std::fmod(z - kCityZ0 - 22.0, 66.0);
			if (std::abs(cx - 30.0) < 12.0 && std::abs(cz - 28.0) < 12.0) add(autumn(k + 31), x, z, 0.8);
		}
		return v;
	}();
	return places;
}
// The trees as scene text: their objects and their placements, those within `range` of `from` (and the willows a little stirred by the wind at time t).
inline std::string treesText(Vec3 from, double range, double t, double wind = 0.4) {
	std::string out;
	for (const ObjectDef &d : treeObjects()) out += d.text();
	for (const TreePlace &p : treePlaces()) {
		if (std::hypot(p.at.x - from.x, p.at.z - from.z) > range) continue;
		const Vec3 lean = nature::windLean(p.at.x, p.at.z, t, wind * 0.03);
		out += nature::placeObject("tree_" + std::string(treeKinds()[static_cast<std::size_t>(p.kind)].name),
								   mul(translate(p.at), nature::shearXZ(lean.x, lean.z), rotate({0, 1, 0}, p.yaw), scale(p.size)));
	}
	return out;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// People
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// A figure's pose: where its arms and head are and how its robe swings. Local frame: feet at the origin, up +y, facing +z; about 1.72 m tall.
struct Pose {
	double walk = 0.0;        // the walk's phase (radians); 0 standing
	double stride = 0.0;      // how long the stride is (0 standing, 1 walking)
	Vec3 handL = {-0.20, 1.02, 0.16}, handR = {0.20, 1.02, 0.16};   // where the hands are (in the sleeves when together in front)
	double headTilt = 0.0;    // degrees, + looking up
	double headTurn = 0.0;    // degrees, + to its left
	double lean = 0.0;        // the body's lean forward, degrees
	Vec3 wind = {0.0, 0.0, 0.0};   // the wind on the robe (world-ish: +x pushes the hem to its right)
	bool kneel = false;       // sitting on the heels (at a low table)
	bool bow = false;
};
// How a figure is dressed.
struct Dress {
	Vec3 robe = {0.55, 0.10, 0.08};
	Vec3 trim = {0.85, 0.75, 0.40};      // the collar and the belt's plaque
	Vec3 under = {0.88, 0.86, 0.80};     // the white under-robe at the neck
	bool lady = false;                   // a high-waisted skirt, a shawl, a high knot of hair
	bool beard = false;
	bool hat = true;                     // the black futou cap with ribbons (men)
	bool helmet = false;                 // a soldier
	double height = 1.72;
};

// The figures are sculpted after the Tang's own tomb figures: slender, long-robed, the shoulders sloping, wide sleeves hanging from the forearms, the faces small and calm with
// painted features. The upper body is worked out in a frame of its own: lowered for a figure kneeling on its heels, bent forward about the hips for a bow.
struct BodyFrame {
	double drop = 0.0;   // how far the upper body is lowered (a kneel)
	double lean = 0.0;   // radians
	double turn = 0.0, tilt = 0.0;   // the head's, radians
	// A point of the world in the upper body's frame (standing, upright).
	Vec3 upper(Vec3 p) const {
		p.y += drop;
		if (lean != 0.0 && p.y > 0.98) {
			const double a = lean * smoothstep(0.98, 1.5, p.y);
			const double y = p.y - 0.98, z = p.z;
			p.y = 0.98 + y * std::cos(a) + z * std::sin(a);
			p.z = -y * std::sin(a) + z * std::cos(a);
		}
		return p;
	}
	// A point in the head's frame (its pivot at the neck, 1.52 m up, facing +z).
	Vec3 head(Vec3 q) const {
		Vec3 h = q - Vec3{0.0, 1.52, 0.0};
		h = {h.x * std::cos(turn) - h.z * std::sin(turn), h.y, h.x * std::sin(turn) + h.z * std::cos(turn)};
		h = {h.x, h.y * std::cos(tilt) - h.z * std::sin(tilt), h.y * std::sin(tilt) + h.z * std::cos(tilt)};
		return h + Vec3{0.0, 1.52, 0.0};
	}
	// A hand given in the world (where it should be) in the upper body's frame.
	Vec3 hand(Vec3 h) const { return h + Vec3{0.0, drop, 0.0}; }
};
inline BodyFrame bodyFrame(const Pose &pose) {
	BodyFrame b;
	b.drop = pose.kneel ? 0.50 : 0.0;
	b.lean = pose.lean * kPi / 180.0;
	b.turn = pose.headTurn * kPi / 180.0;
	b.tilt = -pose.headTilt * kPi / 180.0;
	return b;
}
// The arm of side `side` (-1 left, +1 right): the shoulder, the elbow (bent outwards and down), the wrist (where the sleeve ends) and the hand.
struct Arm {
	Vec3 shoulder, elbow, wrist, hand;
};
inline Arm armOf(const Pose &pose, const BodyFrame &b, int side) {
	Arm a;
	a.shoulder = {side * 0.205, 1.375, 0.0};
	a.hand = b.hand(side < 0 ? pose.handL : pose.handR);
	const Limb l = limbIk(a.shoulder, a.hand, 0.29, 0.29, {side * 0.7, -0.8, -0.3});
	a.elbow = l.elbow;
	a.hand = l.hand;
	a.wrist = a.hand - normalize(a.hand - a.elbow) * 0.07;
	return a;
}

// The robe: the body, the skirt to the ankles (or folded round the knees of a kneeling figure), the round collar and the wide sleeves.
inline double robeSdf(Vec3 p, const Pose &pose, const Dress &dress) {
	using namespace sdf;
	const double s = dress.height / 1.72;
	p = p * (1.0 / s);
	const BodyFrame b = bodyFrame(pose);
	const Vec3 q = b.upper(p);
	const double waist = dress.lady ? 1.27 : 1.0;
	// the body: waist to chest to the sloping shoulders
	double d = roundCone(q, {0.0, waist - 0.12, 0.0}, {0.0, 1.29, 0.0}, dress.lady ? 0.135 : 0.155, 0.165);
	d = smin(d, roundCone(q, {0.0, 1.29, 0.0}, {0.0, 1.40, -0.01}, 0.165, 0.10), 0.05);
	for (int side = -1; side <= 1; side += 2) d = smin(d, capsule(q, {0.0, 1.405, -0.01}, {side * 0.205, 1.375, 0.0}, 0.068), 0.05);
	// the round collar, standing a little off the neck
	d = smin(d, ell(q, {0.0, 1.425, 0.005}, {0.075, 0.035, 0.07}), 0.02);
	// the skirt
	if (!pose.kneel) {
		const double u = clamp01((waist - p.y) / waist);   // 0 at the waist, 1 at the hem
		const Vec3 sway = Vec3{pose.wind.x, 0.0, pose.wind.z} * (0.20 * u * u) + Vec3{0.0, 0.0, 0.06 * pose.stride * std::sin(pose.walk) * u};
		const Vec3 k = p - sway;
		double rx = lerp(dress.lady ? 0.145 : 0.165, dress.lady ? 0.40 : 0.30, std::pow(u, 0.85));
		const double theta = std::atan2(k.z, k.x);
		rx *= 1.0 + 0.04 * u * std::sin(9.0 * theta + 1.3) + 0.02 * u * std::sin(17.0 * theta);   // soft pleats
		const double rz = rx * (dress.lady ? 0.9 : 0.8);
		double skirt = (std::hypot(k.x / rx, k.z / rz) - 1.0) * (std::min)(rx, rz);
		skirt = (std::max)(skirt, k.y - waist + 0.02);
		skirt = (std::max)(skirt, 0.03 - k.y);
		d = smin(d, skirt, 0.06);
	} else {
		double lap = ell(p, {0.0, 0.20, 0.10}, {0.26, 0.20, 0.36});
		lap = smin(lap, ell(p, {0.0, 0.10, -0.02}, {0.29, 0.10, 0.33}), 0.06);
		d = smin(d, lap, 0.08);
	}
	// the sleeves: a slim upper arm, then the wide sleeve over the forearm with its long hanging fold
	for (int side = -1; side <= 1; side += 2) {
		const Arm a = armOf(pose, b, side);
		d = smin(d, capsule(q, a.shoulder, a.elbow, 0.058), 0.04);
		d = smin(d, roundCone(q, a.elbow, a.wrist, 0.060, dress.lady ? 0.088 : 0.080), 0.03);
		const Vec3 hangTop = lerp(a.elbow, a.wrist, 0.25), hangBottom = a.wrist + Vec3{pose.wind.x * 0.08, -0.24 - (dress.lady ? 0.06 : 0.0), pose.wind.z * 0.08};
		d = smin(d, roundCone(q, hangTop, hangBottom, 0.034, dress.lady ? 0.062 : 0.055), 0.06);
	}
	return d * s;
}
// The face, the neck and the hands.
inline double skinSdf(Vec3 p, const Pose &pose, const Dress &dress) {
	using namespace sdf;
	const double s = dress.height / 1.72;
	p = p * (1.0 / s);
	const BodyFrame b = bodyFrame(pose);
	const Vec3 q = b.upper(p);
	const Vec3 h = b.head(q);
	double d = ell(h, {0.0, 1.605, 0.0}, {0.084, 0.108, 0.094});
	d = smin(d, ell(h, {0.0, 1.545, 0.028}, {0.062, 0.058, 0.066}), 0.03);              // the jaw and chin
	d = smin(d, ell(h, {0.0, 1.583, 0.090}, {0.0085, 0.019, 0.010}), 0.010);            // the nose, a fine ridge
	for (int side = -1; side <= 1; side += 2) d = smin(d, ell(h, {side * 0.085, 1.595, -0.004}, {0.012, 0.025, 0.017}), 0.008);   // the ears
	d = smin(d, capsule(q, {0.0, 1.40, -0.005}, {0.0, 1.55, 0.0}, 0.040), 0.03);       // the neck
	for (int side = -1; side <= 1; side += 2) {
		const Arm a = armOf(pose, b, side);
		const Vec3 dir = normalize(a.hand - a.elbow);
		double hand = capsule(q, a.wrist, a.hand + dir * 0.02, 0.026);                    // the palm, from the cuff
		hand = smin(hand, capsule(q, a.hand, a.hand + dir * 0.065 + Vec3{0.0, -0.012, 0.0}, 0.016), 0.012);   // the fingers, together
		hand = smin(hand, capsule(q, a.hand - dir * 0.02, a.hand - dir * 0.01 + Vec3{side * -0.025, 0.012, 0.02}, 0.011), 0.008);   // the thumb
		d = (std::min)(d, hand);
	}
	return d * s;
}
// The painted features of the face: two narrow dark eyes and the brows over them (drawn in the hair's black).
inline double featureSdf(Vec3 p, const Pose &pose, const Dress &dress) {
	using namespace sdf;
	const double s = dress.height / 1.72;
	p = p * (1.0 / s);
	const BodyFrame b = bodyFrame(pose);
	const Vec3 h = b.head(b.upper(p));
	double d = 1e9;
	for (int side = -1; side <= 1; side += 2) {
		d = (std::min)(d, ell(h, {side * 0.031, 1.611, 0.084}, {0.0125, 0.0042, 0.006}));
		d = (std::min)(d, capsule(h, {side * 0.013, 1.632, 0.089}, {side * 0.050, 1.636, 0.077}, 0.0032));
	}
	return d * s;
}
inline double hairSdf(Vec3 p, const Pose &pose, const Dress &dress) {
	using namespace sdf;
	const double s = dress.height / 1.72;
	p = p * (1.0 / s);
	const BodyFrame b = bodyFrame(pose);
	const Vec3 h = b.head(b.upper(p));
	double d;
	if (dress.lady) {
		// hair swept up from the face into a high knot of two loops
		d = ell(h, {0.0, 1.64, -0.018}, {0.092, 0.088, 0.100});
		d = (std::max)(d, -(h.z - 0.03 - 0.6 * (1.62 - h.y)));   // (off the face)
		d = smin(d, ell(h, {0.0, 1.76, -0.03}, {0.060, 0.07, 0.055}), 0.03);
		d = smin(d, ell(h, {0.0, 1.86, -0.025}, {0.050, 0.060, 0.038}), 0.03);   // (the knot's top, a single high loop)
	} else if (dress.helmet) {
		d = ell(h, {0.0, 1.655, -0.005}, {0.102, 0.098, 0.108});
		d = (std::max)(d, -(h.z - 0.05 - 0.9 * (1.60 - h.y)));
		d = smin(d, roundCone(h, {0.0, 1.73, -0.01}, {0.0, 1.83, -0.01}, 0.025, 0.008), 0.02);
		d = (std::min)(d, ell(h, {0.0, 1.55, -0.06}, {0.12, 0.07, 0.08}));   // the neck guard
	} else {
		// the futou: a black cap over the hair, a raised crown at its back, two soft ribbons hanging behind
		d = ell(h, {0.0, 1.648, -0.016}, {0.091, 0.078, 0.100});
		d = (std::max)(d, -(h.z - 0.045 - 0.9 * (1.625 - h.y)));   // (the brim across the forehead)
		d = smin(d, ell(h, {0.0, 1.74, -0.035}, {0.056, 0.062, 0.052}), 0.025);
		for (int side = -1; side <= 1; side += 2) {
			const Vec3 a{side * 0.03, 1.64, -0.10}, e{side * 0.065 + pose.wind.x * 0.08, 1.42 + pose.wind.y * 0.04, -0.16 - std::abs(pose.wind.z) * 0.10};
			d = (std::min)(d, roundCone(h, a, e, 0.018, 0.014));
		}
	}
	return d * s;
}
inline double beardSdf(Vec3 p, const Pose &pose, const Dress &dress) {
	using namespace sdf;
	if (!dress.beard) return 1e9;
	const double s = dress.height / 1.72;
	p = p * (1.0 / s);
	const BodyFrame b = bodyFrame(pose);
	const Vec3 h = b.head(b.upper(p));
	double d = roundCone(h, {0.0, 1.515, 0.072}, {0.0, 1.38, 0.10}, 0.032, 0.010);
	for (int side = -1; side <= 1; side += 2) d = smin(d, capsule(h, {0.0, 1.567, 0.098}, {side * 0.035, 1.55, 0.085}, 0.006), 0.01);
	return d * s;
}
inline double shoeSdf(Vec3 p, const Pose &pose, const Dress &dress) {
	using namespace sdf;
	if (pose.kneel) return 1e9;
	const double s = dress.height / 1.72;
	p = p * (1.0 / s);
	double d = 1e9;
	for (int side = -1; side <= 1; side += 2) {
		const double step = pose.stride * 0.16 * std::sin(pose.walk + (side > 0 ? kPi : 0.0));
		d = (std::min)(d, ell(p, {side * 0.085, 0.035, 0.07 + step}, {0.045, 0.04, 0.115}));
	}
	return d * s;
}
// The belt (a man's, black with gold plaques, at the waist) or the sash under a lady's breast with its long ends hanging down the front.
inline double beltSdf(Vec3 p, const Pose &pose, const Dress &dress) {
	using namespace sdf;
	const double s = dress.height / 1.72;
	p = p * (1.0 / s);
	const BodyFrame b = bodyFrame(pose);
	const Vec3 q = b.upper(p);
	const double y = dress.lady ? 1.27 : 0.99, rx = dress.lady ? 0.150 : 0.172, rz = dress.lady ? 0.128 : 0.142;
	double d = (std::max)((std::hypot(q.x / rx, q.z / rz) - 1.0) * rz, std::abs(q.y - y) - (dress.lady ? 0.028 : 0.032));
	if (dress.lady && !pose.kneel)
		for (int side = -1; side <= 1; side += 2) d = (std::min)(d, capsule(q, {side * 0.03, y, rz}, {side * 0.05 + pose.wind.x * 0.1, 0.72, rz + 0.06}, 0.016));
	return d * s;
}
// A lady's shawl (pibo): a long broad ribbon from behind the shoulders, over the forearms, hanging at her sides.
inline double shawlSdf(Vec3 p, const Pose &pose, const Dress &dress) {
	using namespace sdf;
	if (!dress.lady) return 1e9;
	const double s = dress.height / 1.72;
	p = p * (1.0 / s);
	const BodyFrame b = bodyFrame(pose);
	const Vec3 q = b.upper(p);
	double d = 1e9;
	for (int side = -1; side <= 1; side += 2) {
		const Arm a = armOf(pose, b, side);
		const std::vector<Vec3> pts = {{0.0, 1.40, -0.15}, {side * 0.20, 1.38, -0.09}, lerp(a.elbow, a.wrist, 0.5) + Vec3{side * 0.05, 0.05, 0.0}, a.wrist + Vec3{side * 0.10 + pose.wind.x * 0.2, -0.40, -0.02},
									   {side * 0.26 + pose.wind.x * 0.35, 0.30 - b.drop, -0.02 + pose.wind.z * 0.2}};
		for (std::size_t i = 0; i + 1 < pts.size(); ++i) d = (std::min)(d, capsule(q, pts[i], pts[i + 1], 0.03));
	}
	return d * s;
}

// A figure's meshes by part (robe, skin, the painted features, hair, beard, shoes, belt, shawl), sculpted at `cell` (metres; the face finer).
struct FigureMeshes {
	Mesh robe, skin, features, hair, beard, shoes, belt, shawl;
};
inline FigureMeshes figureMeshes(const Pose &pose, const Dress &dress, double cell = 0.02) {
	const double s = dress.height / 1.72;
	const Vec3 lo{-0.6 * s, -0.02, -0.6 * s}, hi{0.6 * s, 1.95 * s, 0.65 * s};
	const BodyFrame b = bodyFrame(pose);
	FigureMeshes f;
	f.robe = sdf::surfaceNets([&](Vec3 p) { return robeSdf(p, pose, dress); }, lo, hi, cell);
	f.skin = sdf::surfaceNets([&](Vec3 p) { return skinSdf(p, pose, dress); }, Vec3{-0.6 * s, 0.4 * s - b.drop, -0.5 * s}, Vec3{0.6 * s, 1.8 * s, 0.65 * s}, cell * 0.45);
	// (the head's region: a box round it, wherever the pose puts it)
	const Vec3 headLo{-0.22 * s, (1.38 - b.drop) * s, -0.40 * s}, headHi{0.22 * s, (1.98 - b.drop) * s, 0.40 * s};
	f.features = sdf::surfaceNets([&](Vec3 p) { return featureSdf(p, pose, dress); }, headLo, headHi, (std::max)(0.0025, cell * 0.16));
	f.hair = sdf::surfaceNets([&](Vec3 p) { return hairSdf(p, pose, dress); }, Vec3{-0.3 * s, (1.2 - b.drop) * s, -0.5 * s}, Vec3{0.3 * s, (2.05 - b.drop) * s, 0.4 * s}, cell * 0.4);
	f.beard = sdf::surfaceNets([&](Vec3 p) { return beardSdf(p, pose, dress); }, headLo, headHi, cell * 0.35);
	f.shoes = sdf::surfaceNets([&](Vec3 p) { return shoeSdf(p, pose, dress); }, Vec3{-0.3 * s, -0.02, -0.3 * s}, Vec3{0.3 * s, 0.2 * s, 0.45 * s}, cell * 0.5);
	f.belt = sdf::surfaceNets([&](Vec3 p) { return beltSdf(p, pose, dress); }, lo, hi, cell * 0.5);
	f.shawl = sdf::surfaceNets([&](Vec3 p) { return shawlSdf(p, pose, dress); }, lo, hi, cell * 0.6);
	return f;
}
inline std::string skinMaterial() { return "Material \"coateddiffuse\" \"rgb reflectance\" [ 0.72 0.52 0.40 ] \"float roughness\" [ 0.5 ]"; }
inline std::string hairMaterial() { return "Material \"coateddiffuse\" \"rgb reflectance\" [ 0.018 0.016 0.016 ] \"float roughness\" [ 0.3 ]"; }
inline std::string robeMaterial(Vec3 c) {
	// silk: a soft sheen over the colour
	char buf[200];
	std::snprintf(buf, sizeof buf, "Material \"coateddiffuse\" \"rgb reflectance\" [ %.3f %.3f %.3f ] \"float roughness\" [ 0.32 ]", c.x, c.y, c.z);
	return buf;
}
// The figure's text, in the figure's frame `m` (story space).
inline std::string figureText(const FigureMeshes &f, const Dress &dress, const Mat4 &m) {
	std::string out;
	out += attr(robeMaterial(dress.robe), emitMesh(f.robe, m));
	out += attr(skinMaterial(), emitMesh(f.skin, m));
	out += attr(hairMaterial(), emitMesh(f.features, m) + emitMesh(f.hair, m) + emitMesh(f.shoes, m));
	if (!f.beard.tris.empty()) out += attr("Material \"diffuse\" \"rgb reflectance\" [ 0.62 0.62 0.60 ]", emitMesh(f.beard, m));
	if (!f.belt.tris.empty()) out += attr(dress.lady ? robeMaterial(dress.trim) : "Material \"coateddiffuse\" \"rgb reflectance\" [ 0.03 0.025 0.02 ] \"float roughness\" [ 0.2 ]", emitMesh(f.belt, m));
	if (!f.shawl.tris.empty()) out += attr("Material \"diffusetransmission\" \"rgb reflectance\" [ " + fmt3(dress.trim) + " ] \"rgb transmittance\" [ " + fmt3(dress.trim * 0.6) + " ]", emitMesh(f.shawl, m));
	return out;
}

// The colours of the guests' robes: the officials' purples, scarlets and greens of the Tang ranks, the scholars' blues and ochres, the ladies' jewel colours.
inline const std::vector<Vec3> &robeColours() {
	static const std::vector<Vec3> c = {{0.42, 0.10, 0.48}, {0.70, 0.08, 0.06}, {0.08, 0.38, 0.22}, {0.10, 0.22, 0.55}, {0.72, 0.48, 0.12}, {0.62, 0.20, 0.32}, {0.15, 0.42, 0.48}, {0.80, 0.70, 0.52},
										{0.30, 0.12, 0.08}, {0.88, 0.30, 0.12}};
	return c;
}

// The standing and seated people as objects: a few poses in each robe colour ("person_<pose>_<colour>").
enum PersonPose { Standing = 0, Clasping, Bowing, Kneeling, Walking, Carrying, kPersonPoses };
inline Pose personPose(int k) {
	Pose p;
	switch (k) {
		case Standing: p.handL = {-0.22, 0.86, 0.10}; p.handR = {0.22, 0.86, 0.10}; break;
		case Clasping: p.handL = {-0.05, 1.12, 0.26}; p.handR = {0.05, 1.12, 0.26}; break;   // hands joined in the sleeves before the chest (a greeting)
		case Bowing: p.handL = {-0.05, 1.05, 0.30}; p.handR = {0.05, 1.05, 0.30}; p.lean = 28.0; p.headTilt = -10.0; break;
		case Kneeling: p.kneel = true; p.handL = {-0.16, 0.62, 0.30}; p.handR = {0.18, 0.66, 0.32}; break;
		case Walking: p.walk = 0.7; p.stride = 1.0; p.handL = {-0.20, 0.92, 0.18}; p.handR = {0.22, 0.88, -0.02}; break;
		case Carrying: p.handL = {-0.18, 1.08, 0.36}; p.handR = {0.18, 1.08, 0.36}; break;   // a tray or a jar held out in front
	}
	return p;
}
inline std::string personObjectName(int pose, int colour, bool lady) { return "person_" + std::to_string(pose) + "_" + std::to_string(colour) + (lady ? "_l" : ""); }
// The object definitions for the people a shot uses (each made once: its PLY parts).
inline std::string personObject(int pose, int colour, bool lady) {
	const std::string name = personObjectName(pose, colour, lady);
	const std::string base = versioned("person_" + std::to_string(pose) + (lady ? "_lady" : ""), kLifeVersion);
	Dress dress;
	dress.lady = lady;
	dress.hat = !lady;
	dress.robe = robeColours()[static_cast<std::size_t>(colour) % robeColours().size()];
	dress.trim = lady ? robeColours()[static_cast<std::size_t>(colour + 3) % robeColours().size()] : Vec3{0.85, 0.72, 0.35};
	dress.height = lady ? 1.62 : 1.72;
	static std::map<std::string, FigureMeshes> made;
	const auto get = [&]() -> const FigureMeshes & {
		auto it = made.find(base);
		if (it == made.end()) it = made.emplace(base, figureMeshes(personPose(pose), dress)).first;
		return it->second;
	};
	std::string out = nature::objectBegin(name);
	out += attr(robeMaterial(dress.robe), film_cache::plyShape(film_cache::ply(kFilmKey, base + "_robe", [&] { return get().robe; })));
	out += attr(skinMaterial(), film_cache::plyShape(film_cache::ply(kFilmKey, base + "_skin", [&] { return get().skin; })));
	out += attr(hairMaterial(), film_cache::plyShape(film_cache::ply(kFilmKey, base + "_hair", [&] { return get().hair; })));
	out += attr(hairMaterial(), film_cache::plyShape(film_cache::ply(kFilmKey, base + "_shoes", [&] { return get().shoes; })));
	out += attr(hairMaterial(), film_cache::plyShape(film_cache::ply(kFilmKey, base + "_features", [&] { return get().features; })));
	out += attr(lady ? robeMaterial(dress.trim) : "Material \"coateddiffuse\" \"rgb reflectance\" [ 0.03 0.025 0.02 ] \"float roughness\" [ 0.2 ]",
				film_cache::plyShape(film_cache::ply(kFilmKey, base + "_belt", [&] { return get().belt; })));
	if (lady)
		out += attr("Material \"diffusetransmission\" \"rgb reflectance\" [ " + fmt3(dress.trim) + " ] \"rgb transmittance\" [ " + fmt3(dress.trim * 0.6) + " ]",
					film_cache::plyShape(film_cache::ply(kFilmKey, base + "_shawl", [&] { return get().shawl; })));
	return out + nature::objectEnd();
}

// A crowd: people to place (pose, colour, lady) and where.
struct PersonPlace {
	int pose, colour;
	bool lady;
	Vec3 at;
	double yaw;
};
inline std::string crowdText(const std::vector<PersonPlace> &people) {
	std::string out;
	std::map<std::string, bool> defined;
	for (const PersonPlace &p : people) {
		const std::string n = personObjectName(p.pose, p.colour, p.lady);
		if (!defined[n]) {
			out += personObject(p.pose, p.colour, p.lady);
			defined[n] = true;
		}
	}
	for (const PersonPlace &p : people) out += nature::placeObject(personObjectName(p.pose, p.colour, p.lady), mul(translate(p.at), rotate({0, 1, 0}, p.yaw)));
	return out;
}

// Wang Bo: a young man in a pale grey-blue travelling robe, a little dusty, a plain black cap; sculpted for each frame in the pose of the moment.
inline Dress wangBoDress() {
	Dress d;
	d.robe = {0.50, 0.56, 0.62};
	d.trim = {0.30, 0.32, 0.36};
	d.height = 1.74;
	return d;
}
inline std::string wangBoText(const Pose &pose, const Mat4 &m, double cell = 0.016) { return figureText(figureMeshes(pose, wangBoDress(), cell), wangBoDress(), m); }
// A walking pose at time t (`speed` steps a second).
inline Pose walkingPose(double t, double speed = 1.6) {
	Pose p;
	p.walk = 2.0 * kPi * speed * t * 0.5;
	p.stride = 1.0;
	p.handL = {-0.21, 0.92, 0.12 + 0.10 * std::sin(p.walk + kPi)};
	p.handR = {0.21, 0.92, 0.12 + 0.10 * std::sin(p.walk)};
	return p;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// Horses
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// A horse's body, neck and head (one mesh, made once) and its legs (bars, moved every frame by the walk's phase). Local frame: facing +z, hooves on y = 0, about 1.6 m at the withers.
inline const Mesh &horseBody() {
	static const Mesh m = [] {
		using namespace sdf;
		const auto f = [](Vec3 p) {
			double d = ell(p, {0.0, 1.30, 0.0}, {0.30, 0.34, 0.80});
			d = smin(d, roundCone(p, {0.0, 1.40, 0.62}, {0.0, 1.85, 0.95}, 0.20, 0.12), 0.10);
			d = smin(d, roundCone(p, {0.0, 1.88, 0.98}, {0.0, 1.62, 1.38}, 0.12, 0.07), 0.06);
			d = smin(d, ell(p, {0.0, 1.98, 0.92}, {0.035, 0.08, 0.04}), 0.02);   // an ear
			d = smin(d, roundCone(p, {0.0, 1.32, -0.74}, {0.0, 0.70, -0.95}, 0.08, 0.05), 0.05);   // the tail
			return d;
		};
		return surfaceNets(f, {-0.5, 0.5, -1.3}, {0.5, 2.2, 1.6}, 0.035);
	}();
	return m;
}
inline std::string horseText(Vec3 at, double yaw, double phase, double stride, Vec3 coat, Vec3 saddle) {
	const Mat4 frame = mul(translate(at), rotate({0, 1, 0}, yaw));
	std::string out = attr(robeMaterial(coat), emitMesh(horseBody(), frame));
	std::string legs;
	const double offs[4] = {0.0, kPi, kPi * 0.5, kPi * 1.5};
	const Vec3 hips[4] = {{-0.16, 1.15, 0.55}, {0.16, 1.15, 0.55}, {-0.16, 1.15, -0.55}, {0.16, 1.15, -0.55}};
	for (int k = 0; k < 4; ++k) {
		const double a = phase + offs[k];
		const Vec3 hoof = hips[k] + Vec3{0.0, -1.12 + 0.10 * stride * (std::max)(0.0, std::sin(a)), 0.22 * stride * std::cos(a)};
		const Vec3 knee = lerp(hips[k], hoof, 0.5) + Vec3{0.0, 0.0, (k < 2 ? 0.06 : -0.06) + 0.05 * stride * std::sin(a)};
		legs += bar(transformPoint(frame, hips[k]), transformPoint(frame, knee), 0.07, 8) + bar(transformPoint(frame, knee), transformPoint(frame, hoof), 0.05, 8);
	}
	out += attr(robeMaterial(coat * 0.8), legs);
	out += attr(robeMaterial(saddle), emitMesh(roundedBox(0.66, 0.16, 0.7, 0.06, 2), mul(frame, translate(0.0, 1.66, 0.05))));
	return out;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// Boats
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// A hull `len` long and `beam` wide: a lofted shell, rising at the bow and stern. Local frame: the bow towards +z, the waterline at y = 0.
inline Mesh hullMesh(double len, double beam, double depth) {
	Mesh m;
	const int nu = 24, nv = 10;
	for (int i = 0; i <= nu; ++i) {
		const double u = static_cast<double>(i) / nu;   // stern to bow
		const double z = lerp(-len / 2.0, len / 2.0, u);
		const double w = beam / 2.0 * std::pow(std::sin(kPi * lerp(0.08, 0.95, u)), 0.7);
		const double sheer = 0.25 * depth + 0.9 * depth * std::pow(std::abs(2.0 * u - 1.0), 3.0);
		for (int j = 0; j <= nv; ++j) {
			const double a = kPi * j / nv;   // round the bottom from one gunwale to the other
			m.verts.push_back({w * std::cos(a), sheer - depth * std::sin(a) * (0.6 + 0.4 * std::sin(kPi * u)), z});
		}
	}
	for (int i = 0; i < nu; ++i)
		for (int j = 0; j < nv; ++j) {
			const int q = i * (nv + 1) + j;
			m.tris.push_back({q, q + 1, q + nv + 1});
			m.tris.push_back({q + 1, q + nv + 2, q + nv + 1});
		}
	sdf::computeNormals(m);
	return m;
}
// A boat's text at `at` heading `yaw`: a fishing sampan (a hull, an arched mat roof, a fisherman in a broad hat) or a junk (a bigger hull, a deck house and one or two batten
// sails of tan or red-brown cloth).
inline std::string boatText(bool junk, Vec3 at, double yaw, double roll, int salt, double sailUp = 1.0) {
	const Mat4 frame = mul(translate(at), rotate({0, 1, 0}, yaw), rotate({0, 0, 1}, roll));
	std::string out;
	const double len = junk ? 16.0 : 7.5, beam = junk ? 4.2 : 1.7;
	const Mesh hull = hullMesh(len, beam, junk ? 1.3 : 0.55);
	out += attr("Material \"coateddiffuse\" \"rgb reflectance\" [ 0.20 0.11 0.05 ] \"float roughness\" [ 0.4 ]", emitMesh(hull, frame));
	out += attr("Material \"diffuse\" \"rgb reflectance\" [ 0.28 0.17 0.08 ]", emitMesh(box(beam * 0.85, 0.05, len * 0.8), mul(frame, translate(0.0, junk ? 0.45 : 0.18, 0.0))));
	if (!junk) {
		// the arched mat roof amidships, and the fisherman with his pole at the stern
		Mesh roof = lathe({{0.85, 0.0}, {0.85, 2.2}}, 16);
		std::string arch;
		for (int k = 0; k < 7; ++k) {
			const double a0 = kPi * k / 7.0, a1 = kPi * (k + 1) / 7.0;
			const Vec3 p0 = transformPoint(frame, {0.8 * std::cos(a0), 0.2 + 0.85 * std::sin(a0), -1.0}), p1 = transformPoint(frame, {0.8 * std::cos(a1), 0.2 + 0.85 * std::sin(a1), -1.0});
			const Vec3 q0 = transformPoint(frame, {0.8 * std::cos(a0), 0.2 + 0.85 * std::sin(a0), 1.2}), q1 = transformPoint(frame, {0.8 * std::cos(a1), 0.2 + 0.85 * std::sin(a1), 1.2});
			Mesh panel;
			panel.verts = {p0, p1, q1, q0};
			const Vec3 n = normalize(cross(p1 - p0, q0 - p0));
			const Vec3 mid = (p0 + q1) * 0.5;
			const Vec3 outn = dot(n, mid - transformPoint(frame, {0.0, 0.2, 0.1})) > 0.0 ? n : n * -1.0;
			panel.normals.assign(4, outn);
			panel.tris = {{0, 1, 2}, {0, 2, 3}};
			arch += emitMesh(panel, identity());
		}
		(void)roof;
		out += attr("Material \"diffusetransmission\" \"rgb reflectance\" [ 0.32 0.24 0.12 ] \"rgb transmittance\" [ 0.25 0.16 0.06 ]", arch);
		Pose p;
		p.handL = {-0.10, 1.40, 0.30};
		p.handR = {0.10, 1.10, 0.35};
		Dress d;
		d.robe = {0.30, 0.28, 0.22};
		d.hat = false;
		static const FigureMeshes fisher = figureMeshes(p, d, 0.03);
		out += figureText(fisher, d, mul(frame, translate(0.0, 0.2, -2.9), rotate({0, 1, 0}, 20.0)));
		// his broad conical hat of straw
		out += attr("Material \"diffuse\" \"rgb reflectance\" [ 0.55 0.44 0.24 ]", emitMesh(lathe({{0.0, 0.18}, {0.32, 0.02}, {0.34, 0.0}, {0.0, 0.0}}, 16), mul(frame, translate(0.0, 1.88, -2.9))));
		out += attr(barkMaterial(), bar(transformPoint(frame, {0.25, 1.2, -2.6}), transformPoint(frame, {0.9, -1.5, -4.2}), 0.025, 6));
	} else {
		out += attr("Material \"coateddiffuse\" \"rgb reflectance\" [ 0.35 0.18 0.08 ] \"float roughness\" [ 0.4 ]", emitMesh(box(beam * 0.7, 1.6, len * 0.25), mul(frame, translate(0.0, 1.2, -len * 0.28))));
		const Vec3 sail = hash01(salt, 111) < 0.5 ? Vec3{0.62, 0.40, 0.20} : Vec3{0.52, 0.18, 0.10};
		for (int mast = 0; mast < 2; ++mast) {
			const double mz = mast == 0 ? len * 0.12 : -len * 0.05, mh = mast == 0 ? 13.0 : 9.0;
			out += attr(barkMaterial(), bar(transformPoint(frame, {0.0, 0.4, mz}), transformPoint(frame, {0.0, mh, mz}), 0.12, 8));
			if (mast == 1 && len < 12.0) continue;
			// the sail: a tall cloth between battens, a little bellied
			Mesh cloth;
			const int nu = 6, nv = 10;
			const double w = mh * 0.55, h = mh * 0.75 * sailUp;
			for (int j = 0; j <= nv; ++j)
				for (int i = 0; i <= nu; ++i) {
					const double u = static_cast<double>(i) / nu, v = static_cast<double>(j) / nv;
					cloth.verts.push_back({0.35 * std::sin(kPi * u) * (0.5 + 0.5 * v), mh - 0.6 - h * (1.0 - v) + 0.0, mz + lerp(-w * 0.35, w * 0.65, u)});
				}
			for (int j = 0; j < nv; ++j)
				for (int i = 0; i < nu; ++i) {
					const int q = j * (nu + 1) + i;
					cloth.tris.push_back({q, q + 1, q + nu + 2});
					cloth.tris.push_back({q, q + nu + 2, q + nu + 1});
				}
			sdf::computeNormals(cloth);
			char mat[220];
			std::snprintf(mat, sizeof mat, "Material \"diffusetransmission\" \"rgb reflectance\" [ %.3f %.3f %.3f ] \"rgb transmittance\" [ %.3f %.3f %.3f ]", sail.x, sail.y, sail.z, sail.x * 0.7, sail.y * 0.6, sail.z * 0.5);
			out += attr(mat, emitMesh(cloth, frame));
			std::string battens;
			for (int j = 0; j <= 5; ++j) {
				const double y = mh - 0.6 - h * (1.0 - j / 5.0);
				battens += bar(transformPoint(frame, {0.15, y, mz - w * 0.35}), transformPoint(frame, {0.15, y, mz + w * 0.65}), 0.05, 5);
			}
			out += attr(barkMaterial(), battens);
		}
	}
	return out;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// Birds
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// A bird at `at` flying towards `dir`, its wings at `flap` (-1 down .. 1 up); `size` is its wingspan. A goose is grey-brown with a black neck; the wild duck is dark against the
// sky; the eagle is broad-winged and brown.
inline std::string birdText(Vec3 at, Vec3 dir, double flap, double size, Vec3 body, Vec3 wing) {
	const Vec3 f = normalize(dir);
	const Mat4 frame = mul(alignY(at, f, {0.0, 1.0, 0.0}), scale(size));
	// (alignY puts local +y along the flight; the body is a long ellipsoid along it, the wings spread to the local +-x)
	std::string out;
	out += attr(robeMaterial(body), emitMesh(uvSphere(1.0, 12, 8), mul(frame, scale(0.07, 0.22, 0.07))));
	out += attr(robeMaterial(body * 0.5), emitMesh(capsule(0.025, 0.16, 6, 2), mul(frame, translate(0.0, 0.16, 0.0))));
	out += attr(robeMaterial(body * 0.5), emitMesh(uvSphere(1.0, 8, 6), mul(frame, translate(0.0, 0.34, 0.01), scale(0.035, 0.05, 0.035))));
	for (int side = -1; side <= 1; side += 2) {
		const double a = flap * 0.75;
		Mesh w;
		const Vec3 root{side * 0.04, 0.02, 0.0}, elbow{side * 0.26, 0.0, 0.26 * std::sin(a)}, tip{side * 0.50, -0.08, 0.50 * std::sin(a * 1.4)};
		w.verts = {root + Vec3{0.0, 0.08, 0.0}, root + Vec3{0.0, -0.10, 0.0}, elbow + Vec3{0.0, 0.06, 0.0}, elbow + Vec3{0.0, -0.12, 0.0}, tip};
		w.normals.assign(5, Vec3{0.0, 0.0, 1.0});
		w.tris = {{0, 1, 3}, {0, 3, 2}, {2, 3, 4}};
		sdf::computeNormals(w);
		out += attr("Material \"diffuse\" \"rgb reflectance\" [ " + fmt3(wing) + " ]", emitMesh(w, frame) + emitMesh(w, mul(frame, translate(0.0, 0.0, -0.004))));
	}
	return out;
}
// A skein of geese in a V, `n` birds, its leader at `lead`, flying along `dir`, their wingbeats a little out of step.
inline std::string skeinText(Vec3 lead, Vec3 dir, int n, double t, double spacing = 2.4) {
	const Vec3 f = normalize(Vec3{dir.x, 0.0, dir.z}), side = Vec3{-f.z, 0.0, f.x};
	std::string out;
	for (int k = 0; k < n; ++k) {
		const int arm = k == 0 ? 0 : (k % 2 ? 1 : -1), rank = (k + 1) / 2;
		const Vec3 at = lead - f * (spacing * rank) + side * (arm * spacing * 0.8 * rank) + Vec3{0.0, 0.15 * std::sin(t * 1.3 + k), 0.0};
		out += birdText(at, dir, std::sin(t * 2.0 * kPi * 1.3 + k * 0.6), 2.3, {0.30, 0.27, 0.24}, {0.24, 0.22, 0.20});
	}
	return out;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// Sheep
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
inline const Mesh &sheepBody() {
	static const Mesh m = [] {
		using namespace sdf;
		const auto f = [](Vec3 p) {
			double d = ell(p, {0.0, 0.62, 0.0}, {0.26, 0.24, 0.42});
			d = smin(d, sphere(p, {0.0, 0.66, 0.24}, 0.2), 0.08);
			d = smin(d, sphere(p, {0.0, 0.56, -0.25}, 0.21), 0.08);
			return d + 0.012 * (std::sin(p.x * 60.0) * std::sin(p.y * 57.0) * std::sin(p.z * 63.0));   // a woolly lumpiness
		};
		return surfaceNets(f, {-0.4, 0.3, -0.7}, {0.4, 0.95, 0.7}, 0.02);
	}();
	return m;
}
inline std::string sheepText(Vec3 at, double yaw, double phase) {
	const Mat4 frame = mul(translate(at), rotate({0, 1, 0}, yaw));
	std::string out = attr("Material \"diffuse\" \"rgb reflectance\" [ 0.78 0.74 0.66 ]", emitMesh(sheepBody(), frame));
	std::string dark = emitMesh(uvSphere(1.0, 10, 6), mul(frame, translate(0.0, 0.70, 0.52), scale(0.08, 0.10, 0.13)));
	for (int k = 0; k < 4; ++k) {
		const Vec3 hip{(k % 2 ? 0.12 : -0.12), 0.45, (k < 2 ? 0.25 : -0.25)};
		const Vec3 hoof = hip + Vec3{0.0, -0.45, 0.06 * std::sin(phase + k * kPi * 0.5)};
		dark += bar(transformPoint(frame, hip), transformPoint(frame, hoof), 0.03, 6);
	}
	return out + attr("Material \"diffuse\" \"rgb reflectance\" [ 0.06 0.05 0.05 ]", dark);
}

}  // namespace tengwang
}  // namespace cartoon
