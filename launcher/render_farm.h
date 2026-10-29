#pragma once
// render_farm.h -- `--render-farm DIR`: more than one computer renders the same film, each taking frames from one queue in a shared folder (a Windows share, a Mac's shared folder, a
// network drive: any folder every computer can write to). Each computer is started on the same film with the same settings and the same DIR; each takes the next frames no one has
// taken (a batch at a time: kBatch), renders them as it always would, and puts each finished picture in DIR. When there are no frames left to take it waits for the others' and
// then makes the video from all of them, so every computer that took part ends with the whole film. Nothing has to be started first or told about the others.
//
// The queue is files. A frame is taken by creating its claim file (claims/NNNN.txt: the computer and its process), which only one computer can do - the file is created with
// "x" (fail if it exists), which a file server does at once for everyone. A finished frame is its picture in pictures/ (copied under another name, then renamed, so a half-copied
// picture is never seen as finished). A computer keeps its claims fresh while it renders them (rewrites them every minute); a claim not rewritten for kStaleSeconds is a computer
// that stopped (switched off, crashed, its program closed) and the frame is taken again, as is a claim of this computer from a run before this one. A frame that fails twice is given up
// (the video fills its hole as it fills any failed frame's). Two computers taking the same stale frame at once both render it, which costs time and nothing else: its two pictures
// are the same picture.
//
// DIR/farm.txt holds the settings of the farm's video (the film, its frame count and rate, the size, samples, depth, draft and seeds); a computer started with others is refused.
// Times are compared on the file server's clock (a file this computer has just written), so computers with clocks that differ agree on what is stale.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace render_farm {

constexpr const char *kSettings = "farm.txt";
constexpr int kBatch = 12;             // frames taken at a time
constexpr int kStaleSeconds = 600;     // a claim not rewritten for this long is a computer that stopped
constexpr int kHeartbeatSeconds = 60;  // how often a computer rewrites its claims
constexpr int kTries = 2;              // a frame that fails this many times is given up

// This computer's name (RAY_TRACER_FARM_NAME, when one computer runs two farm renders: they must not be taken for one run and the run before it), and its process.
inline std::string hostName() {
	std::string h;
	if (const char *c = std::getenv("RAY_TRACER_FARM_NAME")) h = c;
#ifdef _WIN32
	if (const char *c = std::getenv("COMPUTERNAME"); h.empty() && c) h = c;
#else
	char buf[256] = {};
	if (h.empty() && gethostname(buf, sizeof(buf) - 1) == 0) h = buf;
#endif
	if (h.empty())
		if (const char *c = std::getenv("HOSTNAME")) h = c;
	for (char &c : h)
		if (c == ' ' || c == '\n' || c == '\r') c = '_';
	return h.empty() ? std::string("computer") : h;
}
inline long long processId() {
#ifdef _WIN32
	return _getpid();
#else
	return static_cast<long long>(getpid());
#endif
}

// Creates `path` with `text` when no file is there - and only then: true when this call made it.
inline bool createOnly(const std::filesystem::path &path, const std::string &text) {
	std::FILE *f = nullptr;
#ifdef _WIN32
	if (_wfopen_s(&f, path.wstring().c_str(), L"wx") != 0) f = nullptr;
#else
	f = std::fopen(path.string().c_str(), "wx");
#endif
	if (!f) return false;
	std::fwrite(text.data(), 1, text.size(), f);
	std::fclose(f);
	return true;
}
inline std::string readAll(const std::filesystem::path &path) {
	std::ifstream in(path, std::ios::binary);
	std::ostringstream s;
	s << in.rdbuf();
	return s.str();
}

// Joins the farm in `root` with the video's `settings`: the first computer writes them, the others must have the same. False (and why) when they do not.
inline bool join(const std::filesystem::path &root, const std::string &settings, std::string &problem) {
	std::error_code ec;
	std::filesystem::create_directories(root, ec);
	if (ec) {
		problem = "cannot make the folder " + root.string() + ": " + ec.message();
		return false;
	}
	createOnly(root / kSettings, settings + "\n");
	std::string there = readAll(root / kSettings);
	for (int tries = 0; there.empty() && tries < 10; ++tries) {   // (another computer that has just made it may not have written it yet)
		std::this_thread::sleep_for(std::chrono::milliseconds(200));
		there = readAll(root / kSettings);
	}
	while (!there.empty() && (there.back() == '\n' || there.back() == '\r')) there.pop_back();
	if (there == settings) return true;
	problem = there.empty() ? "cannot read " + (root / kSettings).string()
							: "the farm in " + root.string() + " renders another video (" + there + "; this one: " + settings + "). Give this video a folder of its own, or delete " +
								  (root / kSettings).string() + " to start that farm again";
	return false;
}

// One set of frames on a farm: the film's own (frames), or the other side of its dissolves (across).
struct Farm {
	std::filesystem::path root, dir;
	std::string host, me;   // this computer, and "<computer> <process>" as its claims say
	int staleSeconds = kStaleSeconds;

