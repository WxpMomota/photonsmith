// film_universe_tests.cpp - round 10 of the film files: the universe - a set in space, planets, atmospheres, rings and moons, nebulae, galaxies, stars, comets,
// black holes, ships, people in space, its camera moves, its light and sound, and the way down to a planet. (film_spectacle_tests.cpp holds round 9.)

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "../../src/shared/cartoon/cartoons.h"

namespace ff = cartoon::film_file;

namespace {

const char *kOpenSet = R"(# an empty set
LookAt 0 40 200  0 30 0  0 1 0
Camera "perspective" "float fov" [ 40 ]
Film "rgb" "integer xresolution" [ 320 ] "integer yresolution" [ 180 ]
Sampler "halton" "integer pixelsamples" [ 16 ]
WorldBegin
LightSource "infinite" "rgb L" [ 0.2 0.25 0.3 ]
LightSource "distant" "point3 from" [ -4 6 5 ] "point3 to" [ 0 0 0 ] "rgb L" [ 2.2 1.9 1.55 ]
)";

// A film file in a temporary folder of its own with the open set; "" or the problem.
std::string loadFilm(const std::string &name, const std::string &filmText, ff::DataFilm &film) {
	const std::filesystem::path d = std::filesystem::temp_directory_path() / ("photonsmith_universe_test_" + name);
	std::filesystem::remove_all(d);
	std::filesystem::create_directories(d);
	std::ofstream(d / "s.pbrt", std::ios::binary) << kOpenSet;
	std::ofstream(d / (name + ".film"), std::ios::binary) << filmText;
	return film.load((d / (name + ".film")).string());
}

// The problem a bad line gives, in a film otherwise fine.
std::string problemOf(const std::string &line) {
	ff::DataFilm bad;
	return loadFilm("bad", "scene s.pbrt\nlength 4\n" + line + "\n", bad);
}

std::size_t count(const std::string &text, const std::string &what) {
	std::size_t n = 0;
	for (std::size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + 1)) ++n;
	return n;
}

// The numbers after `key` at its first occurrence in `text` from `from` on.
std::vector<double> numbersAfter(const std::string &text, const std::string &key, std::size_t from = 0, int n = 3) {
	std::vector<double> v;
	const std::size_t at = text.find(key, from);
	if (at == std::string::npos) return v;
	std::istringstream in(text.substr(at + key.size()));
	for (int i = 0; i < n; ++i) {
		double x = 0.0;
		if (in >> x) v.push_back(x);
	}
	return v;
}

void expectProblems(const std::vector<std::pair<std::string, std::string>> &cases) {
	for (const auto &[line, says] : cases) {
		const std::string problem = problemOf(line);
		EXPECT_NE(problem.find(says), std::string::npos) << line << " -> " << problem;
	}
}

}  // namespace

// ---- 1. the set in space ---------------------------------------------------------------------------------------------------------------------------------------------------

namespace sp = cartoon::space;

// Stars take their colours from their temperatures; the Milky Way is a band, brighter along it than at its pole; a space film has the starfield for its sky and the sun a
// glowing ball a thousand kilometres off where the line says, and none of its set's own sky lights.
TEST(FilmUniverseSpace, TheStarsHaveTheirColoursTheMilkyWayIsABandAndTheSunIsWhereItIsPut) {
	const cartoon::Vec3 cool = sp::blackbody(3000.0), sun = sp::blackbody(5800.0), hot = sp::blackbody(15000.0);
	EXPECT_GT(cool.x, 2.0 * cool.z) << "a cool star is orange-red";
	EXPECT_GT(hot.z, hot.x) << "a hot one blue-white";
	EXPECT_NEAR(sun.x / sun.z, 1.0, 0.25) << "the sun near white";
	EXPECT_NEAR(0.2126 * sun.x + 0.7152 * sun.y + 0.0722 * sun.z, 1.0, 0.05) << "luminance 1";
	EXPECT_GT(sp::milkyWayAt(sp::galacticCentre(), 1), 5.0 * sp::milkyWayAt(sp::galacticPole(), 1));

	ff::DataFilm film;
	ASSERT_EQ(loadFilm("space", "scene s.pbrt\nlength 4\nspace stars 1 milkyway 1 sun 200 10\nshot 0 A\n key 0 0 0  0 0 -100  50\n", film), "");
	const std::string s = film.sceneAt(1.0);
	EXPECT_NE(s.find("# space (film_space.h)"), std::string::npos);
	EXPECT_EQ(count(s, "LightSource \"distant\""), 0u) << "the set's own sun left out";
	EXPECT_EQ(s.find("\"rgb L\" [ 0.2 0.25 0.3 ]"), std::string::npos) << "and its sky";
	const std::vector<double> at = numbersAfter(s, "Translate ", s.find("AreaLightSource", s.find("# space")));
	ASSERT_EQ(at.size(), 3u);
	const cartoon::Vec3 d = sp::direction(200.0, 10.0) * sp::kSunDistance;
	EXPECT_NEAR(at[0], d.x, 1.0);
	EXPECT_NEAR(at[1], d.y, 1.0);
	EXPECT_NEAR(at[2], d.z, 1.0);
	expectProblems({{"space\nspace", "one `space` line"}, {"space stars 9", "stars and milkyway 0 to 4"}, {"space\nsky 12:00 12:00", "no `sky` with it"}});
}
