/**
 * @file live_object_edit_tests.cpp
 * @brief Unit tests for picking and moving the objects of a flattened pbrt scene (src/shared/live_object_edit.h), the Live Preview's object editing.
 */

#include <gtest/gtest.h>

#include "live_object_edit.h"
#include "pbrt_flatten.h"
#include "pbrt_scene.h"

#include <cmath>
#include <string>

using namespace live_objects;

namespace {

pbrt_flatten::FlatScene flattenText(const std::string &text) {
	const pbrt_scene::ParseResult r = pbrt_scene::parse(text);
	EXPECT_TRUE(r.ok) << r.error;
	return pbrt_flatten::flatten(r.scene, {});
}

// A sphere at the origin, a quad on the floor (z = 0, x and y from 3 to 5), a cylinder along z at x = -4.
const char *kScene =
	"WorldBegin\n"
	"Shape \"sphere\" \"float radius\" [ 1 ]\n"
	"Shape \"trianglemesh\" \"integer indices\" [ 0 1 2  0 2 3 ] \"point3 P\" [ 3 3 0  5 3 0  5 5 0  3 5 0 ]\n"
	"AttributeBegin\n  Translate -4 0 0\n  Shape \"cylinder\" \"float radius\" [ 0.5 ] \"float zmin\" [ 0 ] \"float zmax\" [ 2 ]\nAttributeEnd\n";

}  // namespace

TEST(LiveObjectEditTest, PickFindsTheShapeASurfacePointBelongsTo) {
	const pbrt_flatten::FlatScene s = flattenText(kScene);
	ASSERT_EQ(objectsOf(s).size(), 3u);
	const PickIndex index = buildPickIndex(s, objectsOf(s));
	const double onSphere[3] = {0, 0, 1}, onQuad[3] = {4, 4, 0}, onCylinder[3] = {-3.5, 0, 1};
	EXPECT_EQ(pick(index, onSphere, 0.01).object, 0);
	EXPECT_EQ(pick(index, onQuad, 0.01).object, 1);
	EXPECT_EQ(pick(index, onCylinder, 0.05).object, 2);
}

TEST(LiveObjectEditTest, PickMissesEmptySpace) {
	const pbrt_flatten::FlatScene s = flattenText(kScene);
	const PickIndex index = buildPickIndex(s, objectsOf(s));
	const double far[3] = {20, 20, 20}, nearButOff[3] = {0, 0, 1.5};
	EXPECT_EQ(pick(index, far, 0.05).object, -1);
	EXPECT_EQ(pick(index, nearButOff, 0.05).object, -1) << "0.5 above the sphere is not on it";
}

TEST(LiveObjectEditTest, MovingAShapeMovesItsPickAndLeavesTheOthers) {
	pbrt_flatten::FlatScene s = flattenText(kScene);
	const double d[3] = {0, 0, 10};
	for (std::size_t i = 0; i < objectsOf(s).size(); ++i) {
		pbrt_flatten::FlatScene m = s;
		ASSERT_TRUE(translateObject(m, objectsOf(m), i, d));
		const PickIndex before = buildPickIndex(s, objectsOf(s)), after = buildPickIndex(m, objectsOf(m));
		for (std::size_t k = 0; k < before.shapes.size(); ++k) {
			const double shift = (k == i) ? 10.0 : 0.0;
			EXPECT_NEAR(after.shapes[k].lo[2], before.shapes[k].lo[2] + shift, 1e-9) << "moved " << i << " looked at " << k;
			EXPECT_NEAR(after.shapes[k].hi[2], before.shapes[k].hi[2] + shift, 1e-9);
			EXPECT_NEAR(after.shapes[k].lo[0], before.shapes[k].lo[0], 1e-9);
		}
	}
}

TEST(LiveObjectEditTest, MovedSphereIsPickedAtItsNewPlace) {
	pbrt_flatten::FlatScene s = flattenText(kScene);
	const double d[3] = {0, 0, 10};
	ASSERT_TRUE(translateObject(s, objectsOf(s), 0, d));
	const PickIndex index = buildPickIndex(s, objectsOf(s));
	const double oldTop[3] = {0, 0, 1}, newTop[3] = {0, 0, 11};
	EXPECT_EQ(pick(index, oldTop, 0.01).object, -1);
	EXPECT_EQ(pick(index, newTop, 0.01).object, 0);
}

