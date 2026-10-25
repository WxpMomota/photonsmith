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
//     shutter  0.5                          # motion blur: a moving block is blurred over this share of a frame (0.5, the default: a film camera's 180-degree shutter); 0: sharp
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
//
// Characters - a film blocked out as a screenplay. A character from the library (characterKinds, film_kinds.h: `nomai-explorer`, `pip`, `person`, `turtle`) is brought in,
// walks and acts (`scale S` on its line sizes a pip or a turtle):
//
//     character  kai nomai-explorer  at 0 0 4  facing 180       # NAME KIND, where it stands (pbrt's x y z; y is the ground) and which way it faces (0: +z, 90: +x)
//     walk       kai  2.0 0.7   0 -2   3 -6                     # NAME, from 2 s at 0.7 m/s, through these points (x z) from where it is
//     gesture    kai  "look up" 8.0 9.0 11.0 11.5               # NAME, a pose of its library, eased in from 8 s to 9 s, out from 11 s to 11.5 s (no out times: held)
//     gesture    kai  "reach to the wall" 12 13 contact 12.8    # contact: the hand meets something (a touch on the soundtrack)
//     say        kai  14.4 "Look, the first star."  rate 1.1  pitch 2   # NAME, a line it says from 14.4 s: its voice, its mouth and a caption (film_speech.h; `nocaption`)
//     feel       pip  happy 3 3.5 6 6.8  amount 0.8             # NAME, an expression on its face eased in and out (film_faces.h: happy, sad, angry, surprised, ...)
//
//     shot 0 Walking
//       follow   kai  behind 3  up 1.7  side 0.6  fov 35          # instead of `key` lines: the camera behind NAME (metres), at a height, to its right, looking at its chest
//     shot 9 Looking up
//       key ...
//       subject  kai                                            # what the shot is about (the film doctor's framing checks); a follow shot is about whom it follows
//
// A film with characters reports its subjects, its footfalls and gesture contacts (Film::events: footsteps on the soundtrack) and its characters' feet (the sliding-feet check).
//
// Props with physics (prop_physics.h) - a marked block that is dropped or thrown and bounces and rolls, or that topples over; each landing is heard (an impact):
//
//     bounce   pebble 6.0  from 0.6 2.4 -3   velocity 0.4 0 -0.2   radius 0.06   # from 6 s: its centre at `from` (where the scene has it), thrown at `velocity`
//     topple   tablet 9.2  pivot 1.4 0 -3.6   axis 0 0 1   height 0.6  depth 0.08   # from 9.2 s it tips over its bottom edge through `pivot` (right-hand turn about `axis`)
//
// (bounce also takes restitution, grip and rolling; topple push and lean: prop_physics.h.)

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include "../camera_keyframes.h"
#include "film_faces.h"
#include "film_speech.h"
#include "film_subjects.h"
#include "music_cues.h"
#include "prop_physics.h"
#include "rig.h"

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

// A shot's camera that follows a character (`follow NAME behind D up H side S fov F`).
struct Follow {
	std::string who;
	double behind = 3.0, up = 1.6, side = 0.0, fov = 0.0;   // fov 0: the scene's
};
struct Shot {
	double start = 0.0;
	std::string name;
	camera_keyframes::Path path;
	bool following = false;
	Follow follow;
	std::vector<std::string> about;   // the characters the shot is about
};

// A character a film file brings in (`character NAME KIND ...`), its walks (`walk`) and its gestures (`gesture`).
struct WalkLine {
	double t0 = 0.0, speed = 0.8;
	std::vector<std::pair<double, double>> points;   // pbrt x z
	int line = 0;
};
struct Character {
	std::string name, kind;
	double start[3] = {0.0, 0.0, 0.0};   // pbrt's coordinates
	double facing = 0.0;                 // degrees: 0 faces +z, 90 faces +x (pbrt's)
	double detail = 0.0;                 // the kind's sculpting cell (0: its own)
	double scale = 1.0;                  // its size (a kind that can be scaled: a child Pip 0.6, a turtle as big as a dog 6)
	std::vector<WalkLine> walks;
	std::vector<rig::Gesture> gestures;
	std::vector<faces::Feeling> feelings;   // its expressions (`feel`)
	int line = 0;
};

