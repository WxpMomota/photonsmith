#pragma once
// fireflies.h -- "Fireflies", a 26 second night-meadow cartoon in four shots, lit almost only by the fireflies themselves: a lone firefly wanders through the grass; a second arrives; they
// notice each other, their flashes drift into step, they circle and then spiral up towards the moon; the meadow wakes with dozens more as a crane shot lifts away.
//
// Shots: ground-level macro (0 - 7 s), the meeting (7 - 14), an orbit round the pair as the swarm wakes (14 - 20), a crane up over the meadow (20 - 26). Every firefly is a small dark body,
// two pale wings and a glowing abdomen (an emissive sphere, so each one really lights the grass round it); the moon is a large cool emitter. The grass sways, the mushrooms glow faintly.
// The story space is +x to the right of the picture, +y up, +z towards the camera (every emitted x is negated: pbrt is left-handed).

#include "cartoon_lib.h"

namespace cartoon {
namespace fireflies {

constexpr double kDuration = 26.0;
constexpr int kFps = 24;
constexpr int kSwarm = 56;
constexpr int kBlades = 1100;

inline double fade(double t) { return smoothstep(0.0, 1.5, t) * (1.0 - smoothstep(kDuration - 2.0, kDuration, t)); }

// A small deterministic pseudo-random number in [0, 1) from an integer and a salt.
inline double hash01(int i, int salt) {
	unsigned x = static_cast<unsigned>(i) * 747796405u + static_cast<unsigned>(salt) * 2891336453u + 12345u;
	x = ((x >> ((x >> 28u) + 4u)) ^ x) * 277803737u;
	x = (x >> 22u) ^ x;
	return (x & 0xFFFFFFu) / 16777216.0;
}

struct Parts {
	Mesh blade, body, glow, wing, trunk, capMushroom, stem, moon, star, ground;
};
inline const Parts &parts() {
	static const Parts p = [] {
		Parts q;
		q.blade = lathe({{0.022, 0.0}, {0.016, 0.25}, {0.008, 0.5}, {0.0, 0.62}}, 5);
		q.body = uvSphere(1.0, 16, 8);
		q.glow = uvSphere(1.0, 16, 8);
		q.wing = uvSphere(1.0, 12, 6);
		q.trunk = lathe({{0.0, 0.0}, {0.5, 0.0}, {0.42, 3.0}, {0.34, 8.0}, {0.0, 8.2}}, 14);
		q.capMushroom = lathe({{0.0, 0.0}, {0.10, 0.0}, {0.35, 0.02}, {0.40, 0.08}, {0.32, 0.20}, {0.15, 0.30}, {0.0, 0.33}}, 20);
		q.stem = lathe({{0.0, 0.0}, {0.05, 0.0}, {0.04, 0.15}, {0.0, 0.17}}, 10);
		q.moon = uvSphere(1.0, 36, 18);
		q.star = uvSphere(1.0, 6, 3);
		return q;
	}();
	return p;
}

inline std::string emitMesh(const Mesh &mesh, const Mat4 &m) {
	static const Mat4 mirror = scale(-1.0, 1.0, 1.0);
	std::string out;
	mesh.appendPbrt(out, mul(mirror, m), true);
	return out;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The two main fireflies
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
inline Vec3 wiggle(double t, double seed, double amount) {
	return {amount * std::sin(7.3 * t + seed), amount * 0.7 * std::sin(9.1 * t + 2.0 * seed), amount * std::cos(6.1 * t + 3.0 * seed)};
}

// Where the pair's meeting point is, and how high it rises when they fly off together.
inline Vec3 meetPoint(double t) {
	return Track3::withEases({{0.0, {0.0, 1.1, 0.3}, Ease::Smooth}, {14.0, {0.0, 1.2, 0.3}, Ease::Smooth}, {19.0, {0.4, 5.2, -1.0}, Ease::Smooth}, {26.0, {1.0, 7.5, -2.5}, Ease::Smooth}})(t);
}

inline Vec3 fireflyA(double t) {
	// wanders the left meadow, then comes to the meeting point; circles with B; spirals up with B
	const Vec3 wander = Track3::withEases({{0.0, {-4.6, 0.7, 0.8}, Ease::Smooth}, {3.0, {-3.4, 1.0, 0.2}, Ease::Smooth}, {5.5, {-3.9, 0.55, 1.0}, Ease::Smooth}, {7.5, {-2.6, 1.0, 0.6}, Ease::Smooth}})(t) +
							wiggle(t, 0.0, 0.10);
	const double meet = smoothstep(7.0, 9.6, t);
	const double orbitAmp = smoothstep(9.6, 11.0, t) * (1.0 - 0.35 * smoothstep(17.0, 20.0, t));
	const double ang = 2.0 * kPi * (0.08 * (t - 9.6) + 0.012 * (t - 9.6) * (t - 9.6)) ;
	const Vec3 orbit = Vec3{std::cos(ang), 0.25 * std::sin(2.0 * ang), std::sin(ang)} * (0.55 * orbitAmp);
	return lerp(wander, meetPoint(t) + orbit + wiggle(t, 0.0, 0.03), meet);
}
inline Vec3 fireflyB(double t) {
	const Vec3 arrive = Track3::withEases({{6.0, {6.5, 2.6, -1.8}, Ease::Smooth}, {9.4, {1.6, 1.4, 0.6}, Ease::Smooth}})(t) + wiggle(t, 1.7, 0.10);
	const double meet = smoothstep(8.8, 10.2, t);
	const double orbitAmp = smoothstep(9.6, 11.0, t) * (1.0 - 0.35 * smoothstep(17.0, 20.0, t));
	const double ang = 2.0 * kPi * (0.08 * (t - 9.6) + 0.012 * (t - 9.6) * (t - 9.6)) + kPi;
	const Vec3 orbit = Vec3{std::cos(ang), 0.25 * std::sin(2.0 * ang), std::sin(ang)} * (0.55 * orbitAmp);
	const Vec3 start = {7.5, 2.8, -2.0};
	const Vec3 base = t < 6.0 ? start : arrive;
	return lerp(base, meetPoint(t) + orbit + wiggle(t, 1.7, 0.03), meet);
}

// Glow: each firefly flashes about once a second; B's flashes are out of step with A's until the two notice each other (8 - 12 s), then the same.
inline double pulse(double t, double phase) {
	const double s = 0.5 + 0.5 * std::sin(2.0 * kPi * (1.05 * t) + phase);
	return 0.06 + 0.94 * s * s * s;
}
inline double glowA(double t) { return pulse(t, 0.0); }
inline double glowB(double t) { return t < 6.0 ? 0.0 : pulse(t, 2.4 * (1.0 - smoothstep(8.5, 12.0, t))); }

inline std::string buildFirefly(const Vec3 &pos, const Vec3 &velocity, double glow, double scaleUp, double f) {
	const Parts &P = parts();
	const Vec3 heading = length(velocity) > 1e-6 ? normalize(velocity) : Vec3{1.0, 0.0, 0.0};
	const Mat4 frame = mul(translate(pos), alignY({0, 0, 0}, heading), scale(scaleUp * 2.4));
	// the axis of the frame is +Y along the heading
	std::string out;
	out += attr(coated({0.07, 0.06, 0.05}, 0.4), emitMesh(P.body, mul(frame, translate(0.0, 0.0, 0.0), scale(0.018, 0.05, 0.018))));
	out += attr(coated({0.8, 0.85, 0.7}, 0.25), emitMesh(P.wing, mul(frame, translate(0.03, 0.0, 0.012), rotate({0, 1, 0}, 25.0), scale(0.012, 0.075, 0.003))) +
												 emitMesh(P.wing, mul(frame, translate(-0.03, 0.0, 0.012), rotate({0, 1, 0}, -25.0), scale(0.012, 0.075, 0.003))));
	const Vec3 L = {f * glow * 16.0, f * glow * 22.0, f * glow * 3.0};
	out += attr("Material \"diffuse\" \"rgb reflectance\" [ 0 0 0 ]", emitMesh(P.glow, mul(frame, translate(0.0, -0.06, 0.0), scale(0.022, 0.034, 0.022))), &L);
	return out;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The swarm: it wakes from the grass between 15 and 21 s and drifts up
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
inline std::string buildSwarm(double t, double f) {
	std::string out;
	for (int i = 0; i < kSwarm; ++i) {
		const double wake = 13.5 + 5.0 * hash01(i, 1);
		if (t < wake) continue;
		const double age = t - wake;
		const Vec3 home = {(hash01(i, 2) - 0.5) * 9.0, 0.2, -2.4 + hash01(i, 3) * 4.6};
		const double rise = smoothstep(0.0, 4.0, age) * (0.5 + 1.8 * hash01(i, 4)) + 0.6 * smoothstep(0.0, 9.0, age);
		const Vec3 pos = home + Vec3{0.4 * std::sin(0.7 * age + 6.0 * hash01(i, 5)), rise + 0.15 * std::sin(1.3 * age + i), 0.4 * std::cos(0.6 * age + 6.0 * hash01(i, 6))};
		const Vec3 vel = {0.25 * std::cos(0.7 * age + 6.0 * hash01(i, 5)), 0.3, -0.25 * std::sin(0.6 * age + 6.0 * hash01(i, 6))};
		const double blinkPhase = 2.0 * kPi * hash01(i, 7);
		const double glow = pulse(t * (0.85 + 0.3 * hash01(i, 8)), blinkPhase) * smoothstep(0.0, 1.2, age) * 0.75;
		out += buildFirefly(pos, vel, glow, 1.0, f);
	}
	return out;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The meadow
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
inline std::string buildMeadow(double t) {
	const Parts &P = parts();
	std::string out;
	out += attr(coated({0.10, 0.16, 0.12}, 0.6), emitMesh(box(90.0, 1.0, 70.0), translate(0.0, -0.5, -12.0)));
	// grass: each blade leans a little and sways
	const std::string green = coated({0.12, 0.34, 0.14}, 0.45);
	std::string blades;
	for (int i = 0; i < kBlades; ++i) {
		const double x = (hash01(i, 11) - 0.5) * 13.0, z = -3.0 + hash01(i, 12) * 7.5;
		const double h = 0.7 + 1.1 * hash01(i, 13);
		const double lean = 14.0 * (hash01(i, 14) - 0.5) + 5.0 * std::sin(1.1 * t + 0.9 * x + 0.5 * z);
		const double yaw = 360.0 * hash01(i, 15);
		blades += emitMesh(P.blade, mul(translate(x, 0.0, z), rotate({0, 1, 0}, yaw), rotate({0, 0, 1}, lean), scale(1.0, h, 1.0)));
	}
	out += attr(green, blades);
	// mushrooms with a faint glow
	const std::string capMat = coated({0.55, 0.22, 0.30}, 0.3), stemMat = coated({0.8, 0.78, 0.7}, 0.4);
	const double mx[5] = {-1.8, 2.4, 3.8, -4.4, 0.8}, mz[5] = {0.6, 0.4, -0.9, -1.0, -1.6}, ms[5] = {1.1, 1.4, 0.8, 1.2, 0.9};
	for (int i = 0; i < 5; ++i) {
		out += attr(stemMat, emitMesh(P.stem, mul(translate(mx[i], 0.0, mz[i]), scale(ms[i]))));
		out += attr(capMat, emitMesh(P.capMushroom, mul(translate(mx[i], 0.12 * ms[i] + 0.04, mz[i]), scale(ms[i] * 0.8))));
	}
	// trees at the back, and the moon and a few stars
	const std::string bark = coated({0.07, 0.06, 0.07}, 0.7);
	for (int i = 0; i < 9; ++i) out += attr(bark, emitMesh(P.trunk, mul(translate(-20.0 + 5.0 * i + 2.0 * hash01(i, 20), 0.0, -17.0 - 6.0 * hash01(i, 21)), scale(0.55 + 0.3 * hash01(i, 22), 1.3 + 1.0 * hash01(i, 23), 0.55 + 0.3 * hash01(i, 22)))));
	return out;
}

inline std::string buildSky(double f) {
	const Parts &P = parts();
	std::string out;
	const Vec3 moonL = {f * 2.2, f * 2.5, f * 3.4}, haloL = {f * 0.07, f * 0.10, f * 0.20};
	const Vec3 moonAt = {-2.0, 8.5, -32.0};
	out += attr("Material \"diffuse\" \"rgb reflectance\" [ 0 0 0 ]", emitMesh(P.moon, mul(translate(moonAt.x, moonAt.y, moonAt.z), scale(1.5))), &moonL);
	out += attr("Material \"diffuse\" \"rgb reflectance\" [ 0 0 0 ]", emitMesh(P.moon, mul(translate(moonAt.x, moonAt.y, moonAt.z - 8.0), scale(5.0))), &haloL);
	return out;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// Shots
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
inline const std::vector<Shot> &shots() {
	static const std::vector<Shot> s = {
		{0.0, Track3::withEases({{0.0, {-1.2, 0.35, 3.0}, Ease::Smooth}, {7.0, {-3.4, 0.55, 2.6}, Ease::Smooth}}), Track3::withEases({{0.0, {-4.0, 0.8, 0.5}, Ease::Smooth}, {7.0, {-3.0, 0.9, 0.2}, Ease::Smooth}}),
		 Track1::withEases({{0.0, 32.0, Ease::Smooth}}), 0.03},
		{7.0, Track3::withEases({{7.0, {1.4, 1.0, 4.4}, Ease::Smooth}, {14.0, {0.5, 1.3, 3.0}, Ease::Smooth}}), Track3::withEases({{7.0, {-1.2, 1.2, 0.3}, Ease::Smooth}, {14.0, {0.0, 1.25, 0.3}, Ease::Smooth}}),
		 Track1::withEases({{7.0, 34.0, Ease::Smooth}}), 0.025},
		{14.0, Track3::withEases({{14.0, {2.2, 1.4, 3.4}, Ease::Smooth}, {20.0, {-2.4, 2.4, 4.2}, Ease::Smooth}}), Track3::withEases({{14.0, {0.0, 1.3, 0.3}, Ease::Smooth}, {20.0, {0.3, 3.4, -0.6}, Ease::Smooth}}),
		 Track1::withEases({{14.0, 36.0, Ease::Smooth}}), 0.02},
		{20.0, Track3::withEases({{20.0, {0.0, 1.8, 6.5}, Ease::Smooth}, {26.0, {0.0, 5.0, 10.5}, Ease::Smooth}}), Track3::withEases({{20.0, {0.3, 3.0, 0.0}, Ease::Smooth}, {26.0, {0.3, 4.2, -2.0}, Ease::Smooth}}),
		 Track1::withEases({{20.0, 44.0, Ease::Smooth}}), 0.012},
	};
	return s;
}

inline std::string sceneAt(double t) {
	const double f = fade(t);
	std::string out = cameraText(cameraAt(shots(), t));
	out += "Film \"rgb\" \"integer xresolution\" [ 1280 ] \"integer yresolution\" [ 720 ]\n";
	out += "Sampler \"halton\" \"integer pixelsamples\" [ 128 ]\n";
	out += "Integrator \"volpath\" \"integer maxdepth\" [ 8 ]\n";
	out += "WorldBegin\n";
	char buf[200];
	std::snprintf(buf, sizeof(buf), "LightSource \"infinite\" \"rgb L\" [ %.4f %.4f %.4f ]\n", f * 0.006, f * 0.011, f * 0.034);
	out += buf;
	// a cool moonlight from high in front, so the grass and the ground have shape
	std::snprintf(buf, sizeof(buf), "LightSource \"distant\" \"point3 from\" [ 4 14 10 ] \"point3 to\" [ 0 0 0 ] \"rgb L\" [ %.4f %.4f %.4f ]\n", f * 0.9, f * 1.2, f * 2.2);
	out += buf;
	out += buildSky(f) + buildMeadow(t);
	const double dt = 0.02;
	const Vec3 pa = fireflyA(t), pb = fireflyB(t);
	out += buildFirefly(pa, (fireflyA(t + dt) - pa) * (1.0 / dt), glowA(t) * f, 1.0, 1.0);
	if (t >= 6.0) out += buildFirefly(pb, (fireflyB(t + dt) - pb) * (1.0 / dt), glowB(t) * f, 1.0, 1.0);
	out += buildSwarm(t, f);
	return out;
}

}  // namespace fireflies
}  // namespace cartoon
