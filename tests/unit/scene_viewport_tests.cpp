// scene_viewport_tests.cpp -- what a change means for the Scene Builder's rendered viewport (src/shared/scene_viewport.h).
#include <gtest/gtest.h>

#include "../../src/shared/scene_arrange.h"
#include "../../src/shared/scene_viewport.h"

using namespace scene_viewport;
using scene_doc::Document;
using scene_doc::makeStarterScene;

namespace {
Pose somePose() {
	Pose p;
	p.eye[0] = 1; p.eye[1] = 2; p.eye[2] = 3;
	p.target[0] = 0; p.target[1] = 1; p.target[2] = 0;
	p.fovDeg = 40;
	p.width = 640;
	p.height = 360;
	return p;
}
}  // namespace

TEST(SceneViewportTest, AMovedEyeOrTargetIsACameraChangeAndNothingIsNothing) {
	const Pose a = somePose();
	EXPECT_EQ(classify(a, a), Change::None);
	Pose b = a;
	b.eye[2] += 0.5;
	EXPECT_EQ(classify(a, b), Change::Camera);
	Pose c = a;
	c.target[0] -= 0.1;
	EXPECT_EQ(classify(a, c), Change::Camera);
}

TEST(SceneViewportTest, ASizeOrFieldOfViewChangeRestartsEvenWhenTheEyeMovedToo) {
	const Pose a = somePose();
	Pose b = a;
	b.width = 700;
	EXPECT_EQ(classify(a, b), Change::Restart);
	Pose c = a;
	c.height = 361;
	EXPECT_EQ(classify(a, c), Change::Restart);
	Pose d = a;
	d.fovDeg = 55;
	d.eye[0] = 9;
	EXPECT_EQ(classify(a, d), Change::Restart) << "the larger change wins";
}

TEST(SceneViewportTest, TheRenderSizeKeepsTheWidgetsShapeAndIsCapped) {
	int w = 0, h = 0;
	renderSize(800, 600, 960, w, h);
	EXPECT_EQ(w, 800);
	EXPECT_EQ(h, 600);
	renderSize(1920, 1080, 960, w, h);
	EXPECT_EQ(w, 960);
	EXPECT_EQ(h, 540) << "half the size, the same shape";
	renderSize(3000, 600, 960, w, h);
	EXPECT_EQ(w, 960);
	EXPECT_EQ(h, 192);
	renderSize(5, 5, 960, w, h);
	EXPECT_GE(w, 16);
	EXPECT_GE(h, 16);
	renderSize(0, 0, 960, w, h);
	EXPECT_GE(w, 16);
}

TEST(SceneViewportTest, AnEditThatDoesNotChangeThePictureDoesNotChangeTheSignature) {
	const Document a = makeStarterScene();
	const std::string sig = sceneSignature(a);
	EXPECT_FALSE(sig.empty());
	Document b = a;
	b.title = "Another name";
	b.camera.position = {9, 9, 9};
	b.camera.fov = 70;
	b.render = {123, 45, 7, 3};
	b.cameraViews.push_back(scene_doc::CameraView{"View", a.camera, 0.0});
	b.objects[1].hidden = true;       // hidden in the views only: it still renders
	b.objects[2].locked = true;
	b.objects[3].name = "Renamed";
	EXPECT_EQ(sceneSignature(b), sig);
}

TEST(SceneViewportTest, AnEditThatChangesThePictureChangesTheSignature) {
	const Document a = makeStarterScene();
	const std::string sig = sceneSignature(a);
	Document moved = a;
	moved.objects[2].position.x += 0.5;
	EXPECT_NE(sceneSignature(moved), sig);
	Document recoloured = a;
	recoloured.objects[3].material.color = {0.1, 0.9, 0.2};
	EXPECT_NE(sceneSignature(recoloured), sig);
	Document lit = a;
	lit.lights[0].intensity *= 2.0;
	EXPECT_NE(sceneSignature(lit), sig);
	Document extra = a;
	extra.objects.push_back(scene_doc::makeVolume(scene_doc::VolumeKind::Fog, "Fog"));
	EXPECT_NE(sceneSignature(extra), sig);
	Document fewer = a;
	fewer.objects.pop_back();
	EXPECT_NE(sceneSignature(fewer), sig);
}

TEST(SceneViewportTest, MovingObjectsGivesEachOneItsOffsetFromTheCopy) {
	const Document base = makeStarterScene();
	Document now = base;
	now.objects[2].position.x += 0.5;
	now.objects[2].position.z -= 0.25;
	const auto offsets = translationOnly(base, now);
	ASSERT_EQ(offsets.size(), base.objects.size());
	EXPECT_NEAR(offsets[2][0], 0.5, 1e-12);
	EXPECT_NEAR(offsets[2][1], 0.0, 1e-12);
	EXPECT_NEAR(offsets[2][2], -0.25, 1e-12);
	EXPECT_EQ(offsets[1], (std::array<double, 3>{0, 0, 0}));
	const auto none = translationOnly(base, base);
	ASSERT_EQ(none.size(), base.objects.size()) << "nothing moved is still a move of zero, not a restart";
	for (const auto &o : none) EXPECT_EQ(o, (std::array<double, 3>{0, 0, 0}));
}

