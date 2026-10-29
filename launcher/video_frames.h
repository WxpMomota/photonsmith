#pragma once
// video_frames.h -- the folder a video's frames are rendered into (<output's folder>/frames, or frames-<film key> for a cartoon), and what becomes of what is in it when a video starts.
//
// A video that starts afresh empties the folder, but only one this program made: the folder is named after the output's folder, where someone may well keep a folder of their own
// called "frames" (it used to be deleted whatever it held). A video that resumes keeps its frames, and the record of the frames that have their words (film_captions.h: without it
// they would get them a second time), and renders again the frames that were copies of a neighbour (cartoon_render.h, fillMissingFrames).

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include "cartoon_render.h"
#include "film_captions.h"
#include "film_temporal.h"
#include "film_transitions.h"

namespace video_frames {

// The file that marks a frames folder as this program's (startAfresh writes it).
constexpr const char *kMarker = "ray_tracer_frames.txt";

// Whether `dir` is a frames folder this program made: it has the marker, or a cartoon's resume signature (resume.txt), or holds nothing but the files a video's frames are (a folder
// from before the marker). An empty folder is anybody's to use.
inline bool isOurs(const std::filesystem::path &dir) {
	std::error_code ec;
	if (std::filesystem::exists(dir / kMarker, ec) || std::filesystem::exists(dir / "resume.txt", ec)) return true;
	for (const auto &entry : std::filesystem::directory_iterator(dir, ec)) {
		const std::string name = entry.path().filename().string();
		const std::string ext = entry.path().extension().string();
		const bool ours = entry.is_directory() ? name.rfind("work", 0) == 0 || name == film_transitions::kAcrossFolder || name == film_temporal::kFolder
											   : (name.rfind("enc_", 0) == 0 || name.rfind(film_captions::kCleanPrefix, 0) == 0 || name.rfind(film_transitions::kPlainPrefix, 0) == 0 || name.rfind("frame_", 0) == 0 || name.rfind("frames", 0) == 0 || ext == ".done" || name == cartoon_render::kFilledList || name == cartoon_render::kFingerprints || name == cartoon_render::kProgramStamp);
		if (!ours) return false;
	}
	return true;
}

// Removes `dir` and everything in it, trying again for up to `patienceSeconds` while something holds a file in it. On Windows a picture that was just written is often opened for a
// moment by something else - the virus scanner, the search indexer, Explorer's thumbnails, the GUI's frames strip - and a removal that meets it fails with "being used by another
// process" (a video that was to start afresh over an old one failed so, 2026-10-28, with nothing holding the folder a second later). Each try removes what it can, so a later
// one has less left. Throws the last std::filesystem::filesystem_error when the folder is still held after that.
inline void removePatiently(const std::filesystem::path &dir, double patienceSeconds = 10.0) {
	const auto until = std::chrono::steady_clock::now() + std::chrono::duration<double>(patienceSeconds);
	for (;;) {
		std::error_code ec;
		std::filesystem::remove_all(dir, ec);
		if (!ec) return;
		if (std::chrono::steady_clock::now() >= until) throw std::filesystem::filesystem_error("remove_all", dir, ec);
		std::this_thread::sleep_for(std::chrono::milliseconds(250));
	}
}

// Empties `dir` (when it is ours: false, and nothing touched, when it is not) and marks it as ours. Throws std::filesystem::filesystem_error when the folder cannot be emptied or made
// (removePatiently: after waiting for whatever held a file in it).
inline bool startAfresh(const std::filesystem::path &dir, double patienceSeconds = 10.0) {
	if (std::filesystem::exists(dir)) {
		if (!isOurs(dir)) return false;
		removePatiently(dir, patienceSeconds);
	}
	std::filesystem::create_directories(dir);
	std::ofstream(dir / kMarker) << "The frames of a video being made by ray_tracer; this folder is emptied when the next video is made.\n";
	return true;
}

// Prepares `dir` for a video that resumes: pictures that were being written when the earlier run stopped, its frame markers and its working folders go; the frames, the captions and
// transitions records (with the pictures they keep: clean_, plain_) and the other side of the transitions (the `across` folder, resumed the same way) stay; the frames that were
// copies of a neighbour go too, so that they are rendered this time. Returns how many of those there were.
inline int keepForResume(const std::filesystem::path &dir) {
	for (const auto &entry : std::filesystem::directory_iterator(dir)) {
		const std::string name = entry.path().filename().string();
		if (name.find(".tmp.png") != std::string::npos || entry.path().extension() == ".ppm" ||
			(entry.path().extension() == ".done" && name != film_captions::kRecord && name != film_transitions::kRecord))
			std::filesystem::remove(entry.path());
		else if (entry.is_directory() && name == film_transitions::kAcrossFolder) keepForResume(entry.path());
		else if (entry.is_directory() && name == film_temporal::kFolder) continue;   // (the denoised frames: each is made again only when its neighbours changed)
		else if (entry.is_directory()) std::filesystem::remove_all(entry.path());
	}
	return cartoon_render::dropFilledFrames(dir);
}

}  // namespace video_frames
