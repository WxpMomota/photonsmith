// light_link_tests.cpp -- light linking and shadow linking (src/shared/light_link.h): what the parser and the flattener make of the words, and what each path tracer
// renders with them. The scenes: a camera looking down at a floor with two tiles, A (group "a") on one side and B (group "b") on the other, lit from above.
#include <gtest/gtest.h>

#include "agreement_test_helpers.h"
#include "pbrt_flatten_test_helpers.h"
#include "../../src/shared/light_link.h"
#include "../../src/shared/cryptomatte.h"
#include "../../src/shared/exr_writer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

// The scene, with `light` (one or more light lines) and `extra` (more shapes).
std::string scene(const std::string &light, const std::string &extra = "") {
	std::ostringstream s;
	s << "LookAt 0 3 0  0 0 0  0 0 1\n"
	  << "Camera \"perspective\" \"float fov\" [ 60 ]\n"
	  << "Film \"rgb\" \"integer xresolution\" [ 64 ] \"integer yresolution\" [ 64 ]\n"
	  << "WorldBegin\n"
	  << light
	  << "AttributeBegin\n  Material \"diffuse\" \"rgb reflectance\" [ 0.5 0.5 0.5 ]\n"
	  << "  Shape \"trianglemesh\" \"integer indices\" [ 0 1 2 0 2 3 ] \"point3 P\" [ -6 0 -6  6 0 -6  6 0 6  -6 0 6 ]\nAttributeEnd\n"
	  << "AttributeBegin\n  Material \"diffuse\" \"rgb reflectance\" [ 0.8 0.2 0.2 ]\n"
	  << "  Shape \"trianglemesh\" \"string linkgroup\" \"a\" \"integer indices\" [ 0 1 2 0 2 3 ] \"point3 P\" [ -1.5 0.05 -1  -0.3 0.05 -1  -0.3 0.05 1  -1.5 0.05 1 ]\nAttributeEnd\n"
	  << "AttributeBegin\n  Material \"diffuse\" \"rgb reflectance\" [ 0.2 0.2 0.8 ]\n"
	  << "  Shape \"trianglemesh\" \"string linkgroup\" \"b\" \"integer indices\" [ 0 1 2 0 2 3 ] \"point3 P\" [ 0.3 0.05 -1  1.5 0.05 -1  1.5 0.05 1  0.3 0.05 1 ]\nAttributeEnd\n"
	  << extra;
	return s.str();
}

const char *kPoint = "LightSource \"point\" \"point3 from\" [ 0 4 0 ] \"rgb I\" [ 20 20 20 ]";
// a small square lamp above the camera, facing down
std::string areaLight(const std::string &words) {
	return "AttributeBegin\n  AreaLightSource \"diffuse\" \"rgb L\" [ 30 30 30 ] \"bool twosided\" true " + words +
		   "\n  Shape \"trianglemesh\" \"integer indices\" [ 0 2 1 0 3 2 ] \"point3 P\" [ -0.4 4 -0.4  0.4 4 -0.4  0.4 4 0.4  -0.4 4 0.4 ]\nAttributeEnd\n";
}

// Renders `text` through the launcher (`--cpu` or `--gpu`) at 64 x 64 and `spp` samples to `depth` bounces; false when it could not.
bool render(const std::string &text, const char *tag, const char *backend, int spp, int depth, std::vector<float> &rgb) {
	const std::string launcher = findRayTracerLauncher();
	if (launcher.empty()) return false;
	const std::string scenePath = std::string("light_link_") + tag + ".pbrt", outPath = std::string("light_link_") + tag + ".exr", logPath = scenePath + ".log";
	{
		std::ofstream out(scenePath, std::ios::binary);
		out << text;
	}
	std::remove(outPath.c_str());
	std::ostringstream cmd;
	cmd << "\"" << launcher << "\" " << backend << " --output \"" << outPath << "\" 64 " << spp << " " << depth << " \"" << scenePath << "\" > \"" << logPath << "\" 2>&1";
	std::string command = cmd.str();
#ifdef _WIN32
	command = "\"" + command + "\"";
#endif
	int w = 0, h = 0;
	const bool ran = std::system(command.c_str()) == 0 && loadLinearRgbPixels(outPath, w, h, rgb) && w == 64 && h == 64;
	std::remove(scenePath.c_str());
	std::remove(outPath.c_str());
	std::remove((outPath + ".run_marker.txt").c_str());
	std::remove(logPath.c_str());
	return ran;
}

