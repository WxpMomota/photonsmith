#pragma once
// film_shows.h -- under the sea, round 18: schools that perform, and the sea's songs.
//
//   schoolshow N heart|ring|ball|spiral|arrow|wall at T [for D] [size S] [toward A] [beat BPM] [centre X Y Z]
//   schoolshow N word TEXT at T ...          the Nth `school` (from 1) leaves its swim to make a shape S metres high (3), facing A (the way the camera looks at it from:
//                                            0 from +z), held for D seconds (8) and let go; a word of up to 12 letters (A-Z, 0-9, ! ?) in fish, a letter S high; with
//                                            `beat` the shape pulses in time (a film's tempo)
//   schoolshow N burst at T                  the school bursts outwards in a flash of silver and comes back together
//   choir N at T [for D] [key K]             the school sings: a slow chord of voices from among it (K the root, a MIDI note: 57), the school breathing in and out in time
//   whalesong at T [for D] [from X Y Z]      a whale's song heard from far off - long swoops and moans (from the film's first humpbacks, else from X Y Z)
//
// A show moves the school's worked-out swim (school::School pos and vel) when the film is loaded, so everything that follows the school - its drawing, a shot that
// follows it, the night's sparkle - sees the show.

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <vector>

#include "film_school.h"

namespace cartoon {
namespace shows {

struct Show {
	int school = 1;
	std::string shape = "heart", word;
	double at = 0.0, length = 8.0, size = 3.0, toward = 0.0, beat = 0.0;
	Vec3 centre;
	bool placed = false;   // (`centre X Y Z`: pbrt's, its height above the seabed until lifted)
};
struct Song {
	double at = 0.0, length = 20.0;
	Vec3 from;
	bool placed = false;
	int key = 57;   // (a choir's root)
	int school = 0; // (a choir's school, from 1; 0 a whale's song)
};
inline const std::vector<std::string> &shapes() {
	static const std::vector<std::string> k = {"heart", "ring", "ball", "spiral", "arrow", "wall", "word", "burst"};
	return k;
}

namespace detail {
// A 5 by 7 letter: its rows, top first, each five bits (the left one high).
inline const unsigned char *glyph(char c) {
	static const unsigned char letters[26][7] = {
		{0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}, {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}, {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}, {0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C},
		{0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}, {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}, {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}, {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11},
		{0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}, {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C}, {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}, {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F},
		{0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}, {0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11}, {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10},
		{0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}, {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}, {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}, {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04},
		{0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}, {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}, {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11},
		{0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}, {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}};
	static const unsigned char digits[10][7] = {
		{0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}, {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}, {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}, {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E},
		{0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}, {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}, {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}, {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
		{0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}, {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}};
	static const unsigned char bang[7] = {0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04}, ask[7] = {0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04}, none[7] = {0, 0, 0, 0, 0, 0, 0};
	if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
	if (c >= 'A' && c <= 'Z') return letters[c - 'A'];
	if (c >= '0' && c <= '9') return digits[c - '0'];
	return c == '!' ? bang : c == '?' ? ask : none;
}
}  // namespace detail

// Whether a word can be written in fish (up to 12 of A-Z, 0-9, ! and ?).
inline bool writable(const std::string &w) {
	if (w.empty() || w.size() > 12) return false;
	return std::all_of(w.begin(), w.end(), [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '!' || c == '?'; });
}
// The place of fish i of n in a shape, in its own plane (x across, y up, z towards whoever looks), size 1 its height.
inline Vec3 shapePoint(const Show &s, int i, int n) {
	const double u = (i + 0.5) / n, h1 = hash01(i, 31), h2 = hash01(i, 37);
	if (s.shape == "heart") {
		const double a = 2.0 * kPi * u, fill = 1.0 - 0.45 * h1 * h1;
		const double x = 16.0 * std::pow(std::sin(a), 3.0), y = 13.0 * std::cos(a) - 5.0 * std::cos(2.0 * a) - 2.0 * std::cos(3.0 * a) - std::cos(4.0 * a);
		return Vec3{x, y + 2.0, 0.0} * (fill / 32.0) + Vec3{0.0, 0.0, 0.05 * (h2 - 0.5)};
	}
	if (s.shape == "ring") {
		const double a = 2.0 * kPi * u;
		return {0.5 * std::cos(a) + 0.04 * (h1 - 0.5), 0.5 * std::sin(a) + 0.04 * (h2 - 0.5), 0.05 * (hash01(i, 41) - 0.5)};
	}
	if (s.shape == "ball") {
		const double y = 1.0 - 2.0 * u, r = std::sqrt(std::max(0.0, 1.0 - y * y)), a = 2.39996 * i;   // (spread evenly over the sphere: a golden spiral)
		return Vec3{r * std::cos(a), y, r * std::sin(a)} * (0.5 * (0.92 + 0.08 * h1));
	}
	if (s.shape == "spiral") {
		const double arm = i % 2 == 0 ? 0.0 : kPi, a = 3.0 * kPi * u + arm, r = 0.05 + 0.5 * u;
		return {r * std::cos(a) + 0.03 * (h1 - 0.5), r * std::sin(a) + 0.03 * (h2 - 0.5), 0.04 * (hash01(i, 41) - 0.5)};
	}
	if (s.shape == "arrow") {   // (pointing across, to +x)
		if (u < 0.5) return {-0.9 + 2.4 * u * 1.4, 0.03 * (h1 - 0.5), 0.03 * (h2 - 0.5)};
		const double v = (u - 0.5) * 2.0, side = i % 2 == 0 ? 1.0 : -1.0;
		return {0.8 - 0.45 * v, side * 0.42 * v, 0.03 * (h2 - 0.5)};
	}
	if (s.shape == "wall") return {2.0 * (h1 - 0.5), h2 - 0.5, 0.03 * (hash01(i, 41) - 0.5)};
	// a word: its letters' lit cells, fish shared among them
	std::vector<std::pair<int, int>> cells;
	const int letters = static_cast<int>(s.word.size());
	for (int l = 0; l < letters; ++l) {
		const unsigned char *g = detail::glyph(s.word[static_cast<std::size_t>(l)]);
		for (int row = 0; row < 7; ++row)
			for (int col = 0; col < 5; ++col)
				if (g[row] & (1 << (4 - col))) cells.push_back({l * 6 + col, 6 - row});
	}
	if (cells.empty()) return {};
	const auto &cell = cells[static_cast<std::size_t>(static_cast<long long>(i) * static_cast<long long>(cells.size()) / std::max(1, n)) % cells.size()];
	const double unit = 1.0 / 7.0, width = (letters * 6 - 1) * unit;
	return {cell.first * unit - 0.5 * width + unit * 0.7 * (h1 - 0.5), cell.second * unit - 0.5 + unit * 0.7 * (h2 - 0.5), 0.03 * (hash01(i, 41) - 0.5)};
}

// The school's swim with its shows in it (a school's worked-out pos and vel, see the top). `centres` the shows' middles already made pbrt's.
inline void perform(school::School &sc, const std::vector<Show> &shows, const std::vector<Song> &choirs, int index) {
	if (sc.pos.size() < 3 || sc.count <= 0) return;
	const std::size_t steps = sc.pos.size(), n = static_cast<std::size_t>(sc.count);
	const std::vector<std::vector<Vec3>> swum = sc.pos;
	const auto middleAt = [&](std::size_t k) {
		Vec3 m{0, 0, 0};
		for (const Vec3 &p : swum[std::min(k, steps - 1)]) m = m + p;
		return m * (1.0 / static_cast<double>(n));
	};
	bool moved = false;
	for (const Show &s : shows) {
		if (s.school != index + 1) continue;
		moved = true;
		const std::size_t k0 = std::min(steps - 1, static_cast<std::size_t>(std::max(0.0, s.at / sc.dt)));
		const Vec3 c = s.placed ? s.centre : middleAt(k0);
		const double a = s.toward * kPi / 180.0;
		const Vec3 normal{std::sin(a), 0.0, std::cos(a)}, up{0.0, 1.0, 0.0}, across = normalize(cross(normal, up));   // (left to right as whoever it faces sees it: pbrt's space is left-handed)
		for (std::size_t k = k0; k < steps; ++k) {
			const double t = k * sc.dt, since = t - s.at;
			if (s.shape == "burst") {
				if (since > 9.0) break;
				const double out = 7.0 * (1.0 - std::exp(-since / 0.3)) * std::exp(-since / 2.2) * (1.0 - smoothstep(7.0, 9.0, since));   // (to nothing by its end: it stopped 12 cm out, and the school jumped)
				for (std::size_t i = 0; i < n; ++i) {
					const Vec3 d = swum[k0][i] - c;
					sc.pos[k][i] = sc.pos[k][i] + (length(d) > 1e-6 ? normalize(d) : Vec3{0.0, 1.0, 0.0}) * out;
				}
				continue;
			}
			if (since > s.length + 2.0) break;
			const double b = smoothstep(0.0, 1.8, since) * (1.0 - smoothstep(s.length, s.length + 2.0, since));
			const double pulse = s.beat > 0.0 ? 1.0 + 0.1 * std::pow(std::max(0.0, std::cos(2.0 * kPi * s.beat / 60.0 * since)), 6.0) : 1.0;
			for (std::size_t i = 0; i < n; ++i) {
				const Vec3 p = shapePoint(s, static_cast<int>(i), static_cast<int>(n)) * (s.size * pulse);
				const double ph = 2.0 * kPi * (0.55 * t + hash01(static_cast<int>(i), 43));   // (each fish circling its place a little: never still)
				const Vec3 target = c + across * (p.x + 0.07 * std::cos(ph)) + up * (p.y + 0.07 * std::sin(ph)) + normal * p.z;
				sc.pos[k][i] = lerp(sc.pos[k][i], target, b);
			}
		}
	}
	for (const Song &ch : choirs) {   // a choir: the school breathing in and out with its song
		if (ch.school != index + 1) continue;
		moved = true;
		for (std::size_t k = static_cast<std::size_t>(std::max(0.0, ch.at / sc.dt)); k < steps; ++k) {
			const double since = k * sc.dt - ch.at;
			if (since > ch.length) break;
			const double b = smoothstep(0.0, 1.5, since) * (1.0 - smoothstep(ch.length - 1.5, ch.length, since));
			Vec3 m{0, 0, 0};   // (the middle of where they are now, a show's shape and all: the swim's own middle swung a heart about a place metres off)
			for (const Vec3 &p : sc.pos[k]) m = m + p;
			m = m * (1.0 / static_cast<double>(n));
			const double breathe = 1.0 + 0.18 * b * std::sin(2.0 * kPi * since / 2.5);
			for (std::size_t i = 0; i < n; ++i) sc.pos[k][i] = m + (sc.pos[k][i] - m) * breathe;
		}
	}
	if (!moved) return;
	for (std::size_t k = 0; k < steps; ++k)   // (their velocities from their places again)
		for (std::size_t i = 0; i < n; ++i) {
			const std::size_t a = k == 0 ? 0 : k - 1, b = std::min(steps - 1, k + 1);
			const Vec3 v = (sc.pos[b][i] - sc.pos[a][i]) * (1.0 / ((b - a) * sc.dt));
			sc.vel[k][i] = length(v) > 0.05 ? v : (length(sc.vel[k][i]) > 0.05 ? sc.vel[k][i] : Vec3{0.0, 0.0, 0.3});   // (never still: a fish faces its way)
		}
}

}  // namespace shows
}  // namespace cartoon
