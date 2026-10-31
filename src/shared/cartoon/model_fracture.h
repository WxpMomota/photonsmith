#pragma once
// model_fracture.h -- modeling round 2, part 10: things that break. `shatter ID at T [pieces N] [force F] [seed K]` breaks the model placed as ID (`place MODEL at ... as ID`)
// into N chunks at T (2 to 60, 12): they burst out from its middle at about F metres a second (3), tumbling, fall, bounce on the ground and come to rest, in a puff of
// dust; the break is heard. The chunks are a Voronoi fracture: every piece of the model as a signed distance field (model_boolean.h) cut by the cells round N points
// scattered inside it, each cell meshed with sharp edges (its broken faces flat, as a break's are) and kept in the film's cache. Each chunk keeps its piece's material.

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "film_cache.h"
#include "model_boolean.h"
#include "model_part.h"

namespace cartoon {
namespace model {

struct Shatter {
	std::string id;
	double t = 0.0, force = 3.0;
	int pieces = 12, seed = 1;
	int line = 0;
};
struct Chunk {
	const Part *part = nullptr;
	std::string file;    // (its PLY in the cache)
	Vec3 centre;         // (the model's own space)
	double radius = 0.1;
};

// The points the cells are round: scattered through the box, kept where the model is (its field below 0), at most `n`.
inline std::vector<Vec3> fractureSeeds(const std::vector<MeshField> &fields, Vec3 lo, Vec3 hi, int n, int seed) {
	std::vector<Vec3> out;
	for (int k = 0; k < 40 * n && static_cast<int>(out.size()) < n; ++k) {
		const Vec3 p{lerp(lo.x, hi.x, hash01(seed * 977 + k, 1)), lerp(lo.y, hi.y, hash01(seed * 977 + k, 2)), lerp(lo.z, hi.z, hash01(seed * 977 + k, 3))};
		for (const MeshField &f : fields)
			if (f(p) < 0.0) {
				out.push_back(p);
				break;
			}
	}
	return out;
}
// The chunks of a model broken by `s` (made once a model and break, in the cache). `pieceMeshOf` gives each piece's mesh at level 1.
template <typename PieceMesh>
inline std::vector<Chunk> chunksOf(const std::string &film, const std::string &modelName, const std::vector<Part> &parts, const Shatter &s, const PieceMesh &pieceMeshOf) {
	std::vector<const Part *> drawn;
	std::vector<Mesh> meshes;
	Vec3 lo{1e30, 1e30, 1e30}, hi{-1e30, -1e30, -1e30};
	std::string key = modelName + "|" + std::to_string(s.pieces) + "|" + std::to_string(s.seed);
	for (const Part &p : parts) {
		if (p.tool) continue;
		drawn.push_back(&p);
		meshes.push_back(pieceMeshOf(p));
		Vec3 a, b;
		boxOf(meshes.back(), a, b);
		lo = {std::min(lo.x, a.x), std::min(lo.y, a.y), std::min(lo.z, a.z)}, hi = {std::max(hi.x, b.x), std::max(hi.y, b.y), std::max(hi.z, b.z)};
		key += "|" + hashOf(keyOf(p));
	}
	const double size = std::max({hi.x - lo.x, hi.y - lo.y, hi.z - lo.z, 1e-3}), cell = size / 70.0;
	std::vector<MeshField> fields;
	for (const Mesh &m : meshes) fields.push_back(meshField(m, cell));
	const std::vector<Vec3> seeds = fractureSeeds(fields, lo, hi, std::clamp(s.pieces, 2, 60), s.seed);
	std::vector<Chunk> out;
	const std::string tag = hashOf(key).substr(0, 12);
	for (std::size_t q = 0; q < drawn.size(); ++q)
		for (std::size_t i = 0; i < seeds.size(); ++i) {
			const auto cellField = [&](Vec3 p) {   // (the Voronoi cell round seed i: inside every bisecting plane towards the others)
				double d = -1e9;
				for (std::size_t j = 0; j < seeds.size(); ++j) {
					if (j == i) continue;
					const Vec3 n = normalize(seeds[j] - seeds[i]);
					d = std::max(d, dot(p - (seeds[i] + seeds[j]) * 0.5, n));
				}
				return d;
			};
			Chunk c;
			c.part = drawn[q];
			const std::string name = "shard_" + modelName + "_" + tag + "_" + std::to_string(q) + "_" + std::to_string(i);
			Mesh made;
			c.file = film_cache::ply(film, name, [&]() {
				made = dualContour([&](Vec3 p) { return std::max(fields[q](p), cellField(p)); }, lo - Vec3{2 * cell, 2 * cell, 2 * cell}, hi + Vec3{2 * cell, 2 * cell, 2 * cell}, cell);
				return autoSmooth(made, 30.0);
			});
			if (c.file.empty()) continue;   // (the cell misses this piece)
			c.centre = seeds[i];
			c.radius = 0.5 * size / std::cbrt(static_cast<double>(seeds.size()));
			out.push_back(c);
		}
	return out;
}
// Where chunk `k` of `n` is `since` seconds after the break, as a transform of the model's own space: burst out from the model's middle `mid`, tumbling, falling to the
// ground (y 0 of the model's space), bouncing and sliding to rest.
inline Mat4 chunkAt(const Chunk &c, int k, const Vec3 &mid, const Shatter &s, double since) {
	if (since <= 0.0) return identity();
	const int salt = s.seed * 131 + k;
	Vec3 dir = c.centre - mid + Vec3{0.0, 0.35 * std::max(0.05, length(c.centre - mid)), 0.0};
	dir = length(dir) > 1e-9 ? normalize(dir) : Vec3{0, 1, 0};
	Vec3 v = dir * (s.force * (0.6 + 0.8 * hash01(salt, 1)));
	const Vec3 axis = normalize(Vec3{hash01(salt, 2) - 0.5, hash01(salt, 3) - 0.5, hash01(salt, 4) - 0.5} + Vec3{1e-6, 0, 0});
	double spin = (90.0 + 450.0 * hash01(salt, 5)) * (hash01(salt, 6) < 0.5 ? -1.0 : 1.0), angle = 0.0;
	Vec3 p = c.centre;
	const double dt = 1.0 / 120.0, g = 9.81, floorY = c.radius * 0.6;
	for (double t = 0.0; t < std::min(since, 12.0); t += dt) {
		v.y -= g * dt;
		p = p + v * dt;
		angle += spin * dt;
		if (p.y < floorY) {   // the ground: a bounce that loses most of its speed, the slide slowed, the tumbling damped
			p.y = floorY;
			if (v.y < 0.0) v.y = -0.3 * v.y;
			v.x *= 0.7, v.z *= 0.7, spin *= 0.6;
			if (std::abs(v.y) < 0.3) v.y = 0.0;
		}
	}
	return mul(translate(p), rotate(axis, angle), translate(c.centre * -1.0));
}

}  // namespace model
}  // namespace cartoon
