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
#include <fstream>
#include <string>
#include <vector>

#include "../pbrt_asset_check.h"

#include "ball_parade.h"
#include "candle.h"
#include "dandelion.h"
#include "domino_run.h"
#include "fireflies.h"
#include "forest_dawn.h"
#include "forest_dawn_score.h"
#include "turtle_escape_score.h"
#include "crystal_heart_score.h"
#include "tengwang_score.h"
#include "nomai_score.h"
#include "lamp_and_ball.h"
#include "little_robot.h"
#include "meet_pip.h"
#include "newtons_cradle.h"
#include "film_file.h"
#include "film_kinds.h"
#include "film_parts.h"
#include "film_score.h"
#include "caption_file.h"
#include "film_cache.h"
#include "orrery.h"
#include "paper_boat.h"
#include "pip_and_the_moon.h"
#include "short_scores.h"
#include "shot_cameras.h"
#include "music_cues.h"

namespace cartoon {

constexpr const char *kCameraPathPrefix = "cartoon:";
constexpr const char *kStillSlugPrefix = "cartoon-";   // the scene a film's still is listed as: "cartoon-fireflies" (and its file, cartoon-fireflies.pbrt)

// The words a film's tags are chosen from. They name what a film shows or how it is lit, so that the scene list's search can find "metal" or "dusk".
constexpr const char *kTagVocabulary[] = {"character", "physics", "particles", "metal", "water", "subsurface", "translucent", "emissive", "backlight", "dof", "reflections", "night", "dusk", "fog", "nature", "dawn", "comedy", "glass"};

// The films. `meta.id` is the video preset's number (V<id>): give a new film the next number and never reuse one. `meta.secondsPerFrame` is measured by scripts/measure_cartoons.py.
inline const Film kFilms[] = {
	{"lamp-and-ball", "Lamp and Ball",
	 "A small yellow desk lamp wakes up in the dark, notices a red ball, hops over, nudges it, watches it bounce off the books and roll home, and tucks its shade over it.",
	 lamp_and_ball::kDuration, lamp_and_ball::kFps, &lamp_and_ball::sceneAt, &lamp_and_ball::headerAt, &lamp_and_ball::shotStarts, 9.1,
	 {7, "character,physics,emissive,night", "The desk", lamp_and_ball::kSamples, 3.7}, &scores::lampAndBall},
	{"ball-parade", "Ball Parade",
	 "Seven glossy balls drop in one after another, bounce in a travelling wave on a checker floor with squash and stretch, and settle one by one into a row.",
	 ball_parade::kDuration, ball_parade::kFps, &ball_parade::sceneAt, &ball_parade::headerAt, &ball_parade::shotStarts, 4.6,
	 {8, "physics,reflections", "The row of balls", ball_parade::kSamples, 3.5}, &scores::ballParade},
	{"little-robot", "The Little Robot",
	 "A small robot wakes in a junkyard at dusk, finds a seedling dying in a tin can, waters it, and watches it bloom into a glowing flower.",
	 little_robot::kDuration, little_robot::kFps, &little_robot::sceneAt, &little_robot::headerAt, &little_robot::shotStarts, 25.5,
	 {9, "character,emissive,dusk,dof", "Wide establishing|Medium on the waking robot|Tracking to the seedling|Close on the watering and the bloom|Wide crane up", little_robot::kSamples, 2.2}, &scores::littleRobot},
	{"fireflies", "Fireflies",
	 "A night meadow lit by fireflies: a lone firefly wanders, a second arrives, their flashes drift into step, they circle and spiral up to the moon as the meadow wakes.",
	 fireflies::kDuration, fireflies::kFps, &fireflies::sceneAt, &fireflies::headerAt, &fireflies::shotStarts, 13.0,
	 {10, "emissive,particles,night,dof", "Ground-level macro|The meeting|Orbit round the pair|Crane up over the meadow", fireflies::kSamples, 2.7}, &scores::fireflies},
	{"newtons-cradle", "Newton's Cradle",
	 "Five chrome balls on strings: one is lifted and let go, the far ball flies out and back, and the swing passes from side to side and slowly dies, with the room in the chrome.",
	 newtons_cradle::kDuration, newtons_cradle::kFps, &newtons_cradle::sceneAt, &newtons_cradle::headerAt, &newtons_cradle::shotStarts, 5.0,
	 {11, "metal,reflections,physics,dof", "The whole cradle|Close-up of the strike|Wide pull back", newtons_cradle::kSamples, 2.7}, &scores::newtonsCradle},
	{"domino-run", "Domino Run",
	 "A red marble taps the first of 130 rainbow dominoes laid in a spiral; the wave runs inwards, chased by the camera, and the last domino lights a small lamp in the middle.",
	 domino_run::kDuration, domino_run::kFps, &domino_run::sceneAt, &domino_run::headerAt, &domino_run::shotStarts, 9.0,
	 {12, "physics,emissive,dof", "The spiral and the marble|The chase|Close-up at the lamp|Rising pull back", domino_run::kSamples, 3.5}, &scores::dominoRun},
	{"paper-boat", "The Paper Boat",
	 "A folded paper boat sails across a pond in the rain at dusk: every drop starts rings of ripples that rock the boat and cross one another, the rain eases, and the boat sails on towards the low sun.",
	 paper_boat::kDuration, paper_boat::kFps, &paper_boat::sceneAt, &paper_boat::headerAt, &paper_boat::shotStarts, 10.0,
	 {13, "water,reflections,dusk,backlight", "Low beside the water|Chase from above|Wide pull back", paper_boat::kSamples, 4.3}, &scores::paperBoat},
	{"orrery", "The Orrery",
	 "A turn round a brass model of a small solar system: a glowing sun, five enamel planets on arms (one with a moon, one with a ring), engraved orbits and a pair of meshing gears, all lit by the sun alone.",
	 orrery::kDuration, orrery::kFps, &orrery::sceneAt, &orrery::headerAt, &orrery::shotStarts, 6.0,
	 {14, "metal,reflections,emissive", "Wide orbit|Close-up on the third planet|Rising look down", orrery::kSamples, 3.0}, &scores::orrery},
	{"dandelion", "The Dandelion",
	 "A gust blows the seeds off a dandelion clock one by one, windward side first, and they drift away on their little parachutes, glowing where the low sun shines through.",
	 dandelion::kDuration, dandelion::kFps, &dandelion::sceneAt, &dandelion::headerAt, &dandelion::shotStarts, 7.5,
	 {15, "translucent,backlight,particles,dof,dusk", "Close beside the seed head|Following the seeds|Wide pull back", dandelion::kSamples, 6.2}, &scores::dandelion},
	{"candle", "The Candle",
	 "One candle burns down in front of out-of-focus fairy lights: the flame flickers, light glows through the wax, drips run down its side, and at the end the flame dies in a thread of smoke.",
	 candle::kDuration, candle::kFps, &candle::sceneAt, &candle::headerAt, &candle::shotStarts, 5.0,
	 {16, "subsurface,emissive,dof,night", "Portrait of the flame|Macro on the wax and drips|Pull back as it dies", candle::kSamples, 4.7}, &scores::candle},
	{"meet-pip", "Meet Pip",
	 "Pip, a small round blue creature with two antennae, says hello, shows what its face can do (surprise, sadness, a scowl, a hop of joy), follows a glowing orb with its eyes and walks off across the stage.",
	 meet_pip::kDuration, meet_pip::kFps, &meet_pip::sceneAt, &meet_pip::headerAt, &meet_pip::shotStarts, 6.0,
	 {17, "character,emissive,dof", "Hello and the faces|Following the orb|Side-on walk", meet_pip::kSamples, 2.8}, &scores::meetPip},
	{"pip-and-the-moon", "Pip and the Moon",
	 "Pip finds the moon in a pond, tries to touch it and breaks it, scoops it up in a bucket and dances with it, trips on the way home and loses it, and is shown the real one by a firefly. With music and sounds.",
	 pip_moon::kDuration, pip_moon::kFps, &pip_moon::sceneAt, &pip_moon::headerAt, &pip_moon::shotStarts, 18.0,
	 {18, "character,emissive,water,reflections,night", "Moon and reflection|Stroll along the dock|The moon in the water|Sorrow and the idea|Running for the bucket|Scooping the moon|The dance|Carrying it home|The fall|Alone at the water|The firefly|Under the real moon|Pull back over the pond", pip_moon::kSamples, 1.7, true}, &pip_moon::soundtrack},
	{"forest-dawn", "Forest Dawn",
	 "A misty forest clearing from the blue before sunrise to the first warm light: the last fireflies go out, the sun comes up behind the hills and shines between the trunks in shafts through the mist, dew catches it on the grass, and leaves fall through the beams.",
	 forest_dawn::kDuration, forest_dawn::kFps, &forest_dawn::sceneAt, &forest_dawn::headerAt, &forest_dawn::shotStarts, 14.0,
	 {19, "fog,nature,dawn,translucent,backlight", "Into the blue clearing|Against the rising sun|Dew on the grass|Leaves in the beams|Crane up as the mist thins", forest_dawn::kSamples, 6.0, true}, &forest_dawn::soundtrack},
	{"turtle-escape", "The Great Turtle Escape",
	 "A tiny turtle escapes its perfect tank like a spy breaking out of prison - rocks, a thermometer, a hose and a robot vacuum stand in its way - and dives into a pool that turns out to be the tub its owner filled for it.",
	 turtle_escape::kDuration, turtle_escape::kFps, &turtle_escape::sceneAt, &turtle_escape::headerAt, &turtle_escape::shotStarts, 199.0,
	 {20, "character,comedy,water,glass,night", "Wide on the room|The plant|The thermometer|Pellets|Through the glass: Perfect|The turtle stares|Keys|The door|Towards the door|Towards the top|Back to the door|The eyes narrow|"
	  "The glass|Water to rim|The thermometer, close|The intake|The hose|The plan|The dive|The push|Again|The charge|Exhausted|The clock|Building the pile|The climb|The crash|The bubble|Plan B|The bite and the pop|The idea|The ramp|A foot on the rim|Lights out|"
	  "The hose|Onto the slate|Hanging|The canyon|Eight inches|The climb|Over the top|Freedom|The slide|The towels|Across the floor|The vacuum wakes|Like a tank|Under the sofa|Beneath|The carpet cliff|The fibres|The far side|Water|"
	  "The endless crawl|The box|The pool|Looking back|A smile|Splash|Swimming|Home|Something is wrong|The empty slate|The thermometer|The rocks|The hose|The bags|Inside the tank|Behind the tank|Under the table|Behind the sofa|Turtle?|The tub|Black",
	  turtle_escape::kSamples, 3.0, true, turtle_escape::kWidth, turtle_escape::kHeight}, &turtle_escape::soundtrack},
	{"crystal-heart", "The Crystal Heart",
	 "A valley of colossal glowing crystals rising out of a mirror lake, from the deep blue before dawn to sunrise: the sun breaks through the ridge and its light falls between the spires through the mist, and the giant crystal floating over the valley wakes, beats and ignites, its light cutting through the mist in beams. With music.",
	 crystal_heart::kDuration, crystal_heart::kFps, &crystal_heart::sceneAt, &crystal_heart::headerAt, &crystal_heart::shotStarts, 15.0,
	 {21, "glass,emissive,fog,water,dawn", "The still lake|Among the spires|First light|The Heart|Ignition|The valley at sunrise", crystal_heart::kSamples, 3.5, false, crystal_heart::kWidth,
	  crystal_heart::kHeight},
	 &crystal_heart::soundtrack},
	{"tengwang", "Preface to the Pavilion of Prince Teng",
	 "One autumn day at Hongzhou after Wang Bo's preface of AD 675, from the sea of clouds before sunrise to the stars over the Gan River: the Pavilion of Prince Teng, the feast, the young "
	 "poet alone on its high terrace above the whole land, the sunset and the solitary duck, the world inside his brush, and his resolve. With the preface's words on the screen in "
	 "Chinese and English, and music.",
	 tengwang::kDuration, tengwang::kFps, &tengwang::sceneAt, &tengwang::headerAt, &tengwang::shotStarts, 219.0,
	 {22, "character,water,dusk,night,fog", tengwang::shotNamesText(), tengwang::kSamples, 5.0, false, tengwang::kWidth, tengwang::kHeight},
	 &tengwang::soundtrack, &tengwang::captions},
	{"nomai-signal", "The Last Nomai Signal",
	 "An unofficial fan film after Outer Wilds: a lone Nomai explorer follows a faint repeating signal to a small moon of a gas giant, finds the ruins of an expedition that "
	 "came there long before, wakes their observatory deep under the ground, and learns the signal comes from a star that is already gone. The explorer answers it anyway. "
	 "With music.",
	 nomai::kDuration, nomai::kFps, &nomai::sceneAt, &nomai::headerAt, &nomai::shotStarts, 45.0,
	 {23, "character,emissive,night,dawn,fog", nomai::shotNamesText(), nomai::kSamples, 8.6, false, nomai::kWidth, nomai::kHeight},
	 &nomai::soundtrack, &nomai::captions, &nomai::subjectsAt, &nomai::events, &nomai::feetAt, &nomai::instruments},
};
inline constexpr int kFilmCount = static_cast<int>(sizeof(kFilms) / sizeof(kFilms[0]));

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// All the films: the ones above, and the ones made from files (film_file.h)
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// Where film files (*.film) are looked for: the folders scenes are (RAY_TRACER_PBRT_DIR, pbrt_scenes beside the program or up from it, the per-user scenes folder).
inline std::vector<std::string> filmFileDirs() {
	std::vector<std::string> dirs = pbrt_asset_check::sceneSearchPaths();
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

// A film as the program shows it: its frames with the changes of its cameras file made (shot_cameras.h: pbrt_scenes/film_assets/<key>/cameras.txt, read when a frame is
// made), and its soundtrack with the cues of its cues file (music_cues.h: pbrt_scenes/film_assets/<key>/cues.txt, read when the soundtrack is made).
inline Film withFilmFiles(Film f) {
	std::vector<std::string> names;
	std::string name;
	for (const char *p = f.meta.shotNames ? f.meta.shotNames : ""; ; ++p) {
		if (*p == '|' || *p == '\0') {
			names.push_back(name);
			name.clear();
			if (*p == '\0') break;
		} else {
			name += *p;
		}
	}
	const std::string key = f.key;
	const auto wrap = [&](std::function<std::string(double)> base) -> std::function<std::string(double)> {
		if (!base) return base;
		return [base, key, starts = f.shotStarts, names](double t) { return shot_cameras::at(key, starts, names, t, base(t)); };
	};
	f.sceneAt = wrap(f.sceneAt);
	f.headerAt = wrap(f.headerAt);
	const std::string cues = music_cues::fileOf(key);
	if (!cues.empty()) {
		f.soundtrack = [base = f.soundtrack, starts = f.shotStarts, names, own = f.instruments, cues, speech = f.speechAt](audio::Mix &m) {
			if (base) base(m);
			const music_cues::Sheet sheet = music_cues::read(cues);
			std::vector<std::string> problems = sheet.problems;
			// the cues on a mix of their own, ducked under the characters' lines (down by up to two thirds, in 0.06 s and back over 0.5 s) and laid on the film's sound
			audio::Mix music(m.seconds());
			music_cues::play(music, sheet, own ? own() : std::map<std::string, CueInstrument>(), starts ? starts() : std::vector<double>(), names, problems);
			m.add(music, speech ? music_cues::duckingOf(speech, m.seconds() + 4.0) : std::vector<float>());
			for (const std::string &problem : problems) std::cerr << "[cues] " << cues << ": " << problem << std::endl;
		};
	}
	return f;
}

// The words of a film file's `tags` line that are in the vocabulary, joined with commas.
inline std::string tagsIn(const std::string &line) {
	std::string tags, word;
	for (std::size_t i = 0; i <= line.size(); ++i) {
		const char c = i < line.size() ? line[i] : ',';
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
	return tags;
}
// A stable number from a film's key (fixed once given, whatever other films there are): 1000 + a hash, moved on past any that is taken.
inline int idFor(const Registry &r, const std::string &key) {
	std::uint32_t h = 2166136261u;
	for (char c : key) h = (h ^ static_cast<unsigned char>(c)) * 16777619u;
	int id = 1000 + static_cast<int>(h % 9000u);
	for (bool taken = true; taken;) {
		taken = false;
		for (const Film *f : r.all) taken = taken || f->meta.id == id;
		if (taken) ++id;
	}
	return id;
}
// Its captions file's captions (film_assets/<key>/captions.txt) and `lines` (its characters' words); null when it has neither.
inline std::function<std::vector<Caption>()> captionsOf(const std::string &key, std::function<std::vector<Caption>()> lines) {
	const std::string file = film_cache::asset(key + "/captions.txt");
	const bool hasFile = std::filesystem::exists(file);
	if (!hasFile && (!lines || lines().empty())) return nullptr;
	return [file, hasFile, lines]() {
		std::vector<Caption> out = hasFile ? caption_file::read(file) : std::vector<Caption>();
		if (lines) {
			const std::vector<Caption> said = lines();
			out.insert(out.end(), said.begin(), said.end());
		}
		return out;
	};
}

// The Film of a film read from a file (or of one part of a film in parts): its frames, shots, sound, captions and transitions. `drafted`: its music is the score drafted from
// it (it has no cues file).
inline Film filmOf(Registry &r, const film_file::DataFilm &d, bool drafted, int id, const std::string &tags) {
	const film_file::DataFilm *p = &d;
	Film f{r.keep(d.spec.key), r.keep(d.spec.name), r.keep(d.spec.about.empty() ? "A film made from " + std::filesystem::path(d.spec.scene).filename().string() + "." : d.spec.about),
		   d.spec.length, d.spec.fps, [p](double t) { return p->sceneAt(t); }, [p](double t) { return p->headerAt(t); }, [p]() { return p->shotStarts(); }, d.spec.still,
		   {id, r.keep(tags), r.keep(d.shotNames()), d.scene.samples, 0.0}};
	if (d.hasEvents()) {   // a film with characters or props with physics: what its shots are about, its footsteps, touches and landings, its characters' feet
		f.subjectsAt = [p](double t) { return p->subjectsAt(t); };
		f.events = [p]() { return p->events(); };
		f.feetAt = [p](double t) { return p->feetAt(t); };
	}
	// how loud its characters speak (its music is ducked under it)
	if (!d.spec.lines.empty() || !d.spec.characters.empty()) f.speechAt = [p](double t) { return p->speechAt(t); };
	// every film file has a soundtrack: its set's sound and its rain, its footsteps, touches and lines, and its music - its cues file's (withFilmFiles) or, when it has none,
	// the score drafted from what happens in it (film_score.h; `music none` leaves it out), ducked under the lines as a cues file's music is. (One with no music and nothing
	// else to hear has a soft wind: a video of a film is never silent.)
	const bool quiet = !drafted && music_cues::fileOf(d.spec.key).empty() && !d.hasEvents() && d.spec.ambience.empty() && d.spec.weather.empty() && d.spec.lightnings.empty();
	f.soundtrack = [p, drafted, quiet, names = d.shotNameList(), speech = f.speechAt](audio::Mix &m) {
		dressing::play(m, quiet ? std::vector<dressing::Ambience>{{"wind", 0.6}} : p->spec.ambience);
		for (const particles::Weather &w : p->spec.weather)
			if (w.kind == "rain") particles::rainSound(m, w);
		sfx::play(m, p->events());
		p->thunder(m);
		p->speak(m);
		p->playTheme(m);
		if (!drafted) return;
		const music_cues::Sheet sheet = music_cues::parse(score::draft(*p));
		std::vector<std::string> problems = sheet.problems;
		audio::Mix music(m.seconds());
		music_cues::play(music, sheet, {}, p->shotStarts(), names, problems);
		m.add(music, speech ? music_cues::duckingOf(speech, m.seconds() + 4.0) : std::vector<float>());
		for (const std::string &problem : problems) std::cerr << "[score] " << p->spec.key << ": " << problem << std::endl;
	};
	// how its shots give way to each other
	if (!d.transitions().empty()) {
		f.transitions = [p]() { return p->transitions(); };
		f.sceneAcross = [p, names = d.shotNameList()](double t) {   // (the other side of a cut is the other shot's: its cameras file's changes)
			return shot_cameras::at(p->spec.key, [p]() { return p->shotStarts(); }, names, p->acrossShotTime(t), p->sceneAcross(t));
		};
		f.acrossShotTime = [p](double t) { return p->acrossShotTime(t); };
	}
	// its captions: its captions file and its characters' lines
	if (!d.lineCaptions().empty()) f.captions = [p]() { return p->lineCaptions(); };
	return f;
}

// Loads one film file into the registry (or says why not): a film, or a film in parts (film_file::partsOf) made of a film for each part (film_parts.h).
inline void addFilmFile(Registry &r, const std::string &file) {
	std::string text;
	{
		std::ifstream in(file, std::ios::binary);
		text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
	}
	const film_file::FilmParts parts = film_file::partsOf(text, file);
	const std::size_t before = r.data.size();
	std::string problem = parts.error;
	const std::size_t count = parts.parts.empty() ? 1 : parts.parts.size();
	for (std::size_t k = 0; k < count && problem.empty(); ++k) {
		r.data.emplace_back();
		problem = r.data.back().load(file, parts.parts.empty() ? -1 : static_cast<int>(k));
		if (problem.empty() && k > 0 && r.data.back().spec.fps != r.data[before].spec.fps)
			problem = file + ":" + std::to_string(parts.parts[k].line) + ": the part \"" + parts.parts[k].name + "\" has another fps than the film's first part";
	}
	const std::string key = problem.empty() ? (parts.parts.empty() ? r.data.back().spec.key : parts.key.empty() ? film_file::detail::keyFromFileName(file) : parts.key) : std::string();
	if (problem.empty()) {
		for (const Film *f : r.all)
			if (key == f->key) problem = file + ": the key \"" + key + "\" is already a film's (" + f->name + ")";
	}
	if (!problem.empty()) {
		while (r.data.size() > before) r.data.pop_back();
		r.problems.push_back(problem);
		return;
	}
	if (parts.parts.empty()) {
		const film_file::DataFilm &d = r.data.back();
		Film f = filmOf(r, d, d.spec.draftMusic && music_cues::fileOf(d.spec.key).empty(), idFor(r, d.spec.key), tagsIn(d.spec.tags));
		f.captions = captionsOf(d.spec.key, f.captions);
		r.made.push_back(withFilmFiles(std::move(f)));
		r.all.push_back(&r.made.back());
		return;
	}
	// a film in parts: each part a film (its music drafted from it when the whole film has no cues file), joined; the whole film's cameras, cues and captions files are its own
	const bool drafted = parts.draftMusic && music_cues::fileOf(key).empty();
	std::vector<Film> films;
	std::vector<Transition> ins;
	std::string shotNames;
	std::vector<std::string> names;
	int samples = 0;
	for (std::size_t k = 0; k < parts.parts.size(); ++k) {
		const film_file::DataFilm &d = r.data[before + k];
		films.push_back(filmOf(r, d, drafted, 0, std::string()));
		ins.push_back(parts.parts[k].in);
		const std::string own = d.shotNames();   // (a part with no shots is one shot, "The whole film")
		shotNames += (shotNames.empty() ? "" : "|") + own;
		for (std::size_t at = 0;;) {
			const std::size_t bar = own.find('|', at);
			names.push_back(own.substr(at, bar == std::string::npos ? std::string::npos : bar - at));
			if (bar == std::string::npos) break;
			at = bar + 1;
		}
		samples = std::max(samples, d.scene.samples);
	}
	const film_file::DataFilm &first = r.data[before];
	Film whole{r.keep(key), r.keep(parts.name.empty() ? key : parts.name), r.keep(parts.about.empty() ? "A film in " + std::to_string(parts.parts.size()) + " parts." : parts.about),
			   0.0, first.spec.fps, nullptr, nullptr, nullptr, 0.0, {idFor(r, key), r.keep(tagsIn(parts.tags)), r.keep(shotNames), samples, 0.0}};
	whole = film_parts::joined(std::move(whole), films, ins);
	whole.stillTime = parts.still >= 0.0 && parts.still <= whole.duration ? parts.still : whole.duration * 0.5;
	whole.captions = captionsOf(key, whole.captions);
	if (whole.sceneAcross) {   // (the other side of a cut is the other shot's: the whole film's cameras file's changes)
		whole.sceneAcross = [across = whole.sceneAcross, when = whole.acrossShotTime, starts = whole.shotStarts, key, names](double t) {
			return shot_cameras::at(key, starts, names, when(t), across(t));
		};
	}
	r.made.push_back(withFilmFiles(std::move(whole)));
	r.all.push_back(&r.made.back());
}

inline Registry &registry() {
	static Registry *r = [] {
		Registry *reg = new Registry();   // (lives as long as the program: films hand out pointers into it)
		film_kinds::install();   // (before any film file is read: its characters are of these kinds)
		for (const Film &f : kFilms) {
			reg->made.push_back(withFilmFiles(f));
			reg->all.push_back(&reg->made.back());
		}
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
// A film made from a file read again when its file (or its scene) was saved since: true when it was (a film timeline follows the edits). Its Film keeps pointing at it,
// so frames made after it are of the new film; its length and its shots' names in the film table stay as they were until the program starts again.
// (A film in parts has each part read again: they are its film file's "<key>--1", "<key>--2" ...)
inline bool refreshFilmFile(const std::string &key) {
	bool changed = false;
	for (film_file::DataFilm &d : detail::registry().data)
		if (d.spec.key == key || (d.partIndex >= 0 && d.spec.key == key + "--" + std::to_string(d.partIndex + 1))) {
			std::string problem;
			changed = d.reloadIfChanged(&problem) || changed;
			if (!problem.empty()) std::cerr << "[film] " << problem << std::endl;
		}
	return changed;
}
// The film file a film was made from and its shots as they are now (read again first when it was saved since): for the changes the GUI's film timeline writes to it
// (film_edit.h). An empty path for a built-in film. A film in parts: its parts' shots in order (their names; the starts in the whole film).
inline const Film *find(const char *key);
struct FileShots {
	std::string path;
	std::vector<double> starts;
	std::vector<std::string> names;
};
inline FileShots fileShotsOf(const std::string &key) {
	FileShots out;
	refreshFilmFile(key);
	const Film *film = find(key.c_str());
	if (!film) return out;
	for (const film_file::DataFilm &d : detail::registry().data) {
		if (d.spec.key != key && !(d.partIndex >= 0 && d.spec.key == key + "--" + std::to_string(d.partIndex + 1))) continue;
		if (out.path.empty()) out.path = d.sourcePath;
		const std::string names = d.shotNames();
		for (std::size_t at = 0;;) {
			const std::size_t bar = names.find('|', at);
			out.names.push_back(names.substr(at, bar == std::string::npos ? std::string::npos : bar - at));
			if (bar == std::string::npos) break;
			at = bar + 1;
		}
	}
	if (film->shotStarts) out.starts = film->shotStarts();
	return out;
}
// The film file a film was made from (null for a built-in film): its characters and their lines. (A film in parts: each part by its own key, "<key>--2".)
inline const film_file::DataFilm *filmFileOf(const std::string &key) {
	for (const film_file::DataFilm &d : detail::registry().data)
		if (d.spec.key == key) return &d;
	return nullptr;
}

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
// What a program that sets up a film's video needs of every film, one line a film, tab-separated: key, name, seconds, fps, still time, width, height, samples, wavefront (0/1),
// soundtrack (0/1), the shot starts (comma-separated) and the shot names ('|'-separated). The GUI reads it from the renderer's library (scene_metadata_film_table; qt_gui/film_info.h)
// instead of compiling the films itself: a GUI not rebuilt with a new film gave its video the wrong length, and every film edit rebuilt most of the GUI.
inline std::string filmTable() {
	std::string s;
	char num[64];
	const auto put = [&](double v) {
		std::snprintf(num, sizeof num, "%.17g", v);
		return std::string(num);
	};
	for (const Film *f : films()) {
		const Film &film = *f;
		std::string starts;
		for (const double t : film.shotStarts()) starts += (starts.empty() ? "" : ",") + put(t);
		s += std::string(film.key) + "\t" + film.name + "\t" + put(film.duration) + "\t" + std::to_string(film.fps) + "\t" + put(film.stillTime) + "\t" + std::to_string(film.meta.width) + "\t" +
			 std::to_string(film.meta.height) + "\t" + std::to_string(film.meta.samples) + "\t" + (film.meta.wavefront ? "1" : "0") + "\t" + (film.soundtrack ? "1" : "0") + "\t" + starts + "\t" +
			 (film.meta.shotNames ? film.meta.shotNames : "") + "\n";
	}
	return s;
}

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
