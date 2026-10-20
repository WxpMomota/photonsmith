/**
 * @file pbrt_flatten_lights_tests.cpp
 * @brief pbrt_flatten tests: LightSource: infinite, and the punctual kinds (point, spot, distant, goniometric, projection).
 * The helpers are in pbrt_flatten_test_helpers.h; this file is one part of what used to be a single pbrt_flatten_tests.cpp.
 */

#include "pbrt_flatten_test_helpers.h"

// ===========================================================================
// LightSource "infinite"
// ===========================================================================
// Real pbrt-v4 scenes (pavilion, ganesha, sportscar-sky) lean on this as
// their main illumination. Dropping it - the old behaviour - renders black
// or near-black instead of failing loudly, so these pin what flatten() must
// carry through: presence, the constant-colour and filename forms, and the
// CTM (an environment map with its rotation dropped still renders, just with
// the sun/horizon facing the wrong way - easy to miss without a check).

TEST(FlattenInfiniteLightTest, AbsentByDefault) {
	const FlatScene s = flattenSource(kQuadMesh);
	EXPECT_FALSE(s.infiniteLight.present);
}

TEST(FlattenInfiniteLightTest, ConstantColorFormPopulatesLAndScale) {
	const FlatScene s = flattenSource(
		"LightSource \"infinite\" \"rgb L\" [ 1 2 3 ] \"float scale\" [ 2 ]\n");
	ASSERT_TRUE(s.infiniteLight.present);
	EXPECT_DOUBLE_EQ(s.infiniteLight.L[0], 1.0);
	EXPECT_DOUBLE_EQ(s.infiniteLight.L[1], 2.0);
	EXPECT_DOUBLE_EQ(s.infiniteLight.L[2], 3.0);
	EXPECT_DOUBLE_EQ(s.infiniteLight.scale, 2.0);
	EXPECT_TRUE(s.infiniteLight.imageFile.empty());
}

TEST(FlattenInfiniteLightTest, FilenameFormRecordsThePathButDoesNotDecodeIt) {
	// flatten() is filesystem-free by design (see the file comment) - decoding
	// filename into imagePixels is pbrt_load::loadFile()'s job, done AFTER
	// flatten() returns. This only pins the half flatten() itself owns.
	const FlatScene s = flattenSource(
		"LightSource \"infinite\" \"string filename\" [ \"sky.exr\" ]\n");
	ASSERT_TRUE(s.infiniteLight.present);
	EXPECT_EQ(s.infiniteLight.imageFile, "sky.exr");
	EXPECT_EQ(s.infiniteLight.imageWidth, 0);
	EXPECT_EQ(s.infiniteLight.imageHeight, 0);
	EXPECT_TRUE(s.infiniteLight.imagePixels.empty());
}

TEST(FlattenInfiniteLightTest, RotationIsCarriedThroughInTheXform) {
	const FlatScene s = flattenSource(
		"Rotate 90 0 1 0\nLightSource \"infinite\" \"rgb L\" [ 1 1 1 ]\n");
	ASSERT_TRUE(s.infiniteLight.present);
	// A 90 degree rotation about Y is not the identity matrix - if the CTM
	// were dropped (the old bug's shape) this would come back as identity
	// regardless of the Rotate directive above it.
	const pbrt_scene::Matrix4 &m = s.infiniteLight.xform;
	const pbrt_scene::Matrix4 id = pbrt_scene::Matrix4::identity();
	bool differsFromIdentity = false;
	for (int i = 0; i < 16; ++i)
		if (std::abs(m.m[i] - id.m[i]) > 1e-9) differsFromIdentity = true;
	EXPECT_TRUE(differsFromIdentity);
}

