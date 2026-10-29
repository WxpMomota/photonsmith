#pragma once
// shot_cameras.h -- changes to a film's shot cameras, written in a text file that is read when a frame is made: a framing tweak needs no rebuild. The file is
// pbrt_scenes/film_assets/<film key>/cameras.txt (film_cache::asset); a film without one is left as its code makes it. Every film gets it (the film registry in cartoons.h
// wraps each film's sceneAt and headerAt), so its video, its stills, the GUI's renders of it and the film doctor all see the same cameras.
//
//     # The Last Nomai Signal: what the code's cameras are changed by. A section a shot - its number (1 is the first, as the doctor's sheet counts) or its name - and [*]
//     # for every shot.
//     [28]
//     dolly 1.5        # forward along the view, in the film's units (negative: back); the aim stays where it was
//     truck -0.4       # to the right (negative: left), aim and all
//     pedestal 0.3     # up (negative: down), aim and all
//     pan 4            # turn the aim to the right, degrees
//     tilt -6          # turn the aim up (negative: down), degrees
//     fov 50           # the field of view in degrees (pbrt's: across the shorter side of the picture) ...
//     zoom 1.2         # ... or the code's divided by this
//     iso 300          # the exposure (the Film's iso) ...
//     stops 0.5        # ... or the code's times 2 to this
//     samples 0.5      # the shot renders at half the video's samples a pixel (a quiet, dark or still shot needs fewer; `python scripts/anim/film.py budget` measures it)
//     lift 0.01 0 0.02 # the shot's grade (lens_look.h): light added (lifting the shadows), one number or red green blue ...
//     gamma 1.1        # ... the middle brightened (above 1) or darkened ...
//     gain 1.05        # ... multiplied ...
//     tint 1 0.95 1.1  # ... and a colour cast that keeps the brightness
//     look star 0.3 star-points 6   # more words for the frame's look line (lens_look.h: a star glint on the brightest lights, bloom, flare ...)
//     tonemap agx      # the curve from light to colours (tone_map.h): agx (Blender's: a bright crystal or neon keeps its hue), neutral (Khronos: colours as painted), aces
//     lighting key 1.5 fill 0.5 rim 1 side right   # key, fill and rim lights round what the camera looks at (shot_lighting.h; `lighting` alone: the usual three)
//     [The answer]
//     tilt 3
//
// A change is made to the camera the code gives at each moment, so a camera that moves keeps moving: dolly 1.5 brings the whole move 1.5 nearer. [*] comes first and the shot's
// own section on top of it (moves add up, zooms multiply, a set fov or iso is the shot's own). What the file says wrong (a word it does not know, a number that is not one) is
// printed once to the error output and the line left out.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include "cartoon_lib.h"
#include "film_cache.h"
#include "shot_lighting.h"
#include "../lens_look.h"   // a `look` line's words

