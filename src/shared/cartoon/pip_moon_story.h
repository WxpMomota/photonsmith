#pragma once
// pip_moon_story.h -- the timeline of "Pip and the Moon" (pip_and_the_moon.h), kept apart from the picture so that the film's sound (pip_moon_score.h) can use the same moments: when Pip's
// feet touch the dock, when it speaks, when the water is struck. Whatever you hear is therefore on the beat of what you see, and a change to the story moves both.
//
// The story, in seconds:
//    0 -  9   a night pond under a big moon; a small figure stands at the far end of a dock
//    4 - 16   Pip walks along the dock to its end, looking at the stars
//   16 - 26   it sees the moon in the water, is delighted, leans over the edge and touches it: the moon breaks into ripples
//   26 - 34   it is sad; then it has an idea (the bucket)
//   34 - 47   it runs for the bucket, runs back, dips it in the pond and lifts out the moon (a glowing ball)
//   47 - 57   it dances with the moon in its bucket
//   57 - 72   it carries the moon home, trips on a stone, the moon flies out of the bucket into the pond and goes out
//   72 - 82   Pip sits at the water's edge, sad; a firefly comes and leads its eyes up
//   82 - 96   it sees the real moon in the sky, understands, waves to it; a slow pull back over the quiet pond
//
// Story space as in the rest of the toolkit: +x to the right of the picture, +y up, +z towards the camera; the water is at y = 0.

#include "cartoon_lib.h"

