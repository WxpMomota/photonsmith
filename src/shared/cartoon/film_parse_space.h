#pragma once
// film_parse_space.h -- the space lines of a film file (its grammar: film_parse.h; round 10, "the universe"): the set in space and what is in it. natureLine
// (film_parse_nature.h) hands them here.

#include "film_spec.h"

namespace cartoon {
namespace film_file {

// Whether `verb` is a space line's.
inline bool isSpaceVerb(const std::string &verb) {
	static const std::vector<std::string> k = {"space", "planet", "moon"};
	return std::find(k.begin(), k.end(), verb) != k.end();
}

// A `space` line (film_space.h). "" or what is wrong with it.
inline std::string spaceSetLine(Spec &s, const std::vector<std::string> &t) {
	using detail::number;
	const auto fail = [](const std::string &what) { return what; };
	const std::string usage = "`space [stars S] [milkyway M] [sun BEARING ELEVATION] [sunsize DEGREES] [sunlight S] [sun off] [seed N]`";
	if (s.space.on) return fail("a film has one `space` line");
	s.space.on = true;
	for (std::size_t i = 1; i < t.size();) {
		double a = 0.0, b = 0.0;
		if (t[i] == "sun" && i + 1 < t.size() && t[i + 1] == "off") {
			s.space.sun = false;
			i += 2;
		} else if (t[i] == "sun" && i + 2 < t.size() && number(t[i + 1], a) && number(t[i + 2], b) && b >= -90.0 && b <= 90.0) {
			s.space.sunBearing = a, s.space.sunElevation = b;
			i += 3;
		} else if (i + 1 < t.size() && number(t[i + 1], a)) {
			if (t[i] == "stars" && a >= 0.0 && a <= 4.0) s.space.stars = a;
			else if (t[i] == "milkyway" && a >= 0.0 && a <= 4.0) s.space.milkyWay = a;
			else if (t[i] == "sunsize" && a > 0.05 && a <= 20.0) s.space.sunSize = a;
			else if (t[i] == "sunlight" && a > 0.0 && a <= 20.0) s.space.sunStrength = a;
			else if (t[i] == "seed") s.space.seed = static_cast<int>(a);
			else return fail("\"" + t[i] + "\": " + usage + " (stars and milkyway 0 to 4, sunsize 0.05 to 20 degrees, sunlight up to 20)");
			i += 2;
		} else {
			return fail("\"" + t[i] + "\": " + usage);
		}
	}
	return std::string();
}

// A `planet` line (film_planets.h). "" or what is wrong with it.
inline std::string planetLine(Spec &s, const std::vector<std::string> &t) {
	using detail::number;
	const auto fail = [](const std::string &what) { return what; };
	const std::string usage = "`planet NAME KIND at X Y Z radius R [tilt DEGREES] [spin SECONDS] [air S [colour blue|orange|white|green]] [cities S] [aurora S] [rings INNER OUTER [opacity S]] [seed N]` (rocky, desert, ice, ocean, gas or lava)";
	if (t.size() < 3) return fail(usage);
	planets::Planet p;
	p.name = t[1], p.kind = t[2];
	if (std::find(planets::kinds().begin(), planets::kinds().end(), p.kind) == planets::kinds().end()) return fail("\"" + p.kind + "\": " + usage);
	for (const planets::Planet &q : s.planets)
		if (q.name == p.name) return fail("there is already a planet called " + p.name);
	bool placed = false, sized = false;
	for (std::size_t i = 3; i < t.size();) {
		double a = 0.0, b = 0.0, c = 0.0;
		if (t[i] == "at" && i + 3 < t.size() && number(t[i + 1], a) && number(t[i + 2], b) && number(t[i + 3], c)) {
			p.at = {a, b, c}, placed = true;
			i += 4;
		} else if (t[i] == "rings" && i + 2 < t.size() && number(t[i + 1], a) && number(t[i + 2], b)) {
			if (!(a >= 1.0 && b > a && b <= 10.0)) return fail("rings INNER OUTER: in the planet's radii, from 1 out to 10, the outer beyond the inner");
			p.ringInner = a, p.ringOuter = b;
			i += 3;
		} else if (t[i] == "colour" && i + 1 < t.size() && std::find(planets::airColours().begin(), planets::airColours().end(), t[i + 1]) != planets::airColours().end()) {
			p.airColour = t[i + 1];
			i += 2;
		} else if (i + 1 < t.size() && number(t[i + 1], a)) {
			if (t[i] == "radius" && a >= 1.0 && a <= 200000.0) p.radius = a, sized = true;
			else if (t[i] == "tilt" && a >= -180.0 && a <= 180.0) p.tilt = a;
			else if (t[i] == "spin" && a != 0.0) p.spin = a;
			else if (t[i] == "seed") p.seed = static_cast<int>(a);
			else if (t[i] == "air" && a >= 0.0 && a <= 10.0) p.air = a;
			else if (t[i] == "cities" && a >= 0.0 && a <= 10.0) p.cities = a;
			else if (t[i] == "aurora" && a >= 0.0 && a <= 10.0) p.aurora = a;
			else if (t[i] == "opacity" && a >= 0.0 && a <= 3.0) p.ringOpacity = a;
			else return fail("\"" + t[i] + "\": " + usage + " (radius 1 to 200000 m, air, cities and aurora 0 to 10)");
			i += 2;
		} else {
			return fail("\"" + t[i] + "\": " + usage);
		}
	}
	if (!placed || !sized) return fail(usage + ": where it is and how big");
	s.planets.push_back(p);
	return std::string();
}

// A `moon` line: a body going round another (film_planets.h). "" or what is wrong with it.
inline std::string moonLine(Spec &s, const std::vector<std::string> &t) {
	using detail::number;
	const auto fail = [](const std::string &what) { return what; };
	const std::string usage = "`moon NAME KIND of BODY radius R orbit D [period SECONDS] [phase DEGREES] [incline DEGREES] [seed N]` (and a planet's options: air, rings ...)";
	if (t.size() < 5 || t[3] != "of") return fail(usage);
	if (std::none_of(s.planets.begin(), s.planets.end(), [&](const planets::Planet &q) { return q.name == t[4]; })) return fail("no planet or moon called " + t[4] + " (a moon's line comes after its planet's)");
	double a = 0.0;
	std::vector<std::string> rest = {"planet", t[1], t[2], "at", "0", "0", "0"};
	planets::Planet m;
	m.around = t[4];
	bool orbits = false;
	for (std::size_t i = 5; i < t.size(); ++i) {
		if (i + 1 < t.size() && number(t[i + 1], a) && (t[i] == "orbit" || t[i] == "period" || t[i] == "phase" || t[i] == "incline")) {
			if (t[i] == "orbit") {
				if (a <= 0.0) return fail("a moon's orbit is a distance from its planet's middle, above 0");
				m.orbit = a, orbits = true;
			} else if (t[i] == "period") {
				if (a == 0.0) return fail("a moon's period is not 0 (negative: it goes the other way)");
				m.period = a;
			} else if (t[i] == "phase") m.phase = a;
			else m.incline = a;
			++i;
		} else {
			rest.push_back(t[i]);
		}
	}
	if (!orbits) return fail(usage + ": its orbit");
	const std::string e = planetLine(s, rest);
	if (!e.empty()) return e;
	planets::Planet &p = s.planets.back();
	p.around = m.around, p.orbit = m.orbit, p.period = m.period, p.phase = m.phase, p.incline = m.incline;
	return std::string();
}

// One space line: what it says goes into `s`. "" or what is wrong with it.
inline std::string spaceLine(Spec &s, const std::string &verb, const std::string &line) {
	const std::vector<std::string> t = detail::tokens(line);
	if (verb == "space") return spaceSetLine(s, t);
	if (verb == "planet") return planetLine(s, t);
	if (verb == "moon") return moonLine(s, t);
	return "unknown space line \"" + verb + "\"";
}

}  // namespace film_file
}  // namespace cartoon