TEST(LiveObjectEditTest, TranslateRejectsAnIndexOutOfRange) {
	pbrt_flatten::FlatScene s = flattenText(kScene);
	const double d[3] = {1, 0, 0};
	EXPECT_FALSE(translateObject(s, objectsOf(s), 99, d));
}

TEST(LiveObjectEditTest, ASmallObjectOnABigOneIsPickedNotTheBigOne) {
	// A ball resting on a floor: a point on the ball's surface where it touches the floor is on both; the smaller shape wins.
	const pbrt_flatten::FlatScene s = flattenText(
		"WorldBegin\n"
		"Shape \"trianglemesh\" \"integer indices\" [ 0 1 2  0 2 3 ] \"point3 P\" [ -10 -10 0  10 -10 0  10 10 0  -10 10 0 ]\n"
		"Translate 0 0 1\nShape \"sphere\" \"float radius\" [ 1 ]\n");
	const PickIndex index = buildPickIndex(s, objectsOf(s));
	const double touch[3] = {0, 0, 0};
	EXPECT_EQ(pick(index, touch, 0.01).object, 1);
}

TEST(LiveObjectEditTest, ShapesOfOneAttributeBlockAreOneObject) {
	// A box written as two quads in a block is one object; a second block is another; a bare shape outside any block is its own.
	pbrt_flatten::FlatScene s = flattenText(
		"WorldBegin\n"
		"AttributeBegin\n"
		"  Shape \"trianglemesh\" \"integer indices\" [ 0 1 2  0 2 3 ] \"point3 P\" [ 0 0 0  1 0 0  1 1 0  0 1 0 ]\n"
		"  Shape \"trianglemesh\" \"integer indices\" [ 0 1 2  0 2 3 ] \"point3 P\" [ 0 0 1  1 0 1  1 1 1  0 1 1 ]\n"
		"AttributeEnd\n"
		"AttributeBegin\n  Translate 5 0 0\n  Shape \"sphere\" \"float radius\" [ 1 ]\nAttributeEnd\n"
		"Shape \"sphere\" \"float radius\" [ 1 ]\n");
	const ObjectList objects = objectsOf(s);
	ASSERT_EQ(objects.size(), 3u);
	EXPECT_EQ(objects[0].size(), 2u);
	EXPECT_EQ(objects[1].size(), 1u);
	EXPECT_EQ(objects[2].size(), 1u);
	// Moving the first moves both quads (z of every vertex), and nothing else.
	const double d[3] = {0, 0, 7};
	ASSERT_TRUE(translateObject(s, objects, 0, d));
	for (const pbrt_flatten::Triangle &t : s.triangles)
		for (int v = 0; v < 3; ++v) EXPECT_GE(t.v[v * 3 + 2], 7.0);
	EXPECT_NEAR(s.spheres[0].center[2], 0.0, 1e-12);
	EXPECT_NEAR(s.spheres[1].center[2], 0.0, 1e-12);
}

TEST(LiveObjectEditTest, TheNestedBlockWinsOverItsParent) {
	const pbrt_flatten::FlatScene s = flattenText(
		"WorldBegin\n"
		"AttributeBegin\n"
		"  Shape \"sphere\" \"float radius\" [ 1 ]\n"
		"  AttributeBegin\n    Translate 4 0 0\n    Shape \"sphere\" \"float radius\" [ 1 ]\n  AttributeEnd\n"
		"  Shape \"sphere\" \"float radius\" [ 1 ]\n"
		"AttributeEnd\n");
	const ObjectList objects = objectsOf(s);
	ASSERT_EQ(objects.size(), 2u) << "the outer block's two spheres (not adjacent in the file) are one object, the nested block's sphere another";
	EXPECT_EQ(objects[0].size(), 2u);
}

