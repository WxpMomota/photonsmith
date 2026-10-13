// gpu_tessellate_tests.cpp - src/shared/gpu_tessellate.h: the shapes a GPU backend cannot trace become triangles, and only the kinds the backend asks for.
#include <gtest/gtest.h>

#include <cmath>

#include "../../src/shared/gpu_tessellate.h"

using namespace gpu_tessellate;

namespace {

double triangleArea(const pbrt_flatten::Triangle& t) {
	const double ax = t.v[3] - t.v[0], ay = t.v[4] - t.v[1], az = t.v[5] - t.v[2];
	const double bx = t.v[6] - t.v[0], by = t.v[7] - t.v[1], bz = t.v[8] - t.v[2];
	const double cx = ay * bz - az * by, cy = az * bx - ax * bz, cz = ax * by - ay * bx;
	return 0.5 * std::sqrt(cx * cx + cy * cy + cz * cz);
}

double totalArea(const pbrt_flatten::FlatScene& s) {
	double a = 0;
	for (const auto& t : s.triangles) a += triangleArea(t);
	return a;
}

pbrt_flatten::FlatScene sceneWithEveryKind() {
	pbrt_flatten::FlatScene s;
	pbrt_flatten::BilinearPatch bp;   // a flat 2 x 3 rectangle: its area is exact
	const double c[4][3] = {{0, 0, 0}, {2, 0, 0}, {0, 3, 0}, {2, 3, 0}};
	for (int i = 0; i < 4; ++i)
		for (int a = 0; a < 3; ++a) bp.p[i][a] = c[i][a];
	s.bilinearPatches.push_back(bp);
	pbrt_flatten::Cone cone;   // radius 1, height 2
	cone.radius = 1.0;
	cone.height = 2.0;
	s.cones.push_back(cone);
	pbrt_flatten::Paraboloid pa;
	pa.radius = 1.0;
	pa.zMin = 0.0;
	pa.zMax = 1.0;
	s.paraboloids.push_back(pa);
	pbrt_flatten::Curve cv;   // one straight Bezier segment
	cv.nSegments = 1;
	cv.cp = {0, 0, 0, 0, 1, 0, 0, 2, 0, 0, 3, 0};
	cv.width0 = cv.width1 = 0.2;
	s.curves.push_back(cv);
	return s;
}

}  // namespace

TEST(GpuTessellateTest, AskingForEverythingTurnsEveryKindIntoTriangles) {
	pbrt_flatten::FlatScene s = sceneWithEveryKind();
	FiberTangents tangents;
	const size_t added = tessellateForBackend(s, TessellationCaps::all(), &tangents);
	EXPECT_EQ(added, s.triangles.size());
	// 16x16 patch cells, 48x4 cone cells, 48x24 paraboloid cells (2 triangles each) plus the curve's tube.
	EXPECT_GE(added, size_t(2 * (16 * 16 + 48 * 4 + 48 * 24)));
	EXPECT_TRUE(s.bilinearPatches.empty() && s.cones.empty() && s.paraboloids.empty() && s.curves.empty());
	EXPECT_FALSE(tangents.empty()) << "the curve's triangles carry their fibre direction";
	for (const auto& kv : tangents) {
		ASSERT_LT(static_cast<size_t>(kv.first), s.triangles.size());
		const auto& d = kv.second;
		EXPECT_NEAR(d[0] * d[0] + d[1] * d[1] + d[2] * d[2], 1.0, 1e-4);
	}
}

TEST(GpuTessellateTest, OnlyTheRequestedKindsAreRewritten) {
	pbrt_flatten::FlatScene s = sceneWithEveryKind();
	const size_t added = tessellateForBackend(s, TessellationCaps::conesAndParaboloids(), nullptr);
	EXPECT_EQ(added, size_t(2 * (48 * 4 + 48 * 24)));
	EXPECT_TRUE(s.cones.empty() && s.paraboloids.empty());
	EXPECT_EQ(s.bilinearPatches.size(), 1u) << "a backend with native patches keeps them";
	EXPECT_EQ(s.curves.size(), 1u) << "and its own curve dicing";
	pbrt_flatten::FlatScene none = sceneWithEveryKind();
	EXPECT_EQ(tessellateForBackend(none, TessellationCaps{}, nullptr), 0u);
	EXPECT_TRUE(none.triangles.empty());
}

