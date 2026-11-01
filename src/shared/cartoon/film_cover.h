#pragma once
// film_cover.h -- what grows on the land by itself: grass blades, and under the trees fallen leaves and ferns, out to a few hundred metres from the camera, and stones here and
// there. A film with land has it unless it says `undergrowth off`; `undergrowth amount A` thins or thickens it.
//
//   patches   a square of blades as a pbrt object, in a few shapes and a few colours. The land is laid out in grids of squares 1, 3, 9 and 27 metres across, finer near the camera,
//             fixed to the land so nothing crawls as the camera moves; a square in view takes the shape its place says, turned and tilted to lie on the ground, and leans with the
//             wind. A field is then some hundreds of placements of a few dozen objects, whatever its size.
//   colour    from the ground under it (terrain::colourAt): how dry and how bright it is pick one of six greens for the kind of land, each a picture of blades, dark at the root
//             and paler at the tip, no two alike. Light comes through a blade.
//   where     not on steep ground, in water, on snow, or high on a mountain. Under the trees the grass gives way to litter and ferns.
//
// Coordinates: pbrt's for the land, story space (x mirrored) for the placements, as film_forest.h.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "cartoon_lib.h"
#include "film_cache.h"
#include "film_dressing.h"
#include "film_forest.h"
#include "film_sky.h"
#include "film_terrain.h"
#include "nature_lib.h"

