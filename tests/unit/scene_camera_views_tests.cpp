// scene_camera_views_tests.cpp - src/shared/scene_camera_views.h: saved cameras of the Scene Builder (name, apply, update, rename, remove) and their JSON.
#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <string>

#include "../../src/shared/scene_camera_views.h"
#include "../../src/shared/scene_doc_diff.h"

using namespace scene_doc;

namespace {

Camera cameraAt(double x, double y, double z) {
	Camera c;
	c.position = {x, y, z};
	c.target = {0.0, 1.0, 0.0};
	return c;
}

}  // namespace

TEST(SceneCameraViews, SavingKeepsTheWholeCameraUnderANumberedName) {
	Document d;
	Camera c = cameraAt(4, 3, 2);
	c.fov = 55;
	c.lensRadius = 0.2;
	c.focusDistance = 5.5;
	EXPECT_EQ(saveCameraView(d, "Front", c), 0);
	EXPECT_EQ(saveCameraView(d, "Front", cameraAt(0, 1, 8)), 1);
	EXPECT_EQ(saveCameraView(d, "  ", cameraAt(0, 1, 9)), 2);
	ASSERT_EQ(d.cameraViews.size(), 3u);
	EXPECT_EQ(d.cameraViews[0].name, "Front");
	EXPECT_EQ(d.cameraViews[1].name, "Front 2");
	EXPECT_EQ(d.cameraViews[2].name, "View") << "a blank name becomes 'View'";
	EXPECT_DOUBLE_EQ(d.cameraViews[0].camera.fov, 55);
	EXPECT_DOUBLE_EQ(d.cameraViews[0].camera.lensRadius, 0.2);
	EXPECT_DOUBLE_EQ(d.cameraViews[0].camera.focusDistance, 5.5);
}

TEST(SceneCameraViews, NumberingContinuesFromTheStemNotAppended) {
	Document d;
	saveCameraView(d, "Front", cameraAt(0, 0, 1));
	saveCameraView(d, "Front 2", cameraAt(0, 0, 2));
	saveCameraView(d, "Front 2", cameraAt(0, 0, 3));
	EXPECT_EQ(d.cameraViews[2].name, "Front 3") << "not 'Front 2 2'";
}

TEST(SceneCameraViews, ApplyingPutsTheSavedCameraOnTheScene) {
	Document d;
	d.camera = cameraAt(0, 2.5, 8);
	saveCameraView(d, "Close", cameraAt(1, 1, 2));
	EXPECT_TRUE(applyCameraView(d, 0));
	EXPECT_DOUBLE_EQ(d.camera.position.x, 1);
	EXPECT_DOUBLE_EQ(d.camera.position.z, 2);
	EXPECT_FALSE(applyCameraView(d, 1));
	EXPECT_FALSE(applyCameraView(d, -1));
}

TEST(SceneCameraViews, UpdateRenameAndRemove) {
	Document d;
	saveCameraView(d, "A", cameraAt(1, 1, 1));
	saveCameraView(d, "B", cameraAt(2, 2, 2));
	EXPECT_TRUE(updateCameraView(d, 0, cameraAt(9, 9, 9)));
	EXPECT_DOUBLE_EQ(d.cameraViews[0].camera.position.x, 9);
	EXPECT_EQ(d.cameraViews[0].name, "A") << "update keeps the name";
	EXPECT_TRUE(renameCameraView(d, 0, "  Top  "));
	EXPECT_EQ(d.cameraViews[0].name, "Top");
	EXPECT_TRUE(renameCameraView(d, 1, "Top"));
	EXPECT_EQ(d.cameraViews[1].name, "Top 2") << "another view has that name";
	EXPECT_TRUE(renameCameraView(d, 0, "Top")) << "a view does not clash with its own name";
	EXPECT_EQ(d.cameraViews[0].name, "Top");
	EXPECT_FALSE(renameCameraView(d, 0, "   ")) << "a blank name is refused";
	EXPECT_EQ(d.cameraViews[0].name, "Top");
	EXPECT_TRUE(removeCameraView(d, 0));
	ASSERT_EQ(d.cameraViews.size(), 1u);
	EXPECT_EQ(d.cameraViews[0].name, "Top 2");
	EXPECT_FALSE(removeCameraView(d, 5));
}

TEST(SceneCameraViews, ThereIsALimit) {
	Document d;
	for (std::size_t i = 0; i < kMaxCameraViews; ++i) EXPECT_GE(saveCameraView(d, "V", cameraAt(0, 0, 1)), 0);
	EXPECT_EQ(saveCameraView(d, "One too many", cameraAt(0, 0, 1)), -1);
	EXPECT_EQ(d.cameraViews.size(), kMaxCameraViews);
}

