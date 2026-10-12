// Tests for the placement of a material's texture coordinates (Material::uvScaleU, uvScaleV, uvOffsetU, uvOffsetV, scene_model.h): the file, the checks, what the pbrt text carries for
// each shape, and (with the launcher built) that a picture on a quad really repeats and moves.
#include <gtest/gtest.h>

#include "agreement_test_helpers.h"
#include "../../src/shared/pbrt_load.h"
#include "../../src/shared/scene_document.h"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace scene_doc;

namespace {

std::string findLauncher() { return findRayTracerLauncher(); }

bool render(const Document& d, const char* tag, int spp, std::vector<float>& rgb, int& w, int& h) { return renderPbrtTextCpu(toPbrt(d), tag, spp, rgb, w, h); }

// The "point2 uv" numbers of the first shape that has them, as written.
std::vector<double> uvOf(const std::string& text) {
	std::vector<double> out;
	const size_t at = text.find("\"point2 uv\" [");
	if (at == std::string::npos) return out;
	const size_t end = text.find(']', at);
	std::istringstream in(text.substr(at + 13, end - at - 13));
	double v;
	while (in >> v) out.push_back(v);
	return out;
}

Object texturedObject(ShapeKind shape) {
	Object o = makeObject(shape, "Thing");
	o.material.imageFile = "picture.png";
	return o;
}

}  // namespace

TEST(SceneUvTest, ASceneThatPlacesNothingWritesExactlyWhatItAlwaysDidAndTheFileStaysSmall) {
	const Document d = makeStarterScene();
	EXPECT_EQ(toJson(d).find("uvScale"), std::string::npos);
	EXPECT_EQ(toJson(d).find("uvOffset"), std::string::npos);
	for (const Object& o : d.objects) EXPECT_FALSE(uvPlaced(o.material));
	for (ShapeKind k : {ShapeKind::Quad, ShapeKind::Box, ShapeKind::Torus}) {
		Document a;
		a.objects.push_back(texturedObject(k));
		Document b = a;
		b.objects[0].material.uvScaleU = 1.0;   // (stated, but the plain value)
		EXPECT_EQ(toPbrt(a), toPbrt(b));
	}
}

TEST(SceneUvTest, ThePlacementSurvivesTheJson) {
	Document d;
	d.objects.push_back(texturedObject(ShapeKind::Quad));
	Material& m = d.objects[0].material;
	m.uvScaleU = 4.0;
	m.uvScaleV = 2.5;
	m.uvOffsetU = 0.25;
	m.uvOffsetV = -0.5;
	Document back;
	std::string error;
	ASSERT_TRUE(fromJson(toJson(d), back, error)) << error;
	EXPECT_EQ(back.objects[0].material.uvScaleU, 4.0);
	EXPECT_EQ(back.objects[0].material.uvScaleV, 2.5);
	EXPECT_EQ(back.objects[0].material.uvOffsetU, 0.25);
	EXPECT_EQ(back.objects[0].material.uvOffsetV, -0.5);
	EXPECT_EQ(toJson(back), toJson(d));
}

TEST(SceneUvTest, AQuadsCoordinatesAreScaledAndShiftedAsStated) {
	Document d;
	d.objects.push_back(texturedObject(ShapeKind::Quad));
	const std::vector<double> plain = uvOf(toPbrt(d));
	ASSERT_EQ(plain.size(), 8u);
	d.objects[0].material.uvScaleU = 3.0;
	d.objects[0].material.uvScaleV = 2.0;
	d.objects[0].material.uvOffsetU = 0.5;
	d.objects[0].material.uvOffsetV = 0.25;
	const std::vector<double> placed = uvOf(toPbrt(d));
	ASSERT_EQ(placed.size(), 8u);
	for (size_t i = 0; i < 8; i += 2) {
		EXPECT_NEAR(placed[i], plain[i] * 3.0 - 0.5, 1e-9) << "scaled, then shifted by half a repeat";
		EXPECT_NEAR(placed[i + 1], plain[i + 1] * 2.0 - 0.25, 1e-9);
	}
}

TEST(SceneUvTest, TheShiftIsInRepeatsWhateverTheRepeatsAre) {
	Document d;
	d.objects.push_back(texturedObject(ShapeKind::Quad));
	const std::vector<double> plain = uvOf(toPbrt(d));
	for (double repeats : {1.0, 4.0, 10.0}) {
		d.objects[0].material.uvScaleU = repeats;
		d.objects[0].material.uvOffsetU = 0.25;
		const std::vector<double> placed = uvOf(toPbrt(d));
		for (size_t i = 0; i < 8; i += 2) EXPECT_NEAR(placed[i] - plain[i] * repeats, -0.25, 1e-9) << "a shift of 0.25 is a quarter of a picture with " << repeats << " repeats";
	}
}

