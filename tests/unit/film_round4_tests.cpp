// film_round4_tests.cpp - the fourth round of film features: a face's shading normals from a sphere (proxy_normal.h, `face-normals`), walks with a personality (film_motion.h
// gaits), anticipation and settle (`anticipate`), motion capture (mocap_bvh.h), characters pushing the rigid world (rigid_world.h's movers), matte paintings (film_matte.h),
// .cube grading (cube_lut.h), samples a shot from a noise target (noise_budget.h, shot_samples.h), the render farm's queue (render_farm.h) and the storyboard's drawing
// (pass_drawing.h).

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "../../launcher/noise_budget.h"
#include "../../launcher/pass_drawing.h"
#include "../../launcher/render_farm.h"
#include "../../launcher/shot_samples.h"
#include "../../src/shared/cartoon/cartoon_films.h"   // (the films themselves, which these tests reach into)
#include "../../src/shared/cartoon/film_matte.h"
#include "../../src/shared/cartoon/mocap_bvh.h"
#include "../../src/shared/cartoon/rigid_world.h"
#include "../../src/shared/cube_lut.h"
#include "../../src/shared/proxy_normal.h"

namespace ff = cartoon::film_file;

namespace {

const char *kGround = R"(# a floor
LookAt 0 1.2 4  0 0.4 0  0 1 0
Camera "perspective" "float fov" [ 40 ]
Film "rgb" "integer xresolution" [ 320 ] "integer yresolution" [ 180 ]
WorldBegin
LightSource "distant" "point3 from" [ 3 6 4 ] "point3 to" [ 0 0 0 ] "rgb L" [ 2 2 2 ]
AttributeBegin
  Material "diffuse" "rgb reflectance" [ 0.5 0.5 0.5 ]
  Shape "trianglemesh" "integer indices" [ 0 1 2  0 2 3 ] "point3 P" [ -20 0 -20   20 0 -20   20 0 20   -20 0 20 ]
AttributeEnd
)";

std::filesystem::path freshDir(const std::string &name) {
	const std::filesystem::path d = std::filesystem::temp_directory_path() / ("photonsmith_round4_test_" + name);
	std::filesystem::remove_all(d);
	std::filesystem::create_directories(d);
	return d;
}

std::string loadFilm(const std::string &name, const std::string &filmText, ff::DataFilm &film) {
	cartoon::film_kinds::install();   // (the person kind: installed when the film registry is first built, which a test run on its own may not have done)
	const std::filesystem::path d = freshDir(name);
	std::ofstream(d / "s.pbrt", std::ios::binary) << kGround;
	std::ofstream(d / (name + ".film"), std::ios::binary) << filmText;
	return film.load((d / (name + ".film")).string());
}

}  // namespace

// ---- a face's shading normals from a sphere ----

TEST(FilmFaceNormals, TheNormalTurnsToTheSphereWithinItsRadiusAndFadesOutBeyond) {
	const double c[3] = {0, 0, 0}, n[3] = {1, 0, 0};
	double out[3];
	const double close[3] = {0, 0, 0.9};
	proxy_normal::apply(close, n, c, 1.0, 1.0, out);
	EXPECT_NEAR(out[2], 1.0, 1e-12) << "inside the radius: the sphere's normal";
	proxy_normal::apply(close, n, c, 1.0, 0.5, out);
	EXPECT_NEAR(out[0], out[2], 1e-12) << "half the blend: half way";
	EXPECT_NEAR(std::sqrt(out[0] * out[0] + out[1] * out[1] + out[2] * out[2]), 1.0, 1e-12);
	const double away[3] = {0, 0, 1.4};
	proxy_normal::apply(away, n, c, 1.0, 1.0, out);
	EXPECT_EQ(out[0], 1.0) << "beyond 1.3 radii: its own normal";
	const double inside[3] = {0, 0, 0.5}, facingIn[3] = {0, 0, -1};
	proxy_normal::apply(inside, facingIn, c, 1.0, 1.0, out);
	EXPECT_NEAR(out[2], -1.0, 1e-12) << "kept on the side being shaded";

	ff::DataFilm film, plain;
	ASSERT_EQ(loadFilm("face", "scene s.pbrt\nlength 2\ncharacter ann person at 0 0 0 facing 0\nface-normals ann 1\n", film), "");
	ASSERT_EQ(loadFilm("face_plain", "scene s.pbrt\nlength 2\ncharacter ann person at 0 0 0 facing 0\n", plain), "");
	EXPECT_NE(film.sceneAt(1.0).find("normalcenter"), std::string::npos);
	EXPECT_EQ(plain.sceneAt(1.0).find("normalcenter"), std::string::npos);
	ff::DataFilm bad;
	EXPECT_NE(loadFilm("face_bad", "scene s.pbrt\nlength 2\ncharacter ann person at 0 0 0 facing 0\nface-normals nobody 1\n", bad), "");
}

