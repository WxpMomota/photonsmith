// path_probe_tests.cpp -- `ray_tracer --probe X Y` (src/shared/path_probe.h, the hooks in camera_path.h, launcher/launcher_args.h): the sink's report, and that what the path tracer books
// into it adds up to what the path tracer returns.

#include <gtest/gtest.h>

#include <cmath>
#include <string>
#include <vector>

#include "pbrt_cpu_builder_test_helpers.h"
#include "camera.h"
#include "independent_sampler.h"
#include "../../launcher/launcher_args.h"
#include "../../src/shared/path_probe.h"

namespace {

bool parseArgs(std::vector<std::string> tokens, LaunchArgs &out) {
	std::vector<std::string> storage;
	storage.push_back("ray_tracer.exe");
	for (auto &t : tokens) storage.push_back(t);
	std::vector<char *> ptrs;
	for (auto &s : storage) ptrs.push_back(const_cast<char *>(s.c_str()));
	return parse_launch_args(static_cast<int>(ptrs.size()), ptrs.data(), out);
}

}  // namespace

TEST(PathProbeArgs, TheProbeTakesAColumnAndARow) {
	LaunchArgs args;
	ASSERT_TRUE(parseArgs({"--probe", "120", "45", "400", "2000", "8"}, args));
	EXPECT_EQ(args.probe_x, 120);
	EXPECT_EQ(args.probe_y, 45);
	EXPECT_EQ(args.image_width, 400) << "the positional arguments still follow";
	EXPECT_EQ(args.samples_per_pixel, 2000);
}

TEST(PathProbeArgs, NoProbeByDefaultAndANegativePixelIsRefused) {
	LaunchArgs none;
	ASSERT_TRUE(parseArgs({}, none));
	EXPECT_EQ(none.probe_x, -1);
	LaunchArgs bad;
	EXPECT_FALSE(parseArgs({"--probe", "-3", "4"}, bad));
}

TEST(PathProbeSink, TheReportSplitsTheMeanBySourceAndBounce) {
	path_probe::Sink sink;
	path_probe::install(&sink);
	for (int s = 0; s < 4; ++s) {
		path_probe::beginSample();
		path_probe::setBounce(0);
		path_probe::add(path_probe::kNeeArea, 0.4, 0.2, 0.1);
		path_probe::setBounce(1);
		path_probe::add(path_probe::kEscaped, 0.1, 0.1, 0.1);
	}
	path_probe::install(nullptr);
	const std::string text = path_probe::report(sink, 7, 9, 64, 48);
	EXPECT_NE(text.find("Pixel (7, 9) of 64 x 48, 4 samples"), std::string::npos) << text;
	EXPECT_NE(text.find("0.5000   0.3000   0.2000"), std::string::npos) << "the mean is the sum per sample\n" << text;
	EXPECT_NE(text.find("next-event: area lights"), std::string::npos);
	EXPECT_NE(text.find("bounce  1"), std::string::npos);
}

TEST(PathProbeSink, WithNoSinkInstalledNothingIsRecorded) {
	EXPECT_FALSE(path_probe::active());
	path_probe::add(path_probe::kEmission, 1.0, 1.0, 1.0);   // a no-op, not a crash
	path_probe::beginSample();
}

// A diffuse floor (albedo 0.5) under a uniform white sky of radiance 1 returns exactly 0.5 (one bounce, nothing to interreflect with). The probe must book all of it under the
// sky, and its total must be what the path tracer returned, sample for sample.
TEST(PathProbeIntegration, ADiffuseFloorUnderAWhiteSkyIsHalfAndAllOfItIsSky) {
	const pbrt_cpu::BuildResult b = buildFrom(
		"LightSource \"infinite\" \"rgb L\" [ 1 1 1 ]\n"
		"AttributeBegin\n AreaLightSource \"diffuse\" \"rgb L\" [ 0.000000001 0.000000001 0.000000001 ]\n"
		" Shape \"trianglemesh\" \"integer indices\" [ 0 1 2 ] \"point3 P\" [ 0 50 0  1 50 0  0 50 1 ]\nAttributeEnd\n"
		"Material \"diffuse\" \"rgb reflectance\" [ 0.5 0.5 0.5 ]\n"
		"Shape \"trianglemesh\" \"integer indices\" [ 0 1 2  0 2 3 ] \"point3 P\" [ -100 0 -100  100 0 -100  100 0 100  -100 0 100 ]\n");
	ASSERT_NE(b.sky, nullptr);
	camera cam;
	cam.lookfrom = point3(0.0, 2.0, 0.0);
	cam.lookat = point3(0.3, 1.0, 0.2);
	cam.image_width = 16;
	cam.samples_per_pixel = 1;
	cam.max_depth = 6;
	cam.background = color(0, 0, 0);
	cam.sky = b.sky;
	cam.initialize();
	ASSERT_NE(b.lights, nullptr);
	path_probe::Sink sink;
	path_probe::install(&sink);
	const int n = 4000;
	double returned = 0.0;
	for (int s = 0; s < n; ++s) {
		IndependentSampler sampler(s, 3, 4);
		const color c = cam.ray_color(ray(point3(0.0, 2.0, 0.0), unit_vector(vec3(0.3, -1.0, 0.2))), 6, *b.world, *b.lights, sampler);
		returned += c.x();
	}
	path_probe::install(nullptr);
	double booked = 0.0, sky = 0.0;
	for (int c = 0; c < path_probe::kComponentCount; ++c)
		for (int k = 0; k < path_probe::kMaxBounce; ++k) {
			booked += sink.comp[c][k].r;
			if (c == path_probe::kNeeSky || c == path_probe::kEscaped) sky += sink.comp[c][k].r;
		}
	EXPECT_EQ(sink.samples, n);
	EXPECT_NEAR(booked, returned, 1e-9 * n) << "what the probe books is what ray_color returns";
	EXPECT_NEAR(returned / n, 0.5, 0.02) << "albedo times the sky's radiance";
	EXPECT_GT(sky, 0.99 * booked) << "all of it is the sky's (its next-event estimate plus the sky a bounce escapes to)";
	EXPECT_EQ(sink.primaryMisses, 0);
	EXPECT_EQ(sink.firstHits.size(), 1u) << "the camera ray hits the floor";
}
