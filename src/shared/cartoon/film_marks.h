#pragma once
// film_marks.h -- a film file's cartoon marks (`mark`): the signs drawn by a character - !, ?, a drop of sweat, an anger mark, hearts, zzz, sparkles - speed lines behind it
// as it moves, and the star of an impact, drawn over the finished frame as a 2D layer (src/shared/screen_marks.h: Blender's Grease Pencil drawn over a render).
//
//     mark pip exclaim 4.2            # a "!" pops up over Pip's head at 4.2 s (and goes 1.8 s later)
//     mark ann question 6 8.5         # from 6 s to 8.5 s
//     mark pip speed 10 12            # speed lines behind Pip while it runs, 10 s to 12 s
//     mark at 0.5 0.4 -2 impact 7.3   # an impact's star at a place (pbrt's coordinates)
//     mark kim heart 3 colour red size 1.3
//
// Kinds: exclaim (or !), question (or ?), sweat, anger, heart, zzz, sparkle, impact, speed. A sign by a character follows its head (over it; sweat and anger beside it) and is
// as big as the character is on the screen; speed lines trail its chest the way it moves on the screen, and only while it moves. Each frame's marks are worked out from
// the moment's camera and written as the frame's `# @rt-mark` lines (film_world.cpp), which the launcher draws after the render and the toon look.

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

#include "../screen_marks.h"
#include "cartoon_lib.h"

namespace cartoon {
namespace film_marks {

struct Mark {
	std::string who;    // the character it is by ("": at a place)
	std::string kind;   // screen_marks::kinds()
	double t0 = 0.0, t1 = 0.0;
	Vec3 at;            // (pbrt's coordinates) a mark at a place
	Vec3 colour{-1.0, -1.0, -1.0};   // (linear; negative: the kind's own)
	double size = 1.0;
	int line = 0;
};

// The kind a film file's word names ("!" and "?" for exclaim and question), or "" when it names none.
inline std::string kindOf(const std::string &word) {
	if (word == "!") return "exclaim";
	if (word == "?") return "question";
	const std::vector<std::string> &k = screen_marks::kinds();
	return std::find(k.begin(), k.end(), word) != k.end() ? word : std::string();
}
// How long a mark stays when its line gives no end.
inline double defaultLife(const std::string &kind) { return kind == "impact" ? 0.45 : kind == "speed" ? 1.0 : 1.8; }

// The moment's camera (story space, as film_subjects::cameraOf gives it; its fov across the picture's shorter side).
struct Camera {
	Vec3 eye, target;
	double fov = 40.0;
};
// Where a point (story space) is on the screen: from the centre in half the shorter side, +x right, +y up (screen_marks.h); false when it is behind the camera.
inline bool toScreen(const Camera &c, Vec3 p, double &x, double &y) {
	const Vec3 f = normalize(c.target - c.eye), right = normalize(cross(f, Vec3{0.0, 1.0, 0.0})), up = cross(right, f), d = p - c.eye;
	const double depth = dot(d, f), tanHalf = std::tan(c.fov * kPi / 360.0);
	if (depth < 1e-3) return false;
	x = dot(d, right) / (depth * tanHalf), y = dot(d, up) / (depth * tanHalf);
	return true;
}

// Where a character is at a moment (story space): its head, the top of it (its kind's height), its feet (its frame's origin) and its chest; false when there is no such
// character.
using PoseAt = std::function<bool(const std::string &who, double t, Vec3 &head, Vec3 &top, Vec3 &feet, Vec3 &chest)>;

// The frame's `# @rt-mark` lines at film time t.
inline std::string headerLines(const std::vector<Mark> &marks, double t, const Camera &cam, const PoseAt &poseAt) {
	std::string out;
	for (std::size_t i = 0; i < marks.size(); ++i) {
		const Mark &mk = marks[i];
		if (t < mk.t0 || t > mk.t1) continue;
		screen_marks::Mark s;
		s.kind = mk.kind, s.age = t - mk.t0, s.life = mk.t1 - mk.t0, s.seed = mk.line * 31 + static_cast<int>(i);
		if (mk.colour.x >= 0.0) s.colour[0] = mk.colour.x, s.colour[1] = mk.colour.y, s.colour[2] = mk.colour.z;
		else screen_marks::defaultColour(mk.kind, s.colour);
		if (mk.who.empty()) {   // at a place: a quarter of a metre across at its distance
			const Vec3 p{-mk.at.x, mk.at.y, mk.at.z};
			double x = 0.0, y = 0.0, x2 = 0.0, y2 = 0.0;
			if (!toScreen(cam, p, x, y) || !toScreen(cam, p + Vec3{0.0, 0.5, 0.0}, x2, y2)) continue;
			s.x = x, s.y = y, s.size = std::clamp(std::hypot(x2 - x, y2 - y) * mk.size, 0.02, 1.0);
			out += screen_marks::lineOf(s);
			continue;
		}
		Vec3 head, top, feet, chest;
		if (!poseAt(mk.who, t, head, top, feet, chest)) continue;
		double hx = 0.0, hy = 0.0, tx = 0.0, ty = 0.0, fx = 0.0, fy = 0.0;
		if (!toScreen(cam, head, hx, hy) || !toScreen(cam, top, tx, ty) || !toScreen(cam, feet, fx, fy)) continue;
		const double tall = std::max(0.05, std::hypot(tx - fx, ty - fy));   // (the character's height on the screen)
		if (mk.kind == "speed") {
			Vec3 h0, t0, f0, c0;
			double cx = 0.0, cy = 0.0, px = 0.0, py = 0.0;
			const double dt = 0.1;
			if (!poseAt(mk.who, t - dt, h0, t0, f0, c0) || !toScreen(cam, chest, cx, cy) || !toScreen(cam, c0, px, py)) continue;
			const double vx = (cx - px) / dt, vy = (cy - py) / dt;
			if (std::hypot(vx, vy) < 0.25 * tall) continue;   // (standing still, or nearly: no lines)
			s.x = cx, s.y = cy, s.angle = std::atan2(-vy, -vx), s.size = std::clamp(0.9 * tall * mk.size, 0.02, 1.5);
		} else if (mk.kind == "impact") {
			if (!toScreen(cam, chest, s.x, s.y)) continue;
			s.size = std::clamp(0.6 * tall * mk.size, 0.02, 1.5);
		} else {
			// over the top of it, or beside its head (sweat, anger)
			const bool beside = mk.kind == "sweat" || mk.kind == "anger";
			const double ux = (tx - fx) / tall, uy = (ty - fy) / tall;   // (the character's up on the screen: a sign leans with a leaning camera)
			const double ax = beside ? hx : tx, ay = beside ? hy : ty, along = beside ? 0.05 : 0.13, aside = beside ? 0.2 : 0.0;
			s.x = ax + tall * (along * ux + aside * uy), s.y = ay + tall * (along * uy - aside * ux);
			s.size = std::clamp((beside ? 0.13 : 0.2) * tall * mk.size, 0.015, 0.6);
		}
		out += screen_marks::lineOf(s);
	}
	return out;
}

}  // namespace film_marks
}  // namespace cartoon
