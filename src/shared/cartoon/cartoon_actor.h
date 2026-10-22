#pragma once
// cartoon_actor.h -- a character rig for the cartoon movies: a round little creature (Pip) with a big head, eyes that look where it looks and blink, eyebrows, a mouth that smiles, frowns
// and opens, two ears that perk or droop, two arms that reach for a point in the world, and two feet that walk. A film acts by filling in a `Pose` for each instant (the way a puppeteer
// would) and asking for `emitActor(character, pose)`; everything the face can do is one number in the pose.
//
// The rig's own frame has +x forward, +y up and +z to the actor's right (the same as the rest of the toolkit: +x to the right of the picture, +z towards the camera, so an actor facing
// +x turns its right side to the camera). `pose.yaw` turns the whole actor about the vertical: 0 faces +x and a positive angle turns the front towards the camera.
//
//   body        a small egg on two feet; squashes and stretches as one with the head (`squash`), leans, rolls, and is lifted off the ground by `lift` (a hop)
//   head        a large sphere on the body, turned by `headYaw`, `headPitch` (positive looks down) and `headRoll`
//   eyes        white balls with a pupil and a highlight; the pupil points at `look` (a world point) so that the eyes can follow anything; `blink` closes the lids (0 open, 1 shut,
//               0.5 half-lidded), `eyeWide` makes them bigger for surprise and `lidTilt` slants the lids (sad and angry)
//   brows       each raised or lowered (`browRaise`) and tilted (`browTilt`: positive = inner end up, the worried look; negative = the angry V)
//   mouth       a curve whose ends turn up or down with `smile`, and opens into a dark mouth with `mouthOpen`
//   arms        two links each; the hand goes to `hand` (world point) while `handWeight` is 1 and rests by the side at 0
//   feet        walk with `stepPhase` (one cycle per 2 pi) and `stepAmount` (0 standing, 1 a full stride); the body bobs and rolls with the steps
//
// Parts are built once; a character with a pose is about 20 small meshes, so a scene can hold several.

#include "cartoon_lib.h"

