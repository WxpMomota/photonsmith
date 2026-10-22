#pragma once
// cartoon_render.h -- the frames of a cartoon video preset (src/shared/cartoon/cartoons.h), rendered by child launcher processes, a few at a time.
//
// A cartoon changes the SCENE from frame to frame, and the renderers keep a loaded scene (and its built copy) for the life of a process, so the frames cannot be rendered by one process
// the way a camera path is. Each frame instead becomes an ordinary single-image run of a .pbrt file: the launcher writes the frame's scene text, runs itself on it with the flags the
// user gave (backend, denoiser, seed, exposure ...; the video-only flags and the output path are replaced), and removes the file.
//
// A frame costs a second or two of work that is not rendering (a process start, the GPU context, reading the scene, writing the picture), and the GPU is idle while that happens. So
// several children run at once (jobsFor: two on a GPU by default): while one renders, the others start up or finish, which keeps the GPU busy. On the Newton's Cradle at 1280 x 720 two
// children took a 12-frame video from 48 s to 39 s, three to 34 s. The pictures come back in any order; runInParallel hands them on in frame order, which the video needs.

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "launcher_args.h"
#include "../src/shared/cartoon/cartoons.h"

namespace cartoon_render {

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
	static const char *const dropWithValue[] = {"--frames", "-f", "--fps", "--speed", "--camera-path", "-p", "--video-preset", "--camera-keyframes", "--output", "-o", "--height", "--cartoon-jobs"};
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

// The command line that renders `scenePath` to `framePath` at the parent's size and quality. The child sees only `sceneDir` as its scene folder (RAY_TRACER_PBRT_DIR), so its scene list
// does not read the 200-odd scene files of pbrt_scenes/ (about half a second of every frame) when all it renders is this one file.
inline std::string frameCommand(const std::string &exe, const std::vector<std::string> &flags, int width, int height, int spp, int depth, const std::string &framePath,
								const std::string &scenePath, const std::string &sceneDir = std::string()) {
	std::string cmd = quote(exe);
	for (const std::string &f : flags) cmd += " " + quote(f);
	cmd += " --height " + std::to_string(height) + " --output " + quote(framePath);
	cmd += " " + std::to_string(width) + " " + std::to_string(spp) + " " + std::to_string(depth) + " " + quote(scenePath);
	if (!sceneDir.empty()) {
#ifdef _WIN32
		cmd = "set " + quote("RAY_TRACER_PBRT_DIR=" + sceneDir) + " && " + cmd;
#else
		cmd = "RAY_TRACER_PBRT_DIR=" + quote(sceneDir) + " " + cmd;
#endif
	}
#ifdef _WIN32
	cmd = "\"" + cmd + "\"";   // cmd.exe strips one pair of outer quotes
#endif
	return cmd;
}

// Writes the frame's scene, runs the child, removes the scene file. Returns the child's exit code (0 = SUCCESS), or -1 when the scene file could not be written. `scenePath` is in a
// folder of its own (one per worker): the child's scene list reads that folder and nothing else.
inline int renderFrame(const cartoon::Film &film, double t, const std::filesystem::path &scenePath, const std::string &exe, const LaunchArgs &args, int width, int height, int spp,
					   int depth, const std::string &framePath) {
	{
		std::ofstream f(scenePath, std::ios::binary);
		if (!f) return -1;
		f << film.sceneAt(t);
	}
	const std::string cmd = frameCommand(exe, forwardedFlags(args), width, height, spp, depth, framePath, scenePath.string(), scenePath.parent_path().string());
	const int rc = std::system(cmd.c_str());
	std::error_code ec;
	std::filesystem::remove(scenePath, ec);
	return rc;
}

// How many frames are rendered at once: --cartoon-jobs N, else the environment variable RAY_TRACER_CARTOON_JOBS, else two on a GPU and one on the CPU (a CPU render already uses every
// core, so running several only has them compete). Between 1 and 8.
inline int jobsFor(const LaunchArgs &args, bool gpu) {
	int n = args.cartoon_jobs;
	if (n <= 0) {
		if (const char *env = std::getenv("RAY_TRACER_CARTOON_JOBS")) n = std::atoi(env);
	}
	if (n <= 0) n = gpu ? 2 : 1;
	return std::max(1, std::min(n, 8));
}

// Renders frames 0 .. count-1 with up to `jobs` at a time. `render(frame, worker)` renders one and returns its result code (the worker number, 0 .. jobs-1, says which folder to use:
// no two frames in flight share one). `completed(frame, result, seconds)` is called as each frame finishes, in whatever order that happens, and `inOrder(frame, result)` once the frame
// and every frame before it have finished, in frame order: what the video's picture sequence needs. Both callbacks are called with a lock held, so they need not be thread-safe.
// Returns the number of frames whose result was 0.
inline int runInParallel(int count, int jobs, const std::function<int(int frame, int worker)> &render, const std::function<void(int frame, int result, double seconds)> &completed,
						 const std::function<void(int frame, int result)> &inOrder) {
	std::mutex lock;
	int next = 0, nextToHand = 0, succeeded = 0;
	std::vector<int> result(static_cast<std::size_t>(std::max(count, 0)), 1);
	std::vector<char> finished(result.size(), 0);
	auto work = [&](int worker) {
		for (;;) {
			int frame;
			{
				std::lock_guard<std::mutex> g(lock);
				if (next >= count) return;
				frame = next++;
			}
			const auto start = std::chrono::steady_clock::now();
			const int rc = render(frame, worker);
			const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
			std::lock_guard<std::mutex> g(lock);
			result[static_cast<std::size_t>(frame)] = rc;
			finished[static_cast<std::size_t>(frame)] = 1;
			if (rc == 0) ++succeeded;
			completed(frame, rc, seconds);
			while (nextToHand < count && finished[static_cast<std::size_t>(nextToHand)]) {
				inOrder(nextToHand, result[static_cast<std::size_t>(nextToHand)]);
				++nextToHand;
			}
		}
	};
	const int n = std::max(1, std::min(jobs, count));
	std::vector<std::thread> threads;
	for (int w = 1; w < n; ++w) threads.emplace_back(work, w);
	work(0);
	for (std::thread &t : threads) t.join();
	return succeeded;
}

}  // namespace cartoon_render
