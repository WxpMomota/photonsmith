/**
 * @file camera_from_photo_tests.cpp
 * @brief "Camera from a photo" (src/shared/camera_from_photo.h): the lens from a photo's EXIF data, the tilt and roll from its horizon, and the Builder camera that sees a rendered scene the same way
 *
 * The photographs are made here from a camera of known tilt, roll and lens (a sky above the horizon, a ground below, a building standing up into the sky), with geometry written out
 * independently of the estimator. The Builder camera made from the result is checked by rendering a scene through it: a huge ground and a plain sky, whose horizon must land where the photo's did.
 */

#include <gtest/gtest.h>
#include "scene_builder_test_helpers.h"

#include "../../src/shared/camera_from_photo.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <random>
#include <string>
#include <vector>

using namespace scene_doc;

namespace {

constexpr double kPi = 3.14159265358979323846;

struct Shot {
	double tilt = 0.0;     // looking up (+)
	double roll = 0.0;     // counter-clockwise seen from behind (+): the horizon then runs down to the right in the picture
	double hfov = 65.0;
	bool building = false;  // a dark block standing up into the sky at the right
};

// A photograph from a camera at (0, 0, 0) looking along +z (x to the right, y up) tilted up by `tilt` and rolled: a sky that is paler towards the horizon above the horizon, a ground below.
std::vector<unsigned char> photograph(const Shot& s, int w, int h) {
	const double focal = (w / 2.0) / std::tan(s.hfov * kPi / 360.0), cx = w / 2.0, cy = h / 2.0;
	const double t = s.tilt * kPi / 180.0, r = s.roll * kPi / 180.0;
	const double f[3] = {0.0, std::sin(t), std::cos(t)}, r0[3] = {1.0, 0.0, 0.0}, u0[3] = {0.0, std::cos(t), -std::sin(t)};
	double right[3], up[3];
	for (int k = 0; k < 3; ++k) {
		right[k] = r0[k] * std::cos(r) + u0[k] * std::sin(r);   // (the right end of the frame goes up when the camera is rolled counter-clockwise)
		up[k] = u0[k] * std::cos(r) - r0[k] * std::sin(r);
	}
	std::vector<unsigned char> px(static_cast<size_t>(w) * h * 3);
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x) {
			double d[3];
			for (int k = 0; k < 3; ++k) d[k] = f[k] + right[k] * (x + 0.5 - cx) / focal - up[k] * (y + 0.5 - cy) / focal;
			const double n = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
			double c[3];
			if (d[1] > 0.0) {
				const double e = d[1] / n;   // the sky's height: pale at the horizon, deeper blue above
				c[0] = 0.30 + 0.45 * (1.0 - e);
				c[1] = 0.50 + 0.35 * (1.0 - e);
				c[2] = 0.92;
			} else {
				c[0] = 0.30; c[1] = 0.36; c[2] = 0.22;   // grass: not sky-like
			}
			if (s.building && x >= static_cast<int>(0.55 * w) && x < static_cast<int>(0.68 * w) && y < static_cast<int>(h * 0.62)) c[0] = c[1] = c[2] = 0.06;
			for (int k = 0; k < 3; ++k) px[(static_cast<size_t>(y) * w + x) * 3 + k] = static_cast<unsigned char>(std::clamp(c[k], 0.0, 1.0) * 255.0 + 0.5);
		}
	return px;
}

