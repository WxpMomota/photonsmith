#pragma once
// side_by_side_frame.h -- joins the two halves of a side-by-side picture (src/shared/side_by_side.h): the left render and the right one, each the whole view at half the
// picture's width, into the picture asked for, with a thin pale line between them. A PPM (the CPU and OptiX renderers' own) or a PNG (a direct PNG, Metal) in, the same out.

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include "../src/external/image_writer.h"
#include "../src/external/stb_image.h"
#include "../src/shared/output_crop.h"

namespace side_by_side_frame {

struct Picture {
	int w = 0, h = 0, maxValue = 255;
	std::vector<int> samples;   // w x h x 3
	bool ppm = false;
};
inline bool read(const std::string &path, Picture &p) {
	if (output_crop::readPpm(path, p.w, p.h, p.maxValue, p.samples)) {
		p.ppm = true;
		return true;
	}
	int n = 0;
	unsigned char *px = stbi_load(path.c_str(), &p.w, &p.h, &n, 3);
	if (!px) return false;
	p.samples.assign(px, px + static_cast<std::size_t>(p.w) * p.h * 3);
	p.maxValue = 255;
	stbi_image_free(px);
	return true;
}

// A half's file beside the picture: "frame_0001.left.ppm" for "frame_0001.ppm".
inline std::string halfPath(const std::string &out, const char *side) {
	const std::filesystem::path p(out);
	return (p.parent_path() / (p.stem().string() + "." + side + p.extension().string())).string();
}

// The two halves (left.w + right.w wide, both as tall) side by side in `out`, a line `line` pixels wide between them; the halves' files are removed. False, with why, when
// either cannot be read or the picture cannot be written.
inline bool join(const std::string &left, const std::string &right, const std::string &out, std::string &why, int line = 4) {
	Picture a, b;
	if (!read(left, a) || !read(right, b)) {
		why = "a half could not be read";
		return false;
	}
	if (a.h != b.h) {
		why = "the halves are not the same height";
		return false;
	}
	const int w = a.w + b.w, h = a.h, mv = std::max(a.maxValue, b.maxValue);
	std::vector<int> joined(static_cast<std::size_t>(w) * h * 3);
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x) {
			const bool inLeft = x < a.w;
			const Picture &src = inLeft ? a : b;
			const int sx = inLeft ? x : x - a.w;
			for (int c = 0; c < 3; ++c) {
				const int v = src.samples[(static_cast<std::size_t>(y) * src.w + sx) * 3 + c];
				joined[(static_cast<std::size_t>(y) * w + x) * 3 + c] = src.maxValue == mv ? v : v * mv / std::max(1, src.maxValue);
			}
			if (x >= a.w - line / 2 && x < a.w - line / 2 + line)   // (the line between, centred on the join)
				for (int c = 0; c < 3; ++c) joined[(static_cast<std::size_t>(y) * w + x) * 3 + c] = mv * 9 / 10;
		}
	bool ok;
	if (a.ppm) {
		ok = output_crop::writePpm(out, w, h, mv, joined);
	} else {
		const std::vector<unsigned char> bytes(joined.begin(), joined.end());
		ok = write_png(out.c_str(), w, h, bytes.data());
	}
	if (!ok) {
		why = "the picture could not be written";
		return false;
	}
	std::error_code ec;
	std::filesystem::remove(left, ec);
	std::filesystem::remove(right, ec);
	return true;
}

}  // namespace side_by_side_frame
