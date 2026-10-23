#pragma once
// tengwang_city.h -- Hongzhou behind the Pavilion (tengwang.h): its walls of grey brick with battlements, the two-tiered tower over its west gate and towers at its corners, thousands
// of white-walled, dark-tiled houses in courtyards along a grid of streets, temples with golden roofs and a seven-storey pagoda over the roofs, the market lane inside the west
// gate with its stalls and hanging silks, and the villages among the fields on the far bank. Like the Pavilion, all of it is written once as PLY files per material.

#include "tengwang_pavilion.h"

namespace cartoon {
namespace tengwang {

constexpr int kCityVersion = 1;

// The city's walls: x from kCityX0 to kCityX1, z from kCityZ0 (the west wall, facing the river, behind the Pavilion) to kCityZ1.
constexpr double kCityX0 = -980.0, kCityX1 = 1180.0, kCityZ0 = 150.0, kCityZ1 = 2250.0;
constexpr double kWallHeight = 9.5;
constexpr double kGateX = 140.0;   // the west gate, a little north of the Pavilion
// The market lane: inside the west gate, running east from it.
constexpr double kMarketZ0 = 175.0, kMarketZ1 = 420.0;

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// A house: white walls, a dark tiled gable roof with a curved ridge, a wooden door and windows. `w` along its ridge, `d` deep, `h` to the eaves.
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
inline void addHouse(Parts &p, Vec3 base, double w, double d, double h, double yaw, int salt, const std::string &roofKey = "roof", const std::string &wallKey = "plaster") {
	const Mat4 frame = mul(translate(base), rotate({0, 1, 0}, yaw));
	const auto put = [&](const std::string &key, const Mesh &m, const Mat4 &local) { append(p[key], m, mul(frame, local)); };
	put(wallKey, box(w, h, d), translate(0.0, h / 2.0, 0.0));
	put("stone_dark", box(w + 0.1, 0.5, d + 0.1), translate(0.0, 0.25, 0.0));   // the plinth
	// the roof: two concave slopes from the ridge to the eaves, overhanging, and a ridge that lifts at its ends
	const double rise = d * 0.36, over = 0.7, ridgeY = h + rise;
	for (int side = 0; side < 2; ++side) {
		const double sz = side == 0 ? -1.0 : 1.0;
		Mesh slope;
		const int nx = 6, nz = 4;
		for (int j = 0; j <= nz; ++j)
			for (int i = 0; i <= nx; ++i) {
				const double u = static_cast<double>(i) / nx, r = static_cast<double>(j) / nz;
				const double x = lerp(-w / 2.0 - over, w / 2.0 + over, u), z = sz * lerp(0.0, d / 2.0 + over, r);
				const double lift = 0.35 * std::pow(std::abs(2.0 * u - 1.0), 4.0);
				slope.verts.push_back({x, ridgeY + lift * (1.0 - r) - (rise + over * 0.4) * (1.0 - std::pow(1.0 - r, 1.4)), z});
			}
		for (int j = 0; j < nz; ++j)
			for (int i = 0; i < nx; ++i) {
				const int q = j * (nx + 1) + i;
				slope.tris.push_back({q, q + 1, q + nx + 1});
				slope.tris.push_back({q + 1, q + nx + 2, q + nx + 1});
			}
		for (auto &t : slope.tris) {
			const Vec3 n = cross(slope.verts[static_cast<std::size_t>(t[1])] - slope.verts[static_cast<std::size_t>(t[0])], slope.verts[static_cast<std::size_t>(t[2])] - slope.verts[static_cast<std::size_t>(t[0])]);
			if (n.y < 0.0) std::swap(t[1], t[2]);
		}
		sdf::computeNormals(slope);
		put(roofKey, slope, identity());
	}
	// the gable ends (white), the ridge, the door and the windows
	for (const double sx : {-1.0, 1.0}) {
		Mesh gable;
		gable.verts = {{sx * w / 2.0, h, -d / 2.0}, {sx * w / 2.0, h, d / 2.0}, {sx * w / 2.0, ridgeY - 0.05, 0.0}};
		gable.normals.assign(3, Vec3{sx, 0.0, 0.0});
		gable.tris = {sx > 0 ? std::array<int, 3>{0, 2, 1} : std::array<int, 3>{0, 1, 2}};
		put(wallKey, gable, identity());
	}
	put("roof_ridge", box(w + 2.0 * over, 0.35, 0.4), translate(0.0, ridgeY + 0.1, 0.0));
	put("door", box(1.3, 2.2, 0.08), translate(lerp(-w / 3.0, w / 3.0, hash01(salt, 71)), 1.1, -d / 2.0 - 0.03));
	for (int k = 0; k < 2; ++k) put("door", box(0.9, 0.8, 0.06), translate((k == 0 ? -1.0 : 1.0) * w * 0.32, h * 0.62, -d / 2.0 - 0.03));
}

// A small hall or gate tower: a colonnade, lattice walls, brackets and one or two tiered roofs, like a small cousin of the Pavilion. `tileKey` picks the roofs' colour.
inline void addSmallHall(Parts &p, Vec3 c, double hx, double hz, double colH, int tiers, const std::string &tileKey) {
	Parts local;
	double y = 0.0;
	for (int tier = 0; tier < tiers; ++tier) {
		const double s = 1.0 - 0.18 * tier;
		addColumns(local, hx * s, hz * s, y, colH, (std::max)(2.5, hx * s / 2.0), 0.32);
		addLatticeWalls(local, hx * s * 0.82, hz * s * 0.78, y + 0.2, y + colH - 0.3, (std::max)(2.5, hx * s / 2.0), {0}, 1);
		addBrackets(local, hx * s, hz * s, y + colH, 0.6, 1.4);
		const bool top = tier == tiers - 1;
		const double eaveY = y + colH + 1.0;
		if (!top) {
			addSkirt(local, {hx * s * 0.86, hz * s * 0.86, eaveY + 2.4, hx * s + 2.8, hz * s + 2.8, eaveY, 1.2, 1.0});
			addBox(local["wood"], {0.0, eaveY + 2.6, 0.0}, {hx * s * 1.9, 0.4, hz * s * 1.9});
			y = eaveY + 2.8;
		} else {
			addSkirt(local, {hx * s * 0.45, (std::max)(0.6, hz * s * 0.25), eaveY + hz * s * 0.9 + 1.5, hx * s + 2.8, hz * s + 2.8, eaveY, 1.4, 1.1});
			addBox(local["ridge"], {0.0, eaveY + hz * s * 0.9 + 1.7, 0.0}, {hx * s * 0.9 + 1.0, 0.6, 0.5});
		}
	}
	for (auto &[key, m] : local.by) append(p[key == "tile" ? tileKey : key], m, translate(c));
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The city
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// A stretch of wall from a to b (on the plan), with a walk on top and merlons along its outer edge (towards `outward`).
inline void addWallRun(Parts &p, Vec3 a, Vec3 b, Vec3 outward, double gapFrom = -1.0, double gapTo = -1.0) {
	const double len = length(b - a);
	const Vec3 dir = normalize(b - a);
	const auto piece = [&](double u0, double u1) {
		if (u1 - u0 < 0.5) return;
		const Vec3 c = a + dir * ((u0 + u1) / 2.0);
		const double yaw = std::atan2(dir.x, dir.z) * 180.0 / kPi;
		addBox(p["brick"], {c.x, kGround + kWallHeight / 2.0 - 1.0, c.z}, {12.0, kWallHeight + 2.0, u1 - u0}, yaw);
		addBox(p["stone_dark"], {c.x, kGround + kWallHeight + 0.15, c.z}, {12.4, 0.3, u1 - u0}, yaw);
		for (double u = u0 + 1.0; u < u1 - 0.5; u += 2.4) {
			const Vec3 m = a + dir * u + outward * 5.8;
			addBox(p["brick"], {m.x, kGround + kWallHeight + 1.0, m.z}, {0.8, 1.7, 1.3}, yaw);
		}
	};
	if (gapFrom < 0.0) piece(0.0, len);
	else {
		piece(0.0, gapFrom);
		piece(gapTo, len);
	}
}

inline Parts cityParts() {
	Parts p;
	const double g = kGround;
	// ---- the walls, the west gate and its tower, the corner towers
	const Vec3 c00{kCityX0, g, kCityZ0}, c10{kCityX1, g, kCityZ0}, c11{kCityX1, g, kCityZ1}, c01{kCityX0, g, kCityZ1};
	addWallRun(p, c00, c10, {0.0, 0.0, -1.0}, kGateX - kCityX0 - 9.0, kGateX - kCityX0 + 9.0);
	addWallRun(p, c10, c11, {1.0, 0.0, 0.0});
	addWallRun(p, c11, c01, {0.0, 0.0, 1.0});
	addWallRun(p, c01, c00, {-1.0, 0.0, 0.0});
	// the gate: a thick block over a passage, and the tower on it
	addBox(p["brick"], {kGateX - 13.0, g + kWallHeight / 2.0 - 1.0, kCityZ0}, {8.0, kWallHeight + 2.0, 16.0});
	addBox(p["brick"], {kGateX + 13.0, g + kWallHeight / 2.0 - 1.0, kCityZ0}, {8.0, kWallHeight + 2.0, 16.0});
	addBox(p["brick"], {kGateX, g + kWallHeight - 1.6, kCityZ0}, {18.0, 3.2, 16.0});
	addBox(p["door"], {kGateX - 4.2, g + 3.2, kCityZ0 - 7.0}, {0.3, 6.4, 4.0}, 60.0);   // the great doors, open
	addBox(p["door"], {kGateX + 4.2, g + 3.2, kCityZ0 - 7.0}, {0.3, 6.4, 4.0}, -60.0);
	addSmallHall(p, {kGateX, g + kWallHeight + 0.3, kCityZ0}, 15.0, 7.0, 5.0, 2, "tile");
	for (const Vec3 &corner : {c00, c10, c11, c01}) addSmallHall(p, corner + Vec3{0.0, kWallHeight + 0.3, 0.0}, 6.0, 6.0, 4.0, 1, "roof");
	// a temple with golden roofs, a great hall (the governor's), and a seven-storey pagoda
	addSmallHall(p, {300.0, g + 2.0, 1150.0}, 16.0, 10.0, 6.0, 2, "tile_gold");
	addBox(p["stone"], {300.0, g + 1.0, 1150.0}, {44.0, 2.0, 30.0});
	addSmallHall(p, {-380.0, g + 1.5, 900.0}, 14.0, 9.0, 6.0, 1, "tile");
	addBox(p["stone"], {-380.0, g + 0.75, 900.0}, {36.0, 1.5, 26.0});
	{
		const Vec3 base{620.0, g, 700.0};
		double y = 0.0, r = 7.0;
		for (int storey = 0; storey < 7; ++storey) {
			const double h = 5.2 - storey * 0.25;
			append(p["brick_light"], lathe({{0.0, 0.0}, {r, 0.0}, {r * 0.97, h}, {0.0, h}}, 8), translate(base + Vec3{0.0, y, 0.0}));
			append(p["roof"], lathe({{0.0, h + 0.9}, {r * 0.8, h + 0.6}, {r + 1.6, h - 0.1}, {r + 1.7, h + 0.15}, {0.0, h + 1.0}}, 8), translate(base + Vec3{0.0, y, 0.0}));
			y += h + 0.8;
			r *= 0.9;
		}
		append(p["gold"], lathe({{0.0, 0.0}, {0.6, 0.0}, {0.3, 2.5}, {0.5, 3.0}, {0.12, 5.5}, {0.0, 6.0}}, 12), translate(base + Vec3{0.0, y, 0.0}));
	}
	// ---- the houses: blocks between the streets, each a ring of houses round a courtyard (left empty where the temples, the hall and the pagoda stand)
	const auto reserved = [](double x, double z) {
		return (std::abs(x - 300.0) < 40.0 && std::abs(z - 1150.0) < 30.0) || (std::abs(x + 380.0) < 34.0 && std::abs(z - 900.0) < 26.0) || (std::abs(x - 620.0) < 18.0 && std::abs(z - 700.0) < 18.0) ||
			   (std::abs(x - kGateX) < 30.0 && z < kCityZ0 + 34.0);
	};
	int salt = 0;
	for (double bz = kCityZ0 + 22.0; bz < kCityZ1 - 40.0; bz += 66.0)
		for (double bx = kCityX0 + 22.0; bx < kCityX1 - 40.0; bx += 70.0) {
			// the main avenues (east-west from the gate, north-south through the middle) are wider
			if (std::abs(bx + 35.0 - kGateX) < 40.0 && bz < kCityZ0 + 450.0) continue;
			const double cx = bx + 30.0, cz = bz + 28.0;
			for (int k = 0; k < 10; ++k) {
				++salt;
				const double side = k % 4;
				const double along = (k / 4 + 0.5) * 18.0 - 18.0;
				Vec3 at;
				double yaw;
				if (side == 0) { at = {cx + along, g, cz - 22.0}; yaw = 0.0; }
				else if (side == 1) { at = {cx + 22.0, g, cz + along}; yaw = 90.0; }
				else if (side == 2) { at = {cx - along, g, cz + 22.0}; yaw = 180.0; }
				else { at = {cx - 22.0, g, cz - along}; yaw = 270.0; }
				if (reserved(at.x, at.z) || hash01(salt, 72) < 0.12) continue;
				const double w = 9.0 + 6.0 * hash01(salt, 73), d = 6.0 + 2.5 * hash01(salt, 74), h = 3.2 + 1.8 * hash01(salt, 75) + (hash01(salt, 76) < 0.15 ? 3.0 : 0.0);
				addHouse(p, at, w, d, h, yaw + 2.0 * (hash01(salt, 77) - 0.5), salt);
			}
		}
	// ---- the market lane inside the west gate: stalls under awnings on both sides, bolts of silk hanging in every colour
	static const Vec3 silks[] = {{0.75, 0.08, 0.06}, {0.92, 0.62, 0.08}, {0.10, 0.35, 0.70}, {0.08, 0.55, 0.32}, {0.70, 0.22, 0.55}, {0.95, 0.85, 0.70}, {0.88, 0.40, 0.08}, {0.35, 0.15, 0.55}};
	for (double z = kMarketZ0; z < kMarketZ1; z += 7.5)
		for (const double sx : {-1.0, 1.0}) {
			++salt;
			const double x = kGateX + sx * 9.0;
			const Vec3 back{x + sx * 2.6, g, z};
			addHouse(p, back + Vec3{sx * 4.0, 0.0, 0.0}, 7.0, 6.0, 4.4, 90.0 * sx, salt);
			for (const double dz : {-3.0, 3.0}) addCylinder(p["wood"], {x, g, z + dz}, 2.8, 0.07, 0.06, 8);
			addBox(p["wood"], {x + sx * 1.2, g + 0.9, z}, {2.2, 0.12, 5.6});   // the counter
			const int col = static_cast<int>(8.0 * hash01(salt, 78));
			Mesh awning = box(3.4, 0.05, 6.6);
			append(p[std::string("silk_") + std::to_string(col)], awning, mul(translate(x + sx * 1.4, g + 3.0, z), rotate({0, 0, 1}, -sx * 14.0)));
			for (int b = 0; b < 4; ++b) {
				const int c2 = static_cast<int>(8.0 * hash01(salt * 5 + b, 79));
				append(p[std::string("silk_") + std::to_string(c2)], box(0.04, 1.7, 0.55), translate(x + sx * 0.1, g + 1.9, z - 2.2 + b * 1.45));
			}
		}
	(void)silks;
	return p;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The villages among the fields: a few houses round a threshing floor, under trees (tengwang_life.h plants the trees)
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
inline const std::vector<Vec3> &villagePlaces() {
	static const std::vector<Vec3> places = [] {
		std::vector<Vec3> v;
		for (int k = 0; k < 4000 && v.size() < 70; ++k) {
			const double x = -14000.0 + 30000.0 * hash01(k, 81), z = -9000.0 + 12000.0 * hash01(k, 82);
			if (fromWater(x, z) < 160.0 || cityMask(x, z) > 0.01 || groundHeight(x, z) > 20.0) continue;
			bool clear = true;
			for (const Vec3 &o : v) clear = clear && std::hypot(o.x - x, o.z - z) > 900.0;
			if (clear) v.push_back({x, groundHeight(x, z), z});
		}
		return v;
	}();
	return places;
}
inline Parts villageParts() {
	Parts p;
	int salt = 5000;
	for (const Vec3 &c : villagePlaces()) {
		const int n = 6 + static_cast<int>(10.0 * hash01(salt, 83));
		for (int k = 0; k < n; ++k) {
			++salt;
			const double a = 2.0 * kPi * hash01(salt, 84), r = 12.0 + 45.0 * std::sqrt(hash01(salt, 85));
			const double x = c.x + r * std::cos(a), z = c.z + r * std::sin(a);
			addHouse(p, {x, groundHeight(x, z) - 0.2, z}, 8.0 + 4.0 * hash01(salt, 86), 6.0, 3.0 + 0.8 * hash01(salt, 87), std::round(hash01(salt, 88) * 4.0) * 90.0 + 8.0 * (hash01(salt, 89) - 0.5), salt);
		}
	}
	return p;
}

inline std::string cityMaterial(const std::string &key) {
	if (key == "brick") return "Texture \"brick_checks\" \"spectrum\" \"checkerboard\" \"rgb tex1\" [ 0.34 0.33 0.32 ] \"rgb tex2\" [ 0.29 0.28 0.28 ] \"float uscale\" [ 0.6 ] \"float vscale\" [ 1.4 ]\n  Material \"diffuse\" \"texture reflectance\" [ \"brick_checks\" ]";
	if (key == "brick_light") return "Material \"diffuse\" \"rgb reflectance\" [ 0.58 0.52 0.44 ]";
	if (key == "plaster") return "Material \"diffuse\" \"rgb reflectance\" [ 0.80 0.78 0.72 ]";
	if (key == "roof") return "Material \"diffuse\" \"rgb reflectance\" [ 0.075 0.078 0.085 ]";
	if (key == "roof_ridge") return "Material \"diffuse\" \"rgb reflectance\" [ 0.06 0.06 0.07 ]";
	if (key == "door") return "Material \"coateddiffuse\" \"rgb reflectance\" [ 0.24 0.10 0.05 ] \"float roughness\" [ 0.3 ]";
	if (key == "tile_gold") return "Material \"coateddiffuse\" \"rgb reflectance\" [ 0.70 0.45 0.06 ] \"float roughness\" [ 0.08 ]";
	if (key.rfind("silk_", 0) == 0) {
		static const Vec3 silks[] = {{0.75, 0.08, 0.06}, {0.92, 0.62, 0.08}, {0.10, 0.35, 0.70}, {0.08, 0.55, 0.32}, {0.70, 0.22, 0.55}, {0.95, 0.85, 0.70}, {0.88, 0.40, 0.08}, {0.35, 0.15, 0.55}};
		const Vec3 c = silks[std::atoi(key.c_str() + 5) % 8];
		char buf[220];
		std::snprintf(buf, sizeof buf, "Material \"diffusetransmission\" \"rgb reflectance\" [ %.3f %.3f %.3f ] \"rgb transmittance\" [ %.3f %.3f %.3f ]", c.x * 0.8, c.y * 0.8, c.z * 0.8, c.x * 0.6, c.y * 0.6,
					  c.z * 0.6);
		return buf;
	}
	return pavilionMaterial(key, 0.0);
}
inline void brickUvs(Mesh &m) { stoneUvs(m, 0.5, 0.25); }

// The city and the villages as scene text (`villages`: the houses among the far fields too).
inline std::string cityText(bool villages = true) {
	struct Files {
		std::vector<std::pair<std::string, std::string>> city, village;
	};
	static const Files files = [] {
		Files f;
		static Parts city, village;
		static bool madeCity = false, madeVillage = false;
		static const char *cityKeys[] = {"brick", "brick_light", "plaster", "roof", "roof_ridge", "door", "stone", "stone_dark", "tile", "tile_gold", "ridge", "soffit", "fascia", "rafter", "paint_blue",
										 "paint_green", "paint_gold", "gold", "red", "lattice", "panel", "paper", "wood", "silk_0", "silk_1", "silk_2", "silk_3", "silk_4", "silk_5", "silk_6", "silk_7"};
		for (const char *k : cityKeys)
			f.city.push_back({k, film_cache::ply(kFilmKey, versioned(std::string("city_") + k, kCityVersion), [&]() {
								  if (!madeCity) {
									  city = cityParts();
									  brickUvs(city["brick"]);
									  stoneUvs(city["stone"], 1.4, 0.55);
									  madeCity = true;
								  }
								  return city[k];
							  })});
		for (const char *k : {"plaster", "roof", "roof_ridge", "door", "stone_dark"})
			f.village.push_back({k, film_cache::ply(kFilmKey, versioned(std::string("village_") + k, kCityVersion), [&]() {
									 if (!madeVillage) {
										 village = villageParts();
										 madeVillage = true;
									 }
									 return village[k];
								 })});
		return f;
	}();
	std::string out;
	for (const auto &[key, file] : files.city) out += attr(cityMaterial(key), film_cache::plyShape(file));
	if (villages)
		for (const auto &[key, file] : files.village) out += attr(cityMaterial(key), film_cache::plyShape(file));
	return out;
}

}  // namespace tengwang
}  // namespace cartoon
