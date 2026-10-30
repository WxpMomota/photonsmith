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
//   night      (`cities S`) the lights of cities on its land - along coasts and rivers, in clusters joined by roads - glowing warm on its night side (and
//              drowned by day); (`aurora S`) ovals of aurora round both poles, curtains of light hanging over its night side, green below, red above, drifting;
//   rings      (`rings INNER OUTER [opacity S]`, in its radii) a system of rings in its equator's plane: some 160 ringlets of ice and dust, their widths
//              (and so the gaps between them) set by how dense the rings are there - bright bands, dusty ones, a wide dark division and a narrow gap - so the stars
//              and the planet show through where they are thin; lit by the sun, in the planet's shadow behind it, and their own shadow striped across it;
//   moons      (`moon NAME KIND of PLANET radius R orbit D [period S] [phase DEGREES] [incline DEGREES]`) a body of its own (any kind, and its own air,
//              rings...) going round another on a circle, so moons cast their shadows across their planets and pass into their planets' shadows;
//   eclipses   when a body, seen from the camera, covers the sun: as the last of it goes, the diamond ring (a blaze at the limb where the sun still peeks out), and
//              in totality the sun's corona - its pearly streamers round the black disc;
//   surface    a picture painted once for the kind and its seed (the sphere's own pbrt coordinates: row 0 its north pole), on pbrt's sphere turned so its poles are
//              up (tipped by `tilt`) and spinning once every `spin` seconds; an ocean world's seas glossy (a coated surface whose roughness is a second picture).
//
// Coordinates are pbrt's, as a film file's.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <tuple>
#include <vector>

#include "cartoon_lib.h"
#include "film_cache.h"
#include "film_clouds.h"
#include "film_sky.h"
#include "film_skyfx.h"
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
	double cities = 0.0, aurora = 0.0;   // its night side's city lights and polar aurora (0 none)
	double ringInner = 0.0, ringOuter = 0.0, ringOpacity = 1.0;   // its rings (in its radii; none when the outer is 0)
	std::string around;   // (a moon) the body it goes round; its orbit's radius, its period (seconds of nature's time), where it starts on it, how its plane tips
	double orbit = 0.0, period = 120.0, phase = 0.0, incline = 0.0;
	// (round 11) its weather: a gas giant's bands flowing, each at its own pace (`flow F`, 0 still), more storms in its bands (`storms N`); a cloud layer over a solid
	// world that drifts over its ground and shades it (`clouds C`, its cover 0 .. 1), lightning flickering in its clouds on the night side (`lightning L`)
	double flow = 0.0, clouds = 0.0, lightning = 0.0;
	int storms = 0;
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
		for (int k = 0; k < p.storms; ++k) {   // (`storms N`: white ovals and small brown ones in the bands, each with a collar of darker cloud)
			const double lon = 2.0 * kPi * hash01(s + k, 51), z = 0.75 * (2.0 * hash01(s + k, 52) - 1.0), size = 0.03 + 0.05 * hash01(s + k, 53);
			const double ox = std::atan2(o.y, o.x), d = std::remainder(ox - lon, 2.0 * kPi) * std::sqrt(std::max(0.0, 1.0 - z * z)), dz2 = o.z - z;
			const double r = std::hypot(d / (2.2 * size), dz2 / size);
			const Vec3 tint = hash01(s + k, 54) < 0.7 ? Vec3{0.95, 0.93, 0.88} : Vec3{0.55, 0.33, 0.2};
			c = lerp(c, c * 0.75, smoothstep(0.8, 1.0, r) * (1.0 - smoothstep(1.0, 1.3, r)));   // (its dark collar)
			c = lerp(c, tint * (0.9 + 0.2 * detail::fbm(o, 50.0, s + 60 + k)), 1.0 - smoothstep(0.6, 0.95, r));
		}
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
	const Vec3 q{o.x, o.y, o.z * 2.2};   // (clouds stretched along the latitudes, in swirls; none painted on when it has a cloud layer of its own: `clouds`)
	const double cloud = p.clouds > 0.0 ? 0.0 : smoothstep(0.52, 0.72, detail::fbm(normalize(q), 5.0, s + 13) + 0.25 * (detail::fbm(o, 20.0, s + 14) - 0.5));
	if (cloud > 0.0) sea *= 1.0 - cloud;
	return lerp(c, Vec3{0.92, 0.93, 0.95}, 0.9 * cloud);
}

