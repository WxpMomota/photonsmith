#pragma once
// print_look.h -- looks of a picture printed or painted on paper, or shown from an old film: a comic book's halftone dots, a watercolour on paper, a 1930s cartoon's
// worn print. Made last of all, on the finished picture as it is shown (sRGB, 0..1: after the tone map, the toon look and the marks), because each is about what is seen -
// dots that cover a share of the paper, pigment that darkens what is under it - and not about the light. Our own code from the ideas in Blender's compositor setups.
//
// Their words go in the picture's look line (lens_look.h `# @rt-look`, a film's cameras file `look`):
//   halftone SIZE      the picture as four inks of dots on paper (cyan, magenta, yellow and black, each on a screen at its own angle, the dots growing with the ink),
//                      SIZE the dots' spacing in pixels at a picture 1080 high (0 off); `halftone-mix M` (0..1) how much of it over the picture.
//   watercolor W       a watercolour (after Bousseau et al., "Interactive watercolor rendering with temporal coherence and abstraction", NPAR 2006): the colours softly
//                      bled, pigment pooled darker at the edges of shapes, grains of pigment and its uneven flow, on the paper's texture; W how much (0..2).
//   vintage V          an old film print: grain, the light flickering, the frame weaving, dust and scratches, softer corners and a sepia cast (`vintage-tone T`, 0..1, 0.5);
//                      V how much (0..2). `seed N` is the frame's (a film writes one into every frame, so the grain and the dust change from frame to frame).

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "ppm_text.h"   // parallel_ranges

