#pragma once
// smart_power.h -- the GUI's Power "Smart" (--smart-power, or RAY_TRACER_BACKGROUND=auto): a video renders at full power while the computer is free, and as a polite guest (the pauses of
// --background, background_mode.h) while it is busy. RunPolicy (run_policy.h) decides what a busy computer changes; this finds out whether it is busy.
//
// Busy (busyReason) means any of:
//   * a key or the mouse was used in the last minute (RAY_TRACER_SMART_IDLE_SECONDS) - someone is at the computer;
//   * Windows says a full-screen program, a game or a presentation is running (a film watched without touching anything);
//   * other programs keep one of the graphics card's engines 15% busy or more, or half of the processor (a game, a second render, a compile left running).
// A busy computer stays busy until 30 s after the last sign of it, so that a short lull does not start the card at full power under a game.
//
// One process watches: the one that renders the video (Monitor, every two seconds, started by start()). It says what it finds through a named event that the workers and frame runs it
// starts read (busyNow(); its name is in RAY_TRACER_SMART_EVENT, which they inherit). While the computer is busy each of the N frames rendered at once pauses N times as long as
// background mode's one would (RunPolicy::smartFrames), so that the duty holds for the graphics card as a whole - N workers each pausing as one would keep it as busy as ever. (Taking
// turns instead - one frame at a time under a lock - was tried and dropped: a frame that hangs holding the lock makes the healthy workers waiting for it look hung to the watchdog.)
// The decision is made as a frame ends: a frame already rendering when someone sits down is finished at full speed (at most one frame's time late), and a pause ends as soon as the
// computer is free again (pauseWhileBusy).
// Other systems than Windows do not read these signs: there Smart is Keep the PC responsive (run_policy.h).

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <iostream>
#include <mutex>
#include <unordered_map>
#include "process.h"   // (windows.h, and killOnExitJob: the workers and frame runs whose processor time is ours)
#include <shellapi.h>
#include <pdh.h>
#include <pdhmsg.h>
#endif

namespace smart_power {

// The environment variable that carries the watching process's event to the programs it starts.
constexpr const char *kEventVariable = "RAY_TRACER_SMART_EVENT";

// What the computer is doing, as last read.
struct Signals {
	double idleSeconds = 1e9;     // since a key or the mouse was last used
	bool fullScreen = false;      // a full-screen program, a game or a presentation
	double otherGpuPercent = 0;   // other programs' share of the graphics card's busiest engine
	double otherCpuPercent = 0;   // other programs' share of the processor (all its cores)
};

// When the computer counts as busy.
struct Limits {
	double idleSeconds = 60;
	double gpuPercent = 15;
	double cpuPercent = 50;
	double holdSeconds = 30;   // busy until this long after the last sign of it

	// The defaults, with RAY_TRACER_SMART_IDLE_SECONDS (5 to 3600) for the time after the last key or mouse movement.
	static Limits fromEnvironment() {
		Limits l;
#ifdef _MSC_VER
#pragma warning(suppress : 4996)
#endif
		if (const char *idle = std::getenv("RAY_TRACER_SMART_IDLE_SECONDS")) l.idleSeconds = (std::max)(5, (std::min)(std::atoi(idle), 3600));
		return l;
	}
};

// Why the computer is busy, in words for the log; empty when it is free.
inline std::string busyReason(const Signals &s, const Limits &l) {
	char percent[32];
	if (s.fullScreen) return "a full-screen program is running";
	if (s.idleSeconds < l.idleSeconds) return "someone is using it";
	if (s.otherGpuPercent >= l.gpuPercent) {
		std::snprintf(percent, sizeof(percent), "%.0f%%", s.otherGpuPercent);
		return std::string("another program keeps the graphics card ") + percent + " busy";
	}
	if (s.otherCpuPercent >= l.cpuPercent) {
		std::snprintf(percent, sizeof(percent), "%.0f%%", s.otherCpuPercent);
		return std::string("other programs keep the processor ") + percent + " busy";
	}
	return std::string();
}

// The verdict over time: busy at once on a sign of it, free again only `holdSeconds` after the last one.
class Decision {
  public:
	explicit Decision(const Limits &limits = Limits()) : limits_(limits) {}
	// Takes a reading at `now` (seconds, any origin). Returns true when the verdict changed.
	bool update(const Signals &s, double now) {
		const std::string why = busyReason(s, limits_);
		if (!why.empty()) {
			lastBusy_ = now;
			reason_ = why;
		}
		const bool busy = !why.empty() || (busy_ && now - lastBusy_ < limits_.holdSeconds);
		const bool changed = busy != busy_;
		busy_ = busy;
		return changed;
	}
	bool busy() const { return busy_; }
	// The last sign of a busy computer, in words.
	const std::string &reason() const { return reason_; }

