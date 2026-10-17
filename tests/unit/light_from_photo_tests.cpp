/**
 * @file light_from_photo_tests.cpp
 * @brief "Light from a photo" (src/shared/light_from_photo.h): the sun and haze of a photographed sky are read back from pictures of skies whose sun is known
 *
 * The photographs are made here from the Hosek-Wilkie sky model the Builder renders its sky with: a pinhole camera of a known field of view and tilt, a ground below the horizon, a
 * dark building hiding part of the sky, and the sun either in the picture (as a clipped white spot) or just outside it.
 */

#include <gtest/gtest.h>

#include "../../src/shared/light_from_photo.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

using light_from_photo::Estimate;
using light_from_photo::Options;

constexpr double kPi = 3.14159265358979323846;

struct SkyShot {
	double elevation = 25.0;    // the sun's height
	double azimuth = 0.0;       // to the right of the centre of the picture
	double turbidity = 3.0;
	double tilt = 8.0;          // the camera looks up by this much
	double hfov = 70.0;
	bool building = true;       // a dark block hides the sky at the left
	double ground[3] = {0.30, 0.42, 0.24};   // below the horizon (grass; a blue sea is {0.05, 0.18, 0.40})
	bool clouds = false;        // bright white cloud tops, clipped, at several places in the sky
};

double encode(double linear) {
	linear = std::clamp(linear, 0.0, 1.0);
	return linear <= 0.0031308 ? linear * 12.92 : 1.055 * std::pow(linear, 1.0 / 2.4) - 0.055;
}

std::vector<unsigned char> photograph(const SkyShot& s, int w = 320, int h = 240) {
	const double focal = (w / 2.0) / std::tan(s.hfov * kPi / 360.0), cx = w / 2.0, cy = h / 2.0;
	const double sinT = std::sin(s.tilt * kPi / 180.0), cosT = std::cos(s.tilt * kPi / 180.0);
	const double el = s.elevation * kPi / 180.0, az = s.azimuth * kPi / 180.0;
	const double sun[3] = {std::cos(el) * std::sin(az), std::sin(el), std::cos(el) * std::cos(az)};
	const hosek_sky::Model model(s.turbidity, 0.3, el);
	struct Px { double lum[3]; bool sky; bool sunSpot; };
	std::vector<Px> px(static_cast<size_t>(w) * h);
	std::vector<double> ys;
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x) {
			const double dx = (x + 0.5 - cx) / focal, dy = -(y + 0.5 - cy) / focal, dz = 1.0;
			const double yy = dy * cosT + dz * sinT, zz = -dy * sinT + dz * cosT, n = std::sqrt(dx * dx + yy * yy + zz * zz);
			const double d[3] = {dx / n, yy / n, zz / n};
			Px& p = px[static_cast<size_t>(y) * w + x];
			p.sky = d[1] > 0.0;
			const double cosGamma = std::clamp(d[0] * sun[0] + d[1] * sun[1] + d[2] * sun[2], -1.0, 1.0);
			p.sunSpot = p.sky && std::acos(cosGamma) < 1.2 * kPi / 180.0;
			if (!p.sky) continue;
			double xyz[3];
			model.xyz(std::acos(std::min(1.0, d[1])), std::acos(cosGamma), xyz);
			p.lum[0] = 3.2404542 * xyz[0] - 1.5371385 * xyz[1] - 0.4985314 * xyz[2];
			p.lum[1] = -0.9692660 * xyz[0] + 1.8760108 * xyz[1] + 0.0415560 * xyz[2];
			p.lum[2] = 0.0556434 * xyz[0] - 0.2040259 * xyz[1] + 1.0572252 * xyz[2];
			if (!p.sunSpot) ys.push_back(xyz[1]);
		}
	std::sort(ys.begin(), ys.end());
	const double scale = 0.6 / ys[static_cast<size_t>(0.9 * (ys.size() - 1))];   // (an exposure that puts the sky at a pleasant level; the brightest part clips)
	std::vector<unsigned char> out(static_cast<size_t>(w) * h * 3);
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x) {
			const Px& p = px[static_cast<size_t>(y) * w + x];
			double c[3];
			if (p.sunSpot) c[0] = c[1] = c[2] = 1.0;
			else if (p.sky) for (int k = 0; k < 3; ++k) c[k] = encode(std::max(0.0, p.lum[k]) * scale);
			else { c[0] = s.ground[0]; c[1] = s.ground[1]; c[2] = s.ground[2]; }
			if (s.clouds && p.sky) {   // four lumpy clipped tops, none of them the sun
				const double lumps[4][3] = {{0.18, 0.12, 0.05}, {0.45, 0.06, 0.07}, {0.62, 0.20, 0.06}, {0.85, 0.10, 0.04}};
				for (const auto& l : lumps) {
					const double ex = (x / static_cast<double>(w) - l[0]) / l[2], ey = (y / static_cast<double>(h) - l[1]) / (l[2] * 0.7);
					if (ex * ex + ey * ey + 0.15 * std::sin(9.0 * x + 5.0 * y) < 1.0) c[0] = c[1] = c[2] = 1.0;
				}
			}
			if (s.building && x >= static_cast<int>(0.10 * w) && x < static_cast<int>(0.25 * w) && y < static_cast<int>(0.6 * h)) c[0] = c[1] = c[2] = 0.05;
			for (int k = 0; k < 3; ++k) out[(static_cast<size_t>(y) * w + x) * 3 + k] = static_cast<unsigned char>(std::clamp(c[k], 0.0, 1.0) * 255.0 + 0.5);
		}
	return out;
}

}  // namespace

