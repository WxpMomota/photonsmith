#pragma once
// nomai_world.h -- the world of "The Last Nomai Signal" (nomai_signal.h): a small, ancient moon of rock and sand, a gas giant that fills a third of its sky, a small
// neighbouring moon, the stars and the sun.
//
// The moon is small, as the planets of Outer Wilds are: 2.4 km in radius, so its horizon is close and curves away (from a standing eye it is about a hundred metres off),
// and the camera can pull back from a campfire to the whole moon in one move. Its centre is at (0, -kMoonR, 0), so the landing site is at the origin on top of it. The
// ground near the site is described in arc coordinates (x, z): the distance along the surface from the site towards story +x and +z, the way a map of the place would be
// drawn. Towards -z lies a low ridge, and beyond it a canyon cut into banded rock (pale sandstone, rust, cream and slate, deeper layers further down, as the strata of
// Ember Twin shift with depth) where the ruins are (nomai_ruins.h).
//
// The light is physical: the sun is a far glowing ball, so night falls wherever the moon hides it; the gas giant is a glowing sphere whose face is painted with its bands as
// the sun lights them (its terminator baked into the picture for the sun's place), and at night a cool fill from its side of the sky (giantshineText) lights the ground. The neighbouring moon is lit by the sun like the ground.
//
// Story space as in every film: +x to the right of the picture, +y up, +z towards the camera, metres.

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

#include "film_cache.h"
#include "film_sky.h"
#include "nature_lib.h"
#include "nomai_mesh.h"
#include "sdf_mesh.h"

