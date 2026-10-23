#pragma once
// film_sky.h -- painted skies for the long films: a whole sky as one picture (an equirectangular HDR image the renderers read as an "infinite" light), made once from a few
// numbers and kept in the film's cache folder (film_cache.h). A sky is:
//   the clear sky       the Hosek-Wilkie model (hosek_sky.h, as scene_sky.h uses it) for the sun's height, carried below the horizon into the blue of twilight;
//   the night           a deep blue sky with the Milky Way across it (a band of glow with dark lanes of dust) and a field of faint stars;
//   the sun's glow      a halo round the sun and the afterglow along the horizon under it, for the drama of a sunrise or sunset;
//   clouds              layers of cloud at a height, streaked along the wind, lit from the sun's side (bright, warm edges towards it, darker cores away from it), fading into the
//                       haze towards the horizon.
// The picture's units are scene_sky.h's (the sky of a full sun is in the same units as that sun's irradiance, 3.0); the film's camera exposure does the rest.
//
// Story space as everywhere in the films (+x to the right of the picture, +y up, +z towards the camera); an azimuth is measured from -z (into the picture) towards +x. The
// picture is in pbrt's space (x negated), row 0 straight up, column phi from pbrt's +x towards -z, the layout scene_sky.h writes and every backend reads.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "film_cache.h"
#include "nature_lib.h"
#include "../scene_sky.h"

