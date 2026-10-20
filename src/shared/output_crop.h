#pragma once
// output_crop.h -- cutting a finished render to its crop window.
//
// A pbrt Film "cropwindow" renders a sub-rectangle of the film and writes a picture of just that rectangle. The renderers here draw only the window too, but into a full-size
// frame with black around it (the --crop flag has always meant that). A scene whose file's crop IS its picture (the pbrt-v4-scenes Head: a 1920 x 1080 film, a crop of its face;
// SceneDescriptor::crop_is_picture) has the finished image cut to the window by the launcher, whichever renderer drew it, so none of the renderers' many writers needs to know.
//
// Plain functions on files (PPM, P3 or P6, and EXR) and no renderer types, so they are unit-tested on their own.

#include "exr_writer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

namespace output_crop {

struct Bounds {
	int x0 = 0, x1 = 0, y0 = 0, y1 = 0;   // [x0, x1) x [y0, y1), in pixels
	bool full = true;                      // the whole frame (nothing to cut)
};

// The crop window {x0, x1, y0, y1} (NDC fractions) as pixel bounds of a width x height frame, rounded as the renderers do (camera.h, scene_registry.h's setup_camera). A window
// that collapses to nothing is the whole frame, like the renderers' own fallback.
inline Bounds pixelBounds(const double frac[4], int width, int height) {
	Bounds b;
	b.x0 = static_cast<int>(std::lround(frac[0] * width));
	b.x1 = static_cast<int>(std::lround(frac[1] * width));
	b.y0 = static_cast<int>(std::lround(frac[2] * height));
	b.y1 = static_cast<int>(std::lround(frac[3] * height));
	b.x0 = std::max(0, std::min(b.x0, width));
	b.x1 = std::max(0, std::min(b.x1, width));
	b.y0 = std::max(0, std::min(b.y0, height));
	b.y1 = std::max(0, std::min(b.y1, height));
	if (b.x1 <= b.x0 || b.y1 <= b.y0) {
		b.x0 = 0; b.x1 = width; b.y0 = 0; b.y1 = height;
	}
	b.full = b.x0 == 0 && b.x1 == width && b.y0 == 0 && b.y1 == height;
	return b;
}

// Cuts `rgb` (width x height, `channels` interleaved values per pixel) to `b`.
template <typename T>
inline std::vector<T> cut(const std::vector<T> &rgb, int width, int channels, const Bounds &b) {
	const int w = b.x1 - b.x0, h = b.y1 - b.y0;
	std::vector<T> out(static_cast<std::size_t>(w) * h * channels);
	for (int y = 0; y < h; ++y) {
		const T *src = &rgb[(static_cast<std::size_t>(b.y0 + y) * width + b.x0) * channels];
		std::copy(src, src + static_cast<std::size_t>(w) * channels, &out[static_cast<std::size_t>(y) * w * channels]);
	}
	return out;
}

// A PPM (P3 text or P6 binary) read whole: its size, its maximum value and its samples (3 per pixel).
inline bool readPpm(const std::string &path, int &width, int &height, int &maxValue, std::vector<int> &samples) {
	std::ifstream in(path, std::ios::binary);
	if (!in) return false;
	std::string magic;
	in >> magic;
	if (magic != "P3" && magic != "P6") return false;
	const auto skipComments = [&in]() {
		in >> std::ws;
		while (in.peek() == '#') { in.ignore(1 << 20, '\n'); in >> std::ws; }
	};
	skipComments(); in >> width;
	skipComments(); in >> height;
	skipComments(); in >> maxValue;
	if (!in || width <= 0 || height <= 0 || maxValue <= 0) return false;
	const std::size_t count = static_cast<std::size_t>(width) * height * 3;
	samples.resize(count);
	if (magic == "P3") {
		for (std::size_t i = 0; i < count; ++i)
			if (!(in >> samples[i])) return false;
	} else {
		in.get();   // the single whitespace after the maximum value
		const int bytes = maxValue > 255 ? 2 : 1;
		for (std::size_t i = 0; i < count; ++i) {
			int v = in.get();
			if (bytes == 2) v = (v << 8) | in.get();
			if (!in) return false;
			samples[i] = v;
		}
	}
	return true;
}

// Writes `path` (P3) holding `samples`.
inline bool writePpm(const std::string &path, int width, int height, int maxValue, const std::vector<int> &samples) {
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	if (!out) return false;
	out << "P3\n" << width << ' ' << height << '\n' << maxValue << '\n';
	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; ++x) {
			const std::size_t i = (static_cast<std::size_t>(y) * width + x) * 3;
			out << samples[i] << ' ' << samples[i + 1] << ' ' << samples[i + 2] << '\n';
		}
	}
	return static_cast<bool>(out);
}

// Cuts the PPM at `path` to the crop window `frac`, in place. Returns true if the file now holds the window (or already was the whole frame), false if it could not be read or
// written (the file is left as it was).
inline bool cropPpmFile(const std::string &path, const double frac[4]) {
	int w = 0, h = 0, maxValue = 0;
	std::vector<int> samples;
	if (!readPpm(path, w, h, maxValue, samples)) return false;
	const Bounds b = pixelBounds(frac, w, h);
	if (b.full) return true;
	const std::string tmp = path + ".crop.tmp";   // (written beside it and renamed over it, so a failed write leaves the render as it was)
	if (!writePpm(tmp, b.x1 - b.x0, b.y1 - b.y0, maxValue, cut(samples, w, 3, b))) { std::remove(tmp.c_str()); return false; }
	std::remove(path.c_str());
	return std::rename(tmp.c_str(), path.c_str()) == 0;
}

// The same for an EXR written as plain R, G, B (a render with extra passes is not cut: the passes would be left the wrong size).
inline bool cropExrFile(const std::string &path, const double frac[4]) {
	float *rgba = nullptr;
	int w = 0, h = 0;
	const char *err = nullptr;
	if (LoadEXR(&rgba, &w, &h, path.c_str(), &err) != TINYEXR_SUCCESS || !rgba) {
		if (err) FreeEXRErrorMessage(err);
		return false;
	}
	const Bounds b = pixelBounds(frac, w, h);
	bool ok = true;
	if (!b.full) {
		std::vector<float> rgb(static_cast<std::size_t>(w) * h * 3);
		for (std::size_t i = 0; i < static_cast<std::size_t>(w) * h; ++i)
			for (int c = 0; c < 3; ++c) rgb[i * 3 + c] = rgba[i * 4 + c];
		const std::vector<float> window = cut(rgb, w, 3, b);
		std::string error;
		ok = write_exr_image(path, window.data(), b.x1 - b.x0, b.y1 - b.y0, error);
	}
	std::free(rgba);
	return ok;
}

}  // namespace output_crop
