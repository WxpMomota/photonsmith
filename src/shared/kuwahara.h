#pragma once
// kuwahara.h -- a painterly filter: the anisotropic Kuwahara filter (Kyprianidis, Kang and Doellner, "Image and Video Abstraction by Anisotropic Kuwahara Filtering",
// Pacific Graphics 2009, with the polynomial sector weights of Kyprianidis, Semmo, Kang and Doellner, "Anisotropic Kuwahara Filtering with Polynomial Weighting
// Functions", TPCG 2010), the filter behind Blender's compositor Kuwahara node (its "Anisotropic" variation). Flat areas become flat strokes of paint, and the strokes
// follow the picture's edges and stripes, so a render reads as an oil or gouache painting while every edge stays sharp.
//
// At each pixel, an ellipse along the local edge direction (from the smoothed structure tensor - how the colour changes round the pixel) is split into eight
// overlapping sectors; the pixel becomes the mean colour of the sectors, each weighted by how even it is (a sector across an edge varies a lot and counts little), so
// the colour is taken from the side of an edge the pixel is on. Our own code from the papers (Blender's is GPL; this project is MIT).
//
// Works on the linear picture (lens_look.h `paint`): the means are of the linear light, so the picture keeps its brightness; the evenness and the edges are measured on
// c / (1 + c), so a bright light does not swamp them and the result does not change with the exposure's scale... much (it is the light's display-like curve).

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include "ppm_text.h"   // parallel_ranges

