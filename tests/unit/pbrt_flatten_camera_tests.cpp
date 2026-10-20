/**
 * @file pbrt_flatten_camera_tests.cpp
 * @brief pbrt_flatten tests: The camera (LookAt, fov, DOF), the pixel filter radius, camera motion blur.
 * The helpers are in pbrt_flatten_test_helpers.h; this file is one part of what used to be a single pbrt_flatten_tests.cpp.
 */

#include "pbrt_flatten_test_helpers.h"

// ===========================================================================
// Camera
// ===========================================================================
// pbrt hands over a world-to-camera matrix; our camera wants an eye, a target
// and a vertical fov. Getting the inversion or the fov convention wrong
// mis-frames every scene, and does so plausibly enough to look intentional.

// LookAt's target survives all the way to the flattened Camera now (see
// pbrt_scene.h's GraphicsState::lookAtDistance/Scene::cameraLookAtDistance
// and cameraFromWorldToCamera()'s own comments): a LookAt immediately
// followed by Camera/WorldBegin, with nothing else touching the CTM in
// between - the overwhelmingly common real-scene pattern, and exactly what
// this test writes - reconstructs the EXACT declared target, not a
// placeholder 1 unit along the view direction.
TEST(FlattenCameraTest, EyeAndTargetAreRecoveredFromWorldToCamera) {
	const FlatScene s = flattenSource(
		"LookAt 0 0 -5   0 0 0   0 1 0\n"
		"Camera \"perspective\" \"float fov\" [ 40 ]\n"
		"WorldBegin\n" + std::string(kQuadMesh));
	EXPECT_NEAR(s.camera.lookfrom[0], 0.0, 1e-9);
	EXPECT_NEAR(s.camera.lookfrom[1], 0.0, 1e-9);
	EXPECT_NEAR(s.camera.lookfrom[2], -5.0, 1e-9);
	// The scene declared the target as the origin - that's what should come
	// back, not lookfrom + a unit step toward it.
	EXPECT_NEAR(s.camera.lookat[0], 0.0, 1e-9);
	EXPECT_NEAR(s.camera.lookat[1], 0.0, 1e-9);
	EXPECT_NEAR(s.camera.lookat[2], 0.0, 1e-9);
	EXPECT_NEAR(s.camera.up[1], 1.0, 1e-9);
}

TEST(FlattenCameraTest, OffAxisEyePositionRoundTrips) {
	const FlatScene s = flattenSource(
		"LookAt 3 4 1.5   0.5 0.5 0   0 0 1\n"
		"Camera \"perspective\"\n"
		"WorldBegin\n" + std::string(kQuadMesh));
	EXPECT_NEAR(s.camera.lookfrom[0], 3.0, 1e-9);
	EXPECT_NEAR(s.camera.lookfrom[1], 4.0, 1e-9);
	EXPECT_NEAR(s.camera.lookfrom[2], 1.5, 1e-9);

	// The real declared target - (0.5, 0.5, 0) - round-trips exactly, not
	// just its direction.
	EXPECT_NEAR(s.camera.lookat[0], 0.5, 1e-9);
	EXPECT_NEAR(s.camera.lookat[1], 0.5, 1e-9);
	EXPECT_NEAR(s.camera.lookat[2], 0.0, 1e-9);
}

// A large eye-to-target distance (the kind a real external scene's LookAt
// declares - e.g. a statue framed from ~245 units away) must survive
// exactly, not just directionally - this is precisely the case that used to
// come back as "1 unit from lookfrom" and broke every orbit-family
// launcher/camera_path.h path pivoting around it (see cameraFromWorldToCamera()'s
// own comment).
TEST(FlattenCameraTest, LargeLookAtDistanceSurvivesExactly) {
	const FlatScene s = flattenSource(
		"LookAt 328 40.282 245   328 10 0   -0.00212272 0.998201 -0.0599264\n"
		"Camera \"perspective\"\n"
		"WorldBegin\n" + std::string(kQuadMesh));
	EXPECT_NEAR(s.camera.lookat[0], 328.0, 1e-6);
	EXPECT_NEAR(s.camera.lookat[1], 10.0, 1e-6);
	EXPECT_NEAR(s.camera.lookat[2], 0.0, 1e-6);
}

