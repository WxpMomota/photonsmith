#pragma once
// film_clouds.h -- clouds you can see round (film_file.h `clouds`): cumulus heaped up from a flat base, towering storm clouds, thin wisps - each a volume of water droplets
// (a "uniformgrid" medium in a sphere, which every renderer draws: they scatter the sun, are dark underneath and bright at the edges it shines through, and throw moving
// shadows on the land), spread over the sky round the film, drifting with the wind - or a deck of cloud over the whole area (`clouds deck`) the camera can fly up through
// into the sunshine above, its gaps letting the sun down in shafts. And fog banks (`fogbank`): fog lying on the land in a valley or over a lake, drifting.
//
// A cloud is a grid of densities (48 x 24 x 48 for a cumulus) made from overlapping blobs - one big one, a ring of smaller ones heaped round it, a flat base where the air
// stops rising - eaten away at its edges by three-dimensional noise into billows. Each is written once as a MakeNamedMedium in a file of the film's cache and named in every
// frame by an Include, inside a transform that places, sizes and drifts it; the sphere that holds it is a pass-through shell ("interface"). Clouds are spread out so their
// spheres never overlap (where two overlap, the GPU samples the second only beyond the first one's far side), and wrap round the area as they drift, so the sky never empties.
//
// The camera may be inside one, and the land may run through one (a fog bank always has the ground in it): every renderer samples a volume along each ray up to the nearest
// surface (the GPU's wavefront renderer since 2026-10-27, the default one hands such a frame to it; Metal since 2026-10-30).

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
// A deck (`deck`): one layer over the whole area, its base at `base`, `thick` metres deep, `cover` the share of the sky it closes.
struct Clouds {
	std::string kind = "cumulus";
	double cover = 0.35, base = 900.0, size = 800.0, speed = 6.0, wind = 90.0, density = 1.0, area = 6000.0, x = 0.0, z = 0.0, thick = 250.0;
	int seed = 1;
	bool windSet = false, speedSet = false;   // (its line gave its own: else the film's `wind`)
};
inline const std::vector<std::string> &kinds() {
	static const std::vector<std::string> k = {"cumulus", "towering", "wisps", "deck"};
	return k;
}

// A fog bank (`fogbank`): fog pooled in the low ground round x z (a place in a valley or on a lake), `size` metres across, its top `depth` metres over the ground at x z
// (lower ground fills deeper, higher stands out of it), thinning to a ragged top, `density` its
// scattering per metre (0.03: some 30 m to see through), drifting with a wind of `speed` m/s from `wind` (a facing); it settles in from t0 and burns off - sinking into the
// ground - by t1 (over a tenth of its time each way, or 3 s when it has only one of them).
struct FogBank {
	double x = 0.0, z = 0.0, size = 600.0, depth = 40.0, density = 0.03, wind = 90.0, speed = 1.5, t0 = -1e30, t1 = 1e30;
	int seed = 1;
	bool windSet = false, speedSet = false;   // (its line gave its own: else the film's `wind`)
	double heightAt(double t) const {   // its depth now, as a share of `depth`
		const bool from = t0 > -1e29, until = t1 < 1e29;
		const double ramp = from && until ? std::max(1e-3, 0.1 * (t1 - t0)) : 3.0;
		double h = 1.0;
		if (from) h = std::min(h, (t - t0) / ramp);
		if (until) h = std::min(h, (t1 - t) / ramp);
		return std::clamp(h, 0.0, 1.0);
	}
};

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

