#pragma once
// ease_curves.h -- how a move gets from its start to its end over time: the easing curves of Blender's keyframe interpolation (blenlib/intern/easing.cc, after
// Robert Penner's equations; our own code): smooth (ease in and out), linear, in, out, back (it overshoots a little and settles: anticipation and follow-through),
// elastic (it springs past and back several times) and bounce (it drops onto its end and bounces, smaller each time). Shared by film files' motions
// (cartoon_lib.h Ease, film_file.h `ease`) and camera paths (camera_keyframes.h `ease back`).

#include <cmath>
#include <string>

namespace ease_curves {

// Where a move is at u (0..1 of its time), 0 at the start and 1 at the end; back and elastic go past 1 on the way.
inline double at(const std::string &curve, double u) {
	u = u < 0.0 ? 0.0 : (u > 1.0 ? 1.0 : u);
	if (curve == "linear") return u;
	if (curve == "in") return u * u;
	if (curve == "out") return 1.0 - (1.0 - u) * (1.0 - u);
	if (curve == "back") {
		const double c1 = 1.70158, c3 = c1 + 1.0, v = u - 1.0;
		return 1.0 + c3 * v * v * v + c1 * v * v;
	}
	if (curve == "elastic") {
		if (u <= 0.0 || u >= 1.0) return u;
		return std::pow(2.0, -9.0 * u) * std::sin((u * 10.0 - 0.75) * (2.0 * 3.14159265358979323846 / 3.0)) + 1.0;
	}
	if (curve == "bounce") {   // (Penner's ease-out bounce: four arcs, each a quarter as high)
		const double n1 = 7.5625, d1 = 2.75;
		if (u < 1.0 / d1) return n1 * u * u;
		if (u < 2.0 / d1) {
			u -= 1.5 / d1;
			return n1 * u * u + 0.75;
		}
		if (u < 2.5 / d1) {
			u -= 2.25 / d1;
			return n1 * u * u + 0.9375;
		}
		u -= 2.625 / d1;
		return n1 * u * u + 0.984375;
	}
	return u * u * (3.0 - 2.0 * u);   // smooth
}

// Whether `word` names a curve.
inline bool known(const std::string &word) {
	return word == "smooth" || word == "linear" || word == "in" || word == "out" || word == "back" || word == "elastic" || word == "bounce";
}

}  // namespace ease_curves
