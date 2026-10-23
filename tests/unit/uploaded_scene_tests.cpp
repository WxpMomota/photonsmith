// uploaded_scene_tests.cpp -- the rule for "the scene already on the GPU" (gpu/optix/uploaded_scene.h): which renderers an upload serves. Pure: no GPU.

#include <gtest/gtest.h>

#include "../../gpu/optix/uploaded_scene.h"

using optix_renderer::RendererKind;
using optix_renderer::UploadedScene;

TEST(UploadedScene, NothingUploadedServesNobodyNotEvenTheEmptyKey) {
	UploadedScene none;
	EXPECT_FALSE(none.serves("A1", RendererKind::Default));
	EXPECT_FALSE(none.serves("A1", RendererKind::Wavefront));
	EXPECT_FALSE(none.serves("", RendererKind::Wavefront)) << "an empty key is 'nothing', never a scene";
}

TEST(UploadedScene, AnUploadForTheWavefrontRendererOrSppmServesThemButNotTheDefaultRenderer) {
	UploadedScene up;
	up.record("A1", /*builtDefaultRendererSbt=*/false);
	EXPECT_TRUE(up.serves("A1", RendererKind::Wavefront));
	EXPECT_TRUE(up.serves("A1", RendererKind::Sppm));
	EXPECT_FALSE(up.serves("A1", RendererKind::Default)) << "no default-renderer shader binding table: it has to be uploaded again";
	EXPECT_FALSE(up.serves("B3", RendererKind::Wavefront)) << "another scene";
}

TEST(UploadedScene, AnUploadWithTheDefaultSbtServesEveryRenderer) {
	UploadedScene up;
	up.record("A1", /*builtDefaultRendererSbt=*/true);
	EXPECT_TRUE(up.serves("A1", RendererKind::Default));
	EXPECT_TRUE(up.serves("A1", RendererKind::Wavefront));
	EXPECT_TRUE(up.serves("A1", RendererKind::Sppm));
	EXPECT_FALSE(up.serves("A1|file|stamp", RendererKind::Default)) << "the same id but a file saved over since";
}

TEST(UploadedScene, AFreshUploadReplacesTheWholeStateAndClearForgetsIt) {
	UploadedScene up;
	up.record("A1", true);
	up.record("B3", false);
	EXPECT_FALSE(up.serves("B3", RendererKind::Default)) << "the SBT flag belongs to the latest upload";
	EXPECT_FALSE(up.serves("A1", RendererKind::Wavefront));
	up.clear();
	EXPECT_FALSE(up.serves("B3", RendererKind::Wavefront));
	EXPECT_FALSE(up.defaultRendererSbt);
}
