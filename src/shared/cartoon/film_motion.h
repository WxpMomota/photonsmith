#pragma once
// film_motion.h -- the cartoon's ways of moving, for film files' characters (animation's principles as Blender's animators get them):
//
//   animate   on ones, twos or threes: a character holds each pose for one, two or three frames while the camera moves smoothly every frame - the hand-drawn feel (and
//             Spider-Verse's), Blender's Stepped Interpolation F-curve modifier. `animate on twos` for the whole film, `animate pip on threes from 10 to 14` for one
//             character for a while (the latest line that covers a moment wins, a character's own over the film's).
//   squash    squash and stretch: a character stretches along the way it moves (more the faster: a smear when it dashes) and, carried by a bouncy spring on its body,
//             stretches as it falls, squashes as it lands and wobbles as it settles - keeping its volume. `squash 1` for every character (0 off, up to 2), `squash pip 1.5`.
//   jiggle    follow-through: what dangles from a character (Pip's antennae) swings behind its head on a looser spring and settles - Blender artists' wiggle bones.
//             `jiggle 1`, `jiggle pip 1.5` (0 off, up to 3).
//   smear     smear frames, as Blender Studio's animators draw them for a dash: the character drawn out back along its way over S frames' travel (`smear 1`, `smear pip
//             1.5`; 0 off, up to 3), and with `multiples N` (up to 4) copies of it left along that way - the cartoon's multiples. Only at a dash's speed (from 2.5
//             heights a second, all of it from 5): a walk or a run is drawn as it is.
//
// All are off unless the film asks: a film without these lines draws exactly as before. A frame is still a function of its time: the springs run over the second before t
// (rig::follow).

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "cartoon_lib.h"
#include "rig.h"

