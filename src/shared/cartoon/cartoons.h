#pragma once
// cartoons.h -- the cartoon films the video presets (video_preset.h) can render: each is a function of time that returns the pbrt scene for that instant (cartoon_lib.h). A cartoon is
// chosen with `--camera-path cartoon:<key>` (or `--video-preset <key>`), and the launcher then renders every frame from its own scene instead of moving a camera round a fixed one.

#include <cstring>

#include "ball_parade.h"
#include "lamp_and_ball.h"

namespace cartoon {

constexpr const char *kCameraPathPrefix = "cartoon:";

inline const Film kFilms[] = {
	{"lamp-and-ball", "Lamp and Ball",
	 "A small yellow desk lamp wakes up in the dark, notices a red ball, hops over, nudges it, watches it bounce off the books and roll home, and tucks its shade over it.",
	 lamp_and_ball::kDuration, lamp_and_ball::kFps, &lamp_and_ball::sceneAt, 9.1},
	{"ball-parade", "Ball Parade",
	 "Seven glossy balls drop in one after another, bounce in a travelling wave on a checker floor with squash and stretch, and settle one by one into a row.",
	 ball_parade::kDuration, ball_parade::kFps, &ball_parade::sceneAt, 4.6},
};
inline constexpr int kFilmCount = static_cast<int>(sizeof(kFilms) / sizeof(kFilms[0]));

inline const Film *find(const char *key) {
	if (!key) return nullptr;
	for (const Film &f : kFilms)
		if (std::strcmp(f.key, key) == 0) return &f;
	return nullptr;
}

// The film a camera-path name asks for ("cartoon:lamp-and-ball"), or null when the name is an ordinary camera path.
inline const Film *forCameraPath(const char *cameraPath) {
	const std::size_t n = std::strlen(kCameraPathPrefix);
	if (!cameraPath || std::strncmp(cameraPath, kCameraPathPrefix, n) != 0) return nullptr;
	return find(cameraPath + n);
}
inline bool isCartoonPath(const char *cameraPath) { return cameraPath && std::strncmp(cameraPath, kCameraPathPrefix, std::strlen(kCameraPathPrefix)) == 0; }

}  // namespace cartoon
