#pragma once
// film_moss.h -- what grows where wood and stone stay damp (film_forest.h's trees and fallen logs, film_cover.h's stones and the ground under the trees).
//
//   on a trunk  a thin skin over the bottom of the wood, close enough to it to hide nothing, cut by a picture of where moss has taken hold: thickest low down, on the side away from
//               the sun, and thinning upwards in patches. A tree stands turned any way, so there are four pictures, one for each side the moss can be on, and each tree takes the one
//               that puts it on the same side of the sky as its neighbours'.
//   on a log    or a stone: the same skin over what faces up, where the rain lies.
//
// A skin is a few hundred triangles and a placement of its own, so a wood of thousands of trees costs little.
//
// Coordinates: story space, as the meshes it is given.

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "cartoon_lib.h"
#include "film_cache.h"
#include "film_sky.h"
#include "nature_lib.h"

namespace cartoon {
namespace moss {

constexpr int kSides = 4;      // (the pictures of a trunk)
constexpr int kTop = kSides;   // (and the one of what faces up)
constexpr double kReach = 4.5;   // (how far up a trunk, metres)

// How a mesh's triangle goes into a skin: raised off the surface by `lift`, with uv from `at` (each corner's own copy, as a skin has seams). `around`: u is an angle round, and wraps.
template <class Uv>
inline Mesh skinOf(const Mesh &m, double lift, const std::function<bool(const Vec3 &, const Vec3 &, const Vec3 &, const Vec3 &)> &wanted, const Uv &at, bool around) {
	Mesh out;
	if (m.normals.size() != m.verts.size()) return out;
	for (const auto &t : m.tris) {
		const Vec3 &a = m.verts[static_cast<std::size_t>(t[0])], &b = m.verts[static_cast<std::size_t>(t[1])], &c = m.verts[static_cast<std::size_t>(t[2])];
		const Vec3 face = cross(b - a, c - a);
		if (length(face) < 1e-12 || !wanted(a, b, c, normalize(face))) continue;
		std::array<std::pair<double, double>, 3> uv = {at(a), at(b), at(c)};
		// (a triangle across the seam of the angle round the trunk: its corners on the same side of it. Not on a flat skin: a long log's triangle spans more than half a repeat)
		const double top = std::max({uv[0].first, uv[1].first, uv[2].first});
		for (auto &q : uv)
			if (around && top - q.first > 0.5) q.first += 1.0;
		const int base = static_cast<int>(out.verts.size());
		for (int k = 0; k < 3; ++k) {
			const std::size_t i = static_cast<std::size_t>(t[k]);
			out.verts.push_back(m.verts[i] + m.normals[i] * lift);
			out.normals.push_back(m.normals[i]);
			out.uvs.push_back(uv[static_cast<std::size_t>(k)]);
		}
		out.tris.push_back({base, base + 1, base + 2});
	}
	return out;
}
// The skin of the bottom of a trunk (and its roots): u the angle round it, v the height.
inline Mesh trunkSkin(const Mesh &wood) {
	return skinOf(wood, 0.007, [](const Vec3 &a, const Vec3 &b, const Vec3 &c, const Vec3 &) { return std::max({a.y, b.y, c.y}) < kReach; },
				  [](const Vec3 &p) { return std::make_pair(std::atan2(p.z, p.x) / (2.0 * kPi) + 0.5, std::clamp(p.y / kReach, 0.0, 1.0)); }, true);
}
// The skin of what faces up: uv across the ground, one repeat of the picture to `size` metres.
inline Mesh topSkin(const Mesh &m, double size, double lift) {
	return skinOf(m, lift, [](const Vec3 &, const Vec3 &, const Vec3 &, const Vec3 &n) { return n.y > 0.25; }, [size](const Vec3 &p) { return std::make_pair(p.x / size, p.z / size); }, false);
}

namespace detail {
// Where it has taken hold on a trunk, 0 .. 1, at (u round it, v up it), with the damp side at `side` of kSides round it; on what faces up, all over, in patches. Every term goes round
// a whole number of times, so the picture has no seam.
inline std::string coverPicture(const std::string &film, int side) {
	constexpr int W = 384, H = 384;
	return film_sky::picture(film, "moss3_cover" + std::to_string(side), W, H, [&](std::vector<float> &rgb) {
		for (int y = 0; y < H; ++y)
			for (int x = 0; x < W; ++x) {
				const double u = (x + 0.5) / W, v = 1.0 - (y + 0.5) / H, a = 2.0 * kPi * u;
				const double n = nature::fbm2(2.6 * std::cos(a) + 2.1 * v * (side == kTop ? 0.0 : 1.0) + 5.0, 2.6 * std::sin(a) + 11.0 * v, 47);
				double want = 0.75;
				if (side != kTop) {
					const double facing = std::cos(a - kPi - 2.0 * kPi * side / kSides);   // (u is the angle round the trunk plus half a turn: trunkSkin)
					want = 0.3 + 0.42 * facing + 0.5 * (1.0 - v) * (1.0 - v) - 0.8 * smoothstep(0.6, 1.0, v);
				}
				const double line = lerp(0.72, 0.36, std::clamp(want, 0.0, 1.0));
				const float keep = static_cast<float>(smoothstep(line - 0.02, line + 0.02, n));
				float *px = &rgb[(static_cast<std::size_t>(y) * W + x) * 3];
				px[0] = px[1] = px[2] = keep;
			}
	});
}
// Its colour: cushions of fine green, yellower where they catch the light, dark between them.
inline std::string colourPicture(const std::string &film) {
	constexpr int W = 256;
	return film_sky::picture(film, "moss1_colour", W, W, [&](std::vector<float> &rgb) {
		for (int y = 0; y < W; ++y)
			for (int x = 0; x < W; ++x) {
				const double fine = nature::fbm2(x * 0.19, y * 0.19, 48), big = nature::noise2(x * 0.024, y * 0.024, 49);
				const Vec3 c = lerp(lerp(Vec3{0.05, 0.09, 0.025}, Vec3{0.13, 0.21, 0.05}, smoothstep(0.32, 0.55, fine)), Vec3{0.26, 0.31, 0.08}, smoothstep(0.55, 0.72, fine) * (0.4 + 0.6 * big));
				float *px = &rgb[(static_cast<std::size_t>(y) * W + x) * 3];
				px[0] = static_cast<float>(c.x), px[1] = static_cast<float>(c.y), px[2] = static_cast<float>(c.z);
			}
	});
}
}  // namespace detail

// A shape of a skin's file, cut by the picture of `side`, under a material of its own (`name` a name for its textures). Goes inside an object.
inline std::string shape(const std::string &film, const std::string &name, const std::string &file, int side) {
	if (file.empty()) return std::string();
	std::string line = film_cache::plyShape(file);
	line.pop_back();
	return "  AttributeBegin\n  Texture \"" + name + "_cover\" \"float\" \"imagemap\" \"string filename\" [ \"" + detail::coverPicture(film, side) + "\" ]\n  Texture \"" + name +
		   "_colour\" \"spectrum\" \"imagemap\" \"string filename\" [ \"" + detail::colourPicture(film) + "\" ]\n  Material \"diffuse\" \"texture reflectance\" \"" + name + "_colour\"\n" + line +
		   " \"texture alpha\" \"" + name + "_cover\"\n  AttributeEnd\n";
}
// Which of the kSides pictures puts the moss on the side `damp` (a direction across the ground, story space) of a tree turned by `place` (its rotation alone).
inline int sideFor(const Mat4 &place, const Vec3 &damp) {
	int best = 0;
	double most = -2.0;
	for (int k = 0; k < kSides; ++k) {
		const double a = 2.0 * kPi * k / kSides;
		if (const double d = dot(transformPoint(place, Vec3{std::cos(a), 0.0, std::sin(a)}), damp); d > most) most = d, best = k;
	}
	return best;
}

}  // namespace moss
}  // namespace cartoon
