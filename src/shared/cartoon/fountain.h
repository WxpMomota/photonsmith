#pragma once
// fountain.h -- a screenplay in Fountain (the plain-text screenplay format, fountain.io) made into a film file to start from (`ray_tracer --film-from-script SCRIPT OUT`):
//
//     Title: The First Star                 the title page: Title (the film's name and key), Cast (who is what kind, and its scale: "PIP=pip, TESS=turtle 6"; others are people)
//     Cast: PIP=pip
//
//     FADE IN:                              a fade up from black
//     EXT. THE PATH - DUSK                  a scene heading: a shot of its own, its cameras chosen from what happens (`auto`)
//     Pip looks up at the sky.              action: kept as a comment for whoever stages it
//     PIP                                   a character's cue, then what they say: a `say` line, timed by its syllables, one after another
//     (excited)                             a parenthetical: an expression (`feel`) or a gesture its kind has (`gesture`) while the line is said
//     Look, the first star!
//     DISSOLVE TO:                          how the next scene comes in: CUT TO (a cut), DISSOLVE TO, FADE TO BLACK / FADE OUT (a fade)
//
// The characters stand in a half circle facing its middle, on the stage set unless another is named. Notes [[...]], the boneyard /* ... */, *emphasis* and centred
// >titles< are taken out.

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "film_faces.h"
#include "film_file.h"
#include "film_speech.h"

