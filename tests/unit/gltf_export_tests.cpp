/**
 * @file gltf_export_tests.cpp
 * @brief A scene as a binary glTF (src/shared/gltf_export.h): the file's structure, the shapes as triangles in the right place, the materials, the lights, and a Scene Builder scene keeping its handedness
 */

#include "pbrt_flatten_test_helpers.h"

#include "../../src/shared/gltf_export.h"
#include "../../src/shared/scene_model.h"
#include "../../src/shared/scene_pbrt_writer.h"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace {

uint32_t read32(const std::string& s, size_t at) {
	uint32_t v = 0;
	for (int i = 0; i < 4; ++i) v |= static_cast<uint32_t>(static_cast<unsigned char>(s[at + static_cast<size_t>(i)])) << (8 * i);
	return v;
}

struct Parsed {
	bool ok = false;
	std::string json;
	const unsigned char* bin = nullptr;
	size_t binSize = 0;
};

// The two chunks of a .glb, after checking the header: magic, version, and that the total length is the file's.
Parsed splitGlb(const std::string& glb) {
	Parsed p;
	if (glb.size() < 28 || read32(glb, 0) != 0x46546C67 || read32(glb, 4) != 2 || read32(glb, 8) != glb.size()) return p;
	const uint32_t jsonSize = read32(glb, 12);
	if (read32(glb, 16) != 0x4E4F534A || 20 + jsonSize + 8 > glb.size()) return p;
	p.json = glb.substr(20, jsonSize);
	const size_t binAt = 20 + jsonSize;
	const uint32_t binSize = read32(glb, binAt);
	if (read32(glb, binAt + 4) != 0x004E4942 || binAt + 8 + binSize != glb.size() || jsonSize % 4 != 0 || binSize % 4 != 0) return p;
	p.bin = reinterpret_cast<const unsigned char*>(glb.data()) + binAt + 8;
	p.binSize = binSize;
	p.ok = true;
	return p;
}

bool has(const std::string& s, const std::string& what) { return s.find(what) != std::string::npos; }

// The first `count` floats of the binary chunk: the first primitive's positions are first.
std::vector<float> firstFloats(const Parsed& p, size_t count) {
	std::vector<float> v(count);
	std::memcpy(v.data(), p.bin, count * sizeof(float));
	return v;
}

gltf_export::Result exportSource(const std::string& pbrt) { return gltf_export::fromFlatScene(flattenSource(pbrt)); }

}  // namespace

TEST(GltfExportTest, ATriangleMeshIsAValidBinaryFileWithItsGeometry) {
	const gltf_export::Result r = exportSource(kQuadMesh);
	ASSERT_TRUE(r.ok) << r.error;
	EXPECT_EQ(r.triangles, 2);
	const Parsed p = splitGlb(r.glb);
	ASSERT_TRUE(p.ok) << "magic, version, total length, chunk types and 4-byte alignment";
	EXPECT_TRUE(has(p.json, "\"version\":\"2.0\""));
	EXPECT_TRUE(has(p.json, "\"POSITION\":0"));
	EXPECT_TRUE(has(p.json, "\"count\":4"));   // two triangles sharing two of their four vertices
	EXPECT_TRUE(has(p.json, "\"componentType\":5125,\"count\":6"));   // and six indices
	EXPECT_TRUE(has(p.json, "\"min\":[0,0,0]"));
	EXPECT_TRUE(has(p.json, "\"max\":[1,1,0]"));
	const std::vector<float> v = firstFloats(p, 9);
	EXPECT_FLOAT_EQ(v[3], 1.0f) << "the second vertex's x is as written";
	EXPECT_FLOAT_EQ(v[7], 1.0f);
}

TEST(GltfExportTest, ASphereBecomesTrianglesOnItsSurface) {
	const gltf_export::Result r = exportSource("AttributeBegin\nTranslate 1 2 3\nShape \"sphere\" \"float radius\" 2\nAttributeEnd\n");
	ASSERT_TRUE(r.ok) << r.error;
	EXPECT_GT(r.triangles, 100);
	const Parsed p = splitGlb(r.glb);
	ASSERT_TRUE(p.ok);
	const std::vector<float> v = firstFloats(p, 90);
	for (size_t i = 0; i + 2 < v.size(); i += 3) {
		const double d = std::sqrt((v[i] - 1.0) * (v[i] - 1.0) + (v[i + 1] - 2.0) * (v[i + 1] - 2.0) + (v[i + 2] - 3.0) * (v[i + 2] - 3.0));
		EXPECT_NEAR(d, 2.0, 0.02) << "vertex " << i / 3 << " is on the sphere";
	}
}

