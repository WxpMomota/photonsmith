// film_blender3_tests.cpp - the third round of techniques borrowed from Blender for the films: smear frames (film_motion.h), the rigid-body world (rigid_world.h), the
// shadow terminator offset on every material (`terminator`), a lens's blades (aperture_shape.h, `aperture ... blades N`), and the onion-skin sheet's cast (onion_frame.h).
// (The toon rim and hatching are in toon_look_tests.cpp, the print looks in lens_look_tests.cpp, the terminator's renders in shadow_terminator_tests.cpp.)

#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "../../src/shared/aperture_shape.h"
#include "../../src/shared/cartoon/cartoon_films.h"   // (the films themselves, which these tests reach into)
#include "../../src/shared/cartoon/rigid_world.h"
#include "../../launcher/film_temporal.h"   // (the print after a temporal denoise)

namespace ff = cartoon::film_file;

namespace {

const char *kBlocks = R"(# a floor and three blocks in a stack
LookAt 0 1.2 4  0 0.4 0  0 1 0
Camera "perspective" "float fov" [ 40 ]
Film "rgb" "integer xresolution" [ 320 ] "integer yresolution" [ 180 ]
WorldBegin
LightSource "distant" "point3 from" [ 3 6 4 ] "point3 to" [ 0 0 0 ] "rgb L" [ 2 2 2 ]
AttributeBegin
  Material "diffuse" "rgb reflectance" [ 0.5 0.5 0.5 ]
  Shape "trianglemesh" "integer indices" [ 0 1 2  0 2 3 ] "point3 P" [ -20 0 -20   20 0 -20   20 0 20   -20 0 20 ]
AttributeEnd
MakeNamedMaterial "red" "string type" [ "diffuse" ] "rgb reflectance" [ 0.8 0.2 0.2 ]
# @anim b1
AttributeBegin
  NamedMaterial "red"
  Translate 0 0.15 0
  Shape "sphere" "float radius" [ 0.15 ]
AttributeEnd
)";

std::string loadFilm(const std::string &name, const std::string &filmText, ff::DataFilm &film) {
	const std::filesystem::path d = std::filesystem::temp_directory_path() / ("photonsmith_blender3_test_" + name);
	std::filesystem::remove_all(d);
	std::filesystem::create_directories(d);
	std::ofstream(d / "s.pbrt", std::ios::binary) << kBlocks;
	std::ofstream(d / (name + ".film"), std::ios::binary) << filmText;
	return film.load((d / (name + ".film")).string());
}

std::size_t count(const std::string &text, const std::string &what) {
	std::size_t n = 0;
	for (std::size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + 1)) ++n;
	return n;
}

}  // namespace

// ---- smear frames ----

TEST(FilmSmear, ADashIsDrawnOutBehindFromItsLeadingSideAndAWalkIsNot) {
	namespace mo = cartoon::motion;
	const auto at = [](double speed) { return [speed](double t) { return cartoon::Vec3{speed * t, 0.0, 0.0}; }; };
	const cartoon::Mat4 walk = mo::smearStretch(at(1.2), 1.0, 1.0, 1.0, 24);
	for (int i = 0; i < 16; ++i) EXPECT_NEAR(walk[i], i % 5 == 0 ? 1.0 : 0.0, 1e-12) << "a walk is drawn as it is";
	const cartoon::Mat4 dash = mo::smearStretch(at(8.0), 1.0, 1.0, 1.5, 24);
	const cartoon::Vec3 lead{8.0 + 0.5, 0.5, 0.0}, tail{8.0 - 0.5, 0.5, 0.0};
	const cartoon::Vec3 leadAfter = cartoon::transformPoint(dash, lead), tailAfter = cartoon::transformPoint(dash, tail);
	EXPECT_NEAR(cartoon::length(leadAfter - lead), 0.0, 1e-9) << "its leading side stays where it is";
	EXPECT_LT(tailAfter.x, tail.x - 0.2) << "its back is drawn out behind it";
	EXPECT_EQ(mo::smearTimes(at(8.0), 1.0, 1.0, 1.5, 24, 3).size(), 3u) << "a dash leaves its multiples";
	EXPECT_TRUE(mo::smearTimes(at(1.2), 1.0, 1.0, 1.5, 24, 3).empty()) << "a walk none";
	for (const double u : mo::smearTimes(at(8.0), 1.0, 1.0, 1.5, 24, 3)) EXPECT_LT(u, 1.0) << "back along its way";

	ff::DataFilm film;
	EXPECT_EQ(loadFilm("smear", "scene s.pbrt\nlength 2\nsmear 1.5 multiples 2\n", film), "");
	ASSERT_EQ(film.spec.smears.size(), 1u);
	EXPECT_EQ(film.spec.smears[0].multiples, 2);
	ff::DataFilm bad;
	EXPECT_NE(loadFilm("smear_bad", "scene s.pbrt\nlength 2\nsmear 5\n", bad), "");
	EXPECT_NE(loadFilm("smear_who", "scene s.pbrt\nlength 2\nsmear nobody 1\n", bad), "");
}