namespace cartoon {
namespace cover {

struct Cover {
	bool on = true;
	double amount = 1.0;
};

constexpr int kShapes = 3, kDry = 3, kBright = 2, kBladeCols = 8;
struct Ring {
	double cell, reach;   // (the squares' size; out to what distance from the camera)
	int blades, joints;   // (in a square of the amount 1; joints in a blade)
	double width;         // (a blade's: wider as they are fewer, so a field far off is as full)
};
inline const std::vector<Ring> &rings() {
	static const std::vector<Ring> r = {{1.0, 8.0, 900, 3, 0.0045}, {3.0, 42.0, 1500, 3, 0.012}, {9.0, 130.0, 1800, 2, 0.036}, {27.0, 330.0, 1800, 1, 0.11}};
	return r;
}
// Whether a square of ring r is laid, `away` from the camera. `wander` (0 to 1, the square's own) moves the ring's far edge, as a line where the grass changes would show. That edge can
// then fall short by a square, and a spot can be in a square whose middle is half a diagonal further off, so the next ring starts inside it: every spot is in one ring or both, and
// no bare ground shows where they meet.
inline bool ringLays(std::size_t r, double away, double wander) {
	const std::vector<Ring> &all = rings();
	if (away + all[r].cell * wander >= all[r].reach) return false;
	return r == 0 || away + 0.75 * all[r].cell >= all[r - 1].reach - 1.75 * all[r - 1].cell;
}
// What grows on a land: `grass` the colour of the green, `straw` of the dry, `tall` how high.
struct Growth {
	Vec3 green, straw;
	double tall = 0.32, density = 1.0;
};
// What this land grows; density 0: nothing.
inline Growth growthOf(const terrain::Spec &s) {
	if (s.biome == "savanna") return {{0.26, 0.25, 0.09}, {0.52, 0.42, 0.19}, 0.55, 0.9};
	if (s.biome == "jungle") return {{0.07, 0.17, 0.04}, {0.14, 0.2, 0.06}, 0.4, 1.0};
	if (s.biome == "desert") return {{0.3, 0.27, 0.13}, {0.42, 0.36, 0.2}, 0.22, 0.18};
	if (s.biome == "tundra") return {{0.17, 0.17, 0.08}, {0.32, 0.26, 0.13}, 0.1, 0.8};
	if (s.biome == "arctic" || s.kind == "dunes" || s.kind == "canyon") return {{0, 0, 0}, {0, 0, 0}, 0.0, 0.0};
	return {{0.085, 0.19, 0.05}, {0.34, 0.31, 0.12}, 0.32, 1.0};
}

namespace detail {
// A square of `blades` blades, `size` across, its middle at the origin, each blade with its own place, height, lean and column of the picture (u) and its height up it (v).
inline Mesh patchMesh(double size, int blades, int joints, double width, double tall, int salt) {
	Mesh m;
	for (int b = 0; b < blades; ++b) {
		const int s = salt * 131 + b;
		const Vec3 root{size * (hash01(s, 1) - 0.5), 0.0, size * (hash01(s, 2) - 0.5)};
		const double h = tall * (0.45 + 0.75 * hash01(s, 3)), a = 2.0 * kPi * hash01(s, 4), lean = h * (0.15 + 0.6 * hash01(s, 5));
		const Vec3 facing{std::cos(a), 0.0, std::sin(a)}, side{-facing.z, 0.0, facing.x};
		const double col = (std::floor(hash01(s, 6) * kBladeCols) + 0.5) / kBladeCols;
		const int base = static_cast<int>(m.verts.size());
		const Vec3 normal = normalize(facing + Vec3{0.0, 0.55, 0.0});
		for (int j = 0; j <= joints; ++j) {
			const double u = static_cast<double>(j) / (joints + 1), w = width * (1.0 - u * u);
			const Vec3 at = root + Vec3{0.0, h * u, 0.0} + facing * (lean * u * u);
			m.verts.push_back(at - side * w), m.verts.push_back(at + side * w);
			m.normals.push_back(normal), m.normals.push_back(normal);
			m.uvs.push_back({col, u}), m.uvs.push_back({col, u});
		}
		m.verts.push_back(root + Vec3{0.0, h, 0.0} + facing * lean);
		m.normals.push_back(normal);
		m.uvs.push_back({col, 0.98});
		for (int j = 0; j < joints; ++j) {
			const int a0 = base + 2 * j;
			m.tris.push_back({a0, a0 + 1, a0 + 3});
			m.tris.push_back({a0, a0 + 3, a0 + 2});
		}
		const int top = base + 2 * joints;
		m.tris.push_back({top, top + 1, top + 2});
	}
	return m;
}
// Fallen leaves lying about on a square, some of them curled up off it. Each is a blade's column of the picture.
inline Mesh litterMesh(double size, int leaves, double leafSize, int salt) {
	Mesh m;
	for (int k = 0; k < leaves; ++k) {
		const int s = salt * 173 + k;
		const Vec3 at{size * (hash01(s, 1) - 0.5), 0.015 + 0.03 * hash01(s, 2), size * (hash01(s, 3) - 0.5)};
		const double a = 2.0 * kPi * hash01(s, 4), len = leafSize * (0.6 + 0.8 * hash01(s, 5));
		const std::size_t from = m.verts.size();
		nature::addLeaf(m, at, {std::cos(a), 0.12 * (hash01(s, 6) - 0.5), std::sin(a)}, normalize(Vec3{0.3 * (hash01(s, 7) - 0.5), 1.0, 0.3 * (hash01(s, 8) - 0.5)}), len, len * 0.6);
		const double col = (std::floor(hash01(s, 9) * kBladeCols) + 0.5) / kBladeCols;
		for (std::size_t i = from; i < m.verts.size(); ++i) m.uvs[i] = {col, 0.2 + 0.6 * m.uvs[i].second};
	}
	return m;
}
inline Vec3 colourFor(const Growth &g, int dry, int bright) {
	const Vec3 c = lerp(g.green, g.straw, dry / (kDry - 1.0));
	return c * (bright ? 1.2 : 0.8);
}
// A picture of blades in `colour`: a column each, dark at the root, where they shade each other, and paler at the tip. Light through a blade is yellower, and with what it reflects
// never more than came in.
inline std::string bladeMaterial(const std::string &film, const std::string &name, Vec3 colour, bool leaves, double wet) {
	constexpr int W = 128, H = 32;
	const auto paint = [&](std::vector<float> &rgb, bool through) {
		for (int y = 0; y < H; ++y)
			for (int x = 0; x < W; ++x) {
				const int col = x * kBladeCols / W;
				const double v = 1.0 - (y + 0.5) / H;
				const Vec3 own = lerp(colour, Vec3{colour.y * 1.05, colour.y, colour.z * 0.7}, 0.5 * hash01(col, 61)) * (0.7 + 0.6 * hash01(col, 62));
				Vec3 c = leaves ? lerp(own, Vec3{0.3, 0.17, 0.07} * (0.5 + 0.9 * hash01(col, 63)), 0.7) : own * (0.45 + 0.75 * smoothstep(0.0, 0.8, v));
				const Vec3 reflected = c;
				if (through) c = Vec3{c.x * 1.1 + 0.03, c.y * 1.2 + 0.03, c.z * 0.8} * (leaves ? 1.0 : 1.0 / (1.0 - forest::detail::kWax));
				const double most = through ? 0.9 - std::max({reflected.x, reflected.y, reflected.z}) : 0.9;
				float *px = &rgb[(static_cast<std::size_t>(y) * W + x) * 3];
				px[0] = static_cast<float>(std::min(c.x, most)), px[1] = static_cast<float>(std::min(c.y, most)), px[2] = static_cast<float>(std::min(c.z, most));
			}
	};
	const std::string r = name + "_r2", t = name + "_t2";
	const std::string reflects = film_sky::picture(film, r, W, H, [&](std::vector<float> &rgb) { paint(rgb, false); });
	const std::string passes = film_sky::picture(film, t, W, H, [&](std::vector<float> &rgb) { paint(rgb, true); });
	const std::string matte = "\"texture reflectance\" \"" + r + "\" \"texture transmittance\" \"" + t + "\"";
	return "Texture \"" + r + "\" \"spectrum\" \"imagemap\" \"string wrap\" \"clamp\" \"string filename\" [ \"" + reflects + "\" ]\n  Texture \"" + t +
		   "\" \"spectrum\" \"imagemap\" \"string wrap\" \"clamp\" \"string filename\" [ \"" + passes + "\" ]\n  " +
		   (leaves ? "Material \"diffusetransmission\" " + matte : forest::detail::waxyMaterial(name, matte, "\"texture reflectance\" \"" + r + "\"", wet));   // (a dead leaf has lost its wax)
}
}  // namespace detail

// The film's cover seen from `eye` looking at `target` (story space) with `fovDeg`, at t: the objects it uses and a placement for each square in view. `trees`: the film's forests,
// under which it is litter and ferns. `snow` how much lies on the land (0 .. 1): it goes under. `wet`, 0 .. 1: rain on the blades.
inline std::string text(const std::string &film, const terrain::Field &field, const Cover &c, Vec3 eye, Vec3 target, double fovDeg, double t, double snow, const dressing::Gusts &gusts,
						const std::vector<std::vector<forest::Tree>> &trees, double wet = 0.0) {
	const terrain::Spec &s = field.spec;
	const Growth g = growthOf(s);
	if (!c.on || g.density <= 0.0 || snow > 0.5) return std::string();
	// (the trees, by the 8 metre squares they stand in; a place is under one within a few metres of its trunk, an edge that wanders with the place)
	std::map<long long, std::vector<Vec3>> shaded;
	const auto squareOf = [](int i, int k) { return (static_cast<long long>(i) << 24) ^ static_cast<long long>(k); };
	for (const std::vector<forest::Tree> &planted : trees)
		for (const forest::Tree &tree : planted)
			if (tree.species != "shrub" && tree.species != "fern" && tree.species != "cactus") shaded[squareOf(static_cast<int>(std::floor(tree.at.x / 8.0)), static_cast<int>(std::floor(tree.at.z / 8.0)))].push_back(tree.at);
	const auto underTrees = [&](double x, double z, double reach) {
		const int a = static_cast<int>(std::floor(x / 8.0)), b = static_cast<int>(std::floor(z / 8.0));
		for (int k = b - 1; k <= b + 1; ++k)
			for (int i = a - 1; i <= a + 1; ++i)
				if (const auto it = shaded.find(squareOf(i, k)); it != shaded.end())
					for (const Vec3 &p : it->second)
						if (std::hypot(p.x - x, p.z - z) < reach) return true;
		return false;
	};
	const Vec3 eyeP{-eye.x, eye.y, eye.z};
	const Vec3 ahead = normalize(Vec3{-(target.x - eye.x), 0.0, target.z - eye.z});
	const double wide = std::cos(std::min(kPi * 0.5, (fovDeg * 0.5 * 16.0 / 9.0 + 22.0) * kPi / 180.0));
	std::string objects, places;
	std::set<std::string> made;
	const double above = s.waterLevel > -1e29 ? s.waterLevel + 0.45 : -1e30;
	const std::vector<Ring> &all = rings();
	for (std::size_t r = 0; r < all.size(); ++r) {
		const Ring &ring = all[r];
		const int i0 =static_cast<int>(std::floor((eyeP.x - ring.reach) / ring.cell)), i1 = static_cast<int>(std::ceil((eyeP.x + ring.reach) / ring.cell));
		const int k0 = static_cast<int>(std::floor((eyeP.z - ring.reach) / ring.cell)), k1 = static_cast<int>(std::ceil((eyeP.z + ring.reach) / ring.cell));
		for (int k = k0; k <= k1; ++k)
			for (int i = i0; i <= i1; ++i) {
				const double x = (i + 0.5) * ring.cell, z = (k + 0.5) * ring.cell;
				if (!field.inside(x, z)) continue;
				const double y = field.heightAt(x, z);
				const double away = std::sqrt((x - eyeP.x) * (x - eyeP.x) + (y - eyeP.y) * (y - eyeP.y) + (z - eyeP.z) * (z - eyeP.z));
				const int salt = (i * 7919) ^ (k * 104729) ^ static_cast<int>(r * 31);
				if (!ringLays(r, away, hash01(salt, 71))) continue;
				const Vec3 to = normalize(Vec3{x - eyeP.x, 0.0, z - eyeP.z});
				if (away > 2.0 * ring.cell && dot(to, ahead) < wide) continue;   // (not in view)
				const Vec3 n = field.normalAt(x, z, 0.5 * ring.cell);
				if (n.y < 0.84 || y < above) continue;
				if (s.snowLine() < 1e8 && y - s.y > s.snowLine() * 0.9) continue;
				const bool under = !shaded.empty() && underTrees(x, z, 3.4 + 2.2 * hash01(salt, 75));
				const Vec3 ground = terrain::colourAt(field, x, z);
				const double dryness = ground.x / std::max(1e-4, ground.y), light = ground.x + ground.y + ground.z;
				const int dry = dryness > 1.0 ? 2 : dryness > 0.72 ? 1 : 0, bright = light > 0.42 ? 1 : 0;
				const int shape = static_cast<int>(hash01(salt, 73) * kShapes) % kShapes;
				char id[80];
				std::snprintf(id, sizeof id, "cover_%s_%d_%d_%d_%d", under ? "litter" : "grass", static_cast<int>(r), shape, dry, bright);
				if (made.insert(id).second) {
					const std::string name = id;
					const double keep = std::clamp(c.amount, 0.05, 3.0);
					const std::string file = film_cache::ply(film, name + (keep == 1.0 ? "" : "_" + std::to_string(static_cast<int>(keep * 100.0))), [&] {
						return under ? detail::litterMesh(ring.cell, static_cast<int>(ring.blades * 0.35 * keep), std::max(0.09, 0.03 * ring.cell), 1 + shape)
									 : detail::patchMesh(ring.cell, static_cast<int>(ring.blades * keep * g.density), ring.joints, ring.width, g.tall, 5 + shape + 11 * static_cast<int>(r));
					});
					objects += nature::objectBegin(name) + "  AttributeBegin\n  " + detail::bladeMaterial(film, name, detail::colourFor(g, dry, bright), under, wet) + "\n" + film_cache::plyShape(file) + "  AttributeEnd\n" + nature::objectEnd();
				}
				// tilted to lie on the ground, and leaning with the wind
				const Mat4 tilt = {1, 0, 0, 0, n.x / n.y, 1, -n.z / n.y, 0, 0, 0, 1, 0, 0, 0, 0, 1};
				const Vec3 foot{-x, y - 0.02, z};
				const Vec3 w = under ? Vec3{0.0, 0.0, 0.0} : nature::windLean(foot.x, foot.z, t, 0.18) + dressing::gustAt(gusts, foot.x, foot.z, t) * 0.35;
				places += nature::placeObject(id, mul(translate(foot), tilt, nature::shearXZ(w.x, w.z), rotate({0, 1, 0}, 90.0 * static_cast<int>(hash01(salt, 74) * 4.0))));
				// here and there, close enough to see: a stone half sunk in the ground, and under the trees a fern
				if (ring.cell == 3.0) {
					const auto dressed = [&](const char *what, int kinds, const std::function<std::string(int)> &make, double chance, double sinks, double size) {
						if (hash01(salt, 76) > chance) return;
						const int which = static_cast<int>(hash01(salt, 77) * kinds) % kinds;
						const std::string name = std::string("cover_") + what + std::to_string(which);
						if (made.insert(name).second) objects += make(which);
						const Vec3 at{-(x + ring.cell * (hash01(salt, 78) - 0.5)), y - sinks, z + ring.cell * (hash01(salt, 79) - 0.5)};
						places += nature::placeObject(name, mul(translate(at), rotate({0, 1, 0}, 360.0 * hash01(salt, 80)), scale(size * (0.6 + 0.9 * hash01(salt, 81)))));
					};
					if (under) dressed("fern", 2, [&](int k) {
						const std::string file = film_cache::ply(film, "cover_fern" + std::to_string(k), [&] { return nature::fern(9, 0.9, 700 + k); });
						return nature::objectBegin("cover_fern" + std::to_string(k)) + "  AttributeBegin\n  " + translucentMaterial({0.07, 0.17, 0.04}, {0.10, 0.22, 0.04}) + "\n" + film_cache::plyShape(file) + "  AttributeEnd\n" + nature::objectEnd();
					}, 0.45, 0.02, 1.0);
					dressed("stone", 3, [&](int k) {
						const std::string file = film_cache::ply(film, "cover_stone" + std::to_string(k), [&] { return nature::rock(0.5, 720 + k); });
						const Vec3 rock = lerp(ground, Vec3{0.3, 0.29, 0.27}, 0.7);
						return nature::objectBegin("cover_stone" + std::to_string(k)) + "  AttributeBegin\n  " + diffuseMaterial(rock) + "\n" + film_cache::plyShape(file) + "  AttributeEnd\n" + nature::objectEnd();
					}, 0.12, 0.08, 1.0);
				}
			}
	}
	return places.empty() ? std::string() : "# what grows on the land (film_cover.h)\n" + objects + places;
}

}  // namespace cover
}  // namespace cartoon