TEST(GltfExportTest, DiskCylinderConeAndPatchAreAllExported) {
	const gltf_export::Result r = exportSource(
		"AttributeBegin\nShape \"disk\" \"float radius\" 1\nAttributeEnd\n"
		"AttributeBegin\nShape \"cylinder\" \"float radius\" 1 \"float zmin\" 0 \"float zmax\" 2\nAttributeEnd\n"
		"AttributeBegin\nShape \"bilinearmesh\" \"point3 P\" [ 0 0 0  1 0 0  0 1 0  1 1 0 ]\nAttributeEnd\n");
	ASSERT_TRUE(r.ok) << r.error;
	EXPECT_GT(r.triangles, 40);
	ASSERT_TRUE(splitGlb(r.glb).ok);
}

TEST(GltfExportTest, MaterialsBecomeMetallicRoughnessWithTheirExtensions) {
	const gltf_export::Result r = exportSource(
		"MakeNamedMaterial \"gold\" \"string type\" \"conductor\" \"rgb reflectance\" [ 0.9 0.7 0.2 ] \"float roughness\" 0.1\n"
		"AttributeBegin\nNamedMaterial \"gold\"\nShape \"trianglemesh\" \"integer indices\" [ 0 1 2 ] \"point3 P\" [ 0 0 0  1 0 0  0 1 0 ]\nAttributeEnd\n"
		"AttributeBegin\nMaterial \"dielectric\" \"float eta\" 1.7\nShape \"trianglemesh\" \"integer indices\" [ 0 1 2 ] \"point3 P\" [ 0 0 1  1 0 1  0 1 1 ]\nAttributeEnd\n"
		"AttributeBegin\nMaterial \"diffuse\" \"rgb reflectance\" [ 0.2 0.4 0.6 ]\nShape \"trianglemesh\" \"integer indices\" [ 0 1 2 ] \"point3 P\" [ 0 0 2  1 0 2  0 1 2 ]\nAttributeEnd\n");
	ASSERT_TRUE(r.ok) << r.error;
	EXPECT_EQ(r.materials, 3);
	const Parsed p = splitGlb(r.glb);
	ASSERT_TRUE(p.ok);
	EXPECT_TRUE(has(p.json, "\"metallicFactor\":1"));
	EXPECT_TRUE(has(p.json, "\"KHR_materials_transmission\":{\"transmissionFactor\":1}"));
	EXPECT_TRUE(has(p.json, "\"KHR_materials_ior\":{\"ior\":1.7}"));
	EXPECT_TRUE(has(p.json, "\"extensionsUsed\":[\"KHR_materials_transmission\",\"KHR_materials_ior\"]")) << p.json.substr(0, 200);
	EXPECT_TRUE(has(p.json, "\"baseColorFactor\":[0.2,0.4,0.6,1]"));
	EXPECT_TRUE(has(p.json, "\"doubleSided\":true"));
}

TEST(GltfExportTest, AnEmitterIsEmissiveAndTheLightsBecomeNodes) {
	const gltf_export::Result r = exportSource(
		"LightSource \"point\" \"point3 from\" [ 1 2 3 ] \"rgb I\" [ 10 5 0 ]\n"
		"LightSource \"distant\" \"point3 from\" [ 0 1 0 ] \"point3 to\" [ 0 0 0 ] \"rgb L\" [ 3 3 3 ]\n"
		"AttributeBegin\nAreaLightSource \"diffuse\" \"rgb L\" [ 4 2 1 ]\nShape \"trianglemesh\" \"integer indices\" [ 0 1 2 ] \"point3 P\" [ 0 0 0  1 0 0  0 1 0 ]\nAttributeEnd\n");
	ASSERT_TRUE(r.ok) << r.error;
	EXPECT_EQ(r.lights, 2);
	const Parsed p = splitGlb(r.glb);
	ASSERT_TRUE(p.ok);
	EXPECT_TRUE(has(p.json, "\"KHR_lights_punctual\""));
	EXPECT_TRUE(has(p.json, "\"type\":\"point\""));
	EXPECT_TRUE(has(p.json, "\"type\":\"directional\""));
	EXPECT_TRUE(has(p.json, "\"translation\":[1,2,3]"));
	EXPECT_TRUE(has(p.json, "\"emissiveFactor\":[1,0.5,0.25]"));
	EXPECT_TRUE(has(p.json, "\"emissiveStrength\":4"));
}

