// pbrt_handedness_tests.cpp -- pbrt is left-handed, and so is the picture this project draws of a pbrt file. For the same eye, target and up, pbrt's perspective camera puts what lies on
// the world's +x side on the LEFT of the picture (`right = cross(up, forward)`); the book-style camera this renderer started from (`u = cross(vup, w)`) puts it on the right. The
// flattener reads the handedness off the file's world-to-camera matrix (a LookAt is a proper rotation; a `Scale -1 1 1` before it, the exporter idiom for a right-handed tool, makes
// it a reflection), and the renderers mirror their horizontal axis accordingly. Real pbrt-v4 renders the same files with the sphere at the same place: see docs/PBRT_SUPPORT.md.
#include <gtest/gtest.h>

#include "agreement_test_helpers.h"
#include "../../src/shared/pbrt_flatten.h"
#include "../../src/shared/pbrt_scene.h"

#include <string>
#include <vector>

namespace {

pbrt_flatten::FlatScene flattenText(const std::string& text) {
	const pbrt_scene::ParseResult r = pbrt_scene::parse(text);
	EXPECT_TRUE(r.ok) << r.error;
	return pbrt_flatten::flatten(r.scene, {});
}

// A red sphere at x = +1.2 seen from (0, 0, 5), under a white sky: `before` is whatever goes in front of the LookAt, `header` extra lines at the top of the file.
std::string sphereScene(const std::string& before, const std::string& header = std::string()) {
	return header + before +
	       "LookAt 0 0 5  0 0 0  0 1 0\n"
	       "Camera \"perspective\" \"float fov\" [ 40 ]\n"
	       "Film \"rgb\" \"integer xresolution\" [ 32 ] \"integer yresolution\" [ 32 ]\n"
	       "Sampler \"independent\" \"integer pixelsamples\" [ 16 ]\n"
	       "Integrator \"path\" \"integer maxdepth\" [ 2 ]\n"
	       "WorldBegin\n"
	       "LightSource \"infinite\" \"rgb L\" [ 1 1 1 ]\n"
	       "AttributeBegin\n"
	       "  Translate 1.2 0 0\n"
	       "  Material \"diffuse\" \"rgb reflectance\" [ 0.9 0.1 0.1 ]\n"
	       "  Shape \"sphere\" \"float radius\" [ 0.8 ]\n"
	       "AttributeEnd\n";
}

// The mean column (0 = left edge, 1 = right edge) of the pixels that are clearly red; -1 when none are.
double redColumn(const std::vector<float>& rgb, int w, int h) {
	double sum = 0;
	int n = 0;
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x) {
			const float* p = &rgb[3 * (static_cast<size_t>(y) * w + x)];
			if (p[0] > 2.0f * p[1] && p[0] > 0.2f) { sum += (x + 0.5) / w; ++n; }
		}
	return n ? sum / n : -1.0;
}

}  // namespace

TEST(PbrtHandednessTest, ALookAtIsLeftHandedAndAMirrorBeforeItTurnsThePictureBack) {
	EXPECT_TRUE(flattenText(sphereScene("")).camera.leftHanded) << "a plain LookAt is pbrt's own, left-handed camera";
	EXPECT_FALSE(flattenText(sphereScene("Scale -1 1 1\n")).camera.leftHanded) << "the exporter's mirror makes the picture the right-handed one";
	EXPECT_FALSE(flattenText(sphereScene("Scale 1 -1 1\n")).camera.leftHanded) << "any reflection of the camera's axes flips it";
	EXPECT_TRUE(flattenText(sphereScene("Scale -1 -1 1\n")).camera.leftHanded) << "two reflections are a rotation again";
}

TEST(PbrtHandednessTest, AFileDrawsWorldPlusXOnTheLeftLikePbrtAndTheExportersMirrorPutsItBack) {
	if (findRayTracerLauncher().empty()) GTEST_SKIP() << "ray_tracer.exe was not found next to the tests";
	std::vector<float> plain, mirrored;
	int w = 0, h = 0, w2 = 0, h2 = 0;
	ASSERT_TRUE(renderPbrtTextCpu(sphereScene(""), "handed_plain", 32, plain, w, h));
	ASSERT_TRUE(renderPbrtTextCpu(sphereScene("Scale -1 1 1\n"), "handed_mirrored", 32, mirrored, w2, h2));
	const double a = redColumn(plain, w, h), b = redColumn(mirrored, w2, h2);
	EXPECT_GT(a, 0.0);
	EXPECT_LT(a, 0.35) << "pbrt-v4 puts the sphere at x = +1.2 on the left of the picture (pixel 11 of 64)";
	EXPECT_GT(b, 0.65) << "with `Scale -1 1 1` before LookAt it is on the right (pixel 52 of 64)";
	EXPECT_NEAR(a + b, 1.0, 0.04) << "the two pictures are mirror images";
}

TEST(PbrtHandednessTest, ASceneBuilderFileWrittenBeforeItWroteTheMirrorKeepsItsOldPicture) {
	// Such a file is right-handed by origin (the Builder's views draw +x on the right): the line the Builder found it by is `@rt-builder-doc`.
	if (findRayTracerLauncher().empty()) GTEST_SKIP() << "ray_tracer.exe was not found next to the tests";
	std::vector<float> legacy;
	int w = 0, h = 0;
	ASSERT_TRUE(renderPbrtTextCpu(sphereScene("", "# Written by the ray_tracer Scene Builder.\n# @rt-builder-doc {}\n"), "handed_legacy", 32, legacy, w, h));
	EXPECT_GT(redColumn(legacy, w, h), 0.65) << "+x on the right, as the Builder showed it";
}
