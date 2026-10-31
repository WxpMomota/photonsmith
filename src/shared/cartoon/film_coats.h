#pragma once
// film_coats.h -- the patterns of real animals' coats (round 13: `herd KIND ... coat PATTERN`), grown as nature grows them and painted on the rigged animals of
// film_animals.h through their coat's picture (scripts/make_rigged_animals.py gives every body tube a place in it: along the tube one way, round it the other, the seam
// under the belly).
//
//   zebra     stripes from a reaction-diffusion (Gray-Scott, Turing's mechanism: an activator and a faster-spreading inhibitor), spreading faster round the body than
//             along it, so the stripes ring the body, the neck and the legs as a zebra's do
//   tiger     the same stripes, thinner and broken, black on orange, a white belly
//   cheetah   solid black spots (the same model where it settles into spots)
//   leopard   rosettes: rings of dark broken round the spots' edges, a darker gold inside
//   giraffe   patches: the cells of a jittered Voronoi tiling, brown, parted by a network of cream lines
//   shade     only countershading: a paler belly under a darker back, as almost every mammal has
//
// Every pattern also pales the belly. Each species gets three variants (another seed each), and a herd's animals take them in turn, so no two neighbours are the same.
// A picture is 384 pixels square, made once per pattern, kind and variant into the film cache (`_coats`); the pattern fields are worked out once per pattern and variant.

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "cartoon_lib.h"
#include "film_sky.h"

