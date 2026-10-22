#pragma once
// candle.h -- "The Candle", a 20 second film of one candle burning down on a dark table in front of a wall of out-of-focus fairy lights: the flame sways and flickers, light travels
// through the wax (it is a subsurface material, so the candle glows round the flame and the pool of melted wax is bright), drips run down its side, it shortens as it burns, and at the end
// the flame shrinks to the wick, goes out in a thread of smoke and leaves the fairy lights alone. Three shots: a close portrait of the flame, a low macro on the pool and the drips, and a
// slow pull back as the flame dies.
//
// The flame is an emissive teardrop (a surface of revolution) whose height and lean flicker with a few sines of different speeds; its blue root is a small second emitter. The candle's
// height is H(t) = 3.0 - 0.9 t / 20, the pool a shallow dish at its top; a drip is a rounded streak that starts at the rim, runs down and stops. The smoke is a chain of small spheres on a
// curve that wanders as it rises, each a little bigger and fainter than the one before.
//
// The story space is +x to the right of the picture, +y up, +z towards the camera (every emitted x is negated: pbrt is left-handed).

#include "cartoon_lib.h"

namespace cartoon {
namespace candle {

constexpr double kDuration = 20.0;
constexpr int kFps = 24;
constexpr int kSamples = 192;   // recommended samples per pixel (the Sampler line of the film's header)
constexpr double kRadius = 0.50;
constexpr double kOut = 16.6;       // the flame starts to die
constexpr double kDark = 18.0;      // the flame is gone

inline double fade(double t) { return smoothstep(0.0, 1.2, t) * (1.0 - smoothstep(kDuration - 1.2, kDuration, t)); }
inline double heightAt(double t) { return 3.0 - 0.9 * t / 20.0; }

// How big the flame is: 1 while it burns, dying away between kOut and kDark with a few last flickers.
inline double burn(double t) {
	if (t < kOut) return 1.0;
	const double u = clamp01((t - kOut) / (kDark - kOut));
	return (1.0 - u * u) * (0.75 + 0.25 * std::sin(34.0 * u));
}
inline double flicker(double t) { return 1.0 + 0.10 * std::sin(9.1 * t) + 0.07 * std::sin(15.3 * t + 1.0) + 0.05 * std::sin(23.7 * t + 2.0); }
inline Vec3 lean(double t) { return {0.07 * std::sin(1.9 * t) + 0.04 * std::sin(6.3 * t), 0.0, 0.05 * std::sin(1.3 * t + 1.0)}; }

struct Parts {
	Mesh flame, root, sphere, bokeh, dish;
};
inline Mesh candleBody(double h) {
	// the candle: a round body with a rounded foot, and a dish melted into its top
	return lathe({{0.0, 0.0}, {kRadius - 0.02, 0.0}, {kRadius, 0.03}, {kRadius, h - 0.12}, {kRadius - 0.04, h - 0.09}, {kRadius - 0.10, h - 0.14}, {kRadius - 0.22, h - 0.17}, {0.0, h - 0.17}}, 64);
}
inline const Parts &parts() {
	static const Parts p = [] {
		Parts q;
		// the flame, a unit teardrop standing on y = 0: widest low down, pointed above
		q.flame = lathe({{0.0, 0.0}, {0.06, 0.02}, {0.12, 0.12}, {0.14, 0.26}, {0.12, 0.42}, {0.08, 0.60}, {0.04, 0.78}, {0.012, 0.92}, {0.0, 1.0}}, 24);
		q.root = uvSphere(1.0, 16, 8);
		q.sphere = uvSphere(1.0, 24, 12);
		q.bokeh = uvSphere(1.0, 14, 7);   // seen far out of focus: a coarse sphere does
		q.dish = lathe({{0.0, 0.0}, {1.0, 0.0}, {1.0, 0.04}, {0.0, 0.04}}, 40);
		return q;
	}();
	return p;
}

inline const std::vector<Shot> &shots() {
	static const std::vector<Shot> s = [] {
		std::vector<Shot> v;
		v.push_back({0.0, Track3::withEases({{0.0, {0.9, 3.6, 4.4}, Ease::Smooth}, {7.0, {0.6, 3.4, 3.2}, Ease::Smooth}}), Track3::withEases({{0.0, {0.0, 3.5, 0.0}, Ease::Smooth}, {7.0, {0.0, 3.3, 0.0}, Ease::Smooth}}),
					 Track1::withEases({{0.0, 30.0, Ease::Smooth}}), 0.05});
		v.push_back({7.0, Track3::withEases({{7.0, {1.0, 1.9, 1.7}, Ease::Smooth}, {13.0, {-0.2, 1.8, 1.6}, Ease::Smooth}}), Track3::withEases({{7.0, {0.0, 2.0, 0.0}, Ease::Smooth}, {13.0, {0.0, 1.9, 0.0}, Ease::Smooth}}),
					 Track1::withEases({{7.0, 26.0, Ease::Smooth}}), 0.035});
		v.push_back({13.0, Track3::withEases({{13.0, {0.6, 3.0, 3.6}, Ease::Smooth}, {kDuration, {1.6, 3.6, 6.6}, Ease::Smooth}}), Track3::withEases({{13.0, {0.0, 2.9, 0.0}, Ease::Smooth}, {kDuration, {0.0, 3.0, 0.0}, Ease::Smooth}}),
					 Track1::withEases({{13.0, 32.0, Ease::Smooth}}), 0.06});
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

inline std::string buildCandle(double t) {
	const Parts &P = parts();
	const double h = heightAt(t);
	std::string out;
	// the holder: a small brass dish
	out += attr(metalMaterial({0.90, 0.72, 0.38}, 0.12), emitMesh(lathe({{0.0, 0.0}, {1.2, 0.0}, {1.25, 0.12}, {1.05, 0.16}, {0.7, 0.10}, {0.0, 0.10}}, 56), identity()));
	// the candle itself: cream wax, light going deep (the flame's glow is what comes out of it)
	out += attr(subsurfaceMaterial({0.96, 0.90, 0.76}, {1.1, 0.75, 0.45}, 0.05), emitMesh(candleBody(h), translate(0.0, 0.10, 0.0)));
	// the wick, and the wax that has run down: drips, and a puddle in the dish
	std::string drips, wick;
	const double top = 0.10 + h;
	wick += bar({0.0, top - 0.17, 0.0}, {0.01, top + 0.14, 0.0}, 0.014, 6);
	for (int i = 0; i < 5; ++i) {
		const double a = 2.0 * kPi * (0.13 + 0.2 * i + 0.06 * hash01(i, 1));
		const double start = 1.5 + 2.6 * i;                                      // when the drip leaves the rim
		const double run = clamp01((t - start) / 4.0);
		const double len = (0.25 + 0.5 * hash01(i, 2)) * smoothstep(0.0, 1.0, run);
		if (run <= 0.0) continue;
		const Vec3 rim = {(kRadius + 0.005) * std::cos(a), top - 0.14, (kRadius + 0.005) * std::sin(a)};
		drips += bar(rim, rim - Vec3{0.0, len + 0.02, 0.0}, 0.04 + 0.015 * hash01(i, 3), 10);
		drips += emitMesh(P.sphere, mul(translate(rim - Vec3{0.0, len + 0.02, 0.0}), scale(0.055, 0.05, 0.055)));
	}
	out += attr(diffuseMaterial({0.93, 0.86, 0.66}), drips);
	out += attr(diffuseMaterial({0.05, 0.04, 0.04}), wick);
	// the puddle spreads round the foot as the candle burns
	const double puddle = kRadius + 0.05 + 0.45 * smoothstep(2.0, 18.0, t);
	out += attr(subsurfaceMaterial({0.96, 0.90, 0.76}, {1.1, 0.75, 0.45}, 0.05), emitMesh(P.dish, mul(translate(0.0, 0.10, 0.0), scale(puddle, 1.0, puddle))));
	return out;
}

inline std::string buildFlame(double t, double f) {
	const Parts &P = parts();
	const double b = burn(t);
	if (b <= 0.01) return std::string();
	const double h = heightAt(t), top = 0.10 + h;
	const double size = b * flicker(t);
	const Vec3 lean3 = lean(t) * b;
	const Mat4 m = mul(translate(0.0, top + 0.12, 0.0), translate(lean3.x * 0.4, 0.0, lean3.z * 0.4), alignY({0, 0, 0}, normalize(Vec3{lean3.x, 1.0, lean3.z})), scale(0.62 * (0.8 + 0.2 * size), 1.18 * size, 0.62 * (0.8 + 0.2 * size)));
	const Vec3 L = {f * 15.0, f * 6.0, f * 0.8};
	std::string out = attr("Material \"diffuse\" \"rgb reflectance\" [ 0 0 0 ]", emitMesh(P.flame, m), &L);
	// the light the flame gives the candle, from just above it (a point light inside the emitter would be shut in by it)
	char lit[160];
	std::snprintf(lit, sizeof(lit), "LightSource \"point\" \"point3 from\" [ %.4f %.4f %.4f ] \"rgb I\" [ %.4f %.4f %.4f ]\n", -lean3.x * 0.3, top + 0.95 * size, lean3.z * 0.3, f * size * 22.0, f * size * 11.0, f * size * 3.0);
	out += lit;
	// and a dim warm fill from the camera's side, which is what shows the drips on the front of the candle
	std::snprintf(lit, sizeof(lit), "LightSource \"point\" \"point3 from\" [ 0.4 %.4f 3.2 ] \"rgb I\" [ %.4f %.4f %.4f ]\n", top - 0.4, f * b * 2.4, f * b * 1.5, f * b * 0.7);
	out += lit;
	// the blue root of the flame, low and small
	const Vec3 blue = {f * 1.5, f * 5.0, f * 18.0};
	out += attr("Material \"diffuse\" \"rgb reflectance\" [ 0 0 0 ]", emitMesh(P.root, mul(translate(0.0, top + 0.17, 0.0), scale(0.05 * b, 0.05 * b, 0.05 * b))), &blue);
	return out;
}

inline std::string buildSmoke(double t) {
	if (t < kOut + 0.8) return std::string();
	const double age = t - (kOut + 0.8);
	const double top = 0.10 + heightAt(t) + 0.2;
	std::string puffs;
	for (int k = 0; k < 18; ++k) {
		const double u = k / 17.0;
		const double y = age * 0.55 * u + 0.05;   // each puff is a little older than the one below it: the thread lengthens as time goes on
		const double fadeOut = std::exp(-1.2 * y) * smoothstep(0.0, 0.6, age);
		if (fadeOut < 0.05) continue;
		const double x = 0.16 * std::sin(5.0 * y + 1.4 * t) * (0.4 + y), z = 0.12 * std::cos(4.1 * y + 1.1 * t) * (0.4 + y);
		puffs += emitMesh(parts().sphere, mul(translate(x, top + y, z), scale(0.025 + 0.05 * y)));
	}
	return attr(diffuseMaterial({0.30, 0.30, 0.32}), puffs);
}

inline std::string buildBokeh(double f, double t) {
	// fairy lights on a far wall, which the lens turns into discs
	std::string out;
	for (int i = 0; i < 70; ++i) {
		const double x = -11.0 + 22.0 * hash01(i, 21), y = 0.5 + 8.0 * hash01(i, 22), z = -9.0 - 4.0 * hash01(i, 23);
		const double twinkle = 0.75 + 0.25 * std::sin(1.3 * t + 6.0 * hash01(i, 24));
		const Vec3 warm = {1.0, 0.62 + 0.25 * hash01(i, 25), 0.25 + 0.3 * hash01(i, 26)};
		const Vec3 L = warm * (f * twinkle * 9.0);
		out += attr("Material \"diffuse\" \"rgb reflectance\" [ 0 0 0 ]", emitMesh(parts().bokeh, mul(translate(x, y, z), scale(0.14 + 0.16 * hash01(i, 27)))), &L);
	}
	return out;
}

inline std::string sceneAt(double t) {
	const double f = fade(t);
	std::string out = headerAt(t);
	char buf[160];
	std::snprintf(buf, sizeof(buf), "LightSource \"infinite\" \"rgb L\" [ %.4f %.4f %.4f ]\n", f * 0.006, f * 0.007, f * 0.012);
	out += buf;
	// the table: dark polished wood that gives the candle a soft reflection; and a dark wall behind the lights
	out += attr(coated({0.10, 0.055, 0.035}, 0.10), emitMesh(box(60.0, 1.0, 40.0), translate(0.0, -0.5, 0.0)));
	out += attr(diffuseMaterial({0.02, 0.02, 0.025}), emitMesh(box(120.0, 60.0, 0.5), translate(0.0, 25.0, -16.0)));
	out += buildBokeh(f, t) + buildCandle(t) + buildFlame(t, f) + buildSmoke(t);
	return out;
}

}  // namespace candle
}  // namespace cartoon
