#pragma once
// nomai_explorer.h -- the explorer of "The Last Nomai Signal" (nomai_signal.h): an original Nomai, made after the game's own (Mobius Digital's concept art of the Nomai
// suit, the in-game suit of Solanum, the statue in the Observatory, the wiki): tall and slender (1.86 m), digitigrade, with a goat-like face, a wide nose and a long
// flowing beard, three amber eyes with the goat's level oblong pupils (two below, one on the forehead), two horns curling back like a ram's and a top antler that forks
// into two points, short tan fur, small three-fingered hands. Its suit, mask and staff are our own designs in the Nomai's spirit (a pale mask with three eye windows,
// warm metal, a ghostly blue light, a long robe): a sea-green twill robe embroidered with a star chart, with rust-red embroidered borders (the opening, the hem, a
// rounded neckline, a waist band), bronze cuff bands and a beaded bronze collar, quilted umber leather pads on the shoulders, dark green trousers, big pale three-toed
// feet, a bronze pack on the back with a horizontal ivory tank, a hose to the mask; the mask a rounded ivory shield with three round windows in bronze bezels, tide rings
// and a glowing inlay round the third, silver stars on the brow, swept blades at the temples and a smooth cowl over the muzzle; and a sky-compass staff, two gimbal
// rings round a glowing crystal on a wooden shaft.
//
// The figure is sculpted for each frame from its pose (signed distance functions blended like clay, meshed by sdf::surfaceNets), so every part follows the pose: the robe's
// skirt swings with the long, slow steps of low gravity and stirs in the wind, each eyelid closes on its own, the third eye can open after the other two, the mask comes off
// into a hand and the beard falls free.
//
// Body space: the origin on the ground between the feet, +y up, the figure facing +z; +x is the figure's left. The head has a space of its own (its pivot at the top of the
// neck, +z forward), which the pose turns.

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

#include "film_sky.h"
#include "nature_lib.h"
#include "nomai_mesh.h"
#include "rig.h"
#include "sdf_mesh.h"