// ---- walks with a personality ----

TEST(FilmGaits, EveryGaitIsNamedAndHasItsOwnStepAndPose) {
	namespace mo = cartoon::motion;
	for (const std::string &name : mo::gaitNames()) {
		mo::Gait g;
		ASSERT_TRUE(mo::gaitNamed(name, g)) << name;
		EXPECT_EQ(mo::gaitNames()[static_cast<std::size_t>(g)], name);
	}
	mo::Gait g;
	EXPECT_FALSE(mo::gaitNamed("moonwalk", g));
	EXPECT_GT(mo::gaitCrouch(mo::Gait::Sneak), 0.0) << "a sneak crouches";
	EXPECT_LT(mo::gaitStride(mo::Gait::Tiptoe), 1.0) << "a tiptoe's steps are short";
	const cartoon::Mat4 frame = cartoon::identity();
	const cartoon::Mat4 plain = mo::gaitPose(mo::Gait::Plain, frame, 2.5, 1.0, 1.7);
	for (int i = 0; i < 16; ++i) EXPECT_EQ(plain[i], i % 5 == 0 ? 1.0 : 0.0) << "a plain walk is as it was";
	const cartoon::Vec3 head{0.0, 1.7, 0.0};
	const cartoon::Vec3 sneak = cartoon::transformPoint(mo::gaitPose(mo::Gait::Sneak, frame, 2.5, 1.0, 1.7), head);
	EXPECT_GT(sneak.z, 0.2) << "a sneak leans forward";
	const cartoon::Vec3 skip = cartoon::transformPoint(mo::gaitPose(mo::Gait::Skip, frame, 2.5, 1.0, 1.7), head);
	EXPECT_GT(skip.y, 1.85) << "a skip is high in the middle of its step";
	const cartoon::Vec3 still = cartoon::transformPoint(mo::gaitPose(mo::Gait::Skip, frame, 2.5, 0.0, 1.7), head);
	EXPECT_EQ(still.y, 1.7) << "standing, no gait";

	ff::DataFilm film;
	ASSERT_EQ(loadFilm("gait", "scene s.pbrt\nlength 5\ncharacter ann person at -3 0 0 facing 90\nwalk ann 0.2 0.9 sneak 3 0\n", film), "");
	ASSERT_EQ(film.spec.characters.size(), 1u);
	ASSERT_EQ(film.spec.characters[0].walks.size(), 1u);
	EXPECT_EQ(film.spec.characters[0].walks[0].gait, mo::Gait::Sneak);
}

// ---- anticipation and settle ----

