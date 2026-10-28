// cartoon_craft_tests.cpp -- the craft of a film, split from cartoon_tests.cpp (which keeps the toolkit, the presets and the video pipeline): the film
// doctor's framing and screen-direction checks, a film's cameras file, the character rig and its gestures, events, samples a shot, sliding and locked feet, music cues,
// the films' baseline, follow-through, props with physics, recordings and voices, a shot's grade, transitions and temporal denoising.

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
#include "../../launcher/run_log.h"
#include "../../src/shared/cartoon/film_subjects.h"
#include "fixture_skip.h"
#include "../../src/shared/cartoon/cartoon_films.h"   // (the films themselves, which these tests reach into)
#include "../../src/shared/cartoon/cartoons_registry.h"
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
	const cartoon::rig::PathWalk walk{{{-4.0, 0.0, 0.0}, {3.5, 0.0, 0.0}}, 1.0, 1.0, 1.0, 0.5};   // (7.5 m: whole strides after the first half one)
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
	// (a stride of about 0.84 m: stretched or shrunk a little so the walk ends as a foot comes down)
	for (std::size_t i = 1; i < inShot.size(); ++i) EXPECT_NEAR(inShot[i] - inShot[i - 1], 0.84 / 0.70, 0.03);
	EXPECT_NEAR(std::fmod(inShot[0] - 49.0, inShot[1] - inShot[0]), 0.5 * (inShot[1] - inShot[0]), 1e-6);
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

// A camera inside a medium's sphere (smoke here) is reported, once a shot, with how far inside; the camera's own medium round it (mist) is not, nor a sphere it is outside of,
// nor one that is scaled (where its sphere is is not read).
TEST(FilmChecks, ACameraInsideSmokeIsFoundButNotInItsOwnMist) {
	const auto scene = [](double t) {
		char eye[120];
		std::snprintf(eye, sizeof eye, "LookAt %.3f 1 0  0 1 -10  0 1 0\n", t < 2.0 ? 0.0 : 20.0);   // (the first shot in the smoke, the second clear of it)
		return std::string("MediumInterface \"\" \"mist\"\n") + eye + "Camera \"perspective\"\nWorldBegin\n"
			   "AttributeBegin\n  MediumInterface \"mist\" \"\"\n  Material \"interface\"\n  Translate 0 1 0\n  Shape \"sphere\" \"float radius\" [ 50 ]\nAttributeEnd\n"
			   "AttributeBegin\n  Translate 1 0 0\n  Translate 0 1.5 0\n  Material \"interface\"\n  MediumInterface \"smoke\" \"\"\n  Shape \"sphere\" \"float radius\" [ 3 ]\nAttributeEnd\n"
			   "AttributeBegin\n  Translate 0 1 0\n  Scale 2 2 2\n  Material \"interface\"\n  MediumInterface \"scaled\" \"\"\n  Shape \"sphere\" \"float radius\" [ 3 ]\nAttributeEnd\n"
			   "AttributeBegin\n  Translate 0 1 -30\n  Material \"interface\"\n  MediumInterface \"far\" \"\"\n  Shape \"sphere\" \"float radius\" [ 3 ]\nAttributeEnd\n";
	};
	cartoon::Film film{"media-test", "Media test", "", 4.0, 24, scene, [&](double t) { return scene(t).substr(0, scene(t).find("WorldBegin")); },
					   []() { return std::vector<double>{0.0, 2.0}; }, 0.0, {9996, "", "In|Out", 8, 0.0}};
	const std::vector<cartoon::film_checks::MediumSphere> spheres = cartoon::film_checks::mediumSpheres(scene(0.0));
	ASSERT_EQ(spheres.size(), 3u) << "the mist, the smoke and the far one (the scaled one left out)";
	EXPECT_EQ(spheres[1].name, "smoke");
	EXPECT_NEAR(spheres[1].centre.x, 1.0, 1e-9);
	EXPECT_NEAR(spheres[1].centre.y, 1.5, 1e-9);
	EXPECT_NEAR(spheres[1].radius, 3.0, 1e-9);
	const std::vector<cartoon::film_checks::InMedium> found = cartoon::film_checks::cameraInMedia(film);
	ASSERT_EQ(found.size(), 1u);
	EXPECT_EQ(found[0].shot, 0);
	EXPECT_EQ(found[0].name, "smoke");
	EXPECT_NEAR(found[0].inside, 3.0 - std::hypot(1.0, 0.5), 1e-3);
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
