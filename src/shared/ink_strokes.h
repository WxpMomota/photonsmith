#pragma once
// ink_strokes.h -- ink lines that look drawn by hand, for the toon look (toon_look.h), after Blender's Freestyle line styles (source/blender/freestyle: its stroke
// "modifiers"): a line thinner the further it is from the camera (Freestyle's "Distance from Camera" thickness), thick and thin with the way it runs like a broad pen
// held at an angle ("Calligraphy"), wobbling a little as a hand's line does ("Perlin Noise" geometry) - and, a frame at a time, the wobble drawn afresh (`boil`, set by
// the frame's seed: the shimmer of hand-drawn lines). Our own code from the ideas, working on the picture's edge map rather than on strokes.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace ink_strokes {

struct Params {
	double depthThin = 0.0;   // 0..1: how much further lines are thinner (1: as the distance's square root)
	double wobble = 0.0;      // how far a line wanders, in pixels at a picture 1080 high (0: straight)
	int seed = 0;             // the wobble's pattern (a film's frames change it when the lines boil)
	double pen = -1.0;        // the pen's angle in degrees (a line along it is thinnest); negative: no pen
	double penAmount = 0.6;   // 0..1: how thin a line along the pen gets
	bool active() const { return depthThin > 0.0 || wobble > 0.0 || pen >= 0.0; }
};

namespace detail {
inline double hash01(int x, int y, int seed) {
	unsigned h = static_cast<unsigned>(x) * 73856093u ^ static_cast<unsigned>(y) * 19349663u ^ static_cast<unsigned>(seed) * 83492791u;
	h ^= h >> 13, h *= 0x5bd1e995u, h ^= h >> 15;
	return (h & 0xFFFFFF) / 16777216.0;
}
inline double noise(double x, double y, int seed) {   // smooth value noise, 0..1
	const int ix = static_cast<int>(std::floor(x)), iy = static_cast<int>(std::floor(y));
	const double u = x - ix, v = y - iy, su = u * u * (3 - 2 * u), sv = v * v * (3 - 2 * v);
	const double a = hash01(ix, iy, seed), b = hash01(ix + 1, iy, seed), c = hash01(ix, iy + 1, seed), d = hash01(ix + 1, iy + 1, seed);
	return (a + (b - a) * su) + ((c + (d - c) * su) - (a + (b - a) * su)) * sv;
}
}  // namespace detail

// The edge map moved by a smooth wander (each pixel takes the edge from a little way off), so straight lines wobble.
inline std::vector<float> wobbled(const std::vector<float> &edges, int w, int h, const Params &p) {
	if (p.wobble <= 0.0) return edges;
	const double amount = p.wobble * h / 1080.0, scale = 40.0 * h / 1080.0;   // (a wave about 40 pixels long at 1080)
	std::vector<float> out(edges.size(), 0.0f);
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x) {
			const double dx = amount * 2.0 * (detail::noise(x / scale, y / scale, p.seed * 2 + 1) - 0.5), dy = amount * 2.0 * (detail::noise(x / scale, y / scale, p.seed * 2 + 2) - 0.5);
			const int sx = std::clamp(static_cast<int>(std::lround(x + dx)), 0, w - 1), sy = std::clamp(static_cast<int>(std::lround(y + dy)), 0, h - 1);
			out[static_cast<std::size_t>(y) * w + x] = edges[static_cast<std::size_t>(sy) * w + sx];
		}
	return out;
}

