/**
 * @file scene_room_tests.cpp
 * @brief The Scene Builder's "Room from a photo" (src/shared/scene_room.h): the room.json the photo helper writes becomes ordinary Builder objects
 *
 * The helper itself (tools/photo_to_room, a depth model plus a geometry fit) is tested in Python; here the data file it writes is read with hand-made files, and (with the launcher
 * built) a room made from one is rendered to check that left stays left, the light lights the room and the camera sees it.
 */

#include <gtest/gtest.h>
#include "scene_builder_test_helpers.h"

#include "../../src/shared/scene_room.h"

#include <algorithm>
#include <string>

using namespace scene_doc;

namespace {

// A room 4 m wide (x -2..2), 4.5 m deep (z -0.5..4), 2.7 m high, seen from (0, 1.4, 0) looking down +z; the left wall red, the right wall green, a lamp in the ceiling. The Builder's
// world is right-handed, so looking along +z the picture's LEFT is +x: the red wall is at x = 2.
std::string roomJson(const std::string& extra = "") {
	return R"json({
 "camera": {"position": [0, 1.4, 0], "target": [0, 1.4, 1], "up": [0, 1, 0], "vfov": 90},
 "room": {"min": [-2, 0, -0.5], "max": [2, 2.7, 4]},
 "surfaces": [
  {"name": "Floor",      "min": [-2.1, -0.1, -0.6], "max": [2.1, 0, 4.1],   "color": [0.6, 0.6, 0.6]},
  {"name": "Ceiling",    "min": [-2.1, 2.7, -0.6],  "max": [2.1, 2.8, 4.1],  "color": [0.8, 0.8, 0.8]},
  {"name": "Left wall",  "min": [2, 0, -0.6],       "max": [2.1, 2.7, 4.1],  "color": [0.8, 0.05, 0.05]},
  {"name": "Right wall", "min": [-2.1, 0, -0.6],    "max": [-2, 2.7, 4.1],   "color": [0.05, 0.6, 0.05]},
  {"name": "Back wall",  "min": [-2, 0, 4],         "max": [2, 2.7, 4.1],    "color": [0.8, 0.8, 0.8]},
  {"name": "Front wall", "min": [-2, 0, -0.6],      "max": [2, 2.7, -0.5],   "color": [0.8, 0.8, 0.8]}
 ],
 "lights": [
  {"name": "Ceiling light 1", "min": [-0.4, 2.67, 1.6], "max": [0.4, 2.7, 2.4], "color": [1, 0.95, 0.85], "strength": 12, "surface": "ceiling"}
 ],
 "notes": ["The left wall is not in the photo; it is placed just beyond what the photo shows."],
 "photo": {"width": 1200, "height": 900, "hfov": 70, "fov_source": "assumed (a phone camera, 26 mm equivalent)"})json" + extra + "}";
}

}  // namespace

TEST(SceneRoomTest, ARoomFileBecomesBoxesALightAndACamera) {
	scene_room::Import room;
	std::string err;
	ASSERT_TRUE(scene_room::fromJson(roomJson(), room, err)) << err;
	ASSERT_EQ(room.surfaces.size(), 6u);
	ASSERT_EQ(room.lights.size(), 1u);
	const Object& left = room.surfaces[2];
	EXPECT_EQ(left.name, "Left wall");
	EXPECT_EQ(left.shape, ShapeKind::Box);
	EXPECT_NEAR(left.position.x, 2.05, 1e-9) << "the photo's left is +x in the Builder's right-handed world";
	EXPECT_NEAR(left.size.x, 0.1, 1e-9);
	EXPECT_NEAR(left.size.y, 2.7, 1e-9);
	EXPECT_NEAR(left.material.color.r, 0.8, 1e-9);
	EXPECT_TRUE(left.locked) << "a wall is not dragged by accident";
	EXPECT_FALSE(left.emissive);
	const Object& lamp = room.lights[0];
	EXPECT_TRUE(lamp.emissive);
	EXPECT_NEAR(lamp.emissionStrength, 12.0, 1e-9);
	EXPECT_NEAR(lamp.emission.b, 0.85, 1e-9);
	EXPECT_NEAR(room.camera.fov, 90.0, 1e-9);
	EXPECT_NEAR(room.camera.position.y, 1.4, 1e-9);
	EXPECT_EQ(room.width, 800);
	EXPECT_EQ(room.height, 600) << "the render has the photo's 4:3 proportions";
	EXPECT_EQ(room.notes.size(), 2u) << "the helper's note, and the one about the assumed lens";
}

TEST(SceneRoomTest, APortraitPhotoGivesATallPicture) {
	scene_room::Import room;
	std::string err;
	std::string text = roomJson();
	const size_t at = text.find("\"width\": 1200, \"height\": 900");
	ASSERT_NE(at, std::string::npos);
	text.replace(at, std::string("\"width\": 1200, \"height\": 900").size(), "\"width\": 900, \"height\": 1200");
	ASSERT_TRUE(scene_room::fromJson(text, room, err)) << err;
	EXPECT_EQ(room.width, 600);
	EXPECT_EQ(room.height, 800);
}

