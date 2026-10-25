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
// The footfalls (PathWalk::footfalls) are when and where each foot came down: the film's dust puffs and its footstep sounds come from the same list. Acting beyond that -
// pointing, kneeling, reaching, looking up, a nod - is gestures (Gesture, act): named poses from the character's own library, eased in and out at film times.

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <stdexcept>
#include <string>
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
	double firstStep = 1.0;   // how far in strides the first foot comes down (a walk cycle whose foot lands half a step in: 0.5); the rest follow a stride apart
	double turn = 2.0;        // at a corner the walker turns over this distance (half before it, half after), not all at once: a sudden turn swings its planted feet round

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
	// The place and the heading at a distance along the path (the heading turning smoothly round each corner, over `turn`).
	At along(double dist) const {
		At a;
		if (points.empty()) return a;
		a.at = points.front();
		if (points.size() == 1) return a;
		double left = std::max(0.0, dist);
		std::vector<double> headings, lengths;
		for (std::size_t i = 0; i + 1 < points.size(); ++i) {
			const Vec3 d = flat(points[i + 1] - points[i]);
			lengths.push_back(cartoon::length(d));
			headings.push_back(lengths.back() > 0.0 ? headingOf(d) : (headings.empty() ? 0.0 : headings.back()));
		}
		for (std::size_t i = 0; i < lengths.size(); ++i) {
			const double l = lengths[i];
			if (left <= l || i + 1 == lengths.size()) {
				const double u = l > 0.0 ? std::min(1.0, left / l) : 1.0;
				a.at = points[i] + (points[i + 1] - points[i]) * u;
				a.yaw = headings[i];
				// near a corner: part of the way round it (the corner before this segment, or the one after)
				const double half = 0.5 * std::min({turn, i > 0 ? lengths[i - 1] : turn, l});
				const auto blend = [&](double from, double to, double w) {
					double d = to - from;
					while (d > 180.0) d -= 360.0;
					while (d < -180.0) d += 360.0;
					return from + d * smoothstep(0.0, 1.0, w);
				};
				if (i > 0 && half > 0.0 && left < half) a.yaw = blend(headings[i - 1], headings[i], 0.5 + 0.5 * left / half);
				const double halfNext = i + 1 < lengths.size() ? 0.5 * std::min({turn, l, lengths[i + 1]}) : 0.0;
				if (halfNext > 0.0 && l - left < halfNext) a.yaw = blend(headings[i], headings[i + 1], 0.5 - 0.5 * (l - left) / halfNext);
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
	// The footfalls in (from, to]: one every stride along the path from firstStep strides in, at the time the walk reaches it.
	std::vector<Footfall> footfalls(double from, double to) const {
		std::vector<Footfall> out;
		const double total = length();
		if (speed <= 0.0 || stride <= 0.0) return out;
		for (int k = 1; (k - 1 + firstStep) * stride <= total + 1e-9; ++k) {
			const double d = (k - 1 + firstStep) * stride;
			const double t = start + d / speed;
			if (t <= from) continue;
			if (t > to) break;
			out.push_back({t, along(d).at, (k - 1) % 2, k});
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

// A gesture: a named pose from the character's library, eased in from `in0` to `in1`, held, and eased out from `out0` to `out1` (absolute film times; leave the out times at
// their default to hold it from then on). `contact` (when not negative) is the moment a hand or a foot meets something: a Touch event for the soundtrack (contactsOf).
//
//     const std::vector<rig::Gesture> acting = {{"reach to the wall", 99.4, 100.6, kForever, kForever, 100.4}, {"look up", 246.5, 248.5}};
//     pose = rig::act(pose, acting, t, explorerPoses());
constexpr double kForever = 1e30;
struct Gesture {
	std::string pose;
	double in0 = 0.0, in1 = 0.0, out0 = kForever, out1 = kForever;
	double contact = -1.0;
};
// How far into a gesture time t is, 0..1 (smoothly in, held, smoothly out).
inline double gestureWeight(const Gesture &g, double t) {
	const double in = g.in1 > g.in0 ? smoothstep(g.in0, g.in1, t) : (t >= g.in0 ? 1.0 : 0.0);
	if (g.out0 >= kForever) return in;
	const double out = g.out1 > g.out0 ? smoothstep(g.out0, g.out1, t) : (t >= g.out0 ? 1.0 : 0.0);
	return in * (1.0 - out);
}
// A character's library of poses: each one changes a pose of the character's own type by a weight (0 leaves it, 1 is the whole pose), given the time since the gesture
// began (a nod, a wave: poses that move).
template <class P>
using PoseLibrary = std::map<std::string, std::function<void(P &, double, double)>>;
// A pose with its gestures at time t, in their order (a later one on top of an earlier). A gesture whose pose is not in the library is an error, said by name.
template <class P>
inline P act(P pose, const std::vector<Gesture> &gestures, double t, const PoseLibrary<P> &library) {
	for (const Gesture &g : gestures) {
		const double w = gestureWeight(g, t);
		if (w <= 0.0) continue;
		const auto it = library.find(g.pose);
		if (it == library.end()) throw std::invalid_argument("no pose called \"" + g.pose + "\" in the character's library");
		it->second(pose, w, t - g.in0);
	}
	return pose;
}
// The gestures' contact moments in [from, to): when a hand or a foot meets something.
inline std::vector<double> contactsOf(const std::vector<Gesture> &gestures, double from = -kForever, double to = kForever) {
	std::vector<double> out;
	for (const Gesture &g : gestures)
		if (g.contact >= 0.0 && g.contact >= from && g.contact < to) out.push_back(g.contact);
	return out;
}

}  // namespace rig
}  // namespace cartoon
