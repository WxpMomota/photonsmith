#pragma once
// film_baseline.h -- what every film is, shot by shot, kept in a file (tests/data/film_baseline.txt), so that a change to code the films share (the rig, the gait, the kit, the
// sky) shows which films and which shots it changed. One line a shot: the fingerprint (frame_fingerprint.h) of the scene at the shot's middle. `ray_tracer --film-baseline FILE`
// builds the scenes at preview coarseness (a character's shape still changes when its code does, in a fraction of the time) and lists what differs; `--write` records the films
// as they are now. `python scripts/anim/film.py baseline` runs it on the repository's file.

#include <algorithm>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "cartoon_lib.h"

namespace cartoon {
namespace film_baseline {

// One shot of one film as recorded.
struct Entry {
	std::string film;
	int shot = 0;   // from 1
	std::string name, fingerprint;
};

// The shots of a film as they are now: each shot's middle, its scene's fingerprint (`fingerprint` is frame_fingerprint::of, passed in so that this header does not need the
// launcher).
inline std::vector<Entry> entriesOf(const Film &film, const std::function<std::string(const std::string &)> &fingerprint) {
	std::vector<Entry> out;
	const std::vector<double> starts = film.shotStarts ? film.shotStarts() : std::vector<double>{0.0};
	std::vector<std::string> names;
	{
		std::string name;
		for (const char *p = film.meta.shotNames ? film.meta.shotNames : ""; ; ++p) {
			if (*p == '|' || *p == '\0') {
				names.push_back(name);
				name.clear();
				if (*p == '\0') break;
			} else {
				name += *p;
			}
		}
	}
	for (std::size_t i = 0; i < starts.size(); ++i) {
		const double end = i + 1 < starts.size() ? starts[i + 1] : film.duration;
		Entry e;
		e.film = film.key;
		e.shot = static_cast<int>(i) + 1;
		e.name = i < names.size() ? names[i] : std::string();
		e.fingerprint = fingerprint(film.sceneAt(0.5 * (starts[i] + end)));
		out.push_back(e);
	}
	return out;
}

inline std::string lineOf(const Entry &e) { return e.film + "|" + std::to_string(e.shot) + "|" + e.fingerprint + "|" + e.name; }

// The entries of a baseline file's text (lines `film|shot|fingerprint|name`; '#' and blank lines left out).
inline std::vector<Entry> parse(const std::string &text) {
	std::vector<Entry> out;
	std::istringstream in(text);
	for (std::string line; std::getline(in, line);) {
		if (!line.empty() && line.back() == '\r') line.pop_back();
		if (line.empty() || line[0] == '#') continue;
		std::vector<std::string> f;
		std::size_t at = 0;
		for (int k = 0; k < 3; ++k) {
			const std::size_t bar = line.find('|', at);
			if (bar == std::string::npos) break;
			f.push_back(line.substr(at, bar - at));
			at = bar + 1;
		}
		if (f.size() != 3) continue;
		Entry e;
		e.film = f[0];
		e.shot = std::atoi(f[1].c_str());
		e.fingerprint = f[2];
		e.name = line.substr(at);
		out.push_back(e);
	}
	return out;
}

// What differs between the recorded entries and the films now, for the films in `now`: "changed", "new" (a shot not recorded) and "gone" (a recorded shot no longer there).
struct Difference {
	std::string kind;
	Entry entry;
};
inline std::vector<Difference> compare(const std::vector<Entry> &recorded, const std::vector<Entry> &now) {
	std::vector<Difference> out;
	std::map<std::pair<std::string, int>, const Entry *> was;
	std::map<std::string, bool> films;
	for (const Entry &e : now) films[e.film] = true;
	for (const Entry &e : recorded)
		if (films.count(e.film)) was[{e.film, e.shot}] = &e;
	for (const Entry &e : now) {
		const auto it = was.find({e.film, e.shot});
		if (it == was.end()) out.push_back({"new", e});
		else if (it->second->fingerprint != e.fingerprint || it->second->name != e.name) out.push_back({"changed", e});
		if (it != was.end()) was.erase(it);
	}
	for (const auto &[key, e] : was) out.push_back({"gone", *e});
	return out;
}

// The file's text with the films in `now` recorded as they are and the other films' lines kept.
inline std::string merged(const std::vector<Entry> &recorded, const std::vector<Entry> &now) {
	std::map<std::string, bool> films;
	for (const Entry &e : now) films[e.film] = true;
	std::string out = "# Every film, shot by shot: film|shot|fingerprint of the scene at the shot's middle (at preview coarseness)|name. Written by\n"
					  "# `python scripts/anim/film.py baseline --write` (ray_tracer --film-baseline); a difference is a film that changed - on purpose or not.\n";
	std::vector<Entry> all;
	for (const Entry &e : recorded)
		if (!films.count(e.film)) all.push_back(e);
	all.insert(all.end(), now.begin(), now.end());
	std::stable_sort(all.begin(), all.end(), [](const Entry &a, const Entry &b) { return a.film != b.film ? a.film < b.film : a.shot < b.shot; });
	for (const Entry &e : all) out += lineOf(e) + "\n";
	return out;
}

}  // namespace film_baseline
}  // namespace cartoon
