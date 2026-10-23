// cartoon_tests.cpp -- the cartoon video presets: the toolkit (src/shared/cartoon/cartoon_lib.h), the Lamp and Ball film, the preset table entry and the launcher's handling of
// `--camera-path cartoon:<key>` (launcher/launcher_args.h, launcher/cartoon_render.h).

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <map>
#include <cctype>
#include <cmath>
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
#include "../../launcher/launcher_args.h"
#include "../../launcher/flag_scopes.h"
#include "../../launcher/render_setup.h"
#include "../../launcher/background_mode.h"
#include "../../launcher/run_policy.h"
#include "../../src/shared/cartoon/cartoons.h"
#include "../../src/shared/cartoon/cartoon_stills.h"
#include "../../src/shared/pbrt_discover.h"
#include "../../src/shared/output_crop.h"
#include "../../src/shared/output_format.h"
#include "../../src/shared/ppm_text.h"
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
	EXPECT_EQ(cartoons, static_cast<int>(cartoon::films().size())) << "one preset per film, nothing more";
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
	// a program that has been rebuilt is another program
	const std::filesystem::path exe = std::filesystem::temp_directory_path() / "photonsmith_signature_test.exe";
	std::ofstream(exe, std::ios::binary) << "one";
	const std::string before = cartoon_render::resumeSignature(*film, a, 480, 24, 1280, 720, 128, 8, exe.string());
	EXPECT_EQ(before, cartoon_render::resumeSignature(*film, a, 480, 24, 1280, 720, 128, 8, exe.string()));
	std::ofstream(exe, std::ios::binary) << "another program";
	EXPECT_NE(before, cartoon_render::resumeSignature(*film, a, 480, 24, 1280, 720, 128, 8, exe.string()));
	std::filesystem::remove(exe);
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
		{"--mlt-max-depth", {"4"}}, {"--ao-max-dist", {"2"}}, {"--ao-illum-scale", {"1"}}, {"--ao-illum-rgb", {"1", "1", "1"}}};
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
	EXPECT_EQ(cartoon_render::workerMemoryLimitBytes(plain), 8192ull * 1024 * 1024);
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