// The light in the picture's left and right halves (the two tiles are on the two sides).
struct Halves {
	double left = 0.0, right = 0.0;
};
Halves halves(const std::vector<float> &rgb) {
	Halves s;
	for (int y = 0; y < 64; ++y)
		for (int x = 0; x < 64; ++x) {
			const std::size_t i = (static_cast<std::size_t>(y) * 64 + x) * 3;
			(x < 32 ? s.left : s.right) += rgb[i] + rgb[i + 1] + rgb[i + 2];
		}
	return s;
}

// The rendering checks, for one backend: a light that lights only "a" leaves B's side black (and so the floor, which is in no group); "b" the other side; the three
// together are the unlinked light. `depth` 2 also has B's bounces reach the lamp: they must not add its light either.
void checkLinking(const char *backend, const std::string &light, const char *tag, int depth) {
	std::vector<float> all, onlyA, onlyB, every;
	ASSERT_TRUE(render(scene(light + "\n"), (std::string(tag) + "_all").c_str(), backend, 16, depth, all));
	ASSERT_TRUE(render(scene(light + " \"string illuminates\" [ \"a\" ]\n"), (std::string(tag) + "_a").c_str(), backend, 16, depth, onlyA));
	ASSERT_TRUE(render(scene(light + " \"string illuminates\" [ \"b\" ]\n"), (std::string(tag) + "_b").c_str(), backend, 16, depth, onlyB));
	ASSERT_TRUE(render(scene(light + " \"string illuminates\" [ \"a\" \"b\" \"rest\" ]\n"), (std::string(tag) + "_every").c_str(), backend, 16, depth, every));
	const Halves h0 = halves(all), ha = halves(onlyA), hb = halves(onlyB), he = halves(every);
	ASSERT_GT(h0.left, 1.0);
	ASSERT_GT(h0.right, 1.0);
	// which side A is on: the side that stays lit with "a"
	const bool aLeft = ha.left > ha.right;
	const double aLit = aLeft ? ha.left : ha.right, aDark = aLeft ? ha.right : ha.left;
	const double bLit = aLeft ? hb.right : hb.left, bDark = aLeft ? hb.left : hb.right;
	EXPECT_GT(aLit, 0.05 * (aLeft ? h0.left : h0.right)) << tag << ": A is lit";
	EXPECT_EQ(aDark, 0.0) << tag << ": B and the floor get none of the light";
	EXPECT_GT(bLit, 0.05 * (aLeft ? h0.right : h0.left)) << tag << ": with \"b\", B is lit";
	EXPECT_EQ(bDark, 0.0) << tag << ": and A's side is dark";
	EXPECT_NEAR(he.left + he.right, h0.left + h0.right, 0.03 * (h0.left + h0.right)) << tag << ": every group named is the unlinked light";
	EXPECT_LT(aLit, aLeft ? h0.left : h0.right) << tag << ": the floor round A is dark now";
}

// Shadow linking: a blocker beside the lamp (outside the picture) shadows the floor; when only the shapes in no group cast the light's shadows and the blocker is in
// a group, the floor is as bright as with no blocker at all.
void checkShadowLinking(const char *backend, const char *tag) {
	const std::string light = "LightSource \"point\" \"point3 from\" [ 6 4 0 ] \"rgb I\" [ 60 60 60 ]";
	const std::string blocker = "AttributeBegin\n  Material \"diffuse\"\n  Shape \"trianglemesh\" \"string linkgroup\" \"blocker\" \"integer indices\" [ 0 1 2 0 2 3 ] "
								"\"point3 P\" [ 4.5 3 -0.6  5.5 3 -0.6  5.5 3 0.6  4.5 3 0.6 ]\nAttributeEnd\n";
	std::vector<float> open, shadowed, linked;
	ASSERT_TRUE(render(scene(light + "\n"), (std::string(tag) + "_open").c_str(), backend, 16, 1, open));
	ASSERT_TRUE(render(scene(light + "\n", blocker), (std::string(tag) + "_shadowed").c_str(), backend, 16, 1, shadowed));
	ASSERT_TRUE(render(scene(light + " \"string shadowsfrom\" [ \"rest\" \"a\" \"b\" ]\n", blocker), (std::string(tag) + "_linked").c_str(), backend, 16, 1, linked));
	const Halves ho = halves(open), hs = halves(shadowed), hl = halves(linked);
	const double o = ho.left + ho.right, s = hs.left + hs.right, l = hl.left + hl.right;
	EXPECT_LT(s, 0.9 * o) << tag << ": the blocker's shadow falls in the picture";
	EXPECT_NEAR(l, o, 0.01 * o) << tag << ": not one of the groups that cast the light's shadows, it casts none";
}

}  // namespace