TEST(SceneRoomTest, AFileTheBuilderCannotUseIsRefusedWithAReason) {
	scene_room::Import room;
	std::string err;
	EXPECT_FALSE(scene_room::fromJson("not json", room, err));
	EXPECT_FALSE(err.empty());
	EXPECT_FALSE(scene_room::fromJson("[1, 2]", room, err));
	EXPECT_FALSE(scene_room::fromJson(R"({"room": {"min": [0,0,0], "max": [1,1,1]}, "surfaces": []})", room, err));
	EXPECT_NE(err.find("camera"), std::string::npos) << err;

	std::string noSurfaces = roomJson();
	noSurfaces.replace(noSurfaces.find("\"surfaces\": ["), 13, "\"surfaces\": [], \"unused\": [");
	EXPECT_FALSE(scene_room::fromJson(noSurfaces, room, err));
	EXPECT_NE(err.find("surfaces"), std::string::npos) << err;

	std::string flat = roomJson();
	flat.replace(flat.find("\"min\": [-2.1, -0.1, -0.6], \"max\": [2.1, 0, 4.1]"), 47, "\"min\": [-2.1, 0, -0.6], \"max\": [2.1, 0, 4.1]");
	EXPECT_FALSE(scene_room::fromJson(flat, room, err));
	EXPECT_NE(err.find("Floor"), std::string::npos) << "the message names the surface: " << err;

	std::string huge = roomJson();
	huge.replace(huge.find("\"max\": [2, 2.7, 4]"), 18, "\"max\": [2, 2.7, 4000]");
	EXPECT_FALSE(scene_room::fromJson(huge, room, err));
	EXPECT_NE(err.find("200"), std::string::npos) << err;

	std::string badFov = roomJson();
	badFov.replace(badFov.find("\"vfov\": 90"), 10, "\"vfov\": 400");
	EXPECT_FALSE(scene_room::fromJson(badFov, room, err));
}

TEST(SceneRoomTest, TheRoomReplacesTheSceneAndIsAValidDocumentThatSurvivesTheFile) {
	scene_room::Import room;
	std::string err;
	ASSERT_TRUE(scene_room::fromJson(roomJson(), room, err)) << err;
	Document d = makeStarterScene();
	ASSERT_FALSE(d.objects.empty());
	scene_room::applyTo(room, d, "Room from a photo");
	EXPECT_EQ(d.objects.size(), 7u) << "six surfaces and a lamp, nothing left of the starter scene";
	EXPECT_TRUE(d.lights.empty());
	EXPECT_EQ(d.title, "Room from a photo");
	EXPECT_EQ(d.render.width, 800);
	EXPECT_EQ(d.render.height, 600);
	EXPECT_FALSE(hasErrors(validate(d))) << "the room is a scene the Builder accepts";

	Document back;
	std::string perr;
	ASSERT_TRUE(fromJson(toJson(d), back, perr)) << perr;
	EXPECT_EQ(back.objects.size(), d.objects.size());
	EXPECT_EQ(toPbrt(back), toPbrt(d));
	const std::string text = toPbrt(d);
	const pbrt_load::LoadResult loaded = loadText(text, "room");
	EXPECT_TRUE(loaded.ok) << "the renderer loads what the Builder writes";
}

TEST(SceneRoomTest, ThePhotosBrightnessIsReadAndAClippedWindowCountsNoMoreThanABrightWall) {
	scene_room::Import room;
	std::string err;
	std::string text = roomJson();
	ASSERT_TRUE(scene_room::fromJson(text, room, err)) << err;
	EXPECT_EQ(room.photoBrightness, 0.0) << "a file without a brightness leaves the lights as the helper set them";
	text.replace(text.find("\"fov_source\""), 0, "\"brightness\": 0.31, ");
	ASSERT_TRUE(scene_room::fromJson(text, room, err)) << err;
	EXPECT_NEAR(room.photoBrightness, 0.31, 1e-12);

	const unsigned char white[6] = {255, 255, 255, 255, 255, 255};
	const unsigned char wall[6] = {230, 230, 230, 230, 230, 230};   // 0.902: the cap
	const unsigned char mixed[6] = {0, 0, 0, 255, 255, 255};
	EXPECT_NEAR(scene_room::pictureBrightness(white, 6), 0.9, 1e-12);
	EXPECT_NEAR(scene_room::pictureBrightness(wall, 6), 0.9, 1e-12);
	EXPECT_NEAR(scene_room::pictureBrightness(mixed, 6), 0.45, 1e-12);
	EXPECT_EQ(scene_room::pictureBrightness(white, 0), 0.0);
}

