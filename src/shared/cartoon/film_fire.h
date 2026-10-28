#pragma once
// film_fire.h -- fire, lava and smoke for a film file (film_file.h `fire`, `lava`, `volcano`, `wildfire`):
//
//   flames     a glowing volume (an "rgbgrid" medium whose voxels give out light, in a pass-through sphere): a teardrop of hot gas, widest low down, licking upward in tongues
//              that flicker and climb (3D noise moving up through it), white-yellow where it is hottest, orange, then deep red at its tips; a point light among them that
//              flickers with them lights what is round it, and sparks fly up (film_particles.h);
//   smoke      a dark volume ("uniformgrid": it absorbs more than it scatters) rising and spreading above a fire, billowing as it climbs;
//   lava       a glowing flow down the land from a vent: a ribbon in the lowest ground (as a river finds its way: film_water.h), its surface a crust of dark rock broken by
//              glowing cracks (an area light with a picture), creeping downhill;
//   volcano    a cone raised in the land with a crater at its top; erupting, it throws lava fountains and a towering plume of ash, glows in its crater, and lava runs down
//              its flanks; with a `column`, its ash climbs kilometres in a column that spreads into an umbrella, lightning flickers in it, ash falls round the camera,
//              and pyroclastic flows - avalanches of hot ash - race down its flanks;
//   wildfire   a line of flames sweeping across the land with the wind, smoke rising behind it.
//
// Flames and smoke are written into each frame (their grids change every frame); their spheres are kept apart, as a renderer follows one medium at a time, and the camera
// stays outside them. Coordinates are pbrt's, as a film file's.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "cartoon_lib.h"
#include "film_clouds.h"
#include "film_sky.h"
#include "nature_lib.h"

