#pragma once
// crystal_heart_score.h -- the music of "The Crystal Heart" (cartoon_audio.h), on the film's own clock (crystal_heart.h): a low drone and a distant choir in D minor over the
// still lake, with crystal chimes; a slow pulse of low drums and a rising glassy figure between the spires; the harmony opening to D major as the sun clears the ridge, with
// a horn-like call; three deep heartbeats on the Heart's own beats; a rising rush into one great impact as it ignites, then the full theme over driving drums; and a last
// swell that resolves as the camera rises over the valley.

#include "cartoon_audio.h"
#include "crystal_heart.h"

namespace cartoon {
namespace crystal_heart {
namespace score {

using audio::hz;
using audio::Mix;

constexpr double kBeat = 60.0 / 72.0;   // 72 beats a minute

inline double unit(unsigned seed) {
	audio::Noise n(seed);
	n.next();
	return 0.5 * (n.next() + 1.0);
}

// A choir-like chord: one pad per note, spread across the stereo field, with plenty of reverb.
inline void choir(Mix &m, double t, std::initializer_list<double> midis, double length, double gain, double attack, double release) {
	double pan = -0.6;
	const double step = midis.size() > 1 ? 1.2 / static_cast<double>(midis.size() - 1) : 0.0;
	for (double n : midis) {
		m.put(t, audio::pad(hz(n), length, attack, release), gain, pan, 0.7);
		pan += step;
	}
}
// A horn: a warm note that swells and holds (odd and even overtones, slow attack).
inline std::vector<float> horn(double f, double length) {
	std::vector<float> v = audio::additive(f, {{1.0, 1.0, 0.25}, {2.0, 0.55, 0.35}, {3.0, 0.32, 0.5}, {4.0, 0.18, 0.7}, {5.0, 0.10, 0.9}}, length, 0.18);
	return v;
}
// A deep drum: a falling low tone, a thump of noise and the knock of the skin.
inline void drum(Mix &m, double t, double gain, double pan = 0.0) {
	m.put(t, audio::bass(hz(26), 1.4), gain, pan, 0.25);
	m.put(t, audio::knock(70.0, 0.5, 9u), gain * 0.9, pan, 0.2);
	m.put(t, audio::noiseBurst(0.4, 0.002, 12.0, 400.0, 120.0, 5u), gain * 0.35, pan, 0.3);
}
// The Heart's beat: two deep thumps a third of a second apart, as its light pulses.
inline void heartbeat(Mix &m, double t, double gain) {
	for (int k = 0; k < 2; ++k) {
		const double at = t + 0.32 * k, g = gain * (k == 0 ? 1.0 : 0.7);
		m.put(at, audio::bass(hz(24), 1.2), g, 0.0, 0.35);
		m.put(at, audio::knock(55.0, 0.45, 13u + static_cast<unsigned>(k)), g * 0.8, 0.0, 0.3);
	}
}

inline void soundtrack(Mix &m) {
	const double end = m.seconds();

	// ---- the still lake (0 - 10): a drone and a distant choir in D minor, crystal chimes ----
	m.put(0.0, audio::pad(hz(26), 20.0, 4.0, 4.0), 0.20, 0.0, 0.3);   // the drone, low D
	m.put(0.0, audio::pad(hz(38), 20.0, 5.0, 4.0), 0.10, 0.0, 0.4);
	choir(m, 1.0, {62, 65, 69}, 9.0, 0.040, 4.0, 3.0);
	const double minorPent[] = {74, 77, 79, 81, 84, 86, 89};
	for (int k = 0; k < 16; ++k) {   // the chimes: high notes of D minor's pentatonic scale, here and there
		const double at = 0.8 + k * 1.15 + 0.5 * unit(100u + static_cast<unsigned>(k));
		m.put(at, audio::bell(hz(minorPent[static_cast<int>(7.0 * unit(200u + static_cast<unsigned>(k)))]), 3.0), 0.045, -0.8 + 1.6 * unit(300u + static_cast<unsigned>(k)), 0.8);
	}

	// ---- among the spires (10 - 20): a slow drum pulse, the choir moving Dm - Bb - F - C, a glassy figure rising ----
	choir(m, 10.0, {62, 65, 69, 74}, 3.6, 0.036, 1.5, 1.5);
	choir(m, 13.3, {58, 62, 65, 70}, 3.6, 0.036, 1.5, 1.5);
	choir(m, 16.6, {57, 60, 65, 69}, 2.0, 0.036, 1.2, 1.2);
	choir(m, 18.3, {55, 60, 64, 67}, 2.4, 0.038, 1.0, 1.5);
	for (double t = 10.0; t < 20.0; t += 2.0 * kBeat) drum(m, t, 0.10 + 0.06 * (t - 10.0) / 10.0, -0.1);
	const double figure[] = {62, 65, 69, 74, 77, 81};
	for (int k = 0; k < 24; ++k) m.put(10.0 + k * 0.42, audio::mallet(hz(figure[k % 6] + 12.0 * (k / 12)), 1.4), 0.05, -0.5 + 0.04 * k, 0.6);

	// ---- first light (20 - 30): D major opens, a horn call rises ----
	m.put(20.0, audio::noiseBurst(2.5, 2.0, 0.6, 300.0, 2600.0, 21u), 0.06, 0.0, 0.6);   // the light's own rush
	choir(m, 20.8, {50, 57, 62, 66, 69}, 9.2, 0.050, 3.0, 2.5);
	m.put(20.8, audio::bass(hz(38), 8.0), 0.16, 0.0, 0.3);
	const double call[][2] = {{57, 22.0}, {62, 23.2}, {64, 24.4}, {66, 25.2}, {69, 26.4}, {66, 28.0}};
	for (const auto &n : call) m.put(n[1], horn(hz(n[0]), 2.4), 0.11, 0.15, 0.5);
	for (int k = 0; k < 8; ++k) m.put(24.0 + 0.7 * k, audio::bell(hz(86 + (k % 2) * 7), 2.4), 0.04, 0.6 - 0.15 * k, 0.8);

	// ---- the Heart (30 - 41): suspense on B minor and G, its three heartbeats, a rising rush ----
	choir(m, 30.0, {47, 54, 59, 62, 66}, 5.2, 0.042, 2.0, 1.5);
	choir(m, 35.2, {43, 50, 55, 59, 62}, 6.0, 0.044, 1.5, 1.0);
	heartbeat(m, kBeat1, 0.30);
	heartbeat(m, kBeat2, 0.38);
	heartbeat(m, kBeat3, 0.46);
	m.put(37.6, audio::noiseBurst(3.6, 3.4, 0.05, 200.0, 7000.0, 31u), 0.13, 0.0, 0.4);   // the rush into the ignition
	for (int k = 0; k < 12; ++k) m.put(38.4 + 0.22 * k, audio::mallet(hz(62 + k), 0.6), 0.04 + 0.004 * k, -0.3 + 0.05 * k, 0.5);

	// ---- ignition (41): one great impact ----
	m.put(kIgnite, audio::bass(hz(19), 5.0), 0.55, 0.0, 0.5);
	m.put(kIgnite, audio::noiseBurst(4.0, 0.004, 0.9, 6000.0, 300.0, 41u), 0.22, 0.0, 0.8);
	drum(m, kIgnite, 0.5);
	choir(m, kIgnite, {38, 50, 57, 62, 66, 69, 74}, 7.5, 0.050, 0.08, 2.5);

	// ---- the theme (42 - 50): the horn over driving drums, D - A - Bm - G ----
	const double chords[][5] = {{50, 57, 62, 66, 69}, {45, 52, 57, 61, 64}, {47, 54, 59, 62, 66}, {43, 50, 55, 59, 62}};
	for (int c = 0; c < 4; ++c) m.put(42.0 + 2.0 * c, audio::bass(hz(chords[c][0] - 12.0), 2.2), 0.20, 0.0, 0.3);
	for (double t = 42.0; t < 50.0; t += kBeat / 2.0) drum(m, t, (std::fmod(t - 42.0, kBeat) < 0.01 ? 0.22 : 0.12), std::fmod(t, 2.0 * kBeat) < kBeat ? -0.2 : 0.2);
	const double theme[][3] = {{74, 42.2, 1.2}, {73, 43.4, 0.6}, {69, 44.0, 1.2}, {66, 45.2, 0.8}, {69, 46.0, 1.0}, {74, 47.0, 1.0}, {76, 48.0, 0.8}, {78, 48.8, 2.6}};
	for (const auto &n : theme) m.put(n[1], horn(hz(n[0]), n[2] + 0.6), 0.12, -0.1, 0.5);

	// ---- ascension (50 - 60): G - A - D, shimmering bells, the last chord dying away ----
	choir(m, 50.0, {43, 50, 55, 59, 62, 67}, 2.6, 0.048, 0.8, 1.0);
	choir(m, 52.4, {45, 52, 57, 61, 64, 69}, 2.0, 0.048, 0.8, 1.0);
	choir(m, 54.2, {38, 50, 57, 62, 66, 69, 74}, end - 54.2 - 1.0, 0.055, 1.2, 4.0);
	m.put(54.2, audio::bass(hz(26), 6.0), 0.25, 0.0, 0.4);
	m.put(54.2, horn(hz(74), 4.5), 0.10, 0.0, 0.6);
	const double majorPent[] = {74, 76, 78, 81, 83, 86, 88, 90};
	for (int k = 0; k < 20; ++k) m.put(50.2 + k * 0.45, audio::bell(hz(majorPent[k % 8]), 2.6), 0.035, -0.8 + 0.08 * k, 0.8);
}

}  // namespace score

inline void soundtrack(audio::Mix &m) { score::soundtrack(m); }

}  // namespace crystal_heart
}  // namespace cartoon
