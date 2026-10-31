#pragma once
// film_swarm.h -- `swarm`: thousands of little glowing things as one film line. Fireflies that stream in from the dark, orbit a point and dive into it; a fountain of sparks; a
// trail of dust behind something moving. Like film_particles.h every particle is a fixed function of its number and the time, so a frame is a function of its time alone and
// frames render in any order. All of them go into a few meshes of little discs turned to the camera, one to a level of brightness, marked `"bool sampled" false` (a glow the
// camera sees, no light to aim at), so ten thousand cost no more to sample than none; `glow K` adds the one real light they cast, at their middle, as bright as there are of them.
//
//   swarm NAME COUNT from X Y Z [radius R] during T0 T1                 born one by one between those times, within R of the point (or `after BLOCK`: where that block is then)
//         [to X Y Z [radius R] [arrive S] [orbit W]]                    each flying there in S seconds on a rising arc, then circling it at W radians a second
//         [into X Y Z at T0 T1 [keep K]] [settle R]                     from then each diving into the point, gone; the first K stay and drift off to hover within R of `to`
//         [velocity X Y Z] [spread S] [gravity G] [life L]              (no `to`) thrown at that velocity, scattered by S, falling by G, gone after L seconds
//         [colour R G B] [bright B] [size S] [pulse HZ] [glow K] [seed N]
//
// Coordinates are pbrt's, as a film file's.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

#include "cartoon_lib.h"

