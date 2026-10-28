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
#include <array>
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

// ---- galaxies ----
// A galaxy: its kind (spiral: two arms of young blue stars and pink knots of glowing gas winding out from a warm yellow bulge, dust lanes along their inner edges;
// barred: the same, its arms starting from the ends of a bar; elliptical: a smooth yellow glow, densest in the middle), how big (across its disc), how its disc is
// tipped from facing the camera (`tilt`, about x) and turned in its plane (`turn`), how bright.
struct Galaxy {
	std::string name, kind = "spiral";
	Vec3 at{0.0, 0.0, -60000.0};
	double size = 40000.0, tilt = 60.0, turn = 0.0, brightness = 1.0;
	int seed = 1;
};
inline const std::vector<std::string> &galaxyKinds() {
	static const std::vector<std::string> k = {"spiral", "barred", "elliptical"};
	return k;
}
// The galaxy's disc coordinates of a point p of its box (unit-box coordinates centred, the box's half-sizes `half`, in units of the galaxy's radius): (u, v) in its
// plane, h out of it.
inline void galaxyFrame(const Galaxy &g, Vec3 p, double &u, double &v, double &h) {
	const double ti = g.tilt * kPi / 180.0, tu = g.turn * kPi / 180.0;
	// undo the tilt about x (the disc is the x-y plane facing +z untilted, so a camera looking down -z sees it face on), then the turn about the disc's axis
	const double y1 = p.y * std::cos(ti) + p.z * std::sin(ti), z1 = -p.y * std::sin(ti) + p.z * std::cos(ti);
	u = p.x * std::cos(tu) + y1 * std::sin(tu), v = -p.x * std::sin(tu) + y1 * std::cos(tu), h = z1;
}
// A galaxy's voxel at disc coordinates (u, v, h) (its radius 1), per metre for a galaxy `size` across.
inline Voxel galaxyVoxel(const Galaxy &g, double u, double v, double h) {
	const int s = g.seed * 131;
	const double r = std::hypot(u, v), th = std::atan2(v, u);
	const double bulge = std::exp(-std::pow(r / 0.13, 2.0) - std::pow(h / 0.09, 2.0));
	Voxel out{};
	if (g.kind == "elliptical") {
		const double rr = std::hypot(r, h / 0.6), e = 3.0 * std::exp(-rr / 0.16) * (1.0 - smoothstep(0.6, 1.0, rr));   // (fading out well inside its box)
		out.sa = Vec3{1.0, 1.0, 1.0} * (e * 0.5 / g.size);   // (not much thinner: the GPU's few collisions in a faint glow come out blotchy)
		out.le = Vec3{1.0, 0.82, 0.6} * (e > 0.003 ? 1.0 : 0.0);
		return out;
	}
	const double disc = std::exp(-r / 0.32) * std::exp(-std::pow(h / (0.035 + 0.02 * r), 2.0)) * (1.0 - smoothstep(0.85, 1.0, r));
	// the arms: logarithmic spirals (two of them), from the bulge's edge, or (barred) from the bar's ends
	const double start = g.kind == "barred" ? 0.22 : 0.08, lr = std::log(std::max(start, r) / start);
	const double phase = th - 3.2 * lr + (g.kind == "barred" ? 0.0 : 0.6);
	const double arm = std::pow(0.5 + 0.5 * std::cos(2.0 * phase), 3.0) * smoothstep(start * 0.7, start * 1.4, r);
	const double lane = std::pow(0.5 + 0.5 * std::cos(2.0 * phase - 0.7), 10.0) * smoothstep(start, start * 2.0, r) * (1.0 - smoothstep(0.7, 0.95, r));   // (dust on the arms' inner edges)
	const double bar = g.kind == "barred" ? std::exp(-std::pow(u / 0.22, 4.0) - std::pow(v / 0.05, 2.0) - std::pow(h / 0.05, 2.0)) : 0.0;
	const double grain = clouds::detail::fbm3(u * 18.0 + 3.0, v * 18.0, h * 18.0, s + 1);
	const double knots = smoothstep(0.62, 0.8, clouds::detail::fbm3(u * 30.0, v * 30.0 - 4.0, h * 30.0, s + 2)) * arm;   // (pink knots of glowing gas)
	const double stars = disc * (0.25 + 1.6 * arm * (0.6 + 0.8 * grain)) + 1.6 * bulge + bar;
	const double dust = disc * lane * 3.0;
	const Vec3 warm{1.0, 0.8, 0.55}, young{0.6, 0.75, 1.0}, pink{1.0, 0.35, 0.55};
	const Vec3 colour = (warm * (1.6 * bulge + bar + 0.3 * disc) + young * (1.6 * disc * arm) + pink * (2.0 * knots * disc)) * (1.0 / std::max(1e-9, stars + 2.0 * knots * disc));
	out.sa = Vec3{1.0, 1.0, 1.0} * (stars * 0.4 / g.size) + Vec3{1.0, 1.15, 1.3} * (dust * 6.0 / g.size);
	out.ss = Vec3{1.0, 1.0, 1.0} * (dust * 1.0 / g.size);
	out.le = stars > 1e-4 ? colour * (stars / (stars + 15.0 * dust + 1e-9)) * (1.0 + 2.0 * knots) : Vec3{0.0, 0.0, 0.0};
	return out;
}
// A galaxy as scene text: its grid in the box round its (tipped) disc, made once, and the sphere it fills.
inline std::string galaxyText(const std::string &film, const Galaxy &g) {
	const double R = 0.5 * g.size, ti = g.tilt * kPi / 180.0, thick = g.kind == "elliptical" ? 0.75 : 0.12;   // (the disc's half-thickness, in radii)
	const Vec3 half{1.0, std::abs(std::sin(ti)) + thick * std::abs(std::cos(ti)), std::abs(std::cos(ti)) + thick * std::abs(std::sin(ti))};   // (in radii)
	const double longest = std::max({half.x, half.y, half.z});
	const int nx = std::max(8, static_cast<int>(144 * half.x / longest)), ny = std::max(8, static_cast<int>(144 * half.y / longest)), nz = std::max(8, static_cast<int>(144 * half.z / longest));
	char file[200];
	std::snprintf(file, sizeof file, "galaxy_v4_%s_%s_%d_%.1f_%.1f_%.0f_%.4g.pbrt", g.name.c_str(), g.kind.c_str(), g.seed, g.tilt, g.turn, g.size, g.brightness);
	const std::string name = "galaxy_" + g.name;
	const std::string path = rgbGridFile(film, file, name, nx, ny, nz, 0.4, 0.04 * g.brightness, [&](int i, int j, int k) {
		const Vec3 p{((i + 0.5) / nx * 2.0 - 1.0) * half.x, ((j + 0.5) / ny * 2.0 - 1.0) * half.y, ((k + 0.5) / nz * 2.0 - 1.0) * half.z};
		double u = 0.0, v = 0.0, h = 0.0;
		galaxyFrame(g, p, u, v, h);
		return galaxyVoxel(g, u, v, h);
	});
	return "# a galaxy (film_cosmos.h): " + g.name + "\n" + placedGrid(path, name, g.at - half * R, half * (2.0 * R));
}

