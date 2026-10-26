#pragma once
// film_edit.h -- changes made to a film file's text from the program (the GUI's film timeline): a shot's camera set to a view, a view added as a shot's next key, a shot's
// start moved. The rest of the file is kept as it was, line for line - its comments, its order, its spacing - so a change made here reads like one made by hand, and the
// film is read again when the file is saved (cartoons.h refreshFilmFile).
//
// A shot of the film as the program lists it (its shot names, in order) is a `shot` line of the file: the lines in the order they come (a film in parts lists its parts'
// shots in order), an `auto` shot listed as several ("Talk: ann", "Talk: bo") all being its one line, and the credits (`credits`: a "Credits" shot the file has no line for)
// none.

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

namespace cartoon {
namespace film_edit {

// A `shot` line of a file: its line (from 0), its start and name, and the lines up to the next shot or part (its own lines are among them).
struct ShotLine {
	std::size_t line = 0, end = 0;
	double start = 0.0;
	std::string name;
};

namespace detail {
inline std::vector<std::string> linesOf(const std::string &text) {
	std::vector<std::string> out;
	std::string line;
	std::istringstream in(text);
	while (std::getline(in, line)) out.push_back(line);
	return out;
}
inline std::string joined(const std::vector<std::string> &lines, const std::string &original) {
	const std::string nl = original.find("\r\n") != std::string::npos ? "\r\n" : "\n";
	std::string out;
	for (const std::string &l : lines) {
		std::string bare = l;
		if (!bare.empty() && bare.back() == '\r') bare.pop_back();
		out += bare + nl;
	}
	return out;
}
inline std::string firstWord(const std::string &line) {
	std::size_t a = line.find_first_not_of(" \t");
	if (a == std::string::npos || line[a] == '#') return std::string();
	std::size_t b = a;
	while (b < line.size() && !std::isspace(static_cast<unsigned char>(line[b]))) ++b;
	return line.substr(a, b - a);
}
inline std::string indentOf(const std::string &line) { return line.substr(0, line.find_first_not_of(" \t")); }
// The words of the camera of a shot (what `Use this view` takes away).
inline bool cameraWord(const std::string &w) {
	return w == "key" || w == "follow" || w == "auto" || w == "side" || w == "orbit" || w == "crane" || w == "push" || w == "track" || w == "fly";
}
inline std::string num(double v) {
	char buf[32];
	std::snprintf(buf, sizeof buf, "%.3f", v);
	std::string s = buf;
	while (s.size() > 1 && s.back() == '0') s.pop_back();
	if (!s.empty() && s.back() == '.') s.pop_back();
	return s == "-0" ? "0" : s;
}
}  // namespace detail

inline std::vector<ShotLine> shotLines(const std::string &text) {
	const std::vector<std::string> lines = detail::linesOf(text);
	std::vector<ShotLine> out;
	for (std::size_t i = 0; i < lines.size(); ++i) {
		const std::string w = detail::firstWord(lines[i]);
		if (w == "part" && !out.empty() && out.back().end == 0) out.back().end = i;
		if (w != "shot") continue;
		if (!out.empty() && out.back().end == 0) out.back().end = i;
		ShotLine s;
		s.line = i;
		std::istringstream words(lines[i]);
		std::string shot, start;
		words >> shot >> start;
		s.start = std::atof(start.c_str());
		std::getline(words, s.name);
		s.name.erase(0, s.name.find_first_not_of(" \t"));
		if (const std::size_t hash = s.name.find(" #"); hash != std::string::npos) s.name.erase(hash);
		while (!s.name.empty() && std::isspace(static_cast<unsigned char>(s.name.back()))) s.name.pop_back();
		out.push_back(s);
	}
	if (!out.empty() && out.back().end == 0) out.back().end = lines.size();
	return out;
}

// The file's shot line for the film's `index`th listed shot (names: the film's shot names in order), or -1; `first` says whether that listed shot is the first of its line's.
inline int shotLineFor(const std::string &text, const std::vector<std::string> &names, std::size_t index, bool *first = nullptr) {
	const std::vector<ShotLine> shots = shotLines(text);
	std::size_t k = 0;
	for (std::size_t s = 0; s < shots.size() && k < names.size(); ++s) {
		const std::size_t from = k;
		const std::string group = shots[s].name + ": ";   // (an auto shot's: "Talk: ann", "Talk: everyone")
		if (names[k] == shots[s].name || names[k].rfind(group, 0) == 0) ++k;
		while (k < names.size() && names[k].rfind(group, 0) == 0) ++k;
		if (k == from) ++k;   // (a name the line does not give itself: a shot named after its number, "Shot 2")
		if (index >= from && index < k) {
			if (first) *first = index == from;
			return static_cast<int>(s);
		}
	}
	return -1;
}

// A `key` line for a camera at `eye` looking at `target` (pbrt's coordinates), with its field of view when given.
inline std::string keyLine(const double eye[3], const double target[3], double fov = 0.0) {
	std::string s = "key   " + detail::num(eye[0]) + " " + detail::num(eye[1]) + " " + detail::num(eye[2]) + "   " + detail::num(target[0]) + " " + detail::num(target[1]) + " " +
					detail::num(target[2]);
	if (fov > 0.0) s += "   " + detail::num(fov);
	return s;
}

// The shot's camera made the view `key`: replacing what its camera was (its keys, a follow, a move, `auto`, a flight), or (`add`) as its next key after the ones it has.
// "" in `why` and the new text, or why not (the text unchanged): a shot whose camera is not keys cannot take one more.
inline std::string setShotView(const std::string &text, int shotLine, const std::string &key, bool add, std::string &why) {
	std::vector<std::string> lines = detail::linesOf(text);
	const std::vector<ShotLine> shots = shotLines(text);
	why.clear();
	if (shotLine < 0 || static_cast<std::size_t>(shotLine) >= shots.size()) {
		why = "the film file has no line for this shot";
		return text;
	}
	const ShotLine &s = shots[static_cast<std::size_t>(shotLine)];
	std::size_t lastKey = s.line, keys = 0;
	std::string indent = "  ";
	bool otherCamera = false;
	for (std::size_t i = s.line + 1; i < s.end; ++i) {
		const std::string w = detail::firstWord(lines[i]);
		if (w == "key") lastKey = i, ++keys, indent = detail::indentOf(lines[i]);
		else if (detail::cameraWord(w)) otherCamera = true;
	}
	if (add) {
		if (otherCamera || keys == 0) {
			why = "the shot's camera is not keys (a follow, a move, `auto` or a flight): use the view for the shot instead";
			return text;
		}
		lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(lastKey + 1), indent + key);
		return detail::joined(lines, text);
	}
	// (a view with no field of view of its own keeps the shot's: its first key's)
	std::string view = key;
	std::istringstream given(key);
	std::vector<std::string> words;
	for (std::string w; given >> w;) words.push_back(w);
	for (std::size_t i = s.line + 1; i < s.end && words.size() == 7; ++i) {
		if (detail::firstWord(lines[i]) != "key") continue;
		std::istringstream old(lines[i]);
		std::vector<std::string> was;
		for (std::string w; old >> w && w[0] != '#';) was.push_back(w);
		if (was.size() >= 8) view += "   " + was[7];
		break;
	}
	std::vector<std::string> out;
	for (std::size_t i = 0; i < lines.size(); ++i) {
		if (i > s.line && i < s.end && detail::cameraWord(detail::firstWord(lines[i]))) continue;
		out.push_back(lines[i]);
		if (i == s.line) out.push_back(indent + view);
	}
	return detail::joined(out, text);
}

// The shot's start moved to `start` (the file's own time: a part's from its part's start). The shots must stay in order and the first at 0: "" or why not.
inline std::string moveShotStart(const std::string &text, int shotLine, double start, std::string &why) {
	std::vector<std::string> lines = detail::linesOf(text);
	const std::vector<ShotLine> shots = shotLines(text);
	why.clear();
	if (shotLine < 0 || static_cast<std::size_t>(shotLine) >= shots.size()) {
		why = "the film file has no line for this shot";
		return text;
	}
	const std::size_t k = static_cast<std::size_t>(shotLine);
	const auto samePart = [&](std::size_t a, std::size_t b) {   // (no `part` line between two shots)
		for (std::size_t i = std::min(shots[a].line, shots[b].line); i < std::max(shots[a].line, shots[b].line); ++i)
			if (detail::firstWord(lines[i]) == "part") return false;
		return true;
	};
	const bool firstOfPart = k == 0 || !samePart(k - 1, k);
	if (firstOfPart) {
		why = "the first shot (of the film or of its part) starts at 0";
		return text;
	}
	if (!(start > shots[k - 1].start + 0.05) || (k + 1 < shots.size() && samePart(k, k + 1) && !(start < shots[k + 1].start - 0.05))) {
		why = "a shot starts after the one before it and before the one after it";
		return text;
	}
	std::string &line = lines[shots[k].line];
	const std::size_t word = line.find("shot");
	const std::size_t a = line.find_first_not_of(" \t", word + 4), b = line.find_first_of(" \t", a);
	line.replace(a, (b == std::string::npos ? line.size() : b) - a, detail::num(start));
	return detail::joined(lines, text);
}

}  // namespace film_edit
}  // namespace cartoon
