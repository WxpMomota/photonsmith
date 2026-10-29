// cartoon_snapshot_tests.cpp - the cartoon films, pinned by what they contain, in tests/data/cartoon_films_snapshot.txt.
//
// One `meta` line per film (its number, length, shots, tags and samples) and one line per film and instant: the film's key, the time, and how many triangles and area lights its scene has. A change to a film (a prop added, a swarm made bigger, a shot's
// timing moved) shows up as a diff of that file in review, instead of going unnoticed until someone watches the video. Only counts are stored, never the scene text, so the
// snapshot does not depend on the last digit of a library's sin() and means the same on every platform. When a film is changed ON PURPOSE, regenerate the file and look at the diff:
//
//     RT_UPDATE_SNAPSHOT=1 ray_tracer_tests --gtest_filter=CartoonFilmSnapshot*
//
// (run from the repository root). The films are compared in test_parts.h's parts (each film in one), so the parallel runner's processes share the work.
#include <gtest/gtest.h>
#include "fixture_skip.h"
#include "test_parts.h"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "pbrt_flatten_test_helpers.h"
#include "../../src/shared/cartoon/cartoons.h"

namespace {

const char *const kSnapshotPath = "tests/data/cartoon_films_snapshot.txt";

// A film's lines of the snapshot: what it says about itself (line 0), then what its scene holds at four instants - the first, its own still, the middle and the last.
constexpr int kLinesPerFilm = 5;
std::string lineOf(const cartoon::Film &film, int k) {
	if (k == 0)
		return "meta|" + std::string(film.key) + "|V" + std::to_string(film.meta.id) + "|" + cartoon::lengthText(film) + "|" + std::to_string(film.fps) + " fps|" +
			   std::to_string(cartoon::shotCountOf(film)) + " shots|" + cartoon::join(cartoon::tagsOf(film), ",") + "|" + std::to_string(film.meta.samples) + " spp";
	const double instants[4] = {0.0, film.stillTime, film.duration * 0.5, film.duration * 0.999};
	const double t = instants[k - 1];
	const FlatScene s = flattenSource(film.sceneAt(t));
	std::ostringstream line;
	line.precision(4);
	line << film.key << '|' << t << '|' << s.triangles.size() << '|' << s.areaLights.size();
	return line.str();
}

// The film a line is of: "meta|<key>|..." or "<key>|...".
std::string keyOf(const std::string &line) {
	const std::size_t start = line.compare(0, 5, "meta|") == 0 ? 5 : 0;
	return line.substr(start, line.find('|', start) - start);
}

// The snapshot file's lines (comments and blank lines left out); false when it is not found.
bool pinnedLines(std::vector<std::string> &out) {
	std::ifstream in(kSnapshotPath, std::ios::binary);
	if (!in) return false;
	for (std::string l; std::getline(in, l);) {
		if (!l.empty() && l.back() == '\r') l.pop_back();
		if (l.empty() || l[0] == '#') continue;
		out.push_back(l);
	}
	return true;
}

}  // namespace

// The snapshot has every film of the table, in its order (what each film holds: the parts below). With RT_UPDATE_SNAPSHOT set it is written again from the films.
TEST(CartoonFilmSnapshot, TheSnapshotHasEveryFilmInTheTablesOrder) {
	if (std::getenv("RT_UPDATE_SNAPSHOT")) {
		std::ofstream out(kSnapshotPath, std::ios::binary);
		ASSERT_TRUE(out.good()) << "cannot write " << kSnapshotPath << " (run from the repository root)";
		out << "# The cartoon films, one instant per line: key|time (s)|triangles|area lights.\n"
		       "# Regenerate with RT_UPDATE_SNAPSHOT=1 (see tests/unit/cartoon_snapshot_tests.cpp) and review the diff.\n";
		std::size_t count = 0;
		for (const cartoon::Film &film : cartoon::kFilms)
			for (int k = 0; k < kLinesPerFilm; ++k) out << lineOf(film, k) << "\n", ++count;
		GTEST_SKIP() << "snapshot rewritten: " << count << " lines";
	}
	std::vector<std::string> pinned;
	if (!pinnedLines(pinned)) RT_SKIP_MISSING_FIXTURE(kSnapshotPath << " is not next to the working directory");
	std::vector<std::string> keys, table;
	for (const std::string &l : pinned)
		if (l.compare(0, 5, "meta|") == 0) keys.push_back(keyOf(l));
	for (const cartoon::Film &film : cartoon::kFilms) table.push_back(film.key);
	EXPECT_EQ(keys, table) << "the films changed (RT_UPDATE_SNAPSHOT=1 rewrites the snapshot if that is intended)";
	EXPECT_EQ(pinned.size(), cartoon::kFilms.size() * kLinesPerFilm) << "the instants changed (RT_UPDATE_SNAPSHOT=1 rewrites the snapshot if that is intended)";
}

// Each film's lines, one line of one film at a time in test_parts.h's parts (a long film's instant is many seconds of building its frame).
class CartoonFilmSnapshotOfEachFilm : public ::testing::TestWithParam<int> {};
TEST_P(CartoonFilmSnapshotOfEachFilm, ContainsWhatTheSnapshotFileSays) {
	if (std::getenv("RT_UPDATE_SNAPSHOT")) GTEST_SKIP() << "the snapshot is being written (CartoonFilmSnapshot.TheSnapshotHasEveryFilmInTheTablesOrder)";
	std::vector<std::string> pinned;
	if (!pinnedLines(pinned)) RT_SKIP_MISSING_FIXTURE(kSnapshotPath << " is not next to the working directory");
	for (std::size_t i = 0; i < cartoon::kFilms.size(); ++i) {
		const cartoon::Film &film = cartoon::kFilms[i];
		std::vector<std::string> theirs;
		for (const std::string &l : pinned)
			if (keyOf(l) == film.key) theirs.push_back(l);
		for (int k = 0; k < kLinesPerFilm; ++k) {
			if (!test_parts::inPart(i * kLinesPerFilm + k, GetParam())) continue;
			EXPECT_EQ(lineOf(film, k), k < static_cast<int>(theirs.size()) ? theirs[k] : std::string("(no such line)"))
				<< film.key << " line " << k << " differs from the snapshot (RT_UPDATE_SNAPSHOT=1 rewrites it if the change is intended)";
		}
	}
}
INSTANTIATE_TEST_SUITE_P(Parts, CartoonFilmSnapshotOfEachFilm, test_parts::all());