// ---- the words: parser and flattener ----

TEST(LightLink, AShapesGroupGivesItACopyOfItsMaterialAndALightsWordsBecomeMasks) {
	const pbrt_flatten::FlatScene s = flattenSource(scene(std::string(kPoint) + " \"string illuminates\" [ \"b\" \"rest\" ] \"string shadowsfrom\" [ \"a\" ]\n" + areaLight("\"string illuminates\" [ \"a\" ]")));
	ASSERT_EQ(s.punctualLights.size(), 1u);
	// "a" and "b" are groups 1 and 2, in the order the scene names them
	EXPECT_EQ(s.punctualLights[0].illuminates, (1ull << 2) | 1ull);
	EXPECT_EQ(s.punctualLights[0].shadowsFrom, 1ull << 1);
	ASSERT_EQ(s.areaLights.size(), 1u);
	EXPECT_EQ(s.areaLights[0].illuminates, 1ull << 1);
	int groupA = 0, groupB = 0, rest = 0;
	for (const auto &m : s.materials) {
		if (m.linkGroup == 1) ++groupA;
		else if (m.linkGroup == 2) ++groupB;
		else ++rest;
	}
	EXPECT_EQ(groupA, 1) << "the tile of group a has its own material";
	EXPECT_EQ(groupB, 1);
	EXPECT_GE(rest, 1) << "the floor's stays as it was";
	EXPECT_FALSE(warnedAbout(s, "linkgroup of no shape"));

	const pbrt_flatten::FlatScene unknown = flattenSource(scene(std::string(kPoint) + " \"string illuminates\" [ \"c\" ]\n"));
	EXPECT_TRUE(warnedAbout(unknown, "linkgroup of no shape")) << "a group no shape is in is said";
	EXPECT_EQ(unknown.punctualLights[0].illuminates, 0ull) << "and lights nothing";
	const pbrt_flatten::FlatScene plain = flattenSource(scene(std::string(kPoint) + "\n"));
	EXPECT_EQ(plain.punctualLights[0].illuminates, light_link::kAll);
	EXPECT_EQ(plain.punctualLights[0].shadowsFrom, light_link::kAll);
}

TEST(LightLink, CoversIsTheMasksBitAndTheCameraSeesEveryLight) {
	EXPECT_TRUE(light_link::covers(1ull << 3, 3));
	EXPECT_FALSE(light_link::covers(1ull << 3, 2));
	EXPECT_TRUE(light_link::covers(0ull, -1)) << "the camera";
	EXPECT_TRUE(light_link::covers(light_link::kAll, 63));
}

// ---- the CPU path tracer ----

TEST(LightLinkCpu, APointLightLightsOnlyItsGroups) {
	if (findRayTracerLauncher().empty()) GTEST_SKIP() << "ray_tracer was not found";
	checkLinking("--cpu", kPoint, "cpu_point", 1);
}