	static Farm at(const std::filesystem::path &root, const std::string &set) {
		Farm f;
		f.root = root;
		f.dir = root / set;
		f.host = hostName();
		f.me = f.host + " " + std::to_string(processId());
		std::error_code ec;
		for (const char *sub : {"claims", "pictures", "failed", "clock"}) std::filesystem::create_directories(f.dir / sub, ec);
		return f;
	}
	static std::string number(int frame) {
		char n[16];
		std::snprintf(n, sizeof(n), "%04d", frame);
		return n;
	}
	std::filesystem::path claim(int frame) const { return dir / "claims" / (number(frame) + ".txt"); }
	std::filesystem::path picture(int frame) const { return dir / "pictures" / ("enc_" + number(frame) + ".png"); }
	std::filesystem::path failures(int frame) const { return dir / "failed" / (number(frame) + ".txt"); }

	bool done(int frame) const {
		std::error_code ec;
		return std::filesystem::exists(picture(frame), ec);
	}
	int failed(int frame) const { return std::atoi(readAll(failures(frame)).c_str()); }
	bool givenUp(int frame) const { return failed(frame) >= kTries; }
	bool take(int frame) const { return createOnly(claim(frame), me + "\n"); }
	// The time now on the file server's clock: the time of a file just written there.
	std::filesystem::file_time_type now() const {
		const std::filesystem::path p = dir / "clock" / (host + ".txt");
		{
			std::ofstream out(p, std::ios::trunc);
			out << me << "\n";
		}
		std::error_code ec;
		return std::filesystem::last_write_time(p, ec);
	}
	// A claim whose computer stopped: not rewritten for staleSeconds, or this computer's from a run before this one.
	bool stale(int frame, std::filesystem::file_time_type serverNow) const {
		std::error_code ec;
		const auto when = std::filesystem::last_write_time(claim(frame), ec);
		if (ec) return false;   // (no claim: free, not stale)
		std::string who = readAll(claim(frame));
		while (!who.empty() && (who.back() == '\n' || who.back() == '\r')) who.pop_back();
		if (who != me && who.rfind(host + " ", 0) == 0) return true;
		return serverNow - when > std::chrono::seconds(staleSeconds);
	}
	// Takes a stale claim over (no "x" here: two computers that both do it render the same frame twice, which is harmless).
	bool takeOver(int frame) const {
		std::ofstream out(claim(frame), std::ios::trunc);
		out << me << "\n";
		return static_cast<bool>(out);
	}
	void release(int frame) const {
		std::error_code ec;
		std::filesystem::remove(claim(frame), ec);
	}
	// A finished frame's picture into the farm: copied under another name, then renamed.
	bool publish(int frame, const std::filesystem::path &from) const {
		std::error_code ec;
		const std::filesystem::path part = dir / "pictures" / ("part_" + number(frame) + "_" + host + ".png");
		std::filesystem::copy_file(from, part, std::filesystem::copy_options::overwrite_existing, ec);
		if (ec) return false;
		std::filesystem::rename(part, picture(frame), ec);
		if (ec) std::filesystem::remove(part, ec);
		return done(frame);
	}
	void fail(int frame) const {
		const int n = failed(frame) + 1;
		{
			std::ofstream out(failures(frame), std::ios::trunc);
			out << n << "\n";
		}
		release(frame);
	}
};

// Rewrites this computer's claims every kHeartbeatSeconds while it renders them, so the others do not take them as a stopped computer's.
class Heartbeat {
public:
	explicit Heartbeat(const Farm &farm) : farm_(farm), thread_([this] { run(); }) {}
	~Heartbeat() {
		{
			std::lock_guard<std::mutex> hold(lock_);
			stop_ = true;
		}
		wake_.notify_all();
		thread_.join();
	}
	void hold(const std::vector<int> &frames) {
		std::lock_guard<std::mutex> hold(lock_);
		frames_ = frames;
	}

private:
	void run() {
		std::unique_lock<std::mutex> hold(lock_);
		while (!stop_) {
			wake_.wait_for(hold, std::chrono::seconds(kHeartbeatSeconds), [this] { return stop_; });
			if (stop_) break;
			for (int f : frames_) {
				std::string who = readAll(farm_.claim(f));
				while (!who.empty() && (who.back() == '\n' || who.back() == '\r')) who.pop_back();
				if (who == farm_.me) farm_.takeOver(f);   // (rewritten: its time is now; a claim another computer took over is left to it)
			}
		}
	}
	const Farm farm_;
	std::mutex lock_;
	std::condition_variable wake_;
	bool stop_ = false;
	std::vector<int> frames_;
	std::thread thread_;
};

