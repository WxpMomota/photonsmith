#pragma once
// cartoons.h -- the cartoon films: each is a function of time that returns the pbrt scene for that instant (cartoon_lib.h). Listing a film in kFilms is all it takes to make it a video preset
// (video_preset.h builds one preset per film from this table) and a scene of the scene list (cartoon_scenes.cpp). A cartoon is chosen with `--camera-path cartoon:<key>` (or
// `--video-preset <key>`), and the launcher then renders every frame from its own scene instead of moving a camera round a fixed one.
//
// Everything the program says about a film comes from its row here and the film's own shot list, and nothing is typed twice: the length, the frame count and the shot count are worked out
// (the descriptions do not repeat them), the video preset's id is the row's `meta.id`, and docs/VIDEO_GENERATION.md's film table is the text `ray_tracer --film-table` prints (a test fails
// when it is out of date).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <exception>
#include <filesystem>
#include <string>
#include <vector>

#include "../pbrt_asset_check.h"

#include "ball_parade.h"
#include "candle.h"
#include "dandelion.h"
#include "domino_run.h"
#include "fireflies.h"
#include "lamp_and_ball.h"
#include "little_robot.h"
#include "meet_pip.h"
#include "newtons_cradle.h"
#include "film_file.h"
#include "orrery.h"
#include "paper_boat.h"
#include "pip_and_the_moon.h"

