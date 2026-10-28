#pragma once
// film_parse_nature.h -- the nature lines of a film file (its grammar: film_parse.h): the land, water, clouds, forests, flowers, wildlife, the sky and its
// events, weather, particles and the set's sound, and the wet and snowy ground, gusts, mist, surf, storms, the air, fog, the world under water, fire, the
// land in action, slow motion, time-lapse and exposure. parse() hands every line to natureLine() first.

#include "film_parse_space.h"   // (and film_spec.h)

namespace cartoon {
namespace film_file {

// A `fogbank` line: fog lying on the land (film_clouds.h). "" or what is wrong with it.
inline std::string fogBankLine(Spec &s, const std::string &line) {
	using detail::number;
	const auto fail = [](const std::string &what) { return what; };
	const std::vector<std::string> t = detail::tokens(line);
	clouds::FogBank f;
	bool placed = false;
	for (std::size_t i = 1; i < t.size();) {
		double a = 0.0, b = 0.0;
		if ((t[i] == "at" || t[i] == "during") && i + 2 < t.size() && number(t[i + 1], a) && number(t[i + 2], b)) {
			if (t[i] == "at") f.x = a, f.z = b, placed = true;
			else if (b > a) f.t0 = a, f.t1 = b;
			else return fail("`during T0 T1`: the fog settles in at T0 and has burnt off by T1 (T1 after T0)");
			i += 3;
		} else if (i + 1 < t.size() && number(t[i + 1], a)) {
			if (t[i] == "size" && a >= 20.0 && a <= 20000.0) f.size = a;
			else if (t[i] == "depth" && a >= 2.0 && a <= 2000.0) f.depth = a;
			else if (t[i] == "density" && a > 0.0 && a <= 1.0) f.density = a;
			else if (t[i] == "wind") f.wind = a, f.windSet = true;
			else if (t[i] == "speed" && a >= 0.0) f.speed = a, f.speedSet = true;
			else if (t[i] == "seed") f.seed = static_cast<int>(a);
			else return fail("\"" + t[i] + "\": a fog bank takes size (20 to 20000 m), depth (2 to 2000 m), density (up to 1 a metre), wind, speed, during T0 T1 and seed");
			i += 2;
		} else {
			return fail("`fogbank at X Z [size W] [depth D] [density S] [wind DEGREES] [speed S] [during T0 T1] [seed N]`: fog lying on the land, D metres deep");
		}
	}
	if (!placed) return fail("`fogbank at X Z ...`: where the fog lies");
	s.fogBanks.push_back(f);
	return std::string();
}

// A `fire`, `lava`, `volcano` or `wildfire` line (film_fire.h). "" or what is wrong with it.
inline std::string fireLine(Spec &s, const std::string &verb, const std::string &line) {
	using detail::number;
	const auto fail = [](const std::string &what) { return what; };
	const std::vector<std::string> t = detail::tokens(line);
	fire::Fire fi;
	fire::Lava la;
	fire::Volcano vo;
	fire::Wildfire wf;
	std::size_t i = 1;
	double a = 0.0, b = 0.0, c = 0.0, d = 0.0;
	const std::string usage = verb == "fire" ? "`fire at X Y Z [size S] [during T0 T1] [smoke off] [seed N]`"
							  : verb == "lava" ? "`lava from X Z [width W] [speed S] [start T]` (down the land from a vent: it needs a `terrain`)"
							  : verb == "volcano" ? "`volcano at X Z [height H] [radius R] [erupting T0 T1] [column H] [wind DEGREES] [lightning N] [ashfall A] [flow T [toward DEGREES]]` (raised in the land: it needs a `terrain`)"
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
			else if (verb == "volcano" && t[i] == "column") {
				if (a < 300.0 || a > 20000.0) return fail("a volcano's ash column climbs 300 to 20000 m");
				vo.column = a;
			} else if (verb == "volcano" && t[i] == "wind") vo.wind = a, vo.windSet = true;
			else if (verb == "volcano" && t[i] == "lightning") {
				if (a < 1.0 || a > 100.0) return fail("lightning in an ash column: 1 to 100 strikes");
				vo.lightning = static_cast<int>(a);
			} else if (verb == "volcano" && t[i] == "ashfall") {
				if (a <= 0.0 || a > 4.0) return fail("ashfall: an amount above 0, up to 4");
				vo.ashfall = a;
			} else if (verb == "volcano" && t[i] == "flow") {
				fire::PyroclasticFlow f;
				f.t = a;
				vo.flows.push_back(f);
			} else if (verb == "volcano" && t[i] == "toward") {
				if (vo.flows.empty()) return fail("`toward DEGREES` is the way a flow goes: after `flow T`");
				vo.flows.back().toward = a, vo.flows.back().towardSet = true;
			}
			else if (verb == "wildfire" && t[i] == "wind") wf.wind = a, wf.windSet = true;
			else return fail("\"" + t[i] + "\": " + usage);
			i += 2;
		} else {
			return fail("\"" + t[i] + "\": " + usage);
		}
	}
	if (verb == "fire") s.fires.push_back(fi);
	else if (verb == "lava") s.lavas.push_back(la);
	else if (verb == "volcano") {
		if (vo.lightning > 0 && vo.column <= 0.0) return fail("lightning flickers in an ash column: give the volcano a `column H`");
		for (const fire::PyroclasticFlow &f : vo.flows)
			if (f.t < vo.t0 || f.t > vo.t1) return fail("a pyroclastic flow comes while the volcano erupts: `flow T` with T in its `erupting T0 T1`");
		s.volcanoes.push_back(vo);
	} else s.wildfires.push_back(wf);
	return std::string();
}

