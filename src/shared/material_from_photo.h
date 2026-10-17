#pragma once
// material_from_photo.h -- "Material from a photo": one photo of a flat surface (a floor, a wall, a piece of fabric) becomes the pictures of a material that can cover any object.
//
// Three things are made from the photo's square patch, with plain image processing (no model, no download, the same on every system):
//   * a colour picture with the photo's broad light and shade divided out (a window's glow across a floor is not part of the wood) and made seamless, so it can be repeated across a
//     large surface with no visible joins;
//   * a height map for the Builder's bump map: what is dark in the picture (grout, a crack, the gap between threads) is low, what is light is high, at two scales (the fine grain and the
//     coarse pattern). It is a guess at the relief from the picture alone, not a measurement of it, and tiles the same way;
//   * the average colour, for the material's flat colour (what shows where a picture is not used, and in the views).
//
// The seams are removed by laying the picture over a copy of itself shifted by half its size: the edges of the result must come from the shifted copy (it is continuous across the
// wrap) and the middle from the photo. Where one gives way to the other is a cut that runs along the path where the two nearly agree (found by dynamic programming, once across and once
// down), so lines and grain stay sharp and the join is a few pixels wide. When the two agree nowhere better than anywhere else (a strong ramp of light that was not flattened) there is no such path and the two are
// blended instead, with a weight that is 1 in the middle and 0 at the edges and the contrast put back (two unrelated pictures: the weights add as squares, not as 1).
//
// Header-only, standard library only; shared by the Scene Builder (qt_gui/scene_builder_material_photo.cpp) and its tests.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace material_from_photo {

enum class SeamMethod { Auto, Cut, Blend };   // Auto: the cut when it finds a path where the picture agrees with its shifted copy, else the blend

struct Options {
	bool flattenLighting = true;   // divide out the photo's broad light and shade
	bool seamless = true;          // make the picture repeat with no joins
	SeamMethod seamMethod = SeamMethod::Auto;
};

struct Result {
	int size = 0;                                  // the pictures are size x size
	std::vector<unsigned char> albedo;             // size * size * 3, sRGB
	std::vector<unsigned char> height;             // size * size, grayscale (light is high)
	double averageColor[3] = {0.5, 0.5, 0.5};      // linear light 0..1
	double lightingRange = 1.0;                    // the brightest broad light over the darkest that was divided out (about 1 for an evenly lit photo)
	bool usedCut = false;                          // the seams were removed by a cut (false: blended, or left alone when not asked for)
	double cutDisagreement = 0.0;                  // how far apart the photo and its shifted copy are along the cuts, 0..1 (root mean square of the channels)
	double cutRatio = 1.0;                         // that against how far apart they are on average: well under 1 when the cut found a path where they agree
};

namespace detail {

inline double toLinear(double s) { return s <= 0.04045 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4); }
inline double toSrgb(double l) { return l <= 0.0031308 ? l * 12.92 : 1.055 * std::pow(l, 1.0 / 2.4) - 0.055; }

inline int edge(int i, int n, bool wrap) {
	if (wrap) {
		i %= n;
		return i < 0 ? i + n : i;
	}
	return i < 0 ? 0 : (i >= n ? n - 1 : i);
}

// One pass of a box blur of radius r along the rows (horizontal) or the columns of a w x h single-channel picture; the edges wrap or repeat their last value.
inline void boxPass(std::vector<float>& img, int w, int h, int r, bool wrap, bool horizontal) {
	const int n = horizontal ? w : h, lines = horizontal ? h : w;
	std::vector<float> line(static_cast<size_t>(n)), res(static_cast<size_t>(n));
	for (int l = 0; l < lines; ++l) {
		for (int i = 0; i < n; ++i) line[static_cast<size_t>(i)] = horizontal ? img[static_cast<size_t>(l) * w + i] : img[static_cast<size_t>(i) * w + l];
		double sum = 0.0;
		for (int k = -r; k <= r; ++k) sum += line[static_cast<size_t>(edge(k, n, wrap))];
		for (int i = 0; i < n; ++i) {
			res[static_cast<size_t>(i)] = static_cast<float>(sum / (2 * r + 1));
			sum += line[static_cast<size_t>(edge(i + r + 1, n, wrap))] - line[static_cast<size_t>(edge(i - r, n, wrap))];
		}
		for (int i = 0; i < n; ++i) (horizontal ? img[static_cast<size_t>(l) * w + i] : img[static_cast<size_t>(i) * w + l]) = res[static_cast<size_t>(i)];
	}
}