TEST(GpuTessellateTest, AFlatPatchKeepsItsAreaAndAConeIsCloseToTheTrueOne) {
	pbrt_flatten::FlatScene patch = sceneWithEveryKind();
	patch.cones.clear();
	patch.paraboloids.clear();
	patch.curves.clear();
	tessellateForBackend(patch, TessellationCaps::all(), nullptr);
	EXPECT_NEAR(totalArea(patch), 6.0, 1e-9);   // 2 x 3
	pbrt_flatten::FlatScene cone = sceneWithEveryKind();
	cone.bilinearPatches.clear();
	cone.paraboloids.clear();
	cone.curves.clear();
	tessellateForBackend(cone, TessellationCaps::all(), nullptr);
	const double slant = std::sqrt(1.0 + 4.0);   // radius 1, height 2
	const double lateral = 3.14159265358979 * 1.0 * slant;
	EXPECT_NEAR(totalArea(cone), lateral, lateral * 0.01) << "within 1% of pi * r * slant";
}

TEST(GpuTessellateTest, AShapeOfAnInterfaceMaterialIsSkipped) {
	pbrt_flatten::FlatScene s = sceneWithEveryKind();
	pbrt_flatten::Material iface;
	iface.kind = pbrt_flatten::MaterialKind::Interface;
	s.materials.push_back(iface);
	s.cones[0].material = 0;
	s.paraboloids[0].material = 0;
	const size_t added = tessellateForBackend(s, TessellationCaps::all(), nullptr);
	EXPECT_LT(added, size_t(2 * (48 * 4 + 48 * 24)));   // neither quadric was turned into triangles; the patch and curve were
	EXPECT_TRUE(s.cones.empty() && s.paraboloids.empty()) << "they are dropped, not left for the backend";
}

TEST(GpuTessellateTest, MaterialAndAreaLightCarryOverToTheTriangles) {
	pbrt_flatten::FlatScene s = sceneWithEveryKind();
	s.cones[0].material = 3;
	s.cones[0].areaLight = 5;
	s.bilinearPatches.clear();
	s.paraboloids.clear();
	s.curves.clear();
	tessellateForBackend(s, TessellationCaps::all(), nullptr);
	ASSERT_FALSE(s.triangles.empty());
	for (const auto& t : s.triangles) {
		EXPECT_EQ(t.material, 3);
		EXPECT_EQ(t.areaLight, 5);
	}
}

namespace {

// A scene of one picture-textured disk and one picture-textured cylinder (material 0), plus a plain disk (material 1).
pbrt_flatten::FlatScene scenePicturedQuadrics() {
	pbrt_flatten::FlatScene s;
	pbrt_flatten::Material pictured;
	pictured.textureFilename = "wood.png";
	s.materials.push_back(pictured);
	s.materials.push_back(pbrt_flatten::Material());
	pbrt_flatten::Disk d;
	d.radius = 2.0;
	d.material = 0;
	s.disks.push_back(d);
	pbrt_flatten::Cylinder cy;
	cy.material = 0;
	s.cylinders.push_back(cy);
	d.material = 1;
	s.disks.push_back(d);
	return s;
}

}  // namespace

TEST(GpuTessellateTest, AnAnalyticQuadricWithAPictureBecomesTrianglesWithPbrtCoordinates) {
	pbrt_flatten::FlatScene s = scenePicturedQuadrics();
	const size_t added = tessellateForBackend(s, TessellationCaps::all(), nullptr);
	EXPECT_EQ(added, size_t(2 * (48 * 2 + 64 * 1)));   // the disk's 48 x 2 cells and the cylinder's 64 x 1
	ASSERT_EQ(s.disks.size(), 1u) << "only the plain disk stays analytic";
	EXPECT_EQ(s.disks[0].material, 1);
	EXPECT_TRUE(s.cylinders.empty());
	// pbrt's disk: v = 0 at the outer rim, 1 at the inner radius (here the centre); u runs round the circle
	for (size_t i = 0; i < size_t(2 * 48 * 2); ++i) {
		const pbrt_flatten::Triangle& t = s.triangles[i];
		ASSERT_TRUE(t.hasUVs);
		for (int c = 0; c < 3; ++c) {
			const double r = std::hypot(t.v[c * 3], t.v[c * 3 + 1]);
			EXPECT_NEAR(r, 2.0 * (1.0 - t.uv[c * 2 + 1]), 1e-9);
		}
	}
}

