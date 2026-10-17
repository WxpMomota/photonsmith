/**
 * @file material_from_photo_tests.cpp
 * @brief "Material from a photo" (src/shared/material_from_photo.h): a patch of a photographed surface becomes a seamless colour picture with the photo's broad light taken out, a height
 *        map that is low in the dark gaps, and an average colour
 *
 * The photographs here are made by hand (stripes, grooves, a gradient of light) so that what the processing should do to each is known.
 */

#include <gtest/gtest.h>

#include "../../src/shared/material_from_photo.h"

#include <cmath>
#include <vector>

namespace {

using material_from_photo::Options;
using material_from_photo::Result;

// A size x size sRGB picture from f(x, y) -> {r, g, b} in 0..1.
template <class F>
std::vector<unsigned char> picture(int size, F f) {
	std::vector<unsigned char> px(static_cast<size_t>(size) * size * 3);
	for (int y = 0; y < size; ++y)
		for (int x = 0; x < size; ++x) {
			const double* c = f(x, y);
			for (int k = 0; k < 3; ++k) px[(static_cast<size_t>(y) * size + x) * 3 + k] = static_cast<unsigned char>(std::clamp(c[k], 0.0, 1.0) * 255.0 + 0.5);
		}
	return px;
}

// The mean of a channel over a block of columns.
double columnsMean(const std::vector<unsigned char>& px, int size, int x0, int x1, int channel = 1) {
	double sum = 0.0;
	int n = 0;
	for (int y = 0; y < size; ++y)
		for (int x = x0; x < x1; ++x, ++n) sum += px[(static_cast<size_t>(y) * size + x) * 3 + channel];
	return sum / std::max(n, 1);
}

// Warm wooden stripes (a dark groove every 16 pixels) lit by a window on the right: the light falls from 1 on the right to 0.35 on the left.
std::vector<unsigned char> litStripes(int size) {
	return picture(size, [size](int x, int y) {
		static double c[3];
		const double phase = (x % 16) / 16.0;
		const double stripe = phase < 0.2 ? 0.25 : 0.8 + 0.1 * std::sin(y * 0.3);   // groove, or wood with a slow ripple along it
		const double light = 0.35 + 0.65 * x / (size - 1);
		c[0] = stripe * light * 0.9;
		c[1] = stripe * light * 0.6;
		c[2] = stripe * light * 0.35;
		return c;
	});
}

}  // namespace

TEST(MaterialFromPhotoTest, TheBroadLightOfThePhotoIsTakenOutAndTheColourStays) {
	const int size = 256;
	const std::vector<unsigned char> photo = litStripes(size);
	const double leftBefore = columnsMean(photo, size, 0, 64), rightBefore = columnsMean(photo, size, size - 64, size);
	ASSERT_GT(rightBefore / leftBefore, 2.0) << "the test photo is lit much brighter on the right";

	const Result r = material_from_photo::make(photo.data(), size);
	ASSERT_EQ(r.size, size);
	ASSERT_EQ(r.albedo.size(), static_cast<size_t>(size) * size * 3);
	const double left = columnsMean(r.albedo, size, 32, 96), right = columnsMean(r.albedo, size, size - 96, size - 32);
	EXPECT_LT(std::fabs(left - right) / std::max(left, right), 0.12) << "the picture is as bright on one side as on the other (" << left << " against " << right << ")";
	EXPECT_GT(r.lightingRange, 1.8) << "and it says the light was uneven";

	// the wood is still warm: red above green above blue
	const double red = columnsMean(r.albedo, size, 0, size, 0), green = columnsMean(r.albedo, size, 0, size, 1), blue = columnsMean(r.albedo, size, 0, size, 2);
	EXPECT_GT(red, green * 1.1);
	EXPECT_GT(green, blue * 1.1);
}

TEST(MaterialFromPhotoTest, WithTheLightLeftAloneTheFlatteningIsOff) {
	const int size = 256;
	const std::vector<unsigned char> photo = litStripes(size);
	Options keep;
	keep.flattenLighting = false;
	keep.seamless = false;
	const Result r = material_from_photo::make(photo.data(), size, keep);
	const double left = columnsMean(r.albedo, size, 0, 64), right = columnsMean(r.albedo, size, size - 64, size);
	EXPECT_GT(right / left, 2.0) << "the picture is what the photo was, gradient and all";
	EXPECT_NEAR(r.lightingRange, 1.0, 1e-9);
}

