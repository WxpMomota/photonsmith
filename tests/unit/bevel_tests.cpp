// bevel_tests.cpp -- the bevel shader (src/shared/bevel.h, Cycles' bevel): a cube's corner seen edge-on, lit only from +x, so one face is lit and the other dark; with a bevel
// the dark face catches light in a band along the edge (its normals lean round it) and nowhere else. On the CPU path tracer and both OptiX renderers.
#include <gtest/gtest.h>

#include "agreement_test_helpers.h"
#include "../../src/shared/bevel.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string cube(double bevel) {
	std::ostringstream s;
	s << "LookAt 3 0.3 3  0 0 0  0 1 0\nCamera \"perspective\" \"float fov\" [ 30 ]\n"
	  << "Film \"rgb\" \"integer xresolution\" [ 64 ] \"integer yresolution\" [ 64 ]\nWorldBegin\n"
	  << "LightSource \"distant\" \"point3 from\" [ 1 0 0 ] \"point3 to\" [ 0 0 0 ] \"rgb L\" [ 3 3 3 ]\n"
	  << "Material \"diffuse\" \"rgb reflectance\" [ 0.7 0.7 0.7 ] \"float bevel\" [ " << bevel << " ]\n"
	  << "Shape \"trianglemesh\" \"integer indices\" [ 0 1 2 0 2 3  4 6 5 4 7 6  8 9 10 8 10 11  12 14 13 12 15 14  16 18 17 16 19 18  20 22 21 20 23 22 ]\n"
	  << "  \"point3 P\" [ 1 -1 -1  1 1 -1  1 1 1  1 -1 1   -1 -1 -1  -1 1 -1  -1 1 1  -1 -1 1   -1 -1 1  1 -1 1  1 1 1  -1 1 1   -1 -1 -1  1 -1 -1  1 1 -1  -1 1 -1"
	  << "   -1 1 -1  1 1 -1  1 1 1  -1 1 1   -1 -1 -1  -1 -1 1  1 -1 1  1 -1 -1 ]\n";
	return s.str();
}

bool render(const std::string &text, const char *tag, const char *backend, std::vector<float> &rgb) {
	const std::string launcher = findRayTracerLauncher();
	if (launcher.empty()) return false;
	const std::string scenePath = std::string("bevel_") + tag + ".pbrt", outPath = std::string("bevel_") + tag + ".exr";
	std::ofstream(scenePath, std::ios::binary) << text;
	std::remove(outPath.c_str());
	std::string command = "\"" + launcher + "\" " + backend + " --output \"" + outPath + "\" 64 64 1 \"" + scenePath + "\" > \"" + scenePath + ".log\" 2>&1";
#ifdef _WIN32
	command = "\"" + command + "\"";
#endif
	int w = 0, h = 0;
	const bool ok = std::system(command.c_str()) == 0 && loadLinearRgbPixels(outPath, w, h, rgb) && w == 64 && h == 64;
	for (const std::string &f : {scenePath, outPath, outPath + ".run_marker.txt", scenePath + ".log"}) std::remove(f.c_str());
	return ok;
}

// The light in each column over the middle rows.
std::vector<double> columns(const std::vector<float> &rgb) {
	std::vector<double> c(64, 0.0);
	for (int y = 22; y < 42; ++y)
		for (int x = 0; x < 64; ++x) c[static_cast<std::size_t>(x)] += rgb[(static_cast<std::size_t>(y) * 64 + x) * 3 + 1] / 20.0;
	return c;
}

void checkBevel(const char *backend, const char *tag) {
	std::vector<float> plain, round;
	ASSERT_TRUE(render(cube(0.0), (std::string(tag) + "_plain").c_str(), backend, plain)) << tag;
	ASSERT_TRUE(render(cube(0.25), (std::string(tag) + "_round").c_str(), backend, round)) << tag;
	const std::vector<double> a = columns(plain), b = columns(round);
	// the edge: the first dark column after the lit face (the +x face, lit, is on the picture's left from this camera)
	int edge = -1;
	bool lit = false;
	for (int x = 0; x < 64 && edge < 0; ++x) {   // (the edge's own column may be part lit: the first wholly dark one)
		lit = lit || a[static_cast<std::size_t>(x)] > 0.3;
		if (lit && a[static_cast<std::size_t>(x)] < 0.05) edge = x;
	}
	ASSERT_GT(edge, 10) << tag << ": the corner is in the middle of the picture";
	ASSERT_LT(edge, 54) << tag;
	EXPECT_LT(a[static_cast<std::size_t>(edge)], 0.05) << tag << ": without a bevel the dark face is dark to its edge";
	EXPECT_GT(b[static_cast<std::size_t>(edge)], 0.03) << tag << ": with one, its edge catches the light";
	EXPECT_NEAR(b[static_cast<std::size_t>(edge + 10)], a[static_cast<std::size_t>(edge + 10)], 0.02) << tag << ": and away from the edge nothing changes";
	EXPECT_NEAR(b[static_cast<std::size_t>(edge - 12)], a[static_cast<std::size_t>(edge - 12)], 0.05 * a[static_cast<std::size_t>(edge - 12)] + 0.02) << tag;
}

bool haveGpu() { return !findRayTracerLauncher().empty() && std::getenv("RT_FORBID_GPU") == nullptr; }

}  // namespace

TEST(Bevel, TheFalloffIsANormalisedCubicAndItsSamplesFallInside) {
	double integral = 0.0;
	for (int i = 0; i < 10000; ++i) {   // 2 pi r f(r) dr over the disk is 1
		const double r = (i + 0.5) / 10000.0 * 0.3;
		integral += 2.0 * 3.14159265358979 * r * bevel::cubicPdf(0.3, r) * 0.3 / 10000.0;
	}
	EXPECT_NEAR(integral, 1.0, 1e-3);
	for (double xi : {0.01, 0.3, 0.7, 0.99}) {
		double r = 0.0, h = 0.0;
		bevel::cubicSample(0.3, xi, r, h);
		EXPECT_GE(r, 0.0);
		EXPECT_LE(r, 0.3);
		EXPECT_NEAR(r * r + h * h, 0.09, 1e-9);
	}
}

TEST(BevelCpu, ACubesDarkFaceCatchesTheLightAlongItsEdgeOnly) {
	if (findRayTracerLauncher().empty()) GTEST_SKIP() << "ray_tracer was not found";
	checkBevel("--cpu", "cpu");
}

TEST(BevelGpu, TheSameOnBothOptixRenderers) {
	if (!haveGpu()) GTEST_SKIP() << "no launcher, or the GPU is not for this run";
	checkBevel("--gpu", "gpu");
	checkBevel("--gpu --wavefront", "wf");
}