TEST(FilmAnticipation, AStartLeansBackAndASteadyWalkIsLeftAlone) {
	namespace mo = cartoon::motion;
	const auto steady = [](double t) { return cartoon::translate({0.0, 0.0, 1.2 * t}); };
	const cartoon::Mat4 a = mo::anticipation(steady, 1.0, 1.7, 1.0);
	for (int i = 0; i < 16; ++i) EXPECT_NEAR(a[i], i % 5 == 0 ? 1.0 : 0.0, 1e-12) << "no change of speed, nothing to wind up";
	const auto setsOff = [](double t) { return cartoon::translate({0.0, 0.0, t < 1.0 ? 0.0 : 1.2 * (t - 1.0)}); };
	const cartoon::Vec3 head = cartoon::transformPoint(mo::anticipation(setsOff, 0.95, 1.7, 1.0), {0.0, 1.7, 0.0});
	EXPECT_LT(head.z, -0.05) << "before it sets off it leans back";
	const auto stops = [](double t) { return cartoon::translate({0.0, 0.0, t < 1.0 ? 1.2 * t : 1.2}); };
	const cartoon::Vec3 after = cartoon::transformPoint(mo::anticipation(stops, 1.05, 1.7, 1.0), {0.0, 1.7, 1.2});
	EXPECT_GT(after.z - 1.2, 0.05) << "after it stops it overshoots forward";

	ff::DataFilm with, without;
	const std::string text = "scene s.pbrt\nlength 5\ncharacter ann person at -2 0 0 facing 90\nwalk ann 1 1.2 2 0\n";
	ASSERT_EQ(loadFilm("antic", text + "anticipate 1\n", with), "");
	ASSERT_EQ(loadFilm("antic_off", text, without), "");
	EXPECT_EQ(with.spec.anticipate.size(), 1u);
	EXPECT_NE(with.sceneAt(0.95), without.sceneAt(0.95)) << "wound up before the walk";
}

// ---- motion capture ----

TEST(FilmMocap, ABvhIsReadWithItsChannelsAndEndSitesAndTurnsItsBones) {
	namespace mc = cartoon::mocap;
	const std::string text = "HIERARCHY\nROOT Hips\n{\n  OFFSET 0 0 0\n  CHANNELS 6 Xposition Yposition Zposition Zrotation Xrotation Yrotation\n  JOINT Spine\n  {\n"
							 "    OFFSET 0 10 0\n    CHANNELS 3 Zrotation Xrotation Yrotation\n    End Site\n    {\n      OFFSET 0 5 0\n    }\n  }\n}\nMOTION\nFrames: 2\n"
							 "Frame Time: 0.05\n0 90 0 0 0 0 0 0 0\n1 90 0 0 0 0 90 0 0\n";
	mc::Bvh b;
	ASSERT_EQ(mc::parse(text, b), "");
	ASSERT_EQ(b.joints.size(), 2u);
	EXPECT_EQ(b.width, 9);
	EXPECT_EQ(b.frames, 2);
	EXPECT_NEAR(b.frameTime, 0.05, 1e-12);
	EXPECT_TRUE(b.joints[1].hasEnd);
	EXPECT_EQ(b.joints[1].end[1], 5.0);
	std::vector<mc::Q> world;
	double root[3];
	mc::frameOf(b, 1, world, root);
	EXPECT_EQ(root[0], 1.0) << "the root's place";
	const double up[3] = {0, 1, 0};
	double bone[3];
	mc::rotate(world[1], up, bone);
	EXPECT_NEAR(std::fabs(bone[0]), 1.0, 1e-9) << "the spine turned a quarter about Z lies along X";
	EXPECT_NEAR(bone[1], 0.0, 1e-9);
	mc::Bvh bad;
	EXPECT_NE(mc::parse("HIERARCHY\nROOT Hips\n{\n", bad), "");

	ff::DataFilm film;
	EXPECT_NE(loadFilm("mocap", "scene s.pbrt\nlength 3\ncharacter ann person at 0 0 0 facing 0\nmocap ann missing.bvh at 0.5\n", film), "") << "a file that is not there";
}

// ---- characters push the rigid world ----

