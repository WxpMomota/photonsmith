// scene_report_tests.cpp -- the "how faithful is this scene?" report (src/shared/scene_report.h): the grouping of warnings, the reference-pack lookup and the page.
#include <gtest/gtest.h>

#include "scene_report.h"
#include "pbrt_scene.h"

namespace {
pbrt_scene::Warning warn(const std::string &m) { return {0, std::string(), m}; }
}  // namespace

TEST(SceneReportTest, TheSameComplaintAboutManyMaterialsIsOneKind) {
	const auto groups = scene_report::groupWarnings({
		warn("material 'a' binds its 'roughness' to a texture ('r1', a 'imagemap' texture), which is not supported; the texture's average value is used instead"),
		warn("material 'b' binds its 'roughness' to a texture ('r2', a 'imagemap' texture), which is not supported; the texture's average value is used instead"),
		warn("light source 'x' is not supported and was dropped; the scene will be darker than intended"),
		warn("material 'c' binds its 'roughness' to a texture ('r3', a 'imagemap' texture), which is not supported; the texture's average value is used instead"),
	});
	ASSERT_EQ(groups.size(), 2u);
	EXPECT_EQ(groups[0].count, 3) << "the most frequent first";
	EXPECT_NE(groups[0].example.find("'a'"), std::string::npos) << "with the first one as the example";
	EXPECT_EQ(groups[1].count, 1);
}

TEST(SceneReportTest, NumbersAndQuotedNamesAreTakenOutOfAKind) {
	EXPECT_EQ(scene_report::kindOf("218 degenerate triangle(s) in a mesh 'geometry/m.ply'"), "# degenerate triangle(s) in a mesh '..'");
	EXPECT_EQ(scene_report::kindOf("a \"float iso\" of 12.5"), "a \"..\" of #");
}

TEST(SceneReportTest, TheReferencePackRowIsFoundByTheFileStem) {
	const std::string table =
		"# comment\n"
		"name test_spp depth size r g b noise cpu cpu_tol rec rec_tol wf wf_tol\n"
		"portal-light 64 8 64 0.07 0.07 0.08 0.0000 1.0002 0.0250 1.0003 0.0150 1.0001 0.0150 0.0002\n"
		"colored-quads 64 8 64 0.66 0.71 0.82 0.0002 0.9982 0.0250 0.9983 0.0150 0.9995 0.0150 0.0017\n";
	const scene_report::Pin pin = scene_report::pinFor(table, "colored-quads");
	ASSERT_TRUE(pin.found);
	EXPECT_NEAR(pin.cpu, 0.9982, 1e-9);
	EXPECT_NEAR(pin.wavefront, 0.9995, 1e-9);
	EXPECT_FALSE(scene_report::pinFor(table, "nope").found);
	EXPECT_EQ(scene_report::stemOf("C:\\scenes\\a\\colored-quads.pbrt"), "colored-quads");
	EXPECT_EQ(scene_report::stemOf("kroken/camera-1.pbrt"), "camera-1");
}

TEST(SceneReportTest, ACleanSceneSaysSoAndAnApproximatedOneListsWhatIsNotAsWritten) {
	pbrt_flatten::FlatScene scene;
	std::string text = scene_report::format("clean.pbrt", scene);
	EXPECT_NE(text.find("drawn as written"), std::string::npos) << text;
	scene.filmImagingRatio = 1.5;
	scene.warnings.push_back(warn("material 'x' binds its 'roughness' to a texture ('r', a 'imagemap' texture), which is not supported; the texture's average value is used instead"));
	text = scene_report::format("rough.pbrt", scene);
	EXPECT_NE(text.find("imaging ratio (shutter x iso / 100) 1.5"), std::string::npos) << text;
	EXPECT_NE(text.find("1 x material '..' binds its '..' to a texture"), std::string::npos) << text;
	EXPECT_NE(text.find("e.g. material 'x'"), std::string::npos) << text;
}
