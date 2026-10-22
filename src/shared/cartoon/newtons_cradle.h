#pragma once
// newtons_cradle.h -- "Newton's Cradle", an 18 second still-life of five chrome balls on strings: one is lifted and let go, strikes the row, and the ball at the far end flies out and
// comes back; the swing passes from side to side and slowly dies away. Three shots: the whole cradle as the first ball is lifted, a low close-up of the balls where they strike, and a
// wide pull back with the room in the chrome.
//
// The balls are mirrors, so the film is mostly about what they reflect: a warm soft box and a cool one from the front, a magenta and a teal light strip at the sides, and a coloured
// backdrop. The motion is the pendulum's own: the lifted ball falls through a quarter period (angle = A cos(w t)), the ball at the far end swings out and back in a half period
// (angle = A sin(w t)) with a little less amplitude each time, so the energy is visibly lost.
//
// The story space is +x to the right of the picture, +y up, +z towards the camera (every emitted x is negated: pbrt is left-handed).

#include "cartoon_lib.h"

namespace cartoon {
namespace newtons_cradle {

constexpr double kDuration = 18.0;
constexpr int kFps = 24;
constexpr int kSamples = 128;   // recommended samples per pixel (the Sampler line of the film's header)
constexpr int kBalls = 5;
constexpr double kRadius = 0.5;
constexpr double kStringLength = 2.6;
constexpr double kPivotY = 4.0;
constexpr double kRodZ = 0.7;          // the two rods the strings hang from, at +-z: each ball hangs in a V
constexpr double kReleaseAt = 2.6;     // seconds: the first ball is let go
constexpr double kQuarter = 0.62;      // seconds: a quarter of the pendulum's period (the fall from the highest point to the lowest)
constexpr double kAmplitude = 36.0;    // degrees: how far the first ball is lifted
constexpr double kKeep = 0.93;         // the swing keeps this fraction of its amplitude at each strike

inline double fade(double t) { return smoothstep(0.0, 1.2, t) * (1.0 - smoothstep(kDuration - 1.6, kDuration, t)); }

// The angle (degrees, from the vertical) of the ball at each end: the left one swings to negative x, the right one to positive.
struct Swing {
	double left, right;
};
inline Swing swingAt(double t) {
	if (t < kReleaseAt) return {-kAmplitude * smoothstep(0.7, 2.0, t), 0.0};   // lifted by hand, held for a moment
	double s = t - kReleaseAt;
	if (s < kQuarter) return {-kAmplitude * std::cos(0.5 * kPi * s / kQuarter), 0.0};   // the first fall
	s -= kQuarter;
	const int k = static_cast<int>(s / (2.0 * kQuarter));
	const double u = (s - k * 2.0 * kQuarter) / (2.0 * kQuarter);
	const double a = kAmplitude * std::pow(kKeep, k + 1) * std::sin(kPi * u);
	return k % 2 == 0 ? Swing{0.0, a} : Swing{-a, 0.0};
}

inline double ballX(int i) { return (i - (kBalls - 1) / 2.0) * 2.0 * kRadius; }

inline Vec3 ballCentre(int i, double t) {
	const Swing sw = swingAt(t);
	const double deg = i == 0 ? sw.left : (i == kBalls - 1 ? sw.right : 0.0);
	const double a = deg * kPi / 180.0;
	return {ballX(i) + kStringLength * std::sin(a), kPivotY - kStringLength * std::cos(a), 0.0};
}

struct Parts {
	Mesh ball, foot;
};
inline const Parts &parts() {
	static const Parts p = [] {
		Parts q;
		q.ball = uvSphere(1.0, 64, 32);
		q.foot = roundedBox(1.0, 1.0, 1.0, 0.1, 3);
		return q;
	}();
	return p;
}

inline const std::vector<Shot> &shots() {
	static const std::vector<Shot> s = {
		{0.0, Track3::withEases({{0.0, {3.6, 2.9, 11.5}, Ease::Smooth}, {6.0, {2.0, 2.6, 9.4}, Ease::Smooth}}), Track3::withEases({{0.0, {0.0, 2.1, 0.0}, Ease::Smooth}, {6.0, {-0.4, 2.1, 0.0}, Ease::Smooth}}),
		 Track1::withEases({{0.0, 36.0, Ease::Smooth}}), 0.03},
		{6.0, Track3::withEases({{6.0, {-1.8, 1.0, 3.6}, Ease::Smooth}, {11.0, {1.8, 1.2, 3.4}, Ease::Smooth}}), Track3::withEases({{6.0, {-0.2, 1.5, 0.0}, Ease::Smooth}, {11.0, {0.2, 1.5, 0.0}, Ease::Smooth}}),
		 Track1::withEases({{6.0, 30.0, Ease::Smooth}}), 0.06},
		{11.0, Track3::withEases({{11.0, {-7.0, 3.0, 8.0}, Ease::Smooth}, {18.0, {6.0, 3.6, 10.5}, Ease::Smooth}}), Track3::withEases({{11.0, {0.0, 2.1, 0.0}, Ease::Smooth}, {18.0, {0.0, 2.0, 0.0}, Ease::Smooth}}),
		 Track1::withEases({{11.0, 38.0, Ease::Smooth}}), 0.02},
	};
	return s;
}

// Everything up to and including WorldBegin: the camera at t, the picture's size, the sampler and the integrator.
inline std::string headerAt(double t) { return sceneHeader(cameraText(cameraAt(shots(), t)), kSamples); }
inline std::vector<double> shotStarts() {
	std::vector<double> v;
	for (const Shot &s : shots()) v.push_back(s.start);
	return v;
}

inline std::string buildCradle(double t) {
	const Parts &P = parts();
	std::string out;
	const std::string steel = metalMaterial({0.78, 0.79, 0.82}, 0.22);
	const std::string chrome = metalMaterial({0.93, 0.93, 0.95}, 0.012);
	const std::string stringMat = metalMaterial({0.30, 0.30, 0.32}, 0.35);
	// the base: a dark polished plate on four feet
	out += attr(coated({0.05, 0.045, 0.05}, 0.08), emitMesh(P.foot, mul(translate(0.0, 0.11, 0.0), scale(7.9, 0.22, 2.2))));
	// the frame: four posts, a cross bar at each end, and the two rods the strings hang from
	std::string frame;
	for (double sx : {-1.0, 1.0})
		for (double sz : {-1.0, 1.0}) frame += bar({sx * 3.6, 0.2, sz * kRodZ}, {sx * 3.6, kPivotY, sz * kRodZ}, 0.07);
	for (double sx : {-1.0, 1.0}) frame += bar({sx * 3.6, kPivotY, -kRodZ}, {sx * 3.6, kPivotY, kRodZ}, 0.07);
	for (double sz : {-1.0, 1.0}) frame += bar({-3.6, kPivotY, sz * kRodZ}, {3.6, kPivotY, sz * kRodZ}, 0.06);
	out += attr(steel, frame);
	// the balls and their strings
	std::string balls, strings;
	for (int i = 0; i < kBalls; ++i) {
		const Vec3 c = ballCentre(i, t);
		balls += emitMesh(P.ball, mul(translate(c), scale(kRadius)));
		for (double sz : {-1.0, 1.0}) strings += bar({ballX(i), kPivotY, sz * kRodZ}, c + normalize(Vec3{ballX(i), kPivotY, sz * kRodZ} - c) * (kRadius * 0.9), 0.012, 6);
	}
	out += attr(chrome, balls);
	out += attr(stringMat, strings);
	return out;
}

inline std::string buildRoom(double f) {
	std::string out;
	// the table: a warm wood top that reflects a little
	out += attr(coated({0.42, 0.27, 0.15}, 0.10), emitMesh(box(60.0, 1.0, 40.0), translate(0.0, -0.5, 0.0)));
	// the backdrop: deep teal, with a pale band along the bottom so the base and its reflection read
	out += attr(diffuseMaterial({0.14, 0.40, 0.44}), emitMesh(box(60.0, 30.0, 0.5), translate(0.0, 14.0, -8.0)));
	out += attr(diffuseMaterial({0.70, 0.66, 0.58}), emitMesh(box(60.0, 0.5, 0.2), translate(0.0, 1.6, -7.7)));
	// lights: a warm soft box and a cool one from the front (what the balls mostly show), and a magenta and a teal strip at the sides
	const Vec3 warm = {f * 9.0, f * 7.6, f * 5.6}, cool = {f * 3.4, f * 5.0, f * 8.0}, magenta = {f * 14.0, f * 1.4, f * 8.4}, teal = {f * 1.4, f * 10.0, f * 11.0};
	out += attr(diffuseMaterial({0.1, 0.1, 0.1}), emitMesh(box(7.0, 0.2, 4.0), mul(translate(-4.0, 9.0, 6.0), rotate({1, 0, 0}, -24.0))), &warm);
	out += attr(diffuseMaterial({0.1, 0.1, 0.1}), emitMesh(box(6.0, 0.2, 3.0), mul(translate(5.0, 8.0, 7.0), rotate({1, 0, 0}, -20.0))), &cool);
	out += attr(diffuseMaterial({0.1, 0.1, 0.1}), emitMesh(box(0.5, 9.0, 4.0), translate(-16.0, 4.5, 4.0)), &magenta);
	out += attr(diffuseMaterial({0.1, 0.1, 0.1}), emitMesh(box(0.5, 9.0, 4.0), translate(16.0, 4.5, 4.0)), &teal);
	return out;
}

inline std::string sceneAt(double t) {
	const double f = fade(t);
	std::string out = headerAt(t);
	char buf[120];
	std::snprintf(buf, sizeof(buf), "LightSource \"infinite\" \"rgb L\" [ %.4f %.4f %.4f ]\n", f * 0.20, f * 0.24, f * 0.30);
	out += buf;
	out += buildRoom(f) + buildCradle(t);
	return out;
}

}  // namespace newtons_cradle
}  // namespace cartoon
