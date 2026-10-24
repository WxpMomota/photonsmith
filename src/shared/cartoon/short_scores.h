#pragma once
// short_scores.h -- music and sound for the short films that had none (cartoon_audio.h): Lamp and Ball, Ball Parade, The Little Robot, Fireflies, Newton's Cradle, Domino Run,
// The Paper Boat, The Orrery, The Dandelion, The Candle and Meet Pip. Each soundtrack reads its film's own motion, so a sound is on the frame of what makes it: the ball's
// bounces and the hit on the books come from Lamp and Ball's physics pass, every ball of the parade plays its own note as it lands, each domino ticks as it strikes the next,
// each drop on the pond makes its sound where the film draws its ring, the cradle clicks at every strike and softer as the swing dies, the fireflies' flashes ring. Original
// music, a few seconds of it each, made from the synthesiser's own instruments.

#include "cartoon_audio.h"
#include "candle.h"
#include "dandelion.h"
#include "domino_run.h"
#include "fireflies.h"
#include "ball_parade.h"
#include "lamp_and_ball.h"
#include "little_robot.h"
#include "meet_pip.h"
#include "newtons_cradle.h"
#include "orrery.h"
#include "paper_boat.h"

namespace cartoon {
namespace scores {

using audio::hz;
using audio::Mix;

// ---- shared sounds ---------------------------------------------------------------------------------------------------------------------------------------------------
inline double pan(int i, int salt, double spread = 0.6) { return spread * (2.0 * hash01(i, salt) - 1.0); }
// A rolling ball or marble: a low rumble over `length`, louder with `speed(t)` (0..1).
template <typename Speed>
inline void roll(Mix &m, double t0, double length, double gain, double bright, Speed speed) {
	for (double s = 0.0; s < length; s += 0.12) {
		const double v = speed(t0 + s);
		if (v > 0.02) m.put(t0 + s, audio::noiseBurst(0.22, 0.05, 6.0, 260.0 + 400.0 * bright, 180.0 + 200.0 * bright, 900u + static_cast<unsigned>(s * 40.0)), gain * v, 0.0, 0.15);
	}
}
// A soft chord of pads.
inline void chord(Mix &m, double t, std::initializer_list<double> midis, double length, double gain, double attack = 1.0, double release = 2.0) {
	int i = 0;
	for (double n : midis) m.put(t, audio::pad(hz(n), length, attack, release), gain, -0.4 + 0.8 * (i++ % 3) / 2.0, 0.6);
}
template <typename Voice>
inline void melody(Mix &m, double t, double beat, std::initializer_list<std::pair<double, double>> notes, double gain, double pan0, Voice voice) {
	double at = t;
	for (const auto &n : notes) {
		if (n.first > 0.0) m.put(at, voice(n.first, n.second * beat), gain, pan0, 0.4);
		at += n.second * beat;
	}
}
inline std::vector<float> celesta(double midi, double length) { return audio::additive(hz(midi), {{1.0, 1.0, 2.2}, {4.0, 0.22, 6.0}, {10.0, 0.05, 12.0}}, length, 0.002); }
inline std::vector<float> marimba(double midi, double length) { return audio::mallet(hz(midi), std::max(0.6, length)); }
inline std::vector<float> pizz(double midi, double length) { return audio::pluck(hz(midi), std::max(0.5, length), static_cast<unsigned>(midi), 0.994); }

// ---- Lamp and Ball -------------------------------------------------------------------------------------------------------------------------------------------------
inline void lampAndBall(Mix &m) {
	namespace L = lamp_and_ball;
	// a quiet room; the lamp clicks on and hums
	for (double t = 0.0; t < L::kDuration; t += 2.5) m.put(t, audio::noiseBurst(3.5, 1.2, 0.5, 300.0, 400.0, 11u + static_cast<unsigned>(t)), 0.012, 0.0, 0.3);
	m.put(1.2, audio::knock(1400.0, 0.04, 12u), 0.10, -0.3, 0.1);
	m.put(1.25, audio::pad(hz(43), 3.0, 0.3, 1.0), 0.02, -0.3, 0.1);
	// the ball rolls in
	roll(m, 4.0, 2.5, 0.10, 0.4, [](double t) { return clamp01(std::abs(L::ballAt(t).vel.x) / 4.0); });
	// the hops: a spring as it squashes and leaves, a light landing
	for (const L::Hop &h : L::hops()) {
		m.put(h.t0 - 0.05, audio::boing(330.0, 0.35), 0.06, -0.2, 0.2);
		m.put(h.t0 + h.dur, audio::knock(220.0, 0.12, 13u), 0.12, -0.2, 0.15);
	}
	// the nudge, then every bounce of the ball (from the physics pass) and its hit on the books
	m.put(L::kNudgeT, audio::knock(600.0, 0.08, 14u), 0.14, 0.1, 0.2);
	const auto &b = L::ball();
	for (std::size_t k = 1; k < b.size(); ++k) {
		const double t = k / 480.0;
		if (b[k - 1].vel.y < -1.0 && b[k].vel.y > 0.0) m.put(t, audio::knock(170.0 + 30.0 * std::min(8.0, -b[k - 1].vel.y), 0.20, 15u), clamp01(-b[k - 1].vel.y / 9.0) * 0.30, 0.3, 0.2);
		if (b[k - 1].vel.x > 0.5 && b[k].vel.x < 0.0) m.put(t, audio::knock(120.0, 0.30, 16u), 0.26, 0.6, 0.3);   // (the books)
	}
	roll(m, 12.0, 3.4, 0.07, 0.3, [&](double t) { return L::ballAt(t).pos.y < L::kBallR + 0.01 ? clamp01(std::abs(L::ballAt(t).vel.x) / 3.0) : 0.0; });
	// the music: a curious little marimba figure, pizzicato through the hops, a celesta lullaby as the shade comes down
	melody(m, 2.0, 0.42, {{72, 1}, {76, 1}, {79, 1}, {76, 1}, {74, 2}, {0, 1}, {71, 1}, {72, 3}}, 0.12, -0.1, marimba);
	for (int k = 0; k < 8; ++k) m.put(6.2 + 0.4 * k, pizz(60 + (k % 4 == 0 ? 0 : (k % 4 == 1 ? 7 : (k % 4 == 2 ? 4 : 7))), 0.5), 0.08, 0.2, 0.3);
	melody(m, L::kCoverT - 0.2, 0.55, {{79, 1}, {76, 1}, {72, 1}, {74, 1}, {76, 2}, {72, 3}}, 0.10, 0.0, celesta);
	chord(m, L::kCoverT, {48, 55, 64, 67}, 3.5, 0.025, 1.2, 2.0);
}

// ---- Ball Parade ---------------------------------------------------------------------------------------------------------------------------------------------------
inline void ballParade(Mix &m) {
	namespace B = ball_parade;
	static const double notes[B::kBalls] = {60, 62, 64, 67, 69, 72, 74};   // each ball its own note: the wave plays a scale
	for (int i = 0; i < B::kBalls; ++i) {
		double prevVy = 0.0;
		for (double tau = 0.0; tau < B::kDuration; tau += 1.0 / 960.0) {
			const B::BallState s = B::ballAt(tau);
			if (prevVy < -0.5 && s.vy > 0.0 && s.amp > 0.05) {
				const double t = tau + i * B::kDelay;
				if (t < B::kDuration) {
					const double loud = clamp01(s.amp / 3.0);
					m.put(t, marimba(notes[i], 0.9), 0.08 + 0.18 * loud, -0.7 + 1.4 * i / (B::kBalls - 1.0), 0.25);
					m.put(t, audio::knock(300.0 + 40.0 * i, 0.05, 20u + static_cast<unsigned>(i)), 0.05 * loud, -0.7 + 1.4 * i / (B::kBalls - 1.0), 0.1);
				}
			}
			prevVy = s.vy;
		}
	}
	// under it, a soft bass line and a final chord as they come to rest
	for (int k = 0; k < 5; ++k) m.put(1.8 + 2.0 * k, audio::bass(hz(k % 2 == 0 ? 36 : 43), 1.8), 0.10, 0.0, 0.2);
	chord(m, 10.6, {48, 55, 60, 64}, 1.2, 0.03, 0.4, 1.0);
}

// ---- The Little Robot ----------------------------------------------------------------------------------------------------------------------------------------------
inline void servo(Mix &m, double t, double length, double gain, double f0 = 520.0, double f1 = 760.0) {
	m.put(t, audio::slide(f0, f1, length, 1.0), gain * 0.5, -0.1, 0.1);
	m.put(t, audio::noiseBurst(length, 0.02, 1.5, 1800.0, 1400.0, 31u), gain * 0.3, -0.1, 0.1);
}
inline void littleRobot(Mix &m) {
	namespace R = little_robot;
	// dusk in the junkyard: wind, a far bird, the creak of metal
	for (double t = 0.0; t < R::kDuration; t += 2.2) m.put(t, audio::noiseBurst(3.0, 1.1, 0.5, 300.0, 520.0, 32u + static_cast<unsigned>(t)), 0.03, std::sin(t), 0.3);
	for (int k = 0; k < 4; ++k) m.put(1.0 + 7.0 * k, audio::chirp(hz(88), hz(84), 0.25, 9.0, 0.03, 1.0), 0.02, 0.6, 0.6);
	// the antenna flickers (electric crackles), the eyes come on (a rising power-up), he sits up (servos)
	for (const double t : {4.45, 4.95, 5.25}) m.put(t, audio::noiseBurst(0.08, 0.002, 30.0, 6000.0, 3000.0, 33u), 0.08, 0.3, 0.2);
	m.put(5.45, audio::slide(300.0, 900.0, 0.3, 2.0), 0.06, 0.3, 0.2);
	m.put(5.9, audio::chirp(hz(76), hz(88), 0.18, 0.0, 0.0, 1.0), 0.08, 0.3, 0.2);
	servo(m, 6.1, 2.4, 0.07, 380.0, 640.0);
	servo(m, 9.2, 0.6, 0.05);
	servo(m, 11.0, 0.5, 0.05, 640.0, 520.0);
	// rolling on his treads, with a squeak now and then
	roll(m, 12.6, 8.2, 0.10, 0.6, [](double t) { return clamp01(std::abs(R::robotX(t + 0.05) - R::robotX(t)) / 0.05); });
	for (const double t : {13.4, 15.1, 19.0}) m.put(t, audio::chirp(hz(95), hz(93), 0.12, 0.0, 0.0, 1.0), 0.02, 0.0, 0.2);
	// the discovery: a surprised little beep-boop; kneeling (servos); the can (a clank), the pour (water), the bloom (a shimmer)
	m.put(17.0, audio::chirp(hz(79), hz(86), 0.12, 0.0, 0.0, 1.0), 0.10, 0.1, 0.2);
	m.put(17.18, audio::chirp(hz(84), hz(76), 0.18, 0.0, 0.0, 1.0), 0.10, 0.1, 0.2);
	servo(m, 19.0, 1.8, 0.05, 640.0, 420.0);
	m.put(23.0, audio::knock(820.0, 0.25, 34u), 0.12, 0.2, 0.3);
	for (double t = 24.6; t < 27.8; t += 0.09) m.put(t, audio::noiseBurst(0.18, 0.01, 12.0, 3500.0, 2200.0, 35u + static_cast<unsigned>(t * 30.0)), 0.025, 0.2, 0.3);
	for (int k = 0; k < 14; ++k) m.put(25.5 + 0.25 * k, celesta(84 + (k % 5) * 2.0 + (k / 5) * 12.0, 1.6), 0.03 + 0.003 * k, pan(k, 36), 0.6);
	// the music: a lonely music box in A minor, then the bloom answered in C major, swelling as the crane lifts away
	melody(m, 1.5, 0.6, {{69, 1}, {72, 1}, {76, 2}, {74, 1}, {72, 1}, {71, 2}, {69, 3}}, 0.07, -0.2, celesta);
	chord(m, 2.0, {45, 52, 57}, 9.0, 0.02, 3.0, 3.0);
	melody(m, 13.0, 0.5, {{64, 1}, {67, 1}, {69, 1}, {72, 2}, {71, 1}, {69, 1}, {67, 2}}, 0.08, 0.1, pizz);
	chord(m, 26.6, {48, 55, 60, 64, 67}, 5.5, 0.035, 2.0, 2.5);
	melody(m, 28.4, 0.45, {{72, 1}, {76, 1}, {79, 1}, {84, 3}}, 0.10, 0.0, celesta);
}

// ---- Fireflies -----------------------------------------------------------------------------------------------------------------------------------------------------
inline void fireflies(Mix &m) {
	namespace F = cartoon::fireflies;
	// the night meadow: crickets and a breath of wind
	for (int i = 0; i < 40; ++i) {
		const double t = 26.0 * hash01(i, 41);
		for (int p = 0; p < 4; ++p) m.put(t + 0.05 * p, audio::additive(4200.0 + 300.0 * hash01(i, 42), {{1.0, 1.0, 40.0}}, 0.04, 0.002), 0.010, pan(i, 43, 0.9), 0.3);
	}
	for (double t = 0.0; t < F::kDuration; t += 3.0) m.put(t, audio::noiseBurst(4.0, 1.5, 0.4, 250.0, 380.0, 44u + static_cast<unsigned>(t)), 0.012, 0.0, 0.3);
	// each flash of the two fireflies rings a soft bell (A high, B a fifth below): out of step at first, then together
	const auto peaks = [&](auto glow, double from, double midi, double panv) {
		double a = glow(from), b = glow(from + 0.01);
		for (double t = from + 0.02; t < F::kDuration - 0.5; t += 0.01) {
			const double c = glow(t);
			if (b > a && b >= c && b > 0.7) m.put(t - 0.01, audio::bell(hz(midi), 1.6), 0.035, panv, 0.6);
			a = b;
			b = c;
		}
	};
	peaks([](double t) { return F::glowA(t); }, 0.5, 88, -0.2);
	peaks([](double t) { return F::glowB(t); }, 6.0, 81, 0.2);
	// the swarm wakes (twinkles), the pair spirals up to the moon (a rising harp)
	for (int k = 0; k < 60; ++k) m.put(14.0 + 8.0 * hash01(k, 45), celesta(84 + 2.0 * (k % 6), 0.8), 0.012, pan(k, 46, 0.9), 0.7);
	for (int k = 0; k < 16; ++k) m.put(20.0 + 0.32 * k, audio::pluck(hz(60 + (k % 5) * 2 + (k / 5) * 12), 1.8, 47u + static_cast<unsigned>(k), 0.996), 0.06, -0.3 + 0.04 * k, 0.5);
	chord(m, 7.0, {52, 59, 64}, 12.0, 0.018, 4.0, 3.0);
	chord(m, 19.0, {48, 55, 64, 67}, 6.5, 0.025, 3.0, 3.0);
}

// ---- Newton's Cradle -----------------------------------------------------------------------------------------------------------------------------------------------
inline void newtonsCradle(Mix &m) {
	namespace N = newtons_cradle;
	m.put(0.8, audio::slide(180.0, 150.0, 1.1, 1.5), 0.02, -0.4, 0.2);   // (the hand lifts the ball: a faint creak of the string)
	// every strike: a sharp click, softer as the swing dies, and a bell that steps down the scale
	static const double scale[] = {84, 83, 81, 79, 77, 76, 74, 72, 71, 69, 67, 65, 64, 62};
	int k = 0;
	for (double t = N::kReleaseAt + N::kQuarter; t < N::kDuration - 0.3; t += 2.0 * N::kQuarter, ++k) {
		const double a = std::pow(N::kKeep, k);
		m.put(t, audio::knock(2400.0, 0.03, 50u + static_cast<unsigned>(k)), 0.30 * a, k % 2 == 0 ? -0.15 : 0.15, 0.25);
		m.put(t, audio::additive(3300.0, {{1.0, 1.0, 30.0}, {1.6, 0.4, 40.0}}, 0.08, 0.001), 0.06 * a, 0.0, 0.3);
		if (k % 2 == 0) m.put(t, celesta(scale[std::min(13, k / 2)], 1.4), 0.05 * a, 0.0, 0.6);
	}
	chord(m, 3.0, {48, 55, 64}, 13.0, 0.012, 4.0, 3.0);
}

// ---- Domino Run ----------------------------------------------------------------------------------------------------------------------------------------------------
inline void dominoRun(Mix &m) {
	namespace D = domino_run;
	// the marble rolls in and taps the first
	roll(m, 0.6, D::kStart - 0.6, 0.10, 0.7, [](double t) { return 0.4 + 0.6 * smoothstep(0.6, 2.0, t); });
	m.put(D::kStart, audio::knock(900.0, 0.06, 60u), 0.14, -0.4, 0.15);
	// each domino ticks as it falls on the next, rising in pitch as the wave runs inwards
	const int n = D::dominoCount();
	for (int i = 0; i < n; ++i) {
		const double t = D::startOf(i) + D::kFall;
		const double u = static_cast<double>(i) / std::max(1, n - 1);
		m.put(t, audio::knock(420.0 + 520.0 * u + 40.0 * hash01(i, 61), 0.05, 62u + static_cast<unsigned>(i % 7)), 0.09, pan(i, 63, 0.5), 0.12);
		if (i % 8 == 0) m.put(t, marimba(60 + 2.0 * std::floor(u * 12.0), 0.6), 0.05, 0.0, 0.3);
	}
	// the last falls against the lamp: it switches on - a click, a warm chord, a bell
	const double lamp = D::impactAt();
	m.put(lamp, audio::knock(1300.0, 0.04, 64u), 0.14, 0.0, 0.1);
	m.put(lamp + 0.05, audio::bell(hz(84), 3.0), 0.08, 0.0, 0.6);
	chord(m, lamp, {48, 55, 60, 64, 72}, std::max(1.0, D::kDuration - lamp - 1.0), 0.035, 0.8, 2.0);
	m.put(0.5, audio::bass(hz(36), 2.5), 0.06, 0.0, 0.2);
}

// ---- The Paper Boat ------------------------------------------------------------------------------------------------------------------------------------------------
inline void paperBoat(Mix &m) {
	namespace P = paper_boat;
	// the rain's hiss, easing off with the rain
	for (double t = 0.0; t < P::kDuration; t += 1.0) m.put(t, audio::noiseBurst(1.6, 0.4, 1.0, 5200.0, 3800.0, 70u + static_cast<unsigned>(t)), 0.05 * P::rainAt(t), 0.0, 0.3);
	// every drop the film draws lands with a small plink
	for (int j = 0; j < P::kDrops; ++j) {
		const P::Drop d = P::dropOf(j);
		if (d.t0 >= P::kDuration || !P::dropKept(j, d.t0)) continue;
		m.put(d.t0, audio::chirp(1400.0 + 900.0 * hash01(j, 71), 700.0 + 300.0 * hash01(j, 72), 0.07, 0.0, 0.0, 1.0), 0.05 + 0.04 * hash01(j, 73), clamp01((d.x + 11.0) / 22.0) * 1.6 - 0.8, 0.4);
	}
	// water lapping at the boat; the music: a slow piano in the rain, warming as it stops, a last chord in the sun
	for (double t = 0.5; t < P::kDuration; t += 1.7) m.put(t, audio::noiseBurst(0.8, 0.15, 3.0, 900.0, 500.0, 74u + static_cast<unsigned>(t)), 0.03, 0.2, 0.3);
	melody(m, 1.0, 0.9, {{69, 2}, {72, 1}, {71, 1}, {67, 2}, {69, 2}, {64, 3}, {0, 1}, {65, 1}, {67, 1}, {69, 4}}, 0.06, -0.2,
		   [](double n, double l) { return audio::additive(hz(n), {{1.0, 1.0, 0.9}, {2.0, 0.4, 1.6}, {3.0, 0.15, 2.5}}, l + 1.5, 0.003); });
	chord(m, 2.0, {45, 52, 57}, 10.0, 0.018, 3.0, 3.0);
	chord(m, 14.0, {48, 55, 60, 64}, 9.0, 0.028, 4.0, 3.0);
	melody(m, 17.0, 0.7, {{72, 1}, {76, 1}, {79, 2}, {77, 1}, {76, 1}, {72, 3}}, 0.06, 0.2, celesta);
}

// ---- The Orrery ----------------------------------------------------------------------------------------------------------------------------------------------------
inline void orrery(Mix &m) {
	namespace O = cartoon::orrery;
	// the clockwork: a tick for every tooth of the big gear, a lighter one for the small gear between
	const double tooth = 14.0 / O::kTeethBig;
	for (double t = 0.3; t < O::kDuration; t += tooth) {
		m.put(t, audio::knock(1900.0, 0.03, 80u), 0.05, 0.5, 0.2);
		m.put(t + tooth * 0.5, audio::knock(2600.0, 0.02, 81u), 0.025, 0.55, 0.2);
	}
	for (double t = 0.0; t < O::kDuration; t += 3.0) m.put(t, audio::pad(hz(31), 3.4, 1.0, 1.0), 0.02, 0.0, 0.2);   // (the sun's low hum)
	// a music box waltz in 3/4, the planets passing
	const std::initializer_list<std::pair<double, double>> waltz = {{76, 1}, {79, 1}, {84, 1}, {83, 2}, {79, 1}, {81, 1}, {77, 1}, {74, 1}, {76, 3}, {72, 1}, {74, 1}, {76, 1}, {79, 2}, {77, 1}, {76, 1}, {74, 1}, {71, 1}, {72, 3}};
	melody(m, 1.0, 0.42, waltz, 0.08, -0.1, celesta);
	melody(m, 11.0, 0.42, waltz, 0.07, 0.1, celesta);
	for (int bar = 0; bar < 16; ++bar) {
		const double t = 1.0 + bar * 3.0 * 0.42;
		m.put(t, pizz(bar % 2 == 0 ? 48 : 43, 0.6), 0.06, -0.2, 0.2);
		m.put(t + 0.42, pizz(64, 0.4), 0.03, 0.2, 0.2);
		m.put(t + 0.84, pizz(67, 0.4), 0.03, 0.2, 0.2);
	}
}

// ---- The Dandelion -------------------------------------------------------------------------------------------------------------------------------------------------
inline void dandelion(Mix &m) {
	namespace Dl = cartoon::dandelion;
	// a summer meadow: a breeze, the gust that takes the seeds, grass
	for (double t = 0.0; t < Dl::kDuration; t += 2.4) m.put(t, audio::noiseBurst(3.2, 1.2, 0.5, 400.0, 600.0, 90u + static_cast<unsigned>(t)), 0.03, 0.0, 0.3);
	m.put(Dl::kGust - 0.6, audio::noiseBurst(4.5, 1.2, 0.6, 900.0, 1500.0, 91u), 0.10, -0.3, 0.4);
	// each seed lets go with a tiny high twinkle
	for (int i = 0; i < Dl::kSeeds; ++i) {
		const double t = Dl::releaseOf(i);
		if (t < Dl::kDuration && i % 2 == 0) m.put(t, celesta(91 + 2.0 * (i % 4), 0.7), 0.010, pan(i, 92, 0.8), 0.7);
	}
	// an airy flute and a warm pad in the low golden light
	const auto flute = [](double n, double l) { return audio::chirp(hz(n), hz(n), l + 0.3, 5.0, 0.008, 1.0); };
	melody(m, 1.5, 0.75, {{74, 2}, {76, 1}, {79, 2}, {81, 1}, {79, 3}, {0, 1}, {76, 1}, {74, 1}, {71, 2}, {74, 4}}, 0.05, -0.1, flute);
	melody(m, 14.0, 0.75, {{79, 2}, {81, 1}, {83, 2}, {86, 1}, {83, 3}, {79, 4}}, 0.045, 0.1, flute);
	chord(m, 1.0, {50, 57, 62, 66}, 19.0, 0.02, 4.0, 3.0);
}

// ---- The Candle ----------------------------------------------------------------------------------------------------------------------------------------------------
inline void candle(Mix &m) {
	namespace C = cartoon::candle;
	// a still room; the flame's soft roar and its crackles, more where it flickers hard
	for (double t = 0.0; t < C::kDark; t += 1.0) m.put(t, audio::noiseBurst(1.5, 0.4, 1.0, 600.0, 400.0, 100u + static_cast<unsigned>(t)), 0.020 * C::burn(t), 0.0, 0.2);
	for (int k = 0; k < 70; ++k) {
		const double t = C::kDark * hash01(k, 101);
		if (C::flicker(t) > 1.08) m.put(t, audio::noiseBurst(0.025, 0.001, 120.0, 4500.0, 2500.0, 102u + static_cast<unsigned>(k)), 0.05 * C::burn(t), pan(k, 103, 0.2), 0.15);
	}
	// it dies: sputters, then out with a soft breath; one last chime for the fairy lights
	for (int k = 0; k < 6; ++k) m.put(C::kOut + 0.22 * k, audio::noiseBurst(0.06, 0.002, 40.0, 3000.0, 1800.0, 104u + static_cast<unsigned>(k)), 0.06, 0.0, 0.2);
	m.put(C::kDark - 0.1, audio::noiseBurst(0.6, 0.05, 4.0, 1200.0, 500.0, 105u), 0.05, 0.0, 0.3);
	m.put(C::kDark + 0.8, celesta(88, 2.5), 0.05, 0.3, 0.8);
	// a slow lullaby on a soft cello and a music box
	const auto cello = [](double n, double l) { return audio::pad(hz(n), l, 0.4, 0.8); };
	melody(m, 1.0, 1.1, {{55, 2}, {57, 1}, {59, 1}, {62, 3}, {60, 1}, {59, 2}, {55, 4}}, 0.05, -0.2, cello);
	melody(m, 9.0, 0.55, {{79, 1}, {83, 1}, {86, 2}, {84, 1}, {83, 1}, {79, 2}, {81, 1}, {79, 3}}, 0.05, 0.2, celesta);
	chord(m, 0.5, {43, 50, 55}, 15.5, 0.015, 3.0, 2.5);
}

// ---- Meet Pip ------------------------------------------------------------------------------------------------------------------------------------------------------
inline void meetPip(Mix &m) {
	const auto say = [&](double t, double f0, double f1, double len, double gain = 0.16) { m.put(t, audio::chirp(f0, f1, len, 6.5, 0.015, 1.0), gain, 0.0, 0.15); };
	// hello: a wave and a cheerful "hi-hi!"
	say(0.9, hz(76), hz(83), 0.14);
	say(1.12, hz(79), hz(86), 0.18);
	say(1.9, hz(81), hz(79), 0.30, 0.12);
	// the faces: surprise (a rising "oh!"), sadness (a falling sigh), anger (low growls), joy (a hop and a trill)
	say(3.62, hz(70), hz(88), 0.20);
	say(5.5, hz(76), hz(64), 0.70, 0.12);
	for (int k = 0; k < 3; ++k) say(7.4 + 0.18 * k, hz(55), hz(52), 0.14, 0.12);
	m.put(8.9, audio::boing(300.0, 0.4), 0.10, 0.0, 0.2);
	for (int k = 0; k < 5; ++k) say(9.0 + 0.08 * k, hz(84 + (k % 2) * 3), hz(86 + (k % 2) * 3), 0.07, 0.10);
	// the orb: a shimmer that travels, and Pip's "ooh"
	for (int k = 0; k < 18; ++k) m.put(9.3 + 0.17 * k, celesta(88 + (k % 6) * 2.0, 0.8), 0.025, std::sin(k * 0.7), 0.6);
	say(10.1, hz(67), hz(74), 0.6, 0.10);
	// the walk: a soft pat for every step, and a skip and a "ta-da!" at the end
	for (double t = 12.6 + 1.0 / 3.8; t < 17.0; t += 1.0 / 3.8) m.put(t, audio::knock(260.0, 0.06, 110u), 0.08 * smoothstep(12.6, 13.2, t), 0.0, 0.1);
	m.put(16.9, audio::boing(360.0, 0.35), 0.08, 0.0, 0.2);
	say(17.2, hz(79), hz(84), 0.12);
	say(17.35, hz(84), hz(91), 0.25);
	// the music: a light marimba tune with pizzicato under it, a step-along figure for the walk
	melody(m, 0.4, 0.38, {{72, 1}, {74, 1}, {76, 2}, {79, 1}, {76, 1}, {74, 2}}, 0.07, -0.2, marimba);
	for (int k = 0; k < 12; ++k) m.put(12.6 + 0.38 * k, pizz((k % 4 == 0) ? 48 : ((k % 4 == 2) ? 55 : 60), 0.4), 0.06, 0.2, 0.2);
	melody(m, 12.8, 0.38, {{76, 1}, {79, 1}, {81, 1}, {79, 1}, {76, 2}, {72, 1}, {74, 1}, {76, 4}}, 0.06, -0.1, marimba);
	chord(m, 17.0, {48, 55, 64, 67}, 1.0, 0.025, 0.3, 1.0);
}

}  // namespace scores
}  // namespace cartoon