TEST(SceneUvTest, ZeroRepeatsOnlyMatterWhereThereIsATextureToPlace) {
	Document d;
	Object plain = makeObject(ShapeKind::Quad, "Plain");
	plain.material.uvScaleU = 0.0;   // (a plain colour: the rows that would fix it are hidden, and nothing is placed)
	d.objects.push_back(plain);
	EXPECT_FALSE(hasErrors(validate(d)));
	d.objects[0].material.checker = true;
	EXPECT_TRUE(hasErrors(validate(d)));
}

TEST(SceneUvTest, AReadyMadeMaterialKeepsTheTextureCoordinatePlacementOfTheBumpPictureItKeeps) {
	Material m;
	m.bumpFile = "bump.png";
	m.uvScaleU = 4.0;
	m.uvOffsetV = 0.3;
	ASSERT_TRUE(applyMaterialPreset(m, "gold"));
	EXPECT_EQ(m.bumpFile, "bump.png");
	EXPECT_EQ(m.uvScaleU, 4.0);
	EXPECT_EQ(m.uvOffsetV, 0.3);
}

TEST(SceneUvTest, ABoxAndAReadyMadeShapeTakeItToo) {
	for (ShapeKind k : {ShapeKind::Box, ShapeKind::Stairs, ShapeKind::Torus, ShapeKind::Dome}) {
		Document d;
		d.objects.push_back(texturedObject(k));
		const std::vector<double> plain = uvOf(toPbrt(d));
		d.objects[0].material.uvScaleU = 5.0;
		d.objects[0].material.uvOffsetV = 0.1;
		const std::vector<double> placed = uvOf(toPbrt(d));
		ASSERT_EQ(placed.size(), plain.size()) << toString(k);
		ASSERT_FALSE(plain.empty()) << toString(k);
		for (size_t i = 0; i + 1 < plain.size(); i += 2) {
			ASSERT_NEAR(placed[i], plain[i] * 5.0, 1e-6) << toString(k);
			ASSERT_NEAR(placed[i + 1], plain[i + 1] - 0.1, 1e-6) << toString(k);
		}
	}
}

TEST(SceneUvTest, ItOnlyActsWhereThereIsATextureToPlace) {
	Document d;
	Object quad = makeObject(ShapeKind::Quad, "Plain");
	d.objects.push_back(quad);
	const std::vector<double> before = uvOf(toPbrt(d));
	d.objects[0].material.uvScaleU = 3.0;
	EXPECT_EQ(uvOf(toPbrt(d)), before) << "a plain colour has no texture, so nothing moves";
	d.objects[0].material.checker = true;
	EXPECT_NE(uvOf(toPbrt(d)), before) << "a checker is placed by the coordinates too";
	d.objects[0].material.checker = false;
	d.objects[0].material.bumpFile = "bump.png";
	EXPECT_NE(uvOf(toPbrt(d)), before) << "and so is a bump map";
}

TEST(SceneUvTest, TheChecksCatchAZeroRepeatAndNoShapeNeedsAWarningOnAnyRenderer) {
	Document d;
	d.objects.push_back(texturedObject(ShapeKind::Quad));
	d.objects[0].material.uvScaleU = 2.0;
	EXPECT_FALSE(hasErrors(validate(d)));
	d.objects[0].material.uvScaleV = 0.0;
	EXPECT_TRUE(hasErrors(validate(d)));
	d.objects[0].material.uvScaleV = 1.0;
	for (ShapeKind k : {ShapeKind::Sphere, ShapeKind::Disk, ShapeKind::Cylinder, ShapeKind::Cone}) {
		d.objects[0].shape = k;
		bool warned = false;
		for (const Problem& p : validate(d)) warned = warned || p.message.find("on a Mac the repeats and shift") != std::string::npos;
		EXPECT_FALSE(warned) << toString(k) << ": the CPU, OptiX and Metal renderers all place it, so there is nothing to warn about";
	}
	d.objects[0].shape = ShapeKind::Box;
	for (const Problem& p : validate(d)) EXPECT_EQ(p.message.find("on a Mac the repeats"), std::string::npos);
}

