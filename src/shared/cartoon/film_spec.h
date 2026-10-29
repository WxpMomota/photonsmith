#pragma once
// film_spec.h -- what a film file says (film_file.h): its shots, characters, kinds of character, animations and nature, as read by its parser (film_parse.h).

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <limits>
#include <mutex>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "../camera_keyframes.h"
#include "../toon_look.h"
#include "film_dressing.h"
#include "film_faces.h"
#include "film_marks.h"
#include "film_cloth.h"
#include "film_particles.h"
#include "film_paths.h"
#include "film_acting.h"
#include "film_speech.h"
#include "shot_lighting.h"
#include "film_tts.h"
#include "film_subjects.h"
#include "film_clouds.h"
#include "film_fire.h"
#include "film_giants.h"
#include "film_glow.h"
#include "film_ice.h"
#include "film_wind.h"
#include "film_ground.h"
#include "film_space.h"
#include "film_planets.h"
#include "film_cosmos.h"
#include "film_voyage.h"
#include "film_flowers.h"
#include "film_geology.h"
#include "film_soundscape.h"
#include "film_forest.h"
#include "film_skyfx.h"
#include "film_cache.h"
#include "film_camera.h"
#include "film_terrain.h"
#include "film_underwater.h"
#include "film_wildlife.h"
#include "film_water.h"
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
	// (`fly`) a drone's flight over waypoints (pbrt's x z), a height above the land (smoothed so it does not bob over every rise), looking ahead and down or at a point
	std::vector<std::pair<double, double>> fly;
	double flyHeight = 60.0, flyFov = 0.0;
	bool flyLook = false;
	Vec3 flyAt;
	std::vector<std::pair<double, std::string>> focus;   // who is in focus from when (the first from the shot's start)
	double aperture = 0.0;
	cosmos::SpaceCam spaceCam;   // (`spacecam`) a journey in space (film_voyage.h)
	shot_lighting::Lighting lighting;   // (`lighting`) key, fill and rim lights round the subject, from the camera
	std::string lightOn;                // ... on this character's chest ("": what the camera looks at)
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
	double pitch = 0.0, rate = 1.0;      // its voice: so many semitones up from its kind's, and how fast it speaks (every line of it; a line's own are on top)
	std::string voice;                   // what its lines are said in (`voice`): "babble", "system" or a word of a computer voice's name (film_tts.h); "": the film's `voices`
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
	double going = 0.0;      // how much it is walking, 0 .. 1, eased over 0.3 s as a walk starts and ends (a rigged model fades between its idle and walk clips by it)
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
	bool trails = false;   // something on it hangs and swings behind it (CharacterMoment::trail: hundreds of moments of its walk worked out each frame, so only when it is used)
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

}  // namespace film_file
}  // namespace cartoon

#include "film_rigged.h"   // (kinds made from rigged glTF characters: it needs CharacterKind and characterKinds above)

