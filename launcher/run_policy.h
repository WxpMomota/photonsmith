#pragma once
// run_policy.h -- how much of the computer a run may use, decided in ONE place.
//
// --background (the GUI's "Keep the PC responsive while rendering", or RAY_TRACER_BACKGROUND=1) used to be decided at six call sites (the number of workers, the frames at once, the stall
// limit, the pause after each frame, the CPU threads, the process priority), and a change to one without the others was a bug (a pause the stall watchdog counted as a hang). Every one of
// those decisions is a method of RunPolicy now, computed once from the arguments; background_mode.h does what needs the operating system (priorities, sleeping).
//
// Pure: no operating-system calls, so it is unit-tested without side effects.

#include <algorithm>
#include <cstdlib>
#include <string>

#include "launcher_args.h"

// What one frame of a cartoon video asks of the GPU, for choosing how many frames to keep in flight: its pixels, its samples per pixel, and the GPU memory that is free when the video
// starts (megabytes; 0 = not known). No pixels = not known: the fixed defaults apply.
struct FrameLoad {
	long long pixels = 0;
	int spp = 0;
	long long freeVramMb = 0;
};

struct RunPolicy {
	// True in background mode: a render that is a polite guest on the computer (background_mode.h).
	bool background = false;
	// True in smart mode (the GUI's Power "Smart", --smart-power): full power while the computer is free, a background render's pauses while it is busy (smart_power.h says which). Its
	// workers and frames at once are full power's; its priorities are background mode's (which cost nothing on a free computer).
	bool smart = false;
	// In smart mode, how many frames the video renders at once (RAY_TRACER_SMART_FRAMES, which the video sets for its workers and frame runs): on a busy computer each pauses that
	// many times as long, so that together they keep to the duty.
	int smartFrames = 1;
	static constexpr const char *kSmartFramesVariable = "RAY_TRACER_SMART_FRAMES";
	// The share of the time a background render (or a smart one on a busy computer) may keep the machine busy, in percent (100 otherwise). After work that took W seconds it pauses
	// W * (100 - duty) / duty.
	int dutyPercent = 100;

	// From the arguments and the environment (RAY_TRACER_BACKGROUND=1, or =auto for smart mode; RAY_TRACER_BACKGROUND_DUTY=percent, 10 to 100).
	static RunPolicy from(const LaunchArgs &args) {
		RunPolicy p;
#ifdef _MSC_VER
#pragma warning(suppress : 4996)
#endif
		const char *env = std::getenv("RAY_TRACER_BACKGROUND");
		const bool envAuto = env && std::string(env) == "auto";
		p.background = args.background || (env && *env && std::string(env) != "0" && !envAuto);
		p.smart = !p.background && (args.smart_power || envAuto);
#ifndef _WIN32
		if (p.smart) p.smart = false, p.background = true;   // (the signs of a busy computer are read on Windows only: elsewhere Smart is always polite)
#endif
		if (p.background || p.smart) {
			p.dutyPercent = 50;
#ifdef _MSC_VER
#pragma warning(suppress : 4996)
#endif
			if (const char *duty = std::getenv("RAY_TRACER_BACKGROUND_DUTY")) p.dutyPercent = (std::max)(10, (std::min)(std::atoi(duty), 100));
		}
		if (p.smart) {
#ifdef _MSC_VER
#pragma warning(suppress : 4996)
#endif
			if (const char *frames = std::getenv(kSmartFramesVariable)) p.smartFrames = (std::max)(1, (std::min)(std::atoi(frames), 16));
		}
		return p;
	}

	// How many frames a GPU keeps in flight for a frame of this load, at full power. A light frame (few pixels times samples) spends much of its time outside the GPU - building and
	// loading its scene, denoising, saving - which more frames in flight hide; a heavy one keeps the GPU busy on its own, and more frames would only take memory. Measured on an RTX 5080
	// with The Great Turtle Escape, 24 frames each (2026-10-23): 120p at 16 samples 9.3 s two at a time, 7.6 s four; 480p at 16: 11.9 s, 10.3 s; 480p at 64: 25.4 s, 22.7 s; 1080p at 16:
	// 24.3 s, 22.7 s three at a time; 1080p at 64: 141 s, 140 s with three (48 frames); 1080p at 256: 274 s two, 275 s three. And each frame in flight holds memory of its own, about
	// 0.72 GB + 1.75 KB a pixel with the wavefront renderer (5 GB at 1080p): never more frames than fit in what is free (a card that runs out pages and slows to a crawl).
	static int framesInFlight(const FrameLoad &load) {
		const double work = static_cast<double>(load.pixels) * load.spp;   // samples per frame
		int n = work <= 3.0e7 ? 4 : (work <= 8.0e7 ? 3 : 2);
		if (load.freeVramMb > 0) {
			const double perFrameMb = 1000.0 + static_cast<double>(load.pixels) * 0.0018;   // (a margin over the measured 0.72 GB + 1.75 KB a pixel, for bigger scenes)
			const int fit = static_cast<int>((static_cast<double>(load.freeVramMb) - 1024.0) / perFrameMb);
			n = (std::min)(n, (std::max)(1, fit));
		} else {
			n = (std::min)(n, 2);   // memory not known: no more than the old fixed two
		}
		return n;
	}

	// How many worker processes render a cartoon video's frames when nobody chose: on an OptiX GPU as many as frames in flight (framesInFlight; two when the load is not known; one in
	// background mode, which keeps a GPU nearly as busy and leaves the rest to the computer's user), none elsewhere (the CPU renders every frame with all its cores; Metal frames are
	// rendered by a process each).
	int defaultWorkers(bool gpu, bool metal, const FrameLoad &load = {}) const {
		if (!gpu || metal) return 0;
		if (background) return 1;
		return load.pixels > 0 ? framesInFlight(load) : 2;
	}

	// How many frames of a cartoon video are rendered at once when nobody chose (the same, and one on the CPU).
	int defaultFrameJobs(bool gpu, const FrameLoad &load = {}) const {
		if (!gpu || background) return 1;
		return load.pixels > 0 ? framesInFlight(load) : 2;
	}

	// The limit a frame (or a worker between two finished frames) may take: the watchdog measures the time since a worker's last finished frame and a background worker pauses after every
	// frame, so the limit grows by the same factor as the pauses (twice at 50%; a smart one's pauses are smartFrames times longer); 0 (never) stays 0.
	int stallSeconds(int seconds) const {
		if ((!background && !smart) || seconds <= 0) return seconds;
		const long long share = smart ? smartFrames : 1;
		return static_cast<int>((std::min)(static_cast<long long>(seconds) * 100 * share / dutyPercent, 24LL * 3600));
	}

	// How long to pause after work that took `workSeconds`, to keep to the duty: in background mode always, in smart mode when the computer is `busy` (smart_power.h; then as one of
	// smartFrames frames at once: W * (100 * frames - duty) / duty), else never.
	double pauseSecondsAfter(double workSeconds, bool busy = true) const {
		if (!(background || (smart && busy))) return 0.0;
		const double share = smart ? smartFrames : 1;
		return (std::max)(0.0, workSeconds * (100.0 * share - dutyPercent) / dutyPercent);
	}

	// The CPU threads a render may use when the user has not chosen a number (RAY_TRACER_THREADS): half of the cores in background mode; 0 = no limit of ours.
	unsigned cpuThreads(unsigned hardwareThreads) const {
		if (!background) return 0;
		return hardwareThreads >= 4 ? hardwareThreads / 2 : 1;
	}
};
