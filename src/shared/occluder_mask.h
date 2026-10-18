#pragma once
// occluder_mask.h -- the mask of "what is in front" for Render into the photo: a railing, a branch or a post that stands between the camera and an object put into the photo, which must hide it.
//
// A mask is w x h bytes, 0 (nothing in front: the object shows) to 255 (in front: the photo's own pixel stays). The user paints it with a brush, selects a region of the photo by colour (the
// magic wand: a branch is a few similar greens), erases, and the edge is feathered a little so that a pasted object meets the thing in front of it without a hard staircase. Std-only; used by the
// Scene Builder's painting window (qt_gui/scene_builder_occluder.cpp) and the compositor (photo_composite.h), and unit tested.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace occluder_mask {

inline bool any(const std::vector<std::uint8_t>& mask) {
	for (const std::uint8_t v : mask)
		if (v != 0) return true;
	return false;
}

// A round brush: every pixel whose middle is within `radius` of (cx, cy) becomes 255, or 0 when erasing.
inline void paintDisc(std::vector<std::uint8_t>& mask, int w, int h, double cx, double cy, double radius, bool erase) {
	if (w < 1 || h < 1 || mask.size() != static_cast<std::size_t>(w) * h || !(radius > 0.0)) return;
	const int x0 = std::max(0, static_cast<int>(std::floor(cx - radius))), x1 = std::min(w - 1, static_cast<int>(std::ceil(cx + radius)));
	const int y0 = std::max(0, static_cast<int>(std::floor(cy - radius))), y1 = std::min(h - 1, static_cast<int>(std::ceil(cy + radius)));
	const double r2 = radius * radius;
	for (int y = y0; y <= y1; ++y)
		for (int x = x0; x <= x1; ++x) {
			const double dx = x + 0.5 - cx, dy = y + 0.5 - cy;
			if (dx * dx + dy * dy <= r2) mask[static_cast<std::size_t>(y) * w + x] = erase ? 0 : 255;
		}
}

// A stroke from one mouse position to the next: discs along the line (no gaps however fast the mouse moved).
inline void paintStroke(std::vector<std::uint8_t>& mask, int w, int h, double x0, double y0, double x1, double y1, double radius, bool erase) {
	const double length = std::hypot(x1 - x0, y1 - y0);
	const int steps = std::max(1, static_cast<int>(std::ceil(length / std::max(0.5, radius * 0.4))));
	for (int i = 0; i <= steps; ++i) {
		const double t = static_cast<double>(i) / steps;
		paintDisc(mask, w, h, x0 + (x1 - x0) * t, y0 + (y1 - y0) * t, radius, erase);
	}
}

// The magic wand: from the pixel (x, y) of the picture (`rgb`, w x h x 3, 8-bit) the region of pixels joined to it (4 neighbours) whose colour is within `tolerance` (0..255, the largest difference in any
// channel) of the pixel's own colour, set to 255 (or 0 when erasing). Returns the number of pixels changed. `maxPixels` stops a runaway selection (a tolerance that takes in the whole sky).
inline int floodSelect(const unsigned char* rgb, int w, int h, int x, int y, int tolerance, std::vector<std::uint8_t>& mask, bool erase, int maxPixels = 0) {
	if (!rgb || w < 1 || h < 1 || x < 0 || y < 0 || x >= w || y >= h || mask.size() != static_cast<std::size_t>(w) * h) return 0;
	const std::size_t n = static_cast<std::size_t>(w) * h;
	if (maxPixels <= 0) maxPixels = static_cast<int>(n);
	const unsigned char* seed = &rgb[(static_cast<std::size_t>(y) * w + x) * 3];
	const int sr = seed[0], sg = seed[1], sb = seed[2];
	std::vector<char> seen(n, 0);
	std::vector<std::size_t> stack;
	stack.push_back(static_cast<std::size_t>(y) * w + x);
	seen[stack.back()] = 1;
	int changed = 0;
	while (!stack.empty() && changed < maxPixels) {
		const std::size_t i = stack.back();
		stack.pop_back();
		mask[i] = erase ? 0 : 255;
		++changed;
		const int px = static_cast<int>(i % w), py = static_cast<int>(i / w);
		const int nx[4] = {px - 1, px + 1, px, px}, ny[4] = {py, py, py - 1, py + 1};
		for (int k = 0; k < 4; ++k) {
			if (nx[k] < 0 || ny[k] < 0 || nx[k] >= w || ny[k] >= h) continue;
			const std::size_t j = static_cast<std::size_t>(ny[k]) * w + nx[k];
			if (seen[j]) continue;
			const unsigned char* c = &rgb[j * 3];
			if (std::abs(c[0] - sr) <= tolerance && std::abs(c[1] - sg) <= tolerance && std::abs(c[2] - sb) <= tolerance) {
				seen[j] = 1;
				stack.push_back(j);
			}
		}
	}
	return changed;
}

