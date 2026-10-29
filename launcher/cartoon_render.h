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
#include "flag_scopes.h"
#include "frame_fingerprint.h"
#include "launcher_args.h"
#include "../src/shared/cartoon/cartoons.h"
#include "../src/shared/cartoon/shot_cameras.h"   // a shot's share of the samples
#include "../src/shared/cartoon/film_cache.h"

namespace cartoon_render {

// The flags of the parent run that a frame's run should repeat: the ones that say how to render (flag_scopes.h: scope Render), as typed, with their values. Not the video's own flags
// (`--video` would make every frame a video of its own) nor the output path, and not a flag that the table does not know yet (it says so).
inline std::vector<std::string> forwardedFlags(const LaunchArgs &args) {
	return renderFlagTokens(args.consumed_tokens);
}

// The flags a frame run repeats: the user's, and the film's own renderer when the film names one (cartoons.h, FilmMeta::wavefront) and the user did not choose.
inline std::vector<std::string> frameFlags(const LaunchArgs &args, const cartoon::Film &film, bool gpu) {
	std::vector<std::string> flags = forwardedFlags(args);
	if (gpu && film.meta.wavefront && !args.use_wavefront) flags.push_back("--wavefront");
	// On a GPU a film samples adaptively unless --no-adaptive: a pixel stops once the noise it would show is under --adaptive-threshold (pixel_convergence_rule.h).
	// Measured at 1080p and 500 samples: a landscape frame (Fire and Ice) 2-3x faster for the same denoised picture; lamp-lit and foggy cartoons are still too
	// noisy at 500 samples for their pixels to stop, and cost what they did.
	if (gpu && !args.adaptive_sampling && !args.no_adaptive) flags.push_back("--adaptive");
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
// fifteen minutes is stuck; twice that in --background mode, see below) and never on the CPU (a CPU frame at a high quality can take that long and more). 0 means never.
inline int timeoutFor(const LaunchArgs &args, bool gpu) {
	int seconds = gpu ? 900 : 0;
	if (args.cartoon_timeout >= 0) seconds = args.cartoon_timeout;
	else if (const char *env = std::getenv("RAY_TRACER_CARTOON_TIMEOUT")) seconds = std::max(0, std::atoi(env));
	// --background (and --smart-power): a worker pauses after every frame, so the limit grows with the pauses (RunPolicy::stallSeconds)
	return RunPolicy::from(args).stallSeconds(seconds);
}

// Writes the frame's scene, runs the child, removes the scene file. Returns the child's exit code (0 = SUCCESS), or -1 when the scene file could not be written or the child could not be
// started or did not finish in `timeoutSeconds` (it is stopped and tried once more first). `scenePath` is in a folder of its own (one per worker): the child's scene list reads that
// folder and nothing else (RAY_TRACER_PBRT_DIR), so that it does not read the 200-odd scene files of pbrt_scenes/ (about half a second of every frame) to render this one.
// `fingerprint` (when given) gets the scene's fingerprint; with `keepIf` (a finished frame's old fingerprint) the frame is not rendered when the scene's is the same, and `*kept` says so.
inline int renderFrame(const cartoon::Film &film, double t, const std::filesystem::path &scenePath, const std::string &exe, const LaunchArgs &args, int width, int height, int spp,
					   int depth, const std::string &framePath, int timeoutSeconds = 0, double timeLimitSeconds = 0.0, bool gpu = false, std::string *fingerprint = nullptr,
					   const std::string &keepIf = std::string(), bool *kept = nullptr, double *sceneSeconds = nullptr) {
	if (kept) *kept = false;
	if (sceneSeconds) *sceneSeconds = 0.0;
	try {
		// One frame's scene at a time: runInParallel calls this from several threads, and a film's code keeps caches of its own (static maps of figure meshes and the like)
		// that are not made for that. Building a scene is usually quick beside rendering it; `sceneSeconds` says how long it took (the wait for the lock left out).
		std::string text;
		{
			static std::mutex building;
			std::lock_guard<std::mutex> hold(building);
			const auto built = std::chrono::steady_clock::now();
			text = film.sceneAt(t);
			if (sceneSeconds) *sceneSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - built).count();
		}
		const std::string fp = frame_fingerprint::of(text);
		if (fingerprint) *fingerprint = fp;
		spp = cartoon::shot_cameras::samplesFor(spp, cartoon::shot_cameras::sampleShareOf(text));   // (a shot that needs fewer samples: its cameras file's `samples`)
		if (!keepIf.empty() && fp == keepIf) {
			if (kept) *kept = true;
			return 0;
		}
		std::ofstream f(scenePath, std::ios::binary);
		if (!f) {
			std::cerr << "\nERROR: could not write " << scenePath << std::endl;
			return -1;
		}
		f << text;
	} catch (const std::exception &e) {   // (a film's code failing on one frame fails that frame, not the whole video; why is said, not dropped)
		std::cerr << "\nERROR: " << film.name << " at " << t << " s: " << e.what() << std::endl;
		return -1;
	}
	const std::vector<std::string> childArgs = frameArgs(frameFlags(args, film, gpu), width, height, spp, depth, framePath, scenePath.string(), timeLimitSeconds);
	const RunPolicy policy = RunPolicy::from(args);
	const auto frameStart = std::chrono::steady_clock::now();
	int rc = -1;
	for (int attempt = 0; attempt < 2; ++attempt) {
		bool timedOut = false;
		// (its output into the work folder's log, not this program's: see Child::start)
		rc = runChild(exe, childArgs, "RAY_TRACER_PBRT_DIR", scenePath.parent_path().string(), timeoutSeconds, &timedOut, (scenePath.parent_path() / "frames.log").string());
		if (!timedOut) break;
		std::cerr << "\nWARNING: " << std::filesystem::path(framePath).filename().string() << " took longer than " << timeoutSeconds << " s and was stopped" << (attempt == 0 ? "; trying it once more." : ".") << std::endl;
		rc = -1;
	}
	std::error_code ec;
	std::filesystem::remove(scenePath, ec);
	background_mode::pauseAfter(policy, std::chrono::duration<double>(std::chrono::steady_clock::now() - frameStart).count());   // (--background, or smart on a busy computer)
	return rc;
}

// ---- resuming an interrupted (or edited) video ----
// What makes a cartoon video's frames comparable at all: the film, the size, the samples, the depth, the frame count and fps, and every flag given. Written beside the frames (resume.txt);
// --resume keeps the frames only when it is the same, and otherwise starts again.
inline std::string resumeSignature(const cartoon::Film &film, const LaunchArgs &args, int frames, int fps, int width, int height, int spp, int depth) {
	std::string s = std::string(film.key) + "|" + std::to_string(frames) + "|" + std::to_string(fps) + "|" + std::to_string(width) + "x" + std::to_string(height) + "|" + std::to_string(spp) + "|" +
					std::to_string(depth);
	for (const std::string &f : forwardedFlags(args)) s += "|" + f;
	if (args.temporal_radius > 0) s += "|a seed a frame";   // (--temporal-denoise: frames rendered with seeds of their own are other pictures)
	return s;
}
// The seed of a frame when each has its own (--temporal-denoise: noise that differs from frame to frame averages away; one seed for all would leave the same noise in every
// frame, which no filter over time can take out): the base seed (--seed, else 0) and the frame's number.
inline long long frameSeed(const LaunchArgs &args, int frame) { return ((args.seed >= 0 ? args.seed : 0) + 1 + frame) % 2147483647LL; }
// Which program and which film made the frames: the program's file (size and time) and the film's scene at two instants. When it is the same as last time, a resumed video keeps every
// finished frame without looking at it; when it is not (the program was rebuilt, the film edited), each finished frame's scene is made again and compared with the fingerprint it was
// rendered from (frame_fingerprint.h), and only the frames that changed are rendered again. Written beside the frames (program.txt) once a run has compared them all.
inline std::string programStamp(const cartoon::Film &film, const std::string &exePath) {
	std::string s;
	if (!exePath.empty()) {
		std::error_code ec;
		const auto size = std::filesystem::file_size(exePath, ec);
		const auto when = std::filesystem::last_write_time(exePath, ec);
		s += std::to_string(static_cast<unsigned long long>(size)) + "|" + std::to_string(static_cast<long long>(when.time_since_epoch().count()));
	}
	s += "|" + frame_fingerprint::of(film.sceneAt(0.0)) + "|" + frame_fingerprint::of(film.sceneAt(film.duration * 0.5));
	// (and its cameras file, read when a frame is made: an edit to one shot's camera changes neither the program nor, often, those two instants)
	const std::string cameras = cartoon::film_cache::asset(std::string(film.key) + "/cameras.txt");
	if (!cameras.empty()) s += "|cameras " + std::to_string(static_cast<unsigned long long>(frame_fingerprint::fileHash(cameras)));
	// (and its cues file: a voice moves a character - music_cues::voiceAt)
	const std::string cues = cartoon::film_cache::asset(std::string(film.key) + "/cues.txt");
	if (!cues.empty()) s += "|cues " + std::to_string(static_cast<unsigned long long>(frame_fingerprint::fileHash(cues)));
	// (and the film file and scene it is made from: an edit to one shot - a camera, a walk - changes neither the program nor, often, the two instants above, and a resumed
	// video kept that shot's old frames)
	for (const std::string &file : cartoon::sourceFilesOf(film.key)) s += "|file " + std::to_string(static_cast<unsigned long long>(frame_fingerprint::fileHash(file)));
	return s;
}
constexpr const char *kFingerprints = "fingerprints.txt";
constexpr const char *kProgramStamp = "program.txt";
// The fingerprints recorded beside the frames: frame -> what its picture was rendered from (the last line for a frame wins).
inline std::map<int, std::string> readFingerprints(const std::filesystem::path &dir) {
	std::map<int, std::string> out;
	std::ifstream in(dir / kFingerprints);
	int frame = 0;
	std::string fp;
	while (in >> frame >> fp) out[frame] = fp;
	return out;
}
inline void recordFingerprint(const std::filesystem::path &dir, int frame, const std::string &fp) {
	if (fp.empty()) return;
	std::ofstream out(dir / kFingerprints, std::ios::app);
	out << frame << " " << fp << "\n";
}
// What a resumed video does with each frame: keep it as it is, check it (make its scene again and render it only when its fingerprint has changed), or render it.
struct ResumePlan {
	std::vector<char> keep;              // finished and kept without a look
	std::map<int, std::string> check;    // finished: kept only if its scene's fingerprint is still this
	int kept = 0, unknown = 0;           // (unknown: finished, but from before fingerprints were kept, and the program or film has changed: rendered again)
};
inline ResumePlan planResume(int count, const std::function<bool(int)> &finished, const std::map<int, std::string> &fingerprints, bool sameProgram) {
	ResumePlan p;
	p.keep.assign(static_cast<std::size_t>(std::max(0, count)), 0);
	for (int f = 0; f < count; ++f) {
		if (!finished(f)) continue;
		if (sameProgram) {
			p.keep[static_cast<std::size_t>(f)] = 1;
			++p.kept;
		} else if (const auto it = fingerprints.find(f); it != fingerprints.end()) {
			p.check[f] = it->second;
		} else {
			++p.unknown;
		}
	}
	return p;
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
// The frames fillMissingFrames made, listed in the frames folder so that a resumed video renders them again instead of keeping the copies.
constexpr const char *kFilledList = "filled.txt";
// A frame that failed leaves a hole in the sequence, which would shorten the video and put its sound out of step; each hole is filled with the frame before it (the first with the next
// one). The copy keeps its source's file time, so that a frame already captioned (film_captions.h, which knows a file by its size and time) is known as captioned in its copy too. Returns
// how many were filled.
inline int fillMissingFrames(const std::filesystem::path &dir, int count) {
	int first = -1;
	for (int i = 0; i < count && first < 0; ++i)
		if (std::filesystem::exists(dir / encName(i))) first = i;
	if (first < 0) return 0;
	int filled = 0;
	std::ofstream list(dir / kFilledList, std::ios::app);
	for (int i = 0; i < count; ++i) {
		if (std::filesystem::exists(dir / encName(i))) continue;
		const int from = i == 0 ? first : i - 1;
		std::error_code ec;
		std::filesystem::copy_file(dir / encName(from), dir / encName(i), std::filesystem::copy_options::overwrite_existing, ec);
		if (ec) continue;
		std::filesystem::last_write_time(dir / encName(i), std::filesystem::last_write_time(dir / encName(from), ec), ec);
		list << i << "\n";
		++filled;
	}
	return filled;
}
// The frames fillMissingFrames has made (copies of a neighbour) in `dir`.
inline std::set<int> filledFrames(const std::filesystem::path &dir) {
	std::set<int> out;
	std::ifstream in(dir / kFilledList);
	for (int i = 0; in >> i;) out.insert(i);
	return out;
}
// Before a video is resumed: the frames that were copies of their neighbours are removed (and the list with them), so that they are rendered this time.
inline int dropFilledFrames(const std::filesystem::path &dir) {
	std::ifstream in(dir / kFilledList);
	int dropped = 0;
	std::error_code ec;
	for (int i = 0; in >> i;)
		if (std::filesystem::remove(dir / encName(i), ec)) ++dropped;
	in.close();
	std::filesystem::remove(dir / kFilledList, ec);
	return dropped;
}

// The soundtrack. A film with one (Film::soundtrack, cartoon_audio.h; every film has one) has it mixed and written as a 16-bit stereo .wav; the video then gets it. It is made
// for the film's own length: a video of the whole film at any frame rate takes it as it is when it lasts as long, and stretched to its length at the same pitch when it is longer
// or shorter (another frame count, --speed; cartoon_audio.h `stretched`), so its sounds stay on their pictures. `videoSeconds` 0: the film's own length.
inline double videoSecondsOf(int frames, int fps) { return static_cast<double>(frames) / std::max(1, fps); }
inline bool soundtrackStretched(const cartoon::Film &film, int frames, int fps) { return std::abs(videoSecondsOf(frames, fps) - film.duration) > 0.001 * film.duration; }
inline bool writeSoundtrack(const cartoon::Film &film, const std::filesystem::path &wavPath, double videoSeconds = 0.0) {
	if (!film.soundtrack) return false;
	cartoon::audio::Mix mix(film.duration);
	film.soundtrack(mix);
	std::vector<std::int16_t> pcm = mix.finish();
	if (videoSeconds > 0.0) pcm = cartoon::audio::stretched(pcm, static_cast<std::size_t>(videoSeconds * cartoon::audio::kRate));
	return cartoon::audio::writeWav(wavPath.string(), pcm);
}

// ---- long-lived workers (cartoon_worker.h) ----
// How many worker processes render a cartoon video's frames: --cartoon-workers N, else the environment variable RAY_TRACER_CARTOON_WORKERS, else on an OptiX GPU as many as the frame's
// load calls for (RunPolicy::framesInFlight: 2 to 4 within the free memory; two when it is not given) and none elsewhere (the CPU renders every frame with all its cores, so a process
// start is nothing to it; Metal frames are rendered by a process each). 0 means a process per frame.
inline int workersFor(const LaunchArgs &args, bool gpu, bool metal, const FrameLoad &load = {}) {
	int n = args.cartoon_workers;
	if (n < 0) {
		if (const char *env = std::getenv("RAY_TRACER_CARTOON_WORKERS")) n = std::atoi(env);
	}
	if (n < 0) n = RunPolicy::from(args).defaultWorkers(gpu, metal, load);
	return std::max(0, std::min(n, 4));
}

// What the pool of workers needs from the outside (so that it can be tested without processes): a way to start a worker on a list of frames, to see whether it still runs and to stop it,
// and to ask whether a frame's marker is in.
struct WorkerHandle {
	virtual ~WorkerHandle() = default;
	virtual bool running() = 0;
	virtual void kill() = 0;
	virtual unsigned long long memoryBytes() { return 0; }   // what the worker holds now; 0 when it cannot be said
};

// How much memory a worker may hold before the pool replaces it (between two frames, not counted as a failed frame): --worker-max-mb N, else RAY_TRACER_WORKER_MAX_MB, else no limit
// (0 = never; the default since 2026-10-23, so that a render uses the computer fully unless asked otherwise).
// A worker's memory is flat in the measurements made (about 440 MB over 120 frames at 640 x 360), so this is protection against a leak that appears in a film or a driver, not a measure of
// anything that happens today.
inline unsigned long long workerMemoryLimitBytes(const LaunchArgs &args) {
	long long mb = 0;
#ifdef _MSC_VER
#pragma warning(suppress : 4996)
#endif
	if (args.worker_max_mb >= 0) mb = args.worker_max_mb;
	else if (const char *env = std::getenv("RAY_TRACER_WORKER_MAX_MB")) mb = std::atoll(env);
	return mb > 0 ? static_cast<unsigned long long>(mb) * 1024ull * 1024ull : 0ull;
}
struct WorkerHooks {
	std::function<std::unique_ptr<WorkerHandle>(int worker, const std::vector<int> &frames)> start;   // null: it could not be started
	std::function<bool(int frame, int &result)> poll;   // true and the frame's result when its marker is in; the marker is used up
	// True while the graphics card's memory is used up (null: never asked). Frames that do not fit side by side are paged to and fro and every worker slows to a crawl, so when it stays
	// full for `cardFullSeconds` one worker fewer runs at a time (down to one).
	std::function<bool()> cardFull;
	double cardFullSeconds = 20.0, cardCheckSeconds = 2.0;
	// Told how many workers render at once from now on, when the card's memory has made it fewer (null: nobody asks); before the workers that go on are started again, so they
	// can be given the number (smart power's pauses follow it: RunPolicy::smartFrames).
	std::function<void(int workers)> workersNow;
};

// Renders `todo` (frame numbers) with `workers` long-lived workers: worker w is given every workers-th frame and renders its list in order. The pool watches the markers the workers
// leave; `completed(frame, result, seconds)` is called as each frame is finished (0 is success) and `inOrder(frame, result)` once it and every frame of `todo` before it are, which is what
// the video's picture sequence needs. A worker that has not finished a frame in `stallSeconds` (0: never) is killed, and one that ends with frames left is replaced: a new worker takes
// the rest of the list, starting with the frame that was in hand; a frame that has taken a worker down twice fails (result -1) and the new worker starts after it. Returns the number of
// frames whose result was 0.
inline int runWorkers(std::vector<int> todo, int workers, int stallSeconds, const WorkerHooks &hooks, const std::function<void(int frame, int result, double seconds)> &completed,
					  const std::function<void(int frame, int result)> &inOrder, int pollMilliseconds = 25, unsigned long long maxWorkerBytes = 0, int firstFrameStallSeconds = 0) {
	using Clock = std::chrono::steady_clock;
	std::sort(todo.begin(), todo.end());
	if (todo.empty()) return 0;
	workers = std::max(1, std::min(workers, static_cast<int>(todo.size())));
	struct Worker {
		std::deque<int> remaining;
		std::unique_ptr<WorkerHandle> handle;
		int framesSinceStart = 0;   // frames this worker (process) has finished: a worker is only replaced for its size after it has made some progress, so replacing never loops
		int restartAfter = -1;      // when this frame is finished the worker is started again, on a list it was given since (a stopped worker's frames: see cardFull below)
		std::chrono::steady_clock::time_point last = std::chrono::steady_clock::now();   // (not the function's `Clock` alias: MSVC does not find a function-local alias inside a local struct)
	};
	std::vector<Worker> ws(static_cast<std::size_t>(workers));
	for (std::size_t i = 0; i < todo.size(); ++i) ws[i % ws.size()].remaining.push_back(todo[i]);
	std::map<int, int> result, attempts;
	std::set<int> finished;
	std::size_t nextToHand = 0, reported = 0;
	int succeeded = 0;
	auto seconds = [](Clock::time_point since) { return std::chrono::duration<double>(Clock::now() - since).count(); };
	// a worker's first frame may have a longer limit (firstFrameStallSeconds: on a GPU it also compiles the shaders when their cache is cold)
	const auto limitFor = [&](const Worker &worker) { return worker.framesSinceStart == 0 && stallSeconds > 0 ? (std::max)(stallSeconds, firstFrameStallSeconds) : stallSeconds; };
	Clock::time_point lastTick = Clock::now();
	// workers at work (started, frames left); at most `allowed` at a time, one fewer each time the card stays full (hooks.cardFull): the one that began its frame last is stopped (that
	// frame is begun again, not counted against it), and its frames go to the workers that go on, in order (handOver)
	int allowed = workers;
	Clock::time_point lastCardCheck = Clock::now(), fullSince = lastCardCheck;
	bool full = false;
	const auto atWork = [&] {
		int n = 0;
		for (const Worker &x : ws) n += x.handle && !x.remaining.empty() ? 1 : 0;
		return n;
	};
	// The frames of the workers that are not running (the one just stopped for the card's memory, its frame in hand too) and the frames after the one in hand of those that are, dealt
	// out again in order to the ones that are running; each is started again on its new list when it has finished the frame in hand. The stopped worker's frames used to wait until
	// another worker had finished its whole list: half of a film's frames came at the end, and the in-order pictures (the video, the GUI's strip of frames) stalled for hours.
	const auto handOver = [&] {
		std::vector<int> rest;
		std::vector<Worker *> going;
		for (Worker &x : ws) {
			if (x.remaining.empty()) continue;
			if (x.handle) {
				going.push_back(&x);
				rest.insert(rest.end(), std::next(x.remaining.begin()), x.remaining.end());
				x.remaining.resize(1);
			} else {
				rest.insert(rest.end(), x.remaining.begin(), x.remaining.end());
				x.remaining.clear();
			}
		}
		if (going.empty()) return;   // (cannot happen: the card check stops a worker only while another runs)
		std::sort(rest.begin(), rest.end());
		for (std::size_t i = 0; i < rest.size(); ++i) going[i % going.size()]->remaining.push_back(rest[i]);
		for (Worker *x : going)
			if (x->remaining.size() > 1) x->restartAfter = x->remaining.front();
		allowed = static_cast<int>(going.size());
		if (hooks.workersNow) hooks.workersNow(allowed);
	};
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
		// This loop wakes every `pollMilliseconds`; a much longer gap means this program was paused (the GUI's Pause suspends it and its workers), and that time is nobody's frame.
		const Clock::time_point now = Clock::now();
		const auto gap = now - lastTick;
		lastTick = now;
		if (gap > std::chrono::seconds(5))
			for (Worker &worker : ws) worker.last += gap;
		if (hooks.cardFull && allowed > 1 && seconds(lastCardCheck) >= hooks.cardCheckSeconds) {
			lastCardCheck = Clock::now();
			if (!hooks.cardFull()) {
				full = false;
			} else if (!full) {
				full = true, fullSince = lastCardCheck;
			} else if (seconds(fullSince) >= hooks.cardFullSeconds && atWork() > 1) {
				Worker *latest = nullptr;
				for (Worker &x : ws)
					if (x.handle && !x.remaining.empty() && (!latest || x.last > latest->last)) latest = &x;
				latest->handle->kill();
				latest->handle.reset();
				handOver();
				full = false;
				std::cerr << "\nNote: the graphics card's memory is used up (these frames do not fit " << allowed + 1 << " at a time); going on with " << allowed << " worker" << (allowed == 1 ? "" : "s")
						  << " at a time, which take up the stopped one's frames in order." << std::endl;
			}
		}
		for (std::size_t w = 0; w < ws.size(); ++w) {
			Worker &worker = ws[w];
			if (worker.remaining.empty()) continue;
			if (!worker.handle) {
				if (atWork() >= allowed) continue;   // (stopped for the card's memory: it waits for a worker to finish)
				worker.last = Clock::now();
				worker.handle = hooks.start(static_cast<int>(w), std::vector<int>(worker.remaining.begin(), worker.remaining.end()));
				worker.framesSinceStart = 0;
				worker.restartAfter = -1;   // (started on its whole list)
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
				++worker.framesSinceStart;
			}
			if (worker.remaining.empty()) continue;
			// given more frames (handOver) and the frame that was in hand is done: started again on the new list (what it began after that frame is begun again)
			if (worker.restartAfter >= 0 && worker.remaining.front() != worker.restartAfter) {
				worker.restartAfter = -1;
				worker.handle->kill();
				worker.handle.reset();
				worker.last = Clock::now();
				continue;
			}
			// Too big: replaced now, between two frames (the frame it has started is begun again by the next worker), and not counted against that frame: nothing went wrong with it.
			if (maxWorkerBytes > 0 && worker.framesSinceStart > 0 && worker.handle->running()) {
				const unsigned long long held = worker.handle->memoryBytes();
				if (held > maxWorkerBytes) {
					std::cerr << "\nNote: a worker has grown to " << held / (1024 * 1024) << " MB (the limit is " << maxWorkerBytes / (1024 * 1024) << " MB, RAY_TRACER_WORKER_MAX_MB); starting another." << std::endl;
					worker.handle->kill();
					worker.handle.reset();
					worker.last = Clock::now();
					continue;
				}
			}
			const bool ended = !worker.handle->running();
			if (ended && hooks.poll(worker.remaining.front(), rc)) {   // (its last marker came just before it ended)
				report(worker.remaining.front(), rc, seconds(worker.last));
				worker.remaining.pop_front();
				worker.last = Clock::now();
				worker.handle.reset();
				continue;
			}
			const int limit = limitFor(worker);
			const bool stalled = limit > 0 && seconds(worker.last) > limit;
			if (ended || stalled) {
				if (stalled) worker.handle->kill();
				worker.handle.reset();
				const int frame = worker.remaining.front();
				if (++attempts[frame] >= 2) {
					std::cerr << "\nWARNING: frame " << frame << " " << (stalled ? "took longer than " + std::to_string(limit) + " s" : std::string("ended its worker")) << " twice and is given up." << std::endl;
					report(frame, -1, seconds(worker.last));
					worker.remaining.pop_front();
				} else {
					std::cerr << "\nWARNING: a worker " << (stalled ? "did not finish frame " + std::to_string(frame) + " in " + std::to_string(limit) + " s" : "ended while rendering frame " + std::to_string(frame)) << "; starting another." << std::endl;
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

// How many frames are rendered at once: --cartoon-jobs N, else the environment variable RAY_TRACER_CARTOON_JOBS, else on a GPU what the frame's load calls for (as workersFor) and one on
// the CPU (a CPU render already uses every core, so running several only has them compete). Between 1 and 8.
inline int jobsFor(const LaunchArgs &args, bool gpu, const FrameLoad &load = {}) {
	int n = args.cartoon_jobs;
	if (n <= 0) {
		if (const char *env = std::getenv("RAY_TRACER_CARTOON_JOBS")) n = std::atoi(env);
	}
	if (n <= 0) n = RunPolicy::from(args).defaultFrameJobs(gpu, load);
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