TEST(FlattenInfiniteLightTest, OnlyTheLastInfiniteLightWins) {
	const FlatScene s = flattenSource(
		"LightSource \"infinite\" \"rgb L\" [ 1 1 1 ]\n"
		"LightSource \"infinite\" \"rgb L\" [ 9 8 7 ]\n");
	ASSERT_TRUE(s.infiniteLight.present);
	EXPECT_DOUBLE_EQ(s.infiniteLight.L[0], 9.0);
	EXPECT_DOUBLE_EQ(s.infiniteLight.L[1], 8.0);
	EXPECT_DOUBLE_EQ(s.infiniteLight.L[2], 7.0);
}

namespace {
bool warnedAboutInfinite(const FlatScene &s) {
	for (const auto &w : s.warnings)
		if (w.message.find("\"infinite\" lights") != std::string::npos) return true;
	return false;
}
}  // namespace

TEST(FlattenInfiniteLightTest, SeveralInfiniteLightsAreWarnedAbout) {
	const FlatScene s = flattenSource(
		"LightSource \"infinite\" \"rgb L\" [ 1 1 1 ]\n"
		"LightSource \"infinite\" \"rgb L\" [ 9 8 7 ]\n");
	bool warned = false;
	for (const auto &w : s.warnings) warned = warned || w.message.find("\"infinite\" lights") != std::string::npos;
	EXPECT_TRUE(warned);
}

TEST(FlattenInfiniteLightTest, OfSeveralPortalLightsTheLargestWindowIsTheMainOneAndTheRestAreKept) {
	// The pbrt-v4-scenes Watercolor has two portal lights, one per window, the later one the smaller: pbrt sums them. The larger is `infiniteLight`, the other an extra.
	const FlatScene s = flattenSource(
		"LightSource \"infinite\" \"rgb L\" [ 1 1 1 ] \"point3 portal\" [ 0 0 0   4 0 0   4 3 0   0 3 0 ]\n"
		"LightSource \"infinite\" \"rgb L\" [ 2 2 2 ] \"point3 portal\" [ 0 0 0   1 0 0   1 1 0   0 1 0 ]\n");
	ASSERT_TRUE(s.infiniteLight.present);
	ASSERT_TRUE(s.infiniteLight.hasPortal);
	EXPECT_DOUBLE_EQ(s.infiniteLight.L[0], 1.0);
	EXPECT_DOUBLE_EQ(s.infiniteLight.portal[3], 4.0);
	ASSERT_EQ(s.extraPortalLights.size(), 1u);
	EXPECT_DOUBLE_EQ(s.extraPortalLights[0].L[0], 2.0);
	EXPECT_FALSE(warnedAboutInfinite(s)) << "nothing was dropped";
}

TEST(FlattenInfiniteLightTest, APlainSkyNextToPortalsIsDroppedWithAWarning) {
	const FlatScene s = flattenSource(
		"LightSource \"infinite\" \"rgb L\" [ 5 5 5 ]\n"
		"LightSource \"infinite\" \"rgb L\" [ 1 1 1 ] \"point3 portal\" [ 0 0 0   1 0 0   1 1 0   0 1 0 ]\n");
	ASSERT_TRUE(s.infiniteLight.hasPortal);
	EXPECT_TRUE(s.extraPortalLights.empty());
	EXPECT_TRUE(warnedAboutInfinite(s));
}

TEST(FlattenInfiniteLightTest, NoPortalParamLeavesHasPortalFalse) {
	// The overwhelmingly common case - a plain (non-windowed) infinite
	// light - must not be misread as having a portal.
	const FlatScene s = flattenSource(
		"LightSource \"infinite\" \"rgb L\" [ 1 1 1 ]\n");
	ASSERT_TRUE(s.infiniteLight.present);
	EXPECT_FALSE(s.infiniteLight.hasPortal);
}

