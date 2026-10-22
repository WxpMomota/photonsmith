#pragma once
// pip_and_the_moon.h -- "Pip and the Moon", a 96 second story in 13 shots: a small blue creature (Pip, cartoon_actor.h) finds the moon in a pond, tries to touch it and breaks it, fetches a
// bucket and scoops it out, dances with it, trips on the way home and loses it, and is comforted by a firefly that shows it the real moon. The timeline, which the music shares, is in
// pip_moon_story.h; the music and the sounds are in pip_moon_score.h.
//
// What is real: the moon is a bright sphere in the sky and its reflection in the pond is the path tracer's own (a camera on the line from the moon's mirror image through the water point
// sees the reflection there, so every camera used for the reflection scenes sits on that line, `reflectionCamera`). The pond is a height field rebuilt for every frame from the list of
// blows it has received (a finger, the bucket, the falling moon); its slopes break the moon's reflection into glitter and put it back together as the ripples die. The moon Pip carries is
// a small emissive sphere in a bucket, and lights Pip's face, the dock and the fireflies that gather round it.
//
// The story space is +x to the right of the picture, +y up, +z towards the camera (every emitted x is negated: pbrt is left-handed).

#include "cartoon_actor.h"
#include "pip_moon_score.h"
#include "pip_moon_story.h"

