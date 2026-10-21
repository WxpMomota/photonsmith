#pragma once
// cartoon_stills.h -- one still picture of every cartoon film (cartoons.h) as an ordinary scene file, so the scene list can show the films.
//
// A film is a function of time, not a scene, and the scene system lists scenes that are files. The bridge is a folder of generated files: ensure() writes
// "cartoon-<key>.pbrt" for every film into it (the film's still frame, with header tags that name it, put it in the Cartoons category and describe it), and
// pbrt_discover.h scans the folder like any other. Nothing else in the scene system knows about cartoons; the stills render, preview and open in the Scene
// Builder like any pbrt file. The film itself stays a video preset ("Cartoon: ..." in Video Settings).
//
// A file is written again only when its source changed: it carries the compiler's timestamp of the film's source and of the toolkit
// (`# @rt-cartoon-stamp`), so editing a film refreshes its still without anyone bumping a number, and a start-up that finds every file current reads four small headers.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <system_error>

#include "cartoons.h"

namespace cartoon_stills {

constexpr const char *kStampTag = "# @rt-cartoon-stamp ";

inline std::string fileName(const cartoon::Film &film) { return std::string("cartoon-") + film.key + ".pbrt"; }

// What the source of this film's still looks like to the compiler: its own file and the toolkit's.
inline std::string stampOf(const cartoon::Film &film) { return std::string(film.stamp) + " | " + cartoon::kLibStamp; }

// The scene file for the film's still: tags, then the frame at stillTime.
inline std::string text(const cartoon::Film &film) {
	char at[32];
	std::snprintf(at, sizeof(at), "%g", film.stillTime);
	std::string out = std::string(kStampTag) + stampOf(film) + "\n";
	out += std::string("# @rt-name ") + film.name + "\n";
	out += "# @rt-category Cartoons\n";
	out += std::string("# @rt-description ") + film.description + " This picture is the frame at " + at + " seconds; the whole film is a video preset (Video Settings, \"Cartoon: " +
		   film.name + "\").\n";
	out += film.sceneAt(film.stillTime);
	return out;
}

// True when `path` exists and says it was made from the source `stamp` stands for. Only the head of the file is read.
inline bool upToDate(const std::filesystem::path &path, const std::string &stamp) {
	std::ifstream in(path, std::ios::binary);
	if (!in) return false;
	std::string line;
	if (!std::getline(in, line)) return false;
	while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
	return line == std::string(kStampTag) + stamp;
}

// Writes the still of every film that is missing or out of date into `dir` (made if need be). Failures are ignored: a folder that cannot be written
// only means the Cartoons category is empty, and several processes (the GUI, the renderer it starts, the preview library) may do this at once, so each file is
// written under a private name and moved into place.
inline void ensure(const std::string &dir) {
	std::error_code ec;
	std::filesystem::create_directories(dir, ec);
	for (const cartoon::Film &film : cartoon::kFilms) {
		const std::filesystem::path path = std::filesystem::path(dir) / fileName(film);
		if (upToDate(path, stampOf(film))) continue;
		const std::filesystem::path tmp = path.string() + ".tmp" + std::to_string(std::random_device{}());
		{
			std::ofstream out(tmp, std::ios::binary);
			out << text(film);
			if (!out) {
				out.close();
				std::filesystem::remove(tmp, ec);
				continue;
			}
		}
		std::filesystem::rename(tmp, path, ec);
		if (ec) std::filesystem::remove(tmp, ec);
	}
}

}  // namespace cartoon_stills