TEST(LightFromPhotoTest, ASunJustOutsideThePictureIsFoundByTheGlowOfTheSky) {
	SkyShot shot;
	shot.elevation = 25.0;
	shot.azimuth = -48.0;   // to the left, just outside the 70-degree picture
	const std::vector<unsigned char> px = photograph(shot);
	const Estimate e = light_from_photo::estimate(px.data(), 320, 240);
	ASSERT_TRUE(e.ok) << e.why;
	EXPECT_FALSE(e.sunSeen);
	EXPECT_TRUE(e.horizonFound);
	EXPECT_NEAR(e.tiltDegrees, shot.tilt, 3.0) << "the horizon gives the tilt";
	EXPECT_NEAR(e.sunElevation, shot.elevation, 12.0) << "the sun's height (found " << e.sunElevation << ")";
	EXPECT_NEAR(e.sunAzimuth, shot.azimuth, 25.0) << "to the left of the picture (found " << e.sunAzimuth << ")";
	EXPECT_GT(e.confidence, 0.5) << "and the fitted sky explains most of the sky's variation";
	EXPECT_GT(e.skyFraction, 0.3);
}

TEST(LightFromPhotoTest, ASunInThePictureIsReadOffDirectly) {
	SkyShot shot;
	shot.elevation = 25.0;
	shot.azimuth = 15.0;
	const std::vector<unsigned char> px = photograph(shot);
	const Estimate e = light_from_photo::estimate(px.data(), 320, 240);
	ASSERT_TRUE(e.ok) << e.why;
	EXPECT_TRUE(e.sunSeen) << "the clipped spot in the sky is the sun";
	EXPECT_NEAR(e.sunElevation, shot.elevation, 3.0) << e.sunElevation;
	EXPECT_NEAR(e.sunAzimuth, shot.azimuth, 3.0) << e.sunAzimuth;
}

TEST(LightFromPhotoTest, LeftIsLeftAndRightIsRight) {
	for (const double azimuth : {-48.0, 48.0}) {
		SkyShot shot;
		shot.azimuth = azimuth;
		shot.building = false;
		const std::vector<unsigned char> px = photograph(shot);
		const Estimate e = light_from_photo::estimate(px.data(), 320, 240);
		ASSERT_TRUE(e.ok) << e.why;
		EXPECT_GT(e.sunAzimuth * azimuth, 0.0) << "the sun at " << azimuth << " was found at " << e.sunAzimuth;
		EXPECT_NEAR(e.sunAzimuth, azimuth, 30.0);
	}
}

TEST(LightFromPhotoTest, AHazierSkyIsReadAsHazier) {
	SkyShot clear, hazy;
	clear.turbidity = 2.0;
	hazy.turbidity = 8.0;
	clear.azimuth = hazy.azimuth = 20.0;   // (in the picture: the haze is read from how the sky falls off round the sun)
	const std::vector<unsigned char> a = photograph(clear), b = photograph(hazy);
	const Estimate ea = light_from_photo::estimate(a.data(), 320, 240), eb = light_from_photo::estimate(b.data(), 320, 240);
	ASSERT_TRUE(ea.ok && eb.ok);
	EXPECT_LT(ea.turbidity, eb.turbidity) << "clear " << ea.turbidity << ", hazy " << eb.turbidity;
}

TEST(LightFromPhotoTest, AGivenTiltIsUsedAndTheHorizonIsNotSearchedFor) {
	SkyShot shot;
	shot.azimuth = -48.0;
	const std::vector<unsigned char> px = photograph(shot);
	Options o;
	o.tiltGiven = true;
	o.tiltDegrees = shot.tilt;
	const Estimate e = light_from_photo::estimate(px.data(), 320, 240, o);
	ASSERT_TRUE(e.ok) << e.why;
	EXPECT_FALSE(e.horizonFound);
	EXPECT_DOUBLE_EQ(e.tiltDegrees, shot.tilt);
	EXPECT_NEAR(e.sunAzimuth, shot.azimuth, 25.0);
}

