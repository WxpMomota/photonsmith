/**
 * @file pbrt_gpu_subsurface_texture_tests.cpp
 * @brief A Subsurface material whose "reflectance" is an image (the pbrt-v4-scenes head) reaches the OptiX backends as the image's index (transmittanceTextureIdx, free
 *        on this kind), its scale and the mean free path (bssrdf_sigma_a); the device inverts the beam-diffusion table for the image's value at each hit. A constant
 *        reflectance is inverted once when the scene is built and keeps the fields it always had.
 */
#include <gtest/gtest.h>

#include <filesystem>
#include <string>

#include "pbrt_gpu_builder.h"
#include "../../src/shared/pbrt_flatten.h"
#include "../../src/shared/pbrt_scene.h"

namespace {

SceneData buildScene(const std::string& body) {
	const pbrt_scene::ParseResult r = pbrt_scene::parse(body);
	EXPECT_TRUE(r.ok) << r.error;
	const pbrt_flatten::FlatScene flat = pbrt_flatten::flatten(r.scene);
	SceneData scene;
	pbrt_gpu::build(flat, scene);
	return scene;
}

const MaterialData* subsurfaceMaterial(const SceneData& scene) {
	for (const MaterialData& m : scene.materials)
		if (m.type == MaterialType::Subsurface) return &m;
	return nullptr;
}

const char* kBall = "Shape \"sphere\" \"float radius\" [ 1 ]\n";

}  // namespace

TEST(PbrtGpuSubsurfaceTexture, AnImageReflectanceKeepsTheImageAndTheMeanFreePathForTheDevice) {
	if (!std::filesystem::exists("pbrt_scenes/uv-checker.bmp")) GTEST_SKIP() << "pbrt_scenes/uv-checker.bmp is not next to the working directory";
	const SceneData scene = buildScene(
		"WorldBegin\n"
		"Texture \"albedo\" \"spectrum\" \"imagemap\" \"string filename\" [ \"pbrt_scenes/uv-checker.bmp\" ]\n"
		"Material \"subsurface\" \"texture reflectance\" [ \"albedo\" ] \"rgb mfp\" [ 0.5 0.25 0.125 ] \"float scale\" 2\n" + std::string(kBall));
	const MaterialData* m = subsurfaceMaterial(scene);
	ASSERT_NE(m, nullptr);
	EXPECT_GE(m->transmittanceTextureIdx, 0) << "the reflectance image";
	EXPECT_LT(m->transmittanceTextureIdx, static_cast<int>(scene.textures.size()));
	EXPECT_GE(m->textureIdx, 0) << "the beam-diffusion table";
	EXPECT_LT(m->textureIdx, static_cast<int>(scene.bssrdfTables.size()));
	EXPECT_NEAR(m->bssrdf_sigma_a.x, 1.0f, 1e-6f) << "the mean free path times scale";
	EXPECT_NEAR(m->bssrdf_sigma_a.y, 0.5f, 1e-6f);
	EXPECT_NEAR(m->bssrdf_sigma_a.z, 0.25f, 1e-6f);
}

TEST(PbrtGpuSubsurfaceTexture, AConstantReflectanceIsInvertedOnceAndHasNoImage) {
	const SceneData scene = buildScene(
		"WorldBegin\nMaterial \"subsurface\" \"rgb reflectance\" [ 0.8 0.8 0.8 ] \"rgb mfp\" [ 0.4 0.4 0.4 ]\n" + std::string(kBall));
	const MaterialData* m = subsurfaceMaterial(scene);
	ASSERT_NE(m, nullptr);
	EXPECT_EQ(m->transmittanceTextureIdx, -1);
	EXPECT_NEAR(m->bssrdf_sigma_a.x, 0.0085459f, 1e-4f) << "sigma_a of reflectance 0.8, mfp 0.4, eta 1.33 (the CPU test pins the same numbers)";
	EXPECT_NEAR(m->bssrdf_sigma_s.x, 2.491454f, 1e-3f);
}