namespace cartoon {
namespace fire {

struct Fire {
	Vec3 at;              // the foot of the flames (pbrt's)
	double size = 1.2;    // how tall the flames are (metres)
	double t0 = 0.0, t1 = 1e30;
	bool smoke = true;
	int seed = 1;
};
struct Lava {
	double x = 0.0, z = 0.0, width = 6.0, speed = 0.4, t0 = 0.0;
	std::vector<Vec3> path;   // worked out when the film is loaded (on its terrain)
};
// A pyroclastic flow: when it starts down the volcano, the way it goes (a compass bearing, as `wind`; else a way of its own), and its path down the land from the crater's
// rim (worked out when the film is loaded).
struct PyroclasticFlow {
	double t = 0.0, toward = 0.0;
	bool towardSet = false;
	std::vector<Vec3> path;
};
struct Volcano {
	double x = 0.0, z = -1500.0, height = 450.0, radius = 900.0, t0 = 0.0, t1 = 1e30;
	double column = 0.0;    // an ash column this high above the crater, spreading into an umbrella (0: the plume of ash)
	double wind = 90.0;     // the way the column leans and its umbrella drifts (a compass bearing)
	int lightning = 0;      // strikes in the column
	double ashfall = 0.0;   // how much ash falls round the camera (as a `weather` amount)
	double rimY = 0.0;      // (the crater's rim: worked out when the film is loaded)
	bool windSet = false;   // (its line gave its own wind: else the film's)
	std::vector<PyroclasticFlow> flows;
};
struct Wildfire {
	double x0 = -200.0, z0 = -200.0, x1 = 200.0, z1 = 200.0, t0 = 0.0, t1 = 30.0, wind = 0.0;
	bool windSet = false;
};

namespace detail {
using clouds::detail::fbm3;
// The colour of fire at a heat from 0 to 1: deep red, orange, yellow, near white.
inline Vec3 heat(double h) {
	h = clamp01(h);
	if (h < 0.35) return lerp(Vec3{0.25, 0.02, 0.0}, Vec3{0.9, 0.18, 0.02}, h / 0.35);
	if (h < 0.7) return lerp(Vec3{0.9, 0.18, 0.02}, Vec3{1.0, 0.55, 0.1}, (h - 0.35) / 0.35);
	return lerp(Vec3{1.0, 0.55, 0.1}, Vec3{1.0, 0.92, 0.65}, (h - 0.7) / 0.3);
}
inline void put(std::string &out, double v) {
	char b[24];
	if (v < 1e-4) out += "0 ";
	else {
		std::snprintf(b, sizeof b, "%.3f ", v);
		out += b;
	}
}
}  // namespace detail

// A flame at t, as scene text: its medium (named `name`) in a box `size` tall at `foot` (pbrt's), the pass-through sphere it fills, and its flickering light.
inline std::string flameText(const std::string &name, Vec3 foot, double size, double t, int seed, int res = 20) {
	const int nx = res, ny = res * 2, nz = res;
	const double w = 0.7 * size;
	std::string sa, ss, le;
	sa.reserve(static_cast<std::size_t>(nx) * ny * nz * 12), ss.reserve(sa.capacity()), le.reserve(sa.capacity());
	double total = 0.0;
	for (int k = 0; k < nz; ++k)
		for (int j = 0; j < ny; ++j)
			for (int i = 0; i < nx; ++i) {
				const double x = (i + 0.5) / nx, y = (j + 0.5) / ny, z = (k + 0.5) / nz;
				// tongues: the gas pushed sideways by noise that climbs through it, more the higher it is
				const double climb = t * 2.4;
				const double dx = 0.45 * y * (detail::fbm3(x * 3.0, y * 3.5 - climb, z * 3.0, seed) - 0.5), dz = 0.45 * y * (detail::fbm3(x * 3.0 + 7.0, y * 3.5 - climb, z * 3.0, seed + 3) - 0.5);
				const double r = std::hypot(x + dx - 0.5, z + dz - 0.5);
				const double R = 0.42 * std::pow(std::max(0.0, 1.0 - y), 0.75) * (0.55 + 0.45 * std::min(1.0, y * 5.0));   // (widest low down, a point at the top)
				const double flicker = detail::fbm3(x * 5.0, y * 5.0 - climb * 1.7, z * 5.0, seed + 7);
				const double d = smoothstep(R, R * 0.25, r) * smoothstep(0.42, 0.62, flicker + 0.3 * (1.0 - y) - 0.15 * y);   // (tongues: the gas broken up, more so higher)
				const double hot = clamp01(d * (1.15 - 0.85 * y) * (0.7 + 0.5 * flicker));
				const Vec3 c = detail::heat(hot) * (hot * hot * 6.0);
				total += hot;
				detail::put(sa, 6.0 * d), detail::put(sa, 6.0 * d), detail::put(sa, 6.0 * d);
				detail::put(ss, 0.3 * d), detail::put(ss, 0.3 * d), detail::put(ss, 0.3 * d);
				detail::put(le, c.x), detail::put(le, c.y), detail::put(le, c.z);
			}
	char buf[400];
	std::string out = "# a flame (film_fire.h)\nAttributeBegin\n";
	std::snprintf(buf, sizeof buf, "  Translate %.4f %.4f %.4f\n  Scale %.4f %.4f %.4f\n", foot.x - 0.5 * w, foot.y, foot.z - 0.5 * w, w, size, w);
	out += buf;
	std::snprintf(buf, sizeof buf, "  MakeNamedMedium \"%s\" \"string type\" [ \"rgbgrid\" ] \"integer nx\" [ %d ] \"integer ny\" [ %d ] \"integer nz\" [ %d ] \"float g\" [ 0 ] \"float Lescale\" [ %.5f ]\n", name.c_str(),
				  nx, ny, nz, 0.02 / std::max(0.2, size));
	out += buf;
	out += "    \"rgb sigma_a\" [ " + sa + "]\n    \"rgb sigma_s\" [ " + ss + "]\n    \"rgb Le\" [ " + le + "]\nAttributeEnd\n";
	const double radius = 0.5 * std::sqrt(2.0 * w * w + size * size) * 1.02;
	std::snprintf(buf, sizeof buf, "AttributeBegin\n  Translate %.4f %.4f %.4f\n  Material \"interface\"\n  MediumInterface \"%s\" \"\"\n  Shape \"sphere\" \"float radius\" [ %.4f ]\nAttributeEnd\n", foot.x,
				  foot.y + 0.5 * size, foot.z, name.c_str(), radius);
	out += buf;
	// its light: among the flames, flickering with them (how much is burning this frame), warm
	const double glow = (0.6 + 0.4 * (total / (nx * ny * nz * 0.08))) * size * size * 2.5;
	std::snprintf(buf, sizeof buf, "LightSource \"point\" \"point3 from\" [ %.4f %.4f %.4f ] \"rgb I\" [ %.4f %.4f %.4f ]\n", foot.x, foot.y + 0.3 * size, foot.z, glow, 0.45 * glow, 0.12 * glow);
	return out + buf;
}

// Smoke at t, as scene text: a dark, billowing column `height` tall and `width` across rising from `base` (pbrt's), leaning with `drift` (metres across over its height).
inline std::string smokeText(const std::string &name, Vec3 base, double width, double height, double t, int seed, double darkness = 1.0, Vec3 drift = {0, 0, 0}) {
	width = std::max(width, 0.6 * height);   // (room for it to spread and lean)
	const int nx = 16, ny = 32, nz = 16;
	std::string dens;
	dens.reserve(static_cast<std::size_t>(nx) * ny * nz * 6);
	for (int k = 0; k < nz; ++k)
		for (int j = 0; j < ny; ++j)
			for (int i = 0; i < nx; ++i) {
				const double x = (i + 0.5) / nx, y = (j + 0.5) / ny, z = (k + 0.5) / nz;
				const double rise = t * 0.25;
				const double lean = y * y;   // (it bends with the wind as it climbs)
				const double r = std::hypot(x - 0.5 - drift.x / width * lean * 0.6, z - 0.5 - drift.z / width * lean * 0.6);
				const double R = 0.06 + 0.3 * std::sqrt(y);   // (it spreads as it rises)
				const double billow = detail::fbm3(x * 4.0, y * 5.0 - rise, z * 4.0, seed + 11);
				const double d = smoothstep(R, R * 0.2, r * (0.55 + 0.9 * billow)) * smoothstep(0.0, 0.06, y) * (1.0 - smoothstep(0.55, 1.0, y)) * smoothstep(0.25, 0.6, billow) * 1.4;
				detail::put(dens, std::max(0.0, d));
			}
	char buf[480];
	std::string out = "# smoke (film_fire.h)\nAttributeBegin\n";
	std::snprintf(buf, sizeof buf, "  Translate %.4f %.4f %.4f\n  Scale %.4f %.4f %.4f\n", base.x - 0.5 * width, base.y, base.z - 0.5 * width, width, height, width);
	out += buf;
	std::snprintf(buf, sizeof buf,
				  "  MakeNamedMedium \"%s\" \"string type\" [ \"uniformgrid\" ] \"integer nx\" [ %d ] \"integer ny\" [ %d ] \"integer nz\" [ %d ] \"rgb sigma_a\" [ %.4f %.4f %.4f ] \"rgb sigma_s\" [ %.4f %.4f %.4f ] \"float g\" [ 0.3 ]\n",
				  name.c_str(), nx, ny, nz, 0.5 * darkness / std::max(1.0, width * 0.05), 0.5 * darkness / std::max(1.0, width * 0.05), 0.5 * darkness / std::max(1.0, width * 0.05),
				  0.25 / std::max(1.0, width * 0.05), 0.25 / std::max(1.0, width * 0.05), 0.26 / std::max(1.0, width * 0.05));
	out += buf;
	out += "    \"float density\" [ " + dens + "]\nAttributeEnd\n";
	const double radius = 0.5 * std::sqrt(2.0 * width * width + height * height) * 1.02;
	std::snprintf(buf, sizeof buf, "AttributeBegin\n  Translate %.4f %.4f %.4f\n  Material \"interface\"\n  MediumInterface \"%s\" \"\"\n  Shape \"sphere\" \"float radius\" [ %.4f ]\nAttributeEnd\n", base.x,
				  base.y + 0.5 * height, base.z, name.c_str(), radius);
	return out + buf;
}

// ---- an eruption's ash ----
// How long an ash column takes to climb to its height (seconds): a hundred metres a second or more, slowing as it goes.
inline double columnRise(double column) { return std::clamp(column / 120.0, 6.0, 60.0); }
// An ash column `since` seconds into its eruption: its top (above the vent) and the radius of its umbrella, the cloud it spreads into where it stops rising.
inline void columnShape(double column, double since, double &top, double &umbrella) {
	const double rise = columnRise(column), u = clamp01(since / rise);
	top = column * std::max(0.05, 1.0 - std::pow(1.0 - u, 1.7));
	umbrella = 0.6 * column * (1.0 - std::exp(-std::max(0.0, since - 0.7 * rise) / (0.6 * rise)));
}
// An ash column at `since` seconds into its eruption, as scene text: a column `ventR` across at the vent (pbrt's: the crater's rim height), widening as it climbs, leaning
// downwind (`wind`: a unit direction across) and billowing (`nt`: the noise's time), its head spreading into an umbrella that drifts downwind; one grid, in a sphere round it.
// The lava in the crater lights its foot from inside.
inline std::string ashColumnText(const std::string &name, Vec3 vent, double ventR, double column, double since, double nt, Vec3 wind, int seed, double glow) {
	double top = 0.0, umbrella = 0.0;
	columnShape(column, since, top, umbrella);
	const double lean = 0.15 * top, rcTop = ventR + 0.18 * top;
	const double hu = 0.86 * top, ht = 0.1 * top + 0.02 * umbrella;   // (the umbrella: a lens round this height, this thick each way)
	const double half = (std::max(rcTop, 1.3 * umbrella) + lean + 0.3 * umbrella) * 1.1, height = 1.02 * top + 0.02 * umbrella;
	const int nx = 56, ny = 56, nz = 56;
	const double L = 0.06 * column;   // (the billows' size)
	std::string dens;
	dens.reserve(static_cast<std::size_t>(nx) * ny * nz * 6);
	for (int k = 0; k < nz; ++k)
		for (int j = 0; j < ny; ++j)
			for (int i = 0; i < nx; ++i) {
				const double dx = ((i + 0.5) / nx - 0.5) * 2.0 * half, dy = (j + 0.5) / ny * height, dz = ((k + 0.5) / nz - 0.5) * 2.0 * half;
				const double billow = detail::fbm3(dx / L, dy / L - 0.15 * nt, dz / L, seed);
				const double bend = lean * (dy / top) * (dy / top);   // (it bends with the wind as it climbs)
				const double r = std::hypot(dx - wind.x * bend, dz - wind.z * bend), rc = ventR + 0.18 * dy;
				const double head = std::hypot(r, std::max(0.0, dy - (top - rcTop)));   // (a rounded head)
				double d = smoothstep(rc, 0.35 * rc, head * (0.65 + 0.7 * billow));
				if (umbrella > 1.0 && std::abs(dy - hu) < ht) {
					const double off = lean * (hu / top) * (hu / top) + 0.3 * umbrella;
					const double ru = std::hypot(dx - wind.x * off, dz - wind.z * off), Ru = umbrella * std::sqrt(1.0 - (dy - hu) * (dy - hu) / (ht * ht));
					d = std::max(d, smoothstep(Ru, 0.4 * Ru, ru * (0.65 + 0.7 * billow)));
				}
				detail::put(dens, d * (0.5 + billow) * smoothstep(0.0, 0.02 * top, dy));
			}
	char buf[480];
	std::string out = "# an ash column (film_fire.h)\nAttributeBegin\n";
	std::snprintf(buf, sizeof buf, "  Translate %.4f %.4f %.4f\n  Scale %.4f %.4f %.4f\n", vent.x - half, vent.y, vent.z - half, 2.0 * half, height, 2.0 * half);
	out += buf;
	std::snprintf(buf, sizeof buf,
				  "  MakeNamedMedium \"%s\" \"string type\" [ \"uniformgrid\" ] \"integer nx\" [ %d ] \"integer ny\" [ %d ] \"integer nz\" [ %d ] \"rgb sigma_a\" [ 0.012 0.0125 0.013 ] \"rgb sigma_s\" [ 0.008 0.0077 0.0073 ] \"float g\" [ 0.4 ]\n",
				  name.c_str(), nx, ny, nz);
	out += buf;
	out += "    \"float density\" [ " + dens + "]\nAttributeEnd\n";
	const double radius = std::sqrt(2.0 * half * half + 0.25 * height * height) * 1.01;
	std::snprintf(buf, sizeof buf, "AttributeBegin\n  Translate %.4f %.4f %.4f\n  Material \"interface\"\n  MediumInterface \"%s\" \"\"\n  Shape \"sphere\" \"float radius\" [ %.4f ]\nAttributeEnd\n", vent.x,
				  vent.y + 0.5 * height, vent.z, name.c_str(), radius);
	out += buf;
	// the column's foot, lit by the fountains of lava in it
	std::snprintf(buf, sizeof buf, "LightSource \"point\" \"point3 from\" [ %.3f %.3f %.3f ] \"rgb I\" [ %.1f %.1f %.1f ]\n", vent.x, vent.y + 0.04 * top, vent.z, glow, 0.38 * glow, 0.07 * glow);
	return out + buf;
}

// A pyroclastic flow `since` seconds after it set off, as scene text: an avalanche of hot ash racing down its path from the rim, fast at first and slowing as it spreads, a
// ground-hugging cloud thicker and wider the further it has gone, its head billowing highest; behind the head the ash lofts and thins away. One grid round the stretch it
// has covered, kept below `topLimit` (where the column's own grid starts). `radius`, `height`: the volcano's.
inline std::string flowText(const std::string &name, const PyroclasticFlow &f, double since, double nt, const std::function<double(double, double)> &groundAt, double topLimit,
							double radius, double height, int seed) {
	if (since < 0.0 || since > 120.0 || f.path.size() < 2) return std::string();
	std::vector<double> along(f.path.size(), 0.0);
	for (std::size_t i = 1; i < f.path.size(); ++i) along[i] = along[i - 1] + std::hypot(f.path[i].x - f.path[i - 1].x, f.path[i].z - f.path[i - 1].z);
	const double maxReach = std::max(1.0, std::min(along.back(), 2.4 * radius)), v0 = 0.1 * radius;
	const double reach = std::max(0.02 * maxReach, maxReach * (1.0 - std::exp(-since * v0 / maxReach)));
	const auto passed = [&](double s) { return -maxReach / v0 * std::log(std::max(1e-6, 1.0 - std::min(s, reach) / maxReach)); };   // (when the front passed s)
	// its path to the front, every so far, and how far along each point is
	std::vector<Vec3> p;
	std::vector<double> ps;
	for (int k = 0; k <= 40; ++k) {
		const double s = reach * k / 40.0;
		const std::size_t i = static_cast<std::size_t>(std::upper_bound(along.begin(), along.end(), s) - along.begin());
		const std::size_t b = std::min(i, f.path.size() - 1), a = b > 0 ? b - 1 : 0;
		const double u = along[b] > along[a] ? clamp01((s - along[a]) / (along[b] - along[a])) : 0.0;
		const Vec3 q = lerp(f.path[a], f.path[b], u);
		p.push_back({q.x, groundAt(q.x, q.z), q.z}), ps.push_back(s);
	}
	const auto thick = [&](double s, double age) {   // (how high the cloud stands and how wide it is at s)
		const double headBulge = 1.0 + 0.6 * std::exp(-std::pow((reach - s) / (0.06 * maxReach), 2.0));
		return std::pair<double, double>{height * (0.15 + 0.25 * s / maxReach) * (1.0 + 0.04 * age) * headBulge, radius * (0.06 + 0.12 * s / maxReach) * (1.0 + 0.03 * age) * (0.7 + 0.3 * headBulge)};
	};
	double lo[3] = {1e30, 1e30, 1e30}, hi[3] = {-1e30, -1e30, -1e30};
	for (std::size_t k = 0; k < p.size(); ++k) {
		const auto [H, R] = thick(ps[k], since - passed(ps[k]));
		lo[0] = std::min(lo[0], p[k].x - 1.3 * R), hi[0] = std::max(hi[0], p[k].x + 1.3 * R);
		lo[2] = std::min(lo[2], p[k].z - 1.3 * R), hi[2] = std::max(hi[2], p[k].z + 1.3 * R);
		lo[1] = std::min(lo[1], p[k].y - 5.0), hi[1] = std::max(hi[1], p[k].y + 1.3 * H);
	}
	hi[1] = std::min(hi[1], topLimit);
	if (hi[1] <= lo[1] + 1.0) return std::string();
	const int nx = 56, ny = 40, nz = 56;
	std::vector<double> nearS(static_cast<std::size_t>(nx) * nz), nearD(nearS.size()), ground(nearS.size());
	for (int k = 0; k < nz; ++k)
		for (int i = 0; i < nx; ++i) {   // each column of the grid: the nearest point of the path, how far along it is, the ground under it
			const double x = lo[0] + (i + 0.5) / nx * (hi[0] - lo[0]), z = lo[2] + (k + 0.5) / nz * (hi[2] - lo[2]);
			double best = 1e30, bs = 0.0;
			for (std::size_t q = 0; q + 1 < p.size(); ++q) {
				const double ex = p[q + 1].x - p[q].x, ez = p[q + 1].z - p[q].z, l2 = ex * ex + ez * ez;
				const double u = l2 > 0.0 ? clamp01(((x - p[q].x) * ex + (z - p[q].z) * ez) / l2) : 0.0;
				const double d = std::hypot(x - p[q].x - u * ex, z - p[q].z - u * ez);
				if (d < best) best = d, bs = ps[q] + u * (ps[q + 1] - ps[q]);
			}
			const std::size_t c = static_cast<std::size_t>(k) * nx + i;
			nearS[c] = bs, nearD[c] = best, ground[c] = groundAt(x, z);
		}
	std::string dens;
	dens.reserve(static_cast<std::size_t>(nx) * ny * nz * 6);
	for (int k = 0; k < nz; ++k)
		for (int j = 0; j < ny; ++j)
			for (int i = 0; i < nx; ++i) {
				const std::size_t c = static_cast<std::size_t>(k) * nx + i;
				const double x = lo[0] + (i + 0.5) / nx * (hi[0] - lo[0]), y = lo[1] + (j + 0.5) / ny * (hi[1] - lo[1]), z = lo[2] + (k + 0.5) / nz * (hi[2] - lo[2]);
				const double age = since - passed(nearS[c]);
				const auto [H, R] = thick(nearS[c], age);
				const double above = y - ground[c];
				const double billow = detail::fbm3(x / (0.04 * radius), (y - 2.0 * nt) / (0.03 * radius), z / (0.04 * radius), seed);
				const double d = above < -1.0 ? 0.0
											  : smoothstep(R, 0.3 * R, nearD[c] * (0.6 + 0.8 * billow)) * smoothstep(H, 0.25 * H, std::max(0.0, above) * (0.6 + 0.8 * billow)) *
													std::exp(-age / 30.0) * (0.6 + 0.8 * billow);
				detail::put(dens, d);
			}
	char buf[480];
	std::string out = "# a pyroclastic flow (film_fire.h)\nAttributeBegin\n";
	std::snprintf(buf, sizeof buf, "  Translate %.4f %.4f %.4f\n  Scale %.4f %.4f %.4f\n", lo[0], lo[1], lo[2], hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]);
	out += buf;
	std::snprintf(buf, sizeof buf,
				  "  MakeNamedMedium \"%s\" \"string type\" [ \"uniformgrid\" ] \"integer nx\" [ %d ] \"integer ny\" [ %d ] \"integer nz\" [ %d ] \"rgb sigma_a\" [ 0.03 0.031 0.033 ] \"rgb sigma_s\" [ 0.03 0.0285 0.027 ] \"float g\" [ 0.3 ]\n",
				  name.c_str(), nx, ny, nz);
	out += buf;
	out += "    \"float density\" [ " + dens + "]\nAttributeEnd\n";
	const double cx = 0.5 * (lo[0] + hi[0]), cy = 0.5 * (lo[1] + hi[1]), cz = 0.5 * (lo[2] + hi[2]);
	const double rad = 0.5 * std::sqrt((hi[0] - lo[0]) * (hi[0] - lo[0]) + (hi[1] - lo[1]) * (hi[1] - lo[1]) + (hi[2] - lo[2]) * (hi[2] - lo[2])) * 1.01;
	std::snprintf(buf, sizeof buf, "AttributeBegin\n  Translate %.4f %.4f %.4f\n  Material \"interface\"\n  MediumInterface \"%s\" \"\"\n  Shape \"sphere\" \"float radius\" [ %.4f ]\nAttributeEnd\n", cx, cy, cz,
				  name.c_str(), rad);
	return out + buf;
}
// Where a pyroclastic flow's front is `since` seconds after it set off (pbrt's), for its roar.
inline Vec3 flowFront(const PyroclasticFlow &f, double since, double radius) {
	if (f.path.empty()) return {};
	double s = 0.0, total = 0.0;
	for (std::size_t i = 1; i < f.path.size(); ++i) total += std::hypot(f.path[i].x - f.path[i - 1].x, f.path[i].z - f.path[i - 1].z);
	const double maxReach = std::max(1.0, std::min(total, 2.4 * radius)), reach = maxReach * (1.0 - std::exp(-std::max(0.0, since) * 0.1 * radius / maxReach));
	for (std::size_t i = 1; i < f.path.size(); ++i) {
		s += std::hypot(f.path[i].x - f.path[i - 1].x, f.path[i].z - f.path[i - 1].z);
		if (s >= reach) return f.path[i];
	}
	return f.path.back();
}

// ---- lava ----
// The crust of a lava flow: dark rock broken into plates by glowing cracks, painted once (tileable).
inline std::string lavaPicture(const std::string &film) {
	return film_sky::picture(film, "lava_crust", 512, 512, [&](std::vector<float> &rgb) {
		const int cells = 7;
		for (int j = 0; j < 512; ++j)
			for (int i = 0; i < 512; ++i) {
				const double x = (i + 0.5) / 512.0 * cells, y = (j + 0.5) / 512.0 * cells;
				const int cx = static_cast<int>(std::floor(x)), cy = static_cast<int>(std::floor(y));
				double f1 = 1e9, f2 = 1e9;
				for (int dy = -1; dy <= 1; ++dy)
					for (int dx = -1; dx <= 1; ++dx) {
						const int gx = ((cx + dx) % cells + cells) % cells, gy = ((cy + dy) % cells + cells) % cells;
						const double px = cx + dx + hash01(gx * 31 + gy, 301), py = cy + dy + hash01(gx * 17 + gy, 303);
						const double d = std::hypot(x - px, y - py);
						if (d < f1) f2 = f1, f1 = d;
						else if (d < f2) f2 = d;
					}
				const double crack = 1.0 - smoothstep(0.0, 0.12, f2 - f1), glowInside = 0.25 * std::pow(nature::noise2(x * 3.0, y * 3.0, 307), 3.0);
				const Vec3 c = detail::heat(0.45 + 0.5 * crack) * (crack * 2.5 + glowInside) + Vec3{0.02, 0.01, 0.005};
				const std::size_t q = (static_cast<std::size_t>(j) * 512 + i) * 3;
				rgb[q] = static_cast<float>(c.x), rgb[q + 1] = static_cast<float>(c.y), rgb[q + 2] = static_cast<float>(c.z);
			}
	});
}
// A lava flow at t, as scene text: a ribbon down its path (its surface a little above it), its crust creeping downhill.
// `glow`: how bright its cracks shine (3 by day; less when the film's exposure has opened up for the night, so they stay orange instead of white).
inline std::string lavaText(const std::string &film, const Lava &l, double t, double glow = 3.0) {
	if (l.path.size() < 2 || t < l.t0) return std::string();
	const int across = 6;
	double s = 0.0;
	const double reach = std::max(4.0, (t - l.t0) * l.speed * 20.0);   // (the front advances)
	// its cross-sections down the path: points (pbrt's), and how far down it each is
	std::vector<std::vector<Vec3>> rows;
	std::vector<double> at;
	std::vector<double> widths;
	for (std::size_t i = 0; i < l.path.size(); ++i) {
		if (i > 0) s += length(Vec3{l.path[i].x - l.path[i - 1].x, 0.0, l.path[i].z - l.path[i - 1].z});
		if (s > reach) break;
		const Vec3 a = l.path[i > 0 ? i - 1 : i], b = l.path[i + 1 < l.path.size() ? i + 1 : i];
		const Vec3 along = normalize(Vec3{b.x - a.x, 0.0, b.z - a.z} + Vec3{1e-9, 0, 0}), side{-along.z, 0.0, along.x};
		const double w = l.width * (1.0 - 0.6 * smoothstep(reach - 15.0, reach, s));   // (narrowing to its front)
		std::vector<Vec3> row;
		for (int k = 0; k <= across; ++k) {
			const double c = k / double(across) - 0.5;
			row.push_back(l.path[i] + side * (c * w) + Vec3{0.0, 0.25 - 0.4 * c * c, 0.0});
		}
		rows.push_back(std::move(row)), at.push_back(s), widths.push_back(w);
	}
	// The crust picture tiles every 12 m down the flow and creeps with it. An area light's picture is read clamped at its edges (pbrt's, and so the renderers'), so a
	// stretch whose v ran past 0..1 was its edge smeared into stripes: the ribbon is cut where v passes a whole number, each piece reading the picture from 0 to 1.
	// Across, the picture spans the flow (at most 12 m of it).
	Mesh m;
	const auto put = [&](const std::vector<Vec3> &a, const std::vector<Vec3> &b, double va, double vb, double ua, double ub) {   // (the strip between cross-sections a and b,
		                                                                                                                             // its picture va to vb down, ua to ub across)
		const int base = static_cast<int>(m.verts.size());
		for (int k = 0; k <= across; ++k) {
			const double across01 = k / double(across);
			for (const auto &[row, v] : {std::pair<const std::vector<Vec3> *, double>{&a, va}, {&b, vb}}) {
				const Vec3 p = (*row)[static_cast<std::size_t>(k)];
				m.verts.push_back({-p.x, p.y, p.z});
				m.normals.push_back({0.0, 1.0, 0.0});
				m.uvs.push_back({ua + (ub - ua) * across01, v});
			}
		}
		for (int k = 0; k < across; ++k) {
			const int q = base + 2 * k;
			m.tris.push_back({q, q + 2, q + 1});
			m.tris.push_back({q + 1, q + 2, q + 3});
		}
	};
	for (std::size_t i = 0; i + 1 < rows.size(); ++i) {
		const double v0 = (at[i] - l.speed * t) / 12.0, v1 = (at[i + 1] - l.speed * t) / 12.0;
		const double u1 = std::min(1.0, 0.5 * (widths[i] + widths[i + 1]) / 12.0);
		double va = v0;
		while (va < v1 - 1e-9) {
			const double tile = std::floor(va + 1e-9), vb = std::min(v1, tile + 1.0);
			const auto cut = [&](double v) {   // (the cross-section where the picture's v is v)
				const double f = (v - v0) / std::max(1e-12, v1 - v0);
				std::vector<Vec3> row;
				for (int k = 0; k <= across; ++k) row.push_back(lerp(rows[i][static_cast<std::size_t>(k)], rows[i + 1][static_cast<std::size_t>(k)], f));
				return row;
			};
			put(cut(va), cut(vb), va - tile, vb - tile, 0.0, u1);
			va = vb;
		}
	}
	if (m.tris.empty()) return std::string();
	char scale[32];
	std::snprintf(scale, sizeof scale, "%.4f", glow);
	return "# a lava flow (film_fire.h)\nAttributeBegin\n  AreaLightSource \"diffuse\" \"string filename\" [ \"" + lavaPicture(film) +
		   "\" ] \"float scale\" [ " + scale + " ] \"bool twosided\" [ true ]\n  Material \"diffuse\" \"rgb reflectance\" [ 0.05 0.04 0.035 ]\n" + emitMesh(m, identity()) + "AttributeEnd\n";
}

}  // namespace fire
}  // namespace cartoon