// ---- stars ----
// A star close up: its kind (or its own temperature), where, how big, how bright; its prominences (arcs of glowing plasma looping off its limb) and the flares that
// burst from it at their times.
struct Star {
	std::string name, kind = "sun";
	Vec3 at{0.0, 0.0, -30000.0};
	double radius = 5000.0, temperature = 0.0, brightness = 1.0;
	int prominences = 0;
	std::vector<double> flares;   // (nature's times)
	int seed = 1;
	double kelvin() const {
		if (temperature > 0.0) return temperature;
		return kind == "red" ? 3200.0 : kind == "orange" ? 4500.0 : kind == "white" ? 9500.0 : kind == "blue" ? 26000.0 : 5800.0;
	}
};
inline const std::vector<std::string> &starKinds() {
	static const std::vector<std::string> k = {"sun", "red", "orange", "white", "blue"};
	return k;
}
// A star's surface (relative brightness, about 1) at the unit direction o of its own frame: granulation (bright cells with darker lanes between), sunspots (a dark
// umbra in a lighter penumbra) in two belts either side of its equator, bright faculae round them.
inline double starSurfaceAt(const Star &s, Vec3 o) {
	const int sd = s.seed * 151;
	const double cells = clouds::detail::fbm3(o.x * 60.0, o.y * 60.0, o.z * 60.0, sd + 1);
	double granulation = 0.82 + 0.3 * smoothstep(0.35, 0.65, cells);
	double spot = 0.0;
	for (int k = 0; k < 9; ++k) {
		const double lat = (hash01(sd + k, 2) < 0.5 ? 1.0 : -1.0) * (8.0 + 22.0 * hash01(sd + k, 3)) * kPi / 180.0, lon = 2.0 * kPi * hash01(sd + k, 4);
		const Vec3 c{std::cos(lat) * std::cos(lon), std::cos(lat) * std::sin(lon), std::sin(lat)};
		const double r = (0.015 + 0.035 * hash01(sd + k, 5)) * (0.85 + 0.3 * clouds::detail::fbm3(o.x * 30.0, o.y * 30.0, o.z * 30.0, sd + k)),
					 d = std::acos(std::clamp(dot(o, c), -1.0, 1.0)) / r;
		spot = std::max(spot, d < 0.45 ? 0.85 : d < 1.0 ? 0.45 : 0.0);
		granulation += 0.12 * std::exp(-std::pow((d - 1.6) / 0.6, 2.0));   // (faculae)
	}
	return granulation * (1.0 - spot);
}
// A star's surface picture (its brightness in its temperature's colour), painted once.
inline std::string starPicture(const std::string &film, const Star &s) {
	const int W = 1024, H = 512;
	const Vec3 colour = space::blackbody(s.kelvin());
	return film_sky::picture(film, "star_v1_" + std::to_string(s.seed) + "_" + std::to_string(static_cast<int>(s.kelvin())), W, H, [&](std::vector<float> &rgb) {
		for (int j = 0; j < H; ++j)
			for (int i = 0; i < W; ++i) {
				const double theta = (j + 0.5) / H * kPi, phi = (i + 0.5) / W * 2.0 * kPi;
				const double v = starSurfaceAt(s, {std::sin(theta) * std::cos(phi), std::sin(theta) * std::sin(phi), std::cos(theta)});
				const std::size_t q = (static_cast<std::size_t>(j) * W + i) * 3;
				rgb[q] = static_cast<float>(colour.x * v), rgb[q + 1] = static_cast<float>(colour.y * v), rgb[q + 2] = static_cast<float>(colour.z * v);
			}
	});
}
// The star's glow (a halo round it, as its corona and the camera's eye would show it): a small grid that gives out light falling off with height above its surface,
// streaked a little.
inline Voxel starGlowVoxel(const Star &s, double x, double y, double z) {
	const double r = std::sqrt(x * x + y * y + z * z);   // (in star radii)
	if (r < 0.98 || r > 3.0) return Voxel{};
	const double a = std::atan2(y, x), streak = 0.7 + 0.6 * nature::fbm2(std::cos(a) * 3.0 + 1.0, std::sin(a) * 3.0 + z, s.seed + 9);
	const double glow = std::pow(std::max(1.0, r), -4.0) * streak * (1.0 - smoothstep(2.4, 3.0, r));
	Voxel v;
	v.sa = Vec3{1.0, 1.0, 1.0} * (0.02 / s.radius);
	v.le = space::blackbody(s.kelvin()) * glow;
	return v;
}
// Prominence k of a star at nature's time `nt`: an arc of plasma rising off the limb and falling back, its ends on the surface, swaying - as a tube (story space mesh).
inline Mesh prominence(const Star &s, int k, double nt) {
	const int sd = s.seed * 171 + k;
	const double lat = (hash01(sd, 1) - 0.5) * 1.2, lon = 2.0 * kPi * hash01(sd, 2), span = 0.12 + 0.18 * hash01(sd, 3), height = 0.15 + 0.35 * hash01(sd, 4);
	const Vec3 c{std::cos(lat) * std::cos(lon), std::sin(lat), std::cos(lat) * std::sin(lon)};
	const Vec3 t0 = normalize(cross(c, Vec3{0.0, 1.0, 0.0}) + Vec3{1e-9, 0.0, 0.0}), t1 = cross(c, t0);
	const double turn = 2.0 * kPi * hash01(sd, 6);
	const Vec3 t = t0 * std::cos(turn) + t1 * std::sin(turn);   // (which way its feet lie on the surface)
	std::vector<Vec3> pts;
	for (int i = 0; i <= 24; ++i) {
		const double u = i / 24.0, a = (u - 0.5) * span * 2.0, rise = std::sin(kPi * u) * height * (1.0 + 0.15 * std::sin(0.3 * nt + k));
		const Vec3 base = normalize(c * std::cos(a) + t * std::sin(a));
		const Vec3 q = s.at + base * (s.radius * (0.98 + rise)) + c * (0.01 * s.radius * std::sin(5.0 * u + 0.5 * nt + k));
		pts.push_back({-q.x, q.y, q.z});
	}
	return sdf::tubeMesh(pts, s.radius * (0.012 + 0.012 * hash01(sd, 5)), 0.6, 8);
}
// A star at nature's time `nt` as scene text: its glowing surface (turning slowly), its glow, its prominences, and any flare under way - a blaze on its surface and a
// cloud of plasma thrown off it, swelling and fading over a minute.
inline std::string starText(const std::string &film, const Star &s, double nt) {
	char buf[400];
	std::string out = "# a star (film_cosmos.h): " + s.name + "\nAttributeBegin\n";
	std::snprintf(buf, sizeof buf, "  Translate %.4f %.4f %.4f\n  Rotate %.4f 0 1 0\n  Rotate -90 1 0 0\n", s.at.x, s.at.y, s.at.z, std::fmod(nt * 0.5, 360.0));
	out += buf;
	std::snprintf(buf, sizeof buf, "  AreaLightSource \"diffuse\" \"string filename\" [ \"%s\" ] \"float scale\" [ %.4f ]\n", starPicture(film, s).c_str(), 1.0 * s.brightness);   // (not so bright that its colour and its spots burn out to white)
	out += buf;
	std::snprintf(buf, sizeof buf, "  Material \"diffuse\" \"rgb reflectance\" [ 0 0 0 ]\n  Shape \"sphere\" \"float radius\" [ %.4f ]\nAttributeEnd\n", s.radius);
	out += buf;
	// its glow
	const int N = 48;
	char file[160];
	std::snprintf(file, sizeof file, "starglow_v1_%s_%d_%.0f_%.4g.pbrt", s.name.c_str(), s.seed, s.kelvin(), s.brightness);
	const std::string name = "glow_" + s.name;
	const std::string path = rgbGridFile(film, file, name, N, N, N, 0.0, 0.06 * s.brightness, [&](int i, int j, int k) {
		return starGlowVoxel(s, ((i + 0.5) / N * 2.0 - 1.0) * 3.0, ((j + 0.5) / N * 2.0 - 1.0) * 3.0, ((k + 0.5) / N * 2.0 - 1.0) * 3.0);
	});
	out += placedGrid(path, name, s.at - Vec3{3.0, 3.0, 3.0} * s.radius, Vec3{6.0, 6.0, 6.0} * s.radius);
	// prominences
	if (s.prominences > 0) {
		Mesh all;
		for (int k = 0; k < s.prominences; ++k) {
			const Mesh m = prominence(s, k, nt);
			const int base = static_cast<int>(all.verts.size());
			all.verts.insert(all.verts.end(), m.verts.begin(), m.verts.end());
			all.normals.insert(all.normals.end(), m.normals.begin(), m.normals.end());
			for (const auto &tr : m.tris) all.tris.push_back({tr[0] + base, tr[1] + base, tr[2] + base});
		}
		const Vec3 L = Vec3{1.0, 0.16, 0.05} * (1.4 * s.brightness);   // (deep red: brighter and it burns out to pale yellow)
		out += "# its prominences\n" + attr(diffuseMaterial({0.0, 0.0, 0.0}), emitMesh(all, identity()), &L);
	}
	// flares
	for (std::size_t k = 0; k < s.flares.size(); ++k) {
		const double since = nt - s.flares[k];
		if (since < 0.0 || since > 60.0) continue;
		const int sd = s.seed * 191 + static_cast<int>(k);
		const double lat = (hash01(sd, 1) - 0.5) * 0.8, lon = 2.0 * kPi * hash01(sd, 2);
		const Vec3 c{std::cos(lat) * std::cos(lon), std::sin(lat), std::cos(lat) * std::sin(lon)};
		const double blaze = std::exp(-since / 4.0) * smoothstep(0.0, 0.5, since);
		const Vec3 spot = s.at + c * (s.radius * 1.002);
		out += "# a flare\n" + emissiveSphere({-spot.x, spot.y, spot.z}, s.radius * 0.04 * (1.0 + 0.5 * since / 4.0), Vec3{1.0, 0.95, 0.85} * (30.0 * blaze * s.brightness));
		// the cloud of plasma thrown off: a swelling, fading shell of glowing blobs
		const double reach = s.radius * (1.0 + 0.08 * since), fade = std::exp(-since / 25.0) * smoothstep(0.0, 2.0, since);
		if (fade < 0.01) continue;
		const Vec3 t1 = normalize(cross(c, Vec3{0.0, 1.0, 0.0}) + Vec3{1e-9, 0.0, 0.0}), t2 = cross(c, t1);
		for (int b = 0; b < 40; ++b) {
			const double a = 2.0 * kPi * hash01(sd * 31 + b, 3), spread = 0.35 * std::sqrt(hash01(sd * 31 + b, 4));
			const Vec3 d = normalize(c + (t1 * std::cos(a) + t2 * std::sin(a)) * spread), q = s.at + d * (reach * (0.97 + 0.1 * hash01(sd * 31 + b, 5)));
			out += emissiveSphere({-q.x, q.y, q.z}, s.radius * (0.025 + 0.02 * hash01(sd * 31 + b, 6)) * (1.0 + 0.06 * since), Vec3{1.0, 0.35, 0.12} * (1.6 * fade * s.brightness));
		}
	}
	return out;
}

