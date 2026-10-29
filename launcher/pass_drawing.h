#pragma once
// pass_drawing.h -- pictures drawn from a frame's first-hit passes (cpu_render_aov_planes: 12 planes of w x h - albedo 0-2, normal 3-5, depth 6, uv 7-8, coverage 9, object ID 10),
// not rendered: the onion-skin sheet's flat shading (onion_frame.h) and the storyboard's pencil drawing (storyboard.h). Plain functions of the planes, so they can be tested
// without a scene.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace pass_drawing {

// A frame's passes in a quick flat shading (sRGB, w x h x 3): its colour lit from above and the front, the background a pale grey.
inline std::vector<float> flatShade(const std::vector<float> &planes, int w, int h) {
	const std::size_t n = static_cast<std::size_t>(w) * h;
	std::vector<float> rgb(n * 3);
	const double L[3] = {-0.35, 0.8, 0.48};
	for (std::size_t i = 0; i < n; ++i) {   // (shown as sRGB: the passes' colours are linear)
		const bool hit = planes[9 * n + i] > 0.5f;
		const double lit = hit ? 0.3 + 0.7 * std::max(0.0, (planes[3 * n + i] * L[0] + planes[4 * n + i] * L[1] + planes[5 * n + i] * L[2]) / 1.0328) : 0.0;
		for (int c = 0; c < 3; ++c)
			rgb[3 * i + c] = hit ? static_cast<float>(std::pow(std::clamp(planes[static_cast<std::size_t>(c) * n + i] * lit * 1.1, 0.0, 1.0), 1.0 / 2.2))
								 : 0.86f - 0.1f * static_cast<float>(i / w) / h;
	}
	return rgb;
}

constexpr float kPaper = 0.96f, kInk = 0.13f;

// A frame's passes drawn as a story artist's panel (sRGB, w x h x 3).
inline std::vector<float> sketch(const std::vector<float> &planes, int w, int h, bool colour) {
	const std::size_t n = static_cast<std::size_t>(w) * h;
	const auto plane = [&](int p, std::size_t i) { return planes[static_cast<std::size_t>(p) * n + i]; };
	std::vector<float> rgb = flatShade(planes, w, h);
	const int spacing = std::max(4, static_cast<int>(std::lround(h / 60.0)));   // (the hatching's)
	const auto hit = [&](std::size_t i) { return plane(9, i) > 0.5f; };
	std::vector<float> value(n, 0.0f);   // (the shading's brightness, smoothed over 3 x 3 so that a texture's speckle does not break the tones up)
	for (std::size_t i = 0; i < n; ++i) value[i] = 0.2126f * rgb[3 * i] + 0.7152f * rgb[3 * i + 1] + 0.0722f * rgb[3 * i + 2];
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x) {
			const std::size_t i = static_cast<std::size_t>(y) * w + x;
			if (!hit(i)) {
				for (int c = 0; c < 3; ++c) rgb[3 * i + c] = kPaper;
				continue;
			}
			if (colour) continue;
			float sum = 0.0f, count = 0.0f;
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx) {
					const int sx = x + dx, sy = y + dy;
					if (sx < 0 || sy < 0 || sx >= w || sy >= h || !hit(static_cast<std::size_t>(sy) * w + sx)) continue;
					sum += value[static_cast<std::size_t>(sy) * w + sx], count += 1.0f;
				}
			const float v = sum / std::max(1.0f, count);
			float tone = v < 0.22f ? 0.5f : v < 0.45f ? 0.68f : v < 0.7f ? 0.83f : 0.94f;
			if (v < 0.22f && (x + y) % spacing == 0) tone = 0.3f;   // (the darkest tone hatched)
			for (int c = 0; c < 3; ++c) rgb[3 * i + c] = tone;
		}
	// the ink: where coverage or the object changes between a pixel and the next, where the depth bends (its second difference: a ground seen edge-on has a steep depth but a
	// straight one), and where the surface folds (its facing turns by more than 40 degrees)
	std::vector<char> ink(n, 0);
	const auto differ = [&](std::size_t a, std::size_t b) {
		if (hit(a) != hit(b)) return true;
		if (!hit(a)) return false;
		if (plane(10, a) != plane(10, b)) return true;
		return plane(3, a) * plane(3, b) + plane(4, a) * plane(4, b) + plane(5, a) * plane(5, b) < 0.75f;
	};
	const auto bends = [&](std::size_t before, std::size_t i, std::size_t after) {
		if (!hit(before) || !hit(after)) return false;
		const float d = plane(6, i);
		return std::fabs(plane(6, before) - 2.0f * d + plane(6, after)) > 0.03f * std::max(1e-3f, d);
	};
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x) {
			const std::size_t i = static_cast<std::size_t>(y) * w + x, row = static_cast<std::size_t>(w);
			if ((x + 1 < w && differ(i, i + 1)) || (y + 1 < h && differ(i, i + row))) ink[i] = 1;
			else if (hit(i) && ((x > 0 && x + 1 < w && bends(i - 1, i, i + 1)) || (y > 0 && y + 1 < h && bends(i - row, i, i + row)))) ink[i] = 1;
		}
	// ink that fills more than half of the 5 x 5 round it is detail too fine to draw (grass and leaves far off): a smudge, left out
	std::vector<char> kept = ink;
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x) {
			if (!ink[static_cast<std::size_t>(y) * w + x]) continue;
			int count = 0;
			for (int dy = -2; dy <= 2; ++dy)
				for (int dx = -2; dx <= 2; ++dx) count += x + dx >= 0 && y + dy >= 0 && x + dx < w && y + dy < h && ink[static_cast<std::size_t>(y + dy) * w + x + dx];
			if (count > 13) kept[static_cast<std::size_t>(y) * w + x] = 0;
		}
	// lines lighter with distance, as an artist draws what is far off (the farthest a tenth of the nearest's darkness)
	std::vector<float> depths;
	for (std::size_t i = 0; i < n; i += 7)
		if (hit(i)) depths.push_back(plane(6, i));
	float nearD = 0.0f, farD = 1.0f;
	if (!depths.empty()) {
		std::sort(depths.begin(), depths.end());
		nearD = depths[depths.size() / 20], farD = std::max(nearD + 1e-3f, depths[depths.size() * 19 / 20]);
	}
	const int thick = std::max(1, static_cast<int>(std::lround(h / 400.0)));   // (thicker lines on a bigger drawing)
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x) {
			float strength = 0.0f;
			for (int dy = 0; dy < thick; ++dy)
				for (int dx = 0; dx < thick; ++dx) {
					if (x - dx < 0 || y - dy < 0) continue;
					const std::size_t j = static_cast<std::size_t>(y - dy) * w + (x - dx);
					if (!kept[j]) continue;
					const float d = hit(j) ? plane(6, j) : nearD;
					strength = std::max(strength, std::clamp(1.0f - 0.9f * (d - nearD) / (farD - nearD), 0.1f, 1.0f));
				}
			if (strength > 0.0f)
				for (int c = 0; c < 3; ++c) {
					float &v = rgb[(static_cast<std::size_t>(y) * w + x) * 3 + c];
					v += (kInk - v) * strength;
				}
		}
	return rgb;
}

}  // namespace pass_drawing