namespace print_look {

struct Params {
	double halftone = 0.0, halftoneMix = 1.0;
	double watercolor = 0.0;
	double vintage = 0.0, vintageTone = 0.5;
	int seed = 0;
	bool active() const { return halftone > 0.0 || watercolor > 0.0 || vintage > 0.0; }
};

namespace detail {

inline std::uint32_t hash(std::uint32_t x) {   // (lowbias32)
	x ^= x >> 16, x *= 0x7feb352dU, x ^= x >> 15, x *= 0x846ca68bU, x ^= x >> 16;
	return x;
}
inline double unit(std::uint32_t a, std::uint32_t b, std::uint32_t c = 0) { return hash(a * 0x9E3779B1U ^ hash(b + 0x632BE5ABU * hash(c + 1))) / 4294967296.0; }

// Smooth value noise at (x, y) in cells, 0..1.
inline double noise(double x, double y, std::uint32_t seed) {
	const double fx = std::floor(x), fy = std::floor(y), tx = x - fx, ty = y - fy;
	const auto at = [&](int i, int j) { return unit(static_cast<std::uint32_t>(static_cast<int>(fx) + i), static_cast<std::uint32_t>(static_cast<int>(fy) + j), seed); };
	const double sx = tx * tx * (3.0 - 2.0 * tx), sy = ty * ty * (3.0 - 2.0 * ty);
	return (at(0, 0) * (1 - sx) + at(1, 0) * sx) * (1 - sy) + (at(0, 1) * (1 - sx) + at(1, 1) * sx) * sy;
}
// Fractal noise: `octaves` of it, each half the size and half as strong, 0..1.
inline double fractal(double x, double y, int octaves, std::uint32_t seed) {
	double sum = 0.0, amp = 1.0, total = 0.0;
	for (int o = 0; o < octaves; ++o, x *= 2.0, y *= 2.0, amp *= 0.5) sum += amp * noise(x, y, seed + static_cast<std::uint32_t>(o) * 101U), total += amp;
	return sum / total;
}

// A box blur of half-width r along the rows and then the columns (three channels), in place.
inline void blur(std::vector<float> &img, int w, int h, int r) {
	if (r <= 0) return;
	std::vector<float> tmp(img.size());
	for (int pass = 0; pass < 2; ++pass) {
		const bool rows = pass == 0;
		const int lines = rows ? h : w, len = rows ? w : h;
		ppm_text::parallel_ranges(static_cast<std::size_t>(lines), [&](std::size_t b, std::size_t e) {
			for (std::size_t l = b; l < e; ++l)
				for (int c = 0; c < 3; ++c) {
					const auto idx = [&](int i) {
						const int k = std::clamp(i, 0, len - 1);
						return rows ? (l * static_cast<std::size_t>(w) + static_cast<std::size_t>(k)) * 3 + c : (static_cast<std::size_t>(k) * w + l) * 3 + c;
					};
					double sum = 0.0;
					for (int i = -r; i <= r; ++i) sum += img[idx(i)];
					for (int i = 0; i < len; ++i) {
						tmp[idx(i)] = static_cast<float>(sum / (2 * r + 1));
						sum += img[idx(i + r + 1)] - img[idx(i - r)];
					}
				}
		});
		img.swap(tmp);
	}
}

// A pigment of density d over colour c (Bousseau et al.'s colour modification): d above 1 darkens as more pigment would, d below 1 lightens, white stays white.
inline double pigment(double c, double d) { return std::clamp(c - (c - c * c) * (d - 1.0), 0.0, 1.0); }

}  // namespace detail

// The halftone (`halftone SIZE`): the picture separated into cyan, magenta, yellow and black (the dark tones' grey taken out of the other three into black), each printed as
// round dots on a screen at its own angle (15, 75, 0 and 45 degrees, as a printer turns them so they do not beat), a dot's area its ink's share, on warm paper.
inline void halftone(float *rgb, int w, int h, double size, double mix) {
	const double s = std::max(3.0, size * h / 1080.0);
	const double angles[4] = {15.0, 75.0, 0.0, 45.0};   // C M Y K
	const double paper[3] = {0.98, 0.96, 0.91};
	ppm_text::parallel_ranges(static_cast<std::size_t>(h), [&](std::size_t y0, std::size_t y1) {
		for (std::size_t y = y0; y < y1; ++y)
			for (int x = 0; x < w; ++x) {
				float *p = rgb + (y * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)) * 3;
				const double c = 1.0 - std::clamp<double>(p[0], 0, 1), m = 1.0 - std::clamp<double>(p[1], 0, 1), ye = 1.0 - std::clamp<double>(p[2], 0, 1);
				// black only for the darker tones (a printer's grey component replacement from a third of the way down: light greys stay coloured dots, not dirt)
				const double k = std::max(0.0, std::min({c, m, ye}) - 0.35) / 0.65;
				const double ink[4] = {(c - k) / std::max(1e-6, 1 - k), (m - k) / std::max(1e-6, 1 - k), (ye - k) / std::max(1e-6, 1 - k), k};
				double cover[4];
				for (int q = 0; q < 4; ++q) {
					const double a = angles[q] * 3.14159265358979 / 180.0, px = x + 0.5, py = y + 0.5;
					const double u = (px * std::cos(a) + py * std::sin(a)) / s, v = (-px * std::sin(a) + py * std::cos(a)) / s;
					const double du = u - std::floor(u) - 0.5, dv = v - std::floor(v) - 0.5, dist = std::sqrt(du * du + dv * dv) * s;
					const double radius = s * std::sqrt(std::clamp(ink[q], 0.0, 1.0) / 3.14159265358979);   // (the dot's area the ink's share of the cell)
					cover[q] = std::clamp(radius + 0.5 - dist, 0.0, 1.0) * std::min(1.0, 2.0 * radius);   // (a dot under a pixel fades out: no ink, no dot)
				}
				// the deepest black solid, as a comic's line art and spot blacks are printed: the ink lines stay lines
				const double solid = std::clamp((k - 0.75) / 0.15, 0.0, 1.0);
				cover[3] = std::max(cover[3], solid * solid * (3.0 - 2.0 * solid));
				// the inks on the paper: each takes away its colour (cyan the red, ...), black all of them
				const double printed[3] = {paper[0] * (1 - cover[0]) * (1 - cover[3]), paper[1] * (1 - cover[1]) * (1 - cover[3]), paper[2] * (1 - cover[2]) * (1 - cover[3])};
				for (int ch = 0; ch < 3; ++ch) p[ch] = static_cast<float>(p[ch] + (printed[ch] - p[ch]) * mix);
			}
	});
}

