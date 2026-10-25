#pragma once
// film_kit.h -- the skeleton of a film told in shots, so that a new film is its story, its sets and its shots, not the plumbing again. (The rest of the kit: kit_people.h for
// people and animals, film_sky.h for painted skies, film_cache.h for heavy meshes written once, cartoon_lib.h's Caption for the words on the screen.)
//
// A film made with it:
//
//     struct Frame : kit::Moment { ... what this film's shots also set: which sky, the sun ... };
//     inline void shotHarbour(double t, Frame &f, bool content) {      // one function a shot: the camera, the light, the look at time t; the scene only when `content`
//         f.view = kit::cam({0, 2, 10}, {0, 1, 0}, 40.0);
//         f.iso = 200.0;
//         if (content) f.content = harbourText(t);
//     }
//     inline const std::vector<kit::Shot<Frame>> &shotList() { static const std::vector<kit::Shot<Frame>> s = {{0.0, "The harbour", shotHarbour}, ...}; return s; }
//     inline std::string sceneAt(double t) { Frame f = kit::momentAt(shotList(), t, true); f.light = ...; return kit::sceneText(f, kFormat); }
//     inline std::string headerAt(double t) { return kit::headerText(kit::momentAt(shotList(), t, false), kFormat); }
//
// and shotStarts() / the shot names for the film table come from the same list (kit::shotNamesOf). A moment whose iso is 1 or less is a black frame (a cut to black, a title
// card): sceneText makes it a near-black scene at an ordinary exposure, which renders in a second - an exposure of almost nothing took the renderer minutes a frame.

#include <algorithm>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "cartoon_lib.h"
#include "nature_lib.h"

namespace cartoon {
namespace kit {

// A moment of a shot: the camera, the light, the look, and (built only for the scene) everything in it.
struct Moment {
	CameraView view{{0.0, 0.0, 0.0}, {0.0, 0.0, -1.0}, 40.0, 0.0, 0};
	double iso = 150.0;   // 1 or less: a black frame
	double bloom = 0.16, vignette = 0.32, warmth = 0.0, saturation = 1.0;   // the camera's look (lens_look.h)
	bool mist = false;   // a haze round the camera (nature::Mist: it ends at its sphere, so a sun outside lights it)
	nature::Mist haze;
	int depth = 6;                     // the Integrator's maxdepth
	double maxComponentValue = 40.0;   // the Film's firefly clamp
	std::string light;     // the LightSource lines (the sky, the sun, ...): the film's own
	std::string content;   // everything in the scene
	std::vector<Vec3> subjects;   // what the shot is about (story space): set by a shot even without its content, for the film doctor's framing checks (Film::subjectsAt)
	std::vector<Foot> feet;       // its characters' feet, likewise (Film::feetAt: the sliding-feet check)
};

// How a film's frames are made: samples per pixel and the picture's size.
struct FilmFormat {
	int samples, width, height;
};

// A shot: where it starts, its name (the film table and the GUI's Shot box), and what it sets for a moment of it.
template <class M>
struct Shot {
	double start;
	const char *name;
	std::function<void(double, M &, bool)> fill;   // (film time, the moment, whether to build the content)
};

inline CameraView cam(Vec3 eye, Vec3 target, double fov, double lens = 0.0) { return CameraView{eye, target, fov, lens, 0}; }
inline double ease(double t0, double t1, double t) { return smoothstep(t0, t1, t); }
inline Vec3 glide(double t0, double t1, double t, Vec3 a, Vec3 b) { return lerp(a, b, ease(t0, t1, t)); }
inline Vec3 glideLinear(double t0, double t1, double t, Vec3 a, Vec3 b) { return lerp(a, b, clamp01((t - t0) / (t1 - t0))); }

// The shot names joined with '|', as the film table's meta record wants them.
template <class M>
inline std::string shotNamesOf(const std::vector<Shot<M>> &shots) {
	std::string s;
	for (const Shot<M> &d : shots) s += (s.empty() ? "" : "|") + std::string(d.name);
	return s;
}

// The moment at film time t: the shot it falls in fills it (the content only when asked for), and the camera is told which shot it is.
template <class M>
inline M momentAt(const std::vector<Shot<M>> &shots, double t, bool content) {
	std::size_t k = 0;
	for (std::size_t i = 0; i < shots.size(); ++i)
		if (t >= shots[i].start) k = i;
	M f;
	shots[k].fill(t, f, content);
	f.view.shot = static_cast<int>(k);
	return f;
}

// The header of a moment's scene: the look line, the camera's haze (when it has one), the camera, the film at the moment's exposure and clamp, the integrator at its depth.
inline std::string header(const Moment &f, const FilmFormat &format) {
	char look[220];
	std::snprintf(look, sizeof look, "# @rt-look bloom %.3f vignette %.3f warmth %.3f saturation %.3f\n", f.bloom, f.vignette, f.warmth, f.saturation);
	std::string h = sceneHeader(cameraText(f.view), format.samples, format.width, format.height);
	char film[160];
	std::snprintf(film, sizeof film, "Film \"rgb\" \"float iso\" [ %.6g ] \"float maxcomponentvalue\" [ %.6g ] ", (std::max)(1e-6, f.iso), f.maxComponentValue);
	const std::size_t at = h.find("Film \"rgb\" ");
	if (at != std::string::npos) h.replace(at, 11, film);
	const std::string depth = "\"integer maxdepth\" [ " + std::to_string(f.depth) + " ]";
	const std::size_t d = h.find("\"integer maxdepth\" [ 8 ]");
	if (d != std::string::npos) h.replace(d, 24, depth);
	return std::string(look) + (f.mist ? nature::mistBeforeCamera(f.haze) : std::string()) + h;
}

// A black frame (a cut to black, a title card) made renderable: a near-black scene at an ordinary exposure, with one small black ball in front of the camera (a renderer will not
// render an empty scene).
inline void makeBlack(Moment &f) {
	f.iso = 100.0;
	f.mist = false;
	f.bloom = 0.0;
	f.light = "LightSource \"infinite\" \"rgb L\" [ 0.00001 0.00001 0.00001 ]\n";
	f.content = attr(diffuseMaterial({0.0, 0.0, 0.0}), emitMesh(uvSphere(0.1, 8, 4), translate(f.view.eye + Vec3{0.0, 0.0, 5.0})));
}

// A moment's header alone (what a film's headerAt returns): a black frame as sceneText makes it.
inline std::string headerText(Moment f, const FilmFormat &format) {
	if (f.iso <= 1.0) {
		f.iso = 100.0;
		f.mist = false;
		f.bloom = 0.0;
	}
	return header(f, format);
}

// A moment's whole scene: the header, the haze's boundary, the light and the content (a black frame made renderable first).
inline std::string sceneText(Moment f, const FilmFormat &format) {
	if (f.iso <= 1.0) makeBlack(f);
	std::string out = header(f, format);
	if (f.mist) out += nature::mistBoundary(f.haze);
	return out + f.light + f.content;
}

}  // namespace kit
}  // namespace cartoon
