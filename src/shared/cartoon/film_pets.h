#pragma once
// film_pets.h -- indoor round 2: pets, as `character NAME cat|dog at X Y Z` (the film file's character kinds: film_kinds.h).
//
//   cat   a cartoon cat 0.3 m at the shoulder: pointed ears, big eyes, whiskers, a long tail swaying slowly (up as it trots); sitting it sits up on its haunches, napping
//         (`nap`) it curls up nose to tail, eyes shut. Colours: fur, belly, eyes, nose
//   dog   a cartoon dog 0.45 m at the shoulder: a long snout, floppy ears, a tail that wags while it stands about or sits waiting (`wait`, `heel`). Colours: fur, ears,
//         belly, eyes, nose
//
// They trot on four legs (front-left with hind-right), their feet held where they come down. Poses (`gesture NAME "paw" ...`): paw (a front paw raised and batted - what
// `knock` does), beg (up on its hind legs, front paws tucked), nod, look up. What they carry they carry in their mouths. Body space as every kind's: +x its left, +y up, +z
// forward, at scale 1.

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "film_sea_kinds.h"

namespace cartoon {
namespace film_kinds {
namespace pets {

using sea::SeaPose;
using sea::detail::brow;
using sea::detail::ellipsoid;
using sea::detail::emitted;
using sea::detail::eye;
using deepsea::detail::add;

inline const rig::PoseLibrary<SeaPose> &poses() {
	static const rig::PoseLibrary<SeaPose> library = {
		{"paw", [](SeaPose &p, double w, double since) { p.raiseR += w, p.waveR += w * std::sin(2.0 * kPi * 2.5 * since), p.eyeWide += 0.2 * w; }},
		{"beg", [](SeaPose &p, double w, double) { p.raiseL += w, p.raiseR += w, p.smile += 0.4 * w, p.eyeWide += 0.4 * w; }},
		{"nod", [](SeaPose &p, double w, double since) { p.nod += 14.0 * w * std::max(0.0, std::sin(2.0 * kPi * 1.8 * since)); }},
		{"look up", [](SeaPose &p, double w, double) { p.nod -= 25.0 * w; }},
	};
	return library;
}
// The pose of a pet at a moment: its face, its lines, a blink, where it looks, its gestures (sea::detail::posed's, with its own poses).
inline SeaPose posed(const CharacterMoment &m, Vec3 headLocal) {
	SeaPose p;
	p.smile = m.face.smile, p.browTilt = m.face.browTilt, p.browRaise = m.face.browRaise, p.eyeWide = m.face.eyeWide, p.lids = m.face.lids, p.mouthOpen = m.face.mouthOpen;
	speakWith(m, p.mouthOpen, p.smile);
	p.blink = rig::blink(m.t, m.seed);
	double yaw = 0.0, pitch = 0.0;
	lookWith(m, headLocal, 50.0, yaw, pitch, p.look, p.lookWeight);
	if (m.idle) idleEyes(m, idleAt(m.t, m.seed), headLocal, p.look, p.lookWeight);
	return m.gestures && !m.gestures->empty() ? rig::act(p, *m.gestures, m.t, poses()) : p;
}

// What a cat and a dog differ in (body space at scale 1).
struct Build {
	double hip = 0.17, shoulder = 0.18, half = 0.13, width = 0.05, legR = 0.02, stride = 0.17;   // (legs' tops, half the body's length, their spread)
	Vec3 body{0.09, 0.085, 0.2}, bodyAt{0.0, 0.21, 0.0}, head{0.075, 0.068, 0.07}, headAt{0.0, 0.33, 0.21};
	bool dog = false;
};
inline Build buildOf(bool dog) {
	Build b;
	if (!dog) return b;
	b.dog = true;
	b.hip = 0.27, b.shoulder = 0.3, b.half = 0.2, b.width = 0.08, b.legR = 0.032, b.stride = 0.25;
	b.body = {0.13, 0.13, 0.3}, b.bodyAt = {0.0, 0.34, 0.0}, b.head = {0.1, 0.095, 0.1}, b.headAt = {0.0, 0.52, 0.33};
	return b;
}
// Its legs: 0 front-left, 1 front-right, 2 hind-left, 3 hind-right (front-left and hind-right step together). Where a foot is at so many steps (body space, scale 1), and whether
// it is down: back along the ground while down, forward through the air, lifted, while not.
inline int legPair(int k) { return (k == 0 || k == 3) ? 0 : 1; }
inline Vec3 restFoot(const Build &b, int k) { return {(k % 2 ? -1.0 : 1.0) * b.width, 0.0, (k < 2 ? 1.0 : -1.0) * b.half}; }
inline Vec3 footAt(const Build &b, double steps, int k) {
	const double phase = gaitPhase(steps) + legPair(k) * kPi, amount = gaitAmount(steps);
	return restFoot(b, k) + Vec3{0.0, 0.05 * b.stride / 0.17 * std::max(0.0, std::cos(phase)) * amount, 0.5 * b.stride * std::sin(phase) * amount};
}

inline std::string petText(const CharacterMoment &m, const Build &b) {
	const SeaPose p = posed(m, b.headAt);
	Vec3 fur = b.dog ? Vec3{0.55, 0.36, 0.2} : Vec3{0.85, 0.48, 0.18}, belly = b.dog ? Vec3{0.88, 0.78, 0.62} : Vec3{0.95, 0.9, 0.82}, iris = b.dog ? Vec3{0.25, 0.14, 0.06} : Vec3{0.35, 0.6, 0.15};
	Vec3 nose = b.dog ? Vec3{0.05, 0.04, 0.04} : Vec3{0.9, 0.5, 0.55}, ears = b.dog ? Vec3{0.32, 0.2, 0.1} : fur;
	colourFrom(m, "fur", fur), colourFrom(m, "belly", belly), colourFrom(m, "eyes", iris), colourFrom(m, "nose", nose);
	if (b.dog) colourFrom(m, "ears", ears);
	Mesh coat, under, earM, white, pupil, lid, brows, noseM, mouth, whiskers;
	const double S = b.dog ? 1.5 : 1.0;   // (the dog's head things are bigger)
	const double curl = std::clamp(m.lie, 0.0, 1.0), sit = std::clamp(m.sit, 0.0, 1.0) * (1.0 - curl), beg = std::min(1.0, std::min(p.raiseL, p.raiseR));
	const double going = std::clamp(m.going, 0.0, 1.0);
	if (curl > 0.5) {   // napping: curled up on its side, nose to tail, eyes shut, breathing
		const double breath = 1.0 + 0.03 * std::sin(2.0 * kPi * m.t / 2.6);
		add(coat, ellipsoid({0.0, b.body.y * 0.95, 0.0}, Vec3{b.body.z * 0.85, b.body.y * breath, b.body.z * 0.75}, 22, 12));
		add(coat, ellipsoid({b.body.z * 0.35, b.body.y * 0.95, b.body.z * 0.55}, b.head * 0.95, 18, 10));
		for (const double side : {1.0, -1.0}) add(earM, sdf::tubeMesh({{b.body.z * 0.35 + 0.03 * side, b.body.y * 1.6, b.body.z * 0.55}, {b.body.z * 0.35 + 0.045 * side, b.body.y * 1.6 + 0.05 * S, b.body.z * 0.5}}, 0.02 * S, b.dog ? 0.8 : 0.15, 6));
		std::vector<Vec3> tail;
		for (int q = 0; q <= 10; ++q) {
			const double a = kPi * (0.9 + 1.1 * q / 10.0);
			tail.push_back({b.body.z * 0.95 * std::cos(a), 0.04, b.body.z * 0.85 * std::sin(a)});
		}
		add(coat, sdf::tubeMesh(tail, b.dog ? 0.03 : 0.022, 0.6, 8));
		SeaPose shut = p;
		shut.lids = 1.0;
		for (const double side : {1.0, -1.0}) eye(white, pupil, lid, m, shut, {b.body.z * 0.35 + side * 0.028 * S, b.body.y * 1.15, b.body.z * 0.55 + b.head.z * 0.85}, 0.018 * S, {0.0, 0.0, 1.0});
		return emitted(m, {{diffuseMaterial(fur), &coat}, {diffuseMaterial(ears), &earM}, {coated({0.96, 0.96, 0.95}, 0.08), &white}, {coated(fur, 0.5), &lid}});
	}
	// the body: level as it stands and trots, its front up as it sits (about its hips) and higher as it begs; its legs to where its feet are
	const double up = 38.0 * sit + 30.0 * beg;
	const Vec3 hips{0.0, b.hip, -b.half};
	const Mat4 lift = mul(translate(hips), rotate({1.0, 0.0, 0.0}, -up), translate(hips * -1.0), translate(0.0, -0.05 * S * sit, 0.0));
	const auto lifted = [&](Vec3 v) { return transformPoint(lift, v); };
	const double bounce = 0.008 * S * going * std::abs(std::sin(gaitPhase(m.steps)));
	add(coat, ellipsoid(lifted(b.bodyAt + Vec3{0.0, bounce, 0.0}), b.body, 22, 12));
	add(under, ellipsoid(lifted(b.bodyAt + Vec3{0.0, -0.035 * S + bounce, 0.02}), Vec3{b.body.x * 0.8, b.body.y * 0.7, b.body.z * 0.8}, 18, 10));
	for (int k = 0; k < 4; ++k) {
		const bool front = k < 2, right = k % 2 == 1;
		const Vec3 top = lifted(Vec3{(right ? -1.0 : 1.0) * b.width, front ? b.shoulder : b.hip, (front ? 1.0 : -1.0) * (b.half - 0.02)} + Vec3{0.0, bounce, 0.0});
		Vec3 foot = m.feetLocked ? m.lockedFeet[k] * (1.0 / std::max(1e-6, m.scale)) : footAt(b, m.steps, k);
		if (front && sit > 0.0) foot = lerp(foot, Vec3{foot.x, 0.0, b.half * 0.55}, sit);   // (sitting: its front feet in under its chest)
		if (front && (right ? p.raiseR : p.raiseL) > 0.0) {   // a paw raised (and batted) or tucked up begging
			const double raise = std::min(1.0, right ? p.raiseR : p.raiseL);
			const Vec3 held = top + Vec3{0.0, -0.06 * S, 0.1 * S + (right ? 0.05 * S * p.waveR : 0.0)} + Vec3{0.0, 0.04 * S * beg, -0.06 * S * beg};
			foot = lerp(foot, held, raise);
		}
		if (!front && sit > 0.0) foot = lerp(foot, Vec3{foot.x * 1.3, 0.0, -b.half * 0.9}, sit);   // (its haunches down beside its hind feet)
		const Vec3 knee = (top + foot) * 0.5 + Vec3{0.0, 0.0, front ? -0.02 : 0.03 * S};
		add(coat, sdf::tubeMesh({top, knee, foot + Vec3{0.0, 0.015, 0.0}}, b.legR, 0.8, 8));
		add(under, ellipsoid(foot + Vec3{0.0, 0.012, 0.012}, {b.legR * 1.2, b.legR * 0.7, b.legR * 1.6}, 10, 6));
	}
	// the head: on its neck, nodding, a little turned to what it looks at
	const Vec3 head = lifted(b.headAt + Vec3{0.0, bounce - 0.01 * std::sin(p.nod * kPi / 180.0), 0.0});
	add(coat, sdf::tubeMesh({lifted(b.bodyAt + Vec3{0.0, 0.03 * S, b.body.z * 0.7}), head + Vec3{0.0, -0.03 * S, -0.02 * S}}, 0.045 * S, 0.8, 10));
	add(coat, ellipsoid(head, b.head, 20, 14));
	const Vec3 ahead = normalize(Vec3{0.0, -std::sin(p.nod * kPi / 180.0) * 0.5, 1.0});
	if (b.dog) {   // a snout, its nose at the end; ears hanging and swinging as it trots
		add(coat, ellipsoid(head + Vec3{0.0, -0.025, 0.09}, {0.055, 0.045, 0.075}, 16, 10));
		add(noseM, ellipsoid(head + Vec3{0.0, -0.005, 0.165}, {0.022, 0.016, 0.014}, 10, 6));
		add(mouth, ellipsoid(head + Vec3{0.0, -0.06, 0.1}, {0.04, 0.004 + 0.02 * std::clamp(p.mouthOpen, 0.0, 1.0), 0.05}, 12, 6));
		for (const double side : {1.0, -1.0}) {
			const double swing = 0.03 * going * std::sin(gaitPhase(m.steps) * 2.0 + side);
			add(earM, sdf::tubeMesh({head + Vec3{side * 0.075, 0.05, -0.01}, head + Vec3{side * 0.11, -0.02, -0.02 + swing}, head + Vec3{side * 0.1, -0.08, swing}}, 0.03, 0.7, 8));
		}
	} else {   // a little nose and mouth, whiskers, pointed ears
		add(noseM, ellipsoid(head + Vec3{0.0, -0.005, b.head.z * 0.98}, {0.012, 0.009, 0.008}, 8, 6));
		add(mouth, ellipsoid(head + Vec3{0.0, -0.032, b.head.z * 0.9}, {0.018, 0.003 + 0.012 * std::clamp(p.mouthOpen, 0.0, 1.0), 0.01}, 10, 6));
		for (const double side : {1.0, -1.0}) {
			add(earM, sdf::tubeMesh({head + Vec3{side * 0.045, 0.045, -0.005}, head + Vec3{side * 0.055, 0.105, -0.01}}, 0.028, 0.12, 6));
			for (int q = 0; q < 3; ++q) add(whiskers, sdf::tubeMesh({head + Vec3{side * 0.02, -0.015, b.head.z * 0.92}, head + Vec3{side * 0.11, -0.005 - 0.012 * q, b.head.z * 0.85 + 0.01}}, 0.0015, 0.5, 4));
		}
	}
	for (const double side : {1.0, -1.0}) {
		const Vec3 c = head + Vec3{side * 0.033 * S, 0.018 * S, b.head.z * 0.78};
		eye(white, pupil, lid, m, p, c, 0.021 * S, normalize(ahead + Vec3{side * 0.25, 0.0, 0.0}));
		brow(brows, c, 0.021 * S, side, p);
	}
	// the tail: a cat's slow sway (up and curling over as it trots); a dog's wag while it stands about, faster the happier it is
	std::vector<Vec3> tail;
	const double len = b.dog ? 0.3 : 0.38, wag = b.dog ? (1.0 - going) * (0.55 + 0.3 * std::max(0.0, p.smile)) * std::sin(2.0 * kPi * 3.2 * m.t) : 0.35 * std::sin(2.0 * kPi * 0.25 * m.t + m.seed);
	for (int q = 0; q <= 10; ++q) {
		const double u = q / 10.0, rise = b.dog ? 0.5 + 0.3 * going : 0.3 + 1.0 * going * u, side = wag * u * (b.dog ? 1.0 : 0.6);
		tail.push_back(lifted(Vec3{0.0, b.bodyAt.y + 0.03 * S, -b.body.z * 0.92}) + Vec3{len * u * std::sin(side), len * u * rise, -len * u * (1.0 - 0.6 * rise * u) * std::cos(side)});
	}
	if (sit > 0.5) for (Vec3 &v : tail) v.y = std::max(0.02, v.y - 0.25 * (v.y - 0.02) * sit);   // (sitting: its tail down along the floor)
	add(coat, sdf::tubeMesh(tail, b.dog ? 0.028 : 0.02, b.dog ? 0.35 : 0.7, 8));
	return emitted(m, {{diffuseMaterial(fur), &coat}, {diffuseMaterial(belly), &under}, {diffuseMaterial(ears), &earM}, {coated({0.96, 0.96, 0.95}, 0.08), &white}, {coated(iris, 0.05), &pupil},
					   {coated(fur, 0.5), &lid}, {diffuseMaterial(fur * 0.45), &brows}, {coated(nose, 0.25), &noseM}, {diffuseMaterial({0.25, 0.06, 0.07}), &mouth}, {diffuseMaterial({0.95, 0.95, 0.95}), &whiskers}});
}

inline CharacterKind petKind(bool dog) {
	const Build b = buildOf(dog);
	CharacterKind k;
	k.curls = true;
	k.footAt = [b](double steps, int leg) { return footAt(b, steps, leg); };
	k.plantedAt = [](double steps, int leg) { return gaitPlanted(steps, legPair(leg)); };
	k.footCount = 4;
	k.stride = b.stride, k.firstStep = 1.0;
	k.chest = b.bodyAt + Vec3{0.0, 0.0, b.body.z * 0.6};
	k.head = b.headAt;
	k.carry = b.headAt + Vec3{0.0, -0.05, dog ? 0.17 : 0.08};   // (in its mouth)
	k.radius = dog ? 0.3 : 0.2, k.height = dog ? 0.62 : 0.42;
	k.scalable = true;
	k.poses = namesOf(poses());
	k.voice = dog ? speech::Voice{300.0, 1.0, 0.05} : speech::Voice{560.0, 1.1, 0.06};
	k.parts = dog ? std::vector<std::string>{"fur", "ears", "belly", "eyes", "nose"} : std::vector<std::string>{"fur", "belly", "eyes", "nose"};
	k.text = [b](const CharacterMoment &m) { return petText(m, b); };
	k.handAt = [b](const CharacterMoment &m, int s) {   // (its front paws; what it carries, in its mouth: carry)
		return transformPoint(m.frame, (restFoot(b, s) + Vec3{0.0, 0.03, 0.0}) * m.scale);
	};
	k.feet = [k](const CharacterMoment &m) { return feetOf(m, k); };
	return k;
}

// Adds them to film_file::characterKinds() (cartoons.cpp, with film_kinds::install).
inline void install() {
	auto &kinds = film_file::characterKinds();
	kinds["cat"] = petKind(false);
	kinds["dog"] = petKind(true);
}

}  // namespace pets
}  // namespace film_kinds
}  // namespace cartoon
