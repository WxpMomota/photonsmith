#pragma once
// pip_moon_score.h -- the music and sounds of "Pip and the Moon" (cartoon_audio.h), placed on the story's own timeline (pip_moon_story.h): every foot on the dock, every chirp of Pip's voice and
// every splash is at the moment the picture has it.
//
// The music is in C major and its relative A minor, at 76 beats a minute, a plain pentatonic tune that is easy to follow:
//   Pip's tune     E G A G E D C                  the stroll along the dock (mallet)
//   the moon tune  G C E D C G                    played in bells whenever the moon is in the story: slowly when it is found, in the dance Pip hums it, tenderly while it is carried, and
//                                                 in full when Pip sees the real one
//   the lament     A G E D, in A minor            for the sorrow (the reflection broken, the moon lost)
// Between them: a pad for the night, a bass on the beat, plucked arpeggios, and the sounds of the place (crickets, water, the dock).

#include "cartoon_audio.h"
#include "pip_moon_story.h"

namespace cartoon {
namespace pip_moon {
namespace score {

using audio::bell;
using audio::hz;
using audio::Mix;

constexpr double kBeat = 60.0 / 76.0;
constexpr double kBar = 4.0 * kBeat;

enum class Voice { Mallet, Bell, Pluck, Bass, Pad };

inline void note(Mix &m, Voice v, double t, double midi, double dur, double gain, double pan = 0.0, double send = 0.25) {
	const double f = hz(midi);
	switch (v) {
		case Voice::Mallet: m.put(t, audio::mallet(f, std::max(0.8, dur)), gain, pan, send); break;
		case Voice::Bell: m.put(t, audio::bell(f, std::max(1.5, dur)), gain, pan, send); break;
		case Voice::Pluck: m.put(t, audio::pluck(f, std::max(0.9, dur), static_cast<unsigned>(midi * 7 + t * 13)), gain, pan, send); break;
		case Voice::Bass: m.put(t, audio::bass(f, std::max(1.0, dur)), gain, pan, send * 0.5); break;
		case Voice::Pad: m.put(t, audio::pad(f, dur), gain, pan, send); break;
	}
}

inline void chord(Mix &m, double t, std::initializer_list<double> midis, double dur, double gain, double attack = 0.7) {
	double pan = -0.35;
	for (double n : midis) {
		m.put(t, audio::pad(hz(n), dur, attack, 1.4), gain, pan, 0.45);
		pan += 0.7 / std::max<std::size_t>(1, midis.size() - 1);
	}
}

struct N {
	double beat, midi, beats;
};

// A tune: notes at `t0 + beat * kBeat`.
inline void tune(Mix &m, Voice v, double t0, const std::vector<N> &notes, double gain, double pan = 0.0, double send = 0.3, int transpose = 0) {
	for (const N &n : notes) note(m, v, t0 + n.beat * kBeat, n.midi + transpose, n.beats * kBeat, gain, pan, send);
}

inline const std::vector<N> &pipsTune() {
	static const std::vector<N> t = {{0, 64, 1}, {1, 67, 1}, {2, 69, 1.5}, {3.5, 67, 0.5}, {4, 64, 1}, {5, 62, 1}, {6, 60, 2}};
	return t;
}
inline const std::vector<N> &moonTune() {
	static const std::vector<N> t = {{0, 67, 2}, {2, 72, 2}, {4, 76, 2}, {6, 74, 1}, {7, 72, 1}, {8, 67, 4}};
	return t;
}
inline const std::vector<N> &lament() {
	static const std::vector<N> t = {{0, 69, 2}, {2, 67, 2}, {4, 64, 3}, {7, 62, 1}, {8, 64, 4}};
	return t;
}

inline double jitter(int i, int salt, double amount) { return amount * (hash01(i, salt) - 0.5); }

// A bass note on each beat of a stretch, following a list of roots one per bar.
inline void bassLine(Mix &m, double t0, double t1, const std::vector<double> &roots, double gain, double fifthEvery = 2.0) {
	int bar = 0;
	for (double t = t0; t < t1; t += kBar, ++bar) {
		const double root = roots[static_cast<std::size_t>(bar) % roots.size()];
		for (int b = 0; b < 4; ++b)
			if (t + b * kBeat < t1) note(m, Voice::Bass, t + b * kBeat, (b % 2 == 0) ? root : root + 7.0, 1.2, gain * (b == 0 ? 1.0 : 0.7), 0.0, 0.15);
		(void)fifthEvery;
	}
}

// Plucked arpeggios over a list of chords (one per bar), each as the notes to play in turn.
inline void arpeggio(Mix &m, double t0, double t1, const std::vector<std::vector<double>> &chords, double step, double gain, double pan = 0.0) {
	int bar = 0;
	for (double t = t0; t < t1; t += kBar, ++bar) {
		const auto &c = chords[static_cast<std::size_t>(bar) % chords.size()];
		for (double s = 0.0; s < kBar && t + s < t1; s += step) note(m, Voice::Pluck, t + s, c[static_cast<std::size_t>(s / step) % c.size()], 1.2, gain, pan + jitter(static_cast<int>(t * 10 + s * 100), 5, 0.3), 0.3);
	}
}

// A cricket's chirp: a short run of pulses on a high tone.
inline std::vector<float> cricket(unsigned seed) {
	const double length = 0.42;
	std::vector<float> v(audio::samplesFor(length), 0.0f);
	const double f = 4200.0 + 200.0 * (seed % 5);
	for (std::size_t i = 0; i < v.size(); ++i) {
		const double t = static_cast<double>(i) / audio::kRate;
		const double pulse = std::max(0.0, std::sin(audio::kTau * 18.0 * t));
		v[i] = static_cast<float>(std::sin(audio::kTau * f * t) * pulse * pulse * std::min(1.0, t * 40.0) * std::min(1.0, (length - t) * 40.0));
	}
	return v;
}

inline void soundtrack(Mix &m) {
	// ---- the place: crickets and water all through, hushed while the story is silent ----
	for (int i = 0; i < 60; ++i) {
		const double t = 1.0 + 95.0 * hash01(i, 301);
		const double hush = (t > 66.5 && t < 69.5) ? 0.0 : 1.0;
		m.put(t, cricket(static_cast<unsigned>(i)), 0.035 * hush, jitter(i, 302, 1.8), 0.25);
	}
	for (double t = 0.0; t < 94.0; t += 3.1) m.put(t, audio::noiseBurst(4.5, 1.6, 0.35, 380.0, 520.0, static_cast<unsigned>(t * 10) + 1), 0.022, jitter(static_cast<int>(t), 303, 1.4), 0.3);

	// ---- Pip: voice, feet and landings ----
	for (const Line &l : lines()) m.put(l.t, audio::chirp(l.f0, l.f1, l.dur, 6.5, 0.012 + 0.015 * (l.dur > 0.5), 1.0), 0.20, 0.0, 0.15);
	int step = 0;
	for (double t : footfalls()) {
		const double f = 125.0 + 45.0 * hash01(step, 311);
		m.put(t, audio::knock(f, 0.2, static_cast<unsigned>(step)), 0.26 * std::min(1.0, 0.5 + walkSpeed(t)), (step % 2 ? 0.1 : -0.1) + jitter(step, 312, 0.1), 0.12);
		++step;
	}
	const double landings[] = {17.5, 19.6, 20.1, 20.6, 31.7, 44.0, 85.3, 86.3};
	for (double t : landings) m.put(t, audio::knock(140.0, 0.2, 40), 0.22, 0.0, 0.12);
	for (double t = 48.4; t < 56.2; t += kBeat) m.put(t, audio::knock(150.0, 0.18, static_cast<unsigned>(t)), 0.16, 0.0, 0.12);

	// ---- 0 - 9: a night under a big moon ----
	chord(m, 0.0, {48, 55, 62, 67}, 9.5, 0.055, 2.5);
	const double twinkles[] = {1.4, 2.9, 4.5, 5.9, 7.3, 8.4};
	const double twinkleNotes[] = {88, 91, 84, 93, 86, 91};
	for (int i = 0; i < 6; ++i) note(m, Voice::Bell, twinkles[i], twinkleNotes[i], 2.0, 0.075, jitter(i, 321, 1.6), 0.5);

	// ---- 4 - 16: the stroll along the dock ----
	const double walkStart = 5.0;
	for (int phrase = 0; phrase < 3; ++phrase) {
		const double t0 = walkStart + phrase * 8.0 * kBeat;
		if (t0 > 15.0) break;
		tune(m, Voice::Mallet, t0, pipsTune(), 0.17, 0.15);
	}
	bassLine(m, walkStart, 15.8, {36, 41, 43, 36}, 0.20);
	arpeggio(m, walkStart + kBar, 15.8, {{60, 64, 67, 64}, {57, 60, 64, 60}, {53, 57, 60, 57}, {55, 59, 62, 59}}, kBeat * 0.5, 0.06, -0.25);
	chord(m, 8.0, {48, 55, 64}, 8.0, 0.05, 1.5);

	// ---- 15.8 - 24: the moon in the water: wonder, a giggle, a reach ----
	note(m, Voice::Bell, 15.9, 72, 2.0, 0.07, 0.0, 0.5);
	m.put(kNotice, audio::slide(500.0, 2000.0, 0.5, 4.0), 0.07, 0.0, 0.4);
	for (int i = 0; i < 6; ++i) note(m, Voice::Bell, kNotice + 0.04 + 0.11 * i, 72 + std::vector<int>{0, 4, 7, 12, 16, 19}[static_cast<std::size_t>(i)], 2.5, 0.12, -0.5 + 0.2 * i, 0.55);
	chord(m, kNotice, {48, 55, 64, 72, 79}, 4.0, 0.075, 0.35);
	const double giggleNotes[] = {91, 88, 91, 93, 96};
	for (int i = 0; i < 5; ++i) note(m, Voice::Mallet, 19.3 + 0.25 * i, giggleNotes[i] - 12, 0.4, 0.15, 0.25, 0.3);
	arpeggio(m, 19.2, 21.6, {{72, 76, 79, 84}, {72, 77, 81, 84}}, kBeat * 0.5, 0.07, 0.3);
	chord(m, 21.5, {43, 50, 60, 65}, 3.2, 0.08, 1.6);
	for (double t = 21.6; t < kTouched - 0.2; t += 0.62) m.put(t, audio::knock(58.0, 0.3, 9), 0.17 + 0.1 * (t - 21.6) / 2.6, 0.0, 0.1);

	// ---- 24.2: the touch, and the moon breaks ----
	note(m, Voice::Bell, kTouched, 96, 2.0, 0.16, 0.2, 0.6);
	m.put(kTouched + 0.02, audio::slide(1100.0, 110.0, 1.4, 2.2), 0.20, 0.0, 0.5);
	m.put(kTouched, audio::noiseBurst(0.9, 0.004, 4.5, 4500.0, 700.0, 17), 0.22, 0.2, 0.4);
	for (int i = 0; i < 5; ++i) note(m, Voice::Bell, kTouched + 0.12 * i, 100 - 3 * i, 1.5, 0.06, 0.8 - 0.4 * i, 0.6);

	// ---- 24.5 - 31: sorrow ----
	chord(m, 24.8, {45, 52, 60, 64}, 6.6, 0.075, 1.2);
	tune(m, Voice::Mallet, 25.6, lament(), 0.17, 0.0, 0.5, 0);
	note(m, Voice::Bass, 25.0, 33, 3.0, 0.20);
	note(m, Voice::Bass, 28.2, 29, 3.0, 0.18);

	// ---- 31.2: the idea ----
	m.put(kIdea + 0.05, audio::boing(330.0, 0.5), 0.14, 0.0, 0.25);
	for (int i = 0; i < 4; ++i) note(m, Voice::Pluck, kIdea + 0.06 * i, 60 + std::vector<int>{0, 4, 7, 12}[static_cast<std::size_t>(i)], 1.0, 0.2, 0.0, 0.3);
	note(m, Voice::Bell, kIdea + 0.3, 84, 2.0, 0.12, 0.2, 0.5);
	// a light, quick tune for the errand: eighth notes at 120 a minute
	{
		const double eighth = 0.25;
		const double roots[] = {48, 48, 53, 55};
		for (double t = 32.2, i = 0; t < 43.4; t += eighth, i += 1.0) {
			if (t > 35.0 && t < 35.8) continue;   // a breath before the run
			const int k = static_cast<int>(i);
			const double root = roots[(k / 8) % 4];
			const double pattern[] = {0, 7, 12, 7, 4, 7, 12, 16};
			note(m, Voice::Pluck, t, root + 12 + pattern[k % 8], 0.8, 0.10, jitter(k, 331, 0.6), 0.25);
			if (k % 2 == 0) note(m, Voice::Bass, t, root - 12, 0.5, 0.16, 0.0, 0.1);
		}
	}
	m.put(kPick, audio::bell(1250.0, 0.7), 0.12, 0.0, 0.3);
	m.put(kPick + 0.02, audio::knock(300.0, 0.15, 5), 0.2, 0.0, 0.1);

	// ---- 43.4 - 47.6: the bucket goes in, the moon comes out ----
	chord(m, 43.4, {43, 50, 58, 65}, 2.0, 0.06, 0.8);
	m.put(kDip, audio::noiseBurst(0.9, 0.01, 4.0, 3500.0, 600.0, 23), 0.30, 0.0, 0.4);
	m.put(kDip + 0.1, audio::slide(300.0, 700.0, 0.25, 8.0), 0.15, 0.0, 0.3);
	m.put(46.2, audio::slide(900.0, 1400.0, 0.08, 40.0), 0.07, 0.0, 0.5);
	m.put(46.7, audio::slide(900.0, 1400.0, 0.08, 40.0), 0.06, 0.2, 0.5);
	for (int i = 0; i < 8; ++i) note(m, Voice::Bell, kGlow + 0.07 * i, 72 + std::vector<int>{0, 4, 7, 11, 12, 16, 19, 23}[static_cast<std::size_t>(i)], 3.0, 0.11, -0.7 + 0.2 * i, 0.6);
	chord(m, kGlow, {48, 55, 64, 71, 79}, 5.5, 0.09, 0.6);
	m.put(kGlow, audio::noiseBurst(1.4, 0.2, 2.0, 9000.0, 4000.0, 31), 0.05, 0.0, 0.6);
	tune(m, Voice::Bell, 46.2, moonTune(), 0.11, 0.1, 0.55, 0);

	// ---- 47.6 - 57: the dance; the bells play what Pip hums ----
	{
		const double chords[4][4] = {{45, 52, 60, 64}, {41, 48, 57, 60}, {48, 55, 60, 64}, {43, 50, 59, 62}};
		for (int b = 0; b < 3; ++b)
			for (int c = 0; c < 4; ++c) {
				const double t = 47.6 + (b * 4 + c) * kBar * 0.5;
				if (t > 56.4) continue;
				chord(m, t, {chords[c][0], chords[c][1], chords[c][2], chords[c][3]}, kBar * 0.5, 0.045, 0.1);
			}
		for (double t = 47.6, i = 0; t < 56.6; t += kBeat, i += 1.0) {
			const int k = static_cast<int>(i);
			note(m, Voice::Bass, t, std::vector<double>{33, 40, 29, 36, 36, 43, 31, 38}[static_cast<std::size_t>(k % 8)], 1.0, 0.20, 0.0, 0.1);
			note(m, Voice::Mallet, t + 0.5 * kBeat, 64 + (k % 2) * 3, 0.5, 0.07, 0.4 * (k % 2 ? 1 : -1), 0.25);
		}
		// the tune Pip hums, in bells
		const double t0[] = {48.6, 49.4, 50.2, 51.0, 52.6, 53.4, 54.2};
		const double midi[] = {69, 72, 74, 72, 74, 76, 74};
		for (int i = 0; i < 7; ++i) {
			note(m, Voice::Bell, t0[i], midi[i] + 12, 1.5, 0.10, 0.35, 0.5);
			note(m, Voice::Mallet, t0[i], midi[i], 1.0, 0.10, -0.25, 0.3);
		}
	}

	// ---- 57 - 66.3: carrying it home, softly, and a little afraid ----
	{
		const double roots[] = {41, 36, 33, 31};
		for (int b = 0; b < 3; ++b) {
			const double t = 57.0 + b * 2.0 * kBar;
			chord(m, t, {roots[b % 4] + 12, roots[b % 4] + 19, roots[b % 4] + 24, roots[b % 4] + 28}, 2.0 * kBar, 0.05, 1.0);
		}
		bassLine(m, 57.0, 66.2, {41, 36, 33, 31}, 0.13);
		tune(m, Voice::Bell, 57.8, moonTune(), 0.09, 0.2, 0.6, 0);
		tune(m, Voice::Bell, 57.8 + 12.0 * kBeat, moonTune(), 0.10, 0.2, 0.6, 0);
		for (int i = 0; i < 9; ++i) note(m, Voice::Bell, 58.3 + 0.95 * i, 91 + (i % 3) * 2, 1.8, 0.04, jitter(i, 341, 1.6), 0.6);
		double gap = 0.62;
		for (double t = 62.5; t < kTrip - 0.2; t += gap, gap = std::max(0.3, gap - 0.04)) m.put(t, audio::knock(62.0, 0.3, 11), 0.18, 0.0, 0.1);
		chord(m, 62.0, {43, 50, 53, 59}, kTrip - 62.0, 0.06, 3.0);
	}

	// ---- 66.4 - 72: the fall, the splash, the dark ----
	m.put(kTrip, audio::knock(95.0, 0.5, 3), 0.40, 0.0, 0.3);
	note(m, Voice::Bass, kTrip, 26, 2.0, 0.28, 0.0, 0.3);
	for (int i = 0; i < 4; ++i) note(m, Voice::Bell, kTrip + 0.03 * i, 70 + i, 1.2, 0.07, -0.5 + 0.35 * i, 0.5);
	// the bucket clatters along the dock
	const double clatter[] = {67.02, 67.2, 67.34, 67.45, 67.54, 67.62, 67.7};
	for (int i = 0; i < 7; ++i) m.put(clatter[i], audio::bell(700.0 + 90.0 * (i % 3), 0.5), 0.12 * (1.0 - 0.1 * i), -0.5, 0.35);
	m.put(67.2, audio::knock(70.0, 0.4, 4), 0.55, -0.2, 0.25);   // Pip on the planks
	m.put(kSplashT, audio::noiseBurst(1.7, 0.006, 2.6, 6000.0, 500.0, 41), 0.55, -0.5, 0.45);
	m.put(kSplashT, audio::slide(200.0, 55.0, 1.0, 3.0), 0.40, -0.5, 0.4);
	m.put(kSplashT + 0.04, audio::slide(1500.0, 180.0, 1.7, 1.6), 0.15, -0.5, 0.6);
	m.put(kSplashT + 0.35, audio::noiseBurst(0.8, 0.01, 4.0, 3000.0, 500.0, 43), 0.18, -0.5, 0.4);
	m.put(68.7, audio::slide(500.0, 90.0, 1.9, 1.6), 0.10, 0.0, 0.6);
	const double drips[] = {69.9, 70.9, 71.6};
	for (int i = 0; i < 3; ++i) note(m, Voice::Bell, drips[i], 91 - 3 * i, 2.0, 0.05, jitter(i, 351, 1.2), 0.7);

	// ---- 72 - 79: alone ----
	chord(m, 72.2, {45, 52, 57, 60}, 8.0, 0.06, 2.0);
	tune(m, Voice::Mallet, 73.0, lament(), 0.15, -0.1, 0.55, 0);
	note(m, Voice::Bass, 73.0, 33, 3.0, 0.14);
	note(m, Voice::Bass, 76.2, 29, 3.0, 0.12);

	// ---- 78.6 - 82: a firefly ----
	const double fly[] = {79.0, 79.8, 80.6, 81.3};
	const double flyNotes[] = {84, 86, 88, 91};
	for (int i = 0; i < 4; ++i) note(m, Voice::Bell, fly[i], flyNotes[i], 2.0, 0.09, 0.4 - 0.25 * i, 0.6);
	chord(m, 79.0, {48, 55, 59, 64}, 4.0, 0.05, 1.4);

	// ---- 82 - 96: the real moon ----
	chord(m, kSeesMoon, {36, 43, 52, 60, 64, 67}, 9.0, 0.10, 1.2);
	chord(m, kSeesMoon + 9.0, {41, 48, 57, 60, 65, 69}, 4.0, 0.09, 0.6);
	chord(m, kSeesMoon + 13.0, {36, 43, 52, 60, 64, 67, 72}, 5.0, 0.09, 1.0);
	{
		const std::vector<std::vector<double>> prog = {{60, 64, 67, 72, 76, 72, 67, 64}, {55, 59, 62, 67, 71, 67, 62, 59}, {57, 60, 64, 69, 72, 69, 64, 60}, {53, 57, 60, 65, 69, 65, 60, 57}};
		arpeggio(m, kSeesMoon + 0.4, 95.0, prog, kBeat * 0.5, 0.075, 0.0);
		bassLine(m, kSeesMoon + 1.6, 94.0, {36, 31, 33, 29}, 0.15);
	}
	tune(m, Voice::Bell, 83.6, moonTune(), 0.15, 0.1, 0.55, 0);
	tune(m, Voice::Mallet, 83.6, moonTune(), 0.12, -0.2, 0.4, -12);
	tune(m, Voice::Bell, 83.6 + 12.0 * kBeat, moonTune(), 0.15, 0.1, 0.55, 5);
	tune(m, Voice::Mallet, 83.6 + 12.0 * kBeat, moonTune(), 0.12, -0.2, 0.4, -7);
	m.put(88.6, audio::slide(500.0, 1400.0, 0.12, 14.0), 0.10, 0.6, 0.4);
	m.put(88.62, audio::noiseBurst(0.5, 0.004, 7.0, 2500.0, 600.0, 51), 0.12, 0.6, 0.4);
	for (int i = 0; i < 7; ++i) note(m, Voice::Bell, 89.0 + 0.09 * i, 96 + std::vector<int>{0, 4, 7, 12, 7, 4, 0}[static_cast<std::size_t>(i)] * 0 + i, 3.0, 0.06, -0.8 + 0.27 * i, 0.7);   // the moon's glint
	note(m, Voice::Bell, 93.0, 84, 3.0, 0.08, 0.0, 0.7);
	note(m, Voice::Bell, 94.2, 91, 3.0, 0.06, 0.2, 0.8);
}

}  // namespace score

inline void soundtrack(audio::Mix &m) { score::soundtrack(m); }

}  // namespace pip_moon
}  // namespace cartoon
