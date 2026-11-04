#pragma once
// film_fastlane.h -- under the sea, round 18: the ocean's fast lanes - a current you can see and ride.
//
//   fastlane NAME at X Y Z to X Y Z ... [width W] [speed S] [streaks N] [debris N] [turtles N] [seed K]
//       a tube of fast water W metres across (4 by default) along its points at S m/s (3): streaks of light along it rushing past, bits of weed and leaves tumbling in it,
//       turtles riding it with their flippers out, and a soft roar inside it
//   ride WHO NAME from T [to T] [across X Y]      a character swept along the lane from T (where it was then, carried in from there over a second) to T (or its end),
//                                                 X Y metres off its middle; it swims as it goes, and lets go at the end
//   chase WHO after OTHER [lag S] [from T] [to T]  a character following another's way S seconds behind it (1.2): a chase
//
// Heights above the seabed until lifted, pbrt's x z (film_parse_sea.h).

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

#include "film_deepsea.h"
#include "film_flow.h"
#include "film_sealife.h"
#include "film_undersea.h"
#include "sdf_mesh.h"

namespace cartoon {
namespace fastlane {

struct Lane {
	std::string name;
	std::vector<Vec3> points;   // pbrt's (heights above the seabed until lifted)
	double width = 4.0, speed = 3.0;
	int streaks = 70, debris = 40, turtles = 3, seed = 1;
	// (worked out from the points when the film is loaded: a smooth line through them and the distance along it at each)
	std::vector<Vec3> line;
	std::vector<double> along;
	double total() const { return along.empty() ? 0.0 : along.back(); }
};
struct Ride {
	std::string who, lane;
	double from = 0.0, to = 1e30, x = 0.0, y = 0.0;
};
struct Chase {
	std::string who, after;
	double lag = 1.2, from = -1e30, to = 1e30;
};

// The smooth line through a lane's points (Catmull-Rom, a point every 0.5 m), and the distance along it.
inline void prepare(Lane &l) {
	l.line.clear(), l.along.clear();
	const std::vector<Vec3> &p = l.points;
	if (p.size() < 2) return;
	for (std::size_t i = 0; i + 1 < p.size(); ++i) {
		const Vec3 a = p[i == 0 ? 0 : i - 1], b = p[i], c = p[i + 1], d = p[std::min(p.size() - 1, i + 2)];
		const int n = std::max(2, static_cast<int>(length(c - b) / 0.5));
		for (int k = 0; k < n; ++k) {
			const double u = static_cast<double>(k) / n, u2 = u * u, u3 = u2 * u;
			l.line.push_back((b * 2.0 + (c - a) * u + (a * 2.0 - b * 5.0 + c * 4.0 - d) * u2 + (b * 3.0 - a - c * 3.0 + d) * u3) * 0.5);
		}
	}
	l.line.push_back(p.back());
	l.along.push_back(0.0);
	for (std::size_t i = 1; i < l.line.size(); ++i) l.along.push_back(l.along.back() + length(l.line[i] - l.line[i - 1]));
}
// The place `s` metres along a lane (pbrt's), its way there, and a frame across it (side, up).
struct Place {
	Vec3 at, way, side, up;
};
inline Place placeAt(const Lane &l, double s) {
	Place p;
	if (l.line.size() < 2) return p;
	s = std::clamp(s, 0.0, l.total());
	std::size_t i = 1;
	while (i + 1 < l.line.size() && l.along[i] < s) ++i;
	const double u = std::clamp((s - l.along[i - 1]) / std::max(1e-9, l.along[i] - l.along[i - 1]), 0.0, 1.0);
	p.at = lerp(l.line[i - 1], l.line[i], u);
	p.way = normalize(l.line[i] - l.line[i - 1]);
	p.side = cross(Vec3{0.0, 1.0, 0.0}, p.way);
	p.side = length(p.side) > 1e-6 ? normalize(p.side) : Vec3{1.0, 0.0, 0.0};
	p.up = cross(p.way, p.side);
	return p;
}
// How far along the lane a rider joins it coming from `from` (pbrt's: the lane's point nearest), and when it lets go (`to`, or the lane's end at its speed).
inline double joinAlong(const Lane &l, const Vec3 &from) {
	double joinAt = 0.0, best = 1e30;
	for (std::size_t i = 0; i < l.line.size(); ++i)
		if (const double d = length(l.line[i] - from); d < best) best = d, joinAt = l.along[i];
	return joinAt;
}
inline double rideEnd(const Lane &l, const Ride &r, const Vec3 &from) { return std::min(r.to, r.from + (l.total() - joinAlong(l, from)) / std::max(1e-6, l.speed)); }
// Where a rider is at t (pbrt's) and its way: in from where it was (`was`, now) over the first second, then along the lane at its speed (a little faster in its middle) from
// where it joins it coming from `from` (where it was when the ride began: DataFilm::rideJoinFrom). Let go, it coasts on for a second and a half and stays there until
// `holdTo` (its next walk or ride: DataFilm::rideHeld; a chase after it eases in from there) - it used to snap back to where it was before the ride. `going`: 1 while it is carried, easing to 0 as it
// comes to rest.
inline bool riderAt(const Lane &l, const Ride &r, double t, const Vec3 &from, const Vec3 &was, Vec3 &at, Vec3 &way, double holdTo = 1e30, double *going = nullptr) {
	if (t < r.from || l.line.size() < 2) return false;
	const double joinAt = joinAlong(l, from), end = rideEnd(l, r, from);
	if (t > std::max(end + 1.5, holdTo)) return false;
	const double ride = std::min(t, end) - r.from;
	const Place p = placeAt(l, joinAt + ride * l.speed * 1.1);
	at = p.at + p.side * r.x + p.up * r.y;
	way = p.way;
	if (t < r.from + 1.0) at = lerp(was, at, smoothstep(r.from, r.from + 1.0, t));
	const double coast = std::clamp(t - end, 0.0, 1.5);   // (let go: it coasts on and slows to a stop at 1.5 s)
	if (t > end) at = at + way * (l.speed * 0.5 * coast * (1.0 - 0.33 * coast));
	if (going) *going = 1.0 - smoothstep(end, end + 1.5, t);
	return true;
}

namespace detail {
using deepsea::detail::add;
using sealife::detail::facingOf;
using sealife::detail::story;
}  // namespace detail

// A lane at nature's time t as scene text: its streaks, debris and riding turtles.
inline std::string laneText(const Lane &l, double t) {
	using namespace detail;
	if (l.line.size() < 2) return std::string();
	const double L = l.total(), R = 0.5 * l.width;
	Mesh streaks, weed, leaves, shells, skins;
	for (int i = 0; i < l.streaks; ++i) {   // streaks of light rushing along it, thinner and faster in its middle
		const int s = l.seed * 977 + i;
		const double a = 2.0 * kPi * hash01(s, 1), rr = R * std::sqrt(hash01(s, 2));
		const double v = l.speed * (1.3 - 0.6 * (rr / R) * (rr / R)), len = 0.3 + 0.8 * hash01(s, 3) * v / l.speed;
		const double s0 = std::fmod(hash01(s, 4) * L + v * t, L);
		std::vector<Vec3> line;
		for (int k = 0; k <= 3; ++k) {
			const Place p = placeAt(l, s0 - len * k / 3.0);
			line.push_back(story(p.at + p.side * (rr * std::cos(a)) + p.up * (rr * std::sin(a))));
		}
		add(streaks, sdf::tubeMesh(line, 0.006 + 0.006 * hash01(s, 5), 0.2, 4));
	}
	for (int i = 0; i < l.debris; ++i) {   // weed and leaves tumbling in it
		const int s = l.seed * 31 + i;
		const double a = 2.0 * kPi * hash01(s, 1), rr = 0.8 * R * std::sqrt(hash01(s, 2)), v = l.speed * (1.1 - 0.4 * rr / R);
		const Place p = placeAt(l, std::fmod(hash01(s, 3) * L + v * t, L));
		const Vec3 at = p.at + p.side * (rr * std::cos(a + 0.4 * t)) + p.up * (rr * std::sin(a + 0.4 * t));
		const Mat4 tumble = mul(translate(story(at)), rotate(normalize(Vec3{hash01(s, 4) - 0.5, hash01(s, 5) - 0.5, hash01(s, 6) - 0.5} + Vec3{0.0, 1e-3, 0.0}), 140.0 * t + 360.0 * hash01(s, 7)));
		if (i % 3 == 0) {
			Mesh blade;
			std::vector<Vec3> left, right;
			for (int k = 0; k <= 6; ++k) left.push_back({-0.02, 0.0, 0.07 * k}), right.push_back({0.02, 0.01 * std::sin(k), 0.07 * k});
			add(weed, flow::strip(left, right), tumble);
		} else {
			Mesh leaf = uvSphere(1.0, 8, 4);
			add(leaves, leaf, mul(tumble, scale(0.06, 0.006, 0.09)));
		}
	}
	for (int i = 0; i < l.turtles; ++i) {   // turtles riding it, flippers out, gliding
		const double s0 = std::fmod((i + 0.5) / std::max(1, l.turtles) * L + l.speed * 0.9 * t, L);
		const Place p = placeAt(l, s0);
		const Vec3 at = p.at + p.side * (0.25 * R * std::sin(0.3 * t + i)) + p.up * (0.15 * R);
		const Mat4 place = mul(translate(story(at)), facingOf(story(p.way)), rotate({0, 0, 1}, 8.0 * std::sin(0.5 * t + i)), scale(0.9 + 0.2 * hash01(i, l.seed)));
		Mesh shell = lathe({{0.0, -0.5}, {0.3, -0.42}, {0.45, -0.15}, {0.48, 0.1}, {0.38, 0.35}, {0.0, 0.48}}, 18);
		for (Vec3 &v : shell.verts) v = Vec3{v.x * 0.9, v.z * 0.3, v.y * 0.9};
		for (Vec3 &n : shell.normals) n = normalize(Vec3{n.x, n.z / 0.3, n.y});
		for (auto &tr : shell.tris) std::swap(tr[1], tr[2]);
		shell.uvs.clear();
		add(shells, shell, place);
		Mesh skin;
		add(skin, uvSphere(1.0, 10, 8), mul(translate(0.0, 0.0, 0.52), scale(0.09, 0.08, 0.11)));
		skin.uvs.clear();
		const double glide = 0.15 * std::sin(1.1 * t + i);
		for (const double side : {-1.0, 1.0}) {
			sealife::detail::fin(skin, {side * 0.25, 0.0, 0.2}, {side * 0.75, glide, -0.05}, {side * 0.25, 0.0, 0.02});
			sealife::detail::fin(skin, {side * 0.2, 0.0, -0.32}, {side * 0.38, 0.0, -0.45}, {side * 0.16, 0.0, -0.42});
		}
		add(skins, skin, place);
	}
	std::string out = "# a fast lane: " + l.name + " (film_fastlane.h)\n";
	const Vec3 glow{0.25, 0.4, 0.5};
	out += flow::glowAttr(undersea::detail::noShadow(emitMesh(streaks, identity())), glow);
	out += attr(diffuseMaterial({0.25, 0.45, 0.15}), emitMesh(weed, identity())) + attr(diffuseMaterial({0.55, 0.4, 0.15}), emitMesh(leaves, identity()));
	out += attr(coated({0.36, 0.3, 0.14}, 0.35), emitMesh(shells, identity())) + attr(diffuseMaterial({0.48, 0.5, 0.34}), emitMesh(skins, identity()));
	return out;
}

}  // namespace fastlane
}  // namespace cartoon