namespace cartoon {
namespace nomai {

struct ExplorerPose {
	double walk = 0.0;     // the walk cycle (a whole number of steps: each foot has stepped once at 2)
	double stride = 0.0;   // 0 standing .. 1 a full stride
	double slope = 0.0;    // the ground's rise per unit forward under the walk (negative: going down a ramp or a stair)
	bool lockFeet = false; // the balls of the feet are `lockedBall` (body space), held where they came down (rig::FootLock: explorerFootLock), not where the gait puts them
	Vec3 lockedBall[2];
	double sit = 0.0;      // 0 standing .. 1 sitting on the ground, legs folded
	double crouch = 0.0;   // 0 .. 1 knees bent, body lowered (reaching to the ground)
	double lean = 0.0;     // degrees forward
	Vec3 handL{0.20, 0.92, 0.06}, handR{-0.20, 0.92, 0.06};   // where the hands are (body space)
	double headYaw = 0.0, headPitch = 0.0, headTilt = 0.0;     // degrees: turn left (+), look down (+), tilt
	double gazeYaw = 0.0, gazePitch = 0.0;                     // the eyes, degrees, within the head
	double lidL = 0.0, lidR = 0.0, lidTop = 0.0;               // 0 open .. 1 closed (the two lower eyes, the third)
	double ears = 0.0;                                         // -1 drooping .. 1 lifted
	double maskOff = 0.0;                                      // 0 on the face .. 1 held down in the left hand
	bool maskShown = true;                                     // false: no mask (laid on the ground: maskOnGroundText)
	bool hood = true;
	int staff = -1;                                            // the hand that holds the staff: -1 right, 1 left, 0 none
	double staffGlow = 0.3;                                    // the staff's light, 0 .. 1
	Vec3 staffTip{0.0, 0.0, 0.0};                              // (not zero) where the staff's top is, body space; otherwise it stands in the hand
	Vec3 wind{0.0, 0.0, 0.0};                                  // the robe's flutter (metres at the hem)
	double time = 0.0;                                         // for the breath and the flutter
};

// The explorer's poses for gestures (rig::act): each blends what it changes towards its own values by the weight. Hand places are body space (+x its left, +z forward).
inline const rig::PoseLibrary<ExplorerPose> &explorerPoses() {
	static const rig::PoseLibrary<ExplorerPose> poses = {
		{"reach to the wall", [](ExplorerPose &p, double w, double) { p.handL = lerp(p.handL, Vec3{0.12, 1.50, 0.52}, w); }},
		{"hand on the lectern", [](ExplorerPose &p, double w, double) { p.handL = lerp(p.handL, Vec3{0.06, 1.10, 0.52}, w); }},
		{"point up", [](ExplorerPose &p, double w, double) { p.handL = lerp(p.handL, Vec3{0.30, 1.40, 0.45}, w); }},
		{"kneel and touch", [](ExplorerPose &p, double w, double) {
			 p.crouch = lerp(p.crouch, 1.0, w);
			 p.handL = lerp(p.handL, Vec3{0.18, 0.12, 0.42}, w);
			 p.headPitch = lerp(p.headPitch, 34.0, w);
		 }},
		{"look up", [](ExplorerPose &p, double w, double) { p.headPitch = lerp(p.headPitch, -35.0, w); }},
		{"look down", [](ExplorerPose &p, double w, double) { p.headPitch = lerp(p.headPitch, 30.0, w); }},
		{"surprised", [](ExplorerPose &p, double w, double) {   // a start back: leaning away, hands up, every eye wide, the ears up
			 p.lean = lerp(p.lean, -8.0, w);
			 p.handL = lerp(p.handL, Vec3{0.26, 1.30, 0.30}, w);
			 p.handR = lerp(p.handR, Vec3{-0.26, 1.30, 0.30}, w);
			 p.lidL = lerp(p.lidL, 0.0, w);
			 p.lidR = lerp(p.lidR, 0.0, w);
			 p.lidTop = lerp(p.lidTop, 0.0, w);
			 p.ears = lerp(p.ears, 1.0, w);
		 }},
		{"hands together", [](ExplorerPose &p, double w, double) {
			 p.handL = lerp(p.handL, Vec3{0.06, 1.02, 0.30}, w);
			 p.handR = lerp(p.handR, Vec3{-0.06, 1.02, 0.30}, w);
		 }},
		{"raise the sceptre", [](ExplorerPose &p, double w, double) {
			 p.handR = lerp(p.handR, Vec3{-0.20, 1.62, 0.30}, w);
			 p.staffGlow = lerp(p.staffGlow, 1.0, w);
		 }},
		{"nod", [](ExplorerPose &p, double w, double s) { p.headPitch += 12.0 * w * std::sin(2.0 * kPi * 1.4 * s); }},
		{"shake head", [](ExplorerPose &p, double w, double s) { p.headYaw += 18.0 * w * std::sin(2.0 * kPi * 1.6 * s); }},
		{"sit", [](ExplorerPose &p, double w, double) { p.sit = lerp(p.sit, 1.0, w); }},
		{"crouch", [](ExplorerPose &p, double w, double) { p.crouch = lerp(p.crouch, 1.0, w); }},
	};
	return poses;
}

// The explorer's blinks (rig::blink): the two lower eyes together every few seconds, the third on its own - and now and then a little after them.
inline void blink(ExplorerPose &p, double t) {
	p.lidL = p.lidR = std::max(p.lidL, rig::blink(t, 7));
	p.lidTop = std::max(p.lidTop, std::max(rig::blink(t - 0.14, 7), rig::blink(t, 8)));   // (the third eye: late, or on its own)
}
// A walking pose: long, slow strides, the staff in the right hand, the left arm swinging a little. `trail` (body space: +x its left, +z forward) is how far the robe's hem
// trails its body (robeTrail): it swings back as the explorer sets off and forward when it stops.
inline ExplorerPose walkPose(double walk, double t, double stride = 1.0, Vec3 trail = {0.0, 0.0, 0.0}) {
	ExplorerPose p;
	p.walk = walk;
	p.stride = stride;
	p.time = t;
	const double sw = std::sin(walk * kPi);
	p.handL = {0.21, 0.90, 0.06 - 0.10 * sw * stride};
	p.handR = {-0.25, 1.18, 0.16 + 0.06 * sw * stride};
	p.headPitch = 4.0;
	p.wind = Vec3{0.03, 0.0, -0.025} + trail;
	p.staffGlow = 0.22;
	blink(p, t);
	return p;
}
// How far the robe's hem trails the body (body space) at t, the body at `where(t)` (story space) facing `yaw(t)`: a loose spring on the body's place (rig::Spring), half of its
// lag put into the hem.
inline Vec3 robeTrail(const std::function<Vec3(double)> &where, double yawDegrees, double t) {
	static const rig::Spring hem{30.0, 5.0, 3.0, 1.0 / 240.0};
	const Vec3 lag = rig::lag(hem, where, t) * 0.5;
	const double a = yawDegrees * kPi / 180.0;
	const Vec3 left{std::cos(a), 0.0, -std::sin(a)}, forward{std::sin(a), 0.0, std::cos(a)};
	return {dot(lag, left), 0.0, dot(lag, forward)};
}
inline ExplorerPose standPose(double t) {
	ExplorerPose p = walkPose(0.0, t, 0.0);
	p.handR = {-0.25, 1.15, 0.14};
	return p;
}

// ---- the skeleton ---------------------------------------------------------------------------------------------------------------------------------------------------
struct ExplorerBody {
	Vec3 pelvis, chest, neck, headPivot, shoulder[2], elbow[2], wrist[2], hip[2], knee[2], hock[2], ball[2];
	Mat4 head, headInv;   // head space -> body space, and back
	Vec3 handDir[2];      // where each hand points (wrist to fingers)
	double hemY;          // the robe's hem
	bool planted[2];      // whether each foot is on the ground (the walk's stance)
};
// Where a foot is in its walk, `steps` steps into it (a step is half a cycle): on the ground from half a step in to a step and a half (`swing` going from 1 to -1 at an even pace:
// the foot's place ahead of the hip, in strides, moves back as the body moves on), then in the air for a step, swinging forward smoothly from -1 to 1 and lifted (`lift`,
// 0..1, highest half way).
inline void gait(double steps, double &swing, double &lift, bool &planted) {
	const double u = steps / 2.0 - std::floor(steps / 2.0);   // through the cycle, 0..1 (two steps)
	const double stance = u - 0.25 - std::floor(u - 0.25);    // 0..1, from the moment the foot comes down
	planted = stance < 0.5;
	if (planted) {
		swing = 1.0 - 4.0 * stance;
		lift = 0.0;
	} else {
		const double v = (stance - 0.5) / 0.5;   // through the swing, 0..1
		swing = -1.0 + 2.0 * smoothstep(0.0, 1.0, v);
		lift = std::sin(kPi * v);
	}
}
inline ExplorerBody explorerBody(const ExplorerPose &p) {
	ExplorerBody b;
	const double breath = 0.006 * std::sin(p.time * 2.0 * kPi / 4.2);
	const double drop = 0.58 * p.sit + 0.22 * p.crouch;
	b.pelvis = {0.0, 0.98 - drop, 0.0};
	const double lean = (p.lean + 10.0 * p.crouch + 6.0 * p.sit) * kPi / 180.0;
	const Vec3 up{0.0, std::cos(lean), std::sin(lean)};
	b.chest = b.pelvis + up * (0.40 + breath);
	b.neck = b.chest + up * 0.13;
	b.headPivot = b.neck + Vec3{0.0, 0.135, 0.025} + Vec3{0.0, 0.0, 0.03 * std::sin(lean)};
	for (int s = 0; s < 2; ++s) {
		const double side = s == 0 ? 1.0 : -1.0;   // 0: left (+x), 1: right
		b.shoulder[s] = b.chest + Vec3{0.165 * side, 0.075, -0.01};
		const Vec3 target = s == 0 ? p.handL : p.handR;
		const Limb arm = limbIk(b.shoulder[s], target, 0.33, 0.31, normalize(Vec3{0.6 * side, -0.2, -1.0}));
		b.elbow[s] = arm.elbow;
		b.wrist[s] = arm.hand;
		b.handDir[s] = normalize(arm.hand - arm.elbow);
		// the legs: the hip, the knee (forward), the hock (back), the ball of the foot. The walk: each foot in turn is on the ground for a step - going back under the body at
		// exactly the body's pace (0.84 m a step, so it does not slide: film_checks.h) - then swings forward, lifted a little, for the next
		b.hip[s] = b.pelvis + Vec3{0.095 * side, -0.04, 0.0};
		double swing = 0.0, lift = 0.0;
		gait(p.walk + (s == 0 ? 0.0 : 1.0), swing, lift, b.planted[s]);
		if (p.stride <= 0.0) b.planted[s] = true;
		Vec3 ball{0.11 * side, 0.03 + 0.07 * lift * p.stride, 0.07 + 0.42 * p.stride * swing};
		ball.y += p.slope * ball.z;   // (on a ramp the feet stand on it: a planted foot keeps its height while the body goes down or up, and the knee takes it)
		if (p.lockFeet) ball = p.lockedBall[s];
		if (p.sit > 0.0) ball = lerp(ball, Vec3{0.30 * side, 0.03, 0.32}, p.sit);   // (sitting: the feet out in front, crossed)
		b.ball[s] = ball;
		const Vec3 hockTarget = ball + Vec3{0.0, 0.20 - 0.08 * p.sit, -0.10};
		const Limb leg = limbIk(b.hip[s], hockTarget, 0.43, 0.38, normalize(Vec3{0.1 * side, 0.0, 1.0}));
		b.knee[s] = leg.elbow;
		b.hock[s] = leg.hand;
	}
	const Mat4 turn = mul(rotate({0.0, 1.0, 0.0}, p.headYaw), rotate({1.0, 0.0, 0.0}, p.headPitch), rotate({0.0, 0.0, 1.0}, p.headTilt));
	b.head = mul(translate(b.headPivot), turn);
	b.headInv = invertAffine(b.head);
	b.hemY = 0.34 + 0.12 * p.sit - 0.10 * p.crouch;
	return b;
}
// A foot lock for the explorer walking with its body at `frameAt(t)` and its walk `stepsAt(t)` steps in (stride and slope as walkPose and ExplorerPose have them), on the
// ground `groundAt` (empty: its own ground): rig::lockedFoot then gives where each foot is held.
inline rig::FootLock explorerFootLock(std::function<Mat4(double)> frameAt, std::function<double(double)> stepsAt, double stride = 1.0, double slope = 0.0,
									  std::function<double(double, double)> groundAt = {}) {
	rig::FootLock k;
	k.frameAt = std::move(frameAt);
	k.stepsAt = std::move(stepsAt);
	k.footAt = [stride, slope](double steps, int s) {
		ExplorerPose p = walkPose(steps, 0.0, stride);
		p.slope = slope;
		return explorerBody(p).ball[s];
	};
	k.plantedAt = [stride](double steps, int s) { return explorerBody(walkPose(steps, 0.0, stride)).planted[s]; };
	k.groundAt = std::move(groundAt);
	return k;
}
// The pose with its feet held by the lock at t.
inline void lockFeet(ExplorerPose &p, const rig::FootLock &k, double t) {
	p.lockFeet = true;
	for (int s = 0; s < 2; ++s) p.lockedBall[s] = rig::lockedFoot(k, s, t);
}

inline double ellv(Vec3 p, Vec3 c, Vec3 r) { return sdf::ellipsoid(p, translate(c * -1.0), r); }

// ---- the head (head space: the pivot at the top of the neck, +z forward) ---------------------------------------------------------------------------------------------
struct EyePlace {
	Vec3 c;
	double r;
	double outward;   // degrees the eye looks to its side
};
inline const EyePlace &eyePlace(int k) {   // 0 left, 1 right, 2 the third
	static const EyePlace e[3] = {{{0.050, 0.096, 0.094}, 0.0190, 16.0}, {{-0.050, 0.096, 0.094}, 0.0190, -16.0}, {{0.0, 0.152, 0.098}, 0.0170, 0.0}};
	return e[k];
}
// The goat-like head: a cranium, a long muzzle angled down, a wide nose, the jaw, a brow over the eyes, the neck.
inline double headSkinSdf(Vec3 q) {
	using namespace sdf;
	double d = ellv(q, {0.0, 0.090, -0.005}, {0.092, 0.104, 0.112});
	d = smin(d, ellipsoid(q, mul(rotate({1.0, 0.0, 0.0}, -18.0), translate(0.0, -0.030, -0.125)), {0.054, 0.058, 0.088}), 0.05);
	d = smin(d, ellv(q, {0.0, 0.006, 0.198}, {0.046, 0.030, 0.030}), 0.025);
	d = smin(d, ellv(q, {0.0, -0.030, 0.120}, {0.040, 0.026, 0.070}), 0.03);
	d = smin(d, ellv(q, {0.0, 0.118, 0.080}, {0.075, 0.050, 0.040}), 0.03);
	d = smin(d, roundCone(q, {0.0, 0.03, -0.03}, {0.0, -0.16, -0.01}, 0.050, 0.060), 0.05);
	return d;
}
inline double eyelidSdf(Vec3 q, int k, double closed) {
	const EyePlace &e = eyePlace(k);
	const Vec3 v = q - e.c;
	const double ball = length(v) - (e.r + 0.0035);
	const double h = e.r * (0.80 - 1.95 * clamp01(closed));
	return std::max(ball, h - v.y);
}
// Small goat ears, out to the sides under the horns, lifted or drooping.
inline double earSdf(Vec3 q, double lift) {
	double d = 1e9;
	for (int s = 0; s < 2; ++s) {
		const double side = s == 0 ? 1.0 : -1.0;
		const Vec3 base{0.080 * side, 0.090, -0.020};
		const Vec3 tip = base + Vec3{0.085 * side, -0.035 + 0.04 * lift, -0.030};
		const Vec3 axis = normalize(tip - base), flat = normalize(cross(axis, Vec3{0.0, 0.0, 1.0}));
		d = std::min(d, sdf::ellipsoid(q, invertAffine(alignY((base + tip) * 0.5, axis, flat)), {0.010, length(tip - base) * 0.55, 0.024}));
	}
	return d;
}
// The long flowing beard of the Nomai (the statue's, the Sun Station's portrait): from the jaw and the chin down over the chest, in locks; `length` 0 (tucked into the
// suit under the mask) .. 1.
inline double beardSdf(Vec3 q, double length) {
	using namespace sdf;
	const double L = 0.05 + 0.20 * clamp01(length);
	const Vec3 top{0.0, -0.045, 0.135}, tip{0.0, -0.045 - L, 0.150 + 0.15 * L};   // (forward of the chest, over the robe)
	double d = roundCone(q, top, tip, 0.050, 0.010);
	d = smin(d, ellv(q, {0.0, -0.040, 0.090}, {0.070, 0.035, 0.060}), 0.03);   // (the cheeks' fur)
	// locks: grooves down its length that deepen and part towards the tips (each lock twisting a little), and finer strands within them
	const double along = clamp01((top.y - q.y) / L);
	const double around = std::atan2(q.x, q.z - 0.06);
	const double lock = std::sin(around * 9.0 + along * 6.0 + 0.6 * std::sin(around * 3.0 + 1.0));
	const double strand = std::sin(around * 41.0 + along * 23.0 + 2.0 * lock);
	return d + (0.003 + 0.004 * along) * lock * (0.4 + 0.6 * along) + 0.0009 * strand * along;
}
// The horns: the two side horns curl back and round like a ram's (the statue's), the top antler rises and forks into two points.
inline const std::vector<std::vector<Vec3>> &hornCurves() {
	static const std::vector<std::vector<Vec3>> c = [] {
		std::vector<std::vector<Vec3>> out;
		for (int s = 0; s < 2; ++s) {
			const double side = s == 0 ? 1.0 : -1.0;
			std::vector<Vec3> curl;
			const Vec3 centre{0.112 * side, 0.120, -0.070};
			for (int i = 0; i <= 14; ++i) {
				const double u = i / 14.0;
				const double a = 1.75 + u * 4.6;   // round the curl: from up-forward, back, down, forward again
				const double r = 0.085 * (1.0 - 0.55 * u);
				curl.push_back(centre + Vec3{0.028 * side * u, r * std::sin(a), r * std::cos(a)});
			}
			curl.front() = {0.052 * side, 0.168, 0.035};
			out.push_back(curl);
		}
		out.push_back({{0.0, 0.196, 0.030}, {0.0, 0.262, -0.010}, {0.0, 0.300, -0.075}});
		out.push_back({{0.0, 0.300, -0.075}, {0.020, 0.335, -0.110}, {0.030, 0.360, -0.130}});   // the fork
		out.push_back({{0.0, 0.300, -0.075}, {-0.015, 0.318, -0.150}, {-0.022, 0.322, -0.195}});
		return out;
	}();
	return c;
}
// The horns' growth rings: ridges round each horn, closer together towards its tip (a ram's horn adds a ring a year, thinner as it grows out), deepest on the two side
// horns and faint on the antler; each ring is a little uneven round the horn. `s` is the distance along the horn from its root, `len` its whole length, `around` the
// angle round it.
inline double hornRing(double s, double len, double around, bool sideHorn) {
	const double u = clamp01(s / std::max(1e-6, len));
	const double phase = (s / 0.011) * (1.0 + 0.9 * u);               // (closer together along the horn)
	const double ridge = std::pow(0.5 + 0.5 * std::cos(2.0 * kPi * phase + 0.35 * std::sin(around * 2.0 + s * 40.0)), 3.0);
	return (sideHorn ? 0.0022 : 0.0008) * (1.0 - 0.6 * u) * ridge;
}
inline double hornSdf(Vec3 q) {
	double d = 1e9;
	for (std::size_t c = 0; c < hornCurves().size(); ++c) {
		const auto &curve = hornCurves()[c];
		const int n = static_cast<int>(curve.size());
		const double base = c < 2 ? 0.024 : (c == 2 ? 0.020 : 0.011);
		const double end = c < 2 ? 0.006 : (c == 2 ? 0.012 : 0.004);
		double len = 0.0;
		for (int i = 0; i + 1 < n; ++i) len += length(curve[static_cast<std::size_t>(i + 1)] - curve[static_cast<std::size_t>(i)]);
		double along = 0.0;
		for (int i = 0; i + 1 < n; ++i) {
			const Vec3 a = curve[static_cast<std::size_t>(i)], b = curve[static_cast<std::size_t>(i + 1)];
			const double r0 = lerp(base, end, static_cast<double>(i) / (n - 1)), r1 = lerp(base, end, static_cast<double>(i + 1) / (n - 1));
			const double seg = length(b - a);
			const double e = sdf::roundCone(q, a, b, r0, r1);
			if (e < 0.02) {   // (near the surface: the rings, measured along the horn - they used to be shells round the head's origin, running across the horns)
				const Vec3 axis = (b - a) * (1.0 / std::max(1e-9, seg));
				const double t = clamp01(dot(q - a, axis) / std::max(1e-9, seg));
				const Vec3 off = q - (a + axis * (t * seg));
				const Vec3 side = normalize(cross(axis, std::abs(axis.y) < 0.9 ? Vec3{0.0, 1.0, 0.0} : Vec3{1.0, 0.0, 0.0}));
				const double around = std::atan2(dot(off, cross(axis, side)), dot(off, side));
				d = std::min(d, e - hornRing(along + t * seg, len, around, c < 2));
			} else {
				d = std::min(d, e);
			}
			along += seg;
		}
	}
	return d;
}

// ---- the life-support mask ------------------------------------------------------------------------------------------------------------------------------------------
// Our own mask, in the spirit of the Nomai's - a pale mask with a window for each of the three eyes, warm metal and a faint blue light - but drawn afresh: a rounded
// shield of bone-white ivory bent round the face, wider at the brow; its three round eye windows set in bronze bezels; round the third eye's window, on the brow, tide
// rings cut into the ivory and a thin inlay glowing faint blue (glowRingFrame); five small silver stars on an arc above them; two swept blades at each temple like a
// swift's wings, each with a bronze spine; a smooth cowl over the long muzzle with a bronze band and a dark breathing grille at its tip. Materials: ivory, bronze,
// silver, the dark grille and the blue glow. `q` in the mask's own space (the head's, when it is worn).
inline Vec3 maskCentre() { return {0.0, 0.104, 0.172}; }
// A point's coordinates on the bent face plate: (across, up, depth above the plate's middle surface).
inline Vec3 maskPlate(Vec3 q) {
	const Vec3 c = maskCentre();
	const double x = q.x - c.x, y = q.y - c.y;
	return {x, y, q.z - c.z + 2.0 * x * x + 0.7 * y * y};
}
// The shield's outline in plate coordinates: a big squared plate with rounded corners (a superellipse), a little narrower at the bottom where the cowl meets it
// (negative inside, about metres). It is bent round the face (maskPlate), so its sides turn back towards the ears.
inline double shieldOutline(double x, double y) {
	constexpr double top = 0.172, bottom = -0.092;
	const double h = 0.5 * (top - bottom), v = (y - 0.5 * (top + bottom)) / h;
	const double w = 0.150 - 0.018 * std::pow(std::max(0.0, -v), 2.0);
	const double e = std::pow(std::pow(std::abs(x) / w, 4.5) + std::pow(std::abs(v), 4.5), 1.0 / 4.5) - 1.0;
	return e * std::min(w, h);
}
constexpr double kMaskHalfThick = 0.008;
// The eye windows, in plate coordinates (0 left, 1 right, 2 the third), and their radius. The two lower windows sit a little outward of their eyes, as the eyes look out.
inline Vec3 maskPortAt(int k) {
	const EyePlace &e = eyePlace(k);
	const Vec3 c = maskCentre();
	return {e.c.x + 0.07 * std::sin(e.outward * kPi / 180.0) - c.x, e.c.y - c.y, 0.0};
}
inline double maskPortRadius(int k) { return k < 2 ? 0.024 : 0.022; }
inline double portDistance(Vec3 p, int k) {   // from the window's axis, in the plate
	const Vec3 at = maskPortAt(k);
	return std::hypot(p.x - at.x, p.y - at.y) - maskPortRadius(k);
}
constexpr double kGlowRing = 0.032;   // the glowing inlay's radius round the third eye's window
inline Mat4 beakFrameInv() {
	static const Mat4 m = invertAffine(mul(translate(0.0, 0.010, 0.150), rotate({1.0, 0.0, 0.0}, 22.0)));
	return m;
}
inline Vec3 cowlRadii() { return {0.066, 0.090, 0.104}; }
// The swept blades at the temples: side s (+1 its left), upper (0) or lower (1): their root and tip in mask space.
inline void bladeEnds(int s, int k, Vec3 &root, Vec3 &tip) {
	root = k == 0 ? Vec3{0.138 * s, 0.222, 0.118} : Vec3{0.146 * s, 0.128, 0.118};   // (from the shield's upper corners and its sides)
	tip = k == 0 ? Vec3{0.212 * s, 0.272, -0.010} : Vec3{0.222 * s, 0.150, 0.010};   // (swept back more than up, like a swift's wings)
}
// A blade's frame: along it, the normal of its flat face (level, across the blade) and the direction in its face towards its upper edge.
inline void bladeAxes(Vec3 root, Vec3 tip, Vec3 &along, Vec3 &normal, Vec3 &upEdge) {
	along = normalize(tip - root);
	normal = normalize(cross(along, Vec3{0.0, 1.0, 0.0}));
	upEdge = normalize(cross(normal, along));
	if (upEdge.y < 0.0) upEdge = upEdge * -1.0;
}
inline double bladeSdf(Vec3 q, int s, int k) {
	Vec3 root, tip, along, normal, up;
	bladeEnds(s, k, root, tip);
	bladeAxes(root, tip, along, normal, up);
	const double t = clamp01(dot(q - root, along) / length(tip - root));
	const Vec3 bow = up * (0.010 * std::sin(kPi * t));   // (a gentle curve, like a feather's)
	const double body = sdf::roundCone(q - bow, root, tip, k == 0 ? 0.026 : 0.020, 0.005);
	return std::max(body, std::abs(dot(q - bow - root, normal)) - 0.0040);
}
inline double bladeSpineSdf(Vec3 q, int s, int k) {
	Vec3 root, tip, along, normal, up;
	bladeEnds(s, k, root, tip);
	bladeAxes(root, tip, along, normal, up);
	const double t = clamp01(dot(q - root, along) / length(tip - root));
	const Vec3 bow = up * (0.010 * std::sin(kPi * t));
	const double r0 = k == 0 ? 0.026 : 0.020;
	return sdf::roundCone(q - bow, root + up * (r0 - 0.004), tip + up * 0.002, 0.0042, 0.0022);
}
inline double maskIvorySdf(Vec3 q) {
	using namespace sdf;
	const Vec3 p = maskPlate(q);
	// the shield, its edge rounded
	const double o = shieldOutline(p.x, p.y) + 0.002, t = std::abs(p.z) - kMaskHalfThick + 0.002;
	double d = std::min(std::max(o, t), 0.0) + std::hypot(std::max(o, 0.0), std::max(t, 0.0)) - 0.002;
	// the cowl over the muzzle, joined to the shield's lower edge
	const Vec3 b = transformPoint(beakFrameInv(), q);
	double cowl = std::abs(ellipsoid(b, identity(), cowlRadii())) - 0.0065;
	cowl = std::max(cowl, std::max(0.010 - b.z, -p.z - 0.004));   // (only what stands in front of the shield: the shield closes it behind, no open edge)
	d = smin(d, cowl, 0.010);
	// the engraving on the face: tide rings rising over the brow round the third window (arcs, open below), a groove for the glowing inlay, a line inset round the
	// shield's edge
	const auto groove = [](double dist, double w, double depth) { return depth * (1.0 - smoothstep(0.0, w, std::abs(dist))); };
	double cut = 0.0;
	if (p.z > 0.0 && p.z < 0.02) {
		const Vec3 third = maskPortAt(2);
		const double r = std::hypot(p.x - third.x, p.y - third.y);
		const double above = smoothstep(-0.020, -0.004, p.y - third.y);
		for (const double ring : {0.040, 0.049, 0.058, 0.067, 0.076}) cut = std::max(cut, groove(r - ring, 0.0016, 0.0013) * above);
		cut = std::max(cut, groove(r - kGlowRing, 0.0030, 0.0030));
		for (const double inset : {0.011, 0.020}) cut = std::max(cut, groove(shieldOutline(p.x, p.y) + inset, 0.0014, 0.0010));
	}
	d += cut;
	// the windows, through
	for (int k = 0; k < 3; ++k) d = std::max(d, -portDistance(p, k));
	// the breathing grille: four slots across the cowl's tip
	if (b.z > 0.080)
		for (int k = 0; k < 4; ++k) d = std::max(d, -std::max(std::abs(b.y - (-0.030 + 0.016 * k)) - 0.0028, std::abs(b.x) - 0.024 + 0.08 * std::max(0.0, std::abs(b.y) - 0.03)));
	return d;
}
// The swept blades on side s (ivory, made apart from the shield: they are coarser), a faint line along each.
inline double maskBladesSdf(Vec3 q, int s) {
	double d = 1e9;
	for (int k = 0; k < 2; ++k) {
		Vec3 root, tip, along, normal, up;
		bladeEnds(s, k, root, tip);
		bladeAxes(root, tip, along, normal, up);
		double e = bladeSdf(q, s, k);
		const double tt = dot(q - root, along) / length(tip - root);
		if (tt > 0.08 && tt < 0.85) e += 0.0008 * (1.0 - smoothstep(0.0, 0.0012, std::abs(dot(q - root, up) - 0.006 * (1.0 - tt))));
		d = std::min(d, e);
	}
	return d;
}
inline double maskBronzeSdf(Vec3 q) {
	using namespace sdf;
	const Vec3 p = maskPlate(q);
	// the rim round the shield (proud of it on both faces) and the bezels round the windows
	const double o = shieldOutline(p.x, p.y);
	double d = std::max(std::abs(o + 0.0035) - 0.0035, std::abs(p.z) - kMaskHalfThick - 0.0022);
	for (int k = 0; k < 3; ++k) {
		const double r = portDistance(p, k);   // (0 at the window's edge)
		d = std::min(d, std::hypot(r - 0.0030, p.z - kMaskHalfThick) - 0.0045);
	}
	// the blades' spines
	for (int s = -1; s <= 1; s += 2)
		for (int k = 0; k < 2; ++k) d = std::min(d, bladeSpineSdf(q, s, k));
	// a band round the cowl
	const Vec3 b = transformPoint(beakFrameInv(), q);
	const double onShell = std::abs(ellipsoid(b, identity(), cowlRadii())) - 0.0088;
	d = std::min(d, std::max(std::max(onShell, std::abs(b.z - 0.052) - 0.0055), -p.z - 0.002));
	return d;
}
// The five silver stars on an arc above the tide rings: four-pointed, flat on the face, the middle one larger.
inline double maskStarSdf(Vec3 q) {
	const Vec3 p = maskPlate(q), third = maskPortAt(2);
	double d = 1e9;
	for (int k = 0; k < 5; ++k) {
		const double a = (40.0 + 25.0 * k) * kPi / 180.0, size = k == 2 ? 0.0105 : 0.0075;
		const double x = p.x - (third.x + 0.094 * std::cos(a)), y = p.y - (third.y + 0.094 * std::sin(a));
		const double star = std::min(std::abs(x) / size + std::abs(y) / (size * 0.28), std::abs(x) / (size * 0.28) + std::abs(y) / size) - 1.0;
		d = std::min(d, std::max(star * size * 0.28, std::abs(p.z - kMaskHalfThick) - 0.0016));
	}
	return d;
}
// The dark inside of the cowl's tip, seen through the grille.
inline double maskDarkSdf(Vec3 q) {
	const Vec3 b = transformPoint(beakFrameInv(), q);
	const Vec3 r = cowlRadii() - Vec3{0.010, 0.010, 0.010};
	return std::max(std::abs(sdf::ellipsoid(b, identity(), r)) - 0.0015, 0.078 - b.z);
}
// The glowing inlay round the third window: a thin ring laid in its groove (mask space; the ring is a torus about its frame's y axis).
inline Mat4 glowRingFrame() {
	const Vec3 third = maskPortAt(2), c = maskCentre();
	const double y = third.y, z = c.z + kMaskHalfThick - 0.0018 - 0.7 * y * y;
	return alignY({third.x + c.x, third.y + c.y, z}, normalize(Vec3{0.0, 1.4 * y, 1.0}));
}

// ---- the body (body space) ------------------------------------------------------------------------------------------------------------------------------------------
inline Vec3 robeSway(const ExplorerPose &p, const ExplorerBody &b, double y) {
	const double below = clamp01((b.pelvis.y - y) / std::max(0.2, b.pelvis.y - b.hemY));
	const double flutter = std::sin(p.time * 5.3 + y * 9.0) * 0.4 + std::sin(p.time * 8.1 + y * 17.0) * 0.2;
	return Vec3{p.wind.x, 0.0, p.wind.z} * (below * below * (1.0 + 0.4 * flutter)) + Vec3{0.0, 0.0, 0.035 * p.stride * std::sin(p.walk * kPi * 2.0)} * below;
}
inline double skirtSdf(Vec3 q, const ExplorerPose &p, const ExplorerBody &b, double inset) {
	using namespace sdf;
	const Vec3 s = q - robeSway(p, b, q.y);
	const Vec3 hemCentre{0.0, b.hemY, 0.035 + 0.05 * p.sit};
	double d = roundCone(s, b.pelvis + Vec3{0.0, 0.03, 0.0}, hemCentre, 0.185 - inset, 0.32 + 0.14 * p.sit - inset);
	for (int k = 0; k < 2; ++k) d = smin(d, roundCone(s, b.hip[k], b.knee[k], 0.12 - inset, 0.10 - inset), 0.08);
	// the cloth's folds: soft vertical pleats from the waist, deeper towards the hem, uneven round the body and swaying a little with the cloth
	const double below = clamp01((b.pelvis.y - q.y) / std::max(0.2, b.pelvis.y - b.hemY));
	const double ang = std::atan2(s.x, s.z - hemCentre.z * below);
	const double sway = 0.35 * std::sin(p.time * 1.1) + 0.6 * p.stride * std::sin(p.walk * kPi);
	const double folds = 0.65 * std::sin(ang * 9.0 + 1.3 * std::sin(ang * 2.0 + 0.5) + sway) + 0.35 * std::sin(ang * 17.0 + 0.8 - 0.5 * sway);
	d += 0.010 * std::pow(below, 1.3) * folds;
	const double hem = b.hemY + 0.012 * std::sin(std::atan2(q.x, q.z) * 7.0) + 0.006 * below * folds;
	return std::max(d, hem - q.y);
}
inline double torsoSdf(Vec3 q, const ExplorerBody &b, double inset) {
	using namespace sdf;
	double d = roundCone(q, b.neck + Vec3{0.0, -0.035, 0.0}, b.pelvis + Vec3{0.0, 0.02, 0.0}, 0.120 - inset, 0.180 - inset);
	d = smin(d, ellv(q, b.chest + Vec3{0.0, -0.02, 0.0}, {0.185 - inset, 0.18, 0.125 - inset}), 0.06);
	return d;
}
// The front panel (the concept's long tabard down the middle): its half width at a height, and the slits beside it.
inline double panelHalf(const ExplorerBody &b, double y) { return 0.070 + 0.050 * std::max(0.0, b.pelvis.y - y); }
constexpr double kSlit = 0.040;
// The green outer robe: the body, the skirt and full sleeves, open down the front below the waist.
inline double outerRobeSdf(Vec3 q, const ExplorerPose &p, const ExplorerBody &b) {
	using namespace sdf;
	double d = smin(torsoSdf(q, b, 0.0), skirtSdf(q, p, b, 0.0), 0.06);
	for (int s = 0; s < 2; ++s) {
		// full sleeves (the concept's puffed ones), gathered at the wrist
		const Vec3 cuff = b.wrist[s] - b.handDir[s] * 0.03;
		d = smin(d, roundCone(q, b.shoulder[s], b.elbow[s], 0.074, 0.092), 0.03);
		d = smin(d, roundCone(q, b.elbow[s], cuff, 0.094, 0.048), 0.03);
		d = std::max(d, -(roundCone(q, cuff - b.handDir[s] * 0.10, cuff + b.handDir[s] * 0.05, 0.030, 0.042)));
	}
	if (q.y < b.pelvis.y - 0.02) {
		// the slits either side of the front panel, and the slit up the back
		d = std::max(d, -(std::max(std::abs(std::abs(q.x) - panelHalf(b, q.y) - kSlit * 0.5) - kSlit * 0.5, 0.02 - q.z)));
		if (q.y < b.pelvis.y - 0.08) d = std::max(d, -(std::max(std::abs(q.x) - (0.010 + 0.10 * (b.pelvis.y - 0.08 - q.y)), q.z + 0.03)));
	}
	return d;
}
// Under it, seen in the slits: rust-red woven wraps over the hips and thighs, teal trousers below.
inline double trousersSdf(Vec3 q, const ExplorerPose &p, const ExplorerBody &b) {
	using namespace sdf;
	double d = 1e9;
	for (int k = 0; k < 2; ++k) {
		d = std::min(d, roundCone(q, b.knee[k] + (b.hip[k] - b.knee[k]) * 0.45, b.knee[k], 0.078, 0.070));
		d = smin(d, roundCone(q, b.knee[k], b.hock[k], 0.066, 0.050), 0.03);
	}
	(void)p;
	return d;
}
inline double hipWrapSdf(Vec3 q, const ExplorerBody &b) {
	double d = 1e9;
	for (int k = 0; k < 2; ++k) {
		const Vec3 axis = normalize(b.knee[k] - b.hip[k]);
		const Vec3 v = q - b.hip[k];
		const double along = dot(v, axis), th = std::atan2(v.x, v.z);
		double e = sdf::roundCone(q, b.hip[k] + Vec3{0.0, 0.06, 0.0}, b.knee[k] + (b.hip[k] - b.knee[k]) * 0.40, 0.105, 0.088);
		e += 0.004 * std::sin(th * 8.0 + along * 60.0) * std::sin(th * 8.0 - along * 60.0);   // (the weave)
		d = std::min(d, e);
	}
	return d;
}
// The embroidered borders (woven cloth, borderMaterial): bands down both edges of the front panel and the slits' outer edges, the hem, the back slit's edges, a rounded
// neckline front and back, and the waist band across the back.
inline double trimSdf(Vec3 q, const ExplorerPose &p, const ExplorerBody &b) {
	const double surface = std::abs(outerRobeSdf(q, p, b) + 0.002) - 0.006;
	double band = 1e9;
	if (q.y < b.pelvis.y - 0.02 && q.z > 0.0) {
		const double ph = panelHalf(b, q.y);
		band = std::min(std::abs(std::abs(q.x) - ph + 0.012) - 0.012, std::abs(std::abs(q.x) - ph - kSlit - 0.010) - 0.010);
	}
	if (q.y < b.pelvis.y - 0.08 && q.z < 0.0) band = std::min(band, std::abs(std::abs(q.x) - (0.010 + 0.10 * (b.pelvis.y - 0.08 - q.y)) - 0.012) - 0.012);
	band = std::min(band, std::abs(q.y - b.hemY - 0.030) - 0.024);
	// the neckline: a rounded U from the shoulders down the chest, front and back (shallower at the back)
	if (q.y > b.chest.y - 0.16 && q.y < b.neck.y + 0.02) {
		const double depth = q.z > 0.0 ? 0.150 : 0.080;
		const double ex = q.x / 0.100, ey = (q.y - b.neck.y) / depth;
		if (ey < 0.0) band = std::min(band, std::abs(std::sqrt(ex * ex + ey * ey) - 1.0) * 0.100 - 0.012);
	}
	// from the neckline down to the waist at the front, two bands either side of the panel
	if (q.y > b.pelvis.y - 0.02 && q.y < b.chest.y + 0.04 && q.z > 0.0) band = std::min(band, std::abs(std::abs(q.x) - 0.075) - 0.011);
	if (q.z < 0.0) band = std::min(band, std::abs(q.y - (b.pelvis.y + 0.06)) - 0.020);
	return std::max(surface, band);
}
// Two bronze bands at each cuff.
inline double cuffBandSdf(Vec3 q, const ExplorerBody &b) {
	double d = 1e9;
	for (int s = 0; s < 2; ++s)
		for (int k = 0; k < 2; ++k) {
			const Vec3 c = b.wrist[s] - b.handDir[s] * (0.022 + 0.040 * k);
			const Vec3 v = q - c;
			const double along = dot(v, b.handDir[s]);
			const double radial = length(v - b.handDir[s] * along);
			d = std::min(d, std::max(std::abs(radial - (0.050 + 0.007 * k)) - 0.009, std::abs(along) - 0.009));
		}
	return d;
}
// A thin silver piping inside the borders of the front panel, and one above the hem.
inline double creamLineSdf(Vec3 q, const ExplorerPose &p, const ExplorerBody &b) {
	const double surface = std::abs(outerRobeSdf(q, p, b) + 0.001) - 0.0065;
	double band = std::abs(q.y - b.hemY - 0.075) - 0.006;
	if (q.y < b.pelvis.y - 0.02 && q.z > 0.0) band = std::min(band, std::abs(std::abs(q.x) - panelHalf(b, q.y) + 0.040) - 0.0045);
	return std::max(surface, band);
}
// The bronze collar: a rolled band round the neck's base, beaded (thirty rounded beads round it), dipping a little at the front.
inline double collarSdf(Vec3 q, const ExplorerBody &b) {
	const Vec3 v = q - (b.neck + Vec3{0.0, -0.045, 0.010});
	const double a = std::atan2(v.x, v.z);
	const double r = std::sqrt(v.x * v.x + v.z * v.z / 0.8);
	const double bead = 0.0035 * std::pow(std::abs(std::cos(a * 15.0)), 0.6);
	return std::hypot(r - 0.124, v.y + 0.02 * std::cos(a)) - 0.0145 - bead;
}
// The quilted shoulder pads: umber leather over each shoulder and upper arm in stitched channels, ringed in bronze at the bottom.
inline double padSdf(Vec3 q, const ExplorerBody &b) {
	double d = 1e9;
	for (int s = 0; s < 2; ++s) {
		const Vec3 axis = normalize(b.elbow[s] - b.shoulder[s]);
		const Mat4 f = alignY(b.shoulder[s] + axis * 0.045 + Vec3{0.0, 0.020, 0.0}, axis);
		const Vec3 l = transformPoint(invertAffine(f), q);
		double e = sdf::ellipsoid(l, identity(), {0.098, 0.135, 0.098});
		e = std::max(e, l.y - 0.095);
		e += 0.0040 * std::pow(std::abs(std::cos(l.y * 72.0)), 6.0);   // (the stitched channels round the arm: the stitches pull the leather in)
		d = std::min(d, e);
	}
	return d;
}
inline double padRingSdf(Vec3 q, const ExplorerBody &b) {
	double d = 1e9;
	for (int s = 0; s < 2; ++s) {
		const Vec3 axis = normalize(b.elbow[s] - b.shoulder[s]);
		const Vec3 c = b.shoulder[s] + axis * 0.140 + Vec3{0.0, 0.020, 0.0};
		const Vec3 v = q - c;
		const double along = dot(v, axis);
		d = std::min(d, std::sqrt(std::pow(length(v - axis * along) - 0.082, 2.0) + along * along) - 0.012);
	}
	return d;
}
// The life-support pack: a bronze housing on the upper back, a horizontal ivory tank across it with gold bands and caps, and the dark hose from the mask's chin to the
// pack, over the shoulder.
inline double packHousingSdf(Vec3 q, const ExplorerBody &b) {
	const Vec3 c = b.neck + Vec3{0.0, 0.215, -0.255};
	double d = sdf::roundBox(q, invertAffine(translate(c)), {0.200, 0.095, 0.100}, 0.015);
	d = std::min(d, sdf::roundBox(q, invertAffine(translate(c + Vec3{0.0, 0.110, -0.010})), {0.160, 0.030, 0.085}, 0.010));   // (a step on top)
	d = std::min(d, sdf::roundBox(q, invertAffine(translate(c + Vec3{0.0, -0.120, 0.050})), {0.120, 0.060, 0.070}, 0.015));   // (its neck to the shoulders)
	return d;
}
inline Vec3 tankCentre(const ExplorerBody &b) { return b.neck + Vec3{0.0, 0.120, -0.340}; }
constexpr double kTankHalf = 0.300, kTankR = 0.080;
inline double tankSdf(Vec3 q, const ExplorerBody &b) {
	const Vec3 v = q - tankCentre(b);
	const double r = std::sqrt(v.y * v.y + v.z * v.z);
	return std::max(r - kTankR, std::abs(v.x) - kTankHalf);
}
inline double tankGoldSdf(Vec3 q, const ExplorerBody &b) {
	const Vec3 v = q - tankCentre(b);
	const double r = std::sqrt(v.y * v.y + v.z * v.z);
	double d = 1e9;
	for (const double x : {-0.300, -0.200, 0.200, 0.300}) d = std::min(d, std::max(r - kTankR - 0.012, std::abs(v.x - x) - (std::abs(x) > 0.25 ? 0.022 : 0.014)));
	// the rims of the caps
	d = std::min(d, std::max(std::abs(r - 0.090) - 0.012, std::abs(std::abs(v.x) - 0.335) - 0.016));
	return d;
}
inline double tankCapSdf(Vec3 q, const ExplorerBody &b) {
	const Vec3 v = q - tankCentre(b);
	const double r = std::sqrt(v.y * v.y + v.z * v.z);
	return std::max(r - 0.082, std::abs(std::abs(v.x) - 0.335) - 0.012);
}
// The hands: a small palm and three fingers (two and a thumb), curled by `grip` (the Nomai's hands are small: their machines are worked by sight, not touch).
inline double handSdf(Vec3 q, const ExplorerBody &b, int s, double grip) {
	using namespace sdf;
	const Vec3 w = b.wrist[s], dir = b.handDir[s];
	const double side = s == 0 ? 1.0 : -1.0;
	const Vec3 across = normalize(cross(dir, Vec3{0.0, 1.0, 0.0})) * side;
	const Vec3 palmUp = normalize(cross(across, dir));
	const Vec3 palm = w + dir * 0.040;
	double d = ellipsoid(q, invertAffine(alignY(palm, dir, across)), {0.027, 0.040, 0.015});
	for (int f = 0; f < 3; ++f) {
		const bool thumb = f == 2;
		const Vec3 base = thumb ? palm + across * 0.024 - dir * 0.014 : palm + dir * 0.034 + across * (f == 0 ? 0.012 : -0.012);
		const Vec3 out = thumb ? normalize(across * 0.8 + dir * 0.6) : dir;
		const double len = thumb ? 0.044 : 0.058;
		const Vec3 mid = base + out * (len * 0.5) - palmUp * (len * 0.25 * grip);
		const Vec3 tip = mid + normalize(out * (1.0 - grip) - palmUp * (0.2 + grip)) * (len * 0.5);
		d = smin(d, roundCone(q, base, mid, 0.011, 0.009), 0.011);
		d = smin(d, roundCone(q, mid, tip, 0.009, 0.007), 0.006);
	}
	return d;
}
// The pale tips of the three fingers (the concept's claws).
inline double fingerTipSdf(Vec3 q, const ExplorerBody &b, int s, double grip) {
	const Vec3 w = b.wrist[s], dir = b.handDir[s];
	const double side = s == 0 ? 1.0 : -1.0;
	const Vec3 across = normalize(cross(dir, Vec3{0.0, 1.0, 0.0})) * side;
	const Vec3 palmUp = normalize(cross(across, dir));
	const Vec3 palm = w + dir * 0.040;
	double d = 1e9;
	for (int f = 0; f < 3; ++f) {
		const bool thumb = f == 2;
		const Vec3 base = thumb ? palm + across * 0.024 - dir * 0.014 : palm + dir * 0.034 + across * (f == 0 ? 0.012 : -0.012);
		const Vec3 out = thumb ? normalize(across * 0.8 + dir * 0.6) : dir;
		const double len = thumb ? 0.044 : 0.058;
		const Vec3 mid = base + out * (len * 0.5) - palmUp * (len * 0.25 * grip);
		const Vec3 tip = mid + normalize(out * (1.0 - grip) - palmUp * (0.2 + grip)) * (len * 0.5);
		d = std::min(d, length(q - tip) - 0.0085);
	}
	return d;
}
inline double ankleBandSdf(Vec3 q, const ExplorerBody &b) {
	double d = 1e9;
	for (int s = 0; s < 2; ++s) {
		const Vec3 axis = normalize(b.ball[s] - b.hock[s]);
		const Vec3 c = lerp(b.hock[s], b.ball[s], 0.40);
		const Vec3 v = q - c;
		const double along = dot(v, axis);
		d = std::min(d, std::max(std::abs(length(v - axis * along) - 0.056) - 0.010, std::abs(along) - 0.016));
	}
	return d;
}
// The feet below the hem: big pale three-toed feet (the suit's), each toe with a dark tip.
inline Vec3 footForward(const ExplorerBody &b, int s) { return normalize(Vec3{b.ball[s].x - b.hock[s].x, 0.0, b.ball[s].z - b.hock[s].z} + Vec3{0.0, 0.0, 1e-6}); }
inline Vec3 toeTip(const ExplorerBody &b, int s, int t) {
	const Vec3 fwd = footForward(b, s), side = normalize(cross(Vec3{0.0, 1.0, 0.0}, fwd));
	return b.ball[s] + fwd * (0.085 - 0.012 * std::abs(t)) + side * (0.040 * t) + Vec3{0.0, -0.010, 0.0};
}
inline double footSdf(Vec3 q, const ExplorerBody &b) {
	using namespace sdf;
	double d = 1e9;
	for (int s = 0; s < 2; ++s) {
		d = std::min(d, roundCone(q, b.hock[s], b.ball[s], 0.048, 0.060));
		for (int t = -1; t <= 1; ++t) d = smin(d, roundCone(q, b.ball[s] + Vec3{0.0, -0.005, 0.0}, toeTip(b, s, t), 0.040, 0.026), 0.025);
	}
	return d;
}
inline double clawSdf(Vec3 q, const ExplorerBody &b) {
	double d = 1e9;
	for (int s = 0; s < 2; ++s)
		for (int t = -1; t <= 1; ++t) d = std::min(d, length(q - (toeTip(b, s, t) + footForward(b, s) * 0.018)) - 0.020);
	return d;
}

// ---- the staff ------------------------------------------------------------------------------------------------------------------------------------------------------
// The explorer's sky-compass staff: a polished dark wooden shaft with carved rings, a leather wrap where it is held, bronze collars and a bronze foot; on top a bronze
// cradle holds two nested gimbal rings - the outer bronze with a scale of ticks, the inner silver - round a crystal that glows ghostly blue (a real light: it lights
// the hand and the mask).
inline Mat4 staffFrame(const ExplorerPose &p, const ExplorerBody &b) {
	const int s = p.staff > 0 ? 0 : 1;
	const Vec3 grip = b.wrist[s] + b.handDir[s] * 0.05;
	const Vec3 up = (p.staffTip.x != 0.0 || p.staffTip.y != 0.0 || p.staffTip.z != 0.0) ? normalize(p.staffTip - grip) : normalize(Vec3{0.0, 1.0, 0.05});
	return alignY(grip, up);
}
// The sceptre at `m` (its grip at the origin, the shaft along +y), its light at `glowAmount` (0 .. 1).
inline std::string sceptreText(const Mat4 &m, double glowAmount) {
	std::string out;
	// the shaft: tapering a little to the top, a shallow carved ring every 12 cm
	std::vector<std::pair<double, double>> shaft;
	shaft.push_back({0.0, -1.14});
	for (int i = 0; i <= 120; ++i) {
		const double y = -1.14 + 1.66 * i / 120.0;
		const double ring = std::pow(std::abs(std::cos(kPi * (y + 1.14) / 0.12)), 24.0);
		shaft.push_back({0.0158 - 0.0022 * (y + 1.14) / 1.66 - 0.0016 * ring, y});
	}
	shaft.push_back({0.0, 0.52});
	out += attr(coated({0.17, 0.095, 0.050}, 0.35), meshText(lathe(shaft, 16), m));
	// the leather wrap where it is held: a binding wound round it
	std::string wrap;
	for (int k = 0; k < 15; ++k) wrap += meshText(torus(0.0160, 0.0034, 18, 6), mul(m, translate(0.0, -0.075 + 0.0105 * k, 0.0), rotate({1.0, 0.0, 0.0}, 6.0)));
	out += attr(diffuseMaterial({0.16, 0.085, 0.045}), wrap);
	// bronze: the foot, collars above and below the wrap and under the head, the cradle and the outer ring
	std::string bronze;
	bronze += meshText(lathe({{0.0, -1.20}, {0.006, -1.198}, {0.012, -1.185}, {0.0165, -1.15}, {0.0170, -1.13}, {0.0, -1.13}}, 16), m);
	for (const double y : {-0.092, 0.090, 0.30, 0.50}) bronze += meshText(torus(0.0168, 0.0042, 18, 8), mul(m, translate(0.0, y, 0.0)));
	bronze += meshText(lathe({{0.0, 0.49}, {0.0175, 0.50}, {0.0210, 0.53}, {0.0120, 0.575}, {0.0060, 0.60}, {0.0, 0.61}}, 16), m);
	const double R = 0.066, cy = 0.690;   // the outer ring's radius, the rings' centre above the grip
	for (int i = 0; i < 12; ++i) {        // the cradle: a half circle under the rings, from pivot to pivot
		const double a0 = kPi + kPi * i / 12.0, a1 = kPi + kPi * (i + 1) / 12.0;
		bronze += rod(transformPoint(m, {(R + 0.010) * std::cos(a0), cy + (R + 0.010) * std::sin(a0), 0.0}), transformPoint(m, {(R + 0.010) * std::cos(a1), cy + (R + 0.010) * std::sin(a1), 0.0}), 0.0042, 8);
	}
	for (int s = -1; s <= 1; s += 2) {   // the pivots, with a bead on the end of each
		bronze += rod(transformPoint(m, {s * (R - 0.003), cy, 0.0}), transformPoint(m, {s * (R + 0.016), cy, 0.0}), 0.0030, 8);
		bronze += meshText(uvSphere(0.0055, 10, 6), mul(m, translate(s * (R + 0.017), cy, 0.0)));
	}
	const Mat4 outer = mul(m, translate(0.0, cy, 0.0), rotate({1.0, 0.0, 0.0}, 90.0 + 24.0));
	bronze += meshText(torus(R, 0.0050, 48, 8), outer);
	for (int k = 0; k < 24; ++k) {   // the scale round the outer ring, a longer tick every sixth
		const double a = 2.0 * kPi * k / 24.0, len = k % 6 == 0 ? 0.011 : 0.006;
		bronze += rod(transformPoint(outer, {(R + 0.004) * std::cos(a), 0.0, (R + 0.004) * std::sin(a)}), transformPoint(outer, {(R + 0.004 + len) * std::cos(a), 0.0, (R + 0.004 + len) * std::sin(a)}), 0.0013, 4);
	}
	out += attr(metalMaterial({0.78, 0.56, 0.32}, 0.28), bronze);
	// the inner ring, silver, pivoting on the outer one across it
	const double r = 0.047;
	const Mat4 inner = mul(outer, rotate({0.0, 0.0, 1.0}, 58.0));
	std::string silver = meshText(torus(r, 0.0038, 40, 8), inner);
	for (int s = -1; s <= 1; s += 2) silver += rod(transformPoint(outer, {0.0, 0.0, s * (r - 0.002)}), transformPoint(outer, {0.0, 0.0, s * (R - 0.003)}), 0.0024, 6);
	out += attr(metalMaterial({0.86, 0.87, 0.90}, 0.18), silver);
	// the crystal: a long six-sided double point at the centre, glowing
	const double glow = 0.15 + 0.85 * clamp01(glowAmount);
	const Vec3 e = Vec3{0.50, 0.78, 1.0} * (2.6 * glow);
	// (two-sided: a mesh's faces light only to their front, and story space mirrors every mesh, so a closed glowing mesh would light inwards)
	out += "AttributeBegin\n  AreaLightSource \"diffuse\" \"rgb L\" [ " + fmt3(e) + " ] \"bool twosided\" true\n  Material \"diffuse\" \"rgb reflectance\" [ 0 0 0 ]\n" +
		   meshText(lathe({{0.0, -0.034}, {0.0125, -0.010}, {0.0125, 0.010}, {0.0, 0.034}}, 6), mul(m, translate(0.0, cy, 0.0), rotate({0.0, 0.0, 1.0}, 8.0))) + "AttributeEnd\n";
	return out;
}
inline std::string staffText(const ExplorerPose &p, const ExplorerBody &b, const Mat4 &world) {
	if (p.staff == 0) return std::string();
	return sceptreText(mul(world, staffFrame(p, b)), p.staffGlow);
}

// ---- materials ----------------------------------------------------------------------------------------------------------------------------------------------------
inline std::string furMaterial() { return "Material \"diffuse\" \"rgb reflectance\" [ 0.80 0.68 0.52 ]"; }
// The head's fur, as a picture laid round the head from a point inside it (headUV): short tan fur, paler and creamier down the muzzle and under the jaw, a shade darker
// over the brow, round the eyes and on the crown between the horns, and fine streaks running the way the fur lies (back over the skull, down the cheeks and neck).
constexpr double kHeadUvCentreY = 0.06, kHeadUvCentreZ = 0.02;
inline Vec3 headUvDirection(double u, double v) {
	const double th = u * 2.0 * kPi - kPi, ph = (v - 0.5) * kPi;
	return {std::sin(th) * std::cos(ph), std::sin(ph), std::cos(th) * std::cos(ph)};
}
inline Vec3 headFurColour(double u, double v) {
	const Vec3 dir = headUvDirection(u, v);
	Vec3 c{0.80, 0.68, 0.52};
	const double muzzle = smoothstep(0.80, 0.95, dot(dir, normalize(Vec3{0.0, -0.40, 0.92})));
	const double chin = smoothstep(0.55, 0.85, dot(dir, normalize(Vec3{0.0, -0.95, 0.30})));
	c = lerp(c, Vec3{0.90, 0.83, 0.70}, std::max(muzzle, 0.8 * chin));
	const double brow = smoothstep(0.90, 0.97, dot(dir, normalize(Vec3{0.0, 0.42, 0.91})));
	const double crown = smoothstep(0.55, 0.85, dir.y);
	double eyes = 0.0;
	for (int k = 0; k < 3; ++k) {
		const Vec3 e = normalize(eyePlace(k).c - Vec3{0.0, kHeadUvCentreY, kHeadUvCentreZ});
		eyes = std::max(eyes, smoothstep(0.96, 0.995, dot(dir, e)));
	}
	c = c * (1.0 - 0.16 * std::max(brow, crown) - 0.10 * eyes);
	// the streaks: noise drawn out along the way the fur lies (v, down the head), and a finer grain
	const double streak = nature::fbm2(u * 300.0, v * 26.0, 911), grain = nature::fbm2(u * 900.0, v * 140.0, 912);
	return c * (0.88 + 0.16 * streak + 0.08 * (grain - 0.5));
}
inline std::string headFurMaterial() {
	const std::string pic = film_sky::picture(kFilmKey, "fur_head", 1024, 512, [](std::vector<float> &rgb) {
		for (int j = 0; j < 512; ++j)
			for (int i = 0; i < 1024; ++i) {
				const Vec3 c = headFurColour((i + 0.5) / 1024.0, 1.0 - (j + 0.5) / 512.0);
				const std::size_t k = (static_cast<std::size_t>(j) * 1024 + i) * 3;
				rgb[k] = static_cast<float>(c.x);
				rgb[k + 1] = static_cast<float>(c.y);
				rgb[k + 2] = static_cast<float>(c.z);
			}
	});
	return "Texture \"ns_fur\" \"spectrum\" \"imagemap\" \"string filename\" [ \"" + pic + "\" ]\n  Material \"diffuse\" \"texture reflectance\" \"ns_fur\"";
}
// The eyes' irises (the goat's: amber, nearly all of the eye that shows): fine fibres running out from the pupil, a paler ring round it, gold flecks, and a darker rim at
// the edge. Laid flat on the eye from the front (eyeballMesh: u, v across it, the pupil at the middle).
inline Vec3 irisColour(double u, double v) {
	const double x = 2.0 * u - 1.0, y = 2.0 * v - 1.0, r = std::sqrt(x * x + y * y), a = std::atan2(y, x);
	const double fibres = 0.5 + 0.5 * std::sin(a * 70.0 + 3.0 * nature::noise2(a * 6.0, r * 4.0, 921)) * std::sin(a * 23.0 + r * 9.0);
	Vec3 c = lerp(Vec3{0.62, 0.40, 0.09}, Vec3{0.80, 0.56, 0.16}, fibres * (0.6 + 0.4 * nature::fbm2(a * 10.0, r * 12.0, 922)));
	c = lerp(c, Vec3{0.86, 0.70, 0.30}, 0.5 * smoothstep(0.30, 0.38, r) * (1.0 - smoothstep(0.42, 0.52, r)));   // (the collar round the pupil)
	c = c * (1.0 - 0.55 * smoothstep(0.78, 0.97, r));                                                            // (the dark rim)
	return c;
}
inline std::string irisMaterial() {
	const std::string pic = film_sky::picture(kFilmKey, "iris", 256, 256, [](std::vector<float> &rgb) {
		for (int j = 0; j < 256; ++j)
			for (int i = 0; i < 256; ++i) {
				const Vec3 c = irisColour((i + 0.5) / 256.0, 1.0 - (j + 0.5) / 256.0);
				const std::size_t k = (static_cast<std::size_t>(j) * 256 + i) * 3;
				rgb[k] = static_cast<float>(c.x);
				rgb[k + 1] = static_cast<float>(c.y);
				rgb[k + 2] = static_cast<float>(c.z);
			}
	});
	return "Texture \"ns_iris\" \"spectrum\" \"imagemap\" \"string filename\" [ \"" + pic + "\" ]\n  Material \"coateddiffuse\" \"texture reflectance\" \"ns_iris\" \"float roughness\" [ 0.02 ]";
}
// An eyeball of radius r looking along +z, its picture laid flat across it from the front.
inline Mesh eyeballMesh(double r) {
	Mesh m = uvSphere(r, 28, 16);
	m.uvs.resize(m.verts.size());
	for (std::size_t i = 0; i < m.verts.size(); ++i) m.uvs[i] = {0.5 + 0.5 * m.verts[i].x / r, 0.5 + 0.5 * m.verts[i].y / r};
	return m;
}
// Coordinates for a mesh in head space, as headFurColour reads them: the direction from a point inside the head (u round it, the seam at the back of the neck, v up). A
// triangle across the seam gets its own copies of the vertices on the low side, as wrapUV does.
inline void headUV(Mesh &m) {
	m.uvs.resize(m.verts.size());
	for (std::size_t i = 0; i < m.verts.size(); ++i) {
		const Vec3 d = m.verts[i] - Vec3{0.0, kHeadUvCentreY, kHeadUvCentreZ};
		const double r = std::max(1e-9, length(d));
		m.uvs[i] = {(std::atan2(d.x, d.z) + kPi) / (2.0 * kPi), 0.5 + std::asin(std::max(-1.0, std::min(1.0, d.y / r))) / kPi};
	}
	for (auto &t : m.tris) {
		double lo = 1e9, hi = -1e9;
		for (int q = 0; q < 3; ++q) lo = std::min(lo, m.uvs[static_cast<std::size_t>(t[q])].first), hi = std::max(hi, m.uvs[static_cast<std::size_t>(t[q])].first);
		if (hi - lo < 0.5) continue;
		for (int q = 0; q < 3; ++q) {
			const std::size_t i = static_cast<std::size_t>(t[q]);
			if (m.uvs[i].first > 0.5) continue;
			m.verts.push_back(m.verts[i]);
			m.normals.push_back(m.normals[i]);
			m.uvs.push_back({m.uvs[i].first + 1.0, m.uvs[i].second});
			t[q] = static_cast<int>(m.verts.size() - 1);
		}
	}
}
inline std::string beardMaterial() { return "Material \"diffuse\" \"rgb reflectance\" [ 0.92 0.90 0.84 ]"; }
inline std::string ivoryMaterial() { return "Material \"coateddiffuse\" \"rgb reflectance\" [ 0.86 0.80 0.66 ] \"float roughness\" [ 0.35 ]"; }
inline std::string goldMaterial() { return metalMaterial({0.78, 0.56, 0.32}, 0.28); }   // (a warm bronze, for every metal fitting)
// The robe's cloth: a deep sea-green twill embroidered with a star chart - constellations of small silver-cream stars joined by fine stitched lines, a few brighter
// stars with four-pointed glints, and loose single stars between - laid round the robe (u round the body, twice; v up it). The picture repeats across u and v.
// A twill's fine diagonal ribs: their height at (u, v) of the robe's picture (0 .. 1), and the stitched star chart's mask (0 .. 1) and brightness.
constexpr int kRobePicture = 1024;
constexpr double kTwillPeriod = 1.0 / 170.0;   // (u is about 0.6 m round the body: a rib every 3.5 mm, six pixels of the picture)
inline double twillHeight(double u, double v) {
	const double s = (u + v) / kTwillPeriod;
	return std::pow(0.5 + 0.5 * std::cos(2.0 * kPi * s), 1.5);
}
struct StarChart {
	struct Star {
		double u, v, size;
		bool glint;
	};
	std::vector<Star> stars;
	std::vector<std::pair<int, int>> lines;   // (indices into stars)
};
inline const StarChart &robeStarChart() {
	static const StarChart chart = [] {
		StarChart c;
		for (int g = 0; g < 16; ++g) {   // the constellations: a few stars round a centre, joined in a chain with a branch
			const double cu = hash01(g, 931), cv = hash01(g, 932), spread = 0.045 + 0.040 * hash01(g, 933);
			const int n = 4 + static_cast<int>(hash01(g, 934) * 4.0), first = static_cast<int>(c.stars.size());
			for (int i = 0; i < n; ++i) {
				const double a = 2.0 * kPi * hash01(g * 31 + i, 935), r = spread * std::sqrt(hash01(g * 31 + i, 936));
				c.stars.push_back({cu + r * std::cos(a), cv + r * std::sin(a), 0.0024 + 0.0030 * hash01(g * 31 + i, 937), hash01(g * 31 + i, 938) > 0.78});
				if (i > 0) c.lines.push_back({first + i - 1 - (i == n - 1 && n > 4 ? 1 : 0), first + i});
			}
		}
		for (int k = 0; k < 140; ++k) c.stars.push_back({hash01(k, 941), hash01(k, 942), 0.0010 + 0.0012 * hash01(k, 943), false});   // (the loose stars)
		return c;
	}();
	return chart;
}
// How much of the embroidery covers (u, v): 0 bare cloth .. 1 thread.
inline double starChartThread(double u, double v) {
	const StarChart &c = robeStarChart();
	double cover = 0.0;
	for (int wu = -1; wu <= 1; ++wu)
		for (int wv = -1; wv <= 1; ++wv) {
			for (const auto &s : c.stars) {
				const double du = u - s.u - wu, dv = v - s.v - wv;
				if (std::abs(du) > 0.02 || std::abs(dv) > 0.02) continue;
				const double r = std::hypot(du, dv);
				cover = std::max(cover, 1.0 - smoothstep(s.size * 0.7, s.size, r));
				if (s.glint) {
					const double arm = std::min(std::abs(du) + 6.0 * std::abs(dv), 6.0 * std::abs(du) + std::abs(dv));
					cover = std::max(cover, 0.85 * (1.0 - smoothstep(s.size * 2.4, s.size * 3.2, arm)));
				}
			}
			for (const auto &l : c.lines) {
				const auto &a = c.stars[static_cast<std::size_t>(l.first)], &b = c.stars[static_cast<std::size_t>(l.second)];
				const double ax = u - a.u - wu, ay = v - a.v - wv, bx = b.u - a.u, by = b.v - a.v;
				const double t = clamp01((ax * bx + ay * by) / std::max(1e-12, bx * bx + by * by));
				const double dist = std::hypot(ax - bx * t, ay - by * t);
				// (a running stitch: the line is broken into short stitches)
				const double stitch = std::fmod(t * std::hypot(bx, by) / 0.0045 + 10.0, 1.0) < 0.72 ? 1.0 : 0.0;
				cover = std::max(cover, 0.75 * stitch * (1.0 - smoothstep(0.0005, 0.0010, dist)));
			}
		}
	return cover;
}
// The embroidery's cover at each pixel of the robe's picture (row 0 at the top, v = 1), worked out once for the picture and its normal map.
inline const std::vector<float> &robeThreadGrid() {
	static const std::vector<float> grid = [] {
		const int N = kRobePicture;
		std::vector<float> g(static_cast<std::size_t>(N) * N);
		film_sky::eachRow(N, [&g, N](int j) {
			for (int i = 0; i < N; ++i) g[static_cast<std::size_t>(j) * N + i] = static_cast<float>(starChartThread((i + 0.5) / N, 1.0 - (j + 0.5) / N));
		});
		return g;
	}();
	return grid;
}
inline std::string robeTexture() {
	return film_sky::picture(kFilmKey, "robe_starchart", kRobePicture, kRobePicture, [](std::vector<float> &rgb) {
		const int N = kRobePicture;
		const std::vector<float> &thread = robeThreadGrid();
		film_sky::eachRow(N, [&rgb, &thread, N](int j) {
			for (int i = 0; i < N; ++i) {
				const double u = (i + 0.5) / N, v = 1.0 - (j + 0.5) / N;
				const double n = nature::fbm2(u * 60.0, v * 60.0, 907), slub = nature::fbm2(u * 9.0, v * 170.0, 908);
				Vec3 c = Vec3{0.075, 0.215, 0.200} * (0.86 + 0.16 * n + 0.08 * (slub - 0.5)) * (0.90 + 0.12 * twillHeight(u, v));
				c = lerp(c, Vec3{0.80, 0.79, 0.70}, 0.92 * thread[static_cast<std::size_t>(j) * N + i]);
				const std::size_t k = (static_cast<std::size_t>(j) * N + i) * 3;
				rgb[k] = static_cast<float>(c.x);
				rgb[k + 1] = static_cast<float>(c.y);
				rgb[k + 2] = static_cast<float>(c.z);
			}
		});
	});
}
// The weave's normal map: the twill's ribs and the embroidery standing a little proud of the cloth.
inline std::string robeNormalTexture() {
	return film_sky::picture(kFilmKey, "robe_starchart_n", kRobePicture, kRobePicture, [](std::vector<float> &rgb) {
		const int N = kRobePicture;
		const std::vector<float> &thread = robeThreadGrid();
		const double du = 1.0 / N, mPerU = 0.60, mPerV = 1.0 / 0.9;   // (metres per unit of u and of v, as wrapUV lays them)
		const auto height = [&thread, N](int i, int j) {
			i = (i % N + N) % N, j = (j % N + N) % N;
			return 0.00025 * twillHeight((i + 0.5) / N, 1.0 - (j + 0.5) / N) + 0.00040 * thread[static_cast<std::size_t>(j) * N + i];
		};
		film_sky::eachRow(N, [&](int j) {
			for (int i = 0; i < N; ++i) {
				const double sx = (height(i + 1, j) - height(i - 1, j)) / (2.0 * du * mPerU), sy = (height(i, j - 1) - height(i, j + 1)) / (2.0 * du * mPerV);
				const Vec3 nn = normalize(Vec3{-sx, -sy, 1.0});
				const std::size_t k = (static_cast<std::size_t>(j) * N + i) * 3;
				rgb[k] = static_cast<float>(0.5 + 0.5 * nn.x);
				rgb[k + 1] = static_cast<float>(0.5 + 0.5 * nn.y);
				rgb[k + 2] = static_cast<float>(0.5 + 0.5 * nn.z);
			}
		});
	});
}
inline std::string robeMaterial() {
	return "Texture \"ns_robe\" \"spectrum\" \"imagemap\" \"string filename\" [ \"" + robeTexture() + "\" ]\n  Material \"diffuse\" \"texture reflectance\" \"ns_robe\" \"string normalmap\" [ \"" +
		   robeNormalTexture() + "\" ]";
}
// The borders' embroidered cloth: a rust-red weave worked with rows of small cream stars between pairs of dots, the same seen along a band or across it (the borders run
// both round the robe and up it).
inline std::string borderMaterial() {
	const std::string pic = film_sky::picture(kFilmKey, "border_stars", 512, 512, [](std::vector<float> &rgb) {
		const int N = 512;
		for (int j = 0; j < N; ++j)
			for (int i = 0; i < N; ++i) {
				const double u = (i + 0.5) / N, v = 1.0 - (j + 0.5) / N;
				const double weave = 0.5 + 0.5 * std::sin(2.0 * kPi * u * 160.0) * std::sin(2.0 * kPi * v * 160.0);
				Vec3 c = Vec3{0.43, 0.15, 0.075} * (0.88 + 0.14 * weave + 0.10 * (nature::fbm2(u * 40.0, v * 40.0, 951) - 0.5));
				// a lattice of motifs every 1/16: a four-pointed star at the cell's middle, a dot at each corner
				const double cu = u * 16.0 - std::floor(u * 16.0) - 0.5, cv = v * 16.0 - std::floor(v * 16.0) - 0.5;
				const double star = std::min(std::abs(cu) + 3.2 * std::abs(cv), 3.2 * std::abs(cu) + std::abs(cv));
				double thread = 1.0 - smoothstep(0.20, 0.25, star);
				thread = std::max(thread, 1.0 - smoothstep(0.045, 0.065, std::hypot(std::abs(cu) - 0.5, std::abs(cv) - 0.5)));
				c = lerp(c, Vec3{0.86, 0.78, 0.58}, 0.90 * thread);
				const std::size_t k = (static_cast<std::size_t>(j) * N + i) * 3;
				rgb[k] = static_cast<float>(c.x);
				rgb[k + 1] = static_cast<float>(c.y);
				rgb[k + 2] = static_cast<float>(c.z);
			}
	});
	return "Texture \"ns_border\" \"spectrum\" \"imagemap\" \"string filename\" [ \"" + pic + "\" ]\n  Material \"diffuse\" \"texture reflectance\" \"ns_border\"";
}
// Coordinates round the body for a mesh in body space: u the angle round the vertical (twice round), v the height; a triangle across the seam at the back gets its own
// copies of the vertices on the low side, so the picture does not smear across it.
inline void wrapUV(Mesh &m) {
	m.uvs.resize(m.verts.size());
	for (std::size_t i = 0; i < m.verts.size(); ++i) m.uvs[i] = {(std::atan2(m.verts[i].x, m.verts[i].z) + kPi) / kPi, m.verts[i].y * 0.9};
	for (auto &t : m.tris) {
		double lo = 1e9, hi = -1e9;
		for (int q = 0; q < 3; ++q) lo = std::min(lo, m.uvs[static_cast<std::size_t>(t[q])].first), hi = std::max(hi, m.uvs[static_cast<std::size_t>(t[q])].first);
		if (hi - lo < 1.0) continue;
		for (int q = 0; q < 3; ++q) {
			const std::size_t i = static_cast<std::size_t>(t[q]);
			if (m.uvs[i].first > 1.0) continue;
			m.verts.push_back(m.verts[i]);
			m.normals.push_back(m.normals[i]);
			m.uvs.push_back({m.uvs[i].first + 2.0, m.uvs[i].second});
			t[q] = static_cast<int>(m.verts.size() - 1);
		}
	}
}

// ---- the figure ----------------------------------------------------------------------------------------------------------------------------------------------------
// The explorer's feet in pose `p` at `world` (body space -> story space): the balls of its feet, and whether each is on the ground (Film::feetAt: the sliding-feet check).
inline std::vector<Foot> explorerFeet(const ExplorerPose &p, const Mat4 &world) {
	const ExplorerBody b = explorerBody(p);
	return {{transformPoint(world, b.ball[0]), b.planted[0]}, {transformPoint(world, b.ball[1]), b.planted[1]}};
}
// The mask's frame (mask space -> body space): on the face, lifted away from it, then lowered into the left hand.
inline Mat4 maskFrame(const ExplorerPose &p, const ExplorerBody &b) {
	if (p.maskOff <= 0.0) return b.head;
	const double u = clamp01(p.maskOff);
	const Mat4 lifted = mul(b.head, translate(0.0, 0.0, 0.08 * smoothstep(0.0, 0.35, u)));
	if (u <= 0.35) return lifted;
	const Vec3 hand = b.wrist[0] + b.handDir[0] * 0.06;
	const Mat4 held = mul(translate(hand + Vec3{0.0, 0.03, 0.0}), rotate({1.0, 0.0, 0.0}, -60.0), rotate({0.0, 1.0, 0.0}, 25.0), translate(0.0, -0.10, -0.12));
	const double v = smoothstep(0.35, 1.0, u);
	Mat4 m;
	for (int i = 0; i < 16; ++i) m[static_cast<std::size_t>(i)] = lerp(lifted[static_cast<std::size_t>(i)], held[static_cast<std::size_t>(i)], v);
	Vec3 x{m[0], m[4], m[8]}, y{m[1], m[5], m[9]};
	x = normalize(x);
	Vec3 z = normalize(cross(x, y));
	y = cross(z, x);
	m[0] = x.x; m[4] = x.y; m[8] = x.z;
	m[1] = y.x; m[5] = y.y; m[9] = y.z;
	m[2] = z.x; m[6] = z.y; m[10] = z.z;
	return m;
}
inline Mesh maskIvoryMesh() {   // (fine: the engraving)
	return sdf::surfaceNets([](Vec3 q) { return maskIvorySdf(q); }, {-0.175, -0.125, 0.06}, {0.175, 0.290, 0.30}, 0.0011);
}
inline Mesh maskBladesMesh() {
	Mesh m;
	for (int s = -1; s <= 1; s += 2) {
		const Mesh side = sdf::surfaceNets([s](Vec3 q) { return maskBladesSdf(q, s); }, {s > 0 ? 0.10 : -0.26, 0.09, -0.03}, {s > 0 ? 0.26 : -0.10, 0.31, 0.16}, 0.0012);
		const int base = static_cast<int>(m.verts.size());
		m.verts.insert(m.verts.end(), side.verts.begin(), side.verts.end());
		m.normals.insert(m.normals.end(), side.normals.begin(), side.normals.end());
		for (auto t : side.tris) m.tris.push_back({t[0] + base, t[1] + base, t[2] + base});
	}
	return m;
}
inline Mesh maskBronzeMesh() { return sdf::surfaceNets([](Vec3 q) { return maskBronzeSdf(q); }, {-0.26, -0.11, -0.03}, {0.26, 0.31, 0.30}, 0.0015); }
inline Mesh maskStarMesh() { return sdf::surfaceNets([](Vec3 q) { return maskStarSdf(q); }, {-0.12, 0.14, 0.12}, {0.12, 0.29, 0.20}, 0.0006); }
inline Mesh maskDarkMesh() { return sdf::surfaceNets([](Vec3 q) { return maskDarkSdf(q); }, {-0.07, -0.11, 0.17}, {0.07, 0.11, 0.30}, 0.0012); }
inline std::string boneIvoryMaterial() { return "Material \"coateddiffuse\" \"rgb reflectance\" [ 0.84 0.79 0.68 ] \"float roughness\" [ 0.28 ]"; }
inline std::string silverMaterial() { return metalMaterial({0.86, 0.87, 0.90}, 0.18); }
// The mask on its own (laid on the ground beside the campfire): at `frame` (mask space -> world). Its meshes never change: made once as PLY parts (film_cache::part,
// "mask3_": our own mask). Its inlay glows at `glow` (0 .. 1).
inline std::string maskText(const Mat4 &frame, double glow = 0.0) {
	const Vec3 e = Vec3{0.55, 0.80, 1.0} * (0.6 + 1.4 * clamp01(glow));
	return film_cache::part(kFilmKey, "mask3_ivory", boneIvoryMaterial(), frame, [] { return maskIvoryMesh(); }) +
		   film_cache::part(kFilmKey, "mask3_blades", boneIvoryMaterial(), frame, [] { return maskBladesMesh(); }) +
		   film_cache::part(kFilmKey, "mask3_bronze", goldMaterial(), frame, [] { return maskBronzeMesh(); }) +
		   film_cache::part(kFilmKey, "mask3_stars", silverMaterial(), frame, [] { return maskStarMesh(); }) +
		   film_cache::part(kFilmKey, "mask3_dark", diffuseMaterial({0.035, 0.030, 0.028}), frame, [] { return maskDarkMesh(); }) +
		   attr("Material \"diffuse\" \"rgb reflectance\" [ 0 0 0 ]", meshText(torus(kGlowRing, 0.0018, 36, 5), mul(frame, glowRingFrame())), &e);
}

// The explorer standing at `world` (body space -> story space) in pose `p`, sculpted at `cell` metres (the face and the hands finer).
inline std::string explorerText(const ExplorerPose &p, const Mat4 &world, double cell = 0.012) {
	const ExplorerBody b = explorerBody(p);
	const double fine = cell * 0.4;
	// the parts that never change shape - the horns (head space), the pack and its tank (the neck's space: they only move with it) - are made once a cell size as PLY parts
	const std::string grain = "_" + std::to_string(std::lround(cell * 1e5));
	ExplorerBody atNeck = b;
	atNeck.neck = {0.0, 0.0, 0.0};
	const Mat4 neck = mul(world, translate(b.neck));
	const Vec3 lo{-0.70, -0.02, -0.70}, hi{0.70, 1.90, 0.80};
	std::string out;
	// the head: fur, eyelids, ears, the beard (let down when the mask comes off), the horns, the eyes
	const Mat4 head = mul(world, b.head);
	const Vec3 hlo{-0.24, -0.30, -0.30}, hhi{0.24, 0.38, 0.30};
	const double lidClose[3] = {p.lidL, p.lidR, p.lidTop};
	Mesh skin = sdf::surfaceNets([&](Vec3 q) {
		double d = headSkinSdf(q);
		for (int k = 0; k < 3; ++k) d = std::min(d, eyelidSdf(q, k, lidClose[k]));
		return std::min(d, earSdf(q, p.ears));
	}, {-0.20, -0.22, -0.22}, {0.20, 0.26, 0.27}, fine);
	headUV(skin);
	out += attr(headFurMaterial(), meshText(skin, head));
	const double beardLength = p.maskShown ? smoothstep(0.2, 0.9, p.maskOff) : 1.0;
	out += attr(beardMaterial(), meshText(sdf::surfaceNets([&](Vec3 q) { return beardSdf(q, beardLength); }, {-0.10, -0.32, 0.02}, {0.10, 0.0, 0.26}, fine), head));
	// (made once, so finer than the rest of the head: the growth rings are a centimetre apart. "horns2": the rings run round the horns now)
	out += film_cache::part(kFilmKey, "horns2" + grain, "Material \"coateddiffuse\" \"rgb reflectance\" [ 0.55 0.42 0.30 ] \"float roughness\" [ 0.5 ]", head,
							[&] { return sdf::surfaceNets([](Vec3 q) { return hornSdf(q); }, {-0.22, 0.0, -0.26}, {0.22, 0.38, 0.08}, fine * 0.6); });
	std::string eyes, pupils;
	for (int k = 0; k < 3; ++k) {
		const EyePlace &e = eyePlace(k);
		const Mat4 look = mul(translate(e.c), rotate({0.0, 1.0, 0.0}, e.outward * 0.5 + p.gazeYaw), rotate({1.0, 0.0, 0.0}, p.gazePitch));
		eyes += meshText(eyeballMesh(e.r), mul(head, look));
		pupils += meshText(roundedBox(e.r * 1.05, e.r * 0.34, e.r * 0.25, e.r * 0.12, 2), mul(head, look, translate(0.0, 0.0, e.r * 0.90)));
	}
	out += attr(irisMaterial(), eyes);
	out += attr("Material \"coateddiffuse\" \"rgb reflectance\" [ 0.008 0.008 0.008 ] \"float roughness\" [ 0.02 ]", pupils);
	if (p.maskShown) out += maskText(mul(world, maskFrame(p, b)), p.staffGlow);
	// the suit
	Mesh robe = sdf::surfaceNets([&](Vec3 q) { return outerRobeSdf(q, p, b); }, lo, hi, cell);
	wrapUV(robe);
	out += attr(robeMaterial(), meshText(robe, world));
	out += attr(diffuseMaterial({0.15, 0.28, 0.28}), meshText(sdf::surfaceNets([&](Vec3 q) { return trousersSdf(q, p, b); }, {-0.4, -0.05, -0.4}, {0.4, b.pelvis.y + 0.05, 0.5}, cell), world));
	out += attr(diffuseMaterial({0.50, 0.23, 0.11}), meshText(sdf::surfaceNets([&](Vec3 q) { return hipWrapSdf(q, b); }, {-0.4, 0.2, -0.4}, {0.4, b.pelvis.y + 0.15, 0.5}, cell * 0.6), world));
	Mesh border = sdf::surfaceNets([&](Vec3 q) { return trimSdf(q, p, b); }, lo, hi, cell * 0.5);
	wrapUV(border);
	for (auto &uv : border.uvs) uv = {uv.first * 2.0, uv.second * 2.0};   // (the border's motifs: about 2.3 cm apart round the robe)
	out += attr(borderMaterial(), meshText(border, world));
	out += attr(goldMaterial(), meshText(sdf::surfaceNets([&](Vec3 q) { return std::min(cuffBandSdf(q, b), std::min(collarSdf(q, b), padRingSdf(q, b))); }, lo, hi, cell * 0.5), world));
	out += attr(silverMaterial(), meshText(sdf::surfaceNets([&](Vec3 q) { return creamLineSdf(q, p, b); }, {-0.55, -0.02, -0.55}, {0.55, b.pelvis.y + 0.02, 0.65}, cell * 0.4), world));
	out += attr(coated({0.27, 0.15, 0.085}, 0.45), meshText(sdf::surfaceNets([&](Vec3 q) { return padSdf(q, b); }, {-0.45, b.chest.y - 0.25, -0.30}, {0.45, b.neck.y + 0.15, 0.30}, cell * 0.5), world));
	// the life-support pack and the hose
	const Vec3 plo{-0.40, -0.15, -0.45}, phi{0.40, 0.40, 0.0};
	out += film_cache::part(kFilmKey, "pack" + grain, metalMaterial({0.60, 0.44, 0.26}, 0.40), neck,
							[&] { return sdf::surfaceNets([&](Vec3 q) { return packHousingSdf(q, atNeck); }, plo, phi, cell * 0.6); });
	out += film_cache::part(kFilmKey, "tank" + grain, ivoryMaterial(), neck,
							[&] { return sdf::surfaceNets([&](Vec3 q) { return std::min(tankSdf(q, atNeck), tankCapSdf(q, atNeck)); }, plo, phi, cell * 0.5); });
	out += film_cache::part(kFilmKey, "tank_gold" + grain, goldMaterial(), neck, [&] { return sdf::surfaceNets([&](Vec3 q) { return tankGoldSdf(q, atNeck); }, plo, phi, cell * 0.5); });
	if (p.maskShown) {
		const Mat4 mf = maskFrame(p, b);
		const Vec3 chin = transformPoint(mf, {0.020, -0.070, 0.180});
		const Vec3 a1 = b.neck + Vec3{0.040, -0.230, 0.190}, a2 = b.neck + Vec3{0.230, -0.020, 0.120}, a3 = b.neck + Vec3{0.320, 0.120, -0.340};
		std::vector<Vec3> hose;
		for (int i = 0; i <= 12; ++i) {
			const double u = i / 12.0;
			// (a smooth curve through chin, chest, shoulder, pack)
			const Vec3 p01 = lerp(chin, a1, u), p12 = lerp(a1, a2, u), p23 = lerp(a2, a3, u);
			hose.push_back(lerp(lerp(p01, p12, u), lerp(p12, p23, u), u));
		}
		out += attr(coated({0.05, 0.05, 0.06}, 0.3), meshText(sdf::tubeMesh(hose, 0.010, 1.0, 8), world));
	}
	for (int s = 0; s < 2; ++s) {
		const double grip = (p.staff == (s == 0 ? 1 : -1)) ? 0.9 : 0.25;
		const Vec3 c = b.wrist[s] + b.handDir[s] * 0.06;
		out += attr(diffuseMaterial({0.34, 0.17, 0.11}), meshText(sdf::surfaceNets([&](Vec3 q) { return handSdf(q, b, s, grip); }, c - Vec3{0.12, 0.12, 0.12}, c + Vec3{0.12, 0.12, 0.12}, fine), world));
		out += attr(diffuseMaterial({0.90, 0.86, 0.74}), meshText(sdf::surfaceNets([&](Vec3 q) { return fingerTipSdf(q, b, s, grip); }, c - Vec3{0.12, 0.12, 0.12}, c + Vec3{0.12, 0.12, 0.12}, fine * 0.7), world));
	}
	out += attr(ivoryMaterial(), meshText(sdf::surfaceNets([&](Vec3 q) { return std::max(footSdf(q, b), q.y - b.hemY - 0.02); }, {-0.45, -0.05, -0.35}, {0.45, b.hemY + 0.05, 0.6}, cell * 0.5), world));
	out += attr(diffuseMaterial({0.38, 0.26, 0.14}), meshText(sdf::surfaceNets([&](Vec3 q) { return clawSdf(q, b); }, {-0.45, -0.05, -0.35}, {0.45, 0.12, 0.6}, cell * 0.4), world));
	out += attr(goldMaterial(), meshText(sdf::surfaceNets([&](Vec3 q) { return ankleBandSdf(q, b); }, {-0.45, -0.05, -0.35}, {0.45, b.hemY + 0.05, 0.6}, cell * 0.4), world));
	out += staffText(p, b, world);
	return out;
}

}  // namespace nomai
}  // namespace cartoon
