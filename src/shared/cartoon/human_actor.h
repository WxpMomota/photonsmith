#pragma once
// human_actor.h -- a cartoon person for the films (an adult, about 1.8 m): shoes, trousers and a sweater with ribbed cuffs, two arms that reach for a point
// in the world, two legs that walk, a body that bends at the hips and crouches at the knees, and a round head whose eyes look, blink and widen, with brows,
// a nose, a mouth that smiles or opens, ears and hair. A film fills in a `HumanPose` for each instant and asks for `emitHuman(pose)`, as for Pip and the
// turtle; props in the hands (keys, grocery bags) are part of the pose.
//
// Units are metres. The rig's own frame has +x forward, +y up and +z to the person's right; `pose.yaw` turns them about the vertical (0 faces +x).

#include "cartoon_actor.h"

namespace cartoon {
namespace human {

struct Outfit {
	Vec3 skin = {0.86, 0.64, 0.50};
	Vec3 hair = {0.20, 0.12, 0.07};
	Vec3 sweater = {0.62, 0.30, 0.22};
	Vec3 trousers = {0.20, 0.23, 0.32};
	Vec3 shoes = {0.14, 0.11, 0.09};
	Vec3 eyes = {0.25, 0.16, 0.08};
};

struct HumanPose {
	Vec3 pos = {0.0, 0.0, 0.0};   // on the floor, between the feet
	double yaw = 0.0;
	double lean = 0.0;            // forward bend at the hips, degrees
	double crouch = 0.0;          // 0 standing .. 1 squatting
	double walkPhase = 0.0, walkAmount = 0.0;
	double headYaw = 0.0, headPitch = 0.0;   // degrees (positive pitch looks down)
	Vec3 look = {0.0, 0.0, 0.0};
	double lookWeight = 0.0;
	double blink = 0.0, eyeWide = 1.0, browRaise = 0.0, browTilt = 0.0;   // browTilt positive = worried
	double smile = 0.3, mouthOpen = 0.0;
	Vec3 handL = {0, 0, 0}, handR = {0, 0, 0};
	double handWeightL = 0.0, handWeightR = 0.0;
	bool keys = false;            // car keys in the right hand
	bool bags = false;            // a grocery bag in each hand
};

constexpr double kHip = 0.92, kThigh = 0.45, kShin = 0.44, kTorso = 0.55, kHeadR = 0.122;

struct Parts {
	Mesh ball, torso, cap, ring;
};
inline const Parts &parts() {
	static const Parts p = [] {
		Parts q;
		q.ball = uvSphere(1.0, 20, 10);
		// the body from the waist (y 0) to the shoulders (y 1): narrow at the waist, full at the chest, round at the shoulders
		q.torso = lathe({{0.0, -0.04}, {0.80, -0.02}, {0.84, 0.10}, {0.86, 0.40}, {1.0, 0.70}, {0.98, 0.86}, {0.78, 0.98}, {0.36, 1.03}, {0.0, 1.04}}, 40);
		q.cap = actor::parts().lid;
		q.ring = torus(1.0, 0.18, 32, 8);
		return q;
	}();
	return p;
}

struct Frames {
	Mat4 root, pelvis, chest, head;
	double hipY;
};
inline Frames framesOf(const HumanPose &p) {
	Frames f;
	const double bob = 0.025 * p.walkAmount * std::abs(std::cos(p.walkPhase));
	f.hipY = kHip * (1.0 - 0.42 * clamp01(p.crouch)) - bob;
	f.root = mul(translate(p.pos), rotate({0, 1, 0}, -p.yaw));
	const double twist = 5.0 * p.walkAmount * std::sin(p.walkPhase);
	f.pelvis = mul(f.root, translate(0.0, f.hipY, 0.0), rotate({0, 1, 0}, twist));
	f.chest = mul(f.pelvis, rotate({0, 0, 1}, -p.lean), rotate({0, 1, 0}, -2.0 * twist));
	f.head = mul(f.chest, translate(0.0, kTorso + 0.10, 0.0), rotate({0, 1, 0}, -p.headYaw), rotate({0, 0, 1}, -p.headPitch), translate(0.0, kHeadR * 0.95, 0.0));
	return f;
}

// Where a foot is (world) for the walk: a stride forwards and back, lifted as it swings through.
inline Vec3 footOf(const Frames &f, const HumanPose &p, double side) {
	const double ph = p.walkPhase + (side < 0 ? 0.0 : kPi);
	const double fwd = 0.30 * p.walkAmount * std::sin(ph), up = 0.11 * p.walkAmount * std::max(0.0, std::cos(ph));
	return transformPoint(f.root, {fwd + 0.02, 0.07 + up, side * (0.11 + 0.05 * clamp01(p.crouch))});
}

struct Arm {
	Vec3 shoulder, elbow, hand;
};
inline Arm armOf(const Frames &f, const HumanPose &p, double side) {
	const Vec3 shoulder = transformPoint(f.chest, {0.0, kTorso - 0.06, side * 0.205});
	// at rest the arm hangs and swings against the legs while walking
	const double swing = -0.20 * p.walkAmount * std::sin(p.walkPhase + (side < 0 ? 0.0 : kPi));
	const Vec3 rest = transformPoint(f.chest, {0.06 + swing, kTorso - 0.62, side * 0.27});
	const double w = clamp01(side < 0 ? p.handWeightL : p.handWeightR);
	Vec3 target = lerp(rest, side < 0 ? p.handL : p.handR, w);
	const double reach = 0.57;
	if (length(target - shoulder) > reach) target = shoulder + normalize(target - shoulder) * reach;
	const Limb l = limbIk(shoulder, target, 0.30, 0.28, transformPoint(f.chest, {-0.6, -0.5, side * 0.5}) - transformPoint(f.chest, {0.0, 0.0, 0.0}));
	return {shoulder, l.elbow, l.hand};
}

inline Vec3 headCentre(const HumanPose &p) { return transformPoint(framesOf(p).head, {0.0, 0.0, 0.0}); }
inline Vec3 handOf(const HumanPose &p, double side) { return armOf(framesOf(p), p, side).hand; }

// A paper grocery bag standing on `base` (world), turned by `yaw`, with what sticks out of the top: a loaf, a leek, apples.
inline std::string groceryBag(Vec3 base, double yaw, int salt) {
	std::string out;
	const Mat4 m = mul(translate(base), rotate({0, 1, 0}, -yaw));
	out += attr(diffuseMaterial({0.66, 0.50, 0.32}), emitMesh(roundedBox(0.26, 0.34, 0.17, 0.012, 2), mul(m, translate(0.0, 0.17, 0.0))));
	out += attr(diffuseMaterial({0.55, 0.40, 0.25}), emitMesh(roundedBox(0.262, 0.03, 0.172, 0.01, 2), mul(m, translate(0.0, 0.325, 0.0))));   // the folded rim
	out += attr(coated({0.80, 0.56, 0.28}, 0.6), emitMesh(capsule(0.035, 0.30, 12, 4), mul(m, translate(-0.05, 0.22, -0.03), rotate({0, 0, 1}, -14.0 - 6.0 * salt))));   // a baguette
	out += attr(coated({0.30, 0.55, 0.18}, 0.5), emitMesh(capsule(0.016, 0.26, 10, 3), mul(m, translate(0.06, 0.25, 0.04), rotate({1, 0, 0}, 10.0))));   // a leek
	for (int k = 0; k < 3; ++k)
		out += attr(coated(k == 1 ? Vec3{0.85, 0.55, 0.12} : Vec3{0.75, 0.10, 0.08}, 0.25), emitMesh(parts().ball, mul(m, translate(-0.06 + 0.06 * k, 0.33, 0.04 - 0.03 * k), scale(0.038))));
	return out;
}

inline std::string emitHuman(const HumanPose &p, const Outfit &o = Outfit{}) {
	const Parts &P = parts();
	const Frames f = framesOf(p);
	std::string out;
	const std::string skin = coated(o.skin, 0.55), hair = coated(o.hair, 0.6), sweater = diffuseMaterial(o.sweater), rib = diffuseMaterial(o.sweater * 0.82);
	const std::string trousers = diffuseMaterial(o.trousers), shoes = coated(o.shoes, 0.35);

	// ---- legs and shoes ----
	std::string legs, feet;
	for (double side : {-1.0, 1.0}) {
		const Vec3 hip = transformPoint(f.pelvis, {0.0, 0.0, side * 0.10});
		const Vec3 ankle = footOf(f, p, side);
		const Limb l = limbIk(hip, ankle, kThigh, kShin, transformPoint(f.root, {1.0, 0.0, side * 0.1}) - transformPoint(f.root, {0.0, 0.0, 0.0}));
		legs += bar(hip, l.elbow, 0.080, 12) + bar(l.elbow, l.hand, 0.064, 12);
		const Mat4 shoe = mul(translate(l.hand), rotate({0, 1, 0}, -p.yaw), translate(0.06, -0.035, 0.0));
		feet += emitMesh(roundedBox(0.28, 0.085, 0.105, 0.035, 3), shoe);
	}
	out += attr(trousers, legs + emitMesh(P.ball, mul(f.pelvis, translate(0.0, 0.02, 0.0), scale(0.17, 0.13, 0.20))));
	out += attr(shoes, feet);

	// ---- the body ----
	out += attr(sweater, emitMesh(P.torso, mul(f.chest, scale(0.17, kTorso, 0.215))));
	std::string ribs = emitMesh(P.ring, mul(f.chest, translate(0.0, 0.02, 0.0), scale(0.142, 0.06, 0.18)));   // the ribbed waist
	ribs += emitMesh(P.ring, mul(f.chest, translate(0.0, kTorso + 0.025, 0.0), scale(0.060, 0.05, 0.065)));          // the collar
	out += attr(rib, ribs);
	out += attr(skin, bar(transformPoint(f.chest, {0.0, kTorso - 0.02, 0.0}), transformPoint(f.head, {-0.01, -kHeadR * 0.8, 0.0}), 0.048, 12));

	// ---- arms and hands ----
	std::string arms, cuffs, hands;
	for (double side : {-1.0, 1.0}) {
		const Arm a = armOf(f, p, side);
		arms += bar(a.shoulder, a.elbow, 0.058, 12) + bar(a.elbow, lerp(a.elbow, a.hand, 0.86), 0.050, 12);
		const Vec3 wristDir = normalize(a.hand - a.elbow);
		cuffs += emitMesh(P.ring, mul(alignY(lerp(a.elbow, a.hand, 0.84), wristDir), scale(0.047, 0.08, 0.047)));
		const Mat4 hand = alignY(a.hand, wristDir, transformPoint(f.chest, {1.0, 0.0, 0.0}) - transformPoint(f.chest, {0.0, 0.0, 0.0}));
		hands += emitMesh(P.ball, mul(hand, translate(0.0, 0.035, 0.0), scale(0.045, 0.060, 0.022)));
		hands += bar(transformPoint(hand, {0.025, 0.02, side * 0.02}), transformPoint(hand, {0.045, 0.06, side * 0.03}), 0.012, 6);   // the thumb
		if (side > 0 && p.keys) {
			std::string metal = emitMesh(P.ring, mul(hand, translate(0.0, 0.10, 0.0), rotate({1, 0, 0}, 90.0), scale(0.018, 0.02, 0.018)));
			metal += bar(transformPoint(hand, {0.0, 0.11, 0.0}), transformPoint(hand, {0.01, 0.16, 0.012}), 0.004, 6);
			metal += bar(transformPoint(hand, {0.0, 0.11, 0.0}), transformPoint(hand, {-0.012, 0.155, -0.01}), 0.004, 6);
			out += attr(metalMaterial({0.85, 0.80, 0.62}, 0.15), metal);
			out += attr(coated({0.08, 0.08, 0.09}, 0.3), emitMesh(roundedBox(0.03, 0.045, 0.012, 0.005, 2), mul(hand, translate(0.0, 0.13, 0.025))));   // the fob
		}
		if (p.bags) out += groceryBag(a.hand - Vec3{0.0, 0.33, 0.0}, p.yaw + side * 8.0, side < 0 ? 0 : 1);
	}
	out += attr(sweater, arms);
	out += attr(rib, cuffs);
	out += attr(skin, hands);

	// ---- the head ----
	out += attr(skin, emitMesh(P.ball, mul(f.head, scale(kHeadR * 0.98, kHeadR * 1.06, kHeadR * 0.94))));
	std::string ears;
	for (double side : {-1.0, 1.0}) ears += emitMesh(P.ball, mul(f.head, translate(-0.005, -0.005, side * kHeadR * 0.93), scale(0.018, 0.032, 0.012)));
	out += attr(skin, ears);
	out += attr(coated(o.skin * 0.92, 0.5), emitMesh(P.ball, mul(f.head, translate(kHeadR * 0.95, -0.012, 0.0), scale(0.026, 0.030, 0.022))));   // the nose
	// hair: a cap over the top and the back, and a little fringe
	out += attr(hair, emitMesh(P.cap, mul(f.head, translate(-0.012, 0.012, 0.0), rotate({0, 0, 1}, 22.0), scale(kHeadR * 1.0, kHeadR * 1.02, kHeadR * 1.0))) +
						  emitMesh(P.ball, mul(f.head, translate(kHeadR * 0.55, kHeadR * 0.78, 0.02), rotate({1, 0, 0}, 12.0), scale(0.05, 0.022, 0.07))));
	// eyes, lids and brows
	std::string whites, irises, pupils, highlights, lids, brows;
	const double wide = std::max(0.5, p.eyeWide);
	for (double side : {-1.0, 1.0}) {
		const Vec3 c = {kHeadR * 0.82, 0.022, side * 0.042};
		const double R = 0.019 * wide;
		whites += emitMesh(P.ball, mul(f.head, translate(c), scale(R)));
		Vec3 dir = {1.0, 0.0, 0.0};
		if (p.lookWeight > 0.0) {
			Vec3 d = normalize(transformPoint(invertAffine(f.head), p.look) - c);
			d.x = std::max(d.x, 0.5);
			dir = normalize(lerp(dir, normalize(d), clamp01(p.lookWeight)));
		}
		irises += emitMesh(P.ball, mul(f.head, translate(c + dir * (R * 0.55)), scale(R * 0.58)));
		pupils += emitMesh(P.ball, mul(f.head, translate(c + dir * (R * 0.75)), scale(R * 0.36)));
		highlights += emitMesh(P.ball, mul(f.head, translate(c + dir * (R * 0.92) + Vec3{0.0, R * 0.25, -R * 0.2}), scale(R * 0.11)));
		const double b = clamp01(p.blink);
		if (b > 0.02) {
			const double psi = (180.0 - 180.0 * b) * kPi / 180.0;
			lids += emitMesh(P.cap, mul(f.head, alignY(c, Vec3{std::cos(psi), std::sin(psi), 0.0}), scale(R * 1.06)));
		}
		const double h = R * 1.6 + 0.008 * p.browRaise;
		brows += bar(transformPoint(f.head, c + Vec3{0.006, h + 0.006 * p.browTilt, -side * 0.012}), transformPoint(f.head, c + Vec3{0.0, h - 0.004 * p.browTilt, side * 0.020}), 0.0045, 6);
	}
	out += attr(coated({0.96, 0.96, 0.94}, 0.1), whites);
	out += attr(coated(o.eyes, 0.1), irises);
	out += attr(coated({0.02, 0.02, 0.02}, 0.05), pupils);
	out += attr(coated({1.0, 1.0, 1.0}, 0.05), highlights);
	if (!lids.empty()) out += attr(skin, lids);
	out += attr(hair, brows);
	// the mouth
	{
		const double smile = std::max(-1.0, std::min(1.0, p.smile)), open = clamp01(p.mouthOpen);
		std::string lip;
		Vec3 prev{};
		for (int k = 0; k <= 8; ++k) {
			const double u = -1.0 + 0.25 * k;
			const Vec3 q = transformPoint(f.head, actor::onHead(u * 24.0, -24.0 + smile * 9.0 * u * u - smile * 3.0, 0.002, kHeadR * 0.97));
			if (k > 0) lip += bar(prev, q, 0.0042, 5);
			prev = q;
		}
		out += attr(diffuseMaterial({0.45, 0.16, 0.14}), lip);
		if (open > 0.04) out += attr(diffuseMaterial({0.18, 0.04, 0.05}), emitMesh(P.ball, mul(f.head, translate(actor::onHead(0.0, -28.0, -0.008, kHeadR * 0.97)), scale(0.012, 0.010 + 0.025 * open, 0.022 + 0.012 * open))));
	}
	return out;
}

}  // namespace human
}  // namespace cartoon
