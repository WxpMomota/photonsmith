#pragma once
// forest_dawn_score.h -- the sound of "Forest Dawn" (cartoon_audio.h), placed on the film's own clock: the night's crickets and a far owl, a pad that warms from D minor to D major as the
// sun clears the hills, the first birds and then more of them as it gets light, a glockenspiel drop for each glint of dew in the macro shot, wind in the leaves and a falling plucked line
// while the leaves come down, and the full chord as the camera rises. No tune to follow: the picture is the story, the sound is the morning.

#include "cartoon_audio.h"

namespace cartoon {
namespace forest_dawn {
namespace score {

using audio::hz;
using audio::Mix;

// A fixed number in [0, 1) from a seed (the film sounds the same every time).
inline double unit(unsigned seed) {
	audio::Noise n(seed);
	n.next();
	return 0.5 * (n.next() + 1.0);
}

// A pad chord: one held tone per note, spread across the stereo field.
inline void padChord(Mix &m, double t, std::initializer_list<double> midis, double length, double gain, double attack, double release) {
	double pan = -0.5;
	const double step = midis.size() > 1 ? 1.0 / static_cast<double>(midis.size() - 1) : 0.0;
	for (double n : midis) {
		m.put(t, audio::pad(hz(n), length, attack, release), gain, pan, 0.55);
		pan += step;
	}
}

// Crickets: trains of three short high pulses, a few voices at different places and rates, thinning out towards sunrise.
inline void crickets(Mix &m, double until) {
	for (int v = 0; v < 4; ++v) {
		const double f = 4300.0 + 260.0 * v, period = 0.55 + 0.13 * v, pan = -0.7 + 0.45 * v;
		for (double t = 0.15 * v; t < until; t += period) {
			const double fade = 1.0 - t / until;
			if (unit(static_cast<unsigned>(t * 1000.0) + 31u * static_cast<unsigned>(v)) > 0.25 + 0.75 * fade) continue;
			for (int k = 0; k < 3; ++k) m.put(t + 0.045 * k, audio::chirp(f, f * 0.995, 0.028, 0.0, 0.0), 0.05 * fade, pan, 0.25);
		}
	}
}

// A songbird's phrase: a few quick glides between 2.3 and 5 kHz, its own shape from `seed`.
inline void bird(Mix &m, double t, unsigned seed, double gain, double pan) {
	const int notes = 3 + static_cast<int>(4.0 * unit(seed));
	double at = t;
	for (int k = 0; k < notes; ++k) {
		const double f0 = 2300.0 + 2600.0 * unit(seed * 7u + static_cast<unsigned>(k));
		const double f1 = f0 * (0.75 + 0.6 * unit(seed * 13u + static_cast<unsigned>(k)));
		const double len = 0.05 + 0.10 * unit(seed * 17u + static_cast<unsigned>(k));
		m.put(at, audio::chirp(f0, f1, len, 18.0, 0.03, 1.0), gain, pan, 0.35);
		at += len + 0.03 + 0.06 * unit(seed * 19u + static_cast<unsigned>(k));
	}
}

inline void soundtrack(Mix &m) {
	const double end = m.seconds();
	// The night: crickets, and an owl far off twice.
	crickets(m, 11.0);
	for (double t : {2.2, 6.4}) {
		m.put(t, audio::chirp(415.0, 380.0, 0.38, 4.0, 0.01, 1.0), 0.10, -0.6, 0.8);
		m.put(t + 0.55, audio::chirp(410.0, 360.0, 0.55, 4.0, 0.01, 1.0), 0.09, -0.6, 0.8);
	}
	// The pad: D minor (D F A) in the blue, opening to D major with an added E as the sun clears the hills, then the full chord for the crane.
	padChord(m, 0.0, {50, 57, 62, 65}, 8.5, 0.050, 2.5, 3.0);
	padChord(m, 8.0, {50, 57, 62, 66, 69}, 10.0, 0.050, 3.0, 3.0);
	padChord(m, 17.5, {50, 57, 64, 66, 71}, 9.0, 0.040, 2.0, 3.0);
	padChord(m, 26.0, {47, 54, 62, 66, 69}, 8.5, 0.040, 2.0, 3.0);
	padChord(m, 33.5, {50, 57, 62, 66, 69, 74}, end - 33.5 - 1.0, 0.055, 3.0, 3.0);
	m.put(33.5, audio::bass(hz(38), 6.0), 0.12, 0.0, 0.3);
	// The birds: the first one as the light comes, then more and more.
	for (int i = 0; i < 46; ++i) {
		const double u = (i + 0.5) / 46.0;
		const double t = 9.0 + (end - 11.0) * std::sqrt(u) + 0.6 * unit(400u + static_cast<unsigned>(i));
		bird(m, t, 1000u + static_cast<unsigned>(i) * 37u, 0.045 + 0.03 * unit(500u + static_cast<unsigned>(i)), -0.9 + 1.8 * unit(600u + static_cast<unsigned>(i)));
	}
	// The dew: a glockenspiel drop now and then in D major pentatonic, high and soft.
	const double dew[] = {86, 81, 83, 78, 86, 88, 81, 85, 83, 78};
	for (int i = 0; i < 10; ++i) m.put(18.6 + 0.78 * i + 0.2 * unit(700u + static_cast<unsigned>(i)), audio::bell(hz(dew[i]), 2.4), 0.06, -0.4 + 0.08 * i, 0.5);
	// The leaves: wind rising and falling, and a plucked line that drifts down like a leaf.
	m.put(25.5, audio::noiseBurst(9.0, 2.5, 0.25, 350.0, 1100.0, 11u), 0.07, 0.3, 0.4);
	const double fall[] = {81, 78, 76, 74, 71, 69, 66, 64, 62};
	for (int i = 0; i < 9; ++i) m.put(26.4 + 0.85 * i, audio::pluck(hz(fall[i]), 1.6, 40u + static_cast<unsigned>(i)), 0.09, 0.5 - 0.11 * i, 0.4);
	// The crane: a last rising pair of bells over the chord.
	m.put(36.0, audio::bell(hz(74), 3.0), 0.07, -0.2, 0.6);
	m.put(37.2, audio::bell(hz(81), 3.5), 0.07, 0.2, 0.6);
}

}  // namespace score

inline void soundtrack(audio::Mix &m) { score::soundtrack(m); }

}  // namespace forest_dawn
}  // namespace cartoon