  private:
	Limits limits_;
	bool busy_ = false;
	double lastBusy_ = -1e18;
	std::string reason_;
};

// One instance of Windows' "GPU Engine" counters is named like "pid_1234_luid_0x00000000_0x0000D1A3_phys_0_eng_0_engtype_3D": the process (1234) and, after it, the engine. Returns the
// process id, and `engine` gets the rest; -1 when the name is not of that form.
inline int pidOfEngineInstance(const std::string &name, std::string *engine = nullptr) {
	if (name.compare(0, 4, "pid_") != 0) return -1;
	std::size_t i = 4;
	long long pid = 0;
	while (i < name.size() && name[i] >= '0' && name[i] <= '9' && pid < (1LL << 32)) pid = pid * 10 + (name[i++] - '0');
	if (i == 4 || i >= name.size() || name[i] != '_' || pid >= (1LL << 32)) return -1;
	if (engine) *engine = name.substr(i + 1);
	return static_cast<int>(pid);
}

// The busiest engine's share, in percent, taken by the processes `ours` says are not ours: each engine's instances are summed over those processes, and the largest sum is returned.
template <class Ours>
double othersOnBusiestEngine(const std::vector<std::pair<std::string, double>> &instances, const Ours &ours) {
	std::vector<std::pair<std::string, double>> engines;   // (a few dozen engines: a list is plenty)
	for (const auto &[name, percent] : instances) {
		std::string engine;
		const int pid = pidOfEngineInstance(name, &engine);
		if (pid < 0 || ours(pid)) continue;
		auto it = std::find_if(engines.begin(), engines.end(), [&](const auto &e) { return e.first == engine; });
		if (it == engines.end()) engines.emplace_back(engine, percent);
		else it->second += percent;
	}
	double most = 0.0;
	for (const auto &e : engines) most = (std::max)(most, e.second);
	return (std::min)(most, 100.0);
}

#ifdef _WIN32

inline double secondsSinceInput() {
	LASTINPUTINFO info{sizeof(LASTINPUTINFO), 0};
	if (!GetLastInputInfo(&info)) return 1e9;
	return static_cast<DWORD>(GetTickCount() - info.dwTime) / 1000.0;   // (unsigned arithmetic: right across the tick count's 49-day wrap)
}

// True while Windows would hold back notifications for a full-screen program, a Direct3D game or a presentation (SHQueryUserNotificationState, looked up so that nothing more is linked).
inline bool fullScreenProgram() {
	using QueryFn = HRESULT(WINAPI *)(QUERY_USER_NOTIFICATION_STATE *);
	static const QueryFn query = [] {
		const HMODULE shell = LoadLibraryA("shell32.dll");
		return shell ? reinterpret_cast<QueryFn>(reinterpret_cast<void *>(GetProcAddress(shell, "SHQueryUserNotificationState"))) : nullptr;
	}();
	QUERY_USER_NOTIFICATION_STATE state{};
	if (!query || FAILED(query(&state))) return false;
	return state == QUNS_BUSY || state == QUNS_RUNNING_D3D_FULL_SCREEN || state == QUNS_PRESENTATION_MODE;
}

// Whether a process is one of ours: this program, under the same path (the workers and frame runs it starts are; so is another render of this program).
class OurProcesses {
  public:
	OurProcesses() : self_(pathOf(GetCurrentProcessId())) {}
	bool operator()(int pid) {
		if (static_cast<DWORD>(pid) == GetCurrentProcessId()) return true;
		if (const auto it = known_.find(pid); it != known_.end()) return it->second;
		if (known_.size() > 4096) known_.clear();
		const std::wstring path = pathOf(static_cast<DWORD>(pid));
		return known_[pid] = !path.empty() && _wcsicmp(path.c_str(), self_.c_str()) == 0;
	}
	// Forgets what it found (process ids are used again after a process ends).
	void forget() { known_.clear(); }

