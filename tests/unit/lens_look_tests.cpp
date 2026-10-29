// lens_look_tests.cpp -- the camera's look (src/shared/lens_look.h): bloom, vignette, warmth and saturation on a linear picture, and how a scene file and the flags set them.
#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <numeric>
#include <vector>

#include "../../src/shared/lens_look.h"

namespace {

std::vector<float> flat(int w, int h, float r, float g, float b) {
	std::vector<float> img(static_cast<std::size_t>(w) * h * 3);
	for (std::size_t i = 0; i < img.size(); i += 3) {
		img[i] = r;
		img[i + 1] = g;
		img[i + 2] = b;
	}
	return img;
}

double total(const std::vector<float> &img) { return std::accumulate(img.begin(), img.end(), 0.0); }

TEST(LensLook, NoLookLeavesThePictureAsItIs) {
	std::vector<float> img = flat(8, 6, 0.3f, 0.5f, 0.7f);
	const std::vector<float> before = img;
	lens_look::apply(img.data(), 8, 6, lens_look::Look{});
	EXPECT_EQ(img, before);
}

TEST(LensLook, BloomSpreadsABrightPixelAndKeepsItsLight) {
	const int w = 64, h = 64;
	std::vector<float> img = flat(w, h, 0.0f, 0.0f, 0.0f);
	const std::size_t centre = (static_cast<std::size_t>(32) * w + 32) * 3;
	img[centre] = img[centre + 1] = img[centre + 2] = 1000.0f;
	const double before = total(img);
	lens_look::Look look;
	look.bloom = 0.3;
	look.bloomRadius = 0.1;
	lens_look::apply(img.data(), w, h, look);
	EXPECT_NEAR(total(img) / before, 1.0, 0.01) << "bloom moves light, it does not add any (the pixel is far from the edges)";
	EXPECT_GT(img[centre], 700.0f) << "70% stays in the pixel, and the tightest halo is narrower than a pixel";
	EXPECT_LT(img[centre], 950.0f);
	const std::size_t near = (static_cast<std::size_t>(32) * w + 36) * 3;
	EXPECT_GT(img[near], 0.1f) << "its neighbours glow";
}

TEST(LensLook, VignetteDarkensTheCornersOnly) {
	const int w = 101, h = 61;
	std::vector<float> img = flat(w, h, 1.0f, 1.0f, 1.0f);
	lens_look::Look look;
	look.vignette = 0.4;
	lens_look::apply(img.data(), w, h, look);
	EXPECT_NEAR(img[(static_cast<std::size_t>(30) * w + 50) * 3], 1.0f, 1e-3f);
	EXPECT_NEAR(img[0], 0.6f, 0.05f) << "a corner is 1 - vignette";
}

TEST(LensLook, WarmthTintsButKeepsWhiteAsBright) {
	std::vector<float> img = flat(4, 4, 1.0f, 1.0f, 1.0f);
	lens_look::Look look;
	look.warmth = 0.5;
	lens_look::apply(img.data(), 4, 4, look);
	EXPECT_GT(img[0], img[2]) << "warm: more red than blue";
	EXPECT_NEAR(0.2126 * img[0] + 0.7152 * img[1] + 0.0722 * img[2], 1.0, 1e-5);
}

TEST(LensLook, SaturationZeroIsGrey) {
	std::vector<float> img = flat(4, 4, 0.8f, 0.2f, 0.1f);
	lens_look::Look look;
	look.saturation = 0.0;
	lens_look::apply(img.data(), 4, 4, look);
	EXPECT_NEAR(img[0], img[1], 1e-6f);
	EXPECT_NEAR(img[1], img[2], 1e-6f);
}

TEST(LensLook, ASceneFileLineSetsItAndAFlagWins) {
	const std::filesystem::path file = std::filesystem::temp_directory_path() / "lens_look_test_scene.pbrt";
	{
		std::ofstream f(file);
		f << "# @rt-look bloom 0.2 vignette 0.3 warmth -0.4\nLookAt 0 0 5 0 0 0 0 1 0\nCamera \"perspective\"\nWorldBegin\n# @rt-look bloom 0.9\n";
	}
	lens_look::Look look;
	std::string problem;
	ASSERT_TRUE(lens_look::fromSceneFile(file.string(), look, problem));
	EXPECT_TRUE(problem.empty()) << problem;
	EXPECT_DOUBLE_EQ(look.bloom, 0.2) << "only the line before WorldBegin counts";
	EXPECT_DOUBLE_EQ(look.vignette, 0.3);
	EXPECT_DOUBLE_EQ(look.warmth, -0.4);
	lens_look::Given flags;
	flags.vignette = 0.0;
	const lens_look::Look merged = lens_look::merged(look, flags);
	EXPECT_DOUBLE_EQ(merged.vignette, 0.0) << "a flag wins, even one that turns it off";
	EXPECT_DOUBLE_EQ(merged.bloom, 0.2) << "an unset flag keeps the file's value";
	std::filesystem::remove(file);
}

TEST(LensLook, AWordItDoesNotKnowIsReported) {
	lens_look::Look look;
	EXPECT_FALSE(lens_look::parse("bloom 0.1 glare 2", look).empty());
	EXPECT_FALSE(lens_look::parse("bloom", look).empty());
	EXPECT_TRUE(lens_look::parse("bloom 5", look).empty());
	EXPECT_DOUBLE_EQ(look.bloom, 1.0) << "clamped to its range";
}

}  // namespace

// A film chooses its colour curve on its look line (its cameras file writes it there); the picture writers read it, apply() does not.
TEST(LensLook, ALookLineCanChooseTheColourCurve) {
	lens_look::Look look;
	EXPECT_TRUE(lens_look::parse("bloom 0.1 tonemap agx", look).empty());
	EXPECT_EQ(look.tonemap, "agx");
	EXPECT_TRUE(lens_look::parse("tonemap neutral saturation 1.1", look).empty()) << "later words win";
	EXPECT_EQ(look.tonemap, "neutral");
	EXPECT_FALSE(lens_look::parse("tonemap filmic", look).empty()) << "a curve it does not know";
	lens_look::Look only;
	only.tonemap = "agx";
	EXPECT_FALSE(only.active()) << "the curve alone changes nothing in apply(): it is the writers'";
}
