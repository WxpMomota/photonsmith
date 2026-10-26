// cartoon_tests.cpp -- the cartoon video presets: the toolkit (src/shared/cartoon/cartoon_lib.h), the Lamp and Ball film, the preset table entry and the launcher's handling of
// `--camera-path cartoon:<key>` (launcher/launcher_args.h, launcher/cartoon_render.h).

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <cctype>
#include <cmath>
#include <cstring>
#include <tuple>
#include <filesystem>
#include <fstream>
#include <random>
#include <regex>
#include <sstream>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "pbrt_flatten_test_helpers.h"
#include "scene_registry.h"
#include "cpu_interface.h"
#include "../../launcher/cartoon_render.h"
#include "../../launcher/cartoon_worker.h"
#include "../../launcher/cartoon_still.h"
#include "../../launcher/film_transitions.h"
#include "../../launcher/film_temporal.h"
#include "../../launcher/launcher_args.h"
#include "../../launcher/flag_scopes.h"
#include "../../launcher/render_setup.h"
#include "../../launcher/background_mode.h"
#include "../../launcher/run_policy.h"
#include "../../launcher/video_frames.h"
#include "../../src/shared/cartoon/film_subjects.h"
#include "fixture_skip.h"
#include "../../src/shared/cartoon/cartoons.h"
#include "../../src/shared/cartoon/cartoon_stills.h"
#include "../../src/shared/cartoon/film_checks.h"
#include "../../src/shared/cartoon/film_baseline.h"
#include "../../src/shared/cartoon/prop_physics.h"
#include "../../src/shared/pbrt_discover.h"
#include "../../src/shared/output_crop.h"
#include "../../src/shared/lens_look.h"
#include "../../src/shared/output_format.h"
#include "../../src/shared/ppm_text.h"
#include "../../qt_gui/scene_bucket.h"
#include "../../qt_gui/film_info.h"
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
		if (std::atoi(p.id + 1) >= 1000)   // (a film made from a file: a number of its own from 1000 up, after the built-in ones)
			EXPECT_GE(i + 1, all.size() - (cartoon::films().size() - static_cast<std::size_t>(cartoon::kFilmCount))) << p.id;
		else
			EXPECT_EQ(std::string(p.id), "V" + std::to_string(i + 1)) << "the built-in ids are V1, V2 ... in table order";
		if (p.isCartoon()) {
			++cartoons;
			sawCartoon = true;
		} else {
			++cameras;
			EXPECT_FALSE(sawCartoon) << p.id << ": the camera presets come before the cartoons";
		}
	}
	EXPECT_EQ(cartoons, static_cast<int>(cartoon::films().size())) << "one preset per film, nothing more";
	EXPECT_GE(cameras, 6);
	for (const cartoon::Film &f : cartoon::kFilms) {
		const video_preset::VideoPreset *p = video_preset::find(f.key);
		ASSERT_NE(p, nullptr) << f.key;
		EXPECT_EQ(std::string(p->name), std::string(f.name) + " (cartoon)");
		EXPECT_STREQ(p->description, f.description) << "the preset's text is the film's own";
		ASSERT_NE(video_preset::filmOf(*p), nullptr);
		EXPECT_STREQ(video_preset::filmOf(*p)->key, f.key) << "(the program's film: the table's row with its cameras file, cartoons.h withFilmFiles)";
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
	const std::vector<std::string> run = cartoon_render::frameArgs(flags, 480, 270, 64, 8, "C:/o/frames/frame_0001.ppm", "C:/o/frames/cartoon_1.pbrt");
	const std::vector<std::string> expectedRun = {"--gpu", "--denoise", "--denoise-blend", "0.1", "--seed", "7", "--height", "270", "--output", "C:/o/frames/frame_0001.ppm", "480", "64", "8", "C:/o/frames/cartoon_1.pbrt"};
	EXPECT_EQ(run, expectedRun) << "the user's flags, then the size, the samples, the depth and the scene";
	// a time limit is the video's: a frame gets its own share, not the whole budget
	LaunchArgs limited;
	ASSERT_TRUE(parseArgs({"--cpu", "--time-limit", "60", "--video", "--camera-path", "cartoon:fireflies"}, limited));
	EXPECT_TRUE(cartoon_render::forwardedFlags(limited).size() == 1) << "only --cpu is repeated";
	const std::vector<std::string> withLimit = cartoon_render::frameArgs(cartoon_render::forwardedFlags(limited), 64, 36, 4, 2, "o.ppm", "s.pbrt", 2.5);
	EXPECT_NE(std::find(withLimit.begin(), withLimit.end(), "--time-limit"), withLimit.end());
	const std::vector<std::string> noLimit = cartoon_render::frameArgs({}, 64, 36, 4, 2, "o.ppm", "s.pbrt");
	EXPECT_EQ(std::find(noLimit.begin(), noLimit.end(), "--time-limit"), noLimit.end()) << "none when there is no limit";
	LaunchArgs more;
	ASSERT_TRUE(parseArgs({"--video", "--resume", "--cartoon-timeout", "30", "--camera-path", "cartoon:fireflies"}, more));
	EXPECT_TRUE(more.cartoon_resume);
	EXPECT_EQ(more.cartoon_timeout, 30);
	const std::vector<std::string> kept = cartoon_render::forwardedFlags(more);
	EXPECT_EQ(std::find(kept.begin(), kept.end(), "--resume"), kept.end()) << "a frame's own run is one frame: neither flag is passed on";
	EXPECT_EQ(std::find(kept.begin(), kept.end(), "--cartoon-timeout"), kept.end());
	EXPECT_EQ(std::find(kept.begin(), kept.end(), "30"), kept.end());
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
	ASSERT_EQ(found.size(), cartoon::films().size());
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
			EXPECT_EQ(d.xResolution, film.meta.width) << "the film's own Film line, from the header";
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
		ASSERT_NE(cartoon::forCameraPath(cartoon::cameraPathOf(film).c_str()), nullptr);
		EXPECT_STREQ(cartoon::forCameraPath(cartoon::cameraPathOf(film).c_str())->key, film.key) << "a camera path finds its film again";
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

#ifdef _WIN32
TEST(CartoonRender, AnArgumentWithSpacesOrQuotesIsQuotedTheWayTheOperatingSystemReadsIt) {
	EXPECT_EQ(win32_quote_arg("plain"), "plain");
	EXPECT_EQ(win32_quote_arg("C:/p/ray tracer.exe"), "\"C:/p/ray tracer.exe\"");
	EXPECT_EQ(win32_quote_arg("a\"b"), "\"a\\\"b\"");
	EXPECT_EQ(win32_quote_arg("C:\\dir with space\\"), "\"C:\\dir with space\\\\\"") << "a backslash before the closing quote is doubled";
	EXPECT_EQ(win32_quote_arg(""), "\"\"");
}
#endif

// a child process that exits with a code that tells what it saw in its environment, and one that outlives any timeout
#ifdef _WIN32
static std::string shellExe() { return "C:\\Windows\\System32\\cmd.exe"; }
static std::vector<std::string> shellArgs(const std::string &script) { return {"/c", script}; }
static const char *kSleepScript = "ping -n 30 127.0.0.1 >nul";
static const char *kEnvScript = "if \"%RT_TEST_VAR%\"==\"seen\" (exit 7) else (exit 8)";
static const char *kExitScript = "exit 3";
#else
static std::string shellExe() { return "/bin/sh"; }
static std::vector<std::string> shellArgs(const std::string &script) { return {"-c", script}; }
static const char *kSleepScript = "sleep 30";
static const char *kEnvScript = "if [ \"$RT_TEST_VAR\" = seen ]; then exit 7; else exit 8; fi";
static const char *kExitScript = "exit 3";
#endif

TEST(CartoonRender, TheChildSeesOnlyItsFramesFolderAndItsExitCodeComesBack) {
	bool timedOut = true;
	EXPECT_EQ(runChild(shellExe(), shellArgs(kExitScript), "", "", 0, &timedOut), 3);
	EXPECT_FALSE(timedOut);
	// the variable is set on the child's own environment (several children run at once, so it cannot be this process's) and is not left in this one
	EXPECT_EQ(runChild(shellExe(), shellArgs(kEnvScript), "RT_TEST_VAR", "seen", 0), 7);
	EXPECT_EQ(runChild(shellExe(), shellArgs(kEnvScript), "", "", 0), 8) << "and left alone when none is asked for";
	EXPECT_EQ(std::getenv("RT_TEST_VAR"), nullptr);
	EXPECT_EQ(runChild("C:/no/such/program.exe", {}, "", "", 0), -1) << "a program that cannot be started";
}

TEST(CartoonRender, AChildThatTakesTooLongIsStoppedAndSaysSo) {
	bool timedOut = false;
	const auto start = std::chrono::steady_clock::now();
	runChild(shellExe(), shellArgs(kSleepScript), "", "", 1, &timedOut);
	const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
	EXPECT_TRUE(timedOut);
	EXPECT_LT(seconds, 12.0) << "it was stopped, not waited for";
}

TEST(CartoonRender, TheTimeoutIsTheFlagThenTheEnvironmentThenFifteenMinutesOnAGpuAndNoneOnTheCpu) {
	LaunchArgs args;
	EXPECT_EQ(cartoon_render::timeoutFor(args, true), 900);
	EXPECT_EQ(cartoon_render::timeoutFor(args, false), 0);
	args.cartoon_timeout = 60;
	EXPECT_EQ(cartoon_render::timeoutFor(args, true), 60);
	EXPECT_EQ(cartoon_render::timeoutFor(args, false), 60);
	args.cartoon_timeout = 0;
	EXPECT_EQ(cartoon_render::timeoutFor(args, true), 0) << "0 asks for none";
}

TEST(CartoonRender, AFrameThatFailedIsFilledFromTheOneBeforeSoTheVideoKeepsItsLength) {
	const std::filesystem::path dir = std::filesystem::temp_directory_path() / "photonsmith_fill_test";
	std::filesystem::remove_all(dir);
	std::filesystem::create_directories(dir);
	auto write = [&](int i, const std::string &text) { std::ofstream(dir / cartoon_render::encName(i), std::ios::binary) << text; };
	write(1, "one");
	write(2, "two");
	write(5, "five");   // 0, 3, 4 and 6 are missing
	EXPECT_EQ(cartoon_render::countFrames(dir, 7), 3);
	EXPECT_EQ(cartoon_render::fillMissingFrames(dir, 7), 4);
	EXPECT_EQ(cartoon_render::countFrames(dir, 7), 7);
	auto read = [&](int i) {
		std::ifstream f(dir / cartoon_render::encName(i), std::ios::binary);
		return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	};
	EXPECT_EQ(read(0), "one") << "the first takes the next";
	EXPECT_EQ(read(3), "two");
	EXPECT_EQ(read(4), "two");
	EXPECT_EQ(read(6), "five");
	EXPECT_EQ(read(5), "five") << "the frames there were are not touched";
	std::filesystem::remove_all(dir);
	EXPECT_EQ(cartoon_render::fillMissingFrames(dir, 3), 0) << "nothing to fill from";
}

// The GUI does not compile the films: it reads the renderer's film table (cartoon::filmTable, served by scene_metadata.dll) with qt_gui/film_info.h. What it reads must be what
// the films are.
TEST(CartoonFilmTable, TheGuiReadsEveryFilmAsTheRendererHasIt) {
	const std::vector<film_info::Film> read = film_info::parse(cartoon::filmTable());
	ASSERT_EQ(read.size(), cartoon::films().size());
	for (std::size_t i = 0; i < read.size(); ++i) {
		const cartoon::Film &f = *cartoon::films()[i];
		const film_info::Film &g = read[i];
		EXPECT_EQ(g.key, f.key);
		EXPECT_EQ(g.name, f.name);
		EXPECT_DOUBLE_EQ(g.duration, f.duration) << f.key;
		EXPECT_EQ(g.fps, f.fps) << f.key;
		EXPECT_DOUBLE_EQ(g.stillTime, f.stillTime) << f.key;
		EXPECT_EQ(g.width, f.meta.width) << f.key;
		EXPECT_EQ(g.height, f.meta.height) << f.key;
		EXPECT_EQ(g.samples, f.meta.samples) << f.key;
		EXPECT_EQ(g.wavefront, f.meta.wavefront) << f.key;
		EXPECT_EQ(g.soundtrack, f.soundtrack != nullptr) << f.key;
		EXPECT_EQ(g.shotStarts, f.shotStarts()) << f.key;
		EXPECT_EQ(g.shotNames, cartoon::shotNamesOf(f)) << f.key;
		EXPECT_EQ(film_info::frameCountOf(g), cartoon::frameCountOf(f)) << f.key;
		EXPECT_EQ(film_info::cameraPathOf(g), cartoon::cameraPathOf(f)) << f.key;
	}
	EXPECT_TRUE(film_info::parse("").empty());
	EXPECT_TRUE(film_info::parse("too\tfew\tfields\n").empty()) << "a line it cannot read is left out";
}

TEST(CartoonRender, ADraftRendersOneFrameInNAndIsAVideoFlagNotAFrameFlag) {
	LaunchArgs a;
	ASSERT_TRUE(parseArgs({"--gpu", "--draft", "4"}, a));
	EXPECT_EQ(a.draft_step, 4);
	EXPECT_EQ(LaunchArgs().draft_step, 1) << "every frame unless asked";
	EXPECT_TRUE(cartoon_render::forwardedFlags(a) == std::vector<std::string>{"--gpu"}) << "the frame runs render one frame each: --draft stays with the video";
	LaunchArgs b;
	EXPECT_FALSE(parseArgs({"--draft", "0"}, b));
	EXPECT_FALSE(parseArgs({"--draft", "25"}, b));
	EXPECT_FALSE(parseArgs({"--draft", "four"}, b));
}

TEST(CartoonRender, AFilledInFrameIsListedAndKeepsTheTimeOfTheFrameItCopies) {
	const std::filesystem::path dir = std::filesystem::temp_directory_path() / "photonsmith_fill_list_test";
	std::filesystem::remove_all(dir);
	std::filesystem::create_directories(dir);
	std::ofstream(dir / cartoon_render::encName(0), std::ios::binary) << "zero";
	std::ofstream(dir / cartoon_render::encName(3), std::ios::binary) << "three";
	const auto old = std::filesystem::last_write_time(dir / cartoon_render::encName(0)) - std::chrono::hours(1);
	std::filesystem::last_write_time(dir / cartoon_render::encName(0), old);
	EXPECT_EQ(cartoon_render::fillMissingFrames(dir, 4), 2);
	EXPECT_EQ(std::filesystem::last_write_time(dir / cartoon_render::encName(1)), old) << "a copy keeps its source's time, which is how the captions know it";
	std::ifstream list(dir / cartoon_render::kFilledList);
	std::vector<int> listed;
	for (int i = 0; list >> i;) listed.push_back(i);
	list.close();
	EXPECT_EQ(listed, (std::vector<int>{1, 2}));
	EXPECT_EQ(cartoon_render::dropFilledFrames(dir), 2);
	EXPECT_EQ(cartoon_render::countFrames(dir, 4), 2) << "the copies go, the frames that were rendered stay";
	EXPECT_FALSE(std::filesystem::exists(dir / cartoon_render::kFilledList));
	std::filesystem::remove_all(dir);
}

TEST(VideoFrames, AResumedVideoKeepsItsFramesAndCaptionsRecordAndRendersTheCopiesAgain) {
	const std::filesystem::path dir = std::filesystem::temp_directory_path() / "photonsmith_resume_folder_test";
	std::filesystem::remove_all(dir);
	std::filesystem::create_directories(dir / "work0");
	for (const char *f : {"enc_0000.png", "enc_0001.png", "enc_0002.png", "clean_0001.png", "captions.done", "frame_0003.done", "frame_0003.ppm", "enc_0003.png.tmp.png", "resume.txt"})
		std::ofstream(dir / f) << "x";
	std::ofstream(dir / cartoon_render::kFilledList) << "2\n";
	EXPECT_EQ(video_frames::keepForResume(dir), 1);
	for (const char *kept : {"enc_0000.png", "enc_0001.png", "clean_0001.png", "captions.done", "resume.txt"}) EXPECT_TRUE(std::filesystem::exists(dir / kept)) << kept;
	for (const char *gone : {"enc_0002.png", "frame_0003.done", "frame_0003.ppm", "enc_0003.png.tmp.png", "work0", cartoon_render::kFilledList})
		EXPECT_FALSE(std::filesystem::exists(dir / gone)) << gone;
	std::filesystem::remove_all(dir);
}

TEST(VideoFrames, AFreshVideoEmptiesOnlyAFramesFolderThisProgramMade) {
	const std::filesystem::path dir = std::filesystem::temp_directory_path() / "photonsmith_fresh_folder_test";
	std::filesystem::remove_all(dir);
	EXPECT_TRUE(video_frames::startAfresh(dir)) << "no folder yet";
	EXPECT_TRUE(std::filesystem::exists(dir / video_frames::kMarker));
	std::ofstream(dir / "enc_0000.png") << "x";
	std::ofstream(dir / "holiday.jpg") << "someone's";
	EXPECT_TRUE(video_frames::startAfresh(dir)) << "it has the marker";
	EXPECT_FALSE(std::filesystem::exists(dir / "holiday.jpg"));
	std::filesystem::remove(dir / video_frames::kMarker);
	std::ofstream(dir / "holiday.jpg") << "someone's";
	EXPECT_FALSE(video_frames::startAfresh(dir)) << "a folder called frames that someone keeps their own files in";
	EXPECT_TRUE(std::filesystem::exists(dir / "holiday.jpg"));
	std::filesystem::remove(dir / "holiday.jpg");
	std::ofstream(dir / "enc_0000.png") << "x";
	std::filesystem::create_directories(dir / "work1");
	EXPECT_TRUE(video_frames::startAfresh(dir)) << "a frames folder from before the marker: nothing but frames";
	EXPECT_FALSE(std::filesystem::exists(dir / "enc_0000.png"));
	std::filesystem::remove_all(dir);
}

TEST(FilmCaptions, ACaptionFileGivesItsStylesLinesAndSettings) {
	std::vector<std::string> problems;
	const std::vector<cartoon::Caption> c = cartoon::caption_file::parse(
		"# words\n"
		"style big font=simsun.ttc size=0.06 colour=1,0.5,0.25 weight=0.5\n"
		"style small font=palai.ttf index=0 size=0.03\n"
		"caption 1.5 4 fade=0.5 centre=0.8 width=0.5 gap=0.2 | big: first | small: second line\n"
		"caption 3 2 | big: backwards\n"
		"caption 5 6 | nostyle: text\n"
		"something else\n",
		&problems);
	ASSERT_EQ(c.size(), 1u);
	EXPECT_DOUBLE_EQ(c[0].t0, 1.5);
	EXPECT_DOUBLE_EQ(c[0].t1, 4.0);
	EXPECT_DOUBLE_EQ(c[0].fade, 0.5);
	EXPECT_DOUBLE_EQ(c[0].centreY, 0.8);
	EXPECT_DOUBLE_EQ(c[0].width, 0.5);
	EXPECT_DOUBLE_EQ(c[0].lineGap, 0.2);
	ASSERT_EQ(c[0].lines.size(), 2u);
	EXPECT_EQ(c[0].lines[0].text, "first");
	EXPECT_EQ(c[0].lines[0].font, "simsun.ttc");
	EXPECT_DOUBLE_EQ(c[0].lines[0].size, 0.06);
	EXPECT_DOUBLE_EQ(c[0].lines[0].colour.y, 0.5);
	EXPECT_DOUBLE_EQ(c[0].lines[0].weight, 0.5);
	EXPECT_EQ(c[0].lines[1].text, "second line");
	EXPECT_EQ(c[0].lines[1].font, "palai.ttf");
	EXPECT_EQ(problems.size(), 4u) << "the backwards caption, the unknown style (and so a caption with no lines) and the line that is neither";
}

TEST(FilmCaptions, TextIsSetCentredAndWrappedAndGoesToASubtitleFile) {
	cartoon::Caption c{0.0, 2.0, std::string()};
	c.width = 0.4;
	cartoon::CaptionLine line;
	line.text = "Words wide enough to need a second row on this small frame";
	line.font = "NotoSansSC-Regular.ttf";   // (comes with the program: qt_gui/fonts)
	line.size = 0.08;
	c.lines.push_back(line);
	const film_captions::Picture pic = film_captions::textPicture(c, 400, 200);
	if (pic.w == 0) RT_SKIP_MISSING_FIXTURE("qt_gui/fonts/NotoSansSC-Regular.ttf was not found from here");
	EXPECT_LE(pic.w, 400 * 0.4 + 60) << "wrapped to the caption's width (and its halo)";
	EXPECT_GT(pic.h, 2 * 0.08 * 200) << "two rows";
	double ink = 0.0;
	for (std::size_t i = 3; i < pic.rgba.size(); i += 4) ink += pic.rgba[i];
	EXPECT_GT(ink, 100.0) << "letters were drawn";
	const std::filesystem::path srt = std::filesystem::temp_directory_path() / "photonsmith_captions_test.srt";
	cartoon::Caption second = c;
	second.t0 = 61.25;
	second.t1 = 3725.5;
	ASSERT_TRUE(film_captions::writeSrt({c, second}, srt));
	std::ifstream in(srt, std::ios::binary);
	const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	in.close();
	EXPECT_NE(text.find("1\n00:00:00,000 --> 00:00:02,000\nWords wide"), std::string::npos) << text;
	EXPECT_NE(text.find("2\n00:01:01,250 --> 01:02:05,500\n"), std::string::npos) << text;
	std::filesystem::remove(srt);
}

TEST(FilmCaptions, AFrameGetsItsWordsOnceEvenWhenTheVideoIsResumedOrTheFrameIsACopy) {
	const cartoon::Film *film = cartoon::find("tengwang");
	ASSERT_NE(film, nullptr);
	ASSERT_TRUE(film->captions);
	const cartoon::Caption first = film->captions().front();
	const int frames = cartoon::frameCountOf(*film);
	const int i = static_cast<int>(0.5 * (first.t0 + first.t1) * film->fps);
	const std::filesystem::path dir = std::filesystem::temp_directory_path() / "photonsmith_captions_once_test";
	std::filesystem::remove_all(dir);
	std::filesystem::create_directories(dir);
	const std::vector<unsigned char> grey(static_cast<std::size_t>(192) * 80 * 3, 90);
	ASSERT_TRUE(write_png((dir / cartoon_render::encName(i)).string().c_str(), 192, 80, grey.data()));
	EXPECT_EQ(film_captions::apply(*film, dir, frames), 1);
	EXPECT_EQ(film_captions::apply(*film, dir, frames), 0) << "the same frame, a second time (a resumed video)";
	// the next frame failed and is a copy of this one: it already has the words
	std::filesystem::copy_file(dir / cartoon_render::encName(i), dir / cartoon_render::encName(i + 1));
	std::filesystem::last_write_time(dir / cartoon_render::encName(i + 1), std::filesystem::last_write_time(dir / cartoon_render::encName(i)));
	EXPECT_EQ(film_captions::apply(*film, dir, frames), 0);
	std::ifstream a(dir / cartoon_render::encName(i), std::ios::binary), b(dir / cartoon_render::encName(i + 1), std::ios::binary);
	EXPECT_EQ(std::string((std::istreambuf_iterator<char>(a)), std::istreambuf_iterator<char>()), std::string((std::istreambuf_iterator<char>(b)), std::istreambuf_iterator<char>()));
	a.close();
	b.close();
	std::filesystem::remove_all(dir);
}

TEST(FilmCaptions, EditedWordsAreLaidAgainOnTheCleanPictureWithoutRenderingTheFrame) {
	const auto fileText = [](const std::filesystem::path &p) {
		std::ifstream in(p, std::ios::binary);
		return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	};
	auto words = std::make_shared<std::string>("style s font=NotoSansSC-Regular.ttf size=0.2\ncaption 2 8 fade=0.1 | s: First words\n");
	cartoon::Film film = *cartoon::find("tengwang");
	film.duration = 10.0;   // (10 frames at 1 fps: frame i is at i seconds)
	film.fps = 1;
	film.captions = [words] { return cartoon::caption_file::parse(*words); };
	if (film_captions::textPicture(film.captions().front(), 192, 80).w == 0) RT_SKIP_MISSING_FIXTURE("qt_gui/fonts/NotoSansSC-Regular.ttf was not found from here");
	const std::filesystem::path dir = std::filesystem::temp_directory_path() / "photonsmith_captions_edit_test";
	std::filesystem::remove_all(dir);
	std::filesystem::create_directories(dir);
	const std::filesystem::path frame = dir / cartoon_render::encName(5), clean = dir / "clean_0005.png";
	const std::vector<unsigned char> grey(static_cast<std::size_t>(192) * 80 * 3, 90);
	ASSERT_TRUE(write_png(frame.string().c_str(), 192, 80, grey.data()));
	const std::string rendered = fileText(frame);
	EXPECT_EQ(film_captions::apply(film, dir, 10), 1);
	const std::string first = fileText(frame);
	EXPECT_NE(first, rendered);
	EXPECT_EQ(fileText(clean), rendered) << "the picture as rendered is kept";
	EXPECT_EQ(film_captions::apply(film, dir, 10), 0) << "nothing changed";
	*words = "style s font=NotoSansSC-Regular.ttf size=0.2\ncaption 2 8 fade=0.1 | s: Other words\n";
	EXPECT_EQ(film_captions::apply(film, dir, 10), 1) << "the caption file was edited";
	EXPECT_NE(fileText(frame), first);
	EXPECT_EQ(fileText(clean), rendered);
	*words = "style s font=NotoSansSC-Regular.ttf size=0.2\ncaption 7 8 | s: Later\n";
	EXPECT_EQ(film_captions::apply(film, dir, 10), 1) << "the frame has no words now";
	EXPECT_EQ(fileText(frame), rendered);
	EXPECT_FALSE(std::filesystem::exists(clean));
	EXPECT_EQ(film_captions::apply(film, dir, 10), 0);
	*words = "style s font=NotoSansSC-Regular.ttf size=0.2\ncaption 2 8 fade=0.1 | s: First words\n";
	EXPECT_EQ(film_captions::apply(film, dir, 10), 1) << "and words again";
	EXPECT_EQ(fileText(frame), first);
	EXPECT_EQ(fileText(clean), rendered);
	std::filesystem::remove_all(dir);
}

TEST(CartoonRender, TheFramesOfAnInterruptedVideoAreResumedOnlyForTheSameFilmSizeAndFlags) {
	const cartoon::Film *film = cartoon::find("lamp-and-ball");
	ASSERT_NE(film, nullptr);
	LaunchArgs a, b;
	ASSERT_TRUE(parseArgs({"--gpu", "--denoise"}, a));
	ASSERT_TRUE(parseArgs({"--gpu"}, b));
	const std::string base = cartoon_render::resumeSignature(*film, a, 480, 24, 1280, 720, 128, 8);
	EXPECT_EQ(base, cartoon_render::resumeSignature(*film, a, 480, 24, 1280, 720, 128, 8)) << "the same video, the same signature";
	EXPECT_NE(base, cartoon_render::resumeSignature(*film, a, 480, 24, 640, 360, 128, 8)) << "another size";
	EXPECT_NE(base, cartoon_render::resumeSignature(*film, a, 480, 24, 1280, 720, 64, 8)) << "another quality";
	EXPECT_NE(base, cartoon_render::resumeSignature(*film, a, 240, 24, 1280, 720, 128, 8)) << "another length";
	EXPECT_NE(base, cartoon_render::resumeSignature(*film, b, 480, 24, 1280, 720, 128, 8)) << "other flags";
	EXPECT_NE(base, cartoon_render::resumeSignature(*cartoon::find("fireflies"), a, 480, 24, 1280, 720, 128, 8)) << "another film";
	// a program that has been rebuilt is another program (its frames are then compared one by one, not thrown away)
	const std::filesystem::path exe = std::filesystem::temp_directory_path() / "photonsmith_signature_test.exe";
	std::ofstream(exe, std::ios::binary) << "one";
	const std::string before = cartoon_render::programStamp(*film, exe.string());
	EXPECT_EQ(before, cartoon_render::programStamp(*film, exe.string()));
	std::ofstream(exe, std::ios::binary) << "another program";
	EXPECT_NE(before, cartoon_render::programStamp(*film, exe.string()));
	std::filesystem::remove(exe);
}

TEST(CartoonRender, AFrameFingerprintIgnoresTheBuildFolderButNotWhatIsInTheFiles) {
	const std::filesystem::path dir = std::filesystem::temp_directory_path() / "photonsmith_fingerprint_test";
	std::filesystem::remove_all(dir);
	std::filesystem::create_directories(dir / "build_0123456789abcdef");
	std::filesystem::create_directories(dir / "build_fedcba9876543210");
	std::ofstream(dir / "build_0123456789abcdef" / "ground.ply", std::ios::binary) << "mesh one";
	std::ofstream(dir / "build_fedcba9876543210" / "ground.ply", std::ios::binary) << "mesh one";
	const auto scene = [&](const char *stamp) {
		return "WorldBegin\nShape \"plymesh\" \"string filename\" [ \"" + (dir / stamp / "ground.ply").generic_string() + "\" ]\n";
	};
	const std::string a = frame_fingerprint::of(scene("build_0123456789abcdef")), b = frame_fingerprint::of(scene("build_fedcba9876543210"));
	EXPECT_EQ(a, b) << "another build's folder, the same mesh: the same picture";
	std::ofstream(dir / "build_fedcba9876543210" / "ground.ply", std::ios::binary) << "mesh two, longer";   // (files are known by path, size and time)
	EXPECT_NE(a, frame_fingerprint::of(scene("build_fedcba9876543210"))) << "a mesh made differently is noticed";
	EXPECT_NE(a, frame_fingerprint::of(scene("build_0123456789abcdef") + "Shape \"sphere\"\n")) << "the scene text itself";
	std::filesystem::remove_all(dir);
}

TEST(CartoonRender, AResumeKeepsUnchangedFramesComparesThemWhenTheProgramChangedAndRendersTheUnknown) {
	const std::map<int, std::string> fps = {{0, "aaaa"}, {1, "bbbb"}, {3, "dddd"}};
	const auto finished = [](int f) { return f != 2; };   // frames 0, 1, 3 and 4 have pictures; 4 has no fingerprint (an older render)
	const cartoon_render::ResumePlan same = cartoon_render::planResume(5, finished, fps, true);
	EXPECT_EQ(same.kept, 4);
	EXPECT_TRUE(same.check.empty()) << "the same program and film: nothing to compare";
	const cartoon_render::ResumePlan changed = cartoon_render::planResume(5, finished, fps, false);
	EXPECT_EQ(changed.kept, 0);
	EXPECT_EQ(changed.check.size(), 3u) << "the program or the film changed: the frames with fingerprints are compared";
	EXPECT_EQ(changed.check.at(1), "bbbb");
	EXPECT_EQ(changed.unknown, 1) << "frame 4 has no fingerprint: rendered again";
	// the worker's job carries the frames to compare and their fingerprints
	cartoon_worker::Job job;
	job.film = "lamp-and-ball";
	job.frames = 480;
	job.scenes = "s";
	job.out = "o";
	job.list = {1, 3};
	job.keep = changed.check;
	const std::filesystem::path file = std::filesystem::temp_directory_path() / "photonsmith_job_keep.txt";
	std::ofstream(file) << job.text();
	cartoon_worker::Job read;
	ASSERT_EQ(read.load(file.string()), "");
	EXPECT_EQ(read.keep, job.keep);
	std::filesystem::remove(file);
}

TEST(FlagScopes, EveryFlagOfTheVideoIsKnownToTheParserAndIsNotRepeatedByTheFrames) {
	// the flags of the video and what each takes when typed (a flag renamed in the parser but not in the table would be noticed here)
	const std::vector<std::vector<std::string>> videoFlags = {
		{"--video"}, {"--video-preset", "V1"}, {"--frames", "7"}, {"-f", "7"}, {"--fps", "7"}, {"--speed", "7"}, {"--camera-path", "orbit"}, {"-p", "orbit"}, {"--camera-keyframes", "x.ppm"},
		{"--output", "x.ppm"}, {"-o", "x.ppm"}, {"--height", "7"}, {"--time-limit", "7"}, {"--cartoon-jobs", "2"}, {"--cartoon-timeout", "3"}, {"--cartoon-workers", "2"}, {"--resume"},
		{"--cartoon-worker", "job.txt"}};
	for (const std::vector<std::string> &words : videoFlags) {
		const FlagScopeEntry *entry = findFlagScope(words[0]);
		ASSERT_NE(entry, nullptr) << words[0] << " is not in launcher/flag_scopes.h";
		EXPECT_EQ(entry->scope, FlagScope::Video) << words[0];
		LaunchArgs a;
		ASSERT_TRUE(parseArgs(words, a)) << words[0];
		EXPECT_EQ(a.consumed_tokens.empty() ? std::string() : a.consumed_tokens.front(), words[0]) << words[0] << " is not a flag of the parser";
		EXPECT_TRUE(cartoon_render::forwardedFlags(a).empty()) << words[0] << " would be repeated by every frame";
	}
}

TEST(FlagScopes, EveryFlagTheUsageTextDocumentsHasAScope) {
	// --help prints the usage text; a flag added to it (and to the parser) without a line in the table fails here, instead of being left out of every frame's run
	std::ostringstream out, err;
	std::streambuf *oldOut = std::cout.rdbuf(out.rdbuf());
	std::streambuf *oldErr = std::cerr.rdbuf(err.rdbuf());
	LaunchArgs a;
	bool help = false;
	std::vector<std::string> words = {"ray_tracer", "--help"};
	std::vector<char *> argv;
	for (std::string &w : words) argv.push_back(&w[0]);
	parse_launch_args(static_cast<int>(argv.size()), argv.data(), a, &help);
	std::cout.rdbuf(oldOut);
	std::cerr.rdbuf(oldErr);
	const std::string text = out.str() + err.str();
	ASSERT_TRUE(help) << "no usage text";
	std::set<std::string> flags;
	for (std::size_t i = 0; i + 2 < text.size(); ++i) {
		if (text[i] == '-' && text[i + 1] == '-' && (i == 0 || !(std::isalnum(static_cast<unsigned char>(text[i - 1])) || text[i - 1] == '-')) && std::islower(static_cast<unsigned char>(text[i + 2]))) {
			std::size_t j = i + 2;
			while (j < text.size() && (std::islower(static_cast<unsigned char>(text[j])) || std::isdigit(static_cast<unsigned char>(text[j])) || text[j] == '-')) ++j;
			std::string flag = text.substr(i, j - i);
			while (!flag.empty() && flag.back() == '-') flag.pop_back();
			flags.insert(flag);
		}
	}
	EXPECT_GT(flags.size(), 40u) << "found hardly any flag in the usage text";
	for (const std::string &flag : flags) EXPECT_NE(findFlagScope(flag), nullptr) << flag << " is documented in the usage text but has no scope in launcher/flag_scopes.h";
}

TEST(FlagScopes, TheFramesGetTheRenderFlagsWithTheirValuesAndNothingElse) {
	using cartoon_render::forwardedFlags;
	LaunchArgs a;
	ASSERT_TRUE(parseArgs({"--gpu", "--wavefront", "--denoise-blend", "0.5", "--crop", "0", "0", "1", "0.5", "--video", "--frames", "10", "--cartoon-workers", "3", "--seed", "4", "--resume"}, a));
	EXPECT_EQ(forwardedFlags(a), (std::vector<std::string>{"--gpu", "--wavefront", "--denoise-blend", "0.5", "--crop", "0", "0", "1", "0.5", "--seed", "4"})) << "a value is repeated with its flag";
	// a negative number is a value, not a flag (a single dash starts a flag only when the table knows it)
	EXPECT_EQ(renderFlagTokens({"--exposure", "-1.5", "-o", "x.ppm", "--gpu"}), (std::vector<std::string>{"--exposure", "-1.5", "--gpu"}));
}

TEST(FlagScopes, AFlagThatIsNotInTheTableIsNotRepeatedAndSaysSoOnce) {
	std::ostringstream err;
	std::streambuf *oldErr = std::cerr.rdbuf(err.rdbuf());
	const std::vector<std::string> out = renderFlagTokens({"--gpu", "--brand-new-flag", "5", "--denoise", "--brand-new-flag"});
	std::cerr.rdbuf(oldErr);
	EXPECT_EQ(out, (std::vector<std::string>{"--gpu", "--denoise"})) << "the new flag and its value are left out, the others kept";
	const std::string text = err.str();
	EXPECT_NE(text.find("--brand-new-flag"), std::string::npos);
	EXPECT_EQ(text.find("--brand-new-flag"), text.rfind("--brand-new-flag")) << "said once, not for every frame";
}

TEST(FlagScopes, EveryRenderFlagTheFramesRepeatIsAFlagTheParserKnows) {
	// the no-value flags of scope Render parse on their own; the ones with values are given sample values
	const std::map<std::string, std::vector<std::string>> values = {
		{"--denoise-blend", {"0.5"}}, {"--exposure", {"1.5"}}, {"--sampler", {"halton"}}, {"--adaptive-threshold", {"0.02"}}, {"--lightsampler", {"power"}}, {"--accelerator", {"bvh"}},
		{"--splitmethod", {"sah"}}, {"--maxcomponentvalue", {"10"}}, {"--crop", {"0", "0", "1", "1"}}, {"--seed", {"3"}}, {"--aperture", {"0.1"}}, {"--focus-distance", {"5"}},
		{"--tonemap", {"aces"}}, {"--gpu-device", {"0"}}, {"--sppm-iterations", {"3"}}, {"--sppm-photons", {"1000"}}, {"--bdpt-max-depth", {"4"}}, {"--mlt-bootstrap", {"100"}}, {"--mlt-mutations", {"100"}},
		{"--mlt-max-depth", {"4"}}, {"--ao-max-dist", {"2"}}, {"--ao-illum-scale", {"1"}}, {"--ao-illum-rgb", {"1", "1", "1"}}, {"--bloom", {"0.2"}}, {"--vignette", {"0.3"}},
		{"--warmth", {"0.1"}}, {"--saturation", {"1.2"}}};
	for (const FlagScopeEntry &e : kFlagScopes) {
		if (e.scope != FlagScope::Render) continue;
		std::vector<std::string> words = {e.name};
		if (const auto it = values.find(e.name); it != values.end()) words.insert(words.end(), it->second.begin(), it->second.end());
		LaunchArgs a;
		std::ostringstream err;
		std::streambuf *oldErr = std::cerr.rdbuf(err.rdbuf());
		const bool ok = parseArgs(words, a);
		std::cerr.rdbuf(oldErr);
		EXPECT_TRUE(ok) << e.name << " does not parse: " << err.str();
		EXPECT_EQ(cartoon_render::forwardedFlags(a), words) << e.name;
	}
}

// ---- long-lived workers -----------------------------------------------------------------------------------------------------------------------------------------------

namespace {

// A pretend worker: it finishes one frame of its list each time the pool looks (`work` is called with the frame in hand and says whether it finishes, hangs or ends the worker).
struct FakeWorker : cartoon_render::WorkerHandle {
	std::vector<int> frames;
	std::size_t next = 0;
	bool alive = true, killed = false;
	std::shared_ptr<bool> killedOut;   // set when killed, for a test that outlives the worker (runWorkers destroys a handle after killing it)
	unsigned long long bytes = 0;      // what it holds, for the pool's memory limit
	unsigned long long memoryBytes() override { return bytes; }
	bool running() override { return alive; }
	void kill() override {
		alive = false;
		killed = true;
		if (killedOut) *killedOut = true;
	}
};

}  // namespace

TEST(CartoonWorkers, TheJobFileRoundTripsAndABadOneIsRefusedWithAReason) {
	cartoon_worker::Job job;
	job.film = "fireflies";
	job.frames = 624;
	job.scenes = "C:/out dir/frames/work0";
	job.out = "C:/out dir/frames";
	job.list = {0, 2, 4, 600};
	const std::filesystem::path file = std::filesystem::temp_directory_path() / "photonsmith_job_test.txt";
	std::ofstream(file) << job.text();
	cartoon_worker::Job back;
	EXPECT_EQ(back.load(file.string()), "");
	EXPECT_EQ(back.film, "fireflies");
	EXPECT_EQ(back.frames, 624);
	EXPECT_EQ(back.scenes, "C:/out dir/frames/work0") << "a folder with a space in its name survives";
	EXPECT_EQ(back.list, (std::vector<int>{0, 2, 4, 600}));
	EXPECT_NE(back.filmPtr, nullptr);
	EXPECT_DOUBLE_EQ(back.timeOf(312), back.filmPtr->duration * 0.5);
	EXPECT_NE(back.picturePath(7).find("frame_0007.ppm"), std::string::npos);
	EXPECT_NE(back.markerPath(7).find("frame_0007.done"), std::string::npos);
	std::ofstream(file) << "film nonesuch\nframes 10\nscenes a\nout b\nlist 1\n";
	EXPECT_NE(cartoon_worker::Job().load(file.string()).find("nonesuch"), std::string::npos);
	std::ofstream(file) << "film fireflies\nframes 10\nscenes a\nout b\nlist 1 12\n";
	EXPECT_NE(cartoon_worker::Job().load(file.string()).find("outside the video"), std::string::npos);
	std::ofstream(file) << "film fireflies\nframes 10\n";
	EXPECT_NE(cartoon_worker::Job().load(file.string()).find("incomplete"), std::string::npos);
	EXPECT_NE(cartoon_worker::Job().load("C:/no/such/job.txt").find("cannot be read"), std::string::npos);
	std::filesystem::remove(file);
}

TEST(CartoonWorkers, AWorkerWritesTheSceneOfEachFrameAndAMarkerWithItsResult) {
	const std::filesystem::path dir = std::filesystem::temp_directory_path() / "photonsmith_worker_test";
	std::filesystem::remove_all(dir);
	cartoon_worker::Job job;
	job.film = "lamp-and-ball";
	job.filmPtr = cartoon::find("lamp-and-ball");
	ASSERT_NE(job.filmPtr, nullptr);
	job.frames = 480;
	job.scenes = (dir / "scenes").string();
	job.out = (dir / "out").string();
	job.list = {10, 300};
	std::filesystem::create_directories(job.out);
	LaunchArgs args;
	RenderSetup setup;
	std::vector<std::string> sceneAtRender;
	std::vector<double> eyes;
	const int rc = cartoon_worker::run(job, args, setup, [&](const LaunchArgs &a, const RenderSetup &s) {
		std::ifstream in(job.scenePath());
		sceneAtRender.push_back(std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()));
		eyes.push_back(a.cam_x);
		EXPECT_TRUE(a.cam_explicit) << "the camera is this frame's own, not what the scene list remembers from the first";
		EXPECT_NE(s.out_path.find(sceneAtRender.size() == 1 ? "frame_0010.ppm" : "frame_0300.ppm"), std::string::npos);
		return sceneAtRender.size() == 1 ? 0 : 7;   // the second frame fails
	});
	EXPECT_EQ(rc, 0) << "a worker goes on after a frame that failed";
	ASSERT_EQ(sceneAtRender.size(), 2u);
	EXPECT_EQ(sceneAtRender[0], job.filmPtr->sceneAt(job.timeOf(10))) << "each frame's own scene was in the file when it was rendered";
	EXPECT_EQ(sceneAtRender[1], job.filmPtr->sceneAt(job.timeOf(300)));
	EXPECT_NE(sceneAtRender[0], sceneAtRender[1]);
	auto marker = [&](int f) {
		std::ifstream in(job.markerPath(f));
		int v = -99;
		in >> v;
		return v;
	};
	EXPECT_EQ(marker(10), 0);
	EXPECT_EQ(marker(300), 7);
	std::filesystem::remove_all(dir);
}