// A Rotate between LookAt and Camera is a directive this loader deliberately
// does not try to track a distance through (see Parser::applyCTM's own
// comment on why every non-LookAt CTM mutation invalidates rather than
// guessing) - it should fall back to the same placeholder "1 unit along the
// view direction" behavior as before this fix, rather than a stale distance
// from the LookAt that preceded it. Rotate (unlike Scale) keeps the
// composed matrix's rotation part orthonormal, so the placeholder's "1
// unit" is exact here rather than picking up an unrelated pre-existing
// quirk where a non-orthonormal CTM (e.g. from a Scale) makes the
// placeholder's implicit unit-forward-vector assumption not quite hold -
// that quirk predates this fix and is out of scope for it.
TEST(FlattenCameraTest, RotateAfterLookAtFallsBackToUnitPlaceholder) {
	const FlatScene s = flattenSource(
		"LookAt 0 0 -5   0 0 0   0 1 0\n"
		"Rotate 45 0 1 0\n"
		"Camera \"perspective\"\n"
		"WorldBegin\n" + std::string(kQuadMesh));
	const double dx = s.camera.lookat[0] - s.camera.lookfrom[0];
	const double dy = s.camera.lookat[1] - s.camera.lookfrom[1];
	const double dz = s.camera.lookat[2] - s.camera.lookfrom[2];
	EXPECT_NEAR(std::sqrt(dx * dx + dy * dy + dz * dz), 1.0, 1e-9);
}

// A camera authored via a raw matrix (Translate/Transform/ConcatTransform)
// rather than LookAt never has a target distance to recover in the first
// place - confirms that path is untouched by this fix and still gets the
// same "1 unit along the view direction" placeholder it always has.
TEST(FlattenCameraTest, TransformOnlyCameraKeepsUnitPlaceholder) {
	const FlatScene s = flattenSource(
		"Translate 0 0 -5\n"
		"Camera \"perspective\"\n"
		"WorldBegin\n" + std::string(kQuadMesh));
	const double dx = s.camera.lookat[0] - s.camera.lookfrom[0];
	const double dy = s.camera.lookat[1] - s.camera.lookfrom[1];
	const double dz = s.camera.lookat[2] - s.camera.lookfrom[2];
	EXPECT_NEAR(std::sqrt(dx * dx + dy * dy + dz * dz), 1.0, 1e-9);
}

// The animated-camera path (see AnimatedCameraCarriesTheEndKeyframeAndShutterWindow
// above) reconstructs the end keyframe through the same
// cameraFromWorldToCamera() - confirm its own LookAt distance survives
// independently of the start keyframe's.
TEST(FlattenCameraTest, AnimatedCameraPreservesLookAtDistanceForBothKeyframes) {
	const FlatScene s = flattenSource(
		"ActiveTransform \"StartTime\"\n"
		"LookAt 0 0 -5   0 0 0   0 1 0\n"
		"ActiveTransform \"EndTime\"\n"
		"LookAt 3 0 -10   3 0 0   0 1 0\n"
		"ActiveTransform \"All\"\n"
		"Camera \"perspective\"\n"
		"WorldBegin\n" + std::string(kQuadMesh));
	ASSERT_TRUE(s.camera.isAnimated);
	EXPECT_NEAR(s.camera.lookat[2], 0.0, 1e-9);    // start keyframe: real distance 5
	EXPECT_NEAR(s.camera.lookat1[2], 0.0, 1e-9);   // end keyframe: real distance 10
}

TEST(FlattenCameraTest, FovIsTakenAsVerticalOnALandscapeFrame) {
	const FlatScene s = flattenSource(
		"Film \"rgb\" \"integer xresolution\" [ 800 ] \"integer yresolution\" [ 600 ]\n"
		"Camera \"perspective\" \"float fov\" [ 45 ]\n"
		"WorldBegin\n" + std::string(kQuadMesh));
	EXPECT_NEAR(s.camera.vfov, 45.0, 1e-9);
}

TEST(FlattenCameraTest, FovIsConvertedOnAPortraitFrame) {
	// pbrt's fov covers the NARROWER axis, which on a portrait frame is the
	// width. Taking it as vertical unconditionally would leave vfov at 45 and
	// silently mis-frame the scene.
	const FlatScene s = flattenSource(
		"Film \"rgb\" \"integer xresolution\" [ 600 ] \"integer yresolution\" [ 800 ]\n"
		"Camera \"perspective\" \"float fov\" [ 45 ]\n"
		"WorldBegin\n" + std::string(kQuadMesh));
	EXPECT_GT(s.camera.vfov, 45.0) << "the vertical angle must widen, not stay put";
	// tan(v/2) = tan(h/2) / aspect, aspect = 600/800
	const double expect = 2.0 * std::atan(std::tan(45.0 * 0.5 * 3.14159265358979323846 / 180.0)
										  / 0.75) * 180.0 / 3.14159265358979323846;
	EXPECT_NEAR(s.camera.vfov, expect, 1e-9);
}

