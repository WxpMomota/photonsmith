#pragma once
// cartoon_still.h -- one frame of a cartoon film (`ray_tracer ... cartoon:fireflies@13.5`) as a temporary scene file.
//
// The launcher renders a scene file, so the frame's scene text is written to a file in a folder of its own in the system temp folder, and removed again when the run ends. A run that is
// killed (the GUI's Stop button, a crash) never gets to remove it, so the next one sweeps away the folders left by earlier runs once they are old enough that no render can still be using them.

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include "../src/shared/cartoon/cartoons.h"

namespace cartoon_still {

constexpr const char *kFolderPrefix = "photonsmith_cartoon_";

// Removes the folders of earlier runs under `root` that are older than `age` (a run takes minutes at most; the default is hours).
inline void sweepStale(const std::filesystem::path &root, std::chrono::hours age = std::chrono::hours(6)) {
	std::error_code ec;
	const auto now = std::filesystem::file_time_type::clock::now();
	for (const auto &entry : std::filesystem::directory_iterator(root, ec)) {
		if (ec) break;
		if (!entry.is_directory(ec) || entry.path().filename().string().compare(0, std::string(kFolderPrefix).size(), kFolderPrefix) != 0) continue;
		const auto written = std::filesystem::last_write_time(entry.path(), ec);
		if (!ec && now - written > age) std::filesystem::remove_all(entry.path(), ec);
	}
}

// The scene file of one frame. The file is named cartoon-<key>-frame.pbrt, so the scene it becomes is not mistaken for the film's own still in the scene list, and it carries
// an @rt-name tag with the film and time, which is what the scene is called in the log.
struct SceneFile {
	std::filesystem::path dir, path;
	SceneFile() = default;
	SceneFile(const SceneFile &) = delete;
	SceneFile &operator=(const SceneFile &) = delete;
	~SceneFile() {
		std::error_code ec;
		if (!dir.empty()) std::filesystem::remove_all(dir, ec);
	}

	// Writes the frame's scene under `root`; false when it cannot be written.
	bool write(const cartoon::Still &still, const std::filesystem::path &root) {
		sweepStale(root);
		std::error_code ec;
		dir = root / (std::string(kFolderPrefix) + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		if (!std::filesystem::create_directories(dir, ec)) return false;
		path = dir / (std::string("cartoon-") + still.film->key + "-frame.pbrt");
		std::ofstream out(path, std::ios::binary);
		out << "# @rt-name " << still.film->name << " at " << still.t << " s\n" << still.film->sceneAt(still.t);
		return static_cast<bool>(out);
	}
};

}  // namespace cartoon_still
