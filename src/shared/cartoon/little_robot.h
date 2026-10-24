#pragma once
// little_robot.h -- "The Little Robot", a 32 second cartoon in five shots: a small robot lies switched off in a junkyard at dusk; his antenna flickers and his eyes light up; he sits up,
// looks at his hands, rolls across the yard and finds a seedling dying in a tin can. He fetches a rusty watering can and waters it, and the seedling grows and opens into a glowing
// flower that lights his face. A crane shot lifts away into the sunset.
//
// Shots (hard cuts): wide establishing (0 - 6.5 s), medium on the waking robot (6.5 - 13), tracking as he rolls and discovers the seedling (13 - 21), close on the watering and the
// bloom (21 - 28.5), wide crane up (28.5 - 32). The story space is +x to the right of the picture, +y up, +z towards the camera (pbrt is left-handed: every emitted x is negated).
//
// The rig: a body, a head that pitches and yaws on a neck, two eyes (an iris of light in a dark glass ball), an antenna with a bulb, treads, and two arms of two links each whose hands
// are placed in the world and solved by limbIk (cartoon_lib.h). The seedling's growth, wilt and bloom, the watering can that follows the hand, and the water drops (analytic arcs) are
// all functions of time.

#include "cartoon_lib.h"

namespace cartoon {
namespace little_robot {

constexpr double kDuration = 32.0;
constexpr int kFps = 24;
constexpr int kSamples = 160;   // recommended samples per pixel (the Sampler line of the film's header)

constexpr Vec3 kSeedling = {2.4, 0.0, 0.35};     // the tin can's centre on the ground
constexpr double kCanTop = 0.42;                  // the soil's height
constexpr Vec3 kWateringCanRest = {1.9, 0.0, 0.95};
constexpr double kStandX = 1.5;                  // where the robot stops beside the seedling

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// Time helpers
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
inline double fade(double t) { return fadeInOut(t, 1.4, 1.8, kDuration); }

inline double robotX(double t) {
	return Track1::withEases({{0.0, -2.4, Ease::Smooth}, {12.6, -2.4, Ease::Smooth}, {17.0, 0.9, Ease::Smooth}, {18.0, 0.9, Ease::Smooth}, {20.6, kStandX, Ease::Smooth}, {40.0, kStandX, Ease::Smooth}})(t);
}
inline double robotYaw(double t) {   // degrees about +y: 0 faces +x; positive turns the front towards the camera (+z)
	return Track1::withEases({{0.0, 0.0, Ease::Smooth}, {6.0, 0.0, Ease::Smooth}, {9.0, 28.0, Ease::Smooth}, {12.4, 18.0, Ease::Smooth}, {17.0, 0.0, Ease::Smooth}, {22.0, 22.0, Ease::Smooth}, {28.6, 34.0, Ease::Smooth}})(t);
}
inline double slump(double t) {   // 1 = switched off and slumped, 0 = upright
	return 1.0 - Track1::withEases({{6.0, 0.0, Ease::Smooth}, {8.6, 1.0, Ease::Back}})(t);
}
inline double kneel(double t) {   // 1 = lowered to look at the seedling
	return Track1::withEases({{19.0, 0.0, Ease::Smooth}, {20.8, 1.0, Ease::Smooth}, {21.6, 1.0, Ease::Smooth}, {22.6, 0.55, Ease::Smooth}, {28.0, 0.55, Ease::Smooth}, {28.8, 0.0, Ease::Smooth}})(t);
}
inline double eyeGlow(double t) {
	// off, then a flicker and a double blink on at 5.4 - 6.4
	return Track1::withEases({{0.0, 0.0, Ease::Linear}, {5.4, 0.0, Ease::Linear}, {5.5, 0.9, Ease::Linear}, {5.62, 0.0, Ease::Linear}, {5.8, 0.0, Ease::Linear}, {5.9, 1.0, Ease::Linear},
							  {20.9, 1.0, Ease::Smooth}, {21.8, 0.45, Ease::Smooth}, {23.4, 0.45, Ease::Smooth}, {24.6, 1.0, Ease::Smooth}})(t);
}
inline double eyeWide(double t) {   // eye size: widens at the discovery
	return 1.0 + 0.45 * Track1::withEases({{17.0, 0.0, Ease::Smooth}, {17.3, 1.0, Ease::Out}, {18.6, 1.0, Ease::Smooth}, {19.4, 0.0, Ease::Smooth}})(t) +
		   0.3 * Track1::withEases({{27.6, 0.0, Ease::Smooth}, {28.2, 1.0, Ease::Out}, {30.0, 1.0, Ease::Smooth}, {31.0, 0.0, Ease::Smooth}})(t);
}
inline double antennaGlow(double t) {
	const double flick = Track1::withEases({{4.4, 0.0, Ease::Linear}, {4.5, 1.0, Ease::Linear}, {4.65, 0.0, Ease::Linear}, {4.9, 0.0, Ease::Linear}, {5.0, 1.0, Ease::Linear}, {5.1, 0.2, Ease::Linear},
											{5.3, 1.0, Ease::Linear}})(t);
	return t < 4.4 ? 0.0 : (flick * (0.7 + 0.3 * std::sin(t * 6.0)));
}

// The pose of the head: pitch (degrees, positive looks down) and yaw (positive towards the camera).
inline void headPose(double t, double &pitch, double &yaw) {
	const double sl = slump(t);
	pitch = lerp(4.0, 58.0, sl);
	yaw = 0.0;
	// looks around while waking up (8.2 - 11.2): left, right, back
	yaw += Track1::withEases({{8.0, 0.0, Ease::Smooth}, {8.8, 38.0, Ease::Smooth}, {9.8, -34.0, Ease::Smooth}, {10.8, 8.0, Ease::Smooth}, {11.4, 0.0, Ease::Smooth}})(t);
	// down at his hands (9.4 - 10.6)
	pitch += Track1::withEases({{9.2, 0.0, Ease::Smooth}, {9.8, 24.0, Ease::Smooth}, {10.7, 24.0, Ease::Smooth}, {11.3, 0.0, Ease::Smooth}})(t);
	// scanning while he rolls (13.5 - 16.6), the double take (16.8 - 18.2)
	yaw += Track1::withEases({{13.4, 0.0, Ease::Smooth}, {14.4, -26.0, Ease::Smooth}, {15.6, 20.0, Ease::Smooth}, {16.6, 0.0, Ease::Smooth}})(t);
	pitch += Track1::withEases({{16.7, 0.0, Ease::Smooth}, {17.0, -14.0, Ease::Out}, {17.9, 12.0, Ease::Smooth}, {18.6, 6.0, Ease::Smooth}})(t);
	// looks down at the seedling (19.4 - 22.6), sadly (21.6 - 23.2), then at the can and the flower
	pitch += Track1::withEases({{19.4, 0.0, Ease::Smooth}, {20.6, 22.0, Ease::Smooth}, {22.0, 34.0, Ease::Smooth}, {23.2, 8.0, Ease::Smooth}, {24.0, 18.0, Ease::Smooth}, {28.0, 18.0, Ease::Smooth},
							   {28.9, -4.0, Ease::Smooth}})(t);
	yaw += Track1::withEases({{22.6, 0.0, Ease::Smooth}, {23.0, 18.0, Ease::Smooth}, {23.6, 0.0, Ease::Smooth}})(t);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// Meshes, built once
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
struct Parts {
	Mesh body, head, tread, neck, eyeBall, iris, bulb, antenna, arm, joint, claw, tire, crate, crateSmall, can, canBody, spout, soilDisc, stem, leaf, petal, drop, rock, hill, pole;
};
inline const Parts &parts() {
	static const Parts p = [] {
		Parts q;
		q.body = roundedBox(0.80, 0.68, 0.62, 0.14, 5);
		q.head = roundedBox(0.72, 0.46, 0.60, 0.15, 5);
		q.tread = roundedBox(1.00, 0.28, 0.20, 0.10, 4);
		q.neck = capsule(0.07, 0.10, 16, 4);
		q.eyeBall = uvSphere(1.0, 28, 14);
		q.iris = uvSphere(1.0, 24, 12);
		q.bulb = uvSphere(1.0, 20, 10);
		q.antenna = capsule(0.018, 0.24, 12, 3);
		q.arm = capsule(0.055, 1.0, 16, 4);
		q.joint = uvSphere(1.0, 18, 9);
		q.claw = roundedBox(0.05, 0.05, 0.16, 0.02, 2);
		q.tire = torus(0.62, 0.26, 44, 16);
		q.crate = roundedBox(0.9, 0.7, 0.9, 0.05, 3);
		q.crateSmall = roundedBox(0.55, 0.45, 0.55, 0.04, 3);
		q.can = lathe({{0.0, 0.0}, {0.27, 0.0}, {0.28, 0.02}, {0.28, 0.40}, {0.295, 0.42}, {0.295, 0.45}, {0.265, 0.45}, {0.265, 0.425}}, 36);
		q.canBody = lathe({{0.0, 0.0}, {0.12, 0.0}, {0.14, 0.02}, {0.14, 0.26}, {0.12, 0.30}, {0.0, 0.30}}, 28);
		q.spout = capsule(0.025, 0.30, 12, 3);
		q.soilDisc = lathe({{0.0, 0.0}, {0.255, 0.0}}, 28);
		q.stem = lathe({{0.0, 0.0}, {0.026, 0.0}, {0.020, 0.5}, {0.012, 1.0}, {0.0, 1.02}}, 12);
		q.leaf = uvSphere(1.0, 20, 10);
		q.petal = uvSphere(1.0, 20, 10);
		q.drop = uvSphere(1.0, 12, 6);
		q.rock = uvSphere(1.0, 18, 9);
		q.hill = lathe({{0.0, 0.0}, {1.0, 0.0}, {0.55, 0.35}, {0.25, 0.7}, {0.0, 1.0}}, 28);
		q.pole = capsule(0.04, 1.0, 10, 3);
		return q;
	}();
	return p;
}


// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The robot
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
struct RobotPose {
	Mat4 world;                 // the robot's frame on the ground
	Vec3 rightHand, leftHand;   // in world space (the hands the story wants)
	Vec3 leaf;                  // the seedling's top leaf, where a hand can touch it
};

inline Mat4 rotY(double deg) { return rotate({0.0, 1.0, 0.0}, deg); }
inline Mat4 rotZ(double deg) { return rotate({0.0, 0.0, 1.0}, deg); }

inline double growth(double t) { return smoothstep(24.8, 28.0, t); }
inline double bloom(double t) { return smoothstep(26.6, 29.0, t); }
inline double wilt(double t) { return 1.0 - smoothstep(24.6, 27.0, t); }

inline Vec3 canHandle(double t, Vec3 rightHand) {   // where the can is: on the ground, then in the right hand
	const double held = smoothstep(22.7, 23.3, t) * (1.0 - smoothstep(28.0, 28.9, t));
	const Vec3 putDown = {kWateringCanRest.x - 0.55, 0.0, kWateringCanRest.z + 0.25};
	const Vec3 rest = lerp(kWateringCanRest, putDown, smoothstep(28.0, 28.9, t));
	return lerp(rest, rightHand + Vec3{0.12, -0.12, 0.04}, held);
}
inline double canTilt(double t) {   // degrees, pouring
	return Track1::withEases({{24.0, 0.0, Ease::Smooth}, {24.9, 62.0, Ease::Smooth}, {27.6, 62.0, Ease::Smooth}, {28.4, 0.0, Ease::Smooth}})(t);
}

struct Hands {
	Vec3 right, left;
};
inline Hands handTargets(double t, const Mat4 &world, double kneeling) {
	auto local = [&](double x, double y, double z) { return transformPoint(world, {x, y, z}); };
	// resting: hanging at the sides
	Vec3 r = local(0.10, 0.45 - 0.25 * kneeling, 0.62), l = local(0.10, 0.45 - 0.25 * kneeling, -0.62);
	// flexing and looking at them (9.0 - 11.0)
	const double flex = Track1::withEases({{8.8, 0.0, Ease::Smooth}, {9.5, 1.0, Ease::Smooth}, {10.6, 1.0, Ease::Smooth}, {11.3, 0.0, Ease::Smooth}})(t);
	r = lerp(r, local(0.55, 1.05, 0.28), flex);
	l = lerp(l, local(0.55, 1.05, -0.28), flex);
	// the left hand reaches the leaf (20.0 - 21.7), then trembles
	const double touch = Track1::withEases({{19.8, 0.0, Ease::Smooth}, {20.7, 1.0, Ease::Smooth}, {21.5, 1.0, Ease::Smooth}, {22.4, 0.0, Ease::Smooth}})(t);
	const Vec3 leaf = {kSeedling.x - 0.12, kCanTop + 0.25, kSeedling.z + 0.02};
	l = lerp(l, leaf, touch);
	// the right hand takes the can (22.2 - 23.2), holds it over the seedling (23.4 - 28.0), puts it down
	const Vec3 canGrip = kWateringCanRest + Vec3{0.0, 0.28, 0.0};
	const double reachCan = Track1::withEases({{22.2, 0.0, Ease::Smooth}, {22.9, 1.0, Ease::Smooth}, {23.3, 1.0, Ease::Smooth}})(t);
	r = lerp(r, canGrip, reachCan);
	const Vec3 overSeedling = {kSeedling.x - 0.50, 1.50, kSeedling.z + 0.16};
	const double lift = Track1::withEases({{23.3, 0.0, Ease::Smooth}, {24.2, 1.0, Ease::Smooth}, {28.0, 1.0, Ease::Smooth}, {28.9, 0.0, Ease::Smooth}})(t);
	r = lerp(r, overSeedling, lift);
	// joy: both hands up (29.0 - 31.0)
	const double joy = Track1::withEases({{28.9, 0.0, Ease::Smooth}, {29.6, 1.0, Ease::Back}, {31.2, 1.0, Ease::Smooth}})(t);
	r = lerp(r, local(0.30, 1.75, 0.55), joy);
	l = lerp(l, local(0.30, 1.75, -0.55), joy);
	return {r, l};
}

inline std::string limbText(const Vec3 &shoulder, const Vec3 &target, double l1, double l2, Vec3 pole, const Mat4 &world, const Parts &P, const std::string &metal, const std::string &dark) {
	const Limb limb = limbIk(shoulder, target, l1, l2, pole);
	std::string out;
	auto seg = [&](Vec3 a, Vec3 b) {
		const Vec3 v = b - a;
		return emitMesh(P.arm, mul(world, alignY(a, v), scale(1.0, length(v), 1.0)));
	};
	out += attr(metal, seg(shoulder, limb.elbow) + seg(limb.elbow, limb.hand));
	out += attr(dark, emitMesh(P.joint, mul(world, translate(shoulder), scale(0.08))) + emitMesh(P.joint, mul(world, translate(limb.elbow), scale(0.07))) +
					  emitMesh(P.joint, mul(world, translate(limb.hand), scale(0.075))));
	// the claw: two small fingers at the hand, pointing along the forearm
	const Vec3 fdir = normalize(limb.hand - limb.elbow);
	const Mat4 hand = mul(world, alignY(limb.hand, fdir));
	out += attr(dark, emitMesh(P.claw, mul(hand, translate(0.045, 0.10, 0.0), rotate({0, 0, 1}, 0.0), rotate({1, 0, 0}, 90.0))) +
					  emitMesh(P.claw, mul(hand, translate(-0.045, 0.10, 0.0), rotate({1, 0, 0}, 90.0))));
	return out;
}

inline std::string buildRobot(double t, double f, const std::string &canText, Vec3 &outRightHand, Vec3 &outLeftHand, double &outEyeGlow) {
	const Parts &P = parts();
	const double sl = slump(t), kn = kneel(t);
	const double yaw = robotYaw(t);
	const double rx = robotX(t);
	// rolling: a bob while the treads turn
	const double rolling = smoothstep(12.6, 13.4, t) * (1.0 - smoothstep(16.6, 17.2, t)) + smoothstep(17.9, 18.4, t) * (1.0 - smoothstep(20.4, 20.9, t));
	const double bob = 0.012 * rolling * std::sin(t * 17.0);
	const Mat4 world = mul(translate(rx, 0.0, 0.0), rotY(yaw));
	const double torsoTilt = lerp(0.0, -14.0, sl) + 22.0 * kn;   // about +z: positive tips the front down
	const double drop = 0.27 * kn;
	const Mat4 torso = mul(world, translate(0.0, 0.34 + bob - drop, 0.0), rotZ(-torsoTilt), translate(0.0, 0.34, 0.0));
	double headPitch, headYaw;
	headPose(t, headPitch, headYaw);
	const Mat4 headFrame = mul(torso, translate(0.0, 0.40, 0.0), rotY(headYaw), rotZ(-headPitch), translate(0.0, 0.22, 0.0));

	const std::string bodyMat = coated({0.16, 0.50, 0.62}, 0.35), headMat = coated({0.86, 0.86, 0.80}, 0.28), darkMat = coated({0.18, 0.19, 0.22}, 0.5), glassMat = coated({0.02, 0.02, 0.03}, 0.04);
	const double glow = eyeGlow(t);
	outEyeGlow = glow;
	std::string out;
	// treads
	out += attr(coated({0.30, 0.32, 0.36}, 0.5), emitMesh(P.tread, mul(world, translate(0.0, 0.14 + bob, 0.38))) + emitMesh(P.tread, mul(world, translate(0.0, 0.14 + bob, -0.38))));
	// body, neck, head
	out += attr(bodyMat, emitMesh(P.body, mul(torso, translate(0.0, 0.34, 0.0))));
	out += attr(darkMat, emitMesh(P.neck, mul(torso, translate(0.0, 0.62, 0.0))));
	out += attr(headMat, emitMesh(P.head, headFrame));
	// eyes: dark glass balls with an iris of light
	const double wide = eyeWide(t);
	const double blink = 1.0 - 0.9 * Track1::withEases({{12.2, 0.0, Ease::Linear}, {12.3, 1.0, Ease::Linear}, {12.45, 0.0, Ease::Linear}})(t) -
						 0.9 * Track1::withEases({{26.0, 0.0, Ease::Linear}, {26.1, 1.0, Ease::Linear}, {26.25, 0.0, Ease::Linear}})(t);
	for (int side = -1; side <= 1; side += 2) {
		const Mat4 eye = mul(headFrame, translate(0.31, 0.02, 0.165 * side), scale(0.5, 1.0 * blink, 1.0), scale(0.15 * wide));
		out += attr(glassMat, emitMesh(P.eyeBall, eye));
		if (glow > 0.01) {
			const Vec3 L = {0.35 * 9.0 * glow * f, 0.95 * 9.0 * glow * f, 1.0 * 9.0 * glow * f};
			out += attr("Material \"diffuse\" \"rgb reflectance\" [ 0 0 0 ]", emitMesh(P.iris, mul(headFrame, translate(0.355, 0.02, 0.165 * side), scale(0.5, 1.0 * blink, 1.0), scale(0.085 * wide))), &L);
		}
	}
	// antenna and its bulb
	out += attr(darkMat, emitMesh(P.antenna, mul(headFrame, translate(-0.05, 0.22, 0.0))));
	const double ag = antennaGlow(t);
	if (ag > 0.02) {
		const Vec3 L = {1.0 * 14.0 * ag * f, 0.62 * 14.0 * ag * f, 0.2 * 14.0 * ag * f};
		out += emissiveSphere(transformPoint(headFrame, {-0.05, 0.50, 0.0}), 0.055, L);
	} else {
		out += attr(coated({0.5, 0.35, 0.1}, 0.1), emitMesh(P.bulb, mul(headFrame, translate(-0.05, 0.50, 0.0), scale(0.05))));
	}
	// arms: shoulders on the torso, hands where the story puts them
	const Hands hands = handTargets(t, world, kn);
	outRightHand = hands.right;
	outLeftHand = hands.left;
	const Mat4 worldInv = invertAffine(world);
	const Mat4 torsoToWorld = torso;
	for (int side = -1; side <= 1; side += 2) {
		const Vec3 shoulderWorld = transformPoint(torsoToWorld, {0.0, 0.62, 0.43 * side});
		const Vec3 targetWorld = side > 0 ? hands.right : hands.left;
		// hanging arms hang: when the hand rests, the target follows the shoulder
		const Vec3 shoulder = transformPoint(worldInv, shoulderWorld), target = transformPoint(worldInv, targetWorld);
		out += limbText(shoulder, target, 0.42, 0.38, {-0.2, -0.6, static_cast<double>(side)}, world, P, bodyMat, darkMat);
	}
	out += canText;
	return out;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The seedling, the watering can and the water
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
inline std::string buildSeedling(double t, double f, Vec3 &outFlowerCentre, double &outFlowerGlow) {
	const Parts &P = parts();
	const double g = growth(t), b = bloom(t), w = wilt(t);
	std::string out;
	out += attr(coated({0.55, 0.30, 0.20}, 0.5), emitMesh(P.can, translate(kSeedling)));
	out += attr(diffuseMaterial({0.16, 0.10, 0.07}), emitMesh(P.soilDisc, translate(kSeedling + Vec3{0.0, kCanTop, 0.0})));
	const double height = 0.16 + 0.95 * g;
	const double lean = 14.0 * w + 3.0;   // wilted: leans over
	const Vec3 base = kSeedling + Vec3{0.0, kCanTop, 0.0};
	const Mat4 stemFrame = mul(translate(base), rotZ(-lean), scale(1.0, height, 1.0));
	const std::string green = coated({0.22, 0.62, 0.20}, 0.3), darkGreen = coated({0.12, 0.40, 0.16}, 0.3);
	out += attr(green, emitMesh(P.stem, stemFrame));
	const Vec3 top = transformPoint(mul(translate(base), rotZ(-lean)), {0.0, height, 0.0});
	// leaves: small and drooping when wilted, springing up as it grows
	const double leafSize = 0.07 + 0.17 * g;
	for (int k = 0; k < 2; ++k) {
		const double side = k == 0 ? 1.0 : -1.0;
		const double hy = height * (k == 0 ? 0.55 : 0.78);
		const double droop = lerp(-20.0, 55.0, w);   // degrees below horizontal when wilted
		const Vec3 at = transformPoint(mul(translate(base), rotZ(-lean)), {0.0, hy, 0.0});
		out += attr(darkGreen, emitMesh(P.leaf, mul(translate(at), rotY(side > 0 ? 20.0 : 200.0), rotZ(-droop), translate(leafSize * 0.9, 0.0, 0.0), scale(leafSize, leafSize * 0.14, leafSize * 0.45))));
	}
	// the bud and the flower
	outFlowerCentre = top + Vec3{0.0, 0.03, 0.0};
	outFlowerGlow = 0.0;
	if (g > 0.55) {
		const double bud = smoothstep(0.55, 1.0, g);
		const int petals = 9;
		const std::string petalMat = coated({1.0, 0.45, 0.62}, 0.2);
		for (int k = 0; k < petals; ++k) {
			const double around = 360.0 * k / petals;
			const double open = lerp(8.0, 74.0, b);   // degrees out from the stem
			const double len = (0.10 + 0.20 * b) * bud;
			out += attr(petalMat, emitMesh(P.petal, mul(translate(outFlowerCentre), rotY(around), rotZ(-open), translate(len * 0.95, 0.0, 0.0), scale(len, len * 0.30, len * 0.34))));
		}
		outFlowerGlow = b;
		const Vec3 L = {(1.0 * 16.0 * b + 0.2) * f, (0.62 * 16.0 * b + 0.1) * f, (0.28 * 16.0 * b + 0.05) * f};
		out += emissiveSphere(outFlowerCentre + Vec3{0.0, 0.02, 0.0}, 0.045 + 0.05 * b, L);
	}
	return out;
}

inline std::string buildWateringCan(double t, Vec3 rightHand, Vec3 &outSpoutTip) {
	const Parts &P = parts();
	const Vec3 pos = canHandle(t, rightHand);
	const double tilt = canTilt(t);
	const Mat4 frame = mul(translate(pos), rotZ(-tilt));   // the can pivots about its handle, spout towards +x ... after the mirror it faces the seedling
	const std::string tin = coated({0.62, 0.36, 0.22}, 0.45);
	std::string out = attr(tin, emitMesh(P.canBody, mul(frame, translate(0.0, 0.0, 0.0))) + emitMesh(P.spout, mul(frame, translate(0.13, 0.12, 0.0), rotZ(-52.0), scale(1.0, 1.0, 1.0))) +
								emitMesh(P.joint, mul(frame, translate(-0.17, 0.2, 0.0), scale(0.045))));
	outSpoutTip = transformPoint(frame, transformPoint(mul(translate(0.13, 0.12, 0.0), rotZ(-52.0)), {0.0, 0.30, 0.0}));
	return out;
}

inline std::string buildWater(double t, Vec3 spoutTip) {
	const Parts &P = parts();
	std::string out;
	if (t < 24.6 || t > 28.4) return out;
	const double gravity = 9.8;
	const Vec3 target = kSeedling + Vec3{0.0, kCanTop + 0.02, 0.02};
	for (int i = 0; i < 60; ++i) {
		const double spawn = 24.8 + i * 0.045;
		const double age = t - spawn;
		if (age < 0.0 || spawn > 27.7) continue;
		// from where the spout was when the drop left it (the tip is nearly still while pouring): a short arc
		const double vx = 0.7, vy = 0.2;
		const Vec3 p = spoutTip + Vec3{vx * age + 0.02 * std::sin(i * 2.1), vy * age - 0.5 * gravity * age * age, 0.02 * std::cos(i * 1.7)};
		if (p.y < target.y) continue;
		out += attr("Material \"dielectric\" \"float eta\" [ 1.33 ]", emitMesh(P.drop, mul(translate(p), scale(0.022, 0.03, 0.022))));
	}
	return out;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The junkyard
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
inline std::string buildYard() {
	const Parts &P = parts();
	std::string out;
	out += attr(coated({0.30, 0.26, 0.22}, 0.55), emitMesh(box(80.0, 1.0, 60.0), translate(0.0, -0.5, -10.0)));
	// a pale patch of swept ground where the story happens
	out += attr(coated({0.52, 0.42, 0.30}, 0.6), emitMesh(lathe({{0.0, 0.0}, {6.5, 0.0}}, 48), translate(0.5, 0.004, 0.0)));
	// the tire stack the robot leans on
	const std::string rubber = coated({0.08, 0.08, 0.09}, 0.7);
	for (int i = 0; i < 3; ++i) out += attr(rubber, emitMesh(P.tire, mul(translate(-4.1 + 0.05 * i, 0.26 + 0.52 * i, -0.4), rotY(17.0 * i))));
	out += attr(rubber, emitMesh(P.tire, mul(translate(-5.6, 0.26, 0.8), rotY(40.0))));
	// crates and junk
	const std::string wood = coated({0.50, 0.36, 0.22}, 0.5), rust = coated({0.55, 0.30, 0.16}, 0.5), grey = coated({0.35, 0.38, 0.40}, 0.45);
	out += attr(wood, emitMesh(P.crate, mul(translate(-1.0, 0.35, -3.6), rotY(12.0))) + emitMesh(P.crate, mul(translate(0.2, 0.35, -4.2), rotY(-20.0))) +
					  emitMesh(P.crateSmall, mul(translate(-0.4, 1.0, -3.9), rotY(35.0))));
	out += attr(rust, emitMesh(P.crate, mul(translate(4.8, 0.35, -2.4), rotY(-8.0))) + emitMesh(P.crateSmall, mul(translate(4.9, 0.22, -1.2), rotY(30.0))));
	out += attr(grey, emitMesh(P.crateSmall, mul(translate(-2.6, 0.22, -2.2), rotY(55.0))) + emitMesh(P.crateSmall, mul(translate(6.4, 0.22, 1.2), rotY(15.0))));
	for (int i = 0; i < 7; ++i)
		out += attr(coated({0.30 + 0.04 * i, 0.28, 0.25}, 0.7), emitMesh(P.rock, mul(translate(-6.0 + 2.1 * i, 0.12, 2.4 + 0.5 * (i % 3)), scale(0.22 + 0.04 * (i % 3), 0.12, 0.2))));
	// a bent pole and, far away, dark hills against the sunset
	out += attr(grey, emitMesh(P.pole, mul(translate(-6.8, 0.0, -4.4), rotZ(-8.0), scale(1.0, 4.0, 1.0))));
	const std::string hill = diffuseMaterial({0.13, 0.12, 0.17});
	for (int i = 0; i < 6; ++i)
		out += attr(hill, emitMesh(P.hill, mul(translate(-16.0 + 7.0 * i, 0.0, -24.0 - 2.0 * (i % 2)), scale(5.5 + (i % 3) * 1.5, 4.0 + (i % 4) * 1.2, 5.5 + (i % 3)))));
	return out;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// Camera shots
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
inline const std::vector<Shot> &shots() {
	static const std::vector<Shot> s = {
		{0.0, Track3::withEases({{0.0, {3.4, 3.0, 9.4}, Ease::Smooth}, {6.5, {1.2, 2.0, 7.6}, Ease::Smooth}}), Track3::withEases({{0.0, {-1.4, 1.2, 0.0}, Ease::Smooth}, {6.5, {-2.0, 0.9, 0.0}, Ease::Smooth}}),
		 Track1::withEases({{0.0, 38.0, Ease::Smooth}}), 0.02},
		{6.5, Track3::withEases({{6.5, {0.4, 1.7, 5.4}, Ease::Smooth}, {13.0, {-0.6, 1.5, 4.7}, Ease::Smooth}}), Track3::withEases({{6.5, {-2.3, 0.9, 0.0}, Ease::Smooth}, {13.0, {-2.3, 0.95, 0.0}, Ease::Smooth}}),
		 Track1::withEases({{6.5, 34.0, Ease::Smooth}}), 0.03},
		{13.0, Track3::withEases({{13.0, {-1.8, 1.4, 6.6}, Ease::Smooth}, {21.0, {1.0, 1.5, 6.4}, Ease::Smooth}}), Track3::withEases({{13.0, {-1.0, 0.9, 0.0}, Ease::Smooth}, {21.0, {2.3, 0.85, 0.3}, Ease::Smooth}}),
		 Track1::withEases({{13.0, 32.0, Ease::Smooth}}), 0.03},
		{21.0, Track3::withEases({{21.0, {2.0, 1.0, 3.4}, Ease::Smooth}, {28.5, {2.5, 1.35, 2.6}, Ease::Smooth}}), Track3::withEases({{21.0, {2.5, 0.75, 0.4}, Ease::Smooth}, {28.5, {2.4, 1.0, 0.4}, Ease::Smooth}}),
		 Track1::withEases({{21.0, 32.0, Ease::Smooth}}), 0.035},
		{28.5, Track3::withEases({{28.5, {1.0, 1.2, 8.2}, Ease::Smooth}, {32.0, {1.4, 4.6, 12.0}, Ease::Smooth}}), Track3::withEases({{28.5, {1.8, 0.9, 0.0}, Ease::Smooth}, {32.0, {1.8, 1.6, 0.0}, Ease::Smooth}}),
		 Track1::withEases({{28.5, 38.0, Ease::Smooth}}), 0.02},
	};
	return s;
}

// Everything up to and including WorldBegin: the camera at t, the picture's size, the sampler and the integrator. Cheap, and all the scene list
// needs to describe the film's still.
inline std::string headerAt(double t) { return sceneHeader(cameraText(cameraAt(shots(), t)), kSamples); }
inline std::vector<double> shotStarts() { return startsOf(shots()); }

inline std::string sceneAt(double t) {
	const double f = fade(t);
	std::string out = headerAt(t);
	// dusk: a warm sky, and a low sun that sinks and reddens over the film
	const double sunset = smoothstep(0.0, kDuration, t);
	char buf[300];
	std::snprintf(buf, sizeof(buf), "LightSource \"infinite\" \"rgb L\" [ %.4f %.4f %.4f ]\n", f * lerp(0.55, 0.28, sunset), f * lerp(0.36, 0.14, sunset), f * lerp(0.30, 0.16, sunset));
	out += buf;
	const Vec3 sun = {f * lerp(26.0, 30.0, sunset), f * lerp(16.0, 9.0, sunset), f * lerp(8.0, 3.0, sunset)};
	const double sunY = lerp(7.0, 3.0, sunset);
	out += attr(diffuseMaterial({0.1, 0.1, 0.1}), emitMesh(box(14.0, 0.2, 7.0), mul(translate(-14.0, sunY, 9.0), rotate({0, 0, 1}, -32.0), rotate({0, 1, 0}, 35.0))), &sun);
	out += buildYard();
	Vec3 flowerCentre, rightHand, leftHand, spoutTip;
	double flowerGlow = 0.0, eyeG = 0.0;
	std::string seedling = buildSeedling(t, f, flowerCentre, flowerGlow);
	// the can follows the right hand, so the robot is built first to learn where the hand is
	std::string dummyCan;
	const std::string robotNoCan = buildRobot(t, f, dummyCan, rightHand, leftHand, eyeG);
	const std::string can = buildWateringCan(t, rightHand, spoutTip);
	out += robotNoCan + can + seedling + buildWater(t, spoutTip);
	return out;
}

}  // namespace little_robot
}  // namespace cartoon