TEST(FlattenInfiniteLightTest, PortalParamPopulatesHasPortalAndCorners) {
	// pbrt-v4's windowed infinite light ("point3 portal[4]") - 4 corners,
	// ordered p0, p1(=p0+right), p2(=p0+right+up), p3(=p0+up) (matches
	// PortalImageInfiniteLightData's own ordering comment).
	const FlatScene s = flattenSource(
		"LightSource \"infinite\" \"string filename\" [ \"env.exr\" ] "
		"\"point3 portal\" [ -1 -1 0   1 -1 0   1 1 0   -1 1 0 ]\n");
	ASSERT_TRUE(s.infiniteLight.present);
	ASSERT_TRUE(s.infiniteLight.hasPortal);
	const double expected[12] = { -1,-1,0,  1,-1,0,  1,1,0,  -1,1,0 };
	for (int i = 0; i < 12; ++i)
		EXPECT_DOUBLE_EQ(s.infiniteLight.portal[i], expected[i]) << "component " << i;
}

TEST(FlattenInfiniteLightTest, PortalCornersAreWorldSpaceWhateverTheCTM) {
	// pbrt maps a portal's corners with the camera transform alone (`cameraTransform.RenderFromWorld(p)`, lights.cpp), never through the light's CTM: the CTM only turns the
	// environment picture. The pbrt-v4-scenes Kroken rotates before its portal light and writes the window's own world coordinates.
	const FlatScene s = flattenSource(
		"Translate 5 0 0\n"
		"Rotate 90 0 1 0\n"
		"LightSource \"infinite\" \"string filename\" [ \"env.exr\" ] "
		"\"point3 portal\" [ -1 -1 0   1 -1 0   1 1 0   -1 1 0 ]\n");
	ASSERT_TRUE(s.infiniteLight.present);
	ASSERT_TRUE(s.infiniteLight.hasPortal);
	const double expected[12] = { -1,-1,0,  1,-1,0,  1,1,0,  -1,1,0 };
	for (int i = 0; i < 12; ++i)
		EXPECT_DOUBLE_EQ(s.infiniteLight.portal[i], expected[i]) << "component " << i;
}

TEST(FlattenInfiniteLightTest, APortalLightsColourIsNotNormalisedToALuminanceOfOne) {
	// pbrt's 1 x 1 picture for "L" with a portal is the colour's XYZ -> sRGB as it is; for a blackbody that is the peak-normalised body (a 6500 K one has Y ~ 0.89), where the
	// light's ordinary L is scaled to a luminance of 1.
	const FlatScene portal = flattenSource(
		"LightSource \"infinite\" \"blackbody L\" [ 6500 ] \"point3 portal\" [ -1 -1 0   1 -1 0   1 1 0   -1 1 0 ]\n");
	const FlatScene plain = flattenSource("LightSource \"infinite\" \"blackbody L\" [ 6500 ]\n");
	const double yPortal = 0.2126 * portal.infiniteLight.L[0] + 0.7152 * portal.infiniteLight.L[1] + 0.0722 * portal.infiniteLight.L[2];
	const double yPlain = 0.2126 * plain.infiniteLight.L[0] + 0.7152 * plain.infiniteLight.L[1] + 0.0722 * plain.infiniteLight.L[2];
	EXPECT_NEAR(yPlain, 1.0, 0.01);
	EXPECT_NEAR(yPortal, 0.886, 0.02);
}

TEST(FlattenInfiniteLightTest, PortalParamWithFewerThanTwelveNumbersIsIgnored) {
	// A malformed/truncated portal[] (e.g. hand-edited scene file) should
	// leave the light as an ordinary (non-windowed) infinite light rather
	// than reading out-of-bounds or half-populating the array.
	const FlatScene s = flattenSource(
		"LightSource \"infinite\" \"rgb L\" [ 1 1 1 ] \"point3 portal\" [ -1 -1 0 ]\n");
	ASSERT_TRUE(s.infiniteLight.present);
	EXPECT_FALSE(s.infiniteLight.hasPortal);
}

TEST(FlattenInfiniteLightTest, OtherLightKindsAreStillDroppedWithAWarning) {
	// point/spot/distant/goniometric/projection all now carry through (see
	// the FlattenPunctualLightTest section below) - only a genuinely unknown
	// kind still hits this fallback.
	const FlatScene s = flattenSource(
		"LightSource \"bogus\" \"rgb I\" [ 1 1 1 ]\n");
	EXPECT_FALSE(s.infiniteLight.present);
	EXPECT_TRUE(s.punctualLights.empty());
	EXPECT_TRUE(warnedAbout(s, "bogus"));
}

