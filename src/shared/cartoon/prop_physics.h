#pragma once
// prop_physics.h -- simple physics for a film's props, written once: something dropped or thrown that bounces and rolls to a stop, and something standing that topples over.
// Each is worked out from its start (closed form for the bounces, a fine fixed step for the topple), so the place at any time is the same whichever frame asks first, and
// each landing is an impact the soundtrack hears (impactsOf: film_events turns them into EventKind::Impact).
//
//     const physics::Bounce pebble{{0.4, 2.0, -3.0}, {0.6, 0.0, -0.3}, 6.0, 0.05};   // dropped from 2 m at 6 s, drifting; a 5 cm ball
//     const physics::Body b = physics::at(pebble, t);                                 // where it is, which way it has turned (rolling), whether it has stopped
//     const physics::Topple sign{9.0, {1.0, 0.0, -4.0}, {0.0, 0.0, 1.0}, 0.6, 0.08};   // from 9 s, pivoting on its edge at (1, 0, -4) about +z: 0.6 m tall, 8 cm thick
//     const double degrees = physics::angleOf(sign, t);
//
// Coordinates are whoever's calls them (story space in film code; a film file passes pbrt's); +y is up and the ground is a plane at `ground`.

#include <algorithm>
#include <cmath>
#include <vector>

#include "cartoon_lib.h"

