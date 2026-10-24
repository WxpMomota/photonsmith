// cartoon_snapshot_tests.cpp - the cartoon films, pinned by what they contain, in tests/data/cartoon_films_snapshot.txt.
//
// One `meta` line per film (its number, length, shots, tags and samples) and one line per film and instant: the film's key, the time, and how many triangles and area lights its scene has. A change to a film (a prop added, a swarm made bigger, a shot's
// timing moved) shows up as a diff of that file in review, instead of going unnoticed until someone watches the video. Only counts are stored, never the scene text, so the
// snapshot does not depend on the last digit of a library's sin() and means the same on every platform. When a film is changed ON PURPOSE, regenerate the file and look at the diff:
//
//     RT_UPDATE_SNAPSHOT=1 ray_tracer_tests --gtest_filter=CartoonFilmSnapshot*
//
// (run from the repository root).
#include <gtest/gtest.h>
#include "fixture_skip.h"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "pbrt_flatten_test_helpers.h"
#include "../../src/shared/cartoon/cartoons.h"

namespace {

std::vector<std::string> currentSnapshot() {
	std::vector<std::string> lines;
	for (const cartoon::Film &film : cartoon::kFilms) {
		// what the film says about itself, then what its scene holds at four instants
		lines.push_back("meta|" + std::string(film.key) + "|V" + std::to_string(film.meta.id) + "|" + cartoon::lengthText(film) + "|" + std::to_string(film.fps) + " fps|" + std::to_string(cartoon::shotCountOf(film)) + " shots|" +
						cartoon::join(cartoon::tagsOf(film), ",") + "|" + std::to_string(film.meta.samples) + " spp");
		// the first instant, the film's own still, the middle and the last
		for (double t : {0.0, film.stillTime, film.duration * 0.5, film.duration * 0.999}) {
			const FlatScene s = flattenSource(film.sceneAt(t));
			std::ostringstream line;
			line.precision(4);
			line << film.key << '|' << t << '|' << s.triangles.size() << '|' << s.areaLights.size();
			lines.push_back(line.str());
		}
	}
	return lines;
}

}  // namespace

TEST(CartoonFilmSnapshot, TheFilmsContainWhatTheSnapshotFileSays) {
	const std::string path = "tests/data/cartoon_films_snapshot.txt";
	const std::vector<std::string> now = currentSnapshot();
	if (std::getenv("RT_UPDATE_SNAPSHOT")) {
		std::ofstream out(path, std::ios::binary);
		ASSERT_TRUE(out.good()) << "cannot write " << path << " (run from the repository root)";
		out << "# The cartoon films, one instant per line: key|time (s)|triangles|area lights.\n"
		       "# Regenerate with RT_UPDATE_SNAPSHOT=1 (see tests/unit/cartoon_snapshot_tests.cpp) and review the diff.\n";
		for (const std::string &l : now) out << l << "\n";
		GTEST_SKIP() << "snapshot rewritten: " << now.size() << " instants";
	}
	std::ifstream in(path, std::ios::binary);
	if (!in) RT_SKIP_MISSING_FIXTURE(path << " is not next to the working directory");
	std::vector<std::string> pinned;
	for (std::string l; std::getline(in, l);) {
		if (!l.empty() && l.back() == '\r') l.pop_back();
		if (l.empty() || l[0] == '#') continue;
		pinned.push_back(l);
	}
	const std::size_t common = std::min(now.size(), pinned.size());
	for (std::size_t i = 0; i < common; ++i)
		EXPECT_EQ(now[i], pinned[i]) << "line " << i + 1 << " differs from the snapshot (RT_UPDATE_SNAPSHOT=1 rewrites it if the change is intended)";
	EXPECT_EQ(now.size(), pinned.size()) << "the films or the instants changed (RT_UPDATE_SNAPSHOT=1 rewrites the snapshot if that is intended)";
}