// ---- the rigid-body world ----

TEST(FilmRigid, ADroppedBoxLandsUprightAndAStackStandsUntilItIsKnocked) {
	namespace rg = cartoon::rigid;
	rg::BodyDef box;
	box.block = "box", box.half = {0.15, 0.15, 0.15}, box.at = {0.0, 1.0, 0.0}, box.start = 0.0;
	const rg::Run drop = rg::simulate({box}, 3.0);
	const rg::Pose end = rg::posesAt(drop, 3.0)[0];
	EXPECT_NEAR(end.at.y, 0.15, 0.01) << "it rests on the ground";
	EXPECT_GT(std::fabs(end.q.w), 0.999) << "upright, as it fell";
	ASSERT_FALSE(drop.impacts.empty()) << "and its landing is heard";
	EXPECT_NEAR(drop.impacts.front().t, std::sqrt(2.0 * 0.85 / 9.81), 0.03);

	std::vector<rg::BodyDef> stack;
	for (int i = 0; i < 3; ++i) {
		rg::BodyDef b;
		b.block = "b" + std::to_string(i), b.half = {0.15, 0.15, 0.15}, b.at = {0.0, 0.15 + 0.3 * i, 0.0};
		stack.push_back(b);
	}
	const std::vector<rg::Pose> standing = rg::posesAt(rg::simulate(stack, 2.0), 2.0);
	for (int i = 0; i < 3; ++i) EXPECT_NEAR(cartoon::length(standing[static_cast<std::size_t>(i)].at - stack[static_cast<std::size_t>(i)].at), 0.0, 1e-9) << "untouched, it stands";
	rg::BodyDef ball;
	ball.block = "ball", ball.sphere = true, ball.radius = 0.15, ball.at = {-2.0, 0.15, 0.0}, ball.start = 0.2, ball.velocity = {5.0, 0.0, 0.0}, ball.mass = 3.0;
	stack.push_back(ball);
	const std::vector<rg::Pose> knocked = rg::posesAt(rg::simulate(stack, 3.0), 3.0);
	EXPECT_LT(knocked[2].at.y, 0.5) << "knocked, its top block falls";
	for (const rg::Pose &p : knocked) EXPECT_GT(p.at.y, 0.1) << "and nothing sinks through the ground";
}

TEST(FilmRigid, AFilmsRigidLineMovesItsBlockFromWhereTheSceneHasIt) {
	ff::DataFilm film;
	ASSERT_EQ(loadFilm("rigid", "scene s.pbrt\nlength 3\nrigid b1 sphere 0.15 at 0 0.15 0 start 0.5 velocity 2 0 0\n", film), "");
	EXPECT_TRUE(film.hasEvents());
	const std::string before = film.sceneAt(0.2), after = film.sceneAt(2.0);
	EXPECT_NE(before.find("@anim b1"), std::string::npos);
	EXPECT_NE(after.find("Rotate"), std::string::npos) << "rolling as it goes";
	EXPECT_NE(before, after) << "it has rolled away";
	ff::DataFilm bad;
	EXPECT_NE(loadFilm("rigid_bad", "scene s.pbrt\nlength 3\nrigid b1 cone 1 at 0 0 0\n", bad), "");
	EXPECT_NE(loadFilm("rigid_mass", "scene s.pbrt\nlength 3\nrigid b1 sphere 0.1 at 0 0 0 mass 0\n", bad), "");
}

// ---- an old film's seed, and its print after a temporal denoise ----

