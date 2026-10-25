#pragma once
// nomai_explorer.h -- the explorer of "The Last Nomai Signal" (nomai_signal.h): an original Nomai, made after the game's own (Mobius Digital's concept art of the Nomai
// suit, the in-game suit of Solanum, the statue in the Observatory, the wiki): tall and slender (1.86 m), digitigrade, with a goat-like face, a wide nose and a long
// flowing beard, three amber eyes with the goat's level oblong pupils (two below, one on the forehead), two horns curling back like a ram's and a top antler that forks
// into two points, short tan fur, small three-fingered hands. It wears the Nomai's life-support suit: a deep green robe patterned with fine cream lines, trimmed in gold
// (the opening, the hem, the chest, the cuffs, a waist band), a gold collar with the statue's square-key steps, woven rust-red pads on the shoulders, dark green
// trousers, big pale three-toed feet, a bronze pack on the back with a horizontal ivory tank, a hose to the mask - and the mask itself, an ivory-and-gold helmet with a
// diamond face, three diamond windows for the eyes, a crown of gold prongs and an ivory beak over the muzzle. It carries the writing sceptre: a wooden shaft with a boxy
// ivory head and gold rods.
//
// The figure is sculpted for each frame from its pose (signed distance functions blended like clay, meshed by sdf::surfaceNets), so every part follows the pose: the robe's
// skirt swings with the long, slow steps of low gravity and stirs in the wind, each eyelid closes on its own, the third eye can open after the other two, the mask comes off
// into a hand and the beard falls free.
//
// Body space: the origin on the ground between the feet, +y up, the figure facing +z; +x is the figure's left. The head has a space of its own (its pivot at the top of the
// neck, +z forward), which the pose turns.

