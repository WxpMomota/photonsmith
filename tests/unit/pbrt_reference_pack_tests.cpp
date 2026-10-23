/**
 * @file pbrt_reference_pack_tests.cpp
 * @brief Every bundled scene pbrt-v4 can read, rendered by the CPU and compared with what real pbrt-v4 drew of it (tests/data/pbrt_reference_means.txt).
 *
 * scripts/pbrt_scene_compare.py compares the bundled scenes with pbrt but needs a pbrt build; scripts/pbrt_reference_pack.py freezes its answer into that table (pbrt's mean
 * picture value per scene at 512 samples, the ratio this renderer reads against it - 1.0 where they agree, the documented offset where they do not - and a tolerance from the
 * measured noise of one render), and this test reads the table, so a change that moves a scene away from pbrt is caught on any machine, pbrt or not. A scene that SHOULD move
 * (a deliberate fix) is a table update with the reason in the commit, not a loosened test: regenerate with scripts/pbrt_reference_pack.py.
 * Header-only helper: pbrt_reference_pack.h. The OptiX backends: pbrt_reference_pack_gpu_tests.cpp.
 */
#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "agreement_test_helpers.h"
#include "pbrt_reference_pack.h"

namespace {

std::vector<PbrtPackRow> packRows() {
	static const std::vector<PbrtPackRow> rows = loadPbrtReferencePack();
	return rows;
}

class PbrtReferencePackCpuTest : public ::testing::TestWithParam<PbrtPackRow> {};

TEST_P(PbrtReferencePackCpuTest, TheCpuRenderStaysWhereItWasAgainstPbrt) {
	const PbrtPackRow& row = GetParam();
	if (row.ratio[0] <= 0.0) GTEST_SKIP() << row.name << ": the CPU was not measured against it";
	const SceneDescriptor* s = find_example_scene(row.name.c_str());
	if (!s) GTEST_SKIP() << row.name << ".pbrt was not discovered - is pbrt_scenes/ present?";
	const std::string out = "pbrt_pack_" + pbrtPackTestName(row.name) + "_cpu.exr";
	// A scene whose estimator has a heavy tail is rendered five times and the render with the median mean is the one compared. A point light inside a scattering fog gives a scatter
	// point near the light a weight of 1/r^2, which has no bound, so a 64-sample render now and then holds one sample worth tens of thousands: camera-medium-absorbing (measured: 6 of
	// 200 renders read more than 10% bright, one 54%, against a median of -1.6%) and chromatic-camera-medium (+3.7% once in 30 renders that otherwise stay within 1.5%, +6.2% once against
	// its 4.5%). pbrt-v4's estimator has the same tail; one render made this test fail at random. The noisy rows are the ones with a CPU tolerance of 4% or more (eight scenes, about 10 s
	// in all). Every CPU render draws fresh randomness, so the five are independent; the median of five fails only when three of them do.
	const int renders = row.tol[0] >= 0.04 ? 5 : 1;
	std::vector<std::array<double, 4>> results;   // channel means and their mean, per render
	for (int r = 0; r < renders; ++r) {
		ASSERT_EQ(cpu_render_main(row.size, row.size, row.spp, row.depth, out.c_str(), s->id.c_str(), s->camera.lookfrom_x, s->camera.lookfrom_y, s->camera.lookfrom_z), 0);
		int w = 0, h = 0;
		std::vector<float> rgb;
		const bool ok = loadLinearRgbPixels(out, w, h, rgb);
		std::remove(out.c_str());
		ASSERT_TRUE(ok) << row.name << ": no readable EXR";
		std::array<double, 4> m = {0.0, 0.0, 0.0, 0.0};
		for (std::size_t i = 0; i < rgb.size(); ++i) m[i % 3] += rgb[i];
		for (int c = 0; c < 3; ++c) m[c] /= std::max(1, w * h);
		m[3] = (m[0] + m[1] + m[2]) / 3.0;
		results.push_back(m);
	}
	std::sort(results.begin(), results.end(), [](const std::array<double, 4>& a, const std::array<double, 4>& b) { return a[3] < b[3]; });
	const std::array<double, 4>& chosen = results[results.size() / 2];
	const double channelMean[3] = {chosen[0], chosen[1], chosen[2]};
	const double mean = chosen[3];
	const double expected = row.pbrtMean() * row.ratio[0];
	std::printf("[pack] %-40s cpu %.5f  expected %.5f (pbrt %.5f x %.3f, tolerance %.1f%%): %+.1f%%\n", row.name.c_str(), mean, expected, row.pbrtMean(), row.ratio[0], 100.0 * row.tol[0], 100.0 * (mean / expected - 1.0));
	EXPECT_GT(mean, (1.0 - row.tol[0]) * expected) << row.name << " reads darker than it did against pbrt-v4";
	EXPECT_LT(mean, (1.0 + row.tol[0]) * expected) << row.name << " reads brighter than it did against pbrt-v4";
	// The mean over the channels cannot see a colour error whose channels cancel (swapped dots colours, a tinted light): each channel that carries a share of the picture is
	// compared with pbrt's own channel, relative to how the whole picture reads against pbrt (the documented offset of the scene stays out of it).
	const double wholeRatio = mean / row.pbrtMean();
	const double channelTolerance = std::max(0.10, row.chroma + std::max(0.05, 2.0 * row.tol[0]));   // (the scene's own recorded offset, plus room for noise)
	for (int c = 0; c < 3; ++c) {
		if (row.mean[c] < 0.1 * row.pbrtMean() || wholeRatio <= 0.0) continue;
		const double relative = (channelMean[c] / row.mean[c]) / wholeRatio;
		EXPECT_NEAR(relative, 1.0, channelTolerance) << row.name << ": channel " << "RGB"[c] << " reads " << channelMean[c] << " against pbrt-v4's " << row.mean[c];
	}
}

INSTANTIATE_TEST_SUITE_P(BundledScenes, PbrtReferencePackCpuTest, ::testing::ValuesIn(packRows()),
                         [](const ::testing::TestParamInfo<PbrtPackRow>& info) { return pbrtPackTestName(info.param.name); });

// The other CPU integrators draw a lamp that reflects (a lamp keeps the shape's material in pbrt-v4) through the same material, so they land on pbrt's mean too. A lamp that
// ended their paths read 13% (the occluder ball) and 8% (the backdrop quad of earth-globe) dark.
TEST(PbrtReferencePackTest, TheBidirectionalAndPhotonIntegratorsAlsoReflectLamps) {
	const std::vector<PbrtPackRow> rows = packRows();
	for (const char* name : {"emissive-sphere-occluder", "earth-globe"}) {
		const auto it = std::find_if(rows.begin(), rows.end(), [&](const PbrtPackRow& r) { return r.name == name; });
		const SceneDescriptor* s = find_example_scene(name);
		if (it == rows.end() || !s) { ADD_FAILURE() << name << " is not in the table or not discovered"; continue; }
		struct Run { const char* label; double tolerance; };
		for (const Run& run : {Run{"bdpt", 0.04}, Run{"sppm", 0.06}}) {
			const std::string out = std::string("pbrt_pack_") + name + "_" + run.label + ".exr";
			int rc = -1;
			if (std::string(run.label) == "bdpt") rc = cpu_render_main_bdpt(48, 48, 128, it->depth, out.c_str(), s->id.c_str(), s->camera.lookfrom_x, s->camera.lookfrom_y, s->camera.lookfrom_z);
			else rc = cpu_render_main_sppm(32, 32, 60, 30000, it->depth, out.c_str(), s->id.c_str(), s->camera.lookfrom_x, s->camera.lookfrom_y, s->camera.lookfrom_z);
			ASSERT_EQ(rc, 0) << name << " " << run.label;
			double mean = 0.0;
			const bool ok = loadLinearMean(out, mean);
			std::remove(out.c_str());
			ASSERT_TRUE(ok);
			EXPECT_NEAR(mean / it->pbrtMean(), 1.0, run.tolerance) << name << " under " << run.label << ": " << mean << " against pbrt's " << it->pbrtMean();
		}
	}
}

// A large scene on the CPU, which needs no GPU (so CI runs it): the Sibenik cathedral (75k triangles, bump-mapped marble: the scene the mip-filtered bump and footprint work was
// found on) loads - OBJ, materials, bump maps, mip pyramids - and renders a picture that is neither black nor blown out. The other large scenes stay opt-in
// (RT_GALLERY_ALL, tests/integration/gallery_scenes_render_test.cpp) because they take minutes to load.
TEST(PbrtReferencePackTest, TheSibenikCathedralLoadsAndRendersOnTheCpu) {
	const SceneDescriptor* s = find_example_scene("environment-sibenik-cathedral");
	if (!s) GTEST_SKIP() << "environment-sibenik-cathedral.pbrt was not discovered - is pbrt_scenes/ present?";
	if (std::getenv("RT_SKIP_GALLERY")) GTEST_SKIP() << "RT_SKIP_GALLERY is set";
	// The mesh is git-ignored (too large to commit), so a clean checkout - the hosted CI - has none: skip rather than fail on a missing asset.
	if (!std::ifstream("models/sibenik_cathedral.obj")) GTEST_SKIP() << "models/sibenik_cathedral.obj is not installed";
	const std::string out = "pbrt_pack_sibenik_smoke.exr";
	ASSERT_EQ(cpu_render_main(48, 32, 4, 4, out.c_str(), s->id.c_str(), s->camera.lookfrom_x, s->camera.lookfrom_y, s->camera.lookfrom_z), 0);
	double mean = 0.0;
	const bool ok = loadLinearMean(out, mean);
	std::remove(out.c_str());
	ASSERT_TRUE(ok) << "the CPU render produced no readable EXR";
	EXPECT_GT(mean, 0.01) << "a lit cathedral is not black";
	EXPECT_LT(mean, 20.0) << "...nor blown out";
}

TEST(PbrtReferencePackTest, TheTableIsPresentAndCoversTheBundledScenes) {
	const std::vector<PbrtPackRow> rows = packRows();
	ASSERT_GT(rows.size(), 100u) << "tests/data/pbrt_reference_means.txt is missing or short (run from the repository root)";
	int unmeasured = 0;
	for (const auto& r : rows) {
		EXPECT_GT(r.pbrtMean(), 0.0) << r.name;
		EXPECT_GT(r.tol[0], 0.0) << r.name;
		if (r.ratio[0] <= 0.0) ++unmeasured;
	}
	EXPECT_LT(unmeasured, 5) << "scenes the CPU could not be measured on";
}

}  // namespace