TEST(LightLinkCpu, AnAreaLightLightsOnlyItsGroupsByLightSamplingAndByBounces) {
	if (findRayTracerLauncher().empty()) GTEST_SKIP() << "ray_tracer was not found";
	// the AreaLightSource's words go in its line: areaLight() puts them there
	for (int depth : {1, 2}) {
		std::vector<float> all, onlyA;
		ASSERT_TRUE(render(scene(areaLight("")), "cpu_area_all", "--cpu", 32, depth, all));
		ASSERT_TRUE(render(scene(areaLight("\"string illuminates\" [ \"a\" ]")), "cpu_area_a", "--cpu", 32, depth, onlyA));
		const Halves h0 = halves(all), ha = halves(onlyA);
		const bool aLeft = ha.left > ha.right;
		EXPECT_GT(aLeft ? ha.left : ha.right, 0.05 * (aLeft ? h0.left : h0.right)) << "depth " << depth;
		EXPECT_EQ(aLeft ? ha.right : ha.left, 0.0) << "depth " << depth << ": B's side gets nothing, by light sampling or by a bounce that finds the lamp";
	}
}

TEST(LightLinkCpu, AShapeOutsideAPointLightsShadowGroupsCastsNoShadow) {
	if (findRayTracerLauncher().empty()) GTEST_SKIP() << "ray_tracer was not found";
	checkShadowLinking("--cpu", "cpu_shadow");
}

// ---- the two GPU path tracers (the recursive renderer, the default, and --wavefront): the same checks ----

namespace {
bool haveGpu() { return !findRayTracerLauncher().empty() && std::getenv("RT_FORBID_GPU") == nullptr; }
void checkAreaLight(const char *backend, const char *tag) {
	for (int depth : {1, 2}) {
		std::vector<float> all, onlyA;
		ASSERT_TRUE(render(scene(areaLight("")), (std::string(tag) + "_all").c_str(), backend, 32, depth, all));
		ASSERT_TRUE(render(scene(areaLight("\"string illuminates\" [ \"a\" ]")), (std::string(tag) + "_a").c_str(), backend, 32, depth, onlyA));
		const Halves h0 = halves(all), ha = halves(onlyA);
		const bool aLeft = ha.left > ha.right;
		EXPECT_GT(aLeft ? ha.left : ha.right, 0.05 * (aLeft ? h0.left : h0.right)) << tag << " depth " << depth;
		EXPECT_EQ(aLeft ? ha.right : ha.left, 0.0) << tag << " depth " << depth << ": B's side gets nothing, by light sampling or by a bounce that finds the lamp";
	}
}
}  // namespace

TEST(LightLinkGpu, ThePointLightAreaLightAndShadowLinkingOnTheRecursiveRenderer) {
	if (!haveGpu()) GTEST_SKIP() << "no launcher, or the GPU is not for this run";
	checkLinking("--gpu", kPoint, "gpu_point", 1);
	checkAreaLight("--gpu", "gpu_area");
	checkShadowLinking("--gpu", "gpu_shadow");
}

TEST(LightLinkGpu, ThePointLightAreaLightAndShadowLinkingOnTheWavefrontRenderer) {
	if (!haveGpu()) GTEST_SKIP() << "no launcher, or the GPU is not for this run";
	checkLinking("--gpu --wavefront", kPoint, "wf_point", 1);
	checkAreaLight("--gpu --wavefront", "wf_area");
	checkShadowLinking("--gpu --wavefront", "wf_shadow");
}

// ---- the ID passes (aov_pass.h renderIds, src/shared/cryptomatte.h): an object is a shape's link group, a material its name ----


TEST(Cryptomatte, TheHashIsMurmurHash3AndTheFloatIsNeverDenormalInfiniteOrNan) {
	EXPECT_EQ(cryptomatte::murmur3_32(""), 0u);
	EXPECT_EQ(cryptomatte::murmur3_32("hello"), 0x248bfa47u) << "MurmurHash3_x86_32's published value";
	EXPECT_EQ(cryptomatte::murmur3_32("The quick brown fox jumps over the lazy dog"), 0x2e4ff723u);
	for (std::uint32_t h : {0u, 0x00400000u, 0x7f800000u, 0xff800001u, 0x12345678u}) {
		const float f = cryptomatte::idToFloat(h);
		EXPECT_TRUE(std::isfinite(f)) << h;
		EXPECT_TRUE(f == 0.0f || std::fabs(f) >= std::numeric_limits<float>::min()) << h << ": not denormal";
	}
	EXPECT_EQ(cryptomatte::layerKey("CryptoObject").size(), 7u);
	EXPECT_EQ(cryptomatte::manifestJson({{"a\"b", 0x10u}}), "{\"a\\\"b\":\"00000010\"}");
}