namespace cartoon {
namespace pip_moon {

constexpr int kSamples = 64;   // recommended samples per pixel (the Sampler line of the film's header): the denoiser makes 64 look like 128 at half the time

using actor::Character;
using actor::Pose;

inline double fade(double t) { return smoothstep(0.0, 2.0, t) * (1.0 - smoothstep(kDuration - 2.5, kDuration, t)); }
inline double trap(double t, double a, double b, double c, double d) { return smoothstep(a, b, t) * (1.0 - smoothstep(c, d, t)); }
inline Vec3 frontOf(double yawDeg) {
	const double y = yawDeg * kPi / 180.0;
	return {std::cos(y), 0.0, std::sin(y)};
}
inline Vec3 rightOf(double yawDeg) {
	const Vec3 f = frontOf(yawDeg);
	return {-f.z, 0.0, f.x};
}
inline Vec3 above(Vec3 p, double h) { return {p.x, p.y + h, p.z}; }
// a hop: a half sine of height h over `dur` seconds from t0
inline double hop(double t, double t0, double dur, double h) {
	const double u = (t - t0) / dur;
	return (u < 0.0 || u > 1.0) ? 0.0 : h * std::sin(kPi * u);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// Pip's route, apart from the walking: sideways to the dock's edge, the fall, the sitting
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
inline double zOf(double t) {
	static const Track1 z = Track1::withEases({{0.0, 0.0, Ease::Smooth}, {70.0, 0.0, Ease::Smooth}, {71.6, 0.72, Ease::Smooth}});
	return z(t);
}
inline double xExtraOf(double t) {
	static const Track1 x = Track1::withEases({{0.0, 0.0, Ease::Smooth}, {66.4, 0.0, Ease::Smooth}, {67.2, -0.45, Ease::Out}, {69.4, -0.45, Ease::Smooth}, {71.4, -0.9, Ease::Smooth}});
	return x(t);
}
inline double yawOf(double t) {
	static const Track1 y = Track1::withEases({{0.0, 12.0, Ease::Smooth}, {13.8, 22.0, Ease::Smooth}, {16.2, 22.0, Ease::Smooth}, {31.4, 22.0, Ease::Smooth}, {32.6, 180.0, Ease::Smooth}, {39.9, 180.0, Ease::Smooth},
												 {40.5, 360.0, Ease::Smooth}, {43.0, 360.0, Ease::Smooth}, {43.9, 382.0, Ease::Smooth}, {47.5, 382.0, Ease::Smooth}, {48.0, 415.0, Ease::Smooth},
												 {52.4, 415.0, Ease::Smooth}, {54.6, 775.0, Ease::Smooth}, {56.0, 775.0, Ease::Smooth}, {57.6, 900.0, Ease::Smooth}, {71.2, 900.0, Ease::Smooth},
												 {72.6, 810.0, Ease::Smooth}, {82.2, 810.0, Ease::Smooth}, {84.0, 630.0, Ease::Smooth}});
	return y(t);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// Where Pip looks
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The firefly that comes to Pip: it arrives from the water, hovers in front of its face, and rises towards the moon.
inline Vec3 fireflyStar(double t) {
	static const Track3 p = Track3::withEases({{78.0, {-2.6, 1.9, 4.0}, Ease::Smooth}, {79.6, {-5.0, 1.25, 1.7}, Ease::Smooth}, {80.2, {-5.0, 1.3, 1.65}, Ease::Smooth}, {82.4, {-6.2, 5.6, -2.4}, Ease::Smooth},
												{84.0, {-7.0, 8.0, -9.0}, Ease::Smooth}});
	return p(t) + Vec3{0.05 * std::sin(9.0 * t), 0.04 * std::sin(7.0 * t + 1.0), 0.05 * std::cos(8.0 * t)};
}

// Roughly where the moon in the bucket is, when Pip carries it (the bucket itself depends on the pose, which depends on this).
inline Vec3 carryPoint(const Pose &p, double height) { return p.pos + frontOf(p.yaw) * 0.18 + rightOf(p.yaw) * 0.45 + Vec3{0.0, height, 0.0}; }

inline Vec3 gazeAt(double t, const Pose &p) {
	const Vec3 ahead = p.pos + frontOf(p.yaw) * 2.5 + Vec3{0.0, 0.7, 0.0};
	const Vec3 carry = carryPoint(p, kDock + 0.95);
	struct Key {
		double t;
		Vec3 v;
	};
	const std::vector<Key> keys = {
		{0.0, moonCentre()},        {8.0, moonCentre()},         {10.0, p.pos + Vec3{2.5, 0.9, 3.5}}, {13.6, {2.2, 0.5, 1.4}},          {15.4, {2.2, 0.5, 1.4}},          {16.4, kTouch},
		{31.0, kTouch},             {31.6, kBucketRest + Vec3{0.0, 0.4, 0.0}},                   {38.2, kBucketRest + Vec3{0.0, 0.4, 0.0}}, {38.9, kBucketRest + Vec3{0.0, 0.7, 0.0}},
		{40.0, kBucketRest + Vec3{0.0, 0.7, 0.0}}, {40.6, ahead}, {43.0, ahead},                    {43.9, kTouch},                   {44.8, kTouch},
		{45.4, carry},              {66.2, carry},               {67.0, kSplash + Vec3{0.0, 0.5, 0.0}}, {78.4, kSplash + Vec3{0.0, 0.5, 0.0}}, {79.4, fireflyStar(79.4)},
		{80.2, fireflyStar(80.2)},  {82.3, fireflyStar(82.3)},   {83.4, moonCentre()},              {kDuration, moonCentre()},
	};
	if (t <= keys.front().t) return keys.front().v;
	for (std::size_t i = 1; i < keys.size(); ++i) {
		if (t <= keys[i].t) {
			const double u = (t - keys[i - 1].t) / (keys[i].t - keys[i - 1].t);
			// a moving target (the firefly) is followed exactly rather than blended
			if (t >= 79.4 && t <= 82.3) return fireflyStar(t);
			return lerp(keys[i - 1].v, keys[i].v, u * u * (3.0 - 2.0 * u));
		}
	}
	return keys.back().v;
}

// The head follows the gaze for a share of the turn (the eyes do the rest).
inline void headToward(Pose &p, Vec3 look, double share) {
	const Vec3 head = p.pos + Vec3{0.0, 1.15, 0.0};
	const Vec3 d = look - head;
	const double horizontal = std::hypot(d.x, d.z);
	const double up = std::atan2(d.y, horizontal) * 180.0 / kPi;
	const double yawWorld = std::atan2(d.z, d.x) * 180.0 / kPi;
	const double rel = actor::wrapDegrees(yawWorld - p.yaw);
	p.headPitch = std::max(-38.0, std::min(30.0, -up * share));
	p.headYaw = std::max(-55.0, std::min(55.0, rel * share));
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// Pip's performance
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
inline Pose poseAt(double t) {
	Pose p;
	p.pos = {walkX(t) + xExtraOf(t), kDock, zOf(t)};
	p.yaw = yawOf(t);
	p.stepPhase = stepPhaseAt(t);
	p.stepAmount = stepAmountAt(t);
	p.blink = actor::autoBlink(t, 5);
	p.squash = actor::breathe(t);
	p.smile = 0.35;
	p.lookWeight = 1.0;
	const Vec3 fr = frontOf(p.yaw), rt = rightOf(p.yaw);
	// ---- faces ----
	const double surprise = trap(t, kNotice, kNotice + 0.25, 18.9, 19.5);
	const double delight = trap(t, 18.8, 19.3, 21.1, 21.8);
	const double focus = trap(t, 21.6, 22.3, 24.0, 24.3);
	const double oops = trap(t, kTouched, kTouched + 0.3, 25.6, 26.6);
	const double sad = trap(t, 25.6, 27.0, 30.4, 31.3);
	const double idea = trap(t, kIdea, kIdea + 0.3, 33.0, 34.2);
	const double determined = trap(t, 35.0, 35.6, 40.0, 40.8);
	const double wonder = trap(t, 44.8, 45.3, 47.0, 47.8);
	const double joy = trap(t, 46.4, 47.6, 55.8, 57.4);
	const double tender = trap(t, 57.0, 58.2, 65.6, 66.3);
	const double alarm = trap(t, 66.3, 66.6, 68.9, 69.4);
	const double despair = trap(t, 69.0, 70.6, 78.2, 79.2);
	const double hope = trap(t, 78.6, 79.5, 81.8, 82.4);
	const double awe = trap(t, 82.0, 83.2, 90.5, 94.5);
	p.eyeWide = 1.0 + 0.55 * surprise + 0.25 * delight + 0.35 * oops + 0.35 * idea + 0.3 * wonder + 0.45 * alarm + 0.25 * hope + 0.2 * awe;
	p.browRaiseL = p.browRaiseR = 1.0 * surprise + 0.5 * delight + 0.7 * oops + 0.9 * idea + 0.7 * wonder + 0.9 * alarm + 0.6 * hope + 0.4 * awe - 0.3 * focus;
	p.browTiltL = p.browTiltR = 1.0 * oops + 1.1 * sad + 1.0 * despair + 0.6 * hope - 0.7 * focus - 0.5 * determined;
	p.lidTilt = -0.9 * sad - 0.9 * despair - 0.5 * oops + 0.5 * focus + 0.4 * determined;
	p.smile = 0.35 + 0.65 * delight + 0.55 * joy + 0.4 * wonder + 0.3 * tender + 0.5 * idea + 0.4 * determined + 0.5 * awe - 0.9 * oops - 1.3 * sad - 1.3 * despair - 1.0 * alarm - 0.3 * surprise - 0.2 * focus;
	p.earDroop = -1.0 * surprise - 0.6 * delight - 1.0 * idea - 0.7 * wonder - 0.5 * joy - 0.4 * tender - 0.8 * hope - 0.6 * awe + 1.5 * sad + 1.5 * despair + 1.0 * oops - 0.7 * alarm;
	p.mouthOpen = mouthOf(t) + 0.25 * focus;
	p.squash *= 1.0 + 0.07 * surprise - 0.07 * sad - 0.08 * despair + 0.04 * joy;
	p.blink = std::max(p.blink, 0.30 * sad + 0.35 * despair);
	// ---- gaze ----
	p.look = gazeAt(t, p);
	headToward(p, p.look, 0.65);
	p.headRoll = 14.0 * hope + 6.0 * sad;
	p.headPitch += 12.0 * sad + 7.0 * despair;
	// ---- the first walk: a stroll, a look round, and at the end a turn to the water ----
	p.lean = 4.0 * std::min(1.0, p.stepAmount);
	// ---- the moon in the water ----
	p.lift += hop(t, kNotice - 0.05, 0.35, 0.10) + hop(t, 19.3, 0.3, 0.13) + hop(t, 19.8, 0.3, 0.13) + hop(t, 20.3, 0.3, 0.13);
	const double clasp = trap(t, 18.9, 19.4, 21.2, 21.7);
	if (clasp > 0.0) {
		p.handWeightL = p.handWeightR = clasp;
		p.handL = p.pos + fr * 0.34 - rt * 0.10 + Vec3{0.0, 0.74, 0.0};
		p.handR = p.pos + fr * 0.34 + rt * 0.10 + Vec3{0.0, 0.74, 0.0};
	}
	const double reach = smoothstep(21.2, 23.0, t) * (1.0 - smoothstep(24.5, 26.2, t));
	p.lean += 80.0 * reach;
	p.squash *= 1.0 - 0.10 * reach;
	const double touch = trap(t, 21.8, 23.4, 24.4, 25.6);
	if (touch > 0.0) {
		p.handWeightR = std::max(p.handWeightR, touch);
		p.handR = kTouch + Vec3{0.0, 0.04 - 0.07 * smoothstep(23.9, kTouched, t) + 0.05 * smoothstep(kTouched, kTouched + 0.5, t), 0.0};
	}
	// ---- the idea, the run, the bucket ----
	p.lift += hop(t, kIdea + 0.05, 0.35, 0.16);
	p.lean += 10.0 * determined * (walkSpeed(t) > 0.3 ? 1.0 : 0.0);
	const double bend = trap(t, 38.6, 39.3, 39.6, 40.4);
	p.lean += 38.0 * bend;
	p.squash *= 1.0 - 0.22 * bend;
	const Vec3 handle = kBucketRest + Vec3{0.0, 0.70, 0.0};
	const Vec3 carryTarget = carryPoint(p, kDock + 0.78);
	if (t >= 38.7 && t < kDip - 0.6) {
		double w = smoothstep(38.7, 39.4, t);
		Vec3 target = handle;
		if (t > kPick) target = lerp(handle, carryTarget, smoothstep(kPick + 0.1, kPick + 0.9, t));
		p.handWeightR = std::max(p.handWeightR, w);
		p.handR = target;
	}
	// ---- the dip ----
	const double dip = smoothstep(43.5, kDip - 0.4, t) * (1.0 - smoothstep(45.7, 47.2, t));
	p.lean += 72.0 * dip;
	p.squash *= 1.0 - 0.10 * dip;
	if (t >= kDip - 0.8 && t < 47.6) {
		const double down = smoothstep(kDip - 0.8, kDip - 0.1, t) * (1.0 - smoothstep(45.6, 46.8, t));
		const Vec3 target = lerp(carryTarget, kTouch + Vec3{0.0, 0.32, 0.0}, down);
		p.handWeightR = 1.0;
		p.handR = target;
	}
	// ---- the dance ----
	if (t >= 47.5 && t < 57.2) {
		const double d = smoothstep(47.5, 48.4, t) * (1.0 - smoothstep(56.0, 57.2, t));
		const double beat = 60.0 / 76.0;
		const double sway = std::sin(2.0 * kPi * (t - 48.0) / (2.0 * beat));
		p.lift += d * 0.16 * std::pow(std::sin(kPi * std::fmod(t - 48.0, beat) / beat), 2.0) * (t > 48.0 ? 1.0 : 0.0);
		p.roll += 7.0 * d * sway;
		const double raised = d;
		p.handWeightR = std::max(p.handWeightR, 1.0);
		p.handR = lerp(carryTarget, p.pos + fr * 0.28 + rt * 0.25 + Vec3{0.0, kDock + 1.0 + 0.10 * std::sin(2.0 * kPi * (t - 48.0) / beat), 0.0}, raised);
		p.handWeightL = d;
		p.handL = p.pos + fr * 0.2 - rt * 0.5 + Vec3{0.0, kDock + 0.55 + 0.25 * sway, 0.0};
		p.lean += 6.0 * d;
	}
	// ---- carrying it home ----
	if (t >= 56.8 && t < kRelease) {
		p.handWeightR = 1.0;
		p.handR = lerp(p.handR, carryTarget, smoothstep(56.8, 57.6, t));
		if (t > 57.6) p.handR = carryTarget + Vec3{0.0, 0.02 * std::sin(2.0 * kPi * 1.5 * (t - 57.6)), 0.0};
	}
	// ---- the fall ----
	const double fallLean = smoothstep(kTrip, 67.2, t) * (1.0 - smoothstep(69.4, 71.0, t));
	p.lean += 84.0 * fallLean;
	p.lift += hop(t, kTrip - 0.1, 0.3, 0.07);
	const double flail = trap(t, kTrip + 0.05, kTrip + 0.35, 67.4, 67.9);
	if (flail > 0.0) {
		p.handWeightL = p.handWeightR = flail;
		p.handR = p.pos + fr * 0.8 + rt * 0.35 + Vec3{0.0, kDock + 0.75, 0.0};
		p.handL = p.pos + fr * 0.8 - rt * 0.35 + Vec3{0.0, kDock + 0.75, 0.0};
		if (t < kRelease + 0.05) p.handR = carryTarget + fr * 0.25;
	}
	// ---- sitting at the edge, then standing to see the moon ----
	const double sit = smoothstep(71.0, 72.6, t) * (1.0 - smoothstep(82.0, 83.4, t));
	p.squash *= 1.0 - 0.22 * sit;
	p.lean += 8.0 * sit;
	if (t >= 84.0) {
		// the wave: the right hand up and swinging; the left hand a little out
		const double wave = trap(t, 85.5, 86.4, 91.0, 92.2);
		p.handWeightR = std::max(p.handWeightR, wave);
		p.handR = p.pos + rt * 0.58 + fr * 0.12 + Vec3{0.0, kDock + 1.02 + 0.07 * std::sin(10.0 * t), 0.0};
		p.handWeightL = std::max(p.handWeightL, 0.5 * wave);
		p.handL = p.pos - rt * 0.55 + Vec3{0.0, kDock + 0.62, 0.0};
		p.squash *= 1.0 - 0.22 * smoothstep(91.8, 93.4, t);
	}
	p.lift += hop(t, 85.0, 0.3, 0.1) + hop(t, 86.0, 0.3, 0.08);
	// when the body leans over, the head stays up to look where it is going
	p.headPitch -= 0.8 * p.lean;
	return p;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The bucket and the moon in it
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
struct BucketState {
	Vec3 pos;      // the middle of its base
	double tilt;   // degrees about z: leaning over, or lying on its side
	bool full;     // water in it
};
// Where the bucket hangs from Pip's right hand (once Pip is falling it is held clear of the dock, which it would otherwise sink into).
inline Vec3 hung(double t) {
	Vec3 p = actor::armOf(Character{}, poseAt(t), 1.0).hand - Vec3{0.0, 0.70, 0.0};
	if (t > kTrip - 0.3) p.y = std::max(p.y, kDock + 0.1);
	return p;
}
inline Vec3 bucketVelocity() { return {-1.3, 1.6, -0.5}; }
// Where the bucket leaves Pip's hand.
inline Vec3 releasePoint() { return hung(kRelease); }

inline BucketState bucketAt(double t) {
	if (t < kPick) return {kBucketRest, 0.0, false};
	const bool full = t >= kDip + 0.4;
	if (t < kRelease) {
		const Vec3 h = hung(t);
		const double s = smoothstep(kPick, kPick + 0.45, t);
		const double sway = 5.0 * std::sin(2.0 * kPi * 1.3 * t) * std::min(1.0, 0.4 + walkSpeed(t));
		return {lerp(kBucketRest, h, s), sway, full};
	}
	// flung: it falls to the dock, bounces over and lies on its side
	const Vec3 p0 = releasePoint(), v = bucketVelocity();
	const double s = t - kRelease;
	const double sLand = (v.y + std::sqrt(v.y * v.y + 19.6 * (p0.y - kDock))) / 9.8;
	if (s < sLand) return {{p0.x + v.x * s, p0.y + v.y * s - 4.9 * s * s, p0.z + v.z * s}, -160.0 * s, true};
	const double u = s - sLand;
	const double lying = -(110.0 + 40.0 * std::exp(-4.0 * u) * std::sin(18.0 * u));
	return {{p0.x + v.x * sLand + v.x * (1.0 - std::exp(-4.0 * u)) / 4.0, kDock + 0.26 * std::abs(std::sin(lying * kPi / 180.0)), p0.z + v.z * sLand}, lying, false};
}

struct GlowState {
	Vec3 pos;
	double power;   // 0 .. 1
};
inline GlowState glowAt(double t) {
	if (t < kGlow) return {{0.0, -5.0, 0.0}, 0.0};
	const double rise = smoothstep(kGlow, kGlow + 0.7, t);
	if (t < kRelease) {
		const BucketState b = bucketAt(t);
		return {b.pos + Vec3{0.0, 0.47 + 0.02 * std::sin(3.0 * t), 0.0}, rise};
	}
	const Vec3 p0 = releasePoint() + Vec3{0.0, 0.47, 0.0};
	const double flight = kSplashT - kRelease;
	const Vec3 v = {(kSplash.x - p0.x) / flight, (0.12 - p0.y + 4.9 * flight * flight) / flight, (kSplash.z - p0.z) / flight};
	const double s = t - kRelease;
	if (t < kSplashT) return {{p0.x + v.x * s, p0.y + v.y * s - 4.9 * s * s, p0.z + v.z * s}, 1.0};
	const double u = t - kSplashT;
	return {{kSplash.x, 0.12 - 0.35 * u, kSplash.z}, 1.0 - smoothstep(0.0, kBurnOut - kSplashT, u)};
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The pond
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
constexpr double kPondX0 = -8.5, kPondX1 = 7.5, kPondZ0 = -1.6, kPondZ1 = 7.0;
constexpr double kPondCell = 0.14;

struct Surface {
	double h, dhdx, dhdz;
};
inline Surface surfaceAt(double x, double z, double t) {
	Surface s = {0.0, 0.0, 0.0};
	const double k = 2.0 * kPi / 0.50;
	for (const Ripple &r : ripples()) {
		const double age = t - r.t0;
		if (age < 0.0 || age > 7.0) continue;
		const double dx = x - r.x, dz = z - r.z;
		const double d = std::sqrt(dx * dx + dz * dz) + 1e-9;
		const double R = 1.5 * age, w = 0.16 + 0.24 * age;
		const double amp = 0.040 * r.amp * std::exp(-age / 1.8) / (1.0 + 1.2 * age);
		const double q = (d - R) / w;
		const double g = std::exp(-q * q), ph = k * (d - R);
		s.h += amp * std::sin(ph) * g;
		const double dhdr = amp * (k * std::cos(ph) * g + std::sin(ph) * g * (-2.0 * q / w));
		s.dhdx += dhdr * dx / d;
		s.dhdz += dhdr * dz / d;
	}
	// a faint breathing of the whole pond
	s.h += 0.0015 * std::sin(1.1 * x + 0.6 * t) + 0.001 * std::sin(1.7 * z - 0.8 * t);
	s.dhdx += 0.0015 * 1.1 * std::cos(1.1 * x + 0.6 * t);
	s.dhdz += 0.001 * 1.7 * std::cos(1.7 * z - 0.8 * t);
	// nothing at the edge of the grid, so that it joins the flat water round it
	const double edge = smoothstep(0.0, 1.6, std::min(std::min(x - kPondX0, kPondX1 - x), std::min(z - kPondZ0, kPondZ1 - z)));
	s.h *= edge;
	s.dhdx *= edge;
	s.dhdz *= edge;
	return s;
}

inline Mesh waterMesh(double t) {
	Mesh m;
	const int nx = static_cast<int>((kPondX1 - kPondX0) / kPondCell), nz = static_cast<int>((kPondZ1 - kPondZ0) / kPondCell);
	m.verts.reserve(static_cast<std::size_t>((nx + 1) * (nz + 1)));
	for (int iz = 0; iz <= nz; ++iz) {
		for (int ix = 0; ix <= nx; ++ix) {
			const double x = kPondX0 + (kPondX1 - kPondX0) * ix / nx, z = kPondZ0 + (kPondZ1 - kPondZ0) * iz / nz;
			const Surface s = surfaceAt(x, z, t);
			m.verts.push_back({x, s.h, z});
			m.normals.push_back(normalize({-s.dhdx, 1.0, -s.dhdz}));
		}
	}
	for (int iz = 0; iz < nz; ++iz) {
		for (int ix = 0; ix < nx; ++ix) {
			const int a = iz * (nx + 1) + ix, b = a + 1, c = a + (nx + 1), d = c + 1;
			m.tris.push_back({a, c, b});   // wound so the face looks up
			m.tris.push_back({b, c, d});
		}
	}
	return m;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// Parts of the set
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
struct Parts {
	Mesh plank, bucket, bucketWater, hoop, stone, trunk, crown, pad, ball;
};
inline const Parts &parts() {
	static const Parts p = [] {
		Parts q;
		q.plank = box(0.30, 0.07, 1.70);
		q.bucket = lathe({{0.0, 0.0}, {0.19, 0.0}, {0.215, 0.02}, {0.285, 0.44}, {0.285, 0.46}, {0.268, 0.46}, {0.20, 0.05}, {0.0, 0.05}}, 36);
		q.bucketWater = lathe({{0.235, 0.0}, {0.0, 0.0}}, 24);
		q.hoop = torus(0.27, 0.011, 36, 8);
		q.stone = uvSphere(1.0, 24, 12);
		q.trunk = lathe({{0.0, 0.0}, {0.5, 0.0}, {0.38, 3.0}, {0.28, 8.0}, {0.0, 8.2}}, 12);
		q.crown = uvSphere(1.0, 20, 10);
		q.pad = lathe({{0.0, 0.0}, {0.27, 0.0}, {0.30, 0.02}, {0.0, 0.02}}, 18);
		q.ball = uvSphere(1.0, 12, 6);
		return q;
	}();
	return p;
}

inline std::string buildSky(double f, double t) {
	std::string out;
	const std::string dark = diffuseMaterial({0.02, 0.02, 0.03});
	// the sky: bands of a cylinder seen from inside, from a pale horizon to a deep blue overhead
	const Vec3 horizon = {f * 0.16, f * 0.26, f * 0.42}, zenith = {f * 0.010, f * 0.020, f * 0.060};
	const double edges[] = {-2.0, 2.0, 6.0, 10.0, 15.0, 21.0, 28.0, 37.0, 50.0, 68.0, 92.0};
	for (int i = 0; i < 10; ++i) {
		const double u = std::pow((i + 0.5) / 10.0, 0.7);
		const Vec3 L = lerp(horizon, zenith, u);
		out += attr(dark, emitMesh(lathe({{95.0, edges[i]}, {95.0, edges[i + 1]}}, 40), identity()), &L);
	}
	// the moon, with a halo behind it; a small twinkle as Pip waves to it
	const double glint = 1.0 + 0.25 * trap(t, 88.0, 89.0, 89.4, 90.5);
	const Vec3 moon = {f * 95.0 * glint, f * 97.0 * glint, f * 105.0 * glint}, halo = {f * 0.7, f * 0.95, f * 1.7};
	out += emissiveSphere(moonCentre(), kMoonRadius, moon);
	out += emissiveSphere(moonCentre() + normalize(moonCentre()) * 10.0, 7.0, halo);
	// stars
	std::string stars;
	for (int i = 0; i < 44; ++i) {
		const double az = 2.0 * kPi * hash01(i, 41), el = (10.0 + 62.0 * hash01(i, 42)) * kPi / 180.0;
		const Vec3 dir = {std::cos(el) * std::sin(az), std::sin(el), -std::cos(el) * std::cos(az)};
		const Vec3 c = dir * 88.0;
		if (length(c - moonCentre()) < 18.0) continue;
		const double twinkle = 0.75 + 0.25 * std::sin(1.3 * t + 9.0 * hash01(i, 43));
		const double b = f * twinkle * (6.0 + 12.0 * hash01(i, 44));
		stars += emissiveSphere(c, 0.14 + 0.16 * hash01(i, 45), {b, b, b * 1.1});
	}
	return out + stars;
}

inline std::string buildGround(double t) {
	const Parts &P = parts();
	std::string out;
	const std::string earth = coated({0.05, 0.07, 0.05}, 0.7);
	out += attr(earth, emitMesh(box(60.0, 1.0, 60.0), translate(-39.2, kDock - 0.5, 0.0)));            // the shore beyond the dock's end
	out += attr(earth, emitMesh(box(160.0, 1.0, 40.0), translate(0.0, 0.45 - 0.5, -32.0)));              // the far bank
	out += attr(earth, emitMesh(box(40.0, 1.0, 60.0), translate(30.0, 0.45 - 0.5, -10.0)));
	// trees on the banks, away from the moon
	std::string trunks, crowns;
	for (int i = 0; i < 14; ++i) {
		// (none on the left: the moon and its reflection are seen along that line)
		const double x = 5.0 + 36.0 * hash01(i, 51), z = -14.0 - 16.0 * hash01(i, 52);
		const double s = 0.8 + 0.5 * hash01(i, 53);
		trunks += emitMesh(P.trunk, mul(translate(x, 0.0, z), scale(s)));
		for (int k = 0; k < 3; ++k) crowns += emitMesh(P.crown, mul(translate(x + (hash01(i, 60 + k) - 0.5) * 2.5, (5.5 + 1.6 * k + 1.2 * hash01(i, 63 + k)) * s, z + (hash01(i, 66 + k) - 0.5) * 2.0), scale(2.6 * s, 2.0 * s, 2.4 * s)));
	}
	out += attr(coated({0.04, 0.05, 0.04}, 0.8), trunks);
	out += attr(coated({0.03, 0.06, 0.05}, 0.8), crowns);
	// reeds along the banks and round the dock's end
	std::string stems, tips;
	for (int i = 0; i < 46; ++i) {
		const bool nearDock = i < 22;
		const double x = nearDock ? 5.6 + 3.0 * hash01(i, 71) : -8.0 + 16.0 * hash01(i, 71), z = nearDock ? -1.2 + 2.6 * hash01(i, 72) - 1.0 * (i % 3) : -4.0 - 3.0 * hash01(i, 72);
		const double h = 1.3 + 0.9 * hash01(i, 73), sway = 0.10 * std::sin(0.8 * t + 5.0 * hash01(i, 74)) + 0.3 * (hash01(i, 75) - 0.5);
		const Vec3 base = {x, -0.1, z}, top = {x + sway * h * 0.6, h, z + 0.12 * h * (hash01(i, 76) - 0.5)};
		stems += bar(base, top, 0.018, 5);
		if (hash01(i, 77) < 0.45) tips += bar(top - normalize(top - base) * 0.26, top + normalize(top - base) * 0.03, 0.04, 6);
	}
	out += attr(coated({0.06, 0.12, 0.06}, 0.5), stems) + attr(coated({0.14, 0.07, 0.04}, 0.6), tips);
	// lily pads, riding the ripples
	std::string pads;
	const double px[7] = {-1.8, -3.2, 3.8, 4.6, -6.4, 0.2, 5.6}, pz[7] = {3.0, 2.4, 2.6, 4.2, 3.6, 5.0, 2.0};
	for (int i = 0; i < 7; ++i) {
		const Surface s = surfaceAt(px[i], pz[i], t);
		pads += emitMesh(P.pad, mul(translate(px[i], s.h + 0.004, pz[i]), rotate({0, 1, 0}, 47.0 * i), scale(1.0 + 0.2 * (i % 3), 1.0, 1.0 + 0.2 * (i % 3))));
	}
	out += attr(diffuseMaterial({0.02, 0.09, 0.03}), pads);
	return out;
}

inline std::string buildDock() {
	const Parts &P = parts();
	std::string planks, posts;
	for (int i = 0; kDockX0 + 0.33 * i < kDockX1; ++i) {
		const double x = kDockX0 + 0.33 * i + 0.15;
		planks += emitMesh(P.plank, translate(x, kDock - 0.035 + 0.006 * (hash01(i, 81) - 0.5), 0.0));
	}
	for (double x = kDockX0 + 0.5; x < kDockX1; x += 2.2) {
		for (double z : {-kDockHalf + 0.06, kDockHalf - 0.06}) posts += bar({x, -0.6, z}, {x, kDock + 0.28, z}, 0.07, 8);
	}
	std::string beams = bar({kDockX0, kDock - 0.12, -kDockHalf + 0.1}, {kDockX1, kDock - 0.12, -kDockHalf + 0.1}, 0.05, 6) + bar({kDockX0, kDock - 0.12, kDockHalf - 0.1}, {kDockX1, kDock - 0.12, kDockHalf - 0.1}, 0.05, 6);
	return attr(coated({0.22, 0.14, 0.08}, 0.55), planks) + attr(coated({0.15, 0.10, 0.06}, 0.6), posts + beams);
}

inline std::string buildProps(double t) {
	const Parts &P = parts();
	std::string out;
	out += attr(coated({0.30, 0.31, 0.34}, 0.5), emitMesh(P.stone, mul(translate(kStone.x, kStone.y + 0.07, kStone.z), scale(0.20, 0.11, 0.17))));
	// the bucket
	const BucketState b = bucketAt(t);
	const Mat4 bm = mul(translate(b.pos), rotate({0, 0, 1}, b.tilt));
	out += attr(metalMaterial({0.62, 0.64, 0.70}, 0.30), emitMesh(P.bucket, bm) + emitMesh(P.hoop, mul(bm, translate(0.0, 0.45, 0.0), rotate({1, 0, 0}, 90.0), rotate({0, 0, 1}, 90.0))));
	if (b.full) out += attr(coated({0.02, 0.05, 0.06}, 0.01), emitMesh(P.bucketWater, mul(bm, translate(0.0, 0.37, 0.0))));
	return out;
}

inline std::string buildMoonInTheBucket(double t, double f) {
	const GlowState g = glowAt(t);
	if (g.power <= 0.002) return std::string();
	std::string out;
	const double flick = 1.0 + 0.04 * std::sin(17.0 * t);
	const double power = g.power * f * flick;
	out += emissiveSphere(g.pos, 0.115, {6.5 * power, 7.0 * power, 8.4 * power});
	// sparks as it appears
	const double age = t - kGlow;
	if (age > 0.0 && age < 1.6) {
		std::string sparks;
		const double fadeOut = 1.0 - smoothstep(0.2, 1.6, age);
		for (int i = 0; i < 12; ++i) {
			const double a = 2.0 * kPi * hash01(i, 91), e = (0.2 + 0.9 * hash01(i, 92));
			const Vec3 dir = {std::cos(a) * std::cos(e), std::sin(e), std::sin(a) * std::cos(e)};
			const Vec3 p = g.pos + dir * (0.15 + 0.9 * (1.0 - std::exp(-3.0 * age)) * (0.6 + 0.6 * hash01(i, 93)));
			sparks += emissiveSphere(p, 0.016 + 0.012 * hash01(i, 94), {9.0 * fadeOut * f, 9.0 * fadeOut * f, 10.0 * fadeOut * f});
		}
		out += sparks;
	}
	return out;
}

// Drops thrown up where the water is struck hard.
inline std::string buildSpray(double t) {
	struct Hit {
		Vec3 at;
		double t0;
		int count;
		double speed;
	};
	const Hit hits[] = {{kTouch, kTouched, 6, 1.4}, {kSplash, kSplashT, 22, 2.8}, {kTouch, kDip, 8, 1.6}};
	std::string drops;
	int salt = 100;
	for (const Hit &h : hits) {
		const double age = t - h.t0;
		for (int i = 0; i < h.count; ++i, ++salt) {
			if (age < 0.0) continue;
			const double a = 2.0 * kPi * hash01(i, salt), s = h.speed * (0.4 + 0.6 * hash01(i, salt + 500));
			const Vec3 v = {std::cos(a) * s * 0.55, s * (0.9 + 0.6 * hash01(i, salt + 900)), std::sin(a) * s * 0.55};
			const Vec3 p = {h.at.x + v.x * age, v.y * age - 4.9 * age * age, h.at.z + v.z * age};
			if (p.y < 0.0) continue;
			drops += emitMesh(parts().ball, mul(translate(p), scale(0.028)));
		}
	}
	return drops.empty() ? drops : attr(coated({0.8, 0.9, 1.0}, 0.05), drops);
}

inline Vec3 fireflyAt(int i, double t) {
	const double ph = 6.28 * hash01(i, 120);
	Vec3 home = {-7.0 + 14.0 * hash01(i, 121), 0.9 + 1.7 * hash01(i, 122), -1.2 + 4.5 * hash01(i, 123)};
	home.x += 0.5 * std::sin(0.31 * t + ph) + 0.15 * std::sin(1.3 * t + 2.0 * ph);
	home.y += 0.30 * std::sin(0.43 * t + 2.0 * ph);
	home.z += 0.5 * std::cos(0.27 * t + ph);
	// while Pip carries the moon home they gather round the bucket
	const double gather = trap(t, 57.5, 62.5, kRelease, kRelease + 1.2);
	if (gather > 0.0) {
		const Pose p = poseAt(std::min(t, kRelease));
		const double a = 2.0 * kPi * hash01(i, 124) + 0.9 * t * (0.6 + hash01(i, 125));
		const double r = 0.55 + 0.7 * hash01(i, 126);
		const Vec3 around = carryPoint(p, kDock + 1.0) + Vec3{r * std::cos(a), 0.25 * std::sin(2.1 * a), r * std::sin(a)};
		home = lerp(home, around, gather);
	}
	return home;
}

inline std::string buildFireflies(double t, double f) {
	std::string out;
	const int n = 16;
	for (int i = 0; i < n; ++i) {
		const double blink = std::pow(std::max(0.0, std::sin(2.0 * kPi * (t / (2.4 + hash01(i, 127)) + hash01(i, 128)))), 3.0);
		const double glow = f * (0.25 + 0.75 * blink);
		out += emissiveSphere(fireflyAt(i, t), 0.028, {14.0 * glow, 13.0 * glow, 3.0 * glow});
	}
	// the firefly that finds Pip
	const double on = trap(t, 77.8, 78.6, 83.0, 84.4);
	if (on > 0.0) {
		const double glow = f * on * (0.65 + 0.35 * std::sin(2.0 * kPi * t / 1.4));
		out += emissiveSphere(fireflyStar(t), 0.04, {22.0 * glow, 20.0 * glow, 5.0 * glow});
	}
	return out;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// Cameras
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
inline Vec3 pipPoint(double t, Vec3 offset) { return poseAt(t).pos + offset; }
// A point 10 metres from `eye` towards `point` on the horizontal, `drop` below the eye's height: the way to aim a camera at the sky and the water at once.
inline Vec3 lookAlong(Vec3 eye, Vec3 point, double drop = 0.0) {
	const Vec3 d = normalize({point.x - eye.x, 0.0, point.z - eye.z});
	return {eye.x + d.x * 10.0, eye.y - drop, eye.z + d.z * 10.0};
}

inline const std::vector<Shot> &shots() {
	static const std::vector<Shot> s = {
		// 0: the moon and its reflection, from low over the water; a slow push in
		{0.0, Track3::withEases({{0.0, reflectionCamera({-2.5, 0.0, 3.0}, 8.5), Ease::Smooth}, {9.0, reflectionCamera({-2.5, 0.0, 3.0}, 6.6), Ease::Smooth}}),
		 Track3::withEases({{0.0, lookAlong(reflectionCamera({-2.5, 0.0, 3.0}, 8.5), {-2.5, 0.0, 3.0}), Ease::Smooth}, {9.0, lookAlong(reflectionCamera({-2.5, 0.0, 3.0}, 6.6), {-2.5, 0.0, 3.0}, 0.5), Ease::Smooth}}),
		 Track1::withEases({{0.0, 56.0, Ease::Smooth}, {9.0, 50.0, Ease::Smooth}}), 0.0},
		// 9: beside Pip as it strolls along the dock
		{9.0, sampledTrack(9.0, 16.2, [](double t) { return pipPoint(t, {1.9, 0.8, 5.6}); }), sampledTrack(9.0, 16.2, [](double t) { return pipPoint(t, {0.5, 0.85, 0.0}); }), Track1::withEases({{9.0, 34.0, Ease::Smooth}}), 0.02},
		// 16: Pip at the water's edge; the moon's reflection is beside it
		{16.0, Track3::withEases({{16.0, reflectionCamera(kTouch, 6.6), Ease::Smooth}, {26.2, reflectionCamera(kTouch, 5.6), Ease::Smooth}}), Track3::withEases({{16.0, {2.85, 0.9, 0.3}, Ease::Smooth}, {26.2, {2.85, 0.8, 0.35}, Ease::Smooth}}),
		 Track1::withEases({{16.0, 32.0, Ease::Smooth}}), 0.015},
		// 26.2: close on Pip's sorrow, and the idea
		{26.2, Track3::withEases({{26.2, reflectionCamera(kTouch, 4.9), Ease::Smooth}, {34.3, reflectionCamera(kTouch, 4.2), Ease::Smooth}}), Track3::withEases({{26.2, {2.2, 1.05, 0.2}, Ease::Smooth}, {34.3, {2.2, 1.15, 0.2}, Ease::Smooth}}),
		 Track1::withEases({{26.2, 30.0, Ease::Smooth}}), 0.03},
		// 34.3: along the dock after Pip, to the bucket and back
		{34.3, sampledTrack(34.3, 43.2, [](double t) { return pipPoint(t, {0.6, 0.55, 4.4}); }), sampledTrack(34.3, 43.2, [](double t) { return pipPoint(t, {-0.2, 0.7, 0.0}); }), Track1::withEases({{34.3, 36.0, Ease::Smooth}}), 0.02},
		// 43.2: the bucket in the pond, the moon coming out
		{43.2, Track3::withEases({{43.2, reflectionCamera(kTouch, 5.2), Ease::Smooth}, {47.4, reflectionCamera(kTouch, 4.4), Ease::Smooth}}), Track3::withEases({{43.2, {2.8, 0.8, 0.3}, Ease::Smooth}, {47.4, {2.8, 1.0, 0.3}, Ease::Smooth}}),
		 Track1::withEases({{43.2, 32.0, Ease::Smooth}}), 0.025},
		// 47.4: round Pip as it dances
		{47.4, sampledTrack(47.4, 57.0, [](double t) {
			 const double a = (20.0 + 55.0 * (t - 47.4) / 9.6) * kPi / 180.0;
			 return Vec3{2.15 + 4.2 * std::sin(a), 1.6, 4.2 * std::cos(a)};
		 }), Track3::withEases({{47.4, {2.15, 1.1, 0.0}, Ease::Smooth}}), Track1::withEases({{47.4, 36.0, Ease::Smooth}}), 0.02},
		// 57: beside Pip as it carries the moon home
		{57.0, sampledTrack(57.0, 66.4, [](double t) { return pipPoint(t, {-2.1, 0.45, 3.5}); }), sampledTrack(57.0, 66.4, [](double t) { return pipPoint(t, {0.2, 0.8, 0.0}); }), Track1::withEases({{57.0, 38.0, Ease::Smooth}}), 0.02},
		// 66.4: the fall, from a little way off
		{66.4, Track3::withEases({{66.4, {-2.8, 1.5, 6.2}, Ease::Smooth}, {72.4, {-3.4, 1.4, 5.6}, Ease::Smooth}}), Track3::withEases({{66.4, {-5.0, 0.7, 0.9}, Ease::Smooth}, {72.4, {-5.2, 0.8, 0.9}, Ease::Smooth}}),
		 Track1::withEases({{66.4, 40.0, Ease::Smooth}}), 0.02},
		// 72.4: Pip alone at the water's edge
		{72.4, Track3::withEases({{72.4, {-3.6, 1.2, 4.6}, Ease::Smooth}, {79.6, {-4.2, 1.0, 3.6}, Ease::Smooth}}), Track3::withEases({{72.4, {-5.2, 0.85, 0.7}, Ease::Smooth}, {79.6, {-5.2, 0.9, 0.7}, Ease::Smooth}}),
		 Track1::withEases({{72.4, 30.0, Ease::Smooth}}), 0.03},
		// 79.6: the firefly, and Pip's eyes go up with it
		{79.6, Track3::withEases({{79.6, {-4.4, 1.3, 3.4}, Ease::Smooth}, {82.4, {-4.8, 1.2, 3.0}, Ease::Smooth}}), sampledTrack(79.6, 82.4, [](double t) { return lerp(Vec3{-5.1, 1.0, 0.8}, fireflyStar(t), 0.45); }),
		 Track1::withEases({{79.6, 36.0, Ease::Smooth}, {82.4, 42.0, Ease::Smooth}}), 0.015},
		// 82.4: from behind Pip, the real moon over the pond
		{82.4, Track3::withEases({{82.4, {-4.9, 1.3, 4.2}, Ease::Smooth}, {89.2, {-4.7, 1.4, 4.5}, Ease::Smooth}}), Track3::withEases({{82.4, lookAlong({-4.9, 1.3, 4.2}, moonCentre(), -2.4), Ease::Smooth}, {89.2, lookAlong({-4.7, 1.4, 4.5}, moonCentre(), -2.0), Ease::Smooth}}),
		 Track1::withEases({{82.4, 50.0, Ease::Smooth}}), 0.0},
		// 89.2: a slow pull back and up over the pond
		{89.2, Track3::withEases({{89.2, {-4.0, 1.7, 4.2}, Ease::Smooth}, {kDuration, {2.5, 5.5, 13.5}, Ease::Smooth}}), Track3::withEases({{89.2, {-5.0, 1.0, 0.7}, Ease::Smooth}, {kDuration, {-3.0, 2.0, -3.0}, Ease::Smooth}}),
		 Track1::withEases({{89.2, 40.0, Ease::Smooth}, {kDuration, 46.0, Ease::Smooth}}), 0.0},
	};
	return s;
}
inline std::vector<double> shotStarts() {
	std::vector<double> v;
	for (const Shot &s : shots()) v.push_back(s.start);
	return v;
}

// Everything up to and including WorldBegin: the camera at t, the picture's size, the sampler and the integrator.
inline std::string headerAt(double t) { return sceneHeader(cameraText(cameraAt(shots(), t)), kSamples); }

inline std::string lightText(Vec3 fromStory, Vec3 radiance) {
	char buf[240];
	std::snprintf(buf, sizeof(buf), "LightSource \"distant\" \"point3 from\" [ %.3f %.3f %.3f ] \"point3 to\" [ 0 0 0 ] \"rgb L\" [ %.4f %.4f %.4f ]\n", -fromStory.x, fromStory.y, fromStory.z, radiance.x, radiance.y, radiance.z);
	return buf;
}

inline std::string sceneAt(double t) {
	const double f = fade(t);
	std::string out = headerAt(t);
	char buf[200];
	std::snprintf(buf, sizeof(buf), "LightSource \"infinite\" \"rgb L\" [ %.4f %.4f %.4f ]\n", f * 0.035, f * 0.065, f * 0.15);
	out += buf;
	// moonlight from behind the pond, and a cool fill from the camera's side so that Pip's face reads
	out += lightText(moonCentre(), {f * 1.1, f * 1.3, f * 2.0});
	out += lightText({4.0, 7.0, 10.0}, {f * 2.5, f * 2.7, f * 3.2});
	out += buildSky(f, t) + buildGround(t) + buildDock();
	// the pond: the height field, and flat water beyond it
	const std::string water = "Material \"coateddiffuse\" \"rgb reflectance\" [ 0.02 0.05 0.06 ] \"float roughness\" [ 0 ]";
	out += attr(water, emitMesh(waterMesh(t), identity()));
	out += attr(water, emitMesh(box(300.0, 1.0, 300.0), translate(0.0, -0.503, 0.0)));
	out += buildProps(t) + buildMoonInTheBucket(t, f) + buildSpray(t) + buildFireflies(t, f);
	out += actor::emitActor(Character{}, poseAt(t));
	return out;
}

}  // namespace pip_moon
}  // namespace cartoon
