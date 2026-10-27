#pragma once
// film_skyfx.h -- the sky's spectacles for a film file (film_file.h `aurora`, `lightning`, `meteors`; the rainbow and the stars are the `sky` line's: film_dressing.h):
//
//   aurora     curtains of light hanging in the night sky some kilometres off: ribbons (area lights with a picture of their glow) whose folds drift and whose rays run along
//              them, a bright green lower edge fading up through green into red and violet, lighting the land and snow below a little;
//   lightning  strikes at times of their own: a jagged, branching bolt from the cloud base to the ground, flickering in three or four strokes over a quarter of a second, a
//              flash that lights the land and the clouds (a point light at the bolt's top), and thunder heard as long after as sound takes to come from it to the camera;
//   meteors    shooting stars: a bright head and a fading tail drawn across the night sky in a second or less.
//
// Coordinates are pbrt's, as a film file's; the meshes are made in story space (x mirrored) and written with emitMesh.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "cartoon_audio.h"
#include "cartoon_lib.h"
#include "film_sky.h"
#include "nature_lib.h"

namespace cartoon {
namespace skyfx {

// Aurora: its colour (green, red or violet: the colour of its lower edge), how bright, the way it lies from the middle of the set (a facing: north by default is the film's
// own north), how far off (metres), when.
struct Aurora {
	std::string colour = "green";
	double brightness = 1.0, facing = 0.0, distance = 9000.0, t0 = 0.0, t1 = 1e30;
	bool facingSet = false;
};
// Lightning: so many strikes between t0 and t1, round (x, z) within `radius` metres, the cloud base it falls from, a seed. The strikes are chosen when the film is loaded.
struct Strike {
	double t = 0.0, x = 0.0, z = 0.0;
	int seed = 0;
};
struct Lightning {
	int count = 4;
	double t0 = 0.0, t1 = 1e30, x = 0.0, z = -3000.0, radius = 2500.0, base = 1400.0;
	int seed = 1;
	std::vector<Strike> strikes;
};
// Meteors: so many between t0 and t1, a seed.
struct Meteors {
	int count = 6;
	double t0 = 0.0, t1 = 1e30;
	int seed = 1;
};
inline const std::vector<std::string> &auroraColours() {
	static const std::vector<std::string> k = {"green", "red", "violet"};
	return k;
}

namespace detail {
inline Vec3 story(Vec3 p) { return {-p.x, p.y, p.z}; }
inline void ribbon(Mesh &m, const std::vector<Vec3> &bottom, const std::vector<Vec3> &top, double u0, double u1) {   // (story space) with uv: u along, v up
	const int base = static_cast<int>(m.verts.size()), n = static_cast<int>(bottom.size());
	for (int i = 0; i < n; ++i) {
		const double u = u0 + (u1 - u0) * i / std::max(1, n - 1);
		for (const Vec3 &p : {bottom[static_cast<std::size_t>(i)], top[static_cast<std::size_t>(i)]}) {
			m.verts.push_back(p);
			m.normals.push_back({0.0, 0.0, 1.0});
		}
		m.uvs.push_back({u, 0.0});
		m.uvs.push_back({u, 1.0});
	}
	for (int i = 0; i + 1 < n; ++i) {
		const int a = base + 2 * i;
		m.tris.push_back({a, a + 2, a + 1});
		m.tris.push_back({a + 1, a + 2, a + 3});
	}
	for (std::size_t i = static_cast<std::size_t>(base); i + 1 < m.verts.size(); i += 2) {   // (normals across the ribbon, from its run)
		const std::size_t j = std::min(m.verts.size() - 2, i + 2), k = i >= static_cast<std::size_t>(base) + 2 ? i - 2 : i;
		const Vec3 along = m.verts[j] - m.verts[k], up = m.verts[i + 1] - m.verts[i];
		const Vec3 n = length(cross(along, up)) > 1e-9 ? normalize(cross(along, up)) : Vec3{0, 0, 1};
		m.normals[i] = m.normals[i + 1] = n;
	}
}
}  // namespace detail

// ---- aurora ----
// How strong the aurora's ray is at u (0 to 1 across its picture, the whole curtain): its rays a few hundred metres apart, finely striated; `salt` a curtain's own.
inline double auroraRay(double u, int salt = 0) {
	const double broad = clamp01(std::pow(nature::fbm2(u * 126.0, 0.5, 71 + 7 * salt), 2.0) * 2.6), fine = nature::noise2(u * 700.0, 0.5, 72 + 7 * salt);
	return broad * (0.7 + 0.3 * fine);
}
// The aurora's glow, painted once: u across its curtain, v up it - a bright lower edge, each ray rising as high as it is strong and fading out at its top, green going up into red
// or violet. It is a glow the sky shows through (auroraText: the curtain is clear glass that is a lamp), so it fades to nothing.
// `curtain`: which of the three (each its own rays).
inline std::string auroraPicture(const std::string &film, const std::string &colour, int curtain = 0) {
	return film_sky::picture(film, "aurora2_" + colour + "_" + std::to_string(curtain), 4096, 256, [&](std::vector<float> &rgb) {
		const Vec3 low = colour == "red" ? Vec3{1.0, 0.18, 0.22} : colour == "violet" ? Vec3{0.6, 0.25, 1.0} : Vec3{0.25, 1.0, 0.45};
		const Vec3 high = colour == "green" ? Vec3{0.35, 0.08, 0.3} : Vec3{0.3, 0.06, 0.35};
		for (int i = 0; i < 4096; ++i) {
			const double u = (i + 0.5) / 4096.0, ray = auroraRay(u, curtain), reach = 0.3 + 0.7 * ray, across = 0.25 + 0.75 * ray;
			const double ends = smoothstep(0.0, 0.12, u) * (1.0 - smoothstep(0.88, 1.0, u));   // (the curtain's ends fade out)
			for (int j = 0; j < 256; ++j) {
				const double v = 1.0 - (j + 0.5) / 256.0;
				const double edge = std::exp(-v / 0.05) * 1.4, body = std::exp(-v / 0.35), rise = smoothstep(0.0, 0.03, v), top = 1.0 - smoothstep(0.45 * reach, reach, v);
				const Vec3 c = lerp(low, high, smoothstep(0.3, 0.85, v / reach)) * ((edge + body) * rise * top * across * ends);
				const std::size_t k = (static_cast<std::size_t>(j) * 4096 + i) * 3;
				rgb[k] = static_cast<float>(c.x);
				rgb[k + 1] = static_cast<float>(c.y);
				rgb[k + 2] = static_cast<float>(c.z);
			}
		}
	});
}
// The aurora at t (seen round `centre`, pbrt's), as scene text: three curtains at different distances, their folds drifting and their rays running along them. Each is a lamp of
// clear glass (index 1): its light is added and the stars and the other curtains show through it, as through the real thing.
inline std::string auroraText(const std::string &film, const Aurora &a, double t, Vec3 centre) {
	if (t < a.t0 || t > a.t1) return std::string();
	const double fade = smoothstep(a.t0, a.t0 + 3.0, t) * (1.0 - smoothstep(a.t1 - 3.0, a.t1, t));
	std::string out = "# the aurora (film_skyfx.h)\n";
	for (int c = 0; c < 3; ++c) {
		Mesh m;
		std::vector<Vec3> bottom, top;
		const double D = a.distance * (0.8 + 0.25 * c), mid = a.facing * kPi / 180.0 + 0.25 * (c - 1);
		const double low = (0.22 + 0.04 * c) * D, high = low + (0.34 + 0.08 * c) * D;   // (from about 13 degrees up to 30 or 40: high in the sky)
		const int n = 900;
		// a point of the curtain: s along it (0 to 1), h up it (0 its lower edge, 1 as high as its rays reach), in story space; it folds and sways
		const auto at = [&](double s, double h) {
			const double ang = mid + (s - 0.5) * 1.8;
			const double fold = 0.10 * std::sin(s * 9.0 + t * 0.15 + c * 2.0) + 0.05 * std::sin(s * 23.0 - t * 0.35 + c) + 0.03 * std::sin(s * 47.0 + t * 0.6);
			const double r = D * (1.0 + fold);
			const Vec3 foot = centre + Vec3{r * std::sin(ang), 0.0, r * std::cos(ang)};
			const double bottomY = low * (1.0 + 0.15 * std::sin(s * 5.0 + t * 0.2)), topY = high * (1.0 + 0.2 * std::sin(s * 3.0 - t * 0.1 + c));
			return detail::story(foot + Vec3{0.0, bottomY + (topY - bottomY) * h, 0.0});
		};
		for (int i = 0; i <= n; ++i) bottom.push_back(at(i / double(n), 0.0)), top.push_back(at(i / double(n), 1.0));
		// the whole picture along it, so its faded ends are the curtain's (the lookup is clamped at the picture's edges, as pbrt's area light's: a stretch beyond them is one
		// colour); its rays move as it folds and sways
		detail::ribbon(m, bottom, top, 0.0, 1.0);
		const std::string picture = auroraPicture(film, a.colour, c);
		const double pulse = 0.85 + 0.15 * std::sin(t * 0.7 + 2.1 * c);   // (it brightens and dims)
		char scale[64];   // (the picture's path is put in as it is: a fixed buffer cut a long one off)
		std::snprintf(scale, sizeof scale, "%.5f", 0.07 * a.brightness * fade * pulse * (1.0 - 0.25 * c));
		out += "AttributeBegin\n  AreaLightSource \"diffuse\" \"string filename\" [ \"" + picture + "\" ] \"float scale\" [ " + scale + " ] \"bool twosided\" [ true ]\n";
		out += "  Material \"dielectric\" \"float eta\" [ 1 ]\n" + emitMesh(m, identity()) + "AttributeEnd\n";
	}
	return out;
}

// ---- lightning ----
// The strikes of a lightning line: their times (spread over its time, none closer than 1.5 s) and places.
inline std::vector<Strike> strikesOf(const Lightning &l, double length) {
	std::vector<Strike> out;
	const double t0 = std::max(0.0, l.t0), t1 = std::min(length, l.t1);
	for (int k = 0; k < l.count * 20 && static_cast<int>(out.size()) < l.count; ++k) {
		Strike s;
		s.seed = l.seed * 977 + k;
		s.t = t0 + (t1 - t0) * hash01(s.seed, 1);
		const double a = 2.0 * kPi * hash01(s.seed, 2), r = l.radius * std::sqrt(hash01(s.seed, 3));
		s.x = l.x + r * std::cos(a), s.z = l.z + r * std::sin(a);
		bool clear = true;
		for (const Strike &q : out) clear = clear && std::abs(q.t - s.t) > 1.5;
		if (clear) out.push_back(s);
	}
	std::sort(out.begin(), out.end(), [](const Strike &a, const Strike &b) { return a.t < b.t; });
	return out;
}
// How bright a strike is `since` seconds after it began: three or four strokes over a quarter of a second, then nothing.
inline double strokes(const Strike &s, double since) {
	if (since < 0.0 || since > 0.32) return 0.0;
	const int n = 3 + static_cast<int>(hash01(s.seed, 4) * 2.0);
	double v = 0.0;
	for (int k = 0; k < n; ++k) {
		const double at = 0.32 * k / n + 0.02 * hash01(s.seed + k, 5);
		v = std::max(v, std::exp(-std::pow((since - at) / 0.018, 2.0)) * (k == 0 ? 1.0 : 0.6 + 0.3 * hash01(s.seed + k, 6)));
	}
	return v;
}
// A bolt's path from the cloud base down to `groundY`: midpoint displacement, and branches off it (pbrt's), as segments.
inline void boltSegments(const Strike &s, double base, double groundY, std::vector<std::pair<Vec3, Vec3>> &segments, std::vector<double> &widths) {
	std::function<void(Vec3, Vec3, double, int, int)> split = [&](Vec3 a, Vec3 b, double width, int depth, int salt) {
		if (depth == 0) {
			segments.push_back({a, b});
			widths.push_back(width);
			return;
		}
		const double len = length(b - a);
		const Vec3 mid = (a + b) * 0.5 + Vec3{(hash01(salt, 1) - 0.5) * 0.35 * len, (hash01(salt, 2) - 0.5) * 0.1 * len, (hash01(salt, 3) - 0.5) * 0.35 * len};
		split(a, mid, width, depth - 1, salt * 3 + 1);
		split(mid, b, width, depth - 1, salt * 3 + 2);
		if (depth >= 3 && depth <= 6 && hash01(salt, 4) < 0.45) {   // a branch: off towards the ground, but not all the way
			const Vec3 dir = normalize(b - a + Vec3{(hash01(salt, 5) - 0.5) * len, 0.0, (hash01(salt, 6) - 0.5) * len});
			split(mid, mid + dir * (len * (0.5 + 0.4 * hash01(salt, 7))), width * 0.45, depth - 2, salt * 3 + 3);
		}
	};
	split({s.x, base, s.z}, {s.x + (hash01(s.seed, 8) - 0.5) * 300.0, groundY, s.z + (hash01(s.seed, 9) - 0.5) * 300.0}, 5.0, 9, s.seed);   // (thicker than life: a bolt kilometres off is still a line on screen)
}
// The strikes at t, as scene text: each bolt while its strokes flash (a glowing tube, and a point light at its top that lights the land and the clouds).
inline std::string lightningText(const Lightning &l, double t, const std::function<double(double, double)> &groundAt) {
	std::string out;
	for (const Strike &s : l.strikes) {
		const double flash = strokes(s, t - s.t);
		if (flash <= 0.01) continue;
		std::vector<std::pair<Vec3, Vec3>> segments;
		std::vector<double> widths;
		boltSegments(s, l.base, groundAt(s.x, s.z), segments, widths);
		Mesh m;
		for (std::size_t i = 0; i < segments.size(); ++i) {
			const Vec3 a = detail::story(segments[i].first), b = detail::story(segments[i].second);
			const Mesh tube = lathe({{widths[i], 0.0}, {widths[i], length(b - a)}}, 5);
			const Mat4 at = alignY(a, b - a);
			const int base = static_cast<int>(m.verts.size());
			for (std::size_t k = 0; k < tube.verts.size(); ++k) {
				m.verts.push_back(transformPoint(at, tube.verts[k]));
				m.normals.push_back(tube.normals[k]);
			}
			for (const auto &tr : tube.tris) m.tris.push_back({tr[0] + base, tr[1] + base, tr[2] + base});
		}
		char buf[320];
		std::snprintf(buf, sizeof buf, "# a lightning strike (film_skyfx.h)\nAttributeBegin\n  AreaLightSource \"diffuse\" \"rgb L\" [ %.3f %.3f %.3f ]\n  Material \"diffuse\" \"rgb reflectance\" [ 0 0 0 ]\n",
					  150.0 * flash, 165.0 * flash, 200.0 * flash);
		out += buf + emitMesh(m, identity()) + "AttributeEnd\n";
		std::snprintf(buf, sizeof buf, "LightSource \"point\" \"point3 from\" [ %.2f %.2f %.2f ] \"rgb I\" [ %.1f %.1f %.1f ]\n", s.x, l.base - 150.0, s.z, 2.0e7 * flash, 2.2e7 * flash, 2.6e7 * flash);
		out += buf;
	}
	return out;
}
// Thunder: each strike heard `distance / 343` seconds after it, a crack when it is near and a long rumble, quieter the further off. `distanceAt(t, x, z)`: from the camera.
inline void thunder(audio::Mix &mix, const Lightning &l, const std::function<double(double, double, double)> &distanceAt) {
	for (const Strike &s : l.strikes) {
		const double d = distanceAt(s.t, s.x, s.z), heard = s.t + d / 343.0;
		const double gain = 0.6 / (1.0 + d / 1500.0);
		const unsigned seed = static_cast<unsigned>(s.seed) * 31u + 7u;
		if (d < 2000.0) mix.put(heard, audio::noiseBurst(0.25, 0.005, 0.2, 5000.0, 2500.0, seed), gain * 0.8, 0.0, 0.3);   // (the crack)
		const double len = 3.5 + d / 1500.0;
		for (int k = 0; k < 4; ++k)   // the rumble: rolls of low noise
			mix.put(heard + 0.15 + k * len / 5.0 * (0.7 + 0.6 * audio::unit(seed + static_cast<unsigned>(k))), audio::noiseBurst(len * 0.6, 0.15, len * 0.5, 260.0, 90.0, seed + static_cast<unsigned>(k) * 3u),
					gain * (1.0 - 0.18 * k), -0.3 + 0.6 * audio::unit(seed + 11u + static_cast<unsigned>(k)), 0.6);
	}
}

// ---- meteors ----
// The meteors at t, seen from `eye` (story space), as scene text: each streaks across a stretch of the sky in under a second, a bright head and a tail that fades.
inline std::string meteorText(const Meteors &mt, double t, Vec3 eyeStory) {
	std::string out;
	for (int k = 0; k < mt.count; ++k) {
		const int s = mt.seed * 523 + k;
		const double start = mt.t0 + (std::min(mt.t1, mt.t0 + 600.0) - mt.t0) * hash01(s, 1), life = 0.4 + 0.5 * hash01(s, 2);
		const double u = (t - start) / life;
		if (u < 0.0 || u > 1.0) continue;
		// a stretch of a great circle across the upper sky, 30 km off
		const double az = 2.0 * kPi * hash01(s, 3), el = (25.0 + 50.0 * hash01(s, 4)) * kPi / 180.0, dir = 2.0 * kPi * hash01(s, 5), span = (8.0 + 14.0 * hash01(s, 6)) * kPi / 180.0;
		const auto onSky = [&](double f) {
			const double a = az + std::cos(dir) * span * f, e = el - std::abs(std::sin(dir)) * span * f;
			return eyeStory + Vec3{std::cos(e) * std::sin(a), std::sin(e), std::cos(e) * std::cos(a)} * 30000.0;
		};
		const Vec3 head = onSky(u), tail = onSky(std::max(0.0, u - 0.35));
		const Vec3 side = normalize(cross(head - tail, head - eyeStory)) * 25.0;
		Mesh m;
		for (const Vec3 &p : {tail - side * 0.3, tail + side * 0.3, head + side, head - side}) {
			m.verts.push_back(p);
			m.normals.push_back(normalize(eyeStory - head));
		}
		m.tris.push_back({0, 1, 2});
		m.tris.push_back({0, 2, 3});
		const double glow = std::sin(kPi * u) * (0.6 + 0.8 * hash01(s, 7));
		char buf[200];
		std::snprintf(buf, sizeof buf, "AttributeBegin\n  AreaLightSource \"diffuse\" \"rgb L\" [ %.3f %.3f %.3f ] \"bool twosided\" [ true ]\n  Material \"diffusetransmission\" \"rgb reflectance\" [ 0 0 0 ] \"rgb transmittance\" [ 1 1 1 ]\n", 1.4 * glow,
					  1.5 * glow, 1.7 * glow);
		out += "# a meteor (film_skyfx.h)\n" + std::string(buf) + emitMesh(m, identity()) + "AttributeEnd\n";
	}
	return out;
}

}  // namespace skyfx
}  // namespace cartoon