namespace cartoon {
namespace motion {

// `animate [NAME] on ones|twos|threes [from T0 to T1]`
struct Stepping {
	std::string who;   // "": every character
	int every = 2;
	double t0 = -1e30, t1 = 1e30;
};
// `squash [NAME] S`, `jiggle [NAME] S`
struct Amount {
	std::string who;
	double amount = 1.0;
};
// `smear [NAME] S [multiples N]`
struct Smear {
	std::string who;
	double amount = 1.0;   // how many frames' travel the smear draws out behind it (0..3)
	int multiples = 0;     // copies of it left along that way (0..4)
};

// A character's smear rule: its own line's, else the film's; null for none.
inline const Smear *smearFor(const std::vector<Smear> &rules, const std::string &who) {
	const Smear *all = nullptr, *own = nullptr;
	for (const Smear &r : rules) {
		if (r.who.empty()) all = &r;
		else if (r.who == who) own = &r;
	}
	return own ? own : all;
}

// How much a smear is drawn at a speed (heights a second): none below 2.5, all of it from 5 - a run is not smeared, a dash is.
inline double smearWeight(double heightsPerSecond) { return std::clamp((heightsPerSecond - 2.5) / 2.5, 0.0, 1.0); }

// The time a character is drawn at for film time t: the frame it holds (the frame at or before t that is a multiple of its step, counted from the rule's start).
inline double heldTime(const std::vector<Stepping> &rules, const std::string &who, double t, int fps) {
	const Stepping *rule = nullptr;
	for (const Stepping &r : rules) {
		if (t < r.t0 || t > r.t1 || (!r.who.empty() && r.who != who)) continue;
		if (!rule || !r.who.empty() || rule->who.empty()) rule = &r;   // (a character's own wins over the film's; a later line over an earlier)
	}
	if (!rule || rule->every <= 1 || fps <= 0) return t;
	const long frame = static_cast<long>(std::floor(t * fps + 1e-6));
	const long start = rule->t0 > -1e29 ? static_cast<long>(std::floor(rule->t0 * fps + 1e-6)) : 0;
	long since = frame - start;
	since -= ((since % rule->every) + rule->every) % rule->every;   // (down to the step's start, negative or not)
	return std::max(rule->t0 > -1e29 ? rule->t0 : -1e30, static_cast<double>(start + since) / fps);
}

// How much of a way of moving a character has: its own line's, else the film's, else 0.
inline double amountFor(const std::vector<Amount> &rules, const std::string &who) {
	double all = 0.0, own = -1.0;
	for (const Amount &r : rules) {
		if (r.who.empty()) all = r.amount;
		else if (r.who == who) own = r.amount;
	}
	return own >= 0.0 ? own : all;
}

// A stretch by k along the unit direction d and by 1/sqrt(k) across it (the volume kept), about the point p.
inline Mat4 stretchAbout(Vec3 p, Vec3 d, double k) {
	const double a = k - 1.0, b = 1.0 / std::sqrt(k) - 1.0;   // M = I + a dd' + b (I - dd')
	const double m[3][3] = {{1.0 + b + (a - b) * d.x * d.x, (a - b) * d.x * d.y, (a - b) * d.x * d.z},
							{(a - b) * d.y * d.x, 1.0 + b + (a - b) * d.y * d.y, (a - b) * d.y * d.z},
							{(a - b) * d.z * d.x, (a - b) * d.z * d.y, 1.0 + b + (a - b) * d.z * d.z}};
	Mat4 M = {m[0][0], m[0][1], m[0][2], 0.0, m[1][0], m[1][1], m[1][2], 0.0, m[2][0], m[2][1], m[2][2], 0.0, 0.0, 0.0, 0.0, 1.0};
	return mul(translate(p), M, translate(p * -1.0));
}

// The squash and stretch of a character whose place over time is `placeAt` (story space; its feet), `height` tall, `amount` strong: a deformation of the world to put
// before its frame (frame' = squash * frame). Stretched along its velocity (by up to a third, at 5 m/s: a smear), and along the vertical by a bouncy spring that follows
// its body's middle as it comes to rest: landing, the spring runs on below it (a squash) and wobbles back.
inline Mat4 squashStretch(const std::function<Vec3(double)> &placeAt, double t, double height, double amount) {
	if (amount <= 0.0) return identity();
	const double dt = 1.0 / 60.0;
	const Vec3 at = placeAt(t), v = (placeAt(t + dt) - placeAt(t - dt)) * (1.0 / (2.0 * dt));
	const double size = std::max(0.1, height);
	const Vec3 middle = at + Vec3{0.0, 0.5 * size, 0.0};
	Mat4 out = identity();
	const double speed = length(v) / size * 1.8;   // (in heights a second: a small thing that runs smears as much as a big one)
	if (speed > 0.05) out = stretchAbout(middle, normalize(v), 1.0 + amount * std::min(0.33, 0.06 * speed));
	static const rig::Spring bouncy{180.0, 9.0, 1.0, 1.0 / 240.0};
	const Vec3 lag = rig::lag(bouncy, [&](double u) { return placeAt(u) + Vec3{0.0, 0.5 * size, 0.0}; }, t);
	// (only as it comes to rest: a landing, a stop. While it moves the velocity's stretch says it - the spring lagging below a body that takes off would squash it
	// just as it should stretch)
	const double resting = 1.0 - std::clamp((speed - 0.3) / 0.6, 0.0, 1.0);
	const double k = 1.0 + amount * resting * std::clamp(2.5 * lag.y / size, -0.25, 0.25);
	if (std::fabs(k - 1.0) > 1e-4) out = mul(stretchAbout(at, {0.0, 1.0, 0.0}, k), out);   // (about the feet: it squashes onto the ground)
	return out;
}

// The smear frame's stretch of a character whose place over time is `placeAt` (story space; its feet), `height` tall: on a dash its body drawn out back along its way,
// over `amount` frames' travel (at `fps`), from its leading side (which stays where it is), thinner across as it lengthens (its volume kept) - the elongated in-between
// of Blender Studio's smear frames. None for a slower move (smearWeight).
inline Mat4 smearStretch(const std::function<Vec3(double)> &placeAt, double t, double height, double amount, int fps) {
	if (amount <= 0.0 || fps <= 0) return identity();
	const double dt = 1.0 / 120.0, size = std::max(0.1, height);
	const Vec3 at = placeAt(t), v = (placeAt(t + dt) - placeAt(t - dt)) * (1.0 / (2.0 * dt));
	const double w = smearWeight(length(v) / size);
	if (w <= 0.0) return identity();
	const Vec3 d = normalize(v);
	const Vec3 lead = at + Vec3{0.0, 0.5 * size, 0.0} + d * (0.5 * size);
	const double trail = w * amount * length(v) / fps;   // (the way it went in `amount` frames)
	return stretchAbout(lead, d, std::min(3.0, 1.0 + trail / size));
}

// The times a smear's multiples are drawn at: `count` copies of the character back along the way it went over `amount` frames, evenly - on a dash only (smearWeight
// past a half, so that copies do not flicker in and out at the edge of it).
inline std::vector<double> smearTimes(const std::function<Vec3(double)> &placeAt, double t, double height, double amount, int fps, int count) {
	std::vector<double> out;
	if (amount <= 0.0 || fps <= 0 || count <= 0) return out;
	const double dt = 1.0 / 120.0, size = std::max(0.1, height);
	const Vec3 v = (placeAt(t + dt) - placeAt(t - dt)) * (1.0 / (2.0 * dt));
	if (smearWeight(length(v) / size) < 0.5) return out;
	for (int i = 1; i <= count; ++i) out.push_back(t - amount * i / (static_cast<double>(fps) * count));
	return out;
}

// How far what dangles from a point (its head) swings behind it at t: the lag of a loose spring following it (story space), scaled by `amount`. `bob` is the walk's own
// up-and-down at a time, so a walk sets the antennae bouncing.
inline Vec3 jiggleOf(const std::function<Vec3(double)> &pointAt, double t, double amount) {
	if (amount <= 0.0) return {};
	static const rig::Spring loose{70.0, 4.5, 1.2, 1.0 / 240.0};
	return rig::lag(loose, pointAt, t) * (2.5 * amount);   // (a loose antenna swings further than its root lags)
}

// A character's scene text (pbrt's, written from story space: x negated) under a deformation `M` of story space: wrapped in the matching pbrt transform.
inline std::string wrapped(const std::string &text, const Mat4 &M) {
	bool identityM = true;
	for (int i = 0; i < 16; ++i) identityM = identityM && std::fabs(M[i] - (i % 5 == 0 ? 1.0 : 0.0)) < 1e-9;
	if (identityM) return text;
	std::string out = "AttributeBegin   # squash and stretch (film_motion.h)\n  ConcatTransform [";
	char buf[32];
	for (int col = 0; col < 4; ++col)
		for (int row = 0; row < 4; ++row) {   // (pbrt reads the 16 numbers a column at a time; x is negated on both sides: F M F)
			const double s = (row == 0 ? -1.0 : 1.0) * (col == 0 ? -1.0 : 1.0);
			std::snprintf(buf, sizeof buf, " %.6g", s * M[row * 4 + col]);
			out += buf;
		}
	return out + " ]\n" + text + "AttributeEnd\n";
}

}  // namespace motion
}  // namespace cartoon