// A `whale` or `dolphins` line (film_giants.h). "" or what is wrong with it.
inline std::string giantLine(Spec &s, const std::string &verb, const std::string &line) {
	using detail::number;
	const auto fail = [](const std::string &what) { return what; };
	const std::vector<std::string> t = detail::tokens(line);
	const bool whale = verb == "whale";
	const std::string usage = whale ? "`whale at X Z [toward DEGREES] [length L] [speed S] [during T0 T1] [breach T] ... [seed N]`"
									: "`dolphins COUNT at X Z [toward DEGREES] [speed S] [during T0 T1] [seed N]`";
	giants::Whale w;
	giants::Dolphins d;
	std::size_t i = 1;
	double a = 0.0, b = 0.0;
	if (!whale) {
		if (t.size() < 2 || !number(t[1], a) || a < 1.0 || a > 40.0 || a != std::floor(a)) return fail(usage + ": 1 to 40 dolphins");
		d.count = static_cast<int>(a);
		i = 2;
	}
	bool placed = false;
	for (; i < t.size();) {
		if ((t[i] == "at" || t[i] == "during") && i + 2 < t.size() && number(t[i + 1], a) && number(t[i + 2], b)) {
			if (t[i] == "at") (whale ? w.x : d.x) = a, (whale ? w.z : d.z) = b, placed = true;
			else if (b > a) (whale ? w.t0 : d.t0) = a, (whale ? w.t1 : d.t1) = b;
			else return fail("`during T0 T1`: T1 after T0");
			i += 3;
		} else if (i + 1 < t.size() && number(t[i + 1], a)) {
			if (t[i] == "toward") (whale ? w.toward : d.toward) = a;
			else if (t[i] == "seed") (whale ? w.seed : d.seed) = static_cast<int>(a);
			else if (whale && t[i] == "length" && a >= 4.0 && a <= 30.0) w.length = a;
			else if (whale && t[i] == "speed" && a >= 0.0 && a <= 8.0) w.speed = a;
			else if (whale && t[i] == "breach") w.breaches.push_back(a);
			else if (!whale && t[i] == "speed" && a >= 0.5 && a <= 15.0) d.speed = a;
			else return fail("\"" + t[i] + "\": " + usage + (whale ? " (length 4 to 30 m, speed up to 8 m/s)" : " (speed 0.5 to 15 m/s)"));
			i += 2;
		} else {
			return fail("\"" + t[i] + "\": " + usage);
		}
	}
	if (!placed) return fail(usage + ": where it is");
	if (whale) {
		for (double x : w.breaches)
			if (x < w.t0 || x > w.t1) return fail("a whale breaches while it is there: `breach T` with T in its `during T0 T1`");
		std::sort(w.breaches.begin(), w.breaches.end());
		s.whales.push_back(w);
	} else {
		s.pods.push_back(d);
	}
	return std::string();
}