// What a character of one kind is at a moment: its frame in story space (the toolkit's: x mirrored from pbrt's), how many steps it has walked, whether it is walking, its
// gestures, the time and its detail.
struct CharacterMoment {
	Mat4 frame;
	double steps = 0.0;
	bool moving = false;
	double t = 0.0;
	const std::vector<rig::Gesture> *gestures = nullptr;
	double detail = 0.0;
	Vec3 trail;   // how far a spring on its place lags it (body space: +x its left, +z forward): for what hangs and swings - a robe's hem, a tail (rig::lag)
	double voice = 0.0;        // how loud it is speaking, 0..1 (its `say` lines, its `sample ... by NAME` cues: music_cues::voiceAt) - a mouth opens by it
	speech::Mouth mouth;       // the mouth shape its `say` lines need now (open, wide, round)
	faces::Face face;          // what it feels now, on its face (its `feel` lines)
	bool feetLocked = false;   // its feet held where they came down (rig::FootLock), in body space: when its kind says where its gait puts them
	Vec3 lockedFeet[4];
	double scale = 1.0;        // its size (Character::scale)
	int seed = 0;              // a number of its own (from its name): its blinks
};
// A kind of character: its scene text (story space, as the toolkit writes it) and its feet at a moment, the point a camera and the film doctor look at (body space), and its walk's
// step (rig::PathWalk's stride and first step).
struct CharacterKind {
	std::function<std::string(const CharacterMoment &)> text;
	std::function<std::vector<Foot>(const CharacterMoment &)> feet;
	Vec3 chest{0.0, 1.25, 0.0};
	double stride = 0.84, firstStep = 0.5;
	// (optional) where its gait puts each of its feet at so many steps (body space, at scale 1) and whether that foot is down: then its feet are locked where they come down
	std::function<Vec3(double, int)> footAt;
	std::function<bool(double, int)> plantedAt;
	int footCount = 2;             // (a turtle has four)
	bool scalable = false;         // whether `scale` on its character line is drawn (its stride, chest and feet are scaled with it)
	std::vector<std::string> poses;   // its library's poses (a gesture naming another is an error when the film is loaded)
	speech::Voice voice;              // how its `say` lines sound (its pitch is moved a little for each character, by its name)
	bool face = true;                 // whether it has a face to show a feeling on (the masked explorer has not)
};
// The library of kinds, by name (cartoons.h fills it before any film file is read).
inline std::map<std::string, CharacterKind> &characterKinds() {
	static std::map<std::string, CharacterKind> kinds;
	return kinds;
}

