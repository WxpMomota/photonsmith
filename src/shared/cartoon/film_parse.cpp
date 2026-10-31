// film_parse.cpp -- the film-file parser: a film file's text read into its Spec (film_parse.h has the language, film_spec.h what it says). Compiled once, in cpu_renderer.

#include "film_parse.h"

namespace cartoon {
namespace film_file {

// One `wind` over the set (film_wind.h): whatever moves with the air and did not say otherwise in its own line takes it.
static void applyWind(Spec &s) {
	if (!s.wind.on) return;
	const wind::Wind &w = s.wind;
	const Vec3 d = w.dir();
	for (clouds::Clouds &c : s.cloudLayers) {
		if (!c.windSet) c.wind = w.toward;
		if (!c.speedSet) c.speed = 1.5 * w.speed;   // (faster aloft)
	}
	for (clouds::FogBank &f : s.fogBanks) {
		if (!f.windSet) f.wind = w.toward;
		if (!f.speedSet) f.speed = 0.3 * w.speed;   // (fog lies low, in the stiller air)
	}
	for (water::Sea &sea : s.seas)
		if (!sea.windSet) sea.wind = w.toward;
	for (fire::Volcano &v : s.volcanoes)
		if (!v.windSet) v.wind = w.toward;
	for (fire::Wildfire &f : s.wildfires)
		if (!f.windSet) f.wind = w.toward;
	if (!s.storm.windSet) s.storm.wind = w.toward;
	if (!s.storm.speedSet && w.speed > 0.0) s.storm.speed = w.speed;
	for (particles::Weather &x : s.weather)
		if (x.windX == 0.0 && x.windZ == 0.0) x.windX = d.x * w.speed, x.windZ = d.z * w.speed;
	if (!s.gusts.windSet) s.gusts.dx = -d.x, s.gusts.dz = d.z;   // (story space)
	if (s.gusts.strength <= 0.0) s.gusts.strength = std::min(5.0, w.speed / 8.0), s.gusts.speed = std::max(1.0, w.speed);
	s.gusts.steady = std::min(1.5, w.speed / 12.0);
}

// A volcano's rim; its pyroclastic flows' ways down from it (as lava finds its way); its column's lightning and its ash (film_fire.h), worked out on the land.
static void eruptionsOnLand(Spec &s, const terrain::Field &groundField) {
	const terrain::Field *ground = &groundField;
	for (std::size_t i = 0; i < s.volcanoes.size(); ++i) {
		fire::Volcano &v = s.volcanoes[i];
		v.rimY = -1e30;
		for (int k = 0; k < 24; ++k) v.rimY = std::max(v.rimY, ground->heightAt(v.x + 0.11 * v.radius * std::cos(kPi * k / 12.0), v.z + 0.11 * v.radius * std::sin(kPi * k / 12.0)));
		for (std::size_t k = 0; k < v.flows.size(); ++k) {
			fire::PyroclasticFlow &f = v.flows[k];
			const double a = (f.towardSet ? f.toward : 360.0 * hash01(static_cast<int>(i * 31 + k), 907)) * kPi / 180.0;
			water::River way;
			way.points = {{v.x + 0.15 * v.radius * std::sin(a), v.z + 0.15 * v.radius * std::cos(a)}};
			way.downhill = true;
			way.width = 0.05 * v.radius;
			f.path = water::riverPath(way, ground);
			for (Vec3 &p : f.path) p.y = ground->heightAt(p.x, p.z);
		}
		if (v.lightning > 0) {   // in the column, over the crater, from part way up it down to the crater and the flanks
			skyfx::Lightning l;
			l.count = v.lightning, l.x = v.x, l.z = v.z, l.radius = 0.12 * v.radius, l.seed = 41 + static_cast<int>(i);
			l.t0 = v.t0 + 0.5 * fire::columnRise(v.column), l.t1 = std::min(v.t1, s.length);
			l.base = v.rimY + 0.35 * v.column;
			if (l.t1 > l.t0) s.lightnings.push_back(l);
		}
		if (v.ashfall > 0.0) {   // ash drifting down round the camera, once the umbrella has spread over
			particles::Weather w;
			w.kind = "ash", w.amount = v.ashfall, w.t0 = v.t0 + (v.column > 0.0 ? fire::columnRise(v.column) : 10.0), w.t1 = v.t1 + 30.0, w.onLand = true;
			s.weather.push_back(w);
		}
	}
}

Parsed parse(const std::string &text, const std::string &where, const std::vector<Spec::CreditName> *castBefore) {
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
		r.error = detail::lineWhere(s, where, n) + ": " + what;
		return r;
	};
	std::string shotText;   // the key lines of the shot being read
	std::string shotEase;   // its `ease camera` (as the camera path's `ease` line)
	auto closeShot = [&]() -> std::string {
		if (s.shots.empty()) return std::string();
		const std::string ease = shotEase;
		shotEase.clear();
		if (!ease.empty() && (s.shots.back().move.kind != Shot::Move::None || !s.shots.back().fly.empty() || s.shots.back().following || s.shots.back().coverage))
			return "the shot \"" + s.shots.back().name + "\" has `ease camera`, which is for a shot of `key` lines";
		if (s.shots.back().move.kind != Shot::Move::None) {
			if (!shotText.empty() || s.shots.back().following || s.shots.back().coverage) return "the shot \"" + s.shots.back().name + "\" has a move and `key`, `follow` or `auto` lines too";
			return std::string();
		}
		if (!s.shots.back().fly.empty()) {
			if (!shotText.empty() || s.shots.back().following || s.shots.back().coverage) return "the shot \"" + s.shots.back().name + "\" has a `fly` line and `key`, `follow` or `auto` lines too";
			return std::string();
		}
		if (s.shots.back().spaceCam.kind != cosmos::SpaceCam::None) {   // (a journey in space: film_voyage.h)
			if (!shotText.empty() || s.shots.back().following || s.shots.back().coverage) return "the shot \"" + s.shots.back().name + "\" has a `spacecam` line and `key`, `follow` or `auto` lines too";
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
		const camera_keyframes::ParseResult p = camera_keyframes::parse(ease + shotText);
		if (!p.ok) return "the shot \"" + s.shots.back().name + "\"'s `key` lines: " + p.error;
		s.shots.back().path = p.path;
		shotText.clear();
		return std::string();
	};
	int shotLine = 0;
	for (;;) {
		if (!std::getline(*sources.back().in, line)) {   // (the end of a library file: back to the file that brought it in)
			if (sources.size() == 1) break;
			sources.pop_back();
			n = detail::lineId(s, here = sources.back().where, where, sources.back().n);
			continue;
		}
		n = detail::lineId(s, here = sources.back().where, where, ++sources.back().n);   // (a library file's lines numbered with their file: detail::lineWhere)
		line = detail::trim(detail::withoutComment(line));
		if (line.empty()) continue;
		const std::vector<std::string> w = detail::words(line);
		const std::string &verb = w[0];
		const std::string rest = detail::trim(line.substr(verb.size()));
		if (sources.back().library && (verb == "film" || verb == "name" || verb == "about" || verb == "tags" || verb == "still" || verb == "length" || verb == "fps" || verb == "music" ||
									   verb == "shot" || verb == "key" || verb == "part" || verb == "title" || verb == "episode" || verb == "credits" || verb == "credit" ||
									   verb == "theme"))
			return fail("`" + verb + "` is the film's, not a set's: a set is a scene and what dresses it, its sound and who is on it");
		// the nature lines (terrain, water, the sky, weather, fire ...): film_parse_nature.h
		bool nature = false;
		if (const std::string e = natureLine(s, verb, line, rest, nature); !e.empty()) return fail(e);
		if (nature) continue;
		// the cartoon lines (`mark`, `cloth`): film_parse_cartoon.h
		bool cartoonish = false;
		if (const std::string e = cartoonLine(s, verb, line, n, cartoonish); !e.empty()) return fail(e);
		if (cartoonish) continue;
		if (verb == "key") {
			if (s.shots.empty()) return fail("a `key` line belongs to a shot: start one with `shot 0 NAME` first");
			shotText += "key " + rest + "\n";   // (written `key ` whatever space followed it: one key line is counted by that, closeShot)
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
			sources.push_back({std::make_shared<std::istringstream>(character + "\n"), here, sources.back().n - 1, sources.back().dir, sources.back().library});   // (read next, as this line)
			if (!model.empty()) {   // (and before it, its kind from its model: `rigged KIND FILE ...` with the file from the cast's folder)
				const std::vector<std::string> mw = detail::tokens(model);
				// (the model's file as the cast names it: the line is read from the cast's folder, its `rigged` record's dir - joined to that folder here as well, a film given by a
				// relative path looked for films/library/films/library/robot.glb)
				std::string rigged = "rigged " + kind + " \"" + mw[0] + "\"";
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
		} else if (verb == "ease") {   // the film's motions wherever the line is; `ease camera` a shot's camera path
			const bool camera = w.size() == 3 && w[1] == "camera";
			const std::string &v = w.back();
			if (w.size() != (camera ? 3u : 2u) || (v != "0" && v != "1" && !ease_curves::known(v)))
				return fail("`ease 1`, `ease 0` or `ease CURVE` (the film's motions), or in a shot `ease camera 1|0|CURVE` (its camera); the curves: smooth, linear, in, out, back, "
							"elastic, bounce");
			if (!camera) s.ease = v != "0", s.easeCurve = ease_curves::known(v) ? v : std::string("smooth");
			else if (s.shots.empty()) return fail("`ease camera` belongs to a shot: start one with `shot 0 NAME` first");
			else shotEase = "ease " + w.back() + "\n";
		} else if (verb == "shutter") {
			if (w.size() != 2 || !number(w[1], s.shutter) || s.shutter < 0.0 || s.shutter > 1.0) return fail("shutter is a share of a frame, from 0 (sharp) to 1");
		} else if (verb == "shot") {
			if (const std::string e = closeShot(); !e.empty()) return n = shotLine, fail(e);   // (a shot's problem at the shot's own line, not the next one's)
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
			std::vector<std::string> t = detail::tokens(line);   // (a walk's gait word is taken out of it)
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
				if (t.size() > 4 && motion::gaitNamed(t[4], wl.gait)) {   // walk NAME FROM SPEED GAIT ...: its personality (film_motion.h), the rest as without it
					t.erase(t.begin() + 4);
				}
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
					else p.seed = detail::seedOf(v);
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
						else sc.seed = detail::seedOf(v);
						i += 2;
					} else {
						return fail("\"" + t[i] + "\": a scatter takes y, seed and scale A B");
					}
				}
				const std::vector<dressing::Placement> many = dressing::placementsOf(sc);
				s.placements.insert(s.placements.end(), many.begin(), many.end());
			}
		} else if (verb == "rigged") {   // a kind of character from a rigged glTF model (film_rigged.h)
			const std::vector<std::string> t = detail::tokens(line);
			Spec::Rigged rg;
			rg.line = n;
			rg.dir = sources.back().dir;
			if (t.size() < 3 || !detail::validKey(t[1])) return fail("`rigged KIND FILE.glb [height H] [stride S] [turn DEGREES] [walk CLIP] [idle CLIP] [run CLIP] [skinning dq|linear] [autorig]` (KIND: lower-case letters, digits and '-')");
			rg.kind = t[1];
			rg.file = t[2];
			for (std::size_t i = 3; i < t.size(); i += 2) {
				if (t[i] == "autorig") {   // (a skeleton fitted to it: auto_rig.h)
					rg.options.autoRig = true;
					--i;
					continue;
				}
				if (i + 1 >= t.size()) return fail("\"" + t[i] + "\" needs a value");
				double v = 0.0;
				if (t[i] == "height" || t[i] == "stride" || t[i] == "turn") {
					if (!number(t[i + 1], v) || (t[i] != "turn" && !(v > 0.0))) return fail("a rigged model's " + t[i] + " is a number" + (t[i] == "turn" ? "" : " above 0"));
					(t[i] == "height" ? rg.options.height : t[i] == "stride" ? rg.options.stride : rg.options.turn) = v;
				} else if (t[i] == "walk" || t[i] == "idle" || t[i] == "run") {
					(t[i] == "walk" ? rg.options.walk : t[i] == "idle" ? rg.options.idle : rg.options.run) = detail::lowerWord(t[i + 1]);
				} else if (t[i] == "skinning" && (t[i + 1] == "dq" || t[i + 1] == "linear")) {   // (dual quaternions, the default, or linear blending)
					rg.options.linearSkin = t[i + 1] == "linear", rg.options.skinningGiven = true;
				} else {
					return fail("\"" + t[i] + "\": a rigged model takes height, stride, turn, walk, idle, run, skinning dq|linear and autorig");
				}
			}
			s.rigged.push_back(rg);
		} else if (verb == "mocap") {   // a motion capture a rigged character plays (mocap_bvh.h)
			const std::vector<std::string> t = detail::tokens(line);
			const char *usage = "`mocap NAME FILE.bvh at T [to T1] [speed S] [turn DEGREES] [travel]`";
			Spec::Mocap mc;
			mc.line = n;
			mc.dir = sources.back().dir;
			if (t.size() < 5 || t[3] != "at" || !number(t[4], mc.at)) return fail(usage);
			mc.who = t[1], mc.file = t[2];
			if (std::none_of(s.characters.begin(), s.characters.end(), [&](const Character &c) { return c.name == mc.who; }))
				return fail("`mocap NAME ...` needs a character brought in before it with `character`");
			for (std::size_t i = 5; i < t.size(); ++i) {
				if (t[i] == "travel") {
					mc.travel = true;
					continue;
				}
				double v = 0.0;
				if (i + 1 >= t.size() || !number(t[i + 1], v)) return fail(usage);
				if (t[i] == "to" && v > mc.at) mc.to = v;
				else if (t[i] == "speed" && v > 0.0 && v <= 10.0) mc.speed = v;
				else if (t[i] == "turn") mc.turn = v;
				else return fail(usage);
				++i;
			}
			s.mocaps.push_back(mc);
		} else if (verb == "act") {   // a face's acting from a video (face_track.h; tools/video_to_acting)
			const std::vector<std::string> t = detail::tokens(line);
			const char *usage = "`act NAME FILE.face at T [to T1] [amount A]`";
			Spec::FaceAct a;
			a.line = n;
			a.dir = sources.back().dir;
			if (t.size() < 5 || t[3] != "at" || !number(t[4], a.at)) return fail(usage);
			a.who = t[1], a.file = t[2];
			if (std::none_of(s.characters.begin(), s.characters.end(), [&](const Character &c) { return c.name == a.who; }))
				return fail("`act NAME ...` needs a character brought in before it with `character`");
			for (std::size_t i = 5; i + 1 < t.size() + 1; i += 2) {
				double v = 0.0;
				if (i + 1 >= t.size() || !number(t[i + 1], v)) return fail(usage);
				if (t[i] == "to" && v > a.at) a.to = v;
				else if (t[i] == "amount" && v >= 0.0 && v <= 2.0) a.amount = v;
				else return fail(usage);
			}
			s.faceActs.push_back(a);
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
				} else if ((t[i] == "height" || t[i] == "fov") && i + 1 < t.size() && number(t[i + 1], a) && a > 0.0 && (t[i] == "height" || a < 179.0)) {
					(t[i] == "height" ? shot.flyHeight : shot.flyFov) = a;
					i += 2;
				} else {
					return fail("\"" + t[i] + "\": `fly X Z X Z [X Z ...] [height H] [look X Y Z] [fov F]` (waypoints, pbrt's x z; heights above the land)");
				}
			}
			if (shot.fly.size() < 2) return fail("`fly X Z X Z [X Z ...] [height H] [look X Y Z] [fov F]`: at least two waypoints");
		} else if (verb == "spacecam") {   // a journey in space (film_voyage.h)
			if (s.shots.empty()) return fail("`spacecam` belongs to a shot: start one with `shot 0 NAME` first");
			if (const std::string e = cosmos::spaceCamLine(detail::tokens(line), s.shots.back().spaceCam); !e.empty()) return fail(e);
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
			if (verb == "aperture" && t.size() > 2) {   // aperture RADIUS [blades N] [rotation DEG] [ratio R]: a lens of N blades draws its bokeh as their polygon (aperture_shape.h)
				const char *usage = "`aperture RADIUS [blades N] [rotation DEG] [ratio R]` (RADIUS 0 to 0.5 m; N 3 to 16 blades; R the anamorphic squeeze, 0.25 to 4)";
				if (!number(t[1], shot.aperture) || shot.aperture < 0.0 || shot.aperture > 0.5 || t.size() % 2 != 0) return fail(usage);
				for (std::size_t i = 2; i + 1 < t.size(); i += 2) {
					double v = 0.0;
					if (!number(t[i + 1], v)) return fail(usage);
					if (t[i] == "blades" && v >= 3.0 && v <= 16.0 && v == std::floor(v)) shot.blades = static_cast<int>(v);
					else if (t[i] == "rotation") shot.bladesRotation = v;
					else if (t[i] == "ratio" && v >= 0.25 && v <= 4.0) shot.apertureRatio = v;
					else return fail(usage);
				}
				continue;
			}
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
				if (k == "fov" && v > 0.5 && v < 179.0) mv.fov = v;   // (a field of view in degrees: 0 or 500 made a broken camera)
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
				else if (t[i] == "fov" && v > 0.5 && v < 179.0) shot.follow.fov = v;   // (degrees, more than 0 and less than 180)
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
			while (i < w.size() && (w[i] == "about" || w[i] == "axis" || w[i] == "ease")) {
				if (w[i] == "ease") {   // its own curve (ease_curves.h)
					if (i + 1 >= w.size() || !ease_curves::known(w[i + 1])) return fail("`ease` on a line takes a curve: smooth, linear, in, out, back, elastic, bounce");
					a.ease = w[i + 1];
					i += 2;
					continue;
				}
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
			return fail("unknown word \"" + verb + "\" (film, name, part, about, scene, length, fps, still, tags, set, cast, ease, shutter, idle, shot, key, orbit, crane, push, track, handheld, focus, aperture, spacecam, dissolve, fade, wipe, fadeout, follow, subject, auto, side, move, turn, grow, light, show, bounce, topple, character, crowd, walk, run, jump, sit, gesture, feel, say, look, touch, hold, hug, rigged, title, episode, credits, credit, theme, prop, pick, put, give, place, scatter, sky, ambience, mute, music, toon, voices, acting, lighting, terrain, water, river, waterfall, clouds, fogbank, forest, flock, herd, whale, dolphins, bioluminescence, fireflies, fungi, glacier, iceberg, seaice, wind, tornado, optics, caustics, closeup, tracks, space, planet, moon, nebula, galaxy, star, blackhole, comet, asteroids, remnant, ship, astronaut, descent, aurora, lightning, meteors, rainbow, fly, timelapse, exposure, wet, snowcover, gusts, mist, fog, air, storm, surf, flowers, biome, slowmo, geyser, rockfall, avalanche, quake, underwater, waterline, kelp, coral, current, bubbles, halocline, deepsea, octopus, cuttlefish, swimmers, seabed, wreck, arch, cenote, reefwall, diver, divecam, school, bigfish, reef, critters, nightdive, kelpforest, boat, sardinerun, vents, murk, silt, cleaningstation, playmate, damselfish, wreckroom, model, shatter, flex, morph, build, vanish, wobble, spin, gear, piston, roll, route, aim, attach, rigidbody, seacave, ruins, clownfish, pufferfish, flounder, moray, cleanershrimp, dugong, icecap, chest, clam, bottle, shells, sink, moonlight, hunt, flight, baitball, reeffish, jellyfish, seaturtles, insects, dew, night, cameratrap, shimmer, burn, laser, explode, shield, gravity, footprints, blob, drift, interior, hologram, screen, label, orbitline, scalebar, compare, skin-light, eyes, hair, footing, act, fire, lava, volcano, wildfire, weather, particles, burst, mark, cloth, animate, squash, jiggle, smear, anticipate, rigid, terminator, face-normals, mocap, matte)");
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
		eruptionsOnLand(s, *ground);   // a volcano's rim, pyroclastic flows, lightning and ash
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
		if (f.frozen) continue;
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
	if (const std::string why = underseaProblems(s); !why.empty()) return n = 0, fail(why);   // (film_parse_sea.h)
	if (s.hasUnderwater) s.seas[0].fromBelow = true, s.seas[0].foam = false;   // (under the film's water: its surface seen from below)
	if ((!s.whales.empty() || !s.pods.empty()) && (s.seas.empty() || s.seas[0].kind == "lake")) {
		n = 0;
		return fail("whales and dolphins swim in the sea: the film needs a `water sea` line");
	}
	if (!s.glaciers.empty() && !s.hasTerrain) {
		n = 0;
		return fail("a glacier lies on the land: the film needs a `terrain` line");
	}
	if ((!s.icebergs.empty() || s.seaIce.on) && s.seas.empty()) {
		n = 0;
		return fail(std::string(s.seaIce.on ? "sea ice" : "an iceberg") + " floats on the water: the film needs a `water` line");
	}
	applyWind(s);   // one wind over the set (film_wind.h)
	if (s.space.on && (s.hasSky || s.storm.on || s.air.on || s.fog.on)) {
		n = 0;
		return fail(std::string("`space` has no air: no `") + (s.hasSky ? "sky" : s.storm.on ? "storm" : s.air.on ? "air" : "fog") + "` with it");
	}
	if (s.closeup.on && !s.hasTerrain) {
		n = 0;
		return fail("`closeup` is the land close up: the film needs a `terrain` line");
	}
	if (s.bio.on) {   // (the sea's foam glows)
		if (s.seas.empty()) {
			n = 0;
			return fail("`bioluminescence` is the sea glowing where it is stirred: the film needs a `water` line");
		}
		for (water::Sea &sea : s.seas) sea.glow = glow::colourOf(s.bio.colour) * (0.6 * s.bio.strength);   // (as it looks on the screen: the frame divides it by the exposure)
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
	if (castBefore) s.creditCast = *castBefore;
	for (const Character &c : s.characters) {   // (a character of an earlier part again: named once)
		if (c.extra || std::any_of(s.creditCast.begin(), s.creditCast.end(), [&](const Spec::CreditName &k) { return k.name == c.name; })) continue;
		const std::string voice = c.voice.empty() ? s.voices : c.voice;
		s.creditCast.push_back({c.name, c.kind, !voice.empty() && voice != "babble"});
	}
	if (s.credits) {   // the credits: cards of three seconds after the story, over black (the film's name; the cast, five to a card; the film's own lines and how it was made)
		if (s.shots.empty()) {
			n = 0;
			return fail("`credits` come after the film's shots: the film needs a `shot` line");
		}
		s.length += 3.0 * static_cast<double>(2 + (s.creditCast.size() + 4) / 5);
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
	if (!s.seas.empty() && s.hasUnderwater && !hears("reef")) s.ambience.push_back({"reef", 0.8});   // (under the water its hush and the reef's crackle as well: seaSounds plays one or the other by the camera's side of the surface)
	if (!s.seas.empty() && !hears("sea") && !hears("seawash") && s.seas[0].kind == "sea") s.ambience.push_back({s.swell.on ? "seawash" : "sea", 0.8});
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

FilmParts partsOf(const std::string &text, const std::string &where) {
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
		for (std::size_t i = at + 1; i < end; ++i) {   // (the whole film's lines in a part were read and left unused without a word)
			if (clean[i].empty()) continue;
			const std::string verb = detail::words(clean[i])[0];
			if (verb == "film" || verb == "name" || verb == "about" || verb == "tags" || verb == "still" || verb == "music")
				return fail(i, "`" + verb + "` is the whole film's: put it before the first `part`");
			if ((verb == "credits" || verb == "credit") && !last) return fail(i, "the credits come after the film's end: put `" + verb + "` in the last part, or before the first `part`");
		}
		for (std::size_t i = 0; i < lines.size(); ++i)
			part.text += (shared[i] || (opening[i] && first) || (closing[i] && last) || (i > at && i < end) ? lines[i] : std::string()) + "\n";
		out.parts.push_back(part);
	}
	return out;
}

}  // namespace film_file
}  // namespace cartoon

// ---- 3D lettering for models (model_text.h): the font reader built in here, its functions private to this file ----
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "../../external/stb_truetype.h"
#include "../scene_text.h"
#include "model_text.h"

namespace cartoon {
namespace model {

Mesh textSolid(const std::string &words, double height, double depth, const std::string &font, std::string &error) {
	namespace fs = std::filesystem;
	const std::string file = font.empty() ? std::string("arial.ttf") : font;
	std::vector<fs::path> where{file};
	if (const std::string own = film_cache::asset("fonts/" + file); !own.empty()) where.insert(where.begin(), own);
#if defined(_WIN32)
	where.push_back(fs::path(pbrt_asset_check::environmentValue("WINDIR").empty() ? "C:/Windows" : pbrt_asset_check::environmentValue("WINDIR")) / "Fonts" / file);
#elif defined(__APPLE__)
	for (const char *d : {"/System/Library/Fonts/Supplemental", "/System/Library/Fonts", "/Library/Fonts"}) where.push_back(fs::path(d) / file);
#else
	for (const char *d : {"/usr/share/fonts/truetype/msttcorefonts", "/usr/share/fonts/truetype/dejavu", "/usr/share/fonts/truetype"}) where.push_back(fs::path(d) / file);
#endif
	std::vector<unsigned char> bytes;
	for (const fs::path &p : where) {
		std::ifstream in(p, std::ios::binary);
		if (in) {
			bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
			break;
		}
	}
	stbtt_fontinfo info;
	if (bytes.empty() || !stbtt_InitFont(&info, bytes.data(), stbtt_GetFontOffsetForIndex(bytes.data(), 0))) {
		error = "no font \"" + file + "\" (under film_assets/fonts, the system's fonts, or a path)";
		return Mesh();
	}
	// the outlines: each glyph's contours, its curves cut into short straight pieces, laid along a line by the font's advances and kerning
	scene_doc::TextOutline outline;
	double x = 0.0;
	int previous = 0;
	for (std::size_t i = 0; i < words.size();) {
		int cp = static_cast<unsigned char>(words[i]);
		const int extra = cp >= 0xF0 ? 3 : cp >= 0xE0 ? 2 : cp >= 0xC0 ? 1 : 0;
		cp &= extra == 3 ? 0x07 : extra == 2 ? 0x0F : extra == 1 ? 0x1F : 0x7F;
		for (int k = 1; k <= extra && i + k < words.size(); ++k) cp = (cp << 6) | (static_cast<unsigned char>(words[i + k]) & 0x3F);
		i += 1 + extra;
		if (previous) x += stbtt_GetCodepointKernAdvance(&info, previous, cp);
		stbtt_vertex *v = nullptr;
		const int n = stbtt_GetCodepointShape(&info, cp, &v);
		scene_doc::TextContour c;
		double px = 0.0, py = 0.0;
		for (int k = 0; k < n; ++k) {
			const double vx = v[k].x + x, vy = v[k].y;
			if (v[k].type == STBTT_vmove) {
				if (c.size() > 2) outline.contours.push_back(c);
				c.clear();
				c.push_back({vx, vy});
			} else if (v[k].type == STBTT_vline) {
				c.push_back({vx, vy});
			} else {   // (a quadratic or a cubic curve, in eight pieces)
				const double cx = v[k].cx + x, cy = v[k].cy, cx1 = v[k].cx1 + x, cy1 = v[k].cy1;
				for (int s = 1; s <= 8; ++s) {
					const double u = s / 8.0, w = 1.0 - u;
					if (v[k].type == STBTT_vcurve) c.push_back({w * w * px + 2 * w * u * cx + u * u * vx, w * w * py + 2 * w * u * cy + u * u * vy});
					else c.push_back({w * w * w * px + 3 * w * w * u * cx + 3 * w * u * u * cx1 + u * u * u * vx, w * w * w * py + 3 * w * w * u * cy + 3 * w * u * u * cy1 + u * u * u * vy});
				}
			}
			px = vx, py = vy;
		}
		if (c.size() > 2) outline.contours.push_back(c);
		if (v) stbtt_FreeShape(&info, v);
		int advance = 0, bearing = 0;
		stbtt_GetCodepointHMetrics(&info, cp, &advance, &bearing);
		x += advance;
		previous = cp;
	}
	int capHeight = 0;
	{   // (a capital's height: H's box)
		int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
		if (stbtt_GetCodepointBox(&info, 'H', &x0, &y0, &x1, &y1)) capHeight = y1 - y0;
	}
	outline.capHeight = capHeight;
	const scene_doc::ShapeMesh s = scene_doc::extrudeOutlines(outline, height, 1.0, depth);
	Mesh m;
	for (std::size_t k = 0; k + 2 < s.P.size(); k += 3) m.verts.push_back({s.P[k], s.P[k + 1], s.P[k + 2]});
	for (std::size_t k = 0; k + 2 < s.N.size(); k += 3) m.normals.push_back({s.N[k], s.N[k + 1], s.N[k + 2]});
	for (std::size_t k = 0; k + 2 < s.indices.size(); k += 3) m.tris.push_back({s.indices[k], s.indices[k + 1], s.indices[k + 2]});
	if (m.tris.empty()) {
		error = "no letters to make of \"" + words + "\"";
		return Mesh();
	}
	Vec3 lo, hi;
	boxOf(m, lo, hi);
	return transformed(m, translate({-0.5 * (lo.x + hi.x), -lo.y, -0.5 * (lo.z + hi.z)}));
}

}  // namespace model
}  // namespace cartoon