TEST(SceneCameraViews, LookingFromKeepsTheLensAndFollowsTheFocusOnlyWithoutDepthOfField) {
	Camera base = cameraAt(0, 2.5, 8);
	base.fov = 33;
	base.up = {0, 1, 0};
	const Camera sharp = cameraLookingFrom(base, {0, 1, 6}, {0, 1, 0});
	EXPECT_DOUBLE_EQ(sharp.position.z, 6);
	EXPECT_DOUBLE_EQ(sharp.fov, 33);
	EXPECT_NEAR(sharp.focusDistance, 6.0, 1e-9) << "no depth of field: the focus follows what is looked at";
	base.lensRadius = 0.1;
	base.focusDistance = 3.0;
	const Camera blurry = cameraLookingFrom(base, {0, 1, 6}, {0, 1, 0});
	EXPECT_DOUBLE_EQ(blurry.focusDistance, 3.0) << "depth of field: the focus was set on purpose";
	EXPECT_DOUBLE_EQ(blurry.lensRadius, 0.1);
}

TEST(SceneCameraViews, ViewsSurviveTheJsonCopyAndOldFilesHaveNone) {
	Document d;
	Camera c = cameraAt(4, 3, 2);
	c.fov = 55;
	c.lensRadius = 0.2;
	saveCameraView(d, "Front", c);
	saveCameraView(d, "Back", cameraAt(0, 1, -8));
	Document back;
	std::string error;
	ASSERT_TRUE(fromJson(toJson(d), back, error)) << error;
	ASSERT_EQ(back.cameraViews.size(), 2u);
	EXPECT_EQ(back.cameraViews[0].name, "Front");
	EXPECT_DOUBLE_EQ(back.cameraViews[0].camera.fov, 55);
	EXPECT_DOUBLE_EQ(back.cameraViews[0].camera.lensRadius, 0.2);
	EXPECT_DOUBLE_EQ(back.cameraViews[1].camera.position.z, -8);
	EXPECT_EQ(toJson(back), toJson(d));
	Document plain;
	EXPECT_EQ(toJson(plain).find("cameraViews"), std::string::npos) << "no views: nothing written, so older files and new ones agree";
	Document fromOld;
	ASSERT_TRUE(fromJson(toJson(plain), fromOld, error)) << error;
	EXPECT_TRUE(fromOld.cameraViews.empty());
}

TEST(SceneCameraViews, TheLogSaysWhenAViewIsAddedOrChanged) {
	Document a, b;
	saveCameraView(b, "Front", cameraAt(0, 1, 8));
	EXPECT_NE(describeChange(a, b).find("camera view"), std::string::npos);
	Document c = b;
	updateCameraView(c, 0, cameraAt(5, 1, 8));
	EXPECT_NE(describeChange(b, c).find("Front"), std::string::npos);
}

TEST(SceneCameraViews, AViewCanHoldAFlythroughForSomeSeconds) {
	Document d;
	saveCameraView(d, "A", cameraAt(1, 1, 1));
	saveCameraView(d, "B", cameraAt(2, 2, 2));
	EXPECT_DOUBLE_EQ(d.cameraViews[0].hold, 0.0) << "no pause by default";
	EXPECT_TRUE(setCameraViewHold(d, 1, 1.5));
	EXPECT_DOUBLE_EQ(d.cameraViews[1].hold, 1.5);
	EXPECT_FALSE(setCameraViewHold(d, 1, -1.0));
	EXPECT_FALSE(setCameraViewHold(d, 1, 5000.0));
	EXPECT_FALSE(setCameraViewHold(d, 1, std::nan("")));
	EXPECT_FALSE(setCameraViewHold(d, 7, 1.0));
	EXPECT_DOUBLE_EQ(d.cameraViews[1].hold, 1.5) << "refused values change nothing";
	EXPECT_TRUE(updateCameraView(d, 1, cameraAt(9, 9, 9)));
	EXPECT_DOUBLE_EQ(d.cameraViews[1].hold, 1.5) << "updating a view's camera keeps its pause";
	EXPECT_TRUE(renameCameraView(d, 1, "Roof"));
	EXPECT_DOUBLE_EQ(d.cameraViews[1].hold, 1.5);
}

