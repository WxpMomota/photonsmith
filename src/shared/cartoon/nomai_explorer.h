#pragma once
// nomai_explorer.h -- the explorer of "The Last Nomai Signal". The sealed suit
// follows Ian Jacobson's published Nomai suit and MaskB sheets for Outer Wilds:
// stepped ivory/gold three-diamond mask, segmented crest, blue-green navigation
// cloth, ochre binding, quilted brown shoulders and a transverse ivory tank.
// https://www.ianjacobson.com/Outer-Wilds
// The living face is an interpretation informed by the statue sheet. The
// writing staff is reconstructed in the same device language; the suit sheet
// does not specify its details. All geometry and textures are authored here.
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
	Vec3 handL{0.20, 0.74, 0.06}, handR{-0.20, 0.74, 0.06};   // where the hands are (body space)
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
	double headScale = 1.0;                                  // family members: children have larger heads relative to their bodies
	Vec3 robeTint{1.0, 1.0, 1.0}, furTint{1.0, 1.0, 1.0};
	double beardScale = 1.0, hornGrowth = 1.0;
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
	p.handL = {0.21, 0.73, 0.06 - 0.10 * sw * stride};
	p.handR = {-0.25, 0.95, 0.16 + 0.06 * sw * stride};
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
	p.handR = {-0.25, 0.92, 0.14};
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
	const double drop = 0.45 * p.sit + 0.18 * p.crouch;
	b.pelvis = {0.0, 0.80 - drop, 0.0};
	const double lean = (p.lean + 10.0 * p.crouch + 6.0 * p.sit) * kPi / 180.0;
	const Vec3 up{0.0, std::cos(lean), std::sin(lean)};
	b.chest = b.pelvis + up * (0.30 + breath);
	b.neck = b.chest + up * 0.10;
	b.headPivot = b.neck + Vec3{0.0, 0.135, 0.025} + Vec3{0.0, 0.0, 0.03 * std::sin(lean)};
	for (int s = 0; s < 2; ++s) {
		const double side = s == 0 ? 1.0 : -1.0;   // 0: left (+x), 1: right
		b.shoulder[s] = b.chest + Vec3{0.190 * side, 0.075, -0.01};
		const Vec3 target = s == 0 ? p.handL : p.handR;
		const Limb arm = limbIk(b.shoulder[s], target, 0.29, 0.27, normalize(Vec3{0.6 * side, -0.2, -1.0}));
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
		const Limb leg = limbIk(b.hip[s], hockTarget, 0.35, 0.31, normalize(Vec3{0.1 * side, 0.0, 1.0}));
		b.knee[s] = leg.elbow;
		b.hock[s] = leg.hand;
	}
	const Mat4 turn = mul(rotate({0.0, 1.0, 0.0}, p.headYaw), rotate({1.0, 0.0, 0.0}, p.headPitch), rotate({0.0, 0.0, 1.0}, p.headTilt));
	b.head = mul(translate(b.headPivot), turn, scale(p.headScale, p.headScale, p.headScale));
	b.headInv = invertAffine(b.head);
	b.hemY = 0.28 - 0.22 * p.sit - 0.08 * p.crouch;   // (sitting: the hem falls to the ground over the folded legs - lifted above the pelvis, the skirt was a wide dome)
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
    for(int side:{-1,1}) {
        for(int lock=0;lock<3;++lock) {
            const Vec3 root{side*0.062,0.060-lock*0.025,0.070};
            const Vec3 tip{side*(0.115-lock*0.006),0.065-lock*0.038,0.015};
            d=smin(d,roundCone(q,root,tip,0.028,0.003),0.009);
        }
        d=smin(d,roundCone(q,{side*0.025,0.126,0.105},{side*0.082,0.135,0.067},0.015,0.006),0.008);
        d=std::max(d,-ellv(q,{side*0.021,0.015,0.222},{0.008,0.006,0.012}));
    }
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
	const double L = 0.25 * std::max(0.0,std::min(1.4,length));
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

// The angular mask geometry is defined below the material helpers.

