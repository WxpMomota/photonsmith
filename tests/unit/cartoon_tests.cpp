// cartoon_tests.cpp -- the cartoon video presets: the toolkit (src/shared/cartoon/cartoon_lib.h), the Lamp and Ball film, the preset table entry and the launcher's handling of
// `--camera-path cartoon:<key>` (launcher/launcher_args.h, launcher/cartoon_render.h).

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <set>
#include <string>
#include <vector>

#include "pbrt_flatten_test_helpers.h"
#include "scene_registry.h"
#include "cpu_interface.h"
#include "../../launcher/cartoon_render.h"
#include "../../launcher/cartoon_still.h"
#include "../../launcher/launcher_args.h"
#include "../../src/shared/cartoon/cartoons.h"
#include "../../src/shared/cartoon/cartoon_stills.h"
#include "../../src/shared/pbrt_discover.h"
#include "../../qt_gui/scene_bucket.h"
#include "../../src/shared/video_preset.h"

namespace {

bool parseArgs(std::vector<std::string> tokens, LaunchArgs &out) {
	std::vector<std::string> storage;
	storage.push_back("ray_tracer.exe");
	for (auto &t : tokens) storage.push_back(t);
	std::vector<char *> ptrs;
	for (auto &s : storage) ptrs.push_back(const_cast<char *>(s.c_str()));
	return parse_launch_args(static_cast<int>(ptrs.size()), ptrs.data(), out);
}

}  // namespace

// ---- the toolkit -----------------------------------------------------------------------------------------------------------------------------------------

TEST(CartoonLib, ATrackHoldsBeforeAndAfterAndBlendsBetween) {
	const cartoon::Track1 t({{1.0, 10.0}, {3.0, 20.0}}, cartoon::Ease::Linear);
	EXPECT_DOUBLE_EQ(t(0.0), 10.0);
	EXPECT_DOUBLE_EQ(t(2.0), 15.0);
	EXPECT_DOUBLE_EQ(t(9.0), 20.0);
}

TEST(CartoonLib, EachKeyEasesTheMotionThatArrivesAtIt) {
	const auto t = cartoon::Track1::withEases({{0.0, 0.0, cartoon::Ease::Smooth}, {1.0, 1.0, cartoon::Ease::Back}});
	EXPECT_GT(t(0.7), 1.0) << "a Back ease overshoots its target before settling";
	EXPECT_NEAR(t(1.0), 1.0, 1e-12);
}

TEST(CartoonLib, AffineInverseUndoesATransform) {
	const cartoon::Mat4 m = cartoon::mul(cartoon::translate(1.0, 2.0, 3.0), cartoon::rotate({0.3, 1.0, 0.2}, 40.0), cartoon::scale(2.0, 0.5, 1.5));
	const cartoon::Vec3 p = {0.7, -1.2, 2.5};
	const cartoon::Vec3 back = cartoon::transformPoint(cartoon::invertAffine(m), cartoon::transformPoint(m, p));
	EXPECT_NEAR(back.x, p.x, 1e-9);
	EXPECT_NEAR(back.y, p.y, 1e-9);
	EXPECT_NEAR(back.z, p.z, 1e-9);
}

TEST(CartoonLib, ALatheOfASphereProfileHasUnitNormalsPointingOutwards) {
	const cartoon::Mesh sphere = cartoon::uvSphere(2.0, 24, 12);
	ASSERT_FALSE(sphere.verts.empty());
	for (std::size_t i = 0; i < sphere.verts.size(); ++i) {
		EXPECT_NEAR(cartoon::length(sphere.normals[i]), 1.0, 1e-9);
		if (cartoon::length(sphere.verts[i]) > 1e-6) EXPECT_GT(cartoon::dot(sphere.normals[i], cartoon::normalize(sphere.verts[i])), 0.9) << "vertex " << i;
	}
}

TEST(CartoonLib, AMeshIsWrittenAsPbrtThatTheLoaderReads) {
	std::string text = "Camera \"perspective\"\nWorldBegin\nMaterial \"diffuse\"\n";
	cartoon::box(1.0, 2.0, 3.0).appendPbrt(text, cartoon::identity(), false);
	const FlatScene s = flattenSource(text);
	EXPECT_EQ(s.triangles.size(), 12u) << "six faces of two triangles";
}

// ---- the film ---------------------------------------------------------------------------------------------------------------------------------------------

TEST(CartoonLampAndBall, EveryFrameOfTheFilmIsAScenePbrtLoadsWithoutComplaint) {
	const cartoon::Film *film = cartoon::find("lamp-and-ball");
	ASSERT_NE(film, nullptr);
	for (double t : {0.0, 1.0, 2.5, 4.5, 6.4, 7.2, 8.9, 9.1, 11.0, 13.0, 15.2, 17.0, 19.96}) {
		const FlatScene s = flattenSource(film->sceneAt(t));
		EXPECT_GT(s.triangles.size(), 5000u) << "t = " << t;
		EXPECT_FALSE(s.areaLights.empty()) << "t = " << t << ": the window and the ceiling are always lit";
		for (const auto &w : s.warnings) ADD_FAILURE() << "t = " << t << ": " << w.message;
	}
}

