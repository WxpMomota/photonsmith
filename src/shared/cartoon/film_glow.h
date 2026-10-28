#pragma once
// film_glow.h -- living light on a night for a film file (film_file.h `bioluminescence`, `fireflies`, `fungi`):
//
//   bioluminescence  the sea glowing where it is stirred: the foam on breaking crests, the wash along the shore, the white water of the surf and the spray and foam of
//                    whales and dolphins give out a blue light (the sea's own foam materials, made emitters: film_water.h, film_giants.h);
//   fireflies        a swarm over a patch of land: each wandering slowly a little above the ground and flashing a yellow-green light every few seconds - each in its own
//                    time, or (`together`) drifting into step until the whole swarm flashes as one, a wave of light;
//   fungi            clusters of small mushrooms on the ground whose caps glow green (foxfire), lighting the ground round them.
//
// Every glow is a real light (an emitting sphere or mesh), so it lights what is near it. Coordinates are pbrt's, as a film file's; the meshes are written in them.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "cartoon_lib.h"
#include "nature_lib.h"

namespace cartoon {
namespace glow {

// The sea's glow: how strong, and its colour (blue, green or teal).
struct Bioluminescence {
	bool on = false;
	double strength = 1.0;
	std::string colour = "blue";
};
inline const std::vector<std::string> &glowColours() {
	static const std::vector<std::string> k = {"blue", "green", "teal"};
	return k;
}
inline Vec3 colourOf(const std::string &c) { return c == "green" ? Vec3{0.25, 1.0, 0.35} : c == "teal" ? Vec3{0.1, 0.85, 0.75} : Vec3{0.12, 0.45, 1.0}; }

// A swarm of fireflies: how many, over which patch (pbrt's x z), how high they fly (metres above the ground, at most), when, whether they flash together, how bright.
struct Fireflies {
	int count = 80;
	double x0 = -10.0, z0 = -10.0, x1 = 10.0, z1 = 10.0, height = 2.0, t0 = 0.0, t1 = 1e30, brightness = 1.0;
	bool together = false;
	int seed = 1;
};
// Glowing fungi: so many clusters over a patch, their colour, how bright.
struct Fungi {
	int count = 30;
	double x0 = -10.0, z0 = -10.0, x1 = 10.0, z1 = 10.0, brightness = 1.0;
	std::string colour = "green";
	int seed = 1;
};

// ---- fireflies ----
// How bright firefly k of a swarm is at t (0 dark, 1 its flash's peak): a flash of a third of a second every 2 to 4 s; `together`, the flashes drift into step over
// half a minute until all flash at once.
inline double flash(const Fireflies &f, int k, double t) {
	const double period = 2.0 + 2.0 * hash01(f.seed * 211 + k, 1), own = period * hash01(f.seed * 211 + k, 2);
	double phase = std::fmod(t + own, period) / period;
	if (f.together) {   // the shared beat: 2.5 s, each one's own phase pulled towards it
		const double pull = smoothstep(0.0, 30.0, t - f.t0), common = 2.5;
		const double mine = std::fmod(t + own, period) / period, shared = std::fmod(t, common) / common;
		double d = shared - mine;
		d -= std::round(d);
		phase = mine + pull * d;
		phase -= std::floor(phase);
	}
	const double width = 0.33 / (f.together ? 2.5 : period);
	return phase < width ? std::sin(kPi * phase / width) : 0.0;
}
// Where firefly k is at t (pbrt's): wandering slowly over its patch, rising and dipping, above the ground `groundAt`.
inline Vec3 fireflyAt(const Fireflies &f, int k, double t, const std::function<double(double, double)> &groundAt) {
	const int s = f.seed * 211 + k;
	const double hx = hash01(s, 3), hz = hash01(s, 4), sp = 0.08 + 0.06 * hash01(s, 5);
	const double x = f.x0 + (f.x1 - f.x0) * clamp01(hx + 0.18 * (nature::noise2(t * sp, k * 1.7, 31) - 0.5) * 2.0);
	const double z = f.z0 + (f.z1 - f.z0) * clamp01(hz + 0.18 * (nature::noise2(k * 1.3, t * sp, 37) - 0.5) * 2.0);
	const double y = groundAt(x, z) + 0.2 + (f.height - 0.2) * clamp01(0.3 + 0.7 * nature::noise2(t * 0.3 + k, k * 0.7, 41));
	return {x, y, z};
}
// The swarm at t, as scene text: each firefly a tiny glowing sphere while it flashes (a dim one between: the glow of its lantern never quite goes out). `light`: what the
// frame's exposure lets through (1 by day, less as the night camera opens up), so a flash looks the same.
inline std::string firefliesText(const Fireflies &f, double t, const std::function<double(double, double)> &groundAt, double light = 1.0) {
	if (t < f.t0 || t > f.t1) return std::string();
	const double in = smoothstep(f.t0, f.t0 + 2.0, t) * (f.t1 >= 1e29 ? 1.0 : 1.0 - smoothstep(f.t1 - 2.0, f.t1, t));
	std::string out;
	char buf[260];
	for (int k = 0; k < f.count; ++k) {
		const double g = (0.02 + flash(f, k, t)) * in * f.brightness * 40.0 * light;
		if (g < 1e-4) continue;
		const Vec3 p = fireflyAt(f, k, t, groundAt);
		std::snprintf(buf, sizeof buf, "AttributeBegin\n  Translate %.4f %.4f %.4f\n  AreaLightSource \"diffuse\" \"rgb L\" [ %.3f %.3f %.3f ]\n  Shape \"sphere\" \"float radius\" [ 0.012 ]\nAttributeEnd\n", p.x,
					  p.y, p.z, 0.75 * g, g, 0.2 * g);
		out += buf;
	}
	return out.empty() ? out : "# fireflies (film_glow.h)\nAttributeBegin\n  Material \"diffuse\" \"rgb reflectance\" [ 0 0 0 ]\n" + out + "AttributeEnd\n";
}

// ---- fungi ----
// The clusters of glowing fungi, as scene text: each a few to a dozen mushrooms on the ground - pale stems, glowing round caps 2 to 6 cm across - breathing slowly brighter
// and dimmer (`light`: as fireflies').
inline std::string fungiText(const Fungi &f, double t, const std::function<double(double, double)> &groundAt, double light = 1.0) {
	const Vec3 c = colourOf(f.colour);
	std::string caps, stems;
	char buf[260];
	for (int k = 0; k < f.count; ++k) {
		const int s = f.seed * 307 + k;
		const double cx = f.x0 + (f.x1 - f.x0) * hash01(s, 1), cz = f.z0 + (f.z1 - f.z0) * hash01(s, 2);
		const int n = 3 + static_cast<int>(9.0 * hash01(s, 3));
		const double breathe = 0.75 + 0.25 * std::sin(0.4 * t + 6.28 * hash01(s, 4));
		for (int i = 0; i < n; ++i) {
			const double a = 6.28318 * hash01(s * 13 + i, 5), r = 0.18 * std::sqrt(hash01(s * 13 + i, 6));
			const double x = cx + r * std::cos(a), z = cz + r * std::sin(a), g = groundAt(x, z);
			const double cap = 0.01 + 0.02 * hash01(s * 13 + i, 7), tall = 0.03 + 0.08 * hash01(s * 13 + i, 8);
			const double L = 1.2 * f.brightness * breathe * light;
			std::snprintf(buf, sizeof buf, "AttributeBegin\n  Translate %.4f %.4f %.4f\n  AreaLightSource \"diffuse\" \"rgb L\" [ %.3f %.3f %.3f ]\n  Shape \"sphere\" \"float radius\" [ %.4f ]\nAttributeEnd\n", x,
						  g + tall + 0.4 * cap, z, L * c.x, L * c.y, L * c.z, cap);
			caps += buf;
			stems += bar({-x, g - 0.01, z}, {-x, g + tall, z}, 0.12 * cap + 0.002, 6);
		}
	}
	if (caps.empty()) return std::string();
	return "# glowing fungi (film_glow.h)\nAttributeBegin\n  Material \"diffuse\" \"rgb reflectance\" [ 0 0 0 ]\n" + caps + "AttributeEnd\n" + attr(diffuseMaterial({0.55, 0.55, 0.5}), stems);
}

}  // namespace glow
}  // namespace cartoon