TEST(MaterialFromPhotoTest, ThePictureRepeatsWithNoVisibleJoin) {
	const int size = 256;
	// a smooth ramp from dark to light with a little texture: the worst case for a seam (the left edge is dark, the right edge light)
	const std::vector<unsigned char> ramp = picture(size, [size](int x, int y) {
		static double c[3];
		const double v = 0.15 + 0.7 * x / (size - 1) + 0.03 * std::sin(x * 0.9) * std::sin(y * 0.7);
		c[0] = c[1] = c[2] = v;
		return c;
	});
	EXPECT_GT(material_from_photo::seamRatio(ramp.data(), size), 4.0) << "the photo as it is has a hard join where it repeats";
	Options noFlatten;
	noFlatten.flattenLighting = false;   // (so that the ramp is still there for the seam to deal with)
	const Result r = material_from_photo::make(ramp.data(), size, noFlatten);
	EXPECT_LT(material_from_photo::seamRatio(r.albedo.data(), size), 1.6) << "the result joins up";
	const Result full = material_from_photo::make(ramp.data(), size);
	EXPECT_LT(material_from_photo::seamRatio(full.albedo.data(), size), 1.6);
}

TEST(MaterialFromPhotoTest, TheGroovesAreLowInTheHeightMapAndItRepeatsToo) {
	const int size = 256;
	const std::vector<unsigned char> photo = picture(size, [](int x, int) {
		static double c[3];
		const double v = (x % 16) < 3 ? 0.12 : 0.7;   // a dark 3-pixel groove every 16 pixels, on a light surface
		c[0] = c[1] = c[2] = v;
		return c;
	});
	Options noSeams;
	noSeams.seamless = false;   // (so that the grooves are where the test knows them to be)
	const Result r = material_from_photo::make(photo.data(), size, noSeams);
	double groove = 0.0, ridge = 0.0;
	int nGroove = 0, nRidge = 0;
	for (int y = 40; y < 216; ++y)
		for (int x = 40; x < 216; ++x) {
			const double h = r.height[static_cast<size_t>(y) * size + x];
			if ((x % 16) >= 1 && (x % 16) < 2) { groove += h; ++nGroove; }          // the middle of a groove
			else if ((x % 16) >= 8 && (x % 16) < 10) { ridge += h; ++nRidge; }      // the middle of the surface between two
		}
	groove /= nGroove;
	ridge /= nRidge;
	EXPECT_LT(groove, 90.0) << "a groove is low";
	EXPECT_GT(ridge, 140.0) << "and the surface between is higher";

	const Result tiled = material_from_photo::make(photo.data(), size);
	EXPECT_LT(material_from_photo::seamRatio(tiled.height.data(), size, 1), 2.5) << "the height map joins up with itself";
}

TEST(MaterialFromPhotoTest, APlainPatchStaysPlainAndGivesItsColour) {
	const int size = 128;
	const std::vector<unsigned char> flat = picture(size, [](int, int) {
		static double c[3] = {0.8, 0.4, 0.2};
		return c;
	});
	const Result r = material_from_photo::make(flat.data(), size);
	for (size_t i = 0; i < r.albedo.size(); i += 97) EXPECT_NEAR(r.albedo[i], flat[i], 4.0);
	for (size_t i = 0; i < r.height.size(); i += 53) EXPECT_NEAR(r.height[i], 128.0, 3.0) << "no relief in a plain picture";
	EXPECT_NEAR(r.averageColor[0], material_from_photo::detail::toLinear(0.8), 0.01);
	EXPECT_NEAR(r.averageColor[2], material_from_photo::detail::toLinear(0.2), 0.01);
	EXPECT_NEAR(r.lightingRange, 1.0, 0.05);
}

TEST(MaterialFromPhotoTest, ARefusedInputGivesAnEmptyResultAndTheSizeFitsThePhoto) {
	EXPECT_EQ(material_from_photo::make(nullptr, 256).size, 0);
	const unsigned char tiny[3 * 4 * 4] = {};
	EXPECT_EQ(material_from_photo::make(tiny, 4).size, 0);
	EXPECT_EQ(material_from_photo::pictureSize(300), 256);
	EXPECT_EQ(material_from_photo::pictureSize(700), 512);
	EXPECT_EQ(material_from_photo::pictureSize(3000), 1024);
	const auto& kinds = material_from_photo::surfaceKinds();
	ASSERT_GE(kinds.size(), 5u);
	EXPECT_TRUE(kinds[0].coated) << "wood is varnished";
	for (const auto& k : kinds) {
		EXPECT_GT(k.reliefMetres, 0.0);
		EXPECT_LT(k.reliefMetres, 0.02) << "a few millimetres at most";
	}
}