TEST(CartoonWorkers, TheFramesAreSharedOutHandedOnInOrderAndAllAccountedFor) {
	std::vector<std::vector<int>> given;
	cartoon_render::WorkerHooks hooks;
	std::set<int> markers;   // frames whose marker is in
	std::vector<FakeWorker *> made;
	hooks.start = [&](int worker, const std::vector<int> &frames) -> std::unique_ptr<cartoon_render::WorkerHandle> {
		if (static_cast<int>(given.size()) <= worker) given.resize(static_cast<std::size_t>(worker) + 1);
		given[static_cast<std::size_t>(worker)] = frames;
		auto w = std::make_unique<FakeWorker>();
		w->frames = frames;
		made.push_back(w.get());
		return w;
	};
	int calls = 0;
	hooks.poll = [&](int frame, int &rc) {
		// the workers finish one frame each time they are looked at, the odd ones in a different order from the pool's
		++calls;
		for (FakeWorker *w : made)
			if (w->alive && w->next < w->frames.size()) {
				markers.insert(w->frames[w->next++]);
				if (w->next == w->frames.size()) w->alive = false;
				break;
			}
		if (markers.count(frame) == 0) return false;
		markers.erase(frame);
		rc = frame == 5 ? 3 : 0;
		return true;
	};
	std::vector<int> completedOrder, handed;
	const int ok = cartoon_render::runWorkers({9, 1, 2, 3, 4, 5, 6, 7, 8, 0}, 3, 0, hooks, [&](int f, int, double) { completedOrder.push_back(f); }, [&](int f, int) { handed.push_back(f); }, 1);
	EXPECT_EQ(ok, 9) << "frame 5 failed";
	EXPECT_EQ(completedOrder.size(), 10u);
	EXPECT_EQ(handed, (std::vector<int>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9})) << "in frame order, whatever order they finished in";
	ASSERT_EQ(given.size(), 3u);
	EXPECT_EQ(given[0], (std::vector<int>{0, 3, 6, 9})) << "every third frame to each worker";
	EXPECT_EQ(given[1], (std::vector<int>{1, 4, 7}));
	EXPECT_EQ(given[2], (std::vector<int>{2, 5, 8}));
}