// The watercolour (`watercolor W`): the colours bled a little into each other (a soft blur, more for more W), then pigment of a density that varies - darker where the
// colour changes (pigment pools at the edge of a wash), in the pigment's grains (fine noise) and its uneven flow (wide, soft noise) - and the paper's texture under it.
inline void watercolor(float *rgb, int w, int h, double amount) {
	const std::size_t n = static_cast<std::size_t>(w) * h;
	const double scale = h / 1080.0;
	std::vector<float> img(rgb, rgb + n * 3), wide = img;
	detail::blur(img, w, h, std::max(1, static_cast<int>(std::lround(2.0 * scale * amount))));
	detail::blur(wide, w, h, std::max(2, static_cast<int>(std::lround(7.0 * scale))));
	const std::uint32_t paperSeed = 7919U, flowSeed = 104729U;   // (the paper and the wash's flow stay put from frame to frame, as on one sheet)
	ppm_text::parallel_ranges(static_cast<std::size_t>(h), [&](std::size_t y0, std::size_t y1) {
		for (std::size_t y = y0; y < y1; ++y)
			for (int x = 0; x < w; ++x) {
				const std::size_t i = y * static_cast<std::size_t>(w) + static_cast<std::size_t>(x);
				// the edge of a wash: how far the colour here is from its surroundings
				double edge = 0.0;
				for (int c = 0; c < 3; ++c) edge += std::abs(img[3 * i + c] - wide[3 * i + c]);
				edge = std::min(1.0, edge * 2.5);
				const double fx = x / scale, fy = y / scale;   // (the textures' sizes in pixels at 1080)
				const double grain = detail::fractal(fx / 2.5, fy / 2.5, 2, paperSeed) - 0.5;
				const double flow = detail::fractal(fx / 90.0, fy / 90.0, 3, flowSeed) - 0.5;
				const double paperTex = detail::fractal(fx / 6.0, fy / 6.0, 3, paperSeed + 17U) - 0.5;
				const double d = 1.0 + amount * (0.55 * edge + 0.45 * grain + 0.5 * flow);
				for (int c = 0; c < 3; ++c) {
					double v = detail::pigment(img[3 * i + c], d);
					v *= 1.0 + 0.12 * amount * paperTex;      // the paper's tooth
					v = v * (1.0 - 0.06 * amount) + 0.06 * amount * (c == 2 ? 0.92 : 0.98);   // (a little of the paper's warm white through the wash)
					rgb[3 * i + c] = static_cast<float>(std::clamp(v, 0.0, 1.0));
				}
			}
	});
}