// A blur close to a Gaussian of the given sigma (three box passes in each direction: their variance is r (r + 1)).
inline void blur(std::vector<float>& img, int w, int h, double sigma, bool wrap) {
	const int r = std::max(1, static_cast<int>(std::lround(std::sqrt(sigma * sigma + 0.25) - 0.5)));
	for (int pass = 0; pass < 3; ++pass) {
		boxPass(img, w, h, r, wrap, true);
		boxPass(img, w, h, r, wrap, false);
	}
}

// How a cut went: the mean difference of the channels (root mean square) between the picture and its shifted copy along the two cuts, and over the whole picture. A cut that is much
// better than the picture's average has found a path where the two agree; one that is not (a ramp of light is 0.35 off everywhere) has found nothing to hide the join in.
struct CutResult {
	double along = 0.0;
	double overall = 0.0;
};

// Makes three channel pictures of n x n repeat along one direction (across x when `acrossX`, else across y): the photo's middle is kept, its edges come from the copy shifted by half, and
// the cut between them (one on each side of the middle, each within a quarter of the picture of where it could be) runs where the two differ least, a few pixels wide. A cut closes round
// the picture (it ends within a pixel of where it began) so that the other direction's repeat is not broken by it.
inline CutResult cutPeriodic(std::vector<float> img[3], int n, bool acrossX) {
	const int half = n / 2;
	const auto index = [&](int a, int b) { return acrossX ? static_cast<size_t>(b) * n + a : static_cast<size_t>(a) * n + b; };   // a: across the seam, b: along it
	std::vector<float> apart(static_cast<size_t>(n) * n);   // apart[b * n + a]: how far the picture is from its shifted copy there
	double total = 0.0;
	for (int b = 0; b < n; ++b)
		for (int a = 0; a < n; ++a) {
			double sq = 0.0;
			for (int c = 0; c < 3; ++c) {
				const double d = img[c][index(a, b)] - img[c][index((a + half) % n, b)];
				sq += d * d;
			}
			const float v = static_cast<float>(std::sqrt(sq / 3.0));
			apart[static_cast<size_t>(b) * n + a] = v;
			total += v;
		}
	const float inf = 1e30f;
	// a closed cut within [lo, hi): found by dynamic programming from a few starting positions, keeping the cheapest that ends within a pixel of where it began
	const auto findCut = [&](int lo, int hi, std::vector<int>& path) {
		const int width = hi - lo;
		std::vector<float> dp(static_cast<size_t>(n) * width);
		const auto at = [&](int b, int a) -> float& { return dp[static_cast<size_t>(b) * width + a]; };
		const auto cost = [&](int b, int a) { const float v = apart[static_cast<size_t>(b) * n + lo + a]; return v * v; };
		float bestEnd = inf;
		int bestStart = 0, bestAt = 0;
		const auto solve = [&](int start, float& endCost, int& endAt) {
			for (int a = 0; a < width; ++a) at(0, a) = a == start ? cost(0, a) : inf;
			for (int b = 1; b < n; ++b)
				for (int a = 0; a < width; ++a) {
					float best = at(b - 1, a);
					if (a > 0) best = std::min(best, at(b - 1, a - 1));
					if (a + 1 < width) best = std::min(best, at(b - 1, a + 1));
					at(b, a) = best >= inf ? inf : best + cost(b, a);
				}
			endCost = inf;
			endAt = start;
			for (int a = std::max(0, start - 1); a <= std::min(width - 1, start + 1); ++a)
				if (at(n - 1, a) < endCost) { endCost = at(n - 1, a); endAt = a; }
		};
		const int stride = std::max(1, width / 24);
		for (int start = 0; start < width; start += stride) {
			float endCost;
			int endAt;
			solve(start, endCost, endAt);
			if (endCost < bestEnd) { bestEnd = endCost; bestStart = start; bestAt = endAt; }
		}
		float endCost;
		int endAt;
		solve(bestStart, endCost, endAt);   // (again for the best start: the table is the one to walk back through)
		path.assign(static_cast<size_t>(n), lo);
		int a = bestAt;
		for (int b = n - 1; b >= 0; --b) {
			path[static_cast<size_t>(b)] = lo + a;
			if (b == 0) break;
			int pick = a;
			float value = at(b - 1, a);
			if (a > 0 && at(b - 1, a - 1) < value) { pick = a - 1; value = at(b - 1, a - 1); }
			if (a + 1 < width && at(b - 1, a + 1) < value) pick = a + 1;
			a = pick;
		}
	};
	std::vector<int> left, right;
	findCut(n / 8, 3 * n / 8, left);
	findCut(5 * n / 8, 7 * n / 8, right);
	CutResult result;
	for (int b = 0; b < n; ++b) result.along += apart[static_cast<size_t>(b) * n + left[static_cast<size_t>(b)]] + apart[static_cast<size_t>(b) * n + right[static_cast<size_t>(b)]];
	result.along /= 2.0 * n;
	result.overall = total / (static_cast<double>(n) * n);
	// the result: the photo between the cuts, the shifted copy outside them, with a join a few pixels wide. The two are rarely the same brightness along a cut (a light that was not quite
	// flattened), which would show as a step; so the difference at the cut, smoothed along it, is carried into the shifted copy, in full at the cut and tapering to nothing at the picture's
	// edge (where the shifted copy is already continuous across the wrap), which makes the join continuous in brightness and leaves the texture as it was.
	const int smooth = std::max(4, n / 16);
	const auto stepAt = [&](const std::vector<int>& path, int b, int c) {   // the photo minus its shifted copy at the cut, averaged along it
		double sum = 0.0;
		for (int k = -smooth; k <= smooth; ++k) {
			const int bb = detail::edge(b + k, n, true);
			const int a = path[static_cast<size_t>(bb)];
			sum += img[c][index(a, bb)] - img[c][index((a + half) % n, bb)];
		}
		return sum / (2 * smooth + 1);
	};
	std::vector<float> stepLeft[3], stepRight[3];
	for (int c = 0; c < 3; ++c) {
		stepLeft[c].resize(static_cast<size_t>(n));
		stepRight[c].resize(static_cast<size_t>(n));
		for (int b = 0; b < n; ++b) {
			stepLeft[c][static_cast<size_t>(b)] = static_cast<float>(stepAt(left, b, c));
			stepRight[c][static_cast<size_t>(b)] = static_cast<float>(stepAt(right, b, c));
		}
	}
	std::vector<float> out[3];
	for (int c = 0; c < 3; ++c) out[c].resize(img[c].size());
	const auto ramp = [](double t) {   // 0 well before the cut, 1 well after, over about 6 pixels
		const double u = std::clamp(t / 6.0 + 0.5, 0.0, 1.0);
		return u * u * (3.0 - 2.0 * u);
	};
	for (int b = 0; b < n; ++b)
		for (int a = 0; a < n; ++a) {
			const int cutLeft = left[static_cast<size_t>(b)], cutRight = right[static_cast<size_t>(b)];
			const double w = std::min(ramp(a - cutLeft), ramp(cutRight - a));   // 1: the photo, 0: the shifted copy
			// the copy, brought to the photo's brightness near the cut it lies beyond
			const double nearLeft = a < half ? std::clamp(static_cast<double>(a) / std::max(cutLeft, 1), 0.0, 1.0) : 0.0;
			const double nearRight = a >= half ? std::clamp(static_cast<double>(n - 1 - a) / std::max(n - 1 - cutRight, 1), 0.0, 1.0) : 0.0;
			for (int c = 0; c < 3; ++c) {
				const double copy = img[c][index((a + half) % n, b)] + nearLeft * stepLeft[c][static_cast<size_t>(b)] + nearRight * stepRight[c][static_cast<size_t>(b)];
				out[c][index(a, b)] = static_cast<float>(w * img[c][index(a, b)] + (1.0 - w) * copy);
			}
		}
	for (int c = 0; c < 3; ++c) img[c] = out[c];
	return result;
}