// ===========================================================================
// LightSource "point"/"spot"/"distant"/"goniometric"/"projection"
// ===========================================================================
// pbrt-v4's five punctual (delta-distribution) light kinds. Rendering support
// for all five already exists on both backends, proven by this codebase's own
// C2-C6 showcase scenes (scenes_advanced.h) - these tests pin the
// parsing/bridging half: parameter defaults, the from/to -> position/
// direction derivation, and that the CTM is honoured the same way
// InfiniteLight::xform already is.

TEST(FlattenPunctualLightTest, PointLightReadsPositionIntensityAndScale) {
	const FlatScene s = flattenSource(
		"LightSource \"point\" \"point3 from\" [ 1 2 3 ] \"rgb I\" [ 0.5 0.6 0.7 ] "
		"\"float scale\" [ 10 ]\n");
	ASSERT_EQ(s.punctualLights.size(), 1u);
	const PunctualLight &pl = s.punctualLights[0];
	EXPECT_EQ(pl.kind, PunctualLightKind::Point);
	EXPECT_DOUBLE_EQ(pl.pos[0], 1.0);
	EXPECT_DOUBLE_EQ(pl.pos[1], 2.0);
	EXPECT_DOUBLE_EQ(pl.pos[2], 3.0);
	EXPECT_DOUBLE_EQ(pl.intensity[0], 0.5);
	EXPECT_DOUBLE_EQ(pl.intensity[1], 0.6);
	EXPECT_DOUBLE_EQ(pl.intensity[2], 0.7);
	EXPECT_DOUBLE_EQ(pl.scale, 10.0);
}

TEST(FlattenPunctualLightTest, PointLightDefaultsMatchPbrt) {
	const FlatScene s = flattenSource("LightSource \"point\"\n");
	ASSERT_EQ(s.punctualLights.size(), 1u);
	const PunctualLight &pl = s.punctualLights[0];
	EXPECT_DOUBLE_EQ(pl.pos[0], 0.0);
	EXPECT_DOUBLE_EQ(pl.pos[1], 0.0);
	EXPECT_DOUBLE_EQ(pl.pos[2], 0.0);
	EXPECT_DOUBLE_EQ(pl.intensity[0], 1.0);
	EXPECT_DOUBLE_EQ(pl.scale, 1.0);
}

TEST(FlattenPunctualLightTest, PointLightPositionHonoursTheCtm) {
	// Same guard InfiniteLight's own RotationIsCarriedThroughInTheXform test
	// makes: a light's placement dropped the CTM used to render in the wrong
	// spot with nothing on screen to explain why.
	const FlatScene s = flattenSource(
		"Translate 10 20 30\nLightSource \"point\"\n");
	ASSERT_EQ(s.punctualLights.size(), 1u);
	EXPECT_DOUBLE_EQ(s.punctualLights[0].pos[0], 10.0);
	EXPECT_DOUBLE_EQ(s.punctualLights[0].pos[1], 20.0);
	EXPECT_DOUBLE_EQ(s.punctualLights[0].pos[2], 30.0);
}

TEST(FlattenPunctualLightTest, MultiplePunctualLightsAreAllKept) {
	// Unlike "infinite", where only the last one wins (see
	// FlattenInfiniteLightTest::OnlyTheLastInfiniteLightWins), a scene with
	// three spotlights genuinely wants all three.
	const FlatScene s = flattenSource(
		"LightSource \"point\"\nLightSource \"point\"\nLightSource \"point\"\n");
	EXPECT_EQ(s.punctualLights.size(), 3u);
}

