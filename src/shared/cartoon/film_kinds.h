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
// A hand target eased in by a gesture's weight: the first gesture on a hand puts it there; a later one moves it from where the earlier one had it.
inline void reachTo(Vec3 &hand, double &weight, Vec3 target, double w) {
	hand = weight > 0.0 ? lerp(hand, target, w) : target;
	weight = std::max(weight, w);
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
		placeOf(m.frame, p.pos, p.yaw);
		p.blink = rig::blink(m.t, m.seed);
		p.squash = actor::breathe(m.t);
		pipFace(m.face, p);
		speakWith(m, p.mouthOpen, p.smile);
		if (m.feetLocked) {
			p.feetHeld = true;
			for (int s = 0; s < 2; ++s) p.heldFoot[s] = transformPoint(m.frame, m.lockedFeet[s]);
		}
		return m.gestures && !m.gestures->empty() ? rig::act(p, *m.gestures, m.t, pipPoses(m.scale)) : p;
	};
	k.text = [pose](const CharacterMoment &m) {
		actor::Character c;
		c.scale = m.scale;
		return actor::emitActor(c, pose(m));
	};
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
		placeOf(m.frame, p.pos, p.yaw);
		p.blink = rig::blink(m.t, m.seed);
		personFace(m.face, p);
		speakWith(m, p.mouthOpen, p.smile);
		if (m.feetLocked) {
			p.feetHeld = true;
			for (int s = 0; s < 2; ++s) p.heldFoot[s] = transformPoint(m.frame, m.lockedFeet[s]);
		}
		return m.gestures && !m.gestures->empty() ? rig::act(p, *m.gestures, m.t, personPoses()) : p;
	};
	k.text = [pose](const CharacterMoment &m) { return human::emitHuman(pose(m)); };
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
		placeOf(m.frame, p.pos, p.yaw);
		p.blink = rig::blink(m.t, m.seed);
		turtleFace(m.face, p);
		speakWith(m, p.mouthOpen, p.smile);
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
		return turtle::emitTurtle(pose(m), look);
	};
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
	explorer.poses = namesOf(nomai::explorerPoses());
	explorer.voice = {165.0, 1.05, 0.12};   // (behind a mask: breathier)
	explorer.face = false;                   // (its mask)
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
