#pragma once
// output_format.h -- what a render's output FILE is, from its name and its bytes.
//
// The renderers choose the format from the file name: ".exr" is linear HDR (exr_writer.h's is_exr_output_path), ".png" is a PNG written straight from the picture's bytes (the GPU renderer,
// with the writer the launcher hands it in OutputOptions), anything else is a text PPM. The launcher reads the file's own first bytes to see whether it is a PNG already.

#include <cctype>
#include <fstream>
#include <string>

// Whether `path` ends in .png (any case).
inline bool is_png_output_path(const std::string &path) {
	if (path.size() < 4) return false;
	const std::string ext = path.substr(path.size() - 4);
	return ext[0] == '.' && std::tolower(static_cast<unsigned char>(ext[1])) == 'p' && std::tolower(static_cast<unsigned char>(ext[2])) == 'n' && std::tolower(static_cast<unsigned char>(ext[3])) == 'g';
}

// Whether the file at `path` is a PNG (its first eight bytes are the PNG signature), whatever its name: a renderer that was asked for a .png may have written a text PPM into it.
inline bool file_is_png(const std::string &path) {
	std::ifstream in(path, std::ios::binary);
	if (!in) return false;
	unsigned char head[8] = {};
	in.read(reinterpret_cast<char *>(head), sizeof(head));
	static const unsigned char kSignature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
	if (in.gcount() != static_cast<std::streamsize>(sizeof(head))) return false;
	for (int i = 0; i < 8; ++i)
		if (head[i] != kSignature[i]) return false;
	return true;
}