TEST(FlattenPunctualLightTest, SpotLightDerivesAxisFromFromAndTo) {
	const FlatScene s = flattenSource(
		"LightSource \"spot\" \"point3 from\" [ 0 0 0 ] \"point3 to\" [ 0 0 5 ] "
		"\"float coneangle\" [ 40 ] \"float conedeltaangle\" [ 10 ]\n");
	ASSERT_EQ(s.punctualLights.size(), 1u);
	const PunctualLight &pl = s.punctualLights[0];
	EXPECT_EQ(pl.kind, PunctualLightKind::Spot);
	EXPECT_DOUBLE_EQ(pl.pos[0], 0.0);
	EXPECT_DOUBLE_EQ(pl.pos[1], 0.0);
	EXPECT_DOUBLE_EQ(pl.pos[2], 0.0);
	EXPECT_NEAR(pl.dir[0], 0.0, 1e-9);
	EXPECT_NEAR(pl.dir[1], 0.0, 1e-9);
	EXPECT_NEAR(pl.dir[2], 1.0, 1e-9);
	EXPECT_DOUBLE_EQ(pl.coneAngleDeg, 40.0);
	EXPECT_DOUBLE_EQ(pl.falloffStartAngleDeg, 30.0);   // coneangle - conedeltaangle
}

TEST(FlattenPunctualLightTest, SpotLightDefaultConeMatchesPbrt) {
	const FlatScene s = flattenSource("LightSource \"spot\"\n");
	ASSERT_EQ(s.punctualLights.size(), 1u);
	const PunctualLight &pl = s.punctualLights[0];
	EXPECT_DOUBLE_EQ(pl.coneAngleDeg, 30.0);
	EXPECT_DOUBLE_EQ(pl.falloffStartAngleDeg, 25.0);   // 30 - 5, pbrt-v4's own default delta
	// Default "to" is (0,0,1): the axis points straight down +z.
	EXPECT_NEAR(pl.dir[2], 1.0, 1e-9);
}

TEST(FlattenPunctualLightTest, SpotLightFalloffStartCanGoNegative) {
	// conedeltaangle larger than coneangle is legal pbrt-v4, not malformed:
	// pbrt-v4's own SpotLight::Create passes coneangle - conedeltaangle
	// through unclamped, and cos() being even means a negative start angle
	// still produces a real, meaningful full-intensity core (cos(-80) ==
	// cos(80)) rather than collapsing to "no core at all" the way clamping
	// to 0 would.
	const FlatScene s = flattenSource(
		"LightSource \"spot\" \"float coneangle\" [ 10 ] \"float conedeltaangle\" [ 90 ]\n");
	ASSERT_EQ(s.punctualLights.size(), 1u);
	EXPECT_DOUBLE_EQ(s.punctualLights[0].falloffStartAngleDeg, -80.0);
}

TEST(FlattenPunctualLightTest, DistantLightPointsFromTowardFrom) {
	// dir must be `wi` (toward the light) - see PunctualLight::dir's own
	// comment. With "from" above "to", the light sits in +y, so wi should
	// point in +y too.
	const FlatScene s = flattenSource(
		"LightSource \"distant\" \"point3 from\" [ 0 10 0 ] \"point3 to\" [ 0 0 0 ] "
		"\"rgb L\" [ 2 2 2 ]\n");
	ASSERT_EQ(s.punctualLights.size(), 1u);
	const PunctualLight &pl = s.punctualLights[0];
	EXPECT_EQ(pl.kind, PunctualLightKind::Distant);
	EXPECT_NEAR(pl.dir[0], 0.0, 1e-9);
	EXPECT_NEAR(pl.dir[1], 1.0, 1e-9);
	EXPECT_NEAR(pl.dir[2], 0.0, 1e-9);
	EXPECT_DOUBLE_EQ(pl.intensity[0], 2.0);
}

TEST(FlattenPunctualLightTest, DistantLightDefaultDirectionMatchesPbrt) {
	// Default from=(0,0,0), to=(0,0,1): the light travels in +z, so wi
	// (toward the light) is -z.
	const FlatScene s = flattenSource("LightSource \"distant\"\n");
	ASSERT_EQ(s.punctualLights.size(), 1u);
	EXPECT_NEAR(s.punctualLights[0].dir[2], -1.0, 1e-9);
}

