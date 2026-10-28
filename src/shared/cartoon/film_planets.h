#pragma once
// film_planets.h -- planets for a film in space (film_file.h `planet`; round 10): worlds lit by the sun (film_space.h), a day side, a night side and the soft line
// between, turning on their axes.
//
//   kinds      rocky (grey highlands, dark plains, craters with bright rims and rays), desert (rust and ochre, dark rock, dunes in bands, white polar caps),
//              ice (white and pale blue, crossed by long brown cracks), ocean (continents of forest, grass, desert and mountain under swirls of cloud, blue seas
//              that catch the sun in a glint, white polar caps), gas (a giant: bands of cloud torn into eddies along their edges, a great oval storm) and lava
//              (dark crust broken by glowing rivers of lava - they light its night side);
//   air        (`air S [colour blue|orange|white|green]`) a shell of air round it, thinning with height (a scale height of 1.2% of its radius), that scatters the
//              sunlight - blue most, as air does (Rayleigh), or an orange haze, white cloud or green - so its rim glows against space, its day side is a little
//              hazed, and along the line between day and night the light through the air comes red; one grid (film_clouds.h's kind, in the film's cache) in a
//              sphere round the planet;
//   surface    a picture painted once for the kind and its seed (the sphere's own pbrt coordinates: row 0 its north pole), on pbrt's sphere turned so its poles are
//              up (tipped by `tilt`) and spinning once every `spin` seconds; an ocean world's seas glossy (a coated surface whose roughness is a second picture).
//
// Coordinates are pbrt's, as a film file's.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "cartoon_lib.h"
#include "film_cache.h"
#include "film_clouds.h"
#include "film_sky.h"
#include "film_space.h"