TEST(SceneUvTest, ABoxBakesThePlacementIntoItsCoordinatesAndASphereGivesItToTheTexture) {
	for (ShapeKind k : {ShapeKind::Sphere, ShapeKind::Disk, ShapeKind::Cylinder, ShapeKind::Cone, ShapeKind::Mesh}) {
		Document d;
		d.objects.push_back(texturedObject(k));
		d.objects[0].material.uvScaleU = 3.0;
		d.objects[0].material.uvScaleV = 2.0;
		d.objects[0].material.uvOffsetU = 0.5;
		d.objects[0].material.uvOffsetV = 0.0;
		const std::string text = toPbrt(d);
		const size_t at = text.find("Texture \"picture-1\"");
		ASSERT_NE(at, std::string::npos) << toString(k);
		const std::string line = text.substr(at, text.find('\n', at) - at);
		EXPECT_NE(line.find("\"float uscale\" [ 3 ]"), std::string::npos) << toString(k) << ": " << line;
		EXPECT_NE(line.find("\"float vscale\" [ 2 ]"), std::string::npos) << toString(k);
		EXPECT_NE(line.find("\"float udelta\" [ -0.5 ]"), std::string::npos) << toString(k) << ": pbrt's delta is minus the shift";
		EXPECT_NE(line.find("\"float vdelta\" [ 0 ]"), std::string::npos) << toString(k) << " (no '-0')";
	}
	for (ShapeKind k : {ShapeKind::Quad, ShapeKind::Box, ShapeKind::Torus}) {
		Document d;
		d.objects.push_back(texturedObject(k));
		d.objects[0].material.uvScaleU = 3.0;
		const std::string text = toPbrt(d);
		const size_t at = text.find("Texture \"picture-1\"");
		ASSERT_NE(at, std::string::npos) << toString(k);
		EXPECT_EQ(text.substr(at, text.find('\n', at) - at).find("uscale"), std::string::npos) << toString(k) << ": its own coordinates carry the placement, it is not applied twice";
	}
	Document bump;
	bump.objects.push_back(makeObject(ShapeKind::Sphere, "Ball"));
	bump.objects[0].material.bumpFile = "bump.png";
	bump.objects[0].material.uvScaleU = 4.0;
	const std::string text = toPbrt(bump);
	EXPECT_NE(text.find("\"bump-1-image\" \"float\" \"imagemap\" \"string filename\" [ \"bump.png\" ] \"float uscale\" [ 4 ]"), std::string::npos) << "a bump map is placed too";
	Document checks;
	checks.objects.push_back(makeObject(ShapeKind::Sphere, "Ball"));
	checks.objects[0].material.checker = true;
	checks.objects[0].material.checkerCount = 8.0;
	checks.objects[0].material.uvScaleU = 3.0;
	EXPECT_NE(toPbrt(checks).find("\"checkerboard\" \"float uscale\" [ 24 ] \"float vscale\" [ 8 ]"), std::string::npos) << "a checker's repeats multiply its checks";
}

// A picture that is red on its left half and blue on its right, on a level quad seen from above, with a uniform sky. The difference of red and blue over the left and the right half of the
// quad's picture says how the picture sits: unrepeated, one half is red and the other blue; repeated twice, each half has one of each; shifted by half a repeat, the halves swap.
TEST(SceneUvRenderTest, APictureRepeatsAndMovesAcrossAQuad) {
	if (findLauncher().empty()) GTEST_SKIP() << "ray_tracer.exe was not found next to the tests";
	const int n = 16;
	std::vector<float> img(static_cast<size_t>(n) * n * 3, 0.0f);
	for (int y = 0; y < n; ++y)
		for (int x = 0; x < n; ++x) img[(static_cast<size_t>(y) * n + x) * 3 + (x < n / 2 ? 0 : 2)] = 1.0f;
	const std::string picture = std::filesystem::absolute("scene_uv_picture.hdr").string();
	{
		std::ofstream out(picture, std::ios::binary);
		out << sky::encodeHdr(img, n, n);
	}
	// [case][half]: red minus blue over the left / right half of the picture
	double side[3][2] = {};
	const double scales[3] = {1.0, 2.0, 1.0}, offsets[3] = {0.0, 0.0, 0.5};
	for (int c = 0; c < 3; ++c) {
		Document d;
		Object quad = makeObject(ShapeKind::Quad, "Quad");
		quad.size = {4, 1, 4};   // (the camera sees all of it)
		quad.material.kind = MaterialKind::Diffuse;
		quad.material.imageFile = picture;
		quad.material.uvScaleU = scales[c];
		quad.material.uvOffsetU = offsets[c];
		d.objects.push_back(quad);
		d.camera.position = {0.0, 8.0, 0.0};
		d.camera.target = {0.0, 0.0, 0.0};
		d.camera.up = {0.0, 0.0, -1.0};
		d.camera.fov = 40.0;
		d.render.width = 64;
		d.render.height = 64;
		Light sky;
		sky.name = "Sky";
		sky.kind = LightKind::Infinite;
		sky.intensity = 1.0;
		d.lights.push_back(sky);
		std::vector<float> rgb;
		int w = 0, h = 0;
		ASSERT_TRUE(render(d, "quad", 48, rgb, w, h));
		for (int y = h / 4; y < 3 * h / 4; ++y)
			for (int x = w / 8; x < 7 * w / 8; ++x) {
				const size_t i = (static_cast<size_t>(y) * w + x) * 3;
				side[c][x < w / 2 ? 0 : 1] += rgb[i] - rgb[i + 2];
			}
	}
	std::remove(picture.c_str());
	const double strong = std::fabs(side[0][0]) + std::fabs(side[0][1]);
	ASSERT_GT(strong, 1.0) << "the picture shows";
	EXPECT_LT(side[0][0] * side[0][1], 0.0) << "unrepeated: one half red, the other blue";
	EXPECT_LT(std::fabs(side[1][0]) + std::fabs(side[1][1]), 0.25 * strong) << "repeated twice: each half has one of each";
	EXPECT_LT(side[2][0] * side[2][1], 0.0) << "shifted: still one of each side";
	EXPECT_LT(side[0][0] * side[2][0], 0.0) << "and the halves have swapped";
	EXPECT_LT(side[0][1] * side[2][1], 0.0);
}


