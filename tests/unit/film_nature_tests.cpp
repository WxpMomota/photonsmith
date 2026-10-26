// film_nature_tests.cpp - the nature of film files: the land (film_terrain.h `terrain`), and what is built on it.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>

#include "../../src/shared/cartoon/cartoons.h"

namespace ff = cartoon::film_file;
namespace terrain = cartoon::terrain;

namespace {

const char *kOpenSet = R"(# an empty set
LookAt 0 40 200  0 30 0  0 1 0
Camera "perspective" "float fov" [ 40 ]
Film "rgb" "integer xresolution" [ 320 ] "integer yresolution" [ 180 ]
Sampler "halton" "integer pixelsamples" [ 16 ]
WorldBegin
LightSource "infinite" "rgb L" [ 0.2 0.25 0.3 ]
)";

// A film file in a temporary folder of its own with the open set; "" or the problem.
std::string loadNature(const std::string &name, const std::string &filmText, ff::DataFilm &film) {
	const std::filesystem::path d = std::filesystem::temp_directory_path() / ("photonsmith_nature_test_" + name);
	std::filesystem::remove_all(d);
	std::filesystem::create_directories(d);
	std::ofstream(d / "s.pbrt", std::ios::binary) << kOpenSet;
	std::ofstream(d / (name + ".film"), std::ios::binary) << filmText;
	return film.load((d / (name + ".film")).string());
}

}  // namespace

// ---- the land ------------------------------------------------------------------------------------------------------------------------------------------------------------------

// A terrain is made from its numbers alone (the same every time, in every process), keeps within its height, leaves a valley floor round the focus of mountains, and is
// carved by water (its flow gathers into channels).
TEST(FilmTerrain, ATerrainIsTheSameEveryTimeStaysWithinItsHeightAndIsCarvedByWater) {
	terrain::Spec s;
	s.kind = "mountains";
	s.size = 800.0;
	s.height = 200.0;
	s.y = 5.0;
	s.seed = 4;
	const auto a = terrain::make(s), b = terrain::make(s);
	ASSERT_EQ(a->h.size(), b->h.size());
	EXPECT_TRUE(a->h == b->h) << "made from its numbers alone";
	EXPECT_EQ(s.key(), terrain::Spec(s).key());
	double lo = 1e9, hi = -1e9, nearFocus = 0.0, all = 0.0;
	int nNear = 0, nAll = 0;
	for (double x = -390.0; x <= 390.0; x += 10.0)
		for (double z = -390.0; z <= 390.0; z += 10.0) {
			const double h = a->heightAt(x, z);
			lo = std::min(lo, h);
			hi = std::max(hi, h);
			all += h, ++nAll;
			if (std::hypot(x, z) < 40.0) nearFocus += h, ++nNear;
		}
	EXPECT_GE(lo, s.y - 3.0) << "(the fine detail dips a little below the base)";
	EXPECT_LE(hi, s.y + s.height + 3.0);
	EXPECT_GT(hi - lo, 0.6 * s.height) << "it has its height";
	EXPECT_LT(nearFocus / nNear - s.y, 0.2 * s.height) << "a valley floor round the focus";
	EXPECT_GT(all / nAll, nearFocus / nNear + 0.15 * s.height);
	int channels = 0;
	for (float f : a->flow) channels += f > 0.5f;
	EXPECT_GT(channels, 100) << "water ran together into channels";
	terrain::Spec dry = s;
	dry.erosion = 0.0;
	const auto c = terrain::make(dry);
	EXPECT_FALSE(c->h == a->h) << "the erosion changed it";
	EXPECT_NE(dry.key(), s.key()) << "and the files it names";
	for (const char *kind : {"hills", "dunes", "canyon"}) {
		terrain::Spec k = s;
		k.kind = kind;
		const auto t = terrain::make(k);
		double kl = 1e9, kh = -1e9;
		for (float v : t->h) kl = std::min(kl, static_cast<double>(v)), kh = std::max(kh, static_cast<double>(v));
		EXPECT_NEAR(kl, 0.0, 1e-3) << kind;
		EXPECT_NEAR(kh, s.height, 0.08 * s.height) << kind;
	}
}

