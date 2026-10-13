// Tests for the Scene Builder's rounded box, cyclorama and terrain (src/shared/scene_shapes.h): each mesh against a closed form (surface area, extent), that its triangles face the way its
// normals do, that the terrain is the same every time, and that the shapes are written, saved, checked and scaled like the other ready-made shapes.
#include <gtest/gtest.h>

#include "agreement_test_helpers.h"
#include "../../src/shared/pbrt_load.h"
#include "../../src/shared/scene_document.h"
#include "../../src/shared/scene_array.h"
#include "../../src/shared/scene_gizmo.h"

#include <cmath>
#include <cstdio>
#include <fstream>

using namespace scene_doc;

namespace {

constexpr double kPi = 3.14159265358979323846;

double meshArea(const ShapeMesh& m) {
	double area = 0;
	for (std::size_t t = 0; t + 2 < m.indices.size(); t += 3) {
		const int a = m.indices[t], b = m.indices[t + 1], c = m.indices[t + 2];
		const double ux = m.P[b * 3] - m.P[a * 3], uy = m.P[b * 3 + 1] - m.P[a * 3 + 1], uz = m.P[b * 3 + 2] - m.P[a * 3 + 2];
		const double vx = m.P[c * 3] - m.P[a * 3], vy = m.P[c * 3 + 1] - m.P[a * 3 + 1], vz = m.P[c * 3 + 2] - m.P[a * 3 + 2];
		const double cx = uy * vz - uz * vy, cy = uz * vx - ux * vz, cz = ux * vy - uy * vx;
		area += 0.5 * std::sqrt(cx * cx + cy * cy + cz * cz);
	}
	return area;
}

// Every triangle's own normal (from its winding) points the way its vertices' normals do, the normals are unit length, and every index is in range.
void expectWellFormed(const ShapeMesh& m, const char* what) {
	ASSERT_GT(m.triangleCount(), 0u) << what;
	ASSERT_EQ(m.N.size(), m.P.size()) << what;
	ASSERT_EQ(m.UV.size() / 2, m.vertexCount()) << what;
	for (std::size_t i = 0; i + 2 < m.N.size(); i += 3) ASSERT_NEAR(std::sqrt(m.N[i] * m.N[i] + m.N[i + 1] * m.N[i + 1] + m.N[i + 2] * m.N[i + 2]), 1.0, 1e-9) << what;
	int disagree = 0;
	for (std::size_t t = 0; t + 2 < m.indices.size(); t += 3) {
		const int a = m.indices[t], b = m.indices[t + 1], c = m.indices[t + 2];
		ASSERT_TRUE(a >= 0 && b >= 0 && c >= 0 && a < static_cast<int>(m.vertexCount()) && b < static_cast<int>(m.vertexCount()) && c < static_cast<int>(m.vertexCount())) << what;
		const double ux = m.P[b * 3] - m.P[a * 3], uy = m.P[b * 3 + 1] - m.P[a * 3 + 1], uz = m.P[b * 3 + 2] - m.P[a * 3 + 2];
		const double vx = m.P[c * 3] - m.P[a * 3], vy = m.P[c * 3 + 1] - m.P[a * 3 + 1], vz = m.P[c * 3 + 2] - m.P[a * 3 + 2];
		const double gx = uy * vz - uz * vy, gy = uz * vx - ux * vz, gz = ux * vy - uy * vx;
		const double nx = m.N[a * 3] + m.N[b * 3] + m.N[c * 3], ny = m.N[a * 3 + 1] + m.N[b * 3 + 1] + m.N[c * 3 + 1], nz = m.N[a * 3 + 2] + m.N[b * 3 + 2] + m.N[c * 3 + 2];
		if (gx * nx + gy * ny + gz * nz <= 0) ++disagree;
	}
	EXPECT_EQ(disagree, 0) << what << ": triangles that face against their normals";
}

}  // namespace