// ---------------------------------------------------------------------------------------------------------------------------------
// The renderers' own texture mapping: pbrt's imagemap uscale/vscale/udelta/vdelta (pbrt_flatten::UvMapping), which a sphere, disk, cylinder, cone or mesh file gets the Scene Builder's
// repeats and shift through.
// ---------------------------------------------------------------------------------------------------------------------------------

TEST(UvMappingTest, TheLoaderReadsAnImagemapsUscaleVscaleUdeltaVdeltaForTheReflectanceAndTheDisplacementImage) {
	const std::vector<float> grey(4 * 4 * 3, 0.5f);   // three small pictures, so that the loader finds the files it is told about
	std::string files[3];
	for (int i = 0; i < 3; ++i) {
		files[i] = std::filesystem::absolute(std::string("uv_mapping_load_") + char('a' + i) + ".hdr").string();
		std::ofstream out(files[i], std::ios::binary);
		out << scene_doc::sky::encodeHdr(grey, 4, 4);
	}
	const std::string text =
	    "LookAt 0 0 5 0 0 0 0 1 0\nCamera \"perspective\" \"float fov\" [ 40 ]\nFilm \"rgb\" \"integer xresolution\" [ 16 ] \"integer yresolution\" [ 16 ]\nWorldBegin\n"
	    "Texture \"a\" \"spectrum\" \"imagemap\" \"string filename\" [ \"" + files[0] + "\" ] \"float uscale\" [ 3 ] \"float vscale\" [ 2 ] \"float udelta\" [ 0.25 ] \"float vdelta\" [ -0.5 ]\n"
	    "Texture \"b\" \"float\" \"imagemap\" \"string filename\" [ \"" + files[1] + "\" ] \"float uscale\" [ 4 ]\n"
	    "Texture \"plain\" \"spectrum\" \"imagemap\" \"string filename\" [ \"" + files[2] + "\" ]\n"
	    "MakeNamedMaterial \"m1\" \"string type\" [ \"diffuse\" ] \"texture reflectance\" \"a\" \"texture displacement\" \"b\"\n"
	    "MakeNamedMaterial \"m2\" \"string type\" [ \"diffuse\" ] \"texture reflectance\" \"plain\"\n"
	    "NamedMaterial \"m1\"\nShape \"sphere\" \"float radius\" [ 1 ]\nNamedMaterial \"m2\"\nShape \"sphere\" \"float radius\" [ 1 ]\n";
	const std::string path = "uv_mapping_load.pbrt";
	{
		std::ofstream out(path, std::ios::binary);
		out << text;
	}
	const pbrt_load::LoadResult r = pbrt_load::loadFile(path);
	std::remove(path.c_str());
	for (const std::string& f : files) std::remove(f.c_str());
	ASSERT_TRUE(r.ok) << r.error;
	const pbrt_flatten::Material* mapped = nullptr;
	const pbrt_flatten::Material* plain = nullptr;
	for (const auto& m : r.scene.materials) {
		if (m.textureFilename.find("uv_mapping_load_a") != std::string::npos) mapped = &m;
		if (m.textureFilename.find("uv_mapping_load_c") != std::string::npos) plain = &m;
	}
	ASSERT_NE(mapped, nullptr);
	ASSERT_NE(plain, nullptr);
	EXPECT_EQ(mapped->textureMapping.su, 3.0);
	EXPECT_EQ(mapped->textureMapping.sv, 2.0);
	EXPECT_EQ(mapped->textureMapping.du, 0.25);
	EXPECT_EQ(mapped->textureMapping.dv, -0.5);
	EXPECT_EQ(mapped->displacementMapping.su, 4.0);
	EXPECT_EQ(mapped->displacementMapping.sv, 1.0);
	EXPECT_TRUE(plain->textureMapping.identity()) << "an imagemap that says nothing is not placed";
	EXPECT_TRUE(plain->displacementMapping.identity());
}