namespace cartoon {
namespace fountain {

struct Line {
	std::string who, text, parenthetical;
};
struct Scene {
	std::string heading;
	std::string transition;   // how it comes in: "" a cut, "dissolve", "fade"
	std::vector<std::string> action;
	std::vector<Line> lines;
};
struct Script {
	std::string title;
	std::map<std::string, std::string> cast;   // NAME -> kind
	std::vector<Scene> scenes;
	bool fadeOut = false;
	std::vector<std::string> characters;   // in the order they first speak
};

namespace detail {
inline std::string trim(const std::string &s) {
	std::size_t a = 0, b = s.size();
	while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
	while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
	return s.substr(a, b - a);
}
inline std::string upper(std::string s) {
	for (char &c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
	return s;
}
inline std::string lower(std::string s) {
	for (char &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return s;
}
inline bool allCaps(const std::string &s) {
	bool letter = false;
	for (char c : s) {
		if (std::islower(static_cast<unsigned char>(c))) return false;
		letter = letter || std::isupper(static_cast<unsigned char>(c));
	}
	return letter;
}
inline bool sceneHeading(const std::string &s) {
	if (s.size() > 1 && s[0] == '.' && s[1] != '.') return true;
	const std::string u = upper(s);
	for (const char *p : {"INT.", "EXT.", "EST.", "INT./EXT", "INT/EXT", "I/E", "INT ", "EXT "})
		if (u.rfind(p, 0) == 0) return true;
	return false;
}
inline bool transitionLine(const std::string &s) {
	if (!s.empty() && s[0] == '>' && s.back() != '<') return true;
	const std::string u = upper(s);
	return allCaps(s) && (u.size() > 3 && u.compare(u.size() - 3, 3, "TO:") == 0 || u == "FADE OUT." || u == "FADE OUT" || u == "FADE TO BLACK." || u == "FADE TO BLACK");
}
// The text without notes, emphasis and the like.
inline std::string clean(std::string s) {
	for (std::size_t a; (a = s.find("[[")) != std::string::npos;) {
		const std::size_t b = s.find("]]", a);
		s.erase(a, b == std::string::npos ? std::string::npos : b + 2 - a);
	}
	std::string out;
	for (char c : s)
		if (c != '*' && c != '_') out += c;
	return trim(out);
}
// A character cue's name: without (V.O.), (CONT'D), a forcing '@' and a dual-dialogue '^'.
inline std::string cueName(std::string s) {
	if (!s.empty() && s[0] == '@') s.erase(0, 1);
	if (const std::size_t p = s.find('('); p != std::string::npos) s.erase(p);
	if (!s.empty() && s.back() == '^') s.pop_back();
	return trim(s);
}
}  // namespace detail

// Reads a Fountain screenplay.
inline Script parse(const std::string &text) {
	using namespace detail;
	Script out;
	std::string body = text;
	for (std::size_t a; (a = body.find("/*")) != std::string::npos;) {   // the boneyard
		const std::size_t b = body.find("*/", a);
		body.erase(a, b == std::string::npos ? std::string::npos : b + 2 - a);
	}
	std::vector<std::string> lines;
	{
		std::istringstream in(body);
		for (std::string l; std::getline(in, l);) {
			if (!l.empty() && l.back() == '\r') l.pop_back();
			lines.push_back(l);
		}
	}
	std::size_t i = 0;
	// the title page: key: value lines before the first blank line, when the first line is one
	if (!lines.empty() && lines[0].find(':') != std::string::npos && !sceneHeading(lines[0]) && !transitionLine(trim(lines[0]))) {
		for (; i < lines.size() && !trim(lines[i]).empty(); ++i) {
			const std::size_t colon = lines[i].find(':');
			if (colon == std::string::npos) continue;
			const std::string key = lower(trim(lines[i].substr(0, colon))), value = clean(lines[i].substr(colon + 1));
			if (key == "title") out.title = value;
			if (key == "cast") {
				std::istringstream items(value);
				for (std::string item; std::getline(items, item, ',');) {
					const std::size_t eq = item.find('=');
					if (eq != std::string::npos) out.cast[upper(trim(item.substr(0, eq)))] = trim(item.substr(eq + 1));
				}
			}
		}
	}
	std::string pending;   // a transition waiting for the next scene
	const auto scene = [&]() -> Scene & {
		if (out.scenes.empty()) out.scenes.push_back(Scene{"The film", pending, {}, {}}), pending.clear();
		return out.scenes.back();
	};
	for (; i < lines.size(); ++i) {
		const std::string t = trim(lines[i]);
		if (t.empty()) continue;
		const bool blankBefore = i == 0 || trim(lines[i - 1]).empty();
		const std::string u = upper(t);
		if (u == "FADE IN:" || u == "FADE IN") {
			pending = "fade";
			continue;
		}
		if (sceneHeading(t) && blankBefore) {
			Scene s;
			s.heading = clean(t[0] == '.' ? t.substr(1) : t);
			s.transition = pending;
			pending.clear();
			out.scenes.push_back(s);
			continue;
		}
		if (transitionLine(t)) {
			if (u.find("DISSOLVE") != std::string::npos) pending = "dissolve";
			else if (u.find("FADE") != std::string::npos) pending = "fade";
			else pending.clear();
			if (u.rfind("FADE OUT", 0) == 0 || u.rfind("FADE TO BLACK", 0) == 0) out.fadeOut = true;
			continue;
		}
		if ((t[0] == '>' && t.back() == '<') || t[0] == '=' || t[0] == '#' || t[0] == '~') continue;   // (a centred title, a page break, a section, lyrics)
		// a character's cue: capitals (or forced with @), a blank line before it, and what they say after it
		const bool cue = (t[0] == '@' || (allCaps(t) && !sceneHeading(t))) && blankBefore && i + 1 < lines.size() && !trim(lines[i + 1]).empty();
		if (cue) {
			Line l;
			l.who = upper(cueName(t));
			std::string said;
			for (++i; i < lines.size() && !trim(lines[i]).empty(); ++i) {
				const std::string d = trim(lines[i]);
				if (d.front() == '(' && d.back() == ')') {
					if (l.parenthetical.empty()) l.parenthetical = lower(d.substr(1, d.size() - 2));
					continue;
				}
				said += (said.empty() ? "" : " ") + clean(d);
			}
			l.text = said;
			if (!l.text.empty()) {
				scene().lines.push_back(l);
				if (std::find(out.characters.begin(), out.characters.end(), l.who) == out.characters.end()) out.characters.push_back(l.who);
			}
			continue;
		}
		// action (a paragraph: this line and those after it up to a blank line)
		std::string para = clean(t[0] == '!' ? t.substr(1) : t);
		for (; i + 1 < lines.size() && !trim(lines[i + 1]).empty(); ++i) para += " " + clean(trim(lines[i + 1]));
		scene().action.push_back(para);
	}
	return out;
}

// What a parenthetical asks of the character: an expression (by name or a word for it) and a pose of its kind's library (by name or a word for it).
inline std::string expressionIn(const std::string &p) {
	static const std::vector<std::pair<const char *, const char *>> words = {
		{"smil", "happy"}, {"happ", "happy"}, {"laugh", "happy"}, {"excit", "happy"}, {"cheer", "happy"}, {"delight", "happy"}, {"sad", "sad"}, {"cry", "sad"}, {"tear", "sad"},
		{"angr", "angry"}, {"furious", "angry"}, {"cross", "angry"}, {"surpris", "surprised"}, {"shock", "surprised"}, {"amaz", "surprised"}, {"gasp", "surprised"},
		{"scare", "scared"}, {"afraid", "scared"}, {"fright", "scared"}, {"terrif", "scared"}, {"worr", "worried"}, {"nervous", "worried"}, {"anxious", "worried"},
		{"sleep", "sleepy"}, {"tired", "sleepy"}, {"yawn", "sleepy"}, {"smug", "smug"}, {"sly", "smug"}, {"think", "thinking"}, {"ponder", "thinking"}, {"wonder", "thinking"},
		{"disgust", "disgusted"}, {"gross", "disgusted"}};
	for (const auto &[w, e] : words)
		if (p.find(w) != std::string::npos) return e;
	return std::string();
}
inline std::string poseIn(const std::string &p, const std::vector<std::string> &poses) {
	for (const std::string &pose : poses)   // its own name first ("look up", "shake head")
		if (p.find(pose) != std::string::npos) return pose;
	static const std::vector<std::pair<const char *, const char *>> words = {{"waving", "wave"}, {"waves", "wave"}, {"pointing", "point"}, {"points", "point"}, {"nodding", "nod"},
																			 {"nods", "nod"}, {"shrug", "shrug"}, {"bowing", "bow"}, {"bows", "bow"}, {"reach", "reach"},
																			 {"jumps", "hop"}, {"hops", "hop"}, {"crouch", "crouch"}};
	for (const auto &[w, pose] : words)
		if (p.find(w) != std::string::npos && std::find(poses.begin(), poses.end(), pose) != poses.end()) return pose;
	return std::string();
}

// The film file of a script: its characters in a half circle on `set`, its lines one after another, a shot of each scene covering who speaks in it, its transitions.
inline std::string filmFile(const Script &s, const std::string &set, const std::string &keyIfNoTitle) {
	char buf[400];
	std::string out = "# Made from a screenplay (fountain.h): its lines timed one after another, a shot of each scene; stage it from here.\n";
	std::string key = film_file::detail::keyFromFileName(s.title.empty() ? keyIfNoTitle : s.title);
	key.erase(std::unique(key.begin(), key.end(), [](char a, char b) { return a == '-' && b == '-'; }), key.end());
	while (!key.empty() && key.back() == '-') key.pop_back();
	while (!key.empty() && key.front() == '-') key.erase(0, 1);
	out += "film    " + (key.empty() ? std::string("from-a-script") : key) + "\n";
	out += "name    " + (s.title.empty() ? keyIfNoTitle : s.title) + "\n";
	out += "scene   " + set + "\n";
	// who: a name for the film file (lower case, no spaces) and a kind
	std::map<std::string, std::string> name, kind;
	for (const std::string &c : s.characters) {
		std::string n = detail::lower(c);
		for (char &ch : n)
			if (!std::isalnum(static_cast<unsigned char>(ch))) ch = '-';
		name[c] = n;
		kind[c] = s.cast.count(c) ? s.cast.at(c) : "person";
	}
	// the lines' times: one after another, a pause between two, a longer one at a new scene
	struct Timed {
		std::size_t scene;
		const Line *line;
		double t, end;
	};
	std::vector<Timed> timed;
	std::vector<double> sceneStart;
	double t = 0.0;
	for (std::size_t k = 0; k < s.scenes.size(); ++k) {
		sceneStart.push_back(t);
		t += s.scenes[k].lines.empty() ? 3.0 : 1.2;   // (a scene of action alone: three seconds of it)
		for (const Line &l : s.scenes[k].lines) {
			speech::Line sl;
			sl.t = t;
			sl.text = l.text;
			const double end = speech::endOf(speech::syllablesOf(sl), t);
			timed.push_back({k, &l, t, end});
			t = end + 0.6;
		}
		t += 0.8;
	}
	const double length = std::max(2.0, t + (s.fadeOut ? 1.0 : 0.0));
	std::snprintf(buf, sizeof buf, "length  %.1f\n", std::ceil(length * 10.0) / 10.0);
	out += buf;
	if (s.fadeOut) out += "fadeout 1.5\n";
	out += "acting  words      # each line acted from its words where the script gives no direction (film_acting.h)\n";
	out += "\n# who, standing round the middle of the stage facing it (two face each other)\n";
	const std::size_t n = s.characters.size();
	const double radius = std::max(0.8, 0.45 * static_cast<double>(n));
	for (std::size_t c = 0; c < n; ++c) {
		double x = 0.0, z = 0.0, facing = 0.0;
		if (n == 2) {
			x = c == 0 ? -0.8 : 0.8;
			facing = c == 0 ? 90.0 : -90.0;
		} else if (n > 2) {   // on an arc behind the middle, 140 degrees wide, each facing the middle
			const double a = (-70.0 + 140.0 * static_cast<double>(c) / static_cast<double>(n - 1)) * 3.14159265358979 / 180.0;
			x = radius * std::sin(a);
			z = -radius * std::cos(a);
			facing = std::atan2(-x, -z) * 180.0 / 3.14159265358979;
		}
		facing = std::round(facing) + 0.0;   // (never "-0")
		std::string k = kind[s.characters[c]], scaled;
		if (const std::size_t space = k.find(' '); space != std::string::npos) {   // ("turtle 6": a kind and its scale)
			scaled = "  scale " + detail::trim(k.substr(space + 1));
			k = k.substr(0, space);
		}
		std::snprintf(buf, sizeof buf, "character  %-10s %-15s at %.2f 0 %.2f  facing %.0f%s\n", name[s.characters[c]].c_str(), k.c_str(), x, z, facing, scaled.c_str());
		out += buf;
	}
	out += "\n# what they say\n";
	for (const Timed &l : timed) {
		std::string text = l.line->text;
		std::replace(text.begin(), text.end(), '"', '\'');
		std::replace(text.begin(), text.end(), '#', ' ');
		std::snprintf(buf, sizeof buf, "say  %-10s %6.2f  ", name[l.line->who].c_str(), l.t);
		out += buf + ("\"" + text + "\"\n");
		if (l.line->parenthetical.empty()) continue;
		// a parenthetical: an expression while the line is said, or a gesture its kind has
		const std::string e = expressionIn(l.line->parenthetical);
		if (!e.empty()) {
			std::snprintf(buf, sizeof buf, "feel %-10s %s %.2f %.2f %.2f %.2f\n", name[l.line->who].c_str(), e.c_str(), std::max(0.0, l.t - 0.4), l.t, l.end, l.end + 0.6);
			out += buf;
		}
		const std::string kindName = kind[l.line->who].substr(0, kind[l.line->who].find(' '));
		const auto k = film_file::characterKinds().find(kindName);
		const std::string pose = k == film_file::characterKinds().end() ? std::string() : poseIn(l.line->parenthetical, k->second.poses);
		if (!pose.empty()) {
			std::snprintf(buf, sizeof buf, "gesture %-10s \"%s\" %.2f %.2f %.2f %.2f\n", name[l.line->who].c_str(), pose.c_str(), std::max(0.0, l.t - 0.3), l.t + 0.2, l.end, l.end + 0.5);
			out += buf;
		}
		if (e.empty() && pose.empty()) out += "# (" + l.line->parenthetical + ")\n";
	}
	out += "\n# the scenes: a shot each, its cameras chosen from what happens in it\n";
	for (std::size_t k = 0; k < s.scenes.size(); ++k) {
		const Scene &sc = s.scenes[k];
		std::string heading = sc.heading;
		std::replace(heading.begin(), heading.end(), '#', ' ');
		std::snprintf(buf, sizeof buf, "shot %.2f %s\n", sceneStart[k], heading.c_str());
		out += buf;
		for (const std::string &a : sc.action) out += "  # " + a + "\n";
		out += "  auto\n";
		if (sc.transition == "dissolve" && k > 0) out += "  dissolve 1\n";
		if (sc.transition == "fade") out += "  fade 1\n";
	}
	return out;
}

}  // namespace fountain
}  // namespace cartoon