namespace cartoon {
namespace film_file {

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
	// the land (`terrain`): what is placed, scattered or brought in stands on it (their heights are above it)
	terrain::Spec terrain;
	bool hasTerrain = false;
	// water (`water`, `river`, `waterfall`)
	std::vector<water::Sea> seas;
	std::vector<water::River> rivers;
	std::vector<water::Fall> falls;
	surf::Swell swell;   // surf on the sea's shore (`surf`: film_surf.h)
	// clouds you can see round (`clouds`)
	std::vector<clouds::Clouds> cloudLayers;
	std::vector<clouds::FogBank> fogBanks;   // fog lying on the land (`fogbank`)
	// forests (`forest`), and their trees (planted when the film is loaded, on the land as it is drawn)
	std::vector<forest::Forest> forests;
	std::vector<flowers::Meadow> meadows;   // wild flowers (`flowers`: film_flowers.h)
	// the land in action (film_geology.h): geysers, rockfalls and avalanches (their ways down worked out when the film is loaded), earthquakes
	std::vector<geology::Geyser> geysers;
	std::vector<geology::Rockfall> rockfalls;
	std::vector<geology::Avalanche> avalanches;
	std::vector<geology::Quake> quakes;
	std::string biome;                      // the land as a biome (`biome`): its ground (film_terrain.h biomePaint), its plants (film_forest.h's mixes), its sound
	int biomeTrees = -1;                    // (how many plants it brings: below 0 by the land's size)
	std::vector<std::vector<forest::Tree>> planted;
	// animals (`flock`, `herd`)
	std::vector<wildlife::Flock> flocks;
	std::vector<wildlife::Herd> herds;
	// the sky's spectacles (`aurora`, `lightning`, `meteors`, `rainbow`)
	std::vector<skyfx::Aurora> auroras;
	std::vector<skyfx::Lightning> lightnings;
	std::vector<skyfx::Meteors> meteors;
	struct Rainbow {
		double strength = 0.8, t0 = 0.0, t1 = 1e30;
	};
	std::vector<Rainbow> rainbows;
	bool tracedCaustics = false;   // (`caustics traced`: film_caustics.h) the net of light under water traced from the surface
	dressing::SkyOptics optics;   // (`optics`: halo, sun dogs, fogbow, crepuscular rays, alpenglow, the green flash)
	// how fast nature runs (`timelapse`: the clouds, the water, the aurora, the wind in the trees), and the exposure (`exposure`: a factor, or `auto` - following the light)
	double timelapse = 1.0, exposure = 1.0;
	// slow motion (`slowmo T0 T1 RATE`): nature's time running at RATE of the film's between T0 and T1 (eased in and out over 0.4 s); nature's time at every
	// kNatureStep seconds of the film, worked out when the film is loaded (empty without slow motion: then it is the film's times the timelapse)
	struct SlowMo {
		double t0 = 0.0, t1 = 0.0, rate = 0.25;
	};
	std::vector<SlowMo> slowmos;
	static constexpr double kNatureStep = 0.01;
	std::vector<double> natureClock;
	bool exposureAuto = false;
	// weather that changes the land (`wet`, `snowcover`: each an amount reached from T0 to T1), its gusts (`gusts`) and a mist round the camera (`mist`)
	struct Ramp {
		double amount = 1.0, t0 = 0.0, t1 = 0.0;
		double at(double t) const { return t1 > t0 ? amount * smoothstep(t0, t1, t) : amount; }
	};
	std::vector<Ramp> wetness, snowcover;
	dressing::Gusts gusts;
	bool hasMist = false;
	// a fog lying in the low ground (`fog`): the camera medium of type "heightfog" (height_fog.h), dense below `top` (pbrt's y), thinning over `thickness` above it; it lifts
	// (thins away and sinks) from liftT0 to liftT1
	struct ValleyFog {
		bool on = false;
		double density = 0.02, top = 0.0, thickness = 0.0;
		bool topSet = false;
		double liftT0 = -1.0, liftT1 = -1.0;
	} fog;
	// the air (`air`): aerial perspective - the camera medium of type "heightfog" with the atmosphere's scattering (atmosphere.h: Rayleigh's blue, the haze's grey),
	// thinning with height, out to `radius` round the camera (the sky's picture beyond)
	struct Air {
		bool on = false;
		double strength = 1.0, haze = -1.0, radius = 0.0;   // (haze below 0: the sky's; radius 0: four and a half times the terrain's size, or 8 km)
	} air;
	nature::Mist mist;
	// a storm (`storm`): rain, snow (a blizzard) or sand (a dust storm), building up from t0 to its strength and dying away to t1 (film seconds), blown by a wind of `speed`
	// m/s from `wind` (a facing)
	struct Storm {
		bool on = false;
		std::string kind = "rain";
		double strength = 1.0, t0 = 0.0, t1 = 1e30, wind = 90.0, speed = 12.0;
		bool windSet = false, speedSet = false;
		double build() const { return t1 < 1e29 ? std::clamp((t1 - t0) / 3.0, 2.0, 8.0) : 6.0; }
		double levelAt(double t) const {   // 0 .. strength
			if (!on) return 0.0;
			return strength * smoothstep(t0, t0 + build(), t) * (t1 < 1e29 ? 1.0 - smoothstep(t1 - build(), t1, t) : 1.0);
		}
	} storm;
	// under the sea (`underwater`, `kelp`, `coral`)
	bool hasUnderwater = false;
	underwater::Water underwater;
	std::vector<underwater::Kelp> kelps;
	std::vector<underwater::Coral> corals;
	// fire, lava and smoke (`fire`, `lava`, `volcano`, `wildfire`)
	std::vector<fire::Fire> fires;
	std::vector<fire::Lava> lavas;
	std::vector<fire::Volcano> volcanoes;
	std::vector<fire::Wildfire> wildfires;
	// the sea's giants (`whale`, `dolphins`)
	std::vector<giants::Whale> whales;
	std::vector<giants::Dolphins> pods;
	// living light (`bioluminescence`, `fireflies`, `fungi`)
	glow::Bioluminescence bio;
	std::vector<glow::Fireflies> fireflies;
	std::vector<glow::Fungi> fungi;
	// ice (`glacier`, `iceberg`, `seaice`; `waterfall ... frozen`)
	std::vector<ice::Glacier> glaciers;
	std::vector<ice::Iceberg> icebergs;
	ice::SeaIce seaIce;
	// the wind (`wind`, `tornado`)
	wind::Wind wind;
	std::vector<wind::Tornado> tornadoes;
	// the ground close up (`closeup`, `tracks`)
	ground::Closeup closeup;
	// the universe (round 10: film_parse_space.h)
	space::Space space;
	std::vector<planets::Planet> planets;
	std::vector<cosmos::Nebula> nebulae;
	std::vector<cosmos::Galaxy> galaxies;
	std::vector<cosmos::Star> stars;
	std::vector<cosmos::BlackHole> blackHoles;
	std::vector<cosmos::Comet> comets;
	std::vector<cosmos::Belt> belts;
	std::vector<cosmos::Remnant> remnants;
	std::vector<cosmos::Ship> ships;
	std::vector<cosmos::Astronaut> astronauts;
	std::vector<cosmos::Descent> descents;
	std::vector<ground::Track> tracks;
	dressing::Sky sky;
	std::vector<dressing::Ambience> ambience;
	bool muteBirds = false;   // (`mute birds`: no birdsong, no eagles' cries, no geese - whatever flies in the picture)
	bool draftMusic = true;   // (`music none`: no drafted score when there is no cues file)
	toon_look::Style toon;    // the toon look (`toon`): written into every frame's header as its `# @rt-toon` line
	std::string voices;       // what the characters' lines are said in unless a character says (`voices`): "" or "babble", "system", a word of a computer voice's name
	bool actingWords = false; // `acting words`: each line brings the gesture and the expression its words ask for (film_acting.h), where the film gives none
	// hands on the set and on each other (`touch`, `hold`, `hug`): reached for half a second before T0, held to T1, let go over half a second
	struct Contact {
		enum Kind { Touch, Hold, Hug } kind = Touch;
		std::string who, other;   // (hold, hug: the other character)
		double t0 = 0.0, t1 = 0.0;
		Vec3 at;                  // (touch: where, pbrt's x y z)
		int hand = 1;             // (touch: 0 the left, 1 the right)
		int line = 0;
	};
	std::vector<Contact> contacts;
	// kinds made from rigged glTF characters (`rigged KIND FILE ...`: film_rigged.h), added when the film is loaded
	struct Rigged {
		std::string kind, file;
		std::filesystem::path dir;   // where `file` is from ("": beside the film file, else the library's)
		RigOptions options;
		int line = 0;
	};
	std::vector<Rigged> rigged;
	// titles and credits: a title card over the opening (`title`, `episode`), cards of credits after the story over black (`credits`, `credit`), a theme under the title
	bool hasTitle = false;
	std::string title;              // "": the film's name
	double titleAt = 0.5, titleFor = 3.0;
	int episode = 0;
	std::string episodeName;
	bool credits = false;
	std::vector<std::string> creditLines;
	struct CreditName {
		std::string name, kind;
		bool voiced = false;        // its lines in a computer's voice
	};
	std::vector<CreditName> creditCast;   // the cast on the credits' cards, extras left out (a film in parts: every part's, in order)
	double storyEnd = 0.0;          // where the story ends and the credits begin (the length without them)
	std::string theme;              // "jingle" or a WAV of the library ("": none)
	double themeGain = 0.8;
	std::string filmName;           // (a part of a film in parts: the whole film's name, for its title and credits)
	// weather and particles (`weather`, `particles`, `burst`)
	std::vector<particles::Weather> weather;
	std::vector<particles::Emitter> emitters;
	// cartoon marks drawn over the frames (`mark`: film_marks.h)
	std::vector<film_marks::Mark> marks;
	// cloth: flags, capes, sheets and curtains (`cloth`: film_cloth.h)
	std::vector<cloth::Cloth> cloths;
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
inline std::string lowerWord(std::string w) {
	for (char &c : w) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return w;
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

namespace detail {

// A file of the library of casts and sets (`cast`, `set`): library/NAME.EXT beside the film file, else film_assets/library/NAME.EXT where the scenes are. "" when there is none.
inline std::string libraryFile(const std::string &name, const std::string &ext, const std::string &filmFile) {
	std::error_code ec;
	const std::filesystem::path beside = std::filesystem::path(filmFile).parent_path() / "library" / (name + ext);
	if (std::filesystem::exists(beside, ec)) return beside.string();
	return film_cache::asset("library/" + name + ext);
}
// A line without its comment: '#' starts one - but not a colour, #rrggbb.
inline std::string withoutComment(std::string line) {
	const auto quoted = [&line](std::size_t at) {   // (a # inside "..." is the text's: `say kai 3 "Room #4"`)
		bool in = false;
		for (std::size_t k = 0; k < at; ++k) in = line[k] == '"' ? !in : in;
		return in;
	};
	for (std::size_t hash = line.find('#'); hash != std::string::npos; hash = line.find('#', hash + 1)) {
		if (quoted(hash)) continue;
		bool colour = hash + 7 <= line.size() && (hash + 7 == line.size() || std::isspace(static_cast<unsigned char>(line[hash + 7])));
		for (std::size_t k = hash + 1; colour && k < hash + 7; ++k) colour = std::isxdigit(static_cast<unsigned char>(line[k])) != 0;
		if (!colour) {
			line.erase(hash);
			break;
		}
	}
	return line;
}
inline std::string textOf(const std::string &file) {
	std::ifstream in(file, std::ios::binary);
	return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

}  // namespace detail

}  // namespace film_file
}  // namespace cartoon
