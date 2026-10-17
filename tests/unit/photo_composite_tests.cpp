/**
 * @file photo_composite_tests.cpp
 * @brief "Render into the photo" (src/shared/photo_composite.h): a scene rendered through a camera matched to a photo is put into the photo with its shadows
 *
 * The compositing arithmetic is checked on pictures made by hand, where every pixel's right answer is known. The whole method is checked by rendering the three passes of a real scene through the
 * launcher (a sphere on a huge ground under a sun) and putting them into a photo.
 */

#include <gtest/gtest.h>
#include "scene_builder_test_helpers.h"

#include "../../src/shared/camera_from_photo.h"
#include "../../src/shared/photo_composite.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

using namespace scene_doc;

namespace {

using photo_composite::Role;

double encodeSrgb(double l) { return photo_composite::detail::encode(l); }
double decodeSrgb(double s) { return photo_composite::detail::decode(s); }

}  // namespace

TEST(PhotoCompositeTest, TheFarFieldIsThePhotoTheShadowIsTheRatioAndTheObjectIsPastedIn) {
	const int w = 64, h = 48;
	const size_t n = static_cast<size_t>(w) * h;
	std::vector<unsigned char> photo(n * 3);
	for (size_t i = 0; i < n; ++i)
		for (int c = 0; c < 3; ++c) photo[i * 3 + static_cast<size_t>(c)] = static_cast<unsigned char>(100 + (i % 64) + 20 * c);   // (a photo with some structure)
	std::vector<float> ground(n * 3, 0.30f), with(n * 3), mask(n, 0.0f);
	std::uint32_t seed = 12345;
	const auto noise = [&]() { seed = seed * 1664525u + 1013904223u; return (static_cast<double>(seed >> 8) / 16777216.0 - 0.5) * 0.04; };   // +-2%: the render's own noise
	const int shadowX0 = 10, shadowX1 = 25, shadowY0 = 30, shadowY1 = 40, discX = 45, discY = 20, discR = 8;
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x) {
			const size_t i = static_cast<size_t>(y) * w + x;
			double scale = 1.0 + noise();
			if (x >= shadowX0 && x < shadowX1 && y >= shadowY0 && y < shadowY1) scale *= 0.5;   // in the shadow: half the ground's light
			for (int c = 0; c < 3; ++c) with[i * 3 + static_cast<size_t>(c)] = static_cast<float>(0.30 * scale);
			const double d = std::sqrt(static_cast<double>((x - discX) * (x - discX) + (y - discY) * (y - discY)));
			if (d < discR - 1.0) {   // the object: reddish, over the ground
				mask[i] = 1.0f;
				with[i * 3 + 0] = 0.8f; with[i * 3 + 1] = 0.2f; with[i * 3 + 2] = 0.2f;
			} else if (d < discR) {   // its edge: half object, half ground (what an anti-aliased render holds)
				mask[i] = 0.5f;
				with[i * 3 + 0] = 0.5f * 0.8f + 0.5f * 0.30f; with[i * 3 + 1] = 0.5f * 0.2f + 0.5f * 0.30f; with[i * 3 + 2] = 0.5f * 0.2f + 0.5f * 0.30f;
			}
		}
	const std::vector<unsigned char> out = photo_composite::composite(photo.data(), with.data(), ground.data(), mask.data(), w, h);
	ASSERT_EQ(out.size(), n * 3);
	const auto at = [&](const std::vector<unsigned char>& px, int x, int y, int c) { return static_cast<int>(px[(static_cast<size_t>(y) * w + x) * 3 + static_cast<size_t>(c)]); };
	// far from everything, with the render's noise: the photo, untouched
	int changed = 0;
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x) {
			const bool closeBy = (x >= shadowX0 - 3 && x < shadowX1 + 3 && y >= shadowY0 - 3 && y < shadowY1 + 3) || std::hypot(x - discX, y - discY) < discR + 3;
			if (closeBy) continue;
			for (int c = 0; c < 3; ++c) changed += std::abs(at(out, x, y, c) - at(photo, x, y, c)) > 1 ? 1 : 0;
		}
	EXPECT_EQ(changed, 0) << "noise within the dead-band leaves the photo exactly as it was";
	// the middle of the shadow: the photo's light, halved (in linear light), less the dead-band's soft share
	for (int c = 0; c < 3; ++c) {
		const double expected = encodeSrgb(decodeSrgb(at(photo, 17, 35, c) / 255.0) * std::exp(std::log(0.5) + photo_composite::Options().deadBand)) * 255.0;
		EXPECT_NEAR(at(out, 17, 35, c), expected, 3.0) << "channel " << c;
		EXPECT_LT(at(out, 17, 35, c), at(photo, 17, 35, c) - 15) << "the shadow darkens the photo";
	}
	// the middle of the object: the render's colour as the renderer shows it (tone mapped), not the photo
	const double aces[3] = {photo_composite::detail::aces(0.8), photo_composite::detail::aces(0.2), photo_composite::detail::aces(0.2)};
	for (int c = 0; c < 3; ++c) EXPECT_NEAR(at(out, discX, discY, c), encodeSrgb(aces[c]) * 255.0, 2.0) << "channel " << c;
	// the object's edge (half covered): half object, half photo, with no halo of the render's ground (the object's own share is un-matted first)
	const int edgeX = discX + discR - 1;   // d = 7: the half-covered ring
	for (int c = 0; c < 3; ++c) {
		const double object = encodeSrgb(aces[c]), background = at(photo, edgeX, discY, c) / 255.0;
		EXPECT_NEAR(at(out, edgeX, discY, c), (0.5 * object + 0.5 * background) * 255.0, 3.0) << "channel " << c;
	}
	// exposure: one stop brighter doubles the object's light before it is tone mapped
	photo_composite::Options bright;
	bright.exposureStops = 1.0;
	const std::vector<unsigned char> lit = photo_composite::composite(photo.data(), with.data(), ground.data(), mask.data(), w, h, bright);
	EXPECT_NEAR(at(lit, discX, discY, 1), encodeSrgb(photo_composite::detail::aces(0.4)) * 255.0, 2.0);
	// no shadows at all
	photo_composite::Options none;
	none.shadowStrength = 0.0;
	const std::vector<unsigned char> flat = photo_composite::composite(photo.data(), with.data(), ground.data(), mask.data(), w, h, none);
	EXPECT_EQ(at(flat, 17, 35, 1), at(photo, 17, 35, 1)) << "shadow strength 0 keeps the photo's ground as it was";
}