TEST(SceneCameraViews, TheHoldSurvivesTheJsonCopyAndNoHoldWritesNothing) {
	Document d;
	saveCameraView(d, "A", cameraAt(1, 1, 1));
	saveCameraView(d, "B", cameraAt(2, 2, 2));
	setCameraViewHold(d, 0, 2.5);
	Document back;
	std::string error;
	ASSERT_TRUE(fromJson(toJson(d), back, error)) << error;
	EXPECT_DOUBLE_EQ(back.cameraViews[0].hold, 2.5);
	EXPECT_DOUBLE_EQ(back.cameraViews[1].hold, 0.0);
	EXPECT_EQ(toJson(back), toJson(d));
	Document none;
	saveCameraView(none, "A", cameraAt(1, 1, 1));
	EXPECT_EQ(toJson(none).find("hold"), std::string::npos) << "older files and new ones without pauses agree";
	// A nonsense hold in a file is a pause of none, not a failure to open the scene.
	std::string text = toJson(d);
	text.replace(text.find("\"hold\":2.5"), 10, "\"hold\":-4");
	Document odd;
	ASSERT_TRUE(fromJson(text, odd, error)) << error;
	EXPECT_DOUBLE_EQ(odd.cameraViews[0].hold, 0.0);
}

TEST(SceneCameraViews, ChangingAHoldIsInTheLog) {
	Document a;
	saveCameraView(a, "Front", cameraAt(0, 1, 8));
	Document b = a;
	setCameraViewHold(b, 0, 2.0);
	EXPECT_NE(describeChange(a, b).find("hold"), std::string::npos);
}

TEST(SceneCameraViews, TheFlythroughPathIsTheViewsInOrderWithTheirOwnLensAndHold) {
	Document d;
	Camera a = cameraAt(0, 1, 8);
	a.fov = 40;
	Camera b = cameraAt(6, 2, 3);
	b.fov = 55;
	saveCameraView(d, "A", a);
	saveCameraView(d, "B", b);
	setCameraViewHold(d, 1, 1.5);
	const camera_keyframes::Path path = flythroughPath(d, false, false);
	ASSERT_EQ(path.keys.size(), 2u);
	EXPECT_FALSE(path.ease);
	EXPECT_DOUBLE_EQ(path.keys[0].pos[2], 8.0);
	EXPECT_DOUBLE_EQ(path.keys[1].pos[0], 6.0);
	EXPECT_DOUBLE_EQ(path.keys[1].fov, 55.0);
	EXPECT_DOUBLE_EQ(path.keys[1].hold, 1.5);
	EXPECT_EQ(flythroughPath(d, true, true).keys.size(), 3u) << "returning to the start repeats the first view";
	EXPECT_DOUBLE_EQ(flythroughPath(d, true, true).keys.back().pos[2], 8.0);
}

TEST(SceneCameraViews, FewerThanTwoViewsHaveNoFlythroughPathOrCurve) {
	Document d;
	EXPECT_TRUE(flythroughPath(d).keys.empty());
	saveCameraView(d, "Only", cameraAt(1, 1, 1));
	EXPECT_TRUE(flythroughPath(d).keys.empty());
	EXPECT_TRUE(flythroughCurve(flythroughPath(d)).empty());
}

TEST(SceneCameraViews, TheCurveStartsAndEndsOnTheViewsAndPassesThroughTheMiddleOne) {
	Document d;
	saveCameraView(d, "A", cameraAt(0, 1, 8));
	saveCameraView(d, "B", cameraAt(5, 3, 0));
	saveCameraView(d, "C", cameraAt(0, 1, -8));
	const std::vector<camera_keyframes::Key> curve = flythroughCurve(flythroughPath(d), 10);
	ASSERT_EQ(curve.size(), 2u * 10u + 1u);
	EXPECT_DOUBLE_EQ(curve.front().pos[2], 8.0);
	EXPECT_DOUBLE_EQ(curve.back().pos[2], -8.0);
	// The first sample of the second stretch is exactly the middle view: the curve goes through it, not past it.
	EXPECT_NEAR(curve[10].pos[0], 5.0, 1e-9);
	EXPECT_NEAR(curve[10].pos[1], 3.0, 1e-9);
	// A step count of zero or less still gives a curve (one step a stretch).
	EXPECT_EQ(flythroughCurve(flythroughPath(d), 0).size(), 3u);
}

TEST(SceneCameraViews, MovingAViewsEyeOrTargetKeepsItsLensAndFollowsTheFocus) {
	Document d;
	Camera c = cameraAt(0, 1, 8);
	c.target = {0, 1, 0};
	c.fov = 35;
	c.lensRadius = 0;
	saveCameraView(d, "Front", c);
	saveCameraView(d, "Side", cameraAt(8, 1, 0));
	ASSERT_TRUE(moveCameraViewEye(d, 0, {0, 1, 4}));
	const Camera& moved = d.cameraViews[0].camera;
	EXPECT_DOUBLE_EQ(moved.position.z, 4.0);
	EXPECT_DOUBLE_EQ(moved.target.z, 0.0);
	EXPECT_DOUBLE_EQ(moved.fov, 35.0);
	EXPECT_NEAR(moved.focusDistance, 4.0, 1e-9) << "with no depth of field the focus follows what is looked at";
	ASSERT_TRUE(moveCameraViewTarget(d, 0, {3, 1, 0}));
	EXPECT_DOUBLE_EQ(d.cameraViews[0].camera.target.x, 3.0);
	EXPECT_DOUBLE_EQ(d.cameraViews[0].camera.position.z, 4.0);
	EXPECT_NEAR(d.cameraViews[0].camera.focusDistance, 5.0, 1e-9);
	EXPECT_DOUBLE_EQ(d.cameraViews[1].camera.position.x, 8.0) << "the other views are untouched";
}

