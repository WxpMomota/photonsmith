#pragma once
// film_voices.h -- instruments for a film's music, by MIDI note (60 is middle C) and length in seconds: a nylon-string guitar, a banjo-like pluck, a soft piano, a bowed
// string and a low drone. Written for "The Last Nomai Signal" (nomai_score.h); every film's cues file can play them (music_cues.h's generic instruments).

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
}  // namespace voices
}  // namespace cartoon
