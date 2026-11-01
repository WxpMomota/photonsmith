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
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "cartoon_lib.h"
#include "film_cache.h"
#include "film_dressing.h"
#include "film_moss.h"
#include "film_sky.h"
#include "film_terrain.h"
#include "nature_lib.h"

namespace cartoon {
namespace forest {

inline const std::vector<std::string> &species() {
	static const std::vector<std::string> k = {"conifer", "broadleaf", "birch", "palm", "shrub", "acacia", "kapok", "cactus", "fern", "oak", "beech", "pine"};
	return k;
}
inline const std::vector<std::string> &kinds() {   // what a `forest` line may ask for: a species, or a mixed wood chosen by height
	static const std::vector<std::string> k = {"conifer", "broadleaf", "birch", "palm", "shrub", "acacia", "kapok", "cactus", "fern", "oak", "beech", "pine", "mixed", "savanna", "jungle", "desert", "tundra"};
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
	struct Change {   // (a further change of season after that one: `then WINTER from T0 to T1`)
		std::string to;
		double t0 = 0.0, t1 = 0.0;
	};
	std::vector<Change> then;
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
	const bool ownUvs = src.uvs.size() == src.verts.size();
	if (ownUvs || !dst.uvs.empty()) dst.uvs.resize(dst.verts.size());
	for (std::size_t i = 0; i < src.verts.size(); ++i) {
		dst.verts.push_back(transformPoint(m, src.verts[i]));
		const Vec3 n = transformDir(m, i < src.normals.size() ? src.normals[i] : Vec3{0.0, 1.0, 0.0});
		dst.normals.push_back(length(n) > 1e-12 ? normalize(n) : Vec3{0.0, 1.0, 0.0});
		if (ownUvs || !dst.uvs.empty()) dst.uvs.push_back(ownUvs ? src.uvs[i] : std::pair<double, double>{0.0, 0.0});
	}
	for (const auto &t : src.tris) dst.tris.push_back({t[0] + base, t[1] + base, t[2] + base});
}
// A tapering limb from `a` to `b`, radius ra at a and rb at b.
inline void addLimb(Mesh &dst, Vec3 a, Vec3 b, double ra, double rb) {
	const double len = length(b - a);
	if (len < 1e-6) return;
	Mesh tube = lathe({{ra, 0.0}, {rb, len}}, 6);
	for (auto &uv : tube.uvs) uv = {uv.first * std::max(1.0, std::round(2.0 * kPi * ra / nature::kBarkTile)), uv.second * len / nature::kBarkTile};   // (bark at its real size)
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
	std::size_t coreVerts = 0, coreTris = 0;   // (what comes before the foliage's leaves: a conifer's dark core)
};

// A crown grown as a tree grows one (space colonisation: Runions, Lane and Prusinkiewicz, 2007). The open air a crown can fill is a scatter of points in a lobed, uneven shape, with
// hollows in it. Each point draws the branch tip nearest it, a tip that is drawn grows a step that way, and a point a branch has reached is used up, so branches fork where the
// space does, compete for it and stop when it is gone: a crown of limbs and gaps, with an outline of its own. Branches thicken towards the trunk as what they carry adds up.
struct Crown {
	Vec3 root, centre, radii;                   // (where the limbs start; the shape they fill)
	int points = 1100, leaves = 28;             // (how many points of air; leaves at a twig's tip)
	double step = 0.36, trunk = 0.24;           // (how far a branch grows at a time; how thick the first limb is)
	double size = 0.95, leafLen = 0.26, droop = 0.0;
};
struct Branch {
	Vec3 at;
	int parent = -1, children = 0;
	double radius = 0.012;
};
inline std::vector<Branch> grownBranches(const Crown &c, int salt) {
	static std::mutex lock;
	static std::map<std::string, std::vector<Branch>> made;
	char key[160];
	std::snprintf(key, sizeof key, "%d|%.3f %.3f %.3f|%.3f %.3f %.3f|%d|%.3f", salt, c.root.y, c.centre.y, c.radii.x, c.radii.y, c.radii.z, c.trunk, c.points, c.step);
	const std::lock_guard<std::mutex> hold(lock);
	if (const auto found = made.find(key); found != made.end()) return found->second;
	const double reach = 4.0 * c.step, used = 1.5 * c.step;
	std::vector<Vec3> air;
	for (int k = 0; static_cast<int>(air.size()) < c.points && k < c.points * 40; ++k) {
		const int s = salt * 37 + k;
		const Vec3 d{2.0 * hash01(s, 1) - 1.0, 2.0 * hash01(s, 2) - 1.0, 2.0 * hash01(s, 3) - 1.0};
		const double out = length(d);
		if (out > 1.0 || out < 1e-6 || d.y < -0.62) continue;
		const Vec3 along = d * (1.0 / out);
		if (out > 0.62 + 0.7 * nature::fbm2(along.x * 1.7 + along.y * 0.9 + 3.0, along.z * 1.7 - along.y * 0.8 + 7.0, salt)) continue;   // (the lobes of its outline)
		if (nature::noise2(d.x * 2.3 + 5.0, d.z * 2.3 + d.y * 1.9, salt + 3) < 0.3) continue;                                             // (hollows, where the sky shows)
		air.push_back(c.centre + Vec3{d.x * c.radii.x, d.y * c.radii.y, d.z * c.radii.z});
	}
	std::vector<Branch> nodes;
	Branch first;
	first.at = c.root;
	nodes.push_back(first);
	std::map<long long, std::vector<int>> near_;   // (the branches by the squares of space they are in)
	const auto cellOf = [reach](const Vec3 &p, int dx, int dy, int dz) {
		const long long x = static_cast<long long>(std::floor(p.x / reach)) + dx + 4096, y = static_cast<long long>(std::floor(p.y / reach)) + dy + 4096, z = static_cast<long long>(std::floor(p.z / reach)) + dz + 4096;
		return (x << 26) ^ (y << 13) ^ z;
	};
	near_[cellOf(c.root, 0, 0, 0)].push_back(0);
	// up from the trunk until the first points are in reach
	while (nodes.back().at.y < c.centre.y - 0.75 * c.radii.y && nodes.size() < 40) {
		Branch next;
		next.at = nodes.back().at + Vec3{0.0, c.step, 0.0}, next.parent = static_cast<int>(nodes.size()) - 1;
		++nodes.back().children;
		nodes.push_back(next);
		near_[cellOf(next.at, 0, 0, 0)].push_back(static_cast<int>(nodes.size()) - 1);
	}
	for (int round = 0; round < 120 && !air.empty(); ++round) {
		std::map<int, Vec3> pull;
		std::vector<Vec3> left;
		left.reserve(air.size());
		for (const Vec3 &p : air) {
			int best = -1;
			double closest = reach;
			for (int dz = -1; dz <= 1; ++dz)
				for (int dy = -1; dy <= 1; ++dy)
					for (int dx = -1; dx <= 1; ++dx)
						if (const auto cell = near_.find(cellOf(p, dx, dy, dz)); cell != near_.end())
							for (const int n : cell->second) {
								const double d = length(nodes[static_cast<std::size_t>(n)].at - p);
								if (d < closest) closest = d, best = n;
							}
			if (closest < used) continue;   // (reached: used up)
			left.push_back(p);
			if (best >= 0) {
				Vec3 &sum = pull[best];
				sum = sum + normalize(p - nodes[static_cast<std::size_t>(best)].at);
			}
		}
		air.swap(left);
		if (pull.empty()) break;
		for (const auto &[n, sum] : pull) {
			const Branch from = nodes[static_cast<std::size_t>(n)];
			const Vec3 last = from.parent >= 0 ? normalize(from.at - nodes[static_cast<std::size_t>(from.parent)].at) : Vec3{0.0, 1.0, 0.0};
			const int s = salt * 11 + n * 3 + round;
			const Vec3 wander{hash01(s, 4) - 0.5, hash01(s, 5) - 0.5, hash01(s, 6) - 0.5};
			const Vec3 d = normalize(normalize(sum) + last * 0.45 + wander * 0.35 + Vec3{0.0, 0.12 - c.droop, 0.0});
			Branch next;
			next.at = from.at + d * c.step, next.parent = n;
			++nodes[static_cast<std::size_t>(n)].children;
			nodes.push_back(next);
			near_[cellOf(next.at, 0, 0, 0)].push_back(static_cast<int>(nodes.size()) - 1);
		}
	}
	// a branch is as thick as what it carries (the children come after their parents)
	for (std::size_t n = nodes.size(); n-- > 1;) {
		Branch &parent = nodes[static_cast<std::size_t>(nodes[n].parent)];
		parent.radius = std::pow(std::pow(parent.radius, 2.4) + std::pow(nodes[n].radius, 2.4), 1.0 / 2.4);
	}
	const double k = c.trunk / std::max(1e-6, nodes[0].radius);
	for (Branch &b : nodes) b.radius = std::max(0.006, b.radius * std::pow(k, 0.5) * 0.6);
	nodes[0].radius = c.trunk;
	return made[key] = nodes;
}
// What a branch ends in: a spray of thin shoots, with the leaves along them in turn, each out from its shoot on its stalk and facing the light. In winter the shoots are what shows against
// the sky. (Leaves used to float about a branch's end in a ball.)
inline void addSpray(Grown &g, const Vec3 &at, int leaves, double reach, double leafLen, double droop, int s, bool bare) {
	constexpr int kShoots = 4;
	for (int k = 0; k < kShoots; ++k) {
		const int own = s * 7 + k;
		const Vec3 d = normalize(Vec3{hash01(own, 8) - 0.5, hash01(own, 9) - 0.25 - droop, hash01(own, 10) - 0.5});
		const double len = reach * (0.6 + 0.6 * hash01(own, 11));
		const Vec3 mid = at + d * (0.5 * len), tip = mid + normalize(d + Vec3{0.0, 0.3 - droop, 0.0}) * (0.5 * len);
		addLimb(g.wood, at, mid, 0.007, 0.0045);
		addLimb(g.wood, mid, tip, 0.0045, 0.0015);
		if (bare) continue;
		const Vec3 side = normalize(cross(d, Vec3{0.31, 1.0, 0.17}));
		const int n = std::max(1, leaves / kShoots);
		for (int j = 0; j < n; ++j) {
			const double u = (j + 1.0) / (n + 0.4), sign = j % 2 ? 1.0 : -1.0;
			const Vec3 p = u < 0.5 ? lerp(at, mid, 2.0 * u) : lerp(mid, tip, 2.0 * u - 1.0);
			const Vec3 dir = normalize(side * sign + d * 0.7 + Vec3{0.0, -0.3, 0.0});
			const Vec3 facing = normalize(Vec3{0.45 * (hash01(own + j, 12) - 0.5), 1.0, 0.45 * (hash01(own + j, 13) - 0.5)});
			nature::addLeaf(g.foliage, p + dir * (0.09 * leafLen), dir, facing, leafLen * (0.8 + 0.4 * hash01(own + j, 14)), 0.56 * leafLen);
		}
	}
}
// The limbs of a crown into `g.wood`, and at each twig's end a spray of leaves, or in winter (`bare`) the finest twigs showing against the sky.
inline void colonise(Grown &g, const Crown &c, int salt, bool bare, bool blossom) {
	const std::vector<Branch> nodes = grownBranches(c, salt);
	Grown crown;
	for (std::size_t n = 1; n < nodes.size(); ++n) addLimb(crown.wood, nodes[static_cast<std::size_t>(nodes[n].parent)].at, nodes[n].at, nodes[static_cast<std::size_t>(nodes[n].parent)].radius, nodes[n].radius);
	addInto(g.wood, crown.wood, identity());
	for (std::size_t n = 0; n < nodes.size(); ++n) {
		const int s = salt * 19 + static_cast<int>(n) * 7;
		if (nodes[n].children > 0 && !(nodes[n].radius < 0.03 && hash01(s, 7) < 0.25)) continue;   // (the leaves at the twigs' ends, and a few along the thinnest branches)
		addSpray(g, nodes[n].at, c.leaves, 0.72 * c.size * (0.8 + 0.4 * hash01(s, 12)), c.leafLen, c.droop, s, bare);
		if (bare) continue;
		if (blossom && hash01(s, 14) < 0.5) addInto(g.blossom, nature::leafCluster(c.leaves / 3, c.size * 0.95, c.leafLen * 0.7, s + 1), translate(nodes[n].at + Vec3{0.0, 0.1, 0.0}));
	}
}

// The picture of leaves (leafPicture): kLeafCells x kLeafCells leaves, a row for how exposed a leaf is, a column of its own. Each leaf of the foliage takes one: a row by how far
// up and out in the crown it is, where the light first turns a leaf, a column by chance. Near and far versions and every stage of a season keep the same leaf in the same cell.
constexpr int kLeafCells = 8;
inline void placeLeaves(Mesh &m, std::size_t coreVerts, int salt) {
	if (m.uvs.size() != m.verts.size() || m.verts.size() <= coreVerts) return;
	Vec3 lo{1e30, 1e30, 1e30}, hi{-1e30, -1e30, -1e30};
	for (std::size_t i = coreVerts; i < m.verts.size(); ++i) {
		const Vec3 &p = m.verts[i];
		lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)}, hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
	}
	const Vec3 mid = (lo + hi) * 0.5;
	const double rx = std::max(1e-6, 0.5 * (hi.x - lo.x)), rz = std::max(1e-6, 0.5 * (hi.z - lo.z)), tall = std::max(1e-6, hi.y - lo.y);
	for (std::size_t i = 0; i < coreVerts; ++i) m.uvs[i] = {0.5 / kLeafCells, 0.5 / kLeafCells};   // (a conifer's core: a leaf deep inside)
	const std::size_t leaves = (m.verts.size() - coreVerts) / 6;
	for (std::size_t leaf = 0; leaf < leaves; ++leaf) {
		const std::size_t from = coreVerts + leaf * 6;
		Vec3 at{0.0, 0.0, 0.0};
		for (std::size_t i = from; i < from + 6; ++i) at = at + m.verts[i] * (1.0 / 6.0);
		const double up = (at.y - lo.y) / tall, out = std::hypot((at.x - mid.x) / rx, (at.z - mid.z) / rz);
		const double exposed = std::clamp(0.55 * up + 0.45 * std::min(1.0, out) + 0.25 * (hash01(static_cast<int>(leaf), salt + 41) - 0.5), 0.0, 0.999);
		const int row = static_cast<int>(exposed * kLeafCells), col = static_cast<int>(hash01(static_cast<int>(leaf), salt + 42) * kLeafCells) % kLeafCells;
		for (std::size_t i = from; i < from + 6; ++i) m.uvs[i] = {(col + 0.04 + 0.92 * m.uvs[i].first) / kLeafCells, (row + 0.04 + 0.92 * m.uvs[i].second) / kLeafCells};
	}
}

