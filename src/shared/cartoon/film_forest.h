#pragma once
// film_forest.h -- forests for a film file (film_file.h `forest`): conifers, broadleaf trees, birches, palms and shrubs, grown with their branches, spread over the land
// where each would grow - not on cliffs, not in water or a river's bed, thinning out at a tree line, denser where water gathers - and dressed for the season.
//
// A species is grown once in a few variants, each in two versions: near (a trunk, its limbs branching into twigs, every leaf or tuft of needles; tens of thousands of
// triangles) and far (a trunk and a few lumps of foliage of the same colours). Each is a PLY file in the film's cache, wrapped in a pbrt object (ObjectBegin ... ObjectEnd) and
// placed as many times as there are trees, so a forest of thousands is a few hundred kilobytes of text a frame and one copy of each shape in the renderer. A tree nearer the
// camera than its `near` distance is drawn near, the rest far; each sways a little in the wind (its placement leans).
//
// Seasons: summer (greens), autumn (broadleaf trees and birches turn orange, red, yellow and brown, a colour to each), winter (they are bare; conifers carry snow on their
// tiers), spring (fresh light green, and some broadleaf trees in blossom). A forest can go from one season to another over the film (`season summer to autumn from T0 to
// T1`): each tree in its own time within that, through eight stages between them - its leaves turning colour, dropping one by one (a stage keeps a share of the summer
// crown's leaves, the same leaves going first at every stage), coming out again; blossom and snow coming and going the same way. And it can grow (`grow T0 T1`): each tree
// from a sapling to its full size over its own part of that time.
//
// Coordinates are pbrt's, as a film file's; the meshes are made in story space and placed with nature::placeObject.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "cartoon_lib.h"
#include "film_cache.h"
#include "film_dressing.h"
#include "film_terrain.h"
#include "nature_lib.h"

