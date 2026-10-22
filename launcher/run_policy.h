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

struct RunPolicy {
	// True in background mode: a render that is a polite guest on the computer (background_mode.h).
	bool background = false;
	// The share of the time a background render may keep the machine busy, in percent (100 outside background mode). After work that took W seconds it pauses W * (100 - duty) / duty.
	int dutyPercent = 100;

	// From the arguments and the environment (RAY_TRACER_BACKGROUND=1, RAY_TRACER_BACKGROUND_DUTY=percent, 10 to 100).
	static RunPolicy from(const LaunchArgs &args) {
		RunPolicy p;
#ifdef _MSC_VER
#pragma warning(suppress : 4996)
#endif
		const char *env = std::getenv("RAY_TRACER_BACKGROUND");
		p.background = args.background || (env && *env && std::string(env) != "0");
		if (p.background) {
			p.dutyPercent = 50;
#ifdef _MSC_VER
#pragma warning(suppress : 4996)
#endif
			if (const char *duty = std::getenv("RAY_TRACER_BACKGROUND_DUTY")) p.dutyPercent = (std::max)(10, (std::min)(std::atoi(duty), 100));
		}
		return p;
	}

	// How many worker processes render a cartoon video's frames when nobody chose: two on a GPU (one keeps a GPU nearly as busy and leaves the rest to the computer's user in background mode),
	// none elsewhere (the CPU renders every frame with all its cores; Metal frames are rendered by a process each).
	int defaultWorkers(bool gpu, bool metal) const { return gpu && !metal ? (background ? 1 : 2) : 0; }

	// How many frames of a cartoon video are rendered at once when nobody chose.
	int defaultFrameJobs(bool gpu) const { return gpu && !background ? 2 : 1; }

	// The limit a frame (or a worker between two finished frames) may take: the watchdog measures the time since a worker's last finished frame and a background worker pauses after every
	// frame, so the limit grows by the same factor as the pauses (twice at 50%); 0 (never) stays 0.
	int stallSeconds(int seconds) const {
		if (!background || seconds <= 0) return seconds;
		return static_cast<int>((std::min)(static_cast<long long>(seconds) * 100 / dutyPercent, 24LL * 3600));
	}

	// How long to pause after work that took `workSeconds`, to keep to the duty (0 outside background mode).
	double pauseSecondsAfter(double workSeconds) const {
		if (!background || dutyPercent >= 100) return 0.0;
		return workSeconds * (100 - dutyPercent) / dutyPercent;
	}

	// The CPU threads a render may use when the user has not chosen a number (RAY_TRACER_THREADS): half of the cores in background mode; 0 = no limit of ours.
	unsigned cpuThreads(unsigned hardwareThreads) const {
		if (!background) return 0;
		return hardwareThreads >= 4 ? hardwareThreads / 2 : 1;
	}
};