TEST(CartoonLampAndBall, TheLampLightsOnlyAfterItWakesUp) {
	const cartoon::Film *film = cartoon::find("lamp-and-ball");
	ASSERT_NE(film, nullptr);
	EXPECT_EQ(flattenSource(film->sceneAt(1.0)).punctualLights.size(), 0u) << "asleep: no lamp light";
	EXPECT_EQ(flattenSource(film->sceneAt(5.0)).punctualLights.size(), 1u) << "awake: its spotlight";
}

TEST(CartoonLampAndBall, TheBallStopsBesideTheLampAndTheShadeCoversIt) {
	const auto &ball = cartoon::lamp_and_ball::ball();
	EXPECT_NEAR(ball.back().pos.x, cartoon::lamp_and_ball::kRestX, 0.02) << "the books' bounce is tuned so that it does";
	EXPECT_NEAR(ball.back().pos.y, cartoon::lamp_and_ball::kBallR, 1e-9) << "resting on the desk";
	const cartoon::Vec3 rest = cartoon::lamp_and_ball::ballAt(cartoon::lamp_and_ball::kCoverT - 0.6).pos;
	EXPECT_LT(std::abs(rest.x - cartoon::lamp_and_ball::kRestX), 0.5) << "it has already come to rest when the cover begins";
	EXPECT_LT(std::abs(rest.z), 0.2) << "in the lamp's plane";
}

TEST(CartoonLampAndBall, ConsecutiveFramesMoveTheBallAndTheLampSmoothly) {
	const double dt = 1.0 / 24.0;
	for (double t = 0.0; t < cartoon::lamp_and_ball::kDuration - dt; t += dt) {
		const cartoon::Vec3 a = cartoon::lamp_and_ball::ballAt(t).pos, b = cartoon::lamp_and_ball::ballAt(t + dt).pos;
		EXPECT_LT(cartoon::length(b - a), 1.2) << "the ball jumped at t = " << t;
		EXPECT_LT(std::abs(cartoon::lamp_and_ball::lampX(t + dt) - cartoon::lamp_and_ball::lampX(t)), 0.3) << "the lamp jumped at t = " << t;
	}
}

TEST(CartoonBallParade, EveryFrameOfTheFilmIsAScenePbrtLoadsWithoutComplaint) {
	const cartoon::Film *film = cartoon::find("ball-parade");
	ASSERT_NE(film, nullptr);
	for (double t : {0.0, 0.5, 1.5, 3.0, 4.6, 6.1, 8.0, 10.0, 11.96}) {
		const FlatScene s = flattenSource(film->sceneAt(t));
		EXPECT_GT(s.triangles.size(), 5000u) << "t = " << t;
		EXPECT_FALSE(s.areaLights.empty()) << "t = " << t;
		for (const auto &w : s.warnings) ADD_FAILURE() << "t = " << t << ": " << w.message;
	}
}

TEST(CartoonBallParade, TheBallsStartAboveThePictureBounceAndEndAtRestOnTheFloor) {
	namespace bp = cartoon::ball_parade;
	EXPECT_GT(bp::ballAt(0.0).bottom, 5.5) << "off the top of the picture";
	double highest = 0.0;
	for (double tau = 1.9; tau < 7.0; tau += 1.0 / 240.0) {
		const bp::BallState b = bp::ballAt(tau);
		EXPECT_GE(b.bottom, -1e-9) << "never through the floor, tau = " << tau;
		highest = std::max(highest, b.bottom);
		EXPECT_GT(b.squash, 0.5) << "tau = " << tau;
		EXPECT_LT(b.squash, 1.3) << "tau = " << tau;
	}
	EXPECT_GT(highest, 2.0) << "the steady bounce";
	const bp::BallState end = bp::ballAt(bp::kDuration);
	EXPECT_NEAR(end.bottom, 0.0, 1e-9);
	EXPECT_NEAR(end.squash, 1.0, 1e-9);
}

TEST(CartoonBallParade, ABallDoesNotJumpBetweenFrames) {
	namespace bp = cartoon::ball_parade;
	for (int i = 0; i < bp::kBalls; ++i) {
		for (double t = 0.0; t < bp::kDuration - 1.0 / 24.0; t += 1.0 / 24.0) {
			const double a = bp::ballAt(t - i * bp::kDelay).bottom, b = bp::ballAt(t + 1.0 / 24.0 - i * bp::kDelay).bottom;
			EXPECT_LT(std::abs(b - a), 3.0) << "ball " << i << " at t = " << t;
		}
	}
}

