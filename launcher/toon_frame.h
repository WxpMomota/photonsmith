#pragma once
// toon_frame.h -- the toon look (src/shared/toon_look.h) on a picture just rendered, in the run that rendered it: the scene's passes first (a cheap first-hit pass on the CPU,
// cpu_render_aov_planes, so it works whatever rendered the picture), then the look on the picture's file - a PPM from the CPU and OptiX renderers, a PNG from Metal - in
// place, before it is cut to the scene's picture or turned into a PNG. A film's frames get it the same way, each in its own run (a frame's scene carries the film's
// `# @rt-toon` line, and --toon is passed on to every frame's run).

#include <algorithm>
#include <string>
#include <vector>

#include "../cpu_renderer/cpu_interface.h"
#include "../src/external/image_writer.h"
#include "../src/external/stb_image.h"
#include "../src/shared/output_crop.h"
#include "../src/shared/toon_look.h"

namespace toon_frame {

// The look for a picture of the scene file `sceneFile`: its own `# @rt-toon` line turns it on with its style; --toon (`forced`) turns it on with that style or the default.
inline toon_look::Style styleFor(bool forced, const std::string &sceneFile, std::string *problem = nullptr) {
	toon_look::Style style;
	std::string why;
	if (!sceneFile.empty() && toon_look::fromSceneFile(sceneFile, style, why) && !why.empty()) {
		if (problem) *problem = why;
		style = toon_look::Style();
	}
	if (forced) style.on = true;
	return style;
}

// The look on the picture at `imagePath` (width x height, as rendered from `sceneId`): true when it was made, else false with why.
inline bool apply(const std::string &imagePath, int width, int height, const toon_look::Style &style, const std::string &sceneId, double camX, double camY, double camZ, std::string &why) {
	// the picture: a PPM, or a PNG (Metal writes one, whatever the name)
	int w = 0, h = 0, maxValue = 255;
	std::vector<int> samples;
	bool ppm = output_crop::readPpm(imagePath, w, h, maxValue, samples);
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
	if (w != width || h != height) {
		why = "the picture is " + std::to_string(w) + " x " + std::to_string(h) + ", not the " + std::to_string(width) + " x " + std::to_string(height) + " rendered";
		return false;
	}
	// its passes, from the scene as its file is now (a film's worker rewrites one scene file for each frame: the CPU would otherwise keep the first frame's)
	cpu_reload_scene_file(sceneId.c_str());
	const std::size_t count = static_cast<std::size_t>(w) * h;
	const std::size_t planeCount = style.needsIds() ? 12 : 10;   // (lines between materials or objects: the two ID planes too)
	std::vector<float> flat(count * planeCount);
	if (const int err = cpu_render_aov_planes(w, h, 16, sceneId.c_str(), camX, camY, camZ, 0, flat.data(), style.needsIds() ? 1 : 0); err != 0) {
		why = "the passes could not be made (error " + std::to_string(err) + ")";
		return false;
	}
	std::vector<std::vector<float>> planes(planeCount);
	for (std::size_t p = 0; p < planeCount; ++p) planes[p].assign(flat.begin() + static_cast<std::ptrdiff_t>(p * count), flat.begin() + static_cast<std::ptrdiff_t>((p + 1) * count));
	// the look, on the linear picture
	std::vector<float> rgb(count * 3);
	for (std::size_t i = 0; i < rgb.size(); ++i) rgb[i] = static_cast<float>(toon_look::srgbToLinear(static_cast<double>(samples[i]) / maxValue));
	toon_look::apply(rgb, w, h, planes, style);
	for (std::size_t i = 0; i < rgb.size(); ++i) samples[i] = static_cast<int>(std::lround(toon_look::linearToSrgb(rgb[i]) * maxValue));
	if (ppm) {
		if (!output_crop::writePpm(imagePath, w, h, maxValue, samples)) {
			why = "the picture cannot be written";
			return false;
		}
		return true;
	}
	std::vector<unsigned char> bytes(samples.begin(), samples.end());
	if (!write_png(imagePath.c_str(), w, h, bytes.data())) {
		why = "the picture cannot be written";
		return false;
	}
	return true;
}

}  // namespace toon_frame
