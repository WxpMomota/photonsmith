#pragma once
// film_clouds.h -- clouds you can see round (film_file.h `clouds`): cumulus heaped up from a flat base, towering storm clouds, thin wisps - each a volume of water droplets
// (a "uniformgrid" medium in a sphere, which every renderer draws: they scatter the sun, are dark underneath and bright at the edges it shines through, and throw moving
// shadows on the land), spread over the sky round the film, drifting with the wind.
//
// A cloud is a grid of densities (48 x 24 x 48 for a cumulus) made from overlapping blobs - one big one, a ring of smaller ones heaped round it, a flat base where the air
// stops rising - eaten away at its edges by three-dimensional noise into billows. Each is written once as a MakeNamedMedium in a file of the film's cache and named in every
// frame by an Include, inside a transform that places, sizes and drifts it; the sphere that holds it is a pass-through shell ("interface"). Clouds are spread out so their
// spheres never overlap (a renderer follows one medium at a time), and wrap round the area as they drift, so the sky never empties.
//
// The camera stays outside them: a camera inside a cloud's sphere sees it from within only partly (the renderers enter a shape's medium where a ray crosses its shell).

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "cartoon_lib.h"
#include "film_cache.h"
#include "nature_lib.h"

namespace cartoon {
namespace clouds {

// A sky of clouds: their kind (cumulus, towering or wisps), how much of the sky they cover (0 to 1), the height of their bases and how big they are (metres across), the
// wind that drifts them (metres a second, and a facing), how dense (1 a fair-weather cumulus), the area they are spread over (a square this many metres across round
// x z) and a seed.
struct Clouds {
	std::string kind = "cumulus";
	double cover = 0.35, base = 900.0, size = 800.0, speed = 6.0, wind = 90.0, density = 1.0, area = 6000.0, x = 0.0, z = 0.0;
	int seed = 1;
};
inline const std::vector<std::string> &kinds() {
	static const std::vector<std::string> k = {"cumulus", "towering", "wisps"};
	return k;
}

namespace detail {

inline double noise3(double x, double y, double z, int salt) {   // value noise in 3D, [0, 1)
	const double fx = std::floor(x), fy = std::floor(y), fz = std::floor(z);
	const int ix = static_cast<int>(fx), iy = static_cast<int>(fy), iz = static_cast<int>(fz);
	const double u = x - fx, v = y - fy, w = z - fz;
	const double su = u * u * (3 - 2 * u), sv = v * v * (3 - 2 * v), sw = w * w * (3 - 2 * w);
	const auto at = [salt](int a, int b, int c) { return hash01(a * 73856093 ^ b * 19349663 ^ c * 83492791, salt); };
	const double x00 = lerp(at(ix, iy, iz), at(ix + 1, iy, iz), su), x10 = lerp(at(ix, iy + 1, iz), at(ix + 1, iy + 1, iz), su);
	const double x01 = lerp(at(ix, iy, iz + 1), at(ix + 1, iy, iz + 1), su), x11 = lerp(at(ix, iy + 1, iz + 1), at(ix + 1, iy + 1, iz + 1), su);
	return lerp(lerp(x00, x10, sv), lerp(x01, x11, sv), sw);
}
inline double fbm3(double x, double y, double z, int salt) {
	double sum = 0.0, amp = 0.5, total = 0.0;
	for (int o = 0; o < 4; ++o) {
		sum += amp * noise3(x, y, z, salt + 59 * o);
		total += amp;
		x *= 2.02, y *= 2.02, z *= 2.02;
		amp *= 0.5;
	}
	return sum / total;
}

// The grid's size for a kind: (nx, ny, nz), and the box's height as a share of its width.
inline void gridOf(const std::string &kind, int &nx, int &ny, int &nz, double &tall) {
	if (kind == "towering") nx = 44, ny = 48, nz = 44, tall = 1.05;
	else if (kind == "wisps") nx = 64, ny = 8, nz = 32, tall = 0.12;
	else nx = 48, ny = 22, nz = 48, tall = 0.42;
}

}  // namespace detail

// One cloud's densities (0 to 1), x fastest, then y, then z, over its unit box: shape `variant` of the kind.
inline std::vector<float> densities(const std::string &kind, int variant) {
	int nx = 0, ny = 0, nz = 0;
	double tall = 0.5;
	detail::gridOf(kind, nx, ny, nz, tall);
	std::vector<float> d(static_cast<std::size_t>(nx) * ny * nz, 0.0f);
	const int salt = 4001 + 97 * variant;
	struct Blob {
		double x, y, z, r;
	};
	std::vector<Blob> blobs;
	if (kind == "wisps") {   // long streaks along x
		for (int k = 0; k < 7; ++k) blobs.push_back({0.15 + 0.7 * hash01(salt + k, 1), 0.5, 0.2 + 0.6 * hash01(salt + k, 2), 0.18 + 0.12 * hash01(salt + k, 3)});
	} else if (kind == "towering") {   // a tower of billows from a broad base, a crown of them on top (the anvil is added below)
		for (int level = 0; level < 9; ++level) {
			const double y = 0.08 + 0.09 * level;
			const int ring = 4 + level / 2;
			for (int k = 0; k < ring; ++k) {
				const double a = 2.0 * kPi * (k + hash01(salt + 31 * level + k, 13)) / ring;
				const double reach = 0.1 + 0.16 * hash01(salt + 31 * level + k, 16);   // (billows bulging out at all sizes: cauliflower)
				blobs.push_back({0.5 + reach * std::cos(a), y + 0.03 * hash01(salt + 31 * level + k, 14), 0.5 + reach * std::sin(a), 0.12 + 0.06 * (level / 8.0) + 0.08 * hash01(salt + 31 * level + k, 15)});
			}
		}
	} else {   // one big blob, a ring of smaller ones heaped round it (higher near the middle)
		blobs.push_back({0.5, 0.36, 0.5, 0.30});
		const int ring = 7 + static_cast<int>(4 * hash01(salt, 4));
		for (int k = 0; k < ring; ++k) {
			const double a = 2.0 * kPi * (k + 0.4 * hash01(salt + k, 5)) / ring, reach = 0.18 + 0.12 * hash01(salt + k, 6);
			blobs.push_back({0.5 + reach * std::cos(a), 0.26 + 0.16 * hash01(salt + k, 7), 0.5 + reach * std::sin(a), 0.12 + 0.09 * hash01(salt + k, 8)});
		}
		for (int k = 0; k < 4; ++k)   // and a few on top
			blobs.push_back({0.4 + 0.2 * hash01(salt + k, 9), 0.55 + 0.12 * hash01(salt + k, 10), 0.4 + 0.2 * hash01(salt + k, 11), 0.1 + 0.07 * hash01(salt + k, 12)});
	}
	for (int k = 0; k < nz; ++k)
		for (int j = 0; j < ny; ++j)
			for (int i = 0; i < nx; ++i) {
				const double x = (i + 0.5) / nx, y = (j + 0.5) / ny, z = (k + 0.5) / nz;
				double shape = 0.0;
				for (const Blob &b : blobs) {   // (y is squashed by the box's shape: the blobs are round in the world)
					const double dx = (x - b.x), dy = (y - b.y) * (kind == "wisps" ? 0.3 : tall), dz = (z - b.z) * (kind == "wisps" ? 2.0 : 1.0);
					shape = std::max(shape, 1.0 - std::sqrt(dx * dx + dy * dy + dz * dz) / b.r);
				}
				if (kind == "towering") {   // the core of the tower, and the anvil its top spreads into
					shape = std::max(shape, 1.0 - std::hypot(x - 0.5, z - 0.5) / (0.2 + 0.04 * y));
					shape = std::max(shape, 0.6 * std::max(0.0, 1.0 - y / 0.18) * std::max(0.0, 1.0 - std::hypot(x - 0.5, z - 0.5) / 0.42));   // (a broad flat base)
					shape = std::max(shape, 0.8 * std::max(0.0, 1.0 - std::abs(y - 0.88) / 0.08) * std::max(0.0, 1.0 - std::hypot((x - 0.5) * 0.85, z - 0.5) / 0.48));
				}
				if (kind == "cumulus") shape = std::max(shape, 0.55 * std::max(0.0, 1.0 - y / 0.32) * std::max(0.0, 1.0 - std::hypot(x - 0.5, z - 0.5) / 0.44));   // (a broad skirt low down: a flat bottom)
				const double floor = kind == "wisps" ? 1.0 : smoothstep(0.03, 0.12, y);   // the flat base, where the air stops rising
				const double billow = detail::fbm3(x * 7.0, y * 7.0 * tall, z * 7.0, salt);
				const double core = shape * floor;
				const double v = (core - 0.3 * (billow - 0.35)) * 7.0 * smoothstep(0.0, 0.05, core);   // (a crisp edge: cauliflower, not cotton wool; nothing outside the shape)
				d[(static_cast<std::size_t>(k) * ny + j) * nx + i] = static_cast<float>(std::pow(std::clamp(v, 0.0, 1.0), 1.4));
			}
	return d;
}

// The file holding a cloud's medium (a MakeNamedMedium named `name`, over the unit box), made once in the film's cache. Returns the path as a scene file wants it.
inline std::string cloudFile(const std::string &film, const std::string &kind, int variant, const std::string &name, double density) {
	char tail[64];
	std::snprintf(tail, sizeof tail, "_v%d_%.3f.pbrt", variant, density);
	const std::filesystem::path target = film_cache::dir(film) / (name + tail);   // (one file per cloud: its medium has a name of its own)
	std::error_code ec;
	if (!(std::filesystem::exists(target, ec) && std::filesystem::file_size(target, ec) > 0)) {
		int nx = 0, ny = 0, nz = 0;
		double tall = 0.5;
		detail::gridOf(kind, nx, ny, nz, tall);
		const std::vector<float> d = densities(kind, variant);
		std::string text;
		text.reserve(d.size() * 6 + 400);
		char buf[320];
		// sigma_s per metre (the box is scaled to hundreds of metres): a fair-weather cumulus is a few tens of free paths across; g 0.8, as droplets scatter forward
		std::snprintf(buf, sizeof buf, "MakeNamedMedium \"%s\" \"string type\" [ \"uniformgrid\" ]\n  \"integer nx\" [ %d ] \"integer ny\" [ %d ] \"integer nz\" [ %d ]\n  \"rgb sigma_a\" [ 0 0 0 ] \"rgb sigma_s\" [ %.5f %.5f %.5f ] \"float g\" [ 0.8 ]\n  \"float density\" [\n",
					  name.c_str(), nx, ny, nz, 0.09 * density, 0.09 * density, 0.09 * density);
		text += buf;
		int col = 0;
		for (float v : d) {
			if (v <= 0.0f) text += "0 ";
			else {
				std::snprintf(buf, sizeof buf, "%.3f ", v);
				text += buf;
			}
			if (++col == 24) text += "\n", col = 0;
		}
		text += "\n  ]\n";
		if (!film_cache::writeOnce(target, text)) throw std::runtime_error("film " + film + ": could not write " + target.string());
	}
	return film_cache::scenePath(target);
}

// Where the clouds are: each its centre at time 0 (pbrt's), its width, and its shape. Spread so their spheres do not overlap.
struct Placed {
	Vec3 at;
	double width, stretch;   // across z, and how many times that it is along x
	int variant;
};
inline double tallOf(const std::string &kind) {
	int nx = 0, ny = 0, nz = 0;
	double tall = 0.5;
	detail::gridOf(kind, nx, ny, nz, tall);
	return tall;
}
inline double sphereOf(const std::string &kind, double width, double stretch = 1.0) {   // the radius of the sphere round a cloud's box
	const double t = tallOf(kind);
	return 0.5 * width * std::sqrt(1.0 + stretch * stretch + t * t) * 1.02;
}
inline std::vector<Placed> layout(const Clouds &c) {
	std::vector<Placed> out;
	const double target = c.cover * c.area * c.area / (0.75 * c.size * c.size);   // (each covers about three quarters of its square)
	const int want = std::min(60, std::max(1, static_cast<int>(target)));
	for (int k = 0; k < want * 30 && static_cast<int>(out.size()) < want; ++k) {
		Placed p;
		p.width = c.size * (0.7 + 0.6 * hash01(c.seed * 131 + k, 1));
		p.at = {c.x + (hash01(c.seed * 131 + k, 2) - 0.5) * c.area, c.base + 0.5 * p.width * tallOf(c.kind), c.z + (hash01(c.seed * 131 + k, 3) - 0.5) * c.area};
		p.variant = k % 6;
		p.stretch = c.kind == "towering" ? 1.0 : 1.0 + 0.7 * hash01(c.seed * 131 + k, 4);
		const double r = sphereOf(c.kind, p.width, p.stretch);
		bool clear = true;
		for (const Placed &q : out)
			if (std::hypot(p.at.x - q.at.x, p.at.z - q.at.z) < r + sphereOf(c.kind, q.width, q.stretch) + 5.0) clear = false;
		if (clear) out.push_back(p);
	}
	return out;
}

// The clouds at t, as scene text.
inline std::string text(const std::string &film, const Clouds &c, double t) {
	const std::vector<Placed> placed = layout(c);
	const double a = c.wind * kPi / 180.0, dx = std::sin(a) * c.speed * t, dz = std::cos(a) * c.speed * t;
	std::string out = "# the clouds: " + c.kind + " (film_clouds.h)\n";
	char buf[400];
	for (std::size_t i = 0; i < placed.size(); ++i) {
		const Placed &p = placed[i];
		// drifted with the wind, and wrapped round the area so the sky does not empty
		const auto wrap = [&](double v, double centre) { return centre - 0.5 * c.area + std::fmod(std::fmod(v - (centre - 0.5 * c.area), c.area) + c.area, c.area); };
		const Vec3 at{wrap(p.at.x + dx, c.x), p.at.y, wrap(p.at.z + dz, c.z)};
		const double w = p.width, wx = w * p.stretch, h = w * tallOf(c.kind), r = sphereOf(c.kind, w, p.stretch);
		char name[64];
		std::snprintf(name, sizeof name, "cloud_%s_%d_%zu", c.kind.c_str(), c.seed, i);
		const std::string file = cloudFile(film, c.kind, p.variant, name, c.density);
		std::snprintf(buf, sizeof buf, "AttributeBegin\n  Translate %.3f %.3f %.3f\n  Scale %.3f %.3f %.3f\n", at.x - 0.5 * wx, at.y - 0.5 * h, at.z - 0.5 * w, wx, h, w);
		out += buf;
		out += "  Include \"" + file + "\"\nAttributeEnd\n";
		std::snprintf(buf, sizeof buf, "AttributeBegin\n  Translate %.3f %.3f %.3f\n  Material \"interface\"\n  MediumInterface \"%s\" \"\"\n  Shape \"sphere\" \"float radius\" [ %.3f ]\nAttributeEnd\n", at.x, at.y,
					  at.z, name, r);
		out += buf;
	}
	return out;
}

}  // namespace clouds
}  // namespace cartoon