TEST(CartoonNewtonsCradle, TheBallsHangTouchingAndTheSwingPassesFromEndToEndWhileLosingEnergy) {
	namespace nc = cartoon::newtons_cradle;
	for (int i = 0; i + 1 < nc::kBalls; ++i)
		EXPECT_NEAR(cartoon::length(nc::ballCentre(i + 1, 0.0) - nc::ballCentre(i, 0.0)), 2.0 * nc::kRadius, 1e-9) << "at rest the balls touch";
	const nc::Swing lifted = nc::swingAt(nc::kReleaseAt - 0.01);
	EXPECT_NEAR(lifted.left, -nc::kAmplitude, 0.5) << "the first ball is held at its highest point";
	EXPECT_EQ(lifted.right, 0.0);
	// every half period the swing is at the other end, and each time a little lower
	double last = nc::kAmplitude;
	for (int k = 0; k < 8; ++k) {
		const double mid = nc::kReleaseAt + nc::kQuarter + (k + 0.5) * 2.0 * nc::kQuarter;
		const nc::Swing s = nc::swingAt(mid);
		const double a = k % 2 == 0 ? s.right : -s.left;
		EXPECT_GT(a, 0.0) << "swing " << k << " is at the " << (k % 2 == 0 ? "right" : "left") << " end";
		EXPECT_LT(a, last) << "each swing keeps less of the energy";
		EXPECT_EQ(k % 2 == 0 ? s.left : s.right, 0.0) << "the other end ball hangs still";
		last = a;
	}
}

TEST(CartoonDominoRun, TheWaveRunsAlongTheWholePathAndTheLampComesOnAtTheEnd) {
	namespace dr = cartoon::domino_run;
	const int n = dr::dominoCount();
	EXPECT_GT(n, 100);
	for (int i = 1; i < n; ++i) EXPECT_GT(dr::startOf(i), dr::startOf(i - 1)) << "domino " << i << " falls after the one before it";
	EXPECT_EQ(dr::leanOf(0, dr::kStart - 0.01), 0.0) << "nothing moves before the marble arrives";
	EXPECT_NEAR(dr::leanOf(n / 2, dr::startOf(n / 2) + dr::kFall + 0.1), dr::kLean, 1e-9) << "a fallen domino rests on its neighbour";
	EXPECT_GT(dr::leanOf(n - 1, dr::impactAt()), 80.0) << "the last one lies against the lamp";
	EXPECT_LT(dr::impactAt() + 3.0, dr::kDuration) << "the lamp is lit with time left to enjoy it";
	// the dominoes are evenly spaced along the path
	for (int i = 1; i < n; ++i) EXPECT_NEAR(cartoon::length(dr::pointAtArc(dr::arcOf(i)) - dr::pointAtArc(dr::arcOf(i - 1))), dr::kSpacing, 0.01);
	// the marble starts well before the first domino and has reached it when it is struck
	EXPECT_GT(cartoon::length(dr::marbleAt(0.0) - dr::marbleAt(dr::kStart)), 3.0);
	// the lamp is dark first and lit after
	EXPECT_EQ(cartoon::domino_run::sceneAt(1.0).find("LightSource \"point\""), std::string::npos);
	EXPECT_NE(cartoon::domino_run::sceneAt(dr::impactAt() + 2.0).find("LightSource \"point\""), std::string::npos);
}

TEST(CartoonPaperBoat, TheRainEasesTheWaterCalmsAndTheBoatSailsAcrossThePond) {
	namespace pb = cartoon::paper_boat;
	EXPECT_NEAR(pb::rainAt(5.0), 1.0, 1e-12);
	EXPECT_NEAR(pb::rainAt(21.0), 0.0, 1e-12);
	// the surface: busy in the rain, nearly flat once it has stopped
	double busy = 0.0, calm = 0.0;
	for (double x = -6.0; x <= 6.0; x += 0.7) {
		for (double z = -4.0; z <= 4.0; z += 0.7) {
			busy = std::max(busy, std::abs(pb::surfaceAt(x, z, 10.0).h));
			calm = std::max(calm, std::abs(pb::surfaceAt(x, z, 23.5).h));
		}
	}
	EXPECT_GT(busy, 0.02);
	EXPECT_LT(calm, 0.03) << "only the faint swell is left";
	EXPECT_LT(pb::boatBase(0.0).x, -7.0);
	EXPECT_GT(pb::boatBase(pb::kDuration).x, 5.0);
	for (double t = 0.0; t <= pb::kDuration; t += 0.5) {
		const pb::BoatPose b = pb::boatAt(t);
		EXPECT_LT(std::abs(b.pitch), 25.0) << "t = " << t << ": rocked, not capsized";
		EXPECT_LT(std::abs(b.roll), 25.0) << "t = " << t;
		EXPECT_LT(std::abs(b.position.y), 0.2) << "t = " << t << ": it floats";
	}
	// the water's slope is the slope of its height: a finite difference agrees with the analytic one
	const double x = 1.3, z = -0.7, e = 1e-4;
	const pb::Surface s = pb::surfaceAt(x, z, 8.0);
	EXPECT_NEAR(s.dhdx, (pb::surfaceAt(x + e, z, 8.0).h - pb::surfaceAt(x - e, z, 8.0).h) / (2 * e), 2e-3);
	EXPECT_NEAR(s.dhdz, (pb::surfaceAt(x, z + e, 8.0).h - pb::surfaceAt(x, z - e, 8.0).h) / (2 * e), 2e-3);
}

