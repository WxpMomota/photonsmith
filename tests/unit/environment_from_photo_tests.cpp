/**
 * @file environment_from_photo_tests.cpp
 * @brief The photo as the scene's environment picture (src/shared/environment_from_photo.h): where it lands in the equirectangular picture, how the rest is filled, the scale
 */

#include <gtest/gtest.h>

#include "../../src/shared/environment_from_photo.h"

#include <cmath>
#include <vector>

namespace {
using namespace environment_from_photo;

Frame lookingDownMinusZ(double fov = 60.0, double aspect = 1.5) {
	const double position[3] = {0, 0, 0}, target[3] = {0, 0, -1}, up[3] = {0, 1, 0};
	return frameOf(position, target, up, fov, aspect);
}

// The pixel of the environment picture in the direction (dx, dy, dz).
const float* at(const std::vector<float>& env, int w, int h, double dx, double dy, double dz) {
	const double l = std::sqrt(dx * dx + dy * dy + dz * dz);
	dx /= l; dy /= l; dz /= l;
	const double theta = std::acos(dy);
	double phi = std::atan2(-dz, dx);
	if (phi < 0) phi += 2.0 * kPi;
	const int col = std::min(w - 1, static_cast<int>(phi / (2.0 * kPi) * w)), row = std::min(h - 1, static_cast<int>(theta / kPi * h));
	return &env[(static_cast<size_t>(row) * w + col) * 3];
}
}  // namespace

TEST(EnvironmentFromPhotoTest, TheFrameOfACameraLookingDownMinusZ) {
	const Frame f = lookingDownMinusZ(90.0, 2.0);
	EXPECT_NEAR(f.forward[2], -1.0, 1e-9);
	EXPECT_NEAR(f.right[0], 1.0, 1e-9) << "right-handed: forward x up";
	EXPECT_NEAR(f.up[1], 1.0, 1e-9);
	EXPECT_NEAR(f.tanHalfY, 1.0, 1e-9);
	EXPECT_NEAR(f.tanHalfX, 2.0, 1e-9);
}

TEST(EnvironmentFromPhotoTest, APlainPhotoMakesAPlainEnvironmentWhateverIsFilledIn) {
	const int w = 64, h = 48, ow = 128, oh = 64;
	const std::vector<unsigned char> photo(static_cast<size_t>(w) * h * 3, 128);
	const std::vector<float> env = make(photo.data(), w, h, lookingDownMinusZ(), ow, oh);
	ASSERT_EQ(env.size(), static_cast<size_t>(ow) * oh * 3);
	const double linear = detail::decode(128.0 / 255.0);
	for (const float v : env) EXPECT_NEAR(v, linear, 0.01) << "covered or filled, a plain photo is plain";
}

TEST(EnvironmentFromPhotoTest, ThePhotoLandsWhereTheCameraLooksAndTopStaysAbove) {
	const int w = 64, h = 64, ow = 256, oh = 128;
	std::vector<unsigned char> photo(static_cast<size_t>(w) * h * 3);
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x) {
			unsigned char* p = &photo[(static_cast<size_t>(y) * w + x) * 3];
			p[0] = y < h / 2 ? 230 : 20;   // red on top ("sky"), blue below ("ground")
			p[1] = 20;
			p[2] = y < h / 2 ? 20 : 230;
		}
	const std::vector<float> env = make(photo.data(), w, h, lookingDownMinusZ(60.0, 1.0), ow, oh);
	const float* above = at(env, ow, oh, 0.0, 0.2, -1.0);
	const float* below = at(env, ow, oh, 0.0, -0.2, -1.0);
	EXPECT_GT(above[0], 0.5f);
	EXPECT_LT(above[2], 0.1f);
	EXPECT_GT(below[2], 0.5f);
	EXPECT_LT(below[0], 0.1f);
}