// ---- black holes ----
// A black hole: its shadow (`radius`: the black disc the camera sees), its accretion disc out to `disc` radii - streams of gas going round it, white-hot inside and
// orange-red further out, the inner ones faster, brighter and bluer on the side coming towards the camera - tipped from edge-on by `tilt` (about x) and turned by `turn`
// (about y); the far side of its disc bent by its gravity over the top of its shadow (and, fainter, under it), a thin bright ring hugging the shadow; with `jets L` two
// beams of plasma knots L radii long shooting out of its poles. Not a true lens: the stars behind it are not bent.
struct BlackHole {
	std::string name;
	Vec3 at{0.0, 0.0, -30000.0};
	double radius = 2000.0, disc = 8.0, tilt = 8.0, turn = 0.0, spin = 1.0, jets = 0.0, brightness = 1.0;
	int seed = 1;
};
// Its disc's frame (pbrt's): the axis n and two directions e1, e2 in its plane.
inline void blackHoleFrame(const BlackHole &b, Vec3 &n, Vec3 &e1, Vec3 &e2) {
	const double ti = b.tilt * kPi / 180.0, tu = b.turn * kPi / 180.0;
	n = {std::sin(tu) * std::sin(ti), std::cos(ti), std::cos(tu) * std::sin(ti)};
	e1 = {std::cos(tu), 0.0, -std::sin(tu)};
	e2 = cross(n, e1);
}
// One stream of the disc: its radius (in the hole's radii, from 3: the innermost stable orbit, to `disc`), where along its orbit it starts at nature's time `nt` and
// how long it is (radians), how thick and how high off the disc's plane (radii), how bright.
struct DiscStream {
	double r = 3.0, phi = 0.0, span = 0.5, thick = 0.04, height = 0.0, glow = 1.0;
};
inline DiscStream discStream(const BlackHole &b, int k, double nt) {
	const int sd = b.seed * 211 + k;
	DiscStream s;
	s.r = 3.0 + (b.disc - 3.0) * std::pow(hash01(sd, 1), 1.6);   // (more of them inside)
	const double omega = 0.6 * b.spin * std::pow(3.0 / s.r, 1.5);   // (an orbit's pace, Kepler's: ten seconds a turn at the inner edge)
	s.phi = 2.0 * kPi * hash01(sd, 2) + omega * nt;
	s.span = 0.6 + 1.2 * hash01(sd, 3);
	s.thick = (0.03 + 0.05 * hash01(sd, 4)) * std::pow(s.r / 3.0, 0.4);
	s.height = 0.06 * (hash01(sd, 5) - 0.5) * (s.r / 3.0);
	s.glow = (0.4 + hash01(sd, 6)) * std::pow(3.0 / s.r, 1.2);
	return s;
}
// The gas's temperature at radius r (radii): hottest at the inner edge.
inline double discKelvin(double r) { return 6500.0 * std::pow(3.0 / std::max(3.0, r), 0.6); }   // (warm white inside, orange out to red: a film's palette, not a real disc's X-ray heat)
// Doppler beaming: how much brighter (above 1) or dimmer gas moving round at radius r looks, its direction of travel `v` and the direction to the camera `c` (unit).
inline double beaming(double r, Vec3 v, Vec3 c) {
	const double beta = 0.45 * std::sqrt(3.0 / std::max(3.0, r)), gamma = 1.0 / std::sqrt(1.0 - beta * beta);
	return 1.0 / (gamma * (1.0 - beta * dot(v, c)));
}
// The point of the hole's frame at angle phi, radius r, height h (radii) - pbrt's.
inline Vec3 discPoint(const BlackHole &b, double phi, double r, double h) {
	Vec3 n, e1, e2;
	blackHoleFrame(b, n, e1, e2);
	return b.at + (e1 * std::cos(phi) + e2 * std::sin(phi)) * (r * b.radius) + n * (h * b.radius);
}
// A black hole at nature's time `nt`, seen from `eye` (pbrt's), as scene text.
inline std::string blackHoleText(const BlackHole &b, double nt, Vec3 eye) {
	std::string out = "# a black hole (film_cosmos.h): " + b.name + "\n";
	char buf[320];
	std::snprintf(buf, sizeof buf, "AttributeBegin\n  Translate %.4f %.4f %.4f\n  Material \"diffuse\" \"rgb reflectance\" [ 0 0 0 ]\n  Shape \"sphere\" \"float radius\" [ %.4f ]\nAttributeEnd\n",
				  b.at.x, b.at.y, b.at.z, b.radius);
	out += "# its shadow\n" + std::string(buf);
	Vec3 n, e1, e2;
	blackHoleFrame(b, n, e1, e2);
	// the camera's view of it: w towards the camera, `up` the disc's axis as the camera sees it, `right` across
	const Vec3 w = normalize(eye - b.at + Vec3{1e-9, 0.0, 0.0});
	Vec3 up = n - w * dot(n, w);
	up = length(up) > 1e-6 ? normalize(up) : normalize(cross(w, e1));
	const Vec3 right = cross(up, w);
	const auto story = [](Vec3 p) { return Vec3{-p.x, p.y, p.z}; };
	const auto emit = [&](const std::vector<Vec3> &pts, double thick, Vec3 L, const char *what) {
		if (pts.size() < 2 || !(L.x + L.y + L.z > 1e-4)) return;
		out += std::string(what) + attr(diffuseMaterial({0.0, 0.0, 0.0}), emitMesh(sdf::tubeMesh(pts, thick * b.radius, 0.3, 6), identity()), &L);
	};
	const int streams = 260;
	for (int k = 0; k < streams; ++k) {
		const DiscStream s = discStream(b, k, nt);
		std::vector<Vec3> pts, bent;
		double beam = 0.0;
		bool bentDone = false;   // (one stretch behind the hole: its image is one arc)
		for (int i = 0; i <= 16; ++i) {
			const double phi = s.phi + s.span * i / 16.0;
			const Vec3 p = discPoint(b, phi, s.r, s.height), d = normalize(p - b.at - n * dot(p - b.at, n));
			pts.push_back(story(p));
			beam += beaming(s.r, cross(n, d) * (b.spin < 0.0 ? -1.0 : 1.0), normalize(eye - p)) / 17.0;
			// its lensed image: the part behind the hole (as the camera sees it) bent up over the shadow's top, its angle round the shadow from how far across it is
			const double across = dot(d, right), behind = -dot(d, w - n * dot(w, n));
			if (!bentDone && behind > 0.0) {
				const double psi = std::atan2(behind, across), ri = 1.15 + 0.12 * (s.r - 3.0);
				bent.push_back(story(b.at + (right * std::cos(psi) + up * std::sin(psi)) * (ri * b.radius) + w * (0.05 * b.radius)));
			} else if (!bentDone) {
				bentDone = bent.size() >= 2;
				if (!bentDone) bent.clear();
			}
		}
		const double boost = std::pow(std::clamp(beam, 0.5, 1.7), 3.0);
		const Vec3 colour = space::blackbody(discKelvin(s.r) * std::clamp(beam, 0.6, 1.6));
		emit(pts, s.thick, colour * (0.7 * s.glow * boost * b.brightness), "");   // (brighter and its colours burn out to white)
		emit(bent, s.thick * 0.6, colour * (0.6 * s.glow * b.brightness), "");
		if (bent.size() >= 2) {   // and its fainter second image, under the shadow
			std::vector<Vec3> under;
			for (const Vec3 &q : bent) {
				const Vec3 p = story(q) - b.at;
				under.push_back(story(b.at + right * dot(p, right) * 0.85 - up * dot(p, up) * 0.85 + w * (0.05 * b.radius)));
			}
			emit(under, s.thick * 0.4, colour * (0.25 * s.glow * b.brightness), "");
		}
	}
	// the photon ring: light that went round the hole, hugging its shadow
	std::vector<Vec3> ring;
	for (int i = 0; i <= 96; ++i) {
		const double a = 2.0 * kPi * i / 96.0;
		ring.push_back(story(b.at + (right * std::cos(a) + up * std::sin(a)) * (1.03 * b.radius) + w * (0.05 * b.radius)));
	}
	emit(ring, 0.012, space::blackbody(6000.0) * (1.2 * b.brightness), "# its photon ring\n");
	// jets: knots of plasma streaming out of its poles, swelling and fading as they go
	if (b.jets > 1.5) {
		for (int side = -1; side <= 1; side += 2)
			for (int k = 0; k < 40; ++k) {
				const double f = std::fmod(k / 40.0 + nt * 0.04 * b.spin / std::max(1.0, b.jets / 10.0) + 10.0, 1.0);
				const double along = 1.3 + (b.jets - 1.3) * f, wob = 0.15 * std::sin(nt * 0.7 + k);
				const Vec3 p = b.at + n * (side * along * b.radius) + e1 * (wob * f * b.radius);
				const double flick = 0.7 + 0.3 * std::sin(nt * 3.0 + k * 1.7);
				out += emissiveSphere(story(p), b.radius * (0.12 + 0.6 * f), Vec3{0.55, 0.7, 1.0} * (1.2 * (1.0 - f) * (1.0 - f) * flick * b.brightness));
			}
	}
	return out;
}