TEST(RoundedBoxTest, ItsSurfaceAreaIsTheClosedFormAndItFillsExactlyItsSize) {
	const double a = 3.0, b = 2.0, c = 1.5;
	for (double r : {0.0, 0.1, 0.3, 0.7}) {
		const ShapeMesh m = roundedBoxMesh(a, b, c, r);
		expectWellFormed(m, "rounded box");
		const double ea = a - 2 * r, eb = b - 2 * r, ec = c - 2 * r;
		const double want = 2.0 * (ea * eb + eb * ec + ea * ec) + 2.0 * kPi * r * (ea + eb + ec) + 4.0 * kPi * r * r;
		EXPECT_NEAR(meshArea(m), want, 0.015 * want) << "radius " << r;
		double hi[3] = {-1e9, -1e9, -1e9}, lo[3] = {1e9, 1e9, 1e9};
		for (std::size_t i = 0; i + 2 < m.P.size(); i += 3)
			for (int k = 0; k < 3; ++k) { hi[k] = std::max(hi[k], m.P[i + k]); lo[k] = std::min(lo[k], m.P[i + k]); }
		EXPECT_NEAR(hi[0], a / 2, 1e-9);
		EXPECT_NEAR(lo[0], -a / 2, 1e-9);
		EXPECT_NEAR(hi[1], b / 2, 1e-9);
		EXPECT_NEAR(hi[2], c / 2, 1e-9);
	}
}

TEST(RoundedBoxTest, ARadiusOfZeroIsAPlainBoxAndAHugeOneIsAsRoundAsItAllows) {
	const ShapeMesh sharp = roundedBoxMesh(2, 2, 2, 0.0);
	EXPECT_NEAR(meshArea(sharp), 24.0, 1e-9);
	const ShapeMesh pill = roundedBoxMesh(2, 2, 2, 50.0);   // clamped to 1: a sphere of radius 1
	expectWellFormed(pill, "clamped");
	EXPECT_NEAR(meshArea(pill), 4.0 * kPi, 0.03 * 4.0 * kPi);
	for (std::size_t i = 0; i + 2 < pill.P.size(); i += 3) EXPECT_NEAR(std::sqrt(pill.P[i] * pill.P[i] + pill.P[i + 1] * pill.P[i + 1] + pill.P[i + 2] * pill.P[i + 2]), 1.0, 1e-9);
}

TEST(CycloramaTest, ItsAreaIsFloorPlusCurvePlusWallAndItFacesTheCameraSide) {
	const double w = 12, h = 6, d = 10;
	for (double r : {0.0, 1.0, 2.5, 5.0}) {
		const ShapeMesh m = cycloramaMesh(w, h, d, r);
		expectWellFormed(m, "cyclorama");
		const double want = w * ((d - r) + 0.5 * kPi * r + (h - r));
		EXPECT_NEAR(meshArea(m), want, 0.006 * want) << "radius " << r;
		for (std::size_t i = 0; i + 2 < m.P.size(); i += 3) {
			EXPECT_LE(std::fabs(m.P[i]), w / 2 + 1e-9);
			EXPECT_LE(std::fabs(m.P[i + 1]), h / 2 + 1e-9);
			EXPECT_LE(std::fabs(m.P[i + 2]), d / 2 + 1e-9);
			EXPECT_GT(m.N[i + 1] + m.N[i + 2], 0.0) << "the surface faces up and towards +Z, into the room";
		}
	}
	const ShapeMesh m = cycloramaMesh(w, h, d, 2.5);
	double floorY = 1e9, wallZ = 1e9, topY = -1e9;
	for (std::size_t i = 0; i + 2 < m.P.size(); i += 3) { floorY = std::min(floorY, m.P[i + 1]); wallZ = std::min(wallZ, m.P[i + 2]); topY = std::max(topY, m.P[i + 1]); }
	EXPECT_NEAR(floorY, -h / 2, 1e-9) << "the floor is the bottom";
	EXPECT_NEAR(wallZ, -d / 2, 1e-9) << "the wall is at the back";
	EXPECT_NEAR(topY, h / 2, 1e-9);
}

