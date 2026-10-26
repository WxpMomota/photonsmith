// film_nature_tests.cpp - the nature of film files: the land (film_terrain.h `terrain`), and what is built on it.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
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

// ---- water ---------------------------------------------------------------------------------------------------------------------------------------------------------------------

namespace water = cartoon::water;

// The sea's waves roll: the surface is the same at the same time, moves on with time, keeps within its height, and is written only where the camera looks.
TEST(FilmWater, TheSeasWavesRollAndAreDrawnWhereTheCameraLooks) {
	water::Sea sea;
	sea.level = 2.0;
	sea.waves = 1.0;
	double lo = 1e9, hi = -1e9, moved = 0.0;
	for (double x = -40.0; x <= 40.0; x += 1.7)
		for (double z = -40.0; z <= 40.0; z += 1.9) {
			cartoon::Vec3 p, n, q, m;
			double fold = 0.0;
			water::seaAt(sea, x, z, 3.0, 1.0, p, n, fold);
			lo = std::min(lo, p.y), hi = std::max(hi, p.y);
			EXPECT_GT(n.y, 0.0);
			water::seaAt(sea, x, z, 3.0, 1.0, q, m, fold);
			EXPECT_DOUBLE_EQ(p.y, q.y) << "the same at the same time";
			water::seaAt(sea, x, z, 4.0, 1.0, q, m, fold);
			moved = std::max(moved, std::abs(p.y - q.y));
		}
	EXPECT_GT(hi - lo, 0.5 * sea.waves) << "waves as high as asked";
	EXPECT_LT(hi - lo, 2.5 * sea.waves);
	EXPECT_NEAR(0.5 * (hi + lo), sea.level, 0.4 * sea.waves);
	EXPECT_GT(moved, 0.1) << "they move on";
	// written in front of the camera only (story space: looking along -z from the origin, 10 m up)
	const std::string text = water::seaText(sea, 1.0, {0.0, 10.0, 0.0}, {0.0, 0.0, -100.0}, 60.0, nullptr);
	ASSERT_NE(text.find("trianglemesh"), std::string::npos);
	std::size_t at = text.find("\"point3 P\" [ ");
	ASSERT_NE(at, std::string::npos);
	std::istringstream in(text.substr(at + 13, text.find(']', at) - at - 13));
	double x = 0.0, y = 0.0, z = 0.0;
	int behind = 0, points = 0;
	while (in >> x >> y >> z) {
		++points;
		if (z > 30.0) ++behind;   // (pbrt's z: the camera looks towards -z)
	}
	EXPECT_GT(points, 1000);
	EXPECT_EQ(behind, 0) << "nothing far behind the camera";
}

