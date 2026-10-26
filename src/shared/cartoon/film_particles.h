#pragma once
// film_particles.h -- weather and particles for film files (film_file.h `weather`, `particles`, `burst`): rain, snow and falling leaves over the set, and sparks, dust and splashes
// thrown from a place. Every particle is a fixed function of time - where it was born and how it was thrown come from its number - so a frame is still a function of its
// time alone and frames render in any order.
//
// Weather fills tiles of the world 10 m square (the same pattern in each, so it stands still in the world) round the camera, out to 14 m: as much of it as is seen, at the
// density it should have, however far the set reaches. Rain is drawn as thin light streaks the length a drop falls while a film camera's shutter is open, turned to face the
// camera; snow as little balls (a card's shadow in a low sun is a dash); leaves tumble as they fall. Coordinates are pbrt's (as a film file's); the scene text is written in them.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "cartoon_audio.h"
#include "cartoon_lib.h"

namespace cartoon {
namespace particles {

// Weather over the set: its kind (rain, snow, leaves), how much (1 a good shower), from when to when (it eases in and out over 1.5 s), the ground's height.
struct Weather {
	std::string kind;
	double amount = 1.0, t0 = 0.0, t1 = 1e30, ground = 0.0;
	bool onLand = false;   // (its ground the land's under the camera: the leaves a forest drops)
};
// Particles thrown from a place: sparks, dust or a splash, from t0 to t1 at `rate` a second (a burst: all at t0), at a speed.
struct Emitter {
	std::string kind;
	Vec3 at;
	double t0 = 0.0, t1 = 0.0, rate = 60.0, speed = 0.0;
	int count = 0;   // (a burst) how many
};
inline const std::vector<std::string> &weatherKinds() {
	static const std::vector<std::string> k = {"rain", "snow", "leaves"};
	return k;
}
inline const std::vector<std::string> &emitterKinds() {
	static const std::vector<std::string> k = {"sparks", "dust", "splash"};
	return k;
}

namespace detail {
inline double u01(int a, int b) { return hash01(a, b); }
inline void num(std::string &out, double v) {
	char buf[24];
	std::snprintf(buf, sizeof buf, "%.4f ", v);
	out += buf;
}
// A mesh being written: its points and its triangles' corners.
struct MeshText {
	std::string points, indices;
	int n = 0;
	void quad(Vec3 a, Vec3 b, Vec3 c, Vec3 d) {
		for (const Vec3 &p : {a, b, c, d}) num(points, p.x), num(points, p.y), num(points, p.z);
		char buf[64];
		std::snprintf(buf, sizeof buf, "%d %d %d %d %d %d ", n, n + 1, n + 2, n, n + 2, n + 3);
		indices += buf;
		n += 4;
	}
	std::string text(const std::string &material) const {
		if (n == 0) return std::string();
		return "AttributeBegin\n  " + material + "\n  Shape \"trianglemesh\" \"integer indices\" [ " + indices + "] \"point3 P\" [ " + points + "]\nAttributeEnd\n";
	}
};
// The right and up of a little card at p facing the eye (up is the world's up, or `along` for a streak).
inline void facing(Vec3 p, Vec3 eye, Vec3 along, Vec3 &right, Vec3 &up) {
	const Vec3 toEye = eye - p;
	up = normalize(along);
	right = cross(up, toEye);
	if (length(right) < 1e-9) right = {1.0, 0.0, 0.0};
	right = normalize(right);
}
}  // namespace detail

// How far the weather is in at t (eased in and out over 1.5 s), 0..1; its amount says how much of it there is then.
inline double strength(const Weather &w, double t) {
	if (t < w.t0 || t > w.t1) return 0.0;
	return smoothstep(w.t0, w.t0 + 1.5, t) * (w.t1 >= 1e29 ? 1.0 : 1.0 - smoothstep(w.t1 - 1.5, w.t1, t));
}

// The weather at t round the camera at `eye`, as scene text.
inline std::string weatherText(const Weather &w, double t, Vec3 eye, double shutterSeconds) {
	const double s = strength(w, t);
	if (s <= 0.0) return std::string();
	constexpr double kTile = 10.0, kReach = 14.0, kTop = 9.0;
	const int perTile = w.kind == "rain" ? 1400 : w.kind == "snow" ? 1300 : 60;
	const int shown = static_cast<int>(perTile * s * std::max(0.0, w.amount));
	detail::MeshText mesh, splashes;
	std::string flakes;
	const int i0 = static_cast<int>(std::floor((eye.x - kReach) / kTile)), i1 = static_cast<int>(std::floor((eye.x + kReach) / kTile));
	const int k0 = static_cast<int>(std::floor((eye.z - kReach) / kTile)), k1 = static_cast<int>(std::floor((eye.z + kReach) / kTile));
	for (int ti = i0; ti <= i1; ++ti)
		for (int tk = k0; tk <= k1; ++tk)
			for (int n = 0; n < shown; ++n) {
				// its place in the tile and its fall: the same in every tile (the pattern stands still in the world)
				const double x = (ti + detail::u01(n, 1)) * kTile, z = (tk + detail::u01(n, 2)) * kTile;
				const double speed = w.kind == "rain" ? 7.0 + 2.0 * detail::u01(n, 3) : w.kind == "snow" ? 0.8 + 0.5 * detail::u01(n, 3) : 0.7 + 0.4 * detail::u01(n, 3);
				const double fall = std::fmod(speed * t + kTop * detail::u01(n, 4), kTop);
				Vec3 p{x, w.ground + kTop - fall, z};
				if (std::hypot(p.x - eye.x, p.z - eye.z) > kReach) continue;
				if (w.kind == "rain") {   // a streak as long as its fall while the shutter is open, a little slanted by the wind
					const Vec3 along = normalize(Vec3{0.08, -1.0, 0.03});
					const double len = std::max(0.08, speed * shutterSeconds);
					Vec3 right, up;
					detail::facing(p, eye, along, right, up);
					const Vec3 r = right * 0.0028;   // (thin: its shadow in a low sun is a faint line)
					mesh.quad(p - r, p + r, p + r + along * len, p - r + along * len);
					// where it lands: a little splash for a moment
					if (fall > kTop - speed * 0.06) {
						const Vec3 g{x, w.ground + 0.01, z};
						splashes.quad(g + Vec3{-0.012, 0.0, 0.0}, g + Vec3{0.0, 0.0, -0.012}, g + Vec3{0.012, 0.0, 0.0}, g + Vec3{0.0, 0.0, 0.012});
					}
				} else if (w.kind == "snow") {   // a flake drifting as it falls
					p.x += 0.25 * std::sin(1.3 * t + 6.28 * detail::u01(n, 5));
					p.z += 0.25 * std::cos(1.1 * t + 6.28 * detail::u01(n, 6));
					char buf[160];   // (a little ball: its shadow in a low sun is a dot, not a dash)
					std::snprintf(buf, sizeof buf, "AttributeBegin\n  Translate %.4f %.4f %.4f\n  Shape \"sphere\" \"float radius\" [ %.4f ]\nAttributeEnd\n", p.x, p.y, p.z, 0.010 + 0.007 * detail::u01(n, 7));
					flakes += buf;
				} else {   // a leaf tumbling down, swinging from side to side
					const double ph = 6.28 * detail::u01(n, 5);
					p.x += 0.6 * std::sin(0.9 * t + ph);
					const double a = 2.5 * t + ph, b = 1.7 * t + 2.0 * ph;
					const Vec3 e1 = normalize(Vec3{std::cos(a), 0.4 * std::sin(b), std::sin(a)}), e2 = normalize(cross(e1, Vec3{std::sin(b), 1.0, std::cos(b)}));
					const double r = 0.035;
					mesh.quad(p - e1 * r - e2 * r * 0.6, p + e1 * r - e2 * r * 0.6, p + e1 * r + e2 * r * 0.6, p - e1 * r + e2 * r * 0.6);
				}
			}
	if (w.kind == "rain")
		return mesh.text("Material \"diffuse\" \"rgb reflectance\" [ 0.8 0.83 0.88 ]") + splashes.text("Material \"diffuse\" \"rgb reflectance\" [ 0.6 0.63 0.68 ]");
	if (w.kind == "snow") return flakes.empty() ? std::string() : "AttributeBegin\n  Material \"diffuse\" \"rgb reflectance\" [ 0.92 0.93 0.95 ]\n" + flakes + "AttributeEnd\n";
	return mesh.text("Material \"diffuse\" \"rgb reflectance\" [ 0.55 0.28 0.07 ]");   // (autumn leaves)
}

// The particles an emitter has in the air at t, as scene text: sparks glow and fall, dust rises and settles, a splash's drops arc and fall back.
inline std::string emitterText(const Emitter &e, double t) {
	const bool burst = e.count > 0;
	const double life = e.kind == "sparks" ? 1.0 : e.kind == "dust" ? 2.2 : 0.9;
	const int total = burst ? e.count : static_cast<int>(std::max(0.0, (e.t1 - e.t0) * e.rate));
	const double speed = e.speed > 0.0 ? e.speed : e.kind == "sparks" ? 3.0 : e.kind == "dust" ? 0.5 : 2.2;
	std::string out;
	detail::MeshText dust;
	for (int n = 0; n < total; ++n) {
		const double born = burst ? e.t0 : e.t0 + n / e.rate;
		const double age = t - born, mine = life * (0.6 + 0.6 * detail::u01(n, 11));
		if (age < 0.0 || age > mine) continue;
		// thrown upward and out, at a speed of its own
		const double a = 6.28318 * detail::u01(n, 12), up = e.kind == "dust" ? 0.3 + 0.4 * detail::u01(n, 13) : 0.5 + 0.5 * detail::u01(n, 13);
		const double v = speed * (0.5 + 0.7 * detail::u01(n, 14));
		const Vec3 vel{v * std::cos(a) * (1.0 - up * 0.6), v * up, v * std::sin(a) * (1.0 - up * 0.6)};
		const double g = e.kind == "dust" ? 0.15 : 9.8, drag = e.kind == "dust" ? 1.6 : 0.4;
		const double k = (1.0 - std::exp(-drag * age)) / drag;   // (velocity damped by drag: the distance it has gone)
		Vec3 p = e.at + vel * k + Vec3{0.0, -0.5 * g * age * age, 0.0};
		p.y = std::max(p.y, e.at.y - 0.02);
		const double fade = 1.0 - age / mine;
		char buf[300];
		if (e.kind == "sparks") {
			std::snprintf(buf, sizeof buf, "AttributeBegin\n  Translate %.4f %.4f %.4f\n  AreaLightSource \"diffuse\" \"rgb L\" [ %.3f %.3f %.3f ]\n  Shape \"sphere\" \"float radius\" [ 0.008 ]\nAttributeEnd\n", p.x, p.y, p.z,
						  60.0 * fade, 26.0 * fade * fade, 6.0 * fade * fade * fade);
			out += buf;
		} else if (e.kind == "splash") {
			std::snprintf(buf, sizeof buf, "AttributeBegin\n  Translate %.4f %.4f %.4f\n  Shape \"sphere\" \"float radius\" [ %.4f ]\nAttributeEnd\n", p.x, p.y, p.z, 0.006 + 0.008 * detail::u01(n, 15));
			out += buf;
		} else {   // dust: little specks that grow as they spread
			const double r = (0.006 + 0.01 * age) * (0.5 + detail::u01(n, 16));
			dust.quad(p + Vec3{-r, 0.0, 0.0}, p + Vec3{0.0, 0.0, -r}, p + Vec3{r, 0.0, 0.0}, p + Vec3{0.0, r * 1.4, 0.0});
		}
	}
	if (e.kind == "splash" && !out.empty()) out = "AttributeBegin\n  Material \"dielectric\" \"float eta\" [ 1.33 ]\n" + out + "AttributeEnd\n";
	if (e.kind == "dust") out += dust.text("Material \"diffuse\" \"rgb reflectance\" [ 0.52 0.45 0.36 ]");
	return out;
}

// The sound of rain: a steady hiss of drops, louder with more rain, eased in and out with it.
inline void rainSound(audio::Mix &m, const Weather &w) {
	for (double s = std::max(0.0, w.t0); s < std::min(m.seconds(), w.t1); s += 0.8) {
		const double g = strength(w, s + 0.4) * w.amount;
		if (g <= 0.0) continue;
		m.put(s, audio::noiseBurst(1.6, 0.4, 0.4, 2500.0, 6000.0, 71u + static_cast<unsigned>(s * 10.0)), 0.10 * g, std::sin(s * 0.7) * 0.4, 0.3);
	}
}

}  // namespace particles
}  // namespace cartoon