TEST(FlattenCameraTest, DepthOfFieldParametersAreCarriedOver) {
	const FlatScene s = flattenSource(
		"Camera \"perspective\" \"float lensradius\" [ 0.1 ] \"float focaldistance\" [ 7 ]\n"
		"WorldBegin\n" + std::string(kQuadMesh));
	EXPECT_NEAR(s.camera.aperture, 0.2, 1e-9) << "aperture is diameter, pbrt gives radius";
	EXPECT_NEAR(s.camera.focusDistance, 7.0, 1e-9);
}

TEST(FlattenCameraTest, CameraTypeDefaultsToPerspective) {
	const FlatScene s = flattenSource("WorldBegin\n" + std::string(kQuadMesh));
	EXPECT_EQ(s.camera.type, "perspective");
}

TEST(FlattenTest, PixelFilterDefaultsToGaussian) {
	// pbrt-v4's real default (see pbrt_flatten::PixelFilter's own comment) -
	// not box/triangle/mitchell.
	const FlatScene s = flattenSource("WorldBegin\n" + std::string(kQuadMesh));
	EXPECT_EQ(s.filter.kind, "gaussian");
	EXPECT_DOUBLE_EQ(s.filter.sigma, 0.5);
}

TEST(FlattenTest, PixelFilterParamsAreCarriedThrough) {
	const FlatScene s = flattenSource(
		std::string("PixelFilter \"mitchell\" \"float B\" [ 0.2 ] \"float C\" [ 0.4 ]\nWorldBegin\n")
		+ kQuadMesh);
	EXPECT_EQ(s.filter.kind, "mitchell");
	EXPECT_DOUBLE_EQ(s.filter.B, 0.2);
	EXPECT_DOUBLE_EQ(s.filter.C, 0.4);
}

TEST(FlattenTest, SincPixelFilterTauIsCarriedThrough) {
	const FlatScene s = flattenSource(
		std::string("PixelFilter \"sinc\" \"float tau\" [ 2.5 ]\nWorldBegin\n") + kQuadMesh);
	EXPECT_EQ(s.filter.kind, "sinc");
	EXPECT_DOUBLE_EQ(s.filter.tau, 2.5);
}

// ===========================================================================
// PixelFilter radius - pbrt-v4's real per-kind default (PixelFilter::radius's
// own comment, pbrt_flatten.h) - previously not parsed at all (see this
// feature's own commit).
// ===========================================================================

TEST(FlattenTest, PixelFilterRadiusDefaultsPerKind) {
	// box=0.5, gaussian=1.5, mitchell=2.0, sinc=4.0, triangle=2.0 - pbrt-v4's
	// own real defaults (confirmed against pbrt-v4/src/pbrt/filters.cpp),
	// NOT a single shared default.
	EXPECT_DOUBLE_EQ(flattenSource("PixelFilter \"box\"\nWorldBegin\n" + std::string(kQuadMesh)).filter.radius, 0.5);
	EXPECT_DOUBLE_EQ(flattenSource("WorldBegin\n" + std::string(kQuadMesh)).filter.radius, 1.5);
	EXPECT_DOUBLE_EQ(flattenSource("PixelFilter \"mitchell\"\nWorldBegin\n" + std::string(kQuadMesh)).filter.radius, 2.0);
	EXPECT_DOUBLE_EQ(flattenSource("PixelFilter \"sinc\"\nWorldBegin\n" + std::string(kQuadMesh)).filter.radius, 4.0);
	EXPECT_DOUBLE_EQ(flattenSource("PixelFilter \"triangle\"\nWorldBegin\n" + std::string(kQuadMesh)).filter.radius, 2.0);
}

TEST(FlattenTest, PixelFilterExplicitXradiusOverridesTheKindDefault) {
	const FlatScene s = flattenSource(
		std::string("PixelFilter \"mitchell\" \"float xradius\" [ 3.0 ]\nWorldBegin\n") + kQuadMesh);
	EXPECT_DOUBLE_EQ(s.filter.radius, 3.0);
}