// Makes three channel pictures of size x size repeat by blending each with its copy shifted by half the size, first across and then down (one direction at a time: a single blend with
// the product of the two weights would let the shifted copy's own seam show near the top and bottom edges), with a weight that is 1 in the middle and 0 at the edges (so the edges are made
// of the shifted copy, which is continuous across the wrap); the contrast the blend loses is put back (two unrelated pictures: the weights add as squares, not as 1).
inline void blendShifted(std::vector<float> img[3], const double mean[3], int size) {
	const double pi = 3.14159265358979323846;
	const size_t n = static_cast<size_t>(size) * static_cast<size_t>(size);
	std::vector<float> weight(static_cast<size_t>(size));
	for (int i = 0; i < size; ++i) {
		const double s = std::sin(pi * (i + 0.5) / size);
		weight[static_cast<size_t>(i)] = static_cast<float>(s * s);
	}
	const int shift = size / 2;
	std::vector<float> blended(n);
	for (const bool acrossX : {true, false})
		for (int c = 0; c < 3; ++c) {
			for (int y = 0; y < size; ++y)
				for (int x = 0; x < size; ++x) {
					const float w = weight[static_cast<size_t>(acrossX ? x : y)];
					const size_t here = static_cast<size_t>(y) * size + x;
					const size_t there = acrossX ? static_cast<size_t>(y) * size + (x + shift) % size : static_cast<size_t>((y + shift) % size) * size + x;
					const double a = img[c][here] - mean[c], b = img[c][there] - mean[c];
					const double norm = std::sqrt(static_cast<double>(w) * w + (1.0 - w) * (1.0 - w));
					blended[here] = static_cast<float>(mean[c] + (w * a + (1.0 - w) * b) / norm);
				}
			img[c] = blended;
		}
}

