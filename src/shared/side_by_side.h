#pragma once
// side_by_side.h -- a picture made of two renders side by side, for teaching one technique: the left half with it turned down, the right half as the render was asked for.
// Each half is the whole view (the same camera, at half the picture's width), so the two can be compared thing by thing. A scene asks for it with a header line (before
// WorldBegin) - a film writes it into every frame (`sidebyside depth N`, film_file.h) - and the launcher renders the halves and joins them (launcher/side_by_side_frame.h).
//
//   # @rt-sidebyside depth 1      the left half at ray depth 1 (only the light that reaches a surface straight from a light: no bounces), the right at the render's own
//
// In the scene's text, so a frame that asks for it has its own fingerprint, and a resumed video renders it again when the line changes.

#include <cstdlib>
#include <fstream>
#include <string>

namespace side_by_side {

inline std::string line(int depth) { return "# @rt-sidebyside depth " + std::to_string(depth) + "\n"; }

// The left half's ray depth a scene's text asks for, or -1 when it does not ask for two halves.
inline int depthOf(const std::string &text) {
	const std::size_t worldBegin = text.find("WorldBegin");
	const std::string tag = "# @rt-sidebyside depth ";
	const std::size_t at = text.find(tag);
	if (at == std::string::npos || (worldBegin != std::string::npos && at > worldBegin)) return -1;
	const int v = std::atoi(text.c_str() + at + tag.size());
	return v >= 1 ? v : -1;
}
// The same for a scene file, reading only its header (a frame's scene can be many megabytes).
inline int depthOfFile(const std::string &path) {
	std::ifstream in(path, std::ios::binary);
	if (!in) return -1;
	std::string header, row;
	while (std::getline(in, row)) {
		header += row + "\n";
		if (row.find("WorldBegin") != std::string::npos || header.size() > (1u << 20)) break;
	}
	return depthOf(header);
}

}  // namespace side_by_side
