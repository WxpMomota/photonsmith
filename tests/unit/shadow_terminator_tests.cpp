// shadow_terminator_tests.cpp -- the shadow terminator offset (src/shared/shadow_terminator.h, Cycles'): a coarse sphere with smooth normals, lit from the side, shows notches
// in its shadow's edge where the next facet hides the light from a point the smooth shading says is lit; with `"float terminator"` its shadow rays near the terminator
// start from the round surface and the notches go. Checked against a finely tessellated sphere, on the CPU path tracer and both OptiX renderers.
#include <gtest/gtest.h>

#include "agreement_test_helpers.h"
#include "../../src/shared/shadow_terminator.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

// A UV sphere of radius 1 with `seg` x `rings` faces and its radial vertex normals, lit by a low sun from the side; `terminator` its material's cutoff.
std::string sphereText(int seg, int rings, double terminator) {
	std::ostringstream p, n, idx;
	for (int r = 0; r <= rings; ++r)
		for (int s = 0; s < seg; ++s) {
			const double th = 3.14159265358979 * r / rings, ph = 2.0 * 3.14159265358979 * s / seg;
			const double v[3] = {std::sin(th) * std::cos(ph), std::cos(th), std::sin(th) * std::sin(ph)};
			p << v[0] << ' ' << v[1] << ' ' << v[2] << ' ';
			n << v[0] << ' ' << v[1] << ' ' << v[2] << ' ';
		}
	for (int r = 0; r < rings; ++r)
		for (int s = 0; s < seg; ++s) {
			const int a = r * seg + s, b = r * seg + (s + 1) % seg, c = a + seg, d = b + seg;
			if (r > 0) idx << a << ' ' << c << ' ' << b << ' ';
			if (r < rings - 1) idx << b << ' ' << c << ' ' << d << ' ';
		}
	std::ostringstream s;
	s << "LookAt 0 0.3 4  0 0 0  0 1 0\nCamera \"perspective\" \"float fov\" [ 32 ]\n"
	  << "Film \"rgb\" \"integer xresolution\" [ 96 ] \"integer yresolution\" [ 96 ]\nWorldBegin\n"
	  << "LightSource \"distant\" \"point3 from\" [ 4 0.5 1 ] \"point3 to\" [ 0 0 0 ] \"rgb L\" [ 3 3 3 ]\n"
	  << "LightSource \"infinite\" \"rgb L\" [ 0.03 0.03 0.03 ]\n"
	  << "Material \"diffuse\" \"rgb reflectance\" [ 0.7 0.7 0.7 ] \"float terminator\" [ " << terminator << " ]\n"
	  << "Shape \"trianglemesh\" \"point3 P\" [ " << p.str() << "] \"normal N\" [ " << n.str() << "] \"integer indices\" [ " << idx.str() << "]\n";
	return s.str();
}

bool render(const std::string &text, const std::string &tag, const char *backend, std::vector<float> &rgb) {
	const std::string launcher = findRayTracerLauncher();
	if (launcher.empty()) return false;
	const std::string scenePath = "terminator_" + tag + ".pbrt", outPath = "terminator_" + tag + ".exr";
	std::ofstream(scenePath, std::ios::binary) << text;
	std::remove(outPath.c_str());
	std::string command = "\"" + launcher + "\" " + backend + " --output \"" + outPath + "\" 96 64 5 \"" + scenePath + "\" > \"" + scenePath + ".log\" 2>&1";
#ifdef _WIN32
	command = "\"" + command + "\"";
#endif
	int w = 0, h = 0;
	const bool ok = std::system(command.c_str()) == 0 && loadLinearRgbPixels(outPath, w, h, rgb) && w == 96 && h == 96;
	for (const std::string &f : {scenePath, outPath, outPath + ".run_marker.txt", scenePath + ".log"}) std::remove(f.c_str());
	return ok;
}

// The notches: pixels well inside the sphere's outline (85% of its radius on the screen, inside every facet's edge) that the round sphere lights and the coarse one
// leaves under half as bright.
int notches(const std::vector<float> &coarse, const std::vector<float> &round) {
	const double radius = 0.85 * 0.898 * 48.0;   // (the sphere's radius on the screen: asin(1/4) against a 32 degree view, of 48 pixels)
	int count = 0;
	for (int y = 0; y < 96; ++y)
		for (int x = 0; x < 96; ++x) {
			if (std::hypot(x + 0.5 - 48.0, y + 0.5 - 48.0) >= radius) continue;
			const std::size_t i = (static_cast<std::size_t>(y) * 96 + x) * 3 + 1;
			count += round[i] > 0.05f && coarse[i] < 0.5f * round[i];
		}
	return count;
}