namespace cartoon {
namespace film_sky {

struct Clouds {
	double altitude = 2500.0;                             // metres above the ground
	double scaleAlong = 9000.0, scaleAcross = 2500.0;     // the size of the cloud shapes along the streaks and across them
	double direction = 20.0;                              // the streaks' azimuth, degrees
	double coverage = 0.45;                               // 0 a clear sky .. 1 overcast
	double softness = 0.16;                               // how soft the edges are
	double opacity = 0.95;
	Vec3 lit = {1.0, 1.0, 1.0};                           // the radiance of the side the sun lights
	Vec3 shade = {0.2, 0.2, 0.25};                        // and of the side away from it
	double silver = 1.2;                                  // how much brighter the thin edges are towards the sun
	double offsetX = 0.0, offsetZ = 0.0;                  // where the pattern is (metres): a different offset is a different sky
	double haze = 45000.0;                                // the distance over which far clouds melt into the sky
	int seed = 1;
};

struct Spec {
	std::string name = "sky";        // the start of the file name
	double sunElevation = 10.0;      // degrees; below 0 the sun has set
	double sunAzimuth = 0.0;         // degrees from -z towards +x
	double turbidity = 3.0;
	double brightness = 1.0;         // the clear sky's brightness
	Vec3 tint = {1.0, 1.0, 1.0};
	double night = 0.0;              // 0 day or twilight .. 1 night
	double nightBrightness = 1.0;
	double milkyWay = 0.0;           // the band's brightness (night)
	Vec3 milkyPole = {0.35, 0.45, -0.82};   // the band runs round the great circle about this direction
	double stars = 0.0;              // the faint stars' brightness
	double glow = 0.0;               // the halo round the sun
	Vec3 glowColour = {1.0, 0.55, 0.25};
	double afterglow = 0.0;          // the warm band along the horizon under the sun
	Vec3 afterglowColour = {1.0, 0.42, 0.16};
	double belt = 0.0;               // the pink band opposite the sun at twilight (the belt of Venus)
	std::vector<Clouds> clouds;
	int width = 4096;                // the picture is width x width/2
};

inline Vec3 direction(double elevationDeg, double azimuthDeg) {
	const double el = elevationDeg * kPi / 180.0, az = azimuthDeg * kPi / 180.0;
	return {std::sin(az) * std::cos(el), std::sin(el), -std::cos(az) * std::cos(el)};
}

namespace detail {

inline double luminance(Vec3 c) { return 0.2126 * c.x + 0.7152 * c.y + 0.0722 * c.z; }
inline Vec3 mulc(Vec3 a, Vec3 b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }

// The clear sky (Hosek-Wilkie, scene_sky.h's units) at `w`, for a sun at `sun`; below the horizon the model's sky for a sun just up, darkened and turned blue.
struct ClearSky {
	explicit ClearSky(const Spec &s) : spec(s), model(s.turbidity, 0.1, (std::max)(0.5, s.sunElevation) * kPi / 180.0) {
		sun = direction((std::max)(0.5, s.sunElevation), s.sunAzimuth);
		const double below = (std::max)(0.0, 0.5 - s.sunElevation);   // degrees under "just up"
		dim = std::exp(-below / 2.4);
		blue = clamp01(below / 7.0);
	}
	Vec3 at(Vec3 w) const {
		const double theta = std::acos((std::max)(0.0, (std::min)(1.0, w.y)));
		const double gamma = std::acos((std::max)(-1.0, (std::min)(1.0, dot(w, sun))));
		double xyz[3];
		model.xyz(theta, gamma, xyz);
		const double k = 683.0 * scene_doc::sky::kScenePerLux;
		Vec3 c = {(std::max)(0.0, k * (3.2404542 * xyz[0] - 1.5371385 * xyz[1] - 0.4985314 * xyz[2])), (std::max)(0.0, k * (-0.9692660 * xyz[0] + 1.8760108 * xyz[1] + 0.0415560 * xyz[2])),
				  (std::max)(0.0, k * (0.0556434 * xyz[0] - 0.2040259 * xyz[1] + 1.0572252 * xyz[2]))};
		c = c * dim;
		// twilight: the sky loses its warmth first and turns a deep blue
		const double l = luminance(c);
		c = lerp(c, Vec3{0.55, 0.75, 1.55} * l, blue);
		return c;
	}
	const Spec &spec;
	hosek_sky::Model model;
	Vec3 sun;
	double dim = 1.0, blue = 0.0;
};

// The night sky in direction w: a gradient from a lighter horizon to a deep zenith, the Milky Way and the faint stars.
inline Vec3 nightSky(const Spec &s, Vec3 w, int row, int col) {
	const double up = (std::max)(0.0, w.y);
	Vec3 c = lerp(Vec3{0.0060, 0.0072, 0.0125}, Vec3{0.0012, 0.0017, 0.0042}, std::pow(up, 0.5)) * s.nightBrightness;
	if (s.milkyWay > 0.0) {
		const Vec3 pole = normalize(s.milkyPole);
		const double off = dot(w, pole);   // the distance from the band's mid-line (sine of it)
		const Vec3 along = normalize(w - pole * off);
		const double u = std::atan2(along.z, along.x), v = off;
		// the band: wide and soft, brighter towards its core, with clumps and a dark lane of dust down the middle
		double band = std::exp(-std::pow(v / 0.20, 2.0)) * (0.55 + 0.9 * nature::fbm2(u * 6.0, v * 22.0, 401));
		band += 0.8 * std::exp(-std::pow(v / 0.07, 2.0)) * nature::fbm2(u * 14.0, v * 40.0, 402);
		const double core = 0.6 + 0.8 * std::exp(-std::pow(std::remainder(u - 0.6, 2.0 * kPi) / 1.1, 2.0));
		const double dust = std::exp(-std::pow((v - 0.015 * std::sin(3.0 * u)) / 0.035, 2.0)) * nature::fbm2(u * 9.0, v * 30.0, 403);
		band = (std::max)(0.0, band * core * (1.0 - 0.85 * dust));
		c = c + Vec3{0.80, 0.86, 1.0} * (0.022 * s.milkyWay * band);
	}
	if (s.stars > 0.0) {
		const double h = hash01(row * 7919 + col, 404);
		if (h > 0.9965) {
			const double b = std::pow((h - 0.9965) / 0.0035, 6.0);
			const double tintK = hash01(row * 13 + col * 7, 405);
			c = c + lerp(Vec3{1.0, 0.85, 0.7}, Vec3{0.75, 0.85, 1.0}, tintK) * (0.25 * s.stars * (0.15 + b));
		}
	}
	return c;
}

// A cloud layer's density (0..1) at a point of its plane.
inline double cloudDensity(const Clouds &L, double x, double z) {
	const double d = L.direction * kPi / 180.0;
	x += L.offsetX;
	z += L.offsetZ;
	const double a = (x * std::sin(d) - z * std::cos(d)) / L.scaleAlong, b = (x * std::cos(d) + z * std::sin(d)) / L.scaleAcross;
	// a little domain warping so the shapes are billowy rather than blobby
	const double wa = nature::fbm2(a * 0.7 + 3.1, b * 0.7, L.seed + 11) - 0.5, wb = nature::fbm2(a * 0.7, b * 0.7 + 7.3, L.seed + 12) - 0.5;
	double n = nature::fbm2(a + 0.9 * wa, b + 0.9 * wb, L.seed);
	n = 0.8 * n + 0.2 * nature::fbm2(a * 4.3 + 1.7, b * 4.3, L.seed + 13);
	const double edge = 1.0 - L.coverage;
	return smoothstep(edge - L.softness, edge + L.softness, n);
}

}  // namespace detail

// The file name (in the film's cache) that says which sky it is: every number goes into it.
inline std::string fileName(const Spec &s) {
	std::string key;
	char b[400];
	std::snprintf(b, sizeof b, "v3|%.3f|%.3f|%.3f|%.4f|%.4f %.4f %.4f|%.3f|%.3f|%.3f|%.3f %.3f %.3f|%.3f|%.3f|%.4f %.4f %.4f|%.3f|%.4f %.4f %.4f|%.3f|%d", s.sunElevation, s.sunAzimuth, s.turbidity,
				  s.brightness, s.tint.x, s.tint.y, s.tint.z, s.night, s.nightBrightness, s.milkyWay, s.milkyPole.x, s.milkyPole.y, s.milkyPole.z, s.stars, s.glow, s.glowColour.x, s.glowColour.y,
				  s.glowColour.z, s.afterglow, s.afterglowColour.x, s.afterglowColour.y, s.afterglowColour.z, s.belt, s.width);
	key = b;
	for (const Clouds &c : s.clouds) {
		std::snprintf(b, sizeof b, "|%.1f %.1f %.1f %.2f %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.1f %.1f %.1f %d", c.altitude, c.scaleAlong, c.scaleAcross, c.direction, c.coverage,
					  c.softness, c.opacity, c.lit.x, c.lit.y, c.lit.z, c.shade.x, c.shade.y, c.shade.z, c.silver, c.offsetX, c.offsetZ, c.haze, 0.0, 0.0, c.seed);
		key += b;
	}
	char name[64];
	std::snprintf(name, sizeof name, "_%016llx.hdr", static_cast<unsigned long long>(std::hash<std::string>()(key)));
	return s.name + name;
}

// The sky's picture: width x width/2 linear RGB floats, row 0 straight up.
inline std::vector<float> render(const Spec &s) {
	const int W = s.width, H = s.width / 2;
	std::vector<float> rgb(static_cast<std::size_t>(W) * H * 3, 0.0f);
	const detail::ClearSky clear(s);
	const Vec3 sunDir = direction(s.sunElevation, s.sunAzimuth);
	// the clear sky is smooth: work it out on a coarse grid and blend
	const int cw = (std::min)(W, 1024), ch = cw / 2;
	std::vector<Vec3> coarse(static_cast<std::size_t>(cw) * (ch + 1));
	const auto pbrtToStory = [](double theta, double phi) {
		const double st = std::sin(theta), ct = std::cos(theta);
		return Vec3{-st * std::cos(phi), ct, -st * std::sin(phi)};
	};
	const int coarseRows = ch / 2 + 1;   // (rows down to the horizon; below it the horizon's colour)
	if (s.night < 1.0)
		for (int r = 0; r <= coarseRows; ++r)
			for (int c = 0; c < cw; ++c) {
				const double theta = (std::min)(kPi / 2.0, (r + 0.5) / ch * kPi), phi = (c + 0.5) / cw * 2.0 * kPi;
				Vec3 w = pbrtToStory(theta, phi);
				w.y = (std::max)(w.y, 0.0005);
				coarse[static_cast<std::size_t>(r) * cw + c] = clear.at(normalize(w));
			}
	for (int row = 0; row < H; ++row) {
		const double theta = (row + 0.5) / H * kPi;
		for (int col = 0; col < W; ++col) {
			const double phi = (col + 0.5) / W * 2.0 * kPi;
			const Vec3 w = pbrtToStory(theta, phi);
			const double up = w.y;
			Vec3 c{0.0, 0.0, 0.0};
			if (s.night < 1.0) {
				// (bilinear in the coarse grid)
				const double gr = (std::max)(0.0, (std::min)(static_cast<double>(coarseRows) - 1.0001, (std::min)(theta, kPi / 2.0) / kPi * ch - 0.5));
				const double gc = (phi / (2.0 * kPi)) * cw - 0.5;
				const int r0 = static_cast<int>(gr), c0 = static_cast<int>(std::floor(gc));
				const double fy = gr - r0, fx = gc - c0;
				const auto at = [&](int rr, int cc) { return coarse[static_cast<std::size_t>((std::min)(rr, coarseRows)) * cw + static_cast<std::size_t>(((cc % cw) + cw) % cw)]; };
				c = lerp(lerp(at(r0, c0), at(r0, c0 + 1), fx), lerp(at(r0 + 1, c0), at(r0 + 1, c0 + 1), fx), fy) * s.brightness;
			}
			if (s.night > 0.0) c = lerp(c, detail::nightSky(s, w, row, col), s.night);
			const double cosG = dot(w, sunDir), gamma = std::acos((std::max)(-1.0, (std::min)(1.0, cosG))) * 180.0 / kPi;
			if (s.glow > 0.0) c = c + s.glowColour * (s.glow * (std::exp(-gamma / 6.0) + 0.35 * std::exp(-gamma / 22.0)));
			const double elevDeg = std::asin((std::max)(-1.0, (std::min)(1.0, up))) * 180.0 / kPi;
			if (s.afterglow > 0.0 || s.belt > 0.0) {
				const Vec3 flat = normalize(Vec3{w.x, 0.0, w.z}), sunFlat = normalize(Vec3{sunDir.x, 0.0, sunDir.z});
				const double daz = std::acos((std::max)(-1.0, (std::min)(1.0, dot(flat, sunFlat)))) * 180.0 / kPi;
				if (elevDeg > -2.0) {
					c = c + s.afterglowColour * (s.afterglow * std::exp(-(std::max)(0.0, elevDeg) / 5.0) * std::exp(-std::pow(daz / 65.0, 2.0)));
					c = c + Vec3{0.85, 0.45, 0.55} * (s.belt * std::exp(-std::pow((elevDeg - 7.0) / 6.0, 2.0)) * std::exp(-std::pow((180.0 - daz) / 80.0, 2.0)));
				}
			}
			c = detail::mulc(c, s.tint);
			// clouds, nearest layer last
			if (up > 0.004) {
				for (const Clouds &L : s.clouds) {
					const double dist = L.altitude / up;
					if (dist > 6.0 * L.haze) continue;
					const double x = w.x * dist, z = w.z * dist;
					const double d = detail::cloudDensity(L, x, z);
					if (d <= 0.0005) continue;
					// the light: towards the sun the cloud is lit through its thin edges; its own shadow is the density a little towards the sun
					const Vec3 toSun = normalize(Vec3{sunDir.x, 0.0, sunDir.z});
					const double shadow = detail::cloudDensity(L, x + toSun.x * 0.06 * L.scaleAcross, z + toSun.z * 0.06 * L.scaleAcross);
					const double forward = std::exp(-gamma / 14.0) * L.silver;
					const double lit = clamp01(0.30 + 0.70 * (1.0 - shadow) + forward * (1.0 - d));
					Vec3 cc = lerp(L.shade, L.lit, lit) + L.lit * (forward * 0.6 * (1.0 - d));
					// far clouds melt into the sky behind them
					const double hz = 1.0 - std::exp(-dist / L.haze);
					cc = lerp(cc, c, hz * 0.85);
					c = lerp(c, cc, d * L.opacity);
				}
			}
			if (up < 0.0) c = c * 0.25;   // (the ground below the horizon: hidden by the land, dimly seen in a reflection)
			float *px = &rgb[(static_cast<std::size_t>(row) * W + col) * 3];
			px[0] = static_cast<float>(c.x);
			px[1] = static_cast<float>(c.y);
			px[2] = static_cast<float>(c.z);
		}
	}
	return rgb;
}

// The sky's file (made the first time it is asked for) and the LightSource text that puts it round the scene, turned `turnDeg` about the vertical (clouds drifting) and scaled.
inline std::string file(const std::string &film, const Spec &s) {
	const std::filesystem::path target = film_cache::dir(film) / fileName(s);
	std::error_code ec;
	if (!(std::filesystem::exists(target, ec) && std::filesystem::file_size(target, ec) > 0)) {
		const std::vector<float> rgb = render(s);
		film_cache::writeOnce(target, scene_doc::sky::encodeHdr(rgb, s.width, s.width / 2));
	}
	return film_cache::scenePath(target);
}
inline std::string light(const std::string &film, const Spec &s, double scale = 1.0, double turnDeg = 0.0) {
	char buf[160];
	std::snprintf(buf, sizeof buf, "AttributeBegin\n  Rotate %.4f 0 1 0\n", turnDeg);
	std::string out = buf;
	std::snprintf(buf, sizeof buf, "\" ] \"float scale\" [ %.5f ]\nAttributeEnd\n", scale);
	return out + "  LightSource \"infinite\" \"string filename\" [ \"" + file(film, s) + buf;
}

}  // namespace film_sky
}  // namespace cartoon
