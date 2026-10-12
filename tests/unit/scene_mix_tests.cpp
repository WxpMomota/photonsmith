// Tests for the Scene Builder's mixed materials (Material::mixWith, scene_model.h): what the file holds, the checks, what the pbrt text says, that the renderer's loader reads it,
// and (with the launcher built) that a mix of two matte materials reads the blend of the two a closed form says it should.
#include <gtest/gtest.h>

#include "agreement_test_helpers.h"
#include "../../src/shared/pbrt_load.h"
#include "../../src/shared/scene_doc_diff.h"
#include "../../src/shared/scene_document.h"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace scene_doc;

namespace {

Material matte(double r, double g, double b) {
	Material m;
	m.kind = MaterialKind::Diffuse;
	m.color = {r, g, b};
	return m;
}

Material mixOf(const Material& a, const Material& b, double amount) {
	Material m = a;
	m.mixWith.assign(1, b);
	m.mixAmount = amount;
	return m;
}

std::string findLauncher() { return findRayTracerLauncher(); }

bool render(const Document& d, const char* tag, int spp, std::vector<float>& rgb, int& w, int& h) { return renderPbrtTextCpu(toPbrt(d), tag, spp, rgb, w, h); }

}  // namespace

TEST(SceneMixTest, ASceneWithoutAMixWritesExactlyWhatItAlwaysDid) {
	const Document d = makeStarterScene();
	const std::string json = toJson(d), text = toPbrt(d);
	EXPECT_EQ(json.find("mixWith"), std::string::npos);
	EXPECT_EQ(json.find("mixAmount"), std::string::npos);
	EXPECT_EQ(text.find("MakeNamedMaterial"), std::string::npos);
	EXPECT_EQ(text.find("\"mix\""), std::string::npos);
	for (const Object& o : d.objects) EXPECT_FALSE(isMix(o.material));
}

TEST(SceneMixTest, AMixSurvivesTheJsonAndComesBackTheSame) {
	Document d = makeStarterScene();
	Material second = matte(0.1, 0.4, 0.2);
	second.kind = MaterialKind::Conductor;
	second.roughness = 0.3;
	d.objects[1].material = mixOf(matte(0.7, 0.2, 0.1), second, 0.35);
	Document back;
	std::string error;
	ASSERT_TRUE(fromJson(toJson(d), back, error)) << error;
	const Material& m = back.objects[1].material;
	ASSERT_TRUE(isMix(m));
	EXPECT_EQ(m.mixAmount, 0.35);
	EXPECT_EQ(m.color.r, 0.7);
	EXPECT_EQ(m.mixWith[0].kind, MaterialKind::Conductor);
	EXPECT_EQ(m.mixWith[0].roughness, 0.3);
	EXPECT_EQ(m.mixWith[0].color.g, 0.4);
	EXPECT_EQ(toJson(back), toJson(d));
	EXPECT_FALSE(isMix(back.objects[0].material));
	std::string bad = toJson(d);
	const size_t at = bad.find("\"mixAmount\"");
	ASSERT_NE(at, std::string::npos);
	bad.replace(at, std::string("\"mixAmount\"").size(), "\"mixWith\":{\"kind\":\"diffuse\",\"mixWith\":{}},\"x\"");
	Document refused;
	EXPECT_FALSE(fromJson(bad, refused, error)) << "a mix inside a mix is refused";
}

TEST(SceneMixTest, ItIsWrittenAsTwoNamedMaterialsAndAMixOfThemAndTheLoaderReadsIt) {
	Document d = makeStarterScene();
	d.objects[1].material = mixOf(matte(0.7, 0.2, 0.1), matte(0.1, 0.4, 0.2), 0.3);
	const std::string text = toPbrt(d);
	const size_t a = text.find("MakeNamedMaterial \"mix-1-a\" \"string type\" [ \"diffuse\" ] \"rgb reflectance\" [ ");
	const size_t b = text.find("MakeNamedMaterial \"mix-1-b\" \"string type\" [ \"diffuse\" ]");
	const size_t m = text.find("Material \"mix\" \"string materials\" [ \"mix-1-a\" \"mix-1-b\" ] \"float amount\" [ 0.3 ]");
	ASSERT_NE(a, std::string::npos);
	ASSERT_NE(b, std::string::npos);
	ASSERT_NE(m, std::string::npos);
	EXPECT_LT(a, b);
	EXPECT_LT(b, m);
	EXPECT_LT(m, text.find("Shape", m)) << "the mix is the material the object's shape gets";
	d.objects[2].material = mixOf(matte(0.5, 0.5, 0.5), matte(0.9, 0.9, 0.9), 0.5);
	EXPECT_NE(toPbrt(d).find("mix-2-a"), std::string::npos) << "each mix has names of its own";
	EXPECT_FALSE(hasErrors(validate(d)));
	const std::string path = "scene_mix_load.pbrt";
	{
		std::ofstream out(path, std::ios::binary);
		out << toPbrt(d);
	}
	const pbrt_load::LoadResult r = pbrt_load::loadFile(path);
	std::remove(path.c_str());
	ASSERT_TRUE(r.ok) << r.error;
	int mixes = 0;
	for (const auto& mat : r.scene.materials) mixes += (mat.mixMaterialA >= 0 && mat.mixMaterialB >= 0) ? 1 : 0;
	EXPECT_GE(mixes, 2) << "the renderer's loader reads them as mix materials";
}