// A leaf has a shape: folded along its midrib (a little, a lot), its sides lifting, its tip hanging, some twisted, each its own. The light then falls differently on its two halves.
inline void shapeLeaves(Mesh &m, std::size_t coreVerts, int salt) {
	if (m.verts.size() <= coreVerts || m.normals.size() != m.verts.size()) return;
	const std::size_t leaves = (m.verts.size() - coreVerts) / 6;
	for (std::size_t leaf = 0; leaf < leaves; ++leaf) {
		const std::size_t f = coreVerts + leaf * 6;
		const int s = salt * 7 + static_cast<int>(leaf);
		const Vec3 base = m.verts[f], tip = m.verts[f + 3], up = m.normals[f];
		const double len = length(tip - base);
		if (len < 1e-9) continue;
		const Vec3 dir = (tip - base) * (1.0 / len), side = normalize(cross(up, dir));
		const double fold = 0.1 + 0.55 * hash01(s, 91), droop = 0.34 * hash01(s, 92), twist = 0.36 * (hash01(s, 93) - 0.5);
		for (const int i : {1, 2, 4, 5}) {
			const Vec3 p = m.verts[f + i] - base;
			const double off = dot(p, side), along = dot(p, dir) / len;
			m.verts[f + i] = base + p + up * (std::abs(off) * fold + off * twist) + Vec3{0.0, -1.0, 0.0} * (droop * len * along * along);
			m.normals[f + i] = normalize(up - side * ((off > 0.0 ? fold : -fold) + twist));
		}
		m.verts[f + 3] = tip + Vec3{0.0, -1.0, 0.0} * (droop * len);
	}
}
// Roots running out from the foot of a trunk into the ground: a tree stands in the land, not on it.
inline void addRoots(Grown &g, int count, double reach, double thick, int salt) {
	for (int k = 0; k < count; ++k) {
		const double a = 2.0 * kPi * (k + 0.5 * hash01(salt + k, 31)) / count, out = reach * (0.7 + 0.6 * hash01(salt + k, 32));
		const Vec3 d{std::cos(a), 0.0, std::sin(a)};
		addLimb(g.wood, Vec3{0.0, 0.55 * reach, 0.0} + d * (0.3 * reach), d * (0.65 * out) + Vec3{0.0, 0.08 * reach, 0.0}, thick, 0.55 * thick);
		addLimb(g.wood, d * (0.65 * out) + Vec3{0.0, 0.08 * reach, 0.0}, d * out + Vec3{0.0, -0.06 * reach, 0.0}, 0.55 * thick, 0.12 * thick);
	}
}

