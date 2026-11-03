#pragma once
// film_seatoon.h -- under the sea, round 18: a cartoon's drawn looks and glows.
//
//   bubblewipe at T                      a wall of bubbles rushing up across the whole picture, hiding the cut at T (from 0.7 s before to 0.7 s after)
//   waterwobble during T0 T1 [amount A]  the whole picture wobbling as if seen through moving water - a dream, a memory, a daze (A 0 to 1: 0.5)
//   drawnrays [count N] [bright B]       rays of light drawn slanting down from the surface round the camera, swaying, brighter near the top (N 7)
//   speedlines WHO [during T0 T1]        white lines streaming behind a character when it swims fast
//   gulp WHO at T                        a comic gulp: a big bubble out of its mouth, wobbling up, and a gulp you hear
//   seasparkle [amount A] [colour R G B] the water lighting up in sparks round anything moving in it - characters, subs, a serpent - as plankton do in the dark
//
// The drawn things (rays, lines) are grouped with the bubbles (a toon film draws no ink round them: film_undersea.h bubblesGrouped).

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

#include "film_deepsea.h"
#include "film_flow.h"
#include "film_sealife.h"
#include "film_sky.h"
#include "film_undersea.h"
#include "sdf_mesh.h"

namespace cartoon {
namespace seatoon {

struct Wobble {
	double t0 = 0.0, t1 = 0.0, amount = 0.5;
};
struct Rays {
	bool on = false;
	int count = 7;
	double bright = 1.0;
};
struct Lines {
	std::string who;
	double t0 = -1e30, t1 = 1e30;
};
struct Gulp {
	std::string who;
	double t = 0.0;
};
struct Sparkle {
	bool on = false;
	double amount = 1.0;
	Vec3 colour{0.35, 0.85, 1.0};
};
struct World {
	std::vector<double> wipes;
	std::vector<Wobble> wobbles;
	Rays rays;
	std::vector<Lines> lines;
	std::vector<Gulp> gulps;
	Sparkle sparkle;
	bool any() const { return !wipes.empty() || !wobbles.empty() || rays.on || !lines.empty() || !gulps.empty() || sparkle.on; }
};

// A glowing thing's material (lighting nothing, casting no shadow), as the bubbles' group draws it.
inline std::string glow(Vec3 L) {
	char buf[220];
	std::snprintf(buf, sizeof buf, "AreaLightSource \"diffuse\" \"rgb L\" [ %.4f %.4f %.4f ] \"bool twosided\" true \"bool sampled\" false\n  Material \"diffuse\" \"rgb reflectance\" [ 0 0 0 ]", L.x, L.y, L.z);
	return buf;
}
// Text with the bubbles' heading, so a toon film draws no ink round it.
inline std::string asBubbles(const std::string &body) { return body.empty() ? body : "# bubbles (film_undersea.h)\nAttributeBegin\n" + body + "AttributeEnd\n"; }

// The bubble wipe at t (pbrt's eye and target; fov the shorter side's, the frame 16:9): bubbles of a wall rushing up through the frame just in front of the lens.
inline void wipeBubbles(double at, double t, const Vec3 &eye, const Vec3 &target, double fov, std::vector<undersea::Bubble> &out) {
	const double u = (t - at) / 0.7;   // (-1 .. 1 across the wipe)
	if (u < -1.0 || u > 1.0) return;
	const Vec3 f = normalize(target - eye), right = normalize(cross(cameraUp(f), f)), up = cross(f, right);   // (the film's own up, as its LookAt's)
	const double d = 0.6, H = d * std::tan(0.5 * fov * kPi / 180.0), W = H * 16.0 / 9.0;
	for (int i = 0; i < 900; ++i) {
		const double h1 = hash01(i, 71), h2 = hash01(i, 72), h3 = hash01(i, 73);
		const double y = (h2 * 2.0 - 1.0) * 1.6 * H + 3.4 * H * u;   // (the wall: a band of them, carried up through the frame)
		if (std::abs(y) > 1.3 * H) continue;
		undersea::Bubble b;
		b.p = eye + f * (d * (0.75 + 0.6 * h3)) + right * ((h1 * 2.0 - 1.0) * 1.15 * W + 0.02 * std::sin(9.0 * t + i)) + up * y;
		b.r = d * (0.012 + 0.05 * std::pow(hash01(i, 74), 2.5));
		b.phase = 7.0 * t + i;
		out.push_back(b);
	}
}

// A wipe's bubbles as round balls (a real bubble that big is a flattened cap: a cartoon's wipe is round ones), drawn as the bubbles are (no ink in a toon film).
inline std::string roundBubbles(const std::vector<undersea::Bubble> &bubbles) {
	if (bubbles.empty()) return std::string();
	std::string body = "  Material \"dielectric\" \"float eta\" [ 0.7502 ]\n";
	char buf[200];
	for (const undersea::Bubble &b : bubbles) {
		std::snprintf(buf, sizeof buf, "  AttributeBegin\n    Translate %.4f %.4f %.4f\n    Shape \"sphere\" \"float radius\" [ %.5f ] \"bool shadow\" false\n  AttributeEnd\n", b.p.x, b.p.y, b.p.z, b.r);
		body += buf;
	}
	return asBubbles(body);
}

// The look's words for a wobble at t ("" for none).
inline std::string wobbleLook(const std::vector<Wobble> &w, double t) {
	for (const Wobble &x : w) {
		const double a = smoothstep(x.t0, x.t0 + 0.6, t) * (1.0 - smoothstep(x.t1 - 0.6, x.t1, t));
		if (a <= 0.0) continue;
		char buf[80];
		std::snprintf(buf, sizeof buf, " shimmer %.4f shimmer-time %.3f", 0.05 * std::clamp(x.amount, 0.0, 1.0) * a, t);
		return buf;
	}
	return std::string();
}

// Drawn rays of light round the pbrt eye: a light just under the surface (over it, the surface would stop it) `level` throwing a picture of N soft spots down into the water, tilted a little towards the sun
// (facing, elevation in degrees) and swaying. The water itself draws each spot's beam (a shape glowing in the water would hide what is behind it).
inline std::string raysText(const std::string &film, const Rays &r, double t, const Vec3 &eye, double level, double facing, double elevation) {
	if (!r.on || eye.y > level) return std::string();
	char name[48];
	std::snprintf(name, sizeof name, "drawnrays_%d", r.count);
	const int size = 256;
	const std::string picture = film_sky::picture(film, name, size, size, [&](std::vector<float> &rgb) {
		for (int j = 0; j < size; ++j)
			for (int i = 0; i < size; ++i) {
				double v = 0.0;
				for (int k = 0; k < r.count; ++k) {   // (each spot a soft disc, its edge a little brighter: a drawn beam)
					const double cx = 0.12 + 0.76 * hash01(k, 91), cy = 0.12 + 0.76 * hash01(k, 92), rr = 0.025 + 0.035 * hash01(k, 93);
					const double d = std::hypot((i + 0.5) / size - cx, (j + 0.5) / size - cy) / rr;
					v += d < 1.0 ? 0.75 + 0.25 * d * d : std::exp(-12.0 * (d - 1.0) * (d - 1.0)) * 0.6;
				}
				const std::size_t q = (static_cast<std::size_t>(j) * size + i) * 3;
				rgb[q] = static_cast<float>(0.85 * v), rgb[q + 1] = static_cast<float>(0.97 * v), rgb[q + 2] = static_cast<float>(v);
			}
	});
	const double tilt = std::clamp(90.0 - elevation, 0.0, 30.0), sway = 4.0 * std::sin(0.25 * t);
	char buf[360];
	std::snprintf(buf, sizeof buf, "# drawn rays (film_seatoon.h)\nAttributeBegin\n  Translate %.3f %.3f %.3f\n  Rotate %.2f 0 1 0\n  Rotate %.2f 1 0 0\n  Rotate %.2f 0 0 1\n", eye.x, level - 0.3, eye.z, 180.0 - facing,
				  90.0 - tilt, sway);
	char light[120];
	std::snprintf(light, sizeof light, "\" ] \"float scale\" [ %.3f ]\nAttributeEnd\n", 300.0 * r.bright);   // (a projection's light falls off with the square of the distance: the bed is 10 to 20 m down
	return buf + std::string("  LightSource \"projection\" \"float fov\" [ 60 ] \"string filename\" [ \"") + picture + light;
}

// Speed lines along a path (story space points, newest first, `speed` m/s now), round a body `size` across.
inline std::string speedLinesText(const std::vector<Vec3> &path, double speed, double size, int seed) {
	if (path.size() < 3 || speed < 0.9) return std::string();
	const double strength = smoothstep(0.9, 2.5, speed);
	const Vec3 way = normalize(path[0] - path[1] + Vec3{0.0, 1e-9, 0.0});
	Vec3 a = cross(way, Vec3{0.0, 1.0, 0.0});
	if (length(a) < 1e-3) a = {1.0, 0.0, 0.0};
	a = normalize(a);
	const Vec3 b = cross(way, a);
	Mesh lines;
	for (int k = 0; k < 9; ++k) {
		const double ang = 2.0 * kPi * (k + 0.3 * hash01(k, seed)) / 9.0, r = 0.5 * size * (0.9 + 0.5 * hash01(k, seed + 1));
		const std::size_t start = 1 + static_cast<std::size_t>(hash01(k, seed + 2) * 2.0), n = std::min(path.size(), start + 3 + static_cast<std::size_t>(5.0 * strength));
		std::vector<Vec3> line;
		for (std::size_t i = start; i < n; ++i) line.push_back(path[i] + a * (r * std::cos(ang)) + b * (r * std::sin(ang)));
		if (line.size() >= 2) deepsea::detail::add(lines, sdf::tubeMesh(line, 0.006 * size, 0.2, 4));
	}
	return asBubbles("  AttributeBegin\n  " + glow(Vec3{1.4, 1.5, 1.6} * strength) + "\n" + undersea::detail::noShadow(emitMesh(lines, identity())) + "  AttributeEnd\n");
}

// Sparks round movers: each mover's places over the last two seconds (pbrt's, newest first, every 0.1 s), sparks born at each place fading as they age.
inline std::string sparkleText(const Sparkle &s, const std::vector<std::vector<Vec3>> &movers, double t) {
	if (!s.on || movers.empty()) return std::string();
	Mesh bins[4];
	const Mesh speck = uvSphere(1.0, 4, 3);
	for (std::size_t m = 0; m < movers.size(); ++m) {
		const std::vector<Vec3> &path = movers[m];
		for (std::size_t i = 0; i + 1 < path.size(); ++i) {
			const double moved = length(path[i] - path[i + 1]);
			if (moved < 0.02) continue;   // (only what moves stirs them)
			const int n = static_cast<int>(std::min(60.0, 220.0 * moved * s.amount));
			const int bin = std::min(3, static_cast<int>(i / 5));
			for (int k = 0; k < n; ++k) {
				const int key = static_cast<int>(std::floor(t * 10.0) - i) * 131 + k + static_cast<int>(m) * 7919;
				const Vec3 p = lerp(path[i], path[i + 1], hash01(key, 1)) + Vec3{0.6 * (hash01(key, 2) - 0.5), 0.6 * (hash01(key, 3) - 0.5), 0.6 * (hash01(key, 4) - 0.5)};
				const double r = 0.004 + 0.004 * hash01(key, 5);
				const int base = static_cast<int>(bins[bin].verts.size());
				for (std::size_t q = 0; q < speck.verts.size(); ++q) bins[bin].verts.push_back(sealife::detail::story(p) + speck.verts[q] * r), bins[bin].normals.push_back(speck.normals[q]);
				for (const auto &tr : speck.tris) bins[bin].tris.push_back({tr[0] + base, tr[1] + base, tr[2] + base});
			}
		}
	}
	std::string body;
	const double fade[4] = {3.0, 1.6, 0.8, 0.35};
	for (int b = 0; b < 4; ++b)
		if (!bins[b].tris.empty()) body += "  AttributeBegin\n  " + glow(s.colour * (fade[b] * s.amount)) + "\n" + undersea::detail::noShadow(emitMesh(bins[b], identity())) + "  AttributeEnd\n";
	return asBubbles(body);
}

}  // namespace seatoon
}  // namespace cartoon
