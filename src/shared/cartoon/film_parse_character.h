#pragma once
// film_parse_character.h -- the lines that make a film's characters look and move as in a feature film (round 12): skin that light goes into, real eyes, faces and lip sync
// for rigged models, strand hair, clothes, feet on the ground. Read by film_parse_cartoon.h's cartoonLine.

#include <algorithm>
#include <string>
#include <vector>

#include "film_parse_nature.h"   // (and film_spec.h)

namespace cartoon {
namespace film_file {

namespace detail {
// `VERB [NAME] ...`: the character a line names (t[1], when it is one brought in already: then i moves past it), "" for every character.
inline std::string characterNamed(const Spec &s, const std::vector<std::string> &t, std::size_t &i) {
	if (i < t.size() && std::any_of(s.characters.begin(), s.characters.end(), [&](const Character &c) { return c.name == t[i]; })) return t[i++];
	return "";
}
}  // namespace detail

inline std::string characterLine(Spec &s, const std::string &verb, const std::string &line, int n, bool &handled) {
	using detail::number;
	(void)n;
	handled = true;
	if (verb == "skin-light") {   // skin-light [NAME] D (cartoon_lib.h skinMaterial)
		const std::vector<std::string> t = detail::tokens(line);
		std::size_t i = 1;
		motion::Amount a;
		a.who = detail::characterNamed(s, t, i);
		if (i + 1 != t.size() || !number(t[i], a.amount) || a.amount < 0.0 || a.amount > 6.0)
			return "`skin-light [NAME] D`: light goes D times a cartoon skin's depth into the skin (0 painted, 1 the usual, up to 6), for one character or (no NAME) every one";
		s.skinLight.push_back(a);
		return "";
	}
	if (verb == "eyes") {   // eyes [NAME] real|painted (eye_model.h)
		const std::vector<std::string> t = detail::tokens(line);
		std::size_t i = 1;
		motion::Amount a;
		a.who = detail::characterNamed(s, t, i);
		if (i + 1 != t.size() || (t[i] != "real" && t[i] != "painted"))
			return "`eyes [NAME] real|painted`: real eyes have a clear cornea over the iris, a wet rim and pupils that size with the light";
		a.amount = t[i] == "real" ? 1.0 : 0.0;
		s.realEyes.push_back(a);
		return "";
	}
	if (verb == "footing") {   // footing [NAME] ground|flat (DataFilm::footOnGround)
		const std::vector<std::string> t = detail::tokens(line);
		std::size_t i = 1;
		motion::Amount a;
		a.who = detail::characterNamed(s, t, i);
		if (i + 1 != t.size() || (t[i] != "ground" && t[i] != "flat"))
			return "`footing [NAME] ground|flat`: ground stands it on the floor under it (a slope, a stair, a rock), each foot on the floor under that foot, tilted to it, its hips down "
				   "to the lower foot; flat (the default) keeps its feet level at its own height";
		a.amount = t[i] == "ground" ? 1.0 : 0.0;
		s.footing.push_back(a);
		return "";
	}
	if (verb == "hair") {   // hair [NAME] STYLE|off [length L] [density D] [curl C] [frizz F] [clump C] [colour C] [width W] (film_hair.h)
		const std::vector<std::string> t = detail::tokens(line);
		const char *usage = "`hair [NAME] short|bob|long|ponytail|curly|mop|off [length L] [density D] [curl C] [frizz F] [clump C] [colour C] [width W]`";
		std::size_t i = 1;
		hair::Groom g;
		g.who = detail::characterNamed(s, t, i);
		if (i >= t.size()) return usage;
		g.style = t[i++];
		if (g.style != "off" && std::find(hair::styles().begin(), hair::styles().end(), g.style) == hair::styles().end()) return usage;
		for (; i < t.size(); i += 2) {
			if (i + 1 >= t.size()) return usage;
			double v = 0.0;
			if (t[i] == "colour" || t[i] == "color") {
				if (!colourOf(t[i + 1], g.colour)) return "`hair ... colour C`: a colour name, r,g,b or #rrggbb";
				g.hasColour = true;
				continue;
			}
			if (!number(t[i + 1], v)) return usage;
			if (t[i] == "length" && v > 0.0 && v <= 1.5) g.length = v;
			else if (t[i] == "density" && v > 0.0 && v <= 4.0) g.density = v;
			else if (t[i] == "curl" && v >= 0.0 && v <= 0.5) g.curl = v;
			else if (t[i] == "frizz" && v >= 0.0 && v <= 1.0) g.frizz = v;
			else if (t[i] == "clump" && v >= 0.0 && v <= 1.0) g.clump = v;
			else if (t[i] == "width" && v > 0.0 && v <= 5.0) g.width = v;
			else return std::string(usage) + ": length 0..1.5 m, density 0..4, curl 0..0.5, frizz and clump 0..1, width 0..5";
		}
		s.grooms.push_back(g);
		return "";
	}
	handled = false;
	return "";
}

}  // namespace film_file
}  // namespace cartoon