// How wide each edge pixel's line is, as a share of the style's width: thinner far off (against the picture's middle distance) and along the pen.
inline std::vector<float> widths(const std::vector<float> &edges, const std::vector<float> &depth, int w, int h, const Params &p) {
	const std::size_t n = static_cast<std::size_t>(w) * h;
	std::vector<float> f(n, 1.0f);
	if (p.depthThin > 0.0) {
		std::vector<float> d;
		for (std::size_t i = 0; i < n; ++i)
			if (edges[i] > 0.0f && depth[i] > 0.0f) d.push_back(depth[i]);
		if (!d.empty()) {
			std::nth_element(d.begin(), d.begin() + static_cast<std::ptrdiff_t>(d.size() / 2), d.end());
			const double mid = d[d.size() / 2];
			for (std::size_t i = 0; i < n; ++i)
				if (edges[i] > 0.0f && depth[i] > 0.0f) f[i] = static_cast<float>(std::clamp(std::pow(mid / depth[i], 0.5 * p.depthThin), 0.35, 1.8));
		}
	}
	if (p.pen >= 0.0) {
		// which way each line runs: the structure tensor of the edge map's gradient over a 5 x 5 window (at a thin line's middle the gradient itself cancels: the two
		// sides point opposite ways, but their tensors add up), whose main direction runs across the line
		std::vector<float> gx(n, 0.0f), gy(n, 0.0f);
		for (int y = 1; y + 1 < h; ++y)
			for (int x = 1; x + 1 < w; ++x) {
				const auto e = [&](int xx, int yy) { return static_cast<double>(edges[static_cast<std::size_t>(yy) * w + xx]); };
				gx[static_cast<std::size_t>(y) * w + x] = static_cast<float>(e(x + 1, y - 1) + 2 * e(x + 1, y) + e(x + 1, y + 1) - e(x - 1, y - 1) - 2 * e(x - 1, y) - e(x - 1, y + 1));
				gy[static_cast<std::size_t>(y) * w + x] = static_cast<float>(e(x - 1, y + 1) + 2 * e(x, y + 1) + e(x + 1, y + 1) - e(x - 1, y - 1) - 2 * e(x, y - 1) - e(x + 1, y - 1));
			}
		const double a = p.pen * 3.14159265358979 / 180.0, nx = std::cos(a), ny = -std::sin(a);   // (the pen's nib on the screen; rows go down)
		for (int y = 2; y + 2 < h; ++y)
			for (int x = 2; x + 2 < w; ++x) {
				const std::size_t i = static_cast<std::size_t>(y) * w + x;
				if (edges[i] <= 0.0f) continue;
				double jxx = 0.0, jxy = 0.0, jyy = 0.0;
				for (int dy = -2; dy <= 2; ++dy)
					for (int dx = -2; dx <= 2; ++dx) {
						const std::size_t j = static_cast<std::size_t>(y + dy) * w + x + dx;
						jxx += gx[j] * gx[j], jxy += gx[j] * gy[j], jyy += gy[j] * gy[j];
					}
				if (jxx + jyy < 1e-9) continue;
				const double across = 0.5 * std::atan2(2.0 * jxy, jxx - jyy);   // (the direction across the line)
				const double meets = std::fabs(std::cos(across) * nx + std::sin(across) * ny);   // 1: the line runs across the nib (thick), 0: along it (thin)
				f[i] = static_cast<float>(f[i] * ((1.0 - p.penAmount) + p.penAmount * meets));
			}
	}
	return f;
}

// The edge map drawn as round strokes, each edge pixel's `radius * widths[i]` pixels, into `ink` (0..1, the darker kept).
inline void draw(std::vector<float> &ink, const std::vector<float> &edges, const std::vector<float> &widths, int w, int h, double radius) {
	double most = 0.0;
	for (std::size_t i = 0; i < edges.size(); ++i)
		if (edges[i] > 0.0f) most = std::max(most, static_cast<double>(widths[i]));
	const int reach = static_cast<int>(std::ceil(radius * most + 0.5));
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x) {
			const std::size_t i = static_cast<std::size_t>(y) * w + x;
			if (edges[i] <= 0.0f) continue;
			const double r = std::max(0.6, radius * widths[i]);
			for (int dy = -reach; dy <= reach; ++dy)
				for (int dx = -reach; dx <= reach; ++dx) {
					const int xx = x + dx, yy = y + dy;
					if (xx < 0 || yy < 0 || xx >= w || yy >= h) continue;
					const double cover = edges[i] * std::clamp(r + 0.5 - std::sqrt(static_cast<double>(dx * dx + dy * dy)), 0.0, 1.0);
					float &m = ink[static_cast<std::size_t>(yy) * w + xx];
					if (cover > m) m = static_cast<float>(cover);
				}
		}
}

}  // namespace ink_strokes
