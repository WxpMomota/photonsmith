// Tests for the placement of a material's texture coordinates (Material::uvScaleU, uvScaleV, uvOffsetU, uvOffsetV, scene_model.h): the file, the checks, what the pbrt text carries for
// each shape, and (with the launcher built) that a picture on a quad really repeats and moves.
#include <gtest/gtest.h>

#include "agreement_test_helpers.h"
#include "../../src/shared/scene_document.h"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace scene_doc;

namespace {

std::string findLauncher() {
	namespace fs = std::filesystem;
	std::error_code ec;
	for (const char* c : {"ray_tracer.exe", "../ray_tracer.exe", "../../ray_tracer.exe", "x64/Release/ray_tracer.exe", "../x64/Release/ray_tracer.exe",
	                      "../../x64/Release/ray_tracer.exe", "ray_tracer", "../ray_tracer", "../../ray_tracer"})
		if (fs::is_regular_file(c, ec)) return fs::absolute(c, ec).string();
	return "";
}

bool render(const Document& d, const char* tag, int spp, std::vector<float>& rgb, int& w, int& h) {
	const std::string launcher = findLauncher();
	const std::string scenePath = std::string("scene_uv_") + tag + ".pbrt", outPath = std::string("scene_uv_") + tag + ".exr";
	{
		std::ofstream out(scenePath, std::ios::binary);
		out << toPbrt(d);
	}
	std::remove(outPath.c_str());
	std::ostringstream cmd;
	cmd << "\"" << launcher << "\" --cpu --output \"" << outPath << "\" " << spp << " 256 1 \"" << scenePath << "\" > " << scenePath << ".log 2>&1";
	std::string command = cmd.str();
#ifdef _WIN32
	command = "\"" + command + "\"";
#endif
	const bool ran = std::system(command.c_str()) == 0 && loadLinearRgbPixels(outPath, w, h, rgb);
	std::remove(scenePath.c_str());
	std::remove(outPath.c_str());
	std::remove((outPath + ".run_marker.txt").c_str());
	std::remove((scenePath + ".log").c_str());
	return ran;
}

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
		EXPECT_NEAR(placed[i], (plain[i] - 0.5) * 3.0, 1e-9);
		EXPECT_NEAR(placed[i + 1], (plain[i + 1] - 0.25) * 2.0, 1e-9);
	}
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

TEST(SceneUvTest, TheChecksCatchAZeroRepeatAndSayWhereItHasNoEffect) {
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
		for (const Problem& p : validate(d)) warned = warned || p.message.find("keeps the renderer's own texture coordinates") != std::string::npos;
		EXPECT_TRUE(warned) << toString(k);
	}
	d.objects[0].shape = ShapeKind::Box;
	for (const Problem& p : validate(d)) EXPECT_EQ(p.message.find("keeps the renderer's own"), std::string::npos);
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
