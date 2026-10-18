/**
 * @file occluder_mask_tests.cpp
 * @brief The mask of what is in front of an object put into a photo (src/shared/occluder_mask.h) and its use by the compositor (photo_composite.h)
 */

#include <gtest/gtest.h>

#include "../../src/shared/occluder_mask.h"
#include "../../src/shared/photo_composite.h"

#include <cstdint>
#include <vector>

namespace {
int countSet(const std::vector<std::uint8_t>& m) {
	int n = 0;
	for (const std::uint8_t v : m) n += v != 0;
	return n;
}
}  // namespace

TEST(OccluderMaskTest, ABrushPaintsADiscAndTheEraserTakesItOut) {
	const int w = 40, h = 30;
	std::vector<std::uint8_t> m(w * h, 0);
	EXPECT_FALSE(occluder_mask::any(m));
	occluder_mask::paintDisc(m, w, h, 20.0, 15.0, 5.0, false);
	const int painted = countSet(m);
	EXPECT_NEAR(painted, 3.14159 * 25, 8) << "a disc of radius 5";
	EXPECT_EQ(m[15 * w + 20], 255);
	EXPECT_EQ(m[15 * w + 30], 0);
	occluder_mask::paintDisc(m, w, h, 20.0, 15.0, 2.0, true);
	EXPECT_EQ(m[15 * w + 20], 0) << "erased in the middle";
	EXPECT_EQ(m[15 * w + 24], 255) << "the ring round it stays";
	occluder_mask::paintDisc(m, w, h, -50.0, -50.0, 5.0, false);   // (off the picture: nothing, no crash)
	occluder_mask::paintDisc(m, w, h, 5.0, 5.0, 0.0, false);
	occluder_mask::paintDisc(m, 7, 7, 5.0, 5.0, 3.0, false);   // (a mask that is not the stated size is left alone)
}

TEST(OccluderMaskTest, AFastStrokeHasNoGaps) {
	const int w = 200, h = 20;
	std::vector<std::uint8_t> m(w * h, 0);
	occluder_mask::paintStroke(m, w, h, 10.0, 10.0, 190.0, 10.0, 3.0, false);   // one jump of the mouse across the picture
	for (int x = 10; x <= 190; ++x) EXPECT_EQ(m[10 * w + x], 255) << "x = " << x;
}

TEST(OccluderMaskTest, TheWandSelectsTheJoinedRegionOfSimilarColourOnly) {
	const int w = 30, h = 20;
	std::vector<unsigned char> rgb(w * h * 3);
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x) {
			unsigned char* p = &rgb[(y * w + x) * 3];
			const bool green = x < 10 || (x >= 20 && y < 5);   // a green region on the left, and an island of green apart from it
			p[0] = green ? 40 : 200;
			p[1] = green ? 120 : 200;
			p[2] = green ? 40 : 220;
			if (x == 5 && y == 5) { p[0] = 46; p[1] = 126; p[2] = 44; }   // a slightly different pixel inside the green
		}
	std::vector<std::uint8_t> m(w * h, 0);
	const int n = occluder_mask::floodSelect(rgb.data(), w, h, 2, 10, 12, m, false);
	EXPECT_EQ(n, 10 * 20) << "the green on the left, the slightly different pixel included";
	EXPECT_EQ(m[5 * w + 5], 255);
	EXPECT_EQ(m[2 * w + 22], 0) << "the island of the same green that is not joined to it is not taken";
	EXPECT_EQ(m[10 * w + 15], 0);
	std::vector<std::uint8_t> tight(w * h, 0);   // a tight tolerance leaves the odd pixel out
	occluder_mask::floodSelect(rgb.data(), w, h, 2, 10, 2, tight, false);
	EXPECT_EQ(tight[5 * w + 5], 0);
	const int erased = occluder_mask::floodSelect(rgb.data(), w, h, 2, 10, 12, m, true);   // erasing with the wand
	EXPECT_EQ(erased, 200);
	EXPECT_FALSE(occluder_mask::any(m));
	std::vector<std::uint8_t> capped(w * h, 0);   // the limit and the edges of the picture
	EXPECT_EQ(occluder_mask::floodSelect(rgb.data(), w, h, 2, 10, 255, capped, false, 50), 50) << "a tolerance that takes in everything stops at the cap";
	EXPECT_EQ(occluder_mask::floodSelect(rgb.data(), w, h, -1, 0, 10, capped, false), 0);
	EXPECT_EQ(occluder_mask::floodSelect(nullptr, w, h, 0, 0, 10, capped, false), 0);
}

