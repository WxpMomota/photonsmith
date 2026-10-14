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
#include <cstdio>
#include <cstdlib>
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
	ASSERT_EQ(cpu_render_main(row.size, row.size, row.spp, row.depth, out.c_str(), s->id.c_str(), s->camera.lookfrom_x, s->camera.lookfrom_y, s->camera.lookfrom_z), 0);
	double mean = 0.0;
	const bool ok = loadLinearMean(out, mean);
	std::remove(out.c_str());
	ASSERT_TRUE(ok) << row.name << ": no readable EXR";
	const double expected = row.pbrtMean() * row.ratio[0];
	std::printf("[pack] %-40s cpu %.5f  expected %.5f (pbrt %.5f x %.3f, tolerance %.1f%%): %+.1f%%\n", row.name.c_str(), mean, expected, row.pbrtMean(), row.ratio[0], 100.0 * row.tol[0], 100.0 * (mean / expected - 1.0));
	EXPECT_GT(mean, (1.0 - row.tol[0]) * expected) << row.name << " reads darker than it did against pbrt-v4";
	EXPECT_LT(mean, (1.0 + row.tol[0]) * expected) << row.name << " reads brighter than it did against pbrt-v4";
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
