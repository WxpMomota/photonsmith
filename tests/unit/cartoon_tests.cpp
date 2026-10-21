// cartoon_tests.cpp -- the cartoon video presets: the toolkit (src/shared/cartoon/cartoon_lib.h), the Lamp and Ball film, the preset table entry and the launcher's handling of
// `--camera-path cartoon:<key>` (launcher/launcher_args.h, launcher/cartoon_render.h).

#include <gtest/gtest.h>

#include <cmath>
#include <string>
#include <vector>

#include "pbrt_flatten_test_helpers.h"
#include "../../launcher/cartoon_render.h"
#include "../../launcher/launcher_args.h"
#include "../../src/shared/cartoon/cartoons.h"
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

// ---- the preset and the launcher -------------------------------------------------------------------------------------------------------------------------------

TEST(CartoonPreset, EveryCartoonPresetNamesAFilmAndMatchesItsLength) {
	int cartoons = 0;
	for (const video_preset::VideoPreset &p : video_preset::kAll) {
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