// A river's water never runs uphill, and on a terrain its bed is cut under it; a waterfall pours in the arc thrown water takes, its cliff raised behind it and its pool cut
// before it, and it is seen and heard: spray where it lands, the roar under the film.
TEST(FilmWater, RiversRunDownhillInTheirBedsAndWaterfallsStandOnTheirCliffs) {
	ff::DataFilm film;
	ASSERT_EQ(loadNature("water", "scene s.pbrt\nlength 6\nterrain hills size 400 height 40 seed 3 far off\nriver from 60 -80 width 6\nwaterfall at 0 12 -30 width 8 drop 12 facing 180\n", film), "");
	ASSERT_EQ(film.spec.rivers.size(), 1u);
	const auto ground = cartoon::terrain::fieldOf(film.spec.terrain);
	const std::vector<cartoon::Vec3> &path = film.spec.rivers[0].path;
	ASSERT_GT(path.size(), 10u);
	for (std::size_t i = 1; i < path.size(); ++i) EXPECT_LE(path[i].y, path[i - 1].y + 1e-9) << "water does not run uphill";
	int dry = 0;
	for (const cartoon::Vec3 &p : path)
		if (ground->inside(p.x, p.z) && ground->heightAt(p.x, p.z) > p.y - 0.2) ++dry;
	EXPECT_LE(dry, static_cast<int>(path.size()) / 20) << "its bed is under it (the fine detail may poke through here and there)";
	// the waterfall
	ASSERT_EQ(film.spec.falls.size(), 1u);
	const water::Fall &f = film.spec.falls[0];
	EXPECT_NEAR(water::fallPoint(f, 0.0, 0.0).y, f.lip.y, 1e-9);
	EXPECT_NEAR(water::fallFoot(f).y, f.lip.y - f.drop, 1e-6);
	EXPECT_LT(water::fallFoot(f).z, f.lip.z) << "it pours the way it faces (180: towards -z)";
	EXPECT_NEAR(ground->heightAt(f.lip.x, f.lip.z + 8.0), f.lip.y, 1.5) << "the cliff behind is raised to its lip";
	EXPECT_LT(ground->heightAt(water::fallFoot(f).x, water::fallFoot(f).z), f.lip.y - f.drop + 0.5) << "and the pool cut before it";
	const auto heard = [&](const std::string &kind) { return std::any_of(film.spec.ambience.begin(), film.spec.ambience.end(), [&](const auto &a) { return a.kind == kind; }); };
	EXPECT_TRUE(heard("falls"));
	EXPECT_TRUE(heard("water"));
	ASSERT_FALSE(film.spec.emitters.empty());
	EXPECT_EQ(film.spec.emitters.back().kind, "splash") << "spray where it lands";
	// a lake's shore: the ground just above its level is drawn under it
	ff::DataFilm lake;
	ASSERT_EQ(loadNature("lake", "scene s.pbrt\nlength 4\nterrain hills size 300 height 30 seed 5 far off\nwater lake level 6\n", lake), "");
	EXPECT_DOUBLE_EQ(lake.spec.terrain.waterLevel, 6.0);
	EXPECT_DOUBLE_EQ(lake.spec.seas[0].waves, 0.05) << "a lake is calm";
	const auto shore = cartoon::terrain::fieldOf(lake.spec.terrain);
	int justAbove = 0;
	for (double x = -140.0; x <= 140.0; x += 2.0)
		for (double z = -140.0; z <= 140.0; z += 2.0) {
			const double h = shore->heightAt(x, z);
			justAbove += h > 6.0 && h < 6.1;
		}
	EXPECT_LT(justAbove, 40) << "no scatter of islands a hand's breadth high";
	// errors
	for (const auto &[line, says] : std::vector<std::pair<std::string, std::string>>{{"water sea waves 2", "needs its `level Y`"},
																						 {"water level 0\nwater lake level 3", "one sea or lake"},
																						 {"river 0 0", "at least two waypoints"},
																						 {"river canyon", "no `terrain canyon`"},
																						 {"river from 0 0", "no `terrain`"},
																						 {"waterfall 0 1 2", "the middle of its lip"}}) {
		ff::DataFilm bad;
		const std::string problem = loadNature("water_bad", "scene s.pbrt\nlength 4\n" + line + "\n", bad);
		EXPECT_NE(problem.find(says), std::string::npos) << line << " -> " << problem;
	}
}

// ---- clouds --------------------------------------------------------------------------------------------------------------------------------------------------------------------

namespace clouds = cartoon::clouds;

