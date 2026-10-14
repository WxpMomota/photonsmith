/**
 * @file pbrt_reference_pack_gpu_tests.cpp
 * @brief The OptiX backends against the pbrt-v4 reference table (tests/data/pbrt_reference_means.txt): see pbrt_reference_pack_tests.cpp. Skips without OptiX.
 */
#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

extern "C" {
	#include "optix_interface.h"
}
#include "agreement_test_helpers.h"
#include "pbrt_reference_pack.h"

namespace {

std::vector<PbrtPackRow> packRows() {
	static const std::vector<PbrtPackRow> rows = loadPbrtReferencePack();
	return rows;
}

void setWavefront(const char* v) {
#ifdef _WIN32
	_putenv_s("RAY_TRACER_WAVEFRONT", v);
#else
	setenv("RAY_TRACER_WAVEFRONT", v, 1);
#endif
}

class PbrtReferencePackGpuTest : public ::testing::TestWithParam<PbrtPackRow> {
  protected:
	// backend: 1 = recursive, 2 = wavefront (the table's column)
	void check(int backend, const char* wavefront, const char* label) {
		const PbrtPackRow& row = GetParam();
		if (!optix_is_available()) GTEST_SKIP() << "OptiX not available";
		if (row.ratio[backend] <= 0.0) GTEST_SKIP() << row.name << ": " << label << " was not measured against it";
		const SceneDescriptor* s = find_example_scene(row.name.c_str());
		if (!s) GTEST_SKIP() << row.name << ".pbrt was not discovered - is pbrt_scenes/ present?";
		const std::string out = "pbrt_pack_" + pbrtPackTestName(row.name) + "_" + label + ".exr";
		setWavefront(wavefront);
		const int rc = optix_render_main(row.size, row.size, row.spp, row.depth, out.c_str(), s->id.c_str(), s->camera.lookfrom_x, s->camera.lookfrom_y, s->camera.lookfrom_z);
		setWavefront("0");
		ASSERT_EQ(rc, 0);
		double mean = 0.0;
		const bool ok = loadLinearMean(out, mean);
		std::remove(out.c_str());
		ASSERT_TRUE(ok) << row.name << ": no readable EXR";
		const double expected = row.pbrtMean() * row.ratio[backend];
		std::printf("[pack] %-40s %s %.5f  expected %.5f (pbrt %.5f x %.3f, tolerance %.1f%%): %+.1f%%\n", row.name.c_str(), label, mean, expected, row.pbrtMean(), row.ratio[backend], 100.0 * row.tol[backend], 100.0 * (mean / expected - 1.0));
		EXPECT_GT(mean, (1.0 - row.tol[backend]) * expected) << row.name << " " << label << " reads darker than it did against pbrt-v4";
		EXPECT_LT(mean, (1.0 + row.tol[backend]) * expected) << row.name << " " << label << " reads brighter than it did against pbrt-v4";
	}
};

TEST_P(PbrtReferencePackGpuTest, TheRecursiveBackendStaysWhereItWasAgainstPbrt) { check(1, "0", "recursive"); }
TEST_P(PbrtReferencePackGpuTest, TheWavefrontBackendStaysWhereItWasAgainstPbrt) { check(2, "1", "wavefront"); }

INSTANTIATE_TEST_SUITE_P(BundledScenes, PbrtReferencePackGpuTest, ::testing::ValuesIn(packRows()),
                         [](const ::testing::TestParamInfo<PbrtPackRow>& info) { return pbrtPackTestName(info.param.name); });

}  // namespace