// A `bioluminescence`, `fireflies` or `fungi` line (film_glow.h). "" or what is wrong with it.
inline std::string glowLine(Spec &s, const std::string &verb, const std::string &line) {
	using detail::number;
	const auto fail = [](const std::string &what) { return what; };
	const std::vector<std::string> t = detail::tokens(line);
	const auto colourOk = [](const std::string &c) { return std::find(glow::glowColours().begin(), glow::glowColours().end(), c) != glow::glowColours().end(); };
	double a = 0.0, b = 0.0, c = 0.0, d = 0.0;
	if (verb == "bioluminescence") {
		if (s.bio.on) return fail("a film has one `bioluminescence` line");
		s.bio.on = true;
		for (std::size_t i = 1; i < t.size(); i += 2) {
			if (i + 1 < t.size() && t[i] == "strength" && number(t[i + 1], a) && a > 0.0 && a <= 20.0) s.bio.strength = a;
			else if (i + 1 < t.size() && t[i] == "colour" && colourOk(t[i + 1])) s.bio.colour = t[i + 1];
			else return fail("\"" + t[i] + "\": `bioluminescence [strength S] [colour blue|green|teal]` (strength above 0, up to 20)");
		}
		return std::string();
	}
	const bool flies = verb == "fireflies";
	const std::string usage = flies ? "`fireflies COUNT in X0 Z0 X1 Z1 [height H] [during T0 T1] [together] [brightness B] [seed N]`"
									: "`fungi COUNT in X0 Z0 X1 Z1 [colour green|blue|teal] [brightness B] [seed N]`";
	if (t.size() < 7 || !number(t[1], a) || a < 1.0 || a > (flies ? 2000.0 : 500.0) || a != std::floor(a) || t[2] != "in" || !number(t[3], b) || !number(t[4], c) || !number(t[5], d)) {
		return fail(usage + (flies ? ": 1 to 2000 fireflies" : ": 1 to 500 clusters"));
	}
	double e = 0.0;
	if (!number(t[6], e)) return fail(usage);
	glow::Fireflies f;
	glow::Fungi g;
	(flies ? f.count : g.count) = static_cast<int>(a);
	(flies ? f.x0 : g.x0) = std::min(b, d), (flies ? f.x1 : g.x1) = std::max(b, d), (flies ? f.z0 : g.z0) = std::min(c, e), (flies ? f.z1 : g.z1) = std::max(c, e);
	for (std::size_t i = 7; i < t.size();) {
		if (flies && t[i] == "together") {
			f.together = true;
			++i;
		} else if (flies && t[i] == "during" && i + 2 < t.size() && number(t[i + 1], a) && number(t[i + 2], b) && b > a) {
			f.t0 = a, f.t1 = b;
			i += 3;
		} else if (!flies && t[i] == "colour" && i + 1 < t.size() && colourOk(t[i + 1])) {
			g.colour = t[i + 1];
			i += 2;
		} else if (i + 1 < t.size() && number(t[i + 1], a)) {
			if (t[i] == "brightness" && a > 0.0 && a <= 20.0) (flies ? f.brightness : g.brightness) = a;
			else if (t[i] == "seed") (flies ? f.seed : g.seed) = static_cast<int>(a);
			else if (flies && t[i] == "height" && a >= 0.3 && a <= 20.0) f.height = a;
			else return fail("\"" + t[i] + "\": " + usage);
			i += 2;
		} else {
			return fail("\"" + t[i] + "\": " + usage);
		}
	}
	if (flies) s.fireflies.push_back(f);
	else s.fungi.push_back(g);
	return std::string();
}