// How bright the lights of cities are at o (0 .. 1): on land, not under ice, more along coasts, in clusters, the bigger cities joined by faint roads.
inline double cityLightAt(const Planet &p, Vec3 o) {
	double sea = 0.0, glow = 0.0;
	const Vec3 c = surfaceAt(p, o, sea, glow);
	if (sea > 0.0 || p.kind == "gas" || (c.x > 0.8 && c.z > 0.8)) return 0.0;   // (no cities at sea, on a gas giant, or under the ice caps and clouds' whites)
	const int s = p.seed * 97;
	const double land = detail::fbm(o, 2.2, s + 5) + 0.35 * (detail::fbm(o, 9.0, s + 6) - 0.5);
	const double coast = 1.0 - smoothstep(0.53, 0.58, land);   // (just inland of the shore)
	const double cluster = smoothstep(0.6, 0.75, detail::fbm(o, 25.0, s + 21)), speck = smoothstep(0.55, 0.9, detail::fbm(o, 140.0, s + 23));
	const double road = (1.0 - smoothstep(0.0, 0.01, std::abs(detail::fbm(o, 8.0, s + 25) - 0.5))) * 0.25;
	return std::min(1.0, (0.4 + 0.9 * coast) * (cluster * (0.5 + speck) + road * smoothstep(0.4, 0.6, detail::fbm(o, 12.0, s + 27))));
}

