// Tests for the Scene Builder's projector light (Light of kind Projector, src/shared/scene_projector.h): the file it writes, that it survives the JSON and the checks, and (with the
// launcher built) that its slide lands on a wall the right way up and not mirrored, whichever way it is aimed, with the brightness a point light of the same strength would give.
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

constexpr double kPi = 3.14159265358979323846;

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
	const std::string scenePath = std::string("scene_projector_") + tag + ".pbrt", outPath = std::string("scene_projector_") + tag + ".exr";
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

// A 16 x 16 slide: red top left, green top right, blue bottom left, white bottom right.
std::string writeSlide() {
	const int n = 16;
	std::vector<float> img(static_cast<size_t>(n) * n * 3, 0.0f);
	for (int y = 0; y < n; ++y)
		for (int x = 0; x < n; ++x) {
			const bool top = y < n / 2, left = x < n / 2;
			float* p = &img[(static_cast<size_t>(y) * n + x) * 3];
			if (top && left) { p[0] = 1.0f; }
			else if (top) { p[1] = 1.0f; }
			else if (left) { p[2] = 1.0f; }
			else { p[0] = p[1] = p[2] = 1.0f; }
		}
	const std::string path = std::filesystem::absolute("scene_projector_slide.hdr").string();
	std::ofstream out(path, std::ios::binary);
	out << sky::encodeHdr(img, n, n);
	return path;
}

// Mean colour of a block of the picture (fractions of its width and height).
void blockMean(const std::vector<float>& rgb, int w, int h, double x0, double x1, double y0, double y1, double out[3]) {
	out[0] = out[1] = out[2] = 0;
	int n = 0;
	for (int y = static_cast<int>(y0 * h); y < static_cast<int>(y1 * h); ++y)
		for (int x = static_cast<int>(x0 * w); x < static_cast<int>(x1 * w); ++x, ++n)
			for (int c = 0; c < 3; ++c) out[c] += rgb[(static_cast<size_t>(y) * w + x) * 3 + c];
	for (int c = 0; c < 3; ++c) out[c] /= std::max(1, n);
}

// The wall is a big diffuse quad facing +Z at the origin; the projector and a camera stand at `from` and look at `to`, the camera with the same field of view, so the camera sees what the
// projector projects: the slide, as it is.
void expectSlideLandsRightWayUp(const Float3& from, const Float3& to, const Float3& cameraUp, const char* tag) {
	if (findLauncher().empty()) GTEST_SKIP() << "ray_tracer.exe was not found next to the tests";
	Document d;
	Object wall = makeObject(ShapeKind::Quad, "Wall");
	wall.size = {400, 1, 400};
	wall.rotation = {90, 0, 0};   // stood up, facing +Z
	wall.material.color = {0.8, 0.8, 0.8};
	d.objects.push_back(wall);
	d.camera.position = from;
	d.camera.target = to;
	d.camera.up = cameraUp;
	d.camera.fov = 30.0;
	d.render.width = 64;
	d.render.height = 64;
	Light l;
	l.name = "Projector";
	l.kind = LightKind::Projector;
	l.position = from;
	l.target = to;
	l.fov = 30.0;
	l.intensity = 100.0;
	l.imageFile = writeSlide();
	d.lights.push_back(l);
	std::vector<float> rgb;
	int w = 0, h = 0;
	ASSERT_TRUE(render(d, tag, 64, rgb, w, h));
	std::remove(l.imageFile.c_str());
	double tl[3], tr[3], bl[3], br[3];
	blockMean(rgb, w, h, 0.12, 0.38, 0.12, 0.38, tl);
	blockMean(rgb, w, h, 0.62, 0.88, 0.12, 0.38, tr);
	blockMean(rgb, w, h, 0.12, 0.38, 0.62, 0.88, bl);
	blockMean(rgb, w, h, 0.62, 0.88, 0.62, 0.88, br);
	EXPECT_GT(tl[0], 3.0 * std::max(tl[1], tl[2])) << tag << ": the slide's red corner is at the top left";
	EXPECT_GT(tr[1], 3.0 * std::max(tr[0], tr[2])) << tag << ": green at the top right";
	EXPECT_GT(bl[2], 3.0 * std::max(bl[0], bl[1])) << tag << ": blue at the bottom left";
	EXPECT_GT(std::min({br[0], br[1], br[2]}), 0.5 * std::max({br[0], br[1], br[2]})) << tag << ": white at the bottom right";
	EXPECT_GT(br[0], 0.05) << tag << ": and it is lit";
}

}  // namespace

