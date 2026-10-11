// scene_arrange_tests.cpp -- hiding, locking, dropping to the floor and measuring what is picked (src/shared/scene_arrange.h).
#include <gtest/gtest.h>

#include <cmath>
#include <string>

#include "../../src/shared/scene_arrange.h"
#include "../../src/shared/scene_doc_diff.h"

using namespace scene_doc;

namespace {

Document sceneOf(std::initializer_list<Object> objects) {
	Document d;
	for (const Object& o : objects) d.objects.push_back(o);
	return d;
}

Object ballAt(double x, double y, double z, double r = 1.0) {
	Object o = makeObject(ShapeKind::Sphere, "Ball");
	o.radius = r;
	o.position = {x, y, z};
	return o;
}

}  // namespace

TEST(SceneArrangeTest, HidingAndLockingChangeOnlyThePickedItemsAndSayHowManyChanged) {
	Document d = sceneOf({ballAt(0, 1, 0), ballAt(3, 1, 0), ballAt(6, 1, 0)});
	Light l;
	d.lights.push_back(l);
	const ItemSet pick = makeItemSet(d, {0, 2}, {0});
	EXPECT_EQ(setHidden(d, pick, true), 3);
	EXPECT_TRUE(d.objects[0].hidden);
	EXPECT_FALSE(d.objects[1].hidden);
	EXPECT_TRUE(d.objects[2].hidden);
	EXPECT_TRUE(d.lights[0].hidden);
	EXPECT_EQ(hiddenCount(d), 3);
	EXPECT_EQ(setHidden(d, pick, true), 0) << "already hidden: nothing changes";
	EXPECT_EQ(setLocked(d, makeItemSet(d, {1}), true), 1);
	EXPECT_EQ(lockedCount(d), 1);
	EXPECT_EQ(showAll(d), 3);
	EXPECT_EQ(hiddenCount(d), 0);
	EXPECT_EQ(lockedCount(d), 1) << "showing does not unlock";
	EXPECT_EQ(unlockAll(d), 1);
	EXPECT_EQ(lockedCount(d), 0);
}

TEST(SceneArrangeTest, HiddenAndLockedSurviveTheJsonCopyAndASceneWithoutThemWritesNothingNew) {
	Document d = makeStarterScene();
	EXPECT_EQ(toJson(d).find("hidden"), std::string::npos);
	EXPECT_EQ(toJson(d).find("locked"), std::string::npos);
	d.objects[1].hidden = true;
	d.objects[2].locked = true;
	d.lights[0].locked = true;
	Document back;
	std::string error;
	ASSERT_TRUE(fromJson(toJson(d), back, error)) << error;
	EXPECT_TRUE(back.objects[1].hidden);
	EXPECT_FALSE(back.objects[1].locked);
	EXPECT_TRUE(back.objects[2].locked);
	EXPECT_TRUE(back.lights[0].locked);
	EXPECT_EQ(toJson(back), toJson(d));
}

TEST(SceneArrangeTest, HiddenObjectsStillRenderSoThePbrtTextIsTheSame) {
	Document d = makeStarterScene();
	const std::string before = toPbrt(d);
	d.objects[1].hidden = true;
	d.objects[2].locked = true;
	// The only difference is the copy of the document the file carries (the hidden and locked flags in the JSON line): the scene itself is unchanged.
	std::string after = toPbrt(d);
	const auto drop = [](std::string s) { return s.substr(s.find("WorldBegin")); };
	EXPECT_EQ(drop(after), drop(before));
}

TEST(SceneArrangeTest, ChangingHiddenOrLockedIsInTheLog) {
	Document a = makeStarterScene();
	Document b = a;
	b.objects[1].hidden = true;
	EXPECT_NE(describeChange(a, b).find("hidden"), std::string::npos);
	Document c = a;
	c.objects[2].locked = true;
	EXPECT_NE(describeChange(a, c).find("locked"), std::string::npos);
}