// A `glacier`, `iceberg` or `seaice` line (film_ice.h). "" or what is wrong with it.
inline std::string iceLine(Spec &s, const std::string &verb, const std::string &line) {
	using detail::number;
	const auto fail = [](const std::string &what) { return what; };
	const std::vector<std::string> t = detail::tokens(line);
	double a = 0.0, b = 0.0;
	if (verb == "seaice") {
		if (s.seaIce.on) return fail("a film has one `seaice` line");
		s.seaIce.on = true;
		for (std::size_t i = 1; i < t.size(); i += 2) {
			if (i + 1 >= t.size() || !number(t[i + 1], a)) return fail("\"" + t[i] + "\": `seaice [cover C] [floe F] [seed N]` (cover 0 to 1, floes 2 to 200 m across)");
			if (t[i] == "cover" && a > 0.0 && a <= 1.0) s.seaIce.cover = a;
			else if (t[i] == "floe" && a >= 2.0 && a <= 200.0) s.seaIce.floe = a;
			else if (t[i] == "seed") s.seaIce.seed = static_cast<int>(a);
			else return fail("\"" + t[i] + "\": `seaice [cover C] [floe F] [seed N]` (cover 0 to 1, floes 2 to 200 m across)");
		}
		return std::string();
	}
	const bool glacier = verb == "glacier";
	const std::string usage = glacier ? "`glacier from X Z toward DEGREES [length L] [width W] [thick T] [calve T] ... [seed N]`" : "`iceberg at X Z [size S] [seed N]`";
	if (t.size() < 4 || t[1] != (glacier ? "from" : "at") || !number(t[2], a) || !number(t[3], b)) return fail(usage);
	ice::Glacier g;
	ice::Iceberg berg;
	(glacier ? g.x : berg.x) = a, (glacier ? g.z : berg.z) = b;
	for (std::size_t i = 4; i < t.size(); i += 2) {
		if (i + 1 >= t.size() || !number(t[i + 1], a)) return fail("\"" + t[i] + "\": " + usage);
		if (glacier && t[i] == "toward") g.toward = a;
		else if (glacier && t[i] == "length" && a >= 50.0 && a <= 20000.0) g.length = a;
		else if (glacier && t[i] == "width" && a >= 10.0 && a <= 5000.0) g.width = a;
		else if (glacier && t[i] == "thick" && a >= 2.0 && a <= 1000.0) g.thick = a;
		else if (glacier && t[i] == "calve") g.calving.push_back(a);
		else if (!glacier && t[i] == "size" && a >= 1.0 && a <= 2000.0) berg.size = a;
		else if (t[i] == "seed") (glacier ? g.seed : berg.seed) = static_cast<int>(a);
		else return fail("\"" + t[i] + "\": " + usage + (glacier ? " (length 50 to 20000 m, width 10 to 5000 m, thick 2 to 1000 m)" : " (size 1 to 2000 m)"));
	}
	if (glacier) {
		std::sort(g.calving.begin(), g.calving.end());
		s.glaciers.push_back(g);
	} else {
		s.icebergs.push_back(berg);
	}
	return std::string();
}

