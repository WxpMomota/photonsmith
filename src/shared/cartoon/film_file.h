#pragma once
// film_file.h -- films made from files: an ordinary pbrt scene animated by a small text file (name.film), with no C++ and no rebuild.
//
// The language (what a film file may say) is in film_parse.h and its parser in film_parse.cpp, what it says in film_spec.h; this file declares what reads the scene and
// makes the film's frames and sound (DataFilm), defined in film_file.cpp, and its world and its nature's sound in film_world.cpp - all compiled once, in cpu_renderer.

#include "film_parse.h"

namespace cartoon {
namespace film_file {

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

	// Reads the film file and its scene; returns "" or the problem, with the file and line. `part` >= 0: that part of a film in parts (partsOf), a film of its own whose key is
	// the whole film's and its number ("journey--2") and whose name is the part's.
	std::string load(const std::string &filePath, int part = -1);
	// The film file read again when it, or its scene, was saved since it was read (a film timeline that follows the edits): true when it was. When the new file has a
	// problem it is said (`problem`) and the film stays as it was.
	bool reloadIfChanged(std::string *problem = nullptr);
	std::string sourcePath;
	std::filesystem::file_time_type sourceTime{}, sceneTime{};
	int partIndex = -1;   // >= 0: the part of a film in parts this is

	std::string loadFile(const std::string &filePath);

	std::vector<std::vector<speech::Syllable>> spoken;   // each line's syllables (spec.lines)

	// `acting words`: each line's gesture and expression from its words (film_acting.h), timed by its syllables, added to its speaker where the film gives that character
	// no gesture (or no expression) of its own during the line. A nod or a shake of the head is quick; the others are held to the line's end; an expression stays a little
	// after it.
	void actFromWords();

	// ---- the set's dressing ----
	// The scene's own infinite and distant lights left out of its plain text (a `sky` brings its own sun and sky).
	void leaveOutSkyLights();
	// The dressing at t: the pieces once, then every placed thing (swaying), as scene text.
	std::string dressingAt(double t) const;
	// The weather on the land at t: how wet it is and how much snow lies on it (film_file.h `wet`, `snowcover`).
	terrain::Weather weatherAt(double t) const;

	// ---- walks that find their way ----
	// The set at t without its characters and its props (pbrt text): what is in the way.
	std::string setAt(double t) const;
	// Finds the way of every walk that goes `to` places, in time order (so whoever walked before is where their walk left them): "" or "line: what is wrong".
	std::string findWays();
	// How high a step a character takes in its stride (a person 0.3 m, Pip a little less, a turtle at its own scale).
	double stepOf(const Character &c) const { return 0.17 * characterKinds().at(c.kind).height * c.scale; }
	// The heights of the walks and jumps that do not find their way, from the floor under each point (the walk's own points: it goes where it is told, on whatever is there).
	void heightsOfWalks();
	std::map<std::string, std::shared_ptr<const film_paths::Terrain>> terrains;   // the floor of each character that walks on uneven ground
	const film_paths::Terrain *terrainFor(const Character &c) const;
	// Two characters that walk into each other (closer than most of their sizes while one of them walks): "" or what happens when.
	std::string crowding() const;

	// ---- automatic coverage ----
	// A camera key from story-space places (pbrt's: x mirrored), with a field of view and, for a shot of one speaker, a shallow focus on them.
	static camera_keyframes::Key keyOf(Vec3 eye, Vec3 target, double fov, double lens);
	Vec3 headOf(const Character &c, double t) const { return bodyPoint(c, t, characterKinds().at(c.kind).head); }
	// The field of view that frames a speaker of this height from this far: their head and shoulders (a frame about 0.55 of their height and 0.3 m more: a big head needs room).
	static double framing(double size, double distance) { return std::clamp(2.0 * std::atan(0.5 * (0.55 * size + 0.3) / std::max(0.1, distance)) * 180.0 / kPi, 12.0, 50.0); }
	double sizeOf(const Character &c) const { return characterKinds().at(c.kind).height * c.scale; }
	// The line of a coverage at t: from the first covered to the second, on the ground (one alone: its own left to right).
	Vec3 lineOf(const std::vector<const Character *> &cast, double t) const;
	// The side of the line the cameras keep to when the shot does not say: the one the characters' faces look to (more of them look that way than the other), the first on the left
	// when it is even.
	int sideTheyFace(const std::vector<const Character *> &cast, double t) const;
	// Where the camera is for a beat at t (story space): `speaker` null for the shot of them all (a two-shot), else a shot of the speaker over the shoulder of the nearest of the
	// others (or, alone or much smaller than that listener, from in front of them, turned to the side), every camera on the `side` of the line from the first covered to the second.
	camera_keyframes::Key coverageKey(const std::vector<const Character *> &cast, const Character *speaker, int side, double t) const;
	// Every `auto` shot made into shots of its own, from what happens in it: a two-shot from its start, a shot of each speaker from just before they speak, back to the
	// two-shot when nobody speaks for a while; no cut less than a second after the last. "" or what is wrong.
	std::string coverShots();

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

