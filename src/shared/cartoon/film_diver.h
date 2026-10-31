#pragma once
// film_diver.h -- a scuba diver, round 15:
//
//   diver NAME from X Y Z to X Y Z [via X Y Z]... [at T] [speed S] [lamp] [ok T]... [suit blue|black|red] [seed K]
//            a diver in a wetsuit, mask and fins, a tank on their back, swimming level along the path (from, each via, to) from time T at S metres a second (0.4), a
//            slow flutter kick from the hips, arms tucked; hovering before and after, finning gently. Each breath out is a burst of bubbles from the regulator, rising
//            from where the diver was (heard: film_world.cpp's soundscape); with `lamp` a torch in the right hand lights the way ahead; at each `ok T` the right hand
//            makes the OK sign in front of the mask for two seconds.
//   follow NAME (a shot's word, film_file.h)
//            a camera can follow a diver by name, as it follows a character - and a school or swimmers by their kind (film_file.cpp wildSubject).
//
// Coordinates: pbrt's in the film file (on a terrain heights are above the ground, as everywhere: lifted()); meshes in story space (x mirrored).

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "cartoon_lib.h"
#include "film_deepsea.h"
#include "film_undersea.h"

namespace cartoon {
namespace diver {

struct Diver {
	std::string name, suit = "black";
	std::vector<Vec3> path;   // (from, each via, to)
	double at = 0.0, speed = 0.4;
	bool lamp = false;
	std::vector<double> oks;
	int seed = 1;
};
inline const std::vector<std::string> &suits() {
	static const std::vector<std::string> k = {"black", "blue", "red"};
	return k;
}
constexpr double kBreath = 4.5;   // (seconds a breath: in, then out)

// How far along its path a diver is at t, and the whole path's length.
inline double pathLength(const Diver &d) {
	double L = 0.0;
	for (std::size_t i = 1; i < d.path.size(); ++i) L += length(d.path[i] - d.path[i - 1]);
	return L;
}
// Where a diver is at t (pbrt's: the middle of their chest) and the way they face (level), smoothly round the corners, easing off at the ends.
inline Vec3 diverAt(const Diver &d, double t, Vec3 *fwd = nullptr) {
	if (d.path.empty()) return {0, 0, 0};
	const double L = pathLength(d);
	const double travel = L > 1e-9 ? std::clamp((t - d.at) * d.speed, 0.0, L) : 0.0;
	const auto pointAt = [&](double s) {
		s = std::clamp(s, 0.0, L);
		for (std::size_t i = 1; i < d.path.size(); ++i) {
			const double seg = length(d.path[i] - d.path[i - 1]);
			if (s <= seg || i + 1 == d.path.size()) return lerp(d.path[i - 1], d.path[i], seg > 1e-9 ? std::min(1.0, s / seg) : 1.0);
			s -= seg;
		}
		return d.path.back();
	};
	// (a short average along the path rounds the corners)
	Vec3 p{0, 0, 0};
	for (int k = -2; k <= 2; ++k) p = p + pointAt(travel + 0.4 * k);
	p = p * 0.2;
	if (fwd) {
		Vec3 w = pointAt(travel + 1.0) - pointAt(travel - 1.0);
		w.y *= 0.3;
		if (length(w) < 1e-6) w = d.path.size() > 1 ? d.path.back() - d.path.front() : Vec3{0, 0, 1};
		*fwd = normalize(Vec3{w.x, w.y, w.z} + Vec3{0.0, 0.0, 1e-9});
	}
	return p + Vec3{0.0, 0.05 * std::sin(2.0 * kPi * t / kBreath), 0.0};   // (rising a little on each breath in)
}
// The regulator, where the bubbles come from (pbrt's), at t.
inline Vec3 mouthAt(const Diver &d, double t) {
	Vec3 f;
	const Vec3 p = diverAt(d, t, &f);
	return p + f * 0.55 + Vec3{0.0, 0.05, 0.0};
}
inline double okness(const Diver &d, double t) {
	double ok = 0.0;
	for (const double o : d.oks) ok = std::max(ok, smoothstep(o, o + 0.4, t) * (1.0 - smoothstep(o + 1.8, o + 2.2, t)));
	return ok;
}

// The diver's meshes at t (local: forward +z, up +y, the chest at the origin, lying level): suit, tank, mask glass, fins, and the lamp's body.
inline void diverMesh(const Diver &d, double t, Mesh &suit, Mesh &tank, Mesh &glass, Mesh &fins, Mesh &gear) {
	using deepsea::detail::add;
	const double kick = std::sin(2.0 * kPi * t / 1.25 + d.seed);   // (a slow flutter kick)
	Mesh torso = capsule(0.17, 0.5, 18, 5);
	torso.uvs.clear();
	add(suit, torso, mul(translate(0.0, 0.0, -0.3), rotate({1, 0, 0}, 90.0), scale(1.15, 1.0, 0.85)));   // (along +z, a little wider than deep)
	Mesh head = uvSphere(0.11, 14, 10);
	head.uvs.clear();
	add(suit, head, translate(0.0, 0.07, 0.42));
	add(glass, roundedBox(0.16, 0.08, 0.04, 0.02, 3), translate(0.0, 0.09, 0.52));   // (the mask)
	add(gear, capsule(0.025, 0.05, 8, 3), mul(translate(0.0, 0.0, 0.52), rotate({1, 0, 0}, 90.0)));   // (the regulator)
	Mesh cyl = capsule(0.085, 0.55, 16, 4);
	cyl.uvs.clear();
	add(tank, cyl, mul(translate(0.0, 0.2, -0.35), rotate({1, 0, 0}, 90.0)));
	for (const double side : {-1.0, 1.0}) {
		// legs: from the hips back, the thigh and shin swinging up and down in turn, the fins flexing behind
		const double swing = 0.22 * kick * side;
		const Vec3 hip{side * 0.09, 0.0, -0.32}, knee = hip + Vec3{0.0, swing * 0.45, -0.42}, ankle = knee + Vec3{0.0, swing * 0.9 - 0.04, -0.4};
		add(suit, sdf::tubeMesh({hip, knee, ankle}, 0.065, 0.7, 8));
		const Vec3 tip = ankle + Vec3{0.0, swing * 0.8 - 0.06, -0.45};
		sealife::detail::fin(fins, ankle + Vec3{side * 0.07, 0.0, 0.0}, ankle - Vec3{side * 0.07, 0.0, 0.0}, tip + Vec3{side * 0.1, 0.0, 0.0});
		sealife::detail::fin(fins, ankle - Vec3{side * 0.07, 0.0, 0.0}, tip - Vec3{side * 0.1, 0.0, 0.0}, tip + Vec3{side * 0.1, 0.0, 0.0});
		// arms: tucked under the chest; the right one forward with the lamp, or up at the mask for the OK sign
		const Vec3 shoulder{side * 0.2, 0.02, 0.2};
		Vec3 elbow = shoulder + Vec3{side * 0.05, -0.15, 0.05}, hand = elbow + Vec3{-side * 0.08, -0.04, 0.2};
		if (side > 0.0 && (d.lamp || okness(d, t) > 0.0)) {
			const double ok = okness(d, t);
			const Vec3 forward = shoulder + Vec3{0.02, -0.12, 0.25}, ahead = forward + Vec3{-0.02, 0.02, 0.3};
			const Vec3 up = shoulder + Vec3{0.0, 0.05, 0.25}, sign = up + Vec3{-0.08, 0.15, 0.12};
			elbow = d.lamp ? lerp(forward, up, ok) : lerp(elbow, up, ok);
			hand = d.lamp ? lerp(ahead, sign, ok) : lerp(hand, sign, ok);
		}
		add(suit, sdf::tubeMesh({shoulder, elbow, hand}, 0.05, 0.75, 8));
		add(suit, uvSphere(0.045, 8, 6), translate(hand));
		if (side > 0.0 && okness(d, t) > 0.3) add(gear, torus(0.022, 0.007, 12, 5), mul(translate(hand + Vec3{0.0, 0.04, 0.02}), rotate({1, 0, 0}, 90.0)));   // (thumb and finger's ring)
		if (side > 0.0 && d.lamp) add(gear, capsule(0.03, 0.14, 10, 3), alignY(hand + Vec3{0.0, 0.0, -0.03}, {0.0, 0.0, 1.0}));
	}
}
// Where the lamp is and the way it points (pbrt's), for its light.
inline void lampOf(const Diver &d, double t, Vec3 &at, Vec3 &dir) {
	Vec3 f;
	const Vec3 p = diverAt(d, t, &f);
	const Vec3 right = normalize(cross(Vec3{0.0, 1.0, 0.0}, f) + Vec3{1e-9, 0, 0});
	at = p + f * 1.05 + right * -0.18 + Vec3{0.0, -0.08, 0.0};
	dir = normalize(f + Vec3{0.0, -0.25, 0.0});
}
// A diver at t as scene text, with their breath's bubbles and their lamp.
inline std::string diverText(const Diver &d, double t, double level, const underwater::Current &c) {
	Vec3 fwd;
	const Vec3 at = diverAt(d, t, &fwd);
	Mesh suit, tank, glass, fins, gear;
	diverMesh(d, t, suit, tank, glass, fins, gear);
	const Mat4 place = deepsea::detail::placed(at, fwd);
	const Vec3 suitColour = d.suit == "blue" ? Vec3{0.05, 0.12, 0.3} : d.suit == "red" ? Vec3{0.4, 0.05, 0.04} : Vec3{0.03, 0.03, 0.035};
	std::string out = "# a diver: " + d.name + " (film_diver.h)\n" + attr(coated(suitColour, 0.35), emitMesh(suit, place)) + attr(metalMaterial({0.85, 0.72, 0.15}, 0.25), emitMesh(tank, place)) +
					  attr("Material \"dielectric\" \"float eta\" [ 1.5 ]", undersea::detail::noShadow(emitMesh(glass, place))) + attr(coated({0.95, 0.75, 0.05}, 0.3), emitMesh(fins, place)) +
					  attr(coated({0.05, 0.05, 0.05}, 0.2), emitMesh(gear, place));
	// the breath: every kBreath seconds, a burst from the regulator where the diver was then
	std::vector<undersea::Bubble> bubbles;
	for (long n = static_cast<long>(std::floor((t - 60.0) / kBreath)); n <= static_cast<long>(std::floor(t / kBreath)); ++n) {
		const double start = n * kBreath + 0.55 * kBreath;
		if (start > t) continue;
		const Vec3 mouth = mouthAt(d, start);
		for (int k = 0; k < 36; ++k) {
			const int key = static_cast<int>(n * 131 + k) + d.seed * 104729;
			const double born = start + 1.1 * hash01(key, 1);
			const double r0 = k < 4 ? 0.01 + 0.012 * hash01(key, 2) : 0.0018 + 0.004 * hash01(key, 2);
			undersea::Bubble b;
			if (undersea::bubbleAt(mouth + Vec3{0.05 * (hash01(key, 3) - 0.5), 0.0, 0.05 * (hash01(key, 4) - 0.5)}, r0, t - born, t, key, level, c, b)) bubbles.push_back(b);
		}
	}
	out += undersea::bubblesText(bubbles);
	if (d.lamp) {
		Vec3 p, dir;
		lampOf(d, t, p, dir);
		const Vec3 to = p + dir * 5.0;
		char buf[320];
		std::snprintf(buf, sizeof buf, "AttributeBegin\n  LightSource \"spot\" \"point3 from\" [ %.4f %.4f %.4f ] \"point3 to\" [ %.4f %.4f %.4f ] \"float coneangle\" [ 20 ] \"float conedelta\" [ 7 ] \"rgb I\" [ 9 8.5 7.6 ]\nAttributeEnd\n",
					  p.x, p.y, p.z, to.x, to.y, to.z);
		out += buf;
	}
	return out;
}
// The film's divers with their heights made pbrt's (on a terrain heights are above the ground there).
inline std::vector<Diver> lifted(std::vector<Diver> divers, const std::function<double(double, double)> &groundAt) {
	for (Diver &d : divers)
		for (Vec3 &p : d.path) p.y += groundAt(p.x, p.z);
	return divers;
}

}  // namespace diver
}  // namespace cartoon