TEST(LightLink, CastNamesEveryGroupUnderIt) {
	const pbrt_flatten::FlatScene s = flattenSource(
		"WorldBegin\n" + std::string(kPoint) + " \"string illuminates\" [ \"cast\" ]\n"
		"Shape \"sphere\" \"string linkgroup\" \"cast/pip\"\nShape \"sphere\" \"string linkgroup\" \"set\"\nShape \"sphere\" \"string linkgroup\" \"cast/ann\"\nShape \"sphere\" \"string linkgroup\" \"castle\"\n");
	ASSERT_EQ(s.linkGroups.size(), 4u);
	EXPECT_EQ(s.punctualLights[0].illuminates, (1ull << 1) | (1ull << 3)) << "cast/pip and cast/ann, not set nor castle";
}

TEST(IdPassesCpu, EachTilesObjectAndMaterialAreItsOwnInTheCryptomatteLayers) {
	const std::string launcher = findRayTracerLauncher();
	if (launcher.empty()) GTEST_SKIP() << "ray_tracer was not found";
	const std::string text = scene(std::string(kPoint) + "\n");
	const std::string scenePath = "id_passes_test.pbrt", outPath = "id_passes_test.exr";
	{
		std::ofstream out(scenePath, std::ios::binary);
		out << text;
	}
	std::remove(outPath.c_str());
	std::string command = "\"" + launcher + "\" --cpu --aovs --output \"" + outPath + "\" 64 8 1 \"" + scenePath + "\" > id_passes_test.log 2>&1";
#ifdef _WIN32
	command = "\"" + command + "\"";
#endif
	ASSERT_EQ(std::system(command.c_str()), 0);
	int w = 0, h = 0;
	std::vector<std::string> names;
	std::vector<std::vector<float>> planes;
	std::vector<std::pair<std::string, std::string>> attributes;
	std::string error;
	ASSERT_TRUE(read_exr_channels(outPath, w, h, names, planes, error, &attributes)) << error;
	for (const char *f : {"id_passes_test.pbrt", "id_passes_test.exr", "id_passes_test.exr.run_marker.txt", "id_passes_test.log"}) std::remove(f);
	const auto plane = [&](const char *name) -> const std::vector<float> & {
		const auto it = std::find(names.begin(), names.end(), name);
		EXPECT_NE(it, names.end()) << name;
		return planes[static_cast<std::size_t>(it - names.begin())];
	};
	const std::vector<float> &obj = plane("CryptoObject00.R"), &cov = plane("CryptoObject00.G"), &mat = plane("CryptoMaterial00.R"), &idObj = plane("id.object");
	const float a = cryptomatte::idToFloat(cryptomatte::murmur3_32("a")), b = cryptomatte::idToFloat(cryptomatte::murmur3_32("b"));
	const float rest = cryptomatte::idToFloat(cryptomatte::murmur3_32("rest"));
	int seenA = 0, seenB = 0, seenRest = 0, fullA = 0;
	std::set<float> materials;
	for (std::size_t i = 0; i < obj.size(); ++i) {
		if (obj[i] == a) {
			++seenA;
			if (cov[i] > 0.99f) ++fullA;
		}
		seenB += obj[i] == b;
		seenRest += obj[i] == rest;
		EXPECT_EQ(idObj[i], obj[i]) << "id.object is the one that covers most";
		materials.insert(mat[i]);
	}
	EXPECT_GT(seenA, 50) << "tile A's pixels are object \"a\"";
	EXPECT_GT(seenB, 50);
	EXPECT_GT(seenRest, 500) << "the floor is in no group: \"rest\"";
	EXPECT_GT(fullA, 30) << "inside the tile, all of the pixel";
	EXPECT_GE(materials.size(), 3u) << "three materials: the floor's and the two tiles'";
	bool manifest = false;
	for (const auto &[key, value] : attributes)
		if (key.find("/manifest") != std::string::npos && value.find("\"a\"") != std::string::npos && value.find("\"rest\"") != std::string::npos) manifest = true;
	EXPECT_TRUE(manifest) << "the header names the objects (Cryptomatte metadata)";
}