TEST(TerrainTest, ItIsTheSameEveryTimeDiffersBySeedAndKeepsWithinItsHeight) {
	const ShapeMesh a = terrainMesh(10, 2, 10, 40, 0.5, 3, 7), b = terrainMesh(10, 2, 10, 40, 0.5, 3, 7), c = terrainMesh(10, 2, 10, 40, 0.5, 3, 8);
	expectWellFormed(a, "terrain");
	EXPECT_EQ(a.P, b.P) << "the same numbers make the same ground";
	EXPECT_NE(a.P, c.P) << "another seed makes another ground";
	EXPECT_EQ(a.vertexCount(), 41u * 41u);
	EXPECT_EQ(a.triangleCount(), 2u * 40u * 40u);
	double lo = 1e9, hi = -1e9;
	for (std::size_t i = 1; i < a.P.size(); i += 3) { lo = std::min(lo, a.P[i]); hi = std::max(hi, a.P[i]); }
	EXPECT_GE(lo, -1.0 - 1e-9);
	EXPECT_LE(hi, 1.0 + 1e-9);
	EXPECT_GT(hi - lo, 0.5) << "the hills use a good part of the height";
	for (std::size_t i = 1; i < a.N.size(); i += 3) EXPECT_GT(a.N[i], 0.0) << "the ground faces up";
}

TEST(TerrainTest, RoughnessAddsSmallLumpsAndTheGridSizeIsClamped) {
	const auto roughnessOf = [](const ShapeMesh& m, int n) {   // the sum of height differences between neighbouring grid points
		double sum = 0;
		for (int j = 0; j <= n; ++j)
			for (int i = 0; i < n; ++i) sum += std::fabs(m.P[(j * (n + 1) + i + 1) * 3 + 1] - m.P[(j * (n + 1) + i) * 3 + 1]);
		return sum;
	};
	const ShapeMesh smooth = terrainMesh(10, 2, 10, 60, 0.0, 3, 1), rough = terrainMesh(10, 2, 10, 60, 1.0, 3, 1);
	EXPECT_GT(roughnessOf(rough, 60), 1.5 * roughnessOf(smooth, 60));
	EXPECT_EQ(terrainMesh(10, 2, 10, 5000, 0.5, 3, 1).vertexCount(), static_cast<std::size_t>((kMaxTerrainResolution + 1) * (kMaxTerrainResolution + 1)));
	EXPECT_EQ(terrainMesh(10, 2, 10, 0, 0.5, 3, 1).vertexCount(), 9u) << "at least 2 cells across";
	const ShapeMesh flat = terrainMesh(10, 0, 10, 10, 0.5, 3, 1);
	for (std::size_t i = 1; i < flat.P.size(); i += 3) EXPECT_NEAR(flat.P[i], 0.0, 1e-12);
}

TEST(MoreShapesTest, TheyAreAddedWrittenSavedCheckedAndScaledLikeTheOthers) {
	for (ShapeKind k : {ShapeKind::RoundedBox, ShapeKind::Cyclorama, ShapeKind::Terrain}) {
		EXPECT_TRUE(isGeneratedShape(k));
		bool listed = false;
		for (ShapeKind all : allShapeKinds()) listed = listed || all == k;
		EXPECT_TRUE(listed) << toString(k);
		ShapeKind back = ShapeKind::Sphere;
		EXPECT_TRUE(shapeKindFromString(toString(k), back));
		EXPECT_EQ(back, k);
		Document d;
		d.objects.push_back(makeObject(k, "Thing"));
		d.lights.push_back(Light());
		EXPECT_FALSE(hasErrors(validate(d))) << toString(k);
		const std::string text = toPbrt(d);
		EXPECT_NE(text.find("Shape \"trianglemesh\""), std::string::npos) << toString(k);
		const std::string path = "scene_more_shapes_load.pbrt";
		{
			std::ofstream out(path, std::ios::binary);
			out << text;
		}
		const pbrt_load::LoadResult r = pbrt_load::loadFile(path);
		std::remove(path.c_str());
		ASSERT_TRUE(r.ok) << toString(k) << ": " << r.error;
		EXPECT_FALSE(r.scene.triangles.empty()) << toString(k) << ": the renderer's loader makes triangles of it";
		Document back2;
		std::string error;
		ASSERT_TRUE(fromJson(toJson(d), back2, error)) << error;
		EXPECT_EQ(back2.objects[0].shape, k);
		EXPECT_EQ(toJson(back2), toJson(d));
		EXPECT_GT(boundingRadius(d.objects[0]), 0.5) << toString(k);
		const Object scaled = scene_gizmo::scaledObject(d.objects[0], 0, 2.0);
		EXPECT_NEAR(scaled.size.x, d.objects[0].size.x * 2.0, 1e-9) << toString(k) << ": the X handle scales its width";
	}
}