TEST(GpuTessellateTest, AQuadricWithoutAPictureOrCaughtByAnotherRuleStaysAnalytic) {
	pbrt_flatten::FlatScene s = scenePicturedQuadrics();
	s.disks[0].areaLight = 0;                       // a light is sampled analytically
	s.cylinders[0].xformEnd[3] = 1.0;               // a moving shape keeps its motion
	EXPECT_EQ(tessellateForBackend(s, TessellationCaps::all(), nullptr), 0u);
	EXPECT_EQ(s.disks.size(), 2u);
	EXPECT_EQ(s.cylinders.size(), 1u);
	s = scenePicturedQuadrics();
	EXPECT_EQ(tessellateForBackend(s, TessellationCaps::conesAndParaboloids(), nullptr), 0u) << "OptiX draws them natively";
	EXPECT_EQ(s.disks.size(), 2u);
}

TEST(GpuTessellateTest, ASphereWithABumpMapBecomesTrianglesWithTheProjectsSphereCoordinates) {
	pbrt_flatten::FlatScene s;
	pbrt_flatten::Material mapped;
	mapped.displacementTextureFilename = "bump.png";
	s.materials.push_back(mapped);
	s.materials.push_back(pbrt_flatten::Material());
	pbrt_flatten::Sphere sp;
	sp.center[0] = 1.0;
	sp.center[1] = 2.0;
	sp.center[2] = 3.0;
	sp.center1[0] = 1.0;
	sp.center1[1] = 2.0;
	sp.center1[2] = 3.0;
	sp.radius = 2.0;
	sp.material = 0;
	s.spheres.push_back(sp);
	sp.material = 1;   // a plain sphere stays analytic
	s.spheres.push_back(sp);
	const size_t added = tessellateForBackend(s, TessellationCaps::all(), nullptr);
	EXPECT_EQ(added, size_t(2 * 96 * 48));
	ASSERT_EQ(s.spheres.size(), 1u);
	EXPECT_EQ(s.spheres[0].material, 1);
	const double kPi = 3.14159265358979323846;
	for (size_t i = 0; i < s.triangles.size(); i += 97) {
		const pbrt_flatten::Triangle& t = s.triangles[i];
		ASSERT_TRUE(t.hasUVs && t.hasNormals);
		for (int c = 0; c < 3; ++c) {
			const double x = t.v[c * 3] - 1.0, y = t.v[c * 3 + 1] - 2.0, z = t.v[c * 3 + 2] - 3.0;
			EXPECT_NEAR(std::sqrt(x * x + y * y + z * z), 2.0, 1e-9);
			// the project's get_sphere_uv: theta = acos(-y / r) = pi v, phi = atan2(-z, x) + pi = 2 pi u
			EXPECT_NEAR(std::acos(-y / 2.0), kPi * t.uv[c * 2 + 1], 1e-9);
			if (std::fabs(y) < 1.99) {   // (away from the poles, where u is not defined)
				double phi = std::atan2(-z, x) + kPi, u = 2.0 * kPi * t.uv[c * 2];
				double d = std::fabs(phi - u);
				d = std::min(d, 2.0 * kPi - d);
				EXPECT_NEAR(d, 0.0, 1e-9);
			}
		}
	}
}

TEST(GpuTessellateTest, ASphereThatIsALightMovesOrIsClippedStaysAnalytic) {
	pbrt_flatten::FlatScene s;
	pbrt_flatten::Material mapped;
	mapped.displacementTextureFilename = "bump.png";
	s.materials.push_back(mapped);
	pbrt_flatten::Sphere sp;
	sp.material = 0;
	sp.areaLight = 0;
	s.spheres.push_back(sp);
	sp.areaLight = -1;
	sp.center1[0] = 0.5;   // moving
	s.spheres.push_back(sp);
	sp.center1[0] = 0.0;
	sp.clipped = true;
	s.spheres.push_back(sp);
	EXPECT_EQ(tessellateForBackend(s, TessellationCaps::all(), nullptr), 0u);
	EXPECT_EQ(s.spheres.size(), 3u);
}