namespace cartoon {
namespace nomai {

constexpr double kMoonR = 2400.0;
inline Vec3 moonCentre() { return {0.0, -kMoonR, 0.0}; }

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The sky's bodies
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
constexpr double kGiantDist = 60000.0, kGiantR = 17500.0, kGiantTilt = 14.0;   // (it spans 34 degrees of the sky from the site)
inline Vec3 giantDir() { return normalize({0.22, 0.50, -0.84}); }
inline Vec3 giantCentre() { return moonCentre() + giantDir() * kGiantDist; }
constexpr double kNeighbourDist = 140000.0, kNeighbourR = 2600.0;
inline Vec3 neighbourDir() { return normalize({-0.60, 0.30, -0.74}); }

// Where the sun is, as seen from the moon: 0 the night of the film (the sun below the site's horizon, behind and to the right, so the giant shows a wide gibbous face), 1 its
// sunrise (just over the horizon front right, the giant a thick crescent beside it). In between, the moon's turn.
inline Vec3 sunDir(double dawn) {
	const Vec3 night = normalize({0.45, -0.25, 0.86}), rise = normalize({0.75, 0.045, -0.66});
	const double u = clamp01(dawn);
	const double c = std::max(-1.0, std::min(1.0, dot(night, rise))), a = std::acos(c);
	return normalize(night * (std::sin((1.0 - u) * a) / std::sin(a)) + rise * (std::sin(u * a) / std::sin(a)));
}
constexpr double kSunDist = 1.0e6;
// (the game's sun: a big gold-orange ball, its light warm)
inline Vec3 sunColour(double dawn) { return lerp(Vec3{1.0, 0.80, 0.56}, Vec3{1.0, 0.60, 0.30}, smoothstep(0.6, 1.0, dawn)); }
// The sun: a glowing ball, so the moon's own body shades the night side. Its irradiance is 3 scene units (the films' sun) times `strength`.
inline std::string sunText(double dawn, double strength = 1.0) {
	const double radius = kSunDist * std::tan(1.6 * kPi / 180.0);   // (big in the sky, as the game draws it)
	const double L = 3.0 * strength / (kPi * std::pow(radius / kSunDist, 2.0));
	return emissiveSphere(moonCentre() + sunDir(dawn) * kSunDist, radius, sunColour(dawn) * L);
}

// ---- noise on the sphere --------------------------------------------------------------------------------------------------------------------------------------------
inline double noise3(Vec3 p, int salt) {
	const double fx = std::floor(p.x), fy = std::floor(p.y), fz = std::floor(p.z);
	const int ix = static_cast<int>(fx), iy = static_cast<int>(fy), iz = static_cast<int>(fz);
	const auto at = [salt](int a, int b, int c) { return hash01(a * 73856093 ^ b * 19349663 ^ c * 83492791, salt); };
	const auto s = [](double u) { return u * u * (3.0 - 2.0 * u); };
	const double u = s(p.x - fx), v = s(p.y - fy), w = s(p.z - fz);
	const double a = lerp(lerp(at(ix, iy, iz), at(ix + 1, iy, iz), u), lerp(at(ix, iy + 1, iz), at(ix + 1, iy + 1, iz), u), v);
	const double b = lerp(lerp(at(ix, iy, iz + 1), at(ix + 1, iy, iz + 1), u), lerp(at(ix, iy + 1, iz + 1), at(ix + 1, iy + 1, iz + 1), u), v);
	return lerp(a, b, w);
}
inline double fbm3(Vec3 p, int salt, int octaves = 5) {
	double sum = 0.0, amp = 0.5, total = 0.0;
	for (int o = 0; o < octaves; ++o) {
		sum += amp * noise3(p, salt + o * 131);
		total += amp;
		p = p * 2.03 + Vec3{1.7, 9.2, 3.1};
		amp *= 0.5;
	}
	return sum / total;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The gas giant: in the greens of Giant's Deep (deep green, teal-green, mint-white cloud bands), torn into eddies, with one great pale storm; lit by the sun (its face painted for the sun's place)
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// pbrt's sphere: u = phi / 2 pi about its z axis, v from its -z pole; an area light's image is looked up at (u, 1 - v), so row 0 is the +z pole. The sphere is turned so that
// its pole is story +y, tipped by kGiantTilt about the line of sight.
inline Vec3 giantObjectToStory(Vec3 o) {
	// pbrt world = Rz(tilt) * Rx(-90) * o, then story = pbrt with x negated
	const Vec3 rx{o.x, o.z, -o.y};
	const double a = kGiantTilt * kPi / 180.0;
	const Vec3 w{rx.x * std::cos(a) - rx.y * std::sin(a), rx.x * std::sin(a) + rx.y * std::cos(a), rx.z};
	return {-w.x, w.y, w.z};
}
// The giant's albedo at a point of its surface (o: the unit direction in its own frame, z its pole).
inline Vec3 giantAlbedo(Vec3 o) {
	const double lat = std::asin(std::max(-1.0, std::min(1.0, o.z)));
	const double lon = std::atan2(o.y, o.x);
	// the bands, bent by eddies that grow towards the equator
	const double warp = 0.16 * (fbm3(o * 3.0, 11) - 0.5) + 0.05 * (fbm3(o * 9.0, 12) - 0.5) + 0.018 * std::sin(lon * 7.0 + lat * 30.0);
	const double b = lat + warp;
	static const Vec3 palette[] = {{0.07, 0.28, 0.20}, {0.30, 0.60, 0.46}, {0.74, 0.86, 0.72}, {0.12, 0.40, 0.30}, {0.05, 0.18, 0.15}, {0.50, 0.74, 0.58}, {0.18, 0.50, 0.40}};
	const double k = 0.5 + 0.5 * std::sin(b * 11.0) + 0.35 * std::sin(b * 27.0 + 1.3);
	const double pos = (b * 2.3 + 3.0) * 1.7 + 0.4 * k;
	const int n = 7;
	const double f = pos - std::floor(pos / n) * n;
	const int i0 = static_cast<int>(f) % n, i1 = (i0 + 1) % n;
	Vec3 c = lerp(palette[i0], palette[i1], smoothstep(0.25, 0.75, f - std::floor(f)));
	// fine streaks
	c = c * (0.86 + 0.28 * fbm3(Vec3{o.x * 40.0, o.y * 40.0, lat * 4.0}, 13));
	// the great storm: a pale oval spiral south of the equator
	const double sLat = -0.36, sLon = 0.9;
	const double dLon = std::remainder(lon - sLon, 2.0 * kPi), dLat = lat - sLat;
	const double r = std::sqrt(std::pow(dLon / 0.30, 2.0) + std::pow(dLat / 0.13, 2.0));
	if (r < 1.6) {
		const double ang = std::atan2(dLat / 0.13, dLon / 0.30) + 3.0 * r;
		const double swirl = 0.5 + 0.5 * std::sin(ang * 3.0 + 4.0 * fbm3(o * 12.0, 14));
		const double in = 1.0 - smoothstep(0.7, 1.6, r);
		c = lerp(c, lerp(Vec3{0.56, 0.80, 0.62}, Vec3{0.88, 0.95, 0.84}, swirl), in * 0.85);
	}
	// the poles darker and bluer
	c = lerp(c, Vec3{0.05, 0.16, 0.14}, smoothstep(0.95, 1.35, std::abs(lat)) * 0.8);
	return c;
}
// The giant's face as its light (radiance), for the sun at `dawn`: the lit side's albedo times the sun's light, a soft terminator, a brighter thin atmosphere at the lit limb,
// and the night side almost black.
inline std::string giantPicture(double dawn) {
	const int key = static_cast<int>(std::lround(clamp01(dawn) * 20.0));   // (the sun's place to a twentieth of the way)
	const Vec3 sun = sunDir(key / 20.0);
	const Vec3 tint = sunColour(key / 20.0);
	return film_sky::picture(kFilmKey, "giant_" + std::to_string(key), 4096, 2048, [&](std::vector<float> &rgb) {
		const int W = 4096, H = 2048;
		for (int row = 0; row < H; ++row) {
			const double theta = kPi * (row + 0.5) / H;
			for (int col = 0; col < W; ++col) {
				const double phi = 2.0 * kPi * (col + 0.5) / W;
				const Vec3 o{std::sin(theta) * std::cos(phi), std::sin(theta) * std::sin(phi), std::cos(theta)};
				const Vec3 n = giantObjectToStory(o);
				const double d = dot(n, sun);
				const double lit = smoothstep(-0.06, 0.18, d) * (0.18 + 0.82 * std::max(0.0, d));
				const Vec3 a = giantAlbedo(o);
				// (3 units of sun on an albedo, over pi) and a cool glow where the light grazes the atmosphere
				Vec3 L = Vec3{a.x * tint.x, a.y * tint.y, a.z * tint.z} * (3.0 / kPi * lit);
				L = L + Vec3{0.36, 0.70, 0.52} * (0.10 * std::exp(-std::pow(d / 0.10, 2.0)));
				L = L + a * 0.0015;   // (the night side: just visible against the stars)
				const std::size_t i = (static_cast<std::size_t>(row) * W + col) * 3;
				rgb[i] = static_cast<float>(L.x);
				rgb[i + 1] = static_cast<float>(L.y);
				rgb[i + 2] = static_cast<float>(L.z);
			}
		}
	});
}
inline std::string giantText(double dawn, double brightness = 1.0) {
	const Vec3 g = giantCentre();
	char buf[480];
	std::snprintf(buf, sizeof buf,
				  "AttributeBegin\n  AreaLightSource \"diffuse\" \"string filename\" [ \"%s\" ] \"float scale\" [ %.5f ]\n  Material \"diffuse\" \"rgb reflectance\" [ 0 0 0 ]\n"
				  "  Translate %.3f %.3f %.3f\n  Rotate %.4f 0 0 1\n  Rotate -90 1 0 0\n  Shape \"sphere\" \"float radius\" [ %.3f ]\nAttributeEnd\n",
				  giantPicture(dawn).c_str(), brightness, -g.x, g.y, g.z, kGiantTilt, kGiantR);
	return buf;
}
// The giant's light on the ground at night, as the film wants it: cool blue-cyan moonlight from the giant's side of the sky (four distant lights spread over its disc, so the
// shadows are soft-edged). The giant's own glow is kept low enough that its bands show in a night exposure; this fill is what lights the land - the brief's "strong cool
// moonlight", and a night on an Outer Wilds planet.
inline std::string giantshineText(double strength) {
	if (strength <= 0.0) return std::string();
	std::string out;
	const Vec3 d = giantDir();
	const Vec3 side = normalize(cross(d, Vec3{0.0, 1.0, 0.0})), upv = normalize(cross(side, d));
	const double spread = 0.42;   // (just outside the giant's disc: its own sphere would shade a light from within it)
	for (int k = 0; k < 4; ++k) {
		const double a = kPi / 4.0 + k * kPi / 2.0;
		const Vec3 dir = normalize(d + side * (spread * std::cos(a)) + upv * (spread * std::sin(a)));
		const Vec3 L = Vec3{0.40, 0.64, 1.0} * (0.20 * strength);
		char buf[260];
		std::snprintf(buf, sizeof buf, "LightSource \"distant\" \"point3 from\" [ %.5f %.5f %.5f ] \"point3 to\" [ 0 0 0 ] \"rgb L\" [ %.5f %.5f %.5f ]\n", -dir.x, dir.y, dir.z, L.x, L.y, L.z);
		out += buf;
	}
	return out;
}
// The neighbouring moon: a small grey ball lit by the sun.
inline std::string neighbourText() {
	const Vec3 c = moonCentre() + neighbourDir() * kNeighbourDist;
	char buf[200];
	std::snprintf(buf, sizeof buf, "  Translate %.3f %.3f %.3f\n  Shape \"sphere\" \"float radius\" [ %.3f ]\n", -c.x, c.y, c.z, kNeighbourR);
	return attr(diffuseMaterial({0.46, 0.45, 0.44}), buf);   // (grey, like Attlerock)
}
// The stars: a black sky with the Milky Way and a field of faint stars (film_sky.h), no air.
inline std::string starsText(double brightness = 1.0) {
	film_sky::Spec s;
	s.name = "ns_stars";
	s.night = 1.0;
	s.nightBrightness = 0.35;
	s.stars = 1.2;
	s.milkyWay = 1.1;
	s.milkyPole = {0.55, 0.62, 0.55};
	s.sunElevation = -30.0;
	return film_sky::light(kFilmKey, s, brightness);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The ground
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The canyon: its middle line (z) along x, its half width at the top, its depth.
inline double canyonZ(double x) { return -175.0 + 22.0 * std::sin(x / 95.0 + 0.6); }
inline double canyonHalf(double x) { return 34.0 + 5.0 * std::sin(x / 41.0); }
constexpr double kCanyonDepth = 50.0;
inline double canyonTaper(double x) { return 1.0 - smoothstep(330.0, 430.0, std::abs(x)); }
// How deep into the canyon a point is: 0 on the rim and beyond, 1 on the floor; the walls come down in three steps (terraces), worn by noise.
inline double canyonDepthFraction(double x, double z) {
	const double a = std::abs(z - canyonZ(x)) / canyonHalf(x);
	if (a >= 1.0) return 0.0;
	const double wear = 0.10 * (nature::fbm2(x / 9.0, z / 9.0, 41) - 0.5);
	const double u = smoothstep(0.30, 1.0, a + wear);
	const double s = u * 3.0, k = std::floor(s), f = s - k;
	const double stair = (k + smoothstep(0.50, 0.95, f)) / 3.0;
	return (1.0 - std::min(1.0, stair)) * canyonTaper(x);
}
inline double ridgeZ(double x) { return -78.0 + 12.0 * std::sin(x / 70.0 + 1.1); }
struct Crater {
	double x, z, r, depth;
};
inline const std::vector<Crater> &craters() {
	static const std::vector<Crater> c = {{95.0, 62.0, 28.0, 6.0}, {-150.0, 40.0, 46.0, 8.0}, {215.0, -40.0, 36.0, 7.0}, {-62.0, 122.0, 18.0, 3.0}, {170.0, 190.0, 62.0, 10.0},
										   {-260.0, -40.0, 42.0, 7.0}, {330.0, 90.0, 80.0, 12.0}, {-380.0, 200.0, 70.0, 11.0}, {40.0, 360.0, 90.0, 14.0}};
	return c;
}
// The moon's large shapes, everywhere: rolling highlands and old craters.
inline double globalHeight(Vec3 d) {
	double h = 26.0 * (fbm3(d * 4.0 + Vec3{3.0, 1.0, 7.0}, 21) - 0.5) + 9.0 * (fbm3(d * 13.0, 22) - 0.5);
	static const std::vector<std::pair<Vec3, double>> big = {{normalize({0.7, 0.2, 0.6}), 0.22}, {normalize({-0.5, -0.4, 0.7}), 0.30}, {normalize({0.1, -0.9, -0.3}), 0.35},
															 {normalize({-0.8, 0.3, -0.5}), 0.18}, {normalize({0.4, -0.3, -0.85}), 0.25}};
	for (const auto &[c, rad] : big) {
		const double a = std::acos(std::max(-1.0, std::min(1.0, dot(d, c)))) / rad;
		if (a < 1.6) h += -40.0 * (1.0 - a * a) * (a < 1.0 ? 1.0 : 0.0) + 14.0 * std::exp(-std::pow((a - 1.0) / 0.2, 2.0));
	}
	return h;
}
inline Vec3 arcDirection(double x, double z) {
	const double r = std::sqrt(x * x + z * z);
	if (r < 1e-9) return {0.0, 1.0, 0.0};
	const double th = r / kMoonR;
	return {std::sin(th) * x / r, std::cos(th), std::sin(th) * z / r};
}
// The height of the ground above the moon's radius at arc coordinates (x, z), with the site at 0.
inline double localHeight(double x, double z) {
	double h = 2.2 * (nature::fbm2(x / 60.0, z / 60.0, 31) - 0.5) + 6.0 * (nature::fbm2(x / 230.0, z / 230.0, 32) - 0.5) + 0.35 * (nature::fbm2(x / 6.0, z / 6.0, 33) - 0.5);
	// dunes of fine sand on the plain: long low waves across the wind
	h += 0.45 * std::sin((x * 0.8 + z * 0.6) / 7.0 + 3.0 * nature::fbm2(x / 40.0, z / 40.0, 34)) * nature::fbm2(x / 90.0, z / 90.0, 35);
	for (const Crater &c : craters()) {
		const double d = std::sqrt((x - c.x) * (x - c.x) + (z - c.z) * (z - c.z)) / c.r;
		if (d < 2.0) h += (d < 1.0 ? -c.depth * (1.0 - d * d) : 0.0) + c.depth * 0.38 * std::exp(-std::pow((d - 1.0) / 0.2, 2.0));
	}
	// the ridge between the landing site and the canyon
	h += 9.0 * std::exp(-std::pow((z - ridgeZ(x)) / 15.0, 2.0)) * (0.75 + 0.5 * nature::fbm2(x / 30.0, z / 30.0, 36)) * (1.0 - smoothstep(380.0, 520.0, std::abs(x)));
	// the canyon (its floor a little sand and rubble)
	const double cf = canyonDepthFraction(x, z);
	if (cf > 0.0) h -= kCanyonDepth * cf - (cf > 0.98 ? 0.6 * (nature::fbm2(x / 8.0, z / 8.0, 37) - 0.3) : 0.0);
	// the landing site: a flat stretch
	const double ds = std::sqrt(x * x + z * z);
	h = lerp(h, 0.2 * (nature::fbm2(x / 6.0, z / 6.0, 33) - 0.5), 1.0 - smoothstep(14.0, 32.0, ds));
	return h;
}
inline double heightAt(double x, double z) {
	static const double site = globalHeight({0.0, 1.0, 0.0});
	const double r = std::sqrt(x * x + z * z);
	const double local = r < 1500.0 ? localHeight(x, z) * (1.0 - smoothstep(900.0, 1500.0, r)) : 0.0;
	return globalHeight(arcDirection(x, z)) - site + local;
}
// The point of the ground at (x, z), and the local up there.
inline Vec3 groundPoint(double x, double z, double lift = 0.0) { return moonCentre() + arcDirection(x, z) * (kMoonR + heightAt(x, z) + lift); }
inline Vec3 upAt(double x, double z) { return arcDirection(x, z); }
// A frame standing on the ground at (x, z): its origin there, y the local up, turned `yawDeg` about it (0: its z along story +z).
inline Mat4 groundFrame(double x, double z, double yawDeg = 0.0, double lift = 0.0) {
	const Vec3 up = upAt(x, z);
	const double a = yawDeg * kPi / 180.0;
	const Vec3 hint{std::sin(a), 0.0, std::cos(a)};
	return alignY(groundPoint(x, z, lift), up, hint * -1.0);   // (alignY's z is the opposite of its hint)
}

// The colour of the ground: sand on the flats, grey rock where it is steep, and in the canyon its strata (bands that change with depth), with a darker crust of rubble on
// the floor.
inline Vec3 strata(double depth, double x, double z) {
	static const Vec3 bands[] = {{0.80, 0.54, 0.32}, {0.58, 0.30, 0.19}, {0.86, 0.72, 0.52}, {0.70, 0.42, 0.26}, {0.46, 0.34, 0.36}, {0.84, 0.60, 0.36}, {0.52, 0.33, 0.24}, {0.88, 0.78, 0.60}};
	const double w = depth + 1.6 * (nature::fbm2(x / 14.0, z / 14.0, 51) - 0.5) + 0.4 * std::sin(x / 5.0);
	const double p = w / 3.4;
	const int n = 8;
	const double f = p - std::floor(p / n) * n;
	const int i0 = static_cast<int>(f) % n, i1 = (i0 + 1) % n;
	return lerp(bands[i0], bands[i1], smoothstep(0.7, 1.0, f - std::floor(f)));
}
inline Vec3 groundColour(double x, double z) {
	const double e = 0.6;
	const double h = heightAt(x, z);
	const double gx = (heightAt(x + e, z) - heightAt(x - e, z)) / (2.0 * e), gz = (heightAt(x, z + e) - heightAt(x, z - e)) / (2.0 * e);
	const double slope = std::sqrt(gx * gx + gz * gz);
	const double n1 = nature::fbm2(x / 3.0, z / 3.0, 52), n2 = nature::fbm2(x / 25.0, z / 25.0, 53);
	Vec3 sand = lerp(Vec3{0.74, 0.52, 0.34}, Vec3{0.82, 0.60, 0.40}, n2);   // (the orange sand of Ash Twin)
	sand = sand * (0.88 + 0.24 * n1);
	const Vec3 rock = Vec3{0.52, 0.36, 0.27} * (0.8 + 0.4 * n1);   // (and the rust rock of Ember Twin)
	Vec3 c = lerp(sand, rock, smoothstep(0.35, 0.9, slope));
	const double cf = canyonDepthFraction(x, z);
	if (cf > 0.0) {
		const double depth = -h;   // (below the plain)
		const Vec3 band = strata(depth, x, z) * (0.85 + 0.3 * n1);
		c = lerp(c, band, smoothstep(0.0, 0.08, cf) * smoothstep(0.25, 0.6, slope + 0.2));
		if (cf > 0.97) c = lerp(c, lerp(Vec3{0.78, 0.56, 0.36}, Vec3{0.58, 0.40, 0.28}, n1), 0.75 * (1.0 - smoothstep(0.3, 0.7, slope)));
	}
	// crater floors a little darker (old dust)
	for (const Crater &k : craters()) {
		const double d = std::sqrt((x - k.x) * (x - k.x) + (z - k.z) * (z - k.z)) / k.r;
		if (d < 1.0) c = c * (1.0 - 0.12 * (1.0 - d));
	}
	return c;
}

// The ground as two meshes on rings round the site: fine near it (half a metre apart at the site, about a metre at the canyon) and coarser out to the far side of the moon.
// The inner one (to kInnerR) has a picture of its colours laid flat over x, z; the outer one a picture in polar form. They share the ring between them, so there is no seam.
constexpr double kInnerR = 450.0;
inline double ringStep(double r) { return 0.5 + 0.0045 * r; }
inline int segmentsAt(double r) { return r <= kInnerR + 1e-6 ? 1536 : (r < 1300.0 ? 1024 : (r < 3200.0 ? 512 : 256)); }
inline std::vector<double> ringRadii() {
	std::vector<double> r{0.0};
	while (r.back() < kInnerR) r.push_back(std::min(kInnerR, r.back() + ringStep(r.back())));
	const double end = kPi * kMoonR - 2.0;
	while (r.back() < end) r.push_back(std::min(end, r.back() + ringStep(r.back())));
	return r;
}
inline double outerTexCoord(double r) { return std::log(1.0 + r / 150.0) / std::log(1.0 + kPi * kMoonR / 150.0); }
inline Mesh groundMeshPart(bool inner) {
	const std::vector<double> rr = ringRadii();
	std::size_t first = 0, last = rr.size() - 1;
	for (std::size_t i = 0; i < rr.size(); ++i)
		if (rr[i] >= kInnerR - 1e-6) {
			(inner ? last : first) = i;
			break;
		}
	Mesh m;
	std::vector<int> prevStart;
	int prevN = 0, prevBase = -1;
	for (std::size_t i = first; i <= last; ++i) {
		const double r = rr[i];
		const int n = r < 1e-9 ? 1 : (inner ? 1536 : segmentsAt(r + 1e-3));
		const int base = static_cast<int>(m.verts.size());
		for (int k = 0; k < n; ++k) {
			const double phi = 2.0 * kPi * k / n;
			const double x = r * std::cos(phi), z = r * std::sin(phi);
			m.verts.push_back(groundPoint(x, z));
			if (inner) m.uvs.push_back({(x + kInnerR) / (2.0 * kInnerR), (z + kInnerR) / (2.0 * kInnerR)});
			else {
				const double g = 0.5 * outerTexCoord(r);
				m.uvs.push_back({0.5 + g * std::cos(phi), 0.5 + g * std::sin(phi)});
			}
		}
		if (prevBase >= 0) {
			if (prevN == 1) {
				for (int k = 0; k < n; ++k) m.tris.push_back({prevBase, base + (k + 1) % n, base + k});
			} else if (prevN == n) {
				for (int k = 0; k < n; ++k) {
					const int a = prevBase + k, b = prevBase + (k + 1) % n, c = base + k, d = base + (k + 1) % n;
					m.tris.push_back({a, b, d});
					m.tris.push_back({a, d, c});
				}
			} else {   // (the next ring has half the points)
				for (int k = 0; k < n; ++k) {
					const int a0 = prevBase + 2 * k, a1 = prevBase + 2 * k + 1, a2 = prevBase + (2 * k + 2) % prevN, b0 = base + k, b1 = base + (k + 1) % n;
					m.tris.push_back({a0, a1, b0});
					m.tris.push_back({a1, b1, b0});
					m.tris.push_back({a1, a2, b1});
				}
			}
		}
		prevBase = base;
		prevN = n;
	}
	if (!inner) {   // the far pole of the moon
		const int c = static_cast<int>(m.verts.size());
		m.verts.push_back(moonCentre() + Vec3{0.0, -1.0, 0.0} * (kMoonR + globalHeight({0.0, -1.0, 0.0}) - globalHeight({0.0, 1.0, 0.0})));
		m.uvs.push_back({0.5, 0.0});
		for (int k = 0; k < prevN; ++k) m.tris.push_back({prevBase + k, prevBase + (k + 1) % prevN, c});
	}
	sdf::computeNormals(m);
	// (the triangles above wind clockwise seen from above: turn them to face up)
	for (auto &t : m.tris) std::swap(t[1], t[2]);
	for (Vec3 &n : m.normals) n = n * -1.0;
	return m;
}
// The steep faces of the inner ground (the canyon's walls): their triangles taken out of the inner mesh into one of their own, whose picture is laid along x and up the
// height, so that the strata run cleanly round the walls instead of stretching down them.
constexpr double kWallSlope = 0.75;
inline void splitWalls(Mesh &inner, Mesh &walls) {
	Mesh keep;
	keep.verts = inner.verts;
	keep.normals = inner.normals;
	keep.uvs = inner.uvs;
	std::vector<int> remap(inner.verts.size(), -1);
	for (const auto &t : inner.tris) {
		const Vec3 &a = inner.verts[static_cast<std::size_t>(t[0])], &b = inner.verts[static_cast<std::size_t>(t[1])], &c = inner.verts[static_cast<std::size_t>(t[2])];
		const Vec3 n = normalize(cross(b - a, c - a));
		const Vec3 mid = (a + b + c) * (1.0 / 3.0);
		const Vec3 up = normalize(mid - moonCentre());
		if (std::abs(dot(n, up)) > kWallSlope) {
			keep.tris.push_back(t);
			continue;
		}
		std::array<int, 3> w{};
		for (int q = 0; q < 3; ++q) {
			const int i = t[static_cast<std::size_t>(q)];
			if (remap[static_cast<std::size_t>(i)] < 0) {
				remap[static_cast<std::size_t>(i)] = static_cast<int>(walls.verts.size());
				const Vec3 &p = inner.verts[static_cast<std::size_t>(i)];
				walls.verts.push_back(p);
				walls.normals.push_back(inner.normals[static_cast<std::size_t>(i)]);
				const double h = length(p - moonCentre()) - kMoonR;
				walls.uvs.push_back({(p.x + kInnerR) / (2.0 * kInnerR), (h + 70.0) / 90.0});
			}
			w[static_cast<std::size_t>(q)] = remap[static_cast<std::size_t>(i)];
		}
		walls.tris.push_back(w);
	}
	inner.tris = keep.tris;
}
inline std::string groundText() {
	static const std::string innerPly = film_cache::ply(kFilmKey, "ground_inner", [] {
		Mesh inner = groundMeshPart(true), walls;
		splitWalls(inner, walls);
		return inner;
	});
	static const std::string wallPly = film_cache::ply(kFilmKey, "ground_walls", [] {
		Mesh inner = groundMeshPart(true), walls;
		splitWalls(inner, walls);
		return walls;
	});
	static const std::string wallPic = film_sky::picture(kFilmKey, "ground_walls", 4096, 1024, [](std::vector<float> &rgb) {
		const int W = 4096, H = 1024;
		for (int j = 0; j < H; ++j)
			for (int i = 0; i < W; ++i) {
				const double x = -kInnerR + 2.0 * kInnerR * (i + 0.5) / W, h = -70.0 + 90.0 * (1.0 - (j + 0.5) / H);
				const double n1 = nature::fbm2(x / 2.5, h / 2.5, 52);
				Vec3 c = strata(-h, x, 0.0) * (0.85 + 0.3 * n1);
				// weathering: darker streaks running down the faces, paler crusts on the ledges
				c = c * (0.86 + 0.14 * nature::fbm2(x / 1.2, h / 14.0, 56));
				const std::size_t k = (static_cast<std::size_t>(j) * W + i) * 3;
				rgb[k] = static_cast<float>(c.x);
				rgb[k + 1] = static_cast<float>(c.y);
				rgb[k + 2] = static_cast<float>(c.z);
			}
	});
	static const std::string outerPly = film_cache::ply(kFilmKey, "ground_outer", [] { return groundMeshPart(false); });
	static const std::string innerPic = film_sky::picture(kFilmKey, "ground_inner", 4096, 4096, [](std::vector<float> &rgb) {
		const int N = 4096;
		for (int j = 0; j < N; ++j)
			for (int i = 0; i < N; ++i) {
				// (image row 0 is v = 1)
				const double x = -kInnerR + 2.0 * kInnerR * (i + 0.5) / N, z = -kInnerR + 2.0 * kInnerR * (1.0 - (j + 0.5) / N);
				const Vec3 c = groundColour(x, z);
				const std::size_t k = (static_cast<std::size_t>(j) * N + i) * 3;
				rgb[k] = static_cast<float>(c.x);
				rgb[k + 1] = static_cast<float>(c.y);
				rgb[k + 2] = static_cast<float>(c.z);
			}
	});
	static const std::string outerPic = film_sky::picture(kFilmKey, "ground_outer", 2048, 2048, [](std::vector<float> &rgb) {
		const int N = 2048;
		const double total = kPi * kMoonR;
		for (int j = 0; j < N; ++j)
			for (int i = 0; i < N; ++i) {
				const double u = (i + 0.5) / N - 0.5, v = (1.0 - (j + 0.5) / N) - 0.5;
				const double g = std::min(1.0, 2.0 * std::sqrt(u * u + v * v));
				const double r = 150.0 * (std::pow(1.0 + total / 150.0, g) - 1.0), phi = std::atan2(v, u);
				const double x = r * std::cos(phi), z = r * std::sin(phi);
				Vec3 c = r < 1400.0 ? groundColour(x, z) : Vec3{0.0, 0.0, 0.0};
				if (r >= 1100.0) {
					const Vec3 d = arcDirection(x, z);
					const Vec3 distant = lerp(Vec3{0.76, 0.54, 0.36}, Vec3{0.52, 0.36, 0.27}, fbm3(d * 18.0, 54)) * (0.85 + 0.3 * fbm3(d * 90.0, 55));
					c = r < 1400.0 ? lerp(c, distant, smoothstep(1100.0, 1400.0, r)) : distant;
				}
				const std::size_t k = (static_cast<std::size_t>(j) * N + i) * 3;
				rgb[k] = static_cast<float>(c.x);
				rgb[k + 1] = static_cast<float>(c.y);
				rgb[k + 2] = static_cast<float>(c.z);
			}
	});
	const auto mat = [](const std::string &tex, const std::string &pic) {
		return "Texture \"" + tex + "\" \"spectrum\" \"imagemap\" \"string filename\" [ \"" + pic + "\" ]\n  Material \"diffuse\" \"texture reflectance\" \"" + tex + "\"";
	};
	return attr(mat("ns_ground_in", innerPic), film_cache::plyShape(innerPly)) + attr(mat("ns_ground_wall", wallPic), film_cache::plyShape(wallPly)) +
		   attr(mat("ns_ground_out", outerPic), film_cache::plyShape(outerPly));
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// Rocks and boulders, scattered over the plain and down in the canyon (instances of a few shapes)
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
constexpr int kRockShapes = 6;
inline std::string rockObjects() {
	std::string out;
	for (int k = 0; k < kRockShapes; ++k) {
		const std::string file = film_cache::ply(kFilmKey, "rock_" + std::to_string(k), [k] { return nature::rock(1.0, 60 + k); });
		const Vec3 c = lerp(Vec3{0.46, 0.32, 0.25}, Vec3{0.64, 0.46, 0.33}, hash01(k, 61));
		out += nature::objectBegin("ns_rock_" + std::to_string(k)) + attr(diffuseMaterial(c), film_cache::plyShape(file)) + nature::objectEnd();
	}
	return out;
}
struct RockPlace {
	double x, z, size, yaw;
	int shape;
};
inline const std::vector<RockPlace> &rockPlaces() {
	static const std::vector<RockPlace> rocks = [] {
		std::vector<RockPlace> v;
		for (int i = 0; i < 4200; ++i) {
			const double r = 420.0 * std::sqrt(hash01(i, 71)), a = 2.0 * kPi * hash01(i, 72);
			const double x = r * std::cos(a), z = r * std::sin(a);
			if (std::sqrt(x * x + z * z) < 26.0) continue;   // (the landing site is clear)
			const double cf = canyonDepthFraction(x, z);
			double size = 0.25 + 1.1 * std::pow(hash01(i, 73), 3.0);
			if (cf > 0.95) size *= 2.2;                 // boulders fallen to the canyon floor
			else if (cf > 0.05) continue;               // (none stuck on the walls)
			v.push_back({x, z, size, 360.0 * hash01(i, 74), static_cast<int>(hash01(i, 75) * kRockShapes) % kRockShapes});
		}
		return v;
	}();
	return rocks;
}
// The rocks within `range` of (cx, cz) (all of them: a negative range).
inline std::string rocksText(double cx, double cz, double range) {
	std::string out = rockObjects();
	for (const RockPlace &p : rockPlaces()) {
		if (range > 0.0 && std::hypot(p.x - cx, p.z - cz) > range) continue;
		out += nature::placeObject("ns_rock_" + std::to_string(p.shape), mul(groundFrame(p.x, p.z, p.yaw, -0.12 * p.size), scale(p.size)));
	}
	return out;
}

}  // namespace nomai
}  // namespace cartoon
