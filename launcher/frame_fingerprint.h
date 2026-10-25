#pragma once
// frame_fingerprint.h -- what a cartoon frame's picture is made from, as a short hash: its scene text and the files the scene names (a film's cached meshes, its painted skies and
// textures). Two frames with the same fingerprint render the same picture (at the same size, samples and flags: the video's own resume signature, cartoon_render.h), so a resumed
// video keeps every frame whose fingerprint has not changed and renders only the others - after a film is edited, only the shots that changed are rendered again.
//
// The film cache's build folder names (build_XXXXXXXXXXXXXXXX: film_cache.h names them after the program that made the files) change with every build without changing a picture, so
// they are left out of the text; what is in the files they hold is folded in instead, so a mesh that a rebuilt program makes differently is still noticed.

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>

namespace frame_fingerprint {

inline std::uint64_t fnv(const char *data, std::size_t size, std::uint64_t h = 14695981039346656037ull) {
	for (std::size_t i = 0; i < size; ++i) {
		h ^= static_cast<unsigned char>(data[i]);
		h *= 1099511628211ull;
	}
	return h;
}
inline bool isHex(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }

// The hash of a file's contents, read once per process for each path, size and time (a film's ground mesh is tens of megabytes and every frame names it). 0 when it cannot be read.
inline std::uint64_t fileHash(const std::string &path) {
	static std::mutex lock;
	static std::map<std::string, std::uint64_t> known;
	std::error_code ec;
	const auto size = std::filesystem::file_size(path, ec);
	if (ec) return 0;
	const auto when = std::filesystem::last_write_time(path, ec);
	const std::string key = path + "|" + std::to_string(static_cast<unsigned long long>(size)) + "|" + std::to_string(static_cast<long long>(when.time_since_epoch().count()));
	{
		std::lock_guard<std::mutex> hold(lock);
		if (const auto it = known.find(key); it != known.end()) return it->second;
	}
	std::ifstream in(path, std::ios::binary);
	if (!in) return 0;
	std::uint64_t h = 14695981039346656037ull;
	std::string buffer(1 << 20, '\0');
	while (in) {
		in.read(&buffer[0], static_cast<std::streamsize>(buffer.size()));
		h = fnv(buffer.data(), static_cast<std::size_t>(in.gcount()), h);
	}
	std::lock_guard<std::mutex> hold(lock);
	known[key] = h;
	return h;
}

// The fingerprint of a frame's scene text (16 hex digits).
inline std::string of(const std::string &scene) {
	std::uint64_t h = 14695981039346656037ull;
	// the text, the build folders' names left out
	std::size_t from = 0;
	for (std::size_t at = scene.find("build_"); at != std::string::npos; at = scene.find("build_", at + 6)) {
		bool stamp = at + 22 <= scene.size();
		for (std::size_t k = at + 6; stamp && k < at + 22; ++k) stamp = isHex(scene[k]);
		if (!stamp) continue;
		h = fnv(scene.data() + from, at + 6 - from, h);
		from = at + 22;
	}
	h = fnv(scene.data() + from, scene.size() - from, h);
	// the files it names: each one's contents
	const std::string tag = "\"string filename\"";
	for (std::size_t at = scene.find(tag); at != std::string::npos; at = scene.find(tag, at + tag.size())) {
		const std::size_t open = scene.find('"', scene.find('[', at + tag.size()));
		if (open == std::string::npos) break;
		const std::size_t close = scene.find('"', open + 1);
		if (close == std::string::npos) break;
		const std::uint64_t f = fileHash(scene.substr(open + 1, close - open - 1));
		h = fnv(reinterpret_cast<const char *>(&f), sizeof f, h);
	}
	char out[17];
	std::snprintf(out, sizeof out, "%016llx", static_cast<unsigned long long>(h));
	return out;
}

}  // namespace frame_fingerprint