TEST(SceneArrangeTest, DroppingToTheFloorMovesTheWholePickByTheGapUnderTheLowestObject) {
	Document d = sceneOf({ballAt(0, 3.0, 0, 1.0), ballAt(4, 5.0, 0, 0.5)});   // lows at 2.0 and 4.5
	EXPECT_TRUE(dropToFloor(d, makeItemSet(d, {0, 1})));
	EXPECT_NEAR(d.objects[0].position.y, 1.0, 1e-9);   // rests on the floor
	EXPECT_NEAR(d.objects[1].position.y, 3.0, 1e-9) << "the other came down by the same amount: they keep their places relative to each other";
	EXPECT_NEAR(d.objects[1].position.y - 0.5, 2.5, 1e-9);
	EXPECT_FALSE(dropToFloor(d, makeItemSet(d, {0, 1}))) << "already standing on it";
}

TEST(SceneArrangeTest, ABoxThatIsBelowTheFloorComesUpAndATurnedOneUsesItsTurnedBox) {
	Object box = makeObject(ShapeKind::Box, "Crate");
	box.size = {2, 2, 2};
	box.position = {0, -3, 0};
	Document d = sceneOf({box});
	ASSERT_TRUE(dropToFloor(d, makeItemSet(d, {0})));
	EXPECT_NEAR(d.objects[0].position.y, 1.0, 1e-9);
	Object tilted = makeObject(ShapeKind::Box, "Tilted");
	tilted.size = {2, 2, 2};
	tilted.rotation = {0, 0, 45};   // on its edge: half the diagonal, sqrt(2), is below its middle
	tilted.position = {0, 5, 0};
	Document e = sceneOf({tilted});
	ASSERT_TRUE(dropToFloor(e, makeItemSet(e, {0})));
	EXPECT_NEAR(e.objects[0].position.y, std::sqrt(2.0), 1e-9);
}

TEST(SceneArrangeTest, ALightIsMovedWithThePickAndAMeshAloneHasNothingToMeasure) {
	Object mesh = makeObject(ShapeKind::Mesh, "Mesh");
	mesh.position = {0, 4, 0};
	mesh.meshFile = "model.ply";
	Document d = sceneOf({ballAt(0, 3, 0, 1.0), mesh});
	Light lamp;
	lamp.position = {0, 6, 0};
	d.lights.push_back(lamp);
	ASSERT_TRUE(dropToFloor(d, makeItemSet(d, {0, 1}, {0})));
	EXPECT_NEAR(d.objects[0].position.y, 1.0, 1e-9);
	EXPECT_NEAR(d.objects[1].position.y, 2.0, 1e-9) << "the mesh comes down with the ball";
	EXPECT_NEAR(d.lights[0].position.y, 4.0, 1e-9);
	Document alone = sceneOf({mesh});
	EXPECT_FALSE(dropToFloor(alone, makeItemSet(alone, {0}))) << "a mesh's size is not known, so there is nothing to put on the floor";
	EXPECT_DOUBLE_EQ(alone.objects[0].position.y, 4.0);
}

TEST(SceneArrangeTest, TheBoundsOfThePickCoverEachObjectAndLight) {
	Document d = sceneOf({ballAt(0, 1, 0, 1.0), ballAt(10, 2, -4, 2.0)});
	Light lamp;
	lamp.position = {-5, 9, 3};
	d.lights.push_back(lamp);
	Float3 lo, hi;
	ASSERT_TRUE(boundsOf(d, makeItemSet(d, {0, 1}, {0}), lo, hi));
	EXPECT_NEAR(lo.x, -5.0, 1e-9);
	EXPECT_NEAR(hi.x, 12.0, 1e-9);
	EXPECT_NEAR(lo.y, 0.0, 1e-9);
	EXPECT_NEAR(hi.y, 9.0, 1e-9);
	EXPECT_NEAR(lo.z, -6.0, 1e-9);
	EXPECT_NEAR(hi.z, 3.0, 1e-9);
	ASSERT_TRUE(boundsOf(d, makeItemSet(d, {0}), lo, hi));
	EXPECT_NEAR(hi.x, 1.0, 1e-9);
	EXPECT_FALSE(boundsOf(d, ItemSet{}, lo, hi));
}