TEST(SceneViewportTest, AMoveWithAnotherChangeOrAnUntrustedObjectIsNotJustAMove) {
	const Document base = makeStarterScene();
	Document recoloured = base;
	recoloured.objects[2].position.x += 1.0;
	recoloured.objects[3].material.color = {0.1, 0.9, 0.2};
	EXPECT_TRUE(translationOnly(base, recoloured).empty());
	Document extra = base;
	extra.objects.push_back(base.objects[1]);
	EXPECT_TRUE(translationOnly(base, extra).empty()) << "a different number of objects changes the renderer's numbering";
	Document turned = base;
	turned.objects[2].rotation.y += 30.0;
	EXPECT_TRUE(translationOnly(base, turned).empty());
	Document fogged = base;
	fogged.objects.push_back(scene_doc::makeVolume(scene_doc::VolumeKind::Fog, "Fog"));
	Document fogMoved = fogged;
	fogMoved.objects.back().position.x += 1.0;
	EXPECT_TRUE(translationOnly(fogged, fogMoved).empty()) << "a volume restarts: its medium would stay where it was declared";
	Document coned = base;
	coned.objects[2].shape = scene_doc::ShapeKind::Cone;
	Document conedMoved = coned;
	conedMoved.objects[2].position.x += 1.0;
	EXPECT_TRUE(translationOnly(coned, conedMoved).empty());
}

TEST(SceneViewportTest, ALensChangeRestartsBecauseTheLensIsFixedWhenTheRenderStarts) {
	const Pose a = somePose();
	Pose b = a;
	b.lensRadius = 0.05;
	b.focusDistance = 6.0;
	EXPECT_EQ(classify(a, b), Change::Restart) << "depth of field turned on";
	Pose c = b;
	c.focusDistance = 8.0;
	EXPECT_EQ(classify(b, c), Change::Restart) << "a new focus distance";
	Pose d = b;
	d.lensRadius = 0.0;
	d.focusDistance = 8.0;
	EXPECT_EQ(classify(b, d), Change::Restart) << "depth of field turned off";
	Pose pinholeA = a, pinholeB = a;
	pinholeB.focusDistance = 20.0;   // (nothing is out of focus with no lens: the distance means nothing)
	EXPECT_EQ(classify(pinholeA, pinholeB), Change::None);
}

TEST(SceneViewportTest, MovingObjectsWithTheBaseSignatureGivesTheSameOffsetsAsWithout) {
	const Document a = makeStarterScene();
	Document now = a;
	now.objects[2].position.x += 0.5;
	EXPECT_EQ(translationOnly(a, sceneSignature(a), now), translationOnly(a, now));
	Document other = now;
	other.objects[3].material.color = {0.1, 0.9, 0.2};
	EXPECT_TRUE(translationOnly(a, sceneSignature(a), other).empty());
}

TEST(SceneViewportTest, AVolumeThatStaysPutDoesNotStopOtherObjectsFromMovingByOffset) {
	Document base = makeStarterScene();
	base.objects.push_back(scene_doc::makeVolume(scene_doc::VolumeKind::Fog, "Fog"));
	Document now = base;
	now.objects[2].position.x += 0.5;
	const auto offsets = translationOnly(base, now);
	ASSERT_EQ(offsets.size(), base.objects.size());
	EXPECT_NEAR(offsets[2][0], 0.5, 1e-12);
	now.objects.back().position.x += 1.0;
	EXPECT_TRUE(translationOnly(base, now).empty()) << "moving the volume itself restarts";
}

TEST(SceneViewportTest, TheSignatureIsTheDocumentsJsonWithoutWhatOnlyTheViewsUse) {
	Document a = makeStarterScene();
	a.objects.push_back(scene_doc::makeVolume(scene_doc::VolumeKind::Fog, "Fog"));
	const std::string sig = sceneSignature(a);
	EXPECT_FALSE(sig.empty());
	EXPECT_EQ(sig.find("\"title\":\"" + a.title), std::string::npos) << "the title is not part of it";
	Document b = a;
	b.objects[0].group = "a group";
	b.lights[0].name = "Another name";
	b.lights[0].hidden = true;
	EXPECT_EQ(sceneSignature(b), sig);
	Document c = a;
	c.objects[1].material.roughness += 0.1;
	EXPECT_NE(sceneSignature(c), sig);
	Document e = a;
	e.objects.back().volume.density *= 2.0;
	EXPECT_NE(sceneSignature(e), sig);
}