TEST(CartoonFilms, EveryFilmFlattensAtSeveralTimesWithoutWarnings) {
	for (const cartoon::Film &film : cartoon::kFilms) {
		for (double f : {0.0, 0.1, 0.3, 0.5, 0.7, 0.9, 0.999}) {
			const double t = f * film.duration;
			const FlatScene s = flattenSource(film.sceneAt(t));
			EXPECT_GT(s.triangles.size(), 1000u) << film.key << " t = " << t;
			EXPECT_FALSE(s.areaLights.empty()) << film.key << " t = " << t;
			for (const auto &w : s.warnings) ADD_FAILURE() << film.key << " t = " << t << ": " << w.message;
		}
	}
}

TEST(CartoonLittleRobot, TheRobotKneelsAtTheSeedlingAndItBloomsAfterTheWatering) {
	namespace lr = cartoon::little_robot;
	const cartoon::Film *film = cartoon::find("little-robot");
	ASSERT_NE(film, nullptr);
	EXPECT_DOUBLE_EQ(film->duration, lr::kDuration);
	// the robot starts left of the seedling and ends beside it
	EXPECT_LT(lr::robotX(0.5), lr::kSeedling.x - 1.0);
	EXPECT_NEAR(lr::robotX(lr::kDuration), lr::kStandX, 0.6);
	// the flower is a good deal more geometry than the bare sprout
	const size_t sprout = flattenSource(film->sceneAt(20.0)).triangles.size();
	const size_t flower = flattenSource(film->sceneAt(30.5)).triangles.size();
	EXPECT_GT(flower, sprout);
}

TEST(CartoonFireflies, TheSwarmWakesAfterThePairHasMetAndEveryLightIsFaded) {
	namespace ff = cartoon::fireflies;
	const cartoon::Film *film = cartoon::find("fireflies");
	ASSERT_NE(film, nullptr);
	const size_t early = flattenSource(film->sceneAt(5.0)).areaLights.size();
	const size_t late = flattenSource(film->sceneAt(23.0)).areaLights.size();
	EXPECT_GT(late, early + 20) << "dozens more fireflies by the end";
	EXPECT_EQ(ff::fade(0.0), 0.0) << "the film opens from black";
	EXPECT_EQ(ff::fade(ff::kDuration), 0.0) << "and closes to black";
	// the two flashes start out of step and end in step
	auto offset = [](double t) { return std::abs(ff::glowA(t) - ff::glowB(t)); };
	double early_gap = 0.0, late_gap = 0.0;
	for (double t = 6.5; t < 8.0; t += 0.01) early_gap = std::max(early_gap, offset(t));
	for (double t = 13.0; t < 14.0; t += 0.01) late_gap = std::max(late_gap, offset(t));
	EXPECT_GT(early_gap, 0.3);
	EXPECT_LT(late_gap, 1e-6);
}

TEST(CartoonFireflies, AFireflyDoesNotJumpBetweenFrames) {
	namespace ff = cartoon::fireflies;
	const double dt = 1.0 / 24.0;
	for (double t = 0.0; t < ff::kDuration - dt; t += dt) {
		EXPECT_LT(cartoon::length(ff::fireflyA(t + dt) - ff::fireflyA(t)), 0.9) << "A jumped at t = " << t;
		if (t >= 6.0) EXPECT_LT(cartoon::length(ff::fireflyB(t + dt) - ff::fireflyB(t)), 0.9) << "B jumped at t = " << t;
	}
}

// ---- the preset and the launcher -------------------------------------------------------------------------------------------------------------------------------

TEST(CartoonPreset, EveryCartoonPresetNamesAFilmAndMatchesItsLength) {
	int cartoons = 0;
	for (const video_preset::VideoPreset &p : video_preset::all()) {
		const cartoon::Film *film = cartoon::forCameraPath(p.camera_path);
		if (!film) continue;
		++cartoons;
		EXPECT_STREQ(p.key, film->key) << "a cartoon preset's key is its film's";
		EXPECT_EQ(p.fps, film->fps);
		EXPECT_EQ(p.frames, static_cast<int>(std::lround(film->duration * film->fps))) << p.id << ": the frames cover the whole film";
		EXPECT_DOUBLE_EQ(p.speed, 1.0);
	}
	EXPECT_GE(cartoons, 1);
}

