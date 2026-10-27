#pragma once
// film_kinds.h -- the kinds of character a film file can bring in (film_file.h `character NAME KIND`), each a rig of the toolkit made to walk, blink, speak and act from a
// CharacterMoment:
//
//     nomai-explorer   the masked Nomai explorer (nomai_explorer.h), its own walk cycle and pose library
//     pip              Pip (cartoon_actor.h), the round little creature with a big head; `scale` (a child 0.6, a parent 1.3)
//     person           a cartoon adult, 1.8 m (human_actor.h)
//     turtle           the turtle (turtle_actor.h), 13 cm long; `scale` (6: as big as a dog)
//
// Each walks with its own gait, its feet held where they come down (rig::FootLock), blinks on its own (its seed), opens its mouth as it speaks (CharacterMoment::voice) and acts
// with a library of poses that its gestures name (the poses are listed with --film-check's message for a pose it does not have).
//
// The rigs have +x forward and +z to their right and are turned by a yaw (0 faces +x, positive turns the front towards +z); a film file's character frame faces its +z, +x its left.

#include <cmath>
#include <map>
#include <string>
#include <vector>

#include "cartoon_actor.h"
#include "film_faces.h"
#include "film_file.h"
#include "human_actor.h"
#include "nomai_explorer.h"
#include "rig.h"
#include "turtle_actor.h"