// A file of the film's cache (`fileName`) holding one grid medium named `name` over the unit box: nx x ny x nz densities (x fastest, then y, then z), scattering
// `sigma` per unit of density and metre (the box is scaled to the world), phase g `g`. Made once (`make` is only called then). Returns the path as a scene file wants it.
template <class Make>
inline std::string gridFile(const std::string &film, const std::string &fileName, const std::string &name, int nx, int ny, int nz, double sigma, double g, Make make) {
	const std::filesystem::path target = film_cache::dir(film) / fileName;
	std::error_code ec;
	if (!(std::filesystem::exists(target, ec) && std::filesystem::file_size(target, ec) > 0)) {
		const std::vector<float> d = make();
		std::string text;
		text.reserve(d.size() * 6 + 400);
		char buf[320];
		std::snprintf(buf, sizeof buf, "MakeNamedMedium \"%s\" \"string type\" [ \"uniformgrid\" ]\n  \"integer nx\" [ %d ] \"integer ny\" [ %d ] \"integer nz\" [ %d ]\n  \"rgb sigma_a\" [ 0 0 0 ] \"rgb sigma_s\" [ %.5f %.5f %.5f ] \"float g\" [ %.2f ]\n  \"float density\" [\n",
					  name.c_str(), nx, ny, nz, sigma, sigma, sigma, g);
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

// The file holding a cloud's medium (a MakeNamedMedium named `name`, over the unit box), made once in the film's cache. Returns the path as a scene file wants it.
inline std::string cloudFile(const std::string &film, const std::string &kind, int variant, const std::string &name, double density) {
	char tail[64];
	std::snprintf(tail, sizeof tail, "_v%d_%.3f.pbrt", variant, density);
	int nx = 0, ny = 0, nz = 0;
	double tall = 0.5;
	detail::gridOf(kind, nx, ny, nz, tall);
	// sigma_s per metre (the box is scaled to hundreds of metres): a fair-weather cumulus is a few tens of free paths across; g 0.8, as droplets scatter forward
	// (one file per cloud: its medium has a name of its own)
	return gridFile(film, name + tail, name, nx, ny, nz, 0.09 * density, 0.8, [&] { return densities(kind, variant); });
}

// ---- a deck of cloud ----
// Its grid: 160 x 14 x 160 over the area. A cell is closed where two-dimensional noise passes a threshold set by `cover` (the noise is close to normal round 0.5, spread
// about 0.1), the layer's top heaves with noise of its own over a flat base, three-dimensional noise tears its edges, and it fades out at the area's border.
constexpr int kDeckNx = 160, kDeckNy = 14, kDeckNz = 160;
inline std::vector<float> deckDensities(double cover, int seed) {
	std::vector<float> d(static_cast<std::size_t>(kDeckNx) * kDeckNy * kDeckNz, 0.0f);
	const int salt = 7001 + 37 * seed;
	const double threshold = 0.5 - 0.25 * (std::clamp(cover, 0.0, 1.0) - 0.5);
	for (int k = 0; k < kDeckNz; ++k)
		for (int i = 0; i < kDeckNx; ++i) {
			const double x = (i + 0.5) / kDeckNx, z = (k + 0.5) / kDeckNz;
			const double fill = smoothstep(threshold - 0.06, threshold + 0.06, detail::fbm3(x * 9.0, 0.37, z * 9.0, salt));
			const double edge = smoothstep(0.0, 0.06, x) * smoothstep(0.0, 0.06, 1.0 - x) * smoothstep(0.0, 0.06, z) * smoothstep(0.0, 0.06, 1.0 - z);
			const double top = 0.5 + 0.5 * detail::fbm3(x * 14.0, 0.71, z * 14.0, salt + 5);
			for (int j = 0; j < kDeckNy; ++j) {
				const double y = (j + 0.5) / kDeckNy;
				const double core = fill * edge * smoothstep(0.0, 0.12, y) * (1.0 - smoothstep(top - 0.25, top, y));
				const double billow = detail::fbm3(x * 40.0, y * 3.0, z * 40.0, salt + 11);
				const double v = (core - 0.35 * (billow - 0.4)) * 2.5 * smoothstep(0.0, 0.08, core);
				d[(static_cast<std::size_t>(k) * kDeckNy + j) * kDeckNx + i] = static_cast<float>(std::clamp(v, 0.0, 1.0));
			}
		}
	return d;
}

// The deck at t (nature's time): one box over the area, drifting with the wind (the area should be bigger than the film's drift); its sphere holds the camera and the land.
inline std::string deckText(const std::string &film, const Clouds &c, double t) {
	char name[64], file[96], buf[400];
	std::snprintf(name, sizeof name, "deck_%d", c.seed);
	std::snprintf(file, sizeof file, "%s_c%.2f_%.3f.pbrt", name, c.cover, c.density);
	// sigma_s 0.03 per metre at density 1: some 30 m to see through inside, a few hundred metres of it close to opaque from below; g 0.8
	const std::string path = gridFile(film, file, name, kDeckNx, kDeckNy, kDeckNz, 0.03 * c.density, 0.8, [&] { return deckDensities(c.cover, c.seed); });
	const double a = c.wind * kPi / 180.0, dx = std::sin(a) * c.speed * t, dz = std::cos(a) * c.speed * t;
	const Vec3 centre{c.x + dx, c.base + 0.5 * c.thick, c.z + dz};
	std::string out = "# a deck of cloud (film_clouds.h)\n";
	std::snprintf(buf, sizeof buf, "AttributeBegin\n  Translate %.3f %.3f %.3f\n  Scale %.3f %.3f %.3f\n", centre.x - 0.5 * c.area, c.base, centre.z - 0.5 * c.area, c.area, c.thick, c.area);
	out += buf;
	out += "  Include \"" + path + "\"\nAttributeEnd\n";
	std::snprintf(buf, sizeof buf, "AttributeBegin\n  Translate %.3f %.3f %.3f\n  Material \"interface\"\n  MediumInterface \"%s\" \"\"\n  Shape \"sphere\" \"float radius\" [ %.3f ]\nAttributeEnd\n", centre.x,
				  centre.y, centre.z, name, 0.5 * std::sqrt(2.0 * c.area * c.area + c.thick * c.thick) * 1.01);
	out += buf;
	return out;
}

// ---- fog banks ----
// A bank's grid: 72 x 12 x 72, round with a ragged edge, dense most of the way up and thinning over its last fifth to a heaving top (fog pooled in a valley has a top you
// can see), in wisps.
constexpr int kBankNx = 72, kBankNy = 12, kBankNz = 72;
inline std::vector<float> bankDensities(int seed) {
	std::vector<float> d(static_cast<std::size_t>(kBankNx) * kBankNy * kBankNz, 0.0f);
	const int salt = 9001 + 41 * seed;
	for (int k = 0; k < kBankNz; ++k)
		for (int i = 0; i < kBankNx; ++i) {
			const double x = (i + 0.5) / kBankNx, z = (k + 0.5) / kBankNz;
			const double r = 2.0 * std::hypot(x - 0.5, z - 0.5) * (0.85 + 0.3 * detail::fbm3(x * 6.0, 0.2, z * 6.0, salt));
			const double round = 1.0 - smoothstep(0.55, 1.0, r);
			const double top = 0.8 + 0.2 * detail::fbm3(x * 5.0, 0.6, z * 5.0, salt + 3);
			for (int j = 0; j < kBankNy; ++j) {
				const double y = (j + 0.5) / kBankNy;
				const double wisps = detail::fbm3(x * 12.0, y * 4.0, z * 12.0, salt + 7);
				const double v = round * (1.0 - smoothstep(top - 0.2, top, y)) * (0.6 + 1.6 * (wisps - 0.5));
				d[(static_cast<std::size_t>(k) * kBankNy + j) * kBankNx + i] = static_cast<float>(std::clamp(v, 0.0, 1.0));
			}
		}
	return d;
}

// A fog bank at film time t (nature's time `nt` drifts it), as scene text: fog pools in the low ground with a flat top, the hills standing out of it, so its box runs from a
// little below the lowest ground under it up to `depth` (times heightAt: it sinks as it burns off) over the ground at its place - a place in the low ground it fills.
// `groundAt(x, z)` is the land's height (pbrt's x and z). Empty once it has burnt off.
template <class Ground>
inline std::string fogBankText(const std::string &film, const FogBank &f, int index, double t, double nt, Ground groundAt) {
	const double h = f.heightAt(t);
	if (h <= 0.01) return std::string();
	char name[64], file[96], buf[400];
	std::snprintf(name, sizeof name, "fogbank_%d", index);
	std::snprintf(file, sizeof file, "%s_s%d_%.4f.pbrt", name, f.seed, f.density);
	const std::string path = gridFile(film, file, name, kBankNx, kBankNy, kBankNz, f.density, 0.6, [&] { return bankDensities(f.seed); });
	const double a = f.wind * kPi / 180.0;
	const double cx = f.x + std::sin(a) * f.speed * nt, cz = f.z + std::cos(a) * f.speed * nt;
	double low = groundAt(cx, cz);
	for (int j = -2; j <= 2; ++j)
		for (int i = -2; i <= 2; ++i) low = std::min(low, groundAt(cx + 0.2 * i * f.size, cz + 0.2 * j * f.size));
	const double level = groundAt(cx, cz) + f.depth * h;   // (the fog's top: `depth` over the ground where it was put, which should be low ground)
	const double base = std::min(low, level - 1.0) - 2.0, top = level, tall = std::max(1.0, top - base);
	std::string out = "# a fog bank (film_clouds.h)\n";
	std::snprintf(buf, sizeof buf, "AttributeBegin\n  Translate %.3f %.3f %.3f\n  Scale %.3f %.3f %.3f\n", cx - 0.5 * f.size, base, cz - 0.5 * f.size, f.size, tall, f.size);
	out += buf;
	out += "  Include \"" + path + "\"\nAttributeEnd\n";
	std::snprintf(buf, sizeof buf, "AttributeBegin\n  Translate %.3f %.3f %.3f\n  Material \"interface\"\n  MediumInterface \"%s\" \"\"\n  Shape \"sphere\" \"float radius\" [ %.3f ]\nAttributeEnd\n", cx,
				  base + 0.5 * tall, cz, name, 0.5 * std::sqrt(2.0 * f.size * f.size + tall * tall) * 1.01);
	out += buf;
	return out;
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
	if (c.kind == "deck") return deckText(film, c, t);
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