// The frames flagged in `wanted`, rendered by this computer and the others on the farm: `render(mask)` renders the frames flagged in its mask into `local` (as enc_NNNN.png,
// the video's pictures); each is put in the farm as it is done. When no frame is left to take, waits for the others' (taking over a stopped computer's), then copies every finished
// picture into `local`. Returns how many of the wanted frames this computer rendered; `waitSeconds` is how long it waits between looks (tests: less).
inline int run(const Farm &farm, const std::filesystem::path &local, const std::vector<char> &wanted, const std::function<void(const std::vector<char> &)> &render,
			   int batch = kBatch, double waitSeconds = 15.0) {
	const int count = static_cast<int>(wanted.size());
	const auto localPicture = [&](int f) { return local / ("enc_" + Farm::number(f) + ".png"); };
	int total = 0, mine = 0;
	for (int f = 0; f < count; ++f) total += wanted[static_cast<std::size_t>(f)] ? 1 : 0;
	std::error_code ec;
	// (this computer's pictures from a run before, not yet in the farm: put in, not rendered again)
	for (int f = 0; f < count; ++f)
		if (wanted[static_cast<std::size_t>(f)] && !farm.done(f) && std::filesystem::exists(localPicture(f), ec)) farm.publish(f, localPicture(f));
	Heartbeat heartbeat(farm);
	auto lastSaid = std::chrono::steady_clock::now() - std::chrono::hours(1);
	for (;;) {
		std::vector<int> taken;
		int finished = 0, open = 0;
		for (int f = 0; f < count; ++f) {
			if (!wanted[static_cast<std::size_t>(f)]) continue;
			if (farm.done(f) || farm.givenUp(f)) {
				++finished;
				continue;
			}
			++open;
			if (static_cast<int>(taken.size()) < batch && farm.take(f)) taken.push_back(f);
		}
		if (taken.empty() && open > 0) {   // nothing free: a stopped computer's frames, else wait for the others
			const auto serverNow = farm.now();
			for (int f = 0; f < count && static_cast<int>(taken.size()) < batch; ++f)
				if (wanted[static_cast<std::size_t>(f)] && !farm.done(f) && !farm.givenUp(f) && farm.stale(f, serverNow) && farm.takeOver(f)) taken.push_back(f);
		}
		if (taken.empty()) {
			if (open == 0) break;
			if (std::chrono::steady_clock::now() - lastSaid > std::chrono::minutes(1)) {
				std::cout << "\nRender farm: " << finished << " of " << total << " frames done; waiting for the other computers' " << open << "..." << std::endl;
				lastSaid = std::chrono::steady_clock::now();
			}
			std::this_thread::sleep_for(std::chrono::duration<double>(waitSeconds));
			continue;
		}
		std::cout << "\nRender farm: " << finished << " of " << total << " frames done; this computer (" << farm.host << ") takes " << taken.size() << " (frames " << taken.front()
				  << " to " << taken.back() << ")." << std::endl;
		heartbeat.hold(taken);
		std::vector<char> mask(static_cast<std::size_t>(count), 0);
		for (int f : taken) {
			mask[static_cast<std::size_t>(f)] = 1;
			std::filesystem::remove(localPicture(f), ec);   // (a picture from before is not this render's)
		}
		render(mask);
		for (int f : taken) {
			if (std::filesystem::exists(localPicture(f), ec) && farm.publish(f, localPicture(f))) farm.release(f), ++mine;
			else farm.fail(f);
		}
		heartbeat.hold({});
	}
	// every finished picture here, for the video
	int here = 0;
	for (int f = 0; f < count; ++f) {
		if (!wanted[static_cast<std::size_t>(f)] || !farm.done(f)) continue;
		const auto size = std::filesystem::file_size(farm.picture(f), ec);
		if (!std::filesystem::exists(localPicture(f)) || std::filesystem::file_size(localPicture(f), ec) != size)
			std::filesystem::copy_file(farm.picture(f), localPicture(f), std::filesystem::copy_options::overwrite_existing, ec);
		here += std::filesystem::exists(localPicture(f), ec) ? 1 : 0;
	}
	std::cout << "\nRender farm: " << here << " of " << total << " frames here (" << mine << " rendered by this computer)." << std::endl;
	return mine;
}

// Something made once for the whole farm (a film's shot samples: --noise-target): the computer that takes `name` first runs make(path) and puts its file in the farm; the others
// wait for it (and make it themselves when that computer stopped). True when root/name is there.
inline bool once(const Farm &farm, const std::string &name, const std::function<bool(const std::filesystem::path &)> &make, double waitSeconds = 3.0) {
	const std::filesystem::path file = farm.root / name, claim = farm.root / (name + ".claim");
	std::error_code ec;
	for (bool said = false;; said = true) {
		if (std::filesystem::exists(file, ec)) return true;
		bool mine = createOnly(claim, farm.me + "\n");
		if (!mine) {
			const auto when = std::filesystem::last_write_time(claim, ec);
			if (!ec && farm.now() - when > std::chrono::seconds(farm.staleSeconds)) {
				std::ofstream out(claim, std::ios::trunc);
				out << farm.me << "\n";
				mine = true;
			}
		}
		if (mine) {
			const std::filesystem::path part = farm.root / (name + "." + farm.host + ".part");
			const bool ok = make(part);
			if (ok) std::filesystem::rename(part, file, ec);
			std::filesystem::remove(claim, ec);
			return ok && std::filesystem::exists(file, ec);
		}
		if (!said) std::cout << "\nRender farm: waiting for another computer's " << name << "..." << std::endl;
		std::this_thread::sleep_for(std::chrono::duration<double>(waitSeconds));
	}
}

}  // namespace render_farm
