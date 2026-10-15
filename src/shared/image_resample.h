// image_resample.h -- pbrt-v4's resize-up of an image whose sides are not powers of two (Image::FloatResizeUp), which MIPMap / Image::GeneratePyramid applies BEFORE building
// the pyramid: a 1000 x 600 picture is resampled to 1024 x 1024 and its levels come from that, so its level 0 is the resampled picture and it has log2(1024) + 1 levels (the
// CPU used to halve with floors and have one level fewer, and the OptiX bump-height chain halved with ceilings and so had pbrt's count but not its texels).
// std-only, one channel at a time, shared by the CPU mipmap (mipmap.h) and the OptiX builder's bump-height mip chain (pbrt_gpu_builder_materials.h).
#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace image_resample {

// The wrap modes of MipWrapMode (mipmap.h), by value, so this header needs nothing.
constexpr int kClamp = 0, kRepeat = 1, kBlack = 2;

inline bool isPowerOf2(int v) { return v > 0 && (v & (v - 1)) == 0; }
inline int roundUpPowerOf2(int v) {
	int p = 1;
	while (p < v) p <<= 1;
	return p;
}

struct Weights {
	int firstPixel = 0;
	double weight[4] = {0.0, 0.0, 0.0, 0.0};
};

// pbrt-v4 Image::ResampleWeights: four taps of a Lanczos-windowed sinc (radius 2, tau 2) around the new pixel's centre in the old image, normalised.
inline std::vector<Weights> resampleWeights(int oldRes, int newRes) {
	const double pi = 3.14159265358979323846;
	const auto sinc = [pi](double x) { return std::fabs(x) < 1e-8 ? 1.0 : std::sin(pi * x) / (pi * x); };
	const auto windowedSinc = [&](double x, double radius, double tau) { return std::fabs(x) > radius ? 0.0 : sinc(x) * sinc(x / tau); };
	std::vector<Weights> out(static_cast<std::size_t>(newRes));
	for (int i = 0; i < newRes; ++i) {
		const double center = (i + 0.5) * oldRes / newRes;
		Weights& w = out[static_cast<std::size_t>(i)];
		w.firstPixel = static_cast<int>(std::floor((center - 2.0) + 0.5));
		double sum = 0.0;
		for (int j = 0; j < 4; ++j) {
			w.weight[j] = windowedSinc(w.firstPixel + j + 0.5 - center, 2.0, 2.0);
			sum += w.weight[j];
		}
		const double inv = sum != 0.0 ? 1.0 / sum : 0.0;
		for (int j = 0; j < 4; ++j) w.weight[j] *= inv;
	}
	return out;
}

// A pixel coordinate under a wrap mode: false for "black" outside the image.
inline bool remap(int& p, int res, int wrap) {
	if (p >= 0 && p < res) return true;
	if (wrap == kRepeat) { p %= res; if (p < 0) p += res; return true; }
	if (wrap == kClamp) { p = std::min(std::max(p, 0), res - 1); return true; }
	return false;
}

// One channel (row-major, width * height) resampled up to the next power of two on each side (a side that already is one is passed through unchanged, up to rounding);
// `newWidth` / `newHeight` receive the new size. pbrt resamples along x, then y, and clamps what the windowed sinc undershoots to zero.
inline std::vector<float> resizeUpToPowerOf2(const std::vector<float>& src, int width, int height, int wrap, int& newWidth, int& newHeight) {
	newWidth = roundUpPowerOf2(width);
	newHeight = roundUpPowerOf2(height);
	if (newWidth == width && newHeight == height) return src;
	const std::vector<Weights> wx = resampleWeights(width, newWidth), wy = resampleWeights(height, newHeight);
	std::vector<float> across(static_cast<std::size_t>(newWidth) * height);
	for (int y = 0; y < height; ++y)
		for (int x = 0; x < newWidth; ++x) {
			double v = 0.0;
			for (int j = 0; j < 4; ++j) {
				int xp = wx[static_cast<std::size_t>(x)].firstPixel + j;
				if (remap(xp, width, wrap)) v += wx[static_cast<std::size_t>(x)].weight[j] * src[static_cast<std::size_t>(y) * width + xp];
			}
			across[static_cast<std::size_t>(y) * newWidth + x] = static_cast<float>(v);
		}
	std::vector<float> out(static_cast<std::size_t>(newWidth) * newHeight);
	for (int y = 0; y < newHeight; ++y)
		for (int x = 0; x < newWidth; ++x) {
			double v = 0.0;
			for (int j = 0; j < 4; ++j) {
				int yp = wy[static_cast<std::size_t>(y)].firstPixel + j;
				if (remap(yp, height, wrap)) v += wy[static_cast<std::size_t>(y)].weight[j] * across[static_cast<std::size_t>(yp) * newWidth + x];
			}
			out[static_cast<std::size_t>(y) * newWidth + x] = static_cast<float>(std::max(0.0, v));
		}
	return out;
}

}  // namespace image_resample