TEST(FlattenTest, PixelFilterExplicitYradiusOverridesTheKindDefaultWhenXradiusIsAbsent) {
	const FlatScene s = flattenSource(
		std::string("PixelFilter \"mitchell\" \"float yradius\" [ 3.5 ]\nWorldBegin\n") + kQuadMesh);
	EXPECT_DOUBLE_EQ(s.filter.radius, 3.5);
}

TEST(FlattenCameraTest, ScreenWindowOverrideIsCarriedThrough) {
	// Orthographic has no fov to derive a scale from any other way - a scene
	// authored larger than ~1 world unit across needs this to see anything.
	const FlatScene s = flattenSource(
		"Camera \"orthographic\" \"float screenwindow\" [ -8 8 -8 8 ]\n"
		"WorldBegin\n" + std::string(kQuadMesh));
	ASSERT_TRUE(s.camera.hasScreenWindow);
	EXPECT_DOUBLE_EQ(s.camera.screenWindow[0], -8.0);
	EXPECT_DOUBLE_EQ(s.camera.screenWindow[1], 8.0);
	EXPECT_DOUBLE_EQ(s.camera.screenWindow[3], 8.0);
}

TEST(FlattenCameraTest, MissingScreenWindowLeavesTheDefaultInPlace) {
	const FlatScene s = flattenSource(
		"Camera \"orthographic\"\nWorldBegin\n" + std::string(kQuadMesh));
	EXPECT_FALSE(s.camera.hasScreenWindow);
}

TEST(FlattenCameraTest, ScreenWindowWithTooFewNumbersIsIgnoredAndWarned) {
	const FlatScene s = flattenSource(
		"Camera \"orthographic\" \"float screenwindow\" [ -8 8 ]\n"
		"WorldBegin\n" + std::string(kQuadMesh));
	EXPECT_FALSE(s.camera.hasScreenWindow);
	EXPECT_TRUE(warnedAbout(s, "screenwindow"));
}

TEST(FlattenCameraTest, OrthographicCameraTypeIsCarriedThrough) {
	// Before this, the type argument to Camera was parsed and then never
	// looked at again - every loaded scene rendered as perspective regardless.
	const FlatScene s = flattenSource(
		"Camera \"orthographic\"\nWorldBegin\n" + std::string(kQuadMesh));
	EXPECT_EQ(s.camera.type, "orthographic");
}

TEST(FlattenCameraTest, SphericalCameraReadsItsMappingParameter) {
	const FlatScene s = flattenSource(
		"Camera \"spherical\" \"string mapping\" [ \"equalarea\" ]\n"
		"WorldBegin\n" + std::string(kQuadMesh));
	EXPECT_EQ(s.camera.type, "spherical");
	EXPECT_EQ(s.camera.sphericalMapping, "equalarea");
}

TEST(FlattenCameraTest, SphericalCameraDefaultsToEqualArea) {
	// pbrt-v4's own default (SphericalCamera::Create: `mapping` is "equalarea"); "equirectangular" has to be asked for.
	const FlatScene s = flattenSource(
		"Camera \"spherical\"\nWorldBegin\n" + std::string(kQuadMesh));
	EXPECT_EQ(s.camera.sphericalMapping, "equalarea");
	const FlatScene t = flattenSource(
		"Camera \"spherical\" \"string mapping\" [ \"equirectangular\" ]\nWorldBegin\n" + std::string(kQuadMesh));
	EXPECT_EQ(t.camera.sphericalMapping, "equirectangular");
}

TEST(FlattenCameraTest, RealisticCameraReadsItsOwnParameters) {
	const FlatScene s = flattenSource(
		"Camera \"realistic\" \"string lensfile\" [ \"dgauss.dat\" ] "
		"\"float aperturediameter\" [ 4.5 ] \"float filmdiag\" [ 50 ]\n"
		"WorldBegin\n" + std::string(kQuadMesh));
	EXPECT_EQ(s.camera.type, "realistic");
	EXPECT_EQ(s.camera.lensFile, "dgauss.dat");
	EXPECT_NEAR(s.camera.apertureDiameterMM, 4.5, 1e-9);
	EXPECT_NEAR(s.camera.filmDiagonalMM, 50.0, 1e-9);
}

