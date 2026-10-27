#pragma once
// film_parse.h -- the film file language (film_file.h) and its parser.
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
//     mute     birds                                            # no birds heard: no birdsong (the trees' or an ambience's), no eagles' cries, no geese, no owl
//     music    none                                             # no music: without a cues file (film_assets/<key>/cues.txt) a film plays its drafted score
//                                                               # (film_score.h, `--film-score`) under its sounds; `music draft` says so
//     terrain  mountains  size 1500  height 320  seed 3         # the land (film_terrain.h): hills, mountains, dunes, canyon or coast, eroded, coloured by height, slope and water,
//                                                               # with far ranges round it (`far off`); `at X Z`, `y Y`, `focus X Z` (finest there; a valley floor round it),
//                                                               # `snow H` (the snow line above y), `erosion E` (0 to 3); on a terrain every height is above the ground
//     water    sea  level 0  waves 1.2  wind 30                 # a sea, lake or clear water at a height (pbrt's y), its waves rolling (the significant height, which way
//                                                               # they run, `choppy C`, `foam off`); on a terrain it fills what lies below its level and laps the shore
//     surf     1.5  period 10  toward 20                       # a swell breaking on the terrain's shore (film_surf.h): it slows, turns to the beach, grows, breaks white;
//                                                               # clear shallows over caustics, foam streaks, spray (the sea's wind when `toward` is not given)
//     river    0 -300  20 -100  0 0  width 12  speed 1.5        # waypoints downstream (pbrt's x z); on a terrain it lies in the lowest ground across its width;
//                                                               # `river from X Z` finds its own way downhill, `river canyon` runs along a canyon's riverbed
//     waterfall at 40 0 -80  width 8  drop 30  facing 180       # the middle of its lip (on a terrain, Y above the ground), how far it drops, which way it pours
//     clouds   cumulus  cover 0.4  base 900  wind 90  speed 6   # clouds you can see round (film_clouds.h): cumulus, towering or wisps; `size W` across, `density D`,
//                                                               # `area A` (the square they are spread over, round `at X Z`), `seed N`; they drift and cast shadows
//     biome    savanna                                         # the land as a biome (savanna, jungle, desert, tundra, arctic): its ground's colours, its plants, its sound
//     forest   mixed 3000  season autumn                        # conifer, broadleaf, birch, palm, shrub, acacia, kapok, cactus, fern; or mixed (by height), savanna,
//                                                               # jungle, desert or tundra (a biome's mix): so many trees where the land suits them;
//                                                               # `season summer to autumn from T0 to T1` (each tree turning in its own time; `then winter from T2 to T3`
//                                                               # on to the next), `grow T0 T1` (from saplings), `in X0 Z0 X1 Z1`, `below H` (the tree line above the land's
//                                                               # base), `above H`, `steep S`, `scale A B`, `near D` (drawn in full within D m of the camera), `seed N`
//     flowers  poppies 3000  in -20 -20 20 20  bloom 2 10       # poppies, daisies, lupins or sunflowers (turning to the sun), opening each in its own time; `close T0 T1`
//     flock    starlings 300  at 0 80 -200  radius 40           # animals (film_wildlife.h): starlings or fish (flocking round `at`), geese (`from X Y Z to X Y Z`, in
//                                                               # a V), butterflies (`in X0 Z0 X1 Z1`); `speed S`, `during T0 T1`, `seed N`; on a terrain Y is above it
//     herd     deer 12  from -200 0  to 200 -150  speed 1.1     # deer, horses or wolves (rigged: film_animals.h) walking over the land, grazing now and then;
//                                                               # `flee T` (they bolt and gallop away), `start T`, `seed N`; `flock eagles` circle a thermal
//     aurora   green  brightness 1.2  during 4 30               # the sky's spectacles (film_skyfx.h): curtains of light (green, red or violet; `facing D`, `distance M`),
//     lightning 5  at 0 -3000  radius 2000  during 2 20         # strikes with their flash and thunder (`base H`, `seed N`), shooting stars (`meteors COUNT`), and
//     rainbow  0.8  during 6 16                                 # a rainbow opposite the sun (the `sky` line's); at night the sky has its stars, turning with the hours
//     wet      0.8  from 4 to 14                                # weather that changes the land: wet (darker, glossy, puddles in the flat hollows), snowcover (snow on the
//     snowcover 0.7  from 0 to 20                               # ground level enough to hold it), each reached from T0 to T1; `gusts S [wind D] [speed V]`: fronts that
//     gusts    1.5  wind 90                                     # sweep across the grass and trees; `mist DENSITY [radius R]`: a mist round the camera
//     fog      0.04  top 30  thickness 4  lift 10 20             # fog lying in the valleys (height_fog.h): dense below the height `top` (pbrt's y; a quarter of the land's
//                                                               # height by default), thinning over `thickness`; lifting from 10 s to 20 s, it thins away and sinks
//     storm    rain  strength 0.8  from 4 to 40  wind 120       # a storm building up and dying away: the sky darkening under a deck of cloud, rain shafts, its rain
//                                                               # (snow: a blizzard; sand: a dust storm) blown by its wind (`speed V`), gusts, wet ground, lightning
//     air      strength 3  haze 1.5                             # the air between the camera and the far land (aerial perspective): the atmosphere's blue and haze,
//                                                               # thinning with height, out to `radius` (4.5 times the terrain's size); `sky ... physical [altitude A]`
//                                                               # makes the sky and the sun's light from the atmosphere itself (atmosphere.h), as the eye A m up sees it
//     underwater  clarity 1.5  colour teal                      # the camera is under the film's water (film_underwater.h): the sea round it, caustics on the seabed, shafts
//     kelp     60  in -20 -20 20 20  height 9                   # of light, marine snow; kelp swaying and coral (`coral COUNT [in X0 Z0 X1 Z1]`) on the seabed
//     geyser   at 30 -40  height 25  every 20                  # the land in action (film_geology.h): a geyser erupting every so often; `rockfall from X Z [at T]`: rocks
//                                                               # tumbling down the slope; `avalanche from X Z [at T]`: a powder cloud racing down; `quake T0 T1`: shaking
//     fire     at 0 0 -4  size 1.2                              # fire (film_fire.h): flames (glowing volumes, flickering, a light among them, sparks) with smoke above;
//     lava     from 120 -300  width 6                           # lava creeping down the land from a vent; a volcano raised in the land (`volcano at X Z [height H]
//     wildfire in -200 -200 200 200  during 0 30  wind 90       # [radius R] [erupting T0 T1]`), and a fire front sweeping across with the wind
//     timelapse 60                                              # nature 60 times faster: the clouds, the water, the aurora, the wind (the camera and the characters not)
//     slowmo   22 30 0.3                                        # from 22 s to 30 s nature runs at 0.3 of its speed (0.25 when not given): a high-speed camera's slow motion
//     exposure auto                                             # (or a factor) the exposure follows the light: 1 by day, opening up through sunset into night
//     shot 0 Over the valley
//       fly  -800 200  0 -100  600 -600  height 80  look 0 40 -900   # (instead of `key` lines) a drone over waypoints at a height above the land, smoothed, looking ahead and
//                                                               # down (or at a point; `fov F`)
//
// Weather and particles (film_particles.h):
//
//     weather   rain 0.8 from 4 to 14                          # rain, snow or leaves over the set round the camera (amount 1 a good shower; eased in and out; `ground Y`)
//     particles sparks at 1 0.9 -2  3 5  rate 80               # sparks, dust or a splash thrown from a place from 3 s to 5 s, so many a second (`speed S`); its y is the
//                                                               # scene's own, not above a terrain
//     burst     splash at 0 0 -1  6.2  count 60                # all at once at 6.2 s
//
// Several sets in one film - a film in parts (partsOf; film_parts.h joins them): each `part` is a film of its own, with its `scene`, its `length`, its characters, its shots
// and its sound, and the parts play one after another. The lines before the first part are every part's (a cast, a default set, an fps), except the film's own `film`,
// `name`, `about`, `tags`, `still` (a time in the whole film) and `music`:
//
//     film     pip-goes-out
//     part     "The lantern path"                              # a part: its name ...
//     scene    film_assets/a-walk-at-dusk/set.pbrt
//     length   7
//     ...
//     part     "The meadow" fade 1.2                           # ... and how it comes in after the one before: a cut, or `dissolve`, `fade` or `wipe` over so many seconds
//     scene    film_assets/stage/set.pbrt
//
// The library - casts and sets kept once and brought into any film by name (library/NAME.cast and library/NAME.set beside the film file, else in film_assets/library):
//
//     set      meadow                                          # a set's lines read here: its scene (from the set's folder), its sky, dressing and sound
//     cast     ann  at -0.8 0 0  facing 30                     # a character as its cast file says (kind, look, voice), standing here
//     cast     kid as pip-child  at 0 0 0.8                    # ... named `kid` in this film; anything more on the line is on top of the cast's
//     rigged   bot models/bot.glb  height 1.6                  # a kind of character from a rigged glTF model with clips (film_rigged.h): idle, walk, run, and its gestures
//                                                              # (`stride S`, `turn DEGREES`, `walk CLIP`, `idle CLIP`, `run CLIP`); a cast file can name its model the same way
//     character  bo person  pitch -2  rate 0.9                 # (a voice for any character: semitones up from its kind's, and how fast it speaks)
//
// Real voices (film_tts.h) - lines said by the computer's own text-to-speech, offline, the mouth following the recording:
//
//     voices   system                                          # every character in one of the computer's voices (by its name; `babble` the default)
//     cast     ann  at -0.7 0 0  voice zira                    # ... or one of them in the voice with that word in its name
//     acting   words                                           # each line acted from its words (film_acting.h): a gesture and an expression where none is given
//     touch    ann 2 5  -0.3 1.2 -0.6 left                     # a hand on a place of the set from 2 s to 5 s (a wall to lean on, a door's handle), reached for and let go
//     hold     ann sam 6 9                                     # ann's right hand and sam's left held together
//     hug      pip kid 6 9                                     # both of each one's arms round the other (stand them close: about a third of a metre chest to chest)
//
// Titles and credits - a title card over the opening, cards of credits after the story over black, a theme under the title:
//
//     title    "The Lost Mitten"  at 0.5  for 3                # (the film's name unless given)
//     episode  2 "Winter"                                      # a line under the title: "Episode 2: Winter"
//     credits                                                  # cards of three seconds after the story: the name, the cast five to a card, the `credit` lines
//     credit   "Music by Sam"
//     theme    jingle                                          # a fanfare of its own under the title, or a WAV of the film library (`theme opening.wav gain 0.8`)
//
// A shot's own lighting (shot_lighting.h) - a key, a fill and a rim round its subject, from where the camera is (in a shot's lines; a cameras file can say it too):
//
//       lighting key 1.5 fill 0.5 rim 1.2 side right on ann    # (any of them; `lighting` alone: key 1.2, fill 0.4, rim 0.8 on what the camera looks at)
//
// The toon look (src/shared/toon_look.h) - the light in flat bands, ink lines round shapes and along creases, stronger colour - on every frame:
//
//     toon     bands 3  lines 2.5  ink #1a1420  saturation 1.25  shadow 0.35    # (any of them, or none for the default style; `toon off`)

#include "film_spec.h"

