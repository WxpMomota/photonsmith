#pragma once
// rig.h -- what any film's characters do, written once: walk along a path (and where each foot came down), blink, look at something, sit down and stand up. A character's
// own file says how it looks; these say when and where it moves, in numbers its pose can take (the Nomai explorer's walk and blinks are these).
//
//     const rig::PathWalk w{{doorway, table, window}, 12.0, 0.7, 0.84};   // from 12 s at 0.7 m/s, 0.84 m a step
//     const rig::PathWalk::At at = w.at(t);                                // where it is, which way it faces, how many steps it has made, whether it is moving
//     pose.walk = at.steps;  pose.stride = at.moving ? 1.0 : 0.0;
//     pose.lidL = pose.lidR = rig::blink(t, 7);
//     const rig::Aim aim = rig::aimAt(headPosition, at.yaw, candle);      // the head's turn to look at the candle, within what a neck can do
//     pose.sit = rig::sitAmount(t, 30.0, 41.0);                            // sits down at 30 s, stands up at 41 s
//
// The footfalls (PathWalk::footfalls) are when and where each foot came down: the film's dust puffs and its footstep sounds come from the same list.

#include <algorithm>
#include <cmath>
#include <vector>

#include "cartoon_lib.h"

namespace cartoon {
namespace rig {

// The heading of a direction on the ground, degrees (0 along +z, 90 along +x): what a character's frame turns by.
inline double headingOf(Vec3 dir) { return std::atan2(dir.x, dir.z) * 180.0 / kPi; }

// A walk along a path of points on the ground (story space; distances and headings are taken on the ground plane, and a film on uneven ground stands the walker on its own
// ground), from time `start` at `speed` (units a second), `stride` the length of one step. It stops at the last point.
struct PathWalk {
	std::vector<Vec3> points;
	double start = 0.0, speed = 1.0, stride = 0.84;

	struct At {
		Vec3 at;             // where it is (y from the path's points)
		double yaw = 0.0;    // which way it faces (headingOf the segment it is on)
		double dist = 0.0;   // how far it has gone
		double steps = 0.0;  // how many steps it has made (fractional): a walk cycle's phase
		bool moving = false; // between the start and the end
	};
	struct Footfall {
		double t;     // when the foot came down
		Vec3 at;      // where (on the path, at the step's distance)
		int foot;     // 0 the left, 1 the right (the first step is the left)
		int step;     // its number, from 1
	};

	double length() const {
		double l = 0.0;
		for (std::size_t i = 0; i + 1 < points.size(); ++i) l += cartoon::length(flat(points[i + 1] - points[i]));
		return l;
	}
	// The place and the heading at a distance along the path.
	At along(double dist) const {
		At a;
		if (points.empty()) return a;
		a.at = points.front();
		if (points.size() == 1) return a;
		double left = std::max(0.0, dist);
		for (std::size_t i = 0; i + 1 < points.size(); ++i) {
			const Vec3 d = flat(points[i + 1] - points[i]);
			const double l = cartoon::length(d);
			if (l <= 0.0) continue;
			a.yaw = headingOf(d);
			if (left <= l || i + 2 == points.size()) {
				const double u = std::min(1.0, left / l);
				a.at = points[i] + (points[i + 1] - points[i]) * u;
				return a;
			}
			left -= l;
		}
		return a;
	}
	At at(double t) const {
		const double total = length();
		const double dist = total > 0.0 ? clamp01((t - start) * speed / total) * total : 0.0;
		At a = along(dist);
		a.dist = dist;
		a.steps = dist / stride;
		a.moving = t > start && dist < total;
		return a;
	}
	// The footfalls in (from, to]: one every stride along the path, at the time the walk reaches it.
	std::vector<Footfall> footfalls(double from, double to) const {
		std::vector<Footfall> out;
		const double total = length();
		if (speed <= 0.0 || stride <= 0.0) return out;
		for (int k = 1; k * stride <= total + 1e-9; ++k) {
			const double t = start + k * stride / speed;
			if (t <= from) continue;
			if (t > to) break;
			out.push_back({t, along(k * stride).at, (k - 1) % 2, k});
		}
		return out;
	}

private:
	static Vec3 flat(Vec3 v) { return {v.x, 0.0, v.z}; }
};

// A blink at time t, 0 open to 1 shut: one in each `period` seconds, at a moment in it that `seed` picks (within `spread` seconds of its start), shutting and opening over
// `half` seconds each way. Two eyes that blink together share a seed; a third that blinks on its own has its own.
inline double blink(double t, int seed, double period = 4.3, double spread = 3.2, double half = 0.10) {
	double c = 0.0;
	for (int k = -1; k <= 1; ++k) {
		const double slot = std::floor(t / period) + k;
		const double tb = slot * period + spread * hash01(static_cast<int>(slot) + 1000, seed);
		c = std::max(c, 1.0 - std::abs(t - tb) / half);
	}
	return clamp01(c);
}

// The turn of a head at `from`, its body facing `facing` (degrees, as headingOf), to look at `target`: a yaw to its left (+) and a pitch up (+), in degrees, each held within
// what a neck can do. `amount` (0..1) eases it in: 0 looks straight ahead.
struct Aim {
	double yaw = 0.0, pitch = 0.0;
};
inline Aim aimAt(Vec3 from, double facing, Vec3 target, double maxYaw = 70.0, double maxPitch = 40.0, double amount = 1.0) {
	const Vec3 d = target - from;
	const double flat = std::sqrt(d.x * d.x + d.z * d.z);
	Aim a;
	double yaw = headingOf(d) - facing;
	while (yaw > 180.0) yaw -= 360.0;
	while (yaw < -180.0) yaw += 360.0;
	a.yaw = std::clamp(yaw, -maxYaw, maxYaw) * amount;
	a.pitch = std::clamp(std::atan2(d.y, std::max(1e-9, flat)) * 180.0 / kPi, -maxPitch, maxPitch) * amount;
	return a;
}

// How far into sitting at time t, 0 standing to 1 seated: sitting down from `down` and standing up from `up`, each over `ease` seconds (smoothly). `up` before `down`: it stays
// seated from `down` on.
inline double sitAmount(double t, double down, double up, double ease = 0.8) {
	const double in = smoothstep(down, down + ease, t);
	return up > down ? in * (1.0 - smoothstep(up, up + ease, t)) : in;
}

}  // namespace rig
}  // namespace cartoon