namespace cartoon {
namespace actor {

struct Character {
	Vec3 skin = {0.58, 0.80, 0.93};       // head, body and arms
	Vec3 belly = {0.88, 0.96, 0.98};      // a pale patch on the front of the body
	Vec3 feet = {0.96, 0.56, 0.16};       // feet, ear tips, the tuft
	Vec3 cheek = {0.98, 0.52, 0.55};
	Vec3 pupil = {0.07, 0.05, 0.08};
	double scale = 1.0;                   // the whole character: a child 0.6, a parent 1.3
};

struct Pose {
	Vec3 pos = {0.0, 0.0, 0.0};   // on the ground, between the feet
	double yaw = 0.0;
	double squash = 1.0;          // vertical scale of the whole body; the width follows so that the volume stays
	double lean = 0.0;            // forward lean of the whole body, degrees (positive leans forward)
	double roll = 0.0;            // sideways lean, degrees
	double lift = 0.0;            // height of the feet above the ground
	double headPitch = 0.0, headYaw = 0.0, headRoll = 0.0;   // degrees
	Vec3 look = {0.0, 0.0, 0.0};
	double lookWeight = 0.0;      // 0 the eyes look straight ahead, 1 they look at `look`
	double blink = 0.0;
	double eyeWide = 1.0;
	double lidTilt = 0.0;         // -1 .. 1: positive slants the lids down towards the nose (angry), negative up towards it (sad)
	double browRaiseL = 0.0, browRaiseR = 0.0, browTiltL = 0.0, browTiltR = 0.0;
	double smile = 0.3;           // -1 .. 1
	double mouthOpen = 0.0;       // 0 .. 1
	double earDroop = 0.0;        // -1 perked, 0 resting, 1 drooping
	Vec3 handL = {0.0, 0.0, 0.0}, handR = {0.0, 0.0, 0.0};
	double handWeightL = 0.0, handWeightR = 0.0;
	double stepPhase = 0.0, stepAmount = 0.0;
};

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// Parts
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
constexpr double kHeadR = 0.50;     // the head's radius
constexpr double kBodyH = 0.58;     // the body's height; the head sits on top of it
constexpr double kEyeR = 0.145;     // an eye's radius

struct Parts {
	Mesh head, body, eye, lid, bar, foot, earTip, hand, mouth;
};
inline const Parts &parts() {
	static const Parts p = [] {
		Parts q;
		q.head = uvSphere(1.0, 48, 24);
		q.body = lathe({{0.0, 0.0}, {0.20, 0.0}, {0.30, 0.10}, {0.33, 0.26}, {0.30, 0.42}, {0.22, 0.54}, {0.0, 0.58}}, 40);
		q.eye = uvSphere(1.0, 32, 16);
		// a lid: the cap of a sphere a little larger than the eye, its pole up the +y axis, from the rim (the equator, at the bottom of the profile) to the pole
		q.lid = lathe({{1.08, 0.0}, {1.043, 0.28}, {0.935, 0.54}, {0.764, 0.764}, {0.54, 0.935}, {0.28, 1.043}, {0.0, 1.08}}, 28);
		q.bar = capsule(1.0, 1.0, 8, 2);
		q.foot = uvSphere(1.0, 20, 10);
		q.earTip = uvSphere(1.0, 14, 7);
		q.hand = uvSphere(1.0, 16, 8);
		q.mouth = uvSphere(1.0, 16, 8);
		return q;
	}();
	return p;
}

// A thin bar from a to b (radius r), as scene text; the actor's parts are emitted through these.
inline std::string rod(Vec3 a, Vec3 b, double r) { return bar(a, b, r, 8); }

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The actor
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
inline double wrapDegrees(double d) {
	while (d > 180.0) d -= 360.0;
	while (d < -180.0) d += 360.0;
	return d;
}

// A point on the head's surface (offset `out` outside it), at the horizontal angle `yawDeg` from straight ahead and the vertical angle `pitchDeg` above the horizon, in the head's own frame
// (centre at the origin, +x forward).
inline Vec3 onHead(double yawDeg, double pitchDeg, double out = 0.0, double radius = kHeadR) {
	const double y = yawDeg * kPi / 180.0, p = pitchDeg * kPi / 180.0, r = radius + out;
	return {r * std::cos(p) * std::cos(y), r * std::sin(p), r * std::cos(p) * std::sin(y)};
}

// The body's frames for a pose: `stance` (the feet's middle, turned, lifted, leaned) and `neck` (the base of the head, squashed with the body).
struct Frames {
	Mat4 root, stance, neck;
	double S, sxz, sy;
};
inline Frames framesOf(const Character &c, const Pose &pose) {
	Frames f;
	f.S = c.scale;
	const double walkBob = 0.035 * pose.stepAmount * std::abs(std::sin(pose.stepPhase));
	const double walkRoll = 6.0 * pose.stepAmount * std::sin(pose.stepPhase);
	f.sy = std::max(0.2, pose.squash);
	f.sxz = 1.0 / std::sqrt(f.sy);
	f.root = mul(translate(pose.pos), rotate({0, 1, 0}, -pose.yaw), translate(0.0, (pose.lift + walkBob) * f.S, 0.0));
	// lean and roll act about the feet's middle; squash is applied above it
	f.stance = mul(f.root, rotate({0, 0, 1}, -pose.lean), rotate({1, 0, 0}, pose.roll + walkRoll));
	f.neck = mul(f.stance, scale(f.S * f.sxz, f.S * f.sy, f.S * f.sxz), translate(0.0, 0.10 + kBodyH * 0.80, 0.0));
	return f;
}

// One arm of a pose: the shoulder, the elbow (bent towards the back and down) and the hand, which is the pose's hand target held within the arm's reach.
struct Arm {
	Vec3 shoulder, elbow, hand;
};
inline Arm armOf(const Character &c, const Pose &pose, double side /* -1 the actor's left, +1 its right */) {
	const Frames f = framesOf(c, pose);
	const double S = f.S;
	const Vec3 shoulder = transformPoint(f.neck, {0.0, -0.12, side * 0.30});
	const double w = clamp01(side < 0 ? pose.handWeightL : pose.handWeightR);
	// at rest the hand hangs a little out from the side
	const Vec3 rest = transformPoint(f.stance, {0.04 * S, 0.30 * S, side * 0.42 * S});
	const Vec3 target = lerp(rest, side < 0 ? pose.handL : pose.handR, w);
	const double reachMax = 0.60 * S;
	Vec3 handP = target;
	const Vec3 toT = target - shoulder;
	if (length(toT) > reachMax) handP = shoulder + normalize(toT) * reachMax;
	const Limb limb = limbIk(shoulder, handP, 0.31 * S, 0.29 * S, normalize(transformPoint(f.stance, {-0.4, -0.8, side * 0.9}) - transformPoint(f.stance, {0.0, 0.0, 0.0})));
	return {shoulder, limb.elbow, limb.hand};
}

inline std::string emitActor(const Character &c, const Pose &pose) {
	const Parts &P = parts();
	std::string out;
	const double S = c.scale;
	const std::string skin = coated(c.skin, 0.30), belly = coated(c.belly, 0.35), feetMat = coated(c.feet, 0.30), cheekMat = coated(c.cheek, 0.6);
	const std::string white = coated({0.97, 0.97, 0.95}, 0.08), pupilMat = coated(c.pupil, 0.04), lidMat = coated(c.skin, 0.30), dark = diffuseMaterial({0.10, 0.03, 0.04}), tongue = diffuseMaterial({0.85, 0.35, 0.40});

	// ---- the body frame: where the feet are, turned, lifted, leaned, squashed ----
	const Frames fr = framesOf(c, pose);
	const double sy = fr.sy, sxz = fr.sxz;
	const Mat4 &root = fr.root, &stance = fr.stance;
	const Mat4 bodyM = mul(stance, scale(S * sxz, S * sy, S * sxz), translate(0.0, 0.10, 0.0));
	// the body, and a belly patch on its front
	out += attr(skin, emitMesh(P.body, bodyM));
	out += attr(belly, emitMesh(P.head, mul(bodyM, translate(0.17, 0.27, 0.0), scale(0.17, 0.20, 0.17))));

	// ---- the head ----
	const Mat4 &neck = fr.neck;
	const Mat4 headM = mul(neck, rotate({1, 0, 0}, pose.headRoll), rotate({0, 1, 0}, -pose.headYaw), rotate({0, 0, 1}, -pose.headPitch), translate(0.0, kHeadR * 0.80, 0.0));
	out += attr(skin, emitMesh(P.head, mul(headM, scale(kHeadR * 1.06, kHeadR, kHeadR * 1.04))));
	// cheeks
	std::string cheeks;
	for (double s : {-1.0, 1.0}) cheeks += emitMesh(P.head, mul(headM, translate(onHead(s * 38.0, -12.0, -0.004)), rotate({0, 1, 0}, -s * 38.0), scale(0.028, 0.065, 0.085)));
	out += attr(cheekMat, cheeks);

	// ---- the eyes ----
	const double wide = std::max(0.3, pose.eyeWide);
	std::string whites, pupils, highlights, lids;
	for (double side : {-1.0, 1.0}) {   // -1 the actor's left eye (-z), +1 the right
		const Vec3 eyeLocal = onHead(side * 21.0, 6.0, -0.060);
		const double R = kEyeR * wide;
		whites += emitMesh(P.eye, mul(headM, translate(eyeLocal), scale(R * 1.0)));
		// the pupil points at the target (in the head's frame), within the eye's reach
		Vec3 dir = {1.0, 0.0, 0.0};
		if (pose.lookWeight > 0.0) {
			const Mat4 inv = invertAffine(headM);
			const Vec3 target = transformPoint(inv, pose.look);
			Vec3 d = normalize(target - eyeLocal);
			const double reach = 0.62;   // how far from straight ahead a pupil can turn, as a sine
			d.x = std::max(d.x, 0.45);
			d = normalize(d);
			if (std::abs(d.y) > reach) d.y = d.y > 0 ? reach : -reach;
			if (std::abs(d.z) > reach) d.z = d.z > 0 ? reach : -reach;
			dir = normalize(lerp(Vec3{1.0, 0.0, 0.0}, normalize(d), clamp01(pose.lookWeight)));
		}
		const Vec3 pupilC = eyeLocal + dir * (R * 0.62);
		pupils += emitMesh(P.eye, mul(headM, translate(pupilC), scale(R * 0.52)));
		highlights += emitMesh(P.eye, mul(headM, translate(pupilC + normalize(dir + Vec3{0.1, 0.55, -0.35 * side}) * (R * 0.30) + dir * (R * 0.18)), scale(R * 0.14)));
		// the upper lid: its pole swings from behind the eye (open) over the top (half) to the front (shut)
		const double b = clamp01(pose.blink);
		const double psi = (180.0 - 180.0 * b) * kPi / 180.0;
		Vec3 axis = {std::cos(psi), std::sin(psi), 0.0};
		// a slanted lid: the pole leans sideways, towards the nose for an angry look, away for a sad one
		const double tilt = pose.lidTilt * 0.55 * side * -1.0;
		axis = normalize(axis + Vec3{0.0, 0.0, tilt * std::sin(psi) * 0.9});
		if (b > 0.015) lids += emitMesh(P.lid, mul(headM, alignY(eyeLocal, axis), scale(R)));
	}
	out += attr(white, whites);
	out += attr(pupilMat, pupils);
	out += attr(white, highlights);
	if (!lids.empty()) out += attr(lidMat, lids);

	// ---- the brows ----
	std::string brows;
	for (double side : {-1.0, 1.0}) {
		const double raise = side < 0 ? pose.browRaiseL : pose.browRaiseR, tiltAmt = side < 0 ? pose.browTiltL : pose.browTiltR;
		const Vec3 eyeLocal = onHead(side * 21.0, 6.0, -0.060);
		const double h = kEyeR * wide * 1.45 + 0.03 * raise;
		const double inner = h + 0.045 * tiltAmt, outer = h - 0.045 * tiltAmt;
		const Vec3 a = eyeLocal + Vec3{0.03, inner, -side * 0.075}, b2 = eyeLocal + Vec3{0.03, outer, side * 0.085};
		brows += rod(transformPoint(headM, a * 1.0), transformPoint(headM, b2 * 1.0), 0.016);
	}
	out += attr(coated(c.pupil * 1.2 + Vec3{0.05, 0.04, 0.03}, 0.5), brows);

	// ---- the mouth ----
	{
		const double smile = std::max(-1.0, std::min(1.0, pose.smile)), open = clamp01(pose.mouthOpen);
		std::string lip;
		Vec3 prev = {0, 0, 0};
		for (int k = 0; k <= 10; ++k) {
			const double u = -1.0 + 0.2 * k;
			const double yawDeg = u * (17.0 + 5.0 * std::abs(smile)), pitch = -23.0 + smile * 8.0 * (u * u) - smile * 3.0;
			const Vec3 p = transformPoint(headM, onHead(yawDeg, pitch, 0.006));
			if (k > 0) lip += rod(prev, p, 0.013);
			prev = p;
		}
		out += attr(dark, lip);
		if (open > 0.04) {
			const Vec3 centre = onHead(0.0, -25.0 - 4.0 * open, -0.012);
			out += attr(dark, emitMesh(P.mouth, mul(headM, translate(centre), scale(0.035, 0.018 + 0.085 * open, 0.045 + 0.06 * open))));
			out += attr(tongue, emitMesh(P.mouth, mul(headM, translate(centre + Vec3{-0.002, -0.020 - 0.03 * open, 0.0}), scale(0.026, 0.012 + 0.03 * open, 0.03 + 0.035 * open))));
		}
	}

	// ---- ears and the tuft ----
	{
		std::string ears, tips, tuft;
		for (double side : {-1.0, 1.0}) {
			const Vec3 base = onHead(side * 62.0, 58.0, -0.02);
			const Vec3 radial = normalize(base);
			const Vec3 outward = normalize(Vec3{0.0, -0.25, side * 1.0});
			const double d = clamp01(0.5 + 0.5 * pose.earDroop);   // 0 perked, 0.5 resting, 1 drooping
			const Vec3 dir = normalize(lerp(lerp(radial + Vec3{0.0, 0.3, 0.0}, radial, clamp01(d * 2.0)), outward + radial * 0.3, clamp01(d * 2.0 - 1.0)));
			const double len = 0.30;
			const Vec3 a = transformPoint(headM, base), b = transformPoint(headM, base + dir * len);
			ears += rod(a, b, 0.052);
			tips += emitMesh(P.earTip, mul(translate(b), scale(0.062)));
		}
		out += attr(skin, ears);
		out += attr(feetMat, tips);
		for (int k = 0; k < 3; ++k) {
			const double sway = (k - 1) * 14.0;
			const Vec3 a = transformPoint(headM, onHead(sway, 80.0, -0.01)), b = transformPoint(headM, onHead(sway * 0.5 + 6.0, 62.0, 0.20 + 0.03 * (k == 1)));
			tuft += rod(a, a + (b - a) * 0.5 + Vec3{0.0, 0.10, 0.0}, 0.012);
			tuft += rod(a + (b - a) * 0.5 + Vec3{0.0, 0.10, 0.0}, b, 0.010);
		}
		out += attr(feetMat, tuft);
	}

	// ---- the arms ----
	{
		std::string arms, hands;
		for (double side : {-1.0, 1.0}) {
			const Arm arm = armOf(c, pose, side);
			arms += rod(arm.shoulder, arm.elbow, 0.058 * S) + rod(arm.elbow, arm.hand, 0.052 * S);
			hands += emitMesh(P.hand, mul(translate(arm.hand), scale(0.088 * S)));
		}
		out += attr(skin, arms + hands);
	}

	// ---- the feet ----
	{
		std::string feet;
		for (double side : {-1.0, 1.0}) {
			const double ph = pose.stepPhase + (side < 0 ? 0.0 : kPi);
			const double fwd = 0.17 * pose.stepAmount * std::sin(ph);
			const double up = 0.10 * pose.stepAmount * std::max(0.0, std::cos(ph));
			// feet stay under the body when it hops: they trail it a little
			const Vec3 f = {0.05 + fwd, 0.055 + up, side * 0.15};
			feet += emitMesh(P.foot, mul(root, rotate({0, 0, 1}, -(10.0 * pose.stepAmount * std::cos(ph))), translate(f * S), scale(0.16 * S, 0.062 * S, 0.105 * S)));
		}
		out += attr(feetMat, feet);
	}
	return out;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// Little helpers for acting
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// A natural blink: about every three seconds (a little different for each `seed`), closing and opening in a sixth of a second.
inline double autoBlink(double t, int seed = 0) {
	const double period = 3.1 + 1.3 * hash01(seed, 91);
	const double phase = std::fmod(t + 2.7 * hash01(seed, 92) * period, period);
	const double len = 0.17;
	if (phase > len) return 0.0;
	const double u = phase / len;
	return u < 0.4 ? u / 0.4 : 1.0 - (u - 0.4) / 0.6;
}
// Breathing: a slight swell of the body, about every three seconds.
inline double breathe(double t, double amount = 0.012) { return 1.0 + amount * std::sin(2.0 * kPi * t / 3.0); }

}  // namespace actor
}  // namespace cartoon
