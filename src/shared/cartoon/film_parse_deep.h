#pragma once
// film_parse_deep.h -- the lines of a film file for under the sea, round 18 (its grammar: film_parse.h): garments that flow, submarines, air under the water, towns, fast
// lanes and chases, schools that perform and the sea's songs, giants in the dark, things from above and the deep's places, drawn looks. natureLine hands them here.

#include "film_parse_water.h"   // (and film_spec.h)

namespace cartoon {
namespace film_file {

inline bool deepVerb(const std::string &verb) {
	static const std::vector<std::string> k = {"wear", "sub", "divingbell", "aircave", "helmet", "bigbubble", "blowring", "seatown", "fastlane", "ride", "chase", "schoolshow", "choir",
											   "whalesong", "kraken", "giantsquid", "seaserpent", "hugeeye", "reveal", "whalefall", "hook", "net", "anchor", "brinepool", "pillowlava",
											   "brinicle", "turbidity", "bubblewipe", "waterwobble", "drawnrays", "speedlines", "gulp", "seasparkle"};
	return std::find(k.begin(), k.end(), verb) != k.end();
}

// The usage of each line (what is said when one is wrong).
inline std::string deepUsage(const std::string &verb) {
	static const std::map<std::string, std::string> u = {
		{"wear", "`wear WHO scarf|cape|veil|ribbon|sash [colour C] [length L] [width W]`"},
		{"sub", "`sub round|bubble|robot NAME at X Y Z [to X Y Z]... [speed S] [from T] [toward A] [dive T]... [rise T]... [lights off] [colour R G B] [seed K]`"},
		{"divingbell", "`divingbell X Y Z [size S] [lamp L] [seed K]` (S 1 to 6 m)"},
		{"aircave", "`aircave X Y Z [radius R] [lamp L] [seed K]` (R 1 to 30 m)"},
		{"helmet", "`helmet WHO [size S]`"},
		{"bigbubble", "`bigbubble X Y Z [radius R] [at T] [rise S] [wobble W] [seed K]` (R 0.05 to 3 m)"},
		{"blowring", "`blowring WHO at T`"},
		{"seatown", "`seatown X Z [radius R] [houses N] [lamps N] [traffic N] [nomarket] [seed K]` (R 6 to 60, houses 3 to 24, lamps 0 to 24, traffic 0 to 200)"},
		{"fastlane", "`fastlane NAME at X Y Z to X Y Z ... [width W] [speed S] [streaks N] [debris N] [turtles N] [seed K]`"},
		{"ride", "`ride WHO LANE from T [to T] [across X Y]`"},
		{"chase", "`chase WHO after OTHER [lag S] [from T] [to T]`"},
		{"schoolshow", "`schoolshow N heart|ring|ball|spiral|arrow|wall at T [for D] [size S] [toward A] [beat BPM] [centre X Y Z]`, `schoolshow N word TEXT at T ...` or `schoolshow N burst at T`"},
		{"choir", "`choir N at T [for D] [key K]`"},
		{"whalesong", "`whalesong at T [for D] [from X Y Z]`"},
		{"kraken", "`kraken X Y Z [size S] [toward A] [rise T] [reach X Y Z] [from T] [glow T] [seed K]`"},
		{"giantsquid", "`giantsquid X Y Z [length L] [toward A] [from T] [glow T] [seed K]`"},
		{"seaserpent", "`seaserpent NAME at X Y Z to X Y Z ... [length L] [speed S] [from T] [glow T] [seed K]`"},
		{"hugeeye", "`hugeeye X Y Z [toward A] [size S] [open T] [close T]`"},
		{"reveal", "`reveal X Y Z at T [bright B] [colour R G B] [over D]`"},
		{"whalefall", "`whalefall X Z [length L] [toward A] [seed K]`"},
		{"hook", "`hook X Z at T [depth D] [yank T] [seed K]`"},
		{"net", "`net X Z at T [size S] [school N] [lift T]`"},
		{"anchor", "`anchor X Z at T`"},
		{"brinepool", "`brinepool X Z [radius R] [seed K]`"},
		{"pillowlava", "`pillowlava X Z [radius R] [seed K]`"},
		{"brinicle", "`brinicle X Z at T [length L]`"},
		{"turbidity", "`turbidity X Z toward A at T [size S] [speed V]`"},
		{"bubblewipe", "`bubblewipe at T`"},
		{"waterwobble", "`waterwobble during T0 T1 [amount A]`"},
		{"drawnrays", "`drawnrays [count N] [bright B]`"},
		{"speedlines", "`speedlines WHO [during T0 T1]`"},
		{"gulp", "`gulp WHO at T`"},
		{"seasparkle", "`seasparkle [amount A] [colour R G B]`"}};
	const auto it = u.find(verb);
	return it == u.end() ? "`" + verb + "`?" : it->second;
}

namespace detail {
// A line's words and where its reading has got to (`i`, after the verb): the next n numbers into `v`, or the next three as a point; false when they are not there.
struct DeepWords {
	std::vector<std::string> t;
	std::vector<double> v;
	std::size_t i = 1;
	bool numbers(std::size_t n) {
		v.assign(n, 0.0);
		for (std::size_t k = 0; k < n; ++k)
			if (i + k >= t.size() || !number(t[i + k], v[k])) return false;
		i += n;
		return true;
	}
	bool point(Vec3 &p) {
		if (!numbers(3)) return false;
		p = {v[0], v[1], v[2]};
		return true;
	}
};
}  // namespace detail

// The lines that read their own words rather than `word value` options: `wear`, `sub`, `ride`.
inline std::string deepOwnWordsLine(Spec &s, const std::string &verb, const std::string &line) {
	detail::DeepWords reader{detail::tokens(line)};
	const std::vector<std::string> &t = reader.t;
	const std::vector<double> &v = reader.v;
	std::size_t &i = reader.i;
	const auto numbers = [&reader](std::size_t n) { return reader.numbers(n); };
	const auto point = [&reader](Vec3 &p) { return reader.point(p); };
	const std::string usage = deepUsage(verb);

	if (verb == "wear") {
		flow::Garment g;
		if (t.size() < 3 || std::find(flow::garments().begin(), flow::garments().end(), t[2]) == flow::garments().end()) return usage;
		g.who = t[1], g.kind = t[2], i = 3;
		while (i < t.size()) {
			const std::string w = t[i++];
			if (w == "colour" || w == "color") {
				if (i >= t.size()) return usage;
				if (!colourOf(t[i], g.colour)) {
					if (!numbers(3)) return usage;
					g.colour = {v[0], v[1], v[2]};
				} else {
					++i;
				}
				continue;
			}
			if (!numbers(1) || v[0] <= 0.0 || v[0] > 6.0) return usage;
			if (w == "length") g.length = v[0];
			else if (w == "width") g.width = v[0];
			else return usage;
		}
		s.garments.push_back(g);
		return std::string();
	}
	if (verb == "sub") {
		subs::Sub b;
		if (t.size() < 3 || std::find(subs::kinds().begin(), subs::kinds().end(), t[1]) == subs::kinds().end()) return usage;
		b.kind = t[1], b.name = t[2], i = 3;
		if (i >= t.size() || t[i++] != "at") return usage;
		Vec3 p;
		if (!point(p)) return usage;
		b.path.push_back(p);
		while (i < t.size()) {
			const std::string w = t[i++];
			if (w == "lights" && i < t.size() && (t[i] == "off" || t[i] == "on")) {
				b.lights = t[i++] == "on";
				continue;
			}
			if (w == "to") {
				if (!point(p) || b.path.size() >= 64) return usage;
				b.path.push_back(p);
				continue;
			}
			if (w == "colour" || w == "color") {
				if (!numbers(3)) return usage;
				b.colour = {v[0], v[1], v[2]};
				continue;
			}
			if (!numbers(1)) return usage;
			if (w == "speed" && v[0] > 0.0 && v[0] <= 12.0) b.speed = v[0];
			else if (w == "from") b.from = v[0];
			else if (w == "toward") b.toward = v[0];
			else if (w == "dive") b.dives.push_back(v[0]);
			else if (w == "rise") b.rises.push_back(v[0]);
			else if (w == "seed") b.seed = detail::seedOf(v[0]);
			else return usage;
		}
		s.subs.push_back(b);
		return std::string();
	}
	// ride WHO LANE [across X Y] from T [to T]
	fastlane::Ride r;
	if (t.size() < 3) return usage;
	r.who = t[1], r.lane = t[2], i = 3;
	bool when = false;
	while (i < t.size()) {
		const std::string w = t[i++];
		if (w == "across") {
			if (!numbers(2)) return usage;
			r.x = v[0], r.y = v[1];
			continue;
		}
		if (!numbers(1)) return usage;
		if (w == "from") r.from = v[0], when = true;
		else if (w == "to") r.to = v[0];
		else return usage;
	}
	if (!when || r.to <= r.from) return usage;
	s.rides.push_back(r);
	return std::string();
}

// The other lines into `s`: "" or what is wrong with it.
inline std::string deepLine(Spec &s, const std::string &verb, const std::string &line) {
	using detail::number;
	detail::DeepWords reader{detail::tokens(line)};
	const std::vector<std::string> &t = reader.t;
	const std::vector<double> &v = reader.v;
	std::size_t &i = reader.i;
	const auto numbers = [&reader](std::size_t n) { return reader.numbers(n); };
	const auto point = [&reader](Vec3 &p) { return reader.point(p); };
	const std::string usage = deepUsage(verb);
	// a line's `word value` options, as a map (`at`, `dive`, `rise` repeatable into `times`), its bare words into `flags`; false for a word it does not know
	std::map<std::string, double> opt;
	std::map<std::string, std::vector<double>> times;
	std::set<std::string> flags;
	std::vector<Vec3> to;
	Vec3 colour{-1.0, -1.0, -1.0}, place;
	bool placed = false;
	// the words each line of a shared list of options has of its own (its usage's): another's was taken and did nothing - `gulp ... size`, `divingbell ... rise`
	static const std::map<std::string, std::vector<std::string>> own = {
		{"helmet", {"size"}}, {"blowring", {"at"}}, {"gulp", {"at"}}, {"divingbell", {"size", "lamp", "seed"}}, {"aircave", {"radius", "lamp", "seed"}},
		{"bigbubble", {"radius", "at", "rise", "wobble", "seed"}}, {"fastlane", {"width", "speed", "streaks", "debris", "turtles", "seed"}},
		{"seaserpent", {"length", "speed", "from", "glow", "seed"}}, {"choir", {"at", "for", "key"}}, {"whalesong", {"at", "for", "from"}},
		{"kraken", {"size", "toward", "rise", "reach", "from", "glow", "seed"}}, {"giantsquid", {"length", "toward", "from", "glow", "seed"}},
		{"hugeeye", {"toward", "size", "open", "close"}}, {"reveal", {"at", "bright", "over"}}, {"whalefall", {"length", "toward", "seed"}},
		{"hook", {"at", "depth", "yank", "seed"}}, {"net", {"at", "size", "school", "lift", "seed"}}, {"anchor", {"at"}}, {"brinepool", {"radius", "seed"}},
		{"pillowlava", {"radius", "seed"}}, {"brinicle", {"at", "length"}}, {"turbidity", {"toward", "at", "size", "speed"}}};
	const auto mine = own.find(verb);
	const auto itsOwn = [&mine](const std::string &w) { return mine == own.end() || std::find(mine->second.begin(), mine->second.end(), w) != mine->second.end(); };
	const auto options = [&](const std::vector<std::string> &words, const std::vector<std::string> &repeated, const std::vector<std::string> &bare) {
		while (i < t.size()) {
			const std::string w = t[i++];
			if (std::find(bare.begin(), bare.end(), w) != bare.end()) {
				flags.insert(w);
				continue;
			}
			if (w == "colour" || w == "color") {
				if (mine != own.end() && verb != "reveal") return false;
				if (!numbers(3)) return false;
				colour = {v[0], v[1], v[2]};
				continue;
			}
			if (w == "to" && (verb == "fastlane" || verb == "seaserpent")) {   // (a place on their way; a chase's `to` is a time)
				Vec3 p;
				if (!point(p) || to.size() >= 64) return false;
				to.push_back(p);
				continue;
			}
			const std::string word = w == "center" ? "centre" : w;
			if (!itsOwn(word)) return false;
			double probe = 0.0;
			const bool threeNumbers = i + 2 < t.size() && number(t[i], probe) && number(t[i + 1], probe) && number(t[i + 2], probe);
			if ((word == "reach" || word == "centre" || word == "from") && std::find(words.begin(), words.end(), word) != words.end() && threeNumbers && (word != "from" || verb == "whalesong")) {
				Vec3 p;
				if (!point(p)) return false;
				opt[word] = 1.0, place = p, placed = true;
				continue;
			}
			if (!numbers(1)) return false;
			if (std::find(repeated.begin(), repeated.end(), w) != repeated.end()) times[w].push_back(v[0]);
			else if (std::find(words.begin(), words.end(), w) != words.end()) opt[w] = v[0];
			else return false;
		}
		return true;
	};
	const auto get = [&](const std::string &w, double fallback) { return opt.count(w) ? opt[w] : fallback; };
	const auto within = [&](const std::string &w, double lo, double hi) { return !opt.count(w) || (opt[w] >= lo && opt[w] <= hi); };
	const auto seed = [&]() { return detail::seedOf(get("seed", 1.0)); };

	if (verb == "helmet" || verb == "blowring" || verb == "speedlines" || verb == "gulp") {   // (a character's)
		if (t.size() < 2) return usage;
		const std::string who = t[1];
		i = 2;
		if (verb == "speedlines") {
			seatoon::Lines l;
			l.who = who;
			if (i < t.size()) {
				if (t[i++] != "during" || !numbers(2) || v[1] <= v[0]) return usage;
				l.t0 = v[0], l.t1 = v[1];
			}
			if (i != t.size()) return usage;
			s.seatoon.lines.push_back(l);
			return std::string();
		}
		if (!options({"size", "at"}, {}, {})) return usage;
		if (verb == "helmet") {
			if (!within("size", 0.15, 1.5)) return usage;
			s.air18.helmets.push_back({who, get("size", 0.0)});
		} else {
			if (!opt.count("at")) return usage;
			if (verb == "blowring") s.air18.rings.push_back({who, opt["at"]});
			else s.seatoon.gulps.push_back({who, opt["at"]});
		}
		return std::string();
	}
	if (verb == "divingbell" || verb == "aircave" || verb == "bigbubble") {
		Vec3 p;
		if (!point(p) || !options({"size", "lamp", "radius", "at", "rise", "wobble", "seed"}, {}, {})) return usage;
		if (verb == "divingbell") {
			if (!within("size", 1.0, 6.0) || !within("lamp", 0.0, 10.0) || opt.count("radius") || opt.count("at")) return usage;
			s.air18.bells.push_back({p, get("size", 2.4), get("lamp", 1.0), seed()});
		} else if (verb == "aircave") {
			if (!within("radius", 1.0, 30.0) || !within("lamp", 0.0, 10.0) || opt.count("size") || opt.count("at")) return usage;
			s.air18.caves.push_back({p, get("radius", 4.0), get("lamp", 0.0), seed()});
		} else {
			if (!within("radius", 0.05, 3.0) || !within("rise", 0.01, 5.0) || !within("wobble", 0.0, 4.0)) return usage;
			s.air18.bigs.push_back({p, get("radius", 0.5), get("at", 0.0), get("rise", -1.0), get("wobble", 1.0), seed()});
		}
		return std::string();
	}
	if (verb == "seatown") {
		seatown::Town w;
		if (!numbers(2)) return usage;
		w.x = v[0], w.z = v[1];
		if (!options({"radius", "houses", "lamps", "traffic", "seed"}, {}, {"nomarket"})) return usage;
		if (!within("radius", 6.0, 60.0) || !within("houses", 3.0, 24.0) || !within("lamps", 0.0, 24.0) || !within("traffic", 0.0, 200.0)) return usage;
		w.radius = get("radius", 14.0), w.houses = static_cast<int>(get("houses", 10.0)), w.lamps = static_cast<int>(get("lamps", 8.0)), w.traffic = static_cast<int>(get("traffic", 40.0));
		w.market = flags.count("nomarket") == 0, w.seed = seed();
		s.towns.push_back(w);
		return std::string();
	}
	if (verb == "fastlane" || verb == "seaserpent") {
		if (t.size() < 3) return usage;
		const std::string name = t[1];
		i = 2;
		if (t[i++] != "at") return usage;
		Vec3 p;
		if (!point(p)) return usage;
		if (!options({"to", "width", "speed", "streaks", "debris", "turtles", "length", "from", "glow", "seed"}, {}, {}) || to.empty()) return usage + ": it goes from one place to another";
		std::vector<Vec3> path{p};
		path.insert(path.end(), to.begin(), to.end());
		if (verb == "fastlane") {
			if (!within("width", 0.5, 40.0) || !within("speed", 0.2, 20.0) || !within("streaks", 0.0, 2000.0) || !within("debris", 0.0, 500.0) || !within("turtles", 0.0, 30.0)) return usage;
			fastlane::Lane l;
			l.name = name, l.points = path, l.width = get("width", 4.0), l.speed = get("speed", 3.0), l.streaks = static_cast<int>(get("streaks", 70.0)), l.debris = static_cast<int>(get("debris", 40.0));
			l.turtles = static_cast<int>(get("turtles", 3.0)), l.seed = seed();
			s.lanes.push_back(l);
		} else {
			if (!within("length", 3.0, 200.0) || !within("speed", 0.1, 20.0)) return usage;
			abyss::Serpent sp;
			sp.name = name, sp.path = path, sp.length = get("length", 30.0), sp.speed = get("speed", 2.5), sp.from = get("from", 0.0), sp.glow = get("glow", 1e30), sp.seed = seed();
			s.abyss.serpents.push_back(sp);
		}
		return std::string();
	}
	if (verb == "chase") {
		fastlane::Chase c;
		if (t.size() < 4 || t[2] != "after") return usage;
		c.who = t[1], c.after = t[3], i = 4;
		if (!options({"lag", "from", "to"}, {}, {}) || !within("lag", 0.1, 30.0)) return usage;
		c.lag = get("lag", 1.2), c.from = get("from", -1e30), c.to = get("to", 1e30);
		if (c.to <= c.from) return usage;
		s.chases.push_back(c);
		return std::string();
	}
	if (verb == "schoolshow") {
		shows::Show sh;
		double n = 0.0;
		if (t.size() < 3 || !number(t[1], n) || n < 1.0 || n != std::floor(n) || std::find(shows::shapes().begin(), shows::shapes().end(), t[2]) == shows::shapes().end()) return usage;
		sh.school = static_cast<int>(n), sh.shape = t[2], i = 3;
		if (sh.shape == "word") {
			if (i >= t.size() || !shows::writable(t[i])) return usage + ": a word of up to 12 letters, numbers, ! or ?";
			sh.word = t[i++];
		}
		if (!options({"at", "for", "size", "toward", "beat", "centre"}, {}, {}) || !opt.count("at") || !within("for", 1.0, 120.0) || !within("size", 0.3, 40.0) || !within("beat", 0.0, 240.0)) return usage;
		sh.at = opt["at"], sh.length = get("for", 8.0), sh.size = get("size", 3.0), sh.toward = get("toward", 0.0), sh.beat = get("beat", 0.0);
		if (opt.count("centre")) sh.centre = place, sh.placed = true;
		s.shows.push_back(sh);
		return std::string();
	}
	if (verb == "choir" || verb == "whalesong") {
		shows::Song sg;
		if (verb == "choir") {
			double n = 0.0;
			if (t.size() < 2 || !number(t[1], n) || n < 1.0 || n != std::floor(n)) return usage;
			sg.school = static_cast<int>(n), i = 2;
		}
		if (!options({"at", "for", "key", "from"}, {}, {}) || !opt.count("at") || !within("for", 1.0, 600.0) || !within("key", 30.0, 90.0)) return usage;
		if (verb == "choir" && placed) return usage;
		sg.at = opt["at"], sg.length = get("for", verb == "choir" ? 10.0 : 20.0), sg.key = static_cast<int>(get("key", 57.0));
		if (placed) sg.from = place, sg.placed = true;
		s.songs.push_back(sg);
		return std::string();
	}
	if (verb == "kraken" || verb == "giantsquid" || verb == "hugeeye" || verb == "reveal") {
		Vec3 p;
		if (!point(p) || !options({"size", "toward", "rise", "reach", "from", "glow", "length", "open", "close", "at", "bright", "over", "seed"}, {}, {})) return usage;
		if (verb == "kraken") {
			if (!within("size", 1.0, 60.0)) return usage;
			if (opt.count("from") && !opt.count("reach")) return usage + ": `from T` is when it reaches for the place `reach` names";
			abyss::Kraken k;
			k.at = p, k.size = get("size", 6.0), k.toward = get("toward", 0.0), k.rise = get("rise", -1.0), k.glow = get("glow", 1e30), k.seed = seed();
			if (opt.count("reach")) k.reach = place, k.reachFrom = get("from", 0.0);
			s.abyss.krakens.push_back(k);
		} else if (verb == "giantsquid") {
			if (!within("length", 2.0, 40.0)) return usage;
			s.abyss.squids.push_back({p, get("length", 12.0), get("toward", 0.0), get("from", 0.0), get("glow", 1e30), seed()});
		} else if (verb == "hugeeye") {
			if (!within("size", 0.3, 30.0)) return usage;
			s.abyss.eyes.push_back({p, get("toward", 0.0), get("size", 2.0), get("open", 0.0), get("close", 1e30)});
		} else {
			if (!opt.count("at") || !within("bright", 0.01, 100.0) || !within("over", 0.05, 60.0)) return usage;   // (not 0: its light came up over no time at all, 0/0, a NaN light)
			abyss::Reveal r;
			r.at = p, r.t = opt["at"], r.bright = get("bright", 1.0), r.over = get("over", 3.0);
			if (colour.x >= 0.0) r.colour = colour;
			s.abyss.reveals.push_back(r);
		}
		return std::string();
	}
	// the rest lead with X Z
	if (!numbers(2)) return usage;
	const double x = v[0], z = v[1];
	if (!options({"length", "toward", "at", "depth", "yank", "size", "school", "lift", "radius", "speed", "seed", "amount", "count", "bright", "for"}, {}, {})) return usage;
	if (verb == "whalefall") {
		if (!within("length", 3.0, 40.0)) return usage;
		s.abyss.falls.push_back({x, z, get("length", 14.0), get("toward", 0.0), seed()});
	} else if (verb == "hook") {
		if (!opt.count("at") || !within("depth", 0.1, 100.0)) return usage;
		s.above.hooks.push_back({x, z, opt["at"], get("depth", 2.0), get("yank", 1e30), seed()});
	} else if (verb == "net") {
		if (!opt.count("at") || !within("size", 1.0, 40.0)) return usage;
		s.above.nets.push_back({x, z, opt["at"], get("size", 8.0), get("lift", 1e30), static_cast<int>(get("school", 0.0)), seed()});
	} else if (verb == "anchor") {
		if (!opt.count("at") || opt.size() != 1) return usage;
		s.above.anchors.push_back({x, z, opt["at"]});
	} else if (verb == "brinepool" || verb == "pillowlava") {
		if (!within("radius", 0.5, 60.0) || opt.count("at")) return usage;
		if (verb == "brinepool") s.above.brines.push_back({x, z, get("radius", 6.0), seed()});
		else s.above.lavas.push_back({x, z, get("radius", 5.0), seed()});
	} else if (verb == "brinicle") {
		if (!opt.count("at") || !within("length", 0.5, 200.0)) return usage;
		s.above.brinicles.push_back({x, z, opt["at"], get("length", 0.0)});
	} else if (verb == "turbidity") {
		if (!opt.count("at") || !within("size", 0.2, 5.0) || !within("speed", 0.2, 20.0)) return usage;
		s.above.slumps.push_back({x, z, get("toward", 0.0), opt["at"], get("size", 1.0), get("speed", 4.0)});
	} else {
		return usage;
	}
	return std::string();
}

// The lines of round 18 that lead with neither a name nor X Z: `bubblewipe`, `waterwobble`, `drawnrays`, `seasparkle`.
inline std::string deepLookLine(Spec &s, const std::string &verb, const std::string &line) {
	using detail::number;
	const std::vector<std::string> t = detail::tokens(line);
	const std::string usage = deepUsage(verb);
	double a = 0.0, b = 0.0;
	if (verb == "bubblewipe") {
		if (t.size() != 3 || t[1] != "at" || !number(t[2], a)) return usage;
		s.seatoon.wipes.push_back(a);
		return std::string();
	}
	if (verb == "waterwobble") {
		seatoon::Wobble w;
		if (t.size() < 4 || t[1] != "during" || !number(t[2], w.t0) || !number(t[3], w.t1) || w.t1 <= w.t0) return usage;
		if (t.size() == 6 && t[4] == "amount" && number(t[5], a) && a >= 0.0 && a <= 1.0) w.amount = a;
		else if (t.size() != 4) return usage;
		s.seatoon.wobbles.push_back(w);
		return std::string();
	}
	if (verb == "drawnrays" || verb == "seasparkle") {
		for (std::size_t i = 1; i < t.size(); i += 2) {
			if (i + 1 >= t.size()) return usage;
			if (t[i] == "colour" && verb == "seasparkle") {
				if (i + 3 >= t.size() || !number(t[i + 1], a) || !number(t[i + 2], b)) return usage;
				double c = 0.0;
				if (!number(t[i + 3], c) || std::min({a, b, c}) < 0.0 || std::max({a, b, c}) > 1.0) return usage + ": a colour's parts are 0 to 1";   // (a negative light stops pbrt)
				s.seatoon.sparkle.colour = {a, b, c};
				i += 2;
				continue;
			}
			if (!number(t[i + 1], a)) return usage;
			if (verb == "drawnrays" && t[i] == "count" && a >= 1.0 && a <= 40.0) s.seatoon.rays.count = static_cast<int>(a);
			else if (verb == "drawnrays" && t[i] == "bright" && a > 0.0 && a <= 10.0) s.seatoon.rays.bright = a;
			else if (verb == "seasparkle" && t[i] == "amount" && a > 0.0 && a <= 5.0) s.seatoon.sparkle.amount = a;
			else return usage;
		}
		(verb == "drawnrays" ? s.seatoon.rays.on : s.seatoon.sparkle.on) = true;
		return std::string();
	}
	return usage;
}

// Any of the round's lines: "" or what is wrong.
inline std::string deepAnyLine(Spec &s, const std::string &verb, const std::string &line) {
	if (verb == "bubblewipe" || verb == "waterwobble" || verb == "drawnrays" || verb == "seasparkle") return deepLookLine(s, verb, line);
	if (verb == "wear" || verb == "sub" || verb == "ride") return deepOwnWordsLine(s, verb, line);
	return deepLine(s, verb, line);
}

// With the land and the water known (after the water's settling): heights above the seabed made pbrt's, the lanes worked out, who sits in a sub, who is dry.
inline void settleDeep(Spec &s) {
	const std::shared_ptr<const terrain::Field> ground = s.hasTerrain ? terrain::fieldOf(s.terrain) : nullptr;
	const double floorY = s.seas.empty() ? 0.0 : s.seas[0].level - 15.0;
	const auto bed = [&](double x, double z) { return ground && ground->inside(x, z) ? ground->heightAt(x, z) : (ground ? s.terrain.y : floorY); };
	const auto lift = [&](Vec3 &p) { p.y += bed(p.x, p.z); };
	for (subs::Sub &b : s.subs)
		for (Vec3 &p : b.path) lift(p);
	for (airpocket::Bell &b : s.air18.bells) lift(b.at);
	for (airpocket::Cave &c : s.air18.caves) lift(c.at);
	for (airpocket::Big &b : s.air18.bigs) lift(b.at);
	for (fastlane::Lane &l : s.lanes) {
		for (Vec3 &p : l.points) lift(p);
		fastlane::prepare(l);
	}
	for (shows::Show &sh : s.shows)
		if (sh.placed) lift(sh.centre);
	for (shows::Song &sg : s.songs)
		if (sg.placed) lift(sg.from);
	for (abyss::Kraken &k : s.abyss.krakens) {
		lift(k.at);
		if (k.reachFrom < 1e29) lift(k.reach);
	}
	for (abyss::Squid &q : s.abyss.squids) lift(q.at);
	for (abyss::Serpent &sp : s.abyss.serpents)
		for (Vec3 &p : sp.path) lift(p);
	for (abyss::Eye &e : s.abyss.eyes) lift(e.at);
	for (abyss::Reveal &r : s.abyss.reveals) lift(r.at);
	for (const airpocket::Helmet &h : s.air18.helmets) s.dry.insert(h.who);
	for (const Spec::Aboard &a : s.aboards)
		for (const subs::Sub &b : s.subs)
			if (b.name == a.boat) {
				s.dry.insert(a.who), s.riders.insert(a.who);
				for (Character &c : s.characters)
					if (c.name == a.who) {
						Character::Sit st;
						st.t0 = -10.0, st.height = 0.42 / std::max(0.01, c.scale);
						c.sits.push_back(st);
					}
			}
}

// What is wrong with the film's round-18 lines as a whole, or "".
inline std::string deepProblems(const Spec &s) {
	const auto aCharacter = [&](const std::string &name) { return std::any_of(s.characters.begin(), s.characters.end(), [&](const Character &c) { return c.name == name; }); };
	const auto noOne = [](const std::string &what, const std::string &name) { return "`" + what + " " + name + "`: there is no character called " + name + " (a `character NAME ...` line; names are matched exactly)"; };
	for (const flow::Garment &g : s.garments)
		if (!aCharacter(g.who)) return noOne("wear", g.who);
	const bool under = s.subs.size() + s.towns.size() + s.lanes.size() + s.shows.size() + s.songs.size() > 0 || s.air18.any() || s.abyss.any() || s.above.any() || !s.seatoon.gulps.empty() ||
					   s.seatoon.rays.on || s.seatoon.sparkle.on;
	if (under && !s.hasUnderwater) return "subs, air pockets, towns, fast lanes, school shows, giants, hooks and nets, the deep's places, gulps, drawn rays and sea sparkle are under the sea: the film needs an `underwater` line (and a `water` one)";
	for (const airpocket::Helmet &h : s.air18.helmets)
		if (!aCharacter(h.who)) return noOne("helmet", h.who);
	for (const airpocket::Ring &r : s.air18.rings)
		if (!aCharacter(r.who)) return noOne("blowring", r.who);
	for (const seatoon::Lines &l : s.seatoon.lines)
		if (!aCharacter(l.who)) return noOne("speedlines", l.who);
	for (const seatoon::Gulp &g : s.seatoon.gulps)
		if (!aCharacter(g.who)) return noOne("gulp", g.who);
	for (const fastlane::Ride &r : s.rides) {
		if (!aCharacter(r.who)) return noOne("ride", r.who);
		if (std::none_of(s.lanes.begin(), s.lanes.end(), [&](const fastlane::Lane &l) { return l.name == r.lane; })) return "`ride " + r.who + " " + r.lane + "`: there is no fast lane called " + r.lane;
	}
	for (const fastlane::Chase &c : s.chases) {
		if (!aCharacter(c.who)) return noOne("chase", c.who);
		if (!aCharacter(c.after)) return noOne("chase ... after", c.after);
		if (c.who == c.after) return "`chase " + c.who + "`: it cannot chase itself";
		for (const fastlane::Chase &d : s.chases)
			if (d.who == c.after) return "`chase " + c.who + " after " + c.after + "`: " + c.after + " is chasing someone too - a chase follows one who goes their own way";
	}
	for (const shows::Show &sh : s.shows)
		if (sh.school > static_cast<int>(s.schools.size())) return "`schoolshow " + std::to_string(sh.school) + "`: there is no school " + std::to_string(sh.school) + " (the film's `school` lines, from 1)";
	for (const shows::Song &sg : s.songs)
		if (sg.school > static_cast<int>(s.schools.size())) return "`choir " + std::to_string(sg.school) + "`: there is no school " + std::to_string(sg.school) + " (the film's `school` lines, from 1)";
	for (const fromabove::Net &n : s.above.nets)
		if (n.school > static_cast<int>(s.schools.size()) || n.school < 0) return "`net ... school " + std::to_string(n.school) + "`: there is no such school (the film's `school` lines, from 1)";
	if (!s.above.brinicles.empty() && !s.iceCap.on) return "`brinicle` grows down from the ice: the film needs an `icecap` line";
	return std::string();
}

}  // namespace film_file
}  // namespace cartoon