namespace cartoon {
namespace planets {

struct Planet {
	std::string name, kind = "rocky";
	Vec3 at{0.0, 0.0, -20000.0};
	double radius = 4000.0, tilt = 15.0, spin = 600.0;
	int seed = 1;
	double air = 0.0;                 // how thick its air is (0 none, 1 an Earth's)
	std::string airColour = "blue";   // blue (Rayleigh), orange (a haze), white (cloud), green
};
inline const std::vector<std::string> &airColours() {
	static const std::vector<std::string> k = {"blue", "orange", "white", "green"};
	return k;
}
inline const std::vector<std::string> &kinds() {
	static const std::vector<std::string> k = {"rocky", "desert", "ice", "ocean", "gas", "lava"};
	return k;
}

namespace detail {
using clouds::detail::fbm3;
// The unit direction in a planet's own frame (z its pole) of the picture's pixel (i, j) of W x H (row 0 the north pole).
inline Vec3 dirOf(int i, int j, int W, int H) {
	const double theta = (j + 0.5) / H * kPi, phi = (i + 0.5) / W * 2.0 * kPi;
	return {std::sin(theta) * std::cos(phi), std::sin(theta) * std::sin(phi), std::cos(theta)};
}
inline double fbm(Vec3 o, double scale, int seed) { return fbm3(o.x * scale + 11.0, o.y * scale + 23.0, o.z * scale + 37.0, seed); }
// Craters: so many centres on the sphere with their sizes; at o, how much of a crater's floor (dark) and its rim (bright) there is.
inline void craters(Vec3 o, int seed, int count, double &floorDark, double &rimBright) {
	floorDark = 0.0, rimBright = 0.0;
	for (int k = 0; k < count; ++k) {
		const double z = 2.0 * hash01(seed * 131 + k, 1) - 1.0, a = 2.0 * kPi * hash01(seed * 131 + k, 2), r = std::sqrt(1.0 - z * z);
		const Vec3 c{r * std::cos(a), r * std::sin(a), z};
		const double size = 0.015 + 0.12 * std::pow(hash01(seed * 131 + k, 3), 3.0), d = std::acos(std::clamp(dot(o, c), -1.0, 1.0)) / size;
		if (d > 4.0) continue;
		floorDark = std::max(floorDark, 1.0 - smoothstep(0.7, 1.0, d));
		rimBright = std::max(rimBright, 0.7 * std::exp(-std::pow((d - 1.05) / 0.25, 2.0)));
		if (size > 0.06) rimBright = std::max(rimBright, 0.35 * std::exp(-(d - 1.0) / 1.2) * (d > 1.0 ? 1.0 : 0.0) * smoothstep(0.55, 0.8, fbm3(o.x * 40.0, o.y * 40.0, o.z * 40.0, seed + k)));   // (rays)
	}
}
}  // namespace detail

// A planet's surface colour (linear albedo) at the unit direction o of its own frame, and (out) how glossy it is there (1 a sea) and how much it glows (lava).
inline Vec3 surfaceAt(const Planet &p, Vec3 o, double &sea, double &glow) {
	sea = 0.0, glow = 0.0;
	const int s = p.seed * 97;
	const double lat = std::asin(std::clamp(o.z, -1.0, 1.0)) * 180.0 / kPi;
	if (p.kind == "gas") {   // bands of cloud, their edges torn into eddies; a great oval storm
		const double warp = 0.12 * (detail::fbm(o, 6.0, s + 1) - 0.5) + 0.04 * (detail::fbm(o, 18.0, s + 2) - 0.5);
		const double y = o.z + warp, band = 0.5 + 0.5 * std::sin(y * 22.0 + 2.0 * std::sin(y * 7.0));
		const Vec3 pale{0.86, 0.80, 0.68}, rust{0.62, 0.40, 0.25}, cream{0.93, 0.89, 0.80}, brown{0.42, 0.30, 0.22};
		Vec3 c = lerp(lerp(rust, pale, band), lerp(brown, cream, band), 0.5 + 0.5 * std::sin(y * 9.0 + 1.3));
		const Vec3 spotC = normalize(Vec3{0.8, 0.2, -0.35});
		const double dx = std::acos(std::clamp(dot(normalize(Vec3{o.x, o.y, 0.0} + Vec3{1e-9, 0, 0}), normalize(Vec3{spotC.x, spotC.y, 0.0})), -1.0, 1.0)), dz = o.z - spotC.z;
		const double spot = 1.0 - smoothstep(0.7, 1.0, std::hypot(dx / 0.22, dz / 0.09));
		c = lerp(c, Vec3{0.72, 0.30, 0.18} * (0.85 + 0.3 * detail::fbm(o, 30.0, s + 3)), spot);
		return c * (0.9 + 0.2 * detail::fbm(o, 40.0, s + 4));
	}
	const double land = detail::fbm(o, 2.2, s + 5) + 0.35 * (detail::fbm(o, 9.0, s + 6) - 0.5);
	const double detail1 = detail::fbm(o, 14.0, s + 7), fine = detail::fbm(o, 60.0, s + 8);
	if (p.kind == "rocky" || p.kind == "lava") {
		double floorDark = 0.0, rimBright = 0.0;
		detail::craters(o, s, p.kind == "rocky" ? 70 : 25, floorDark, rimBright);
		const double maria = smoothstep(0.55, 0.62, land);
		Vec3 c = lerp(Vec3{0.38, 0.36, 0.34}, Vec3{0.16, 0.155, 0.15}, maria) * (0.8 + 0.4 * detail1) * (0.9 + 0.2 * fine);
		c = c * (1.0 - 0.35 * floorDark) + Vec3{0.2, 0.19, 0.18} * rimBright;
		if (p.kind == "lava") {   // dark crust and glowing rivers of lava along the cracks
			c = c * 0.35;
			const double crack = 1.0 - smoothstep(0.0, 0.025, std::abs(detail::fbm(o, 7.0, s + 9) - 0.5)) * (0.7 + 0.3 * detail1);
			glow = crack * smoothstep(0.35, 0.6, detail::fbm(o, 3.0, s + 10)) + 0.6 * floorDark * maria;
		}
		return c;
	}
	if (p.kind == "ice") {   // white and pale blue, crossed by long brown cracks
		const double crack = 1.0 - smoothstep(0.0, 0.015, std::abs(detail::fbm(o, 5.0, s + 11) - 0.5));
		const double crack2 = 1.0 - smoothstep(0.0, 0.01, std::abs(detail::fbm(o, 11.0, s + 12) - 0.5));
		Vec3 c = lerp(Vec3{0.82, 0.86, 0.9}, Vec3{0.62, 0.72, 0.82}, detail1);
		return lerp(c, Vec3{0.45, 0.32, 0.22}, std::max(crack, 0.7 * crack2)) * (0.95 + 0.1 * fine);
	}
	const double cap = smoothstep(72.0, 78.0, std::abs(lat) + 6.0 * (detail1 - 0.5));
	if (p.kind == "desert") {   // rust and ochre, dark rock, dunes in bands, white polar caps
		Vec3 c = lerp(Vec3{0.62, 0.36, 0.2}, Vec3{0.72, 0.52, 0.3}, detail1);
		c = lerp(c, Vec3{0.3, 0.2, 0.15}, smoothstep(0.6, 0.7, land));
		c = c * (0.92 + 0.08 * std::sin(o.z * 300.0 + 20.0 * fine));
		return lerp(c, Vec3{0.88, 0.88, 0.9}, cap);
	}
	// an ocean world: continents (forest, grass, desert by latitude, mountains) in blue seas, white caps, swirls of cloud
	Vec3 c;
	if (land > 0.53) {
		const double hot = 1.0 - smoothstep(10.0, 35.0, std::abs(lat)), dry = smoothstep(0.45, 0.6, detail1) * (1.0 - smoothstep(25.0, 40.0, std::abs(lat))) * smoothstep(12.0, 18.0, std::abs(lat));
		c = lerp(Vec3{0.12, 0.2, 0.08}, Vec3{0.2, 0.32, 0.1}, hot);
		c = lerp(c, Vec3{0.62, 0.5, 0.32}, dry);
		c = lerp(c, Vec3{0.32, 0.28, 0.24}, smoothstep(0.66, 0.72, land));
	} else {
		sea = 1.0;
		c = lerp(Vec3{0.015, 0.05, 0.12}, Vec3{0.03, 0.12, 0.2}, smoothstep(0.45, 0.53, land));   // (shallower near the coasts)
	}
	c = lerp(c, Vec3{0.88, 0.9, 0.92}, cap);
	if (cap > 0.5) sea = 0.0;
	const Vec3 q{o.x, o.y, o.z * 2.2};   // (clouds stretched along the latitudes, in swirls)
	const double cloud = smoothstep(0.52, 0.72, detail::fbm(normalize(q), 5.0, s + 13) + 0.25 * (detail::fbm(o, 20.0, s + 14) - 0.5));
	if (cloud > 0.0) sea *= 1.0 - cloud;
	return lerp(c, Vec3{0.92, 0.93, 0.95}, 0.9 * cloud);
}

// A planet's pictures, painted once for its kind and seed: its colour (always), how glossy it is (an ocean world) and how it glows (lava). Their file paths.
struct Pictures {
	std::string colour, gloss, glow;
};
inline Pictures pictures(const std::string &film, const Planet &p) {
	const int W = 2048, H = 1024;
	Pictures out;
	const std::string base = "planet_v2_" + p.kind + "_" + std::to_string(p.seed);
	out.colour = film_sky::picture(film, base + "_colour", W, H, [&](std::vector<float> &rgb) {
		for (int j = 0; j < H; ++j)
			for (int i = 0; i < W; ++i) {
				double sea = 0.0, glow = 0.0;
				const Vec3 c = surfaceAt(p, detail::dirOf(i, j, W, H), sea, glow);
				const std::size_t q = (static_cast<std::size_t>(j) * W + i) * 3;
				rgb[q] = static_cast<float>(c.x), rgb[q + 1] = static_cast<float>(c.y), rgb[q + 2] = static_cast<float>(c.z);
			}
	});
	if (p.kind == "ocean")
		out.gloss = film_sky::picture(film, base + "_roughness", W / 2, H / 2, [&](std::vector<float> &rgb) {
			for (int j = 0; j < H / 2; ++j)
				for (int i = 0; i < W / 2; ++i) {
					double sea = 0.0, glow = 0.0;
					surfaceAt(p, detail::dirOf(i, j, W / 2, H / 2), sea, glow);
					const float r = static_cast<float>(0.8 - 0.75 * sea);   // (land rough, the sea smooth enough to show the sun's glint)
					const std::size_t q = (static_cast<std::size_t>(j) * (W / 2) + i) * 3;
					rgb[q] = rgb[q + 1] = rgb[q + 2] = r;
				}
		});
	if (p.kind == "lava")
		out.glow = film_sky::picture(film, base + "_glow", W, H, [&](std::vector<float> &rgb) {
			for (int j = 0; j < H; ++j)
				for (int i = 0; i < W; ++i) {
					double sea = 0.0, glow = 0.0;
					surfaceAt(p, detail::dirOf(i, j, W, H), sea, glow);
					const Vec3 c = Vec3{1.0, 0.35, 0.06} * glow;
					const std::size_t q = (static_cast<std::size_t>(j) * W + i) * 3;
					rgb[q] = static_cast<float>(c.x), rgb[q + 1] = static_cast<float>(c.y), rgb[q + 2] = static_cast<float>(c.z);
				}
		});
	return out;
}

// The transform that places a planet at nature's time `nt`: at its place, its pole tipped by `tilt` (about the line of sight's x axis), turned by its spin, pbrt's sphere's
// z pole turned up.
inline std::string placement(const Planet &p, double nt) {
	char buf[240];
	std::snprintf(buf, sizeof buf, "  Translate %.4f %.4f %.4f\n  Rotate %.4f 1 0 0\n  Rotate %.4f 0 1 0\n  Rotate -90 1 0 0\n", p.at.x, p.at.y, p.at.z, p.tilt,
				  std::fmod(360.0 * nt / std::max(1.0, p.spin), 360.0));
	return buf;
}
// ---- air ----
constexpr int kAirN = 144;
constexpr double kAirScale = 0.012, kAirTop = 8.0;   // (the scale height as a share of the radius; the shell's top in scale heights)
// The air's grid over the box round a planet of radius 1 (half-width 1 + kAirTop scale heights): its density by height, 1 at the ground (and below it, inside the
// planet, so the trilinear look-up does not dip at the surface), falling off by e every scale height, 0 above the top.
inline std::vector<float> airDensities() {
	const int n = kAirN;
	const double half = 1.0 + kAirTop * kAirScale;
	std::vector<float> d(static_cast<std::size_t>(n) * n * n, 0.0f);
	for (int k = 0; k < n; ++k)
		for (int j = 0; j < n; ++j)
			for (int i = 0; i < n; ++i) {
				const double x = ((i + 0.5) / n - 0.5) * 2.0 * half, y = ((j + 0.5) / n - 0.5) * 2.0 * half, z = ((k + 0.5) / n - 0.5) * 2.0 * half;
				const double r = std::sqrt(x * x + y * y + z * z), h = (r - 1.0) / kAirScale;
				if (r < 0.96 || h > kAirTop) continue;
				d[(static_cast<std::size_t>(k) * n + j) * n + i] = static_cast<float>(h <= 0.0 ? 1.0 : std::exp(-h) * (1.0 - smoothstep(kAirTop - 1.5, kAirTop, h)));
			}
	return d;
}
// The air's scattering per metre at the ground for a planet (blue the strongest for Rayleigh air), its phase g.
inline Vec3 airSigma(const Planet &p, double &g) {
	const double tangent = std::sqrt(2.0 * kPi * kAirScale) * p.radius;   // (the path along the ground's tangent through the air, its density integrated)
	const double k = 1.5 * p.air / tangent;
	g = p.airColour == "blue" ? 0.0 : 0.4;
	if (p.airColour == "orange") return Vec3{1.0, 0.62, 0.3} * (0.6 * k);
	if (p.airColour == "white") return Vec3{1.0, 1.0, 1.0} * (0.5 * k);
	if (p.airColour == "green") return Vec3{0.35, 1.0, 0.55} * (0.7 * k);
	return Vec3{0.12, 0.35, 1.0} * k;
}
// The file holding a planet's air (a MakeNamedMedium over the unit box), made once in the film's cache.
inline std::string airFile(const std::string &film, const Planet &p, const std::string &name) {
	double g = 0.0;
	const Vec3 sg = airSigma(p, g);
	char file[160];
	std::snprintf(file, sizeof file, "air_v1_%s_%.6g_%.6g_%.6g_%.2f.pbrt", name.c_str(), sg.x, sg.y, sg.z, g);
	const std::filesystem::path target = film_cache::dir(film) / file;
	std::error_code ec;
	if (!(std::filesystem::exists(target, ec) && std::filesystem::file_size(target, ec) > 0)) {
		const std::vector<float> d = airDensities();
		std::string text;
		text.reserve(d.size() * 3 + 400);
		char buf[400];
		std::snprintf(buf, sizeof buf, "MakeNamedMedium \"%s\" \"string type\" [ \"uniformgrid\" ]\n  \"integer nx\" [ %d ] \"integer ny\" [ %d ] \"integer nz\" [ %d ]\n  \"rgb sigma_a\" [ 0 0 0 ] \"rgb sigma_s\" [ %.6g %.6g %.6g ] \"float g\" [ %.2f ]\n  \"float density\" [\n",
					  name.c_str(), kAirN, kAirN, kAirN, sg.x, sg.y, sg.z, g);
		text += buf;
		int col = 0;
		for (float v : d) {
			if (v <= 0.0f) text += "0 ";
			else {
				std::snprintf(buf, sizeof buf, "%.4f ", v);
				text += buf;
			}
			if (++col == 32) text += "\n", col = 0;
		}
		text += "\n  ]\n";
		if (!film_cache::writeOnce(target, text)) throw std::runtime_error("film " + film + ": could not write " + target.string());
	}
	return film_cache::scenePath(target);
}
// A planet's air as scene text: its grid in the box round it, and the pass-through sphere it fills.
inline std::string airText(const std::string &film, const Planet &p) {
	if (p.air <= 0.0) return std::string();
	const std::string name = "air_" + p.name;
	const double half = p.radius * (1.0 + kAirTop * kAirScale);
	char buf[320];
	std::snprintf(buf, sizeof buf, "AttributeBegin\n  Translate %.4f %.4f %.4f\n  Scale %.4f %.4f %.4f\n", p.at.x - half, p.at.y - half, p.at.z - half, 2.0 * half, 2.0 * half, 2.0 * half);
	std::string out = "# its air (film_planets.h)\n" + std::string(buf) + "  Include \"" + airFile(film, p, name) + "\"\nAttributeEnd\n";
	std::snprintf(buf, sizeof buf, "AttributeBegin\n  Translate %.4f %.4f %.4f\n  Material \"interface\"\n  MediumInterface \"%s\" \"\"\n  Shape \"sphere\" \"float radius\" [ %.4f ]\nAttributeEnd\n", p.at.x,
				  p.at.y, p.at.z, name.c_str(), half * 1.74);
	return out + buf;
}

// A planet at nature's time `nt`, as scene text.
inline std::string planetText(const std::string &film, const Planet &p, double nt) {
	const Pictures pic = pictures(film, p);
	std::string out = "# a planet (film_planets.h): " + p.name + "\nAttributeBegin\n" + placement(p, nt);
	out += "  Texture \"" + p.name + "_colour\" \"spectrum\" \"imagemap\" \"string filename\" [ \"" + pic.colour + "\" ]\n";
	if (!pic.glow.empty()) {
		char scale[32];
		std::snprintf(scale, sizeof scale, "%.4f", 1.5);
		out += "  AreaLightSource \"diffuse\" \"string filename\" [ \"" + pic.glow + "\" ] \"float scale\" [ " + scale + " ]\n";
	}
	if (!pic.gloss.empty()) {
		out += "  Texture \"" + p.name + "_roughness\" \"float\" \"imagemap\" \"string filename\" [ \"" + pic.gloss + "\" ]\n";
		out += "  Material \"coateddiffuse\" \"texture reflectance\" \"" + p.name + "_colour\" \"texture roughness\" \"" + p.name + "_roughness\"\n";
	} else {
		out += "  Material \"diffuse\" \"texture reflectance\" \"" + p.name + "_colour\"\n";
	}
	char buf[96];
	std::snprintf(buf, sizeof buf, "  Shape \"sphere\" \"float radius\" [ %.4f ]\nAttributeEnd\n", p.radius);
	return out + buf + airText(film, p);
}

}  // namespace planets
}  // namespace cartoon
