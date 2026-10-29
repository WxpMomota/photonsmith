#pragma once
// film_space.h -- the set in space for a film file (film_file.h `space`): no ground, no sky, no air - black, with the stars all round and the band of the Milky Way across
// them, and the sun a glowing ball far off.
//
//   stars      a field of some 60 000 stars painted into the sky's picture (an infinite light, as the films' night sky is), their brightnesses spread as the real
//              sky's are (each magnitude fainter two and a half times as many), their colours from their temperatures (most a little orange, a few blue-white,
//              some deep red), crowding towards the band of the Milky Way; not a catalogue of the real stars - the constellations are not there;
//   Milky Way  a band of light round a great circle, brightest towards its centre, broken by dark lanes of dust and knotted with star clouds;
//   the sun    a disc in the sky's picture as big as the sun looks, so bright that it gives the films' 3 units of irradiance; the sky light's importance sampling
//              finds it, its shadows are its own size, and a planet's night side is dark. (Not a glowing ball a thousand kilometres off, as the Nomai film's: the
//              wavefront renderer - the one a frame with a volume in it is drawn by - lights almost nothing with so small and far a sphere, 2026-10-27.)
//
// Scale: the GPU works in single precision, so keep a space film's world within a few hundred kilometres of the origin - planets a few thousand to some tens of
// thousands of metres across, set some tens of kilometres off - and let the camera's moves and the cuts tell the distances (the Nomai film's giant is 17.5 km).
// Coordinates are pbrt's, as a film file's. Directions are a compass bearing (0 +z, 90 +x) and an elevation.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

#include "cartoon_lib.h"
#include "film_sky.h"
#include "nature_lib.h"