TEST(PhotoCompositeTest, ABlackGroundNanAndBadInputsDoNotBreakIt) {
	const int w = 8, h = 8;
	const size_t n = 64;
	const std::vector<unsigned char> photo(n * 3, 120);
	std::vector<float> ground(n * 3, 0.0f), with(n * 3, 0.0f), mask(n, 0.0f);
	std::vector<unsigned char> out = photo_composite::composite(photo.data(), with.data(), ground.data(), mask.data(), w, h);
	ASSERT_EQ(out.size(), n * 3);
	for (const unsigned char v : out) EXPECT_EQ(v, 120) << "no light anywhere: no ratio, the photo as it is";
	with[10] = std::numeric_limits<float>::quiet_NaN();
	ground[13] = std::numeric_limits<float>::infinity();
	out = photo_composite::composite(photo.data(), with.data(), ground.data(), mask.data(), w, h);
	ASSERT_EQ(out.size(), n * 3);
	EXPECT_TRUE(photo_composite::composite(nullptr, with.data(), ground.data(), mask.data(), w, h).empty());
	EXPECT_TRUE(photo_composite::composite(photo.data(), with.data(), ground.data(), mask.data(), 0, h).empty());
}

TEST(PhotoCompositeTest, TheThreeScenesSplitTheObjectsByWhatTheyAreFor) {
	Document d = makeStarterScene();   // Floor, Glass ball, Gold ball, Red box, Ceiling light (a lamp), and a Sky
	d.lights.push_back(d.lights[0]);
	const std::vector<Role> roles = photo_composite::guessRoles(d);
	ASSERT_EQ(roles.size(), d.objects.size());
	EXPECT_EQ(roles[0], Role::Ground) << "the Floor";
	EXPECT_EQ(roles[1], Role::Inserted);
	EXPECT_EQ(roles[3], Role::Inserted);
	EXPECT_EQ(roles[4], Role::Light) << "the ceiling lamp is emissive";
	const photo_composite::Passes p = photo_composite::makePasses(d, roles);
	// ground: the floor and the lamp, the lights, no inserted objects
	ASSERT_EQ(p.ground.objects.size(), 2u);
	EXPECT_EQ(p.ground.objects[0].name, "Floor");
	EXPECT_EQ(p.ground.objects[1].name, "Ceiling light");
	EXPECT_EQ(p.ground.lights.size(), d.lights.size());
	// mask: the floor (black, it hides what is under it) and the three inserted objects (white, self-lit); no lamp, no lights
	ASSERT_EQ(p.mask.objects.size(), 4u);
	EXPECT_TRUE(p.mask.lights.empty());
	for (const Object& o : p.mask.objects) {
		if (o.name == "Floor") {
			EXPECT_FALSE(o.emissive);
			EXPECT_DOUBLE_EQ(o.material.color.r, 0.0);
		} else {
			EXPECT_TRUE(o.emissive) << o.name;
			EXPECT_DOUBLE_EQ(o.emissionStrength, 1.0);
			EXPECT_DOUBLE_EQ(o.material.color.r, 0.0) << "it only emits";
		}
	}
	EXPECT_FALSE(hasErrors(validate(p.ground)));
	EXPECT_FALSE(hasErrors(validate(p.mask)));
	// the scene itself is untouched
	EXPECT_EQ(d.objects.size(), 5u);
	EXPECT_TRUE(d.objects[1].material.kind == MaterialKind::Dielectric);
}