TEST(FilmRigidMovers, AWalkingBodyPushesABallItWalksInto) {
	namespace rg = cartoon::rigid;
	rg::BodyDef ball;
	ball.block = "ball", ball.sphere = true, ball.radius = 0.15, ball.at = {0.0, 0.15, 0.0}, ball.mass = 0.4;
	rg::Kinematic body;
	body.radius = 0.3;
	for (double t = 0.0; t <= 3.0 + 1e-9; t += body.step) body.at.push_back({-2.0 + 1.5 * t, 0.3, 0.0});
	const std::vector<rg::Kinematic> movers{body};
	const rg::Run run = rg::simulate({ball}, 3.0, 0.0, &movers);
	EXPECT_EQ(run.bodies, 1u) << "only the ball's poses are kept";
	const rg::Pose end = rg::posesAt(run, 3.0)[0];
	EXPECT_GT(end.at.x, 0.4) << "pushed along the way the body walks";
	EXPECT_GT(end.at.y, 0.1) << "and not through the ground";
	const rg::Pose alone = rg::posesAt(rg::simulate({ball}, 3.0), 3.0)[0];
	EXPECT_NEAR(alone.at.x, 0.0, 1e-9) << "with no one walking into it, it stays";
}

// ---- matte paintings ----

TEST(FilmMatte, APaintingIsAGlowThatLightsNothingButItselfAndCastsNoShadow) {
	cartoon::matte::Matte m;
	m.file = "C:/paint.png", m.from = {0.0, 1.5, 6.0}, m.to = {0.0, 1.5, -10.0}, m.distance = 30.0, m.grid = 8;
	const std::string text = cartoon::matte::textOf(m, 0);
	EXPECT_NE(text.find("\"bool sampled\" false"), std::string::npos) << "not a light the renderer samples";
	EXPECT_NE(text.find("illuminates"), std::string::npos);
	EXPECT_NE(text.find("\"bool shadow\" false"), std::string::npos);
	EXPECT_NE(text.find("C:/paint.png"), std::string::npos);
	ff::DataFilm bad;
	EXPECT_NE(loadFilm("matte_bad", "scene s.pbrt\nlength 2\nmatte nothere.png from 0 1 5 to 0 1 0\n", bad), "") << "a picture that is not there";
}

// ---- grading with a .cube ----

TEST(FilmLut, AnIdentityCubeLeavesColoursAndACurveMapsThem) {
	cube_lut::Lut id;
	std::string text = "TITLE \"id\"\nLUT_3D_SIZE 2\n";
	for (int b = 0; b < 2; ++b)
		for (int g = 0; g < 2; ++g)
			for (int r = 0; r < 2; ++r) text += std::to_string(r) + " " + std::to_string(g) + " " + std::to_string(b) + "\n";
	ASSERT_EQ(cube_lut::parse(text, id), "");
	float c[3] = {0.25f, 0.5f, 0.75f};
	cube_lut::apply(id, c, 1.0);
	EXPECT_NEAR(c[0], 0.25f, 1e-6f);
	EXPECT_NEAR(c[2], 0.75f, 1e-6f);
	cube_lut::Lut invert;
	ASSERT_EQ(cube_lut::parse("LUT_1D_SIZE 2\n1 1 1\n0 0 0\n", invert), "");
	float d[3] = {0.25f, 0.5f, 0.75f};
	cube_lut::apply(invert, d, 1.0);
	EXPECT_NEAR(d[0], 0.75f, 1e-6f);
	float e[3] = {0.25f, 0.5f, 0.75f};
	cube_lut::apply(invert, e, 0.5);
	EXPECT_NEAR(e[0], 0.5f, 1e-6f) << "half mixed";
	cube_lut::Lut bad;
	EXPECT_NE(cube_lut::parse("LUT_3D_SIZE 2\n0 0 0\n", bad), "") << "too few entries";
}

// ---- samples a shot from a noise target ----