TEST(CartoonWorkers, AWorkerThatHoldsTooMuchMemoryIsReplacedBetweenFramesAndEveryFrameStillComesOut) {
	int starts = 0;
	std::vector<std::vector<int>> lists;
	cartoon_render::WorkerHooks hooks;
	std::set<int> markers;
	std::vector<FakeWorker *> live;
	hooks.start = [&](int, const std::vector<int> &frames) -> std::unique_ptr<cartoon_render::WorkerHandle> {
		++starts;
		lists.push_back(frames);
		auto w = std::make_unique<FakeWorker>();
		w->frames = frames;
		live.clear();
		live.push_back(w.get());
		return w;
	};
	int looks = 0;
	hooks.poll = [&](int frame, int &rc) {
		// the worker finishes one frame each time the pool looks (the pool asks twice a look: a real worker's frames come seconds apart, not all within one look) and holds 500 bytes more for each
		if (++looks % 2 == 0) return false;
		for (FakeWorker *w : live)
			if (w->alive && !w->killed && w->next < w->frames.size()) {
				markers.insert(w->frames[w->next++]);
				w->bytes += 500;
				break;
			}
		if (markers.count(frame) == 0) return false;
		markers.erase(frame);
		rc = 0;
		return true;
	};
	std::vector<int> handed;
	// a limit of 800 bytes: a worker is replaced after its second frame
	const int ok = cartoon_render::runWorkers({0, 1, 2, 3, 4, 5, 6, 7}, 1, 0, hooks, [](int, int, double) {}, [&](int f, int) { handed.push_back(f); }, 1, 800);
	EXPECT_EQ(ok, 8) << "no frame was lost or given up by the replacing";
	EXPECT_EQ(handed, (std::vector<int>{0, 1, 2, 3, 4, 5, 6, 7}));
	EXPECT_GE(starts, 3) << "replaced more than once";
	ASSERT_GE(lists.size(), 2u);
	EXPECT_EQ(lists[0], (std::vector<int>{0, 1, 2, 3, 4, 5, 6, 7}));
	EXPECT_EQ(lists[1].front(), 2) << "the next worker takes the rest, from the frame that was in hand";

	// without a limit nothing is replaced
	starts = 0;
	markers.clear();
	live.clear();
	handed.clear();
	cartoon_render::runWorkers({0, 1, 2, 3}, 1, 0, hooks, [](int, int, double) {}, [&](int f, int) { handed.push_back(f); }, 1, 0);
	EXPECT_EQ(starts, 1);
	EXPECT_EQ(handed.size(), 4u);
}

TEST(CartoonWorkers, TheMemoryLimitIsTheFlagThenTheEnvironmentThenEightGigabytes) {
	const char *saved = std::getenv("RAY_TRACER_WORKER_MAX_MB");
	const std::string before = saved ? saved : "";
	const auto put = [](const std::string &value, bool present) {
#ifdef _WIN32
		_putenv_s("RAY_TRACER_WORKER_MAX_MB", present ? value.c_str() : "");
#else
		if (present) setenv("RAY_TRACER_WORKER_MAX_MB", value.c_str(), 1); else unsetenv("RAY_TRACER_WORKER_MAX_MB");
#endif
	};
	LaunchArgs plain;
	put("", false);
	EXPECT_EQ(cartoon_render::workerMemoryLimitBytes(plain), 0ull) << "no limit unless one is asked for";
	put("2048", true);
	EXPECT_EQ(cartoon_render::workerMemoryLimitBytes(plain), 2048ull * 1024 * 1024);
	put("0", true);
	EXPECT_EQ(cartoon_render::workerMemoryLimitBytes(plain), 0ull) << "0 = never";
	LaunchArgs flagged;
	ASSERT_TRUE(parseArgs({"--worker-max-mb", "4096", "--video"}, flagged));
	EXPECT_EQ(flagged.worker_max_mb, 4096);
	EXPECT_EQ(cartoon_render::workerMemoryLimitBytes(flagged), 4096ull * 1024 * 1024) << "the flag before the environment";
	EXPECT_TRUE(cartoon_render::forwardedFlags(flagged).empty()) << "the video's own flag: the frames' runs are not given it";
	put(before, saved != nullptr);
}

TEST(CartoonWorkers, AWorkerThatEndsWithFramesLeftIsReplacedAndAFrameThatKillsTwoIsGivenUp) {
	int starts = 0;
	std::vector<std::vector<int>> lists;
	cartoon_render::WorkerHooks hooks;
	std::set<int> markers;
	std::vector<FakeWorker *> live;
	hooks.start = [&](int, const std::vector<int> &frames) -> std::unique_ptr<cartoon_render::WorkerHandle> {
		++starts;
		lists.push_back(frames);
		auto w = std::make_unique<FakeWorker>();
		w->frames = frames;
		live.push_back(w.get());
		return w;
	};
	hooks.poll = [&](int frame, int &rc) {
		// the worker on frame 1 dies every time it reaches it; the others work
		for (FakeWorker *w : live)
			if (w->alive && w->next < w->frames.size()) {
				if (w->frames[w->next] == 1) {
					w->alive = false;
					break;
				}
				markers.insert(w->frames[w->next++]);
				break;
			}
		if (markers.count(frame) == 0) return false;
		markers.erase(frame);
		rc = 0;
		return true;
	};
	std::vector<std::pair<int, int>> results;
	const int ok = cartoon_render::runWorkers({0, 1, 2, 3}, 1, 0, hooks, [&](int f, int rc, double) { results.push_back({f, rc}); }, [](int, int) {}, 1);
	EXPECT_EQ(ok, 3) << "frames 0, 2 and 3";
	ASSERT_EQ(results.size(), 4u);
	EXPECT_EQ(results[1], (std::pair<int, int>{1, -1})) << "frame 1 took its worker down twice and failed";
	EXPECT_EQ(starts, 3) << "the first worker, a second that started at frame 1 and died again, and a third that began after it";
	EXPECT_EQ(lists[1], (std::vector<int>{1, 2, 3})) << "the replacement starts with the frame that was in hand";
	EXPECT_EQ(lists[2], (std::vector<int>{2, 3}));
}

TEST(CartoonWorkers, AWorkerThatStopsMakingProgressIsKilled) {
	cartoon_render::WorkerHooks hooks;
	std::vector<std::shared_ptr<bool>> killed;   // (not pointers to the workers: runWorkers destroys a handle once it has killed it)
	hooks.start = [&](int, const std::vector<int> &frames) -> std::unique_ptr<cartoon_render::WorkerHandle> {
		auto w = std::make_unique<FakeWorker>();
		w->frames = frames;
		w->killedOut = std::make_shared<bool>(false);
		killed.push_back(w->killedOut);
		return w;
	};
	hooks.poll = [&](int, int &) { return false; };   // nothing ever finishes
	std::vector<int> failed;
	const auto start = std::chrono::steady_clock::now();
	const int ok = cartoon_render::runWorkers({0}, 1, 1, hooks, [&](int f, int rc, double) { if (rc != 0) failed.push_back(f); }, [](int, int) {}, 5);
	const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
	EXPECT_EQ(ok, 0);
	EXPECT_EQ(failed, std::vector<int>{0});
	ASSERT_EQ(killed.size(), 2u) << "tried once more";
	EXPECT_TRUE(*killed[0]);
	EXPECT_TRUE(*killed[1]);
	EXPECT_GT(seconds, 1.9) << "after the stall time, twice";
	EXPECT_LT(seconds, 8.0);
}

TEST(CartoonWorkers, ARealProcessCanBeAskedWhetherItRunsAndStopped) {
#ifdef _WIN32
	const std::string exe = "C:\\Windows\\System32\\cmd.exe";
	const std::vector<std::string> sleep = {"/c", "ping -n 30 127.0.0.1 >nul"}, quick = {"/c", "exit 4"};
#else
	const std::string exe = "/bin/sh";
	const std::vector<std::string> sleep = {"-c", "sleep 30"}, quick = {"-c", "exit 4"};
#endif
	Child slow;
	ASSERT_TRUE(slow.start(exe, sleep, "", ""));
	EXPECT_TRUE(slow.running());
	slow.kill();
	EXPECT_FALSE(slow.running());
	Child fast;
	ASSERT_TRUE(fast.start(exe, quick, "", ""));
	EXPECT_TRUE(fast.wait(10));
	EXPECT_EQ(fast.exitCode(), 4);
	Child never;
	EXPECT_FALSE(never.running()) << "one that was not started does not run";
	EXPECT_FALSE(never.start("C:/no/such/program.exe", {}, "", ""));
	EXPECT_EQ(cartoon_render::workersFor(LaunchArgs{}, true, false), 2);
	EXPECT_EQ(cartoon_render::workersFor(LaunchArgs{}, false, false), 0) << "none on the CPU";
	EXPECT_EQ(cartoon_render::workersFor(LaunchArgs{}, true, true), 0) << "nor on Metal";
	LaunchArgs asked;
	asked.cartoon_workers = 3;
	EXPECT_EQ(cartoon_render::workersFor(asked, false, true), 3) << "asked for, so asked for";
	asked.cartoon_workers = 0;
	EXPECT_EQ(cartoon_render::workersFor(asked, true, false), 0);
}

TEST(CartoonRender, AFilmThatNamesTheWavefrontRendererGetsItOnTheGpuUnlessTheUserChoseAndTheOthersDoNot) {
	const cartoon::Film *pip = cartoon::find("pip-and-the-moon"), *candle = cartoon::find("candle");
	ASSERT_NE(pip, nullptr);
	ASSERT_NE(candle, nullptr);
	EXPECT_TRUE(pip->meta.wavefront) << "its frames matched the default renderer's (docs/VIDEO_GENERATION.md)";
	EXPECT_FALSE(candle->meta.wavefront) << "the wavefront renderer draws the candle's wax darker: it keeps the default one";
	LaunchArgs plain;
	auto has = [](const std::vector<std::string> &flags) { return std::find(flags.begin(), flags.end(), "--wavefront") != flags.end(); };
	EXPECT_TRUE(has(cartoon_render::frameFlags(plain, *pip, true)));
	EXPECT_FALSE(has(cartoon_render::frameFlags(plain, *pip, false))) << "not on the CPU, which has no such renderer";
	EXPECT_FALSE(has(cartoon_render::frameFlags(plain, *candle, true)));
	LaunchArgs chosen;
	ASSERT_TRUE(parseArgs({"--wavefront"}, chosen));
	const std::vector<std::string> once = cartoon_render::frameFlags(chosen, *pip, true);
	EXPECT_EQ(std::count(once.begin(), once.end(), "--wavefront"), 1) << "asked for by the user and by the film: once";
	EXPECT_TRUE(has(cartoon_render::frameFlags(chosen, *candle, true))) << "the user's own choice is kept for any film";
}

TEST(CartoonRender, TheNumberOfFramesAtOnceIsTheFlagThenTheEnvironmentThenTwoOnAGpuAndOneOnTheCpu) {
	LaunchArgs args;
	EXPECT_EQ(cartoon_render::jobsFor(args, true), 2);
	EXPECT_EQ(cartoon_render::jobsFor(args, false), 1);
	args.cartoon_jobs = 3;
	EXPECT_EQ(cartoon_render::jobsFor(args, true), 3);
	EXPECT_EQ(cartoon_render::jobsFor(args, false), 3) << "asked for, so asked for";
	args.cartoon_jobs = 99;
	EXPECT_EQ(cartoon_render::jobsFor(args, true), 8) << "within reason";
	LaunchArgs parsed;
	ASSERT_TRUE(parseArgs({"--video", "--cartoon-jobs", "4", "--camera-path", "cartoon:fireflies"}, parsed));
	EXPECT_EQ(parsed.cartoon_jobs, 4);
	const std::vector<std::string> flags = cartoon_render::forwardedFlags(parsed);
	EXPECT_EQ(std::find(flags.begin(), flags.end(), "--cartoon-jobs"), flags.end()) << "a frame's own run is one frame: the flag is not passed on";
}

