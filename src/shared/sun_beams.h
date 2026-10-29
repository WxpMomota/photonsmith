#pragma once
// sun_beams.h -- god rays in the picture: the light of the sun (or a lamp) streaking out across the frame from where it is, as Blender's compositor Sun Beams node makes
// them - the bright parts of the picture smeared along the lines through the light's place on the screen, fading with distance, added on top. Where something dark
// stands in front of the light (a tree's leaves, a window's frame), its gaps send out the beams and the leaves the shadows between them, at the cost of a blur rather
// than of tracing light through a mist. Our own code from the idea.
//
// The source is where the light is on the screen (0..1 across and down, it may be outside the picture: the sun just out of the frame); without one, the middle of the
// brightest light in the picture. Made at half the picture's size, on the linear picture (lens_look.h `sunbeams`).

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include "ppm_text.h"   // parallel_ranges

namespace sun_beams {

struct Params {
	double strength = 0.4;      // 0..2: how much of the bright light goes into the beams
	double length = 0.35;       // how long the beams are, as a share of the picture's larger side (Blender's Ray Length)
	double threshold = 1.0;     // linear luminance a pixel must pass to send out beams
	bool placed = false;        // the source given (x, y), else found
	double x = 0.5, y = 0.5;
};

inline void apply(float *rgb, int w, int h, Params p) {
	if (p.strength <= 0.0 || w < 4 || h < 4) return;
	const int sw = w / 2, sh = h / 2;
	std::vector<float> bright(static_cast<std::size_t>(sw) * sh * 3, 0.0f);
	double cx = 0.0, cy = 0.0, total = 0.0;
	for (int y = 0; y < sh; ++y)
		for (int x = 0; x < sw; ++x) {
			double c[3] = {0.0, 0.0, 0.0};
			for (int dy = 0; dy < 2; ++dy)
				for (int dx = 0; dx < 2; ++dx)
					for (int k = 0; k < 3; ++k) c[k] += rgb[(static_cast<std::size_t>(2 * y + dy) * w + 2 * x + dx) * 3 + k] / 4.0;
			const double lum = 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2], over = lum > p.threshold ? (lum - p.threshold) / lum : 0.0;
			// (the light above the threshold, in its colour, brought into 0..1 as v / (1 + v): the node works on a display picture, and a sun's light, a hundred times
			// the sky's, would otherwise flood the frame white)
			const double v = lum * over, squeeze = v > 0.0 ? 1.0 / (1.0 + v) : 0.0;
			for (int k = 0; k < 3; ++k) bright[(static_cast<std::size_t>(y) * sw + x) * 3 + k] = static_cast<float>(std::isfinite(c[k]) ? c[k] * over * squeeze : 0.0);
			const double e = lum * over;
			if (std::isfinite(e)) total += e, cx += e * (x + 0.5) / sw, cy += e * (y + 0.5) / sh;
		}
	if (!p.placed) {
		if (total <= 0.0) return;   // (nothing bright: no beams)
		p.x = cx / total, p.y = cy / total;
	}
	// each pixel gathers the bright light on the line from it towards the source, as far as the beams reach (so a pixel further off gets none, and one nearer
	// gets the source's light spread over the stretch: fainter with the distance)
	const double sx = p.x * sw, sy = p.y * sh, reach = p.length * std::max(sw, sh);
	const int steps = 40;
	std::vector<float> beams(bright.size(), 0.0f);
	ppm_text::parallel_ranges(static_cast<std::size_t>(sh), [&](std::size_t y0, std::size_t y1) {
		for (std::size_t y = y0; y < y1; ++y)
			for (int x = 0; x < sw; ++x) {
				const double dx = sx - (x + 0.5), dy = sy - (y + 0.5), dist = std::hypot(dx, dy), span = dist > 1e-9 ? std::min(1.0, reach / dist) : 0.0;
				double acc[3] = {0.0, 0.0, 0.0}, weights = 0.0;
				for (int i = 0; i < steps; ++i) {
					const double u = span * (i + 0.5) / steps, wt = 1.0;
					const int qx = static_cast<int>(x + 0.5 + dx * u), qy = static_cast<int>(y + 0.5 + dy * u);
					weights += wt;
					if (qx < 0 || qy < 0 || qx >= sw || qy >= sh) continue;
					for (int k = 0; k < 3; ++k) acc[k] += wt * bright[(static_cast<std::size_t>(qy) * sw + qx) * 3 + k];
				}
				const double fade = reach > 0.0 ? std::pow(std::max(0.0, 1.0 - dist / reach), 2.0) : 0.0;   // (out to the beams' reach, no further)
				for (int k = 0; k < 3; ++k) beams[(y * sw + x) * 3 + k] = static_cast<float>(fade * acc[k] / weights);
			}
	});
	ppm_text::parallel_ranges(static_cast<std::size_t>(h), [&](std::size_t y0, std::size_t y1) {
		for (std::size_t y = y0; y < y1; ++y)
			for (int x = 0; x < w; ++x) {   // (the half-size beams laid back on, smoothly)
				const double fx = std::clamp((x + 0.5) / 2.0 - 0.5, 0.0, sw - 1.0), fy = std::clamp((y + 0.5) / 2.0 - 0.5, 0.0, sh - 1.0);
				const int x0 = static_cast<int>(fx), y0i = static_cast<int>(fy), x1 = std::min(sw - 1, x0 + 1), y1i = std::min(sh - 1, y0i + 1);
				const double ax = fx - x0, ay = fy - y0i;
				for (int k = 0; k < 3; ++k) {
					const auto at = [&](int qx, int qy) { return static_cast<double>(beams[(static_cast<std::size_t>(qy) * sw + qx) * 3 + k]); };
					const double v = (1 - ay) * ((1 - ax) * at(x0, y0i) + ax * at(x1, y0i)) + ay * ((1 - ax) * at(x0, y1i) + ax * at(x1, y1i));
					rgb[(y * w + x) * 3 + k] += static_cast<float>(p.strength * v);
				}
			}
	});
}

}  // namespace sun_beams