// Each species grown at its own size (metres), variant `v`.
inline Grown grow(const std::string &sp, int v, bool winter, bool spring) {
	Grown g;
	const int salt = 9001 + 977 * v + static_cast<int>(sp.size()) * 31;
	if (sp == "conifer") {
		const double H = 14.0 + 3.0 * hash01(salt, 1);
		addInto(g.wood, nature::trunk(H, 0.24, 0.0, salt), identity());
		addRoots(g, 6, 0.9, 0.11, salt);
		const int tiers = 20;
		addInto(g.foliage, lathe({{0.16 * H, H * 0.22}, {0.1 * H, H * 0.55}, {0.03 * H, H * 0.9}, {0.0, H * 0.98}}, 9), identity());   // (the dark core you see between the tiers)
		g.coreVerts = g.foliage.verts.size(), g.coreTris = g.foliage.tris.size();
		for (int k = 0; k < tiers; ++k) {
			const double h = H * (0.2 + 0.78 * k / (tiers - 1.0)), r0 = 0.3 * H * std::pow(1.0 - (h / H - 0.2) / 0.8, 1.1) + 0.3;
			const int arms = 9;
			for (int a = 0; a < arms; ++a) {
				if (hash01(salt + k * 13 + a, 3) < 0.16) continue;   // (no tree has every branch: gaps, and the tiers stop reading as tiers)
				const double yaw = 2.0 * kPi * (a + 0.8 * hash01(salt + k * 13 + a, 2)) / arms + k * 0.7, drop = 0.04 * H * (hash01(salt + k * 13 + a, 4) - 0.5);
				const Vec3 out{std::cos(yaw), 0.0, std::sin(yaw)};
				const double r = r0 * (0.62 + 0.6 * hash01(salt + k * 13 + a, 5));
				const Vec3 from{0.0, h + drop, 0.0}, tip = from + out * r + Vec3{0.0, -0.22 * r, 0.0};
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
		addRoots(g, 7, 1.1, 0.14, salt);
		Crown crown;
		crown.root = {0.0, trunkH * 0.95, 0.0}, crown.centre = {0.0, trunkH + 3.2, 0.0}, crown.radii = {4.2, 3.4, 4.2}, crown.trunk = 0.16;   // (no thicker than the trunk it grows from)
		colonise(g, crown, salt, winter, spring);
	} else if (sp == "oak") {   // low and broad: a short thick trunk, heavy limbs going out as much as up, a wide uneven crown
		const double trunkH = 2.0 + 0.9 * hash01(salt, 1);
		addInto(g.wood, nature::trunk(trunkH, 0.5, 0.3 * (hash01(salt, 2) - 0.5), salt), identity());
		addRoots(g, 8, 1.3, 0.18, salt);
		Crown crown;
		crown.root = {0.0, trunkH * 0.92, 0.0}, crown.centre = {0.0, trunkH + 2.9, 0.0}, crown.radii = {6.4, 3.7, 6.4}, crown.trunk = 0.24;
		crown.points = 1500, crown.step = 0.44, crown.leaves = 30, crown.size = 0.95, crown.leafLen = 0.2;
		colonise(g, crown, salt, winter, false);
	} else if (sp == "beech") {   // a tall smooth grey column going high before it divides, and a tall dome
		const double trunkH = 5.2 + 1.2 * hash01(salt, 1);
		addInto(g.wood, nature::trunk(trunkH + 1.5, 0.36, 0.2 * (hash01(salt, 2) - 0.5), salt), identity());
		addRoots(g, 6, 0.9, 0.12, salt);
		Crown crown;
		crown.root = {0.0, trunkH * 0.9, 0.0}, crown.centre = {0.0, trunkH + 4.2, 0.0}, crown.radii = {4.4, 6.0, 4.4}, crown.trunk = 0.17;
		crown.points = 1300, crown.step = 0.36, crown.leaves = 30, crown.size = 0.85, crown.leafLen = 0.15, crown.droop = 0.12;
		colonise(g, crown, salt, winter, spring);
	} else if (sp == "pine") {   // a bare red-brown trunk going up through the wood, the old limbs long gone, and a flat irregular top of long dark sprays
		const double H = 15.0 + 4.0 * hash01(salt, 1);
		addInto(g.wood, nature::trunk(H, 0.3, 0.7 * (hash01(salt, 2) - 0.5), salt), identity());
		addRoots(g, 6, 0.9, 0.11, salt);
		for (int k = 0; k < 26; ++k) {
			const double h = H * (0.56 + 0.44 * k / 25.0), yaw = 2.0 * kPi * hash01(salt + k, 3);
			if (hash01(salt + k, 4) < 0.12) continue;
			const double r = (0.66 - 0.5 * k / 25.0) * (0.3 * H) * (0.65 + 0.7 * hash01(salt + k, 5));
			const Vec3 out{std::cos(yaw), 0.0, std::sin(yaw)}, from{0.0, h, 0.0}, tip = from + out * r + Vec3{0.0, r * (0.12 + 0.3 * hash01(salt + k, 6)), 0.0};
			addLimb(g.wood, from, tip, 0.05 + 0.012 * r, 0.014);
			for (int p = 1; p <= 3; ++p) {
				const Vec3 at = from + (tip - from) * (0.4 + 0.3 * p);
				const Mesh pad = nature::leafCluster(70, 1.7 * (0.8 + 0.2 * p), 0.3, salt + k * 31 + p);
				addInto(g.foliage, pad, mul(translate(at), rotate(Vec3{0, 1, 0}, yaw * 57.3), scale(1.0, 0.38, 1.0)));
				if (winter) addInto(g.snow, pad, mul(translate(at + Vec3{0.0, 0.06, 0.0}), rotate(Vec3{0, 1, 0}, yaw * 57.3), scale(0.85, 0.16, 0.85)));
			}
		}
	} else if (sp == "birch") {
		const double trunkH = 4.5 + 1.5 * hash01(salt, 1);
		addInto(g.wood, nature::trunk(trunkH + 3.0, 0.14, 0.4 * (hash01(salt, 2) - 0.5), salt), identity());
		Crown crown;
		crown.root = {0.0, trunkH * 0.55, 0.0}, crown.centre = {0.0, trunkH + 2.3, 0.0}, crown.radii = {2.3, 3.8, 2.3};
		crown.points = 800, crown.step = 0.3, crown.trunk = 0.09, crown.leaves = 22, crown.size = 0.7, crown.leafLen = 0.16, crown.droop = 0.28;   // (slight, with hanging twigs)
		colonise(g, crown, salt, winter, false);
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
	} else if (sp == "acacia") {   // a short trunk splitting into limbs that spread up and out to a flat, wide umbrella of a crown
		const double trunkH = 1.8 + 0.8 * hash01(salt, 1), lean = 0.4 * (hash01(salt, 2) - 0.5), top = 5.2 + 1.0 * hash01(salt, 3), reach = 3.6 + 1.0 * hash01(salt, 4);
		addInto(g.wood, nature::trunk(trunkH, 0.22, lean, salt), identity());
		addRoots(g, 5, 0.8, 0.09, salt);
		const Vec3 fork{lean * trunkH, trunkH, 0.0};
		const int limbs = 3 + static_cast<int>(2 * hash01(salt, 5));
		for (int k = 0; k < limbs; ++k) {
			const double a = 2.0 * kPi * (k + 0.3 * hash01(salt + k, 6)) / limbs, r = reach * (0.45 + 0.35 * hash01(salt + k, 7));
			const Vec3 end{fork.x + r * std::cos(a), top - 0.4, r * std::sin(a)}, mid = fork + (end - fork) * 0.5 + Vec3{0.0, 0.4, 0.0};
			addLimb(g.wood, fork, mid, 0.13, 0.09);
			addLimb(g.wood, mid, end, 0.09, 0.05);
		}
		for (int k = 0; k < 120; ++k) {   // the umbrella: clusters spread over a flat disc, thin top to bottom
			const double a = 2.0 * kPi * hash01(salt + k, 8), r = reach * std::sqrt(hash01(salt + k, 9)) * 1.05;
			addInto(g.foliage, nature::leafCluster(90, 1.7, 0.2, salt + 31 * k), mul(translate(fork.x + r * std::cos(a), top + 0.25 * (hash01(salt + k, 10) - 0.5), r * std::sin(a)), scale(1.0, 0.35, 1.0)));
		}
	} else if (sp == "kapok") {   // a jungle giant: a tall straight trunk on buttress roots, a broad dome of a crown high above the rest of the forest
		const double H = 24.0 + 6.0 * hash01(salt, 1);
		addInto(g.wood, nature::trunk(H, 0.75, 0.0, salt), identity());
		for (int k = 0; k < 5; ++k) {   // the buttresses: thin fins of wood out from the trunk's foot
			const double a = 2.0 * kPi * (k + 0.3 * hash01(salt + k, 2)) / 5.0, out = 2.2 + 1.0 * hash01(salt + k, 3), up = 2.5 + 1.5 * hash01(salt + k, 4);
			const Vec3 d{std::cos(a), 0.0, std::sin(a)}, side{-d.z, 0.0, d.x};
			const Vec3 pts[3] = {d * 0.5 + Vec3{0.0, up, 0.0}, d * out, d * 0.5};
			for (const double w : {-0.07, 0.07}) {
				const int b = static_cast<int>(g.wood.verts.size());
				for (const Vec3 &p : pts) {
					g.wood.verts.push_back(p + side * w);
					g.wood.normals.push_back(side * (w > 0 ? 1.0 : -1.0));
				}
				if (w > 0) g.wood.tris.push_back({b, b + 1, b + 2});
				else g.wood.tris.push_back({b, b + 2, b + 1});
			}
		}
		Crown crown;
		crown.root = {0.0, H * 0.82, 0.0}, crown.centre = {0.0, H + 2.5, 0.0}, crown.radii = {8.4, 3.6, 8.4};
		crown.points = 1500, crown.step = 0.7, crown.trunk = 0.45, crown.leaves = 34, crown.size = 1.5, crown.leafLen = 0.32;
		colonise(g, crown, salt, false, false);
	} else if (sp == "snag") {   // a dead tree still standing: its top gone, a few bare limbs broken short
		const double H = 5.0 + 5.0 * hash01(salt, 1);
		addInto(g.wood, nature::trunk(H, 0.26, 0.5 * (hash01(salt, 2) - 0.5), salt), identity());
		addRoots(g, 5, 0.9, 0.11, salt);
		for (int k = 0; k < 6; ++k) {
			const double a = 2.0 * kPi * hash01(salt + k, 3), at = H * (0.45 + 0.5 * hash01(salt + k, 4)), len = 0.6 + 1.6 * hash01(salt + k, 5);
			addLimb(g.wood, {0.0, at, 0.0}, Vec3{std::cos(a) * len, at + 0.5 * len * hash01(salt + k, 6), std::sin(a) * len}, 0.06, 0.012);
		}
	} else if (sp == "log") {   // a tree that fell long ago, lying half sunk in the ground, with the stubs of its limbs
		const double L = 5.0 + 4.0 * hash01(salt, 1);
		addLimb(g.wood, {-0.5 * L, 0.2, 0.0}, {0.0, 0.17, 0.08}, 0.3, 0.25);
		addLimb(g.wood, {0.0, 0.17, 0.08}, {0.5 * L, 0.13, 0.0}, 0.25, 0.14);
		for (int k = 0; k < 5; ++k) {
			const double along = L * (hash01(salt + k, 2) - 0.2) * 0.5, a = 2.0 * kPi * hash01(salt + k, 3);
			addLimb(g.wood, {along, 0.2, 0.05}, {along + 0.2, 0.2 + 0.5 * std::abs(std::sin(a)), 0.05 + 0.5 * std::cos(a)}, 0.04, 0.012);
		}
		addRoots(g, 6, 0.55, 0.1, salt);
	} else if (sp == "cactus") {   // a saguaro: a ribbed column with arms that bend up
		const double H = 4.5 + 2.5 * hash01(salt, 1), r = 0.24 + 0.06 * hash01(salt, 2);
		const auto ribbed = [&](double radius, double len) {   // a column of 12 ribs, rounded at its top
			std::vector<std::pair<double, double>> prof;
			for (int k = 0; k <= 8; ++k) {
				const double u = k / 8.0;
				prof.push_back({radius * (u < 0.85 ? 1.0 : std::sqrt(std::max(0.0, 1.0 - std::pow((u - 0.85) / 0.15, 2.0)))), len * u});
			}
			Mesh m = lathe(prof, 24);
			for (std::size_t i = 0; i < m.verts.size(); ++i) {   // (the ribs: pleats round it)
				const double a = std::atan2(m.verts[i].z, m.verts[i].x), k = 1.0 + 0.09 * std::cos(12.0 * a);
				m.verts[i].x *= k, m.verts[i].z *= k;
			}
			return m;
		};
		addInto(g.foliage, ribbed(r, H), identity());
		const int arms = static_cast<int>(3.0 * hash01(salt, 3));
		for (int k = 0; k < arms; ++k) {
			const double a = 2.0 * kPi * (k + hash01(salt + k, 4)) / std::max(1, arms), h0 = H * (0.4 + 0.25 * hash01(salt + k, 5)), out = 0.6 + 0.3 * hash01(salt + k, 6);
			const Vec3 d{std::cos(a), 0.0, std::sin(a)};
			addInto(g.foliage, ribbed(0.75 * r, out), mul(translate(Vec3{0.0, h0, 0.0}), alignY(Vec3{0, 0, 0}, d)));   // (out to the side)
			addInto(g.foliage, ribbed(0.75 * r, H * (0.35 + 0.2 * hash01(salt + k, 7))), translate(d * out + Vec3{0.0, h0 - 0.05, 0.0}));   // (and up)
		}
	} else if (sp == "fern") {   // a fern: arching fronds from a crown at the ground, each a midrib with leaflets
		for (int f = 0; f < 14; ++f) {
			const double a = 2.0 * kPi * (f + 0.4 * hash01(salt + f, 1)) / 14.0, len = 0.8 + 0.4 * hash01(salt + f, 2), rise = 0.9 + 0.5 * hash01(salt + f, 3);
			const Vec3 out{std::cos(a), 0.0, std::sin(a)}, side = normalize(cross(Vec3{0, 1, 0}, out));
			const auto path = [&](double s) { return out * (len * s) + Vec3{0.0, len * (rise * s - 1.1 * s * s), 0.0}; };
			for (int k = 1; k <= 12; ++k) {
				const double s = k / 13.0;
				const Vec3 p = path(s), tangent = normalize(path(std::min(1.0, s + 0.02)) - path(std::max(0.0, s - 0.02)));
				for (int sg = -1; sg <= 1; sg += 2) {
					const Vec3 dir = normalize(side * static_cast<double>(sg) + tangent * 0.6);
					nature::addLeaf(g.foliage, p, dir, normalize(cross(dir, tangent) * static_cast<double>(sg) + Vec3{0, 0.6, 0}), 0.16 * (1.0 - 0.7 * s), 0.035);
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
	if (sp != "cactus") {
		placeLeaves(g.foliage, g.coreVerts, salt);
		shapeLeaves(g.foliage, g.coreVerts, salt);
	}
	g.wood.uvs.resize(g.wood.verts.size());   // (what was built by hand has none: all of a mesh has them, or none does and its bark is lost)
	return g;
}
// A mesh of leaves (nature::addLeaf's six points and four triangles each, after `coreVerts` points and `coreTris` triangles of something else) with only a share `keep` of them,
// each grown about its middle to cover what the others did: as much sky, with the same gaps.
inline Mesh sparserLeaves(const Mesh &m, double keep, int salt, std::size_t coreVerts = 0, std::size_t coreTris = 0) {
	Mesh out;
	const bool uvs = m.uvs.size() == m.verts.size();
	out.verts.assign(m.verts.begin(), m.verts.begin() + static_cast<std::ptrdiff_t>(coreVerts));
	out.normals.assign(m.normals.begin(), m.normals.begin() + static_cast<std::ptrdiff_t>(coreVerts));
	if (uvs) out.uvs.assign(m.uvs.begin(), m.uvs.begin() + static_cast<std::ptrdiff_t>(coreVerts));
	out.tris.assign(m.tris.begin(), m.tris.begin() + static_cast<std::ptrdiff_t>(coreTris));
	const double larger = std::sqrt(1.0 / keep);
	const std::size_t leaves = (m.verts.size() - coreVerts) / 6;
	for (std::size_t leaf = 0; leaf < leaves; ++leaf) {
		if (hash01(static_cast<int>(leaf), salt) >= keep) continue;
		const std::size_t from = coreVerts + leaf * 6;
		Vec3 mid{0.0, 0.0, 0.0};
		for (std::size_t i = from; i < from + 6; ++i) mid = mid + m.verts[i] * (1.0 / 6.0);
		const int base = static_cast<int>(out.verts.size());
		for (std::size_t i = from; i < from + 6; ++i) {
			out.verts.push_back(mid + (m.verts[i] - mid) * larger);
			out.normals.push_back(m.normals[i]);
			if (uvs) out.uvs.push_back(m.uvs[i]);
		}
		for (int t = 1; t <= 4; ++t) out.tris.push_back({base, base + t, base + t + 1});
	}
	return out;
}

// The far version: the near tree's own limbs with a fifth of its leaves, each one bigger, so a tree keeps its broken outline and its gaps out to the horizon. (A tree is one
// object however many stand: this costs a few thousand triangles a species, not a tree. Balls and cones were cheaper still, and what a wide shot was mostly made of.)
constexpr double kFarLeaves = 0.2;
inline Grown growFar(const std::string &sp, int v, bool winter, bool spring = false) {
	Grown g = grow(sp, v, winter, spring);
	if (sp == "cactus" || sp == "fern") return g;   // (small enough to draw as they are)
	const int salt = 9001 + 977 * v + static_cast<int>(sp.size()) * 31;
	g.foliage = sparserLeaves(g.foliage, kFarLeaves, salt + 1, g.coreVerts, g.coreTris);
	g.snow = sparserLeaves(g.snow, kFarLeaves, salt + 2);
	g.blossom = sparserLeaves(g.blossom, kFarLeaves, salt + 3);
	return g;
}

// The colours: bark, and the foliage for the season (variant v: each broadleaf its own autumn colour).
inline Vec3 barkOf(const std::string &sp) {
	if (sp == "acacia") return {0.24, 0.18, 0.13};
	if (sp == "kapok") return {0.42, 0.40, 0.36};   // (pale grey bark)
	if (sp == "beech") return {0.46, 0.45, 0.41};   // (smooth silver-grey)
	if (sp == "oak") return {0.22, 0.19, 0.15};     // (grey-brown and deeply cut)
	if (sp == "pine") return {0.40, 0.21, 0.12};    // (orange-brown plates)
	if (sp == "snag" || sp == "log") return {0.30, 0.27, 0.23};   // (dead wood, weathered grey)
	return sp == "birch" ? Vec3{0.72, 0.7, 0.66} : sp == "palm" ? Vec3{0.28, 0.23, 0.17} : sp == "conifer" ? Vec3{0.27, 0.17, 0.11} : Vec3{0.24, 0.19, 0.14};
}
// How high a species' bark stands at (u, v) of its picture, 0 down in a furrow .. 1 on a ridge. Every term goes round a whole number of times, so the picture tiles with no seam.
inline double barkHeight(const std::string &sp, double u, double v) {
	const double tau = 2.0 * kPi;
	const double wander = 0.45 * std::sin(tau * (2.0 * v + u)) + 0.2 * std::sin(tau * (5.0 * v - 2.0 * u));
	if (sp == "palm") return std::pow(std::abs(std::sin(kPi * (9.0 * v + 0.12 * std::sin(tau * u)))), 0.6);   // (the rings its old leaves left)
	if (sp == "birch" || sp == "kapok" || sp == "beech") return 0.8 + 0.2 * std::sin(tau * (3.0 * u + 0.4 * std::sin(tau * 6.0 * v)));   // (smooth)
	const double furrows = sp == "conifer" || sp == "pine" ? 5.0 : sp == "acacia" ? 9.0 : sp == "oak" ? 6.0 : 7.0;
	const double ridge = std::pow(std::abs(std::sin(kPi * (furrows * u + wander))), 0.55);
	const double breaks = 0.72 + 0.28 * std::sin(tau * ((sp == "conifer" || sp == "pine" ? 5.0 : 11.0) * v + 3.0 * u + 0.5 * std::sin(tau * 4.0 * u)));
	const double grain = 0.9 + 0.1 * std::sin(tau * (37.0 * u + 13.0 * v)) * std::sin(tau * (41.0 * v - 7.0 * u));
	return std::clamp(ridge * breaks * grain, 0.0, 1.0);
}
// The material of a species' wood: its picture of bark, dark in the furrows and paler on the ridges, and the same relief as a bump, so the low sun catches it. (A birch has its white
// bark with the dark dashes across it.)
inline std::string barkMaterial(const std::string &film, const std::string &sp, double wet = 0.0) {
	constexpr int W = 256;
	const Vec3 base = barkOf(sp);
	const std::string name = "bark_" + sp;
	const std::string colours = film_sky::picture(film, name, W, W, [&](std::vector<float> &rgb) {
		const double tau = 2.0 * kPi;
		for (int y = 0; y < W; ++y)
			for (int x = 0; x < W; ++x) {
				const double u = (x + 0.5) / W, v = (y + 0.5) / W, h = barkHeight(sp, u, v);
				Vec3 c = lerp(base * 0.32, base * 1.45, h);
				if (sp == "birch") {
					const double dash = smoothstep(0.93, 0.99, std::sin(tau * (13.0 * v + 0.3 * std::sin(tau * 3.0 * u)))) * smoothstep(-0.2, 0.3, std::sin(tau * (5.0 * u + 2.0 * v)));
					c = lerp(c, Vec3{0.05, 0.045, 0.04}, dash);
				}
				float *px = &rgb[(static_cast<std::size_t>(y) * W + x) * 3];
				px[0] = static_cast<float>(c.x), px[1] = static_cast<float>(c.y), px[2] = static_cast<float>(c.z);
			}
	});
	const std::string relief = film_sky::picture(film, name + "_h", W, W, [&](std::vector<float> &rgb) {
		for (int y = 0; y < W; ++y)
			for (int x = 0; x < W; ++x) {
				const float h = static_cast<float>(barkHeight(sp, (x + 0.5) / W, (y + 0.5) / W));
				float *px = &rgb[(static_cast<std::size_t>(y) * W + x) * 3];
				px[0] = px[1] = px[2] = h;
			}
	});
	char wetter[96];   // (rain darkens bark and puts a shine on it)
	std::snprintf(wetter, sizeof wetter, " \"float scale\" [ %.3f ]", lerp(1.0, 0.45, wet));
	char rough[40];
	std::snprintf(rough, sizeof rough, "%.3f", lerp(0.8, 0.22, wet));
	return "Texture \"" + name + "\" \"spectrum\" \"imagemap\"" + wetter + " \"string filename\" [ \"" + colours + "\" ]\n  Texture \"" + name + "_h\" \"float\" \"imagemap\" \"float scale\" [ 0.018 ] \"string filename\" [ \"" + relief +
		   "\" ]\n  Material \"coateddiffuse\" \"texture reflectance\" \"" + name + "\" \"float roughness\" [ " + rough + " ] \"texture displacement\" \"" + name + "_h\"";
}
inline bool deciduous(const std::string &sp) { return sp == "broadleaf" || sp == "birch" || sp == "shrub" || sp == "oak" || sp == "beech"; }
inline Vec3 foliageOf(const std::string &sp, const std::string &season, int v) {
	if (sp == "conifer") return Vec3{0.045, 0.095, 0.045} * (0.85 + 0.1 * v);
	if (sp == "pine") return Vec3{0.05, 0.11, 0.055} * (0.9 + 0.08 * v);
	if (sp == "oak" && season == "autumn") return Vec3{0.34, 0.2, 0.05} * (0.85 + 0.12 * v);      // (russet and late)
	if (sp == "beech" && season == "autumn") return Vec3{0.55, 0.26, 0.05} * (0.85 + 0.12 * v);   // (copper)
	if (sp == "oak" && season == "summer") return Vec3{0.065, 0.14, 0.04} * (0.85 + 0.1 * v);     // (a dark heavy green)
	if (sp == "beech" && season == "summer") return Vec3{0.1, 0.22, 0.05} * (0.85 + 0.1 * v);     // (fresher)
	if (sp == "palm") return {0.10, 0.20, 0.05};
	if (sp == "acacia") return Vec3{0.11, 0.15, 0.045} * (0.9 + 0.08 * v);   // (evergreen: no autumn for these)
	if (sp == "kapok") return Vec3{0.05, 0.13, 0.035} * (0.9 + 0.08 * v);
	if (sp == "cactus") return {0.12, 0.20, 0.08};
	if (sp == "fern") return {0.07, 0.20, 0.04};
	if (season == "autumn" && sp != "shrub") {
		static const Vec3 autumn[4] = {{0.55, 0.22, 0.04}, {0.45, 0.07, 0.03}, {0.60, 0.45, 0.07}, {0.32, 0.18, 0.06}};
		return sp == "birch" ? Vec3{0.62, 0.48, 0.08} : autumn[v % 4];
	}
	if (season == "spring") return sp == "birch" ? Vec3{0.24, 0.42, 0.08} : Vec3{0.18, 0.36, 0.07};
	return sp == "birch" ? Vec3{0.14, 0.27, 0.06} : sp == "shrub" ? Vec3{0.09, 0.18, 0.05} : Vec3{0.08, 0.17, 0.045} * (0.85 + 0.1 * v);
}

// The colour of the leaf in a cell of the picture of leaves: the tree's own colour for the season, no two leaves alike. Autumn does not take a crown all at once: the most exposed
// leaves have turned furthest, with the tree's neighbouring colours among them, and those deep inside are still green.
inline Vec3 cellColour(const std::string &sp, const std::string &season, int v, int row, int col) {
	const int cell = row * kLeafCells + col + 977 * v;
	const double exposed = (row + 0.5) / kLeafCells;
	Vec3 c = foliageOf(sp, season, v);
	if (season == "autumn" && deciduous(sp) && sp != "shrub") {
		const Vec3 other = foliageOf(sp, season, v + 1 + static_cast<int>(hash01(cell, 51) * 2.0)), green = foliageOf(sp, "summer", v);
		c = lerp(c, other, 0.45 * hash01(cell, 52));
		c = lerp(c, lerp(green, Vec3{0.32, 0.30, 0.05}, 0.5), (1.0 - smoothstep(0.1, 0.6, exposed)) * (0.5 + 0.5 * hash01(cell, 53)));
	} else {
		c = c * (0.9 + 0.3 * exposed);                                                        // (the young leaves out in the light are paler)
		c = lerp(c, Vec3{c.y * 0.9, c.y * 1.05, c.z * 0.6}, 0.25 * hash01(cell, 54));         // (some yellower)
	}
	return c * (0.78 + 0.44 * hash01(cell, 55));
}
inline std::string leafTextureName(const std::string &base, const char *which) { return base + "_leaves3_" + which; }
// A leaf is waxy: a cuticle over the green, which is why a sunlit crown glitters and a wet one gleams, and what a flat matte leaf never does. Most of a leaf is the light it scatters and
// lets through (`matte`, its transmittance a picture 1 / (1 - kWax) as bright for what the wax takes of it) and a share of it is wax over the same green. A share of a coat reflects a
// share of a coat's mirror, so the wax is a denser one than a real leaf's (eta 1.8, 8% straight on): the sum comes to the 3 or 4% of a real leaf. `wet`, 0 .. 1: water on the leaves.
constexpr double kWax = 0.4;
inline std::string waxyDefined(const std::string &name, const std::string &matte, const std::string &reflectance, double wet) {
	char b[80];
	std::snprintf(b, sizeof b, " \"float roughness\" [ %.3f ] \"float eta\" [ 1.8 ]", lerp(0.32, 0.1, wet));
	return "MakeNamedMaterial \"" + name + "_matte\" \"string type\" \"diffusetransmission\" " + matte + "\n  MakeNamedMaterial \"" + name + "_wax\" \"string type\" \"coateddiffuse\" " + reflectance + b;
}
inline std::string waxyUsed(const std::string &name, double wet) {
	char b[60];
	std::snprintf(b, sizeof b, " \"float amount\" [ %.3f ]", lerp(kWax, 0.65, wet));
	return "Material \"mix\" \"string materials\" [ \"" + name + "_matte\" \"" + name + "_wax\" ]" + b;
}
// How far a leaf reaches from its midrib, as a share of its cell (the leaf's own point lengthwise, 0 stem .. 1 tip): rounded, broadest below the middle, drawn out to a point. It takes
// the corners off the six-sided shape it is cut from, and no more than that.
inline double leafHalfWidth(double along) {
	if (along <= 0.0 || along >= 1.0) return 0.0;
	return 0.43 * std::pow(std::sin(kPi * std::pow(along, 0.68)), 1.25);
}
// What is left of the leaves by the picture's cells: a pointed outline with a toothed edge, and here and there what ate it, a bite out of the edge or a hole.
inline std::string leafCutPicture(const std::string &film, const std::string &sp) {
	constexpr int kCell = 48, W = kCell * kLeafCells;
	return film_sky::picture(film, "leafcut2_" + sp, W, W, [&](std::vector<float> &rgb) {
		for (int y = 0; y < W; ++y)
			for (int x = 0; x < W; ++x) {
				const int col = x / kCell, row = (W - 1 - y) / kCell, cell = row * kLeafCells + col;
				const double u = ((x % kCell + 0.5) / kCell - 0.04) / 0.92 - 0.5, v = (((W - 1 - y) % kCell + 0.5) / kCell - 0.04) / 0.92;
				const double teeth = 1.0 - 0.13 * std::abs(std::sin(kPi * v * 9.0)) - 0.1 * (0.5 + 0.5 * std::sin(kPi * v * 3.0 + 6.0 * hash01(cell, 80)));   // (toothed, and wavy)
				double keep = smoothstep(-0.018, 0.018, leafHalfWidth(v) * teeth - std::abs(u));
				if (v < 0.07 && std::abs(u) < 0.022) keep = 1.0;   // (the stalk)
				const int bites = hash01(cell, 81) < 0.5 ? 1 + static_cast<int>(hash01(cell, 82) * 2.0) : 0, holes = hash01(cell, 83) < 0.3 ? 1 + static_cast<int>(hash01(cell, 84) * 3.0) : 0;
				for (int k = 0; k < bites; ++k) {   // (at the edge)
					const double at = 0.25 + 0.6 * hash01(cell * 7 + k, 85), sign = hash01(cell * 7 + k, 86) < 0.5 ? -1.0 : 1.0, r = 0.08 + 0.13 * hash01(cell * 7 + k, 87);
					keep = std::min(keep, smoothstep(r - 0.015, r + 0.015, std::hypot(u - sign * leafHalfWidth(at), v - at)));
				}
				for (int k = 0; k < holes; ++k) {   // (within)
					const double at = 0.25 + 0.5 * hash01(cell * 11 + k, 88), off = 0.5 * leafHalfWidth(at) * (2.0 * hash01(cell * 11 + k, 89) - 1.0), r = 0.025 + 0.05 * hash01(cell * 11 + k, 90);
					keep = std::min(keep, smoothstep(r - 0.012, r + 0.012, std::hypot(u - off, v - at)));
				}
				float *px = &rgb[(static_cast<std::size_t>(y) * W + x) * 3];
				px[0] = px[1] = px[2] = static_cast<float>(keep);
			}
	});
}
inline std::string waxyMaterial(const std::string &name, const std::string &matte, const std::string &reflectance, double wet) {
	return waxyDefined(name, matte, reflectance, wet) + "\n  " + waxyUsed(name, wet);
}
// The pictures of leaves of a tree in `season`, or `u` of the way to `to` (each leaf turning in its own time, the exposed first): what a leaf reflects and what it lets through,
// and the lines that make them a material. A leaf's veins are dark against the light; what it reflects and what it lets through are never more than comes in.
// (`defined`: what only needs saying once, as many objects are to use it: the material is then waxyUsed(base, wet).)
inline std::string leafDefined(const std::string &film, const std::string &base, const std::string &sp, int v, const std::string &season, const std::string &to, double u, double wet) {
	constexpr int kCell = 48, W = kCell * kLeafCells;
	const bool needles = sp == "conifer" || sp == "pine";
	const double hurt = (season == "spring" ? 0.35 : season == "autumn" ? 1.5 : 1.0) * (needles ? 0.0 : 1.0);   // (a leaf gathers its marks over the summer)
	const auto colourOf = [&](int row, int col) {
		const Vec3 a = cellColour(sp, season, v, row, col);
		if (to.empty()) return a;
		const double first = 0.7 * (1.0 - (row + 0.5) / kLeafCells) + 0.3 * hash01(row * kLeafCells + col + 977 * v, 56);   // (when it turns: 0 first .. 1 last)
		return lerp(a, cellColour(sp, to, v, row, col), smoothstep(first * 0.7, first * 0.7 + 0.3, u));
	};
	const auto paint = [&](std::vector<float> &rgb, bool through) {
		for (int y = 0; y < W; ++y)
			for (int x = 0; x < W; ++x) {
				const int col = x / kCell, row = (W - 1 - y) / kCell;
				const double lu = (x % kCell + 0.5) / kCell, lv = ((W - 1 - y) % kCell + 0.5) / kCell, off = std::abs(lu - 0.5);
				Vec3 c = colourOf(row, col);
				double veins = 1.0;
				if (!needles) {
					const double rib = std::exp(-off * off / 0.0012), side = std::pow(std::abs(std::sin(kPi * (lv * 7.0 - off * 5.0))), 18.0);
					veins = 1.0 - (through ? 0.55 : 0.28) * rib - (through ? 0.25 : 0.1) * side * (1.0 - rib);
				}
				const double blotch = nature::noise2(x * 0.11 + col * 7.0, y * 0.11 + row * 3.0, 57);
				c = c * (veins * (0.9 + 0.2 * blotch));
				// what a season does to a leaf: a brown tip, a scorched edge, the spots of a fungus each in its yellow ring
				const int cell = row * kLeafCells + col + 977 * v;
				const double lv2 = (lv - 0.04) / 0.92, edge = std::abs((lu - 0.04) / 0.92 - 0.5) / std::max(0.02, leafHalfWidth(lv2)), rough = 0.2 * (blotch - 0.5);
				const Vec3 dead = Vec3{0.2, 0.13, 0.06} * (0.8 + 0.5 * blotch);
				if (hash01(cell, 58) < 0.3 * hurt) c = lerp(c, dead, smoothstep(0.74, 0.98, lv2 + rough));
				if (hash01(cell, 59) < 0.26 * hurt) c = lerp(c, dead, smoothstep(0.7, 1.0, edge + rough));
				if (hash01(cell, 53) < 0.24 * hurt)
					for (int k = 0; k < 4; ++k) {
						const double sv = 0.2 + 0.6 * hash01(cell * 5 + k, 52), su = 0.5 + 0.7 * leafHalfWidth(sv) * (2.0 * hash01(cell * 5 + k, 51) - 1.0), sr = 0.02 + 0.03 * hash01(cell * 5 + k, 50);
						const double d = std::hypot((lu - 0.04) / 0.92 - su, lv2 - sv);
						c = lerp(c, Vec3{c.x * 1.2 + 0.05, c.y * 1.1 + 0.03, c.z * 0.5}, 0.7 * (1.0 - smoothstep(sr, 2.1 * sr, d)));
						c = lerp(c, dead * 0.7, 1.0 - smoothstep(0.5 * sr, sr, d));
					}
				if (through) c = Vec3{c.x * 1.05 + 0.02, c.y * 1.2 + 0.03, c.z * 0.8} * (1.0 / (1.0 - kWax));   // (the light through a leaf is yellower)
				const Vec3 back = colourOf(row, col) * 1.1;   // (the most this leaf reflects)
				const Vec3 most = through ? Vec3{std::max(0.0, 0.92 - back.x), std::max(0.0, 0.92 - back.y), std::max(0.0, 0.92 - back.z)} : Vec3{0.92, 0.92, 0.92};
				float *px = &rgb[(static_cast<std::size_t>(y) * W + x) * 3];
				px[0] = static_cast<float>(std::min(c.x, most.x));
				px[1] = static_cast<float>(std::min(c.y, most.y));
				px[2] = static_cast<float>(std::min(c.z, most.z));
			}
	};
	const std::string nameR = leafTextureName(base, "r"), nameT = leafTextureName(base, "t");
	const std::string reflects = film_sky::picture(film, nameR, W, W, [&](std::vector<float> &rgb) { paint(rgb, false); });
	const std::string passes = film_sky::picture(film, nameT, W, W, [&](std::vector<float> &rgb) { paint(rgb, true); });
	return "Texture \"" + nameR + "\" \"spectrum\" \"imagemap\" \"string wrap\" \"clamp\" \"string filename\" [ \"" + reflects + "\" ]\n  Texture \"" + nameT + "\" \"spectrum\" \"imagemap\" \"string wrap\" \"clamp\" \"string filename\" [ \"" + passes + "\" ]\n  " +
		   waxyDefined(base, "\"texture reflectance\" \"" + nameR + "\" \"texture transmittance\" \"" + nameT + "\"", "\"texture reflectance\" \"" + nameR + "\"", wet);
}

inline std::string leafMaterial(const std::string &film, const std::string &base, const std::string &sp, int v, const std::string &season, const std::string &to, double u, double wet) {
	return leafDefined(film, base, sp, v, season, to, u, wet) + "\n  " + waxyUsed(base, wet);
}
// The material of a close tree's tufts, one for all of them, and the shape of its leaves.
inline std::string tuftsDefined(const std::string &film, const std::string &base, const std::string &sp, const std::string &leaf, double wet) {
	const std::string used = waxyUsed(base, wet), rest = used.substr(used.find("\"string materials\""));
	return leaf + "\n  MakeNamedMaterial \"" + base + "_leaf\" \"string type\" \"mix\" " + rest + "\n  Texture \"" + base + "_cut\" \"float\" \"imagemap\" \"string filename\" [ \"" +
		   leafCutPicture(film, sp) + "\" ]\n";
}

// ---- a tree close to the camera: its leaves in tufts ----
// A tree placed whole sways as one piece, and that is how a toy moves. So the leaves of one close to the camera are cut into tufts, each its own object about its own middle: what bears
// them is slow, a limb answers the trunk late, and a tuft turns quickly this way and that. A few dozen placements, nothing written again from frame to frame; the wood keeps still under
// them by a few centimetres, which nobody sees. Further off the tree is one object as ever.
constexpr double kTuftsWithin = 38.0;   // (metres)
constexpr int kMostTufts = 48;
inline Vec3 leafMiddle(const Mesh &m, std::size_t first) {
	Vec3 at{0.0, 0.0, 0.0};
	for (std::size_t i = first; i < first + 6; ++i) at = at + m.verts[i] * (1.0 / 6.0);
	return at;
}
// The tufts' middles of a species' variant (none: it has no leaves to cut up), the same for every season and stage of its leaves: a few rounds of Lloyd's from leaves chosen by chance.
inline const std::vector<Vec3> &tuftsOf(const std::string &sp, int v) {
	static std::mutex lock;
	static std::map<std::string, std::vector<Vec3>> made;
	const std::lock_guard<std::mutex> hold(lock);
	const auto [it, fresh] = made.try_emplace(sp + "_" + std::to_string(v));
	if (!fresh || sp == "cactus") return it->second;
	const Grown g = grow(sp, v, false, true);
	const std::size_t leaves = g.foliage.verts.size() > g.coreVerts ? (g.foliage.verts.size() - g.coreVerts) / 6 : 0;
	if (leaves < 12) return it->second;
	const int salt = 8101 + 977 * v;
	std::vector<Vec3> at;
	const std::size_t want = std::clamp<std::size_t>(leaves / 40, 6, kMostTufts);
	for (std::size_t k = 0; k < want; ++k) at.push_back(leafMiddle(g.foliage, g.coreVerts + 6 * (static_cast<std::size_t>(hash01(static_cast<int>(k), salt) * leaves) % leaves)));
	const auto nearestOf = [&](const Vec3 &p) {
		std::size_t best = 0;
		double d = 1e30;
		for (std::size_t k = 0; k < at.size(); ++k)
			if (const double e = dot(p - at[k], p - at[k]); e < d) d = e, best = k;
		return best;
	};
	for (int round = 0; round < 3; ++round) {
		std::vector<Vec3> sum(at.size(), Vec3{0.0, 0.0, 0.0});
		std::vector<int> n(at.size(), 0);
		for (std::size_t leaf = 0; leaf < leaves; leaf += 3) {   // (every third is plenty)
			const Vec3 p = leafMiddle(g.foliage, g.coreVerts + 6 * leaf);
			const std::size_t k = nearestOf(p);
			sum[k] = sum[k] + p, ++n[k];
		}
		for (std::size_t k = 0; k < at.size(); ++k)
			if (n[k] > 0) at[k] = sum[k] * (1.0 / n[k]);
	}
	return it->second = at;
}
// A mesh of leaves cut into the tufts `at`, with what is not leaves (coreVerts, coreTris: a conifer's dark core) in one more mesh after them. Up close a leaf the size of a far tree's
// shows as what it is, a big flat shape, so each here is three, smaller, spread as a spray of them with their own cells of the picture: as much leaf, nothing further off.
inline std::vector<Mesh> cutIntoTufts(const Mesh &m, std::size_t coreVerts, std::size_t coreTris, const std::vector<Vec3> &at) {
	std::vector<Mesh> out(at.size() + 1);
	constexpr double kSmaller = 0.68;
	Mesh &core = out.back();
	const bool uvs = m.uvs.size() == m.verts.size();
	core.verts.assign(m.verts.begin(), m.verts.begin() + static_cast<std::ptrdiff_t>(coreVerts));
	core.normals.assign(m.normals.begin(), m.normals.begin() + static_cast<std::ptrdiff_t>(coreVerts));
	if (uvs) core.uvs.assign(m.uvs.begin(), m.uvs.begin() + static_cast<std::ptrdiff_t>(coreVerts));
	std::set<std::size_t> placed;
	for (std::size_t t = 0; t < m.tris.size(); ++t) {
		if (t < coreTris) {
			core.tris.push_back(m.tris[t]);
			continue;
		}
		const std::size_t leaf = (static_cast<std::size_t>(m.tris[t][0]) - coreVerts) / 6, first = coreVerts + leaf * 6;
		if (!placed.insert(leaf).second) continue;
		const Vec3 p = leafMiddle(m, first);
		std::size_t best = 0;
		double d = 1e30;
		for (std::size_t k = 0; k < at.size(); ++k)
			if (const double e = dot(p - at[k], p - at[k]); e < d) d = e, best = k;
		Mesh &tuft = out[best];
		const int s = static_cast<int>(leaf);
		const Vec3 up = normalize(m.normals[first]), along = m.verts[first + 3] - m.verts[first];
		for (int j = 0; j < 3; ++j) {
			const int base = static_cast<int>(tuft.verts.size());
			const Mat4 spin = rotate(up, (j == 0 ? 0.0 : j == 1 ? 42.0 : -42.0) + 24.0 * (hash01(s * 3 + j, 94) - 0.5));
			const Vec3 mid = p + (j == 0 ? along * 0.14 : cross(up, along) * (j == 1 ? 0.3 : -0.3) - along * 0.12);
			for (std::size_t i = first; i < first + 6; ++i) {
				tuft.verts.push_back(mid + transformPoint(spin, (m.verts[i] - p) * (kSmaller * (0.88 + 0.24 * hash01(s * 3 + j, 95)))));
				tuft.normals.push_back(transformPoint(spin, m.normals[i]));
				if (uvs) {
					const int c = static_cast<int>(std::floor(m.uvs[i].first * kLeafCells));
					tuft.uvs.push_back({m.uvs[i].first + ((c + 3 * j) % kLeafCells - c) / static_cast<double>(kLeafCells), m.uvs[i].second});
				}
			}
			for (int f = 1; f <= 4; ++f) tuft.tris.push_back({base, base + f, base + f + 1});
		}
	}
	return out;
}
// A tuft's own move, in the tree's own space, at t: a turn about its middle, faster and further the harder it blows (`wind`: how far the trunk leans).
inline Mat4 tuftTurn(const Vec3 &at, int salt, double t, double wind) {
	const double angle = std::min(11.0, 2.2 + 170.0 * wind), hz = 1.1 + 1.5 * hash01(salt, 1), phase = 2.0 * kPi * hash01(salt, 2);
	const double a = angle * (std::sin(2.0 * kPi * hz * t + phase) + 0.5 * std::sin(2.0 * kPi * 1.73 * hz * t + 2.0 * phase));
	const Vec3 axis = normalize(Vec3{hash01(salt, 3) - 0.5, 0.35 * (hash01(salt, 4) - 0.5), hash01(salt, 5) - 0.5} + Vec3{1e-6, 0.0, 0.0});
	return mul(translate(at), rotate(axis, a), translate(at * -1.0));
}

// ---- between seasons ----
constexpr int kStages = 8;
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
}  // namespace detail

// The objects (near and far) of a species' variant in a season, as scene text: ObjectBegin ... ObjectEnd for each, its meshes PLY files in the film's cache.
inline std::string objectName(const std::string &sp, int v, const std::string &season, bool full) {
	return "forest_" + sp + "_" + std::to_string(v) + "_" + season + (full ? "_near" : "_far");
}
// The moss of a near tree (film_moss.h): the object of its skin with the damp on side `side`, or of a log's, on what faces up. None on a species of dry country.
inline bool mossy(const std::string &sp) { return sp == "broadleaf" || sp == "birch" || sp == "conifer" || sp == "kapok" || sp == "snag" || sp == "log" || sp == "oak" || sp == "beech"; }
inline std::string mossName(const std::string &sp, int v, int side) { return "forest_moss_" + sp + "_" + std::to_string(v) + "_" + std::to_string(side); }
inline std::string mossText(const std::string &film, const std::string &sp, int v, int side) {
	const std::string name = mossName(sp, v, side);
	const std::string file = film_cache::ply(film, "forest_moss_" + sp + "_" + std::to_string(v), [&] {
		const detail::Grown g = detail::grow(sp, v, true, false);
		return sp == "log" ? moss::topSkin(g.wood, 3.0, 0.008) : moss::trunkSkin(g.wood);
	});
	return nature::objectBegin(name) + moss::shape(film, name, file, side) + nature::objectEnd();
}
// The objects of a close tree's tufts, named `base`_t0.. (`foliage`: its leaves), each with what `used` says; `present` gets the numbers of those that hold anything (the last of the
// tuftsOf(sp, v).size() + 1 is what does not move). What is on the land is then the tree's own object, with the wood, and these.
inline std::string tuftObjects(const std::string &film, const std::string &base, const std::vector<Vec3> &at, const std::function<std::vector<Mesh>()> &cut, std::vector<int> &present, double wet = 0.0) {
	std::shared_ptr<std::vector<Mesh>> meshes;
	std::string out;
	for (std::size_t k = 0; k <= at.size(); ++k) {
		const std::string name = base + "_t" + std::to_string(k);
		const std::string file = film_cache::ply(film, name, [&] {
			if (!meshes) meshes = std::make_shared<std::vector<Mesh>>(cut());
			return (*meshes)[k];
		});
		if (file.empty()) continue;
		present.push_back(static_cast<int>(k));
		std::string shape = film_cache::plyShape(file);
		shape.pop_back();
		if (k < at.size()) shape += " \"texture alpha\" \"" + base + "_cut\"";   // (not a conifer's core)
		std::string beads;
		if (wet > 0.15 && k < at.size()) {   // (rain stands on the leaves in beads: small round drops, a mesh of them, as a tree may lean and stretch)
			const std::string file = film_cache::ply(film, name + "_beads", [&] {
				Mesh m;
				const Mesh ball = uvSphere(1.0, 6, 4);
				const int s = static_cast<int>(k) * 131 + 17;
				for (int d = 0; d < 26; ++d) {
					const Vec3 p = at[k] + Vec3{1.2 * (hash01(s + d, 96) - 0.5), 0.9 * (hash01(s + d, 97) - 0.5), 1.2 * (hash01(s + d, 98) - 0.5)};
					detail::addInto(m, ball, mul(translate(p), scale(0.0028 + 0.0035 * hash01(s + d, 99))));
				}
				return m;
			});
			beads = "  AttributeBegin\n  Material \"dielectric\" \"float eta\" [ 1.33 ]\n" + film_cache::plyShape(file) + "  AttributeEnd\n";
		}
		out += nature::objectBegin(name) + "  AttributeBegin\n  NamedMaterial \"" + base + "_leaf\"\n" + shape + "\n  AttributeEnd\n" + beads + nature::objectEnd();
	}
	return out;
}
inline std::string objectText(const std::string &film, const std::string &sp, int v, const std::string &season, bool full, double wet = 0.0, std::vector<int> *tufts = nullptr) {
	const bool winter = season == "winter", spring = season == "spring";
	std::shared_ptr<detail::Grown> g;
	const auto get = [&]() -> const detail::Grown & {
		if (!g) g = std::make_shared<detail::Grown>(full ? detail::grow(sp, v, winter, spring) : detail::growFar(sp, v, winter, spring));
		return *g;
	};
	const std::string base = objectName(sp, v, season, full) + (tufts ? "_close" : "");
	const std::string plain = objectName(sp, v, season, full);
	const std::string wood = film_cache::ply(film, plain + "_wood", [&] { return get().wood; });
	const std::string leaves = tufts ? std::string() : film_cache::ply(film, plain + "_foliage", [&] { return get().foliage; });
	const std::string snow = film_cache::ply(film, plain + "_snow", [&] { return get().snow; });
	const std::string blossom = film_cache::ply(film, plain + "_blossom", [&] { return get().blossom; });
	const Vec3 leaf = detail::foliageOf(sp, season, v);
	std::string before;
	if (tufts)
		before = "  " + detail::tuftsDefined(film, base, sp, detail::leafDefined(film, base, sp, v, season, std::string(), 0.0, wet), wet) +
				 tuftObjects(film, base, detail::tuftsOf(sp, v), [&] { return detail::cutIntoTufts(get().foliage, get().coreVerts, get().coreTris, detail::tuftsOf(sp, v)); }, *tufts, wet);
	std::string out = before + nature::objectBegin(base) + "  AttributeBegin\n  " + detail::barkMaterial(film, sp, wet) + "\n" + film_cache::plyShape(wood) + "  AttributeEnd\n";
	if (!leaves.empty())   // (leaves let a little light through)
		out += "  AttributeBegin\n  " + (sp != "cactus" ? detail::leafMaterial(film, base, sp, v, season, std::string(), 0.0, wet) : diffuseMaterial(leaf * 0.9)) + "\n" + film_cache::plyShape(leaves) + "  AttributeEnd\n";
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
inline std::string stageText(const std::string &film, const std::string &sp, int v, const std::string &a, const std::string &b, int k, bool full, double wet, std::vector<int> *tufts = nullptr) {
	const double u = static_cast<double>(k) / detail::kStages;
	const double leaves = lerp(detail::leavesIn(sp, a), detail::leavesIn(sp, b), u), blossom = lerp(a == "spring" ? 1.0 : 0.0, b == "spring" ? 1.0 : 0.0, u),
				 snow = lerp(a == "winter" ? 1.0 : 0.0, b == "winter" ? 1.0 : 0.0, u);
	const bool bare = a == "winter" || b == "winter";
	const std::string plain = stageName(sp, v, a, b, k, full), base = plain + (tufts ? "_close" : "");
	const int salt = 401 + 977 * v;
	std::shared_ptr<detail::Grown> leafy, winter;
	const auto getLeafy = [&]() -> const detail::Grown & {
		if (!leafy) leafy = std::make_shared<detail::Grown>(full ? detail::grow(sp, v, false, true) : detail::growFar(sp, v, false, true));
		return *leafy;
	};
	const auto getWinter = [&]() -> const detail::Grown & {
		if (!winter) winter = std::make_shared<detail::Grown>(full ? detail::grow(sp, v, true, false) : detail::growFar(sp, v, true, false));
		return *winter;
	};
	const std::string wood = film_cache::ply(film, plain + "_wood", [&] { return bare ? getWinter().wood : getLeafy().wood; });
	const auto theLeaves = [&] { return leaves >= 1.0 ? getLeafy().foliage : detail::shareOf(getLeafy().foliage, leaves, 4, salt); };
	const std::string foliage = tufts ? std::string() : film_cache::ply(film, plain + "_foliage", theLeaves);
	const std::string blooms = film_cache::ply(film, plain + "_blossom", [&] { return detail::shareOf(getLeafy().blossom, blossom, 4, salt + 1); });
	const std::string snowy = film_cache::ply(film, plain + "_snow", [&] { return detail::shareOf(getWinter().snow, snow, 4, salt + 2); });
	const auto seasonIn = [&](const std::string &season, const std::string &other) { return detail::leavesIn(sp, season) > 0.0 ? season : other; };   // (leaves keep their colour as they fall)
	const Vec3 leaf = lerp(detail::foliageOf(sp, seasonIn(a, b), v), detail::foliageOf(sp, seasonIn(b, a), v), u);
	std::string before;
	if (tufts && leaves > 0.0)
		before = "  " + detail::tuftsDefined(film, base, sp, detail::leafDefined(film, base, sp, v, seasonIn(a, b), seasonIn(b, a), u, wet), wet) +
				 tuftObjects(film, base, detail::tuftsOf(sp, v), [&] {
					 const detail::Grown &g = getLeafy();
					 return detail::cutIntoTufts(leaves >= 1.0 ? g.foliage : detail::shareOf(g.foliage, leaves, 4, salt), g.coreVerts, g.coreTris, detail::tuftsOf(sp, v));
				 }, *tufts, wet);
	std::string out = before + nature::objectBegin(base) + "  AttributeBegin\n  " + detail::barkMaterial(film, sp, wet) + "\n" + film_cache::plyShape(wood) + "  AttributeEnd\n";
	if (!foliage.empty() && leaves > 0.0)
		out += "  AttributeBegin\n  " + (sp != "cactus" ? detail::leafMaterial(film, base, sp, v, seasonIn(a, b), seasonIn(b, a), u, wet) : diffuseMaterial(leaf * 0.9)) + "\n" + film_cache::plyShape(foliage) + "  AttributeEnd\n";
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
		// A wood is not scattered evenly: trees stand in groves with glades between them, and a seedling comes up within a few metres of the tree that dropped it.
		const bool seedling = !out.empty() && hash01(s, 12) < 0.28;
		double x = x0 + (x1 - x0) * hash01(s, 1), z = z0 + (z1 - z0) * hash01(s, 2);
		if (seedling) {
			const Tree &parent = out[static_cast<std::size_t>(hash01(s, 13) * out.size()) % out.size()];
			const double a = 2.0 * kPi * hash01(s, 14), r = 2.5 + 6.0 * hash01(s, 15);
			x = parent.at.x + r * std::cos(a), z = parent.at.z + r * std::sin(a);
		} else if (hash01(s, 9) > 0.04 + smoothstep(0.34, 0.56, nature::fbm2(x / 55.0, z / 55.0, f.seed + 17))) {
			continue;
		}
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
			if (up < 0.35) t.species = r < 0.24 ? "oak" : r < 0.44 ? "beech" : r < 0.58 ? "broadleaf" : r < 0.78 ? "birch" : r < 0.9 ? "shrub" : r < 0.96 ? "pine" : "conifer";
			else if (up < 0.7) t.species = r < 0.14 ? "beech" : r < 0.24 ? "oak" : r < 0.34 ? "broadleaf" : r < 0.46 ? "birch" : r < 0.55 ? "shrub" : r < 0.75 ? "pine" : "conifer";
			else t.species = r < 0.68 ? "conifer" : r < 0.9 ? "pine" : "shrub";
		} else if (f.kind == "savanna") {   // the mixes of the biomes: acacias over the grass with a few bushes; the jungle's giants over palms, broadleaf trees and ferns;
			const double r = hash01(s, 5);    // the desert's cacti among scrub; the tundra's low scrub and a few stunted conifers
			t.species = r < 0.7 ? "acacia" : "shrub";
		} else if (f.kind == "jungle") {
			const double r = hash01(s, 5);
			t.species = r < 0.18 ? "kapok" : r < 0.42 ? "palm" : r < 0.62 ? "broadleaf" : "fern";
		} else if (f.kind == "desert") {
			t.species = hash01(s, 5) < 0.6 ? "cactus" : "shrub";
		} else if (f.kind == "tundra") {
			t.species = hash01(s, 5) < 0.8 ? "shrub" : "conifer";
		}
		t.variant = static_cast<int>(hash01(s, 6) * kVariants) % kVariants;
		t.at = {x, y - 0.05, z};
		t.scale = f.scale0 + (f.scale1 - f.scale0) * hash01(s, 7);
		t.scale *= seedling ? 0.22 + 0.3 * hash01(s, 16) : 0.68 + 0.62 * std::pow(hash01(s, 10), 1.5);   // (saplings, many in their prime and the odd old giant)
		if (!seedling && (t.species == "broadleaf" || t.species == "birch" || t.species == "conifer" || t.species == "kapok" || t.species == "oak" || t.species == "beech" || t.species == "pine")) {   // (and what has died: still standing, or fallen)
			const double dead = hash01(s, 11);
			if (dead < 0.1) t.species = dead < 0.05 ? "snag" : "log";
		}
		if (t.species == "conifer" && ground) t.scale *= 1.0 - 0.35 * std::min(1.0, frac / std::max(0.05, treeline / height));   // (smaller towards the tree line)
		if (f.kind == "tundra") t.scale *= 0.45;   // (stunted)
		t.turn = 360.0 * hash01(s, 8);
		out.push_back(t);
	}
	return out;
}

// A forest's trees at t (the wind's time; `filmT` the film's, for a change of season and growing), seen from `eye` (story space; near ones in full), as scene text: the
// objects it uses, then a placement for each tree, leaning in the wind. `wet`, 0 .. 1: rain on the leaves. `written`: what a frame's forests have said so far, as they share objects.
struct Written {
	std::set<std::string> made;
	std::map<std::string, std::vector<int>> tuftsThere;   // (a close object's: tuftObjects)
};
inline std::string text(const std::string &film, const std::vector<Tree> &trees, const Forest &f, double t, double filmT, const Vec3 &eyeStory, const dressing::Gusts &gusts, double wet, Written &written) {
	std::string objects, places;
	const auto need = [&](const std::string &name, const std::function<std::string()> &make) {
		if (written.made.insert(name).second) objects += make();
		return name;
	};
	std::map<std::string, std::vector<int>> &tuftsThere = written.tuftsThere;
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
		// turning: each change over its own 40% of its time; the last change it has begun says between which two seasons it is
		int stage = 0;
		std::string from = f.season, to = f.seasonTo;
		if (!f.seasonTo.empty()) {
			const double start = f.turn0 + 0.6 * turnSpan * hash01(own, 32);
			stage = static_cast<int>(std::lround(smoothstep(start, start + 0.4 * turnSpan, filmT) * detail::kStages));
			std::string before = f.seasonTo;
			for (std::size_t c = 0; c < f.then.size(); ++c) {
				const Forest::Change &ch = f.then[c];
				const double span = std::max(1e-6, ch.t1 - ch.t0), s0 = ch.t0 + 0.6 * span * hash01(own, 33 + static_cast<int>(c));
				const int k = static_cast<int>(std::lround(smoothstep(s0, s0 + 0.4 * span, filmT) * detail::kStages));
				if (k > 0) from = before, to = ch.to, stage = k;
				before = ch.to;
			}
		}
		const std::string &sp = tr.species;
		const int v = tr.variant;
		const bool stiff = sp == "log" || sp == "snag";   // (dead wood does not sway)
		const bool bare = stage == 0 ? detail::leavesIn(sp, from) <= 0.0 : stage == detail::kStages && detail::leavesIn(sp, to) <= 0.0;
		const bool close = full && !stiff && !bare && length(foot - eyeStory) < detail::kTuftsWithin + 6.0 * size && !detail::tuftsOf(sp, v).empty();
		const auto object = [&](std::vector<int> *tufts) {
			const std::string tag = tufts ? "_close" : "";
			return stage == 0 ? need(objectName(sp, v, from, full) + tag, [&] { return objectText(film, sp, v, from, full, wet, tufts); })
				   : stage == detail::kStages ? need(objectName(sp, v, to, full) + tag, [&] { return objectText(film, sp, v, to, full, wet, tufts); })
											  : need(stageName(sp, v, from, to, stage, full) + tag, [&] { return stageText(film, sp, v, from, to, stage, full, wet, tufts); });
		};
		const auto windAt = [&](double when) {
			return nature::windLean(foot.x, foot.z, when, sp == "conifer" ? 0.008 : 0.014) + dressing::gustAt(gusts, foot.x, foot.z, when) * (sp == "conifer" ? 0.04 : 0.07);
		};
		const Vec3 w = stiff ? Vec3{0.0, 0.0, 0.0} : windAt(t);
		const Mat4 whole = mul(translate(foot), nature::shearXZ(w.x, w.z), rotate({0, 1, 0}, tr.turn), scale(size));
		const auto mossOn = [&] {   // (near trees: the same side of the sky on all of them)
			if (!full || !mossy(sp)) return;
			const int side = sp == "log" ? moss::kTop : moss::sideFor(rotate({0, 1, 0}, tr.turn), {0.0, 0.0, -1.0});
			places += nature::placeObject(need(mossName(sp, v, side), [&] { return mossText(film, sp, v, side); }), whole);
		};
		if (!close) {
			places += nature::placeObject(object(nullptr), whole);
			mossOn();
			continue;
		}
		std::vector<int> got;
		const std::string name = object(&got);
		if (!got.empty()) tuftsThere[name] = got;
		places += nature::placeObject(name, whole);
		mossOn();
		const std::vector<Vec3> &at = detail::tuftsOf(sp, v);
		if (wet > 0.15)   // (what has come through the crown falls on from leaf to leaf: a drop from somewhere every moment)
			for (int d = 0; d < 7; ++d) {
				const int salt = own * 37 + d;
				const double rate = 0.9 + 0.8 * hash01(salt, 64), cycle = t * rate + hash01(salt, 65), from = 2.5 + 3.5 * hash01(salt, 66), age = (cycle - std::floor(cycle)) * std::min(1.0 / rate, std::sqrt(from / 4.9));
				const Vec3 at = foot + Vec3{3.0 * (hash01(salt + static_cast<int>(std::floor(cycle)), 67) - 0.5), from * size - 4.9 * age * age, 3.0 * (hash01(salt + static_cast<int>(std::floor(cycle)), 68) - 0.5)};
				char b[140];
				std::snprintf(b, sizeof b, "AttributeBegin\n  Material \"dielectric\" \"float eta\" [ 1.33 ]\n  Translate %.4f %.4f %.4f\n  Shape \"sphere\" \"float radius\" [ 0.0028 ]\nAttributeEnd\n", -at.x, at.y, at.z);
				places += b;
			}
		for (const int k : tuftsThere[name]) {
			const std::string tuft = name + "_t" + std::to_string(k);
			if (static_cast<std::size_t>(k) >= at.size()) {   // (what does not move with the tufts: it stands with the tree)
				places += nature::placeObject(tuft, whole);
				continue;
			}
			const Vec3 &p = at[static_cast<std::size_t>(k)];
			const int salt = own * 131 + k;
			// a limb answers the trunk late, the further up and out the later, and carries its leaves with it: where the wind had the tree a moment ago, less where it has it now
			const double lag = 0.12 + (0.035 * p.y + 0.05 * std::hypot(p.x, p.z)) * (0.7 + 0.6 * hash01(salt, 6));
			const Vec3 late = windAt(t - lag) - w;
			const Vec3 limb = Vec3{late.x, 0.25 * std::hypot(late.x, late.z) * std::sin(2.3 * t + 6.0 * hash01(salt, 7)), late.z} * (p.y * size);
			places += nature::placeObject(tuft, mul(translate(foot + limb), nature::shearXZ(w.x, w.z), rotate({0, 1, 0}, tr.turn), scale(size), detail::tuftTurn(p, salt, t, length(w))));
		}
	}
	return trees.empty() ? std::string() : "# a forest (film_forest.h)\n" + objects + places;
}
inline std::string text(const std::string &film, const std::vector<Tree> &trees, const Forest &f, double t, double filmT, const Vec3 &eyeStory, const dressing::Gusts &gusts = {}, double wet = 0.0) {
	Written written;
	return text(film, trees, f, t, filmT, eyeStory, gusts, wet, written);
}

}  // namespace forest
}  // namespace cartoon
