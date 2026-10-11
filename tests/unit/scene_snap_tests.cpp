// scene_snap_tests.cpp - src/shared/scene_snap.h: the boxes round objects and snapping a drag to the objects around it.
#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "../../src/shared/scene_snap.h"

using namespace scene_doc;

namespace {

Object box(const std::string& name, double x, double y, double z, double sx = 2.0, double sy = 1.0, double sz = 2.0) {
	Object o = makeObject(ShapeKind::Box, name);
	o.size = {sx, sy, sz};
	o.position = {x, y, z};
	return o;
}

const bool kXZ[3] = {true, false, true};
const bool kY[3] = {false, true, false};
const bool kAll[3] = {true, true, true};

}  // namespace

TEST(SceneSnap, TheBoxRoundASphereABoxAndAQuad) {
	Object s = makeObject(ShapeKind::Sphere, "S");
	s.radius = 0.5;
	s.position = {1, 2, 3};
	Aabb b = aabbOf(s);
	EXPECT_DOUBLE_EQ(b.lo.x, 0.5);
	EXPECT_DOUBLE_EQ(b.hi.y, 2.5);
	EXPECT_DOUBLE_EQ(b.centre(2), 3.0);
	b = aabbOf(box("B", 0, 0.5, 0, 2, 1, 4));
	EXPECT_DOUBLE_EQ(b.lo.x, -1.0);
	EXPECT_DOUBLE_EQ(b.hi.z, 2.0);
	EXPECT_DOUBLE_EQ(b.lo.y, 0.0);
	EXPECT_DOUBLE_EQ(b.hi.y, 1.0);
	Object q = makeObject(ShapeKind::Quad, "Q");
	q.size = {6, 1, 4};
	q.position = {0, 0, 0};
	b = aabbOf(q);
	EXPECT_DOUBLE_EQ(b.lo.y, 0.0);
	EXPECT_DOUBLE_EQ(b.hi.y, 0.0) << "a flat panel has no height";
	EXPECT_DOUBLE_EQ(b.hi.x, 3.0);
}

TEST(SceneSnap, ATurnedBoxHasTheBoxRoundTheTurnedBox) {
	Object o = box("B", 0, 0, 0, 2, 2, 2);
	o.rotation = {0, 45, 0};
	const Aabb b = aabbOf(o);
	EXPECT_NEAR(b.hi.x, std::sqrt(2.0), 1e-9);
	EXPECT_NEAR(b.hi.z, std::sqrt(2.0), 1e-9);
	EXPECT_NEAR(b.hi.y, 1.0, 1e-9);
	o.rotation = {0, 90, 0};
	o.size = {4, 1, 2};
	const Aabb c = aabbOf(o);
	EXPECT_NEAR(c.hi.x, 1.0, 1e-9) << "a long box turned a quarter lies across";
	EXPECT_NEAR(c.hi.z, 2.0, 1e-9);
}

TEST(SceneSnap, AGeneratedShapeUsesItsTriangles) {
	Object p = makeObject(ShapeKind::Pyramid, "P");
	p.size = {2, 1, 2};
	p.height = 3;
	p.position = {5, 1.5, 0};
	const Aabb b = aabbOf(p);
	EXPECT_NEAR(b.lo.y, 0.0, 1e-9);
	EXPECT_NEAR(b.hi.y, 3.0, 1e-9);
	EXPECT_NEAR(b.lo.x, 4.0, 1e-9);
	EXPECT_NEAR(b.hi.x, 6.0, 1e-9);
	Object m = makeObject(ShapeKind::Mesh, "M");
	m.position = {1, 1, 1};
	const Aabb point = aabbOf(m);
	EXPECT_TRUE(point.valid);
	EXPECT_DOUBLE_EQ(point.lo.x, point.hi.x) << "a mesh file's size is not known: a point";
}

TEST(SceneSnap, ABoxRestsOnTopOfAnotherWhenItIsCloseEnough) {
	Document d;
	d.objects = {box("Table", 0, 0.5, 0), box("Cup", 5, 3.0, 0, 1, 1, 1)};   // the table top is at y 1; the cup's bottom is at 2.5
	// Dragged down to 0.18 above the table top, within a tolerance of 0.25.
	const SnapResult r = snapMove(d, {1}, {0, -1.32, 0}, kY, 0.25);   // asks for y 1.68 -> bottom 1.18
	ASSERT_EQ(r.guides.size(), 1u);
	EXPECT_NEAR(3.0 + r.delta.y, 1.5, 1e-9) << "the cup's bottom is on the table top (cup centre 1.5)";
	EXPECT_EQ(r.guides[0].axis, 1);
	EXPECT_EQ(r.guides[0].object, 0);
	EXPECT_EQ(r.guides[0].mine, 0);
	EXPECT_EQ(r.guides[0].theirs, 2);
	EXPECT_NEAR(r.guides[0].value, 1.0, 1e-9);
	// Too far: it stays where it was asked.
	const SnapResult far = snapMove(d, {1}, {0, -1.0, 0}, kY, 0.25);   // bottom at 1.5: 0.5 above
	EXPECT_TRUE(far.guides.empty());
	EXPECT_DOUBLE_EQ(far.delta.y, -1.0);
}