TEST(SceneMixTest, ThePrimaryMaterialKeepsItsPictureAndBumpInsideAMix) {
	Document d = makeStarterScene();
	Material first = matte(0.7, 0.2, 0.1);
	first.pattern = Pattern::Marble;
	d.objects[1].material = mixOf(first, matte(0.1, 0.4, 0.2), 0.5);
	const std::string text = toPbrt(d);
	EXPECT_NE(text.find("\"mix-1-a\" \"string type\" [ \"diffuse\" ] \"texture reflectance\" \"pattern-"), std::string::npos) << "the first material still uses its pattern";
	EXPECT_NE(text.find("MakeNamedMaterial \"mix-1-b\" \"string type\" [ \"diffuse\" ] \"rgb reflectance\""), std::string::npos) << "the second is plain";
}

TEST(SceneMixTest, TheChecksCatchABadAmountANestedMixAndASecondMaterialOutOfRange) {
	Document d = makeStarterScene();
	d.objects[1].material = mixOf(matte(0.7, 0.2, 0.1), matte(0.1, 0.4, 0.2), 0.5);
	EXPECT_FALSE(hasErrors(validate(d)));
	d.objects[1].material.mixAmount = 1.5;
	EXPECT_TRUE(hasErrors(validate(d)));
	d.objects[1].material.mixAmount = 0.5;
	d.objects[1].material.mixWith[0].roughness = 2.0;
	EXPECT_TRUE(hasErrors(validate(d)));
	d.objects[1].material.mixWith[0].roughness = 0.0;
	d.objects[1].material.mixWith[0].mixWith.assign(1, matte(1, 1, 1));
	EXPECT_TRUE(hasErrors(validate(d)));
}

TEST(SceneMixTest, TheViewsDrawAMixAsTheBlendOfItsTwoColoursAndTheDiffNamesIt) {
	const Material m = mixOf(matte(1.0, 0.0, 0.0), matte(0.0, 0.0, 1.0), 0.25);
	const Rgb c = displayColor(m);
	EXPECT_NEAR(c.r, 0.75, 1e-12);
	EXPECT_NEAR(c.b, 0.25, 1e-12);
	EXPECT_EQ(displayColor(matte(0.3, 0.4, 0.5)).g, 0.4);
	Document before = makeStarterScene(), after = before;
	after.objects[1].material = m;
	EXPECT_NE(describeChange(before, after), "") << "a mix is a change the undo list can describe";
	EXPECT_NE(describeChange(before, after), describeChange(before, before));
}

// A big level matte plate under a uniform sky of radiance L reads albedo * L. For a mix of two plates' albedos A and B with amount t, the points that are the second are a fraction t
// of the surface, so the plate reads ((1 - t) * A + t * B) * L on average.
TEST(SceneMixRenderTest, AMixOfTwoMatteMaterialsReadsTheBlendOfTheirAlbedos) {
	if (findLauncher().empty()) GTEST_SKIP() << "ray_tracer.exe was not found next to the tests";
	const double A = 0.8, B = 0.2;
	for (double t : {0.0, 0.25, 0.7, 1.0}) {
		Document d;
		Object plate = makeObject(ShapeKind::Quad, "Plate");
		plate.size = {400, 1, 400};
		plate.material = mixOf(matte(A, A, A), matte(B, B, B), t);
		d.objects.push_back(plate);
		d.camera.position = {0.0, 6.0, 0.0};
		d.camera.target = {0.0, 0.0, 0.0};
		d.camera.up = {0.0, 0.0, -1.0};
		d.camera.fov = 8.0;
		d.render.width = 64;
		d.render.height = 64;
		Light sky;
		sky.name = "Sky";
		sky.kind = LightKind::Infinite;
		sky.color = {1.0, 1.0, 1.0};
		sky.intensity = 1.0;
		d.lights.push_back(sky);
		std::vector<float> rgb;
		int w = 0, h = 0;
		ASSERT_TRUE(render(d, "plate", 48, rgb, w, h));
		double mean = 0;
		for (size_t i = 0; i < rgb.size() / 3; ++i) mean += rgb[3 * i + 1];
		mean /= static_cast<double>(rgb.size() / 3);
		const double want = (1.0 - t) * A + t * B;
		EXPECT_NEAR(mean, want, 0.06 * want) << "amount " << t;
	}
}