namespace cartoon {
namespace shot_cameras {

// What one section says.
struct Change {
	double dolly = 0.0, truck = 0.0, pedestal = 0.0, pan = 0.0, tilt = 0.0;
	double fov = 0.0;    // 0: the code's
	double zoom = 1.0;
	double iso = 0.0;    // 0: the code's
	double stops = 0.0;
	double samples = 1.0;   // the share of the video's samples a pixel
	double lift[3] = {0.0, 0.0, 0.0}, gamma[3] = {1.0, 1.0, 1.0}, gain[3] = {1.0, 1.0, 1.0}, tint[3] = {1.0, 1.0, 1.0};   // the grade (lens_look::Look)
	shot_lighting::Lighting lighting;   // key, fill and rim round what the camera looks at
	std::string tonemap;                // the colour curve (tone_map.h: agx, neutral, aces ...); empty: the render's
	std::string look;                   // `look WORDS`: more of the look line's own words (lens_look.h: star 0.3 star-points 6, bloom, flare ...), as written
	bool graded() const {
		for (int k = 0; k < 3; ++k)
			if (lift[k] != 0.0 || gamma[k] != 1.0 || gain[k] != 1.0 || tint[k] != 1.0) return true;
		return false;
	}
	bool moves() const { return dolly != 0.0 || truck != 0.0 || pedestal != 0.0 || pan != 0.0 || tilt != 0.0; }
	bool any() const { return moves() || fov > 0.0 || zoom != 1.0 || iso > 0.0 || stops != 0.0 || samples != 1.0 || graded() || lighting.on || !tonemap.empty() || !look.empty(); }
};

// A section on top of another: moves add up, zooms multiply, a set fov or iso replaces.
inline Change combine(const Change &under, const Change &over) {
	Change c = under;
	c.dolly += over.dolly;
	c.truck += over.truck;
	c.pedestal += over.pedestal;
	c.pan += over.pan;
	c.tilt += over.tilt;
	c.zoom *= over.zoom;
	c.stops += over.stops;
	c.samples *= over.samples;
	for (int k = 0; k < 3; ++k) {   // (a shot's grade on top of the film's: lifts add, the rest multiply)
		c.lift[k] += over.lift[k];
		c.gamma[k] *= over.gamma[k];
		c.gain[k] *= over.gain[k];
		c.tint[k] *= over.tint[k];
	}
	if (over.fov > 0.0) c.fov = over.fov;
	if (over.iso > 0.0) c.iso = over.iso;
	if (over.lighting.on) c.lighting = over.lighting;   // (a shot's own lighting, whole)
	if (!over.tonemap.empty()) c.tonemap = over.tonemap;
	if (!over.look.empty()) c.look = c.look.empty() ? over.look : c.look + " " + over.look;   // (the shot's words after the film's: they win)
	return c;
}

// A whole file.
struct File {
	Change all;                              // [*]
	std::map<int, Change> byNumber;          // [28]: the 28th shot
	std::map<std::string, Change> byName;    // [The answer]
	std::vector<std::string> problems;       // "line 4: ..."
	bool empty() const { return !all.any() && byNumber.empty() && byName.empty(); }
};

inline std::string trimmed(const std::string &s) {
	const std::size_t a = s.find_first_not_of(" \t\r\n");
	if (a == std::string::npos) return std::string();
	const std::size_t b = s.find_last_not_of(" \t\r\n");
	return s.substr(a, b - a + 1);
}

inline File parse(const std::string &text) {
	File f;
	std::istringstream in(text);
	std::string line;
	Change *at = nullptr;
	for (int n = 1; std::getline(in, line); ++n) {
		const std::size_t hash = line.find('#');
		line = trimmed(hash == std::string::npos ? line : line.substr(0, hash));
		if (line.empty()) continue;
		const std::string where = "line " + std::to_string(n) + ": ";
		if (line.front() == '[') {
			const std::size_t close = line.find(']');
			const std::string name = trimmed(line.substr(1, close == std::string::npos ? std::string::npos : close - 1));
			if (close == std::string::npos || name.empty()) {
				f.problems.push_back(where + "a section is [shot number], [shot name] or [*]");
				at = nullptr;
			} else if (name == "*") {
				at = &f.all;
			} else if (name.find_first_not_of("0123456789") == std::string::npos) {
				at = &f.byNumber[std::atoi(name.c_str())];
			} else {
				at = &f.byName[name];
			}
			continue;
		}
		std::istringstream words(line);
		std::string word;
		double value = 0.0;
		words >> word;
		if (word == "lighting") {   // the rest of the line: shot_lighting::parse
			if (!at) {
				f.problems.push_back(where + "'lighting' before any [shot] section");
				continue;
			}
			std::string rest;
			std::getline(words, rest);
			if (const std::string e = shot_lighting::parse(rest, at->lighting); !e.empty()) {
				at->lighting = shot_lighting::Lighting();
				f.problems.push_back(where + e);
			}
			continue;
		}
		if (word == "look") {   // the look line's own words (lens_look.h), checked here so that a mistake is said once, with its line
			std::string rest;
			std::getline(words, rest);
			if (!at) {
				f.problems.push_back(where + "'look' before any [shot] section");
				continue;
			}
			lens_look::Look scratch;
			if (const std::string e = lens_look::parse(rest, scratch); !e.empty()) {
				f.problems.push_back(where + "look: " + e);
				continue;
			}
			at->look = trimmed(rest);
			continue;
		}
		if (word == "tonemap") {   // the film's (or the shot's) colour curve: tone_map.h
			std::string name;
			words >> name;
			if (!at) {
				f.problems.push_back(where + "'tonemap' before any [shot] section");
				continue;
			}
			if (name != "aces" && name != "agx" && name != "agx-punchy" && name != "neutral" && name != "reinhard" && name != "none") {
				f.problems.push_back(where + "'tonemap' takes aces, agx, agx-punchy, neutral, reinhard or none");
				continue;
			}
			at->tonemap = name;
			continue;
		}
		if (word == "lift" || word == "gamma" || word == "gain" || word == "tint") {   // one number or three (red, green, blue)
			std::vector<double> v;
			for (double x; words >> x;) v.push_back(x);
			if (!at) {
				f.problems.push_back(where + "'" + word + "' before any [shot] section");
				continue;
			}
			if (v.size() != 1 && v.size() != 3) {
				f.problems.push_back(where + "'" + word + "' takes one number or three (red, green, blue)");
				continue;
			}
			double *dst = word == "lift" ? at->lift : word == "gamma" ? at->gamma : word == "gain" ? at->gain : at->tint;
			for (int k = 0; k < 3; ++k) dst[k] = v[v.size() == 1 ? 0 : static_cast<std::size_t>(k)];
			continue;
		}
		if (!(words >> value)) {
			f.problems.push_back(where + "'" + word + "' needs a number");
			continue;
		}
		if (!at) {
			f.problems.push_back(where + "'" + word + "' before any [shot] section");
			continue;
		}
		if (word == "dolly") at->dolly = value;
		else if (word == "truck") at->truck = value;
		else if (word == "pedestal") at->pedestal = value;
		else if (word == "pan") at->pan = value;
		else if (word == "tilt") at->tilt = value;
		else if (word == "fov" && value > 0.0 && value < 180.0) at->fov = value;
		else if (word == "zoom" && value > 0.0) at->zoom = value;
		else if (word == "iso" && value > 0.0) at->iso = value;
		else if (word == "stops") at->stops = value;
		else if (word == "samples" && value > 0.0 && value <= 8.0) at->samples = value;
		else f.problems.push_back(where + "'" + word + " " + trimmed(line.substr(word.size())) + "' is not a change this file knows (dolly, truck, pedestal, pan, tilt, fov, zoom, iso, stops, samples 0-8, lift, gamma, gain, tint, tonemap, look, lighting)");
	}
	return f;
}

// The change for a shot (its index from 0 and its name): [*], then [number], then [name].
inline Change changeFor(const File &f, int shot, const std::string &name) {
	Change c = f.all;
	const auto byNumber = f.byNumber.find(shot + 1);
	if (byNumber != f.byNumber.end()) c = combine(c, byNumber->second);
	const auto byName = f.byName.find(name);
	if (!name.empty() && byName != f.byName.end()) c = combine(c, byName->second);
	return c;
}

namespace detail {
inline bool numbersAt(const std::string &s, std::size_t pos, int count, std::vector<double> &out, std::size_t &end) {
	out.clear();
	const char *p = s.c_str() + pos;
	for (int i = 0; i < count; ++i) {
		char *next = nullptr;
		const double v = std::strtod(p, &next);
		if (next == p) return false;
		out.push_back(v);
		p = next;
	}
	end = static_cast<std::size_t>(p - s.c_str());
	return true;
}
inline std::string num(double v) {
	char b[40];
	std::snprintf(b, sizeof b, "%.6g", v);
	return b;
}
// The value of a `"float <name>" [ v ]` parameter in [from, to) of s: its place (the number's start and end) or false.
inline bool floatParam(const std::string &s, std::size_t from, std::size_t to, const std::string &name, std::size_t &numStart, std::size_t &numEnd, double &value) {
	const std::size_t at = s.find("\"float " + name + "\"", from);
	if (at == std::string::npos || at >= to) return false;
	const std::size_t open = s.find('[', at);
	if (open == std::string::npos || open >= to) return false;
	numStart = s.find_first_not_of(" \t", open + 1);
	std::vector<double> v;
	if (numStart == std::string::npos || !numbersAt(s, numStart, 1, v, numEnd)) return false;
	value = v[0];
	return true;
}
inline Vec3 turned(Vec3 v, Vec3 axis, double degrees) {   // v turned about the unit axis (Rodrigues)
	const double a = degrees * kPi / 180.0, c = std::cos(a), s = std::sin(a);
	return v * c + cross(axis, v) * s + axis * (dot(axis, v) * (1.0 - c));
}
}  // namespace detail

// The header line that tells a video to render a frame at a share of its samples a pixel (sampleShareOf reads it back). In the scene's text, so that a frame whose share changes
// has a new fingerprint and a resumed video renders it again.
inline std::string samplesLine(double share) {
	char b[64];
	std::snprintf(b, sizeof b, "# @rt-samples %.4g\n", share);
	return b;
}
// The share a frame's text asks for (1 when it has no such line).
inline double sampleShareOf(const std::string &text) {
	const std::size_t worldBegin = text.find("WorldBegin");
	const std::size_t at = text.find("# @rt-samples ");
	if (at == std::string::npos || (worldBegin != std::string::npos && at > worldBegin)) return 1.0;
	const double v = std::atof(text.c_str() + at + 14);
	return v > 0.0 ? v : 1.0;
}
// The samples a pixel of a frame of a video made at `spp`: its share of them, never fewer than one.
inline int samplesFor(int spp, double share) { return std::max(1, static_cast<int>(std::lround(spp * share))); }

// A frame's scene (or header) with the change made to it: its LookAt moved and turned, the Camera's fov and focus distance, the Film's iso. Only the header (before WorldBegin)
// is touched; a scene without a LookAt is returned as it is.
inline std::string apply(const std::string &scene, const Change &c) {
	if (!c.any()) return scene;
	const std::size_t worldBegin = scene.find("WorldBegin");
	const std::size_t headerEnd = worldBegin == std::string::npos ? scene.size() : worldBegin;
	std::string head = scene.substr(0, headerEnd);
	if (c.moves()) {
		const std::size_t look = head.find("LookAt");
		std::vector<double> v;
		std::size_t end = 0;
		if (look == std::string::npos || !detail::numbersAt(head, look + 6, 9, v, end)) return scene;
		// (pbrt's LookAt has x negated against the films' story space: cameraText. The moves are made in story space, where +x is to the right of the picture.)
		Vec3 eye{-v[0], v[1], v[2]}, target{-v[3], v[4], v[5]};
		const Vec3 worldUp = normalize(Vec3{-v[6], v[7], v[8]});
		const double focus = length(target - eye);
		Vec3 forward = normalize(target - eye);
		Vec3 right = normalize(cross(forward, worldUp));
		eye = eye + right * c.truck + worldUp * c.pedestal;
		target = target + right * c.truck + worldUp * c.pedestal;
		Vec3 aim = target - eye;
		aim = detail::turned(aim, worldUp, -c.pan);   // (turning right about up is negative in a right-handed space)
		right = normalize(cross(normalize(aim), worldUp));
		aim = detail::turned(aim, right, c.tilt);
		forward = normalize(aim);
		eye = eye + forward * c.dolly;
		target = eye + forward * std::max(1e-3, length(aim) - c.dolly);
		const std::string lookAt = "LookAt " + detail::num(-eye.x) + " " + detail::num(eye.y) + " " + detail::num(eye.z) + "  " + detail::num(-target.x) + " " + detail::num(target.y) + " " +
								   detail::num(target.z) + "  " + detail::num(v[6]) + " " + detail::num(v[7]) + " " + detail::num(v[8]);
		head.replace(look, end - look, lookAt);
		// the focus follows the subject the aim was on: as far as before, less the dolly
		const std::size_t camera = head.find("Camera");
		std::size_t a = 0, b = 0;
		double d = 0.0;
		if (camera != std::string::npos && detail::floatParam(head, camera, head.size(), "focaldistance", a, b, d))
			head.replace(a, b - a, detail::num(std::max(1e-3, focus - c.dolly)));
	}
	if (c.fov > 0.0 || c.zoom != 1.0) {
		const std::size_t camera = head.find("Camera");
		if (camera != std::string::npos) {
			const std::size_t lineEnd = std::min(head.find('\n', camera), head.size());
			std::size_t a = 0, b = 0;
			double fov = 90.0;   // (pbrt's default)
			const bool has = detail::floatParam(head, camera, lineEnd, "fov", a, b, fov);
			double now = c.fov > 0.0 ? c.fov : fov;
			now = 2.0 * std::atan(std::tan(now * kPi / 360.0) / c.zoom) * 180.0 / kPi;
			if (has) {
				head.replace(a, b - a, detail::num(now));
			} else {
				const std::size_t kind = head.find('"', head.find('"', camera) + 1);   // after "perspective"
				if (kind != std::string::npos && kind < lineEnd) head.insert(kind + 1, " \"float fov\" [ " + detail::num(now) + " ]");
			}
		}
	}
	if (c.iso > 0.0 || c.stops != 0.0) {
		const std::size_t film = head.find("Film ");
		if (film != std::string::npos) {
			const std::size_t lineEnd = std::min(head.find('\n', film), head.size());
			std::size_t a = 0, b = 0;
			double iso = 100.0;   // (pbrt's default: an exposure of 1)
			const bool has = detail::floatParam(head, film, lineEnd, "iso", a, b, iso);
			const double now = (c.iso > 0.0 ? c.iso : iso) * std::pow(2.0, c.stops);
			if (has) {
				head.replace(a, b - a, detail::num(now));
			} else {
				const std::size_t kind = head.find('"', head.find('"', film) + 1);   // after "rgb"
				if (kind != std::string::npos && kind < lineEnd) head.insert(kind + 1, " \"float iso\" [ " + detail::num(now) + " ]");
			}
		}
	}
	if (c.samples != 1.0) head = samplesLine(c.samples) + head;
	if (c.graded() || !c.tonemap.empty() || !c.look.empty()) {   // the grade, the curve and the look's own words: added to the frame's look line (later words win: lens_look::parse), or a look line of its own
		char graded[320] = "";
		if (c.graded())
			std::snprintf(graded, sizeof graded, " lift %.4g %.4g %.4g gamma %.4g %.4g %.4g gain %.4g %.4g %.4g tint %.4g %.4g %.4g", c.lift[0], c.lift[1], c.lift[2], c.gamma[0],
						  c.gamma[1], c.gamma[2], c.gain[0], c.gain[1], c.gain[2], c.tint[0], c.tint[1], c.tint[2]);
		std::string g = graded;
		if (!c.tonemap.empty()) g += " tonemap " + c.tonemap;
		if (!c.look.empty()) g += " " + c.look;
		const std::size_t look = head.find("# @rt-look");
		if (look != std::string::npos) {
			std::size_t end = head.find('\n', look);
			if (end == std::string::npos) end = head.size();
			if (end > look && head[end - 1] == '\r') --end;
			head.insert(end, g);
		} else {
			head = "# @rt-look" + g + "\n" + head;
		}
	}
	if (c.lighting.on) {   // the shot's lighting round what the (changed) camera looks at
		const std::size_t look = head.find("LookAt");
		std::vector<double> v;
		std::size_t end = 0;
		if (look != std::string::npos && detail::numbersAt(head, look + 6, 9, v, end))
			return shot_lighting::withLights(head + scene.substr(headerEnd), shot_lighting::lightsText({-v[0], v[1], v[2]}, {-v[3], v[4], v[5]}, c.lighting));
	}
	return head + scene.substr(headerEnd);
}

// The file of a film (null when it has none), read again when it changes. Thread-safe; what it says wrong is printed once a reading.
inline std::shared_ptr<const File> forFilm(const std::string &key) {
	struct Seen {
		std::string path;
		bool looked = false;
		std::filesystem::file_time_type time{};
		std::uintmax_t size = 0;
		std::shared_ptr<const File> file;
	};
	static std::mutex lock;
	static std::map<std::string, Seen> seen;
	const std::lock_guard<std::mutex> hold(lock);
	Seen &s = seen[key];
	if (!s.looked) {
		s.path = film_cache::asset(key + "/cameras.txt");   // (looked for once: a file made later is seen by the next program)
		s.looked = true;
	}
	if (s.path.empty()) return nullptr;
	std::error_code ec;
	const auto time = std::filesystem::last_write_time(s.path, ec);
	const auto size = ec ? 0 : std::filesystem::file_size(s.path, ec);
	if (ec) return nullptr;
	if (!s.file || time != s.time || size != s.size) {
		std::ifstream in(s.path, std::ios::binary);
		std::stringstream text;
		text << in.rdbuf();
		auto file = std::make_shared<File>(parse(text.str()));
		for (const std::string &p : file->problems) std::cerr << "[cameras] " << s.path << ": " << p << std::endl;
		s.file = std::move(file);
		s.time = time;
		s.size = size;
	}
	return s.file;
}

// A film's frame text at time t with its cameras file's change for the shot t falls in (the text as it is when the film has no file).
// A frame's text with its seed in its look line when that look is an old film's (print_look.h `vintage`): its grain and dust change from frame to frame, whether the
// cameras file or the scene's own header asked for it (t in hundredths of a second: a new seed every frame). A look line that gives its own seed keeps it.
inline std::string withFrameSeed(std::string text, double t) {
	const std::size_t at = text.find("# @rt-look"), world = text.find("WorldBegin");
	if (at == std::string::npos || (world != std::string::npos && at > world)) return text;
	std::size_t end = text.find('\n', at);
	if (end == std::string::npos) end = text.size();
	if (end > at && text[end - 1] == '\r') --end;
	const std::string line = text.substr(at, end - at) + " ";
	if (line.find("vintage") == std::string::npos || line.find(" seed ") != std::string::npos) return text;
	return text.insert(end, " seed " + std::to_string(std::llround(std::max(0.0, t) * 100.0)));
}

// A frame's text with its look line's `lut FILE` made a full path (cube_lut.h): a film's own (beside its cameras file), else the library's (library/luts/), as given when
// neither has it. A frame's scene is written to a folder of its own, so a bare name would not be found where it is rendered.
inline std::string withLutPath(std::string text, const std::string &key) {
	const std::size_t at = text.find("# @rt-look"), world = text.find("WorldBegin");
	if (at == std::string::npos || (world != std::string::npos && at > world)) return text;
	const std::size_t end = std::min(text.find('\n', at), text.size()), word = text.find(" lut ", at);
	if (word == std::string::npos || word > end) return text;
	const std::size_t from = word + 5, to = std::min(text.find_first_of(" \r\n", from), end);
	const std::string name = text.substr(from, to - from);
	if (name.empty() || std::filesystem::path(name).is_absolute()) return text;
	for (const std::string &rel : {key + "/" + name, std::string("library/luts/") + name, std::string("library/") + name})
		if (const std::string found = film_cache::asset(rel); !found.empty()) return text.replace(from, to - from, std::filesystem::path(found).generic_string());
	return text;
}

inline std::string at(const std::string &key, const std::function<std::vector<double>()> &starts, const std::vector<std::string> &shotNames, double t, const std::string &text) {
	const std::shared_ptr<const File> file = forFilm(key);
	if (!file || file->empty()) return withLutPath(withFrameSeed(text, t), key);
	const std::vector<double> shotStarts = starts ? starts() : std::vector<double>();
	int shot = 0;
	for (std::size_t i = 0; i < shotStarts.size(); ++i)
		if (t >= shotStarts[i]) shot = static_cast<int>(i);
	const std::string name = shot < static_cast<int>(shotNames.size()) ? shotNames[static_cast<std::size_t>(shot)] : std::string();
	return withLutPath(withFrameSeed(shot_cameras::apply(text, changeFor(*file, shot, name)), t), key);
}

}  // namespace shot_cameras
}  // namespace cartoon
