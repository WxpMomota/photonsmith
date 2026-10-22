#pragma once
// domino_run.h -- "Domino Run", a 24 second film of a spiral of 130 dominoes falling in turn. A red marble rolls in and taps the first; the wave runs round and round, inwards, with a
// camera that chases its front; the last domino falls against the foot of a small lamp in the middle, and the lamp comes on. Four shots: the whole spiral and the marble, the chase,
// a low close-up as the wave reaches the lamp, and a rising pull back over the fallen rainbow.
//
// The dominoes are placed at equal steps along an Archimedean spiral (the radius falls linearly with the angle), each turned to face along the path. Domino i is struck at
// t_i = start + s_i / speed (s_i its distance along the path) and falls, over a third of a second and with an accelerating angle, about its bottom front edge until it leans on the
// next; every domino ends at the same lean, so they lie parallel and do not pass through one another. The colours run once round the rainbow and a half along the chain.
//
// The story space is +x to the right of the picture, +y up, +z towards the camera (every emitted x is negated: pbrt is left-handed).

#include "cartoon_lib.h"

namespace cartoon {
namespace domino_run {

constexpr double kDuration = 24.0;
constexpr int kFps = 24;
constexpr double kHeight = 0.60, kWidth = 0.34, kThick = 0.10;   // one domino
constexpr double kSpacing = 0.46;                                 // distance between neighbours along the path
constexpr double kFall = 0.30;                                    // seconds a domino takes to fall onto the next
constexpr double kLean = 68.0;                                    // degrees from the vertical where a fallen domino rests on its neighbour
constexpr double kSpeed = 3.8;                                    // how fast the wave runs along the path (units per second)
constexpr double kStart = 3.2;                                    // when the marble strikes the first domino
constexpr double kOuter = 4.4, kInner = 1.35, kTurns = 3.3;       // the spiral: radius at its start and end, and how many times it goes round
constexpr double kFirst = 0.8;                                    // distance along the path of the first domino
constexpr double kMarbleRadius = 0.2;

inline double fade(double t) { return smoothstep(0.0, 1.0, t) * (1.0 - smoothstep(kDuration - 1.4, kDuration, t)); }

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The path
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
constexpr double kTheta = kTurns * 2.0 * kPi;
inline double radiusAt(double th) { return kOuter - (kOuter - kInner) * th / kTheta; }
inline Vec3 pointAtTheta(double th) { return {radiusAt(th) * std::cos(th), 0.0, radiusAt(th) * std::sin(th)}; }
inline Vec3 tangentAtTheta(double th) {
	const double r = radiusAt(th), dr = -(kOuter - kInner) / kTheta;
	return normalize(Vec3{dr * std::cos(th) - r * std::sin(th), 0.0, dr * std::sin(th) + r * std::cos(th)});
}

// Distance along the path against the angle, tabulated once.
struct PathTable {
	std::vector<double> arc;   // arc[k] = distance at theta = k * dTheta
	double dTheta = 0.002;
	double length = 0.0;
};
inline const PathTable &pathTable() {
	static const PathTable t = [] {
		PathTable p;
		const int n = static_cast<int>(kTheta / p.dTheta) + 1;
		p.arc.assign(static_cast<std::size_t>(n) + 1, 0.0);
		for (int k = 1; k <= n; ++k) p.arc[static_cast<std::size_t>(k)] = p.arc[static_cast<std::size_t>(k) - 1] + length(pointAtTheta(k * p.dTheta) - pointAtTheta((k - 1) * p.dTheta));
		p.length = p.arc.back();
		return p;
	}();
	return t;
}
// The angle at distance s along the path (s is clamped to the path).
inline double thetaAtArc(double s) {
	const PathTable &p = pathTable();
	s = std::max(0.0, std::min(s, p.length));
	std::size_t lo = 0, hi = p.arc.size() - 1;
	while (hi - lo > 1) {
		const std::size_t mid = (lo + hi) / 2;
		(p.arc[mid] <= s ? lo : hi) = mid;
	}
	const double span = p.arc[hi] - p.arc[lo];
	return (static_cast<double>(lo) + (span > 0.0 ? (s - p.arc[lo]) / span : 0.0)) * p.dTheta;
}
inline Vec3 pointAtArc(double s) { return pointAtTheta(thetaAtArc(s)); }

inline int dominoCount() { return static_cast<int>((pathTable().length - kFirst) / kSpacing); }
inline double arcOf(int i) { return kFirst + i * kSpacing; }
inline double startOf(int i) { return kStart + (arcOf(i) - kFirst) / kSpeed; }
// When the last domino has fallen onto the lamp.
inline double impactAt() { return startOf(dominoCount() - 1) + kFall; }

// Lean angle (degrees) of domino i at time t: 0 until it is struck, then an accelerating fall to its rest. The last domino falls on, flat against the lamp.
inline double leanOf(int i, double t) {
	const double u = clamp01((t - startOf(i)) / kFall);
	return (i == dominoCount() - 1 ? 84.0 : kLean) * u * u;
}

inline Vec3 rainbow(double u) {
	// hue 0..1 -> rgb
	const double h = (u - std::floor(u)) * 6.0;
	const int k = static_cast<int>(h);
	const double f = h - k, q = 1.0 - f;
	const double s = 0.82, v = 0.95;
	const double lo = v * (1.0 - s), a = v * (1.0 - s * f), b = v * (1.0 - s * q);
	switch (k % 6) {
		case 0: return {v, b, lo};
		case 1: return {a, v, lo};
		case 2: return {lo, v, b};
		case 3: return {lo, a, v};
		case 4: return {b, lo, v};
		default: return {v, lo, a};
	}
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// Parts
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
struct Parts {
	Mesh domino, marble, bulb, stem, base;
};
inline const Parts &parts() {
	static const Parts p = [] {
		Parts q;
		q.domino = roundedBox(1.0, 1.0, 1.0, 0.06, 2);
		q.marble = uvSphere(1.0, 32, 16);
		q.bulb = uvSphere(1.0, 32, 16);
		q.stem = lathe({{0.0, 0.0}, {0.30, 0.0}, {0.28, 0.06}, {0.06, 0.16}, {0.05, 0.70}, {0.0, 0.72}}, 24);
		q.base = lathe({{0.0, 0.0}, {0.42, 0.0}, {0.42, 0.10}, {0.30, 0.16}, {0.0, 0.16}}, 32);
		return q;
	}();
	return p;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The marble: it rolls in along the path's own tangent, accelerating, and strikes the first domino at kStart
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
inline Vec3 marbleAt(double t) {
	const double th = thetaAtArc(kFirst);
	const Vec3 p0 = pointAtTheta(th), d = tangentAtTheta(th);
	const double reach = 4.0;                                   // how far behind the first domino it starts
	const double a = 2.0 * (reach - kThick) / (kStart * kStart);   // constant acceleration
	double s;
	if (t < kStart) s = -reach + 0.5 * a * t * t;
	else s = -kThick + (a * kStart) * 0.25 * (1.0 - std::exp(-(t - kStart) * 3.0));   // it carries on a little after the tap, slowing
	return p0 + d * s + Vec3{0.0, kMarbleRadius, 0.0};
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// Cameras
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// A track that follows `fn` sampled every quarter of a second (a smooth function, so the corners of the straight pieces do not show).
template <typename F>
inline Track3 sampled(double t0, double t1, F fn) {
	std::vector<Track3::Key> keys;
	for (double t = t0; t < t1 + 1e-9; t += 0.25) keys.push_back({t, fn(t), Ease::Linear});
	return Track3::withEases(keys);
}
inline double frontArc(double t) { return kFirst + std::max(0.0, t - kStart) * kSpeed; }
// The chase: a little behind the wave front, outside the spiral and above it, looking ahead along the path.
inline Vec3 chaseEye(double t) {
	const double s = frontArc(t - 0.3);
	const Vec3 p = pointAtArc(s - 2.2);
	const Vec3 outward = normalize(Vec3{p.x, 0.0, p.z});
	return p + outward * 1.6 + Vec3{0.0, 1.5, 0.0};
}
inline Vec3 chaseTarget(double t) { return pointAtArc(frontArc(t) + 1.2) + Vec3{0.0, 0.2, 0.0}; }

inline const std::vector<Shot> &shots() {
	static const std::vector<Shot> s = [] {
		const double tImpact = impactAt();
		std::vector<Shot> v;
		v.push_back({0.0, Track3::withEases({{0.0, {0.0, 8.0, 10.5}, Ease::Smooth}, {kStart, {1.8, 7.0, 9.6}, Ease::Smooth}}), Track3::withEases({{0.0, {0.0, 0.2, 0.0}, Ease::Smooth}, {kStart, {0.4, 0.2, 0.0}, Ease::Smooth}}),
					 Track1::withEases({{0.0, 38.0, Ease::Smooth}}), 0.03});
		v.push_back({kStart, sampled(kStart, tImpact - 1.4, chaseEye), sampled(kStart, tImpact - 1.4, chaseTarget), Track1::withEases({{kStart, 44.0, Ease::Smooth}}), 0.03});
		v.push_back({tImpact - 1.4, Track3::withEases({{tImpact - 1.4, {2.6, 0.9, 2.4}, Ease::Smooth}, {tImpact + 2.4, {1.8, 0.8, 2.0}, Ease::Smooth}}), Track3::withEases({{tImpact - 1.4, {0.2, 0.55, 0.1}, Ease::Smooth}, {tImpact + 2.4, {0.1, 0.6, 0.0}, Ease::Smooth}}),
					 Track1::withEases({{tImpact - 1.4, 34.0, Ease::Smooth}}), 0.04});
		v.push_back({tImpact + 2.4, Track3::withEases({{tImpact + 2.4, {0.5, 3.0, 7.4}, Ease::Smooth}, {kDuration, {0.0, 10.5, 5.5}, Ease::Smooth}}), Track3::withEases({{tImpact + 2.4, {0.0, 0.4, 0.0}, Ease::Smooth}, {kDuration, {0.0, 0.0, 0.0}, Ease::Smooth}}),
					 Track1::withEases({{tImpact + 2.4, 40.0, Ease::Smooth}}), 0.02});
		return v;
	}();
	return s;
}

// Everything up to and including WorldBegin: the camera at t, the picture's size, the sampler and the integrator.
inline std::string headerAt(double t) { return sceneHeader(cameraText(cameraAt(shots(), t))); }

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The scene
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
inline std::string buildDominoes(double t) {
	const Parts &P = parts();
	std::string out;
	const int n = dominoCount();
	for (int i = 0; i < n; ++i) {
		const double th = thetaAtArc(arcOf(i));
		const Vec3 p = pointAtTheta(th), d = tangentAtTheta(th);
		const double yaw = std::atan2(d.x, d.z) * 180.0 / kPi;
		const double lean = leanOf(i, t);
		// about the bottom front edge, which is kThick / 2 ahead of the domino's centre
		const Mat4 m = mul(translate(p), rotate({0, 1, 0}, yaw), translate(0.0, 0.0, kThick * 0.5), rotate({1, 0, 0}, lean), translate(0.0, 0.0, -kThick * 0.5), translate(0.0, kHeight * 0.5, 0.0),
						   scale(kWidth, kHeight, kThick));
		out += attr(coated(rainbow(1.5 * i / static_cast<double>(n)), 0.06), emitMesh(P.domino, m));
	}
	return out;
}

inline std::string buildLamp(double t, double f) {
	const Parts &P = parts();
	std::string out;
	out += attr(metalMaterial({0.86, 0.72, 0.42}, 0.15), emitMesh(P.base, identity()) + emitMesh(P.stem, translate(0.0, 0.16, 0.0)));
	// the bulb: dark until the last domino reaches the lamp, then it comes up over a second
	const double on = smoothstep(impactAt(), impactAt() + 1.2, t);
	const Vec3 glow = {f * on * 2.6, f * on * 1.9, f * on * 0.9};
	out += attr("Material \"diffuse\" \"rgb reflectance\" [ 0.9 0.9 0.8 ]", emitMesh(P.bulb, mul(translate(0.0, 1.02, 0.0), scale(0.26))), &glow);
	// and the light it gives the dominoes round it
	char lit[160];
	std::snprintf(lit, sizeof(lit), "LightSource \"point\" \"point3 from\" [ 0 1.02 0 ] \"rgb I\" [ %.4f %.4f %.4f ]\n", f * on * 7.0, f * on * 5.0, f * on * 2.2);
	if (on > 0.0) out += lit;
	return out;
}

inline std::string sceneAt(double t) {
	const double f = fade(t);
	std::string out = headerAt(t);
	char buf[200];
	std::snprintf(buf, sizeof(buf), "LightSource \"infinite\" \"rgb L\" [ %.4f %.4f %.4f ]\n", f * 0.07, f * 0.075, f * 0.11);
	out += buf;
	// two soft boxes: a big warm one up and to the front, a cool one behind and to the side
	const Vec3 key = {f * 9.0, f * 7.4, f * 5.6}, rim = {f * 2.6, f * 3.4, f * 5.2};
	out += attr(diffuseMaterial({0.1, 0.1, 0.1}), emitMesh(box(14.0, 0.2, 9.0), mul(translate(-3.0, 15.0, 5.0), rotate({1, 0, 0}, -14.0))), &key);
	out += attr(diffuseMaterial({0.1, 0.1, 0.1}), emitMesh(box(10.0, 0.2, 6.0), translate(6.0, 14.0, -6.0)), &rim);
	// the table: a green cutting mat with a fine grid, on a plain top
	out += "Texture \"mat\" \"spectrum\" \"checkerboard\" \"float uscale\" [ 34 ] \"float vscale\" [ 34 ] \"rgb tex1\" [ 0.16 0.30 0.26 ] \"rgb tex2\" [ 0.19 0.34 0.30 ]\n";
	out += attr("Material \"coateddiffuse\" \"texture reflectance\" [ \"mat\" ] \"float roughness\" [ 0.12 ]", emitMesh(box(34.0, 1.0, 34.0), translate(0.0, -0.5, 0.0)));
	out += attr(diffuseMaterial({0.10, 0.12, 0.18}), emitMesh(box(600.0, 40.0, 0.5), translate(0.0, 18.0, -17.0)));
	out += buildDominoes(t);
	out += buildLamp(t, f);
	const Vec3 m = marbleAt(t);
	out += attr(coated({0.85, 0.08, 0.08}, 0.03), emitMesh(parts().marble, mul(translate(m), scale(kMarbleRadius))));
	return out;
}

}  // namespace domino_run
}  // namespace cartoon