namespace cartoon {
namespace swarm {

struct Swarm {
	std::string name, after;   // (`after`: the block or group it streams from)
	int count = 100, keep = 0, seed = 1;
	Vec3 from, to, into, velocity, colour{1.0, 0.77, 0.15};
	double fromRadius = 0.0, toRadius = 0.0, settle = 0.0, t0 = 0.0, t1 = 0.0, arrive = 3.0, orbit = 0.0, into0 = 0.0, into1 = 0.0, spread = 0.0, gravity = 0.0, life = 0.0;
	double bright = 6.0, size = 0.06, pulse = 0.0, glow = 0.0;
	bool hasTo = false, hasInto = false;
};
constexpr int kLevels = 5;   // (how many levels of brightness: one mesh each)

namespace detail {
inline double rnd(const Swarm &s, int k, int salt) { return hash01(k * 2039 + s.seed * 7919, salt); }
inline Vec3 inBall(const Swarm &s, int k, int salt) {
	const double a = 6.2831853 * rnd(s, k, salt), z = 2.0 * rnd(s, k, salt + 1) - 1.0, r = std::cbrt(rnd(s, k, salt + 2)), c = std::sqrt(std::max(0.0, 1.0 - z * z));
	return Vec3{r * c * std::cos(a), r * z, r * c * std::sin(a)};
}
inline Vec3 orbitAt(const Swarm &s, int k, double t) {
	const double r = s.toRadius * (0.45 + 0.55 * rnd(s, k, 7)), a = 6.2831853 * rnd(s, k, 8) + s.orbit * (0.7 + 0.6 * rnd(s, k, 9)) * t * (rnd(s, k, 10) < 0.8 ? 1.0 : -1.0);
	const double tighten = s.hasInto ? 1.0 - 0.55 * smoothstep(s.into0 - 2.5, s.into0, t) : 1.0;
	return s.to + Vec3{r * tighten * std::cos(a), s.toRadius * 0.4 * (rnd(s, k, 11) - 0.5) + 0.2 * std::sin(1.7 * t + 6.28 * rnd(s, k, 12)), r * tighten * std::sin(a)};
}
inline Vec3 bezier(Vec3 a, Vec3 b, Vec3 c, double u) { return a * ((1.0 - u) * (1.0 - u)) + b * (2.0 * u * (1.0 - u)) + c * (u * u); }
}  // namespace detail

// Particle k at t: false when it is not there; else where it is and how bright (0..1). `origin(T)`: where the block it streams from is at T.
inline bool at(const Swarm &s, int k, double t, const std::function<Vec3(double)> &origin, Vec3 &p, double &b) {
	const double born = s.t0 + (s.t1 - s.t0) * (s.count > 1 ? (k + detail::rnd(s, k, 1)) / s.count : 0.0), age = t - born;
	if (age < 0.0) return false;
	const Vec3 start = (origin ? origin(born) : s.from) + detail::inBall(s, k, 2) * s.fromRadius;
	double fade = smoothstep(0.0, 0.5, age);
	if (s.life > 0.0) {
		const double lifeHere = s.life * (0.6 + 0.7 * detail::rnd(s, k, 13));
		if (age > lifeHere) return false;
		const double left = 1.0 - age / lifeHere;
		fade = std::min(fade, left * std::sqrt(left));
	}
	if (s.hasTo) {
		const double arrived = born + s.arrive;
		const double dive = s.hasInto && k >= s.keep ? s.into0 + (s.into1 - s.into0) * detail::rnd(s, k, 14) : 1e30;
		if (t >= dive + 0.8) return false;
		if (t < arrived) {
			const double u = age / s.arrive;
			const Vec3 end = detail::orbitAt(s, k, arrived);
			p = detail::bezier(start, (start + end) * 0.5 + Vec3{0.0, 0.35 * length(end - start) * 0.5, 0.0}, end, 0.3 * smoothstep(0.0, 1.0, u) + 0.7 * u);
		} else if (t >= dive) {
			const double u = (t - dive) / 0.8;
			p = detail::orbitAt(s, k, dive) + (s.into - detail::orbitAt(s, k, dive)) * (u * u);
		} else if (s.hasInto && k < s.keep && t >= s.into1) {
			const double u = smoothstep(s.into1, s.into1 + 4.0, t);
			const Vec3 d = detail::inBall(s, k, 15), home = s.to + Vec3{d.x * s.settle, -0.6 * std::abs(d.y) * s.settle * 0.5, d.z * s.settle};
			p = detail::orbitAt(s, k, s.into1) + (home - detail::orbitAt(s, k, s.into1)) * u + Vec3{0.2 * std::sin(0.8 * t + k), 0.25 * std::sin(1.1 * t + k), 0.2 * std::cos(0.6 * t + k)} * u;
		} else {
			p = detail::orbitAt(s, k, t);
		}
	} else {
		const Vec3 v = s.velocity + detail::inBall(s, k, 16) * s.spread;
		p = start + v * age + Vec3{0.0, -s.gravity * age * age, 0.0};
	}
	b = fade * (s.pulse > 0.0 ? 0.6 + 0.4 * std::sin(6.2831853 * s.pulse * (0.7 + 0.6 * detail::rnd(s, k, 17)) * t + 6.28 * detail::rnd(s, k, 18)) : 1.0);
	return b > 0.003;
}

// The swarm at t, as scene text, its discs turned to `eye`.
inline std::string text(const Swarm &s, double t, Vec3 eye, const std::function<Vec3(double)> &origin) {
	if (t < s.t0) return std::string();
	std::string points[kLevels], corners[kLevels];
	int made[kLevels] = {0};
	Vec3 sum{0.0, 0.0, 0.0};
	int alive = 0;
	char buf[160];
	constexpr int kSides = 6;
	for (int k = 0; k < s.count; ++k) {
		Vec3 p;
		double b = 0.0;
		if (!at(s, k, t, origin, p, b)) continue;
		sum = sum + p, ++alive;
		const int level = std::min(kLevels - 1, static_cast<int>(b * kLevels));
		Vec3 toEye = eye - p;
		if (!(length(toEye) > 1e-6)) continue;
		toEye = normalize(toEye);
		const Vec3 right = normalize(cross(std::abs(toEye.y) > 0.95 ? Vec3{1.0, 0.0, 0.0} : Vec3{0.0, 1.0, 0.0}, toEye)), up = cross(toEye, right);
		const int first = made[level];
		std::snprintf(buf, sizeof buf, "%.4f %.4f %.4f  ", p.x, p.y, p.z);
		points[level] += buf;
		for (int c = 0; c < kSides; ++c) {
			const double a = 6.2831853 * c / kSides;
			const Vec3 q = p + (right * std::cos(a) + up * std::sin(a)) * s.size;
			std::snprintf(buf, sizeof buf, "%.4f %.4f %.4f  ", q.x, q.y, q.z);
			points[level] += buf;
			std::snprintf(buf, sizeof buf, "%d %d %d ", first, first + 1 + c, first + 1 + (c + 1) % kSides);
			corners[level] += buf;
		}
		made[level] += kSides + 1;
	}
	std::string out;
	for (int level = 0; level < kLevels; ++level) {
		if (made[level] == 0) continue;
		const double k = s.bright * (level + 0.5) / kLevels;
		std::snprintf(buf, sizeof buf, "[ %.4f %.4f %.4f ]", s.colour.x * k, s.colour.y * k, s.colour.z * k);
		out += std::string("AttributeBegin\n  AreaLightSource \"diffuse\" \"rgb L\" ") + buf + " \"bool twosided\" true \"bool sampled\" false\n  Material \"diffuse\" \"rgb reflectance\" [ 0 0 0 ]\n"
			   "  Shape \"trianglemesh\" \"integer indices\" [ " + corners[level] + "] \"point3 P\" [ " + points[level] + "]\nAttributeEnd\n";
	}
	if (s.glow > 0.0 && alive > 0) {
		const Vec3 mid = sum * (1.0 / alive);
		const double k = s.glow * alive / s.count;
		std::snprintf(buf, sizeof buf, "AttributeBegin\n  LightSource \"point\" \"point3 from\" [ %.4f %.4f %.4f ] \"rgb I\" [ %.4f %.4f %.4f ]\nAttributeEnd\n", mid.x, mid.y, mid.z, s.colour.x * k, s.colour.y * k, s.colour.z * k);
		out += buf;
	}
	return out.empty() ? out : "# swarm " + s.name + " (film_swarm.h)\n" + out;
}

// One `swarm` line's tokens after the verb. "" or what is wrong with it.
inline std::string parse(const std::vector<std::string> &t, Swarm &s) {
	const char *usage = "`swarm NAME COUNT from X Y Z [radius R]|after BLOCK during T0 T1 [to X Y Z [radius R] [arrive S] [orbit W]] [into X Y Z at T0 T1 [keep K]] [settle R] [velocity X Y Z] [spread S] [gravity G] [life L] "
						"[colour R G B] [bright B] [size S] [pulse HZ] [glow K] [seed N]`";
	const auto num = [&](std::size_t i, double &v) {
		if (i >= t.size()) return false;
		char *end = nullptr;
		v = std::strtod(t[i].c_str(), &end);
		return end != t[i].c_str() && *end == '\0' && std::isfinite(v);
	};
	double n = 0.0;
	if (t.size() < 6 || !num(2, n) || n < 1.0 || n > 200000.0 || n != std::floor(n)) return std::string(usage) + " (COUNT: 1 to 200000)";
	s.name = t[1], s.count = static_cast<int>(n);
	bool during = false, source = false;
	double *radiusOf = &s.fromRadius;
	for (std::size_t i = 3; i < t.size();) {
		const std::string w = t[i];
		const auto three = [&](Vec3 &v) {
			if (!num(i + 1, v.x) || !num(i + 2, v.y) || !num(i + 3, v.z)) return false;
			i += 4;
			return true;
		};
		const auto one = [&](double &v) {
			if (!num(i + 1, v)) return false;
			i += 2;
			return true;
		};
		bool ok = false;
		if (w == "from") ok = three(s.from), source = true, radiusOf = &s.fromRadius;
		else if (w == "after" && i + 1 < t.size()) s.after = t[i + 1], i += 2, ok = source = true;
		else if (w == "to") ok = three(s.to), s.hasTo = true, radiusOf = &s.toRadius;
		else if (w == "radius") ok = one(*radiusOf);
		else if (w == "during") ok = num(i + 1, s.t0) && num(i + 2, s.t1) && s.t1 >= s.t0, i += 3, during = true;
		else if (w == "arrive") ok = one(s.arrive) && s.arrive > 0.0;
		else if (w == "orbit") ok = one(s.orbit);
		else if (w == "into") {
			ok = three(s.into) && i + 2 < t.size() && t[i] == "at" && num(i + 1, s.into0) && num(i + 2, s.into1) && s.into1 >= s.into0;
			s.hasInto = true, i += 3;
		} else if (w == "keep") {
			double k = 0.0;
			ok = one(k) && k >= 0.0, s.keep = static_cast<int>(k);
		} else if (w == "settle") ok = one(s.settle);
		else if (w == "velocity") ok = three(s.velocity);
		else if (w == "spread") ok = one(s.spread);
		else if (w == "gravity") ok = one(s.gravity);
		else if (w == "life") ok = one(s.life) && s.life > 0.0;
		else if (w == "colour") ok = three(s.colour);
		else if (w == "bright") ok = one(s.bright) && s.bright > 0.0;
		else if (w == "size") ok = one(s.size) && s.size > 0.0;
		else if (w == "pulse") ok = one(s.pulse);
		else if (w == "glow") ok = one(s.glow);
		else if (w == "seed") {
			double k = 0.0;
			ok = one(k), s.seed = static_cast<int>(k);
		}
		if (!ok) return "\"" + w + "\": " + usage;
	}
	if (!during || !source) return usage;
	if (s.hasInto && !s.hasTo) return "`into` goes with `to`";
	return std::string();
}

}  // namespace swarm
}  // namespace cartoon
