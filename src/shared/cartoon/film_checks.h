#pragma once
// film_checks.h -- checks of a film's motion that need no render: sliding feet, and screen direction across a cut (below). Sliding feet: a foot the walk means to be on the ground (Film::feetAt, Foot::planted) should stay where it
// came down; one that moves while planted skates. Each shot is sampled at the film's frame rate, and the fastest a planted foot moves between two frames is reported
// (`ray_tracer --film-feet <key>`, and the film doctor warns on it).

#include <algorithm>
#include <string>
#include <vector>

#include "cartoon_lib.h"
#include "film_subjects.h"

namespace cartoon {
namespace film_checks {

// What the sliding-feet check found in one shot.
struct Slide {
	int shot = 0;            // from 0
	double worst = 0.0;      // the fastest a planted foot moved, units a second
	double at = 0.0;         // when
	double mean = 0.0;       // its mean over the planted frames
	int planted = 0;         // how many (foot, frame) pairs were planted on both frames
};

// A planted foot faster than this (units a second) skates visibly: 10 cm a second, a fifth of a slow walk.
constexpr double kSlideLimit = 0.10;

// Every shot in which a foot is planted at some frame, with how fast the planted feet moved. Shots with no planted feet are left out.
inline std::vector<Slide> slidingFeet(const Film &film, int fps = 0) {
	std::vector<Slide> out;
	if (!film.feetAt) return out;
	const double dt = 1.0 / std::max(1, fps > 0 ? fps : film.fps);
	const std::vector<double> starts = film.shotStarts ? film.shotStarts() : std::vector<double>{0.0};
	for (std::size_t s = 0; s < starts.size(); ++s) {
		const double end = s + 1 < starts.size() ? starts[s + 1] : film.duration;
		Slide slide;
		slide.shot = static_cast<int>(s);
		double sum = 0.0;
		std::vector<Foot> before = film.feetAt(starts[s]);
		for (double t = starts[s] + dt; t < end - 1e-9; t += dt) {
			const std::vector<Foot> now = film.feetAt(t);
			for (std::size_t i = 0; i < std::min(before.size(), now.size()); ++i) {
				if (!before[i].planted || !now[i].planted) continue;
				const double speed = length(now[i].at - before[i].at) / dt;
				sum += speed;
				++slide.planted;
				if (speed > slide.worst) {
					slide.worst = speed;
					slide.at = t;
				}
			}
			before = now;
		}
		if (slide.planted == 0) continue;
		slide.mean = sum / slide.planted;
		out.push_back(slide);
	}
	return out;
}

// ---- screen direction across a cut ----
// At each cut, what the shots on either side are about (Film::subjectsAt: subject 0, and subject 1 when both shots name two) is taken through each side's own camera
// (film_subjects.h). Two things confuse an audience: the same moving thing going left-to-right before the cut and right-to-left after it (a "flip": for one thing in one place a
// warning; for a traveller cut to another place on the journey a note - a journey should keep its direction across the screen), and two subjects that swap sides on the
// screen (the camera crossed the line between them).
struct CutIssue {
	int shot = 0;          // the shot after the cut, from 0
	bool samePlace = false;// the subject is where it was (one thing, one place) rather than somewhere else on its way
	bool crossedLine = false;   // two subjects swapped sides (else: a flip of the direction of motion)
	double before = 0.0, after = 0.0;   // the screen motion (picture widths a second, + to the right) or the subjects' order (x of 0 minus x of 1, in widths)
};
constexpr double kScreenSpeed = 0.02;   // screen motion slower than this (picture widths a second) has no direction to keep
constexpr double kSamePlace = 1.0;      // a subject this near (story units) to the one before the cut is the same thing in the same place

inline std::vector<CutIssue> screenDirection(const Film &film) {
	std::vector<CutIssue> out;
	if (!film.subjectsAt || !film.headerAt || !film.shotStarts) return out;
	const int width = film.meta.width, height = film.meta.height;
	const std::vector<double> starts = film.shotStarts();
	const double dt = 1.0 / std::max(1, film.fps), eps = 1e-4;
	// where subject i is on the screen at time t (the camera of `at`'s moment), and whether it is in front
	const auto screen = [&](double at, double t, std::size_t i, double &x, Vec3 &world) {
		Vec3 eye, target;
		double fov = 90.0;
		if (!film_subjects::cameraOf(film.headerAt(at), eye, target, fov)) return false;
		const std::vector<Vec3> s = film.subjectsAt(t);
		if (s.size() <= i) return false;
		world = s[i];
		const film_subjects::View v = film_subjects::project(eye, target, fov, s[i], width, height);
		x = v.x / std::max(1, width);
		return v.inFront;
	};
	for (std::size_t k = 1; k < starts.size(); ++k) {
		const double cut = starts[k];
		const double a0 = cut - eps - 4.0 * dt, a1 = cut - eps, b0 = cut, b1 = cut + 4.0 * dt;
		if (a0 <= starts[k - 1] || (k + 1 < starts.size() && b1 >= starts[k + 1])) continue;   // (a shot too short to show a direction)
		// subject 0's motion on each side, each in its own camera (the camera of the moment held for both instants, so a camera move is not taken for the subject's)
		double xa0 = 0, xa1 = 0, xb0 = 0, xb1 = 0;
		Vec3 wa0, wa1, wb0, wb1;
		if (!screen(a1, a0, 0, xa0, wa0) || !screen(a1, a1, 0, xa1, wa1) || !screen(b0, b0, 0, xb0, wb0) || !screen(b0, b1, 0, xb1, wb1)) continue;
		const double before = (xa1 - xa0) / (4.0 * dt), after = (xb1 - xb0) / (4.0 * dt);
		// (a direction across the screen only when the motion is more across the view than into it or out of it: a walk away from the camera has none to keep)
		const auto across = [&](double at, Vec3 motion) {
			Vec3 eye, target;
			double fov = 90.0;
			if (!film_subjects::cameraOf(film.headerAt(at), eye, target, fov) || length(motion) < 1e-9) return false;
			const Vec3 f = normalize(target - eye), right = normalize(cross(f, Vec3{0.0, 1.0, 0.0}));
			return std::abs(dot(motion, right)) > std::abs(dot(motion, f));
		};
		if (across(a1, wa1 - wa0) && across(b0, wb1 - wb0) && std::abs(before) > kScreenSpeed && std::abs(after) > kScreenSpeed && (before > 0) != (after > 0)) {
			CutIssue c;
			c.shot = static_cast<int>(k);
			c.samePlace = length(wb0 - wa1) < kSamePlace;
			c.before = before;
			c.after = after;
			out.push_back(c);
		}
		// two subjects on both sides, the same two (each near its own across the cut): do they keep their sides?
		double ya = 0, yb = 0;
		Vec3 va, vb;
		if (screen(a1, a1, 1, ya, va) && screen(b0, b0, 1, yb, vb) && length(vb - va) < kSamePlace && length(wb0 - wa1) < kSamePlace) {
			const double orderA = xa1 - ya, orderB = xb0 - yb;
			if (std::abs(orderA) > 0.02 && std::abs(orderB) > 0.02 && (orderA > 0) != (orderB > 0)) {
				CutIssue c;
				c.shot = static_cast<int>(k);
				c.samePlace = true;
				c.crossedLine = true;
				c.before = orderA;
				c.after = orderB;
				out.push_back(c);
			}
		}
	}
	return out;
}

}  // namespace film_checks
}  // namespace cartoon