TEST(EnvironmentFromPhotoTest, WhatTheCameraSideLooksAtGoesToTheRightPlace) {
	// left half of the photo green, right half red: a direction to the camera's right (+X for a camera looking down -Z) is red, to the left green
	const int w = 64, h = 64, ow = 256, oh = 128;
	std::vector<unsigned char> photo(static_cast<size_t>(w) * h * 3);
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x) {
			unsigned char* p = &photo[(static_cast<size_t>(y) * w + x) * 3];
			p[0] = x < w / 2 ? 10 : 240;
			p[1] = x < w / 2 ? 240 : 10;
			p[2] = 10;
		}
	const std::vector<float> env = make(photo.data(), w, h, lookingDownMinusZ(50.0, 1.0), ow, oh);
	const float* right = at(env, ow, oh, 0.25, 0.0, -1.0);
	const float* left = at(env, ow, oh, -0.25, 0.0, -1.0);
	EXPECT_GT(right[0], right[1]);
	EXPECT_GT(left[1], left[0]);
}

TEST(EnvironmentFromPhotoTest, WhatTheCameraDidNotSeeIsFilledFromThePhotoAndNothingIsInvented) {
	const int w = 64, h = 64, ow = 128, oh = 64;
	std::vector<unsigned char> photo(static_cast<size_t>(w) * h * 3);
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
			for (int c = 0; c < 3; ++c) photo[(static_cast<size_t>(y) * w + x) * 3 + c] = static_cast<unsigned char>(40 + (x + y) * 160 / (w + h));
	const std::vector<float> env = make(photo.data(), w, h, lookingDownMinusZ(60.0, 1.0), ow, oh);
	const double lo = detail::decode(40.0 / 255.0), hi = detail::decode(200.0 / 255.0);
	for (const float v : env) {
		EXPECT_TRUE(std::isfinite(v));
		EXPECT_GE(v, lo - 0.01);
		EXPECT_LE(v, hi + 0.01);
	}
	const float* behind = at(env, ow, oh, 0.0, 0.0, 1.0);
	EXPECT_GT(behind[0], lo - 0.01);
}

TEST(EnvironmentFromPhotoTest, TheScaleMakesTheUpperHalfAsBrightAsTheSkyItReplaces) {
	const int w = 32, h = 32, ow = 128, oh = 64;
	std::vector<unsigned char> photo(static_cast<size_t>(w) * h * 3, 90);
	const std::vector<float> env = make(photo.data(), w, h, lookingDownMinusZ(), ow, oh, 0.37);
	double sum = 0.0;
	for (int row = 0; row < oh / 2; ++row)
		for (int col = 0; col < ow; ++col) sum += detail::luminance(&env[(static_cast<size_t>(row) * ow + col) * 3]);
	EXPECT_NEAR(sum / (ow * (oh / 2)), 0.37, 0.005);
}

TEST(EnvironmentFromPhotoTest, ThePicturesAreRecognisedByTheirName) {
	EXPECT_TRUE(isEnvironmentFile("C:/Users/me/assets/skies/photo_env_0123456789ab.hdr"));
	EXPECT_TRUE(isEnvironmentFile("C:\\skies\\photo_env_x.hdr"));
	EXPECT_TRUE(isEnvironmentFile("photo_env_x.hdr"));
	EXPECT_FALSE(isEnvironmentFile("C:/skies/sky_v1_e45.0_a0_t3.0_g0.30.hdr"));
	EXPECT_FALSE(isEnvironmentFile(""));
}

TEST(EnvironmentFromPhotoTest, ANotUsablePhotoOrSizeMakesNothing) {
	const unsigned char px[12] = {};
	EXPECT_TRUE(make(nullptr, 2, 2, Frame(), 64, 32).empty());
	EXPECT_TRUE(make(px, 1, 2, Frame(), 64, 32).empty());
	EXPECT_TRUE(make(px, 2, 2, Frame(), 4, 2).empty());
}