// A sphere defined once and placed twice (at x = -5 and x = 5, the second twice as big), plus a quad on the floor.
const char *kInstances =
	"WorldBegin\n"
	"ObjectBegin \"ball\"\n  Shape \"sphere\" \"float radius\" [ 1 ]\nObjectEnd\n"
	"AttributeBegin\n  Translate -5 0 0\n  ObjectInstance \"ball\"\nAttributeEnd\n"
	"AttributeBegin\n  Translate 5 0 0\n  Scale 2 2 2\n  ObjectInstance \"ball\"\nAttributeEnd\n"
	"Shape \"trianglemesh\" \"integer indices\" [ 0 1 2  0 2 3 ] \"point3 P\" [ 0 3 0  2 3 0  2 5 0  0 5 0 ]\n";

TEST(LiveObjectEditTest, AnObjectInstanceIsAnObjectOfItsOwn) {
	const pbrt_flatten::FlatScene s = flattenText(kInstances);
	const ObjectList objects = objectsOf(s);
	ASSERT_EQ(objects.size(), 3u);   // the quad (a Shape, listed first), then the two placements
	const PickIndex index = buildPickIndex(s, objects);
	const double onFirst[3] = {-5, 0, 1}, onSecond[3] = {5, 0, 2}, onQuad[3] = {1, 4, 0};
	const PickResult a = pick(index, onFirst, 0.01), b = pick(index, onSecond, 0.01), q = pick(index, onQuad, 0.01);
	ASSERT_GE(a.object, 0);
	ASSERT_GE(b.object, 0);
	ASSERT_GE(q.object, 0);
	EXPECT_NE(a.object, b.object);
	EXPECT_NE(a.object, q.object);
	EXPECT_NE(b.object, q.object);
	// the second placement is twice as big: a point on its far side is on its surface, not the first's
	const double farSide[3] = {5, 0, -2};
	EXPECT_EQ(pick(index, farSide, 0.01).object, b.object);
}

TEST(LiveObjectEditTest, MovingAnInstanceChangesItsTransformAndNothingElse) {
	pbrt_flatten::FlatScene s = flattenText(kInstances);
	const ObjectList objects = objectsOf(s);
	const PickIndex before = buildPickIndex(s, objects);
	const double onSecond[3] = {5, 0, 2};
	const int second = pick(before, onSecond, 0.01).object;
	ASSERT_GE(second, 0);
	const pbrt_flatten::FlatScene original = s;
	const double d[3] = {3, 1, -2};
	ASSERT_TRUE(translateObject(s, objects, static_cast<std::size_t>(second), d));
	// the shared geometry is where it was, the other placement is where it was, the moved one's translation column moved by d
	EXPECT_DOUBLE_EQ(s.groups[0].spheres[0].center[0], original.groups[0].spheres[0].center[0]);
	int moved = 0;
	for (std::size_t i = 0; i < s.instances.size(); ++i) {
		const bool same = s.instances[i].xform[3] == original.instances[i].xform[3] && s.instances[i].xform[7] == original.instances[i].xform[7];
		if (!same) {
			++moved;
			EXPECT_DOUBLE_EQ(s.instances[i].xform[3], original.instances[i].xform[3] + 3);
			EXPECT_DOUBLE_EQ(s.instances[i].xform[7], original.instances[i].xform[7] + 1);
			EXPECT_DOUBLE_EQ(s.instances[i].xform[11], original.instances[i].xform[11] - 2);
		}
	}
	EXPECT_EQ(moved, 1);
	// and it is picked at its new place
	const PickIndex after = buildPickIndex(s, objects);
	const double onNewSurface[3] = {8, 1, -4};   // the old centre (5, 0, 0) moved by d is (8, 1, -2); the surface 2 (the doubled radius) further down z
	EXPECT_EQ(pick(after, onNewSurface, 0.01).object, second);
}

TEST(LiveObjectEditTest, AnInstanceWithAnEmitterInItsDefinitionIsNotAnObject) {
	const pbrt_flatten::FlatScene s = flattenText(
		"WorldBegin\nObjectBegin \"lamp\"\n  AreaLightSource \"diffuse\" \"rgb L\" [ 1 1 1 ]\n  Shape \"sphere\" \"float radius\" [ 1 ]\nObjectEnd\n"
		"AttributeBegin\n  ObjectInstance \"lamp\"\nAttributeEnd\n");
	for (const pbrt_flatten::ShapeRange& r : s.shapeRanges) EXPECT_LT(r.instance, 0);
}
