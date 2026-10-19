/**
 * @file gltf_import_tests.cpp
 * @brief A glTF model read into parts (src/shared/gltf_import.h): the exporter's own files, hand-made files (node transforms, normals, strips, pictures), files that must be refused, and the PLY it writes
 */

#include "pbrt_flatten_test_helpers.h"

#include "../../src/shared/gltf_export.h"
#include "../../src/shared/gltf_import.h"
#include "../../src/shared/ply_mesh.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace {

std::string base64Encode(const std::string& data) {
	static const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string out;
	for (size_t i = 0; i < data.size(); i += 3) {
		const uint32_t a = static_cast<unsigned char>(data[i]), b = i + 1 < data.size() ? static_cast<unsigned char>(data[i + 1]) : 0, c = i + 2 < data.size() ? static_cast<unsigned char>(data[i + 2]) : 0;
		const uint32_t v = (a << 16) | (b << 8) | c;
		out += alphabet[(v >> 18) & 63];
		out += alphabet[(v >> 12) & 63];
		out += i + 1 < data.size() ? alphabet[(v >> 6) & 63] : '=';
		out += i + 2 < data.size() ? alphabet[v & 63] : '=';
	}
	return out;
}

std::string floats(const std::vector<float>& v) { return std::string(reinterpret_cast<const char*>(v.data()), v.size() * sizeof(float)); }
std::string u16s(const std::vector<uint16_t>& v) { return std::string(reinterpret_cast<const char*>(v.data()), v.size() * sizeof(uint16_t)); }

// A .gltf whose one buffer is `bin` (as a data: URI) with the given pieces of JSON for the rest.
std::string gltfText(const std::string& bin, const std::string& rest, const std::string& version = "2.0") {
	return "{\"asset\":{\"version\":\"" + version + "\"},\"buffers\":[{\"byteLength\":" + std::to_string(bin.size()) + ",\"uri\":\"data:application/octet-stream;base64," + base64Encode(bin) + "\"}]," + rest + "}";
}

// One triangle (0,0,0) (1,0,0) (0,1,0), counter-clockwise seen from +Z, with u16 indices.
std::string triangleGltf(const std::string& nodes, const std::string& extra = std::string(), const std::string& primitiveExtra = std::string(), bool withNormals = false, const std::vector<float>& normals = {0, 0, 1, 0, 0, 1, 0, 0, 1}) {
	const std::string pos = floats({0, 0, 0, 1, 0, 0, 0, 1, 0}), nrm = floats(normals), idx = u16s({0, 1, 2, 0});
	const std::string bin = pos + nrm + idx;
	std::string views = "{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36},{\"buffer\":0,\"byteOffset\":36,\"byteLength\":36},{\"buffer\":0,\"byteOffset\":72,\"byteLength\":6}";
	std::string accessors = "{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},{\"bufferView\":1,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},{\"bufferView\":2,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"}";
	const std::string attrs = withNormals ? "\"POSITION\":0,\"NORMAL\":1" : "\"POSITION\":0";
	return gltfText(bin, "\"bufferViews\":[" + views + "],\"accessors\":[" + accessors + "],\"meshes\":[{\"primitives\":[{\"attributes\":{" + attrs + "},\"indices\":2" + primitiveExtra + "}]}],\"nodes\":[" + nodes + "],\"scenes\":[{\"nodes\":[0]}]" + extra);
}

std::string replaceFirst(std::string s, const std::string& from, const std::string& to) {
	const size_t at = s.find(from);
	EXPECT_NE(at, std::string::npos) << from;
	if (at != std::string::npos) s.replace(at, from.size(), to);
	return s;
}

}  // namespace