	const Spec::Prop *prop(const std::string &name) const;
	// Plays the picks, puts and gives through (who holds what, from when): "" or "line: what is wrong".
	std::string holdingTimeline();
	// The world place (story space) of a body-space point of a character at t.
	Vec3 bodyPoint(const Character &c, double t, Vec3 local) const { return transformPoint(momentOf(c, t).frame, local * c.scale); }
	// Where a character carries a prop in a hand at t (story space).
	Vec3 carryOf(const Character &c, int hand, double t) const;
	// Where two characters' hands meet to give a prop at t: from half way between where each carries it, each hand reaches for the point and the point moves half way between
	// where they got to (an arm too short, a hand too high), a few times over - a place both can reach when there is one.
	Vec3 meetingOf(const Character &a, int handA, const Character &b, int handB, double t, double *gap = nullptr) const;
	// Where the hands of a character reach at t for the props it handles (CharacterMoment::hands), as its picks, puts and gives say.
	void handsAt(const Character &c, double t, CharacterMoment &m) const;
	// Its hands on the set and on others (`touch`, `hold`, `hug`): reached for over half a second up to T0, held, let go over half a second after T1.
	void contactsAt(const Character &c, double t, CharacterMoment &m) const;
	// A character's moment with its hands and its look: what its kind needs to draw it, or to say where its hand is.
	CharacterMoment actingMoment(const Character &c, double t) const;
	// The frame's cloth (film_cloth.h: simulated up to t) and its cartoon marks' `# @rt-mark` lines (film_marks.h, through the camera of `header`).
	std::string clothAt(double t) const;
	// A character's scene text at t with the film's ways of moving (film_motion.h): its follow-through (m.jiggle, set here) and its squash and stretch.
	std::string motionOf(const Character &c, const CharacterKind &kind, double t, CharacterMoment &m) const;
	// (round 11, `footprints`) the prints the characters' feet have left by t, and the dust their last steps kicked up (film_world.cpp)
	std::string footprintsText(double t) const;
	std::string overlaysAt(const std::string &header, double t, double nt) const;   // (round 11: an explainer's drawings, film_explain.h)
	std::string aboardAt(const std::string &header, double t, double nt) const;     // (round 11: footprints, floating things, interiors, holograms, screens, drawings)
	std::string marksAt(const std::string &header, double t) const;
	// Where a character looks of itself while someone speaks (CharacterMoment::look): at the speaker, turning to them just after they begin and back a little after they end
	// (the look moves smoothly from one speaker to the next); and while it speaks, at the nearest of those who listen. A `look` line is on top of this.
	void listenAt(const Character &c, double t, CharacterMoment &m) const;
	// Where a prop's handle is at t (story space): where it rests, or in the hand that holds it.
	Vec3 propAt(const std::string &name, double t) const;
	// ---- where characters look ----
	void lookAt(const Character &c, double t, CharacterMoment &m) const;