TEST(FilmNoiseTarget, NoiseIsHalfTheDifferenceOfTwoSeedsAndTheTableGivesEachShotItsFactor) {
	const std::vector<int> a{100, 100, 100, 100}, b{110, 90, 110, 90};
	EXPECT_EQ(noise_budget::noiseOf(a, a, 255), 0.0);
	EXPECT_NEAR(noise_budget::noiseOf(a, b, 255), (10.0 / 255.0) / std::sqrt(2.0) / (100.0 / 255.0), 1e-9);
	EXPECT_LT(noise_budget::noiseOf(a, {1, 2}, 255), 0.0) << "pictures of different sizes";
	shot_samples::Table t;
	t.starts = {0.0, 2.0, 5.0}, t.factors = {0.25, 2.0, 1.0};
	EXPECT_EQ(t.at(1.0), 0.25);
	EXPECT_EQ(t.at(2.0), 2.0);
	EXPECT_EQ(t.at(9.0), 1.0);
	const std::filesystem::path file = freshDir("shots") / "shot_samples.txt";
	ASSERT_TRUE(shot_samples::save(t, file));
	EXPECT_EQ(shot_samples::factorAt(file.string(), 3.0), 2.0) << "read back";
	EXPECT_EQ(shot_samples::factorAt("", 3.0), 1.0) << "no table, no change";
}

// ---- the render farm ----

TEST(RenderFarm, OneComputerTakesAFrameAndAnotherCannotUntilItsClaimGoesStale) {
	const std::filesystem::path root = freshDir("farm_claims");
	std::string why;
	ASSERT_TRUE(render_farm::join(root, "film | 24 frames", why)) << why;
	EXPECT_TRUE(render_farm::join(root, "film | 24 frames", why)) << "the same video joins";
	EXPECT_FALSE(render_farm::join(root, "film | 48 frames", why)) << "another is refused";
	render_farm::Farm a = render_farm::Farm::at(root, "frames"), b = a;
	a.host = "PCA", a.me = "PCA 1", b.host = "MAC", b.me = "MAC 2";
	EXPECT_TRUE(a.take(3));
	EXPECT_FALSE(b.take(3)) << "taken";
	EXPECT_FALSE(b.stale(3, b.now())) << "fresh";
	render_farm::Farm impatient = b;
	impatient.staleSeconds = -1;
	EXPECT_TRUE(impatient.stale(3, impatient.now())) << "not rewritten in time";
	render_farm::Farm again = a;
	again.me = "PCA 99";
	EXPECT_TRUE(again.stale(3, again.now())) << "this computer's claim from a run before";
	EXPECT_FALSE(a.stale(3, a.now())) << "its own claim, this run";
	EXPECT_FALSE(a.stale(4, a.now())) << "a frame no one has taken is free, not stale";

	int made = 0;
	const auto make = [&](const std::filesystem::path &to) {
		++made;
		std::ofstream(to) << "0 1\n";
		return true;
	};
	EXPECT_TRUE(render_farm::once(a, "table.txt", make, 0.01));
	EXPECT_TRUE(render_farm::once(b, "table.txt", make, 0.01));
	EXPECT_EQ(made, 1) << "made once for the whole farm";
}

TEST(RenderFarm, TwoComputersShareTheFramesAndEachEndsWithAllOfThem) {
	const std::filesystem::path root = freshDir("farm_share");
	std::vector<char> wanted(20, 1);
	wanted[7] = 0;   // (a frame the video does not want: a draft's)
	std::atomic<int> rendered{0};
	const auto computer = [&](const std::string &name, int &mine) {
		render_farm::Farm f = render_farm::Farm::at(root, "frames");
		f.host = name, f.me = name + " 1";
		const std::filesystem::path local = root / ("local_" + name);
		std::filesystem::create_directories(local);
		mine = render_farm::run(f, local, wanted, [&](const std::vector<char> &mask) {
			for (std::size_t i = 0; i < mask.size(); ++i)
				if (mask[i]) {
					std::this_thread::sleep_for(std::chrono::milliseconds(5));
					std::ofstream(local / ("enc_" + render_farm::Farm::number(static_cast<int>(i)) + ".png"), std::ios::binary) << "frame " << i;
					++rendered;
				}
		}, 3, 0.01);
	};
	int pc = 0, mac = 0;
	std::thread one([&] { computer("PCA", pc); }), two([&] { computer("MAC", mac); });
	one.join(), two.join();
	EXPECT_EQ(pc + mac, 19) << "every wanted frame rendered once";
	EXPECT_EQ(rendered.load(), 19);
	for (const char *name : {"PCA", "MAC"})
		for (int f = 0; f < 20; ++f)
			EXPECT_EQ(std::filesystem::exists(root / (std::string("local_") + name) / ("enc_" + render_farm::Farm::number(f) + ".png")), f != 7) << name << " frame " << f;
	std::string picture;
	std::getline(std::ifstream(root / "local_MAC" / "enc_0012.png", std::ios::binary), picture);
	EXPECT_EQ(picture, "frame 12");
}