TEST(GltfImportTest, TheExportersOwnFilesComeBackWithTheirShapesAndMaterials) {
	const std::string pbrt =
		"MakeNamedMaterial \"gold\" \"string type\" \"conductor\" \"rgb reflectance\" [ 0.9 0.7 0.2 ] \"float roughness\" 0.1\n"
		"AttributeBegin\nNamedMaterial \"gold\"\nTranslate 0 1 0\nShape \"sphere\" \"float radius\" 1\nAttributeEnd\n"
		"AttributeBegin\nMaterial \"diffuse\" \"rgb reflectance\" [ 0.2 0.4 0.6 ]\nShape \"trianglemesh\" \"integer indices\" [ 0 1 2 ] \"point3 P\" [ 3 0 0  4 0 0  3 0 1 ]\nAttributeEnd\n";
	const gltf_export::Result exported = gltf_export::fromFlatScene(flattenSource(pbrt));
	ASSERT_TRUE(exported.ok);
	const gltf_import::Result r = gltf_import::parse(exported.glb);
	ASSERT_TRUE(r.ok) << r.error;
	EXPECT_EQ(r.model.parts.size(), 2u) << "one part for each material";
	EXPECT_EQ(static_cast<int>(r.model.triangles), exported.triangles);
	EXPECT_NEAR(r.model.lo[1], 0.0, 0.02);
	EXPECT_NEAR(r.model.hi[1], 2.0, 0.02);
	EXPECT_NEAR(r.model.hi[0], 4.0, 1e-6);
	ASSERT_EQ(r.model.materials.size(), 2u);
	bool metal = false, blue = false;
	for (const gltf_import::Material& m : r.model.materials) {
		metal = metal || m.metallic > 0.99;
		blue = blue || (std::fabs(m.baseColor[2] - 0.6) < 1e-6 && m.baseColor[0] < 0.3);
	}
	EXPECT_TRUE(metal);
	EXPECT_TRUE(blue);
}

TEST(GltfImportTest, ANodesTranslationRotationAndScaleMoveItsMesh) {
	// scale 2, then a quarter turn about Z (x -> y), then moved by (1, 2, 3): the vertex (1,0,0) goes to (1, 4, 3)... scale first: (2,0,0), rotate: (0,2,0), move: (1,4,3)
	const double s = std::sqrt(0.5);
	const gltf_import::Result r = gltf_import::parse(triangleGltf("{\"mesh\":0,\"translation\":[1,2,3],\"rotation\":[0,0," + std::to_string(s) + "," + std::to_string(s) + "],\"scale\":[2,2,2]}"));
	ASSERT_TRUE(r.ok) << r.error;
	ASSERT_EQ(r.model.parts.size(), 1u);
	const std::vector<double>& p = r.model.parts[0].positions;
	EXPECT_NEAR(p[3], 1.0, 1e-6);
	EXPECT_NEAR(p[4], 4.0, 1e-6);
	EXPECT_NEAR(p[5], 3.0, 1e-6);
	EXPECT_NEAR(p[0], 1.0, 1e-6);
	EXPECT_NEAR(p[1], 2.0, 1e-6);
}

TEST(GltfImportTest, ChildNodesInheritTheirParentsMatrix) {
	const std::string pos = floats({0, 0, 0, 1, 0, 0, 0, 1, 0});
	const gltf_import::Result r = gltf_import::parse(gltfText(pos,
		"\"bufferViews\":[{\"buffer\":0,\"byteLength\":36}],\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"}],"
		"\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0}}]}],\"nodes\":[{\"children\":[1],\"translation\":[10,0,0]},{\"mesh\":0,\"matrix\":[1,0,0,0, 0,1,0,0, 0,0,1,0, 0,5,0,1]}],\"scenes\":[{\"nodes\":[0]}]"));
	ASSERT_TRUE(r.ok) << r.error;
	EXPECT_NEAR(r.model.lo[0], 10.0, 1e-9);
	EXPECT_NEAR(r.model.lo[1], 5.0, 1e-9) << "a column-major matrix with its translation in the last column";
	EXPECT_EQ(r.model.triangles, 1u) << "no indices: every three vertices are a triangle";
}