namespace cartoon {
namespace film_kinds {

using film_file::CharacterKind;
using film_file::CharacterMoment;

// ---- what every rig kind shares ----
// The rig's place and yaw from a character's frame (story space).
inline void placeOf(const Mat4 &frame, Vec3 &pos, double &yaw) {
	pos = transformPoint(frame, {0.0, 0.0, 0.0});
	const Vec3 f = transformPoint(frame, {0.0, 0.0, 1.0}) - pos;
	yaw = std::atan2(f.z, f.x) * 180.0 / kPi;
}
// A rig point (+x forward, +z right) in a character's body space (+x left, +z forward), and back.
inline Vec3 bodyOfRig(Vec3 r) { return {-r.z, r.y, r.x}; }
// A point in the rig's own frame carried into the world (story space).
inline Vec3 rigPoint(Vec3 pos, double yaw, Vec3 local) { return transformPoint(mul(translate(pos), rotate({0.0, 1.0, 0.0}, -yaw)), local); }

// The rigs' walk cycles (a cycle is two steps, one per 2 pi): the phase at so many steps - each starts with both feet down, the left about to lift - and how much of a stride they
// take (eased in over the first half step, so a walk does not start with a jump).
inline double gaitPhase(double steps) { return -0.5 * kPi + kPi * steps; }
inline double gaitAmount(double steps) { return smoothstep(0.0, 0.5, steps); }
// Whether the foot of a pair (0 the left, or a turtle's front-left and hind-right; 1 the other) is on the ground at so many steps: it is down while its swing is behind it.
inline bool gaitPlanted(double steps, int pair) { return std::cos(gaitPhase(steps) + pair * kPi) <= 1e-9; }

// A foot of a moment in the world: held where it came down, or where the gait puts it.
inline Vec3 worldFoot(const CharacterMoment &m, const CharacterKind &kind, int s) {
	return transformPoint(m.frame, m.feetLocked ? m.lockedFeet[s] : kind.footAt(m.steps, s) * m.scale);
}
inline std::vector<Foot> feetOf(const CharacterMoment &m, const CharacterKind &kind) {
	std::vector<Foot> out;
	for (int s = 0; s < kind.footCount; ++s) out.push_back({worldFoot(m, kind, s), kind.plantedAt(m.steps, s)});
	return out;
}
// A hand target eased in by a gesture's weight. Several at once take the mean of their targets by their weights, so a gesture fading out under the next hands the hand over smoothly (the first one used to claim it
// outright: when it faded to nothing the hand jumped to the other's target). `weight` is their sum; the arm clamps it to 1.
inline void reachTo(Vec3 &hand, double &weight, Vec3 target, double w) {
	if (!(w > 0.0)) return;
	hand = weight > 0.0 ? (hand * weight + target * w) * (1.0 / (weight + w)) : target;
	weight += w;
}
template <class P>
inline std::vector<std::string> namesOf(const rig::PoseLibrary<P> &library) {
	std::vector<std::string> out;
	for (const auto &[name, pose] : library) out.push_back(name);
	return out;
}
// How far a mouth opens and how it smiles for a moment: the shape its `say` lines need (wide for an "ee", round for an "oo"), or, for a recording (a cues file's
// `sample ... by NAME`), open by the voice's loudness.
inline void speakWith(const CharacterMoment &m, double &mouthOpen, double &smile) {
	const bool line = m.mouth.wide + m.mouth.round > 0.0;
	mouthOpen = std::max(mouthOpen, line ? m.mouth.open : 0.85 * m.voice);
	smile += 0.35 * m.mouth.wide - 0.3 * m.mouth.round;
}
// Lids lowered by a feeling, and a blink on top of them.
inline double lidsWith(double lids, double blink) {
	lids = std::clamp(lids, 0.0, 0.9);
	return lids + (1.0 - lids) * blink;
}
// Its hands where the moment's props need them (CharacterMoment::hands: 0 the left, 1 the right), eased in by their weights.
inline void reachWith(const CharacterMoment &m, Vec3 &handL, double &weightL, Vec3 &handR, double &weightR) {
	if (m.hands[0].weight > 0.0) reachTo(handL, weightL, m.hands[0].at, m.hands[0].weight);
	if (m.hands[1].weight > 0.0) reachTo(handR, weightR, m.hands[1].at, m.hands[1].weight);
}
// How far a hand reaches below `low` (metres above its feet; 0 .. 1 at `low - range`): how much it bends down for it.
inline double reachingDown(const CharacterMoment &m, double low, double range) {
	const double feet = transformPoint(m.frame, {0.0, 0.0, 0.0}).y;
	double d = 0.0;
	for (const CharacterMoment::Reach &h : m.hands) d = std::max(d, h.weight * clamp01((low - (h.at.y - feet)) / range));
	return d;
}
// Its head and eyes turned to what it looks at (CharacterMoment::look), as far as a neck turns: a rig's head yaw turns it to its right and its pitch looks down, so the aim
// (to its left and up) is taken away.
inline void lookWith(const CharacterMoment &m, Vec3 headLocal, double maxYaw, double &headYaw, double &headPitch, Vec3 &look, double &lookWeight) {
	if (m.look.weight <= 0.0) return;
	const Vec3 head = transformPoint(m.frame, headLocal * m.scale), ahead = transformPoint(m.frame, {0.0, 0.0, 1.0}) - transformPoint(m.frame, {0.0, 0.0, 0.0});
	const rig::Aim aim = rig::aimAt(head, rig::headingOf(ahead), m.look.at, maxYaw, 40.0, m.look.weight);
	headYaw -= aim.yaw;
	headPitch -= aim.pitch;
	look = m.look.at;
	lookWeight = std::max(lookWeight, m.look.weight);
}
// Idle life (CharacterMoment::idle): a head that drifts a little, eyes that dart - a jump every second or two to another point near what they look at (or, looking at
// nothing, near straight ahead), quick as a real saccade - and the slow numbers a body breathes and shifts its weight by. All from the time and the character's seed.
struct Idle {
	double headYaw = 0.0, headPitch = 0.0;   // degrees
	Vec3 gaze;                                // an offset for the eyes' target (world, metres at the target)
	double breath = 0.0, sway = 0.0;          // -1..1: in and out, from foot to foot
};
inline Idle idleAt(double t, int seed) {
	Idle i;
	const double s = seed * 0.37;
	i.headYaw = 3.5 * std::sin(0.31 * t + s) + 1.5 * std::sin(0.73 * t + 2.0 * s);
	i.headPitch = 2.0 * std::sin(0.23 * t + 1.7 * s);
	i.breath = std::sin(2.0 * kPi * t / 3.6 + s);
	i.sway = std::sin(2.0 * kPi * t / 5.3 + 3.0 * s);
	// the saccades: segments of 1.1 to 2.6 s, each with its own small offset, jumped to in 0.05 s
	const double period = 1.85;
	const int k = static_cast<int>(std::floor(t / period));
	const auto offsetOf = [&](int n) { return Vec3{0.12 * (hash01(n, seed) - 0.5), 0.08 * (hash01(n, seed + 7) - 0.5), 0.0}; };
	const double start = (k + 0.7 * hash01(k, seed + 13)) * period;
	const Vec3 before = offsetOf(t < start ? k - 1 : k), after = offsetOf(t < start ? k : k + 1);
	const double jumpAt = t < start ? start : (k + 1 + 0.7 * hash01(k + 1, seed + 13)) * period;
	i.gaze = lerp(before, after, smoothstep(jumpAt, jumpAt + 0.05, t));
	return i;
}
// The eyes' target and how much they follow it with idle life: what the moment looks at, darting a little round it, or straight ahead (2 m out from the head) when it looks
// at nothing.
inline void idleEyes(const CharacterMoment &m, const Idle &i, Vec3 headLocal, Vec3 &look, double &lookWeight) {
	if (!m.idle) return;
	const Vec3 ahead = transformPoint(m.frame, headLocal * m.scale + Vec3{0.0, 0.0, 2.0 * m.scale});
	const Vec3 base = lookWeight > 0.0 ? look : ahead;
	look = base + i.gaze * (lookWeight > 0.0 ? 0.6 : 2.5) * m.scale;
	lookWeight = std::max(lookWeight, 0.85);
}
// A part's colour from the moment's character line, if it gives one (`sweater red`).
inline void colourFrom(const CharacterMoment &m, const char *part, Vec3 &colour) {
	if (!m.colours) return;
	if (const auto it = m.colours->find(part); it != m.colours->end()) colour = it->second;
}
// How much wider a run's gait is than a walk's (film_file.h, DataFilm::kRunStride: its stride is as much longer).
inline double runWidth(const CharacterMoment &m) { return 1.0 + (film_file::DataFilm::kRunStride - 1.0) * m.running; }
// A to-and-fro at `hz` (a wave, a shake of the head), from the gesture's start.
inline double wiggle(double since, double hz) { return std::sin(2.0 * kPi * hz * since); }

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// Pip (its head is a ball of 0.5 round 0.96 up: what a hand reaches for is outside it)
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// What Pip feels, on its face: brows, lids, eyes, mouth, its ears and its head.
inline void pipFace(const faces::Face &f, actor::Pose &p) {
	p.smile += f.smile;
	p.browRaiseL += f.browRaise;
	p.browRaiseR += f.browRaise;
	p.browTiltL += f.browTilt;
	p.browTiltR += f.browTilt;
	p.lidTilt += f.lidTilt;
	p.eyeWide += f.eyeWide;
	p.blink = lidsWith(f.lids, p.blink);
	p.mouthOpen = std::max(p.mouthOpen, f.mouthOpen);
	p.earDroop += f.droop;
	p.headPitch += f.headPitch;
	p.headRoll += f.headRoll;
}
inline actor::Pose pipWalk(double steps) {
	actor::Pose p;
	p.stepPhase = gaitPhase(steps);
	p.stepAmount = gaitAmount(steps);
	return p;
}
// Where Pip's walk puts a foot (body space, scale 1): as emitActor draws it, before the walk's bob.
inline Vec3 pipFoot(double steps, int s) {
	const actor::Pose p = pipWalk(steps);
	const double side = s == 0 ? -1.0 : 1.0;
	const double ph = p.stepPhase + (side < 0 ? 0.0 : kPi);
	return bodyOfRig(transformPoint(rotate({0, 0, 1}, -(10.0 * p.stepAmount * std::cos(ph))), actor::footPlace(p, side)));
}
inline rig::PoseLibrary<actor::Pose> pipPoses(double S) {
	using P = actor::Pose;
	const auto at = [S](const P &p, Vec3 local) { return rigPoint(p.pos, p.yaw, local * S); };
	rig::PoseLibrary<P> lib;
	lib["wave"] = [at](P &p, double w, double since) {
		reachTo(p.handR, p.handWeightR, at(p, {0.18, 1.00, 0.70 + 0.08 * wiggle(since, 2.5)}), w);
		p.smile += 0.3 * w;
	};
	lib["point"] = [at](P &p, double w, double) { reachTo(p.handR, p.handWeightR, at(p, {0.62, 0.62, 0.22}), w); };
	lib["reach"] = [at](P &p, double w, double) {
		reachTo(p.handR, p.handWeightR, at(p, {0.52, 0.30, 0.18}), w);
		p.lean += 18.0 * w;
	};
	lib["cheer"] = [at](P &p, double w, double) {
		reachTo(p.handL, p.handWeightL, at(p, {0.10, 1.00, -0.72}), w);
		reachTo(p.handR, p.handWeightR, at(p, {0.10, 1.00, 0.72}), w);
		p.smile += 0.5 * w;
		p.earDroop -= w;
	};
	lib["shrug"] = [at](P &p, double w, double) {
		reachTo(p.handL, p.handWeightL, at(p, {0.22, 0.48, -0.50}), w);
		reachTo(p.handR, p.handWeightR, at(p, {0.22, 0.48, 0.50}), w);
		p.browRaiseL += 0.6 * w;
		p.browRaiseR += 0.6 * w;
		p.headRoll += 8.0 * w;
	};
	lib["think"] = [at](P &p, double w, double) {
		reachTo(p.handR, p.handWeightR, at(p, {0.50, 0.58, 0.10}), w);
		p.headRoll += 10.0 * w;
		p.headPitch -= 10.0 * w;
		p.browRaiseL += 0.5 * w;
	};
	lib["look up"] = [](P &p, double w, double) { p.headPitch -= 28.0 * w; };
	lib["look down"] = [](P &p, double w, double) { p.headPitch += 22.0 * w; };
	lib["nod"] = [](P &p, double w, double since) { p.headPitch += 12.0 * w * 0.5 * (1.0 - std::cos(2.0 * kPi * 2.0 * since)); };
	lib["shake head"] = [](P &p, double w, double since) { p.headYaw += 20.0 * w * wiggle(since, 2.0); };
	lib["bow"] = [](P &p, double w, double) {
		p.lean += 18.0 * w;   // (its big head goes a long way down)
		p.headPitch += 10.0 * w;
	};
	lib["hop"] = [](P &p, double w, double since) { p.lift += w * 0.22 * std::abs(std::sin(kPi * since / 0.45)); };
	return lib;
}
inline CharacterKind pipKind() {
	CharacterKind k;
	k.footAt = pipFoot;
	k.plantedAt = gaitPlanted;
	k.footCount = 2;
	k.stride = 0.34;      // a planted foot goes from 0.17 in front to 0.17 behind in a step
	k.firstStep = 1.0;    // (the left comes down a step in)
	k.chest = {0.0, 0.75, 0.0};
	k.scalable = true;
	k.poses = namesOf(pipPoses(1.0));
	k.voice = {290.0, 1.3, 0.04};   // (high and small)
	const auto pose = [](const CharacterMoment &m) {
		actor::Pose p = pipWalk(m.steps);
		p.stepAmount *= runWidth(m);
		placeOf(m.frame, p.pos, p.yaw);
		p.blink = rig::blink(m.t, m.seed);
		p.squash = actor::breathe(m.t);
		pipFace(m.face, p);
		speakWith(m, p.mouthOpen, p.smile);
		reachWith(m, p.handL, p.handWeightL, p.handR, p.handWeightR);
		p.lean += 30.0 * reachingDown(m, 0.35 * m.scale, 0.3 * m.scale);
		lookWith(m, {0.0, 0.96, 0.2}, 70.0, p.headYaw, p.headPitch, p.look, p.lookWeight);
		if (m.idle) {
			const Idle i = idleAt(m.t, m.seed);
			p.headYaw += i.headYaw;
			p.headPitch += i.headPitch;
			p.roll += (m.moving ? 0.3 : 1.0) * 1.8 * i.sway;   // (from foot to foot)
			p.squash *= 1.0 + 0.006 * i.breath;
			idleEyes(m, i, {0.0, 0.96, 0.2}, p.look, p.lookWeight);
		}
		// running leans into it; a jump gathers, stretches in the air and squashes on landing; sitting lifts it onto its seat, its feet forward
		p.lean += 12.0 * m.running;
		p.squash *= (1.0 - 0.22 * m.crouch) * (m.airborne ? 1.12 : 1.0);
		if (m.airborne) p.stepAmount = 0.0;
		p.lean -= 8.0 * m.sit;
		p.lift += m.seat / m.scale * m.sit;
		if (m.feetLocked) {
			p.feetHeld = true;
			for (int s = 0; s < 2; ++s) p.heldFoot[s] = transformPoint(m.frame, m.lockedFeet[s]);
		}
		if (m.sit > 0.0 && p.feetHeld)
			for (int s = 0; s < 2; ++s)
				p.heldFoot[s] = lerp(p.heldFoot[s], transformPoint(m.frame, Vec3{s == 0 ? 0.15 : -0.15, std::max(0.055, m.seat / m.scale - 0.12), 0.30} * m.scale), m.sit);
		return m.gestures && !m.gestures->empty() ? rig::act(p, *m.gestures, m.t, pipPoses(m.scale)) : p;
	};
	k.text = [pose](const CharacterMoment &m) {
		actor::Character c;
		c.scale = m.scale;
		colourFrom(m, "skin", c.skin);
		colourFrom(m, "belly", c.belly);
		colourFrom(m, "feet", c.feet);
		colourFrom(m, "cheeks", c.cheek);
		colourFrom(m, "eyes", c.pupil);
		return actor::emitActor(c, pose(m));
	};
	k.parts = {"skin", "belly", "feet", "cheeks", "eyes"};
	k.handAt = [pose](const CharacterMoment &m, int s) {
		actor::Character c;
		c.scale = m.scale;
		return actor::armOf(c, pose(m), s == 0 ? -1.0 : 1.0).hand;
	};
	k.carry = {0.25, 0.35, 0.45};   // (in front of its belly, below its big head, on the hand's side)
	k.head = {0.0, 0.96, 0.2};
	k.radius = 0.5;   // (its head, wider than its feet)
	k.height = 1.5;
	k.feet = [k](const CharacterMoment &m) { return feetOf(m, k); };
	return k;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// A person
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// What a person feels, on their face (a person's head does not tilt sideways, and their lids do not slant).
inline void personFace(const faces::Face &f, human::HumanPose &p) {
	p.smile += f.smile;
	p.browRaise += f.browRaise;
	p.browTilt += f.browTilt;
	p.eyeWide += f.eyeWide;
	p.blink = lidsWith(f.lids, p.blink);
	p.mouthOpen = std::max(p.mouthOpen, f.mouthOpen);
	p.headPitch += f.headPitch;
}
inline human::HumanPose personWalk(double steps) {
	human::HumanPose p;
	p.walkPhase = gaitPhase(steps);
	p.walkAmount = gaitAmount(steps);
	return p;
}
inline Vec3 personFoot(double steps, int s) {
	const human::HumanPose p = personWalk(steps);
	return bodyOfRig(human::footOf(human::framesOf(p), p, s == 0 ? -1.0 : 1.0));
}
inline rig::PoseLibrary<human::HumanPose> personPoses() {
	using P = human::HumanPose;
	const auto at = [](const P &p, Vec3 local) { return rigPoint(p.pos, p.yaw, local); };
	rig::PoseLibrary<P> lib;
	lib["wave"] = [at](P &p, double w, double since) {
		reachTo(p.handR, p.handWeightR, at(p, {0.15, 1.75, 0.38 + 0.12 * wiggle(since, 2.2)}), w);
		p.smile += 0.3 * w;
	};
	lib["point"] = [at](P &p, double w, double) { reachTo(p.handR, p.handWeightR, at(p, {0.62, 1.35, 0.25}), w); };
	lib["reach"] = [at](P &p, double w, double) {
		reachTo(p.handR, p.handWeightR, at(p, {0.55, 1.00, 0.20}), w);
		p.lean += 15.0 * w;
	};
	lib["cheer"] = [at](P &p, double w, double) {
		reachTo(p.handL, p.handWeightL, at(p, {0.05, 1.95, -0.32}), w);
		reachTo(p.handR, p.handWeightR, at(p, {0.05, 1.95, 0.32}), w);
		p.smile += 0.5 * w;
	};
	lib["shrug"] = [at](P &p, double w, double) {
		reachTo(p.handL, p.handWeightL, at(p, {0.25, 1.05, -0.42}), w);
		reachTo(p.handR, p.handWeightR, at(p, {0.25, 1.05, 0.42}), w);
		p.browRaise += 0.6 * w;
	};
	lib["think"] = [at](P &p, double w, double) {
		reachTo(p.handR, p.handWeightR, at(p, {0.16, 1.52, 0.04}), w);
		p.headPitch -= 8.0 * w;
	};
	lib["hands on hips"] = [at](P &p, double w, double) {
		reachTo(p.handL, p.handWeightL, at(p, {-0.02, 0.98, -0.25}), w);
		reachTo(p.handR, p.handWeightR, at(p, {-0.02, 0.98, 0.25}), w);
	};
	lib["look up"] = [](P &p, double w, double) { p.headPitch -= 30.0 * w; };
	lib["look down"] = [](P &p, double w, double) { p.headPitch += 25.0 * w; };
	lib["nod"] = [](P &p, double w, double since) { p.headPitch += 12.0 * w * 0.5 * (1.0 - std::cos(2.0 * kPi * 2.0 * since)); };
	lib["shake head"] = [](P &p, double w, double since) { p.headYaw += 20.0 * w * wiggle(since, 2.0); };
	lib["bow"] = [](P &p, double w, double) {
		p.lean += 35.0 * w;
		p.headPitch += 10.0 * w;
	};
	lib["crouch"] = [](P &p, double w, double) { p.crouch += 0.8 * w; };
	return lib;
}
inline CharacterKind personKind() {
	CharacterKind k;
	k.footAt = personFoot;
	k.plantedAt = gaitPlanted;
	k.footCount = 2;
	k.stride = 0.60;
	k.firstStep = 1.0;
	k.chest = {0.0, 1.35, 0.0};
	k.poses = namesOf(personPoses());
	k.voice = {140.0, 1.0, 0.04};
	const auto pose = [](const CharacterMoment &m) {
		human::HumanPose p = personWalk(m.steps);
		p.walkAmount *= runWidth(m);
		placeOf(m.frame, p.pos, p.yaw);
		p.blink = rig::blink(m.t, m.seed);
		personFace(m.face, p);
		speakWith(m, p.mouthOpen, p.smile);
		reachWith(m, p.handL, p.handWeightL, p.handR, p.handWeightR);
		const double down = reachingDown(m, 1.1, 0.6);   // (a cup on a low table, a hand held out to someone small: they crouch and bend for it)
		p.crouch += 0.8 * down;
		p.lean += 30.0 * down;
		lookWith(m, {0.0, 1.64, 0.05}, 75.0, p.headYaw, p.headPitch, p.look, p.lookWeight);
		if (m.idle) {
			const Idle i = idleAt(m.t, m.seed);
			p.headYaw += i.headYaw;
			p.headPitch += i.headPitch;
			p.lean += 0.8 * i.breath + (m.moving ? 0.0 : 0.6 * i.sway);   // (a breath, a slow shift of the weight)
			idleEyes(m, i, {0.0, 1.64, 0.05}, p.look, p.lookWeight);
		}
		// running leans into it; a jump crouches before it, tucks its legs in the air and bends its knees to land; sitting lowers the hips to the seat, the feet forward
		p.lean += 10.0 * m.running;
		p.crouch += 0.45 * m.crouch + (m.airborne ? 0.25 : 0.0);
		if (m.airborne) p.walkAmount = 0.0;
		if (m.feetLocked) {
			p.feetHeld = true;
			for (int s = 0; s < 2; ++s) p.heldFoot[s] = transformPoint(m.frame, m.lockedFeet[s]);
		}
		if (m.sit > 0.0) {
			const double hips = std::clamp((1.0 - (m.seat + 0.1) / human::kHip) / 0.42, 0.0, 1.0);   // (the crouch that puts the hips on the seat)
			for (int s = 0; s < 2; ++s) {
				const Vec3 from = p.feetHeld ? p.heldFoot[s] : human::footOf(human::framesOf(p), p, s == 0 ? -1.0 : 1.0);
				p.heldFoot[s] = lerp(from, transformPoint(m.frame, Vec3{s == 0 ? 0.11 : -0.11, 0.055, 0.42}), m.sit);
			}
			p.feetHeld = true;
			p.crouch = std::max(p.crouch, hips * m.sit);
			p.lean -= 4.0 * m.sit;
		}
		return m.gestures && !m.gestures->empty() ? rig::act(p, *m.gestures, m.t, personPoses()) : p;
	};
	k.text = [pose](const CharacterMoment &m) {
		human::Outfit o;
		colourFrom(m, "skin", o.skin);
		colourFrom(m, "hair", o.hair);
		colourFrom(m, "sweater", o.sweater);
		colourFrom(m, "trousers", o.trousers);
		colourFrom(m, "shoes", o.shoes);
		colourFrom(m, "soles", o.sole);
		colourFrom(m, "eyes", o.eyes);
		colourFrom(m, "lips", o.lips);
		colourFrom(m, "cheeks", o.blush);
		return human::emitHuman(pose(m), o);
	};
	k.parts = {"skin", "hair", "sweater", "trousers", "shoes", "soles", "eyes", "lips", "cheeks"};
	k.handAt = [pose](const CharacterMoment &m, int s) { return human::handOf(pose(m), s == 0 ? -1.0 : 1.0); };
	k.carry = {0.20, 1.05, 0.35};
	k.head = {0.0, 1.64, 0.05};
	k.radius = 0.25;
	k.height = 1.8;
	k.feet = [k](const CharacterMoment &m) { return feetOf(m, k); };
	return k;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The turtle
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// What the turtle feels, on its face (its brows: -1 worried .. 1 cross; an angry turtle narrows its eyes).
inline void turtleFace(const faces::Face &f, turtle::TurtlePose &p) {
	p.smile += f.smile;
	p.browRaise += f.browRaise;
	p.browL -= f.browTilt;
	p.browR -= f.browTilt;
	p.squint += 0.4 * std::max(0.0, f.lidTilt);
	p.eyeWide += f.eyeWide;
	p.blink = lidsWith(f.lids, p.blink);
	p.mouthOpen = std::max(p.mouthOpen, f.mouthOpen);
	p.headPitch += f.headPitch;
	p.headRoll += f.headRoll;
}
inline turtle::TurtlePose turtleWalk(double steps) {
	turtle::TurtlePose p;
	p.walkPhase = gaitPhase(steps);
	p.walkAmount = gaitAmount(steps);
	return p;
}
// Its legs: 0 front-left, 1 front-right, 2 hind-left, 3 hind-right; front-left and hind-right step together.
inline int turtlePair(int k) { return (k == 0 || k == 3) ? 0 : 1; }
inline Vec3 turtleFoot(double steps, int k) {
	const turtle::TurtlePose p = turtleWalk(steps);
	return bodyOfRig(turtle::footOf(turtle::framesOf(turtle::Look{}, p), p, k));
}
inline rig::PoseLibrary<turtle::TurtlePose> turtlePoses(double S) {
	using P = turtle::TurtlePose;
	const auto at = [S](const P &p, Vec3 local) { return rigPoint(p.pos, p.yaw, local * S); };
	rig::PoseLibrary<P> lib;
	lib["hide"] = [](P &p, double w, double) {
		p.neck += (0.0 - p.neck) * w;
		p.tuck += (1.0 - p.tuck) * w;
	};
	lib["stretch"] = [](P &p, double w, double) { p.neck += (1.7 - p.neck) * w; };
	lib["wave"] = [at](P &p, double w, double since) { reachTo(p.frontR, p.frontWeightR, at(p, {0.075, 0.045 + 0.012 * wiggle(since, 2.5), 0.06}), w); };
	lib["reach"] = [at](P &p, double w, double) {
		reachTo(p.frontR, p.frontWeightR, at(p, {0.11, 0.012, 0.04}), w);
		p.neck += 0.4 * w;
	};
	lib["rear up"] = [](P &p, double w, double) {
		p.pitch += 25.0 * w;
		p.lift += 0.012 * w;
	};
	lib["swim"] = [](P &p, double w, double since) {
		p.swimAmount = std::max(p.swimAmount, w);
		p.swimPhase = 2.0 * kPi * 1.2 * since;
	};
	lib["look up"] = [](P &p, double w, double) { p.headPitch -= 30.0 * w; };
	lib["look down"] = [](P &p, double w, double) { p.headPitch += 25.0 * w; };
	lib["nod"] = [](P &p, double w, double since) { p.headPitch += 14.0 * w * 0.5 * (1.0 - std::cos(2.0 * kPi * 2.0 * since)); };
	lib["shake head"] = [](P &p, double w, double since) { p.headYaw += 22.0 * w * wiggle(since, 2.0); };
	return lib;
}
inline CharacterKind turtleKind() {
	CharacterKind k;
	k.footAt = turtleFoot;
	k.plantedAt = [](double steps, int leg) { return gaitPlanted(steps, turtlePair(leg)); };
	k.footCount = 4;
	k.stride = 0.028;     // a planted front foot goes from 0.014 in front to 0.014 behind in a step
	k.firstStep = 1.0;
	k.chest = {0.0, 0.035, 0.03};
	k.scalable = true;
	k.poses = namesOf(turtlePoses(1.0));
	k.voice = {360.0, 1.4, 0.06};
	const auto pose = [](const CharacterMoment &m) {
		turtle::TurtlePose p = turtleWalk(m.steps);
		p.walkAmount *= runWidth(m);
		placeOf(m.frame, p.pos, p.yaw);
		p.blink = rig::blink(m.t, m.seed);
		turtleFace(m.face, p);
		speakWith(m, p.mouthOpen, p.smile);
		reachWith(m, p.frontL, p.frontWeightL, p.frontR, p.frontWeightR);
		lookWith(m, {0.0, 0.045, 0.085}, 60.0, p.headYaw, p.headPitch, p.look, p.lookWeight);
		if (m.idle) {
			const Idle i = idleAt(m.t, m.seed);
			p.headYaw += 1.5 * i.headYaw;   // (a turtle's head is restless)
			p.headPitch += i.headPitch;
			p.neck += 0.04 * i.breath;
			idleEyes(m, i, {0.0, 0.045, 0.085}, p.look, p.lookWeight);
		}
		// a jump gathers it and tucks its legs in the air; sitting is lying down on its belly, its legs drawn in
		p.squash *= 1.0 - 0.15 * m.crouch;
		if (m.airborne) p.tuck = std::max(p.tuck, 0.3), p.walkAmount = 0.0;
		p.tuck = std::max(p.tuck, 0.6 * m.sit);
		p.lift += (0.002 - p.lift) * m.sit;
		if (m.feetLocked) {   // (its legs reach for where their feet came down: a turtle's feet follow world targets)
			Vec3 *target[4] = {&p.frontL, &p.frontR, &p.hindL, &p.hindR};
			double *weight[4] = {&p.frontWeightL, &p.frontWeightR, &p.hindWeightL, &p.hindWeightR};
			for (int s = 0; s < 4; ++s) {
				*target[s] = transformPoint(m.frame, m.lockedFeet[s]);
				*weight[s] = 1.0;
			}
		}
		return m.gestures && !m.gestures->empty() ? rig::act(p, *m.gestures, m.t, turtlePoses(m.scale)) : p;
	};
	k.text = [pose](const CharacterMoment &m) {
		turtle::Look look;
		look.scale = m.scale;
		colourFrom(m, "skin", look.skin);
		colourFrom(m, "shell", look.plate);
		colourFrom(m, "seams", look.shell);
		colourFrom(m, "rings", look.ring);
		colourFrom(m, "rim", look.rim);
		colourFrom(m, "belly", look.plastron);
		colourFrom(m, "cheeks", look.blush);
		colourFrom(m, "eyes", look.iris);
		colourFrom(m, "brows", look.brow);
		return turtle::emitTurtle(pose(m), look);
	};
	k.parts = {"skin", "shell", "seams", "rings", "rim", "belly", "cheeks", "eyes", "brows"};
	k.handAt = [pose](const CharacterMoment &m, int s) {
		turtle::Look look;
		look.scale = m.scale;
		const turtle::TurtlePose p = pose(m);
		return turtle::footOf(turtle::framesOf(look, p), p, s);   // (its front feet: 0 the left, 1 the right)
	};
	k.carry = {0.03, 0.03, 0.085};
	k.head = {0.0, 0.045, 0.085};
	k.radius = 0.07;
	k.height = 0.1;
	k.feet = [k](const CharacterMoment &m) { return feetOf(m, k); };
	return k;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The Nomai explorer
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
inline CharacterKind explorerKind() {
	CharacterKind explorer;
	const auto pose = [](const CharacterMoment &m) {
		nomai::ExplorerPose p = m.moving || m.steps > 0.0 ? nomai::walkPose(m.steps, m.t, 1.0) : nomai::standPose(m.t);   // (after a walk: its feet where they came down)
		p.wind = p.wind + m.trail * 0.5;   // (the robe's hem trails the body: nomai::robeTrail)
		p.staffGlow += (1.0 - p.staffGlow) * m.voice;   // (it speaks behind a mask: its staff lights up with its voice, and is as it was when it is silent)
		const Mat4 toBody = invertAffine(m.frame);   // (its hands are placed in body space)
		if (m.hands[0].weight > 0.0) p.handL = lerp(p.handL, transformPoint(toBody, m.hands[0].at), m.hands[0].weight);
		if (m.hands[1].weight > 0.0) p.handR = lerp(p.handR, transformPoint(toBody, m.hands[1].at), m.hands[1].weight);
		p.crouch += 0.8 * reachingDown(m, 1.0, 0.6);
		if (m.look.weight > 0.0) {   // (its head turns left with a positive yaw)
			double yaw = 0.0, pitch = 0.0, weight = 0.0;
			Vec3 look;
			lookWith(m, {0.0, 1.72, 0.08}, 70.0, yaw, pitch, look, weight);
			p.headYaw -= yaw;
			p.headPitch += pitch;
		}
		p.crouch += 0.5 * m.crouch;   // (a jump's gathering and landing)
		p.sit = std::max(p.sit, m.sit);
		if (m.feetLocked) {   // (its feet held where they came down: rig::FootLock)
			p.lockFeet = true;
			p.lockedBall[0] = m.lockedFeet[0];
			p.lockedBall[1] = m.lockedFeet[1];
		}
		return m.gestures && !m.gestures->empty() ? rig::act(p, *m.gestures, m.t, nomai::explorerPoses()) : p;
	};
	explorer.text = [pose](const CharacterMoment &m) { return nomai::explorerText(pose(m), m.frame, m.detail > 0.0 ? m.detail : 0.012); };
	explorer.feet = [pose](const CharacterMoment &m) { return nomai::explorerFeet(pose(m), m.frame); };
	explorer.chest = {0.0, 1.25, 0.0};
	explorer.footAt = [](double steps, int s) { return nomai::explorerBody(nomai::walkPose(steps, 0.0, 1.0)).ball[s]; };
	explorer.plantedAt = [](double steps, int s) { return nomai::explorerBody(nomai::walkPose(steps, 0.0, 1.0)).planted[s]; };
	explorer.stride = 0.84;
	explorer.firstStep = 0.5;
	explorer.trails = true;   // (its robe's hem)
	explorer.poses = namesOf(nomai::explorerPoses());
	explorer.voice = {165.0, 1.05, 0.12};   // (behind a mask: breathier)
	explorer.face = false;                   // (its mask)
	explorer.handAt = [pose](const CharacterMoment &m, int s) {
		const nomai::ExplorerBody b = nomai::explorerBody(pose(m));
		return transformPoint(m.frame, b.wrist[s] + b.handDir[s] * 0.06);
	};
	explorer.carry = {0.18, 1.0, 0.32};
	explorer.hand = 0;
	explorer.head = {0.0, 1.72, 0.08};
	explorer.radius = 0.3;
	explorer.height = 2.1;
	return explorer;
}

// Fills film_file::characterKinds() (before any film file is read: its characters are of these kinds).
inline void install() {
	auto &kinds = film_file::characterKinds();
	kinds["nomai-explorer"] = explorerKind();
	kinds["pip"] = pipKind();
	kinds["person"] = personKind();
	kinds["turtle"] = turtleKind();
}

}  // namespace film_kinds
}  // namespace cartoon