// ---- rocks in space ----
// A rock of space - a comet's nucleus, an asteroid - `size` across: a lumpy ball (pushed in and out by a noise over its whole surface, squashed a little along one axis),
// pocked with craters. Story space, centred on the origin.
inline Mesh spaceRock(int seed, double size, int nu = 28, int nv = 16) {
	Mesh m = uvSphere(1.0, nu, nv);
	const double squash = 0.65 + 0.3 * hash01(seed, 1);
	std::vector<Vec3> craters;
	for (int k = 0; k < 7; ++k) {
		const double z = 2.0 * hash01(seed * 7 + k, 2) - 1.0, a = 2.0 * kPi * hash01(seed * 7 + k, 3);
		craters.push_back({std::sqrt(1.0 - z * z) * std::cos(a), std::sqrt(1.0 - z * z) * std::sin(a), z});
	}
	for (Vec3 &v : m.verts) {
		const Vec3 o = normalize(v);
		double k = 0.72 + 0.55 * clouds::detail::fbm3(o.x * 1.6 + 3.0, o.y * 1.6, o.z * 1.6, seed + 11);
		for (std::size_t c = 0; c < craters.size(); ++c) {
			const double d = std::acos(std::clamp(dot(o, craters[c]), -1.0, 1.0)) / (0.18 + 0.25 * hash01(seed * 7 + static_cast<int>(c), 4));
			if (d < 1.3) k += d < 1.0 ? -0.08 * (1.0 - d * d) : 0.03 * (1.3 - d) / 0.3;   // (a bowl and its rim)
		}
		v = Vec3{o.x, o.y * squash, o.z} * (0.5 * size * k);
	}
	std::vector<Vec3> acc(m.verts.size(), Vec3{0.0, 0.0, 0.0});
	for (const auto &t : m.tris) {
		const Vec3 n = cross(m.verts[static_cast<std::size_t>(t[1])] - m.verts[static_cast<std::size_t>(t[0])], m.verts[static_cast<std::size_t>(t[2])] - m.verts[static_cast<std::size_t>(t[0])]);
		for (int c = 0; c < 3; ++c) acc[static_cast<std::size_t>(t[c])] = acc[static_cast<std::size_t>(t[c])] + n;
	}
	for (std::size_t i = 0; i < acc.size(); ++i) m.normals[i] = length(acc[i]) > 1e-12 ? normalize(acc[i]) : Vec3{0.0, 1.0, 0.0};
	return m;
}

