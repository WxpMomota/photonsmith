#pragma once
// cartoon_worker.h -- a cartoon video's frames rendered by ONE long-lived process instead of one process each.
//
// A frame run costs about a second and a half before it renders anything (the process starts, OptiX makes its context and builds its pipeline), and at a draft quality that is most of
// the frame: 2.4 s for a frame whose rendering takes a fraction of a second. A worker (`ray_tracer ... --cartoon-worker JOBFILE`) starts once and renders a list of frames in a row:
// for each it writes the frame's scene to the same file, which the renderer sees has changed (it keeps what is on the GPU by the scene's file and the time it was written, so the next
// build uploads the new scene and reuses everything else), renders it with the camera that frame's scene says, and writes the picture and then a marker file (`frame_0172.done`,
// holding the result) that tells the parent the picture is whole. The parent (cartoon_render.h, runWorkers) watches the markers; a worker that stops making progress, or ends, is
// replaced by a new one that carries on from the frame that was in hand.
//
// The job file is plain text:
//     film   fireflies                 the film's key
//     frames 624                       how many frames the video has (a frame's time is the film's length * frame / frames)
//     scenes C:/out/frames/work0       a folder of the worker's own, for the one scene file it rewrites
//     out    C:/out/frames             where the pictures and the markers go
//     list   0 2 4 6                   the frames to render, in this order
//     keep   4 0123456789abcdef        (any number) a finished frame and the fingerprint it was rendered from: not rendered again if its scene's fingerprint is still that
//
// A marker holds the result, the fingerprint of the scene the frame was made from (frame_fingerprint.h), and "kept" when the frame was not rendered because it had not changed.

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include "background_mode.h"
#include <map>

#include "frame_fingerprint.h"
#include "process.h"
#include "launcher_args.h"
#include "render_setup.h"
#include "../src/shared/cartoon/cartoons.h"
#include "../src/shared/cartoon/shot_cameras.h"   // a shot's share of the samples

namespace cartoon_worker {

struct Job {
	std::string film, scenes, out;
	// what the frames' picture files are called: "png" when the renderer writes the PNG itself (a GPU), "ppm" when the video converts a text PPM
	std::string ext = "ppm";
	int frames = 0;
	std::vector<int> list;
	std::map<int, std::string> keep;   // finished frames and the fingerprints they were rendered from
	bool across = false;               // the other side of the film's dissolves and wipes (Film::sceneAcross), not its own frames
	bool seeds = false;                // each frame renders with a seed of its own (its noise is its own: --temporal-denoise averages it away), not one for all
	const cartoon::Film *filmPtr = nullptr;

	std::string scenePath() const { return (std::filesystem::path(scenes) / "frame.pbrt").string(); }
	std::string picturePath(int frame) const { return pathFor(frame, ext.c_str()); }
	std::string markerPath(int frame) const { return pathFor(frame, "done"); }
	double timeOf(int frame) const { return filmPtr->duration * frame / std::max(1, frames); }

	std::string text() const {
		std::ostringstream o;
		o << "film " << film << "\nframes " << frames << "\nscenes " << scenes << "\nout " << out << "\next " << ext << "\nlist";
		for (int f : list) o << " " << f;
		o << "\n";
		for (const auto &[f, fp] : keep) o << "keep " << f << " " << fp << "\n";
		if (across) o << "across 1\n";
		if (seeds) o << "seeds 1\n";
		return o.str();
	}

	// Reads a job file; returns "" or what is wrong.
	std::string load(const std::string &file) {
		std::ifstream in(file);
		if (!in) return "the job file " + file + " cannot be read";
		std::string line;
		while (std::getline(in, line)) {
			std::istringstream words(line);
			std::string verb;
			words >> verb;
			if (verb == "film") words >> film;
			else if (verb == "frames") words >> frames;
			else if (verb == "scenes") std::getline(words >> std::ws, scenes);
			else if (verb == "out") std::getline(words >> std::ws, out);
			else if (verb == "ext") words >> ext;
			else if (verb == "across") words >> across;
			else if (verb == "seeds") words >> seeds;
			else if (verb == "list")
				for (int f; words >> f;) list.push_back(f);
			else if (verb == "keep") {
				int f = -1;
				std::string fp;
				if (words >> f >> fp) keep[f] = fp;
			}
		}
		filmPtr = cartoon::find(film.c_str());
		if (!filmPtr) return "the job names a film (" + film + ") this program does not have";
		if (frames < 1 || list.empty() || scenes.empty() || out.empty()) return "the job file " + file + " is incomplete";
		for (int f : list)
			if (f < 0 || f >= frames) return "the job lists a frame (" + std::to_string(f) + ") outside the video";
		return std::string();
	}

