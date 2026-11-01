#pragma once
// film_alien.h -- what stands on another world (film_file.h `plumes`, `spires`; `biome rust|frost|glass|moonrock|bloom` is the land's colours, film_terrain.h):
//
//   plumes   vents in the ice or the rust that blow in their own time, each a column of soft cloud going up and spreading, and falling back as it thins (film_air.h's puffs: no volume);
//   spires   a field of crystal: six-sided shards standing out of the ground at their own angles, of clear glass or a coloured stone, a few object placements however many.
//
// Coordinates: pbrt's for the film file, story space (x mirrored) for the placements.

#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <vector>

#include "cartoon_lib.h"
#include "film_air.h"
#include "film_cache.h"
#include "film_terrain.h"
#include "nature_lib.h"

namespace cartoon {
namespace alien {

struct Plumes {
	int count = 6;
	double x0 = -60.0, z0 = -60.0, x1 = 60.0, z1 = 60.0, height = 24.0;
	Vec3 colour{0.84, 0.88, 0.92};
	int seed = 1;
};
struct Spires {
	int count = 80;
	double x0 = -60.0, z0 = -60.0, x1 = 60.0, z1 = 60.0, height = 6.0;
	Vec3 colour{0.0, 0.0, 0.0};   // (a stone of this colour; all 0: clear glass)
	int seed = 1;
};

// The ground at a place (0 with no land).
inline double groundAt(const terrain::Field *land, double x, double z) { return land ? land->heightAt(x, z) : 0.0; }

// The vents' columns at t, seen from `eye`, as scene text.
inline std::string plumesText(const std::string &film, const Plumes &g, const terrain::Field *land, double t, const Vec3 &eye, const std::string &name) {
	std::vector<airborne::Puff> puffs;
	for (int v = 0; v < g.count; ++v) {
		const int s = g.seed * 131 + v;
		const double x = g.x0 + (g.x1 - g.x0) * hash01(s, 1), z = g.z0 + (g.z1 - g.z0) * hash01(s, 2), y = groundAt(land, x, z);
		const double cycle = 9.0 + 7.0 * hash01(s, 3), u = t / cycle + hash01(s, 4), phase = (u - std::floor(u)) * cycle;   // (it blows for some seconds, and rests)

		const double high = g.height * (0.7 + 0.6 * hash01(s, 5));
		for (int k = 0; k < 22; ++k) {
			const double age = 0.14 * k + 0.09 * hash01(s + k, 6), at = phase - age;   // (each puff went up when the vent was as it was `age` ago)
			if (at < 0.0 || at > 5.5) continue;
			const double strength = smoothstep(0.0, 0.5, at) * (1.0 - smoothstep(3.5, 5.5, at));
			const double up = high * (1.0 - std::pow(1.0 - std::min(1.0, age / 2.6), 2.0)) * (0.5 + 0.5 * strength), spread = 0.05 * high * (0.3 + age);
			const Vec3 p{x + spread * (hash01(s + k, 7) - 0.5) * 2.0 + 0.6 * age * age, y + up, z + spread * (hash01(s + k, 8) - 0.5) * 2.0};
			puffs.push_back({p, 0.05 * high + 0.1 * high * age, 0.5 * strength * (1.0 - std::min(1.0, age / 3.1))});
		}

	}
	return puffs.empty() ? std::string() : "# plumes (film_alien.h)\n" + airborne::puffsText(film, puffs, g.colour, eye, name);
}

namespace detail {
// A shard: six sides, tapering a little, cut to a point, 1 tall and leaning by what places it.
inline Mesh shard(int salt) {
	const double wide = 0.09 + 0.05 * hash01(salt, 1), shoulder = 0.78 + 0.14 * hash01(salt, 2);
	Mesh m = lathe({{wide * 1.15, -0.1}, {wide, shoulder}, {0.0, 1.0}}, 6);
	return m;
}
}  // namespace detail
inline std::string spiresText(const std::string &film, const Spires &f, const terrain::Field *land) {
	std::string objects, places;
	std::set<int> made;
	const std::string base = "spire_" + std::to_string(f.seed);
	const bool clear = f.colour.x + f.colour.y + f.colour.z <= 0.0;
	for (int k = 0; k < f.count; ++k) {
		const int s = f.seed * 211 + k, which = static_cast<int>(hash01(s, 1) * 3.0) % 3;
		if (made.insert(which).second) {
			const std::string file = film_cache::ply(film, "spire_shard" + std::to_string(which), [&] { return detail::shard(which + 7); });
			objects += nature::objectBegin(base + std::to_string(which)) + "  AttributeBegin\n  " + (clear ? std::string("Material \"dielectric\" \"float eta\" [ 1.52 ]") : coated(f.colour, 0.08)) + "\n" + film_cache::plyShape(file) + "  AttributeEnd\n" + nature::objectEnd();
		}
		// (they stand in clusters, the big ones among the small, leaning out from their cluster's middle)
		const int cluster = static_cast<int>(hash01(s, 2) * std::max(4, f.count / 9));
		const double cx = f.x0 + (f.x1 - f.x0) * hash01(cluster + f.seed * 7, 3), cz = f.z0 + (f.z1 - f.z0) * hash01(cluster + f.seed * 7, 4);
		const double a = 2.0 * kPi * hash01(s, 5), r = 0.25 * f.height * std::sqrt(hash01(s, 6)), x = cx + r * std::cos(a), z = cz + r * std::sin(a);
		const double high = f.height * (0.25 + 1.0 * std::pow(hash01(s, 7), 2.2));
		const Vec3 foot{-x, groundAt(land, x, z) - 0.03 * high, z};
		const Mat4 lean = nature::shearXZ(-0.5 * std::cos(a) * r / std::max(0.1, f.height), 0.5 * std::sin(a) * r / std::max(0.1, f.height));
		places += nature::placeObject(base + std::to_string(which), mul(translate(foot), lean, rotate({0, 1, 0}, 360.0 * hash01(s, 8)), scale(high * (0.75 + 0.5 * hash01(s, 9)), high, high * (0.75 + 0.5 * hash01(s, 9)))));
	}
	return places.empty() ? std::string() : "# spires (film_alien.h)\n" + objects + places;
}

}  // namespace alien
}  // namespace cartoon