// ---- comets ----
// A comet: its nucleus (a dark lumpy rock `size` across) where it is at nature's time 0 and how it moves (`move`, metres a second); its coma, a green-white glow round the
// nucleus; its two tails `tail` long, pointing away from the sun - the ion tail straight, thin, blue and streaked, the dust tail broad, warm white and curving back along the
// way it came. `away` is the way the tails point (pbrt's); zero: away from the space set's sun.
struct Comet {
	std::string name;
	Vec3 at{0.0, 0.0, -20000.0}, move{0.0, 0.0, 0.0}, away{0.0, 0.0, 0.0};
	double size = 300.0, tail = 20000.0, brightness = 1.0;
	int seed = 1;
};
// The comet's frame (pbrt's): X along its tails, Y the way its dust tail curves (back along its path), Z across.
inline void cometFrame(const Comet &c, Vec3 fromSun, Vec3 &X, Vec3 &Y, Vec3 &Z) {
	X = normalize(length(c.away) > 1e-9 ? c.away : fromSun);
	Vec3 back = c.move * -1.0 - X * dot(c.move * -1.0, X);
	if (length(back) < 1e-9) back = cross(X, std::abs(X.y) < 0.9 ? Vec3{0.0, 1.0, 0.0} : Vec3{1.0, 0.0, 0.0});
	Y = normalize(back);
	Z = cross(X, Y);
}
// The box round its glow, in its own frame and tail lengths: corner and size.
constexpr double kCometLo[3] = {-0.08, -0.12, -0.15}, kCometSize[3] = {1.08, 0.54, 0.3};
// The comet's glow at (x, y, z) of its own frame (in tail lengths): how much (0 up) and its colour.
inline void cometGlow(const Comet &c, double x, double y, double z, double &amount, Vec3 &colour) {
	const int s = c.seed * 223;
	const double r = std::sqrt(x * x + y * y + z * z), coma = 2.5 * std::exp(-r / 0.022);
	double ion = 0.0, dust = 0.0;
	if (x > 0.0) {
		const double wi = 0.005 + 0.03 * x, streak = 0.5 + 0.9 * clouds::detail::fbm3(x * 4.0, y * 70.0, z * 70.0, s + 1);
		ion = std::exp(-(y * y + z * z) / (wi * wi)) * std::exp(-1.6 * x) * streak * smoothstep(0.0, 0.04, x);
		const double dy = y - 0.3 * x * x, wd = 0.01 + 0.09 * x;
		dust = 1.3 * std::exp(-(dy * dy) / (wd * wd) - (z * z) / std::pow(0.5 * wd + 0.004, 2.0)) * std::exp(-2.4 * x) * (0.75 + 0.5 * clouds::detail::fbm3(x * 9.0, y * 9.0, z * 9.0, s + 2)) *
			   smoothstep(0.0, 0.02, x);
	}
	amount = coma + ion + dust;
	colour = amount > 1e-9 ? (Vec3{0.75, 1.0, 0.8} * coma + Vec3{0.35, 0.55, 1.0} * ion + Vec3{1.0, 0.88, 0.7} * dust) * (1.0 / amount) : Vec3{0.0, 0.0, 0.0};
	amount *= 1.0 - smoothstep(0.85, 1.0, x);
}
// A comet at nature's time `nt` as scene text, its tails pointing from `fromSun` (unit, pbrt's): its nucleus and its glow (a grid made once, placed where it is now).
inline std::string cometText(const std::string &film, const Comet &c, double nt, Vec3 fromSun) {
	Vec3 X, Y, Z;
	cometFrame(c, fromSun, X, Y, Z);
	const Vec3 at = c.at + c.move * nt;
	const double L = c.tail;
	std::string out = "# a comet (film_cosmos.h): " + c.name + "\n";
	out += attr(diffuseMaterial({0.05, 0.045, 0.04}), emitMesh(spaceRock(c.seed, c.size), mul(translate(-at.x, at.y, at.z), rotate({0.3, 1.0, 0.2}, std::fmod(nt * 3.0, 360.0)))));
	const int nx = 160, ny = 80, nz = 44;
	char file[200];
	std::snprintf(file, sizeof file, "comet_v3_%s_%d_%.0f_%.4g.pbrt", c.name.c_str(), c.seed, L, c.brightness);
	const std::string name = "comet_" + c.name;
	const std::string path = rgbGridFile(film, file, name, nx, ny, nz, 0.0, 0.025 * c.brightness, [&](int i, int j, int k) {
		const double x = kCometLo[0] + kCometSize[0] * (i + 0.5) / nx, y = kCometLo[1] + kCometSize[1] * (j + 0.5) / ny, z = kCometLo[2] + kCometSize[2] * (k + 0.5) / nz;
		double e = 0.0;
		Vec3 colour;
		cometGlow(c, x, y, z, e, colour);
		Voxel v;
		v.sa = Vec3{1.0, 1.0, 1.0} * (std::min(e, 3.0) * 15.0 / L);   // (about one across the tail: a thin glow is a few bright collisions, speckled on the GPU)
		v.le = e > 0.002 ? colour : Vec3{0.0, 0.0, 0.0};
		return v;
	});
	// the grid's unit box onto the comet's frame: the box's corner and its edges along X, Y, Z (pbrt takes the matrix by columns)
	const Vec3 o = at + (X * kCometLo[0] + Y * kCometLo[1] + Z * kCometLo[2]) * L, ex = X * (kCometSize[0] * L), ey = Y * (kCometSize[1] * L), ez = Z * (kCometSize[2] * L);
	char buf[520];
	std::snprintf(buf, sizeof buf, "AttributeBegin\n  ConcatTransform [ %.6g %.6g %.6g 0  %.6g %.6g %.6g 0  %.6g %.6g %.6g 0  %.6f %.6f %.6f 1 ]\n  Include \"%s\"\nAttributeEnd\n", ex.x, ex.y, ex.z,
				  ey.x, ey.y, ey.z, ez.x, ez.y, ez.z, o.x, o.y, o.z, path.c_str());
	out += buf;
	const Vec3 mid = o + (ex + ey + ez) * 0.5;
	std::snprintf(buf, sizeof buf, "AttributeBegin\n  Translate %.4f %.4f %.4f\n  Material \"interface\"\n  MediumInterface \"%s\" \"\"\n  Shape \"sphere\" \"float radius\" [ %.4f ]\nAttributeEnd\n", mid.x, mid.y,
				  mid.z, name.c_str(), 0.5 * length(ex + ey + ez) * 1.01);
	return out + buf;
}

