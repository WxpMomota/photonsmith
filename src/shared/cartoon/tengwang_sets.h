#pragma once
// tengwang_sets.h -- the sets and props of "Preface to the Pavilion of Prince Teng" (tengwang.h) beyond the land, the Pavilion, the city and its people:
//   the banquet hall      the second tier's hall dressed for the feast: low lacquered tables in two rows with bronze cups and dishes, red mats, tall bronze lamp stands, silk
//                         curtains between the columns, a gilded screen behind the governor's seat, musicians on the floor between the rows
//   props                 the wine ewer and its stream, a pipa, bamboo scrolls, the brush, the inkstone and the paper, a jade pendant, a carriage, a wine jar, an incense burner
//   the procession        horses, a carriage and banners going in at the west gate
//   the wetland           reeds by the water and egrets rising from them
//   the ginkgo branch     the leaf that falls past the railing
//   the dreams            the ink world (paper, strokes of ink, the land in black and white), the mountain pass in the rain and the crossroads, the palace doors that close, the
//                         general in the snow, the stone sinking in deep water, the eagle's cliff
// Story space and conventions as in tengwang_land.h.

#include "tengwang_life.h"

namespace cartoon {
namespace tengwang {

constexpr int kSetsVersion = 3;

inline std::string lacquer(Vec3 c, double rough = 0.05) {
	char buf[200];
	std::snprintf(buf, sizeof buf, "Material \"coateddiffuse\" \"rgb reflectance\" [ %.3f %.3f %.3f ] \"float roughness\" [ %.3f ]", c.x, c.y, c.z, rough);
	return buf;
}
inline std::string bronze(double rough = 0.25) {
	char buf[200];
	std::snprintf(buf, sizeof buf, "Material \"conductor\" \"rgb reflectance\" [ 0.62 0.42 0.20 ] \"float roughness\" [ %.3f ]", rough);
	return buf;
}
inline std::string goldMat() { return pavilionMaterial("gold", 0.0); }
// A flame: a small white-hot core in a warm glow (`size` its height), and the light it casts (an unseen point light, so the flame's own ball needs no more than its look).
inline std::string flameText(Vec3 at, double size, double strength, double t, int salt) {
	const double flicker = 1.0 + 0.08 * std::sin(t * 23.0 + salt) + 0.05 * std::sin(t * 37.0 + 2.0 * salt);
	std::string out = emissiveSphere(at + Vec3{0.0, size * 0.35, 0.0}, size * 0.18, Vec3{1.0, 0.78, 0.42} * (60.0 * flicker));
	out += emissiveSphere(at + Vec3{0.0, size * 0.6, 0.0}, size * 0.11, Vec3{1.0, 0.55, 0.18} * (35.0 * flicker));
	if (strength <= 0.0) return out;   // (a flame that only glows: its light comes from elsewhere)
	const Vec3 I = Vec3{1.0, 0.62, 0.30} * (strength * flicker);
	char buf[200];
	std::snprintf(buf, sizeof buf, "LightSource \"point\" \"point3 from\" [ %.4f %.4f %.4f ] \"rgb I\" [ %.4f %.4f %.4f ]\n", -at.x, at.y + size * 0.5, at.z, I.x, I.y, I.z);
	return out + buf;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The banquet hall (the second tier, floor at kDeck2; the hall is 29 m by 17 m, open to the balcony and the river at -z)
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
struct Seat {
	Vec3 at;      // where the guest kneels (floor level)
	double yaw;   // the way they face
};
// The seats: two rows facing each other across the middle of the hall (along x), the governor's at the east end facing west, Wang Bo's the last of the river-side row.
inline const std::vector<Seat> &hallSeats() {
	static const std::vector<Seat> seats = [] {
		std::vector<Seat> v;
		for (int i = 0; i < 7; ++i) {
			const double x = 8.0 - 2.9 * i;
			v.push_back({{x, kDeck2, -3.6}, 0.0});     // the river-side row, facing +z (towards the middle)
			v.push_back({{x, kDeck2, 3.6}, 180.0});    // the far row, facing -z
		}
		v.push_back({{11.8, kDeck2, 0.0}, -90.0});     // the governor, facing west down the hall
		return v;
	}();
	return seats;
}
inline Vec3 wangBoSeat() { return hallSeats()[12].at; }   // (the last of the river-side row, at x = 8 - 2.9 * 6)
inline Vec3 tableBefore(const Seat &s) { return s.at + Vec3{std::sin(s.yaw * kPi / 180.0) * 0.75, 0.0, std::cos(s.yaw * kPi / 180.0) * 0.75}; }
// The lamp stands round the hall, where their flames are.
inline const std::vector<Vec3> &hallLamps() {
	static const std::vector<Vec3> lamps = [] {
		std::vector<Vec3> v;
		for (int i = 0; i < 6; ++i)
			for (const double z : {-6.4, 6.4}) v.push_back({9.5 - 3.9 * i, kDeck2 + 1.62, z});
		for (const double z : {-2.2, 2.2}) v.push_back({13.4, kDeck2 + 1.62, z});
		return v;
	}();
	return lamps;
}

inline Parts hallParts() {
	Parts p;
	const double f = kDeck2;
	// the tables, with dishes and cups; the mats
	for (const Seat &s : hallSeats()) {
		const Vec3 c = tableBefore(s);
		const double yaw = s.yaw;
		const bool host = s.at.x > 11.0;
		const double w = host ? 2.4 : 1.6;
		addBox(p["lacquer_black"], c + Vec3{0.0, 0.33, 0.0}, {w, 0.05, 0.62}, yaw + 90.0);
		addBox(p["lacquer_red"], c + Vec3{0.0, 0.355, 0.0}, {w - 0.12, 0.012, 0.5}, yaw + 90.0);
		for (const double sx : {-1.0, 1.0})
			for (const double sz : {-1.0, 1.0}) {
				const Vec3 off = {sx * (w / 2.0 - 0.08), 0.0, sz * 0.24};
				const double a = (yaw + 90.0) * kPi / 180.0;
				addBox(p["lacquer_black"], c + Vec3{off.x * std::cos(a) + off.z * std::sin(a), 0.16, -off.x * std::sin(a) + off.z * std::cos(a)}, {0.06, 0.32, 0.06});
			}
		addBox(p["mat"], s.at + Vec3{0.0, 0.015, 0.0}, {1.0, 0.03, 1.0}, yaw);
		// a bronze cup, two dishes and a bowl of fruit (Wang Bo's table is cleared for the paper)
		if (length(s.at - Vec3{8.0 - 2.9 * 6, kDeck2, -3.6}) < 0.1) continue;
		const double a = (yaw + 90.0) * kPi / 180.0;
		const auto along = [&](double u, double v) { return c + Vec3{u * std::cos(a) + v * std::sin(a), 0.36, -u * std::sin(a) + v * std::cos(a)}; };
		append(p["bronze"], lathe({{0.0, 0.0}, {0.05, 0.0}, {0.03, 0.03}, {0.035, 0.08}, {0.06, 0.13}, {0.055, 0.135}, {0.0, 0.06}}, 16), translate(along(0.35, 0.05)));
		append(p["porcelain"], lathe({{0.0, 0.0}, {0.07, 0.0}, {0.14, 0.03}, {0.13, 0.035}, {0.0, 0.01}}, 20), translate(along(-0.25, 0.08)));
		append(p["porcelain"], lathe({{0.0, 0.0}, {0.06, 0.0}, {0.12, 0.025}, {0.11, 0.03}, {0.0, 0.008}}, 20), translate(along(0.05, -0.12)));
		append(p["fruit"], uvSphere(0.04, 10, 6), translate(along(-0.25, 0.08) + Vec3{0.02, 0.05, 0.0}));
		append(p["fruit"], uvSphere(0.04, 10, 6), translate(along(-0.22, 0.12) + Vec3{0.0, 0.05, 0.03}));
	}
	// the lamp stands: a bronze stem on three feet, a dish on top (the flames are added with the lights)
	for (const Vec3 &l : hallLamps()) {
		const Vec3 base{l.x, f, l.z};
		append(p["bronze"], lathe({{0.0, 0.0}, {0.28, 0.0}, {0.24, 0.06}, {0.05, 0.12}, {0.035, 1.40}, {0.07, 1.45}, {0.20, 1.52}, {0.21, 1.56}, {0.0, 1.53}}, 18), translate(base));
	}
	// silk curtains hanging between the inner columns along both sides, gathered to the sides with gold cords
	static const Vec3 curtainColours[] = {{0.72, 0.08, 0.06}, {0.85, 0.62, 0.12}};
	for (int i = -2; i < 2; ++i)
		for (const double z : {-3.8, 3.8}) {
			const double x0 = i * 5.4 + 0.35, x1 = (i + 1) * 5.4 - 0.35;
			for (int side = 0; side < 2; ++side) {
				Mesh c;
				const int nu = 18, nv = 10;
				for (int j = 0; j <= nv; ++j)
					for (int k = 0; k <= nu; ++k) {
						const double u = static_cast<double>(k) / nu, v = static_cast<double>(j) / nv;
						// gathered: wide at the top, drawn in towards its column lower down
						const double gather = side == 0 ? lerp(0.45, 0.15, v) : lerp(0.45, 0.15, v);
						const double x = side == 0 ? lerp(x0, x0 + (x1 - x0) * gather, u) : lerp(x1 - (x1 - x0) * gather, x1, u);
						c.verts.push_back({x, f + kCol2 - 0.5 - v * (kCol2 - 0.6), z + 0.08 * std::sin(u * 22.0 + side) * (1.0 - 0.5 * v)});
					}
				for (int j = 0; j < nv; ++j)
					for (int k = 0; k < nu; ++k) {
						const int q = j * (nu + 1) + k;
						c.tris.push_back({q, q + 1, q + nu + 2});
						c.tris.push_back({q, q + nu + 2, q + nu + 1});
					}
				sdf::computeNormals(c);
				append(p[(i + 2) % 2 ? "silk_red" : "silk_gold"], c, identity());
			}
		}
	(void)curtainColours;
	// the gilded screen behind the governor's seat: six panels in a zigzag
	for (int k = 0; k < 6; ++k) {
		const double z = -3.0 + 1.0 * (k + 0.5);
		addBox(p["screen_gold"], {13.6 + (k % 2 ? 0.12 : -0.12), f + 1.15, z}, {0.04, 2.1, 0.98}, (k % 2 ? 8.0 : -8.0));
		addBox(p["lacquer_black"], {13.6 + (k % 2 ? 0.12 : -0.12), f + 2.22, z}, {0.07, 0.06, 1.0}, (k % 2 ? 8.0 : -8.0));
	}
	// bronze ding tripods at the hall's corners
	for (const double x : {-12.5, 12.5})
		for (const double z : {-6.6, 6.6}) {
			append(p["bronze"], lathe({{0.0, 0.25}, {0.35, 0.28}, {0.42, 0.55}, {0.40, 0.75}, {0.46, 0.80}, {0.0, 0.80}}, 20), translate(x, f, z));
			for (int k = 0; k < 3; ++k) {
				const double a = 2.0 * kPi * k / 3.0;
				append(p["bronze"], capsule(0.04, 0.3, 8, 2), translate(x + 0.25 * std::cos(a), f, z + 0.25 * std::sin(a)));
			}
		}
	return p;
}
inline std::string hallMaterial(const std::string &key) {
	if (key == "lacquer_black") return lacquer({0.015, 0.012, 0.012}, 0.04);
	if (key == "lacquer_red") return lacquer({0.55, 0.04, 0.03}, 0.04);
	if (key == "mat") return "Material \"diffuse\" \"rgb reflectance\" [ 0.55 0.12 0.08 ]";
	if (key == "bronze") return bronze(0.22);
	if (key == "porcelain") return "Material \"coateddiffuse\" \"rgb reflectance\" [ 0.70 0.80 0.74 ] \"float roughness\" [ 0.02 ]";   // celadon
	if (key == "fruit") return "Material \"coateddiffuse\" \"rgb reflectance\" [ 0.85 0.35 0.05 ] \"float roughness\" [ 0.2 ]";
	// (deep, rich silks that let only a glow of the window through)
	if (key == "silk_red") return "Material \"diffusetransmission\" \"rgb reflectance\" [ 0.46 0.035 0.04 ] \"rgb transmittance\" [ 0.16 0.015 0.012 ]";
	if (key == "silk_gold") return "Material \"diffusetransmission\" \"rgb reflectance\" [ 0.06 0.20 0.30 ] \"rgb transmittance\" [ 0.02 0.07 0.10 ]";
	if (key == "screen_gold") return "Material \"conductor\" \"spectrum eta\" [ \"metal-Au-eta\" ] \"spectrum k\" [ \"metal-Au-k\" ] \"float roughness\" [ 0.32 ]";
	return diffuseMaterial({0.5, 0.5, 0.5});
}
// The hall's furniture (PLY files) and, if `lit`, its lamps' flames and light.
inline std::string hallText(double t, double lit = 1.0) {
	static const std::vector<std::pair<std::string, std::string>> files = [] {
		std::vector<std::pair<std::string, std::string>> f;
		static Parts parts;
		static bool made = false;
		for (const char *k : {"lacquer_black", "lacquer_red", "mat", "bronze", "porcelain", "fruit", "silk_red", "silk_gold", "screen_gold"})
			f.push_back({k, film_cache::ply(kFilmKey, versioned(std::string("hall_") + k, kSetsVersion), [&]() {
							 if (!made) {
								 parts = hallParts();
								 made = true;
							 }
							 return parts[k];
						 })});
		return f;
	}();
	std::string out;
	for (const auto &[key, file] : files) out += attr(hallMaterial(key), film_cache::plyShape(file));
	if (lit > 0.0) {
		int k = 0;
		// the lamps' flames (their own glow only: a point light per flame made a hall frame three times as slow, for light that the few lights below give as well)
		for (const Vec3 &l : hallLamps()) out += flameText(l, 0.12, 0.0, t, ++k);
		// the light of the lanterns hung in the beams, filling the hall: five, staggered down its length
		const double spots[5][2] = {{-2.0, -2.2}, {-1.0, 2.2}, {0.0, -2.2}, {1.0, 2.2}, {2.0, -2.2}};
		for (const auto &s : spots) {
			char buf[200];
			std::snprintf(buf, sizeof buf, "LightSource \"point\" \"point3 from\" [ %.3f %.3f %.3f ] \"rgb I\" [ %.3f %.3f %.3f ]\n", -5.4 * s[0], kDeck2 + 4.05, s[1], 29.0 * lit, 21.0 * lit, 13.5 * lit);   // (just under the lantern's tassel)
			out += buf;
			// the lantern the light hangs under: red silk on a cord from the ceiling, glowing from within
			const Vec3 c{5.4 * s[0], kDeck2 + 5.15, s[1]};
			out += attr("Material \"diffusetransmission\" \"rgb reflectance\" [ 0.62 0.06 0.04 ] \"rgb transmittance\" [ 0.75 0.12 0.05 ]",
						emitMesh(lathe({{0.0, -0.40}, {0.25, -0.38}, {0.42, -0.24}, {0.48, 0.0}, {0.42, 0.24}, {0.25, 0.38}, {0.0, 0.40}}, 24), translate(c)));
			out += attr(pavilionMaterial("gold", 0.0), emitMesh(lathe({{0.0, 0.0}, {0.22, 0.0}, {0.20, 0.10}, {0.0, 0.10}}, 16), translate(c + Vec3{0.0, 0.38, 0.0})) +
														  emitMesh(lathe({{0.0, 0.0}, {0.20, 0.0}, {0.22, 0.10}, {0.0, 0.10}}, 16), translate(c - Vec3{0.0, 0.48, 0.0})));
			out += attr("Material \"diffuse\" \"rgb reflectance\" [ 0.6 0.05 0.04 ]", bar(c + Vec3{0.0, 0.45, 0.0}, c + Vec3{0.0, 0.95, 0.0}, 0.012, 5) + bar(c - Vec3{0.0, 0.5, 0.0}, c - Vec3{0.0, 0.95, 0.0}, 0.03, 6));
			out += emissiveSphere(c, 0.16, Vec3{1.0, 0.55, 0.22} * (14.0 * lit));
		}
	}
	return out;
}
// The guests in their seats (Wang Bo's seat left empty: he is sculpted for each frame), and the musicians between the rows.
inline std::vector<PersonPlace> hallGuests() {
	std::vector<PersonPlace> v;
	int k = 0;
	for (const Seat &s : hallSeats()) {
		++k;
		if (length(s.at - wangBoSeat()) < 0.1) continue;
		v.push_back({Kneeling, (k * 3) % 10, false, s.at, s.yaw});
	}
	for (int m = 0; m < 4; ++m) v.push_back({Kneeling, (m * 2 + 1) % 10, true, {9.6 - 1.3 * m, kDeck2, 0.4}, -60.0 - 10.0 * m});
	return v;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// Props
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// A pipa: a pear-shaped wooden body with a bent-back neck and four strings, at `frame` (its body's middle at the origin, the neck along +y).
inline std::string pipaText(const Mat4 &frame) {
	std::string out = attr(lacquer({0.42, 0.20, 0.07}, 0.12), emitMesh(lathe({{0.0, -0.40}, {0.18, -0.36}, {0.25, -0.18}, {0.21, 0.05}, {0.10, 0.22}, {0.04, 0.30}, {0.0, 0.31}}, 24), mul(frame, scale(1.0, 1.0, 0.25))));
	out += attr(lacquer({0.25, 0.10, 0.04}, 0.15), emitMesh(box(0.06, 0.32, 0.04), mul(frame, translate(0.0, 0.44, 0.0))));
	out += attr(lacquer({0.25, 0.10, 0.04}, 0.15), emitMesh(box(0.07, 0.10, 0.05), mul(frame, translate(0.0, 0.63, -0.03), rotate({1, 0, 0}, -60.0))));
	std::string strings;
	for (int k = 0; k < 4; ++k) strings += bar(transformPoint(frame, {-0.03 + 0.02 * k, -0.30, 0.065}), transformPoint(frame, {-0.015 + 0.01 * k, 0.60, 0.03}), 0.0018, 4);
	return out + attr(goldMat(), strings);
}
// The wine ewer pouring: a bronze ewer tilted by `tilt` degrees at `frame`, and its stream of wine (a thin glassy column that curves down to the cup).
inline std::string ewerText(const Mat4 &frame, double tilt, Vec3 cupMouth, double flow) {
	const Mat4 f = mul(frame, rotate({0, 0, 1}, -tilt));
	std::string out = attr(bronze(0.18), emitMesh(lathe({{0.0, 0.0}, {0.07, 0.0}, {0.11, 0.06}, {0.12, 0.14}, {0.07, 0.22}, {0.05, 0.27}, {0.06, 0.29}, {0.0, 0.29}}, 20), f));
	out += attr(bronze(0.18), bar(transformPoint(f, {0.08, 0.16, 0.0}), transformPoint(f, {0.20, 0.27, 0.0}), 0.018, 8));   // the spout
	if (flow > 0.0) {
		const Vec3 spout = transformPoint(f, {0.205, 0.275, 0.0});
		std::vector<Vec3> pts;
		for (int k = 0; k <= 10; ++k) {
			const double u = k / 10.0;
			pts.push_back(lerp(spout, cupMouth, u) + Vec3{0.06 * std::sin(kPi * u), 0.08 * std::sin(kPi * u), 0.0});
		}
		out += attr("Material \"dielectric\" \"float eta\" [ 1.36 ]", emitMesh(sdf::tubeMesh(pts, 0.006 * flow, 1.0, 8), identity()));
	}
	return out;
}
inline std::string cupText(Vec3 at, double fill) {
	std::string out = attr(bronze(0.2), emitMesh(lathe({{0.0, 0.0}, {0.05, 0.0}, {0.03, 0.03}, {0.035, 0.08}, {0.06, 0.13}, {0.055, 0.135}, {0.05, 0.08}, {0.0, 0.07}}, 24), translate(at)));
	if (fill > 0.0)
		out += attr("Material \"dielectric\" \"float eta\" [ 1.36 ]",
					emitMesh(lathe({{0.0, 0.07}, {0.05, 0.08}, {0.048 + 0.008 * fill, 0.08 + 0.045 * fill}, {0.0, 0.08 + 0.045 * fill}}, 24), translate(at)));
	return out;
}
// A bundle of bamboo slips tied with red cord, a scroll of paper, the inkstone, the brush and the sheet of paper Wang Bo writes on.
inline std::string bambooScrollText(const Mat4 &frame) {
	std::string slats;
	for (int k = 0; k < 14; ++k) slats += emitMesh(box(0.012, 0.32, 0.03), mul(frame, translate(-0.09 + 0.0135 * k, 0.0, 0.004 * std::sin(k * 1.3)), rotate({0, 1, 0}, 3.0 * std::sin(k))));
	std::string out = attr("Material \"coateddiffuse\" \"rgb reflectance\" [ 0.62 0.48 0.22 ] \"float roughness\" [ 0.35 ]", slats);
	out += attr("Material \"diffuse\" \"rgb reflectance\" [ 0.65 0.06 0.05 ]", emitMesh(box(0.21, 0.015, 0.04), mul(frame, translate(0.0, 0.06, 0.0))) + emitMesh(box(0.21, 0.015, 0.04), mul(frame, translate(0.0, -0.08, 0.0))));
	return out;
}
// The writing things on a table at `at` (the table's top), turned `yaw`: the sheet (the manuscript with `columns` columns written), the inkstone, the brush (in a hand or on its rest).
inline std::string paperText(Vec3 at, double yaw, int columns) {
	static const int stages[] = {0, 3, 6, 9, 12};
	int stage = 0;
	for (int s : stages)
		if (columns >= s) stage = s;
	const std::string image = film_cache::asset("tengwang/manuscript_" + std::to_string(stage) + ".jpg");
	Mesh sheet;
	const double w = 0.66, d = 0.44;
	sheet.verts = {{-w / 2.0, 0.0, -d / 2.0}, {w / 2.0, 0.0, -d / 2.0}, {w / 2.0, 0.0, d / 2.0}, {-w / 2.0, 0.0, d / 2.0}};
	sheet.normals.assign(4, Vec3{0.0, 1.0, 0.0});
	// (the picture's top is the sheet's far edge, its right-hand column at the writer's right)
	sheet.uvs = {{1.0, 0.0}, {0.0, 0.0}, {0.0, 1.0}, {1.0, 1.0}};
	sheet.tris = {{0, 2, 1}, {0, 3, 2}};
	const Mat4 frame = mul(translate(at + Vec3{0.0, 0.004, 0.0}), rotate({0, 1, 0}, yaw));
	std::string out;
	if (!image.empty())
		out += "AttributeBegin\n  Texture \"sheet\" \"spectrum\" \"imagemap\" \"string filename\" [ \"" + image + "\" ]\n  Material \"coateddiffuse\" \"texture reflectance\" [ \"sheet\" ] \"float roughness\" [ 0.5 ]\n" +
			   emitMesh(sheet, frame) + "AttributeEnd\n";
	else out += attr(diffuseMaterial({0.85, 0.80, 0.68}), emitMesh(sheet, frame));
	// the inkstone (dark stone with a pool of ink) at the sheet's right
	out += attr("Material \"coateddiffuse\" \"rgb reflectance\" [ 0.05 0.05 0.06 ] \"float roughness\" [ 0.3 ]", emitMesh(roundedBox(0.16, 0.035, 0.22, 0.01, 2), mul(frame, translate(0.44, 0.0175, 0.05))));
	out += attr("Material \"coateddiffuse\" \"rgb reflectance\" [ 0.005 0.005 0.006 ] \"float roughness\" [ 0.02 ]", emitMesh(lathe({{0.0, 0.0}, {0.05, 0.0}, {0.0, 0.0005}}, 16), mul(frame, translate(0.44, 0.036, 0.11))));
	return out;
}
// A writing brush from `tipAt` along `dir`: a bamboo shaft and a tapering tip, black with ink.
inline std::string brushText(Vec3 tipAt, Vec3 dir) {
	const Vec3 d = normalize(dir);
	std::string out = attr("Material \"coateddiffuse\" \"rgb reflectance\" [ 0.55 0.40 0.16 ] \"float roughness\" [ 0.3 ]", emitMesh(capsule(0.0055, 0.20, 10, 2), alignY(tipAt + d * 0.035, d)));
	out += attr("Material \"coateddiffuse\" \"rgb reflectance\" [ 0.01 0.01 0.01 ] \"float roughness\" [ 0.15 ]",
				emitMesh(lathe({{0.0, 0.0}, {0.004, 0.006}, {0.0075, 0.025}, {0.0065, 0.04}, {0.0, 0.042}}, 12), alignY(tipAt, d)));
	return out;
}
// A jade pendant (a green disc with a hole) on a red silk cord with a tassel, swinging by `swing` degrees from `top`.
inline std::string jadeText(Vec3 top, double swing) {
	const Mat4 f = mul(translate(top), rotate({0, 0, 1}, swing));
	std::string out = attr(subsurfaceMaterial({0.25, 0.62, 0.38}, {0.02, 0.05, 0.03}, 0.05), emitMesh(torus(0.032, 0.014, 32, 12), mul(f, translate(0.0, -0.16, 0.0), rotate({1, 0, 0}, 90.0), scale(1.0, 0.35, 1.0))));
	out += attr("Material \"diffuse\" \"rgb reflectance\" [ 0.70 0.06 0.05 ]", emitMesh(capsule(0.003, 0.11, 6, 1), mul(f, translate(0.0, -0.12, 0.0))) + emitMesh(capsule(0.01, 0.09, 8, 2), mul(f, translate(0.0, -0.31, 0.0))));
	return out;
}
// A carriage: a box with a curved canopy on two spoked wheels (turned by `roll` radians), drawn by its horse.
inline std::string carriageText(Vec3 at, double yaw, double roll, Vec3 colour) {
	const Mat4 f = mul(translate(at), rotate({0, 1, 0}, yaw));
	std::string out = attr(lacquer(colour, 0.15), emitMesh(box(1.4, 1.1, 1.8), mul(f, translate(0.0, 1.45, 0.0))));
	out += attr(lacquer({0.08, 0.06, 0.05}, 0.2), emitMesh(lathe({{0.0, 0.0}, {1.1, 0.0}, {1.0, 0.25}, {0.0, 0.42}}, 20), mul(f, translate(0.0, 2.0, 0.0), scale(0.75, 1.0, 1.0))));
	std::string wheels;
	for (const double sx : {-0.85, 0.85}) {
		const Mat4 w = mul(f, translate(sx, 0.75, 0.0), rotate({0, 0, 1}, 90.0), rotate({0, 1, 0}, roll * 180.0 / kPi));
		wheels += emitMesh(torus(0.72, 0.045, 32, 8), w);
		for (int k = 0; k < 12; ++k) {
			const double a = 2.0 * kPi * k / 12.0;
			wheels += bar(transformPoint(w, {0.0, 0.0, 0.0}), transformPoint(w, {0.70 * std::cos(a), 0.0, 0.70 * std::sin(a)}), 0.018, 5);
		}
		wheels += emitMesh(uvSphere(0.1, 10, 6), w);
	}
	out += attr(lacquer({0.22, 0.12, 0.05}, 0.3), wheels);
	out += attr(lacquer({0.22, 0.12, 0.05}, 0.3), bar(transformPoint(f, {-0.3, 1.0, 0.9}), transformPoint(f, {-0.3, 1.3, 3.0}), 0.04, 6) + bar(transformPoint(f, {0.3, 1.0, 0.9}), transformPoint(f, {0.3, 1.3, 3.0}), 0.04, 6));
	return out;
}
// A wine jar of brown glazed pottery with a red cloth over its mouth, lifted off by `open` (0..1).
inline std::string wineJarText(Vec3 at, double open) {
	std::string out = attr("Material \"coateddiffuse\" \"rgb reflectance\" [ 0.24 0.12 0.05 ] \"float roughness\" [ 0.12 ]",
						   emitMesh(lathe({{0.0, 0.0}, {0.16, 0.0}, {0.26, 0.15}, {0.27, 0.32}, {0.16, 0.50}, {0.09, 0.54}, {0.10, 0.58}, {0.0, 0.58}}, 28), translate(at)));
	out += attr("Material \"diffusetransmission\" \"rgb reflectance\" [ 0.70 0.06 0.05 ] \"rgb transmittance\" [ 0.5 0.05 0.03 ]",
				emitMesh(lathe({{0.0, 0.07}, {0.12, 0.05}, {0.15, -0.04}, {0.13, -0.06}, {0.0, 0.03}}, 24), mul(translate(at + Vec3{0.18 * open, 0.58 + 0.25 * open, 0.0}), rotate({0, 0, 1}, -40.0 * open))));
	return out;
}
// A bronze incense burner with three threads of smoke rising and curling (thin pale strands).
inline std::string incenseText(Vec3 at, double t) {
	std::string out = attr(bronze(0.3), emitMesh(lathe({{0.0, 0.0}, {0.10, 0.02}, {0.16, 0.10}, {0.15, 0.16}, {0.11, 0.18}, {0.0, 0.17}}, 24), translate(at)));
	std::string smoke;
	for (int k = 0; k < 3; ++k) {
		std::vector<Vec3> pts;
		for (int j = 0; j <= 16; ++j) {
			const double u = j / 16.0;
			pts.push_back(at + Vec3{0.02 * (k - 1) + 0.06 * u * std::sin(t * 0.8 + u * 7.0 + k), 0.18 + 0.7 * u, 0.05 * u * std::cos(t * 0.6 + u * 5.0 + 2.0 * k)});
		}
		smoke += emitMesh(sdf::tubeMesh(pts, 0.0035, 2.0, 6), identity());
	}
	return out + attr("Material \"diffusetransmission\" \"rgb reflectance\" [ 0.7 0.7 0.72 ] \"rgb transmittance\" [ 0.8 0.8 0.82 ]", smoke);
}
// A banner on a pole: a tall strip of silk with a scalloped edge, rippling in the wind (`wind` 0 calm .. 1 snapping).
inline std::string bannerText(Vec3 base, double height, Vec3 colour, double t, double wind, double yaw = 0.0) {
	const Mat4 f = mul(translate(base), rotate({0, 1, 0}, yaw));
	std::string out = attr(lacquer({0.30, 0.10, 0.05}, 0.3), bar(transformPoint(f, {0.0, 0.0, 0.0}), transformPoint(f, {0.0, height, 0.0}), 0.05, 8));
	Mesh cloth;
	const int nu = 14, nv = 10;
	const double w = height * 0.32, h = height * 0.55;
	for (int j = 0; j <= nv; ++j)
		for (int i = 0; i <= nu; ++i) {
			const double u = static_cast<double>(i) / nu, v = static_cast<double>(j) / nv;
			const double ripple = wind * 0.22 * u * std::sin(u * 7.0 - t * (4.0 + 6.0 * wind) + v * 1.5);
			cloth.verts.push_back({u * w * (1.0 - 0.15 * wind * std::sin(t * 3.0)), height - 0.1 - v * h * (1.0 - 0.1 * u), ripple * w});
		}
	for (int j = 0; j < nv; ++j)
		for (int i = 0; i < nu; ++i) {
			const int q = j * (nu + 1) + i;
			cloth.tris.push_back({q, q + 1, q + nu + 2});
			cloth.tris.push_back({q, q + nu + 2, q + nu + 1});
		}
	sdf::computeNormals(cloth);
	char mat[220];
	std::snprintf(mat, sizeof mat, "Material \"diffusetransmission\" \"rgb reflectance\" [ %.3f %.3f %.3f ] \"rgb transmittance\" [ %.3f %.3f %.3f ]", colour.x, colour.y, colour.z, colour.x * 0.6, colour.y * 0.6, colour.z * 0.6);
	return out + attr(mat, emitMesh(cloth, f));
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// Reeds and egrets
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
inline const Mesh &reedClump() {
	static const Mesh m = [] {
		Mesh r;
		for (int k = 0; k < 40; ++k) {
			const double a = 2.0 * kPi * hash01(k, 121), rr = 0.5 * std::sqrt(hash01(k, 122)), h = 1.6 + 1.0 * hash01(k, 123);
			const Vec3 root{rr * std::cos(a), 0.0, rr * std::sin(a)}, lean{0.3 * (hash01(k, 124) - 0.5), 0.0, 0.3 * (hash01(k, 125) - 0.5)};
			nature::addStrip(r, [&](double u) { return root + Vec3{0.0, h * u, 0.0} + lean * (u * u * h); }, [](double u) { return 0.02 * (1.0 - u * 0.7); }, normalize(Vec3{std::cos(a), 0.0, std::sin(a)}), 4);
			// a feathery plume at the top
			const Vec3 top = root + Vec3{0.0, h, 0.0} + lean * h;
			nature::addLeaf(r, top, normalize(lean + Vec3{0.0, 0.6, 0.0}), {1.0, 0.0, 0.0}, 0.32, 0.035);
		}
		return r;
	}();
	return m;
}
inline std::string reedsText(Vec3 centre, double radius, int count, double t) {
	std::string out = nature::objectBegin("reeds") +
					  attr("Material \"diffusetransmission\" \"rgb reflectance\" [ 0.70 0.60 0.42 ] \"rgb transmittance\" [ 0.55 0.45 0.25 ]",
						   film_cache::plyShape(film_cache::ply(kFilmKey, versioned("reeds", kSetsVersion), [] { return reedClump(); }))) +
					  nature::objectEnd();
	for (int k = 0; k < count; ++k) {
		const double a = 2.0 * kPi * hash01(k, 126), r = radius * std::sqrt(hash01(k, 127));
		const Vec3 at{centre.x + r * std::cos(a), kWater - 0.3, centre.z + r * std::sin(a)};
		const Vec3 lean = nature::windLean(at.x, at.z, t, 0.05);
		out += nature::placeObject("reeds", mul(translate(at), nature::shearXZ(lean.x, lean.z), rotate({0, 1, 0}, 360.0 * hash01(k, 128)), scale(0.8 + 0.5 * hash01(k, 129))));
	}
	return out;
}
// An egret: white, long-necked, rising from the reeds on broad slow wingbeats.
inline std::string egretText(Vec3 at, Vec3 dir, double t, int salt) { return birdText(at, dir, std::sin(t * 2.0 * kPi * 0.9 + salt), 2.1, {0.92, 0.92, 0.90}, {0.95, 0.95, 0.93}); }

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The ginkgo branch and its falling leaf
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// A ginkgo leaf: a fan with a notch, on a short stalk. Local: the stalk at the origin, the fan opening along +y.
inline Mesh ginkgoLeaf() {
	Mesh m;
	const int n = 16;
	m.verts.push_back({0.0, 0.0, 0.0});
	for (int k = 0; k <= n; ++k) {
		const double a = lerp(-0.85, 0.85, static_cast<double>(k) / n);
		const double r = 0.055 * (1.0 - 0.25 * std::exp(-std::pow(a / 0.08, 2.0)));   // the notch in the middle of the edge
		m.verts.push_back({r * std::sin(a), 0.035 + r * std::cos(a), 0.004 * std::sin(a * 3.0)});
	}
	for (int k = 0; k < n; ++k) m.tris.push_back({0, k + 1, k + 2});
	m.normals.assign(m.verts.size(), Vec3{0.0, 0.0, 1.0});
	return m;
}
inline std::string ginkgoBranchText(Vec3 base, Vec3 dir, double t, bool withFallingLeaf, Vec3 fallingAt, double fallingSpin) {
	std::string out = attr(barkMaterial(), emitMesh(sdf::tubeMesh({base, base + dir * 0.6 + Vec3{0.0, 0.05, 0.0}, base + dir * 1.3 + Vec3{0.0, -0.05, 0.1}}, 0.025, 0.35, 8), identity()));
	std::string leaves;
	for (int k = 0; k < 40; ++k) {
		const double u = 0.25 + 0.75 * hash01(k, 131);
		const Vec3 at = base + dir * (1.3 * u) + Vec3{0.08 * (hash01(k, 132) - 0.5), 0.05 * (hash01(k, 133) - 0.5), 0.08 * (hash01(k, 134) - 0.5)};
		const double sway = 6.0 * std::sin(t * 1.7 + k);
		leaves += emitMesh(ginkgoLeaf(), mul(translate(at), rotate({0, 1, 0}, 360.0 * hash01(k, 135)), rotate({1, 0, 0}, 120.0 + 50.0 * hash01(k, 136) + sway)));
	}
	if (withFallingLeaf) leaves += emitMesh(ginkgoLeaf(), mul(translate(fallingAt), rotate({0, 1, 0}, fallingSpin * 1.3), rotate({1, 0, 0}, fallingSpin), scale(1.8)));
	return out + attr(leafMaterial({0.95, 0.72, 0.08}), leaves);
}

}  // namespace tengwang
}  // namespace cartoon