// What a film file says.
struct Spec {
	std::string key, name, about, scene;   // `scene` as written
	std::string tags;
	double length = 0.0, still = -1.0;
	int fps = 24;
	bool ease = true;
	double shutter = 0.5;   // the share of a frame a moving block is blurred over (0: none)
	std::vector<Shot> shots;
	std::vector<Anim> anims;
	std::vector<Character> characters;
	// props with physics: the block they move, and the line that says so
	std::vector<std::pair<std::string, physics::Bounce>> bounces;
	std::vector<std::pair<std::string, physics::Topple>> topples;
	std::vector<std::pair<std::string, int>> physicsLines;
	std::vector<speech::Line> lines;   // what the characters say (`say`)
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
// The words of a line, a "quoted phrase" as one (a gesture's pose: "look up").
inline std::vector<std::string> tokens(const std::string &s) {
	std::vector<std::string> out;
	for (std::size_t i = 0; i < s.size();) {
		if (std::isspace(static_cast<unsigned char>(s[i]))) {
			++i;
			continue;
		}
		if (s[i] == '"') {
			const std::size_t close = s.find('"', i + 1);
			out.push_back(s.substr(i + 1, (close == std::string::npos ? s.size() : close) - i - 1));
			i = close == std::string::npos ? s.size() : close + 1;
			continue;
		}
		std::size_t j = i;
		while (j < s.size() && !std::isspace(static_cast<unsigned char>(s[j]))) ++j;
		out.push_back(s.substr(i, j - i));
		i = j;
	}
	return out;
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
		if (s.shots.back().following) {
			if (!shotText.empty()) return "the shot \"" + s.shots.back().name + "\" has both `key` lines and `follow`";
			return std::string();
		}
		if (shotText.empty()) return "the shot \"" + s.shots.back().name + "\" has no `key` lines (or a `follow` line)";
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
		} else if (verb == "shutter") {
			if (w.size() != 2 || !number(w[1], s.shutter) || s.shutter < 0.0 || s.shutter > 1.0) return fail("shutter is a share of a frame, from 0 (sharp) to 1");
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
		} else if (verb == "bounce" || verb == "topple") {
			const std::vector<std::string> t = detail::tokens(line);
			double start = 0.0;
			if (t.size() < 3 || !number(t[2], start)) return fail("`" + verb + " NAME START ...`: a block's name and a start time");
			const auto three = [&](std::size_t i, Vec3 &v) {   // the three numbers after word i
				double a = 0, b = 0, c = 0;
				if (i + 3 >= t.size() || !number(t[i + 1], a) || !number(t[i + 2], b) || !number(t[i + 3], c)) return false;
				v = {a, b, c};
				return true;
			};
			if (verb == "bounce") {
				physics::Bounce b;
				b.start = start;
				bool from = false;
				for (std::size_t i = 3; i < t.size();) {
					double v = 0.0;
					if ((t[i] == "from" || t[i] == "velocity") && i + 3 < t.size()) {
						if (!three(i, t[i] == "from" ? b.from : b.velocity)) return fail("`" + t[i] + "` takes three numbers");
						from = from || t[i] == "from";
						i += 4;
					} else if ((t[i] == "radius" || t[i] == "restitution" || t[i] == "grip" || t[i] == "rolling") && i + 1 < t.size() && number(t[i + 1], v)) {
						(t[i] == "radius" ? b.radius : t[i] == "restitution" ? b.restitution : t[i] == "grip" ? b.grip : b.rolling) = v;
						i += 2;
					} else {
						return fail("\"" + t[i] + "\": a bounce takes from X Y Z, velocity X Y Z, radius, restitution, grip and rolling");
					}
				}
				if (!from) return fail("a bounce needs `from X Y Z`: where the block's centre is in the scene");
				b.ground = 0.0;
				s.bounces.push_back({t[1], b});
			} else {
				physics::Topple p;
				p.start = start;
				for (std::size_t i = 3; i < t.size();) {
					double v = 0.0;
					if ((t[i] == "pivot" || t[i] == "axis") && i + 3 < t.size()) {
						if (!three(i, t[i] == "pivot" ? p.pivot : p.axis)) return fail("`" + t[i] + "` takes three numbers");
						i += 4;
					} else if ((t[i] == "height" || t[i] == "depth" || t[i] == "push" || t[i] == "lean") && i + 1 < t.size() && number(t[i + 1], v)) {
						(t[i] == "height" ? p.height : t[i] == "depth" ? p.depth : t[i] == "push" ? p.push : p.lean) = v;
						i += 2;
					} else {
						return fail("\"" + t[i] + "\": a topple takes pivot X Y Z, axis X Y Z, height, depth, push and lean");
					}
				}
				if (length(p.axis) < 1e-9) return fail("a topple's axis must not be 0 0 0");
				s.topples.push_back({t[1], p});
			}
			s.physicsLines.push_back({t[1], n});
		} else if (verb == "character") {
			const std::vector<std::string> t = detail::tokens(line);
			if (t.size() < 3) return fail("`character NAME KIND [at X Y Z] [facing DEGREES] [scale S] [detail CELL]`");
			Character c;
			c.name = t[1];
			c.kind = t[2];
			c.line = n;
			for (const Character &other : s.characters)
				if (other.name == c.name) return fail("there is already a character called " + c.name);
			for (std::size_t i = 3; i < t.size();) {
				if (t[i] == "at" && i + 3 < t.size()) {
					for (int k = 0; k < 3; ++k)
						if (!number(t[i + 1 + k], c.start[k])) return fail("`at` takes three numbers");
					i += 4;
				} else if ((t[i] == "facing" || t[i] == "detail" || t[i] == "scale") && i + 1 < t.size()) {
					if (!number(t[i + 1], t[i] == "facing" ? c.facing : t[i] == "detail" ? c.detail : c.scale) || !(c.scale > 0.0)) return fail("`" + t[i] + "` takes a number" + std::string(t[i] == "scale" ? " above 0" : ""));
					i += 2;
				} else {
					return fail("\"" + t[i] + "\": a character takes `at X Y Z`, `facing DEGREES`, `scale S` and `detail CELL`");
				}
			}
			s.characters.push_back(c);
		} else if (verb == "walk" || verb == "gesture" || verb == "feel") {
			const std::vector<std::string> t = detail::tokens(line);
			Character *c = nullptr;
			for (Character &each : s.characters)
				if (t.size() > 1 && each.name == t[1]) c = &each;
			if (!c) return fail("`" + verb + "` needs the name of a character brought in before it with `character`");
			if (verb == "feel") {
				faces::Feeling f;
				rig::Gesture &g = f.when;
				if (t.size() < 5 || !number(t[3], g.in0) || !number(t[4], g.in1) || g.in1 < g.in0) return fail("`feel NAME EXPRESSION IN0 IN1 [OUT0 OUT1] [amount A]`");
				g.pose = t[2];
				if (!faces::expressions().count(g.pose)) return fail("no expression called \"" + g.pose + "\" (there are: " + faces::expressionList() + ")");
				std::size_t i = 5;
				if (i + 1 < t.size() && t[i] != "amount") {
					if (!number(t[i], g.out0) || !number(t[i + 1], g.out1) || g.out0 < g.in1 || g.out1 < g.out0) return fail("a feeling's out times come after its in times");
					i += 2;
				}
				if (i + 1 < t.size() && t[i] == "amount") {
					if (!number(t[i + 1], f.amount) || f.amount < 0.0 || f.amount > 2.0) return fail("`amount` is from 0 to 2");
					i += 2;
				}
				if (i != t.size()) return fail("\"" + t[i] + "\" is not part of a feeling");
				c->feelings.push_back(f);
				continue;
			}
			if (verb == "walk") {
				WalkLine wl;
				wl.line = n;
				if (t.size() < 6 || (t.size() - 4) % 2 != 0 || !number(t[2], wl.t0) || !number(t[3], wl.speed) || wl.speed <= 0.0)
					return fail("`walk NAME FROM SPEED X Z [X Z ...]`: a start time, a speed above 0 and points");
				for (std::size_t i = 4; i + 1 < t.size(); i += 2) {
					double x = 0, z = 0;
					if (!number(t[i], x) || !number(t[i + 1], z)) return fail("a walk's points are pairs of numbers (x z)");
					wl.points.push_back({x, z});
				}
				if (!c->walks.empty() && wl.t0 < c->walks.back().t0) return fail("a character's walks go in time order");
				c->walks.push_back(wl);
			} else {
				rig::Gesture g;
				if (t.size() < 5 || !number(t[3], g.in0) || !number(t[4], g.in1) || g.in1 < g.in0) return fail("`gesture NAME \"POSE\" IN0 IN1 [OUT0 OUT1] [contact T]`");
				g.pose = t[2];
				std::size_t i = 5;
				if (i + 1 < t.size() && t[i] != "contact") {
					if (!number(t[i], g.out0) || !number(t[i + 1], g.out1) || g.out0 < g.in1 || g.out1 < g.out0) return fail("a gesture's out times come after its in times");
					i += 2;
				}
				if (i + 1 < t.size() && t[i] == "contact") {
					if (!number(t[i + 1], g.contact)) return fail("`contact` takes a time");
					i += 2;
				}
				if (i != t.size()) return fail("\"" + t[i] + "\" is not part of a gesture");
				c->gestures.push_back(g);
			}
		} else if (verb == "say") {
			const std::vector<std::string> t = detail::tokens(line);
			speech::Line l;
			l.line = n;
			if (t.size() < 4 || !number(t[2], l.t) || l.t < 0.0) return fail("`say NAME T \"WHAT IT SAYS\" [rate R] [pitch SEMITONES] [nocaption]`");
			l.who = t[1];
			l.text = t[3];
			for (std::size_t i = 4; i < t.size();) {
				if (t[i] == "nocaption") {
					l.caption = false;
					++i;
				} else if ((t[i] == "rate" || t[i] == "pitch") && i + 1 < t.size() && number(t[i + 1], t[i] == "rate" ? l.rate : l.pitch)) {
					i += 2;
				} else {
					return fail("\"" + t[i] + "\": a line takes rate R, pitch SEMITONES and nocaption");
				}
			}
			if (!(l.rate > 0.0)) return fail("a line's rate is more than 0");
			s.lines.push_back(l);
		} else if (verb == "follow" || verb == "subject") {
			if (s.shots.empty()) return fail("`" + verb + "` belongs to a shot: start one with `shot 0 NAME` first");
			const std::vector<std::string> t = detail::tokens(line);
			if (t.size() < 2) return fail("`" + verb + "` needs a character's name");
			Shot &shot = s.shots.back();
			if (verb == "subject") {
				for (std::size_t i = 1; i < t.size(); ++i) shot.about.push_back(t[i]);
				continue;
			}
			shot.following = true;
			shot.follow.who = t[1];
			for (std::size_t i = 2; i < t.size(); i += 2) {
				double v = 0.0;
				if (i + 1 >= t.size() || !number(t[i + 1], v)) return fail("`follow NAME [behind D] [up H] [side S] [fov F]`: each takes a number");
				if (t[i] == "behind") shot.follow.behind = v;
				else if (t[i] == "up") shot.follow.up = v;
				else if (t[i] == "side") shot.follow.side = v;
				else if (t[i] == "fov") shot.follow.fov = v;
				else return fail("\"" + t[i] + "\": a follow takes behind, up, side and fov");
			}
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
			return fail("unknown word \"" + verb + "\" (film, name, about, scene, length, fps, still, tags, ease, shutter, shot, key, follow, subject, move, turn, grow, light, show, bounce, topple, character, walk, gesture, feel, say)");
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
		for (const auto &[name, line] : spec.physicsLines)
			if (std::find(scene.names.begin(), scene.names.end(), name) == scene.names.end())
				return filePath + ":" + std::to_string(line) + ": no block of " + sceneFile.filename().string() + " is marked `# @anim " + name + "`";
		for (const Character &c : spec.characters)
			if (!characterKinds().count(c.kind)) {
				std::string known;
				for (const auto &[name, kind] : characterKinds()) known += (known.empty() ? "" : ", ") + name;
				return filePath + ":" + std::to_string(c.line) + ": no kind of character called \"" + c.kind + "\" (there are: " + (known.empty() ? "none" : known) + ")";
			}
		for (const Character &c : spec.characters) {
			const CharacterKind &kind = characterKinds().at(c.kind);
			if (c.scale != 1.0 && !kind.scalable) return filePath + ":" + std::to_string(c.line) + ": a " + c.kind + " cannot be scaled";
			if (!c.feelings.empty() && !kind.face) return filePath + ": " + c.name + " has a `feel` line, but a " + c.kind + " has no face to show it (give it a gesture)";
			if (kind.poses.empty()) continue;
			for (const rig::Gesture &g : c.gestures)
				if (std::find(kind.poses.begin(), kind.poses.end(), g.pose) == kind.poses.end()) {
					std::string known;
					for (const std::string &pose : kind.poses) known += (known.empty() ? "\"" : ", \"") + pose + "\"";
					return filePath + ": " + c.name + "'s gesture \"" + g.pose + "\" is not a pose of a " + c.kind + " (its poses: " + known + ")";
				}
		}
		for (const speech::Line &l : spec.lines)
			if (!character(l.who)) return filePath + ":" + std::to_string(l.line) + ": " + l.who + " says a line but is not brought in with `character`";
		spoken.clear();
		for (const speech::Line &l : spec.lines) spoken.push_back(speech::syllablesOf(l));
		for (const Shot &shot : spec.shots) {
			std::vector<std::string> named = shot.about;
			if (shot.following) named.push_back(shot.follow.who);
			for (const std::string &who : named)
				if (!character(who)) return filePath + ": the shot \"" + shot.name + "\" names " + who + ", who is not brought in with `character`";
		}
		return std::string();
	}