TEST(FlattenCameraTest, RealisticCameraFocusDistanceSpellingIsAlsoRead) {
	// pbrt-v4 spells this "focaldistance" for perspective/orthographic but
	// "focusdistance" for realistic - reading only the first silently keeps
	// every realistic-camera scene at the 1e6 no-DOF sentinel.
	const FlatScene s = flattenSource(
		"Camera \"realistic\" \"string lensfile\" [ \"dgauss.dat\" ] "
		"\"float focusdistance\" [ 12 ]\n"
		"WorldBegin\n" + std::string(kQuadMesh));
	EXPECT_NEAR(s.camera.focusDistance, 12.0, 1e-9);
}

TEST(FlattenCameraTest, RealisticCameraWithNoLensfileFallsBackToPerspective) {
	const FlatScene s = flattenSource(
		"Camera \"realistic\"\nWorldBegin\n" + std::string(kQuadMesh));
	EXPECT_EQ(s.camera.type, "perspective");
	EXPECT_TRUE(warnedAbout(s, "lensfile"));
}

// ===========================================================================
// Camera motion blur (ActiveTransform / TransformTimes / shutteropen /
// shutterclose)
// ===========================================================================

TEST(FlattenTest, RegularizeDefaultsToFalse) {
	const FlatScene s = flattenSource(
		"Camera \"perspective\"\nWorldBegin\n" + std::string(kQuadMesh));
	EXPECT_FALSE(s.regularize);
}

TEST(FlattenTest, RegularizeIsCarriedThroughFromTheIntegratorDirective) {
	const FlatScene s = flattenSource(
		"Integrator \"volpath\" \"bool regularize\" [ true ]\n"
		"Camera \"perspective\"\nWorldBegin\n" + std::string(kQuadMesh));
	EXPECT_TRUE(s.regularize);
}

TEST(FlattenTest, MaxComponentValueDefaultsToEffectivelyUnbounded) {
	const FlatScene s = flattenSource(
		"Camera \"perspective\"\nWorldBegin\n" + std::string(kQuadMesh));
	EXPECT_DOUBLE_EQ(s.maxComponentValue, 1e9);
}

TEST(FlattenTest, MaxComponentValueIsCarriedThroughFromTheFilmDirective) {
	const FlatScene s = flattenSource(
		"Film \"rgb\" \"float maxcomponentvalue\" [ 25 ]\n"
		"Camera \"perspective\"\nWorldBegin\n" + std::string(kQuadMesh));
	EXPECT_DOUBLE_EQ(s.maxComponentValue, 25.0);
}

// Film "iso" and the camera's shutter: pbrt's sensor multiplies every radiance by exposure time * iso / 100. flatten() applies it to the scene's emitters.
TEST(FlattenTest, FilmImagingRatioIsOneWithoutIsoOrShutter) {
	const FlatScene s = flattenSource(
		"Camera \"perspective\"\nWorldBegin\n"
		"LightSource \"point\" \"rgb I\" [ 2 2 2 ]\n" + std::string(kQuadMesh));
	EXPECT_DOUBLE_EQ(s.filmImagingRatio, 1.0);
	ASSERT_EQ(s.punctualLights.size(), 1u);
	EXPECT_DOUBLE_EQ(s.punctualLights[0].scale, 1.0);
}

TEST(FlattenTest, FilmIsoScalesEveryEmitter) {
	const FlatScene s = flattenSource(
		"Film \"rgb\" \"float iso\" [ 400 ] \"float maxcomponentvalue\" [ 40 ]\n"
		"Camera \"perspective\"\nWorldBegin\n"
		"LightSource \"point\" \"rgb I\" [ 2 2 2 ] \"float scale\" [ 3 ]\n"
		"LightSource \"infinite\" \"rgb L\" [ 1 1 1 ]\n"
		"AttributeBegin\nAreaLightSource \"diffuse\" \"rgb L\" [ 1 1 1 ] \"float scale\" [ 5 ]\n" + std::string(kQuadMesh) + "AttributeEnd\n");
	EXPECT_DOUBLE_EQ(s.filmImagingRatio, 4.0);
	ASSERT_EQ(s.punctualLights.size(), 1u);
	EXPECT_DOUBLE_EQ(s.punctualLights[0].scale, 12.0);
	EXPECT_DOUBLE_EQ(s.infiniteLight.scale, 4.0);
	ASSERT_EQ(s.areaLights.size(), 1u);
	EXPECT_DOUBLE_EQ(s.areaLights[0].scale, 20.0);
	EXPECT_DOUBLE_EQ(s.maxComponentValue, 10.0);   // the film clamps after the sensor scale
}