TEST(RenderFarm, AFrameThatFailsTwiceIsGivenUp) {
	const std::filesystem::path root = freshDir("farm_fail");
	render_farm::Farm f = render_farm::Farm::at(root, "frames");
	int calls = 0;
	EXPECT_EQ(render_farm::run(f, root, std::vector<char>(2, 1), [&](const std::vector<char> &) { ++calls; }, 4, 0.01), 0);
	EXPECT_EQ(calls, render_farm::kTries);
	EXPECT_TRUE(f.givenUp(0));
	EXPECT_FALSE(f.done(0));
}

// ---- the storyboard's drawing ----

TEST(Storyboard, AnObjectIsInkedRoundItsEdgeAndAGroundSeenEdgeOnIsNot) {
	const int w = 32, h = 18;
	const std::size_t n = static_cast<std::size_t>(w) * h;
	const auto planesOf = [&](bool ground) {
		std::vector<float> p(n * 12, 0.0f);
		for (int y = 0; y < h; ++y)
			for (int x = 0; x < w; ++x) {
				const std::size_t i = static_cast<std::size_t>(y) * w + x;
				const bool in = ground || (x >= 8 && x < 24 && y >= 4 && y < 14);
				if (!in) continue;
				p[0 * n + i] = 0.8f, p[1 * n + i] = 0.3f, p[2 * n + i] = 0.2f;   // (a red albedo)
				p[(ground ? 4 : 5) * n + i] = 1.0f;                                // (facing up, or the camera)
				p[6 * n + i] = ground ? 2.0f + 1.5f * static_cast<float>(h - y) : 5.0f;   // (a ground's depth steep but straight)
				p[9 * n + i] = 1.0f, p[10 * n + i] = 1.0f;
			}
		return p;
	};
	const std::vector<float> box = pass_drawing::sketch(planesOf(false), w, h, false);
	const auto at = [&](const std::vector<float> &rgb, int x, int y) { return rgb[(static_cast<std::size_t>(y) * w + x) * 3]; };
	EXPECT_EQ(at(box, 1, 1), pass_drawing::kPaper) << "nothing there: the paper";
	EXPECT_LT(at(box, 23, 8), 0.3f) << "its edge inked";
	EXPECT_LT(at(box, 7, 8), 0.3f) << "on both sides";
	const float inside = at(box, 16, 9);
	EXPECT_TRUE(inside == 0.5f || inside == 0.68f || inside == 0.83f || inside == 0.94f) << "inside, one of the pencil's tones: " << inside;
	const std::vector<float> coloured = pass_drawing::sketch(planesOf(false), w, h, true);
	EXPECT_GT(at(coloured, 16, 9), coloured[(9u * w + 16) * 3 + 1]) << "`colour` keeps its red";
	const std::vector<float> ground = pass_drawing::sketch(planesOf(true), w, h, false);
	int inked = 0;
	for (int y = 2; y < h - 2; ++y)
		for (int x = 2; x < w - 2; ++x) inked += at(ground, x, y) < 0.45f ? 1 : 0;
	EXPECT_EQ(inked, 0) << "a flat ground has no lines across it, however steep its depth";
}
