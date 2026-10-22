#pragma once
// dandelion.h -- "The Dandelion", a 22 second film of a dandelion clock against a low golden sun: a gust of wind blows its seeds away one by one, the windward side first, and they drift
// off over the meadow, each on its own little parachute, glowing where the sun shines through the fluff. Three shots: close beside the seed head as the first seeds lift, a slow follow of
// the cloud of seeds going away, and a wide pull back to the bare head and the long grass.
//
// A seed is a brown spindle on a thin stalk under a flat dome of pappus (a translucent disc, so the sun shows through it). On the head the seeds point out in all directions
// (Fibonacci's spiral on a sphere); a released seed turns its parachute up, so that it hangs beneath it, and travels with the wind: x = s (1.8 a + 0.12 a^2) along the wind, a slow
// rise and a gentle sway across it, where a is the seconds since it let go and s is its own speed.
//
// The story space is +x to the right of the picture, +y up, +z towards the camera (every emitted x is negated: pbrt is left-handed).

#include "cartoon_lib.h"

namespace cartoon {
namespace dandelion {

constexpr double kDuration = 22.0;
constexpr int kFps = 24;
constexpr int kSamples = 192;   // recommended samples per pixel (the Sampler line of the film's header)
constexpr int kSeeds = 140;
constexpr double kHeadHeight = 3.2;
constexpr double kStalk = 0.55;   // a seed's stalk
constexpr double kGust = 4.0;     // when the first seed lets go

inline double fade(double t) { return smoothstep(0.0, 1.2, t) * (1.0 - smoothstep(kDuration - 1.6, kDuration, t)); }

inline Vec3 wind() { return normalize(Vec3{1.0, 0.10, -0.35}); }

// The direction a seed points on the head: Fibonacci's spiral on a sphere.
inline Vec3 seedDirection(int i) {
	const double golden = kPi * (3.0 - std::sqrt(5.0));
	const double y = 1.0 - 2.0 * (i + 0.5) / kSeeds, r = std::sqrt(std::max(0.0, 1.0 - y * y));
	return {r * std::cos(golden * i), y, r * std::sin(golden * i)};
}

// When seed i lets go: the ones facing the wind first, spread over about six seconds.
inline double releaseOf(int i) {
	const double u = 0.5 - 0.5 * dot(seedDirection(i), wind());
	return kGust + 5.4 * u + 1.2 * hash01(i, 1);
}

struct SeedState {
	Vec3 position;   // the middle of the pappus
	Vec3 axis;       // from the spindle towards the pappus
	bool free;
	double age;
};
inline SeedState seedAt(int i, double t) {
	const Vec3 head = {0.0, kHeadHeight, 0.0}, d = seedDirection(i);
	const Vec3 attached = head + d * (0.12 + kStalk);
	const double release = releaseOf(i);
	if (t < release) {
		// a breath of wind sways the whole head a little before the gust
		const double sway = 0.012 * std::sin(1.7 * t + 3.0 * hash01(i, 2)) * smoothstep(0.0, 3.0, t);
		return {attached + Vec3{sway, 0.0, sway * 0.5}, d, false, 0.0};
	}
	const double a = t - release;
	const double s = 0.7 + 0.7 * hash01(i, 3);
	const double along = s * (1.8 * a + 0.12 * a * a);
	const double rise = 0.30 * a * (0.6 + hash01(i, 4)) + 0.4 * std::sin(0.9 * a + 6.0 * hash01(i, 5)) * (1.0 - std::exp(-a));
	const double across = 0.5 * std::sin(0.7 * a + 6.0 * hash01(i, 6)) * (1.0 - std::exp(-a * 0.6));
	const Vec3 w = wind(), side = normalize(cross(w, Vec3{0.0, 1.0, 0.0}));
	const Vec3 pos = attached + w * along + Vec3{0.0, rise, 0.0} + side * across;
	// the parachute turns up over about a second, then sways with the flight
	const double turn = smoothstep(0.0, 1.1, a);
	const Vec3 up = normalize(Vec3{0.18 * std::sin(1.3 * a + 4.0 * hash01(i, 7)), 1.0, 0.18 * std::cos(1.1 * a + 5.0 * hash01(i, 8))});
	return {pos, normalize(lerp(d, up, turn)), true, a};
}

struct Parts {
	Mesh pappus, spindle, sphere;
};
inline const Parts &parts() {
	static const Parts p = [] {
		Parts q;
		// the pappus: a shallow dome of fluff, from the stalk's end outwards
		q.pappus = lathe({{0.0, 0.0}, {0.03, 0.006}, {0.08, 0.02}, {0.13, 0.026}, {0.16, 0.018}, {0.14, 0.015}, {0.08, 0.015}, {0.0, 0.0}}, 16);
		q.spindle = lathe({{0.0, 0.0}, {0.02, 0.02}, {0.025, 0.08}, {0.012, 0.16}, {0.0, 0.18}}, 8);
		q.sphere = uvSphere(1.0, 32, 16);
		return q;
	}();
	return p;
}

inline const std::vector<Shot> &shots() {
	static const std::vector<Shot> s = [] {
		std::vector<Shot> v;
		v.push_back({0.0, Track3::withEases({{0.0, {-1.6, 3.5, 3.4}, Ease::Smooth}, {8.0, {-1.0, 3.3, 2.5}, Ease::Smooth}}), Track3::withEases({{0.0, {0.2, 3.2, 0.0}, Ease::Smooth}, {8.0, {0.3, 3.2, 0.0}, Ease::Smooth}}),
					 Track1::withEases({{0.0, 32.0, Ease::Smooth}}), 0.06});
		v.push_back({8.0, Track3::withEases({{8.0, {-1.0, 3.3, 2.5}, Ease::Smooth}, {15.0, {3.4, 3.9, 5.2}, Ease::Smooth}}), Track3::withEases({{8.0, {0.3, 3.2, 0.0}, Ease::Smooth}, {15.0, {5.5, 4.4, -1.0}, Ease::Smooth}}),
					 Track1::withEases({{8.0, 34.0, Ease::Smooth}}), 0.05});
		v.push_back({15.0, Track3::withEases({{15.0, {2.0, 2.4, 6.5}, Ease::Smooth}, {kDuration, {4.0, 3.4, 9.5}, Ease::Smooth}}), Track3::withEases({{15.0, {0.8, 3.0, 0.0}, Ease::Smooth}, {kDuration, {2.5, 3.2, -1.0}, Ease::Smooth}}),
					 Track1::withEases({{15.0, 38.0, Ease::Smooth}}), 0.035});
		return v;
	}();
	return s;
}

// Everything up to and including WorldBegin: the camera at t, the picture's size, the sampler and the integrator.
inline std::string headerAt(double t) { return sceneHeader(cameraText(cameraAt(shots(), t)), kSamples); }
inline std::vector<double> shotStarts() {
	std::vector<double> v;
	for (const Shot &s : shots()) v.push_back(s.start);
	return v;
}

inline std::string buildSeeds(double t) {
	const Parts &P = parts();
	std::string fluff, bodies, stalks;
	for (int i = 0; i < kSeeds; ++i) {
		const SeedState s = seedAt(i, t);
		const Vec3 end = s.position;                      // the pappus' middle, on the stalk's end
		const Vec3 base = end - s.axis * kStalk;          // the spindle's top, where the stalk starts
		const Mat4 frame = alignY(end, s.axis);
		fluff += emitMesh(P.pappus, mul(frame, scale(1.0)));
		stalks += bar(base, end, 0.005, 5);
		bodies += emitMesh(P.spindle, alignY(base - s.axis * 0.16, s.axis));
	}
	return attr(translucentMaterial({0.42, 0.40, 0.34}, {0.55, 0.52, 0.44}), fluff) + attr(coated({0.88, 0.86, 0.78}, 0.4), stalks) + attr(coated({0.42, 0.30, 0.16}, 0.4), bodies);
}

inline std::string buildPlant() {
	const Parts &P = parts();
	std::string out;
	// the head's receptacle, and the stem bending up to it
	out += attr(coated({0.34, 0.45, 0.20}, 0.5), emitMesh(P.sphere, mul(translate(0.0, kHeadHeight, 0.0), scale(0.13))));
	std::string stem;
	Vec3 prev = {0.0, 0.0, 0.0};
	for (int k = 1; k <= 12; ++k) {
		const double u = k / 12.0;
		const Vec3 p = {0.45 * u * u * (1.0 - 0.3 * u) - 0.1 * u, kHeadHeight * u, -0.18 * std::sin(kPi * u)};
		stem += bar(prev, p, 0.045 * (1.0 - 0.3 * u), 8);
		prev = p;
	}
	// the head sits where the stem ends
	out += attr(coated({0.30, 0.46, 0.18}, 0.45), stem);
	// leaves at the foot: long flat blades
	std::string leaves;
	for (int i = 0; i < 6; ++i) {
		const double a = 60.0 * i + 10.0;
		leaves += emitMesh(P.sphere, mul(translate(0.0, 0.05, 0.0), rotate({0, 1, 0}, a), translate(0.55, 0.12, 0.0), rotate({0, 0, 1}, 14.0), scale(0.62, 0.035, 0.12)));
	}
	out += attr(coated({0.22, 0.42, 0.14}, 0.5), leaves);
	return out;
}

inline std::string buildMeadow(double f) {
	std::string out;
	// the ground and a row of far-off grass heads that the lens turns into soft shapes
	out += attr(coated({0.30, 0.42, 0.14}, 0.6), emitMesh(box(300.0, 1.0, 300.0), translate(0.0, -0.5, 0.0)));
	std::string blades;
	for (int i = 0; i < 260; ++i) {
		const double x = -6.0 + 12.0 * hash01(i, 11), z = -7.0 + 8.4 * hash01(i, 12);
		const double h = 0.8 + 2.0 * hash01(i, 13), lean = 0.5 * (hash01(i, 14) - 0.5) * h;
		blades += bar({x, -0.05, z}, {x + lean, h, z + 0.2 * lean}, 0.016, 5);
	}
	out += attr(coated({0.30, 0.46, 0.16}, 0.5), blades);
	// the sky: bands of gold round the meadow, seen from inside, with the sun low behind the flower
	const std::string dark = diffuseMaterial({0.02, 0.02, 0.02});
	const Vec3 low = {f * 3.4, f * 1.8, f * 0.6}, mid = {f * 0.55, f * 0.36, f * 0.28}, high = {f * 0.16, f * 0.2, f * 0.40};
	auto band = [](double y0, double y1) { return lathe({{110.0, y0}, {110.0, y1}}, 48); };
	out += attr(dark, emitMesh(band(-2.0, 7.0), identity()), &low);
	out += attr(dark, emitMesh(band(7.0, 24.0), identity()), &mid);
	out += attr(dark, emitMesh(band(24.0, 100.0), identity()), &high);
	const Vec3 sun = {f * 220.0, f * 96.0, f * 18.0};
	out += emissiveSphere({-24.0, 6.0, -70.0}, 4.4, sun);
	return out;
}

inline std::string sceneAt(double t) {
	const double f = fade(t);
	std::string out = headerAt(t);
	char buf[160];
	std::snprintf(buf, sizeof(buf), "LightSource \"infinite\" \"rgb L\" [ %.4f %.4f %.4f ]\n", f * 0.20, f * 0.19, f * 0.16);
	out += buf;
	// the sun's own light on the flower: a distant light from behind and a little to the left
	std::snprintf(buf, sizeof(buf), "LightSource \"distant\" \"point3 from\" [ 4.8 5.0 -14 ] \"point3 to\" [ 0 3 0 ] \"rgb L\" [ %.4f %.4f %.4f ]\n", f * 3.0, f * 2.2, f * 1.2);
	out += buf;
	out += buildMeadow(f) + buildPlant() + buildSeeds(t);
	return out;
}

}  // namespace dandelion
}  // namespace cartoon