// A quad textured through the Texture's own mapping renders as the quad whose own coordinates were scaled and shifted by hand: the same picture, to within the tiny difference of the
// texture filter's footprint (the mapped texture scales its screen-space derivatives, the baked one gets them from the scaled coordinates).
TEST(UvMappingRenderTest, ATextureMappedByItsUscaleVscaleUdeltaVdeltaRendersAsTheSameQuadWithCoordinatesScaledAndShiftedByHand) {
	if (findRayTracerLauncher().empty()) GTEST_SKIP() << "ray_tracer.exe was not found next to the tests";
	const int n = 8;
	std::vector<float> img(static_cast<size_t>(n) * n * 3, 0.0f);
	for (int y = 0; y < n; ++y)
		for (int x = 0; x < n; ++x) {
			float* p = &img[(static_cast<size_t>(y) * n + x) * 3];
			if ((x < n / 2) == (y < n / 2)) { p[0] = 1.0f; p[1] = 0.9f; } else { p[2] = 1.0f; }
		}
	const std::string picture = std::filesystem::absolute("uv_mapping_picture.hdr").string();
	{
		std::ofstream out(picture, std::ios::binary);
		out << scene_doc::sky::encodeHdr(img, n, n);
	}
	const auto scene = [&](const std::string& mapping, const std::string& uv) {
		return std::string("LookAt 0 0 6 0 0 0 0 1 0\nCamera \"perspective\" \"float fov\" [ 30 ]\nFilm \"rgb\" \"integer xresolution\" [ 64 ] \"integer yresolution\" [ 64 ]\nWorldBegin\n"
		                   "LightSource \"infinite\" \"rgb L\" [ 1 1 1 ]\nTexture \"t\" \"spectrum\" \"imagemap\" \"string filename\" [ \"") + picture + "\" ] \"string wrap\" \"repeat\"" + mapping +
		       "\nMaterial \"diffuse\" \"texture reflectance\" \"t\"\nShape \"trianglemesh\" \"integer indices\" [ 0 1 2 0 2 3 ] \"point3 P\" [ -2 -2 0 2 -2 0 2 2 0 -2 2 0 ] \"point2 uv\" [ " + uv + " ]\n";
	};
	std::vector<float> a, b;
	int w = 0, h = 0, w2 = 0, h2 = 0;
	ASSERT_TRUE(renderPbrtTextCpu(scene(" \"float uscale\" [ 3 ] \"float vscale\" [ 2 ] \"float udelta\" [ 0.25 ] \"float vdelta\" [ -0.1 ]", "0 0 1 0 1 1 0 1"), "uvmap_mapped", 48, a, w, h));
	ASSERT_TRUE(renderPbrtTextCpu(scene("", "0.25 -0.1 3.25 -0.1 3.25 1.9 0.25 1.9"), "uvmap_baked", 48, b, w2, h2));
	std::remove(picture.c_str());
	ASSERT_EQ(w, w2);
	ASSERT_EQ(h, h2);
	double diff = 0, level = 0;
	for (size_t i = 0; i < a.size(); ++i) {
		diff += std::fabs(a[i] - b[i]);
		level += b[i];
	}
	EXPECT_GT(level / static_cast<double>(b.size()), 0.2) << "the picture shows";
	EXPECT_LT(diff / level, 0.02) << "mapped and by-hand coordinates render the same picture";
	// and the mapping is not the identity: without it the picture is different
	std::vector<float> plain;
	ASSERT_TRUE(renderPbrtTextCpu(scene("", "0 0 1 0 1 1 0 1"), "uvmap_plain", 48, plain, w2, h2));
	double plainDiff = 0;
	for (size_t i = 0; i < a.size(); ++i) plainDiff += std::fabs(a[i] - plain[i]);
	EXPECT_GT(plainDiff / level, 0.2) << "the mapping changes the picture";
}
