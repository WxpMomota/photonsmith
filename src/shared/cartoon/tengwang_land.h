#pragma once
// tengwang_land.h -- the land of "Preface to the Pavilion of Prince Teng" (tengwang.h): the Gan River, the plain and the hills round Hongzhou, the day's light, the skies and the
// sea of clouds of the opening.
//
// Story space in metres: the Pavilion of Prince Teng stands at the origin on the east bank of the Gan, facing the river and the sunset to the west, -z; north is +x (the river
// flows that way, to Lake Pengli), south -x (it comes from the mountains there), and the city of Hongzhou lies behind the Pavilion, +z. The river's surface is at y = -4; the
// plain on the banks is about a metre above y = 0. An azimuth is measured from -z (west) towards +x (north): east is 180, south 270.
//
// The land is one large mesh round the Pavilion, finer near it (a ring a few metres wide at the bank, a hundred and fifty at the far hills), cut into the kinds of ground (rice
// stubble, green fields, fallow earth, woodland, rock, the river's banks), each written once as a PLY file (film_cache.h) and drawn with its own colour, so the plain is a
// patchwork of fields from the air.

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

#include "film_cache.h"
#include "film_sky.h"
#include "nature_lib.h"
#include "sdf_mesh.h"

namespace cartoon {
namespace tengwang {

constexpr const char *kFilmKey = "tengwang";
constexpr double kWater = -4.0;   // the river's surface
constexpr int kLandVersion = 4;   // raise when the land's meshes change (their files carry it)

inline std::string versioned(const std::string &name, int v = kLandVersion) { return name + "_v" + std::to_string(v); }

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The river and the ground
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The river's half width and its middle (z) at a point along it (x). The near bank passes just in front of the Pavilion (z = -46 at x = 0); downstream it widens towards the
// lake, upstream it narrows between the mountains.
inline double riverHalfWidth(double x) {
	double w = 430.0 + 70.0 * std::sin(x / 5100.0 + 1.0);
	w *= lerp(1.0, 0.42, smoothstep(-3500.0, -11000.0, x));
	w += 1400.0 * smoothstep(9000.0, 26000.0, x);
	return w;
}
inline double nearBank(double x) {
	double z = -46.0 - 170.0 * std::sin(x / 3300.0) - 60.0 * std::sin(x / 1150.0 + 0.4) + 60.0 * std::sin(0.4);
	z -= 650.0 * std::sin(x / 2700.0 + 1.3) * smoothstep(-3000.0, -8000.0, x);   // (the meanders upstream, between the mountains)
	return z;
}
inline double riverMiddle(double x) { return nearBank(x) - riverHalfWidth(x); }
// How far a point is from the water's edge: negative in the river, positive on land.
inline double fromWater(double x, double z) { return std::abs(z - riverMiddle(x)) - riverHalfWidth(x); }

// Ridged noise in [0, 1]: sharp crests, the shape of eroded hills.
inline double ridged(double x, double z, int salt) {
	double sum = 0.0, amp = 0.5, total = 0.0;
	for (int o = 0; o < 5; ++o) {
		const double n = 1.0 - std::abs(2.0 * nature::noise2(x, z, salt + o * 31) - 1.0);
		sum += amp * n * n;
		total += amp;
		x *= 2.07;
		z *= 2.07;
		amp *= 0.48;
	}
	return sum / total;
}

// The city's ground (flat), the Pavilion's terrace on the bank, and the shape of the river valley.
inline double cityMask(double x, double z) {
	const double inX = 1.0 - smoothstep(1100.0, 1700.0, std::abs(x - 100.0));
	const double inZ = smoothstep(-200.0, -40.0, z) * (1.0 - smoothstep(2300.0, 3200.0, z));
	return inX * inZ;
}

// The height of the ground at (x, z).
inline double groundHeight(double x, double z) {
	const double d = fromWater(x, z);
	if (d < 0.0) return kWater - 1.5 - (std::min)(7.0, -d * 0.04);   // the river bed
	// the banks rise from the water to the plain
	double h = kWater - 0.6 + 5.4 * smoothstep(0.0, 26.0, d);
	h += 1.6 * (nature::fbm2(x / 260.0, z / 260.0, 5) - 0.5) * smoothstep(20.0, 200.0, d);
	// the hills: the West Hills beyond the far bank, gentler hills behind the city, mountains closing in upstream
	const bool west = z < riverMiddle(x);
	const double reachW = smoothstep(2200.0, 7000.0, d), reachE = smoothstep(4500.0, 10000.0, d);
	const double upstream = smoothstep(-3000.0, -10000.0, x) * smoothstep(150.0, 1400.0, d);
	double mask = west ? reachW : reachE;
	mask = (std::max)(mask, upstream);
	const double rid = ridged(x / 5200.0 + 3.0, z / 5200.0, 21);
	double hills = mask * (west ? 640.0 : 380.0) * rid * (0.55 + 0.45 * nature::fbm2(x / 11000.0, z / 11000.0, 22));
	hills += upstream * 520.0 * ridged(x / 2600.0, z / 2600.0 + 9.0, 23);
	// low rolling rises on the plain, and the far mountains all round
	hills += 18.0 * smoothstep(300.0, 2500.0, d) * nature::fbm2(x / 1500.0, z / 1500.0, 24);
	const double r = std::hypot(x, z);
	hills += smoothstep(16000.0, 30000.0, r) * 1500.0 * ridged(x / 9000.0, z / 9000.0, 25);
	h += hills;
	// the city and the Pavilion's bank stay flat
	const double c = cityMask(x, z);
	return lerp(h, 1.0 + 0.4 * nature::fbm2(x / 90.0, z / 90.0, 26), c);
}
inline Vec3 groundNormal(double x, double z, double e) {
	return normalize({groundHeight(x - e, z) - groundHeight(x + e, z), 2.0 * e, groundHeight(x, z - e) - groundHeight(x, z + e)});
}

// The kinds of ground, each drawn in its own colour (the fields on the plain are plots of their own laid over it: fieldsText).
enum Ground { Bank = 0, Plain, WoodDark, WoodGold, WoodRust, Rock, Grass, CityGround, kGroundKinds };
inline const char *groundName(int g) {
	static const char *names[] = {"bank", "plain", "wooddark", "woodgold", "woodrust", "rock", "grass", "cityground"};
	return names[g];
}
inline Vec3 groundColour(int g) {
	static const Vec3 c[] = {{0.30, 0.26, 0.20}, {0.19, 0.16, 0.09}, {0.055, 0.075, 0.035}, {0.15, 0.13, 0.045}, {0.16, 0.075, 0.035}, {0.21, 0.19, 0.17}, {0.17, 0.18, 0.07}, {0.30, 0.25, 0.19}};
	return c[g];
}
// Woodland in autumn: dark evergreens with patches of gold and rust (the canopy's colour, which is what a wooded hill is from afar).
inline int woodKind(Vec3 p) {
	const double n = nature::fbm2(p.x / 260.0, p.z / 260.0, 35) + 0.25 * (hash01(static_cast<int>(p.x / 40.0) * 92821 + static_cast<int>(p.z / 40.0), 36) - 0.5);
	return n < 0.48 ? WoodDark : (n < 0.62 ? WoodGold : WoodRust);
}
// Is the plain flat and dry enough here for a field?
inline bool fieldLand(double x, double z) {
	return fromWater(x, z) > 45.0 && groundHeight(x, z) < 12.0 && cityMask(x, z) < 0.02 && nature::fbm2(x / 900.0, z / 900.0, 33) < 0.62;
}
// What grows at a point: the plain (under the fields), woods on the slopes and in groves among the fields, rock where it is steep or high.
inline int groundKind(Vec3 p, Vec3 n) {
	const double d = fromWater(p.x, p.z);
	if (d < 22.0) return Bank;
	if (cityMask(p.x, p.z) > 0.5) return CityGround;
	const double slope = 1.0 - n.y;
	if (p.y > 300.0 + 90.0 * nature::fbm2(p.x / 700.0, p.z / 700.0, 31) || slope > 0.36) return Rock;
	if (p.y > 30.0 || slope > 0.10) return nature::fbm2(p.x / 400.0, p.z / 400.0, 32) > 0.30 ? woodKind(p) : Grass;
	if (nature::fbm2(p.x / 900.0, p.z / 900.0, 33) > 0.62) return woodKind(p);   // groves among the fields
	return d < 60.0 ? Grass : Plain;
}

// The ring radii of the land's mesh: a few metres apart near the Pavilion, growing with the distance.
inline std::vector<double> landRings(double r0, double r1, double ratio, double minStep) {
	std::vector<double> rings{r0};
	while (rings.back() < r1) rings.push_back(rings.back() + (std::max)(minStep, rings.back() * ratio));
	return rings;
}

// The land round (cx, cz), `around` steps round, as one mesh per kind of ground (shared vertices only within a kind).
inline std::array<Mesh, kGroundKinds> landMeshes(double cx, double cz, double r0, double r1, int around, double ratio, double minStep) {
	const std::vector<double> rings = landRings(r0, r1, ratio, minStep);
	const int nr = static_cast<int>(rings.size());
	std::vector<Vec3> pts(static_cast<std::size_t>(nr) * around), nrm(pts.size());
	for (int i = 0; i < nr; ++i)
		for (int j = 0; j < around; ++j) {
			const double a = 2.0 * kPi * j / around;
			const double x = cx + rings[static_cast<std::size_t>(i)] * std::cos(a), z = cz + rings[static_cast<std::size_t>(i)] * std::sin(a);
			const double cell = (std::max)(1.0, rings[static_cast<std::size_t>(i)] * 2.0 * kPi / around);
			pts[static_cast<std::size_t>(i) * around + j] = {x, groundHeight(x, z), z};
			nrm[static_cast<std::size_t>(i) * around + j] = groundNormal(x, z, (std::min)(60.0, cell * 0.7));
		}
	std::array<Mesh, kGroundKinds> out;
	std::array<std::vector<int>, kGroundKinds> remap;
	for (auto &r : remap) r.assign(pts.size(), -1);
	const auto vert = [&](int kind, int idx) {
		int &m = remap[static_cast<std::size_t>(kind)][static_cast<std::size_t>(idx)];
		if (m < 0) {
			m = static_cast<int>(out[static_cast<std::size_t>(kind)].verts.size());
			out[static_cast<std::size_t>(kind)].verts.push_back(pts[static_cast<std::size_t>(idx)]);
			out[static_cast<std::size_t>(kind)].normals.push_back(nrm[static_cast<std::size_t>(idx)]);
		}
		return m;
	};
	for (int i = 0; i + 1 < nr; ++i)
		for (int j = 0; j < around; ++j) {
			const int a = i * around + j, b = i * around + (j + 1) % around, c = (i + 1) * around + j, d = (i + 1) * around + (j + 1) % around;
			for (const std::array<int, 3> &t : {std::array<int, 3>{a, c, b}, std::array<int, 3>{b, c, d}}) {
				const Vec3 p0 = pts[static_cast<std::size_t>(t[0])], p1 = pts[static_cast<std::size_t>(t[1])], p2 = pts[static_cast<std::size_t>(t[2])];
				const Vec3 centre = (p0 + p1 + p2) * (1.0 / 3.0);
				if (centre.y < kWater - 3.0) continue;   // (the deep river bed: never seen through the water's mirror)
				Vec3 fn = cross(p1 - p0, p2 - p0);
				if (fn.y < 0.0) fn = fn * -1.0;
				const int kind = groundKind(centre, normalize(fn));
				out[static_cast<std::size_t>(kind)].tris.push_back({vert(kind, t[0]), vert(kind, t[1]), vert(kind, t[2])});
			}
		}
	// (the winding: every triangle's face normal up)
	for (Mesh &m : out)
		for (auto &t : m.tris) {
			const Vec3 n = cross(m.verts[static_cast<std::size_t>(t[1])] - m.verts[static_cast<std::size_t>(t[0])], m.verts[static_cast<std::size_t>(t[2])] - m.verts[static_cast<std::size_t>(t[0])]);
			if (n.y < 0.0) std::swap(t[1], t[2]);
		}
	return out;
}

// The land as scene text: one PLY per kind of ground. `detail` 0 the whole valley (60 km across), 1 the same but finer near the Pavilion (for shots on the terrace and the bank).
inline std::string landText() {
	static const std::array<std::string, kGroundKinds> files = [] {
		std::array<std::string, kGroundKinds> f;
		std::array<Mesh, kGroundKinds> meshes;
		bool made = false;
		for (int g = 0; g < kGroundKinds; ++g)
			f[static_cast<std::size_t>(g)] = film_cache::ply(kFilmKey, versioned(std::string("land_") + groundName(g)), [&]() {
				if (!made) {
					meshes = landMeshes(0.0, -250.0, 25.0, 60000.0, 900, 0.0085, 2.5);
					made = true;
				}
				return meshes[static_cast<std::size_t>(g)];
			});
		return f;
	}();
	std::string out;
	for (int g = 0; g < kGroundKinds; ++g) {
		const Vec3 c = groundColour(g);
		const std::string mat = g == Bank ? "Material \"coateddiffuse\" \"rgb reflectance\" [ " + fmt3(c) + " ] \"float roughness\" [ 0.35 ]" : diffuseMaterial(c);
		out += attr(mat, film_cache::plyShape(files[static_cast<std::size_t>(g)]));
	}
	return out;
}

// The fields of the plain: plots laid out in a grid turned to follow the river, each a little above the ground with a strip of earth between it and the next (the dikes), in
// the colours of autumn: rice stubble after the harvest, late green crops, fallow earth, and here and there a flooded paddy that holds the sky.
enum FieldKind { StubbleA = 0, StubbleB, StubbleC, GreenA, GreenB, Fallow, Paddy, kFieldKinds };
inline const char *fieldName(int k) {
	static const char *names[] = {"stubble_a", "stubble_b", "stubble_c", "green_a", "green_b", "fallow", "paddy"};
	return names[k];
}
inline std::string fieldMaterial(int k) {
	static const Vec3 c[] = {{0.50, 0.37, 0.13}, {0.40, 0.29, 0.11}, {0.58, 0.47, 0.22}, {0.15, 0.22, 0.07}, {0.24, 0.28, 0.09}, {0.25, 0.16, 0.09}, {0.0, 0.0, 0.0}};
	if (k == Paddy) return "Material \"dielectric\" \"float eta\" [ 1.333 ]";
	return diffuseMaterial(c[k]);
}
inline std::array<Mesh, kFieldKinds> fieldMeshes() {
	std::array<Mesh, kFieldKinds> out;
	const double a = 0.25, ca = std::cos(a), sa = std::sin(a);
	const auto toWorld = [&](double u, double v) { return Vec3{u * ca - v * sa, 0.0, u * sa + v * ca}; };
	const double cellU = 110.0, cellV = 75.0, dike = 1.4;
	for (double v = -14000.0; v < 14000.0; v += cellV) {
		const int row = static_cast<int>(std::floor(v / cellV));
		const double shift = 37.0 * hash01(row, 41);   // (the rows do not line up)
		for (double u = -24000.0 + shift; u < 26000.0; u += cellU) {
			const int col = static_cast<int>(std::floor(u / cellU));
			// a plot is sometimes split in two along its length
			const bool split = hash01(col * 131 + row * 7, 42) < 0.35;
			for (int part = 0; part < (split ? 2 : 1); ++part) {
				const double u0 = u + (split ? part * cellU * 0.5 : 0.0) + dike, u1 = u + (split ? (part + 1) * cellU * 0.5 : cellU) - dike;
				const double v0 = v + dike, v1 = v + cellV - dike;
				bool ok = true;
				for (const auto &c : {std::pair<double, double>{u0, v0}, {u1, v0}, {u0, v1}, {u1, v1}, {(u0 + u1) / 2, (v0 + v1) / 2}}) {
					const Vec3 p = toWorld(c.first, c.second);
					ok = ok && fieldLand(p.x, p.z);
				}
				if (!ok) continue;
				const double h = hash01(col * 977 + row * 13 + part * 5, 43);
				const int kind = h < 0.22 ? StubbleA : h < 0.42 ? StubbleB : h < 0.55 ? StubbleC : h < 0.68 ? GreenA : h < 0.77 ? GreenB : h < 0.92 ? Fallow : Paddy;
				Mesh &m = out[static_cast<std::size_t>(kind)];
				const int base = static_cast<int>(m.verts.size());
				double level = 0.0;
				if (kind == Paddy) {   // (a flooded plot is level: its water stands at the plot's highest corner)
					level = -1e9;
					for (const auto &c : {std::pair<double, double>{u0, v0}, {u1, v0}, {u0, v1}, {u1, v1}}) {
						const Vec3 p = toWorld(c.first, c.second);
						level = (std::max)(level, groundHeight(p.x, p.z));
					}
				}
				for (int j = 0; j <= 2; ++j)
					for (int i = 0; i <= 2; ++i) {
						Vec3 p = toWorld(lerp(u0, u1, i / 2.0), lerp(v0, v1, j / 2.0));
						p.y = kind == Paddy ? level + 0.12 : groundHeight(p.x, p.z) + 0.3;
						m.verts.push_back(p);
						m.normals.push_back(kind == Paddy ? Vec3{0.0, 1.0, 0.0} : groundNormal(p.x, p.z, 8.0));
					}
				for (int j = 0; j < 2; ++j)
					for (int i = 0; i < 2; ++i) {
						const int q = base + j * 3 + i;
						m.tris.push_back({q, q + 3, q + 1});
						m.tris.push_back({q + 1, q + 3, q + 4});
					}
			}
		}
	}
	for (Mesh &m : out)
		for (auto &t : m.tris) {
			const Vec3 n = cross(m.verts[static_cast<std::size_t>(t[1])] - m.verts[static_cast<std::size_t>(t[0])], m.verts[static_cast<std::size_t>(t[2])] - m.verts[static_cast<std::size_t>(t[0])]);
			if (n.y < 0.0) std::swap(t[1], t[2]);
		}
	return out;
}
inline std::string fieldsText() {
	static const std::array<std::string, kFieldKinds> files = [] {
		std::array<std::string, kFieldKinds> f;
		std::array<Mesh, kFieldKinds> meshes;
		bool made = false;
		for (int k = 0; k < kFieldKinds; ++k)
			f[static_cast<std::size_t>(k)] = film_cache::ply(kFilmKey, versioned(std::string("field_") + fieldName(k)), [&]() {
				if (!made) {
					meshes = fieldMeshes();
					made = true;
				}
				return meshes[static_cast<std::size_t>(k)];
			});
		return f;
	}();
	std::string out;
	for (int k = 0; k < kFieldKinds; ++k) out += attr(fieldMaterial(k), film_cache::plyShape(files[static_cast<std::size_t>(k)]));
	return out;
}

// The river: one flat sheet of water over the whole valley (the land rises out of it), and the dark of the deep water under it.
inline std::string riverText(double roughness = 0.0) {
	Mesh water;
	const double s = 90000.0;
	water.verts = {{-s, kWater, -s}, {s, kWater, -s}, {s, kWater, s}, {-s, kWater, s}};
	water.normals.assign(4, Vec3{0.0, 1.0, 0.0});
	water.tris = {{0, 2, 1}, {0, 3, 2}};
	char mat[160];
	std::snprintf(mat, sizeof mat, "Material \"dielectric\" \"float eta\" [ 1.333 ] \"float roughness\" [ %.4f ]", roughness);
	Mesh bed = water;
	for (Vec3 &v : bed.verts) v.y = kWater - 9.0;
	return attr(mat, emitMesh(water, identity())) + attr(diffuseMaterial({0.018, 0.022, 0.020}), emitMesh(bed, identity()));
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The day
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The sun's height and direction over the film (it follows one day: a sunrise, the afternoon, the sunset over the West Hills, the blue hour, the night).
struct SunPlace {
	double elevation, azimuth;
};
inline Vec3 sunDir(const SunPlace &s) { return film_sky::direction(s.elevation, s.azimuth); }

// The sun as a glowing ball 25 km away (about 1.2 degrees across, a little larger than the real one, as a film's sun is), its colour from the air it crosses.
inline Vec3 sunColourAt(double elevation) {
	const double low = 1.0 - smoothstep(-1.0, 14.0, elevation);
	return lerp(Vec3{1.0, 0.93, 0.80}, Vec3{1.0, 0.42, 0.12}, low);
}
inline std::string sunText(const SunPlace &s, double strength = 1.0, Vec3 from = {0.0, 0.0, 0.0}) {
	if (s.elevation < -1.5 || strength <= 0.0) return std::string();
	const double up = smoothstep(-1.5, 0.6, s.elevation);
	const double dist = 25000.0, radius = dist * std::tan(0.6 * kPi / 180.0);
	// (its irradiance is about 3 scene units, the sky pictures' own scale: L = E / (pi (r/d)^2))
	const double L = 3.0 / (kPi * std::pow(radius / dist, 2.0));
	return emissiveSphere(from + sunDir(s) * dist, radius, sunColourAt(s.elevation) * (L * up * strength));
}

}  // namespace tengwang
}  // namespace cartoon