TEST(MoreShapesTest, TheSeedIsSavedOnlyForATerrainAndTheChecksCatchBadValues) {
	Document d;
	d.objects.push_back(makeObject(ShapeKind::Box, "Box"));
	EXPECT_EQ(toJson(d).find("\"seed\""), std::string::npos) << "other scenes are written as they always were";
	Object t = makeObject(ShapeKind::Terrain, "Ground");
	t.seed = 42;
	d.objects.push_back(t);
	Document back;
	std::string error;
	ASSERT_TRUE(fromJson(toJson(d), back, error)) << error;
	EXPECT_EQ(back.objects[1].seed, 42);
	d.lights.push_back(Light());
	EXPECT_FALSE(hasErrors(validate(d)));
	d.objects[1].steps = 1;
	EXPECT_TRUE(hasErrors(validate(d)));
	d.objects[1].steps = 40;
	d.objects[1].radius = 2.0;
	EXPECT_TRUE(hasErrors(validate(d)));
	d.objects[1] = makeObject(ShapeKind::Cyclorama, "Backdrop");
	d.objects[1].size.y = 0;
	EXPECT_TRUE(hasErrors(validate(d)));
	d.objects[1] = makeObject(ShapeKind::RoundedBox, "Box");
	d.objects[1].radius = -0.1;
	EXPECT_TRUE(hasErrors(validate(d)));
	d.objects[1].radius = 5.0;
	EXPECT_FALSE(hasErrors(validate(d)));
	bool warned = false;
	for (const Problem& p : validate(d)) warned = warned || p.message.find("as round as the box allows") != std::string::npos;
	EXPECT_TRUE(warned);
}

TEST(MoreShapesTest, ScalingATerrainScalesItsSizeButNotItsRoughnessOrHillCount) {
	Object t = makeObject(ShapeKind::Terrain, "Ground");
	const double roughness = t.radius, hills = t.radius2, width = t.size.x, steps = t.steps;
	scaleObject(t, 3.0);
	EXPECT_NEAR(t.size.x, width * 3.0, 1e-9);
	EXPECT_EQ(t.radius, roughness) << "roughness is 0 to 1, not a length";
	EXPECT_EQ(t.radius2, hills) << "the number of hills is not a length";
	EXPECT_EQ(t.steps, steps);
	Document d;
	d.objects.push_back(t);
	d.lights.push_back(Light());
	EXPECT_FALSE(hasErrors(validate(d)));
	Object box = makeObject(ShapeKind::RoundedBox, "Box");
	const double round = box.radius;
	scaleObject(box, 2.0);
	EXPECT_NEAR(box.radius, round * 2.0, 1e-9) << "a rounded box's roundness is a length and scales with it";
}

TEST(CycloramaTest, WithNoCurveTheFloorAndTheWallKeepTheirOwnNormals) {
	const ShapeMesh m = cycloramaMesh(8, 4, 6, 0.0);
	expectWellFormed(m, "sharp cyclorama");
	for (std::size_t t = 0; t + 2 < m.indices.size(); t += 3) {
		const int a = m.indices[t], b = m.indices[t + 1], c = m.indices[t + 2];
		for (int k = 0; k < 3; ++k) {
			EXPECT_NEAR(m.N[a * 3 + k], m.N[b * 3 + k], 1e-9) << "a triangle of a flat part has one normal";
			EXPECT_NEAR(m.N[a * 3 + k], m.N[c * 3 + k], 1e-9);
		}
	}
	EXPECT_NEAR(meshArea(m), 8.0 * (6.0 + 4.0), 1e-9) << "floor plus wall, nothing between";
}

// ---- The cone: pbrt-v4 has no cone shape (it went with pbrt-v3), so the Builder writes one as a mesh ----