// A JPEG file's start with an EXIF block that holds FocalLengthIn35mmFilm = `focal` (and nothing else), in the given byte order.
std::vector<unsigned char> jpegWithFocal(unsigned focal, bool little, bool inExifIfd = true) {
	std::vector<unsigned char> tiff;
	const auto u16 = [&](unsigned v) { if (little) { tiff.push_back(v & 255); tiff.push_back((v >> 8) & 255); } else { tiff.push_back((v >> 8) & 255); tiff.push_back(v & 255); } };
	const auto u32 = [&](unsigned v) { if (little) { for (int i = 0; i < 4; ++i) tiff.push_back((v >> (8 * i)) & 255); } else { for (int i = 3; i >= 0; --i) tiff.push_back((v >> (8 * i)) & 255); } };
	tiff.push_back(little ? 'I' : 'M');
	tiff.push_back(little ? 'I' : 'M');
	u16(42);
	u32(8);                        // IFD0 at 8
	u16(1);                        // one entry: ExifIFD pointer (0x8769, LONG) -> 26
	u16(0x8769); u16(4); u32(1); u32(26);
	u32(0);                        // (no next IFD)
	// at 26: the Exif IFD with one entry: 0xA405 SHORT
	u16(1);
	u16(inExifIfd ? 0xA405 : 0xA406); u16(3); u32(1); u16(focal); u16(0);
	u32(0);
	std::vector<unsigned char> out = {0xFF, 0xD8, 0xFF, 0xE1};
	const size_t length = 2 + 6 + tiff.size();
	out.push_back(static_cast<unsigned char>(length >> 8));
	out.push_back(static_cast<unsigned char>(length & 255));
	for (const char c : std::string("Exif\0\0", 6)) out.push_back(static_cast<unsigned char>(c));
	out.insert(out.end(), tiff.begin(), tiff.end());
	out.push_back(0xFF);
	out.push_back(0xD9);
	return out;
}

}  // namespace

TEST(CameraFromPhotoTest, TheTiltAndRollAreReadOffTheHorizon) {
	const Shot shots[] = {{0.0, 0.0}, {8.0, 0.0}, {-6.0, 0.0}, {0.0, 5.0}, {5.0, -4.0}, {10.0, 7.0}, {-4.0, -8.0}};
	for (const Shot& s : shots) {
		const std::vector<unsigned char> px = photograph(s, 320, 240);
		camera_from_photo::Options o;
		o.hfovDegrees = s.hfov;
		const camera_from_photo::Estimate e = camera_from_photo::estimate(px.data(), 320, 240, o);
		ASSERT_TRUE(e.ok) << e.why;
		EXPECT_TRUE(e.horizonFound) << "tilt " << s.tilt << ", roll " << s.roll << ": " << e.why;
		EXPECT_NEAR(e.tiltDegrees, s.tilt, 1.0) << "tilt " << s.tilt << ", roll " << s.roll;
		EXPECT_NEAR(e.rollDegrees, s.roll, 1.0) << "tilt " << s.tilt << ", roll " << s.roll;
	}
}

TEST(CameraFromPhotoTest, ABuildingStandingUpIntoTheSkyDoesNotBendTheHorizon) {
	for (const Shot& s : {Shot{4.0, 3.0, 65.0, true}, Shot{-3.0, -5.0, 65.0, true}}) {
		const std::vector<unsigned char> px = photograph(s, 320, 240);
		const camera_from_photo::Estimate e = camera_from_photo::estimate(px.data(), 320, 240);
		ASSERT_TRUE(e.ok) << e.why;
		EXPECT_TRUE(e.horizonFound) << e.why;
		EXPECT_NEAR(e.tiltDegrees, s.tilt, 1.5);
		EXPECT_NEAR(e.rollDegrees, s.roll, 1.5);
	}
}

