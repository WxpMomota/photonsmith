#pragma once
// sfx.h -- a small library of sound effects for the films, and the player that places them from a film's events (FilmEvent, film_events.h): footsteps on dust, stone, grass,
// wood, metal or snow; impacts light or heavy; flashes; a hand laid on something. Every sound is made here from noise and partials (cartoon_audio.h), different for each seed,
// so two footsteps in a row are not the same sound.
//
//     sfx::Palette p;                    // what this film's world sounds like
//     p.ground = sfx::Surface::Stone;
//     sfx::play(mix, film.events(), p);  // every footfall, impact, flash and touch, at its time, pan and strength

#include <algorithm>
#include <cmath>
#include <vector>

#include "cartoon_audio.h"
#include "cartoon_lib.h"

namespace cartoon {
namespace sfx {

enum class Surface { Dust, Stone, Grass, Wood, Metal, Snow };

// Two sounds mixed (b from `offset` seconds in, scaled by `gain`).
inline std::vector<float> layered(std::vector<float> a, const std::vector<float> &b, double gain, double offset = 0.0) {
	const std::size_t at = static_cast<std::size_t>(std::max(0.0, offset) * audio::kRate);
	if (a.size() < at + b.size()) a.resize(at + b.size(), 0.0f);
	for (std::size_t i = 0; i < b.size(); ++i) a[at + i] += static_cast<float>(gain) * b[i];
	return a;
}
inline unsigned take(int seed, unsigned salt) { return 2654435761u * static_cast<unsigned>(seed + 1) + salt; }
inline double vary(int seed, unsigned salt, double amount) { return 1.0 + amount * (2.0 * audio::unit(take(seed, salt)) - 1.0); }

// A footstep: a body (the heel's thud) and a top (grit, a scrape, a click), each surface its own.
inline std::vector<float> footstep(Surface s, int seed) {
	const double v = vary(seed, 11u, 0.18);
	switch (s) {
		case Surface::Dust:   // (the Nomai film's: a soft thud and a little grit)
			return layered(audio::noiseBurst(0.18, 0.004, 18.0, 520.0 * v, 180.0, take(seed, 1u)), audio::noiseBurst(0.10, 0.002, 40.0, 3200.0 * v, 1800.0, take(seed, 2u)), 0.25, 0.02);
		case Surface::Stone:
			return layered(audio::knock(110.0 * v, 0.12, take(seed, 3u)), audio::noiseBurst(0.06, 0.001, 70.0, 5200.0 * v, 2600.0, take(seed, 4u)), 0.35, 0.004);
		case Surface::Grass:
			return layered(audio::noiseBurst(0.22, 0.02, 14.0, 2400.0 * v, 900.0, take(seed, 5u)), audio::noiseBurst(0.14, 0.004, 22.0, 420.0, 160.0, take(seed, 6u)), 0.6);
		case Surface::Wood:
			return layered(audio::knock(190.0 * v, 0.16, take(seed, 7u)), audio::additive(410.0 * v, {{1.0, 1.0, 26.0}, {2.3, 0.4, 40.0}}, 0.12, 0.001), 0.15, 0.003);
		case Surface::Metal:
			return layered(audio::knock(260.0 * v, 0.10, take(seed, 8u)), audio::additive(1180.0 * v, {{1.0, 1.0, 9.0}, {2.76, 0.5, 13.0}, {5.4, 0.25, 18.0}}, 0.5, 0.001), 0.12, 0.002);
		case Surface::Snow:
			return layered(audio::noiseBurst(0.26, 0.03, 10.0, 1400.0 * v, 500.0, take(seed, 9u)), audio::noiseBurst(0.20, 0.01, 16.0, 6000.0, 3500.0, take(seed, 10u)), 0.3, 0.05);
	}
	return {};
}
// Something landing: a low thud with a rush of what it throws up. `weight` 0 (a ball) .. 1 (a ship setting down) makes it lower and longer.
inline std::vector<float> impact(double weight, int seed) {
	const double w = std::clamp(weight, 0.0, 1.0);
	return layered(audio::knock(lerp(240.0, 60.0, w) * vary(seed, 12u, 0.1), lerp(0.12, 0.5, w), take(seed, 13u)),
				   audio::noiseBurst(lerp(0.3, 3.0, w), 0.01 + 0.04 * w, lerp(14.0, 0.9, w), lerp(2400.0, 420.0, w), lerp(900.0, 130.0, w), take(seed, 14u)), 0.8);
}
// A flash: a bright glassy ping.
inline std::vector<float> flash(int seed) {
	const double f = 1760.0 * vary(seed, 15u, 0.06);
	return layered(audio::additive(f, {{1.0, 1.0, 5.0}, {2.0, 0.35, 8.0}, {3.01, 0.15, 12.0}}, 0.9, 0.001), audio::noiseBurst(0.05, 0.001, 60.0, 9000.0, 6000.0, take(seed, 16u)), 0.2);
}
// A hand laid on something hard: a soft tap.
inline std::vector<float> touch(int seed) { return audio::knock(300.0 * vary(seed, 17u, 0.08), 0.1, take(seed, 18u)); }

// What a film's world sounds like: the ground, and how loud each kind of event is.
struct Palette {
	Surface ground = Surface::Dust;
	double footfall = 0.18, impact = 0.3, flash = 0.08, touch = 0.06;
	double weight = 0.5;   // an impact's weight (impact())
	double send = 0.15;    // how much goes to the reverb
};

// Every event at its time, pan and strength.
inline void play(audio::Mix &m, const std::vector<FilmEvent> &events, const Palette &p = {}) {
	for (const FilmEvent &e : events) {
		switch (e.kind) {
			case EventKind::Footfall: m.put(e.t, footstep(p.ground, e.seed), p.footfall * e.strength, e.pan, p.send); break;
			case EventKind::Impact: m.put(e.t, impact(p.weight, e.seed), p.impact * e.strength, e.pan, p.send * 2.0); break;
			case EventKind::Flash: m.put(e.t, flash(e.seed), p.flash * e.strength, e.pan, p.send * 3.0); break;
			case EventKind::Touch: m.put(e.t, touch(e.seed), p.touch * e.strength, e.pan, p.send * 2.0); break;
		}
	}
}

}  // namespace sfx
}  // namespace cartoon