	// ---- characters ----
	const Character *character(const std::string &name) const;
	static Vec3 story(double x, double y, double z) { return {-x, y, z}; }   // (pbrt's coordinates to the toolkit's: x mirrored)
	// Nature's time at film time t (`timelapse`, and `slowmo` windows where it runs slower): what the clouds, the water, the wind and the animals go by.
	double natureAt(double t) const;
	// A herd or a flock as a shot's subject - `herd1`, `herd2` ..., `flock1` ... (the film's herds and flocks in the order of their lines): where its middle is at t, which
	// way it is going, and the height a camera looks at (story space, as a character's feet, front and chest). False when `who` is none.
	bool wildSubject(const std::string &who, double t, Vec3 &feet, Vec3 &front, Vec3 &chest) const;
	// A shot's subject, a character or a herd or flock: its feet, front and chest at t (story space). False when it is neither.
	bool subjectFrame(const std::string &who, double t, Vec3 &feet, Vec3 &front, Vec3 &chest) const;
	// A walk of a character as a path in story space, from where it was before the walk.
	rig::PathWalk pathOf(const Character &c, std::size_t walk) const;
	// Where a walk, a run or a jump leaves a character (story space; its height the floor's there).
	Vec3 endOf(const Character &c, std::size_t walk) const;
	static constexpr double kRunStride = 1.6;   // a run's gait, and so its stride, is this much wider than a walk's
	double flightOf(const WalkLine &w) const { return 2.0 * std::sqrt(2.0 * w.height / std::max(0.05, spec.gravity)); }   // (a jump's time in the air, under the film's gravity)
	// A character's walks, runs and jumps one after another: each starts when the one before has ended (its way's length at its pace, or a jump's flight). One that started
	// sooner was put off until the other ended and then went on as if it had left on time - the walker jumped ahead and its footsteps were heard twice. A crowd's walks are
	// made, not written: the next is put off until it can start instead.
	std::string walksInTurn();
	// The moment a character last landed from a jump at or before t (or -1e30).
	double landedAt(const Character &c, double t) const;
	// Where a character is at t (story space), which way it faces (degrees, rig::headingOf), how many steps it has made in its walk and whether it is walking. After a walk it
	// stands where the walk ended, its feet where they came down.
	CharacterMoment momentOf(const Character &c, double t, Vec3 *at = nullptr) const;
	// Its feet held where they came down (CharacterMoment::lockedFeet), when its kind says where its gait puts them and it has walked.
	void lockFeet(const Character &c, double t, CharacterMoment &m) const;
	// How far a loose spring on the character's place lags it at t, in its body's space (CharacterMoment::trail).
	Vec3 trailOf(const Character &c, double t, const CharacterMoment &m) const;
	Vec3 chestOf(const Character &c, double t) const { return transformPoint(momentOf(c, t).frame, characterKinds().at(c.kind).chest * c.scale); }
	static int seedOf(const std::string &name);
	std::size_t shotAt(double t) const;
	// What the moment is about (Film::subjectsAt): the characters its shot names, or the one it follows.
	std::vector<Vec3> subjectsAt(double t) const;
	// The characters' feet, in the same order at every moment (Film::feetAt).
	std::vector<Foot> feetAt(double t) const;
	// The footfalls of every walk and the gestures' contacts (Film::events), panned and quietened from the camera at each moment.
	std::vector<FilmEvent> events() const;
	bool hasEvents() const { return !spec.characters.empty() || !spec.bounces.empty() || !spec.topples.empty() || !spec.rigids.empty(); }   // (props are handled by characters)

	// ---- what the characters say ----
	// What a character's lines are said in: its own `voice`, else the film's `voices`; "" for the babble voice.
	std::string voiceAskedOf(const Character &c) const;
	// The recording of line i in a computer's voice (film_tts.h), made when it is first needed; null when it is said in the babble voice (asked for, or no speech engine, or
	// not made yet in a program that cannot make it). A small creature's voice (its kind's above 200 Hz) is raised towards its own, by up to 9 semitones.
	const music_cues::Recording *recordingOf(std::size_t i) const;
	// When line i ends: the last sound of its recording, or its last syllable. (Kept once known: it is asked for every 1/100 s of the film by the music's ducking.)
	double lineEnd(std::size_t i) const;
	// A character's mouth and voice at t from its lines (a line in a computer's voice: its recording's mouth shapes and loudness).
	void speakingAt(const Character &c, double t, CharacterMoment &m) const;
	// How loud the characters are speaking at t, 0..1: their lines and their recorded voices (the music is ducked under it).
	double speechAt(double t) const;
	// The voice a character speaks in: its kind's, its pitch moved by up to 1.5 semitones either way by its name.
	speech::Voice voiceOf(const Character &c) const;
	// The lines spoken on a mix: each panned and quietened from where the camera is when it starts.
	void speak(audio::Mix &mix) const;
	// The captions of the lines (those not marked nocaption): each from its start to a little after its end, cut short where the next begins.
	std::vector<Caption> lineCaptions() const;
	// The title card (`title`, `episode`) and the cards of the credits (`credits`, `credit`): the film's name; its cast, five to a card; its own lines and how it was made.
	std::vector<Caption> titleCaptions() const;
	// The film's theme under its title (`theme`): a fanfare of its own (`jingle`: a bell's arpeggio over a chord), or a recording of the film library (library/FILE beside the
	// film file, else in film_assets/library).
	void playTheme(audio::Mix &m) const;
	// (film_events.h's panOf and audibility, here so that this header does not need the sound library)
	static double panOf(const CameraView &view, Vec3 at);
	static double audibility(double distance) { return distance <= 3.0 ? 1.0 : std::max(0.1, std::pow(3.0 / distance, 0.7)); }

	std::vector<double> shotStarts() const;
	std::vector<std::string> shotNameList() const;
	std::string shotNames() const;