	std::vector<std::vector<speech::Syllable>> spoken;   // each line's syllables (spec.lines)

	// ---- characters ----
	const Character *character(const std::string &name) const {
		for (const Character &c : spec.characters)
			if (c.name == name) return &c;
		return nullptr;
	}
	static Vec3 story(double x, double y, double z) { return {-x, y, z}; }   // (pbrt's coordinates to the toolkit's: x mirrored)
	// A walk of a character as a path in story space, from where it was before the walk.
	rig::PathWalk pathOf(const Character &c, std::size_t walk) const {
		const CharacterKind &kind = characterKinds().at(c.kind);
		Vec3 from = story(c.start[0], c.start[1], c.start[2]);
		for (std::size_t i = 0; i < walk; ++i) {
			const auto &last = c.walks[i].points.back();
			from = story(last.first, c.start[1], last.second);
		}
		rig::PathWalk w;
		w.points.push_back(from);
		for (const auto &pt : c.walks[walk].points) w.points.push_back(story(pt.first, c.start[1], pt.second));
		w.start = c.walks[walk].t0;
		w.speed = c.walks[walk].speed;
		w.stride = kind.stride * c.scale;
		w.firstStep = kind.firstStep;
		return w;
	}
	// Where a character is at t (story space), which way it faces (degrees, rig::headingOf), how many steps it has made in its walk and whether it is walking. After a walk it
	// stands where the walk ended, its feet where they came down.
	CharacterMoment momentOf(const Character &c, double t, Vec3 *at = nullptr) const {
		Vec3 where = story(c.start[0], c.start[1], c.start[2]);
		double heading = -c.facing, steps = 0.0;
		bool moving = false;
		for (std::size_t i = 0; i < c.walks.size(); ++i) {
			if (t < c.walks[i].t0) break;
			const rig::PathWalk path = pathOf(c, i);
			const rig::PathWalk::At a = path.at(t);
			where = a.at;
			heading = a.yaw;
			steps = a.steps;
			moving = a.moving;
			if (moving) break;
		}
		CharacterMoment m;
		m.frame = mul(translate(where), rotate({0.0, 1.0, 0.0}, heading));
		m.steps = steps;
		m.moving = moving;
		m.t = t;
		m.gestures = &c.gestures;
		m.detail = c.detail;
		m.scale = c.scale;
		m.seed = seedOf(c.name);
		if (at) *at = where;
		return m;
	}
	// Its feet held where they came down (CharacterMoment::lockedFeet), when its kind says where its gait puts them and it has walked.
	void lockFeet(const Character &c, double t, CharacterMoment &m) const {
		const CharacterKind &kind = characterKinds().at(c.kind);
		if (!kind.footAt || !kind.plantedAt || (m.steps <= 0.0 && !m.moving)) return;
		rig::FootLock k;
		k.frameAt = [this, &c](double u) { return momentOf(c, u).frame; };
		k.stepsAt = [this, &c](double u) { return momentOf(c, u).steps; };
		const double scale = c.scale;
		k.footAt = [&kind, scale](double steps, int s) { return kind.footAt(steps, s) * scale; };
		k.plantedAt = kind.plantedAt;
		m.feetLocked = true;
		for (int s = 0; s < std::min(4, kind.footCount); ++s) m.lockedFeet[s] = rig::lockedFoot(k, s, t);
	}
	// How far a loose spring on the character's place lags it at t, in its body's space (CharacterMoment::trail).
	Vec3 trailOf(const Character &c, double t, const CharacterMoment &m) const {
		static const rig::Spring loose{30.0, 5.0, 3.0, 1.0 / 240.0};
		const Vec3 lag = rig::lag(loose, [this, &c](double u) {
			Vec3 at;
			momentOf(c, u, &at);
			return at;
		}, t);
		const Vec3 origin = transformPoint(m.frame, {0.0, 0.0, 0.0});
		const Vec3 left = transformPoint(m.frame, {1.0, 0.0, 0.0}) - origin, forward = transformPoint(m.frame, {0.0, 0.0, 1.0}) - origin;
		return {dot(lag, left), 0.0, dot(lag, forward)};
	}
	Vec3 chestOf(const Character &c, double t) const { return transformPoint(momentOf(c, t).frame, characterKinds().at(c.kind).chest * c.scale); }
	static int seedOf(const std::string &name) {
		std::uint32_t h = 2166136261u;
		for (char ch : name) h = (h ^ static_cast<unsigned char>(ch)) * 16777619u;
		return static_cast<int>(h % 100000u);
	}
	std::size_t shotAt(double t) const {
		std::size_t k = 0;
		for (std::size_t i = 0; i < spec.shots.size(); ++i)
			if (t >= spec.shots[i].start) k = i;
		return k;
	}
	// What the moment is about (Film::subjectsAt): the characters its shot names, or the one it follows.
	std::vector<Vec3> subjectsAt(double t) const {
		std::vector<Vec3> out;
		if (spec.shots.empty()) return out;
		const Shot &shot = spec.shots[shotAt(t)];
		std::vector<std::string> named = shot.about;
		if (shot.following && std::find(named.begin(), named.end(), shot.follow.who) == named.end()) named.insert(named.begin(), shot.follow.who);
		for (const std::string &who : named)
			if (const Character *c = character(who)) out.push_back(chestOf(*c, t));
		return out;
	}
	// The characters' feet, in the same order at every moment (Film::feetAt).
	std::vector<Foot> feetAt(double t) const {
		std::vector<Foot> out;
		for (const Character &c : spec.characters) {
			const CharacterKind &kind = characterKinds().at(c.kind);
			if (!kind.feet) continue;
			CharacterMoment m = momentOf(c, t);
			lockFeet(c, t, m);
			const std::vector<Foot> f = kind.feet(m);
			out.insert(out.end(), f.begin(), f.end());
		}
		return out;
	}
	// The footfalls of every walk and the gestures' contacts (Film::events), panned and quietened from the camera at each moment.
	std::vector<FilmEvent> events() const {
		std::vector<FilmEvent> out;
		const auto viewAt = [this](double t) {
			Vec3 eye, target;
			double fov = 40.0;
			film_subjects::cameraOf(headerAt(t), eye, target, fov);
			return CameraView{eye, target, fov, 0.0, 0};
		};
		int seed = 0;
		for (const Character &c : spec.characters) {
			for (std::size_t i = 0; i < c.walks.size(); ++i)
				for (const rig::PathWalk::Footfall &f : pathOf(c, i).footfalls(-1e30, 1e30)) {
					FilmEvent e;
					e.t = f.t;
					e.kind = EventKind::Footfall;
					e.seed = ++seed;
					const CameraView v = viewAt(f.t);
					e.pan = panOf(v, f.at);
					e.strength = audibility(length(f.at - v.eye));
					out.push_back(e);
				}
			for (const double t : rig::contactsOf(c.gestures)) {
				FilmEvent e;
				e.t = t;
				e.kind = EventKind::Touch;
				e.seed = ++seed;
				const CameraView v = viewAt(t);
				const Vec3 at = chestOf(c, t);
				e.pan = panOf(v, at);
				e.strength = audibility(length(at - v.eye));
				out.push_back(e);
			}
		}
		// the props' landings (pbrt's coordinates: x mirrored for the story-space camera)
		std::vector<physics::Impact> impacts;
		for (const auto &[block, b] : spec.bounces) {
			const std::vector<physics::Impact> i = physics::impactsOf(b);
			impacts.insert(impacts.end(), i.begin(), i.end());
		}
		for (const auto &[block, p] : spec.topples) {
			const std::vector<physics::Impact> i = physics::impactsOf(p);
			impacts.insert(impacts.end(), i.begin(), i.end());
		}
		for (const physics::Impact &i : impacts) {
			FilmEvent e;
			e.t = i.t;
			e.kind = EventKind::Impact;
			e.seed = ++seed;
			const CameraView v = viewAt(i.t);
			const Vec3 at = story(i.at.x, i.at.y, i.at.z);
			e.pan = panOf(v, at);
			e.strength = std::min(1.0, std::max(0.05, i.speed / 4.0)) * audibility(length(at - v.eye));
			out.push_back(e);
		}
		std::stable_sort(out.begin(), out.end(), [](const FilmEvent &a, const FilmEvent &b) { return a.t < b.t; });
		return out;
	}
	bool hasEvents() const { return !spec.characters.empty() || !spec.bounces.empty() || !spec.topples.empty(); }