// A box blur of `radius` pixels (twice, which is nearly a Gaussian): the edge of what is in front becomes a short ramp, not a staircase. 0 and 255 inside stay.
inline void feather(std::vector<std::uint8_t>& mask, int w, int h, int radius) {
	if (radius < 1 || w < 1 || h < 1 || mask.size() != static_cast<std::size_t>(w) * h) return;
	std::vector<float> a(mask.begin(), mask.end()), b(a.size());
	for (int pass = 0; pass < 2; ++pass) {
		for (int y = 0; y < h; ++y) {   // across
			for (int x = 0; x < w; ++x) {
				float sum = 0.0f;
				int n = 0;
				for (int k = -radius; k <= radius; ++k) {
					const int xx = x + k;
					if (xx < 0 || xx >= w) continue;
					sum += a[static_cast<std::size_t>(y) * w + xx];
					++n;
				}
				b[static_cast<std::size_t>(y) * w + x] = sum / n;
			}
		}
		for (int y = 0; y < h; ++y) {   // down
			for (int x = 0; x < w; ++x) {
				float sum = 0.0f;
				int n = 0;
				for (int k = -radius; k <= radius; ++k) {
					const int yy = y + k;
					if (yy < 0 || yy >= h) continue;
					sum += b[static_cast<std::size_t>(yy) * w + x];
					++n;
				}
				a[static_cast<std::size_t>(y) * w + x] = sum / n;
			}
		}
	}
	for (std::size_t i = 0; i < mask.size(); ++i) mask[i] = static_cast<std::uint8_t>(std::min(255.0f, std::max(0.0f, a[i] + 0.5f)));
}

// The mask at another size (bilinear: a painted edge stays soft). Empty for a size below 1 or a mask that does not fit its stated size.
inline std::vector<std::uint8_t> resized(const std::vector<std::uint8_t>& mask, int w, int h, int nw, int nh) {
	if (w < 1 || h < 1 || nw < 1 || nh < 1 || mask.size() != static_cast<std::size_t>(w) * h) return {};
	if (w == nw && h == nh) return mask;
	std::vector<std::uint8_t> out(static_cast<std::size_t>(nw) * nh);
	for (int y = 0; y < nh; ++y) {
		const double fy = std::min<double>(h - 1, std::max(0.0, (y + 0.5) * h / nh - 0.5));
		const int y0 = static_cast<int>(fy), y1 = std::min(h - 1, y0 + 1);
		const double ty = fy - y0;
		for (int x = 0; x < nw; ++x) {
			const double fx = std::min<double>(w - 1, std::max(0.0, (x + 0.5) * w / nw - 0.5));
			const int x0 = static_cast<int>(fx), x1 = std::min(w - 1, x0 + 1);
			const double tx = fx - x0;
			const double top = mask[static_cast<std::size_t>(y0) * w + x0] * (1.0 - tx) + mask[static_cast<std::size_t>(y0) * w + x1] * tx;
			const double bottom = mask[static_cast<std::size_t>(y1) * w + x0] * (1.0 - tx) + mask[static_cast<std::size_t>(y1) * w + x1] * tx;
			out[static_cast<std::size_t>(y) * nw + x] = static_cast<std::uint8_t>(top * (1.0 - ty) + bottom * ty + 0.5);
		}
	}
	return out;
}

}  // namespace occluder_mask