namespace kuwahara {

struct Params {
	double radius = 6.0;        // the strokes' size: the ellipse's radius in pixels (before the anisotropy stretches it)
	double sharpness = 0.5;     // 0..1: how strongly the evenest sector wins (higher: crisper edges, more contrast between strokes)
	double eccentricity = 1.0;  // 0..2: how much a stroke stretches along an edge (0: round strokes, as the classic filter)
	double uniformity = 4.0;    // the smoothing of the edge directions, in pixels (higher: longer, more even strokes)
};

namespace detail {

// A separable Gaussian blur of an n-channel image, in place.
inline void blur(std::vector<float> &img, int w, int h, int channels, double sigma) {
	if (sigma < 0.3) return;
	const int r = (std::max)(1, static_cast<int>(std::ceil(3.0 * sigma)));
	std::vector<double> k(static_cast<std::size_t>(2 * r + 1));
	double sum = 0.0;
	for (int i = -r; i <= r; ++i) sum += k[static_cast<std::size_t>(i + r)] = std::exp(-0.5 * i * i / (sigma * sigma));
	for (double &x : k) x /= sum;
	std::vector<float> tmp(img.size());
	for (int pass = 0; pass < 2; ++pass) {
		const bool rows = pass == 0;
		const std::vector<float> &src = rows ? img : tmp;
		std::vector<float> &dst = rows ? tmp : img;
		ppm_text::parallel_ranges(static_cast<std::size_t>(h), [&](std::size_t y0, std::size_t y1) {
			for (std::size_t y = y0; y < y1; ++y)
				for (int x = 0; x < w; ++x)
					for (int c = 0; c < channels; ++c) {
						double acc = 0.0;
						for (int i = -r; i <= r; ++i) {
							const int sx = rows ? std::clamp(x + i, 0, w - 1) : x, sy = rows ? static_cast<int>(y) : std::clamp(static_cast<int>(y) + i, 0, h - 1);
							acc += k[static_cast<std::size_t>(i + r)] * src[(static_cast<std::size_t>(sy) * w + sx) * channels + c];
						}
						dst[(y * w + x) * channels + c] = static_cast<float>(acc);
					}
		});
	}
}

}  // namespace detail

// Filters `rgb` (w x h, 3 floats a pixel, linear) in place.
inline void apply(float *rgb, int w, int h, const Params &p) {
	if (p.radius < 0.5 || w < 3 || h < 3) return;
	const std::size_t n = static_cast<std::size_t>(w) * h;
	// the display-like values the evenness and the edges are measured on
	std::vector<float> t(n * 3);
	for (std::size_t i = 0; i < n * 3; ++i) {
		const float c = std::isfinite(rgb[i]) ? (std::max)(0.0f, rgb[i]) : 0.0f;
		t[i] = c / (1.0f + c);
	}
	// The structure tensor (E, F, G: the sums over the channels of fx*fx, fx*fy, fy*fy from a Sobel gradient), smoothed.
	std::vector<float> tensor(n * 3);
	ppm_text::parallel_ranges(static_cast<std::size_t>(h), [&](std::size_t y0, std::size_t y1) {
		for (std::size_t y = y0; y < y1; ++y)
			for (int x = 0; x < w; ++x) {
				const int ym = static_cast<int>(y) > 0 ? static_cast<int>(y) - 1 : 0, yp = (std::min)(h - 1, static_cast<int>(y) + 1);
				const int xm = x > 0 ? x - 1 : 0, xp = (std::min)(w - 1, x + 1);
				const auto at = [&](int sx, int sy, int c) { return static_cast<double>(t[(static_cast<std::size_t>(sy) * w + sx) * 3 + c]); };
				double e = 0.0, f = 0.0, g = 0.0;
				for (int c = 0; c < 3; ++c) {
					const double fx = (at(xp, ym, c) + 2.0 * at(xp, static_cast<int>(y), c) + at(xp, yp, c) - at(xm, ym, c) - 2.0 * at(xm, static_cast<int>(y), c) - at(xm, yp, c)) / 4.0;
					const double fy = (at(xm, yp, c) + 2.0 * at(x, yp, c) + at(xp, yp, c) - at(xm, ym, c) - 2.0 * at(x, ym, c) - at(xp, ym, c)) / 4.0;
					e += fx * fx, f += fx * fy, g += fy * fy;
				}
				float *o = &tensor[(y * w + x) * 3];
				o[0] = static_cast<float>(e), o[1] = static_cast<float>(f), o[2] = static_cast<float>(g);
			}
	});
	detail::blur(tensor, w, h, 3, p.uniformity);

	const double alpha = 1.0 / (std::max)(0.01, p.eccentricity);   // (the papers' alpha: 1 by default; smaller stretches more)
	const double q = 1.0 + 15.0 * std::clamp(p.sharpness, 0.0, 1.0);   // (the papers' q: 8 by default)
	// the sector weights: polynomials in the unit disk, each zero beyond 3 pi / 8 from its sector's axis, all zeta at the centre
	const double zeta = 0.25, zeroCross = 3.0 * 3.14159265358979 / 8.0;
	const double eta = (zeta + std::cos(zeroCross)) / (std::sin(zeroCross) * std::sin(zeroCross));
	const std::vector<float> src(rgb, rgb + n * 3);
	ppm_text::parallel_ranges(static_cast<std::size_t>(h), [&](std::size_t y0, std::size_t y1) {
		for (std::size_t y = y0; y < y1; ++y)
			for (int x = 0; x < w; ++x) {
				const float *T = &tensor[(y * w + x) * 3];
				const double E = T[0], F = T[1], G = T[2];
				const double root = std::sqrt((E - G) * (E - G) + 4.0 * F * F);
				const double l1 = 0.5 * (E + G + root), l2 = 0.5 * (E + G - root);
				double tx = l1 - E, ty = -F;   // along the edge (the minor eigenvector)
				if (tx * tx + ty * ty < 1e-20) tx = 1.0, ty = 0.0;
				const double phi = std::atan2(ty, tx), A = l1 + l2 > 1e-12 ? (l1 - l2) / (l1 + l2) : 0.0;
				const double a = p.radius * std::clamp((alpha + A) / alpha, 0.1, 2.0), b = p.radius * std::clamp(alpha / (alpha + A), 0.1, 2.0);
				const double cp = std::cos(phi), sp = std::sin(phi);
				const int maxX = static_cast<int>(std::sqrt(a * a * cp * cp + b * b * sp * sp)), maxY = static_cast<int>(std::sqrt(a * a * sp * sp + b * b * cp * cp));
				const int step = (std::max)(1, static_cast<int>(std::lround((std::max)(a, b) / 8.0)));   // (a big ellipse is sampled sparsely: about 17 across)
				double W[8] = {}, M[8][3] = {}, S[8][3] = {}, S2[8][3] = {};
				for (int dy = -maxY; dy <= maxY; dy += step)
					for (int dx = -maxX; dx <= maxX; dx += step) {
						double vx = (cp * dx + sp * dy) / a, vy = (-sp * dx + cp * dy) / b;   // (into the unit disk)
						const double r2 = vx * vx + vy * vy;
						if (r2 > 1.0) continue;
						const std::size_t at = (static_cast<std::size_t>(std::clamp(static_cast<int>(y) + dy, 0, h - 1)) * w + std::clamp(x + dx, 0, w - 1)) * 3;
						double wk[8], sum = 0.0;
						for (int half = 0; half < 2; ++half) {
							if (half == 1) {   // (the odd sectors: the even ones turned by 45 degrees)
								const double rx = 0.70710678118654752 * (vx - vy), ry = 0.70710678118654752 * (vx + vy);
								vx = rx, vy = ry;
							}
							const double vxx = zeta - eta * vx * vx, vyy = zeta - eta * vy * vy;
							const double z[4] = {(std::max)(0.0, vy + vxx), (std::max)(0.0, -vx + vyy), (std::max)(0.0, -vy + vxx), (std::max)(0.0, vx + vyy)};
							for (int s = 0; s < 4; ++s) sum += wk[2 * s + half] = z[s] * z[s];
						}
						if (sum <= 0.0) continue;
						const double gauss = std::exp(-0.78 * r2) / sum;
						for (int k = 0; k < 8; ++k) {
							const double wt = wk[k] * gauss;
							if (wt <= 0.0) continue;
							W[k] += wt;
							for (int c = 0; c < 3; ++c) {
								const double tc = t[at + c];
								M[k][c] += wt * src[at + c], S[k][c] += wt * tc, S2[k][c] += wt * tc * tc;
							}
						}
					}
				double out[3] = {0.0, 0.0, 0.0}, total = 0.0;
				for (int k = 0; k < 8; ++k) {
					if (W[k] <= 0.0) continue;
					double var = 0.0;
					for (int c = 0; c < 3; ++c) {
						const double mean = S[k][c] / W[k];
						var += std::fabs(S2[k][c] / W[k] - mean * mean);
					}
					const double wk = 1.0 / (1.0 + std::pow(255.0 * var, 0.5 * q));
					total += wk;
					for (int c = 0; c < 3; ++c) out[c] += wk * M[k][c] / W[k];
				}
				float *o = &rgb[(y * w + x) * 3];
				if (total > 0.0)
					for (int c = 0; c < 3; ++c) o[c] = static_cast<float>(out[c] / total);
			}
	});
}

}  // namespace kuwahara