TEST(SceneProjectorTest, TheFrameIsAProperRotationWithTheSlidesTopUpwardsWhereverItIsAimed) {
	Light l;
	l.kind = LightKind::Projector;
	for (Float3 target : {Float3{0, 0, 0}, Float3{5, -3, 2}, Float3{0, -10, 0.001}, Float3{0, 10, 0}, Float3{-4, 0, -4}}) {
		l.position = {0.5, 3.0, 1.0};
		l.target = target;
		const projector::Frame f = projector::frameOf(l);
		const auto dot = [](const Float3& a, const Float3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; };
		EXPECT_NEAR(dot(f.right, f.right), 1.0, 1e-9);
		EXPECT_NEAR(dot(f.up, f.up), 1.0, 1e-9);
		EXPECT_NEAR(dot(f.dir, f.dir), 1.0, 1e-9);
		EXPECT_NEAR(dot(f.right, f.up), 0.0, 1e-9);
		EXPECT_NEAR(dot(f.right, f.dir), 0.0, 1e-9);
		EXPECT_NEAR(dot(f.up, f.dir), 0.0, 1e-9);
		const Float3 c = projector::cross(f.right, f.up);
		EXPECT_NEAR(dot(c, f.dir), 1.0, 1e-9) << "right x up = forward: no mirroring";
	}
	Light down;
	down.position = {0, 5, 0};
	down.target = {0, 0, 0};
	EXPECT_NEAR(projector::frameOf(down).up.z, -1.0, 1e-9) << "aimed straight down, the top of the slide points towards -Z";
}

TEST(SceneProjectorTest, TheSlideSpansTheFieldOfViewAlongItsShorterSide) {
	double hw, hh;
	projector::halfSize(90.0, 1.0, 5.0, hw, hh);
	EXPECT_NEAR(hw, 5.0, 1e-9);
	EXPECT_NEAR(hh, 5.0, 1e-9);
	projector::halfSize(90.0, 2.0, 5.0, hw, hh);
	EXPECT_NEAR(hh, 5.0, 1e-9);
	EXPECT_NEAR(hw, 10.0, 1e-9);
	projector::halfSize(90.0, 0.5, 5.0, hw, hh);
	EXPECT_NEAR(hw, 5.0, 1e-9);
	EXPECT_NEAR(hh, 10.0, 1e-9);
}

TEST(SceneProjectorTest, ItIsWrittenAsAProjectionLightWithAFrameAndSurvivesTheFileAndTheChecks) {
	Document d = makeStarterScene();
	Light l;
	l.name = "Slide";
	l.kind = LightKind::Projector;
	l.position = {0, 4, 6};
	l.target = {0, 1, 0};
	l.fov = 35.0;
	l.intensity = 150.0;
	l.imageFile = "slide.png";
	d.lights.push_back(l);
	const std::string text = toPbrt(d);
	const size_t at = text.find("LightSource \"projection\"");
	ASSERT_NE(at, std::string::npos);
	EXPECT_NE(text.find("\"float fov\" [ 35 ]", at), std::string::npos);
	EXPECT_NE(text.find("\"float scale\" [ 150 ]", at), std::string::npos);
	EXPECT_NE(text.find("\"string filename\" [ \"slide.png\" ]", at), std::string::npos);
	EXPECT_NE(text.rfind("Transform [", at), std::string::npos);
	Document back;
	std::string error;
	ASSERT_TRUE(fromJson(toJson(d), back, error)) << error;
	ASSERT_EQ(back.lights.size(), d.lights.size());
	EXPECT_EQ(back.lights.back().kind, LightKind::Projector);
	EXPECT_EQ(back.lights.back().fov, 35.0);
	EXPECT_FALSE(hasErrors(validate(d)));
	const std::string path = "scene_projector_load.pbrt";
	{
		std::ofstream out(path, std::ios::binary);
		out << text;
	}
	const pbrt_load::LoadResult r = pbrt_load::loadFile(path);
	std::remove(path.c_str());
	ASSERT_TRUE(r.ok) << r.error;
	bool found = false;
	for (const auto& pl : r.scene.punctualLights) found = found || pl.kind == pbrt_flatten::PunctualLightKind::Projection;
	EXPECT_TRUE(found) << "the renderer's loader reads it as a projection light";
}

