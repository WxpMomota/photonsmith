/**
 * @file pbrt_reference_pack_gpu_tests.cpp
 * @brief The OptiX backends against the pbrt-v4 reference table (tests/data/pbrt_reference_means.txt): see pbrt_reference_pack_tests.cpp. Skips without OptiX.
 */
#include <gtest/gtest.h>
#include <algorithm>
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
		// A heavy-tailed scene (the rows with a CPU tolerance of 4% or more: a point light in a scattering fog, see pbrt_reference_pack_tests.cpp) is rendered five times
		// and the median compared, as on the CPU. The wavefront renderer seeds each render from a frame counter that runs over the whole process, so where the row falls
		// in a run decides its draw: camera-medium-absorbing read 1.59, 1.57, 1.56 and 1.62 behind different tests, and +19% at one place in the Slow tier.
		const int renders = row.tol[0] >= 0.04 ? 5 : 1;
		std::vector<double> means;
		for (int r = 0; r < renders; ++r) {
			setWavefront(wavefront);
			const int rc = optix_render_main(row.size, row.size, row.spp, row.depth, out.c_str(), s->id.c_str(), s->camera.lookfrom_x, s->camera.lookfrom_y, s->camera.lookfrom_z);
			setWavefront("0");
			ASSERT_EQ(rc, 0);
			double one = 0.0;
			const bool ok = loadLinearMean(out, one);
			std::remove(out.c_str());
			ASSERT_TRUE(ok) << row.name << ": no readable EXR";
			means.push_back(one);
		}
		std::sort(means.begin(), means.end());
		const double mean = means[means.size() / 2];
		const double expected = row.pbrtMean() * row.ratio[backend];
		std::printf("[pack] %-40s %s %.5f  expected %.5f (pbrt %.5f x %.3f, tolerance %.1f%%): %+.1f%%\n", row.name.c_str(), label, mean, expected, row.pbrtMean(), row.ratio[backend], 100.0 * row.tol[backend], 100.0 * (mean / expected - 1.0));
		EXPECT_GT(mean, (1.0 - row.tol[backend]) * expected) << row.name << " " << label << " reads darker than it did against pbrt-v4";
		EXPECT_LT(mean, (1.0 + row.tol[backend]) * expected) << row.name << " " << label << " reads brighter than it did against pbrt-v4";
	}
};

TEST_P(PbrtReferencePackGpuTest, TheRecursiveBackendStaysWhereItWasAgainstPbrt) { check(1, "0", "recursive"); }
TEST_P(PbrtReferencePackGpuTest, TheWavefrontBackendStaysWhereItWasAgainstPbrt) { check(2, "1", "wavefront"); }

// GPU SPPM only handles scenes of spheres and quads; earth-globe is one, and its backdrop is a lamp that reflects the sky (it read 8% dark while GPU SPPM ended photon and
// camera paths at a lamp).
TEST(PbrtReferencePackGpuSppmTest, ALampThatReflectsIsDrawnLikePbrtDoes) {
	if (!optix_is_available()) GTEST_SKIP() << "OptiX not available";
	const std::vector<PbrtPackRow> rows = packRows();
	// earth-globe: the lamp is a quad that reflects the sky (it read 8% dark). spherical-camera-scene: the lamp is a sphere seen directly; a visible point on the sphere used to
	// sample the sphere itself and read its own emission back (the lamp pixels, 10 on a lamp of 10, came out 11.2).
	for (const char* name : {"earth-globe", "spherical-camera-scene"}) {
		const auto it = std::find_if(rows.begin(), rows.end(), [&](const PbrtPackRow& r) { return r.name == name; });
		const SceneDescriptor* s = find_example_scene(name);
		if (it == rows.end() || !s) { ADD_FAILURE() << name << " is not in the table or not discovered"; continue; }
		const std::string out = std::string("pbrt_pack_") + pbrtPackTestName(name) + "_gpu_sppm.exr";
		const int rc = optix_render_main_sppm(48, 48, 60, 60000, it->depth, out.c_str(), s->id.c_str(), s->camera.lookfrom_x, s->camera.lookfrom_y, s->camera.lookfrom_z);
		ASSERT_EQ(rc, 0) << name;
		double mean = 0.0;
		const bool ok = loadLinearMean(out, mean);
		std::remove(out.c_str());
		ASSERT_TRUE(ok) << name;
		EXPECT_NEAR(mean / it->pbrtMean(), 1.0, 0.06) << name << ": " << mean << " against pbrt's " << it->pbrtMean();
	}
}

INSTANTIATE_TEST_SUITE_P(BundledScenes, PbrtReferencePackGpuTest, ::testing::ValuesIn(packRows()),
                         [](const ::testing::TestParamInfo<PbrtPackRow>& info) { return pbrtPackTestName(info.param.name); });

}  // namespace