// ---- the body (body space) ------------------------------------------------------------------------------------------------------------------------------------------
inline Vec3 robeSway(const ExplorerPose &p, const ExplorerBody &b, double y) {
	const double below = clamp01((b.pelvis.y - y) / std::max(0.2, b.pelvis.y - b.hemY));
	const double flutter = std::sin(p.time * 5.3 + y * 9.0) * 0.4 + std::sin(p.time * 8.1 + y * 17.0) * 0.2;
	return Vec3{p.wind.x, 0.0, p.wind.z} * (below * below * (1.0 + 0.4 * flutter)) + Vec3{0.0, 0.0, 0.035 * p.stride * std::sin(p.walk * kPi * 2.0)} * below;
}
inline double skirtSdf(Vec3 q, const ExplorerPose &p, const ExplorerBody &b, double inset) {
	using namespace sdf;
	const Vec3 s = q - robeSway(p, b, q.y);
	const Vec3 hemCentre{0.0, b.hemY, 0.035 + 0.17 * p.sit};   // (sitting: forward, over the knees)
	double d = roundCone(s, b.pelvis + Vec3{0.0, 0.03, 0.0}, hemCentre, 0.185 - inset, 0.32 + 0.03 * p.sit - inset);
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
// The gold bindings (borderMaterial): bands down both edges of the front panel and the slits' outer edges, the hem, the back slit's edges, a stepped
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
	// The stepped rectangular neckline on the suit sheet.
	if (q.y > b.chest.y - 0.16 && q.y < b.neck.y + 0.02) {
        const double depth = q.z > 0.0 ? 0.150 : 0.080;
        const double x = std::abs(q.x), y = b.neck.y - q.y;
        if (y > 0.0 && y < depth) band = std::min(band, std::abs(x - 0.110) - 0.012);
        if (x < 0.110) band = std::min(band, std::abs(y - depth) - 0.012);
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
// The suit collar is a square gold binding rather than a beaded necklace.
inline double collarSdf(Vec3 q, const ExplorerBody &b) {
    const Vec3 v = q - (b.neck + Vec3{0.0, -0.065, 0.01});
    const double edge = std::max(std::abs(v.x) - 0.116, std::abs(v.z) - 0.115);
    return std::max(std::abs(edge) - 0.009, std::abs(v.y) - 0.018);
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
		const double around = std::atan2(l.x, l.z) * 0.095;
		e += 0.0035 * (std::pow(std::abs(std::cos((l.y + around) * 55.0)), 10.0) + std::pow(std::abs(std::cos((l.y - around) * 55.0)), 10.0));   // (the stitched channels round the arm: the stitches pull the leather in)
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
constexpr double kTankHalf = 0.245, kTankR = 0.105;
inline double tankSdf(Vec3 q, const ExplorerBody &b) {
	const Vec3 v = q - tankCentre(b);
	const double r = std::sqrt(v.y * v.y + v.z * v.z);
	return std::max(r - kTankR, std::abs(v.x) - kTankHalf);
}
inline double tankGoldSdf(Vec3 q, const ExplorerBody &b) {
	const Vec3 v = q - tankCentre(b);
	const double r = std::sqrt(v.y * v.y + v.z * v.z);
	double d = 1e9;
	for (const double x : {-0.245, -0.170, 0.170, 0.245}) d = std::min(d, std::max(r - kTankR - 0.012, std::abs(v.x - x) - (std::abs(x) > 0.20 ? 0.022 : 0.014)));
	// the rims of the caps
	d = std::min(d, std::max(std::abs(r - 0.110) - 0.012, std::abs(std::abs(v.x) - 0.278) - 0.016));
	return d;
}
inline double tankCapSdf(Vec3 q, const ExplorerBody &b) {
	const Vec3 v = q - tankCentre(b);
	const double r = std::sqrt(v.y * v.y + v.z * v.z);
	return std::max(r - 0.105, std::abs(std::abs(v.x) - 0.278) - 0.012);
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
	return b.ball[s] + fwd * (0.085 - 0.012 * std::abs(t)) + side * (0.050 * t) + Vec3{0.0, -0.010, 0.0};
}
inline double footSdf(Vec3 q, const ExplorerBody &b) {
	using namespace sdf;
	double d = 1e9;
	for (int s = 0; s < 2; ++s) {
		d = std::min(d, roundCone(q, b.hock[s], b.ball[s], 0.055, 0.073));
		for (int t = -1; t <= 1; ++t) d = smin(d, roundCone(q, b.ball[s] + Vec3{0.0, -0.005, 0.0}, toeTip(b, s, t), 0.046, 0.034), 0.025);
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
// A Nomai writing staff: dark shaft, angular cradle and circular slate. Its
// luminous spiral preserves the film's staffGlow lighting and staffTip targeting.
inline Mat4 staffFrame(const ExplorerPose &p, const ExplorerBody &b) {
	const int s = p.staff > 0 ? 0 : 1;
	const Vec3 grip = b.wrist[s] + b.handDir[s] * 0.05;
	const Vec3 up = (p.staffTip.x != 0.0 || p.staffTip.y != 0.0 || p.staffTip.z != 0.0) ? normalize(p.staffTip - grip) : normalize(Vec3{0.0, 1.0, 0.05});
	return alignY(grip, up);
}
// The sceptre at `m` (its grip at the origin, the shaft along +y), its light at `glowAmount` (0 .. 1).
// A circular Nomai writing surface in an angular gold cradle. The film's glow
// control now lights the writing rather than an invented floating crystal.
inline std::string sceptreText(const Mat4 &m, double glowAmount) {
    std::string out;
    out += attr(coated({0.14, 0.085, 0.042}, 0.50), meshText(lathe({{0,-0.90},{0.016,-0.90},{0.014,0.54},{0,0.54}}, 12), m));
    std::string fittings;
    for (double y : {-0.90,-0.08,0.08,0.50})
        fittings += meshText(torus(0.017,0.004,16,6), mul(m,translate(0,y,0)));
    for (int side : {-1,1}) {
        const Vec3 a{0,0.50,0}, b{side*0.115,0.565,0}, c{side*0.115,0.73,0};
        fittings += meshText(box(0.024,length(b-a),0.028), mul(m,alignY((a+b)*0.5,b-a,{0,0,1})));
        fittings += meshText(box(0.024,length(c-b),0.028), mul(m,alignY((b+c)*0.5,c-b,{0,0,1})));
    }
    const Mat4 disk = mul(m, translate(0,0.70,0), rotate({1,0,0},90));
    fittings += meshText(torus(0.099,0.007,48,6), disk);
    fittings += meshText(torus(0.081,0.003,48,6), mul(disk,translate(0,-0.008,0)));
    out += attr(metalMaterial({0.72, 0.47, 0.15}, 0.38), fittings);
    out += attr(diffuseMaterial({0.18,0.32,0.30}), meshText(lathe({{0,-0.005},{0.095,-0.005},{0.095,0.005},{0,0.005}},48),disk));
    // Thin two-sided decals keep the slate legible from either side, with
    // hundreds of light triangles rather than thousands of capsule faces.
    Mesh script;
    const auto stroke = [&script](Vec3 a, Vec3 b, double width, double face) {
        a.z=face*0.010; b.z=a.z;
        const Vec3 d=normalize(b-a), n{-d.y*width,d.x*width,0};
        const Vec3 q[4]={a-n,b-n,b+n,a+n};
        const int base=static_cast<int>(script.verts.size());
        for(int k=0;k<4;++k) { script.verts.push_back(q[k]); script.normals.push_back({0,0,face}); }
        if(face>0) { script.tris.push_back({base,base+1,base+2}); script.tris.push_back({base,base+2,base+3}); }
        else { script.tris.push_back({base,base+2,base+1}); script.tris.push_back({base,base+3,base+2}); }
    };
    const auto point=[](double u) {
        const double a=u*3.8*kPi, r=0.012+0.057*u;
        return Vec3{r*std::cos(a),0.70+r*std::sin(a),0};
    };
    const int steps=90;
    for(double face : {-1.0,1.0}) for(int i=0;i<steps;++i) {
        const Vec3 a=point(i/static_cast<double>(steps)), b=point((i+1)/static_cast<double>(steps));
        stroke(a,b,0.0008,face);
        if(i%4==0) stroke(a,a+normalize(Vec3{a.x,a.y-0.70,0})*0.006,0.0006,face);
    }
    const Vec3 e=Vec3{0.48,0.72,0.84}*(0.4+2.6*clamp01(glowAmount));
    out += attr(diffuseMaterial({0.36,0.57,0.60}),meshText(script,m),&e);
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
inline std::string tintKey(Vec3 tint) {
    return std::to_string(std::lround(tint.x*1000))+"_"+std::to_string(std::lround(tint.y*1000))+"_"+std::to_string(std::lround(tint.z*1000));
}
inline std::string headFurMaterial(Vec3 tint={1,1,1}, double reveal=1.0) {
	const std::string pic = film_sky::picture(kFilmKey, "fur_family_v2_" + tintKey(tint) + "_" + std::to_string(std::lround(reveal*1000)), 1024, 512, [tint,reveal](std::vector<float> &rgb) {
		for (int j = 0; j < 512; ++j)
			for (int i = 0; i < 1024; ++i) {
				Vec3 c = headFurColour((i + 0.5) / 1024.0, 1.0 - (j + 0.5) / 512.0);
                c=lerp(Vec3{0.025,0.040,0.038},Vec3{c.x*tint.x,c.y*tint.y,c.z*tint.z},reveal);
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
inline std::string goldMaterial() { return metalMaterial({0.72, 0.47, 0.15}, 0.38); }   // aged ochre-gold fittings
// The robe's blue-green twill carries broad ochre navigation arcs and connected nodes,
// following the suit sheet. UVs wrap twice round the body and repeat up its height.
// A twill's fine diagonal ribs: their height at (u, v) of the robe's picture (0 .. 1), and the stitched star chart's mask (0 .. 1) and brightness.
constexpr int kRobePicture = 1024;
constexpr double kTwillPeriod = 1.0 / 170.0;   // (u is about 0.6 m round the body: a rib every 3.5 mm, six pixels of the picture)
inline double twillHeight(double u, double v) {
	const double s = (u + v) / kTwillPeriod;
	return std::pow(0.5 + 0.5 * std::cos(2.0 * kPi * s), 1.5);
}
// How much of the embroidery covers (u, v): 0 bare cloth .. 1 thread.
inline double starChartThread(double u, double v) {
    double cover = 0.0;
    const Vec3 nodes[] = {{0.12,0.20,0}, {0.43,0.42,0}, {0.77,0.18,0}, {0.88,0.72,0}, {0.32,0.85,0}};
    for (int wu = -1; wu <= 1; ++wu) for (int wv = -1; wv <= 1; ++wv) {
        const Vec3 p{u - wu, v - wv, 0.0};
        for (int i = 0; i < 5; ++i) {
            const Vec3 a = nodes[i], b = nodes[(i + 1) % 5], d = b - a;
            const double t = clamp01(dot(p - a, d) / dot(d, d));
            cover = std::max(cover, 1.0 - smoothstep(0.0010, 0.0020, length(p - a - d * t)));
            cover = std::max(cover, 1.0 - smoothstep(0.0014, 0.0024, std::abs(length(p - a) - 0.012)));
        }
        for (Vec3 c : {Vec3{0.18,0.52,0}, Vec3{0.86,0.12,0}})
            cover = std::max(cover, 1.0 - smoothstep(0.0010, 0.0020, std::abs(length(p - c) - 0.36)));
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
inline std::string robeTexture(Vec3 tint={1,1,1}) {
	return film_sky::picture(kFilmKey, "robe_family_v3_" + tintKey(tint), kRobePicture, kRobePicture, [tint](std::vector<float> &rgb) {
		const int N = kRobePicture;
		const std::vector<float> &thread = robeThreadGrid();
		film_sky::eachRow(N, [&rgb, &thread, N,tint](int j) {
			for (int i = 0; i < N; ++i) {
				const double u = (i + 0.5) / N, v = 1.0 - (j + 0.5) / N;
				const double n = nature::fbm2(u * 60.0, v * 60.0, 907), slub = nature::fbm2(u * 9.0, v * 170.0, 908);
				Vec3 c = Vec3{0.065, 0.155, 0.160} * (0.86 + 0.16 * n + 0.08 * (slub - 0.5)) * (0.90 + 0.12 * twillHeight(u, v));
				c = {c.x*tint.x,c.y*tint.y,c.z*tint.z};
				c = lerp(c, Vec3{0.66, 0.55, 0.25}, 0.92 * thread[static_cast<std::size_t>(j) * N + i]);
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
	return film_sky::picture(kFilmKey, "robe_navigation_v2_n", kRobePicture, kRobePicture, [](std::vector<float> &rgb) {
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
inline std::string robeMaterial(Vec3 tint={1,1,1}) {
	return "Texture \"ns_robe\" \"spectrum\" \"imagemap\" \"string filename\" [ \"" + robeTexture(tint) + "\" ]\n  Material \"diffuse\" \"texture reflectance\" \"ns_robe\" \"string normalmap\" [ \"" +
		   robeNormalTexture() + "\" ]";
}
// Broad ochre bindings, as on the suit turnaround.
inline std::string borderMaterial() { return "Material \"coateddiffuse\" \"rgb reflectance\" [ 0.52 0.34 0.075 ] \"float roughness\" [ 0.48 ]"; }
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
constexpr double kSuitMaskScale = 1.15;
inline Vec3 maskGripPoint() { return {0.247,0.12,0.19}; }
inline Mat4 maskFrame(const ExplorerPose &p, const ExplorerBody &b) {
	if (p.maskOff <= 0.0) return b.head;
	const double u = clamp01(p.maskOff);
	const Mat4 lifted = mul(b.head, translate(0.0, 0.0, 0.08 * smoothstep(0.0, 0.35, u)));
	if (u <= 0.35) return lifted;
	const Vec3 hand = b.wrist[0] + b.handDir[0] * 0.06;
	const Mat4 held = mul(translate(hand), rotate({1.0, 0.0, 0.0}, -60.0), rotate({0.0, 1.0, 0.0}, 25.0), translate(maskGripPoint()*-kSuitMaskScale));
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
// Authored from Ian Jacobson's Outer Wilds Nomai suit and MaskB sheets:
// https://www.ianjacobson.com/Outer-Wilds
// All pieces are closed, flat-shaded prisms. No game assets are embedded.
inline void maskQuad(Mesh &m, Vec3 a, Vec3 b, Vec3 c, Vec3 d) {
    const int n = static_cast<int>(m.verts.size());
    const Vec3 normal = normalize(cross(b - a, c - a));
    for (Vec3 v : {a, b, c, d}) { m.verts.push_back(v); m.normals.push_back(normal); }
    m.tris.push_back({n, n + 1, n + 2}); m.tris.push_back({n, n + 2, n + 3});
}
inline void maskPrism(Mesh &m, const std::array<Vec3, 4> &p, double z, double depth) {
    Vec3 f[4], b[4];
    for (int i = 0; i < 4; ++i) { f[i] = {p[i].x, p[i].y, z}; b[i] = {p[i].x, p[i].y, z - depth}; }
    maskQuad(m, f[0], f[1], f[2], f[3]);
    maskQuad(m, b[3], b[2], b[1], b[0]);
    for (int i = 0; i < 4; ++i) { const int j = (i + 1) % 4; maskQuad(m, b[i], b[j], f[j], f[i]); }
}
inline void maskRail(Mesh &m, Vec3 a, Vec3 b, double width, double z, double depth) {
    const Vec3 d = normalize(b - a), n{-d.y * width * 0.5, d.x * width * 0.5, 0.0};
    maskPrism(m, {a - n, b - n, b + n, a + n}, z, depth);
}
inline Vec3 maskEyeAt(int k) { return k == 2 ? Vec3{0.0, 0.166, 0.0} : Vec3{k == 0 ? 0.065 : -0.065, 0.101, 0.0}; }
// Extrude a continuous annular strip; adjacent facets share no internal caps.
inline void maskBand(Mesh &m, const std::vector<Vec3> &outer, const std::vector<Vec3> &inner, bool closed, double z, double depth) {
    const auto at = [](Vec3 p, double zz) { return Vec3{p.x,p.y,zz}; };
    const int n = static_cast<int>(outer.size()), count = closed ? n : n - 1;
    for (int i=0;i<count;++i) {
        const int j=(i+1)%n;
        maskQuad(m,at(outer[i],z),at(outer[j],z),at(inner[j],z),at(inner[i],z));
        maskQuad(m,at(inner[i],z-depth),at(inner[j],z-depth),at(outer[j],z-depth),at(outer[i],z-depth));
        maskQuad(m,at(outer[i],z-depth),at(outer[j],z-depth),at(outer[j],z),at(outer[i],z));
        maskQuad(m,at(inner[j],z-depth),at(inner[i],z-depth),at(inner[i],z),at(inner[j],z));
    }
    if (!closed) {
        maskQuad(m,at(inner[0],z-depth),at(outer[0],z-depth),at(outer[0],z),at(inner[0],z));
        maskQuad(m,at(outer[n-1],z-depth),at(inner[n-1],z-depth),at(inner[n-1],z),at(outer[n-1],z));
    }
}
inline void maskDiamond(Mesh &m, Vec3 c, double outer, double inner, double z, double depth) {
    std::vector<Vec3> a,b;
    for (Vec3 v : {Vec3{1,0,0},Vec3{0,1,0},Vec3{-1,0,0},Vec3{0,-1,0}}) {
        a.push_back(c+v*outer); b.push_back(c+v*inner);
    }
    maskBand(m,a,b,true,z,depth);
}
// Three interrupted arcs, with broad flat facets and gold end caps.
inline void maskCrest(Mesh &m, double outer, double inner, double z, double depth) {
    for (int sector=0;sector<3;++sector) {
        std::vector<Vec3> a,b;
        for (int i=0;i<=9;++i) {
            const double angle=(45.0+120.0*sector+10.0*i)*kPi/180.0;
            const Vec3 v{std::cos(angle),std::sin(angle),0}, c{0,0.12,0};
            a.push_back(c+v*outer); b.push_back(c+v*inner);
        }
        maskBand(m,a,b,false,z,depth);
    }
}
inline Mesh maskIvoryMesh() {
    Mesh m;
    for (int k = 0; k < 3; ++k) {
        maskDiamond(m, maskEyeAt(k), 0.061, 0.046, 0.224, 0.012);
        maskDiamond(m, maskEyeAt(k), 0.039, 0.029, 0.231, 0.010);
    }
    maskCrest(m, 0.259, 0.237, 0.198, 0.016);
    for (int side : {-1, 1}) {
        const auto v = [side](double x, double y) { return Vec3{side * x, y, 0.0}; };
        maskRail(m, v(0.018, 0.222), v(0.036, 0.302), 0.016, 0.224, 0.012);
        maskRail(m, v(0.036, 0.302), v(0.014, 0.352), 0.016, 0.224, 0.012);
        maskRail(m, v(0.117, 0.100), v(0.184, 0.036), 0.021, 0.218, 0.014);
        maskRail(m, v(0.184, 0.036), v(0.213, 0.044), 0.021, 0.218, 0.014);
        maskRail(m, v(0.213, 0.044), v(0.217, 0.111), 0.021, 0.218, 0.014);
        maskRail(m, v(0.042, 0.046), v(0.097, -0.034), 0.018, 0.214, 0.013);
        maskRail(m, v(0.097, -0.034), v(0.153, -0.060), 0.018, 0.214, 0.013);
        maskRail(m, v(0.014, 0.073), v(0.014, -0.057), 0.012, 0.268, 0.013);
    }
    return m;
}
inline Mesh maskBronzeMesh() {
    Mesh m;
    for (int k = 0; k < 3; ++k) {
        maskDiamond(m, maskEyeAt(k), 0.0645, 0.024, 0.215, 0.024);
        maskDiamond(m, maskEyeAt(k), 0.044, 0.041, 0.232, 0.008);
        maskDiamond(m, maskEyeAt(k), 0.028, 0.024, 0.235, 0.008);
    }
    maskCrest(m, 0.271, 0.224, 0.185, 0.028);
    for (int side : {-1, 1}) {
        const auto v = [side](double x, double y) { return Vec3{side * x, y, 0.0}; };
        maskRail(m, v(0.018, 0.220), v(0.036, 0.302), 0.027, 0.209, 0.020);
        maskRail(m, v(0.036, 0.302), v(0.014, 0.364), 0.027, 0.209, 0.020);
        maskRail(m, v(0.117, 0.100), v(0.184, 0.036), 0.036, 0.203, 0.020);
        maskRail(m, v(0.184, 0.036), v(0.213, 0.044), 0.036, 0.203, 0.020);
        maskRail(m, v(0.213, 0.044), v(0.217, 0.111), 0.036, 0.203, 0.020);
        maskRail(m, v(0.042, 0.046), v(0.097, -0.034), 0.032, 0.199, 0.020);
        maskRail(m, v(0.097, -0.034), v(0.153, -0.060), 0.032, 0.199, 0.020);
    }
    maskRail(m, {0.0, 0.090, 0.0}, {0.0, -0.072, 0.0}, 0.044, 0.254, 0.035);
    return m;
}
inline Mesh maskBackingMesh() {
    Mesh m;
    maskPrism(m, {Vec3{-0.060,0.045,0}, Vec3{-0.034,-0.110,0}, Vec3{0.034,-0.110,0}, Vec3{0.060,0.045,0}}, 0.244, 0.065);
    return m;
}
inline Mesh maskLensMesh() {
    Mesh m;
    for (int k = 0; k < 3; ++k) {
        const Vec3 c = maskEyeAt(k);
        const double r = 0.0235;
        maskPrism(m, {c + Vec3{r,0,0}, c + Vec3{0,r,0}, c + Vec3{-r,0,0}, c + Vec3{0,-r,0}}, 0.205, 0.007);
    }
    return m;
}
inline Mesh maskInlayMesh() {
    Mesh m;
    for(int sector=0;sector<3;++sector) {
        Vec3 previous{};
        for(int i=0;i<7;++i) {
            const double a=(49+sector*120+i*13)*kPi/180.0;
            const Vec3 c{0.264*std::cos(a),0.12+0.264*std::sin(a),0};
            const double r=(i%3==0)?0.0055:0.0035;
            maskPrism(m,{c+Vec3{r,0,0},c+Vec3{0,r,0},c-Vec3{r,0,0},c-Vec3{0,r,0}},0.204,0.004);
            if(i>0) maskRail(m,previous,c,0.0015,0.200,0.002);
            previous=c;
        }
    }
    for(int k=0;k<3;++k) {
        const Vec3 c=maskEyeAt(k);
        maskDiamond(m,c,0.018,0.015,0.210,0.002);
        for(int side:{-1,1}) maskRail(m,c+Vec3{side*0.008,0.007,0},c+Vec3{0,-0.015,0},0.0018,0.211,0.002);
    }
    return m;
}
inline std::string boneIvoryMaterial() { return "Material \"coateddiffuse\" \"rgb reflectance\" [ 0.72 0.69 0.58 ] \"float roughness\" [ 0.42 ]"; }
inline std::string silverMaterial() { return metalMaterial({0.86, 0.87, 0.90}, 0.18); }
inline std::string maskText(const Mat4 &at, double glow = 0.0) {
    const Mat4 frame = mul(at, scale(kSuitMaskScale,kSuitMaskScale,kSuitMaskScale));
    const Vec3 e = Vec3{0.35, 0.12, 0.015} * (0.12 + 0.28 * clamp01(glow));
    return film_cache::part(kFilmKey, "mask6_constellation_inlays", goldMaterial(), frame, [] { return maskInlayMesh(); }) +
           film_cache::part(kFilmKey, "mask5_angular_ivory", boneIvoryMaterial(), frame, [] { return maskIvoryMesh(); }) +
           film_cache::part(kFilmKey, "mask5_angular_gold", goldMaterial(), frame, [] { return maskBronzeMesh(); }) +
           film_cache::part(kFilmKey, "mask5_chin_housing", diffuseMaterial({0.055, 0.045, 0.030}), frame, [] { return maskBackingMesh(); }) +
           attr("Material \"coateddiffuse\" \"rgb reflectance\" [ 0.42 0.19 0.028 ] \"float roughness\" [ 0.13 ]", meshText(maskLensMesh(), frame), &e);
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
		for (int k = 0; k < 3; ++k) {
            const EyePlace &eye=eyePlace(k);
            d=std::max(d,-(length(q-eye.c)-eye.r*1.10));
            d=std::min(d,eyelidSdf(q,k,lidClose[k]));
        }
		return std::min(d, earSdf(q, p.ears));
	}, {-0.20, -0.22, -0.22}, {0.20, 0.26, 0.27}, fine);
	headUV(skin);
	// A fitted dark liner encloses the face while the suit is sealed. It
    // reveals the fur continuously as the mask lifts away from the face.
    const double reveal=p.hood && p.maskShown ? smoothstep(0.0,0.35,p.maskOff) : 1.0;
    const std::string skinMaterial=headFurMaterial(p.furTint,reveal);
    out += attr(skinMaterial, meshText(skin, head));
	const double beardLength = p.maskShown ? smoothstep(0.2, 0.9, p.maskOff) * p.beardScale : p.beardScale;
	if (beardLength > 0.001) out += attr(beardMaterial(), meshText(sdf::surfaceNets([&](Vec3 q) { return beardSdf(q, beardLength); }, {-0.10, -0.42, 0.02}, {0.10, 0.0, 0.30}, fine), head));
	// (made once, so finer than the rest of the head: the growth rings are a centimetre apart. "horns2": the rings run round the horns now)
	if (!p.hood) out += film_cache::part(kFilmKey, "horns2" + grain, "Material \"coateddiffuse\" \"rgb reflectance\" [ 0.55 0.42 0.30 ] \"float roughness\" [ 0.5 ]", mul(head,translate(0,0.16,0),scale(p.hornGrowth,p.hornGrowth,p.hornGrowth),translate(0,-0.16,0)),
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
    if (p.hood) {
        const Mesh hood = sdf::surfaceNets([](Vec3 q) {
            const double shell = std::abs(ellv(q, {0.0, 0.075, -0.035}, {0.13, 0.19, 0.14})) - 0.009;
            return std::max(shell, q.z - 0.075);
        }, {-0.15, -0.14, -0.20}, {0.15, 0.28, 0.09}, cell * 0.5);
        out += attr(diffuseMaterial({0.055, 0.13, 0.135}), meshText(hood, head));
    }
	if (p.maskShown) out += maskText(mul(world, maskFrame(p, b)), p.staffGlow);
	// the suit
	Mesh robe = sdf::surfaceNets([&](Vec3 q) { return outerRobeSdf(q, p, b); }, lo, hi, cell);
	wrapUV(robe);
	out += attr(robeMaterial(p.robeTint), meshText(robe, world));
	out += attr(diffuseMaterial({0.15, 0.28, 0.28}), meshText(sdf::surfaceNets([&](Vec3 q) { return trousersSdf(q, p, b); }, {-0.4, -0.05, -0.4}, {0.4, b.pelvis.y + 0.05, 0.5}, cell), world));
	out += attr(diffuseMaterial({0.50, 0.23, 0.11}), meshText(sdf::surfaceNets([&](Vec3 q) { return hipWrapSdf(q, b); }, {-0.4, 0.2, -0.4}, {0.4, b.pelvis.y + 0.15, 0.5}, cell * 0.6), world));
	Mesh border = sdf::surfaceNets([&](Vec3 q) { return trimSdf(q, p, b); }, lo, hi, cell * 0.5);
	wrapUV(border);
	for (auto &uv : border.uvs) uv = {uv.first * 2.0, uv.second * 2.0};   // (matching UV scale for the cloth bindings)
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
		const Mat4 mf = mul(maskFrame(p, b), scale(kSuitMaskScale,kSuitMaskScale,kSuitMaskScale));
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
		const double grip = (p.staff == (s == 0 ? 1 : -1)) ? 0.9 : (s==0 && p.maskShown ? lerp(0.25,0.85,smoothstep(0.7,1.0,p.maskOff)) : 0.25);
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
