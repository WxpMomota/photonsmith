#pragma once
// film_parse_sea.h -- the lines of a film file for under the sea, round 14 (its grammar: film_parse.h): the water round the camera (`underwater`), its flow (`current`),
// bubbles and haloclines (film_undersea.h), the deep's animals, octopus, cuttlefish and the open water's swimmers (film_deepsea.h), the seabed's life and its places
// (film_seabed.h). natureLine hands them here.

#include "film_parse_space.h"   // (and film_spec.h)

namespace cartoon {
namespace film_file {

inline bool underseaVerb(const std::string &verb) {
	static const std::vector<std::string> k = {"underwater", "waterline", "current", "bubbles", "halocline", "deepsea", "octopus", "cuttlefish", "swimmers", "seabed", "wreck", "arch", "cenote", "reefwall"};
	return std::find(k.begin(), k.end(), verb) != k.end();
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
	if (verb == "underwater") {
		const char *usage = "`underwater [clarity C] [colour blue|green|teal] [lamp L] [ripples]`: the camera is under the film's `water` (clarity 1 a clear sea, 2 tropical, 0.5 murky; "
							"lamp: a diver's lamp on the camera, 1 a strong one; ripples: the sand in ripples)";
		while (i < t.size()) {
			const std::string w = t[i++];
			if (w == "ripples") {
				s.underwater.ripples = true;
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
												 : "`reefwall X Z [toward A] [length L] [height H]` (L 4 to 200 m, H 2 to 120 m)";
	seabed::Place p;
	p.kind = verb;
	if (!numbers(2)) return fail(usage);
	p.x = v[0], p.z = v[1];
	while (i < t.size()) {
		const std::string w = t[i++];
		if (!numbers(1)) return fail(usage);
		if (w == "toward" && verb != "cenote") p.toward = v[0];
		else if (w == "length" && verb == "wreck" && v[0] >= 5.0 && v[0] <= 150.0) p.length = v[0];
		else if (w == "length" && verb == "reefwall" && v[0] >= 4.0 && v[0] <= 200.0) p.length = v[0];
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

}  // namespace film_file
}  // namespace cartoon
