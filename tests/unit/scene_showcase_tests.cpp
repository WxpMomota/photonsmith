// Tests for the Scene Builder's showcase scene (src/shared/scene_showcase.h, pbrt_scenes/studio-showcase.pbrt): that it is a valid Scene Builder document that uses what it says it
// uses, that the committed file is exactly what the code writes (so they cannot drift), and that the file opens in the Builder and is listed under its tags.
#include <gtest/gtest.h>

#include "../../src/shared/pbrt_discover.h"
#include "../../src/shared/pbrt_load.h"
#include "../../src/shared/scene_document.h"
#include "../../src/shared/scene_showcase.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace scene_doc;

namespace {

// pbrt_scenes/studio-showcase.pbrt, found from wherever the tests run (a few folders up from the build folder); empty if the checkout does not have it.
std::string findShowcaseFile() {
	namespace fs = std::filesystem;
	std::error_code ec;
	for (const char* c : {"pbrt_scenes/studio-showcase.pbrt", "../pbrt_scenes/studio-showcase.pbrt", "../../pbrt_scenes/studio-showcase.pbrt", "../../../pbrt_scenes/studio-showcase.pbrt",
	                      "../../../../pbrt_scenes/studio-showcase.pbrt"})
		if (fs::is_regular_file(c, ec)) return fs::absolute(c, ec).string();
	return "";
}

std::string readAll(const std::string& path) {
	std::ifstream in(path, std::ios::binary);
	std::ostringstream ss;
	ss << in.rdbuf();
	std::string text = ss.str();
	std::string out;
	for (char c : text)
		if (c != '\r') out.push_back(c);   // (a checkout may have changed the line endings)
	return out;
}

}  // namespace

// Not a check: writes the file after a change to the scene (RT_SHOWCASE_OUT=<path to pbrt_scenes/studio-showcase.pbrt>).
TEST(SceneShowcaseTest, WriteTheFile) {
	const char* out = std::getenv("RT_SHOWCASE_OUT");
	if (!out) GTEST_SKIP() << "set RT_SHOWCASE_OUT to write the showcase file";
	std::ofstream f(out, std::ios::binary);
	f << showcaseFileText();
	ASSERT_TRUE(f.good());
}

TEST(SceneShowcaseTest, ItIsAValidDocumentThatUsesWhatItSaysItUses) {
	const Document d = makeShowcaseScene();
	EXPECT_EQ(d.title, "Studio Showcase");
	for (const Problem& p : validate(d)) ADD_FAILURE() << p.message;   // no errors and no notes
	bool cyclorama = false, rounded = false, mix = false, subsurface = false, glass = false, tiled = false, projector = false, studio = false;
	for (const Object& o : d.objects) {
		cyclorama = cyclorama || o.shape == ShapeKind::Cyclorama;
		rounded = rounded || o.shape == ShapeKind::RoundedBox;
		mix = mix || isMix(o.material);
		subsurface = subsurface || o.material.kind == MaterialKind::Subsurface;
		glass = glass || o.material.kind == MaterialKind::Dielectric;
		tiled = tiled || (!o.material.imageFile.empty() && uvPlaced(o.material));
	}
	for (const Light& l : d.lights) {
		projector = projector || l.kind == LightKind::Projector;
		studio = studio || l.name == "Key light";
	}
	EXPECT_TRUE(cyclorama && rounded && mix && subsurface && glass && tiled && projector && studio);
}

TEST(SceneShowcaseTest, TheCommittedFileIsExactlyWhatTheCodeWritesAndItOpensInTheBuilder) {
	const std::string path = findShowcaseFile();
	if (path.empty()) GTEST_SKIP() << "pbrt_scenes/studio-showcase.pbrt was not found from here";
	const std::string text = readAll(path);
	EXPECT_EQ(text, showcaseFileText()) << "the scene changed: write the file again with RT_SHOWCASE_OUT=" << path << " ray_tracer_tests --gtest_filter=SceneShowcaseTest.WriteTheFile";
	Document back;
	std::string error;
	ASSERT_TRUE(fromPbrt(text, back, error)) << error;
	EXPECT_EQ(back.title, "Studio Showcase");
	EXPECT_EQ(back.objects.size(), makeShowcaseScene().objects.size());
	EXPECT_EQ(back.lights.size(), makeShowcaseScene().lights.size());
	EXPECT_FALSE(hasErrors(validate(back)));
	const pbrt_discover::detail::HeaderTags tags = pbrt_discover::detail::readHeaderTags(text);
	EXPECT_EQ(tags.category, "Materials");
	EXPECT_NE(tags.description.find("photo studio"), std::string::npos);
	EXPECT_EQ(tags.performance, "Medium");
	EXPECT_GT(tags.sceneSize, 1.0) << "the Scene Builder wrote its size";
}

TEST(SceneShowcaseTest, TheRenderersLoaderReadsEveryPartOfIt) {
	const std::string path = findShowcaseFile();
	if (path.empty()) GTEST_SKIP() << "pbrt_scenes/studio-showcase.pbrt was not found from here";
	const pbrt_load::LoadResult r = pbrt_load::loadFile(path);
	ASSERT_TRUE(r.ok) << r.error;
	bool projection = false, mixed = false;
	for (const auto& pl : r.scene.punctualLights) projection = projection || pl.kind == pbrt_flatten::PunctualLightKind::Projection;
	for (const auto& m : r.scene.materials) mixed = mixed || (m.mixMaterialA >= 0 && m.mixMaterialB >= 0);
	EXPECT_TRUE(projection);
	EXPECT_TRUE(mixed);
	EXPECT_FALSE(r.scene.triangles.empty());
	EXPECT_FALSE(r.scene.spheres.empty());
}
