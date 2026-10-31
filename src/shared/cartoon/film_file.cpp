// film_file.cpp -- DataFilm's workings (film_file.h): reading a film file and its scene, its characters' walks, acting and lines, its shots, cameras and transitions, its
// events and captions. Compiled once, in cpu_renderer, so a file that reads or tests films does not compile them again; film_world.cpp has its world and its sound, film_parse.cpp
// the reading of its lines.

#include "film_file.h"
#include "../../external/stb_image.h"   // (a matte painting's size: stbi_info)

namespace cartoon {
namespace film_file {

std::string DataFilm::load(const std::string &filePath, int part) {
	sourcePath = filePath;
	partIndex = part;
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

bool DataFilm::reloadIfChanged(std::string *problem) {
	std::error_code ec;
	if (sourcePath.empty()) return false;
	const auto t = std::filesystem::last_write_time(sourcePath, ec), ts = std::filesystem::last_write_time(sceneFile, ec);
	if (t == sourceTime && ts == sceneTime) return false;
	DataFilm fresh;
	const std::string why = fresh.load(sourcePath, partIndex);
	sourceTime = t, sceneTime = ts;   // (a bad file is not read again until it is saved again)
	if (!why.empty()) {
		if (problem) *problem = why;
		return false;
	}
	*this = std::move(fresh);
	return true;
}

std::string DataFilm::loadFile(const std::string &filePath) {
	std::ifstream f(filePath, std::ios::binary);
	if (!f) return filePath + ": the file cannot be read";
	std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	FilmParts parts;
	if (partIndex >= 0) {
		parts = partsOf(text, filePath);
		if (!parts.error.empty()) return parts.error;
		if (partIndex >= static_cast<int>(parts.parts.size())) return filePath + ": it has no part " + std::to_string(partIndex + 1) + " (it has " + std::to_string(parts.parts.size()) + ")";
		text = parts.parts[static_cast<std::size_t>(partIndex)].text;
	}
	std::vector<Spec::CreditName> castBefore;   // (the last part's credits name the whole film's cast)
	if (partIndex > 0 && partIndex + 1 == static_cast<int>(parts.parts.size()))
		for (int k = 0; k < partIndex; ++k)
			if (const Parsed earlier = parse(parts.parts[static_cast<std::size_t>(k)].text, filePath, &castBefore); earlier.ok) castBefore = earlier.spec.creditCast;
	Parsed p = parse(text, filePath, castBefore.empty() ? nullptr : &castBefore);
	if (!p.ok) return p.error;
	spec = p.spec;
	spec.version = std::to_string(std::hash<std::string>{}(text));
	for (auto &b : spec.bounces) b.second.gravity = spec.gravity;   // (the film's gravity on what falls and topples: `gravity`)
	for (auto &tp : spec.topples) tp.second.gravity = spec.gravity;
	for (const std::string &who : spec.footprints)   // (`footprints NAME`: a character of the film)
		if (who != "*" && std::none_of(spec.characters.begin(), spec.characters.end(), [&](const Character &c) { return c.name == who; }))
			return filePath + ": footprints of \"" + who + "\": no character of that name";
	for (const zerog::Blob &b : spec.blobs)   // (`blob ... on NAME`: a character of the film)
		if (!b.on.empty() && std::none_of(spec.characters.begin(), spec.characters.end(), [&](const Character &c) { return c.name == b.on; }))
			return filePath + ": the blob " + b.name + " on \"" + b.on + "\": no character of that name";
	if (partIndex >= 0) {
		spec.key = (parts.key.empty() ? detail::keyFromFileName(filePath) : parts.key) + "--" + std::to_string(partIndex + 1);
		spec.name = parts.parts[static_cast<std::size_t>(partIndex)].name;
		spec.filmName = parts.name.empty() ? (parts.key.empty() ? detail::keyFromFileName(filePath) : parts.key) : parts.name;   // (for its title and credits)
	}
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
	for (const Spec::Rigged &rg : spec.rigged) {   // the kinds its rigged models make (beside the film file, else in the library)
		std::filesystem::path file = std::filesystem::path(rg.file).is_absolute() ? std::filesystem::path(rg.file) : (rg.dir.empty() ? base : rg.dir) / rg.file;
		std::error_code ec;
		if (!std::filesystem::exists(file, ec) && !std::filesystem::path(rg.file).is_absolute())
			if (const std::string lib = film_cache::asset("library/" + rg.file); !lib.empty()) file = lib;
		// (the kind is this film's own - "<film>/<kind>" in the table every film shares - and its characters are pointed at it: two films' rigged kinds of one name
		// were one kind, the last film's, and a rigged kind named as a built-in one replaced it in every film)
		if (characterKinds().count(rg.kind)) return filePath + ":" + std::to_string(rg.line) + ": \"" + rg.kind + "\" is a kind of character already: name the rigged model something else";
		const std::string own = spec.key + "/" + rg.kind;
		if (const std::string e = addRiggedKind(own, file.string(), rg.options); !e.empty()) return filePath + ":" + std::to_string(rg.line) + ": " + e;
		for (Character &c : spec.characters)
			if (c.kind == rg.kind) c.kind = own;
	}
	for (const explain::Label &l : spec.explain.labels)   // what an explainer's drawings name: things of the film (film_explain.h)
		if (!l.on.empty() && std::none_of(spec.ships.begin(), spec.ships.end(), [&](const cosmos::Ship &s) { return s.name == l.on; }) &&
			std::none_of(spec.planets.begin(), spec.planets.end(), [&](const planets::Planet &p) { return p.name == l.on; }) &&
			std::none_of(spec.characters.begin(), spec.characters.end(), [&](const Character &c) { return c.name == l.on; }))
			return filePath + ": the label \"" + l.text + "\" on \"" + l.on + "\": no craft, planet, moon or character of that name";
	for (const explain::OrbitLine &o : spec.explain.orbits)
		if (std::none_of(spec.ships.begin(), spec.ships.end(), [&](const cosmos::Ship &s) { return s.name == o.of && s.orbit.on; }) &&
			std::none_of(spec.planets.begin(), spec.planets.end(), [&](const planets::Planet &p) { return p.name == o.of && !p.around.empty(); }))
			return filePath + ": the orbit line of \"" + o.of + "\": no craft on an orbit (`orbit`) and no moon of that name";
	for (const explain::Compare &c : spec.explain.compares)
		for (const std::string &name : c.bodies)
			if (!explain::knownBody(name) && std::none_of(spec.planets.begin(), spec.planets.end(), [&](const planets::Planet &p) { return p.name == name; }))
				return filePath + ": compare \"" + name + "\": " + explain::compareUsage();
	for (screens::Screen &sc : spec.screens) {   // a screen's own picture (`show picture FILE`): beside the film file, else in the library
		if (sc.show != "picture") continue;
		std::filesystem::path file = std::filesystem::path(sc.file).is_absolute() ? std::filesystem::path(sc.file) : base / sc.file;
		std::error_code ec;
		if (!std::filesystem::exists(file, ec) && !std::filesystem::path(sc.file).is_absolute())
			if (const std::string lib = film_cache::asset("library/" + sc.file); !lib.empty()) file = lib;
		int w = 0, h = 0, comp = 0;
		if (!stbi_info(file.string().c_str(), &w, &h, &comp) || w <= 0 || h <= 0) return filePath + ": the screen " + sc.name + "'s picture " + file.string() + " cannot be read";
		sc.file = std::filesystem::absolute(file, ec).generic_string();
	}
	for (matte::Matte &m : spec.mattes) {   // the matte paintings' pictures: beside the film file, else in the library; their shapes as wide as they are
		std::filesystem::path file = std::filesystem::path(m.file).is_absolute() ? std::filesystem::path(m.file) : base / m.file;
		std::error_code ec;
		if (!std::filesystem::exists(file, ec) && !std::filesystem::path(m.file).is_absolute())
			if (const std::string lib = film_cache::asset("library/" + m.file); !lib.empty()) file = lib;
		int w = 0, h = 0, comp = 0;
		if (!stbi_info(file.string().c_str(), &w, &h, &comp) || w <= 0 || h <= 0) return filePath + ":" + std::to_string(m.line) + ": the matte painting " + file.string() + " cannot be read";
		m.file = std::filesystem::absolute(file, ec).generic_string();
		m.aspect = static_cast<double>(w) / h;
	}
	for (Spec::FaceAct &a : spec.faceActs) {   // the faces' acting from videos (face_track.h): beside the film, else in the library
		if (const Character *c = character(a.who); c && characterKinds().count(c->kind) && !characterKinds().at(c->kind).face)
			return filePath + ":" + std::to_string(a.line) + ": " + a.who + " is a " + c->kind + ", which has no face to act with (a rigged model needs face morphs)";
		std::filesystem::path file = std::filesystem::path(a.file).is_absolute() ? std::filesystem::path(a.file) : (a.dir.empty() ? base : a.dir) / a.file;
		std::error_code ec;
		if (!std::filesystem::exists(file, ec) && !std::filesystem::path(a.file).is_absolute())
			if (const std::string lib = film_cache::asset("library/" + a.file); !lib.empty()) file = lib;
		std::string why;
		a.track = face_track::load(file.string(), why);
		if (!a.track) return filePath + ":" + std::to_string(a.line) + ": " + why;
	}
	for (const Spec::Mocap &mc : spec.mocaps) {   // the motion captures its rigged characters play: a clip of the kind's rig, played as a gesture
		std::filesystem::path file = std::filesystem::path(mc.file).is_absolute() ? std::filesystem::path(mc.file) : (mc.dir.empty() ? base : mc.dir) / mc.file;
		std::error_code ec;
		if (!std::filesystem::exists(file, ec) && !std::filesystem::path(mc.file).is_absolute())
			if (const std::string lib = film_cache::asset("library/" + mc.file); !lib.empty()) file = lib;
		Character *who = nullptr;
		for (Character &c : spec.characters)
			if (c.name == mc.who) who = &c;
		if (!who) continue;
		char clip[300];
		std::snprintf(clip, sizeof clip, "mocap:%s|%g|%g|%d", file.filename().string().c_str(), mc.speed, mc.turn, mc.travel ? 1 : 0);
		mocap::Options o;
		o.speed = mc.speed, o.turn = mc.turn, o.travel = mc.travel;
		double length = 0.0;
		if (const std::string e = addMocapClip(who->kind, clip, file.string(), o, length); !e.empty()) return filePath + ":" + std::to_string(mc.line) + ": " + e;
		rig::Gesture g;   // (played from its start - a gesture's clip runs from its in0 - faded in over its first 0.3 s and out over its last)
		g.pose = clip;
		const double end = mc.to > mc.at ? mc.to : mc.at + length;
		g.in0 = mc.at, g.in1 = std::min(mc.at + 0.3, 0.5 * (mc.at + end));
		g.out0 = std::max(g.in1, end - 0.3), g.out1 = end;
		who->gestures.push_back(g);
	}
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
	for (speech::Line &l : spec.lines)   // (a character's voice: its pitch and pace under each of its lines' own)
		if (const Character *c = character(l.who)) l.pitch += c->pitch, l.rate *= c->rate;
	for (const hair::Groom &g : spec.grooms)   // (`hair NAME ...`: a kind with a head to grow it on)
		if (const Character *c = g.who.empty() ? nullptr : character(g.who); c && characterKinds().count(c->kind) && !characterKinds().at(c->kind).headFrame)
			return filePath + ": " + g.who + " is a " + c->kind + ", which takes no hair of strands (a person and Pip do)";
	spoken.clear();
	phones.clear();
	for (const speech::Line &l : spec.lines) {
		spoken.push_back(speech::syllablesOf(l));
		phones.push_back(speech::lipsync::phonesOf(l, spoken.back()));
	}
	if (spec.actingWords) actFromWords();
	if (spec.hasSky || spec.space.on) leaveOutSkyLights();
	for (const Spec::Prop &pr : spec.props)
		if (std::find(scene.names.begin(), scene.names.end(), pr.name) == scene.names.end())
			return filePath + ":" + std::to_string(pr.line) + ": no block of " + sceneFile.filename().string() + " is marked `# @anim " + pr.name + "`";
	if (const std::string e = findWays(); !e.empty()) return filePath + ":" + e;
	heightsOfWalks();
	if (const std::string e = walksInTurn(); !e.empty()) return filePath + ":" + e;
	if (const std::string e = crowding(); !e.empty()) return filePath + ": " + e;
	if (const std::string e = holdingTimeline(); !e.empty()) return filePath + ":" + e;
	for (const Spec::Contact &k : spec.contacts)
		for (const std::string &name : {k.who, k.other})
			if (!name.empty() && !character(name)) return filePath + ":" + std::to_string(k.line) + ": " + name + " is not brought in with `character`";
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
		if (!shot.lightOn.empty()) named.push_back(shot.lightOn);
		for (const std::string &who : named) {
			Vec3 feet, front, chest;
			if (!character(who) && !wildSubject(who, shot.start, feet, front, chest))
				return filePath + ": the shot \"" + shot.name + "\" names " + who + ", who is not brought in with `character` (nor a herd or a flock: herd1, flock1 ...)";
		}
	}
	return std::string();
}

void DataFilm::actFromWords() {
	std::map<std::string, std::vector<std::pair<double, double>>> ownGestures, ownFeelings;   // (the film's own, before any is added)
	for (const Character &c : spec.characters) {
		for (const rig::Gesture &g : c.gestures) ownGestures[c.name].push_back({g.in0, g.out1 >= rig::kForever ? 1e30 : g.out1});
		for (const faces::Feeling &f : c.feelings) ownFeelings[c.name].push_back({f.when.in0, f.when.out1 >= rig::kForever ? 1e30 : f.when.out1});
	}
	const auto busy = [](const std::vector<std::pair<double, double>> &spans, double t0, double t1) {
		for (const auto &[a, b] : spans)
			if (a < t1 && b > t0) return true;
		return false;
	};
	for (std::size_t i = 0; i < spec.lines.size(); ++i) {
		Character *c = nullptr;
		for (Character &each : spec.characters)
			if (each.name == spec.lines[i].who) c = &each;
		if (!c) continue;
		const CharacterKind &kind = characterKinds().at(c->kind);
		const double t0 = spec.lines[i].t, t1 = speech::endOf(spoken[i], t0);
		for (const acting::Cue &cue : acting::cuesOf(spec.lines[i].text, t0, t1, kind.poses, kind.face)) {
			if (cue.expression) {
				if (busy(ownFeelings[c->name], t0 - 0.3, t1 + 0.5)) continue;
				faces::Feeling f;
				f.when = {cue.what, cue.at - 0.25, cue.at + 0.15, std::max(cue.at + 0.6, t1 + 0.4), std::max(cue.at + 1.1, t1 + 0.9)};
				f.amount = 0.8;
				c->feelings.push_back(f);
			} else {
				if (busy(ownGestures[c->name], t0 - 0.5, t1 + 0.5)) continue;
				const bool quick = cue.what == "nod" || cue.what == "shake head";
				rig::Gesture g;
				g.pose = cue.what;
				g.in0 = cue.at - (quick ? 0.15 : 0.35);
				g.in1 = cue.at + (quick ? 0.1 : 0.15);
				g.out0 = quick ? cue.at + 0.75 : std::max(cue.at + 0.9, t1);
				g.out1 = g.out0 + (quick ? 0.3 : 0.45);
				c->gestures.push_back(g);
			}
		}
	}
}

void DataFilm::leaveOutSkyLights() {
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

std::string DataFilm::dressingAt(double t) const {
	std::string out = spec.hasTerrain ? terrain::text(spec.key.empty() ? std::string("film") : spec.key, spec.terrain, weatherAt(t)) : std::string();
	if (spec.placements.empty()) return out;
	out += "# the set's dressing (film_dressing.h)\n" + dressing::objects();
	for (const dressing::Placement &p : spec.placements) out += dressing::placementText(p, natureAt(t), spec.gusts);
	return out;
}

terrain::Weather DataFilm::weatherAt(double t) const {
	terrain::Weather w;
	for (const Spec::Ramp &r : spec.wetness) w.wet = std::max(w.wet, r.at(t));
	for (const Spec::Ramp &r : spec.snowcover) w.snow = std::max(w.snow, r.at(t));
	w.wet = std::round(w.wet * 40.0) / 40.0, w.snow = std::round(w.snow * 40.0) / 40.0;   // (frames a moment apart write the same text)
	return w;
}

std::string DataFilm::setAt(double t) const {
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

std::string DataFilm::findWays() {
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

void DataFilm::heightsOfWalks() {
	terrains.clear();
	const auto grounded = [this](const Character &c) { return motion::amountFor(spec.footing, c.name) > 0.5; };
	const bool anyWalks = std::any_of(spec.characters.begin(), spec.characters.end(), [&](const Character &c) { return !c.walks.empty() || grounded(c); });
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
		if (grounded(c)) {   // (`footing ground`: it stands on the floor under it - one near where it was put, else the film's land however far above or below)
			const double nearby = terrain->floorAt(c.start[0], c.start[2], c.start[1], std::max(0.6, stepOf(c)));   // (not `near`: a Windows macro)
			if (nearby != c.start[1] || !spec.hasTerrain) {
				c.start[1] = nearby;
			} else if (const std::shared_ptr<const terrain::Field> land = terrain::fieldOf(spec.terrain); land->inside(c.start[0], c.start[2])) {
				c.start[1] = land->heightAt(c.start[0], c.start[2]);
				terrain = std::make_shared<film_paths::Terrain>(film_paths::terrainOf(flat, 0.5 * (x0 + x1), 0.5 * (z0 + z1), half, 0.1, c.start[1]));   // (its floors searched from there)
			}
		}
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
		if (uneven || grounded(c)) terrains[c.name] = terrain;   // (its feet on the floor too)
	}
}

const film_paths::Terrain * DataFilm::terrainFor(const Character &c) const {
	const auto it = terrains.find(c.name);
	return it == terrains.end() ? nullptr : it->second.get();
}

std::string DataFilm::crowding() const {
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

camera_keyframes::Key DataFilm::keyOf(Vec3 eye, Vec3 target, double fov, double lens) {
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

Vec3 DataFilm::lineOf(const std::vector<const Character *> &cast, double t) const {
	Vec3 d;
	if (cast.size() >= 2) d = headOf(*cast[1], t) - headOf(*cast[0], t);
	d.y = 0.0;
	if (length(d) < 1e-6) {
		const Mat4 f = momentOf(*cast[0], t).frame;
		d = transformPoint(f, {-1.0, 0.0, 0.0}) - transformPoint(f, {0.0, 0.0, 0.0});   // (its right)
	}
	return normalize(d);
}

int DataFilm::sideTheyFace(const std::vector<const Character *> &cast, double t) const {
	const Vec3 d = lineOf(cast, t), n{-d.z, 0.0, d.x};
	double toward = 0.0;
	for (const Character *c : cast) {
		const Mat4 f = momentOf(*c, t).frame;
		toward += dot(transformPoint(f, {0.0, 0.0, 1.0}) - transformPoint(f, {0.0, 0.0, 0.0}), n);
	}
	return toward < -1e-6 ? -1 : 1;
}

camera_keyframes::Key DataFilm::coverageKey(const std::vector<const Character *> &cast, const Character *speaker, int side, double t) const {
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

std::string DataFilm::coverShots() {
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
			g.lighting = shot.lighting;   // (its lighting on the speaker the shot is of, unless it names someone)
			g.lightOn = !shot.lightOn.empty() ? shot.lightOn : beats[b].second ? beats[b].second->name : std::string();
			if (beats[b].second) g.about.push_back(beats[b].second->name);
			else
				for (const Character *c : cast) g.about.push_back(c->name);
			out.push_back(g);
		}
	}
	spec.shots = out;
	return std::string();
}

const Spec::Prop * DataFilm::prop(const std::string &name) const {
	for (const Spec::Prop &p : spec.props)
		if (p.name == name) return &p;
	return nullptr;
}

std::string DataFilm::holdingTimeline() {
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
				for (const auto &[other, held] : holdings)   // (as a pick: one prop to a hand)
					if (other != h.prop && !held.empty() && held.back().who == h.to && held.back().hand == next.hand) return where + h.to + "'s hand already holds " + other;
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

Vec3 DataFilm::carryOf(const Character &c, int hand, double t) const {
	Vec3 k = characterKinds().at(c.kind).carry;
	if (hand == 1) k.x = -k.x;   // (body space: +x its left)
	return bodyPoint(c, t, k);
}

Vec3 DataFilm::meetingOf(const Character &a, int handA, const Character &b, int handB, double t, double *gap) const {
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

void DataFilm::handsAt(const Character &c, double t, CharacterMoment &m) const {
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
	contactsAt(c, t, m);
}

void DataFilm::contactsAt(const Character &c, double t, CharacterMoment &m) const {
	for (const Spec::Contact &k : spec.contacts) {
		const bool mine = k.who == c.name, theirs = k.other == c.name;
		if ((!mine && !theirs) || t < k.t0 - kReach || t > k.t1 + kSettle) continue;
		const double weight = smoothstep(k.t0 - kReach, k.t0, t) * (1.0 - smoothstep(k.t1, k.t1 + kSettle, t));
		if (k.kind == Spec::Contact::Touch) {
			m.hands[k.hand] = {story(k.at.x, k.at.y, k.at.z), weight};
			continue;
		}
		const Character *other = character(mine ? k.other : k.who);
		if (!other) continue;
		if (k.kind == Spec::Contact::Hold) {   // the one's right hand and the other's left meet between them
			const Character &a = mine ? c : *other, &b = mine ? *other : c;
			m.hands[mine ? 1 : 0] = {meetingOf(a, 1, b, 0, t), weight};
			continue;
		}
		// a hug: both arms round the other's sides, a little below the shoulders
		const Vec3 me = chestOf(c, t), them = chestOf(*other, t);
		Vec3 toward = them - me;
		toward.y = 0.0;
		toward = length(toward) > 1e-6 ? normalize(toward) : Vec3{0.0, 0.0, 1.0};
		const Vec3 side = cross(Vec3{0.0, 1.0, 0.0}, toward);
		const double reach = 0.18 * std::min(c.scale, other->scale);
		for (int h = 0; h < 2; ++h) m.hands[h] = {them + toward * (0.3 * reach) + side * ((h == 0 ? 1.0 : -1.0) * reach) + Vec3{0.0, -0.05, 0.0}, weight};   // (round its sides, just behind them)
	}
}

CharacterMoment DataFilm::actingMoment(const Character &c, double t) const {
	CharacterMoment m = momentOf(c, t);
	handsAt(c, t, m);
	m.idle = spec.idle;
	if (spec.idle) listenAt(c, t, m);
	lookAt(c, t, m);
	return m;
}

void DataFilm::listenAt(const Character &c, double t, CharacterMoment &m) const {
	Vec3 sum{0.0, 0.0, 0.0};
	double weights = 0.0, most = 0.0;
	for (std::size_t i = 0; i < spec.lines.size(); ++i) {
		const speech::Line &l = spec.lines[i];
		const double end = lineEnd(i);
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

Vec3 DataFilm::propAt(const std::string &name, double t) const {
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

void DataFilm::lookAt(const Character &c, double t, CharacterMoment &m) const {
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

const Character * DataFilm::character(const std::string &name) const {
	for (const Character &c : spec.characters)
		if (c.name == name) return &c;
	return nullptr;
}

double DataFilm::natureAt(double t) const {
	if (spec.natureClock.empty()) return t * spec.timelapse;
	const double k = t / Spec::kNatureStep;
	if (k <= 0.0) return t * spec.timelapse;
	const std::size_t i = static_cast<std::size_t>(k);
	if (i + 1 >= spec.natureClock.size()) return spec.natureClock.back() + (t - (spec.natureClock.size() - 1) * Spec::kNatureStep) * spec.timelapse;
	return spec.natureClock[i] + (spec.natureClock[i + 1] - spec.natureClock[i]) * (k - i);
}

bool DataFilm::wildSubject(const std::string &who, double t, Vec3 &feet, Vec3 &front, Vec3 &chest) const {
	{   // under the sea (round 15): a diver by name, or schoolN, swimmersN, bigfishN (the Nth line of that word: its first animal, or a school's middle)
		const double nt = natureAt(t);
		const auto lift = [&](Vec3 p) { return Vec3{p.x, p.y + seabedAt(p.x, p.z), p.z}; };
		Vec3 at, way{0, 0, 1};
		bool found = false;
		for (const diver::Diver &d : spec.divers)
			if (d.name == who) {
				diver::Diver l = d;
				for (Vec3 &p : l.path) p = lift(p);
				at = diver::diverAt(l, nt, &way), found = true;
			}
		double n = 0.0;
		const auto numbered = [&](const char *word) {
			const std::size_t w = std::char_traits<char>::length(word);
			return who.size() > w && who.compare(0, w, word) == 0 && detail::number(who.substr(w), n) && n >= 1.0 && n == std::floor(n);
		};
		if (!found && numbered("school") && static_cast<std::size_t>(n) <= spec.schools.size()) {
			const school::School &sc = schoolsSwum()[static_cast<std::size_t>(n) - 1];
			Vec3 sum{0, 0, 0}, vel{0, 0, 0}, v;
			for (int i = 0; i < sc.count; ++i) sum = sum + school::fishAt(sc, i, nt, &v), vel = vel + v;
			at = sum * (1.0 / std::max(1, sc.count)), way = vel, found = true;
		} else if (!found && numbered("swimmers") && static_cast<std::size_t>(n) <= spec.deep.swimmers.size()) {
			deepsea::Swimmers g = spec.deep.swimmers[static_cast<std::size_t>(n) - 1];
			if (g.kind != "otters") g.at = lift(g.at);
			double roll = 0.0;
			at = deepsea::swimmerAt(g, 0, nt, way, roll, spec.seas.empty() ? 0.0 : spec.seas[0].level), found = true;
		} else if (!found && numbered("bigfish") && static_cast<std::size_t>(n) <= spec.bigFish.size()) {
			school::BigFish b = spec.bigFish[static_cast<std::size_t>(n) - 1];
			b.at = lift(b.at);
			at = school::bigAt(b, 0, nt, way), found = true;
		}
		if (found) {
			way.y = 0.0;
			way = length(way) > 1e-9 ? normalize(way) : Vec3{0, 0, 1};
			feet = story(at.x, at.y, at.z), front = story(way.x, 0.0, way.z), chest = feet;
			return true;
		}
	}
	const bool herd = who.rfind("herd", 0) == 0, flock = who.rfind("flock", 0) == 0;
	if (!herd && !flock) return false;
	double n = 0.0;
	if (!detail::number(who.substr(herd ? 4 : 5), n) || n < 1.0 || n != std::floor(n)) return false;
	const std::size_t i = static_cast<std::size_t>(n) - 1;
	const double nt = natureAt(t);
	const std::shared_ptr<const terrain::Field> ground = spec.hasTerrain ? terrain::fieldOf(spec.terrain) : nullptr;
	Vec3 sum{0, 0, 0}, way{0, 0, 0};
	double lift = 1.0;
	int count = 0;
	if (herd) {
		if (i >= spec.herds.size()) return false;
		const wildlife::Herd &h = spec.herds[i];
		const animals::Species *sp = animals::species(h.kind);
		if (!sp) return false;
		for (int a = 0; a < std::min(h.count, 12); ++a, ++count) {   // (the herd's leading animals: the bunch the camera is on)
			const wildlife::Animal an = wildlife::animalAt(h, *sp, a, nt, ground.get());
			sum = sum + an.at, way = an.dir;
		}
		lift = 0.85 * sp->length;
	} else {
		if (i >= spec.flocks.size()) return false;
		const wildlife::Flock &f = spec.flocks[i];
		if (f.kind == "eagles") {
			for (int a = 0; a < f.count; ++a, ++count) {
				Vec3 fwd;
				sum = sum + wildlife::eagleAt(f, a, nt, &fwd);
				way = way + fwd;
			}
		} else if ((f.kind == "starlings" || f.kind == "fish") && !f.pos.empty()) {
			const std::size_t k = std::min(f.pos.size() - 1, static_cast<std::size_t>(std::max(0.0, (t - f.t0) / f.dt)));
			for (std::size_t a = 0; a < f.pos[k].size(); ++a, ++count) sum = sum + f.pos[k][a], way = way + f.vel[k][a];
		} else if (f.kind == "geese") {
			const double speed = f.speed > 0.0 ? f.speed : 15.0;
			way = normalize(f.to - f.from + Vec3{1e-9, 0, 0});
			sum = f.from + way * std::min(length(f.to - f.from), (nt - f.t0) * speed), count = 1;
		} else {   // butterflies: over their patch
			sum = Vec3{0.5 * (f.x0 + f.x1), f.at.y + (ground ? ground->heightAt(0.5 * (f.x0 + f.x1), 0.5 * (f.z0 + f.z1)) : 0.0) + 0.8, 0.5 * (f.z0 + f.z1)}, count = 1;
			way = {0, 0, 1};
		}
		lift = 0.0;
	}
	if (count == 0) return false;
	const Vec3 mid = sum * (1.0 / count);
	way.y = 0.0;
	if (length(way) < 1e-9) way = {0, 0, 1};
	way = normalize(way);
	feet = story(mid.x, mid.y, mid.z);
	front = story(way.x, 0.0, way.z);
	chest = feet + Vec3{0.0, lift, 0.0};
	return true;
}

bool DataFilm::subjectFrame(const std::string &who, double t, Vec3 &feet, Vec3 &front, Vec3 &chest) const {
	if (const Character *c = character(who)) {
		const CharacterMoment m = momentOf(*c, t, &feet);
		front = normalize(transformPoint(m.frame, {0.0, 0.0, 1.0}) - feet);
		chest = transformPoint(m.frame, characterKinds().at(c->kind).chest * c->scale);
		return true;
	}
	return wildSubject(who, t, feet, front, chest);
}

rig::PathWalk DataFilm::pathOf(const Character &c, std::size_t walk) const {
	const CharacterKind &kind = characterKinds().at(c.kind);
	Vec3 from = story(c.start[0], c.start[1], c.start[2]);
	for (std::size_t i = 0; i < walk; ++i) from = endOf(c, i);
	rig::PathWalk w;
	w.points.push_back(from);
	const WalkLine &wl = c.walks[walk];
	for (std::size_t p = 0; p < wl.points.size(); ++p) w.points.push_back(story(wl.points[p].first, p < wl.heights.size() ? wl.heights[p] : c.start[1], wl.points[p].second));
	w.start = c.walks[walk].t0;
	w.speed = c.walks[walk].speed;
	w.stride = kind.stride * c.scale * (c.walks[walk].style == WalkLine::Run ? kRunStride : 1.0) * motion::gaitStride(c.walks[walk].gait);   // (a sneak's long steps, a tiptoe's short)
	if (spec.gravity < 9.81) w.stride *= std::min(2.0, std::pow(9.81 / std::max(0.1, spec.gravity), 0.35));   // (low gravity: long bounding steps, `gravity`)
	w.firstStep = kind.firstStep;
	return w;
}

Vec3 DataFilm::endOf(const Character &c, std::size_t walk) const {
	const WalkLine &w = c.walks[walk];
	return story(w.points.back().first, w.heights.empty() ? c.start[1] : w.heights.back(), w.points.back().second);
}

std::string DataFilm::walksInTurn() {
	for (Character &c : spec.characters)
		for (std::size_t i = 0; i + 1 < c.walks.size(); ++i) {
			const WalkLine &w = c.walks[i];
			if (w.points.empty()) continue;
			const double end = w.style == WalkLine::Jump ? w.t0 + flightOf(w) : w.t0 + pathOf(c, i).length() / std::max(1e-6, pathOf(c, i).speed);
			WalkLine &next = c.walks[i + 1];
			if (next.t0 >= end - 1e-3) continue;
			if (c.extra) {
				next.t0 = end;
				continue;
			}
			char b[200];
			std::snprintf(b, sizeof b, "%d: %s's walk starts at %.2f s, but the one before it (line %d) goes on until %.2f s: start it then, or make the one before shorter or quicker",
						  next.line, c.name.c_str(), next.t0, w.line, end);
			return b;
		}
	return std::string();
}

double DataFilm::landedAt(const Character &c, double t) const {
	double land = -1e30;
	for (const WalkLine &w : c.walks)
		if (w.style == WalkLine::Jump && w.t0 + flightOf(w) <= t) land = w.t0 + flightOf(w);
	return land;
}

CharacterMoment DataFilm::momentOf(const Character &c, double t, Vec3 *at) const {
	Vec3 where = story(c.start[0], c.start[1], c.start[2]);
	double heading = -c.facing, steps = 0.0, running = 0.0, crouch = 0.0, going = 0.0;
	bool moving = false, airborne = false;
	motion::Gait gait = motion::Gait::Plain;
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
		const double end = w.t0 + path.length() / std::max(1e-6, path.speed);
		going = a.moving ? smoothstep(w.t0, w.t0 + 0.3, t) : 1.0 - smoothstep(end, end + 0.3, t);
		where = a.at;
		// it turns to its way as it sets off, from the way it faced (a quarter of a second, and more the further it turns): it took the path's heading in one frame,
		// its planted feet twisting under it
		double turnBy = a.yaw - heading;
		while (turnBy > 180.0) turnBy -= 360.0;
		while (turnBy < -180.0) turnBy += 360.0;
		const double turning = 0.25 + std::abs(turnBy) / 360.0;
		heading = t < w.t0 + turning ? heading + turnBy * smoothstep(w.t0, w.t0 + turning, t) : a.yaw;
		steps = a.steps;
		moving = a.moving;
		running = w.style == WalkLine::Run ? 1.0 : 0.0;
		gait = w.gait;
		if (moving) break;
	}
	double swim = 0.0;   // (round 16) under the water a walk is a swim: at its own height, bobbing, its feet on nothing (film_kinds.h strokes it, castAt lays it level)
	if (swimming(c)) {
		swim = 1.0, airborne = true;
		where.y = c.start[1] + 0.06 * c.scale * std::sin(2.0 * kPi * t / 3.2 + seedOf(c.name));
	}
	CharacterMoment m;
	m.swim = swim;
	m.running = running;
	m.gait = moving || going > 0.0 ? gait : motion::Gait::Plain;
	m.crouch = std::max(crouch, motion::gaitCrouch(m.gait) * going);   // (a sneak goes crouched)
	m.airborne = airborne;
	for (const Character::Sit &st : c.sits) {
		const double s = smoothstep(st.t0, st.t0 + 0.7, t) * (1.0 - smoothstep(st.t1, st.t1 + 0.7, t));
		if (s > m.sit) m.sit = s, m.seat = st.height * c.scale;
	}
	m.frame = mul(translate(where), rotate({0.0, 1.0, 0.0}, heading));
	m.steps = steps;
	m.moving = moving;
	m.going = going;
	m.t = t;
	m.gestures = &c.gestures;
	m.detail = c.detail;
	m.scale = c.scale;
	m.seed = seedOf(c.name);
	m.colours = &c.colours;
	if (at) *at = where;
	return m;
}

void DataFilm::lockFeet(const Character &c, double t, CharacterMoment &m) const {
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

// `footing ground`: a character standing still has each foot on the floor under that foot (a walk's planted feet are already: lockFeet); its hips come down so the lower foot
// reaches its floor (the higher leg bends), at most a third of its height; and each foot is tilted to the slope under it (CharacterMoment::footNormal).
void DataFilm::footOnGround(const Character &c, CharacterMoment &m) const {
	if (motion::amountFor(spec.footing, c.name) < 0.5 || m.airborne) return;
	const CharacterKind &kind = characterKinds().at(c.kind);
	const film_paths::Terrain *ground = terrainFor(c);
	if (!ground || !kind.footAt) return;
	const double bodyY = transformPoint(m.frame, {0.0, 0.0, 0.0}).y, step = stepOf(c);
	const auto floor = [&](double x, double z) { return ground->floorAt(-x, z, bodyY, step); };   // (story space; the terrain is pbrt's)
	const int feet = std::min(4, kind.footCount);
	if (!m.feetLocked) {   // (standing: its feet where its stance puts them, each on the floor under it, as high above it as above the body's floor)
		m.feetLocked = true;
		for (int s = 0; s < feet; ++s) {
			const Vec3 local = kind.footAt(m.steps, s) * m.scale, w = transformPoint(m.frame, local);
			m.lockedFeet[s] = local + Vec3{0.0, floor(w.x, w.z) - bodyY, 0.0};
		}
	}
	double lowest = bodyY;
	for (int s = 0; s < feet; ++s) {
		const Vec3 w = transformPoint(m.frame, m.lockedFeet[s]);
		const double f = floor(w.x, w.z), d = 0.06 * m.scale;
		lowest = std::min(lowest, f);
		const double sx = (floor(w.x + d, w.z) - floor(w.x - d, w.z)) / (2.0 * d), sz = (floor(w.x, w.z + d) - floor(w.x, w.z - d)) / (2.0 * d);
		m.footNormal[s] = normalize(Vec3{-std::clamp(sx, -1.5, 1.5), 1.0, -std::clamp(sz, -1.5, 1.5)});   // (a step's edge is not a slope: steeper than 56 degrees is taken as that)
	}
	const double drop = std::min(bodyY - lowest, kind.height * m.scale / 3.0);
	if (drop > 1e-4) {   // (the body down to the lower foot's floor, the feet staying where they are)
		m.frame = mul(translate(Vec3{0.0, -drop, 0.0}), m.frame);
		for (int s = 0; s < feet; ++s) m.lockedFeet[s] = m.lockedFeet[s] + Vec3{0.0, drop, 0.0};
	}
}

// A character at t as it is drawn - acting, its feet locked where they came down and (`footing ground`) on the floor under them, its hips down to the lower one - so what
// it wears, holds or has drawn by it is where its body is.
CharacterMoment DataFilm::standingMoment(const Character &c, double t) const {
	CharacterMoment m = actingMoment(c, t);
	lockFeet(c, t, m);
	footOnGround(c, m);
	return m;
}

Vec3 DataFilm::trailOf(const Character &c, double t, const CharacterMoment &m) const {
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

int DataFilm::seedOf(const std::string &name) {
	std::uint32_t h = 2166136261u;
	for (char ch : name) h = (h ^ static_cast<unsigned char>(ch)) * 16777619u;
	return static_cast<int>(h % 100000u);
}

std::size_t DataFilm::shotAt(double t) const {
	std::size_t k = 0;
	for (std::size_t i = 0; i < spec.shots.size(); ++i)
		if (t >= spec.shots[i].start) k = i;
	return k;
}

std::vector<Vec3> DataFilm::subjectsAt(double t) const {
	std::vector<Vec3> out;
	if (spec.shots.empty()) return out;
	const Shot &shot = spec.shots[shotAt(t)];
	std::vector<std::string> named = shot.about;
	if (shot.following && std::find(named.begin(), named.end(), shot.follow.who) == named.end()) named.insert(named.begin(), shot.follow.who);
	for (const std::string &who : named) {
		Vec3 feet, front, chest;
		if (const Character *c = character(who)) out.push_back(chestOf(*c, t));
		else if (wildSubject(who, t, feet, front, chest)) out.push_back(chest);
	}
	return out;
}

std::vector<Foot> DataFilm::feetAt(double t) const {
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

std::vector<FilmEvent> DataFilm::events() const {
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
	for (const Spec::Contact &k : spec.contacts) {   // a hand on the set, two hands, a hug
		const Character *c = character(k.who);
		if (!c) continue;
		FilmEvent e;
		e.t = k.t0;
		e.kind = EventKind::Touch;
		e.seed = ++seed;
		const CameraView v = viewAt(k.t0);
		const Vec3 at = k.kind == Spec::Contact::Touch ? story(k.at.x, k.at.y, k.at.z) : chestOf(*c, k.t0);
		e.pan = panOf(v, at);
		e.strength = (k.kind == Spec::Contact::Hug ? 0.6 : 0.8) * audibility(length(at - v.eye));
		out.push_back(e);
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
	if (!spec.rigids.empty()) {   // (the rigid-body world's hard landings)
		const std::vector<physics::Impact> &i = rigidRun()->impacts;
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

std::string DataFilm::voiceAskedOf(const Character &c) const {
	const std::string v = c.voice.empty() ? spec.voices : c.voice;
	return v == "babble" ? std::string() : v;
}

const music_cues::Recording * DataFilm::recordingOf(std::size_t i) const {
	const Character *c = character(spec.lines[i].who);
	const std::string asked = c ? voiceAskedOf(*c) : std::string();
	if (asked.empty()) return nullptr;
	const double kindHz = characterKinds().at(c->kind).voice.pitch;
	const double raise = kindHz > 200.0 ? std::min(9.0, 12.0 * std::log2(kindHz / 165.0)) : 0.0;
	std::string why;
	const std::string path = tts::wavFor(spec.lines[i].text, asked, static_cast<unsigned>(seedOf(c->name)), spec.lines[i].pitch + raise, spec.lines[i].rate, &why);
	if (path.empty()) {
		static std::mutex lock;
		static std::set<std::string> said;
		const std::lock_guard<std::mutex> hold(lock);
		if (tts::runner() && said.insert(spec.key + "|" + std::to_string(i)).second)   // (once; and not where no speech engine is expected: the GUI's preview)
			std::cerr << "[voice] " << spec.key << " line " << spec.lines[i].line << ": " << why << " - it is said in the babble voice" << std::endl;
		return nullptr;
	}
	const music_cues::Recording &r = music_cues::recording(path);
	return r.loudness.empty() ? nullptr : &r;
}

double DataFilm::lineEnd(std::size_t i) const {
	{
		const std::lock_guard<std::mutex> hold(lineEnds_.lock);
		if (lineEnds_.ends.size() == spec.lines.size() && lineEnds_.ends[i] == lineEnds_.ends[i]) return lineEnds_.ends[i];
	}
	double end = speech::endOf(spoken[i], spec.lines[i].t);
	if (const music_cues::Recording *r = recordingOf(i)) {
		std::size_t last = 0;
		for (std::size_t k = 0; k < r->loudness.size(); ++k)
			if (r->loudness[k] > 0.05f) last = k;
		end = spec.lines[i].t + (last + 1) / 100.0;
	}
	const std::lock_guard<std::mutex> hold(lineEnds_.lock);
	if (lineEnds_.ends.size() != spec.lines.size()) lineEnds_.ends.assign(spec.lines.size(), std::numeric_limits<double>::quiet_NaN());
	lineEnds_.ends[i] = end;
	return end;
}

void DataFilm::speakingAt(const Character &c, double t, CharacterMoment &m) const {
	for (std::size_t i = 0; i < spec.lines.size(); ++i) {
		if (spec.lines[i].who != c.name || t < spec.lines[i].t - 0.1 || t > lineEnd(i) + 0.1) continue;
		speech::Mouth mouth = speech::lipsync::mouthAt(phones[i], t);   // (each sound's shape, round 12: film_lipsync.h)
		double loud = speech::loudnessAt(spoken[i], t);
		if (const music_cues::Recording *r = recordingOf(i)) {
			const std::size_t k = static_cast<std::size_t>(std::max(0.0, t - spec.lines[i].t) * 100.0);
			mouth = k < r->mouth.size() ? r->mouth[k] : speech::Mouth();
			loud = k < r->loudness.size() ? r->loudness[k] : 0.0;
			speech::visemesFromShape(mouth);
		}
		if (mouth.open + mouth.closed + mouth.teeth > m.mouth.open + m.mouth.closed + m.mouth.teeth) m.mouth = mouth;   // (two lines at once: the one that moves the mouth more)
		m.voice = std::max(m.voice, loud);
	}
}

double DataFilm::speechAt(double t) const {
	double v = 0.0;
	for (std::size_t i = 0; i < spec.lines.size(); ++i) {
		if (t < spec.lines[i].t - 0.1 || t > lineEnd(i) + 0.1) continue;
		if (const music_cues::Recording *r = recordingOf(i)) {
			const std::size_t k = static_cast<std::size_t>(std::max(0.0, t - spec.lines[i].t) * 100.0);
			if (k < r->loudness.size()) v = std::max(v, static_cast<double>(r->loudness[k]));
		} else {
			v = std::max(v, speech::loudnessAt(spoken[i], t));
		}
	}
	bool voiced = false;   // (the shots worked out once, not once for each character)
	for (const Character &c : spec.characters) voiced = voiced || !c.extra;
	if (voiced) {
		const std::vector<double> starts = shotStarts();
		const std::vector<std::string> names = shotNameList();
		for (const Character &c : spec.characters)
			if (!c.extra) v = std::max(v, music_cues::voiceAt(spec.key, c.name, t, starts, names));
	}
	return std::min(1.0, v);
}

speech::Voice DataFilm::voiceOf(const Character &c) const {
	speech::Voice v = characterKinds().at(c.kind).voice;
	v.pitch *= std::pow(2.0, (static_cast<double>(seedOf(c.name) % 1000) / 999.0 * 3.0 - 1.5) / 12.0) / std::sqrt(c.scale);   // (a bigger one is lower)
	return v;
}

void DataFilm::speak(audio::Mix &mix) const {
	for (std::size_t i = 0; i < spec.lines.size(); ++i) {
		const Character &c = *character(spec.lines[i].who);
		const double t0 = spec.lines[i].t;
		Vec3 eye, target;
		double fov = 40.0;
		film_subjects::cameraOf(headerAt(t0), eye, target, fov);
		const CameraView view{eye, target, fov, 0.0, 0};
		const Vec3 at = chestOf(c, t0);
		const double gain = std::max(0.35, audibility(length(at - eye)));
		std::vector<float> sound = recordingOf(i) ? recordingOf(i)->sound : speech::speak(spoken[i], t0, voiceOf(c), static_cast<unsigned>(i + 1));
		// (round 16) under the water a voice is muffled - its highs gone, a little louder below, bubbling (the speaker's bubbles are drawn: castAt) - when the one speaking
		// or the ears are under it
		const bool under = swimming(c) || (spec.hasUnderwater && !spec.seas.empty() && eye.y < spec.seas[0].level);
		if (under) {
			const double k = 1.0 - std::exp(-2.0 * kPi * 650.0 / audio::kRate);   // (two passes of a one-pole low-pass at 650 Hz)
			for (int pass = 0; pass < 2; ++pass) {
				double y = 0.0;
				for (float &v : sound) y += k * (v - y), v = static_cast<float>(1.3 * y);
			}
		}
		if (recordingOf(i)) mix.put(t0, sound, 0.8 * gain, panOf(view, at) * 0.6, under ? 0.45 : 0.2);   // (a computer's voice: its recording)
		else mix.put(t0, sound, 0.55 * gain, panOf(view, at) * 0.6, under ? 0.45 : 0.25);
	}
}

std::vector<Caption> DataFilm::lineCaptions() const {
	std::vector<Caption> out;
	std::vector<std::size_t> order;
	for (std::size_t i = 0; i < spec.lines.size(); ++i)
		if (spec.lines[i].caption) order.push_back(i);
	std::stable_sort(order.begin(), order.end(), [this](std::size_t a, std::size_t b) { return spec.lines[a].t < spec.lines[b].t; });
	for (std::size_t k = 0; k < order.size(); ++k) {
		const speech::Line &l = spec.lines[order[k]];
		Caption cap;
		cap.t0 = l.t;
		cap.t1 = lineEnd(order[k]) + 0.6;
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
	const std::vector<Caption> cards = titleCaptions();
	out.insert(out.end(), cards.begin(), cards.end());
	return out;
}

std::vector<Caption> DataFilm::titleCaptions() const {
	std::vector<Caption> out;
	const std::string name = spec.filmName.empty() ? spec.name : spec.filmName;
	const std::string episode = spec.episode > 0 ? "Episode " + std::to_string(spec.episode) + (spec.episodeName.empty() ? std::string() : ": " + spec.episodeName) : std::string();
	const auto card = [&](double t0, double t1, double fade, const std::vector<std::pair<std::string, double>> &lines) {
		Caption c;
		c.t0 = t0, c.t1 = t1, c.fade = fade, c.centreY = 0.46, c.width = 0.8;
		for (const auto &[text, size] : lines) {
			CaptionLine l;
			l.text = text;
			l.size = size;
			l.weight = size > 0.06 ? 1.5 : 0.0;
			c.lines.push_back(l);
		}
		out.push_back(c);
	};
	if (spec.hasTitle) {
		std::vector<std::pair<std::string, double>> lines{{spec.title.empty() ? name : spec.title, 0.09}};
		if (!episode.empty()) lines.push_back({episode, 0.045});
		card(spec.titleAt, spec.titleAt + spec.titleFor, 0.7, lines);
	}
	if (!spec.credits) return out;
	double t = spec.storyEnd + 0.6;   // (after the fade to black)
	std::vector<std::pair<std::string, double>> first{{name, 0.075}};
	if (!episode.empty()) first.push_back({episode, 0.042});
	card(t, t + 2.6, 0.45, first);
	t += 3.0;
	const std::vector<Spec::CreditName> &cast = spec.creditCast;
	for (std::size_t k = 0; k < cast.size(); k += 5) {
		std::vector<std::pair<std::string, double>> lines{{k == 0 ? "Cast" : "Cast (continued)", 0.05}};
		for (std::size_t j = k; j < std::min(cast.size(), k + 5); ++j) {
			std::string who = cast[j].name, kind = cast[j].kind;
			if (!who.empty()) who[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(who[0])));
			std::replace(kind.begin(), kind.end(), '-', ' ');
			lines.push_back({who + "  -  a " + kind, 0.038});
		}
		card(t, t + 2.6, 0.45, lines);
		t += 3.0;
	}
	std::vector<std::pair<std::string, double>> last;
	for (const std::string &l : spec.creditLines) last.push_back({l, 0.04});
	if (std::any_of(cast.begin(), cast.end(), [](const Spec::CreditName &c) { return c.voiced; })) last.push_back({"Voices by the computer's own text-to-speech", 0.034});
	last.push_back({"Made with Photonsmith", 0.034});
	card(t, std::min(spec.length - 0.1, t + 2.6), 0.45, last);
	return out;
}

void DataFilm::playTheme(audio::Mix &m) const {
	if (spec.theme.empty()) return;
	const double t0 = spec.hasTitle ? std::max(0.0, spec.titleAt - 0.3) : 0.0, g = spec.themeGain;
	if (spec.theme == "jingle") {
		const double arpeggio[4] = {72.0, 76.0, 79.0, 84.0};   // C E G C, up
		for (int i = 0; i < 4; ++i) m.put(t0 + 0.22 * i, audio::bell(audio::hz(arpeggio[i]), 2.2), 0.11 * g, -0.3 + 0.2 * i, 0.5);
		for (const double note : {60.0, 64.0, 67.0}) m.put(t0 + 0.9, audio::pad(audio::hz(note), 2.6, 0.3, 1.2), 0.035 * g, 0.0, 0.7);
		m.put(t0 + 0.9, audio::bass(audio::hz(48.0), 2.2), 0.09 * g, 0.0, 0.2);
		return;
	}
	std::error_code ec;
	std::string file = (std::filesystem::path(sourcePath).parent_path() / "library" / spec.theme).string();
	if (!std::filesystem::exists(file, ec)) file = film_cache::asset("library/" + spec.theme);
	const music_cues::Recording &r = music_cues::recording(file);
	if (r.sound.empty()) {
		std::cerr << "[theme] " << spec.key << ": " << spec.theme << " " << (r.why.empty() ? "is not in the film library" : r.why) << std::endl;
		return;
	}
	m.put(t0, r.sound, 0.6 * g, 0.0, 0.2);
}

double DataFilm::panOf(const CameraView &view, Vec3 at) {
	const Vec3 f = normalize(view.target - view.eye), right = normalize(cross(f, Vec3{0.0, 1.0, 0.0}));
	const Vec3 d = at - view.eye;
	const double depth = dot(d, f);
	if (depth <= 1e-6) return 0.0;
	return std::max(-1.0, std::min(1.0, dot(d, right) / (depth * std::tan(view.fov * kPi / 360.0) * 16.0 / 9.0))) * 0.8;
}

std::vector<double> DataFilm::shotStarts() const {
	std::vector<double> v;
	for (const Shot &s : spec.shots) v.push_back(s.start);
	if (v.empty()) v.push_back(0.0);
	return v;
}

std::vector<std::string> DataFilm::shotNameList() const {
	std::vector<std::string> out;
	for (const Shot &s : spec.shots) out.push_back(s.name);
	return out;
}

std::string DataFilm::shotNames() const {
	std::string out;
	for (const Shot &s : spec.shots) out += (out.empty() ? "" : "|") + s.name;
	return out.empty() ? "The whole film" : out;
}

std::string DataFilm::creditsScene() const {
	return scene.header + "AttributeBegin\n  Material \"diffuse\" \"rgb reflectance\" [ 0 0 0 ]\n  Translate 0 -100000 0\n  Shape \"sphere\" \"float radius\" [ 0.001 ]\nAttributeEnd\n";
}

std::string DataFilm::headerAt(double t) const {
	if (inCredits(t)) return scene.header;
	if (spec.shots.empty()) return toonLine(t) + scene.header;
	std::string lookAt, camera;
	cameraAt(t, lookAt, camera);
	// The scene's lines before its LookAt, then the new LookAt, the lines between, the new Camera, the lines after. A scene with neither statement gets both after all its header lines.
	return toonLine(t) + scene.headerBefore + lookAt + scene.headerBetween + camera + scene.headerAfter + "WorldBegin\n";
}

std::vector<Transition> DataFilm::transitions() const {
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

std::string DataFilm::sceneAcross(double t) const {
	for (std::size_t k = 1; k < spec.shots.size(); ++k) {
		const Transition &tr = spec.shots[k].transition;
		const double cut = spec.shots[k].start;
		if (tr.length <= 0.0 || tr.kind == Transition::Fade || std::abs(t - cut) > 0.5 * tr.length) continue;
		std::string lookAt, camera;
		cameraAt(t, lookAt, camera, static_cast<int>(t < cut ? k : k - 1));
		return terminated(worldAt(toonLine(t) + (spec.shots.empty() ? scene.header : scene.headerBefore + lookAt + scene.headerBetween + camera + scene.headerAfter + "WorldBegin\n"), t));
	}
	return sceneAt(t);
}

std::string DataFilm::terminated(std::string text) const {
	if (spec.terminator <= 0.0) return text;
	char words[64];
	std::snprintf(words, sizeof words, " \"float terminator\" [ %g ]", spec.terminator);
	return withMaterialWords(text, words);
}

std::shared_ptr<const rigid::Run> DataFilm::rigidRun() const {
	// the characters push what is in their way: each a mover of its body and (a kind that says where its feet are) its two feet, sampled 120 times a second
	return rigid::runFor(spec.rigids, std::max(spec.length, spec.storyEnd) + 1.0, spec.version, [this] {
		std::vector<rigid::Kinematic> out;
		const double seconds = std::max(spec.length, spec.storyEnd) + 1.0;
		for (const Character &c : spec.characters) {
			const CharacterKind &kind = characterKinds().at(c.kind);
			rigid::Kinematic body, feet[2];
			body.radius = std::max(0.05, kind.radius * c.scale);
			for (rigid::Kinematic &f : feet) f.radius = std::max(0.03, 0.06 * kind.height * c.scale);
			for (double u = 0.0; u <= seconds; u += body.step) {
				const CharacterMoment m = momentOf(c, u);
				const auto pbrt = [](Vec3 p) { return Vec3{-p.x, p.y, p.z}; };   // (story space to pbrt's)
				body.at.push_back(pbrt(transformPoint(m.frame, Vec3{0.0, 0.5 * kind.height, 0.0} * c.scale)));
				for (int s = 0; s < 2; ++s)
					feet[s].at.push_back(kind.footAt ? pbrt(transformPoint(m.frame, kind.footAt(m.steps, s) * c.scale)) + Vec3{0.0, feet[s].radius, 0.0} : Vec3{0.0, -1e6, 0.0});
			}
			out.push_back(body);
			if (kind.footAt) out.push_back(feet[0]), out.push_back(feet[1]);
		}
		return out;
	}, spec.gravity);
}

std::string DataFilm::withMaterialWords(const std::string &text, const std::string &words) {
	// one pass, the text copied once (a frame is megabytes, with thousands of materials): after each `Material "TYPE"` and `MakeNamedMaterial "NAME"` - a statement, not the
	// end of a longer word (NamedMaterial, MakeNamedMaterial's own `Material "`) - the words, the parameters' order being free
	const std::string key = "Material \"";
	std::string out;
	out.reserve(text.size() + text.size() / 64);
	std::size_t copied = 0;
	for (std::size_t at = text.find(key); at != std::string::npos; at = text.find(key, at + 1)) {
		const bool plain = at == 0 || text[at - 1] == '\n' || text[at - 1] == ' ' || text[at - 1] == '\t';
		const bool made = at >= 9 && text.compare(at - 9, 9, "MakeNamed") == 0 && (at == 9 || text[at - 10] == '\n' || text[at - 10] == ' ' || text[at - 10] == '\t');
		if (!plain && !made) continue;
		const std::size_t close = text.find('"', at + key.size());   // (after its type, or its name)
		if (close == std::string::npos) break;
		out.append(text, copied, close + 1 - copied);
		out += words;
		copied = close + 1;
	}
	out.append(text, copied, std::string::npos);
	return out;
}

double DataFilm::acrossShotTime(double t) const {
	for (std::size_t k = 1; k < spec.shots.size(); ++k) {
		const Transition &tr = spec.shots[k].transition;
		const double cut = spec.shots[k].start;
		if (tr.length <= 0.0 || tr.kind == Transition::Fade || std::abs(t - cut) > 0.5 * tr.length) continue;
		return t < cut ? cut : spec.shots[k - 1].start;
	}
	return t;
}

double DataFilm::exposureAt(double t) const {
	if (!spec.exposureAuto || !spec.hasSky) return spec.exposure;
	double el = 0.0, facing = 0.0;
	dressing::sunAt(spec.sky, dressing::hourAt(spec.sky, t, spec.storyEnd), el, facing);
	return spec.exposure * film_camera::autoExposure(el);
}

std::string DataFilm::shotLightsAt(const std::string &header, double t) const {
	if (spec.shots.empty()) return std::string();
	const Shot &shot = spec.shots[shotAt(t)];
	if (!shot.lighting.on) return std::string();
	Vec3 eye, target;
	double fov = 40.0;
	if (!film_subjects::cameraOf(header, eye, target, fov)) return std::string();
	if (const Character *c = shot.lightOn.empty() ? nullptr : character(shot.lightOn)) target = chestOf(*c, t);
	return shot_lighting::lightsText(eye, target, shot.lighting);
}

double DataFilm::progress(const Anim &anim, double t, int component) const {
	const std::vector<Key> &keys = anim.keys;
	if (keys.empty()) return 0.0;
	if (t <= keys.front().t) return keys.front().v[component];
	if (t >= keys.back().t) return keys.back().v[component];
	for (std::size_t i = 1; i < keys.size(); ++i) {
		if (t <= keys[i].t) {
			const double span = keys[i].t - keys[i - 1].t;
			double u = span > 0.0 ? (t - keys[i - 1].t) / span : 1.0;
			if (!anim.ease.empty()) u = ease_curves::at(anim.ease, u);   // (its own curve, else the film's: ease_curves.h)
			else if (spec.ease) u = ease_curves::at(spec.easeCurve, u);
			return keys[i - 1].v[component] + (keys[i].v[component] - keys[i - 1].v[component]) * u;
		}
	}
	return keys.back().v[component];
}

bool DataFilm::visible(const std::string &name, double t) const {
	bool anyShow = false, shown = false;
	for (const Anim &a : spec.anims) {
		if (a.kind != Kind::Show || a.name != name) continue;
		anyShow = true;
		for (const auto &iv : a.intervals) shown = shown || (t >= iv.first && t <= iv.second);
	}
	return !anyShow || shown;
}

std::string DataFilm::num(double v) {
	char buf[40];
	std::snprintf(buf, sizeof(buf), "%.6g", v);
	return buf;
}

std::string DataFilm::transformsFor(const std::string &name, double t) const {
	const std::string open = transformsAt(name, t);
	if (spec.shutter <= 0.0) return open;
	const std::string close = transformsAt(name, t + spec.shutter / std::max(1, spec.fps));
	if (close == open) return open;
	return "  ActiveTransform \"StartTime\"\n" + open + "  ActiveTransform \"EndTime\"\n" + close + "  ActiveTransform \"All\"\n";
}

std::string DataFilm::transformsAt(const std::string &name, double t) const {
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
	for (std::size_t k = 0; k < spec.rigids.size(); ++k) {   // a body of the film's rigid-body world (rigid_world.h): where the world has it
		if (spec.rigids[k].block != name) continue;
		const std::vector<rigid::Pose> poses = rigid::posesAt(*rigidRun(), t);
		if (k < poses.size()) out += rigid::transformOf(spec.rigids[k], poses[k], &DataFilm::num);
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
			out += "  Translate " + num(progress(a, t, 0)) + " " + num(progress(a, t, 1)) + " " + num(progress(a, t, 2)) + "\n";
		} else if (a.kind == Kind::Turn) {
			out += "  Translate " + num(a.about[0]) + " " + num(a.about[1]) + " " + num(a.about[2]) + "\n  Rotate " + num(progress(a, t, 0)) + " " + num(a.axis[0]) + " " + num(a.axis[1]) + " " +
				   num(a.axis[2]) + "\n  Translate " + num(-a.about[0]) + " " + num(-a.about[1]) + " " + num(-a.about[2]) + "\n";
		} else if (a.kind == Kind::Grow) {
			const double k = std::max(1e-6, progress(a, t, 0));
			out += "  Translate " + num(a.about[0]) + " " + num(a.about[1]) + " " + num(a.about[2]) + "\n  Scale " + num(k) + " " + num(k) + " " + num(k) + "\n  Translate " + num(-a.about[0]) + " " +
				   num(-a.about[1]) + " " + num(-a.about[2]) + "\n";
		}
	}
	return out;
}

std::string DataFilm::lightsScaled(const std::string &name, const std::string &body, double t) const {
	double k = 1.0;
	bool any = false;
	for (const Anim &a : spec.anims)
		if (a.kind == Kind::Light && a.name == name) {
			k *= std::max(0.0, progress(a, t, 0));
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

camera_keyframes::Key DataFilm::moveKey(const Shot &shot, double t, double span) const {
	const Shot::Move &mv = shot.move;
	Vec3 feet, front, chest;   // (a character, or a herd or a flock)
	subjectFrame(mv.who, t, feet, front, chest);
	const Vec3 up{0.0, 1.0, 0.0}, right = cross(front, up);
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

void DataFilm::cameraAt(double t, std::string &lookAt, std::string &camera, int shotIndex) const {
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
		Vec3 feet, forward, target;   // (a character, or a herd or a flock)
		subjectFrame(shot.follow.who, t, feet, forward, target);
		const Vec3 up{0.0, 1.0, 0.0}, right = cross(forward, up);
		const Vec3 eye = feet - forward * shot.follow.behind + right * shot.follow.side + up * shot.follow.up;
		key.pos[0] = -eye.x;
		key.pos[1] = eye.y;
		key.pos[2] = eye.z;
		key.target[0] = -target.x;
		key.target[1] = target.y;
		key.target[2] = target.z;
		key.fov = shot.follow.fov;
	} else if (shot.spaceCam.kind != cosmos::SpaceCam::None) {   // a journey in space (film_voyage.h)
		key = cosmos::spaceCamKey(shot.spaceCam, (t - shot.start) / span);
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
	if (shot.divecam > 0.0) {   // a diver's camera (round 15): rising on each breath in and sinking as it goes out, the view tipping a little with it, a slow sway
		const double a = shot.divecam, breath = std::sin(2.0 * kPi * t / diver::kBreath), sway = std::sin(0.43 * t + 1.1) + 0.5 * std::sin(1.07 * t);
		key.pos[1] += 0.06 * a * breath, key.pos[0] += 0.03 * a * sway, key.pos[2] += 0.02 * a * std::sin(0.31 * t);
		key.target[1] += 0.1 * a * breath, key.target[0] += 0.05 * a * sway;
	}
	double quaking = 0.0;   // an earthquake: the camera shaken, fast and jolting
	for (const geology::Quake &q : spec.quakes) quaking = std::max(quaking, q.levelAt(t));
	if (quaking > 0.0) {
		const double jx = std::sin(41.0 * t) + 0.6 * std::sin(67.0 * t + 1.3) + 0.4 * std::sin(13.0 * t), jy = std::sin(53.0 * t + 0.7) + 0.5 * std::sin(29.0 * t + 2.0);
		const double jz = std::sin(47.0 * t + 2.1) + 0.5 * std::sin(17.0 * t);
		key.pos[0] += 0.05 * quaking * jx, key.pos[1] += 0.04 * quaking * jy, key.pos[2] += 0.05 * quaking * jz;
		key.target[0] += 0.12 * quaking * jy, key.target[1] += 0.10 * quaking * jx;
	}
	if (!shot.focus.empty() || shot.aperture > 0.0) {   // the focus on who the shot says (pulled from one to the next over 0.6 s), or on what the move is about
		const Vec3 eye{-key.pos[0], key.pos[1], key.pos[2]};
		const auto distanceTo = [&](const std::string &who) {
			const Character *c = character(who);
			Vec3 feet, front, chest;
			return c ? length(bodyPoint(*c, t, characterKinds().at(c->kind).head) - eye) : wildSubject(who, t, feet, front, chest) ? length(chest - eye) : 0.0;
		};
		double d = 0.0;
		if (!shot.focus.empty()) {
			d = distanceTo(shot.focus.front().second);
			for (std::size_t f = 1; f < shot.focus.size(); ++f) d += (distanceTo(shot.focus[f].second) - d) * smoothstep(shot.focus[f].first, shot.focus[f].first + 0.6, t);
		} else if (!shot.move.who.empty()) {
			d = distanceTo(shot.move.who);
		} else if (shot.following && !shot.follow.who.empty()) {   // (an `aperture` alone: on who the camera follows, else on what it looks at - it was dropped)
			d = distanceTo(shot.follow.who);
		}
		if (!(d > 0.0)) d = length(Vec3{key.target[0] - key.pos[0], key.target[1] - key.pos[1], key.target[2] - key.pos[2]});
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
	if (lens > 0.0 && shot.blades >= 3) camera += " \"integer blades\" [ " + std::to_string(shot.blades) + " ] \"float bladesrotation\" [ " + num(shot.bladesRotation) + " ]";   // (aperture_shape.h)
	if (lens > 0.0 && shot.apertureRatio != 1.0) camera += " \"float apertureratio\" [ " + num(shot.apertureRatio) + " ]";
	camera += "\n";
}

}  // namespace film_file
}  // namespace cartoon