TEST(FlattenPunctualLightTest, GoniometricLightWithNoFilenameIsIsotropicAndUnwarned) {
	const FlatScene s = flattenSource(
		"LightSource \"goniometric\" \"rgb I\" [ 3 3 3 ] \"float scale\" [ 5 ]\n");
	ASSERT_EQ(s.punctualLights.size(), 1u);
	const PunctualLight &pl = s.punctualLights[0];
	EXPECT_EQ(pl.kind, PunctualLightKind::Goniometric);
	EXPECT_DOUBLE_EQ(pl.intensity[0], 3.0);
	EXPECT_DOUBLE_EQ(pl.scale, 5.0);
	EXPECT_FALSE(pl.hadImageFilename);
	EXPECT_FALSE(warnedAbout(s, "goniometric"));
}

TEST(FlattenPunctualLightTest, GoniometricLightWithFilenameIsStoredForLaterResolution) {
	// pbrt_flatten.h stays filesystem-free (see PunctualLight::filename's own
	// comment) - resolving/decoding the name happens later, in pbrt_load.h/
	// the CPU-GPU builders, so this stage should neither warn nor decode,
	// just carry the raw name through unchanged.
	const FlatScene s = flattenSource(
		"LightSource \"goniometric\" \"string filename\" [ \"profile.exr\" ]\n");
	ASSERT_EQ(s.punctualLights.size(), 1u);
	EXPECT_TRUE(s.punctualLights[0].hadImageFilename);
	EXPECT_EQ(s.punctualLights[0].filename, "profile.exr");
	EXPECT_FALSE(warnedAbout(s, "goniometric"));
}

TEST(FlattenPunctualLightTest, GoniometricLightRotationIsCarriedThroughInWorldToLight) {
	const FlatScene s = flattenSource(
		"Rotate 90 0 1 0\nLightSource \"goniometric\"\n");
	ASSERT_EQ(s.punctualLights.size(), 1u);
	const double *w2l = s.punctualLights[0].worldToLight;
	const double identity[9] = {1,0,0, 0,1,0, 0,0,1};
	bool differsFromIdentity = false;
	for (int i = 0; i < 9; ++i)
		if (std::abs(w2l[i] - identity[i]) > 1e-9) differsFromIdentity = true;
	EXPECT_TRUE(differsFromIdentity);
}

TEST(FlattenPunctualLightTest, ProjectionLightReadsFovAndScale) {
	const FlatScene s = flattenSource(
		"LightSource \"projection\" \"float fov\" [ 25 ] \"float scale\" [ 8 ] "
		"\"string filename\" [ \"slide.exr\" ]\n");
	ASSERT_EQ(s.punctualLights.size(), 1u);
	const PunctualLight &pl = s.punctualLights[0];
	EXPECT_EQ(pl.kind, PunctualLightKind::Projection);
	EXPECT_DOUBLE_EQ(pl.fovDeg, 25.0);
	EXPECT_DOUBLE_EQ(pl.scale, 8.0);
	EXPECT_TRUE(pl.hadImageFilename);
	EXPECT_EQ(pl.filename, "slide.exr");
	EXPECT_FALSE(warnedAbout(s, "projection"));
}

TEST(FlattenPunctualLightTest, ProjectionLightDefaultFovMatchesPbrt) {
	const FlatScene s = flattenSource("LightSource \"projection\"\n");
	ASSERT_EQ(s.punctualLights.size(), 1u);
	EXPECT_DOUBLE_EQ(s.punctualLights[0].fovDeg, 90.0);
	// pbrt-v4 requires "filename"; a scene without one still renders (as a
	// uniform beam) rather than being dropped outright, but is still
	// worth a warning since it is itself malformed.
	EXPECT_TRUE(warnedAbout(s, "projection"));
}