// A `wind` or `tornado` line (film_wind.h). "" or what is wrong with it.
inline std::string windLine(Spec &s, const std::string &verb, const std::string &line) {
	using detail::number;
	const auto fail = [](const std::string &what) { return what; };
	const std::vector<std::string> t = detail::tokens(line);
	double a = 0.0, b = 0.0, c = 0.0, d = 0.0;
	if (verb == "wind") {
		if (s.wind.on) return fail("a film has one `wind` line");
		if (t.size() != 3 || !number(t[1], a) || !number(t[2], b) || b < 0.0 || b > 60.0)
			return fail("`wind DEGREES SPEED`: the way it blows (a compass bearing: 0 +z, 90 +x) and its speed (0 to 60 m/s)");
		s.wind.on = true, s.wind.toward = a, s.wind.speed = b;
		return std::string();
	}
	const std::string usage = "`tornado from X Z to X Z [during T0 T1] [width W] [height H] [seed N]`";
	wind::Tornado k;
	if (t.size() < 7 || t[1] != "from" || !number(t[2], a) || !number(t[3], b) || t[4] != "to" || !number(t[5], c) || !number(t[6], d)) return fail(usage);
	k.x0 = a, k.z0 = b, k.x1 = c, k.z1 = d;
	for (std::size_t i = 7; i < t.size();) {
		if (t[i] == "during" && i + 2 < t.size() && number(t[i + 1], a) && number(t[i + 2], b)) {
			if (b < a + 12.0) return fail("a tornado lasts at least 12 s (`during T0 T1`): 6 s to form, 6 s to rope out");
			k.t0 = a, k.t1 = b;
			i += 3;
		} else if (i + 1 < t.size() && number(t[i + 1], a)) {
			if (t[i] == "width" && a >= 2.0 && a <= 1000.0) k.width = a;
			else if (t[i] == "height" && a >= 50.0 && a <= 5000.0) k.height = a;
			else if (t[i] == "seed") k.seed = static_cast<int>(a);
			else return fail("\"" + t[i] + "\": " + usage + " (width 2 to 1000 m, height 50 to 5000 m)");
			i += 2;
		} else {
			return fail("\"" + t[i] + "\": " + usage);
		}
	}
	s.tornadoes.push_back(k);
	return std::string();
}

// An `optics` line (film_dressing.h, film_sky.h): the sky's halos, fogbow, rays, alpenglow and green flash. "" or what is wrong with it.
inline std::string opticsLine(Spec &s, const std::string &line) {
	using detail::number;
	const auto fail = [](const std::string &what) { return what; };
	const std::vector<std::string> t = detail::tokens(line);
	const std::string usage = "`optics [halo S] [fogbow S] [rays S] [alpenglow S] [greenflash]` (each 0 to 1)";
	if (t.size() < 2) return fail(usage);
	for (std::size_t i = 1; i < t.size();) {
		double a = 0.0;
		if (t[i] == "greenflash") {
			s.optics.greenFlash = true;
			++i;
		} else if (i + 1 < t.size() && number(t[i + 1], a) && a >= 0.0 && a <= 1.0 && (t[i] == "halo" || t[i] == "fogbow" || t[i] == "rays" || t[i] == "alpenglow")) {
			(t[i] == "halo" ? s.optics.halo : t[i] == "fogbow" ? s.optics.fogbow : t[i] == "rays" ? s.optics.rays : s.optics.alpenglow) = a;
			i += 2;
		} else {
			return fail("\"" + t[i] + "\": " + usage);
		}
	}
	return std::string();
}

// A `closeup` or `tracks` line (film_ground.h). "" or what is wrong with it.
inline std::string groundLine(Spec &s, const std::string &verb, const std::string &line) {
	using detail::number;
	const auto fail = [](const std::string &what) { return what; };
	const std::vector<std::string> t = detail::tokens(line);
	double a = 0.0, b = 0.0, c = 0.0, d = 0.0;
	if (verb == "closeup") {
		s.closeup.on = true;
		for (std::size_t i = 1; i < t.size(); i += 2) {
			if (i + 1 < t.size() && number(t[i + 1], a) && t[i] == "size" && a >= 2.0 && a <= 60.0) s.closeup.size = a;
			else if (i + 1 < t.size() && number(t[i + 1], a) && t[i] == "cell" && a >= 0.01 && a <= 0.5) s.closeup.cell = a;
			else return fail("\"" + t[i] + "\": `closeup [size S] [cell C]` (size 2 to 60 m, a point every C metres, 0.01 to 0.5)");
		}
		return std::string();
	}
	const std::string usage = "`tracks from X Z to X Z [during T0 T1] [kind boots|paws|hooves] [stride L] [seed N]`";
	ground::Track k;
	if (t.size() < 7 || t[1] != "from" || !number(t[2], a) || !number(t[3], b) || t[4] != "to" || !number(t[5], c) || !number(t[6], d)) return fail(usage);
	k.x0 = a, k.z0 = b, k.x1 = c, k.z1 = d;
	for (std::size_t i = 7; i < t.size();) {
		if (t[i] == "during" && i + 2 < t.size() && number(t[i + 1], a) && number(t[i + 2], b) && b > a) {
			k.t0 = a, k.t1 = b;
			i += 3;
		} else if (t[i] == "kind" && i + 1 < t.size() && std::find(ground::trackKinds().begin(), ground::trackKinds().end(), t[i + 1]) != ground::trackKinds().end()) {
			k.kind = t[i + 1];
			i += 2;
		} else if (i + 1 < t.size() && number(t[i + 1], a) && ((t[i] == "stride" && a >= 0.2 && a <= 3.0) || t[i] == "seed")) {
			if (t[i] == "stride") k.stride = a;
			else k.seed = static_cast<int>(a);
			i += 2;
		} else {
			return fail("\"" + t[i] + "\": " + usage);
		}
	}
	s.tracks.push_back(k);
	return std::string();
}

