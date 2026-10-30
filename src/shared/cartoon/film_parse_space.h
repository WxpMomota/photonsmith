#pragma once
// film_parse_space.h -- the space lines of a film file (its grammar: film_parse.h; round 10, "the universe"): the set in space and what is in it. natureLine
// (film_parse_nature.h) hands them here.

#include "film_spec.h"

namespace cartoon {
namespace film_file {

// Whether `verb` is a space line's.
inline bool isSpaceVerb(const std::string &verb) {
	static const std::vector<std::string> k = {"space", "planet", "moon", "nebula", "galaxy", "star", "blackhole", "comet", "asteroids", "remnant", "ship", "astronaut", "descent", "orbit", "burn", "laser", "explode", "shield", "gravity", "footprints", "blob", "drift", "interior", "hologram", "screen", "label", "orbitline", "scalebar", "compare"};
	return std::find(k.begin(), k.end(), verb) != k.end();
}
// Whether an `orbit` line is a ship's (`orbit SHIP ...`: a ship named before it, or before any shot) rather than a shot's camera going round someone (film_parse.cpp).
inline bool isShipOrbit(const Spec &s, const std::string &line) {
	const std::vector<std::string> t = detail::tokens(line);
	return s.shots.empty() || (t.size() > 1 && std::any_of(s.ships.begin(), s.ships.end(), [&](const cosmos::Ship &sh) { return sh.name == t[1]; }));
}

// A `space` line (film_space.h). "" or what is wrong with it.
inline std::string spaceSetLine(Spec &s, const std::vector<std::string> &t) {
	using detail::number;
	const auto fail = [](const std::string &what) { return what; };
	const std::string usage = "`space [stars S|real] [constellations] [date YYYY-MM-DD] [milkyway M] [sun BEARING ELEVATION] [sunsize DEGREES] [sunlight S] [sun off] [flare F] [bloom B] [planetshine S] [hum H] [seed N]`";
	if (s.space.on) return fail("a film has one `space` line");
	s.space.on = true;
	bool sunGiven = false;
	for (std::size_t i = 1; i < t.size();) {
		double a = 0.0, b = 0.0;
		if (t[i] == "stars" && i + 1 < t.size() && t[i + 1] == "real") {   // (film_starsky.h)
			s.space.realStars = true;
			i += 2;
		} else if (t[i] == "constellations") {
			s.space.constellations = true;
			++i;
		} else if (t[i] == "date" && i + 1 < t.size()) {
			if (!starsky::parseDate(t[i + 1], s.space.jd)) return fail("\"date " + t[i + 1] + "\": a date from 1800 to 2050, YYYY-MM-DD (or YYYY-MM-DDTHH:MM) - " + usage);
			i += 2;
		} else if (t[i] == "sun" && i + 1 < t.size() && t[i + 1] == "off") {
			s.space.sun = false;
			i += 2;
		} else if (t[i] == "sun" && i + 2 < t.size() && number(t[i + 1], a) && number(t[i + 2], b) && b >= -90.0 && b <= 90.0) {
			s.space.sunBearing = a, s.space.sunElevation = b, sunGiven = true;
			i += 3;
		} else if (i + 1 < t.size() && number(t[i + 1], a)) {
			if (t[i] == "stars" && a >= 0.0 && a <= 4.0) s.space.stars = a;
			else if (t[i] == "milkyway" && a >= 0.0 && a <= 4.0) s.space.milkyWay = a;
			else if (t[i] == "sunsize" && a > 0.05 && a <= 20.0) s.space.sunSize = a;
			else if (t[i] == "sunlight" && a > 0.0 && a <= 20.0) s.space.sunStrength = a;
			else if (t[i] == "flare" && a >= 0.0 && a <= 1.0) s.space.flare = a;
			else if (t[i] == "bloom" && a >= 0.0 && a <= 1.0) s.space.bloom = a;
			else if (t[i] == "planetshine" && a >= 0.0 && a <= 10.0) s.space.planetshine = a;
			else if (t[i] == "hum" && a >= 0.0 && a <= 4.0) s.space.hum = a;
			else if (t[i] == "seed") s.space.seed = static_cast<int>(a);
			else return fail("\"" + t[i] + "\": " + usage + " (stars and milkyway 0 to 4, sunsize 0.05 to 20 degrees, sunlight up to 20, flare and bloom 0 to 1, planetshine 0 to 10, hum 0 to 4)");
			i += 2;
		} else {
			return fail("\"" + t[i] + "\": " + usage);
		}
	}
	if (s.space.jd > 0.0 && !sunGiven) {   // (on a date, the sun where it was among the stars: the real sky's frame, film_starsky.h)
		double ra = 0.0, dec = 0.0;
		starsky::sunOn(s.space.jd, ra, dec);
		const Vec3 d = starsky::Frame{}.world(ra, dec);
		s.space.sunBearing = std::atan2(d.x, d.z) * 180.0 / kPi, s.space.sunElevation = std::asin(std::clamp(d.y, -1.0, 1.0)) * 180.0 / kPi;
	}
	if (s.space.constellations && !s.space.realStars) return fail("`constellations` are drawn between the real stars: `stars real` - " + usage);
	return std::string();
}

// A `planet` line (film_planets.h). "" or what is wrong with it.
inline std::string planetLine(Spec &s, const std::vector<std::string> &t) {
	using detail::number;
	const auto fail = [](const std::string &what) { return what; };
	const std::string usage = "`planet NAME KIND at X Y Z radius R [tilt DEGREES] [spin SECONDS] [air S [colour blue|orange|white|green]] [cities S] [aurora S] [rings INNER OUTER [opacity S]] [flow F] [storms N] [clouds C] [lightning L] [seed N]` (rocky, desert, ice, ocean, gas or lava)";
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
			else if (t[i] == "flow" && a >= 0.0 && a <= 10.0 && p.kind == "gas") p.flow = a;   // (round 11: its weather, film_planets.h)
			else if (t[i] == "storms" && a >= 0.0 && a <= 20.0 && p.kind == "gas") p.storms = static_cast<int>(a);
			else if (t[i] == "clouds" && a > 0.0 && a <= 1.0 && p.kind != "gas") p.clouds = a;
			else if (t[i] == "lightning" && a >= 0.0 && a <= 4.0) p.lightning = a;
			else return fail("\"" + t[i] + "\": " + usage + " (radius 1 to 200000 m, air, cities and aurora 0 to 10; a gas giant's flow 0 to 10 and storms up to 20; clouds 0 to 1 over a solid world; lightning 0 to 4)");
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

// The common start of a body's line - `VERB NAME [KIND] at X Y Z` - and its numbered options, each with its range; "" or what is wrong. `kinds` empty: no kind.
// `option(word, value)` takes an option (false: not one of this line's).
inline std::string bodyLine(const std::vector<std::string> &t, const std::string &usage, const std::vector<std::string> &kinds, std::string &name, std::string &kind, Vec3 &at,
							const std::function<bool(const std::string &, double)> &option, const std::function<bool(const std::string &, const std::string &)> &word = nullptr) {
	using detail::number;
	if (t.size() < 2) return usage;
	name = t[1];
	bool placed = false;
	for (std::size_t i = 2; i < t.size();) {
		double a = 0.0, b = 0.0, c = 0.0;
		if (t[i] == "at" && i + 3 < t.size() && number(t[i + 1], a) && number(t[i + 2], b) && number(t[i + 3], c)) {
			at = {a, b, c}, placed = true;
			i += 4;
		} else if (t[i] == "kind" && i + 1 < t.size() && std::find(kinds.begin(), kinds.end(), t[i + 1]) != kinds.end()) {
			kind = t[i + 1];
			i += 2;
		} else if (i + 1 < t.size() && word && word(t[i], t[i + 1])) {
			i += 2;
		} else if (i + 1 < t.size() && number(t[i + 1], a) && option(t[i], a)) {
			i += 2;
		} else {
			return "\"" + t[i] + "\": " + usage;
		}
	}
	if (!placed) return usage + ": where it is";
	return std::string();
}
// A `nebula` line (film_cosmos.h). "" or what is wrong with it.
inline std::string nebulaLine(Spec &s, const std::vector<std::string> &t) {
	cosmos::Nebula n;
	bool sized = false;
	const std::string e = bodyLine(t, "`nebula NAME at X Y Z size S [kind emission|dark|pillars] [brightness B] [seed N]` (size 100 to 200000 m, brightness up to 20)", cosmos::nebulaKinds(),
								   n.name, n.kind, n.at, [&](const std::string &w, double a) {
									   if (w == "size" && a >= 100.0 && a <= 200000.0) return n.size = a, sized = true;
									   if (w == "brightness" && a > 0.0 && a <= 20.0) return n.brightness = a, true;
									   if (w == "seed") return n.seed = static_cast<int>(a), true;
									   return false;
								   });
	if (!e.empty()) return e;
	if (!sized) return "`nebula NAME at X Y Z size S ...`: how big it is";
	s.nebulae.push_back(n);
	return std::string();
}

// A `galaxy` line (film_cosmos.h). "" or what is wrong with it.
inline std::string galaxyLine(Spec &s, const std::vector<std::string> &t) {
	cosmos::Galaxy g;
	bool sized = false;
	const std::string e = bodyLine(t, "`galaxy NAME at X Y Z size S [kind spiral|barred|elliptical] [tilt DEGREES] [turn DEGREES] [brightness B] [seed N]` (size 100 to 400000 m)",
								   cosmos::galaxyKinds(), g.name, g.kind, g.at, [&](const std::string &w, double a) {
									   if (w == "size" && a >= 100.0 && a <= 400000.0) return g.size = a, sized = true;
									   if (w == "tilt" && a >= -90.0 && a <= 90.0) return g.tilt = a, true;
									   if (w == "turn") return g.turn = a, true;
									   if (w == "brightness" && a > 0.0 && a <= 20.0) return g.brightness = a, true;
									   if (w == "seed") return g.seed = static_cast<int>(a), true;
									   return false;
								   });
	if (!e.empty()) return e;
	if (!sized) return "`galaxy NAME at X Y Z size S ...`: how big it is";
	s.galaxies.push_back(g);
	return std::string();
}

// A `star` line (film_cosmos.h). "" or what is wrong with it.
inline std::string starLine(Spec &s, const std::vector<std::string> &t) {
	cosmos::Star st;
	bool sized = false;
	const std::string e = bodyLine(t, "`star NAME at X Y Z radius R [kind sun|red|orange|white|blue] [temperature K] [brightness B] [prominences N] [flare T] ... [seed N]` (radius 10 to 200000 m, 1500 to 50000 K, 0 to 40 prominences)",
								   cosmos::starKinds(), st.name, st.kind, st.at, [&](const std::string &w, double a) {
									   if (w == "radius" && a >= 10.0 && a <= 200000.0) return st.radius = a, sized = true;
									   if (w == "temperature" && a >= 1500.0 && a <= 50000.0) return st.temperature = a, true;
									   if (w == "brightness" && a > 0.0 && a <= 20.0) return st.brightness = a, true;
									   if (w == "prominences" && a >= 0.0 && a <= 40.0) return st.prominences = static_cast<int>(a), true;
									   if (w == "flare") return st.flares.push_back(a), true;
									   if (w == "seed") return st.seed = static_cast<int>(a), true;
									   return false;
								   });
	if (!e.empty()) return e;
	if (!sized) return "`star NAME at X Y Z radius R ...`: how big it is";
	s.stars.push_back(st);
	return std::string();
}

// A `blackhole` line (film_cosmos.h). "" or what is wrong with it.
inline std::string blackHoleLine(Spec &s, const std::vector<std::string> &t) {
	cosmos::BlackHole b;
	bool sized = false;
	std::string kind;
	const std::string e = bodyLine(t, "`blackhole NAME at X Y Z radius R [disc D] [tilt DEGREES] [turn DEGREES] [spin S] [jets L] [brightness B] [seed N]` (radius 10 to 100000 m, "
									  "disc 4 to 30 radii, jets 0 or 2 to 60 radii)",
								   {}, b.name, kind, b.at, [&](const std::string &w, double a) {
									   if (w == "radius" && a >= 10.0 && a <= 100000.0) return b.radius = a, sized = true;
									   if (w == "disc" && a >= 4.0 && a <= 30.0) return b.disc = a, true;
									   if (w == "tilt" && a >= -90.0 && a <= 90.0) return b.tilt = a, true;
									   if (w == "turn") return b.turn = a, true;
									   if (w == "spin" && a >= -5.0 && a <= 5.0) return b.spin = a, true;
									   if (w == "jets" && (a == 0.0 || (a >= 2.0 && a <= 60.0))) return b.jets = a, true;
									   if (w == "brightness" && a > 0.0 && a <= 20.0) return b.brightness = a, true;
									   if (w == "seed") return b.seed = static_cast<int>(a), true;
									   return false;
								   });
	if (!e.empty()) return e;
	if (!sized) return "`blackhole NAME at X Y Z radius R ...`: how big it is";
	s.blackHoles.push_back(b);
	return std::string();
}

// A `comet` line (film_cosmos.h). "" or what is wrong with it.
inline std::string cometLine(Spec &s, const std::vector<std::string> &t) {
	const std::string usage = "`comet NAME at X Y Z [move DX DY DZ] [away DX DY DZ] [size S] [tail L] [brightness B] [seed N]` (size 10 to 20000 m, tail 100 to 400000 m)";
	cosmos::Comet c;
	// (its three-number words first: what is left is a body's line)
	std::vector<std::string> rest;
	for (std::size_t i = 0; i < t.size(); ++i) {
		if ((t[i] == "move" || t[i] == "away") && i + 3 < t.size()) {
			double a = 0.0, b = 0.0, d = 0.0;
			if (!detail::number(t[i + 1], a) || !detail::number(t[i + 2], b) || !detail::number(t[i + 3], d)) return "\"" + t[i] + "\": " + usage;
			(t[i] == "move" ? c.move : c.away) = {a, b, d};
			i += 3;
		} else {
			rest.push_back(t[i]);
		}
	}
	std::string kind;
	const std::string e = bodyLine(rest, usage, {}, c.name, kind, c.at, [&](const std::string &w, double a) {
		if (w == "size" && a >= 10.0 && a <= 20000.0) return c.size = a, true;
		if (w == "tail" && a >= 100.0 && a <= 400000.0) return c.tail = a, true;
		if (w == "brightness" && a > 0.0 && a <= 20.0) return c.brightness = a, true;
		if (w == "seed") return c.seed = static_cast<int>(a), true;
		return false;
	});
	if (!e.empty()) return e;
	s.comets.push_back(c);
	return std::string();
}

// An `asteroids` line (film_cosmos.h). "" or what is wrong with it.
inline std::string beltLine(Spec &s, const std::vector<std::string> &t) {
	cosmos::Belt b;
	const std::string e = bodyLine(t, "`asteroids NAME at X Y Z [kind ring|field] [radius R] [width W] [count N] [size S] [tilt DEGREES] [turn DEGREES] [spin S] [seed N]` "
									  "(radius 100 to 1000000 m, count 1 to 20000, size 1 to 20000 m)",
								   cosmos::beltKinds(), b.name, b.kind, b.at, [&](const std::string &w, double a) {
									   if (w == "radius" && a >= 100.0 && a <= 1000000.0) return b.radius = a, true;
									   if (w == "width" && a > 0.0 && a <= 1000000.0) return b.width = a, true;
									   if (w == "count" && a >= 1.0 && a <= 20000.0) return b.count = static_cast<int>(a), true;
									   if (w == "size" && a >= 1.0 && a <= 20000.0) return b.size = a, true;
									   if (w == "tilt" && a >= -90.0 && a <= 90.0) return b.tilt = a, true;
									   if (w == "turn") return b.turn = a, true;
									   if (w == "spin" && a >= -10.0 && a <= 10.0) return b.spin = a, true;
									   if (w == "seed") return b.seed = static_cast<int>(a), true;
									   return false;
								   });
	if (!e.empty()) return e;
	s.belts.push_back(b);
	return std::string();
}

// A `remnant` line (film_cosmos.h). "" or what is wrong with it.
inline std::string remnantLine(Spec &s, const std::vector<std::string> &t) {
	cosmos::Remnant m;
	bool sized = false;
	const std::string e = bodyLine(t, "`remnant NAME at X Y Z size S [kind supernova|pulsar|planetary] [blast T] [period P] [tilt DEGREES] [brightness B] [seed N]` "
									  "(size 100 to 400000 m, period 0.05 to 600 s)",
								   cosmos::remnantKinds(), m.name, m.kind, m.at, [&](const std::string &w, double a) {
									   if (w == "size" && a >= 100.0 && a <= 400000.0) return m.size = a, sized = true;
									   if (w == "blast") return m.blast = a, true;
									   if (w == "period" && a >= 0.05 && a <= 600.0) return m.period = a, true;
									   if (w == "tilt" && a >= -90.0 && a <= 90.0) return m.tilt = a, true;
									   if (w == "brightness" && a > 0.0 && a <= 20.0) return m.brightness = a, true;
									   if (w == "seed") return m.seed = static_cast<int>(a), true;
									   return false;
								   });
	if (!e.empty()) return e;
	if (!sized) return "`remnant NAME at X Y Z size S ...`: how big it is";
	s.remnants.push_back(m);
	return std::string();
}

// A `ship` line (film_cosmos.h). "" or what is wrong with it.
inline std::string shipLine(Spec &s, const std::vector<std::string> &t) {
	const std::string usage = "`ship NAME at X Y Z [kind shuttle|fighter|freighter|station|wheel] [size S] [move DX DY DZ] [heading DEGREES] [pitch DEGREES] [spin S] "
							  "[engines on|off] [lights on|off] [colour R G B] [seed N]` (size 1 to 20000 m, colour 0 to 1)";
	cosmos::Ship sh;
	std::vector<std::string> rest;   // (its three-number words first: what is left is a body's line)
	for (std::size_t i = 0; i < t.size(); ++i) {
		if ((t[i] == "move" || t[i] == "colour") && i + 3 < t.size()) {
			double a = 0.0, b = 0.0, d = 0.0;
			if (!detail::number(t[i + 1], a) || !detail::number(t[i + 2], b) || !detail::number(t[i + 3], d)) return "\"" + t[i] + "\": " + usage;
			if (t[i] == "colour" && (std::min({a, b, d}) < 0.0 || std::max({a, b, d}) > 1.0)) return "\"colour\": " + usage;
			(t[i] == "move" ? sh.move : sh.colour) = {a, b, d};
			i += 3;
		} else {
			rest.push_back(t[i]);
		}
	}
	const std::string e = bodyLine(
		rest, usage, cosmos::shipKinds(), sh.name, sh.kind, sh.at,
		[&](const std::string &w, double a) {
			if (w == "size" && a >= 1.0 && a <= 20000.0) return sh.size = a, true;
			if (w == "heading") return sh.heading = a, true;
			if (w == "pitch" && a >= -90.0 && a <= 90.0) return sh.pitch = a, true;
			if (w == "spin" && a >= -20.0 && a <= 20.0) return sh.spin = a, true;
			if (w == "seed") return sh.seed = static_cast<int>(a), true;
			return false;
		},
		[&](const std::string &w, const std::string &v) {
			if ((w == "engines" || w == "lights") && (v == "on" || v == "off")) {
				if (w == "engines") sh.engines = v == "on" ? 1 : 0;
				else sh.lights = v == "on";
				return true;
			}
			return false;
		});
	if (!e.empty()) return e;
	s.ships.push_back(sh);
	return std::string();
}

// An `astronaut` line (film_cosmos.h). "" or what is wrong with it.
inline std::string astronautLine(Spec &s, const std::vector<std::string> &t) {
	const std::string usage = "`astronaut NAME at X Y Z [move DX DY DZ] [tumble DEGREES] [size H] [suit R G B] [tether X Y Z | tether SHIP] [floaters N] [lamps on|off] [seed N]` "
							  "(size 0.3 to 20 m, floaters 0 to 40, a ship named before it)";
	cosmos::Astronaut a;
	std::vector<std::string> rest;   // (its three-number words first, and a tether: what is left is a body's line)
	for (std::size_t i = 0; i < t.size(); ++i) {
		double x = 0.0, y = 0.0, z = 0.0;
		const bool three = i + 3 < t.size() && detail::number(t[i + 1], x) && detail::number(t[i + 2], y) && detail::number(t[i + 3], z);
		if ((t[i] == "move" || t[i] == "suit") && i + 3 < t.size()) {
			if (!three || (t[i] == "suit" && (std::min({x, y, z}) < 0.0 || std::max({x, y, z}) > 1.0))) return "\"" + t[i] + "\": " + usage;
			(t[i] == "move" ? a.move : a.suit) = {x, y, z};
			i += 3;
		} else if (t[i] == "tether" && three) {
			a.tether = {x, y, z}, a.tethered = true;
			i += 3;
		} else if (t[i] == "tether" && i + 1 < t.size()) {
			const bool known = std::any_of(s.ships.begin(), s.ships.end(), [&](const cosmos::Ship &sh) { return sh.name == t[i + 1]; });
			if (!known) return "\"tether " + t[i + 1] + "\": no ship of that name before it - " + usage;
			a.tetherShip = t[i + 1], a.tethered = true;
			i += 1;
		} else {
			rest.push_back(t[i]);
		}
	}
	std::string kind;
	const std::string e = bodyLine(
		rest, usage, {}, a.name, kind, a.at,
		[&](const std::string &w, double v) {
			if (w == "tumble" && v >= -360.0 && v <= 360.0) return a.tumble = v, true;
			if (w == "size" && v >= 0.3 && v <= 20.0) return a.size = v, true;
			if (w == "floaters" && v >= 0.0 && v <= 40.0) return a.floaters = static_cast<int>(v), true;
			if (w == "seed") return a.seed = static_cast<int>(v), true;
			return false;
		},
		[&](const std::string &w, const std::string &v) {
			if (w == "lamps" && (v == "on" || v == "off")) return a.lamps = v == "on", true;
			return false;
		});
	if (!e.empty()) return e;
	s.astronauts.push_back(a);
	return std::string();
}

// An `orbit` line (film_orbits.h): a ship named before it goes round a planet (or a point) instead of along its `move`. "" or what is wrong with it.
inline std::string orbitLine(Spec &s, const std::vector<std::string> &t) {
	using detail::number;
	const std::string usage = cosmos::orbitUsage();
	if (t.size() < 4 || t[2] != "around") return usage;
	cosmos::Ship *ship = nullptr;
	for (cosmos::Ship &sh : s.ships)
		if (sh.name == t[1]) ship = &sh;
	if (!ship) return "\"" + t[1] + "\": no ship of that name before it - " + usage;
	cosmos::Orbit o;
	o.on = true;
	std::size_t i = 3;
	Vec3 c;
	if (i + 2 < t.size() && number(t[i], c.x) && number(t[i + 1], c.y) && number(t[i + 2], c.z)) {
		o.centre = c, i += 3;
	} else {
		const auto it = std::find_if(s.planets.begin(), s.planets.end(), [&](const planets::Planet &p) { return p.name == t[i]; });
		if (it == s.planets.end()) return "\"around " + t[i] + "\": no planet of that name before it - " + usage;
		o.around = t[i], o.centre = it->at, ++i;
	}
	bool radius = false;
	while (i < t.size()) {
		double a = 0.0, b = 0.0;
		if (t[i] == "backwards") {
			o.backwards = true, ++i;
		} else if (t[i] == "to" && i + 3 < t.size() && number(t[i + 1], a) && t[i + 2] == "at" && number(t[i + 3], b) && a > 0.0 && b >= 0.0) {
			o.to = a, o.at = b, i += 4;
		} else if (i + 1 < t.size() && number(t[i + 1], a)) {
			if (t[i] == "radius" && a > 0.0) o.radius = a, radius = true;
			else if (t[i] == "apoapsis" && a > 0.0) o.apo = a;
			else if (t[i] == "period" && a >= 1.0 && a <= 100000.0) o.period = a;
			else if (t[i] == "incline" && a >= -180.0 && a <= 180.0) o.incline = a;
			else if (t[i] == "node") o.node = a;
			else if (t[i] == "phase") o.phase = a;
			else return "\"" + t[i] + "\": " + usage;
			i += 2;
		} else {
			return "\"" + t[i] + "\": " + usage;
		}
	}
	if (!radius) return "how far out (`radius R`): " + usage;
	if (o.apo > 0.0 && o.apo <= o.radius) return "\"apoapsis\" must be beyond the radius: " + usage;
	if (o.at >= 0.0 && o.apo > 0.0) return "a transfer (`to R2 at T`) only from a circle: " + usage;
	ship->orbit = o;
	return std::string();
}

// A `burn SHIP T0 T1` line: its engines fire from T0 to T1 (nature's time). "" or what is wrong with it.
inline std::string burnLine(Spec &s, const std::vector<std::string> &t) {
	const std::string usage = "`burn SHIP T0 T1` (a ship named before it; T1 after T0)";
	double a = 0.0, b = 0.0;
	if (t.size() != 4 || !detail::number(t[2], a) || !detail::number(t[3], b) || b <= a) return usage;
	for (cosmos::Ship &sh : s.ships)
		if (sh.name == t[1]) return sh.burns.push_back({a, b}), std::string();
	return "\"" + t[1] + "\": no ship of that name before it - " + usage;
}

// A colour word or three numbers at t[i]: red, green, blue or R G B (0 to 1). How many words it took (0: not a colour).
inline std::size_t colourAt(const std::vector<std::string> &t, std::size_t i, Vec3 &out) {
	if (i >= t.size()) return 0;
	if (t[i] == "red") return out = {1.0, 0.15, 0.1}, 1;
	if (t[i] == "green") return out = {0.2, 1.0, 0.25}, 1;
	if (t[i] == "blue") return out = {0.25, 0.5, 1.0}, 1;
	Vec3 c;
	if (i + 2 < t.size() && detail::number(t[i], c.x) && detail::number(t[i + 1], c.y) && detail::number(t[i + 2], c.z) && std::min({c.x, c.y, c.z}) >= 0.0 && std::max({c.x, c.y, c.z}) <= 1.0)
		return out = c, 3;
	return 0;
}
inline bool knownShip(const Spec &s, const std::string &name) {
	return std::any_of(s.ships.begin(), s.ships.end(), [&](const cosmos::Ship &sh) { return sh.name == name; });
}

// A `laser` line (film_battle.h). "" or what is wrong with it.
inline std::string laserLine(Spec &s, const std::vector<std::string> &t) {
	using detail::number;
	const std::string usage = battle::laserUsage();
	if (t.size() < 4 || t[2] != "at") return usage;
	battle::Laser l;
	l.from = t[1];
	if (!knownShip(s, l.from)) return "\"" + l.from + "\": no ship of that name before it - " + usage;
	if (!number(t[3], l.at)) return "\"at " + t[3] + "\": " + usage;
	for (std::size_t i = 4; i < t.size();) {
		double a = 0.0;
		Vec3 p;
		if (t[i] == "to" && i + 3 < t.size() && number(t[i + 1], p.x) && number(t[i + 2], p.y) && number(t[i + 3], p.z)) {
			l.toPoint = p, l.toIsPoint = true, i += 4;
		} else if (t[i] == "to" && i + 1 < t.size()) {
			if (!knownShip(s, t[i + 1])) return "\"to " + t[i + 1] + "\": no ship of that name before it - " + usage;
			l.to = t[i + 1], i += 2;
		} else if (t[i] == "colour") {
			const std::size_t n = colourAt(t, i + 1, l.colour);
			if (!n) return "\"colour\": " + usage;
			i += 1 + n;
		} else if (t[i] == "count" && i + 3 < t.size() && number(t[i + 1], a) && a >= 1.0 && a <= 50.0 && t[i + 2] == "every" && number(t[i + 3], l.every) && l.every > 0.0) {
			l.count = static_cast<int>(a), i += 4;
		} else if (t[i] == "speed" && i + 1 < t.size() && number(t[i + 1], a) && a > 0.0) {
			l.speed = a, i += 2;
		} else {
			return "\"" + t[i] + "\": " + usage;
		}
	}
	s.battle.lasers.push_back(l);
	return std::string();
}

// An `explode` line (film_battle.h): a ship blown up (gone from then on), or a blast at a place. "" or what is wrong with it.
inline std::string explodeLine(Spec &s, const std::vector<std::string> &t) {
	using detail::number;
	const std::string usage = battle::explodeUsage();
	battle::Explosion e;
	std::size_t i = 1;
	Vec3 p;
	if (t.size() > 3 && number(t[1], p.x) && number(t[2], p.y) && number(t[3], p.z)) {
		e.at = p, i = 4;
	} else if (t.size() > 1 && knownShip(s, t[1])) {
		e.ship = t[1], i = 2;
	} else {
		return (t.size() > 1 ? "\"" + t[1] + "\": no ship of that name before it - " : std::string()) + usage;
	}
	bool when = false;
	for (; i < t.size(); i += 2) {
		double a = 0.0;
		if (i + 1 >= t.size() || !number(t[i + 1], a)) return "\"" + t[i] + "\": " + usage;
		if (t[i] == "at") e.t = a, when = true;
		else if (t[i] == "size" && a > 0.0) e.size = a;
		else if (t[i] == "seed") e.seed = static_cast<int>(a);
		else return "\"" + t[i] + "\": " + usage;
	}
	if (!when) return "when (`at T`): " + usage;
	e.seed += static_cast<int>(s.battle.explosions.size()) * 7;
	s.battle.explosions.push_back(e);
	for (cosmos::Ship &sh : s.ships)
		if (sh.name == e.ship && (sh.goneAt < 0.0 || e.t < sh.goneAt)) sh.goneAt = e.t;
	return std::string();
}

// A `shield` line (film_battle.h). "" or what is wrong with it.
inline std::string shieldLine(Spec &s, const std::vector<std::string> &t) {
	const std::string usage = battle::shieldUsage();
	if (t.size() < 2 || !knownShip(s, t[1])) return (t.size() > 1 ? "\"" + t[1] + "\": no ship of that name before it - " : std::string()) + usage;
	battle::Shield sh;
	sh.ship = t[1];
	for (std::size_t i = 2; i < t.size();) {
		double a = 0.0;
		if (t[i] == "radius" && i + 1 < t.size() && detail::number(t[i + 1], a) && a > 0.0) {
			sh.radius = a, i += 2;
		} else if (t[i] == "colour") {
			const std::size_t n = colourAt(t, i + 1, sh.colour);
			if (!n) return "\"colour\": " + usage;
			i += 1 + n;
		} else {
			return "\"" + t[i] + "\": " + usage;
		}
	}
	s.battle.shields.push_back(sh);
	return std::string();
}

// One space line: what it says goes into `s`. "" or what is wrong with it.
inline std::string spaceLine(Spec &s, const std::string &verb, const std::string &line) {
	const std::vector<std::string> t = detail::tokens(line);
	if (verb == "space") return spaceSetLine(s, t);
	if (verb == "planet") return planetLine(s, t);
	if (verb == "moon") return moonLine(s, t);
	if (verb == "nebula") return nebulaLine(s, t);
	if (verb == "galaxy") return galaxyLine(s, t);
	if (verb == "star") return starLine(s, t);
	if (verb == "blackhole") return blackHoleLine(s, t);
	if (verb == "comet") return cometLine(s, t);
	if (verb == "asteroids") return beltLine(s, t);
	if (verb == "remnant") return remnantLine(s, t);
	if (verb == "ship") return shipLine(s, t);
	if (verb == "astronaut") return astronautLine(s, t);
	if (verb == "orbit") return orbitLine(s, t);
	if (verb == "burn") return burnLine(s, t);
	if (verb == "label" || verb == "orbitline" || verb == "scalebar" || verb == "compare") {   // an explainer's drawings (film_explain.h)
		const std::string usage = verb == "label" ? explain::labelUsage() : verb == "orbitline" ? explain::orbitLineUsage() : verb == "scalebar" ? explain::scaleBarUsage() : explain::compareUsage();
		explain::Label l;
		explain::OrbitLine o;
		explain::ScaleBar b;
		explain::Compare c;
		std::size_t i = 2;
		if (t.size() < 2) return usage;
		if (verb == "label") l.text = t[1];
		else if (verb == "orbitline") o.of = t[1];
		else if (verb == "scalebar" && !(detail::number(t[1], b.length) && b.length > 0.0)) return usage;
		else if (verb == "compare") {
			for (i = 1; i < t.size() && t[i] != "at"; ++i) c.bodies.push_back(t[i]);
			if (c.bodies.empty()) return usage;
		}
		bool placed = verb == "orbitline";
		double from = 0.0, to = 1e9;
		Vec3 colour{-1.0, -1.0, -1.0};
		while (i < t.size()) {
			Vec3 v;
			double a = 0.0;
			const bool three = i + 3 < t.size() && detail::number(t[i + 1], v.x) && detail::number(t[i + 2], v.y) && detail::number(t[i + 3], v.z);
			if (t[i] == "at" && three && verb != "orbitline") {
				l.at = b.at = c.at = v, placed = true, i += 4;
			} else if (t[i] == "on" && verb == "label" && i + 1 < t.size()) {
				l.on = t[i + 1], placed = true, i += 2;
			} else if (t[i] == "colour" && three && std::min({v.x, v.y, v.z}) >= 0.0 && std::max({v.x, v.y, v.z}) <= 1.0) {
				colour = v, i += 4;
			} else if (t[i] == "dashed" && verb == "orbitline") {
				o.dashed = true, ++i;
			} else if (t[i] == "text" && verb == "scalebar" && i + 1 < t.size()) {
				b.text = t[i + 1], i += 2;
			} else if (i + 1 < t.size() && detail::number(t[i + 1], a)) {
				if (t[i] == "from") from = a;
				else if (t[i] == "to") to = a;
				else if (t[i] == "size" && verb == "label" && a >= 0.01 && a <= 0.2) l.size = a;
				else if (t[i] == "height" && verb == "compare" && a > 0.0) c.height = a;
				else return "\"" + t[i] + "\": " + usage;
				i += 2;
			} else {
				return "\"" + t[i] + "\": " + usage;
			}
		}
		if (!placed) return (verb == "label" ? "what it names (`on NAME`) or where it is (`at X Y Z`): " : "where it is (`at X Y Z`): ") + usage;
		if (to <= from) return "it must go (`to`) after it comes (`from`): " + usage;
		if (verb == "label") {
			l.from = from, l.to = to;
			if (colour.x >= 0.0) l.colour = colour;
			s.explain.labels.push_back(l);
		} else if (verb == "orbitline") {
			o.from = from, o.to = to;
			if (colour.x >= 0.0) o.colour = colour;
			s.explain.orbits.push_back(o);
		} else if (verb == "scalebar") {
			b.from = from, b.to = to;
			if (colour.x >= 0.0) b.colour = colour;
			s.explain.bars.push_back(b);
		} else {
			c.from = from, c.to = to;
			if (colour.x >= 0.0) c.colour = colour;
			s.explain.compares.push_back(c);
		}
		return std::string();
	}
	if (verb == "hologram") {   // (film_screens.h)
		const std::string usage = screens::hologramUsage();
		screens::Hologram h;
		if (t.size() < 3 || std::find(screens::hologramKinds().begin(), screens::hologramKinds().end(), t[2]) == screens::hologramKinds().end()) return usage;
		h.name = t[1], h.kind = t[2];
		bool placed = false;
		for (std::size_t i = 3; i < t.size();) {
			Vec3 v;
			double a = 0.0;
			const bool three = i + 3 < t.size() && detail::number(t[i + 1], v.x) && detail::number(t[i + 2], v.y) && detail::number(t[i + 3], v.z);
			if (t[i] == "at" && three) {
				h.at = v, placed = true, i += 4;
			} else if (t[i] == "colour" && three && std::min({v.x, v.y, v.z}) >= 0.0 && std::max({v.x, v.y, v.z}) <= 1.0) {
				h.colour = v, i += 4;
			} else if (t[i] == "ship" && i + 1 < t.size() && std::find(cosmos::shipKinds().begin(), cosmos::shipKinds().end(), t[i + 1]) != cosmos::shipKinds().end()) {
				h.ship = t[i + 1], i += 2;
			} else if (i + 1 < t.size() && detail::number(t[i + 1], a)) {
				if (t[i] == "size" && a >= 0.05 && a <= 50.0) h.size = a;
				else if (t[i] == "spin" && std::fabs(a) <= 720.0) h.spin = a;
				else if (t[i] == "flicker" && a >= 0.0 && a <= 5.0) h.flicker = a;
				else if (t[i] == "seed") h.seed = static_cast<int>(a);
				else return "\"" + t[i] + "\": " + usage;
				i += 2;
			} else {
				return "\"" + t[i] + "\": " + usage;
			}
		}
		if (!placed) return "where its projector stands (`at X Y Z`): " + usage;
		s.holograms.push_back(h);
		return std::string();
	}
	if (verb == "screen") {   // (film_screens.h)
		const std::string usage = screens::screenUsage();
		screens::Screen sc;
		if (t.size() < 2) return usage;
		sc.name = t[1];
		bool placed = false, shows = false;
		for (std::size_t i = 2; i < t.size();) {
			Vec3 v;
			double a = 0.0, b = 0.0;
			const bool three = i + 3 < t.size() && detail::number(t[i + 1], v.x) && detail::number(t[i + 2], v.y) && detail::number(t[i + 3], v.z);
			if ((t[i] == "at" || t[i] == "centre") && three) {
				if (t[i] == "at") sc.at = v, placed = true;
				else sc.centre = v, sc.hasCentre = true;
				i += 4;
			} else if (t[i] == "show" && i + 1 < t.size() && std::find(screens::screenShows().begin(), screens::screenShows().end(), t[i + 1]) != screens::screenShows().end()) {
				sc.show = t[i + 1], shows = true, i += 2;
				if (sc.show == "picture") {
					if (i >= t.size()) return "which picture (`show picture FILE`): " + usage;
					sc.file = t[i++];
				}
			} else if (t[i] == "size" && i + 2 < t.size() && detail::number(t[i + 1], a) && detail::number(t[i + 2], b) && a > 0.01 && b > 0.01 && a <= 100.0 && b <= 100.0) {
				sc.width = a, sc.height = b, i += 3;
			} else if (i + 1 < t.size() && detail::number(t[i + 1], a)) {
				if (t[i] == "facing") sc.facing = a;
				else if (t[i] == "tilt" && std::fabs(a) <= 90.0) sc.tilt = a;
				else if (t[i] == "bright" && a >= 0.0 && a <= 100.0) sc.bright = a;
				else if (t[i] == "range" && a > 0.0) sc.range = a;
				else if (t[i] == "seed") sc.seed = static_cast<int>(a);
				else return "\"" + t[i] + "\": " + usage;
				i += 2;
			} else {
				return "\"" + t[i] + "\": " + usage;
			}
		}
		if (!placed) return "where it hangs (`at X Y Z`): " + usage;
		if (!shows) return "what it shows (`show ...`): " + usage;
		s.screens.push_back(sc);
		return std::string();
	}
	if (verb == "interior") {   // (film_interior.h)
		const std::string usage = interior::usage();
		interior::Interior in;
		if (t.size() < 2 || std::find(interior::kinds().begin(), interior::kinds().end(), t[1]) == interior::kinds().end()) return usage;
		in.kind = t[1];
		if (in.kind == "airlock") in.length = 3.0;
		bool placed = false;
		for (std::size_t i = 2; i < t.size();) {
			Vec3 v;
			double a = 0.0;
			const bool three = i + 3 < t.size() && detail::number(t[i + 1], v.x) && detail::number(t[i + 2], v.y) && detail::number(t[i + 3], v.z);
			if (t[i] == "at" && three) {
				in.at = v, placed = true, i += 4;
			} else if (t[i] == "light" && three && std::min({v.x, v.y, v.z}) >= 0.0 && std::max({v.x, v.y, v.z}) <= 1.0) {
				in.light = v, i += 4;
			} else if (i + 1 < t.size() && detail::number(t[i + 1], a)) {
				if (t[i] == "facing") in.facing = a;
				else if (t[i] == "length" && a >= 2.0 && a <= 200.0) in.length = a;
				else if (t[i] == "cycle" && a >= 0.0 && in.kind == "airlock") in.cycle = a;
				else if (t[i] == "seed") in.seed = static_cast<int>(a);
				else return "\"" + t[i] + "\": " + usage;
				i += 2;
			} else {
				return "\"" + t[i] + "\": " + usage;
			}
		}
		if (!placed) return "where it stands (`at X Y Z`): " + usage;
		s.interiors.push_back(in);
		return std::string();
	}
	if (verb == "blob") {   // (film_zerog.h)
		const std::string usage = zerog::blobUsage();
		zerog::Blob b;
		if (t.size() < 2) return usage;
		b.name = t[1];
		bool placed = false;
		for (std::size_t i = 2; i < t.size();) {
			Vec3 v;
			double a = 0.0, c = 0.0;
			const bool three = i + 3 < t.size() && detail::number(t[i + 1], v.x) && detail::number(t[i + 2], v.y) && detail::number(t[i + 3], v.z);
			if ((t[i] == "at" || t[i] == "move") && three) {
				(t[i] == "at" ? b.at : b.move) = v, placed = placed || t[i] == "at", i += 4;
			} else if (t[i] == "on" && i + 1 < t.size()) {
				b.on = t[i + 1], placed = true, i += 2;
			} else if (t[i] == "grow" && i + 2 < t.size() && detail::number(t[i + 1], a) && detail::number(t[i + 2], c) && c > a) {
				b.grow0 = a, b.grow1 = c, i += 3;
			} else if (i + 1 < t.size() && detail::number(t[i + 1], a)) {
				if (t[i] == "size" && a >= 0.002 && a <= 2.0) b.size = a;
				else if (t[i] == "split" && a >= 0.0) b.split = a;
				else if (t[i] == "seed") b.seed = static_cast<int>(a);
				else return "\"" + t[i] + "\": " + usage;
				i += 2;
			} else {
				return "\"" + t[i] + "\": " + usage;
			}
		}
		if (!placed) return "where it is (`at X Y Z`, or `on CHARACTER`): " + usage;
		s.blobs.push_back(b);
		return std::string();
	}
	if (verb == "drift") {   // (film_zerog.h)
		const std::string usage = zerog::driftUsage();
		zerog::Drift d;
		double n = 0.0;
		if (t.size() < 3 || !detail::number(t[2], n) || n < 1.0 || n > 200.0) return usage;
		d.name = t[1], d.count = static_cast<int>(n);
		bool placed = false;
		for (std::size_t i = 3; i < t.size();) {
			Vec3 v;
			double a = 0.0;
			const bool three = i + 3 < t.size() && detail::number(t[i + 1], v.x) && detail::number(t[i + 2], v.y) && detail::number(t[i + 3], v.z);
			if (t[i] == "at" && three) {
				d.centre = v, placed = true, i += 4;
			} else if (t[i] == "box" && three && std::min({v.x, v.y, v.z}) > 0.0) {
				d.half = v * 0.5, i += 4;
			} else if (t[i] == "kind" && i + 1 < t.size() && std::find(zerog::driftKinds().begin(), zerog::driftKinds().end(), t[i + 1]) != zerog::driftKinds().end()) {
				d.kind = t[i + 1], i += 2;
			} else if (i + 1 < t.size() && detail::number(t[i + 1], a)) {
				if (t[i] == "size" && a > 0.0 && a <= 5.0) d.size = a;
				else if (t[i] == "speed" && a >= 0.0 && a <= 10.0) d.speed = a;
				else if (t[i] == "seed") d.seed = static_cast<int>(a);
				else return "\"" + t[i] + "\": " + usage;
				i += 2;
			} else {
				return "\"" + t[i] + "\": " + usage;
			}
		}
		if (!placed) return "where they float (`at X Y Z`): " + usage;
		s.drifts.push_back(d);
		return std::string();
	}
	if (verb == "gravity") {   // `gravity G` (m/s^2; film_spec.h)
		double g = 0.0;
		if (t.size() != 2 || !detail::number(t[1], g) || g <= 0.0 || g > 30.0) return "`gravity G`: how strongly things fall, in m/s^2 (the Earth's 9.81, the Moon's 1.62, Mars's 3.71; above 0, up to 30)";
		s.gravity = g;
		return std::string();
	}
	if (verb == "footprints") {   // `footprints [NAME ...]`: everyone's, or these characters'
		if (t.size() == 1) s.footprints.push_back("*");
		for (std::size_t i = 1; i < t.size(); ++i) s.footprints.push_back(t[i]);
		return std::string();
	}
	if (verb == "laser") return laserLine(s, t);
	if (verb == "explode") return explodeLine(s, t);
	if (verb == "shield") return shieldLine(s, t);
	if (verb == "descent") {   // (film_voyage.h)
		cosmos::Descent d;
		const std::string e = cosmos::descentLine(t, d);
		if (e.empty()) s.descents.push_back(d);
		return e;
	}
	return "unknown space line \"" + verb + "\"";
}

}  // namespace film_file
}  // namespace cartoon
