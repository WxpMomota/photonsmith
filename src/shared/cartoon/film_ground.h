#pragma once
// film_ground.h -- the ground close up for a film file (film_file.h `closeup`, `tracks`): what the land's mesh is too coarse to show a camera a metre or two off it.
//
//   relief     a patch of fine ground round what the camera sees nearest (16 m across by default, a vertex every 5 cm), laid over the land: wind ripples in sand a
//              hand apart, soft drifts and wind-carved ridges in snow, grit and pebbles on bare earth - painted with the land's own colours (film_terrain.h colourAt),
//              snow where it lies, darker and glossy when it is wet; its edge eases down into the land's surface;
//   footprints in snow, sand or mud: a herd's (film_wildlife.h) hoof and paw prints along the ways its animals have come over the last half minute, and a `tracks`
//              line's trail - boots, paws or hooves - made step by step from one point to another over its time; each a dent with a pushed-up rim;
//   wet sand   on a shore, the band the waves have just left: dark and smooth as a mirror, the sky in it.
//
// Coordinates are pbrt's, as a film file's; the patch is written in them.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "cartoon_lib.h"
#include "film_sky.h"
#include "film_terrain.h"
#include "nature_lib.h"

namespace cartoon {
namespace ground {

// The close-up patch: on, how big (metres across), how fine (metres between its points).
struct Closeup {
	bool on = false;
	double size = 16.0, cell = 0.05;
};
// A trail of footprints: from where to where (pbrt's x z), made over [t0, t1], what makes them, how long a step is.
struct Track {
	double x0 = 0.0, z0 = 0.0, x1 = 10.0, z1 = 0.0, t0 = 0.0, t1 = 10.0, stride = 0.75;
	std::string kind = "boots";
	int seed = 1;
};
inline const std::vector<std::string> &trackKinds() {
	static const std::vector<std::string> k = {"boots", "paws", "hooves"};
	return k;
}
// One footprint: where (pbrt's x z), which way it points (a unit direction across the ground), its kind, how deep.
struct Print {
	double x = 0.0, z = 0.0, dx = 0.0, dz = 1.0, depth = 0.03;
	std::string kind = "boots";
};

// The prints of a track made by t: one every stride along its way, left and right in turn, as far as the walker has come.
inline std::vector<Print> trackPrints(const Track &k, double t) {
	std::vector<Print> out;
	if (t < k.t0) return out;
	const double L = std::hypot(k.x1 - k.x0, k.z1 - k.z0);
	if (L < 1e-6) return out;
	const double ux = (k.x1 - k.x0) / L, uz = (k.z1 - k.z0) / L, reach = L * clamp01((t - k.t0) / std::max(1e-6, k.t1 - k.t0));
	const double half = k.kind == "boots" ? 0.12 : 0.1;
	for (int n = 0; n * 0.5 * k.stride <= reach; ++n) {
		const double s = n * 0.5 * k.stride, side = n % 2 ? 1.0 : -1.0, wob = 0.03 * (hash01(k.seed * 97 + n, 1) - 0.5);
		Print p;
		p.x = k.x0 + ux * s + uz * side * half + wob, p.z = k.z0 + uz * s - ux * side * half + wob;
		const double turn = 0.08 * side + 0.06 * (hash01(k.seed * 97 + n, 2) - 0.5);
		p.dx = ux * std::cos(turn) - uz * std::sin(turn), p.dz = uz * std::cos(turn) + ux * std::sin(turn);
		p.kind = k.kind, p.depth = k.kind == "boots" ? 0.05 : 0.035;
		out.push_back(p);
	}
	return out;
}

// How deep a print dents the ground at (lx along its way, ly to its left), in metres of its own depth (0 .. 1), and the rim pushed up round it (negative).
inline double printShape(const Print &p, double lx, double ly) {
	const auto blob = [](double x, double y, double rx, double ry) { return 1.0 - smoothstep(0.75, 1.0, std::hypot(x / rx, y / ry)); };
	const auto rim = [](double x, double y, double rx, double ry) {
		const double r = std::hypot(x / rx, y / ry);
		return std::exp(-std::pow((r - 1.15) / 0.18, 2.0));
	};
	if (p.kind == "boots") {   // a sole and a heel, ridged across by the tread
		const double sole = blob(lx - 0.05, ly, 0.11, 0.05), heel = blob(lx + 0.1, ly, 0.05, 0.04);
		const double tread = 0.85 + 0.15 * std::sin(lx * 160.0);
		return std::max(sole, heel) * tread - 0.25 * std::max(rim(lx - 0.05, ly, 0.11, 0.05), rim(lx + 0.1, ly, 0.05, 0.04));
	}
	if (p.kind == "paws") {   // a pad and four toes
		double d = blob(lx, ly, 0.035, 0.04);
		for (int k = 0; k < 4; ++k) d = std::max(d, blob(lx - 0.05 - 0.012 * (k == 1 || k == 2), ly - 0.03 * (k - 1.5), 0.014, 0.012));
		return d - 0.2 * rim(lx, ly, 0.05, 0.05);
	}
	// hooves: two halves of a cloven print
	return std::max(blob(lx, ly - 0.022, 0.045, 0.018), blob(lx, ly + 0.022, 0.045, 0.018)) - 0.2 * rim(lx, ly, 0.06, 0.05);
}

// The ground's fine relief at (x, z) (metres up from the land): `kind` "sand" (ripples a hand apart across the wind), "snow" (soft drifts, wind-carved ridges) or
// "earth" (grit and pebbles).
inline double relief(const std::string &kind, double x, double z, int seed) {
	if (kind == "sand") {
		const double along = 0.8 * x + 0.6 * z + 0.04 * nature::noise2(x * 0.7, z * 0.7, seed + 401);
		const double ph = std::fmod(along / 0.11 + 10000.0, 1.0);
		const double ripple = ph < 0.7 ? smoothstep(0.0, 0.7, ph) : 1.0 - smoothstep(0.7, 1.0, ph);   // (a gentle windward side, a steep lee)
		return 0.008 * ripple * (0.6 + 0.4 * nature::noise2(x * 1.3, z * 1.3, seed + 403)) + 0.02 * nature::fbm2(x * 0.3, z * 0.3, seed + 405);
	}
	if (kind == "snow") {
		const double drift = 0.05 * nature::fbm2(x * 0.25, z * 0.25, seed + 411);
		const double ridge = 0.015 * std::pow(std::abs(2.0 * nature::noise2(x * 1.1 + 0.4 * z, z * 0.35, seed + 413) - 1.0), 3.0);
		return drift + ridge + 0.003 * nature::noise2(x * 9.0, z * 9.0, seed + 415);
	}
	// earth: grit everywhere, a pebble here and there
	double pebble = 0.0;
	const int cx = static_cast<int>(std::floor(x / 0.25)), cz = static_cast<int>(std::floor(z / 0.25));
	for (int b = -1; b <= 1; ++b)
		for (int a = -1; a <= 1; ++a) {
			const int h = (cx + a) * 7919 + (cz + b) * 104729 + seed;
			if (hash01(h, 421) > 0.35) continue;
			const double px = (cx + a + hash01(h, 423)) * 0.25, pz = (cz + b + hash01(h, 425)) * 0.25, r = 0.015 + 0.04 * hash01(h, 427);
			const double d = std::hypot(x - px, z - pz);
			if (d < r) pebble = std::max(pebble, 0.6 * std::sqrt(r * r - d * d));
		}
	return pebble + 0.004 * nature::noise2(x * 12.0, z * 12.0, seed + 429) + 0.015 * nature::fbm2(x * 0.4, z * 0.4, seed + 431);
}

// The close-up patch as scene text: centred on (cx, cz) (pbrt's; snapped to a grid so it does not swim as the camera moves), its relief of `kind` with `prints` dented
// in, laid over `groundAt` and painted with `colourAt` (snow over it by `snow`, glossy by `wet`); below `wetLine` (a shore's water level plus the wash's reach, or
// -1e30) the sand is wet and smooth as a mirror.
inline std::string closeupText(const std::string &film, const Closeup &c, double cx, double cz, const std::string &kind, const std::vector<Print> &prints,
							   const std::function<double(double, double)> &groundAt, const std::function<Vec3(double, double)> &colourAt, double snow, double wet,
							   double waterLevel, const std::string &landKey, int seed) {
	const double S = c.size, snap = 1.0;
	const double x0 = std::floor((cx - 0.5 * S) / snap) * snap, z0 = std::floor((cz - 0.5 * S) / snap) * snap;
	const int n = std::clamp(static_cast<int>(S / c.cell), 16, 400);
	// the prints near the patch, bucketed by the patch's half-metre cells
	std::map<std::pair<int, int>, std::vector<const Print *>> nearby;
	for (const Print &p : prints) {
		if (p.x < x0 - 0.5 || p.x > x0 + S + 0.5 || p.z < z0 - 0.5 || p.z > z0 + S + 0.5) continue;
		nearby[std::make_pair(static_cast<int>(std::floor(p.x / 0.5)), static_cast<int>(std::floor(p.z / 0.5)))].push_back(&p);
	}
	Mesh dry, mirror;
	std::vector<int> index(static_cast<std::size_t>(n + 1) * (n + 1));
	std::vector<Vec3> pts(index.size());
	std::vector<bool> isWet(index.size(), false);
	for (int j = 0; j <= n; ++j)
		for (int i = 0; i <= n; ++i) {
			const double x = x0 + S * i / n, z = z0 + S * j / n;
			const double edge = smoothstep(0.0, 0.08, std::min({i / double(n), j / double(n), 1.0 - i / double(n), 1.0 - j / double(n)}));
			const double g = groundAt(x, z);
			const bool wetHere = waterLevel > -1e29 && g > waterLevel - 0.3 && g < waterLevel + 0.5;
			double h = (wetHere ? 0.25 : 1.0) * relief(kind, x, z, seed);   // (wet sand: the ripples washed smooth)
			for (int b = -1; b <= 1; ++b)
				for (int a = -1; a <= 1; ++a) {
					const auto it = nearby.find(std::make_pair(static_cast<int>(std::floor(x / 0.5)) + a, static_cast<int>(std::floor(z / 0.5)) + b));
					if (it == nearby.end()) continue;
					for (const Print *p : it->second) {
						const double rx = x - p->x, rz = z - p->z, lx = rx * p->dx + rz * p->dz, ly = -rx * p->dz + rz * p->dx;
						if (std::abs(lx) > 0.25 || std::abs(ly) > 0.15) continue;
						h -= p->depth * printShape(*p, lx, ly);
					}
				}
			pts[static_cast<std::size_t>(j) * (n + 1) + i] = {x, g + 0.03 + h * edge - 0.03 * (1.0 - edge), z};
			isWet[static_cast<std::size_t>(j) * (n + 1) + i] = wetHere;
		}
	const auto normalAt = [&](int i, int j) {
		const auto P = [&](int a, int b) { return pts[static_cast<std::size_t>(std::clamp(b, 0, n)) * (n + 1) + std::clamp(a, 0, n)]; };
		const Vec3 du = P(i + 1, j) - P(i - 1, j), dv = P(i, j + 1) - P(i, j - 1);
		return normalize(cross(dv, du));
	};
	for (Mesh *m : {&dry, &mirror}) {
		m->verts.reserve(pts.size()), m->normals.reserve(pts.size());
		for (int j = 0; j <= n; ++j)
			for (int i = 0; i <= n; ++i) {
				const Vec3 p = pts[static_cast<std::size_t>(j) * (n + 1) + i];
				m->verts.push_back({-p.x, p.y, p.z});   // (story space: emitMesh mirrors it back)
				const Vec3 nn = normalAt(i, j);
				m->normals.push_back({-nn.x, nn.y, nn.z});
				m->uvs.push_back({i / double(n), 1.0 - j / double(n)});
			}
	}
	for (int j = 0; j < n; ++j)
		for (int i = 0; i < n; ++i) {
			const int a = j * (n + 1) + i;
			const bool w = isWet[static_cast<std::size_t>(a)] && isWet[static_cast<std::size_t>(a) + 1] && isWet[static_cast<std::size_t>(a) + n + 1] && isWet[static_cast<std::size_t>(a) + n + 2];
			Mesh &m = w ? mirror : dry;
			m.tris.push_back({a, a + n + 1, a + 1});
			m.tris.push_back({a + 1, a + n + 1, a + n + 2});
		}
	// its colours: the land's, with snow where it lies
	char name[160];
	std::snprintf(name, sizeof name, "closeup_%016llx_%d_%d_%d_s%02d", static_cast<unsigned long long>(std::hash<std::string>()(landKey)), static_cast<int>(x0), static_cast<int>(z0),
				  static_cast<int>(S), static_cast<int>(std::lround(snow * 20.0)));
	const int W = 512;
	const std::string picture = film_sky::picture(film, name, W, W, [&](std::vector<float> &rgb) {
		for (int j = 0; j < W; ++j)
			for (int i = 0; i < W; ++i) {
				const double x = x0 + S * (i + 0.5) / W, z = z0 + S * (1.0 - (j + 0.5) / W);
				Vec3 col = colourAt(x, z) * (0.92 + 0.16 * nature::noise2(x * 6.0, z * 6.0, seed + 441));
				if (snow > 0.0) col = lerp(col, Vec3{0.84, 0.86, 0.9}, snow * (0.85 + 0.15 * nature::noise2(x * 2.0, z * 2.0, seed + 443)));
				const std::size_t q = (static_cast<std::size_t>(j) * W + i) * 3;
				rgb[q] = static_cast<float>(col.x), rgb[q + 1] = static_cast<float>(col.y), rgb[q + 2] = static_cast<float>(col.z);
			}
	});
	std::string out = "# the ground close up (film_ground.h)\nAttributeBegin\n  Texture \"closeup\" \"spectrum\" \"imagemap\" \"string filename\" [ \"" + picture + "\" ]\n";
	if (!dry.tris.empty()) {
		char buf[200];
		if (wet > 0.0) {
			std::snprintf(buf, sizeof buf, "  Texture \"closeup_wet\" \"spectrum\" \"scale\" \"texture tex\" \"closeup\" \"float scale\" [ %.4f ]\n", 1.0 - 0.45 * wet);
			out += buf;
			std::snprintf(buf, sizeof buf, "  Material \"coateddiffuse\" \"texture reflectance\" \"closeup_wet\" \"float roughness\" [ %.4f ]\n", 0.35 * (1.0 - wet) + 0.03);
			out += buf;
		} else {
			out += "  Material \"diffuse\" \"texture reflectance\" \"closeup\"\n";
		}
		out += emitMesh(dry, identity());
	}
	if (!mirror.tris.empty())   // the wet sand the wash has just left: dark, under a film of water as smooth as glass
		out += "  Texture \"closeup_mirror\" \"spectrum\" \"scale\" \"texture tex\" \"closeup\" \"float scale\" [ 0.45 ]\n  Material \"coateddiffuse\" \"texture reflectance\" \"closeup_mirror\" \"float roughness\" [ 0 ]\n" +
			   emitMesh(mirror, identity());
	return out + "AttributeEnd\n";
}

}  // namespace ground
}  // namespace cartoon
