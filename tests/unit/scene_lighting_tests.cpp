// Tests for the Scene Builder's lighting presets (src/shared/scene_lighting.h) and the turn of an image sky (Light::skySpin): what each preset puts in the document, where it puts it
// relative to the camera, that the file round trips, and (with the launcher built) that the renderer lights a scene the way the numbers say.
#include <gtest/gtest.h>

#include "agreement_test_helpers.h"
#include "../../src/shared/scene_document.h"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace scene_doc;
using namespace scene_doc::lighting;

namespace {

constexpr double kPi = 3.14159265358979323846;

double dot(const Float3& a, const Float3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Float3 minus(const Float3& a, const Float3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
double length(const Float3& a) { return std::sqrt(dot(a, a)); }

const Light* named(const Document& d, const std::string& name) {
	for (const Light& l : d.lights)
		if (l.name == name) return &l;
	return nullptr;
}

Document sceneLookingAlong(double fx, double fz, double distance) {
	Document d = makeStarterScene();
	d.camera.target = {0.0, 1.0, 0.0};
	d.camera.position = {-fx * distance, 1.0, -fz * distance};   // the camera looks along (fx, 0, fz)
	return d;
}

std::string findLauncher() {
	namespace fs = std::filesystem;
	std::error_code ec;
	for (const char* c : {"ray_tracer.exe", "../ray_tracer.exe", "../../ray_tracer.exe", "x64/Release/ray_tracer.exe", "../x64/Release/ray_tracer.exe",
	                      "../../x64/Release/ray_tracer.exe", "ray_tracer", "../ray_tracer", "../../ray_tracer"})
		if (fs::is_regular_file(c, ec)) return fs::absolute(c, ec).string();
	return "";
}

// Renders `d` on the CPU to a linear float picture (w x h, rgb); false if it did not.
bool render(const Document& d, const char* tag, int spp, std::vector<float>& rgb, int& w, int& h) {
	const std::string launcher = findLauncher();
	const std::string scenePath = std::string("scene_lighting_") + tag + ".pbrt", outPath = std::string("scene_lighting_") + tag + ".exr";
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

}  // namespace

TEST(SceneLightingTest, EveryPresetIsAValidLightingWithAName) {
	for (Preset p : allPresets()) {
		Document d = makeStarterScene();
		apply(d, p);
		EXPECT_FALSE(d.lights.empty()) << presetName(p);
		EXPECT_FALSE(hasErrors(validate(d))) << presetName(p);
		EXPECT_GT(std::string(presetName(p)).size(), 3u);
		EXPECT_GT(std::string(presetNote(p)).size(), 20u);
	}
}

TEST(SceneLightingTest, ALightingReplacesTheLightsButNotTheObjectsThatGiveOffLight) {
	Document d = makeStarterScene();
	Object panel = makeAreaLightPanel("Panel");
	d.objects.push_back(panel);
	const size_t objects = d.objects.size();
	d.lights.push_back(Light());
	d.lights.push_back(Light());
	apply(d, Preset::Overcast);
	EXPECT_EQ(d.lights.size(), 1u);
	EXPECT_EQ(d.objects.size(), objects);
	EXPECT_TRUE(d.objects.back().emissive);
}

TEST(SceneLightingTest, TheStudioRigAimsAtWhatTheCameraLooksAtAtTheCamerasDistanceWithTheStatedIrradiance) {
	for (double dist : {4.0, 12.0, 60.0}) {
		Document d = sceneLookingAlong(0.0, -1.0, dist);
		apply(d, Preset::Studio);
		const Light* key = named(d, "Key light");
		const Light* fill = named(d, "Fill light");
		const Light* rim = named(d, "Rim light");
		ASSERT_TRUE(key && fill && rim);
		for (const Light* l : {key, fill, rim}) {
			EXPECT_EQ(l->kind, LightKind::Spot);
			EXPECT_NEAR(l->target.x, d.camera.target.x, 1e-9);
			EXPECT_NEAR(l->target.y, d.camera.target.y, 1e-9);
			const double away = length(minus(l->position, l->target));
			EXPECT_NEAR(away, 0.9 * dist, 0.02 * dist) << "the rig is made for the size of the view";
		}
		// strength = irradiance * d^2: a surface at the subject facing the light gets the stated irradiance
		EXPECT_NEAR(key->intensity / std::pow(length(minus(key->position, key->target)), 2), 2.2, 1e-9);
		EXPECT_NEAR(fill->intensity / std::pow(length(minus(fill->position, fill->target)), 2), 0.7, 1e-9);
		EXPECT_GT(key->intensity, fill->intensity);
	}
}

TEST(SceneLightingTest, TheKeyIsOnTheCamerasLeftTheFillOnItsRightAndTheRimBehindWhicheverWayTheCameraLooks) {
	for (double heading : {0.0, 70.0, 180.0, 250.0}) {
		const double fx = std::sin(heading * kPi / 180.0), fz = -std::cos(heading * kPi / 180.0);
		Document d = sceneLookingAlong(fx, fz, 8.0);
		apply(d, Preset::Studio);
		const Float3 right{-fz, 0.0, fx}, forward{fx, 0.0, fz};
		const Float3 toKey = minus(named(d, "Key light")->position, d.camera.target);
		const Float3 toFill = minus(named(d, "Fill light")->position, d.camera.target);
		const Float3 toRim = minus(named(d, "Rim light")->position, d.camera.target);
		EXPECT_LT(dot(toKey, right), 0.0) << "heading " << heading;
		EXPECT_LT(dot(toKey, forward), 0.0) << "the key is on the camera's side of the subject";
		EXPECT_GT(dot(toFill, right), 0.0) << "heading " << heading;
		EXPECT_GT(dot(toRim, forward), 0.0) << "the rim light is behind the subject";
		EXPECT_GT(toKey.y, 0.0);
	}
}

TEST(SceneLightingTest, TheSkyPresetsUseSunAndSkyWithTheSunOnTheRightSideOfTheCamera) {
	Document d = makeStarterScene();   // looks along -Z: the camera's right is +X, behind it +Z
	apply(d, Preset::Sunny);
	ASSERT_EQ(d.lights.size(), 1u);
	ASSERT_TRUE(d.lights[0].physicalSky);
	EXPECT_EQ(d.lights[0].kind, LightKind::Infinite);
	const sky::SunLight sun = sky::sunLight(d.lights[0].sky);
	EXPECT_GT(sun.irradiance, 1.0);
	const Float3 toSun = sky::sunDirection(d.lights[0].sky);
	EXPECT_GT(toSun.x, 0.0) << "to the camera's right";
	EXPECT_GT(toSun.z, 0.0) << "behind the camera (the sun lights what it looks at)";
	EXPECT_GT(toSun.y, 0.5);
	Document golden = makeStarterScene();
	apply(golden, Preset::GoldenHour);
	EXPECT_LT(golden.lights[0].sky.sunElevation, 15.0);
	EXPECT_GT(sky::sunDirection(golden.lights[0].sky).x, 0.5) << "from the side";
	// the Sun light appears once the sky is applied, as the Scene Builder does after an edit
	const std::filesystem::path dir = std::filesystem::path("scene_lighting_skies");
	std::error_code ec;
	std::filesystem::create_directories(dir, ec);
	std::string error;
	ASSERT_TRUE(sky::applySunAndSky(d.lights, 0, dir.string(), error)) << error;
	EXPECT_EQ(d.lights.size(), 2u);
	std::filesystem::remove_all(dir, ec);
}

TEST(SceneLightingTest, AnImageSkyTurnIsWrittenAsARotationAndSurvivesTheFile) {
	Document d = makeStarterScene();
	d.lights[0].imageFile = "sky.hdr";
	const std::string plain = toPbrt(d);
	EXPECT_EQ(plain.find("Rotate 0 0 1"), std::string::npos);
	d.lights[0].skySpin = 90.0;
	const std::string spun = toPbrt(d);
	const size_t rotate = spun.find("Rotate 90 0 1 0");
	ASSERT_NE(rotate, std::string::npos);
	EXPECT_LT(spun.find("AttributeBegin", spun.find("WorldBegin")), rotate);
	EXPECT_LT(rotate, spun.find("LightSource \"infinite\""));
	Document back;
	std::string error;
	ASSERT_TRUE(fromJson(toJson(d), back, error)) << error;
	EXPECT_EQ(back.lights[0].skySpin, 90.0);
	Document old;
	ASSERT_TRUE(fromJson(toJson(makeStarterScene()), old, error)) << error;
	EXPECT_EQ(old.lights[0].skySpin, 0.0);
	EXPECT_EQ(toJson(makeStarterScene()).find("skySpin"), std::string::npos) << "an unturned sky leaves the file as it was";
	d.lights[0].physicalSky = true;
	EXPECT_EQ(toPbrt(d).find("Rotate 90 0 1 0"), std::string::npos) << "a generated sky is tied to its sun and is not turned";
}

// A diffuse plate of albedo rho seen from above under a uniform sky of radiance L reads rho * L (depth 1, nothing else in the scene).
TEST(SceneLightingRenderTest, AnOvercastSkyLightsALevelPlateWithAlbedoTimesTheSkyBrightness) {
	if (findLauncher().empty()) GTEST_SKIP() << "ray_tracer.exe was not found next to the tests";
	Document d;
	Object plate = makeObject(ShapeKind::Quad, "Plate");
	plate.size = {400, 1, 400};
	plate.material.color = {0.5, 0.5, 0.5};
	d.objects.push_back(plate);
	d.camera.position = {0.0, 6.0, 0.0};
	d.camera.target = {0.0, 0.0, 0.0};
	d.camera.up = {0.0, 0.0, -1.0};
	d.camera.fov = 8.0;
	apply(d, Preset::Overcast);
	const Light sky = d.lights[0];
	std::vector<float> rgb;
	int w = 0, h = 0;
	ASSERT_TRUE(render(d, "overcast", 24, rgb, w, h));
	const double colour[3] = {sky.color.r, sky.color.g, sky.color.b};
	for (int c = 0; c < 3; ++c) {
		double mean = 0;
		for (size_t i = 0; i < rgb.size() / 3; ++i) mean += rgb[3 * i + c];
		mean /= static_cast<double>(rgb.size() / 3);
		const double want = 0.5 * colour[c] * sky.intensity;   // (the colours are stored as linear values, which is what the renderer reads)
		EXPECT_NEAR(mean, want, 0.05 * want) << "channel " << c;
	}
}

// A picture that is bright towards +X and dark elsewhere lights the +X side of a ball; turned half way round it lights the other side.
TEST(SceneLightingRenderTest, TurningAnImageSkyHalfWayRoundMovesItsLightToTheOtherSideOfABall) {
	if (findLauncher().empty()) GTEST_SKIP() << "ray_tracer.exe was not found next to the tests";
	const int iw = 64, ih = 32;
	std::vector<float> img(static_cast<size_t>(iw) * ih * 3, 0.0f);
	for (int row = 0; row < ih; ++row)
		for (int col = 0; col < iw; ++col) {
			const double phi = (col + 0.5) / iw * 2.0 * kPi;   // 0 towards +X, 90 degrees towards -Z (the sky picture's own convention)
			const bool towardsPlusX = std::cos(phi) > 0.0;
			for (int c = 0; c < 3; ++c) img[(static_cast<size_t>(row) * iw + col) * 3 + c] = towardsPlusX ? 2.0f : 0.0f;
		}
	const std::filesystem::path path = std::filesystem::absolute("scene_lighting_halfsky.hdr");
	{
		std::ofstream out(path, std::ios::binary);
		out << sky::encodeHdr(img, iw, ih);
	}
	double side[2][2] = {};   // [spin 0 or 180][left or right half of the ball]
	int index = 0;
	for (double spin : {0.0, 180.0}) {
		Document d;
		Object ball = makeObject(ShapeKind::Sphere, "Ball");
		ball.radius = 1.0;
		ball.material.color = {0.8, 0.8, 0.8};
		d.objects.push_back(ball);
		d.camera.position = {0.0, 0.0, 6.0};   // looks along -Z: +X is on the right of the picture
		d.camera.target = {0.0, 0.0, 0.0};
		d.camera.fov = 25.0;
		Light l;
		l.name = "Sky";
		l.kind = LightKind::Infinite;
		l.imageFile = path.string();
		l.intensity = 1.0;
		l.skySpin = spin;
		d.lights.push_back(l);
		std::vector<float> rgb;
		int w = 0, h = 0;
		ASSERT_TRUE(render(d, "halfsky", 48, rgb, w, h));
		for (int y = h / 3; y < 2 * h / 3; ++y)
			for (int x = 0; x < w; ++x) {
				if (std::abs(x - w / 2) >= w / 8) continue;   // the middle of the picture, where the ball is
				side[index][x < w / 2 ? 0 : 1] += rgb[(static_cast<size_t>(y) * w + x) * 3 + 1];
			}
		++index;
	}
	std::remove(path.string().c_str());
	EXPECT_GT(side[0][1], 1.5 * side[0][0]) << "unturned, the +X side (the right) is the lit one";
	EXPECT_GT(side[1][0], 1.5 * side[1][1]) << "turned half way round, the left is";
}