  private:
	static std::wstring pathOf(DWORD pid) {
		std::wstring path;
		if (HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
			wchar_t buffer[1024];
			DWORD n = 1024;
			if (QueryFullProcessImageNameW(h, 0, buffer, &n)) path.assign(buffer, n);
			CloseHandle(h);
		}
		return path;
	}
	std::wstring self_;
	std::unordered_map<int, bool> known_;
};

// Windows' "GPU Engine" performance counters (pdh.dll, looked up so that nothing more is linked): each engine's busy share per process since the last reading.
class GpuEngines {
  public:
	GpuEngines() {
		pdh_ = LoadLibraryA("pdh.dll");
		if (!pdh_) return;
		open_ = function<OpenFn>("PdhOpenQueryW");
		add_ = function<AddFn>("PdhAddEnglishCounterW");
		collect_ = function<CollectFn>("PdhCollectQueryData");
		array_ = function<ArrayFn>("PdhGetFormattedCounterArrayW");
		close_ = function<CloseFn>("PdhCloseQuery");
		if (!open_ || !add_ || !collect_ || !array_ || !close_ || open_(nullptr, 0, &query_) != ERROR_SUCCESS) {
			query_ = nullptr;
			return;
		}
		if (add_(query_, L"\\GPU Engine(*)\\Utilization Percentage", 0, &counter_) != ERROR_SUCCESS) counter_ = nullptr;
		else collect_(query_);   // (a rate: the first reading is the start of the first interval)
	}
	~GpuEngines() {
		if (query_) close_(query_);
	}
	GpuEngines(const GpuEngines &) = delete;
	GpuEngines &operator=(const GpuEngines &) = delete;

	// Other programs' share of the busiest engine since the last call, in percent; -1 when the counters cannot be read.
	template <class Ours>
	double others(Ours &ours) {
		if (!counter_ || collect_(query_) != ERROR_SUCCESS) return -1.0;
		DWORD bytes = 0, count = 0;
		if (array_(counter_, PDH_FMT_DOUBLE, &bytes, &count, nullptr) != static_cast<PDH_STATUS>(PDH_MORE_DATA) || bytes == 0) return count == 0 ? 0.0 : -1.0;
		std::vector<unsigned char> buffer(bytes);
		auto *items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W *>(buffer.data());
		if (array_(counter_, PDH_FMT_DOUBLE, &bytes, &count, items) != ERROR_SUCCESS) return -1.0;
		std::vector<std::pair<std::string, double>> instances;
		instances.reserve(count);
		for (DWORD i = 0; i < count; ++i) {
			if (items[i].FmtValue.CStatus != PDH_CSTATUS_VALID_DATA && items[i].FmtValue.CStatus != PDH_CSTATUS_NEW_DATA) continue;
			std::string name;
			for (const wchar_t *c = items[i].szName; c && *c; ++c) name += static_cast<char>(*c < 128 ? *c : '?');   // (the names are ASCII)
			instances.emplace_back(std::move(name), items[i].FmtValue.doubleValue);
		}
		return othersOnBusiestEngine(instances, [&](int pid) { return ours(pid); });
	}

