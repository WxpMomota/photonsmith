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
#include "process.h"
#include "launcher_args.h"
#include "render_setup.h"
#include "../src/shared/cartoon/cartoons.h"

namespace cartoon_worker {

struct Job {
	std::string film, scenes, out;
	// what the frames' picture files are called: "png" when the renderer writes the PNG itself (a GPU), "ppm" when the video converts a text PPM
	std::string ext = "ppm";
	int frames = 0;
	std::vector<int> list;
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
			else if (verb == "list")
				for (int f; words >> f;) list.push_back(f);
		}
		filmPtr = cartoon::find(film.c_str());
		if (!filmPtr) return "the job names a film (" + film + ") this program does not have";
		if (frames < 1 || list.empty() || scenes.empty() || out.empty()) return "the job file " + file + " is incomplete";
		for (int f : list)
			if (f < 0 || f >= frames) return "the job lists a frame (" + std::to_string(f) + ") outside the video";
		return std::string();
	}

	// Writes the scene of `frame` to scenePath(); false when it cannot be written. `eye` is where the scene's own camera is (the first LookAt of its header).
	bool writeScene(int frame, double eye[3]) const {
		try {
			const std::string header = filmPtr->headerAt(timeOf(frame));
			static const std::regex look("LookAt\\s+(\\S+)\\s+(\\S+)\\s+(\\S+)");
			std::smatch m;
			if (std::regex_search(header, m, look))
				for (int i = 0; i < 3; ++i) eye[i] = std::stod(m[i + 1].str());
			std::ofstream f(scenePath(), std::ios::binary | std::ios::trunc);
			if (!f) {
				std::cerr << "ERROR: frame " << frame << ": could not write " << scenePath() << std::endl;
				return false;
			}
			f << filmPtr->sceneAt(timeOf(frame));
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
		if (job.writeScene(frame, eye)) {
			LaunchArgs a = args;
			RenderSetup s = setup;
			a.cam_explicit = true;
			a.cam_x = s.cam_x = eye[0];
			a.cam_y = s.cam_y = eye[1];
			a.cam_z = s.cam_z = eye[2];
			s.out_path = job.picturePath(frame);
			rc = render(a, s);
		}
		// written under another name and moved into place, so the parent never reads half a marker
		const std::string marker = job.markerPath(frame), temporary = marker + ".tmp";
		{
			std::ofstream out(temporary, std::ios::trunc);
			out << rc << "\n";
		}
		std::error_code ec;
		std::filesystem::rename(temporary, marker, ec);
		std::cout.flush();
		background_mode::pauseAfter(policy, std::chrono::duration<double>(std::chrono::steady_clock::now() - frameStart).count());   // (--background: leave the machine to its user for a while)
	}
	return 0;
}

}  // namespace cartoon_worker
