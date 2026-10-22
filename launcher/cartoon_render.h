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
#include <deque>
#include <map>
#include <memory>
#include <set>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "process.h"
#include "background_mode.h"
#include "launcher_args.h"
#include "../src/shared/cartoon/cartoons.h"

namespace cartoon_render {

// The flags of the parent run that a frame's run should repeat: everything the parser consumed except the video-only flags and the output path.
inline std::vector<std::string> forwardedFlags(const LaunchArgs &args) {
	std::vector<std::string> out;
	const std::vector<std::string> &t = args.consumed_tokens;
	for (std::size_t i = 0; i < t.size(); ++i) {
		bool drop = false;
		for (const VideoFlag &f : kVideoFlags) {
			if (t[i] == f.name) {
				drop = true;
				if (f.takesValue) ++i;   // its value too
				break;
			}
		}
		if (!drop) out.push_back(t[i]);
	}
	return out;
}

// The flags a frame run repeats: the user's, and the film's own renderer when the film names one (cartoons.h, FilmMeta::wavefront) and the user did not choose.
inline std::vector<std::string> frameFlags(const LaunchArgs &args, const cartoon::Film &film, bool gpu) {
	std::vector<std::string> flags = forwardedFlags(args);
	if (gpu && film.meta.wavefront && !args.use_wavefront) flags.push_back("--wavefront");
	flags.push_back("--ppm-only");   // (the video makes the PNG of every frame itself: video_render.h, BackgroundPngConverter)
	return flags;
}

// The arguments (after the program) that render `scenePath` to `framePath` at the parent's size and quality: the flags the user gave, then the size, the samples and the scene.
// `timeLimitSeconds` > 0 is this frame's own time limit (the CPU renderer's --time-limit).
inline std::vector<std::string> frameArgs(const std::vector<std::string> &flags, int width, int height, int spp, int depth, const std::string &framePath, const std::string &scenePath,
										 double timeLimitSeconds = 0.0) {
	std::vector<std::string> a = flags;
	if (timeLimitSeconds > 0.0) {
		a.push_back("--time-limit");
		a.push_back(std::to_string(timeLimitSeconds));
	}
	for (const std::string &s : {std::string("--height"), std::to_string(height), std::string("--output"), framePath, std::to_string(width), std::to_string(spp), std::to_string(depth), scenePath}) a.push_back(s);
	return a;
}

// How long one frame may take: --cartoon-timeout S, else the environment variable RAY_TRACER_CARTOON_TIMEOUT, else 15 minutes on a GPU (a frame there takes seconds, so one that has taken
// fifteen minutes is stuck) and never on the CPU (a CPU frame at a high quality can take that long and more). 0 means never.
inline int timeoutFor(const LaunchArgs &args, bool gpu) {
	if (args.cartoon_timeout >= 0) return args.cartoon_timeout;
	if (const char *env = std::getenv("RAY_TRACER_CARTOON_TIMEOUT")) return std::max(0, std::atoi(env));
	return gpu ? 900 : 0;
}

// Writes the frame's scene, runs the child, removes the scene file. Returns the child's exit code (0 = SUCCESS), or -1 when the scene file could not be written or the child could not be
// started or did not finish in `timeoutSeconds` (it is stopped and tried once more first). `scenePath` is in a folder of its own (one per worker): the child's scene list reads that
// folder and nothing else (RAY_TRACER_PBRT_DIR), so that it does not read the 200-odd scene files of pbrt_scenes/ (about half a second of every frame) to render this one.
inline int renderFrame(const cartoon::Film &film, double t, const std::filesystem::path &scenePath, const std::string &exe, const LaunchArgs &args, int width, int height, int spp,
					   int depth, const std::string &framePath, int timeoutSeconds = 0, double timeLimitSeconds = 0.0, bool gpu = false) {
	try {
		std::ofstream f(scenePath, std::ios::binary);
		if (!f) return -1;
		f << film.sceneAt(t);
	} catch (const std::exception &) {   // (a film's code failing on one frame fails that frame, not the whole video)
		return -1;
	}
	const std::vector<std::string> childArgs = frameArgs(frameFlags(args, film, gpu), width, height, spp, depth, framePath, scenePath.string(), timeLimitSeconds);
	const auto frameStart = std::chrono::steady_clock::now();
	int rc = -1;
	for (int attempt = 0; attempt < 2; ++attempt) {
		bool timedOut = false;
		rc = runChild(exe, childArgs, "RAY_TRACER_PBRT_DIR", scenePath.parent_path().string(), timeoutSeconds, &timedOut);
		if (!timedOut) break;
		std::cerr << "\nWARNING: " << std::filesystem::path(framePath).filename().string() << " took longer than " << timeoutSeconds << " s and was stopped" << (attempt == 0 ? "; trying it once more." : ".") << std::endl;
		rc = -1;
	}
	std::error_code ec;
	std::filesystem::remove(scenePath, ec);
	background_mode::pauseAfter(std::chrono::duration<double>(std::chrono::steady_clock::now() - frameStart).count());   // (--background)
	return rc;
}

// ---- resuming an interrupted video ----
// What identifies a cartoon video's frames: the film (its scene text at two instants, so that a change to the film's code is noticed), the size, the samples, the depth, the frame count and
// every flag given, and the program itself (its file's size and time, so that a renderer that has been rebuilt does not continue the frames of the old one). Written beside the frames;
// --resume continues from them only when it is the same.
inline std::string resumeSignature(const cartoon::Film &film, const LaunchArgs &args, int frames, int fps, int width, int height, int spp, int depth, const std::string &exePath = std::string()) {
	std::string s = std::string(film.key) + "|" + std::to_string(frames) + "|" + std::to_string(fps) + "|" + std::to_string(width) + "x" + std::to_string(height) + "|" + std::to_string(spp) + "|" +
					std::to_string(depth);
	for (const std::string &f : forwardedFlags(args)) s += "|" + f;
	if (!exePath.empty()) {
		std::error_code ec;
		const auto size = std::filesystem::file_size(exePath, ec);
		const auto when = std::filesystem::last_write_time(exePath, ec);
		s += "|" + std::to_string(static_cast<unsigned long long>(size)) + "|" + std::to_string(static_cast<long long>(when.time_since_epoch().count()));
	}
	const std::hash<std::string> h;
	s += "|" + std::to_string(h(film.sceneAt(0.0))) + "|" + std::to_string(h(film.sceneAt(film.duration * 0.5)));
	return s;
}
inline std::string encName(int index) {
	char name[32];
	std::snprintf(name, sizeof(name), "enc_%04d.png", index);
	return name;
}
// The pictures of frames 0 .. count-1 that exist in `dir`.
inline int countFrames(const std::filesystem::path &dir, int count) {
	int n = 0;
	for (int i = 0; i < count; ++i)
		if (std::filesystem::exists(dir / encName(i))) ++n;
	return n;
}
// A frame that failed leaves a hole in the sequence, which would shorten the video and put its sound out of step; each hole is filled with the frame before it (the first with the next
// one). Returns how many were filled.
inline int fillMissingFrames(const std::filesystem::path &dir, int count) {
	int first = -1;
	for (int i = 0; i < count && first < 0; ++i)
		if (std::filesystem::exists(dir / encName(i))) first = i;
	if (first < 0) return 0;
	int filled = 0;
	std::error_code ec;
	for (int i = 0; i < count; ++i) {
		if (std::filesystem::exists(dir / encName(i))) continue;
		const int from = i == 0 ? first : i - 1;
		std::filesystem::copy_file(dir / encName(from), dir / encName(i), std::filesystem::copy_options::overwrite_existing, ec);
		if (!ec) ++filled;
	}
	return filled;
}

// The soundtrack. A film with one (Film::soundtrack, cartoon_audio.h) has it mixed and written as a 16-bit stereo .wav; the video then gets it. It is made for the film's own length and
// frame rate, so it is only added to a video that has them (a shorter or faster one would not fit it).
inline bool soundtrackFits(const cartoon::Film &film, int frames, int fps) { return film.soundtrack != nullptr && frames == cartoon::frameCountOf(film) && fps == film.fps; }
inline bool writeSoundtrack(const cartoon::Film &film, const std::filesystem::path &wavPath) {
	if (!film.soundtrack) return false;
	cartoon::audio::Mix mix(film.duration);
	film.soundtrack(mix);
	return cartoon::audio::writeWav(wavPath.string(), mix.finish());
}

// ---- long-lived workers (cartoon_worker.h) ----
// How many worker processes render a cartoon video's frames: --cartoon-workers N, else the environment variable RAY_TRACER_CARTOON_WORKERS, else two on an OptiX GPU and none elsewhere
// (the CPU renders every frame with all its cores, so a process start is nothing to it; Metal frames are rendered by a process each). 0 means a process per frame.
inline int workersFor(const LaunchArgs &args, bool gpu, bool metal) {
	int n = args.cartoon_workers;
	if (n < 0) {
		if (const char *env = std::getenv("RAY_TRACER_CARTOON_WORKERS")) n = std::atoi(env);
	}
	if (n < 0) n = gpu && !metal ? (args.background ? 1 : 2) : 0;   // (--background: one worker keeps a GPU nearly as busy and leaves the rest to the computer's user)
	return std::max(0, std::min(n, 4));
}

// What the pool of workers needs from the outside (so that it can be tested without processes): a way to start a worker on a list of frames, to see whether it still runs and to stop it,
// and to ask whether a frame's marker is in.
struct WorkerHandle {
	virtual ~WorkerHandle() = default;
	virtual bool running() = 0;
	virtual void kill() = 0;
};
struct WorkerHooks {
	std::function<std::unique_ptr<WorkerHandle>(int worker, const std::vector<int> &frames)> start;   // null: it could not be started
	std::function<bool(int frame, int &result)> poll;   // true and the frame's result when its marker is in; the marker is used up
};

// Renders `todo` (frame numbers) with `workers` long-lived workers: worker w is given every workers-th frame and renders its list in order. The pool watches the markers the workers
// leave; `completed(frame, result, seconds)` is called as each frame is finished (0 is success) and `inOrder(frame, result)` once it and every frame of `todo` before it are, which is what
// the video's picture sequence needs. A worker that has not finished a frame in `stallSeconds` (0: never) is killed, and one that ends with frames left is replaced: a new worker takes
// the rest of the list, starting with the frame that was in hand; a frame that has taken a worker down twice fails (result -1) and the new worker starts after it. Returns the number of
// frames whose result was 0.
inline int runWorkers(std::vector<int> todo, int workers, int stallSeconds, const WorkerHooks &hooks, const std::function<void(int frame, int result, double seconds)> &completed,
					  const std::function<void(int frame, int result)> &inOrder, int pollMilliseconds = 25) {
	using Clock = std::chrono::steady_clock;
	std::sort(todo.begin(), todo.end());
	if (todo.empty()) return 0;
	workers = std::max(1, std::min(workers, static_cast<int>(todo.size())));
	struct Worker {
		std::deque<int> remaining;
		std::unique_ptr<WorkerHandle> handle;
		std::chrono::steady_clock::time_point last = std::chrono::steady_clock::now();   // (not the function's `Clock` alias: MSVC does not find a function-local alias inside a local struct)
	};
	std::vector<Worker> ws(static_cast<std::size_t>(workers));
	for (std::size_t i = 0; i < todo.size(); ++i) ws[i % ws.size()].remaining.push_back(todo[i]);
	std::map<int, int> result, attempts;
	std::set<int> finished;
	std::size_t nextToHand = 0, reported = 0;
	int succeeded = 0;
	auto seconds = [](Clock::time_point since) { return std::chrono::duration<double>(Clock::now() - since).count(); };
	auto report = [&](int frame, int rc, double took) {
		result[frame] = rc;
		finished.insert(frame);
		if (rc == 0) ++succeeded;
		++reported;
		completed(frame, rc, took);
		while (nextToHand < todo.size() && finished.count(todo[nextToHand]) > 0) {
			inOrder(todo[nextToHand], result[todo[nextToHand]]);
			++nextToHand;
		}
	};
	while (reported < todo.size()) {
		for (std::size_t w = 0; w < ws.size(); ++w) {
			Worker &worker = ws[w];
			if (worker.remaining.empty()) continue;
			if (!worker.handle) {
				worker.last = Clock::now();
				worker.handle = hooks.start(static_cast<int>(w), std::vector<int>(worker.remaining.begin(), worker.remaining.end()));
				if (!worker.handle) {   // it could not be started: the frame in hand fails
					report(worker.remaining.front(), -1, 0.0);
					worker.remaining.pop_front();
					continue;
				}
			}
			int rc = 0;
			while (!worker.remaining.empty() && hooks.poll(worker.remaining.front(), rc)) {
				report(worker.remaining.front(), rc, seconds(worker.last));
				worker.remaining.pop_front();
				worker.last = Clock::now();
			}
			if (worker.remaining.empty()) continue;
			const bool ended = !worker.handle->running();
			if (ended && hooks.poll(worker.remaining.front(), rc)) {   // (its last marker came just before it ended)
				report(worker.remaining.front(), rc, seconds(worker.last));
				worker.remaining.pop_front();
				worker.last = Clock::now();
				worker.handle.reset();
				continue;
			}
			const bool stalled = stallSeconds > 0 && seconds(worker.last) > stallSeconds;
			if (ended || stalled) {
				if (stalled) worker.handle->kill();
				worker.handle.reset();
				const int frame = worker.remaining.front();
				if (++attempts[frame] >= 2) {
					std::cerr << "\nWARNING: frame " << frame << " " << (stalled ? "took longer than " + std::to_string(stallSeconds) + " s" : std::string("ended its worker")) << " twice and is given up." << std::endl;
					report(frame, -1, seconds(worker.last));
					worker.remaining.pop_front();
				} else {
					std::cerr << "\nWARNING: a worker " << (stalled ? "did not finish frame " + std::to_string(frame) + " in " + std::to_string(stallSeconds) + " s" : "ended while rendering frame " + std::to_string(frame)) << "; starting another." << std::endl;
				}
				worker.last = Clock::now();
			}
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(pollMilliseconds));
	}
	for (Worker &w : ws)
		if (w.handle && w.handle->running()) w.handle->kill();   // (none should be left running: all their frames are in)
	return succeeded;
}

// How many frames are rendered at once: --cartoon-jobs N, else the environment variable RAY_TRACER_CARTOON_JOBS, else two on a GPU and one on the CPU (a CPU render already uses every
// core, so running several only has them compete). Between 1 and 8.
inline int jobsFor(const LaunchArgs &args, bool gpu) {
	int n = args.cartoon_jobs;
	if (n <= 0) {
		if (const char *env = std::getenv("RAY_TRACER_CARTOON_JOBS")) n = std::atoi(env);
	}
	if (n <= 0) n = gpu && !args.background ? 2 : 1;
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
