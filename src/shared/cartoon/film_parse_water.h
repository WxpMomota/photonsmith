#pragma once
// film_parse_water.h -- the lines of a film file for life on the water and by it (its grammar: film_parse.h). natureLine hands them here.

#include "film_parse_space.h"   // (and film_spec.h)

namespace cartoon {
namespace film_file {

inline bool waterVerb(const std::string &verb, const std::string &line) {
	static const std::vector<std::string> k = {"ripple", "skipstone", "aboard", "adrift", "fallin", "climbout", "bankside", "watermist", "pondlife", "tide", "flood", "whirlpool", "bigwave", "bore", "sandcastle", "rockpool", "toonwater"};
	if (std::find(waterside::kinds().begin(), waterside::kinds().end(), verb) != waterside::kinds().end()) return true;
	if (verb == "boat") {   // (`boat X Z ...`, numbers first: round 16's, a hull seen from under the sea)
		const std::vector<std::string> t = detail::tokens(line);
		double v = 0.0;
		return t.size() > 1 && !detail::number(t[1], v);
	}
	return std::find(k.begin(), k.end(), verb) != k.end();
}

// The lines that change the water as the film goes.
inline std::string waterChangeLine(Spec &s, const std::string &verb, const std::vector<std::string> &t) {
	using detail::number;
	if (verb == "bore") {
		const char *usage = "`bore N at T [height H]`: a wall of water coming down river N";
		water::River::Bore b;
		double n = 0.0, v = 0.0;
		if (t.size() < 4 || !number(t[1], n) || t[2] != "at" || !number(t[3], b.t) || t.size() % 2 != 0) return usage;   // (a word left without its number is wrong, not dropped)
		for (std::size_t i = 4; i + 1 < t.size(); i += 2) {
			if (t[i] == "height" && number(t[i + 1], v) && v > 0.0 && v <= 12.0) b.height = v;
			else return usage;
		}
		if (n < 0.0 || n >= static_cast<double>(s.rivers.size())) return "`bore`: there is no river " + t[1] + " yet (they count from 0, and it must come after the river's line)";
		s.rivers[static_cast<std::size_t>(n)].bores.push_back(b);
		return std::string();
	}
	if (s.seas.empty()) return "`" + verb + "` is the film's water: put it after the `water` line";
	water::Sea &sea = s.seas[0];
	double v[4] = {0.0, 0.0, 0.0, 0.0};
	if (verb == "tide") {
		const char *usage = "`tide low L high H [period P] [from T]`: the water going down to L and up to H, every P seconds (120 by default)";
		sea.tidePeriod = 120.0;
		bool lowSet = false, highSet = false;
		if (t.size() % 2 == 0) return usage;
		for (std::size_t i = 1; i + 1 < t.size(); i += 2) {
			if (!number(t[i + 1], v[0])) return usage;
			if (t[i] == "low") sea.tideLow = v[0], lowSet = true;
			else if (t[i] == "high") sea.tideHigh = v[0], highSet = true;
			else if (t[i] == "period" && v[0] > 0.0) sea.tidePeriod = v[0];
			else if (t[i] == "from") sea.tideFrom = v[0];
			else return usage;
		}
		return lowSet && highSet ? std::string() : usage;
	}
	if (verb == "flood") {
		const char *usage = "`flood from Y to Y during T0 T1`: the water rising (or falling) from one height to another";
		if (t.size() != 8 || t[1] != "from" || t[3] != "to" || t[5] != "during" || !number(t[2], v[0]) || !number(t[4], v[1]) || !number(t[6], v[2]) || !number(t[7], v[3]) || v[3] <= v[2]) return usage;
		sea.floods.push_back({v[0], v[1], v[2], v[3]});
		return std::string();
	}
	const char *usage = verb == "whirlpool" ? "`whirlpool X Z [radius R] [depth D] [from T] [until T]`" : "`bigwave X Z toward A at T [height H] [length L] [speed S] [width W] [until T]`: at X Z at T";
	water::Sea::Bend b;
	if (t.size() < 3 || !number(t[1], b.x) || !number(t[2], b.z) || t.size() % 2 == 0) return usage;
	b.kind = verb == "whirlpool" ? 0 : 1;
	if (b.kind == 1) b.radius = 24.0, b.depth = 7.0;
	for (std::size_t i = 3; i + 1 < t.size(); i += 2) {
		if (!number(t[i + 1], v[0])) return usage;
		const std::string &w = t[i];
		if (w == "radius" || w == "length") b.radius = std::max(1.0, v[0]);
		else if (w == "depth" || w == "height") b.depth = std::max(0.1, v[0]);
		else if (w == "from" || w == "at") b.t0 = v[0];
		else if (w == "until") b.t1 = v[0];
		else if (w == "toward") b.facing = v[0];
		else if (w == "speed") b.speed = v[0];
		else if (w == "width") b.width = std::max(5.0, v[0]);
		else return usage;
	}
	sea.bends.push_back(b);
	return std::string();
}

// One of these lines into `s`: "" or what is wrong with it.
inline std::string waterLine(Spec &s, const std::string &verb, const std::string &line) {
	using detail::number;
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
	if (verb == "ripple") {
		const char *usage = "`ripple X Z at T [size S]`: something touches the water there (S how big it is, 0.01 to 2 m)";
		stir::Plop p;
		if (!numbers(2)) return usage;
		p.x = v[0], p.z = v[1];
		bool when = false;
		while (i < t.size()) {
			const std::string w = t[i++];
			if (!numbers(1)) return usage;
			if (w == "at") p.at = v[0], when = true;
			else if (w == "size" && v[0] >= 0.01 && v[0] <= 2.0) p.size = v[0];
			else return usage;
		}
		if (!when) return usage;
		s.plops.push_back(p);
		return std::string();
	}
	if (verb == "skipstone") {
		const char *usage = "`skipstone X Z toward A at T [speed S] [bounces N] [height H] [seed K]`: a flat stone thrown from there, H above the water (speed 3 to 20, bounces 1 to 16)";
		stir::Skip k;
		if (!numbers(2)) return usage;
		k.x = v[0], k.z = v[1];
		bool when = false;
		while (i < t.size()) {
			const std::string w = t[i++];
			if (!numbers(1)) return usage;
			if (w == "toward") k.toward = v[0];
			else if (w == "at") k.at = v[0], when = true;
			else if (w == "speed" && v[0] >= 3.0 && v[0] <= 20.0) k.speed = v[0];
			else if (w == "bounces" && v[0] >= 1.0 && v[0] <= 16.0) k.bounces = static_cast<int>(v[0]);
			else if (w == "height" && v[0] > 0.0 && v[0] <= 10.0) k.height = v[0];
			else if (w == "seed") k.seed = detail::seedOf(v[0]);
			else return usage;
		}
		if (!when) return usage;
		s.skips.push_back(k);
		return std::string();
	}
	if (verb == "boat") {
		const char *usage = "`boat rowboat|canoe|raft|sailboat|fishing|ferry NAME at X Z [to X Z]... [facing A] [speed S] [from T] [length L] [colour R G B] [seed K]`, or `... NAME river N [across C] ...` to go down a river";
		craft::Craft c;
		if (t.size() < 3 || std::find(craft::kinds().begin(), craft::kinds().end(), t[1]) == craft::kinds().end()) return usage;
		c.kind = t[1], c.name = t[2], i = 3;
		while (i < t.size()) {
			const std::string w = t[i++];
			if (w == "at" || w == "to") {
				if (!numbers(2) || (w == "at") != c.points.empty() || c.points.size() >= 64) return usage;
				c.points.push_back({v[0], v[1]});
				continue;
			}
			if (w == "colour") {
				if (!numbers(3)) return usage;
				c.colour = {v[0], v[1], v[2]};
				continue;
			}
			if (!numbers(1)) return usage;
			if (w == "river") c.river = static_cast<int>(v[0]);
			else if (w == "across") c.across = v[0];
			else if (w == "facing") c.facing = v[0];
			else if (w == "speed" && v[0] > 0.0 && v[0] <= 15.0) c.speed = v[0];
			else if (w == "from") c.from = v[0];
			else if (w == "length" && v[0] >= 1.5 && v[0] <= 80.0) c.length = v[0];
			else if (w == "seed") c.seed = detail::seedOf(v[0]);
			else return usage;
		}
		if (c.points.empty() && c.river < 0) return usage;
		craft::prepare(c);
		s.crafts.push_back(c);
		return std::string();
	}
	if (verb == "aboard") {
		const char *usage = "`aboard WHO BOAT [seat N] [rows|paddles]`: a character in a boat, sitting or standing as her kind has it (N 0 to 7)";
		Spec::Aboard a;
		if (t.size() < 3) return usage;
		a.who = t[1], a.boat = t[2], i = 3;
		while (i < t.size()) {
			const std::string w = t[i++];
			if (w == "rows" || w == "paddles") {
				a.pulls = true;
				continue;
			}
			if (w != "seat" || !numbers(1) || v[0] < 0.0 || v[0] > 7.0) return usage;
			a.seat = static_cast<int>(v[0]);
		}
		s.aboards.push_back(a);
		return std::string();
	}
	if (verb == "adrift") {
		const char *usage = "`adrift leaf|paperboat|log|bottle|duck|crate|lantern NAME at X Z [river N] [from T] [count N] [size S] [seed K]` (count 1 to 40)";
		adrift::Thing th;
		if (t.size() < 3 || std::find(adrift::kinds().begin(), adrift::kinds().end(), t[1]) == adrift::kinds().end()) return usage;
		th.kind = t[1], th.name = t[2], i = 3;
		bool placed = false;
		while (i < t.size()) {
			const std::string w = t[i++];
			if (w == "at") {
				if (!numbers(2)) return usage;
				th.x = v[0], th.z = v[1], placed = true;
				continue;
			}
			if (!numbers(1)) return usage;
			if (w == "river") th.river = static_cast<int>(v[0]);
			else if (w == "from") th.from = v[0];
			else if (w == "count" && v[0] >= 1.0 && v[0] <= 40.0) th.count = static_cast<int>(v[0]);
			else if (w == "size" && v[0] > 0.0 && v[0] <= 10.0) th.size = v[0];
			else if (w == "seed") th.seed = detail::seedOf(v[0]);
			else return usage;
		}
		if (!placed) return usage;
		s.adrifts.push_back(th);
		return std::string();
	}
	if (verb == "fallin" || verb == "climbout") {
		const char *usage = verb == "fallin" ? "`fallin WHO at T [to X Z]`: over the side and into the water, up again a moment later, treading water" : "`climbout WHO at T to X Y Z`: out of the water onto a bank or a pier there";
		Spec::Tumble f;
		if (t.size() < 2) return usage;
		f.who = t[1], f.out = verb == "climbout", i = 2;
		bool when = false;
		while (i < t.size()) {
			const std::string w = t[i++];
			if (w == "at" && numbers(1)) f.at = v[0], when = true;
			else if (w == "to" && numbers(f.out ? 3 : 2)) f.x = v[0], f.z = v[f.out ? 2 : 1], f.y = f.out ? v[1] : 0.0, f.placed = true;
			else return usage;
		}
		if (!when || (f.out && !f.placed)) return usage;
		s.tumbles.push_back(f);
		return std::string();
	}
	if (verb == "bankside") {
		const char *usage = "`bankside [reeds D] [lilies D] [duckweed D] [willows N] [mud W] [patch X Z R]... [seed K]`: D how many to the metre of shore";
		s.bank.on = true;
		while (i < t.size()) {
			const std::string w = t[i++];
			if (w == "patch") {
				if (!numbers(3) || v[2] <= 0.0 || v[2] > 200.0 || s.bank.patches.size() >= 64) return usage;
				s.bank.patches.push_back({v[0], v[1], v[2]});
				continue;
			}
			if (!numbers(1) || v[0] < 0.0) return usage;
			if (w == "reeds" && v[0] <= 20.0) s.bank.reeds = v[0];
			else if (w == "lilies" && v[0] <= 10.0) s.bank.lilies = v[0];
			else if (w == "duckweed" && v[0] <= 10.0) s.bank.duckweed = v[0];
			else if (w == "willows" && v[0] <= 16.0) s.bank.willows = static_cast<int>(v[0]);
			else if (w == "mud" && v[0] <= 8.0) s.bank.mud = v[0];
			else if (w == "seed") s.bank.seed = detail::seedOf(v[0]);
			else return usage;
		}
		return std::string();
	}
	if (verb == "watermist") {
		const char *usage = "`watermist A [during T0 T1]`: mist lifting off the water (A 0 to 3)";
		if (!numbers(1) || v[0] < 0.0 || v[0] > 3.0) return usage;
		s.waterMist.on = true, s.waterMist.amount = v[0];
		if (i < t.size()) {
			if (t[i++] != "during" || !numbers(2) || v[1] <= v[0]) return usage;
			s.waterMist.t0 = v[0], s.waterMist.t1 = v[1];
		}
		return std::string();
	}
	if (verb == "pondlife") {
		const char *usage = "`pondlife ducks|swans|heron|kingfisher|frogs|otter|beaver|trout|salmon [N] at X Z [radius R] [young N] [toward A] [height H] [strike|dive|slap T]... [seed K]`";
		pondlife::Life l;
		if (t.size() < 2 || std::find(pondlife::kinds().begin(), pondlife::kinds().end(), t[1]) == pondlife::kinds().end()) return usage;
		l.kind = t[1], i = 2;
		double n = 1.0;
		if (i < t.size() && number(t[i], n)) ++i;
		if (n < 1.0 || n > 40.0) return usage;
		l.count = static_cast<int>(n);
		bool placed = false;
		while (i < t.size()) {
			const std::string w = t[i++];
			if (w == "at") {
				if (!numbers(2)) return usage;
				l.x = v[0], l.z = v[1], placed = true;
				continue;
			}
			if (!numbers(1)) return usage;
			if (w == "radius" && v[0] > 0.0 && v[0] <= 200.0) l.radius = v[0];
			else if (w == "young" && v[0] >= 0.0 && v[0] <= 12.0) l.young = static_cast<int>(v[0]);
			else if (w == "toward") l.toward = v[0];
			else if (w == "height" && v[0] > 0.0 && v[0] <= 12.0) l.height = v[0];
			else if ((w == "strike" || w == "dive" || w == "slap") && l.times.size() < 64) l.times.push_back(v[0]);
			else if (w == "seed") l.seed = detail::seedOf(v[0]);
			else return usage;
		}
		if (!placed) return usage;
		s.lifes.push_back(l);
		return std::string();
	}
	if (verb == "tide" || verb == "flood" || verb == "whirlpool" || verb == "bigwave" || verb == "bore") return waterChangeLine(s, verb, t);
	if (verb == "sandcastle" || verb == "rockpool") {
		const char *usage = verb == "sandcastle" ? "`sandcastle X Z [size S]`" : "`rockpool X Z [radius R] [seed K]`";
		if (!numbers(2)) return usage;
		const double x = v[0], z = v[1];
		double size = verb == "sandcastle" ? 1.0 : 1.2;
		int seed = 1;
		while (i < t.size()) {
			const std::string w = t[i++];
			if (!numbers(1)) return usage;
			if ((w == "size" || w == "radius") && v[0] > 0.0 && v[0] <= 6.0) size = v[0];
			else if (w == "seed") seed = detail::seedOf(v[0]);
			else return usage;
		}
		if (verb == "sandcastle") s.castles.push_back({x, z, size});
		else s.pools.push_back({x, z, size, seed});
		return std::string();
	}
	if (verb == "toonwater") {
		const char *usage = "`toonwater off` or `toonwater [deep R G B] [mid R G B] [shallow R G B] [lines N] [sparkle N]`";
		if (t.size() == 2 && t[1] == "off") {
			s.paint.off = true;
			return std::string();
		}
		while (i < t.size()) {
			const std::string w = t[i++];
			if (w == "deep" || w == "mid" || w == "shallow") {
				if (!numbers(3)) return usage;
				(w == "deep" ? s.paint.deep : w == "mid" ? s.paint.mid : s.paint.shallow) = {v[0], v[1], v[2]};
				s.paint.coloured = true;
				continue;
			}
			if (!numbers(1) || v[0] < 0.0 || v[0] > 4.0) return usage;
			if (w == "lines") s.paint.lines = v[0];
			else if (w == "sparkle") s.paint.sparkle = v[0];
			else return usage;
		}
		return std::string();
	}
	if (std::find(waterside::kinds().begin(), waterside::kinds().end(), verb) != waterside::kinds().end()) {
		const std::string usage = "`" + verb + " X Z [to X Z] [facing|toward A] [length L] [height H] [count N] [radius R] [river N] [burst T] [seed K]`";
		waterside::Place p;
		p.kind = verb;
		if (verb == "stilthouses" && t.size() > 2 && t[2] == "at") {   // (`stilthouses N at X Z`: a count only when `at` follows it, or `stilthouses X Z` read X as one)
			if (!numbers(1) || v[0] < 1.0 || v[0] > 60.0) return usage + ": 1 to 60 huts";
			p.count = static_cast<int>(v[0]), ++i;
		}
		if (!numbers(2)) return usage;
		p.x = v[0], p.z = v[1], p.x1 = p.x, p.z1 = p.z;
		bool ended = false;
		while (i < t.size()) {
			const std::string w = t[i++];
			if (w == "to") {
				if (!numbers(2)) return usage;
				p.x1 = v[0], p.z1 = v[1], ended = true;
				continue;
			}
			if (!numbers(1)) return usage;
			if (w == "facing" || w == "toward") p.facing = v[0];
			else if (w == "length" && v[0] > 0.0 && v[0] <= 400.0) p.length = v[0];
			else if (w == "height" && v[0] > 0.0 && v[0] <= 120.0) p.height = v[0];
			else if ((w == "count" || w == "arches") && v[0] >= 1.0 && v[0] <= 60.0) p.count = static_cast<int>(v[0]);
			else if (w == "radius" && v[0] > 0.0 && v[0] <= 200.0) p.radius = v[0];
			else if (w == "river") p.river = static_cast<int>(v[0]);
			else if (w == "burst") p.burst = v[0];
			else if (w == "seed") p.seed = detail::seedOf(v[0]);
			else return usage;
		}
		if ((verb == "bridge" || verb == "steppingstones" || verb == "dam") && !ended) return usage + ": it goes from one place to another";
		s.places.push_back(p);
		return std::string();
	}
	return "`" + verb + "`?";
}

// With the land and the rivers known: rivers that have no land, boats and things on rivers, and who is aboard a boat sits on what she has to sit on.
inline void settleOnWater(Spec &s) {
	for (water::River &rv : s.rivers)
		if (rv.along.empty()) stream::prepare(rv, nullptr, s.seas.empty() ? -1e30 : s.seas[0].level);
	for (craft::Craft &c : s.crafts)
		if (c.river >= 0 && c.river < static_cast<int>(s.rivers.size())) craft::downRiver(c, s.rivers[static_cast<std::size_t>(c.river)]);
	for (adrift::Thing &th : s.adrifts) adrift::prepare(th, s.rivers, s.seas.empty() ? nullptr : &s.seas[0], std::max(600.0, 4.0 * s.length));
	if (s.bank.on) bankside::prepare(s.bank, s.seas, s.rivers, s.hasTerrain ? terrain::fieldOf(s.terrain).get() : nullptr);
	for (pondlife::Life &l : s.lifes) pondlife::prepare(l, std::max(60.0, s.length));
	for (const waterside::Place &p : s.places)   // a dam going sends a wall of water down its river
		if (p.kind == "dam" && p.burst >= 0.0 && p.river >= 0 && p.river < static_cast<int>(s.rivers.size())) {
			water::River &r = s.rivers[static_cast<std::size_t>(p.river)];
			double best = 1e30, from = 0.0;
			for (std::size_t q = 0; q < r.path.size(); ++q)
				if (const double d = std::hypot(r.path[q].x - 0.5 * (p.x + p.x1), r.path[q].z - 0.5 * (p.z + p.z1)); d < best) best = d, from = r.along[q];
			water::River::Bore b;
			b.t = p.burst, b.height = p.height > 0.0 ? 0.6 * p.height : 1.8, b.from = from;
			r.bores.push_back(b);
		}
	for (const Spec::Aboard &a : s.aboards)
		for (const craft::Craft &k : s.crafts) {
			if (k.name != a.boat) continue;
			const craft::Seat seat = craft::seatOf(craft::shapeOf(k.kind, k.length, k.colour), k.kind, a.seat, a.pulls && k.kind == "rowboat");
			for (Character &c : s.characters)
				if (c.name == a.who && seat.height > 0.0) {
					Character::Sit st;
					st.t0 = -10.0, st.height = seat.height / std::max(0.01, c.scale);
					for (const Spec::Tumble &f : s.tumbles)
						if (f.who == a.who && !f.out) st.t1 = std::min(st.t1, f.at - 0.7);
					c.sits.push_back(st);
				}
		}
}

// What is wrong with the film as a whole: "" for nothing.
inline std::string waterProblems(const Spec &s) {
	const bool anyWater = !s.seas.empty() || !s.rivers.empty();
	if ((!s.plops.empty() || !s.skips.empty()) && !anyWater) return "`ripple` and `skipstone` touch the water: the film needs a `water` or a `river` line";
	for (const craft::Craft &c : s.crafts) {
		if (c.river >= static_cast<int>(s.rivers.size())) return "boat " + c.name + ": there is no river " + std::to_string(c.river) + " (they count from 0)";
		if (c.river < 0 && s.seas.empty()) return "a boat floats on the water: the film needs a `water` line, or `river N` on her line";
	}
	for (const adrift::Thing &th : s.adrifts) {
		if (th.river >= static_cast<int>(s.rivers.size())) return "adrift " + th.name + ": there is no river " + std::to_string(th.river) + " (they count from 0)";
		if (th.river < 0 && s.seas.empty()) return "`adrift` floats on the water: the film needs a `water` line, or `river N` on its line";
	}
	for (const Spec::Tumble &f : s.tumbles)
		if (s.seas.empty()) return "`fallin` and `climbout` need a `water` line (" + f.who + ")";
	if ((s.bank.on || s.waterMist.on || !s.lifes.empty()) && !anyWater) return "`bankside`, `watermist` and `pondlife` are by the water: the film needs a `water` or a `river` line";
	if (s.bank.on && s.bank.patches.empty() && s.rivers.empty() && !s.hasTerrain) return "`bankside`: a lake with no land has no shore: give it a `terrain`, a `river` or `patch X Z R`";
	if ((!s.castles.empty() || !s.pools.empty()) && s.seas.empty()) return "`sandcastle` and `rockpool` are on a shore: the film needs a `water` line";
	if (!s.places.empty() && !anyWater) return "a `" + s.places[0].kind + "` is on the water: the film needs a `water` or a `river` line";
	for (const Spec::Aboard &a : s.aboards)   // (a boat on the water, or a sub under it: round 18, film_subs.h)
		if (std::none_of(s.crafts.begin(), s.crafts.end(), [&a](const craft::Craft &c) { return c.name == a.boat; }) &&
			std::none_of(s.subs.begin(), s.subs.end(), [&a](const subs::Sub &b) { return b.name == a.boat; }))
			return "`aboard`: there is no boat or sub called \"" + a.boat + "\"";
	return std::string();
}

}  // namespace film_file
}  // namespace cartoon
