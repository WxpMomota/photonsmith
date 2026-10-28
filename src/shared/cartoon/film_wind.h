#pragma once
// film_wind.h -- the wind for a film file (film_file.h `wind`, `tornado`):
//
//   wind      one wind over the whole set, a bearing and a speed: everything that moves with the air takes it unless its own line says otherwise - the grass and the
//             trees lean with it and its gusts sweep across them, the clouds and the fog banks drift with it, rain slants and snow streams, a volcano's ash column
//             leans, smoke from a fire bends away, the waves run with it, a storm blows with it (the film's parser hands it on: film_parse.h);
//   tornado   a funnel from the cloud base to the ground, twisting, that crosses the land from one point to another: it lowers from the cloud as it forms, its rope
//             leaning and snaking as it goes, a cloud of dust and debris churning round its foot (over water, a ring of spray: a waterspout), and it ropes out and
//             lifts at its end; heard as a roar.
//
// Coordinates are pbrt's, as a film file's.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "cartoon_lib.h"
#include "film_clouds.h"

namespace cartoon {
namespace wind {

// The wind: which way it blows (a compass bearing: the way it goes, 0 +z, 90 +x) and how fast (metres a second).
struct Wind {
	bool on = false;
	double toward = 90.0, speed = 6.0;
	Vec3 dir() const { return {std::sin(toward * kPi / 180.0), 0.0, std::cos(toward * kPi / 180.0)}; }
};
// A tornado: from where to where it crosses (pbrt's x z), when, how wide its funnel is at the ground, how high its cloud base is (above the ground), a seed.
struct Tornado {
	double x0 = 0.0, z0 = -600.0, x1 = 0.0, z1 = 0.0, t0 = 0.0, t1 = 30.0, width = 40.0, height = 700.0;
	int seed = 1;
};

// How formed the tornado is at t (0..1): lowering from the cloud over its first 6 s, roping out over its last 6.
inline double strength(const Tornado &k, double t) {
	if (t < k.t0 || t > k.t1) return 0.0;
	return smoothstep(k.t0, k.t0 + 6.0, t) * (1.0 - smoothstep(k.t1 - 6.0, k.t1, t));
}
// Where its foot is at t (pbrt's x z; y left 0): along its way, eased in and out, wandering.
inline Vec3 footAt(const Tornado &k, double t) {
	const double u = smoothstep(k.t0, k.t1, std::clamp(t, k.t0, k.t1)) * 0.7 + 0.3 * clamp01((t - k.t0) / std::max(1e-6, k.t1 - k.t0));
	const double wob = 0.6 * k.width;
	return {k.x0 + (k.x1 - k.x0) * u + wob * (clouds::detail::fbm3(t * 0.08, 1.0, 2.0, k.seed + 3) - 0.5) * 2.0, 0.0,
			k.z0 + (k.z1 - k.z0) * u + wob * (clouds::detail::fbm3(3.0, t * 0.08, 5.0, k.seed + 5) - 0.5) * 2.0};
}
namespace detail {
inline void put(std::string &out, double v) {
	char b[24];
	if (v < 1e-4) out += "0 ";
	else {
		std::snprintf(b, sizeof b, "%.3f ", v);
		out += b;
	}
}
// A uniform-grid medium in a box (corner `lo`, size `size`) and the sphere round it, as scene text.
inline std::string gridText(const std::string &name, Vec3 lo, Vec3 size, int nx, int ny, int nz, const std::string &dens, const char *sigmaA, const char *sigmaS, double g) {
	char buf[640];
	std::string out = "AttributeBegin\n";
	std::snprintf(buf, sizeof buf, "  Translate %.4f %.4f %.4f\n  Scale %.4f %.4f %.4f\n", lo.x, lo.y, lo.z, size.x, size.y, size.z);
	out += buf;
	std::snprintf(buf, sizeof buf, "  MakeNamedMedium \"%s\" \"string type\" [ \"uniformgrid\" ] \"integer nx\" [ %d ] \"integer ny\" [ %d ] \"integer nz\" [ %d ] \"rgb sigma_a\" [ %s ] \"rgb sigma_s\" [ %s ] \"float g\" [ %.2f ]\n",
				  name.c_str(), nx, ny, nz, sigmaA, sigmaS, g);
	out += buf;
	out += "    \"float density\" [ " + dens + "]\nAttributeEnd\n";
	const Vec3 c = lo + size * 0.5;
	std::snprintf(buf, sizeof buf, "AttributeBegin\n  Translate %.4f %.4f %.4f\n  Material \"interface\"\n  MediumInterface \"%s\" \"\"\n  Shape \"sphere\" \"float radius\" [ %.4f ]\nAttributeEnd\n", c.x, c.y,
				  c.z, name.c_str(), 0.5 * length(size) * 1.01);
	return out + buf;
}
}  // namespace detail

// The funnel's middle at height y above the ground (0..h): leaning and snaking, more so as it ropes out.
inline Vec3 funnelOffset(const Tornado &k, double y, double h, double t, double s) {
	const double v = y / h, bend = (0.12 + 0.35 * (1.0 - s)) * h * v * (1.0 - v);
	const double a = 6.28 * clouds::detail::fbm3(t * 0.05, v * 1.5, 1.0, k.seed + 7);
	return {bend * std::cos(a), 0.0, bend * std::sin(a)};
}
// The tornado at t (nature's `nt` for its twist), its foot on the ground `groundAt` (or `water`: on the sea at that level), as scene text: the funnel; the churning cloud
// round its foot (dust, or spray over water); the debris flung round it.
inline std::string tornadoText(const Tornado &k, int index, double t, double nt, const std::function<double(double, double)> &groundAt, double waterLevel) {
	const double s = strength(k, t);
	if (s <= 0.0) return std::string();
	const Vec3 foot0 = footAt(k, t);
	const bool water = waterLevel > -1e29 && groundAt(foot0.x, foot0.z) < waterLevel;
	const double ground = water ? waterLevel : groundAt(foot0.x, foot0.z), h = k.height, top = ground + h;
	const double r0 = 0.5 * k.width * (0.4 + 0.6 * s), rTop = 0.18 * h;
	const double reach = h * (1.0 - std::pow(1.0 - std::min(1.0, 1.3 * s), 2.0));   // (how far down from the cloud it has come)
	std::string out = "# a tornado (film_wind.h)\n";
	// the funnel: a twisting cone narrowing to a rope, in one grid from 0.12 h up to the cloud base
	{
		double off = 0.0;
		for (int j = 0; j <= 8; ++j) off = std::max(off, length(funnelOffset(k, h * j / 8.0, h, nt, s)));
		const double half = rTop + off + 0.5 * k.width, y0 = ground + 0.12 * h;
		const int nx = 40, ny = 64, nz = 40;
		std::string dens;
		dens.reserve(static_cast<std::size_t>(nx) * ny * nz * 6);
		for (int kz = 0; kz < nz; ++kz)
			for (int j = 0; j < ny; ++j)
				for (int i = 0; i < nx; ++i) {
					const double y = 0.12 * h + (j + 0.5) / ny * 0.88 * h, v = y / h;
					const Vec3 o = funnelOffset(k, y, h, nt, s);
					const double dx = ((i + 0.5) / nx - 0.5) * 2.0 * half - o.x, dz = ((kz + 0.5) / nz - 0.5) * 2.0 * half - o.z;
					const double rho = std::hypot(dx, dz), r = r0 + (rTop - r0) * std::pow(v, 2.4);
					const double theta = std::atan2(dz, dx) - nt * (1.5 + 3.0 * (1.0 - v));   // (twisting, faster low down)
					const double streak = clouds::detail::fbm3(3.0 * std::cos(theta) * rho / r, v * 6.0 - nt * 0.6, 3.0 * std::sin(theta) * rho / r, k.seed + 11);
					const double formed = smoothstep(h - reach - 0.05 * h, h - reach + 0.05 * h, y);   // (lowering from the cloud)
					const double d = smoothstep(r, 0.55 * r, rho * (0.8 + 0.5 * streak)) * formed * (0.6 + 0.8 * streak) * (0.5 + 0.5 * s);
					detail::put(dens, d);
				}
		out += detail::gridText("funnel_" + std::to_string(index), {foot0.x - half, y0, foot0.z - half}, {2.0 * half, top - y0, 2.0 * half}, nx, ny, nz, dens, "0.035 0.036 0.038",
								"0.04 0.041 0.043", 0.4);
	}
	// the cloud round its foot: dust and debris (brown), or spray (white) over water; below the funnel's grid, so the two never overlap
	if (reach > 0.85 * h) {
		const double R = 2.5 * k.width, H = 0.12 * h;
		const int nx = 40, ny = 16, nz = 40;
		std::string dens;
		for (int kz = 0; kz < nz; ++kz)
			for (int j = 0; j < ny; ++j)
				for (int i = 0; i < nx; ++i) {
					const double dx = ((i + 0.5) / nx - 0.5) * 2.0 * R, dz = ((kz + 0.5) / nz - 0.5) * 2.0 * R, y = (j + 0.5) / ny * H;
					const double rho = std::hypot(dx, dz), theta = std::atan2(dz, dx) - nt * 2.5;
					const double billow = clouds::detail::fbm3(2.5 * std::cos(theta) * rho / R + 5.0, y / H * 3.0 - nt * 0.5, 2.5 * std::sin(theta) * rho / R, k.seed + 13);
					const double ring = smoothstep(R, 0.5 * R, rho * (0.7 + 0.6 * billow)) * smoothstep(0.0, 0.25 * k.width, rho);   // (the eye of it clearer)
					const double low = 1.0 - smoothstep(0.3 * H, H, y * (0.7 + 0.6 * billow));
					detail::put(dens, ring * low * (0.5 + billow) * smoothstep(0.85, 1.0, reach / h));
				}
		out += water ? detail::gridText("spray_" + std::to_string(index), {foot0.x - R, ground, foot0.z - R}, {2.0 * R, H, 2.0 * R}, nx, ny, nz, dens, "0.001 0.001 0.001",
										"0.05 0.05 0.05", 0.6)
					 : detail::gridText("dust_" + std::to_string(index), {foot0.x - R, ground - 1.0, foot0.z - R}, {2.0 * R, H + 1.0, 2.0 * R}, nx, ny, nz, dens, "0.03 0.04 0.055",
										"0.03 0.028 0.025", 0.3);
		if (!water) {   // debris: dark scraps whirled round it, climbing and flung out
			Mesh m;
			for (int n = 0; n < 240; ++n) {
				const double u = std::fmod(nt * (0.12 + 0.1 * hash01(n, 3)) + hash01(n, 4), 1.0);
				const double rr = (0.6 + 2.0 * hash01(n, 5)) * k.width * (0.5 + u), a = 6.28 * hash01(n, 6) + nt * 40.0 / std::max(5.0, rr);
				const double y = ground + 0.3 * h * u * (0.4 + 0.6 * hash01(n, 7));
				const Vec3 o = funnelOffset(k, y - ground, h, nt, s);
				const Vec3 c{foot0.x + o.x + rr * std::cos(a), y, foot0.z + o.z + rr * std::sin(a)};
				const double z = 0.4 + 1.6 * hash01(n, 8), sp = nt * 5.0 + n;
				const Vec3 e1 = normalize(Vec3{std::cos(sp), std::sin(1.3 * sp), std::sin(sp)}) * z, e2 = normalize(cross(e1, Vec3{0.3, 1.0, 0.2})) * (0.6 * z);
				const int b = static_cast<int>(m.verts.size());
				for (const Vec3 &p : {c - e1 - e2, c + e1 - e2, c + e1 + e2, c - e1 + e2}) m.verts.push_back(p), m.normals.push_back(normalize(cross(e1, e2)));
				m.tris.push_back({b, b + 1, b + 2});
				m.tris.push_back({b, b + 2, b + 3});
			}
			std::string shapes;
			m.appendPbrt(shapes, identity(), false);
			out += attr("Material \"diffusetransmission\" \"rgb reflectance\" [ 0.12 0.1 0.08 ] \"rgb transmittance\" [ 0.05 0.04 0.03 ]", shapes);
		}
	}
	return out;
}

}  // namespace wind
}  // namespace cartoon