// The far ranges carry the land on beyond the square without a step: where they meet it they have its height, their first row is under it, and they rise outward.
TEST(FilmTerrain, TheFarRangesMeetTheSquaresEdgeWithoutAStep) {
	terrain::Spec s;
	s.kind = "mountains";
	s.size = 600.0;
	s.height = 150.0;
	s.seed = 7;
	const auto f = terrain::make(s);
	double worst = 0.0, worstNext = 0.0;
	for (int i = 0; i < 64; ++i) {
		const double a = 2.0 * cartoon::kPi * i / 64.0, rs = terrain::edgeRadius(s, a) * 0.9999;
		const double edge = f->heightAt(rs * std::cos(a), rs * std::sin(a));
		worst = std::max(worst, std::abs(terrain::farHeight(*f, a, 0.0) - edge));
		worstNext = std::max(worstNext, std::abs(terrain::farHeight(*f, a, 1.0 / 96.0) - edge));
		const double ri = terrain::ringRadius(s, a, -1.0);
		EXPECT_LT(terrain::farHeight(*f, a, -1.0), f->heightAt(ri * std::cos(a), ri * std::sin(a))) << "the first row is under the square";
	}
	EXPECT_LT(worst, 0.15) << "the ring has the square's height at its edge";
	EXPECT_LT(worstNext, 3.0) << "and carries on from it";
	double farMean = 0.0;
	for (int i = 0; i < 64; ++i) farMean += terrain::farHeight(*f, 2.0 * cartoon::kPi * i / 64.0, 0.6) / 64.0;
	EXPECT_GT(farMean, 0.3 * s.height) << "mountains beyond the square";
}

// A film file's terrain: its line, its errors, what stands on it, and its scene text (the surface with its painted colours; cliffs with pictures projected from the side; the
// far ranges unless they are turned off).
TEST(FilmTerrain, AFilmFilesTerrainCarriesWhatIsPlacedOnItAndIsSceneText) {
	ff::DataFilm film;
	ASSERT_EQ(loadNature("terrain", "scene s.pbrt\nlength 4\nterrain canyon size 240 height 40 seed 2 far off\nplace rock at 30 0.5 -20\nscatter grass 5 in -50 -50 50 50\ncharacter kai person at 10 0 10\n", film), "");
	EXPECT_TRUE(film.spec.hasTerrain);
	EXPECT_EQ(film.spec.terrain.kind, "canyon");
	EXPECT_DOUBLE_EQ(film.spec.terrain.size, 240.0);
	EXPECT_FALSE(film.spec.terrain.ranges);
	const auto ground = terrain::fieldOf(film.spec.terrain);
	ASSERT_FALSE(film.spec.placements.empty());
	EXPECT_NEAR(film.spec.placements[0].at.y, ground->heightAt(30.0, -20.0) + 0.5, 1e-9) << "a height above the ground";
	for (std::size_t i = 1; i < film.spec.placements.size(); ++i) {
		const auto &p = film.spec.placements[i];
		EXPECT_NEAR(p.at.y, ground->heightAt(p.at.x, p.at.z), 1e-9) << "scattered on the ground";
	}
	EXPECT_NEAR(film.spec.characters[0].start[1], ground->heightAt(10.0, 10.0), 1e-9) << "and the character stands on it";
	const std::string scene = film.sceneAt(1.0);
	EXPECT_NE(scene.find("# the terrain: canyon"), std::string::npos);
	EXPECT_EQ(std::count(scene.begin(), scene.end(), '\n') > 0, true);
	std::size_t shapes = 0;
	for (std::size_t at = scene.find("\"plymesh\""); at != std::string::npos; at = scene.find("\"plymesh\"", at + 1)) ++shapes;
	EXPECT_EQ(shapes, 3u) << "the ground and the cliffs facing either way (no far ranges)";
	EXPECT_NE(scene.find("\"string mapping\" \"planar\""), std::string::npos) << "the cliffs' pictures are projected from the side";
	// errors
	for (const auto &[line, says] : std::vector<std::pair<std::string, std::string>>{{"terrain volcano", "hills, mountains, dunes or canyon"},
																						 {"terrain hills size 5", "size (20 to 20000 m)"},
																						 {"terrain hills wobble 3", "a terrain takes"},
																						 {"terrain hills\nterrain dunes", "a film has one terrain"}}) {
		ff::DataFilm bad;
		const std::string problem = loadNature("terrain_bad", "scene s.pbrt\nlength 4\n" + line + "\n", bad);
		EXPECT_NE(problem.find(says), std::string::npos) << line << " -> " << problem;
	}
}