TEST(SceneRoomTest, TheLightsScaleAndStayWithinSaneLimits) {
	scene_room::Import room;
	std::string err;
	ASSERT_TRUE(scene_room::fromJson(roomJson(), room, err)) << err;
	scene_room::scaleLights(room, 2.0);
	EXPECT_NEAR(room.lights[0].emissionStrength, 24.0, 1e-9);
	scene_room::scaleLights(room, 1000.0);
	EXPECT_NEAR(room.lights[0].emissionStrength, 500.0, 1e-9);
	scene_room::scaleLights(room, 1e-9);
	EXPECT_NEAR(room.lights[0].emissionStrength, 0.5, 1e-9);
}

TEST(SceneRoomTest, TheLightSolverFindsTheFactorThatReachesTheTargetBrightness) {
	int evaluations = 0;
	// a picture that brightens with the square root of the light (the usual case)
	auto root = [&](double f) { ++evaluations; return 0.1 * std::sqrt(f); };
	double f = scene_room::solveLightFactor(root, 0.3);
	EXPECT_NEAR(0.1 * std::sqrt(f), 0.3, 0.3 * 0.05) << "factor " << f;
	EXPECT_LE(evaluations, 7);

	// one that saturates towards white (a bright room: more light helps less and less)
	auto saturating = [](double f) { return 0.9 * (1.0 - std::exp(-0.3 * f)); };
	f = scene_room::solveLightFactor(saturating, 0.5);
	EXPECT_NEAR(saturating(f), 0.5, 0.5 * 0.05) << "factor " << f;

	// one that is already right
	evaluations = 0;
	f = scene_room::solveLightFactor([&](double x) { ++evaluations; return 0.2 + 0.0 * x; }, 0.2);
	EXPECT_EQ(f, 1.0);
	EXPECT_EQ(evaluations, 1);

	// far too bright: turned down
	f = scene_room::solveLightFactor([](double x) { return 0.8 * std::pow(x, 0.5); }, 0.1);
	EXPECT_LT(f, 0.1);
}

TEST(SceneRoomTest, TheLightSolverGivesUpGracefully) {
	// the render cannot run: no change
	EXPECT_EQ(scene_room::solveLightFactor([](double) { return -1.0; }, 0.3), 1.0);
	// the render fails after a few steps: the best factor so far, not a jump
	int n = 0;
	const double f = scene_room::solveLightFactor([&](double x) { return ++n >= 3 ? -1.0 : 0.05 * std::sqrt(x); }, 0.5);
	EXPECT_GT(f, 1.0);
	EXPECT_LE(f, 300.0);
	// a target no light can reach: it stops within the evaluation limit, at a bounded factor
	n = 0;
	const double g = scene_room::solveLightFactor([&](double) { ++n; return 0.2; }, 0.5);
	EXPECT_LE(n, 7);
	EXPECT_LE(g, 300.0);
	EXPECT_GE(g, 0.02);
	// nonsense target
	EXPECT_EQ(scene_room::solveLightFactor([](double x) { return x; }, 0.0), 1.0);
}

TEST(SceneRoomTest, LeftStaysLeftTheLampLightsTheRoomAndTheCameraSeesIt) {
	const std::string launcher = findLauncher();
	if (launcher.empty()) GTEST_SKIP() << "ray_tracer.exe was not found next to the tests";
	scene_room::Import room;
	std::string err;
	ASSERT_TRUE(scene_room::fromJson(roomJson(), room, err)) << err;
	Document d = makeStarterScene();
	scene_room::applyTo(room, d, "Room");
	std::vector<float> rgb;
	int w = 0, h = 0;
	ASSERT_TRUE(renderWithLauncher(launcher, toPbrt(d), "room_lr", 48, 96, 5, rgb, w, h));
	ASSERT_GT(w, 0);
	auto block = [&](double x0, double x1, double y0, double y1, double out[3]) {
		out[0] = out[1] = out[2] = 0;
		int n = 0;
		for (int y = static_cast<int>(y0 * h); y < static_cast<int>(y1 * h); ++y)
			for (int x = static_cast<int>(x0 * w); x < static_cast<int>(x1 * w); ++x, ++n)
				for (int c = 0; c < 3; ++c) out[c] += rgb[(static_cast<size_t>(y) * w + x) * 3 + c];
		for (int c = 0; c < 3; ++c) out[c] /= std::max(n, 1);
	};
	double left[3], right[3], middle[3];
	block(0.0, 0.18, 0.3, 0.7, left);
	block(0.82, 1.0, 0.3, 0.7, right);
	block(0.4, 0.6, 0.3, 0.7, middle);
	EXPECT_GT(left[0], 2.0 * left[1]) << "the red wall is on the left, as in the photo";
	EXPECT_GT(right[1], 2.0 * right[0]) << "the green wall is on the right";
	EXPECT_GT(middle[0] + middle[1] + middle[2], 0.05) << "the lamp lights the room";
}
