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
	for (const auto &[line, says] : std::vector<std::pair<std::string, std::string>>{{"terrain volcano", "hills, mountains, dunes, canyon or coast"},
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

// A landscape with no `ambience` line is heard as what it is: wind over the land, birds in a wood by day, crickets in it after dark; an `ambience` line is heard instead.
TEST(FilmWater, ALandscapeIsHeardAsWhatItIsUnlessTheFilmSaysHow) {
	const auto kinds = [](const ff::DataFilm &f) {
		std::vector<std::string> k;
		for (const auto &a : f.spec.ambience) k.push_back(a.kind);
		return k;
	};
	const std::string land = "scene s.pbrt\nlength 4\nterrain hills size 200 height 20 seed 1 far off\n";
	ff::DataFilm bare, wood, night, said;
	ASSERT_EQ(loadNature("bare", land, bare), "");
	EXPECT_EQ(kinds(bare), (std::vector<std::string>{"wind"}));
	ASSERT_EQ(loadNature("wood", land + "sky 10:00 11:00\nforest broadleaf 40 seed 2\n", wood), "");
	EXPECT_EQ(kinds(wood), (std::vector<std::string>{"wind", "birds"}));
	ASSERT_EQ(loadNature("nightwood", land + "sky 23:00 23:30\nforest broadleaf 40 seed 2\n", night), "");
	EXPECT_EQ(kinds(night), (std::vector<std::string>{"wind", "crickets"}));
	ASSERT_EQ(loadNature("said", land + "ambience night 0.7\n", said), "");
	EXPECT_EQ(kinds(said), (std::vector<std::string>{"night"}));
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
	EXPECT_TRUE(heard("wind")) << "and the land's own wind, nothing being said of its sound";
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

// ---- forests -------------------------------------------------------------------------------------------------------------------------------------------------------------------

namespace forest = cartoon::forest;

// Trees grow where the land suits them - not on cliffs, not in the water, not in a river's bed, not above the tree line - a mixed wood by height (broadleaf low, conifers
// high), each tree drawn in full near the camera and as a far version beyond, with the season's leaves.
TEST(FilmForest, TreesGrowWhereTheLandSuitsThemAndAreDrawnNearOrFar) {
	ff::DataFilm film;
	ASSERT_EQ(loadNature("forest", "scene s.pbrt\nlength 4\nterrain mountains size 800 height 200 seed 3 far off\nwater lake level 12\nriver from 150 -200 width 8\nforest mixed 600 seed 2\nforest conifer 50 in -50 -50 50 50 season winter near 0\n", film), "");
	ASSERT_EQ(film.spec.planted.size(), 2u);
	const auto ground = cartoon::terrain::fieldOf(film.spec.terrain);
	const std::vector<forest::Tree> &trees = film.spec.planted[0];
	ASSERT_GT(trees.size(), 300u);
	double lowBroad = 0.0, highConifer = 0.0;
	int nBroad = 0, nConifer = 0;
	for (const forest::Tree &t : trees) {
		EXPECT_NEAR(t.at.y, ground->heightAt(t.at.x, t.at.z) - 0.05, 1e-6) << "standing on the ground";
		EXPECT_GE(t.at.y, 12.0 + 0.5) << "not in the lake";
		EXPECT_LE(1.0 - ground->normalAt(t.at.x, t.at.z, 2.0).y, 0.2 + 1e-9) << "not on a cliff";
		EXPECT_LE(t.at.y - film.spec.terrain.y, 0.55 * 200.0 + 6.0) << "not above the tree line";
		for (const cartoon::Vec3 &p : film.spec.rivers[0].path) ASSERT_GE(std::hypot(p.x - t.at.x, p.z - t.at.z), 0.5 * 8.0 + 1.0) << "not in the river";
		if (t.species == "broadleaf") lowBroad += t.at.y, ++nBroad;
		if (t.species == "conifer") highConifer += t.at.y, ++nConifer;
	}
	ASSERT_GT(nBroad, 10);
	ASSERT_GT(nConifer, 10);
	EXPECT_LT(lowBroad / nBroad, highConifer / nConifer) << "broadleaf trees lower down, conifers higher";
	// near and far: with `near 0` every tree is drawn far; a winter conifer carries snow
	const std::string far = forest::text("photonsmith-test-forest", film.spec.planted[1], "winter", 1.0, {0.0, 50.0, 0.0}, 0.0);
	EXPECT_NE(far.find("ObjectBegin \"forest_conifer_"), std::string::npos);
	EXPECT_EQ(far.find("_near\""), std::string::npos);
	EXPECT_NE(far.find("_far\""), std::string::npos);
	std::size_t placements = 0;
	for (std::size_t at = far.find("ObjectInstance"); at != std::string::npos; at = far.find("ObjectInstance", at + 1)) ++placements;
	EXPECT_EQ(placements, film.spec.planted[1].size());
	EXPECT_NE(far.find("[ 0.82000 0.84000 0.88000 ]"), std::string::npos) << "snow on the winter conifers";
	// the species grow: leaves in summer, none on a broadleaf tree in winter
	const auto summer = forest::detail::grow("broadleaf", 1, false, false), winter = forest::detail::grow("broadleaf", 1, true, false);
	EXPECT_GT(summer.foliage.tris.size(), 10000u);
	EXPECT_TRUE(winter.foliage.tris.empty());
	EXPECT_GT(winter.wood.tris.size(), summer.wood.tris.size()) << "(bare, more twigs show)";
	for (const std::string &sp : forest::species()) {
		EXPECT_FALSE(forest::detail::grow(sp, 0, false, false).wood.tris.empty()) << sp;
		EXPECT_FALSE(forest::detail::growFar(sp, 0, false).foliage.tris.empty()) << sp;
	}
	EXPECT_NE(forest::detail::foliageOf("broadleaf", "autumn", 0).x, forest::detail::foliageOf("broadleaf", "summer", 0).x) << "autumn colours";
	for (const auto &[line, says] : std::vector<std::pair<std::string, std::string>>{{"forest oak", "conifer, broadleaf, birch, palm, shrub or mixed"},
																						 {"forest mixed 100 season monsoon", "a forest takes"},
																						 {"forest mixed 0", "1 to 50000 trees"}}) {
		ff::DataFilm bad;
		const std::string problem = loadNature("forest_bad", "scene s.pbrt\nlength 4\n" + line + "\n", bad);
		EXPECT_NE(problem.find(says), std::string::npos) << line << " -> " << problem;
	}
}

// ---- wildlife ------------------------------------------------------------------------------------------------------------------------------------------------------------------

namespace wildlife = cartoon::wildlife;

// A murmuration flocks (worked out once: the same every time), its birds keeping apart, flying at about their speed, staying round where it was put; geese fly in a V behind
// their leader; a herd walks to where it is going, pausing to graze, on the ground.
TEST(FilmWildlife, FlocksSwirlRoundTheirPlaceGeeseFlyInAVAndHerdsGraze) {
	ff::DataFilm film;
	ASSERT_EQ(loadNature("wild", "scene s.pbrt\nlength 8\nterrain hills size 600 height 20 seed 6 far off\nflock starlings 300 at 0 40 -100 radius 40\nflock geese 7 from 50 30 0 to -150 30 -60\nflock butterflies 6 in -3 -1 3 -5\nherd deer 6 from -15 -20 to 30 -26 speed 1.2\n", film), "");
	ASSERT_EQ(film.spec.flocks.size(), 3u);
	const wildlife::Flock &f = film.spec.flocks[0];
	ASSERT_FALSE(f.pos.empty());
	const auto ground = cartoon::terrain::fieldOf(film.spec.terrain);
	EXPECT_NEAR(f.at.y, 40.0 + ground->heightAt(0.0, -100.0), 1e-9) << "its height above the land";
	wildlife::Flock again = f;
	again.pos.clear(), again.vel.clear();
	wildlife::prepare(again, 8.0);
	for (std::size_t i = 0; i < f.pos.back().size(); ++i) ASSERT_EQ(cartoon::length(again.pos.back()[i] - f.pos.back()[i]), 0.0) << "the same flight every time";
	const std::vector<cartoon::Vec3> &p = f.pos[static_cast<std::size_t>(6.0 / f.dt)], &v = f.vel[static_cast<std::size_t>(6.0 / f.dt)];
	cartoon::Vec3 mid{0, 0, 0};
	for (const cartoon::Vec3 &q : p) mid = mid + q * (1.0 / p.size());
	EXPECT_LT(cartoon::length(mid - f.at), 2.0 * f.radius) << "it stays round where it was put";
	int crowded = 0;
	for (std::size_t i = 0; i < p.size(); ++i) {
		EXPECT_GT(cartoon::length(v[i]), 0.5 * 11.0);
		EXPECT_LT(cartoon::length(v[i]), 1.35 * 11.0);
		double nearest = 1e9;
		for (std::size_t j = 0; j < p.size(); ++j)
			if (j != i) nearest = std::min(nearest, cartoon::length(p[i] - p[j]));
		crowded += nearest < 0.3;
	}
	EXPECT_LT(crowded, static_cast<int>(p.size()) / 20) << "they keep apart";
	const std::string a = wildlife::flockText(f, 6.0, ground.get());
	std::size_t placed = 0;
	for (std::size_t at = a.find("ObjectInstance"); at != std::string::npos; at = a.find("ObjectInstance", at + 1)) ++placed;
	EXPECT_EQ(placed, 300u);
	EXPECT_NE(a.find("ObjectBegin \"wild_starlings_0\""), std::string::npos);
	EXPECT_EQ(wildlife::flockText(f, 6.0, ground.get(), false).find("ObjectBegin"), std::string::npos) << "(another flock of the kind wrote the poses)";
	// the herd: on its way, stopping to graze, on the ground
	const wildlife::Herd &h = film.spec.herds[0];
	const std::string h0 = wildlife::herdText(h, 0.0, ground.get()), h6 = wildlife::herdText(h, 6.0, ground.get()), h60 = wildlife::herdText(h, 60.0, ground.get());
	EXPECT_NE(h0, h6) << "they walk";
	EXPECT_NE(h60.find("wild_deer_graze"), std::string::npos) << "and graze where they arrive";
	// the geese are heard; the film's own line errors
	EXPECT_TRUE(std::any_of(film.spec.ambience.begin(), film.spec.ambience.end(), [](const auto &am) { return am.kind == "geese"; }));
	for (const auto &[line, says] : std::vector<std::pair<std::string, std::string>>{{"flock bats 10 at 0 0 0", "starlings, geese, fish or butterflies"},
																						 {"flock geese 5", "from X Y Z to X Y Z"},
																						 {"flock starlings 20 radius 3", "needs its `at X Y Z`"},
																						 {"herd deer 5 from 0 0", "`herd deer COUNT from X Z to X Z"}}) {
		ff::DataFilm bad;
		const std::string problem = loadNature("wild_bad", "scene s.pbrt\nlength 4\n" + line + "\n", bad);
		EXPECT_NE(problem.find(says), std::string::npos) << line << " -> " << problem;
	}
}

// ---- the sky's spectacles ------------------------------------------------------------------------------------------------------------------------------------------------------

namespace skyfx = cartoon::skyfx;

// Lightning strikes at times of its own and flashes in strokes over a quarter of a second, lighting the land; thunder comes after it, later the further off it is; an aurora
// hangs in curtains of light; meteors streak and are gone; a rainbow needs a sun; the night sky has its stars.
TEST(FilmSkyFx, LightningFlashesInStrokesAndThunderFollowsAurorasHangAndRainbowsNeedASun) {
	ff::DataFilm film;
	ASSERT_EQ(loadNature("skyfx", "scene s.pbrt\nlength 12\nlightning 3 at 0 -2000 radius 500 during 1 11 seed 2\naurora green\nmeteors 30 during 0 10\nrainbow 0.6 during 2 8\nsky 22:00 22:00 south 0 latitude 64 day 355\n", film), "");
	ASSERT_EQ(film.spec.lightnings.size(), 1u);
	const auto &strikes = film.spec.lightnings[0].strikes;
	ASSERT_EQ(strikes.size(), 3u);
	for (std::size_t i = 0; i < strikes.size(); ++i) {
		EXPECT_GE(strikes[i].t, 1.0);
		EXPECT_LE(strikes[i].t, 11.0);
		if (i > 0) EXPECT_GT(strikes[i].t - strikes[i - 1].t, 1.5) << "apart in time";
	}
	const skyfx::Strike &s = strikes[0];
	double lit = 0.0;
	int flashes = 0;
	bool wasOn = false;
	for (double dt = 0.0; dt < 0.4; dt += 0.002) {
		const double v = skyfx::strokes(s, dt);
		lit += v;
		if (v > 0.5 && !wasOn) ++flashes;
		wasOn = v > 0.5;
	}
	EXPECT_GE(flashes, 3) << "it flickers in strokes";
	EXPECT_EQ(skyfx::strokes(s, 0.5), 0.0) << "and is over in a quarter of a second or so";
	std::string during;
	for (double dt = 0.0; dt < 0.3 && during.find("lightning strike") == std::string::npos; dt += 0.005) during = film.sceneAt(s.t + dt);
	EXPECT_NE(during.find("# a lightning strike"), std::string::npos) << "the bolt is drawn while it flashes";
	EXPECT_NE(during.find("LightSource \"point\""), std::string::npos) << "with its flash";
	EXPECT_EQ(film.sceneAt(s.t + 0.6).find("# a lightning strike"), std::string::npos);
	// thunder after it, later for a strike further off
	cartoon::audio::Mix near(30.0), far(30.0);
	skyfx::Lightning one = film.spec.lightnings[0];
	one.strikes = {s};
	skyfx::thunder(near, one, [](double, double, double) { return 500.0; });
	skyfx::thunder(far, one, [](double, double, double) { return 3000.0; });
	const auto firstSound = [](const cartoon::audio::Mix &m) {
		const std::vector<std::int16_t> pcm = m.finish(0.0, 0.0);
		for (std::size_t i = 0; i < pcm.size(); ++i)
			if (std::abs(pcm[i]) > 3) return static_cast<double>(i / 2) / cartoon::audio::kRate;
		return 1e9;
	};
	EXPECT_NEAR(firstSound(near), s.t + 500.0 / 343.0, 0.1);
	EXPECT_NEAR(firstSound(far), s.t + 3000.0 / 343.0, 0.4);
	// the aurora, the meteors, the stars, the rainbow
	const std::string night = film.sceneAt(5.0);
	EXPECT_NE(night.find("# the aurora"), std::string::npos);
	EXPECT_NE(night.find("aurora_green"), std::string::npos) << "its glow's picture";
	int streaks = 0;
	for (double t = 0.0; t < 10.0; t += 0.25) streaks += skyfx::meteorText(film.spec.meteors[0], t, {0.0, 2.0, 0.0}).find("# a meteor") != std::string::npos;
	EXPECT_GT(streaks, 3) << "meteors now and then";
	cartoon::film_sky::Spec dark;
	dark.night = 1.0, dark.stars = 1.0, dark.width = 256;
	const std::vector<float> nightSky = cartoon::film_sky::render(dark);
	float brightest = 0.0f, median = 0.0f;
	std::vector<float> lum;
	for (std::size_t i = 0; i < nightSky.size() / 2; i += 3) lum.push_back(nightSky[i + 1]), brightest = std::max(brightest, nightSky[i + 1]);
	std::nth_element(lum.begin(), lum.begin() + lum.size() / 2, lum.end());
	median = lum[lum.size() / 2];
	EXPECT_GT(brightest, 10.0f * median) << "stars in the night sky";
	cartoon::film_sky::Spec day, bow;
	day.width = bow.width = 256;
	day.sunElevation = bow.sunElevation = 20.0;
	bow.rainbow = 1.0;
	EXPECT_NE(cartoon::film_sky::fileName(day), cartoon::film_sky::fileName(bow));
	const std::vector<float> a = cartoon::film_sky::render(day), b = cartoon::film_sky::render(bow);
	double changed = 0.0;
	for (std::size_t i = 0; i < a.size(); ++i) changed += std::abs(a[i] - b[i]);
	EXPECT_GT(changed, 1.0) << "the rainbow is painted";
	ff::DataFilm bad;
	EXPECT_NE(loadNature("skyfx_bad", "scene s.pbrt\nlength 4\nrainbow 0.5\n", bad).find("needs a `sky` line"), std::string::npos);
}

// ---- aerial and time-lapse cameras, exposure -----------------------------------------------------------------------------------------------------------------------------------

namespace film_camera = cartoon::film_camera;

// A drone flies its waypoints at its height above the land (never lower than half of it, smoothed), looking ahead; time-lapse runs nature faster; the exposure follows
// the light from day into night, written into the Film line.
TEST(FilmCamera, DronesKeepAboveTheLandTimeLapseRunsNatureAndExposureFollowsTheLight) {
	terrain::Spec ts;
	ts.kind = "mountains", ts.size = 800.0, ts.height = 200.0, ts.seed = 3, ts.ranges = false;
	const auto ground = terrain::make(ts);
	const std::vector<std::pair<double, double>> way{{-300.0, 300.0}, {0.0, 0.0}, {300.0, -300.0}};
	const camera_keyframes::Path path = film_camera::flight(way, 60.0, false, {}, 0.0, ground.get());
	ASSERT_GE(path.keys.size(), 20u);
	EXPECT_NEAR(path.keys.front().pos[0], -300.0, 1e-6);
	EXPECT_NEAR(path.keys.back().pos[2], -300.0, 1e-6);
	for (std::size_t k = 0; k < path.keys.size(); ++k) {
		const auto &key = path.keys[k];
		EXPECT_GE(key.pos[1], ground->heightAt(key.pos[0], key.pos[2]) + 30.0 - 1e-6) << "above the land";
		EXPECT_LT(key.target[1], key.pos[1] + 1e-6) << "looking ahead and down";
		if (k > 0) EXPECT_LT(std::abs(key.pos[1] - path.keys[k - 1].pos[1]), 40.0) << "no lurches";
	}
	// time-lapse and exposure from a film file
	ff::DataFilm film;
	ASSERT_EQ(loadNature("flight", "scene s.pbrt\nlength 20\nterrain hills size 600 height 30 seed 2 far off\ntimelapse 60\nexposure auto\nsky 19:30 22:30 south 180 latitude 46\nshot 0 Over\n  fly -200 0  0 0  200 -100  height 40\n", film), "");
	EXPECT_DOUBLE_EQ(film.spec.timelapse, 60.0);
	EXPECT_FALSE(film.spec.shots[0].path.keys.empty()) << "the fly became the shot's keys";
	EXPECT_LT(film.exposureAt(0.0), film.exposureAt(19.0)) << "it opens up as night falls";
	EXPECT_NEAR(film_camera::autoExposure(20.0), 1.0, 1e-9);
	EXPECT_NEAR(film_camera::autoExposure(-15.0), std::pow(2.0, 4.9), 1e-6);
	const std::string header = "LookAt 0 0 0  0 0 -1  0 1 0\nCamera \"perspective\"\nFilm \"rgb\" \"integer xresolution\" [ 320 ]\nWorldBegin\n";
	const std::string brighter = film_camera::exposed(header, 4.0);
	EXPECT_NE(brighter.find("Film \"rgb\" \"float iso\" [ 400.0000 ]"), std::string::npos) << brighter;
	EXPECT_NE(film_camera::exposed(brighter, 2.0).find("\"float iso\" [ 800.0000 ]"), std::string::npos) << "an iso of its own is multiplied";
	EXPECT_EQ(film_camera::exposed(header, 1.0), header);
	EXPECT_NE(film.sceneAt(19.0).find("\"float iso\""), std::string::npos) << "a night frame is exposed";
	for (const auto &[line, says] : std::vector<std::pair<std::string, std::string>>{{"timelapse 0.5", "`timelapse FACTOR`"},
																						 {"exposure dark", "`exposure auto [KEY]`"},
																						 {"shot 0 A\n  fly 0 0", "at least two waypoints"},
																						 {"shot 0 A\n  key 0 1 0  0 0 -1  40\n  fly 0 0 5 5", "has a `fly` line and `key`"}}) {
		ff::DataFilm bad;
		const std::string problem = loadNature("flight_bad", "scene s.pbrt\nlength 4\n" + line + "\n", bad);
		EXPECT_NE(problem.find(says), std::string::npos) << line << " -> " << problem;
	}
}

// ---- weather that changes the land ---------------------------------------------------------------------------------------------------------------------------------------------

// Wet ground is darker and glossy (a coat over the land's picture scaled down), snow is mixed into it where the ground is level, both reached over their times; puddles lie
// only in hollows; gusts sweep across as fronts; a mist is the camera's own medium ended by a sphere round it.
TEST(FilmWeather, WetGroundIsDarkAndGlossySnowSettlesGustsSweepAndMistSurroundsTheCamera) {
	const cartoon::terrain::Weather dry, wet{1.0, 0.0}, snowy{0.0, 0.8};
	EXPECT_NE(terrain::groundMaterial("g", dry, "").find("Material \"diffuse\" \"texture reflectance\" \"g\""), std::string::npos);
	const std::string w = terrain::groundMaterial("g", wet, "");
	EXPECT_NE(w.find("\"scale\" \"texture tex\" \"g\" \"float scale\" [ 0.5500 ]"), std::string::npos) << w;
	EXPECT_NE(w.find("Material \"coateddiffuse\" \"texture reflectance\" \"g_wet\""), std::string::npos) << w;
	const std::string sn = terrain::groundMaterial("g", snowy, "mask.hdr");
	EXPECT_NE(sn.find("\"float scale\" [ 0.8000 ]"), std::string::npos) << sn;
	EXPECT_NE(sn.find("\"mix\" \"texture tex1\" \"g\" \"rgb tex2\""), std::string::npos) << sn;
	EXPECT_NE(sn.find("Material \"diffuse\" \"texture reflectance\" \"g_snowy\""), std::string::npos) << sn;
	ff::DataFilm film;
	ASSERT_EQ(loadNature("weather", "scene s.pbrt\nlength 10\nterrain hills size 400 height 20 seed 2 far off\nwet 0.8 from 2 to 6\nsnowcover 0.5\ngusts 2 wind 90\nmist 0.004 radius 300\n", film), "");
	EXPECT_DOUBLE_EQ(film.weatherAt(0.0).wet, 0.0);
	EXPECT_DOUBLE_EQ(film.weatherAt(8.0).wet, 0.8);
	EXPECT_GT(film.weatherAt(4.0).wet, 0.2);
	EXPECT_LT(film.weatherAt(4.0).wet, 0.6);
	EXPECT_DOUBLE_EQ(film.weatherAt(0.0).snow, 0.5) << "(from the start)";
	// puddles: each lies at the lowest of its hollow, and the ground round most of it is higher
	const auto ground = terrain::fieldOf(film.spec.terrain);
	const cartoon::Mesh puddles = terrain::puddleMesh(*ground);
	for (std::size_t i = 0; i < puddles.verts.size(); i += 12) {
		const cartoon::Vec3 c{-puddles.verts[i].x, puddles.verts[i].y, puddles.verts[i].z};
		EXPECT_LE(c.y, ground->heightAt(c.x, c.z) + 0.05) << "not floating above the ground";
	}
	// gusts: fronts that move with the wind
	const cartoon::dressing::Gusts &g = film.spec.gusts;
	EXPECT_NEAR(g.dx, -1.0, 1e-9) << "wind 90 is towards +x: -x in story space";
	double most = 0.0, least = 1e9;
	for (double x = -50.0; x <= 50.0; x += 1.0) {
		const double v = cartoon::length(cartoon::dressing::gustAt(g, x, 0.0, 3.0));
		most = std::max(most, v), least = std::min(least, v);
	}
	EXPECT_GT(most, 1.0);
	EXPECT_LT(least, 0.1) << "between the fronts it is still";
	// the mist: round the camera
	const std::string scene = film.sceneAt(1.0);
	EXPECT_NE(scene.find("MakeNamedMedium \"mist\""), std::string::npos);
	EXPECT_LT(scene.find("MakeNamedMedium \"mist\""), scene.find("Camera \"")) << "before the camera";
	EXPECT_NE(scene.find("MediumInterface \"mist\" \"\""), std::string::npos) << "and the sphere it ends at";
	for (const auto &[line, says] : std::vector<std::pair<std::string, std::string>>{{"wet 2", "`wet AMOUNT [from T0 to T1]`"},
																						 {"snowcover 0.5 from 3", "`snowcover AMOUNT [from T0 to T1]`"},
																						 {"gusts 9", "`gusts STRENGTH"},
																						 {"mist 3", "`mist DENSITY"}}) {
		ff::DataFilm bad;
		const std::string problem = loadNature("weather_bad", "scene s.pbrt\nlength 4\n" + line + "\n", bad);
		EXPECT_NE(problem.find(says), std::string::npos) << line << " -> " << problem;
	}
}

// ---- under the sea -------------------------------------------------------------------------------------------------------------------------------------------------------------

namespace underwater = cartoon::underwater;

// Under the film's water the camera is in the sea's medium (ended by a sphere whose top is the surface), the surface is seen from below as thin water, caustics lie on the
// seabed and a projector shines shafts down; the net of light loops and tiles; kelp sways and coral stands on the seabed.
TEST(FilmUnderwater, TheCameraIsInTheSeaUnderAThinSurfaceWithCausticsShaftsKelpAndCoral) {
	ff::DataFilm film;
	ASSERT_EQ(loadNature("reef", "scene s.pbrt\nlength 10\nwater sea level 0 waves 0.8\nunderwater clarity 1.5 colour teal\nkelp 20 in -10 -20 10 -5 height 8\ncoral 15 in -5 -10 5 -2\nshot 0 Reef\n  key 0 -10 6   0 -12 -20   60\n", film), "");
	EXPECT_TRUE(film.spec.hasUnderwater);
	EXPECT_TRUE(film.spec.seas[0].fromBelow);
	const std::string scene = film.sceneAt(2.0);
	EXPECT_LT(scene.find("MakeNamedMedium \"sea\""), scene.find("Camera \"")) << "the camera is in the sea";
	EXPECT_NE(scene.find("MediumInterface \"sea\" \"\""), std::string::npos) << "and the sphere it ends at";
	EXPECT_NE(scene.find("\"float radius\" [ 3000.000 ]"), std::string::npos) << "so big its top is the surface";
	EXPECT_NE(scene.find("Material \"thindielectric\""), std::string::npos) << "the surface from below";
	EXPECT_NE(scene.find("# caustics on the seabed"), std::string::npos);
	EXPECT_NE(scene.find("LightSource \"projection\""), std::string::npos) << "shafts of light";
	EXPECT_NE(scene.find("# kelp"), std::string::npos);
	EXPECT_NE(scene.find("# coral"), std::string::npos);
	EXPECT_NE(scene.find("# marine snow"), std::string::npos);
	EXPECT_NE(film.sceneAt(2.0).find(underwater::causticPicture(film.spec.key, underwater::causticFrame(2.0), 512)), std::string::npos);
	EXPECT_EQ(underwater::causticFrame(0.05), underwater::causticFrame(2.45)) << "the net loops every 2.4 s";
	EXPECT_NE(underwater::kelpText(film.spec.kelps[0], 0.0, [](double, double) { return -15.0; }), underwater::kelpText(film.spec.kelps[0], 1.0, [](double, double) { return -15.0; })) << "kelp sways";
	for (const auto &[line, says] : std::vector<std::pair<std::string, std::string>>{{"underwater", "needs a `water ... level Y` line"},
																						 {"water sea level 0\nunderwater\nmist 0.01", "use one"},
																						 {"water sea level 0\nunderwater clarity 0", "`underwater [clarity C]"},
																						 {"kelp 0", "`kelp COUNT"}}) {
		ff::DataFilm bad;
		const std::string problem = loadNature("reef_bad", "scene s.pbrt\nlength 4\n" + line + "\n", bad);
		EXPECT_NE(problem.find(says), std::string::npos) << line << " -> " << problem;
	}
}

// ---- fire, lava and smoke ------------------------------------------------------------------------------------------------------------------------------------------------------

namespace firefx = cartoon::fire;

// Flames are a glowing volume that flickers frame to frame, with a light among them and sparks; smoke rises above; a volcano is a cone in the land with a crater, erupting a
// plume, fountains and lava flows that creep down from its rim; a wildfire's front moves across with time.
TEST(FilmFire, FlamesFlickerVolcanoesRiseFromTheLandAndLavaCreepsDown) {
	const std::string a = firefx::flameText("f", {0, 0, 0}, 1.2, 1.0, 1, 10), b = firefx::flameText("f", {0, 0, 0}, 1.2, 1.1, 1, 10);
	EXPECT_NE(a.find("\"rgbgrid\""), std::string::npos);
	EXPECT_NE(a.find("\"rgb Le\""), std::string::npos);
	EXPECT_NE(a.find("LightSource \"point\""), std::string::npos) << "a light among the flames";
	EXPECT_NE(a, b) << "they flicker";
	EXPECT_NE(firefx::smokeText("s", {0, 2, 0}, 3.0, 10.0, 1.0, 1).find("\"uniformgrid\""), std::string::npos);
	ff::DataFilm film;
	ASSERT_EQ(loadNature("fire", "scene s.pbrt\nlength 10\nterrain hills size 3000 height 40 seed 3 far off\nvolcano at 0 -800 height 300 radius 600\nfire at 50 0 50 size 1.5\nwildfire in -300 100 300 400 during 0 10 wind 0\n", film), "");
	const auto ground = terrain::fieldOf(film.spec.terrain);
	EXPECT_GT(ground->heightAt(0.0, -800.0 + 0.1 * 600.0), 0.8 * 300.0) << "the cone rises from the land";
	EXPECT_LT(ground->heightAt(0.0, -800.0), ground->heightAt(0.0, -800.0 + 0.1 * 600.0)) << "a crater in its top";
	EXPECT_EQ(film.spec.lavas.size(), 3u) << "lava runs down its flanks";
	for (const auto &l : film.spec.lavas) {
		ASSERT_GT(l.path.size(), 20u);
		EXPECT_LT(l.path.back().y, l.path.front().y - 100.0) << "downhill";
	}
	const auto &l = film.spec.lavas[0];
	const auto tris = [](const std::string &text) { return std::count(text.begin(), text.end(), '\n'); };
	EXPECT_GE(tris(firefx::lavaText(film.spec.key, l, 5.0)), tris(firefx::lavaText(film.spec.key, l, 1.0))) << "its front advances";
	EXPECT_NEAR(film.spec.fires[0].at.y, ground->heightAt(50.0, 50.0), 1e-9) << "the fire stands on the land";
	int sparks = 0;
	for (const auto &e : film.spec.emitters) sparks += e.kind == "sparks";
	EXPECT_EQ(sparks, 2) << "sparks from the fire and the crater's fountains";
	const std::string s1 = film.sceneAt(2.0), s2 = film.sceneAt(8.0);
	EXPECT_NE(s1.find("MediumInterface \"wild_0_0\""), std::string::npos) << "the wildfire's flames";
	EXPECT_NE(s1.find("MediumInterface \"plume_0\""), std::string::npos) << "the volcano's plume";
	EXPECT_NE(s1.substr(s1.find("wild_0_0")), s2.substr(s2.find("wild_0_0"))) << "the front moves";
	for (const auto &[line, says] : std::vector<std::pair<std::string, std::string>>{{"lava from 0 0", "the film needs a `terrain` line"},
																						 {"fire 0 0 0", "`fire at X Y Z"},
																						 {"fire at 0 0 0 size 500", "`fire at X Y Z"},
																						 {"wildfire in 0 0 1", "`wildfire in X0 Z0 X1 Z1"}}) {
		ff::DataFilm bad;
		const std::string problem = loadNature("fire_bad", "scene s.pbrt\nlength 4\n" + line + "\n", bad);
		EXPECT_NE(problem.find(says), std::string::npos) << line << " -> " << problem;
	}
}

// Fog in the valleys (`fog`: height_fog.h): every frame's camera medium is a "heightfog" layer before its Camera line, its top a quarter of the land's height unless given;
// lifting, it thins away and sinks; it does not go with a mist or the sea round the camera.
TEST(FilmFog, FogLiesInTheValleysAndLifts) {
	ff::DataFilm film;
	ASSERT_EQ(loadNature("fog", "scene s.pbrt\nlength 10\nterrain hills size 300 height 80 seed 2 far off\nfog 0.04 lift 4 8\nshot 0 A\n key 0 60 150  0 20 0\n", film), "");
	EXPECT_DOUBLE_EQ(film.spec.fog.top, 20.0) << "a quarter of the land's height";
	EXPECT_NEAR(film.spec.fog.thickness, 4.8, 1e-9);
	const std::string early = film.sceneAt(1.0);
	const std::size_t medium = early.find("\"heightfog\""), camera = early.find("Camera \"");
	ASSERT_NE(medium, std::string::npos);
	EXPECT_LT(medium, camera) << "before the Camera line: the camera's own medium";
	EXPECT_NE(early.find("MediumInterface \"\" \"valleyfog\""), std::string::npos);
	EXPECT_NE(early.find("\"float top\" [ 20.0000 ]"), std::string::npos) << early.substr(medium, 300);
	const auto sigmaS = [](const std::string &s) {
		const std::size_t at = s.find("\"rgb sigma_s\" [ ");
		return at == std::string::npos ? 0.0 : std::atof(s.c_str() + at + 16);
	};
	EXPECT_NEAR(sigmaS(early), 0.04, 1e-6);
	EXPECT_LT(sigmaS(film.sceneAt(6.0)), 0.04) << "lifting";
	EXPECT_GT(sigmaS(film.sceneAt(6.0)), 0.0);
	EXPECT_EQ(film.sceneAt(9.0).find("\"heightfog\""), std::string::npos) << "gone";
	for (const auto &[line, says] : std::vector<std::pair<std::string, std::string>>{{"fog", "`fog DENSITY"},
																						{"fog 0.05 thickness 0", "thickness is above 0"},
																						{"fog 0.05 colour red", "takes top Y"},
																						{"fog 0.05\nmist 0.01", "`fog` and `mist`"}}) {
		ff::DataFilm bad;
		const std::string problem = loadNature("fog_bad", "scene s.pbrt\nlength 4\n" + line + "\n", bad);
		EXPECT_NE(problem.find(says), std::string::npos) << line << " -> " << problem;
	}
}

// The sea as an ocean spectrum (film_ocean.h): its significant height is the sea's, it is the same every time it is made and repeats over its tile, it moves on with time,
// and sharp crests fold over (foam) where a gentle sea does not.
TEST(FilmOcean, TheSpectrumMakesTheSeasWavesAndTheyMove) {
	namespace oc = cartoon::ocean;
	const oc::Tile a = oc::tileAt(2.0, 30.0, 0.8, 300.0, 5.0, 4001);
	double sum = 0.0, sum2 = 0.0;
	for (double h : a.h) sum += h, sum2 += h * h;
	const double mean = sum / a.h.size(), rms = std::sqrt(sum2 / a.h.size() - mean * mean);
	EXPECT_NEAR(4.0 * rms, 2.0, 0.05) << "the significant height is four times the surface's rms";
	EXPECT_NEAR(mean, 0.0, 0.01);
	const oc::Tile again = oc::tileAt(2.0, 30.0, 0.8, 300.0, 5.0, 4001);
	EXPECT_EQ(a.h, again.h) << "the same every time";
	EXPECT_NEAR(oc::sample(a, 12.3, -45.6).h, oc::sample(a, 12.3 + 300.0, -45.6 - 600.0).h, 1e-9) << "it repeats over its tile";
	const oc::Tile later = oc::tileAt(2.0, 30.0, 0.8, 300.0, 6.0, 4001);
	double moved = 0.0;
	for (std::size_t i = 0; i < a.h.size(); ++i) moved += std::abs(later.h[i] - a.h[i]);
	EXPECT_GT(moved / a.h.size(), 0.05) << "a second later the waves have moved";
	double sharp = 0.0, gentle = 0.0;
	for (double f : a.fold) sharp = std::max(sharp, f);
	for (double f : oc::tileAt(2.0, 30.0, 0.1, 300.0, 5.0, 4001).fold) gentle = std::max(gentle, f);
	EXPECT_GT(sharp, gentle) << "sharp crests fold over more";
	EXPECT_GT(sharp, 0.3);
	// the waves run along the wind: the slopes across the wind are smaller than along it
	double along = 0.0, across = 0.0;
	const double wx = std::sin(30.0 * cartoon::kPi / 180.0), wz = std::cos(30.0 * cartoon::kPi / 180.0);
	for (std::size_t i = 0; i < a.h.size(); ++i) {
		along += std::abs(a.sx[i] * wx + a.sz[i] * wz);
		across += std::abs(a.sx[i] * wz - a.sz[i] * wx);
	}
	EXPECT_GT(along, 1.5 * across);
	// and the film's sea uses it
	ff::DataFilm film;
	ASSERT_EQ(loadNature("ocean", "scene s.pbrt\nlength 4\nwater sea level 0 waves 1.5\nshot 0 A\n key 0 3 20  0 0 -50\n", film), "");
	EXPECT_TRUE(film.spec.seas[0].spectral);
	cartoon::Vec3 p, n;
	double fold = 0.0;
	cartoon::water::seaAt(film.spec.seas[0], 10.0, -20.0, 1.0, 1.0, p, n, fold);
	EXPECT_LT(std::abs(p.y), 2.0);
	EXPECT_GT(n.y, 0.5);
	ff::DataFilm old;
	ASSERT_EQ(loadNature("ocean_old", "scene s.pbrt\nlength 4\nwater sea level 0 waves 1.5 model gerstner\n", old), "");
	EXPECT_FALSE(old.spec.seas[0].spectral);
}

// Surf on a coast (film_surf.h): the coast's sea floor shelves up to a beach at the sea's level; the swell slows in the shallows and turns to meet the shore, grows there and
// breaks white, keeps the water's mean level, and the film draws the shallows clear over sand netted with caustics.
TEST(FilmSurf, TheSwellTurnsToTheShoreGrowsAndBreaksWhite) {
	ff::DataFilm film;
	ASSERT_EQ(loadNature("surf", "scene s.pbrt\nlength 6\nterrain coast size 400 far off\nwater sea level 0 waves 0.4\nsurf 1.5 period 10 toward 45\nshot 0 A\n key 0 4 -10  0 0 -80\n", film), "");
	EXPECT_DOUBLE_EQ(film.spec.terrain.y, -10.0) << "a coast's beach is put at the sea's level";
	const std::shared_ptr<const terrain::Field> ground = terrain::fieldOf(film.spec.terrain);
	EXPECT_LT(ground->heightAt(0.0, -180.0), -5.0) << "the sea floor off the coast";
	EXPECT_GT(ground->heightAt(0.0, 150.0), 2.0) << "the land behind";
	const cartoon::surf::Swell &sw = film.spec.swell;
	ASSERT_TRUE(sw.on);
	EXPECT_DOUBLE_EQ(sw.toward, 45.0);
	const std::shared_ptr<const cartoon::surf::Grid> grid = cartoon::surf::gridOf(*ground, 0.0, sw);
	// a point in the shallows along a line straight out from the shore at x = 0
	double zShallow = 0.0, zDeep = 0.0;
	for (double z = 100.0; z > -200.0; z -= 0.5) {
		const double d = -ground->heightAt(0.0, z);
		if (zShallow == 0.0 && d > 1.2) zShallow = z;
		if (d > grid->deep) {
			zDeep = z;
			break;
		}
	}
	ASSERT_NE(zShallow, 0.0);
	ASSERT_NE(zDeep, 0.0);
	// refraction: the crests' way of running (the arrival time's gradient) turns from 45 degrees out at sea towards the shore's normal (+z) in the shallows
	const auto heading = [&](double x, double z) {
		bool in = false;
		const double e = 2.0, gx = grid->sample(grid->arrive, x + e, z, in) - grid->sample(grid->arrive, x - e, z, in), gz = grid->sample(grid->arrive, x, z + e, in) - grid->sample(grid->arrive, x, z - e, in);
		return std::atan2(gx, gz) * 180.0 / cartoon::kPi;
	};
	EXPECT_NEAR(heading(0.0, zDeep - 20.0), 45.0, 8.0);
	EXPECT_LT(std::abs(heading(0.0, zShallow)), 32.0) << "turned to meet the shore";
	// shoaling and breaking: the water in the shallows rises higher than the swell out at sea, never above 0.78 of its depth, keeps its mean, and goes white
	double lo = 1e9, hi = -1e9, sum = 0.0, white = 0.0;
	const int steps = 400;
	for (int k = 0; k < steps; ++k) {
		const cartoon::surf::At a = cartoon::surf::at(*grid, sw, 0.0, zShallow, k * sw.period / steps);
		lo = std::min(lo, a.rise), hi = std::max(hi, a.rise), sum += a.rise, white = std::max(white, a.white);
		EXPECT_TRUE(a.shallow);
		EXPECT_LT(a.open, 0.2) << "the open sea's waves have given way";
	}
	EXPECT_NEAR(sum / steps, 0.0, 0.05) << "the mean level stays";
	EXPECT_LE(hi - lo, 0.78 * -ground->heightAt(0.0, zShallow) + 1e-6);
	EXPECT_GT(hi - lo, 0.5);
	EXPECT_GT(white, 0.9) << "breaking";
	const cartoon::surf::At out = cartoon::surf::at(*grid, sw, 0.0, zDeep - 30.0, 1.0);
	EXPECT_EQ(out.white, 0.0);
	EXPECT_DOUBLE_EQ(out.open, 1.0);
	// the film draws it: clear shallows and caustics under them
	const std::string scene = film.sceneAt(2.0);
	EXPECT_NE(scene.find("# the shallows: clear water"), std::string::npos);
	EXPECT_NE(scene.find("# the sand under the water, with its caustics"), std::string::npos);
	ff::DataFilm plain;
	ASSERT_EQ(loadNature("surf_wind", "scene s.pbrt\nlength 4\nterrain coast size 400 far off\nwater sea level 0 wind 20\nsurf 1\n", plain), "");
	EXPECT_DOUBLE_EQ(plain.spec.swell.toward, 20.0) << "the sea's wind unless given";
	for (const auto &[text, says] : std::vector<std::pair<std::string, std::string>>{{"surf 1\n", "`water sea` line and a `terrain`"},
																						  {"water sea level 0\nsurf 1\n", "`water sea` line and a `terrain`"},
																						  {"surf 0\n", "`surf HEIGHT"},
																						  {"surf 1 colour 2\n", "surf takes period"},
																						  {"surf 1 period\n", "needs a number"}}) {
		ff::DataFilm bad;
		const std::string problem = loadNature("surf_bad", "scene s.pbrt\nlength 4\n" + text, bad);
		EXPECT_NE(problem.find(says), std::string::npos) << text << " -> " << problem;
	}
}