  private:
	using OpenFn = PDH_STATUS(WINAPI *)(LPCWSTR, DWORD_PTR, PDH_HQUERY *);
	using AddFn = PDH_STATUS(WINAPI *)(PDH_HQUERY, LPCWSTR, DWORD_PTR, PDH_HCOUNTER *);
	using CollectFn = PDH_STATUS(WINAPI *)(PDH_HQUERY);
	using ArrayFn = PDH_STATUS(WINAPI *)(PDH_HCOUNTER, DWORD, LPDWORD, LPDWORD, PPDH_FMT_COUNTERVALUE_ITEM_W);
	using CloseFn = PDH_STATUS(WINAPI *)(PDH_HQUERY);
	template <class Fn>
	Fn function(const char *name) const {
		return reinterpret_cast<Fn>(reinterpret_cast<void *>(GetProcAddress(pdh_, name)));
	}
	HMODULE pdh_ = nullptr;
	OpenFn open_ = nullptr;
	AddFn add_ = nullptr;
	CollectFn collect_ = nullptr;
	ArrayFn array_ = nullptr;
	CloseFn close_ = nullptr;
	PDH_HQUERY query_ = nullptr;
	PDH_HCOUNTER counter_ = nullptr;
};

// Other programs' share of the processor since the last reading: the whole computer's busy time less this process's and that of the programs it started (their job object counts the
// time of the ones that have ended too, such as a frame run per frame).
class Processor {
  public:
	Processor() { read(busy_, ours_, total_); }
	// In percent; -1 when it cannot be read.
	double others() {
		unsigned long long busy = 0, ours = 0, total = 0;
		if (!read(busy, ours, total) || total <= total_) return -1.0;
		const double other = static_cast<double>(busy - busy_) - static_cast<double>(ours - ours_);
		const double share = 100.0 * (std::max)(0.0, other) / static_cast<double>(total - total_);
		busy_ = busy, ours_ = ours, total_ = total;
		return (std::min)(share, 100.0);
	}

  private:
	static unsigned long long ticks(const FILETIME &t) { return (static_cast<unsigned long long>(t.dwHighDateTime) << 32) | t.dwLowDateTime; }
	static bool read(unsigned long long &busy, unsigned long long &ours, unsigned long long &total) {
		FILETIME idle, kernel, user;   // (the system's kernel time includes its idle time)
		if (!GetSystemTimes(&idle, &kernel, &user)) return false;
		total = ticks(kernel) + ticks(user);
		busy = total - ticks(idle);
		ours = 0;
		FILETIME created, exited, ownKernel, ownUser;
		if (GetProcessTimes(GetCurrentProcess(), &created, &exited, &ownKernel, &ownUser)) ours += ticks(ownKernel) + ticks(ownUser);
		JOBOBJECT_BASIC_ACCOUNTING_INFORMATION job{};
		if (QueryInformationJobObject(killOnExitJob(), JobObjectBasicAccountingInformation, &job, sizeof(job), nullptr))
			ours += static_cast<unsigned long long>(job.TotalKernelTime.QuadPart + job.TotalUserTime.QuadPart);
		return true;
	}
	unsigned long long busy_ = 0, ours_ = 0, total_ = 0;
};

// Reads every sign at once. The rates (the graphics card, the processor) are over the time since the readers' last reading.
inline Signals read(GpuEngines &gpu, Processor &cpu, OurProcesses &ours) {
	Signals s;
	s.idleSeconds = secondsSinceInput();
	s.fullScreen = fullScreenProgram();
	s.otherGpuPercent = (std::max)(0.0, gpu.others(ours));
	s.otherCpuPercent = (std::max)(0.0, cpu.others());
	return s;
}

// Reads the computer every two seconds for as long as the program runs, and keeps `event` set while it is busy. Says in the log when the verdict changes.
class Monitor {
  public:
	explicit Monitor(HANDLE event) : event_(event), decision_(Limits::fromEnvironment()) {
		sample(true);
		std::thread([this] {
			// The render runs below normal priority (background_mode::apply), and a thread of it at that priority does not run at all while other programs keep every core busy -
			// exactly when it has something to say (measured: never a reading in 105 s beside a program on all cores). It wakes every two seconds for a moment, so it may run first.
			SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
			for (int n = 1;; ++n) {
				std::this_thread::sleep_for(std::chrono::seconds(2));
				if (n % 150 == 0) ours_.forget();   // (every five minutes)
				sample(false);
			}
		}).detach();
	}

