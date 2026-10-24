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

#include "kit_people.h"
#include "tengwang_city.h"

namespace cartoon {
namespace tengwang {

constexpr int kLifeVersion = 4;

// The people and animals are the film kit's (kit_people.h); the figures are cached under this film's name and version.
using namespace kit;
inline std::string personObject(int pose, int colour, bool lady) { return kit::personObject(kFilmKey, kLifeVersion, pose, colour, lady); }
inline std::string crowdText(const std::vector<PersonPlace> &people) { return kit::crowdText(kFilmKey, kLifeVersion, people); }

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// Trees
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
inline std::string leafMaterial(Vec3 c) {
	char buf[220];
	std::snprintf(buf, sizeof buf, "Material \"diffusetransmission\" \"rgb reflectance\" [ %.3f %.3f %.3f ] \"rgb transmittance\" [ %.3f %.3f %.3f ]", c.x, c.y, c.z, c.x * 0.7, c.y * 0.7, c.z * 0.5);
	return buf;
}

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

// Wang Bo: a young man in a pale grey-blue travelling robe, a little dusty, a plain black cap; sculpted for each frame in the pose of the moment.
inline Dress wangBoDress() {
	Dress d;
	d.robe = {0.50, 0.56, 0.62};
	d.trim = {0.30, 0.32, 0.36};
	d.height = 1.74;
	return d;
}
inline std::string wangBoText(const Pose &pose, const Mat4 &m, double cell = 0.016) { return figureText(figureMeshes(pose, wangBoDress(), cell), wangBoDress(), m); }
}  // namespace tengwang
}  // namespace cartoon
