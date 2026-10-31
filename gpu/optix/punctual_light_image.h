#pragma once
// punctual_light_image.h -- the picture of a projector light (its slide) or a goniometric light (its profile) for the GPU scene builder (pbrt_gpu_builder.h).

#include <cstdlib>
#include <string>
#include <vector>

#include "../../src/external/stb_image.h"
#include "../../src/external/tinyexr.h"

// A punctual light's image (a projector's slide, a goniometric profile) as linear RGB floats: an .exr through tinyexr - stb_image cannot read it, and an .exr slide or
// profile fell back to a plain light on both GPU renderers - anything else through stb_image, as the CPU's decodePunctualLightImageFile (pbrt_cpu_stages.h) does.
inline bool loadPunctualLightImage(const std::string &path, std::vector<float> &rgb, int &w, int &h) {
	w = h = 0;
	if (path.size() >= 4 && (path.compare(path.size() - 4, 4, ".exr") == 0 || path.compare(path.size() - 4, 4, ".EXR") == 0)) {
		float *rgba = nullptr;
		const char *err = nullptr;
		const int rc = LoadEXR(&rgba, &w, &h, path.c_str(), &err);
		if (err) FreeEXRErrorMessage(err);
		if (rc != TINYEXR_SUCCESS || !rgba || w <= 0 || h <= 0) {
			if (rgba) free(rgba);
			w = h = 0;
			return false;
		}
		rgb.resize(static_cast<std::size_t>(w) * h * 3);
		for (std::size_t i = 0; i < static_cast<std::size_t>(w) * h; ++i)
			for (int k = 0; k < 3; ++k) rgb[i * 3 + k] = rgba[i * 4 + k];
		free(rgba);
		return true;
	}
	int channels = 0;
	float *data = stbi_loadf(path.c_str(), &w, &h, &channels, 3);
	if (!data || w <= 0 || h <= 0) {
		if (data) stbi_image_free(data);
		w = h = 0;
		return false;
	}
	rgb.assign(data, data + static_cast<std::size_t>(w) * h * 3);
	stbi_image_free(data);
	return true;
}