TEST(CameraFromPhotoTest, WhatIsGivenIsUsedAndWithoutAHorizonItSaysSo) {
	// no horizon: the camera looks high up and the sky fills the picture
	Shot up;
	up.tilt = 45.0;
	const std::vector<unsigned char> sky = photograph(up, 320, 240);
	camera_from_photo::Estimate e = camera_from_photo::estimate(sky.data(), 320, 240);
	ASSERT_TRUE(e.ok);
	EXPECT_FALSE(e.horizonFound);
	EXPECT_DOUBLE_EQ(e.tiltDegrees, 0.0);
	EXPECT_DOUBLE_EQ(e.rollDegrees, 0.0);
	EXPECT_NE(e.why.find("horizon"), std::string::npos) << e.why;
	// given
	camera_from_photo::Options o;
	o.tiltGiven = o.rollGiven = true;
	o.tiltDegrees = 12.0;
	o.rollDegrees = -3.0;
	e = camera_from_photo::estimate(sky.data(), 320, 240, o);
	EXPECT_TRUE(e.ok);
	EXPECT_DOUBLE_EQ(e.tiltDegrees, 12.0);
	EXPECT_DOUBLE_EQ(e.rollDegrees, -3.0);
	// one given, the other read
	Shot s;
	s.tilt = 6.0;
	s.roll = 4.0;
	const std::vector<unsigned char> px = photograph(s, 320, 240);
	camera_from_photo::Options half;
	half.rollGiven = true;
	half.rollDegrees = 4.0;
	e = camera_from_photo::estimate(px.data(), 320, 240, half);
	EXPECT_NEAR(e.tiltDegrees, 6.0, 1.0);
	EXPECT_DOUBLE_EQ(e.rollDegrees, 4.0);
	// no sky at all
	const std::vector<unsigned char> indoors(static_cast<size_t>(160) * 120 * 3, 90);
	e = camera_from_photo::estimate(indoors.data(), 160, 120);
	EXPECT_TRUE(e.ok);
	EXPECT_FALSE(e.horizonFound);
	EXPECT_NE(e.why.find("sky"), std::string::npos) << e.why;
	EXPECT_FALSE(camera_from_photo::estimate(nullptr, 100, 100).ok);
}

TEST(CameraFromPhotoTest, TheLensIsReadFromTheExifDataWhateverItsByteOrderAndNothingElseIsTrusted) {
	for (const bool little : {true, false}) {
		const std::vector<unsigned char> file = jpegWithFocal(26, little);
		EXPECT_DOUBLE_EQ(camera_from_photo::exifFocalLength35mm(file.data(), file.size()), 26.0) << (little ? "little" : "big") << " endian";
		EXPECT_DOUBLE_EQ(camera_from_photo::exifFocalLength35mm(jpegWithFocal(77, little).data(), jpegWithFocal(77, little).size()), 77.0);
		const std::vector<unsigned char> other = jpegWithFocal(26, little, false);   // (the tag is not there)
		EXPECT_DOUBLE_EQ(camera_from_photo::exifFocalLength35mm(other.data(), other.size()), 0.0);
		// every truncation of a valid file, and the file with each byte damaged, is read without a crash and without inventing a value
		for (size_t n = 0; n < file.size(); ++n) {
			const double f = camera_from_photo::exifFocalLength35mm(file.data(), n);
			EXPECT_TRUE(f == 0.0 || f == 26.0) << "truncated at " << n;
		}
		std::mt19937 rng(5);
		for (int i = 0; i < 2000; ++i) {
			std::vector<unsigned char> damaged = file;
			damaged[rng() % damaged.size()] = static_cast<unsigned char>(rng());
			const double f = camera_from_photo::exifFocalLength35mm(damaged.data(), damaged.size());
			EXPECT_TRUE(f >= 0.0 && f < 2000.0);
		}
	}
	EXPECT_DOUBLE_EQ(camera_from_photo::exifFocalLength35mm(nullptr, 0), 0.0);
	const unsigned char png[] = {0x89, 'P', 'N', 'G', 13, 10, 26, 10, 0, 0, 0, 0};
	EXPECT_DOUBLE_EQ(camera_from_photo::exifFocalLength35mm(png, sizeof png), 0.0);
	std::vector<unsigned char> noise(4096);
	std::mt19937 rng(9);
	for (auto& b : noise) b = static_cast<unsigned char>(rng());
	noise[0] = 0xFF;
	noise[1] = 0xD8;
	EXPECT_DOUBLE_EQ(camera_from_photo::exifFocalLength35mm(noise.data(), noise.size()), 0.0) << "a JPEG start followed by noise";
}

