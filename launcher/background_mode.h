#pragma once
// background_mode.h -- the operating-system side of --background (and the GUI's "Keep the PC responsive while rendering"): a render that is a polite guest, for someone who wants to use the
// computer while it runs. WHAT to do is decided by RunPolicy (run_policy.h: the workers, the frames at once, the stall limit, the pause, the threads); this does it.
//
// A lower priority alone does not give the graphics card back: Windows shares it between programs' compute work in turns whatever their priority class (measured: a normal job beside a
// background job took 57 s for what it does alone in 33 s, the same as beside another normal job). So a render that is only a guest also leaves gaps: after each frame it pauses for as long
// as the frame took (a duty of 50%), which is what lets a game or an editor have the card for half the time. Changing it: RAY_TRACER_BACKGROUND_DUTY=percent (10 to 100).
//
// apply() changes, for this process and every program it starts (they are given the flag too, and on Windows inherit the priority class):
//   * CPU priority below normal, and a low memory priority, so the rest of the system is served first when it is busy;
//   * the GPU scheduling class of the process lowered (Windows: D3DKMTSetProcessSchedulingPriorityClass), which favours the desktop's own drawing over this render's work; it does NOT give the
//     card to another program that is doing compute or heavy graphics (see above: measured, no effect), only the pauses do that;
//   * half of the CPU cores for a CPU render (unless RAY_TRACER_THREADS says otherwise).
// The pauses (pauseAfter) make a video take about twice as long even when nothing else is running; the priorities cost nothing then.

#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>

#include "run_policy.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/resource.h>
#endif

namespace background_mode {

#ifdef _WIN32
// D3DKMT_SCHEDULINGPRIORITYCLASS: IDLE 0, BELOW_NORMAL 1, NORMAL 2 ... (d3dkmthk.h, which is in the driver kit, so the function is looked up in gdi32 instead).
inline bool lowerGpuSchedulingClass() {
	using SetClassFn = LONG(WINAPI *)(HANDLE, int);
	const HMODULE gdi = LoadLibraryA("gdi32.dll");
	if (!gdi) return false;
	const SetClassFn set = reinterpret_cast<SetClassFn>(reinterpret_cast<void *>(GetProcAddress(gdi, "D3DKMTSetProcessSchedulingPriorityClass")));
	return set && set(GetCurrentProcess(), 1) >= 0;   // (an NTSTATUS: negative is a failure)
}
#endif

// Applies the process-wide parts of `policy` (nothing outside background mode). Safe to call more than once. Returns what was done, for the run's log.
inline std::string apply(const RunPolicy &policy) {
	if (!policy.background) return std::string();
	std::string done;
#ifdef _WIN32
	if (SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS)) done += "CPU priority below normal";
	// (ProcessMemoryPriority = 0 for SetProcessInformation; MEMORY_PRIORITY_BELOW_NORMAL = 4)
	struct MemoryPriority { ULONG MemoryPriority; } mp{4};
	if (SetProcessInformation(GetCurrentProcess(), ProcessMemoryPriority, &mp, sizeof(mp))) done += ", low memory priority";
	if (lowerGpuSchedulingClass()) done += ", GPU scheduling class below normal";
#else
	if (setpriority(PRIO_PROCESS, 0, 10) == 0) done += "nice 10";
#endif
	if (!std::getenv("RAY_TRACER_THREADS")) {
		if (const unsigned int threads = policy.cpuThreads(std::thread::hardware_concurrency())) {
			const std::string s = std::to_string(threads);
#ifdef _WIN32
			_putenv_s("RAY_TRACER_THREADS", s.c_str());
#else
			setenv("RAY_TRACER_THREADS", s.c_str(), 1);
#endif
			done += ", " + s + " CPU threads";
		}
	}
	done += ", paused " + std::to_string(100 - policy.dutyPercent) + "% of the time between frames";
	return done;
}

// Sleeps for policy.pauseSecondsAfter(workSeconds): nothing outside background mode.
inline void pauseAfter(const RunPolicy &policy, double workSeconds) {
	const double s = policy.pauseSecondsAfter(workSeconds);
	if (s > 0.0) std::this_thread::sleep_for(std::chrono::duration<double>(s));
}

}  // namespace background_mode