TEST(ConeMeshTest, ItsAreaIsTheClosedFormItIsOpenAtTheBaseAndFacesOutward) {
	const double r = 0.8, h = 2.0;
	const ShapeMesh m = coneMesh(r, h);
	// The lateral surface of a cone: pi r sqrt(r^2 + h^2); the 96-sided polygon is a little under it.
	const double exact = kPi * r * std::sqrt(r * r + h * h);
	EXPECT_NEAR(meshArea(m), exact, 0.002 * exact);
	double lo[3] = {1e9, 1e9, 1e9}, hi[3] = {-1e9, -1e9, -1e9};
	for (std::size_t i = 0; i < m.P.size(); ++i) {
		lo[i % 3] = std::min(lo[i % 3], m.P[i]);
		hi[i % 3] = std::max(hi[i % 3], m.P[i]);
	}
	EXPECT_NEAR(lo[1], -h / 2, 1e-9) << "the base is at -height/2";
	EXPECT_NEAR(hi[1], h / 2, 1e-9) << "the apex at +height/2";
	EXPECT_NEAR(hi[0], r, 1e-9);
	EXPECT_NEAR(lo[0], -r, 1e-9);
	EXPECT_NEAR(hi[2], r, 1e-9);
	for (std::size_t t = 0; t + 2 < m.indices.size(); t += 3) {   // each triangle's own normal agrees with its vertices' (outward)
		const int a = m.indices[t], b = m.indices[t + 1], c = m.indices[t + 2];
		const double ux = m.P[b * 3] - m.P[a * 3], uy = m.P[b * 3 + 1] - m.P[a * 3 + 1], uz = m.P[b * 3 + 2] - m.P[a * 3 + 2];
		const double vx = m.P[c * 3] - m.P[a * 3], vy = m.P[c * 3 + 1] - m.P[a * 3 + 1], vz = m.P[c * 3 + 2] - m.P[a * 3 + 2];
		const double nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
		const double dot = nx * (m.N[a * 3] + m.N[b * 3] + m.N[c * 3]) + ny * (m.N[a * 3 + 1] + m.N[b * 3 + 1] + m.N[c * 3 + 1]) + nz * (m.N[a * 3 + 2] + m.N[b * 3 + 2] + m.N[c * 3 + 2]);
		ASSERT_GT(dot, 0.0) << "triangle " << t / 3 << " faces inward";
	}
	for (std::size_t v = 0; v < m.vertexCount(); ++v) {
		const double len = std::sqrt(m.N[v * 3] * m.N[v * 3] + m.N[v * 3 + 1] * m.N[v * 3 + 1] + m.N[v * 3 + 2] * m.N[v * 3 + 2]);
		ASSERT_NEAR(len, 1.0, 1e-9);
		// the exact cone normal: perpendicular to the slant line (radial * h, up * r)
		const double radial = std::hypot(m.P[v * 3], m.P[v * 3 + 2]);
		if (radial > 1e-6) ASSERT_NEAR(m.N[v * 3 + 1] * h - std::hypot(m.N[v * 3], m.N[v * 3 + 2]) * r, 0.0, 1e-9) << "vertex " << v;
	}
}

TEST(ConeMeshTest, ItsTextureCoordinatesAreTheOldAnalyticCones) {
	// pbrt-v3's cone: u = phi / phiMax with phi measured in the cone's own x-y plane (which became the scene's x and -z), v = 0 at the base and 1 at the apex.
	const ShapeMesh m = coneMesh(1.0, 2.0);
	for (std::size_t v = 0; v < m.vertexCount(); ++v) {
		const double x = m.P[v * 3], y = m.P[v * 3 + 1], z = m.P[v * 3 + 2];
		EXPECT_NEAR(m.UV[v * 2 + 1], (y + 1.0) / 2.0, 1e-9) << "v runs from the base to the apex";
		const double radial = std::hypot(x, z);
		if (radial < 1e-6) continue;   // (at the apex u is whatever the segment says)
		double phi = std::atan2(-z, x);
		if (phi < 0) phi += 2 * kPi;
		const double u = m.UV[v * 2];
		EXPECT_TRUE(std::fabs(u - phi / (2 * kPi)) < 1e-9 || std::fabs(u - 1.0 - phi / (2 * kPi)) < 1e-9 || std::fabs(u - (phi / (2 * kPi) - 1.0)) < 1e-9 || (u == 1.0 && phi < 1e-9)) << "vertex " << v;
	}
}

