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
#include <cstring>
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
#include "film_moss.h"
#include "film_sky.h"
#include "film_terrain.h"
#include "nature_lib.h"

namespace cartoon {
namespace cover {

struct Cover {
	bool on = true;
	double amount = 1.0;
	std::string season;   // (`undergrowth season S`; none: the forests')
};

// A way the film's animals keep to (pbrt's x z, from and to): the grass is thin along it and the earth shows through.
struct Trail {
	double x0 = 0.0, z0 = 0.0, x1 = 0.0, z1 = 0.0, wide = 1.0;
};
// Where something heavy is or has just been (pbrt's x z; `age` seconds ago; `reach` how far it pushes): the grass is shoved aside and lies down, and comes back in a few seconds.
struct Press {
	double x = 0.0, z = 0.0, reach = 0.5, age = 0.0;
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
	if (s.biome == "bloom") return {{0.2, 0.08, 0.3}, {0.05, 0.25, 0.27}, 0.28, 0.8};   // (a violet sward, teal where it is dry)
	if (s.biome == "arctic" || s.biome == "rust" || s.biome == "frost" || s.biome == "glass" || s.biome == "moonrock" || s.kind == "dunes" || s.kind == "canyon") return {{0, 0, 0}, {0, 0, 0}, 0.0, 0.0};
	return {{0.085, 0.19, 0.05}, {0.34, 0.31, 0.12}, 0.32, 1.0};
}

namespace detail {
// A square of `blades` blades, `size` across, its middle at the origin, each blade with its own place, height, lean and column of the picture (u) and its height up it (v). Grass is not
// a carpet: it grows in tussocks of their own height with thinner ground between them, a share of it has died where it stood (the picture's last two columns are straw) or lies flat
// under the rest, and close to the camera some has gone to seed (`heads`).
inline Mesh patchMesh(double size, int blades, int joints, double width, double tall, int salt, bool heads = false) {
	Mesh m;
	constexpr int kTussocks = 6;
	const double spread = 0.09 * size;
	for (int b = 0; b < blades; ++b) {
		const int s = salt * 131 + b, tussock = static_cast<int>(hash01(s, 10) * kTussocks) % kTussocks, own = salt * 17 + tussock;
		const bool loose = hash01(s, 11) < 0.3, dead = hash01(s, 12) < 0.16, lying = hash01(s, 13) < 0.1, seeded = heads && !lying && hash01(s, 14) < 0.025;
		const double wx = hash01(s, 1) - 0.5, wz = hash01(s, 2) - 0.5;   // (scattered, or drawn in round a tussock: a gaussian about it, by two chances)
		const double spin = 2.0 * kPi * wx, reach = spread * std::sqrt(-2.0 * std::log(std::max(1e-6, wz + 0.5)));
		const Vec3 root = loose ? Vec3{size * wx, 0.0, size * wz} : Vec3{size * (hash01(own, 15) - 0.5) + reach * std::cos(spin), 0.0, size * (hash01(own, 16) - 0.5) + reach * std::sin(spin)};
		const double own_h = 0.6 + 0.75 * hash01(own, 17);
		const double h = tall * (0.45 + 0.75 * hash01(s, 3)) * (loose ? 0.8 : own_h) * (lying ? 0.5 : 1.0) * (seeded ? 1.3 : 1.0);
		const double a = 2.0 * kPi * hash01(s, 4), lean = h * (lying ? 1.5 : 0.15 + 0.6 * hash01(s, 5));
		const Vec3 facing{std::cos(a), 0.0, std::sin(a)}, side{-facing.z, 0.0, facing.x};
		const double col = dead ? (kBladeCols - 1.5 + std::floor(hash01(s, 6) * 2.0)) / kBladeCols : (std::floor(hash01(s, 6) * (kBladeCols - 2)) + 0.5) / kBladeCols;
		if (seeded)   // a head of grain up the last of the stem, seven husks hanging to one side
			for (int k = 0; k < 7; ++k) {
				const double u = 0.8 + 0.2 * k / 6.0;
				const Vec3 at = root + Vec3{0.0, h * u, 0.0} + facing * (lean * u * u), dir = normalize(facing + Vec3{0.3 * std::cos(2.4 * k), -0.5, 0.3 * std::sin(2.4 * k)});
				const std::size_t from = m.verts.size();
				nature::addLeaf(m, at, dir, normalize(Vec3{0.0, 1.0, 0.0} + side * 0.4), 0.014, 0.005);
				for (std::size_t i = from; i < m.verts.size(); ++i) m.uvs[i] = {(kBladeCols - 0.5) / kBladeCols, 0.92};
			}
		const int stem = static_cast<int>(m.verts.size());
		const Vec3 normal = normalize(facing + Vec3{0.0, 0.55, 0.0});
		for (int j = 0; j <= joints; ++j) {
			const double u = static_cast<double>(j) / (joints + 1), w = width * (1.0 - u * u) * (seeded ? 0.5 : 1.0);
			const Vec3 at = root + Vec3{0.0, h * u, 0.0} + facing * (lean * u * u);
			m.verts.push_back(at - side * w), m.verts.push_back(at + side * w);
			m.normals.push_back(normal), m.normals.push_back(normal);
			m.uvs.push_back({col, u}), m.uvs.push_back({col, u});
		}
		m.verts.push_back(root + Vec3{0.0, h, 0.0} + facing * lean);
		m.normals.push_back(normal);
		m.uvs.push_back({col, 0.98});
		for (int j = 0; j < joints; ++j) {
			const int a0 = stem + 2 * j;
			m.tris.push_back({a0, a0 + 1, a0 + 3});
			m.tris.push_back({a0, a0 + 3, a0 + 2});
		}
		const int top = stem + 2 * joints;
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
		const double a = 2.0 * kPi * hash01(s, 4), len = leafSize * (0.4 + 0.75 * hash01(s, 5));
		const std::size_t from = m.verts.size();
		nature::addLeaf(m, at, {std::cos(a), 0.12 * (hash01(s, 6) - 0.5), std::sin(a)}, normalize(Vec3{0.3 * (hash01(s, 7) - 0.5), 1.0, 0.3 * (hash01(s, 8) - 0.5)}), len, len * 0.55);
		const double col = (std::floor(hash01(s, 9) * kBladeCols) + 0.5) / kBladeCols;
		for (std::size_t i = from; i < m.verts.size(); ++i) m.uvs[i] = {col, 0.2 + 0.6 * m.uvs[i].second};
	}
	forest::detail::shapeLeaves(m, 0, salt + 3);   // (dry ones curl)
	return m;
}
// What lies between the blades near the camera: stones the size of a thumbnail or a fist half sunk, broken dead stems, and bits of twig. `stones` and `stems` are two meshes, for two
// materials.
inline void addLitter(Mesh &stones, Mesh &stems, double size, int count, int salt) {
	for (int k = 0; k < count; ++k) {
		const int s = salt * 211 + k;
		const Vec3 at{size * (hash01(s, 1) - 0.5), 0.0, size * (hash01(s, 2) - 0.5)};
		if (hash01(s, 3) < 0.45) {
			const double r = 0.008 + 0.028 * std::pow(hash01(s, 4), 2.5);
			Mesh pebble = uvSphere(1.0, 6, 4);
			const Vec3 squash{r * (0.8 + 0.5 * hash01(s, 5)), r * (0.45 + 0.3 * hash01(s, 6)), r * (0.8 + 0.5 * hash01(s, 7))};
			const int base = static_cast<int>(stones.verts.size());
			for (std::size_t i = 0; i < pebble.verts.size(); ++i) {
				const Vec3 &v = pebble.verts[i];
				const double lump = 0.8 + 0.4 * hash01(s + static_cast<int>(i), 8);
				stones.verts.push_back(at + Vec3{v.x * squash.x * lump, v.y * squash.y * lump, v.z * squash.z * lump});
				stones.normals.push_back(normalize(Vec3{v.x / squash.x, v.y / squash.y, v.z / squash.z}));
			}
			for (const auto &t : pebble.tris) stones.tris.push_back({t[0] + base, t[1] + base, t[2] + base});
		} else {
			const double a = 2.0 * kPi * hash01(s, 9), len = 0.05 + 0.2 * hash01(s, 10), w = 0.0015 + 0.002 * hash01(s, 11), lift = 0.004 + 0.03 * hash01(s, 12);
			const Vec3 d{std::cos(a) * len, lift, std::sin(a) * len}, side{-std::sin(a) * w, 0.0, std::cos(a) * w};
			const int base = static_cast<int>(stems.verts.size());
			for (const Vec3 &p : {at - side + Vec3{0.0, 0.004, 0.0}, at + side + Vec3{0.0, 0.004, 0.0}, at + d + side, at + d - side}) stems.verts.push_back(p), stems.normals.push_back({0.0, 1.0, 0.0});
			stems.tris.push_back({base, base + 1, base + 2}), stems.tris.push_back({base, base + 2, base + 3});
		}
	}
}
// The earth itself, near the camera: a skin over the ground of a square, cut to blotches, in the colours of soil rather than the far colour of the land. Its uv in metres of its picture.
inline Mesh earthSkin(double size) {
	Mesh m;
	const double h = 0.5 * size;
	m.verts = {{-h, 0.012, -h}, {h, 0.012, -h}, {h, 0.012, h}, {-h, 0.012, h}};
	m.normals.assign(4, Vec3{0.0, 1.0, 0.0});
	m.uvs = {{0.0, 0.0}, {size, 0.0}, {size, size}, {0.0, size}};
	m.tris = {{0, 2, 1}, {0, 3, 2}};
	return m;
}
// The ground's colour to a twentieth (each part: blue too), and its name: what the near earth and stones are made in, so a square's are its own colour and not the first square's.
inline Vec3 soilOf(Vec3 ground) { return {std::floor(ground.x * 20.0) / 20.0, std::floor(ground.y * 20.0) / 20.0, std::floor(ground.z * 20.0) / 20.0}; }
inline std::string soilKey(Vec3 ground) {
	const Vec3 s = soilOf(ground);
	return std::to_string(static_cast<int>(s.x * 20.0 + 0.5)) + "_" + std::to_string(static_cast<int>(s.y * 20.0 + 0.5)) + "_" + std::to_string(static_cast<int>(s.z * 20.0 + 0.5));
}
inline std::string earthMaterial(const std::string &film, const std::string &name, Vec3 ground, const std::string &mesh, bool bare) {
	constexpr int W = 256;
	const std::string colours = film_sky::picture(film, "earth2_" + soilKey(ground), W, W, [&](std::vector<float> &rgb) {
		const Vec3 soil = lerp(Vec3{0.16, 0.115, 0.075}, soilOf(ground), 0.2);
		for (int y = 0; y < W; ++y)
			for (int x = 0; x < W; ++x) {
				const double a = 2.0 * kPi * x / W, b = 2.0 * kPi * y / W;
				const double big = nature::fbm2(2.0 * std::cos(a) + 3.0 * std::cos(b), 2.0 * std::sin(a) + 3.0 * std::sin(b), 61), fine = nature::fbm2(9.0 * std::cos(a) + 13.0 * std::cos(b), 9.0 * std::sin(a) + 13.0 * std::sin(b), 62);
				const Vec3 c = soil * (0.55 + 0.9 * big) * (0.75 + 0.5 * fine);
				float *px = &rgb[(static_cast<std::size_t>(y) * W + x) * 3];
				px[0] = static_cast<float>(c.x), px[1] = static_cast<float>(c.y), px[2] = static_cast<float>(c.z);
			}
	});
	const std::string cut = film_sky::picture(film, std::string("earth1_cut") + (bare ? "b" : ""), W, W, [&](std::vector<float> &rgb) {
		for (int y = 0; y < W; ++y)
			for (int x = 0; x < W; ++x) {
				const double a = 2.0 * kPi * x / W, b = 2.0 * kPi * y / W;
				const double n = nature::fbm2(1.7 * std::cos(a) + 2.6 * std::cos(b), 1.7 * std::sin(a) + 2.6 * std::sin(b), 63);
				const float keep = static_cast<float>(smoothstep(bare ? 0.3 : 0.47, (bare ? 0.3 : 0.47) + 0.05, n));
				float *px = &rgb[(static_cast<std::size_t>(y) * W + x) * 3];
				px[0] = px[1] = px[2] = keep;
			}
	});
	std::string shape = film_cache::plyShape(mesh);
	shape.pop_back();
	return "Texture \"" + name + "_c\" \"spectrum\" \"imagemap\" \"string filename\" [ \"" + colours + "\" ]\n  Texture \"" + name + "_a\" \"float\" \"imagemap\" \"string filename\" [ \"" + cut +
		   "\" ]\n  Material \"diffuse\" \"texture reflectance\" \"" + name + "_c\"\n" + shape + " \"texture alpha\" \"" + name + "_a\"\n";
}
// A few wild flowers on a stem each, about the origin, in `n` heads of five petals: what shows in the grass of a meadow.
inline void addFlowers(Mesh &stems, Mesh &petals, int n, double spread, int salt) {
	for (int k = 0; k < n; ++k) {
		const int s = salt * 37 + k;
		const double h = 0.22 + 0.3 * hash01(s, 1), a = 2.0 * kPi * hash01(s, 2), r = spread * std::sqrt(hash01(s, 3));
		const Vec3 root{r * std::cos(a), 0.0, r * std::sin(a)}, top = root + Vec3{0.1 * h * (hash01(s, 4) - 0.5), h, 0.1 * h * (hash01(s, 5) - 0.5)};
		const int b = static_cast<int>(stems.verts.size());
		for (const Vec3 &p : {root + Vec3{-0.002, 0.0, 0.0}, root + Vec3{0.002, 0.0, 0.0}, top})
			stems.verts.push_back(p), stems.normals.push_back({0.0, 0.0, 1.0});
		stems.tris.push_back({b, b + 1, b + 2});
		const Vec3 facing = normalize(Vec3{0.5 * (hash01(s, 6) - 0.5), 1.0, 0.5 * (hash01(s, 7) - 0.5)});
		for (int p = 0; p < 5; ++p) {
			const double t = 2.0 * kPi * p / 5.0;
			nature::addLeaf(petals, top, {std::cos(t), 0.12, std::sin(t)}, facing, 0.022, 0.016);
		}
	}
}
inline Vec3 colourFor(const Growth &g, int dry, int bright) {
	const Vec3 c = lerp(g.green, g.straw, dry / (kDry - 1.0));
	return c * (bright ? 1.2 : 0.8);
}
// A picture of blades in `colour`: a column each, dark at the root, where they shade each other, and paler at the tip. Light through a blade is yellower, and with what it reflects
// never more than came in.
inline std::string bladeMaterial(const std::string &film, const std::string &name, Vec3 colour, Vec3 straw, bool leaves, double wet) {
	constexpr int W = 128, H = 32;
	const auto paint = [&](std::vector<float> &rgb, bool through) {
		for (int y = 0; y < H; ++y)
			for (int x = 0; x < W; ++x) {
				const int col = x * kBladeCols / W;
				const double v = 1.0 - (y + 0.5) / H;
				const Vec3 own = lerp(colour, Vec3{colour.y * 1.05, colour.y, colour.z * 0.7}, 0.5 * hash01(col, 61)) * (0.7 + 0.6 * hash01(col, 62));
				const Vec3 fallen = hash01(col, 64) < 0.4 ? Vec3{0.11, 0.08, 0.05} : Vec3{0.25, 0.15, 0.07};   // (a dead leaf: last year's dark, this year's brown)
				Vec3 c = leaves ? lerp(own, fallen * (0.5 + 0.9 * hash01(col, 63)), 0.82) : col >= kBladeCols - 2 ? straw * (0.7 + 0.5 * hash01(col, 65)) * (0.75 + 0.35 * v) : own * (0.45 + 0.75 * smoothstep(0.0, 0.8, v));
				const Vec3 reflected = c;
				if (through) c = Vec3{c.x * 1.1 + 0.03, c.y * 1.2 + 0.03, c.z * 0.8} * (leaves ? 1.0 : 1.0 / (1.0 - forest::detail::kWax));
				const double most = through ? 0.9 - std::max({reflected.x, reflected.y, reflected.z}) : 0.9;
				float *px = &rgb[(static_cast<std::size_t>(y) * W + x) * 3];
				px[0] = static_cast<float>(std::min(c.x, most)), px[1] = static_cast<float>(std::min(c.y, most)), px[2] = static_cast<float>(std::min(c.z, most));
			}
	};
	const std::string r = name + "_r3", t = name + "_t3";
	const std::string reflects = film_sky::picture(film, r, W, H, [&](std::vector<float> &rgb) { paint(rgb, false); });
	const std::string passes = film_sky::picture(film, t, W, H, [&](std::vector<float> &rgb) { paint(rgb, true); });
	const std::string matte = "\"texture reflectance\" \"" + r + "\" \"texture transmittance\" \"" + t + "\"";
	return "Texture \"" + r + "\" \"spectrum\" \"imagemap\" \"string wrap\" \"clamp\" \"string filename\" [ \"" + reflects + "\" ]\n  Texture \"" + t +
		   "\" \"spectrum\" \"imagemap\" \"string wrap\" \"clamp\" \"string filename\" [ \"" + passes + "\" ]\n  " +
		   (leaves ? "Material \"diffusetransmission\" " + matte : forest::detail::waxyMaterial(name, matte, "\"texture reflectance\" \"" + r + "\"", wet));   // (a dead leaf has lost its wax)
}
}  // namespace detail

// How far the year has gone for what grows at filmT: -0.6 spring (fresh), 0 summer, 1 autumn (drying), 1.6 winter (dead and beaten down). A film's forests say, unless `undergrowth
// season` does.
inline double seasonLevel(const std::string &season) { return season == "spring" ? -0.6 : season == "autumn" ? 1.0 : season == "winter" ? 1.6 : 0.0; }
inline double yearAt(const Cover &c, const std::vector<forest::Forest> &forests, double filmT) {
	if (!c.season.empty()) return seasonLevel(c.season);
	if (forests.empty()) return 0.0;
	const forest::Forest &f = forests[0];
	double level = seasonLevel(f.season);
	if (!f.seasonTo.empty()) level = lerp(level, seasonLevel(f.seasonTo), smoothstep(f.turn0, f.turn1, filmT));
	for (const forest::Forest::Change &ch : f.then) level = lerp(level, seasonLevel(ch.to), smoothstep(ch.t0, ch.t1, filmT));
	return level;
}

// The film's cover seen from `eye` looking at `target` (story space) with `fovDeg`, at t: the objects it uses and a placement for each square in view. `trees`: the film's forests,
// under which it is litter and ferns. `snow` how much lies on the land (0 .. 1): it goes under. `wet`, 0 .. 1: rain on the blades. `year`: yearAt.
inline std::string text(const std::string &film, const terrain::Field &field, const Cover &c, Vec3 eye, Vec3 target, double fovDeg, double t, double snow, const dressing::Gusts &gusts,
						const std::vector<std::vector<forest::Tree>> &trees, double wet = 0.0, double year = 0.0, const std::vector<Press> &presses = {}, const std::vector<Trail> &trails = {}) {
	const terrain::Spec &s = field.spec;
	const Growth g = growthOf(s);
	if (!c.on || g.density <= 0.0 || snow > 0.5) return std::string();
	std::string growth;
	{
		char words[400];
		int n = std::snprintf(words, sizeof words, "%.3f|%.3f", g.density, g.tall);
		for (int d = 0; d < kDry; ++d)
			for (int b = 0; b < 2; ++b) {
				const Vec3 col = detail::colourFor(g, d, b);
				n += std::snprintf(words + n, sizeof words - static_cast<std::size_t>(n), "|%.3f,%.3f,%.3f", col.x, col.y, col.z);
			}
		growth = film_cache::nameOf(words).substr(0, 8);
	}
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
	const bool overhead = normalize(target - eye).y < -0.87;   // (looking down from above: what is in view is all round under it, on no one side)
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
				const int salt = static_cast<int>((static_cast<unsigned>(i) * 7919u) ^ (static_cast<unsigned>(k) * 104729u) ^ static_cast<unsigned>(r * 31));   // (unsigned: far out, an int overflowed)
				if (!ringLays(r, away, hash01(salt, 71))) continue;
				const Vec3 to = normalize(Vec3{x - eyeP.x, 0.0, z - eyeP.z});
				if (away > 2.0 * ring.cell && !overhead && dot(to, ahead) < wide) continue;   // (not in view)
				const Vec3 n = field.normalAt(x, z, 0.5 * ring.cell);
				if (n.y < 0.84 || y < above) continue;
				if (s.snowLine() < 1e8 && y - s.y > s.snowLine() * 0.9) continue;
				const double reach = 3.4 + 2.2 * hash01(salt, 75);
				const bool under = !shaded.empty() && underTrees(x, z, reach);
				const bool wooded = !under && !shaded.empty() && underTrees(x, z, reach + 3.2);   // (where the grass gives way to the wood: a thin grass over the leaves, and no line between them)
				bool worn = false;   // (and along a way the animals keep to, thin over bare earth)
				for (const Trail &tr : trails) {
					const double dx = tr.x1 - tr.x0, dz = tr.z1 - tr.z0, along = std::clamp(((x - tr.x0) * dx + (z - tr.z0) * dz) / std::max(1e-9, dx * dx + dz * dz), 0.0, 1.0);
					if (std::hypot(x - tr.x0 - dx * along, z - tr.z0 - dz * along) < tr.wide + 0.25 * ring.cell) worn = true;
				}
				const bool thin = wooded || worn;
				const Vec3 ground = terrain::colourAt(field, x, z);
				const double dryness = ground.x / std::max(1e-4, ground.y), light = ground.x + ground.y + ground.z;
				// the year: each square turns in its own time, drier as it goes on, and the dead grass of winter is beaten down
				const double turned = year + 0.8 * (hash01(salt, 82) - 0.5);
				const int dry = std::clamp((dryness > 1.0 ? 2 : dryness > 0.72 ? 1 : 0) + static_cast<int>(std::lround(turned)), 0, kDry - 1), bright = light > 0.42 || year < -0.3 ? 1 : 0;
				const double height = year > 1.2 ? lerp(1.0, 0.55, smoothstep(1.2, 1.6, year)) : 1.0;
				const int shape = static_cast<int>(hash01(salt, 73) * kShapes) % kShapes;
				const auto laid = [&](const char *what, bool leaves, double share) {   // (one of the square's objects, written once)
					char id[96];   // (its name, and its files': the land's kind of growth too - its blades' height, how many, their green - or an edited biome kept the old grass)
					std::snprintf(id, sizeof id, "cover_%s_%d_%d_%d_%d_%s", what, static_cast<int>(r), shape, dry, bright, growth.c_str());
					if (made.insert(id).second) {
						const std::string name = id;
						const double keep = std::clamp(c.amount, 0.05, 3.0);
						const std::string file = film_cache::ply(film, name + (keep == 1.0 ? "" : "_" + std::to_string(static_cast<int>(keep * 100.0))), [&] {
							return leaves ? detail::litterMesh(ring.cell, static_cast<int>(ring.blades * 0.35 * keep), std::max(0.07, 0.03 * ring.cell), 1 + shape)
										  : detail::patchMesh(ring.cell, static_cast<int>(ring.blades * keep * g.density * share), ring.joints, ring.width, g.tall, 5 + shape + 11 * static_cast<int>(r), r < 2);
						});
						objects += nature::objectBegin(name) + "  AttributeBegin\n  " + detail::bladeMaterial(film, name, detail::colourFor(g, dry, bright), detail::colourFor(g, kDry - 1, bright), leaves, wet) + "\n" +
								   film_cache::plyShape(file) + "  AttributeEnd\n" + nature::objectEnd();
					}
					return std::string(id);
				};
				// tilted to lie on the ground, and leaning with the wind
				const Mat4 tilt = {1, 0, 0, 0, n.x / n.y, 1, -n.z / n.y, 0, 0, 0, 1, 0, 0, 0, 0, 1};
				const Vec3 foot{-x, y - 0.02, z};
				const Mat4 turnIt = rotate({0, 1, 0}, 90.0 * static_cast<int>(hash01(salt, 74) * 4.0));
				const Vec3 w = under ? Vec3{0.0, 0.0, 0.0} : nature::windLean(foot.x, foot.z, t, 0.18) + dressing::gustAt(gusts, foot.x, foot.z, t) * 0.35;
				if (under || wooded) places += nature::placeObject(laid("litter", true, 1.0), mul(translate(foot), tilt, turnIt));
				const Vec3 onGround{-x, y, z};   // (the earth and what lies on it are on the ground: at the blades' roots, 2 cm under it, they were buried)
				const std::string soil = detail::soilKey(ground);
				if (r <= 1 && !under) {   // near the camera: the earth between the blades, what lies on it
					const std::string earth = std::string("cover_earth") + (worn ? "b" : "") + "_" + std::to_string(r) + "_" + std::to_string(shape) + "_" + soil;   // (its soil's colour: the first square's was all of them)
					if (made.insert(earth).second) {
						const std::string file = film_cache::ply(film, "cover_earth_" + std::to_string(r), [&] { return detail::earthSkin(ring.cell); });
						objects += nature::objectBegin(earth) + "  AttributeBegin\n  " + detail::earthMaterial(film, earth, ground, file, worn) + "  AttributeEnd\n" + nature::objectEnd();
					}
					places += nature::placeObject(earth, mul(translate(onGround), tilt, turnIt));
				}
				if (r == 0) {
					const std::string bits = "cover_bits" + std::to_string(shape);
					if (made.insert(bits).second) {
						Mesh stones, stems;
						detail::addLitter(stones, stems, ring.cell, 34, 780 + shape);
						const std::string a = film_cache::ply(film, bits + "_stones", [&] { return stones; }), b = film_cache::ply(film, bits + "_stems", [&] { return stems; });
						objects += nature::objectBegin(bits) + "  AttributeBegin\n  " + diffuseMaterial({0.3, 0.28, 0.25}) + "\n" + film_cache::plyShape(a) + "  AttributeEnd\n  AttributeBegin\n  " + diffuseMaterial({0.32, 0.26, 0.17}) + "\n" +
								   film_cache::plyShape(b) + "  AttributeEnd\n" + nature::objectEnd();
					}
					places += nature::placeObject(bits, mul(translate(onGround), tilt, turnIt));
				}
				// what has walked here: shoved out from under it as it comes, lying down behind it, and getting up again
				Vec3 shoved{0.0, 0.0, 0.0};
				double flat = 0.0;
				if (r < 2)
					for (const Press &p : presses) {
						const double d = std::hypot(x - p.x, z - p.z), reach = p.reach + 0.75 * ring.cell;
						if (d >= reach) continue;
						const double near_ = 1.0 - d / reach, fresh = std::exp(-p.age / 4.0);
						flat = std::max(flat, 0.32 * near_ * near_ * fresh);   // (it lies down: it does not go away)
						if (d > 1e-6) shoved = shoved + Vec3{-(x - p.x) / d, 0.0, (z - p.z) / d} * (1.1 * near_ * fresh);
					}
				const Vec3 lean = w + shoved * (1.0 / std::max(1.0, length(shoved) / 1.1));
				if (!under) places += nature::placeObject(laid(thin ? "thin" : "grass", false, thin ? 0.3 : 1.0), mul(translate(foot), tilt, nature::shearXZ(lean.x, lean.z), turnIt, scale(1.0, height * (1.0 - flat), 1.0)));
				// here and there, close enough to see: a stone half sunk in the ground, and under the trees a fern
				if (ring.cell == 3.0) {
					const auto dressed = [&](const char *what, int kinds, const std::function<std::string(int)> &make, double chance, double sinks, double size) {
						const int own = static_cast<int>(static_cast<unsigned>(salt) * 31u + static_cast<unsigned>(what[0] * 131 + what[1]));
						if (hash01(own, 76) > chance) return;
						const int which = static_cast<int>(hash01(own, 77) * kinds) % kinds;
						const std::string name = std::string("cover_") + what + std::to_string(which) + (std::strcmp(what, "stone") == 0 ? "_" + soil : std::string());   // (a stone takes the ground's colour)
						if (made.insert(name).second) objects += make(which);
						const Vec3 at{-(x + ring.cell * (hash01(own, 78) - 0.5)), y - sinks, z + ring.cell * (hash01(own, 79) - 0.5)};
						places += nature::placeObject(name, mul(translate(at), rotate({0, 1, 0}, 360.0 * hash01(own, 80)), scale(size * (0.6 + 0.9 * hash01(own, 81)))));
					};
					if (under) dressed("fern", 2, [&](int k) {
						const std::string file = film_cache::ply(film, "cover_fern" + std::to_string(k), [&] { return nature::fern(9, 0.9, 700 + k); });
						return nature::objectBegin("cover_fern" + std::to_string(k)) + "  AttributeBegin\n  " + translucentMaterial({0.07, 0.17, 0.04}, {0.10, 0.22, 0.04}) + "\n" + film_cache::plyShape(file) + "  AttributeEnd\n" + nature::objectEnd();
					}, 0.45, 0.02, 1.0);
					if (under) dressed("cushion", 2, [&](int k) {   // (a cushion of moss on the shaded ground)
						const std::string name = "cover_cushion" + std::to_string(k);
						const std::string file = film_cache::ply(film, name, [&] {
							Mesh low = nature::rock(1.0, 740 + k);
							for (Vec3 &p : low.verts) p.y *= 0.3;
							for (Vec3 &q : low.normals) q = normalize(Vec3{q.x, q.y / 0.3, q.z});   // (its normals squashed with it: the steep rock's lit it)
							return moss::topSkin(low, 0.8, 0.0);
						});
						return nature::objectBegin(name) + moss::shape(film, name, file, moss::kTop) + nature::objectEnd();
					}, 0.28, 0.06, 1.0);
					if (!under && year < 0.7 && g.density >= 1.0 && nature::fbm2(x / 34.0, z / 34.0, 83) > 0.53) dressed("bloom", 3, [&](int k) {   // (a meadow's flowers, in drifts)
						static const Vec3 kinds[3] = {{0.86, 0.85, 0.8}, {0.8, 0.62, 0.1}, {0.46, 0.3, 0.7}};
						Mesh stems, petals;
						detail::addFlowers(stems, petals, 26, 1.3, 760 + k);
						const std::string a = film_cache::ply(film, "cover_bloom_stems" + std::to_string(k), [&] { return stems; }), b = film_cache::ply(film, "cover_bloom_petals" + std::to_string(k), [&] { return petals; });
						return nature::objectBegin("cover_bloom" + std::to_string(k)) + "  AttributeBegin\n  " + translucentMaterial({0.08, 0.17, 0.05}, {0.1, 0.2, 0.05}) + "\n" + film_cache::plyShape(a) + "  AttributeEnd\n  AttributeBegin\n  " +
							   translucentMaterial(kinds[k], kinds[k] * 0.35) + "\n" + film_cache::plyShape(b) + "  AttributeEnd\n" + nature::objectEnd();
					}, 0.6, 0.0, 1.0);
					const bool damp = s.biome.empty() || s.biome == "jungle" || s.biome == "tundra";
					dressed("stone", 3, [&](int k) {
						const std::string name = "cover_stone" + std::to_string(k) + "_" + soil;
						const std::string file = film_cache::ply(film, "cover_stone" + std::to_string(k), [&] { return nature::rock(0.5, 720 + k); });
						const std::string top = damp ? film_cache::ply(film, "cover_stone" + std::to_string(k) + "_moss", [&] { return moss::topSkin(nature::rock(0.5, 720 + k), 0.8, 0.004); }) : std::string();
						const Vec3 rock = lerp(detail::soilOf(ground), Vec3{0.3, 0.29, 0.27}, 0.7);
						return nature::objectBegin(name) + "  AttributeBegin\n  " + diffuseMaterial(rock) + "\n" + film_cache::plyShape(file) + "  AttributeEnd\n" + moss::shape(film, name, top, moss::kTop) + nature::objectEnd();
					}, 0.12, 0.08, 1.0);
				}
			}
	}
	return places.empty() ? std::string() : "# what grows on the land (film_cover.h)\n" + objects + places;
}

}  // namespace cover
}  // namespace cartoon