TEST(CameraFromPhotoTest, TheFocalLengthGivesTheFieldOfViewForThePicturesShape) {
	// 26 mm (a phone's main camera) on a 36 x 24 mm frame (3:2) sees 2 atan(18 / 26) = 69.4 degrees across; the frame's diagonal (43.27 mm) is the same for any shape, so across a 4:3 picture's
	// long side (a 34.6 mm wide frame) it is 2 atan(17.3 / 26) = 67.3, and across a portrait 4:3 one's short side (a 26 mm wide one) 2 atan(13 / 26) = 53.1
	EXPECT_NEAR(camera_from_photo::hfovFromFocal35(26.0, 3000, 2000), 69.4, 0.2);
	EXPECT_NEAR(camera_from_photo::hfovFromFocal35(26.0, 4000, 3000), 67.3, 0.2);
	EXPECT_NEAR(camera_from_photo::hfovFromFocal35(26.0, 3000, 4000), 53.1, 0.2);
	EXPECT_GT(camera_from_photo::hfovFromFocal35(14.0, 4000, 3000), camera_from_photo::hfovFromFocal35(50.0, 4000, 3000)) << "a wide lens sees more";
	EXPECT_DOUBLE_EQ(camera_from_photo::hfovFromFocal35(0.0, 4000, 3000), 0.0);
	EXPECT_DOUBLE_EQ(camera_from_photo::hfovFromFocal35(26.0, 0, 3000), 0.0);
}

TEST(CameraFromPhotoTest, TheBuilderCameraHasThePhotosLensTiltAndRollAndKeepsItsHeading) {
	Camera start;   // the starter camera looks along -Z
	const Camera cam = camera_from_photo::makeCamera(start, 70.0, 4000, 3000, 8.0, 5.0, 1.6);
	EXPECT_NEAR(cam.position.y, 1.6, 1e-9);
	EXPECT_NEAR(cam.position.x, start.position.x, 1e-9);
	EXPECT_NEAR(cam.position.z, start.position.z, 1e-9);
	const double fx = cam.target.x - cam.position.x, fy = cam.target.y - cam.position.y, fz = cam.target.z - cam.position.z;
	const double fl = std::sqrt(fx * fx + fy * fy + fz * fz);
	EXPECT_NEAR(std::asin(fy / fl) * 180.0 / kPi, 8.0, 1e-6) << "looks up by the tilt";
	EXPECT_NEAR(fx, 0.0, 1e-6) << "and keeps looking along -Z";
	EXPECT_LT(fz, 0.0);
	// the vertical field of view of a 70-degree horizontal one across a 4:3 picture
	EXPECT_NEAR(cam.fov, 2.0 * std::atan(std::tan(35.0 * kPi / 180.0) * 0.75) * 180.0 / kPi, 1e-9);
	const double ul = std::sqrt(cam.up.x * cam.up.x + cam.up.y * cam.up.y + cam.up.z * cam.up.z);
	EXPECT_NEAR(ul, 1.0, 1e-9);
	EXPECT_NEAR(cam.up.x * fx + cam.up.y * fy + cam.up.z * fz, 0.0, 1e-6) << "up is square to the view";
	// a camera with no heading at all (it looks straight down) is given one
	Camera down;
	down.target = {down.position.x, down.position.y - 5.0, down.position.z};
	const Camera made = camera_from_photo::makeCamera(down, 60.0, 100, 100, 0.0, 0.0, 1.5);
	EXPECT_LT(made.target.z, made.position.z);
}

