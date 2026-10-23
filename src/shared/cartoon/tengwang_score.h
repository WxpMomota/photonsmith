#pragma once
// tengwang_score.h -- the music and sounds of "Preface to the Pavilion of Prince Teng" (cartoon_audio.h), on the film's own clock (tengwang.h). It begins with the old instruments
// -- the guqin's low plucked strings sliding between notes, the breathy xiao, the bright pipa and its tremolo, the guzheng's sweeps, temple bells and drums -- and opens out into
// strings, horns and a choir, the guqin always underneath. It is not continuous: wind, water, hooves, cups and voices carry whole scenes, the world falls silent round Wang Bo
// in his darkest moment, and comes back -- wind, river, drums, orchestra -- for "though poor, firmer still".
//
// The music is in D (gong) pentatonic -- D E F# A B -- turning to its B (yu) mode for the melancholy, and to a full D major for the end.

#include "cartoon_audio.h"
#include "tengwang.h"

namespace cartoon {
namespace tengwang {
namespace score {

using audio::hz;
using audio::Mix;

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// Instruments
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The guqin: a long low plucked string, warm and woody, its pitch sliding by `slide` semitones over the note (the player's hand moving along the string).
inline std::vector<float> guqin(double midi, double length, double slide = 0.0, unsigned seed = 1) {
	std::vector<float> v = audio::pluck(hz(midi), length, seed, 0.9985);
	if (slide != 0.0) {
		// a slide: resample the note, faster or slower over its length
		std::vector<float> out(v.size(), 0.0f);
		double pos = 0.0;
		for (std::size_t i = 0; i < out.size() && pos < v.size() - 1; ++i) {
			const double u = static_cast<double>(i) / out.size();
			const std::size_t p0 = static_cast<std::size_t>(pos);
			const double fr = pos - p0;
			out[i] = static_cast<float>(v[p0] * (1.0 - fr) + v[p0 + 1] * fr);
			pos += std::pow(2.0, slide * smoothstep(0.15, 0.6, u) / 12.0);
		}
		v = out;
	}
	// its body: a soft low partial under the string
	const std::vector<float> body = audio::additive(hz(midi), {{1.0, 0.6, 1.1}, {2.0, 0.2, 2.0}}, length, 0.006);
	for (std::size_t i = 0; i < v.size() && i < body.size(); ++i) v[i] = 0.75f * v[i] + 0.35f * body[i];
	return v;
}
// A harmonic of the guqin (a light touch at a node): a bell-clear high tone.
inline std::vector<float> guqinHarmonic(double midi, double length) { return audio::additive(hz(midi), {{1.0, 1.0, 1.4}, {2.0, 0.15, 3.0}, {3.0, 0.05, 5.0}}, length, 0.003); }
// The xiao: a soft end-blown flute, breathy, with a slow vibrato.
inline std::vector<float> xiao(double midi, double length, unsigned seed = 3) {
	std::vector<float> v(audio::samplesFor(length + 0.4), 0.0f);
	const double f = hz(midi);
	double phase = 0.0;
	audio::Noise n(seed);
	double breath = 0.0;
	for (std::size_t i = 0; i < v.size(); ++i) {
		const double t = static_cast<double>(i) / audio::kRate;
		const double env = (std::min)(1.0, t / 0.25) * (t < length ? 1.0 : (std::max)(0.0, 1.0 - (t - length) / 0.4));
		const double vib = 1.0 + 0.006 * std::sin(audio::kTau * 5.0 * t) * smoothstep(0.3, 0.9, t);
		phase += audio::kTau * f * vib / audio::kRate;
		breath = 0.97 * breath + 0.03 * n.next();
		v[i] = static_cast<float>(env * (std::sin(phase) + 0.18 * std::sin(2.0 * phase) + 0.06 * std::sin(3.0 * phase) + 0.9 * breath));
	}
	return v;
}
// The pipa: a bright, quick pluck; and its tremolo (lunzhi), the note repeated fast by all five fingers.
inline std::vector<float> pipa(double midi, double length = 0.9, unsigned seed = 5) { return audio::pluck(hz(midi), length, seed, 0.993); }
inline void pipaTremolo(Mix &m, double t, double midi, double length, double gain, double pan) {
	for (double s = 0.0; s < length; s += 0.065) m.put(t + s, pipa(midi, 0.4, 5u + static_cast<unsigned>(s * 100.0)), gain * (0.7 + 0.3 * std::sin(s * 20.0)), pan, 0.25);
}
// The guzheng's sweep: a fast run of plucks up (or down) the pentatonic scale.
inline void zhengSweep(Mix &m, double t, double from, int notes, bool up, double gain, double pan) {
	static const int steps[] = {0, 2, 4, 7, 9};
	for (int k = 0; k < notes; ++k) {
		const int i = up ? k : notes - 1 - k;
		const double midi = from + 12.0 * (i / 5) + steps[i % 5];
		m.put(t + 0.045 * k, audio::pluck(hz(midi), 1.6, 9u + static_cast<unsigned>(k), 0.996), gain, pan + 0.04 * (k - notes / 2.0), 0.4);
	}
}
// The temple bell: a great bronze bell struck once, its hum lasting long.
inline std::vector<float> templeBell(double midi, double length = 9.0) { return audio::additive(hz(midi), {{0.5, 0.7, 0.18}, {1.0, 1.0, 0.25}, {1.19, 0.5, 0.35}, {2.0, 0.35, 0.5}, {2.76, 0.25, 0.8}, {5.4, 0.12, 1.6}}, length, 0.002); }
// A drum: the big Tang drum (a deep thump with the skin's knock) or a small one.
inline void drum(Mix &m, double t, double gain, double pan = 0.0, bool big = true) {
	m.put(t, audio::bass(hz(big ? 28 : 40), big ? 1.6 : 0.6), gain, pan, 0.3);
	m.put(t, audio::knock(big ? 75.0 : 160.0, big ? 0.5 : 0.18, 9u), gain * 0.8, pan, 0.25);
	m.put(t, audio::noiseBurst(big ? 0.4 : 0.15, 0.002, big ? 10.0 : 25.0, 600.0, 150.0, 5u), gain * 0.3, pan, 0.3);
}
// A cello-like voice: a bowed tone that swells and holds, with vibrato (the "low cello" of the score).
inline std::vector<float> cello(double midi, double length) {
	std::vector<float> v(audio::samplesFor(length + 0.8), 0.0f);
	const double f = hz(midi);
	double phase = 0.0;
	for (std::size_t i = 0; i < v.size(); ++i) {
		const double t = static_cast<double>(i) / audio::kRate;
		const double env = (std::min)(1.0, t / 0.6) * (t < length ? 1.0 : (std::max)(0.0, 1.0 - (t - length) / 0.8));
		phase += audio::kTau * f * (1.0 + 0.004 * std::sin(audio::kTau * 5.5 * t) * smoothstep(0.4, 1.2, t)) / audio::kRate;
		double s = 0.0;
		for (int k = 1; k <= 7; ++k) s += std::sin(k * phase) / (k * 0.9);
		v[i] = static_cast<float>(env * s * 0.4);
	}
	return v;
}
inline std::vector<float> horn(double midi, double length) { return audio::additive(hz(midi), {{1.0, 1.0, 0.25}, {2.0, 0.55, 0.35}, {3.0, 0.32, 0.5}, {4.0, 0.18, 0.7}, {5.0, 0.10, 0.9}}, length, 0.15); }
// A chord of strings (or a choir: softer, slower): one pad per note, spread across the field.
inline void strings(Mix &m, double t, std::initializer_list<double> midis, double length, double gain, double attack = 1.5, double release = 2.0) {
	double pan = -0.6;
	const double step = midis.size() > 1 ? 1.2 / static_cast<double>(midis.size() - 1) : 0.0;
	for (double n : midis) {
		m.put(t, audio::pad(hz(n), length, attack, release), gain, pan, 0.6);
		pan += step;
	}
}
// A long stretch of wind (`strength` 0 a breath .. 1 a gale), its brightness rising with it.
inline void wind(Mix &m, double t, double length, double strength, unsigned seed = 21) {
	for (double s = 0.0; s < length; s += 2.0) {
		const double g = strength * (0.7 + 0.3 * std::sin(s * 0.9 + seed));
		m.put(t + s, audio::noiseBurst(3.0, 1.0, 0.6, 300.0 + 600.0 * strength, 500.0 + 900.0 * strength, seed + static_cast<unsigned>(s)), 0.15 * g, std::sin(s * 0.4), 0.3);
	}
}
// Water lapping on stone, a quiet river.
inline void water(Mix &m, double t, double length, double gain) {
	for (double s = 0.0; s < length; s += 0.7) m.put(t + s + 0.2 * std::sin(s * 7.0), audio::noiseBurst(0.6, 0.08, 4.0, 1800.0, 600.0, 31u + static_cast<unsigned>(s * 10.0)), gain, 0.5 * std::sin(s), 0.4);
}
// Hooves on paving: a pair of knocks per step, and the bells on the harness.
inline void hooves(Mix &m, double t, double length, double gain, double rate = 1.6) {
	for (double s = 0.0; s < length; s += 1.0 / rate) {
		m.put(t + s, audio::knock(380.0, 0.08, 41u), gain, -0.2, 0.2);
		m.put(t + s + 0.11, audio::knock(330.0, 0.08, 42u), gain * 0.8, 0.2, 0.2);
		if (std::fmod(s, 1.5) < 0.6) m.put(t + s + 0.05, audio::bell(hz(88 + (static_cast<int>(s) % 2) * 3), 0.6), gain * 0.25, 0.3, 0.3);
	}
}
// A crowd: the murmur of many voices and, here and there, laughter (bursts of short rising tones).
inline void crowd(Mix &m, double t, double length, double gain, bool laughter) {
	for (double s = 0.0; s < length; s += 0.9) m.put(t + s, audio::noiseBurst(1.4, 0.3, 1.2, 700.0, 500.0, 51u + static_cast<unsigned>(s * 3.0)), gain, 0.6 * std::sin(s * 1.3), 0.5);
	if (laughter)
		for (double s = 1.0; s < length; s += 2.7) {
			for (int k = 0; k < 4; ++k) m.put(t + s + 0.13 * k, audio::chirp(hz(57 + (k % 2) * 3), hz(59 + (k % 2) * 3), 0.1, 6.0, 0.05, 0.5), gain * 0.5, 0.5 * std::cos(s), 0.5);
		}
}
// A goose's call: two honks, a little falling.
inline void goose(Mix &m, double t, double gain, double pan) {
	for (int k = 0; k < 2; ++k) m.put(t + 0.32 * k, audio::chirp(hz(70), hz(67), 0.22, 7.0, 0.04, 1.0), gain, pan, 0.6);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The themes
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
struct Note {
	double midi, beat, len;
};
// The land's theme on the guqin (D pentatonic): A D E F# E D B A.
inline const std::vector<Note> &landTheme() {
	static const std::vector<Note> n = {{57, 0, 2}, {62, 2, 1}, {64, 3, 1}, {66, 4, 2}, {64, 6, 1}, {62, 7, 1}, {59, 8, 2}, {57, 10, 3}};
	return n;
}
// The hero's theme (D major, the strings and horns of the end): D F# A D' B A F# A G F# E D.
inline const std::vector<Note> &heroTheme() {
	static const std::vector<Note> n = {{62, 0, 1}, {66, 1, 1}, {69, 2, 1}, {74, 3, 2}, {71, 5, 1}, {69, 6, 1}, {66, 7, 1}, {69, 8, 1}, {67, 9, 1}, {66, 10, 1}, {64, 11, 1}, {62, 12, 3}};
	return n;
}
template <typename Voice>
inline void play(Mix &m, double t, double beat, const std::vector<Note> &notes, double transpose, double gain, double pan, Voice voice) {
	for (const Note &n : notes) m.put(t + n.beat * beat, voice(n.midi + transpose, n.len * beat), gain, pan, 0.5);
}

inline void soundtrack(Mix &m) {
	const double end = m.seconds();
	// ---- 1. The land of Hongzhou (0 - 45): black, wind; the bell; a very low guqin note; the land's theme; the strings opening at the Pavilion ----
	wind(m, 0.0, 45.0, 0.35);
	m.put(3.0, templeBell(38, 12.0), 0.15, 0.0, 0.8);
	m.put(9.0, guqin(38, 6.0, 0.0, 11u), 0.42, -0.1, 0.5);
	m.put(15.0, guqinHarmonic(74, 3.0), 0.12, 0.3, 0.7);
	play(m, 21.0, 0.9, landTheme(), -12.0, 0.30, -0.2, [](double n, double l) { return guqin(n, l + 1.6, 0.0, static_cast<unsigned>(n)); });
	strings(m, 24.0, {38, 45, 50}, 14.0, 0.045, 4.0, 4.0);
	strings(m, 37.0, {50, 54, 57, 62, 66}, 9.0, 0.06, 2.5, 3.0);   // (the Pavilion)
	m.put(37.0, guqin(50, 4.0, 2.0, 13u), 0.35, 0.1, 0.5);
	play(m, 39.0, 0.75, landTheme(), 0.0, 0.10, 0.3, [](double n, double l) { return xiao(n, l); });

	// ---- 2. A gathering of great men (45 - 95): hooves at the gate, the things of the feast, the hall, the guests, the musicians beginning ----
	hooves(m, 45.0, 8.5, 0.16);
	crowd(m, 45.0, 8.0, 0.025, false);
	for (int k = 0; k < 6; ++k) {   // the montage: a sound for each thing
		const double at = 53.0 + 2.0 * k;
		if (k == 0) m.put(at + 0.4, audio::knock(300.0, 0.15, 61u), 0.35, 0.0, 0.3);
		if (k == 1) for (int s = 0; s < 4; ++s) m.put(at + 0.45 * s, audio::knock(140.0, 0.12, 62u), 0.12, 0.1, 0.3);
		if (k == 2) m.put(at + 0.3, audio::bell(hz(93), 1.5), 0.10, 0.2, 0.6);
		if (k == 3) m.put(at + 0.2, audio::noiseBurst(0.5, 0.01, 6.0, 3000.0, 1500.0, 63u), 0.06, -0.2, 0.3);
		if (k == 4) m.put(at + 0.7, audio::boing(260.0, 0.3), 0.08, 0.0, 0.3);
		if (k == 5) for (int s = 0; s < 3; ++s) m.put(at + 0.5 * s, pipa(69 + 0.3 * s, 0.6, 64u), 0.14, 0.3, 0.4);
	}
	// the hall: the musicians begin, a gentle guzheng and pipa air in D, the murmur of the guests
	crowd(m, 65.0, 30.0, 0.04, true);
	for (int bar = 0; bar < 7; ++bar) {
		const double at = 66.0 + 4.0 * bar;
		zhengSweep(m, at, 62, 6, bar % 2 == 0, 0.17, -0.3);
		static const double melody[] = {74, 76, 78, 81, 78, 76, 74, 71};
		for (int k = 0; k < 4; ++k) m.put(at + 1.0 + 0.7 * k, pipa(melody[(bar * 2 + k) % 8], 0.9, 70u + static_cast<unsigned>(k)), 0.24, 0.35, 0.35);
	}
	strings(m, 85.0, {47, 54, 59, 62}, 9.0, 0.04, 2.0, 3.0);   // (the young traveller: a shadow under the music)

	// ---- 3. Ascending the Pavilion (95 - 140): lighter; the xiao joins the guqin; the wind on the terrace; the music opening ----
	crowd(m, 95.0, 18.0, 0.012, false);
	play(m, 96.0, 1.0, landTheme(), 0.0, 0.18, -0.3, [](double n, double l) { return guqin(n - 12.0, l + 1.2, 0.0, static_cast<unsigned>(n) + 3u); });
	play(m, 104.0, 1.0, landTheme(), 12.0, 0.11, 0.3, [](double n, double l) { return xiao(n, l * 0.95); });
	m.put(113.0, audio::noiseBurst(3.5, 0.6, 0.5, 600.0, 1400.0, 71u), 0.16, -0.2, 0.3);   // the wind as he steps out
	wind(m, 113.0, 27.0, 0.5);
	strings(m, 121.0, {50, 57, 62, 66, 69}, 19.0, 0.07, 5.0, 4.0);
	play(m, 127.0, 1.1, landTheme(), 0.0, 0.14, 0.0, [](double n, double l) { return xiao(n, l); });
	m.put(127.0, guqin(38, 8.0, 0.0, 72u), 0.3, -0.2, 0.6);

	// ---- 4. The world below (140 - 185): a symphony of the land, then the fishermen's song, the geese, and only wind ----
	for (int bar = 0; bar < 6; ++bar) {
		const double at = 140.0 + 4.0 * bar;
		drum(m, at, 0.10, -0.2, false);
		drum(m, at + 2.0, 0.07, 0.2, false);
		zhengSweep(m, at + 0.5, 62, 8, true, 0.06, -0.3);
		pipaTremolo(m, at + 1.5, 74 + (bar % 3) * 2, 1.2, 0.05, 0.4);
	}
	strings(m, 140.0, {50, 57, 62, 66}, 12.0, 0.05);
	strings(m, 152.0, {47, 54, 59, 62}, 12.0, 0.05);
	m.put(159.0, audio::noiseBurst(2.5, 0.05, 1.2, 4000.0, 2000.0, 81u), 0.08, 0.2, 0.4);   // (wings)
	// the fishermen's song across the water: a slow, open melody on the xiao, the low strings under it
	static const double song[][3] = {{69, 165.5, 1.5}, {71, 167.0, 1.0}, {74, 168.0, 2.0}, {71, 170.0, 1.0}, {69, 171.0, 1.0}, {66, 172.0, 2.0}};
	for (const auto &n : song) m.put(n[1], xiao(n[0], n[2]), 0.13, 0.2, 0.7);
	strings(m, 165.0, {45, 52, 57, 61}, 9.0, 0.05);
	water(m, 165.0, 10.0, 0.035);
	// the geese, their calls echoing; the music falls away and leaves the wind
	for (int k = 0; k < 9; ++k) goose(m, 175.0 + 0.9 * k + 0.4 * std::sin(k), 0.06, -0.6 + 0.15 * k);
	strings(m, 174.0, {45, 52, 57}, 5.0, 0.04, 1.0, 1.5);
	wind(m, 176.0, 22.0, 0.45);

	// ---- 5. Autumn (185 - 245): the leaf in silence; the strings very quietly; the swell to the first peak with the duck; the water's stillness ----
	m.put(186.5, guqinHarmonic(81, 4.0), 0.10, 0.2, 0.8);
	strings(m, 196.0, {50, 57, 62}, 14.0, 0.035, 6.0, 3.0);
	strings(m, 203.0, {47, 54, 59, 62, 66}, 9.0, 0.05, 4.0, 3.0);
	// the peak: the hero's theme, slow, on the strings and the choir, the guqin underneath
	strings(m, 210.0, {38, 50, 57, 62, 66, 69}, 18.0, 0.07, 3.0, 5.0);
	play(m, 211.0, 1.35, heroTheme(), 0.0, 0.10, 0.0, [](double n, double l) { return cello(n, l); });
	play(m, 211.0, 1.35, heroTheme(), 12.0, 0.045, 0.3, [](double n, double l) { return audio::pad(hz(n), l, 0.6, 1.2); });
	for (int k = 0; k < 6; ++k) m.put(211.0 + 3.0 * k, guqin(50 + (k % 3) * 2, 3.0, 0.0, 90u + static_cast<unsigned>(k)), 0.16, -0.3, 0.6);
	m.put(220.0, templeBell(50, 8.0), 0.12, 0.2, 0.9);
	strings(m, 228.0, {50, 57, 62, 66, 69, 74}, 14.0, 0.05, 2.0, 6.0);
	water(m, 228.0, 17.0, 0.03);

	// ---- 6. The banquet (245 - 295): laughter, music, wine; the paper comes round and the room quietens; his breathing; one sharp stroke -- black ----
	crowd(m, 245.0, 18.0, 0.06, true);
	m.put(245.6, audio::noiseBurst(3.5, 0.05, 0.3, 2500.0, 2000.0, 101u), 0.05, 0.2, 0.3);   // (the wine)
	m.put(249.6, audio::bell(hz(96), 0.5), 0.10, 0.3, 0.3);   // (the cup)
	for (int bar = 0; bar < 4; ++bar) {
		const double at = 250.0 + 3.2 * bar;
		pipaTremolo(m, at, 74 + (bar % 2) * 2, 1.4, 0.18, 0.3);
		static const double air[] = {71, 74, 76, 74, 71, 69};
		for (int k = 0; k < 3; ++k) m.put(at + 1.6 + 0.5 * k, pipa(air[(bar + k) % 6], 0.8, 110u), 0.24, 0.3, 0.3);
		drum(m, at, 0.13, -0.3, false);
	}
	crowd(m, 263.0, 18.0, 0.015, false);
	strings(m, 268.0, {47, 54, 59}, 13.0, 0.035, 4.0, 3.0);
	for (double s = 281.0; s < 294.0; s += 3.4) m.put(s, audio::noiseBurst(2.4, 1.0, 1.0, 500.0, 350.0, 121u + static_cast<unsigned>(s)), 0.05, 0.0, 0.1);   // (his breathing)
	m.put(294.4, audio::noiseBurst(0.35, 0.004, 9.0, 5000.0, 900.0, 131u), 0.4, 0.0, 0.6);   // the stroke

	// ---- 7. The world inside the brush (295 - 350): wet ink and the guqin; the flight through the clouds; joy exhausted -- the low cello ----
	m.put(296.0, guqin(50, 6.0, -2.0, 141u), 0.3, 0.0, 0.7);
	for (int k = 0; k < 5; ++k) m.put(297.0 + 4.3 * k, audio::noiseBurst(1.5, 0.3, 1.6, 1200.0, 500.0, 142u + static_cast<unsigned>(k)), 0.03, 0.4 * std::sin(k), 0.6);   // (ink spreading)
	strings(m, 300.0, {50, 57, 62}, 18.0, 0.045, 5.0, 4.0);
	m.put(313.0, audio::bass(hz(31), 4.0), 0.12, 0.0, 0.6);   // (the storm)
	strings(m, 318.0, {38, 50, 57, 62, 66, 69}, 16.0, 0.065, 5.0, 4.0);
	play(m, 320.0, 1.2, landTheme(), 12.0, 0.08, 0.2, [](double n, double l) { return audio::pad(hz(n), l, 0.5, 1.0); });
	m.put(321.0, guqin(38, 10.0, 0.0, 143u), 0.3, -0.3, 0.6);
	strings(m, 335.0, {47, 54, 59, 62}, 15.0, 0.04, 3.0, 4.0);
	play(m, 336.0, 1.6, {{59, 0, 2}, {57, 2, 1}, {54, 3, 2}, {52, 5, 1}, {50, 6, 3}}, -12.0, 0.16, -0.1, [](double n, double l) { return cello(n, l); });

	// ---- 8. Distance (350 - 405): colder; the cello alone; the rain; the crossroads -- no music, only wind ----
	crowd(m, 350.0, 10.0, 0.008, true);
	play(m, 351.0, 2.0, {{47, 0, 2}, {50, 2, 1}, {52, 3, 2}, {50, 5, 1}, {47, 6, 3}, {45, 9, 2}}, -12.0, 0.15, 0.0, [](double n, double l) { return cello(n, l); });
	strings(m, 360.0, {35, 42, 47, 50}, 12.0, 0.04, 4.0, 4.0);
	wind(m, 360.0, 45.0, 0.5);
	for (double s = 372.0; s < 390.0; s += 0.25) m.put(s, audio::noiseBurst(0.6, 0.05, 2.0, 5000.0, 3000.0, 151u + static_cast<unsigned>(s * 7.0)), 0.025, std::sin(s * 3.0), 0.4);   // the rain
	m.put(380.0, guqinHarmonic(71, 4.0), 0.06, 0.2, 0.9);

	// ---- 9. Unrecognised talent (405 - 455): a dream's drone; the doors closing; the snow; the ink drop; the stone into the deep -- almost nothing ----
	strings(m, 405.0, {35, 42}, 26.0, 0.045, 5.0, 5.0);
	m.put(416.0, audio::bass(hz(26), 3.0), 0.35, 0.0, 0.9);
	m.put(416.0, audio::knock(60.0, 0.8, 161u), 0.3, 0.0, 0.9);
	wind(m, 419.0, 12.0, 0.6, 33);
	for (int k = 0; k < 12; ++k) m.put(420.0 + 0.9 * k, audio::knock(110.0, 0.2, 162u), 0.05, -0.4 + 0.07 * k, 0.4);   // (marching)
	for (double s = 431.0; s < 441.0; s += 3.3) m.put(s + 3.0, audio::knock(900.0, 0.06, 163u), 0.06, 0.0, 0.5);   // (ink dripping)
	m.put(444.8, audio::noiseBurst(0.8, 0.003, 4.0, 2500.0, 400.0, 164u), 0.25, 0.0, 0.8);   // the stone into the water
	m.put(445.0, audio::bass(hz(24), 8.0), 0.18, 0.0, 0.9);
	for (int k = 0; k < 8; ++k) m.put(446.0 + 1.1 * k, audio::chirp(hz(80 + k), hz(86 + k), 0.08, 3.0, 0.01, 0.3), 0.02, 0.2 * std::sin(k), 0.9);   // (bubbles)

	// ---- 10. Defiance (455 - 505): one cello, then the strings, then the low drums; the wind; the eagle; the whole orchestra, the guqin underneath ----
	play(m, 455.5, 1.4, heroTheme(), -12.0, 0.14, 0.0, [](double n, double l) { return cello(n, l); });
	wind(m, 465.0, 25.0, 0.8, 41);
	strings(m, 465.0, {38, 45, 50, 54}, 10.0, 0.06, 3.0, 2.0);
	strings(m, 473.0, {43, 50, 55, 59, 62}, 9.0, 0.07, 2.0, 2.0);
	for (int k = 0; k < 8; ++k) drum(m, 476.0 + 2.0 * k, 0.10 + 0.03 * k, -0.1);
	m.put(482.5, audio::chirp(hz(88), hz(80), 0.9, 8.0, 0.03, 1.0), 0.12, 0.3, 0.8);   // the eagle
	strings(m, 483.0, {45, 52, 57, 61, 64}, 7.0, 0.08, 1.5, 2.0);
	// the orchestra opens: the hero's theme on horns and strings, drums driving, the guqin's low string sounding under it all
	strings(m, 490.0, {38, 50, 57, 62, 66, 69, 74}, 15.0, 0.09, 0.8, 3.0);
	play(m, 490.0, 0.75, heroTheme(), 0.0, 0.14, -0.1, [](double n, double l) { return horn(n, l + 0.2); });
	play(m, 490.0, 0.75, heroTheme(), 12.0, 0.05, 0.3, [](double n, double l) { return audio::pad(hz(n), l, 0.1, 0.6); });
	for (double s = 490.0; s < 505.0; s += 0.75) drum(m, s, std::fmod(s - 490.0, 1.5) < 0.1 ? 0.24 : 0.12, std::fmod(s, 3.0) < 1.5 ? -0.2 : 0.2);
	for (int k = 0; k < 5; ++k) m.put(490.0 + 3.0 * k, guqin(38, 4.0, 0.0, 170u + static_cast<unsigned>(k)), 0.26, -0.4, 0.4);
	m.put(497.0, audio::noiseBurst(1.5, 0.8, 1.0, 1500.0, 6000.0, 171u), 0.08, 0.0, 0.5);

	// ---- 11. The young Wang Bo (505 - 545): silence but for the brush; the faces; the brush laid down; silence ----
	for (double s = 505.5; s < 529.0; s += 1.15) m.put(s + 0.2 * std::sin(s * 3.0), audio::noiseBurst(0.35, 0.03, 6.0, 2500.0, 1200.0, 181u + static_cast<unsigned>(s * 3.0)), 0.035, 0.1, 0.3);
	m.put(530.5, audio::knock(1200.0, 0.05, 182u), 0.06, 0.1, 0.4);
	strings(m, 533.0, {50, 57, 62, 66}, 10.0, 0.035, 4.0, 4.0);

	// ---- 12. Epilogue (545 - end): the lanterns going out; the stair; hooves; the night and its river; the last guqin melody; wings, wind, water; the title ----
	water(m, 545.0, 64.0, 0.02);
	for (int k = 0; k < 8; ++k) m.put(545.5 + 0.95 * k, audio::noiseBurst(0.3, 0.01, 8.0, 1200.0, 800.0, 191u + static_cast<unsigned>(k)), 0.03, 0.4 * std::sin(k), 0.4);   // (lanterns put out)
	for (double s = 553.5; s < 559.0; s += 0.55) m.put(s, audio::knock(220.0, 0.1, 192u), 0.04, 0.0, 0.5);   // (his steps on the stair)
	hooves(m, 563.0, 12.0, 0.1, 1.2);
	for (int k = 0; k < 30; ++k) m.put(560.0 + 1.3 * k, audio::chirp(hz(98), hz(99), 0.05, 30.0, 0.05, 0.4), 0.008, 0.6 * std::sin(k), 0.6);   // (crickets)
	strings(m, 575.0, {38, 45, 50, 57}, 16.0, 0.04, 5.0, 5.0);
	play(m, 583.0, 1.15, landTheme(), 0.0, 0.26, -0.1, [](double n, double l) { return guqin(n - 12.0, l + 2.0, 0.0, static_cast<unsigned>(n) + 9u); });
	m.put(596.0, guqinHarmonic(74, 5.0), 0.10, 0.2, 0.8);
	wind(m, 600.0, 9.0, 0.35);
	for (int k = 0; k < 7; ++k) m.put(602.0 + 0.45 * k, audio::noiseBurst(0.3, 0.05, 6.0, 900.0, 600.0, 201u), 0.04, -0.3 + 0.1 * k, 0.4);   // (wings)
	m.put(611.0, guqin(50, 8.0, 0.0, 211u), 0.22, 0.0, 0.8);
	m.put(611.0, guqinHarmonic(74, 7.0), 0.08, 0.1, 0.9);
	(void)end;
}

}  // namespace score

inline void soundtrack(audio::Mix &m) { score::soundtrack(m); }

}  // namespace tengwang
}  // namespace cartoon
