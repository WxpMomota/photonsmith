#pragma once
// ball_parade.h -- "Ball Parade", a 12 second cartoon of squash and stretch: seven glossy balls drop in one after another from above the picture, bounce in a travelling wave along a
// glossy checker floor, and settle one by one with smaller and smaller bounces until they sit still in a row.
//
// Every ball does the same thing a little later than its neighbour: ball i follows f(t - i * kDelay). The bounce is a parabola whose height A(tau) follows an envelope (a first long fall
// from above the picture, a steady bounce, then decay) and whose period shrinks with the height, as a real bounce's does (T = T0 sqrt(A / A0)); the phase is integrated numerically so
// that the motion stays continuous while the height changes. The squash on landing and the stretch in the air come from the ball's speed.
//
// The story space is +x to the right of the picture, +y up, +z towards the camera; pbrt is left-handed, so every emitted x is negated (see lamp_and_ball.h's note).

#include "cartoon_lib.h"

namespace cartoon {
namespace ball_parade {

constexpr double kDuration = 12.0;
constexpr int kFps = 24;
constexpr int kSamples = 128;   // recommended samples per pixel (the Sampler line of the film's header)
constexpr int kBalls = 7;
constexpr double kRadius = 0.60;
constexpr double kSpacing = 1.45;
constexpr double kDelay = 0.22;      // seconds between neighbouring balls
constexpr double kT0 = 1.0;          // the steady bounce's period (seconds) at height kA0
constexpr double kA0 = 2.2;          // the steady bounce's height

// The height envelope of a bounce at ball-time tau: a long first fall, a steady bounce, a decay to rest.
inline double envelope(double tau) {
	return Track1::withEases({{0.0, 7.0, Ease::Smooth}, {0.9, 7.0, Ease::Smooth}, {1.8, kA0, Ease::Smooth}, {7.2, kA0, Ease::Smooth}, {10.4, 0.0, Ease::Smooth}})(tau);
}
inline double periodFor(double a) {
	const double minT = 0.14;
	return std::max(minT, kT0 * std::sqrt(std::max(a, 0.0) / kA0));
}

struct BallState {
	double bottom;   // height of the ball's lowest point above the floor
	double vy;
	double squash;   // vertical scale (< 1 squashed, > 1 stretched)
	double amp;
};

// Integrates the phase once, at a fine step, and keeps the table.
inline const std::vector<double> &phaseTable() {
	static const std::vector<double> table = [] {
		const double dt = 1.0 / 960.0;
		const int n = static_cast<int>((kDuration + 1.0) / dt) + 2;
		std::vector<double> th(static_cast<std::size_t>(n));
		double theta = 0.5;   // starts at the top of the first arc
		for (int k = 0; k < n; ++k) {
			th[static_cast<std::size_t>(k)] = theta;
			theta += dt / periodFor(envelope(k * dt));
		}
		return th;
	}();
	return table;
}

inline BallState ballAt(double tau) {
	if (tau < 0.0) return {kRadius * 0.0 + 8.0, 0.0, 1.0, 7.0};   // still above the picture
	const auto &tab = phaseTable();
	const double pos = tau * 960.0;
	const std::size_t k = std::min(static_cast<std::size_t>(pos), tab.size() - 2);
	const double theta = lerp(tab[k], tab[k + 1], pos - static_cast<double>(k));
	const double a = envelope(tau);
	if (a < 0.03) return {0.0, 0.0, 1.0, 0.0};
	const double u = theta - std::floor(theta);
	const double period = periodFor(a);
	const double h = a * 4.0 * u * (1.0 - u);
	const double vy = a * 4.0 * (1.0 - 2.0 * u) / period;
	// the contact: squash for a moment after touching the floor and just before leaving it
	const double d = std::min(u, 1.0 - u) * period;   // seconds from the nearest contact
	const double impact = clamp01(a / 2.0);
	double s = 1.0 + 0.020 * std::abs(vy) * 0.6 * (1.0 - clamp01(d / 0.25));   // stretch as it speeds up towards the floor
	if (d < 0.07) s = 1.0 - 0.34 * impact * (1.0 - d / 0.07);
	return {h, vy, s, a};
}

inline const Mesh &sphere() {
	static const Mesh m = uvSphere(1.0, 48, 24);
	return m;
}

inline Vec3 worldPoint(Vec3 p) { return {-p.x, p.y, p.z}; }

inline std::string cameraText(double t) {
	const Vec3 eye = Track3::withEases({{0.0, {-3.0, 3.2, 11.3}, Ease::Smooth}, {12.0, {2.8, 2.6, 9.8}, Ease::Smooth}})(t);
	const Vec3 target = Track3::withEases({{0.0, {0.0, 1.5, 0.0}, Ease::Smooth}, {12.0, {0.0, 1.0, 0.0}, Ease::Smooth}})(t);
	char buf[300];
	std::snprintf(buf, sizeof(buf), "LookAt %s %s 0 1 0\nCamera \"perspective\" \"float fov\" [ 34 ] \"float lensradius\" [ 0.04 ] \"float focaldistance\" [ %.3f ]\n",
				  fmt3(worldPoint(eye)).c_str(), fmt3(worldPoint(target)).c_str(), length(eye - target));
	return buf;
}

// Everything up to and including WorldBegin: the camera at t, the picture's size, the sampler and the integrator. Cheap, and all the scene list
// needs to describe the film's still.
inline std::string headerAt(double t) { return sceneHeader(cameraText(t), kSamples); }
inline std::vector<double> shotStarts() { return {0.0}; }   // one continuous shot

inline std::string sceneAt(double t) {
	std::string out = headerAt(t);
	out += "LightSource \"infinite\" \"rgb L\" [ 0.06 0.075 0.12 ]\n";
	// two soft boxes up out of the picture: a big warm one in front, a cooler one behind to light the balls rims
	const Vec3 key = {7.0, 6.2, 5.2}, rim = {2.0, 3.0, 5.5};
	out += attr(diffuseMaterial({0.1, 0.1, 0.1}), emitMesh(box(14.0, 0.2, 9.0), translate(1.0, 9.5, 3.0)), &key);
	out += attr(diffuseMaterial({0.1, 0.1, 0.1}), emitMesh(box(14.0, 0.2, 5.0), translate(-1.0, 9.5, -5.5)), &rim);
	// the floor: a glossy checker (the uv of the box's top face covers it once; the texture repeats)
	out += "Texture \"floor-check\" \"spectrum\" \"checkerboard\" \"float uscale\" [ 18 ] \"float vscale\" [ 10 ] \"rgb tex1\" [ 0.80 0.78 0.74 ] \"rgb tex2\" [ 0.30 0.32 0.40 ]\n";
	out += attr("Material \"coateddiffuse\" \"texture reflectance\" [ \"floor-check\" ] \"float roughness\" [ 0.04 ]", emitMesh(box(36.0, 1.0, 20.0), translate(0.0, -0.5, -1.0)));
	out += attr(diffuseMaterial({0.13, 0.17, 0.30}), emitMesh(box(60.0, 30.0, 0.5), translate(0.0, 12.0, -11.0)));
	const Vec3 colors[kBalls] = {{0.92, 0.12, 0.12}, {0.95, 0.50, 0.08}, {0.96, 0.82, 0.10}, {0.18, 0.72, 0.28}, {0.08, 0.72, 0.78}, {0.14, 0.32, 0.88}, {0.62, 0.20, 0.82}};
	for (int i = 0; i < kBalls; ++i) {
		const double x = (i - (kBalls - 1) / 2.0) * kSpacing;
		const BallState b = ballAt(t - i * kDelay);
		const double sy = b.squash, sxz = 1.0 / std::sqrt(sy);
		const double cy = kRadius * sy + b.bottom;   // the lowest point stays on the floor when squashed
		out += attr(coated(colors[i], 0.03), emitMesh(sphere(), mul(translate(x, cy, 0.0), scale(kRadius * sxz, kRadius * sy, kRadius * sxz))));
	}
	return out;
}

}  // namespace ball_parade
}  // namespace cartoon