TEST(CameraFromPhotoTest, ARenderThroughTheBuilderCameraHasTheHorizonWhereThePhotoHadIt) {
	const std::string launcher = findLauncher();
	if (launcher.empty()) GTEST_SKIP() << "ray_tracer.exe was not found next to the tests";
	// A huge dark ground under a plain bright sky, seen through the camera made from a photo of known tilt and roll (a square one, since the test's render is square): the horizon in the render
	// must run where the photo's did. This checks the signs of the tilt and the roll all the way through the Builder's document, the pbrt writer and the renderer.
	const Shot shots[] = {{0.0, 0.0}, {8.0, 0.0}, {-6.0, 0.0}, {0.0, 6.0}, {5.0, -5.0}, {-4.0, 7.0}};
	for (const Shot& s : shots) {
		const Camera cam = camera_from_photo::makeCamera(Camera(), 70.0, 160, 160, s.tilt, s.roll, 1.6);
		Document d = makeStarterScene();
		Object ground = d.objects[0];
		ground.size = {40000.0, 1.0, 40000.0};
		ground.material.checker = false;
		ground.material.color = {0.04, 0.04, 0.04};
		d.objects = {ground};
		d.camera = cam;
		std::vector<float> rgb;
		int w = 0, h = 0;
		ASSERT_TRUE(renderWithLauncher(launcher, toPbrt(d), "camera_horizon", 128, 8, 2, rgb, w, h));
		const auto lum = [&](int x, int y) { const float* p = &rgb[(static_cast<size_t>(y) * w + x) * 3]; return 0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2]; };
		double lo = 1e30, hi = 0.0;
		for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) { lo = std::min(lo, lum(x, y)); hi = std::max(hi, lum(x, y)); }
		ASSERT_GT(hi, 4.0 * lo) << "the sky is much brighter than the ground";
		const double mid = std::sqrt(lo * hi);
		const auto horizonRow = [&](int x) {   // the first row from the top that is ground, to a fraction of a row
			for (int y = 0; y < h - 1; ++y)
				if (lum(x, y) < mid) return y + (lum(x, y - (y > 0 ? 1 : 0)) - mid) / std::max(1e-9, lum(x, y - (y > 0 ? 1 : 0)) - lum(x, y));
			return static_cast<double>(h);
		};
		const int xa = w / 5, xb = 4 * w / 5;
		const double ra = horizonRow(xa), rb = horizonRow(xb), rc = horizonRow(w / 2);
		if (ra >= h - 1 || rb >= h - 1 || ra <= 1 || rb <= 1) continue;   // (a horizon off the picture at one side: not measurable)
		const double rollRendered = std::atan((rb - ra) / (xb - xa)) * 180.0 / kPi;
		const double vf = cam.fov * kPi / 180.0;
		const double focal = (h / 2.0) / std::tan(vf / 2.0);
		const double tiltRendered = std::atan2((rc - h / 2.0) * std::cos(rollRendered * kPi / 180.0), focal) * 180.0 / kPi;
		EXPECT_NEAR(rollRendered, s.roll, 1.5) << "tilt " << s.tilt << ", roll " << s.roll;
		EXPECT_NEAR(tiltRendered, s.tilt, 1.5) << "tilt " << s.tilt << ", roll " << s.roll;
	}
}

TEST(CameraFromPhotoTest, ARaggedHorizonOrLittleSkyIsSaidToBeUncertain) {
	// found on real photos: hills and dunes along the sky's lower edge gave a roll of 7 degrees with a clean-looking answer
	Shot s;
	s.tilt = 3.0;
	std::vector<unsigned char> px = photograph(s, 320, 240);
	std::mt19937 rng(11);
	// raise the ground into jagged hills: every column's ground starts a random 0..14 rows higher than the horizon (the sky's edge stays well above the true horizon)
	for (int x = 0; x < 320; ++x) {
		const int rise = static_cast<int>(rng() % 15);
		int row = 0;
		for (int y = 0; y < 240; ++y) {
			const unsigned char* c = &px[(static_cast<size_t>(y) * 320 + x) * 3];
			if (c[0] < 100 && c[2] < 80) { row = y; break; }   // the first ground pixel (the grass is not blue)
		}
		for (int y = std::max(0, row - rise); y < row; ++y) {
			unsigned char* c = &px[(static_cast<size_t>(y) * 320 + x) * 3];
			c[0] = 76; c[1] = 92; c[2] = 56;
		}
	}
	camera_from_photo::Estimate e = camera_from_photo::estimate(px.data(), 320, 240);
	ASSERT_TRUE(e.ok) << e.why;
	if (e.horizonFound) EXPECT_NE(e.why.find("straight line"), std::string::npos) << "hills along the horizon: " << e.why << " (spread " << e.lineSpread << ")";
	else EXPECT_FALSE(e.why.empty());
	// a thin strip of sky
	Shot strip;
	strip.tilt = -24.0;
	const std::vector<unsigned char> thin = photograph(strip, 320, 240);
	e = camera_from_photo::estimate(thin.data(), 320, 240);
	ASSERT_TRUE(e.ok);
	EXPECT_LT(e.skyFraction, 0.12);
	EXPECT_NE(e.why.find("small part"), std::string::npos) << e.why;
}