TEST(GltfImportTest, SmoothNormalsAreMadeWhereTheFileHasNoneAndAMirrorKeepsThemOutward) {
	const gltf_import::Result r = gltf_import::parse(triangleGltf("{\"mesh\":0}"));
	ASSERT_TRUE(r.ok) << r.error;
	const std::vector<double>& n = r.model.parts[0].normals;
	ASSERT_EQ(n.size(), 9u);
	EXPECT_NEAR(n[2], 1.0, 1e-9) << "a counter-clockwise triangle seen from +Z faces +Z";
	const gltf_import::Result mirrored = gltf_import::parse(triangleGltf("{\"mesh\":0,\"scale\":[-1,1,1]}"));
	ASSERT_TRUE(mirrored.ok);
	EXPECT_NEAR(mirrored.model.parts[0].normals[2], 1.0, 1e-9) << "the mirror turns the triangle's winding over, so it still faces +Z";
	EXPECT_NEAR(mirrored.model.hi[0], 0.0, 1e-12);
	EXPECT_NEAR(mirrored.model.lo[0], -1.0, 1e-12);
}

TEST(GltfImportTest, NormalsFromTheFileTurnLikeASurfaceUnderANonUniformScale) {
	// a normal (0,1,1)/sqrt2 under a stretch of 2 in y becomes (0, 1/2, 1) normalised
	const float h = static_cast<float>(std::sqrt(0.5));
	const gltf_import::Result r = gltf_import::parse(triangleGltf("{\"mesh\":0,\"scale\":[1,2,1]}", std::string(), std::string(), true, {0, h, h, 0, h, h, 0, h, h}));
	ASSERT_TRUE(r.ok) << r.error;
	const std::vector<double>& n = r.model.parts[0].normals;
	EXPECT_NEAR(n[0], 0.0, 1e-6);
	EXPECT_NEAR(n[1], 1.0 / std::sqrt(5.0), 1e-6);
	EXPECT_NEAR(n[2], 2.0 / std::sqrt(5.0), 1e-6) << "(0, 1/2, 1) normalised: the stretch leans the normal towards z, not towards y";
}

TEST(GltfImportTest, StripsAndFansBecomeTriangles) {
	const std::string pos = floats({0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 1, 0, 2, 0, 0});
	const auto make = [&](int mode) {
		return gltf_import::parse(gltfText(pos,
			"\"bufferViews\":[{\"buffer\":0,\"byteLength\":60}],\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":5,\"type\":\"VEC3\"}],"
			"\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"mode\":" + std::to_string(mode) + "}]}],\"nodes\":[{\"mesh\":0}],\"scenes\":[{\"nodes\":[0]}]"));
	};
	const gltf_import::Result strip = make(5), fan = make(6), lines = make(1);
	ASSERT_TRUE(strip.ok);
	EXPECT_EQ(strip.model.triangles, 3u) << "5 vertices as a strip";
	ASSERT_TRUE(fan.ok);
	EXPECT_EQ(fan.model.triangles, 3u);
	EXPECT_FALSE(lines.ok) << "lines are not read, and a model of nothing else has nothing to place";
}

