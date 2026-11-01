#pragma once
// film_cosmic.h -- a film's `event KIND ...`: great things that happen in the sky, worked out and not painted. What they are and how they are drawn is cosmic_event.h, on the finished picture;
// this is the film's side of it: where one is, when, and the words a frame's look line needs.
//
// Coordinates: pbrt's. Their own time is `over` film seconds, from `from`, whatever the billions of years it stands for.

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "../cosmic_event.h"
#include "cartoon_lib.h"

namespace cartoon {
namespace events {

struct Event {
	std::string kind = "collision";
	Vec3 at{0.0, 0.0, -40000.0};
	double size = 40000.0, t0 = 0.0, span = 20.0, brightness = 1.0, tilt = 25.0;
	int count = 12000, seed = 1;
};
inline const std::vector<std::string> &kinds() {
	static const std::vector<std::string> k = {"collision", "disc", "merger", "web", "stars"};
	return k;
}
inline int kindOf(const std::string &name) {
	const auto &k = kinds();
	return static_cast<int>(std::find(k.begin(), k.end(), name) - k.begin());
}

// The look line's words for an event at t, seen from `eye` looking at `target` (pbrt's), `fov` degrees (lens_look.h, cosmic_event.h). Nothing before it starts.
inline std::string eventLook(const Event &e, const Vec3 &eye, const Vec3 &target, double fov, double t) {
	if (t < e.t0 - 1e-6) return std::string();
	char words[420];
	std::snprintf(words, sizeof words, " event 1 event-kind %d event-count %d event-seed %d event-x %.3f event-y %.3f event-z %.3f event-size %.3f event-u %.6f event-tilt %.3f event-bright %.3f event-eye-x %.3f event-eye-y %.3f event-eye-z %.3f event-target-x %.3f event-target-y %.3f event-target-z %.3f event-fov %.3f",
				  kindOf(e.kind), e.count, e.seed, e.at.x, e.at.y, e.at.z, e.size, std::clamp((t - e.t0) / std::max(1e-6, e.span), 0.0, 1.0), e.tilt, e.brightness, eye.x, eye.y, eye.z, target.x, target.y, target.z, fov);
	return words;
}

}  // namespace events
}  // namespace cartoon
