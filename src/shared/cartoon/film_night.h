#pragma once
// film_night.h -- night shots and the long lens (round 13):
//
//   night [eyeshine E] [flash F]
//            eyeshine: the animals' eyes shine back at the camera (film_animals.h eyeShineOf: green-gold, yellow, blue-green, orange, a rabbit's red), E times a
//            strong shine (1); flash: a light on the camera, F times a camera trap's flash (1: lights an animal 5 m off as a flash does)
//   cameratrap [ir] [name WORDS...] [clock HH:MM] [date YYYY-MM-DD] [temp C]
//            the picture a trail camera takes: its flash (unless the film's `night` sets one), the strip along its foot (the camera's name, the date, the clock
//            running on from `clock` at the film's start, the temperature), a darker edge; `ir`: its infrared night look - grey with a green cast, no colour
//   shimmer A
//            heat shimmer: the picture wavers as hot air rises off the ground (lens_look.h Look::shimmer: A a share of the picture's height, 0.002 to 0.006)
//
// The strip is a card in front of the lens (film_explain.h's: it glows and lights nothing); with depth of field it is soft, as a trail camera has none.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "cartoon_lib.h"
#include "film_explain.h"

namespace cartoon {
namespace night {

struct Night {
	double eyeshine = 0.0, flash = 0.0;
	bool trap = false, ir = false;
	std::string name = "CAM 03", date = "2026-10-30";
	int clockMinutes = 2 * 60 + 14;   // (02:14)
	double temp = -2.0;
	double shimmer = 0.0;
	bool any() const { return eyeshine > 0.0 || flash > 0.0 || trap || shimmer > 0.0; }
	double flashNow() const { return flash > 0.0 ? flash : trap ? 1.0 : 0.0; }
};

// The camera's flash: a point light just over the lens (pbrt's coordinates).
inline std::string flashText(const Night &n, Vec3 eye, Vec3 target) {
	const double f = n.flashNow();
	if (f <= 0.0) return std::string();
	const Vec3 up{0.0, 1.0, 0.0}, at = eye + up * 0.08 + normalize(target - eye + Vec3{0, 0, 1e-12}) * 0.02;
	const Vec3 I = n.ir ? Vec3{60.0, 60.0, 60.0} * f : Vec3{70.0, 66.0, 60.0} * f;   // (a white flash; an infrared one, seen through the camera's filter, grey)
	char buf[200];
	std::snprintf(buf, sizeof buf, "AttributeBegin\n  LightSource \"point\" \"point3 from\" [ %.5f %.5f %.5f ] \"rgb I\" [ %.3f %.3f %.3f ]\nAttributeEnd\n", at.x, at.y, at.z, I.x, I.y, I.z);
	return std::string("# a camera's flash (film_night.h)\n") + buf;
}
// The strip a trail camera stamps along the foot of its pictures, at film time t.
inline std::string stampWords(const Night &n, double t) {
	const int seconds = static_cast<int>(std::floor(n.clockMinutes * 60.0 + t)) % 86400;
	char buf[160];
	std::snprintf(buf, sizeof buf, "%s     %s   %02d:%02d:%02d     %+.0f C", n.name.c_str(), n.date.c_str(), seconds / 3600, seconds / 60 % 60, seconds % 60, n.temp);
	return buf;
}
inline std::string stampText(const Night &n, double t, const explain::View &v, const std::string &film) {
	if (!n.trap) return std::string();
	double aspect = 1.0;
	const std::string pic = explain::cardPicture(film, stampWords(n, t), n.ir ? Vec3{0.6, 0.65, 0.6} : Vec3{0.9, 0.9, 0.9}, aspect);
	if (pic.empty()) return std::string();
	const double d = 0.25;   // (a quarter of a metre in front of the lens)
	const Vec3 c0 = v.eye + v.forward() * d;
	const double H = v.frameHeightAt(c0), W = H * 16.0 / 9.0, h = std::min(0.07 * H, 0.94 * W / aspect);
	const Vec3 c = c0 - v.up() * (0.5 * H - 0.5 * h - 0.025 * H);
	return "# a trail camera's strip (film_night.h)\n" + explain::detail::card(pic, aspect, c, h, v, n.ir ? 0.8 : 0.9);
}
// The words a frame's look line gets for the night's look and the shimmer (lens_look.h; later words win), or "".
inline std::string lookWords(const Night &n, double t) {
	std::string out;
	char buf[160];
	if (n.trap) out += n.ir ? " saturation 0 tint 0.92 1.06 0.9 vignette 0.5 bloom 0.08" : " saturation 0.8 vignette 0.4";
	if (n.shimmer > 0.0) {
		std::snprintf(buf, sizeof buf, " shimmer %.5f shimmer-time %.4f", n.shimmer, t);
		out += buf;
	}
	return out;
}
// `words` added to the scene's look line (one made when it has none); the scene as it was for none.
inline std::string withWords(std::string scene, const std::string &words) {
	if (words.empty()) return scene;
	const std::size_t look = scene.find("# @rt-look");
	if (look == std::string::npos) return "# @rt-look" + words + "\n" + scene;
	std::size_t end = scene.find('\n', look);
	if (end == std::string::npos) end = scene.size();
	if (end > look && scene[end - 1] == '\r') --end;
	return scene.insert(end, words);
}
inline std::string withLook(std::string scene, const Night &n, double t) { return withWords(std::move(scene), lookWords(n, t)); }

}  // namespace night
}  // namespace cartoon
