#pragma once
// orrery.h -- "The Orrery", a 22 second turn round a brass model of a small solar system: a glowing sun on a column, five enamel planets on arms at five heights going round at their own
// speeds (the third has a moon, the fifth a ring), engraved orbit tracks on a round base, and beside it a pair of meshing gears that turn the other way. The only light is the sun
// (and a faint cool fill), so the film is mostly reflections of one bright thing in polished brass and enamel: a slow wide orbit, a low close-up that follows the third planet and its
// moon, and a rising look down at the orbits.
//
// Each planet k goes round at angle theta_k = theta0_k + 2 pi t / period_k; the moon goes round its planet five times as fast. The gears have 22 and 14 teeth: the small one turns
// 22/14 times as fast the other way, with its teeth half a pitch out of phase so that they mesh.
//
// The story space is +x to the right of the picture, +y up, +z towards the camera (every emitted x is negated: pbrt is left-handed).

#include "cartoon_lib.h"

namespace cartoon {
namespace orrery {

constexpr double kDuration = 22.0;
constexpr int kFps = 24;
constexpr int kPlanets = 5;

inline double fade(double t) { return smoothstep(0.0, 1.0, t) * (1.0 - smoothstep(kDuration - 1.4, kDuration, t)); }

struct Planet {
	double orbit, height, radius, period, phase;
	Vec3 colour;
};
inline const Planet &planet(int k) {
	static const Planet p[kPlanets] = {{1.25, 1.0, 0.15, 5.0, 0.4, {0.72, 0.72, 0.76}},
									   {1.95, 1.4, 0.21, 8.0, 2.1, {0.95, 0.52, 0.10}},
									   {2.65, 1.8, 0.25, 12.0, 4.0, {0.14, 0.44, 0.86}},
									   {3.35, 2.2, 0.20, 17.0, 5.2, {0.85, 0.16, 0.10}},
									   {4.05, 2.6, 0.40, 26.0, 1.0, {0.86, 0.70, 0.42}}};
	return p[k];
}
constexpr double kSunHeight = 2.9;
constexpr double kBaseRadius = 4.7;

inline double angleOf(int k, double t) { return planet(k).phase + 2.0 * kPi * t / planet(k).period; }
inline Vec3 planetAt(int k, double t) {
	const double a = angleOf(k, t);
	return {planet(k).orbit * std::cos(a), planet(k).height, planet(k).orbit * std::sin(a)};
}
inline Vec3 moonAt(double t) {
	const Vec3 p = planetAt(2, t);
	const double a = 2.0 * kPi * t / 2.4;
	return p + Vec3{0.55 * std::cos(a), 0.08 * std::sin(1.7 * a), 0.55 * std::sin(a)};
}

// the gears: 22 and 14 teeth, centres kGearGap apart (the sum of their pitch radii)
constexpr int kTeethBig = 22, kTeethSmall = 14;
constexpr double kPitch = 0.34;   // tooth to tooth along the pitch circle
constexpr double kBigRadius = kTeethBig * kPitch / (2.0 * kPi), kSmallRadius = kTeethSmall * kPitch / (2.0 * kPi);
inline Vec3 bigGearAt() { return {6.9, 0.02, -0.6}; }
inline Vec3 smallGearAt() { return bigGearAt() + Vec3{kBigRadius + kSmallRadius, 0.0, 0.0}; }
inline double bigGearAngle(double t) { return 360.0 * t / 14.0; }                                                                 // degrees: one turn in 14 s
inline double smallGearAngle(double t) { return -bigGearAngle(t) * kTeethBig / kTeethSmall + 180.0 / kTeethSmall; }                  // the other way, half a tooth out

struct Parts {
	Mesh sphere, base, column, tooth, disc, ring;
};
inline const Parts &parts() {
	static const Parts p = [] {
		Parts q;
		q.sphere = uvSphere(1.0, 40, 20);
		q.base = lathe({{0.0, 0.0}, {kBaseRadius + 0.25, 0.0}, {kBaseRadius + 0.25, 0.12}, {kBaseRadius + 0.10, 0.20}, {kBaseRadius, 0.26}, {kBaseRadius, 0.30}, {0.0, 0.30}}, 96);
		q.column = lathe({{0.0, 0.30}, {0.34, 0.30}, {0.30, 0.42}, {0.12, 0.60}, {0.10, 2.35}, {0.18, 2.45}, {0.18, 2.58}, {0.0, 2.58}}, 40);
		q.tooth = roundedBox(1.0, 1.0, 1.0, 0.08, 2);
		q.disc = lathe({{0.0, 0.0}, {1.0, 0.0}, {1.0, 1.0}, {0.0, 1.0}}, 56);
		q.ring = torus(1.0, 0.045, 48, 8);
		return q;
	}();
	return p;
}

// A gear lying flat at `centre`: a disc of the pitch radius, its teeth round it, a hub, turned by `degrees` about the vertical.
inline std::string gear(Vec3 centre, double pitchRadius, int teeth, double degrees) {
	const Parts &P = parts();
	std::string out;
	const double h = 0.22;
	out += emitMesh(P.disc, mul(translate(centre), rotate({0, 1, 0}, degrees), scale(pitchRadius - 0.05, h, pitchRadius - 0.05)));
	for (int i = 0; i < teeth; ++i) {
		const double a = 360.0 * i / teeth;
		out += emitMesh(P.tooth, mul(translate(centre), rotate({0, 1, 0}, degrees + a), translate(pitchRadius, h * 0.5, 0.0), scale(0.20, h, kPitch * 0.46)));
	}
	// a hub and four cut-outs' worth of spokes would be more gear-like, but the shine is what counts: a raised hub
	out += emitMesh(P.disc, mul(translate(centre + Vec3{0.0, h, 0.0}), scale(0.16, 0.08, 0.16)));
	return out;
}

inline const std::vector<Shot> &shots() {
	static const std::vector<Shot> s = [] {
		std::vector<Shot> v;
		v.push_back({0.0, Track3::withEases({{0.0, {-9.5, 5.2, 9.5}, Ease::Smooth}, {7.0, {2.0, 5.0, 12.5}, Ease::Smooth}}), Track3::withEases({{0.0, {1.4, 1.6, 0.0}, Ease::Smooth}, {7.0, {1.6, 1.6, 0.0}, Ease::Smooth}}),
					 Track1::withEases({{0.0, 36.0, Ease::Smooth}}), 0.03});
		// the close-up: beside the third planet and a little above, looking at it and its moon as they go round
		auto close = [](double t) { return planetAt(2, t) + Vec3{1.5, 0.7, 1.6}; };
		auto look = [](double t) { return planetAt(2, t) + Vec3{0.0, 0.0, 0.0}; };
		v.push_back({7.0, sampledTrack(7.0, 15.0, close), sampledTrack(7.0, 15.0, look), Track1::withEases({{7.0, 30.0, Ease::Smooth}}), 0.07});
		v.push_back({15.0, Track3::withEases({{15.0, {5.5, 3.0, 7.5}, Ease::Smooth}, {kDuration, {1.0, 12.5, 4.0}, Ease::Smooth}}), Track3::withEases({{15.0, {2.8, 1.6, 0.0}, Ease::Smooth}, {kDuration, {3.0, 0.0, 0.0}, Ease::Smooth}}),
					 Track1::withEases({{15.0, 40.0, Ease::Smooth}}), 0.02});
		return v;
	}();
	return s;
}

// Everything up to and including WorldBegin: the camera at t, the picture's size, the sampler and the integrator.
inline std::string headerAt(double t) { return sceneHeader(cameraText(cameraAt(shots(), t))); }

inline std::string buildOrrery(double t) {
	const Parts &P = parts();
	std::string out;
	const std::string brass = metalMaterial({0.90, 0.72, 0.38}, 0.10), dark = metalMaterial({0.55, 0.42, 0.22}, 0.28);
	out += attr(brass, emitMesh(P.base, identity()) + emitMesh(P.column, identity()));
	// orbit tracks engraved on the base
	std::string tracks;
	for (int k = 0; k < kPlanets; ++k) tracks += emitMesh(P.ring, mul(translate(0.0, 0.31, 0.0), scale(planet(k).orbit, 1.0, planet(k).orbit)));
	out += attr(dark, tracks);
	// arms and planets
	std::string arms;
	for (int k = 0; k < kPlanets; ++k) {
		const Vec3 p = planetAt(k, t);
		arms += bar({0.0, p.y, 0.0}, {p.x * (1.0 - planet(k).radius / planet(k).orbit), p.y, p.z * (1.0 - planet(k).radius / planet(k).orbit)}, 0.032, 8);
		arms += bar({p.x, 0.31, p.z}, {p.x, p.y - planet(k).radius, p.z}, 0.016, 6);   // a thin upright from the track, as a real orrery has
	}
	out += attr(brass, arms);
	for (int k = 0; k < kPlanets; ++k) out += attr(coated(planet(k).colour, 0.04), emitMesh(P.sphere, mul(translate(planetAt(k, t)), scale(planet(k).radius))));
	// the moon, and the ring of the fifth planet
	out += attr(coated({0.92, 0.92, 0.90}, 0.2), emitMesh(P.sphere, mul(translate(moonAt(t)), scale(0.075))));
	out += attr(coated({0.76, 0.62, 0.40}, 0.15), emitMesh(P.ring, mul(translate(planetAt(4, t)), rotate({1, 0, 0}, 24.0), scale(0.62, 0.35, 0.62))));
	// the sun
	return out;
}

inline std::string buildSun(double f, double t) {
	const double glow = 0.92 + 0.08 * std::sin(1.3 * t);
	const Vec3 L = {f * glow * 46.0, f * glow * 20.0, f * glow * 3.6};
	return attr("Material \"diffuse\" \"rgb reflectance\" [ 0 0 0 ]", emitMesh(parts().sphere, mul(translate(0.0, kSunHeight, 0.0), scale(0.52))), &L);
}

inline std::string sceneAt(double t) {
	const double f = fade(t);
	std::string out = headerAt(t);
	char buf[160];
	std::snprintf(buf, sizeof(buf), "LightSource \"infinite\" \"rgb L\" [ %.4f %.4f %.4f ]\n", f * 0.07, f * 0.07, f * 0.085);
	out += buf;
	// a cool soft fill from the side opposite the camera's first position
	const Vec3 fill = {f * 2.6, f * 2.2, f * 1.8};
	out += attr(diffuseMaterial({0.1, 0.1, 0.1}), emitMesh(box(8.0, 0.2, 5.0), translate(0.0, 16.0, 4.0)), &fill);
	// the table: dark polished wood
	out += attr(coated({0.17, 0.09, 0.05}, 0.07), emitMesh(box(60.0, 1.0, 40.0), translate(0.0, -0.5, 0.0)));
	out += attr(diffuseMaterial({0.03, 0.035, 0.05}), emitMesh(box(300.0, 60.0, 0.5), translate(0.0, 25.0, -22.0)));
	out += buildOrrery(t) + buildSun(f, t);
	// the gears
	const std::string gearMat = metalMaterial({0.92, 0.74, 0.40}, 0.12);
	out += attr(gearMat, gear(bigGearAt(), kBigRadius, kTeethBig, bigGearAngle(t)));
	out += attr(gearMat, gear(smallGearAt(), kSmallRadius, kTeethSmall, smallGearAngle(t)));
	return out;
}

}  // namespace orrery
}  // namespace cartoon
