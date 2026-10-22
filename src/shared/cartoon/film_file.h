#pragma once
// film_file.h -- films made from files: an ordinary pbrt scene animated by a small text file (name.film), with no C++ and no rebuild.
//
// A film file says what to do with a scene over time; the scene is a pbrt file of your own (a Scene Builder scene, an exported one, one you wrote). Parts of the scene that should
// move are marked in the pbrt file with a comment line, `# @anim NAME`, right before an AttributeBegin ... AttributeEnd block:
//
//     # @anim windmill
//     AttributeBegin
//       Translate 0 2 0
//       Shape "trianglemesh" ...
//     AttributeEnd
//
// and the film file moves, turns, scales, lights and shows those blocks, and puts the camera on its own path. The file is plain text, one thing per line, '#' starts a comment
// (the same style as a camera keyframes file, camera_keyframes.h, whose `key` lines it reuses for the camera):
//
//     film     windmill-day                 # the key: letters, digits and '-' (default: the file's name); `cartoon:<key>` and `--video-preset <key>` name the film
//     name     A Day at the Windmill        # what the scene list calls it
//     about    The sails turn, the sun sets, the camera pulls back.   # one sentence for the scene list
//     scene    windmill.pbrt                # the pbrt scene, relative to this file (required)
//     length   12                           # seconds (required)
//     fps      24                           # (default 24)
//     still    6                            # the moment the scene list's picture shows (default: the middle)
//     tags     emissive,dusk                # words from the scene list's vocabulary (cartoons.h); others are ignored
//     ease     1                            # 1 (the default): motions start and stop gently; 0: they move at an even pace
//
//     shot 0 The sails                      # a camera shot that begins at 0 seconds ... (a hard cut at the next shot's start)
//       key  px py pz  tx ty tz  [fov [lensRadius focusDistance]]  [hold S]    # as in a camera keyframes file: where the camera is and what it looks at
//       key  ...
//     shot 6 Pull back
//       key  ...
//
//     turn  windmill about 0 3 0 axis 0 0 1   0 0   12 720   # rotate NAME: `about X Y Z` the point it turns about (default 0 0 0), `axis X Y Z` (default 0 1 0), then pairs: time degrees
//     move  cart   0 -5 0 0   12 5 0 0                      # translate NAME: groups of time x y z
//     grow  flower about 0 0 0   0 0.1   4 1                 # scale NAME about a point: pairs time scale
//     light lamp   0 0   2 1   10 1   12 0                   # the power of the lights in NAME times k: pairs time k (a lamp that fades up and down)
//     show  ghost  3 5   8 9                                 # NAME is there only between these times (any number of pairs); outside them it is left out
//
// Between two keys a value goes smoothly (or evenly with `ease 0`); before the first key it holds the first value and after the last it holds the last. Coordinates are pbrt's own (the
// scene's, not the cartoon toolkit's), and a film with no `shot` lines keeps the scene's own camera. Everything a pbrt file refers to by a relative path (meshes, pictures) is looked up
// beside the pbrt file. A problem in a film file is reported with its line number when the file is loaded (`ray_tracer --film-check FILE`) and the film is left out.

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include "../camera_keyframes.h"

