#pragma once
// cartoon_render.h -- one frame of a cartoon video preset (src/shared/cartoon/cartoons.h), rendered by a child launcher process.
//
// A cartoon changes the SCENE from frame to frame, and the renderers keep a loaded scene (and its built copy) for the life of a process, so the frames cannot be rendered by one process
// the way a camera path is. Each frame instead becomes an ordinary single-image run of a .pbrt file: the launcher writes the frame's scene text, runs itself on it with the flags the
// user gave (backend, denoiser, seed, exposure ...; the video-only flags and the output path are replaced), and removes the file. The cost is a process start per frame, a second or two.

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "launcher_args.h"
#include "../src/shared/cartoon/cartoons.h"

namespace cartoon_render {

// Sets an environment variable for the life of the object and puts the old value back after it.
class ScopedEnv {
public:
	ScopedEnv(const char *name, const std::string &value) : name_(name) {
		if (const char *old = std::getenv(name)) {
			had_ = true;
			old_ = old;
		}
		set(value.c_str());
	}
	~ScopedEnv() {
		if (had_) set(old_.c_str());
		else unset();
	}
	ScopedEnv(const ScopedEnv &) = delete;
	ScopedEnv &operator=(const ScopedEnv &) = delete;

private:
	void set(const char *value) {
#ifdef _WIN32
		_putenv_s(name_.c_str(), value);
#else
		setenv(name_.c_str(), value, 1);
#endif
	}
	void unset() {
#ifdef _WIN32
		_putenv_s(name_.c_str(), "");
#else
		unsetenv(name_.c_str());
#endif
	}
	std::string name_, old_;
	bool had_ = false;
};

inline std::string quote(const std::string &s) {
	std::string q = "\"";
	for (char c : s) {
		if (c == '"') q += "\\\"";
		else q += c;
	}
	return q + "\"";
}

// The flags of the parent run that a frame's run should repeat: everything the parser consumed except the video-only flags and the output path.
inline std::vector<std::string> forwardedFlags(const LaunchArgs &args) {
	static const char *const dropWithValue[] = {"--frames", "-f", "--fps", "--speed", "--camera-path", "-p", "--video-preset", "--camera-keyframes", "--output", "-o", "--height"};
	std::vector<std::string> out;
	const std::vector<std::string> &t = args.consumed_tokens;
	for (std::size_t i = 0; i < t.size(); ++i) {
		bool drop = t[i] == "--video";
		for (const char *d : dropWithValue) {
			if (t[i] == d) {
				drop = true;
				++i;   // its value too
				break;
			}
		}
		if (!drop) out.push_back(t[i]);
	}
	return out;
}

// The command line that renders `scenePath` to `framePath` at the parent's size and quality.
inline std::string frameCommand(const std::string &exe, const std::vector<std::string> &flags, int width, int height, int spp, int depth, const std::string &framePath,
								const std::string &scenePath) {
	std::string cmd = quote(exe);
	for (const std::string &f : flags) cmd += " " + quote(f);
	cmd += " --height " + std::to_string(height) + " --output " + quote(framePath);
	cmd += " " + std::to_string(width) + " " + std::to_string(spp) + " " + std::to_string(depth) + " " + quote(scenePath);
#ifdef _WIN32
	cmd = "\"" + cmd + "\"";   // cmd.exe strips one pair of outer quotes
#endif
	return cmd;
}

// Writes the frame's scene, runs the child, removes the scene file. Returns the child's exit code (0 = SUCCESS), or -1 when the scene file could not be written.
inline int renderFrame(const cartoon::Film &film, double t, const std::filesystem::path &scenePath, const std::string &exe, const LaunchArgs &args, int width, int height, int spp,
					   int depth, const std::string &framePath) {
	{
		std::ofstream f(scenePath, std::ios::binary);
		if (!f) return -1;
		f << film.sceneAt(t);
	}
	const std::string cmd = frameCommand(exe, forwardedFlags(args), width, height, spp, depth, framePath, scenePath.string());
	// The child only renders this one file, so its scene list need not read the 200-odd scene files of pbrt_scenes/ (about half a second of every frame): pointing the scene folder
	// at the folder of this frame leaves it the frame and the film stills.
	const ScopedEnv onlyThisFrame("RAY_TRACER_PBRT_DIR", scenePath.parent_path().string());
	const int rc = std::system(cmd.c_str());
	std::error_code ec;
	std::filesystem::remove(scenePath, ec);
	return rc;
}

}  // namespace cartoon_render
