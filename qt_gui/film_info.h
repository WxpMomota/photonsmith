#pragma once
// film_info.h -- the cartoon films as the GUI knows them: what the renderer's library says of them (scene_metadata_film_table, one line a film: cartoon::filmTable), not a
// compiled copy of the films. The GUI used to compile every film (src/shared/cartoon/), so a GUI not rebuilt with a new film gave its video the wrong length, and every edit
// of a film rebuilt most of the GUI; now adding a film needs only the renderer rebuilt.
//
// parse() is plain C++ (the unit tests check it against cartoon::filmTable()); all() and find() read the table through SceneMetadataClient, once and again when the list has grown (film_info.cpp).

#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

namespace film_info {

struct Film {
	std::string key, name;
	double duration = 0.0, stillTime = 0.0;
	int fps = 24, width = 0, height = 0, samples = 0;   // width/height 0: the film does not ask for a size
	bool wavefront = false, soundtrack = false;
	std::vector<double> shotStarts;
	std::vector<std::string> shotNames;
};

inline std::vector<std::string> split(const std::string &s, char separator) {
	std::vector<std::string> out;
	std::string field;
	for (const char c : s) {
		if (c == separator) {
			out.push_back(field);
			field.clear();
		} else {
			field += c;
		}
	}
	out.push_back(field);
	return out;
}

// The films of a film table (cartoon::filmTable's lines: key, name, seconds, fps, still time, width, height, samples, wavefront, soundtrack, shot starts, shot names). A line with
// too few fields is left out.
inline std::vector<Film> parse(const std::string &table) {
	std::vector<Film> films;
	for (const std::string &line : split(table, '\n')) {
		const std::vector<std::string> f = split(line, '\t');
		if (f.size() < 12 || f[0].empty()) continue;
		Film film;
		film.key = f[0];
		film.name = f[1];
		film.duration = std::atof(f[2].c_str());
		film.fps = std::atoi(f[3].c_str());
		film.stillTime = std::atof(f[4].c_str());
		film.width = std::atoi(f[5].c_str());
		film.height = std::atoi(f[6].c_str());
		film.samples = std::atoi(f[7].c_str());
		film.wavefront = f[8] == "1";
		film.soundtrack = f[9] == "1";
		if (!f[10].empty())
			for (const std::string &t : split(f[10], ',')) film.shotStarts.push_back(std::atof(t.c_str()));
		if (!f[11].empty()) film.shotNames = split(f[11], '|');
		films.push_back(film);
	}
	return films;
}

// The film's frame count at its own frame rate (cartoon::frameCountOf).
inline int frameCountOf(const Film &film) { return static_cast<int>(std::lround(film.duration * film.fps)); }
// "cartoon:<key>": the camera path of a video that renders the film (cartoon::cameraPathOf).
inline std::string cameraPathOf(const Film &film) { return "cartoon:" + film.key; }

// The films the renderer's library knows (read once), and one of them by key (null when there is no such film).
const std::vector<Film> &all();
const Film *find(const char *key);

}  // namespace film_info