TEST(FlattenTest, ShutterTimeScalesTheFilmLikeIsoDoes) {
	const FlatScene s = flattenSource(
		"Film \"rgb\" \"float iso\" [ 50 ]\n"
		"Camera \"perspective\" \"float shutteropen\" [ 0.25 ] \"float shutterclose\" [ 2.25 ]\n"
		"WorldBegin\n"
		"LightSource \"point\" \"rgb I\" [ 1 1 1 ]\n" + std::string(kQuadMesh));
	EXPECT_DOUBLE_EQ(s.filmImagingRatio, 1.0);   // 2 s at iso 50
	ASSERT_EQ(s.punctualLights.size(), 1u);
	EXPECT_DOUBLE_EQ(s.punctualLights[0].scale, 1.0);
}

TEST(FlattenTest, AMeasuredSensorIsWarnedAboutNotSilentlyIgnored) {
	const FlatScene s = flattenSource(
		"Film \"rgb\" \"string sensor\" [ \"canon_eos_100d\" ]\n"
		"Camera \"perspective\"\nWorldBegin\n" + std::string(kQuadMesh));
	bool warned = false;
	for (const auto &w : s.warnings) warned = warned || w.message.find("sensor") != std::string::npos;
	EXPECT_TRUE(warned);
}

TEST(FlattenTest, CropWindowDefaultsToFullFrame) {
	const FlatScene s = flattenSource(
		"Camera \"perspective\"\nWorldBegin\n" + std::string(kQuadMesh));
	EXPECT_DOUBLE_EQ(s.cropX0, 0.0);
	EXPECT_DOUBLE_EQ(s.cropX1, 1.0);
	EXPECT_DOUBLE_EQ(s.cropY0, 0.0);
	EXPECT_DOUBLE_EQ(s.cropY1, 1.0);
}

TEST(FlattenTest, FilmCropWindowIsCarriedThroughAsAFraction) {
	const FlatScene s = flattenSource(
		"Film \"rgb\" \"float cropwindow\" [ 0.25 0.75 0.1 0.9 ]\n"
		"Camera \"perspective\"\nWorldBegin\n" + std::string(kQuadMesh));
	EXPECT_DOUBLE_EQ(s.cropX0, 0.25);
	EXPECT_DOUBLE_EQ(s.cropX1, 0.75);
	EXPECT_DOUBLE_EQ(s.cropY0, 0.1);
	EXPECT_DOUBLE_EQ(s.cropY1, 0.9);
}

TEST(FlattenTest, FilmPixelBoundsIsConvertedToAFractionOfTheScenesOwnResolution) {
	const FlatScene s = flattenSource(
		"Film \"rgb\" \"integer xresolution\" [ 800 ] \"integer yresolution\" [ 400 ] "
		"\"integer pixelbounds\" [ 200 600 100 300 ]\n"
		"Camera \"perspective\"\nWorldBegin\n" + std::string(kQuadMesh));
	EXPECT_DOUBLE_EQ(s.cropX0, 200.0 / 800.0);
	EXPECT_DOUBLE_EQ(s.cropX1, 600.0 / 800.0);
	EXPECT_DOUBLE_EQ(s.cropY0, 100.0 / 400.0);
	EXPECT_DOUBLE_EQ(s.cropY1, 300.0 / 400.0);
}

TEST(FlattenTest, FilmCropWindowAndPixelBoundsBothIntersect) {
	// pixelbounds (in [0,800]) restricts x to [0.25,0.75]; cropwindow
	// independently restricts y to [0.2,0.8] - both apply together.
	const FlatScene s = flattenSource(
		"Film \"rgb\" \"integer xresolution\" [ 800 ] \"integer yresolution\" [ 400 ] "
		"\"float cropwindow\" [ 0.0 1.0 0.2 0.8 ] "
		"\"integer pixelbounds\" [ 200 600 0 400 ]\n"
		"Camera \"perspective\"\nWorldBegin\n" + std::string(kQuadMesh));
	EXPECT_DOUBLE_EQ(s.cropX0, 0.25);
	EXPECT_DOUBLE_EQ(s.cropX1, 0.75);
	EXPECT_DOUBLE_EQ(s.cropY0, 0.2);
	EXPECT_DOUBLE_EQ(s.cropY1, 0.8);
}