TEST(VideoPresets, EveryFilmIsAPresetAndTheCameraPresetsComeFirst) {
	const auto &all = video_preset::all();
	int cartoons = 0, cameras = 0;
	bool sawCartoon = false;
	for (std::size_t i = 0; i < all.size(); ++i) {
		const video_preset::VideoPreset &p = all[i];
		EXPECT_EQ(std::string(p.id), "V" + std::to_string(i + 1)) << "ids are V1, V2 ... in table order";
		if (p.isCartoon()) {
			++cartoons;
			sawCartoon = true;
		} else {
			++cameras;
			EXPECT_FALSE(sawCartoon) << p.id << ": the camera presets come before the cartoons";
		}
	}
	EXPECT_EQ(cartoons, cartoon::kFilmCount) << "one preset per film, nothing more";
	EXPECT_GE(cameras, 6);
	for (const cartoon::Film &f : cartoon::kFilms) {
		const video_preset::VideoPreset *p = video_preset::find(f.key);
		ASSERT_NE(p, nullptr) << f.key;
		EXPECT_EQ(std::string(p->name), std::string(f.name) + " (cartoon)");
		EXPECT_STREQ(p->description, f.description) << "the preset's text is the film's own";
		EXPECT_EQ(p->film(), &f);
	}
}

TEST(VideoPresets, IdsAndKeysAreUniqueAndEitherFindsThePreset) {
	std::set<std::string> ids, keys;
	for (const video_preset::VideoPreset &p : video_preset::all()) {
		EXPECT_TRUE(ids.insert(p.id).second) << "duplicate id " << p.id;
		EXPECT_TRUE(keys.insert(p.key).second) << "duplicate key " << p.key;
		EXPECT_EQ(video_preset::find(p.id), &p);
		EXPECT_EQ(video_preset::find(p.key), &p);
	}
	EXPECT_EQ(video_preset::find("nope"), nullptr);
}

TEST(VideoPresets, TheListTextHasOneLinePerPresetWithItsKind) {
	const std::string text = video_preset::listText();
	std::size_t lines = 0;
	for (char c : text) lines += c == '\n';
	EXPECT_EQ(lines, video_preset::all().size());
	EXPECT_NE(text.find("cornell-orbit"), std::string::npos);
	EXPECT_NE(text.find("camera"), std::string::npos);
	EXPECT_NE(text.find("lamp-and-ball"), std::string::npos);
	EXPECT_NE(text.find("cartoon"), std::string::npos);
	EXPECT_NE(text.find("480 frames at 24 fps (20.0 s)"), std::string::npos) << text;
}

TEST(CartoonLauncherArgs, ACartoonCameraPathIsAcceptedAndAnUnknownOneIsNot) {
	LaunchArgs ok;
	ASSERT_TRUE(parseArgs({"--video", "--camera-path", "cartoon:lamp-and-ball"}, ok));
	EXPECT_EQ(ok.camera_path, "cartoon:lamp-and-ball");
	EXPECT_TRUE(ok.camera_path_explicit);
	LaunchArgs bad;
	EXPECT_FALSE(parseArgs({"--video", "--camera-path", "cartoon:nope"}, bad));
}

TEST(CartoonLauncherArgs, TheVideoPresetSetsTheFilmFramesAndFps) {
	LaunchArgs args;
	ASSERT_TRUE(parseArgs({"--video-preset", "lamp-and-ball"}, args));
	EXPECT_TRUE(args.video_mode);
	EXPECT_EQ(args.camera_path, "cartoon:lamp-and-ball");
	EXPECT_EQ(args.video_frames, 480);
	EXPECT_EQ(args.video_fps, 24);
}

TEST(CartoonRender, TheFrameRunRepeatsTheUsersFlagsAndDropsTheVideoOnes) {
	LaunchArgs args;
	ASSERT_TRUE(parseArgs({"--gpu", "--denoise", "--denoise-blend", "0.1", "--video", "--frames", "480", "--fps", "24", "--camera-path", "cartoon:lamp-and-ball", "--output", "C:/o/video.ppm",
						   "--seed", "7", "--height", "270", "480", "64", "8"}, args));
	const std::vector<std::string> flags = cartoon_render::forwardedFlags(args);
	const std::vector<std::string> expected = {"--gpu", "--denoise", "--denoise-blend", "0.1", "--seed", "7"};
	EXPECT_EQ(flags, expected);
	const std::string cmd = cartoon_render::frameCommand("C:/p/ray tracer.exe", flags, 480, 270, 64, 8, "C:/o/frames/frame_0001.ppm", "C:/o/frames/cartoon_1.pbrt");
	EXPECT_NE(cmd.find("\"C:/p/ray tracer.exe\""), std::string::npos) << "paths with spaces are quoted";
	EXPECT_NE(cmd.find("--height 270"), std::string::npos);
	EXPECT_NE(cmd.find("480 64 8 \"C:/o/frames/cartoon_1.pbrt\""), std::string::npos) << "width, samples, depth and the scene come last";
}

// ---- one frame of a film as a scene ----------------------------------------------------------------------------------------------------------------------------