namespace cartoon {
namespace film_file {

// Reads a film file's text. `where` names it in the messages. The scene is not read here (see load()), so the names it animates are not yet checked. The library files it
// brings in (`set NAME`: library/NAME.set, film-file lines - a scene and what dresses it; `cast NAME`: library/NAME.cast, what a `character` line says of a character but
// where it stands) are read here, as if their lines were the film file's at that place.
inline Parsed parse(const std::string &text, const std::string &where) {
	using detail::number;
	Parsed r;
	Spec &s = r.spec;
	struct Source {   // a text being read: the film file, or a set of the library it brings in
		std::shared_ptr<std::istringstream> in;
		std::string where;
		int n = 0;
		std::filesystem::path dir;   // a library file's folder: its `scene` is from there
		bool library = false;
	};
	std::vector<Source> sources{{std::make_shared<std::istringstream>(text), where, 0, {}, false}};
	std::string here = where;   // the file of the line being read
	std::string line;
	int n = 0;
	auto fail = [&](const std::string &what) {
		r.ok = false;
		r.error = here + ":" + std::to_string(n) + ": " + what;
		return r;
	};
	std::string shotText;   // the key lines of the shot being read
	auto closeShot = [&]() -> std::string {
		if (s.shots.empty()) return std::string();
		if (s.shots.back().move.kind != Shot::Move::None) {
			if (!shotText.empty() || s.shots.back().following || s.shots.back().coverage) return "the shot \"" + s.shots.back().name + "\" has a move and `key`, `follow` or `auto` lines too";
			return std::string();
		}
		if (!s.shots.back().fly.empty()) {
			if (!shotText.empty() || s.shots.back().following || s.shots.back().coverage) return "the shot \"" + s.shots.back().name + "\" has a `fly` line and `key`, `follow` or `auto` lines too";
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
	for (;;) {
		if (!std::getline(*sources.back().in, line)) {   // (the end of a library file: back to the file that brought it in)
			if (sources.size() == 1) break;
			sources.pop_back();
			here = sources.back().where, n = sources.back().n;
			continue;
		}
		n = ++sources.back().n;
		here = sources.back().where;
		line = detail::trim(detail::withoutComment(line));
		if (line.empty()) continue;
		const std::vector<std::string> w = detail::words(line);
		const std::string &verb = w[0];
		const std::string rest = detail::trim(line.substr(verb.size()));
		if (sources.back().library && (verb == "film" || verb == "name" || verb == "about" || verb == "tags" || verb == "still" || verb == "length" || verb == "fps" || verb == "music" ||
									   verb == "shot" || verb == "key" || verb == "part" || verb == "title" || verb == "episode" || verb == "credits" || verb == "credit" ||
									   verb == "theme"))
			return fail("`" + verb + "` is the film's, not a set's: a set is a scene and what dresses it, its sound and who is on it");
		if (verb == "key") {
			if (s.shots.empty()) return fail("a `key` line belongs to a shot: start one with `shot 0 NAME` first");
			shotText += line + "\n";
		} else if (verb == "set") {   // a set of the library: its lines read here
			if (w.size() != 2 || !detail::validKey(w[1])) return fail("`set NAME`: a set of the library (library/NAME.set), its name lower-case letters, digits and '-'");
			if (sources.size() > 4) return fail("sets that bring in sets, more than four deep");
			const std::string file = detail::libraryFile(w[1], ".set", where);
			if (file.empty()) return fail("no set called \"" + w[1] + "\" (library/" + w[1] + ".set beside the film file, or in film_assets/library)");
			sources.push_back({std::make_shared<std::istringstream>(detail::textOf(file)), file, 0, std::filesystem::path(file).parent_path(), true});
		} else if (verb == "cast") {   // a character of the library: its cast file's kind and look, and where it stands here, as one `character` line
			const std::vector<std::string> t = detail::tokens(line);
			if (t.size() < 2) return fail("`cast NAME [as WHO] [at X Y Z] [facing DEGREES] ...`: a character of the library (library/WHO.cast; WHO is NAME unless it is given)");
			std::size_t i = 2;
			std::string who = t[1];
			if (t.size() > 3 && t[2] == "as") who = t[3], i = 4;
			if (!detail::validKey(who)) return fail("a cast's name is lower-case letters, digits and '-' (got \"" + who + "\")");
			const std::string file = detail::libraryFile(who, ".cast", where);
			if (file.empty()) return fail("no cast called \"" + who + "\" (library/" + who + ".cast beside the film file, or in film_assets/library)");
			std::string kind, look, model;
			std::istringstream castText(detail::textOf(file));
			for (std::string c; std::getline(castText, c);) {
				c = detail::withoutComment(c);
				const std::vector<std::string> cw = detail::words(c);
				if (cw.empty()) continue;
				if (cw[0] == "kind" && cw.size() == 2) kind = cw[1];
				else if (cw[0] == "rigged" && cw.size() >= 2) model = detail::trim(c).substr(6);   // (its kind is a rigged model: the file and how it is drawn, from the cast's folder)
				else look += " " + detail::trim(c);
			}
			if (kind.empty()) return fail("the cast " + file + " has no `kind KIND` line");
			std::string character = "character \"" + t[1] + "\" " + kind + look;
			for (; i < t.size(); ++i) character += " " + t[i];
			sources.push_back({std::make_shared<std::istringstream>(character + "\n"), here, n - 1, sources.back().dir, sources.back().library});   // (read next, as this line)
			if (!model.empty()) {   // (and before it, its kind from its model: `rigged KIND FILE ...` with the file from the cast's folder)
				const std::vector<std::string> mw = detail::tokens(model);
				std::string rigged = "rigged " + kind + " \"" + (std::filesystem::path(file).parent_path() / mw[0]).lexically_normal().generic_string() + "\"";
				for (std::size_t k = 1; k < mw.size(); ++k) rigged += " " + mw[k];
				sources.push_back({std::make_shared<std::istringstream>(rigged + "\n"), file, 0, std::filesystem::path(file).parent_path(), false});
			}
		} else if (verb == "film") {
			if (!detail::validKey(rest)) return fail("the film's key must be lower-case letters, digits and '-' (got \"" + rest + "\")");
			s.key = rest;
		} else if (verb == "name") {
			s.name = rest;
		} else if (verb == "about") {
			s.about = rest;
		} else if (verb == "scene") {   // (a set's scene is from the set's folder; the film file's from its own, when it is loaded)
			s.scene = sources.back().library && std::filesystem::path(rest).is_relative() ? (sources.back().dir / rest).lexically_normal().string() : rest;
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
					if (t[i] == "detail" && !(c.detail >= 0.004 && c.detail <= 0.1))   // (its sculpting grid: finer than 4 mm is billions of samples, the program out of memory)
						return fail("`detail` is the sculpting cell in metres, from 0.004 to 0.1");
					i += 2;
				} else if ((t[i] == "pitch" || t[i] == "rate") && i + 1 < t.size()) {   // its voice
					if (!number(t[i + 1], t[i] == "pitch" ? c.pitch : c.rate) || !(c.rate > 0.0) || std::abs(c.pitch) > 24.0) return fail("a voice's `pitch` is semitones (-24 to 24) and its `rate` a number above 0");
					i += 2;
				} else if (t[i] == "voice" && i + 1 < t.size()) {   // babble, system, or a word of a computer voice's name (film_tts.h)
					c.voice = detail::lowerWord(t[i + 1]);
					i += 2;
				} else if (Vec3 colour; i + 1 < t.size() && colourOf(t[i + 1], colour)) {   // a part's colour (the kind's parts are checked when the film is loaded)
					c.colours[t[i]] = colour;
					i += 2;
				} else {
					return fail("\"" + t[i] + "\": a character takes `at X Y Z`, `facing DEGREES`, `scale S`, `detail CELL`, its voice's `pitch SEMITONES`, `rate R` and `voice babble|system|NAME`, and its parts' colours (`sweater red`, `hair 0.2,0.1,0.05`, `skin #f0c8a0`)");
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
		} else if (verb == "terrain") {
			const std::vector<std::string> t = detail::tokens(line);
			if (t.size() < 2 || std::find(terrain::kinds().begin(), terrain::kinds().end(), t[1]) == terrain::kinds().end())
				return fail("`terrain KIND [size S] [height H] [at X Z] [y Y] [focus X Z] [snow H] [erosion E] [seed N] [far off]`: hills, mountains, dunes, canyon or coast");
			if (s.hasTerrain) return fail("a film has one terrain");
			terrain::Spec &ts = s.terrain;
			ts.kind = t[1];
			if (ts.kind == "mountains") ts.size = 1500.0, ts.height = 320.0;
			else if (ts.kind == "dunes") ts.size = 800.0, ts.height = 35.0;
			else if (ts.kind == "canyon") ts.size = 1000.0, ts.height = 120.0;
			else if (ts.kind == "coast") ts.height = 40.0;
			for (std::size_t i = 2; i < t.size();) {
				double a = 0.0, b = 0.0;
				if ((t[i] == "at" || t[i] == "focus") && i + 2 < t.size() && number(t[i + 1], a) && number(t[i + 2], b)) {
					if (t[i] == "at") ts.x = a, ts.z = b;
					else ts.focusX = a, ts.focusZ = b, ts.focusSet = true;
					i += 3;
				} else if (t[i] == "far" && i + 1 < t.size() && (t[i + 1] == "off" || t[i + 1] == "on")) {
					ts.ranges = t[i + 1] == "on";
					i += 2;
				} else if (i + 1 < t.size() && number(t[i + 1], a)) {
					if (t[i] == "size" && a >= 20.0 && a <= 20000.0) ts.size = a;
					else if (t[i] == "height" && a > 0.0 && a <= 5000.0) ts.height = a;
					else if (t[i] == "y") ts.y = a, ts.ySet = true;
					else if (t[i] == "snow" && a >= 0.0) ts.snow = a;
					else if (t[i] == "erosion" && a >= 0.0 && a <= 3.0) ts.erosion = a;
					else if (t[i] == "seed") ts.seed = static_cast<int>(a);
					else return fail("\"" + t[i] + "\": a terrain takes size (20 to 20000 m), height (up to 5000 m), at X Z, y, focus X Z, snow, erosion (0 to 3), seed and far off");
					i += 2;
				} else {
					return fail("\"" + t[i] + "\": a terrain takes size, height, at X Z, y, focus X Z, snow, erosion, seed and far off");
				}
			}
			s.hasTerrain = true;
		} else if (verb == "water") {
			const std::vector<std::string> t = detail::tokens(line);
			water::Sea sea;
			std::size_t i = 1;
			if (i < t.size() && std::find(water::seaKinds().begin(), water::seaKinds().end(), t[i]) != water::seaKinds().end()) sea.kind = t[i++];
			if (sea.kind == "lake") sea.waves = 0.05, sea.choppy = 0.4;
			bool level = false;
			for (; i < t.size();) {
				double v = 0.0;
				if (t[i] == "foam" && i + 1 < t.size() && (t[i + 1] == "off" || t[i + 1] == "on")) {
					sea.foam = t[i + 1] == "on";
				} else if (t[i] == "model" && i + 1 < t.size() && (t[i + 1] == "spectrum" || t[i + 1] == "gerstner")) {
					sea.spectral = t[i + 1] == "spectrum";
				} else if (i + 1 < t.size() && number(t[i + 1], v)) {
					if (t[i] == "level") sea.level = v, level = true;
					else if (t[i] == "waves" && v >= 0.0 && v <= 15.0) sea.waves = v;
					else if (t[i] == "wind") sea.wind = v;
					else if (t[i] == "choppy" && v >= 0.0 && v <= 1.0) sea.choppy = v;
					else return fail("\"" + t[i] + "\": water takes level, waves (0 to 15 m), wind, choppy (0 to 1) and foam off");
				} else {
					return fail("`water [sea|lake|clear] level Y [waves H] [wind DEGREES] [choppy C] [foam off] [model spectrum|gerstner]`");
				}
				i += 2;
			}
			if (!level) return fail("water needs its `level Y` (its surface's height, pbrt's y: not above the ground)");
			if (!s.seas.empty()) return fail("a film has one sea or lake (rivers and waterfalls as many as it likes)");
			s.seas.push_back(sea);
		} else if (verb == "river") {
			const std::vector<std::string> t = detail::tokens(line);
			water::River rv;
			std::size_t i = 1;
			if (i < t.size() && t[i] == "canyon") rv.canyon = true, ++i;
			else if (i < t.size() && t[i] == "from") rv.downhill = true, ++i;
			for (; i + 1 < t.size() && !(rv.canyon || (rv.downhill && rv.points.size() == 1)); i += 2) {
				double x = 0.0, z = 0.0;
				if (!number(t[i], x) || !number(t[i + 1], z)) break;
				rv.points.push_back({x, z});
			}
			for (; i < t.size(); i += 2) {
				double v = 0.0;
				if (i + 1 >= t.size() || !number(t[i + 1], v) || v < 0.0) return fail("\"" + t[i] + "\": a river takes width, speed and y, each with a number");
				if (t[i] == "width" && v > 0.0) rv.width = v;
				else if (t[i] == "speed") rv.speed = v;
				else if (t[i] == "y") rv.y = v;
				else return fail("\"" + t[i] + "\": a river takes width, speed and y");
			}
			if (!rv.canyon && rv.points.size() < (rv.downhill ? 1u : 2u))
				return fail("`river X Z X Z [X Z ...] [width W] [speed S] [y Y]` (at least two waypoints, downstream), `river from X Z ...` (on a terrain: downhill from there) or `river canyon ...`");
			s.rivers.push_back(rv);
		} else if (verb == "waterfall") {
			const std::vector<std::string> t = detail::tokens(line);
			water::Fall fl;
			if (t.size() < 5 || t[1] != "at" || !number(t[2], fl.lip.x) || !number(t[3], fl.lip.y) || !number(t[4], fl.lip.z))
				return fail("`waterfall at X Y Z [width W] [drop H] [facing DEGREES]`: the middle of its lip");
			for (std::size_t i = 5; i < t.size(); i += 2) {
				double v = 0.0;
				if (i + 1 >= t.size() || !number(t[i + 1], v)) return fail("\"" + t[i] + "\": a waterfall takes width, drop and facing, each with a number");
				if (t[i] == "width" && v > 0.0 && v <= 200.0) fl.width = v;
				else if (t[i] == "drop" && v > 0.0 && v <= 1000.0) fl.drop = v;
				else if (t[i] == "facing") fl.facing = v;
				else return fail("\"" + t[i] + "\": a waterfall takes width (up to 200 m), drop (up to 1000 m) and facing");
			}
			s.falls.push_back(fl);
		} else if (verb == "clouds") {
			const std::vector<std::string> t = detail::tokens(line);
			clouds::Clouds c;
			if (t.size() < 2 || std::find(clouds::kinds().begin(), clouds::kinds().end(), t[1]) == clouds::kinds().end())
				return fail("`clouds KIND [cover C] [base H] [size W] [wind DEGREES] [speed S] [density D] [area A] [at X Z] [seed N]`: cumulus, towering or wisps");
			c.kind = t[1];
			if (c.kind == "towering") c.size = 1500.0, c.base = 1000.0, c.cover = 0.2, c.density = 1.4;
			else if (c.kind == "wisps") c.size = 1400.0, c.base = 5000.0, c.cover = 0.3, c.density = 0.35, c.area = 12000.0;
			for (std::size_t i = 2; i < t.size();) {
				double a = 0.0, b = 0.0;
				if (t[i] == "at" && i + 2 < t.size() && number(t[i + 1], a) && number(t[i + 2], b)) {
					c.x = a, c.z = b;
					i += 3;
				} else if (i + 1 < t.size() && number(t[i + 1], a)) {
					if (t[i] == "cover" && a > 0.0 && a <= 1.0) c.cover = a;
					else if (t[i] == "base") c.base = a;
					else if (t[i] == "size" && a >= 20.0 && a <= 5000.0) c.size = a;
					else if (t[i] == "wind") c.wind = a;
					else if (t[i] == "speed" && a >= 0.0) c.speed = a;
					else if (t[i] == "density" && a > 0.0 && a <= 10.0) c.density = a;
					else if (t[i] == "area" && a >= 100.0) c.area = a;
					else if (t[i] == "seed") c.seed = static_cast<int>(a);
					else return fail("\"" + t[i] + "\": clouds take cover (0 to 1), base, size (20 to 5000 m), wind, speed, density (up to 10), area, at X Z and seed");
					i += 2;
				} else {
					return fail("\"" + t[i] + "\": clouds take cover, base, size, wind, speed, density, area, at X Z and seed");
				}
			}
			s.cloudLayers.push_back(c);
		} else if (verb == "forest") {
			const std::vector<std::string> t = detail::tokens(line);
			forest::Forest f;
			if (t.size() < 2 || std::find(forest::kinds().begin(), forest::kinds().end(), t[1]) == forest::kinds().end())
				return fail("`forest KIND [COUNT] [in X0 Z0 X1 Z1] [below H] [above H] [steep S] [scale A B] [near D] [season S] [seed N]`: conifer, broadleaf, birch, palm, shrub, acacia, kapok, cactus or fern, or a mix: mixed, savanna, jungle, desert or tundra");
			f.kind = t[1];
			std::size_t i = 2;
			double count = 0.0;
			if (i < t.size() && number(t[i], count)) {
				if (count < 1 || count > 50000) return fail("a forest has 1 to 50000 trees");
				f.count = static_cast<int>(count), ++i;
			}
			for (; i < t.size();) {
				double a = 0.0, b = 0.0, c = 0.0, d = 0.0;
				if (t[i] == "in" && i + 4 < t.size() && number(t[i + 1], a) && number(t[i + 2], b) && number(t[i + 3], c) && number(t[i + 4], d)) {
					f.x0 = std::min(a, c), f.z0 = std::min(b, d), f.x1 = std::max(a, c), f.z1 = std::max(b, d), f.area = true;
					i += 5;
				} else if (t[i] == "scale" && i + 2 < t.size() && number(t[i + 1], a) && number(t[i + 2], b) && a > 0.0 && b >= a) {
					f.scale0 = a, f.scale1 = b;
					i += 3;
				} else if (t[i] == "season" && i + 1 < t.size() && std::find(forest::seasons().begin(), forest::seasons().end(), t[i + 1]) != forest::seasons().end()) {
					f.season = t[i + 1];
					i += 2;
					if (i < t.size() && t[i] == "to") {   // a change of season: `season A to B from T0 to T1`
						if (!(i + 5 < t.size() && std::find(forest::seasons().begin(), forest::seasons().end(), t[i + 1]) != forest::seasons().end() && t[i + 1] != f.season &&
							  t[i + 2] == "from" && number(t[i + 3], a) && t[i + 4] == "to" && number(t[i + 5], b) && b > a))
							return fail("`season A to B from T0 to T1`: from one season to another (summer, autumn, winter, spring), each tree turning in its own time between T0 and T1");
						f.seasonTo = t[i + 1], f.turn0 = a, f.turn1 = b;
						i += 6;
						std::string last = f.seasonTo;
						double lastEnd = b;
						while (i < t.size() && t[i] == "then") {   // and on: `then C from T2 to T3`
							if (!(i + 5 < t.size() && std::find(forest::seasons().begin(), forest::seasons().end(), t[i + 1]) != forest::seasons().end() && t[i + 1] != last &&
								  t[i + 2] == "from" && number(t[i + 3], a) && t[i + 4] == "to" && number(t[i + 5], b) && b > a && a >= lastEnd))
								return fail("`... then C from T2 to T3`: on to another season, after the change before it has ended");
							f.then.push_back({t[i + 1], a, b});
							last = t[i + 1], lastEnd = b;
							i += 6;
						}
					}
				} else if (t[i] == "grow" && i + 2 < t.size() && number(t[i + 1], a) && number(t[i + 2], b) && b > a && a >= 0.0) {
					f.grow0 = a, f.grow1 = b;
					i += 3;
				} else if (i + 1 < t.size() && number(t[i + 1], a)) {
					if (t[i] == "below" && a >= 0.0) f.below = a;
					else if (t[i] == "above") f.above = a;
					else if (t[i] == "steep" && a > 0.0 && a <= 1.0) f.steep = a;
					else if (t[i] == "near" && a >= 0.0) f.fullWithin = a;
					else if (t[i] == "seed") f.seed = static_cast<int>(a);
					else return fail("\"" + t[i] + "\": a forest takes in X0 Z0 X1 Z1, below, above, steep (0 to 1), scale A B, near, season (summer, autumn, winter or spring; `to B from T0 to T1`), grow T0 T1 and seed");
					i += 2;
				} else {
					return fail("\"" + t[i] + "\": a forest takes in X0 Z0 X1 Z1, below, above, steep, scale A B, near, season, grow T0 T1 and seed");
				}
			}
			s.forests.push_back(f);
		} else if (verb == "biome") {   // the land as a biome: savanna, jungle, desert, tundra or arctic
			const std::vector<std::string> t = detail::tokens(line);
			static const std::vector<std::string> biomes = {"savanna", "jungle", "desert", "tundra", "arctic"};
			double count = 0.0;
			if (t.size() < 2 || std::find(biomes.begin(), biomes.end(), t[1]) == biomes.end() ||
				!(t.size() == 2 || (t.size() == 4 && t[2] == "plants" && number(t[3], count) && count >= 0.0 && count <= 50000.0)))
				return fail("`biome NAME [plants N]`: savanna, jungle, desert, tundra or arctic - the land's colours, its plants (N of them; by the land's size when not given) and its sound");
			s.biome = t[1];
			if (t.size() == 4) s.biomeTrees = static_cast<int>(count);
		} else if (verb == "flowers") {   // a meadow of wild flowers, opening and closing (film_flowers.h)
			const std::vector<std::string> t = detail::tokens(line);
			flowers::Meadow m;
			double count = 0.0;
			if (t.size() < 3 || std::find(flowers::kinds().begin(), flowers::kinds().end(), t[1]) == flowers::kinds().end() || !number(t[2], count) || count < 1 || count > 100000)
				return fail("`flowers KIND COUNT [in X0 Z0 X1 Z1] [bloom T0 T1] [close T0 T1] [scale S] [seed N]`: poppies, daisies, lupins or sunflowers, 1 to 100000 of them");
			m.kind = t[1];
			m.count = static_cast<int>(count);
			for (std::size_t i = 3; i < t.size();) {
				double a = 0.0, b = 0.0, c = 0.0, d = 0.0;
				if (t[i] == "in" && i + 4 < t.size() && number(t[i + 1], a) && number(t[i + 2], b) && number(t[i + 3], c) && number(t[i + 4], d)) {
					m.x0 = std::min(a, c), m.z0 = std::min(b, d), m.x1 = std::max(a, c), m.z1 = std::max(b, d);
					i += 5;
				} else if ((t[i] == "bloom" || t[i] == "close") && i + 2 < t.size() && number(t[i + 1], a) && number(t[i + 2], b) && b > a) {
					(t[i] == "bloom" ? m.bloom0 : m.close0) = a;
					(t[i] == "bloom" ? m.bloom1 : m.close1) = b;
					i += 3;
				} else if ((t[i] == "scale" || t[i] == "seed") && i + 1 < t.size() && number(t[i + 1], a) && (t[i] == "seed" || a > 0.0)) {
					if (t[i] == "scale") m.scale = a;
					else m.seed = static_cast<int>(a);
					i += 2;
				} else {
					return fail("\"" + t[i] + "\": flowers take in X0 Z0 X1 Z1, bloom T0 T1 (each opening in its own time between them), close T0 T1, scale and seed");
				}
			}
			s.meadows.push_back(m);
		} else if (verb == "flock") {
			const std::vector<std::string> t = detail::tokens(line);
			wildlife::Flock f;
			double count = 0.0;
			if (t.size() < 3 || std::find(wildlife::flockKinds().begin(), wildlife::flockKinds().end(), t[1]) == wildlife::flockKinds().end() || !number(t[2], count) || count < 1 || count > 3000)
				return fail("`flock KIND COUNT ...`: starlings, geese, fish, butterflies or eagles, 1 to 3000 of them (`at X Y Z` and `radius R`; geese `from X Y Z to X Y Z`; butterflies `in X0 Z0 X1 Z1`)");
			f.kind = t[1];
			f.count = static_cast<int>(count);
			if (f.kind == "fish") f.radius = 6.0;
			if (f.kind == "eagles") f.radius = 60.0;
			bool where = false;
			for (std::size_t i = 3; i < t.size();) {
				double a = 0.0, b = 0.0, c = 0.0, d = 0.0;
				if ((t[i] == "at" || t[i] == "from" || t[i] == "to") && i + 3 < t.size() && number(t[i + 1], a) && number(t[i + 2], b) && number(t[i + 3], c)) {
					(t[i] == "at" ? f.at : t[i] == "from" ? f.from : f.to) = Vec3{a, b, c};
					where = where || t[i] != "to";
					i += 4;
				} else if (t[i] == "in" && i + 4 < t.size() && number(t[i + 1], a) && number(t[i + 2], b) && number(t[i + 3], c) && number(t[i + 4], d)) {
					f.x0 = std::min(a, c), f.z0 = std::min(b, d), f.x1 = std::max(a, c), f.z1 = std::max(b, d);
					where = true;
					i += 5;
				} else if (t[i] == "during" && i + 2 < t.size() && number(t[i + 1], a) && number(t[i + 2], b) && b > a) {
					f.t0 = a, f.t1 = b;
					i += 3;
				} else if (i + 1 < t.size() && number(t[i + 1], a)) {
					if (t[i] == "radius" && a > 0.0) f.radius = a;
					else if (t[i] == "speed" && a > 0.0) f.speed = a;
					else if (t[i] == "height") f.at.y = a;
					else if (t[i] == "seed") f.seed = static_cast<int>(a);
					else return fail("\"" + t[i] + "\": a flock takes at X Y Z, from X Y Z, to X Y Z, in X0 Z0 X1 Z1, radius, speed, height, during T0 T1 and seed");
					i += 2;
				} else {
					return fail("\"" + t[i] + "\": a flock takes at X Y Z, from X Y Z, to X Y Z, in X0 Z0 X1 Z1, radius, speed, height, during T0 T1 and seed");
				}
			}
			if (!where) return fail(f.kind == "geese" ? "geese fly `from X Y Z to X Y Z`" : f.kind == "butterflies" ? "butterflies flit `in X0 Z0 X1 Z1`" : "a flock needs its `at X Y Z`");
			s.flocks.push_back(f);
		} else if (verb == "herd") {
			const std::vector<std::string> t = detail::tokens(line);
			wildlife::Herd h;
			double count = 0.0;
			if (t.size() < 9 || std::find(wildlife::herdKinds().begin(), wildlife::herdKinds().end(), t[1]) == wildlife::herdKinds().end() || !number(t[2], count) || count < 1 || count > 500 ||
				t[3] != "from" || !number(t[4], h.fx) || !number(t[5], h.fz) || t[6] != "to" || !number(t[7], h.tx) || !number(t[8], h.tz))
				return fail("`herd KIND COUNT from X Z to X Z [speed S] [start T] [flee T] [seed N]`: deer, horses or wolves, 1 to 500 of them");
			h.kind = t[1];
			h.count = static_cast<int>(count);
			for (std::size_t i = 9; i < t.size(); i += 2) {
				double a = 0.0;
				if (i + 1 >= t.size() || !number(t[i + 1], a)) return fail("\"" + t[i] + "\": a herd takes speed, start, flee and seed, each with a number");
				if (t[i] == "speed" && a > 0.0) h.speed = a;
				else if (t[i] == "start") h.t0 = a;
				else if (t[i] == "flee" && a >= 0.0) h.flee = a;
				else if (t[i] == "seed") h.seed = static_cast<int>(a);
				else return fail("\"" + t[i] + "\": a herd takes speed, start, flee (when it bolts) and seed");
			}
			s.herds.push_back(h);
		} else if (verb == "aurora" || verb == "lightning" || verb == "meteors" || verb == "rainbow") {
			const std::vector<std::string> t = detail::tokens(line);
			skyfx::Aurora au;
			skyfx::Lightning li;
			skyfx::Meteors me;
			Spec::Rainbow rb;
			double *t0 = verb == "aurora" ? &au.t0 : verb == "lightning" ? &li.t0 : verb == "meteors" ? &me.t0 : &rb.t0;
			double *t1 = verb == "aurora" ? &au.t1 : verb == "lightning" ? &li.t1 : verb == "meteors" ? &me.t1 : &rb.t1;
			std::size_t i = 1;
			double v = 0.0;
			if (verb == "aurora" && i < t.size() && std::find(skyfx::auroraColours().begin(), skyfx::auroraColours().end(), t[i]) != skyfx::auroraColours().end()) au.colour = t[i++];
			if (i < t.size() && number(t[i], v)) {   // (a count, or a rainbow's strength)
				if (verb == "lightning" && v >= 1 && v <= 200) li.count = static_cast<int>(v);
				else if (verb == "meteors" && v >= 1 && v <= 500) me.count = static_cast<int>(v);
				else if (verb == "rainbow" && v > 0.0 && v <= 1.0) rb.strength = v;
				else return fail(verb == "rainbow" ? "a rainbow's strength is from 0 to 1" : verb == "aurora" ? "`aurora [green|red|violet] [brightness B] [facing DEGREES] [distance M] [during T0 T1]`"
																							: "`" + verb + " [COUNT]`: " + (verb == "lightning" ? "1 to 200 strikes" : "1 to 500 meteors"));
				++i;
			}
			for (; i < t.size();) {
				double a = 0.0, b = 0.0;
				if (t[i] == "during" && i + 2 < t.size() && number(t[i + 1], a) && number(t[i + 2], b) && b > a) {
					*t0 = a, *t1 = b;
					i += 3;
				} else if (verb == "lightning" && t[i] == "at" && i + 2 < t.size() && number(t[i + 1], a) && number(t[i + 2], b)) {
					li.x = a, li.z = b;
					i += 3;
				} else if (i + 1 < t.size() && number(t[i + 1], a)) {
					if (verb == "aurora" && t[i] == "brightness" && a > 0.0) au.brightness = a;
					else if (verb == "aurora" && t[i] == "facing") au.facing = a, au.facingSet = true;
					else if (verb == "aurora" && t[i] == "distance" && a > 100.0) au.distance = a;
					else if (verb == "lightning" && t[i] == "radius" && a >= 0.0) li.radius = a;
					else if (verb == "lightning" && t[i] == "base" && a > 0.0) li.base = a;
					else if ((verb == "lightning" || verb == "meteors") && t[i] == "seed") (verb == "lightning" ? li.seed : me.seed) = static_cast<int>(a);
					else return fail("\"" + t[i] + "\" is not part of a `" + verb + "` line (aurora: brightness, facing, distance; lightning: at X Z, radius, base, seed; meteors: seed; all: during T0 T1)");
					i += 2;
				} else {
					return fail("\"" + t[i] + "\" is not part of a `" + verb + "` line");
				}
			}
			if (verb == "aurora") s.auroras.push_back(au);
			else if (verb == "lightning") s.lightnings.push_back(li);
			else if (verb == "meteors") s.meteors.push_back(me);
			else s.rainbows.push_back(rb);
		} else if (verb == "sky") {
			const std::vector<std::string> t = detail::tokens(line);
			const auto hour = [&](const std::string &w, double &h) {   // 18:30 or 18.5
				const std::size_t colon = w.find(':');
				double a = 0.0, b = 0.0;
				if (colon == std::string::npos) return number(w, h) && h >= 0.0 && h <= 24.0;
				if (!number(w.substr(0, colon), a) || !number(w.substr(colon + 1), b) || a < 0 || a > 24 || b < 0 || b >= 60) return false;
				h = a + b / 60.0;
				return h <= 24.0;   // (24:59 is not a time of day)
			};
			if (t.size() < 3 || !hour(t[1], s.sky.from) || !hour(t[2], s.sky.to))
				return fail("`sky FROM TO [south DEGREES] [latitude L] [day N] [haze H] [physical] [altitude A]`: times of day as 18:30 or 18.5");
			for (std::size_t i = 3; i < t.size(); i += 2) {
				double v = 0.0;
				if (t[i] == "physical") {   // (a word on its own: the sky from the atmosphere itself, atmosphere.h)
					s.sky.physical = true;
					--i;
					continue;
				}
				if (i + 1 >= t.size() || !number(t[i + 1], v)) return fail("a sky takes south, latitude, day, haze and altitude, each with a number, and physical");
				if (t[i] == "altitude" && v >= 0.0 && v <= 9000.0) {
					s.sky.altitude = v;
					continue;
				}
				if (t[i] == "south") s.sky.south = v;
				else if (t[i] == "latitude" && std::abs(v) <= 89.0) s.sky.latitude = v;
				else if (t[i] == "day" && v >= 1 && v <= 366) s.sky.day = static_cast<int>(v);
				else if (t[i] == "haze" && v >= 1.7 && v <= 10.0) s.sky.haze = v;
				else return fail("\"" + t[i] + "\": a sky takes south, latitude (-89 to 89), day (1 to 366), haze (1.7 to 10), physical and altitude (0 to 9000 m)");
			}
			s.hasSky = true;
		} else if (verb == "weather") {
			const std::vector<std::string> t = detail::tokens(line);
			particles::Weather wt;
			if (t.size() < 2 || std::find(particles::weatherKinds().begin(), particles::weatherKinds().end(), t[1]) == particles::weatherKinds().end())
				return fail("`weather KIND [AMOUNT] [from T] [to T] [ground Y]`: rain, snow, leaves or dust");
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
		} else if (verb == "mute") {
			const std::vector<std::string> t = detail::tokens(line);
			if (t.size() != 2 || t[1] != "birds") return fail("`mute birds`: no birds heard (their songs in the trees, eagles' cries, geese, the night's owl), though they are seen");
			s.muteBirds = true;
		} else if (verb == "ambience") {
			const std::vector<std::string> t = detail::tokens(line);
			dressing::Ambience a;
			if (t.size() < 2 || std::find(dressing::ambienceKinds().begin(), dressing::ambienceKinds().end(), t[1]) == dressing::ambienceKinds().end())
				return fail("`ambience KIND [GAIN]`: wind, birds, crickets, water or night");
			a.kind = t[1];
			if (t.size() > 3 || (t.size() == 3 && (!number(t[2], a.gain) || a.gain < 0.0 || a.gain > 4.0))) return fail("an ambience's gain is a number from 0 to 4");
			s.ambience.push_back(a);
		} else if (verb == "rigged") {   // a kind of character from a rigged glTF model (film_rigged.h)
			const std::vector<std::string> t = detail::tokens(line);
			Spec::Rigged rg;
			rg.line = n;
			rg.dir = sources.back().dir;
			if (t.size() < 3 || !detail::validKey(t[1])) return fail("`rigged KIND FILE.glb [height H] [stride S] [turn DEGREES] [walk CLIP] [idle CLIP] [run CLIP]` (KIND: lower-case letters, digits and '-')");
			rg.kind = t[1];
			rg.file = t[2];
			for (std::size_t i = 3; i < t.size(); i += 2) {
				if (i + 1 >= t.size()) return fail("\"" + t[i] + "\" needs a value");
				double v = 0.0;
				if (t[i] == "height" || t[i] == "stride" || t[i] == "turn") {
					if (!number(t[i + 1], v) || (t[i] != "turn" && !(v > 0.0))) return fail("a rigged model's " + t[i] + " is a number" + (t[i] == "turn" ? "" : " above 0"));
					(t[i] == "height" ? rg.options.height : t[i] == "stride" ? rg.options.stride : rg.options.turn) = v;
				} else if (t[i] == "walk" || t[i] == "idle" || t[i] == "run") {
					(t[i] == "walk" ? rg.options.walk : t[i] == "idle" ? rg.options.idle : rg.options.run) = detail::lowerWord(t[i + 1]);
				} else {
					return fail("\"" + t[i] + "\": a rigged model takes height, stride, turn, walk, idle and run");
				}
			}
			s.rigged.push_back(rg);
		} else if (verb == "title") {   // a title card over the opening
			const std::vector<std::string> t = detail::tokens(line);
			s.hasTitle = true;
			for (std::size_t i = 1; i < t.size(); ++i) {
				if ((t[i] == "at" || t[i] == "for") && i + 1 < t.size()) {
					if (!number(t[i + 1], t[i] == "at" ? s.titleAt : s.titleFor) || s.titleAt < 0.0 || !(s.titleFor > 0.5)) return fail("a title's `at` is a time and its `for` more than half a second");
					++i;
				} else if (i == 1) {
					s.title = t[i];
				} else {
					return fail("`title [\"TEXT\"] [at T] [for SECONDS]` (the text: the film's name unless given)");
				}
			}
		} else if (verb == "episode") {
			const std::vector<std::string> t = detail::tokens(line);
			double v = 0.0;
			if (t.size() < 2 || t.size() > 3 || !number(t[1], v) || v < 1 || v != std::floor(v)) return fail("`episode N [\"NAME\"]`: its number (from 1) and its name");
			s.episode = static_cast<int>(v);
			if (t.size() == 3) s.episodeName = t[2];
			s.hasTitle = true;
		} else if (verb == "credits") {
			if (w.size() != 1) return fail("`credits` (the cast and the film's own `credit` lines, on cards after the story)");
			s.credits = true;
		} else if (verb == "credit") {
			const std::vector<std::string> t = detail::tokens(line);
			if (t.size() != 2) return fail("`credit \"TEXT\"`: a line of the credits (\"Music by Sam\")");
			s.creditLines.push_back(t[1]);
			s.credits = true;
		} else if (verb == "theme") {
			const std::vector<std::string> t = detail::tokens(line);
			if (t.size() != 2 && !(t.size() == 4 && t[2] == "gain" && number(t[3], s.themeGain) && s.themeGain >= 0.0 && s.themeGain <= 4.0))
				return fail("`theme jingle|FILE.wav [gain G]`: a fanfare under the title, or a recording of the film library");
			s.theme = t[1];
		} else if (verb == "touch" || verb == "hold" || verb == "hug") {   // hands on the set, or on another character
			const std::vector<std::string> t = detail::tokens(line);
			Spec::Contact c;
			c.kind = verb == "touch" ? Spec::Contact::Touch : verb == "hold" ? Spec::Contact::Hold : Spec::Contact::Hug;
			c.line = n;
			if (verb == "touch") {
				if (t.size() < 7 || t.size() > 8 || !number(t[2], c.t0) || !number(t[3], c.t1) || !number(t[4], c.at.x) || !number(t[5], c.at.y) || !number(t[6], c.at.z) ||
					(t.size() == 8 && t[7] != "left" && t[7] != "right"))
					return fail("`touch NAME T0 T1 X Y Z [left|right]`: a hand on a place of the set (a wall to lean on, a door's handle) from T0 to T1");
				if (t.size() == 8) c.hand = t[7] == "left" ? 0 : 1;
			} else {
				if (t.size() != 5 || !number(t[3], c.t0) || !number(t[4], c.t1)) return fail("`" + verb + " NAME OTHER T0 T1`: from T0 to T1");
				c.other = t[2];
				if (c.other == t[1]) return fail("a character " + verb + "s someone else");
			}
			c.who = t[1];
			if (!(c.t1 > c.t0) || c.t0 < 0.0) return fail("`" + verb + "` lasts from T0 to a later T1");
			s.contacts.push_back(c);
		} else if (verb == "acting") {   // gestures and expressions from the words of the lines (film_acting.h)
			if (w.size() != 2 || (w[1] != "words" && w[1] != "off")) return fail("`acting words` (each line acted from its words) or `acting off`");
			s.actingWords = w[1] == "words";
		} else if (verb == "voices") {   // what every character's lines are said in (film_tts.h)
			if (w.size() != 2) return fail("`voices babble|system|NAME`: the babble voice, one of the computer's own voices, or the computer voice with NAME in its name");
			s.voices = detail::lowerWord(w[1]);
		} else if (verb == "toon") {   // the toon look on every frame (toon_look.h)
			if (rest == "off") s.toon = toon_look::Style();
			else if (const std::string e = toon_look::parse(rest, s.toon); !e.empty()) return fail(e);
		} else if (verb == "music") {
			const std::vector<std::string> t = detail::tokens(line);
			if (t.size() != 2 || (t[1] != "none" && t[1] != "draft")) return fail("`music none` (no music) or `music draft` (the drafted score when there is no cues file)");
			s.draftMusic = t[1] == "draft";
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
		} else if (verb == "fly") {
			if (s.shots.empty()) return fail("`fly` belongs to a shot: start one with `shot 0 NAME` first");
			const std::vector<std::string> t = detail::tokens(line);
			Shot &shot = s.shots.back();
			std::size_t i = 1;
			for (; i + 1 < t.size(); i += 2) {
				double x = 0.0, z = 0.0;
				if (!number(t[i], x) || !number(t[i + 1], z)) break;
				shot.fly.push_back({x, z});
			}
			for (; i < t.size();) {
				double a = 0.0, b = 0.0, c = 0.0;
				if (t[i] == "look" && i + 3 < t.size() && number(t[i + 1], a) && number(t[i + 2], b) && number(t[i + 3], c)) {
					shot.flyLook = true, shot.flyAt = Vec3{a, b, c};
					i += 4;
				} else if ((t[i] == "height" || t[i] == "fov") && i + 1 < t.size() && number(t[i + 1], a) && a > 0.0) {
					(t[i] == "height" ? shot.flyHeight : shot.flyFov) = a;
					i += 2;
				} else {
					return fail("\"" + t[i] + "\": `fly X Z X Z [X Z ...] [height H] [look X Y Z] [fov F]` (waypoints, pbrt's x z; heights above the land)");
				}
			}
			if (shot.fly.size() < 2) return fail("`fly X Z X Z [X Z ...] [height H] [look X Y Z] [fov F]`: at least two waypoints");
		} else if (verb == "wet" || verb == "snowcover") {
			const std::vector<std::string> t = detail::tokens(line);
			Spec::Ramp r;
			if (t.size() < 2 || !number(t[1], r.amount) || r.amount < 0.0 || r.amount > 1.0 || !(t.size() == 2 || (t.size() == 6 && t[2] == "from" && number(t[3], r.t0) && t[4] == "to" && number(t[5], r.t1) && r.t1 > r.t0)))
				return fail("`" + verb + " AMOUNT [from T0 to T1]`: 0 to 1, reached from T0 to T1 (or from the start)");
			(verb == "wet" ? s.wetness : s.snowcover).push_back(r);
		} else if (verb == "gusts") {
			const std::vector<std::string> t = detail::tokens(line);
			double a = 0.0, wind = 0.0;
			if (t.size() < 2 || !number(t[1], s.gusts.strength) || s.gusts.strength <= 0.0 || s.gusts.strength > 5.0)
				return fail("`gusts STRENGTH [wind DEGREES] [speed S]`: a strength from 0 to 5 (1 a stiff breeze)");
			for (std::size_t i = 2; i < t.size(); i += 2) {
				if (i + 1 >= t.size() || !number(t[i + 1], a)) return fail("\"" + t[i] + "\": gusts take wind and speed, each with a number");
				if (t[i] == "wind") wind = a;
				else if (t[i] == "speed" && a > 0.0) s.gusts.speed = a;
				else return fail("\"" + t[i] + "\": gusts take wind and speed");
			}
			s.gusts.dx = -std::sin(wind * kPi / 180.0), s.gusts.dz = std::cos(wind * kPi / 180.0);   // (story space)
		} else if (verb == "mist") {
			const std::vector<std::string> t = detail::tokens(line);
			if (t.size() < 2 || !number(t[1], s.mist.density) || s.mist.density <= 0.0 || s.mist.density > 1.0 ||
				!(t.size() == 2 || (t.size() == 4 && t[2] == "radius" && number(t[3], s.mist.radius) && s.mist.radius > 1.0)))
				return fail("`mist DENSITY [radius R]`: scattering per metre (0.02 a light haze, 0.08 a thick mist) round the camera, out to R metres (60)");
			s.hasMist = true;
		} else if (verb == "surf") {   // swell breaking on the shore (film_surf.h)
			const std::vector<std::string> t = detail::tokens(line);
			if (t.size() < 2 || !number(t[1], s.swell.height) || !(s.swell.height > 0.0) || s.swell.height > 8.0)
				return fail("`surf HEIGHT [period SECONDS] [toward DEGREES]`: the swell's height out at sea (0.5 to 3 m a beach, up to 8), breaking on the land's shore");
			for (std::size_t i = 2; i < t.size(); i += 2) {
				double v = 0.0;
				if (i + 1 >= t.size() || !number(t[i + 1], v)) return fail("\"" + t[i] + "\" needs a number");
				if (t[i] == "period" && v >= 3.0 && v <= 25.0) s.swell.period = v;
				else if (t[i] == "toward") s.swell.toward = v, s.swell.towardSet = true;
				else return fail("\"" + t[i] + "\": surf takes period (3 to 25 s) and toward (which way it runs, degrees as a facing)");
			}
			s.swell.on = true;
		} else if (verb == "storm") {   // a storm building up and dying away: its sky, its wind, its rain, snow or dust, its lightning
			const std::vector<std::string> t = detail::tokens(line);
			std::size_t i = 1;
			if (i < t.size() && (t[i] == "rain" || t[i] == "snow" || t[i] == "sand")) s.storm.kind = t[i++];
			for (; i < t.size(); i += 2) {
				double v = 0.0;
				if (i + 1 >= t.size() || !number(t[i + 1], v)) return fail("`storm [rain|snow|sand] [strength S] [from T] [to T] [wind D] [speed V]`: \"" + t[i] + "\" needs a number");
				if (t[i] == "strength" && v > 0.0 && v <= 1.0) s.storm.strength = v;
				else if (t[i] == "from" && v >= 0.0) s.storm.t0 = v;
				else if (t[i] == "to") s.storm.t1 = v;
				else if (t[i] == "wind") s.storm.wind = v;
				else if (t[i] == "speed" && v >= 0.0 && v <= 40.0) s.storm.speed = v;
				else return fail("\"" + t[i] + "\": a storm takes rain, snow or sand, strength (0 to 1), from T, to T, wind (a facing) and speed (0 to 40 m/s)");
			}
			if (s.storm.t1 <= s.storm.t0) return fail("a storm's `to` is after its `from`");
			s.storm.on = true;
		} else if (verb == "air") {   // the air between the camera and the far land (aerial perspective)
			const std::vector<std::string> t = detail::tokens(line);
			for (std::size_t i = 1; i < t.size(); i += 2) {
				double v = 0.0;
				if (i + 1 >= t.size() || !number(t[i + 1], v)) return fail("`air [strength S] [haze H] [radius R]`: \"" + t[i] + "\" needs a number");
				if (t[i] == "strength" && v > 0.0 && v <= 200.0) s.air.strength = v;
				else if (t[i] == "haze" && v >= 0.2 && v <= 10.0) s.air.haze = v;
				else if (t[i] == "radius" && v >= 50.0) s.air.radius = v;
				else return fail("\"" + t[i] + "\": air takes strength (1 the real air; more for a small land), haze (0.2 very clear to 10, as a sky's) and radius (metres, 50 or more)");
			}
			s.air.on = true;
		} else if (verb == "fog") {   // fog lying in the valleys (height_fog.h)
			const std::vector<std::string> t = detail::tokens(line);
			if (t.size() < 2 || !number(t[1], s.fog.density) || !(s.fog.density > 0.0) || s.fog.density > 2.0)
				return fail("`fog DENSITY [top Y] [thickness T] [lift T0 T1]`: scattering per metre (0.01 a thin haze, 0.05 a white sea of fog) below the height Y");
			for (std::size_t i = 2; i < t.size();) {
				double a = 0.0, b = 0.0;
				if ((t[i] == "top" || t[i] == "thickness") && i + 1 < t.size() && number(t[i + 1], a)) {
					if (t[i] == "top") s.fog.top = a, s.fog.topSet = true;
					else if (a > 0.0) s.fog.thickness = a;
					else return fail("a fog's thickness is above 0");
					i += 2;
				} else if (t[i] == "lift" && i + 2 < t.size() && number(t[i + 1], a) && number(t[i + 2], b) && b > a) {
					s.fog.liftT0 = a, s.fog.liftT1 = b;
					i += 3;
				} else {
					return fail("\"" + t[i] + "\": a fog takes top Y (a height in the world, pbrt's y), thickness T and lift T0 T1 (thinning away and sinking)");
				}
			}
			s.fog.on = true;
		} else if (verb == "underwater") {
			const std::vector<std::string> t = detail::tokens(line);
			for (std::size_t i = 1; i < t.size(); i += 2) {
				double v = 0.0;
				if (i + 1 < t.size() && t[i] == "clarity" && number(t[i + 1], v) && v > 0.05 && v <= 10.0) s.underwater.clarity = v;
				else if (i + 1 < t.size() && t[i] == "colour" && std::find(underwater::colours().begin(), underwater::colours().end(), t[i + 1]) != underwater::colours().end()) s.underwater.colour = t[i + 1];
				else return fail("`underwater [clarity C] [colour blue|green|teal]`: the camera is under the film's `water` (clarity 1 a clear sea, 2 tropical, 0.5 murky)");
			}
			s.hasUnderwater = true;
		} else if (verb == "kelp" || verb == "coral") {
			const std::vector<std::string> t = detail::tokens(line);
			underwater::Kelp kp;
			underwater::Coral co;
			double count = 0.0, a = 0.0, b = 0.0, c = 0.0, d = 0.0;
			if (t.size() < 2 || !number(t[1], count) || count < 1 || count > 2000) return fail("`" + verb + " COUNT [in X0 Z0 X1 Z1]" + (verb == "kelp" ? " [height H]" : "") + " [seed N]` (1 to 2000)");
			(verb == "kelp" ? kp.count : co.count) = static_cast<int>(count);
			for (std::size_t i = 2; i < t.size();) {
				if (t[i] == "in" && i + 4 < t.size() && number(t[i + 1], a) && number(t[i + 2], b) && number(t[i + 3], c) && number(t[i + 4], d)) {
					if (verb == "kelp") kp.x0 = std::min(a, c), kp.z0 = std::min(b, d), kp.x1 = std::max(a, c), kp.z1 = std::max(b, d);
					else co.x0 = std::min(a, c), co.z0 = std::min(b, d), co.x1 = std::max(a, c), co.z1 = std::max(b, d);
					i += 5;
				} else if (i + 1 < t.size() && number(t[i + 1], a)) {
					if (t[i] == "seed") (verb == "kelp" ? kp.seed : co.seed) = static_cast<int>(a);
					else if (verb == "kelp" && t[i] == "height" && a > 0.0) kp.height = a;
					else return fail("\"" + t[i] + "\" is not part of a `" + verb + "` line (in X0 Z0 X1 Z1, " + (verb == "kelp" ? "height, " : "") + "seed)");
					i += 2;
				} else {
					return fail("\"" + t[i] + "\" is not part of a `" + verb + "` line");
				}
			}
			if (verb == "kelp") s.kelps.push_back(kp);
			else s.corals.push_back(co);
		} else if (verb == "fire" || verb == "lava" || verb == "volcano" || verb == "wildfire") {
			const std::vector<std::string> t = detail::tokens(line);
			fire::Fire fi;
			fire::Lava la;
			fire::Volcano vo;
			fire::Wildfire wf;
			std::size_t i = 1;
			double a = 0.0, b = 0.0, c = 0.0, d = 0.0;
			const std::string usage = verb == "fire" ? "`fire at X Y Z [size S] [during T0 T1] [smoke off] [seed N]`"
									  : verb == "lava" ? "`lava from X Z [width W] [speed S] [start T]` (down the land from a vent: it needs a `terrain`)"
									  : verb == "volcano" ? "`volcano at X Z [height H] [radius R] [erupting T0 T1]` (raised in the land: it needs a `terrain`)"
														  : "`wildfire in X0 Z0 X1 Z1 [during T0 T1] [wind DEGREES]`";
			if (verb == "fire") {
				if (t.size() < 5 || t[1] != "at" || !number(t[2], fi.at.x) || !number(t[3], fi.at.y) || !number(t[4], fi.at.z)) return fail(usage);
				i = 5;
			} else if (verb == "lava" || verb == "volcano") {
				if (t.size() < 4 || t[1] != (verb == "lava" ? "from" : "at") || !number(t[2], a) || !number(t[3], b)) return fail(usage);
				(verb == "lava" ? la.x : vo.x) = a, (verb == "lava" ? la.z : vo.z) = b;
				i = 4;
			} else {
				if (t.size() < 6 || t[1] != "in" || !number(t[2], a) || !number(t[3], b) || !number(t[4], c) || !number(t[5], d)) return fail(usage);
				wf.x0 = std::min(a, c), wf.z0 = std::min(b, d), wf.x1 = std::max(a, c), wf.z1 = std::max(b, d);
				i = 6;
			}
			for (; i < t.size();) {
				if ((t[i] == "during" || t[i] == "erupting") && i + 2 < t.size() && number(t[i + 1], a) && number(t[i + 2], b) && b > a) {
					if (verb == "fire") fi.t0 = a, fi.t1 = b;
					else if (verb == "volcano") vo.t0 = a, vo.t1 = b;
					else if (verb == "wildfire") wf.t0 = a, wf.t1 = b;
					else return fail(usage);
					i += 3;
				} else if (verb == "fire" && t[i] == "smoke" && i + 1 < t.size() && t[i + 1] == "off") {
					fi.smoke = false;
					i += 2;
				} else if (i + 1 < t.size() && number(t[i + 1], a)) {
					if (verb == "fire" && t[i] == "size" && a > 0.05 && a <= 100.0) fi.size = a;
					else if (verb == "fire" && t[i] == "seed") fi.seed = static_cast<int>(a);
					else if (verb == "lava" && t[i] == "width" && a > 0.0) la.width = a;
					else if (verb == "lava" && t[i] == "speed" && a > 0.0) la.speed = a;
					else if (verb == "lava" && t[i] == "start") la.t0 = a;
					else if (verb == "volcano" && t[i] == "height" && a > 10.0) vo.height = a;
					else if (verb == "volcano" && t[i] == "radius" && a > 20.0) vo.radius = a;
					else if (verb == "wildfire" && t[i] == "wind") wf.wind = a;
					else return fail("\"" + t[i] + "\": " + usage);
					i += 2;
				} else {
					return fail("\"" + t[i] + "\": " + usage);
				}
			}
			if (verb == "fire") s.fires.push_back(fi);
			else if (verb == "lava") s.lavas.push_back(la);
			else if (verb == "volcano") s.volcanoes.push_back(vo);
			else s.wildfires.push_back(wf);
		} else if (verb == "geyser" || verb == "rockfall" || verb == "avalanche") {   // the land in action (film_geology.h)
			const std::vector<std::string> t = detail::tokens(line);
			const std::string where = verb == "geyser" ? "at" : "from";
			double x = 0.0, z = 0.0;
			if (t.size() < 4 || t[1] != where || !number(t[2], x) || !number(t[3], z))
				return fail(verb == "geyser" ? "`geyser at X Z [height H] [every S] [last D] [first T] [seed N]`: its vent (pbrt's x z)"
					: verb == "rockfall" ? "`rockfall from X Z [at T] [count N] [size S] [seed N]`: where the rocks break away (pbrt's x z), and when"
										 : "`avalanche from X Z [at T] [width W] [speed V]`: where it breaks away (pbrt's x z), and when");
			geology::Geyser gy;
			geology::Rockfall rf;
			geology::Avalanche av;
			gy.x = rf.x = av.x = x, gy.z = rf.z = av.z = z;
			for (std::size_t i = 4; i < t.size(); i += 2) {
				double v = 0.0;
				if (i + 1 >= t.size() || !number(t[i + 1], v)) return fail("\"" + t[i] + "\" needs a number");
				const std::string &k = t[i];
				bool ok = true;
				if (verb == "geyser") {
					if (k == "height" && v > 0.0) gy.height = v;
					else if (k == "every" && v >= 4.0) gy.every = v;
					else if (k == "last" && v >= 2.0) gy.last = v;
					else if (k == "first" && v >= 0.0) gy.first = v;
					else if (k == "seed") gy.seed = static_cast<int>(v);
					else ok = false;
				} else if (verb == "rockfall") {
					if (k == "at" && v >= 0.0) rf.t0 = v;
					else if (k == "count" && v >= 1.0 && v <= 200.0) rf.count = static_cast<int>(v);
					else if (k == "size" && v > 0.0) rf.size = v;
					else if (k == "seed") rf.seed = static_cast<int>(v);
					else ok = false;
				} else {
					if (k == "at" && v >= 0.0) av.t0 = v;
					else if (k == "width" && v > 0.0) av.width = v;
					else if (k == "speed" && v > 0.0) av.speed = v;
					else ok = false;
				}
				if (!ok)
					return fail("\"" + k + "\": " + (verb == "geyser" ? std::string("a geyser takes height, every (4 s or more), last (2 s or more), first and seed")
														 : verb == "rockfall" ? std::string("a rockfall takes at, count (1 to 200), size and seed")
																			  : std::string("an avalanche takes at, width and speed")));
			}
			if (gy.last > gy.every) gy.last = gy.every;
			if (verb == "geyser") s.geysers.push_back(gy);
			else if (verb == "rockfall") s.rockfalls.push_back(rf);
			else s.avalanches.push_back(av);
		} else if (verb == "quake") {   // the ground shaking
			const std::vector<std::string> t = detail::tokens(line);
			geology::Quake q;
			if (t.size() < 3 || t.size() > 5 || !number(t[1], q.t0) || !number(t[2], q.t1) || q.t1 <= q.t0 ||
				(t.size() > 3 && !(t.size() == 5 && t[3] == "strength" && number(t[4], q.strength) && q.strength > 0.0 && q.strength <= 3.0)))
				return fail("`quake T0 T1 [strength S]`: the ground shaking from T0 to T1 (strength 1 a strong quake, up to 3)");
			s.quakes.push_back(q);
		} else if (verb == "slowmo") {   // nature in slow motion for a while
			const std::vector<std::string> t = detail::tokens(line);
			Spec::SlowMo sm;
			if (t.size() < 3 || t.size() > 4 || !number(t[1], sm.t0) || !number(t[2], sm.t1) || sm.t1 <= sm.t0 || (t.size() == 4 && (!number(t[3], sm.rate) || sm.rate <= 0.0 || sm.rate > 1.0)))
				return fail("`slowmo T0 T1 [RATE]`: from T0 to T1 nature runs at RATE of its speed (0.25 when not given: a high-speed camera's slow motion)");
			s.slowmos.push_back(sm);
		} else if (verb == "timelapse") {
			double v = 0.0;
			if (!number(rest, v) || v < 1.0 || v > 100000.0) return fail("`timelapse FACTOR`: how many times faster the clouds, the water and the wind go (1 to 100000)");
			s.timelapse = v;
		} else if (verb == "exposure") {
			const std::vector<std::string> t = detail::tokens(line);
			double v = 0.0;
			if (t.size() >= 2 && t[1] == "auto" && t.size() <= 3 && (t.size() == 2 || (number(t[2], v) && v > 0.0))) {
				s.exposureAuto = true;
				if (t.size() == 3) s.exposure = v;
			} else if (t.size() == 2 && number(t[1], v) && v > 0.0) {
				s.exposure = v;
			} else {
				return fail("`exposure auto [KEY]` (follows the light: day, sunset, night) or `exposure FACTOR`");
			}
		} else if (verb == "lighting") {   // key, fill and rim lights round the shot's subject (shot_lighting.h)
			if (s.shots.empty()) return fail("`lighting` belongs to a shot: start one with `shot 0 NAME` first");
			Shot &shot = s.shots.back();
			std::string words;
			for (std::size_t i = 1; i < w.size(); ++i) {
				if (w[i] == "on" && i + 1 < w.size()) shot.lightOn = w[++i];
				else words += " " + w[i];
			}
			if (const std::string e = shot_lighting::parse(words, shot.lighting); !e.empty()) return fail(e + " (and `on WHO`: the character it lights)");
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
		} else if (verb == "part") {
			return fail("a film in parts is read a part at a time (film_file::partsOf): this text is not one of its parts");
		} else {
			return fail("unknown word \"" + verb + "\" (film, name, part, about, scene, length, fps, still, tags, set, cast, ease, shutter, idle, shot, key, orbit, crane, push, track, handheld, focus, aperture, dissolve, fade, wipe, fadeout, follow, subject, auto, side, move, turn, grow, light, show, bounce, topple, character, crowd, walk, run, jump, sit, gesture, feel, say, look, touch, hold, hug, rigged, title, episode, credits, credit, theme, prop, pick, put, give, place, scatter, sky, ambience, mute, music, toon, voices, acting, lighting, terrain, water, river, waterfall, clouds, forest, flock, herd, aurora, lightning, meteors, rainbow, fly, timelapse, exposure, wet, snowcover, gusts, mist, fog, air, storm, surf, flowers, biome, slowmo, geyser, rockfall, avalanche, quake, underwater, kelp, coral, fire, lava, volcano, wildfire, weather, particles, burst)");
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
	if (s.fadeOut > s.length) {   // (longer, it darkened the whole film)
		n = 0;
		return fail("`fadeout " + std::to_string(s.fadeOut) + "` is longer than the film (" + std::to_string(s.length) + " s)");
	}
	for (Character &c : s.characters)   // (a crowd's wandering was made before the film's length was known: what starts after the film ends is not walked)
		if (c.extra) c.walks.erase(std::remove_if(c.walks.begin(), c.walks.end(), [&](const WalkLine &w) { return w.t0 > s.length - 1.0; }), c.walks.end());
	for (const water::River &rv : s.rivers)
		if ((rv.downhill || rv.canyon) && !(s.hasTerrain && (rv.downhill || s.terrain.kind == "canyon"))) {
			n = 0;
			return fail(rv.canyon ? "`river canyon` runs along a canyon terrain's riverbed: the film has no `terrain canyon`" : "`river from X Z` finds its way downhill over a terrain: the film has no `terrain`");
		}
	if (!s.biome.empty()) {   // a biome: the land's colours, its plants unless the film plants its own, its sound unless the film says
		if (!s.hasTerrain) {
			n = 0;
			return fail("`biome` is the land's: the film needs a `terrain` line");
		}
		s.terrain.biome = s.biome;
		if (s.forests.empty() && s.biome != "arctic") {
			forest::Forest f;
			f.kind = s.biome;
			const double perSquareMetre = s.biome == "jungle" ? 0.003 : s.biome == "savanna" ? 0.0004 : s.biome == "desert" ? 0.0007 : 0.001;
			f.count = s.biomeTrees >= 0 ? s.biomeTrees : static_cast<int>(std::clamp(perSquareMetre * s.terrain.size * s.terrain.size, 40.0, 8000.0));
			f.steep = s.biome == "jungle" ? 0.3 : 0.2;
			if (f.count > 0) s.forests.push_back(f);
		}
		if (s.ambience.empty()) {
			if (s.biome == "jungle") s.ambience = {{"birds", 0.7}, {"crickets", 0.5}};
			else if (s.biome == "savanna") s.ambience = {{"crickets", 0.4}, {"wind", 0.4}};
			else s.ambience = {{"wind", s.biome == "desert" ? 0.6 : 0.8}};
		}
	}
	if (s.hasTerrain) {   // on a terrain, heights are above the ground: what is placed and who is brought in stand on it
		if (!s.seas.empty()) s.terrain.waterLevel = s.seas[0].level;   // (its shore is drawn for the water)
		if (s.terrain.kind == "coast" && !s.terrain.ySet) s.terrain.y = (s.seas.empty() ? 0.0 : s.seas[0].level) - 0.25 * s.terrain.height;   // (its beach at the water)
		for (const fire::Volcano &v : s.volcanoes) s.terrain.cones.push_back({v.x, v.z, v.height, v.radius});   // (a volcano's cone is part of the land)
		if (!s.rivers.empty()) {   // each river's way over the land, then a bed cut under it (so no rise of the ground hides it)
			const std::shared_ptr<const terrain::Field> uncut = terrain::fieldOf(s.terrain);
			for (water::River &rv : s.rivers) {
				rv.path = water::riverPath(rv, uncut.get());
				s.terrain.channels.push_back({rv.path, rv.width});
			}
		}
		if (!s.falls.empty()) {   // a waterfall's lip is above the ground there: the land is raised behind it into a cliff, and lowered before it for its pool
			const std::shared_ptr<const terrain::Field> before = terrain::fieldOf(s.terrain);
			for (water::Fall &f : s.falls) {
				f.lip.y += before->heightAt(f.lip.x, f.lip.z);
				s.terrain.steps.push_back({f.lip, f.width, f.drop, f.facing});
			}
		}
		const std::shared_ptr<const terrain::Field> ground = terrain::fieldOf(s.terrain);
		for (Shot &shot : s.shots)   // (a camera's heights too: above the ground under it, and what it looks at above the ground under that)
			for (camera_keyframes::Key &k : shot.path.keys) {
				k.pos[1] += ground->heightAt(k.pos[0], k.pos[2]);
				k.target[1] += ground->heightAt(k.target[0], k.target[2]);
			}
		for (Shot &shot : s.shots)
			if (shot.flyLook) shot.flyAt.y += ground->heightAt(shot.flyAt.x, shot.flyAt.z);
		for (dressing::Placement &p : s.placements) p.at.y += ground->heightAt(p.at.x, p.at.z);
		for (Character &c : s.characters) c.start[1] += ground->heightAt(c.start[0], c.start[2]);
	}
	{   // the forests' trees, on the land as it is drawn (out of the water and the rivers' beds)
		const std::shared_ptr<const terrain::Field> ground = s.hasTerrain ? terrain::fieldOf(s.terrain) : nullptr;
		const double level = s.seas.empty() ? -1e30 : s.seas[0].level;
		for (const forest::Forest &f : s.forests) s.planted.push_back(forest::plant(f, ground.get(), level, s.terrain.channels));
		for (const forest::Forest &f : s.forests) {   // (leaves falling while a forest drops them, unless the film says what falls: its change from autumn, or into winter)
			if (f.seasonTo.empty() || f.kind == "conifer" || f.kind == "palm" || std::any_of(s.weather.begin(), s.weather.end(), [](const particles::Weather &w) { return w.kind == "leaves"; }))
				continue;
			std::string from = f.season, to = f.seasonTo;
			double t0 = f.turn0, t1 = f.turn1;
			for (std::size_t c = 0; c <= f.then.size(); ++c) {
				if (c > 0) from = to, to = f.then[c - 1].to, t0 = f.then[c - 1].t0, t1 = f.then[c - 1].t1;
				if (from == "winter" || !(to == "winter" || from == "autumn")) continue;
				particles::Weather w;
				w.kind = "leaves", w.amount = 0.5, w.t0 = t0 + 0.3 * (t1 - t0), w.t1 = t1 + 2.0, w.onLand = true;
				s.weather.push_back(w);
				break;
			}
		}
	}
	{   // the flocks: their heights above the land (on a terrain), and their flights worked out
		const std::shared_ptr<const terrain::Field> ground = s.hasTerrain ? terrain::fieldOf(s.terrain) : nullptr;
		for (wildlife::Flock &f : s.flocks) {
			if (ground && f.kind != "butterflies") {
				f.at.y += ground->heightAt(f.at.x, f.at.z);
				f.from.y += ground->heightAt(f.from.x, f.from.z);
				f.to.y += ground->heightAt(f.to.x, f.to.z);
			}
			wildlife::prepare(f, s.length);
		}
	}
	{   // the drones' flights: keys along each `fly` (the camera_keyframes path is spaced by distance, so the drone goes at an even pace, easing at its ends)
		const std::shared_ptr<const terrain::Field> ground = s.hasTerrain ? terrain::fieldOf(s.terrain) : nullptr;
		for (Shot &shot : s.shots)
			if (!shot.fly.empty()) shot.path = film_camera::flight(shot.fly, shot.flyHeight, shot.flyLook, shot.flyAt, shot.flyFov, ground.get());
	}
	if ((!s.lavas.empty() || !s.volcanoes.empty()) && !s.hasTerrain) {
		n = 0;
		return fail("lava and volcanoes are part of the land: the film needs a `terrain` line");
	}
	if (s.hasTerrain) {   // fire on the land: flames stand on it; a volcano's lava runs down its flanks from its crater's rim; lava finds its way down as water would
		const std::shared_ptr<const terrain::Field> ground = terrain::fieldOf(s.terrain);
		for (fire::Fire &f : s.fires) f.at.y += ground->heightAt(f.at.x, f.at.z);
		for (const fire::Volcano &v : s.volcanoes)
			for (int k = 0; k < 3; ++k) {
				const double a = 2.0 * kPi * (k + 0.2) / 3.0;
				fire::Lava la;
				la.x = v.x + 0.13 * v.radius * std::cos(a), la.z = v.z + 0.13 * v.radius * std::sin(a), la.width = 0.035 * v.radius, la.speed = 0.8, la.t0 = v.t0 - 25.0;   // (already flowing)
				s.lavas.push_back(la);
			}
		for (fire::Lava &l : s.lavas) {
			water::River way;
			way.points = {{l.x, l.z}};
			way.downhill = true;
			way.width = l.width;
			l.path = water::riverPath(way, ground.get());
			for (Vec3 &p : l.path) p.y = ground->heightAt(p.x, p.z);   // (lava lies on the ground: it does not fill the valley as water would)
		}
	}
	for (const fire::Fire &f : s.fires) {   // sparks fly up from a fire
		particles::Emitter e;
		e.kind = "sparks", e.at = f.at + Vec3{0.0, 0.3 * f.size, 0.0}, e.t0 = std::max(0.0, f.t0), e.t1 = std::min(s.length, f.t1), e.rate = 12.0 * f.size, e.speed = 1.5 * std::sqrt(f.size);
		s.emitters.push_back(e);
	}
	if (s.hasTerrain)
		for (const fire::Volcano &v : s.volcanoes) {   // a volcano's lava fountains
			const std::shared_ptr<const terrain::Field> ground = terrain::fieldOf(s.terrain);
			particles::Emitter e;
			e.kind = "bombs", e.at = {v.x, ground->heightAt(v.x, v.z) + 5.0, v.z}, e.t0 = std::max(0.0, v.t0), e.t1 = std::min(s.length, v.t1), e.rate = 40.0, e.speed = 0.14 * v.height;   // (glowing bombs metres across, seen from kilometres)
			s.emitters.push_back(e);
		}
	for (const water::Fall &f : s.falls) {   // a waterfall throws up spray where it lands, the whole film long
		particles::Emitter e;
		e.kind = "splash";
		e.at = water::fallFoot(f);
		e.t0 = 0.0;
		e.t1 = s.length;
		e.rate = std::min(600.0, 30.0 * f.width);
		e.speed = std::min(8.0, 2.0 + 0.12 * f.drop);
		s.emitters.push_back(e);
	}
	if (!s.rainbows.empty() && !s.hasSky) {
		n = 0;
		return fail("a rainbow is opposite the sun: the film needs a `sky` line");
	}
	if (s.hasUnderwater) {   // under the film's water: its surface seen from below
		if (s.seas.empty()) {
			n = 0;
			return fail("`underwater` puts the camera under the film's water: it needs a `water ... level Y` line");
		}
		if (s.hasMist || s.fog.on) {
			n = 0;
			return fail(std::string("`underwater` and `") + (s.hasMist ? "mist" : "fog") + "` both fill the air round the camera: use one");
		}
		s.seas[0].fromBelow = true;
		s.seas[0].foam = false;
	}
	if (s.swell.on) {   // (the surf needs a sea and a shore)
		if (s.seas.empty() || s.seas[0].kind == "lake" || !s.hasTerrain) {
			n = 0;
			return fail("`surf` breaks on a shore: the film needs a `water sea` line and a `terrain` for it to break on");
		}
		if (!s.swell.towardSet) s.swell.toward = s.seas[0].wind;
	}
	if (s.storm.on) {   // a storm brings what it is made of, unless the film says otherwise: a sky to darken, its weather blown by its wind, gusts, wet or snowy ground,
						// lightning in a rainstorm, the wind heard
		Spec::Storm &st = s.storm;
		if (!s.hasSky) s.hasSky = true, s.sky.from = s.sky.to = 15.0;
		const double a = st.wind * kPi / 180.0;
		const std::string kind = st.kind == "sand" ? "dust" : st.kind;
		if (std::none_of(s.weather.begin(), s.weather.end(), [&](const particles::Weather &w) { return w.kind == kind; })) {
			particles::Weather w;
			w.kind = kind, w.amount = std::min(4.0, (st.kind == "rain" ? 2.5 : st.kind == "snow" ? 3.0 : 2.0) * st.strength), w.t0 = st.t0, w.t1 = st.t1, w.onLand = true;
			w.windX = std::sin(a) * st.speed, w.windZ = std::cos(a) * st.speed;
			s.weather.push_back(w);
		}
		if (s.gusts.strength <= 0.0) s.gusts.strength = std::min(5.0, 0.3 + 0.2 * st.speed * st.strength), s.gusts.dx = -std::sin(a), s.gusts.dz = std::cos(a);
		std::vector<Spec::Ramp> &ground = st.kind == "snow" ? s.snowcover : s.wetness;
		if (st.kind != "sand" && ground.empty()) {
			Spec::Ramp r;
			r.amount = std::min(1.0, 0.9 * st.strength), r.t0 = st.t0 + 0.5 * st.build(), r.t1 = st.t0 + 3.0 * st.build();
			ground.push_back(r);
		}
		if (st.kind == "rain" && s.lightnings.empty()) {
			skyfx::Lightning l;
			l.t0 = st.t0 + st.build(), l.t1 = std::min(st.t1, s.length);
			l.count = std::max(2, static_cast<int>(std::lround(st.strength * (l.t1 - l.t0) / 4.0)));
			if (l.t1 > l.t0) s.lightnings.push_back(l);
		}
		if (std::none_of(s.ambience.begin(), s.ambience.end(), [](const dressing::Ambience &am) { return am.kind == "wind"; })) s.ambience.push_back({"wind", 0.5 + 0.5 * st.strength});
	}
	if (s.air.on) {   // (one medium round the camera)
		if (s.hasMist || s.fog.on || s.hasUnderwater) {
			n = 0;
			return fail(std::string("`air` and `") + (s.hasMist ? "mist" : s.fog.on ? "fog" : "underwater") + "` both fill the air round the camera: use one");
		}
		if (s.air.haze < 0.0) s.air.haze = s.hasSky ? film_sky::physicalHaze(s.sky.haze) : 1.0;
		if (s.air.radius <= 0.0) s.air.radius = s.hasTerrain ? 4.5 * s.terrain.size : 8000.0;
	}
	if (s.fog.on) {   // (one medium round the camera: a fog in the valleys or a mist, not both; its top and thickness from the land unless the film says)
		if (s.hasMist) {
			n = 0;
			return fail("`fog` and `mist` both fill the air round the camera: use one");
		}
		if (!s.fog.topSet) s.fog.top = s.hasTerrain ? s.terrain.y + 0.25 * s.terrain.height : 2.0;
		if (s.fog.thickness <= 0.0) s.fog.thickness = s.hasTerrain ? std::max(2.0, 0.06 * s.terrain.height) : 1.0;
	}
	for (skyfx::Lightning &l : s.lightnings) l.strikes = skyfx::strikesOf(l, s.length);
	if (!s.rockfalls.empty() || !s.avalanches.empty()) {   // the rocks' and the avalanches' ways down the land, and a puff of dust where each rock lands
		if (!s.hasTerrain) {
			n = 0;
			return fail(std::string("a ") + (s.rockfalls.empty() ? "n `avalanche`" : " `rockfall`") + " comes down the land: the film needs a `terrain` line");
		}
		const std::shared_ptr<const terrain::Field> ground = terrain::fieldOf(s.terrain);
		for (geology::Rockfall &rf : s.rockfalls) {
			geology::fall(rf, *ground);
			for (const geology::Rock &r : rf.rocks)
				for (std::size_t k = 0; k < r.impacts.size(); ++k) {
					const std::size_t at = std::min(r.at.size() - 1, static_cast<std::size_t>((r.impacts[k] - r.t0) * 48.0));
					particles::Emitter e;
					e.kind = "dust", e.at = r.at[at] - Vec3{0.0, 0.5 * r.size, 0.0}, e.t0 = e.t1 = r.impacts[k], e.count = static_cast<int>(10 + 14 * r.size), e.speed = 0.8 + 0.6 * r.size;
					s.emitters.push_back(e);
				}
		}
		for (geology::Avalanche &av : s.avalanches) av.path = geology::downhill(*ground, av.x, av.z);
	}
	if (!s.slowmos.empty()) {   // nature's clock: its rate eased into and out of each slow motion, summed
		const auto rate = [&](double t) {
			double r = 1.0;
			for (const Spec::SlowMo &sm : s.slowmos) r = std::min(r, 1.0 - (1.0 - sm.rate) * smoothstep(sm.t0, sm.t0 + 0.4, t) * (1.0 - smoothstep(sm.t1 - 0.4, sm.t1, t)));
			return r * s.timelapse;
		};
		s.natureClock.assign(1, 0.0);
		for (double t = Spec::kNatureStep; t <= s.length + 4.0; t += Spec::kNatureStep) s.natureClock.push_back(s.natureClock.back() + 0.5 * (rate(t - Spec::kNatureStep) + rate(t)) * Spec::kNatureStep);
	}
	for (skyfx::Aurora &a : s.auroras)
		if (!a.facingSet && s.hasSky) a.facing = s.sky.south + 180.0;   // (the aurora is towards the pole: the film's own north)
	s.storyEnd = s.length;
	if (s.credits) {   // the credits: cards of three seconds after the story, over black (the film's name; the cast, five to a card; the film's own lines and how it was made)
		if (s.shots.empty()) {
			n = 0;
			return fail("`credits` come after the film's shots: the film needs a `shot` line");
		}
		std::size_t cast = 0;
		for (const Character &c : s.characters) cast += c.extra ? 0 : 1;
		s.length += 3.0 * static_cast<double>(2 + (cast + 4) / 5);
		Shot credits;
		credits.start = s.storyEnd;
		credits.name = "Credits";
		credits.path = camera_keyframes::parse("key 0 1 5  0 1 0\nkey 0 1 5  0 1 0\n").path;   // (its frames are black: DataFilm::inCredits)
		credits.transition.kind = Transition::Fade;
		credits.transition.length = 1.0;
		s.shots.push_back(credits);
	}
	if (s.ambience.empty() && !s.hasUnderwater && (s.hasTerrain || !s.forests.empty())) {   // a landscape is heard as what it is, unless an `ambience` line says how: wind over the
		double elevation = 0.0, facing = 0.0;                                                // land, and birds in a wood by day, crickets in it and the night's sounds after dark
		dressing::sunAt(s.sky, 0.5 * (s.sky.from + s.sky.to), elevation, facing);
		const bool night = s.hasSky && elevation < -4.0;
		s.ambience.push_back({"wind", 0.5});
		if (!s.forests.empty()) s.ambience.push_back({night ? "crickets" : "birds", night ? 0.5 : 0.25});   // (birds softly: the trees near the camera sing too)
		else if (night) s.ambience.push_back({"night", 0.5});
	}
	const auto hears = [&](const std::string &kind) { return std::any_of(s.ambience.begin(), s.ambience.end(), [&](const dressing::Ambience &a) { return a.kind == kind; }); };
	// (water is heard, unless the film file says how: a sea's wash - only its wash where `surf` breaks, the soundscape's crashes being the waves; a river's rush. A waterfall
	// is heard from its pool, placed: soundscape())
	if (!s.seas.empty() && s.seas[0].kind == "sea" && !hears("sea")) s.ambience.push_back({s.swell.on ? "seawash" : "sea", 0.8});
	if (!s.rivers.empty() && !hears("water")) s.ambience.push_back({"water", 0.7});
	if (std::any_of(s.flocks.begin(), s.flocks.end(), [](const wildlife::Flock &f) { return f.kind == "geese"; }) && !hears("geese")) s.ambience.push_back({"geese", 0.8});
	if (s.muteBirds) {   // (the night's owl too: crickets alone)
		for (dressing::Ambience &a : s.ambience)
			if (a.kind == "night") a.kind = "crickets";
		s.ambience.erase(std::remove_if(s.ambience.begin(), s.ambience.end(), [](const dressing::Ambience &a) { return a.kind == "birds" || a.kind == "geese"; }), s.ambience.end());
	}
	r.ok = true;
	return r;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// A film in parts
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// A film file may be cut into parts - `part NAME [dissolve|fade|wipe SECONDS]` - each a film of its own, with its set (`scene`), its length, its cast, its shots and its sound,
// played one after another as one film (film_parts.h). The lines before the first part are every part's (a cast, a set, an fps they share; a part's own `scene` or `length`
// is its own), except the film's own `film`, `name`, `about`, `tags`, `still` and `music`, which are the whole film's (`still` a time in the whole film).
struct PartText {
	std::string name;
	int line = 0;
	Transition in;      // how it comes in (length 0: a cut)
	std::string text;   // the file with the shared lines and this part's, the others left blank (so a problem names the file's own line)
};
struct FilmParts {
	std::vector<PartText> parts;   // none: the film is not in parts
	std::string key, name, about, tags, error;
	double still = -1.0;
	bool draftMusic = true;
};
inline FilmParts partsOf(const std::string &text, const std::string &where) {
	FilmParts out;
	std::vector<std::string> lines;
	{
		std::istringstream in(text);
		for (std::string line; std::getline(in, line);) lines.push_back(line);
	}
	std::vector<std::string> clean(lines.size());   // each line's words, its comment left out
	std::vector<std::size_t> starts;
	for (std::size_t i = 0; i < lines.size(); ++i) {
		clean[i] = detail::trim(detail::withoutComment(lines[i]));
		if (!clean[i].empty() && detail::words(clean[i])[0] == "part") starts.push_back(i);
	}
	if (starts.empty()) return out;
	const auto fail = [&](std::size_t i, const std::string &what) {
		out.error = where + ":" + std::to_string(i + 1) + ": " + what;
		out.parts.clear();
		return out;
	};
	// the film's own lines, before the first part
	std::vector<bool> shared(lines.size(), false), opening(lines.size(), false), closing(lines.size(), false);
	for (std::size_t i = 0; i < starts[0]; ++i) {
		const std::string &w = clean[i];
		if (w.empty()) continue;
		const std::string verb = detail::words(w)[0], rest = detail::trim(w.substr(verb.size()));
		if (verb == "film") {
			if (!detail::validKey(rest)) return fail(i, "the film's key must be lower-case letters, digits and '-' (got \"" + rest + "\")");
			out.key = rest;
		} else if (verb == "name") {
			out.name = rest;
		} else if (verb == "about") {
			out.about = rest;
		} else if (verb == "tags") {
			out.tags = rest;
		} else if (verb == "still") {
			if (!detail::number(rest, out.still) || out.still < 0.0) return fail(i, "still is a time in seconds (in the whole film)");
		} else if (verb == "music") {
			if (rest != "none" && rest != "draft") return fail(i, "`music none` (no music) or `music draft` (the drafted score when there is no cues file)");
			out.draftMusic = rest == "draft";
		} else if (verb == "shot" || verb == "key") {
			return fail(i, "a shot belongs to a part: the lines before the first `part` are what every part shares");
		} else if (verb == "title" || verb == "episode" || verb == "theme") {
			opening[i] = true;   // (the first part's: the title is over the film's opening)
		} else if (verb == "credits" || verb == "credit") {
			closing[i] = true;   // (the last part's: the credits come after the film's end)
		} else {
			shared[i] = true;
		}
	}
	for (std::size_t p = 0; p < starts.size(); ++p) {
		const std::size_t at = starts[p], end = p + 1 < starts.size() ? starts[p + 1] : lines.size();
		const std::vector<std::string> t = detail::tokens(clean[at]);
		PartText part;
		part.line = static_cast<int>(at + 1);
		if (t.size() < 2) return fail(at, "`part NAME [dissolve|fade|wipe SECONDS]`: a part has a name");
		part.name = t[1];
		if (t.size() > 2) {
			double length = 0.0;
			if (t.size() != 4 || (t[2] != "dissolve" && t[2] != "fade" && t[2] != "wipe") || !detail::number(t[3], length) || !(length > 0.0 && length <= 10.0))
				return fail(at, "a part comes in with `dissolve`, `fade` or `wipe` and how many seconds it takes (up to 10)");
			part.in.kind = t[2] == "dissolve" ? Transition::Dissolve : t[2] == "fade" ? Transition::Fade : Transition::Wipe;
			part.in.length = length;
		}
		for (const PartText &other : out.parts)
			if (other.name == part.name) return fail(at, "there is already a part called \"" + part.name + "\"");
		const bool first = p == 0, last = p + 1 == starts.size();
		for (std::size_t i = 0; i < lines.size(); ++i)
			part.text += (shared[i] || (opening[i] && first) || (closing[i] && last) || (i > at && i < end) ? lines[i] : std::string()) + "\n";
		out.parts.push_back(part);
	}
	return out;
}

}  // namespace film_file
}  // namespace cartoon