// A planet's pictures, painted once for its kind and seed: its colour (always), how glossy it is (an ocean world) and how it glows (lava). Their file paths.
struct Pictures {
	std::string colour, gloss, glow;
};
inline Pictures pictures(const std::string &film, const Planet &p) {
	const int W = 2048, H = 1024;
	Pictures out;
	const std::string base = "planet_v2_" + p.kind + "_" + std::to_string(p.seed) + (p.storms > 0 ? "_s" + std::to_string(p.storms) : std::string()) + (p.clouds > 0.0 && p.kind == "ocean" ? std::string("_clear") : std::string());
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
	if (p.kind == "lava" || p.cities > 0.0)
		out.glow = film_sky::picture(film, base + (p.kind == "lava" ? "_glow" : "_cities"), W, H, [&](std::vector<float> &rgb) {
			for (int j = 0; j < H; ++j)
				for (int i = 0; i < W; ++i) {
					double sea = 0.0, glow = 0.0;
					const Vec3 o = detail::dirOf(i, j, W, H);
					surfaceAt(p, o, sea, glow);
					const Vec3 c = p.kind == "lava" ? Vec3{1.0, 0.35, 0.06} * glow : Vec3{1.0, 0.72, 0.38} * cityLightAt(p, o);
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
				  std::fmod(360.0 * nt / (p.spin < 0.0 ? std::min(-1.0, p.spin) : std::max(1.0, p.spin)), 360.0));   // (a negative spin turns it the other way: max(1, spin) made it a turn a second)
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

// ---- aurora seen from above ----
// The world direction of a planet's north pole (its pole tipped by `tilt` about x, as placement() turns it).
inline Vec3 poleOf(const Planet &p) {
	const double a = p.tilt * kPi / 180.0;
	return {0.0, std::cos(a), std::sin(a)};
}
// A planet's aurora at nature's time `nt` as scene text: an oval round each pole at about 67 degrees of latitude, wavering and drifting, a curtain of light standing from
// 1% to 3.5% of the radius above the ground (skyfx's aurora picture: green below, red above) - drawn only over the night side (towards `sunDir` is day).
inline std::string auroraText(const std::string &film, const Planet &p, double nt, Vec3 sunDir) {
	if (p.aurora <= 0.0) return std::string();
	const Vec3 pole = poleOf(p);
	Vec3 e1 = normalize(cross(pole, Vec3{0.0, 0.0, 1.0}) + Vec3{1e-9, 0.0, 0.0});
	const Vec3 e2 = cross(pole, e1);
	std::string out;
	for (int hemi = 0; hemi < 2; ++hemi) {
		const Vec3 up = hemi == 0 ? pole : pole * -1.0;
		Mesh m;
		const int n = 240;
		std::vector<int> keep(n + 1, 0);
		for (int k = 0; k <= n; ++k) {
			const double a = 2.0 * kPi * k / n;
			const double lat = (67.0 + 3.0 * std::sin(3.0 * a + 0.2 * nt + hemi) + 2.0 * (nature::noise2(4.0 * std::cos(a) + hemi * 7.0, 4.0 * std::sin(a) + 0.05 * nt, p.seed + 61) - 0.5) * 3.0) * kPi / 180.0;
			const Vec3 flat = e1 * std::cos(a) + e2 * std::sin(a), radial = normalize(up * std::sin(lat) + flat * std::cos(lat));
			keep[static_cast<std::size_t>(k)] = dot(radial, sunDir) < -0.05;   // (the night side)
			// four points round the oval: the curtain's foot and its top, and the edges of the glowing band it stands in (2.5 degrees either side, the picture's green
			// fading towards them) - from above, a curtain is a line; the band is what the oval looks like from orbit
			const double spread = 2.5 * kPi / 180.0;
			const Vec3 inner = normalize(up * std::sin(lat - spread) + flat * std::cos(lat - spread)), outer = normalize(up * std::sin(lat + spread) + flat * std::cos(lat + spread));
			for (const auto &[dir, h, v] : {std::tuple<Vec3, double, double>{radial, 0.01, 0.0}, {radial, 0.035, 1.0}, {inner, 0.01, 0.75}, {outer, 0.01, 0.75}}) {
				const Vec3 q = p.at + dir * (p.radius * (1.0 + h));
				m.verts.push_back({-q.x, q.y, q.z});   // (story space: emitMesh mirrors it back)
				m.normals.push_back({-dir.x, dir.y, dir.z});
				m.uvs.push_back({k / double(n), v});
			}
		}
		for (int k = 0; k < n; ++k)
			if (keep[static_cast<std::size_t>(k)] && keep[static_cast<std::size_t>(k) + 1]) {
				const int v = 4 * k, w = v + 4;
				for (const auto &[a, b] : {std::pair<int, int>{0, 1}, {2, 0}, {0, 3}}) {   // (the curtain, and the band's two halves)
					m.tris.push_back({v + a, w + a, v + b});
					m.tris.push_back({v + b, w + a, w + b});
				}
			}
		if (m.tris.empty()) continue;
		char scale[48];
		std::snprintf(scale, sizeof scale, "%.4f", 0.6 * p.aurora);
		out += "# aurora (film_planets.h)\nAttributeBegin\n  AreaLightSource \"diffuse\" \"string filename\" [ \"" + skyfx::auroraPicture(film, "green", hemi) + "\" ] \"float scale\" [ " + scale +
			   " ] \"bool twosided\" [ true ]\n  Material \"dielectric\" \"float eta\" [ 1 ]\n" + emitMesh(m, identity()) + "AttributeEnd\n";
	}
	return out;
}

// ---- rings ----
constexpr int kRinglets = 160;
// How dense a planet's rings are at the share `u` (0 inner edge .. 1 outer) of their width: bands of their own, a wide division at 0.62 and a narrow gap at 0.92, the
// inner edge dusty and faint.
inline double ringDensity(const Planet &p, double u) {
	const double bands = 0.35 + 0.65 * nature::fbm2(u * 9.0, 0.5, p.seed + 71);
	const double division = smoothstep(0.02, 0.035, std::abs(u - 0.62)), gap = smoothstep(0.004, 0.008, std::abs(u - 0.92));
	return bands * division * gap * smoothstep(0.0, 0.18, u) * (1.0 - smoothstep(0.97, 1.0, u));
}
// A planet's rings at nature's time `nt`, as scene text: ringlets (pbrt disks with an inner radius) in its equator's plane, each as wide as the rings are dense there.
inline std::string ringsText(const Planet &p, double nt) {
	if (p.ringOuter <= p.ringInner || p.ringOuter <= 0.0) return std::string();
	std::string out = "# its rings (film_planets.h)\nAttributeBegin\n" + placement(p, nt);
	const double r0 = p.ringInner * p.radius, r1 = p.ringOuter * p.radius, step = (r1 - r0) / kRinglets;
	char buf[260];
	for (int i = 0; i < kRinglets; ++i) {
		const double u = (i + 0.5) / kRinglets, d = std::min(1.0, ringDensity(p, u) * p.ringOpacity);
		if (d < 0.03) continue;
		const double mid = r0 + (i + 0.5) * step, w = step * (0.15 + 0.85 * d);
		const double tint = nature::noise2(u * 40.0, 1.5, p.seed + 73);
		const Vec3 c = lerp(Vec3{0.52, 0.48, 0.42}, Vec3{0.82, 0.75, 0.62}, 0.4 * tint + 0.6 * d);
		std::snprintf(buf, sizeof buf, "  Material \"diffuse\" \"rgb reflectance\" [ %.4f %.4f %.4f ]\n  Shape \"disk\" \"float radius\" [ %.4f ] \"float innerradius\" [ %.4f ]\n", c.x, c.y, c.z,
					  mid + 0.5 * w, mid - 0.5 * w);
		out += buf;
	}
	return out + "AttributeEnd\n";
}

// ---- moons and eclipses ----
// Where body i of `all` is at nature's time `nt`: its own place, or (a moon) on its orbit round the body it goes round, wherever that is.
inline Vec3 centreAt(const std::vector<Planet> &all, std::size_t i, double nt, int depth = 0) {
	const Planet &p = all[i];
	if (p.around.empty() || depth > 8) return p.at;
	for (std::size_t k = 0; k < all.size(); ++k)
		if (all[k].name == p.around) {
			const double a = (p.phase + 360.0 * nt / (p.period == 0.0 ? 1.0 : p.period)) * kPi / 180.0, inc = p.incline * kPi / 180.0;
			return centreAt(all, k, nt, depth + 1) + Vec3{std::cos(a), std::sin(a) * std::sin(inc), std::sin(a) * std::cos(inc)} * p.orbit;
		}
	return p.at;
}
// How much of the sun (toward `sunDir`, `sunRadius` radians across from its middle) body (centre `c`, radius `r`) covers seen from `eye`: 0 .. 1 (the overlap of the
// two discs), and (out) which way across the sky the body's middle lies from the sun's.
inline double sunCovered(Vec3 eye, Vec3 c, double r, Vec3 sunDir, double sunRadius, Vec3 &away) {
	const Vec3 v = c - eye;
	const double dist = length(v);
	if (dist <= r) return 0.0;
	const Vec3 dir = v * (1.0 / dist);
	const double rho = std::asin(std::min(1.0, r / dist)), theta = std::acos(std::clamp(dot(dir, sunDir), -1.0, 1.0)), s = sunRadius;
	away = normalize(sunDir - dir + Vec3{1e-12, 0.0, 0.0});   // (across the sky, from the body's middle towards the sun's)
	if (theta >= rho + s) return 0.0;
	if (theta <= rho - s) return 1.0;
	if (theta <= s - rho) return (rho * rho) / (s * s);   // (a small body inside the sun's disc: an annulus or a transit)
	// the overlap of two circles (radii s and rho, centres theta apart), as a share of the sun's
	const double d = theta, a1 = std::acos(std::clamp((d * d + s * s - rho * rho) / (2.0 * d * s), -1.0, 1.0)), a2 = std::acos(std::clamp((d * d + rho * rho - s * s) / (2.0 * d * rho), -1.0, 1.0));
	const double area = s * s * a1 + rho * rho * a2 - 0.5 * std::sqrt(std::max(0.0, (-d + s + rho) * (d + s - rho) * (d - s + rho) * (d + s + rho)));
	return std::clamp(area / (kPi * s * s), 0.0, 1.0);
}
// The corona's picture: pearly streamers round a dark middle (a square, the sun's disc a sixth of it across).
inline std::string coronaPicture(const std::string &film) {
	const int N = 1024;
	return film_sky::picture(film, "corona_v2", N, N, [&](std::vector<float> &rgb) {
		for (int j = 0; j < N; ++j)
			for (int i = 0; i < N; ++i) {
				const double x = (i + 0.5) / N * 2.0 - 1.0, y = (j + 0.5) / N * 2.0 - 1.0, r = std::hypot(x, y) * 6.0, a = std::atan2(y, x);   // (r in sun radii)
				double v = 0.0;
				if (r > 1.0) {
					const double streamers = 0.4 + 0.6 * std::pow(nature::fbm2(std::cos(a) * 3.0 + 5.0, std::sin(a) * 3.0, 91), 2.0) * (1.0 + 0.8 * std::pow(std::abs(std::cos(a)), 3.0));
					v = std::pow(r, -2.5) * streamers * (1.0 - smoothstep(4.5, 6.0, r)) * smoothstep(1.0, 1.06, r);
				}
				const std::size_t q = (static_cast<std::size_t>(j) * N + i) * 3;
				rgb[q] = static_cast<float>(v * 0.95), rgb[q + 1] = static_cast<float>(v * 0.97), rgb[q + 2] = static_cast<float>(v * 1.05);
			}
	});
}
// An eclipse seen from `eye` at nature's time `nt`, as scene text: the corona (a glowing card facing the camera, far beyond the body that covers the sun) in totality,
// and the diamond ring (a small blazing ball at the limb) as the last of the sun goes or the first returns. "" when nothing covers the sun.
inline std::string eclipseText(const std::string &film, const std::vector<Planet> &all, double nt, Vec3 eye, Vec3 sunDir, double sunSizeDegrees) {
	const double s = 0.5 * sunSizeDegrees * kPi / 180.0;
	double covered = 0.0, nearest = 1e30;
	Vec3 away{0.0, 1.0, 0.0};
	for (std::size_t i = 0; i < all.size(); ++i) {
		Vec3 a;
		const Vec3 c = centreAt(all, i, nt);
		const double f = sunCovered(eye, c, all[i].radius, sunDir, s, a);
		if (f > covered) covered = f, away = a, nearest = length(c - eye);
	}
	if (covered < 0.97) return std::string();
	std::string out;
	const double D = std::max(std::min(4.0e5, 20.0 * nearest), 2.0 * nearest);   // (far beyond the body that covers the sun - always beyond it: a body 450 km off had the card in front)
	Vec3 e1 = normalize(cross(sunDir, Vec3{0.0, 1.0, 0.0}) + Vec3{1e-9, 0.0, 0.0});
	const Vec3 e2 = cross(e1, sunDir), mid = eye + sunDir * D;
	const double corona = smoothstep(0.985, 0.9995, covered), half = D * std::tan(6.0 * s);
	if (corona > 0.0) {   // the corona: a card six sun-radii across each way, facing the camera
		Mesh m;
		for (const auto &[u, v] : {std::pair<double, double>{0, 0}, {1, 0}, {1, 1}, {0, 1}}) {
			const Vec3 q = mid + e1 * ((2.0 * u - 1.0) * half) + e2 * ((2.0 * v - 1.0) * half);
			m.verts.push_back({-q.x, q.y, q.z}), m.normals.push_back({sunDir.x, -sunDir.y, -sunDir.z}), m.uvs.push_back({u, v});
		}
		m.tris = {{0, 1, 2}, {0, 2, 3}};
		char scale[48];
		std::snprintf(scale, sizeof scale, "%.4f", 1.5 * corona);
		out += "# the sun's corona (film_planets.h)\nAttributeBegin\n  AreaLightSource \"diffuse\" \"string filename\" [ \"" + coronaPicture(film) + "\" ] \"float scale\" [ " + scale +
			   " ] \"bool twosided\" [ true ]\n  Material \"dielectric\" \"float eta\" [ 1 ]\n" + emitMesh(m, identity()) + "AttributeEnd\n";
	}
	// (the diamond ring: as the last sliver goes - and gone once it has: the curve alone was still a sixth of its brightness at totality)
	const double ring = std::exp(-std::pow((covered - 0.996) / 0.003, 2.0)) * std::clamp((1.0 - covered) / 0.002, 0.0, 1.0);
	if (ring > 0.02) {
		const Vec3 limb = normalize(sunDir * std::cos(0.97 * s) + normalize(away - sunDir * dot(away, sunDir) + Vec3{1e-9, 0, 0}) * std::sin(0.97 * s));
		const Vec3 q = eye + limb * D;
		out += "# the diamond ring (film_planets.h)\n" + emissiveSphere({-q.x, q.y, q.z}, D * std::tan(0.12 * s), Vec3{1.0, 0.98, 0.94} * (3000.0 * ring));
	}
	return out;
}

// A planet at nature's time `nt`, as scene text.
// ---- weather (round 11) ----
// A sphere of radius r as a mesh in its own frame (z its pole, as pbrt's sphere), its uvs as pbrt's sphere's (u round from +x, v 1 at the north pole) so a planet's picture
// lies on it as on the sphere; only the band of latitudes from `z0` to `z1` (the pole's -1 .. 1).
inline Mesh sphereZone(double r, double z0, double z1, int segments, int rows) {
	Mesh m;
	const double t0 = std::acos(std::clamp(z1, -1.0, 1.0)), t1 = std::acos(std::clamp(z0, -1.0, 1.0));
	for (int j = 0; j <= rows; ++j) {
		const double theta = t0 + (t1 - t0) * j / rows;
		for (int i = 0; i <= segments; ++i) {
			const double phi = 2.0 * kPi * i / segments;
			const Vec3 n{std::sin(theta) * std::cos(phi), std::sin(theta) * std::sin(phi), std::cos(theta)};
			m.verts.push_back(n * r), m.normals.push_back(n), m.uvs.push_back({static_cast<double>(i) / segments, 1.0 - theta / kPi});
		}
	}
	const int w = segments + 1;
	for (int j = 0; j < rows; ++j)
		for (int i = 0; i < segments; ++i) {
			const int a = j * w + i;
			m.tris.push_back({a, a + w, a + 1}), m.tris.push_back({a + 1, a + w, a + w + 1});
		}
	return m;
}
// How far a gas giant's band at latitude z (-1 .. 1) has drifted round from the rest of it by nature's time nt (degrees): jets alternating east and west, the fastest at
// the equator, by `flow`.
inline double bandDrift(const Planet &p, double z, double nt) {
	const double lat = std::asin(std::clamp(z, -1.0, 1.0));
	const double pace = 0.06 * std::cos(7.0 * lat) * (1.0 - 0.6 * std::abs(z)) + 0.04 * std::exp(-std::pow(lat / 0.15, 2.0));   // (a share of its spin)
	return std::fmod(360.0 * p.flow * pace * nt / std::max(1.0, std::abs(p.spin)), 360.0);
}
// The cloud layer's picture (white where there is cloud: its alpha), painted once.
inline std::string cloudPicture(const std::string &film, const Planet &p) {
	const int W = 2048, H = 1024;
	char name[96];
	std::snprintf(name, sizeof name, "planet_v2_clouds_%d_%.2f", p.seed, p.clouds);
	return film_sky::picture(film, name, W, H, [&](std::vector<float> &rgb) {
		const int s = p.seed * 97;
		for (int j = 0; j < H; ++j)
			for (int i = 0; i < W; ++i) {
				const Vec3 o = detail::dirOf(i, j, W, H), q{o.x, o.y, o.z * 2.2};   // (stretched along the latitudes, in swirls)
				const double n = detail::fbm(normalize(q), 5.0, s + 13) + 0.25 * (detail::fbm(o, 20.0, s + 14) - 0.5) + 0.12 * (detail::fbm(o, 60.0, s + 15) - 0.5);
				const float c = static_cast<float>(smoothstep(0.72 - 0.3 * p.clouds, 0.78 - 0.3 * p.clouds, n));
				const std::size_t k = (static_cast<std::size_t>(j) * W + i) * 3;
				rgb[k] = rgb[k + 1] = rgb[k + 2] = c;
			}
	});
}
// Its weather as scene text: the flowing bands of a gas giant (its sphere drawn in 24 zones of latitude, each turned by its band's drift), or its cloud layer (a shell 1.2%
// above its ground, turning a little faster than the ground, shading it), and lightning on its night side.
inline std::string weatherText(const std::string &film, const Planet &p, const std::string &colourPicture, double nt, Vec3 sunDir) {
	std::string out;
	if (p.kind == "gas" && p.flow > 0.0) {
		out += "# its bands flowing (film_planets.h)\nAttributeBegin\n" + placement(p, nt) + "  Texture \"" + p.name + "_bands\" \"spectrum\" \"imagemap\" \"string filename\" [ \"" + colourPicture +
			   "\" ]\n  Material \"diffuse\" \"texture reflectance\" \"" + p.name + "_bands\"\n";
		const int zones = 24;
		for (int k = 0; k < zones; ++k) {
			const double z0 = -1.0 + 2.0 * k / zones, z1 = -1.0 + 2.0 * (k + 1) / zones;
			char buf[96];
			std::snprintf(buf, sizeof buf, "  AttributeBegin\n  Rotate %.4f 0 0 1\n", bandDrift(p, 0.5 * (z0 + z1), nt));
			out += buf;
			sphereZone(p.radius, z0, z1, 128, 6).appendPbrt(out, identity(), false);
			out += "  AttributeEnd\n";
		}
		out += "AttributeEnd\n";
	}
	if (p.clouds > 0.0 && p.kind != "gas") {
		Planet shell = p;
		shell.spin = p.spin / 1.15;   // (the clouds go round a little faster than the ground under them)
		const std::string pic = cloudPicture(film, p);
		out += "# its clouds (film_planets.h)\nAttributeBegin\n" + placement(shell, nt) + "  Texture \"" + p.name + "_cloudcover\" \"float\" \"imagemap\" \"string filename\" [ \"" + pic +
			   "\" ]\n  Material \"diffuse\" \"rgb reflectance\" [ 0.9 0.9 0.92 ]\n";
		std::string mesh;
		sphereZone(p.radius * 1.012, -1.0, 1.0, 256, 128).appendPbrt(mesh, identity(), false);
		const std::string shape = "Shape \"trianglemesh\"";
		const std::size_t at = mesh.find(shape);
		if (at != std::string::npos) mesh.insert(at + shape.size(), " \"texture alpha\" \"" + p.name + "_cloudcover\"");
		out += mesh + "AttributeEnd\n";
	}
	if (p.lightning > 0.0) {   // flashes in the clouds on its night side: a few at a time, each a fifth of a second, somewhere new every half second
		const int slot = static_cast<int>(std::floor(nt * 2.0));
		const Vec3 pole = poleOf(p), side = normalize(cross(pole, std::fabs(pole.y) < 0.9 ? Vec3{0.0, 1.0, 0.0} : Vec3{1.0, 0.0, 0.0})), other = cross(pole, side);
		for (int k = 0; k < static_cast<int>(3 * std::min(4.0, p.lightning)); ++k) {
			const int h = p.seed * 7919 + slot * 31 + k;
			const double when = hash01(h, 1) * 0.5, now = nt - slot * 0.5;
			if (now < when || now > when + 0.2) continue;
			const double lat = std::asin(2.0 * hash01(h, 2) - 1.0) * 0.6, lon = 2.0 * kPi * hash01(h, 3);
			const Vec3 d = normalize(pole * std::sin(lat) + (side * std::cos(lon) + other * std::sin(lon)) * std::cos(lat));
			if (dot(d, sunDir) > -0.1) continue;   // (only where it is night)
			const Vec3 c = p.at + d * (p.radius * 1.008);
			const double flicker = (0.5 + 0.5 * std::sin((now - when) * 90.0 + k)) * p.lightning;
			out += emissiveSphere({-c.x, c.y, c.z}, p.radius * 0.012, Vec3{0.75, 0.82, 1.0} * (40.0 * flicker));
		}
	}
	return out;
}

inline std::string planetText(const std::string &film, const Planet &p, double nt, Vec3 sunDir = Vec3{0.0, 1.0, 0.0}) {
	const Pictures pic = pictures(film, p);
	std::string out = "# a planet (film_planets.h): " + p.name + "\nAttributeBegin\n" + placement(p, nt);
	out += "  Texture \"" + p.name + "_colour\" \"spectrum\" \"imagemap\" \"string filename\" [ \"" + pic.colour + "\" ]\n";
	if (!pic.glow.empty()) {
		char scale[32];
		std::snprintf(scale, sizeof scale, "%.4f", p.kind == "lava" ? 1.5 : 0.25 * p.cities);
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
	if (p.kind == "gas" && p.flow > 0.0) out = "# a planet (film_planets.h): " + p.name + "\n";   // (its sphere drawn by its flowing bands instead)
	else out += buf;
	return out + weatherText(film, p, pic.colour, nt, sunDir) + airText(film, p) + auroraText(film, p, nt, sunDir) + ringsText(p, nt);
}

}  // namespace planets
}  // namespace cartoon
