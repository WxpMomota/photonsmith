#pragma once
// nomai_score.h -- the music and sound of "The Last Nomai Signal" (nomai_signal.h), on the film's own clock (cartoon_audio.h). Original music, written for the film:
// minimal, acoustic, melancholic and in the end hopeful - a soft guitar, a banjo-like pluck, a few piano notes, quiet strings, pads and low drones - with silence used on
// purpose. The signal is one simple sound, a pulse every 3.6 seconds; it opens the film, carries the cockpit and the observatory, goes on alone when its star has gone, and
// closes the film as the explorer's own answer. Around it: wind, footsteps in dust, the resonant tones and stone sounds of Nomai machines, the elevator's grind, falling
// dust, the campfire.
//
// The music is in D: D Dorian (D E F G A B C) for the search and the loss, D major for the answer and the dawn. Nothing is taken from the game's soundtrack.

#include "cartoon_audio.h"
#include "nomai_signal.h"
#include "sfx.h"

namespace cartoon {
namespace nomai {
namespace score {

using audio::hz;
using audio::Mix;

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// Instruments
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
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
// The signal: a clean tone with a faint hiss of the radio round it.
inline std::vector<float> beep(double length = 0.32) {
	std::vector<float> v(audio::samplesFor(length + 0.05), 0.0f);
	audio::Noise n(77);
	for (std::size_t i = 0; i < v.size(); ++i) {
		const double t = static_cast<double>(i) / audio::kRate;
		const double env = std::min(1.0, t / 0.012) * std::max(0.0, std::min(1.0, (length - t) / 0.06));
		v[i] = static_cast<float>(env * (std::sin(audio::kTau * 988.0 * t) + 0.25 * std::sin(audio::kTau * 1976.0 * t) + 0.05 * n.next()));
	}
	return v;
}
// A Nomai machine's tone: a resonant, glassy bell with a long, slowly beating tail.
inline std::vector<float> resonance(double midi, double length = 5.0) {
	return audio::additive(hz(midi), {{1.0, 1.0, 0.45}, {1.003, 0.6, 0.5}, {2.41, 0.35, 0.9}, {3.97, 0.18, 1.4}, {5.85, 0.08, 2.2}}, length, 0.01);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// Sounds of the world
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
inline void wind(Mix &m, double t, double length, double strength, unsigned seed = 21) {
	for (double s = 0.0; s < length; s += 2.0) {
		const double g = strength * (0.7 + 0.3 * std::sin(s * 0.7 + seed));
		m.put(t + s, audio::noiseBurst(3.2, 1.2, 0.5, 220.0 + 400.0 * strength, 380.0 + 600.0 * strength, seed + static_cast<unsigned>(s)), 0.13 * g, 0.7 * std::sin(s * 0.3), 0.35);
	}
}
// What the film's motion does (nomai::events): the footsteps - in dust outside, on stone in the ruins - the touchdown and the hand on the stone, each where and when the
// picture has it.
inline void filmSounds(Mix &m) {
	std::vector<FilmEvent> outside, ruins;
	for (const FilmEvent &e : events()) (e.t >= 80.0 && e.t < 250.0 ? ruins : outside).push_back(e);
	sfx::Palette dust;
	dust.weight = 1.0;   // (the one impact outside: the shuttle setting down)
	sfx::Palette stone = dust;
	stone.ground = sfx::Surface::Stone;
	stone.footfall = 0.11;
	stone.send = 0.35;   // (the corridor and the hall ring)
	sfx::play(m, outside, dust);
	sfx::play(m, ruins, stone);
}
// A low electrical hum (old machinery waking), steady for `length`.
inline void hum(Mix &m, double t, double length, double gain, double midi = 33.0) { m.put(t, drone(midi, length, 1.5, 2.0), gain, 0.0, 0.4); }
// Stone moving on stone: a rough low rumble that swells and fades.
inline void grind(Mix &m, double t, double length, double gain, unsigned seed = 91) {
	for (double s = 0.0; s < length; s += 0.5) m.put(t + s, audio::noiseBurst(0.9, 0.2, 2.5, 160.0, 90.0, seed + static_cast<unsigned>(s * 10.0)), gain * (0.7 + 0.3 * std::sin(s * 3.1)), 0.3 * std::sin(s), 0.5);
}
// A shimmer of high glassy tones (holograms, the star map).
inline void shimmer(Mix &m, double t, double length, double gain, double base = 84.0) {
	static const int steps[] = {0, 2, 3, 5, 7, 9, 10, 12};
	for (double s = 0.0; s < length; s += 0.23) {
		const int k = static_cast<int>(std::fmod(s * 13.7, 8.0));
		m.put(t + s, audio::additive(hz(base + steps[k]), {{1.0, 1.0, 3.0}, {2.0, 0.2, 5.0}}, 1.2, 0.02), gain * (0.6 + 0.4 * std::sin(s * 2.3)), std::sin(s * 1.9), 0.8);
	}
}
// A campfire: soft roar and crackles.
inline void campfire(Mix &m, double t, double length, double gain) {
	for (double s = 0.0; s < length; s += 1.0) m.put(t + s, audio::noiseBurst(1.6, 0.4, 1.0, 900.0, 500.0, 500u + static_cast<unsigned>(s)), gain * 0.5, 0.1, 0.2);
	for (double s = 0.0; s < length; s += 0.17 + 0.25 * std::fabs(std::sin(s * 7.3))) m.put(t + s, audio::noiseBurst(0.03, 0.001, 120.0, 5000.0, 2500.0, 600u + static_cast<unsigned>(s * 50.0)), gain * (0.4 + 0.6 * std::fabs(std::sin(s * 3.7))), 0.2 * std::sin(s * 5.0), 0.15);
}
// The shuttle: its engine's low roar, rising and falling with `level`.
inline void engine(Mix &m, double t, double length, double gain) {
	for (double s = 0.0; s < length; s += 0.6) m.put(t + s, audio::noiseBurst(1.2, 0.3, 1.5, 260.0, 180.0, 700u + static_cast<unsigned>(s * 10.0)), gain, 0.0, 0.3);
	m.put(t, drone(28.0, length, 1.0, 1.5), gain * 0.8, 0.0, 0.2);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The theme (original): a slow line that rises, turns and falls back - asked as a question in Dorian, answered at the end in major
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
struct Note {
	double midi, beat, len;
};
inline const std::vector<Note> &themeQuestion() {   // D Dorian
	static const std::vector<Note> n = {{57, 0, 1}, {62, 1, 1}, {64, 2, 1}, {65, 3, 2}, {64, 5, 1}, {62, 6, 1}, {60, 7, 1}, {57, 8, 3}, {55, 11, 1}, {57, 12, 1}, {60, 13, 1}, {62, 14, 3}, {64, 17, 2}, {62, 19, 4}};
	return n;
}
inline const std::vector<Note> &themeAnswer() {   // D major
	static const std::vector<Note> n = {{57, 0, 1}, {62, 1, 1}, {64, 2, 1}, {66, 3, 2}, {69, 5, 1}, {66, 6, 1}, {64, 7, 1}, {62, 8, 3}, {59, 11, 1}, {62, 12, 1}, {64, 13, 1}, {66, 14, 2}, {64, 16, 1}, {62, 17, 5}};
	return n;
}
template <typename Voice>
inline void play(Mix &m, double t, double beat, const std::vector<Note> &notes, double transpose, double gain, double pan, Voice voice, double until = 1e9) {
	for (const Note &n : notes)
		if (t + n.beat * beat < until) m.put(t + n.beat * beat, voice(n.midi + transpose, n.len * beat), gain, pan, 0.45);
}
// Guitar arpeggios under a chord sequence: each chord's notes rolled upwards, one bar each.
inline void arpeggios(Mix &m, double t, double bar, const std::vector<std::vector<double>> &chords, double gain, double pan = -0.25) {
	for (std::size_t c = 0; c < chords.size(); ++c)
		for (std::size_t k = 0; k < chords[c].size(); ++k) {
			const double at = t + c * bar + k * bar / chords[c].size();
			m.put(at, guitar(chords[c][k], bar * 1.5, 11u + static_cast<unsigned>(c * 7 + k)), gain, pan + 0.05 * k, 0.4);
		}
}
inline void strings(Mix &m, double t, std::initializer_list<double> midis, double length, double gain, double attack = 2.0, double release = 2.5) {
	double pan = -0.5;
	const double step = midis.size() > 1 ? 1.0 / static_cast<double>(midis.size() - 1) : 0.0;
	for (double n : midis) {
		m.put(t, bowed(n, length, attack, release), gain, pan, 0.6);
		pan += step;
	}
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The soundtrack
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
inline void beeps(Mix &m, double from, double to, double gain, double pan = 0.0, double send = 0.5) {
	for (double t = std::ceil(from / kPulsePeriod) * kPulsePeriod; t < to; t += kPulsePeriod) m.put(t, beep(), gain, pan, send);
}
inline void soundtrack(Mix &m) {
	// ---- I. Arrival (0 - 50): darkness and the pulse; the giant (a low drone, a pad); the descent (the engine); the cockpit (the signal, close); the landing ----
	beeps(m, 0.5, 18.0, 0.22, 0.0, 0.7);
	m.put(8.5, drone(38, 32.0, 6.0, 6.0), 0.22, 0.0, 0.6);   // D2
	strings(m, 10.0, {50, 57, 62}, 12.0, 0.035, 5.0, 5.0);
	engine(m, 19.0, 6.0, 0.10);
	hum(m, 25.0, 11.0, 0.05, 45.0);
	beeps(m, 25.0, 36.0, 0.16, 0.2, 0.15);
	for (int k = 0; k < 4; ++k) m.put(31.5 + 0.7 * k, audio::knock(900.0 + 120.0 * k, 0.05, 80u + static_cast<unsigned>(k)), 0.10, 0.3, 0.1);   // (the keys)
	m.put(28.0, piano(74, 4.0), 0.10, 0.0, 0.6);
	engine(m, 36.0, 4.0, 0.16);
	filmSounds(m);   // (the footsteps, the touchdown, the hands on the stone: from the film's events)
	wind(m, 40.0, 25.0, 0.30);
	m.put(42.0, guitar(50, 6.0), 0.20, -0.2, 0.5);
	m.put(44.0, guitar(57, 6.0), 0.14, 0.1, 0.5);

	// ---- II. The signal (50 - 120): the theme on the guitar as the explorer walks; the ridge; the canyon (strings); the third eye (a high tone); the floating steps; the
	// corridor's lamps (a tone each); the writing (glass); the recording - cut off ----
	play(m, 51.0, 0.85, themeQuestion(), -12.0, 0.26, -0.15, [](double n, double l) { return guitar(n, l + 1.5, static_cast<unsigned>(n)); });
	arpeggios(m, 51.0, 3.4, {{38, 45, 50, 53}, {34, 41, 46, 50}, {36, 43, 48, 52}, {33, 40, 45, 48}, {38, 45, 50, 53}}, 0.10);
	m.put(60.0, banjo(74, 1.2), 0.08, 0.4, 0.6);
	m.put(61.7, banjo(76, 1.2), 0.07, 0.4, 0.6);
	m.put(63.4, banjo(77, 1.6), 0.07, 0.4, 0.6);
	strings(m, 66.0, {50, 57, 62, 65}, 10.0, 0.05, 4.0, 4.0);
	m.put(70.0, drone(26, 14.0, 4.0, 5.0), 0.18, 0.0, 0.5);
	m.put(77.2, resonance(86, 6.0), 0.07, 0.2, 0.8);   // the third eye opens
	wind(m, 80.0, 8.0, 0.25, 33);
	for (int k = 0; k < 6; ++k) m.put(80.5 + 1.3 * k, piano(62 + (k % 3 == 0 ? 0 : (k % 3 == 1 ? 5 : 7)), 3.0), 0.07, 0.1, 0.6);
	// the corridor: each lamp wakes with a soft tone, rising
	hum(m, 88.0, 32.0, 0.04, 40.0);
	for (int k = 0; k < kCorridorLamps; ++k) {
		const double s = corridorLampAt(k) - 0.20;
		const double tk = 88.0 + clamp01((s - 0.02) / 0.28) * 10.0;
		if (tk < 98.0 && tk >= 88.0) m.put(tk, resonance(62 + 2.0 * (k % 5), 3.0), 0.06, k % 2 == 0 ? -0.4 : 0.4, 0.7);
	}
	shimmer(m, 100.5, 4.8, 0.035, 79.0);
	m.put(100.6, resonance(69, 6.0), 0.07, -0.2, 0.8);
	strings(m, 101.0, {57, 62, 64}, 6.0, 0.03, 2.0, 3.0);
	shimmer(m, 107.5, 10.0, 0.03, 86.0);
	for (double t = 107.6; t < 117.5; t += 0.9) m.put(t, audio::noiseBurst(0.12, 0.005, 25.0, 4000.0, 2000.0, 90u + static_cast<unsigned>(t * 3.0)), 0.025, 0.3, 0.3);   // (static)
	play(m, 110.5, 0.9, themeQuestion(), 0.0, 0.07, 0.2, [](double n, double l) { return piano(n, l + 1.0); }, 117.5);

	// ---- III. The observatory (120 - 190): the elevator (stone grinding, a descending drone); the hall (deep and empty); the staff; the hall wakes (a slow build, a tone for
	// each ring); the star map (the theme on banjo and guitar, shimmering); the flashing star; it is gone (the music stops; the signal goes on) ----
	m.put(124.2, resonance(74, 5.0), 0.08, 0.0, 0.8);
	grind(m, 126.0, 10.0, 0.10);
	m.put(126.0, drone(43, 10.0, 2.0, 2.0), 0.12, 0.0, 0.5);
	m.put(132.0, drone(38, 14.0, 2.0, 4.0), 0.11, 0.0, 0.7);
	m.put(136.0, drone(26, 20.0, 4.0, 5.0), 0.12, 0.0, 0.8);
	for (int k = 0; k < 3; ++k) m.put(138.0 + 2.6 * k, piano(50 + 7 * k, 5.0), 0.06, 0.0, 0.9);
	hum(m, 147.0, 40.0, 0.06, 38.0);
	strings(m, 149.0, {38, 45}, 18.0, 0.05, 4.0, 3.0);
	for (int k = 0; k < kRings; ++k) {   // a tone for each ring that wakes
		const double tk = 155.0 + 10.0 * (k + 0.5) / (kRings * 1.1);
		static const int scale[] = {50, 53, 55, 57, 60, 62, 65, 67, 69, 72};
		m.put(tk, resonance(scale[k % 10] + 12.0 * (k / 10), 4.0), 0.035, std::sin(k * 1.7), 0.8);
	}
	strings(m, 156.0, {50, 57, 62, 65, 69}, 12.0, 0.045, 5.0, 4.0);
	grind(m, 155.0, 9.0, 0.05, 97);
	for (double t = 156.0; t < 166.0; t += 0.4) m.put(t, audio::noiseBurst(0.4, 0.1, 6.0, 6000.0, 4000.0, 99u + static_cast<unsigned>(t * 5.0)), 0.012, std::sin(t * 2.0), 0.6);   // (dust)
	shimmer(m, 166.0, 12.0, 0.03, 81.0);
	play(m, 166.5, 0.62, themeQuestion(), 12.0, 0.08, 0.35, [](double n, double l) { return banjo(n, l + 0.8, static_cast<unsigned>(n)); });
	arpeggios(m, 166.5, 2.5, {{38, 45, 50, 53}, {41, 48, 53, 57}, {36, 43, 48, 52}, {38, 45, 50, 53}}, 0.08);
	strings(m, 168.0, {50, 57, 62}, 16.0, 0.035, 3.0, 2.0);
	beeps(m, 178.0, 199.0, 0.18, 0.1, 0.6);
	m.put(185.5, resonance(57, 7.0), 0.05, 0.0, 0.9);   // (the star goes out: one low tone, then only the signal)

	// ---- IV. The message (190 - 250): near silence; the remote memories (a warm, distant piano, fragments of the theme in major); the transmission ends; silence; the face (the
	// guitar alone, slow); the answer (rising strings, a full but gentle chord, the pulse going out) ----
	hum(m, 190.0, 34.0, 0.03, 38.0);
	m.put(195.0, resonance(66, 6.0), 0.05, 0.0, 0.8);
	static const double starts[] = {198.0, 203.0, 208.0, 212.0, 216.0, 220.0};
	for (int k = 0; k < 6; ++k) {
		const double t = starts[k];
		const std::vector<Note> &a = themeAnswer();
		for (int i = 0; i < 3; ++i) {
			const Note &n = a[static_cast<std::size_t>((k * 2 + i) % a.size())];
			m.put(t + 0.6 + 1.1 * i, piano(n.midi + 12.0, 3.5), 0.05, -0.2 + 0.1 * k, 0.9);
		}
		m.put(t + 0.2, audio::noiseBurst(0.3, 0.02, 8.0, 3000.0, 1500.0, 110u + static_cast<unsigned>(k)), 0.02, 0.0, 0.5);   // (the image forming)
	}
	m.put(223.6, audio::noiseBurst(0.25, 0.002, 18.0, 5000.0, 800.0, 120u), 0.06, 0.0, 0.4);   // the transmission ends
	for (double t = 224.0; t < 231.0; t += 2.0) m.put(t, audio::noiseBurst(3.0, 1.0, 0.6, 180.0, 260.0, 121u + static_cast<unsigned>(t)), 0.035, 0.0, 0.8);   // (nearly silent: the hall's air)
	play(m, 231.0, 1.15, themeQuestion(), -12.0, 0.18, -0.1, [](double n, double l) { return guitar(n, l + 2.0, static_cast<unsigned>(n) + 50u); }, 244.0);
	strings(m, 236.0, {50, 57}, 9.0, 0.025, 4.0, 3.0);
	strings(m, 244.5, {50, 57, 62, 66, 69}, 6.0, 0.05, 2.5, 2.0);
	m.put(246.0, drone(38, 6.0, 0.5, 4.0), 0.2, 0.0, 0.7);
	m.put(246.0, resonance(62, 8.0), 0.10, 0.0, 0.9);
	m.put(246.05, resonance(66, 8.0), 0.08, -0.3, 0.9);
	m.put(246.1, resonance(69, 8.0), 0.08, 0.3, 0.9);
	m.put(246.0, beep(0.5), 0.20, 0.0, 0.9);   // the answer goes out

	// ---- V. Ending (250 - 300): dawn (wind, the theme answered in major on guitar and banjo, strings); the campfire; the long pull back (the music opens and thins, and the
	// new signal - the explorer's own - beeps out into space); the title in silence ----
	wind(m, 250.0, 16.0, 0.25, 55);
	arpeggios(m, 251.0, 3.0, {{38, 45, 50, 54}, {35, 42, 47, 50}, {43, 50, 55, 59}, {45, 52, 57, 61}, {38, 45, 50, 54}, {35, 42, 47, 50}, {43, 50, 55, 59}, {45, 52, 57, 61}, {38, 45, 50, 54}, {43, 50, 55, 59}, {38, 45, 50, 54}}, 0.11);
	play(m, 254.0, 0.9, themeAnswer(), 0.0, 0.20, 0.1, [](double n, double l) { return guitar(n, l + 1.8, static_cast<unsigned>(n) + 90u); });
	campfire(m, 257.0, 30.0, 0.10);
	play(m, 270.0, 0.9, themeAnswer(), 12.0, 0.08, 0.35, [](double n, double l) { return banjo(n, l + 1.0, static_cast<unsigned>(n) + 9u); });
	strings(m, 268.0, {50, 57, 62, 66}, 18.0, 0.045, 5.0, 6.0);
	m.put(268.0, drone(38, 20.0, 5.0, 6.0), 0.14, 0.0, 0.7);
	beeps(m, 280.0, 292.0, 0.18, 0.0, 0.8);
	m.put(296.0, beep(), 0.12, 0.0, 0.9);
}

}  // namespace score

inline void soundtrack(audio::Mix &m) { score::soundtrack(m); }

}  // namespace nomai
}  // namespace cartoon
