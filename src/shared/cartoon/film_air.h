#pragma once
// film_air.h -- what hangs in the air of a film's land, and the soft clouds of a moving herd (film_wildlife.h).
//
//   puffs    a cloud is a camera-facing square with a soft round picture for its alpha: a hit is kept with the picture's chance (pbrt's stochastic alpha), so it thins out to nothing at
//            its edge and costs a few triangles where a volume would take a renderer of its own. Dust behind a herd and breath in cold air are made of them.
//   motes    pollen and seeds drifting round the camera, a speck or two to the cubic metre, slow on the air: they show where the sun catches them against dark shade. Midges dance in
//            their own small cloud in the still air by a tree. `motes off` leaves it clear, and rain washes it out.
//
// The motes are worked out from the time alone (no state), each on its own slow way round a place of its own that is fixed to the land, so a camera moves through them and they do
// not crawl with it.
//
// Coordinates: pbrt's.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "cartoon_lib.h"
#include "film_sky.h"
#include "nature_lib.h"

namespace cartoon {
namespace airborne {

struct Motes {
	bool on = true;
	double amount = 1.0;
};
struct Puff {
	Vec3 at;
	double size = 1.0, opacity = 0.2;
};

constexpr int kThickness = 4;   // (the pictures: how dense a puff is, each a soft round cloud)
inline double thicknessOf(int k) { return 0.12 * (k + 1) * (k + 1) * 0.5 + 0.06; }
namespace detail {
// A soft cloud: thick in the middle, thinning unevenly to nothing at its edge.
inline std::string cloudPicture(const std::string &film, int k) {
	constexpr int W = 96;
	return film_sky::picture(film, "puff1_" + std::to_string(k), W, W, [&](std::vector<float> &rgb) {
		for (int y = 0; y < W; ++y)
			for (int x = 0; x < W; ++x) {
				const double u = 2.0 * (x + 0.5) / W - 1.0, v = 2.0 * (y + 0.5) / W - 1.0, r = std::sqrt(u * u + v * v);
				const double lumps = nature::fbm2(u * 2.4 + 9.0, v * 2.4 + 4.0, 67);
				const double a = (1.0 - smoothstep(0.25, 1.0, r + 0.5 * (lumps - 0.5))) * (0.65 + 0.7 * lumps);
				float *px = &rgb[(static_cast<std::size_t>(y) * W + x) * 3];
				px[0] = px[1] = px[2] = static_cast<float>(std::clamp(a * thicknessOf(k), 0.0, 1.0));
			}
	});
}
}  // namespace detail

// Puffs of a colour, seen from `eye`, as scene text: a mesh of squares facing it for each thickness used.
inline std::string puffsText(const std::string &film, const std::vector<Puff> &puffs, const Vec3 &colour, const Vec3 &eye, const std::string &name) {
	std::string out;
	char buf[260];
	for (int k = 0; k < kThickness; ++k) {
		std::string points, uvs, indices;
		int n = 0;
		for (const Puff &p : puffs) {
			int best = 0;
			for (int j = 1; j < kThickness; ++j)
				if (std::abs(thicknessOf(j) - p.opacity) < std::abs(thicknessOf(best) - p.opacity)) best = j;
			if (best != k) continue;
			const Vec3 to = normalize(eye - p.at), right = normalize(cross(Vec3{0.0, 1.0, 0.0}, to) + Vec3{1e-6, 0.0, 0.0}), up = cross(to, right);
			const Vec3 a = p.at - right * p.size - up * p.size, b = p.at + right * p.size - up * p.size, c = p.at + right * p.size + up * p.size, d = p.at - right * p.size + up * p.size;
			std::snprintf(buf, sizeof buf, "%.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f ", a.x, a.y, a.z, b.x, b.y, b.z, c.x, c.y, c.z, d.x, d.y, d.z);
			points += buf;
			uvs += "0 0 1 0 1 1 0 1 ";
			std::snprintf(buf, sizeof buf, "%d %d %d %d %d %d ", n, n + 1, n + 2, n, n + 2, n + 3);
			indices += buf;
			n += 4;
		}
		if (n == 0) continue;
		const std::string tex = name + "_puff" + std::to_string(k);
		out += "AttributeBegin\n  Texture \"" + tex + "\" \"float\" \"imagemap\" \"string filename\" [ \"" + detail::cloudPicture(film, k) + "\" ]\n  " + diffuseMaterial(colour) +
			   "\n  Shape \"trianglemesh\" \"texture alpha\" [ \"" + tex + "\" ] \"integer indices\" [ " + indices + "] \"point3 P\" [ " + points + "] \"point2 uv\" [ " + uvs + "]\nAttributeEnd\n";
	}
	return out;
}

// The specks in the air round `eye` at t, as scene text. `still`: how little wind there is, 0 .. 1 (midges want still air); `trees` the near trees' feet, where midges dance.
inline std::string motesText(const Motes &a, const Vec3 &eye, double t, double still, const std::vector<Vec3> &trees) {
	if (!a.on || a.amount <= 0.0) return std::string();
	constexpr double kCell = 2.5;
	constexpr int kReach = 4;
	const int cx = static_cast<int>(std::floor(eye.x / kCell)), cy = static_cast<int>(std::floor(eye.y / kCell)), cz = static_cast<int>(std::floor(eye.z / kCell));
	std::string pale, dark;
	char buf[120];
	const auto speck = [&](std::string &to, const Vec3 &p, double r) {
		std::snprintf(buf, sizeof buf, "AttributeBegin Translate %.4f %.4f %.4f Shape \"sphere\" \"float radius\" [ %.5f ] AttributeEnd\n", p.x, p.y, p.z, r);
		to += buf;
	};
	for (int k = -kReach; k <= kReach; ++k)
		for (int j = -2; j <= 2; ++j)
			for (int i = -kReach; i <= kReach; ++i) {
				const int cell = (cx + i) * 73856093 ^ (cy + j) * 19349663 ^ (cz + k) * 83492791;
				const int n = static_cast<int>(1.6 * a.amount * hash01(cell, 41) + 0.5);
				for (int m = 0; m < n; ++m) {
					const int s = cell * 31 + m;
					const double w = 0.2 + 0.25 * hash01(s, 42), ph = 6.3 * hash01(s, 43);
					const Vec3 home{(cx + i + hash01(s, 44)) * kCell, (cy + j + hash01(s, 45)) * kCell, (cz + k + hash01(s, 46)) * kCell};
					const Vec3 p = home + Vec3{0.5 * std::sin(w * t + ph), 0.3 * std::sin(0.7 * w * t + 2.0 * ph), 0.5 * std::cos(0.9 * w * t + ph)};
					if (p.y < 0.0) continue;
					speck(pale, p, hash01(s, 47) < 0.08 ? 0.0035 : 0.0012);   // (a seed now and then among the pollen)
				}
			}
	if (still > 0.3)
		for (std::size_t n = 0; n < trees.size() && n < 6; ++n) {
			const int s = static_cast<int>(n) * 977 + 5;
			const Vec3 middle = trees[n] + Vec3{1.6 * hash01(s, 48) - 0.8, 1.5 + 0.8 * hash01(s, 49), 1.6 * hash01(s, 50) - 0.8};
			for (int m = 0; m < 40; ++m) {
				const double w = 3.0 + 4.0 * hash01(s + m, 51), ph = 6.3 * hash01(s + m, 52), r = 0.15 + 0.3 * hash01(s + m, 53);
				speck(dark, middle + Vec3{r * std::sin(w * t + ph), 0.5 * r * std::sin(1.3 * w * t + 2.0 * ph) + 0.1 * std::sin(0.8 * t + ph), r * std::cos(0.8 * w * t + 3.0 * ph)}, 0.0011);
			}
		}
	std::string out;
	if (!pale.empty()) out += "# what hangs in the air (film_air.h)\nAttributeBegin\n" + diffuseMaterial({0.85, 0.8, 0.62}) + "\n" + pale + "AttributeEnd\n";
	if (!dark.empty()) out += "AttributeBegin\n" + diffuseMaterial({0.04, 0.035, 0.03}) + "\n" + dark + "AttributeEnd\n";
	return out;
}

}  // namespace airborne
}  // namespace cartoon
