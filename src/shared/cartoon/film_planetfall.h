#pragma once
// film_planetfall.h -- from orbit to the ground in one shot (film_file.h `planetfall`, and a shot's `descend`): a film on land whose camera starts high above it. Or the other way: with `from`
// below `to` it rises off the ground, and with `straight` it looks straight down all the way (a powers-of-ten shot).
//
//   the land   the film's own terrain is the place the camera comes down to, and round it the planet goes on: a cap, a ring of mesh from the terrain's edge out past the horizon of
//              the highest camera, curving away over a planet of real size. Its rings are spaced by the logarithm of their distance, so it is as fine seen from anywhere over the
//              landing place, and its picture (a planet's surface, film_planets.h) is laid out the same way. Near the terrain its colours and heights turn into the terrain's own.
//   clouds     two thin sheets, cut to clouds by a picture (stochastic alpha), that the camera falls through; a veil of blue over everything from above, for the air.
//   the sky    the film's physical sky (atmosphere.h) seen from the camera's height: blue on the ground, deep and then black overhead, the thin bright line of the air on the horizon;
//              the stars come out in it as it darkens.
//
// All of it is made once and kept: a frame is a few placements and a sky.
//
// Coordinates: pbrt's for the film file, story space (x mirrored) for the meshes.

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "cartoon_lib.h"
#include "film_cache.h"
#include "film_planets.h"
#include "film_sky.h"
#include "film_terrain.h"

