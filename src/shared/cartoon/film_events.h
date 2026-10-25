#pragma once
// film_events.h -- a film's motion as a list of events its soundtrack hears (Film::events, FilmEvent in cartoon_lib.h): the footfalls of a walk, a thing landing, a flash, a hand
// laid on something. The film makes the list from the same walks and moments its shots use, so a walk that is retimed retimes its footsteps; sfx.h plays the list.
//
//     std::vector<FilmEvent> events() {
//         std::vector<FilmEvent> out;
//         film_events::footfalls(out, gardenWalk(), 12.0, 20.0, 0.8, viewAt);      // each foot that comes down in the garden shot, panned where the camera sees it
//         film_events::at(out, 31.2, EventKind::Impact, 1.0, ballLanding(), viewAt); // the ball lands
//         return film_events::sorted(out);
//     }
//
// `viewAt(t)` is the camera at film time t (a film_kit film: its moment's view). An event's strength falls off with its distance from the camera (`closeBy` units and nearer: full).

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

#include "cartoon_lib.h"
#include "rig.h"

namespace cartoon {
namespace film_events {

using ViewAt = std::function<CameraView(double)>;

// Where a point is heard, -1 (the left edge of the picture) to 1 (the right edge), held a little in from the ends: the camera's own right in story space (+x is to the right of
// the picture). `aspect` is the picture's width over its height (the fov is across the shorter side). A point behind the camera is heard in the middle.
inline double panOf(const CameraView &view, Vec3 at, double aspect = 16.0 / 9.0) {
	const Vec3 f = normalize(view.target - view.eye);
	const Vec3 right = normalize(cross(f, Vec3{0.0, 1.0, 0.0}));
	const Vec3 d = at - view.eye;
	const double depth = dot(d, f);
	if (depth <= 1e-6) return 0.0;
	const double half = std::tan(view.fov * kPi / 360.0) * std::max(1.0, aspect);
	return std::clamp(dot(d, right) / (depth * half), -1.0, 1.0) * 0.8;
}
// How loud a sound from `distance` away is, 0..1: full within `closeBy`, then falling (as the distance to the 0.7), never below a tenth.
inline double audibility(double distance, double closeBy) { return distance <= closeBy ? 1.0 : std::max(0.1, std::pow(closeBy / distance, 0.7)); }

// One event at time t, at a place in the story (panned and quietened from the camera at t when `viewAt` is given).
inline void at(std::vector<FilmEvent> &out, double t, EventKind kind, double strength, Vec3 where, const ViewAt &viewAt = {}, int seed = 0, double closeBy = 3.0) {
	FilmEvent e;
	e.t = t;
	e.kind = kind;
	e.strength = strength;
	e.seed = seed;
	if (viewAt) {
		const CameraView v = viewAt(t);
		e.pan = panOf(v, where);
		e.strength *= audibility(length(where - v.eye), closeBy);
	}
	out.push_back(e);
}

// The footfalls of a walk in (from, to] (rig::PathWalk::footfalls): where each foot came down, `lift` above its path point (the ground is the film's own).
inline void footfalls(std::vector<FilmEvent> &out, const rig::PathWalk &walk, double from, double to, double strength, const ViewAt &viewAt = {},
					  const std::function<Vec3(Vec3)> &onGround = {}, double closeBy = 3.0) {
	for (const rig::PathWalk::Footfall &s : walk.footfalls(from, to)) at(out, s.t, EventKind::Footfall, strength, onGround ? onGround(s.at) : s.at, viewAt, s.step, closeBy);
}

// The footfalls of something whose distance walked is known as a function of time - a body moved along a curve, not a rig::PathWalk - in (from, to]: one each time the
// distance passes a step (firstStep strides in, then a stride apart), found to a 240th of a second. `where(t)` is where it is.
inline void stepsAlong(std::vector<FilmEvent> &out, const std::function<double(double)> &distance, const std::function<Vec3(double)> &where, double from, double to,
					   double stride, double firstStep, double strength, const ViewAt &viewAt = {}, double closeBy = 3.0) {
	const double dt = 1.0 / 240.0;
	double before = distance(from);
	int step = std::max(0, static_cast<int>(std::floor((before / stride) - firstStep + 1.0)));   // (the steps already made)
	for (double t = from + dt; t <= to + 1e-9; t += dt) {
		const double now = distance(t);
		const double next = (step + firstStep) * stride;   // the distance of the next step
		if (now >= next && now > before) {
			const double tt = t - dt + dt * (next - before) / (now - before);
			++step;
			at(out, tt, EventKind::Footfall, strength, where(tt), viewAt, step, closeBy);
		}
		before = now;
	}
}

// The list in time order (what Film::events returns).
inline std::vector<FilmEvent> sorted(std::vector<FilmEvent> events) {
	std::stable_sort(events.begin(), events.end(), [](const FilmEvent &a, const FilmEvent &b) { return a.t < b.t; });
	return events;
}

}  // namespace film_events
}  // namespace cartoon
