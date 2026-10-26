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
//       dissolve 0.8                        # how this shot comes in: `dissolve`, `fade` (to black and up) or `wipe`, over so many seconds centred on its cut (default: a cut)
//     fadeout  1.5                          # the film ends with a fade to black over its last 1.5 s (a `fade` in its first shot fades it up from black)
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
//     character  ann person  sweater red  hair blond            # ... and the colours of its parts (each kind's: --film-check lists them): a name, r,g,b (0 to 1) or #rrggbb
//     walk       kai  2.0 0.7   0 -2   3 -6                     # NAME, from 2 s at 0.7 m/s, through these points (x z) from where it is
//     walk       kai  9.0 0.7   to 3 -6                         # ... or to a place, finding its own way round the set and whoever stands there (film_paths.h)
//     gesture    kai  "look up" 8.0 9.0 11.0 11.5               # NAME, a pose of its library, eased in from 8 s to 9 s, out from 11 s to 11.5 s (no out times: held)
//     run        kai  4.0 3.0   2 -6                            # as `walk` (points, or `to` a place), running: a longer stride, a lean
//     jump       kai  7.0 to 3 -7  height 0.5                   # NAME, at 7 s, a jump to there: a crouch before it, an arc, a landing that squashes and is heard
//     sit        kai  9.0 height 0.45 until 14                  # NAME sits down at 9 s on a seat that high (0: the ground) and stands up at 14 s
//     gesture    kai  "reach to the wall" 12 13 contact 12.8    # contact: the hand meets something (a touch on the soundtrack)
//     say        kai  14.4 "Look, the first star."  rate 1.1  pitch 2   # NAME, a line it says from 14.4 s: its voice, its mouth and a caption (film_speech.h; `nocaption`)
//     feel       pip  happy 3 3.5 6 6.8  amount 0.8             # NAME, an expression on its face eased in and out (film_faces.h: happy, sad, angry, surprised, ...)
//     look       pip  at ann 2 2.5 8 8.5                        # NAME turns its head and eyes to someone, a prop or a point (`at X Y Z`) and back
//     crowd      folk person 12 in -8 -12 8 -4  seed 3          # NAME KIND COUNT in X0 Z0 X1 Z1: extras of varied looks that wander, pause and turn, each in a cell of its
//                                                               # own (`from T`, `to T`: when they wander); not in the crowding check or an `auto` shot unless named
//     idle       off                                            # (a film line) no idle life: by default a character breathes, shifts its weight, darts its eyes,
//                                                               # turns to whoever speaks and looks at the one it speaks to (film_kinds.h; a `look` line wins)
//
// Props that characters handle - a marked block that is picked up, carried, put down and handed over (it follows the hand that holds it):
//
//     prop       cup  at 0.6 0.82 -1.0                          # a block (`# @anim cup`) a character can pick up: where its handle is in the scene
//     pick       ann  cup 3.0  left                             # NAME reaches for it and takes it at 3.0 s (its usual hand, or `left` / `right`)
//     put        ann  cup 7.5  at 1.2 0.8 -1.5                  # puts it down there at 7.5 s
//     give       ann  cup 9.0  to pip                           # hands it to pip at 9.0 s (their hands meet half way)
//
//     shot 0 Walking
//       follow   kai  behind 3  up 1.7  side 0.6  fov 35          # instead of `key` lines: the camera behind NAME (metres), at a height, to its right, looking at its chest
//     shot 9 Looking up
//       key ...
//       subject  kai                                            # what the shot is about (the film doctor's framing checks); a follow shot is about whom it follows
//     shot 10 Around her
//       orbit    ann  radius 3  degrees 120  height 1.6        # instead of `key` lines, a move about a character: orbit (round them), crane (rise in front of them:
//                                                               # from H1 to H2, distance D), push (in towards them: from D1 to D2), track (beside them as they go: side S)
//       handheld 1                                              # (any shot) a gentle shake, as a hand-held camera
//       focus    ann  4.5 pip                                   # (any shot) the focus on ann, pulled to pip at 4.5 s (`aperture R`, 0.02 when not given)
//     shot 12 Talking
//       auto     pip ann                                        # instead of `key` lines: shots chosen from what happens - a two-shot, over-the-shoulder shots of
//       side     left                                           # each speaker as they speak - all on one side of the line between the first two (`left`: the first on the left)
//
// A film with characters reports its subjects, its footfalls and gesture contacts (Film::events: footsteps on the soundtrack) and its characters' feet (the sliding-feet check).
//
// Props with physics (prop_physics.h) - a marked block that is dropped or thrown and bounces and rolls, or that topples over; each landing is heard (an impact):
//
//     bounce   pebble 6.0  from 0.6 2.4 -3   velocity 0.4 0 -0.2   radius 0.06   # from 6 s: its centre at `from` (where the scene has it), thrown at `velocity`
//     topple   tablet 9.2  pivot 1.4 0 -3.6   axis 0 0 1   height 0.6  depth 0.08   # from 9.2 s it tips over its bottom edge through `pivot` (right-hand turn about `axis`)
//
// (bounce also takes restitution, grip and rolling; topple push and lean: prop_physics.h.)
//
// Dressing the set (film_dressing.h) - things from the nature library on the ground, a sun and sky that follow the time of day, and the set's sound:
//
//     place    tree  at 3 0 -6  scale 1.2  turn 40              # a tree, rock, fern, bush or grass tuft, its foot there (pbrt's x y z)
//     scatter  grass 300 in -8 -10 8 2  scale 0.7 1.3  seed 2   # so many of a kind over a rectangle of the ground (x0 z0 x1 z1; `y` its height)
//     sky      18:30 19:10  south 180  latitude 40              # the sun and sky from 6:30 to 7:10 pm over the film (the scene's own infinite and distant lights are left out)
//     ambience birds 0.6                                        # a sound under the whole film: wind, birds, crickets, water or night, at a gain
//
// Weather and particles (film_particles.h):
//
//     weather   rain 0.8 from 4 to 14                          # rain, snow or leaves over the set round the camera (amount 1 a good shower; eased in and out; `ground Y`)
//     particles sparks at 1 0.9 -2  3 5  rate 80               # sparks, dust or a splash thrown from a place from 3 s to 5 s, so many a second (`speed S`)
//     burst     splash at 0 0 -1  6.2  count 60                # all at once at 6.2 s

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
#include "film_dressing.h"
#include "film_faces.h"
#include "film_particles.h"
#include "film_paths.h"
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
	bool coverage = false;            // `auto`: chosen from what happens, when the film is loaded (it becomes shots of its own)
	std::vector<std::string> cover;   // (auto) the characters it covers (none named: those who speak in it, or everyone)
	int side = 0;                     // (auto) 1: the first of them on the left of the frame, -1: on the right, 0: the side their faces look to
	Transition transition;            // how it comes in (length 0: a cut)
	// (a move about a character: `orbit`, `crane`, `push`, `track`) and what any shot may add: a hand-held shake, a focus pulled between characters
	struct Move {
		enum Kind { None, Orbit, Crane, Push, Track } kind = None;
		std::string who;
		double radius = 3.0, height = 1.6, degrees = 90.0, from = 0.0;   // orbit
		double low = 0.8, high = 4.0, distance = 4.0;                   // crane (low, high: heights; distance in front)
		double near0 = 5.0, near1 = 2.0;                                // push: from and to (distances)
		double side = 3.0;                                              // track
		double fov = 0.0;
	} move;
	double handheld = 0.0;
	std::vector<std::pair<double, std::string>> focus;   // who is in focus from when (the first from the shot's start)
	double aperture = 0.0;
};

// A character a film file brings in (`character NAME KIND ...`), its walks (`walk`) and its gestures (`gesture`).
struct WalkLine {
	enum Style { Walk, Run, Jump } style = Walk;
	double height = 0.5;   // (a jump) how high it rises
	double t0 = 0.0, speed = 0.8;
	std::vector<std::pair<double, double>> points;   // pbrt x z (for a walk that finds its way: the way found, when the film is loaded)
	std::vector<std::pair<double, double>> targets;  // (`to X Z ...`) the places it goes to, finding its way
	std::vector<double> heights;                     // the floor's height at each point (from the set: uneven ground), or none (the character's own height)
	int line = 0;
};
struct Character {
	std::string name, kind;
	double start[3] = {0.0, 0.0, 0.0};   // pbrt's coordinates
	double facing = 0.0;                 // degrees: 0 faces +z, 90 faces +x (pbrt's)
	double detail = 0.0;                 // the kind's sculpting cell (0: its own)
	double scale = 1.0;                  // its size (a kind that can be scaled: a child Pip 0.6, a turtle as big as a dog 6)
	std::map<std::string, Vec3> colours; // the colours of its parts (`sweater red`): the kind's names for them
	bool extra = false;                  // one of a crowd (`crowd`): in the background
	std::vector<WalkLine> walks;
	std::vector<rig::Gesture> gestures;
	std::vector<faces::Feeling> feelings;   // its expressions (`feel`)
	struct Sit {
		double t0 = 0.0, t1 = 1e30, height = 0.0;
	};
	std::vector<Sit> sits;                  // when it sits down, on what height, and stands up (`sit`)
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
	const std::map<std::string, Vec3> *colours = nullptr;   // the colours of its parts (Character::colours)
	// where its hands reach (0 the left, 1 the right) and where it looks, in the world (story space), each with how far it is there (0..1): a prop it picks up, carries, puts
	// down or hands over, someone it looks at
	struct Reach {
		Vec3 at;
		double weight = 0.0;
	};
	Reach hands[2];
	Reach look;
	bool idle = false;   // its idle life: breathing, a shifting weight, darting eyes (the film's `idle`, on unless it says off)
	double running = 0.0;    // 1 while it runs (a longer stride: its gait 1.6 times as wide), 0 walking
	double crouch = 0.0;     // 0..1: crouched before a jump, squashed by a landing
	bool airborne = false;   // in the air in a jump
	double sit = 0.0;        // 0 standing .. 1 sitting, on a seat `seat` high
	double seat = 0.0;
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
	// (for props) where a hand of it is at a moment, in the world (its arm's reach decides: a prop held follows it), where it carries a prop (body space, scale 1, the left
	// hand's: the right's is mirrored) and which hand it uses (0 the left, 1 the right); (for `look`) its head, body space at scale 1
	std::function<Vec3(const CharacterMoment &, int)> handAt;
	Vec3 carry{0.25, 1.0, 0.35};
	int hand = 1;
	Vec3 head{0.0, 1.6, 0.05};
	// its size on the floor and its height (scale 1): what is in its way when it finds its way, and how close another may come
	double radius = 0.3, height = 1.8;
	std::vector<std::string> parts;   // the parts whose colour a character line can set (`sweater red`)
};

