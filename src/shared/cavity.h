#pragma once
// cavity.h -- edges and crevices: the sharp outer edges of things caught light and their creases and corners darkened, as Blender's Workbench draws its "cavity"
// (curvature: ridges lighter, valleys darker) and as Cycles' Pointiness attribute is used for worn, hand-painted edges. Made from the picture's passes (aov_pass.h: the
// normal and the depth) after the render: where the surface turns (its normal changes between neighbours on the same surface) is an edge; whether it turns outwards
// (a ridge: the depth curves away from the camera round it) or inwards (a valley: it curves towards it) says which. Our own code from the idea.
//
// `edges E` lightens ridges by up to E, `cavity C` darkens valleys by up to C (toon_look.h words; 0 off). `edge-width W`: how wide the effect reaches, in pixels at
// a picture 1080 high.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace cavity {

struct Params {
	double edges = 0.0, cavity = 0.0, width = 3.0;
};

// `rgb` (linear, w x h x 3) changed in place; `normal` the three normal planes, `depth` the depth plane, `cover` the coverage plane (aov_pass.h's order).
inline void apply(std::vector<float> &rgb, int w, int h, const std::vector<float> *normal[3], const std::vector<float> &depth, const std::vector<float> &cover, const Params &p) {
	if ((p.edges <= 0.0 && p.cavity <= 0.0) || w < 5 || h < 5) return;
	const std::size_t n = static_cast<std::size_t>(w) * h;
	const auto covered = [&](std::size_t i) { return cover[i] > 0.5f && depth[i] > 0.0f; };
	const auto nd = [&](std::size_t i, std::size_t j) {
		return (*normal[0])[i] * (*normal[0])[j] + (*normal[1])[i] * (*normal[1])[j] + (*normal[2])[i] * (*normal[2])[j];
	};
	const int r = std::max(1, static_cast<int>(std::lround(p.width * h / 1080.0 * 0.5)));
	// the signed turn at each pixel: + a ridge, - a valley
	std::vector<float> turn(n, 0.0f);
	for (int y = r; y + r < h; ++y)
		for (int x = r; x + r < w; ++x) {
			const std::size_t i = static_cast<std::size_t>(y) * w + x;
			if (!covered(i)) continue;
			const std::size_t nb[4] = {i - static_cast<std::size_t>(r), i + static_cast<std::size_t>(r), i - static_cast<std::size_t>(r) * w, i + static_cast<std::size_t>(r) * w};
			double bend = 0.0, lap = 0.0;
			bool across = false;   // (a depth jump: the outline's, not a turn of the surface)
			const double c = 1.0 / depth[i];
			for (std::size_t j : nb) {
				if (!covered(j)) {
					across = true;
					break;
				}
				const double cj = 1.0 / depth[j];
				if (std::fabs(cj - c) > 0.08 * c) {
					across = true;
					break;
				}
				bend = std::max(bend, 1.0 - nd(i, j));
				lap += cj - c;
			}
			if (across) continue;
			const double strength = std::clamp((bend - 0.01) / 0.3, 0.0, 1.0);   // (a turn of about 45 degrees within the reach is a full edge; a hair's turn, noise, none)
			turn[i] = static_cast<float>(lap < 0.0 ? strength : -strength);   // (the neighbours further off round it: a ridge)
		}
	// softened over the reach, and laid on
	std::vector<float> soft(n, 0.0f);
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x) {
			double s = 0.0, wsum = 0.0;
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx) {
					const int xx = std::clamp(x + dx, 0, w - 1), yy = std::clamp(y + dy, 0, h - 1);
					const double wt = (dx == 0 ? 2.0 : 1.0) * (dy == 0 ? 2.0 : 1.0);
					s += wt * turn[static_cast<std::size_t>(yy) * w + xx], wsum += wt;
				}
			soft[static_cast<std::size_t>(y) * w + x] = static_cast<float>(s / wsum);
		}
	for (std::size_t i = 0; i < n; ++i) {
		if (!covered(i) || soft[i] == 0.0f) continue;
		const double f = soft[i] > 0.0f ? 1.0 + p.edges * soft[i] : 1.0 + p.cavity * soft[i];
		for (int c = 0; c < 3; ++c) rgb[3 * i + c] = static_cast<float>(std::max(0.0, rgb[3 * i + c] * f));
	}
}

}  // namespace cavity