inline float percentile(std::vector<float> v, double p) {
	if (v.empty()) return 0.0f;
	const size_t k = std::min(v.size() - 1, static_cast<size_t>(p * static_cast<double>(v.size() - 1) + 0.5));
	std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(k), v.end());
	return v[k];
}

}  // namespace detail

// The side of the pictures made from a patch whose shorter side is `shortSide` pixels: the largest of 256, 512 and 1024 the photo can fill without being blown up.
inline int pictureSize(int shortSide) { return shortSide >= 1024 ? 1024 : (shortSide >= 512 ? 512 : 256); }

// How visible the seams of a picture are when it is repeated: the difference across the wrap-around edges against the difference between neighbouring pixels inside. About 1 means
// the edges look like the inside of the picture; a photo of a wall that is darker on one side is far above that. rgb8 is size x size x 3 (or 1 channel with `channels` = 1).
inline double seamRatio(const unsigned char* pixels, int size, int channels = 3) {
	if (!pixels || size < 4) return 1.0;
	const auto at = [&](int x, int y, int c) { return static_cast<double>(pixels[(static_cast<size_t>(y) * size + x) * channels + c]); };
	double seam = 0.0, inside = 0.0;
	size_t nSeam = 0, nInside = 0;
	for (int y = 0; y < size; ++y)
		for (int c = 0; c < channels; ++c) {
			seam += std::fabs(at(size - 1, y, c) - at(0, y, c)) + std::fabs(at(y, size - 1, c) - at(y, 0, c));
			nSeam += 2;
			for (int x = 0; x + 1 < size; ++x) {
				inside += std::fabs(at(x, y, c) - at(x + 1, y, c)) + std::fabs(at(y, x, c) - at(y, x + 1, c));
				nInside += 2;
			}
		}
	return (seam / static_cast<double>(nSeam)) / std::max(inside / static_cast<double>(nInside), 1e-6);
}