TEST(GltfImportTest, MaterialsFactorsExtensionsAndABaseColourPictureAreRead) {
	const std::string png = "PNGBYTES";
	const std::string extra =
		",\"materials\":[{\"name\":\"glass\",\"pbrMetallicRoughness\":{\"baseColorFactor\":[0.5,0.6,0.7,1],\"metallicFactor\":0,\"roughnessFactor\":0.25,\"baseColorTexture\":{\"index\":0}},"
		"\"emissiveFactor\":[1,0.5,0],\"extensions\":{\"KHR_materials_transmission\":{\"transmissionFactor\":1},\"KHR_materials_ior\":{\"ior\":1.7},\"KHR_materials_emissive_strength\":{\"emissiveStrength\":4}}}],"
		"\"textures\":[{\"source\":0}],\"images\":[{\"uri\":\"data:image/png;base64," + base64Encode(png) + "\"}]";
	const gltf_import::Result r = gltf_import::parse(triangleGltf("{\"mesh\":0}", extra, ",\"material\":0"));
	ASSERT_TRUE(r.ok) << r.error;
	ASSERT_EQ(r.model.materials.size(), 1u);
	const gltf_import::Material& m = r.model.materials[0];
	EXPECT_EQ(m.name, "glass");
	EXPECT_DOUBLE_EQ(m.metallic, 0.0);
	EXPECT_DOUBLE_EQ(m.roughness, 0.25);
	EXPECT_DOUBLE_EQ(m.transmission, 1.0);
	EXPECT_DOUBLE_EQ(m.ior, 1.7);
	EXPECT_DOUBLE_EQ(m.emissive[0], 4.0) << "the emissive factor times the strength";
	EXPECT_DOUBLE_EQ(m.emissive[1], 2.0);
	ASSERT_EQ(r.model.images.size(), 1u);
	EXPECT_EQ(m.baseColorImage, 0);
	EXPECT_EQ(r.model.images[0].mime, "image/png");
	EXPECT_EQ(std::string(r.model.images[0].bytes.begin(), r.model.images[0].bytes.end()), png);
}

TEST(GltfImportTest, ExternalFilesComeThroughTheFetcherAndAMissingOneIsSaid) {
	const std::string pos = floats({0, 0, 0, 1, 0, 0, 0, 1, 0});
	const std::string text = "{\"asset\":{\"version\":\"2.0\"},\"buffers\":[{\"byteLength\":36,\"uri\":\"model.bin\"}],\"bufferViews\":[{\"buffer\":0,\"byteLength\":36}],"
	                         "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"}],\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0}}]}],\"nodes\":[{\"mesh\":0}]}";
	std::string asked;
	const gltf_import::Result ok = gltf_import::parse(text, [&](const std::string& uri, std::vector<unsigned char>& bytes) {
		asked = uri;
		bytes.assign(pos.begin(), pos.end());
		return true;
	});
	ASSERT_TRUE(ok.ok) << ok.error;
	EXPECT_EQ(asked, "model.bin");
	const gltf_import::Result missing = gltf_import::parse(text, [](const std::string&, std::vector<unsigned char>&) { return false; });
	EXPECT_FALSE(missing.ok);
	EXPECT_NE(missing.error.find("model.bin"), std::string::npos);
	EXPECT_FALSE(gltf_import::parse(text).ok) << "no fetcher: an external file cannot be had";
}

TEST(GltfImportTest, FilesThatCannotBeReadAreRefusedWithAMessage) {
	const std::string good = triangleGltf("{\"mesh\":0}");
	EXPECT_TRUE(gltf_import::parse(good).ok) << "(a valid file is read)";
	EXPECT_FALSE(gltf_import::parse("").ok);
	EXPECT_FALSE(gltf_import::parse("not json at all").ok);
	EXPECT_FALSE(gltf_import::parse(replaceFirst(good, "\"version\":\"2.0\"", "\"version\":\"1.0\"")).ok);
	const gltf_import::Result draco = gltf_import::parse(triangleGltf("{\"mesh\":0}", ",\"extensionsRequired\":[\"KHR_draco_mesh_compression\"]"));
	ASSERT_FALSE(draco.ok);
	EXPECT_NE(draco.error.find("Draco"), std::string::npos);
	// an accessor that reads past its buffer view, indices that point outside the vertices, a count that is absurd
	EXPECT_FALSE(gltf_import::parse(replaceFirst(good, "\"count\":3,\"type\":\"VEC3\"}", "\"count\":9,\"type\":\"VEC3\"}")).ok);
	EXPECT_FALSE(gltf_import::parse(replaceFirst(good, "\"count\":3,\"type\":\"VEC3\"}", "\"count\":2,\"type\":\"VEC3\"}")).ok) << "the indices still name vertex 2";
	EXPECT_FALSE(gltf_import::parse(replaceFirst(good, "\"count\":3,\"type\":\"VEC3\"}", "\"count\":1e18,\"type\":\"VEC3\"}")).ok);
}