	// ---- what the characters say ----
	// A character's mouth and voice at t from its lines.
	void speakingAt(const Character &c, double t, CharacterMoment &m) const {
		for (std::size_t i = 0; i < spec.lines.size(); ++i) {
			if (spec.lines[i].who != c.name || t < spec.lines[i].t - 0.1 || t > speech::endOf(spoken[i], spec.lines[i].t) + 0.1) continue;
			const speech::Mouth mouth = speech::mouthAt(spoken[i], t);
			m.mouth.open = std::max(m.mouth.open, mouth.open);
			m.mouth.wide = std::max(m.mouth.wide, mouth.wide);
			m.mouth.round = std::max(m.mouth.round, mouth.round);
			m.voice = std::max(m.voice, speech::loudnessAt(spoken[i], t));
		}
	}
	// The voice a character speaks in: its kind's, its pitch moved by up to 1.5 semitones either way by its name.
	speech::Voice voiceOf(const Character &c) const {
		speech::Voice v = characterKinds().at(c.kind).voice;
		v.pitch *= std::pow(2.0, (static_cast<double>(seedOf(c.name) % 1000) / 999.0 * 3.0 - 1.5) / 12.0) / std::sqrt(c.scale);   // (a bigger one is lower)
		return v;
	}
	// The lines spoken on a mix: each panned and quietened from where the camera is when it starts.
	void speak(audio::Mix &mix) const {
		for (std::size_t i = 0; i < spec.lines.size(); ++i) {
			const Character &c = *character(spec.lines[i].who);
			const double t0 = spec.lines[i].t;
			Vec3 eye, target;
			double fov = 40.0;
			film_subjects::cameraOf(headerAt(t0), eye, target, fov);
			const CameraView view{eye, target, fov, 0.0, 0};
			const Vec3 at = chestOf(c, t0);
			mix.put(t0, speech::speak(spoken[i], t0, voiceOf(c), static_cast<unsigned>(i + 1)), 0.55 * std::max(0.35, audibility(length(at - eye))), panOf(view, at) * 0.6, 0.25);
		}
	}
	// The captions of the lines (those not marked nocaption): each from its start to a little after its end, cut short where the next begins.
	std::vector<Caption> lineCaptions() const {
		std::vector<Caption> out;
		std::vector<std::size_t> order;
		for (std::size_t i = 0; i < spec.lines.size(); ++i)
			if (spec.lines[i].caption) order.push_back(i);
		std::stable_sort(order.begin(), order.end(), [this](std::size_t a, std::size_t b) { return spec.lines[a].t < spec.lines[b].t; });
		for (std::size_t k = 0; k < order.size(); ++k) {
			const speech::Line &l = spec.lines[order[k]];
			Caption cap;
			cap.t0 = l.t;
			cap.t1 = speech::endOf(spoken[order[k]], l.t) + 0.6;
			if (k + 1 < order.size()) cap.t1 = std::min(cap.t1, spec.lines[order[k + 1]].t - 0.05);
			cap.t1 = std::max(cap.t1, cap.t0 + 0.5);
			cap.fade = 0.15;
			cap.centreY = 0.88;
			cap.width = 0.72;
			CaptionLine text;
			text.text = l.text;
			text.size = 0.042;
			cap.lines.push_back(text);
			out.push_back(cap);
		}
		return out;
	}
	// (film_events.h's panOf and audibility, here so that this header does not need the sound library)
	static double panOf(const CameraView &view, Vec3 at) {
		const Vec3 f = normalize(view.target - view.eye), right = normalize(cross(f, Vec3{0.0, 1.0, 0.0}));
		const Vec3 d = at - view.eye;
		const double depth = dot(d, f);
		if (depth <= 1e-6) return 0.0;
		return std::max(-1.0, std::min(1.0, dot(d, right) / (depth * std::tan(view.fov * kPi / 360.0) * 16.0 / 9.0))) * 0.8;
	}
	static double audibility(double distance) { return distance <= 3.0 ? 1.0 : std::max(0.1, std::pow(3.0 / distance, 0.7)); }

