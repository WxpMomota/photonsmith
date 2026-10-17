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
// The seams are removed the usual way: the picture is blended with a copy of itself shifted by half its size, with a weight that is 1 in the middle and 0 at the edges, so the edges of
// the result are made of the shifted copy, which is continuous across the wrap. The blend of two unrelated halves is flatter than either, so the contrast is put back
// (the weights add as squares, not as 1, when the two are unrelated).
//
// Header-only, standard library only; shared by the Scene Builder (qt_gui/scene_builder_material_photo.cpp) and its tests.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace material_from_photo {

struct Options {
	bool flattenLighting = true;   // divide out the photo's broad light and shade
	bool seamless = true;          // make the picture repeat with no joins
};

struct Result {
	int size = 0;                                  // the pictures are size x size
	std::vector<unsigned char> albedo;             // size * size * 3, sRGB
	std::vector<unsigned char> height;             // size * size, grayscale (light is high)
	double averageColor[3] = {0.5, 0.5, 0.5};      // linear light 0..1
	double lightingRange = 1.0;                    // the brightest broad light over the darkest that was divided out (about 1 for an evenly lit photo)
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

	// seamless: blend with the copy shifted by half the size
	if (options.seamless) {
		const double pi = 3.14159265358979323846;
		std::vector<float> weight(static_cast<size_t>(size));
		for (int i = 0; i < size; ++i) {
			const double s = std::sin(pi * (i + 0.5) / size);
			weight[static_cast<size_t>(i)] = static_cast<float>(s * s);
		}
		const int shift = size / 2;
		std::vector<float> blended(n);
		for (int c = 0; c < 3; ++c) {
			for (int y = 0; y < size; ++y)
				for (int x = 0; x < size; ++x) {
					const float w = weight[static_cast<size_t>(x)] * weight[static_cast<size_t>(y)];
					const size_t here = static_cast<size_t>(y) * size + x;
					const size_t there = static_cast<size_t>((y + shift) % size) * size + (x + shift) % size;
					const double a = img[c][here] - mean[c], b = img[c][there] - mean[c];
					const double norm = std::sqrt(static_cast<double>(w) * w + (1.0 - w) * (1.0 - w));   // (two unrelated pictures: the weights add as squares)
					blended[here] = static_cast<float>(mean[c] + (w * a + (1.0 - w) * b) / norm);
				}
			img[c] = blended;
		}
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
