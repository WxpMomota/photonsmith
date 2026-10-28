#pragma once
// film_cosmos.h -- the universe beyond the planets for a film in space (film_file.h `nebula`; round 10):
//
//   nebula     a cloud of glowing gas some kilometres across (in a film's scale): its gas lit from inside by young stars and glowing in the colours of its
//              elements - the deep red of hydrogen in its outer parts, the teal of oxygen near its hot middle - knotted and wispy, crossed by dark lanes of dust that
//              hide what is behind them (`emission`); or a dark cloud of dust silhouetted against a glowing one behind it, its edges lit (`dark`); or columns of
//              dust rising from a dense base, their tops and edges glowing where the light eats into them, in glowing gas (`pillars`). A few hot stars burn in it.
//              One "rgbgrid" medium (it absorbs, scatters and gives out light per voxel) written once into the film's cache, in a sphere round it - the camera can
//              fly into it.
//
// Coordinates are pbrt's, as a film file's.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

#include "cartoon_lib.h"
#include "film_cache.h"
#include "film_clouds.h"
#include "film_space.h"

namespace cartoon {
namespace cosmos {

// ---- a grid that glows ----
// One voxel of an "rgbgrid" medium: its absorption, its scattering and the light it gives out (pbrt's: emitted radiance is sigma_a * Le * Lescale per metre).
struct Voxel {
	Vec3 sa, ss, le;
};
// A file of the film's cache holding one rgbgrid medium named `name` over the unit box, nx x ny x nz voxels from `voxel(i, j, k)` (x fastest, then y, then z), its phase
// g and the scale of its light. Made once. Returns the path as a scene file wants it.
inline std::string rgbGridFile(const std::string &film, const std::string &fileName, const std::string &name, int nx, int ny, int nz, double g, double lescale,
							   const std::function<Voxel(int, int, int)> &voxel) {
	const std::filesystem::path target = film_cache::dir(film) / fileName;
	std::error_code ec;
	if (std::filesystem::exists(target, ec) && std::filesystem::file_size(target, ec) > 0) return film_cache::scenePath(target);
	std::string sa, ss, le;
	const std::size_t n = static_cast<std::size_t>(nx) * ny * nz;
	sa.reserve(n * 8), ss.reserve(n * 8), le.reserve(n * 8);
	const auto put = [](std::string &out, const Vec3 &v) {
		char b[64];
		for (const double c : {v.x, v.y, v.z}) {
			if (c < 1e-6) out += "0 ";
			else {
				std::snprintf(b, sizeof b, "%.4g ", c);
				out += b;
			}
		}
	};
	for (int k = 0; k < nz; ++k)
		for (int j = 0; j < ny; ++j)
			for (int i = 0; i < nx; ++i) {
				const Voxel v = voxel(i, j, k);
				put(sa, v.sa), put(ss, v.ss), put(le, v.le);
			}
	char head[320];
	std::snprintf(head, sizeof head, "MakeNamedMedium \"%s\" \"string type\" [ \"rgbgrid\" ] \"integer nx\" [ %d ] \"integer ny\" [ %d ] \"integer nz\" [ %d ] \"float g\" [ %.2f ] \"float Lescale\" [ %.6g ]\n",
				  name.c_str(), nx, ny, nz, g, lescale);
	const std::string text = std::string(head) + "  \"rgb sigma_a\" [ " + sa + "]\n  \"rgb sigma_s\" [ " + ss + "]\n  \"rgb Le\" [ " + le + "]\n";
	if (!film_cache::writeOnce(target, text)) throw std::runtime_error("film " + film + ": could not write " + target.string());
	return film_cache::scenePath(target);
}
// A grid medium in the box (corner `lo`, size `size`) and the pass-through sphere it fills, as scene text.
inline std::string placedGrid(const std::string &path, const std::string &name, Vec3 lo, Vec3 size) {
	char buf[320];
	std::snprintf(buf, sizeof buf, "AttributeBegin\n  Translate %.4f %.4f %.4f\n  Scale %.4f %.4f %.4f\n", lo.x, lo.y, lo.z, size.x, size.y, size.z);
	std::string out = std::string(buf) + "  Include \"" + path + "\"\nAttributeEnd\n";
	const Vec3 c = lo + size * 0.5;
	std::snprintf(buf, sizeof buf, "AttributeBegin\n  Translate %.4f %.4f %.4f\n  Material \"interface\"\n  MediumInterface \"%s\" \"\"\n  Shape \"sphere\" \"float radius\" [ %.4f ]\nAttributeEnd\n", c.x, c.y, c.z,
				  name.c_str(), 0.5 * length(size) * 1.01);
	return out + buf;
}

// ---- nebulae ----
struct Nebula {
	std::string name, kind = "emission";
	Vec3 at{0.0, 0.0, -30000.0};
	double size = 20000.0, brightness = 1.0;
	int seed = 1;
};
inline const std::vector<std::string> &nebulaKinds() {
	static const std::vector<std::string> k = {"emission", "dark", "pillars"};
	return k;
}
constexpr int kNebulaN = 72;

namespace detail {
using clouds::detail::fbm3;
inline double ridge(double v) { return 1.0 - std::abs(2.0 * v - 1.0); }
}  // namespace detail

// A nebula's voxel at (x, y, z) in its unit box (0..1 each way), its absorption etc. per metre for a nebula `size` across: how much gas there is, how much dust, what
// colour its gas glows.
inline Voxel nebulaVoxel(const Nebula &n, double x, double y, double z) {
	const int s = n.seed * 113;
	const double cx = x - 0.5, cy = y - 0.5, cz = z - 0.5, r = 2.0 * std::sqrt(cx * cx + cy * cy + cz * cz);   // (r 1 at the box's faces)
	const double warp = 0.8 * (detail::fbm3(x * 3.0, y * 3.0, z * 3.0, s + 1) - 0.5);   // (a ragged outline, not a ball)
	const double shape = 1.0 - smoothstep(0.4, 1.0, r + warp);
	const double clumps = detail::fbm3(x * 6.0 + warp, y * 6.0, z * 6.0 - warp, s + 2), wisps = detail::ridge(detail::fbm3(x * 11.0, y * 11.0 + warp, z * 11.0, s + 3));
	double gas = shape * smoothstep(0.3, 0.75, clumps * 0.7 + wisps * 0.45);
	double dust = shape * smoothstep(0.62, 0.8, detail::ridge(detail::fbm3(x * 4.5 - 2.0, y * 4.5 + warp, z * 4.5, s + 4)));   // (lanes along the ridges of a noise)
	if (n.kind == "dark") {   // a dark cloud in front of a glowing one: dust in the front half, gas behind it (+z is towards a camera looking down -z)
		dust = shape * smoothstep(0.35, 0.6, clumps) * smoothstep(0.45, 0.6, z);
		gas *= 0.6 + 0.8 * (1.0 - smoothstep(0.4, 0.7, z));
	} else if (n.kind == "pillars") {   // columns of dust rising from a dense base, gas round them
		double col = 0.0;
		for (int k = 0; k < 3; ++k) {
			const double px = 0.3 + 0.2 * k + 0.06 * (hash01(s + k, 5) - 0.5), pz = 0.5 + 0.12 * (hash01(s + k, 6) - 0.5), top = 0.55 + 0.25 * hash01(s + k, 7);
			const double w = (0.06 + 0.03 * hash01(s + k, 8)) * (1.15 - 0.4 * y) * (0.8 + 0.4 * clumps);
			col = std::max(col, (1.0 - smoothstep(0.7 * w, w, std::hypot(x - px - 0.05 * std::sin(y * 7.0 + k), z - pz))) * (1.0 - smoothstep(top - 0.06, top + 0.02, y)));
		}
		dust = std::max(col, smoothstep(0.25, 0.1, y) * smoothstep(0.4, 0.8, clumps)) * (1.0 - smoothstep(0.9, 1.0, r));
		gas = shape * smoothstep(0.25, 0.7, clumps * 0.6 + wisps * 0.5) * (1.0 - 0.8 * dust);
	}
	const double hot = 1.0 - smoothstep(0.0, 0.7, r);   // (oxygen's teal near the hot middle, hydrogen's red outside)
	const Vec3 colour = lerp(Vec3{1.0, 0.12, 0.16}, Vec3{0.15, 0.85, 0.8}, 0.9 * hot) + Vec3{0.25, 0.05, 0.3} * (0.4 * wisps);
	const double S = n.size;
	Voxel v;
	v.sa = Vec3{1.0, 1.0, 1.0} * (gas * 0.6 / S) + Vec3{1.0, 1.08, 1.2} * (dust * 80.0 / S);   // (dust reddens what shines through it)
	v.ss = Vec3{1.0, 1.0, 1.0} * (gas * 0.08 / S + dust * 1.0 / S);
	v.le = gas > 0.0 ? colour * (gas / (gas + 13.3 * dust + 1e-9)) : Vec3{0.0, 0.0, 0.0};   // (the light its gas gives, per unit of its absorption: dust gives none)
	return v;
}
// A nebula as scene text: its grid (made once) in its box, the sphere it fills, and the few hot stars that light it from inside.
inline std::string nebulaText(const std::string &film, const Nebula &n) {
	char file[160];
	std::snprintf(file, sizeof file, "nebula_v4_%s_%s_%d_%.0f_%.4g.pbrt", n.name.c_str(), n.kind.c_str(), n.seed, n.size, n.brightness);
	const std::string name = "nebula_" + n.name;
	const int N = kNebulaN;
	const std::string path = rgbGridFile(film, file, name, N, N, N, 0.3, 0.012 * n.brightness,   // (calibrated by eye: a nebula then glows about as bright as a lit planet)
										 [&](int i, int j, int k) {
		return nebulaVoxel(n, (i + 0.5) / N, (j + 0.5) / N, (k + 0.5) / N);
	});
	const Vec3 lo = n.at - Vec3{0.5, 0.5, 0.5} * n.size;
	std::string out = "# a nebula (film_cosmos.h): " + n.name + "\n" + placedGrid(path, name, lo, Vec3{1.0, 1.0, 1.0} * n.size);
	for (int k = 0; k < 4; ++k) {   // its young stars, blue-white, near its middle
		const Vec3 p = n.at + Vec3{hash01(n.seed * 7 + k, 11) - 0.5, hash01(n.seed * 7 + k, 12) - 0.5, hash01(n.seed * 7 + k, 13) - 0.5} * (0.3 * n.size);
		out += emissiveSphere({-p.x, p.y, p.z}, 0.004 * n.size, space::blackbody(18000.0 + 12000.0 * hash01(n.seed * 7 + k, 14)) * (25.0 * n.brightness));
	}
	return out;
}

}  // namespace cosmos
}  // namespace cartoon
