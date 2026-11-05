#pragma once
// side_by_side.h -- a picture made of two renders side by side, for teaching one technique: the left half with it turned down, the right half as the render was asked for.
// Each half is the whole view (the same camera, at half the picture's width), so the two can be compared thing by thing. A scene asks for it with a header line (before
// WorldBegin) - a film writes it into every frame (`sidebyside ...`, film_file.h) - and the launcher renders the halves and joins them (launcher/side_by_side_frame.h).
//
//   # @rt-sidebyside depth 1      the left half at ray depth 1 (only the light that reaches a surface straight from a light: no bounces), the right at the render's own
//   # @rt-sidebyside samples 4    the left half at 4 samples (rays) a pixel, with no denoiser and no adaptive sampling: the Monte Carlo noise of so few rays, as it is
//   # @rt-sidebyside pinhole      the left half through a pinhole (lens radius 0: everything sharp), the right through the scene's own lens (its depth of field)
//
// In the scene's text, so a frame that asks for it has its own fingerprint, and a resumed video renders it again when the line changes.

#include <cstdlib>
#include <fstream>
#include <string>

namespace side_by_side {

// What the left half is rendered with instead of the render's own settings; nothing asked (asked() false): one picture.
struct Left {
	int depth = -1;         // its ray depth (1 and up), or -1: the render's own
	int samples = -1;       // its samples a pixel (1 and up, no denoiser, no adaptive sampling), or -1: the render's own
	bool pinhole = false;   // through a pinhole: no depth of field
	bool asked() const { return depth >= 1 || samples >= 1 || pinhole; }
	bool operator==(const Left &o) const { return depth == o.depth && samples == o.samples && pinhole == o.pinhole; }
	bool operator!=(const Left &o) const { return !(*this == o); }
};

inline std::string line(const Left &left) {
	if (left.depth >= 1) return "# @rt-sidebyside depth " + std::to_string(left.depth) + "\n";
	if (left.samples >= 1) return "# @rt-sidebyside samples " + std::to_string(left.samples) + "\n";
	if (left.pinhole) return "# @rt-sidebyside pinhole\n";
	return std::string();
}
inline std::string line(int depth) {
	Left left;
	left.depth = depth;
	return line(left);
}

// What a scene's text asks of the left half (asked() false when it does not ask for two halves; the line only counts in the header, before WorldBegin).
inline Left leftOf(const std::string &text) {
	Left left;
	const std::size_t worldBegin = text.find("WorldBegin");
	const std::string tag = "# @rt-sidebyside ";
	const std::size_t at = text.find(tag);
	if (at == std::string::npos || (worldBegin != std::string::npos && at > worldBegin)) return left;
	const std::size_t from = at + tag.size();
	const auto word = [&](const char *w) { return text.compare(from, std::string(w).size(), w) == 0; };
	if (word("depth ")) {
		const int v = std::atoi(text.c_str() + from + 6);
		if (v >= 1) left.depth = v;
	} else if (word("samples ")) {
		const int v = std::atoi(text.c_str() + from + 8);
		if (v >= 1) left.samples = v;
	} else if (word("pinhole")) {
		left.pinhole = true;
	}
	return left;
}
// The left half's ray depth a scene's text asks for, or -1 when it does not ask for two halves at another depth.
inline int depthOf(const std::string &text) { return leftOf(text).depth; }

// The same for a scene file, reading only its header (a frame's scene can be many megabytes).
inline Left leftOfFile(const std::string &path) {
	std::ifstream in(path, std::ios::binary);
	if (!in) return Left();
	std::string header, row;
	while (std::getline(in, row)) {
		header += row + "\n";
		if (row.find("WorldBegin") != std::string::npos || header.size() > (1u << 20)) break;
	}
	return leftOf(header);
}
inline int depthOfFile(const std::string &path) { return leftOfFile(path).depth; }

}  // namespace side_by_side
