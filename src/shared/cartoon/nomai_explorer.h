#pragma once
// nomai_explorer.h -- the explorer of "The Last Nomai Signal" (nomai_signal.h): an original Nomai, as the game draws its people - tall and slender (1.86 m), digitigrade,
// with a goat-like face and a wide nose, three eyes (two below, one on the forehead; amber, with the goat's level, oblong pupils), three antlers (one curving back along
// each side of the head, one on top), ears that stand out sideways, short pale woolly fur, three-fingered hands, a stone-bead necklace, layered robes (dark grey inside,
// off-white, a pale beige outer robe with a hood and a shoulder cloth), a belt with pouches and a bronze clasp, cloth-wrapped feet, an aged ceramic mask with three eye
// openings, and a staff with a stone ring round a glowing core and ghostly blue channels down its shaft.
//
// The figure is sculpted for each frame from its pose (signed distance functions blended like clay, meshed by sdf::surfaceNets), so every part follows the pose: the robe's
// skirt swings with the long, slow steps of low gravity and stirs in the wind, each eyelid closes on its own, the third eye can open after the other two, the ears lift and
// droop, the mask comes off into a hand.
//
// Body space: the origin on the ground between the feet, +y up, the figure facing +z; +x is the figure's left. The head has a space of its own (its pivot at the top of the
// neck, +z forward), which the pose turns.

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "nature_lib.h"
#include "nomai_mesh.h"
#include "sdf_mesh.h"