TEST(CartoonStill, ANameIsAFilmWithAnOptionalTime) {
	cartoon::Still s;
	std::string err;
	ASSERT_TRUE(cartoon::parseStill("cartoon:fireflies", s, err)) << err;
	EXPECT_STREQ(s.film->key, "fireflies");
	EXPECT_DOUBLE_EQ(s.t, s.film->stillTime) << "no time: the film's own still";
	ASSERT_TRUE(cartoon::parseStill("cartoon:little-robot@3.5", s, err)) << err;
	EXPECT_STREQ(s.film->key, "little-robot");
	EXPECT_DOUBLE_EQ(s.t, 3.5);
	EXPECT_TRUE(cartoon::parseStill("cartoon:ball-parade@0", s, err)) << "the first instant is a frame";
}

TEST(CartoonStill, ABadNameOrTimeIsAnErrorAndAnythingElseIsNotACartoon) {
	cartoon::Still s;
	std::string err;
	for (const char *bad : {"cartoon:nope", "cartoon:fireflies@", "cartoon:fireflies@abc", "cartoon:fireflies@5x", "cartoon:fireflies@-1", "cartoon:fireflies@26.5"}) {
		EXPECT_FALSE(cartoon::parseStill(bad, s, err)) << bad;
		EXPECT_FALSE(err.empty()) << bad << " says why";
	}
	for (const char *other : {"cornell-box", "A1", "scene.pbrt", ""}) {
		EXPECT_FALSE(cartoon::parseStill(other, s, err)) << other;
		EXPECT_TRUE(err.empty()) << other << " is simply not a cartoon";
	}
}

TEST(CartoonLauncherArgs, ACartoonFrameCanBeTheScene) {
	LaunchArgs args;
	ASSERT_TRUE(parseArgs({"--cpu", "320", "16", "4", "cartoon:fireflies@13.5"}, args));
	EXPECT_EQ(args.cartoon_still, "cartoon:fireflies@13.5");
	EXPECT_TRUE(args.scene_file.empty());
	LaunchArgs plain;
	ASSERT_TRUE(parseArgs({"320", "16", "4", "cornell-box"}, plain));
	EXPECT_TRUE(plain.cartoon_still.empty());
	LaunchArgs bad;
	EXPECT_FALSE(parseArgs({"320", "16", "4", "cartoon:nope"}, bad));
}

namespace {
// A folder of its own per process: the test shards run at the same time.
std::filesystem::path scratchDir(const char *name) {
	const std::filesystem::path dir = std::filesystem::temp_directory_path() / (std::string("photonsmith_") + name + "_" + std::to_string(std::random_device{}()));
	std::filesystem::remove_all(dir);
	return dir;
}
}  // namespace

TEST(CartoonScenes, EveryFilmIsListedFromItsHeaderAloneUnderCartoons) {
	std::vector<pbrt_discover::Discovered> found;
	pbrt_discover::listGenerated(found);
	ASSERT_EQ(found.size(), static_cast<std::size_t>(cartoon::kFilmCount));
	for (const cartoon::Film &film : cartoon::kFilms) {
		bool listed = false;
		for (const pbrt_discover::Discovered &d : found) {
			if (d.name != cartoon::stillSlugOf(film)) continue;
			listed = true;
			EXPECT_TRUE(d.ok) << d.error;
			EXPECT_EQ(d.category, "Cartoons");
			EXPECT_EQ(d.tagName, film.name);
			EXPECT_EQ(d.cartoonKey, film.key);
			EXPECT_NE(d.description.find(film.description), std::string::npos);
			EXPECT_TRUE(d.declaresCamera);
			EXPECT_EQ(d.xResolution, 1280) << "the film's own Film line, from the header";
			EXPECT_FALSE(d.nested);
			EXPECT_FALSE(d.userFile);
		}
		EXPECT_TRUE(listed) << film.key;
	}
}

TEST(CartoonScenes, TheFileIsWrittenWhenAskedForAndMatchesWhatWasListed) {
	std::vector<pbrt_discover::Discovered> found;
	pbrt_discover::listGenerated(found);
	ASSERT_FALSE(found.empty());
	const pbrt_discover::Discovered listed = found[0];
	pbrt_discover::materializeGenerated("some/other/file.pbrt");   // not a generated scene's file: nothing happens
	EXPECT_FALSE(std::filesystem::exists("some/other/file.pbrt"));
	pbrt_discover::materializeGenerated(listed.path);
	ASSERT_TRUE(std::filesystem::exists(listed.path)) << listed.path;
	const pbrt_discover::Discovered written = pbrt_discover::describeFile(listed.path);
	EXPECT_TRUE(written.ok) << written.error;
	EXPECT_EQ(written.tagName, listed.tagName);
	EXPECT_EQ(written.cartoonKey, listed.cartoonKey);
	EXPECT_EQ(written.description, listed.description);
	EXPECT_EQ(written.xResolution, listed.xResolution);
	EXPECT_EQ(written.samplesPerPixel, listed.samplesPerPixel);
	EXPECT_DOUBLE_EQ(written.camera.vfov, listed.camera.vfov);
	EXPECT_DOUBLE_EQ(written.camera.lookfrom[0], listed.camera.lookfrom[0]);
}

