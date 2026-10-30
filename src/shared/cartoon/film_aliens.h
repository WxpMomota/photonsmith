#pragma once
// film_aliens.h -- three more kinds of character for a film file (film_file.h `character NAME KIND`; round 11, for films in space), each walking, blinking, speaking,
// feeling (its `feel` lines on its face), looking at what it looks at, acting the same library of gestures as Pip (wave, point, reach, cheer, shrug, think, look up,
// look down, nod, shake head, bow, hop) and holding props:
//
//     blob   a squishy alien a metre tall: a gumdrop of jelly that gets about by hopping - squashing as it lands, stretching as it goes up - its two eyes on stalks that
//            wobble after it, two stubby arms
//     squid  a big-headed alien 1.3 m tall that walks on six tentacles rippling under it, the two in front its arms; the tentacles trail as it moves
//     hoverbot  a little robot 0.9 m tall that hovers on a glowing thruster, its face drawn on a screen - glowing eyes that blink and widen, a mouth that smiles and talks -
//            an antenna with a blinking light, noodle arms with claws
//
// Body space as the other kinds' (film_kinds.h): +x the character's left, +y up, +z forward, at scale 1; the frame carries it into story space.

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "film_kinds.h"
#include "sdf_mesh.h"

namespace cartoon {
namespace film_kinds {
namespace aliens {

// What a moment and its gestures make of one of these: where its hands reach (body space, scale 1) and how much, how its head turns, its lean and lift, and its face.
struct Pose {
	Vec3 handL, handR;
	double weightL = 0.0, weightR = 0.0;
	double headYaw = 0.0, headPitch = 0.0, headRoll = 0.0, lean = 0.0, lift = 0.0;
	double smile = 0.0, browRaise = 0.0, browTilt = 0.0, eyeWide = 0.0, blink = 0.0, mouthOpen = 0.0;
	Vec3 look{0.0, 0.0, 0.0};   // (body space, scale 1)
	double lookWeight = 0.0;
};

// The gestures, at a hand's height `h` (its shoulder) and a reach `r`: the same names as Pip's and a person's, so a line written for one acts on another.
inline rig::PoseLibrary<Pose> poses(double h, double r) {
	rig::PoseLibrary<Pose> lib;
	const auto reach = [](Vec3 &hand, double &weight, Vec3 to, double w) {
		hand = weight + w > 1e-9 ? (hand * weight + to * w) * (1.0 / (weight + w)) : to;
		weight += w;
	};
	lib["wave"] = [=](Pose &p, double w, double since) {
		reach(p.handR, p.weightR, {-1.0 * r, h + 1.1 * r, 0.35 * r + 0.2 * r * wiggle(since, 2.4)}, w);
		p.smile += 0.35 * w;
	};
	lib["point"] = [=](Pose &p, double w, double) { reach(p.handR, p.weightR, {-0.75 * r, h + 0.2 * r, 1.0 * r}, w); };
	lib["reach"] = [=](Pose &p, double w, double) {
		reach(p.handR, p.weightR, {-0.2 * r, h - 0.3 * r, 1.0 * r}, w);
		p.lean += 15.0 * w;
	};
	lib["cheer"] = [=](Pose &p, double w, double) {
		reach(p.handL, p.weightL, {1.0 * r, h + 1.1 * r, 0.4 * r}, w);
		reach(p.handR, p.weightR, {-1.0 * r, h + 1.1 * r, 0.4 * r}, w);
		p.smile += 0.6 * w, p.eyeWide += 0.2 * w;
	};
	lib["shrug"] = [=](Pose &p, double w, double) {
		reach(p.handL, p.weightL, {0.75 * r, h + 0.15 * r, 0.35 * r}, w);
		reach(p.handR, p.weightR, {-0.75 * r, h + 0.15 * r, 0.35 * r}, w);
		p.browRaise += 0.6 * w, p.headRoll += 8.0 * w;
	};
	lib["think"] = [=](Pose &p, double w, double) {
		reach(p.handR, p.weightR, {-0.15 * r, h + 0.45 * r, 0.6 * r}, w);
		p.headRoll += 10.0 * w, p.headPitch -= 8.0 * w, p.browRaise += 0.4 * w;
	};
	lib["look up"] = [](Pose &p, double w, double) { p.headPitch -= 28.0 * w; };
	lib["look down"] = [](Pose &p, double w, double) { p.headPitch += 22.0 * w; };
	lib["nod"] = [](Pose &p, double w, double since) { p.headPitch += 12.0 * w * 0.5 * (1.0 - std::cos(2.0 * kPi * 2.0 * since)); };
	lib["shake head"] = [](Pose &p, double w, double since) { p.headYaw += 20.0 * w * wiggle(since, 2.0); };
	lib["bow"] = [](Pose &p, double w, double) { p.lean += 20.0 * w, p.headPitch += 10.0 * w; };
	lib["hop"] = [](Pose &p, double w, double since) { p.lift += w * 0.25 * std::abs(std::sin(kPi * since / 0.45)); };
	return lib;
}

// The pose of a moment: its face (feelings, blinks, speech), where it looks, its idle life, where its props pull its hands, then its gestures.
inline Pose poseOf(const CharacterMoment &m, Vec3 headLocal, double handHeight, double reachLength) {
	Pose p;
	const faces::Face &f = m.face;
	p.smile = f.smile, p.browRaise = f.browRaise, p.browTilt = f.browTilt, p.eyeWide = f.eyeWide, p.headPitch = f.headPitch, p.headRoll = f.headRoll;
	p.blink = lidsWith(f.lids, rig::blink(m.t, m.seed));
	p.mouthOpen = f.mouthOpen;
	speakWith(m, p.mouthOpen, p.smile);
	const Mat4 toBody = invertAffine(mul(m.frame, scale(m.scale, m.scale, m.scale)));
	if (m.look.weight > 0.0) p.look = transformPoint(toBody, m.look.at), p.lookWeight = m.look.weight;
	if (m.idle) {
		const Idle i = idleAt(m.t, m.seed);
		p.headYaw += i.headYaw, p.headPitch += i.headPitch;
		if (p.lookWeight < 0.5) p.look = headLocal + Vec3{0.3 * std::sin(m.t * 0.7 + m.seed), 0.1 * std::sin(m.t * 1.3), 2.0}, p.lookWeight = std::max(p.lookWeight, 0.6);
	}
	for (int s = 0; s < 2; ++s)
		if (m.hands[s].weight > 0.0) (s == 0 ? p.handL : p.handR) = transformPoint(toBody, m.hands[s].at), (s == 0 ? p.weightL : p.weightR) = m.hands[s].weight;
	p.lean += 10.0 * m.running;
	if (m.gestures && !m.gestures->empty()) p = rig::act(p, *m.gestures, m.t, poses(handHeight, reachLength));
	p.weightL = std::min(1.0, p.weightL), p.weightR = std::min(1.0, p.weightR);
	return p;
}

// ---- shared parts ----
inline std::string jelly(Vec3 c, double roughness = 0.08) {
	char buf[200];
	std::snprintf(buf, sizeof buf, "Material \"coateddiffuse\" \"rgb reflectance\" [ %.4f %.4f %.4f ] \"float roughness\" [ %.3f ]", c.x, c.y, c.z, roughness);
	return buf;
}
// An eye (white ball, dark pupil turned to `look` by `lookWeight`, lids closed by `blink`), centred at `at` (body space) of radius r, under the transform W.
inline std::string eye(const Mat4 &W, Vec3 at, double r, Vec3 look, double lookWeight, double blink, double wide, Vec3 pupilColour) {
	Vec3 dir = normalize(lerp(Vec3{0.0, 0.0, 1.0}, normalize(look - at + Vec3{0.0, 0.0, 1e-6}), std::clamp(lookWeight, 0.0, 1.0)) + Vec3{0.0, 0.0, 1e-6});
	dir.z = std::max(dir.z, 0.25);
	dir = normalize(dir);
	const double open = std::clamp((1.0 - 0.92 * blink) * (1.0 + 0.3 * wide), 0.06, 1.3);
	const Mat4 lid = mul(W, translate(at.x, at.y, at.z), scale(1.0, open, 1.0));
	std::string out = attr(jelly({0.95, 0.95, 0.93}, 0.05), emitMesh(uvSphere(r, 20, 10), lid));
	const Vec3 p = dir * (0.78 * r);
	out += attr(jelly(pupilColour, 0.03), emitMesh(uvSphere(0.42 * r, 14, 7), mul(lid, translate(p.x, p.y, p.z), scale(1.0, 1.0, 0.55))));
	return out;
}
// A mouth drawn as a line across a face: `width` wide at `at`, its corners up by `smile` (-1 .. 1), as thick as it is open; bent round a face of radius `round`.
inline Mesh mouthLine(Vec3 at, double width, double smile, double open, double round) {
	std::vector<Vec3> pts;
	for (int i = 0; i <= 8; ++i) {
		const double u = i / 8.0 * 2.0 - 1.0, x = 0.5 * width * u;
		pts.push_back({x, at.y + smile * 0.35 * width * (u * u - 0.35), at.z - (round > 0.0 ? x * x / (2.0 * round) : 0.0)});
	}
	for (Vec3 &p : pts) p.x += at.x;
	return sdf::tubeMesh(pts, 0.012 + 0.05 * open, open > 0.15 ? 0.85 : 0.5, 10);
}
// A brow over an eye at `at`: a short dark stroke, raised and tilted (its inner end - towards the middle - down for anger, up for worry).
inline Mesh brow(Vec3 at, double r, double raise, double tilt, double side) {
	const double up = 0.35 * r * raise, inner = 0.3 * r * tilt;
	return sdf::tubeMesh({{at.x + side * 0.7 * r, at.y + 1.25 * r + up - 0.1 * r, at.z + 0.6 * r}, {at.x, at.y + 1.35 * r + up, at.z + 0.75 * r},
						  {at.x - side * 0.7 * r, at.y + 1.25 * r + up + inner, at.z + 0.6 * r}},
						 0.12 * r, 0.6, 8);
}
// An arm from `shoulder` to where its hand is (rest, or reaching to `to` by `w`), a ball for a hand.
inline std::string arm(const Mat4 &W, Vec3 shoulder, Vec3 rest, Vec3 to, double w, double r, const std::string &material, const std::string &handMaterial, double handR) {
	const Vec3 hand = lerp(rest, to, std::clamp(w, 0.0, 1.0));
	const Vec3 mid = lerp(shoulder, hand, 0.5) + Vec3{0.0, -0.06, 0.0} * length(hand - shoulder);   // (a soft droop)
	std::vector<Vec3> pts;
	for (int i = 0; i <= 6; ++i) {
		const double u = i / 6.0;
		pts.push_back(shoulder * ((1 - u) * (1 - u)) + mid * (2 * u * (1 - u)) + hand * (u * u));
	}
	return attr(material, emitMesh(sdf::tubeMesh(pts, r, 0.85, 10), W)) + attr(handMaterial, emitMesh(uvSphere(handR, 14, 7), mul(W, translate(hand.x, hand.y, hand.z))));
}
inline Vec3 partColour(const CharacterMoment &m, const char *part, Vec3 fallback) {
	Vec3 c = fallback;
	colourFrom(m, part, c);
	return c;
}

// ---- the blob ----
inline constexpr double kBlobHand = 0.38, kBlobReach = 0.45;
// How high its hop has it (body space, scale 1) and its squash (1: none; below, squashed; above, stretched) at so many steps, walking by `going`.
inline void blobHop(double steps, double going, double &lift, double &squash) {
	const double p = std::fmod(std::max(0.0, steps), 1.0), up = std::sin(kPi * p);
	lift = 0.16 * going * up;
	squash = 1.0 + going * (0.12 * up - 0.22 * std::pow(1.0 - up, 6.0));
}
inline Pose blobPose(const CharacterMoment &m) { return poseOf(m, {0.0, 0.95, 0.15}, kBlobHand, kBlobReach); }
inline Mat4 blobBody(const CharacterMoment &m, const Pose &p, double &squash) {
	double lift = 0.0;
	blobHop(m.steps, m.moving ? std::max(0.3, m.going) : m.going, lift, squash);
	squash *= (1.0 - 0.25 * m.crouch) * (m.airborne ? 1.12 : 1.0) * (1.0 + 0.02 * std::sin(m.t * 2.2 + m.seed));   // (crouching, a jump's stretch, breathing)
	return mul(m.frame, scale(m.scale, m.scale, m.scale), translate(0.0, lift + p.lift, 0.0), rotate({1.0, 0.0, 0.0}, p.lean), scale(1.0 / std::sqrt(squash), squash, 1.0 / std::sqrt(squash)));
}
inline std::string blobText(const CharacterMoment &m) {
	const Pose p = blobPose(m);
	double squash = 1.0;
	const Mat4 W = blobBody(m, p, squash);
	const Vec3 skin = partColour(m, "skin", {0.38, 0.82, 0.32}), eyes = partColour(m, "eyes", {0.06, 0.05, 0.08});
	std::string out = "# a blob (film_aliens.h)\n";
	// a gumdrop of jelly: wide and soft at the bottom, round on top
	out += attr(jelly(skin, 0.06), emitMesh(lathe({{0.0, 0.0}, {0.36, 0.0}, {0.43, 0.06}, {0.44, 0.2}, {0.4, 0.42}, {0.31, 0.62}, {0.18, 0.74}, {0.0, 0.78}}, 40), W));
	// its eyes on stalks, swinging behind as it goes (`jiggle`), turning to what it looks at
	const Mat4 back = invertAffine(m.frame);
	const Vec3 swing = (transformPoint(back, transformPoint(m.frame, Vec3{0, 0, 0}) + m.jiggle) - Vec3{0, 0, 0}) * (1.0 / std::max(0.1, m.scale));
	for (int s = 0; s < 2; ++s) {
		const double side = s == 0 ? 1.0 : -1.0;
		const Vec3 root{0.11 * side, 0.7, 0.05}, tip = Vec3{0.2 * side, 1.0, 0.12} + Vec3{swing.x, 0.0, swing.z} * 0.6 + Vec3{0.0, 0.0, 0.0};
		const Mat4 head = mul(W, rotate({0.0, 1.0, 0.0}, -p.headYaw), rotate({1.0, 0.0, 0.0}, p.headPitch), rotate({0.0, 0.0, 1.0}, p.headRoll * side * 0.5));
		out += attr(jelly(skin, 0.06), emitMesh(sdf::tubeMesh({root, lerp(root, tip, 0.5) + Vec3{0.02 * side, 0.02, 0.0}, tip}, 0.035, 0.75, 8), head));
		out += eye(head, tip + Vec3{0.0, 0.07, 0.0}, 0.085, p.look, p.lookWeight, p.blink, p.eyeWide, eyes);
		out += attr(diffuseMaterial({0.08, 0.1, 0.06}), emitMesh(brow(tip + Vec3{0.0, 0.07, 0.0}, 0.085, p.browRaise, p.browTilt, side), head));
	}
	// its mouth, across the front of its body
	out += attr(diffuseMaterial({0.06, 0.03, 0.04}), emitMesh(mouthLine({0.0, 0.46, 0.405}, 0.22, p.smile, p.mouthOpen, 0.4), W));
	// two stubby arms
	for (int s = 0; s < 2; ++s) {
		const double side = s == 0 ? 1.0 : -1.0;
		out += arm(W, {0.36 * side, kBlobHand, 0.05}, {0.5 * side, 0.22, 0.1}, s == 0 ? p.handL : p.handR, s == 0 ? p.weightL : p.weightR, 0.05, jelly(skin, 0.06), jelly(skin * 0.9, 0.06), 0.065);
	}
	return out;
}

// ---- the squid ----
inline constexpr double kSquidHand = 0.55, kSquidReach = 0.6;
inline Pose squidPose(const CharacterMoment &m) { return poseOf(m, {0.0, 0.95, 0.2}, kSquidHand, kSquidReach); }
inline std::string squidText(const CharacterMoment &m) {
	const Pose p = squidPose(m);
	const Vec3 skin = partColour(m, "skin", {0.55, 0.3, 0.75}), eyes = partColour(m, "eyes", {0.08, 0.04, 0.14});
	const double going = m.moving ? std::max(0.3, m.going) : m.going, bob = 0.04 * going * std::abs(std::sin(kPi * m.steps)) + 0.015 * std::sin(m.t * 1.8 + m.seed);
	const Mat4 base = mul(m.frame, scale(m.scale, m.scale, m.scale));
	const Mat4 W = mul(base, translate(0.0, bob + p.lift, 0.0), rotate({1.0, 0.0, 0.0}, p.lean));
	const Mat4 head = mul(W, translate(0.0, 0.9, 0.0), rotate({0.0, 1.0, 0.0}, -p.headYaw), rotate({1.0, 0.0, 0.0}, p.headPitch), rotate({0.0, 0.0, 1.0}, p.headRoll), translate(0.0, -0.9, 0.0));
	std::string out = "# a squid (film_aliens.h)\n";
	out += attr(jelly(skin, 0.1), emitMesh(lathe({{0.0, 0.45}, {0.24, 0.47}, {0.36, 0.62}, {0.4, 0.85}, {0.38, 1.05}, {0.3, 1.2}, {0.17, 1.29}, {0.0, 1.31}}, 40), head));
	for (int s = 0; s < 2; ++s) {
		const double side = s == 0 ? 1.0 : -1.0;
		const Vec3 at{0.15 * side, 0.92, 0.3};
		out += eye(head, at, 0.11, p.look, p.lookWeight, p.blink, p.eyeWide, eyes);
		out += attr(diffuseMaterial({0.2, 0.08, 0.25}), emitMesh(brow(at, 0.11, p.browRaise, p.browTilt, side), head));
	}
	out += attr(diffuseMaterial({0.15, 0.04, 0.12}), emitMesh(mouthLine({0.0, 0.66, 0.36}, 0.18, p.smile, p.mouthOpen, 0.36), head));
	// six tentacles from under its head down to the ground: rippling round as it walks (each a little after the one before), trailing as it moves, the front two its
	// arms when it reaches for something
	const Vec3 trail = m.trail * (1.0 / std::max(0.1, m.scale));
	for (int k = 0; k < 6; ++k) {
		const double a = 2.0 * kPi * k / 6.0 + kPi / 6.0, ca = std::cos(a), sa = std::sin(a);   // (k = 0 and 5 are in front, left and right)
		const double wave = std::sin(kPi * m.steps + k * kPi / 3.0) * going;
		const Vec3 root{0.2 * sa, 0.5, 0.2 * ca};
		Vec3 tip{0.42 * sa, 0.0, 0.42 * ca + 0.1 * wave};
		tip.y += 0.07 * std::max(0.0, wave);
		const bool front = k == 0 || k == 5;
		const double w = front ? (k == 0 ? p.weightL : p.weightR) : 0.0;
		if (w > 0.0) tip = lerp(tip, k == 0 ? p.handL : p.handR, std::clamp(w, 0.0, 1.0));
		std::vector<Vec3> pts;
		for (int i = 0; i <= 8; ++i) {
			const double u = i / 8.0;
			const Vec3 bend = Vec3{0.18 * sa, 0.0, 0.18 * ca} * std::sin(kPi * u) * (1.0 - w);   // (bowed outwards)
			const Vec3 curl = Vec3{0.06 * sa, 0.05, 0.06 * ca} * std::pow(u, 6.0) * (1.0 - w);   // (its tip curling up)
			const Vec3 lag = Vec3{-trail.x, 0.0, -trail.z} * (0.35 * u * u) * (1.0 - w);
			pts.push_back(lerp(root, tip, u) + bend + curl + lag + Vec3{0.0, 0.02 * std::sin(m.t * 2.0 + k + 4.0 * u) * (1.0 - w), 0.0});
		}
		out += attr(jelly(skin * 0.95, 0.1), emitMesh(sdf::tubeMesh(pts, 0.06, 0.3, 10), mul(base, translate(0.0, bob * 0.5, 0.0))));
	}
	return out;
}

// ---- the bot ----
inline constexpr double kBotHand = 0.55, kBotReach = 0.5;
inline Pose botPose(const CharacterMoment &m) { return poseOf(m, {0.0, 0.95, 0.2}, kBotHand, kBotReach); }
inline double botHover(const CharacterMoment &m) { return 0.16 + 0.025 * std::sin(m.t * 2.3 + m.seed) + 0.02 * std::sin(kPi * m.steps) * m.going; }
inline std::string botText(const CharacterMoment &m) {
	const Pose p = botPose(m);
	const Vec3 shell = partColour(m, "body", {0.92, 0.9, 0.86}), trim = partColour(m, "trim", {0.95, 0.45, 0.12}), glow = partColour(m, "eyes", {0.3, 0.85, 1.0});
	const double going = m.moving ? std::max(0.3, m.going) : m.going;
	const Mat4 W = mul(m.frame, scale(m.scale, m.scale, m.scale), translate(0.0, botHover(m) + p.lift, 0.0), rotate({1.0, 0.0, 0.0}, p.lean + 9.0 * going));
	std::string out = "# a bot (film_aliens.h)\n";
	out += attr(jelly(shell, 0.12), emitMesh(roundedBox(0.44, 0.36, 0.34, 0.07), mul(W, translate(0.0, 0.38, 0.0))));
	out += attr(jelly(trim, 0.15), emitMesh(roundedBox(0.46, 0.06, 0.36, 0.03), mul(W, translate(0.0, 0.23, 0.0))));
	// its thruster: a glowing ring under it, flickering
	const Vec3 jet = Vec3{0.45, 0.75, 1.0} * (3.0 * (0.85 + 0.15 * std::sin(m.t * 37.0 + m.seed)));
	out += attr(diffuseMaterial({0.0, 0.0, 0.0}), emitMesh(lathe({{0.0, 0.17}, {0.1, 0.17}, {0.08, 0.13}, {0.0, 0.12}}, 20), W), &jet);
	// its head on a neck, a screen for a face
	const Mat4 head = mul(W, translate(0.0, 0.62, 0.0), rotate({0.0, 1.0, 0.0}, -p.headYaw), rotate({1.0, 0.0, 0.0}, p.headPitch), rotate({0.0, 0.0, 1.0}, p.headRoll));
	out += attr(metalMaterial({0.6, 0.6, 0.62}, 0.3), emitMesh(capsule(0.04, 0.06, 10, 3), mul(W, translate(0.0, 0.59, 0.0))));
	out += attr(jelly(shell, 0.12), emitMesh(roundedBox(0.4, 0.3, 0.3, 0.08), mul(head, translate(0.0, 0.18, 0.0))));
	out += attr(jelly({0.02, 0.025, 0.03}, 0.02), emitMesh(roundedBox(0.32, 0.21, 0.02, 0.04), mul(head, translate(0.0, 0.18, 0.15))));
	// the face on the screen: two glowing eyes (blinking to a line, widening) and a glowing mouth (smiling, talking), and brows when it feels something
	const double open = std::clamp((1.0 - 0.9 * p.blink) * (1.0 + 0.35 * p.eyeWide), 0.08, 1.35);
	const Vec3 eyeGlow = glow * 4.0;
	const Vec3 gaze = p.lookWeight > 0.0 ? normalize(p.look - Vec3{0.0, 0.95, 0.2} + Vec3{0.0, 0.0, 1e-6}) * (0.025 * std::min(1.0, p.lookWeight)) : Vec3{};
	for (int s = 0; s < 2; ++s) {
		const double side = s == 0 ? 1.0 : -1.0;
		out += attr(diffuseMaterial({0.0, 0.0, 0.0}), emitMesh(uvSphere(0.035, 14, 7), mul(head, translate(0.065 * side + gaze.x, 0.21 + gaze.y, 0.161), scale(0.85, open * 1.25, 0.25))), &eyeGlow);
		if (std::fabs(p.browRaise) + std::fabs(p.browTilt) > 0.05)
			out += attr(diffuseMaterial({0.0, 0.0, 0.0}), emitMesh(box(0.05, 0.008, 0.006), mul(head, translate(0.065 * side, 0.27 + 0.02 * p.browRaise, 0.162), rotate({0.0, 0.0, 1.0}, -18.0 * p.browTilt * side))), &eyeGlow);
	}
	const Vec3 mouthGlow = glow * 3.0;
	out += attr(diffuseMaterial({0.0, 0.0, 0.0}), emitMesh(mouthLine({0.0, 0.125, 0.162}, 0.12, p.smile, 0.5 * p.mouthOpen, 0.0), head), &mouthGlow);
	// the antenna, swinging after it (`jiggle`), its light blinking
	const Mat4 back = invertAffine(m.frame);
	const Vec3 swing = (transformPoint(back, transformPoint(m.frame, Vec3{0, 0, 0}) + m.jiggle) - Vec3{0, 0, 0}) * (1.0 / std::max(0.1, m.scale));
	const Vec3 tipAt{0.08 + 0.5 * swing.x, 0.52, 0.5 * swing.z};
	out += attr(metalMaterial({0.6, 0.6, 0.62}, 0.3), emitMesh(sdf::tubeMesh({{0.08, 0.32, 0.0}, {0.08 + 0.2 * swing.x, 0.42, 0.2 * swing.z}, tipAt}, 0.008, 1.0, 6), head));
	const Vec3 beacon = std::fmod(m.t + 0.13 * m.seed, 1.6) < 0.25 ? Vec3{1.0, 0.15, 0.1} * 12.0 : Vec3{0.25, 0.04, 0.03};
	out += attr(diffuseMaterial({0.0, 0.0, 0.0}), emitMesh(uvSphere(0.025, 10, 5), mul(head, translate(tipAt.x, tipAt.y, tipAt.z))), &beacon);
	// noodle arms with claws
	for (int s = 0; s < 2; ++s) {
		const double side = s == 0 ? 1.0 : -1.0;
		out += arm(W, {0.24 * side, kBotHand, 0.0}, {0.33 * side, 0.26, 0.08}, s == 0 ? p.handL : p.handR, s == 0 ? p.weightL : p.weightR, 0.022, metalMaterial({0.6, 0.6, 0.62}, 0.3),
				   jelly(trim, 0.15), 0.045);
	}
	return out;
}

// ---- the kinds ----
// A kind of these: what they share (the gestures, a voice, feet on the ground under it for the sound and the footprints, hands for props).
inline CharacterKind kindOf(std::function<std::string(const CharacterMoment &)> text, std::function<Pose(const CharacterMoment &)> pose, double hand, double reach, double height,
							double radius, double stride, speech::Voice voice, Vec3 head, std::vector<std::string> parts, bool hovers, Vec3 restLeft) {
	CharacterKind k;
	k.text = std::move(text);
	k.poses = namesOf(poses(hand, reach));
	k.stride = stride, k.firstStep = 0.5, k.scalable = true;
	k.chest = {0.0, 0.55 * height, 0.0};
	k.head = head, k.radius = radius, k.height = height, k.voice = voice, k.parts = std::move(parts);
	k.carry = {0.2, hand - 0.05, 0.35}, k.hand = 1;
	k.handAt = [pose, restLeft](const CharacterMoment &m, int s) {   // (where its hand is drawn: at rest, or reaching)
		const Pose p = pose(m);
		const Vec3 rest{s == 0 ? restLeft.x : -restLeft.x, restLeft.y, restLeft.z};
		const Vec3 body = lerp(rest, s == 0 ? p.handL : p.handR, std::clamp(s == 0 ? p.weightL : p.weightR, 0.0, 1.0));
		return transformPoint(mul(m.frame, scale(m.scale, m.scale, m.scale)), body);
	};
	k.feet = [hovers](const CharacterMoment &m) {   // (one foot under its middle: down between hops, never for the hovering bot)
		const double p = std::fmod(std::max(0.0, m.steps), 1.0);
		return std::vector<Foot>{{transformPoint(m.frame, {0.0, 0.0, 0.0}), !hovers && !m.airborne && (!m.moving || p < 0.12 || p > 0.88)}};
	};
	k.footCount = 1;
	return k;
}
inline CharacterKind blobKind() {
	return kindOf(blobText, blobPose, kBlobHand, kBlobReach, 1.0, 0.45, 0.45, {340.0, 1.25, 0.06}, {0.0, 1.0, 0.12}, {"skin", "eyes"}, false, {0.5, 0.22, 0.1});
}
inline CharacterKind squidKind() {
	CharacterKind k = kindOf(squidText, squidPose, kSquidHand, kSquidReach, 1.3, 0.45, 0.5, {170.0, 0.9, 0.05}, {0.0, 0.92, 0.3}, {"skin", "eyes"}, false, {0.21, 0.0, 0.36});
	k.trails = true;   // (its tentacles trail)
	return k;
}
inline CharacterKind botKind() {
	return kindOf(botText, botPose, kBotHand, kBotReach, 0.9, 0.3, 0.5, {230.0, 1.1, 0.0}, {0.0, 0.8, 0.15}, {"body", "trim", "eyes"}, true, {0.33, 0.26, 0.08});
}

// Adds them to film_file::characterKinds() (cartoons.cpp, with film_kinds::install).
inline void install() {
	auto &kinds = film_file::characterKinds();
	kinds["blob"] = blobKind();
	kinds["squid"] = squidKind();
	kinds["hoverbot"] = botKind();
}

}  // namespace aliens
}  // namespace film_kinds
}  // namespace cartoon
