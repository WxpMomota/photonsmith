#pragma once
// smart_power.h -- the GUI's Power "Smart" (--smart-power, or RAY_TRACER_BACKGROUND=auto): a video renders at full power unless something needs the graphics card, and then as a polite
// guest (the pauses of --background, background_mode.h). RunPolicy (run_policy.h) decides what that changes; this finds out when it is needed.
//
// Someone typing or browsing does not need it. Measured on an RTX 5080, a tiny piece of graphics work sent 60 times a second waited 0.2 ms on an idle card and 0.2 ms beside a film frame
// (worst 4.7 ms), and the render runs at a lower priority anyway, so a key or the mouse does not pause anything. What does (busyReason):
//   * Windows says a full-screen program, a game or a presentation is running;
//   * other programs keep the graphics card's 3D or compute engines 15% busy or more, or half of the processor (a game, a second render, a compile left running);
//   * someone is at the computer (a key or the mouse in the last minute: RAY_TRACER_SMART_IDLE_SECONDS) and work on the card is really being kept waiting: a tiny job four times a second
//     takes 33 ms or more (GpuCanary; two frames overflowing the card's memory stalled it for seconds);
//   * another program wants the card's memory and it has run out (a frame of a film can hold 10 GB: the card's own check then stops a worker: video_render.h).
// It counts other programs on the card the render uses: not this program's GUI, not the desktop's own drawing (dwm), not another adapter, not a film playing (its video engines).
// A busy computer stays busy until 10 s after the last sign of it, so that a short lull does not start the card at full power under a game.
//
// One process watches: the one that renders the video (Monitor, started by start()): four times a second for the signs that cost nothing, twice a second for the counters. It says what
// it finds through a named event that the workers and frame runs it starts read (busyNow(); its name is in RAY_TRACER_SMART_EVENT, which they inherit). While the computer is busy each of
// the N frames rendered at once pauses N times as long as background mode's one would (RunPolicy::smartFrames), so that the duty holds for the graphics card as a whole - N workers each
// pausing as one would keep it as busy as ever. (Taking turns instead - one frame at a time under a lock - was tried and dropped: a frame that hangs holding the lock makes the healthy
// workers waiting for it look hung to the watchdog.)
// The wavefront renderer goes back to the program between samples, so a worker pauses there too (yieldInsideFrames), which makes room within seconds however long a frame takes (measured:
// 3.4 s from a test program starting to the verdict), and with gaps a fraction of a second long, which is what a game needs. The default renderer sends a frame to the card in one go, so it can only wait for the frame's end. A pause ends as soon
// as the computer is free again (pauseWhileBusy). RAY_TRACER_SMART_BUSY=1 says busy whatever the signs, for testing.
// Other systems than Windows do not read these signs: there Smart is Keep the PC responsive (run_policy.h).

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "gpu_memory.h"
#include "../src/shared/render_yield.h"

#ifdef _WIN32
#include <iostream>
#include <mutex>
#include <unordered_map>
#include "process.h"   // (windows.h, and killOnExitJob: the workers and frame runs whose processor time is ours)
#include <shellapi.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <tlhelp32.h>
#include <d3d11.h>
#include <dxgi.h>
#undef interface   // (Windows' COM headers define these two, and the film code has variables of those names)
#undef small
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
	double gpuWaitMs = 0;         // how long a tiny piece of graphics work has been waiting for the card lately (GpuCanary)
	double otherGpuMemoryMb = 0;  // the card's memory held by other programs
	double freeGpuMemoryMb = 1e9; // the card's memory nobody holds
};