// The old film (`vintage V`): the frame weaving (moved a fraction of a pixel to a couple of pixels, smoothly from frame to frame), toned towards sepia, its light
// flickering, grain, softer darker corners, and a frame's dust (dark specks) and scratches (thin light lines down the frame, a few frames long).
inline void vintage(float *rgb, int w, int h, double amount, double tone, int seed) {
	const std::size_t n = static_cast<std::size_t>(w) * h;
	const double scale = h / 1080.0;
	const std::uint32_t frame = static_cast<std::uint32_t>(seed);
	// the weave: a smooth wander (noise along the frames)
	const double wx = (detail::noise(seed * 0.35, 0.5, 11U) - 0.5) * 2.0 * amount * scale, wy = (detail::noise(seed * 0.35, 7.5, 13U) - 0.5) * 3.0 * amount * scale;
	std::vector<float> src(rgb, rgb + n * 3);
	const double flicker = 1.0 + 0.06 * amount * (detail::unit(frame, 3U) - 0.5) * 2.0;
	const double sepia[3] = {1.07, 0.98, 0.80};
	// this frame's dust: specks at random places, a few pixels across
	struct Speck {
		double x, y, r, dark;
	};
	std::vector<Speck> specks;
	const int count = static_cast<int>(std::lround(6.0 * amount * detail::unit(frame, 5U)));
	for (int k = 0; k < count; ++k)
		specks.push_back({detail::unit(frame, 20U, k) * w, detail::unit(frame, 21U, k) * h, (0.8 + 2.5 * detail::unit(frame, 22U, k)) * scale, 0.5 + 0.4 * detail::unit(frame, 23U, k)});
	// scratches: each lasts a few frames (its seed is the frame's group of six), drifting a little
	struct Scratch {
		double x, alpha;
	};
	std::vector<Scratch> scratches;
	const std::uint32_t group = frame / 6U;
	for (int k = 0; k < 2; ++k)
		if (detail::unit(group, 30U, k) < 0.45 * amount)
			scratches.push_back({(detail::unit(group, 31U, k) + 0.002 * (frame % 6U)) * w, 0.12 + 0.12 * detail::unit(frame, 32U, k)});
	const double cx = 0.5 * w, cy = 0.5 * h, halfDiag2 = cx * cx + cy * cy;
	ppm_text::parallel_ranges(static_cast<std::size_t>(h), [&](std::size_t y0, std::size_t y1) {
		for (std::size_t y = y0; y < y1; ++y)
			for (int x = 0; x < w; ++x) {
				// the weave: the picture sampled a little off (bilinear, the edge repeated)
				const double sx = std::clamp(x - wx, 0.0, w - 1.0), sy = std::clamp(static_cast<double>(y) - wy, 0.0, h - 1.0);
				const int x0 = static_cast<int>(sx), yi = static_cast<int>(sy), x1 = std::min(w - 1, x0 + 1), y1i = std::min(h - 1, yi + 1);
				const double ax = sx - x0, ay = sy - yi;
				double c[3];
				for (int ch = 0; ch < 3; ++ch) {
					const auto at = [&](int qx, int qy) { return static_cast<double>(src[(static_cast<std::size_t>(qy) * w + qx) * 3 + ch]); };
					c[ch] = (1 - ay) * ((1 - ax) * at(x0, yi) + ax * at(x1, yi)) + ay * ((1 - ax) * at(x0, y1i) + ax * at(x1, y1i));
				}
				// the tone: towards a sepia print of its brightness
				const double lum = 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2];
				const double t = std::clamp(tone * std::min(1.0, amount), 0.0, 1.0);
				for (int ch = 0; ch < 3; ++ch) c[ch] += (lum * sepia[ch] - c[ch]) * t;
				// the flicker, the grain, the corners
				const double grain = (detail::unit(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), frame * 7919U + 1U) - 0.5) * 0.16 * amount;
				const double px = x + 0.5 - cx, py = y + 0.5 - cy, r2 = (px * px + py * py) / halfDiag2;
				const double corner = 1.0 - 0.45 * std::min(1.0, amount) * r2 * r2;
				double dust = 1.0;
				for (const Speck &s : specks) {
					const double d = std::hypot(x + 0.5 - s.x, y + 0.5 - s.y);
					if (d < s.r + 1.0) dust = std::min(dust, 1.0 - s.dark * std::clamp(s.r + 0.5 - d, 0.0, 1.0));
				}
				double scratch = 0.0;
				for (const Scratch &s : scratches) scratch = std::max(scratch, s.alpha * std::clamp(1.0 - std::abs(x + 0.5 - s.x) / std::max(0.6, 0.8 * scale), 0.0, 1.0));
				for (int ch = 0; ch < 3; ++ch) {
					double v = c[ch] * flicker * corner + grain;
					v = v * dust;
					v += (1.0 - v) * scratch;
					rgb[(y * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)) * 3 + ch] = static_cast<float>(std::clamp(v, 0.0, 1.0));
				}
			}
	});
}

// All of them that `p` asks for, on the shown picture (sRGB 0..1, w x h x 3), in place: the paint first (a watercolour), then the print (the halftone), then the film.
inline void apply(float *rgb, int w, int h, const Params &p) {
	if (!p.active() || w < 4 || h < 4) return;
	if (p.watercolor > 0.0) watercolor(rgb, w, h, p.watercolor);
	if (p.halftone > 0.0) halftone(rgb, w, h, p.halftone, p.halftoneMix);
	if (p.vintage > 0.0) vintage(rgb, w, h, p.vintage, p.vintageTone, p.seed);
}

}  // namespace print_look
