#pragma once
// film_tideline.h -- what water leaves and what it takes (water round 8): the shore a tide uncovers, and the things on it. The water's own changes are in film_water.h (`tide`, `flood`,
// `whirlpool`, `bigwave`) and film_stream.h (`bore`); here is what they act on:
//
//   the wet band   the land between low and high water, dark and glossy
//   sandcastle     `sandcastle X Z [size S]`: towers, a wall, a flag. As the water comes up to it it slumps, and it is gone when the water has been over it a while.
//   rockpool       `rockpool X Z [radius R]`: a ring of rocks holding its own water, with a starfish in it, seen when the tide is out and drowned when it is in.
//
// Coordinates: pbrt's in a film file; meshes in story space (x mirrored), a ply file's pbrt's.

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "cartoon_lib.h"
#include "film_cache.h"
#include "film_craft.h"
#include "film_stream.h"
#include "film_terrain.h"
#include "film_water.h"

namespace cartoon {
namespace tideline {

struct Castle {
	double x = 0.0, z = 0.0, size = 1.0;
};
struct Pool {
	double x = 0.0, z = 0.0, radius = 1.2;
	int seed = 1;
};

// The land between the water's lowest and highest, wet.
inline std::string wetBandText(const std::string &film, const terrain::Field &g, double low, double high) {
	char words[64];   // (its name: the land's shape too, or an edited terrain kept the old band)
	std::snprintf(words, sizeof words, "|%.4f|%.4f|%d", low, high, g.n);
	const std::string name = film_cache::ply(film, "tide2_band_" + film_cache::nameOf(g.spec.key() + words), [&] {
		Mesh m;
		for (int j = 0; j + 1 < g.n; ++j)
			for (int i = 0; i + 1 < g.n; ++i) {
				const double x = g.x0 + i * g.cell, z = g.z0 + j * g.cell, h = g.heightAt(x + 0.5 * g.cell, z + 0.5 * g.cell);
				if (h < low - 0.2 || h > high + 0.12) continue;
				const int base = static_cast<int>(m.verts.size());
				for (const auto &[a, c] : {std::pair<double, double>{0.0, 0.0}, {1.0, 0.0}, {1.0, 1.0}, {0.0, 1.0}}) {
					const double px = x + a * g.cell, pz = z + c * g.cell;
					m.verts.push_back({-px, g.heightAt(px, pz) + 0.01, pz}), m.normals.push_back({0.0, 1.0, 0.0});
				}
				m.tris.push_back({base, base + 2, base + 1}), m.tris.push_back({base, base + 3, base + 2});
			}
		return m;
	});
	return name.empty() ? std::string() : "# the wet band of the shore (film_tideline.h)\nAttributeBegin\n  " + coated({0.2, 0.17, 0.12}, 0.1) + "\n  Shape \"plymesh\" \"string filename\" [ \"" + name + "\" ]\nAttributeEnd\n";
}

// How far gone a castle is: 0 whole, 1 a low smooth heap. `since`: how long the water has been at it.
inline double slumped(double since) { return since <= 0.0 ? 0.0 : 1.0 - std::exp(-since / 2.5); }
inline std::string castleText(const Castle &c, double groundY, double slump, double t) {
	Mesh sand, flag, pole;
	const double S = c.size, h = 1.0 - 0.92 * slump, w = 1.0 + 0.7 * slump;
	const Mat4 at = mul(translate(-c.x, groundY, c.z), scale(S * w, S * h, S * w));
	const std::vector<std::pair<double, double>> tower = {{0.16, 0.0}, {0.13, 0.26}, {0.15, 0.3}, {0.15, 0.34}, {0.11, 0.34}};
	craft::detail::add(sand, lathe({{0.2, 0.0}, {0.16, 0.42}, {0.19, 0.46}, {0.19, 0.5}, {0.13, 0.5}, {0.0, 0.5}}, 20), at);
	for (int k = 0; k < 4; ++k) {
		const double a = 0.785 + k * 1.5708;
		craft::detail::add(sand, lathe(tower, 16), mul(at, translate(0.42 * std::cos(a), 0.0, 0.42 * std::sin(a))));
		const double b = a + 0.785;
		craft::detail::add(sand, box(0.42, 0.16, 0.08), mul(at, translate(0.31 * std::cos(b), 0.08, 0.31 * std::sin(b)), rotate({0.0, 1.0, 0.0}, -b * 57.3 + 90.0)));
	}
	std::string out = "# a sandcastle (film_tideline.h)\n" + attr(coated({0.72, 0.6, 0.4}, 0.6), emitMesh(sand, identity()));
	if (slump < 0.6) {
		const Vec3 top{-c.x, groundY + 0.5 * S * h, c.z};
		craft::detail::bar(pole, top, top + Vec3{0.0, 0.32 * S, 0.0}, 0.006 * S);
		const Vec3 q = top + Vec3{0.0, 0.3 * S, 0.0};
		const double flap = 0.3 * std::sin(6.0 * t);
		const int base = 0;
		for (const Vec3 &v : {q, q + Vec3{0.16 * S, -0.04 * S, flap * 0.05 * S}, q + Vec3{0.0, -0.09 * S, 0.0}}) flag.verts.push_back(v), flag.normals.push_back({0.0, 0.0, 1.0});
		flag.tris.push_back({base, base + 1, base + 2});
		out += attr(diffuseMaterial({0.3, 0.26, 0.2}), emitMesh(pole, identity())) + attr(diffuseMaterial({0.82, 0.16, 0.12}), emitMesh(flag, identity()));
	}
	return out;
}
inline std::string poolText(const Pool &p, double groundY, double level) {
	if (level > groundY + 0.5) return std::string();   // (drowned: the sea's own surface is over it)
	Mesh rock, sand, star;
	for (int n = 0; n < 16; ++n) {
		const double a = 6.283 * n / 16.0, r = p.radius * (0.92 + 0.2 * hash01(p.seed + n, 1)), size = 0.2 + 0.2 * hash01(p.seed + n, 2);
		craft::detail::add(rock, stream::detail::boulder(size * p.radius, p.seed + n, 12, 7), translate(-(p.x + r * std::cos(a)), groundY + 0.1, p.z + r * std::sin(a)));
	}
	Mesh disc;
	const int spokes = 28;
	const double y = groundY + 0.22 * p.radius;
	disc.verts.push_back({-p.x, y, p.z}), disc.normals.push_back({0.0, 1.0, 0.0});
	for (int i = 0; i <= spokes; ++i) {
		const double a = 6.283 * i / spokes;
		disc.verts.push_back({-(p.x + 1.02 * p.radius * std::cos(a)), y, p.z + 1.02 * p.radius * std::sin(a)}), disc.normals.push_back({0.0, 1.0, 0.0});
		sand.verts.push_back({-(p.x + 1.02 * p.radius * std::cos(a)), y - 0.2 * p.radius, p.z + 1.02 * p.radius * std::sin(a)}), sand.normals.push_back({0.0, 1.0, 0.0});
	}
	sand.verts.push_back({-p.x, y - 0.24 * p.radius, p.z}), sand.normals.push_back({0.0, 1.0, 0.0});
	for (int i = 0; i < spokes; ++i) disc.tris.push_back({0, i + 2, i + 1}), sand.tris.push_back({spokes + 1, i, i + 1});
	const Vec3 at{-p.x + 0.2 * p.radius, y - 0.19 * p.radius, p.z - 0.1 * p.radius};
	for (int a = 0; a < 5; ++a) craft::detail::add(star, capsule(0.03 * p.radius, 0.2 * p.radius, 6, 2), mul(translate(at), rotate({0.0, 1.0, 0.0}, 72.0 * a), rotate({1.0, 0.0, 0.0}, 90.0)));
	return "# a rock pool (film_tideline.h)\n" + attr(coated({0.27, 0.24, 0.2}, 0.3), emitMesh(rock, identity())) + attr(diffuseMaterial({0.55, 0.47, 0.34}), emitMesh(sand, identity())) +
		   attr(diffuseMaterial({0.8, 0.3, 0.12}), emitMesh(star, identity())) + attr(glassMaterial(1.333, 0.0), emitMesh(water::detail::agreeing(disc), identity()));
}

}  // namespace tideline
}  // namespace cartoon
