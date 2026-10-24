#pragma once
// turtle_escape_score.h -- the music and sounds of "The Great Turtle Escape" (cartoon_audio.h), on the film's own beats (turtle_escape.h). All of it is
// original: a cosy morning in C major; a spy thriller in E minor (a plucked bass ostinato, a muted riff, ticking hi-hat, stabs on every close-up) that drives
// the escape; a heroic swell on the hose; a tiptoe for the sneaking; an emotional theme when the turtle finds the water and a march when it dives in; and the
// noises of the room (the door, the clock, the fan, the vacuum, footsteps, BONK, CRASH, POP, splashes) where the picture has them. The owner's two lines are
// hummed: "Perfect." falls, "Turtle?" rises.

#include "cartoon_audio.h"
#include "turtle_escape.h"

namespace cartoon {
namespace turtle_escape {
namespace score {

using audio::hz;
using audio::Mix;

constexpr double kSpyBeat = 60.0 / 112.0;

using audio::unit;   // (cartoon_audio.h)

inline void chord(Mix &m, double t, std::initializer_list<double> midis, double length, double gain, double attack = 0.5, double release = 1.2) {
	double pan = -0.45;
	const double step = midis.size() > 1 ? 0.9 / static_cast<double>(midis.size() - 1) : 0.0;
	for (double n : midis) {
		m.put(t, audio::pad(hz(n), length, attack, release), gain, pan, 0.5);
		pan += step;
	}
}
// A hi-hat: a very short burst of bright noise.
inline std::vector<float> hat(unsigned seed) { return audio::noiseBurst(0.06, 0.001, 60.0, 9000.0, 7000.0, seed); }
// A snare-ish hit.
inline std::vector<float> snare(unsigned seed) { return audio::noiseBurst(0.18, 0.001, 18.0, 4000.0, 1800.0, seed); }

// The spy theme from t0 to t1: a plucked bass ostinato (E B E D, A E A G on the change), a muted riff every other bar, hats on the off-beats.
inline void spy(Mix &m, double t0, double t1, double gain, double tempo = 1.0) {
	const double beat = kSpyBeat / tempo;
	const double bassE[8] = {40, 47, 52, 50, 40, 47, 52, 50}, bassA[8] = {45, 52, 57, 55, 45, 52, 57, 55};
	const double riff[8] = {64, 67, 71, 70, 69, 67, 64, 62};
	int k = 0;
	for (double t = t0; t < t1 - 0.05; t += beat * 0.5, ++k) {
		const int bar = k / 8, step = k % 8;
		const double *bass = (bar % 4 == 2) ? bassA : bassE;
		m.put(t, audio::pluck(hz(bass[step]), 0.6, 100u + static_cast<unsigned>(k), 0.993), gain * 0.9, -0.15, 0.15);
		if (step % 2 == 1) m.put(t, hat(200u + static_cast<unsigned>(k)), gain * 0.18, 0.35, 0.1);
		if (bar % 2 == 1 && step % 2 == 0) m.put(t, audio::pluck(hz(riff[step]), 0.35, 300u + static_cast<unsigned>(k), 0.985), gain * 0.45, 0.4, 0.3);
		if (step == 4) m.put(t, snare(400u + static_cast<unsigned>(k)), gain * 0.12, 0.1, 0.2);
	}
}
// A stab: a short dark chord, for a cut to a close-up.
inline void stab(Mix &m, double t, double gain, int rootNote = 52) {
	const double root = rootNote;
	for (double n : {root, root + 3.0, root + 7.0, root + 11.0}) m.put(t, audio::pad(hz(n), 0.25, 0.01, 0.5), gain, 0.0, 0.5);
	m.put(t, audio::bass(hz(root - 12), 0.8), gain * 1.5, 0.0, 0.2);
}
// Footsteps for a walk: a soft knock each time a foot comes down (from the walk's distance).
template <typename Distance>
inline void steps(Mix &m, double t0, double t1, double stride, Distance distance, double gain, double pitch, double pan) {
	double last = std::floor(distance(t0) / stride);
	for (double t = t0; t < t1; t += 0.01) {
		const double k = std::floor(distance(t) / stride);
		if (k > last) {
			m.put(t, audio::knock(pitch * (0.95 + 0.1 * unit(static_cast<unsigned>(t * 100.0))), 0.12, static_cast<unsigned>(t * 37.0)), gain, pan, 0.15);
			last = k;
		}
	}
}

inline void soundtrack(Mix &m) {
	// ======== scene 1: a cosy morning ========
	chord(m, 0.0, {48, 55, 64, 67}, 9.0, 0.040, 2.0, 2.5);
	chord(m, 9.0, {53, 57, 64, 69}, 9.0, 0.040, 2.0, 2.5);
	chord(m, 18.0, {55, 59, 62, 67}, 9.0, 0.040, 2.0, 2.5);
	chord(m, 27.0, {48, 55, 64, 72}, 9.5, 0.040, 2.0, 2.5);
	const double morning[] = {72, 76, 79, 76, 74, 72, 74, 76, 72, 67, 69, 72};
	for (int k = 0; k < 12; ++k) m.put(1.5 + 2.8 * k, audio::bell(hz(morning[k]), 2.2), 0.05, -0.3 + 0.05 * k, 0.4);
	for (int k = 0; k < 9; ++k) m.put(0.8 + 4.0 * k, audio::mallet(hz(48.0 + (k % 2 ? 7.0 : 0.0)), 1.2), 0.05, 0.0, 0.2);
	m.put(6.3, audio::noiseBurst(0.5, 0.05, 6.0, 900.0, 500.0, 5u), 0.03, 0.2, 0.3);    // a splash of the hand in the water
	for (int k = 0; k < 3; ++k) m.put(12.2 + 0.4 * k, audio::knock(900.0, 0.05, 7u + static_cast<unsigned>(k)), 0.05, 0.1, 0.1);   // taps on the glass
	for (int k = 0; k < 5; ++k) m.put(kPellets + 0.12 * k + 0.35, audio::noiseBurst(0.08, 0.002, 30.0, 3000.0, 2000.0, 9u + static_cast<unsigned>(k)), 0.04, 0.0, 0.2);   // plinks
	// "Perfect.": a satisfied hum, two falling notes
	m.put(kPerfect, audio::chirp(225.0, 205.0, 0.22, 5.0, 0.01, 1.0), 0.16, 0.0, 0.3);
	m.put(kPerfect + 0.26, audio::chirp(200.0, 160.0, 0.35, 5.0, 0.01, 1.0), 0.16, 0.0, 0.3);
	for (int k = 0; k < 6; ++k) m.put(kKeys + 0.05 * k, audio::bell(hz(96.0 + 3 * (k % 3)), 0.25), 0.02, 0.3, 0.2);   // the keys jingle
	steps(m, 20.6, kDoorClick, 0.65, [](double t) { return ownerDistance(t); }, 0.10, 140.0, 0.3);
	m.put(35.4, audio::slide(300.0, 240.0, 0.6, 3.0), 0.03, 0.8, 0.3);   // the door's creak
	m.put(kDoorClick, audio::knock(320.0, 0.15, 21u), 0.22, 0.8, 0.4);   // CLICK
	m.put(kDoorClick + 0.03, audio::knock(1400.0, 0.04, 22u), 0.12, 0.8, 0.2);
	// the quiet room: the clock ticking and the fan's hum
	for (double t = kDoorClick + 0.6; t < kSpy; t += 1.0) m.put(t, audio::knock(2200.0, 0.03, static_cast<unsigned>(t * 10.0)), 0.035, -0.6, 0.2);
	m.put(kDoorClick, audio::noiseBurst(kSpy - kDoorClick, 1.0, 0.05, 200.0, 220.0, 31u), 0.025, 0.0, 0.1);
	// ======== scene 2: the mission ========
	stab(m, kSpy, 0.06, 52);
	spy(m, kSpy + 0.5, kPlan, 0.11);
	for (int k = 0; k < 5; ++k) stab(m, 50.0 + 2.6 * k, 0.04, 52 + (k % 2) * 3);
	// the plan: a low drone and a typewriter blip per letter
	chord(m, kPlan, {40, 47, 52, 55}, 8.0, 0.05, 0.3, 1.0);
	for (int i = 0; i < 17; ++i) m.put(kPlan + 5.0 + i / 9.0, audio::knock(2600.0, 0.03, 50u + static_cast<unsigned>(i)), 0.06, 0.0, 0.1);
	m.put(kPlan + 4.5, audio::chirp(1200.0, 1200.0, 0.12, 0.0, 0.0, 1.0), 0.05, 0.0, 0.2);   // the X: a blip
	spy(m, 71.0, kClock, 0.12, 1.15);
	m.put(kDive, audio::noiseBurst(0.9, 0.01, 4.0, 2500.0, 600.0, 61u), 0.18, 0.2, 0.4);   // the dive
	for (int k = 0; k < 3; ++k) m.put(kNudge - 1.0 + 1.2 * k, audio::slide(120.0, 110.0, 0.5, 4.0), 0.06, 0.0, 0.2);   // grinding on the gravel
	m.put(kNudge, audio::knock(110.0, 0.2, 62u), 0.10, 0.0, 0.3);
	m.put(kCharge - 0.6, audio::chirp(300.0, 900.0, 0.6, 0.0, 0.0, 1.0), 0.04, 0.0, 0.3);   // the wind-up
	m.put(kBonk, audio::boing(160.0, 0.8), 0.28, 0.0, 0.3);   // BONK
	m.put(kBonk, audio::knock(90.0, 0.3, 63u), 0.25, 0.0, 0.3);
	for (int k = 0; k < 3; ++k) m.put(kBonk + 0.3 + 0.4 * k, audio::bell(hz(88.0 + 2 * k), 0.6), 0.04, -0.3 + 0.3 * k, 0.4);   // the stars
	// cut to the clock: ticking, the hands spinning with a whoosh, three hours gone
	for (double t = kClock; t < kClock + 0.5; t += 0.25) m.put(t, audio::knock(2200.0, 0.03, static_cast<unsigned>(t * 10.0)), 0.06, -0.2, 0.2);
	m.put(kClock + 0.5, audio::noiseBurst(4.5, 1.5, 0.6, 600.0, 3000.0, 71u), 0.06, 0.0, 0.3);
	for (int k = 0; k < 20; ++k) m.put(kClock + 0.6 + 0.22 * k, audio::knock(2200.0 + 40.0 * k, 0.03, 80u + static_cast<unsigned>(k)), 0.03, -0.2, 0.2);
	// ======== scene 3: the route ========
	spy(m, 100.0, 110.0, 0.12, 1.5);   // the time-lapse: faster
	for (int k = 0; k < 4; ++k) m.put(100.4 + 2.4 * k + 1.8, audio::knock(130.0, 0.25, 90u + static_cast<unsigned>(k)), 0.10, 0.0, 0.3);   // each rock set down
	spy(m, 110.0, kSlip, 0.10, 1.0);
	m.put(kSlip, audio::slide(400.0, 250.0, 0.3, 6.0), 0.08, 0.0, 0.2);   // the slip...
	for (int k = 0; k < 6; ++k) m.put(kCrash + 0.12 * k, audio::knock(140.0 + 60.0 * (k % 3), 0.25, 100u + static_cast<unsigned>(k)), 0.18, -0.3 + 0.12 * k, 0.4);   // ...CRASH
	m.put(kCrash + 0.2, audio::noiseBurst(0.8, 0.01, 4.0, 2000.0, 500.0, 109u), 0.14, 0.0, 0.4);
	m.put(kBubble + 2.3, audio::chirp(900.0, 1600.0, 0.06, 0.0, 0.0, 1.0), 0.05, 0.0, 0.3);   // the bubble pops
	stab(m, kPlanB, 0.06, 52);   // PLAN B
	for (int i = 0; i < 6; ++i) m.put(kPlanB + i / 8.0, audio::knock(2600.0, 0.03, 120u + static_cast<unsigned>(i)), 0.06, 0.0, 0.1);
	spy(m, kPlanB + 1.6, kPop, 0.11, 1.0);
	m.put(kBite, audio::knock(600.0, 0.08, 130u), 0.08, 0.0, 0.2);
	for (int k = 0; k < 2; ++k) m.put(126.6 + 2.5 * k, audio::slide(180.0, 200.0, 1.5, 1.0), 0.05, 0.0, 0.2);   // the strain
	m.put(kPop, audio::chirp(500.0, 1500.0, 0.08, 0.0, 0.0, 1.0), 0.25, 0.0, 0.3);   // POP
	m.put(kPop, audio::knock(800.0, 0.06, 131u), 0.15, 0.0, 0.3);
	m.put(kPop + 0.4, audio::noiseBurst(0.7, 0.01, 5.0, 2500.0, 600.0, 132u), 0.15, 0.0, 0.4);
	m.put(kIdea, audio::bell(hz(84), 1.5), 0.08, 0.0, 0.5);   // the idea
	m.put(kIdea + 0.15, audio::bell(hz(88), 1.5), 0.08, 0.0, 0.5);
	spy(m, 137.0, kLampOff, 0.11, 1.0);
	chord(m, 144.0, {52, 59, 64, 67}, 7.0, 0.04, 2.0, 0.5);   // rising hope
	m.put(kTouch, audio::bell(hz(91), 1.0), 0.06, -0.4, 0.4);
	m.put(kLampOff, audio::knock(1200.0, 0.06, 140u), 0.22, 0.3, 0.3);   // CLICK
	m.put(kLampOff + 0.02, audio::knock(300.0, 0.10, 141u), 0.12, 0.3, 0.3);
	spy(m, kLampOff + 1.2, 160.0, 0.06, 0.8);   // the music goes on, low, in the dark
	m.put(kSlideBack, audio::slide(500.0, 150.0, 0.6, 3.0), 0.08, -0.2, 0.3);
	m.put(kSlideBack + 0.6, audio::noiseBurst(0.6, 0.01, 5.0, 2000.0, 500.0, 145u), 0.10, -0.2, 0.4);
	// ======== scene 4: the hose ========
	chord(m, 160.0, {40, 47, 52, 55}, 8.0, 0.045, 1.0, 1.0);
	m.put(kHang, audio::slide(700.0, 500.0, 0.4, 3.0), 0.08, 0.0, 0.3);   // feet slipping
	// the canyon: heroic, enormous: brass-like pads, timpani, wind
	chord(m, 170.5, {36, 43, 48, 52, 55, 60}, 6.0, 0.07, 0.4, 1.5);
	for (int k = 0; k < 6; ++k) m.put(171.0 + 0.85 * k, audio::bass(hz(36), 1.2), 0.20, 0.0, 0.5);
	m.put(171.0, audio::noiseBurst(5.0, 1.5, 0.3, 500.0, 900.0, 151u), 0.07, 0.0, 0.5);
	m.put(176.0, audio::bell(hz(96), 0.6), 0.04, 0.4, 0.3);   // eight inches: a tiny plink
	// the climb: the heroic theme, slow, a heave on every pull
	const double heroic[] = {60, 64, 67, 72, 71, 67, 69, 72};
	chord(m, 180.0, {48, 55, 60, 64}, 6.0, 0.05, 1.5, 1.0);
	chord(m, 186.0, {53, 57, 60, 65}, 6.0, 0.05, 1.5, 1.0);
	for (int k = 0; k < 8; ++k) {
		m.put(180.0 + 1.5 * k + 1.0, audio::bass(hz(36 + (k % 2) * 7), 1.0), 0.14, 0.0, 0.3);
		m.put(180.0 + 1.5 * k + 1.0, audio::bell(hz(heroic[k]), 1.6), 0.06, 0.0, 0.5);
	}
	chord(m, kOverTop, {48, 55, 60, 64, 67, 72}, 5.0, 0.07, 0.3, 1.5);   // over the top
	m.put(kOverTop + 2.0, audio::bass(hz(36), 2.0), 0.2, 0.0, 0.5);
	chord(m, 197.0, {53, 60, 64, 69, 72}, 6.0, 0.05, 1.2, 1.0);   // freedom
	for (int k = 0; k < 6; ++k) m.put(197.3 + 0.7 * k, audio::bell(hz(84.0 + 3 * (k % 3)), 2.0), 0.04, -0.4 + 0.15 * k, 0.6);
	m.put(kHoseSlip, audio::slide(1400.0, 300.0, 1.8, 1.0), 0.12, 0.3, 0.3);   // the slide whistle
	m.put(kHoseSlip + 0.3, audio::noiseBurst(1.6, 0.2, 1.0, 900.0, 2500.0, 161u), 0.05, 0.3, 0.3);
	m.put(kLand, audio::knock(110.0, 0.3, 162u), 0.18, 0.4, 0.2);   // whump on the towels (and the music stops)
	m.put(kEyesOpen + 0.2, audio::bell(hz(84), 1.5), 0.05, 0.0, 0.5);
	m.put(kEyesOpen + 0.5, audio::bell(hz(91), 1.5), 0.04, 0.0, 0.5);
	// ======== scene 5: the room ========
	// the tiptoe: staccato plucks on every step, stopping when the turtle stops
	{
		double last = std::floor(floorDistance(212.0) / 0.03);
		for (double t = 214.5; t < 268.0; t += 0.01) {
			const double k = std::floor(floorDistance(t) / 0.03);
			if (k > last) {
				const int i = static_cast<int>(k);
				const double notes[4] = {64, 67, 64, 71};
				m.put(t, audio::pluck(hz(notes[i % 4] - (t > 244.0 ? 0 : 12)), 0.25, 170u + static_cast<unsigned>(i), 0.98), 0.07, 0.0, 0.2);
				last = k;
			}
		}
	}
	// the vacuum: beeps, its motor, the brushes, its pass; stabs as it comes on like a tank
	m.put(kVacuumOn, audio::chirp(1500.0, 1500.0, 0.08, 0.0, 0.0, 1.0), 0.10, 0.6, 0.2);
	m.put(kVacuumOn + 0.15, audio::chirp(2000.0, 2000.0, 0.12, 0.0, 0.0, 1.0), 0.10, 0.6, 0.2);
	m.put(kVacuumOn + 0.4, audio::noiseBurst(14.0, 0.8, 0.12, 300.0, 350.0, 181u), 0.09, 0.3, 0.2);
	for (int k = 0; k < 4; ++k) stab(m, kVacuumOn + 1.5 + 1.6 * k, 0.05, 40 + k);
	m.put(kHide, audio::slide(600.0, 900.0, 0.5, 3.0), 0.05, 0.0, 0.2);
	stab(m, kHide + 1.2, 0.05, 45);
	// the cliff: strain, a slip, success
	m.put(kRugEdge + 0.5, audio::slide(180.0, 260.0, 2.5, 0.6), 0.04, 0.0, 0.2);
	m.put(239.8, audio::slide(500.0, 300.0, 0.4, 3.0), 0.05, 0.0, 0.2);
	m.put(kRugTop, audio::bell(hz(79), 1.0), 0.05, 0.0, 0.4);
	// the fan's searchlights: a low swell each time a shadow passes
	for (double t : {247.4, 251.5}) chord(m, t, {40, 46, 52}, 1.6, 0.05, 0.1, 0.5);
	m.put(244.0, audio::noiseBurst(14.0, 1.0, 0.1, 150.0, 170.0, 191u), 0.04, -0.2, 0.1);   // the fan's hum overhead
	// the far edge: exhausted; then water, somewhere; the emotional theme
	m.put(kPlip, audio::chirp(1100.0, 1800.0, 0.05, 0.0, 0.0, 1.0), 0.07, 0.7, 0.6);
	m.put(kPlip + 1.6, audio::chirp(1000.0, 1700.0, 0.05, 0.0, 0.0, 1.0), 0.05, 0.7, 0.6);
	chord(m, 262.0, {48, 55, 60, 64}, 8.0, 0.045, 3.0, 2.0);
	// ======== scene 6: freedom ========
	const double longing[] = {67, 72, 76, 74, 72, 69, 67, 72, 76, 79, 77, 76};
	chord(m, 270.0, {53, 60, 65, 69}, 8.0, 0.045, 2.5, 2.0);
	chord(m, 278.0, {55, 62, 67, 71}, 7.0, 0.045, 2.0, 2.0);
	for (int k = 0; k < 12; ++k) m.put(266.0 + 1.6 * k, audio::bell(hz(longing[k]), 2.4), 0.05, 0.0, 0.5);
	chord(m, 284.0, {57, 60, 64, 69}, 3.2, 0.04, 1.0, 1.0);   // looking back: wistful
	chord(m, 287.0, {48, 55, 64, 67, 72}, 4.0, 0.05, 1.0, 1.0);
	m.put(kSplash, audio::noiseBurst(1.2, 0.01, 3.0, 3000.0, 600.0, 201u), 0.24, 0.3, 0.5);   // SPLASH
	m.put(kSplash, audio::knock(100.0, 0.25, 202u), 0.12, 0.3, 0.3);
	// the march: triumphant, in C
	const double march[] = {72, 72, 76, 79, 77, 76, 74, 72, 79, 77, 76, 74, 72, 76, 72, 67};
	for (int k = 0; k < 16; ++k) {
		const double t = kSplash + 0.6 + 0.5 * k;
		m.put(t, audio::bell(hz(march[k]), 1.2), 0.07, 0.0, 0.4);
		if (k % 2 == 0) m.put(t, audio::bass(hz(k % 4 == 0 ? 36 : 43), 0.8), 0.15, 0.0, 0.2);
		if (k % 2 == 1) m.put(t, snare(210u + static_cast<unsigned>(k)), 0.06, 0.1, 0.2);
	}
	chord(m, kSplash + 0.6, {48, 55, 60, 64, 67}, 8.0, 0.05, 0.3, 1.0);
	// ======== scene 7: home ========
	m.put(kHome, audio::knock(300.0, 0.12, 221u), 0.12, 0.8, 0.3);
	m.put(kHome + 0.1, audio::slide(260.0, 330.0, 0.8, 2.0), 0.04, 0.8, 0.3);   // the creak
	steps(m, kHome + 0.4, 338.0, 0.65, [](double t) { return ownerDistance(t); }, 0.11, 140.0, 0.2);
	for (int k = 0; k < 4; ++k) m.put(kHome + 0.6 + 0.4 * k, audio::noiseBurst(0.25, 0.02, 10.0, 1500.0, 1200.0, 230u + static_cast<unsigned>(k)), 0.03, 0.5, 0.2);   // the paper bags
	m.put(kLightOn, audio::knock(1600.0, 0.04, 241u), 0.12, 0.6, 0.2);   // the switch
	chord(m, 304.5, {40, 46, 51}, 4.0, 0.05, 0.8, 1.0);   // something is wrong
	for (int k = 0; k < 4; ++k) stab(m, 307.0 + 1.6 * k, 0.045, 40 + 3 * k);   // the details, one cut each
	m.put(313.5, audio::noiseBurst(1.5, 0.3, 2.0, 1500.0, 900.0, 251u), 0.05, 0.0, 0.2);
	spy(m, 316.6, kCall - 0.5, 0.09, 1.4);   // searching, faster and faster
	// "Turtle?": a rising call
	m.put(kCall, audio::chirp(240.0, 210.0, 0.22, 6.0, 0.015, 1.0), 0.20, -0.2, 0.4);
	m.put(kCall + 0.26, audio::chirp(215.0, 330.0, 0.40, 6.0, 0.015, 1.0), 0.20, -0.2, 0.4);
	// the tub: a music box lullaby; the bubble; the button
	const double lullaby[] = {79, 76, 72, 76, 79, 84, 81, 79, 76, 77, 74, 72};
	for (int k = 0; k < 12; ++k) m.put(kHeadUp - 0.5 + 0.6 * k, audio::bell(hz(lullaby[k]), 1.6), 0.055, 0.2, 0.5);
	chord(m, kHeadUp - 0.5, {60, 64, 67, 72}, 7.0, 0.035, 1.5, 1.5);
	m.put(kLastBubble + 1.2, audio::chirp(1200.0, 1900.0, 0.05, 0.0, 0.0, 1.0), 0.08, 0.2, 0.5);   // plip
	m.put(kBlack, audio::pluck(hz(36), 1.2, 260u, 0.995), 0.18, 0.0, 0.3);
	m.put(kBlack, audio::bell(hz(84), 2.0), 0.06, 0.0, 0.6);
}

}  // namespace score

inline void soundtrack(audio::Mix &m) { score::soundtrack(m); }

}  // namespace turtle_escape
}  // namespace cartoon
