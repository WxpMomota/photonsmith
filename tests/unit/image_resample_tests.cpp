/**
 * @file image_resample_tests.cpp
 * @brief pbrt-v4's resize-up of a picture to powers of two (Image::FloatResizeUp), the step before a mip pyramid is built (src/shared/image_resample.h).
 *
 * The CPU mipmap and the OptiX bump-height chain both resample first; a flat colour passes through any weights that sum to one, so these check what a flat colour cannot: that
 * the weights are pbrt's (a ramp stays a ramp at the right positions), that the wrap modes act at the edges, that nothing goes negative, and that a side that is already a
 * power of two is left alone.
 */
#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "image_resample.h"

namespace {

std::vector<float> resize(const std::vector<float>& src, int w, int h, int wrap, int& nw, int& nh) {
	return image_resample::resizeUpToPowerOf2(src, w, h, wrap, nw, nh);
}

}  // namespace

TEST(ImageResample, TheWeightsOfEveryNewPixelSumToOne) {
	for (int oldRes : {3, 5, 6, 7, 100, 1000}) {
		const int newRes = image_resample::roundUpPowerOf2(oldRes);
		const auto weights = image_resample::resampleWeights(oldRes, newRes);
		ASSERT_EQ(static_cast<int>(weights.size()), newRes);
		for (const auto& w : weights) EXPECT_NEAR(w.weight[0] + w.weight[1] + w.weight[2] + w.weight[3], 1.0, 1e-12) << oldRes << " -> " << newRes;
	}
}

TEST(ImageResample, ASideThatIsAlreadyAPowerOfTwoIsLeftAlone) {
	int nw = 0, nh = 0;
	const std::vector<float> src = {0.0f, 1.0f, 4.0f, 9.0f, 16.0f, 25.0f, 36.0f, 49.0f};   // 4 x 2
	EXPECT_EQ(resize(src, 4, 2, image_resample::kRepeat, nw, nh), src);
	EXPECT_EQ(nw, 4);
	EXPECT_EQ(nh, 2);
	// ... and so is each side on its own: the 4-wide side of a 4 x 3 picture is not touched, the 3-high one becomes 4.
	const std::vector<float> three(4 * 3, 0.5f);
	resize(three, 4, 3, image_resample::kClamp, nw, nh);
	EXPECT_EQ(nw, 4);
	EXPECT_EQ(nh, 4);
}

// A linear ramp is reproduced (a windowed sinc with normalised weights is exact for it away from the edges): pixel x of the old image holds its centre's position x + 0.5, so new
// pixel i must hold the position of its own centre, (i + 0.5) * old / new.
TEST(ImageResample, ARampStaysARampAtTheRightPositions) {
	const int w = 6, h = 1;
	std::vector<float> ramp(w);
	for (int x = 0; x < w; ++x) ramp[static_cast<std::size_t>(x)] = x + 0.5f;
	int nw = 0, nh = 0;
	const std::vector<float> out = resize(ramp, w, h, image_resample::kClamp, nw, nh);
	ASSERT_EQ(nw, 8);
	ASSERT_EQ(nh, 1);
	for (int i = 2; i <= 5; ++i) EXPECT_NEAR(out[static_cast<std::size_t>(i)], (i + 0.5) * 6.0 / 8.0, 0.03) << "new pixel " << i;
}

// The sinc has negative lobes: a single bright pixel must not come back with negative values (pbrt clamps at zero).
TEST(ImageResample, AnImpulseNeverGoesNegative) {
	std::vector<float> src(5 * 5, 0.0f);
	src[12] = 1.0f;
	int nw = 0, nh = 0;
	const std::vector<float> out = resize(src, 5, 5, image_resample::kRepeat, nw, nh);
	ASSERT_EQ(nw, 8);
	double sum = 0.0;
	for (float v : out) {
		EXPECT_GE(v, 0.0f);
		sum += v;
	}
	EXPECT_GT(sum, 0.0);
	EXPECT_GT(out[(8 * 8) / 2 + 4], out[0]) << "the energy stays round the impulse";
}

TEST(ImageResample, TheWrapModeActsAtTheEdges) {
	int nw = 0, nh = 0;
	const std::vector<float> flat(3, 1.0f);   // 3 x 1, all ones
	const auto black = resize(flat, 3, 1, image_resample::kBlack, nw, nh);
	const auto repeat = resize(flat, 3, 1, image_resample::kRepeat, nw, nh);
	const auto clamp = resize(flat, 3, 1, image_resample::kClamp, nw, nh);
	EXPECT_LT(black.front(), 0.99f) << "past the edge a black wrap reads zero";
	EXPECT_LT(black.back(), 0.99f);
	EXPECT_NEAR(repeat.front(), 1.0f, 1e-5f);
	EXPECT_NEAR(clamp.front(), 1.0f, 1e-5f);
	// A bright first pixel leaks into the LAST new pixel only when the picture repeats.
	const std::vector<float> bright = {1.0f, 0.0f, 0.0f};
	const auto repeating = resize(bright, 3, 1, image_resample::kRepeat, nw, nh);
	const auto clamping = resize(bright, 3, 1, image_resample::kClamp, nw, nh);
	EXPECT_GT(repeating.back(), clamping.back() + 1e-3f);
}
