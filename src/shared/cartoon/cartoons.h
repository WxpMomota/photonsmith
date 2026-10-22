#pragma once
// cartoons.h -- the cartoon films: each is a function of time that returns the pbrt scene for that instant (cartoon_lib.h). Listing a film in kFilms is all it takes to make it a video preset
// (video_preset.h builds one preset per film: name, description, length and fps from here; append, never reorder). A cartoon is
// chosen with `--camera-path cartoon:<key>` (or `--video-preset <key>`), and the launcher then renders every frame from its own scene instead of moving a camera round a fixed one.

#include <cstdio>
#include <cstring>
#include <exception>
#include <string>

#include "ball_parade.h"
#include "domino_run.h"
#include "fireflies.h"
#include "lamp_and_ball.h"
#include "little_robot.h"
#include "newtons_cradle.h"
#include "paper_boat.h"

namespace cartoon {

constexpr const char *kCameraPathPrefix = "cartoon:";
constexpr const char *kStillSlugPrefix = "cartoon-";   // the scene a film's still is listed as: "cartoon-fireflies" (and its file, cartoon-fireflies.pbrt)

inline const Film kFilms[] = {
	{"lamp-and-ball", "Lamp and Ball",
	 "A small yellow desk lamp wakes up in the dark, notices a red ball, hops over, nudges it, watches it bounce off the books and roll home, and tucks its shade over it.",
	 lamp_and_ball::kDuration, lamp_and_ball::kFps, &lamp_and_ball::sceneAt, &lamp_and_ball::headerAt, 9.1},
	{"ball-parade", "Ball Parade",
	 "Seven glossy balls drop in one after another, bounce in a travelling wave on a checker floor with squash and stretch, and settle one by one into a row.",
	 ball_parade::kDuration, ball_parade::kFps, &ball_parade::sceneAt, &ball_parade::headerAt, 4.6},
	{"little-robot", "The Little Robot",
	 "A small robot wakes in a junkyard at dusk, finds a seedling dying in a tin can, waters it, and watches it bloom into a glowing flower. Five shots, 32 seconds.",
	 little_robot::kDuration, little_robot::kFps, &little_robot::sceneAt, &little_robot::headerAt, 25.5},
	{"fireflies", "Fireflies",
	 "A night meadow lit by fireflies: a lone firefly wanders, a second arrives, their flashes drift into step, they circle and spiral up to the moon as the meadow wakes. Four shots, 26 seconds.",
	 fireflies::kDuration, fireflies::kFps, &fireflies::sceneAt, &fireflies::headerAt, 13.0},
	{"newtons-cradle", "Newton's Cradle",
	 "Five chrome balls on strings: one is lifted and let go, the far ball flies out and back, and the swing passes from side to side and slowly dies, with the room in the chrome. Three shots, 18 seconds.",
	 newtons_cradle::kDuration, newtons_cradle::kFps, &newtons_cradle::sceneAt, &newtons_cradle::headerAt, 5.0},
	{"domino-run", "Domino Run",
	 "A red marble taps the first of 130 rainbow dominoes laid in a spiral; the wave runs inwards, chased by the camera, and the last domino lights a small lamp in the middle. Four shots, 24 seconds.",
	 domino_run::kDuration, domino_run::kFps, &domino_run::sceneAt, &domino_run::headerAt, 9.0},
	{"paper-boat", "The Paper Boat",
	 "A folded paper boat sails across a pond in the rain at dusk: every drop starts rings of ripples that rock the boat and cross one another, the rain eases, and the boat sails on towards the low sun. Three shots, 24 seconds.",
	 paper_boat::kDuration, paper_boat::kFps, &paper_boat::sceneAt, &paper_boat::headerAt, 10.0},
};
inline constexpr int kFilmCount = static_cast<int>(sizeof(kFilms) / sizeof(kFilms[0]));

inline const Film *find(const char *key) {
	if (!key) return nullptr;
	for (const Film &f : kFilms)
		if (std::strcmp(f.key, key) == 0) return &f;
	return nullptr;
}

// The names one film goes by, written once. Everything that needs one of them calls these (the video presets, the launcher, the stills, the GUI) rather than joining strings itself.
//   key           "fireflies"            Film::key: how the table and the command line name it
//   camera path   "cartoon:fireflies"    --camera-path, the GUI's Camera Path entry, the video preset's path
//   still scene   "cartoon-fireflies"    the slug of its still in the scene list (the scene list may add a suffix when a scene of the user's has the name; the still's own
//                                        `# @rt-cartoon <key>` tag, not this, is what links a listed scene back to its film)
//   preset id     "V10"                  video_preset::all(): numbered in kFilms order after the camera presets
inline std::string cameraPathOf(const Film &film) { return std::string(kCameraPathPrefix) + film.key; }
inline std::string stillSlugOf(const Film &film) { return std::string(kStillSlugPrefix) + film.key; }

// The film a camera-path name asks for ("cartoon:lamp-and-ball"), or null when the name is an ordinary camera path.
inline const Film *forCameraPath(const char *cameraPath) {
	const std::size_t n = std::strlen(kCameraPathPrefix);
	if (!cameraPath || std::strncmp(cameraPath, kCameraPathPrefix, n) != 0) return nullptr;
	return find(cameraPath + n);
}
inline bool isCartoonPath(const char *cameraPath) { return cameraPath && std::strncmp(cameraPath, kCameraPathPrefix, std::strlen(kCameraPathPrefix)) == 0; }

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
		for (const Film &f : kFilms) error += " " + cameraPathOf(f);
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
			char length[32];
			std::snprintf(length, sizeof(length), "%g", film->duration);
			error = "\"" + spec + "\": the time after @ must be a number of seconds from 0 to " + length + " (the length of " + film->name + ")";
			return false;
		}
	}
	out.film = film;
	out.t = t;
	return true;
}

}  // namespace cartoon