#include <algorithm>
#include <cmath>
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
// A walking pose: long, slow strides, the staff in the right hand, the left arm swinging a little.
inline ExplorerPose walkPose(double walk, double t, double stride = 1.0) {
	ExplorerPose p;
	p.walk = walk;
	p.stride = stride;
	p.time = t;
	const double sw = std::sin(walk * kPi);
	p.handL = {0.21, 0.90, 0.06 - 0.10 * sw * stride};
	p.handR = {-0.25, 1.18, 0.16 + 0.06 * sw * stride};
	p.headPitch = 4.0;
	p.wind = {0.03, 0.0, -0.025};
	p.staffGlow = 0.22;
	blink(p, t);
	return p;
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
	// locks: ripples down its length
	const double along = clamp01((top.y - q.y) / L);
	return d + 0.004 * std::sin(std::atan2(q.x, q.z - 0.06) * 9.0 + along * 6.0) * along;
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
inline double hornSdf(Vec3 q) {
	double d = 1e9;
	for (std::size_t c = 0; c < hornCurves().size(); ++c) {
		const auto &curve = hornCurves()[c];
		const int n = static_cast<int>(curve.size());
		const double base = c < 2 ? 0.024 : (c == 2 ? 0.020 : 0.011);
		const double end = c < 2 ? 0.006 : (c == 2 ? 0.012 : 0.004);
		for (int i = 0; i + 1 < n; ++i) {
			const double r0 = lerp(base, end, static_cast<double>(i) / (n - 1)), r1 = lerp(base, end, static_cast<double>(i + 1) / (n - 1));
			d = std::min(d, sdf::roundCone(q, curve[static_cast<std::size_t>(i)], curve[static_cast<std::size_t>(i + 1)], r0, r1));
		}
	}
	return d + 0.0014 * std::sin(length(q) * 300.0);   // (the rings along a horn)
}

// ---- the life-support mask ------------------------------------------------------------------------------------------------------------------------------------------
// After Mobius Digital's concept sheet of the Nomai suit: a big angular helmet of ivory edged in gold. In the middle a cluster of four small diamonds set as one large
// diamond - the top one and the two at the sides are the windows of the three eyes, the bottom one a dark inset - each ringed in gold, on an ivory face plate; angular
// wings step out from its sides with small teal squares in them; two big curved ivory arcs rise from its top like horns round a central gold spine; a great gold fan
// arc stands behind the head; an ivory beak with a gold ridge covers the long muzzle below. Four materials: ivory, gold, a dark brown and the teal.
// `q` in the mask's own space (the head's, when it is worn).
inline Vec3 maskCentre() { return {0.0, 0.104, 0.172}; }
// A point's coordinates on the bent face plate: (along one diagonal, along the other, depth).
inline Vec3 maskPlate(Vec3 q) {
	const Vec3 c = maskCentre();
	const double x = q.x - c.x, y = q.y - c.y;
	const double z = q.z - c.z + 2.0 * x * x + 0.7 * y * y;
	return {(x + y) * 0.70710678, (y - x) * 0.70710678, z};
}
inline double diamondSdf(Vec3 p, double half, double depth) {   // a square in plate coordinates (a diamond on the face), `half` its half-diagonal
	const double s = half * 0.70710678;
	const Vec3 d{std::abs(p.x) - s, std::abs(p.y) - s, std::abs(p.z) - depth};
	const Vec3 m{std::max(d.x, 0.0), std::max(d.y, 0.0), std::max(d.z, 0.0)};
	return length(m) + std::min(std::max(d.x, std::max(d.y, d.z)), 0.0);
}
// The four small diamonds: top, left, right (the eyes), bottom (an inset).
inline Vec3 maskDiamondAt(int k) {
	if (k < 3) {
		const EyePlace &e = eyePlace(k);
		return {e.c.x + 0.07 * std::sin(e.outward * kPi / 180.0), e.c.y, 0.0};
	}
	return {0.0, 0.040, 0.0};
}
constexpr double kMaskSmall = 0.031;   // half-diagonal of a small diamond
inline double smallDiamond(Vec3 q, int k, double grow, double depth, double lift = 0.0) {
	const Vec3 at = maskDiamondAt(k);
	const Vec3 p = maskPlate(q), c = maskPlate({at.x, at.y, q.z});
	return diamondSdf({p.x - c.x, p.y - c.y, p.z - lift}, kMaskSmall + grow, depth);
}
inline Mat4 beakFrameInv() {
	static const Mat4 m = invertAffine(mul(translate(0.0, 0.010, 0.150), rotate({1.0, 0.0, 0.0}, 22.0)));
	return m;
}
// A thick flat ribbon along an arc in the plane z = zc of mask space: centre (cx, cy), radius r, from angle a0 to a1 (radians, from +x towards +y), `w` wide, `t` thick.
inline double arcRibbon(Vec3 q, double cx, double cy, double zc, double r, double a0, double a1, double w, double t) {
	const double dx = q.x - cx, dy = q.y - cy;
	double a = std::atan2(dy, dx);
	const double mid = 0.5 * (a0 + a1), half = 0.5 * std::abs(a1 - a0);
	const double off = std::abs(std::remainder(a - mid, 2.0 * kPi)) - half;
	const double radial = std::abs(std::sqrt(dx * dx + dy * dy) - r) - w * 0.5;
	const double along = std::max(0.0, off * r);
	const double inPlane = along > 0.0 ? std::sqrt(std::max(0.0, radial) * std::max(0.0, radial) + along * along) : radial;
	return std::max(inPlane, std::abs(q.z - zc) - t * 0.5);
}
inline double maskIvorySdf(Vec3 q) {
	using namespace sdf;
	const Vec3 p = maskPlate(q);
	// the face plate: a broad diamond, its corners clipped into steps
	double d = diamondSdf(p, 0.118, 0.010);
	d = std::max(d, std::abs(q.x) - 0.105);
	// the wings stepping out from its sides, turned back round the head
	for (int s = -1; s <= 1; s += 2) {
		const Mat4 w = invertAffine(mul(translate(0.125 * s, 0.100, 0.132), rotate({0.0, 1.0, 0.0}, 34.0 * s)));
		d = std::min(d, roundBox(q, w, {0.040, 0.040, 0.009}, 0.003));
		const Mat4 w2 = invertAffine(mul(translate(0.150 * s, 0.050, 0.110), rotate({0.0, 1.0, 0.0}, 42.0 * s)));
		d = std::min(d, roundBox(q, w2, {0.024, 0.026, 0.008}, 0.003));
	}
	// the crown's two great arcs, curling out like horns from the top corner
	for (int s = -1; s <= 1; s += 2) d = std::min(d, arcRibbon(q, 0.070 * s, 0.250, 0.150, 0.060, s > 0 ? -2.4 : kPi - 1.0, s > 0 ? 1.0 : kPi + 2.4, 0.028, 0.014));
	// the beak
	const Vec3 b = transformPoint(beakFrameInv(), q);
	double shell = std::abs(ellipsoid(b, identity(), {0.066, 0.100, 0.118})) - 0.0065;
	shell = std::max(shell, 0.010 - b.z);
	shell = std::max(shell, b.y - 0.040);
	d = std::min(d, shell);
	// the windows (through) and the inset (shallow)
	for (int k = 0; k < 3; ++k) d = std::max(d, -smallDiamond(q, k, 0.0, 1.0));
	d = std::max(d, -smallDiamond(q, 3, 0.0, 0.006, 0.010));
	return d;
}
inline double maskGoldSdf(Vec3 q) {
	using namespace sdf;
	const Vec3 p = maskPlate(q);
	// the rim of the plate and the rings of the four small diamonds
	double d = std::max(diamondSdf({p.x, p.y, p.z - 0.002}, 0.126, 0.012), -diamondSdf(p, 0.116, 1.0));
	d = std::max(d, std::abs(q.x) - 0.110);
	for (int k = 0; k < 4; ++k) d = std::min(d, std::max(smallDiamond(q, k, 0.009, 0.013, 0.004), -smallDiamond(q, k, 0.0, 1.0)));
	// the edges of the crown's arcs and of the wings
	for (int s = -1; s <= 1; s += 2) {
		d = std::min(d, arcRibbon(q, 0.070 * s, 0.250, 0.150, 0.075, s > 0 ? -2.2 : kPi - 0.9, s > 0 ? 0.9 : kPi + 2.2, 0.007, 0.012));
		const Mat4 w = invertAffine(mul(translate(0.125 * s, 0.100, 0.132), rotate({0.0, 1.0, 0.0}, 34.0 * s)));
		d = std::min(d, std::max(roundBox(q, w, {0.046, 0.046, 0.011}, 0.003), -roundBox(q, w, {0.038, 0.038, 0.02}, 0.002)));
	}
	// the central spine of the crown and its cross piece
	const auto bar3 = [&](Vec3 a, Vec3 e, double r) { return roundCone(q, a, e, r, r); };
	const Vec3 c = maskCentre();
	d = std::min(d, bar3(c + Vec3{0.0, 0.110, -0.012}, c + Vec3{0.0, 0.215, -0.030}, 0.008));
	d = std::min(d, bar3(c + Vec3{-0.035, 0.175, -0.024}, c + Vec3{0.035, 0.175, -0.024}, 0.006));
	// the great fan behind the head: a half ring standing on the crown, two spokes
	const double fz = -0.060;
	d = std::min(d, arcRibbon(q, 0.0, 0.250, fz, 0.150, 0.15, kPi - 0.15, 0.030, 0.016));
	d = std::min(d, bar3({0.0, 0.250, fz}, {0.106, 0.356, fz}, 0.009));
	d = std::min(d, bar3({0.0, 0.250, fz}, {-0.106, 0.356, fz}, 0.009));
	d = std::min(d, bar3({0.0, 0.200, fz + 0.02}, {0.0, 0.250, fz}, 0.012));
	// the ridge down the beak
	const Vec3 b = transformPoint(beakFrameInv(), q);
	const double onShell = std::abs(ellipsoid(b, identity(), {0.066, 0.100, 0.118})) - 0.0085;
	d = std::min(d, std::max(std::max(onShell, std::abs(b.x) - 0.006), std::max(b.y - 0.035, 0.030 - b.z)));
	return d;
}
inline double maskDarkSdf(Vec3 q) { return smallDiamond(q, 3, -0.004, 0.004, 0.006); }   // the bottom diamond's dark centre
inline double maskTealSdf(Vec3 q) {   // the small teal squares in the wings
	double d = 1e9;
	for (int s = -1; s <= 1; s += 2) {
		const Mat4 w = invertAffine(mul(translate(0.125 * s, 0.100, 0.132), rotate({0.0, 1.0, 0.0}, 34.0 * s)));
		d = std::min(d, sdf::roundBox(q, mul(translate(0.0, 0.0, -0.010), w), {0.014, 0.014, 0.004}, 0.002));
	}
	return d;
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
	const double hem = b.hemY + 0.012 * std::sin(std::atan2(q.x, q.z) * 7.0);
	return std::max(d, hem - q.y);
}
inline double torsoSdf(Vec3 q, const ExplorerBody &b, double inset) {
	using namespace sdf;
	double d = roundCone(q, b.neck + Vec3{0.0, -0.035, 0.0}, b.pelvis + Vec3{0.0, 0.02, 0.0}, 0.120 - inset, 0.180 - inset);
	d = smin(d, ellv(q, b.chest + Vec3{0.0, -0.02, 0.0}, {0.185 - inset, 0.18, 0.125 - inset}), 0.06);
	return d;
}
// The robe's front opening below the waist: how far it is from the middle at a height.
inline double openingHalf(const ExplorerBody &b, double y) { return 0.014 + 0.11 * std::max(0.0, b.pelvis.y - y); }
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
// The gold trim: bands down both edges of the front panel and the slits' outer edges, the hem, the back slit's edges, the stepped yoke round the neck front and back,
// the waist band across the back, and three bands at each cuff.
inline double trimSdf(Vec3 q, const ExplorerPose &p, const ExplorerBody &b) {
	const double surface = std::abs(outerRobeSdf(q, p, b) + 0.002) - 0.006;
	double band = 1e9;
	if (q.y < b.pelvis.y - 0.02 && q.z > 0.0) {
		const double ph = panelHalf(b, q.y);
		band = std::min(std::abs(std::abs(q.x) - ph + 0.012) - 0.012, std::abs(std::abs(q.x) - ph - kSlit - 0.010) - 0.010);
	}
	if (q.y < b.pelvis.y - 0.08 && q.z < 0.0) band = std::min(band, std::abs(std::abs(q.x) - (0.010 + 0.10 * (b.pelvis.y - 0.08 - q.y)) - 0.012) - 0.012);
	band = std::min(band, std::abs(q.y - b.hemY - 0.030) - 0.024);
	// the yoke: a stepped frame round the neck, front and back
	const double yy = q.y - (b.chest.y - 0.02);
	if (q.y > b.chest.y - 0.08 && q.y < b.neck.y) {
		band = std::min(band, std::abs(std::abs(q.x) - 0.085) - 0.011);
		band = std::min(band, std::max(std::abs(yy + 0.050) - 0.011, std::abs(q.x) - 0.096));
	}
	if (q.y > b.pelvis.y - 0.02 && q.y < b.chest.y - 0.06 && q.z > 0.0) band = std::min(band, std::abs(std::abs(q.x) - 0.075) - 0.011);
	if (q.z < 0.0) band = std::min(band, std::abs(q.y - (b.pelvis.y + 0.06)) - 0.020);
	double d = std::max(surface, band);
	for (int s = 0; s < 2; ++s)
		for (int k = 0; k < 3; ++k) {
			const Vec3 c = b.wrist[s] - b.handDir[s] * (0.020 + 0.034 * k);
			const Vec3 v = q - c;
			const double along = dot(v, b.handDir[s]);
			const double radial = length(v - b.handDir[s] * along);
			d = std::min(d, std::max(std::abs(radial - (0.050 + 0.006 * k)) - 0.011, std::abs(along) - 0.012));
		}
	return d;
}
// A thin cream line inside the gold bands of the front panel, and one above the hem.
inline double creamLineSdf(Vec3 q, const ExplorerPose &p, const ExplorerBody &b) {
	const double surface = std::abs(outerRobeSdf(q, p, b) + 0.001) - 0.0065;
	double band = std::abs(q.y - b.hemY - 0.075) - 0.006;
	if (q.y < b.pelvis.y - 0.02 && q.z > 0.0) band = std::min(band, std::abs(std::abs(q.x) - panelHalf(b, q.y) + 0.040) - 0.0045);
	return std::max(surface, band);
}
// The gold collar: a band round the neck's base with the square-key steps of the statue's collar.
inline double collarSdf(Vec3 q, const ExplorerBody &b) {
	const Vec3 v = q - (b.neck + Vec3{0.0, -0.045, 0.010});
	const double a = std::atan2(v.x, v.z);
	const double step = std::fmod(a / (2.0 * kPi) * 24.0 + 100.0, 1.0) < 0.5 ? 0.012 : 0.0;
	const double r = std::sqrt(v.x * v.x + v.z * v.z / 0.8);
	return std::max(std::abs(r - 0.120 - step * 0.5) - 0.022 - step * 0.5, std::abs(v.y + 0.02 * std::cos(a)) - 0.016);
}
// The woven shoulder pads: rust-red basket weave over each shoulder and upper arm, ringed in gold at the bottom.
inline double padSdf(Vec3 q, const ExplorerBody &b) {
	double d = 1e9;
	for (int s = 0; s < 2; ++s) {
		const Vec3 axis = normalize(b.elbow[s] - b.shoulder[s]);
		const Mat4 f = alignY(b.shoulder[s] + axis * 0.045 + Vec3{0.0, 0.020, 0.0}, axis);
		const Vec3 l = transformPoint(invertAffine(f), q);
		double e = sdf::ellipsoid(l, identity(), {0.098, 0.135, 0.098});
		e = std::max(e, l.y - 0.095);
		const double th = std::atan2(l.x, l.z);
		e += 0.0035 * std::sin(th * 9.0 + l.y * 70.0) * std::sin(th * 9.0 - l.y * 70.0);   // (the weave)
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

// ---- the sceptre ---------------------------------------------------------------------------------------------------------------------------------------------------
// The Nomai's writing sceptre: a dark wooden shaft and a boxy ivory head with gold rods standing out of it (they type on it and press its end to a surface to write); a
// small crystal in the head and two fine channels in it glow ghostly blue.
inline Mat4 staffFrame(const ExplorerPose &p, const ExplorerBody &b) {
	const int s = p.staff > 0 ? 0 : 1;
	const Vec3 grip = b.wrist[s] + b.handDir[s] * 0.05;
	const Vec3 up = (p.staffTip.x != 0.0 || p.staffTip.y != 0.0 || p.staffTip.z != 0.0) ? normalize(p.staffTip - grip) : normalize(Vec3{0.0, 1.0, 0.05});
	return alignY(grip, up);
}
// The sceptre at `m` (its grip at the origin, the shaft along +y), its light at `glowAmount` (0 .. 1).
inline std::string sceptreText(const Mat4 &m, double glowAmount) {
	std::string out;
	std::vector<std::pair<double, double>> shaft;
	for (int i = 0; i <= 30; ++i) shaft.push_back({i == 0 ? 0.0 : 0.015 + 0.0015 * std::sin(i * 1.7), -1.20 + 1.75 * i / 30.0});
	shaft.push_back({0.0, 0.55});
	out += attr(diffuseMaterial({0.24, 0.15, 0.09}), meshText(lathe(shaft, 12), m));
	std::string gold;
	for (int k = 0; k < 4; ++k) gold += meshText(torus(0.0175, 0.004, 14, 6), mul(m, translate(0.0, 0.25 + 0.08 * k, 0.0)));
	// the head: an ivory block, turned a little, with rods standing out of its side like a comb
	const Mat4 head = mul(m, translate(0.0, 0.66, 0.0), rotate({0.0, 0.0, 1.0}, 12.0));
	out += attr(coated({0.84, 0.78, 0.64}, 0.4), meshText(roundedBox(0.075, 0.22, 0.050, 0.012, 2), head));
	gold += meshText(roundedBox(0.090, 0.020, 0.060, 0.006, 1), mul(head, translate(0.0, -0.115, 0.0))) + meshText(roundedBox(0.090, 0.020, 0.060, 0.006, 1), mul(head, translate(0.0, 0.115, 0.0)));
	for (int k = 0; k < 5; ++k) {
		const double y = -0.08 + 0.04 * k;
		const double len = 0.10 + 0.05 * std::sin(k * 1.9 + 0.5);
		gold += rod(transformPoint(head, {0.035, y, 0.0}), transformPoint(head, {0.035 + len, y + 0.012, 0.0}), 0.0055, 6);
	}
	out += attr(metalMaterial({0.86, 0.66, 0.30}, 0.32), gold);
	const double glow = 0.15 + 0.85 * clamp01(glowAmount);
	out += emissiveSphere(transformPoint(head, {0.0, 0.0, 0.028}), 0.016, Vec3{0.55, 0.80, 1.0} * (4.0 * glow));
	const Vec3 e = Vec3{0.45, 0.75, 1.0} * (1.4 * glow);
	out += attr("Material \"diffuse\" \"rgb reflectance\" [ 0 0 0 ]", meshText(box(0.004, 0.18, 0.004), mul(head, translate(0.018, 0.0, 0.026))) + meshText(box(0.004, 0.18, 0.004), mul(head, translate(-0.018, 0.0, 0.026))), &e);
	return out;
}
inline std::string staffText(const ExplorerPose &p, const ExplorerBody &b, const Mat4 &world) {
	if (p.staff == 0) return std::string();
	return sceptreText(mul(world, staffFrame(p, b)), p.staffGlow);
}

// ---- materials ----------------------------------------------------------------------------------------------------------------------------------------------------
inline std::string furMaterial() { return "Material \"diffuse\" \"rgb reflectance\" [ 0.80 0.68 0.52 ]"; }
inline std::string beardMaterial() { return "Material \"diffuse\" \"rgb reflectance\" [ 0.92 0.90 0.84 ]"; }
inline std::string ivoryMaterial() { return "Material \"coateddiffuse\" \"rgb reflectance\" [ 0.86 0.80 0.66 ] \"float roughness\" [ 0.35 ]"; }
inline std::string goldMaterial() { return metalMaterial({0.86, 0.66, 0.30}, 0.30); }
// The robe's cloth: deep green with fine cream lines in branching curves (the suits' pattern), laid round the robe (u round the body, twice; v up it).
inline std::string robeTexture() {
	return film_sky::picture(kFilmKey, "robe_teal", 1024, 1024, [](std::vector<float> &rgb) {
		const int N = 1024;
		struct Arc {
			double u, v, r, a0, a1;
		};
		std::vector<Arc> arcs;
		for (int k = 0; k < 46; ++k) {
			const double a0 = 2.0 * kPi * hash01(k, 901), span = 0.8 + 2.2 * hash01(k, 902);
			arcs.push_back({hash01(k, 903), hash01(k, 904), 0.06 + 0.22 * hash01(k, 905), a0, a0 + span});
		}
		for (int j = 0; j < N; ++j)
			for (int i = 0; i < N; ++i) {
				const double u = (i + 0.5) / N, v = 1.0 - (j + 0.5) / N;
				double line = 1e9;
				for (const Arc &a : arcs)
					for (int w = -1; w <= 1; ++w) {
						const double du = u - a.u + w, dv = v - a.v;
						double ang = std::atan2(dv, du);
						while (ang < a.a0) ang += 2.0 * kPi;
						if (ang > a.a1) continue;
						line = std::min(line, std::abs(std::sqrt(du * du + dv * dv) - a.r));
					}
				const double n = nature::fbm2(u * 90.0, v * 90.0, 907);
				Vec3 c = Vec3{0.20, 0.38, 0.37} * (0.90 + 0.18 * n);
				c = lerp(c, Vec3{0.80, 0.76, 0.56}, 0.85 * (1.0 - smoothstep(0.0012, 0.0026, line)));
				const std::size_t k = (static_cast<std::size_t>(j) * N + i) * 3;
				rgb[k] = static_cast<float>(c.x);
				rgb[k + 1] = static_cast<float>(c.y);
				rgb[k + 2] = static_cast<float>(c.z);
			}
	});
}
inline std::string robeMaterial() { return "Texture \"ns_robe\" \"spectrum\" \"imagemap\" \"string filename\" [ \"" + robeTexture() + "\" ]\n  Material \"diffuse\" \"texture reflectance\" \"ns_robe\""; }
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
inline const Mesh &maskIvoryMesh() {
	static const Mesh m = sdf::surfaceNets([](Vec3 q) { return maskIvorySdf(q); }, {-0.20, -0.12, 0.02}, {0.20, 0.34, 0.30}, 0.0025);
	return m;
}
inline const Mesh &maskGoldMesh() {
	static const Mesh m = sdf::surfaceNets([](Vec3 q) { return maskGoldSdf(q); }, {-0.20, -0.10, -0.10}, {0.20, 0.42, 0.30}, 0.0025);
	return m;
}
inline const Mesh &maskDarkMesh() {
	static const Mesh m = sdf::surfaceNets([](Vec3 q) { return maskDarkSdf(q); }, {-0.06, -0.02, 0.10}, {0.06, 0.10, 0.25}, 0.0015);
	return m;
}
inline const Mesh &maskTealMesh() {
	static const Mesh m = sdf::surfaceNets([](Vec3 q) { return maskTealSdf(q); }, {-0.20, 0.04, 0.05}, {0.20, 0.16, 0.20}, 0.0015);
	return m;
}
// The mask on its own (laid on the ground beside the campfire): at `frame` (mask space -> world). Its four meshes never change: made once as PLY parts (film_cache::part).
inline std::string maskText(const Mat4 &frame, double = 0.0) {
	return film_cache::part(kFilmKey, "mask_ivory", ivoryMaterial(), frame, [] { return maskIvoryMesh(); }) +
		   film_cache::part(kFilmKey, "mask_gold", goldMaterial(), frame, [] { return maskGoldMesh(); }) +
		   film_cache::part(kFilmKey, "mask_dark", diffuseMaterial({0.10, 0.07, 0.05}), frame, [] { return maskDarkMesh(); }) +
		   film_cache::part(kFilmKey, "mask_teal", diffuseMaterial({0.28, 0.55, 0.55}), frame, [] { return maskTealMesh(); });
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
	const Mesh skin = sdf::surfaceNets([&](Vec3 q) {
		double d = headSkinSdf(q);
		for (int k = 0; k < 3; ++k) d = std::min(d, eyelidSdf(q, k, lidClose[k]));
		return std::min(d, earSdf(q, p.ears));
	}, {-0.20, -0.22, -0.22}, {0.20, 0.26, 0.27}, fine);
	out += attr(furMaterial(), meshText(skin, head));
	const double beardLength = p.maskShown ? smoothstep(0.2, 0.9, p.maskOff) : 1.0;
	out += attr(beardMaterial(), meshText(sdf::surfaceNets([&](Vec3 q) { return beardSdf(q, beardLength); }, {-0.10, -0.32, 0.02}, {0.10, 0.0, 0.26}, fine), head));
	out += film_cache::part(kFilmKey, "horns" + grain, "Material \"coateddiffuse\" \"rgb reflectance\" [ 0.55 0.42 0.30 ] \"float roughness\" [ 0.5 ]", head,
							[&] { return sdf::surfaceNets([](Vec3 q) { return hornSdf(q); }, {-0.22, 0.0, -0.26}, {0.22, 0.38, 0.08}, fine); });
	std::string eyes, pupils;
	for (int k = 0; k < 3; ++k) {
		const EyePlace &e = eyePlace(k);
		const Mat4 look = mul(translate(e.c), rotate({0.0, 1.0, 0.0}, e.outward * 0.5 + p.gazeYaw), rotate({1.0, 0.0, 0.0}, p.gazePitch));
		eyes += meshText(uvSphere(e.r, 20, 12), mul(head, look));
		pupils += meshText(roundedBox(e.r * 1.05, e.r * 0.34, e.r * 0.25, e.r * 0.12, 2), mul(head, look, translate(0.0, 0.0, e.r * 0.90)));
	}
	out += attr("Material \"coateddiffuse\" \"rgb reflectance\" [ 0.70 0.48 0.12 ] \"float roughness\" [ 0.02 ]", eyes);
	out += attr("Material \"coateddiffuse\" \"rgb reflectance\" [ 0.008 0.008 0.008 ] \"float roughness\" [ 0.02 ]", pupils);
	if (p.maskShown) out += maskText(mul(world, maskFrame(p, b)));
	// the suit
	Mesh robe = sdf::surfaceNets([&](Vec3 q) { return outerRobeSdf(q, p, b); }, lo, hi, cell);
	wrapUV(robe);
	out += attr(robeMaterial(), meshText(robe, world));
	out += attr(diffuseMaterial({0.15, 0.28, 0.28}), meshText(sdf::surfaceNets([&](Vec3 q) { return trousersSdf(q, p, b); }, {-0.4, -0.05, -0.4}, {0.4, b.pelvis.y + 0.05, 0.5}, cell), world));
	out += attr(diffuseMaterial({0.50, 0.23, 0.11}), meshText(sdf::surfaceNets([&](Vec3 q) { return hipWrapSdf(q, b); }, {-0.4, 0.2, -0.4}, {0.4, b.pelvis.y + 0.15, 0.5}, cell * 0.6), world));
	out += attr(goldMaterial(), meshText(sdf::surfaceNets([&](Vec3 q) { return std::min(trimSdf(q, p, b), std::min(collarSdf(q, b), padRingSdf(q, b))); }, lo, hi, cell * 0.5), world));
	out += attr(diffuseMaterial({0.86, 0.82, 0.62}), meshText(sdf::surfaceNets([&](Vec3 q) { return creamLineSdf(q, p, b); }, {-0.55, -0.02, -0.55}, {0.55, b.pelvis.y + 0.02, 0.65}, cell * 0.4), world));
	out += attr(diffuseMaterial({0.50, 0.21, 0.11}), meshText(sdf::surfaceNets([&](Vec3 q) { return padSdf(q, b); }, {-0.45, b.chest.y - 0.25, -0.30}, {0.45, b.neck.y + 0.15, 0.30}, cell * 0.5), world));
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