namespace cartoon {
namespace nomai {

struct ExplorerPose {
	double walk = 0.0;     // the walk cycle (a whole number of steps: each foot has stepped once at 2)
	double stride = 0.0;   // 0 standing .. 1 a full stride
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

// ---- the skeleton ---------------------------------------------------------------------------------------------------------------------------------------------------
struct ExplorerBody {
	Vec3 pelvis, chest, neck, headPivot, shoulder[2], elbow[2], wrist[2], hip[2], knee[2], hock[2], ball[2];
	Mat4 head, headInv;   // head space -> body space, and back
	Vec3 handDir[2];      // where each hand points (wrist to fingers)
	double hemY;          // the robe's hem
};
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
		// the legs: the hip, the knee (forward), the hock (back), the ball of the foot; the walk swings each foot forward in turn and lifts it a little
		b.hip[s] = b.pelvis + Vec3{0.095 * side, -0.04, 0.0};
		const double phase = p.walk * kPi + (s == 0 ? 0.0 : kPi);
		const double swing = std::sin(phase), lift = std::max(0.0, std::cos(phase));
		Vec3 ball{0.11 * side, 0.03 + 0.07 * lift * p.stride, 0.07 + 0.42 * p.stride * swing};
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
inline double headSkinSdf(Vec3 q) {
	using namespace sdf;
	double d = ellv(q, {0.0, 0.090, -0.005}, {0.092, 0.104, 0.112});                                   // the cranium
	d = smin(d, ellipsoid(q, mul(rotate({1.0, 0.0, 0.0}, -18.0), translate(0.0, -0.030, -0.125)), {0.054, 0.058, 0.088}), 0.05);   // the long muzzle, angled down
	d = smin(d, ellv(q, {0.0, 0.006, 0.198}, {0.046, 0.030, 0.030}), 0.025);                            // the wide nose
	d = smin(d, ellv(q, {0.0, -0.030, 0.120}, {0.040, 0.026, 0.070}), 0.03);                            // the jaw
	d = smin(d, ellv(q, {0.0, 0.118, 0.080}, {0.075, 0.050, 0.040}), 0.03);                             // the brow over the eyes
	// the neck, down from the pivot
	d = smin(d, roundCone(q, {0.0, 0.03, -0.03}, {0.0, -0.16, -0.01}, 0.050, 0.060), 0.05);
	return d;
}
inline double eyelidSdf(Vec3 q, int k, double closed) {
	const EyePlace &e = eyePlace(k);
	const Vec3 v = q - e.c;
	const double ball = length(v) - (e.r + 0.0035);
	const double h = e.r * (0.80 - 1.95 * clamp01(closed));   // the lid's edge: high (open) .. below the eye (closed)
	return std::max(ball, h - v.y);
}
// The ears: long leaves standing out to the sides and a little back, lifted or drooping.
inline double earSdf(Vec3 q, double lift) {
	double d = 1e9;
	for (int s = 0; s < 2; ++s) {
		const double side = s == 0 ? 1.0 : -1.0;
		const Vec3 base{0.078 * side, 0.104, -0.012};
		const Vec3 tip = base + Vec3{0.125 * side, -0.035 + 0.05 * lift, -0.050};
		const Vec3 axis = normalize(tip - base), flat = normalize(cross(axis, Vec3{0.0, 0.0, 1.0}));
		const Mat4 frame = alignY((base + tip) * 0.5, axis, flat);
		d = std::min(d, sdf::ellipsoid(q, invertAffine(frame), {0.012, length(tip - base) * 0.55, 0.030}));
	}
	return d;
}
// The three antlers: tapering, gently ringed horns along curves.
inline const std::vector<std::vector<Vec3>> &hornCurves() {
	static const std::vector<std::vector<Vec3>> c = {
		{{0.052, 0.168, 0.035}, {0.085, 0.215, -0.030}, {0.115, 0.215, -0.120}, {0.130, 0.165, -0.195}, {0.128, 0.090, -0.205}, {0.112, 0.040, -0.170}},
		{{-0.052, 0.168, 0.035}, {-0.085, 0.215, -0.030}, {-0.115, 0.215, -0.120}, {-0.130, 0.165, -0.195}, {-0.128, 0.090, -0.205}, {-0.112, 0.040, -0.170}},
		{{0.0, 0.196, 0.030}, {0.0, 0.262, -0.015}, {0.0, 0.300, -0.100}, {0.0, 0.295, -0.190}, {0.0, 0.258, -0.250}},
	};
	return c;
}
inline double hornSdf(Vec3 q) {
	double d = 1e9;
	for (const auto &curve : hornCurves()) {
		const int n = static_cast<int>(curve.size());
		for (int i = 0; i + 1 < n; ++i) {
			const double r0 = 0.022 * (1.0 - 0.78 * i / (n - 1.0)), r1 = 0.022 * (1.0 - 0.78 * (i + 1) / (n - 1.0));
			d = std::min(d, sdf::roundCone(q, curve[static_cast<std::size_t>(i)], curve[static_cast<std::size_t>(i + 1)], r0, r1));
		}
	}
	// (the rings along a horn: a small ripple)
	return d + 0.0012 * std::sin(length(q) * 260.0);
}
// The mask: a carved shell of aged ceramic over the face, following the head's large shapes - the brow and the long muzzle, smoothed of the nose and the jaw - so it
// has the long snout of the Nomai masks, two centimetres off the fur; three eye openings ringed by raised rims, a carved band round the muzzle and a line down its
// bridge, and three small dots on the brow. `q` in the mask's own space (the head's, when it is worn).
inline double maskBaseSdf(Vec3 q) {
	using namespace sdf;
	static const Mat4 muzzle = mul(rotate({1.0, 0.0, 0.0}, -18.0), translate(0.0, -0.030, -0.125));   // (the skin's muzzle: an inverse frame, as sdf::ellipsoid takes it)
	double d = ellv(q, {0.0, 0.092, -0.005}, {0.092, 0.106, 0.112});
	d = smin(d, ellipsoid(q, muzzle, {0.060, 0.064, 0.112}), 0.07);
	return d;
}
inline double maskSdf(Vec3 q) {
	using namespace sdf;
	const double base = maskBaseSdf(q);
	double d = std::abs(base - 0.019) - 0.0050;
	// only the front of the face, from over the brow to under the muzzle
	d = std::max(d, 0.035 - q.z + 0.25 * std::max(0.0, q.y - 0.14));
	d = std::max(d, q.y - 0.215);
	d = std::max(d, -0.075 - q.y);
	// the eye openings, along each eye's gaze
	double rims = 1e9;
	for (int k = 0; k < 3; ++k) {
		const EyePlace &e = eyePlace(k);
		const Vec3 axis = normalize(Vec3{std::sin(e.outward * kPi / 180.0), 0.0, std::cos(e.outward * kPi / 180.0)});
		const Vec3 v = q - e.c;
		const double radial = length(v - axis * dot(v, axis));
		const double hole = (k == 2 ? 0.019 : 0.022);
		d = std::max(d, hole - radial);
		rims = std::min(rims, std::max(std::abs(radial - hole - 0.0045) - 0.0045, std::abs(base - 0.024) - 0.0045));
	}
	d = std::min(d, std::max(rims, 0.035 - q.z));
	// a carved band round the muzzle, a line down its bridge, three dots on the brow
	const Vec3 m = transformPoint(mul(rotate({1.0, 0.0, 0.0}, -18.0), translate(0.0, -0.030, -0.125)), q);
	double groove = std::abs(m.z - 0.025) - 0.0018;
	groove = std::min(groove, std::max(std::abs(q.x) - 0.0016, std::max(q.y - 0.070, -0.020 - q.y)));
	d = std::max(d, -std::max(groove, std::abs(base - 0.0245) - 0.003));
	for (int k = -1; k <= 1; ++k) {
		const Vec3 dot0{0.024 * k, 0.188 - 0.006 * std::abs(k), 0.0};
		// (on the shell's surface over the brow: the point of the base's offset surface nearest that height)
		d = std::max(d, -(std::sqrt((q.x - dot0.x) * (q.x - dot0.x) + (q.y - dot0.y) * (q.y - dot0.y)) - 0.0045 + std::max(0.0, std::abs(base - 0.024) - 0.004)));
	}
	return d;
}
// The hood: a cloth shell round the back and top of the head, open at the face, with the antlers and the ears through it, falling to the shoulders.
inline double hoodSdf(Vec3 q) {
	using namespace sdf;
	double d = std::abs(ellv(q, {0.0, 0.080, -0.035}, {0.140, 0.155, 0.155})) - 0.014;
	d = smin(d, std::abs(roundCone(q, {0.0, 0.02, -0.06}, {0.0, -0.17, -0.06}, 0.125, 0.18)) - 0.014, 0.03);
	d = std::max(d, q.z - (0.030 - 0.25 * std::max(0.0, q.y - 0.12)));   // (the opening for the face)
	d = std::max(d, -0.20 - q.y);
	d = std::max(d, -(hornSdf(q) - 0.010));
	d = std::max(d, -(earSdf(q, 0.0) - 0.012));
	return d;
}

// ---- the body (body space) ------------------------------------------------------------------------------------------------------------------------------------------
// The robe's sway at a height: the walk swings the skirt, the wind lifts it (more towards the hem).
inline Vec3 robeSway(const ExplorerPose &p, const ExplorerBody &b, double y) {
	const double below = clamp01((b.pelvis.y - y) / std::max(0.2, b.pelvis.y - b.hemY));
	const double flutter = std::sin(p.time * 5.3 + y * 9.0) * 0.4 + std::sin(p.time * 8.1 + y * 17.0) * 0.2;
	return Vec3{p.wind.x, 0.0, p.wind.z} * (below * below * (1.0 + 0.4 * flutter)) + Vec3{0.0, 0.0, 0.035 * p.stride * std::sin(p.walk * kPi * 2.0)} * below;
}
inline double skirtSdf(Vec3 q, const ExplorerPose &p, const ExplorerBody &b, double inset) {
	using namespace sdf;
	const Vec3 s = q - robeSway(p, b, q.y);
	const Vec3 hemCentre{0.0, b.hemY, 0.035 + 0.05 * p.sit};
	double d = roundCone(s, b.pelvis + Vec3{0.0, 0.03, 0.0}, hemCentre, 0.155 - inset, 0.28 + 0.14 * p.sit - inset);
	// the legs push the cloth
	for (int k = 0; k < 2; ++k) d = smin(d, roundCone(s, b.hip[k], b.knee[k], 0.12 - inset, 0.10 - inset), 0.08);
	// a ragged hem
	const double hem = b.hemY + 0.025 * nature::noise2(std::atan2(q.x, q.z) * 4.0 + 10.0, 0.5, 81) + 0.012 * std::sin(std::atan2(q.x, q.z) * 7.0);
	return std::max(d, hem - q.y);
}
inline double torsoSdf(Vec3 q, const ExplorerBody &b, double inset) {
	using namespace sdf;
	double d = roundCone(q, b.neck + Vec3{0.0, -0.035, 0.0}, b.pelvis + Vec3{0.0, 0.02, 0.0}, 0.105 - inset, 0.150 - inset);
	d = smin(d, ellv(q, b.chest + Vec3{0.0, -0.02, 0.0}, {0.160 - inset, 0.16, 0.100 - inset}), 0.06);
	return d;
}
// The outer robe: the body, the skirt and the sleeves, open down the front below the belt.
inline double outerRobeSdf(Vec3 q, const ExplorerPose &p, const ExplorerBody &b) {
	using namespace sdf;
	double d = smin(torsoSdf(q, b, 0.0), skirtSdf(q, p, b, 0.0), 0.06);
	for (int s = 0; s < 2; ++s) {
		d = smin(d, roundCone(q, b.shoulder[s], b.elbow[s], 0.058, 0.054), 0.03);
		const Vec3 cuff = b.wrist[s] - b.handDir[s] * 0.02;
		d = smin(d, roundCone(q, b.elbow[s], cuff, 0.054, 0.072), 0.02);
		d = std::max(d, -(roundCone(q, cuff - b.handDir[s] * 0.10, cuff + b.handDir[s] * 0.05, 0.040, 0.062)));   // (the sleeve's open end)
	}
	// the opening at the front, widening towards the hem
	const double below = b.pelvis.y - q.y;
	if (below > 0.0) d = std::max(d, -(std::max(std::abs(q.x) - (0.012 + 0.10 * below), 0.02 - q.z)));
	return d;
}
inline double middleRobeSdf(Vec3 q, const ExplorerPose &p, const ExplorerBody &b) {
	double d = sdf::smin(torsoSdf(q, b, 0.010), skirtSdf(q, p, b, 0.014), 0.06);
	// it shows only below the belt, in the outer robe's opening; and the dark inner layer in its own, narrower opening
	d = std::max(d, q.y - b.pelvis.y + 0.02);
	const double below = b.pelvis.y - q.y;
	return std::max(d, -(std::max(std::abs(q.x) - (0.004 + 0.045 * below), 0.02 - q.z)));
}
inline double innerRobeSdf(Vec3 q, const ExplorerPose &p, const ExplorerBody &b) {
	double d = sdf::smin(torsoSdf(q, b, 0.020), skirtSdf(q, p, b, 0.028), 0.06);
	d = std::max(d, q.y - b.pelvis.y + 0.02);
	// and a dark collar at the neck
	return std::min(d, std::max(std::abs(sdf::roundCone(q, b.neck + Vec3{0.0, -0.05, 0.0}, b.neck + Vec3{0.0, 0.03, 0.01}, 0.072, 0.062)) - 0.006, q.y - b.neck.y - 0.03));
}
// The shoulder cloth: a short poncho draped from the neck over the shoulders to the upper arms, its edge cut in a soft wave, stirred by the wind.
inline double mantleSdf(Vec3 q, const ExplorerPose &p, const ExplorerBody &b) {
	using namespace sdf;
	Vec3 s = q - Vec3{p.wind.x, 0.0, p.wind.z} * (0.25 * clamp01((b.neck.y - q.y) / 0.3));
	const Vec3 c = b.neck;
	s = c + Vec3{s.x - c.x, s.y - c.y, (s.z - c.z) * 1.22};
	double d = std::abs(roundCone(s, b.neck + Vec3{0.0, 0.015, 0.0}, b.chest + Vec3{0.0, -0.17, -0.01}, 0.072, 0.232)) - 0.011;
	d = std::max(d, b.chest.y - 0.18 + 0.025 * std::sin(std::atan2(q.x, q.z) * 7.0) - q.y);
	d = std::max(d, q.y - b.neck.y - 0.01);
	return d;
}
inline double beltSdf(Vec3 q, const ExplorerBody &b) {
	using namespace sdf;
	const Vec3 c = b.pelvis + Vec3{0.0, 0.03, 0.01};
	const Vec3 v = q - c;
	const double ring = std::sqrt(v.x * v.x / 1.0 + v.z * v.z / 0.72);
	double d = std::sqrt(std::pow(ring - 0.160, 2.0) + v.y * v.y) - 0.018;   // the belt
	d = std::min(d, std::sqrt(std::pow(ring - 0.165, 2.0) + std::pow(v.y + 0.030, 2.0)) - 0.006);   // a rope binding under it
	// pouches: one on the left hip, a smaller one at the right front
	d = std::min(d, roundBox(q, invertAffine(mul(translate(c + Vec3{0.150, -0.055, 0.02}), rotate({0.0, 1.0, 0.0}, 80.0))), {0.045, 0.050, 0.022}, 0.014));
	d = std::min(d, roundBox(q, invertAffine(mul(translate(c + Vec3{-0.105, -0.050, 0.110}), rotate({0.0, 1.0, 0.0}, -35.0))), {0.032, 0.036, 0.018}, 0.010));
	return d;
}
inline double claspSdf(Vec3 q, const ExplorerBody &b) {
	const Vec3 c = b.pelvis + Vec3{0.0, 0.03, 0.143};
	const Vec3 v = q - c;
	return std::max(std::sqrt(v.x * v.x + v.y * v.y) - 0.024, std::abs(v.z) - 0.007);
}
inline double necklaceSdf(Vec3 q, const ExplorerBody &b, bool teal) {
	double d = 1e9;
	for (int k = 0; k < 13; ++k) {
		if ((k % 4 == 1) != teal) continue;
		const double a = (k - 6) * 0.30;
		const Vec3 c = b.neck + Vec3{0.088 * std::sin(a), -0.055 + 0.03 * (1.0 - std::cos(a)), 0.015 + 0.075 * std::cos(a)};
		d = std::min(d, length(q - c) - (k == 6 ? 0.016 : 0.011));
	}
	return d;
}
// The hands: a palm and three long fingers (two and a thumb), curled by `grip`.
inline double handSdf(Vec3 q, const ExplorerBody &b, int s, double grip) {
	using namespace sdf;
	const Vec3 w = b.wrist[s], dir = b.handDir[s];
	const double side = s == 0 ? 1.0 : -1.0;
	const Vec3 across = normalize(cross(dir, Vec3{0.0, 1.0, 0.0})) * side;
	const Vec3 palmUp = normalize(cross(across, dir));
	const Vec3 palm = w + dir * 0.045;
	double d = ellipsoid(q, invertAffine(alignY(palm, dir, across)), {0.030, 0.045, 0.016});
	for (int f = 0; f < 3; ++f) {
		const bool thumb = f == 2;
		const Vec3 base = thumb ? palm + across * 0.026 - dir * 0.015 : palm + dir * 0.038 + across * (f == 0 ? 0.013 : -0.013);
		const Vec3 out = thumb ? normalize(across * 0.8 + dir * 0.6) : dir;
		const double len = thumb ? 0.050 : 0.068;
		const Vec3 mid = base + out * (len * 0.5) - palmUp * (len * 0.25 * grip);
		const Vec3 tip = mid + normalize(out * (1.0 - grip) - palmUp * (0.2 + grip)) * (len * 0.5);
		d = smin(d, roundCone(q, base, mid, 0.012, 0.010), 0.012);
		d = smin(d, roundCone(q, mid, tip, 0.010, 0.0075), 0.006);
	}
	return d;
}
// The feet below the hem: cloth wraps from the hock to the cloven foot.
inline double footSdf(Vec3 q, const ExplorerBody &b) {
	using namespace sdf;
	double d = 1e9;
	for (int s = 0; s < 2; ++s) {
		d = std::min(d, roundCone(q, b.knee[s], b.hock[s], 0.050, 0.036));
		d = smin(d, roundCone(q, b.hock[s], b.ball[s], 0.034, 0.030), 0.02);
		const Vec3 fwd = normalize(Vec3{b.ball[s].x - b.hock[s].x, 0.0, b.ball[s].z - b.hock[s].z} + Vec3{0.0, 0.0, 1e-6});
		const Vec3 side = normalize(cross(Vec3{0.0, 1.0, 0.0}, fwd));
		for (int t = -1; t <= 1; t += 2)
			d = smin(d, ellv(q, b.ball[s] + fwd * 0.040 + side * (0.016 * t) + Vec3{0.0, -0.012, 0.0}, {0.017, 0.016, 0.034}), 0.012);
	}
	return d;
}

// ---- the staff -----------------------------------------------------------------------------------------------------------------------------------------------------
// Its frame: along the hand's grip, its top `staffTip` or straight up from the hand. The shaft runs from 1.2 m below the grip to 0.62 m above it.
inline Mat4 staffFrame(const ExplorerPose &p, const ExplorerBody &b) {
	const int s = p.staff > 0 ? 0 : 1;
	const Vec3 grip = b.wrist[s] + b.handDir[s] * 0.05;
	const Vec3 up = (p.staffTip.x != 0.0 || p.staffTip.y != 0.0 || p.staffTip.z != 0.0) ? normalize(p.staffTip - grip) : normalize(Vec3{0.0, 1.0, 0.05});
	return alignY(grip, up);
}
inline std::string staffText(const ExplorerPose &p, const ExplorerBody &b, const Mat4 &world) {
	if (p.staff == 0) return std::string();
	const Mat4 m = mul(world, staffFrame(p, b));
	std::string out;
	// the shaft (dark wood under a bronze spiral binding) and its foot
	std::vector<std::pair<double, double>> shaft;
	for (int i = 0; i <= 40; ++i) {
		const double y = -1.20 + 1.80 * i / 40.0;
		shaft.push_back({(i == 0 ? 0.0 : 0.016 + 0.002 * std::sin(i * 1.3)), y});
	}
	shaft.push_back({0.0, 0.60});
	out += attr(diffuseMaterial({0.16, 0.11, 0.08}), meshText(lathe(shaft, 14), m));
	std::string binding;
	for (int k = 0; k < 9; ++k) binding += meshText(torus(0.0185, 0.0035, 16, 6), mul(m, translate(0.0, -0.9 + 0.17 * k, 0.0)));
	out += attr(metalMaterial({0.72, 0.52, 0.32}, 0.35), binding);
	// the head: a stone ring on its edge, held by bronze fork arms, round a glowing core
	out += attr(metalMaterial({0.72, 0.52, 0.32}, 0.3), rod({0.0, 0.58, 0.0}, {0.05, 0.66, 0.0}, 0.008) + rod({0.0, 0.58, 0.0}, {-0.05, 0.66, 0.0}, 0.008));
	const Mat4 ring = mul(m, translate(0.0, 0.70, 0.0), rotate({1.0, 0.0, 0.0}, 90.0));
	out += attr(coated({0.70, 0.66, 0.58}, 0.6), meshText(torus(0.075, 0.016, 32, 10), ring));
	const Vec3 core = transformPoint(m, {0.0, 0.70, 0.0});
	const double glow = 0.15 + 0.85 * clamp01(p.staffGlow);
	out += emissiveSphere(core, 0.026, Vec3{0.55, 0.80, 1.0} * (2.5 * glow));
	// the channels down the shaft: three thin lines of ghostly blue
	const Vec3 e = Vec3{0.45, 0.75, 1.0} * (1.6 * glow);
	std::string shapes;
	for (int k = 0; k < 3; ++k) {
		const double a = 2.0 * kPi * k / 3.0;
		shapes += meshText(capsule(0.0022, 0.86, 5, 2), mul(m, translate(0.0175 * std::cos(a), -0.30, 0.0175 * std::sin(a))));
	}
	out += attr("Material \"diffuse\" \"rgb reflectance\" [ 0 0 0 ]", shapes, &e);
	return out;
}

// ---- the figure ----------------------------------------------------------------------------------------------------------------------------------------------------
inline std::string furMaterial() { return "Material \"diffuse\" \"rgb reflectance\" [ 0.84 0.82 0.78 ]"; }
inline std::string maskMaterial() { return "Material \"coateddiffuse\" \"rgb reflectance\" [ 0.80 0.76 0.68 ] \"float roughness\" [ 0.45 ]"; }
// The mask's frame (mask space -> body space): on the face, lifted away from it, then lowered into the left hand.
inline Mat4 maskFrame(const ExplorerPose &p, const ExplorerBody &b) {
	if (p.maskOff <= 0.0) return b.head;
	const double u = clamp01(p.maskOff);
	const Mat4 lifted = mul(b.head, translate(0.0, 0.0, 0.08 * smoothstep(0.0, 0.35, u)));
	if (u <= 0.35) return lifted;
	// in the hand: held at the hand, face up and outwards
	const Vec3 hand = b.wrist[0] + b.handDir[0] * 0.06;
	const Mat4 held = mul(translate(hand + Vec3{0.0, 0.03, 0.0}), rotate({1.0, 0.0, 0.0}, -60.0), rotate({0.0, 1.0, 0.0}, 25.0), translate(0.0, -0.08, -0.10));
	const double v = smoothstep(0.35, 1.0, u);
	// (blend the two frames' origins and axes: a rigid-enough move for a held mask)
	Mat4 m;
	for (int i = 0; i < 16; ++i) m[static_cast<std::size_t>(i)] = lerp(lifted[static_cast<std::size_t>(i)], held[static_cast<std::size_t>(i)], v);
	// re-orthonormalise the rotation
	Vec3 x{m[0], m[4], m[8]}, y{m[1], m[5], m[9]};
	x = normalize(x);
	Vec3 z = normalize(cross(x, y));
	y = cross(z, x);
	m[0] = x.x; m[4] = x.y; m[8] = x.z;
	m[1] = y.x; m[5] = y.y; m[9] = y.z;
	m[2] = z.x; m[6] = z.y; m[10] = z.z;
	return m;
}
// The mask on its own (laid on the ground beside the campfire): at `frame` (mask space -> world).
inline std::string maskText(const Mat4 &frame, double cell = 0.003) {
	static const Mesh mesh = sdf::surfaceNets([](Vec3 q) { return maskSdf(q); }, {-0.12, -0.12, 0.02}, {0.12, 0.25, 0.30}, cell);
	return attr(maskMaterial(), meshText(mesh, frame));
}

// The explorer standing at `world` (body space -> story space) in pose `p`, sculpted at `cell` metres (the face and the hands finer).
inline std::string explorerText(const ExplorerPose &p, const Mat4 &world, double cell = 0.012) {
	const ExplorerBody b = explorerBody(p);
	const double fine = cell * 0.4;
	const Vec3 lo{-0.70, -0.02, -0.70}, hi{0.70, 1.90, 0.80};
	std::string out;
	// the head's parts are sculpted in head space and placed by the head's frame
	const Mat4 head = mul(world, b.head);
	const Vec3 hlo{-0.22, -0.22, -0.26}, hhi{0.22, 0.30, 0.27};
	const double lidClose[3] = {p.lidL, p.lidR, p.lidTop};
	const Mesh skin = sdf::surfaceNets([&](Vec3 q) {
		double d = headSkinSdf(q);
		for (int k = 0; k < 3; ++k) d = std::min(d, eyelidSdf(q, k, lidClose[k]));
		return std::min(d, earSdf(q, p.ears));
	}, hlo, hhi, fine);
	out += attr(furMaterial(), meshText(skin, head));
	out += attr("Material \"coateddiffuse\" \"rgb reflectance\" [ 0.46 0.34 0.22 ] \"float roughness\" [ 0.5 ]", meshText(sdf::surfaceNets([](Vec3 q) { return hornSdf(q); }, {-0.17, 0.0, -0.29}, {0.17, 0.34, 0.08}, fine), head));
	// the eyes: amber, with level oblong pupils that follow the gaze
	std::string eyes, pupils;
	for (int k = 0; k < 3; ++k) {
		const EyePlace &e = eyePlace(k);
		const Mat4 look = mul(translate(e.c), rotate({0.0, 1.0, 0.0}, e.outward * 0.5 + p.gazeYaw), rotate({1.0, 0.0, 0.0}, p.gazePitch));
		eyes += meshText(uvSphere(e.r, 20, 12), mul(head, look));
		pupils += meshText(roundedBox(e.r * 1.05, e.r * 0.34, e.r * 0.25, e.r * 0.12, 2), mul(head, look, translate(0.0, 0.0, e.r * 0.90)));
	}
	out += attr("Material \"coateddiffuse\" \"rgb reflectance\" [ 0.66 0.44 0.12 ] \"float roughness\" [ 0.02 ]", eyes);
	out += attr("Material \"coateddiffuse\" \"rgb reflectance\" [ 0.008 0.008 0.008 ] \"float roughness\" [ 0.02 ]", pupils);
	if (p.hood) out += attr(diffuseMaterial({0.72, 0.64, 0.50}), meshText(sdf::surfaceNets([](Vec3 q) { return hoodSdf(q); }, {-0.20, -0.24, -0.27}, {0.20, 0.30, 0.14}, (std::min)(0.006, cell * 0.5)), head));
	if (p.maskShown) {
		static const Mesh maskMesh = sdf::surfaceNets([](Vec3 q) { return maskSdf(q); }, {-0.12, -0.12, 0.02}, {0.12, 0.25, 0.30}, 0.0028);
		out += attr(maskMaterial(), meshText(maskMesh, mul(world, maskFrame(p, b))));
	}
	// the body
	out += attr(diffuseMaterial({0.74, 0.66, 0.52}), meshText(sdf::surfaceNets([&](Vec3 q) { return outerRobeSdf(q, p, b); }, lo, hi, cell), world));
	out += attr(diffuseMaterial({0.87, 0.85, 0.79}), meshText(sdf::surfaceNets([&](Vec3 q) { return middleRobeSdf(q, p, b); }, lo, {hi.x, b.pelvis.y + 0.05, hi.z}, cell), world));
	out += attr(diffuseMaterial({0.21, 0.21, 0.23}), meshText(sdf::surfaceNets([&](Vec3 q) { return innerRobeSdf(q, p, b); }, lo, hi, cell), world));
	out += attr(diffuseMaterial({0.64, 0.55, 0.42}), meshText(sdf::surfaceNets([&](Vec3 q) { return mantleSdf(q, p, b); }, {-0.4, b.chest.y - 0.25, -0.35}, {0.4, b.neck.y + 0.1, 0.35}, cell * 0.8), world));
	const Vec3 blo{-0.30, b.pelvis.y - 0.15, -0.25}, bhi{0.30, b.pelvis.y + 0.10, 0.25};
	out += attr(diffuseMaterial({0.30, 0.20, 0.13}), meshText(sdf::surfaceNets([&](Vec3 q) { return beltSdf(q, b); }, blo, bhi, cell * 0.5), world));
	out += attr(metalMaterial({0.72, 0.52, 0.32}, 0.35), meshText(sdf::surfaceNets([&](Vec3 q) { return claspSdf(q, b); }, blo, bhi, cell * 0.4), world));
	const Vec3 nlo = b.neck + Vec3{-0.14, -0.12, -0.08}, nhi = b.neck + Vec3{0.14, 0.02, 0.14};
	out += attr(diffuseMaterial({0.62, 0.58, 0.52}), meshText(sdf::surfaceNets([&](Vec3 q) { return necklaceSdf(q, b, false); }, nlo, nhi, cell * 0.35), world));
	out += attr(diffuseMaterial({0.18, 0.48, 0.46}), meshText(sdf::surfaceNets([&](Vec3 q) { return necklaceSdf(q, b, true); }, nlo, nhi, cell * 0.35), world));
	for (int s = 0; s < 2; ++s) {
		const double grip = (p.staff == (s == 0 ? 1 : -1)) ? 0.9 : 0.25;
		const Vec3 c = b.wrist[s] + b.handDir[s] * 0.06;
		out += attr(furMaterial(), meshText(sdf::surfaceNets([&](Vec3 q) { return handSdf(q, b, s, grip); }, c - Vec3{0.13, 0.13, 0.13}, c + Vec3{0.13, 0.13, 0.13}, fine), world));
	}
	out += attr(diffuseMaterial({0.28, 0.24, 0.21}), meshText(sdf::surfaceNets([&](Vec3 q) { return std::max(footSdf(q, b), q.y - b.hemY - 0.05); }, {-0.4, -0.05, -0.35}, {0.4, b.hemY + 0.08, 0.6}, cell * 0.6), world));
	out += staffText(p, b, world);
	return out;
}

}  // namespace nomai
}  // namespace cartoon