TEST(SceneSnap, TouchingSidesAndLinedUpMiddles) {
	Document d;
	d.objects = {box("Wall", 0, 0.5, 0, 4, 1, 3), box("Block", 9, 0.5, 9, 1, 1, 1)};   // the wall spans x -2..2 and z -1.5..1.5
	// The block dragged so its right side is 0.1 short of the wall's left side (x -2): its centre would be -2.6.
	const SnapResult touch = snapMove(d, {1}, {-11.6, 0, 0}, kXZ, 0.25);
	ASSERT_FALSE(touch.guides.empty());
	EXPECT_EQ(touch.guides[0].axis, 0);
	EXPECT_EQ(touch.guides[0].mine, 2);
	EXPECT_EQ(touch.guides[0].theirs, 0);
	EXPECT_NEAR(9.0 + touch.delta.x, -2.5, 1e-9) << "the block's right side now touches the wall's left side";
	// The block's middle 0.1 from the wall's middle in z (the wall is 3 deep, the block 1: only the middles are close).
	const SnapResult mid = snapMove(d, {1}, {0, 0, -8.9}, kXZ, 0.25);
	ASSERT_EQ(mid.guides.size(), 1u);
	EXPECT_EQ(mid.guides[0].axis, 2);
	EXPECT_EQ(mid.guides[0].mine, 1);
	EXPECT_EQ(mid.guides[0].theirs, 1);
	EXPECT_NEAR(9.0 + mid.delta.z, 0.0, 1e-9) << "its centre z is the wall's centre z";
	EXPECT_DOUBLE_EQ(mid.delta.x, 0.0) << "x was asked to stay: the block is far from everything in x";
}

TEST(SceneSnap, TheNearestMatchWinsAndATouchBeatsATie) {
	Document d;
	d.objects = {box("A", 0, 0.5, 0, 2, 1, 2), box("B", 0.4, 0.5, 5, 2, 1, 2), box("Mover", 10, 0.5, 5, 2, 1, 2)};
	// Moving to x about 0.3: B's middle (0.4) is 0.1 away, A's middle (0) is 0.3 away: B wins.
	const SnapResult r = snapMove(d, {2}, {0.3 - 10.0, 0, 0}, kXZ, 0.35);
	ASSERT_FALSE(r.guides.empty());
	bool onB = false;
	for (const SnapGuide& g : r.guides) onB = onB || (g.axis == 0 && g.object == 1);
	EXPECT_TRUE(onB);
	EXPECT_NEAR(10.0 + r.delta.x, 0.4, 1e-9);
}

TEST(SceneSnap, OnlyTheAxesTheDragChangesCanSnap) {
	Document d;
	d.objects = {box("Table", 0, 0.5, 0), box("Cup", 0.05, 1.6, 0.05, 1, 1, 1)};
	const SnapResult r = snapMove(d, {1}, {0, 0, 0}, kXZ, 0.25);
	for (const SnapGuide& g : r.guides) EXPECT_NE(g.axis, 1) << "height was not part of the drag";
	EXPECT_DOUBLE_EQ(r.delta.y, 0.0);
	const SnapResult all = snapMove(d, {1}, {0, 0, 0}, kAll, 0.25);
	bool y = false;
	for (const SnapGuide& g : all.guides) y = y || g.axis == 1;
	EXPECT_TRUE(y) << "with height in the drag, the cup at 1.1 above the floor... rests on the table top";
}

TEST(SceneSnap, TheFloorIsATargetForTheLowestPoint) {
	Document d;
	d.objects = {box("Crate", 0, 2.0, 0, 2, 2, 2)};   // floating: bottom at y 1
	const SnapResult r = snapMove(d, {0}, {0, -0.9, 0}, kY, 0.2);   // bottom would be 0.1 above the floor
	ASSERT_EQ(r.guides.size(), 1u);
	EXPECT_EQ(r.guides[0].object, -1);
	EXPECT_NEAR(2.0 + r.delta.y, 1.0, 1e-9) << "bottom on the floor: centre at 1";
	const SnapResult off = snapMove(d, {0}, {0, -0.9, 0}, kY, 0.2, /*floorToo=*/false);
	EXPECT_TRUE(off.guides.empty());
}

TEST(SceneSnap, ASetMovesAsOneBoxAndNeverSnapsToItself) {
	Document d;
	d.objects = {box("Left", 0, 0.5, 0, 1, 1, 1), box("Right", 3, 0.5, 0, 1, 1, 1), box("Post", 10, 0.5, 0, 1, 1, 1)};
	// The pair (Left, Right) spans x -0.5..3.5; dragged so its right side nears the post's left side (9.5): touching.
	const SnapResult r = snapMove(d, {0, 1}, {5.8, 0, 0}, kXZ, 0.3);   // right side would be 9.3
	ASSERT_FALSE(r.guides.empty());
	EXPECT_EQ(r.guides[0].object, 2) << "the set's own members are never targets";
	EXPECT_NEAR(3.5 + r.delta.x, 9.5, 1e-9);
	// Nothing moving, no tolerance: nothing happens.
	EXPECT_TRUE(snapMove(d, {}, {1, 0, 0}, kXZ, 0.3).guides.empty());
	EXPECT_TRUE(snapMove(d, {0}, {1, 0, 0}, kXZ, 0.0).guides.empty());
}