TEST(CartoonRender, FramesRenderedAtOnceAreHandedOnInFrameOrderAndNeverMoreThanTheLimitAtOnce) {
	for (int jobs : {1, 2, 3, 5}) {
		const int count = 23;
		std::atomic<int> running{0}, peak{0};
		std::vector<int> rendered(count, 0), workerOf(count, -1);
		std::vector<int> completedOrder, handedOrder;
		const int ok = cartoon_render::runInParallel(
			count, jobs,
			[&](int frame, int worker) {
				const int now = ++running;
				int seen = peak.load();
				while (now > seen && !peak.compare_exchange_weak(seen, now)) {}
				// a frame takes a different time, so that they finish out of order
				std::this_thread::sleep_for(std::chrono::milliseconds(frame % 3 == 0 ? 12 : 2));
				++rendered[static_cast<std::size_t>(frame)];
				workerOf[static_cast<std::size_t>(frame)] = worker;
				--running;
				return frame == 7 ? 1 : 0;   // one frame fails
			},
			[&](int frame, int, double) { completedOrder.push_back(frame); }, [&](int frame, int) { handedOrder.push_back(frame); });
		EXPECT_EQ(ok, count - 1) << "jobs " << jobs << ": the failed frame is not counted";
		EXPECT_LE(peak.load(), jobs) << "never more than the limit at once";
		for (int f = 0; f < count; ++f) {
			EXPECT_EQ(rendered[static_cast<std::size_t>(f)], 1) << "frame " << f << " is rendered once";
			EXPECT_GE(workerOf[static_cast<std::size_t>(f)], 0);
			EXPECT_LT(workerOf[static_cast<std::size_t>(f)], jobs) << "a worker number is below the limit, so that it names a folder";
		}
		EXPECT_EQ(completedOrder.size(), static_cast<std::size_t>(count));
		ASSERT_EQ(handedOrder.size(), static_cast<std::size_t>(count)) << "every frame is handed on, the failed one with its result";
		for (int f = 0; f < count; ++f) EXPECT_EQ(handedOrder[static_cast<std::size_t>(f)], f) << "the video's pictures are in frame order whatever order the frames finished in";
	}
	// no frames at all is fine
	EXPECT_EQ(cartoon_render::runInParallel(0, 2, [](int, int) { return 0; }, [](int, int, double) {}, [](int, int) {}), 0);
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

TEST(CartoonRender, AFrameRunWritesThePpmOnlyBecauseTheVideoMakesTheNumberedPng) {
	LaunchArgs plain;
	const cartoon::Film *pip = cartoon::find("pip-and-the-moon");
	ASSERT_NE(pip, nullptr);
	const std::vector<std::string> flags = cartoon_render::frameFlags(plain, *pip, true);
	EXPECT_NE(std::find(flags.begin(), flags.end(), "--ppm-only"), flags.end());
	LaunchArgs parsed;
	ASSERT_TRUE(parseArgs({"--ppm-only"}, parsed));
	EXPECT_TRUE(parsed.ppm_only);
	LaunchArgs normal;
	ASSERT_TRUE(parseArgs({"--cpu"}, normal));
	EXPECT_FALSE(normal.ppm_only) << "a picture someone renders still gets its PNG";
}

TEST(PpmText, TheFastTextIsWhatIostreamWritesAndReadsBack) {
	const std::size_t pixels = 100003;   // (not a multiple of the thread count)
	std::vector<unsigned char> rgb(pixels * 3);
	unsigned seed = 12345;
	for (unsigned char &c : rgb) {
		seed = seed * 1664525u + 1013904223u;
		c = static_cast<unsigned char>(seed >> 24);
	}
	rgb[0] = 0;
	rgb[1] = 9;
	rgb[2] = 10;
	rgb[3] = 99;
	rgb[4] = 100;
	rgb[5] = 255;
	std::ostringstream slow;
	for (std::size_t i = 0; i < pixels; ++i) slow << int(rgb[i * 3]) << " " << int(rgb[i * 3 + 1]) << " " << int(rgb[i * 3 + 2]) << "\n";
	std::string fast;
	ppm_text::append_p3_pixels(fast, rgb.data(), pixels);
	EXPECT_EQ(fast, slow.str());

	std::vector<unsigned char> back(rgb.size());
	std::size_t failedAt = 0;
	ASSERT_TRUE(ppm_text::parse_p3_samples(fast.data(), fast.data() + fast.size(), back.data(), back.size(), failedAt));
	EXPECT_EQ(back, rgb);
	std::string cut = fast.substr(0, fast.size() / 2);
	EXPECT_FALSE(ppm_text::parse_p3_samples(cut.data(), cut.data() + cut.size(), back.data(), back.size(), failedAt)) << "a picture cut short is an error";
	EXPECT_GT(failedAt, 0u);
	const std::string junk = "1 2 x";
	EXPECT_FALSE(ppm_text::parse_p3_samples(junk.data(), junk.data() + junk.size(), back.data(), 3, failedAt));
	EXPECT_EQ(failedAt, 2u);
}

TEST(PpmText, TheParallelLoopVisitsEveryIndexOnce) {
	for (std::size_t n : {std::size_t(0), std::size_t(1), std::size_t(16383), std::size_t(16384), std::size_t(1000003)}) {
		std::vector<int> seen(n, 0);
		ppm_text::parallel_ranges(n, [&](std::size_t begin, std::size_t end) {
			for (std::size_t i = begin; i < end; ++i) ++seen[i];
		});
		EXPECT_EQ(std::count(seen.begin(), seen.end(), 1), static_cast<std::ptrdiff_t>(n)) << n;
	}
}

TEST(CartoonRender, BackgroundModeUsesOneWorkerAndOneFrameAtATimeAndIsPassedOnToTheFrameRuns) {
	LaunchArgs normal;
	EXPECT_FALSE(normal.background);
	EXPECT_EQ(cartoon_render::workersFor(normal, true, false), 2);
	EXPECT_EQ(cartoon_render::jobsFor(normal, true), 2);

	LaunchArgs quiet;
	ASSERT_TRUE(parseArgs({"--background", "--video", "--camera-path", "cartoon:fireflies"}, quiet));
	EXPECT_TRUE(quiet.background);
	EXPECT_EQ(cartoon_render::workersFor(quiet, true, false), 1) << "one worker keeps a GPU nearly as busy and leaves the rest to the user";
	EXPECT_EQ(cartoon_render::jobsFor(quiet, true), 1);
	EXPECT_EQ(cartoon_render::workersFor(quiet, false, false), 0) << "the CPU renderer has no workers either way";

	quiet.cartoon_workers = 3;
	EXPECT_EQ(cartoon_render::workersFor(quiet, true, false), 3) << "a number the user asked for is kept";

	const cartoon::Film *pip = cartoon::find("pip-and-the-moon");
	ASSERT_NE(pip, nullptr);
	const std::vector<std::string> flags = cartoon_render::frameFlags(quiet, *pip, true);
	EXPECT_NE(std::find(flags.begin(), flags.end(), "--background"), flags.end()) << "the workers and frame runs lower their own priority too";
}

TEST(RunPolicy, ItIsDecidedOnceFromTheArgumentsAndTheEnvironmentAndEveryNumberFollowsFromIt) {
	LaunchArgs plain;
	const RunPolicy normal = RunPolicy::from(plain);
	EXPECT_FALSE(normal.background);
	EXPECT_EQ(normal.dutyPercent, 100);
	EXPECT_EQ(normal.defaultWorkers(true, false), 2);
	EXPECT_EQ(normal.defaultWorkers(false, false), 0) << "the CPU renders every frame with all its cores";
	EXPECT_EQ(normal.defaultWorkers(true, true), 0) << "a Metal frame is a process of its own";
	EXPECT_EQ(normal.defaultFrameJobs(true), 2);
	EXPECT_EQ(normal.defaultFrameJobs(false), 1);
	// with the frame's load known: light frames more at once, heavy ones two, never more than the free memory holds (RunPolicy::framesInFlight's measurements)
	EXPECT_EQ(normal.defaultFrameJobs(true, {214 * 120, 16, 15600}), 4) << "120p at 16 samples";
	EXPECT_EQ(normal.defaultFrameJobs(true, {854 * 480, 64, 15600}), 4) << "480p at 64 samples";
	EXPECT_EQ(normal.defaultFrameJobs(true, {1920 * 1080, 16, 15600}), 3) << "1080p at 16 samples";
	EXPECT_EQ(normal.defaultFrameJobs(true, {1920 * 1080, 256, 15600}), 2) << "1080p at 256 samples keeps the GPU busy by itself";
	EXPECT_EQ(normal.defaultFrameJobs(true, {1920 * 1080, 16, 6000}), 1) << "only one 1080p frame fits in 6 GB";
	EXPECT_EQ(normal.defaultFrameJobs(true, {3840 * 2160, 16, 15600}), 1) << "a 4K frame holds most of a 16 GB card";
	EXPECT_EQ(normal.defaultFrameJobs(true, {214 * 120, 16, 0}), 2) << "memory not known: the old two";
	EXPECT_EQ(normal.defaultWorkers(true, false, {214 * 120, 16, 15600}), 4) << "workers follow the frames in flight";
	EXPECT_EQ(normal.defaultFrameJobs(false, {214 * 120, 16, 15600}), 1) << "the CPU renders one frame at a time with all its cores";
	EXPECT_EQ(normal.stallSeconds(900), 900);
	EXPECT_EQ(normal.pauseSecondsAfter(2.0), 0.0);
	EXPECT_EQ(normal.cpuThreads(16), 0u) << "no limit of its own";

	LaunchArgs quietArgs;
	quietArgs.background = true;
	const RunPolicy quiet = RunPolicy::from(quietArgs);
	EXPECT_TRUE(quiet.background);
	EXPECT_EQ(quiet.dutyPercent, 50);
	EXPECT_EQ(quiet.defaultWorkers(true, false), 1);
	EXPECT_EQ(quiet.defaultFrameJobs(true), 1);
	EXPECT_EQ(quiet.stallSeconds(900), 1800);
	EXPECT_EQ(quiet.stallSeconds(0), 0) << "never stays never";
	EXPECT_NEAR(quiet.pauseSecondsAfter(2.0), 2.0, 1e-9) << "a duty of 50%: as long as the work";
	EXPECT_EQ(quiet.cpuThreads(16), 8u);
	EXPECT_EQ(quiet.cpuThreads(2), 1u);
	EXPECT_EQ(quiet.stallSeconds(2'000'000'000), 24 * 3600) << "capped at a day, not overflowed";
}

TEST(RunPolicy, TheEnvironmentAloneMakesABackgroundRunAndTheDutyCanBeChosen) {
	const char *savedBackground = std::getenv("RAY_TRACER_BACKGROUND");
	const char *savedDuty = std::getenv("RAY_TRACER_BACKGROUND_DUTY");
	const std::string background = savedBackground ? savedBackground : "", duty = savedDuty ? savedDuty : "";
	const auto put = [](const char *name, const std::string &value, bool present) {
#ifdef _WIN32
		_putenv_s(name, present ? value.c_str() : "");
#else
		if (present) setenv(name, value.c_str(), 1); else unsetenv(name);
#endif
	};
	put("RAY_TRACER_BACKGROUND", "1", true);
	put("RAY_TRACER_BACKGROUND_DUTY", "25", true);
	LaunchArgs plain;
	const RunPolicy fromEnvironment = RunPolicy::from(plain);
	EXPECT_TRUE(fromEnvironment.background);
	EXPECT_EQ(fromEnvironment.dutyPercent, 25);
	EXPECT_NEAR(fromEnvironment.pauseSecondsAfter(1.0), 3.0, 1e-9);
	put("RAY_TRACER_BACKGROUND_DUTY", "1", true);
	EXPECT_EQ(RunPolicy::from(plain).dutyPercent, 10) << "not below 10%";
	put("RAY_TRACER_BACKGROUND", "0", true);
	EXPECT_FALSE(RunPolicy::from(plain).background) << "0 is off";
	put("RAY_TRACER_BACKGROUND", background, savedBackground != nullptr);
	put("RAY_TRACER_BACKGROUND_DUTY", duty, savedDuty != nullptr);
}

#ifdef _WIN32
namespace {
// Puts back what a test that calls background_mode::apply() changes in the whole test program (the priority class and RAY_TRACER_THREADS), however the test ends.
struct BackgroundModeRestore {
	DWORD priority = GetPriorityClass(GetCurrentProcess());
	bool hadThreads = std::getenv("RAY_TRACER_THREADS") != nullptr;
	std::string threads = hadThreads ? std::getenv("RAY_TRACER_THREADS") : "";
	~BackgroundModeRestore() {
		SetPriorityClass(GetCurrentProcess(), priority);
		if (hadThreads) _putenv_s("RAY_TRACER_THREADS", threads.c_str());
		else _putenv("RAY_TRACER_THREADS=");
	}
};
}  // namespace

TEST(BackgroundMode, ItLowersThePriorityOfThisProcessAndHalvesTheCpuThreads) {
	BackgroundModeRestore restore;
	_putenv("RAY_TRACER_THREADS=");   // (removes it: the user did not choose a number)
	LaunchArgs quietArgs;
	quietArgs.background = true;
	const RunPolicy quiet = RunPolicy::from(quietArgs);

	const std::string done = background_mode::apply(quiet);
	EXPECT_NE(done.find("CPU priority below normal"), std::string::npos) << done;
	EXPECT_EQ(GetPriorityClass(GetCurrentProcess()), static_cast<DWORD>(BELOW_NORMAL_PRIORITY_CLASS));
	const char *now = std::getenv("RAY_TRACER_THREADS");
	ASSERT_NE(now, nullptr);
	EXPECT_EQ(std::atoi(now), std::max(1, static_cast<int>(std::thread::hardware_concurrency() / 2)));

	// a number the user chose is kept
	_putenv("RAY_TRACER_THREADS=3");
	background_mode::apply(quiet);
	EXPECT_STREQ(std::getenv("RAY_TRACER_THREADS"), "3");

	// outside background mode nothing at all is changed
	SetPriorityClass(GetCurrentProcess(), restore.priority);
	EXPECT_TRUE(background_mode::apply(RunPolicy::from(LaunchArgs())).empty());
	EXPECT_EQ(GetPriorityClass(GetCurrentProcess()), restore.priority);
}
#endif

TEST(CartoonRender, ABackgroundVideoTheWatchdogAllowsForThePausesBetweenFrames) {
	LaunchArgs normal;
	EXPECT_EQ(cartoon_render::timeoutFor(normal, true), 900);
	LaunchArgs quiet;
	quiet.background = true;
	EXPECT_EQ(cartoon_render::timeoutFor(quiet, true), 1800) << "a worker that sleeps as long as each frame took needs twice the time between two finished frames";
	EXPECT_EQ(cartoon_render::timeoutFor(quiet, false), 0) << "never stops being never";
	quiet.cartoon_timeout = 30;
	EXPECT_EQ(cartoon_render::timeoutFor(quiet, true), 60) << "also a limit the user chose";
	quiet.cartoon_timeout = 0;
	EXPECT_EQ(cartoon_render::timeoutFor(quiet, true), 0);
}

TEST(OutputFormat, TheFileNameDecidesWhetherAGpuRenderWritesThePngItself) {
	EXPECT_TRUE(is_png_output_path("C:/o/render.png"));
	EXPECT_TRUE(is_png_output_path("frame_0001.PNG"));
	EXPECT_TRUE(is_png_output_path("x.Png"));
	EXPECT_FALSE(is_png_output_path("x.ppm"));
	EXPECT_FALSE(is_png_output_path("x.exr"));
	EXPECT_FALSE(is_png_output_path("png"));
	EXPECT_FALSE(is_png_output_path("x.png.ppm"));
	EXPECT_FALSE(is_png_output_path(""));
}

TEST(OutputFormat, AFileIsAPngByItsFirstEightBytesWhateverItIsCalled) {
	const std::filesystem::path dir = std::filesystem::temp_directory_path() / ("output_format_test_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	std::filesystem::create_directories(dir);
	{
		std::ofstream png(dir / "real.dat", std::ios::binary);
		const unsigned char signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
		png.write(reinterpret_cast<const char *>(signature), 8);
		png << "rest of the file";
		std::ofstream text(dir / "fake.png", std::ios::binary);
		text << "P3\n1 1\n255\n1 2 3\n";   // a renderer that was asked for a .png and wrote a text PPM into it
		std::ofstream tiny(dir / "tiny.png", std::ios::binary);
		tiny << "\x89PNG";
	}
	EXPECT_TRUE(file_is_png((dir / "real.dat").string())) << "the name does not matter";
	EXPECT_FALSE(file_is_png((dir / "fake.png").string())) << "nor does a .png name make a text PPM one";
	EXPECT_FALSE(file_is_png((dir / "tiny.png").string())) << "shorter than the signature";
	EXPECT_FALSE(file_is_png((dir / "missing.png").string()));
	std::error_code ec;
	std::filesystem::remove_all(dir, ec);
}

TEST(CartoonRender, TheWorkersJobSaysWhatItsPicturesAreCalledAndAFrameRunThatIsAskedForAPpmOnlySkipsThePng) {
	LaunchArgs frame;
	ASSERT_TRUE(parseArgs({"--ppm-only"}, frame));
	EXPECT_TRUE(frame.ppm_only);
	LaunchArgs plain;
	ASSERT_TRUE(parseArgs({"--cpu"}, plain));
	EXPECT_FALSE(plain.ppm_only) << "a picture someone renders still gets its PNG";

	cartoon_worker::Job job;
	job.film = "fireflies";
	job.frames = 10;
	job.scenes = "C:/s";
	job.out = "C:/o";
	job.list = {1, 2};
	EXPECT_NE(job.picturePath(1).find("frame_0001.ppm"), std::string::npos) << "a text PPM to convert unless the job says otherwise";
	job.ext = "png";
	const std::filesystem::path file = std::filesystem::temp_directory_path() / "photonsmith_job_ext_test.txt";
	std::ofstream(file) << job.text();
	cartoon_worker::Job back;
	EXPECT_EQ(back.load(file.string()), "");
	EXPECT_EQ(back.ext, "png") << "a worker is told what to write";
	EXPECT_NE(back.picturePath(2).find("frame_0002.png"), std::string::npos);
	std::filesystem::remove(file);
}

TEST(FlagScopes, TheGpuDeviceIsChosenWithAFlagThatTheFramesOfAVideoRepeat) {
	LaunchArgs none;
	EXPECT_EQ(none.gpu_device, -1) << "not given: RAY_TRACER_GPU_DEVICE, else the first";
	LaunchArgs second;
	ASSERT_TRUE(parseArgs({"--gpu", "--gpu-device", "1", "--video"}, second));
	EXPECT_EQ(second.gpu_device, 1);
	EXPECT_EQ(cartoon_render::forwardedFlags(second), (std::vector<std::string>{"--gpu", "--gpu-device", "1"}));

	std::ostringstream err;
	std::streambuf *oldErr = std::cerr.rdbuf(err.rdbuf());
	LaunchArgs bad;
	EXPECT_TRUE(parseArgs({"--gpu-device", "-3"}, bad));
	LaunchArgs text;
	EXPECT_TRUE(parseArgs({"--gpu-device", "two"}, text));
	std::cerr.rdbuf(oldErr);
	EXPECT_EQ(bad.gpu_device, -1) << "a negative device number is refused and the first GPU used";
	EXPECT_EQ(text.gpu_device, -1);
	EXPECT_NE(err.str().find("--gpu-device"), std::string::npos);
}

TEST(CartoonWorkers, AWorkerWatchesItsParentAndSeesItAliveWhileItRuns) {
	const ParentWatch watch;   // (this test program's parent, the test runner, is alive)
	EXPECT_FALSE(watch.parentGone());
}

// A frame run's or worker's output goes to its own log when asked (Child::start's logPath): what it prints - stdout and stderr - lands in the file, appended, and
// none of it in this program's own output (a GUI reading a video's output could not keep up with fifty lines a frame).
TEST(CartoonWorkers, AChildsOutputCanGoToItsOwnLog) {
	const std::filesystem::path log = std::filesystem::temp_directory_path() / "rt_child_log_test.log";
	std::filesystem::remove(log);
#ifdef _WIN32
	const std::string exe = "C:\\Windows\\System32\\cmd.exe";
	const std::vector<std::string> say = {"/c", "echo to-out & echo to-err 1>&2"};
#else
	const std::string exe = "/bin/sh";
	const std::vector<std::string> say = {"-c", "echo to-out; echo to-err 1>&2"};
#endif
	for (int run = 0; run < 2; ++run) {
		Child child;
		ASSERT_TRUE(child.start(exe, say, "", "", log.string()));
		ASSERT_TRUE(child.wait(10));
		EXPECT_EQ(child.exitCode(), 0);
	}
	std::ifstream in(log);
	std::stringstream text;
	text << in.rdbuf();
	in.close();
	const std::string s = text.str();
	EXPECT_NE(s.find("to-out"), std::string::npos) << s;
	EXPECT_NE(s.find("to-err"), std::string::npos) << "stderr too: " << s;
	EXPECT_NE(s.find("to-out", s.find("to-out") + 1), std::string::npos) << "appended, not replaced, on a second start: " << s;
	EXPECT_EQ(runChild(exe, say, "", "", 10, nullptr, log.string()), 0) << "runChild passes it on";
	std::filesystem::remove(log);
}

// ---- the film doctor's framing checks (film_subjects.h) -------------------------------------------------------------------------------------------------

// The camera is read back from a film's header as the film wrote it (cameraText negates x), and a subject goes to the pixel the renderer draws it at: a ball at (2, 1, 0)
// seen from (0, 0, 10) with a 40-degree lens, rendered at 320 x 180 on the CPU and the GPU, has its middle at (208.9, 64.7) - the projection says (209.4, 65.3), half a pixel
// off for the pixel centres.
TEST(FilmSubjects, ASubjectGoesWhereThePictureShowsItAndOutOfTheFrameOrBehindTheCameraIsSaid) {
	using cartoon::Vec3;
	const std::string header = cartoon::sceneHeader(cartoon::cameraText(cartoon::CameraView{{0.0, 0.0, 10.0}, {0.0, 0.0, 0.0}, 40.0, 0.0, 0}), 8, 320, 180);
	Vec3 eye, target;
	double fov = 0.0;
	ASSERT_TRUE(film_subjects::cameraOf(header, eye, target, fov)) << header;
	EXPECT_NEAR(eye.z, 10.0, 1e-9);
	EXPECT_NEAR(eye.x, 0.0, 1e-9);
	EXPECT_NEAR(fov, 40.0, 1e-9);

	film_subjects::View v = film_subjects::project(eye, target, fov, {2.0, 1.0, 0.0}, 320, 180);
	EXPECT_TRUE(v.inFrame);
	EXPECT_NEAR(v.x, 209.44, 0.05) << "+x is to the right of the picture";
	EXPECT_NEAR(v.y, 65.27, 0.05) << "+y is up";
	EXPECT_NEAR(v.distance, std::sqrt(105.0), 1e-9);
	v = film_subjects::project(eye, target, fov, {0.0, 0.0, 0.0}, 320, 180);
	EXPECT_NEAR(v.x, 160.0, 1e-9);
	EXPECT_NEAR(v.y, 90.0, 1e-9);

	// the fov is across the shorter side: the top edge of a wide picture at tan(20 degrees), of a tall one at the sides
	const double edge = 10.0 * std::tan(20.0 * cartoon::kPi / 180.0);
	EXPECT_TRUE(film_subjects::project(eye, target, fov, {0.0, edge * 0.98, 0.0}, 320, 180).inFrame);
	EXPECT_FALSE(film_subjects::project(eye, target, fov, {0.0, edge * 1.02, 0.0}, 320, 180).inFrame);
	EXPECT_TRUE(film_subjects::project(eye, target, fov, {edge * 0.98, 0.0, 0.0}, 180, 320).inFrame);
	EXPECT_FALSE(film_subjects::project(eye, target, fov, {edge * 1.02, 0.0, 0.0}, 180, 320).inFrame);

	v = film_subjects::project(eye, target, fov, {0.0, 0.0, 20.0}, 320, 180);
	EXPECT_FALSE(v.inFront);
	EXPECT_FALSE(v.inFrame);
	EXPECT_FALSE(film_subjects::cameraOf("Film \"rgb\"\nWorldBegin\n", eye, target, fov)) << "no LookAt, no camera";
}

// Every film that names its subjects keeps them in the frame in the middle of each shot (a start or an end may be a subject entering or leaving). This is the doctor's check
// without a render: a camera move that loses its subject fails here.
TEST(FilmSubjects, EveryFilmThatNamesItsSubjectsKeepsThemInTheFrameInTheMiddleOfEachShot) {
	int checked = 0;
	for (const cartoon::Film *film : cartoon::films()) {
		if (!film->subjectsAt) continue;
		const std::vector<double> starts = film->shotStarts();
		for (std::size_t i = 0; i < starts.size(); ++i) {
			const double end = i + 1 < starts.size() ? starts[i + 1] : film->duration;
			const double t = 0.5 * (starts[i] + end);
			cartoon::Vec3 eye, target;
			double fov = 0.0;
			ASSERT_TRUE(film_subjects::cameraOf(film->headerAt(t), eye, target, fov)) << film->key << " at " << t;
			for (const cartoon::Vec3 &p : film->subjectsAt(t)) {
				const film_subjects::View v = film_subjects::project(eye, target, fov, p, film->meta.width, film->meta.height);
				EXPECT_TRUE(v.inFrame) << film->key << ": shot " << i + 1 << " at " << t << " s loses its subject (" << v.x << ", " << v.y << (v.inFront ? "" : ", behind")
									   << ")";
				++checked;
			}
		}
	}
	EXPECT_GT(checked, 20) << "the Nomai film names a subject for most of its shots";
}

// ---- a film's cameras file (shot_cameras.h) ------------------------------------------------------------------------------------------------------------------

namespace {
// The camera a header holds, in story space (the LookAt's x negated back), with its fov, focus distance and iso.
struct HeaderCamera {
	cartoon::Vec3 eye, target;
	double fov = 0.0, focus = 0.0, iso = 0.0;
};
HeaderCamera headerCamera(const std::string &header) {
	HeaderCamera c;
	EXPECT_TRUE(film_subjects::cameraOf(header, c.eye, c.target, c.fov)) << header;
	std::smatch m;
	if (std::regex_search(header, m, std::regex(R"("float focaldistance"\s*\[\s*(\S+))"))) c.focus = std::stod(m[1].str());
	if (std::regex_search(header, m, std::regex(R"("float iso"\s*\[\s*(\S+))"))) c.iso = std::stod(m[1].str());
	return c;
}
std::string testHeader() {
	return cartoon::sceneHeader(cartoon::cameraText(cartoon::CameraView{{1.0, 2.0, 10.0}, {1.0, 2.0, 0.0}, 40.0, 0.01, 0}), 8, 320, 180);
}
}  // namespace

// What the file says: a section a shot by number or name and [*] for all; moves, a fov or zoom, an iso or stops; and what it cannot read is said and left out.
TEST(ShotCameras, TheFileNamesShotsByNumberOrNameAndSaysWhatItCannotRead) {
	const cartoon::shot_cameras::File f = cartoon::shot_cameras::parse(
		"# a comment\n"
		"[*]\n"
		"stops 0.5\n"
		"zoom 1.5\n"
		"[2]   # the second shot\n"
		"dolly 1.5\n"
		"zoom 2\n"
		"[The answer]\n"
		"tilt -6\n"
		"fov 50\n"
		"wobble 3\n"
		"pan\n");
	EXPECT_EQ(f.problems.size(), 2u) << "an unknown word and a missing number";
	const cartoon::shot_cameras::Change second = cartoon::shot_cameras::changeFor(f, 1, "Whatever");
	EXPECT_DOUBLE_EQ(second.dolly, 1.5);
	EXPECT_DOUBLE_EQ(second.zoom, 3.0) << "zooms multiply: [*] then the shot's";
	EXPECT_DOUBLE_EQ(second.stops, 0.5) << "[*] applies to every shot";
	const cartoon::shot_cameras::Change answer = cartoon::shot_cameras::changeFor(f, 6, "The answer");
	EXPECT_DOUBLE_EQ(answer.tilt, -6.0);
	EXPECT_DOUBLE_EQ(answer.fov, 50.0);
	EXPECT_DOUBLE_EQ(answer.dolly, 0.0);
	EXPECT_FALSE(cartoon::shot_cameras::changeFor(cartoon::shot_cameras::parse(""), 0, "").any());
	EXPECT_EQ(cartoon::shot_cameras::parse("dolly 1\n").problems.size(), 1u) << "a change before any section";
}

// The changes made to a header: the camera moved along its own axes in story space (+x to the right of the picture), the aim turned, the focus kept on what the aim was on,
// the fov and the iso set (or added when the header has none); what follows WorldBegin is left alone.
TEST(ShotCameras, AChangeMovesAndTurnsTheCameraOnItsOwnAxesAndSetsTheFovAndExposure) {
	namespace sc = cartoon::shot_cameras;
	const std::string scene = testHeader() + "LookAt 9 9 9 9 9 9 0 1 0 # (not the camera: after WorldBegin)\n";
	const auto with = [&](const std::string &text) {
		sc::Change c = sc::changeFor(sc::parse("[*]\n" + text), 0, "");
		return sc::apply(scene, c);
	};
	HeaderCamera c = headerCamera(with("dolly 4"));
	EXPECT_NEAR(c.eye.z, 6.0, 1e-4) << "4 nearer along the view";
	EXPECT_NEAR(c.target.z, 0.0, 1e-4) << "the aim stays where it was";
	EXPECT_NEAR(c.focus, 6.0, 1e-3) << "the focus stays on it";
	c = headerCamera(with("truck 1\npedestal 0.5"));
	EXPECT_NEAR(c.eye.x, 2.0, 1e-4) << "truck is to the right of the picture: +x in story space";
	EXPECT_NEAR(c.target.x, 2.0, 1e-4);
	EXPECT_NEAR(c.eye.y, 2.5, 1e-4);
	c = headerCamera(with("pan 90"));
	EXPECT_NEAR(c.eye.x, 1.0, 1e-4);
	EXPECT_GT(c.target.x - c.eye.x, 9.9) << "pan right turns the aim towards +x";
	c = headerCamera(with("tilt 30"));
	EXPECT_NEAR((c.target.y - c.eye.y) / 10.0, std::sin(30.0 * cartoon::kPi / 180.0), 1e-4) << "tilt is up";
	c = headerCamera(with("zoom 2"));
	EXPECT_NEAR(std::tan(c.fov * cartoon::kPi / 360.0), std::tan(20.0 * cartoon::kPi / 180.0) / 2.0, 1e-5);
	c = headerCamera(with("fov 55"));
	EXPECT_NEAR(c.fov, 55.0, 1e-9);
	c = headerCamera(with("iso 300\nstops 1"));
	EXPECT_NEAR(c.iso, 600.0, 1e-9) << "a header without an iso gets one";
	const std::string moved = with("dolly 1\nfov 30");
	EXPECT_NE(moved.find("LookAt 9 9 9 9 9 9 0 1 0 # (not the camera"), std::string::npos) << "after WorldBegin is left alone";
	EXPECT_EQ(sc::apply(scene, sc::Change{}), scene) << "no change, the same text";
}

// Every film the program lists reads its cameras file when a frame is made: an edit is seen by the next frame, without a rebuild, and only in the shot it names.
TEST(ShotCameras, AFilmsCameraFileIsReadWhenAFrameIsMadeAndChangesOnlyItsShot) {
	const std::filesystem::path dir = std::filesystem::temp_directory_path() / "rt_shot_cameras_test";
	std::filesystem::remove_all(dir);
	std::filesystem::create_directories(dir / "film_assets" / "camera-file-test");
	const char *saved = std::getenv("RAY_TRACER_PBRT_DIR");
	const std::string savedValue = saved ? saved : "";
#ifdef _WIN32
	_putenv_s("RAY_TRACER_PBRT_DIR", dir.string().c_str());
#else
	setenv("RAY_TRACER_PBRT_DIR", dir.string().c_str(), 1);
#endif
	const std::filesystem::path file = dir / "film_assets" / "camera-file-test" / "cameras.txt";
	{
		std::ofstream(file) << "[Second]\ndolly 2\n";
	}
	cartoon::Film film{"camera-file-test", "Camera file test", "", 10.0, 24, [](double) { return testHeader() + "Shape \"sphere\"\n"; }, [](double) { return testHeader(); },
					   []() { return std::vector<double>{0.0, 5.0, 8.0}; }, 1.0, {9999, "", "First|Second|Third", 8, 0.0}};
	const cartoon::Film shown = cartoon::detail::withFilmFiles(film);
	EXPECT_NEAR(headerCamera(shown.sceneAt(2.0)).eye.z, 10.0, 1e-4) << "the first shot is as the code made it";
	EXPECT_NEAR(headerCamera(shown.sceneAt(7.0)).eye.z, 8.0, 1e-4) << "the second is moved, by its name";
	EXPECT_NEAR(headerCamera(shown.headerAt(7.0)).eye.z, 8.0, 1e-4) << "the header alone too (the doctor's framing checks read it)";
	EXPECT_NE(shown.sceneAt(7.0).find("Shape \"sphere\""), std::string::npos);
	std::this_thread::sleep_for(std::chrono::milliseconds(20));
	{
		std::ofstream(file) << "[1]\ndolly 3\n# longer, so that the size changes as well as the time\n";
	}
	EXPECT_NEAR(headerCamera(shown.sceneAt(2.0)).eye.z, 7.0, 1e-4) << "an edit is read by the next frame";
	EXPECT_NEAR(headerCamera(shown.sceneAt(7.0)).eye.z, 10.0, 1e-4);
	// an edit to a shot that neither of the two instants the program stamp looks at (0 s and half way) falls in still changes the stamp, so a resume compares the frames again
	const std::string stamp = cartoon_render::programStamp(shown, "");
	const std::string atStart = shown.sceneAt(0.0), halfWay = shown.sceneAt(5.0);
	std::this_thread::sleep_for(std::chrono::milliseconds(20));
	{
		std::ofstream(file) << "[1]\ndolly 3\n# longer, so that the size changes as well as the time\n[Third]\ndolly 1\n";
	}
	EXPECT_NEAR(headerCamera(shown.sceneAt(9.0)).eye.z, 9.0, 1e-4);
	EXPECT_EQ(shown.sceneAt(0.0), atStart);
	EXPECT_EQ(shown.sceneAt(5.0), halfWay);
	EXPECT_NE(cartoon_render::programStamp(shown, ""), stamp) << "the cameras file is part of the program stamp";
#ifdef _WIN32
	_putenv_s("RAY_TRACER_PBRT_DIR", savedValue.c_str());
#else
	if (saved) setenv("RAY_TRACER_PBRT_DIR", savedValue.c_str(), 1); else unsetenv("RAY_TRACER_PBRT_DIR");
#endif
	std::filesystem::remove_all(dir);
}

// ---- the character rig (rig.h) and parts made once (film_cache::part) ------------------------------------------------------------------------------------

// A walk along a path: where it is and which way it faces on each segment, how many steps it has made, that it stops at the end, and a footfall every stride at the time the
// walk reaches it, feet in turn.
TEST(CharacterRig, AWalkFollowsItsPathAndItsFootfallsComeEveryStrideInTurn) {
	using cartoon::Vec3;
	const cartoon::rig::PathWalk w{{{0.0, 0.0, 0.0}, {0.0, 0.0, 4.0}, {3.0, 0.0, 4.0}}, 10.0, 2.0, 1.0};
	EXPECT_DOUBLE_EQ(w.length(), 7.0);
	cartoon::rig::PathWalk::At a = w.at(9.0);
	EXPECT_FALSE(a.moving) << "not yet started";
	EXPECT_NEAR(a.at.z, 0.0, 1e-12);
	a = w.at(11.0);
	EXPECT_TRUE(a.moving);
	EXPECT_NEAR(a.at.z, 2.0, 1e-12);
	EXPECT_NEAR(a.yaw, 0.0, 1e-12) << "along +z";
	EXPECT_NEAR(a.steps, 2.0, 1e-12);
	a = w.at(12.5);
	EXPECT_NEAR(a.at.x, 1.0, 1e-12) << "round the corner";
	EXPECT_NEAR(a.at.z, 4.0, 1e-12);
	EXPECT_NEAR(a.yaw, 90.0, 1e-12) << "along +x";
	a = w.at(100.0);
	EXPECT_FALSE(a.moving) << "stopped at the end";
	EXPECT_NEAR(a.at.x, 3.0, 1e-12);
	EXPECT_NEAR(a.dist, 7.0, 1e-12);

	const std::vector<cartoon::rig::PathWalk::Footfall> all = w.footfalls(-1e9, 1e9);
	ASSERT_EQ(all.size(), 7u) << "one each metre of the 7";
	for (std::size_t i = 0; i < all.size(); ++i) {
		EXPECT_NEAR(all[i].t, 10.0 + 0.5 * (i + 1), 1e-12);
		EXPECT_EQ(all[i].foot, static_cast<int>(i % 2)) << "the left first, then in turn";
		EXPECT_EQ(all[i].step, static_cast<int>(i + 1));
	}
	EXPECT_NEAR(all[4].at.x, 1.0, 1e-12) << "the fifth step is a metre round the corner";
	const std::vector<cartoon::rig::PathWalk::Footfall> some = w.footfalls(11.0, 12.0);
	ASSERT_EQ(some.size(), 2u) << "(from, to]: the steps at 11.5 and 12";
	EXPECT_EQ(some[0].step, 3);
}

// A blink shuts and opens within its half-time each way, once a period; a look at something turns the head within its limits; sitting eases in and out.
TEST(CharacterRig, BlinksLooksAndSittingStayInTheirRanges) {
	using cartoon::Vec3;
	int shut = 0;
	bool wasShut = false;
	for (double t = 0.0; t < 43.0; t += 0.01) {
		const double b = cartoon::rig::blink(t, 7);
		ASSERT_GE(b, 0.0);
		ASSERT_LE(b, 1.0);
		if (b > 0.5 && !wasShut) ++shut;
		wasShut = b > 0.5;
	}
	EXPECT_GE(shut, 9) << "about one blink in every 4.3 s";
	EXPECT_LE(shut, 11);

	cartoon::rig::Aim aim = cartoon::rig::aimAt({0.0, 1.6, 0.0}, 0.0, {1.0, 1.6, 1.0});
	EXPECT_NEAR(aim.yaw, 45.0, 1e-9) << "+x is to the left of a body facing +z";
	EXPECT_NEAR(aim.pitch, 0.0, 1e-9);
	aim = cartoon::rig::aimAt({0.0, 1.6, 0.0}, 0.0, {0.0, 1.6, -1.0});
	EXPECT_NEAR(std::abs(aim.yaw), 70.0, 1e-9) << "behind it: held at what a neck can do";
	aim = cartoon::rig::aimAt({0.0, 1.6, 0.0}, 350.0, {0.0, 11.6, 1.0}, 70.0, 40.0, 0.5);
	EXPECT_NEAR(aim.yaw, 5.0, 1e-9) << "350 degrees round is 10 to the right, so 10 to the left to face +z; half of it";
	EXPECT_NEAR(aim.pitch, 20.0, 1e-9) << "up, held at 40, half of it";

	EXPECT_DOUBLE_EQ(cartoon::rig::sitAmount(29.0, 30.0, 41.0), 0.0);
	EXPECT_DOUBLE_EQ(cartoon::rig::sitAmount(35.0, 30.0, 41.0), 1.0);
	EXPECT_DOUBLE_EQ(cartoon::rig::sitAmount(42.0, 30.0, 41.0), 0.0);
	EXPECT_GT(cartoon::rig::sitAmount(30.4, 30.0, 41.0), 0.0);
	EXPECT_DOUBLE_EQ(cartoon::rig::sitAmount(99.0, 30.0, 0.0), 1.0) << "no standing up: seated from then on";
}

// A part that never changes shape is made once - a second frame does not make it again - and placed by its frame, mirrored into pbrt's space like every film mesh.
TEST(CharacterRig, APartIsMadeOnceAndPlacedByItsFrame) {
	int made = 0;
	const auto make = [&] {
		++made;
		return cartoon::uvSphere(0.1, 8, 4);
	};
	const std::string name = "rig_test_part_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
	const std::string a = cartoon::film_cache::part("rig-test", name, "Material \"diffuse\"", cartoon::translate(2.0, 0.0, 0.0), make);
	const std::string b = cartoon::film_cache::part("rig-test", name, "Material \"diffuse\"", cartoon::translate(0.0, 3.0, 0.0), make);
	EXPECT_EQ(made, 1) << "made once";
	EXPECT_NE(a.find("Shape \"plymesh\""), std::string::npos) << a;
	EXPECT_NE(a.find("ConcatTransform [ 1.000000 0.000000 0.000000 0.000000  0.000000 1.000000 0.000000 0.000000  0.000000 0.000000 1.000000 0.000000  -2.000000 0.000000 0.000000 1.000000 ]"),
			  std::string::npos) << "+x in story space is -x in pbrt's: " << a;
	EXPECT_NE(b.find("0.000000 3.000000 0.000000 1.000000 ]"), std::string::npos) << b;
	EXPECT_EQ(cartoon::film_cache::part("rig-test", name + "_empty", "Material \"diffuse\"", cartoon::Mat4{}, [] { return cartoon::Mesh{}; }), std::string()) << "nothing to draw";
}

// ---- a film's events and the sound effects they play (film_events.h, sfx.h) ------------------------------------------------------------------------------

// Events from motion: a walk's footfalls in a shot's span, a body moved along a curve stepping each time its distance passes a step, panned by where the camera sees them
// (+x to the right of the picture) and quieter further away.
TEST(FilmEvents, FootfallsComeFromTheWalksAndArePannedAndQuietenedFromTheCamera) {
	using cartoon::Vec3;
	const cartoon::CameraView view{{0.0, 1.5, 10.0}, {0.0, 1.5, 0.0}, 40.0, 0.0, 0};
	EXPECT_GT(cartoon::film_events::panOf(view, {3.0, 1.5, 0.0}), 0.3) << "+x: the right";
	EXPECT_LT(cartoon::film_events::panOf(view, {-3.0, 1.5, 0.0}), -0.3);
	EXPECT_NEAR(cartoon::film_events::panOf(view, {0.0, 0.0, 0.0}), 0.0, 1e-9);
	EXPECT_DOUBLE_EQ(cartoon::film_events::panOf(view, {5.0, 1.5, 20.0}), 0.0) << "behind the camera: the middle";
	EXPECT_DOUBLE_EQ(cartoon::film_events::audibility(2.0, 3.0), 1.0);
	EXPECT_LT(cartoon::film_events::audibility(30.0, 3.0), 0.3);
	EXPECT_GE(cartoon::film_events::audibility(1e6, 3.0), 0.1);

	const cartoon::film_events::ViewAt at = [&](double) { return view; };
	std::vector<cartoon::FilmEvent> out;
	const cartoon::rig::PathWalk walk{{{-4.0, 0.0, 0.0}, {4.0, 0.0, 0.0}}, 1.0, 1.0, 1.0, 0.5};
	cartoon::film_events::footfalls(out, walk, 0.0, 4.0, 1.0, at);
	ASSERT_EQ(out.size(), 3u) << "steps half a stride in: at 1.5, 2.5 and 3.5 s";
	EXPECT_NEAR(out[0].t, 1.5, 1e-12);
	EXPECT_LT(out[0].pan, out[2].pan) << "walking to the right, heard moving to the right";
	EXPECT_NE(out[0].seed, out[1].seed) << "each step its own take";

	std::vector<cartoon::FilmEvent> curve;
	cartoon::film_events::stepsAlong(curve, [](double t) { return 2.0 * t; }, [](double t) { return Vec3{0.0, 0.0, -t}; }, 0.0, 2.0, 1.0, 0.5, 1.0, at);
	ASSERT_EQ(curve.size(), 4u) << "2 units a second, a step at 0.5, 1.5, 2.5, 3.5 units";
	EXPECT_NEAR(curve[0].t, 0.25, 1.0 / 240.0);
	EXPECT_NEAR(curve[3].t, 1.75, 1.0 / 240.0);
	EXPECT_GT(curve[0].strength, curve[3].strength) << "further from the camera, quieter";
	std::vector<cartoon::FilmEvent> mixed = {curve[2], out[0], curve[0]};
	mixed = cartoon::film_events::sorted(mixed);
	EXPECT_TRUE(mixed[0].t <= mixed[1].t && mixed[1].t <= mixed[2].t);
}

// The sound effects: every footstep surface, impact, flash and touch is a sound, different from take to take, and play() puts each event's sound at its time.
TEST(FilmEvents, TheSoundLibraryMakesEverySoundAndPlaysEachEventAtItsTime) {
	using cartoon::sfx::Surface;
	for (Surface s : {Surface::Dust, Surface::Stone, Surface::Grass, Surface::Wood, Surface::Metal, Surface::Snow}) {
		const std::vector<float> a = cartoon::sfx::footstep(s, 1), b = cartoon::sfx::footstep(s, 2);
		ASSERT_FALSE(a.empty());
		double energy = 0.0, diff = 0.0;
		for (std::size_t i = 0; i < std::min(a.size(), b.size()); ++i) {
			energy += a[i] * a[i];
			diff += (a[i] - b[i]) * (a[i] - b[i]);
		}
		EXPECT_GT(energy, 1e-4) << static_cast<int>(s);
		EXPECT_GT(diff, 1e-6) << "two takes differ: " << static_cast<int>(s);
	}
	EXPECT_FALSE(cartoon::sfx::impact(0.0, 1).empty());
	EXPECT_GT(cartoon::sfx::impact(1.0, 1).size(), cartoon::sfx::impact(0.0, 1).size()) << "a heavy landing rings longer";
	EXPECT_FALSE(cartoon::sfx::flash(1).empty());
	EXPECT_FALSE(cartoon::sfx::touch(1).empty());

	cartoon::audio::Mix mix(4.0);
	cartoon::FilmEvent step;
	step.t = 1.0;
	step.kind = cartoon::EventKind::Footfall;
	cartoon::FilmEvent bang;
	bang.t = 3.0;
	bang.kind = cartoon::EventKind::Impact;
	cartoon::sfx::play(mix, {step, bang});
	const std::vector<std::int16_t> pcm = mix.finish(0.0, 0.0);
	const auto loudness = [&](double t0, double t1) {
		double sum = 0.0;
		std::size_t n = 0;
		for (std::size_t i = static_cast<std::size_t>(t0 * cartoon::audio::kRate) * 2; i < static_cast<std::size_t>(t1 * cartoon::audio::kRate) * 2 && i < pcm.size(); ++i, ++n) sum += std::abs(pcm[i]);
		return n ? sum / n : 0.0;
	};
	EXPECT_LT(loudness(0.2, 0.95), 1.0) << "silence before the step";
	EXPECT_GT(loudness(1.0, 1.15), 20.0) << "the step at 1 s";
	EXPECT_GT(loudness(3.0, 3.3), 20.0) << "the impact at 3 s";
}

// The Nomai film's events: in time order, its footfalls only while the explorer walks in the shot on screen (a step every 0.84 m at the walk's speed), the touchdown when the
// shuttle sets down, and the hands on the stone.
TEST(FilmEvents, TheNomaiFilmsFootstepsAreTheShotsOwnWalks) {
	const cartoon::Film *film = cartoon::find("nomai-signal");
	ASSERT_NE(film, nullptr);
	ASSERT_TRUE(static_cast<bool>(film->events));
	const std::vector<cartoon::FilmEvent> events = film->events();
	ASSERT_GT(events.size(), 40u);
	int footfalls = 0, impacts = 0, touches = 0;
	for (std::size_t i = 0; i < events.size(); ++i) {
		const cartoon::FilmEvent &e = events[i];
		if (i > 0) EXPECT_LE(events[i - 1].t, e.t) << "in time order";
		EXPECT_GE(e.strength, 0.0);
		EXPECT_LE(e.strength, 1.0);
		EXPECT_LE(std::abs(e.pan), 1.0);
		footfalls += e.kind == cartoon::EventKind::Footfall;
		impacts += e.kind == cartoon::EventKind::Impact;
		touches += e.kind == cartoon::EventKind::Touch;
	}
	EXPECT_EQ(impacts, 1) << "the touchdown";
	EXPECT_EQ(touches, 2) << "the carved wall and the ring of glyphs";
	EXPECT_GT(footfalls, 40);
	// the footsteps shot (50 - 57 s): the walk from 49 s at 0.70 m/s, a step every 1.2 s, half a step in
	std::vector<double> inShot;
	for (const cartoon::FilmEvent &e : events)
		if (e.kind == cartoon::EventKind::Footfall && e.t >= 50.0 && e.t < 57.0) inShot.push_back(e.t);
	ASSERT_GE(inShot.size(), 5u);
	for (std::size_t i = 1; i < inShot.size(); ++i) EXPECT_NEAR(inShot[i] - inShot[i - 1], 0.84 / 0.70, 1e-9);
	EXPECT_NEAR(std::fmod(inShot[0] - 49.0, 1.2), 0.6, 1e-9);
	for (const cartoon::FilmEvent &e : events)
		if (e.kind == cartoon::EventKind::Footfall) EXPECT_FALSE(e.t > 166.0 && e.t < 250.0) << "nobody walks between the star map and the dawn: " << e.t;
}

// ---- a shot's share of the samples (shot_cameras.h `samples`, read by the video) ------------------------------------------------------------------------

// A shot's `samples` line puts its share into the frame's header, where it changes the fingerprint (so a resume renders the frame again) and where the worker reads it: each frame
// renders at its own shot's share of the video's samples, never fewer than one.
TEST(ShotSamples, AFrameRendersAtItsShotsShareOfTheVideosSamples) {
	namespace sc = cartoon::shot_cameras;
	EXPECT_DOUBLE_EQ(sc::sampleShareOf("LookAt 0 0 0 0 0 -1 0 1 0\nWorldBegin\n"), 1.0);
	const std::string halved = sc::apply(testHeader() + "Shape \"sphere\"\n", sc::changeFor(sc::parse("[*]\nsamples 0.5\n"), 0, ""));
	EXPECT_DOUBLE_EQ(sc::sampleShareOf(halved), 0.5);
	EXPECT_NE(frame_fingerprint::of(halved), frame_fingerprint::of(testHeader() + "Shape \"sphere\"\n")) << "a new share is a new frame";
	EXPECT_DOUBLE_EQ(sc::sampleShareOf("WorldBegin\n# @rt-samples 0.5\n"), 1.0) << "only the header's line counts";
	EXPECT_EQ(sc::samplesFor(32, 0.25), 8);
	EXPECT_EQ(sc::samplesFor(2, 0.1), 1) << "never fewer than one";
	EXPECT_EQ(sc::parse("[*]\nsamples 0\n").problems.size(), 1u);

	const std::filesystem::path dir = std::filesystem::temp_directory_path() / "rt_shot_samples_test";
	std::filesystem::remove_all(dir);
	std::filesystem::create_directories(dir / "film_assets" / "samples-test");
	const char *saved = std::getenv("RAY_TRACER_PBRT_DIR");
	const std::string savedValue = saved ? saved : "";
#ifdef _WIN32
	_putenv_s("RAY_TRACER_PBRT_DIR", dir.string().c_str());
#else
	setenv("RAY_TRACER_PBRT_DIR", dir.string().c_str(), 1);
#endif
	{
		std::ofstream(dir / "film_assets" / "samples-test" / "cameras.txt") << "[2]\nsamples 0.25\n";
	}
	const cartoon::Film film{"samples-test", "Samples test", "", 10.0, 24, [](double) { return testHeader() + "Shape \"sphere\"\n"; }, [](double) { return testHeader(); },
							 []() { return std::vector<double>{0.0, 5.0}; }, 1.0, {9998, "", "First|Second", 8, 0.0}};
	const cartoon::Film shown = cartoon::detail::withFilmFiles(film);
	cartoon_worker::Job job;
	job.film = "samples-test";
	job.filmPtr = &shown;
	job.frames = 240;
	job.scenes = (dir / "scenes").string();
	job.out = (dir / "out").string();
	job.list = {24, 168};   // 1 s (the first shot) and 7 s (the second)
	std::filesystem::create_directories(job.out);
	LaunchArgs args;
	RenderSetup setup;
	setup.samples_per_pixel = 32;
	std::vector<int> used;
	cartoon_worker::run(job, args, setup, [&](const LaunchArgs &, const RenderSetup &s) {
		used.push_back(s.samples_per_pixel);
		return 0;
	});
	ASSERT_EQ(used.size(), 2u);
	EXPECT_EQ(used[0], 32) << "the first shot at the video's samples";
	EXPECT_EQ(used[1], 8) << "the second at its quarter";
#ifdef _WIN32
	_putenv_s("RAY_TRACER_PBRT_DIR", savedValue.c_str());
#else
	if (saved) setenv("RAY_TRACER_PBRT_DIR", savedValue.c_str(), 1); else unsetenv("RAY_TRACER_PBRT_DIR");
#endif
	std::filesystem::remove_all(dir);
}

// ---- sliding feet (film_checks.h) -----------------------------------------------------------------------------------------------------------------------

// The check samples each shot at the film's frame rate and reports the fastest a planted foot moves: a foot planted and held is still, one planted while the body drags it
// skates, and a foot in the air may move as it likes.
TEST(FilmChecks, APlantedFootThatMovesIsFoundAndOneThatStaysIsNot) {
	using cartoon::Vec3;
	cartoon::Film film{"feet-test", "Feet test", "", 4.0, 24, [](double) { return std::string(); }, [](double) { return std::string(); },
					   []() { return std::vector<double>{0.0, 2.0}; }, 0.0, {9997, "", "Still|Skating", 8, 0.0}};
	film.feetAt = [](double t) {
		std::vector<cartoon::Foot> feet(2);
		feet[0] = {{0.0, 0.0, 0.0}, true};                    // planted and still
		feet[1] = {{t < 2.0 ? 0.0 : 0.5 * t, 0.0, 0.0}, t >= 2.0};   // in the air for the first shot; planted and dragged at 0.5 a second in the second
		if (t < 2.0) feet[1].at = {std::sin(t * 9.0), 1.0, 0.0};   // (flying about: not planted, not counted)
		return feet;
	};
	const std::vector<cartoon::film_checks::Slide> found = cartoon::film_checks::slidingFeet(film);
	ASSERT_EQ(found.size(), 2u);
	EXPECT_EQ(found[0].shot, 0);
	EXPECT_LT(found[0].worst, 1e-9) << "the still foot, and the flying one is not counted";
	EXPECT_EQ(found[1].shot, 1);
	EXPECT_NEAR(found[1].worst, 0.5, 1e-6) << "the dragged foot";
	EXPECT_GT(found[1].worst, cartoon::film_checks::kSlideLimit);
}

// The Nomai explorer's walk: a foot on the ground goes back under the body at the body's own pace, so in every shot it walks in the planted feet stay put (the turning
// floating steps, a body on a 6 m curve, the most: a little over a tenth of a metre a second). Its old swinging gait skated at 0.55 to 0.92 m/s.
TEST(FilmChecks, TheNomaiExplorersPlantedFeetStayPut) {
	const cartoon::Film *film = cartoon::find("nomai-signal");
	ASSERT_NE(film, nullptr);
	const std::vector<cartoon::film_checks::Slide> found = cartoon::film_checks::slidingFeet(*film);
	ASSERT_GE(found.size(), 8u) << "every shot it walks in";
	const std::vector<std::string> names = cartoon::shotNamesOf(*film);
	for (const cartoon::film_checks::Slide &s : found) EXPECT_LT(s.worst, 0.12) << names[static_cast<std::size_t>(s.shot)] << " at " << s.at << " s";   // (feet locked: only where a walk starts)
	// the gait itself: planted for one step from half a step in, the foot's place going back evenly from 1 to -1
	double swing = 0.0, lift = 0.0;
	bool planted = false;
	cartoon::nomai::gait(0.5, swing, lift, planted);
	EXPECT_TRUE(planted);
	EXPECT_NEAR(swing, 1.0, 1e-9);
	cartoon::nomai::gait(1.0, swing, lift, planted);
	EXPECT_NEAR(swing, 0.0, 1e-9) << "half way through the stance, under the hip";
	EXPECT_DOUBLE_EQ(lift, 0.0);
	cartoon::nomai::gait(2.0, swing, lift, planted);
	EXPECT_FALSE(planted) << "then a step in the air (down again at 2.5)";
	EXPECT_NEAR(lift, 1.0, 1e-9) << "highest half way through the swing";
}

// ---- gestures (rig.h: named poses from a character's library) -------------------------------------------------------------------------------------------

// A gesture eases in, holds and eases out at its film times; later gestures go on top of earlier ones; a moving pose gets the time since its gesture began; a pose that
// the library does not have is an error by name; and contacts are listed for the soundtrack.
TEST(Gestures, AGestureEasesInHoldsEasesOutAndGoesOnTopOfTheOnesBefore) {
	struct Pose {
		double arm = 0.0, head = 0.0;
	};
	const cartoon::rig::PoseLibrary<Pose> library = {
		{"raise", [](Pose &p, double w, double) { p.arm = cartoon::lerp(p.arm, 1.0, w); }},
		{"lower", [](Pose &p, double w, double) { p.arm = cartoon::lerp(p.arm, -1.0, w); }},
		{"nod", [](Pose &p, double w, double since) { p.head += w * since; }},
	};
	const std::vector<cartoon::rig::Gesture> acting = {{"raise", 1.0, 2.0, 5.0, 6.0, 1.8}, {"lower", 3.0, 3.0, 4.0, 4.0}, {"nod", 7.0, 7.0}};
	EXPECT_DOUBLE_EQ(cartoon::rig::act(Pose{}, acting, 0.5, library).arm, 0.0) << "before";
	EXPECT_DOUBLE_EQ(cartoon::rig::act(Pose{}, acting, 1.5, library).arm, 0.5) << "half way in (smoothly)";
	EXPECT_DOUBLE_EQ(cartoon::rig::act(Pose{}, acting, 2.5, library).arm, 1.0) << "held";
	EXPECT_DOUBLE_EQ(cartoon::rig::act(Pose{}, acting, 3.5, library).arm, -1.0) << "a later gesture on top (an instant one)";
	EXPECT_DOUBLE_EQ(cartoon::rig::act(Pose{}, acting, 4.5, library).arm, 1.0) << "and gone again";
	EXPECT_DOUBLE_EQ(cartoon::rig::act(Pose{}, acting, 6.5, library).arm, 0.0) << "eased out";
	EXPECT_DOUBLE_EQ(cartoon::rig::act(Pose{}, acting, 9.0, library).head, 2.0) << "a moving pose gets the time since it began, and holds from then on";
	EXPECT_THROW(cartoon::rig::act(Pose{}, {{"wave", 0.0, 1.0}}, 0.5, library), std::invalid_argument);
	const std::vector<double> contacts = cartoon::rig::contactsOf(acting);
	ASSERT_EQ(contacts.size(), 1u);
	EXPECT_DOUBLE_EQ(contacts[0], 1.8);
	EXPECT_TRUE(cartoon::rig::contactsOf(acting, 2.0, 9.0).empty());
}

// The Nomai explorer's library has the poses its film acts with and the ones a new film would want, each changing only what it says (a look moves only the head).
TEST(Gestures, TheExplorersLibraryHasItsPosesAndEachChangesOnlyItsOwnParts) {
	const auto &poses = cartoon::nomai::explorerPoses();
	for (const char *name : {"reach to the wall", "hand on the lectern", "point up", "kneel and touch", "look up", "look down", "surprised", "hands together",
							 "raise the sceptre", "nod", "shake head", "sit", "crouch"})
		EXPECT_EQ(poses.count(name), 1u) << name;
	cartoon::nomai::ExplorerPose p;
	const cartoon::nomai::ExplorerPose before = p;
	poses.at("look up")(p, 1.0, 0.0);
	EXPECT_DOUBLE_EQ(p.headPitch, -35.0);
	EXPECT_DOUBLE_EQ(p.handL.y, before.handL.y);
	EXPECT_DOUBLE_EQ(p.crouch, before.crouch);
	poses.at("kneel and touch")(p, 0.5, 0.0);
	EXPECT_DOUBLE_EQ(p.crouch, 0.5);
}

// ---- screen direction across a cut (film_checks.h) ------------------------------------------------------------------------------------------------------

// A walker going left to right, then filmed from the other side in the same place (a flip); then cut to somewhere else going the other way (a journey); two still subjects,
// then seen from the other side (the camera crossed the line between them).
TEST(FilmChecks, ACutThatReversesTheScreenDirectionOrCrossesTheLineIsFound) {
	using cartoon::Vec3;
	const auto front = [] { return cartoon::sceneHeader(cartoon::cameraText(cartoon::CameraView{{0.0, 0.0, 10.0}, {0.0, 0.0, 0.0}, 40.0, 0.0, 0}), 8, 320, 180); };
	const auto back = [] { return cartoon::sceneHeader(cartoon::cameraText(cartoon::CameraView{{0.0, 0.0, -10.0}, {0.0, 0.0, 0.0}, 40.0, 0.0, 0}), 8, 320, 180); };
	cartoon::Film film{"cuts-test", "Cuts test", "", 10.0, 24, [](double) { return std::string(); },
					   [=](double t) { return (t >= 2.0 && t < 4.0) || t >= 8.0 ? back() : front(); }, []() { return std::vector<double>{0.0, 2.0, 4.0, 6.0, 8.0}; }, 0.0,
					   {9996, "", "Walks|Other side|Far away|Two|Two, other side", 8, 0.0}};
	film.meta.width = 320;
	film.meta.height = 180;
	film.subjectsAt = [](double t) -> std::vector<Vec3> {
		if (t < 4.0) return {{-1.0 + 0.5 * t, 0.0, 0.0}};                  // walking along +x
		if (t < 6.0) return {{-2.0 + 2.0 * (t - 4.0), 0.0, -20.0}};       // far away, along +x, seen from the front again: left to right
		return {{-1.0, 0.0, 0.0}, {1.0, 0.0, 0.0}};                         // two, standing
	};
	const std::vector<cartoon::film_checks::CutIssue> found = cartoon::film_checks::screenDirection(film);
	ASSERT_EQ(found.size(), 3u);
	EXPECT_EQ(found[0].shot, 1);
	EXPECT_TRUE(found[0].samePlace) << "the same walker in the same place, from the other side: a flip";
	EXPECT_FALSE(found[0].crossedLine);
	EXPECT_GT(found[0].before, 0.0);
	EXPECT_LT(found[0].after, 0.0);
	EXPECT_EQ(found[1].shot, 2);
	EXPECT_FALSE(found[1].samePlace) << "somewhere else on the way: a journey";
	EXPECT_EQ(found[2].shot, 4);
	EXPECT_TRUE(found[2].crossedLine) << "the two swapped sides";
}

// ---- music cues (music_cues.h) --------------------------------------------------------------------------------------------------------------------------

// A cues file: sections by shot name or number (or none: the film's start), times from the shot's start or @ a film time, notes as numbers or names, a length after the
// notes, the options; what it cannot read is said by line.
TEST(MusicCues, TheFileIsReadAndEachCueIsPlacedByItsShot) {
	namespace mc = cartoon::music_cues;
	const mc::Sheet sheet = mc::parse(
		"# a comment\n"
		"1.0 bell C4\n"
		"[Second]\n"
		"2.5 piano D4 F#4 3.0 gain 0.05 pan -0.3\n"
		"@9.0 knock\n"
		"[2]\n"
		"+0.5 pluck A4 C5 E5 spread 0.25\n"
		"[Third]\n"
		"1.0 guitar Bb2\n"
		"oops bell C4\n"
		"2.0\n"
		"3.0 bell C4 gain\n");
	ASSERT_EQ(sheet.cues.size(), 5u);
	EXPECT_EQ(sheet.problems.size(), 3u) << "a time that is not one, a cue without an instrument, an option without its number";
	EXPECT_DOUBLE_EQ(sheet.cues[0].notes[0], 60.0) << "C4 is middle C";
	EXPECT_EQ(sheet.cues[1].notes.size(), 2u);
	EXPECT_DOUBLE_EQ(sheet.cues[1].notes[1], 66.0) << "F#4";
	EXPECT_DOUBLE_EQ(sheet.cues[1].length, 3.0) << "a number with a point after the notes is the length";
	EXPECT_DOUBLE_EQ(sheet.cues[1].gain, 0.05);
	EXPECT_DOUBLE_EQ(sheet.cues[1].pan, -0.3);
	EXPECT_DOUBLE_EQ(sheet.cues[4].notes[0], 46.0) << "Bb2";
	const std::vector<double> starts = {0.0, 4.0, 8.0};
	const std::vector<std::string> names = {"First", "Second", "Third"};
	double t = 0.0;
	std::string why;
	ASSERT_TRUE(mc::timeOf(sheet.cues[0], starts, names, t, why));
	EXPECT_DOUBLE_EQ(t, 1.0) << "no section: from the film's start";
	ASSERT_TRUE(mc::timeOf(sheet.cues[1], starts, names, t, why));
	EXPECT_DOUBLE_EQ(t, 6.5) << "from the second shot's start, by name";
	ASSERT_TRUE(mc::timeOf(sheet.cues[2], starts, names, t, why));
	EXPECT_DOUBLE_EQ(t, 9.0) << "@: a film time";
	ASSERT_TRUE(mc::timeOf(sheet.cues[3], starts, names, t, why));
	EXPECT_DOUBLE_EQ(t, 4.5) << "by number";
	EXPECT_FALSE(mc::timeOf(sheet.cues[4], {0.0}, {"First"}, t, why)) << "a shot the film does not have";
	EXPECT_NE(why.find("Third"), std::string::npos);
}

// The cues go on the soundtrack at their times, with the film's own instruments or the generic ones; a film with a cues file and no score of its own gets a soundtrack
// from the cues, read when it is made.
TEST(MusicCues, AFilmsCuesAreHeardAtTheirTimesOnItsSoundtrack) {
	const std::filesystem::path dir = std::filesystem::temp_directory_path() / "rt_music_cues_test";
	std::filesystem::remove_all(dir);
	std::filesystem::create_directories(dir / "film_assets" / "cues-test");
	const char *saved = std::getenv("RAY_TRACER_PBRT_DIR");
	const std::string savedValue = saved ? saved : "";
#ifdef _WIN32
	_putenv_s("RAY_TRACER_PBRT_DIR", dir.string().c_str());
#else
	setenv("RAY_TRACER_PBRT_DIR", dir.string().c_str(), 1);
#endif
	{
		std::ofstream(dir / "film_assets" / "cues-test" / "cues.txt") << "[Second]\n1.0 bell A4 1.0 gain 0.3 send 0\n2.0 thud\n@0.5 whistle C5 send 0\n";   // (send 0: no reverb tail between them)
	}
	cartoon::Film film{"cues-test", "Cues test", "", 6.0, 24, [](double) { return std::string(); }, [](double) { return std::string(); },
					   []() { return std::vector<double>{0.0, 2.0}; }, 0.0, {9995, "", "First|Second", 8, 0.0}};
	film.instruments = [] {
		return std::map<std::string, cartoon::CueInstrument>{{"whistle", [](cartoon::audio::Mix &m, double t, const std::vector<double> &n, double, double g, double p, double s) {
																   m.put(t, cartoon::audio::chirp(cartoon::audio::hz(n[0]), cartoon::audio::hz(n[0]), 0.3), g, p, s);
															   }}};
	};
	const cartoon::Film shown = cartoon::detail::withFilmFiles(film);
	ASSERT_TRUE(static_cast<bool>(shown.soundtrack)) << "a cues file makes a soundtrack";
	cartoon::audio::Mix mix(6.0);
	testing::internal::CaptureStderr();
	shown.soundtrack(mix);
	const std::string said = testing::internal::GetCapturedStderr();
	EXPECT_NE(said.find("thud"), std::string::npos) << "an instrument it does not know is said: " << said;
	const std::vector<std::int16_t> pcm = mix.finish(0.0, 0.0);
	const auto loudness = [&](double t0, double t1) {
		double sum = 0.0;
		std::size_t n = 0;
		for (std::size_t i = static_cast<std::size_t>(t0 * cartoon::audio::kRate) * 2; i < static_cast<std::size_t>(t1 * cartoon::audio::kRate) * 2 && i < pcm.size(); ++i, ++n) sum += std::abs(pcm[i]);
		return n ? sum / n : 0.0;
	};
	EXPECT_LT(loudness(0.1, 0.45), 1.0) << "silence before the first cue";
	EXPECT_GT(loudness(0.5, 0.75), 20.0) << "the film's own instrument at 0.5 s";
	EXPECT_LT(loudness(1.5, 2.95), 1.0) << "silence";
	EXPECT_GT(loudness(3.0, 3.3), 20.0) << "the bell at the second shot's start + 1 s";
#ifdef _WIN32
	_putenv_s("RAY_TRACER_PBRT_DIR", savedValue.c_str());
#else
	if (saved) setenv("RAY_TRACER_PBRT_DIR", savedValue.c_str(), 1); else unsetenv("RAY_TRACER_PBRT_DIR");
#endif
	std::filesystem::remove_all(dir);
}

// ---- the films' baseline (film_baseline.h) --------------------------------------------------------------------------------------------------------------

// Each shot is recorded by the fingerprint of its middle's scene; a comparison names the shots that changed, the new ones and the ones gone, for the films looked at only; and
// recording keeps the other films' lines, in film and shot order.
TEST(FilmBaseline, AFilmsShotsAreRecordedAndAChangeNamesTheShotsItTouched) {
	namespace fb = cartoon::film_baseline;
	int version = 0;
	cartoon::Film film{"baseline-test", "Baseline test", "", 6.0, 24, [&](double t) { return std::string("scene at ") + std::to_string(t < 3.0 ? 0 : 1 + version); },
					   [](double) { return std::string(); }, []() { return std::vector<double>{0.0, 3.0}; }, 0.0, {9994, "", "One|Two", 8, 0.0}};
	const auto fp = [](const std::string &s) { return s; };   // (the scene's text itself stands for its fingerprint here)
	const std::vector<fb::Entry> first = fb::entriesOf(film, fp);
	ASSERT_EQ(first.size(), 2u);
	EXPECT_EQ(first[1].name, "Two");
	EXPECT_EQ(first[0].fingerprint, "scene at 0") << "the first shot's middle";
	const std::string text = fb::merged(fb::parse("other|1|abc|A shot of another film\n"), first);
	const std::vector<fb::Entry> recorded = fb::parse(text);
	ASSERT_EQ(recorded.size(), 3u) << "the other film's line is kept";
	EXPECT_EQ(recorded[0].film, "baseline-test") << "in film order";
	EXPECT_TRUE(fb::compare(recorded, fb::entriesOf(film, fp)).empty()) << "nothing changed";
	version = 1;   // the second shot changes
	const std::vector<fb::Difference> d = fb::compare(recorded, fb::entriesOf(film, fp));
	ASSERT_EQ(d.size(), 1u);
	EXPECT_EQ(d[0].kind, "changed");
	EXPECT_EQ(d[0].entry.shot, 2);
	std::vector<fb::Entry> shorter = fb::entriesOf(film, fp);
	shorter.pop_back();
	EXPECT_EQ(fb::compare(recorded, shorter).back().kind, "gone");
}

// ---- follow-through (rig::Spring) ------------------------------------------------------------------------------------------------------------------------

// A spring settles on a point that stops, trails one that moves steadily by speed x damping / stiffness, swings past one that stops suddenly, and is the same at the same time
// however often it is asked (a frame is a function of its time alone).
TEST(FollowThrough, ASpringTrailsAMovingPointSwingsWhenItStopsAndSettles) {
	using cartoon::Vec3;
	const cartoon::rig::Spring s{60.0, 8.0, 3.0, 1.0 / 240.0};
	// moving at 1 m/s along +z until 5 s, then stopped
	const auto where = [](double t) { return Vec3{0.0, 0.0, std::min(t, 5.0)}; };
	const Vec3 steady = cartoon::rig::lag(s, where, 4.0);
	EXPECT_NEAR(steady.z, -8.0 / 60.0, 0.01) << "speed x damping / stiffness behind";
	EXPECT_NEAR(steady.x, 0.0, 1e-12);
	double most = -1.0;
	for (double t = 5.0; t < 6.0; t += 0.02) most = std::max(most, cartoon::rig::lag(s, where, t).z);
	EXPECT_GT(most, 0.005) << "it swings past where the point stopped";
	EXPECT_NEAR(cartoon::rig::lag(s, where, 9.0).z, 0.0, 1e-3) << "and settles";
	EXPECT_EQ(cartoon::rig::follow(s, where, 5.37).z, cartoon::rig::follow(s, where, 5.37).z) << "the same each time";
	// the explorer's robe: back as it sets off (and walks), forward just after it stops
	const auto walker = [](double t) { return Vec3{0.0, 0.0, 0.8 * std::clamp(t - 1.0, 0.0, 4.0)}; };
	EXPECT_LT(cartoon::nomai::robeTrail(walker, 0.0, 3.0).z, -0.02) << "facing +z and walking: the hem trails back";
	double forward = -1.0;
	for (double t = 5.0; t < 6.5; t += 0.05) forward = std::max(forward, cartoon::nomai::robeTrail(walker, 0.0, t).z);
	EXPECT_GT(forward, 0.005) << "when it stops the hem swings forward";
}

// ---- props with physics (prop_physics.h) ----------------------------------------------------------------------------------------------------------------

// A dropped ball lands when gravity says, bounces lower each time (its restitution), rolls on with what its grip left, turning by the distance over its radius, and stops; every
// landing is an impact, each softer than the last.
TEST(PropPhysics, ADroppedBallBouncesLowerEachTimeRollsAndStops) {
	using cartoon::Vec3;
	namespace ph = cartoon::physics;
	const ph::Bounce ball{{0.0, 2.1, 0.0}, {1.0, 0.0, 0.0}, 1.0, 0.1};
	const std::vector<ph::Impact> hits = ph::impactsOf(ball);
	ASSERT_GE(hits.size(), 3u);
	EXPECT_NEAR(hits[0].t, 1.0 + std::sqrt(2.0 * 2.0 / ph::kGravity), 1e-9) << "2 m to fall (its centre to the ground less its radius)";
	EXPECT_NEAR(hits[0].speed, std::sqrt(2.0 * ph::kGravity * 2.0), 1e-9);
	EXPECT_NEAR(hits[0].at.x, hits[0].t - 1.0, 1e-9) << "drifting at 1 m/s";
	for (std::size_t i = 1; i < hits.size(); ++i) EXPECT_LT(hits[i].speed, hits[i - 1].speed);
	EXPECT_NEAR(hits[1].speed / hits[0].speed, ball.restitution, 1e-9);
	EXPECT_NEAR(ph::at(ball, 0.5).at.y, 2.1, 1e-12) << "before it starts, where it was";
	const ph::Body flying = ph::at(ball, hits[0].t - 0.1);
	EXPECT_GT(flying.at.y, ball.radius);
	EXPECT_FALSE(flying.resting);
	const ph::Body late = ph::at(ball, 30.0);
	EXPECT_TRUE(late.resting) << "rolled to a stop";
	EXPECT_NEAR(late.at.y, ball.radius, 1e-9) << "on the ground";
	EXPECT_NEAR(ph::at(ball, 31.0).at.x, late.at.x, 1e-12) << "and stays";
	EXPECT_NEAR(late.spin, ph::at(ball, 30.0).spin, 1e-12);
	EXPECT_NEAR(late.spinAxis.z, -1.0, 1e-9) << "rolling along +x turns it about -z (up x the way it goes)";
}

// A block tips over its bottom edge once it is pushed past its balance, lands at 90 degrees (an impact where its top meets the ground) and stays; one not pushed stands.
TEST(PropPhysics, APushedBlockTopplesAndLandsAndOneNotPushedStands) {
	using cartoon::Vec3;
	namespace ph = cartoon::physics;
	const ph::Topple slab{2.0, {1.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, 0.6, 0.08, 60.0};
	double landed = -1.0;
	EXPECT_DOUBLE_EQ(ph::angleOf(slab, 1.0), 0.0);
	EXPECT_DOUBLE_EQ(ph::angleOf(slab, 10.0, &landed), 90.0);
	EXPECT_GT(landed, 2.2);
	EXPECT_LT(landed, 4.0);
	EXPECT_LT(ph::angleOf(slab, 0.5 * (2.0 + landed)), 90.0);
	const std::vector<ph::Impact> hits = ph::impactsOf(slab);
	ASSERT_EQ(hits.size(), 1u);
	EXPECT_NEAR(hits[0].t, landed, 1e-9);
	EXPECT_NEAR(hits[0].at.x, 1.0 - 0.6, 1e-9) << "its top lands 0.6 m away on the side the turn takes it (+y about +z goes to -x)";
	const Vec3 top = cartoon::transformPoint(ph::transformOf(slab, 10.0), {1.0, 0.6, 0.0});
	EXPECT_NEAR(top.x, 0.4, 1e-9);
	EXPECT_NEAR(top.y, 0.0, 1e-9);
	const ph::Topple still{2.0, {0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, 0.6, 0.08, 0.0};
	EXPECT_DOUBLE_EQ(ph::angleOf(still, 10.0), 0.0) << "not pushed past its balance: it stands";
	EXPECT_TRUE(ph::impactsOf(still).empty());
}

// ---- feet locked to the ground (rig::FootLock) -----------------------------------------------------------------------------------------------------------

// A gait that slides (its feet move too little for the body's pace) on bumpy ground: with the lock, a planted foot stays exactly where it came down, on the ground there; it
// lifts off without a jump; in the air it follows its gait over the ground.
TEST(FootLock, APlantedFootStaysWhereItCameDownOnTheGroundAndLiftsWithoutAJump) {
	using cartoon::Vec3;
	cartoon::rig::FootLock k;
	k.frameAt = [](double t) { return cartoon::translate(0.0, 0.0, t); };   // 1 m/s along +z
	k.stepsAt = [](double t) { return t; };
	k.footAt = [](double steps, int s) { return Vec3{s == 0 ? 0.1 : -0.1, 0.03, 0.3 * std::sin(cartoon::kPi * (steps + s))}; };
	k.plantedAt = [](double steps, int s) { return std::cos(cartoon::kPi * (steps + s)) <= 0.0; };
	k.groundAt = [](double x, double z) { return 0.2 * std::sin(x + 2.0 * z); };
	double worst = 0.0, unlocked = 0.0, jump = 0.0;
	Vec3 last[2], lastFree[2];
	bool wasPlanted[2] = {false, false};
	for (double t = 1.0; t < 6.0; t += 1.0 / 240.0) {
		for (int s = 0; s < 2; ++s) {
			const bool planted = k.plantedAt(t, s);
			const Vec3 world = cartoon::transformPoint(k.frameAt(t), cartoon::rig::lockedFoot(k, s, t));
			const Vec3 free = cartoon::transformPoint(k.frameAt(t), k.footAt(t, s));
			if (planted && wasPlanted[s]) {
				worst = std::max(worst, cartoon::length(world - last[s]) * 240.0);
				unlocked = std::max(unlocked, cartoon::length(free - lastFree[s]) * 240.0);
				EXPECT_NEAR(world.y, 0.2 * std::sin(world.x + 2.0 * world.z) + 0.03, 1e-6) << "on the ground where it came down";
			}
			if (!planted && wasPlanted[s]) jump = std::max(jump, cartoon::length(world - last[s]));
			last[s] = world;
			lastFree[s] = free;
			wasPlanted[s] = planted;
		}
	}
	EXPECT_LT(worst, 1e-4) << "held (to a tenth of a millimetre a second: where the search pins the landing)";
	EXPECT_GT(unlocked, 0.2) << "(the gait alone slides)";
	EXPECT_LT(jump, 0.01) << "no jump when it lifts (a 240th of a second apart)";
}

// ---- recordings and voices (audio::readWav, music_cues `sample ... by NAME`) -----------------------------------------------------------------------------

namespace {
// A WAV file of a tone: `bits` 16 or 24 PCM or 32 float, at `rate`, `channels` the same in each.
void writeToneWav(const std::filesystem::path &path, int rate, int bits, int channels, double seconds, double hz) {
	std::ofstream f(path, std::ios::binary);
	const auto u32 = [&](std::uint32_t v) { for (int i = 0; i < 4; ++i) f.put(static_cast<char>((v >> (8 * i)) & 0xFF)); };
	const auto u16 = [&](std::uint16_t v) { for (int i = 0; i < 2; ++i) f.put(static_cast<char>((v >> (8 * i)) & 0xFF)); };
	const std::uint32_t frames = static_cast<std::uint32_t>(rate * seconds), bytes = frames * channels * (bits / 8);
	f.write("RIFF", 4);
	u32(36 + bytes);
	f.write("WAVEfmt ", 8);
	u32(16);
	u16(bits == 32 ? 3 : 1);
	u16(static_cast<std::uint16_t>(channels));
	u32(static_cast<std::uint32_t>(rate));
	u32(static_cast<std::uint32_t>(rate * channels * (bits / 8)));
	u16(static_cast<std::uint16_t>(channels * (bits / 8)));
	u16(static_cast<std::uint16_t>(bits));
	f.write("data", 4);
	u32(bytes);
	for (std::uint32_t i = 0; i < frames; ++i) {
		const double v = 0.5 * std::sin(2.0 * cartoon::kPi * hz * i / rate);
		for (int c = 0; c < channels; ++c) {
			if (bits == 16) u16(static_cast<std::uint16_t>(static_cast<std::int16_t>(v * 32767)));
			else if (bits == 24) {
				const std::int32_t s = static_cast<std::int32_t>(v * 8388607);
				for (int k = 0; k < 3; ++k) f.put(static_cast<char>((s >> (8 * k)) & 0xFF));
			} else {
				const float x = static_cast<float>(v);
				std::uint32_t raw;
				std::memcpy(&raw, &x, 4);
				u32(raw);
			}
		}
	}
}
}  // namespace

// A WAV file is read whatever its bits, channels and rate, as one channel at the mix's rate; what is not a WAV file is said.
TEST(Recordings, AWavFileIsReadAsOneChannelAtTheMixsRateWhateverItsFormat) {
	const std::filesystem::path dir = std::filesystem::temp_directory_path() / "rt_recordings_test";
	std::filesystem::remove_all(dir);
	std::filesystem::create_directories(dir);
	for (const auto &[rate, bits, channels] : std::vector<std::tuple<int, int, int>>{{44100, 16, 1}, {22050, 16, 1}, {48000, 24, 2}, {32000, 32, 2}}) {
		const std::filesystem::path p = dir / ("tone_" + std::to_string(rate) + "_" + std::to_string(bits) + ".wav");
		writeToneWav(p, rate, bits, channels, 0.5, 440.0);
		std::vector<float> s;
		std::string why;
		ASSERT_TRUE(cartoon::audio::readWav(p.string(), s, &why)) << why;
		EXPECT_NEAR(static_cast<double>(s.size()), 0.5 * cartoon::audio::kRate, 3.0) << rate << " Hz: resampled to the mix's rate";
		double peak = 0.0;
		for (float v : s) peak = std::max(peak, static_cast<double>(std::abs(v)));
		EXPECT_NEAR(peak, 0.5, 0.02) << bits << "-bit, " << channels << " channel(s)";
	}
	std::ofstream(dir / "not.wav") << "hello";
	std::vector<float> s;
	std::string why;
	EXPECT_FALSE(cartoon::audio::readWav((dir / "not.wav").string(), s, &why));
	EXPECT_NE(why.find("not a WAV"), std::string::npos);
	std::filesystem::remove_all(dir);
}

// A recording cued `by` a character is its voice: voiceAt is how loud it is at a moment (the loudest of the line 1), 0 before and after, and the recording is heard on the
// film's soundtrack at its time.
TEST(Recordings, ACharactersVoiceIsAsLoudAsItsLineAtEachMomentAndIsHeard) {
	const std::filesystem::path dir = std::filesystem::temp_directory_path() / "rt_voice_test";
	std::filesystem::remove_all(dir);
	std::filesystem::create_directories(dir / "film_assets" / "voice-test");
	const char *saved = std::getenv("RAY_TRACER_PBRT_DIR");
	const std::string savedValue = saved ? saved : "";
#ifdef _WIN32
	_putenv_s("RAY_TRACER_PBRT_DIR", dir.string().c_str());
#else
	setenv("RAY_TRACER_PBRT_DIR", dir.string().c_str(), 1);
#endif
	writeToneWav(dir / "film_assets" / "voice-test" / "line.wav", 22050, 16, 1, 1.0, 300.0);
	{
		std::ofstream(dir / "film_assets" / "voice-test" / "cues.txt") << "[Second]\n0.5 sample line.wav gain 0.8 send 0 by kai\n";
	}
	const std::vector<double> starts = {0.0, 2.0};
	const std::vector<std::string> names = {"First", "Second"};
	EXPECT_DOUBLE_EQ(cartoon::music_cues::voiceAt("voice-test", "kai", 2.0, starts, names), 0.0) << "before its line";
	EXPECT_GT(cartoon::music_cues::voiceAt("voice-test", "kai", 3.0, starts, names), 0.9) << "speaking (a steady tone: as loud as its loudest)";
	EXPECT_DOUBLE_EQ(cartoon::music_cues::voiceAt("voice-test", "kai", 4.0, starts, names), 0.0) << "after it";
	EXPECT_DOUBLE_EQ(cartoon::music_cues::voiceAt("voice-test", "mira", 3.0, starts, names), 0.0) << "someone else is silent";
	cartoon::Film film{"voice-test", "Voice test", "", 5.0, 24, [](double) { return std::string(); }, [](double) { return std::string(); },
					   [starts]() { return starts; }, 0.0, {9993, "", "First|Second", 8, 0.0}};
	const cartoon::Film shown = cartoon::detail::withFilmFiles(film);
	ASSERT_TRUE(static_cast<bool>(shown.soundtrack));
	cartoon::audio::Mix mix(5.0);
	shown.soundtrack(mix);
	const std::vector<std::int16_t> pcm = mix.finish(0.0, 0.0);
	const auto loudness = [&](double t0, double t1) {
		double sum = 0.0;
		std::size_t n = 0;
		for (std::size_t i = static_cast<std::size_t>(t0 * cartoon::audio::kRate) * 2; i < static_cast<std::size_t>(t1 * cartoon::audio::kRate) * 2 && i < pcm.size(); ++i, ++n) sum += std::abs(pcm[i]);
		return n ? sum / n : 0.0;
	};
	EXPECT_LT(loudness(1.0, 2.4), 1.0);
	EXPECT_GT(loudness(2.6, 3.4), 100.0) << "the line at the second shot's start + 0.5 s";
#ifdef _WIN32
	_putenv_s("RAY_TRACER_PBRT_DIR", savedValue.c_str());
#else
	if (saved) setenv("RAY_TRACER_PBRT_DIR", savedValue.c_str(), 1); else unsetenv("RAY_TRACER_PBRT_DIR");
#endif
	std::filesystem::remove_all(dir);
}

// ---- a shot's colour grade (lens_look.h's lift, gamma, gain, tint; shot_cameras.h) --------------------------------------------------------------------

// The grade's words take one number (all three channels) or three; gain multiplies, lift adds, gamma bends the middle, tint casts a colour and keeps grey's brightness;
// a cameras file's grade goes onto the frame's look line (after the film's own words, so it wins), or makes one.
TEST(ShotGrade, LiftGammaGainAndTintGradeAPictureAndACamerasFileSetsThemForAShot) {
	lens_look::Look look;
	EXPECT_EQ(lens_look::parse("warmth 0.1 lift 0.02 gain 1 1 2 gamma 2 tint 1 0.5 1", look), "");
	EXPECT_DOUBLE_EQ(look.warmth, 0.1) << "the other words still read";
	EXPECT_DOUBLE_EQ(look.lift[1], 0.02) << "one number: all three";
	EXPECT_DOUBLE_EQ(look.gain[2], 2.0);
	EXPECT_DOUBLE_EQ(look.gamma[0], 2.0);
	EXPECT_NE(lens_look::parse("gain 1 2", look), "") << "two numbers: neither one nor three";

	const auto graded = [](const std::string &words, float r, float g, float b) {
		lens_look::Look l;
		EXPECT_EQ(lens_look::parse(words, l), "");
		float px[3] = {r, g, b};
		lens_look::apply(px, 1, 1, l);
		return std::array<float, 3>{px[0], px[1], px[2]};
	};
	EXPECT_NEAR(graded("gain 2", 0.25f, 0.25f, 0.25f)[0], 0.5f, 1e-6);
	EXPECT_NEAR(graded("lift 0.1", 0.25f, 0.25f, 0.25f)[1], 0.35f, 1e-6);
	EXPECT_NEAR(graded("gamma 2", 0.25f, 0.25f, 0.25f)[2], 0.5f, 1e-6) << "0.25 ^ (1/2)";
	const std::array<float, 3> tinted = graded("tint 1.2 1 0.8", 0.5f, 0.5f, 0.5f);
	EXPECT_GT(tinted[0], tinted[2]) << "warmer";
	EXPECT_NEAR(0.2126 * tinted[0] + 0.7152 * tinted[1] + 0.0722 * tinted[2], 0.5, 1e-5) << "grey keeps its brightness";

	namespace sc = cartoon::shot_cameras;
	const sc::File f = sc::parse("[*]\ngain 1.1\n[2]\ntint 1 0.9 1.1\nlift 0.01\n");
	EXPECT_TRUE(f.problems.empty());
	const sc::Change c = sc::changeFor(f, 1, "");
	EXPECT_DOUBLE_EQ(c.gain[0], 1.1) << "[*] and the shot's together";
	EXPECT_DOUBLE_EQ(c.tint[2], 1.1);
	const std::string withLook = sc::apply("# @rt-look bloom 0.1 warmth 0.2\n" + testHeader(), c);
	const std::size_t line = withLook.find("# @rt-look");
	const std::string lookLine = withLook.substr(line, withLook.find('\n', line) - line);
	EXPECT_NE(lookLine.find("bloom 0.1 warmth 0.2 lift 0.01"), std::string::npos) << "added after the film's own words: " << lookLine;
	lens_look::Look read;
	EXPECT_EQ(lens_look::parse(lookLine.substr(10), read), "");
	EXPECT_DOUBLE_EQ(read.gain[1], 1.1);
	EXPECT_NE(sc::apply(testHeader(), c).find("# @rt-look lift 0.01"), std::string::npos) << "a header without a look line gets one";
	EXPECT_EQ(sc::parse("[*]\ngain 1 2\n").problems.size(), 1u);
}

// ---- transitions ----------------------------------------------------------------------------------------------------------------------------------------------------------------

// A dissolve blends the outgoing and incoming pictures in linear light (half way: the mean of their light, not of their numbers), a fade takes a frame to black and up, and a wipe
// brings the incoming picture in from the left with a soft edge; before the cut the frame's own picture is the outgoing one, after it the incoming one.
TEST(FilmTransitions, ADissolveBlendsInLinearLightAFadeGoesToBlackAndAWipeComesInFromTheLeft) {
	const int w = 40, h = 2;
	const std::vector<unsigned char> black(w * h * 3, 0), white(w * h * 3, 255);
	const std::vector<cartoon::Transition> trs = {{cartoon::Transition::Dissolve, 5.0, 2.0}, {cartoon::Transition::Fade, 10.0, 1.0}, {cartoon::Transition::Wipe, 15.0, 2.0}};
	EXPECT_EQ(film_transitions::at(trs, 3.0).in, nullptr) << "a plain frame";
	// half way through the dissolve, just before its cut: the frame's own (black, the outgoing shot) and the other side's (white)
	const film_transitions::At mid = film_transitions::at(trs, 5.0 - 1e-6);
	ASSERT_NE(mid.in, nullptr);
	EXPECT_TRUE(mid.beforeCut);
	EXPECT_NEAR(mid.amount, 0.5, 1e-3);
	const std::vector<unsigned char> d = film_transitions::blend(mid, black, white, w, h);
	EXPECT_NEAR(d[0], 188, 1) << "half the light of white is 188 in sRGB, not 128";
	// after the cut the frame's own is the incoming shot: late in the dissolve it is mostly itself
	const film_transitions::At late = film_transitions::at(trs, 5.9);
	EXPECT_FALSE(late.beforeCut);
	EXPECT_GT(film_transitions::blend(late, white, black, w, h)[0], 240);
	// a fade: black at its cut, the frame itself at its ends
	const film_transitions::At atCut = film_transitions::at(trs, 10.0);
	EXPECT_EQ(film_transitions::blend(atCut, white, white, w, h)[0], 0);
	EXPECT_GT(film_transitions::blend(film_transitions::at(trs, 10.49), white, white, w, h)[0], 250);
	// a wipe a third of the way: the left of the frame is the incoming picture, the right the outgoing one
	const film_transitions::At wipe = film_transitions::at(trs, 15.2);   // (after its cut: the frame's own picture is the incoming one)
	const std::vector<unsigned char> p = film_transitions::blend(wipe, white, black, w, h);
	EXPECT_EQ(p[0], 255) << "the left: incoming";
	EXPECT_EQ(p[(w - 1) * 3], 0) << "the right: outgoing";
}

// ---- temporal denoising ---------------------------------------------------------------------------------------------------------------------------------------------------------

// Each frame is made again from its neighbours in its shot where its neighbourhood is the same (noise): a still picture's noise goes down, an edge that moves stays where this
// frame has it, and nothing is taken across a cut; a resumed run makes nothing again.
TEST(FilmTemporal, StillNoiseIsAveragedAwayAMovingEdgeStaysSharpAndCutsAreKept) {
	namespace fs = std::filesystem;
	const fs::path dir = fs::temp_directory_path() / "photonsmith_temporal_test";
	fs::remove_all(dir);
	fs::create_directories(dir);
	const int w = 64, h = 32, count = 10;
	std::mt19937 rng(7);
	std::normal_distribution<double> noise(0.0, 8.0);
	// shot 1 (frames 0-4): a grey field with an edge moving right 4 pixels a frame; shot 2 (frames 5-9): a brighter field
	for (int f = 0; f < count; ++f) {
		std::vector<unsigned char> px(static_cast<std::size_t>(w) * h * 3);
		for (int y = 0; y < h; ++y)
			for (int x = 0; x < w; ++x) {
				const double base = f < 5 ? (x < 20 + 4 * f ? 90.0 : 160.0) : 200.0;
				const unsigned char v = static_cast<unsigned char>(std::clamp(base + noise(rng), 0.0, 255.0));
				for (int c = 0; c < 3; ++c) px[(static_cast<std::size_t>(y) * w + x) * 3 + c] = v;
			}
		ASSERT_TRUE(write_png((dir / cartoon_render::encName(f)).string().c_str(), w, h, px.data()));
	}
	cartoon::Film film{};
	film.duration = 10.0;
	film.shotStarts = []() { return std::vector<double>{0.0, 5.0}; };
	EXPECT_EQ(film_temporal::apply(film, dir, count, 2), count);
	const auto load = [&](const fs::path &p) { return film_temporal::load(p); };
	const auto spread = [&](const film_temporal::Frame &f, int x0, int x1) {   // the noise left in a band of columns: the mean distance from its mean
		double sum = 0.0, sum2 = 0.0;
		int n = 0;
		for (int y = 0; y < h; ++y)
			for (int x = x0; x < x1; ++x) {
				const double v = f.rgb[(static_cast<std::size_t>(y) * w + x) * 3];
				sum += v, sum2 += v * v, ++n;
			}
		return std::sqrt(sum2 / n - (sum / n) * (sum / n));
	};
	const auto raw = load(dir / cartoon_render::encName(7)), den = load(dir / film_temporal::kFolder / cartoon_render::encName(7));
	ASSERT_TRUE(raw && den);
	EXPECT_LT(spread(*den, 0, w), 0.62 * spread(*raw, 0, w)) << "a still picture's noise: five frames averaged";
	// the moving edge in frame 2 is at x = 28: left of it dark, right of it bright, as in the frame itself
	const auto edge = load(dir / film_temporal::kFolder / cartoon_render::encName(2));
	double left = 0.0, right = 0.0;
	for (int y = 0; y < h; ++y) left += edge->rgb[(static_cast<std::size_t>(y) * w + 25) * 3], right += edge->rgb[(static_cast<std::size_t>(y) * w + 31) * 3];
	EXPECT_LT(left / h, 110.0) << "not smeared by its neighbours' edges";
	EXPECT_GT(right / h, 140.0);
	// across the cut: the last frame of shot 1 is not brightened by shot 2
	const auto last = load(dir / film_temporal::kFolder / cartoon_render::encName(4));
	double edgeOfShot = 0.0;
	for (int y = 0; y < h; ++y) edgeOfShot += last->rgb[(static_cast<std::size_t>(y) * w + 5) * 3];
	EXPECT_LT(edgeOfShot / h, 100.0);
	EXPECT_EQ(film_temporal::apply(film, dir, count, 2), 0) << "made already from the same frames";
	fs::remove_all(dir);
}