TEST(SceneCameraViews, ALensWithDepthOfFieldKeepsItsFocusWhenAMarkerMoves) {
	Document d;
	Camera c = cameraAt(0, 1, 8);
	c.lensRadius = 0.2;
	c.focusDistance = 6.5;
	saveCameraView(d, "Close", c);
	ASSERT_TRUE(moveCameraViewEye(d, 0, {1, 2, 3}));
	EXPECT_DOUBLE_EQ(d.cameraViews[0].camera.focusDistance, 6.5);
}

TEST(SceneCameraViews, MovingAMarkerRefusesABadIndexOrAPointThatIsNotFinite) {
	Document d;
	saveCameraView(d, "A", cameraAt(1, 1, 1));
	const Document before = d;
	EXPECT_FALSE(moveCameraViewEye(d, 1, {0, 0, 0}));
	EXPECT_FALSE(moveCameraViewEye(d, -1, {0, 0, 0}));
	EXPECT_FALSE(moveCameraViewTarget(d, 0, {std::nan(""), 0, 0}));
	EXPECT_FALSE(moveCameraViewEye(d, 0, {0, std::numeric_limits<double>::infinity(), 0}));
	EXPECT_EQ(toJson(d), toJson(before));
}

TEST(SceneCameraViews, TheScrubbedCameraStartsAtTheFirstViewEndsAtTheLastAndTakesEachViewsLens) {
	Document d;
	Camera a = cameraAt(0, 1, 8);
	a.fov = 30;
	a.lensRadius = 0.1;
	a.focusDistance = 8;
	Camera b = cameraAt(8, 1, 0);
	b.fov = 60;
	b.lensRadius = 0.3;
	b.focusDistance = 4;
	saveCameraView(d, "A", a);
	saveCameraView(d, "B", b);
	d.camera.up = {0, 1, 0};
	const Camera first = cameraAtFlythrough(d, 0.0), last = cameraAtFlythrough(d, 1.0), mid = cameraAtFlythrough(d, 0.5);
	EXPECT_DOUBLE_EQ(first.position.z, 8.0);
	EXPECT_DOUBLE_EQ(first.fov, 30.0);
	EXPECT_DOUBLE_EQ(last.position.x, 8.0);
	EXPECT_DOUBLE_EQ(last.fov, 60.0);
	EXPECT_DOUBLE_EQ(last.lensRadius, 0.3);
	EXPECT_DOUBLE_EQ(last.focusDistance, 4.0);
	EXPECT_GT(mid.fov, 30.0);
	EXPECT_LT(mid.fov, 60.0);
	EXPECT_GT(mid.position.x, 0.0);
	EXPECT_LT(mid.position.x, 8.0);
	EXPECT_EQ(mid.up.y, 1.0) << "the rest of the scene camera is kept";
}

TEST(SceneCameraViews, WithoutTwoViewsTheScrubbedCameraIsTheSceneCamera) {
	Document d;
	d.camera = cameraAt(3, 3, 3);
	EXPECT_DOUBLE_EQ(cameraAtFlythrough(d, 0.7).position.x, 3.0);
	saveCameraView(d, "Only", cameraAt(9, 9, 9));
	EXPECT_DOUBLE_EQ(cameraAtFlythrough(d, 0.7).position.x, 3.0);
}

TEST(SceneCameraViews, AHoldStandsStillWhileTheSliderMoves) {
	Document d;
	saveCameraView(d, "A", cameraAt(0, 1, 8));
	saveCameraView(d, "B", cameraAt(8, 1, 0));
	saveCameraView(d, "C", cameraAt(8, 1, -8));
	setCameraViewHold(d, 1, 4.0);   // 4 of the nominal 10 seconds at B
	const Camera before = cameraAtFlythrough(d, 0.45), after = cameraAtFlythrough(d, 0.55);
	EXPECT_NEAR(before.position.x, after.position.x, 1e-6);
	EXPECT_NEAR(before.position.z, after.position.z, 1e-6);
	EXPECT_DOUBLE_EQ(before.position.x, 8.0);
}