TEST(SceneProjectorTest, TheChecksCatchABadFieldOfViewAMissingSlideAndAnAimAtItself) {
	Document d = makeStarterScene();
	Light l;
	l.name = "Slide";
	l.kind = LightKind::Projector;
	l.position = {0, 4, 6};
	l.target = {0, 1, 0};
	d.lights.push_back(l);
	bool warned = false;
	for (const Problem& p : validate(d)) warned = warned || (p.message.find("no slide picture") != std::string::npos);
	EXPECT_TRUE(warned) << "a projector without a slide shines a plain beam: say so";
	d.lights.back().fov = 0.0;
	EXPECT_TRUE(hasErrors(validate(d)));
	d.lights.back().fov = 40.0;
	d.lights.back().target = d.lights.back().position;
	EXPECT_TRUE(hasErrors(validate(d)));
}

TEST(SceneProjectorRenderTest, TheSlideLandsOnAWallRightWayUpAndNotMirroredAimedLevel) {
	expectSlideLandsRightWayUp({0.0, 0.0, 6.0}, {0.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, "level");
}

TEST(SceneProjectorRenderTest, TheSlideLandsRightWayUpAimedDownAndSideways) {
	expectSlideLandsRightWayUp({3.0, 4.0, 5.0}, {0.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, "tilted");
}

// A white slide of strength I, d away from a diffuse wall of albedo rho facing it: the middle of the wall reads rho / pi * I / d^2, as it does under a point light of the same strength.
TEST(SceneProjectorRenderTest, AProjectorLightsASurfaceLikeAPointLightOfTheSameStrengthWhereTheSlideIsWhite) {
	if (findLauncher().empty()) GTEST_SKIP() << "ray_tracer.exe was not found next to the tests";
	const int n = 8;
	std::vector<float> white(static_cast<size_t>(n) * n * 3, 1.0f);
	const std::string slide = std::filesystem::absolute("scene_projector_white.hdr").string();
	{
		std::ofstream out(slide, std::ios::binary);
		out << sky::encodeHdr(white, n, n);
	}
	double mean[2] = {0, 0};
	for (int pass = 0; pass < 2; ++pass) {
		Document d;
		Object wall = makeObject(ShapeKind::Quad, "Wall");
		wall.size = {400, 1, 400};
		wall.rotation = {90, 0, 0};
		wall.material.color = {0.5, 0.5, 0.5};
		d.objects.push_back(wall);
		d.camera.position = {0.0, 0.0, 6.0};
		d.camera.target = {0.0, 0.0, 0.0};
		d.camera.fov = 6.0;
		d.render.width = 32;
		d.render.height = 32;
		Light l;
		l.name = "Light";
		l.kind = pass == 0 ? LightKind::Projector : LightKind::Point;
		l.position = {0.0, 0.0, 6.0};
		l.target = {0.0, 0.0, 0.0};
		l.intensity = 90.0;
		l.fov = 40.0;
		if (pass == 0) l.imageFile = slide;
		d.lights.push_back(l);
		std::vector<float> rgb;
		int w = 0, h = 0;
		ASSERT_TRUE(render(d, "units", 32, rgb, w, h));
		double m = 0;
		for (size_t i = 0; i < rgb.size() / 3; ++i) m += rgb[3 * i + 1];
		mean[pass] = m / static_cast<double>(rgb.size() / 3);
	}
	std::remove(slide.c_str());
	const double want = 0.5 / kPi * 90.0 / 36.0;
	EXPECT_NEAR(mean[1], want, 0.05 * want) << "the point light reads rho/pi * I/d^2";
	EXPECT_NEAR(mean[0], mean[1], 0.08 * mean[1]) << "and the projector's white slide reads the same";
}