TEST(CartoonScenes, TheRegistryListsEveryFilmAndLoadsAStillThroughIt) {
	for (const cartoon::Film &film : cartoon::kFilms) {
		const SceneDescriptor *s = find_scene(cartoon::stillSlugOf(film));
		ASSERT_NE(s, nullptr) << film.key;
		EXPECT_STREQ(s->category, "Cartoons");
		EXPECT_EQ(s->film_key, film.key);
		EXPECT_STREQ(s->name, film.name);
		EXPECT_FALSE(s->requires_files);
	}
	const SceneDescriptor *s = find_scene(cartoon::stillSlugOf(cartoon::kFilms[1]));   // Ball Parade: the lightest
	ASSERT_NE(s, nullptr);
	const hittable_list world = s->build_world();   // writes the file, loads it
	EXPECT_FALSE(world.objects.empty()) << "the loaded still has geometry";
	const char *path = cpu_scene_pbrt_path_by_id(s->id.c_str());
	EXPECT_TRUE(std::filesystem::exists(path)) << path;
}

TEST(CartoonStills, EveryStillNamesItsFilmByKeyAndTheNamesAgree) {
	for (const cartoon::Film &film : cartoon::kFilms) {
		EXPECT_EQ(cartoon::cameraPathOf(film), std::string("cartoon:") + film.key);
		EXPECT_EQ(cartoon::stillSlugOf(film), std::string("cartoon-") + film.key);
		EXPECT_EQ(cartoon_stills::fileName(film), cartoon::stillSlugOf(film) + ".pbrt");
		EXPECT_EQ(cartoon::forCameraPath(cartoon::cameraPathOf(film).c_str()), &film) << "a camera path finds its film again";
		EXPECT_NE(cartoon_stills::text(film).find(std::string("# @rt-cartoon ") + film.key + "\n"), std::string::npos) << "the tag that links a listed still to its film";
	}
}

TEST(CartoonStills, AStillIsTheFilmsFrameAtItsStillTime) {
	const cartoon::Film &film = cartoon::kFilms[0];
	const FlatScene fromFile = flattenSource(cartoon_stills::text(film));
	const FlatScene fromFilm = flattenSource(film.sceneAt(film.stillTime));
	EXPECT_EQ(fromFile.triangles.size(), fromFilm.triangles.size());
	EXPECT_EQ(fromFile.areaLights.size(), fromFilm.areaLights.size());
	for (const auto &w : fromFile.warnings) ADD_FAILURE() << w.message;
}

TEST(CartoonStills, AFileIsWrittenAgainOnlyWhenItsTextWouldChange) {
	const std::filesystem::path dir = scratchDir("stills_fresh");
	cartoon_stills::ensure(dir.string());
	const cartoon::Film &film = cartoon::kFilms[0];
	const std::filesystem::path file = dir / cartoon_stills::fileName(film);
	ASSERT_TRUE(std::filesystem::exists(file));
	const std::string hash = cartoon_stills::hashOf(cartoon_stills::body(film));
	EXPECT_TRUE(cartoon_stills::upToDate(file, hash));
	EXPECT_FALSE(cartoon_stills::upToDate(file, "0123456789abcdef")) << "another hash means the file is stale";
	EXPECT_EQ(cartoon_stills::hashOf("a"), cartoon_stills::hashOf("a"));
	EXPECT_NE(cartoon_stills::hashOf("a"), cartoon_stills::hashOf("b"));
	// current: left alone (a marker appended to the end survives)
	{ std::ofstream(file, std::ios::binary | std::ios::app) << "# marker\n"; }
	cartoon_stills::ensure(dir.string());
	std::ifstream in(file, std::ios::binary);
	std::stringstream all;
	all << in.rdbuf();
	EXPECT_NE(all.str().find("# marker"), std::string::npos);
	in.close();
	// stale: replaced
	{
		std::ofstream out(file, std::ios::binary | std::ios::trunc);
		out << "# @rt-cartoon-hash something else\nWorldBegin\n";
	}
	cartoon_stills::ensure(dir.string());
	EXPECT_TRUE(cartoon_stills::upToDate(file, hash));
	std::filesystem::remove_all(dir);
}

TEST(CartoonStills, TheStillOfAFilmThatIsGoneIsRemovedAndOtherFilesAreKept) {
	const std::filesystem::path dir = scratchDir("stills_prune");
	std::filesystem::create_directories(dir);
	{ std::ofstream(dir / "cartoon-gone-film.pbrt") << "WorldBegin\n"; }
	{ std::ofstream(dir / "my-scene.pbrt") << "WorldBegin\n"; }
	cartoon_stills::ensure(dir.string());
	EXPECT_FALSE(std::filesystem::exists(dir / "cartoon-gone-film.pbrt"));
	EXPECT_TRUE(std::filesystem::exists(dir / "my-scene.pbrt")) << "only the generated names are touched";
	for (const cartoon::Film &film : cartoon::kFilms) EXPECT_TRUE(std::filesystem::exists(dir / cartoon_stills::fileName(film))) << film.key;
	std::filesystem::remove_all(dir);
}

