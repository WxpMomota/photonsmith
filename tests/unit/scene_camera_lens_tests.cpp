// scene_camera_lens_tests.cpp -- the camera as a photographer sees it: focal length on a real sensor, the f-number and the focus point (src/shared/scene_camera_lens.h).
#include <gtest/gtest.h>

#include <cmath>
#include <set>
#include <string>

#include "../../src/shared/scene_document.h"

using namespace scene_doc;

namespace {
constexpr double kPi = 3.14159265358979323846;
double deg(double rad) { return rad * 180.0 / kPi; }
// The horizontal field of view that goes with a vertical one in a picture of this shape.
double horizontalFov(double verticalFovDeg, double aspect) { return deg(2.0 * std::atan(std::tan(verticalFovDeg * kPi / 360.0) * aspect)); }
}  // namespace

TEST(SceneCameraLensTest, TheFocalLengthsOfRealLensesGiveTheirFamiliarFields) {
	// On full frame (36 mm wide) the horizontal field of view is 2 atan(18 / f): 50 mm is 39.6 degrees, 24 mm 73.7, 135 mm 15.2.
	const double aspect = 1.5;   // 36 x 24
	EXPECT_NEAR(horizontalFov(fovFromFocalLength(50.0, aspect, 36.0), aspect), 39.598, 0.01);
	EXPECT_NEAR(horizontalFov(fovFromFocalLength(24.0, aspect, 36.0), aspect), 73.740, 0.01);
	EXPECT_NEAR(horizontalFov(fovFromFocalLength(135.0, aspect, 36.0), aspect), 15.189, 0.01);
	EXPECT_NEAR(fovFromFocalLength(50.0, aspect, 36.0), 27.0, 0.05) << "the vertical one: 24 mm tall film";
}

TEST(SceneCameraLensTest, ThePictureShapeCropsTheSensorAndAWiderPictureSeesLessUpAndDown) {
	// The same 50 mm lens: the picture is as wide as the film whatever its height, so a 16:9 picture is shorter than a 3:2 one.
	const double three2 = fovFromFocalLength(50.0, 1.5, 36.0), sixteen9 = fovFromFocalLength(50.0, 16.0 / 9.0, 36.0), square = fovFromFocalLength(50.0, 1.0, 36.0);
	EXPECT_LT(sixteen9, three2);
	EXPECT_GT(square, three2);
	EXPECT_NEAR(horizontalFov(sixteen9, 16.0 / 9.0), horizontalFov(three2, 1.5), 1e-9) << "the width of the view does not change: only the film's height does";
}

TEST(SceneCameraLensTest, FocalLengthAndFieldOfViewAreInversesAndSmallSensorsSeeNarrowerFields) {
	for (double f : {14.0, 35.0, 50.0, 200.0})
		for (double w : {36.0, 17.3, 7.6}) {
			const double fov = fovFromFocalLength(f, 1.6, w);
			EXPECT_NEAR(focalLengthMm(fov, 1.6, w), f, 1e-6 * f) << f << " mm on " << w << " mm";
		}
	EXPECT_LT(fovFromFocalLength(50.0, 1.5, 17.3), fovFromFocalLength(50.0, 1.5, 36.0)) << "the same lens on a smaller sensor is a narrower view (a crop)";
}

TEST(SceneCameraLensTest, ExtremeLensesAreKeptToTheCamerasLimits) {
	EXPECT_DOUBLE_EQ(fovFromFocalLength(0.5, 1.5, 36.0), kMaxFovDeg);
	EXPECT_DOUBLE_EQ(fovFromFocalLength(100000.0, 1.5, 36.0), kMinFovDeg);
	EXPECT_GT(focalLengthMm(170.0, 1.5, 36.0), 0.0);
}

TEST(SceneCameraLensTest, TheFNumberSetsTheLensRadiusAndBack) {
	EXPECT_NEAR(lensRadiusFromFNumber(50.0, 1.8), 0.0138889, 1e-6) << "50 mm at f/1.8: an opening 27.8 mm across, on a scene where a unit is a metre";
	EXPECT_NEAR(lensRadiusFromFNumber(50.0, 1.8, 100.0), 1.38889, 1e-5) << "a scene in centimetres has 100 units to the metre";
	EXPECT_NEAR(fNumberFromLensRadius(50.0, lensRadiusFromFNumber(50.0, 2.8)), 2.8, 1e-9);
	EXPECT_DOUBLE_EQ(lensRadiusFromFNumber(50.0, 0.0), 0.0) << "no f-number is a pinhole";
	EXPECT_DOUBLE_EQ(fNumberFromLensRadius(50.0, 0.0), 0.0);
	EXPECT_LT(lensRadiusFromFNumber(50.0, 8.0), lensRadiusFromFNumber(50.0, 2.0)) << "a bigger number is a smaller opening";
	EXPECT_GT(lensRadiusFromFNumber(200.0, 4.0), lensRadiusFromFNumber(50.0, 4.0)) << "a longer lens at the same number has a bigger opening";
}