namespace cartoon {
namespace film_file {

enum class Kind { Move, Turn, Grow, Light, Show };

struct Key {
	double t = 0.0;
	double v[3] = {0.0, 0.0, 0.0};
};

struct Anim {
	Kind kind = Kind::Move;
	std::string name;
	int line = 0;
	double about[3] = {0.0, 0.0, 0.0};
	double axis[3] = {0.0, 1.0, 0.0};
	std::vector<Key> keys;                               // Move: v = the offset; Turn: v[0] = degrees; Grow and Light: v[0] = the factor
	std::vector<std::pair<double, double>> intervals;   // Show
};

struct Shot {
	double start = 0.0;
	std::string name;
	camera_keyframes::Path path;
};

// What a film file says.
struct Spec {
	std::string key, name, about, scene;   // `scene` as written
	std::string tags;
	double length = 0.0, still = -1.0;
	int fps = 24;
	bool ease = true;
	std::vector<Shot> shots;
	std::vector<Anim> anims;
};

struct Parsed {
	bool ok = false;
	std::string error;   // "file:line: what is wrong"
	Spec spec;
};

namespace detail {

inline std::string trim(const std::string &s) {
	std::size_t a = 0, b = s.size();
	while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
	while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
	return s.substr(a, b - a);
}
inline std::vector<std::string> words(const std::string &s) {
	std::vector<std::string> out;
	std::istringstream in(s);
	for (std::string w; in >> w;) out.push_back(w);
	return out;
}
inline bool number(const std::string &s, double &out) {
	if (s.empty()) return false;
	try {
		std::size_t used = 0;
		out = std::stod(s, &used);
		return used == s.size() && std::isfinite(out);
	} catch (const std::exception &) {
		return false;
	}
}
inline bool validKey(const std::string &k) {
	if (k.empty() || k.size() > 60) return false;
	for (char c : k)
		if (!(std::islower(static_cast<unsigned char>(c)) || std::isdigit(static_cast<unsigned char>(c)) || c == '-')) return false;
	return true;
}
inline std::string keyFromFileName(const std::string &path) {
	std::string k = std::filesystem::path(path).stem().string();
	for (char &c : k) c = std::isalnum(static_cast<unsigned char>(c)) ? static_cast<char>(std::tolower(static_cast<unsigned char>(c))) : '-';
	return k;
}

}  // namespace detail

// Reads a film file's text. `where` names it in the messages. The scene is not read here (see load()), so the names it animates are not yet checked.
inline Parsed parse(const std::string &text, const std::string &where) {
	using detail::number;
	Parsed r;
	Spec &s = r.spec;
	std::istringstream in(text);
	std::string line;
	int n = 0;
	auto fail = [&](const std::string &what) {
		r.ok = false;
		r.error = where + ":" + std::to_string(n) + ": " + what;
		return r;
	};
	std::string shotText;   // the key lines of the shot being read
	auto closeShot = [&]() -> std::string {
		if (s.shots.empty()) return std::string();
		if (shotText.empty()) return "the shot \"" + s.shots.back().name + "\" has no `key` lines";
		// one `key` is a camera that stays where it is (a camera path wants two)
		std::size_t keyLines = 0;
		for (std::size_t at = shotText.find("key "); at != std::string::npos; at = shotText.find("key ", at + 1)) ++keyLines;
		if (keyLines == 1) {
			const std::size_t at = shotText.find("key ");
			shotText += shotText.substr(at, shotText.find('\n', at) - at + 1);
		}
		const camera_keyframes::ParseResult p = camera_keyframes::parse(shotText);
		if (!p.ok) return p.error;
		s.shots.back().path = p.path;
		shotText.clear();
		return std::string();
	};
	int shotLine = 0;
	while (std::getline(in, line)) {
		++n;
		if (const std::size_t hash = line.find('#'); hash != std::string::npos) line.erase(hash);
		line = detail::trim(line);
		if (line.empty()) continue;
		const std::vector<std::string> w = detail::words(line);
		const std::string &verb = w[0];
		const std::string rest = detail::trim(line.substr(verb.size()));
		if (verb == "key") {
			if (s.shots.empty()) return fail("a `key` line belongs to a shot: start one with `shot 0 NAME` first");
			shotText += line + "\n";
		} else if (verb == "film") {
			if (!detail::validKey(rest)) return fail("the film's key must be lower-case letters, digits and '-' (got \"" + rest + "\")");
			s.key = rest;
		} else if (verb == "name") {
			s.name = rest;
		} else if (verb == "about") {
			s.about = rest;
		} else if (verb == "scene") {
			s.scene = rest;
		} else if (verb == "tags") {
			s.tags = rest;
		} else if (verb == "length") {
			if (w.size() != 2 || !number(w[1], s.length) || !(s.length > 0.0 && s.length <= 3600.0)) return fail("length is a number of seconds, more than 0 and at most 3600");
		} else if (verb == "fps") {
			double v = 0;
			if (w.size() != 2 || !number(w[1], v) || v < 1 || v > 240 || v != std::floor(v)) return fail("fps is a whole number from 1 to 240");
			s.fps = static_cast<int>(v);
		} else if (verb == "still") {
			if (w.size() != 2 || !number(w[1], s.still) || s.still < 0) return fail("still is a time in seconds");
		} else if (verb == "ease") {
			double v = 1;
			if (w.size() != 2 || !number(w[1], v)) return fail("ease is 1 or 0");
			if (s.shots.empty()) s.ease = v != 0.0;
			else shotText += line + "\n";   // inside a shot it is the camera path's own `ease`
		} else if (verb == "shot") {
			if (const std::string e = closeShot(); !e.empty()) return fail(e);
			Shot shot;
			if (w.size() < 2 || !number(w[1], shot.start) || shot.start < 0) return fail("a shot starts with its time in seconds: `shot 0 Name`");
			shot.name = detail::trim(rest.substr(w[1].size()));
			if (shot.name.empty()) shot.name = "Shot " + std::to_string(s.shots.size() + 1);
			if (s.shots.empty() && shot.start != 0.0) return fail("the first shot starts at 0");
			if (!s.shots.empty() && shot.start <= s.shots.back().start) return fail("shots must start later one after the other");
			s.shots.push_back(shot);
			shotLine = n;
		} else if (verb == "move" || verb == "turn" || verb == "grow" || verb == "light" || verb == "show") {
			if (w.size() < 3) return fail("`" + verb + " NAME ...` needs a name and values");
			Anim a;
			a.name = w[1];
			a.line = n;
			std::size_t i = 2;
			const std::size_t group = verb == "move" ? 4 : 2;
			a.kind = verb == "move" ? Kind::Move : verb == "turn" ? Kind::Turn : verb == "grow" ? Kind::Grow : verb == "light" ? Kind::Light : Kind::Show;
			while (i < w.size() && (w[i] == "about" || w[i] == "axis")) {
				if (a.kind != Kind::Turn && !(a.kind == Kind::Grow && w[i] == "about")) return fail("`" + w[i] + "` is not used by " + verb);
				double *dst = w[i] == "about" ? a.about : a.axis;
				if (i + 3 >= w.size()) return fail("`" + w[i] + "` needs three numbers");
				for (int k = 0; k < 3; ++k)
					if (!number(w[i + 1 + k], dst[k])) return fail("`" + w[i] + "` needs three numbers");
				i += 4;
			}
			if (a.kind == Kind::Turn && std::abs(a.axis[0]) + std::abs(a.axis[1]) + std::abs(a.axis[2]) == 0.0) return fail("a turn's axis must not be 0 0 0");
			const std::size_t count = w.size() - i;
			if (count == 0 || count % group != 0) return fail("`" + verb + "` takes " + (group == 4 ? "groups of time x y z" : "pairs of numbers") + " after the name");
			for (; i < w.size(); i += group) {
				double t = 0, v[3] = {0, 0, 0};
				if (!number(w[i], t)) return fail("\"" + w[i] + "\" is not a number");
				if (a.kind == Kind::Show) {
					double t1 = 0;
					if (!number(w[i + 1], t1) || t1 < t) return fail("show takes pairs of times, the second not before the first");
					a.intervals.push_back({t, t1});
					continue;
				}
				for (std::size_t k = 0; k + 1 < group; ++k)
					if (!number(w[i + 1 + k], v[k])) return fail("\"" + w[i + 1 + k] + "\" is not a number");
				if (!a.keys.empty() && t < a.keys.back().t) return fail("the times of a track must not go back");
				Key key;
				key.t = t;
				for (int k = 0; k < 3; ++k) key.v[k] = v[k];
				a.keys.push_back(key);
			}
			s.anims.push_back(a);
		} else {
			return fail("unknown word \"" + verb + "\" (film, name, about, scene, length, fps, still, tags, ease, shot, key, move, turn, grow, light, show)");
		}
	}
	n = shotLine;
	if (const std::string e = closeShot(); !e.empty()) return fail(e);
	if (s.scene.empty()) {
		n = 0;
		return fail("no `scene FILE.pbrt` line");
	}
	if (!(s.length > 0.0)) {
		n = 0;
		return fail("no `length SECONDS` line");
	}
	for (const Shot &shot : s.shots)
		if (shot.start >= s.length) {
			n = shotLine;
			return fail("the shot \"" + shot.name + "\" starts at " + std::to_string(shot.start) + ", after the film's end");
		}
	r.ok = true;
	return r;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The scene, cut into the plain parts and the animated blocks
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
struct Block {
	std::string name;
	std::string head;   // from the `# @anim` line to the end of the AttributeBegin line
	std::string body;   // the rest, up to and including the matching AttributeEnd line
};
struct Piece {
	std::string text;
	int block = -1;   // >= 0: an animated block (its head and body are in `blocks`); `text` is unused
};
enum class Marker { None, LookAt, Camera };

struct Scene {
	std::string headerBefore, headerBetween, headerAfter;   // the header cut at the LookAt and Camera statements
	bool hasLookAt = false, hasCamera = false;
	std::string cameraStatement;                            // the original Camera line (for its field of view and lens)
	std::vector<Piece> world;                                // after WorldBegin (the WorldBegin line is the end of the header)
	std::vector<Block> blocks;
	std::vector<std::string> names;                          // the distinct `# @anim` names
	int samples = 128;
	std::string header;                                      // the whole header as it is in the file
};

namespace detail {

inline std::vector<std::string> splitLines(const std::string &text) {
	std::vector<std::string> out;
	std::size_t at = 0;
	while (at < text.size()) {
		std::size_t nl = text.find('\n', at);
		if (nl == std::string::npos) nl = text.size() - 1;
		out.push_back(text.substr(at, nl - at + 1));
		at = nl + 1;
	}
	return out;
}
inline std::string firstWord(const std::string &line) {
	std::size_t a = 0;
	while (a < line.size() && std::isspace(static_cast<unsigned char>(line[a]))) ++a;
	std::size_t b = a;
	while (b < line.size() && std::isalpha(static_cast<unsigned char>(line[b]))) ++b;
	return line.substr(a, b - a);
}
// The statement that starts at line `i`: that line and the lines after it that go on with its numbers and parameters (they do not start with a letter).
inline std::size_t statementEnd(const std::vector<std::string> &lines, std::size_t i) {
	std::size_t j = i + 1;
	while (j < lines.size()) {
		const std::string t = trim(lines[j]);
		if (t.empty() || t[0] == '#' || std::isalpha(static_cast<unsigned char>(t[0]))) break;
		++j;
	}
	return j;
}

}  // namespace detail

// Cuts a pbrt scene's text. Returns "" or what is wrong (a `# @anim` that is not followed by an AttributeBegin, a block never closed).
inline std::string cutScene(const std::string &text, Scene &out) {
	const std::vector<std::string> lines = detail::splitLines(text);
	std::size_t i = 0;
	// ---- the header: everything up to and including WorldBegin ----
	enum { Before, Between, After } where = Before;
	std::size_t world = lines.size();
	for (; i < lines.size(); ++i) {
		const std::string w = detail::firstWord(lines[i]);
		if (w == "WorldBegin") {
			world = i + 1;
			break;
		}
		if (w == "LookAt" && !out.hasLookAt) {
			out.hasLookAt = true;
			where = Between;
			i = detail::statementEnd(lines, i) - 1;
			continue;
		}
		if (w == "Camera" && !out.hasCamera) {
			out.hasCamera = true;
			const std::size_t end = detail::statementEnd(lines, i);
			for (std::size_t k = i; k < end; ++k) out.cameraStatement += lines[k];
			where = After;
			i = end - 1;
			continue;
		}
		std::string &dst = where == Before ? out.headerBefore : where == Between ? out.headerBetween : out.headerAfter;
		dst += lines[i];
		if (w == "Sampler") {
			static const std::regex px("\"integer pixelsamples\"\\s*\\[\\s*([0-9]+)");
			std::smatch m;
			if (std::regex_search(lines[i], m, px)) out.samples = std::stoi(m[1]);
		}
	}
	if (world == lines.size()) return "the scene has no WorldBegin line";
	// (the header as it is in the file, for a film with no shots)
	for (std::size_t k = 0; k < world; ++k) out.header += lines[k];
	// ---- the world: plain text and the tagged blocks ----
	std::string plain;
	auto flush = [&]() {
		if (!plain.empty()) out.world.push_back({plain, -1});
		plain.clear();
	};
	for (i = world; i < lines.size(); ++i) {
		const std::string t = detail::trim(lines[i]);
		if (t.compare(0, 1, "#") == 0) {
			std::istringstream in(t.substr(1));
			std::string tag, name;
			in >> tag >> name;
			if (tag == "@anim" && !name.empty()) {
				std::size_t b = i + 1;
				while (b < lines.size() && (detail::trim(lines[b]).empty() || detail::trim(lines[b])[0] == '#')) ++b;
				if (b >= lines.size() || detail::firstWord(lines[b]) != "AttributeBegin") return "line " + std::to_string(i + 1) + ": `# @anim " + name + "` is not followed by an AttributeBegin";
				int depth = 0;
				std::size_t e = b;
				for (; e < lines.size(); ++e) {
					const std::string w = detail::firstWord(lines[e]);
					if (w == "AttributeBegin") ++depth;
					if (w == "AttributeEnd" && --depth == 0) break;
				}
				if (e >= lines.size()) return "line " + std::to_string(b + 1) + ": the AttributeBegin after `# @anim " + name + "` is never closed";
				Block block;
				block.name = name;
				for (std::size_t k = i; k <= b; ++k) block.head += lines[k];
				for (std::size_t k = b + 1; k <= e; ++k) block.body += lines[k];
				flush();
				out.world.push_back({std::string(), static_cast<int>(out.blocks.size())});
				out.blocks.push_back(block);
				if (std::find(out.names.begin(), out.names.end(), name) == out.names.end()) out.names.push_back(name);
				i = e;
				continue;
			}
		}
		plain += lines[i];
	}
	flush();
	return std::string();
}

namespace detail {

// A relative file name in a pbrt scene made absolute (the frames are written to a folder of their own, and a still is written in the program's own folder).
inline std::string absolutize(const std::string &text, const std::filesystem::path &base) {
	static const std::regex param("(\"string\\s+(?:filename|mapname|lensfile)\"\\s*\\[?\\s*\")([^\"]+)(\")");
	static const std::regex include("^(\\s*(?:Include|Import)\\s+\")([^\"]+)(\")", std::regex::ECMAScript | std::regex::multiline);
	auto fix = [&](const std::string &in, const std::regex &re) {
		std::string out;
		std::sregex_iterator it(in.begin(), in.end(), re), end;
		std::size_t last = 0;
		for (; it != end; ++it) {
			const std::smatch &m = *it;
			out.append(in, last, static_cast<std::size_t>(m.position(0)) - last);
			const std::filesystem::path p = m[2].str();
			out += m[1].str() + (p.is_absolute() ? p : (base / p)).lexically_normal().generic_string() + m[3].str();
			last = static_cast<std::size_t>(m.position(0) + m.length(0));
		}
		out.append(in, last, std::string::npos);
		return out;
	};
	return fix(fix(text, param), include);
}

}  // namespace detail

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// A film made from a file
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
class DataFilm {
  public:
	Spec spec;
	Scene scene;
	std::filesystem::path sceneFile;

	// Reads the film file and its scene; returns "" or the problem, with the file and line.
	std::string load(const std::string &filePath) {
		try {
			return loadFile(filePath);
		} catch (const std::exception &e) {   // (a pattern or a number the text does not fit: a problem of this file, not a crash)
			return filePath + ": " + e.what();
		}
	}

	std::string loadFile(const std::string &filePath) {
		std::ifstream f(filePath, std::ios::binary);
		if (!f) return filePath + ": the file cannot be read";
		const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
		Parsed p = parse(text, filePath);
		if (!p.ok) return p.error;
		spec = p.spec;
		if (spec.key.empty()) spec.key = detail::keyFromFileName(filePath);
		if (!detail::validKey(spec.key)) return filePath + ": the key \"" + spec.key + "\" is not lower-case letters, digits and '-'; give the film a `film KEY` line";
		if (spec.name.empty()) spec.name = spec.key;
		if (spec.still < 0.0 || spec.still > spec.length) spec.still = spec.length * 0.5;
		const std::filesystem::path base = std::filesystem::path(filePath).parent_path();
		sceneFile = std::filesystem::path(spec.scene).is_absolute() ? std::filesystem::path(spec.scene) : base / spec.scene;
		std::ifstream sf(sceneFile, std::ios::binary);
		if (!sf) return filePath + ": the scene " + sceneFile.string() + " cannot be read";
		const std::string sceneText = detail::absolutize(std::string((std::istreambuf_iterator<char>(sf)), std::istreambuf_iterator<char>()), sceneFile.parent_path());
		if (const std::string e = cutScene(sceneText, scene); !e.empty()) return sceneFile.string() + ": " + e;
		for (const Anim &a : spec.anims)
			if (std::find(scene.names.begin(), scene.names.end(), a.name) == scene.names.end()) {
				std::string known;
				for (const std::string &n : scene.names) known += (known.empty() ? "" : ", ") + n;
				return filePath + ":" + std::to_string(a.line) + ": no block of " + sceneFile.filename().string() + " is marked `# @anim " + a.name + "`" + (known.empty() ? " (it has none)" : " (it has: " + known + ")");
			}
		return std::string();
	}

	std::vector<double> shotStarts() const {
		std::vector<double> v;
		for (const Shot &s : spec.shots) v.push_back(s.start);
		if (v.empty()) v.push_back(0.0);
		return v;
	}
	std::string shotNames() const {
		std::string out;
		for (const Shot &s : spec.shots) out += (out.empty() ? "" : "|") + s.name;
		return out.empty() ? "The whole film" : out;
	}

	std::string headerAt(double t) const {
		if (spec.shots.empty()) return scene.header;
		std::string lookAt, camera;
		cameraAt(t, lookAt, camera);
		// The scene's lines before its LookAt, then the new LookAt, the lines between, the new Camera, the lines after. A scene with neither statement gets both after all its header lines.
		return scene.headerBefore + lookAt + scene.headerBetween + camera + scene.headerAfter + "WorldBegin\n";
	}

	std::string sceneAt(double t) const {
		std::string out = headerAt(t);
		for (const Piece &piece : scene.world) {
			if (piece.block < 0) {
				out += piece.text;
				continue;
			}
			const Block &b = scene.blocks[static_cast<std::size_t>(piece.block)];
			if (!visible(b.name, t)) continue;
			out += b.head + transformsFor(b.name, t) + lightsScaled(b.name, b.body, t);
		}
		return out;
	}

  private:
	double progress(const std::vector<Key> &keys, double t, int component) const {
		if (keys.empty()) return 0.0;
		if (t <= keys.front().t) return keys.front().v[component];
		if (t >= keys.back().t) return keys.back().v[component];
		for (std::size_t i = 1; i < keys.size(); ++i) {
			if (t <= keys[i].t) {
				const double span = keys[i].t - keys[i - 1].t;
				double u = span > 0.0 ? (t - keys[i - 1].t) / span : 1.0;
				if (spec.ease) u = u * u * (3.0 - 2.0 * u);
				return keys[i - 1].v[component] + (keys[i].v[component] - keys[i - 1].v[component]) * u;
			}
		}
		return keys.back().v[component];
	}
	bool visible(const std::string &name, double t) const {
		bool anyShow = false, shown = false;
		for (const Anim &a : spec.anims) {
			if (a.kind != Kind::Show || a.name != name) continue;
			anyShow = true;
			for (const auto &iv : a.intervals) shown = shown || (t >= iv.first && t <= iv.second);
		}
		return !anyShow || shown;
	}
	static std::string num(double v) {
		char buf[40];
		std::snprintf(buf, sizeof(buf), "%.6g", v);
		return buf;
	}
	std::string transformsFor(const std::string &name, double t) const {
		std::string out;
		for (const Anim &a : spec.anims) {
			if (a.name != name) continue;
			if (a.kind == Kind::Move) {
				out += "  Translate " + num(progress(a.keys, t, 0)) + " " + num(progress(a.keys, t, 1)) + " " + num(progress(a.keys, t, 2)) + "\n";
			} else if (a.kind == Kind::Turn) {
				out += "  Translate " + num(a.about[0]) + " " + num(a.about[1]) + " " + num(a.about[2]) + "\n  Rotate " + num(progress(a.keys, t, 0)) + " " + num(a.axis[0]) + " " + num(a.axis[1]) + " " +
					   num(a.axis[2]) + "\n  Translate " + num(-a.about[0]) + " " + num(-a.about[1]) + " " + num(-a.about[2]) + "\n";
			} else if (a.kind == Kind::Grow) {
				const double k = std::max(1e-6, progress(a.keys, t, 0));
				out += "  Translate " + num(a.about[0]) + " " + num(a.about[1]) + " " + num(a.about[2]) + "\n  Scale " + num(k) + " " + num(k) + " " + num(k) + "\n  Translate " + num(-a.about[0]) + " " +
					   num(-a.about[1]) + " " + num(-a.about[2]) + "\n";
			}
		}
		return out;
	}
	// The lights of a block with their power multiplied by the block's `light` factor at t.
	std::string lightsScaled(const std::string &name, const std::string &body, double t) const {
		double k = 1.0;
		bool any = false;
		for (const Anim &a : spec.anims)
			if (a.kind == Kind::Light && a.name == name) {
				k *= std::max(0.0, progress(a.keys, t, 0));
				any = true;
			}
		if (!any) return body;
		static const std::regex scaleParam("(\"float\\s+scale\"\\s*\\[\\s*)([-+0-9.eE]+)");
		std::string out;
		for (const std::string &line : detail::splitLines(body)) {
			const std::string w = detail::firstWord(line);
			if (w != "LightSource" && w != "AreaLightSource") {
				out += line;
				continue;
			}
			std::string l = line;
			while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
			std::smatch m;
			if (std::regex_search(l, m, scaleParam)) {
				double old = 1.0;
				detail::number(m[2].str(), old);
				l = l.substr(0, static_cast<std::size_t>(m.position(2))) + num(old * k) + l.substr(static_cast<std::size_t>(m.position(2) + m.length(2)));
			} else {
				l += " \"float scale\" [ " + num(k) + " ]";
			}
			out += l + "\n";
		}
		return out;
	}
	void cameraAt(double t, std::string &lookAt, std::string &camera) const {
		std::size_t k = 0;
		for (std::size_t i = 0; i < spec.shots.size(); ++i)
			if (t >= spec.shots[i].start) k = i;
		const Shot &shot = spec.shots[k];
		const double end = k + 1 < spec.shots.size() ? spec.shots[k + 1].start : spec.length;
		const double span = std::max(1e-6, end - shot.start);
		const camera_keyframes::Key key = camera_keyframes::at(shot.path, std::min(1.0, std::max(0.0, (t - shot.start) / span)), span);
		lookAt = "LookAt " + num(key.pos[0]) + " " + num(key.pos[1]) + " " + num(key.pos[2]) + "  " + num(key.target[0]) + " " + num(key.target[1]) + " " + num(key.target[2]) + "  0 1 0\n";
		// the scene's own field of view and lens unless the keys give theirs
		static const std::regex fovRe("\"float\\s+fov\"\\s*\\[\\s*([-+0-9.eE]+)");
		static const std::regex lensRe("\"float\\s+lensradius\"\\s*\\[\\s*([-+0-9.eE]+)");
		static const std::regex focusRe("\"float\\s+focaldistance\"\\s*\\[\\s*([-+0-9.eE]+)");
		std::smatch m;
		double fov = 40.0, lens = 0.0, focus = 0.0;
		if (std::regex_search(scene.cameraStatement, m, fovRe)) detail::number(m[1].str(), fov);
		if (std::regex_search(scene.cameraStatement, m, lensRe)) detail::number(m[1].str(), lens);
		if (std::regex_search(scene.cameraStatement, m, focusRe)) detail::number(m[1].str(), focus);
		if (key.fov > 0.0) fov = key.fov;
		if (key.hasLens()) {
			lens = key.lensRadius;
			focus = key.focus;
		}
		camera = "Camera \"perspective\" \"float fov\" [ " + num(fov) + " ]";
		if (lens > 0.0) camera += " \"float lensradius\" [ " + num(lens) + " ] \"float focaldistance\" [ " + num(focus > 0.0 ? focus : 1.0) + " ]";
		camera += "\n";
	}
};

}  // namespace film_file
}  // namespace cartoon