namespace cartoon {
namespace physics {

constexpr double kGravity = 9.81;

// Something dropped or thrown: a ball of `radius` from `from` (its centre) with `velocity` at `start`. Each bounce keeps `restitution` of the speed into the ground and
// `grip` of the speed along it; when a bounce would rise less than `settleHeight` it rolls, slowing by `rolling` (m/s^2), and stops.
struct Bounce {
	Vec3 from, velocity;
	double start = 0.0, radius = 0.1;
	double restitution = 0.55, grip = 0.85, rolling = 1.2, ground = 0.0, settleHeight = 0.004;
	double gravity = kGravity;   // (the film's: `gravity`, a moon's 1.62)
};
// Where it is at a time: its centre, its velocity, how far it has turned about `spinAxis` (radians, rolling without slipping on the ground and spinning as it flies), and
// whether it has stopped.
struct Body {
	Vec3 at, velocity;
	double spin = 0.0;
	Vec3 spinAxis{1.0, 0.0, 0.0};
	bool resting = false;
};
// A landing: when, where (the point of contact) and how hard (the speed into the ground, m/s).
struct Impact {
	double t = 0.0;
	Vec3 at;
	double speed = 0.0;
};

namespace detail {
// The phases of a bounce: each flight (start time, start place, start velocity), then the roll.
struct Flight {
	double t0;
	Vec3 x0, v0;
	double duration;   // until it lands (or for ever, the last: < 0)
};
inline std::vector<Flight> flights(const Bounce &b, std::vector<Impact> *impacts = nullptr) {
	std::vector<Flight> out;
	const double floor = b.ground + b.radius;
	Vec3 x = b.from, v = b.velocity;
	double t = b.start;
	for (int n = 0; n < 64; ++n) {
		// y(s) = x.y + v.y s - g s^2 / 2 = floor: the later root
		const double a = 0.5 * b.gravity, bq = -v.y, c = floor - x.y;
		const double disc = bq * bq - 4.0 * a * c;
		if (disc < 0.0) break;   // (below the ground already: nothing to do)
		const double s = (-bq + std::sqrt(disc)) / (2.0 * a);
		out.push_back({t, x, v, s});
		const Vec3 landX = x + v * s + Vec3{0.0, -0.5 * b.gravity * s * s, 0.0};
		const double into = -(v.y - b.gravity * s);
		if (impacts) impacts->push_back({t + s, Vec3{landX.x, b.ground, landX.z}, into});
		t += s;
		x = Vec3{landX.x, floor, landX.z};
		const double up = into * b.restitution;
		v = Vec3{v.x * b.grip, up, v.z * b.grip};
		if (up * up / (2.0 * b.gravity) < b.settleHeight) {
			v.y = 0.0;
			out.push_back({t, x, v, -1.0});   // rolling from here
			return out;
		}
	}
	return out;
}
}  // namespace detail

inline Body at(const Bounce &b, double t) {
	Body body;
	body.at = b.from;
	if (t <= b.start) return body;
	const std::vector<detail::Flight> phases = detail::flights(b);
	double turned = 0.0;
	for (std::size_t i = 0; i < phases.size(); ++i) {
		const detail::Flight &f = phases[i];
		const Vec3 flat{f.v0.x, 0.0, f.v0.z};
		const double speed = length(flat);
		if (speed > 1e-9) body.spinAxis = normalize(cross(Vec3{0.0, 1.0, 0.0}, flat));
		if (f.duration < 0.0) {   // rolling: slowing evenly to a stop
			const double s = t - f.t0, stopAfter = b.rolling > 0.0 ? speed / b.rolling : 1e30;
			const double u = std::min(s, stopAfter);
			const double dist = speed * u - 0.5 * b.rolling * u * u;
			body.at = f.x0 + (speed > 1e-9 ? flat * (dist / speed) : Vec3{});
			body.velocity = speed > 1e-9 ? flat * ((speed - b.rolling * u) / speed) : Vec3{};
			body.spin = turned + dist / b.radius;
			body.resting = s >= stopAfter;
			return body;
		}
		if (t <= f.t0 + f.duration || i + 1 == phases.size()) {
			const double s = std::min(t - f.t0, f.duration);
			body.at = f.x0 + f.v0 * s + Vec3{0.0, -0.5 * b.gravity * s * s, 0.0};
			body.velocity = f.v0 + Vec3{0.0, -b.gravity * s, 0.0};
			body.spin = turned + speed * s / b.radius;   // (spinning in the air as it rolled on the ground: the grip keeps it turning)
			return body;
		}
		turned += speed * f.duration / b.radius;
	}
	return body;
}
inline std::vector<Impact> impactsOf(const Bounce &b) {
	std::vector<Impact> out;
	detail::flights(b, &out);
	return out;
}

// Something standing that topples: from `start` it tips about a bottom edge through `pivot` (direction `axis`, the turn by the right-hand rule) until it lies on the ground. It
// is `height` tall and `depth` thick (a slab, a domino, a book on end); `push` starts it turning (degrees a second); it is already `lean` degrees over at the start.
struct Topple {
	double start = 0.0;
	Vec3 pivot, axis{0.0, 0.0, 1.0};
	double height = 1.0, depth = 0.1, push = 30.0, lean = 0.0;
	double gravity = kGravity;
};
// How far over it is at t (degrees, 0 standing .. 90 lying), and when it lands (`landed`, < 0 when it does not).
inline double angleOf(const Topple &p, double t, double *landed = nullptr) {
	// a uniform block pivoting on its bottom edge: I theta'' = m g r sin(theta - theta0'), its centre r from the edge at theta0 off the vertical when standing
	const double r = 0.5 * std::sqrt(p.height * p.height + p.depth * p.depth), tilt = std::atan2(p.depth, p.height);
	const double inertia = (p.height * p.height + p.depth * p.depth) / 3.0;   // about the edge, per unit mass
	double theta = p.lean * kPi / 180.0, omega = p.push * kPi / 180.0;
	if (landed) *landed = -1.0;
	if (t <= p.start) return p.lean;
	const double dt = 1.0 / 960.0;
	for (double s = 0.0; s < t - p.start; s += dt) {
		const double alpha = p.gravity * r * std::sin(theta - tilt) / inertia;
		omega += alpha * dt;
		theta += omega * dt;
		if (theta >= 0.5 * kPi) {
			if (landed) *landed = p.start + s + dt;
			return 90.0;
		}
		if (theta < 0.0) {   // (not pushed hard enough: it rocks back and stands)
			theta = 0.0;
			omega = 0.0;
		}
	}
	return theta * 180.0 / kPi;
}
// Its landing as an impact (none when it does not fall): when, where its top lands, and how fast its top was going.
inline std::vector<Impact> impactsOf(const Topple &p) {
	double landed = -1.0;
	angleOf(p, p.start + 30.0, &landed);
	if (landed < 0.0) return {};
	const double before = angleOf(p, landed - 1.0 / 240.0) * kPi / 180.0;
	const double speed = (0.5 * kPi - before) * 240.0 * p.height;   // (the top's speed as it lands)
	// where the top lands: the pivot plus `height` along the ground, the way the turn takes it (+y turned about +z goes to -x: cross(axis, up))
	const Vec3 up{0.0, 1.0, 0.0};
	const Vec3 fall = normalize(cross(normalize(p.axis), up));
	return {{landed, p.pivot + fall * p.height, speed}};
}
// The block's transform at t: a turn of angleOf degrees about the pivot's edge (pivot space: translate to the pivot, rotate, translate back).
inline Mat4 transformOf(const Topple &p, double t) {
	return mul(translate(p.pivot), rotate(normalize(p.axis), angleOf(p, t)), translate(p.pivot * -1.0));
}

}  // namespace physics
}  // namespace cartoon
