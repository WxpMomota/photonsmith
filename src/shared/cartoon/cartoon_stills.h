#pragma once
// cartoon_stills.h -- one still picture of every cartoon film (cartoons.h) as an ordinary scene file, so the scene list can show the films.
//
// A film is a function of time, not a scene, and the scene system lists scenes that are files. The scene list therefore describes a film's still from its header alone (headerText(): the
// tags that name it and put it in the Cartoons category, then the camera and picture setup up to WorldBegin, which costs next to nothing), and the file itself is written the first time
// something needs to load it (ensureOne(), through pbrt_discover::materializeGenerated). The stills render, preview and open in the Scene Builder like any pbrt file; the film
// itself stays a video preset ("Cartoon: ..." in Video Settings).
//
// A file is written again only when its text would change: its first line is a hash of the text that follows (`# @rt-cartoon-hash`), so editing a film or the toolkit refreshes the still
// without anyone bumping a number, and two binaries that share a source write the same file. The files of films that no longer exist are removed.

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <system_error>

#include "cartoons.h"

namespace cartoon_stills {

constexpr const char *kHashTag = "# @rt-cartoon-hash ";
inline std::string fileName(const cartoon::Film &film) { return cartoon::stillSlugOf(film) + ".pbrt"; }

// 64-bit FNV-1a of `text`, as 16 hex digits.
inline std::string hashOf(const std::string &text) {
	std::uint64_t h = 14695981039346656037ull;
	for (unsigned char c : text) {
		h ^= c;
		h *= 1099511628211ull;
	}
	char out[17];
	std::snprintf(out, sizeof(out), "%016llx", static_cast<unsigned long long>(h));
	return out;
}

// The header tags of a still: its name, its category, the film it is of (`@rt-cartoon`, which is what links the listed scene back to the film) and a description.
inline std::string tags(const cartoon::Film &film) {
	char at[32];
	std::snprintf(at, sizeof(at), "%g", film.stillTime);
	std::string out = std::string("# @rt-name ") + film.name + "\n";
	out += "# @rt-category Cartoons\n";
	out += std::string("# @rt-cartoon ") + film.key + "\n";
	out += std::string("# @rt-description ") + film.description + " This picture is the frame at " + at + " seconds; the whole film is a video preset (Video Settings, \"Cartoon: " +
		   film.name + "\").\n";
	return out;
}

// What the scene list needs to describe the still: the tags and the scene up to WorldBegin.
inline std::string headerText(const cartoon::Film &film) { return tags(film) + film.headerAt(film.stillTime); }

// The scene file's text after the hash line: the tags, then the frame at stillTime.
inline std::string body(const cartoon::Film &film) { return tags(film) + film.sceneAt(film.stillTime); }

// The whole file: the hash line, then body().
inline std::string text(const cartoon::Film &film) {
	const std::string b = body(film);
	return std::string(kHashTag) + hashOf(b) + "\n" + b;
}

// True when `path` exists and its hash line is `hash`. Only the head of the file is read.
inline bool upToDate(const std::filesystem::path &path, const std::string &hash) {
	std::ifstream in(path, std::ios::binary);
	if (!in) return false;
	std::string line;
	if (!std::getline(in, line)) return false;
	while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
	return line == std::string(kHashTag) + hash;
}

// Removes the generated stills of films that are no longer in kFilms (a renamed or dropped film would otherwise leave its file behind).
inline void pruneUnknown(const std::string &dir) {
	std::error_code ec;
	for (const auto &entry : std::filesystem::directory_iterator(dir, ec)) {
		if (ec) break;
		const std::string name = entry.path().filename().string();
		if (name.compare(0, std::string(cartoon::kStillSlugPrefix).size(), cartoon::kStillSlugPrefix) != 0 || entry.path().extension() != ".pbrt") continue;
		bool known = false;
		for (const cartoon::Film &film : cartoon::kFilms) known = known || name == fileName(film);
		if (!known) std::filesystem::remove(entry.path(), ec);
	}
}

// Writes the still of `film` into `dir` (made if need be) unless the file there is already current. Failures are ignored: the load that follows then reports the missing file. Several
// processes (the GUI, the renderer it starts, the preview library) may do this at once, so the file is written under a private name and moved into place.
inline void ensureOne(const std::string &dir, const cartoon::Film &film) {
	std::error_code ec;
	std::filesystem::create_directories(dir, ec);
	const std::filesystem::path path = std::filesystem::path(dir) / fileName(film);
	const std::string b = body(film);
	const std::string hash = hashOf(b);
	if (upToDate(path, hash)) return;
	const std::filesystem::path tmp = path.string() + ".tmp" + std::to_string(std::random_device{}());
	{
		std::ofstream out(tmp, std::ios::binary);
		out << kHashTag << hash << "\n" << b;
		if (!out) {
			out.close();
			std::filesystem::remove(tmp, ec);
			return;
		}
	}
	std::filesystem::rename(tmp, path, ec);
	if (ec) std::filesystem::remove(tmp, ec);
}

// Writes the still of every film that is missing or out of date into `dir`, and removes the ones nothing makes any more.
inline void ensure(const std::string &dir) {
	std::error_code ec;
	std::filesystem::create_directories(dir, ec);
	pruneUnknown(dir);
	for (const cartoon::Film &film : cartoon::kFilms) ensureOne(dir, film);
}

}  // namespace cartoon_stills