	// Whether t is in the credits (after the story: `credits`), whose frames are black.
	bool inCredits(double t) const { return spec.credits && t >= spec.storyEnd; }
	// A black frame: the scene's own header and nothing lit (a speck far below the set: a world with nothing in it is not a scene).
	std::string creditsScene() const;
	std::string headerAt(double t) const;
	// The `# @rt-toon` line of every frame of a film with the toon look (`toon`), else nothing.
	// The frame's `# @rt-toon` line at t: the film's toon look, with the frame's seed when its lines boil (drawn afresh every other frame).
	std::string toonLine(double t) const {
		if (!spec.toon.on) return std::string();
		toon_look::Style st = spec.toon;
		if (st.boil) st.strokes.seed = 1 + static_cast<int>(std::floor(std::max(0.0, t) * std::max(1, spec.fps) / 2.0 + 1e-6));
		return toon_look::tagOf(st);
	}

	std::string sceneAt(double t) const { return inCredits(t) ? creditsScene() : terminated(worldAt(headerAt(t), t)); }
	// A frame's text with the film's shadow terminator offset (`terminator`, shadow_terminator.h) on every one of its materials; as it is without one.
	std::string terminated(std::string text) const;
	// `text` with `words` (pbrt parameters) added to every Material and MakeNamedMaterial statement in it.
	static std::string withMaterialWords(const std::string &text, const std::string &words);
	// The film's rigid-body world (igid, rigid_world.h), with its characters' bodies and feet pushing what is in their way.
	std::shared_ptr<const rigid::Run> rigidRun() const;

	// ---- how shots give way to each other ----
	std::vector<Transition> transitions() const;
	// The scene at t seen by the other side of the dissolve or wipe it is in (the shot after the cut before it, the shot before after it); outside one, the scene itself.
	std::string sceneAcross(double t) const;
	// The time that picks the shot whose camera sceneAcross(t) uses (what a cameras file changes is that shot's).
	double acrossShotTime(double t) const;

	// The exposure at t: the film's factor, and (`exposure auto`) one that follows the light - 1 by day, opening up through sunset to about 30 times at night.
	double exposureAt(double t) const;
	// The sounds of its nature placed where they come from (film_soundscape.h): waterfalls, fire, lava and volcanoes, surf, animals, birds in the trees - each heard from
	// the camera as it moves.
	void soundscape(audio::Mix &m) const;
	// The thunder of its lightning, each strike heard as long after as sound takes to come from it to the camera at its time.
	void thunder(audio::Mix &m) const;
	// The scene with this header at t: the set, its animated blocks, its dressing, its sky and its characters.
	// The air as a camera medium: the atmosphere's scattering at the film's altitude (the sky line's; pbrt's y = 0 there) - Rayleigh's, three times stronger in the blue,
	// and the haze's, grey and thrown forward - thinning with height over the haze's scale height (1.2 km: the air near the ground, between the camera and the land; the
	// air above is in the sky's picture already, and a sky ray should not cross it twice), written as height_fog.h's layer far above its top (ten scale heights under
	// the ground: its upper tail, an exponential), and ended at a sphere round the camera.
	std::string airMedium() const;

	std::string worldAt(const std::string &header, double t) const;
	// The shot's own lighting at t (`lighting`: shot_lighting.h): round the character it is on, or what the camera looks at, from where the camera is.
	std::string shotLightsAt(const std::string &header, double t) const;

  private:
	// The lines' ends once worked out (lineEnd): a copy of the film starts without them.
	struct LineEnds {
		std::mutex lock;
		std::vector<double> ends;
		LineEnds() = default;
		LineEnds(const LineEnds &) {}
		LineEnds &operator=(const LineEnds &) { return *this; }
	};
	mutable LineEnds lineEnds_;
	double progress(const Anim &anim, double t, int component) const;
	bool visible(const std::string &name, double t) const;
	static std::string num(double v);
	// The transforms of a block at t; with a shutter, a moving block's at the shutter's opening (StartTime) and closing (EndTime) - pbrt's object motion blur, which
	// the CPU and the GPU both render (gpu: a two-key triangle GAS, moving spheres).
	std::string transformsFor(const std::string &name, double t) const;
	std::string transformsAt(const std::string &name, double t) const;
	// The lights of a block with their power multiplied by the block's `light` factor at t.
	std::string lightsScaled(const std::string &name, const std::string &body, double t) const;
	// A move's camera at t: about its character, the way along it eased (an orbit at an even pace), in story space and then pbrt's.
	camera_keyframes::Key moveKey(const Shot &shot, double t, double span) const;
	void cameraAt(double t, std::string &lookAt, std::string &camera, int shotIndex = -1) const;
};

}  // namespace film_file
}  // namespace cartoon