TEST(CartoonRender, TheChildSeesOnlyItsFramesFolderAndTheEnvironmentComesBack) {
	const char *name = "RT_TEST_CARTOON_ENV";
#ifdef _WIN32
	_putenv_s(name, "before");
#else
	setenv(name, "before", 1);
#endif
	{
		cartoon_render::ScopedEnv inner(name, "during");
		EXPECT_STREQ(std::getenv(name), "during");
	}
	ASSERT_NE(std::getenv(name), nullptr);
	EXPECT_STREQ(std::getenv(name), "before") << "the old value is put back";
	{
		cartoon_render::ScopedEnv fresh("RT_TEST_CARTOON_ENV_UNSET", "x");
		EXPECT_STREQ(std::getenv("RT_TEST_CARTOON_ENV_UNSET"), "x");
	}
	EXPECT_EQ(std::getenv("RT_TEST_CARTOON_ENV_UNSET"), nullptr) << "a variable that was not there is not left behind";
}

// ---- a frame as a temporary scene file (launcher/cartoon_still.h) ----------------------------------------------------------------------------------------------

TEST(CartoonStillFile, AFrameIsWrittenReadAsAScenePbrtLoadsAndRemovedAgain) {
	const std::filesystem::path root = scratchDir("stillfile");
	std::filesystem::create_directories(root);
	cartoon::Still still;
	std::string err;
	ASSERT_TRUE(cartoon::parseStill("cartoon:fireflies@13", still, err));
	std::filesystem::path written;
	{
		cartoon_still::SceneFile file;
		ASSERT_TRUE(file.write(still, root));
		written = file.path;
		EXPECT_TRUE(std::filesystem::exists(written));
		EXPECT_EQ(written.filename().string(), "cartoon-fireflies-frame.pbrt");
		std::ifstream in(written, std::ios::binary);
		std::stringstream all;
		all << in.rdbuf();
		const FlatScene s = flattenSource(all.str());
		EXPECT_GT(s.triangles.size(), 1000u);
		for (const auto &w : s.warnings) ADD_FAILURE() << w.message;
		EXPECT_EQ(pbrt_discover::describeFile(written.string()).tagName, "Fireflies at 13 s");
	}
	EXPECT_FALSE(std::filesystem::exists(written)) << "removed with the object";
	EXPECT_FALSE(std::filesystem::exists(written.parent_path()));
	std::filesystem::remove_all(root);
}

TEST(CartoonStillFile, FoldersOfKilledRunsAreSweptOnceOldEnoughAndOnlyThose) {
	const std::filesystem::path root = scratchDir("stillsweep");
	std::filesystem::create_directories(root / "photonsmith_cartoon_old");
	std::filesystem::create_directories(root / "photonsmith_cartoon_new");
	std::filesystem::create_directories(root / "someone_elses_folder");
	std::filesystem::last_write_time(root / "photonsmith_cartoon_old", std::filesystem::file_time_type::clock::now() - std::chrono::hours(10));
	std::filesystem::last_write_time(root / "someone_elses_folder", std::filesystem::file_time_type::clock::now() - std::chrono::hours(10));
	cartoon_still::sweepStale(root);
	EXPECT_FALSE(std::filesystem::exists(root / "photonsmith_cartoon_old"));
	EXPECT_TRUE(std::filesystem::exists(root / "photonsmith_cartoon_new")) << "a run that may still be going keeps its folder";
	EXPECT_TRUE(std::filesystem::exists(root / "someone_elses_folder")) << "only our own folders";
	std::filesystem::remove_all(root);
}

// ---- the GUI's scene buckets (qt_gui/scene_bucket.h) -----------------------------------------------------------------------------------------------------------

TEST(SceneBucket, TheFourBucketsAreTheScenePairAndTheCartoonPair) {
	using namespace scene_bucket;
	EXPECT_EQ(of(false, false), SelfContained);
	EXPECT_EQ(of(false, true), ExternalFiles);
	EXPECT_EQ(of(true, false), Cartoons);
	EXPECT_EQ(of(true, true), CartoonsExternal);
	for (int b = 0; b < kCount; ++b) {
		EXPECT_EQ(of(isCartoons(b), requiresFiles(b)), b) << "a bucket survives being taken apart and put together";
	}
	EXPECT_FALSE(isCartoons(SelfContained) || isCartoons(ExternalFiles));
	EXPECT_TRUE(isCartoons(Cartoons) && isCartoons(CartoonsExternal));
	EXPECT_TRUE(requiresFiles(ExternalFiles) && requiresFiles(CartoonsExternal));
	EXPECT_FALSE(requiresFiles(SelfContained) || requiresFiles(Cartoons));
}
