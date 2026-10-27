#pragma once
// film_soundscape.h -- the sounds of a nature film placed where they come from: each heard from the camera as it moves - later the further off it is (sound's 343 m/s),
// quieter with distance, panned to the side it is on, with more of the room's reverb the further it is.
//
//   waterfalls   a roar from the pool at the foot of each
//   fire         a fire's low roar and its crackle; a wildfire's roar; lava's hiss and its bubbles popping; a volcano's rumble
//   surf         each wave crashing where it breaks near the camera, and the fizz of its wash after
//   animals      hoofbeats at each footfall of a herd's gait (a galloping herd thunders; wolves' pads are soft), eagles' cries from the sky, wolves howling when they
//                stand
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
inline std::vector<std::vector<float>> roar(double lo, double hi, double length, unsigned seed) {   // grains of a roar between lo and hi Hz
	std::vector<std::vector<float>> out;
	for (unsigned k = 0; k < 6; ++k) out.push_back(audio::noiseBurst(length, 0.35 * length, 0.55 * length, lo, hi, seed + 13 * k));
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

}  // namespace soundscape
}  // namespace cartoon