// ---- light groups (src/shared/light_groups.h): the picture's light split by which lights it came from, as passes beside it ----

namespace {
// A key point light, a fill lamp and a sky, each in its own group (or `only` of them, when given), rendered to `out` (.exr, or .png for the sidecar file).
std::string groupScene(const std::string &only = "") {
	std::string lights;
	if (only.empty() || only == "key") lights += "LightSource \"point\" \"point3 from\" [ -2 3 0 ] \"rgb I\" [ 12 10 8 ] \"string lightgroup\" \"key\"\n";
	if (only.empty() || only == "fill") lights += areaLight("\"string lightgroup\" \"fill\"");
	if (only.empty() || only == "sky") lights += "LightSource \"infinite\" \"rgb L\" [ 0.2 0.25 0.35 ] \"string lightgroup\" \"sky\"\n";
	return scene(lights);
}
bool renderPasses(const std::string &text, const std::string &tag, const char *backend, const std::string &ext, std::vector<std::string> &names,
				  std::vector<std::vector<float>> &planes) {
	const std::string launcher = findRayTracerLauncher();
	if (launcher.empty()) return false;
	const std::string scenePath = "light_groups_" + tag + ".pbrt", outPath = "light_groups_" + tag + ext, passPath = ext == ".exr" ? outPath : outPath + ".lightgroups.exr";
	{
		std::ofstream out(scenePath, std::ios::binary);
		out << text;
	}
	std::remove(passPath.c_str());
	std::string command = "\"" + launcher + "\" " + backend + " --output \"" + outPath + "\" 64 32 3 \"" + scenePath + "\" > \"" + scenePath + ".log\" 2>&1";
#ifdef _WIN32
	command = "\"" + command + "\"";
#endif
	int w = 0, h = 0;
	std::string error;
	const bool ok = std::system(command.c_str()) == 0 && read_exr_channels(passPath, w, h, names, planes, error) && w == 64 && h == 64;
	for (const std::string &f : {scenePath, outPath, passPath, outPath + ".run_marker.txt", scenePath + ".log"}) std::remove(f.c_str());
	return ok;
}
double total(const std::vector<std::string> &names, const std::vector<std::vector<float>> &planes, const std::string &prefix) {
	double t = 0.0;
	for (std::size_t i = 0; i < names.size(); ++i)
		if (names[i] == prefix + "R" || names[i] == prefix + "G" || names[i] == prefix + "B")
			for (float v : planes[i]) t += v;
	return t;
}
// The passes are there, each group's has light, they add up to the picture (every light is in a group), and the key's pass is the picture the key light alone makes.
void checkLightGroups(const char *backend, const std::string &tag) {
	std::vector<std::string> names, aloneNames;
	std::vector<std::vector<float>> planes, alonePlanes;
	ASSERT_TRUE(renderPasses(groupScene(), tag, backend, ".exr", names, planes)) << tag;
	for (const char *g : {"key", "fill", "sky"})
		for (const char *c : {".R", ".G", ".B"})
			ASSERT_NE(std::find(names.begin(), names.end(), std::string("lightgroup.") + g + c), names.end()) << tag << ": " << g << c;
	const double beauty = total(names, planes, ""), key = total(names, planes, "lightgroup.key."), fill = total(names, planes, "lightgroup.fill."),
				 sky = total(names, planes, "lightgroup.sky.");
	EXPECT_GT(key, 0.05 * beauty) << tag;
	EXPECT_GT(fill, 0.05 * beauty) << tag;
	EXPECT_GT(sky, 0.01 * beauty) << tag;
	EXPECT_NEAR(key + fill + sky, beauty, 0.01 * beauty) << tag << ": the passes add up to the picture";
	ASSERT_TRUE(renderPasses(groupScene("key"), tag + "_key", backend, ".exr", aloneNames, alonePlanes)) << tag;
	const double alone = total(aloneNames, alonePlanes, "");
	EXPECT_NEAR(key, alone, 0.04 * alone) << tag << ": the key's pass is the key light's own picture";
}
}  // namespace

TEST(LightGroupsCpu, EachGroupsPassIsItsLightsAndThePassesAddUpToThePicture) {
	if (findRayTracerLauncher().empty()) GTEST_SKIP() << "ray_tracer was not found";
	checkLightGroups("--cpu", "cpu");
}

