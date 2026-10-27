/**
 * @file pbrt_gpu_lamp_material_tests.cpp
 * @brief A lamp keeps the shape's material in pbrt-v4 (default diffuse 0.5), so it reflects. The OptiX builder stores a flat diffuse reflectance in MaterialData::albedo (what
 *        lamp_reflects() reads) and counts the lamps whose material is anything else (SceneData::lampMaterialsNotReflected), which the scene builder warns about: the CPU reflects
 *        those as their own material, OptiX draws them as pure emitters - except smooth glass (MaterialData::lampGlass), which it refracts.
 */
#include <gtest/gtest.h>

#include <vector>

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

std::string lamp(const std::string& material, double x) {
	return "AttributeBegin\n  " + material + "\n  AreaLightSource \"diffuse\" \"rgb L\" [ 4 4 4 ]\n  Translate " + std::to_string(x) +
	       " 0 0\n  Shape \"sphere\" \"float radius\" [ 0.5 ]\nAttributeEnd\n";
}

std::vector<const MaterialData*> lampMaterials(const SceneData& scene) {
	std::vector<const MaterialData*> out;
	for (const MaterialData& m : scene.materials)
		if (m.type == MaterialType::DiffuseLight) out.push_back(&m);
	return out;
}

}  // namespace

TEST(PbrtGpuLampMaterial, ADefaultLampReflectsAndABlackBodyDoesNot) {
	const SceneData scene = buildScene("WorldBegin\n" + lamp("", 0) + lamp("Material \"diffuse\" \"rgb reflectance\" [ 0 0 0 ]", 2));
	const std::vector<const MaterialData*> lamps = lampMaterials(scene);
	ASSERT_EQ(lamps.size(), 2u);
	EXPECT_TRUE(lamp_reflects(*lamps[0])) << "pbrt's default material is diffuse 0.5";
	EXPECT_FALSE(lamp_reflects(*lamps[1])) << "a black body (what the Scene Builder writes for an emitter) reflects nothing";
	EXPECT_EQ(scene.lampMaterialsNotReflected, 0) << "both are flat diffuse: nothing is lost against the CPU";
}

TEST(PbrtGpuLampMaterial, ALampWithAnotherMaterialIsCountedAsNotReflected) {
	const SceneData scene = buildScene("WorldBegin\n" + lamp("Material \"conductor\"", 0) + lamp("Material \"coateddiffuse\"", 2) + lamp("", 4));
	EXPECT_EQ(scene.lampMaterialsNotReflected, 2) << "the conductor and the coated lamp; the default one is a flat diffuse";
}

TEST(PbrtGpuLampMaterial, AnOrdinaryConductorIsNotALamp) {
	const SceneData scene = buildScene(
		"WorldBegin\nAttributeBegin\n  Material \"conductor\"\n  Shape \"sphere\" \"float radius\" [ 1 ]\nAttributeEnd\n");
	EXPECT_EQ(scene.lampMaterialsNotReflected, 0);
}

TEST(PbrtGpuLampMaterial, ASmoothGlassLampRefractsAndAClearOneLetsTheSceneThrough) {
	const SceneData scene = buildScene("WorldBegin\n" + lamp("Material \"dielectric\" \"float eta\" [ 1 ]", 0) + lamp("Material \"dielectric\" \"float eta\" [ 1.5 ]", 2) +
									   lamp("Material \"dielectric\" \"float roughness\" [ 0.3 ]", 4));
	const std::vector<const MaterialData*> lamps = lampMaterials(scene);
	ASSERT_EQ(lamps.size(), 3u);
	EXPECT_TRUE(lamp_glass(*lamps[0]) && lamp_scatters(*lamps[0]));
	EXPECT_FALSE(lamp_reflects(*lamps[0])) << "glass refracts: it is not the diffuse lamp";
	EXPECT_FLOAT_EQ(lamps[0]->ior, 1.0f) << "index 1: a glow the scene behind shows through (pbrt's dielectric at eta 1)";
	EXPECT_FLOAT_EQ(lamps[1]->ior, 1.5f);
	EXPECT_FALSE(lamp_glass(*lamps[2])) << "rough glass is not handled on a lamp";
	EXPECT_EQ(scene.lampMaterialsNotReflected, 1) << "only the rough one is lost against the CPU";
}