TEST(GltfExportTest, ADirectionalLightShinesTowardWhatItLightsNotAwayFromIt) {
	// from (0,1,0) to the origin: the sun is above and its light goes down (-Y); glTF's light shines along its own -Z, so the node's rotation turns -Z into -Y: 90 degrees about -X... (x, y, z, w) = (-sin45, 0, 0, cos45)
	const gltf_export::Result r = exportSource("LightSource \"distant\" \"point3 from\" [ 0 1 0 ] \"point3 to\" [ 0 0 0 ] \"rgb L\" [ 1 1 1 ]\n"
	                                           "Shape \"trianglemesh\" \"integer indices\" [ 0 1 2 ] \"point3 P\" [ 0 0 0  1 0 0  0 1 0 ]\n");
	ASSERT_TRUE(r.ok) << r.error;
	const Parsed p = splitGlb(r.glb);
	ASSERT_TRUE(p.ok);
	const size_t at = p.json.find("\"rotation\":[");
	ASSERT_NE(at, std::string::npos);
	double q[4];
	ASSERT_EQ(std::sscanf(p.json.c_str() + at, "\"rotation\":[%lf,%lf,%lf,%lf]", &q[0], &q[1], &q[2], &q[3]), 4);
	// rotate (0, 0, -1) by q: the result is where the light shines
	const double x = 0, y = 0, z = -1;
	const double tx = 2 * (q[1] * z - q[2] * y), ty = 2 * (q[2] * x - q[0] * z), tz = 2 * (q[0] * y - q[1] * x);
	const double rx = x + q[3] * tx + (q[1] * tz - q[2] * ty), ry = y + q[3] * ty + (q[2] * tx - q[0] * tz), rz = z + q[3] * tz + (q[0] * ty - q[1] * tx);
	EXPECT_NEAR(rx, 0.0, 1e-6);
	EXPECT_NEAR(ry, -1.0, 1e-6) << "the light travels down";
	EXPECT_NEAR(rz, 0.0, 1e-6);
}

TEST(GltfExportTest, WhatCannotBeExportedIsSaidAndAnEmptySceneIsRefused) {
	const gltf_export::Result r = exportSource("LightSource \"infinite\" \"rgb L\" [ 1 1 1 ]\nShape \"trianglemesh\" \"integer indices\" [ 0 1 2 ] \"point3 P\" [ 0 0 0  1 0 0  0 1 0 ]\n");
	ASSERT_TRUE(r.ok) << r.error;
	bool env = false;
	for (const std::string& n : r.notes) env = env || n.find("environment") != std::string::npos;
	EXPECT_TRUE(env);
	const gltf_export::Result none = exportSource("LightSource \"point\" \"rgb I\" [ 1 1 1 ]\n");
	EXPECT_FALSE(none.ok);
	EXPECT_FALSE(none.error.empty());
}

TEST(GltfExportTest, ASceneBuilderSceneKeepsItsHandedness) {
	// The Builder's world is right-handed and glTF is too: a sphere at +X stays at +X (the mirror the pbrt file writes is in the camera, not in the shapes)
	scene_doc::Document doc;
	doc.objects.clear();
	scene_doc::Object ball;
	ball.name = "Ball";
	ball.shape = scene_doc::ShapeKind::Sphere;
	ball.position = {3.0, 1.0, -2.0};
	ball.radius = 1.0;
	doc.objects.push_back(ball);
	const std::string text = scene_doc::toPbrt(doc);
	const pbrt_scene::ParseResult parsed = pbrt_scene::parse(text);
	ASSERT_TRUE(parsed.ok) << parsed.error;
	const gltf_export::Result r = gltf_export::fromFlatScene(pbrt_flatten::flatten(parsed.scene, {}));
	ASSERT_TRUE(r.ok) << r.error;
	const Parsed p = splitGlb(r.glb);
	ASSERT_TRUE(p.ok);
	const std::vector<float> v = firstFloats(p, 90);
	double cx = 0, cy = 0, cz = 0;
	for (size_t i = 0; i < 30; ++i) { cx += v[i * 3]; cy += v[i * 3 + 1]; cz += v[i * 3 + 2]; }
	EXPECT_NEAR(cx / 30, 3.0, 1.0);
	EXPECT_GT(cx / 30, 1.5) << "+X stays +X";
	EXPECT_NEAR(cy / 30, 1.0, 1.0);
	EXPECT_LT(cz / 30, -1.0) << "-Z stays -Z";
}

TEST(GltfExportTest, AFileThatDoesNotFitIn32BitLengthsIsRefused) {
	EXPECT_TRUE(gltf_export::detail::fitsInGlb(1000, 1000000));
	EXPECT_TRUE(gltf_export::detail::fitsInGlb(1000, 0xFFFFFFFFull - 12 - 8 - 1000 - 8));
	EXPECT_FALSE(gltf_export::detail::fitsInGlb(1000, 0xFFFFFFFFull - 12 - 8 - 1000 - 8 + 1));
	EXPECT_FALSE(gltf_export::detail::fitsInGlb(10, 5000000000ull));
}