// ---- asteroid belts ----
// Rocks by the thousand: a belt (`ring`: a band `width` wide round a centre at `radius`, a tenth as thick, its rocks going round at Kepler's pace - those inside faster -
// tipped by `tilt` about x and turned by `turn` about y) or a field (`field`: a ball of rocks `radius` round, drifting and tumbling) - `count` rocks, most of them small, the
// biggest `size` across, each a placement of one of eight rock shapes (pbrt objects), grey to brown.
struct Belt {
	std::string name, kind = "ring";
	Vec3 at{0.0, 0.0, -30000.0};
	double radius = 20000.0, width = 4000.0, size = 400.0, tilt = 0.0, turn = 0.0, spin = 1.0;
	int count = 3000, seed = 1;
};
inline const std::vector<std::string> &beltKinds() {
	static const std::vector<std::string> k = {"ring", "field"};
	return k;
}
constexpr int kBeltShapes = 8;
// Rock k of a belt at nature's time `nt`: where its centre is (pbrt's), how big, which shape, and its turn (an axis and degrees).
struct BeltRock {
	Vec3 at, axis;
	double size = 0.0, degrees = 0.0;
	int shape = 0;
};
inline BeltRock beltRock(const Belt &b, int k, double nt) {
	const int sd = b.seed * 307 + k * 13;
	BeltRock r;
	r.size = b.size * (0.12 + 0.88 * std::pow(hash01(sd, 1), 4.0));   // (many small, a few big)
	r.shape = static_cast<int>(hash01(sd, 2) * kBeltShapes) % kBeltShapes;
	r.axis = normalize(Vec3{hash01(sd, 3) - 0.5, hash01(sd, 4) - 0.5, hash01(sd, 5) - 0.5} + Vec3{1e-6, 0.0, 0.0});
	r.degrees = std::fmod(360.0 * hash01(sd, 6) + nt * (2.0 + 10.0 * hash01(sd, 7)) * b.spin, 360.0);
	if (b.kind == "field") {   // in a ball, drifting slowly
		const double z = 2.0 * hash01(sd, 8) - 1.0, a = 2.0 * kPi * hash01(sd, 9), rr = b.radius * std::cbrt(hash01(sd, 10));
		const Vec3 drift = Vec3{hash01(sd, 11) - 0.5, hash01(sd, 12) - 0.5, hash01(sd, 13) - 0.5} * (0.004 * b.radius * b.spin);
		r.at = b.at + Vec3{std::sqrt(1.0 - z * z) * std::cos(a), z, std::sqrt(1.0 - z * z) * std::sin(a)} * rr + drift * nt;
		return r;
	}
	const double rr = b.radius + b.width * (hash01(sd, 8) + hash01(sd, 9) - 1.0) * 0.5 * 1.4;   // (thicker in the middle of the band)
	const double omega = 2.0 * kPi / 600.0 * b.spin * std::pow(b.radius / std::max(1.0, rr), 1.5);   // (a turn in ten minutes at the band's middle)
	const double a = 2.0 * kPi * hash01(sd, 10) + omega * nt, h = b.width * 0.1 * (hash01(sd, 11) + hash01(sd, 12) - 1.0);
	const double ti = b.tilt * kPi / 180.0, tu = b.turn * kPi / 180.0;
	const Vec3 flat{rr * std::cos(a), h, rr * std::sin(a)};
	const Vec3 tipped{flat.x, flat.y * std::cos(ti) - flat.z * std::sin(ti), flat.y * std::sin(ti) + flat.z * std::cos(ti)};
	r.at = b.at + Vec3{tipped.x * std::cos(tu) + tipped.z * std::sin(tu), tipped.y, -tipped.x * std::sin(tu) + tipped.z * std::cos(tu)};
	return r;
}
// A belt at nature's time `nt` as scene text: its eight rock shapes as objects (a unit across), then a placement of one for each rock.
inline std::string beltText(const Belt &b, double nt) {
	std::string out = "# an asteroid belt (film_cosmos.h): " + b.name + "\n";
	for (int s = 0; s < kBeltShapes; ++s) {
		const double tone = hash01(b.seed * 17 + s, 1);
		const Vec3 colour = lerp(Vec3{0.2, 0.19, 0.18}, Vec3{0.32, 0.25, 0.18}, tone) * (0.8 + 0.4 * hash01(b.seed * 17 + s, 2));
		out += nature::objectBegin("belt_" + b.name + "_" + std::to_string(s)) + attr(diffuseMaterial(colour), emitMesh(spaceRock(b.seed * 31 + s, 1.0, 24, 14), identity())) + nature::objectEnd();
	}
	for (int k = 0; k < b.count; ++k) {
		const BeltRock r = beltRock(b, k, nt);
		out += nature::placeObject("belt_" + b.name + "_" + std::to_string(r.shape), mul(translate(-r.at.x, r.at.y, r.at.z), rotate(r.axis, r.degrees), scale(r.size, r.size, r.size)));
	}
	return out;
}