	std::vector<double> shotStarts() const {
		std::vector<double> v;
		for (const Shot &s : spec.shots) v.push_back(s.start);
		if (v.empty()) v.push_back(0.0);
		return v;
	}
	std::vector<std::string> shotNameList() const {
		std::vector<std::string> out;
		for (const Shot &s : spec.shots) out.push_back(s.name);
		return out;
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
		for (const Character &c : spec.characters) {   // the characters, as their kinds draw them
			const CharacterKind &kind = characterKinds().at(c.kind);
			if (!kind.text) continue;
			CharacterMoment m = momentOf(c, t);
			m.trail = trailOf(c, t, m);
			lockFeet(c, t, m);
			m.voice = music_cues::voiceAt(spec.key, c.name, t, shotStarts(), shotNameList());
			speakingAt(c, t, m);
			m.face = faces::faceAt(c.feelings, t);
			out += "# " + c.name + " (" + c.kind + ")\n" + kind.text(m);
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
	// The transforms of a block at t; with a shutter, a moving block's at the shutter's opening (StartTime) and closing (EndTime) - pbrt's object motion blur, which
	// the CPU and the GPU both render (gpu: a two-key triangle GAS, moving spheres).
	std::string transformsFor(const std::string &name, double t) const {
		const std::string open = transformsAt(name, t);
		if (spec.shutter <= 0.0) return open;
		const std::string close = transformsAt(name, t + spec.shutter / std::max(1, spec.fps));
		if (close == open) return open;
		return "  ActiveTransform \"StartTime\"\n" + open + "  ActiveTransform \"EndTime\"\n" + close + "  ActiveTransform \"All\"\n";
	}
	std::string transformsAt(const std::string &name, double t) const {
		std::string out;
		for (const auto &[block, b] : spec.bounces) {   // where its physics has it: moved from where the scene has it, and turned as it rolls
			if (block != name) continue;
			const physics::Body body = physics::at(b, t);
			out += "  Translate " + num(body.at.x) + " " + num(body.at.y) + " " + num(body.at.z) + "\n  Rotate " + num(body.spin * 180.0 / kPi) + " " + num(body.spinAxis.x) + " " +
				   num(body.spinAxis.y) + " " + num(body.spinAxis.z) + "\n  Translate " + num(-b.from.x) + " " + num(-b.from.y) + " " + num(-b.from.z) + "\n";
		}
		for (const auto &[block, p] : spec.topples) {
			if (block != name) continue;
			const Vec3 a = normalize(p.axis);
			out += "  Translate " + num(p.pivot.x) + " " + num(p.pivot.y) + " " + num(p.pivot.z) + "\n  Rotate " + num(physics::angleOf(p, t)) + " " + num(a.x) + " " + num(a.y) + " " + num(a.z) +
				   "\n  Translate " + num(-p.pivot.x) + " " + num(-p.pivot.y) + " " + num(-p.pivot.z) + "\n";
		}
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
		camera_keyframes::Key key;
		if (shot.following) {
			// behind the character, at a height above its feet, to its right, looking at its chest (story space, then pbrt's: x mirrored)
			const Character &c = *character(shot.follow.who);
			Vec3 feet;
			const CharacterMoment m = momentOf(c, t, &feet);
			const Vec3 forward = normalize(transformPoint(m.frame, {0.0, 0.0, 1.0}) - feet), up{0.0, 1.0, 0.0}, right = cross(forward, up);
			const Vec3 eye = feet - forward * shot.follow.behind + right * shot.follow.side + up * shot.follow.up, target = transformPoint(m.frame, characterKinds().at(c.kind).chest * c.scale);
			key.pos[0] = -eye.x;
			key.pos[1] = eye.y;
			key.pos[2] = eye.z;
			key.target[0] = -target.x;
			key.target[1] = target.y;
			key.target[2] = target.z;
			key.fov = shot.follow.fov;
		} else {
			key = camera_keyframes::at(shot.path, std::min(1.0, std::max(0.0, (t - shot.start) / span)), span);
		}
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
