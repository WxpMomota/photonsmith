/**
 * @file output_crop_tests.cpp
 * @brief Cutting a finished render to its crop window (src/shared/output_crop.h): the pixel bounds, a PPM and an EXR cut in place.
 */

#include <gtest/gtest.h>

#include "output_crop.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace {

std::string tempPath(const std::string &name) {
	const char *tmp = std::getenv("TEMP");
	return (std::filesystem::path(tmp ? tmp : ".") / ("output_crop_tests_" + name)).string();
}

}  // namespace

TEST(OutputCropTest, TheWindowIsRoundedLikeTheRenderersDoIt) {
	const double frac[4] = {0.3, 0.8, 0.15, 0.7};
	const output_crop::Bounds b = output_crop::pixelBounds(frac, 100, 40);
	EXPECT_EQ(b.x0, 30);
	EXPECT_EQ(b.x1, 80);
	EXPECT_EQ(b.y0, 6);
	EXPECT_EQ(b.y1, 28);
	EXPECT_FALSE(b.full);
}

TEST(OutputCropTest, AWindowThatCollapsesIsTheWholeFrame) {
	const double frac[4] = {0.5, 0.5, 0.0, 1.0};
	const output_crop::Bounds b = output_crop::pixelBounds(frac, 64, 48);
	EXPECT_TRUE(b.full);
	EXPECT_EQ(b.x1, 64);
	EXPECT_EQ(b.y1, 48);
}

TEST(OutputCropTest, APpmIsCutInPlaceAndKeepsTheWindowsPixels) {
	const std::string path = tempPath("a.ppm");
	// 4 x 2, every pixel (x + 10 y) in all three channels.
	{
		std::vector<int> samples;
		for (int y = 0; y < 2; ++y)
			for (int x = 0; x < 4; ++x)
				for (int c = 0; c < 3; ++c) samples.push_back(x + 10 * y);
		ASSERT_TRUE(output_crop::writePpm(path, 4, 2, 255, samples));
	}
	const double frac[4] = {0.25, 0.75, 0.5, 1.0};   // columns 1-2, row 1
	ASSERT_TRUE(output_crop::cropPpmFile(path, frac));
	int w = 0, h = 0, maxValue = 0;
	std::vector<int> samples;
	ASSERT_TRUE(output_crop::readPpm(path, w, h, maxValue, samples));
	EXPECT_EQ(w, 2);
	EXPECT_EQ(h, 1);
	EXPECT_EQ(maxValue, 255);
	ASSERT_EQ(samples.size(), 6u);
	EXPECT_EQ(samples[0], 11);
	EXPECT_EQ(samples[3], 12);
	std::remove(path.c_str());
}

TEST(OutputCropTest, AWholeFrameWindowLeavesThePpmAlone) {
	const std::string path = tempPath("b.ppm");
	std::vector<int> samples(4 * 2 * 3, 7);
	ASSERT_TRUE(output_crop::writePpm(path, 4, 2, 255, samples));
	const double frac[4] = {0, 1, 0, 1};
	ASSERT_TRUE(output_crop::cropPpmFile(path, frac));
	int w = 0, h = 0, maxValue = 0;
	std::vector<int> back;
	ASSERT_TRUE(output_crop::readPpm(path, w, h, maxValue, back));
	EXPECT_EQ(w, 4);
	EXPECT_EQ(h, 2);
	std::remove(path.c_str());
}

TEST(OutputCropTest, AFileThatIsNotAPpmIsReportedNotDamaged) {
	const std::string path = tempPath("c.ppm");
	{
		std::FILE *f = std::fopen(path.c_str(), "wb");
		ASSERT_NE(f, nullptr);
		std::fputs("\x89PNG not a ppm", f);
		std::fclose(f);
	}
	const double frac[4] = {0.25, 0.75, 0.25, 0.75};
	EXPECT_FALSE(output_crop::cropPpmFile(path, frac));
	std::remove(path.c_str());
}

TEST(OutputCropTest, AnExrIsCutInPlace) {
	const std::string path = tempPath("d.exr");
	std::vector<float> rgb;
	for (int y = 0; y < 4; ++y)
		for (int x = 0; x < 8; ++x) {
			rgb.push_back(static_cast<float>(x));
			rgb.push_back(static_cast<float>(y));
			rgb.push_back(0.5f);
		}
	std::string error;
	ASSERT_TRUE(write_exr_image(path, rgb.data(), 8, 4, error)) << error;
	const double frac[4] = {0.25, 0.75, 0.25, 1.0};   // columns 2-5, rows 1-3
	ASSERT_TRUE(output_crop::cropExrFile(path, frac));
	float *rgba = nullptr;
	int w = 0, h = 0;
	const char *err = nullptr;
	ASSERT_EQ(LoadEXR(&rgba, &w, &h, path.c_str(), &err), TINYEXR_SUCCESS);
	EXPECT_EQ(w, 4);
	EXPECT_EQ(h, 3);
	EXPECT_FLOAT_EQ(rgba[0], 2.0f);   // the first pixel is column 2, row 1
	EXPECT_FLOAT_EQ(rgba[1], 1.0f);
	std::free(rgba);
	std::remove(path.c_str());
}