TEST(FilmVintage, EveryFrameOfAnOldFilmHasItsOwnSeedWhereverItsLookLineComesFrom) {
	namespace sc = cartoon::shot_cameras;
	const std::string scene = "# @rt-look vintage 1\nLookAt 0 0 0  0 0 -1  0 1 0\nWorldBegin\n";
	const std::string a = sc::withFrameSeed(scene, 1.0), b = sc::withFrameSeed(scene, 1.0417);
	EXPECT_NE(a.find("# @rt-look vintage 1 seed 100\n"), std::string::npos) << a;
	EXPECT_NE(a, b) << "the next frame another seed";
	EXPECT_EQ(sc::withFrameSeed("# @rt-look vintage 1 seed 7\nWorldBegin\n", 3.0), "# @rt-look vintage 1 seed 7\nWorldBegin\n") << "a seed given is kept";
	EXPECT_EQ(sc::withFrameSeed("# @rt-look bloom 0.2\nWorldBegin\n", 3.0), "# @rt-look bloom 0.2\nWorldBegin\n") << "no old film, no seed";
	// the print the denoising puts on after it (--defer-print): the frame header's look, then the run's own words
	cartoon::Film film{};
	film.duration = 2.0;
	film.headerAt = [&](double t) { return sc::withFrameSeed(scene, t); };
	const print_look::Params p = film_temporal::printOf(film, 24, 48, "vintage-tone 0.2");
	EXPECT_EQ(p.vintage, 1.0);
	EXPECT_EQ(p.seed, 100);
	EXPECT_EQ(p.vintageTone, 0.2);
	EXPECT_FALSE(film_temporal::printOf(cartoon::Film{}, 0, 1, "").active());
}

// ---- the shadow terminator offset on every material ----

TEST(FilmTerminator, EveryMaterialOfTheFramesGetsTheOffset) {
	ff::DataFilm film;
	ASSERT_EQ(loadFilm("terminator", "scene s.pbrt\nlength 2\nterminator 0.1\n", film), "");
	const std::string s = film.sceneAt(1.0);
	EXPECT_EQ(count(s, "\"float terminator\" [ 0.1 ]"), 2u) << "the plain Material and the MakeNamedMaterial (not the NamedMaterial that uses it)";
	EXPECT_NE(s.find("MakeNamedMaterial \"red\" \"float terminator\" [ 0.1 ]"), std::string::npos);
	ff::DataFilm none;
	ASSERT_EQ(loadFilm("terminator_none", "scene s.pbrt\nlength 2\n", none), "");
	EXPECT_EQ(none.sceneAt(1.0).find("terminator"), std::string::npos) << "none unless the film asks";
	ff::DataFilm bad;
	EXPECT_NE(loadFilm("terminator_bad", "scene s.pbrt\nlength 2\nterminator 3\n", bad), "");
}

// ---- a lens's blades ----

TEST(FilmBokeh, ALensOfSixBladesSamplesItsHexagonEvenlyAndAShotSaysSo) {
	// every point inside the hexagon (no further out along any edge's normal than its inner radius), its area covered evenly
	const double inner = std::cos(3.14159265358979 / 6.0);
	double r2 = 0.0;
	const int n = 20000;
	for (int i = 0; i < n; ++i) {
		double x = 0.0, y = 0.0;
		aperture_shape::polygon(6.0, 0.0, (i + 0.5) / n, std::fmod(i * 0.618033988749895, 1.0), x, y);
		for (int e = 0; e < 6; ++e) {
			const double a = e * 3.14159265358979 / 3.0;   // (its corners at 30 degrees either side of the first edge's normal)
			EXPECT_LE(x * std::cos(a) + y * std::sin(a), inner + 1e-9);
		}
		r2 += (x * x + y * y) / n;
	}
	EXPECT_NEAR(r2, 5.0 / 12.0, 0.01) << "the mean squared radius of a uniform hexagon of circumradius 1";
	double x = 0.0, y = 0.0;
	aperture_shape::sample(0, 0.0, 2.0, 0.3, 0.7, 0.4, 0.2, x, y);
	EXPECT_NEAR(x, 0.2, 1e-12) << "a round lens keeps its own disk sample, squeezed by the anamorphic ratio";
	EXPECT_NEAR(y, 0.2, 1e-12);

	ff::DataFilm film;
	ASSERT_EQ(loadFilm("bokeh", "scene s.pbrt\nlength 2\nshot 0 A\n key 0 1 4  0 0.4 0  40\n aperture 0.05 blades 6 rotation 15 ratio 1.5\n", film), "");
	const std::string h = film.sceneAt(1.0);
	EXPECT_NE(h.find("\"integer blades\" [ 6 ] \"float bladesrotation\" [ 15 ]"), std::string::npos) << h.substr(0, 400);
	EXPECT_NE(h.find("\"float apertureratio\" [ 1.5 ]"), std::string::npos);
	ff::DataFilm bad;
	EXPECT_NE(loadFilm("bokeh_bad", "scene s.pbrt\nlength 2\nshot 0 A\n key 0 1 4  0 0.4 0  40\n aperture 0.05 blades 2\n", bad), "");
}