// One line of a film file, if it is a nature line: what it says goes into `s`. `handled` is false when `verb` is not a nature word (the line is parse()'s); the
// result is "" or what is wrong with the line (parse() adds the file and the line).
inline std::string natureLine(Spec &s, const std::string &verb, const std::string &line, const std::string &rest, bool &handled) {
	using detail::number;
	const auto fail = [](const std::string &what) { return what; };
	handled = true;
	if (verb == "terrain") {
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
				else if (t[i] == "wind") sea.wind = v, sea.windSet = true;
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
			return fail("`waterfall at X Y Z [width W] [drop H] [facing DEGREES] [frozen] [rainbow]`: the middle of its lip");
		for (std::size_t i = 5; i < t.size(); i += 2) {
			double v = 0.0;
			if (t[i] == "frozen" || t[i] == "rainbow") {   // (hung in ice: film_ice.h; a rainbow in its spray: film_caustics.h)
				if (t[i] == "frozen") fl.frozen = true;
				else fl.rainbow = 1.0;
				--i;
				continue;
			}
			if (i + 1 >= t.size() || !number(t[i + 1], v)) return fail("\"" + t[i] + "\": a waterfall takes width, drop and facing, each with a number");
			if (t[i] == "width" && v > 0.0 && v <= 200.0) fl.width = v;
			else if (t[i] == "drop" && v > 0.0 && v <= 1000.0) fl.drop = v;
			else if (t[i] == "facing") fl.facing = v;
			else return fail("\"" + t[i] + "\": a waterfall takes width (up to 200 m), drop (up to 1000 m), facing, frozen and rainbow");
		}
		s.falls.push_back(fl);
	} else if (verb == "clouds") {
		const std::vector<std::string> t = detail::tokens(line);
		clouds::Clouds c;
		if (t.size() < 2 || std::find(clouds::kinds().begin(), clouds::kinds().end(), t[1]) == clouds::kinds().end())
			return fail("`clouds KIND [cover C] [base H] [size W] [wind DEGREES] [speed S] [density D] [area A] [at X Z] [seed N]`: cumulus, towering, wisps or deck "
						"(a layer over the whole area, `thick T` deep, that the camera can fly through)");
		c.kind = t[1];
		if (c.kind == "towering") c.size = 1500.0, c.base = 1000.0, c.cover = 0.2, c.density = 1.4;
		else if (c.kind == "wisps") c.size = 1400.0, c.base = 5000.0, c.cover = 0.3, c.density = 0.35, c.area = 12000.0;
		else if (c.kind == "deck") c.base = 600.0, c.cover = 0.75, c.area = 8000.0, c.speed = 3.0;
		for (std::size_t i = 2; i < t.size();) {
			double a = 0.0, b = 0.0;
			if (t[i] == "at" && i + 2 < t.size() && number(t[i + 1], a) && number(t[i + 2], b)) {
				c.x = a, c.z = b;
				i += 3;
			} else if (i + 1 < t.size() && number(t[i + 1], a)) {
				if (t[i] == "cover" && a > 0.0 && a <= 1.0) c.cover = a;
				else if (t[i] == "base") c.base = a;
				else if (t[i] == "size" && a >= 20.0 && a <= 5000.0) c.size = a;
				else if (t[i] == "wind") c.wind = a, c.windSet = true;
				else if (t[i] == "speed" && a >= 0.0) c.speed = a, c.speedSet = true;
				else if (t[i] == "density" && a > 0.0 && a <= 10.0) c.density = a;
				else if (t[i] == "area" && a >= 100.0) c.area = a;
				else if (t[i] == "seed") c.seed = static_cast<int>(a);
				else if (t[i] == "thick" && c.kind == "deck" && a >= 20.0 && a <= 3000.0) c.thick = a;
				else return fail("\"" + t[i] + "\": clouds take cover (0 to 1), base, size (20 to 5000 m), wind, speed, density (up to 10), area, at X Z and seed, and a deck thick (20 to 3000 m)");
				i += 2;
			} else {
				return fail("\"" + t[i] + "\": clouds take cover, base, size, wind, speed, density, area, at X Z and seed");
			}
		}
		s.cloudLayers.push_back(c);
	} else if (verb == "fogbank") {   // fog lying on the land (film_clouds.h)
		return fogBankLine(s, line);
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
				else if (t[i] == "feathers" && (f.kind == "eagles" || f.kind == "geese") && a >= 0.0 && a <= 4.0) f.feathers = a;
				else return fail("\"" + t[i] + "\": a flock takes at X Y Z, from X Y Z, to X Y Z, in X0 Z0 X1 Z1, radius, speed, height, during T0 T1 and seed (eagles and geese: feathers 0 to 4)");
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
			else if (t[i] == "fur" && a >= 0.0 && a <= 4.0) h.fur = a;
			else return fail("\"" + t[i] + "\": a herd takes speed, start, flee (when it bolts), fur (0 to 4) and seed");
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
			return fail("`weather KIND [AMOUNT] [from T] [to T] [ground Y]`: rain, snow, leaves, dust or ash");
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
			if (t[i] == "wind") wind = a, s.gusts.windSet = true;
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
			else if (t[i] == "wind") s.storm.wind = v, s.storm.windSet = true;
			else if (t[i] == "speed" && v >= 0.0 && v <= 40.0) s.storm.speed = v, s.storm.speedSet = true;
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
		return fireLine(s, verb, line);
	} else if (verb == "whale" || verb == "dolphins") {   // the sea's giants (film_giants.h)
		return giantLine(s, verb, line);
	} else if (verb == "bioluminescence" || verb == "fireflies" || verb == "fungi") {   // living light (film_glow.h)
		return glowLine(s, verb, line);
	} else if (verb == "glacier" || verb == "iceberg" || verb == "seaice") {   // ice (film_ice.h)
		return iceLine(s, verb, line);
	} else if (verb == "wind" || verb == "tornado") {   // the wind (film_wind.h)
		return windLine(s, verb, line);
	} else if (isSpaceVerb(verb)) {   // the universe (film_parse_space.h)
		return spaceLine(s, verb, line);
	} else if (verb == "closeup" || verb == "tracks") {   // the ground close up (film_ground.h)
		return groundLine(s, verb, line);
	} else if (verb == "caustics") {   // the net of light under water (film_caustics.h)
		const std::vector<std::string> t = detail::tokens(line);
		if (t.size() != 2 || (t[1] != "traced" && t[1] != "painted")) return fail("`caustics traced` (worked out from the rippling surface) or `caustics painted` (the default)");
		s.tracedCaustics = t[1] == "traced";
	} else if (verb == "optics") {   // the sky's optics (film_sky.h, film_dressing.h)
		return opticsLine(s, line);
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
	} else {
		handled = false;
	}
	return std::string();
}

}  // namespace film_file
}  // namespace cartoon
