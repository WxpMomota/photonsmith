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

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#endif

#include "launcher_args.h"
#include "../src/shared/cartoon/cartoons.h"

namespace cartoon_render {

// The flags of the parent run that a frame's run should repeat: everything the parser consumed except the video-only flags and the output path.
inline std::vector<std::string> forwardedFlags(const LaunchArgs &args) {
	static const char *const dropWithValue[] = {"--frames", "-f", "--fps", "--speed", "--camera-path", "-p", "--video-preset", "--camera-keyframes", "--output", "-o", "--height", "--cartoon-jobs", "--cartoon-timeout"};
	std::vector<std::string> out;
	const std::vector<std::string> &t = args.consumed_tokens;
	for (std::size_t i = 0; i < t.size(); ++i) {
		bool drop = t[i] == "--video" || t[i] == "--resume";
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

// The arguments (after the program) that render `scenePath` to `framePath` at the parent's size and quality: the flags the user gave, then the size, the samples and the scene.
inline std::vector<std::string> frameArgs(const std::vector<std::string> &flags, int width, int height, int spp, int depth, const std::string &framePath, const std::string &scenePath) {
	std::vector<std::string> a = flags;
	for (const std::string &s : {std::string("--height"), std::to_string(height), std::string("--output"), framePath, std::to_string(width), std::to_string(spp), std::to_string(depth), scenePath}) a.push_back(s);
	return a;
}

// A command-line argument as CreateProcess's parser wants it (quotes round anything with a space, backslashes before a quote doubled).
inline std::string windowsQuote(const std::string &s) {
	if (!s.empty() && s.find_first_of(" \t\"") == std::string::npos) return s;
	std::string r = "\"";
	std::size_t backslashes = 0;
	for (char c : s) {
		if (c == '\\') {
			++backslashes;
			r += c;
		} else if (c == '"') {
			r.append(backslashes + 1, '\\');
			backslashes = 0;
			r += '"';
		} else {
			backslashes = 0;
			r += c;
		}
	}
	r.append(backslashes, '\\');
	return r + "\"";
}

// Runs `exe args...` with one more environment variable (`envName`=`envValue`, when envName is not empty) and waits for it; when it has not finished after `timeoutSeconds` (0: wait as
// long as it takes) the process is killed and `*timedOut` set. Returns its exit code, or -1 when it could not be started. The child sees only its own environment: several children run at
// once, so a variable cannot be set in this process for one of them.
inline int runChild(const std::string &exe, const std::vector<std::string> &args, const std::string &envName, const std::string &envValue, int timeoutSeconds, bool *timedOut = nullptr) {
	if (timedOut) *timedOut = false;
#ifdef _WIN32
	std::string cmd = windowsQuote(exe);
	for (const std::string &a : args) cmd += " " + windowsQuote(a);
	std::string block;
	for (char **e = _environ; e && *e; ++e) {
		const std::string entry(*e);
		const bool same = !envName.empty() && entry.size() > envName.size() && _strnicmp(entry.c_str(), envName.c_str(), envName.size()) == 0 && entry[envName.size()] == '=';
		if (!same) block.append(entry).push_back('\0');
	}
	if (!envName.empty()) block.append(envName + "=" + envValue).push_back('\0');
	block.push_back('\0');
	STARTUPINFOA si{};
	si.cb = sizeof(si);
	PROCESS_INFORMATION pi{};
	std::vector<char> mutableCmd(cmd.begin(), cmd.end());
	mutableCmd.push_back('\0');
	if (!CreateProcessA(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE, 0, block.data(), nullptr, &si, &pi)) return -1;
	const DWORD waited = WaitForSingleObject(pi.hProcess, timeoutSeconds > 0 ? static_cast<DWORD>(timeoutSeconds) * 1000u : INFINITE);
	int rc = 1;
	if (waited == WAIT_TIMEOUT) {
		TerminateProcess(pi.hProcess, 1);
		WaitForSingleObject(pi.hProcess, 10000);
		if (timedOut) *timedOut = true;
	} else {
		DWORD code = 1;
		GetExitCodeProcess(pi.hProcess, &code);
		rc = static_cast<int>(code);
	}
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
	return rc;
#else
	std::vector<std::string> argvStore = {exe};
	argvStore.insert(argvStore.end(), args.begin(), args.end());
	std::vector<char *> argv;
	for (std::string &a : argvStore) argv.push_back(&a[0]);
	argv.push_back(nullptr);
	std::vector<std::string> envStore;
	for (char **e = environ; e && *e; ++e) {
		const std::string entry(*e);
		if (envName.empty() || entry.compare(0, envName.size() + 1, envName + "=") != 0) envStore.push_back(entry);
	}
	if (!envName.empty()) envStore.push_back(envName + "=" + envValue);
	std::vector<char *> envp;
	for (std::string &e : envStore) envp.push_back(&e[0]);
	envp.push_back(nullptr);
	pid_t pid = 0;
	if (posix_spawn(&pid, exe.c_str(), nullptr, nullptr, argv.data(), envp.data()) != 0) return -1;
	int status = 0;
	const auto start = std::chrono::steady_clock::now();
	for (;;) {
		const pid_t done = waitpid(pid, &status, WNOHANG);
		if (done == pid) break;
		if (done < 0) return -1;
		if (timeoutSeconds > 0 && std::chrono::steady_clock::now() - start > std::chrono::seconds(timeoutSeconds)) {
			kill(pid, SIGKILL);
			waitpid(pid, &status, 0);
			if (timedOut) *timedOut = true;
			return 1;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}
	return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
#endif
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
					   int depth, const std::string &framePath, int timeoutSeconds = 0) {
	try {
		std::ofstream f(scenePath, std::ios::binary);
		if (!f) return -1;
		f << film.sceneAt(t);
	} catch (const std::exception &) {   // (a film's code failing on one frame fails that frame, not the whole video)
		return -1;
	}
	const std::vector<std::string> childArgs = frameArgs(forwardedFlags(args), width, height, spp, depth, framePath, scenePath.string());
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
	return rc;
}

// ---- resuming an interrupted video ----
// What identifies a cartoon video's frames: the film (its scene text at two instants, so that a change to the film's code is noticed), the size, the samples, the depth, the frame count and
// every flag given. Written beside the frames; --resume continues from them only when it is the same.
inline std::string resumeSignature(const cartoon::Film &film, const LaunchArgs &args, int frames, int fps, int width, int height, int spp, int depth) {
	std::string s = std::string(film.key) + "|" + std::to_string(frames) + "|" + std::to_string(fps) + "|" + std::to_string(width) + "x" + std::to_string(height) + "|" + std::to_string(spp) + "|" +
					std::to_string(depth);
	for (const std::string &f : forwardedFlags(args)) s += "|" + f;
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