void checkNotches(const char *backend, const std::string &tag) {
	std::vector<float> round, plain, fixed;
	ASSERT_TRUE(render(sphereText(64, 32, 0.0), tag + "_round", backend, round)) << tag;
	ASSERT_TRUE(render(sphereText(20, 10, 0.0), tag + "_plain", backend, plain)) << tag;
	ASSERT_TRUE(render(sphereText(20, 10, 0.1), tag + "_fixed", backend, fixed)) << tag;
	EXPECT_GE(notches(plain, round), 5) << tag << ": a coarse sphere's shadow has notches where the next facet hides the light";
	EXPECT_LE(notches(fixed, round), 1) << tag << ": with the offset its shadow rays start from the round surface and they go";
}

bool haveGpu() { return !findRayTracerLauncher().empty() && std::getenv("RT_FORBID_GPU") == nullptr; }

}  // namespace

TEST(ShadowTerminator, AFlatFacetIsNotLiftedAndARoundOneIsLiftedOutwards) {
	// a flat triangle whose vertex normals are its own: no lift
	const double V[3][3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}, flat[3][3] = {{0, 0, 1}, {0, 0, 1}, {0, 0, 1}}, ng[3] = {0, 0, 1};
	double out[3];
	shadow_terminator::lift(V, flat, 1.0 / 3, 1.0 / 3, 1.0 / 3, ng, out);
	EXPECT_NEAR(out[0] * out[0] + out[1] * out[1] + out[2] * out[2], 0.0, 1e-20);
	// a facet of a unit sphere with radial normals: its middle lifted outwards, towards the sphere, and not past it
	const double a = 0.35;
	const double S[3][3] = {{std::sin(a), 0, std::cos(a)}, {-0.5 * std::sin(a), 0.866 * std::sin(a), std::cos(a)}, {-0.5 * std::sin(a), -0.866 * std::sin(a), std::cos(a)}};
	double sng[3] = {0, 0, 1};
	shadow_terminator::lift(S, S, 1.0 / 3, 1.0 / 3, 1.0 / 3, sng, out);
	const double mid[3] = {(S[0][0] + S[1][0] + S[2][0]) / 3, (S[0][1] + S[1][1] + S[2][1]) / 3, (S[0][2] + S[1][2] + S[2][2]) / 3};
	const double before = std::sqrt(mid[0] * mid[0] + mid[1] * mid[1] + mid[2] * mid[2]);
	const double after = std::sqrt((mid[0] + out[0]) * (mid[0] + out[0]) + (mid[1] + out[1]) * (mid[1] + out[1]) + (mid[2] + out[2]) * (mid[2] + out[2]));
	EXPECT_GT(out[2], 0.0);
	EXPECT_GT(after, before) << "towards the round surface";
	EXPECT_LT(std::fabs(after - 1.0), 1.0 - before) << "nearer it than the facet was (Cycles bounds the lift by the vertices' tangent planes, not the sphere: a little past it here)";
	// at a vertex: nothing to lift
	shadow_terminator::lift(S, S, 1.0, 0.0, 0.0, sng, out);
	EXPECT_NEAR(out[0] * out[0] + out[1] * out[1] + out[2] * out[2], 0.0, 1e-20);
}

TEST(ShadowTerminator, OnlyRaysNearTheTerminatorTakeTheLift) {
	EXPECT_EQ(shadow_terminator::amount(0.9, 0.9, 0.1), 0.0) << "a ray well above the surface";
	EXPECT_EQ(shadow_terminator::amount(0.02, 0.01, 0.1), 1.0) << "a ray along it";
	const double half = shadow_terminator::amount(0.2, 0.05, 0.1);
	EXPECT_GT(half, 0.0);
	EXPECT_LT(half, 1.0);
	EXPECT_EQ(shadow_terminator::amount(0.02, 0.01, 0.0), 0.0) << "off";
}

TEST(ShadowTerminatorCpu, ACoarseSpheresShadowLosesItsNotches) {
	if (findRayTracerLauncher().empty()) GTEST_SKIP() << "ray_tracer was not found";
	checkNotches("--cpu", "cpu");
}

TEST(ShadowTerminatorGpu, TheSameOnBothOptixRenderers) {
	if (!haveGpu()) GTEST_SKIP() << "no launcher, or the GPU is not for this run";
	checkNotches("--gpu", "gpu");
	checkNotches("--gpu --wavefront", "wf");
}