  private:
	void sample(bool first) {
		const Signals s = read(gpu_, cpu_, ours_);
		const double now = std::chrono::duration<double>(std::chrono::steady_clock::now() - start_).count();
		const bool changed = decision_.update(s, now);
		if (decision_.busy()) SetEvent(event_);
		else ResetEvent(event_);
		if (changed || first)
			std::cout << "\nSmart power: " << (decision_.busy() ? "the computer is busy (" + decision_.reason() + "): a pause after every frame." : std::string("the computer is free: full power."))
					  << std::endl;
	}
	HANDLE event_;
	Decision decision_;
	GpuEngines gpu_;
	Processor cpu_;
	OurProcesses ours_;
	const std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now();
};

// Starts watching the computer for this process and the programs it starts from now on (a video calls it before its first frame). Nothing when this process was itself started by one
// that watches (a worker, a frame run: RAY_TRACER_SMART_EVENT is set). False when it cannot watch (busyNow() then says busy: polite).
inline bool start() {
#ifdef _MSC_VER
#pragma warning(suppress : 4996)
#endif
	if (const char *name = std::getenv(kEventVariable); name && *name) return true;
	const std::string name = "Local\\photonsmith-busy-" + std::to_string(GetCurrentProcessId());
	const HANDLE event = CreateEventA(nullptr, TRUE, FALSE, name.c_str());
	if (!event) return false;
	_putenv_s(kEventVariable, name.c_str());
	static Monitor *monitor = new Monitor(event);   // (kept for the rest of the program: its thread watches as long as the program runs)
	(void)monitor;
	return true;
}

// Whether the computer is busy, as the watching process last found. True (polite) when nobody watches or its event cannot be opened.
inline bool busyNow() {
#ifdef _MSC_VER
#pragma warning(suppress : 4996)
#endif
	const char *name = std::getenv(kEventVariable);
	if (!name || !*name) return true;
	static std::mutex lock;
	static std::string openName;
	static HANDLE event = nullptr;
	std::lock_guard<std::mutex> hold(lock);
	if (openName != name) {
		if (event) CloseHandle(event);
		event = OpenEventA(SYNCHRONIZE, FALSE, name);
		openName = name;
	}
	return !event || WaitForSingleObject(event, 0) == WAIT_OBJECT_0;
}

// Sleeps for `seconds`, or until the computer is free again, whichever comes first (a pause ends as soon as nobody needs the machine).
inline void pauseWhileBusy(double seconds) {
	const auto until = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
	while (std::chrono::steady_clock::now() < until && busyNow())
		std::this_thread::sleep_for((std::min)(std::chrono::duration<double>(0.5), std::chrono::duration<double>(until - std::chrono::steady_clock::now())));
}

// One reading of every sign over `seconds`, for --diagnose.
inline Signals readOnce(double seconds) {
	GpuEngines gpu;
	Processor cpu;
	OurProcesses ours;
	std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
	return read(gpu, cpu, ours);
}

#else  // not Windows: no signs are read (RunPolicy makes Smart a background run there)

inline bool start() { return false; }
inline bool busyNow() { return true; }
inline void pauseWhileBusy(double seconds) { std::this_thread::sleep_for(std::chrono::duration<double>(seconds)); }

#endif

}  // namespace smart_power