namespace cartoon {
namespace forest {

inline const std::vector<std::string> &species() {
	static const std::vector<std::string> k = {"conifer", "broadleaf", "birch", "palm", "shrub"};
	return k;
}
inline const std::vector<std::string> &kinds() {   // what a `forest` line may ask for: a species, or a mixed wood chosen by height
	static const std::vector<std::string> k = {"conifer", "broadleaf", "birch", "palm", "shrub", "mixed"};
	return k;
}
inline const std::vector<std::string> &seasons() {
	static const std::vector<std::string> k = {"summer", "autumn", "winter", "spring"};
	return k;
}
constexpr int kVariants = 4;

// A forest: what grows (a species or mixed), how many, over which rectangle (pbrt's x0 z0 x1 z1; the whole terrain when not given), between which heights above the land's
// base (the tree line), up to how steep, how big (a scale range), how near the camera a tree is drawn in full, the season, a seed.
struct Forest {
	std::string kind = "mixed";
	int count = 500;
	double x0 = 0.0, z0 = 0.0, x1 = 0.0, z1 = 0.0;
	bool area = false;
	double below = -1.0, above = -1e9, steep = 0.2, scale0 = 0.8, scale1 = 1.25, fullWithin = 120.0;
	std::string season = "summer";
	std::string seasonTo;              // (a change of season: to this one, each tree turning in its own time within turn0..turn1, film seconds)
	double turn0 = 0.0, turn1 = 0.0;
	double grow0 = -1.0, grow1 = -1.0;   // (growing: each tree from a sapling to its size within grow0..grow1; below 0 not)
	int seed = 1;
};
// A tree: its species, its variant, where its foot is (pbrt's), its size and turn.
struct Tree {
	std::string species;
	int variant = 0;
	Vec3 at;
	double scale = 1.0, turn = 0.0;
};

namespace detail {

inline Vec3 transformDir(const Mat4 &m, Vec3 d) { return {m[0] * d.x + m[1] * d.y + m[2] * d.z, m[4] * d.x + m[5] * d.y + m[6] * d.z, m[8] * d.x + m[9] * d.y + m[10] * d.z}; }
// `src` through `m`, appended to `dst`.
inline void addInto(Mesh &dst, const Mesh &src, const Mat4 &m) {
	const int base = static_cast<int>(dst.verts.size());
	for (std::size_t i = 0; i < src.verts.size(); ++i) {
		dst.verts.push_back(transformPoint(m, src.verts[i]));
		const Vec3 n = transformDir(m, i < src.normals.size() ? src.normals[i] : Vec3{0.0, 1.0, 0.0});
		dst.normals.push_back(length(n) > 1e-12 ? normalize(n) : Vec3{0.0, 1.0, 0.0});
	}
	for (const auto &t : src.tris) dst.tris.push_back({t[0] + base, t[1] + base, t[2] + base});
}
// A tapering limb from `a` to `b`, radius ra at a and rb at b.
inline void addLimb(Mesh &dst, Vec3 a, Vec3 b, double ra, double rb) {
	const double len = length(b - a);
	if (len < 1e-6) return;
	const Mesh tube = lathe({{ra, 0.0}, {rb, len}}, 6);
	addInto(dst, tube, alignY(a, b - a));
}
inline Vec3 turned(Vec3 d, double yawDeg, double pitchDeg) {   // d tipped away from the vertical by pitch, about the vertical by yaw
	const Vec3 side = length(cross(d, Vec3{0, 1, 0})) > 1e-6 ? normalize(cross(d, Vec3{0, 1, 0})) : Vec3{1, 0, 0};
	const Mat4 r = mul(rotate(Vec3{0, 1, 0}, yawDeg), rotate(side, pitchDeg));
	return normalize(transformDir(r, d));
}

// A species' shape, grown: its wood and its foliage as two meshes (foliage: the leaves or needles; `snow`: winter snow on a conifer's tiers).
struct Grown {
	Mesh wood, foliage, snow, blossom;
};

inline void branchOut(Grown &g, Vec3 at, Vec3 dir, double len, double radius, int depth, int maxDepth, int salt, double spread, double clusterSize, int clusterLeaves, double leafLen,
					  bool blossom) {
	const Vec3 end = at + dir * len;
	addLimb(g.wood, at, end, radius, radius * 0.65);
	if (depth >= maxDepth - 1 && clusterLeaves > 0)   // (leaves along the outer branches, not only at their tips)
		addInto(g.foliage, nature::leafCluster(clusterLeaves / 2, clusterSize * 0.8, leafLen, salt + 7), translate(at + (end - at) * 0.55));
	if (depth == maxDepth) {
		const Mesh c = nature::leafCluster(clusterLeaves, clusterSize, leafLen, salt);
		addInto(g.foliage, c, mul(translate(end), rotate(Vec3{0, 1, 0}, 360.0 * hash01(salt, 3))));
		if (blossom && hash01(salt, 9) < 0.6) addInto(g.blossom, nature::leafCluster(clusterLeaves / 3, clusterSize * 0.95, leafLen * 0.7, salt + 1), translate(end + Vec3{0, 0.1, 0}));
		return;
	}
	const int children = depth == 0 ? 3 + static_cast<int>(2 * hash01(salt, 1)) : 2 + static_cast<int>(1.6 * hash01(salt, 2));
	for (int k = 0; k < children; ++k) {
		const int s = salt * 7 + k * 131 + 17;
		// the first limbs spread out wide (as far as the spread again from the vertical), the rest fork at the spread; all reach up a little for the light
		const double yaw = 360.0 * (k + 0.4 * hash01(s, 4)) / children, pitch = (depth == 0 ? 1.35 : 1.0) * spread * (0.6 + 0.6 * hash01(s, 5));
		Vec3 d = turned(depth == 0 ? Vec3{0.0, 1.0, 0.0} : dir, yaw, pitch);
		d = normalize(d + Vec3{0.0, depth == 0 ? 0.05 : 0.15, 0.0});
		branchOut(g, end, d, len * (0.62 + 0.15 * hash01(s, 6)), radius * 0.62, depth + 1, maxDepth, s, spread, clusterSize, clusterLeaves, leafLen, blossom);
	}
}

// Leaves filling a crown: `clusters` clusters of `leaves` leaves in an ellipsoid round `centre` with radii `r`, most of them towards its outer shell (the inside of a crown
// is in its own shade, and seen from outside the shell is what shows).
inline void fillCrown(Grown &g, Vec3 centre, Vec3 r, int clusters, int leaves, double size, double leafLen, int salt, bool blossom) {
	for (int k = 0; k < clusters; ++k) {
		const int s = salt * 13 + k * 101;
		const double y = 1.0 - 2.0 * hash01(s, 1), ring = std::sqrt(std::max(0.0, 1.0 - y * y)), a = 2.0 * kPi * hash01(s, 2);
		const double out = std::pow(0.35 + 0.65 * hash01(s, 3), 0.4);
		const Vec3 at = centre + Vec3{r.x * ring * std::cos(a), r.y * y, r.z * ring * std::sin(a)} * out;
		addInto(g.foliage, nature::leafCluster(leaves, size * (0.8 + 0.4 * hash01(s, 4)), leafLen, s), mul(translate(at), rotate(Vec3{0, 1, 0}, 360.0 * hash01(s, 5))));
		if (blossom && hash01(s, 6) < 0.5) addInto(g.blossom, nature::leafCluster(leaves / 3, size * 0.95, leafLen * 0.7, s + 1), translate(at + Vec3{0.0, 0.1, 0.0}));
	}
}

// Each species grown at its own size (metres), variant `v`.
inline Grown grow(const std::string &sp, int v, bool winter, bool spring) {
	Grown g;
	const int salt = 9001 + 977 * v + static_cast<int>(sp.size()) * 31;
	if (sp == "conifer") {
		const double H = 14.0 + 3.0 * hash01(salt, 1);
		addInto(g.wood, nature::trunk(H, 0.24, 0.0, salt), identity());
		const int tiers = 20;
		addInto(g.foliage, lathe({{0.16 * H, H * 0.22}, {0.1 * H, H * 0.55}, {0.03 * H, H * 0.9}, {0.0, H * 0.98}}, 9), identity());   // (the dark core you see between the tiers)
		for (int k = 0; k < tiers; ++k) {
			const double h = H * (0.2 + 0.78 * k / (tiers - 1.0)), r = 0.3 * H * std::pow(1.0 - (h / H - 0.2) / 0.8, 1.1) + 0.3;
			const int arms = 9;
			for (int a = 0; a < arms; ++a) {
				const double yaw = 2.0 * kPi * (a + 0.5 * hash01(salt + k * 13 + a, 2)) / arms + k * 0.7;
				const Vec3 out{std::cos(yaw), 0.0, std::sin(yaw)};
				const Vec3 from{0.0, h, 0.0}, tip = from + out * r + Vec3{0.0, -0.22 * r, 0.0};
				addLimb(g.wood, from, tip, 0.04 + 0.01 * r, 0.015);
				for (int p = 1; p <= 4; ++p) {   // pads of needles along the arm, drooping
					const double u = p / 4.0;
					const Vec3 at = from + (tip - from) * u;
					const double size = 0.7 * r * (1.15 - 0.5 * u) + 0.3;
					const Mesh pad = nature::leafCluster(26, size, 0.3, salt + k * 31 + a * 7 + p);
					addInto(g.foliage, pad, mul(translate(at), rotate(Vec3{0, 1, 0}, yaw * 57.3), scale(1.0, 0.42, 1.0)));
					if (winter) addInto(g.snow, pad, mul(translate(at + Vec3{0.0, 0.07, 0.0}), rotate(Vec3{0, 1, 0}, yaw * 57.3), scale(0.85, 0.18, 0.85)));
				}
			}
		}
	} else if (sp == "broadleaf") {
		const double trunkH = 2.4 + 1.0 * hash01(salt, 1);
		addInto(g.wood, nature::trunk(trunkH, 0.32, 0.25 * (hash01(salt, 2) - 0.5), salt), identity());
		Grown crown;   // the limbs (bare in winter), then the leaves filling a crown round them
		branchOut(crown, {0.0, trunkH * 0.95, 0.0}, {0.0, 1.0, 0.0}, 2.3, 0.24, 0, winter ? 4 : 3, salt, 50.0, 2.1, 0, 0.18, false);   // (bare, the twigs show)
		addInto(g.wood, crown.wood, identity());
		if (!winter) fillCrown(g, {0.0, trunkH + 3.0, 0.0}, {3.6, 2.8, 3.6}, 120, 150, 2.0, 0.26, salt, spring);
	} else if (sp == "birch") {
		const double trunkH = 4.5 + 1.5 * hash01(salt, 1);
		addInto(g.wood, nature::trunk(trunkH + 3.0, 0.14, 0.4 * (hash01(salt, 2) - 0.5), salt), identity());
		Grown crown;
		branchOut(crown, {0.0, trunkH * 0.55, 0.0}, {0.0, 1.0, 0.0}, 2.0, 0.09, 0, winter ? 4 : 3, salt, 30.0, 1.4, 0, 0.12, false);
		addInto(g.wood, crown.wood, identity());
		if (!winter) fillCrown(g, {0.0, trunkH + 2.4, 0.0}, {2.0, 3.2, 2.0}, 80, 110, 1.4, 0.16, salt, false);
	} else if (sp == "palm") {
		const double H = 8.0 + 3.0 * hash01(salt, 1), bend = 1.2 + hash01(salt, 2);
		const auto spine = [&](double s) { return Vec3{bend * s * s, H * s, 0.0}; };
		for (int k = 0; k < 12; ++k) addLimb(g.wood, spine(k / 12.0), spine((k + 1) / 12.0), 0.22 - 0.06 * k / 12.0, 0.22 - 0.06 * (k + 1) / 12.0);
		const Vec3 top = spine(1.0);
		for (int f = 0; f < 13; ++f) {   // arching fronds, each a stem with leaflets
			const double a = 2.0 * kPi * (f + 0.3 * hash01(salt + f, 3)) / 13.0, len = 3.6 + 1.0 * hash01(salt + f, 4), rise = 0.5 + 0.4 * hash01(salt + f, 5);
			const Vec3 out{std::cos(a), 0.0, std::sin(a)};
			const auto path = [&](double s) { return top + out * (len * s) + Vec3{0.0, len * (rise * s - 0.95 * s * s), 0.0}; };
			const Vec3 side = normalize(cross(Vec3{0, 1, 0}, out));
			nature::addStrip(g.wood, path, [](double s) { return 0.06 * (1.0 - s); }, side, 6);
			for (int k = 1; k <= 16; ++k) {
				const double s = k / 17.0;
				const Vec3 p = path(s), tangent = normalize(path(std::min(1.0, s + 0.02)) - path(std::max(0.0, s - 0.02)));
				for (int sg = -1; sg <= 1; sg += 2) {
					const Vec3 dir = normalize(side * static_cast<double>(sg) + tangent * 0.5 + Vec3{0.0, -0.4, 0.0});
					nature::addLeaf(g.foliage, p, dir, normalize(cross(dir, tangent) * static_cast<double>(sg) + Vec3{0, 0.5, 0}), 0.9 * (1.0 - 0.6 * s), 0.12);
				}
			}
		}
	} else {   // shrub
		for (int k = 0; k < 5; ++k) {
			const double a = 2.0 * kPi * k / 5.0;
			addLimb(g.wood, {0.0, 0.0, 0.0}, {0.35 * std::cos(a), 0.8, 0.35 * std::sin(a)}, 0.03, 0.015);
		}
		if (!winter) addInto(g.foliage, nature::leafCluster(260, 1.8, 0.17, salt), mul(translate(0.0, 0.9, 0.0), scale(1.0, 0.75, 1.0)));
		if (spring) addInto(g.blossom, nature::leafCluster(40, 1.5, 0.1, salt + 5), translate(0.0, 0.95, 0.0));
	}
	return g;
}
// The far version: the trunk, and the foliage as a few lumps (or a cone, for a conifer) in its place.
inline Grown growFar(const std::string &sp, int v, bool winter) {
	Grown g;
	const int salt = 9001 + 977 * v + static_cast<int>(sp.size()) * 31;
	const Mesh lump = uvSphere(1.0, 10, 6);
	if (sp == "conifer") {
		const double H = 14.0 + 3.0 * hash01(salt, 1);
		addLimb(g.wood, {0, 0, 0}, {0, H * 0.25, 0}, 0.3, 0.2);
		const Mesh cone = lathe({{0.3 * H + 0.3, H * 0.18}, {0.18 * H, H * 0.5}, {0.06 * H, H * 0.85}, {0.0, H}}, 10);
		addInto(g.foliage, cone, identity());
		if (winter) addInto(g.snow, lathe({{0.2 * H, H * 0.45}, {0.05 * H, H * 0.88}, {0.0, H * 1.005}}, 10), identity());
	} else if (sp == "palm") {
		const double H = 8.0 + 3.0 * hash01(salt, 1), bend = 1.2 + hash01(salt, 2);
		addLimb(g.wood, {0, 0, 0}, {bend, H, 0}, 0.22, 0.16);
		addInto(g.foliage, lump, mul(translate(bend, H - 0.3, 0.0), scale(3.5, 0.9, 3.5)));
	} else if (sp == "shrub") {
		if (!winter) addInto(g.foliage, lump, mul(translate(0.0, 0.8, 0.0), scale(0.8, 0.55, 0.8)));
	} else {
		const bool birch = sp == "birch";
		const double trunkH = birch ? 4.5 + 1.5 * hash01(salt, 1) : 2.4 + 1.0 * hash01(salt, 1);
		addLimb(g.wood, {0, 0, 0}, {0, trunkH + 1.5, 0}, birch ? 0.14 : 0.32, 0.12);
		if (!winter)
			for (int k = 0; k < 5; ++k) {
				const double a = 2.0 * kPi * (k + hash01(salt + k, 7)) / 5.0, r = (birch ? 1.6 : 3.2) * (0.4 + 0.5 * hash01(salt + k, 8));
				const double s = (birch ? 1.5 : 2.9) * (0.7 + 0.5 * hash01(salt + k, 9));
				addInto(g.foliage, lump, mul(translate(r * std::cos(a), trunkH + (birch ? 3.0 : 4.0) + 1.5 * hash01(salt + k, 10), r * std::sin(a)), scale(s, s * 0.8, s)));
			}
	}
	return g;
}

// The colours: bark, and the foliage for the season (variant v: each broadleaf its own autumn colour).
inline Vec3 barkOf(const std::string &sp) {
	return sp == "birch" ? Vec3{0.62, 0.6, 0.55} : sp == "palm" ? Vec3{0.25, 0.2, 0.14} : sp == "conifer" ? Vec3{0.13, 0.09, 0.06} : Vec3{0.12, 0.09, 0.065};
}
inline Vec3 foliageOf(const std::string &sp, const std::string &season, int v) {
	if (sp == "conifer") return Vec3{0.045, 0.095, 0.045} * (0.85 + 0.1 * v);
	if (sp == "palm") return {0.10, 0.20, 0.05};
	if (season == "autumn" && sp != "shrub") {
		static const Vec3 autumn[4] = {{0.55, 0.22, 0.04}, {0.45, 0.07, 0.03}, {0.60, 0.45, 0.07}, {0.32, 0.18, 0.06}};
		return sp == "birch" ? Vec3{0.62, 0.48, 0.08} : autumn[v % 4];
	}
	if (season == "spring") return sp == "birch" ? Vec3{0.24, 0.42, 0.08} : Vec3{0.18, 0.36, 0.07};
	return sp == "birch" ? Vec3{0.14, 0.27, 0.06} : sp == "shrub" ? Vec3{0.09, 0.18, 0.05} : Vec3{0.08, 0.17, 0.045} * (0.85 + 0.1 * v);
}

// ---- between seasons ----
constexpr int kStages = 8;
inline bool deciduous(const std::string &sp) { return sp == "broadleaf" || sp == "birch" || sp == "shrub"; }
inline double leavesIn(const std::string &sp, const std::string &season) { return deciduous(sp) && season == "winter" ? 0.0 : 1.0; }
// A mesh with a share `keep` of its groups of `per` triangles (whole leaves: nature::addLeaf makes four), chosen by a hash of each group so a larger share keeps the
// smaller one's: the same leaves fall first.
inline Mesh shareOf(const Mesh &m, double keep, int per, int salt) {
	Mesh out;
	out.verts = m.verts, out.normals = m.normals, out.uvs = m.uvs;
	for (std::size_t t = 0; t < m.tris.size(); ++t)
		if (hash01(static_cast<int>(t / static_cast<std::size_t>(per)), salt) < keep) out.tris.push_back(m.tris[t]);
	return out;
}
// The far lumps (each a uvSphere of `n` points, one after another) shrunk towards their middles by `k`.
inline Mesh shrunk(const Mesh &m, double k, std::size_t n) {
	Mesh out = m;
	for (std::size_t b = 0; b + n <= out.verts.size(); b += n) {
		Vec3 mid{0, 0, 0};
		for (std::size_t i = b; i < b + n; ++i) mid = mid + out.verts[i] * (1.0 / n);
		for (std::size_t i = b; i < b + n; ++i) out.verts[i] = mid + (out.verts[i] - mid) * k;
	}
	return out;
}

}  // namespace detail

// The objects (near and far) of a species' variant in a season, as scene text: ObjectBegin ... ObjectEnd for each, its meshes PLY files in the film's cache.
inline std::string objectName(const std::string &sp, int v, const std::string &season, bool full) {
	return "forest_" + sp + "_" + std::to_string(v) + "_" + season + (full ? "_near" : "_far");
}
inline std::string objectText(const std::string &film, const std::string &sp, int v, const std::string &season, bool full) {
	const bool winter = season == "winter", spring = season == "spring";
	std::shared_ptr<detail::Grown> g;
	const auto get = [&]() -> const detail::Grown & {
		if (!g) g = std::make_shared<detail::Grown>(full ? detail::grow(sp, v, winter, spring) : detail::growFar(sp, v, winter));
		return *g;
	};
	const std::string base = objectName(sp, v, season, full);
	const std::string wood = film_cache::ply(film, base + "_wood", [&] { return get().wood; });
	const std::string leaves = film_cache::ply(film, base + "_foliage", [&] { return get().foliage; });
	const std::string snow = film_cache::ply(film, base + "_snow", [&] { return get().snow; });
	const std::string blossom = film_cache::ply(film, base + "_blossom", [&] { return get().blossom; });
	const Vec3 leaf = detail::foliageOf(sp, season, v);
	std::string out = nature::objectBegin(base) + "  AttributeBegin\n  " + coated(detail::barkOf(sp), 0.8) + "\n" + film_cache::plyShape(wood) + "  AttributeEnd\n";
	if (!leaves.empty())   // (leaves let a little light through; the far lumps are opaque)
		out += "  AttributeBegin\n  " + (full ? translucentMaterial(leaf, leaf * 1.3 + Vec3{0.03, 0.05, 0.0}) : diffuseMaterial(leaf * 0.9)) + "\n" + film_cache::plyShape(leaves) + "  AttributeEnd\n";
	if (!snow.empty()) out += "  AttributeBegin\n  " + diffuseMaterial({0.82, 0.84, 0.88}) + "\n" + film_cache::plyShape(snow) + "  AttributeEnd\n";
	if (!blossom.empty())
		out += "  AttributeBegin\n  " + translucentMaterial(v % 2 ? Vec3{0.85, 0.55, 0.65} : Vec3{0.88, 0.86, 0.84}, {0.3, 0.25, 0.25}) + "\n" + film_cache::plyShape(blossom) + "  AttributeEnd\n";
	return out + nature::objectEnd();
}

// The object of a species' variant at stage k (1 to kStages - 1) of the change from season `a` to `b`: the summer crown's leaves in the share between the two seasons'
// (falling into winter, coming out of it), in a colour between theirs (leaves keep their colour as they fall, and come out in spring's), blossom and snow likewise; the
// limbs of the bare tree whenever one of the two is winter.
inline std::string stageName(const std::string &sp, int v, const std::string &a, const std::string &b, int k, bool full) {
	return "forest_" + sp + "_" + std::to_string(v) + "_" + a + "-" + b + "_" + std::to_string(k) + (full ? "_near" : "_far");
}
inline std::string stageText(const std::string &film, const std::string &sp, int v, const std::string &a, const std::string &b, int k, bool full) {
	const double u = static_cast<double>(k) / detail::kStages;
	const double leaves = lerp(detail::leavesIn(sp, a), detail::leavesIn(sp, b), u), blossom = lerp(a == "spring" ? 1.0 : 0.0, b == "spring" ? 1.0 : 0.0, u),
				 snow = lerp(a == "winter" ? 1.0 : 0.0, b == "winter" ? 1.0 : 0.0, u);
	const bool bare = a == "winter" || b == "winter";
	const std::string base = stageName(sp, v, a, b, k, full);
	const int salt = 401 + 977 * v;
	std::shared_ptr<detail::Grown> leafy, winter;
	const auto getLeafy = [&]() -> const detail::Grown & {
		if (!leafy) leafy = std::make_shared<detail::Grown>(full ? detail::grow(sp, v, false, true) : detail::growFar(sp, v, false));
		return *leafy;
	};
	const auto getWinter = [&]() -> const detail::Grown & {
		if (!winter) winter = std::make_shared<detail::Grown>(full ? detail::grow(sp, v, true, false) : detail::growFar(sp, v, true));
		return *winter;
	};
	const std::string wood = film_cache::ply(film, base + "_wood", [&] { return bare ? getWinter().wood : getLeafy().wood; });
	const std::string foliage = film_cache::ply(film, base + "_foliage", [&] {
		if (leaves >= 1.0) return getLeafy().foliage;
		return full ? detail::shareOf(getLeafy().foliage, leaves, 4, salt) : detail::shrunk(getLeafy().foliage, std::sqrt(leaves), uvSphere(1.0, 10, 6).verts.size());
	});
	const std::string blooms = film_cache::ply(film, base + "_blossom", [&] { return full ? detail::shareOf(getLeafy().blossom, blossom, 4, salt + 1) : Mesh(); });
	const std::string snowy = film_cache::ply(film, base + "_snow", [&] { return full ? detail::shareOf(getWinter().snow, snow, 4, salt + 2) : snow > 0.5 ? getWinter().snow : Mesh(); });
	const auto colourIn = [&](const std::string &season, const std::string &other) { return detail::foliageOf(sp, detail::leavesIn(sp, season) > 0.0 ? season : other, v); };
	const Vec3 leaf = lerp(colourIn(a, b), colourIn(b, a), u);
	std::string out = nature::objectBegin(base) + "  AttributeBegin\n  " + coated(detail::barkOf(sp), 0.8) + "\n" + film_cache::plyShape(wood) + "  AttributeEnd\n";
	if (!foliage.empty() && leaves > 0.0)
		out += "  AttributeBegin\n  " + (full ? translucentMaterial(leaf, leaf * 1.3 + Vec3{0.03, 0.05, 0.0}) : diffuseMaterial(leaf * 0.9)) + "\n" + film_cache::plyShape(foliage) + "  AttributeEnd\n";
	if (!snowy.empty() && snow > 0.0) out += "  AttributeBegin\n  " + diffuseMaterial({0.82, 0.84, 0.88}) + "\n" + film_cache::plyShape(snowy) + "  AttributeEnd\n";
	if (!blooms.empty() && blossom > 0.0)
		out += "  AttributeBegin\n  " + translucentMaterial(v % 2 ? Vec3{0.85, 0.55, 0.65} : Vec3{0.88, 0.86, 0.84}, {0.3, 0.25, 0.25}) + "\n" + film_cache::plyShape(blooms) + "  AttributeEnd\n";
	return out + nature::objectEnd();
}

// Where the trees of a forest stand: tried at points of a fixed sequence over its rectangle, kept where the land suits the species. `ground` may be null (flat ground at
// y 0), `waterLevel` below -1e29 when there is none, `rivers` the channels to keep out of.
inline std::vector<Tree> plant(const Forest &f, const terrain::Field *ground, double waterLevel, const std::vector<terrain::Spec::Channel> &rivers) {
	std::vector<Tree> out;
	double x0 = f.x0, z0 = f.z0, x1 = f.x1, z1 = f.z1;
	if (!f.area) {
		if (ground) x0 = ground->sx0, z0 = ground->sz0, x1 = ground->sx0 + ground->spec.size, z1 = ground->sz0 + ground->spec.size;
		else x0 = z0 = -100.0, x1 = z1 = 100.0;
	}
	const double base = ground ? ground->spec.y : 0.0, height = ground ? std::max(1.0, ground->spec.height) : 1.0;
	const double treeline = f.below >= 0.0 ? f.below : ground && ground->spec.kind == "mountains" ? 0.55 * height : 1e9;
	for (int k = 0; k < f.count * 40 && static_cast<int>(out.size()) < f.count; ++k) {
		const int s = f.seed * 7919 + k;
		const double x = x0 + (x1 - x0) * hash01(s, 1), z = z0 + (z1 - z0) * hash01(s, 2);
		double y = 0.0, frac = 0.0, wet = 0.5;
		if (ground) {
			if (!ground->inside(x, z)) continue;
			y = ground->heightAt(x, z);
			if (1.0 - ground->normalAt(x, z, 2.0).y > f.steep) continue;   // (no trees on cliffs)
			frac = (y - base) / height;
			wet = ground->flowAt(x, z);
		}
		if (y < waterLevel + 0.6) continue;   // (not in the water, nor on its very edge)
		if (y - base > treeline + 0.05 * height * (hash01(s, 3) - 0.5) || y - base < f.above) continue;
		bool inRiver = false;
		for (const terrain::Spec::Channel &c : rivers)
			for (std::size_t i = 0; i + 1 < c.path.size() && !inRiver; i += 2) inRiver = std::hypot(c.path[i].x - x, c.path[i].z - z) < 0.5 * c.width + 3.0;
		if (inRiver) continue;
		if (hash01(s, 4) > 0.3 + 0.7 * std::min(1.0, 2.0 * wet + 0.3)) continue;   // (denser where water gathers)
		Tree t;
		t.species = f.kind;
		if (f.kind == "mixed") {   // by height: broadleaf trees and birches low down, conifers coming in, then conifers alone below the tree line
			const double up = std::min(1.0, frac / std::max(0.05, std::min(1.0, treeline / height)));
			const double r = hash01(s, 5);
			if (up < 0.35) t.species = r < 0.55 ? "broadleaf" : r < 0.8 ? "birch" : r < 0.92 ? "shrub" : "conifer";
			else if (up < 0.7) t.species = r < 0.3 ? "broadleaf" : r < 0.45 ? "birch" : r < 0.55 ? "shrub" : "conifer";
			else t.species = r < 0.9 ? "conifer" : "shrub";
		}
		t.variant = static_cast<int>(hash01(s, 6) * kVariants) % kVariants;
		t.at = {x, y - 0.05, z};
		t.scale = f.scale0 + (f.scale1 - f.scale0) * hash01(s, 7);
		if (t.species == "conifer" && ground) t.scale *= 1.0 - 0.35 * std::min(1.0, frac / std::max(0.05, treeline / height));   // (smaller towards the tree line)
		t.turn = 360.0 * hash01(s, 8);
		out.push_back(t);
	}
	return out;
}

// A forest's trees at t (the wind's time; `filmT` the film's, for a change of season and growing), seen from `eye` (story space; near ones in full), as scene text: the
// objects it uses, then a placement for each tree, leaning in the wind.
inline std::string text(const std::string &film, const std::vector<Tree> &trees, const Forest &f, double t, double filmT, const Vec3 &eyeStory, const dressing::Gusts &gusts = {}) {
	std::string objects, places;
	std::vector<std::string> made;
	const auto need = [&](const std::string &name, const std::function<std::string()> &make) {
		if (std::find(made.begin(), made.end(), name) == made.end()) {
			made.push_back(name);
			objects += make();
		}
		return name;
	};
	const double turnSpan = std::max(1e-6, f.turn1 - f.turn0), growSpan = std::max(1e-6, f.grow1 - f.grow0);
	for (const Tree &tr : trees) {
		const Vec3 foot{-tr.at.x, tr.at.y, tr.at.z};   // (story space)
		const int own = static_cast<int>(std::floor(tr.at.x * 7.31 + tr.at.z * 3.17)) ^ (tr.variant * 101);   // (each tree's own moment)
		double size = tr.scale;
		if (f.grow0 >= 0.0) {   // growing: from a sapling over its own half of the time
			const double start = f.grow0 + 0.5 * growSpan * hash01(own, 31);
			size *= 0.03 + 0.97 * smoothstep(start, start + 0.5 * growSpan, filmT);
		}
		const bool full = length(foot - eyeStory) < f.fullWithin * size;
		int stage = 0;
		if (!f.seasonTo.empty()) {   // turning: over its own 40% of the change
			const double start = f.turn0 + 0.6 * turnSpan * hash01(own, 32);
			stage = static_cast<int>(std::lround(smoothstep(start, start + 0.4 * turnSpan, filmT) * detail::kStages));
		}
		const std::string &sp = tr.species;
		const int v = tr.variant;
		const std::string name = stage == 0 ? need(objectName(sp, v, f.season, full), [&] { return objectText(film, sp, v, f.season, full); })
								 : stage == detail::kStages ? need(objectName(sp, v, f.seasonTo, full), [&] { return objectText(film, sp, v, f.seasonTo, full); })
															: need(stageName(sp, v, f.season, f.seasonTo, stage, full), [&] { return stageText(film, sp, v, f.season, f.seasonTo, stage, full); });
		const Vec3 w = nature::windLean(foot.x, foot.z, t, sp == "conifer" ? 0.008 : 0.014) + dressing::gustAt(gusts, foot.x, foot.z, t) * (sp == "conifer" ? 0.04 : 0.07);
		places += nature::placeObject(name, mul(translate(foot), nature::shearXZ(w.x, w.z), rotate({0, 1, 0}, tr.turn), scale(size)));
	}
	return trees.empty() ? std::string() : "# a forest (film_forest.h)\n" + objects + places;
}

}  // namespace forest
}  // namespace cartoon