TEST(PhotoCompositeTest, ARenderedSphereOnAGroundIsPutIntoThePhotoWithItsShadow) {
	const std::string launcher = findLauncher();
	if (launcher.empty()) GTEST_SKIP() << "ray_tracer.exe was not found next to the tests";
	const int size = 96;
	// the photo: a plain sky over a plain ground, the horizon through the middle (the camera is level, 1.6 m up)
	std::vector<unsigned char> photo(static_cast<size_t>(size) * size * 3);
	for (int y = 0; y < size; ++y)
		for (int x = 0; x < size; ++x) {
			const unsigned char sky[3] = {150, 190, 235}, grass[3] = {96, 120, 70};
			for (int c = 0; c < 3; ++c) photo[(static_cast<size_t>(y) * size + x) * 3 + static_cast<size_t>(c)] = y < size / 2 ? sky[c] : grass[c];
		}
	// the scene: a huge ground, a sphere on it, a sun to the left behind the camera so the shadow falls to the right and away; seen through the camera matched to the photo
	Document d = makeStarterScene();
	Object ground = d.objects[0];
	ground.size = {4000.0, 1.0, 4000.0};
	ground.material.checker = false;
	ground.material.color = {0.5, 0.5, 0.5};
	Object ball = makeObject(ShapeKind::Sphere, "Ball");
	ball.radius = 0.9;
	ball.position = {0.0, 0.9, 0.0};
	ball.material.color = {0.8, 0.15, 0.1};
	d.objects = {ground, ball};
	d.lights.clear();
	Light sky;
	sky.name = "Sky";
	sky.kind = LightKind::Infinite;
	sky.color = {0.6, 0.75, 1.0};
	sky.intensity = 0.4;
	Light sun;
	sun.name = "Sun";
	sun.kind = LightKind::Distant;
	sun.position = {-6.0, 9.0, 6.0};
	sun.target = {0.0, 0.0, 0.0};
	sun.color = {1.0, 0.95, 0.85};
	sun.intensity = 3.0;
	d.lights = {sky, sun};
	d.camera = camera_from_photo::makeCamera(Camera(), 60.0, size, size, 0.0, 0.0, 1.6);
	d.camera.position.z = 7.0;
	d.camera.target.z = d.camera.position.z - 8.0;
	const std::vector<Role> roles = photo_composite::guessRoles(d);
	ASSERT_EQ(roles[0], Role::Ground);
	ASSERT_EQ(roles[1], Role::Inserted);
	const photo_composite::Passes passes = photo_composite::makePasses(d, roles);
	std::vector<float> full, groundOnly, maskRgb;
	int w = 0, h = 0;
	ASSERT_TRUE(renderWithLauncher(launcher, toPbrt(d), "composite_full", size, 96, 4, full, w, h));
	ASSERT_EQ(w, size);
	ASSERT_TRUE(renderWithLauncher(launcher, toPbrt(passes.ground), "composite_ground", size, 96, 4, groundOnly, w, h));
	ASSERT_TRUE(renderWithLauncher(launcher, toPbrt(passes.mask), "composite_mask", size, 16, 2, maskRgb, w, h));
	std::vector<float> mask(static_cast<size_t>(size) * size);
	for (size_t i = 0; i < mask.size(); ++i) mask[i] = std::min(1.0f, std::max(0.0f, (maskRgb[i * 3] + maskRgb[i * 3 + 1] + maskRgb[i * 3 + 2]) / 3.0f));
	// the passes behave: the mask is the ball (covering some pixels, and nothing of the sky or the far ground), the ground pass has no ball
	size_t covered = 0;
	for (const float m : mask) covered += m > 0.9f ? 1 : 0;
	ASSERT_GT(covered, 60u) << "the sphere covers a few dozen pixels of the picture";
	EXPECT_LT(mask[0], 0.01f) << "the sky is not the object";
	EXPECT_LT(mask[static_cast<size_t>(size - 1) * size], 0.01f) << "nor the near ground";
	const std::vector<unsigned char> out = photo_composite::composite(photo.data(), full.data(), groundOnly.data(), mask.data(), size, size);
	ASSERT_EQ(out.size(), photo.size());
	// the object's pixels are not the photo's; the shadow's are darker than it; the sky and the far ground are the photo, untouched
	double objectDiff = 0.0;
	size_t objects = 0, shadows = 0, untouched = 0, untouchedChanged = 0, skyChanged = 0, skyPixels = 0;
	double shadowDrop = 0.0;
	for (int y = 0; y < size; ++y)
		for (int x = 0; x < size; ++x) {
			const size_t i = static_cast<size_t>(y) * size + x;
			const int diff = std::abs(static_cast<int>(out[i * 3 + 1]) - static_cast<int>(photo[i * 3 + 1]));
			const double lw = 0.2126 * full[i * 3] + 0.7152 * full[i * 3 + 1] + 0.0722 * full[i * 3 + 2], lg = 0.2126 * groundOnly[i * 3] + 0.7152 * groundOnly[i * 3 + 1] + 0.0722 * groundOnly[i * 3 + 2];
			if (mask[i] > 0.95f) { objectDiff += diff; ++objects; }
			else if (mask[i] < 0.02f && y >= size / 2 && lw < 0.6 * lg) { shadowDrop += static_cast<int>(photo[i * 3 + 1]) - static_cast<int>(out[i * 3 + 1]); ++shadows; }
			else if (mask[i] < 0.02f && y < size / 2 - 4) { ++skyPixels; skyChanged += diff > 1 ? 1 : 0; }
			if (mask[i] < 0.02f && std::fabs(std::log((lw + 1e-4) / (lg + 1e-4))) < 0.02) { ++untouched; untouchedChanged += diff > 2 ? 1 : 0; }
		}
	ASSERT_GT(objects, 60u);
	EXPECT_GT(objectDiff / objects, 20.0) << "the sphere is not the photo's ground";
	ASSERT_GT(shadows, 10u) << "the sphere throws a shadow on the ground";
	EXPECT_GT(shadowDrop / shadows, 10.0) << "and it darkens the photo's ground";
	EXPECT_EQ(skyChanged, 0u) << "the sky of " << skyPixels << " pixels is the photo's";
	EXPECT_LE(static_cast<double>(untouchedChanged), 0.02 * untouched) << "where the render shows no shadow the photo is as it was (" << untouchedChanged << " of " << untouched << " pixels changed by more than 2)";
}