namespace cartoon {

constexpr const char *kCameraPathPrefix = "cartoon:";
constexpr const char *kStillSlugPrefix = "cartoon-";   // the scene a film's still is listed as: "cartoon-fireflies" (and its file, cartoon-fireflies.pbrt)

// The words a film's tags are chosen from. They name what a film shows or how it is lit, so that the scene list's search can find "metal" or "dusk".
constexpr const char *kTagVocabulary[] = {"character", "physics", "particles", "metal", "water", "subsurface", "translucent", "emissive", "backlight", "dof", "reflections", "night", "dusk"};

// The films. `meta.id` is the video preset's number (V<id>): give a new film the next number and never reuse one. `meta.secondsPerFrame` is measured by scripts/measure_cartoons.py.
inline const Film kFilms[] = {
	{"lamp-and-ball", "Lamp and Ball",
	 "A small yellow desk lamp wakes up in the dark, notices a red ball, hops over, nudges it, watches it bounce off the books and roll home, and tucks its shade over it.",
	 lamp_and_ball::kDuration, lamp_and_ball::kFps, &lamp_and_ball::sceneAt, &lamp_and_ball::headerAt, &lamp_and_ball::shotStarts, 9.1,
	 {7, "character,physics,emissive,night", "The desk", lamp_and_ball::kSamples, 3.7}},
	{"ball-parade", "Ball Parade",
	 "Seven glossy balls drop in one after another, bounce in a travelling wave on a checker floor with squash and stretch, and settle one by one into a row.",
	 ball_parade::kDuration, ball_parade::kFps, &ball_parade::sceneAt, &ball_parade::headerAt, &ball_parade::shotStarts, 4.6,
	 {8, "physics,reflections", "The row of balls", ball_parade::kSamples, 3.5}},
	{"little-robot", "The Little Robot",
	 "A small robot wakes in a junkyard at dusk, finds a seedling dying in a tin can, waters it, and watches it bloom into a glowing flower.",
	 little_robot::kDuration, little_robot::kFps, &little_robot::sceneAt, &little_robot::headerAt, &little_robot::shotStarts, 25.5,
	 {9, "character,emissive,dusk,dof", "Wide establishing|Medium on the waking robot|Tracking to the seedling|Close on the watering and the bloom|Wide crane up", little_robot::kSamples, 2.2}},
	{"fireflies", "Fireflies",
	 "A night meadow lit by fireflies: a lone firefly wanders, a second arrives, their flashes drift into step, they circle and spiral up to the moon as the meadow wakes.",
	 fireflies::kDuration, fireflies::kFps, &fireflies::sceneAt, &fireflies::headerAt, &fireflies::shotStarts, 13.0,
	 {10, "emissive,particles,night,dof", "Ground-level macro|The meeting|Orbit round the pair|Crane up over the meadow", fireflies::kSamples, 2.7}},
	{"newtons-cradle", "Newton's Cradle",
	 "Five chrome balls on strings: one is lifted and let go, the far ball flies out and back, and the swing passes from side to side and slowly dies, with the room in the chrome.",
	 newtons_cradle::kDuration, newtons_cradle::kFps, &newtons_cradle::sceneAt, &newtons_cradle::headerAt, &newtons_cradle::shotStarts, 5.0,
	 {11, "metal,reflections,physics,dof", "The whole cradle|Close-up of the strike|Wide pull back", newtons_cradle::kSamples, 2.7}},
	{"domino-run", "Domino Run",
	 "A red marble taps the first of 130 rainbow dominoes laid in a spiral; the wave runs inwards, chased by the camera, and the last domino lights a small lamp in the middle.",
	 domino_run::kDuration, domino_run::kFps, &domino_run::sceneAt, &domino_run::headerAt, &domino_run::shotStarts, 9.0,
	 {12, "physics,emissive,dof", "The spiral and the marble|The chase|Close-up at the lamp|Rising pull back", domino_run::kSamples, 3.5}},
	{"paper-boat", "The Paper Boat",
	 "A folded paper boat sails across a pond in the rain at dusk: every drop starts rings of ripples that rock the boat and cross one another, the rain eases, and the boat sails on towards the low sun.",
	 paper_boat::kDuration, paper_boat::kFps, &paper_boat::sceneAt, &paper_boat::headerAt, &paper_boat::shotStarts, 10.0,
	 {13, "water,reflections,dusk,backlight", "Low beside the water|Chase from above|Wide pull back", paper_boat::kSamples, 4.3}},
	{"orrery", "The Orrery",
	 "A turn round a brass model of a small solar system: a glowing sun, five enamel planets on arms (one with a moon, one with a ring), engraved orbits and a pair of meshing gears, all lit by the sun alone.",
	 orrery::kDuration, orrery::kFps, &orrery::sceneAt, &orrery::headerAt, &orrery::shotStarts, 6.0,
	 {14, "metal,reflections,emissive", "Wide orbit|Close-up on the third planet|Rising look down", orrery::kSamples, 3.0}},
	{"dandelion", "The Dandelion",
	 "A gust blows the seeds off a dandelion clock one by one, windward side first, and they drift away on their little parachutes, glowing where the low sun shines through.",
	 dandelion::kDuration, dandelion::kFps, &dandelion::sceneAt, &dandelion::headerAt, &dandelion::shotStarts, 7.5,
	 {15, "translucent,backlight,particles,dof,dusk", "Close beside the seed head|Following the seeds|Wide pull back", dandelion::kSamples, 6.2}},
	{"candle", "The Candle",
	 "One candle burns down in front of out-of-focus fairy lights: the flame flickers, light glows through the wax, drips run down its side, and at the end the flame dies in a thread of smoke.",
	 candle::kDuration, candle::kFps, &candle::sceneAt, &candle::headerAt, &candle::shotStarts, 5.0,
	 {16, "subsurface,emissive,dof,night", "Portrait of the flame|Macro on the wax and drips|Pull back as it dies", candle::kSamples, 4.7}},
	{"meet-pip", "Meet Pip",
	 "Pip, a small round blue creature with two antennae, says hello, shows what its face can do (surprise, sadness, a scowl, a hop of joy), follows a glowing orb with its eyes and walks off across the stage.",
	 meet_pip::kDuration, meet_pip::kFps, &meet_pip::sceneAt, &meet_pip::headerAt, &meet_pip::shotStarts, 6.0,
	 {17, "character,emissive,dof", "Hello and the faces|Following the orb|Side-on walk", meet_pip::kSamples, 2.8}},
	{"pip-and-the-moon", "Pip and the Moon",
	 "Pip finds the moon in a pond, tries to touch it and breaks it, scoops it up in a bucket and dances with it, trips on the way home and loses it, and is shown the real one by a firefly. With music and sounds.",
	 pip_moon::kDuration, pip_moon::kFps, &pip_moon::sceneAt, &pip_moon::headerAt, &pip_moon::shotStarts, 18.0,
	 {18, "character,emissive,water,reflections,night", "Moon and reflection|Stroll along the dock|The moon in the water|Sorrow and the idea|Running for the bucket|Scooping the moon|The dance|Carrying it home|The fall|Alone at the water|The firefly|Under the real moon|Pull back over the pond", pip_moon::kSamples, 5.2}, &pip_moon::soundtrack},
};
inline constexpr int kFilmCount = static_cast<int>(sizeof(kFilms) / sizeof(kFilms[0]));

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// All the films: the ones above, and the ones made from files (film_file.h)
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// Where film files (*.film) are looked for: the folders scenes are (RAY_TRACER_PBRT_DIR, pbrt_scenes beside the program or up from it, the per-user scenes folder).
inline std::vector<std::string> filmFileDirs() {
	std::vector<std::string> dirs;
	if (const char *env = std::getenv("RAY_TRACER_PBRT_DIR")) {
		if (*env) dirs.emplace_back(env);
	}
	for (const char *d : {"pbrt_scenes", "../pbrt_scenes", "../../pbrt_scenes"}) dirs.emplace_back(d);
	if (const std::string user = pbrt_asset_check::userSceneDir(); !user.empty()) dirs.push_back(user);
	return dirs;
}

namespace detail {

struct Registry {
	std::deque<film_file::DataFilm> data;
	std::deque<Film> made;
	std::deque<std::string> strings;
	std::vector<const Film *> all;
	std::vector<std::string> problems;
	const char *keep(std::string s) {
		strings.push_back(std::move(s));
		return strings.back().c_str();
	}
};

// Loads one film file into the registry (or says why not).
inline void addFilmFile(Registry &r, const std::string &file) {
	r.data.emplace_back();
	film_file::DataFilm &d = r.data.back();
	std::string problem = d.load(file);
	if (problem.empty()) {
		for (const Film *f : r.all)
			if (d.spec.key == f->key) problem = file + ": the key \"" + d.spec.key + "\" is already a film's (" + f->name + ")";
	}
	if (!problem.empty()) {
		r.data.pop_back();
		r.problems.push_back(problem);
		return;
	}
	// the tags that are in the vocabulary
	std::string tags;
	{
		std::string word;
		for (std::size_t i = 0; i <= d.spec.tags.size(); ++i) {
			const char c = i < d.spec.tags.size() ? d.spec.tags[i] : ',';
			if (c == ',' || c == ' ') {
				if (!word.empty()) {
					for (const char *v : kTagVocabulary)
						if (word == v) tags += (tags.empty() ? "" : ",") + word;
					word.clear();
				}
			} else {
				word += c;
			}
		}
	}
	// a stable number from the key (fixed once given, whatever other films there are): 1000 + a hash, moved on past any that is taken
	std::uint32_t h = 2166136261u;
	for (char c : d.spec.key) h = (h ^ static_cast<unsigned char>(c)) * 16777619u;
	int id = 1000 + static_cast<int>(h % 9000u);
	for (bool taken = true; taken;) {
		taken = false;
		for (const Film *f : r.all) taken = taken || f->meta.id == id;
		if (taken) ++id;
	}
	const film_file::DataFilm *p = &d;
	Film f{r.keep(d.spec.key), r.keep(d.spec.name), r.keep(d.spec.about.empty() ? "A film made from " + std::filesystem::path(d.spec.scene).filename().string() + "." : d.spec.about),
		   d.spec.length, d.spec.fps, [p](double t) { return p->sceneAt(t); }, [p](double t) { return p->headerAt(t); }, [p]() { return p->shotStarts(); }, d.spec.still,
		   {id, r.keep(tags), r.keep(d.shotNames()), d.scene.samples, 0.0}};
	r.made.push_back(std::move(f));
	r.all.push_back(&r.made.back());
}

inline Registry &registry() {
	static Registry *r = [] {
		Registry *reg = new Registry();   // (lives as long as the program: films hand out pointers into it)
		for (const Film &f : kFilms) reg->all.push_back(&f);
		std::vector<std::string> files;
		for (const std::string &dir : filmFileDirs()) {
			std::error_code ec;
			for (const auto &entry : std::filesystem::directory_iterator(dir, ec)) {
				if (ec) break;
				if (entry.path().extension() != ".film") continue;
				const std::string canonical = std::filesystem::weakly_canonical(entry.path(), ec).string();
				if (std::find(files.begin(), files.end(), canonical) == files.end()) files.push_back(canonical);
			}
		}
		std::sort(files.begin(), files.end());
		for (const std::string &file : files) addFilmFile(*reg, file);
		return reg;
	}();
	return *r;
}

}  // namespace detail

// Every film: the built-in ones in the table's order, then those made from film files (in the order of their file names).
inline const std::vector<const Film *> &films() { return detail::registry().all; }
// What was wrong with the film files that were left out ("file:line: what"), one entry each.
inline const std::vector<std::string> &filmFileProblems() { return detail::registry().problems; }

inline const Film *find(const char *key) {
	if (!key) return nullptr;
	for (const Film *f : films())
		if (std::strcmp(f->key, key) == 0) return f;
	return nullptr;
}

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
inline const Film *forCameraPath(const char *cameraPath) {
	const std::size_t n = std::strlen(kCameraPathPrefix);
	if (!cameraPath || std::strncmp(cameraPath, kCameraPathPrefix, n) != 0) return nullptr;
	return find(cameraPath + n);
}
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
inline std::string markdownTable() {
	std::string out = "| Key | Name | Length | Shots | Tags | What happens |\n|-----|------|--------|-------|------|--------------|\n";
	for (const Film &f : kFilms) out += markdownRowOf(f) + "\n";
	return out;
}

// One frame of a film as a still scene: "cartoon:<key>" (the film's stillTime) or "cartoon:<key>@<seconds>". Used where a scene is named, `ray_tracer ... cartoon:fireflies@13 `.
struct Still {
	const Film *film = nullptr;
	double t = 0.0;
};
// Parses a still's name. Returns false when `spec` is not one (`error` stays empty) or is a bad one (`error` says why).
inline bool parseStill(const std::string &spec, Still &out, std::string &error) {
	error.clear();
	const std::size_t n = std::strlen(kCameraPathPrefix);
	if (spec.compare(0, n, kCameraPathPrefix) != 0) return false;
	const std::size_t at = spec.find('@', n);
	const std::string key = spec.substr(n, at == std::string::npos ? std::string::npos : at - n);
	const Film *film = find(key.c_str());
	if (!film) {
		error = "Unknown cartoon \"" + key + "\" - valid cartoons:";
		for (const Film *f : films()) error += " " + cameraPathOf(*f);
		return false;
	}
	double t = film->stillTime;
	if (at != std::string::npos) {
		const std::string text = spec.substr(at + 1);
		std::size_t used = 0;
		try {
			t = std::stod(text, &used);
		} catch (const std::exception &) {
			used = 0;
		}
		if (used == 0 || used != text.size() || !(t >= 0.0 && t <= film->duration)) {
			error = "\"" + spec + "\": the time after @ must be a number of seconds from 0 to " + lengthText(*film).substr(0, lengthText(*film).size() - 2) + " (the length of " + film->name + ")";
			return false;
		}
	}
	out.film = film;
	out.t = t;
	return true;
}

}  // namespace cartoon
