#pragma once
// background_mode.h -- --background (and the GUI's "Keep the PC responsive" setting): a render that is a polite guest, for someone who wants to use the computer while it runs.
//
// A lower priority alone does not give the graphics card back: Windows shares it between programs' compute work in turns whatever their priority class (measured: a normal job beside a
// background job took 57 s for what it does alone in 33 s, the same as beside another normal job). So a render that is only a guest also leaves gaps: after each frame it pauses for as long
// as the frame took (a duty of 50%), which is what lets a game or an editor have the card for half the time. Changing it: RAY_TRACER_BACKGROUND_DUTY=percent (10 to 100).
//
// What it changes, in the launcher and in every program the launcher starts (they are given the flag too, and on Windows inherit the priority class):
//   * CPU priority below normal, and a low memory priority, so the rest of the system is served first when it is busy;
//   * the GPU scheduling class of the process lowered (Windows: D3DKMTSetProcessSchedulingPriorityClass), so that the desktop, a browser or a game gets the GPU first when it wants it;
//   * half of the CPU cores for a CPU render (unless RAY_TRACER_THREADS says otherwise);
//   * one worker and one frame at a time for a cartoon video (cartoon_render.h: workersFor, jobsFor);
//   * the pause after each frame of a video (pauseAfter below).
// The pauses make a video take about twice as long even when nothing else is running; the priorities cost nothing then.

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/resource.h>
#endif

namespace background_mode {

// True when the environment asks for it (the GUI's setting is the flag; RAY_TRACER_BACKGROUND=1 is for scripts).
inline bool requestedByEnvironment() {
	const char *env = std::getenv("RAY_TRACER_BACKGROUND");
	return env && *env && std::string(env) != "0";
}

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

// Whether this process is in background mode (apply() was called).
inline bool &active() {
	static bool on = false;
	return on;
}

// The share of the time a background render may keep the machine busy, in percent.
inline int dutyPercent() {
	int duty = 50;
	if (const char *env = std::getenv("RAY_TRACER_BACKGROUND_DUTY")) duty = std::atoi(env);
	return std::max(10, std::min(duty, 100));
}

// How long to pause after work that took `workSeconds`, to keep to the duty: 0 outside background mode.
inline double pauseSecondsAfter(double workSeconds) {
	if (!active()) return 0.0;
	const int duty = dutyPercent();
	return duty >= 100 ? 0.0 : workSeconds * (100 - duty) / duty;
}

// Sleeps for pauseSecondsAfter(workSeconds).
inline void pauseAfter(double workSeconds) {
	const double s = pauseSecondsAfter(workSeconds);
	if (s > 0.0) std::this_thread::sleep_for(std::chrono::duration<double>(s));
}

// Applies the process-wide parts. Safe to call more than once. Returns what was done, for the run's log.
inline std::string apply() {
	active() = true;
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
		const unsigned int hw = std::thread::hardware_concurrency();
		const unsigned int half = hw >= 4 ? hw / 2 : 1;
		const std::string s = std::to_string(half);
#ifdef _WIN32
		_putenv_s("RAY_TRACER_THREADS", s.c_str());
#else
		setenv("RAY_TRACER_THREADS", s.c_str(), 1);
#endif
		done += ", " + s + " CPU threads";
	}
	done += ", paused " + std::to_string(100 - dutyPercent()) + "% of the time between frames";
	return done;
}

}  // namespace background_mode
