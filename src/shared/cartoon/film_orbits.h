#pragma once
// film_orbits.h -- craft that fly as craft in orbit do (film_file.h `orbit`, `burn`; round 11): Kepler's orbits round a planet or a point, the transfer from one orbit to
// another (Hohmann's: a burn onto an ellipse that touches both, half a turn on it, a burn onto the new circle), the main engines firing on each burn, and the small
// thrusters of the attitude control (RCS) puffing whenever the craft starts or stops turning - before a burn against its way it flips round to fire its engines ahead, puffs
// at the start and the end of the flip, and flips back after.
//
// An orbit's pace is a cartoon's: `period` seconds a turn on its first circle (60 when not given), and the other circles and ellipses round the same body at Kepler's
// pace for that body (a turn's time as the 1.5th power of the orbit's size), so a craft climbing to an orbit four times as wide takes eight times as long a turn.
// Coordinates are pbrt's.

#include <algorithm>
#include <cmath>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "cartoon_lib.h"

namespace cartoon {
namespace cosmos {

struct Orbit {
	bool on = false;
	std::string around;            // (the planet's name, or "" for a point)
	Vec3 centre;                   // pbrt's
	double radius = 1000.0;        // its circle (or the ellipse's nearest)
	double apo = 0.0;              // the ellipse's farthest (0: a circle)
	double period = 60.0;          // seconds a turn on its first orbit
	double incline = 0.0, node = 0.0, phase = 0.0;   // degrees: its plane tilted about the x axis, then turned about the up axis; where on it at nature's time 0
	double to = 0.0, at = -1.0;    // a transfer to a circle of radius `to`, its first burn at nature's time `at` (below 0: none)
	bool backwards = false;        // going round clockwise seen from above
};

// Where on its orbit a craft is at nature's time `nt` (pbrt's), its velocity, and whether its engines are firing for a burn (a transfer's two burns, each a second and a
// half either side of its moment) and against its way (the transfer down: it fires ahead to slow).
struct OrbitState {
	Vec3 at, velocity;
	double burn = 0.0;   // 0 .. 1: how hard its engines fire
	bool retrograde = false;
	double flip = 0.0;   // 0 .. 1: how far it has turned round to fire ahead
};

namespace detail_orbit {
constexpr double kBurnHalf = 1.5, kFlip = 2.5;   // (seconds: a burn's half length; how long a flip takes, ending as the burn begins)
// A point `ang` radians round an ellipse (semi-major axis a, eccentricity e, periapsis at angle w) at mean anomaly M, in its own plane (x, z).
inline std::pair<double, double> kepler(double a, double e, double w, double M) {
	double E = M;
	for (int i = 0; i < 12; ++i) E -= (E - e * std::sin(E) - M) / (1.0 - e * std::cos(E));   // (Newton's method on Kepler's equation)
	const double nu = 2.0 * std::atan2(std::sqrt(1.0 + e) * std::sin(0.5 * E), std::sqrt(1.0 - e) * std::cos(0.5 * E));
	const double r = a * (1.0 - e * std::cos(E)), ang = w + nu;
	return {r * std::cos(ang), r * std::sin(ang)};
}
}  // namespace detail_orbit

// The point (pbrt's) a craft on the orbit is at, at nature's time `nt` (without the burns' flags: see orbitState).
inline Vec3 orbitPoint(const Orbit &o, double nt) {
	using namespace detail_orbit;
	const double r1 = o.radius, mu = 4.0 * kPi * kPi * r1 * r1 * r1 / (o.period * o.period);   // (the body's pull, from the first orbit's pace)
	const auto turnOf = [&](double a) { return 2.0 * kPi * std::sqrt(a * a * a / mu); };   // (a turn's time on an orbit of semi-major axis a)
	double x = 0.0, z = 0.0;
	const double phase0 = (o.backwards ? -o.phase : o.phase) * kPi / 180.0;   // (worked out going round anticlockwise, then mirrored when it goes the other way)
	if (o.apo > o.radius) {   // an ellipse, its nearest point at the phase angle
		const double a = 0.5 * (o.radius + o.apo), e = (o.apo - o.radius) / (o.apo + o.radius);
		std::tie(x, z) = kepler(a, e, phase0, 2.0 * kPi * nt / turnOf(a));
	} else {
		const double angleAt = phase0 + 2.0 * kPi * (o.at >= 0.0 && o.to > 0.0 ? std::min(nt, o.at) : nt) / turnOf(r1);
		if (o.at < 0.0 || nt <= o.at || o.to <= 0.0) {
			x = r1 * std::cos(angleAt), z = r1 * std::sin(angleAt);
		} else {   // a transfer: from the point of the first burn, half a turn on the ellipse that touches both circles, then on round the new one
			const double r2 = o.to, a = 0.5 * (r1 + r2), e = std::fabs(r2 - r1) / (r1 + r2), half = 0.5 * turnOf(a);
			if (nt < o.at + half) {
				const double w = r2 > r1 ? angleAt : angleAt + kPi;   // (going up, the burn's point is the nearest; going down, the farthest)
				std::tie(x, z) = kepler(a, e, w, (r2 > r1 ? 0.0 : kPi) + 2.0 * kPi * (nt - o.at) / turnOf(a));
			} else {
				const double ang = angleAt + kPi + 2.0 * kPi * (nt - o.at - half) / turnOf(r2);
				x = r2 * std::cos(ang), z = r2 * std::sin(ang);
			}
		}
	}
	if (o.backwards) z = -z;
	// its plane: tilted about the x axis, then turned about the up axis
	const double inc = o.incline * kPi / 180.0, node = o.node * kPi / 180.0;
	const double y1 = -z * std::sin(inc), z1 = z * std::cos(inc);
	return o.centre + Vec3{x * std::cos(node) + z1 * std::sin(node), y1, -x * std::sin(node) + z1 * std::cos(node)};
}

// The time a transfer's second burn comes (nature's time; below 0: none).
inline double secondBurn(const Orbit &o) {
	if (!o.on || o.at < 0.0 || o.to <= 0.0 || o.apo > o.radius) return -1.0;
	const double r1 = o.radius, r2 = o.to, a = 0.5 * (r1 + r2), mu = 4.0 * kPi * kPi * r1 * r1 * r1 / (o.period * o.period);
	return o.at + kPi * std::sqrt(a * a * a / mu);
}

inline OrbitState orbitState(const Orbit &o, double nt) {
	using namespace detail_orbit;
	OrbitState s;
	const double dt = 0.02;
	s.at = orbitPoint(o, nt);
	s.velocity = (orbitPoint(o, nt + dt) - orbitPoint(o, nt - dt)) * (1.0 / (2.0 * dt));
	if (o.at >= 0.0 && o.to > 0.0 && o.apo <= o.radius) {
		const bool down = o.to < o.radius;   // (both burns slow it going down: each fired ahead, after a flip)
		for (const double b : {o.at, secondBurn(o)}) {
			s.burn = std::max(s.burn, smoothstep(b - kBurnHalf - 0.3, b - kBurnHalf, nt) * (1.0 - smoothstep(b + kBurnHalf, b + kBurnHalf + 0.3, nt)));
			if (down) s.flip = std::max(s.flip, std::min(std::clamp((nt - (b - kBurnHalf - kFlip)) / kFlip, 0.0, 1.0), 1.0 - std::clamp((nt - (b + kBurnHalf)) / kFlip, 0.0, 1.0)));
		}
		s.retrograde = down;
	}
	return s;
}

// How far round a flip has turned (0 .. 1) when it is `u` of the way through its time, as thrusters turn a craft: a quick push to start it turning, a coast at that
// pace, a quick push the other way to stop it (the thrusters puff only at its two ends).
inline double flipTurn(double u) {
	u = std::clamp(u, 0.0, 1.0);
	const double r = 0.15, v = 1.0 / (1.0 - r);   // (the pushes' share of its time at each end; its coasting pace, so that it turns all the way)
	if (u < r) return 0.5 * v * u * u / r;
	if (u > 1.0 - r) return 1.0 - 0.5 * v * (1.0 - u) * (1.0 - u) / r;
	return 0.5 * v * r + v * (u - r);
}

// The `orbit` line's words after `orbit SHIP` (the centre already found): "" or what is wrong.
inline std::string orbitUsage() {
	return "`orbit SHIP around PLANET|X Y Z radius R [apoapsis A] [period SECONDS] [incline DEGREES] [node DEGREES] [phase DEGREES] [backwards] [to R2 at T]` "
		   "(a ship named before it; period 1 to 100000 s; apoapsis beyond the radius; a transfer only from a circle)";
}

}  // namespace cosmos
}  // namespace cartoon