TEST(OccluderMaskTest, FeatheringMakesARampAndLeavesTheMiddleAlone) {
	const int w = 60, h = 20;
	std::vector<std::uint8_t> m(w * h, 0);
	for (int y = 0; y < h; ++y)
		for (int x = 20; x < 40; ++x) m[y * w + x] = 255;
	occluder_mask::feather(m, w, h, 2);
	EXPECT_EQ(m[10 * w + 30], 255) << "well inside: still fully in front";
	EXPECT_EQ(m[10 * w + 5], 0) << "well outside: still nothing";
	EXPECT_GT(m[10 * w + 19], 0);
	EXPECT_LT(m[10 * w + 19], 255) << "the edge is a ramp";
	EXPECT_GT(m[10 * w + 21], m[10 * w + 19]) << "rising into the mask";
}

TEST(OccluderMaskTest, ResizingKeepsTheShapeAndTheSoftness) {
	const int w = 8, h = 8;
	std::vector<std::uint8_t> m(w * h, 0);
	for (int y = 0; y < h; ++y)
		for (int x = 4; x < w; ++x) m[y * w + x] = 255;
	const std::vector<std::uint8_t> big = occluder_mask::resized(m, w, h, 32, 32);
	ASSERT_EQ(big.size(), 32u * 32u);
	EXPECT_EQ(big[16 * 32 + 2], 0);
	EXPECT_EQ(big[16 * 32 + 30], 255);
	EXPECT_GT(big[16 * 32 + 16], 0) << "the edge is blended, not a staircase";
	EXPECT_LT(big[16 * 32 + 16], 255);
	EXPECT_EQ(occluder_mask::resized(m, w, h, 8, 8), m);
	EXPECT_TRUE(occluder_mask::resized(m, 3, 3, 8, 8).empty()) << "a mask that is not the stated size";
}

TEST(OccluderMaskTest, WhatIsInFrontShowsThePhotoAndNotTheObjectOrItsShadow) {
	const int w = 16, h = 8;
	const size_t n = static_cast<size_t>(w) * h;
	std::vector<unsigned char> photo(n * 3, 100);
	std::vector<float> ground(n * 3, 0.3f), with(n * 3, 0.3f), objectMask(n, 0.0f);
	for (int y = 0; y < h; ++y)
		for (int x = 4; x < 12; ++x) {   // the object: a bright one over the middle
			const size_t i = static_cast<size_t>(y) * w + x;
			objectMask[i] = 1.0f;
			for (int c = 0; c < 3; ++c) with[i * 3 + c] = 1.5f;
		}
	std::vector<unsigned char> front(n, 0);   // a post in front of the right half of the object
	for (int y = 0; y < h; ++y)
		for (int x = 8; x < 12; ++x) front[static_cast<size_t>(y) * w + x] = 255;
	const std::vector<unsigned char> without = photo_composite::composite(photo.data(), with.data(), ground.data(), objectMask.data(), w, h);
	const std::vector<unsigned char> hidden = photo_composite::composite(photo.data(), with.data(), ground.data(), objectMask.data(), w, h, photo_composite::Options(), front.data());
	ASSERT_EQ(hidden.size(), without.size());
	const size_t left = (static_cast<size_t>(4) * w + 5) * 3, right = (static_cast<size_t>(4) * w + 10) * 3;
	EXPECT_EQ(hidden[left], without[left]) << "the part of the object that nothing is in front of is as it was";
	EXPECT_NE(without[right], 100) << "the object is there without the mask";
	EXPECT_EQ(hidden[right], 100) << "where something is in front the pixel is the photo's own";
	EXPECT_EQ(hidden[right + 1], 100);
	std::vector<unsigned char> half(n, 0);   // half in front: half way between
	half[static_cast<size_t>(4) * w + 10] = 128;
	const std::vector<unsigned char> blended = photo_composite::composite(photo.data(), with.data(), ground.data(), objectMask.data(), w, h, photo_composite::Options(), half.data());
	EXPECT_GT(blended[right], std::min(without[right], static_cast<unsigned char>(100)));
	EXPECT_LT(blended[right], std::max(without[right], static_cast<unsigned char>(100)));
}
