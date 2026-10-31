#pragma once
// model_detail.h -- modeling, part 7: surface detail in the geometry itself. `displace PATTERN A [size S] [seed K]` moves every point of a part out (or in) along its normal
// by A times a pattern S across - after the mesh is first split finely enough (each edge at most a quarter of S) for the pattern to show:
//
//   noise    soft lumps (fractal value noise): clay, a worn statue, dough
//   rock     ridged, cracked lumps: stone, cliffs, a meteor
//   bumps    round bumps scattered over it: a toad's skin, a pickle, rivets
//   scales   overlapping cells, each raised to its centre: a fish's, a dragon's, a pine cone
//   wood     rings round the upright axis with a little wobble: carved wood, tree bark
//   ribs     even ridges up the part: a gourd, a column's flutes, corduroy
//
// It works the same on every renderer (it is geometry, not a shading trick) and the render time grows only with the triangles it adds. Also here: 3D value noise and each
// vertex's curvature, which the painted textures (model_texture.h) use for worn edges and dirty creases.

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <string>
#include <vector>

#include "cartoon_lib.h"
#include "model_modifiers.h"
#include "sdf_mesh.h"

namespace cartoon {
namespace model {

// ---- noise ----
inline double hash3(int x, int y, int z, int seed) { return hash01(x * 73856093 ^ y * 19349663 ^ z * 83492791, seed); }
// Smooth value noise in [0, 1).
inline double noise3(Vec3 p, int seed) {
	const int ix = static_cast<int>(std::floor(p.x)), iy = static_cast<int>(std::floor(p.y)), iz = static_cast<int>(std::floor(p.z));
	const double fx = p.x - ix, fy = p.y - iy, fz = p.z - iz;
	const double ux = fx * fx * (3 - 2 * fx), uy = fy * fy * (3 - 2 * fy), uz = fz * fz * (3 - 2 * fz);
	const auto c = [&](int a, int b, int d) { return hash3(ix + a, iy + b, iz + d, seed); };
	return lerp(lerp(lerp(c(0, 0, 0), c(1, 0, 0), ux), lerp(c(0, 1, 0), c(1, 1, 0), ux), uy), lerp(lerp(c(0, 0, 1), c(1, 0, 1), ux), lerp(c(0, 1, 1), c(1, 1, 1), ux), uy), uz);
}
inline double fbm3(Vec3 p, int seed, int octaves = 4) {
	double sum = 0.0, amp = 0.5, total = 0.0;
	for (int o = 0; o < octaves; ++o) sum += amp * noise3(p, seed + o), total += amp, p = p * 2.03, amp *= 0.5;
	return sum / total;
}
// The distance to the nearest of the jittered cell points (cellular noise) and that point.
inline double cellular(Vec3 p, int seed, Vec3 *nearest = nullptr) {
	const int ix = static_cast<int>(std::floor(p.x)), iy = static_cast<int>(std::floor(p.y)), iz = static_cast<int>(std::floor(p.z));
	double best = 1e9;
	for (int a = -1; a <= 1; ++a)
		for (int b = -1; b <= 1; ++b)
			for (int c = -1; c <= 1; ++c) {
				const Vec3 q{ix + a + hash3(ix + a, iy + b, iz + c, seed), iy + b + hash3(ix + a, iy + b, iz + c, seed + 1), iz + c + hash3(ix + a, iy + b, iz + c, seed + 2)};
				const double d = length(q - p);
				if (d < best) {
					best = d;
					if (nearest) *nearest = q;
				}
			}
	return best;
}
// The pattern's height at p (about 0 to 1), the pattern `size` across.
inline double patternAt(const std::string &kind, Vec3 p, double size, int seed) {
	const Vec3 q = p * (1.0 / std::max(1e-6, size));
	if (kind == "noise") return fbm3(q, seed);
	if (kind == "rock") return 1.0 - std::abs(2.0 * fbm3(q, seed, 5) - 1.0);
	if (kind == "bumps") return std::pow(std::max(0.0, 1.0 - cellular(q, seed) / 0.45), 2.0);
	if (kind == "scales") {
		Vec3 c;
		const double d = cellular(q + Vec3{0.0, 0.3 * q.y, 0.0}, seed, &c);
		return std::clamp(1.0 - d / 0.7, 0.0, 1.0) * (0.7 + 0.3 * hash3(int(c.x * 7), int(c.y * 7), int(c.z * 7), seed));
	}
	if (kind == "wood") return 0.5 + 0.5 * std::sin(2.0 * kPi * (std::hypot(q.x, q.z) + 0.3 * fbm3(q * 0.5, seed)) * 3.0);
	if (kind == "ribs") return 0.5 + 0.5 * std::cos(std::atan2(q.z, q.x) * std::max(1.0, std::round(1.0 / std::max(0.05, size))));
	return 0.0;
}
inline const std::vector<std::string> &patternKinds() {
	static const std::vector<std::string> k = {"noise", "rock", "bumps", "scales", "wood", "ribs"};
	return k;
}

// ---- refining and displacing ----
// Each triangle split into four (its edges' middles) until no edge is longer than `maxEdge` (at most `maxTris` triangles).
inline Mesh refined(const Mesh &in, double maxEdge, std::size_t maxTris = 400000) {
	Mesh m = in;
	m.uvs.clear();
	for (int round = 0; round < 8 && m.tris.size() * 4 <= maxTris; ++round) {
		double longest = 0.0;
		for (const auto &t : m.tris)
			for (int q = 0; q < 3; ++q) longest = std::max(longest, length(m.verts[t[q]] - m.verts[t[(q + 1) % 3]]));
		if (longest <= maxEdge) break;
		std::map<std::pair<int, int>, int> mid;
		const auto middle = [&](int a, int b) {
			const std::pair<int, int> key{std::min(a, b), std::max(a, b)};
			auto it = mid.find(key);
			if (it != mid.end()) return it->second;
			m.verts.push_back((m.verts[a] + m.verts[b]) * 0.5);
			m.normals.push_back(normalize(m.normals[a] + m.normals[b]));
			return mid[key] = static_cast<int>(m.verts.size()) - 1;
		};
		std::vector<std::array<int, 3>> next;
		next.reserve(m.tris.size() * 4);
		for (const auto &t : m.tris) {
			const int ab = middle(t[0], t[1]), bc = middle(t[1], t[2]), ca = middle(t[2], t[0]);
			next.push_back({t[0], ab, ca}), next.push_back({ab, t[1], bc}), next.push_back({ca, bc, t[2]}), next.push_back({ab, bc, ca});
		}
		m.tris = next;
	}
	return m;
}
struct Displace {
	std::string kind = "noise";
	double amount = 0.01, size = 0.1;
	int seed = 1;
};
inline Mesh displaced(const Mesh &in, const Displace &d) {
	Mesh m = in;
	if (m.normals.size() != m.verts.size()) sdf::computeNormals(m);
	m = refined(m, 0.25 * d.size);
	sdf::computeNormals(m);
	for (std::size_t i = 0; i < m.verts.size(); ++i) m.verts[i] = m.verts[i] + m.normals[i] * (d.amount * (patternAt(d.kind, m.verts[i], d.size, d.seed) - 0.5));
	sdf::computeNormals(m);
	return m;
}

// ---- curvature ----
// Each vertex's curvature: how far its neighbours' middle lies off its tangent plane, over the length of its edges - above 0 where the surface bulges out (an edge, a
// knuckle), below 0 in a crease or a hollow. About -1 to 1 for strong features.
inline std::vector<double> curvatureOf(const Mesh &m) {
	std::vector<double> k(m.verts.size(), 0.0);
	if (m.normals.size() != m.verts.size()) return k;
	const std::vector<std::vector<int>> nb = neighboursOf(m);
	for (std::size_t i = 0; i < m.verts.size(); ++i) {
		if (nb[i].empty()) continue;
		Vec3 c{0, 0, 0};
		double len = 0.0;
		for (int j : nb[i]) c = c + m.verts[j], len += length(m.verts[j] - m.verts[i]);
		c = c * (1.0 / nb[i].size());
		len /= nb[i].size();
		k[i] = -dot(m.normals[i], c - m.verts[i]) / std::max(1e-9, len) * 4.0;
	}
	return k;
}

}  // namespace model
}  // namespace cartoon
