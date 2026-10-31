#pragma once
// cartoons.h -- the cartoon films: each is a function of time that returns the pbrt scene for that instant (cartoon_lib.h). Listing a film in the table (cartoons.cpp) is all it takes to make
// it a video preset (video_preset.h builds one preset per film) and a scene of the scene list (cartoon_scenes.cpp). A cartoon is chosen with `--camera-path cartoon:<key>` (or
// `--video-preset <key>`), and the launcher then renders every frame from its own scene instead of moving a camera round a fixed one.
//
// Everything the program says about a film comes from its row and the film's own shot list, and nothing is typed twice: the length, the frame count and the shot count are worked out
// (the descriptions do not repeat them), the video preset's id is the row's `meta.id`, and docs/VIDEO_GENERATION.md's film table is the text `ray_tracer --film-table` prints (a test fails
// when it is out of date).
//
// This header only declares: the films' code and the film-file reader are compiled once, in cartoons.cpp (cpu_renderer), so including it costs a file that names films (the launcher's
// arguments, the video presets) next to nothing. A file that works on a film itself includes that film's header or film_file.h; cartoons_registry.h has how the list is made.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "cartoon_lib.h"

namespace cartoon {

namespace film_file {
class DataFilm;
}

constexpr const char *kCameraPathPrefix = "cartoon:";
constexpr const char *kStillSlugPrefix = "cartoon-";   // the scene a film's still is listed as: "cartoon-fireflies" (and its file, cartoon-fireflies.pbrt)

// The words a film's tags are chosen from. They name what a film shows or how it is lit, so that the scene list's search can find "metal" or "dusk".
constexpr const char *kTagVocabulary[] = {"character", "physics", "particles", "metal", "water", "subsurface", "translucent", "emissive", "backlight", "dof", "reflections", "night", "dusk", "fog", "nature", "dawn", "comedy", "glass"};

// The built-in films, in the table's order (cartoons.cpp): `for (const Film &f : kFilms)`, `kFilms[0]`, `kFilms.size()`.
struct FilmTable {
	const Film *begin() const;
	const Film *end() const;
	std::size_t size() const { return static_cast<std::size_t>(end() - begin()); }
	const Film &operator[](std::size_t i) const { return begin()[i]; }
};
inline constexpr FilmTable kFilms{};

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// All the films: the built-in ones, and the ones made from files (film_file.h)
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// Where film files (*.film) are looked for: the folders scenes are (RAY_TRACER_PBRT_DIR, pbrt_scenes beside the program or up from it, the per-user scenes folder).
std::vector<std::string> filmFileDirs();
// Reads the film files saved since the list was made, and those left out for a mistake that have been saved since, adding their films at the end of films(). How many it added.
int readNewFilmFiles();

// Every film: the built-in ones in the table's order, then those made from film files (in the order of their file names).
const std::vector<const Film *> &films();
// What was wrong with the film files that were left out ("file:line: what"), one entry each.
const std::vector<std::string> &filmFileProblems();
// Why there is no film called `key` (or "cartoon:key", "cartoon:key@t") when a film file of that name could not be read: what "unknown cartoon" goes on to say, so a
// film left out for a mistake in its file (or a word only a newer program knows) is not just missing. "" when no such file failed.
std::string whyNoFilm(std::string key);
// A film made from a file read again when its file (or its scene) was saved since: true when it was (a film timeline follows the edits). Its Film keeps pointing at it,
// so frames made after it are of the new film; its length and its shots' names in the film table stay as they were until the program starts again.
// (A film in parts has each part read again: they are its film file's "<key>--1", "<key>--2" ...)
bool refreshFilmFile(const std::string &key);
// The files a film made from a file is read from: its film file and its scene (for each part of a film in parts). None for a built-in film. What a resumed video
// compares (launcher/cartoon_render.h programStamp): an edit to them changes the frames.
std::vector<std::string> sourceFilesOf(const std::string &key);
// The film file a film was made from and its shots as they are now (read again first when it was saved since): for the changes the GUI's film timeline writes to it
// (film_edit.h). An empty path for a built-in film. A film in parts: its parts' shots in order (their names; the starts in the whole film).
struct FileShots {
	std::string path;
	std::vector<double> starts;
	std::vector<std::string> names;
};
FileShots fileShotsOf(const std::string &key);
// The film file a film was made from (null for a built-in film): its characters and their lines. (A film in parts: each part by its own key, "<key>--2".)
const film_file::DataFilm *filmFileOf(const std::string &key);
// The film called `key`, or null.
const Film *find(const char *key);

// The names one film goes by, written once. Everything that needs one of them calls these (the video presets, the launcher, the stills, the GUI) rather than joining strings itself.
//   key           "fireflies"            Film::key: how the table and the command line name it
//   camera path   "cartoon:fireflies"    --camera-path, the GUI's Camera Path entry, the video preset's path
//   still scene   "cartoon-fireflies"    the slug of its still in the scene list (the scene list may add a suffix when a scene of the user's has the name; the still's own
//                                        `# @rt-cartoon <key>` tag, not this, is what links a listed scene back to its film)
//   preset id     "V10"                  the film's `meta.id`: stable, whatever the order of the table
inline std::string cameraPathOf(const Film &film) { return std::string(kCameraPathPrefix) + film.key; }
inline std::string stillSlugOf(const Film &film) { return std::string(kStillSlugPrefix) + film.key; }
inline std::string presetIdOf(const Film &film) { return "V" + std::to_string(film.meta.id); }

// The film a camera-path name asks for ("cartoon:lamp-and-ball"), or null when the name is an ordinary camera path.
const Film *forCameraPath(const char *cameraPath);
inline bool isCartoonPath(const char *cameraPath) { return cameraPath && std::strncmp(cameraPath, kCameraPathPrefix, std::strlen(kCameraPathPrefix)) == 0; }

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// What a film says about itself, worked out from its row
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
inline std::vector<std::string> splitList(const char *list, char separator) {
	std::vector<std::string> out;
	std::string item;
	for (const char *p = list ? list : ""; ; ++p) {
		if (*p == separator || *p == '\0') {
			if (!item.empty()) out.push_back(item);
			item.clear();
			if (*p == '\0') break;
		} else {
			item += *p;
		}
	}
	return out;
}
inline std::vector<std::string> tagsOf(const Film &film) { return splitList(film.meta.tags, ','); }
inline std::vector<std::string> shotNamesOf(const Film &film) { return splitList(film.meta.shotNames, '|'); }
inline int shotCountOf(const Film &film) { return static_cast<int>(film.shotStarts().size()); }
inline int frameCountOf(const Film &film) { return static_cast<int>(std::lround(film.duration * film.fps)); }
// What a program that sets up a film's video needs of every film, one line a film, tab-separated: key, name, seconds, fps, still time, width, height, samples, wavefront (0/1),
// soundtrack (0/1), the shot starts (comma-separated) and the shot names ('|'-separated). The GUI reads it from the renderer's library (scene_metadata_film_table; qt_gui/film_info.h)
// instead of compiling the films itself: a GUI not rebuilt with a new film gave its video the wrong length, and every film edit rebuilt most of the GUI.
std::string filmTable();

inline std::string join(const std::vector<std::string> &items, const char *separator) {
	std::string out;
	for (const std::string &s : items) out += (out.empty() ? "" : separator) + s;
	return out;
}
// "26 s": the length, in whole seconds when it is one.
inline std::string lengthText(const Film &film) {
	char buf[32];
	std::snprintf(buf, sizeof(buf), "%g s", film.duration);
	return buf;
}
// "about 5 s": what one 1280 x 720 frame takes, or "" when it has not been measured.
inline std::string costText(const Film &film) {
	if (!(film.meta.secondsPerFrame > 0.0)) return std::string();
	char buf[48];
	std::snprintf(buf, sizeof(buf), "about %.0f s", std::max(1.0, film.meta.secondsPerFrame));
	return buf;
}

// The story, then the facts: "<description> 26 s in 4 shots (a, b, c, d). Tags: x, y. Rendering a frame takes about 5 s (1280 x 720, 192 samples, GPU)." What the scene list shows for the
// film's still.
inline std::string summaryOf(const Film &film) {
	std::string out = std::string(film.description) + " " + lengthText(film) + " in " + std::to_string(shotCountOf(film)) + (shotCountOf(film) == 1 ? " shot" : " shots");
	const std::vector<std::string> shots = shotNamesOf(film);
	if (!shots.empty()) out += " (" + join(shots, ", ") + ")";
	out += ".";
	if (!tagsOf(film).empty()) out += " Tags: " + join(tagsOf(film), ", ") + ".";
	if (!costText(film).empty()) out += " A frame takes " + costText(film) + " (1280 x 720, " + std::to_string(film.meta.samples) + " samples, GPU, denoised).";
	return out;
}

// One row of docs/VIDEO_GENERATION.md's film table (what `ray_tracer --film-table` prints, one per film).
inline std::string markdownRowOf(const Film &film) {
	return "| " + std::string(film.key) + " | " + film.name + " | " + lengthText(film) + " | " + std::to_string(shotCountOf(film)) + " | " + join(tagsOf(film), ", ") + " | " + film.description + " |";
}
std::string markdownTable();

// One frame of a film as a still scene: "cartoon:<key>" (the film's stillTime) or "cartoon:<key>@<seconds>". Used where a scene is named, `ray_tracer ... cartoon:fireflies@13 `.
struct Still {
	const Film *film = nullptr;
	double t = 0.0;
};
// Parses a still's name. Returns false when `spec` is not one (`error` stays empty) or is a bad one (`error` says why).
bool parseStill(const std::string &spec, Still &out, std::string &error);

}  // namespace cartoon
