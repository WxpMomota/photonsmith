#pragma once
// film_voices.h -- instruments for a film's music, by MIDI note (60 is middle C) and length in seconds: a nylon-string guitar, a banjo-like pluck, a soft piano, a bowed
// string and a low drone (written for "The Last Nomai Signal", nomai_score.h), and for a score's big moments a brass section, a choir and timpani. Every film's cues
// file can play them (music_cues.h's generic instruments).

#include <algorithm>
#include <cmath>
#include <vector>

#include "cartoon_audio.h"
#include "cartoon_lib.h"

namespace cartoon {
namespace voices {

using audio::hz;

// A nylon-string guitar: a warm pluck over a soft body.
inline std::vector<float> guitar(double midi, double length, unsigned seed = 1) {
	std::vector<float> v = audio::pluck(hz(midi), length, seed, 0.9978);
	const std::vector<float> body = audio::additive(hz(midi), {{1.0, 0.7, 1.4}, {2.0, 0.25, 2.5}, {3.0, 0.08, 4.0}}, length, 0.004);
	for (std::size_t i = 0; i < v.size() && i < body.size(); ++i) v[i] = 0.7f * v[i] + 0.35f * body[i];
	return v;
}
// A banjo-like pluck: bright and quick, a little metallic.
inline std::vector<float> banjo(double midi, double length = 0.9, unsigned seed = 3) {
	std::vector<float> v = audio::pluck(hz(midi), length, seed, 0.9915);
	const std::vector<float> twang = audio::additive(hz(midi), {{2.0, 0.35, 6.0}, {3.01, 0.20, 9.0}, {5.02, 0.08, 14.0}}, length, 0.001);
	for (std::size_t i = 0; i < v.size() && i < twang.size(); ++i) v[i] = 0.8f * v[i] + 0.3f * twang[i];
	return v;
}
// A soft piano note: struck partials dying away, the hammer's touch at the start.
inline std::vector<float> piano(double midi, double length) {
	std::vector<float> v = audio::additive(hz(midi), {{1.0, 1.0, 0.9}, {2.0, 0.42, 1.6}, {3.0, 0.20, 2.4}, {4.01, 0.10, 3.5}, {5.02, 0.05, 5.0}}, length, 0.003);
	const std::vector<float> hammer = audio::noiseBurst(0.04, 0.001, 60.0, 2500.0, 900.0, static_cast<unsigned>(midi));
	for (std::size_t i = 0; i < v.size() && i < hammer.size(); ++i) v[i] += 0.15f * hammer[i];
	return v;
}
// A bowed string: a swell with a slow vibrato.
inline std::vector<float> bowed(double midi, double length, double attack = 1.2, double release = 1.6) {
	std::vector<float> v(audio::samplesFor(length + release), 0.0f);
	const double f = hz(midi);
	double phase = 0.0, lp = 0.0;
	for (std::size_t i = 0; i < v.size(); ++i) {
		const double t = static_cast<double>(i) / audio::kRate;
		const double env = std::min(1.0, t / attack) * (t < length ? 1.0 : std::max(0.0, 1.0 - (t - length) / release));
		phase += audio::kTau * f * (1.0 + 0.0035 * std::sin(audio::kTau * 5.2 * t) * smoothstep(0.5, 1.5, t)) / audio::kRate;
		double s = 0.0;
		for (int k = 1; k <= 8; ++k) s += std::sin(k * phase) / k;
		lp += 0.18 * (s - lp);   // (a soft bow: the upper partials rolled off)
		v[i] = static_cast<float>(env * lp * 0.45);
	}
	return v;
}
// A low drone: two slightly detuned tones and their octave, breathing slowly.
inline std::vector<float> drone(double midi, double length, double attack = 3.0, double release = 4.0) {
	std::vector<float> v(audio::samplesFor(length + release), 0.0f);
	const double f = hz(midi);
	for (std::size_t i = 0; i < v.size(); ++i) {
		const double t = static_cast<double>(i) / audio::kRate;
		const double env = std::min(1.0, t / attack) * (t < length ? 1.0 : std::max(0.0, 1.0 - (t - length) / release));
		const double breathe = 0.8 + 0.2 * std::sin(audio::kTau * 0.07 * t);
		v[i] = static_cast<float>(env * breathe * (0.5 * std::sin(audio::kTau * f * t) + 0.4 * std::sin(audio::kTau * f * 1.004 * t + 1.0) + 0.18 * std::sin(audio::kTau * 2.0 * f * t)));
	}
	return v;
}
// Brass: a section of horns - two players a few cents apart, a brassy tone that brightens as it swells (its upper partials come in with the breath) and darkens as it fades.
inline std::vector<float> brass(double midi, double length, double attack = 0.18, double release = 0.5) {
	std::vector<float> v(audio::samplesFor(length + release), 0.0f);
	const double f = hz(midi);
	double phase[2] = {0.0, 1.3};
	int partials = 0;   // (the partials under 9 kHz, at most 18, and each one's 1/sqrt(k): worked out once, not every sample)
	double root[18];
	for (int k = 1; k <= 18 && k * f < 9000.0; ++k) root[partials++] = 1.0 / std::sqrt(static_cast<double>(k));
	for (std::size_t i = 0; i < v.size(); ++i) {
		const double t = static_cast<double>(i) / audio::kRate;
		const double env = smoothstep(0.0, attack, t) * (t < length ? 1.0 : std::max(0.0, 1.0 - (t - length) / release));
		const double bright = 1.5 + 7.0 * env * (0.85 + 0.15 * smoothstep(attack, attack + 0.6, t));   // (how many partials sound: more the harder it is blown)
		const double fall = std::exp(-1.0 / bright);   // (each partial this much softer than the one below it: exp(-(k - 1) / bright))
		double s = 0.0;
		for (int p = 0; p < 2; ++p) {
			phase[p] += audio::kTau * f * (p ? 1.0017 : 0.9983) * (1.0 + 0.003 * std::sin(audio::kTau * 4.8 * t + p) * smoothstep(0.4, 1.2, t)) / audio::kRate;
			const double c2 = 2.0 * std::cos(phase[p]);   // (sin(k x) by the recurrence sin((k+1)x) = 2 cos x sin(kx) - sin((k-1)x))
			double before = 0.0, now = std::sin(phase[p]), w = 1.0;
			for (int k = 0; k < partials; ++k) {
				s += now * w * root[k];
				const double next = c2 * now - before;
				before = now, now = next, w *= fall;
			}
		}
		v[i] = static_cast<float>(env * s * 0.16);
	}
	return v;
}
// A choir singing "aah": three voices a little apart on each note, with a slow vibrato, its partials shaped by the vowel's formants (700, 1220 and 2600 Hz).
inline std::vector<float> choir(double midi, double length, double attack = 0.7, double release = 1.4) {
	std::vector<float> v(audio::samplesFor(length + release), 0.0f);
	const double f = hz(midi);
	const auto formant = [](double hzAt) {
		const auto peak = [hzAt](double c, double w, double g) { const double d = (hzAt - c) / w; return g / (1.0 + d * d); };
		return peak(700.0, 130.0, 1.0) + peak(1220.0, 140.0, 0.5) + peak(2600.0, 220.0, 0.25) + 0.04;
	};
	double phase[3] = {0.0, 2.1, 4.2};
	const double detune[3] = {1.0, 1.0035, 0.9968};
	int partials[3] = {0, 0, 0};   // (each voice's partials under 5 kHz, at most 24, with the vowel's weight on each: worked out once from the note, not every sample - the
	double gain[3][24];            // vibrato moves them by a fraction of a percent)
	for (int p = 0; p < 3; ++p)
		for (int k = 1; k <= 24 && k * f * detune[p] < 5000.0; ++k) gain[p][partials[p]++] = formant(k * f * detune[p]) / k;
	for (std::size_t i = 0; i < v.size(); ++i) {
		const double t = static_cast<double>(i) / audio::kRate;
		const double env = smoothstep(0.0, attack, t) * (t < length ? 1.0 : std::max(0.0, 1.0 - (t - length) / release));
		double s = 0.0;
		for (int p = 0; p < 3; ++p) {
			const double fp = f * detune[p] * (1.0 + 0.004 * std::sin(audio::kTau * (4.6 + 0.4 * p) * t + p) * smoothstep(0.3, 1.2, t));
			phase[p] += audio::kTau * fp / audio::kRate;
			const double c2 = 2.0 * std::cos(phase[p]);   // (sin(k x) by recurrence, as brass())
			double before = 0.0, now = std::sin(phase[p]);
			for (int k = 0; k < partials[p]; ++k) {
				s += now * gain[p][k];
				const double next = c2 * now - before;
				before = now, now = next;
			}
		}
		v[i] = static_cast<float>(env * s * 0.22);
	}
	return v;
}
// Timpani: a tuned drum struck - its modes (1, 1.5, 1.99, 2.44 times the note) dying away, the pitch dropping a little just after the stroke, the mallet's thump.
inline std::vector<float> timpani(double midi, double length) {
	const double ring = std::clamp(length, 0.6, 4.0);
	std::vector<float> v(audio::samplesFor(ring + 0.3), 0.0f);
	const double f = hz(midi);
	const double ratio[4] = {1.0, 1.5, 1.99, 2.44}, amp[4] = {1.0, 0.45, 0.3, 0.18}, decay[4] = {1.0, 0.55, 0.4, 0.3};
	double phase[4] = {0.0, 0.0, 0.0, 0.0}, lp = 0.0;
	unsigned noise = static_cast<unsigned>(midi * 977.0) | 1u;
	for (std::size_t i = 0; i < v.size(); ++i) {
		const double t = static_cast<double>(i) / audio::kRate;
		const double glide = 1.0 + 0.035 * std::exp(-t / 0.06);
		double s = 0.0;
		for (int m = 0; m < 4; ++m) {
			phase[m] += audio::kTau * f * ratio[m] * glide / audio::kRate;
			s += amp[m] * std::sin(phase[m]) * std::exp(-t / (decay[m] * ring * 0.45));
		}
		noise = noise * 1664525u + 1013904223u;
		lp += 0.08 * ((static_cast<double>(noise >> 8) / 8388608.0 - 1.0) - lp);
		s += 1.4 * lp * std::exp(-t / 0.025);
		v[i] = static_cast<float>(s * smoothstep(0.0, 0.002, t) * 0.5 * (t < ring ? 1.0 : std::max(0.0, 1.0 - (t - ring) / 0.3)));
	}
	return v;
}
}  // namespace voices
}  // namespace cartoon
