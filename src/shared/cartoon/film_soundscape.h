#pragma once
// film_soundscape.h -- the sounds of a nature film placed where they come from: each heard from the camera as it moves - later the further off it is (sound's 343 m/s),
// quieter with distance, panned to the side it is on, with more of the room's reverb the further it is.
//
//   waterfalls   a roar from the pool at the foot of each
//   fire         a fire's low roar and its crackle; a wildfire's roar; lava's hiss and its bubbles popping; a volcano's rumble
//   surf         each wave crashing where it breaks near the camera, and the fizz of its wash after
//   animals      hoofbeats at each footfall of a herd's gait (a galloping herd thunders; wolves' pads are soft), eagles' cries from the sky; a standing herd's calls (callOf:
//                a lion's roar, an elephant's trumpet, a wolf's howl, a bear's growl, a fox's bark, a zebra's bray), geese honking as a flight goes
//   birds        songs from the trees of a forest near the camera
//
// A continuous sound (a roar, a hiss) is laid as overlapping grains a fraction of a second apart, each placed from where the camera is when it is heard; an event (a crash,
// a hoofbeat, a cry) is one sound placed once. Coordinates are pbrt's.

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

#include "cartoon_audio.h"
#include "cartoon_lib.h"

namespace cartoon {
namespace soundscape {

// Where the camera is at each moment (sampled every `dt` seconds and eased between) and which way its right is (pbrt's).
struct Ears {
	struct At {
		Vec3 eye, right{1, 0, 0};
	};
	std::vector<At> at;
	double dt = 0.1;
	At operator()(double t) const {
		if (at.empty()) return At();
		const double k = std::clamp(t / dt, 0.0, static_cast<double>(at.size() - 1));
		const std::size_t i = std::min(at.size() - 1, static_cast<std::size_t>(k));
		const std::size_t j = std::min(at.size() - 1, i + 1);
		const double u = k - i;
		At a;
		a.eye = at[i].eye * (1.0 - u) + at[j].eye * u;
		const Vec3 r = at[i].right * (1.0 - u) + at[j].right * u;
		a.right = length(r) > 1e-9 ? normalize(r) : Vec3{1, 0, 0};
		return a;
	}
};
// The ears over `seconds` from the camera at each time: `camera(t, eye, target)` in story space (false: no camera then - the last place kept).
inline Ears earsOf(double seconds, const std::function<bool(double, Vec3 &, Vec3 &)> &camera, double dt = 0.1) {
	Ears e;
	e.dt = dt;
	Ears::At last;
	for (double t = 0.0; t <= seconds + dt; t += dt) {
		Vec3 eye, target;
		if (camera(t, eye, target)) {
			const Vec3 fwd = target - eye, r = cross(Vec3{fwd.x, 0.0, fwd.z}, Vec3{0, 1, 0});   // (story space: +x is the picture's right)
			last.eye = {-eye.x, eye.y, eye.z};
			if (length(r) > 1e-9) last.right = normalize(Vec3{-r.x, 0.0, r.z});
		}
		e.at.push_back(last);
	}
	return e;
}

// A sound from pbrt's `p`, made at `t`, as heard: delayed by its distance, `gain` within `within` metres, falling off as 1/d beyond (and a little more over kilometres of
// air), panned to its side, more reverb further off.
inline void place(audio::Mix &m, const Ears &ears, double t, const Vec3 &p, const std::vector<float> &sound, double gain, double within) {
	const Ears::At a = ears(t);
	const Vec3 d = p - a.eye;
	const double dist = length(d), g = gain * within / std::max(within, dist) * std::exp(-dist / 4000.0);
	if (g < 2e-4 || sound.empty()) return;
	const Vec3 flat{d.x, 0.0, d.z};
	const double pan = length(flat) > 1e-6 ? 0.85 * std::clamp(dot(normalize(flat), a.right), -1.0, 1.0) : 0.0;
	m.put(t + dist / 343.0, sound, g, pan, 0.15 + 0.5 * std::min(1.0, dist / 250.0));
}
// A continuous sound from `where(t)` while `level(t)` > 0: grains `grain(k)` (a few kinds, k = 0, 1, 2 ...) every `step` seconds from t0 to t1.
inline void lay(audio::Mix &m, const Ears &ears, double t0, double t1, double step, const std::function<Vec3(double)> &where, const std::function<double(double)> &level,
				const std::vector<std::vector<float>> &grains, double within) {
	if (grains.empty()) return;
	int k = 0;
	for (double t = std::max(0.0, t0); t < std::min(t1, m.seconds()); t += step, ++k) {
		const double g = level(t);
		if (g > 0.0) place(m, ears, t, where(t), grains[static_cast<std::size_t>(k) % grains.size()], g, within);
	}
}

// ---- the sounds ----
// Grains of a roar between lo and hi Hz, each swelling and dying away smoothly (a raised-cosine window: grains laid half a grain apart sum to an even roar, not a pulse).
// Filtered noise is quieter the lower its band (its power goes with the cut-off), so a low rumble is brought up to stand with the rest: a volcano is not 20 dB under a crackle.
inline std::vector<std::vector<float>> roar(double lo, double hi, double length, unsigned seed) {
	std::vector<std::vector<float>> out;
	const double level = std::clamp(std::sqrt(1000.0 / std::max(1.0, 0.5 * (lo + hi))), 0.5, 4.0);
	for (unsigned k = 0; k < 6; ++k) {
		std::vector<float> g = audio::noiseBurst(length, 0.0, 0.0, lo, hi, seed + 13 * k);
		for (std::size_t i = 0; i < g.size(); ++i) {
			const double w = std::sin(kPi * static_cast<double>(i) / std::max<std::size_t>(1, g.size() - 1));
			g[i] = static_cast<float>(g[i] * w * w * level);
		}
		out.push_back(std::move(g));
	}
	return out;
}
inline std::vector<float> crackle(unsigned seed) { return audio::noiseBurst(0.025, 0.001, 0.02, 2500.0, 9000.0, seed); }
inline std::vector<float> crash(unsigned seed) { return audio::noiseBurst(2.2, 0.06, 1.6, 500.0, 2600.0, seed); }
inline std::vector<float> wash(unsigned seed) { return audio::noiseBurst(2.8, 0.6, 1.8, 2500.0, 7000.0, seed); }
inline std::vector<float> hoof(double f, unsigned seed) { return audio::knock(f, 0.16, seed); }
inline std::vector<float> eagleCry() {   // a high, thin scream falling away, and a stutter of notes after it
	std::vector<float> s = audio::chirp(2900.0, 2300.0, 0.55, 22.0, 0.05);
	for (int k = 0; k < 3; ++k) {
		const std::vector<float> note = audio::chirp(2600.0 - 120.0 * k, 2350.0 - 120.0 * k, 0.12, 0.0, 0.0, 0.7);
		const std::size_t at = s.size() + static_cast<std::size_t>(0.06 * audio::kRate);
		s.resize(at + note.size(), 0.0f);
		for (std::size_t i = 0; i < note.size(); ++i) s[at + i] += note[i];
	}
	return s;
}
inline std::vector<float> howl(unsigned seed) {   // rising, holding, falling away
	const double f = 380.0 + 60.0 * audio::unit(seed);
	std::vector<float> s = audio::chirp(f, f * 1.45, 0.9, 5.0, 0.012);
	const std::vector<float> fall = audio::chirp(f * 1.45, f * 1.05, 1.8, 5.0, 0.018);
	s.insert(s.end(), fall.begin(), fall.end());
	audio::edges(s, 0.15, 0.6);
	return s;
}
inline std::vector<float> birdPhrase(unsigned seed) {   // three to six quick notes
	std::vector<float> s;
	const int notes = 3 + static_cast<int>(4 * audio::unit(seed));
	const double base = 2800.0 + 2200.0 * audio::unit(seed + 1);
	for (int k = 0; k < notes; ++k) {
		const double f0 = base * (1.0 + 0.25 * (audio::unit(seed + 7 * k + 2) - 0.5)), f1 = f0 * (0.8 + 0.4 * audio::unit(seed + 7 * k + 3));
		const std::vector<float> n = audio::chirp(f0, f1, 0.06 + 0.08 * audio::unit(seed + 7 * k + 4), 30.0, 0.03, 0.6);
		s.insert(s.end(), n.begin(), n.end());
		s.resize(s.size() + static_cast<std::size_t>(0.03 * audio::kRate), 0.0f);
	}
	return s;
}
inline std::vector<float> bubble(unsigned seed) { return audio::slide(110.0 + 50.0 * audio::unit(seed), 55.0, 0.3, 9.0); }

// ---- animal calls (round 13) ----
// A voiced call, as an animal's throat makes one: a buzz (the vocal folds' pulses, rich in harmonics) gliding from f0 to f1, made rough by `rough` (the pulses'
// rate jittering and their strength flickering ~30 times a second: a growl, a roar's rasp), with `breath` of noise, through the mouth's two resonances (formants
// at fA and fB Hz) that make it an "ah" or an "oo"; swelling in and dying away.
inline std::vector<float> voiced(double f0, double f1, double length, double fA, double fB, double rough, double breath, unsigned seed) {
	std::vector<float> v(audio::samplesFor(length), 0.0f);
	const double rate = audio::kRate;
	double phase = 0.0, jitter = 0.0, flicker = 0.0;
	unsigned noise = audio::mixSeed(seed);
	// two resonators (a state-variable band-pass each)
	double a1 = 0.0, b1 = 0.0, a2 = 0.0, b2 = 0.0;
	const double qA = 0.25, qB = 0.3;
	const double gA = 2.0 * std::sin(audio::kTau * 0.5 * std::min(fA, 0.2 * rate) / rate), gB = 2.0 * std::sin(audio::kTau * 0.5 * std::min(fB, 0.2 * rate) / rate);
	for (std::size_t i = 0; i < v.size(); ++i) {
		const double t = static_cast<double>(i) / rate, u = std::min(1.0, t / length);
		noise = noise * 1664525u + 1013904223u;
		const double white = (noise >> 8) / 8388608.0 - 1.0;
		jitter += (white - jitter) * 0.002, flicker += (white - flicker) * 0.004;
		const double f = (f0 + (f1 - f0) * u * u * (3.0 - 2.0 * u)) * (1.0 + 0.6 * rough * jitter);
		phase += f / rate;
		phase -= std::floor(phase);
		const double buzz = (2.0 * phase - 1.0) * (1.0 - 0.6 * rough * (0.5 + 0.5 * std::sin(audio::kTau * 31.0 * t + 6.0 * flicker)));
		const double src = buzz + breath * white;
		b1 += gA * a1, a1 += gA * (src - b1 - qA * a1);   // (a1: the band-pass at fA)
		b2 += gB * a2, a2 += gB * (src - b2 - qB * a2);
		const double env = std::min(1.0, t / std::max(0.01, 0.12 * length)) * std::min(1.0, (length - t) / std::max(0.01, 0.35 * length));
		v[i] = static_cast<float>(0.35 * env * (a1 + 0.6 * a2));
	}
	return v;
}
inline void then(std::vector<float> &s, const std::vector<float> &next, double gap) {
	s.resize(s.size() + static_cast<std::size_t>(gap * audio::kRate), 0.0f);
	s.insert(s.end(), next.begin(), next.end());
}
// A lion's roar: a deep moaning rise and the full roar falling away, then the grunts, each shorter and lower.
inline std::vector<float> roar(unsigned seed) {
	std::vector<float> s = voiced(110.0, 175.0, 0.9, 420.0, 1000.0, 0.35, 0.2, seed);
	then(s, voiced(185.0, 105.0, 1.6, 380.0, 950.0, 0.55, 0.3, seed + 1), 0.05);
	for (int k = 0; k < 5; ++k) then(s, voiced(135.0 - 6.0 * k, 95.0 - 4.0 * k, 0.55 - 0.06 * k, 340.0, 800.0, 0.6, 0.35, seed + 2 + k), 0.45 + 0.08 * k);
	return s;
}
// An elephant's trumpet: a brassy blast, rising and breaking, through the trunk (high resonances), and the low rumble under it.
inline std::vector<float> trumpet(unsigned seed) {
	std::vector<float> s = voiced(430.0, 560.0, 0.5, 1100.0, 2600.0, 0.25, 0.25, seed);
	then(s, voiced(560.0, 380.0, 0.9, 1000.0, 2400.0, 0.4, 0.3, seed + 1), 0.0);
	const std::vector<float> rumble = voiced(28.0, 24.0, 1.4, 120.0, 260.0, 0.3, 0.1, seed + 2);
	for (std::size_t i = 0; i < rumble.size() && i < s.size(); ++i) s[i] += 0.8f * rumble[i];
	return s;
}
// A bear's growl: low, long and rough.
inline std::vector<float> growl(unsigned seed) { return voiced(78.0, 62.0, 1.5, 300.0, 700.0, 0.95, 0.45, seed); }
// A fox's bark: two or three sharp yelps.
inline std::vector<float> bark(unsigned seed) {
	std::vector<float> s = voiced(820.0, 560.0, 0.16, 1300.0, 2600.0, 0.35, 0.3, seed);
	for (int k = 0; k < 1 + static_cast<int>(2.0 * audio::unit(seed + 3)); ++k) then(s, voiced(800.0 - 40.0 * k, 540.0, 0.15, 1300.0, 2600.0, 0.35, 0.3, seed + 4 + k), 0.55);
	return s;
}
// A zebra's bray (and a penguin's: higher): a run of quick barking gasps, in and out ("kwa-ha-ha").
inline std::vector<float> bray(unsigned seed, double pitch = 1.0) {
	std::vector<float> s;
	for (int k = 0; k < 6; ++k)
		then(s, voiced((k % 2 ? 360.0 : 620.0) * pitch, (k % 2 ? 330.0 : 520.0) * pitch, 0.14, 850.0 * pitch, 1900.0 * pitch, 0.4, 0.45, seed + k), k ? 0.05 : 0.0);
	return s;
}
// A horse's whinny: a high neigh shaking down.
inline std::vector<float> whinny(unsigned seed) { return audio::chirp(1100.0 + 100.0 * audio::unit(seed), 520.0, 1.3, 9.0, 0.08, 0.7); }
// A deer's bark (a roe's alarm): one gruff cough.
inline std::vector<float> deerBark(unsigned seed) { return voiced(420.0, 300.0, 0.3, 700.0, 1700.0, 0.6, 0.6, seed); }
// A goose's honk: two or three nasal honks.
inline std::vector<float> honk(unsigned seed) {
	std::vector<float> s = voiced(420.0, 380.0, 0.2, 950.0, 2100.0, 0.1, 0.15, seed);
	then(s, voiced(400.0, 360.0, 0.18, 950.0, 2100.0, 0.1, 0.15, seed + 1), 0.25);
	return s;
}
// A kind's call while it stands (an empty sound: it has none), how loud, from how far it carries (m), and every how many seconds one of a herd's callers calls.
struct Call {
	std::vector<float> sound;
	double gain = 0.0, within = 30.0, every = 0.0;
};
inline Call callOf(const std::string &kind, unsigned seed) {
	if (kind == "wolves") return {howl(seed), 0.4, 60.0, 25.0};
	if (kind == "lions") return {roar(seed), 0.55, 120.0, 30.0};
	if (kind == "elephants") return {trumpet(seed), 0.5, 100.0, 35.0};
	if (kind == "bears") return {growl(seed), 0.35, 30.0, 25.0};
	if (kind == "foxes") return {bark(seed), 0.3, 50.0, 20.0};
	if (kind == "zebras") return {bray(seed), 0.35, 60.0, 22.0};
	if (kind == "penguins") return {bray(seed, 1.35), 0.3, 40.0, 12.0};
	if (kind == "horses") return {whinny(seed), 0.3, 60.0, 30.0};
	if (kind == "deer") return {deerBark(seed), 0.3, 50.0, 28.0};
	return {};
}

}  // namespace soundscape
}  // namespace cartoon