// Makes the material's pictures from a size x size, 8-bit sRGB patch of the photo (`rgb8`, three values per pixel; the caller crops and resizes the photo to a square).
inline Result make(const unsigned char* rgb8, int size, const Options& options = Options()) {
	Result out;
	if (!rgb8 || size < 16) return out;
	const size_t n = static_cast<size_t>(size) * static_cast<size_t>(size);
	out.size = size;

	// linear light, three channels
	std::vector<float> lin[3];
	for (int c = 0; c < 3; ++c) {
		lin[c].resize(n);
		for (size_t i = 0; i < n; ++i) lin[c][i] = static_cast<float>(detail::toLinear(rgb8[i * 3 + static_cast<size_t>(c)] / 255.0));
	}

	// the photo's broad light and shade: the luminance blurred over about a sixth of the picture, divided out so that the picture is as bright on average everywhere (the colour is kept: every
	// channel gets the same gain)
	if (options.flattenLighting) {
		std::vector<float> broad(n);
		double mean = 0.0;
		for (size_t i = 0; i < n; ++i) {
			broad[i] = 0.2126f * lin[0][i] + 0.7152f * lin[1][i] + 0.0722f * lin[2][i];
			mean += broad[i];
		}
		mean /= static_cast<double>(n);
		detail::blur(broad, size, size, size / 6.0, false);
		const float lo = detail::percentile(broad, 0.05), hi = detail::percentile(broad, 0.95);
		out.lightingRange = std::max(1.0, static_cast<double>(hi) / std::max(static_cast<double>(lo), 1e-4));
		for (size_t i = 0; i < n; ++i) {
			const float gain = static_cast<float>(std::clamp(mean / std::max(static_cast<double>(broad[i]), 1e-4), 0.35, 3.0));
			for (int c = 0; c < 3; ++c) lin[c][i] = std::min(1.0f, lin[c][i] * gain);
		}
	}

	// perceptual (sRGB-encoded) values 0..1: the blend and the height are judged by eye
	std::vector<float> img[3];
	double mean[3] = {0, 0, 0};
	for (int c = 0; c < 3; ++c) {
		img[c].resize(n);
		for (size_t i = 0; i < n; ++i) {
			img[c][i] = static_cast<float>(detail::toSrgb(std::clamp(static_cast<double>(lin[c][i]), 0.0, 1.0)));
			mean[c] += img[c][i];
		}
		mean[c] /= static_cast<double>(n);
	}

	// seamless: the cut where the photo agrees with its copy shifted by half the size, or (where it never does) the blend of the two
	if (options.seamless) {
		bool cut = options.seamMethod != SeamMethod::Blend;
		if (cut) {
			std::vector<float> trial[3] = {img[0], img[1], img[2]};
			const detail::CutResult across = detail::cutPeriodic(trial, size, true);   // (the cuts down the picture, then the ones across it)
			const detail::CutResult down = detail::cutPeriodic(trial, size, false);
			out.cutDisagreement = std::max(across.along, down.along);
			out.cutRatio = std::max(across.along / std::max(across.overall, 1e-6), down.along / std::max(down.overall, 1e-6));
			// a cut is worth it when its path is better than the picture's average (or nearly perfect): a ramp of light is as far off along the best path as anywhere
			cut = options.seamMethod == SeamMethod::Cut || out.cutRatio < 0.85 || out.cutDisagreement < 0.04;
			if (cut)
				for (int c = 0; c < 3; ++c) img[c] = trial[c];
		}
		out.usedCut = cut;
		if (!cut) detail::blendShifted(img, mean, size);
	}

	out.albedo.resize(n * 3);
	double lum[3] = {0, 0, 0};
	for (int c = 0; c < 3; ++c)
		for (size_t i = 0; i < n; ++i) {
			const float v = std::clamp(img[c][i], 0.0f, 1.0f);
			out.albedo[i * 3 + static_cast<size_t>(c)] = static_cast<unsigned char>(v * 255.0f + 0.5f);
			lum[c] += detail::toLinear(v);
		}
	for (int c = 0; c < 3; ++c) out.averageColor[c] = lum[c] / static_cast<double>(n);

	// the height: the picture's brightness at two scales (fine grain and coarse pattern), with its broad average taken away, so that grout and cracks are low and what stands out is high.
	// The blurs wrap, so the height map repeats as the picture does.
	std::vector<float> fine(n), coarse(n);
	for (size_t i = 0; i < n; ++i) fine[i] = coarse[i] = 0.2126f * img[0][i] + 0.7152f * img[1][i] + 0.0722f * img[2][i];
	detail::blur(fine, size, size, 0.8, options.seamless);
	detail::blur(coarse, size, size, size / 16.0, options.seamless);
	std::vector<float> relief(n);
	for (size_t i = 0; i < n; ++i) relief[i] = fine[i] - coarse[i];
	const float p2 = detail::percentile(relief, 0.02), p98 = detail::percentile(relief, 0.98);
	const float range = std::max({std::fabs(p2), std::fabs(p98), 1e-4f});
	out.height.resize(n);
	for (size_t i = 0; i < n; ++i) out.height[i] = static_cast<unsigned char>(std::clamp(0.5f + 0.5f * relief[i] / range, 0.0f, 1.0f) * 255.0f + 0.5f);
	return out;
}

// What kind of surface a photo shows, which sets the material's starting values (a guess to be changed: the picture and the relief come from the photo, these do not).
struct SurfaceKind {
	const char* name;        // shown in the dialog
	bool coated;             // a glossy coat (varnish, glaze, polish) over the colour: the Builder's coated diffuse
	double coatRoughness;    // 0 = mirror-like coat
	double reliefMetres;     // how tall the relief is, in scene units taken as metres (the height map's 0..1 times this)
};

inline const std::vector<SurfaceKind>& surfaceKinds() {
	static const std::vector<SurfaceKind> kinds = {
	    {"Wood", true, 0.25, 0.0015},
	    {"Tile or polished stone", true, 0.10, 0.0020},
	    {"Brick, rough stone or concrete", false, 0.0, 0.0040},
	    {"Fabric or carpet", false, 0.0, 0.0020},
	    {"Leather", true, 0.35, 0.0015},
	    {"Plaster or paint", false, 0.0, 0.0008},
	    {"Something else", false, 0.0, 0.0020},
	};
	return kinds;
}

}  // namespace material_from_photo
