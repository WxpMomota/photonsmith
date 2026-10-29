#pragma once
// marks_frame.h -- a picture's cartoon marks (src/shared/screen_marks.h: its scene's `# @rt-mark` lines, which a film writes into each frame) drawn onto the picture just
// rendered, in the run that rendered it: after the toon look (toon_frame.h), on the picture's file - a PPM from the CPU and OptiX renderers, a PNG from Metal or
// a direct PNG - in place, before it is cut to the scene's picture or turned into a PNG. And last of all the picture on paper or film (src/shared/print_look.h: halftone,
// watercolour, vintage), the same way.

#include <functional>
#include <string>
#include <vector>

#include "../src/external/image_writer.h"
#include "../src/external/stb_image.h"
#include "../src/shared/output_crop.h"
#include "../src/shared/print_look.h"
#include "../src/shared/screen_marks.h"

namespace marks_frame {

// The picture at `imagePath` changed as it is shown (sRGB, 0..1, w x h x 3) by `edit`, and written back: true when it was, else false with why.
inline bool editShown(const std::string &imagePath, const std::function<void(float *, int, int)> &edit, std::string &why) {
	int w = 0, h = 0, maxValue = 255;
	std::vector<int> samples;
	const bool ppm = output_crop::readPpm(imagePath, w, h, maxValue, samples);
	if (!ppm) {
		int n = 0;
		unsigned char *px = stbi_load(imagePath.c_str(), &w, &h, &n, 3);
		if (!px) {
			why = "the picture cannot be read";
			return false;
		}
		samples.assign(px, px + static_cast<std::size_t>(w) * h * 3);
		stbi_image_free(px);
	}
	std::vector<float> srgb(samples.size());
	for (std::size_t i = 0; i < srgb.size(); ++i) srgb[i] = static_cast<float>(samples[i]) / static_cast<float>(maxValue);
	edit(srgb.data(), w, h);
	for (std::size_t i = 0; i < srgb.size(); ++i) samples[i] = static_cast<int>(std::lround(std::clamp(srgb[i], 0.0f, 1.0f) * maxValue));
	if (ppm) {
		if (!output_crop::writePpm(imagePath, w, h, maxValue, samples)) {
			why = "the picture cannot be written";
			return false;
		}
		return true;
	}
	const std::vector<unsigned char> bytes(samples.begin(), samples.end());
	if (!write_png(imagePath.c_str(), w, h, bytes.data())) {
		why = "the picture cannot be written";
		return false;
	}
	return true;
}

// Draws `marks` on the picture at `imagePath`: true when they were drawn, else false with why.
inline bool apply(const std::string &imagePath, const std::vector<screen_marks::Mark> &marks, std::string &why) {
	return editShown(imagePath, [&](float *rgb, int w, int h) { screen_marks::draw(rgb, w, h, marks); }, why);
}

// The picture on paper or film (print_look.h) on the picture at `imagePath`.
inline bool print(const std::string &imagePath, const print_look::Params &p, std::string &why) {
	return editShown(imagePath, [&](float *rgb, int w, int h) { print_look::apply(rgb, w, h, p); }, why);
}

}  // namespace marks_frame
