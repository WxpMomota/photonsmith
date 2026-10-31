#pragma once
// film_parse_sea.h -- the lines of a film file for under the sea, round 14 (its grammar: film_parse.h): the water round the camera (`underwater`), its flow (`current`),
// bubbles and haloclines (film_undersea.h), the deep's animals, octopus, cuttlefish and the open water's swimmers (film_deepsea.h), the seabed's life and its places
// (film_seabed.h). natureLine hands them here.

#include "film_parse_space.h"   // (and film_spec.h)

namespace cartoon {
namespace film_file {

inline bool underseaVerb(const std::string &verb) {
	static const std::vector<std::string> k = {"underwater", "waterline", "current", "bubbles", "halocline", "deepsea", "octopus", "cuttlefish", "swimmers", "seabed", "wreck", "arch", "cenote", "reefwall",
											   "diver", "divecam", "school", "bigfish", "reef", "critters", "nightdive", "kelpforest", "boat", "sardinerun", "vents", "murk", "silt",
											   "cleaningstation", "playmate", "damselfish", "wreckroom"};
	return std::find(k.begin(), k.end(), verb) != k.end();
}
// A barracuda tornado's line (`tornado N at X Y Z ...`, round 16) - not the wind's (`tornado from X Z to X Z ...`, film_wind.h), which shares the word.
inline bool barracudaTornado(const std::string &verb, const std::string &line) {
	const std::vector<std::string> t = detail::tokens(line);
	return verb == "tornado" && t.size() > 2 && t[2] == "at";
}
// Round 15's lines (`diver`, `divecam`, `school`, `bigfish`, `reef`, `critters`, `nightdive`, `kelpforest`) into `s`: "" or what is wrong with it.
inline std::string underseaLine15(Spec &s, const std::string &verb, const std::string &line) {
	using detail::number;
	const auto fail = [](const std::string &what) { return what; };
	const std::vector<std::string> t = detail::tokens(line);
	std::vector<double> v;
	std::size_t i = 1;
	const auto numbers = [&](std::size_t n) {   // (the next n numbers, or false)
		v.assign(n, 0.0);
		for (std::size_t k = 0; k < n; ++k)
			if (i + k >= t.size() || !number(t[i + k], v[k])) return false;
		i += n;
		return true;
	};
	const auto oneOf = [](const std::string &w, const std::vector<std::string> &kinds) { return std::find(kinds.begin(), kinds.end(), w) != kinds.end(); };
	if (verb == "divecam") {
		double a = 1.0;
		if (s.shots.empty() || t.size() > 2 || (t.size() == 2 && (!number(t[1], a) || a < 0.0 || a > 4.0)))
			return fail("`divecam [A]` belongs to a shot (start one with `shot 0 NAME` first): a diver's camera, rising and sinking with each breath (A 0 to 4, 1 by default)");
		s.shots.back().divecam = a;
		return std::string();
	}
	if (verb == "diver") {
		const char *usage = "`diver NAME from X Y Z to X Y Z [via X Y Z]... [at T] [speed S] [lamp] [ok T]... [suit black|blue|red] [seed K]`";
		diver::Diver d;
		if (t.size() < 2) return fail(usage);
		d.name = t[i++];
		Vec3 from, to;
		bool hasFrom = false, hasTo = false;
		std::vector<Vec3> via;
		while (i < t.size()) {
			const std::string w = t[i++];
			if (w == "lamp") {
				d.lamp = true;
				continue;
			}
			if (w == "suit" && i < t.size() && oneOf(t[i], diver::suits())) {
				d.suit = t[i++];
				continue;
			}
			if (w == "from" || w == "to" || w == "via") {
				if (!numbers(3)) return fail(usage);
				const Vec3 p{v[0], v[1], v[2]};
				if (w == "from") from = p, hasFrom = true;
				else if (w == "to") to = p, hasTo = true;
				else if (via.size() < 32) via.push_back(p);
				continue;
			}
			if (!numbers(1)) return fail(usage);
			if (w == "at") d.at = v[0];
			else if (w == "speed" && v[0] > 0.0 && v[0] <= 3.0) d.speed = v[0];
			else if (w == "ok" && d.oks.size() < 16) d.oks.push_back(v[0]);
			else if (w == "seed") d.seed = static_cast<int>(v[0]);
			else return fail(usage);
		}
		if (!hasFrom || !hasTo) return fail(std::string(usage) + ": where the diver starts and where they go");
		d.path.push_back(from);
		d.path.insert(d.path.end(), via.begin(), via.end());
		d.path.push_back(to);
		s.divers.push_back(d);
		return std::string();
	}
	if (verb == "school" || verb == "bigfish") {
		const std::string usage = verb == "school" ? "`school sardines|anchovies|jacks|barracuda N at X Y Z [radius R] [strike T]... [hunter sharks|dolphins|sealions] [seed K]` (1 to 3000)"
												   : "`bigfish whaleshark|hammerheads|reefsharks|dolphins|humpbacks N at X Y Z [radius R] [seed K]` (1 to 20)";
		school::School sc;
		school::BigFish bf;
		if (i >= t.size() || !oneOf(t[i], verb == "school" ? school::kinds() : school::bigKinds())) return fail(usage);
		sc.kind = bf.kind = t[i++];
		if (!numbers(1) || v[0] < 1 || v[0] > (verb == "school" ? 3000 : 20)) return fail(usage);
		sc.count = bf.count = static_cast<int>(v[0]);
		if (i >= t.size() || t[i++] != "at" || !numbers(3)) return fail(usage);
		sc.at = bf.at = {v[0], v[1], v[2]};
		while (i < t.size()) {
			const std::string w = t[i++];
			if (verb == "school" && w == "hunter" && i < t.size() && (t[i] == "sharks" || t[i] == "dolphins" || t[i] == "sealions")) {
				sc.hunter = t[i++];
				continue;
			}
			if (!numbers(1)) return fail(usage);
			if (w == "radius" && v[0] > 0.3 && v[0] <= 200.0) sc.radius = bf.radius = v[0];
			else if (w == "strike" && verb == "school" && sc.strikes.size() < 32) sc.strikes.push_back(v[0]);
			else if (w == "seed") sc.seed = bf.seed = static_cast<int>(v[0]);
			else return fail(usage);
		}
		if (verb == "school") s.schools.push_back(sc);
		else s.bigFish.push_back(bf);
		return std::string();
	}
	if (verb == "reef" || verb == "critters" || verb == "kelpforest") {
		const std::string usage = verb == "reef" ? "`reef N in X0 Z0 X1 Z1 [bleached] [fluoresce] [seed K]` (1 to 2000)"
								  : verb == "critters" ? "`critters nudibranchs|seahorses|shrimp|mantisshrimp N in X0 Z0 X1 Z1 [seed K]` (1 to 200)"
													   : "`kelpforest N in X0 Z0 X1 Z1 [seed K]` (1 to 500)";
		reef::Reef rf;
		reef::Critters cr;
		reef::KelpForest kf;
		if (verb == "critters") {
			if (i >= t.size() || !oneOf(t[i], reef::critterKinds())) return fail(usage);
			cr.kind = t[i++];
		}
		const double most = verb == "reef" ? 2000 : verb == "critters" ? 200 : 500;
		if (!numbers(1) || v[0] < 1 || v[0] > most) return fail(usage);
		rf.count = cr.count = kf.count = static_cast<int>(v[0]);
		if (i >= t.size() || t[i++] != "in" || !numbers(4)) return fail(usage);
		rf.x0 = cr.x0 = kf.x0 = std::min(v[0], v[2]), rf.z0 = cr.z0 = kf.z0 = std::min(v[1], v[3]);
		rf.x1 = cr.x1 = kf.x1 = std::max(v[0], v[2]), rf.z1 = cr.z1 = kf.z1 = std::max(v[1], v[3]);
		while (i < t.size()) {
			const std::string w = t[i++];
			if (verb == "reef" && (w == "bleached" || w == "fluoresce")) {
				(w == "bleached" ? rf.bleached : rf.fluoresce) = true;
				continue;
			}
			if (w != "seed" || !numbers(1)) return fail(usage);
			rf.seed = cr.seed = kf.seed = static_cast<int>(v[0]);
		}
		if (verb == "reef") s.reefLife.reefs.push_back(rf);
		else if (verb == "critters") s.reefLife.critters.push_back(cr);
		else s.reefLife.kelp.push_back(kf);
		return std::string();
	}
	if (verb == "nightdive") {
		const char *usage = "`nightdive [glow G] [spawn T0 T1]`: night under the sea - the water lights up round what moves (G 0 to 10), the corals spawn from T0 to T1";
		reef::Night &n = s.reefLife.night;
		n.on = true;
		while (i < t.size()) {
			const std::string w = t[i++];
			if (w == "glow" && numbers(1) && v[0] >= 0.0 && v[0] <= 10.0) n.glow = v[0];
			else if (w == "spawn" && numbers(2) && v[1] > v[0]) n.spawn0 = v[0], n.spawn1 = v[1];
			else return fail(usage);
		}
		return std::string();
	}
	return fail("`" + verb + "`: not a line of round 15");
}
// Round 16's lines (`boat`, `sardinerun`, `vents`, `murk`, `silt`, `cleaningstation`, `tornado`, `playmate`, `damselfish`) into `s`: "" or what is wrong with it.
inline std::string underseaLine16(Spec &s, const std::string &verb, const std::string &line) {
	using detail::number;
	const auto fail = [](const std::string &what) { return what; };
	const std::vector<std::string> t = detail::tokens(line);
	std::vector<double> v;
	std::size_t i = 1;
	const auto numbers = [&](std::size_t n) {   // (the next n numbers, or false)
		v.assign(n, 0.0);
		for (std::size_t k = 0; k < n; ++k)
			if (i + k >= t.size() || !number(t[i + k], v[k])) return false;
		i += n;
		return true;
	};
	if (verb == "boat") {
		const char *usage = "`boat X Z [toward A] [length L] [speed S] [during T0 T1] [anchor T] [seed K]` (L 3 to 60 m, S up to 15 m/s)";
		boat::Boat b;
		if (!numbers(2)) return fail(usage);
		b.x = v[0], b.z = v[1];
		while (i < t.size()) {
			const std::string w = t[i++];
			if (w == "during" && numbers(2) && v[1] > v[0]) {
				b.t0 = v[0], b.t1 = v[1];
				continue;
			}
			if (w == "during" || !numbers(1)) return fail(usage);
			if (w == "toward") b.toward = v[0];
			else if (w == "length" && v[0] >= 3.0 && v[0] <= 60.0) b.length = v[0];
			else if (w == "speed" && v[0] >= 0.0 && v[0] <= 15.0) b.speed = v[0];
			else if (w == "anchor") b.anchor = v[0];
			else if (w == "seed") b.seed = static_cast<int>(v[0]);
			else return fail(usage);
		}
		s.boats.push_back(b);
		return std::string();
	}
	if (verb == "sardinerun") {
		const char *usage = "`sardinerun X Y Z [radius R] [gannets N] [dolphins N] [from T] [seed K]` (gannets 0 to 60, dolphins 0 to 20)";
		boat::Run r;
		if (!numbers(3)) return fail(usage);
		r.at = {v[0], v[1], v[2]};
		while (i < t.size()) {
			const std::string w = t[i++];
			if (!numbers(1)) return fail(usage);
			if (w == "radius" && v[0] > 0.3 && v[0] <= 30.0) r.radius = v[0];
			else if (w == "gannets" && v[0] >= 0 && v[0] <= 60) r.gannets = static_cast<int>(v[0]);
			else if (w == "dolphins" && v[0] >= 0 && v[0] <= 20) r.dolphins = static_cast<int>(v[0]);
			else if (w == "from") r.from = v[0];
			else if (w == "seed") r.seed = static_cast<int>(v[0]);
			else return fail(usage);
		}
		s.sardineRuns.push_back(r);
		return std::string();
	}
	if (verb == "vents") {
		const char *usage = "`vents N in X0 Z0 X1 Z1 [seed K]` (1 to 30)";
		vents::Vents vs;
		if (!numbers(1) || v[0] < 1 || v[0] > 30) return fail(usage);
		vs.count = static_cast<int>(v[0]);
		if (i >= t.size() || t[i++] != "in" || !numbers(4)) return fail(usage);
		vs.x0 = std::min(v[0], v[2]), vs.z0 = std::min(v[1], v[3]), vs.x1 = std::max(v[0], v[2]), vs.z1 = std::max(v[1], v[3]);
		if (i < t.size()) {
			if (t[i++] != "seed" || !numbers(1)) return fail(usage);
			vs.seed = static_cast<int>(v[0]);
		}
		if (i < t.size()) return fail(usage);
		s.vents.push_back(vs);
		return std::string();
	}
	if (verb == "murk") {
		const char *usage = "`murk X Y Z [radius R] [green|grey] [density D]`: a ball of murky water (R 0.5 to 200 m, D 0.05 to 20)";
		undersea::Murk m;
		if (!numbers(3)) return fail(usage);
		m.at = {v[0], v[1], v[2]};
		while (i < t.size()) {
			const std::string w = t[i++];
			if (w == "green" || w == "grey") {
				m.colour = w;
				continue;
			}
			if (!numbers(1)) return fail(usage);
			if (w == "radius" && v[0] >= 0.5 && v[0] <= 200.0) m.radius = v[0];
			else if (w == "density" && v[0] >= 0.05 && v[0] <= 20.0) m.density = v[0];
			else return fail(usage);
		}
		s.murks.push_back(m);
		return std::string();
	}
	if (verb == "silt") {
		const char *usage = "`silt X Z at T [size S]`: a puff of silt kicked up from the seabed at T (S 0.1 to 3)";
		undersea::Silt sl;
		if (!numbers(2)) return fail(usage);
		sl.x = v[0], sl.z = v[1];
		bool timed = false;
		while (i < t.size()) {
			const std::string w = t[i++];
			if (!numbers(1)) return fail(usage);
			if (w == "at") sl.at = v[0], timed = true;
			else if (w == "size" && v[0] >= 0.1 && v[0] <= 3.0) sl.size = v[0];
			else return fail(usage);
		}
		if (!timed) return fail(usage);
		s.silts.push_back(sl);
		return std::string();
	}
	// the set pieces of behaviour (film_behaviour.h)
	const std::string usage = verb == "cleaningstation" ? "`cleaningstation X Y Z [seed K]`"
							  : verb == "tornado" ? "`tornado N at X Y Z [around NAME] [radius R] [height H] [seed K]` (10 to 600)"
							  : verb == "playmate" ? "`playmate X Y Z [with NAME] [seed K]`"
												   : "`damselfish N at X Y Z [radius R] [seed K]` (1 to 40)";
	behaviour::Station st;
	behaviour::Tornado tn;
	behaviour::Playmate pm;
	behaviour::Damsels dm;
	if (verb == "tornado" || verb == "damselfish") {
		if (!numbers(1) || v[0] < (verb == "tornado" ? 10 : 1) || v[0] > (verb == "tornado" ? 600 : 40)) return fail(usage);
		tn.count = dm.count = static_cast<int>(v[0]);
		if (i >= t.size() || t[i++] != "at") return fail(usage);
	}
	if (!numbers(3)) return fail(usage);
	st.at = tn.at = pm.at = dm.at = {v[0], v[1], v[2]};
	while (i < t.size()) {
		const std::string w = t[i++];
		if ((w == "around" && verb == "tornado") || (w == "with" && verb == "playmate")) {
			if (i >= t.size()) return fail(usage);
			(verb == "tornado" ? tn.around : pm.with) = t[i++];
			continue;
		}
		if (!numbers(1)) return fail(usage);
		if (w == "seed") st.seed = tn.seed = pm.seed = dm.seed = static_cast<int>(v[0]);
		else if (w == "radius" && (verb == "tornado" || verb == "damselfish") && v[0] > 0.2 && v[0] <= 30.0) tn.radius = dm.radius = v[0];
		else if (w == "height" && verb == "tornado" && v[0] > 0.5 && v[0] <= 60.0) tn.height = v[0];
		else return fail(usage);
	}
	if (verb == "cleaningstation") s.behaviours.stations.push_back(st);
	else if (verb == "tornado") s.behaviours.tornadoes.push_back(tn);
	else if (verb == "playmate") s.behaviours.playmates.push_back(pm);
	else s.behaviours.damsels.push_back(dm);
	return std::string();
}
// One of those lines into `s`: "" or what is wrong with it.
inline std::string underseaLine(Spec &s, const std::string &verb, const std::string &line) {
	using detail::number;
	const auto fail = [](const std::string &what) { return what; };
	const std::vector<std::string> t = detail::tokens(line);
	std::vector<double> v;
	std::size_t i = 1;
	const auto numbers = [&](std::size_t n) {   // (the next n numbers, or false)
		v.assign(n, 0.0);
		for (std::size_t k = 0; k < n; ++k)
			if (i + k >= t.size() || !number(t[i + k], v[k])) return false;
		i += n;
		return true;
	};
	const auto oneOf = [](const std::string &w, const std::vector<std::string> &kinds) { return std::find(kinds.begin(), kinds.end(), w) != kinds.end(); };
	if (verb == "diver" || verb == "divecam" || verb == "school" || verb == "bigfish" || verb == "reef" || verb == "critters" || verb == "nightdive" || verb == "kelpforest")
		return underseaLine15(s, verb, line);
	if (verb == "boat" || verb == "sardinerun" || verb == "vents" || verb == "murk" || verb == "silt" || verb == "cleaningstation" || verb == "tornado" || verb == "playmate" ||
		verb == "damselfish")
		return underseaLine16(s, verb, line);
	if (verb == "underwater") {
		const char *usage = "`underwater [clarity C] [colour blue|green|teal] [lamp L] [ripples]`: the camera is under the film's `water` (clarity 1 a clear sea, 2 tropical, 0.5 murky; "
							"lamp: a diver's lamp on the camera, 1 a strong one - `blue` makes it blue; ripples: the sand in ripples; `port flat|dome`, `filter red|magenta`: the camera's housing)";
		while (i < t.size()) {
			const std::string w = t[i++];
			if (w == "ripples" || w == "blue") {   // (blue: the lamp's, round 15)
				(w == "ripples" ? s.underwater.ripples : s.underwater.lampBlue) = true;
				continue;
			}
			if ((w == "port" && i < t.size() && (t[i] == "flat" || t[i] == "dome")) || (w == "filter" && i < t.size() && (t[i] == "red" || t[i] == "magenta"))) {   // (round 16)
				(w == "port" ? s.underwater.port : s.underwater.filter) = t[i++];
				continue;
			}
			if (w == "colour" && i < t.size() && oneOf(t[i], underwater::colours())) {
				s.underwater.colour = t[i++];
				continue;
			}
			if (!numbers(1)) return fail(usage);
			if (w == "clarity" && v[0] > 0.05 && v[0] <= 10.0) s.underwater.clarity = v[0];
			else if (w == "lamp" && v[0] >= 0.0 && v[0] <= 20.0) s.underwater.lamp = v[0];
			else return fail(usage);
		}
		s.hasUnderwater = true;
		return std::string();
	}
	if (verb == "waterline") {
		if (s.shots.empty() || t.size() != 1) return fail("`waterline` belongs to a shot (start one with `shot 0 NAME` first): its camera floats at the water's surface, half above it");
		s.shots.back().waterline = true;
		return std::string();
	}
	if (verb == "current") {
		const char *usage = "`current SPEED toward A [surge P]`: the water's flow, SPEED metres a second (up to 3) towards A degrees (0 +z, 90 +x); surge: swinging back and forth every P seconds";
		if (!numbers(1) || v[0] < 0.0 || v[0] > 3.0) return fail(usage);
		s.current.on = true, s.current.speed = v[0];
		while (i < t.size()) {
			const std::string w = t[i++];
			if (!numbers(1)) return fail(usage);
			if (w == "toward") s.current.toward = v[0];
			else if (w == "surge" && v[0] >= 1.0 && v[0] <= 60.0) s.current.surge = v[0];
			else return fail(usage);
		}
		return std::string();
	}
	if (verb == "halocline") {
		if (!numbers(1)) return fail("`halocline Y [strength S]`: a boundary between two waters at height Y (pbrt's y, in the world): it shimmers, and mirrors at a glancing look");
		undersea::Layer l;
		l.y = v[0];
		if (i < t.size()) {
			if (t[i++] != "strength" || !numbers(1) || v[0] <= 0.0 || v[0] > 5.0) return fail("`halocline Y [strength S]` (S 0.1 to 5)");
			l.strength = v[0];
		}
		if (i < t.size()) return fail("`halocline Y [strength S]`");
		s.undersea.layers.push_back(l);
		return std::string();
	}
	if (verb == "bubbles") {
		const char *usage = "`bubbles stream X Y Z [rate R] [size S] [during T0 T1] [seed K]`, `bubbles breath X Y Z [every P] [seed K]`, `bubbles net X Z at T [radius R] [depth D] [seed K]` "
							"or `bubbles ring X Y Z at T`";
		if (i >= t.size()) return fail(usage);
		const std::string kind = t[i++];
		if (kind == "net") {
			undersea::Net n;
			if (!numbers(2)) return fail(usage);
			n.x = v[0], n.z = v[1];
			bool timed = false;
			while (i < t.size()) {
				const std::string w = t[i++];
				if (!numbers(1)) return fail(usage);
				if (w == "at") n.at = v[0], timed = true;
				else if (w == "radius" && v[0] > 0.5 && v[0] <= 60.0) n.radius = v[0];
				else if (w == "depth" && v[0] > 0.5 && v[0] <= 200.0) n.depth = v[0];
				else if (w == "seed") n.seed = static_cast<int>(v[0]);
				else return fail(usage);
			}
			if (!timed) return fail("`bubbles net X Z at T`: when the whale starts blowing it");
			s.undersea.nets.push_back(n);
			return std::string();
		}
		if (kind != "stream" && kind != "breath" && kind != "ring") return fail(usage);
		if (!numbers(3)) return fail(usage);
		const Vec3 at{v[0], v[1], v[2]};
		undersea::Stream st;
		undersea::Breath br;
		undersea::Ring rg;
		st.at = br.at = rg.at = at;
		bool timed = false;
		while (i < t.size()) {
			const std::string w = t[i++];
			if (w == "during" && kind == "stream" && numbers(2) && v[1] > v[0]) {
				st.t0 = v[0], st.t1 = v[1];
				continue;
			}
			if (!numbers(1)) return fail(usage);
			if (w == "rate" && kind == "stream" && v[0] > 0.0 && v[0] <= 200.0) st.rate = v[0];
			else if (w == "size" && kind == "stream" && v[0] > 0.0 && v[0] <= 10.0) st.size = v[0];
			else if (w == "every" && kind == "breath" && v[0] >= 1.5 && v[0] <= 60.0) br.every = v[0];
			else if (w == "at" && kind == "ring") rg.t = v[0], timed = true;
			else if (w == "seed") st.seed = br.seed = rg.seed = static_cast<int>(v[0]);
			else return fail(usage);
		}
		if (kind == "stream") s.undersea.streams.push_back(st);
		else if (kind == "breath") s.undersea.breaths.push_back(br);
		else if (!timed) return fail("`bubbles ring X Y Z at T`: when it is blown");
		else s.undersea.rings.push_back(rg);
		return std::string();
	}
	if (verb == "deepsea") {
		const char *usage = "`deepsea anglerfish|lanternfish|combjellies|siphonophores N in X0 Y0 Z0 X1 Y1 Z1 [seed K]` (1 to 300), or `deepsea sparks`";
		if (t.size() == 2 && t[1] == "sparks") {
			s.deep.sparks = true;
			return std::string();
		}
		deepsea::Group g;
		if (i >= t.size() || !oneOf(t[i], deepsea::deepKinds())) return fail(usage);
		g.kind = t[i++];
		if (!numbers(1) || v[0] < 1 || v[0] > 300) return fail(usage);
		g.count = static_cast<int>(v[0]);
		if (i >= t.size() || t[i++] != "in" || !numbers(6)) return fail(usage);
		g.lo = {std::min(v[0], v[3]), std::min(v[1], v[4]), std::min(v[2], v[5])}, g.hi = {std::max(v[0], v[3]), std::max(v[1], v[4]), std::max(v[2], v[5])};
		if (i < t.size()) {
			if (t[i++] != "seed" || !numbers(1)) return fail(usage);
			g.seed = static_cast<int>(v[0]);
		}
		if (i < t.size()) return fail(usage);
		s.deep.groups.push_back(g);
		return std::string();
	}
	if (verb == "octopus") {
		const char *usage = "`octopus X Y Z [toward A] [size S] [jet T]... [ink T]... [hide T] [seed K]`";
		deepsea::Octopus o;
		if (!numbers(3)) return fail(usage);
		o.at = {v[0], v[1], v[2]};
		while (i < t.size()) {
			const std::string w = t[i++];
			if (!numbers(1)) return fail(usage);
			if (w == "toward") o.toward = v[0];
			else if (w == "size" && v[0] > 0.1 && v[0] <= 10.0) o.size = v[0];
			else if (w == "jet" && o.jets.size() < 16) o.jets.push_back(v[0]);
			else if (w == "ink" && o.inks.size() < 8) o.inks.push_back(v[0]);
			else if (w == "hide") o.hide = v[0];
			else if (w == "seed") o.seed = static_cast<int>(v[0]);
			else return fail(usage);
		}
		std::sort(o.jets.begin(), o.jets.end());
		s.deep.octopuses.push_back(o);
		return std::string();
	}
	if (verb == "cuttlefish" || verb == "swimmers") {
		const std::string usage = verb == "cuttlefish" ? "`cuttlefish N at X Y Z [radius R] [seed K]` (1 to 40)"
													   : "`swimmers mantas|eaglerays|stingrays|sealions|penguins|otters N at X Y Z [radius R] [rise T] [seed K]` (1 to 60)";
		deepsea::Swimmers sw;
		deepsea::Cuttle cu;
		if (verb == "swimmers") {
			if (i >= t.size() || !oneOf(t[i], deepsea::swimmerKinds())) return fail(usage);
			sw.kind = t[i++];
		}
		if (!numbers(1) || v[0] < 1 || v[0] > (verb == "cuttlefish" ? 40 : 60)) return fail(usage);
		sw.count = cu.count = static_cast<int>(v[0]);
		if (i >= t.size() || t[i++] != "at" || !numbers(3)) return fail(usage);
		sw.at = cu.at = {v[0], v[1], v[2]};
		while (i < t.size()) {
			const std::string w = t[i++];
			if (!numbers(1)) return fail(usage);
			if (w == "radius" && v[0] > 0.1 && v[0] <= 500.0) sw.radius = cu.radius = v[0];
			else if (w == "rise" && verb == "swimmers" && sw.kind == "stingrays") sw.rise = v[0];
			else if (w == "seed") sw.seed = cu.seed = static_cast<int>(v[0]);
			else return fail(usage);
		}
		if (verb == "cuttlefish") s.deep.cuttles.push_back(cu);
		else s.deep.swimmers.push_back(sw);
		return std::string();
	}
	if (verb == "seabed") {
		const char *usage = "`seabed anemones|seagrass|starfish|urchins|crabs|gardeneels N in X0 Z0 X1 Z1 [seed K]` (1 to 2000)";
		seabed::Patch p;
		if (i >= t.size() || !oneOf(t[i], seabed::kinds())) return fail(usage);
		p.kind = t[i++];
		if (!numbers(1) || v[0] < 1 || v[0] > 2000) return fail(usage);
		p.count = static_cast<int>(v[0]);
		if (i >= t.size() || t[i++] != "in" || !numbers(4)) return fail(usage);
		p.x0 = std::min(v[0], v[2]), p.z0 = std::min(v[1], v[3]), p.x1 = std::max(v[0], v[2]), p.z1 = std::max(v[1], v[3]);
		if (i < t.size()) {
			if (t[i++] != "seed" || !numbers(1)) return fail(usage);
			p.seed = static_cast<int>(v[0]);
		}
		if (i < t.size()) return fail(usage);
		s.seabed.patches.push_back(p);
		return std::string();
	}
	// a place: wreck, arch, cenote, reefwall
	const std::string usage = verb == "wreck" ? "`wreck X Z [toward A] [length L] [list D]` (L 5 to 150 m)"
							  : verb == "arch" ? "`arch X Z [toward A] [size S]` (S 0.2 to 10)"
							  : verb == "cenote" ? "`cenote X Z [radius R] [depth D]` (R 3 to 60 m, D 5 to 120 m)"
							  : verb == "wreckroom" ? "`wreckroom X Z [toward A] [length L]` (inside a wreck: L 4 to 60 m)"
												 : "`reefwall X Z [toward A] [length L] [height H]` (L 4 to 200 m, H 2 to 120 m)";
	seabed::Place p;
	p.kind = verb;
	if (verb == "wreckroom") p.length = 12.0;
	if (!numbers(2)) return fail(usage);
	p.x = v[0], p.z = v[1];
	while (i < t.size()) {
		const std::string w = t[i++];
		if (!numbers(1)) return fail(usage);
		if (w == "toward" && verb != "cenote") p.toward = v[0];
		else if (w == "length" && verb == "wreck" && v[0] >= 5.0 && v[0] <= 150.0) p.length = v[0];
		else if (w == "length" && verb == "reefwall" && v[0] >= 4.0 && v[0] <= 200.0) p.length = v[0];
		else if (w == "length" && verb == "wreckroom" && v[0] >= 4.0 && v[0] <= 60.0) p.length = v[0];
		else if (w == "list" && verb == "wreck" && std::abs(v[0]) <= 80.0) p.list = v[0];
		else if (w == "size" && verb == "arch" && v[0] >= 0.2 && v[0] <= 10.0) p.size = v[0];
		else if (w == "radius" && verb == "cenote" && v[0] >= 3.0 && v[0] <= 60.0) p.radius = v[0];
		else if (w == "depth" && verb == "cenote" && v[0] >= 5.0 && v[0] <= 120.0) p.depth = v[0];
		else if (w == "height" && verb == "reefwall" && v[0] >= 2.0 && v[0] <= 120.0) p.height = v[0];
		else return fail(usage);
	}
	s.seabed.places.push_back(p);
	return std::string();
}

// What is wrong with a whole film's lines under the sea, or "": `underwater` needs a `water` line and no mist or fog; the lines of rounds 14 to 16 are drawn only for a camera under the water (without `underwater` they
// would be dropped without a word), and a barracuda tornado `around` a diver or a sea lion playing `with` one names a diver of the film.
inline std::string underseaProblems(const Spec &s) {
	if (s.hasUnderwater && s.seas.empty()) return "`underwater` puts the camera under the film's water: it needs a `water ... level Y` line";
	if (s.hasUnderwater && (s.hasMist || s.fog.on)) return std::string("`underwater` and `") + (s.hasMist ? "mist" : "fog") + "` both fill the air round the camera: use one";
	if (!s.hasUnderwater && (s.undersea.any() || s.deep.any() || s.seabed.any() || !s.divers.empty() || !s.schools.empty() || !s.bigFish.empty() || s.reefLife.any() || !s.boats.empty() ||
							 !s.sardineRuns.empty() || !s.vents.empty() || !s.murks.empty() || !s.silts.empty() || s.behaviours.any() ||
							 std::any_of(s.shots.begin(), s.shots.end(), [](const Shot &sh) { return sh.waterline; })))
		return "divers, schools, boats, vents, murk, the seabed's life and places, bubbles and `waterline` shots are under the sea: the film needs an `underwater` line (and a `water` one)";
	const auto aDiver = [&](const std::string &name) { return std::any_of(s.divers.begin(), s.divers.end(), [&](const diver::Diver &d) { return d.name == name; }); };
	for (const behaviour::Tornado &tn : s.behaviours.tornadoes)
		if (!tn.around.empty() && !aDiver(tn.around)) return "`tornado ... around " + tn.around + "`: there is no diver called " + tn.around + " (a `diver NAME ...` line; names are matched exactly)";
	for (const behaviour::Playmate &pm : s.behaviours.playmates)
		if (!pm.with.empty() && !aDiver(pm.with)) return "`playmate ... with " + pm.with + "`: there is no diver called " + pm.with + " (a `diver NAME ...` line; names are matched exactly)";
	return std::string();
}

}  // namespace film_file
}  // namespace cartoon
