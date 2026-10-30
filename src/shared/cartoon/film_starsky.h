#pragma once
// film_starsky.h -- the real night sky for a film (round 11): the 9,096 stars the eye can see (the Yale Bright Star Catalogue, film_assets/library/stars/bsc5.txt), each
// where it is in the sky, as bright and of the colour it is; the constellations' figures drawn between them if asked (stars/constellations.txt); the Milky Way where it
// really runs (through Cygnus, Sagittarius and Crux); and on a date, the planets - Mercury, Venus, Mars, Jupiter and Saturn - where they were among the stars that day
// (JPL's Keplerian elements for the major planets, 1800 to 2050: E. M. Standish, "Keplerian Elements for Approximate Positions of the Major Planets"), and the sun.
//
// `space ... stars real [constellations] [date YYYY-MM-DD]` paints them into the space set's sky (film_space.h): the celestial north pole up, the vernal equinox
// (right ascension 0) at bearing 0. `sky ... stars real` paints them into a night sky over the land (film_sky.h, film_dressing.h): the pole as high as the film's latitude,
// turning through the night.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include "cartoon_lib.h"
#include "film_cache.h"

namespace cartoon {
namespace starsky {

struct Star {
	double ra = 0.0, dec = 0.0, mag = 6.0;   // degrees (J2000); visual magnitude
	double kelvin = 5800.0;
};
// The catalogue, read once ("" path: film_assets/library/stars/bsc5.txt). Empty when it is not found.
inline const std::vector<Star> &catalogue() {
	static std::once_flag once;
	static std::vector<Star> stars;
	std::call_once(once, [] {
		std::ifstream in(film_cache::asset("library/stars/bsc5.txt"));
		std::string line;
		while (std::getline(in, line)) {
			if (line.empty() || line[0] == '#') continue;
			std::istringstream l(line);
			Star s;
			if (l >> s.ra >> s.dec >> s.mag >> s.kelvin) stars.push_back(s);
		}
	});
	return stars;
}
// The constellations' figures: polylines of (right ascension, declination) in degrees.
inline const std::vector<std::vector<std::pair<double, double>>> &figures() {
	static std::once_flag once;
	static std::vector<std::vector<std::pair<double, double>>> lines;
	std::call_once(once, [] {
		std::ifstream in(film_cache::asset("library/stars/constellations.txt"));
		std::string line;
		while (std::getline(in, line)) {
			if (line.empty() || line[0] == '#') continue;
			std::istringstream l(line);
			std::string name;
			l >> name;
			std::vector<std::pair<double, double>> pts;
			double a = 0.0, d = 0.0;
			while (l >> a >> d) pts.push_back({a, d});
			if (pts.size() > 1) lines.push_back(pts);
		}
	});
	return lines;
}

// Where the sky's coordinates point in the world (pbrt's): the celestial north pole, and the point of the equator at right ascension `ra0` degrees. Right ascension
// grows to the east - to the left of a viewer facing the equator with the pole up, as the real sky's (pbrt's pictures show +x on the right looking along +z).
struct Frame {
	Vec3 north{0.0, 1.0, 0.0}, equator{0.0, 0.0, 1.0};
	double ra0 = 0.0;
	Vec3 world(double ra, double dec) const {
		const double a = (ra - ra0) * kPi / 180.0, d = dec * kPi / 180.0;
		const Vec3 east = cross(equator, north);
		return normalize((equator * std::cos(a) + east * std::sin(a)) * std::cos(d) + north * std::sin(d));
	}
};

// ---- the date ----
// The Julian date of a calendar date and hour (UT), Gregorian.
inline double julianDate(int y, int m, int d, double hour = 0.0) {
	if (m <= 2) y -= 1, m += 12;
	const int A = y / 100, B = 2 - A + A / 4;
	return std::floor(365.25 * (y + 4716)) + std::floor(30.6001 * (m + 1)) + d + B - 1524.5 + hour / 24.0;
}
// "YYYY-MM-DD" (or with "THH:MM") as a Julian date; false when it is not one.
inline bool parseDate(const std::string &s, double &jd) {
	int y = 0, m = 0, d = 0, hh = 0, mm = 0;
	const int n = std::sscanf(s.c_str(), "%d-%d-%dT%d:%d", &y, &m, &d, &hh, &mm);
	if (n < 3 || m < 1 || m > 12 || d < 1 || d > 31 || y < 1800 || y > 2050) return false;
	jd = julianDate(y, m, d, hh + mm / 60.0);
	return true;
}

// ---- the planets ----
struct Body {
	const char *name;
	double a, da, e, de, I, dI, L, dL, peri, dperi, node, dnode;   // (JPL's elements at J2000 and their change a century)
	double H;        // its magnitude at a unit distance from the sun and from us
	double kelvin;   // (its colour, as a star's)
};
inline const std::vector<Body> &bodies() {
	static const std::vector<Body> b = {
		{"mercury", 0.38709927, 0.00000037, 0.20563593, 0.00001906, 7.00497902, -0.00594749, 252.25032350, 149472.67411175, 77.45779628, 0.16047689, 48.33076593, -0.12534081, -0.42, 5200.0},
		{"venus", 0.72333566, 0.00000390, 0.00677672, -0.00004107, 3.39467605, -0.00078890, 181.97909950, 58517.81538729, 131.60246718, 0.00268329, 76.67984255, -0.27769418, -4.40, 6200.0},
		{"earth", 1.00000261, 0.00000562, 0.01671123, -0.00004392, -0.00001531, -0.01294668, 100.46457166, 35999.37244981, 102.93768193, 0.32327364, 0.0, 0.0, 0.0, 5800.0},
		{"mars", 1.52371034, 0.00001847, 0.09339410, 0.00007882, 1.84969142, -0.00813131, -4.55343205, 19140.30268499, -23.94362959, 0.44441088, 49.55953891, -0.29257343, -1.52, 3600.0},
		{"jupiter", 5.20288700, -0.00011607, 0.04838624, -0.00013253, 1.30439695, -0.00183714, 34.39644051, 3034.74612775, 14.72847983, 0.21252668, 100.47390909, 0.20469106, -9.40, 5200.0},
		{"saturn", 9.53667594, -0.00125060, 0.05386179, -0.00050991, 2.48599187, 0.00193609, 49.95424423, 1222.49362201, 92.59887831, -0.41897216, 113.66242448, -0.28867794, -8.88, 4900.0},
	};
	return b;
}
// A body's place round the sun (ecliptic of J2000, astronomical units) at Julian date jd.
inline Vec3 heliocentric(const Body &b, double jd) {
	const double T = (jd - 2451545.0) / 36525.0, r = kPi / 180.0;
	const double a = b.a + b.da * T, e = b.e + b.de * T, I = (b.I + b.dI * T) * r, L = b.L + b.dL * T, peri = b.peri + b.dperi * T, node = (b.node + b.dnode * T) * r;
	const double w = peri * r - node, M = std::remainder((L - peri) * r, 2.0 * kPi);
	double E = M + e * std::sin(M);
	for (int i = 0; i < 10; ++i) E -= (E - e * std::sin(E) - M) / (1.0 - e * std::cos(E));
	const double x = a * (std::cos(E) - e), y = a * std::sqrt(1.0 - e * e) * std::sin(E);
	const double cw = std::cos(w), sw = std::sin(w), cn = std::cos(node), sn = std::sin(node), ci = std::cos(I), si = std::sin(I);
	return {(cw * cn - sw * sn * ci) * x + (-sw * cn - cw * sn * ci) * y, (cw * sn + sw * cn * ci) * x + (-sw * sn + cw * cn * ci) * y, sw * si * x + cw * si * y};
}
// Right ascension and declination (degrees) of an ecliptic direction.
inline void equatorial(Vec3 v, double &ra, double &dec) {
	const double eps = 23.43928 * kPi / 180.0;
	const double x = v.x, y = v.y * std::cos(eps) - v.z * std::sin(eps), z = v.y * std::sin(eps) + v.z * std::cos(eps);
	ra = std::atan2(y, x) * 180.0 / kPi;
	if (ra < 0.0) ra += 360.0;
	dec = std::asin(std::clamp(z / std::max(1e-12, length(v)), -1.0, 1.0)) * 180.0 / kPi;
}
struct Seen {
	std::string name;
	double ra = 0.0, dec = 0.0, mag = 0.0, kelvin = 5800.0;
};
// The planets as seen from the Earth at Julian date jd: where among the stars and how bright.
inline std::vector<Seen> planetsOn(double jd) {
	std::vector<Seen> out;
	const Vec3 earth = heliocentric(bodies()[2], jd);
	for (const Body &b : bodies()) {
		if (std::string(b.name) == "earth") continue;
		const Vec3 p = heliocentric(b, jd), g = p - earth;
		Seen s;
		s.name = b.name, s.kelvin = b.kelvin;
		equatorial(g, s.ra, s.dec);
		s.mag = b.H + 5.0 * std::log10(std::max(1e-6, length(p) * length(g)));
		out.push_back(s);
	}
	return out;
}
// Where the sun is among the stars at Julian date jd.
inline void sunOn(double jd, double &ra, double &dec) { equatorial(heliocentric(bodies()[2], jd) * -1.0, ra, dec); }

// ---- painting them ----
// Paints the stars (and, on a date, the planets; and the constellations' figures) into an equal-angle sky picture `rgb` (W x H, row 0 straight up) through `pixelOf` (a
// world direction, pbrt's, to its row and column), `brightness` as the painted stars' (a magnitude-0 star 30 x brightness of light, spread over its few pixels).
inline void paint(std::vector<float> &rgb, int W, int H, const Frame &frame, double brightness, bool constellations, double jd,
				  const std::function<void(Vec3, int &, int &)> &pixelOf, const std::function<Vec3(double)> &colourOf) {
	const auto splat = [&](Vec3 w, Vec3 c) {
		int row = 0, col = 0;
		pixelOf(w, row, col);
		for (int dr = -1; dr <= 1; ++dr)   // (a little spread, so it does not shimmer between pixels as the camera turns: as the painted stars)
			for (int dc = -1; dc <= 1; ++dc) {
				const int rr = std::clamp(row + dr, 0, H - 1), cc = ((col + dc) % W + W) % W;
				const double f = (dr == 0 && dc == 0) ? 0.5 : (dr == 0 || dc == 0) ? 0.1 : 0.025;
				const std::size_t q = (static_cast<std::size_t>(rr) * W + cc) * 3;
				rgb[q] += static_cast<float>(c.x * f), rgb[q + 1] += static_cast<float>(c.y * f), rgb[q + 2] += static_cast<float>(c.z * f);
			}
	};
	if (constellations)   // (first, so the stars sit on them: faint blue lines from star to star)
		for (const auto &fig : figures())
			for (std::size_t i = 0; i + 1 < fig.size(); ++i) {
				const Vec3 a = frame.world(fig[i].first, fig[i].second), b = frame.world(fig[i + 1].first, fig[i + 1].second);
				const double angle = std::acos(std::clamp(dot(a, b), -1.0, 1.0)), step = 0.5 * kPi / H;
				const int n = std::max(2, static_cast<int>(angle / step));
				for (int k = 2; k < n - 1; ++k) {   // (the ends kept off the stars)
					const double u = static_cast<double>(k) / n;
					const Vec3 w = normalize(a * (1.0 - u) + b * u);
					int row = 0, col = 0;
					pixelOf(w, row, col);
					const std::size_t q = (static_cast<std::size_t>(row) * W + col) * 3;
					rgb[q] += static_cast<float>(0.010 * brightness), rgb[q + 1] += static_cast<float>(0.018 * brightness), rgb[q + 2] += static_cast<float>(0.035 * brightness);
				}
			}
	for (const Star &s : catalogue()) splat(frame.world(s.ra, s.dec), colourOf(s.kelvin) * (brightness * 30.0 * std::pow(10.0, -0.4 * s.mag)));
	if (jd > 0.0)
		for (const Seen &p : planetsOn(jd)) splat(frame.world(p.ra, p.dec), colourOf(p.kelvin) * (brightness * 30.0 * std::pow(10.0, -0.4 * p.mag)));
}

// The Milky Way's pole and centre in the sky's coordinates (the galactic north pole, RA 192.86 Dec +27.13; the galactic centre, RA 266.40 Dec -28.94).
inline Vec3 galacticPole(const Frame &f) { return f.world(192.859, 27.128); }
inline Vec3 galacticCentre(const Frame &f) { return f.world(266.405, -28.936); }

}  // namespace starsky
}  // namespace cartoon