// A cloud is a volume heaped up from a flat base, billowed at its edges; clouds are spread so their spheres never overlap, drift with the wind, and each is a medium in a
// pass-through sphere, its densities in a file of its own.
TEST(FilmClouds, CloudsHaveFlatBasesNeverOverlapAndDriftWithTheWind) {
	const std::vector<float> d = clouds::densities("cumulus", 2), e = clouds::densities("cumulus", 3);
	const int nx = 48, ny = 22, nz = 48;
	ASSERT_EQ(d.size(), static_cast<std::size_t>(nx) * ny * nz);
	EXPECT_FALSE(d == e) << "shapes of their own";
	double base = 0.0, middle = 0.0, top = 0.0;
	for (int k = 0; k < nz; ++k)
		for (int i = 0; i < nx; ++i) {
			base += d[(static_cast<std::size_t>(k) * ny + 0) * nx + i];
			middle += d[(static_cast<std::size_t>(k) * ny + ny / 3) * nx + i];
			top += d[(static_cast<std::size_t>(k) * ny + ny - 1) * nx + i];
		}
	EXPECT_EQ(base, 0.0) << "nothing below its flat base";
	EXPECT_GT(middle, 100.0) << "dense in its body";
	EXPECT_LT(top, middle * 0.3) << "and rounded on top";
	for (float v : d) ASSERT_TRUE(v >= 0.0f && v <= 1.0f);
	clouds::Clouds c;
	c.cover = 0.5;
	const std::vector<clouds::Placed> placed = clouds::layout(c);
	ASSERT_GT(placed.size(), 5u);
	for (std::size_t i = 0; i < placed.size(); ++i)
		for (std::size_t j = i + 1; j < placed.size(); ++j)
			EXPECT_GE(std::hypot(placed[i].at.x - placed[j].at.x, placed[i].at.z - placed[j].at.z),
					  clouds::sphereOf(c.kind, placed[i].width, placed[i].stretch) + clouds::sphereOf(c.kind, placed[j].width, placed[j].stretch))
				<< "their spheres do not overlap";
	const std::string a = clouds::text("photonsmith-test-clouds", c, 0.0), b = clouds::text("photonsmith-test-clouds", c, 10.0);
	std::size_t includes = 0;
	for (std::size_t at = a.find("Include \""); at != std::string::npos; at = a.find("Include \"", at + 1)) ++includes;
	EXPECT_EQ(includes, placed.size());
	EXPECT_NE(a.find("MediumInterface \"cloud_cumulus_1_0\" \"\""), std::string::npos);
	EXPECT_NE(a.find("Material \"interface\""), std::string::npos);
	EXPECT_NE(a, b) << "they drift";
	// the first cloud's sphere, 10 s later, is 60 m on with a 6 m/s wind towards +x (facing 90), unless it wrapped round the area
	const auto sphereAt = [](const std::string &text) {
		const std::size_t at = text.find("Material \"interface\"");
		const std::size_t tr = text.rfind("Translate ", at);
		double x = 0.0, y = 0.0, z = 0.0;
		std::istringstream(text.substr(tr + 10)) >> x >> y >> z;
		return cartoon::Vec3{x, y, z};
	};
	const cartoon::Vec3 pa = sphereAt(a), pb = sphereAt(b);
	EXPECT_NEAR(std::fmod(pb.x - pa.x + c.area, c.area), 60.0, 1e-3);
	EXPECT_NEAR(pb.z, pa.z, 1e-9);
	// a film file's line
	ff::DataFilm film;
	ASSERT_EQ(loadNature("clouds", "scene s.pbrt\nlength 4\nclouds towering cover 0.3 base 1500 seed 4\nclouds wisps\n", film), "");
	ASSERT_EQ(film.spec.cloudLayers.size(), 2u);
	EXPECT_EQ(film.spec.cloudLayers[0].kind, "towering");
	EXPECT_DOUBLE_EQ(film.spec.cloudLayers[0].base, 1500.0);
	EXPECT_DOUBLE_EQ(film.spec.cloudLayers[1].base, 5000.0) << "wisps are high";
	for (const auto &[line, says] : std::vector<std::pair<std::string, std::string>>{{"clouds fog", "cumulus, towering or wisps"}, {"clouds cumulus cover 2", "cover (0 to 1)"}}) {
		ff::DataFilm bad;
		const std::string problem = loadNature("clouds_bad", "scene s.pbrt\nlength 4\n" + line + "\n", bad);
		EXPECT_NE(problem.find(says), std::string::npos) << line << " -> " << problem;
	}
}
