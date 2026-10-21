#pragma once
// cartoon_stills.h -- one still picture of every cartoon film (cartoons.h) as an ordinary scene file, so the scene list can show the films.
//
// A film is a function of time, not a scene, and the scene system lists scenes that are files. The bridge is a folder of generated files: ensure() writes
// "cartoon-<key>.pbrt" for every film into it (the film's still frame, with header tags that name it, put it in the Cartoons category and describe it), and
// pbrt_discover.h scans the folder like any other. Nothing else in the scene system knows about cartoons; the stills render, preview and open in the Scene
// Builder like any pbrt file. The film itself stays a video preset ("Cartoon: ..." in Video Settings).
//
// A file is written again only when its text would change: its first line is a hash of the text that follows (`# @rt-cartoon-hash`), so editing a film or the toolkit refreshes the still
// without anyone bumping a number, and a build of the launcher and one of the GUI that share a source write the same file. The files of films that no longer exist are removed.

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
constexpr const char *kFilePrefix = "cartoon-";

inline std::string fileName(const cartoon::Film &film) { return std::string(kFilePrefix) + film.key + ".pbrt"; }

// The film a still's displayed name ("Fireflies": the @rt-name tag of its file) belongs to, or null. The link is the name rather than the scene's slug, which the
// scene list changes ("cartoon-fireflies-2") when a scene of the user's has the same file name.
inline const cartoon::Film *filmForName(const std::string &name) {
	for (const cartoon::Film &film : cartoon::kFilms)
		if (name == film.name) return &film;
	return nullptr;
}

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

// The scene file's text after the hash line: the tags, then the frame at stillTime.
inline std::string body(const cartoon::Film &film) {
	char at[32];
	std::snprintf(at, sizeof(at), "%g", film.stillTime);
	std::string out = std::string("# @rt-name ") + film.name + "\n";
	out += "# @rt-category Cartoons\n";
	out += std::string("# @rt-description ") + film.description + " This picture is the frame at " + at + " seconds; the whole film is a video preset (Video Settings, \"Cartoon: " +
		   film.name + "\").\n";
	out += film.sceneAt(film.stillTime);
	return out;
}

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

// Removes the generated stills of films that are no longer in kFilms (a renamed or dropped film would otherwise stay in the scene list for good).
inline void pruneUnknown(const std::string &dir) {
	std::error_code ec;
	for (const auto &entry : std::filesystem::directory_iterator(dir, ec)) {
		if (ec) break;
		const std::string name = entry.path().filename().string();
		if (name.compare(0, std::string(kFilePrefix).size(), kFilePrefix) != 0 || entry.path().extension() != ".pbrt") continue;
		bool known = false;
		for (const cartoon::Film &film : cartoon::kFilms) known = known || name == fileName(film);
		if (!known) std::filesystem::remove(entry.path(), ec);
	}
}

// Writes the still of every film that is missing or out of date into `dir` (made if need be) and removes the ones nothing makes any more. Failures are ignored: a
// folder that cannot be written only means the Cartoons category is empty, and several processes (the GUI, the renderer it starts, the preview library) may do this at
// once, so each file is written under a private name and moved into place.
inline void ensure(const std::string &dir) {
	std::error_code ec;
	std::filesystem::create_directories(dir, ec);
	pruneUnknown(dir);
	for (const cartoon::Film &film : cartoon::kFilms) {
		const std::filesystem::path path = std::filesystem::path(dir) / fileName(film);
		const std::string b = body(film);
		const std::string hash = hashOf(b);
		if (upToDate(path, hash)) continue;
		const std::filesystem::path tmp = path.string() + ".tmp" + std::to_string(std::random_device{}());
		{
			std::ofstream out(tmp, std::ios::binary);
			out << kHashTag << hash << "\n" << b;
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
