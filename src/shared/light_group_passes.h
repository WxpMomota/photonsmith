#pragma once
// light_group_passes.h -- writes a finished picture's light-group passes (light_groups.h): into the picture's own EXR beside its R, G, B, or beside a PNG or PPM
// picture into <picture>.lightgroups.exr. Linear and before any look or tone map (a compositor adds them up and grades them). Used by the CPU path tracer
// (camera_render.h) and the GPU one (optix_interface.cpp).

#include "exr_writer.h"
#include "light_groups.h"

#include <string>
#include <vector>

namespace light_groups {

// rgb: the picture's w*h*3 linear floats, read only when `exr` (the picture is an EXR, rewritten with the passes); groups: names.size() pictures of w*h*3 floats,
// one after another, multiplied by `scale` (the picture's exposure) as they are written. Returns the file written, or "" with `error` saying why not.
inline std::string writePasses(const std::string &picture, bool exr, int w, int h, const float *rgb, const std::vector<std::string> &names, const float *groups,
							   float scale, std::string &error) {
	const std::size_t plane = static_cast<std::size_t>(w) * h;
	std::vector<std::string> channels;
	std::vector<std::vector<float>> planes;
	if (exr) {
		for (int c = 0; c < 3; ++c) {
			channels.push_back(c == 0 ? "R" : c == 1 ? "G" : "B");
			planes.emplace_back(plane);
			for (std::size_t p = 0; p < plane; ++p) planes.back()[p] = rgb[p * 3 + c];
		}
	}
	for (std::size_t g = 0; g < names.size(); ++g) {
		const std::vector<std::string> ch = channelsOf(names[g]);
		for (int c = 0; c < 3; ++c) {
			channels.push_back(ch[static_cast<std::size_t>(c)]);
			planes.emplace_back(plane);
			for (std::size_t p = 0; p < plane; ++p) planes.back()[p] = groups[g * plane * 3 + p * 3 + c] * scale;
		}
	}
	const std::string path = exr ? picture : picture + ".lightgroups.exr";
	return write_exr_channels(path, w, h, channels, planes, error) ? path : std::string();
}

}  // namespace light_groups
