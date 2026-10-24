#pragma once
// meet_pip.h -- "Meet Pip", an 18 second showreel of the character rig (cartoon_actor.h): Pip says hello, shows what its face can do (surprise, sadness, anger, joy), follows a glowing
// orb with its eyes and head, and walks across the stage. It is the test of the rig and the introduction to the character the cartoon movies are about.
//
// Three shots on a bare stage: a medium close-up for the hello and the faces (0 - 9 s), a close-up as the orb flies round (9 - 12.5 s), and a side-on follow while Pip walks (12.5 - 18 s).
//
// The story space is +x to the right of the picture, +y up, +z towards the camera (every emitted x is negated: pbrt is left-handed).

#include "cartoon_actor.h"

namespace cartoon {
namespace meet_pip {

constexpr double kDuration = 18.0;
constexpr int kFps = 24;
constexpr int kSamples = 128;   // recommended samples per pixel (the Sampler line of the film's header)

inline double fade(double t) { return fadeInOut(t, 0.8, 1.2, kDuration); }

using actor::Character;
using actor::Pose;

inline Vec3 orbAt(double t) {
	const double a = 2.0 * kPi * (t - 9.0) / 3.2;
	const double r = 1.5;
	const Vec3 flight = {r * std::cos(a), 1.35 + 0.35 * std::sin(1.7 * a), 0.2 + 0.9 * std::sin(a)};
	// the orb comes in from beside Pip's hand and goes away again
	return lerp(Vec3{0.9, 1.2, 0.55}, flight, smoothstep(8.6, 9.6, t)) + Vec3{0.0, 0.0, 0.0};
}

// Pip's performance: its pose at time t.
inline Pose poseAt(double t) {
	Pose p;
	p.pos = {-0.3, 0.0, 0.0};
	p.yaw = 55.0;                 // turned a little towards the camera
	p.blink = actor::autoBlink(t, 3);
	p.squash = actor::breathe(t);
	p.smile = 0.35;
	// ---- 0 - 3.2: hello ----
	p.lookWeight = 1.0;
	p.look = {1.5, 1.35, 6.0};   // the camera
	p.headYaw = -12.0 * smoothstep(0.2, 1.0, t);
	p.headPitch = -4.0;
	{
		const double wave = Track1::withEases({{0.8, 0.0, Ease::Smooth}, {1.3, 1.0, Ease::Back}, {3.0, 1.0, Ease::Smooth}, {3.5, 0.0, Ease::Smooth}})(t);
		p.handWeightL = wave;
		const double yawRad = p.yaw * kPi / 180.0;
		const Vec3 front = {std::cos(yawRad), 0.0, std::sin(yawRad)}, right = {-front.z, 0.0, front.x};
		const double flap = std::sin(10.0 * t);
		p.handL = p.pos - right * (0.52 + 0.07 * flap) + front * 0.18 + Vec3{0.0, 1.40 + 0.05 * flap, 0.0};
		p.smile = lerp(0.35, 0.95, wave);
		p.mouthOpen = 0.35 * wave;
		p.browRaiseL = p.browRaiseR = 0.6 * wave;
		p.squash *= 1.0 + 0.05 * wave * std::sin(10.0 * t) * 0.3;
	}
	// ---- 3.6 - 9: the faces ----
	const double surprise = Track1::withEases({{3.6, 0.0, Ease::Smooth}, {3.85, 1.0, Ease::Out}, {4.9, 1.0, Ease::Smooth}, {5.3, 0.0, Ease::Smooth}})(t);
	const double sad = Track1::withEases({{5.3, 0.0, Ease::Smooth}, {6.0, 1.0, Ease::Smooth}, {6.9, 1.0, Ease::Smooth}, {7.4, 0.0, Ease::Smooth}})(t);
	const double angry = Track1::withEases({{7.2, 0.0, Ease::Smooth}, {7.6, 1.0, Ease::Back}, {8.3, 1.0, Ease::Smooth}, {8.7, 0.0, Ease::Smooth}})(t);
	// surprise: wide eyes, brows up, mouth open, a little jump back
	p.eyeWide += 0.55 * surprise;
	p.browRaiseL += 1.0 * surprise;
	p.browRaiseR += 1.0 * surprise;
	p.mouthOpen += 0.9 * surprise;
	p.smile -= 0.55 * surprise;
	p.lift += 0.10 * surprise * (t < 3.95 ? 1.0 : 0.0);
	p.squash *= 1.0 + 0.10 * surprise;
	p.earDroop -= 1.0 * surprise;
	// sad: slumped, head down, lids and brows slanted up in the middle, a frown, ears down
	p.headPitch += 20.0 * sad;
	p.lookWeight = lerp(p.lookWeight, 0.0, sad);
	p.smile = lerp(p.smile, -0.8, sad);
	p.lidTilt -= 0.9 * sad;
	p.browTiltL += 1.0 * sad;
	p.browTiltR += 1.0 * sad;
	p.earDroop += 1.4 * sad;
	p.squash *= 1.0 - 0.07 * sad;
	p.blink = std::max(p.blink, 0.28 * sad);
	// angry: a scowl, a lean forward, the brows in a V
	p.lidTilt += 0.9 * angry;
	p.browTiltL -= 1.3 * angry;
	p.browTiltR -= 1.3 * angry;
	p.browRaiseL -= 0.5 * angry;
	p.browRaiseR -= 0.5 * angry;
	p.smile = lerp(p.smile, -0.6, angry);
	p.lean += 12.0 * angry;
	p.squash *= 1.0 - 0.05 * angry;
	// joy: the biggest smile and a hop (8.9 - 9.6)
	const double joy = Track1::withEases({{8.7, 0.0, Ease::Smooth}, {9.0, 1.0, Ease::Out}, {9.5, 1.0, Ease::Smooth}, {9.9, 0.0, Ease::Smooth}})(t);
	const double hopU = clamp01((t - 8.9) / 0.6);
	p.lift += 0.35 * 4.0 * hopU * (1.0 - hopU) * (t >= 8.9 && t <= 9.5 ? 1.0 : 0.0);
	p.smile = lerp(p.smile, 1.0, joy);
	p.mouthOpen += 0.8 * joy;
	p.earDroop -= 0.8 * joy;
	p.browRaiseL += 0.5 * joy;
	p.browRaiseR += 0.5 * joy;
	// ---- 9.3 - 12.5: the orb ----
	const double orb = Track1::withEases({{9.3, 0.0, Ease::Smooth}, {9.8, 1.0, Ease::Smooth}, {12.0, 1.0, Ease::Smooth}, {12.6, 0.0, Ease::Smooth}})(t);
	if (orb > 0.0) {
		p.look = lerp(p.look, orbAt(t), orb);
		p.lookWeight = std::max(p.lookWeight, orb);
		const Vec3 toOrb = orbAt(t) - p.pos;
		p.headYaw = lerp(p.headYaw, std::max(-24.0, std::min(24.0, std::atan2(toOrb.z, toOrb.x) * 180.0 / kPi * 0.5 - 20.0)), orb);
		p.headPitch = lerp(p.headPitch, -6.0 * std::sin(1.7 * 2.0 * kPi * (t - 9.0) / 3.2) - 6.0, orb);
		p.smile = lerp(p.smile, 0.8, orb);
		p.mouthOpen += 0.25 * orb;
	}
	// ---- 12.5 - 18: the walk ----
	const double walk = Track1::withEases({{12.6, 0.0, Ease::Smooth}, {13.2, 1.0, Ease::Smooth}, {16.4, 1.0, Ease::Smooth}, {17.0, 0.0, Ease::Smooth}})(t);
	if (t > 12.4) {
		const double x = Track1::withEases({{12.6, -0.3, Ease::Smooth}, {17.0, 3.6, Ease::Smooth}})(t);
		p.pos = {x, 0.0, 0.0};
		p.yaw = lerp(55.0, 0.0, smoothstep(12.5, 13.2, t));
		p.stepAmount = walk;
		p.stepPhase = 2.0 * kPi * 1.9 * (t - 12.6);
		p.lookWeight = 0.0;
		p.headYaw = lerp(p.headYaw, 0.0, smoothstep(12.4, 13.0, t));
		p.headPitch = lerp(p.headPitch, -4.0, smoothstep(12.4, 13.0, t));
		p.smile = lerp(p.smile, 0.7, smoothstep(12.4, 13.0, t));
		p.lean = 5.0 * walk;
		p.handWeightL = p.handWeightR = 0.0;
		// a little skip at the end, and a happy look to the camera
		const double end = smoothstep(16.8, 17.4, t);
		p.yaw = lerp(p.yaw, 60.0, end);
		p.lookWeight = end;
		p.look = {p.pos.x + 1.0, 1.4, 6.0};
		p.smile = lerp(p.smile, 1.0, end);
		p.mouthOpen += 0.5 * end;
	}
	return p;
}

inline const std::vector<Shot> &shots() {
	static const std::vector<Shot> s = {
		{0.0, Track3::withEases({{0.0, {1.9, 1.55, 6.4}, Ease::Smooth}, {9.0, {1.2, 1.5, 5.4}, Ease::Smooth}}), Track3::withEases({{0.0, {-0.3, 1.05, 0.0}, Ease::Smooth}, {9.0, {-0.3, 1.2, 0.0}, Ease::Smooth}}),
		 Track1::withEases({{0.0, 30.0, Ease::Smooth}}), 0.03},
		{9.0, Track3::withEases({{9.0, {0.6, 1.5, 4.6}, Ease::Smooth}, {12.5, {-0.2, 1.4, 4.4}, Ease::Smooth}}), Track3::withEases({{9.0, {-0.3, 1.3, 0.0}, Ease::Smooth}, {12.5, {-0.3, 1.3, 0.0}, Ease::Smooth}}),
		 Track1::withEases({{9.0, 32.0, Ease::Smooth}}), 0.03},
		{12.5, sampledTrack(12.5, kDuration, [](double t) { return Vec3{poseAt(t).pos.x + 0.6, 1.0, 5.6}; }), sampledTrack(12.5, kDuration, [](double t) { return Vec3{poseAt(t).pos.x + 0.7, 0.95, 0.0}; }),
		 Track1::withEases({{12.5, 36.0, Ease::Smooth}}), 0.03},
	};
	return s;
}
inline std::vector<double> shotStarts() { return startsOf(shots()); }

// Everything up to and including WorldBegin: the camera at t, the picture's size, the sampler and the integrator.
inline std::string headerAt(double t) { return sceneHeader(cameraText(cameraAt(shots(), t)), kSamples); }

inline std::string sceneAt(double t) {
	const double f = fade(t);
	std::string out = headerAt(t);
	char buf[200];
	std::snprintf(buf, sizeof(buf), "LightSource \"infinite\" \"rgb L\" [ %.4f %.4f %.4f ]\n", f * 0.20, f * 0.22, f * 0.30);
	out += buf;
	// a stage: a warm soft box from the upper left, a cool one from the right, and a rim from behind
	const Vec3 key = {f * 7.0, f * 6.0, f * 5.0}, fill = {f * 1.8, f * 2.6, f * 4.2}, rim = {f * 6.0, f * 5.0, f * 6.5};
	out += attr(diffuseMaterial({0.1, 0.1, 0.1}), emitMesh(box(5.0, 0.2, 3.0), mul(translate(-3.0, 6.0, 4.0), rotate({1, 0, 0}, -30.0), rotate({0, 0, 1}, 20.0))), &key);
	out += attr(diffuseMaterial({0.1, 0.1, 0.1}), emitMesh(box(4.0, 0.2, 3.0), mul(translate(5.0, 4.5, 4.0), rotate({1, 0, 0}, -25.0), rotate({0, 0, 1}, -25.0))), &fill);
	out += attr(diffuseMaterial({0.1, 0.1, 0.1}), emitMesh(box(4.0, 0.2, 2.0), mul(translate(-1.0, 5.0, -5.0), rotate({1, 0, 0}, 30.0))), &rim);
	// the floor and the backdrop
	out += "Texture \"stage\" \"spectrum\" \"checkerboard\" \"float uscale\" [ 30 ] \"float vscale\" [ 30 ] \"rgb tex1\" [ 0.86 0.84 0.80 ] \"rgb tex2\" [ 0.78 0.76 0.74 ]\n";
	out += attr("Material \"coateddiffuse\" \"texture reflectance\" [ \"stage\" ] \"float roughness\" [ 0.10 ]", emitMesh(box(60.0, 1.0, 40.0), translate(0.0, -0.5, 0.0)));
	out += attr(diffuseMaterial({0.35, 0.50, 0.62}), emitMesh(box(80.0, 40.0, 0.5), translate(0.0, 18.0, -7.5)));
	// Pip, and the orb
	Character pip;
	out += actor::emitActor(pip, poseAt(t));
	const double orbOn = Track1::withEases({{8.4, 0.0, Ease::Smooth}, {9.2, 1.0, Ease::Smooth}, {12.0, 1.0, Ease::Smooth}, {12.8, 0.0, Ease::Smooth}})(t);
	if (orbOn > 0.01) {
		const Vec3 L = {f * orbOn * 30.0, f * orbOn * 22.0, f * orbOn * 8.0};
		out += emissiveSphere(orbAt(t), 0.11, L);
	}
	return out;
}

}  // namespace meet_pip
}  // namespace cartoon