// ---- dying stars ----
// What a star leaves when it dies, `size` across: a `supernova` - a red giant that blows up at `blast T` (nature's time) in a white flash that fades over seconds, its shell of
// glowing gas (red outside, blue-white inside, torn into filaments) racing out to its size and slowing; a `pulsar` - a spinning neutron star whose two beams of light sweep
// round like a lighthouse's (`period` seconds a turn), in a small glowing cloud of its wind (a ring round its waist and jets out of its poles); or a `planetary` nebula - a
// glowing shell blown off a dying star, an hourglass of teal gas rimmed with red round a tiny white-hot dwarf.
struct Remnant {
	std::string name, kind = "supernova";
	Vec3 at{0.0, 0.0, -30000.0};
	double size = 10000.0, blast = 0.0, period = 4.0, tilt = 20.0, brightness = 1.0;
	int seed = 1;
};
inline const std::vector<std::string> &remnantKinds() {
	static const std::vector<std::string> k = {"supernova", "pulsar", "planetary"};
	return k;
}
// A remnant's glow at (x, y, z) of its box (-1 .. 1 each way, its radius 1): how much and its colour.
inline void remnantGlow(const Remnant &m, double x, double y, double z, double &amount, Vec3 &colour) {
	const int s = m.seed * 331;
	amount = 0.0, colour = {0.0, 0.0, 0.0};
	if (m.kind == "supernova") {
		const double r = std::sqrt(x * x + y * y + z * z), warp = 0.12 * (clouds::detail::fbm3(x * 2.5, y * 2.5, z * 2.5, s + 1) - 0.5);
		const double shell = std::exp(-std::pow((r + warp - 0.82) / 0.09, 2.0));
		const double strands = std::pow(detail::ridge(clouds::detail::fbm3(x * 7.0, y * 7.0, z * 7.0, s + 2)), 4.0), knots = smoothstep(0.65, 0.8, clouds::detail::fbm3(x * 14.0, y * 14.0, z * 14.0, s + 3));
		amount = shell * (0.25 + 1.8 * strands + 1.2 * knots) + 0.15 * std::exp(-std::pow(r / 0.5, 2.0));   // (and a faint glow filling it)
		colour = lerp(Vec3{0.35, 0.6, 1.0}, Vec3{1.0, 0.25, 0.15}, smoothstep(0.7, 0.9, r + warp)) + Vec3{0.6, 0.55, 0.3} * knots;
	} else if (m.kind == "pulsar") {   // its wind: a ring round its waist and jets out of its poles, blue-white
		const double rho = std::hypot(x, z), ring = std::exp(-std::pow((rho - 0.4) / 0.1, 2.0) - std::pow(y / 0.06, 2.0)), jet = std::exp(-std::pow(rho / (0.05 + 0.1 * std::abs(y)), 2.0)) * (1.0 - smoothstep(0.6, 1.0, std::abs(y)));
		const double fog = std::exp(-std::pow(std::sqrt(x * x + y * y + z * z) / 0.45, 2.0)) * (0.5 + clouds::detail::fbm3(x * 5.0, y * 5.0, z * 5.0, s + 4));
		amount = 1.2 * ring + 0.9 * jet + 0.35 * fog;
		colour = Vec3{0.55, 0.7, 1.0} + Vec3{0.3, 0.1, 0.0} * fog;
	} else {   // planetary: an hourglass shell
		const double rr = std::sqrt(x * x + z * z + std::pow(1.3 * y / (1.0 + 0.7 * std::abs(y)), 2.0)) / 0.55, warp = 0.08 * (clouds::detail::fbm3(x * 4.0, y * 4.0, z * 4.0, s + 5) - 0.5);
		const double shell = std::exp(-std::pow((rr + warp - 1.0) / 0.13, 2.0)), inside = 0.25 * (1.0 - smoothstep(0.6, 1.0, rr));
		amount = shell * (0.8 + 0.6 * clouds::detail::fbm3(x * 9.0, y * 9.0, z * 9.0, s + 6)) + inside;
		colour = lerp(Vec3{0.2, 0.85, 0.8}, Vec3{1.0, 0.2, 0.25}, smoothstep(0.95, 1.12, rr + warp));
	}
	amount *= 1.0 - smoothstep(0.92, 1.0, std::max({std::abs(x), std::abs(y), std::abs(z)}));
}
// A pulsar's beams at nature's time `nt`: two cones of light out of its poles, sweeping round its spin axis (story space mesh).
inline Mesh pulsarBeams(const Remnant &m, double nt) {
	const double ti = m.tilt * kPi / 180.0, a = 2.0 * kPi * nt / std::max(0.05, m.period);
	const Vec3 axis = normalize(Vec3{std::sin(ti) * std::cos(a), std::cos(ti), std::sin(ti) * std::sin(a)});
	const Vec3 u = normalize(cross(axis, Vec3{1.0, 0.0, 0.0}) + Vec3{0.0, 0.0, 1e-9}), v = cross(axis, u);
	const double length = 0.9 * m.size, spread = 0.04 * length;
	Mesh out;
	for (int side = -1; side <= 1; side += 2) {
		const int base = static_cast<int>(out.verts.size());
		const Vec3 apex = m.at + axis * (side * 0.004 * m.size), end = m.at + axis * (side * length);
		out.verts.push_back({-apex.x, apex.y, apex.z});
		out.normals.push_back(axis * static_cast<double>(side));
		for (int i = 0; i < 16; ++i) {
			const double t = 2.0 * kPi * i / 16.0;
			const Vec3 d = u * std::cos(t) + v * std::sin(t), p = end + d * spread;
			out.verts.push_back({-p.x, p.y, p.z});
			const Vec3 nn = normalize(d);
			out.normals.push_back({-nn.x, nn.y, nn.z});
		}
		for (int i = 0; i < 16; ++i) {
			const int p0 = base + 1 + i, p1 = base + 1 + (i + 1) % 16;
			out.tris.push_back(side > 0 ? std::array<int, 3>{base, p1, p0} : std::array<int, 3>{base, p0, p1});
		}
	}
	return out;
}
// A remnant at nature's time `nt` as scene text.
inline std::string remnantText(const std::string &film, const Remnant &m, double nt) {
	std::string out = "# a " + m.kind + " (film_cosmos.h): " + m.name + "\n";
	double R = 0.5 * m.size;   // (how far its glow reaches now)
	if (m.kind == "supernova") {
		const double since = nt - m.blast;
		if (since < 0.0) {   // the red giant before it
			return out + emissiveSphere({-m.at.x, m.at.y, m.at.z}, 0.03 * m.size, space::blackbody(3400.0) * (1.5 * m.brightness));
		}
		const double flash = std::exp(-since / 5.0) * smoothstep(0.0, 0.4, since);
		if (flash > 0.01) out += "# its flash\n" + emissiveSphere({-m.at.x, m.at.y, m.at.z}, m.size * (0.02 + 0.05 * flash), Vec3{1.0, 0.97, 0.92} * (40.0 * flash * m.brightness));
		R *= 1.0 - std::exp(-(since + 0.05) / 10.0);   // (racing out, slowing)
		if (R < 0.01 * m.size) return out;
	} else if (m.kind == "pulsar") {
		out += emissiveSphere({-m.at.x, m.at.y, m.at.z}, 0.006 * m.size, Vec3{0.8, 0.9, 1.0} * (8.0 * m.brightness));
		const Vec3 L = Vec3{0.6, 0.75, 1.0} * (0.9 * m.brightness);
		out += "# its beams\n" + attr(diffuseMaterial({0.0, 0.0, 0.0}), emitMesh(pulsarBeams(m, nt), identity()), &L);
		R *= 0.6;   // (its wind's cloud is smaller than the reach of its beams)
	} else {
		out += emissiveSphere({-m.at.x, m.at.y, m.at.z}, 0.008 * m.size, space::blackbody(40000.0) * (6.0 * m.brightness));
	}
	const int N = 96;
	char file[200];
	std::snprintf(file, sizeof file, "remnant_v3_%s_%s_%d_%.0f_%.4g.pbrt", m.name.c_str(), m.kind.c_str(), m.seed, m.size, m.brightness);
	const std::string name = "remnant_" + m.name;
	const std::string path = rgbGridFile(film, file, name, N, N, N, 0.0, 0.006 * m.brightness, [&](int i, int j, int k) {
		double e = 0.0;
		Vec3 colour;
		remnantGlow(m, (i + 0.5) / N * 2.0 - 1.0, (j + 0.5) / N * 2.0 - 1.0, (k + 0.5) / N * 2.0 - 1.0, e, colour);
		Voxel v;
		v.sa = Vec3{1.0, 1.0, 1.0} * (std::min(e, 3.0) * 12.0 / m.size);   // (per metre at its full size: about one through its shell)
		v.le = e > 0.003 ? colour * std::min(e, 3.0) : Vec3{0.0, 0.0, 0.0};   // (how much as well as what colour: where it is thick a glow shows only its light, so its filaments are in that)
		return v;
	});
	return out + placedGrid(path, name, m.at - Vec3{R, R, R}, Vec3{2.0 * R, 2.0 * R, 2.0 * R});
}

}  // namespace cosmos
}  // namespace cartoon
