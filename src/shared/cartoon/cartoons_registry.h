#pragma once
// cartoons_registry.h -- how the program's list of films is made (cartoons.cpp): the built-in films with their cameras and cues files, then the films read from film files. Declared
// here for what builds a list of its own (the film-file tests); everything else asks cartoons.h's films().

#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "cartoons.h"
#include "film_file.h"

namespace cartoon {
namespace detail {

struct Registry {
	std::deque<film_file::DataFilm> data;
	std::deque<Film> made;
	std::deque<std::string> strings;
	std::vector<const Film *> all;
	std::vector<std::string> problems;
	std::set<std::string> listed;                                    // the film files that made a film
	std::map<std::string, std::filesystem::file_time_type> tried;    // and the time of each file when it was read (one left out is read again once it has been saved)
	const char *keep(std::string s) {
		strings.push_back(std::move(s));
		return strings.back().c_str();
	}
};

// A film as the program shows it: its frames with the changes of its cameras file made (shot_cameras.h: pbrt_scenes/film_assets/<key>/cameras.txt, read when a frame is
// made), and its soundtrack with the cues of its cues file (music_cues.h: pbrt_scenes/film_assets/<key>/cues.txt, read when the soundtrack is made).
Film withFilmFiles(Film f);
// The words of a film file's `tags` line that are in the vocabulary, joined with commas.
std::string tagsIn(const std::string &line);
// A stable number from a film's key (fixed once given, whatever other films there are): 1000 + a hash, moved on past any that is taken.
int idFor(const Registry &r, const std::string &key);
// Its captions file's captions (film_assets/<key>/captions.txt) and `lines` (its characters' words); null when it has neither.
std::function<std::vector<Caption>()> captionsOf(const std::string &key, std::function<std::vector<Caption>()> lines);
// The Film of a film read from a file (or of one part of a film in parts): its frames, shots, sound, captions and transitions. `drafted`: its music is the score drafted from
// it (it has no cues file).
Film filmOf(Registry &r, const film_file::DataFilm &d, bool drafted, int id, const std::string &tags);
// Loads one film file into the registry (or says why not): a film, or a film in parts (film_file::partsOf) made of a film for each part (film_parts.h).
void addFilmFile(Registry &r, const std::string &file);
// The program's list: made the first time it is asked for, and kept as long as the program runs (films hand out pointers into it).
Registry &registry();

}  // namespace detail
}  // namespace cartoon