TEST(SceneCameraLensTest, TheFocusDistanceIsMeasuredAlongTheViewAxisNotRoundTheCamera) {
	Camera c;
	c.position = {0, 0, 0};
	c.target = {0, 0, -5};   // looks along -Z
	EXPECT_NEAR(focusDistanceTo(c, {0, 0, -7}), 7.0, 1e-12);
	EXPECT_NEAR(focusDistanceTo(c, {3, 4, -7}), 7.0, 1e-12) << "off to the side at the same depth is as far in focus: the focal plane is flat";
	EXPECT_DOUBLE_EQ(focusDistanceTo(c, {0, 0, 2}), 0.01) << "a point behind the camera cannot be focused: the smallest distance";
	c.target = c.position;
	EXPECT_DOUBLE_EQ(focusDistanceTo(c, {1, 1, 1}), 0.01) << "a camera with no direction";
}

TEST(SceneCameraLensTest, WithFocalLengthChangesOnlyTheFieldOfView) {
	Camera c;
	c.lensRadius = 0.02;
	c.focusDistance = 6.0;
	c.sensorWidthMm = 23.5;
	RenderSettings r;
	r.width = 1200;
	r.height = 800;
	const Camera t = withFocalLength(c, 85.0, r);
	EXPECT_DOUBLE_EQ(t.lensRadius, 0.02);
	EXPECT_DOUBLE_EQ(t.focusDistance, 6.0);
	EXPECT_DOUBLE_EQ(t.sensorWidthMm, 23.5);
	EXPECT_NEAR(focalLengthMm(t.fov, aspectOf(r), t.sensorWidthMm), 85.0, 1e-6);
}

TEST(SceneCameraLensTest, ThePresetsAreSensibleAndDistinct) {
	std::set<std::string> ids;
	for (const SensorPreset& s : sensorPresets()) {
		EXPECT_TRUE(ids.insert(s.id).second) << s.id;
		EXPECT_GT(s.widthMm, s.heightMm * 0.99) << s.name << ": wider than tall";
		EXPECT_GT(s.heightMm, 1.0);
	}
	for (const LensPreset& l : lensPresets()) EXPECT_TRUE(ids.insert(l.id).second) << l.id;
	ASSERT_NE(findSensorByWidth(36.0), nullptr);
	EXPECT_STREQ(findSensorByWidth(36.0)->id, "full-frame");
	EXPECT_EQ(findSensorByWidth(31.0), nullptr);
	const std::vector<double>& n = standardFNumbers();
	for (std::size_t i = 1; i < n.size(); ++i) EXPECT_GT(n[i], n[i - 1]);
}

TEST(SceneCameraLensTest, ASensorOtherThanFullFrameSurvivesTheFileAndADefaultOneWritesNothingNew) {
	Document d = makeStarterScene();
	EXPECT_EQ(toJson(d).find("sensorWidthMm"), std::string::npos) << "older scenes write what they always did";
	d.camera.sensorWidthMm = 23.5;
	d.cameraViews.push_back(CameraView{"Wide", d.camera, 0.0});
	Document back;
	std::string error;
	ASSERT_TRUE(fromJson(toJson(d), back, error)) << error;
	EXPECT_DOUBLE_EQ(back.camera.sensorWidthMm, 23.5);
	EXPECT_DOUBLE_EQ(back.cameraViews[0].camera.sensorWidthMm, 23.5);
	EXPECT_EQ(toJson(back), toJson(d));
	// It is not in the .pbrt the renderer reads: only the field of view is.
	const std::string pbrt = toPbrt(d);
	EXPECT_EQ(pbrt.substr(pbrt.find("WorldBegin")).find("sensor"), std::string::npos);
}

TEST(SceneCameraLensTest, ANonsenseSensorWidthIsAnError) {
	Document d = makeStarterScene();
	d.camera.sensorWidthMm = 0.0;
	EXPECT_TRUE(hasErrors(validate(d)));
	d.camera.sensorWidthMm = 5000.0;
	EXPECT_TRUE(hasErrors(validate(d)));
	d.camera.sensorWidthMm = 36.0;
	EXPECT_FALSE(hasErrors(validate(d)));
}