TEST(LightFromPhotoTest, AnEvenSkyIsSaidToBeOvercastAndANoSkyIsRefused) {
	const int w = 160, h = 120;
	std::vector<unsigned char> overcast(static_cast<size_t>(w) * h * 3);
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x) {
			const bool ground = y > 80;
			const unsigned char c[3] = {ground ? static_cast<unsigned char>(70) : static_cast<unsigned char>(185), ground ? static_cast<unsigned char>(100) : static_cast<unsigned char>(195),
			                            ground ? static_cast<unsigned char>(60) : static_cast<unsigned char>(205)};
			for (int k = 0; k < 3; ++k) overcast[(static_cast<size_t>(y) * w + x) * 3 + k] = c[k];
		}
	const Estimate e = light_from_photo::estimate(overcast.data(), w, h);
	ASSERT_TRUE(e.ok) << e.why;
	EXPECT_TRUE(e.flatSky);
	EXPECT_DOUBLE_EQ(e.turbidity, 10.0);
	EXPECT_DOUBLE_EQ(e.confidence, 0.0);

	std::vector<unsigned char> indoors(static_cast<size_t>(w) * h * 3, 90);   // a dark grey room: no sky
	const Estimate none = light_from_photo::estimate(indoors.data(), w, h);
	EXPECT_FALSE(none.ok);
	EXPECT_NE(none.why.find("sky"), std::string::npos) << none.why;
	EXPECT_FALSE(light_from_photo::estimate(nullptr, w, h).ok);
}

TEST(LightFromPhotoTest, ASkyThatFillsThePictureHasNoHorizonAndSaysSo) {
	SkyShot shot;
	shot.tilt = 40.0;   // looking high up: the horizon is below the bottom of the picture
	shot.azimuth = -30.0;
	shot.building = false;
	const std::vector<unsigned char> px = photograph(shot);
	const Estimate e = light_from_photo::estimate(px.data(), 320, 240);
	ASSERT_TRUE(e.ok) << e.why;
	EXPECT_FALSE(e.horizonFound);
	EXPECT_DOUBLE_EQ(e.tiltDegrees, 0.0);
	EXPECT_NE(e.why.find("horizon"), std::string::npos) << "it says the camera was taken to look level: " << e.why;
}

TEST(LightFromPhotoTest, ABlueSeaBelowTheHorizonIsNotTakenForSky) {
	// found on a real beach photo: the blue sea is blue like the sky, and was counted as sky (its darkness was the clue: a sky does not fall off towards the horizon)
	SkyShot shot;
	shot.azimuth = -48.0;
	shot.building = false;
	shot.ground[0] = 0.05;
	shot.ground[1] = 0.18;
	shot.ground[2] = 0.40;
	const std::vector<unsigned char> px = photograph(shot);
	const Estimate e = light_from_photo::estimate(px.data(), 320, 240);
	ASSERT_TRUE(e.ok) << e.why;
	EXPECT_TRUE(e.horizonFound);
	EXPECT_NEAR(e.tiltDegrees, shot.tilt, 3.0) << "the horizon is where the sky ends, not where the sea does";
	EXPECT_LT(e.skyFraction, 0.75) << "the sea below the horizon is not sky";
	EXPECT_NEAR(e.sunAzimuth, shot.azimuth, 30.0);
}

TEST(LightFromPhotoTest, BrightWhiteCloudTopsAreNotTheSun) {
	// found on real photos: clipped cloud tops were read as the sun. A sun is one spot; clouds give several.
	SkyShot shot;
	shot.elevation = 25.0;
	shot.azimuth = -48.0;   // the sun is outside the picture
	shot.building = false;
	shot.clouds = true;
	const std::vector<unsigned char> px = photograph(shot);
	const Estimate e = light_from_photo::estimate(px.data(), 320, 240);
	ASSERT_TRUE(e.ok) << e.why;
	EXPECT_FALSE(e.sunSeen) << "several bright spots in the sky are clouds";
}

TEST(LightFromPhotoTest, ASkyThatIsOnlyAThinStripIsNotClaimedToBeWellFitted) {
	// found on a real photo of a beach and a hazy sunrise: the fit rested on very little sky and reported near-perfect confidence
	SkyShot shot;
	shot.tilt = -24.0;   // looking well down: the sky is a thin strip at the top
	shot.azimuth = -30.0;
	shot.building = false;
	const std::vector<unsigned char> px = photograph(shot);
	const Estimate e = light_from_photo::estimate(px.data(), 320, 240);
	ASSERT_TRUE(e.ok) << e.why;
	EXPECT_LT(e.skyFraction, 0.15);
	EXPECT_LE(e.confidence, 0.3) << "little sky: little confidence";
	EXPECT_NE(e.why.find("small part"), std::string::npos) << e.why;
}