// A colour as a film file writes it: a name (the cartoon palette below), r,g,b from 0 to 1, or #rrggbb. False when it is none of those.
inline bool colourOf(const std::string &w, Vec3 &out) {
	static const std::map<std::string, Vec3> names = {
		{"red", {0.78, 0.12, 0.10}},     {"orange", {0.90, 0.45, 0.10}}, {"yellow", {0.92, 0.78, 0.18}}, {"green", {0.22, 0.58, 0.20}},  {"teal", {0.10, 0.52, 0.52}},
		{"blue", {0.15, 0.35, 0.78}},    {"navy", {0.08, 0.12, 0.35}},   {"purple", {0.45, 0.20, 0.62}}, {"pink", {0.92, 0.55, 0.65}},   {"brown", {0.40, 0.24, 0.12}},
		{"black", {0.04, 0.04, 0.045}},  {"white", {0.92, 0.92, 0.90}},  {"grey", {0.45, 0.45, 0.46}},  {"gray", {0.45, 0.45, 0.46}},   {"cream", {0.93, 0.86, 0.70}},
		{"blond", {0.85, 0.68, 0.36}},   {"ginger", {0.72, 0.32, 0.10}}, {"tan", {0.80, 0.62, 0.45}},    {"olive", {0.42, 0.42, 0.16}},  {"lilac", {0.70, 0.60, 0.85}},
		{"mint", {0.60, 0.88, 0.70}},    {"sky", {0.55, 0.78, 0.95}},    {"gold", {0.85, 0.65, 0.20}},   {"silver", {0.75, 0.76, 0.78}}, {"crimson", {0.60, 0.05, 0.12}},
		{"pale", {0.93, 0.80, 0.70}},    {"dark", {0.36, 0.22, 0.14}},   {"lime", {0.55, 0.85, 0.20}},   {"peach", {0.98, 0.72, 0.55}},  {"denim", {0.25, 0.38, 0.58}}};
	if (const auto it = names.find(w); it != names.end()) {
		out = it->second;
		return true;
	}
	if (w.size() == 7 && w[0] == '#') {
		unsigned v = 0;
		for (std::size_t i = 1; i < 7; ++i) {
			const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(w[i])));
			if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
			v = v * 16 + static_cast<unsigned>(std::isdigit(static_cast<unsigned char>(c)) ? c - '0' : c - 'a' + 10);
		}
		const auto lin = [](unsigned b) { const double c = b / 255.0; return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); };   // (sRGB, as a picker gives it)
		out = {lin((v >> 16) & 255), lin((v >> 8) & 255), lin(v & 255)};
		return true;
	}
	double r = 0, g = 0, b = 0;
	char extra = 0;
	if (std::sscanf(w.c_str(), "%lf,%lf,%lf%c", &r, &g, &b, &extra) == 3 && r >= 0 && g >= 0 && b >= 0 && r <= 1 && g <= 1 && b <= 1) {
		out = {r, g, b};
		return true;
	}
	return false;
}
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
	double fadeOut = 0.0;   // seconds of a fade to black at the end
	bool idle = true;       // the characters' idle life, and turning to whoever speaks (`idle off`)
	std::vector<Shot> shots;
	std::vector<Anim> anims;
	std::vector<Character> characters;
	// props with physics: the block they move, and the line that says so
	std::vector<std::pair<std::string, physics::Bounce>> bounces;
	std::vector<std::pair<std::string, physics::Topple>> topples;
	std::vector<std::pair<std::string, int>> physicsLines;
	std::vector<speech::Line> lines;   // what the characters say (`say`)
	// props characters handle (`prop`), what they do with them (`pick`, `put`, `give`) in time order, and where they look (`look`)
	struct Prop {
		std::string name;
		Vec3 home;   // its handle, where the scene has it (pbrt's)
		int line = 0;
	};
	struct Handling {
		enum Kind { Pick, Put, Give } kind = Pick;
		std::string who, prop, to;
		double t = 0.0;
		int hand = -1;   // -1: the holder's usual (or, for put and give, the one that holds it)
		Vec3 at;         // put: where (pbrt's)
		int line = 0;
	};
	struct Look {
		std::string who, target;   // target: a character, a prop, or "" for `point`
		Vec3 point;                // (pbrt's)
		rig::Gesture when;
		int line = 0;
	};
	std::vector<Prop> props;
	std::vector<Handling> handling;
	std::vector<Look> looks;
	// the set's dressing (`place`, `scatter`), its time of day (`sky`) and its sound (`ambience`)
	std::vector<dressing::Placement> placements;
	bool hasSky = false;
	dressing::Sky sky;
	std::vector<dressing::Ambience> ambience;
	// weather and particles (`weather`, `particles`, `burst`)
	std::vector<particles::Weather> weather;
	std::vector<particles::Emitter> emitters;
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