	// Writes the scene of `frame` to scenePath(); false when it cannot be written. `eye` is where the scene's own camera is (the first LookAt of its header); `fingerprint` gets the
	// scene's fingerprint, and `unchanged` is true (and nothing is written) when the frame is one to keep and its fingerprint is the one it was rendered from. `share` gets the
	// share of the video's samples the frame asks for (its shot's `samples` in the film's cameras file; 1 when it says nothing).
	bool writeScene(int frame, double eye[3], std::string &fingerprint, bool &unchanged, double &share) const {
		unchanged = false;
		share = 1.0;
		try {
			const bool otherSide = across && filmPtr->sceneAcross;
			const std::string text = otherSide ? filmPtr->sceneAcross(timeOf(frame)) : filmPtr->sceneAt(timeOf(frame));
			const std::string header = otherSide ? text : filmPtr->headerAt(timeOf(frame));   // (the other side's camera is in its own text)
			static const std::regex look("LookAt\\s+(\\S+)\\s+(\\S+)\\s+(\\S+)");
			std::smatch m;
			if (std::regex_search(header, m, look))
				for (int i = 0; i < 3; ++i) eye[i] = std::stod(m[i + 1].str());
			fingerprint = frame_fingerprint::of(text);
			share = cartoon::shot_cameras::sampleShareOf(text);
			if (const auto it = keep.find(frame); it != keep.end() && it->second == fingerprint) {
				unchanged = true;
				return true;
			}
			std::ofstream f(scenePath(), std::ios::binary | std::ios::trunc);
			if (!f) {
				std::cerr << "ERROR: frame " << frame << ": could not write " << scenePath() << std::endl;
				return false;
			}
			f << text;
			return static_cast<bool>(f);
		} catch (const std::exception &e) {   // (the film's code failed on this frame: why goes to this worker's log, which the launcher's failure line points to)
			std::cerr << "ERROR: frame " << frame << " of " << film << ": " << e.what() << std::endl;
			return false;
		}
	}

  private:
	std::string pathFor(int frame, const char *ext) const {
		char name[48];
		std::snprintf(name, sizeof(name), "frame_%04d.%s", frame, ext);
		return (std::filesystem::path(out) / name).string();
	}
};

// Renders the job's frames one after the other with `render` (the launcher's single-image render): the arguments and the setup are the user's, with this frame's output file and camera.
// Returns 0. A frame that fails leaves its marker with the result and the worker goes on.
inline int run(const Job &job, const LaunchArgs &args, const RenderSetup &setup, const std::function<int(const LaunchArgs &, const RenderSetup &)> &render) {
	std::filesystem::create_directories(job.scenes);
	const RunPolicy policy = RunPolicy::from(args);
	const ParentWatch parent;   // (POSIX: ends this worker with the launcher that started it; process.h)
	for (int frame : job.list) {
		if (parent.parentGone()) {
			std::cerr << "The program that started this worker has ended: stopping." << std::endl;
			return 0;
		}
		const auto frameStart = std::chrono::steady_clock::now();
		double eye[3] = {args.cam_x, args.cam_y, args.cam_z};
		int rc = -1;
		std::string fingerprint;
		bool unchanged = false;
		double share = 1.0;
		const bool written = job.writeScene(frame, eye, fingerprint, unchanged, share);
		const double sceneSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - frameStart).count();   // (its scene made: the launcher's time summary)
		if (written && unchanged) {
			rc = 0;   // (finished, and its scene has not changed: kept as it is)
		} else if (written) {
			LaunchArgs a = args;
			RenderSetup s = setup;
			a.cam_explicit = true;
			a.cam_x = s.cam_x = eye[0];
			a.cam_y = s.cam_y = eye[1];
			a.cam_z = s.cam_z = eye[2];
			s.out_path = job.picturePath(frame);
			s.samples_per_pixel = cartoon::shot_cameras::samplesFor(setup.samples_per_pixel, share);
			if (job.seeds) a.seed = cartoon_render::frameSeed(args, frame);
			rc = render(a, s);
		}
		// written under another name and moved into place, so the parent never reads half a marker
		const std::string marker = job.markerPath(frame), temporary = marker + ".tmp";
		{
			std::ofstream out(temporary, std::ios::trunc);
			out << rc << "\n" << (fingerprint.empty() ? "-" : fingerprint) << "\n" << (unchanged ? "kept" : "rendered") << "\n" << sceneSeconds << "\n";
		}
		std::error_code ec;
		std::filesystem::rename(temporary, marker, ec);
		std::cout.flush();
		background_mode::pauseAfter(policy, std::chrono::duration<double>(std::chrono::steady_clock::now() - frameStart).count());   // (--background: leave the machine to its user for a while)
	}
	return 0;
}

}  // namespace cartoon_worker