TEST(FlattenTest, FilmCropWindowEmptyIntersectionFallsBackToFullFrameWithWarning) {
	const FlatScene s = flattenSource(
		"Film \"rgb\" \"float cropwindow\" [ 0.6 0.6 0.0 1.0 ]\n"
		"Camera \"perspective\"\nWorldBegin\n" + std::string(kQuadMesh));
	EXPECT_DOUBLE_EQ(s.cropX0, 0.0);
	EXPECT_DOUBLE_EQ(s.cropX1, 1.0);
	EXPECT_DOUBLE_EQ(s.cropY0, 0.0);
	EXPECT_DOUBLE_EQ(s.cropY1, 1.0);
	bool sawWarning = false;
	for (const auto &w : s.warnings)
		if (w.message.find("cropwindow") != std::string::npos) sawWarning = true;
	EXPECT_TRUE(sawWarning);
}

TEST(FlattenCameraTest, StaticSceneCameraIsNotAnimated) {
	const FlatScene s = flattenSource(
		"LookAt 0 0 -5   0 0 0   0 1 0\n"
		"Camera \"perspective\"\n"
		"WorldBegin\n" + std::string(kQuadMesh));
	EXPECT_FALSE(s.camera.isAnimated);
	EXPECT_DOUBLE_EQ(s.camera.shutterOpen, 0.0);
	EXPECT_DOUBLE_EQ(s.camera.shutterClose, 1.0);
}

TEST(FlattenCameraTest, AnimatedCameraCarriesTheEndKeyframeAndShutterWindow) {
	const FlatScene s = flattenSource(
		"ActiveTransform \"StartTime\"\n"
		"LookAt 0 0 -5   0 0 0   0 1 0\n"
		"ActiveTransform \"EndTime\"\n"
		"LookAt 3 0 -5   0 0 0   0 1 0\n"
		"ActiveTransform \"All\"\n"
		"Camera \"perspective\" \"float shutteropen\" [ 0.1 ] \"float shutterclose\" [ 0.9 ]\n"
		"WorldBegin\n" + std::string(kQuadMesh));
	EXPECT_TRUE(s.camera.isAnimated);
	EXPECT_NEAR(s.camera.lookfrom[0], 0.0, 1e-9);    // start keyframe, unchanged
	EXPECT_NEAR(s.camera.lookfrom1[0], 3.0, 1e-9);   // end keyframe
	EXPECT_DOUBLE_EQ(s.camera.shutterOpen, 0.1);
	EXPECT_DOUBLE_EQ(s.camera.shutterClose, 0.9);
}

TEST(FlattenCameraTest, TransformTimesDistinctFromShutterWindowWarns) {
	// This codebase's own camera implementation uses shutteropen/
	// shutterclose for BOTH the keyframe times and the shutter sampling
	// window (Camera::shutterOpen's own comment) - a scene relying on
	// TransformTimes differing from them must find out, not silently get
	// the wrong timing.
	const FlatScene s = flattenSource(
		"TransformTimes 0.25 0.75\n"
		"ActiveTransform \"StartTime\"\n"
		"LookAt 0 0 -5   0 0 0   0 1 0\n"
		"ActiveTransform \"EndTime\"\n"
		"LookAt 3 0 -5   0 0 0   0 1 0\n"
		"ActiveTransform \"All\"\n"
		"Camera \"perspective\"\n"
		"WorldBegin\n" + std::string(kQuadMesh));
	EXPECT_TRUE(s.camera.isAnimated);
	EXPECT_TRUE(warnedAbout(s, "TransformTimes"));
}

TEST(FlattenCameraTest, TransformTimesMatchingShutterWindowDoesNotWarn) {
	const FlatScene s = flattenSource(
		"TransformTimes 0 1\n"
		"ActiveTransform \"StartTime\"\n"
		"LookAt 0 0 -5   0 0 0   0 1 0\n"
		"ActiveTransform \"EndTime\"\n"
		"LookAt 3 0 -5   0 0 0   0 1 0\n"
		"ActiveTransform \"All\"\n"
		"Camera \"perspective\"\n"
		"WorldBegin\n" + std::string(kQuadMesh));
	EXPECT_TRUE(s.camera.isAnimated);
	EXPECT_FALSE(warnedAbout(s, "TransformTimes"));
}