TEST(LightGroupsCpu, BesideAPngThePassesGoInTheirOwnExr) {
	if (findRayTracerLauncher().empty()) GTEST_SKIP() << "ray_tracer was not found";
	std::vector<std::string> names;
	std::vector<std::vector<float>> planes;
	ASSERT_TRUE(renderPasses(groupScene(), "cpu_png", "--cpu", ".png", names, planes));
	EXPECT_EQ(names.size(), 9u) << "three groups, three channels each, and no picture";
	EXPECT_EQ(std::find(names.begin(), names.end(), "R"), names.end());
}

TEST(LightGroupsGpu, TheWavefrontRendererSplitsTheLightAndAGpuRenderGoesToIt) {
	if (!haveGpu()) GTEST_SKIP() << "no launcher, or the GPU is not for this run";
	checkLightGroups("--gpu --wavefront", "wf");
	checkLightGroups("--gpu", "gpu");   // (asked for the default renderer: the scene's groups send it to the wavefront renderer)
}

// ---- ray visibility (Cycles' object visibility: a shape's `"bool camera" false`, `"bool shadow" false`) ----

namespace {
// A green card over tile A, between it and the point light (and the camera): seen or not, shadowing or not.
std::string withCard(const std::string &words) {
	return scene(std::string(kPoint) + "\n", "AttributeBegin\n  Material \"diffuse\" \"rgb reflectance\" [ 0.1 0.9 0.1 ]\n"
												"  Shape \"trianglemesh\" " + words + " \"integer indices\" [ 0 1 2 0 2 3 ] \"point3 P\" [ -1.6 1 -1.1  -0.2 1 -1.1  -0.2 1 1.1  -1.6 1 1.1 ]\nAttributeEnd\n");
}
struct Side {
	double red = 0.0, green = 0.0;
};
Side leftSide(const std::vector<float> &rgb) {   // (tile A's side of the picture)
	Side s;
	for (int y = 8; y < 56; ++y)
		for (int x = 0; x < 32; ++x) {
			const std::size_t i = (static_cast<std::size_t>(y) * 64 + x) * 3;
			s.red += rgb[i], s.green += rgb[i + 1];
		}
	return s;
}
void checkVisibility(const char *backend, const std::string &tag) {
	std::vector<float> none, seen, hidden, ghost;
	ASSERT_TRUE(render(scene(std::string(kPoint) + "\n"), (tag + "_none").c_str(), backend, 16, 1, none));
	ASSERT_TRUE(render(withCard(""), (tag + "_seen").c_str(), backend, 16, 1, seen));
	ASSERT_TRUE(render(withCard("\"bool camera\" false"), (tag + "_hidden").c_str(), backend, 16, 1, hidden));
	ASSERT_TRUE(render(withCard("\"bool camera\" false \"bool shadow\" false"), (tag + "_ghost").c_str(), backend, 16, 1, ghost));
	const Side n = leftSide(none), s = leftSide(seen), h = leftSide(hidden), g = leftSide(ghost);
	EXPECT_GT(s.green, s.red) << tag << ": the card is seen (green)";
	EXPECT_LT(h.green, n.green * 1.02 + 0.5) << tag << ": hidden from the camera: none of its green is seen";
	EXPECT_LT(h.red, 0.8 * n.red) << tag << ": but it still casts its shadow on the tile";
	EXPECT_NEAR(g.red, n.red, 0.03 * n.red) << tag << ": hidden and casting no shadow: as if it were not there";
}
}  // namespace

TEST(RayVisibilityCpu, AShapeHiddenFromTheCameraStillShadowsUnlessItCastsNone) {
	if (findRayTracerLauncher().empty()) GTEST_SKIP() << "ray_tracer was not found";
	checkVisibility("--cpu", "vis_cpu");
}

TEST(RayVisibilityGpu, TheSameOnBothOptixRenderers) {
	if (!haveGpu()) GTEST_SKIP() << "no launcher, or the GPU is not for this run";
	checkVisibility("--gpu", "vis_gpu");
	checkVisibility("--gpu --wavefront", "vis_wf");
}