// When the computer counts as busy.
struct Limits {
	double idleSeconds = 60;   // someone is at the computer until this long after the last key or mouse movement
	double gpuPercent = 15;
	double cpuPercent = 50;
	double gpuWaitMs = 33;     // (a frame of the desktop at 30 Hz: past this, someone at the computer sees it)
	double freeMemoryMb = 1024, otherMemoryMb = 2048;   // the card is short of memory for someone else: under this free, with others holding this much
	double holdSeconds = 10;   // busy until this long after the last sign of it

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
#ifdef _MSC_VER
#pragma warning(suppress : 4996)
#endif
	if (const char *test = std::getenv("RAY_TRACER_SMART_BUSY"); test && *test && *test != '0') return "RAY_TRACER_SMART_BUSY says so";
	if (s.fullScreen) return "a full-screen program is running";
	if (s.otherGpuPercent >= l.gpuPercent) {
		std::snprintf(percent, sizeof(percent), "%.0f%%", s.otherGpuPercent);
		return std::string("another program keeps the graphics card ") + percent + " busy";
	}
	if (s.otherCpuPercent >= l.cpuPercent) {
		std::snprintf(percent, sizeof(percent), "%.0f%%", s.otherCpuPercent);
		return std::string("other programs keep the processor ") + percent + " busy";
	}
	if (s.idleSeconds < l.idleSeconds && s.gpuWaitMs >= l.gpuWaitMs) {
		std::snprintf(percent, sizeof(percent), "%.0f ms", s.gpuWaitMs);
		return std::string("someone is using it and work on the graphics card waits ") + percent;
	}
	if (s.freeGpuMemoryMb < l.freeMemoryMb && s.otherGpuMemoryMb >= l.otherMemoryMb) return "another program needs the graphics card's memory";
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

// Whether an engine counts: on the card the render uses (`adapter`, its "luid_..." name; empty: any), and one a render competes for. The video engines play films and copy engines move
// memory, beside the render, not in its way.
inline bool engineCounts(const std::string &engine, const std::string &adapter) {
	if (!adapter.empty() && engine.find(adapter) == std::string::npos) return false;
	for (const char *free : {"engtype_VideoDecode", "engtype_VideoEncode", "engtype_VideoProcessing", "engtype_Copy", "engtype_Security", "engtype_OFA"})
		if (engine.find(free) != std::string::npos) return false;
	return true;
}

// The busiest engine's share, in percent, taken by the processes `ours` says are not ours: each engine's instances are summed over those processes, and the largest sum is returned.
template <class Ours>
double othersOnBusiestEngine(const std::vector<std::pair<std::string, double>> &instances, const Ours &ours, const std::string &adapter = std::string()) {
	std::vector<std::pair<std::string, double>> engines;   // (a few dozen engines: a list is plenty)
	for (const auto &[name, percent] : instances) {
		std::string engine;
		const int pid = pidOfEngineInstance(name, &engine);
		if (pid < 0 || ours(pid) || !engineCounts(engine, adapter)) continue;
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

// Whether a process does not count as another program: this program under the same path (the workers and frame runs it starts are; so is another render of this program), its GUI beside
// it, and what only shows the desktop: its own drawing (dwm), Chrome Remote Desktop, which kept 11% of the 3D engine by itself and tripped the 15% with nothing else running, and
// Windows' own csrss, which reached 25% of it while the computer was used over Chrome Remote Desktop and paused a film every 30 s (2026-11-06; no program runs there). dwm is one no
// program may open, so these are found by name in the list of processes.
class OurProcesses {
  public:
	OurProcesses() : self_(pathOf(GetCurrentProcessId())) {
		gui_ = self_.substr(0, self_.find_last_of(L"\\/") + 1) + L"RayTracerGUI.exe";
	}
	bool operator()(int pid) {
		if (static_cast<DWORD>(pid) == GetCurrentProcessId()) return true;
		if (const auto it = known_.find(pid); it != known_.end()) return it->second;
		if (known_.size() > 4096) known_.clear();
		const std::wstring path = pathOf(static_cast<DWORD>(pid));
		return known_[pid] = (!path.empty() && (_wcsicmp(path.c_str(), self_.c_str()) == 0 || _wcsicmp(path.c_str(), gui_.c_str()) == 0)) || showsTheDesktop(static_cast<DWORD>(pid));
	}
	// Forgets what it found (process ids are used again after a process ends).
	void forget() { known_.clear(); }

  private:
	static bool showsTheDesktop(DWORD pid) {
		const HANDLE all = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
		if (all == INVALID_HANDLE_VALUE) return false;
		PROCESSENTRY32W entry{};
		entry.dwSize = sizeof(entry);
		bool desktop = false;
		for (BOOL more = Process32FirstW(all, &entry); more; more = Process32NextW(all, &entry))
			if (entry.th32ProcessID == pid) {
				desktop = _wcsicmp(entry.szExeFile, L"dwm.exe") == 0 || _wcsicmp(entry.szExeFile, L"remoting_desktop.exe") == 0 || _wcsicmp(entry.szExeFile, L"csrss.exe") == 0;
				break;
			}
		CloseHandle(all);
		return desktop;
	}
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
	std::wstring self_, gui_;
	std::unordered_map<int, bool> known_;
};

// The graphics card a render uses (the one with the most memory of its own), named as the GPU counters name it. Looked up in dxgi.dll so that nothing more is linked.
struct Adapter {
	IDXGIAdapter1 *adapter = nullptr;
	std::string luid;     // "luid_0x00000000_0x0000D1A3"
};
inline Adapter mainAdapter() {
	Adapter out;
	using CreateFn = HRESULT(WINAPI *)(REFIID, void **);
	const HMODULE dxgi = LoadLibraryA("dxgi.dll");
	const CreateFn create = dxgi ? reinterpret_cast<CreateFn>(reinterpret_cast<void *>(GetProcAddress(dxgi, "CreateDXGIFactory1"))) : nullptr;
	IDXGIFactory1 *factory = nullptr;
	if (!create || FAILED(create(__uuidof(IDXGIFactory1), reinterpret_cast<void **>(&factory)))) return out;
	IDXGIAdapter1 *a = nullptr;
	SIZE_T most = 0;
	for (UINT i = 0; factory->EnumAdapters1(i, &a) != DXGI_ERROR_NOT_FOUND; ++i) {
		DXGI_ADAPTER_DESC1 d;
		if (SUCCEEDED(a->GetDesc1(&d)) && d.DedicatedVideoMemory > most) {
			most = d.DedicatedVideoMemory;
			char name[64];
			std::snprintf(name, sizeof(name), "luid_0x%08lX_0x%08lX", static_cast<unsigned long>(d.AdapterLuid.HighPart), d.AdapterLuid.LowPart);
			if (out.adapter) out.adapter->Release();
			out.adapter = a, out.luid = name;
		} else {
			a->Release();
		}
	}
	factory->Release();
	return out;
}

// What a program feels: four times a second a tiny piece of graphics work goes to the card and is timed until it is done. Its own thread at normal priority, as a card that is stalled would
// hold it for seconds. A reading is the middle of the last eight, so one late piece is not a verdict (a film frame's scene going up to the card held single pieces 150 to 250 ms), unless
// one has been out for a second: a card stalled that long need not wait for the middle. Nothing is linked: d3d11.dll is looked up, and with no device it reads 0.
class GpuCanary {
  public:
	explicit GpuCanary(IDXGIAdapter1 *adapter) {
		if (!adapter) return;
		using CreateFn = HRESULT(WINAPI *)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL *, UINT, UINT, ID3D11Device **, D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);
		const HMODULE d3d = LoadLibraryA("d3d11.dll");
		const CreateFn create = d3d ? reinterpret_cast<CreateFn>(reinterpret_cast<void *>(GetProcAddress(d3d, "D3D11CreateDevice"))) : nullptr;
		if (!create || FAILED(create(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device_, nullptr, &context_))) return;
		D3D11_TEXTURE2D_DESC td{};
		td.Width = td.Height = 64, td.MipLevels = td.ArraySize = 1, td.Format = DXGI_FORMAT_R8G8B8A8_UNORM, td.SampleDesc.Count = 1, td.Usage = D3D11_USAGE_DEFAULT, td.BindFlags = D3D11_BIND_RENDER_TARGET;
		D3D11_QUERY_DESC qd{};
		qd.Query = D3D11_QUERY_EVENT;
		if (FAILED(device_->CreateTexture2D(&td, nullptr, &texture_)) || FAILED(device_->CreateRenderTargetView(texture_, nullptr, &target_)) || FAILED(device_->CreateQuery(&qd, &query_))) return;
		thread_ = std::thread([this] { run(); });
	}
	// (one made for a single reading, --diagnose's, ends with it: its thread is stopped before what it uses goes)
	~GpuCanary() {
		stop_ = true;
		if (thread_.joinable()) thread_.join();
		for (IUnknown *u : {static_cast<IUnknown *>(query_), static_cast<IUnknown *>(target_), static_cast<IUnknown *>(texture_), static_cast<IUnknown *>(context_), static_cast<IUnknown *>(device_)})
			if (u) u->Release();
	}
	GpuCanary(const GpuCanary &) = delete;
	GpuCanary &operator=(const GpuCanary &) = delete;
	double readingMs() const {
		const double out = outFor_.load();
		return (std::max)(middle_.load(), out >= 1000.0 ? out : 0.0);
	}

  private:
	void run() {
		std::vector<double> last;   // (the last eight; the middle of those there are, so a reading soon after the start is not the middle of zeros)
		const float colour[4] = {0.0f, 0.0f, 0.0f, 1.0f};
		for (std::size_t n = 0; !stop_; ++n) {
			const auto start = std::chrono::steady_clock::now();
			context_->ClearRenderTargetView(target_, colour);
			context_->End(query_);
			context_->Flush();
			BOOL done = FALSE;
			for (int spin = 0; !stop_; ++spin) {
				const HRESULT got = context_->GetData(query_, &done, sizeof(done), 0);
				if (got == S_OK) break;
				if (FAILED(got)) {   // (the device is gone - a driver reset or update: no more readings, and not a card kept waiting for ever)
					outFor_ = 0.0, middle_ = 0.0;
					return;
				}
				outFor_ = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
				if (spin < 2000) std::this_thread::yield();
				else std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
			const double took = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
			if (last.size() < 8) last.push_back(took);
			else last[n % last.size()] = took;
			outFor_ = 0.0;
			std::vector<double> sorted = last;
			std::sort(sorted.begin(), sorted.end());
			middle_ = sorted[sorted.size() / 2];
			for (int k = 0; k < 25 && !stop_; ++k) std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
	}
	std::thread thread_;
	std::atomic<bool> stop_{false};
	ID3D11Device *device_ = nullptr;
	ID3D11DeviceContext *context_ = nullptr;
	ID3D11Texture2D *texture_ = nullptr;
	ID3D11RenderTargetView *target_ = nullptr;
	ID3D11Query *query_ = nullptr;
	std::atomic<double> middle_{0.0}, outFor_{0.0};
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
		if (add_(query_, L"\\GPU Process Memory(*)\\Dedicated Usage", 0, &memory_) != ERROR_SUCCESS) memory_ = nullptr;
		if (counter_ || memory_) collect_(query_);   // (a rate: the first reading is the start of the first interval)
	}
	~GpuEngines() {
		if (query_) close_(query_);
	}
	GpuEngines(const GpuEngines &) = delete;
	GpuEngines &operator=(const GpuEngines &) = delete;

	// Reads the counters: what others() and memoryOf() report comes from this reading. False when they cannot be read.
	bool collect() { return (counter_ || memory_) && collect_(query_) == ERROR_SUCCESS; }
	// Other programs' share of the busiest engine since the last reading, in percent, on the card named `adapter`; -1 when the counters cannot be read.
	template <class Ours>
	double others(Ours &ours, const std::string &adapter) {
		std::vector<std::pair<std::string, double>> instances;
		if (!read(counter_, instances)) return -1.0;
		return othersOnBusiestEngine(instances, [&](int pid) { return ours(pid); }, adapter);
	}
	// How much of the card's memory other programs hold, in megabytes. (These counters cannot say what is free: they gave the desktop's own process 10.8 GB when the driver said 558 MB of the
	// card was in use.)
	template <class Ours>
	double memoryOf(Ours &ours, const std::string &adapter) {
		std::vector<std::pair<std::string, double>> instances;
		double others = 0.0;
		if (!read(memory_, instances)) return others;
		for (const auto &[name, bytes] : instances) {
			std::string where;
			const int pid = pidOfEngineInstance(name, &where);
			if (pid >= 0 && !ours(pid) && (adapter.empty() || where.find(adapter) != std::string::npos)) others += bytes / (1024.0 * 1024.0);
		}
		return others;
	}

  private:
	bool read(PDH_HCOUNTER counter, std::vector<std::pair<std::string, double>> &instances) {
		if (!counter) return false;
		DWORD bytes = 0, count = 0;
		if (array_(counter, PDH_FMT_DOUBLE, &bytes, &count, nullptr) != static_cast<PDH_STATUS>(PDH_MORE_DATA) || bytes == 0) return count == 0;
		std::vector<unsigned char> buffer(bytes);
		auto *items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W *>(buffer.data());
		if (array_(counter, PDH_FMT_DOUBLE, &bytes, &count, items) != ERROR_SUCCESS) return false;
		instances.reserve(count);
		for (DWORD i = 0; i < count; ++i) {
			if (items[i].FmtValue.CStatus != PDH_CSTATUS_VALID_DATA && items[i].FmtValue.CStatus != PDH_CSTATUS_NEW_DATA) continue;
			std::string name;
			for (const wchar_t *c = items[i].szName; c && *c; ++c) name += static_cast<char>(*c < 128 ? *c : '?');   // (the names are ASCII)
			instances.emplace_back(std::move(name), items[i].FmtValue.doubleValue);
		}
		return true;
	}
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
	PDH_HCOUNTER counter_ = nullptr, memory_ = nullptr;
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

// The signs that cost nothing to read (a key or the mouse, a full-screen program).
inline void readQuick(Signals &s) {
	s.idleSeconds = secondsSinceInput();
	s.fullScreen = fullScreenProgram();
}
// The signs behind counters. The rates (the graphics card, the processor) are over the time since the readers' last reading.
inline void readCounters(Signals &s, GpuEngines &gpu, Processor &cpu, OurProcesses &ours, const Adapter &card) {
	if (gpu.collect()) {
		s.otherGpuPercent = (std::max)(0.0, gpu.others(ours, card.luid));
		s.otherGpuMemoryMb = gpu.memoryOf(ours, card.luid);
		const Limits limits;
		s.freeGpuMemoryMb = 1e9;
		if (s.otherGpuMemoryMb >= limits.otherMemoryMb)   // (asked of the driver only when it matters: it loads a library each time)
			if (const long long free = gpu_memory::freeMb(0)) s.freeGpuMemoryMb = static_cast<double>(free);
	}
	s.otherCpuPercent = (std::max)(0.0, cpu.others());
}

// Reads the computer for as long as the program runs, and keeps `event` set while it is busy. Says in the log when the verdict changes.
class Monitor {
  public:
	explicit Monitor(HANDLE event) : event_(event), decision_(Limits::fromEnvironment()), card_(mainAdapter()), canary_(card_.adapter) {
		readCounters(now_, gpu_, cpu_, ours_, card_);
		sample(true);
		std::thread([this] {
			// The render runs below normal priority (background_mode::apply), and a thread of it at that priority does not run at all while other programs keep every core busy -
			// exactly when it has something to say (measured: never a reading in 105 s beside a program on all cores). It wakes for a moment at a time, so it may run first. (A reading of
			// the counters took 0.6 ms with 520 of them, the quick signs 45 microseconds.)
			SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
			for (int n = 1;; ++n) {
				std::this_thread::sleep_for(std::chrono::milliseconds(250));
				if (n % 1200 == 0) ours_.forget();   // (every five minutes)
				if (n % 2 == 0) {   // (twice a second, and the last two readings taken together, so that one blip of a program's drawing is not a game)
					const double before = now_.otherGpuPercent;
					readCounters(now_, gpu_, cpu_, ours_, card_);
					now_.otherGpuPercent = 0.5 * (before + now_.otherGpuPercent);
				}
				sample(false);
			}
		}).detach();
	}

  private:
	void sample(bool first) {
		readQuick(now_);
		now_.gpuWaitMs = canary_.readingMs();
		const Signals &s = now_;
		const double now = std::chrono::duration<double>(std::chrono::steady_clock::now() - start_).count();
		const bool changed = decision_.update(s, now);
		if (decision_.busy()) SetEvent(event_);
		else ResetEvent(event_);
		if (changed || first)
			std::cout << "\nSmart power: " << (decision_.busy() ? "the computer is busy (" + decision_.reason() + "): making room." : std::string("nothing needs the computer's graphics card: full power."))
					  << std::endl;
	}
	HANDLE event_;
	Decision decision_;
	Adapter card_;
	GpuEngines gpu_;
	Processor cpu_;
	OurProcesses ours_;
	GpuCanary canary_;
	Signals now_;
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

// A worker's pause inside a frame. Between two of a frame's samples: while the computer is busy, the work done since the last one is paid for at once with a pause, `pausePerWork` times as long
// (RunPolicy::pauseSecondsAfter(1, true)), so a game has gaps a fraction of a second long, starting at once, and not a pause after a frame half a minute later.
struct InFrame {
	double pausePerWork = 1.0;
	std::chrono::steady_clock::time_point since = std::chrono::steady_clock::now();
	std::atomic<double> paidFor{0.0};   // (seconds of this frame's work and pauses already settled: the pause at its end leaves them out)
	std::atomic<bool> fresh{true};      // (a frame has begun and has not reached its samples yet)
};
inline InFrame &inFrame() {
	static InFrame state;
	return state;
}
inline void betweenSamples() {
	InFrame &f = inFrame();
	const auto now = std::chrono::steady_clock::now();
	if (f.fresh.exchange(false)) {   // (its first: the clock starts here. Making its scene, compiling the card's programs and the pause after the last frame are not work on the card
		f.since = now;               // to pay for between samples - the pause at the frame's end sees to what it took outside them)
		return;
	}
	const double work = std::chrono::duration<double>(now - f.since).count();
	if (work < 0.05) return;   // (a batch of samples a time: often enough to keep the gaps short)
	if (busyNow()) {
		pauseWhileBusy(work * f.pausePerWork);
		f.since = std::chrono::steady_clock::now();
		f.paidFor = f.paidFor.load() + std::chrono::duration<double>(f.since - now).count() + work;
	} else {
		f.since = now;
	}
}
// From now on a worker pauses inside frames too, on a renderer that goes back to the program between samples.
inline void yieldInsideFrames(double pausePerWork) {
	inFrame().pausePerWork = pausePerWork;
	render_yield::hook() = &betweenSamples;
}
// A worker's frame begins (before its scene is made).
inline void frameStarts() { inFrame().fresh = true; }
// How much of the time since the last call was work and pauses settled inside the frame.
inline double takePaidFor() { return inFrame().paidFor.exchange(0.0); }

// One reading of every sign over `seconds`, for --diagnose.
inline Signals readOnce(double seconds) {
	GpuEngines gpu;
	Processor cpu;
	OurProcesses ours;
	const Adapter card = mainAdapter();
	GpuCanary canary(card.adapter);
	std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
	Signals s;
	readQuick(s);
	readCounters(s, gpu, cpu, ours, card);
	s.gpuWaitMs = canary.readingMs();
	if (card.adapter) card.adapter->Release();
	return s;
}

#else  // not Windows: no signs are read (RunPolicy makes Smart a background run there)

inline bool start() { return false; }
inline bool busyNow() { return true; }
inline void pauseWhileBusy(double seconds) { std::this_thread::sleep_for(std::chrono::duration<double>(seconds)); }
inline void yieldInsideFrames(double) {}
inline void frameStarts() {}
inline double takePaidFor() { return 0.0; }

#endif

}  // namespace smart_power