namespace cartoon {
namespace planetfall {

struct Fall {
	bool on = false;
	std::string kind = "ocean";         // (the planet: film_planets.h kinds)
	double from = 300000.0, to = 6.0;   // (metres above the landing place)
	double t0 = 0.0, t1 = 12.0;         // (film seconds)
	double radius = 6.37e6;             // (the planet's)
	double x = 0.0, z = 0.0, heading = 0.0;   // (where it comes down, pbrt's, and the way it is going, a compass bearing)
	double clouds = 0.5;
	int seed = 3;
	bool straight = false;              // (`straight`: looking straight down over the landing place, all the way)
	double highest() const { return std::max(from, to); }
	double lowest() const { return std::min(from, to); }
};

// How far along the fall it is: slow to start, fast through the middle, easing to the ground.
inline double progressAt(const Fall &f, double t) {
	const double u = std::clamp((t - f.t0) / std::max(1e-6, f.t1 - f.t0), 0.0, 1.0);
	return f.straight ? u : u * u * (3.0 - 2.0 * u);   // (straight up or down: at one pace, to join the shots either side of it)
}
// Its height at t, falling by the same share of itself each moment, as a fall from orbit looks.
inline double heightAt(const Fall &f, double t) { return f.from * std::pow(f.to / f.from, progressAt(f, t)); }
// Where the camera is and what it looks at, at t, over a landing place at height `ground`: looking well down from orbit, coming level as it nears the land, and sliding in along its
// heading from far out, so the land comes up under it at a slant.
inline void cameraAt(const Fall &f, double t, double ground, Vec3 &eye, Vec3 &target) {
	const double h = heightAt(f, t), level = std::log(h / f.lowest()) / std::max(1e-6, std::log(f.highest() / f.lowest()));
	const double b = f.heading * kPi / 180.0, down = (7.0 + 30.0 * smoothstep(0.08, 0.55, level) - 10.0 * smoothstep(0.75, 1.0, level)) * kPi / 180.0;
	const Vec3 ahead{std::sin(b), 0.0, std::cos(b)};
	if (f.straight) {
		eye = Vec3{f.x, ground + h, f.z};
		target = Vec3{f.x, ground, f.z} + ahead * (0.002 * h);   // (a hair off plumb: a camera looking exactly along its up has no way up)
		return;
	}
	eye = Vec3{f.x, ground + h, f.z} - ahead * (0.55 * (h - f.lowest()));
	target = eye + ahead * (1000.0 * std::cos(down)) + Vec3{0.0, -1000.0 * std::sin(down), 0.0};
}

namespace detail {
constexpr int kRings = 80, kSpokes = 240;
inline double reachOf(const Fall &f) { return std::min(0.8 * f.radius, 1.3 * std::sqrt(2.0 * f.radius * f.highest())); }
// A ring's distance from the landing place: a share of the way out, by the logarithm.
inline double ringAt(double from, double to, double u) { return from * std::pow(to / from, u); }
// How far a place `r` out has dropped below the landing place's level, on a planet of radius R.
inline double dropAt(double R, double r) { return R - std::sqrt(std::max(0.0, R * R - r * r)); }
inline Vec3 upAt(double R, double dx, double dz) { return normalize(Vec3{dx, R, dz}); }

// The land past the terrain's edge: from `r0` out, its first ring on the terrain's own heights.
inline Mesh capMesh(const terrain::Field &field, const Fall &f) {
	Mesh m;
	const double r0 = 0.47 * field.spec.size, r1 = reachOf(f), cx = field.sx0 + 0.5 * field.spec.size, cz = field.sz0 + 0.5 * field.spec.size;
	for (int j = 0; j <= kRings; ++j) {
		const double u = static_cast<double>(j) / kRings, r = ringAt(r0, r1, u);
		for (int i = 0; i <= kSpokes; ++i) {
			const double a = 2.0 * kPi * i / kSpokes, dx = r * std::cos(a), dz = r * std::sin(a);
			const double rim = field.heightAt(cx + r0 * std::cos(a), cz + r0 * std::sin(a));
			const double y = lerp(rim, field.spec.y, smoothstep(r0, 2.4 * r0, r)) - dropAt(f.radius, r);
			m.verts.push_back({-(cx + dx), y, cz + dz});
			const Vec3 n = upAt(f.radius, dx, dz);
			m.normals.push_back({-n.x, n.y, n.z});
			m.uvs.push_back({static_cast<double>(i) / kSpokes, u});
		}
	}
	for (int j = 0; j < kRings; ++j)
		for (int i = 0; i < kSpokes; ++i) {
			const int a = j * (kSpokes + 1) + i, b = a + kSpokes + 1;
			m.tris.push_back({a, b, a + 1}), m.tris.push_back({a + 1, b, b + 1});
		}
	return m;
}
// A sheet of cloud over all of it, whole, its rings from a few metres out as the cap's are from the terrain's edge.
constexpr double kSheetFrom = 30.0;
inline Mesh sheetMesh(const terrain::Field &field, const Fall &f) {
	Mesh m;
	const double r0 = kSheetFrom, r1 = reachOf(f), cx = field.sx0 + 0.5 * field.spec.size, cz = field.sz0 + 0.5 * field.spec.size;
	constexpr double kFirstRow = 0.002;   // (its picture repeats: from the very edge of it the middle of the sheet would read the far side of the planet's clouds)
	m.verts.push_back({-cx, 0.0, cz}), m.normals.push_back({0.0, 1.0, 0.0}), m.uvs.push_back({0.5, kFirstRow});
	for (int j = 0; j <= kRings; ++j) {
		const double u = static_cast<double>(j) / kRings, r = ringAt(r0, r1, u);
		for (int i = 0; i <= kSpokes; ++i) {
			const double a = 2.0 * kPi * i / kSpokes, dx = r * std::cos(a), dz = r * std::sin(a);
			m.verts.push_back({-(cx + dx), -dropAt(f.radius, r), cz + dz});
			const Vec3 n = upAt(f.radius, dx, dz);
			m.normals.push_back({-n.x, n.y, n.z});
			m.uvs.push_back({static_cast<double>(i) / kSpokes, kFirstRow + (1.0 - 2.0 * kFirstRow) * u});
		}
	}
	for (int i = 0; i < kSpokes; ++i) m.tris.push_back({0, 1 + i, 2 + i});
	for (int j = 0; j < kRings; ++j)
		for (int i = 0; i < kSpokes; ++i) {
			const int a = 1 + j * (kSpokes + 1) + i, b = a + kSpokes + 1;
			m.tris.push_back({a, b, a + 1}), m.tris.push_back({a + 1, b, b + 1});
		}
	return m;
}
// The planet's own frame's direction under a place on the cap: `a` the angle round the landing place, `r` metres out.
inline Vec3 planetDir(double a, double r, double R) {
	const Vec3 under = normalize(Vec3{0.45, 0.72, 0.53}), e1 = normalize(cross(under, Vec3{0.0, 0.0, 1.0})), e2 = cross(under, e1);
	const double arc = std::asin(std::min(1.0, r / R));
	return normalize(under * std::cos(arc) + (e1 * std::cos(a) + e2 * std::sin(a)) * std::sin(arc));
}
inline planets::Planet planetOf(const Fall &f) {
	planets::Planet p;
	p.kind = f.kind, p.seed = f.seed, p.radius = f.radius;
	return p;
}
// The cap's colours: the planet's surface at the largest scales, with the grain of real land laid over it at every smaller scale down to the terrain's, and a coast for a planet with
// sea; near the terrain's edge it comes to the terrain's own colour.
inline std::string groundPicture(const std::string &film, const terrain::Field &field, const Fall &f) {
	constexpr int W = 1536, H = 1024;
	const double r0 = 0.47 * field.spec.size, r1 = reachOf(f), cx = field.sx0 + 0.5 * field.spec.size, cz = field.sz0 + 0.5 * field.spec.size;
	const planets::Planet p = planetOf(f);
	Vec3 own{0.0, 0.0, 0.0};
	for (int k = 0; k < 32; ++k) own = own + terrain::colourAt(field, cx + 0.6 * r0 * std::cos(0.196 * k), cz + 0.6 * r0 * std::sin(0.196 * k)) * (1.0 / 32.0);
	// (the land just past the terrain is the terrain again, turned over at its edges, so no line shows where one stops)
	const auto mirrored = [&](double d) {
		const double v = std::abs(std::fmod(d / field.spec.size + 0.5, 2.0));
		return (v > 1.0 ? 2.0 - v : v) * (field.spec.size - 2.0) + 1.0;
	};
	return film_sky::picture(film, "fall4_ground_" + f.kind + "_" + std::to_string(f.seed) + "_" + std::to_string(static_cast<int>(f.highest())), W, H, [&](std::vector<float> &rgb) {
		for (int y = 0; y < H; ++y)
			for (int x = 0; x < W; ++x) {
				const double a = 2.0 * kPi * (x + 0.5) / W, u = 1.0 - (y + 0.5) / H, r = ringAt(r0, r1, u), dx = r * std::cos(a), dz = r * std::sin(a);
				double sea = 0.0, glow = 0.0;
				Vec3 c = planets::surfaceAt(p, planetDir(a, r, f.radius), sea, glow);
				const double big = nature::fbm2(dx / 260000.0 + 3.0, dz / 260000.0, f.seed + 11), mid = nature::fbm2(dx / 41000.0, dz / 41000.0, f.seed + 12), small = nature::fbm2(dx / 6000.0, dz / 6000.0, f.seed + 13), fine = nature::fbm2(dx / 800.0, dz / 800.0, f.seed + 14);
				const double coast = big + 0.28 * (mid - 0.5) + 0.07 * (small - 0.5) + 0.2 * (1.0 - smoothstep(0.0, 0.5, r / 200000.0));   // (the landing place is on land)
				const double green = smoothstep(0.42, 0.6, mid + 0.3 * (small - 0.5));
				if (f.kind == "ocean") {
					const Vec3 land = lerp(Vec3{0.3, 0.26, 0.15}, Vec3{0.08, 0.17, 0.06}, green) * (0.65 + 0.6 * small) * (0.8 + 0.4 * fine);
					const Vec3 water = lerp(Vec3{0.012, 0.05, 0.1}, Vec3{0.03, 0.14, 0.2}, smoothstep(0.4, 0.46, coast));
					c = lerp(water, land, smoothstep(0.455, 0.47, coast));
				} else {
					c = c * (0.55 + 0.9 * mid) * (0.7 + 0.6 * small) * (0.8 + 0.4 * fine);
				}
				if (const double far_out = smoothstep(0.06, 0.22, u); far_out < 1.0) c = lerp(terrain::colourAt(field, field.sx0 + mirrored(dx), field.sz0 + mirrored(dz)), lerp(own * (0.75 + 0.5 * fine), c, smoothstep(0.1, 0.3, u)), far_out);
				float *px = &rgb[(static_cast<std::size_t>(y) * W + x) * 3];
				px[0] = static_cast<float>(c.x), px[1] = static_cast<float>(c.y), px[2] = static_cast<float>(c.z);
			}
	});
}
// What of a sheet is there: a sheet of cloud cut by the noise of its own height, or the even veil of the air.
inline std::string sheetPicture(const std::string &film, const terrain::Field &field, const Fall &f, int which) {
	constexpr int W = 1536, H = 1024;
	const double r0 = kSheetFrom, r1 = reachOf(f);
	(void)field;
	return film_sky::picture(film, "fall4_sheet" + std::to_string(which) + "_" + std::to_string(f.seed) + "_" + std::to_string(static_cast<int>(f.highest())), W, H, [&](std::vector<float> &rgb) {
		for (int y = 0; y < H; ++y)
			for (int x = 0; x < W; ++x) {
				const double a = 2.0 * kPi * (x + 0.5) / W, u = 1.0 - (y + 0.5) / H, r = ringAt(r0, r1, u), dx = r * std::cos(a), dz = r * std::sin(a);
				const double scale = which == 0 ? 9000.0 : 30000.0;
				const double big = nature::fbm2(dx / (9.0 * scale) + 7.0, dz / (9.0 * scale), f.seed + 40 + which), body = nature::fbm2(dx / scale, dz / scale, f.seed + 42 + which), fine = nature::fbm2(dx / (0.12 * scale), dz / (0.12 * scale), f.seed + 44 + which);
				const double n = 0.45 * big + 0.55 * body + 0.1 * (fine - 0.5), line = lerp(0.6, 0.44, std::clamp(f.clouds, 0.0, 1.0));
				const float keep = static_cast<float>(0.85 * smoothstep(line, line + 0.06, n) * smoothstep(1500.0, 4500.0, r));   // (a clear gap over the landing place: the middle of a sheet shows the rings of its picture)
				float *px = &rgb[(static_cast<std::size_t>(y) * W + x) * 3];
				px[0] = px[1] = px[2] = keep;
			}
	});
}
}  // namespace detail

// The land past the terrain and what is over it, for a camera `h` above the land. `film`: the film's cache.
inline std::string text(const std::string &film, const terrain::Field &field, const Fall &f, double h) {
	const std::string cap = film_cache::ply(film, "fall1_cap", [&] { return detail::capMesh(field, f); });
	const std::string sheet = film_cache::ply(film, "fall2_sheet", [&] { return detail::sheetMesh(field, f); });
	std::string out = "# the planet round the land (film_planetfall.h)\nAttributeBegin\n  Texture \"fall_ground\" \"spectrum\" \"imagemap\" \"string filename\" [ \"" + detail::groundPicture(film, field, f) +
					  "\" ]\n  Material \"diffuse\" \"texture reflectance\" \"fall_ground\"\n" + film_cache::plyShape(cap) + "AttributeEnd\n";
	std::string line = film_cache::plyShape(sheet);
	line.pop_back();
	char at[80];
	for (int k = 0; k < 2; ++k) {
		const double high = field.spec.y + (k == 0 ? 2600.0 : 6400.0);
		if (h < high - field.spec.y) continue;   // (a thin sheet is no sky from below: past it, it is gone, in the moment the camera is inside it)
		std::snprintf(at, sizeof at, "  Translate 0 %.1f 0\n", high);
		const std::string tex = "fall_cloud" + std::to_string(k);
		out += "AttributeBegin\n" + std::string(at) + "  Texture \"" + tex + "\" \"float\" \"imagemap\" \"string filename\" [ \"" + detail::sheetPicture(film, field, f, k) + "\" ]\n  " + diffuseMaterial({0.88, 0.89, 0.91}) + "\n" +
			   line + " \"texture alpha\" \"" + tex + "\"\nAttributeEnd\n";
	}
	return out;
}
// The eye's height for a sky picture: the sky is worked out for each, so a fall takes some dozens of them and not one a frame.
inline double skyHeight(double h) { return std::exp(std::floor(std::log(std::max(1.0, h)) / 0.06) * 0.06); }
// How much of the stars show from height h, in steps (each a picture).
inline double starsAt(double h) { return std::floor(smoothstep(12000.0, 70000.0, h) * 4.0) / 4.0; }

}  // namespace planetfall
}  // namespace cartoon