namespace cartoon {
namespace space {

// The set in space: how bright its stars and its Milky Way are (1 as painted), which way its sun is (a bearing and an elevation, degrees), how big the sun looks
// (degrees across) and how strong its light (1 the films' sunlight), a seed for the stars.
struct Space {
	bool on = false;
	double stars = 1.0, milkyWay = 1.0, sunBearing = 60.0, sunElevation = 15.0, sunSize = 0.53, sunStrength = 1.0;
	bool sun = true;
	int seed = 1;
	double flare = 0.0, bloom = 0.0, planetshine = 0.0, hum = 0.6;   // the light: the lens's flare and glow, the planets' light on what is near them; the hum of space (film_voyage.h)
};


// A direction from a bearing and an elevation (degrees; pbrt's).
inline Vec3 direction(double bearing, double elevation) {
	const double b = bearing * kPi / 180.0, e = elevation * kPi / 180.0;
	return {std::cos(e) * std::sin(b), std::sin(e), std::cos(e) * std::cos(b)};
}

// ---- colour from temperature ----
namespace detail {
// The CIE 1931 colour matching functions, as Wyman, Sloan and Shirley's multi-lobe fit (2013).
inline double cieX(double l) {
	const double t1 = (l - 442.0) * (l < 442.0 ? 0.0624 : 0.0374), t2 = (l - 599.8) * (l < 599.8 ? 0.0264 : 0.0323), t3 = (l - 501.1) * (l < 501.1 ? 0.0490 : 0.0382);
	return 0.362 * std::exp(-0.5 * t1 * t1) + 1.056 * std::exp(-0.5 * t2 * t2) - 0.065 * std::exp(-0.5 * t3 * t3);
}
inline double cieY(double l) {
	const double t1 = (l - 568.8) * (l < 568.8 ? 0.0213 : 0.0247), t2 = (l - 530.9) * (l < 530.9 ? 0.0613 : 0.0322);
	return 0.821 * std::exp(-0.5 * t1 * t1) + 0.286 * std::exp(-0.5 * t2 * t2);
}
inline double cieZ(double l) {
	const double t1 = (l - 437.0) * (l < 437.0 ? 0.0845 : 0.0278), t2 = (l - 459.0) * (l < 459.0 ? 0.0385 : 0.0725);
	return 1.217 * std::exp(-0.5 * t1 * t1) + 0.681 * std::exp(-0.5 * t2 * t2);
}
}  // namespace detail
// The colour of a black body at `kelvin` in linear sRGB, its luminance 1 (a star's colour: about 3000 K deep orange-red, 5800 K the sun's near-white, 10 000 K and up
// blue-white). Worked out once for every 100 K and kept.
inline Vec3 blackbody(double kelvin) {
	static const std::vector<Vec3> table = [] {
		std::vector<Vec3> t;
		for (int k = 0; k <= 400; ++k) {
			const double T = 1000.0 + 100.0 * k;
			double X = 0.0, Y = 0.0, Z = 0.0;
			for (double l = 380.0; l <= 780.0; l += 5.0) {
				const double m = l * 1e-9, planck = 1.0 / (m * m * m * m * m * (std::exp(1.4388e-2 / (m * T)) - 1.0));
				X += planck * detail::cieX(l), Y += planck * detail::cieY(l), Z += planck * detail::cieZ(l);
			}
			X /= Y, Z /= Y, Y = 1.0;
			const Vec3 rgb{3.2406 * X - 1.5372 * Y - 0.4986 * Z, -0.9689 * X + 1.8758 * Y + 0.0415 * Z, 0.0557 * X - 0.2040 * Y + 1.0570 * Z};
			t.push_back({std::max(0.0, rgb.x), std::max(0.0, rgb.y), std::max(0.0, rgb.z)});
		}
		return t;
	}();
	const double u = std::clamp((kelvin - 1000.0) / 100.0, 0.0, 399.999);
	const int i = static_cast<int>(u);
	return lerp(table[static_cast<std::size_t>(i)], table[static_cast<std::size_t>(i) + 1], u - i);
}

// ---- the starfield ----
// The Milky Way's great circle: its pole (pbrt's direction) and the direction of its centre (in its plane).
inline Vec3 galacticPole() { return normalize(Vec3{0.32, 0.62, -0.72}); }
inline Vec3 galacticCentre() { return normalize(cross(galacticPole(), Vec3{0.0, 1.0, 0.0})); }
// The Milky Way's glow in direction w (pbrt's, unit): its band, brightest towards its centre, with dark lanes and star clouds; 0 .. about 1.
inline double milkyWayAt(Vec3 w, int seed) {
	const Vec3 pole = galacticPole(), centre = galacticCentre(), side = cross(pole, centre);
	const double b = std::asin(std::clamp(dot(w, pole), -1.0, 1.0)) * 180.0 / kPi;   // (galactic latitude)
	const double l = std::atan2(dot(w, side), dot(w, centre));                          // (longitude, 0 at the centre)
	const double toCentre = 0.5 + 0.5 * std::cos(l), width = 7.0 + 9.0 * toCentre;
	const double band = std::exp(-std::pow(b / width, 2.0)) * (0.35 + 0.65 * std::pow(toCentre, 2.0));
	const double clumps = nature::fbm2(6.0 * std::cos(l) + 3.0 + b / 9.0, 6.0 * std::sin(l) + b / 11.0, seed + 501);
	const double lane = smoothstep(0.55, 0.75, nature::fbm2(4.0 * std::cos(l) + b / 4.0, 4.0 * std::sin(l) - b / 5.0, seed + 503)) * std::exp(-std::pow(b / 4.0, 2.0));
	const double bulge = std::exp(-(b * b + std::pow(l * 180.0 / kPi, 2.0)) / (2.0 * 12.0 * 12.0));
	const double fine = nature::fbm2(30.0 * std::cos(l) + b / 2.0, 30.0 * std::sin(l) - b / 2.5, seed + 505);   // (star clouds and knots)
	return std::max(0.0, band * (0.35 + 0.8 * clumps) * (0.45 + 1.1 * fine * fine) * (1.0 - 0.8 * lane) + 0.6 * bulge * (1.0 - 0.6 * lane));
}
// The starfield as the sky's picture (an infinite light's equal-angle picture, film_sky.h's layout: row 0 straight up), written once into the film's cache.
inline std::string starfieldPicture(const std::string &film, const Space &s) {
	char name[96];
	std::snprintf(name, sizeof name, "space_v4_%d_%.2f_%.2f_sun%d_%.2f_%.2f_%.3f_%.3f", s.seed, s.stars, s.milkyWay, s.sun ? 1 : 0, s.sunBearing, s.sunElevation, s.sunSize,
				  s.sunStrength);
	const int W = 4096, H = 2048;
	return film_sky::picture(film, name, W, H, [&](std::vector<float> &rgb) {
		// the pixel a direction falls in (film_sky::render's mapping: story space w = (-sin theta cos phi, cos theta, -sin theta sin phi); pbrt's x is story's -x)
		const auto pixelOf = [&](Vec3 p, int &row, int &col) {
			const double theta = std::acos(std::clamp(p.y, -1.0, 1.0));
			double phi = std::atan2(-p.z, p.x);   // (story x = -pbrt x: -w.x = p.x)
			if (phi < 0.0) phi += 2.0 * kPi;
			row = std::min(H - 1, static_cast<int>(theta / kPi * H)), col = std::min(W - 1, static_cast<int>(phi / (2.0 * kPi) * W));
		};
		const auto dirOf = [&](int row, int col) {
			const double theta = (row + 0.5) / H * kPi, phi = (col + 0.5) / W * 2.0 * kPi;
			return Vec3{std::sin(theta) * std::cos(phi), std::cos(theta), -std::sin(theta) * std::sin(phi)};
		};
		// the Milky Way, and a faint glow of unresolved stars everywhere
		for (int row = 0; row < H; ++row)
			for (int col = 0; col < W; ++col) {
				const Vec3 w = dirOf(row, col);
				const double mw = s.milkyWay * milkyWayAt(w, s.seed) * 0.05;
				const Vec3 c = Vec3{1.0, 0.93, 0.82} * mw + Vec3{0.0012, 0.0013, 0.0016};
				const std::size_t q = (static_cast<std::size_t>(row) * W + col) * 3;
				rgb[q] = static_cast<float>(c.x), rgb[q + 1] = static_cast<float>(c.y), rgb[q + 2] = static_cast<float>(c.z);
			}
		// the sun: a disc of the radiance that gives its irradiance, a little darker towards its rim, each pixel by how much of it the disc covers (16 x 16 samples)
		if (s.sun) {
			const Vec3 sd = direction(s.sunBearing, s.sunElevation);
			const double ar = 0.5 * s.sunSize * kPi / 180.0, L = 3.0 * s.sunStrength / (kPi * ar * ar * (1.0 - 0.6 / 3.0));   // (the limb darkening's share taken out)
			const Vec3 colour = blackbody(5800.0);
			int row = 0, col = 0;
			pixelOf(sd, row, col);
			const int reach = 2 + static_cast<int>(ar / kPi * H / std::max(0.05, std::sin(std::acos(std::clamp(sd.y, -1.0, 1.0))))) + static_cast<int>(ar / kPi * H);
			for (int dr = -reach; dr <= reach; ++dr)
				for (int dc = -reach; dc <= reach; ++dc) {
					const int rr = row + dr;
					if (rr < 0 || rr >= H) continue;
					const int cc = ((col + dc) % W + W) % W;
					double sum = 0.0;
					for (int a = 0; a < 16; ++a)
						for (int b = 0; b < 16; ++b) {
							const double theta = (rr + (a + 0.5) / 16.0) / H * kPi, phi = (cc + (b + 0.5) / 16.0) / W * 2.0 * kPi;
							const Vec3 w{std::sin(theta) * std::cos(phi), std::cos(theta), -std::sin(theta) * std::sin(phi)};
							const double off = std::acos(std::clamp(dot(w, sd), -1.0, 1.0)) / ar;
							if (off < 1.0) sum += 1.0 - 0.6 * (1.0 - std::sqrt(1.0 - off * off));   // (limb darkening)
						}
					if (sum <= 0.0) continue;
					const Vec3 c = colour * (L * sum / 256.0);
					const std::size_t q = (static_cast<std::size_t>(rr) * W + cc) * 3;
					rgb[q] += static_cast<float>(c.x), rgb[q + 1] += static_cast<float>(c.y), rgb[q + 2] += static_cast<float>(c.z);
				}
		}
		// the stars: magnitudes from -1 to 9, each fainter magnitude about 2.5 times as many; the faint ones crowding to the Milky Way
		const int count = static_cast<int>(60000.0 * std::clamp(s.stars, 0.0, 4.0));
		for (int k = 0; k < count; ++k) {
			const int h = s.seed * 1000003 + k;
			const double mag = -1.0 + std::log(1.0 + hash01(h, 1) * (std::pow(2.5, 10.0) - 1.0)) / std::log(2.5);   // (-1 .. 9, most of them faint)
			Vec3 w;
			for (int tries = 0; tries < 8; ++tries) {   // (a direction, kept with a chance that grows towards the band for the faint ones)
				const double z = 2.0 * hash01(h, 2 + tries * 3) - 1.0, a = 2.0 * kPi * hash01(h, 3 + tries * 3), r = std::sqrt(std::max(0.0, 1.0 - z * z));
				w = {r * std::cos(a), z, r * std::sin(a)};
				const double keep = 0.25 + 0.75 * std::min(1.0, 2.0 * milkyWayAt(w, s.seed)) * smoothstep(3.0, 7.0, mag) + (1.0 - smoothstep(3.0, 7.0, mag));
				if (hash01(h, 4 + tries * 3) < keep) break;
			}
			const double T = hash01(h, 30) < 0.08 ? 9000.0 + 21000.0 * hash01(h, 31) : 3000.0 + 4500.0 * std::pow(hash01(h, 32), 0.7);   // (a few hot blue-white ones)
			const double flux = s.stars * 30.0 * std::pow(10.0, -0.4 * mag);
			int row = 0, col = 0;
			pixelOf(w, row, col);
			const Vec3 c = blackbody(T) * flux;
			for (int dr = -1; dr <= 1; ++dr)   // (a little spread, so it does not shimmer between pixels as the camera turns)
				for (int dc = -1; dc <= 1; ++dc) {
					const int rr = std::clamp(row + dr, 0, H - 1), cc = ((col + dc) % W + W) % W;
					const double f = (dr == 0 && dc == 0) ? 0.5 : (dr == 0 || dc == 0) ? 0.1 : 0.025;
					const std::size_t q = (static_cast<std::size_t>(rr) * W + cc) * 3;
					rgb[q] += static_cast<float>(c.x * f), rgb[q + 1] += static_cast<float>(c.y * f), rgb[q + 2] += static_cast<float>(c.z * f);
				}
		}
	});
}

// A space film's frame moved so its camera is near the origin (a floating origin): the GPU works in float, whose grid far from the origin is coarse (0.06 at a
// million, more than the 0.01 a shadow ray starts off a surface), so the camera (its `LookAt`) and everything in the world are moved by the same whole number of
// kilometres. (It was first made for a worse loss, fixed since: the GPU's camera rays snapped to a grid of directions far out, so a planet 75 km away came out in
// blocks - GpuCameraParams::lower_left_offset.) The world's lines are wrapped in a Translate, but not an object's definition (ObjectBegin .. ObjectEnd): its
// instances are placed inside the Translate, and pbrt composes the two. Lights at infinity do not move. A scene with its camera within 5 km of the origin is left as
// it is.
inline std::string recentred(const std::string &scene) {
	const std::size_t look = scene.find("LookAt "), world = scene.find("WorldBegin\n");
	if (look == std::string::npos || world == std::string::npos || look > world) return scene;
	const std::size_t end = scene.find('\n', look);
	std::istringstream in(scene.substr(look + 7, end - look - 7));
	double v[9];
	for (double &x : v)
		if (!(in >> x)) return scene;
	const double ox = std::round(v[0] / 1000.0) * 1000.0, oy = std::round(v[1] / 1000.0) * 1000.0, oz = std::round(v[2] / 1000.0) * 1000.0;
	if (std::max({std::abs(ox), std::abs(oy), std::abs(oz)}) < 5000.0) return scene;
	char buf[320];
	std::snprintf(buf, sizeof buf, "LookAt %.6f %.6f %.6f  %.6f %.6f %.6f  %.6f %.6f %.6f", v[0] - ox, v[1] - oy, v[2] - oz, v[3] - ox, v[4] - oy, v[5] - oz, v[6], v[7], v[8]);
	char open[160];
	std::snprintf(open, sizeof open, "AttributeBegin   # (a floating origin: film_space.h recentred)\n  Translate %.1f %.1f %.1f\n", -ox, -oy, -oz);
	std::string out = scene.substr(0, look) + buf + scene.substr(end, world + 11 - end) + open;
	// the world, with each object's definition kept outside the Translate
	std::size_t at = world + 11;
	while (at < scene.size()) {
		const std::size_t ob = scene.find("ObjectBegin \"", at);
		if (ob == std::string::npos || (ob > 0 && scene[ob - 1] != '\n')) {
			out += scene.substr(at);
			break;
		}
		const std::size_t oe = scene.find("ObjectEnd\n", ob);
		if (oe == std::string::npos) {
			out += scene.substr(at);
			break;
		}
		out += scene.substr(at, ob - at) + "AttributeEnd\n" + scene.substr(ob, oe + 10 - ob) + open;
		at = oe + 10;
	}
	return out + "AttributeEnd\n";
}

// Which way the sun is (pbrt's, unit).
inline Vec3 sunDirection(const Space &s) { return direction(s.sunBearing, s.sunElevation); }
// The set in space as scene text: the starfield, with the sun in it (the sky's picture as an infinite light). `sunAsBall`: for a narrow shot with the sun in it (the
// sky's picture is too coarse there: the sun showed as a square of its pixels), the sun is a glowing ball a thousand kilometres off instead, round however close the
// lens, and the picture leaves it out. `eye` (story space): the camera, which the ball is that far from along the sun's direction - placed from the world's origin, a
// camera tens of kilometres out saw it degrees off the sun, and an eclipse's corona round a planet with no sun behind it.
inline std::string spaceText(const std::string &film, const Space &s, bool sunAsBall = false, Vec3 eye = {0.0, 0.0, 0.0}) {
	if (!sunAsBall || !s.sun) return "# space (film_space.h)\nLightSource \"infinite\" \"string filename\" [ \"" + starfieldPicture(film, s) + "\" ]\n";
	Space without = s;
	without.sun = false;
	const double D = 1.0e6, radius = D * std::tan(0.5 * s.sunSize * kPi / 180.0), L = 3.0 * s.sunStrength / (kPi * std::pow(radius / D, 2.0));
	const Vec3 p = sunDirection(s) * D;   // (pbrt's)
	return "# space (film_space.h): the sun as a ball, for a narrow shot of it\nLightSource \"infinite\" \"string filename\" [ \"" + starfieldPicture(film, without) + "\" ]\n" +
		   emissiveSphere(eye + Vec3{-p.x, p.y, p.z}, radius, blackbody(5800.0) * L);
}
// Whether a shot (its camera at `eye` looking at `target`, story space, `fov` degrees) is narrow and has the sun in it.
inline bool narrowOnTheSun(const Space &s, Vec3 eye, Vec3 target, double fov) {
	if (!s.sun || fov > 12.0) return false;
	const Vec3 look = normalize(Vec3{-(target.x - eye.x), target.y - eye.y, target.z - eye.z});
	return std::acos(std::clamp(dot(look, sunDirection(s)), -1.0, 1.0)) * 180.0 / kPi < fov;
}

}  // namespace space
}  // namespace cartoon