TEST(GltfImportTest, ATruncatedBinaryFileNeverCrashes) {
	const gltf_export::Result exported = gltf_export::fromFlatScene(flattenSource("Shape \"sphere\" \"float radius\" 1\n"));
	ASSERT_TRUE(exported.ok);
	ASSERT_TRUE(gltf_import::parse(exported.glb).ok);
	for (size_t cut = 0; cut < exported.glb.size(); cut += std::max<size_t>(1, exported.glb.size() / 97)) {
		const gltf_import::Result r = gltf_import::parse(exported.glb.substr(0, cut));
		if (cut < exported.glb.size() - 8) EXPECT_FALSE(r.ok) << "cut at " << cut << " of " << exported.glb.size();
	}
	// every byte of the binary part turned into garbage must not crash either (the indices may point anywhere: it is refused, or it is read)
	std::string noisy = exported.glb;
	for (size_t i = noisy.size() / 2; i < noisy.size(); i += 7) noisy[i] = static_cast<char>(0xA5 ^ static_cast<char>(i));
	(void)gltf_import::parse(noisy);
}

TEST(GltfImportTest, APartIsWrittenAsAPlyThePlyReaderGivesBack) {
	const gltf_import::Result r = gltf_import::parse(triangleGltf("{\"mesh\":0}", std::string(), std::string(), true));
	ASSERT_TRUE(r.ok);
	gltf_import::Part part = r.model.parts[0];
	part.uvs = {0.0, 0.25, 1.0, 0.25, 0.0, 1.0};
	const double offset[3] = {1.0, 0.0, 0.0};
	const std::string ply = gltf_import::toPly(part, offset, 4.0);
	const ply_mesh::LoadResult back = ply_mesh::parse(ply);
	ASSERT_TRUE(back.ok) << back.error;
	EXPECT_EQ(back.mesh.vertexCount(), 3u);
	EXPECT_EQ(back.mesh.triangleCount(), 1u);
	EXPECT_NEAR(back.mesh.positions[3], 0.0, 1e-6) << "(1 - 1) * 4";
	EXPECT_NEAR(back.mesh.positions[0], -4.0, 1e-6) << "(0 - 1) * 4";
	EXPECT_NEAR(back.mesh.positions[7], 4.0, 1e-6);
	ASSERT_EQ(back.mesh.uvs.size(), 6u);
	EXPECT_NEAR(back.mesh.uvs[1], 0.75, 1e-6) << "v is turned over: a glTF picture starts at its top";
	ASSERT_EQ(back.mesh.normals.size(), 9u);
	EXPECT_NEAR(back.mesh.normals[2], 1.0, 1e-6);
}

// RAY_TRACER_GLB_DIR=<folder of .glb files>: every file in it is read (a check against real files, such as the exports of all the bundled scenes); skipped when it is not set.
TEST(GltfImportTest, EveryFileOfAFolderReads) {
	const char* dir = std::getenv("RAY_TRACER_GLB_DIR");
	if (!dir) GTEST_SKIP() << "RAY_TRACER_GLB_DIR is not set";
	int files = 0;
	for (const auto& entry : std::filesystem::directory_iterator(dir)) {
		if (entry.path().extension() != ".glb") continue;
		std::ifstream in(entry.path(), std::ios::binary);
		const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		const gltf_import::Result r = gltf_import::parse(bytes);
		EXPECT_TRUE(r.ok) << entry.path().filename().string() << ": " << r.error;
		if (r.ok) {
			EXPECT_GT(r.model.triangles, 0u);
			for (const gltf_import::Part& p : r.model.parts) {
				EXPECT_EQ(p.normals.size(), p.positions.size());
				EXPECT_EQ(p.uvs.size() / 2, p.positions.size() / 3);
			}
		}
		++files;
	}
	EXPECT_GT(files, 0);
}