namespace cartoon {
namespace pip_moon {

constexpr double kDuration = 96.0;
constexpr int kFps = 24;

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The place
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
constexpr double kDock = 0.34;                       // the dock's top above the water
constexpr double kDockX0 = -9.0, kDockX1 = 2.4;      // its two ends (the shore is beyond the first)
constexpr double kDockHalf = 0.85;                   // half its width

inline Vec3 moonCentre() { return {-38.0, 26.0, -52.0}; }
constexpr double kMoonRadius = 3.3;

// A camera at `distance` from the water point `water` on the line that makes the moon's reflection fall exactly on that point (the mirror image of the moon lies as far below the water as
// the moon is above it, and the reflection is where the line from the camera to that image crosses the water). Any camera on this line sees the reflection at `water`.
inline Vec3 reflectionCamera(Vec3 water, double distance) {
	const Vec3 m = moonCentre();
	const Vec3 image = {m.x, -m.y, m.z};
	return water + normalize(water - image) * distance;
}

constexpr Vec3 kStation = {2.15, kDock, 0.0};       // where Pip stands at the dock's end
constexpr Vec3 kTouch = {2.92, 0.0, 0.30};          // where the moon shows in the water beyond the dock's end, and where Pip touches it
constexpr Vec3 kBucketRest = {-2.0, kDock, -0.42};   // where the bucket stands
constexpr Vec3 kStone = {-4.35, kDock, 0.0};       // the stone Pip trips on
constexpr Vec3 kSitting = {-5.2, kDock, 0.45};       // where Pip sits after the fall

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// Moments
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
constexpr double kNotice = 17.2;      // Pip sees the moon in the water
constexpr double kTouched = 24.2;     // its hand touches the water: the moon breaks
constexpr double kIdea = 31.3;        // the bucket
constexpr double kPick = 39.5;        // the bucket leaves the dock in Pip's hand
constexpr double kDip = 44.3;         // the bucket goes in the pond
constexpr double kGlow = 45.2;        // the moon appears in it
constexpr double kTrip = 66.4;        // Pip's foot catches the stone
constexpr double kRelease = 66.55;    // the bucket leaves its hand
constexpr double kBurnOut = 69.0;     // the moon in the pond has gone out
constexpr double kFirefly = 78.6;     // the firefly comes
constexpr double kSeesMoon = 82.0;    // Pip understands

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// Where Pip walks on the dock
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
struct Leg {
	double t0, t1, x0, x1;
};
inline const std::vector<Leg> &legs() {
	static const std::vector<Leg> l = {
		{4.0, 15.0, -8.8, 2.15},      // a stroll to the end
		{36.0, 38.6, 2.15, -1.45},    // a run to the bucket
		{40.5, 43.0, -1.45, 2.15},    // and back with it
		{57.0, 66.35, 2.15, -4.3},    // carrying the moon home, slowly
	};
	return l;
}
inline double legU(const Leg &l, double t) {
	const double u = clamp01((t - l.t0) / (l.t1 - l.t0));
	return u * u * (3.0 - 2.0 * u);
}
// Pip's x on the dock from its walking alone (the fall and the sitting add to it).
inline double walkX(double t) {
	double x = legs().front().x0;
	for (const Leg &l : legs()) {
		if (t >= l.t0) x = l.x0 + (l.x1 - l.x0) * legU(l, t);
		else break;
	}
	return x;
}
// How fast it goes (metres per second), and how far it has gone altogether (for the feet).
inline double walkSpeed(double t) {
	for (const Leg &l : legs()) {
		if (t >= l.t0 && t <= l.t1) {
			const double u = clamp01((t - l.t0) / (l.t1 - l.t0));
			return std::abs(l.x1 - l.x0) / (l.t1 - l.t0) * 6.0 * u * (1.0 - u);
		}
	}
	return 0.0;
}
inline double walkDistance(double t) {
	double d = 0.0;
	for (const Leg &l : legs()) {
		if (t < l.t0) break;
		d += std::abs(l.x1 - l.x0) * legU(l, t);
	}
	return d;
}
constexpr double kStride = 0.68;   // metres per cycle of the feet (two steps)
inline double stepPhaseAt(double t) { return 2.0 * kPi * walkDistance(t) / kStride; }
inline double stepAmountAt(double t) { return std::min(1.3, walkSpeed(t) / 0.95); }

// The moments a foot lands on the dock (from the walk alone), for the sound.
inline std::vector<double> footfalls() {
	std::vector<double> out;
	double last = std::floor((stepPhaseAt(0.0) - 0.5 * kPi) / kPi);
	for (double t = 0.0; t < kDuration; t += 0.004) {
		const double k = std::floor((stepPhaseAt(t) - 0.5 * kPi) / kPi);
		if (k > last) {
			if (walkSpeed(t) > 0.2) out.push_back(t);
			last = k;
		}
	}
	return out;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// What Pip says: each line is a chirp (a glide from f0 to f1 Hz) and the mouth opens as long as it lasts
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
struct Line {
	double t, dur, f0, f1, open;
};
inline const std::vector<Line> &lines() {
	static const std::vector<Line> l = {
		{15.9, 0.35, 420, 620, 0.5},                                                           // "oh?"
		{17.5, 0.55, 480, 900, 0.9},                                                           // "ooh!" at the moon in the water
		{19.3, 0.16, 700, 820, 0.6}, {19.55, 0.16, 700, 840, 0.6}, {19.8, 0.16, 720, 860, 0.6},   // a giggle
		{20.2, 0.16, 760, 880, 0.6}, {20.45, 0.2, 780, 900, 0.7},
		{24.5, 0.3, 640, 520, 0.7}, {24.95, 0.55, 520, 340, 0.8},                              // "oh no"
		{28.0, 0.9, 400, 280, 0.35},                                                           // a sigh
		{31.4, 0.35, 500, 780, 0.8},                                                           // "ah!" (the idea)
		{35.9, 0.15, 600, 680, 0.6},                                                           // "hup!"
		{44.9, 0.6, 520, 720, 0.8},                                                            // "ooh" at the moon in the bucket
		{46.5, 0.16, 760, 880, 0.6}, {46.75, 0.16, 780, 900, 0.6}, {47.0, 0.2, 800, 940, 0.7},
		{48.6, 0.32, 440, 440, 0.5}, {49.4, 0.32, 524, 524, 0.5}, {50.2, 0.32, 588, 588, 0.5}, {51.0, 0.7, 524, 524, 0.55},   // humming the moon's tune
		{52.6, 0.32, 588, 588, 0.5}, {53.4, 0.32, 660, 660, 0.5}, {54.2, 0.8, 588, 588, 0.55},
		{66.3, 0.25, 500, 700, 0.8},                                                           // "oh!" as the foot catches
		{67.3, 0.3, 280, 180, 0.7},                                                            // "oof" on the dock
		{68.0, 0.5, 600, 880, 0.8},                                                            // "aah!" at the moon in the air
		{70.6, 0.7, 440, 300, 0.4}, {72.4, 0.8, 400, 280, 0.35},                               // sobs
		{79.6, 0.4, 420, 600, 0.55},                                                           // "oh?" at the firefly
		{82.2, 1.1, 500, 540, 0.6},                                                            // "ahh" at the moon
		{86.2, 0.16, 720, 840, 0.6}, {86.45, 0.16, 740, 860, 0.6},
		{88.0, 0.4, 600, 820, 0.75},                                                           // "hi!"
	};
	return l;
}
// How far Pip's mouth is open at t because of what it is saying.
inline double mouthOf(double t) {
	double m = 0.0;
	for (const Line &l : lines()) {
		const double u = (t - l.t) / l.dur;
		if (u < 0.0 || u > 1.0) continue;
		m = std::max(m, l.open * std::sin(kPi * u) * (0.75 + 0.25 * std::sin(2.0 * kPi * 5.0 * (t - l.t))));
	}
	return m;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The water: where it is struck
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
struct Ripple {
	double x, z, t0, amp;
};
// The moon's flight, and where it comes down (the fall of pip_and_the_moon.h's bucket throws it): constants here so that the splash can be listed with the others.
constexpr Vec3 kSplash = {-5.8, 0.0, 1.73};
constexpr double kSplashT = 67.33;
inline const std::vector<Ripple> &ripples() {
	static const std::vector<Ripple> r = {
		{kTouch.x, kTouch.z, kTouched, 1.0},                                    // a finger on the moon
		{kTouch.x, kTouch.z, 24.45, 0.45},
		{kTouch.x, kTouch.z, kDip, 0.8}, {kTouch.x + 0.2, kTouch.z + 0.1, 44.6, 0.35},   // the bucket goes in
		{kTouch.x, kTouch.z, 46.2, 0.25}, {kTouch.x - 0.1, kTouch.z - 0.1, 46.7, 0.2},   // drips from the lifted bucket
		{kSplash.x, kSplash.z, kSplashT, 2.2}, {kSplash.x + 0.1, kSplash.z, kSplashT + 0.3, 0.9},   // the moon falls in
		{3.6, 3.2, 88.6, 0.5},                                                  // a fish, far off
	};
	return r;
}

}  // namespace pip_moon
}  // namespace cartoon