TEST(ConeMeshTest, ItIsWrittenAsAMeshNotAShapePbrtV4DoesNotHaveAndTheRendererLoadsIt) {
	Document d;
	d.objects.push_back(makeObject(ShapeKind::Cone, "Pine"));
	const std::string text = toPbrt(d);
	EXPECT_EQ(text.find("Shape \"cone\""), std::string::npos) << "pbrt-v4 has no cone shape (the document's own JSON line still says the object is a cone)";
	EXPECT_NE(text.find("trianglemesh"), std::string::npos);
	{
		std::ofstream out("cone_mesh_test.pbrt", std::ios::binary);
		out << text;
	}
	const pbrt_load::LoadResult r = pbrt_load::loadFile("cone_mesh_test.pbrt");
	std::remove("cone_mesh_test.pbrt");
	ASSERT_TRUE(r.ok) << r.error;
	EXPECT_TRUE(r.scene.cones.empty());
	EXPECT_EQ(r.scene.triangles.size(), coneMesh(0.8, 2.0).triangleCount());
	Document back;
	std::string err;
	ASSERT_TRUE(fromPbrt(text, back, err)) << err;
	EXPECT_EQ(toJson(back), toJson(d)) << "the file still opens in the Builder as a cone";
}

// A mesh cone and pbrt's own analytic cone (the shape the Builder wrote before, which this renderer still reads from a file) light a ball-sized scene the same, within what 96 flat sides cost.
TEST(ConeMeshTest, ItRendersLikeTheAnalyticConeItReplaced) {
	if (findRayTracerLauncher().empty()) GTEST_SKIP() << "ray_tracer.exe was not found next to the tests";
	const auto scene = [](const std::string& shape) {
		return "LookAt 3 2 5  0 0 0  0 1 0\nCamera \"perspective\" \"float fov\" [ 40 ]\nFilm \"rgb\" \"integer xresolution\" [ 48 ] \"integer yresolution\" [ 48 ]\n"
		       "Sampler \"independent\" \"integer pixelsamples\" [ 64 ]\nIntegrator \"path\" \"integer maxdepth\" [ 3 ]\nWorldBegin\n"
		       "LightSource \"infinite\" \"rgb L\" [ 0.5 0.6 0.8 ]\nLightSource \"distant\" \"point3 from\" [ 3 5 2 ] \"point3 to\" [ 0 0 0 ] \"rgb L\" [ 3 3 3 ]\n"
		       "AttributeBegin\n  Material \"diffuse\" \"rgb reflectance\" [ 0.7 0.5 0.3 ]\n" + shape + "AttributeEnd\n";
	};
	Document d;
	d.objects.push_back(makeObject(ShapeKind::Cone, "Cone"));
	d.objects[0].position = {0, 0, 0};
	d.objects[0].radius = 1.0;
	d.objects[0].height = 2.0;
	d.objects[0].material.color = {0.7, 0.5, 0.3};   // (the analytic scene's colour)
	const std::string written = toPbrt(d);
	const size_t at = written.find("AttributeBegin"), end = written.rfind("AttributeEnd");
	std::string mesh = written.substr(at, end - at);   // the Builder's own object block: Translate, material, mesh
	const size_t mat = mesh.find("Material");
	mesh = mesh.substr(mat);   // (drop AttributeBegin and Translate: placed at the origin)
	const std::string analytic = "  Material \"diffuse\" \"rgb reflectance\" [ 0.7 0.5 0.3 ]\n  Rotate -90 1 0 0\n  Translate 0 0 -1\n  Shape \"cone\" \"float radius\" [ 1 ] \"float height\" [ 2 ]\n";
	std::vector<float> a, b;
	int w = 0, h = 0, w2 = 0, h2 = 0;
	ASSERT_TRUE(renderPbrtTextCpu(scene(analytic), "cone_analytic", 48, a, w, h));
	ASSERT_TRUE(renderPbrtTextCpu(scene("  " + mesh), "cone_mesh", 48, b, w2, h2));
	double ma = 0, mb = 0;
	for (float v : a) ma += v;
	for (float v : b) mb += v;
	ma /= a.size();
	mb /= b.size();
	EXPECT_NEAR(mb, ma, 0.02 * ma) << "mean of the analytic cone's picture " << ma << ", the mesh's " << mb;
}
