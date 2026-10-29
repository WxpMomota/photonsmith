#pragma once
// film_parse_cartoon.h -- the cartoon lines of a film file (its grammar: film_parse.h): `mark` (cartoon marks over the frames: film_marks.h) and `cloth` (flags, capes,
// sheets and curtains: film_cloth.h). parse() hands every line to cartoonLine() after the nature lines.

#include "film_parse_nature.h"   // (and film_spec.h)

namespace cartoon {
namespace film_file {

// A `cloth` or `mark` line (line `n` of its file) into `s`: "" or what is wrong with it; `handled` says whether it was one of them.
inline std::string cartoonLine(Spec &s, const std::string &verb, const std::string &line, int n, bool &handled) {
	using detail::number;
	const auto fail = [](const std::string &what) { return what; };
	handled = true;
	if (verb == "cloth") {   // cloth (film_cloth.h): cloth flag|curtain|sheet at X Y Z ..., cloth cape on NAME ...
		const std::vector<std::string> t = detail::tokens(line);
		const char *usage = "`cloth flag|curtain|sheet at X Y Z [size W H] ...` or `cloth cape on NAME [length L] [width W] ...`, with `colour C`, `stiff S` (0..1), `wind S`, "
							"`ground Y`, a flag's `pole H`, a curtain's or a sheet's `facing D`, a sheet's `drop T` and `over X Y Z SX SY SZ`";
		cloth::Cloth cl;
		cl.line = n;
		if (t.size() < 4 || std::find(cloth::kinds().begin(), cloth::kinds().end(), t[1]) == cloth::kinds().end()) return fail(usage);
		cl.kind = t[1];
		std::size_t i = 2;
		if (cl.kind == "cape") {
			if (t[2] != "on") return fail(usage);
			cl.who = t[3];
			if (std::none_of(s.characters.begin(), s.characters.end(), [&](const Character &c) { return c.name == cl.who; }))
				return fail("a cape goes `on` a character brought in before it with `character`");
			cl.width = 0.45, cl.height = 0.8, cl.colour = {0.45, 0.20, 0.62};
			i = 4;
		} else {
			double v[3];
			if (t.size() < 6 || t[2] != "at") return fail(usage);
			for (int k = 0; k < 3; ++k)
				if (!number(t[3 + k], v[k])) return fail(usage);
			cl.at = {v[0], v[1], v[2]};
			if (cl.kind == "curtain") cl.width = 2.0, cl.height = 2.2, cl.colour = {0.93, 0.86, 0.70};
			if (cl.kind == "sheet") cl.width = 2.0, cl.height = 2.0, cl.colour = {0.92, 0.92, 0.90};
			i = 6;
		}
		while (i < t.size()) {
			const std::string &w = t[i];
			double v[6];
			const auto numbers = [&](int count) {
				if (i + count >= t.size()) return false;
				for (int k = 0; k < count; ++k)
					if (!number(t[i + 1 + k], v[k])) return false;
				return true;
			};
			if (w == "size" && numbers(2) && v[0] > 0.05 && v[1] > 0.05 && v[0] <= 20.0 && v[1] <= 20.0) cl.width = v[0], cl.height = v[1], i += 3;
			else if ((w == "length" || w == "width") && cl.kind == "cape" && numbers(1) && v[0] > 0.05 && v[0] <= 5.0) (w == "length" ? cl.height : cl.width) = v[0], i += 2;
			else if (w == "pole" && cl.kind == "flag" && numbers(1) && v[0] >= 0.0) cl.pole = v[0], i += 2;
			else if (w == "facing" && numbers(1)) cl.facing = v[0], i += 2;
			else if (w == "drop" && cl.kind == "sheet" && numbers(1)) cl.drop = v[0], i += 2;
			else if (w == "over" && cl.kind == "sheet" && numbers(6) && v[3] > 0.0 && v[4] > 0.0 && v[5] > 0.0) cl.over.push_back({{v[0], v[1], v[2]}, {v[3], v[4], v[5]}}), i += 7;
			else if (w == "stiff" && numbers(1) && v[0] >= 0.0 && v[0] <= 1.0) cl.stiffness = v[0], i += 2;
			else if (w == "wind" && numbers(1) && v[0] >= 0.0 && v[0] <= 10.0) cl.windScale = v[0], i += 2;
			else if (w == "ground" && numbers(1)) cl.ground = v[0], i += 2;
			else if ((w == "colour" || w == "color") && i + 1 < t.size() && colourOf(t[i + 1], cl.colour)) i += 2;
			else return fail("\"" + w + "\": " + usage);
		}
		s.cloths.push_back(cl);
		return "";
	}
	if (verb == "mark") {   // a cartoon mark over the frames (film_marks.h): mark WHO|at X Y Z KIND T0 [T1] [colour C] [size S]
		const std::vector<std::string> t = detail::tokens(line);
		const char *usage = "`mark NAME KIND T0 [T1] [colour C] [size S]`, or `mark at X Y Z KIND T0 ...` (kinds: exclaim or !, question or ?, sweat, anger, heart, zzz, sparkle, impact, speed)";
		film_marks::Mark mk;
		mk.line = n;
		std::size_t i = 1;
		if (t.size() > 4 && t[1] == "at") {
			double v[3];
			for (int k = 0; k < 3; ++k)
				if (!number(t[2 + k], v[k])) return fail(usage);
			mk.at = {v[0], v[1], v[2]};
			i = 5;
		} else if (t.size() > 1) {
			mk.who = t[1];
			if (std::none_of(s.characters.begin(), s.characters.end(), [&](const Character &c) { return c.name == mk.who; }))
				return fail("`mark` needs the name of a character brought in before it with `character` (or `at X Y Z`)");
			i = 2;
		}
		if (i + 1 >= t.size() || (mk.kind = film_marks::kindOf(t[i])).empty() || !number(t[i + 1], mk.t0)) return fail(usage);
		if (mk.who.empty() && mk.kind == "speed") return fail("speed lines go behind a character that moves: `mark NAME speed T0 T1`");
		mk.t1 = mk.t0 + film_marks::defaultLife(mk.kind);
		i += 2;
		if (i < t.size() && t[i] != "colour" && t[i] != "color" && t[i] != "size") {
			if (!number(t[i], mk.t1) || !(mk.t1 > mk.t0)) return fail("a mark's end comes after its start");
			++i;
		}
		for (; i < t.size(); i += 2) {
			if (i + 1 >= t.size()) return fail(usage);
			if (t[i] == "colour" || t[i] == "color") {
				if (!colourOf(t[i + 1], mk.colour)) return fail("\"" + t[i + 1] + "\" is not a colour");
			} else if (t[i] == "size") {
				if (!number(t[i + 1], mk.size) || !(mk.size > 0.0 && mk.size <= 5.0)) return fail("a mark's size is from 0 to 5 (1: its own)");
			} else {
				return fail(usage);
			}
		}
		s.marks.push_back(mk);
		return "";
	}
	if (verb == "animate") {   // animate [NAME] on ones|twos|threes [from T0 to T1] (film_motion.h)
		const std::vector<std::string> t = detail::tokens(line);
		const char *usage = "`animate [NAME] on ones|twos|threes [from T0 to T1]`";
		motion::Stepping st;
		std::size_t i = 1;
		if (i < t.size() && t[i] != "on") {
			st.who = t[i++];
			if (std::none_of(s.characters.begin(), s.characters.end(), [&](const Character &c) { return c.name == st.who; }))
				return fail("`animate NAME ...` needs a character brought in before it with `character`");
		}
		if (i + 1 >= t.size() || t[i] != "on") return fail(usage);
		const std::string &every = t[i + 1];
		st.every = every == "ones" ? 1 : every == "twos" ? 2 : every == "threes" ? 3 : every == "fours" ? 4 : 0;
		if (st.every == 0) return fail(usage);
		i += 2;
		if (i < t.size()) {
			if (i + 3 != t.size() || t[i] != "from" || t[i + 2] != "to" || !number(t[i + 1], st.t0) || !number(t[i + 3], st.t1) || !(st.t1 > st.t0)) return fail(usage);
		}
		s.stepping.push_back(st);
		return "";
	}
	if (verb == "squash" || verb == "jiggle") {   // squash [NAME] S, jiggle [NAME] S (film_motion.h)
		const std::vector<std::string> t = detail::tokens(line);
		motion::Amount a;
		const double most = verb == "squash" ? 2.0 : 3.0;
		if (t.size() == 3) {
			a.who = t[1];
			if (std::none_of(s.characters.begin(), s.characters.end(), [&](const Character &c) { return c.name == a.who; }))
				return fail("`" + verb + " NAME S` needs a character brought in before it with `character`");
		}
		if ((t.size() != 2 && t.size() != 3) || !number(t.back(), a.amount) || a.amount < 0.0 || a.amount > most)
			return fail("`" + verb + " [NAME] S`: S from 0 (none) to " + (verb == "squash" ? std::string("2") : std::string("3")) + " (1 the usual)");
		(verb == "squash" ? s.squash : s.jiggle).push_back(a);
		return "";
	}
	if (verb == "smear") {   // smear [NAME] S [multiples N] (film_motion.h)
		const std::vector<std::string> t = detail::tokens(line);
		const char *usage = "`smear [NAME] S [multiples N]`: S frames' travel drawn out behind a dash, from 0 (none) to 3; N copies left along it, up to 4";
		motion::Smear sm;
		std::size_t i = 1;
		if (i < t.size() && !number(t[i], sm.amount)) {
			sm.who = t[i++];
			if (std::none_of(s.characters.begin(), s.characters.end(), [&](const Character &c) { return c.name == sm.who; }))
				return fail("`smear NAME ...` needs a character brought in before it with `character`");
		}
		if (i >= t.size() || !number(t[i++], sm.amount) || sm.amount < 0.0 || sm.amount > 3.0) return fail(usage);
		if (i < t.size()) {
			double n = 0.0;
			if (t[i] != "multiples" || i + 2 != t.size() || !number(t[i + 1], n) || n < 0.0 || n > 4.0 || n != std::floor(n)) return fail(usage);
			sm.multiples = static_cast<int>(n);
		}
		s.smears.push_back(sm);
		return "";
	}
	if (verb == "rigid") {   // rigid BLOCK box W H D | sphere R  at X Y Z [turn DEG] [mass M] [start T] [velocity X Y Z] [spin X Y Z] [fixed] [bounce B] [friction F] (rigid_world.h)
		const std::vector<std::string> t = detail::tokens(line);
		const char *usage = "`rigid BLOCK box W H D | sphere R  at X Y Z [turn DEG] [mass M] [start T] [velocity X Y Z] [spin X Y Z] [fixed] [bounce B] [friction F]`";
		rigid::BodyDef d;
		if (t.size() < 3) return fail(usage);
		d.block = t[1];
		std::size_t i = 2;
		const auto three = [&](Vec3 &v) {
			if (i + 3 > t.size() || !number(t[i], v.x) || !number(t[i + 1], v.y) || !number(t[i + 2], v.z)) return false;
			i += 3;
			return true;
		};
		if (t[i] == "box") {
			++i;
			Vec3 size;
			if (!three(size) || size.x <= 0.0 || size.y <= 0.0 || size.z <= 0.0) return fail(usage);
			d.half = size * 0.5;
		} else if (t[i] == "sphere") {
			if (i + 1 >= t.size() || !number(t[i + 1], d.radius) || d.radius <= 0.0) return fail(usage);
			d.sphere = true, i += 2;
		} else {
			return fail(usage);
		}
		if (i >= t.size() || t[i] != "at") return fail(usage);
		++i;
		if (!three(d.at)) return fail(usage);
		while (i < t.size()) {
			const std::string w = t[i++];
			double v = 0.0;
			if (w == "fixed") d.fixed = true;
			else if (w == "velocity" || w == "spin") {
				if (!three(w == "velocity" ? d.velocity : d.spin)) return fail(usage);
			} else if ((w == "turn" || w == "mass" || w == "start" || w == "bounce" || w == "friction") && i < t.size() && number(t[i], v)) {
				++i;
				if ((w == "mass" && v <= 0.0) || (w == "bounce" && (v < 0.0 || v > 1.0)) || (w == "friction" && (v < 0.0 || v > 2.0)) || (w == "start" && v < 0.0)) return fail(usage);
				(w == "turn" ? d.turn : w == "mass" ? d.mass : w == "start" ? d.start : w == "bounce" ? d.bounce : d.friction) = v;
			} else {
				return fail(usage);
			}
		}
		if (d.start < 0.0 && (length(d.velocity) > 0.0 || length(d.spin) > 0.0)) d.start = 0.0;   // (thrown from the start)
		s.rigids.push_back(d);
		s.physicsLines.push_back({d.block, n});
		return "";
	}
	if (verb == "terminator") {   // terminator C: every material of the frames rounds the shadows of coarse smooth meshes (shadow_terminator.h)
		const std::vector<std::string> t = detail::tokens(line);
		double c = 0.0;
		if (t.size() != 2 || !number(t[1], c) || c < 0.0 || c > 1.0) return fail("`terminator C`: C from 0 (off) to 1 (0.1 the usual one, Cycles')");
		s.terminator = c;
		return "";
	}
	handled = false;
	return "";
}

}  // namespace film_file
}  // namespace cartoon
