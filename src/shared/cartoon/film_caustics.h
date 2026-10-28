#pragma once
// film_caustics.h -- light and water, worked out (round 9 of the film files: `caustics traced`, `waterfall ... rainbow`):
//
//   caustics    the net of light under water traced from the surface itself: a tile of ripples 4 m across (two dozen waves, each running at the speed deep water
//               gives its length, looped over 2.4 s), and through each of half a million points of it a ray of sunlight refracted into the water and followed down
//               to the bed `depth` below - where the rays crowd together the bed is bright, where they spread it is dim. So the net sharpens and brightens with depth
//               to where the ripples focus (a metre or so) and blurs and coarsens below it, as a real one does; the painted net (film_underwater.h causticAt) is the
//               same everywhere. Used, with `caustics traced`, wherever the films draw caustics: the seabed under water, the light shafts, the sand of the shallows;
//   mistbow     a rainbow in a waterfall's spray: seen by the camera 40.5 to 42.5 degrees round the point opposite the sun wherever that cone passes through the
//               spray (a sphere round the fall's foot), red outside, violet inside, brighter where it crosses more of the spray - only while the sun is up and
//               behind the camera.
//
// Coordinates are pbrt's.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "cartoon_lib.h"
#include "film_sky.h"

namespace cartoon {
namespace caustics {

constexpr double kTile = 4.0;        // metres: the tile of ripples (and of the net)
constexpr int kGrid = 256;           // the net's cells across the tile
constexpr int kFrames = 24;          // its loop: 24 frames over 2.4 s
constexpr std::array<double, 6> kDepths = {0.3, 0.6, 1.2, 2.4, 4.8, 9.6};

namespace detail {
struct Wave {
	double kx, kz, amp, omega, phase;
};
// The ripples: wave vectors on the tile's lattice (so the tile repeats), lengths from 15 cm to 1.5 m, steeper the shorter (a slope of about 0.1 each way all told),
// each frequency the deep-water one rounded to a whole number of turns a loop (so the loop closes).
inline const std::vector<Wave> &waves() {
	static const std::vector<Wave> w = [] {
		std::vector<Wave> out;
		const double base = 2.0 * kPi / kTile;
		for (int i = 0; out.size() < 24 && i < 4000; ++i) {
			const int nx = static_cast<int>(std::floor(hash01(i, 801) * 53.0)) - 26, nz = static_cast<int>(std::floor(hash01(i, 803) * 53.0)) - 26;
			const double k = base * std::hypot(nx, nz), len = 2.0 * kPi / std::max(1e-9, k);
			if (len < 0.15 || len > 1.5) continue;
			const double turns = std::max(1.0, std::round(std::sqrt(9.81 * k) / (2.0 * kPi) * 2.4));
			out.push_back({base * nx, base * nz, 0.012 * len / (2.0 * kPi) * (0.6 + 0.8 * hash01(i, 805)), 2.0 * kPi * turns / 2.4, 2.0 * kPi * hash01(i, 807)});
		}
		return out;
	}();
	return w;
}
}  // namespace detail

// The net of light at frame k (of kFrames) on a bed `depth` metres down under the tile (kGrid x kGrid, the tile's cells row by row; 1 is the light a flat surface lets
// through, more where the rays crowd). Worked out once each and kept.
inline const std::vector<float> &pattern(int k, int depthIndex) {
	static std::mutex lock;
	static std::map<int, std::array<std::vector<float>, kDepths.size()>> made;
	const std::lock_guard<std::mutex> hold(lock);
	k = ((k % kFrames) + kFrames) % kFrames;
	auto it = made.find(k);
	if (it == made.end()) {
		std::array<std::vector<float>, kDepths.size()> nets;
		for (auto &n : nets) n.assign(static_cast<std::size_t>(kGrid) * kGrid, 0.0f);
		const double t = 2.4 * k / kFrames, eta = 1.0 / 1.333;
		const Vec3 sun = normalize(Vec3{0.18, -1.0, 0.1});   // (sunlight a little off straight down)
		const int M = 2 * kGrid;   // (rays across the tile each way: four to a cell)
		const double weight = double(kGrid) * kGrid / (double(M) * M);
		for (int j = 0; j < M; ++j)
			for (int i = 0; i < M; ++i) {
				const double x = (i + 0.5) / M * kTile, z = (j + 0.5) / M * kTile;
				double sx = 0.0, sz = 0.0;
				for (const detail::Wave &w : detail::waves()) {   // the slope there
					const double c = std::cos(w.kx * x + w.kz * z - w.omega * t + w.phase) * w.amp;
					sx += w.kx * c, sz += w.kz * c;
				}
				const Vec3 n = normalize(Vec3{-sx, 1.0, -sz});
				// refracted into the water (Snell's law)
				const double cosi = -dot(n, sun), k2 = 1.0 - eta * eta * (1.0 - cosi * cosi);
				if (k2 <= 0.0) continue;
				const Vec3 r = sun * eta + n * (eta * cosi - std::sqrt(k2));
				if (r.y >= -1e-6) continue;
				for (std::size_t d = 0; d < kDepths.size(); ++d) {
					const double px = (x + r.x / -r.y * kDepths[d]) / kTile * kGrid - 0.5, pz = (z + r.z / -r.y * kDepths[d]) / kTile * kGrid - 0.5;
					const int x0 = static_cast<int>(std::floor(px)), z0 = static_cast<int>(std::floor(pz));
					const double fx = px - x0, fz = pz - z0;
					for (int b = 0; b < 2; ++b)
						for (int a = 0; a < 2; ++a) {
							const int cx = ((x0 + a) % kGrid + kGrid) % kGrid, cz = ((z0 + b) % kGrid + kGrid) % kGrid;
							nets[d][static_cast<std::size_t>(cz) * kGrid + cx] += static_cast<float>(weight * (a ? fx : 1.0 - fx) * (b ? fz : 1.0 - fz));
						}
				}
			}
		it = made.emplace(k, std::move(nets)).first;
	}
	return it->second[static_cast<std::size_t>(std::clamp(depthIndex, 0, static_cast<int>(kDepths.size()) - 1))];
}
// The net at (u, v) (in tiles: it repeats over 0 to 1 each way) at frame k on a bed `depth` metres down: between the depths it was worked out for (shallower than the
// first, it fades to the even light under a flat surface).
inline double at(double u, double v, int k, double depth) {
	const auto sample = [&](int d) {
		const double x = (u - std::floor(u)) * kGrid, z = (v - std::floor(v)) * kGrid;
		const int i = std::min(kGrid - 1, static_cast<int>(x)), j = std::min(kGrid - 1, static_cast<int>(z));
		return static_cast<double>(pattern(k, d)[static_cast<std::size_t>(j) * kGrid + i]);
	};
	if (depth <= kDepths[0]) return 1.0 + (sample(0) - 1.0) * clamp01(depth / kDepths[0]);
	for (std::size_t d = 0; d + 1 < kDepths.size(); ++d)
		if (depth <= kDepths[d + 1]) {
			const double f = std::log(depth / kDepths[d]) / std::log(kDepths[d + 1] / kDepths[d]);
			return lerp(sample(static_cast<int>(d)), sample(static_cast<int>(d) + 1), f);
		}
	return sample(static_cast<int>(kDepths.size()) - 1);
}
// The net as a tiling picture at `size` pixels for frame k on a bed `depth` down (to the nearest of the depths it was worked out for), scaled as the painted one's
// pictures are (film_underwater.h: about 0.35 in the dark, up to 2 or so on the brightest lines).
inline std::string picture(const std::string &film, int k, int size, double depth) {
	int d = 0;
	for (std::size_t i = 1; i < kDepths.size(); ++i)
		if (std::abs(std::log(depth / kDepths[i])) < std::abs(std::log(depth / kDepths[static_cast<std::size_t>(d)]))) d = static_cast<int>(i);
	char name[64];
	std::snprintf(name, sizeof name, "caustic_traced_%d_%02d_d%d", size, ((k % kFrames) + kFrames) % kFrames, d);
	return film_sky::picture(film, name, size, size, [&](std::vector<float> &rgb) {
		for (int j = 0; j < size; ++j)
			for (int i = 0; i < size; ++i) {
				const double I = at((i + 0.5) / size, (j + 0.5) / size, k, kDepths[static_cast<std::size_t>(d)]);
				const float val = static_cast<float>(std::min(4.0, 0.75 * I));
				const std::size_t q = (static_cast<std::size_t>(j) * size + i) * 3;
				rgb[q] = rgb[q + 1] = rgb[q + 2] = val;
			}
	});
}

// ---- a rainbow in a waterfall's spray ----
// The bow's colour `x` across it (0 inside, violet, to 1 outside, red), fading at its edges.
inline Vec3 bowColour(double x) {
	const Vec3 c{clamp01(1.5 - std::abs(x - 1.0) * 3.0) + clamp01(0.6 - x * 3.0), clamp01(1.5 - std::abs(x - 0.55) * 3.5), clamp01(1.5 - std::abs(x - 0.15) * 4.0)};
	return c * (smoothstep(0.0, 0.15, x) * (1.0 - smoothstep(0.85, 1.0, x)));
}
// The rainbow in a fall's spray (a sphere at `mist` of `radius`) as seen from `eye`, the sun toward `sunDir` (pbrt's, normalised) with `irradiance`, as scene text: the
// spray itself glows in the bow's colours where it lies 40.5 to 42.5 degrees from the point opposite the sun as the camera sees it - a faintly emitting volume (an
// "rgbgrid" medium that hardly absorbs) in a pass-through sphere, so it neither hides nor shadows what is behind it, densest in the middle of the spray.
// "" when the sun is down or the bow misses the spray.
inline std::string mistbowText(Vec3 eye, Vec3 sunDir, double irradiance, Vec3 mist, double radius, double strength) {
	if (sunDir.y <= 0.0 || irradiance <= 0.0 || strength <= 0.0) return std::string();
	const Vec3 A = sunDir * -1.0;
	const int n = 40;
	std::string sa, le;
	sa.reserve(static_cast<std::size_t>(n) * n * n * 12), le.reserve(sa.capacity());
	double lit = 0.0;
	const auto put = [](std::string &out, double v) {
		char b[24];
		if (v < 1e-5) out += "0 ";
		else {
			std::snprintf(b, sizeof b, "%.4f ", v);
			out += b;
		}
	};
	for (int k = 0; k < n; ++k)
		for (int j = 0; j < n; ++j)
			for (int i = 0; i < n; ++i) {
				const Vec3 p = mist + Vec3{((i + 0.5) / n - 0.5) * 2.0 * radius, ((j + 0.5) / n - 0.5) * 2.0 * radius, ((k + 0.5) / n - 0.5) * 2.0 * radius};
				const double r = length(p - mist) / radius, spray = r < 1.0 ? std::pow(1.0 - r * r, 1.5) : 0.0;
				Vec3 c{0.0, 0.0, 0.0};
				if (spray > 0.0) {
					const double ang = std::acos(std::clamp(dot(normalize(p - eye), A), -1.0, 1.0)) * 180.0 / kPi;
					c = bowColour((ang - 40.5) / 2.0) * spray;
				}
				lit += c.x + c.y + c.z;
				put(sa, spray > 0.0 ? 0.002 : 0.0), put(sa, spray > 0.0 ? 0.002 : 0.0), put(sa, spray > 0.0 ? 0.002 : 0.0);
				put(le, c.x), put(le, c.y), put(le, c.z);
			}
	if (lit <= 0.0) return std::string();
	char buf[400];
	std::string out = "# a rainbow in the spray (film_caustics.h)\nAttributeBegin\n";
	std::snprintf(buf, sizeof buf, "  Translate %.4f %.4f %.4f\n  Scale %.4f %.4f %.4f\n", mist.x - radius, mist.y - radius, mist.z - radius, 2.0 * radius, 2.0 * radius, 2.0 * radius);
	out += buf;
	// (sigma_a * Le along a line of sight through the bow, which crosses the whole spray at the one angle: a few hundredths of the sunlight)
	std::snprintf(buf, sizeof buf, "  MakeNamedMedium \"mistbow\" \"string type\" [ \"rgbgrid\" ] \"integer nx\" [ %d ] \"integer ny\" [ %d ] \"integer nz\" [ %d ] \"float g\" [ 0 ] \"float Lescale\" [ %.5f ]\n",
				  n, n, n, 30.0 * irradiance * strength / std::max(1.0, radius));
	out += buf;
	std::string ss;
	for (std::size_t q = 0; q < static_cast<std::size_t>(n) * n * n; ++q) ss += "0 0 0 ";
	out += "    \"rgb sigma_a\" [ " + sa + "]\n    \"rgb sigma_s\" [ " + ss + "]\n    \"rgb Le\" [ " + le + "]\nAttributeEnd\n";
	std::snprintf(buf, sizeof buf, "AttributeBegin\n  Translate %.4f %.4f %.4f\n  Material \"interface\"\n  MediumInterface \"mistbow\" \"\"\n  Shape \"sphere\" \"float radius\" [ %.4f ]\nAttributeEnd\n", mist.x,
				  mist.y, mist.z, radius * 1.74);
	return out + buf;
}

}  // namespace caustics
}  // namespace cartoon