namespace cartoon {
namespace coats {

inline const std::vector<std::string> &patterns() {
	static const std::vector<std::string> p = {"zebra", "tiger", "cheetah", "leopard", "giraffe", "shade", "plain"};
	return p;
}
// A kind's own pattern when its herd line names none ("": the model's flat colours, as before).
inline std::string defaultFor(const std::string &kind) {
	if (kind == "zebras") return "zebra";
	if (kind == "giraffes") return "giraffe";
	if (kind == "lions" || kind == "bears" || kind == "elephants" || kind == "foxes") return "shade";
	return "";
}

constexpr int kSize = 384;

namespace detail {
// Gray-Scott on a torus: f, k its feed and kill rates, wx and wy how fast it spreads along x and y. Returns the inhibited chemical B (0 .. about 0.4).
inline std::vector<float> grayScott(double f, double k, double wx, double wy, int seed, int steps = 7000) {
	const int n = kSize;
	std::vector<float> A(static_cast<std::size_t>(n) * n, 1.0f), B(A.size(), 0.0f), A2(A.size()), B2(A.size());
	for (int s = 0; s < n * n / 120; ++s) {   // (seeds: small squares of B)
		const int x = static_cast<int>(hash01(s, seed * 7 + 1) * n), y = static_cast<int>(hash01(s, seed * 7 + 2) * n);
		for (int dy = -2; dy < 2; ++dy)
			for (int dx = -2; dx < 2; ++dx) {
				const std::size_t i = static_cast<std::size_t>(((y + dy + n) % n) * n + (x + dx + n) % n);
				A[i] = 0.5f, B[i] = 0.25f;
			}
	}
	const float Da = 0.2097f, Db = 0.105f, fx = static_cast<float>(wx), fy = static_cast<float>(wy), F = static_cast<float>(f), K = static_cast<float>(k);
	for (int it = 0; it < steps; ++it) {
		for (int y = 0; y < n; ++y) {
			const int yu = (y + n - 1) % n * n, yd = (y + 1) % n * n, yc = y * n;
			for (int x = 0; x < n; ++x) {
				const int xl = (x + n - 1) % n, xr = (x + 1) % n;
				const std::size_t c = static_cast<std::size_t>(yc + x);
				const float a = A[c], b = B[c];
				const float la = fx * (A[static_cast<std::size_t>(yc + xl)] + A[static_cast<std::size_t>(yc + xr)] - 2.0f * a) + fy * (A[static_cast<std::size_t>(yu + x)] + A[static_cast<std::size_t>(yd + x)] - 2.0f * a);
				const float lb = fx * (B[static_cast<std::size_t>(yc + xl)] + B[static_cast<std::size_t>(yc + xr)] - 2.0f * b) + fy * (B[static_cast<std::size_t>(yu + x)] + B[static_cast<std::size_t>(yd + x)] - 2.0f * b);
				const float abb = a * b * b;
				A2[c] = a + Da * la - abb + F * (1.0f - a);
				B2[c] = b + Db * lb + abb - (K + F) * b;
			}
		}
		A.swap(A2), B.swap(B2);
	}
	return B;
}
// A jittered Voronoi tiling's edge distance at (x, y) (pixels, wrapping): F2 - F1, and the nearest cell's number.
inline double voronoiEdge(double x, double y, double cell, int seed, int &id) {
	const int n = static_cast<int>(std::round(kSize / cell));
	const int cx = static_cast<int>(std::floor(x / cell)), cy = static_cast<int>(std::floor(y / cell));
	double f1 = 1e30, f2 = 1e30;
	for (int dy = -2; dy <= 2; ++dy)
		for (int dx = -2; dx <= 2; ++dx) {
			const int gx = cx + dx, gy = cy + dy, wx = ((gx % n) + n) % n, wy = ((gy % n) + n) % n;
			const int h = wy * n + wx;
			const double px = (gx + 0.15 + 0.7 * hash01(h, seed * 13 + 3)) * cell, py = (gy + 0.15 + 0.7 * hash01(h, seed * 13 + 4)) * cell;
			const double d = std::hypot(x - px, (y - py) * 1.15);
			if (d < f1) f2 = f1, f1 = d, id = h;
			else if (d < f2) f2 = d;
		}
	return f2 - f1;
}
// How dark the pattern is at each pixel, 0 .. 1 (worked out once per pattern and variant).
inline const std::vector<float> &field(const std::string &pattern, int variant) {
	static std::mutex lock;
	static std::map<std::string, std::vector<float>> made;
	const std::lock_guard<std::mutex> hold(lock);
	const std::string key = pattern + "#" + std::to_string(variant);
	if (const auto it = made.find(key); it != made.end()) return it->second;
	std::vector<float> out(static_cast<std::size_t>(kSize) * kSize, 0.0f);
	const int seed = 11 + variant * 17;
	if (pattern == "zebra" || pattern == "tiger") {   // (stripes that spread faster round the body than along it: they ring it)
		const std::vector<float> B = grayScott(0.026, 0.055, 0.3, 1.7, seed);
		const double lo = pattern == "tiger" ? 0.22 : 0.12, hi = pattern == "tiger" ? 0.30 : 0.2;
		for (std::size_t i = 0; i < out.size(); ++i) out[i] = static_cast<float>(smoothstep(lo, hi, B[i]));
	} else if (pattern == "cheetah" || pattern == "leopard") {
		const std::vector<float> B = grayScott(0.035, 0.065, 1.0, 1.0, seed);
		for (std::size_t i = 0; i < out.size(); ++i) {
			const double b = B[i];
			if (pattern == "cheetah") out[i] = static_cast<float>(smoothstep(0.14, 0.22, b));
			else {   // a rosette: dark at the spot's rim, broken here and there, a little darker gold within
				const int x = static_cast<int>(i % kSize), y = static_cast<int>(i / kSize);
				const double rim = smoothstep(0.06, 0.12, b) * (1.0 - smoothstep(0.17, 0.24, b)), broken = 0.55 + 0.45 * std::sin(0.31 * x + 0.23 * y + 3.0 * hash01(x / 9 + y / 9 * 97, seed));
				out[i] = static_cast<float>(std::max(rim * std::clamp(broken * 1.4, 0.0, 1.0), 0.35 * smoothstep(0.2, 0.26, b)));
			}
		}
	} else if (pattern == "giraffe") {
		for (int y = 0; y < kSize; ++y)
			for (int x = 0; x < kSize; ++x) {
				int id = 0;
				const double e = voronoiEdge(x + 0.5, y + 0.5, 26.0, seed, id);
				out[static_cast<std::size_t>(y * kSize + x)] = static_cast<float>(smoothstep(2.0, 4.5, e) * (0.85 + 0.15 * hash01(id, seed + 5)));
			}
	}
	return made[key] = out;
}
// The part of the coat's picture a point is in (scripts/make_rigged_animals.py's rects), and how far round its tube it is (0 and 1: under it).
inline double roundness(double x, double y) {
	if (y >= 0.5) {
		const double leg = std::floor((y - 0.5) / 0.125);
		return (y - 0.5 - 0.125 * leg) / 0.125;
	}
	if (x < 0.75) return y / 0.5;              // (the barrel, the neck)
	return y < 0.25 ? y / 0.25 : (y - 0.25) / 0.25;   // (the head; the tail or a trunk)
}
}  // namespace detail

// The coat's colour at a point (ax, ay) of its picture (0..1, row 0 at the top), in a pattern and variant, from its model's coat and leg colours: the picture's pixels,
// and a hero's strands of fur (film_fur.h) take theirs from it too.
inline Vec3 colourAt(const std::string &pattern, int variant, Vec3 coat, Vec3 legs, double ax, double ay) {
	Vec3 base = coat, ink{0.03, 0.025, 0.02}, belly{0.92, 0.88, 0.80};
	if (pattern == "tiger") base = {0.86, 0.45, 0.12}, belly = {0.95, 0.93, 0.88};
	if (pattern == "cheetah" || pattern == "leopard") base = {0.80, 0.55, 0.22}, belly = {0.93, 0.88, 0.76};
	if (pattern == "leopard") ink = {0.10, 0.06, 0.03};
	if (pattern == "giraffe") base = {0.93, 0.85, 0.68}, ink = {0.52, 0.30, 0.12};
	const Vec3 b = ay >= 0.5 && pattern != "tiger" && pattern != "cheetah" && pattern != "leopard" && pattern != "giraffe" ? legs : base;
	const double under = smoothstep(0.35, 0.95, std::cos(2.0 * kPi * detail::roundness(ax, ay)));   // (1 under the body)
	const std::vector<float> &dark = detail::field(pattern, variant);
	const int x = std::clamp(static_cast<int>(ax * kSize), 0, kSize - 1), y = std::clamp(static_cast<int>(ay * kSize), 0, kSize - 1);
	const double d = dark[static_cast<std::size_t>(y * kSize + x)] * (pattern == "zebra" ? 1.0 : 1.0 - 0.6 * under);
	const Vec3 c = lerp(b, belly, 0.55 * under * (pattern == "zebra" ? 0.3 : 1.0));
	return lerp(c, ink, d);
}

// The coat's picture of a kind in a pattern and variant (its scene path), from its model's coat and leg colours.
inline std::string picture(const std::string &pattern, const std::string &kind, int variant, Vec3 coat, Vec3 legs) {
	char name[160];
	std::snprintf(name, sizeof name, "coat_v2_%s_%s_%d_%.3f_%.3f_%.3f", pattern.c_str(), kind.c_str(), variant, coat.x, coat.y, coat.z);
	return film_sky::picture("_coats", name, kSize, kSize, [&](std::vector<float> &rgb) {
		for (int y = 0; y < kSize; ++y)
			for (int x = 0; x < kSize; ++x) {
				const Vec3 c = colourAt(pattern, variant, coat, legs, (x + 0.5) / kSize, (y + 0.5) / kSize);
				const std::size_t i = static_cast<std::size_t>(y * kSize + x) * 3;
				rgb[i] = static_cast<float>(c.x), rgb[i + 1] = static_cast<float>(c.y), rgb[i + 2] = static_cast<float>(c.z);
			}
	});
}

}  // namespace coats
}  // namespace cartoon
