#pragma once
// turtle_escape.h -- "The Great Turtle Escape", a five-and-a-half-minute visual comedy at 1920 x 1080: a tiny turtle's escape from its perfect tank, shot like a
// spy thriller. The owner tidies the tank and leaves; the turtle narrows its eyes; the tank becomes a prison (close-ups, a blueprint of the room); it pushes a
// rock (one inch), builds a pile under the thermometer (it falls), pulls the thermometer off its suction cup (POP) and makes it a ramp (the heat lamp clicks
// off); by moonlight it climbs the filter hose (a canyon, to the turtle; eight inches, to us), tops the rim, slides down the hose onto the towels, dodges the
// robot vacuum under the sofa, scales the rug, sneaks through the fan's moving shadows, finds a pool of clean water and dives in. The owner comes home to an
// empty tank and searches everywhere; the turtle, a few feet away, is floating in the storage tub he had filled for it - and quietly sinks out of sight.
//
// The set is turtle_escape_set.h, the turtle turtle_actor.h, the owner human_actor.h, the sound turtle_escape_score.h. Times are seconds; the story's beats
// are the constants below, which the score shares, so every BONK and POP is where the picture has it.

#include "turtle_escape_set.h"

namespace cartoon {
namespace turtle_escape {

constexpr double kDuration = 341.0;
constexpr int kFps = 24;
constexpr int kSamples = 64;
constexpr int kWidth = 1920, kHeight = 1080;

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The beats
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
constexpr double kPellets = 17.0;      // pellets into the water
constexpr double kPerfect = 24.0;      // "Perfect." (hummed)
constexpr double kKeys = 31.0;         // the keys picked up
constexpr double kDoorClick = 37.0;    // the front door shuts
constexpr double kSpy = 46.5;          // the spy music starts; the eyes narrow
constexpr double kPlan = 63.0;         // the blueprint
constexpr double kDive = 72.6;         // into the water
constexpr double kNudge = 78.5;        // the rock moves, barely
constexpr double kCharge = 88.2;       // full speed...
constexpr double kBonk = 89.2;         // BONK: one inch
constexpr double kClock = 94.0;        // cut to the clock
constexpr double kSlip = 113.5;        // a rock of the pile slips...
constexpr double kCrash = 113.8;       // ...CRASH
constexpr double kBubble = 117.6;      // one bubble rises
constexpr double kSurface = 120.5;     // the head comes up, annoyed
constexpr double kPlanB = 124.0;       // PLAN B
constexpr double kBite = 126.2;        // the turtle bites the thermometer
constexpr double kPop = 130.6;         // POP
constexpr double kIdea = 135.5;        // the ramp idea
constexpr double kTouch = 148.5;       // a front foot on the rim
constexpr double kLampOff = 151.0;     // the heat lamp clicks off
constexpr double kSlideBack = 153.3;   // (in the dark) it slides back down the ramp
constexpr double kGrab = 166.8;        // the hose
constexpr double kHang = 168.2;        // its feet slip: hanging
constexpr double kOverTop = 192.0;     // pulling itself over the rim
constexpr double kHoseSlip = 203.0;    // the hose slips
constexpr double kLand = 205.0;        // onto the towels
constexpr double kEyesOpen = 209.3;
constexpr double kVacuumOn = 219.0;    // the robot vacuum wakes
constexpr double kHide = 225.6;        // under the sofa
constexpr double kRugEdge = 237.0;     // the carpet cliff
constexpr double kRugTop = 243.0;
constexpr double kPlip = 261.0;        // water, somewhere
constexpr double kOnBox = 280.5;
constexpr double kSplash = 291.0;      // into the pool
constexpr double kHome = 300.2;        // the door opens
constexpr double kLightOn = 303.0;
constexpr double kBagsDown = 315.5;
constexpr double kCall = 329.0;        // "Turtle?"
constexpr double kHeadUp = 331.0;      // the turtle's head out of the water
constexpr double kSink = 334.5;        // it sinks back
constexpr double kLastBubble = 337.5;  // one tiny bubble...
constexpr double kBlack = 339.6;       // CUT TO BLACK

using turtle::TurtlePose;
using human::HumanPose;

inline double trap(double t, double a, double b, double c, double d) { return smoothstep(a, b, t) * (1.0 - smoothstep(c, d, t)); }
inline double hopAt(double t, double t0, double dur, double h) {
	const double u = (t - t0) / dur;
	return (u < 0.0 || u > 1.0) ? 0.0 : h * std::sin(kPi * u);
}
inline Vec3 frontOf(double yawDeg) { return {std::cos(yawDeg * kPi / 180.0), 0.0, std::sin(yawDeg * kPi / 180.0)}; }
inline Vec3 rightOf(double yawDeg) {
	const Vec3 f = frontOf(yawDeg);
	return {-f.z, 0.0, f.x};
}
inline double yawTowards(Vec3 from, Vec3 to) { return std::atan2(to.z - from.z, to.x - from.x) * 180.0 / kPi; }

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The day
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
inline Light lightAt(double t) {
	if (t < 100.0) return Light::Morning;
	if (t < 140.0) return Light::Afternoon;
	if (t < kLampOff) return Light::Dusk;
	if (t < kLightOn) return Light::Night;
	return Light::RoomLamp;
}
// Minutes after 8:00 the clock shows: real time at first, then hours go by in the clock shot and the afternoon.
inline double clockMinutesAt(double t) {
	if (t < kClock) return 5.0 + t / 60.0;
	if (t < kClock + 5.5) return 5.0 + kClock / 60.0 + 190.0 * smoothstep(kClock + 0.5, kClock + 5.0, t);   // the hands spin: three hours go by
	if (t < kLampOff) return 197.0 + 600.0 * (t - kClock - 5.5) / (kLampOff - kClock - 5.5);   // the afternoon into the evening (8:00 pm at the click)
	return 797.0 + 360.0 * (t - kLampOff) / (kHome - kLampOff);   // the night
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The rocks, the thermometer, the hose, the vacuum, the water
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// Where rocks 1-4 go in the pile under the thermometer, and where they land when it falls.
inline Vec3 pileSpot(int k) {
	const Vec3 base = {0.47, kGravel, -2.09};
	const Vec3 lifts[4] = {{0.0, 0.0, 0.0}, {0.008, 0.045, -0.004}, {-0.006, 0.088, 0.0}, {0.004, 0.125, 0.002}};
	return base + lifts[k];
}
inline Vec3 scatterSpot(int k) {
	const Vec3 s[4] = {{0.40, kGravel, -2.18}, {0.57, kGravel, -2.12}, {0.43, kGravel, -2.28}, {0.61, kGravel, -2.24}};
	return s[k];
}
// The pile's rocks: home, then (100 - 109.6) pushed one by one to the pile, then scattered at the crash.
inline double pileShare(int k, double t) { return smoothstep(100.0 + 2.4 * k + 0.4, 100.0 + 2.4 * (k + 1), t); }
inline Vec3 pileRockAt(int k, double t) {
	const Vec3 home = tankRocks()[static_cast<std::size_t>(k + 1)].home;
	Vec3 p = lerp(home, pileSpot(k), pileShare(k, t));
	if (t >= kSlip) {
		const double f = smoothstep(kSlip + (k == 1 ? 0.0 : 0.12), kSlip + 0.9 + 0.1 * k, t);
		p = lerp(pileSpot(k), scatterSpot(k), f) + Vec3{0.0, 0.03 * std::sin(kPi * f) * (k + 1) * 0.5, 0.0};
	}
	return p;
}
// The stubborn rock: a nudge (barely), then BONK (an inch).
inline Vec3 stubbornOffset(double t) { return {-0.008 * smoothstep(kNudge - 0.4, kNudge + 0.2, t) - 0.025 * smoothstep(kBonk, kBonk + 0.35, t), 0.0, 0.0}; }

// The ramp: the thermometer leaning on the fallen rocks, its top towards the left glass.
inline Vec3 rampBottom() { return {0.50, 0.985, -2.10}; }
inline Vec3 rampTop() { return {0.37, 1.10, -2.10}; }

struct Thermo {
	double state;   // 0 on the glass, 1 off (in the mouth, floating, a ramp)
	Vec3 pos;
	double tilt;
};
Thermo thermoAt(double t);   // (needs the turtle's pose while it is in its mouth: defined after it)
// Where the thermometer is when it is not in the turtle's mouth: on the glass, floating, pushed to the rocks as a ramp, knocked off again.
inline Thermo thermoFree(double t) {
	if (t < kPop) return {0.0, thermometerSpot(), 0.0};
	const Vec3 floating = {0.53, kWater + 0.007 + 0.002 * std::sin(2.0 * kPi * 0.5 * t), -2.22};
	if (t < 137.0) return {1.0, floating, 90.0};
	const Vec3 ramp = (rampBottom() + rampTop()) * 0.5;
	// (the owner finds it floating sideways again in scene 7: knocked off the rocks when the turtle slid back)
	if (t > kSlideBack) {
		const double off = smoothstep(kSlideBack, kSlideBack + 0.8, t);
		return {1.0, lerp(ramp, Vec3{0.48, kWater + 0.007, -2.16}, off), lerp(42.0, 90.0, off)};
	}
	const double u = smoothstep(137.0, 140.6, t);
	return {1.0, lerp(floating, ramp, u), lerp(90.0, 42.0, u)};
}

// The vacuum's route: out of its dock, towards the turtle like a tank, past the sofa, and on round the room.
inline void vacuumAt(double t, Vec3 &pos, double &yaw, double &on) {
	static const Track3 route({{kVacuumOn + 0.5, vacuumDock()}, {kVacuumOn + 1.5, vacuumDock() + Vec3{-0.25, 0.0, 0.0}}, {kVacuumOn + 4.0, {1.60, 0.0, -0.42}}, {kVacuumOn + 7.0, {0.75, 0.0, -0.62}},
							   {kVacuumOn + 9.5, {0.0, 0.0, -0.64}}, {kVacuumOn + 13.0, {-1.55, 0.0, -0.66}}, {kVacuumOn + 17.0, {-2.40, 0.0, 0.40}}, {kVacuumOn + 26.0, {-2.30, 0.0, 1.80}},
							   {kVacuumOn + 40.0, {1.0, 0.0, 1.95}}, {kVacuumOn + 55.0, {2.40, 0.0, 0.60}}, {kVacuumOn + 62.0, vacuumDock()}},
							  Ease::Linear);
	pos = route(t);
	const Vec3 ahead = route(t + 0.25);
	yaw = length(ahead - pos) > 1e-4 ? yawTowards(pos, ahead) : 180.0;
	on = trap(t, kVacuumOn, kVacuumOn + 0.6, kVacuumOn + 61.0, kVacuumOn + 62.0);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The turtle's performance
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
inline Vec3 slate() { return basking(); }

// Turn the head towards a point for a share of the way (the eyes do the rest).
inline void aim(TurtlePose &p, Vec3 target, double share = 0.8) {
	const Vec3 head = p.pos + frontOf(p.yaw) * 0.09 + Vec3{0.0, 0.035, 0.0};
	const Vec3 d = target - head;
	const double yawWorld = yawTowards(head, target);
	const double rel = actor::wrapDegrees(yawWorld - p.yaw);
	const double up = std::atan2(d.y, std::hypot(d.x, d.z)) * 180.0 / kPi;
	p.headYaw = std::max(-60.0, std::min(60.0, rel * share));
	p.headPitch = std::max(-55.0, std::min(45.0, -(up - p.pitch) * share));
	p.look = target;
	p.lookWeight = 1.0;
}

// The point on a polyline at a share `u` of its length.
inline Vec3 alongPath(const std::vector<Vec3> &pts, double u) {
	double total = 0.0;
	for (std::size_t i = 1; i < pts.size(); ++i) total += length(pts[i] - pts[i - 1]);
	double want = clamp01(u) * total;
	for (std::size_t i = 1; i < pts.size(); ++i) {
		const double l = length(pts[i] - pts[i - 1]);
		if (want <= l || i + 1 == pts.size()) return lerp(pts[i - 1], pts[i], l > 0.0 ? std::min(1.0, want / l) : 0.0);
		want -= l;
	}
	return pts.back();
}

// The turtle's route on the floor (scene 5 and 6), as times and places; the walk's phase comes from the distance covered.
struct FloorKey {
	double t;
	Vec3 p;
};
inline const std::vector<FloorKey> &floorRoute() {
	static const std::vector<FloorKey> k = {
		{214.5, {1.30, 0.0, -1.68}}, {221.0, {0.30, 0.0, -0.62}},                    // to the sofa's corner
		{kHide, {0.30, 0.0, -0.62}}, {kHide + 1.2, {-0.10, 0.0, -0.32}},             // frozen, then under the sofa at the last second
		{232.0, {-0.10, 0.0, -0.32}}, {236.0, {-0.55, 0.0, 0.57}},                   // under the sofa to its front
		{kRugEdge, {-0.55, 0.0, 0.58}}, {kRugTop, {-0.55, kRugThick, 0.70}},         // the carpet cliff
		{247.5, {-0.25, kRugThick, 1.08}}, {249.2, {-0.25, kRugThick, 1.08}},       // across the rug, stopping when a shadow passes
		{251.6, {0.02, kRugThick, 1.40}}, {253.0, {0.02, kRugThick, 1.40}}, {256.0, {0.42, kRugThick, 1.92}},
		{257.6, {0.52, 0.0, 2.04}},                                                  // off the far edge
		{268.0, {0.52, 0.0, 2.04}}, {277.0, {1.40, 0.0, 2.17}},                      // the endless crawl to the tub
	};
	return k;
}
inline Vec3 floorPos(double t, double &speed) {
	const auto &k = floorRoute();
	speed = 0.0;
	if (t <= k.front().t) return k.front().p;
	for (std::size_t i = 1; i < k.size(); ++i)
		if (t <= k[i].t) {
			const double u = (t - k[i - 1].t) / (k[i].t - k[i - 1].t);
			const double s = u * u * (3.0 - 2.0 * u);
			speed = length(k[i].p - k[i - 1].p) / (k[i].t - k[i - 1].t) * 6.0 * u * (1.0 - u);
			return lerp(k[i - 1].p, k[i].p, s);
		}
	return k.back().p;
}
inline double floorDistance(double t) {
	double d = 0.0;
	const auto &k = floorRoute();
	for (std::size_t i = 1; i < k.size(); ++i) {
		if (t <= k[i - 1].t) break;
		const double u = clamp01((t - k[i - 1].t) / (k[i].t - k[i - 1].t));
		d += length(k[i].p - k[i - 1].p) * u * u * (3.0 - 2.0 * u);
	}
	return d;
}

// Where the turtle's hand-over-hand grip is on the hose while it climbs (scene 4).
inline Vec3 gripAt(double t) {
	const double steps = 8.0, t0 = 180.0, t1 = kOverTop;
	const double k = clamp01((t - t0) / (t1 - t0)) * steps;
	const double whole = std::floor(k), frac = k - whole;
	const double u = (whole + smoothstep(0.55, 1.0, frac)) / steps;   // each pull: a hold, then a heave
	return lerp(hoseIn2(), hoseIn1(), std::min(1.0, u));
}

inline TurtlePose turtleAt(double t) {
	TurtlePose p;
	p.blink = actor::autoBlink(t * 0.6, 3);
	p.squash = 1.0 + 0.008 * std::sin(2.0 * kPi * t / 2.6);
	p.smile = 0.15;
	const Vec3 B = slate();
	// ---------------- scene 1 and 2 (to the dive): on the basking slate ----------------
	if (t < 71.2) {
		p.pos = B;
		p.lift = 0.003;
		p.yaw = 110.0;
		p.blink = std::max(p.blink * 0.4, 0.0);   // it hardly blinks: it stares
		// whom it looks at: the owner, then the door, the top of the tank, the door again; then the tank's parts
		const Vec3 owner = {0.70, 1.62, -1.45}, door = {2.95, 1.1, 1.15}, top = {1.0, 1.40, -2.28};
		Vec3 look = owner;
		if (t > 37.6) look = lerp(owner, door, smoothstep(37.6, 40.4, t));
		if (t > 41.0) look = lerp(door, top, smoothstep(41.0, 42.6, t));
		if (t > 44.0) look = lerp(top, door, smoothstep(44.0, 45.4, t));
		// the inspection: the glass, the rim, the thermometer, the intake, the hose
		const Vec3 items[5] = {{0.70, 1.05, -2.03}, {0.70, 1.21, -2.03}, thermometerSpot(), hoseIn3(), hoseIn1()};
		for (int k = 0; k < 5; ++k)
			if (t > 50.0 + 2.6 * k) look = lerp(look, items[k], smoothstep(50.0 + 2.6 * k, 50.8 + 2.6 * k, t));
		if (t > 66.0) look = lerp(look, {0.75, 1.0, -2.15}, smoothstep(66.0, 67.5, t));   // the water
		aim(p, look, t < 37.6 ? 0.3 : 0.75);
		p.squint = smoothstep(kSpy + 1.0, kSpy + 2.6, t) * (1.0 - 0.5 * smoothstep(63.0, 66.0, t));
		p.browL = p.browR = 0.7 * smoothstep(kSpy + 1.0, kSpy + 2.6, t);
		p.smile = 0.15 - 0.25 * smoothstep(kSpy, kSpy + 2.0, t) + 0.35 * trap(t, 68.0, 69.0, 70.5, 71.2);   // a sly grin before the dive
		p.neck = 1.0 + 0.35 * trap(t, 41.0, 42.0, 43.6, 44.4) + 0.25 * trap(t, 50.0, 51.0, 62.0, 63.0);
		return p;
	}
	// ---------------- the dive, the swim to the stubborn rock, the push, the BONK ----------------
	if (t < 94.0) {
		static const Track3 pos({{71.2, B}, {72.0, B + Vec3{-0.05, 0.045, 0.05}}, {kDive, {0.93, 0.992, -2.18}}, {73.6, {0.86, 0.88, -2.15}}, {75.5, {0.80, 0.835, -2.14}},
								 {77.2, {0.712, 0.818, -2.14}}, {kNudge, {0.708, 0.818, -2.14}}, {84.0, {0.705, 0.818, -2.14}}, {87.5, {0.96, 0.83, -2.14}}, {kCharge, {0.96, 0.83, -2.14}},
								 {kBonk, {0.690, 0.820, -2.14}}, {kBonk + 0.4, {0.76, 0.825, -2.14}}, {91.0, {0.78, 0.81, -2.14}}, {93.0, {0.80, 0.802, -2.15}}});
		p.pos = pos(t);
		p.yaw = t < 73.6 ? 110.0 + 70.0 * smoothstep(72.6, 73.6, t) : 180.0;
		p.pitch = -35.0 * trap(t, 71.8, 72.3, 72.7, 73.4);   // nose down into the water
		p.lift = 0.0;
		p.swimAmount = t > 72.6 ? 1.0 - 0.7 * trap(t, 77.2, 77.6, 84.0, 84.3) : 0.0;
		p.swimPhase = 2.0 * kPi * (t * 0.9) + (t > 87.5 && t < kBonk ? 2.0 * kPi * 2.0 * (t - 87.5) : 0.0);   // flat out on the charge
		// the push: front feet on the rock, leaning into it
		const double push = trap(t, 77.0, 77.5, 79.5, 80.2) + trap(t, 80.6, 81.0, 83.6, 84.0);
		if (push > 0.0) {
			const Vec3 rock = tankRocks()[0].home + stubbornOffset(t) + Vec3{0.035, 0.02, 0.0};
			p.frontL = rock + Vec3{0.0, 0.0, -0.022};
			p.frontR = rock + Vec3{0.0, 0.0, 0.022};
			p.frontWeightL = p.frontWeightR = push;
			p.squash *= 1.0 - 0.06 * push;
			p.squint = 0.6 * push;
			p.browL = p.browR = 0.8 * push;
			p.mouthOpen = 0.25 * trap(t, 81.2, 81.5, 83.2, 83.6);
			p.smile = -0.4 * push;
		}
		// backing up: eyes narrow, a pause, then the charge with the head down
		const double resolve = trap(t, 85.5, 86.5, kBonk, kBonk + 0.2);
		p.squint = std::max(p.squint, 0.85 * resolve);
		p.browL = std::max(p.browL, resolve);
		p.browR = std::max(p.browR, resolve);
		p.neck = 1.0 - 0.6 * trap(t, kCharge, kCharge + 0.3, kBonk, kBonk + 0.2) + 0.3 * trap(t, kBonk + 0.4, kBonk + 0.8, 91.0, 92.0);
		// BONK: squashed, dazed, then exhausted
		p.squash *= 1.0 - 0.18 * trap(t, kBonk - 0.05, kBonk, kBonk + 0.1, kBonk + 0.35);
		const double dazed = trap(t, kBonk + 0.3, kBonk + 0.6, 91.0, 91.6);
		p.blink = std::max(p.blink, 0.5 * dazed);
		p.headRoll = 12.0 * dazed * std::sin(2.0 * kPi * 1.2 * t);
		const double tired = smoothstep(91.0, 92.0, t);
		p.blink = std::max(p.blink, 0.55 * tired);
		p.mouthOpen = std::max(p.mouthOpen, 0.45 * tired * (0.7 + 0.3 * std::sin(2.0 * kPi * 0.8 * t)));
		p.smile = std::min(p.smile, 0.15 - 0.4 * tired);
		aim(p, tankRocks()[0].home + Vec3{0.0, 0.03, 0.0}, 0.5);
		if (t < 74.0) aim(p, {0.9, 0.95, -2.18}, 0.5);
		return p;
	}
	// ---------------- the clock (the turtle is not in the shot) and the pile ----------------
	if (t < 117.0) {
		p.lift = 0.0;
		p.swimAmount = 0.8;
		p.swimPhase = 2.0 * kPi * 1.4 * t;
		if (t < 100.0) {
			p.pos = {0.80, 0.802, -2.15};
			p.yaw = 180.0;
			p.swimAmount = 0.0;
			return p;
		}
		if (t < 109.6) {
			// time-lapse: behind each rock in turn, pushing it to the pile
			const int k = std::min(3, static_cast<int>((t - 100.0) / 2.4));
			const double t0 = 100.0 + 2.4 * k;
			const Vec3 rock = pileRockAt(k, t), target = pileSpot(k);
			const Vec3 dir = length(target - tankRocks()[static_cast<std::size_t>(k + 1)].home) > 1e-6 ? normalize(target - tankRocks()[static_cast<std::size_t>(k + 1)].home) : Vec3{-1.0, 0.0, 0.0};
			const Vec3 behind = Vec3{rock.x, 0.0, rock.z} - Vec3{dir.x, 0.0, dir.z} * 0.10 + Vec3{0.0, rock.y + 0.03, 0.0};
			const Vec3 prev = k == 0 ? Vec3{0.80, 0.802, -2.15} : pileSpot(k - 1) + Vec3{0.10, 0.03, -0.02};
			p.pos = lerp(prev, behind, smoothstep(t0, t0 + 0.4, t));
			p.yaw = yawTowards(behind, target);
			p.squash *= 1.0 - 0.05 * trap(t, t0 + 0.4, t0 + 0.6, t0 + 2.2, t0 + 2.4);
			aim(p, target + Vec3{0.0, 0.02, 0.0}, 0.4);
			return p;
		}
		// climbing the pile, reaching up; the slip; tumbling backwards into the water
		static const Track3 climb({{109.6, {0.57, 0.83, -2.10}}, {111.5, {0.52, 0.92, -2.095}}, {113.0, {0.48, kGravel + 0.18, -2.09}}, {kSlip, {0.48, kGravel + 0.18, -2.09}},
								   {kCrash + 0.4, {0.56, 0.90, -2.12}}, {115.2, {0.62, 0.80, -2.13}}});
		p.pos = climb(t);
		p.yaw = 180.0;
		p.swimAmount = 0.3;
		p.pitch = 25.0 * trap(t, 110.0, 110.8, 112.5, 113.0) + 15.0 * trap(t, 112.8, 113.0, kSlip, kSlip + 0.1);
		p.neck = 1.0 + 0.6 * trap(t, 112.6, 113.0, kSlip, kSlip + 0.2);
		p.roll = 180.0 * smoothstep(kSlip + 0.1, kCrash + 0.9, t);   // over onto its back...
		const double flail = trap(t, kSlip, kSlip + 0.15, 115.4, 116.0);
		p.walkAmount = 1.2 * flail;
		p.walkPhase = 2.0 * kPi * 5.0 * t;
		p.eyeWide = 1.0 + 0.6 * trap(t, kSlip, kSlip + 0.15, 114.6, 115.4);
		p.browRaise = p.eyeWide - 1.0;
		p.mouthOpen = 0.6 * trap(t, kSlip, kSlip + 0.1, 114.2, 114.8);
		aim(p, thermometerSpot() + Vec3{0.0, 0.10, 0.0}, 0.6);
		return p;
	}
	// ---------------- on its back on the gravel, the bubble, righting itself, the head comes up ----------------
	if (t < 125.0) {
		static const Track3 pos({{117.0, {0.62, 0.80, -2.13}}, {118.6, {0.62, 0.80, -2.13}}, {119.6, {0.63, 0.80, -2.13}}, {kSurface, {0.63, 0.82, -2.13}}, {122.5, {0.62, 0.968, -2.14}}});
		p.pos = pos(t);
		p.yaw = 160.0;
		p.lift = 0.0;
		p.roll = 180.0 + 180.0 * smoothstep(118.6, 119.6, t);   // rights itself (all the way round)
		p.swimAmount = 0.5 * smoothstep(119.6, 120.2, t);
		p.swimPhase = 2.0 * kPi * 0.7 * t;
		// annoyed: flat brows, half lids, a flat mouth
		const double annoyed = smoothstep(kSurface + 0.5, 122.0, t);
		p.blink = std::max(p.blink, 0.45 * annoyed);
		p.browL = p.browR = 0.55 * annoyed;
		p.smile = 0.15 - 0.45 * annoyed;
		aim(p, t < 123.0 ? Vec3{0.30, 1.30, -1.3} : thermometerSpot() + Vec3{0.0, -0.07, 0.0}, 0.7);
		p.squint = 0.4 * smoothstep(123.0, 123.8, t);   // Plan B: the eyes go to the suction cup
		p.eyeWide = 1.0 + 0.2 * smoothstep(123.5, 124.0, t);
		return p;
	}
	// ---------------- the thermometer: bite, pull, POP, float, the idea, the ramp, the climb ----------------
	if (t < 154.0) {
		static const Track3 pos({{125.0, {0.62, 0.968, -2.14}}, {126.0, {0.43, 0.835, -2.13}}, {126.5, {0.42, 0.835, -2.13}}, {128.2, {0.42, 0.835, -2.134}}, {129.0, {0.42, 0.835, -2.13}},
								 {130.5, {0.42, 0.835, -2.142}}, {kPop + 0.35, {0.48, 0.95, -2.30}}, {131.2, {0.50, 0.968, -2.30}}, {133.0, {0.58, 0.968, -2.28}}, {137.0, {0.58, 0.968, -2.27}},
								 {139.0, {0.55, 0.968, -2.14}}, {140.8, {0.56, 0.968, -2.11}}});
		p.pos = pos(t);
		p.lift = 0.0;
		p.yaw = t < 131.2 ? 90.0 : (t < 137.0 ? 150.0 : 180.0);
		p.swimAmount = t < 140.8 ? 0.7 : 0.0;
		p.swimPhase = 2.0 * kPi * (t < 130.6 ? 1.6 : 0.8) * t;
		const Thermo th = thermoFree(t);
		if (t < kPop + 0.6) {
			const Vec3 bottom = th.pos - Vec3{0.0, 0.075, 0.0};
			aim(p, bottom, 0.9);
			p.neck = 1.0 + 0.4 * trap(t, 125.6, 126.0, kPop, kPop + 0.3);
			p.mouthOpen = 0.55 * trap(t, 125.5, 125.9, kBite - 0.1, kBite);
			const double pull = trap(t, 126.5, 126.9, 128.0, 128.3) + 1.3 * trap(t, 129.0, 129.4, 130.4, kPop);
			p.squash *= 1.0 - 0.05 * pull;
			p.squint = 0.7 * pull;
			p.browL = p.browR = 0.9 * std::min(1.0, pull);
			p.pitch = -8.0 * pull;
		} else {
			aim(p, th.pos, 0.6);
		}
		p.eyeWide = 1.0 + 0.6 * trap(t, kPop, kPop + 0.1, 131.5, 132.5) + 0.4 * trap(t, kIdea, kIdea + 0.2, 137.0, 138.0);
		p.browRaise = 1.2 * trap(t, kIdea, kIdea + 0.2, 137.0, 138.0);
		p.smile = 0.15 + 0.6 * trap(t, kIdea, kIdea + 0.3, 137.5, 138.5) + 0.3 * trap(t, 147.0, 147.6, kLampOff, kLampOff + 0.1);
		p.squash *= 1.0 + 0.06 * trap(t, kIdea - 0.1, kIdea, kIdea + 0.2, kIdea + 0.5);
		// the climb up the ramp, a slip, the stretch for the rim; then frozen in the dark, and the slide back
		if (t >= 140.8) {
			const double u = 0.55 * smoothstep(141.0, 143.4, t) - 0.12 * trap(t, 143.4, 143.6, 144.0, 144.4) + 0.47 * smoothstep(144.4, 147.0, t);
			const double down = smoothstep(kSlideBack, kSlideBack + 0.6, t);
			p.pos = lerp(lerp(rampBottom() + Vec3{0.02, 0.012, 0.0}, rampTop() + Vec3{0.02, 0.01, 0.0}, std::max(0.0, u)), Vec3{0.54, 0.968, -2.12}, down);
			p.pitch = 42.0 * (1.0 - down);
			p.walkAmount = 1.0 * (trap(t, 141.0, 141.3, 143.2, 143.4) + trap(t, 144.4, 144.6, 146.8, 147.0));
			p.walkPhase = 2.0 * kPi * 1.8 * t;
			const double reach = trap(t, 147.0, kTouch - 0.6, kSlideBack - 0.1, kSlideBack);
			p.neck = 1.0 + 0.7 * reach;
			p.frontL = {kTankX0 + 0.012, kTankRim + 0.003, -2.10};
			p.frontWeightL = smoothstep(147.6, kTouch, t) * (1.0 - down);
			aim(p, {kTankX0, kTankRim + 0.05, -2.10}, 0.8);
			const double frozen = trap(t, kLampOff, kLampOff + 0.1, kSlideBack, kSlideBack + 0.2);
			p.eyeWide = std::max(p.eyeWide, 1.0 + 0.5 * frozen);
			p.blink = frozen > 0.0 ? 0.0 : p.blink;
			p.smile = p.smile * (1.0 - frozen) - 0.2 * frozen;
		}
		return p;
	}
	// ---------------- the hose: noticed, the slate, the grab, hanging, the climb, over the top, the slide, the towels ----------------
	if (t < 212.0) {
		const Vec3 grip = t < 180.0 ? hoseIn2() : gripAt(t);
		if (t < 163.5) {
			static const Track3 pos({{154.0, {0.54, 0.968, -2.12}}, {160.0, {0.62, 0.968, -2.18}}, {163.5, {0.90, 0.972, -2.27}}});
			p.pos = pos(t);
			p.lift = 0.0;
			p.yaw = t < 160.0 ? yawTowards({0.6, 0.0, -2.15}, hoseIn2()) : -10.0;
			p.swimAmount = t < 156.0 ? 0.2 : 0.8;
			p.swimPhase = 2.0 * kPi * 0.9 * t;
			aim(p, hoseIn2() + Vec3{0.0, 0.05, 0.0}, 0.8);
			p.squint = 0.6 * smoothstep(156.0, 157.5, t);
			p.browL = p.browR = 0.6 * smoothstep(156.0, 157.5, t);
			p.smile = 0.15 + 0.35 * smoothstep(157.0, 158.0, t);
			return p;
		}
		if (t < kHang) {
			// up onto the slate, rearing up, front feet and mouth on the hose
			p.pos = lerp({0.90, 0.972, -2.27}, B + Vec3{0.04, 0.0, -0.02}, smoothstep(163.5, 165.0, t));
			p.yaw = -20.0;
			p.lift = 0.004;
			p.walkAmount = 0.9 * trap(t, 163.5, 163.8, 164.8, 165.0);
			p.walkPhase = 2.0 * kPi * 2.0 * t;
			const double rear = smoothstep(165.0, 166.4, t);
			p.pitch = 55.0 * rear;
			p.neck = 1.0 + 0.6 * rear;
			p.frontL = grip + Vec3{0.0, 0.010, -0.014};
			p.frontR = grip + Vec3{0.0, -0.006, 0.014};
			p.frontWeightL = p.frontWeightR = smoothstep(165.8, kGrab, t);
			p.mouthOpen = 0.4 * trap(t, 166.0, 166.4, kGrab, kGrab + 0.2);
			aim(p, grip, 0.9);
			p.squint = 0.5 * rear;
			return p;
		}
		// hanging below the grip, then climbing hand over hand, then over the top
		if (t < kOverTop) {
			const double swing = 8.0 * std::sin(2.0 * kPi * 0.45 * (t - kHang)) * std::exp(-0.25 * (t - kHang));
			p.yaw = -20.0;
			p.pitch = 80.0;
			p.roll = swing;
			p.lift = 0.0;
			p.neck = 1.15;
			// the shell hangs under the front feet: its middle about 8 cm below the grip
			p.pos = grip + Vec3{-0.006, -0.088, 0.03};
			const double step = std::fmod(clamp01((t - 180.0) / (kOverTop - 180.0)) * 8.0, 1.0);
			const bool leftUp = static_cast<int>(clamp01((t - 180.0) / (kOverTop - 180.0)) * 8.0) % 2 == 0;
			p.frontL = grip + Vec3{0.0, (leftUp && t > 180.0 ? 0.012 * smoothstep(0.1, 0.5, step) : 0.0), -0.014};
			p.frontR = grip + Vec3{0.0, (!leftUp && t > 180.0 ? 0.012 * smoothstep(0.1, 0.5, step) : 0.0), 0.014};
			p.frontWeightL = p.frontWeightR = 1.0;
			p.hindL = grip + Vec3{-0.02, -0.15, -0.02 + 0.01 * std::sin(6.0 * t)};
			p.hindR = grip + Vec3{-0.02, -0.15, 0.02 + 0.01 * std::cos(6.0 * t)};
			p.hindWeightL = p.hindWeightR = 0.5 * trap(t, kHang, kHang + 0.3, 171.0, 172.0);   // kicking in the air at first
			p.walkAmount = 1.3 * trap(t, kHang, kHang + 0.2, 170.5, 171.5);
			p.walkPhase = 2.0 * kPi * 4.0 * t;
			p.mouthOpen = 0.3 * trap(t, kHang, kHang + 0.1, 169.4, 170.0);
			p.eyeWide = 1.0 + 0.7 * trap(t, kHang, kHang + 0.1, 170.0, 171.0);
			p.browRaise = 1.2 * trap(t, kHang, kHang + 0.1, 170.0, 171.0);
			p.squint = 0.65 * smoothstep(176.0, 180.0, t);   // grim determination
			p.browL = p.browR = 0.8 * smoothstep(176.0, 180.0, t);
			p.smile = -0.3 * smoothstep(176.0, 180.0, t);
			p.headPitch = t > 171.0 && t < 176.0 ? 40.0 : -30.0;   // looking down into the canyon, or up the hose
			p.lookWeight = 0.0;
			return p;
		}
		if (t < kHoseSlip) {
			// over the rim: the shell tips from hanging to standing on the hose's bend outside, looking out over the room
			const double over = smoothstep(kOverTop, 195.5, t);
			const Vec3 top = Vec3{1.19, kTankRim + 0.062, -2.30};
			p.pos = lerp(gripAt(kOverTop - 0.01) + Vec3{-0.006, -0.088, 0.03}, top, over);
			p.yaw = -20.0 + 75.0 * smoothstep(195.5, 197.5, t);   // turning to face the room
			p.pitch = 80.0 * (1.0 - over) - 10.0 * trap(t, 195.0, 195.5, 196.0, 196.6);
			p.lift = 0.006;
			p.walkAmount = 1.0 * trap(t, kOverTop, kOverTop + 0.3, 195.0, 195.5) + 0.5 * trap(t, 202.3, 202.5, 202.9, kHoseSlip);
			p.walkPhase = 2.0 * kPi * 2.5 * t;
			p.mouthOpen = 0.5 * trap(t, 194.0, 194.4, 195.2, 195.6);   // a gasp at the top
			p.squash *= 1.0 - 0.08 * trap(t, 193.0, 193.6, 194.6, 195.2);
			const double awe = smoothstep(197.0, 198.5, t);
			p.eyeWide = 1.0 + 0.45 * awe;
			p.browRaise = 0.8 * awe;
			p.smile = 0.15 + 0.5 * awe;
			p.neck = 1.0 + 0.4 * awe;
			aim(p, {0.0, 0.6, 1.5}, 0.6);
			return p;
		}
		// the slide down the hose to the towels, the landing, the eyes shut, then open
		{
			const std::vector<Vec3> path = {Vec3{1.19, kTankRim + 0.05, -2.30}, hoseOut1() + Vec3{0.0, 0.03, 0.0}, hoseOut2() + Vec3{0.0, 0.03, 0.02}, hoseOut3() + Vec3{0.0, 0.02, 0.0}};
			const double u = std::pow(smoothstep(kHoseSlip + 0.3, kLand, t), 1.6);
			const Vec3 onTowel = towels() + Vec3{0.02, 0.255, 0.0};
			p.pos = t < kLand ? alongPath(path, u) : onTowel;
			p.pos.y += hopAt(t, kLand, 0.35, 0.03);
			p.yaw = t < kLand ? 60.0 : 70.0;
			p.pitch = t < kLand ? 50.0 * smoothstep(kHoseSlip, kHoseSlip + 0.4, t) : 0.0;
			p.lift = 0.0;
			p.frontL = alongPath(path, u + 0.05) + Vec3{0.0, 0.0, -0.012};
			p.frontR = alongPath(path, u + 0.05) + Vec3{0.0, 0.0, 0.012};
			p.frontWeightL = p.frontWeightR = t < kLand ? 1.0 : 0.0;
			p.squash *= 1.0 - 0.22 * trap(t, kLand - 0.02, kLand + 0.05, kLand + 0.15, kLand + 0.45);
			p.eyeWide = 1.0 + 0.7 * trap(t, kHoseSlip, kHoseSlip + 0.2, kLand - 0.4, kLand);
			p.mouthOpen = 0.8 * trap(t, kHoseSlip + 0.1, kHoseSlip + 0.3, kLand - 0.3, kLand);
			p.blink = t > kLand + 0.2 && t < kEyesOpen ? 1.0 : (t >= kEyesOpen ? std::max(p.blink, 1.0 - smoothstep(kEyesOpen, kEyesOpen + 0.5, t)) : p.blink);
			p.smile = t < kLand ? -0.4 : 0.15 + 0.45 * smoothstep(kEyesOpen + 0.6, kEyesOpen + 1.6, t);
			p.neck = t < kLand ? 0.5 : 0.8 + 0.3 * smoothstep(kEyesOpen, kEyesOpen + 1.0, t);
			aim(p, {0.5, 0.4, 0.5}, 0.4);
			return p;
		}
	}
	// ---------------- the floor: down from the towels, the vacuum, the sofa, the rug, the shadows, the far edge ----------------
	if (t < 268.0) {
		double speed = 0.0;
		if (t < 214.5) {
			p.pos = lerp(towels() + Vec3{0.02, 0.255, 0.0}, floorRoute().front().p, smoothstep(212.0, 214.5, t));
			p.pos.y = (towels().y + 0.255) * (1.0 - smoothstep(212.2, 214.3, t));
			p.yaw = 120.0;
			p.pitch = -30.0 * trap(t, 212.2, 212.6, 214.0, 214.4);
			p.walkAmount = 0.8;
			p.walkPhase = 2.0 * kPi * 1.5 * t;
			aim(p, floorRoute().front().p + Vec3{-0.3, 0.0, 0.3}, 0.5);
			return p;
		}
		p.pos = floorPos(t, speed);
		double dummy;
		const Vec3 next = floorPos(t + 0.3, dummy);
		p.yaw = length(next - p.pos) > 1e-4 ? yawTowards(p.pos, next) : (t < 236.0 ? 125.0 : 60.0);
		if (t > 221.0 && t < kHide) p.yaw = 125.0;
		if (t > 256.0) p.yaw = 20.0;
		p.walkAmount = std::min(1.2, speed / 0.10);
		p.walkPhase = 2.0 * kPi * floorDistance(t) / 0.06;
		p.lift = 0.012;
		// the vacuum: frozen stiff, eyes wide; then the dash under the sofa
		Vec3 vp;
		double vy, von;
		vacuumAt(t, vp, vy, von);
		const double fear = trap(t, kVacuumOn + 1.0, kVacuumOn + 1.5, 230.5, 232.0);
		p.eyeWide = 1.0 + 0.6 * fear;
		p.browRaise = fear;
		p.browL = p.browR = -0.6 * fear;
		p.smile = 0.15 - 0.5 * fear;
		p.neck = 1.0 - 0.5 * trap(t, kVacuumOn + 1.5, kVacuumOn + 2.0, kHide, kHide + 0.2) - 0.3 * trap(t, kHide + 1.2, kHide + 1.5, 230.5, 231.5);
		if (t > kVacuumOn && t < 232.0) aim(p, vp + Vec3{0.0, 0.06, 0.0}, 0.9);
		// the rug's edge: rearing up, front feet on the top, slipping back once, then over
		const double climb = trap(t, kRugEdge, kRugEdge + 0.5, kRugTop - 0.4, kRugTop);
		if (climb > 0.0) {
			p.pitch = 28.0 * climb * (1.0 - 0.6 * trap(t, 239.6, 239.9, 240.6, 241.0));
			p.frontL = {p.pos.x - 0.02, kRugThick + 0.002, 0.645};
			p.frontR = {p.pos.x + 0.02, kRugThick + 0.002, 0.645};
			p.frontWeightL = p.frontWeightR = climb * (1.0 - trap(t, 239.6, 239.9, 240.6, 241.0));
			p.walkAmount = 1.4 * climb;
			p.walkPhase = 2.0 * kPi * 4.0 * t;
			p.squint = 0.7 * climb;
			p.browL = p.browR = 0.7 * climb;
			p.mouthOpen = 0.3 * climb;
			aim(p, {p.pos.x, 0.05, 1.2}, 0.5);
		}
		// the fan's shadows: stopping dead under each, eyes darting
		const double hold = trap(t, 247.4, 247.6, 249.1, 249.3) + trap(t, 251.5, 251.7, 252.9, 253.1);
		if (hold > 0.0) {
			p.neck = 1.0 - 0.6 * hold;
			p.eyeWide = 1.0 + 0.3 * hold;
			p.lookWeight = 1.0;
			p.look = p.pos + Vec3{0.3 * std::sin(3.0 * t), 0.4, 0.3 * std::cos(3.0 * t)};
		}
		// the far edge: flat on its belly, panting; then the sound of water; the head comes up
		const double spent = trap(t, 257.6, 258.2, 262.0, 263.0);
		p.lift *= 1.0 - spent;
		p.squash *= 1.0 - 0.06 * spent;
		p.mouthOpen = std::max(p.mouthOpen, 0.45 * spent * (0.6 + 0.4 * std::sin(2.0 * kPi * 1.1 * t)));
		p.blink = std::max(p.blink, 0.5 * spent);
		const double hear = smoothstep(kPlip + 0.4, kPlip + 2.4, t);
		if (hear > 0.0) {
			p.neck = 1.0 + 0.6 * hear;
			aim(p, {2.0, 0.17, 2.25}, 0.9);
			p.eyeWide = 1.0 + 0.4 * hear;
			p.browRaise = 0.9 * hear;
			p.smile = 0.15 + 0.4 * smoothstep(264.0, 266.0, t);
		}
		return p;
	}
	// ---------------- the tub: the crawl, the box, the look down, the look back, the smile, the SPLASH, swimming ----------------
	if (t < 300.0) {
		if (t < 277.0) {
			double speed = 0.0;
			p.pos = floorPos(t, speed);
			p.yaw = 8.0;
			p.walkAmount = std::min(1.0, speed / 0.08);
			p.walkPhase = 2.0 * kPi * floorDistance(t) / 0.06;
			aim(p, {2.0, 0.17, 2.25}, 0.6);
			p.smile = 0.3;
			p.squint = 0.2;
			return p;
		}
		const Vec3 box = cardboardBox();
		if (t < kSplash) {
			static const Track3 pos({{277.0, {1.40, 0.0, 2.17}}, {278.5, {1.42, 0.03, 2.20}}, {kOnBox, {box.x - 0.01, kBoxH, box.z}}, {281.0, {box.x + 0.03, kBoxH, box.z}}});
			p.pos = pos(t);
			p.yaw = 0.0;
			p.pitch = 30.0 * trap(t, 277.2, 277.6, 279.6, kOnBox) + 28.0 * trap(t, 281.0, 281.6, 284.0, 284.6);
			p.walkAmount = 1.2 * trap(t, 277.2, 277.5, 280.2, kOnBox);
			p.walkPhase = 2.0 * kPi * 3.0 * t;
			p.lift = 0.010;
			// peeking over the tub's rim
			const double peek = trap(t, 281.0, 281.6, 289.4, 290.0);
			p.frontL = {kTubX0 + 0.004, kTubH + 0.004, box.z - 0.025};
			p.frontR = {kTubX0 + 0.004, kTubH + 0.004, box.z + 0.025};
			p.frontWeightL = p.frontWeightR = peek * (1.0 - trap(t, 284.0, 284.4, 286.6, 287.0));
			p.neck = 1.0 + 0.6 * peek;
			aim(p, {2.05, 0.06, 2.25}, 0.8);
			p.eyeWide = 1.0 + 0.6 * trap(t, 281.6, 282.0, 283.6, 284.0);
			p.browRaise = trap(t, 281.6, 282.0, 283.6, 284.0);
			// looking back across the room at the tank (the body turns part of the way)
			const double back = trap(t, 284.0, 284.6, 286.6, 287.2);
			p.yaw -= 70.0 * back;
			if (back > 0.0) aim(p, {0.70, 1.05, -2.2}, 0.9);
			p.smile = 0.15 + 0.7 * smoothstep(287.4, 288.4, t);
			p.squint = 0.4 * smoothstep(287.4, 288.4, t);   // a happy squint
			p.squash *= 1.0 - 0.10 * trap(t, 289.8, 290.1, 290.2, 290.35);   // crouching for the jump
			// the jump
			const double jump = smoothstep(290.2, kSplash, t);
			if (jump > 0.0) {
				p.pos = lerp(p.pos, {1.86, kTubWater - 0.01, 2.25}, jump);
				p.pos.y += hopAt(t, 290.2, kSplash - 290.2, 0.11);
				p.pitch = -20.0 + 60.0 * jump;
				p.frontWeightL = p.frontWeightR = 0.0;
			}
			return p;
		}
		// swimming happily in the tub, round and round
		const double a = (t - kSplash) * 0.7;
		p.pos = lerp(Vec3{1.86, kTubWater - 0.01, 2.25}, Vec3{2.01 + 0.15 * std::cos(a), 0.065, 2.25 + 0.09 * std::sin(a)}, smoothstep(kSplash, kSplash + 1.2, t));   // (from where it landed)
		p.yaw = a * 180.0 / kPi + 90.0;
		p.pitch = -10.0 * smoothstep(kSplash, kSplash + 0.4, t) * (1.0 - smoothstep(kSplash + 1.0, kSplash + 2.0, t));
		p.lift = 0.0;
		p.swimAmount = 1.0;
		p.swimPhase = 2.0 * kPi * 1.2 * t;
		p.smile = 0.8;
		p.squint = 0.35;
		p.lookWeight = 0.0;
		return p;
	}
	// ---------------- scene 7: floating among the lettuce; the head comes up; it sinks ----------------
	{
		static const Track3 pos({{300.0, {2.10, 0.075, 2.30}}, {kHeadUp - 0.4, {2.10, 0.075, 2.30}}, {kHeadUp + 1.6, {2.10, 0.105, 2.30}}, {kSink, {2.10, 0.105, 2.30}}, {kSink + 2.0, {2.10, 0.060, 2.30}}});
		p.pos = pos(t);
		p.yaw = -100.0;   // facing the room
		p.lift = 0.0;
		p.swimAmount = 0.25;
		p.swimPhase = 2.0 * kPi * 0.3 * t;
		p.blink = std::max(p.blink, 0.6 * (1.0 - smoothstep(kHeadUp - 1.0, kHeadUp, t)));   // drowsing
		p.smile = 0.5;
		p.neck = 1.0 + 0.3 * trap(t, kHeadUp, kHeadUp + 1.0, kSink, kSink + 1.0);
		const Vec3 owner = {0.5, 1.6, 1.2};
		if (t > kHeadUp - 0.5) aim(p, owner, 0.7);
		p.eyeWide = 1.0 + 0.3 * trap(t, kHeadUp + 0.5, kHeadUp + 1.0, kSink - 0.4, kSink);
		p.squint = 0.3 * smoothstep(kSink - 0.8, kSink, t);   // a knowing look, then under
		return p;
	}
}

inline Thermo thermoAt(double t) {
	// in the mouth as the turtle flies back with it; otherwise where thermoFree says
	if (t >= kPop && t < 131.4) {
		const TurtlePose p = turtleAt(t);
		return {1.0, turtle::headOf(turtle::Look{}, p) + Vec3{0.0, 0.0, 0.05}, 90.0 * smoothstep(kPop, 131.3, t)};
	}
	return thermoFree(t);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The owner's performance
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
inline bool ownerOn(double t) { return t < kDoorClick || t > kHome + 0.4; }

struct OwnerKey {
	double t;
	Vec3 p;
};
inline const std::vector<OwnerKey> &ownerRoute() {
	static const std::vector<OwnerKey> k = {
		{0.0, {0.70, 0.0, -1.62}},  {20.6, {0.70, 0.0, -1.62}}, {22.2, {0.72, 0.0, -1.22}},   // tidying, then a step back to admire
		{29.6, {0.72, 0.0, -1.22}}, {30.6, {0.42, 0.0, -1.60}}, {31.6, {0.42, 0.0, -1.60}},   // the keys from the cabinet
		{35.6, {2.65, 0.0, 1.10}},  {36.6, {3.35, 0.0, 1.16}},                               // out of the door
		// scene 7
		{kHome + 0.4, {3.35, 0.0, 1.16}}, {303.2, {2.35, 0.0, 1.00}}, {307.0, {2.35, 0.0, 1.00}}, {312.5, {1.00, 0.0, -1.45}},
		{316.0, {1.00, 0.0, -1.45}}, {317.2, {0.75, 0.0, -1.80}}, {319.2, {0.75, 0.0, -1.80}}, {320.3, {1.45, 0.0, -1.95}},
		{321.6, {1.45, 0.0, -1.95}}, {323.2, {-0.25, 0.0, -1.15}}, {325.0, {-0.25, 0.0, -1.15}}, {326.6, {-1.0, 0.0, 0.95}},
		{330.0, {-1.0, 0.0, 0.95}}, {338.0, {1.60, 0.0, 1.40}},
	};
	return k;
}
inline Vec3 ownerPos(double t, double &speed) {
	const auto &k = ownerRoute();
	speed = 0.0;
	if (t <= k.front().t) return k.front().p;
	for (std::size_t i = 1; i < k.size(); ++i)
		if (t <= k[i].t) {
			const double u = (t - k[i - 1].t) / (k[i].t - k[i - 1].t), s = u * u * (3.0 - 2.0 * u);
			speed = length(k[i].p - k[i - 1].p) / (k[i].t - k[i - 1].t) * 6.0 * u * (1.0 - u);
			return lerp(k[i - 1].p, k[i].p, s);
		}
	return k.back().p;
}
inline double ownerDistance(double t) {
	double d = 0.0;
	const auto &k = ownerRoute();
	for (std::size_t i = 1; i < k.size(); ++i) {
		if (t <= k[i - 1].t) break;
		const double u = clamp01((t - k[i - 1].t) / (k[i].t - k[i - 1].t));
		d += length(k[i].p - k[i - 1].p) * u * u * (3.0 - 2.0 * u);
	}
	return d;
}

inline HumanPose ownerAt(double t) {
	HumanPose p;
	double speed = 0.0;
	p.pos = ownerPos(t, speed);
	double dummy;
	const Vec3 next = ownerPos(t + 0.3, dummy);
	ownerPos(t, speed);
	p.walkAmount = std::min(1.0, speed / 1.0);
	p.walkPhase = 2.0 * kPi * ownerDistance(t) / 1.30;
	p.yaw = speed > 0.05 && length(next - p.pos) > 1e-3 ? yawTowards(p.pos, next) : -90.0;
	p.blink = actor::autoBlink(t, 11);
	p.lookWeight = 1.0;
	const Vec3 tank = {0.70, 1.0, -2.2};
	p.look = tank;
	// ---- scene 1 ----
	if (t < kDoorClick) {
		if (speed < 0.05) p.yaw = t < 29.6 ? -90.0 : (t < 31.6 ? -100.0 : -90.0);
		// tidying: the right hand straightens a plant, taps the thermometer, drops pellets
		const double plant = trap(t, 5.8, 6.8, 10.2, 11.0), tap = trap(t, 11.2, 12.2, 14.6, 15.3), feed = trap(t, 15.6, 16.4, 18.6, 19.6);
		p.handWeightR = std::max(plant, std::max(tap, feed)) + 0.6 * trap(t, 0.5, 1.5, 4.5, 5.6);
		p.handR = {0.98, 1.0 + 0.02 * std::sin(2.0 * kPi * 0.6 * t), -2.33};
		if (t < 5.7) p.handR = {0.60 + 0.15 * std::sin(1.3 * t), kTankRim + 0.02, -2.10};   // straightening the lamp's cord, the rim
		if (tap > 0.0) p.handR = {0.42, 0.95 + 0.006 * std::abs(std::sin(2.0 * kPi * 2.5 * t)), kTankZ1 + 0.03};
		if (feed > 0.0) p.handR = {0.60, 1.32, -2.18};
		p.lean = 18.0 * std::max(plant, tap) + 8.0 * feed;
		p.look = t < 11.0 ? Vec3{0.98, 1.0, -2.33} : (t < 15.4 ? thermometerSpot() : (t < 20.6 ? Vec3{0.58, 1.0, -2.2} : turtle::headOf(turtle::Look{}, turtleAt(t))));
		p.headPitch = 20.0 * std::max(plant, std::max(tap, feed));
		// admiring it, hands on the hips: "Perfect."
		const double admire = trap(t, 22.4, 23.0, 29.4, 29.8);
		p.handWeightL = std::max(p.handWeightL, admire);
		p.handWeightR = std::max(p.handWeightR, admire);
		if (admire > 0.0) {
			const Vec3 r = rightOf(p.yaw), f = frontOf(p.yaw);
			p.handL = p.pos + Vec3{0.0, 0.95, 0.0} - r * 0.22 - f * 0.02;
			p.handR = p.pos + Vec3{0.0, 0.95, 0.0} + r * 0.22 - f * 0.02;
		}
		p.smile = 0.3 + 0.6 * smoothstep(23.0, kPerfect, t);
		p.mouthOpen = 0.25 * trap(t, kPerfect, kPerfect + 0.15, kPerfect + 0.6, kPerfect + 0.8);
		p.blink = std::max(p.blink, 0.6 * trap(t, kPerfect - 0.2, kPerfect, kPerfect + 0.7, kPerfect + 1.0));   // a satisfied blink
		// the keys
		const double reach = trap(t, 30.4, 30.9, 31.2, 31.7);
		if (reach > 0.0) {
			p.handWeightR = reach;
			p.handR = {0.32, 0.79, -1.99};
			p.lean = 25.0 * reach;
		}
		p.keys = t > kKeys;
		if (t > 32.0) p.look = Vec3{2.95, 1.6, 1.16};
		// the door: the left hand on the handle
		const double handle = trap(t, 35.0, 35.4, 36.3, 36.6);
		if (handle > 0.0) {
			p.handWeightL = handle;
			p.handL = {2.97, 1.0, 1.06};
		}
		return p;
	}
	// ---- scene 7 ----
	p.bags = t < kBagsDown;
	p.look = t < 304.0 ? Vec3{1.5, 1.4, 0.0} : tank;
	if (speed < 0.05) p.yaw = t < 312.5 ? yawTowards(p.pos, {0.7, 0.0, -2.2}) : p.yaw;
	if (p.bags) {
		const Vec3 r = rightOf(p.yaw), f = frontOf(p.yaw);
		p.handL = p.pos + Vec3{0.0, 0.70, 0.0} - r * 0.30 + f * 0.05;
		p.handR = p.pos + Vec3{0.0, 0.70, 0.0} + r * 0.30 + f * 0.05;
		p.handWeightL = p.handWeightR = 1.0;
	}
	// something is wrong
	const double wrong = trap(t, 304.4, 305.4, 312.0, 313.0);
	p.browTilt = 0.6 * wrong;
	p.smile = 0.3 - 0.6 * wrong;
	p.headPitch = -5.0 * wrong;
	// the bags go down, slowly
	const double down = trap(t, 313.0, 314.4, kBagsDown, 316.4);
	p.crouch = 0.6 * down;
	p.lean = 20.0 * down;
	// searching: inside the tank, behind it, under the table, behind the sofa
	const double inside = trap(t, 317.2, 317.8, 318.8, 319.3), behind = trap(t, 320.3, 320.8, 321.3, 321.7), under = trap(t, 323.2, 323.8, 324.6, 325.0), sofa = trap(t, 326.6, 327.2, 328.2, 328.6);
	p.lean += 35.0 * inside + 25.0 * behind + 30.0 * under + 45.0 * sofa;
	p.crouch = std::max(p.crouch, 0.95 * under);
	p.headPitch += 35.0 * inside + 50.0 * under + 40.0 * sofa;
	if (inside > 0.0) p.yaw = -90.0;
	if (behind > 0.0) p.yaw = -60.0, p.look = {1.2, 0.8, -2.48};
	if (under > 0.0) p.yaw = 180.0, p.look = {-1.0, 0.2, -1.15};
	if (sofa > 0.0) p.yaw = -90.0, p.look = {-1.0, 0.2, 0.55};
	const double worry = smoothstep(317.0, 322.0, t);
	p.browTilt = std::max(p.browTilt, 0.9 * worry);
	p.smile = std::min(p.smile, 0.3 - 1.0 * worry);
	p.eyeWide = 1.0 + 0.3 * worry + 0.5 * trap(t, kCall - 0.6, kCall - 0.2, kCall + 1.6, kCall + 2.4);
	p.browRaise = 0.6 * trap(t, kCall - 0.6, kCall - 0.2, kCall + 1.6, kCall + 2.4);
	// "Turtle?": both hands to the head
	const double call = trap(t, kCall - 0.6, kCall - 0.2, kCall + 1.0, kCall + 1.6);
	p.mouthOpen = 0.6 * trap(t, kCall, kCall + 0.1, kCall + 0.6, kCall + 0.9);
	if (call > 0.0) {
		const Vec3 head = human::headCentre(p);
		p.handL = head + Vec3{0.0, 0.05, 0.0} - rightOf(p.yaw) * 0.14;
		p.handR = head + Vec3{0.0, 0.05, 0.0} + rightOf(p.yaw) * 0.14;
		p.handWeightL = p.handWeightR = call;
	}
	if (t > 330.0) p.look = p.pos + frontOf(p.yaw) * 2.0 + Vec3{0.0, 0.6 + 0.6 * std::sin(2.0 * t), 0.0};
	return p;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The set at time t
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
inline SetState setAt(double t) {
	SetState s;
	s.t = t;
	s.light = lightAt(t);
	s.heatLamp = t < kLampOff ? 1.0 : 0.0;
	s.door = trap(t, 35.4, 35.9, 36.6, kDoorClick) + trap(t, kHome, kHome + 0.8, 305.0, 306.2);
	s.fanTurn = 160.0 * t;   // the fan turns all the time (a hot day)
	s.roomLamp = smoothstep(kLightOn, kLightOn + 0.15, t);
	s.clockMinutes = clockMinutesAt(t);
	// the rocks
	s.rockOffset.assign(tankRocks().size(), Vec3{0.0, 0.0, 0.0});
	s.rockTilt.assign(tankRocks().size(), 0.0);
	s.rockOffset[0] = stubbornOffset(t);
	for (int k = 0; k < 4; ++k) {
		s.rockOffset[static_cast<std::size_t>(k + 1)] = pileRockAt(k, t) - tankRocks()[static_cast<std::size_t>(k + 1)].home;
		if (t > kSlip) s.rockTilt[static_cast<std::size_t>(k + 1)] = 65.0 * smoothstep(kSlip, kSlip + 0.9, t) * (k % 2 ? 1.0 : -1.0);
	}
	const Thermo th = thermoAt(t);
	s.thermoState = th.state;
	s.thermoPos = th.pos;
	s.thermoTilt = th.tilt;
	// the hose: sagging under the turtle, slipping out over the rim
	if (t > kHang && t < kOverTop + 3.0) {
		s.hoseSag = 0.012;
		s.hoseGrip = gripAt(t);
	}
	s.hoseOut = smoothstep(kHoseSlip, kHoseSlip + 1.4, t);
	s.drip = t > kHome ? smoothstep(kHome, kHome + 10.0, t) : 0.0;
	s.pellets = {kPellets};
	s.tankRipples = {{0.60, -2.18, kPellets, 0.4},  {0.93, -2.18, kDive, 1.6},        {0.92, -2.20, kDive + 0.25, 0.7}, {0.50, -2.09, kCrash, 1.2},
					 {0.48, -2.30, kPop + 0.4, 1.2}, {0.62, -2.14, kSurface + 1.2, 0.4}, {0.54, -2.12, kSlideBack + 0.6, 1.0}, {1.06, -2.30, kHang + 0.1, 0.3},
					 {0.62, -2.14, kBubble + 2.4, 0.15}};
	s.tubRipples = {{1.86, 2.25, kSplash, 2.0}, {1.90, 2.22, kSplash + 0.3, 0.8}, {2.10, 2.30, kSink + 0.3, 0.4}, {2.10, 2.30, kLastBubble + 1.2, 0.12}, {2.1, 2.3, kPlip, 0.2}};
	vacuumAt(t, s.vacuumPos, s.vacuumYaw, s.vacuumOn);
	s.bagsOnFloor = t > kBagsDown;
	s.blueprint = (t >= kPlan && t < 71.0) || (t >= kPlanB - 0.2 && t < kPlanB + 1.6);
	return s;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// Bubbles, stars, and the blueprint's lines and words
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
inline std::string sphereAt(Vec3 c, double r) {
	char buf[160];
	std::snprintf(buf, sizeof(buf), "AttributeBegin\n  Translate %.5f %.5f %.5f\n  Shape \"sphere\" \"float radius\" [ %.5f ]\nAttributeEnd\n", -c.x, c.y, c.z, r);
	return buf;
}
inline std::string buildBubbles(double t, const TurtlePose &tp) {
	std::string b;
	// the turtle breathes out now and then under water (scene 2 and 3), in short strings
	const bool under = (t > kDive && t < kSurface + 1.0 && !(t > kClock && t < 100.0)) || (t > 125.5 && t < 131.0) || (t > kSplash && t < 300.0) || (t > 300.0 && t < kHeadUp - 0.3);
	if (under) {
		const Vec3 head = turtle::headOf(turtle::Look{}, tp);
		for (int k = 0; k < 6; ++k) {
			const double age = std::fmod(t * 0.8 + k * 0.37, 2.4);
			const double top = t > kSplash ? kTubWater : kWater;
			const Vec3 p = head + Vec3{0.004 * std::sin(9.0 * age + k), 0.06 * age, 0.004 * std::cos(7.0 * age + k)};
			if (p.y < top - 0.003) b += sphereAt(p, 0.0016 + 0.0008 * (k % 3));
		}
	}
	// the one bubble after the crash, and the very last one
	const auto single = [&](double t0, Vec3 from, double top, double r) {
		const double a = t - t0;
		if (a < 0.0 || a > 3.0) return;
		const Vec3 p = from + Vec3{0.003 * std::sin(4.0 * a), 0.07 * a, 0.0};
		if (p.y < top) b += sphereAt(p, r * (1.0 + 0.2 * a));
	};
	single(kBubble, {0.63, 0.83, -2.10}, kWater, 0.004);
	single(kLastBubble, {2.10, 0.06, 2.30}, kTubWater, 0.003);
	return b.empty() ? std::string() : attr("Material \"dielectric\" \"float eta\" [ 1.1 ]", b);
}
// BONK: three little stars round the turtle's head for a moment.
inline std::string buildStars(double t, const TurtlePose &tp) {
	const double a = trap(t, kBonk + 0.05, kBonk + 0.2, 90.6, 91.2);
	if (a <= 0.0) return std::string();
	std::string out;
	const Vec3 head = turtle::headOf(turtle::Look{}, tp) + Vec3{0.0, 0.03, 0.0};
	for (int k = 0; k < 3; ++k) {
		const double ang = 2.0 * kPi * (k / 3.0 + 0.8 * (t - kBonk));
		out += emissiveSphere(head + Vec3{0.03 * std::cos(ang), 0.005 * std::sin(3.0 * ang), 0.03 * std::sin(ang)}, 0.0035, Vec3{30.0, 26.0, 8.0} * a);
	}
	return out;
}

// A stroke font for the blueprint's words: each glyph a list of segments in a 4 x 6 cell (x right, y up).
inline std::vector<std::array<double, 4>> glyph(char c) {
	switch (c) {
		case 'A': return {{0, 0, 2, 6}, {2, 6, 4, 0}, {1, 3, 3, 3}};
		case 'B': return {{0, 0, 0, 6}, {0, 6, 3, 6}, {3, 6, 4, 5}, {4, 5, 3, 3}, {0, 3, 3, 3}, {3, 3, 4, 2}, {4, 2, 4, 1}, {4, 1, 3, 0}, {3, 0, 0, 0}};
		case 'C': return {{4, 6, 0, 6}, {0, 6, 0, 0}, {0, 0, 4, 0}};
		case 'E': return {{4, 6, 0, 6}, {0, 6, 0, 0}, {0, 0, 4, 0}, {0, 3, 3, 3}};
		case 'I': return {{2, 0, 2, 6}, {1, 6, 3, 6}, {1, 0, 3, 0}};
		case 'J': return {{4, 6, 4, 1}, {4, 1, 3, 0}, {3, 0, 1, 0}, {1, 0, 0, 1}};
		case 'L': return {{0, 6, 0, 0}, {0, 0, 4, 0}};
		case 'N': return {{0, 0, 0, 6}, {0, 6, 4, 0}, {4, 0, 4, 6}};
		case 'O': return {{0, 0, 0, 6}, {0, 6, 4, 6}, {4, 6, 4, 0}, {4, 0, 0, 0}};
		case 'P': return {{0, 0, 0, 6}, {0, 6, 4, 6}, {4, 6, 4, 3}, {4, 3, 0, 3}};
		case 'S': return {{4, 6, 0, 6}, {0, 6, 0, 3}, {0, 3, 4, 3}, {4, 3, 4, 0}, {4, 0, 0, 0}};
		case 'T': return {{0, 6, 4, 6}, {2, 6, 2, 0}};
		case 'V': return {{0, 6, 2, 0}, {2, 0, 4, 6}};
		case ':': return {{2, 1, 2, 1.6}, {2, 4, 2, 4.6}};
		default: return {};
	}
}
// Words lying on a plane (the floor of the plan): `origin` the bottom left, `right` and `up` the directions, `size` a letter's height. Letters appear one by one
// from `t0`, `rate` a second.
inline std::string strokeText(const std::string &text, Vec3 origin, Vec3 right, Vec3 up, double size, double t, double t0, double rate) {
	std::string out;
	const double cell = size / 6.0;
	for (std::size_t i = 0; i < text.size(); ++i) {
		if (t < t0 + static_cast<double>(i) / rate) break;
		const Vec3 o = origin + right * (5.2 * cell * static_cast<double>(i));
		for (const auto &s : glyph(text[i])) out += bar(o + right * (s[0] * cell) + up * (s[1] * cell), o + right * (s[2] * cell) + up * (s[3] * cell), cell * 0.32, 5);
	}
	return out;
}

// The blueprint overlay: a grid on the floor of the plan, outlines of the tank, the dashed route up the hose, the X on the rim, and the words.
inline std::string buildBlueprintOverlay(double t) {
	std::string white, red;
	const double y = kTankRim + 0.012;
	if (t < 71.0) {
		// grid lines over the cabinet's area, and the tank's outline, drawn in as the shot runs
		const double draw = smoothstep(kPlan + 0.3, kPlan + 2.5, t);
		for (int k = 0; k <= 12; ++k) {
			const double x = -0.30 + 0.20 * k;
			white += bar({x, 0.01, -2.45}, {x, 0.01, -2.45 + 2.4 * draw}, 0.003, 4);
		}
		for (int k = 0; k <= 12; ++k) {
			const double z = -2.45 + 0.20 * k;
			white += bar({-0.30, 0.01, z}, {-0.30 + 2.4 * draw, 0.01, z}, 0.003, 4);
		}
		const Vec3 c[4] = {{kTankX0, y, kTankZ0}, {kTankX1, y, kTankZ0}, {kTankX1, y, kTankZ1}, {kTankX0, y, kTankZ1}};
		for (int k = 0; k < 4; ++k) white += bar(c[k], lerp(c[k], c[(k + 1) % 4], draw), 0.004, 4);
		// the route: from the slate to the hose, dashes appearing; the X at the rim
		const double route = smoothstep(kPlan + 2.5, kPlan + 4.5, t);
		const Vec3 a = basking() + Vec3{0.0, 0.18, 0.0}, b = hoseIn1() + Vec3{0.0, 0.03, 0.0}, c2 = hoseOut1() + Vec3{0.0, 0.03, 0.0};
		for (int k = 0; k < 10; ++k) {
			const double u0 = k / 10.0, u1 = u0 + 0.05;
			if (u0 > route) break;
			red += bar(lerp(a, b, u0 * 2.0 > 1.0 ? 1.0 : u0 * 2.0), lerp(a, b, std::min(1.0, u1 * 2.0)), 0.005, 4);
			if (u0 > 0.5) red += bar(lerp(b, c2, (u0 - 0.5) * 2.0), lerp(b, c2, std::min(1.0, (u1 - 0.5) * 2.0)), 0.005, 4);
		}
		if (t > kPlan + 4.5) {
			const double pulse = 1.0 + 0.15 * std::sin(2.0 * kPi * 2.0 * t);
			red += bar(b + Vec3{-0.04, 0.0, -0.04} * pulse, b + Vec3{0.04, 0.0, 0.04} * pulse, 0.007, 4) + bar(b + Vec3{-0.04, 0.0, 0.04} * pulse, b + Vec3{0.04, 0.0, -0.04} * pulse, 0.007, 4);
		}
		white += strokeText("OBJECTIVE: ESCAPE", {-0.10, 0.85, -1.38}, {1.0, 0.0, 0.0}, {0.0, 0.0, -1.0}, 0.075, t, kPlan + 5.0, 9.0);
	} else {
		white += strokeText("PLAN B", {0.18, kTankRim + 0.02, -1.95}, {1.0, 0.0, 0.0}, {0.0, 0.0, -1.0}, 0.06, t, kPlanB, 8.0);
		const Vec3 cup = thermometerSpot() + Vec3{0.0, -0.06, 0.0};
		red += emitMesh(torus(0.035, 0.004, 32, 6), translate(cup.x, kTankRim + 0.01, cup.z - 0.03));
	}
	return attr("Material \"diffuse\" \"rgb reflectance\" [ 0.92 0.97 1.0 ]", white) + attr("Material \"diffuse\" \"rgb reflectance\" [ 0.95 0.18 0.12 ]", red);
}

// The plan's look: every surface drawn in pale blueprint blue, lit flatly from above (no lamps, no shadows to speak of).
inline std::string blueprintify(const std::string &world) {
	std::string out;
	std::size_t pos = 0;
	while (pos < world.size()) {
		std::size_t end = world.find('\n', pos);
		if (end == std::string::npos) end = world.size();
		const std::string line = world.substr(pos, end - pos);
		pos = end + 1;
		if (line.find("LightSource") != std::string::npos) continue;   // (AreaLightSource and LightSource lines both)
		if (line.find("Material \"") != std::string::npos) {
			out += "  Material \"diffuse\" \"rgb reflectance\" [ 0.16 0.28 0.56 ]\n";
			continue;
		}
		out += line + "\n";
	}
	return out;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The shots
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
inline Shot shotOf(double t0, double t1, Vec3 e0, Vec3 e1, Vec3 g0, Vec3 g1, double fov, double lens) {
	return {t0, Track3({{t0, e0}, {t1, e1}}), Track3({{t0, g0}, {t1, g1}}), Track1({{t0, fov}, {t1, fov}}), lens};
}
inline const std::vector<Shot> &shots() {
	static const std::vector<Shot> s = [] {
		std::vector<Shot> v;
		const Vec3 B = basking();
		const auto add = [&](double t0, double t1, Vec3 e0, Vec3 e1, Vec3 g0, Vec3 g1, double fov, double lens = 0.004) { v.push_back(shotOf(t0, t1, e0, e1, g0, g1, fov, lens)); };
		// ---- scene 1: the perfect tank ----
		add(0.0, 6.0, {-1.55, 1.55, 1.95}, {-0.95, 1.45, 1.15}, {0.75, 1.05, -2.1}, {0.75, 1.0, -2.1}, 48.0, 0.01);
		add(6.0, 11.0, {0.80, 1.22, -1.70}, {0.84, 1.20, -1.78}, {0.98, 1.0, -2.33}, {0.98, 1.0, -2.33}, 34.0);
		add(11.0, 15.5, {0.50, 1.00, -1.78}, {0.49, 0.99, -1.83}, {0.42, 0.93, -2.045}, {0.42, 0.93, -2.045}, 28.0);
		add(15.5, 20.5, {0.38, 1.34, -1.90}, {0.40, 1.32, -1.95}, {0.58, 0.98, -2.22}, {0.58, 0.98, -2.22}, 40.0);
		add(20.5, 26.0, {0.88, 1.06, -2.36}, {0.84, 1.06, -2.36}, {0.72, 1.55, -1.30}, {0.72, 1.62, -1.22}, 46.0);
		add(26.0, 29.5, {0.92, 1.10, -2.06}, {0.93, 1.10, -2.08}, B + Vec3{-0.03, 0.04, 0.08}, B + Vec3{-0.03, 0.04, 0.08}, 30.0, 0.002);
		add(29.5, 33.0, {0.10, 1.50, -0.55}, {0.15, 1.48, -0.62}, {0.55, 1.45, -1.40}, {0.45, 1.35, -1.55}, 40.0);
		add(33.0, 37.5, {0.92, 1.13, -2.12}, {0.92, 1.13, -2.12}, {2.85, 1.15, 1.05}, {2.85, 1.15, 1.10}, 56.0);
		add(37.5, 41.0, {1.10, 1.09, -2.10}, {1.09, 1.09, -2.11}, B + Vec3{-0.03, 0.04, 0.07}, B + Vec3{-0.03, 0.045, 0.07}, 28.0, 0.002);
		add(41.0, 44.0, {0.96, 1.04, -2.17}, {0.96, 1.04, -2.17}, {1.0, 1.30, -2.30}, {1.0, 1.32, -2.30}, 52.0);
		add(44.0, kSpy, {1.07, 1.08, -2.12}, {1.07, 1.08, -2.13}, B + Vec3{-0.03, 0.045, 0.07}, B + Vec3{-0.03, 0.045, 0.07}, 30.0, 0.002);
		add(kSpy, 50.0, {0.995, 1.083, -2.18}, {0.995, 1.082, -2.185}, B + Vec3{-0.033, 0.046, 0.085}, B + Vec3{-0.033, 0.046, 0.085}, 22.0, 0.001);
		// ---- scene 2: the mission ----
		add(50.0, 52.6, {1.12, 1.05, -2.10}, {1.12, 1.06, -2.12}, {0.40, 1.07, -2.04}, {0.40, 1.08, -2.04}, 34.0);
		add(52.6, 55.2, {0.70, 1.005, -2.14}, {0.70, 1.005, -2.14}, {0.70, 1.21, -2.035}, {0.70, 1.215, -2.035}, 62.0);
		add(55.2, 57.8, {0.47, 0.95, -2.13}, {0.46, 0.945, -2.12}, {0.42, 0.93, -2.045}, {0.42, 0.93, -2.045}, 26.0, 0.002);
		add(57.8, 60.4, {1.00, 0.97, -2.22}, {1.01, 0.975, -2.24}, hoseIn3(), hoseIn3() + Vec3{0.0, 0.03, 0.0}, 30.0, 0.002);
		add(60.4, kPlan, {0.98, 1.32, -2.00}, {1.00, 1.30, -2.02}, hoseIn1(), hoseOut1(), 36.0);
		add(kPlan, 71.0, {0.72, 2.52, -1.70}, {0.70, 2.50, -1.72}, {0.70, 0.0, -1.82}, {0.70, 0.0, -1.82}, 68.0, 0.0);
		add(71.0, 74.0, {0.78, 1.10, -1.80}, {0.80, 1.08, -1.82}, {0.95, 0.98, -2.22}, {0.90, 0.92, -2.20}, 40.0);
		add(74.0, 80.0, {0.64, 0.86, -2.06}, {0.66, 0.86, -2.06}, {0.70, 0.83, -2.14}, {0.66, 0.83, -2.14}, 46.0);
		add(80.0, 84.0, {0.76, 0.85, -2.07}, {0.77, 0.85, -2.07}, {0.66, 0.84, -2.14}, {0.66, 0.84, -2.14}, 34.0, 0.002);
		add(84.0, 91.0, {0.79, 0.88, -2.045}, {0.79, 0.88, -2.045}, {0.78, 0.83, -2.14}, {0.75, 0.83, -2.14}, 62.0);
		add(91.0, kClock, {0.74, 0.83, -2.08}, {0.745, 0.83, -2.085}, {0.79, 0.82, -2.15}, {0.79, 0.815, -2.15}, 30.0, 0.002);
		add(kClock, 100.0, {-0.55, 1.95, -1.65}, {-0.55, 1.95, -1.75}, clockFace(), clockFace(), 26.0);
		// ---- scene 3: the route ----
		add(100.0, 110.0, {0.58, 0.95, -1.62}, {0.56, 0.95, -1.62}, {0.60, 0.85, -2.20}, {0.52, 0.86, -2.15}, 45.0);
		add(110.0, 113.2, {0.53, 0.99, -1.86}, {0.53, 0.99, -1.86}, {0.48, 0.95, -2.09}, {0.48, 0.97, -2.09}, 36.0);
		add(113.2, 117.0, {0.62, 1.06, -1.56}, {0.60, 1.05, -1.56}, {0.50, 0.88, -2.14}, {0.55, 0.86, -2.14}, 40.0);
		add(117.0, kPlanB - 0.2, {0.77, 1.012, -2.10}, {0.76, 1.012, -2.10}, {0.62, 0.98, -2.13}, {0.62, 1.0, -2.14}, 36.0, 0.002);
		add(kPlanB - 0.2, kPlanB + 1.6, {0.42, 2.10, -1.85}, {0.42, 2.06, -1.86}, {0.42, 0.0, -2.0}, {0.42, 0.0, -2.0}, 40.0, 0.0);
		add(kPlanB + 1.6, 133.0, {0.28, 0.90, -2.19}, {0.29, 0.90, -2.20}, {0.42, 0.86, -2.10}, {0.45, 0.88, -2.14}, 46.0);
		add(133.0, 137.0, {0.80, 1.06, -2.06}, {0.79, 1.06, -2.06}, {0.55, 1.0, -2.25}, {0.56, 1.0, -2.25}, 40.0);
		add(137.0, 147.0, {0.64, 1.09, -1.86}, {0.62, 1.10, -1.86}, {0.48, 1.02, -2.12}, {0.42, 1.07, -2.10}, 40.0);
		add(147.0, kLampOff, {0.33, 1.27, -1.99}, {0.33, 1.27, -2.0}, {0.28, 1.20, -2.10}, {0.28, 1.21, -2.10}, 32.0, 0.002);
		add(kLampOff, 154.0, {0.56, 1.16, -1.62}, {0.56, 1.16, -1.62}, {0.45, 1.04, -2.10}, {0.45, 1.04, -2.10}, 44.0);
		// ---- scene 4: the hose ----
		add(154.0, 160.0, {0.54, 1.05, -2.10}, {0.80, 1.07, -2.18}, hoseIn2(), hoseIn2(), 40.0, 0.003);
		add(160.0, kHang, {0.84, 1.13, -2.06}, {0.86, 1.13, -2.07}, {1.03, 1.08, -2.30}, {1.05, 1.08, -2.32}, 42.0);
		add(kHang, 171.0, {0.88, 1.05, -2.10}, {0.88, 1.06, -2.11}, {1.07, 1.04, -2.32}, {1.07, 1.04, -2.32}, 46.0);
		add(171.0, 176.0, {1.08, 1.105, -2.315}, {1.08, 1.106, -2.315}, {1.03, 0.80, -2.30}, {1.02, 0.79, -2.29}, 100.0, 0.0);
		add(176.0, 180.0, {0.10, 1.32, -1.05}, {0.10, 1.32, -1.05}, {1.02, 1.06, -2.30}, {1.02, 1.06, -2.30}, 22.0);
		add(180.0, kOverTop, {0.93, 1.21, -2.08}, {0.94, 1.26, -2.09}, {1.09, 1.10, -2.32}, {1.11, 1.20, -2.32}, 40.0);
		add(kOverTop, 197.0, {1.45, 1.36, -2.00}, {1.44, 1.36, -2.01}, {1.17, 1.24, -2.31}, {1.18, 1.27, -2.30}, 40.0);
		add(197.0, kHoseSlip - 0.5, {1.20, 1.31, -2.27}, {1.21, 1.31, -2.26}, {0.2, 0.55, 1.5}, {0.25, 0.5, 1.5}, 78.0, 0.0);
		add(kHoseSlip - 0.5, kLand + 0.6, {1.82, 0.85, -1.38}, {1.80, 0.75, -1.40}, {1.32, 0.95, -2.10}, {1.42, 0.40, -1.92}, 50.0);
		add(kLand + 0.6, 212.0, {1.72, 0.37, -1.66}, {1.70, 0.37, -1.68}, {1.50, 0.27, -1.86}, {1.50, 0.27, -1.86}, 34.0, 0.003);
		// ---- scene 5: the room (212 - 268 has tracking shots of its own: see viewAt) ----
		add(212.0, kVacuumOn, {1.70, 0.12, -1.20}, {0.75, 0.10, -0.30}, {1.30, 0.05, -1.68}, {0.30, 0.04, -0.62}, 45.0, 0.003);
		add(kVacuumOn, 223.0, {2.20, 0.055, -0.25}, {2.15, 0.055, -0.30}, {2.60, 0.06, 0.05}, {1.90, 0.06, -0.25}, 42.0, 0.003);
		add(223.0, kHide, {0.18, 0.045, -0.82}, {0.18, 0.045, -0.82}, {1.20, 0.06, -0.50}, {0.85, 0.06, -0.60}, 52.0, 0.003);
		add(kHide, 232.0, {0.16, 0.04, -0.98}, {0.14, 0.04, -0.98}, {-0.05, 0.04, -0.44}, {-0.05, 0.04, -0.44}, 58.0, 0.003);
		add(232.0, kRugEdge - 1.0, {-0.30, 0.05, -0.10}, {-0.42, 0.05, 0.25}, {-0.10, 0.03, -0.32}, {-0.55, 0.03, 0.57}, 54.0, 0.002);
		add(kRugEdge - 1.0, kRugTop + 0.5, {-0.46, 0.010, 0.50}, {-0.47, 0.012, 0.52}, {-0.55, 0.02, 0.66}, {-0.55, 0.025, 0.70}, 50.0, 0.0015);
		add(kRugTop + 0.5, 256.0, {-0.40, 0.03, 0.62}, {0.55, 0.03, 1.78}, {-0.50, 0.02, 0.75}, {0.42, 0.02, 1.92}, 52.0, 0.0015);
		add(256.0, 260.0, {0.62, 0.05, 1.82}, {0.62, 0.05, 1.82}, {0.50, 0.03, 2.02}, {0.52, 0.03, 2.04}, 40.0, 0.002);
		add(260.0, 268.0, {0.36, 0.06, 1.97}, {0.38, 0.06, 1.97}, {2.0, 0.15, 2.25}, {2.0, 0.15, 2.25}, 22.0, 0.004);
		// ---- scene 6: freedom ----
		add(268.0, 277.0, {2.25, 0.07, 1.72}, {2.25, 0.07, 1.72}, {0.70, 0.03, 2.08}, {1.30, 0.03, 2.15}, 16.0, 0.006);
		add(277.0, 281.0, {1.28, 0.16, 1.93}, {1.30, 0.17, 1.95}, {1.52, 0.10, 2.22}, {1.56, 0.12, 2.24}, 40.0, 0.003);
		add(281.0, 284.0, {1.68, 0.26, 2.24}, {1.69, 0.26, 2.24}, {2.05, 0.06, 2.25}, {2.06, 0.06, 2.25}, 82.0, 0.0);
		add(284.0, 287.0, {1.60, 0.16, 2.34}, {1.60, 0.16, 2.34}, {0.70, 1.0, -2.2}, {0.70, 1.0, -2.2}, 14.0, 0.0);
		add(287.0, 290.0, {1.78, 0.19, 2.06}, {1.77, 0.19, 2.07}, {1.62, 0.16, 2.24}, {1.62, 0.16, 2.24}, 30.0, 0.002);
		add(290.0, 292.5, {2.12, 0.32, 1.72}, {2.12, 0.30, 1.72}, {1.84, 0.12, 2.25}, {1.88, 0.10, 2.25}, 42.0);
		add(292.5, 300.0, {2.27, 0.055, 2.14}, {2.25, 0.055, 2.16}, {2.00, 0.05, 2.25}, {2.02, 0.05, 2.26}, 72.0, 0.0);
		// ---- scene 7: home ----
		add(300.0, 304.0, {1.55, 1.52, 0.10}, {1.60, 1.52, 0.15}, {2.90, 1.20, 1.15}, {2.40, 1.40, 1.00}, 46.0);
		add(304.0, 307.0, {1.62, 1.62, 0.42}, {1.63, 1.62, 0.44}, {2.35, 1.65, 1.00}, {2.35, 1.65, 1.00}, 30.0);
		add(307.0, 308.8, {0.94, 1.15, -2.04}, {0.95, 1.15, -2.05}, B + Vec3{0.0, 0.02, 0.0}, B + Vec3{0.0, 0.02, 0.0}, 30.0);
		add(308.8, 310.4, {0.62, 1.10, -1.98}, {0.62, 1.09, -1.99}, {0.48, 1.0, -2.16}, {0.48, 1.0, -2.16}, 30.0);
		add(310.4, 312.0, {0.55, 1.02, -1.80}, {0.56, 1.01, -1.81}, {0.50, 0.82, -2.18}, {0.50, 0.82, -2.18}, 34.0);
		add(312.0, 313.5, {1.75, 0.62, -1.55}, {1.74, 0.61, -1.56}, {1.45, 0.38, -1.90}, {1.45, 0.36, -1.90}, 36.0);
		add(313.5, 316.6, {0.20, 0.62, -0.55}, {0.20, 0.62, -0.58}, {1.0, 0.40, -1.45}, {1.0, 0.30, -1.45}, 40.0);
		add(316.6, 319.6, {0.75, 0.94, -2.26}, {0.75, 0.94, -2.26}, {0.75, 1.55, -1.85}, {0.75, 1.50, -1.85}, 56.0);
		add(319.6, 321.8, {1.92, 1.20, -1.40}, {1.90, 1.20, -1.42}, {1.45, 1.10, -1.95}, {1.45, 1.10, -1.95}, 40.0);
		add(321.8, 325.2, {-1.0, 0.11, -1.15}, {-0.98, 0.11, -1.15}, {-0.25, 0.55, -1.15}, {-0.25, 0.45, -1.15}, 60.0);
		add(325.2, 328.6, {-1.0, 0.26, 0.68}, {-1.0, 0.26, 0.70}, {-1.0, 1.40, 0.95}, {-1.0, 1.30, 0.95}, 56.0);
		add(328.6, kHeadUp, {-0.45, 1.62, 1.62}, {-0.47, 1.62, 1.60}, {-1.0, 1.62, 0.95}, {-1.0, 1.62, 0.95}, 32.0);
		add(kHeadUp, kBlack, {2.18, 0.14, 2.12}, {2.30, 0.42, 2.44}, {2.08, 0.11, 2.28}, {1.90, 0.12, 2.10}, 30.0, 0.003);
		add(kBlack, kDuration, {0.0, 1.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 1.0, -1.0}, {0.0, 1.0, -1.0}, 40.0, 0.0);
		return v;
	}();
	return s;
}

inline std::vector<double> shotStarts() {
	std::vector<double> v;
	for (const Shot &s : shots()) v.push_back(s.start);
	return v;
}

// The camera at t: the shot's own, a held camera's wander, and for the long tracking shots of the turtle on the floor, a camera that follows it.
inline CameraView viewAt(double t) {
	CameraView v = cameraAt(shots(), t);
	const TurtlePose tp = turtleAt(t);
	// following the turtle down at its level through the fibres and across the floor
	if (t >= kRugTop + 0.5 && t < 256.0) {
		v.target = tp.pos + Vec3{0.0, 0.03, 0.0};
		v.eye = tp.pos + Vec3{0.10, 0.025, -0.16};
	}
	if (t >= 212.0 && t < kVacuumOn) {
		v.target = tp.pos + Vec3{0.0, 0.03, 0.0};
		v.eye = tp.pos + Vec3{0.32, 0.07, 0.30};
	}
	// shots framed on someone who moves: the eye and the aim each an offset from the world, the turtle, its head, the owner's head or the vacuum (an offset
	// moves from its first value to its second over the shot)
	enum Anchor { World, Body, Head, Owner, Vacuum, HeadLocal };   // (HeadLocal: from the head, in the turtle's own frame: x to its right, y up, z ahead)
	struct Rig {
		double t0, t1;
		Anchor eyeAt;
		Vec3 e0, e1;
		Anchor aimAt;
		Vec3 a0, a1;
		double fov, lens;
	};
	static const Rig rigs[] = {
		{6.0, 11.0, World, {0.60, 1.11, -2.10}, {0.63, 1.10, -2.11}, World, {0.98, 1.02, -2.30}, {0.98, 1.01, -2.31}, 42.0, 0.002},    // the plant, from inside the tank
		{15.5, 20.5, World, {0.68, 1.16, -2.08}, {0.66, 1.15, -2.09}, World, {0.58, 0.99, -2.24}, {0.57, 0.99, -2.25}, 52.0, 0.002},   // pellets on the water
		{20.5, 26.0, World, {0.86, 1.17, -2.40}, {0.84, 1.17, -2.40}, Owner, {0.0, -0.05, 0.0}, {0.0, -0.03, 0.0}, 44.0, 0.002},     // "Perfect.", over the rim
		{29.5, 33.0, World, {1.25, 1.35, -1.98}, {1.23, 1.35, -1.97}, Owner, {0.0, -0.10, 0.0}, {0.0, -0.05, 0.0}, 44.0, 0.004},     // the keys: the face
		{74.0, 80.0, Body, {0.12, 0.03, 0.20}, {0.10, 0.03, 0.21}, Body, {-0.05, 0.01, 0.0}, {-0.06, 0.01, 0.0}, 46.0, 0.002},       // the push, through the glass
		{80.0, 84.0, Body, {-0.15, 0.03, 0.16}, {-0.14, 0.03, 0.17}, Body, {-0.02, 0.01, 0.0}, {-0.02, 0.01, 0.0}, 40.0, 0.002},
		{84.0, 91.0, World, {0.82, 0.88, -1.76}, {0.80, 0.88, -1.76}, World, {0.72, 0.83, -2.14}, {0.68, 0.83, -2.14}, 50.0, 0.002},
		{117.0, kPlanB - 0.2, Head, {0.06, 0.05, 0.20}, {0.05, 0.05, 0.20}, Head, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, 38.0, 0.002},        // the bubble
		{kPlanB + 1.6, 133.0, Body, {0.06, 0.03, 0.22}, {0.05, 0.03, 0.22}, Head, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, 44.0, 0.002},    // the bite and the pop
		{147.0, kLampOff, Head, {0.10, 0.10, 0.22}, {0.10, 0.10, 0.21}, Head, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, 38.0, 0.002},          // a foot on the rim
		{154.0, 160.0, Body, {-0.06, 0.08, 0.32}, {-0.03, 0.08, 0.30}, Body, {0.0, 0.02, 0.0}, {0.0, 0.02, 0.0}, 46.0, 0.002},
		{171.0, 176.0, World, {0.98, 0.83, -2.20}, {0.98, 0.83, -2.20}, Body, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, 74.0, 0.0},     // the canyon: from the bottom, up at it
		{197.0, kHoseSlip - 0.5, Body, {0.02, 0.09, -0.10}, {0.02, 0.09, -0.10}, World, {0.2, 0.55, 1.5}, {0.25, 0.5, 1.5}, 72.0, 0.0},   // freedom, over its shoulder
		{kHoseSlip - 0.5, kLand + 0.6, Body, {0.35, 0.06, 0.32}, {0.35, 0.10, 0.30}, Body, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, 46.0, 0.003},   // the slide
		{kVacuumOn, 223.0, World, {1.55, 0.05, -0.72}, {1.50, 0.05, -0.72}, Vacuum, {0.0, 0.06, 0.0}, {0.0, 0.06, 0.0}, 30.0, 0.003},
		{232.0, kRugEdge - 1.0, Body, {0.07, 0.04, -0.15}, {0.07, 0.04, -0.15}, Body, {-0.10, 0.02, 0.30}, {-0.10, 0.02, 0.30}, 56.0, 0.002},   // beneath the sofa
		{256.0, 260.0, Body, {0.13, 0.04, 0.15}, {0.12, 0.04, 0.15}, Head, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, 42.0, 0.002},
		{260.0, 268.0, Head, {-0.05, 0.05, -0.08}, {-0.05, 0.05, -0.08}, World, {2.0, 0.15, 2.25}, {2.0, 0.15, 2.25}, 24.0, 0.004},   // the water, far off
		{281.0, 284.0, HeadLocal, {0.0, 0.090, -0.10}, {0.0, 0.095, -0.09}, World, {2.05, 0.12, 2.25}, {2.05, 0.12, 2.25}, 60.0, 0.0},    // the pool, over its head
		{284.0, 287.0, Head, {0.01, 0.030, 0.20}, {0.01, 0.030, 0.20}, World, {0.70, 1.0, -2.2}, {0.70, 1.0, -2.2}, 26.0, 0.0},           // looking back, over its shoulder
		{287.0, 290.0, Head, {0.15, 0.06, -0.05}, {0.14, 0.055, -0.05}, Head, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, 30.0, 0.0015},         // a smile
		{292.5, 300.0, Body, {0.0, 0.22, -0.14}, {0.03, 0.20, -0.12}, Body, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, 50.0, 0.0},            // swimming, from above
		{312.0, 313.5, World, {1.95, 0.45, -1.75}, {1.94, 0.45, -1.76}, World, {1.40, 0.70, -2.15}, {1.40, 0.66, -2.15}, 40.0, 0.004},   // the hose
		{316.6, 319.6, World, {0.95, 0.90, -2.15}, {0.94, 0.90, -2.15}, Owner, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, 56.0, 0.002},       // inside the tank
		{319.6, 321.8, World, {2.25, 1.35, -2.30}, {2.24, 1.35, -2.29}, Owner, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, 40.0, 0.003},       // behind the tank
		{321.8, 325.2, Owner, {-1.5, 0.0, 0.05}, {-1.5, 0.0, 0.05}, Owner, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, 34.0, 0.003},           // under the table
		{325.2, 328.6, World, {-0.90, 0.95, -0.20}, {-0.92, 0.95, -0.18}, Owner, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, 40.0, 0.003},     // behind the sofa
		{328.6, kHeadUp, World, {0.60, 1.20, -0.60}, {0.58, 1.20, -0.58}, Owner, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, 40.0, 0.004},    // "Turtle?"
		{kHeadUp + 3.0, kBlack, World, {2.55, 0.28, 2.42}, {2.58, 0.30, 2.44}, World, {1.75, 0.25, 1.85}, {1.75, 0.22, 1.85}, 50.0, 0.003},   // the owner walks past
	};
	for (const Rig &r : rigs)
		if (t >= r.t0 && t < r.t1) {
			const double u = smoothstep(r.t0, r.t1, t);
			const auto at = [&](Anchor a) -> Vec3 {
				if (a == Body) return tp.pos;
				if (a == Head) return turtle::headOf(turtle::Look{}, tp);
				if (a == Owner) return human::headCentre(ownerAt(t));
				if (a == Vacuum) return setAt(t).vacuumPos;
				if (a == HeadLocal) return turtle::headOf(turtle::Look{}, tp);
				return {0.0, 0.0, 0.0};
			};
			const Vec3 e = lerp(r.e0, r.e1, u);
			v.eye = at(r.eyeAt) + (r.eyeAt == HeadLocal ? rightOf(tp.yaw) * e.x + Vec3{0.0, e.y, 0.0} + frontOf(tp.yaw) * e.z : e);
			v.target = at(r.aimAt) + lerp(r.a0, r.a1, u);
			v.fov = r.fov;
			v.lensRadius = r.lens;
		}
	// close-ups of the turtle's face, framed from its own head and the way it faces (it moves; a fixed camera would lose it)
	struct Face {
		double t0, t1, dist, side, up, fov;
	};
	static const Face faces[] = {{26.0, 29.5, 0.16, 0.25, 0.02, 30.0}, {37.5, 41.0, 0.15, -0.9, 0.01, 30.0}, {44.0, kSpy, 0.15, 0.6, 0.015, 30.0}, {kSpy, 50.0, 0.105, 0.0, 0.006, 24.0},
								 {91.0, kClock, 0.13, 0.5, 0.01, 30.0}, {133.0, 137.0, 0.17, 0.6, 0.03, 36.0},
								 {kLand + 0.6, 212.0, 0.18, 0.5, 0.05, 34.0}, {kHeadUp, kHeadUp + 3.0, 0.14, 0.2, 0.01, 32.0}};
	for (const Face &f : faces)
		if (t >= f.t0 && t < f.t1) {
			const Vec3 head = turtle::headOf(turtle::Look{}, tp);
			const double yaw = tp.yaw + 0.55 * tp.headYaw;
			const Vec3 dir = normalize(frontOf(yaw) * std::cos(f.side) + rightOf(yaw) * std::sin(f.side));
			v.target = head + Vec3{0.0, 0.004, 0.0};
			v.eye = head + dir * f.dist + Vec3{0.0, f.up, 0.0};
			v.fov = f.fov;
			v.lensRadius = 0.0015;
		}
	const bool macro = v.fov < 32.0 || (t > 236.0 && t < 260.0);
	const double hand = macro ? 0.0008 : (v.lensRadius == 0.0 ? 0.0 : 0.006);
	v.eye = v.eye + handheld(t, hand, 3);
	v.target = v.target + handheld(t, hand * 2.5, 4);
	return v;
}

// The grade of the moment: warm by day, blue by moonlight, cool and soft under water, cold blue for the plan, a touch of glow everywhere.
inline std::string lookAt(double t) {
	double warmth = 0.18, sat = 1.06, bloom = 0.12, vignette = 0.30;
	const Light l = lightAt(t);
	if (l == Light::Afternoon) warmth = 0.28;
	if (l == Light::Dusk) warmth = 0.35;
	if (l == Light::Night) warmth = -0.45, sat = 0.95, bloom = 0.16;
	if (l == Light::RoomLamp) warmth = 0.25;
	if (t >= kSpy && t < 71.0) vignette = 0.45, sat = 0.92;   // the thriller look
	const bool underwater = (t >= 74.0 && t < kClock) || (t >= 125.6 && t < 133.0) || (t >= 292.5 && t < 300.0);
	if (underwater) warmth -= 0.25, bloom = 0.22, sat = 1.12;
	if ((t >= kPlan && t < 71.0) || (t >= kPlanB - 0.2 && t < kPlanB + 1.6)) warmth = -0.3, sat = 1.0, bloom = 0.22, vignette = 0.5;
	if (t >= 171.0 && t < 176.0) vignette = 0.6, bloom = 0.2;   // the canyon
	char buf[160];
	std::snprintf(buf, sizeof(buf), "# @rt-look bloom %.3f vignette %.3f warmth %.3f saturation %.3f\n", bloom, vignette, warmth, sat);
	return buf;
}

// The camera's exposure (pbrt's Film "iso"; 100 is 1x): opened up as the day goes, like a camera operator would, so the dusk and the night read as dark blue, not black.
inline double exposureAt(double t) {
	const Light l = lightAt(t);
	if (l == Light::Afternoon) return 160.0;
	if (l == Light::Dusk) return 160.0 + 140.0 * smoothstep(140.0, kLampOff, t);
	if (l == Light::Night) return 420.0;
	if (l == Light::RoomLamp) return 420.0 - 250.0 * smoothstep(kLightOn, kLightOn + 1.0, t);
	return 130.0;
}

inline std::string headerAt(double t) {
	std::string h = sceneHeader(cameraText(viewAt(t)), kSamples, kWidth, kHeight);
	char iso[48];
	std::snprintf(iso, sizeof(iso), "Film \"rgb\" \"float iso\" [ %.1f ] ", exposureAt(t));
	const size_t at = h.find("Film \"rgb\" ");
	if (at != std::string::npos) h.replace(at, 11, iso);
	return lookAt(t) + h;
}

// The light of the moment: the sun or the moon through the window and the skylight, the sky beyond them, the night-light, the lamps.
inline std::string lightsAt(double t, const SetState &s) {
	std::string out;
	char buf[260];
	// (the sun comes in at the left: through the big window in the left wall and the one in the back wall; the moon from high up, through the skylight too)
	Vec3 sunFrom = {-5.0, 2.6, -2.2}, sun = {11.0, 9.8, 8.0}, sky = {1.0, 1.15, 1.40};
	if (s.light == Light::Afternoon) sunFrom = {-6.0, 2.0, 0.5}, sun = {10.0, 8.0, 5.6}, sky = {0.95, 0.95, 1.05};
	if (s.light == Light::Dusk) {
		const double u = smoothstep(140.0, kLampOff, t);
		sunFrom = {-6.0, 0.8, 1.0}, sun = lerp(Vec3{8.0, 4.6, 2.0}, Vec3{2.4, 1.1, 0.6}, u), sky = lerp(Vec3{0.70, 0.45, 0.45}, Vec3{0.22, 0.17, 0.30}, u);
	}
	// the moon: a cold beam slanting in through the big side window across the floor, and a blue night sky (a cartoon night: dark, but everything reads)
	if (s.light == Light::Night || s.light == Light::RoomLamp) sunFrom = {-5.0, 3.2, 0.9}, sun = {2.6, 3.4, 5.6}, sky = {0.14, 0.19, 0.34};
	std::snprintf(buf, sizeof(buf), "LightSource \"distant\" \"point3 from\" [ %.4f %.4f %.4f ] \"point3 to\" [ 0 0 0 ] \"rgb L\" [ %.4f %.4f %.4f ]\n", -sunFrom.x, sunFrom.y, sunFrom.z, sun.x, sun.y, sun.z);
	out += buf;
	std::snprintf(buf, sizeof(buf), "LightSource \"infinite\" \"rgb L\" [ %.5f %.5f %.5f ]\n", sky.x, sky.y, sky.z);
	out += buf;
	// the night-light by the bookshelf: a small warm glow low on the wall (always on: it is what the dark room is lit by besides the moon)
	out += emissiveSphere({kRoomX0 + 0.04, 0.32, -0.55}, 0.018, Vec3{90.0, 60.0, 27.0});
	out += boxAt({kRoomX0 + 0.015, 0.32, -0.55}, {0.03, 0.09, 0.06}, coated({0.95, 0.95, 0.92}, 0.3));
	// the floor lamp in the evening and when the owner is home
	if (s.light == Light::Dusk || s.light == Light::RoomLamp) out += emissiveSphere({kSofaX0 - 0.30, 1.34, kSofaZ1 - 0.05}, 0.035, Vec3{160.0, 120.0, 64.0});
	return out;
}

inline std::string sceneAt(double t) {
	const SetState s = setAt(t);
	const TurtlePose tp = turtleAt(t);
	std::string out = headerAt(t);
	// cut to black: a black card in front of the lens, and the night as it was
	if (t >= kBlack) {
		out += lightsAt(t, s);
		const CameraView v = viewAt(t);
		out += attr(diffuseMaterial({0.0, 0.0, 0.0}), emitMesh(box(4.0, 4.0, 0.01), mul(translate(v.eye + normalize(v.target - v.eye) * 0.1), alignY({0.0, 0.0, 0.0}, {0.0, 1.0, 0.0}))));
		return out;
	}
	std::string world;
	world += turtle_escape::objectDefinitions();
	world += lightsAt(t, s);
	world += buildFloor() + buildWalls(s) + buildWindow(s) + buildSideWindow(s) + buildDoor(s) + buildFan(s) + buildSofaAndTable() + buildShelfLampPictures() + buildClock(s) + buildVacuum(s) + buildTowelsTubBox(s) + buildTank(s);
	// the rug: its yarn tufts only where the camera is down among them
	const bool fibres = t > kRugEdge - 1.0 && t < 260.0;
	world += buildRug(fibres, tp.pos, 0.35);
	// the tub's water
	world += attr("Material \"thindielectric\" \"float eta\" [ 1.33 ]", emitMesh(waterSurface(kTubX0 + 0.006, kTubX1 - 0.006, kTubZ0 + 0.006, kTubZ1 - 0.006, kTubWater, t, s.tubRipples, 0.02), identity()));
	// the actors
	world += turtle::emitTurtle(tp);
	if (ownerOn(t)) world += human::emitHuman(ownerAt(t));
	if (s.bagsOnFloor) world += human::groceryBag({1.30, 0.0, -1.38}, -80.0, 0) + human::groceryBag({0.72, 0.0, -1.40}, -100.0, 1);
	world += buildBubbles(t, tp) + buildStars(t, tp);
	// at night, a small eye light just behind the camera when it is close on the turtle, as film crews use (it lifts the face out of the dark; the lens never sees it)
	if (s.light == Light::Night) {
		const CameraView v = viewAt(t);
		const double d = length(turtle::headOf(turtle::Look{}, tp) - v.eye);
		if (d < 0.6) {
			const Vec3 back = normalize(v.eye - v.target);
			const double r = 0.01, L = std::min(2000.0, 0.3 * (d + 0.06) * (d + 0.06) / (kPi * r * r));
			world += emissiveSphere(v.eye + back * 0.06 + Vec3{0.0, 0.05, 0.0}, r, Vec3{0.75, 0.85, 1.0} * L);
		}
	}
	if (s.blueprint) {
		out += blueprintify(world);
		out += "LightSource \"infinite\" \"rgb L\" [ 1.2 1.25 1.35 ]\n";
		out += buildBlueprintOverlay(t);
		return out;
	}
	return out + world;
}

}  // namespace turtle_escape
}  // namespace cartoon
