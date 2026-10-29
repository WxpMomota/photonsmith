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
	handled = false;
	return "";
}

}  // namespace film_file
}  // namespace cartoon