// A crowd's extras (`crowd`): `count` characters of a kind named NAME-1, NAME-2 ..., each in a cell of its own of the rectangle (so they never meet), with looks of their own
// (colours from a palette of the kind's parts, a size for a kind that can be scaled), wandering from `from` to `until`: a pause, a walk that finds its way to another place in
// its cell, a pause ... - all from `seed`, the same every time.
inline std::vector<Character> crowdOf(const std::string &name, const std::string &kindName, int count, double x0, double z0, double x1, double z1, int seed, double from, double until, int line) {
	std::vector<Character> out;
	const auto kindIt = characterKinds().find(kindName);
	const double radius = kindIt == characterKinds().end() ? 0.4 : kindIt->second.radius;
	const bool scalable = kindIt != characterKinds().end() && kindIt->second.scalable;
	const double w = std::max(0.1, x1 - x0), d = std::max(0.1, z1 - z0);
	const int cols = std::max(1, static_cast<int>(std::ceil(std::sqrt(count * w / d)))), rows = (count + cols - 1) / cols;
	const double cw = w / cols, cd = d / rows;
	std::vector<int> cells(static_cast<std::size_t>(cols * rows));
	for (std::size_t i = 0; i < cells.size(); ++i) cells[i] = static_cast<int>(i);
	for (std::size_t i = cells.size(); i > 1; --i) std::swap(cells[i - 1], cells[static_cast<std::size_t>(hash01(seed * 7 + static_cast<int>(i), 991) * i)]);   // (which cells, shuffled)
	static const char *tones[] = {"red", "orange", "yellow", "green", "teal", "blue", "navy", "purple", "pink", "brown", "cream", "olive", "lilac", "mint", "sky", "crimson", "peach", "denim"};
	static const char *hair[] = {"black", "brown", "dark", "blond", "ginger", "grey"};
	static const char *skin[] = {"pale", "peach", "tan", "brown", "dark"};
	const auto pick = [&](const char *const *list, int n, int i, int k) { Vec3 c; colourOf(list[static_cast<int>(hash01(seed * 131 + i, k) * n) % n], c); return c; };
	for (int i = 0; i < count; ++i) {
		Character c;
		c.name = name + "-" + std::to_string(i + 1);
		c.kind = kindName;
		c.line = line;
		c.extra = true;
		const int cell = cells[static_cast<std::size_t>(i)];
		const double cx0 = x0 + (cell % cols) * cw, cz0 = z0 + (cell / cols) * cd;
		const double scale = scalable ? (kindName == "turtle" ? 4.0 + 2.0 * hash01(seed * 17 + i, 5) : 0.7 + 0.35 * hash01(seed * 17 + i, 5)) : 1.0;
		c.scale = scale;
		const double margin = std::min(0.45 * std::min(cw, cd), radius * scale + 0.25);
		const auto spot = [&](int k) {   // a place in its cell, clear of the cell's edges by its size
			return std::make_pair(cx0 + margin + (cw - 2.0 * margin) * hash01(seed * 37 + i * 11 + k, 1), cz0 + margin + (cd - 2.0 * margin) * hash01(seed * 37 + i * 11 + k, 2));
		};
		const auto [sx, sz] = spot(0);
		c.start[0] = sx, c.start[1] = 0.0, c.start[2] = sz;
		c.facing = 360.0 * hash01(seed * 41 + i, 3);
		if (kindName == "person") {
			c.colours["sweater"] = pick(tones, 18, i, 11);
			c.colours["trousers"] = pick(tones, 18, i, 12) * 0.6;
			c.colours["hair"] = pick(hair, 6, i, 13);
			c.colours["skin"] = pick(skin, 5, i, 14);
		} else if (kindName == "pip") {
			c.colours["skin"] = pick(tones, 18, i, 11);
		} else if (kindName == "turtle") {
			c.colours["shell"] = pick(tones, 18, i, 11);
		}
		// wandering: pauses and walks to places in its cell, a walk's time allowed generously (it finds its own way)
		double t = from + 3.0 * hash01(seed * 43 + i, 4);
		const double speed = (0.55 + 0.4 * hash01(seed * 47 + i, 6)) * (kindName == "person" ? 1.0 : 0.6) * std::sqrt(scale);
		double px = sx, pz = sz;
		for (int k = 1; t < std::min(until, 1e4) - 1.0 && k < 400; ++k) {
			const auto [tx, tz] = spot(k);
			const double dist = std::hypot(tx - px, tz - pz);
			if (dist > 0.3) {
				WalkLine wl;
				wl.t0 = t;
				wl.speed = speed;
				wl.targets.push_back({tx, tz});
				wl.line = line;
				c.walks.push_back(wl);
				t += dist / speed * 1.6 + 0.5;
				px = tx, pz = tz;
			}
			t += 1.5 + 3.5 * hash01(seed * 53 + i * 13 + k, 7);   // (a pause)
		}
		out.push_back(c);
	}
	return out;
}

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
		if (s.shots.back().move.kind != Shot::Move::None) {
			if (!shotText.empty() || s.shots.back().following || s.shots.back().coverage) return "the shot \"" + s.shots.back().name + "\" has a move and `key`, `follow` or `auto` lines too";
			return std::string();
		}
		if (s.shots.back().following || s.shots.back().coverage) {
			if (!shotText.empty()) return "the shot \"" + s.shots.back().name + "\" has both `key` lines and `" + (s.shots.back().following ? "follow" : "auto") + "`";
			if (s.shots.back().following && s.shots.back().coverage) return "the shot \"" + s.shots.back().name + "\" has both `follow` and `auto`";
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
		for (std::size_t hash = line.find('#'); hash != std::string::npos; hash = line.find('#', hash + 1)) {   // '#' starts a comment - but not a colour, #rrggbb
			bool colour = hash + 7 <= line.size() && (hash + 7 == line.size() || std::isspace(static_cast<unsigned char>(line[hash + 7])));
			for (std::size_t k = hash + 1; colour && k < hash + 7; ++k) colour = std::isxdigit(static_cast<unsigned char>(line[k])) != 0;
			if (!colour) {
				line.erase(hash);
				break;
			}
		}
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
		} else if (verb == "crowd") {
			const std::vector<std::string> t = detail::tokens(line);
			double count = 0.0, x0 = 0.0, z0 = 0.0, x1 = 0.0, z1 = 0.0;
			if (t.size() < 9 || !number(t[3], count) || count < 1 || count > 200 || t[4] != "in" || !number(t[5], x0) || !number(t[6], z0) || !number(t[7], x1) || !number(t[8], z1))
				return fail("`crowd NAME KIND COUNT in X0 Z0 X1 Z1 [seed S] [from T] [to T]` (1 to 200 of them)");
			double seed = 1.0, from = 0.0, until = 1e30;
			for (std::size_t i = 9; i < t.size(); i += 2) {
				double v = 0.0;
				if (i + 1 >= t.size() || !number(t[i + 1], v)) return fail("a crowd takes seed, from and to, each with a number");
				if (t[i] == "seed") seed = v;
				else if (t[i] == "from") from = v;
				else if (t[i] == "to") until = v;
				else return fail("\"" + t[i] + "\": a crowd takes seed, from and to");
			}
			const std::vector<Character> folk = crowdOf(t[1], t[2], static_cast<int>(count), std::min(x0, x1), std::min(z0, z1), std::max(x0, x1), std::max(z0, z1), static_cast<int>(seed), from, until, n);
			for (const Character &c : folk)
				for (const Character &other : s.characters)
					if (other.name == c.name) return fail("there is already a character called " + c.name);
			s.characters.insert(s.characters.end(), folk.begin(), folk.end());
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
				} else if (Vec3 colour; i + 1 < t.size() && colourOf(t[i + 1], colour)) {   // a part's colour (the kind's parts are checked when the film is loaded)
					c.colours[t[i]] = colour;
					i += 2;
				} else {
					return fail("\"" + t[i] + "\": a character takes `at X Y Z`, `facing DEGREES`, `scale S`, `detail CELL` and its parts' colours (`sweater red`, `hair 0.2,0.1,0.05`, `skin #f0c8a0`)");
				}
			}
			s.characters.push_back(c);
		} else if (verb == "walk" || verb == "run" || verb == "jump" || verb == "sit" || verb == "gesture" || verb == "feel") {
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
			if (verb == "jump") {
				WalkLine wl;
				wl.line = n;
				wl.style = WalkLine::Jump;
				double x = 0.0, z = 0.0;
				if (t.size() < 6 || !number(t[2], wl.t0) || t[3] != "to" || !number(t[4], x) || !number(t[5], z)) return fail("`jump NAME T to X Z [height H]`");
				if (t.size() == 8 && t[6] == "height" && number(t[7], wl.height) && wl.height > 0.0 && wl.height <= 5.0) {
				} else if (t.size() != 6) {
					return fail("`jump NAME T to X Z [height H]` (a height above 0, up to 5 m)");
				}
				wl.points.push_back({x, z});
				if (!c->walks.empty() && wl.t0 < c->walks.back().t0) return fail("a character's walks, runs and jumps go in time order");
				c->walks.push_back(wl);
				continue;
			}
			if (verb == "sit") {
				Character::Sit st;
				if (t.size() < 3 || !number(t[2], st.t0)) return fail("`sit NAME T [height H] [until T]`");
				for (std::size_t i = 3; i < t.size(); i += 2) {
					double v = 0.0;
					if (i + 1 >= t.size() || !number(t[i + 1], v)) return fail("a sit takes height and until, each with a number");
					if (t[i] == "height" && v >= 0.0 && v <= 1.5) st.height = v;
					else if (t[i] == "until" && v > st.t0 + 0.7) st.t1 = v;
					else return fail("\"" + t[i] + "\": a sit takes height (0 to 1.5 m) and until (later than it sits down, by 0.7 s at least)");
				}
				c->sits.push_back(st);
				continue;
			}
			if (verb == "walk" || verb == "run") {
				WalkLine wl;
				wl.line = n;
				if (verb == "run") wl.style = WalkLine::Run;
				if (t.size() > 4 && t[4] == "to") {   // places to go to, finding its way
					if (!number(t[2], wl.t0) || !number(t[3], wl.speed) || wl.speed <= 0.0 || (t.size() - 4) % 3 != 0) return fail("`walk NAME FROM SPEED to X Z [to X Z ...]`");
					for (std::size_t i = 4; i + 2 < t.size(); i += 3) {
						double x = 0, z = 0;
						if (t[i] != "to" || !number(t[i + 1], x) || !number(t[i + 2], z)) return fail("`walk NAME FROM SPEED to X Z [to X Z ...]`");
						wl.targets.push_back({x, z});
					}
				} else {
					if (t.size() < 6 || (t.size() - 4) % 2 != 0 || !number(t[2], wl.t0) || !number(t[3], wl.speed) || wl.speed <= 0.0)
						return fail("`walk NAME FROM SPEED X Z [X Z ...]` (or `to X Z`): a start time, a speed above 0 and points");
					for (std::size_t i = 4; i + 1 < t.size(); i += 2) {
						double x = 0, z = 0;
						if (!number(t[i], x) || !number(t[i + 1], z)) return fail("a walk's points are pairs of numbers (x z)");
						wl.points.push_back({x, z});
					}
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
		} else if (verb == "place" || verb == "scatter") {
			const std::vector<std::string> t = detail::tokens(line);
			const auto known = [&](const std::string &k) { return std::find(dressing::kinds().begin(), dressing::kinds().end(), k) != dressing::kinds().end(); };
			if (t.size() < 2 || !known(t[1])) return fail("`" + verb + "` takes a kind: tree, rock, fern, bush or grass");
			if (verb == "place") {
				dressing::Placement p;
				p.kind = t[1];
				if (t.size() < 6 || t[2] != "at" || !number(t[3], p.at.x) || !number(t[4], p.at.y) || !number(t[5], p.at.z)) return fail("`place KIND at X Y Z [scale S] [turn DEGREES] [seed N]`");
				for (std::size_t i = 6; i < t.size(); i += 2) {
					double v = 0.0;
					if (i + 1 >= t.size() || !number(t[i + 1], v) || (t[i] != "scale" && t[i] != "turn" && t[i] != "seed")) return fail("a place takes scale, turn and seed");
					if (t[i] == "scale") p.scale = v;
					else if (t[i] == "turn") p.turn = v;
					else p.seed = static_cast<int>(v);
				}
				s.placements.push_back(p);
			} else {
				dressing::Scatter sc;
				sc.kind = t[1];
				double count = 0.0;
				if (t.size() < 8 || !number(t[2], count) || count < 1 || count > 20000 || t[3] != "in" || !number(t[4], sc.x0) || !number(t[5], sc.z0) || !number(t[6], sc.x1) || !number(t[7], sc.z1))
					return fail("`scatter KIND COUNT in X0 Z0 X1 Z1 [y Y] [seed N] [scale A B]` (1 to 20000 of them)");
				sc.count = static_cast<int>(count);
				for (std::size_t i = 8; i < t.size();) {
					double v = 0.0;
					if (t[i] == "scale" && i + 2 < t.size() && number(t[i + 1], sc.scale0) && number(t[i + 2], sc.scale1)) {
						i += 3;
					} else if ((t[i] == "y" || t[i] == "seed") && i + 1 < t.size() && number(t[i + 1], v)) {
						if (t[i] == "y") sc.y = v;
						else sc.seed = static_cast<int>(v);
						i += 2;
					} else {
						return fail("\"" + t[i] + "\": a scatter takes y, seed and scale A B");
					}
				}
				const std::vector<dressing::Placement> many = dressing::placementsOf(sc);
				s.placements.insert(s.placements.end(), many.begin(), many.end());
			}
		} else if (verb == "sky") {
			const std::vector<std::string> t = detail::tokens(line);
			const auto hour = [&](const std::string &w, double &h) {   // 18:30 or 18.5
				const std::size_t colon = w.find(':');
				double a = 0.0, b = 0.0;
				if (colon == std::string::npos) return number(w, h) && h >= 0.0 && h <= 24.0;
				if (!number(w.substr(0, colon), a) || !number(w.substr(colon + 1), b) || a < 0 || a > 24 || b < 0 || b >= 60) return false;
				h = a + b / 60.0;
				return true;
			};
			if (t.size() < 3 || !hour(t[1], s.sky.from) || !hour(t[2], s.sky.to)) return fail("`sky FROM TO [south DEGREES] [latitude L] [day N] [haze H]`: times of day as 18:30 or 18.5");
			for (std::size_t i = 3; i < t.size(); i += 2) {
				double v = 0.0;
				if (i + 1 >= t.size() || !number(t[i + 1], v)) return fail("a sky takes south, latitude, day and haze, each with a number");
				if (t[i] == "south") s.sky.south = v;
				else if (t[i] == "latitude" && std::abs(v) <= 89.0) s.sky.latitude = v;
				else if (t[i] == "day" && v >= 1 && v <= 366) s.sky.day = static_cast<int>(v);
				else if (t[i] == "haze" && v >= 1.7 && v <= 10.0) s.sky.haze = v;
				else return fail("\"" + t[i] + "\": a sky takes south, latitude (-89 to 89), day (1 to 366) and haze (1.7 to 10)");
			}
			s.hasSky = true;
		} else if (verb == "weather") {
			const std::vector<std::string> t = detail::tokens(line);
			particles::Weather wt;
			if (t.size() < 2 || std::find(particles::weatherKinds().begin(), particles::weatherKinds().end(), t[1]) == particles::weatherKinds().end())
				return fail("`weather KIND [AMOUNT] [from T] [to T] [ground Y]`: rain, snow or leaves");
			wt.kind = t[1];
			std::size_t i = 2;
			if (i < t.size() && number(t[i], wt.amount)) ++i;
			for (; i < t.size(); i += 2) {
				double v = 0.0;
				if (i + 1 >= t.size() || !number(t[i + 1], v)) return fail("a weather takes from, to and ground, each with a number");
				if (t[i] == "from") wt.t0 = v;
				else if (t[i] == "to") wt.t1 = v;
				else if (t[i] == "ground") wt.ground = v;
				else return fail("\"" + t[i] + "\": a weather takes from, to and ground");
			}
			if (!(wt.amount > 0.0 && wt.amount <= 4.0) || wt.t1 <= wt.t0) return fail("a weather's amount is from 0 to 4 and its end after its start");
			s.weather.push_back(wt);
		} else if (verb == "particles" || verb == "burst") {
			const std::vector<std::string> t = detail::tokens(line);
			particles::Emitter e;
			const bool burst = verb == "burst";
			if (t.size() < 2 || std::find(particles::emitterKinds().begin(), particles::emitterKinds().end(), t[1]) == particles::emitterKinds().end())
				return fail("`" + verb + "` takes a kind: sparks, dust or splash");
			e.kind = t[1];
			const std::size_t need = burst ? 7 : 8;
			if (t.size() < need || t[2] != "at" || !number(t[3], e.at.x) || !number(t[4], e.at.y) || !number(t[5], e.at.z) || !number(t[6], e.t0) || (!burst && (!number(t[7], e.t1) || e.t1 <= e.t0)))
				return fail(burst ? "`burst KIND at X Y Z T [count N] [speed S]`" : "`particles KIND at X Y Z FROM TO [rate R] [speed S]`");
			if (burst) e.count = 40;
			for (std::size_t i = need; i < t.size(); i += 2) {
				double v = 0.0;
				if (i + 1 >= t.size() || !number(t[i + 1], v) || v <= 0.0) return fail("\"" + t[i] + "\" takes a number above 0");
				if (t[i] == "speed") e.speed = v;
				else if (t[i] == "rate" && !burst && v <= 1000.0) e.rate = v;
				else if (t[i] == "count" && burst && v <= 2000.0) e.count = static_cast<int>(v);
				else return fail("\"" + t[i] + "\": " + (burst ? "a burst takes count (up to 2000) and speed" : "particles take rate (up to 1000 a second) and speed"));
			}
			s.emitters.push_back(e);
		} else if (verb == "ambience") {
			const std::vector<std::string> t = detail::tokens(line);
			dressing::Ambience a;
			if (t.size() < 2 || std::find(dressing::ambienceKinds().begin(), dressing::ambienceKinds().end(), t[1]) == dressing::ambienceKinds().end())
				return fail("`ambience KIND [GAIN]`: wind, birds, crickets, water or night");
			a.kind = t[1];
			if (t.size() > 3 || (t.size() == 3 && (!number(t[2], a.gain) || a.gain < 0.0 || a.gain > 4.0))) return fail("an ambience's gain is a number from 0 to 4");
			s.ambience.push_back(a);
		} else if (verb == "prop") {
			const std::vector<std::string> t = detail::tokens(line);
			Spec::Prop pr;
			pr.line = n;
			if (t.size() != 6 || t[2] != "at" || !number(t[3], pr.home.x) || !number(t[4], pr.home.y) || !number(t[5], pr.home.z)) return fail("`prop NAME at X Y Z`: a marked block and where its handle is");
			pr.name = t[1];
			for (const Spec::Prop &other : s.props)
				if (other.name == pr.name) return fail("there is already a prop called " + pr.name);
			s.props.push_back(pr);
		} else if (verb == "pick" || verb == "put" || verb == "give") {
			const std::vector<std::string> t = detail::tokens(line);
			Spec::Handling h;
			h.line = n;
			h.kind = verb == "pick" ? Spec::Handling::Pick : verb == "put" ? Spec::Handling::Put : Spec::Handling::Give;
			if (t.size() < 4 || !number(t[3], h.t)) return fail("`" + verb + " NAME PROP T ...`: who, which prop and when");
			h.who = t[1];
			h.prop = t[2];
			std::size_t i = 4;
			if (h.kind == Spec::Handling::Pick && i < t.size() && (t[i] == "left" || t[i] == "right")) h.hand = t[i++] == "left" ? 0 : 1;
			if (h.kind == Spec::Handling::Put) {
				if (i + 3 >= t.size() || t[i] != "at" || !number(t[i + 1], h.at.x) || !number(t[i + 2], h.at.y) || !number(t[i + 3], h.at.z)) return fail("`put NAME PROP T at X Y Z`: where it is put down");
				i += 4;
			}
			if (h.kind == Spec::Handling::Give) {
				if (i + 1 >= t.size() || t[i] != "to") return fail("`give NAME PROP T to OTHER`: whom it is given to");
				h.to = t[i + 1];
				i += 2;
			}
			if (i != t.size()) return fail("\"" + t[i] + "\" is not part of a " + verb);
			if (!s.handling.empty() && h.t < s.handling.back().t) return fail("picks, puts and gives go in time order");
			s.handling.push_back(h);
		} else if (verb == "look") {
			const std::vector<std::string> t = detail::tokens(line);
			Spec::Look l;
			l.line = n;
			if (t.size() < 4 || t[2] != "at") return fail("`look NAME at WHO|PROP|X Y Z IN0 IN1 [OUT0 OUT1]`");
			l.who = t[1];
			std::size_t i = 3;
			double x = 0.0, y = 0.0, z = 0.0;
			if (t.size() >= i + 5 && number(t[i], x) && number(t[i + 1], y) && number(t[i + 2], z)) {   // a point
				l.point = {x, y, z};
				i += 3;
			} else {
				l.target = t[i++];
			}
			if (i + 1 >= t.size() || !number(t[i], l.when.in0) || !number(t[i + 1], l.when.in1)) return fail("a look needs its times: IN0 IN1 [OUT0 OUT1]");
			i += 2;
			if (i + 1 < t.size()) {
				if (!number(t[i], l.when.out0) || !number(t[i + 1], l.when.out1) || l.when.out0 < l.when.in1 || l.when.out1 < l.when.out0) return fail("a look's out times come after its in times");
				i += 2;
			}
			if (i != t.size() || l.when.in1 < l.when.in0) return fail("`look NAME at WHO|PROP|X Y Z IN0 IN1 [OUT0 OUT1]`");
			l.when.pose = l.target;
			s.looks.push_back(l);
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
		} else if (verb == "dissolve" || verb == "fade" || verb == "wipe") {
			if (s.shots.empty()) return fail("`" + verb + " SECONDS` belongs to a shot (how it comes in): start one with `shot 0 NAME` first");
			Transition &tr = s.shots.back().transition;
			if (w.size() != 2 || !number(w[1], tr.length) || tr.length <= 0.0 || tr.length > 10.0) return fail("`" + verb + " SECONDS`: from more than 0 to 10 seconds");
			tr.kind = verb == "dissolve" ? Transition::Dissolve : verb == "fade" ? Transition::Fade : Transition::Wipe;
			if (s.shots.size() == 1 && tr.kind != Transition::Fade) return fail("the first shot can only fade in (`fade`): there is nothing before it to " + verb + " from");
		} else if (verb == "idle") {
			if (w.size() != 2 || (w[1] != "on" && w[1] != "off")) return fail("`idle on` or `idle off`");
			s.idle = w[1] == "on";
		} else if (verb == "fadeout") {
			if (w.size() != 2 || !number(w[1], s.fadeOut) || s.fadeOut <= 0.0) return fail("`fadeout SECONDS`: the fade to black at the end");
		} else if (verb == "orbit" || verb == "crane" || verb == "push" || verb == "track" || verb == "handheld" || verb == "focus" || verb == "aperture") {
			if (s.shots.empty()) return fail("`" + verb + "` belongs to a shot: start one with `shot 0 NAME` first");
			const std::vector<std::string> t = detail::tokens(line);
			Shot &shot = s.shots.back();
			if (verb == "handheld" || verb == "aperture") {
				double v = verb == "handheld" ? 1.0 : 0.02;
				if (t.size() > 2 || (t.size() == 2 && (!number(t[1], v) || v < 0.0 || v > (verb == "handheld" ? 4.0 : 0.5)))) return fail(verb == "handheld" ? "`handheld [AMOUNT]` (0 to 4)" : "`aperture RADIUS` (0 to 0.5 m)");
				(verb == "handheld" ? shot.handheld : shot.aperture) = v;
				continue;
			}
			if (verb == "focus") {
				if (t.size() < 2 || t.size() % 2 != 0) return fail("`focus WHO [T WHO ...]`: who is in focus, and from when another is");
				shot.focus.push_back({shot.start, t[1]});
				for (std::size_t i = 2; i + 1 < t.size(); i += 2) {
					double at = 0.0;
					if (!number(t[i], at) || at < shot.focus.back().first) return fail("a focus pull's times go in order");
					shot.focus.push_back({at, t[i + 1]});
				}
				continue;
			}
			Shot::Move &mv = shot.move;
			if (t.size() < 2) return fail("`" + verb + " WHO ...`: whom the camera moves about");
			mv.kind = verb == "orbit" ? Shot::Move::Orbit : verb == "crane" ? Shot::Move::Crane : verb == "push" ? Shot::Move::Push : Shot::Move::Track;
			mv.who = t[1];
			if (mv.kind == Shot::Move::Track) mv.height = 1.5;
			for (std::size_t i = 2; i < t.size(); i += 2) {
				double v = 0.0;
				if (i + 1 >= t.size() || !number(t[i + 1], v)) return fail("\"" + t[i] + "\" takes a number");
				const std::string &k = t[i];
				bool ok = true;
				if (k == "fov") mv.fov = v;
				else if (mv.kind == Shot::Move::Orbit && k == "radius" && v > 0.0) mv.radius = v;
				else if ((mv.kind == Shot::Move::Orbit || mv.kind == Shot::Move::Push || mv.kind == Shot::Move::Track) && k == "height") mv.height = v;
				else if (mv.kind == Shot::Move::Orbit && k == "degrees") mv.degrees = v;
				else if (mv.kind == Shot::Move::Orbit && k == "from") mv.from = v;
				else if (mv.kind == Shot::Move::Crane && k == "from") mv.low = v;
				else if (mv.kind == Shot::Move::Crane && k == "to") mv.high = v;
				else if (mv.kind == Shot::Move::Crane && k == "distance" && v > 0.0) mv.distance = v;
				else if (mv.kind == Shot::Move::Push && k == "from" && v > 0.0) mv.near0 = v;
				else if (mv.kind == Shot::Move::Push && k == "to" && v > 0.0) mv.near1 = v;
				else if (mv.kind == Shot::Move::Track && k == "side") mv.side = v;
				else ok = false;
				if (!ok) return fail("\"" + k + "\": " + (mv.kind == Shot::Move::Orbit ? "an orbit takes radius, height, degrees, from and fov" : mv.kind == Shot::Move::Crane ? "a crane takes from, to, distance and fov" : mv.kind == Shot::Move::Push ? "a push takes from, to, height and fov" : "a track takes side, height and fov"));
			}
		} else if (verb == "auto" || verb == "side") {
			if (s.shots.empty()) return fail("`" + verb + "` belongs to a shot: start one with `shot 0 NAME` first");
			const std::vector<std::string> t = detail::tokens(line);
			Shot &shot = s.shots.back();
			if (verb == "side") {
				if (t.size() != 2 || (t[1] != "left" && t[1] != "right")) return fail("`side left` or `side right`: where the first character covered is in the frame");
				shot.side = t[1] == "left" ? 1 : -1;
				continue;
			}
			shot.coverage = true;
			for (std::size_t i = 1; i < t.size(); ++i) shot.cover.push_back(t[i]);
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
			return fail("unknown word \"" + verb + "\" (film, name, about, scene, length, fps, still, tags, ease, shutter, idle, shot, key, orbit, crane, push, track, handheld, focus, aperture, dissolve, fade, wipe, fadeout, follow, subject, auto, side, move, turn, grow, light, show, bounce, topple, character, crowd, walk, run, jump, sit, gesture, feel, say, look, prop, pick, put, give, place, scatter, sky, ambience, weather, particles, burst)");
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
	for (Character &c : s.characters)   // (a crowd's wandering was made before the film's length was known: what starts after the film ends is not walked)
		if (c.extra) c.walks.erase(std::remove_if(c.walks.begin(), c.walks.end(), [&](const WalkLine &w) { return w.t0 > s.length - 1.0; }), c.walks.end());
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
		sourcePath = filePath;
		std::error_code ec;
		sourceTime = std::filesystem::last_write_time(filePath, ec);
		try {
			const std::string problem = loadFile(filePath);
			sceneTime = std::filesystem::last_write_time(sceneFile, ec);
			return problem;
		} catch (const std::exception &e) {   // (a pattern or a number the text does not fit: a problem of this file, not a crash)
			return filePath + ": " + e.what();
		}
	}
	// The film file read again when it, or its scene, was saved since it was read (a film timeline that follows the edits): true when it was. When the new file has a
	// problem it is said (`problem`) and the film stays as it was.
	bool reloadIfChanged(std::string *problem = nullptr) {
		std::error_code ec;
		if (sourcePath.empty()) return false;
		const auto t = std::filesystem::last_write_time(sourcePath, ec), ts = std::filesystem::last_write_time(sceneFile, ec);
		if (t == sourceTime && ts == sceneTime) return false;
		DataFilm fresh;
		const std::string why = fresh.load(sourcePath);
		sourceTime = t, sceneTime = ts;   // (a bad file is not read again until it is saved again)
		if (!why.empty()) {
			if (problem) *problem = why;
			return false;
		}
		*this = std::move(fresh);
		return true;
	}
	std::string sourcePath;
	std::filesystem::file_time_type sourceTime{}, sceneTime{};

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
			for (const auto &[part, colour] : c.colours)
				if (std::find(kind.parts.begin(), kind.parts.end(), part) == kind.parts.end()) {
					std::string known;
					for (const std::string &k : kind.parts) known += (known.empty() ? "" : ", ") + k;
					return filePath + ":" + std::to_string(c.line) + ": a " + c.kind + " has no part called \"" + part + "\" to colour" + (known.empty() ? std::string(" (its colours are its own)") : " (its parts: " + known + ")");
				}
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
		if (spec.hasSky) leaveOutSkyLights();
		for (const Spec::Prop &pr : spec.props)
			if (std::find(scene.names.begin(), scene.names.end(), pr.name) == scene.names.end())
				return filePath + ":" + std::to_string(pr.line) + ": no block of " + sceneFile.filename().string() + " is marked `# @anim " + pr.name + "`";
		if (const std::string e = findWays(); !e.empty()) return filePath + ":" + e;
		heightsOfWalks();
		if (const std::string e = crowding(); !e.empty()) return filePath + ": " + e;
		if (const std::string e = holdingTimeline(); !e.empty()) return filePath + ":" + e;
		for (const Spec::Look &l : spec.looks) {
			if (!character(l.who)) return filePath + ":" + std::to_string(l.line) + ": " + l.who + " is not brought in with `character`";
			if (!l.target.empty() && !character(l.target) && !prop(l.target)) return filePath + ":" + std::to_string(l.line) + ": " + l.target + " is neither a character nor a prop";
		}
		if (const std::string e = coverShots(); !e.empty()) return filePath + ": " + e;
		for (Shot &shot : spec.shots)   // (what a move is about, it is about)
			if (shot.move.kind != Shot::Move::None && std::find(shot.about.begin(), shot.about.end(), shot.move.who) == shot.about.end()) shot.about.push_back(shot.move.who);
		for (const Shot &shot : spec.shots) {
			std::vector<std::string> named = shot.about;
			if (shot.following) named.push_back(shot.follow.who);
			for (const auto &[at, who] : shot.focus) named.push_back(who);
			for (const std::string &who : named)
				if (!character(who)) return filePath + ": the shot \"" + shot.name + "\" names " + who + ", who is not brought in with `character`";
		}
		return std::string();
	}

	std::vector<std::vector<speech::Syllable>> spoken;   // each line's syllables (spec.lines)

	// ---- the set's dressing ----
	// The scene's own infinite and distant lights left out of its plain text (a `sky` brings its own sun and sky).
	void leaveOutSkyLights() {
		for (Piece &piece : scene.world) {
			if (piece.block >= 0) continue;
			const std::vector<std::string> lines = detail::splitLines(piece.text);
			std::string kept;
			for (std::size_t i = 0; i < lines.size();) {
				const std::string t = detail::trim(lines[i]);
				if (detail::firstWord(lines[i]) == "LightSource" && (t.find("\"infinite\"") != std::string::npos || t.find("\"distant\"") != std::string::npos)) {
					i = detail::statementEnd(lines, i);
					continue;
				}
				kept += lines[i++];
			}
			piece.text = kept;
		}
	}
	// The dressing at t: the pieces once, then every placed thing (swaying), as scene text.
	std::string dressingAt(double t) const {
		if (spec.placements.empty()) return std::string();
		std::string out = "# the set's dressing (film_dressing.h)\n" + dressing::objects();
		for (const dressing::Placement &p : spec.placements) out += dressing::placementText(p, t);
		return out;
	}

	// ---- walks that find their way ----
	// The set at t without its characters and its props (pbrt text): what is in the way.
	std::string setAt(double t) const {
		std::string out = headerAt(t);
		for (const Piece &piece : scene.world) {
			if (piece.block < 0) {
				out += piece.text;
				continue;
			}
			const Block &b = scene.blocks[static_cast<std::size_t>(piece.block)];
			if (!visible(b.name, t) || prop(b.name)) continue;
			out += b.head + transformsAt(b.name, t) + b.body;
		}
		return out + dressingAt(t);
	}
	// Finds the way of every walk that goes `to` places, in time order (so whoever walked before is where their walk left them): "" or "line: what is wrong".
	std::string findWays() {
		std::vector<std::pair<Character *, std::size_t>> walks;
		for (Character &c : spec.characters)
			for (std::size_t i = 0; i < c.walks.size(); ++i)
				if (!c.walks[i].targets.empty()) walks.push_back({&c, i});
		std::stable_sort(walks.begin(), walks.end(), [](const auto &a, const auto &b) { return a.first->walks[a.second].t0 < b.first->walks[b.second].t0; });
		std::map<std::string, pbrt_flatten::FlatScene> flattened;   // (by the set's text: a set that does not move is flattened once)
		const pbrt_flatten::MeshResolver meshes = [](const std::string &path, std::vector<float> &positions, std::vector<int> &indices, std::vector<float> &uvs, std::vector<float> &normals) {
			ply_mesh::LoadResult m = ply_mesh::loadFile(path);
			if (!m.ok) return false;
			positions = std::move(m.mesh.positions);
			indices = std::move(m.mesh.indices);
			uvs = std::move(m.mesh.uvs);
			normals = std::move(m.mesh.normals);
			return true;
		};
		for (const auto &[c, i] : walks) {
			WalkLine &w = c->walks[i];
			const CharacterKind &kind = characterKinds().at(c->kind);
			double fx = c->start[0], fz = c->start[2], fy = c->start[1];   // from where its last walk left it
			if (i > 0) {
				const Vec3 last = endOf(*c, i - 1);
				fx = -last.x, fy = last.y, fz = last.z;
			}
			const std::string set = setAt(w.t0);
			if (!flattened.count(set)) flattened[set] = film_paths::flattenText(set, meshes);
			const pbrt_flatten::FlatScene &flat = flattened[set];
			// the map round the walk: the set between its ankles and its head, whoever else stands there, grown by its size
			double x0 = fx, x1 = fx, z0 = fz, z1 = fz;
			for (const auto &[x, z] : w.targets) x0 = std::min(x0, x), x1 = std::max(x1, x), z0 = std::min(z0, z), z1 = std::max(z1, z);
			const double half = std::min(30.0, 0.5 * std::max(x1 - x0, z1 - z0) + 6.0);
			const double cell = std::max(0.05, std::min(0.12, kind.radius * c->scale * 0.4));
			const double height = std::min(2.0, kind.height * c->scale), step = stepOf(*c);
			const film_paths::Terrain terrain = film_paths::terrainOf(flat, 0.5 * (x0 + x1), 0.5 * (z0 + z1), half, cell, c->start[1]);
			std::vector<double> floors;
			film_paths::Grid g = film_paths::reachable(terrain, fx, fy, fz, step, std::min(0.15, 0.4 * height), height, floors);   // (the floor it can walk on, from where it stands)
			for (const Character &other : spec.characters) {
				if (&other == c) continue;
				Vec3 at;
				momentOf(other, w.t0, &at);
				film_paths::addDisc(g, -at.x, at.z, characterKinds().at(other.kind).radius * other.scale);
			}
			const film_paths::Grid ways = film_paths::grown(g, kind.radius * c->scale);
			w.points.clear();
			w.heights.clear();
			for (const auto &[tx, tz] : w.targets) {
				std::vector<std::pair<double, double>> leg;
				std::string why;
				if (!film_paths::findWay(ways, fx, fz, tx, tz, leg, why)) {
					if (why == "something is in the way there" && std::isnan(film_paths::floorFrom(g, floors, tx, tz)) &&
						terrain.roomy(terrain.grid.col(tx), terrain.grid.row(tz), terrain.floorAt(tx, tz, fy + 50.0, 100.0), std::min(0.15, 0.4 * height), height))
						why = "it cannot get up or down there (a drop or a rise of more than a step)";   // (the place has room: the way there is the trouble)
					char buf[200];
					std::snprintf(buf, sizeof buf, "%d: %s cannot find a way to %g %g: %s", w.line, c->name.c_str(), tx, tz, why.c_str());
					return buf;
				}
				// along each leg every quarter of a metre, at the floor's height (over a hill, up the steps)
				double px = fx, pz = fz;
				for (const auto &[lx, lz] : leg) {
					const int n = std::max(1, static_cast<int>(std::ceil(std::hypot(lx - px, lz - pz) / 0.25)));
					for (int k = 1; k <= n; ++k) {
						const double u = static_cast<double>(k) / n, x = px + (lx - px) * u, z = pz + (lz - pz) * u;
						const double y = film_paths::floorFrom(g, floors, x, z);
						w.points.push_back({x, z});
						w.heights.push_back(std::isnan(y) ? c->start[1] : (std::abs(y - c->start[1]) < terrain.flat ? c->start[1] : y));
					}
					px = lx, pz = lz;
				}
				fx = tx;
				fz = tz;
			}
		}
		return std::string();
	}
	// How high a step a character takes in its stride (a person 0.3 m, Pip a little less, a turtle at its own scale).
	double stepOf(const Character &c) const { return 0.17 * characterKinds().at(c.kind).height * c.scale; }
	// The heights of the walks and jumps that do not find their way, from the floor under each point (the walk's own points: it goes where it is told, on whatever is there).
	void heightsOfWalks() {
		terrains.clear();
		const bool anyWalks = std::any_of(spec.characters.begin(), spec.characters.end(), [](const Character &c) { return !c.walks.empty(); });
		if (!anyWalks) return;
		const pbrt_flatten::MeshResolver meshes = [](const std::string &path, std::vector<float> &positions, std::vector<int> &indices, std::vector<float> &uvs, std::vector<float> &normals) {
			ply_mesh::LoadResult m = ply_mesh::loadFile(path);
			if (!m.ok) return false;
			positions = std::move(m.mesh.positions);
			indices = std::move(m.mesh.indices);
			uvs = std::move(m.mesh.uvs);
			normals = std::move(m.mesh.normals);
			return true;
		};
		const pbrt_flatten::FlatScene flat = film_paths::flattenText(setAt(0.0), meshes);
		for (Character &c : spec.characters) {
			double x0 = c.start[0], x1 = x0, z0 = c.start[2], z1 = z0;
			for (const WalkLine &w : c.walks)
				for (const auto &[x, z] : w.points) x0 = std::min(x0, x), x1 = std::max(x1, x), z0 = std::min(z0, z), z1 = std::max(z1, z);
			const double half = std::min(30.0, 0.5 * std::max(x1 - x0, z1 - z0) + 2.0);
			auto terrain = std::make_shared<film_paths::Terrain>(film_paths::terrainOf(flat, 0.5 * (x0 + x1), 0.5 * (z0 + z1), half, 0.1, c.start[1]));
			double y = c.start[1];
			bool uneven = false;
			const double step = stepOf(c);
			for (std::size_t i = 0; i < c.walks.size(); ++i) {
				WalkLine &w = c.walks[i];
				if (!w.targets.empty()) {   // (found already, on the floor)
					if (!w.heights.empty()) y = w.heights.back();
					continue;
				}
				w.heights.clear();
				for (const auto &[x, z] : w.points) {
					// a jump lands on the highest floor within its height; a walk keeps to a floor within a step of the last
					y = terrain->floorAt(x, z, y, w.style == WalkLine::Jump ? std::max(step, w.height) : step);
					w.heights.push_back(y);
				}
			}
			for (const WalkLine &w : c.walks)
				for (double h : w.heights) uneven = uneven || h != c.start[1];
			if (uneven) terrains[c.name] = terrain;   // (its feet on the floor too)
		}
	}
	std::map<std::string, std::shared_ptr<const film_paths::Terrain>> terrains;   // the floor of each character that walks on uneven ground
	const film_paths::Terrain *terrainFor(const Character &c) const {
		const auto it = terrains.find(c.name);
		return it == terrains.end() ? nullptr : it->second.get();
	}
	// Two characters that walk into each other (closer than most of their sizes while one of them walks): "" or what happens when.
	std::string crowding() const {
		const double step = 0.05;
		for (double t = 0.0; t <= spec.length; t += step)
			for (std::size_t a = 0; a < spec.characters.size(); ++a)
				for (std::size_t b = a + 1; b < spec.characters.size(); ++b) {
					const Character &ca = spec.characters[a], &cb = spec.characters[b];
					if (ca.extra || cb.extra) continue;   // (a crowd keeps to its cells; the others are told of each other)
					Vec3 pa, pb;
					const bool movingA = momentOf(ca, t, &pa).moving, movingB = momentOf(cb, t, &pb).moving;
					if (!movingA && !movingB) continue;
					const double reach = 0.8 * (characterKinds().at(ca.kind).radius * ca.scale + characterKinds().at(cb.kind).radius * cb.scale);
					if (std::hypot(pa.x - pb.x, pa.z - pb.z) < reach) {
						char buf[200];
						std::snprintf(buf, sizeof buf, "%s and %s walk into each other at %.2f s (give one of them a walk `to` a place: it finds its way round the other)", ca.name.c_str(), cb.name.c_str(), t);
						return buf;
					}
				}
		return std::string();
	}

	// ---- automatic coverage ----
	// A camera key from story-space places (pbrt's: x mirrored), with a field of view and, for a shot of one speaker, a shallow focus on them.
	static camera_keyframes::Key keyOf(Vec3 eye, Vec3 target, double fov, double lens) {
		camera_keyframes::Key k;
		k.pos[0] = -eye.x, k.pos[1] = eye.y, k.pos[2] = eye.z;
		k.target[0] = -target.x, k.target[1] = target.y, k.target[2] = target.z;
		k.fov = fov;
		if (lens > 0.0) {
			k.lensRadius = lens;
			k.focus = length(target - eye);
		}
		return k;
	}
	Vec3 headOf(const Character &c, double t) const { return bodyPoint(c, t, characterKinds().at(c.kind).head); }
	// The field of view that frames a speaker of this height from this far: their head and shoulders (a frame about 0.55 of their height and 0.3 m more: a big head needs room).
	static double framing(double size, double distance) { return std::clamp(2.0 * std::atan(0.5 * (0.55 * size + 0.3) / std::max(0.1, distance)) * 180.0 / kPi, 12.0, 50.0); }
	double sizeOf(const Character &c) const { return characterKinds().at(c.kind).height * c.scale; }
	// The line of a coverage at t: from the first covered to the second, on the ground (one alone: its own left to right).
	Vec3 lineOf(const std::vector<const Character *> &cast, double t) const {
		Vec3 d;
		if (cast.size() >= 2) d = headOf(*cast[1], t) - headOf(*cast[0], t);
		d.y = 0.0;
		if (length(d) < 1e-6) {
			const Mat4 f = momentOf(*cast[0], t).frame;
			d = transformPoint(f, {-1.0, 0.0, 0.0}) - transformPoint(f, {0.0, 0.0, 0.0});   // (its right)
		}
		return normalize(d);
	}
	// The side of the line the cameras keep to when the shot does not say: the one the characters' faces look to (more of them look that way than the other), the first on the left
	// when it is even.
	int sideTheyFace(const std::vector<const Character *> &cast, double t) const {
		const Vec3 d = lineOf(cast, t), n{-d.z, 0.0, d.x};
		double toward = 0.0;
		for (const Character *c : cast) {
			const Mat4 f = momentOf(*c, t).frame;
			toward += dot(transformPoint(f, {0.0, 0.0, 1.0}) - transformPoint(f, {0.0, 0.0, 0.0}), n);
		}
		return toward < -1e-6 ? -1 : 1;
	}
	// Where the camera is for a beat at t (story space): `speaker` null for the shot of them all (a two-shot), else a shot of the speaker over the shoulder of the nearest of the
	// others (or, alone or much smaller than that listener, from in front of them, turned to the side), every camera on the `side` of the line from the first covered to the second.
	camera_keyframes::Key coverageKey(const std::vector<const Character *> &cast, const Character *speaker, int side, double t) const {
		static const Vec3 up{0.0, 1.0, 0.0};
		const auto flat = [](Vec3 v) { return Vec3{v.x, 0.0, v.z}; };
		const Vec3 d = lineOf(cast, t);
		const Vec3 n = Vec3{-d.z, 0.0, d.x} * static_cast<double>(side);   // (looking back at the line from here, the first is on the left)
		const double hfov = 2.0 * std::atan(std::tan(35.0 * kPi / 360.0) * 16.0 / 9.0);
		if (!speaker) {   // all of them: square on to the line, far enough back for everyone
			// everyone from their feet to above the tallest head, side to side
			Vec3 mid{0.0, 0.0, 0.0};
			double size = 0.0, half = 0.0, top = -1e30, bottom = 1e30;
			for (const Character *c : cast) {
				const Vec3 h = headOf(*c, t), feet = bodyPoint(*c, t, {0.0, 0.0, 0.0});
				mid = mid + h * (1.0 / cast.size());
				size = std::max(size, sizeOf(*c));
				top = std::max(top, feet.y + 1.08 * sizeOf(*c));
				bottom = std::min(bottom, feet.y - 0.05 * sizeOf(*c));
			}
			for (const Character *c : cast) half = std::max(half, std::abs(dot(flat(headOf(*c, t) - mid), d)) + 0.45 * sizeOf(*c));
			double dist = std::max({1.6 * size, half / std::tan(0.5 * hfov) * 1.15, 0.5 * (top - bottom) / std::tan(35.0 * kPi / 360.0) * 1.1});
			const Vec3 target{mid.x, 0.5 * (top + bottom), mid.z};
			// back away until everyone's feet and head, either side of them, are in the frame (one standing off the line, nearer the camera, may not be yet)
			const double tanH = std::tan(0.5 * hfov) * 0.92, tanV = std::tan(35.0 * kPi / 360.0) * 0.92;
			for (int i = 0; i < 40; ++i) {
				const Vec3 eye = target + n * dist + up * (0.1 * size), fwd = normalize(target - eye), right = normalize(cross(fwd, up)), camUp = cross(right, fwd);
				bool fits = true;
				for (const Character *c : cast) {
					const Vec3 feet = bodyPoint(*c, t, {0.0, 0.0, 0.0});
					const double r = characterKinds().at(c->kind).radius * c->scale;
					for (const Vec3 &q : {feet + right * r, feet - right * r, feet + up * (1.05 * sizeOf(*c)) + right * r, feet + up * (1.05 * sizeOf(*c)) - right * r}) {
						const double depth = dot(q - eye, fwd);
						if (depth <= 0.1 || std::abs(dot(q - eye, right)) > tanH * depth || std::abs(dot(q - eye, camUp)) > tanV * depth) fits = false;
					}
				}
				if (fits) break;
				dist *= 1.08;
			}
			return keyOf(target + n * dist + up * (0.1 * size), target, 35.0, 0.0);
		}
		const Vec3 hs = headOf(*speaker, t);
		const Character *listener = nullptr;
		for (const Character *c : cast)
			if (c != speaker && (!listener || length(headOf(*c, t) - hs) < length(headOf(*listener, t) - hs))) listener = c;
		const double size = sizeOf(*speaker);
		if (!listener || sizeOf(*listener) > 1.6 * size) {   // alone, or small beside the listener (over a tall one's shoulder it would be lost below them): from in front, a third of the way round
			const Mat4 f = momentOf(*speaker, t).frame;
			const Vec3 front = normalize(transformPoint(f, {0.0, 0.0, 1.0}) - transformPoint(f, {0.0, 0.0, 0.0}));
			const Vec3 at = normalize(front * 0.87 + n * 0.5);
			const Vec3 eye = hs + at * (1.1 * size) - up * (0.03 * size), target = hs - up * (0.08 * size);
			return keyOf(eye, target, framing(size, length(target - eye)), 0.012 * size);
		}
		// over the listener's shoulder: behind them, to the camera side, about their head's height (between theirs and the speaker's)
		const Vec3 hl = headOf(*listener, t), away = normalize(flat(hl - hs));
		const double ls = sizeOf(*listener);
		const double beside = 1.5 * characterKinds().at(listener->kind).radius * listener->scale + 0.3;   // (to the side of the listener's head and shoulders: a third of the frame)
		const Vec3 eye = hl + away * (0.35 * ls) + n * beside + up * (0.2 * (hs.y - hl.y) + 0.02 * ls);
		const Vec3 target = hs - up * (0.08 * size);
		return keyOf(eye, target, framing(size, length(target - eye)), 0.012 * size);
	}
	// Every `auto` shot made into shots of its own, from what happens in it: a two-shot from its start, a shot of each speaker from just before they speak, back to the
	// two-shot when nobody speaks for a while; no cut less than a second after the last. "" or what is wrong.
	std::string coverShots() {
		std::vector<Shot> out;
		for (std::size_t k = 0; k < spec.shots.size(); ++k) {
			const Shot &shot = spec.shots[k];
			if (!shot.coverage) {
				out.push_back(shot);
				continue;
			}
			const double t0 = shot.start, t1 = k + 1 < spec.shots.size() ? spec.shots[k + 1].start : spec.length;
			std::vector<const Character *> cast;
			for (const std::string &name : shot.cover) {
				const Character *c = character(name);
				if (!c) return "the shot \"" + shot.name + "\" covers " + name + ", who is not brought in with `character`";
				cast.push_back(c);
			}
			std::vector<std::size_t> lines;   // its lines, in time order
			for (std::size_t i = 0; i < spec.lines.size(); ++i)
				if (spec.lines[i].t >= t0 && spec.lines[i].t < t1) lines.push_back(i);
			std::stable_sort(lines.begin(), lines.end(), [this](std::size_t a, std::size_t b) { return spec.lines[a].t < spec.lines[b].t; });
			if (cast.empty()) {
				for (std::size_t i : lines)
					if (std::find(cast.begin(), cast.end(), character(spec.lines[i].who)) == cast.end()) cast.push_back(character(spec.lines[i].who));
				for (const Character &c : spec.characters)
					if (!c.extra && std::find(cast.begin(), cast.end(), &c) == cast.end()) cast.push_back(&c);
			}
			if (cast.empty()) return "the shot \"" + shot.name + "\" is `auto` but the film has no characters to cover";
			// the beats: who is on screen from when
			std::vector<std::pair<double, const Character *>> beats{{t0, nullptr}};
			for (std::size_t j = 0; j < lines.size(); ++j) {
				const speech::Line &l = spec.lines[lines[j]];
				const Character *who = character(l.who);
				if (std::find(cast.begin(), cast.end(), who) == cast.end()) continue;
				const double cut = std::max(t0, l.t - 0.15);
				if (cut - beats.back().first < 1.0) {
					if (beats.size() == 1) beats.back() = {t0, who};   // (it opens on the speaker)
					else if (beats.back().second != who) beats.back().second = who;
				} else if (beats.back().second != who) {
					beats.push_back({cut, who});
				}
				const double end = speech::endOf(spoken[lines[j]], l.t);
				const double next = j + 1 < lines.size() ? spec.lines[lines[j + 1]].t : t1;
				if (next - end > 2.0 && end + 0.6 < t1 - 1.0) beats.push_back({end + 0.6, nullptr});
			}
			for (std::size_t b = 0; b < beats.size(); ++b) {
				const double from = beats[b].first, to = b + 1 < beats.size() ? beats[b + 1].first : t1;
				Shot g;
				g.start = from;
				g.name = shot.name + (beats.size() == 1 ? std::string() : beats[b].second ? ": " + beats[b].second->name : ": everyone");
				const int side = shot.side != 0 ? shot.side : sideTheyFace(cast, t0);
				g.path.keys = {coverageKey(cast, beats[b].second, side, from), coverageKey(cast, beats[b].second, side, std::max(from, to - 1e-3))};
				g.path.ease = true;
				if (b == 0) g.transition = shot.transition;   // (how the auto shot comes in)
				g.handheld = shot.handheld;
				if (beats[b].second) g.about.push_back(beats[b].second->name);
				else
					for (const Character *c : cast) g.about.push_back(c->name);
				out.push_back(g);
			}
		}
		spec.shots = out;
		return std::string();
	}

	// ---- props characters handle ----
	// Who holds a prop from a time on: a character's hand, or nobody (it rests at `rest`, pbrt's).
	struct Holding {
		double t = -1e30;
		std::string who;
		int hand = 1;
		Vec3 rest;
		Vec3 meet;   // (a give) where the two hands meet (story space)
	};
	std::map<std::string, std::vector<Holding>> holdings;   // each prop's, in time order
	static constexpr double kReach = 0.6, kSettle = 0.5;    // seconds: a hand reaches for a prop (or puts it down) over this; it carries it in, or goes back to rest, over that

	const Spec::Prop *prop(const std::string &name) const {
		for (const Spec::Prop &p : spec.props)
			if (p.name == name) return &p;
		return nullptr;
	}
	// Plays the picks, puts and gives through (who holds what, from when): "" or "line: what is wrong".
	std::string holdingTimeline() {
		holdings.clear();
		for (const Spec::Prop &p : spec.props) holdings[p.name].push_back({-1e30, std::string(), 1, p.home});
		for (const Spec::Handling &h : spec.handling) {
			const std::string where = std::to_string(h.line) + ": ";
			if (!prop(h.prop)) return where + h.prop + " is not a prop (`prop " + h.prop + " at X Y Z`)";
			if (!character(h.who)) return where + h.who + " is not brought in with `character`";
			std::vector<Holding> &line = holdings[h.prop];
			const Holding now = line.back();
			Holding next;
			next.t = h.t;
			if (h.kind == Spec::Handling::Pick) {
				if (!now.who.empty()) return where + h.prop + " is in " + now.who + "'s hand then";
				next.who = h.who;
				next.hand = h.hand >= 0 ? h.hand : characterKinds().at(character(h.who)->kind).hand;
				for (const auto &[other, held] : holdings)
					if (other != h.prop && !held.empty() && held.back().who == h.who && held.back().hand == next.hand) return where + h.who + "'s hand already holds " + other;
			} else {
				if (now.who != h.who) return where + h.who + " does not hold " + h.prop + " then";
				if (h.kind == Spec::Handling::Put) {
					next.rest = h.at;
				} else {
					if (!character(h.to)) return where + h.to + " is not brought in with `character`";
					if (h.to == h.who) return where + "a character cannot give a prop to itself";
					next.who = h.to;
					next.hand = characterKinds().at(character(h.to)->kind).hand;
				}
			}
			if (h.t - now.t < kReach + kSettle && now.t > -1e29) return where + "too soon after the last thing done with " + h.prop + " (" + std::to_string(kReach + kSettle) + " s at least)";
			if (h.kind == Spec::Handling::Give) {
				double gap = 0.0;
				next.meet = meetingOf(*character(now.who), now.hand, *character(next.who), next.hand, h.t, &gap);
				if (gap > 0.06) {
					char buf[160];
					std::snprintf(buf, sizeof buf, "%s and %s cannot reach each other to give %s at %g s (their hands stay %.2f m apart): bring them closer", now.who.c_str(), next.who.c_str(), h.prop.c_str(), h.t, gap);
					return where + buf;
				}
			}
			line.push_back(next);
		}
		return std::string();
	}
	// The world place (story space) of a body-space point of a character at t.
	Vec3 bodyPoint(const Character &c, double t, Vec3 local) const { return transformPoint(momentOf(c, t).frame, local * c.scale); }
	// Where a character carries a prop in a hand at t (story space).
	Vec3 carryOf(const Character &c, int hand, double t) const {
		Vec3 k = characterKinds().at(c.kind).carry;
		if (hand == 1) k.x = -k.x;   // (body space: +x its left)
		return bodyPoint(c, t, k);
	}
	// Where two characters' hands meet to give a prop at t: from half way between where each carries it, each hand reaches for the point and the point moves half way between
	// where they got to (an arm too short, a hand too high), a few times over - a place both can reach when there is one.
	Vec3 meetingOf(const Character &a, int handA, const Character &b, int handB, double t, double *gap = nullptr) const {
		Vec3 meet = (carryOf(a, handA, t) + carryOf(b, handB, t)) * 0.5;
		const CharacterKind &ka = characterKinds().at(a.kind), &kb = characterKinds().at(b.kind);
		if (gap) *gap = 0.0;
		if (!ka.handAt || !kb.handAt) return meet;
		for (int i = 0; i < 8; ++i) {
			CharacterMoment ma = momentOf(a, t), mb = momentOf(b, t);
			ma.hands[handA] = {meet, 1.0};
			mb.hands[handB] = {meet, 1.0};
			const Vec3 ha = ka.handAt(ma, handA), hb = kb.handAt(mb, handB);
			meet = (ha + hb) * 0.5;
			if (gap) *gap = length(ha - hb);
		}
		return meet;
	}
	// Where the hands of a character reach at t for the props it handles (CharacterMoment::hands), as its picks, puts and gives say.
	void handsAt(const Character &c, double t, CharacterMoment &m) const {
		for (const auto &[name, line] : holdings) {
			for (std::size_t i = 1; i < line.size(); ++i) {
				const Holding &before = line[i - 1], &now = line[i];
				const double T = now.t;
				if (t < T - kReach - 0.01 || t > T + kSettle + 0.01) {
					// holding it between two changes: its hand at its carry spot
					const double until = i + 1 < line.size() ? line[i + 1].t - kReach : 1e30;
					if (now.who == c.name && t > T + kSettle && t < until) m.hands[now.hand] = {carryOf(c, now.hand, t), 1.0};
					continue;
				}
				const double in = smoothstep(T - kReach, T, t), out = smoothstep(T, T + kSettle, t);
				if (before.who.empty() && now.who == c.name) {   // a pick: reach for where it rests, then bring it in
					const Vec3 rest = story(before.rest.x, before.rest.y, before.rest.z);
					m.hands[now.hand] = t < T ? CharacterMoment::Reach{rest, in} : CharacterMoment::Reach{lerp(rest, carryOf(c, now.hand, t), out), 1.0};
				} else if (before.who == c.name && now.who.empty()) {   // a put: take it to where it goes, let go, the hand goes back
					const Vec3 to = story(now.rest.x, now.rest.y, now.rest.z);
					m.hands[before.hand] = t < T ? CharacterMoment::Reach{lerp(carryOf(c, before.hand, t), to, in), 1.0} : CharacterMoment::Reach{to, 1.0 - out};
				} else if (before.who == c.name || now.who == c.name) {   // a give: both hands go to where they meet
					const Vec3 meet = now.meet;
					if (before.who == c.name) m.hands[before.hand] = t < T ? CharacterMoment::Reach{lerp(carryOf(c, before.hand, t), meet, in), 1.0} : CharacterMoment::Reach{meet, 1.0 - out};
					else m.hands[now.hand] = t < T ? CharacterMoment::Reach{meet, in} : CharacterMoment::Reach{lerp(meet, carryOf(c, now.hand, t), out), 1.0};
				}
			}
		}
	}
	// A character's moment with its hands and its look: what its kind needs to draw it, or to say where its hand is.
	CharacterMoment actingMoment(const Character &c, double t) const {
		CharacterMoment m = momentOf(c, t);
		handsAt(c, t, m);
		m.idle = spec.idle;
		if (spec.idle) listenAt(c, t, m);
		lookAt(c, t, m);
		return m;
	}
	// Where a character looks of itself while someone speaks (CharacterMoment::look): at the speaker, turning to them just after they begin and back a little after they end
	// (the look moves smoothly from one speaker to the next); and while it speaks, at the nearest of those who listen. A `look` line is on top of this.
	void listenAt(const Character &c, double t, CharacterMoment &m) const {
		Vec3 sum{0.0, 0.0, 0.0};
		double weights = 0.0, most = 0.0;
		for (std::size_t i = 0; i < spec.lines.size(); ++i) {
			const speech::Line &l = spec.lines[i];
			const double end = speech::endOf(spoken[i], l.t);
			if (t < l.t - 0.3 || t > end + 1.2) continue;
			const double w = smoothstep(l.t - 0.25, l.t + 0.35, t) * (1.0 - smoothstep(end + 0.4, end + 1.1, t));
			if (w <= 0.0) continue;
			const Character *speaker = character(l.who);
			Vec3 at;
			if (speaker != &c) {
				at = bodyPoint(*speaker, t, characterKinds().at(speaker->kind).head);
			} else {   // it speaks: to the nearest of the others
				const Character *nearest = nullptr;
				double best = 1e30;
				for (const Character &o : spec.characters) {
					if (&o == &c) continue;
					const double d = length(bodyPoint(o, t, {0.0, 0.0, 0.0}) - bodyPoint(c, t, {0.0, 0.0, 0.0}));
					if (d < best) best = d, nearest = &o;
				}
				if (!nearest || best > 8.0) continue;
				at = bodyPoint(*nearest, t, characterKinds().at(nearest->kind).head);
			}
			if (length(at - bodyPoint(c, t, characterKinds().at(c.kind).head)) > 12.0) continue;   // (too far to be talking to)
			sum = sum + at * w;
			weights += w;
			most = std::max(most, w);
		}
		if (weights > 0.0) m.look = {sum * (1.0 / weights), most * 0.9};
	}
	// Where a prop's handle is at t (story space): where it rests, or in the hand that holds it.
	Vec3 propAt(const std::string &name, double t) const {
		const std::vector<Holding> &line = holdings.at(name);
		const Holding *h = &line.front();
		for (const Holding &each : line)
			if (each.t <= t) h = &each;
		if (h->who.empty()) return story(h->rest.x, h->rest.y, h->rest.z);
		const Character &c = *character(h->who);
		const CharacterKind &kind = characterKinds().at(c.kind);
		CharacterMoment m = momentOf(c, t);   // (its hands; not its look - it may be looking at this prop)
		handsAt(c, t, m);
		return kind.handAt ? kind.handAt(m, h->hand) : carryOf(c, h->hand, t);
	}
	// ---- where characters look ----
	void lookAt(const Character &c, double t, CharacterMoment &m) const {
		for (const Spec::Look &l : spec.looks) {
			if (l.who != c.name) continue;
			const double w = rig::gestureWeight(l.when, t);
			if (w <= 0.0) continue;
			Vec3 target;
			if (l.target.empty()) target = story(l.point.x, l.point.y, l.point.z);
			else if (const Character *other = character(l.target)) target = bodyPoint(*other, t, characterKinds().at(other->kind).head);
			else target = propAt(l.target, t);
			m.look = {m.look.weight > 0.0 ? lerp(m.look.at, target, w) : target, std::max(m.look.weight, w)};
		}
	}

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
		for (std::size_t i = 0; i < walk; ++i) from = endOf(c, i);
		rig::PathWalk w;
		w.points.push_back(from);
		const WalkLine &wl = c.walks[walk];
		for (std::size_t p = 0; p < wl.points.size(); ++p) w.points.push_back(story(wl.points[p].first, p < wl.heights.size() ? wl.heights[p] : c.start[1], wl.points[p].second));
		w.start = c.walks[walk].t0;
		w.speed = c.walks[walk].speed;
		w.stride = kind.stride * c.scale * (c.walks[walk].style == WalkLine::Run ? kRunStride : 1.0);
		w.firstStep = kind.firstStep;
		return w;
	}
	// Where a walk, a run or a jump leaves a character (story space; its height the floor's there).
	Vec3 endOf(const Character &c, std::size_t walk) const {
		const WalkLine &w = c.walks[walk];
		return story(w.points.back().first, w.heights.empty() ? c.start[1] : w.heights.back(), w.points.back().second);
	}
	static constexpr double kRunStride = 1.6;   // a run's gait, and so its stride, is this much wider than a walk's
	static double flightOf(const WalkLine &w) { return 2.0 * std::sqrt(2.0 * w.height / 9.81); }   // (a jump's time in the air)
	// The moment a character last landed from a jump at or before t (or -1e30).
	double landedAt(const Character &c, double t) const {
		double land = -1e30;
		for (const WalkLine &w : c.walks)
			if (w.style == WalkLine::Jump && w.t0 + flightOf(w) <= t) land = w.t0 + flightOf(w);
		return land;
	}
	// Where a character is at t (story space), which way it faces (degrees, rig::headingOf), how many steps it has made in its walk and whether it is walking. After a walk it
	// stands where the walk ended, its feet where they came down.
	CharacterMoment momentOf(const Character &c, double t, Vec3 *at = nullptr) const {
		Vec3 where = story(c.start[0], c.start[1], c.start[2]);
		double heading = -c.facing, steps = 0.0, running = 0.0, crouch = 0.0;
		bool moving = false, airborne = false;
		for (std::size_t i = 0; i < c.walks.size(); ++i) {
			const WalkLine &w = c.walks[i];
			if (t < w.t0 || w.points.empty()) {   // (a way not found yet: the film is being loaded)
				if (w.style == WalkLine::Jump && !w.points.empty()) crouch = std::max(crouch, smoothstep(w.t0 - 0.34, w.t0 - 0.08, t) * (1.0 - smoothstep(w.t0 - 0.08, w.t0, t)));   // (gathering itself)
				break;
			}
			if (w.style == WalkLine::Jump) {   // an arc from where it was to there; it faces where it jumps
				const Vec3 from = where, to = story(w.points[0].first, w.heights.empty() ? c.start[1] : w.heights[0], w.points[0].second);
				if (std::hypot(to.x - from.x, to.z - from.z) > 1e-6) heading = rig::headingOf(to - from);
				const double flight = flightOf(w);
				if (t < w.t0 + flight) {
					const double u = (t - w.t0) / flight;
					where = lerp(from, to, u) + Vec3{0.0, 4.0 * w.height * c.scale * u * (1.0 - u), 0.0};
					airborne = true;
					break;
				}
				where = to;
				crouch = std::max(crouch, 1.0 - smoothstep(w.t0 + flight + 0.04, w.t0 + flight + 0.42, t));   // (the landing squashes it)
				continue;
			}
			const rig::PathWalk path = pathOf(c, i);
			const rig::PathWalk::At a = path.at(t);
			where = a.at;
			heading = a.yaw;
			steps = a.steps;
			moving = a.moving;
			running = w.style == WalkLine::Run ? 1.0 : 0.0;
			if (moving) break;
		}
		CharacterMoment m;
		m.running = running;
		m.crouch = crouch;
		m.airborne = airborne;
		for (const Character::Sit &st : c.sits) {
			const double s = smoothstep(st.t0, st.t0 + 0.7, t) * (1.0 - smoothstep(st.t1, st.t1 + 0.7, t));
			if (s > m.sit) m.sit = s, m.seat = st.height * c.scale;
		}
		m.frame = mul(translate(where), rotate({0.0, 1.0, 0.0}, heading));
		m.steps = steps;
		m.moving = moving;
		m.t = t;
		m.gestures = &c.gestures;
		m.detail = c.detail;
		m.scale = c.scale;
		m.seed = seedOf(c.name);
		m.colours = &c.colours;
		if (at) *at = where;
		return m;
	}
	// Its feet held where they came down (CharacterMoment::lockedFeet), when its kind says where its gait puts them and it has walked.
	void lockFeet(const Character &c, double t, CharacterMoment &m) const {
		const CharacterKind &kind = characterKinds().at(c.kind);
		if (!kind.footAt || !kind.plantedAt || (m.steps <= 0.0 && !m.moving) || m.airborne) return;
		rig::FootLock k;
		k.frameAt = [this, &c](double u) { return momentOf(c, u).frame; };
		k.stepsAt = [this, &c](double u) { return momentOf(c, u).steps; };
		const double scale = c.scale;
		k.footAt = [&kind, scale](double steps, int s) { return kind.footAt(steps, s) * scale; };
		const bool runs = std::any_of(c.walks.begin(), c.walks.end(), [](const WalkLine &w) { return w.style == WalkLine::Run; });
		if (runs)   // (a run's gait is wider about the feet's rest)
			k.footAtTime = [this, &c, &kind, scale](double u, int s) {
				const CharacterMoment mu = momentOf(c, u);
				const Vec3 rest = kind.footAt(0.0, s), f = kind.footAt(mu.steps, s);
				return (rest + (f - rest) * (1.0 + (kRunStride - 1.0) * mu.running)) * scale;
			};
		if (const film_paths::Terrain *ground = terrainFor(c)) {   // (on uneven ground: a planted foot on the floor under it)
			const double bodyY = transformPoint(m.frame, {0.0, 0.0, 0.0}).y;
			const double step = stepOf(c);
			k.groundAt = [ground, bodyY, step](double x, double z) { return ground->floorAt(-x, z, bodyY, step); };
		}
		const double land = landedAt(c, t);   // (after a jump its feet are held from where it landed, not from before it jumped)
		if (t - land < k.lookBack) k.lookBack = std::max(1.0 / 60.0, t - land);
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
			for (std::size_t i = 0; i < c.walks.size(); ++i) {
				if (c.walks[i].style == WalkLine::Jump) {   // the landing
					FilmEvent e;
					e.t = c.walks[i].t0 + flightOf(c.walks[i]);
					e.kind = EventKind::Footfall;
					e.seed = ++seed;
					const Vec3 at = endOf(c, i);
					const CameraView v = viewAt(e.t);
					e.pan = panOf(v, at);
					e.strength = std::min(1.0, 1.4 * audibility(length(at - v.eye)));
					out.push_back(e);
					continue;
				}
				for (const rig::PathWalk::Footfall &f : pathOf(c, i).footfalls(-1e30, 1e30)) {
					FilmEvent e;
					e.t = f.t;
					e.kind = EventKind::Footfall;
					e.seed = ++seed;
					const CameraView v = viewAt(f.t);
					e.pan = panOf(v, f.at);
					e.strength = audibility(length(f.at - v.eye)) * (c.walks[i].style == WalkLine::Run ? 1.3 : 1.0);
					out.push_back(e);
				}
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
		for (const Spec::Handling &h : spec.handling) {   // a hand meets a prop
			FilmEvent e;
			e.t = h.t;
			e.kind = EventKind::Touch;
			e.seed = ++seed;
			const CameraView v = viewAt(h.t);
			const Vec3 at = propAt(h.prop, h.t);
			e.pan = panOf(v, at);
			e.strength = 0.8 * audibility(length(at - v.eye));
			out.push_back(e);
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
	bool hasEvents() const { return !spec.characters.empty() || !spec.bounces.empty() || !spec.topples.empty(); }   // (props are handled by characters)

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
	// How loud the characters are speaking at t, 0..1: their lines and their recorded voices (the music is ducked under it).
	double speechAt(double t) const {
		double v = 0.0;
		for (std::size_t i = 0; i < spec.lines.size(); ++i)
			if (t >= spec.lines[i].t - 0.1 && t <= speech::endOf(spoken[i], spec.lines[i].t) + 0.1) v = std::max(v, speech::loudnessAt(spoken[i], t));
		for (const Character &c : spec.characters)
			if (!c.extra) v = std::max(v, music_cues::voiceAt(spec.key, c.name, t, shotStarts(), shotNameList()));
		return std::min(1.0, v);
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

	std::string sceneAt(double t) const { return worldAt(headerAt(t), t); }

	// ---- how shots give way to each other ----
	std::vector<Transition> transitions() const {
		std::vector<Transition> out;
		for (const Shot &shot : spec.shots)
			if (shot.transition.length > 0.0) {
				Transition tr = shot.transition;
				tr.at = shot.start;
				out.push_back(tr);
			}
		if (spec.fadeOut > 0.0) out.push_back({Transition::Fade, spec.length, 2.0 * spec.fadeOut});   // (half of a fade is before its cut: all of it before the end)
		return out;
	}
	// The scene at t seen by the other side of the dissolve or wipe it is in (the shot after the cut before it, the shot before after it); outside one, the scene itself.
	std::string sceneAcross(double t) const {
		for (std::size_t k = 1; k < spec.shots.size(); ++k) {
			const Transition &tr = spec.shots[k].transition;
			const double cut = spec.shots[k].start;
			if (tr.length <= 0.0 || tr.kind == Transition::Fade || std::abs(t - cut) > 0.5 * tr.length) continue;
			std::string lookAt, camera;
			cameraAt(t, lookAt, camera, static_cast<int>(t < cut ? k : k - 1));
			return worldAt(spec.shots.empty() ? scene.header : scene.headerBefore + lookAt + scene.headerBetween + camera + scene.headerAfter + "WorldBegin\n", t);
		}
		return sceneAt(t);
	}
	// The time that picks the shot whose camera sceneAcross(t) uses (what a cameras file changes is that shot's).
	double acrossShotTime(double t) const {
		for (std::size_t k = 1; k < spec.shots.size(); ++k) {
			const Transition &tr = spec.shots[k].transition;
			const double cut = spec.shots[k].start;
			if (tr.length <= 0.0 || tr.kind == Transition::Fade || std::abs(t - cut) > 0.5 * tr.length) continue;
			return t < cut ? cut : spec.shots[k - 1].start;
		}
		return t;
	}

	// The scene with this header at t: the set, its animated blocks, its dressing, its sky and its characters.
	std::string worldAt(const std::string &header, double t) const {
		std::string out = header;
		if (spec.hasSky) out += "# the sun and sky at " + std::to_string(dressing::hourAt(spec.sky, t, spec.length)) + " h (film_dressing.h)\n" + dressing::skyText(spec.key, spec.sky, t, spec.length);
		out += dressingAt(t);
		if (!spec.weather.empty() || !spec.emitters.empty()) {   // weather round the camera, and particles (film_particles.h)
			Vec3 eye, target;
			double fov = 40.0;
			film_subjects::cameraOf(header, eye, target, fov);
			const Vec3 at{-eye.x, eye.y, eye.z};   // (pbrt's)
			for (const particles::Weather &w : spec.weather) out += particles::weatherText(w, t, at, spec.shutter / std::max(1, spec.fps));
			for (const particles::Emitter &e : spec.emitters) out += particles::emitterText(e, t);
		}
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
			CharacterMoment m = actingMoment(c, t);
			m.trail = trailOf(c, t, m);
			lockFeet(c, t, m);
			m.voice = music_cues::voiceAt(spec.key, c.name, t, shotStarts(), shotNameList(), &m.mouth);   // (a recording's loudness and mouth shapes)
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
		if (const Spec::Prop *pr = prop(name)) {   // a prop: moved from where the scene has it to where it is (pbrt's: x mirrored back)
			const Vec3 at = propAt(name, t);
			out += "  Translate " + num(-at.x - pr->home.x) + " " + num(at.y - pr->home.y) + " " + num(at.z - pr->home.z) + "\n";
		}
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
	// A move's camera at t: about its character, the way along it eased (an orbit at an even pace), in story space and then pbrt's.
	camera_keyframes::Key moveKey(const Shot &shot, double t, double span) const {
		const Shot::Move &mv = shot.move;
		const Character &c = *character(mv.who);
		Vec3 feet;
		const CharacterMoment m = momentOf(c, t, &feet);
		const Vec3 up{0.0, 1.0, 0.0}, front = normalize(transformPoint(m.frame, {0.0, 0.0, 1.0}) - feet), right = cross(front, up);
		const Vec3 chest = transformPoint(m.frame, characterKinds().at(c.kind).chest * c.scale);
		const double u = std::min(1.0, std::max(0.0, (t - shot.start) / span)), eased = smoothstep(0.0, 1.0, u);
		Vec3 eye = chest + front * 3.0;
		switch (mv.kind) {
			case Shot::Move::Orbit: {   // round it from `from` degrees off its front, `degrees` over the shot (to its left: positive)
				const double a = (mv.from + mv.degrees * u) * kPi / 180.0;
				eye = feet + (front * std::cos(a) + right * -std::sin(a)) * mv.radius + up * mv.height;
				break;
			}
			case Shot::Move::Crane: eye = feet + front * mv.distance + up * (mv.low + (mv.high - mv.low) * eased); break;
			case Shot::Move::Push: eye = Vec3{chest.x, feet.y + mv.height, chest.z} + front * (mv.near0 + (mv.near1 - mv.near0) * eased); break;
			case Shot::Move::Track: eye = feet + right * mv.side + up * mv.height; break;
			default: break;
		}
		camera_keyframes::Key key;
		key.pos[0] = -eye.x, key.pos[1] = eye.y, key.pos[2] = eye.z;
		key.target[0] = -chest.x, key.target[1] = chest.y, key.target[2] = chest.z;
		key.fov = mv.fov;
		return key;
	}
	void cameraAt(double t, std::string &lookAt, std::string &camera, int shotIndex = -1) const {
		std::size_t k = 0;
		for (std::size_t i = 0; i < spec.shots.size(); ++i)
			if (t >= spec.shots[i].start) k = i;
		if (shotIndex >= 0) k = static_cast<std::size_t>(shotIndex);   // (the camera of another shot: the other side of a dissolve)
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
		} else if (shot.move.kind != Shot::Move::None) {
			key = moveKey(shot, t, span);
		} else {
			key = camera_keyframes::at(shot.path, std::min(1.0, std::max(0.0, (t - shot.start) / span)), span);
		}
		if (shot.handheld > 0.0) {   // a hand-held camera: a slow wander of where it is and what it looks at (sums of sines: the same every run)
			const double a = shot.handheld, p = static_cast<double>(k) * 1.7;
			const double dx = std::sin(1.31 * t + p) + 0.5 * std::sin(3.07 * t + 2.0 * p), dy = std::sin(1.73 * t + 0.4 * p) + 0.5 * std::sin(2.71 * t + p);
			const double dz = std::sin(0.97 * t + 3.0 * p), tx = std::sin(1.13 * t + 5.0 * p) + 0.4 * std::sin(4.1 * t + p), ty = std::sin(1.57 * t + 4.0 * p);
			key.pos[0] += 0.012 * a * dx, key.pos[1] += 0.010 * a * dy, key.pos[2] += 0.008 * a * dz;
			key.target[0] += 0.03 * a * tx, key.target[1] += 0.025 * a * ty;
		}
		if (!shot.focus.empty() || shot.aperture > 0.0) {   // the focus on who the shot says (pulled from one to the next over 0.6 s), or on what the move is about
			const Vec3 eye{-key.pos[0], key.pos[1], key.pos[2]};
			const auto distanceTo = [&](const std::string &who) {
				const Character *c = character(who);
				return c ? length(bodyPoint(*c, t, characterKinds().at(c->kind).head) - eye) : 0.0;
			};
			double d = 0.0;
			if (!shot.focus.empty()) {
				d = distanceTo(shot.focus.front().second);
				for (std::size_t f = 1; f < shot.focus.size(); ++f) d += (distanceTo(shot.focus[f].second) - d) * smoothstep(shot.focus[f].first, shot.focus[f].first + 0.6, t);
			} else if (!shot.move.who.empty()) {
				d = distanceTo(shot.move.who);
			}
			if (d > 0.0) {
				key.lensRadius = shot.aperture > 0.0 ? shot.aperture : 0.02;
				key.focus = d;
			}
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
