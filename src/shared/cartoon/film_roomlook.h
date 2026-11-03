#pragma once
// film_roomlook.h -- what a room looks like (indoor round 1): its materials by its style, its walls painted or papered and its floor's boards, tiles or carpet (`walls`,
// `floor`), and the whole of what does not move - walls, floors, ceilings, the stairs, the furniture - as cached meshes, one a material.
//
//   walls ROOM paint R G B                                   painted walls
//   walls ROOM wallpaper stripes|roses|checks|herringbone|tiles [colour R G B] [colour R G B]   papered (two colours: the ground and the pattern)
//   floor ROOM wood|tiles|carpet|checks|boards [colour R G B]
//   style (on `room` and `house`): cosy (warm wood, red fabric, cream walls), modern (pale wood, grey, white), old (dark wood, green, faded walls)
//
// Patterns are pictures painted once into the film's cache (film_sky.h), a metre of the wall or floor each, repeating; their names carry every number they are made from.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

#include "cartoon_lib.h"
#include "film_cache.h"
#include "film_furniture.h"
#include "film_rooms.h"
#include "film_sky.h"
#include "nature_lib.h"

namespace cartoon {
namespace rooms {

inline const std::vector<std::string> &papers() {
	static const std::vector<std::string> k = {"stripes", "roses", "checks", "herringbone", "tiles"};
	return k;
}
inline const std::vector<std::string> &floors() {
	static const std::vector<std::string> k = {"wood", "boards", "tiles", "carpet", "checks"};
	return k;
}
inline const std::vector<std::string> &styles() {
	static const std::vector<std::string> k = {"cosy", "modern", "old"};
	return k;
}
// The groups a room's meshes can be in, and each one's own material (a room's style, its walls' and floor's looks).
inline const std::vector<std::string> &groupNames() {
	static const std::vector<std::string> k = {"wall0", "wall1", "wall2", "wall3", "floor", "ceiling", "trim", "white", "wood", "fabric", "fabric2", "accent", "dark", "metal", "stone",
											   "glass", "pane", "mirror", "water", "red", "blue", "green", "yellow", "board", "rug", "pot", "leaf", "shade"};
	return k;
}
struct Palette {
	Vec3 wood, fabric, fabric2, accent, wall, floor, rug;
};
inline Palette paletteOf(const std::string &style) {
	if (style == "modern") return {{0.72, 0.58, 0.42}, {0.34, 0.37, 0.41}, {0.5, 0.53, 0.56}, {0.95, 0.62, 0.15}, {0.9, 0.9, 0.88}, {0.7, 0.62, 0.52}, {0.6, 0.62, 0.64}};
	if (style == "old") return {{0.27, 0.16, 0.09}, {0.22, 0.35, 0.25}, {0.33, 0.45, 0.34}, {0.62, 0.48, 0.2}, {0.72, 0.68, 0.55}, {0.33, 0.21, 0.12}, {0.5, 0.22, 0.18}};
	return {{0.45, 0.27, 0.14}, {0.62, 0.2, 0.16}, {0.75, 0.32, 0.25}, {0.95, 0.75, 0.3}, {0.88, 0.8, 0.65}, {0.5, 0.31, 0.17}, {0.58, 0.36, 0.22}};
}

namespace detail {
// A pattern's picture: a metre square of it (W pixels), its name every number it is made from.
inline std::string patternPicture(const std::string &film, const std::string &kind, Vec3 a, Vec3 b, int seed) {
	constexpr int W = 256;
	char name[200];
	std::snprintf(name, sizeof name, "room1_%s_%.3f_%.3f_%.3f_%.3f_%.3f_%.3f_%d", kind.c_str(), a.x, a.y, a.z, b.x, b.y, b.z, seed);
	return film_sky::picture(film, name, W, W, [&](std::vector<float> &rgb) {
		for (int y = 0; y < W; ++y)
			for (int x = 0; x < W; ++x) {
				const double u = (x + 0.5) / W, v = (y + 0.5) / W;   // (metres, across and up)
				double t = 0.0;
				Vec3 c = a;
				if (kind == "stripes") t = std::fmod(u * 6.0, 1.0) < 0.42 ? 1.0 : 0.0;
				else if (kind == "checks") t = (static_cast<int>(u * 5.0) + static_cast<int>(v * 5.0)) % 2 ? 1.0 : 0.0;
				else if (kind == "tiles") {   // white tiles, the pattern's colour its grout
					const double gu = std::fmod(u * 6.0, 1.0), gv = std::fmod(v * 6.0, 1.0);
					t = gu < 0.05 || gv < 0.05 ? 1.0 : 0.0;
				} else if (kind == "roses") {   // a scatter of little flowers in a lattice, each of five petals round a heart
					const double cu = std::fmod(u * 4.0 + (static_cast<int>(v * 4.0) % 2 ? 0.5 : 0.0), 1.0) - 0.5, cv = std::fmod(v * 4.0, 1.0) - 0.5;
					const double r = std::hypot(cu, cv), ang = std::atan2(cv, cu);
					t = r < 0.18 * (0.7 + 0.3 * std::cos(5.0 * ang)) ? (r < 0.05 ? 0.6 : 1.0) : 0.0;
				} else if (kind == "herringbone") {   // planks set at right angles in a zigzag
					const double k = std::fmod(u * 4.0 + v * 4.0, 2.0), p = std::fmod((k < 1.0 ? u : v) * 16.0, 1.0);
					t = p < 0.06 ? 1.0 : 0.15 * nature::noise2(u * 40.0, v * 3.0, seed);
				} else if (kind == "wood" || kind == "boards") {   // boards (18 cm, or 12 for `boards`) of their own shade, a grain along them, a dark line between
					const double width = kind == "boards" ? 0.12 : 0.18, row = std::floor(u / width), along = v + hash01(static_cast<int>(row) + seed * 37, 1);
					const double seam = std::fmod(u / width, 1.0), end = std::fmod(along * 0.5, 1.0);
					const double grain = nature::noise2(u * 60.0, along * 4.0, seed) * 0.5 + nature::noise2(u * 200.0, along * 12.0, seed + 1) * 0.2;
					c = a * (0.82 + 0.3 * hash01(static_cast<int>(row) * 13 + static_cast<int>(std::floor(along * 0.5)), seed)) * (0.85 + 0.3 * grain);
					t = seam < 0.03 || end < 0.01 ? 1.0 : 0.0;
				} else if (kind == "carpet") {
					c = a * (0.86 + 0.28 * nature::noise2(u * 90.0, v * 90.0, seed));
				}
				const Vec3 out = lerp(c, b, std::clamp(t, 0.0, 1.0));
				float *p = &rgb[(static_cast<std::size_t>(y) * W + x) * 3];
				p[0] = static_cast<float>(out.x), p[1] = static_cast<float>(out.y), p[2] = static_cast<float>(out.z);
			}
	});
}
inline std::string textured(const std::string &film, const std::string &tex, const std::string &kind, Vec3 a, Vec3 b, int seed, double rough) {
	const std::string pic = patternPicture(film, kind, a, b, seed);
	char r[40];
	std::snprintf(r, sizeof r, "%.3f", rough);
	return "Texture \"" + tex + "\" \"spectrum\" \"imagemap\" \"string filename\" [ \"" + pic + "\" ]\n  " +
		   (rough < 0.99 ? "Material \"coateddiffuse\" \"texture reflectance\" \"" + tex + "\" \"float roughness\" [ " + r + " ]" : "Material \"diffuse\" \"texture reflectance\" \"" + tex + "\"");
}
}  // namespace detail

// The material of a group of room `r` (`tex`: a name for its texture, the film's own).
inline std::string materialOf(const std::string &film, const Room &r, const std::string &group, const std::string &tex, double fog = 0.0) {
	if (group == "mirror" && fog > 0.0) return metalMaterial(lerp(Vec3{0.92, 0.93, 0.94}, Vec3{0.78, 0.8, 0.82}, fog), lerp(0.003, 0.32, fog));   // (steamed up: indoor round 2)
	const Palette p = paletteOf(r.style);
	if (group.rfind("wall", 0) == 0) {
		if (!r.look.paper.empty()) return detail::textured(film, tex, r.look.paper, r.look.paper == "tiles" ? Vec3{0.92, 0.93, 0.92} : r.look.wall, r.look.paper == "tiles" ? Vec3{0.62, 0.64, 0.62} : r.look.paper2, r.seed, 1.0);
		return diffuseMaterial(r.look.wall);
	}
	if (group == "floor") {
		const std::string kind = !r.look.floor.empty() ? r.look.floor : r.kind == "bathroom" || r.kind == "kitchen" ? "tiles" : r.kind == "bedroom" ? "carpet" : "wood";
		const Vec3 base = r.look.floorColour.x >= 0.0 ? r.look.floorColour : kind == "tiles" ? Vec3{0.85, 0.84, 0.8} : kind == "carpet" ? p.rug : kind == "checks" ? Vec3{0.9, 0.88, 0.84} : p.floor;
		const Vec3 lines = kind == "tiles" ? Vec3{0.45, 0.45, 0.43} : kind == "checks" ? Vec3{0.12, 0.12, 0.13} : base * 0.45;
		return detail::textured(film, tex, kind, base, lines, r.seed, kind == "carpet" ? 1.0 : 0.35);
	}
	if (group == "ceiling") return diffuseMaterial({0.9, 0.89, 0.86});
	if (group == "trim" || group == "white") return coated({0.9, 0.9, 0.88}, 0.3);
	if (group == "wood") return coated(p.wood, 0.4);
	if (group == "fabric") return diffuseMaterial(p.fabric);
	if (group == "fabric2") return diffuseMaterial(p.fabric2);
	if (group == "accent") return diffuseMaterial(p.accent);
	if (group == "rug") return detail::textured(film, tex, "carpet", p.rug, p.rug, r.seed + 3, 1.0);
	if (group == "dark") return coated({0.05, 0.05, 0.055}, 0.25);
	if (group == "metal") return metalMaterial({0.75, 0.75, 0.76}, 0.18);
	if (group == "stone") return coated({0.55, 0.53, 0.5}, 0.3);
	if (group == "glass") return "Material \"thindielectric\" \"float eta\" [ 1.5 ]";
	if (group == "pane") return "Material \"thindielectric\" \"float eta\" [ 1.5 ]";   // (by day the sky through it; by night, with the lamps on, the room in it: a dim mirror)
	if (group == "mirror") return metalMaterial({0.92, 0.93, 0.94}, 0.003);
	if (group == "water") return "Material \"dielectric\" \"float eta\" [ 1.33 ]";
	if (group == "board") return diffuseMaterial({0.08, 0.2, 0.15});
	if (group == "pot") return diffuseMaterial({0.62, 0.32, 0.2});
	if (group == "leaf") return translucentMaterial({0.08, 0.3, 0.07}, {0.1, 0.32, 0.05});
	if (group == "shade") return translucentMaterial({0.85, 0.78, 0.62}, {0.55, 0.45, 0.3});
	static const std::map<std::string, Vec3> books = {{"red", {0.55, 0.12, 0.1}}, {"blue", {0.12, 0.2, 0.45}}, {"green", {0.15, 0.38, 0.18}}, {"yellow", {0.8, 0.62, 0.15}}};
	if (const auto b = books.find(group); b != books.end()) return diffuseMaterial(b->second);
	return diffuseMaterial({0.6, 0.6, 0.6});
}

// What of a room does not move, by group (story space): its walls (each its own group, for a cutaway), floor and ceiling (with the stairs' holes), the stairs, the furniture and
// the ceiling lamps' cords and shades. `opened`: the items whose doors someone opens (drawn on their own).
inline Groups staticGroups(const Room &r, const std::vector<int> &opened) {
	Groups g;
	const Mat4 F = frameOf(r);
	for (int wall = 0; wall < 4; ++wall) wallPieces(r, wall, g, F);
	slab(r, g["floor"], F, -0.06, 0.0, r.floorHole);
	slab(r, g["ceiling"], F, r.h, r.h + 0.06, r.ceilingHole);
	if (r.stairs) stairPieces(r, g, F);
	for (std::size_t i = 0; i < r.items.size(); ++i) pieceOf(r.items[i], g, F, std::find(opened.begin(), opened.end(), static_cast<int>(i)) == opened.end());
	for (const Lamp &l : r.lamps)
		if (l.kind == "ceiling") {   // (a cord from the ceiling and a shade round its bulb)
			detail::addMesh(g["dark"], sdf::tubeMesh({{l.at.x, r.h, l.at.z}, {l.at.x, l.at.y + 0.12, l.at.z}}, 0.006, 1.0, 6), F);
			detail::addMesh(g["shade"], lathe({{0.05, 0.14}, {0.26, -0.05}, {0.25, -0.05}, {0.04, 0.14}}, 24), mul(F, translate(l.at)));
		}
	return g;
}
// The name of a room's meshes: every number they are made from.
inline std::string keyOf(const Room &r, const std::vector<int> &opened) {
	std::string words;
	char b[200];
	std::snprintf(b, sizeof b, "v1|%s|%s|%.4f|%.4f|%.4f|%.4f|%.4f|%.4f|%.3f|%d|%d|%d|%d|%.3f|%.3f|%.3f|%.3f", r.kind.c_str(), r.style.c_str(), r.x, r.z, r.y, r.w, r.d, r.h, r.facing, r.seed, r.stairs,
				  r.ceilingHole, r.floorHole, r.holeX0, r.holeX1, r.holeZ0, r.holeZ1);
	words += b;
	for (const Opening &o : r.openings) std::snprintf(b, sizeof b, "|o%d,%.3f,%.3f,%.3f,%.3f,%d", o.wall, o.along, o.width, o.height, o.sill, o.door), words += b;
	for (const Item &it : r.items) std::snprintf(b, sizeof b, "|i%s,%.3f,%.3f,%.1f,%d%s", it.kind.c_str(), it.x, it.z, it.turn, it.salt, it.water ? "" : "d"), words += b;
	for (const Lamp &l : r.lamps) std::snprintf(b, sizeof b, "|l%s,%.3f,%.3f,%.3f", l.kind.c_str(), l.at.x, l.at.y, l.at.z), words += b;
	for (const int i : opened) words += "|x" + std::to_string(i);
	return film_cache::nameOf(words);
}
// A room's still part as scene text: a placement of its meshes (written once), each in its material. `leaveOut`: walls not to draw (bit w: wall w), for a cutaway.
inline std::string staticText(const std::string &film, const Room &r, const std::vector<int> &opened, int leaveOut = 0, double fog = 0.0) {
	const std::string key = keyOf(r, opened);
	std::optional<Groups> built;   // (made only when a file is missing)
	const auto groups = [&]() -> Groups & {
		if (!built) built = staticGroups(r, opened);
		return *built;
	};
	std::string out = "# a room: " + r.name + " (film_rooms.h)\n";
	for (const std::string &name : groupNames()) {
		if (name.rfind("wall", 0) == 0 && (leaveOut >> (name[4] - '0') & 1)) continue;
		const std::string file = film_cache::ply(film, "room1_" + key + "_" + name, [&] { return groups()[name]; });
		if (file.empty()) continue;
		out += "AttributeBegin\n  " + materialOf(film, r, name, "room_" + key.substr(0, 8) + "_" + name, std::round(fog * 20.0) / 20.0) + "\n" + film_cache::plyShape(file) + "AttributeEnd\n";
	}
	return out;
}

}  // namespace rooms
}  // namespace cartoon
